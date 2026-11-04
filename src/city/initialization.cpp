#include "city/initialization.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>
#include <set>

#include "advisors/ad_ini.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_date.h"
#include "city/ct_init_text.h"
#include "city/ct_scheme_manager_impl.h"
#include "city/module.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/manager_object.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/city_scheme.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// The legacy lead-byte call retains the upper bytes of its pointer-derived input while replacing only the low byte.
enum CtFilenameLeadByteMask : u32 {
  kCtFilenameLeadPointerHighBytesMask = 0xffffff00,
};

// City initialization commands identify dialog selection/results and load/save arguments separately.
enum CtInitCommandParameterId : u32 {
  kCtDialogSelectorParameterId = 0x10e8c85,
  kCtDialogAcceptedParameterId = 0x110e8c85,
  kCtDialogSelectedPathParameterId = 0x210e8c85,
  kCtSavePathParameterId = 0x810d2e74,
  kCtSaveSucceededParameterId = 0x43785228,
  kCtLoadModeParameterId = 0x10d2dff,
};

// Serialized keys retain their record-format role independently of live class and interface identities.
enum CtCityStyleArchiveKey : u32 {
  kCtCityStyleRecordType = 0x525a6b9,
  kCtCityStyleRecordGroup = 0x4525a6e2,
};

// File previews distinguish their placeholder artwork from the live map raster filter.
enum CtFilePreviewId : u32 {
  kCtFileMapPreviewFilterId = 0x82f3ace9,
  kCtFileCityPreviewPlaceholderImageId = 0x22bf46cb,
  kCtFileStarterTownPreviewPlaceholderImageId = 0xe34990e9,
};

// File-dialog control identities, established by their construction and metadata-update methods.
enum CtFileDialogControl : u32 {
  kCtFileAuthorLabel = 0x2dfdd75,
  kCtFileAuthorValue = 0x2dfdd76,
  kCtFilePreviewOutlineIdBase = 0x2dfdd7d,
  kCtFilePreviewPlaceholder = 0x2dfdd7f,
  kCtFilePreviewBitmap = 0x2dfdd80,
  kCtFilePreviewDescriptionLabel = 0x2dfdd81,
  kCtFileDialogWindow = 0x02dfdd67,
  kCtFileCancelButton = 0x02dfdd68,
  kCtFileAcceptButton = 0x02dfdd6a,
  kCtFileDirectoryCombo = 0x02dfdd6c,
  kCtFileNameEdit = 0x02dfdd6e,
  kCtFileList = 0x02dfdd6f,
  kCtFilePopulationLabel = 0x02dfdd70,
  kCtFileFundsLabel = 0x02dfdd71,
  kCtFileDateLabel = 0x02dfdd72,
  kCtFileMayorLabel = 0x02dfdd73,
  kCtFileCityLabel = 0x02dfdd74,
  kCtFileMayorNameEdit = 0x02dfdd77,
  kCtFileCityNameEdit = 0x02dfdd78,
  kCtFileDirectoryLabel = 0x02dfdd79,
  kCtFileNameLabel = 0x02dfdd7a,
  kCtFileShowStatistics = 0x02dfdd7b,
  kCtFileMapPreview = 0x02dfdd7c,
};

// Compare descendant first, then ancestor, matching the original pair-key map ordering.
bool CtAncestorKey::operator<(const CtAncestorKey &other) const {
  if (descendant != other.descendant)
    return descendant < other.descendant;
  return ancestor < other.ancestor;
}

// Create a registered agent type, set its tree label and resource key, then add it to the receiving tree.
// Release the factory reference after insertion, including when the tree already contains the identifier.
void SC3_CDECL CtReadInitAgentType(const ScStringView &key, const ScStringView &value, void *context) {
  ScString label;
  CtAgentType *node = nullptr;
  CtGetInitDirector()->GZCOM()->GetClassObject(kCtAgentTypeClassId, IID_CtAgentType, reinterpret_cast<void **>(&node));
  if (!node)
    return;
  node->SetId(ScConvertToUint32(key.ToChar()));
  ScString remaining(value);
  label = remaining.SplitToken(":");
  node->AsIGZTreeNode()->SetNodeText(&label);
  RsKey resource;
  label = remaining.SplitToken(",");
  resource.type = ScConvertToUint32(label.ToChar());
  label = remaining.SplitToken(",");
  resource.group = ScConvertToUint32(label.ToChar());
  resource.instance = ScConvertToUint32(remaining.ToChar());
  node->SetName(resource);
  static_cast<CtAgentTreeImpl *>(context)->AddNode(node);
  node->Release();
}

// Translate the two registered identifiers and link the corresponding child and parent types.
// Registry enumeration uses the SDK's void callback contract and discards the link operation's result.
void SC3_CDECL CtReadInitAgentHierarchy(const ScStringView &key, const ScStringView &value, void *context) {
  u32 child = ScConvertToUint32(key.ToChar());
  u32 parent = ScConvertToUint32(value.ToChar());
  static_cast<CtAgentTreeImpl *>(context)->LinkNodes(child, parent);
}

CtAgentTreeImpl::CtAgentTreeImpl() : initialized(false), root(nullptr), references(0) {
}

CtAgentTreeImpl::~CtAgentTreeImpl() {
  if (initialized)
    CtAgentTreeImpl::Shutdown();
}

// Publish and retain the tree for either its own interface or the Unknown identity.
bool CtAgentTreeImpl::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtAgentTree);
  HANDLE_QUERY_INTERFACE(IID_CtAgentTree, CtAgentTree);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the tree reference count before retaining its SIMINIT director.
u32 CtAgentTreeImpl::AddRef() {
  ++references;
  CtGetInitDirector()->AddRef();
  return references;
}

// Release a positive reference and its director retain, then destroy the concrete tree when the count reaches zero.
u32 CtAgentTreeImpl::Release() {
  if (references) {
    CtGetInitDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Enumerate registered agent types, choose type zero as root, then enumerate hierarchy links.
bool CtAgentTreeImpl::Init() {
  if (initialized)
    Shutdown();
  ScRegistry *registry = nullptr;
  CtGetInitFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  bool result = false;
  if (registry->EnumCategory("sc3agenttype", CtReadInitAgentType, this)) {
    root = GetNode(0);
    if (registry->EnumCategory("sc3typehierarchy", CtReadInitAgentHierarchy, this)) {
      initialized = true;
      result = true;
    }
  }
  if (registry)
    registry->Release();
  return result;
}

// Release each retained type in identifier order, clear the node map and reset the active root and flag.
// The separately stored ancestry cache is not cleared by this installed shutdown method.
bool CtAgentTreeImpl::Shutdown() {
  if (initialized) {
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
      it->second->Release();
    nodes.clear();
    root = nullptr;
    initialized = false;
  }
  return true;
}

// Insert a type by its original identifier and retain it only when that identifier was not already present.
bool CtAgentTreeImpl::AddNode(CtAgentType *node) {
  u32 id = node->Id();
  auto inserted = nodes.emplace(id, node);
  if (!inserted.second)
    return false;
  node->AddRef();
  return true;
}

// Link an existing child to an existing parent, placing the child at the head of the parent's sibling chain.
bool CtAgentTreeImpl::LinkNodes(u32 child, u32 parent) {
  auto childEntry = nodes.find(child);
  if (childEntry == nodes.end())
    return false;
  auto parentEntry = nodes.find(parent);
  if (parentEntry == nodes.end())
    return false;
  CtAgentType *childNode = childEntry->second, *parentNode = parentEntry->second;
  childNode->SetParent(parentNode);
  childNode->SetSibling(parentNode->FirstChild());
  parentNode->SetFirstChild(childNode);
  return true;
}

u32 CtAgentTreeImpl::Count() {
  return initialized ? static_cast<u32>(nodes.size()) : ~u32(0);
}

// Borrow the stored root pointer without changing its ownership.
CtAgentType *CtAgentTreeImpl::GetRootNode() {
  return root;
}

// Borrow the type associated with the requested identifier, or return null when it is absent.
CtAgentType *CtAgentTreeImpl::GetNode(u32 id) {
  auto found = nodes.find(id);
  return found == nodes.end() ? nullptr : found->second;
}

bool CtAgentTreeImpl::IsAncestor(u32 ancestor, u32 descendant) {
  u32 distance = 0;
  return IsAncestor(ancestor, descendant, &distance);
}

// Resolve and cache whether an ancestor is reachable by following the descendant's parent links.
// On a failed traversal the output keeps the number of links visited, while cached failures leave it unchanged.
bool CtAgentTreeImpl::IsAncestor(u32 ancestor, u32 descendant, u32 *distance) {
  CtAncestorKey key{descendant, ancestor};
  CtAncestorResult cached = ancestry[key];
  if (cached.state == kCtAncestorFound) {
    *distance = cached.distance;
    return true;
  }
  if (cached.state == kCtAncestorMissing)
    return false;
  CtAgentType *first = GetNode(ancestor);
  if (first) {
    CtAgentType *current = GetNode(descendant);
    if (current) {
      *distance = 0;
      while (current != first) {
        current = current->Parent();
        ++*distance;
        if (!current) {
          CtAncestorResult &entry = ancestry[key];
          entry.state = kCtAncestorMissing;
          entry.distance = cached.distance;
          return false;
        }
      }
      CtAncestorResult &entry = ancestry[key];
      entry.state = kCtAncestorFound;
      entry.distance = *distance;
      return true;
    }
  }
  CtAncestorResult &entry = ancestry[key];
  entry.state = kCtAncestorMissing;
  entry.distance = cached.distance;
  return false;
}

// Construct and retain an iterator at the first stored type, returning it through the original output pointer.
bool CtAgentTreeImpl::CreateNodeIterator(CtAgentTreeIterator **out) {
  *out = new CtAgentTreeIteratorImpl(this);
  (*out)->AddRef();
  return true;
}

static u32 CtInitAgentDebugDepth;

// Resolve each visited type's name resource and traverse children and siblings in the installed recursive order.
// The original indentation counter changes around child traversal even though its logging operation is absent.
void CtAgentTreeImpl::DebugDumpTree(CtAgentType *node) {
  if (!node)
    node = root;
  CtInitText text;
  if (node == root)
    CtInitAgentDebugDepth = 0;
  RsKey name;
  node->GetName(&name);
  {
    CtInitText source(name.instance, name.group);
    text = source;
  }
  if (node->FirstChild() && !node->Sibling()) {
    CtInitAgentDebugDepth += 2;
    DebugDumpTree(node->FirstChild());
    CtInitAgentDebugDepth -= 2;
  } else if (node->Sibling() && !node->FirstChild()) {
    DebugDumpTree(node->Sibling());
  } else if (node->Sibling() && node->FirstChild()) {
    CtInitAgentDebugDepth += 2;
    DebugDumpTree(node->FirstChild());
    CtInitAgentDebugDepth -= 2;
    DebugDumpTree(node->Sibling());
  }
}

CtAgentTreeIteratorImpl::CtAgentTreeIteratorImpl(CtAgentTreeImpl *owner)
    : tree(owner), current(owner->nodes.begin()), end(owner->nodes.end()), references(0) {
}

bool CtAgentTreeIteratorImpl::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtAgentTreeIterator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the iterator count before retaining the SIMINIT director.
u32 CtAgentTreeIteratorImpl::AddRef() {
  ++references;
  CtGetInitDirector()->AddRef();
  return references;
}

// Release a positive iterator and director reference, deleting the receiver whenever its resulting count is zero.
u32 CtAgentTreeIteratorImpl::Release() {
  if (references) {
    CtGetInitDirector()->RemoveRef();
    --references;
  }
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Return the current borrowed type and advance the ordered-map iterator, or return null at the end.
CtAgentType *CtAgentTreeIteratorImpl::Next() {
  if (current == end)
    return nullptr;
  CtAgentType *value = current->second;
  ++current;
  return value;
}

// The installed key-list secondary base retains SIMINIT's director for each acquired reference.
class RsInitKeyRef : public ScUnknown {
public:
  RsInitKeyRef();
  virtual ~RsInitKeyRef();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 RemoveRef();
  virtual u32 RefCount();

private:
  u32 references;
};

// SIMINIT embeds this actual resource-key-list implementation while reading segment contents.
// Its ordered std::set matches the installed tree's group, instance and type ordering.
class RsInitKeyList final : public RsKeyListInterface, public RsInitKeyRef {
public:
  RsInitKeyList();
  ~RsInitKeyList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Insert(const RsKey &key) override;
  bool Insert(const RsKeyListInterface &source) override;
  bool Erase(const RsKey &key) override;
  bool EraseAll() override;
  void EnumKeys(void (*callback)(const RsKey &, void *), void *context) const override;
  bool IsPresent(const RsKey &key) override;
  u32 Size() const override;
  RsKey *GetKey(u32 index) override;

private:
  static void InsertKeyCallback(const RsKey &key, void *context);
  std::set<RsKey, OcInitKeyLess> keys;
  RsKey endKey; // Stable address corresponding to the original set's end-node payload.
};

RsInitKeyRef::RsInitKeyRef() : references(0) {
}

RsInitKeyRef::~RsInitKeyRef() {
}

bool RsInitKeyRef::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsInitKeyRef::AddRef() {
  CtGetInitDirector()->AddRef();
  return ++references;
}

// Release the module reference and delete the complete object for a final reference.
u32 RsInitKeyRef::Release() {
  bool last = references == 1;
  CtGetInitDirector()->RemoveRef();
  if (last) {
    delete this;
    return 0;
  }
  return --references;
}

// Remove a positive reference and its module retain without deleting the object.
u32 RsInitKeyRef::RemoveRef() {
  if (references) {
    CtGetInitDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 RsInitKeyRef::RefCount() {
  return references;
}

RsInitKeyList::RsInitKeyList() {
}

RsInitKeyList::~RsInitKeyList() {
}

bool RsInitKeyList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsKeyListInterface, RsKeyListInterface);
  return RsInitKeyRef::QueryInterface(iid, out);
}

u32 RsInitKeyList::AddRef() {
  return RsInitKeyRef::AddRef();
}

u32 RsInitKeyList::Release() {
  return RsInitKeyRef::Release();
}

bool RsInitKeyList::Insert(const RsKey &key) {
  keys.insert(key);
  return true;
}

void RsInitKeyList::InsertKeyCallback(const RsKey &key, void *context) {
  static_cast<RsInitKeyList *>(context)->keys.insert(key);
}

bool RsInitKeyList::Insert(const RsKeyListInterface &source) {
  source.EnumKeys(InsertKeyCallback, this);
  return true;
}

bool RsInitKeyList::Erase(const RsKey &key) {
  keys.erase(key);
  return true;
}

bool RsInitKeyList::EraseAll() {
  keys.clear();
  return true;
}

void RsInitKeyList::EnumKeys(void (*callback)(const RsKey &, void *), void *context) const {
  for (const RsKey &key : keys)
    callback(key, context);
}

bool RsInitKeyList::IsPresent(const RsKey &key) {
  return keys.find(key) != keys.end();
}

u32 RsInitKeyList::Size() const {
  return static_cast<u32>(keys.size());
}

// Borrow the ordered key at an in-range index, or the stable end-node address at the count.
// The original end-node payload is uninitialized; consuming its contents is outside the defined domain.
RsKey *RsInitKeyList::GetKey(u32 index) {
  assert(index <= keys.size());
  if (index == keys.size())
    return &endKey;
  auto entry = keys.begin();
  while (index--)
    ++entry;
  return const_cast<RsKey *>(&*entry);
}

// Compare resource keys in the ordering used by the installed attribute map.
bool OcInitKeyLess::operator()(const RsKey &left, const RsKey &right) const {
  if (left.group != right.group)
    return left.group < right.group;
  if (left.instance != right.instance)
    return left.instance < right.instance;
  return left.type < right.type;
}

OcInitAttribCache::OcInitAttribCache()
    : initialized(false), references(0), test(nullptr), state(0), agentTree(nullptr), random(~u32(0)) {
}

OcInitAttribCache::~OcInitAttribCache() {
  if (initialized)
    OcInitAttribCache::Shutdown();
}

// Return the cache interface for either supported identity and retain it for the caller.
bool OcInitAttribCache::QueryInterface(u32 iid, void **out) {
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcAttribCache);
  HANDLE_QUERY_INTERFACE(IID_OcAttribCache, OcAttribCache);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 OcInitAttribCache::AddRef() {
  return ++references;
}

u32 OcInitAttribCache::Release() {
  return ScReleaseImpl(this, &references);
}

// The original registry enumeration parses a resource key and adds it without rebuilding the index per entry.
// The registry's value is unused, and invalid key text contributes no attribute.
void SC3_CDECL OcReadInitRegistryAttrib(const ScStringView &key, const ScStringView &, void *context) {
  if (!context)
    return;
  RsKey parsed;
#if defined(_MSC_VER)
  if (sscanf_s(key.ToChar(), "0x%x,0x%x,0x%x", &parsed.type, &parsed.group, &parsed.instance) == 3)
#else
  if (sscanf(key.ToChar(), "0x%x,0x%x,0x%x", &parsed.type, &parsed.group, &parsed.instance) == 3)
#endif
    static_cast<OcInitAttribCache *>(context)->AddAttribInternal(parsed, false);
}

void SC3_CDECL OcReadInitSegment(const ScString &, const ScString &value, void *owner) {
  if (owner)
    static_cast<OcInitAttribCache *>(owner)->segments.push_back(ScConvertToUint32(value.Data()));
}

void SC3_CDECL OcReadInitSegmentKey(const RsKey &key, void *context) {
  if (context)
    static_cast<OcInitAttribCache *>(context)->AddAttribInternal(key, false);
}

// Read configured attribute segment identifiers from the loose or packaged system INI and load their resource keys.
// Each acquired segment is released after its ordered key list has been traversed.
u8 OcInitAttribCache::LoadSegments() {
  RsManager *resources = CtGetInitResourceManager();
  if (!resources || !initialized)
    return 0;
  ScString root(CtGetInitFileSystem()->DataDirectory());
  ScString loose(root), package(root);
  loose.Append("Sys\\Default.ini");
  package.Append("Sys\\SYS.PAK");
  AdIni ini;
  ini.SetPath(loose);
  ini.SetCompressedPath(package);
  ini.EnumEntries(ScString("OccAttribCache"), OcReadInitSegment, static_cast<OcAttribCache *>(this));
  for (u32 index = 0; index < segments.size(); ++index) {
    RsDatabaseSegment *segment = nullptr;
    if (resources->FindDBSegment(segments[index], &segment)) {
      RsInitKeyList keys;
      if (segment->GetResourceKeyList(&keys) && keys.Size())
        keys.EnumKeys(OcReadInitSegmentKey, this);
      segment->Release();
    }
  }
  return 1;
}

// Borrow the definition's agent tree and create the occupant filter before reading configured attributes.
// An unsuccessful tree or registry lookup leaves the initialized cache state for the normal shutdown path.
bool OcInitAttribCache::Init(CtDefinition *source) {
  if (!source)
    return false;
  definition = source;
  if (initialized)
    Shutdown();
  initialized = true;
  test = ScAddRefNotNull(new OcInitAttribTest);
  agentTree = source->GetAgentTypeTree();
  if (!agentTree)
    return false;
  LoadSegments();
  if (!CtGetInitRegistry()->EnumCategory("sc3occattrib", OcReadInitRegistryAttrib, this))
    return false;
  RebuildBuildingIndex();
  return true;
}

// Clear the building index and records, release cached attributes in key order, then release the occupant filter.
bool OcInitAttribCache::Shutdown() {
  if (initialized) {
    initialized = false;
    ClearBuildingIndex();
    records.clear();
    for (auto &attribute : attributes) {
      if (attribute.second)
        attribute.second->Release();
      attribute.second = nullptr;
    }
    attributes.clear();
    ScRelease(&test);
  }
  return true;
}

bool OcInitAttribCache::GetOccupantAttrib(const RsKey &key, GeAttrib **out) {
  return GetGenericAttrib(key, IID_GeAttrib, reinterpret_cast<void **>(out));
}

i32 OcInitAttribCache::Count() {
  return initialized ? static_cast<i32>(records.size()) : -1;
}

// Borrow the attribute stored for the complete resource key, or return null if the key is absent.
GeAttrib *OcInitAttribCache::FindAttrib(const RsKey &key) {
  auto found = attributes.find(key);
  return found == attributes.end() ? nullptr : found->second;
}

u8 OcInitAttribCache::GetBuildingAttrib(const RsKey &key, GeBuildingType **out) {
  return GetGenericAttrib(key, IID_GeBuildingType, reinterpret_cast<void **>(out));
}

// Find the first record with the requested building type and dispatch its key through the virtual building lookup.
bool OcInitAttribCache::GetBuildingAttrib(u32 type, GeBuildingType **out) {
  for (const OcInitAttribRecord &record : records)
    if (record.type == type)
      return GetBuildingAttrib(record.key, out) != 0;
  return false;
}

// Find the keyed attribute and ask it for the requested interface when present.
bool OcInitAttribCache::GetGenericAttrib(const RsKey &key, u32 iid, void **out) {
  GeAttrib *attribute = FindAttrib(key);
  if (!attribute)
    return false;
  return attribute->QueryInterface(iid, out);
}

// Choose an indexed building of the requested type and size, or scan matching ancestors when no type index exists.
bool OcInitAttribCache::GetABuildingOfType(u32 type, u32 size, RsKey *out) {
  auto entry = buildings.find(type);
  if (entry == buildings.end()) {
    for (const OcInitAttribRecord &record : records) {
      if (agentTree->IsAncestor(type, record.type) && record.sizeX == size && record.sizeZ == size) {
        *out = record.key;
        return true;
      }
    }
    return false;
  }
  OcInitSizeMap *sizes = entry->second;
  if (!sizes)
    return false;
  auto matching = sizes->find(OcInitSizeKey(size, size));
  if (matching == sizes->end() || !matching->second || matching->second->empty())
    return false;
  const std::vector<u32> &indices = *matching->second;
  *out = records[indices[random.Bounded(static_cast<u32>(indices.size()))]].key;
  return true;
}

// Construct and retain a borrowed-cache iterator at the first keyed attribute.
OcCacheIterator *OcInitAttribCache::CreateNodeIterator() {
  OcInitCacheIterator *iterator = new OcInitCacheIterator(this);
  iterator->AddRef();
  return iterator;
}

void OcInitAttribCache::DeleteNodeIterator(OcCacheIterator *iterator) {
  iterator->Release();
}

void OcInitAttribCache::AddAttrib(const RsKey &key) {
  AddAttribInternal(key, true);
}

// Reload the requested attribute, retaining it in the cache and recording its type and footprint on first insertion.
// Replacement updates affected occupants before releasing the old attribute; rebuild the building index when requested.
bool OcInitAttribCache::AddAttribInternal(const RsKey &key, bool rebuild) {
  RsManager *resources = CtGetInitResourceManager();
  if (!resources)
    return false;
  resources->RemoveResource(key);
  GeAttrib *attribute = nullptr;
  if (!resources->GetResource(key, IID_GeAttrib, reinterpret_cast<void **>(&attribute)))
    return false;
  auto old = attributes.find(key);
  bool inserted;
  if (old != attributes.end()) {
    GeAttrib *previous = old->second;
    attributes.erase(old);
    inserted = attributes.emplace(key, attribute).second;
    ReplaceAttribute(key, previous, attribute);
    previous->Release();
  } else {
    inserted = attributes.emplace(key, attribute).second;
    if (inserted) {
      OcInitAttribRecord record;
      record.key = key;
      record.type = attribute->TypeId();
      record.unknown = 50;
      record.sizeX = attribute->SizeInCellsX();
      record.sizeZ = attribute->SizeInCellsZ();
      records.push_back(record);
    }
  }
  if (inserted)
    attribute->AddRef();
  if (rebuild)
    RebuildBuildingIndex();
  attribute->Release();
  return inserted;
}

void SC3_CDECL OcReadInitAttribKey(const RsKey &key, void *context) {
  if (key.type > 1 && (key.type <= 0x62ff || (key.type > 0x6301 && key.type != kRsTextResourceType)) && context)
    static_cast<OcInitAttribCache *>(context)->AddAttribInternal(key, false);
}

// Add each eligible key without rebuilding the building index between entries, then rebuild it once.
// An empty key list returns false without invoking the enumeration callback.
bool OcInitAttribCache::AddAttribs(const RsKeyListInterface &keys) {
  if (!keys.Size())
    return false;
  keys.EnumKeys(OcReadInitAttribKey, this);
  RebuildBuildingIndex();
  return true;
}

// Rebuild the installed building-type and footprint index after releasing every previously allocated size bucket.
u32 OcInitAttribCache::RebuildBuildingIndex() {
  ClearBuildingIndex();
  static constexpr u32 types[] = {3000, 3100, 3300, 3500, 3700, 3900, 4100, 4300, 4500, 4700, 5000, 5100, 5500, 5700,
      6100, 6300, 6700, 8000, 8100, 8300, 8500, 15410, 15411, 15420, 15421};
  for (u32 type : types) {
    OcInitSizeMap *sizes = new OcInitSizeMap;
    buildings[type] = sizes;
    for (u32 size = 1; size <= 5; ++size)
      (*sizes)[OcInitSizeKey(size, size)] = new std::vector<u32>;
  }
  u32 count = static_cast<u32>(records.size());
  for (u32 index = 0; index < count; ++index) {
    const OcInitAttribRecord &record = records[index];
    u32 type = record.type;
    OcInitSizeKey footprint(record.sizeX, record.sizeZ);
    for (auto &building : buildings) {
      if (!agentTree->IsAncestor(building.first, type))
        continue;
      auto found = building.second->find(footprint);
      if (found != building.second->end())
        found->second->push_back(index);
    }
  }
  return count;
}

// Delete every footprint vector and type map before clearing the building index.
// The installed method returns one after all nested allocations have been released.
u8 OcInitAttribCache::ClearBuildingIndex() {
  for (auto &building : buildings) {
    OcInitSizeMap *sizes = building.second;
    for (auto &size : *sizes)
      delete size.second;
    sizes->clear();
    delete sizes;
  }
  buildings.clear();
  return 1;
}

// Post the occupant-attribute change notification when the message service exists.
void OcInitAttribCache::PostOccAttribChangedMessage() {
  ScMessageServer *server = CtGetInitMessageServer();
  if (server)
    server->MessagePost(ScMessage(kOcMsg_AttributesChanged), false);
}

// Clear every present override in resource-key order through the original attribute-update operation.
bool OcInitAttribCache::ResetAllModifiedAttribs() {
  for (const auto &entry : attributes)
    if (entry.second->GetOverRideAttrib())
      ModifyAttribInternal(entry.second, nullptr);
  return true;
}

// Retain matching occupants before closing the iterator, then remove those occupants from their manager.
// A null manager contributes no occupants; the installed filter is the cache's owned filter.
void OcInitAttribCache::CollectMatchingOccupants(
    ScOccupantManager *manager, OcInitAttribTest *, std::vector<ScOccupant *> *occupants) {
  if (!manager)
    return;
  OcIterator *iterator = nullptr;
  manager->GetOccupants(&iterator, nullptr, test);
  ScOccupant *occupant = iterator->Reset();
  while (occupant) {
    occupant->AddRef();
    occupants->push_back(occupant);
    occupant = iterator->Next();
  }
  iterator->Shutdown();
  iterator->Release();
  for (ScOccupant *item : *occupants)
    manager->RemoveOccupant(item);
}

// Find the keyed cached attribute and apply the supplied override when that key exists.
u8 OcInitAttribCache::ModifyOccupantAttrib(const RsKey &key, OcOverride *replacement) {
  auto found = attributes.find(key);
  if (found == attributes.end())
    return 0;
  return ModifyAttribInternal(found->second, replacement);
}

// Pause and lock the city while matching occupants are removed, updated with the modified attribute and reinserted.
// Without a city, apply the override directly; a null attribute is rejected after the city lookup.
bool OcInitAttribCache::ModifyAttribInternal(GeAttrib *attribute, OcOverride *replacement) {
  std::vector<ScOccupant *> occupants[3];
  CtCity *city = CtGetInitCity();
  if (!attribute)
    return false;
  if (!city) {
    attribute->SetOverRideAttrib(replacement);
    return true;
  }
  city->SimHiddenPause();
  city->Lock();
  ScOccupantManager *managers[3] = {nullptr, nullptr, nullptr};
  CtCity *current = CtGetInitCity();
  if (current) {
    managers[0] = current->SurfaceOccupantManager();
    managers[1] = current->UndergroundOccupantManagerLevelOne();
    managers[2] = current->UndergroundOccupantManagerLevelTwo();
  }
  test->key = *attribute->Key();
  for (u32 i = 0; i < 3; ++i)
    CollectMatchingOccupants(managers[i], test, &occupants[i]);
  attribute->SetOverRideAttrib(replacement);
  for (u32 i = 0; i < 3; ++i) {
    for (ScOccupant *occupant : occupants[i])
      occupant->Init(attribute);
    if (managers[i]) {
      for (ScOccupant *occupant : occupants[i]) {
        managers[i]->InsertOccupant(occupant);
        occupant->Release();
      }
      occupants[i].clear();
    }
  }
  city->Unlock();
  city->SimHiddenUnpause();
  return true;
}

// Replace the attribute on matching occupants while the city is paused and locked, then restore each manager's entries.
// Occupants also present in an earlier manager are not initialized a second time.
void OcInitAttribCache::ReplaceAttribute(const RsKey &key, GeAttrib *, GeAttrib *replacement) {
  std::vector<ScOccupant *> occupants[3];
  CtCoord location = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  CtCity *city = CtGetInitCity();
  if (!city)
    return;
  city->SimHiddenPause();
  city->Lock();
  ScOccupantManager *managers[3] = {nullptr, nullptr, nullptr};
  CtCity *current = CtGetInitCity();
  if (current) {
    managers[0] = current->SurfaceOccupantManager();
    managers[1] = current->UndergroundOccupantManagerLevelOne();
    managers[2] = current->UndergroundOccupantManagerLevelTwo();
  }
  test->key = key;
  for (u32 i = 0; i < 3; ++i)
    CollectMatchingOccupants(managers[i], test, &occupants[i]);
  for (u32 i = 0; i < 3; ++i) {
    for (ScOccupant *occupant : occupants[i]) {
      bool alreadyUpdated = false;
      if (i) {
        occupant->GetLocation(&location);
        for (u32 previous = 0; previous < i; ++previous) {
          ScOccupant *found = nullptr;
          if (managers[previous]->GetOccupant(location, &found)) {
            bool same = found == occupant;
            found->Release();
            if (same) {
              alreadyUpdated = true;
              break;
            }
          }
        }
      }
      if (!alreadyUpdated)
        occupant->Init(replacement);
    }
    if (managers[i]) {
      for (ScOccupant *occupant : occupants[i]) {
        managers[i]->InsertOccupant(occupant);
        occupant->Release();
      }
      occupants[i].clear();
    }
  }
  city->Unlock();
  city->SimHiddenUnpause();
}

OcInitCacheIterator::OcInitCacheIterator(OcInitAttribCache *owner)
    : cache(owner), current(owner->attributes.begin()), end(owner->attributes.end()), references(0) {
}

bool OcInitCacheIterator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcCacheIterator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 OcInitCacheIterator::AddRef() {
  return ++references;
}

u32 OcInitCacheIterator::Release() {
  return ScReleaseImpl(this, &references);
}

// Borrow the next mapped attribute, advancing in the installed key order.
GeAttrib *OcInitCacheIterator::GetNext() {
  if (current == end)
    return nullptr;
  GeAttrib *attribute = current->second;
  ++current;
  return attribute;
}

OcInitAttribTest::OcInitAttribTest() : references(0), key() {
}

OcInitAttribTest::~OcInitAttribTest() {
}

// Publish the filter for its own identity or Unknown and retain it on success.
bool OcInitAttribTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, OcIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 OcInitAttribTest::AddRef() {
  return ++references;
}

u32 OcInitAttribTest::Release() {
  return ScReleaseImpl(this, &references);
}

// Match an occupant only when its attribute's complete resource key equals the selected key.
bool OcInitAttribTest::Test(ScOccupant *occupant) {
  GeAttrib *attribute = occupant->GetAttribData();
  if (!attribute)
    return false;
  RsKey *candidate = attribute->Key();
  return candidate->type == key.type && candidate->group == key.group && candidate->instance == key.instance;
}

CtInitCursorScope::CtInitCursorScope(u32 identifier) : pushed(0) {
  WnWindowManager *windows = CtGetInitWindowManager();
  if (windows) {
    WnCursorManager *cursors = windows->GetCursorManager();
    if (cursors)
      pushed = cursors->PushAutoCursor(identifier);
  }
}

// Pop a successful automatic-cursor push through freshly acquired window and cursor managers.
CtInitCursorScope::~CtInitCursorScope() {
  if (pushed) {
    WnWindowManager *windows = CtGetInitWindowManager();
    if (windows) {
      WnCursorManager *cursors = windows->GetCursorManager();
      if (cursors)
        cursors->PopAutoCursor();
    }
  }
}

u32 CtInitCommand::AddRef() {
  CtGetInitDirector()->AddRef();
  return ++references;
}

u32 CtInitCommand::Release() {
  return UiCommandOwner::Release();
}

// Remove a positive command reference after releasing this module; an initial zero remains unchanged.
u32 CtInitCommand::RemoveRef() {
  if (references) {
    CtGetInitDirector()->RemoveRef();
    --references;
  }
  return references;
}

bool CtInitShutdownCommand::Execute() {
  CtGetInitApplication()->ShutdownCity();
  return true;
}

CtInitDialogCommand::CtInitDialogCommand() {
  AddInputParameter(3, kCtDialogSelectorParameterId);
  AddOutputParameter(11, kCtDialogAcceptedParameterId);
  AddOutputParameter(0x800c, kCtDialogSelectedPathParameterId);
}

// Queue the accepted byte and, when supplied, a copy of the selected path.
u32 CtInitDialogCommand::PublishResult(u8 accepted, const ScStringView *path) {
  ScValue *result = new ScValue(false);
  result->value.unsigned8 = accepted;
  bool failed = !PushBackOutput(result);
  if (path) {
    ScValue *name = new ScValue(*path);
    failed |= !PushBackOutput(name);
  }
  return !failed;
}

// Run the requested city-file dialog while the current city is hidden-paused, then publish its selected path.
// Early failures publish a false result; the installed window-construction failure retains its dialog reference.
bool CtInitDialogCommand::Execute() {
  ScString path;
  WnWindowManager *windows = CtGetInitWindowManager();
  CtCity *city = CtGetInitCity();
  if ((!city || city->CanPlayerLoadSave()) && windows) {
    ScVariant *argument = nullptr;
    if (PopFrontInput(&argument)) {
      u32 mode = *argument->AsUint32();
      argument->Release();
      CtInitFileDialog *dialog = new CtInitFileDialog(mode);
      dialog->AddRef();
      WnWindow *window = nullptr;
      if (dialog->BuildWindow(&window)) {
        if (city)
          city->SimHiddenPause();
        bool accepted = windows->DoModalWin(window) == kCtFileAcceptButton;
        path = dialog->output;
        window->Shutdown();
        window->Release();
        if (accepted)
          PublishResult(1, &path);
        else
          PublishResult(0, nullptr);
        if (city)
          city->SimHiddenUnpause();
        dialog->Release();
        return accepted;
      }
    }
  }
  PublishResult(0, nullptr);
  return false;
}

CtInitSaveCommand::CtInitSaveCommand() {
  AddInputParameter(0x800c, kCtSavePathParameterId);
  AddOutputParameter(11, kCtSaveSucceededParameterId);
}

// Resolve the queued save argument, pause the current city while saving, and publish the Boolean result.
// The underlying save-path operation remains the original virtual dependency.
bool CtInitSaveCommand::Execute() {
  ScString path;
  CtCity *city = CtGetInitCity();
  if (city && !city->CanPlayerLoadSave())
    return false;
  if (CtInitHasCity() == 1)
    CtGetInitCity()->SimHiddenPause();
  bool result;
  if (!InputQueueSize()) {
    result = SavePath(&path);
  } else {
    ScVariant *argument;
    PopFrontInput(&argument);
    if (argument->GetType() == kScValueUint32) {
      if (!*argument->AsUint32()) {
        city = CtGetInitCity();
        if (city)
          path.Copy(*city->GetFilePath());
      }
      result = SavePath(&path);
    } else if (argument->GetType() == kScValueCharArray) {
      u32 count = argument->GetCount();
      char *data = argument->RefRZChar();
      path.FromChar(data, count);
      result = SavePath(&path);
    } else {
      result = false;
    }
    if (argument)
      argument->Release();
  }
  if (CtInitHasCity())
    CtGetInitCity()->SimHiddenUnpause();
  PushBackOutput(new ScValue(result));
  return result;
}

static void CtInitLowerSaveString(ScString *value) {
  value->MakeLower();
}

// Normalize a localized save extension and append a period when its first byte is not one.
static void CtInitPrepareSaveExtension(ScString *value) {
  CtInitLowerSaveString(value);
  if (value->begin[0] != '.')
    value->Append(".");
}

// Read the native file-operation error immediately after a failed save step.
static u32 CtInitSavePlatformError() {
  return static_cast<u32>(errno);
}

// Select and validate a save filename, write through a temporary segment, and replace the target with backup recovery.
// Localized extensions, overwrite prompts and failures retain their installed ordering.
u8 CtInitSaveCommand::SavePath(ScString *inputPath) {
  ScString path, directory, extension;
  ScMessageServer *messages = CtGetInitMessageServer();
  CtInitCursorScope cursor(14);
  ScString selected, firstExtension, secondExtension;
  firstExtension = CtInitText(140, kRsCommonTextGroup);
  secondExtension = CtInitText(142, kRsCommonTextGroup);
  ScString scenarioExtension(".snr"), extra, message;
  message = CtInitText(kRsMessageErrorMessageText, kRsMessageTextGroup);
  WnWindowManager *windows = CtGetInitWindowManager();
  CtCity *city = CtGetInitCity();
  if (!city)
    return false;
  CtInitPrepareSaveExtension(&firstExtension);
  CtInitPrepareSaveExtension(&secondExtension);
  {
    ScString input(*static_cast<ScStringView *>(inputPath));
    selected = input;
  }
  bool showDialog = !selected.Strlen();
  if (!showDialog) {
    ScPath input(selected);
    ScString currentExtension(input.Extension());
    CtInitLowerSaveString(&currentExtension);
    showDialog = city->GetCityType() != 3 && !currentExtension.IsEqual(&secondExtension);
    if (currentExtension.IsEqual(&scenarioExtension))
      showDialog = true;
  }
  for (;;) {
    if (showDialog) {
      UiCommandInterface *command = nullptr;
      if (!CtGetInitCommandFactory()->GetCommand(
              kCtFileDialogCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command)))
        return false;
      ScValue *kind = new ScValue(kCtFileSaveCity, kScValueUint32);
      kind->AddRef();
      command->PushBackInput(kind);
      kind->Release();
      if (!command->Execute() || command->OutputQueueSize() < 2) {
        command->Release();
        return false;
      }
      ScVariant *argument = nullptr;
      command->PopFrontOutput(&argument);
      if (!*argument->AsBool()) {
        command->Release();
        argument->Release();
        return false;
      }
      argument->Release();
      argument = nullptr;
      if (!command->PopFrontOutput(&argument)) {
        command->Release();
        return false;
      }
      u32 count = argument->GetCount();
      char *data = argument->RefRZChar();
      ScString filename(data, count);
      path = filename;
      argument->Release();
      command->Release();
    } else {
      ScString input(*static_cast<ScStringView *>(inputPath));
      path = input;
    }
    bool hasExtension = RsFile::GetPathExtension(path, &extension);
    bool replacedExtension = false;
    if (hasExtension) {
      CtInitLowerSaveString(&extension);
      if (extension.IsEqual(&firstExtension)) {
        ScPath rewritten(path);
        const ScPath &parts = rewritten;
        const ScString &drive = parts.Drive(), &directoryPart = parts.Directory(), &filename = parts.FileName();
        rewritten.MakePath(drive, directoryPart, filename, secondExtension);
        path = *rewritten.Path();
        replacedExtension = true;
      }
    }
    if (!replacedExtension &&
        (!hasExtension || (!extension.IsEqual(&secondExtension) && !extension.IsEqual(&scenarioExtension)))) {
      u32 length = path.Strlen();
      if (length && path.begin[length - 1] == '.')
        path.Erase(length - 1, 1);
      path.Append(secondExtension.Data(), secondExtension.Strlen());
    }
    if (showDialog) {
      if (RsFile::FileExists(path)) {
        bool writable = RsFile::IsFileWritable(path);
        ScString prompt = CtInitText(writable ? 12 : kRsMessageReadOnlySaveAnotherNameText, kRsMessageTextGroup);
        u32 choice = kWnCancelButtonId;
        if (windows) {
          choice = windows->GZMsgBox(prompt, message, writable ? 0x10004 : 0x40000, true, 0);
          ApBoxX *box = CtGetInitBox();
          if (box)
            box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
        }
        if (!writable || choice == kWnNoButtonId)
          continue;
        if (choice != kWnYesButtonId)
          return false;
      }
    } else if (!RsFile::IsFileWritable(path)) {
      ScString prompt = CtInitText(kRsMessageReadOnlyUseSaveAsText, kRsMessageTextGroup);
      if (windows) {
        windows->GZMsgBox(prompt, message, 0x40000, true, 0);
        ApBoxX *box = CtGetInitBox();
        if (box)
          box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
      }
      return false;
    }
    messages->MessageSend(ScMessage(kCtMsg_CitySaveStarting, reinterpret_cast<uptr>(&path), 0, 0));
    CtInitCursorScope savingCursor(14);
    u32 error = 0;
    bool saved = false;
    if (!RsFile::IsFileWritable(path)) {
      error = CtInitSavePlatformError();
      if (error == EACCES || error == EPERM)
        error = kRsFileErrorAccessFailure;
      else if (error == ENOTDIR)
        error = kRsFileErrorInvalidDirectory;
      else if (error == ENAMETOOLONG)
        error = kRsFileErrorNameTooLong;
    } else if (!RsFile::MakeTempPathName(&directory)) {
      error = CtInitSavePlatformError();
    } else {
      RsDatabaseSegment *segment = nullptr;
      if (CtGetInitDirector()->GZCOM()->GetClassObject(
              kRsLayeredFileClassId, IID_RsDatabaseSegment, reinterpret_cast<void **>(&segment))) {
        RsFileSegment *file = nullptr;
        if (segment->QueryInterface(IID_RsFileSegment, reinterpret_cast<void **>(&file))) {
          if (file->OpenWrite(&directory)) {
            messages->MessageSend(
                ScMessage(kCtMsg_SaveToSegment, reinterpret_cast<uptr>(segment), reinterpret_cast<uptr>(&path), 0));
            segment->Shutdown();
          }
          file->Release();
        }
      }
      if (segment)
        segment->Release();
      bool available = !RsFile::FileExists(path);
      if (!available) {
        extra = path.Left(path.Strlen() - 1);
        extra.Append("_");
        available = RsFile::Rename(path, extra);
      }
      if (available) {
        saved = RsFile::Copy(directory, path, true);
        if (!saved) {
          error = CtInitSavePlatformError();
          if (extra.Strlen())
            RsFile::Rename(extra, path);
        }
      } else {
        error = CtInitSavePlatformError();
      }
    }
    if (saved || error == kRsFileErrorNone) {
      RsFile::Delete(directory);
      if (extra.Strlen())
        RsFile::Delete(extra);
      city->SetFilePath(path);
      messages->MessageSend(ScMessage(kCtMsg_CitySaveFinished, reinterpret_cast<uptr>(&path), 0, 0));
      CtGetInitMessageServer()->MessagePost(ScMessage(kCtMsg_CityNameChanged), false);
      return true;
    }
    u32 resourceId = error == kRsFileErrorAccessFailure ? 39
        : error == kRsFileErrorNameTooLong              ? 42
        : error == kRsFileErrorInvalidDirectory         ? 45
                                                        : 46;
    ScString errorMessage;
    {
      CtInitText resource(resourceId, kRsMessageTextGroup);
      ScString text = static_cast<ScString>(resource);
      if (resourceId == 39)
        errorMessage.Sprintf(text.ToChar(), path.ToChar());
      else
        errorMessage = text;
    }
    if (windows)
      windows->GZMsgBox(errorMessage, message, 0x40000, true, 0);
    if (!showDialog)
      return false;
  }
}

CtInitLoadCommand::CtInitLoadCommand() : requestKind(kCtFileOpenCity) {
  AddInputParameter(0x800c, kCtLoadModeParameterId);
}

// Consume the load command's mode and optional flags and filename in their original queue order.
// A special numeric second argument requires exactly one further numeric argument; Boolean bytes are copied raw.
bool CtInitLoadCommand::ReadParameters(u32 *kind, u8 *confirm, u8 *showDialog, u8 *special, ScString *path) {
  ScVariant *argument = nullptr;
  *confirm = 1;
  *showDialog = 1;
  *special = 0;
  bool valid = InputQueueSize() != 0;
  if (!valid)
    return false;
  PopFrontInput(&argument);
  if (argument->GetType() == kScValueUint32)
    *kind = *argument->AsUint32();
  else
    valid = false;
  if (argument)
    argument->Release();
  if (!valid)
    return false;
  valid = InputQueueSize() != 0;
  if (!valid)
    return false;
  PopFrontInput(&argument);
  if (argument->GetType() == kScValueUint32) {
    *special = 1;
  } else if (argument->GetType() == kScValueBool) {
    *confirm = *argument->AsBool();
    *special = 0;
  }
  if (argument)
    argument->Release();
  if (*special == 1) {
    valid = InputQueueSize() == 1;
    if (valid) {
      PopFrontInput(&argument);
      valid = argument->GetType() == kScValueUint32;
      if (argument)
        argument->Release();
    }
  } else {
    if (InputQueueSize()) {
      PopFrontInput(&argument);
      if (argument->GetType() == kScValueCharArray) {
        u32 count = argument->GetCount();
        const char *data = argument->RefRZChar();
        path->ScString::FromChar(data, count);
        *showDialog = 0;
      }
      if (argument)
        argument->Release();
    }
    InputQueueSize();
  }
  return valid;
}

// Run the file-selection command with this request kind and copy its accepted filename from the output queue.
// The installed failure path retains the command reference when execution itself returns false.
bool CtInitLoadCommand::SelectFile(ScString *path) {
  UiCommandInterface *command = nullptr;
  if (!CtGetInitCommandFactory()->GetCommand(
          kCtFileDialogCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command)))
    return false;
  ScValue *kind = new ScValue(requestKind, kScValueUint32);
  kind->AddRef();
  command->PushBackInput(kind);
  kind->Release();
  bool accepted = command->Execute();
  if (accepted) {
    if (command->OutputQueueSize() < 2) {
      accepted = false;
    } else {
      ScVariant *argument = nullptr;
      command->PopFrontOutput(&argument);
      if (!*argument->AsBool())
        accepted = false;
      argument->Release();
      if (accepted) {
        command->PopFrontOutput(&argument);
        u32 count = argument->GetCount();
        const char *data = argument->RefRZChar();
        ScString selected(data, count);
        *path = selected;
        argument->Release();
      }
    }
    command->Release();
  }
  return accepted;
}

// Ask whether the current city should be saved before loading and run the save command when requested.
// Preserve the installed command-release behavior on both save success and failure.
bool CtInitLoadCommand::ConfirmSave() {
  ScString message = CtInitText(kRsGameWouldYouLikeToSaveThisCityText, kRsGameTextGroup);
  ScString title = CtInitText(kRsMessageSaveText, kRsMessageTextGroup);
  u32 choice = kWnCancelButtonId;
  WnWindowManager *windows = CtGetInitWindowManager();
  if (windows) {
    choice = windows->GZMsgBox(message, title, 0x10004, true, 0);
    ApBoxX *box = CtGetInitBox();
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  }
  if (choice == kWnNoButtonId)
    return true;
  if (choice != kWnYesButtonId)
    return false;
  UiCommandInterface *command = nullptr;
  if (CtGetInitCommandFactory()->GetCommand(
          kCtSaveCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command)) &&
      !command->Execute()) {
    command->Release();
    return false;
  }
  return true;
}

// Parse the load request, optionally confirm saving, and retry selection after a failed mode-specific load.
// Pause the current city during the operation and publish the final Boolean result through the output queue.
bool CtInitLoadCommand::Execute() {
  ScString path;
  CtGetInitWindowManager();
  CtCity *city = CtGetInitCity();
  if (city && !city->CanPlayerLoadSave())
    return true;
  CtGetInitApplication();
  u8 special = 0, retried = 0, confirm = 1, showDialog = 1;
  if (CtInitHasCity() == 1)
    CtGetInitCity()->SimHiddenPause();
  u8 result = ReadParameters(&requestKind, &confirm, &showDialog, &special, &path);
  if (result == 1 && CtInitHasCity() == 1 && confirm == 1)
    result = ConfirmSave();
  while (result == 1) {
    if (showDialog == 1) {
      result = SelectFile(&path);
      if (result != 1)
        break;
    }
    u8 loaded = 0;
    switch (requestKind) {
    case kCtFileOpenTerrain:
      loaded = LoadGenerated(&path, 1);
      break;
    case kCtFileOpenStarterTown:
      loaded = LoadGenerated(&path, 0);
      break;
    case kCtFileOpenCity: {
      CtInitCursorScope cursor(14);
      loaded = LoadCity(&path, nullptr);
      break;
    }
    case kCtFileOpenScenario:
      loaded = LoadScenario(&path);
      break;
    }
    result = loaded;
    if (result)
      break;
    retried = 1;
    result = true;
    showDialog = 1;
  }
  if (!result) {
    if (CtInitHasCity())
      CtGetInitCity()->SimHiddenUnpause();
    if (retried == 1 && special == 1 && !CtInitHasCity()) {
      CtGetInitApplication()->SetRestartOnExit(true);
      CtGetInitApplication()->Quit(false, false);
    }
  }
  ScValue *output = new ScValue(false);
  output->value.unsigned8 = result;
  output->AddRef();
  PushBackOutput(output);
  output->Release();
  return result != 0;
}

// Check a nonempty scenario path and its original file-type validator before loading the city.
// The automatic cursor is retired on every return, including validation failure.
u8 CtInitLoadCommand::LoadScenario(ScString *path) {
  CtInitCursorScope cursor(14);
  u8 result = 0;
  if (path->Strlen() && RsFile::FileExists(*path)) {
    CtDefinition *definition = CtGetInitApplication()->GetAppCityDefinition();
    if (definition && definition->ValidateSNRFile(*path))
      result = LoadCity(path, nullptr);
  }
  return result;
}

// Reject an empty or unavailable city file, then dispatch the installed file type to its import and city initializer.
// Report a recoverable loading failure through the original message box and application cleanup.
u8 CtInitLoadCommand::LoadCity(ScString *path, CtNewCityInfo *settings) {
  ScString selected;
  ScMessageServer *messages = CtGetInitMessageServer();
  CtInitCursorScope cursor(14);
  {
    ScString copy(*static_cast<ScStringView *>(path));
    selected = copy;
  }
  if (!selected.Strlen() || !RsFile::FileExists(selected))
    return 0;
  CtDefinition *definition = CtGetInitApplication()->GetAppCityDefinition();
  if (!definition)
    return 0;
  u32 type = definition->GetFileType(selected);
  ApGameInterface *application = CtGetInitApplication();
  if (!application)
    return 0;
  application->PreCityLoad();
  if (type < 1 || type > 6)
    return 0;
  u8 result = 0;
  if (type == 1) {
    ScImporter *importer = nullptr;
    result = CtGetInitDirector()->GZCOM()->GetClassObject(
        kScImporterClassId, IID_ScImporter, reinterpret_cast<void **>(&importer));
    if (result) {
      result = importer->Init(selected);
      if (result) {
        result = application->CreateCity();
        if (result) {
          CtCity *city = CtGetInitCity();
          result = city ? city->Init(importer) : 0;
        }
      }
    }
    if (importer)
      importer->Release();
  } else {
    RsDatabaseSegment *segment = nullptr;
    result = CtGetInitDirector()->GZCOM()->GetClassObject(
        kRsLayeredFileClassId, IID_RsDatabaseSegment, reinterpret_cast<void **>(&segment));
    if (result) {
      RsFileSegment *file = nullptr;
      result = segment->QueryInterface(IID_RsFileSegment, reinterpret_cast<void **>(&file));
      if (result) {
        result = file->OpenRead(&selected);
        if (result && file->GetVersion() < 101)
          result = 0;
        file->Release();
      }
    }
    if ((type == 2 || type == 3 || type == 6) && !settings)
      ApplyCityScheme(segment);
    if (result) {
      result = application->CreateCity();
      if (result) {
        CtCity *city = application->GetCity();
        if (city) {
          result = city->Init(segment);
          if (result)
            messages->MessageSend(ScMessage(
                kCtMsg_LoadFromSegment, reinterpret_cast<uptr>(segment), reinterpret_cast<uptr>(&selected), 0));
        } else {
          result = 0;
        }
      }
    }
    if (segment)
      segment->Release();
    if (type == 4 || type == 5)
      selected.FromChar("");
  }
  if (settings) {
    CtCity *city = application->GetCity();
    if (city) {
      if (requestKind == kCtFileOpenTerrain)
        city->SetDate(1, settings->GetStartYear());
      if (requestKind == kCtFileOpenTerrain || requestKind == kCtFileOpenStarterTown) {
        city->SetCityName(*settings->GetCityName());
        city->SetMayorName(*settings->GetMayorName());
        city->SetDifficulty(settings->GetDifficulty());
        EcBudgetLayer *budget = city->BudgetLayer();
        if (budget) {
          if (settings->GetLoanByte() == 1) {
            budget->SetTotalFunds(0);
            budget->IssueBond(static_cast<u32>(settings->GetInitialFunds()));
          } else {
            budget->SetTotalFunds(settings->GetInitialFunds());
          }
        }
        city->SetAutoBudgetEnabled(settings->GetAutoBudgetByte() != 0);
        city->SetDisastersEnabled(settings->GetDisastersByte() != 0);
      }
    }
  }
  if (!result) {
    application->ShutdownCity();
    ScString message = CtInitText(kRsCommonUnknownFileTypeText, kRsCommonTextGroup);
    ScString title = CtInitText(kRsCommonSimCity3000Text, kRsCommonTextGroup);
    WnWindowManager *windows = CtGetInitWindowManager();
    if (windows)
      windows->GZMsgBox(message, title, 0x40000, true, 0);
    return 0;
  }
  application->PostCityInit(1, type, &selected);
  return 1;
}

// Collect generated-city settings from the original properties command and load the selected starter or terrain.
// Retain the new-city settings throughout command execution and release both command views in their original order.
u8 CtInitLoadCommand::LoadGenerated(ScString *path, u8 terrain) {
  u32 kind = terrain ? 3 : 2;
  CtNewCityInfoImpl *settings = new CtNewCityInfoImpl;
  settings->AddRef();
  UiCitySchemeSelection *properties = nullptr;
  u8 result = CtGetInitCommandFactory()->GetCommand(
      kUiSelectCitySchemeCommandClassId, IID_UiCitySchemeSelection, reinterpret_cast<void **>(&properties));
  if (result) {
    UiCommandInterface *command = nullptr;
    properties->QueryInterface(IID_UiCommandInterface, reinterpret_cast<void **>(&command));
    ScValue *argument = new ScValue(kind, kScValueUint32);
    argument->AddRef();
    command->PushBackInput(argument);
    argument->Release();
    argument = ScAddRefNotNull(new ScValue(0u, kScValueUint32));
    command->PushBackInput(argument);
    command->PushBackInput(argument);
    argument->Release();
    result = command->Execute();
    if (result) {
      if (properties->GetDirtGenerator()) {
        settings->SetCityName(properties->GetCityName());
        settings->SetMayorName(properties->GetMayorName());
        settings->SetFunds(properties->GetFunds());
        settings->SetFundsAreDebt(properties->AreFundsDebt());
        settings->SetDifficulty(properties->GetDifficulty());
        settings->SetCitySize(properties->GetCitySize());
        settings->SetStartYear(properties->GetStartYear());
        settings->SetAutoBudgetEnabled(properties->IsAutoBudgetEnabled());
        settings->SetDisastersEnabled(properties->AreDisastersEnabled());
        settings->SetDirtGenerator(properties->GetDirtGenerator());
      } else {
        result = 0;
      }
    }
    command->Release();
    properties->Release();
    if (result) {
      CtInitCursorScope cursor(14);
      result = LoadCity(path, settings);
    }
  }
  settings->Release();
  return result;
}

// Read the saved scheme selections when present, apply them, and load the segment's BAT replacements.
// Missing or unreadable records retain the manager's defaults for the affected selections.
bool CtInitLoadCommand::ApplyCityScheme(RsDatabaseSegment *segment) {
  CtSchemeManager *scheme = CtGetInitApplication()->CitySchemeManager();
  if (!scheme)
    return false;
  u32 landscape = scheme->GetDefaultLandScape();
  u32 flora = scheme->GetDefaultFloraSet();
  u32 buildings = scheme->GetDefaultBuildingSet();
  RsKey key(kCtCityStyleRecordType, kCtCityStyleRecordGroup, 1);
  RsRecord *record = nullptr;
  if (segment->OpenRecord(key, &record)) {
    RsStream *stream = nullptr;
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->ReadU32(&buildings);
      stream->ReadU32(&flora);
      stream->ReadU32(&landscape);
      stream->Release();
    }
    segment->CloseRecord(&record);
  }
  scheme->SetCityScheme(landscape, flora, buildings, false);
  return scheme->LoadBATReplacementSet(segment, true);
}

CtInitNewCityCommand::CtInitNewCityCommand() {
}

// Confirm replacement of an existing city, collect new-city properties and create the city through the original command.
// Publish the Boolean result after the city notifications or the failure-path hidden unpause.
bool CtInitNewCityCommand::Execute() {
  bool result = true;
  if (CtInitHasCity() == 1) {
    CtGetInitCity()->SimHiddenPause();
    ScString message = CtInitText(kRsMessageDoYouWantToSaveYourCityText, kRsMessageTextGroup);
    ScString title = CtInitText(kRsMessageSaveText, kRsMessageTextGroup);
    u32 choice = kWnCancelButtonId;
    if (CtGetInitWindowManager()) {
      choice = CtGetInitWindowManager()->GZMsgBox(message, title, 0x10004, true, 0);
      if (CtGetInitBox())
        CtGetInitBox()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    }
    if (choice != kWnNoButtonId) {
      if (choice == kWnYesButtonId) {
        UiCommandInterface *save = nullptr;
        result = CtGetInitCommandFactory()->GetCommand(
            kCtSaveCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&save));
        if (result) {
          result = save->Execute();
          save->Release();
        }
      } else {
        result = false;
      }
    }
  }
  if (result) {
    u32 count = InputQueueSize();
    if (count == 3) {
      for (u32 i = 0; i < 3; ++i) {
        ScVariant *argument;
        PopFrontInput(&argument);
        if (argument->GetType() != kScValueUint32)
          result = false;
        if (result)
          result = *argument->AsUint32() == 0;
        argument->Release();
      }
    } else if (count != 0) {
      result = false;
    }
  }
  CtNewCityInfoImpl *settings = nullptr;
  if (result) {
    settings = ScAddRefNotNull(new CtNewCityInfoImpl);
    UiCitySchemeSelection *properties = nullptr;
    result = CtGetInitCommandFactory()->GetCommand(
        kUiSelectCitySchemeCommandClassId, IID_UiCitySchemeSelection, reinterpret_cast<void **>(&properties));
    if (result) {
      UiCommandInterface *command = nullptr;
      properties->QueryInterface(IID_UiCommandInterface, reinterpret_cast<void **>(&command));
      ScValue *argument = new ScValue(1u, kScValueUint32);
      argument->AddRef();
      command->PushBackInput(argument);
      argument->Release();
      argument = ScAddRefNotNull(new ScValue(0u, kScValueUint32));
      command->PushBackInput(argument);
      command->PushBackInput(argument);
      argument->Release();
      result = command->Execute();
      if (result) {
        if (properties->GetDirtGenerator()) {
          settings->SetCityName(properties->GetCityName());
          settings->SetMayorName(properties->GetMayorName());
          settings->SetFunds(properties->GetFunds());
          settings->SetFundsAreDebt(properties->AreFundsDebt());
          settings->SetDifficulty(properties->GetDifficulty());
          settings->SetCitySize(properties->GetCitySize());
          settings->SetStartYear(properties->GetStartYear());
          settings->SetAutoBudgetEnabled(properties->IsAutoBudgetEnabled());
          settings->SetDisastersEnabled(properties->AreDisastersEnabled());
          settings->SetDirtGenerator(properties->GetDirtGenerator());
        } else {
          result = false;
        }
      }
      command->Release();
      properties->Release();
      if (result)
        result = CreateCity(settings);
      if (result) {
        CtGetInitMessageServer()->MessagePost(ScMessage(kWnMsg_EnterTerrainEditor), false);
        CtGetInitBox()->Event(kAuEventTerrainEditor, 0, 0, 0, 0);
      }
    }
  }
  if (!result && CtInitHasCity())
    CtGetInitCity()->SimHiddenUnpause();
  ScValue *output = new ScValue(result);
  output->AddRef();
  PushBackOutput(output);
  output->Release();
  if (settings)
    settings->Release();
  return result;
}

// Create and initialize a city from the supplied settings, or shut it down when preparation or initialization fails.
// A non-null request returns success after either path, matching the installed command contract.
bool CtInitNewCityCommand::CreateCity(CtNewCityInfo *settings) {
  if (!settings)
    return false;
  CtInitCursorScope cursor(14);
  ApGameInterface *application = CtGetInitApplication();
  bool created = application->PreCityLoad();
  if (created)
    created = application->CreateCity();
  CtCity *city = application->GetCity();
  if (created && city && city->Init(settings))
    application->PostCityInit(2, 7, nullptr);
  else
    application->ShutdownCity();
  return true;
}

CtInitFactoryBase::CtInitFactoryBase() : references(0) {
}

// Release each child in insertion order until the first null entry; array storage is then retired.
CtInitFactoryBase::~CtInitFactoryBase() {
  auto current = children.begin(), end = children.end();
  while (current != end && *current) {
    (*current)->Release();
    *current = nullptr;
    ++current;
  }
}

// Publish the same factory view for the unknown and command-factory identities, retaining it on success.
bool CtInitFactoryBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the factory count before retaining its module director, then return the live factory count.
u32 CtInitFactoryBase::AddRef() {
  ++references;
  CtGetInitDirector()->AddRef();
  return references;
}

// Decrement the factory and module counts, deleting the factory only when its live count reaches zero.
u32 CtInitFactoryBase::Release() {
  --references;
  CtGetInitDirector()->RemoveRef();
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

bool CtInitFactoryBase::GetCommand(u32 classId, u32 iid, void **out) {
  for (auto current = children.begin(); current != children.end(); ++current)
    if ((*current)->GetCommand(classId, iid, out))
      return true;
  return CreateCommand(classId, iid, out);
}

// Retain a non-null child before appending it to the ordered factory list.
bool CtInitFactoryBase::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  children.push_back(factory);
  return true;
}
