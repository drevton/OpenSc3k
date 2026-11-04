#include "city/module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <list>
#include <new>

#include "advisors/ad_ini.h"
#include "city/api.h"
#include "city/initialization.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_error.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/manager_object.h"
#include "sprites/api.h"

CtDirectorChildren::CtDirectorChildren() : begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

CtDirectorChildren::~CtDirectorChildren() {
  delete[] begin;
}

// Append one borrowed child in source order, growing ordinary pointer storage only when full.
void CtDirectorChildren::Append(CtDllDirector *director) {
  if (end == storageEnd) {
    usize count = begin ? end - begin : 0, capacity = count ? count * 2 : 4;
    if (capacity < count || capacity > SIZE_MAX / sizeof(*begin))
      ScFatal(SC_ERROR_NOMEM, "Director child array cannot grow");
    CtDllDirector **data = new CtDllDirector *[capacity];
    if (count)
      memcpy(data, begin, count * sizeof(*begin));
    delete[] begin;
    begin = data;
    end = data + count;
    storageEnd = data + capacity;
  }
  *end++ = director;
}

CtDirectorClasses::CtDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
CtDirectorClasses::~CtDirectorClasses() {
  while (head.next != &head) {
    CtClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void CtDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  CtClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  CtClassRegistration *entry = new CtClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

CtDllDirector::CtDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

CtDllDirector::~CtDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool CtDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtDllDirector::AddRef() {
  return ++references;
}

u32 CtDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 CtDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 CtDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *CtDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *CtDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool CtDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (CtDllDirector **entry = children.begin; entry != children.end; ++entry)
    (*entry)->InitializeCOM(input, path);
  return true;
}

// Initialize a borrowed child, recursively append its own children, then append the child without a retain.
void CtDllDirector::AddDirector(CtDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (CtDllDirector **entry = director->children.begin; entry != director->children.end; ++entry)
    AddDirector(*entry);
  children.Append(director);
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void CtDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (CtDllDirector **entry = children.begin; entry != children.end; ++entry)
    (*entry)->EnumClassObjects(callback, context);
  for (CtClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool CtDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (CtDllDirector **entry = children.begin; entry != children.end; ++entry)
    if ((*entry)->GetClassObject(clsid, iid, out))
      return true;
  for (CtClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
    if (entry->clsid != clsid)
      continue;
    ScUnknown *object = entry->create();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// Let live children veto unloading before checking this director's own reference word.
// The installed PC body differs from the available SDK's reference-count shortcut.
bool CtDllDirector::CanUnloadNow() {
  for (CtDllDirector **entry = children.begin; entry != children.end; ++entry)
    if (!(*entry)->CanUnloadNow())
      return false;
  return references == 0;
}

bool CtDllDirector::OnUnload() {
  return true;
}

bool CtDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool CtDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (CtDllDirector **entry = children.begin; entry != children.end; ++entry)
    if (!(*entry)->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool CtDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  CtGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool CtDllDirector::PreFrameWorkInit() {
  return true;
}

bool CtDllDirector::PreAppInit() {
  return true;
}

bool CtDllDirector::PostAppInit() {
  return true;
}

bool CtDllDirector::PreAppShutdown() {
  return true;
}

bool CtDllDirector::PostAppShutdown() {
  return true;
}

bool CtDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool CtDllDirector::AbortiveQuit() {
  return true;
}

bool CtDllDirector::OnInstall() {
  return true;
}

CtDirector::CtDirector() {
  classes.Insert(kCtSimulatorClassId, CtCreateSimulator, 0);
  classes.Insert(kCtCityClassId, CtCreateCity, 0);
  classes.Insert(kCtAgentTypeClassId, CtCreateAgentType, 0);
  classes.Insert(kCtAgentClassId, CtCreateAgent, 0);
}

CtDirector::~CtDirector() {
}

u32 CtDirector::GetDirectorID() const {
  return kCtDirectorId;
}

bool CtDirector::AppRegister(ScRegistry *) {
  return true;
}

alignas(CtDirector) static u8 CtDirectorStorage[sizeof(CtDirector)];
static u8 CtDirectorInitFlags;

static void CtDestroyDirector() {
  reinterpret_cast<CtDirector *>(CtDirectorStorage)->~CtDirector();
}

// Set the lazy guard before constructing the actual static module object, then register its ordinary destruction.
ScDirector *CtGetDirector() {
  if (!(CtDirectorInitFlags & 1)) {
    CtDirectorInitFlags |= 1;
    new (CtDirectorStorage) CtDirector;
    atexit(CtDestroyDirector);
  }
  return reinterpret_cast<CtDirector *>(CtDirectorStorage);
}

#if !defined(SC3_MONOLITHIC)
// Return the real process-static SIMCITY director through the original module export.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return CtGetDirector();
}
#endif

// Equal prefixes are ordered by their full stored lengths.
bool BdPluginPathLess::operator()(const ScString &left, const ScString &right) const {
  u32 leftLength = left.ScString::Strlen(), rightLength = right.ScString::Strlen();
  u32 common = leftLength < rightLength ? leftLength : rightLength;
  int comparison = memcmp(left.ScString::Data(), right.ScString::Data(), common);
  return comparison ? comparison < 0 : leftLength < rightLength;
}

void SC3_CDECL BdReadPluginSegment(const ScString &key, const ScString &value, void *) {
  u32 id = ScConvertToUint32(key.ToChar());
  ScString path(value), classText = path.SplitToken(";");
  u32 classId = ScConvertToUint32(classText.ToChar());
  RsManager *resources = CtGetInitResourceManager();
  if (!resources)
    return;
  RsDatabaseSegment *segment = nullptr;
  if (!CtGetInitDirector()->GZCOM()->GetClassObject(
          classId, IID_RsDatabaseSegment, reinterpret_cast<void **>(&segment)))
    return;
  if (segment->Init(id, &path, false)) {
    segment->OpenDBSegment(true, false);
    resources->RegisterDBSegment(segment);
  }
  segment->Release();
}

// Register occupant mappings during the initial scan, or collect parsed resource keys for an updating scan.
// The registry service is cached as a borrowed pointer after releasing the framework's acquisition.
void SC3_CDECL BdReadPluginOccupant(const ScString &key, const ScString &value, void *owner) {
  BdPluginManagerImpl *manager = static_cast<BdPluginManagerImpl *>(owner);
  if (manager->scanFlag) {
    RsKey result;
    if (sscanf(key.ToChar(), "0x%x,0x%x,0x%x", &result.type, &result.group, &result.instance) == 3)
      manager->pendingKeys.push_back(result);
    return;
  }
  static bool acquired = false;
  static ScRegistry *registry = nullptr;
  if (!acquired) {
    acquired = true;
    registry = CtGetInitRegistry();
  }
  static ScString name;
  name.Erase(0, name.Strlen());
  if (registry->MakeRegistryName("sc3occattrib", key, value, &name))
    registry->Register(name);
}

// Enumerate DAT files in the plugin sprite directory and add each full path to the image-cache search list.
// Leave the callback without side effects when either required service is unavailable.
void SC3_CDECL BdReadPluginSprites(const ScString &key, const ScString &, void *) {
  RsFileSystem *files = CtGetInitFileSystem();
  SpImageCache *cache = CtGetInitImageCache();
  if (!files || !cache)
    return;
  ScString directory(files->DataDirectory());
  directory.Append("Plugin\\");
  directory.Append(key.Data(), key.Strlen());
  directory.Append("\\");
  RsDirectoryListing listing(directory, false, false, false);
  std::list<ScString> names;
  ScString path;
  {
    ScString filter("*.DAT");
    listing.SetNewDirectoryEntryFilter(filter, true);
  }
  listing.ReadDirectoryFileEntriesIntoStringList(&names);
  for (const ScString &name : names) {
    path = directory;
    path.Append(name.Data(), name.Strlen());
    cache->AddDBSegmentSpriteToSearchList(path, 0);
  }
}

void BdPluginManagerImpl::ProcessPluginFile(const ScString &path) {
  AdIniFile input;
  if (processedFiles.find(path) != processedFiles.end())
    return;
  processedFiles.insert(path);
  input.SetPath(path);
  input.EnumEntries(ScString("DBSegments"), BdReadPluginSegment, this);
  input.EnumEntries(ScString("OccupantKeys"), BdReadPluginOccupant, this);
  input.EnumEntries(ScString("SpriteSegments"), BdReadPluginSprites, this);
}

// Scan the plugin tree, process newly discovered PIN files, then publish collected occupant keys on an update.
// If the live cache is absent during an update, retain those keys until the next scan clears the collection.
void BdPluginManagerImpl::CheckForPluginBuildings(bool update) {
  RsDirectoryListing listing(directory, false, false, false);
  std::list<ScString> files;
  scanFlag = update;
  pendingKeys.clear();
  listing.SetNewDirectoryEntryFilter(ScString("*.pin"), true);
  listing.ReadDirectorySpecificEntriesIntoStringListRecursive(&directory, &files, true);
  for (const ScString &path : files)
    ProcessPluginFile(path);
  if (!update) {
    pendingKeys.clear();
  } else {
    OcAttribCache *cache = CtGetInitAttribCache();
    if (cache) {
      for (const RsKey &key : pendingKeys)
        cache->AddAttrib(key);
      if (!pendingKeys.empty())
        cache->PostOccAttribChangedMessage();
      pendingKeys.clear();
    }
  }
}

BdPluginManagerImpl::BdPluginManagerImpl() : references(0), scanFlag(1) {
}

BdPluginManagerImpl::~BdPluginManagerImpl() {
  BdPluginManagerImpl::Shutdown();
}

// Publish the primary plugin-manager identity for either supported IID and retain it on success.
bool BdPluginManagerImpl::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, BdPluginManager);
  HANDLE_QUERY_INTERFACE(IID_BdPluginManager, BdPluginManager);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the manager count before retaining its module director, then return the live manager count.
u32 BdPluginManagerImpl::AddRef() {
  ++references;
  CtGetInitDirector()->AddRef();
  return references;
}

// Remove a positive reference and its module retain, deleting the manager only when that decrement reaches zero.
u32 BdPluginManagerImpl::Release() {
  if (!references)
    return 0;
  --references;
  CtGetInitDirector()->RemoveRef();
  u32 remaining = references;
  if (!remaining)
    delete this;
  return remaining;
}

// Copy the plugin directory and invoke the real initial scan without requesting cache updates.
bool BdPluginManagerImpl::Init(const ScStringView &path) {
  directory.Copy(path);
  CheckForPluginBuildings(false);
  return true;
}

bool BdPluginManagerImpl::Shutdown() {
  processedFiles.clear();
  return true;
}
