#include "sprites/module.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <SDL3/SDL_filesystem.h>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "environment/api.h"
#include "geometry/ge_types.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "network/nw_manager.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/city_window.h"
#include "sprites/disaster_effects.h"
#include "sprites/sp_archive.h"
#include "sprites/sp_definition_factory.h"
#include "sprites/sp_effect.h"
#include "sprites/sprite.h"
#include "sprites/ufo_effects.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "window/api.h"

// Cell-aligned insertion can wrap its sprite in terrain and select the elevated cell-map path.
enum SpManagerLayerFlag : u32 {
  kSpLayerTerrainWrapped = 0x1000000,
  kSpLayerElevated = 0x10000000,
};

// The service constructor supplies this original startup ordering priority.
enum SpImageCacheOrder : i32 {
  kSpImageCachePriorityMagnitude = 0x2dc6c0,
};

// Reject a duplicate live DAT path before allocation. Open the real indexed archive through its actual secondary
// file method, register without retaining on success, and delete only when opening fails.
u8 SpImageStore::AddDatPath(const ScStringView *path, u32 append) {
  for (SpImageProvider *provider = SpImageProviders->next; provider != SpImageProviders; provider = provider->next) {
    auto archive = static_cast<SpArchive *>(provider->source);
    if (archive->path.IsEqual(path))
      return 0;
  }
  auto archive = new SpArchive;
  if (archive->OpenRead(*path))
    return AddDBSegmentSpriteToSearchList(archive, append);
  delete archive;
  return 0;
}

// Enumerate each original sprite search twice: refresh first, then capture file-only names in native enumeration order.
// Retain the complete name snapshot before registering any archive, including on SDL filesystems.
static void SpScanSpriteNames(const ScStringView *pattern, bool collect, ScArray<ScString *> *names) {
  ScString query = ScNativeFilePath(pattern->Data()), directory(".");
  const char *separator = strrchr(query.Data(), '/'), *filter = separator ? separator + 1 : query.Data();
  if (separator)
    directory.FromChar(query.Data(), u32(separator - query.Data()) + 1);
  int count = 0;
  char **entries = ScNativeDirectoryGlob(directory.Data(), filter, &count);
  if (!entries)
    return;
  for (int i = 0; collect && i < count; ++i) {
    ScString path(directory);
    if (path.begin != path.end && path.end[-1] != '/')
      path.Append("/");
    path.Append(entries[i]);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path.Data(), &info) && info.type != SDL_PATHTYPE_DIRECTORY)
      *names->Append() = new ScString(entries[i]);
  }
  SDL_free(entries);
}

// Discover SII files before DAT providers, preserving the source's two scans and complete per-pattern snapshot.
// Concatenate the filesystem root and literal without inserting a separator between them, then register each path.
void SpImageStore::DiscoverSources() {
  RsFileSystem *files = SpGetFileSystem();
  ScString base(files ? files->DataDirectory() : "");
  base.Append("Res\\Sprites");
  u32 length = base.Strlen();
  if (length && base.Data()[length - 1] != '\\' && base.Data()[length - 1] != '/')
    base.Append("\\");
  static const char *patterns[] = {"*.SII", "*.DAT"};
  ScString path;
  for (u32 kind = 0; kind < 2; ++kind) {
    ScString pattern(base);
    pattern.Append(patterns[kind]);
    ScArray<ScString *> names;
    SpScanSpriteNames(&pattern, false, &names);
    SpScanSpriteNames(&pattern, true, &names);
    for (u32 i = 0; i < names.size; ++i) {
      ScString joined(base);
      joined.Append(names.data[i]->Data(), names.data[i]->Strlen());
      path = joined;
      if (!kind)
        AddIndex(&path, 1);
      else
        AddDatPath(&path, 1);
    }
    for (u32 i = 0; i < names.size; ++i)
      delete names.data[i];
  }
}

SpModuleObject::SpModuleObject() : references(0) {
}

SpModuleObject::~SpModuleObject() {
}

bool SpModuleObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpModuleObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpModuleObject::AddRef() {
  SpGetDirector()->AddRef();
  return ++references;
}

// Select final deletion before removing the module reference. Other counts decrement after that callback.
u32 SpModuleObject::Release() {
  if (references == 1) {
    SpGetDirector()->RemoveRef();
    delete this;
    return 0;
  }
  SpGetDirector()->RemoveRef();
  return --references;
}

// Drop a nonzero reference and its module retain without deleting the object. Zero remains unchanged.
u32 SpModuleObject::RemoveRef() {
  if (references) {
    SpGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 SpModuleObject::RefCount() {
  return references;
}

SpService::SpService(u32 value, i32 order) : identifier(value), priority(order), running(false) {
}

SpService::~SpService() {
}

bool SpService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  return SpModuleObject::QueryInterface(iid, out);
}

u32 SpService::AddRef() {
  return SpModuleObject::AddRef();
}

u32 SpService::Release() {
  return SpModuleObject::Release();
}

u32 SpService::GetServiceID() const {
  return identifier;
}

void SpService::SetServiceID(u32 value) {
  identifier = value;
}

i32 SpService::GetServicePriority() const {
  return priority;
}

bool SpService::IsServiceRunning() const {
  return running;
}

void SpService::SetServiceRunning(bool value) {
  running = value;
}

bool SpService::Init() {
  return true;
}

bool SpService::Shutdown() {
  return true;
}

bool SpService::OnTick(u32) {
  return true;
}

bool SpService::OnIdle(u32) {
  return true;
}

bool SpService::OnInstall() {
  return true;
}

SpImageStore::SpImageStore() : SpService(kSpImageCacheServiceId, -kSpImageCachePriorityMagnitude), initialized(0) {
}

SpImageStore::~SpImageStore() {
}

bool SpImageStore::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpImageCache, SpImageCache);
  return SpService::QueryInterface(iid, out);
}

u32 SpImageStore::AddRef() {
  return SpService::AddRef();
}

u32 SpImageStore::Release() {
  return SpModuleObject::RemoveRef();
}

static SpImageStore SpImageService;
static SpImageMap SpImageMapRoot;
static SpImageProvider SpImageProviderRoot = {&SpImageProviderRoot, &SpImageProviderRoot, nullptr};
SpImageMap *SpImageMaps = &SpImageMapRoot;
SpImageProvider *SpImageProviders = &SpImageProviderRoot;
u32 SpImageColorKey;

// Return the module's statically constructed cache receiver without a query, retain or lazy allocation.
SpImageStore *SpGetImageStore() {
  return &SpImageService;
}

SpImageMap::SpImageMap() : next(this), previous(this) {
  Rehash(100);
}

SpImageMap::~SpImageMap() {
}

// Find a key within one real SII map. Returned image storage stays stable across later map growth.
SpImage *SpImageMap::Find(u32 resource, u32 group) {
  for (u32 index = buckets.data[resource % buckets.size]; index;) {
    SpImageEntry *entry = &entries.data[index - 1];
    if (entry->resource == resource && entry->group == group)
      return entry->image;
    index = entry->next;
  }
  return nullptr;
}

// Retain the source hash table's prime sizes and bucket-chain traversal while using ordinary project storage.
// Rehash visits old buckets in order and prepends each extracted head, preserving the original reversal effects.
void SpImageMap::Rehash(u32 count) {
  static const u32 kSpImageBucketPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
      196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189,
      805306457, 1610612741, 3221225473, 4294967291};
  if (count <= buckets.size)
    return;
  u32 size = kSpImageBucketPrimes[27];
  for (u32 candidate : kSpImageBucketPrimes)
    if (candidate >= count) {
      size = candidate;
      break;
    }
  if (size <= buckets.size)
    return;
  ScArray<u32> replacement;
  replacement.data = static_cast<u32 *>(ScArrayGrow(nullptr, &replacement.capacity, size - 1, sizeof(u32)));
  replacement.size = size;
  memset(replacement.data, 0, (usize)size * sizeof(u32));
  for (u32 bucket = 0; bucket < buckets.size; ++bucket) {
    for (u32 index = buckets.data[bucket]; index;) {
      SpImageEntry *entry = &entries.data[index - 1];
      u32 following = entry->next, target = entry->resource % size;
      entry->next = replacement.data[target];
      replacement.data[target] = index;
      index = following;
    }
  }
  u32 *old = buckets.data, capacity = buckets.capacity;
  buckets.data = replacement.data;
  buckets.size = replacement.size;
  buckets.capacity = replacement.capacity;
  replacement.data = old;
  replacement.capacity = capacity;
}

// Keep the first mapping for a duplicate key. The source checks growth before discovering a duplicate.
// Metadata stays separately allocated and survives bucket or entry-array movement.
void SpImageMap::Insert(u32 resource, u32 group, SpImage *image) {
  Rehash(entries.size + 1);
  if (Find(resource, group))
    return;
  u32 *head = &buckets.data[resource % buckets.size];
  *entries.Append() = {group, resource, image, *head};
  *head = entries.size;
}

// Begin source-order hash iteration at the first nonempty bucket.
u32 SpImageMap::First() {
  for (u32 bucket = 0; bucket < buckets.size; ++bucket)
    if (buckets.data[bucket])
      return buckets.data[bucket];
  return 0;
}

// Follow the live chain before scanning later buckets using the current table size, as the source iterator does.
u32 SpImageMap::Next(u32 index) {
  SpImageEntry *entry = &entries.data[index - 1];
  if (entry->next)
    return entry->next;
  for (u32 bucket = entry->resource % buckets.size + 1; bucket < buckets.size; ++bucket)
    if (buckets.data[bucket])
      return buckets.data[bucket];
  return 0;
}

// Clear the original global map list's native storage, retaining the static sentinel for later reinitialization.
static void SpClearImageMaps() {
  while (SpImageMaps->next != SpImageMaps) {
    SpImageMap *map = SpImageMaps->next;
    SpImageMaps->next = map->next;
    map->next->previous = SpImageMaps;
    delete map;
  }
}

// Remove registration nodes without releasing their borrowed providers. Actual provider destruction is in Close.
static void SpClearImageProviders() {
  while (SpImageProviders->next != SpImageProviders) {
    SpImageProvider *provider = SpImageProviders->next;
    SpImageProviders->next = provider->next;
    provider->next->previous = SpImageProviders;
    delete provider;
  }
}

// The original module registers separate map/provider list destruction after constructing its singleton.
static const int SpMapExit = atexit(SpClearImageMaps), SpProviderExit = atexit(SpClearImageProviders);

// Drain each metadata record's references through the real virtual release method in original hash order.
// Then destroy maps, providers through their separate deleting identity, and registration nodes before clearing ready.
u8 SpImageStore::RealShutdown() {
  if (!initialized)
    return 1;
  lock.Lock();
  for (SpImageMap *map = SpImageMaps->next; map != SpImageMaps; map = map->next) {
    for (u32 index = map->First(); index; index = map->Next(index)) {
      SpImage *image = map->entries.data[index - 1].image;
      if (image->references) {
        u32 resource = map->entries.data[index - 1].resource, group = map->entries.data[index - 1].group;
        while (UnprepareImage(resource, group))
          ;
      }
      delete image;
    }
  }
  SpClearImageMaps();
  for (SpImageProvider *provider = SpImageProviders->next; provider != SpImageProviders; provider = provider->next) {
    if (provider->source) {
      ScObject *object = dynamic_cast<ScObject *>(provider->source);
      assert(object);
      delete object;
    }
  }
  SpClearImageProviders();
  initialized = 0;
  lock.Unlock();
  return 1;
}

u8 SpImageStore::IsInitialized() const {
  return initialized;
}

// Initialize once under the real recursive lock, retaining the previous key when no window-resource service exists.
// Discover providers and try every empty SII map before publishing the initialization byte; map failures are ignored.
bool SpImageStore::Init() {
  if (initialized)
    return 1;
  lock.Lock();
  WnResources *resources = SpGetWindowResources();
  if (resources)
    SpImageColorKey = resources->GetColor(kWnColorUiTransparency);
  DiscoverSources();
  SpLoadImageMaps();
  initialized = 1;
  lock.Unlock();
  return 1;
}

// Register a distinct case-sensitive SII path at the requested list end. An initialized service immediately tries
// all empty maps; the original method returns zero even when it inserts and loads a new file successfully.
u8 SpImageStore::AddIndex(const ScStringView *path, u8 append) {
  for (SpImageMap *map = SpImageMaps->next; map != SpImageMaps; map = map->next)
    if (map->path.IsEqual(path))
      return 0;
  auto map = new SpImageMap;
  map->path.Copy(*path);
  SpImageMap *before = append ? SpImageMaps : SpImageMaps->next;
  map->next = before;
  map->previous = before->previous;
  before->previous->next = map;
  before->previous = map;
  if (initialized)
    SpLoadImageMaps();
  return 0;
}

// Register each provider pointer only once, at the live list's head or tail. Registration neither opens nor retains
// the provider, and its actual one-byte result reports whether this call inserted a new node.
u8 SpImageStore::AddDBSegmentSpriteToSearchList(SpImageSource *source, u32 append) {
  for (SpImageProvider *provider = SpImageProviders->next; provider != SpImageProviders; provider = provider->next)
    if (provider->source == source)
      return 0;
  auto provider = new SpImageProvider;
  SpImageProvider *before = (u8)append ? SpImageProviders : SpImageProviders->next;
  provider->source = source;
  provider->next = before;
  provider->previous = before->previous;
  before->previous->next = provider;
  before->previous = provider;
  return 1;
}

// Load each currently empty SII map while continuing after ordinary failures. Partially populated maps are skipped
// on later calls, and new list entries remain visible to the original live traversal.
int SpLoadImageMaps() {
  u8 failed = 0;
  for (SpImageMap *map = SpImageMaps->next; map != SpImageMaps; map = map->next)
    if (!map->entries.size)
      failed |= SpReadImageMap(&map->path, map, 0) == 0;
  return failed == 0;
}

// Search all SII maps before asking DAT providers for metadata. A successful provider record is copied into the
// current first map under the requested key, with no retain and no synthesized fallback map.
SpImage *SpImageStore::GetImageInfo(u32 resource, u32 group) {
  lock.Lock();
  SpImage *image = nullptr;
  for (SpImageMap *map = SpImageMaps->next; map != SpImageMaps; map = map->next) {
    image = map->Find(resource, group);
    if (image)
      break;
  }
  if (!image) {
    RsKey key(1, group, resource);
    SpImage metadata;
    for (SpImageProvider *provider = SpImageProviders->next; provider != SpImageProviders; provider = provider->next) {
      if (provider->source->ReadMetadata(&metadata, &key)) {
        image = new SpImage;
        *image = metadata;
        SpImageMaps->next->Insert(resource, group, image);
        break;
      }
    }
  }
  lock.Unlock();
  return image;
}

// Acquire image pixels under the recursive cache lock. New records enter the first map before initialization;
// existing metadata preserves its rectangle while a newly loaded record derives it from live buffer getters.
u8 SpImageStore::PrepareImage(u32 resource, u32 group, SpImage **out) {
  lock.Lock();
  SpImage *image = GetImageInfo(resource, group);
  u8 result = 0;
  if (image) {
    if (!image->buffer) {
      image->buffer = SpLoadImage(resource, group);
      if (image->buffer)
        image->buffer->AddRef();
      else {
        lock.Unlock();
        return 0;
      }
    }
    ++image->references;
    if (out)
      *out = image;
    result = 1;
  } else {
    image = new SpImage;
    SpImageMaps->next->Insert(resource, group, image);
    image->references = 0;
    image->resource = resource;
    image->group = group;
    image->buffer = SpLoadImage(resource, group);
    if (image->buffer) {
      image->buffer->AddRef();
      GxBuffer *buffer = image->buffer;
      ++image->references;
      result = 1;
      if (buffer) {
        image->left = (i16)(buffer->Width() >> 1);
        image->top = (i16)(image->buffer->Height() - 1);
        u16 width = (u16)image->buffer->Width();
        image->bottom = 1;
        image->right = (i16)(width - (u16)image->left);
      } else {
        image->bottom = 0;
        image->right = 0;
        image->top = 0;
        image->left = 0;
      }
      *out = image;
    }
  }
  lock.Unlock();
  return result;
}

// Release one nonzero pixel reference and clear the buffer only after its final virtual Release returns.
// The captured remaining count is returned even when that callback changes the live record.
u32 SpImageStore::UnprepareImage(u32 resource, u32 group) {
  lock.Lock();
  SpImage *image = GetImageInfo(resource, group);
  u32 result = 0;
  if (image && image->references) {
    result = --image->references;
    if (!result) {
      image->buffer->Release();
      image->buffer = nullptr;
    }
  }
  lock.Unlock();
  return result;
}

// Try DAT providers in their live list order, accepting only success with a nonnull pixel buffer.
// Apply the module's full display color key before publishing the borrowed image; no extra retain is added.
GxBuffer *SpLoadImage(u32 resource, u32 group) {
  RsKey key(0, group, resource);
  GxBuffer *image;
  for (SpImageProvider *provider = SpImageProviders->next; provider != SpImageProviders; provider = provider->next) {
    if (provider->source->ReadImage(&image, &key) && image) {
      image->SetTransparency(SpImageColorKey);
      return image;
    }
  }
  return nullptr;
}

bool SpImageStore::Shutdown() {
  return true;
}

u8 SpImageStore::AddSpriteInfoFileToSearchList(const ScStringView &path, u8 append) {
  ScString value(path);
  return AddIndex(&value, append);
}

u8 SpImageStore::RemoveSpriteInfoFileToSearchList(const ScStringView &path) {
  ScString value(path);
  return RemoveIndex(&value);
}

u8 SpImageStore::AddDBSegmentSpriteToSearchList(const ScStringView &path, u32 append) {
  ScString value(path);
  return AddDatPath(&value, append);
}

u8 SpImageStore::RemoveDBSegmentSpriteToSearchList(const ScStringView &path) {
  ScString value(path);
  return RemoveDatPath(&value);
}

// Remove every node storing this provider, then optionally destroy its separate object identity.
u8 SpImageStore::RemoveDBSegmentSpriteToSearchList(SpImageSource *source, u8 destroy) {
  for (SpImageProvider *node = SpImageProviders->next, *next; node != SpImageProviders; node = next) {
    next = node->next;
    if (node->source == source) {
      node->previous->next = next;
      next->previous = node->previous;
      delete node;
    }
  }
  if (destroy && source) {
    ScObject *object = dynamic_cast<ScObject *>(source);
    assert(object);
    delete object;
  }
  return 1;
}

// Erase the first exactly matching SII map. Raw metadata records survive this removal, as in the original pointer map.
u8 SpImageStore::RemoveIndex(const ScStringView *path) {
  for (SpImageMap *map = SpImageMaps->next; map != SpImageMaps; map = map->next) {
    if (map->path.IsEqual(path)) {
      map->previous->next = map->next;
      map->next->previous = map->previous;
      delete map;
      return 1;
    }
  }
  return 0;
}

// Find the first provider with the requested DAT path. Invoke the real virtual removal with its deletion flag set.
u8 SpImageStore::RemoveDatPath(const ScStringView *path) {
  for (SpImageProvider *node = SpImageProviders->next; node != SpImageProviders; node = node->next) {
    auto archive = static_cast<SpArchive *>(node->source);
    if (archive->path.IsEqual(path))
      return RemoveDBSegmentSpriteToSearchList(node->source, 1);
  }
  return 0;
}

void SpManager::GetViewID(i32 *out) const {
  *out = view_id;
}

// Compute the requested bit change and dispatch the full flag update only when redraw is requested and bits differ.
// The installed method deliberately does not write the flag word on the no-update path.
void SpManager::SetCityViewFlag(u32 mask, bool enabled, bool update) {
  u32 flags = enabled ? view_flags | mask : view_flags & ~mask;
  if (update && flags != view_flags)
    SetCityViewFlags(flags, update);
}

bool SpManager::GetCityViewFlag(u32 mask) const {
  return (view_flags & mask) != 0;
}

u32 SpManager::GetCityViewFlags() const {
  return view_flags;
}

bool SpManager::TestCityViewFlags(u32 mask) const {
  return (view_flags & mask) != 0;
}

void SpManager::ClearCityViewFlags() {
  view_flags = 0;
}

// Return the borrowed occupant manager selected by the active view. Ownership stays with the city sprite manager.
ScOccupantManager *SpManager::GetCurrentOccupantManager() {
  return current_occupants;
}

// Ignore occupants rejected by the active filter, otherwise dispatch to the aligned or animated insertion path.
// The source notification identifier selects animated occupants independently of the occupant's own type.
bool SpManager::OccupantInserted(u32 source, ScOccupant *occupant) {
  if (occupant_filter && !occupant_filter->Test(occupant))
    return true;
  if (source == kOcAnimManagerId)
    return InsertUnalignedSprite(occupant, 1, 0);
  return InsertCellAlignedSprite(occupant, false);
}

// Apply the same view filter as insertion before removing an occupant's aligned or animated sprite.
// A filtered notification is acknowledged without touching the cell map.
bool SpManager::OccupantRemoved(u32 source, ScOccupant *occupant) {
  if (occupant_filter && !occupant_filter->Test(occupant))
    return true;
  if (source == kOcAnimManagerId)
    return RemoveUnalignedSprite(occupant);
  return RemoveCellAlignedSprite(occupant);
}

// Forward accepted animated-occupant movement to the sprite relocation method.
bool SpManager::OccupantMoved(u32 source, ScOccupant *occupant, const CtCoord &position) {
  if (occupant_filter && !occupant_filter->Test(occupant))
    return true;
  if (source == kOcAnimManagerId)
    return MoveUnalignedSprite(occupant, position);
  return false;
}

bool SpManager::OccupantChanged(u32 source, ScOccupant *occupant, i32 flags) {
  return OccupantChanged(source, occupant, flags, nullptr);
}

// Route accepted changes to aligned occupants or their animated sprite. The animated path reads location before
// acquiring the graphics lock, then resolves the live sprite only after the lock has been acquired.
bool SpManager::OccupantChanged(u32 source, ScOccupant *occupant, i32 flags, void *payload) {
  if (occupant_filter && !occupant_filter->Test(occupant))
    return true;
  if (source != kOcAnimManagerId)
    return ChangeCellAlignedSprite(occupant, (u32)flags, payload, 0);
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->ChangeDynamicSprite(occupant->GetSpriteInst(), position.x, position.z, (u32)flags, payload);
  lock->Unlock();
  return result;
}

// Query at most128 dynamic sprites inside normalized volume bounds, then acquire each exposed occupant interface.
// Publish only successful occupant queries and transfer their QueryInterface references to the caller.
bool SpManager::GetUnalignedOccupantsInVolume(const GeBounds &bounds, ScOccupant **out, u32 *count) {
  GeBounds normalized = bounds;
  if (*count > 128)
    *count = 128;
  if (normalized.minimum.x > normalized.maximum.x) {
    i32 previous = normalized.minimum.x;
    normalized.minimum.x = normalized.maximum.x;
    normalized.maximum.x = previous;
  }
  if (normalized.minimum.z > normalized.maximum.z) {
    i32 previous = normalized.minimum.z;
    normalized.minimum.z = normalized.maximum.z;
    normalized.maximum.z = previous;
  }
  if (normalized.minimum.altitude > normalized.maximum.altitude) {
    i32 previous = normalized.minimum.altitude;
    normalized.minimum.altitude = normalized.maximum.altitude;
    normalized.maximum.altitude = previous;
  }
  SpSprite *sprites[128];
  city->LockGraphics();
  cell_map->GetDynamicSpritesInVolume(normalized.minimum.x, normalized.minimum.z, normalized.minimum.altitude,
      normalized.maximum.x, normalized.maximum.z, normalized.maximum.altitude, sprites, count);
  city->UnlockGraphics();
  u32 found = 0;
  for (u32 i = 0; i < *count; ++i)
    if (sprites[i]) {
      ScOccupant *occupant = nullptr;
      if (sprites[i]->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&occupant)) && occupant)
        out[found++] = occupant;
    }
  *count = found;
  return true;
}

// Read both occupant widths while holding the city graphics lock, then remove and insert in order.
// The original result is true when either operation fails, and deliberately retains that inverted convention.
bool SpManager::RemoveAndInsertCellAlignedSprites(ScOccupant *remove, ScOccupant *insert) const {
  city->LockGraphics();
  remove->SizeInCellsX();
  insert->SizeInCellsX();
  bool failed = !RemoveCellAlignedSprite(remove);
  failed = !InsertCellAlignedSprite(insert, false) || failed;
  city->UnlockGraphics();
  return failed;
}

// Convert a micro-coordinate location to the aligned cell and forward its change while holding the real city lock.
// Preserve the manager's invalidation flag rather than replacing it with a forced redraw.
bool SpManager::ChangeCellAlignedSpriteInst(const CtCoord &position, u32 flags, void *payload) const {
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->ChangeSprite((u32)(position.x >> kCtMicroCoordinateShift),
      (u32)(position.z >> kCtMicroCoordinateShift), invalidate_changes != 0, flags, payload);
  lock->Unlock();
  return result;
}

bool SpManager::InsertUnalignedSprite(ScOccupant *occupant, i32 x, i32 z, i32 altitude, i32 priority, u32 flags) const {
  SpSprite *sprite = occupant->GetSpriteInst();
  sprite->SetGeomFlag(kSpLayerAnchorAdjustmentGeometry);
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->InsertDynamicSprite(sprite, x, z, altitude, invalidate_changes != 0, priority, flags);
  lock->Unlock();
  return result;
}

// Read the occupant's live position, mark its sprite animated and perform the original bounds query before insertion.
// Keep the initial coordinate sentinels for a producer that intentionally leaves a location component unchanged.
bool SpManager::InsertUnalignedSprite(ScOccupant *occupant, i32 priority, u32 flags) const {
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  SpSprite *sprite = occupant->GetSpriteInst();
  sprite->SetGeomFlag(kSpLayerAnchorAdjustmentGeometry);
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  cell_map->InMicroCellBounds(position.x, position.z);
  bool result = cell_map->InsertDynamicSprite(
      sprite, position.x, position.z, position.altitude, invalidate_changes != 0, priority, flags);
  lock->Unlock();
  return result;
}

// Remove the occupant's sprite at the supplied horizontal micro position under the city graphics lock.
// The original altitude argument is retained in the signature but is not consumed by the removal path.
bool SpManager::RemoveUnalignedSprite(ScOccupant *occupant, i32 x, i32 z, i32) const {
  SpSprite *sprite = occupant->GetSpriteInst();
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->RemoveDynamicSprite(sprite, x, z, invalidate_changes != 0);
  lock->Unlock();
  return result;
}

// Obtain the occupant's current position and sprite, then remove it after the source's discarded bounds query.
bool SpManager::RemoveUnalignedSprite(ScOccupant *occupant) const {
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  SpSprite *sprite = occupant->GetSpriteInst();
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  cell_map->InMicroCellBounds(position.x, position.z);
  bool result = cell_map->RemoveDynamicSprite(sprite, position.x, position.z, invalidate_changes != 0);
  lock->Unlock();
  return result;
}

// Relocate a sprite using the two supplied horizontal positions and the final altitude, retaining the source's
// unused third coordinate. The real graphics critical section surrounds only the cell-map movement.
bool SpManager::MoveUnalignedSprite(ScOccupant *occupant, i32 x, i32 z, i32, i32 old_x, i32 old_z, i32 altitude) const {
  SpSprite *sprite = occupant->GetSpriteInst();
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->MoveDynamicSprite(sprite, x, z, old_x, old_z, altitude);
  lock->Unlock();
  return result;
}

// Move the occupant's sprite from the supplied previous position to its current location. Both original bounds
// queries run before movement, and the occupant's current altitude remains the actual final argument.
bool SpManager::MoveUnalignedSprite(ScOccupant *occupant, const CtCoord &previous) const {
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  SpSprite *sprite = occupant->GetSpriteInst();
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  cell_map->InMicroCellBounds(position.x, position.z);
  cell_map->InMicroCellBounds(previous.x, previous.z);
  bool result = cell_map->MoveDynamicSprite(sprite, previous.x, previous.z, position.x, position.z, position.altitude);
  lock->Unlock();
  return result;
}

// Change an occupant's sprite at explicit horizontal micro coordinates. Artwork-change flags and payload are
// passed through unchanged, and the original graphics lock is released after the virtual cell-map call.
bool SpManager::ChangeUnalignedSprite(ScOccupant *occupant, i32 x, i32 z, u32 flags, void *payload) const {
  SpSprite *sprite = occupant->GetSpriteInst();
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->ChangeDynamicSprite(sprite, x, z, flags, payload);
  lock->Unlock();
  return result;
}

// Change an occupant's sprite at a caller-supplied coordinate without consuming its altitude component.
// Preserve sprite lookup before acquiring the city's graphics critical section.
bool SpManager::ChangeUnalignedSprite(ScOccupant *occupant, const CtCoord &position, u32 flags, void *payload) const {
  SpSprite *sprite = occupant->GetSpriteInst();
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->ChangeDynamicSprite(sprite, position.x, position.z, flags, payload);
  lock->Unlock();
  return result;
}

// Resolve the sprite before asking the occupant for its current position, then forward the requested dynamic change.
// The coordinate sentinel values and graphics-lock ordering match this particular overload's installed body.
bool SpManager::ChangeUnalignedSprite(ScOccupant *occupant, u32 flags, void *payload) const {
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  SpSprite *sprite = occupant->GetSpriteInst();
  occupant->GetLocation(&position);
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->ChangeDynamicSprite(sprite, position.x, position.z, flags, payload);
  lock->Unlock();
  return result;
}

// Insert a supplied sprite directly as an animated sprite, forcing the original invalidation argument true.
// This instance overload bypasses occupant lookup and preserves the explicit priority and flags.
bool SpManager::InsertUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position, i32 priority, u32 flags) const {
  sprite->SetGeomFlag(kSpLayerAnchorAdjustmentGeometry);
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->InsertDynamicSprite(sprite, position.x, position.z, position.altitude, true, priority, flags);
  lock->Unlock();
  return result;
}

// Remove a supplied sprite directly with invalidation enabled. Only the coordinate's horizontal components are used.
// Preserve the real city graphics critical section around the virtual removal.
bool SpManager::RemoveUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position) const {
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->RemoveDynamicSprite(sprite, position.x, position.z, true);
  lock->Unlock();
  return result;
}

// Forward the two coordinate records in their original order, including the second record's altitude.
// The caller supplies an existing sprite instance, so no occupant or geometry lookup is introduced.
bool SpManager::MoveUnalignedSpriteInst(SpSprite *sprite, const CtCoord &first, const CtCoord &second) const {
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->MoveDynamicSprite(sprite, first.x, first.z, second.x, second.z, second.altitude);
  lock->Unlock();
  return result;
}

// Change a supplied sprite directly at the two horizontal micro coordinates. The change payload remains the original
// untyped game payload rather than being bundled into a reconstruction-only request object.
bool SpManager::ChangeUnalignedSpriteInst(SpSprite *sprite, const CtCoord &position, u32 flags, void *payload) const {
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  bool result = cell_map->ChangeDynamicSprite(sprite, position.x, position.z, flags, payload);
  lock->Unlock();
  return result;
}

// Decode one of the three five-bit layer-priority groups. Missing groups, their zero-priority bit and unsupported
// layer selectors return zero; the first positive-priority bit wins when several bits are set.
i32 SpLayerPriority(i32 layer, u32 flags) {
  u32 shift;
  if (layer == 0)
    shift = 8;
  else if (layer == 1)
    shift = 13;
  else if (layer == 2)
    shift = 18;
  else
    return 0;
  u32 group = (flags >> shift) & 31u;
  if (!group || (group & 1u))
    return 0;
  for (u32 priority = 1; priority <= 4; ++priority)
    if (group & (1u << priority))
      return (i32)priority;
  return 0;
}

// Classify a chain as all terrain, or search for terrain/special geometry when the caller requests any match.
// A null initial chain returns true in both modes; an exhausted nonempty any-match search returns false.
u8 SpManager::OccupantIsDirtClod(SpSprite *sprite, bool require_all) const {
  if (!sprite)
    return 1;
  if (!require_all) {
    while (!sprite->HasGeomFlag(1) && !sprite->HasGeomFlag(1028)) {
      sprite = sprite->GetNext();
      if (!sprite)
        return 0;
    }
    return 1;
  }
  while (sprite->HasGeomFlag(1)) {
    sprite = sprite->GetNext();
    if (!sprite)
      return 1;
  }
  return 0;
}

// Scan inclusive cell bounds for cursor colors, stopping at the source's invalid-placement color36.
// A colored region chooses one of the two prepared native colors; an empty region publishes zero.
u8 SpManager::FindHighlightColor(u32 min_x, u32 min_z, u32 max_x, u32 max_z, u32 *out) const {
  bool colored = false, blocked = false;
  u32 color = 0;
  for (u32 x = min_x; x <= max_x && !blocked; ++x)
    for (u32 z = min_z; z <= max_z && !blocked; ++z) {
      CtCoord position = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
      cursor->GetColor(position, &color);
      if (color) {
        colored = true;
        if (color == 36)
          blocked = true;
      }
    }
  *out = colored ? blocked ? bad_cursor_color : good_cursor_color : 0;
  return colored;
}

// Produce a terrain sprite from the active cell cursor's palette and nonzero cell color.
// The cursor's DWORD color is explicitly narrowed to the original byte before it becomes the WORD palette row.
u8 SpManager::CreateTerrainCursor(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  if (!cursor_visible)
    return 0;
  CtCoord position = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
  u32 palette, color;
  cursor->GetDefaultColor(&palette, &color);
  cursor->GetColor(position, &color);
  if (!color)
    return 0;
  return dirt->GetDirtClodSingle((u8)palette, (u8)color, x, z, dry_only != 0, out, altitude);
}

// Read the zone's palette and WORD row and produce its dry terrain color only when the cell has no water vertices.
// Both genuine zone outputs retain their original widths instead of reading the untouched upper half of a DWORD.
u8 SpManager::CreateTerrainZone(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  u8 palette;
  u16 row;
  if (!zone->GetZoneColor(x, z, &palette, &row))
    return 0;
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(palette, row, x, z, dry_only != 0, out, altitude);
}

// Prefer the cell cursor, then the active view's special terrain representation, falling back to the real dirt bag.
// Preserve the original per-view dispatch and final raw-byte result rather than inventing a default successful sprite.
u8 SpManager::CreateTerrainSprite(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  if (CreateTerrainCursor(x, z, altitude, out))
    return 1;
  if (view_id == 0) {
    if ((view_flags & kSpCityViewZones) && CreateTerrainZone(x, z, altitude, out))
      return 1;
    if (dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0))
      return 1;
  }
  if ((view_id == 1 && CreateTerrainSubway(x, z, altitude, out)) ||
      (view_id == 2 && CreateTerrainPipe(x, z, altitude, out)) ||
      (view_id == 10 && CreateTerrainAura(x, z, altitude, out)) ||
      (view_id == 11 && CreateTerrainCrime(x, z, altitude, out)) ||
      (view_id == 12 && CreateTerrainPower(x, z, altitude, out)) ||
      (view_id == 13 && CreateTerrainFlammability(x, z, altitude, out)) ||
      (view_id == 14 && CreateTerrainLandValue(x, z, altitude, out)) ||
      (view_id == 15 && CreateTerrainPollution(x, z, altitude, out)) ||
      (view_id == 16 && CreateTerrainTraffic(x, z, altitude, out)) ||
      (view_id == 17 && CreateTerrainPopulation(x, z, altitude, out)))
    return 1;
  return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
}

// Color zoned land by signed aura, choosing the original positive/negative palettes and asymmetric126/127 offsets.
// Nonzoned cells use ordinary terrain; water vertices reject the single-color sprite after the aura query.
u8 SpManager::CreateTerrainAura(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  u8 zoned = 0;
  zone->GetValue(x, z, &zoned);
  if (!zoned)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  i8 value;
  aura->GetValue(x, z, &value);
  u8 palette = value > 0 ? 1 : 2;
  i32 amount = value > 0 ? 127 - value : value + 126;
  u16 row = (u16)(i64)((f64)amount * 0x1.f3e7dp-3);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(palette, row, x, z, dry_only != 0, out, altitude);
}

// Shade zoned cells using inverse crime intensity and the original red palette. The exact stored float coefficient
// is promoted before truncation, retaining the source's row boundaries rather than substituting rounded decimal math.
u8 SpManager::CreateTerrainCrime(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  u8 zoned = 0;
  zone->GetValue(x, z, &zoned);
  if (!zoned)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  u8 value;
  crime->GetValue(x, z, &value);
  u16 row = (u16)(i64)((f64)(255 - value) * 0x1.f1f1f2p-4);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(2, row, x, z, dry_only != 0, out, altitude);
}

// Shade zoned cells using inverse land value and the original green palette. Empty zoning keeps ordinary terrain,
// while cells with water vertices reject the single-color representation after reading the land-value map.
u8 SpManager::CreateTerrainLandValue(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  u8 zoned = 0;
  zone->GetValue(x, z, &zoned);
  if (!zoned)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  u8 value;
  land_value->GetValue(x, z, &value);
  u16 row = (u16)(i64)((f64)(255 - value) * 0x1.f1f1f2p-4);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(1, row, x, z, dry_only != 0, out, altitude);
}

// A missing hazard map or water cell declines the specialized sprite instead of inventing a usable substitute.
u8 SpManager::CreateTerrainFlammability(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  if (!flammability)
    return 0;
  u8 value;
  flammability->GetValue(x, z, &value);
  if ((f64)value <= 0x1.8ad6b6p+4)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  u16 row = (u16)(i64)((f64)(255 - value) * 0x1.f1f1f2p-4);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(2, row, x, z, dry_only != 0, out, altitude);
}

// Read air pollution, retain ordinary terrain at or below its prepared threshold and scale higher values to red.
// Clamp only at the original configured maximum before the palette-row conversion and water test.
u8 SpManager::CreateTerrainPollution(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  if (!pollution)
    return 0;
  u32 value;
  pollution->GetAirValue(x, z, &value);
  if ((f64)value <= pollution_threshold)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  u32 maximum = pollution_maximum;
  if (value > maximum)
    value = pollution_maximum;
  u16 row = (u16)(i64)((f64)(maximum - value) * pollution_scale);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(2, row, x, z, dry_only != 0, out, altitude);
}

// Shade zoned land with the original two power-state rows, leaving nonzoned cells as ordinary terrain.
// The actual power map writes one Boolean byte; no undefined upper bytes of a temporary word are read.
u8 SpManager::CreateTerrainPower(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  if (!power)
    return 0;
  u8 zoned = 0;
  zone->GetValue(x, z, &zoned);
  if (!zoned)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  bool powered;
  power->GetValue(x, z, &powered);
  u16 row = powered ? 0 : 31;
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(1, row, x, z, dry_only != 0, out, altitude);
}

// Use the valve layer's actual density byte to shade zoned cells. Preserve the source's early missing-layer refusal,
// ordinary unzoned terrain and final water-vertex test before creating the green density sprite.
u8 SpManager::CreateTerrainPopulation(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  if (!valves)
    return 0;
  u8 zoned = 0;
  zone->GetValue(x, z, &zoned);
  if (!zoned)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  u8 value;
  valves->GetDensity(x, z, &value);
  u16 row = (u16)(i64)((f64)(255 - value) * 0x1.f1f1f2p-4);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(1, row, x, z, dry_only != 0, out, altitude);
}

// Shade transport cells using the greater volume for masks21 and2. Other zoned land keeps its zone palette,
// and empty or special zone22 cells use ordinary terrain before the original single-color water rejection.
u8 SpManager::CreateTerrainTraffic(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  u8 zoned = 0, palette = 2;
  zone->GetValue(x, z, &zoned);
  CtCoord position = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
  u16 row;
  if (networks->HasTransportAt(position)) {
    u8 first, second;
    traffic->GetVolume(x, z, &first, 21);
    traffic->GetVolume(x, z, &second, 2);
    u8 volume = first >= second ? first : second;
    row = (u16)(i64)((f64)(255 - volume) * 0x1.f1f1f2p-4);
  } else if (!zoned || zoned == 22)
    return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, 0);
  else
    zone->GetZoneColor(x, z, &palette, &row);
  if (dirt->CellWaterVertCount(x, z))
    return 0;
  return dirt->GetDirtClodSingle(palette, row, x, z, dry_only != 0, out, altitude);
}

// Choose the subway-view terrain overlay from underground and surface occupants, the surface geometry and zoning.
// Borrowed query results are retained by their producers and released surface-first before terrain creation.
u8 SpManager::CreateTerrainSubway(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  CtCoord position = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
  ScOccupant *underground = nullptr, *surface = nullptr;
  u8 zoned = 0, overlay;
  subway_occupants->GetOccupant(position, &underground);
  surface_occupants->GetOccupant(position, &surface);
  zone->GetValue(x, z, &zoned);
  if (!underground) {
    if (surface) {
      if (surface->MajorType() == 1)
        overlay = 43;
      else if (surface->IsGeomFlag(kOcGeomTransitPiece))
        overlay = 46;
      else
        overlay = zoned ? 45 : 39;
    } else
      overlay = zoned ? 45 : 39;
  } else if (surface) {
    if (surface->MajorType() == 1)
      overlay = 43;
    else if (surface->IsGeomFlag(kOcGeomTransitPiece))
      overlay = 50;
    else
      overlay = zoned ? 49 : 39;
  } else
    overlay = zoned ? 45 : 39;
  if (surface)
    surface->Release();
  if (underground)
    underground->Release();
  return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, overlay);
}

// Choose the pipe-view overlay from water coverage, underground pipes, surface geometry and zoning.
// Keep each branch's original geometry-query order and release both occupant query holds before drawing the tile.
u8 SpManager::CreateTerrainPipe(u32 x, u32 z, u8 *altitude, SpSprite **out) const {
  CtCoord position = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
  ScOccupant *underground = nullptr, *surface = nullptr;
  u8 zoned = 0, overlay;
  pipe_occupants->GetOccupant(position, &underground);
  surface_occupants->GetOccupant(position, &surface);
  zone->GetValue(x, z, &zoned);
  if (!plumbing->IsWatered(position)) {
    if (underground) {
      if (surface)
        overlay = surface->IsGeomFlag(kOcGeomTransitPiece) ? 50 : 49;
      else
        overlay = zoned ? 45 : 49;
    } else if (surface) {
      if (surface->MajorType() == 1)
        overlay = 43;
      else if (surface->IsGeomFlag(kOcGeomTransitPiece))
        overlay = 46;
      else
        overlay = zoned ? 45 : 39;
    } else
      overlay = zoned ? 45 : 39;
  } else if (underground && surface) {
    if (surface->MajorType() == 1)
      overlay = 42;
    else if (surface->IsGeomFlag(kOcGeomTransitPiece))
      overlay = 47;
    else
      overlay = zoned ? 44 : 47;
  } else if (surface) {
    if (surface->MajorType() == 1)
      overlay = 42;
    else if (surface->IsGeomFlag(kOcGeomTransitPiece))
      overlay = 48;
    else
      overlay = zoned ? 44 : 41;
  } else if (zoned)
    overlay = 44;
  else
    overlay = underground ? 47 : 41;
  if (surface)
    surface->Release();
  if (underground)
    underground->Release();
  return dirt->GetDirtClod(terrain_kind, x, z, dry_only != 0, out, altitude, overlay);
}

// Replace a cell with the active terrain view under the city's recursive graphics lock.
void SpManager::UpdateTerrainQuad(u32 x, u32 z, bool) const {
  city->LockGraphics();
  u8 altitude;
  SpSprite *sprite;
  if (CreateTerrainSprite(x, z, &altitude, &sprite)) {
    cell_map->Remove(x, z, invalidate_changes != 0);
    cell_map->Insert(sprite, x, z, altitude, 1, invalidate_changes != 0, 1);
    sprite->Release();
  }
  city->UnlockGraphics();
}

// Visit the inclusive original terrain rectangle in X-major order.
void SpManager::UpdateTerrainMesh(u32 min_x, u32 min_z, u32 max_x, u32 max_z, bool update) const {
  for (u32 x = min_x; x <= max_x; ++x)
    for (u32 z = min_z; z <= max_z; ++z)
      UpdateTerrainQuad(x, z, update);
}

// Replace the occupant's square footprint with its aligned artwork and up to two animated chain sprites.
// Terrain-wrapped sprites use the real wrapper class; preserve failed-placement accumulation and ownership order.
bool SpManager::InsertCellAlignedSprite(ScOccupant *occupant, bool preserve_existing) const {
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  u32 x = (u32)(position.x >> kCtMicroCoordinateShift), z = (u32)(position.z >> kCtMicroCoordinateShift);
  if (preserve_existing) {
    SpSprite *existing = cell_map->GetSprite((i32)x, (i32)z);
    if (existing && !OccupantIsDirtClod(existing, true)) {
      lock->Unlock();
      return true;
    }
  }
  u8 size = occupant->SizeInCellsX();
  u32 last_x = x + size - 1u, last_z = z + size - 1u;
  for (u32 column = x; column <= last_x; ++column)
    for (u32 row = z; row <= last_z; ++row)
      cell_map->Remove(column, row, false);
  SpSprite *sprite = occupant->GetSpriteInst(sprite_layer);
  if (!sprite)
    sprite = occupant->GetSpriteInst(1);
  if (!sprite) {
    lock->Unlock();
    return false;
  }
  u32 layer_flags = sprite->GetLayerFlags();
  i32 priority = SpLayerPriority(0, layer_flags), first_priority = SpLayerPriority(1, layer_flags);
  i32 second_priority = SpLayerPriority(2, layer_flags);
  bool elevated = (layer_flags & kSpLayerElevated) != 0, ready = true, failed = false;
  if (layer_flags & 0x40u) {
    u8 altitude = 0;
    SpSprite *terrain = nullptr;
    ready = CreateTerrainSprite((u32)(position.x >> kCtMicroCoordinateShift),
                (u32)(position.z >> kCtMicroCoordinateShift), &altitude, &terrain) != 0;
    failed = !ready;
    if (ready) {
      sprite->SetNext(terrain);
      if (!priority)
        priority = 128;
      terrain->Release();
    }
  }
  if (layer_flags & kSpLayerTerrainWrapped) {
    u8 altitude = 0;
    SpSprite *terrain = nullptr;
    bool created = CreateTerrainSprite((u32)(position.x >> kCtMicroCoordinateShift),
                       (u32)(position.z >> kCtMicroCoordinateShift), &altitude, &terrain) != 0;
    failed |= !created;
    if (created) {
      if (terrain) {
        SpTerrainSprite *wrapper = new SpTerrainSprite(terrain);
        wrapper->SetNext(sprite);
        sprite = wrapper;
        terrain->Release();
      }
      failed |= !cell_map->Insert(sprite, (u32)(position.x >> kCtMicroCoordinateShift),
          (u32)(position.z >> kCtMicroCoordinateShift), altitude, size, invalidate_changes != 0,
          1u | (elevated ? 2u : 0));
    }
    second_priority = first_priority;
    first_priority = priority;
  } else {
    if (ready)
      ready = cell_map->Insert(sprite, (u32)(position.x >> kCtMicroCoordinateShift),
          (u32)(position.z >> kCtMicroCoordinateShift), (u8)(position.altitude >> kCtMicroCoordinateShift), size,
          invalidate_changes != 0, ((layer_flags & 0x80u) != 0) | (elevated ? 2u : 0));
    failed |= !ready;
  }
  SpSprite *first = sprite->GetNext();
  if (first) {
    first->SetGeomFlag(kSpLayerAnchorAdjustmentGeometry);
    u32 flags = elevated ? 2u : 1u;
    failed |= !cell_map->InsertDynamicSprite(first, (i32)((u32)position.x + 254u), position.z, position.altitude,
        invalidate_changes != 0, first_priority, flags);
    SpSprite *second = first->GetNext();
    if (second) {
      second->SetGeomFlag(kSpLayerAnchorAdjustmentGeometry);
      failed |= !cell_map->InsertDynamicSprite(second, (i32)((u32)position.x + 254u), position.z, position.altitude,
          invalidate_changes != 0, second_priority, flags);
    }
  }
  lock->Unlock();
  return !failed;
}

// Remove an occupant's animated chain in reverse order and restore its entire terrain footprint.
// Pure terrain already in the map needs no work; a missing occupant sprite declines the replacement.
bool SpManager::RemoveCellAlignedSprite(ScOccupant *occupant) const {
  GeBounds bounds = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  occupant->GetBounds(&bounds);
  u32 min_x = (u32)(bounds.minimum.x >> kCtMicroCoordinateShift),
      min_z = (u32)(bounds.minimum.z >> kCtMicroCoordinateShift);
  u32 max_x = (u32)(bounds.maximum.x >> kCtMicroCoordinateShift),
      max_z = (u32)(bounds.maximum.z >> kCtMicroCoordinateShift);
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  SpSprite *existing = cell_map->GetSprite((i32)min_x, (i32)min_z);
  if (existing && !OccupantIsDirtClod(existing, true)) {
    SpSprite *sprite = occupant->GetSpriteInst(sprite_layer);
    if (!sprite) {
      lock->Unlock();
      return false;
    }
    if (!(sprite->GetLayerFlags() & kSpLayerTerrainWrapped))
      sprite = sprite->GetNext();
    if (sprite) {
      SpSprite *second = sprite->GetNext();
      if (second)
        cell_map->RemoveDynamicSprite(second, (i32)((min_x << kCtMicroCoordinateShift) + 254u),
            (i32)(min_z << kCtMicroCoordinateShift), invalidate_changes != 0);
      cell_map->RemoveDynamicSprite(sprite, (i32)((min_x << kCtMicroCoordinateShift) + 254u),
          (i32)(min_z << kCtMicroCoordinateShift), invalidate_changes != 0);
    }
    UpdateTerrainMesh(min_x, min_z, max_x, max_z, true);
  }
  lock->Unlock();
  return true;
}

// Refresh the aligned sprite's highlight, terrain link and registration, then propagate the change to its chain.
// Preserve the source's reuse of the layer-flags word as a priority after rebuilding a terrain link.
bool SpManager::ChangeCellAlignedSprite(ScOccupant *occupant, u32 flags, void *payload, u32 color) const {
  GeBounds bounds = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  occupant->GetBounds(&bounds);
  i32 x = bounds.minimum.x >> kCtMicroCoordinateShift, z = bounds.minimum.z >> kCtMicroCoordinateShift;
  if (!color)
    FindHighlightColor((u32)x, (u32)z, (u32)(bounds.maximum.x >> kCtMicroCoordinateShift),
        (u32)(bounds.maximum.z >> kCtMicroCoordinateShift), &color);
  u32 previous_layer = sprite_layer;
  ScLock *lock = city->GetGraphicsCriticalSection();
  lock->Lock();
  SpSprite *sprite = occupant->GetSpriteInst(sprite_layer);
  if (!sprite) {
    lock->Unlock();
    return false;
  }
  u32 layer_flags = sprite->GetLayerFlags();
  if (layer_flags & 0x40u) {
    u8 altitude;
    SpSprite *terrain = nullptr;
    if (CreateTerrainSprite((u32)x, (u32)z, &altitude, &terrain)) {
      sprite->SetNext(terrain);
      layer_flags = (u32)SpLayerPriority(0, layer_flags);
      if (!layer_flags)
        layer_flags = 128;
      terrain->Release();
    }
  }
  bool result = true;
  if (layer_flags & kSpLayerTerrainWrapped) {
    occupant->SizeInCellsX();
    if (occupant_filter && !occupant_filter->Test(occupant)) {
      lock->Unlock();
      return true;
    }
    RemoveCellAlignedSprite(occupant);
    InsertCellAlignedSprite(occupant, false);
    sprite = occupant->GetSpriteInst(previous_layer);
    sprite->SetEffect(color);
  } else {
    if (!occupant_filter || occupant_filter->Test(occupant))
      sprite->SetEffect(color);
    result = cell_map->ChangeSprite((u32)x, (u32)z, invalidate_changes != 0, flags, payload);
    sprite = sprite->GetNext();
  }
  if (sprite) {
    result = cell_map->ChangeDynamicSprite(sprite, (i32)(((u32)x << kCtMicroCoordinateShift) + 254u),
        (i32)((u32)z << kCtMicroCoordinateShift), flags, payload);
    SpSprite *second = sprite->GetNext();
    if (second)
      result = cell_map->ChangeDynamicSprite(second, (i32)(((u32)x << kCtMicroCoordinateShift) + 254u),
          (i32)((u32)z << kCtMicroCoordinateShift), flags, payload);
  }
  lock->Unlock();
  return result;
}

// Acquire the original manager's view predicates in allocation/query order, retaining every successful product.
void SpManager::AcquireOccTests() {
  empty_test = ScAddRefNotNull(new SpEmptyTest);
  surface_test = ScAddRefNotNull(new SpSurfaceTest(city->ZoneLayer()));
  surface_test->SetTest(true, true, true, true, true, true);
  if (networks->QueryPredicate(reinterpret_cast<void **>(&road_test), IID_OcIteratorTest, 2))
    road_test->AddRef();
  if (networks->QueryPredicate(reinterpret_cast<void **>(&rail_test), IID_OcIteratorTest, 4))
    rail_test->AddRef();
  if (networks->QueryPredicate(reinterpret_cast<void **>(&network_test), IID_OcIteratorTest, 1))
    network_test->AddRef();
  subway_test = ScAddRefNotNull(new SpSubwayTest);
  pipe_test = ScAddRefNotNull(new SpPipeTest);
  flammability_test = ScAddRefNotNull(new SpFlammabilityTest);
  crime_test = ScAddRefNotNull(new SpCrimeTest);
  traffic_test = ScAddRefNotNull(new SpTrafficTest);
  power_test = ScAddRefNotNull(new SpPowerTest);
  spare_test = ScAddRefNotNull(new SpEmptyTest);
}

// Seed the original surface-view filters without retaining either alias or selecting an occupant manager.
void SpManager::SetupViewFilters() {
  OcIteratorTest *surface = surface_test;
  current_occupants = nullptr;
  view_id = 0;
  occupant_filter = surface;
  view_filter = surface;
}

// Return the manager, message target or city-change receiver through its actual interface adjustment. Publish the
// supported identity before its virtual retain; rejected queries clear the output.
bool SpManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpManagerView);
  HANDLE_QUERY_INTERFACE(IID_SpManagerView, SpManagerView);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpManager::AddRef() {
  SpGetDirector()->AddRef();
  return ++references;
}

// Retire a module hold before decrementing a nonzero object count, then delete at the live zero count.
// The director callback can change that count, so it is read again after the callback and before deletion.
u32 SpManager::Release() {
  if (references) {
    SpGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

SpManager::~SpManager() {
}

// Allocate the two retained terrain-edge ranges once and subscribe to rotation changes before building them. Both
// original side lengths come from CellCountX, including the source's square-city assumption.
u8 SpManager::InitEdges() {
  if (!edge_sprites) {
    edge_compass = -1;
    edge_first_count = edge_second_count = city->CellCountX();
    edge_sprites = new SpSprite *[usize(edge_first_count) * 2] {};
    SpGetMessageServer()->AddNotification(this, kWnMsg_CameraChanging);
    SetEdgeCompass(cell_map->GetRotate());
  }
  return 1;
}

// Unsubscribe and remove the current compass's edge sprites before retiring their pointer storage.
// The original zeroes the second side count before the first after publishing a null storage pointer.
u8 SpManager::ShutdownEdges() {
  if (edge_sprites) {
    SpGetMessageServer()->RemoveNotification(this, kWnMsg_CameraChanging);
    SetEdgeCompass(-1);
    delete[] edge_sprites;
    edge_sprites = nullptr;
    edge_second_count = 0;
    edge_first_count = 0;
  }
  return 1;
}

// Remove both old edge ranges, publish the new compass and recreate both ranges in their original order.
// Seed only the native scratch coordinates; unsupported source compasses otherwise leave those outputs unwritten.
u8 SpManager::SetEdgeCompass(i32 compass) {
  if (compass == edge_compass)
    return 1;
  u32 x = 0, z = 0;
  if (edge_compass != -1) {
    for (u32 i = 0; i < edge_first_count; ++i) {
      GetEdgeElementCoords(true, i, &x, &z);
      RemoveEdgeElement(true, i, x, z);
    }
    for (u32 i = 0; i < edge_second_count; ++i) {
      GetEdgeElementCoords(false, i, &x, &z);
      RemoveEdgeElement(false, i, x, z);
    }
  }
  edge_compass = compass;
  if (compass != -1) {
    for (u32 i = 0; i < edge_first_count; ++i) {
      GetEdgeElementCoords(true, i, &x, &z);
      AddEdgeElement(true, i, x, z);
    }
    for (u32 i = 0; i < edge_second_count; ++i) {
      GetEdgeElementCoords(false, i, &x, &z);
      AddEdgeElement(false, i, x, z);
    }
  }
  return 1;
}

// Convert an index on either visible edge to its city-cell coordinate for the current compass.
// Unsupported compass values preserve both outputs while retaining the original byte-one result.
u8 SpManager::GetEdgeElementCoords(bool first, u32 index, u32 *x, u32 *z) {
  if (first) {
    switch (edge_compass) {
    case 0:
      *x = edge_second_count - 1;
      *z = index;
      break;
    case 1:
      *x = index;
      *z = 0;
      break;
    case 2:
      *x = 0;
      *z = index;
      break;
    case 3:
      *x = index;
      *z = edge_first_count - 1;
      break;
    }
  } else {
    switch (edge_compass) {
    case 0:
      *x = index;
      *z = edge_first_count - 1;
      break;
    case 1:
      *x = edge_second_count - 1;
      *z = index;
      break;
    case 2:
      *x = index;
      *z = 0;
      break;
    case 3:
      *x = 0;
      *z = index;
      break;
    }
  }
  return 1;
}

// Identify a visible edge while preserving the first edge's precedence at the shared corner.
// A cell outside both selected edges leaves its side and index outputs unchanged.
u8 SpManager::IsEdge(u32 x, u32 z, bool *first, u32 *index) {
  switch (edge_compass) {
  case 0:
    if (x == edge_second_count - 1) {
      *first = true;
      *index = z;
    } else if (z == edge_first_count - 1) {
      *first = false;
      *index = x;
    } else
      return 0;
    break;
  case 1:
    if (!z) {
      *first = true;
      *index = x;
    } else if (x == edge_second_count - 1) {
      *first = false;
      *index = z;
    } else
      return 0;
    break;
  case 2:
    if (!x) {
      *first = true;
      *index = z;
    } else if (!z) {
      *first = false;
      *index = x;
    } else
      return 0;
    break;
  case 3:
    if (z == edge_first_count - 1) {
      *first = true;
      *index = x;
    } else if (!x) {
      *first = false;
      *index = z;
    } else
      return 0;
    break;
  default:
    return 0;
  }
  return 1;
}

// Acquire an edge sprite under the city's actual graphics critical section, then insert it at zero altitude.
// Terrain supplies the edge's held reference; its returned altitude and insertion result are ignored by the source.
u8 SpManager::AddEdgeElement(bool first, u32 index, u32 x, u32 z) {
  SpSprite **slot = &edge_sprites[index + (first ? 0 : edge_first_count)];
  bool underground = view_id == 1 || view_id == 2;
  ScLock *critical = city->GetGraphicsCriticalSection();
  critical->Lock();
  u8 altitude;
  dirt->GetDirtClodEdge(x, z, first, underground, slot, &altitude);
  cell_map->InsertDynamicSprite(
      *slot, (i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0, invalidate_changes, 1, 0);
  critical->Unlock();
  return 1;
}

u8 SpManager::RemoveEdgeElement(bool first, u32 index, u32 x, u32 z) {
  SpSprite **slot = &edge_sprites[index + (first ? 0 : edge_first_count)];
  city->LockGraphics();
  cell_map->RemoveDynamicSprite(
      *slot, (i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), invalidate_changes);
  city->UnlockGraphics();
  (*slot)->Release();
  *slot = nullptr;
  return 1;
}

// Refresh an edge's dynamic position at zero altitude, also refreshing the other side at a shared corner.
// The original selects that companion from the compass and retains the graphics critical section across both moves.
u8 SpManager::UpdateEdgeElement(bool first, u32 index, u32 x, u32 z) {
  ScLock *critical = city->GetGraphicsCriticalSection();
  critical->Lock();
  i32 micro_x = (i32)(x << kCtMicroCoordinateShift), micro_z = (i32)(z << kCtMicroCoordinateShift);
  cell_map->MoveDynamicSprite(
      edge_sprites[index + (first ? 0 : edge_first_count)], micro_x, micro_z, micro_x, micro_z, 0);
  if (first) {
    if (!index && edge_compass > 1) {
      u32 companion = edge_compass == 2 ? 0 : edge_second_count - 1;
      cell_map->MoveDynamicSprite(edge_sprites[edge_first_count + companion], micro_x, micro_z, micro_x, micro_z, 0);
    } else if (index == edge_first_count - 1 && edge_compass < 2) {
      u32 companion = edge_compass == 1 ? 0 : edge_second_count - 1;
      cell_map->MoveDynamicSprite(edge_sprites[edge_first_count + companion], micro_x, micro_z, micro_x, micro_z, 0);
    }
  }
  critical->Unlock();
  return 1;
}

// Refresh every city cell with invalidation batched off, then invalidate the entire screen under the graphics lock.
// Restore the live invalidation byte before unlocking, preserving the source's callback-visible ordering.
bool SpManager::CellChangedAll(u32 sender) {
  invalidate_changes = 0;
  u32 width = city->CellCountX(), height = city->CellCountZ();
  for (u32 x = 0; x < width; ++x)
    for (u32 z = 0; z < height; ++z)
      CellChanged(sender, x, z);
  city->LockGraphics();
  cell_map->InvalidateEntireScreen();
  invalidate_changes = 1;
  city->UnlockGraphics();
  return true;
}

// Update a visible edge first, then refresh the cell's retained aligned occupant or regenerate its terrain.
// The occupant query's reference is released after the original change dispatch; the sender is not consulted.
bool SpManager::CellChanged(u32, u32 x, u32 z) {
  bool first;
  u32 index;
  if (IsEdge(x, z, &first, &index))
    UpdateEdgeElement(first, index, x, z);
  ScOccupant *occupant = nullptr;
  CtCoord position = {(i32)(x << kCtMicroCoordinateShift), (i32)(z << kCtMicroCoordinateShift), 0};
  if (current_occupants && current_occupants->GetOccupant(position, &occupant)) {
    ChangeCellAlignedSprite(occupant, 1, nullptr, 0);
    occupant->Release();
  } else
    UpdateTerrainQuad(x, z, false);
  return true;
}

// Batch large cell rectangles through the point operation, or update small occupied ranges with the original
// 1024-entry rolling duplicate list. Rejected occupants regenerate terrain and every acquired occupant is released.
bool SpManager::CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) {
  if (x0 == x1 && z0 == z1)
    return CellChanged(sender, x0, z0);
  bool whole = (z0 - z1) * (x0 - x1) >= 600u;
  if (whole)
    invalidate_changes = 0;
  if (whole || !current_occupants) {
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        CellChanged(sender, x, z);
    if (!invalidate_changes) {
      city->LockGraphics();
      cell_map->InvalidateEntireScreen();
      invalidate_changes = 1;
      city->UnlockGraphics();
    }
    return true;
  }
  ScOccupant *seen[1024];
  u32 count = 0;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z) {
      ScOccupant *occupant = nullptr;
      if (current_occupants->GetOccupantAtCell(x, z, &occupant)) {
        if (!occupant_filter || occupant_filter->Test(occupant)) {
          if (occupant->SizeInCellsX() <= 1)
            ChangeCellAlignedSprite(occupant, 1, nullptr, 0);
          else {
            u32 index = 0;
            while (index < count && seen[index] != occupant)
              ++index;
            if (index == count) {
              seen[count++] = occupant;
              ChangeCellAlignedSprite(occupant, 1, nullptr, 0);
              if (count == 1024)
                count = 0;
            }
          }
        } else
          UpdateTerrainQuad(x, z, false);
        occupant->Release();
      } else
        UpdateTerrainQuad(x, z, false);
    }
  return true;
}

// Initialize the original manager fields and notification identifiers. Service and effect references start empty; the
// cursor starts visible.
SpManager::SpManager() {
  city = nullptr;
  cell_map = nullptr;
  current_occupants = surface_occupants = pipe_occupants = subway_occupants = nullptr;
  occupant_filter = road_test = rail_test = network_test = view_filter = nullptr;
  cursor = nullptr;
  dirt = nullptr;
  zone = nullptr;
  aura = nullptr;
  crime = nullptr;
  land_value = nullptr;
  power = nullptr;
  plumbing = nullptr;
  flammability = nullptr;
  pollution = nullptr;
  valves = nullptr;
  traffic = nullptr;
  networks = nullptr;
  animated_occupants = nullptr;
  police = nullptr;
  fire = nullptr;
  cell_bits = nullptr;
  empty_test = spare_test = nullptr;
  surface_test = nullptr;
  subway_test = nullptr;
  pipe_test = nullptr;
  flammability_test = nullptr;
  crime_test = nullptr;
  traffic_test = nullptr;
  power_test = nullptr;
  pollution_maximum = 0;
  pollution_scale = pollution_threshold = 0.0f;
  view_id = 0;
  view_flags = sprite_layer = 0;
  invalidate_changes = 0;
  cursor_visible = 1;
  terrain_kind = dry_only = flag16 = flag216 = flag117 = flag218 = 0;
  good_cursor_color = bad_cursor_color = 0;
  city_width = city_height = 0;
  surface_sender = kOcSurfaceManagerId;
  pipe_sender = kOcPipeManagerId;
  subway_sender = kOcSubwayManagerId;
  animation_sender = kOcAnimManagerId;
  zone_sender = kTrDirtBagLayerType;
  cursor_sender = kSpCellCursorChangeSenderId;
  change_sender = kEcZoneLayerType;
  surface_effects = pipe_effects = subway_effects = nullptr;
  effects = nullptr;
  power_display = nullptr;
  radiation_display = nullptr;
  weather_sim = nullptr;
  animations_visible = animations_applied = 0;
}

// Update the surface test even when redraw is disabled. Grid-only changes request a later full redraw;
// rebuilding the occupant view retains the original change-mask and callback order.
void SpManager::SetCityViewFlags(u32 flags, bool update) {
  if (flags == view_flags)
    return;
  surface_test->SetTest((flags & kSpCityViewFlora) != 0, (flags & kSpCityViewPowerLines) != 0,
      (flags & kSpCityViewTransport) != 0, (flags & kSpCityViewZonedBuildings) != 0,
      (flags & kSpCityViewOtherBuildings) != 0, (flags & kSpCityViewZones) != 0);
  if (!update)
    return;
  u32 changed = view_flags ^ flags;
  bool zone_update = (changed & kSpCityViewZones) != 0, terrain_update = (changed & kSpCityViewTerrainLayerMask) != 0;
  bool occupant_update = (changed & kSpCityViewOccupantLayerMask) != 0;
  view_flags = flags;
  if (changed & kSpCityViewTerrainGrid) {
    dirt->SetGrid((view_flags & kSpCityViewTerrainGrid) != 0);
    if (!terrain_update && !occupant_update) {
      city->LockGraphics();
      cell_map->InvalidateEntireScreen();
      city->UnlockGraphics();
      return;
    }
  } else if (!terrain_update && !occupant_update)
    return;
  SetOccupantView(current_occupants, surface_test, surface_test, zone_update, false);
}

// Switch between surface, underground and data-view predicates using the original case mapping. Rebuild boundary
// sprites only when entering or leaving an underground view.
void SpManager::SetViewID(i32 id, bool) {
  if (id == view_id)
    return;
  bool underground = id == kSpCityViewSubway || id == kSpCityViewWaterPipes,
       previous_underground = view_id == kSpCityViewSubway || view_id == kSpCityViewWaterPipes;
  bool rebuild_edges = underground != previous_underground;
  if (rebuild_edges)
    ShutdownEdges();
  ScOccupantManager *manager = surface_occupants;
  OcIteratorTest *filter;
  bool force = true;
  terrain_kind = 4;
  dry_only = 0;
  view_id = id;
  switch (id) {
  case kSpCityViewSurface:
    filter = surface_test;
    break;
  case kSpCityViewSubway:
    manager = subway_occupants;
    filter = subway_test;
    terrain_kind = 5;
    dry_only = 1;
    break;
  case kSpCityViewWaterPipes:
    manager = pipe_occupants;
    filter = pipe_test;
    terrain_kind = 5;
    dry_only = 1;
    break;
  case kSpCityViewAura:
  case kSpCityViewLandValue:
  case kSpCityViewDensity:
  case 18:
    filter = network_test;
    break;
  case kSpCityViewCrime:
    filter = crime_test;
    break;
  case kSpCityViewPower:
    filter = power_test;
    break;
  case kSpCityViewFlammability:
    filter = flammability_test;
    break;
  case kSpCityViewPollution:
  case 30:
    filter = empty_test;
    break;
  case kSpCityViewTraffic:
    filter = traffic_test;
    break;
  default:
    view_id = kSpCityViewSurface;
    filter = surface_test;
    force = false;
    break;
  }
  SetOccupantView(manager, filter, filter, false, force);
  view_id = id;
  if (rebuild_edges)
    InitEdges();
}

// Retain the three terrain occupant managers and the separate animation manager in source order. Failed acquisition
// dispatches the original manager shutdown.
u8 SpManager::RegisterTerrain() {
  surface_occupants = city->SurfaceOccupantManager();
  if (!surface_occupants) {
    Shutdown();
    return 0;
  }
  surface_occupants->AddRef();
  pipe_occupants = city->UndergroundOccupantManagerLevelOne();
  if (!pipe_occupants) {
    Shutdown();
    return 0;
  }
  pipe_occupants->AddRef();
  subway_occupants = city->UndergroundOccupantManagerLevelTwo();
  if (!subway_occupants) {
    Shutdown();
    return 0;
  }
  subway_occupants->AddRef();
  animated_occupants = city->AnimOccupantManager();
  if (!animated_occupants) {
    Shutdown();
    return 0;
  }
  animated_occupants->AddRef();
  animated_occupants->RegisterChangeReceiver(this, &animation_sender, true);
  return 1;
}

// Acquire the cursor, zone change sources and actual city-layer interfaces in the original order. Layer query failures
// return directly; earlier cursor/zone failures dispatch shutdown.
u8 SpManager::Register() {
  SpGetMessageServer()->AddNotification(this, kSpMsg_CursorCellsChanged);
  SpGetMessageServer()->AddNotification(this, kSpMsg_CursorRectangleChanged);
  SpGetMessageServer()->AddNotification(this, kSpMsg_CursorCellChanged);
  cursor = SpGetCityWindowView()->GetCellCursorMap();
  if (!cursor) {
    Shutdown();
    return 0;
  }
  cursor->AddRef();
  CtChangeSource *source = nullptr;
  cursor->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source));
  if (!source) {
    Shutdown();
    return 0;
  }
  source->AddReceiver(this, &cursor_sender);
  source->Release();
  source = nullptr;
  zone = city->ZoneLayer();
  if (!zone) {
    Shutdown();
    return 0;
  }
  zone->AddRef();
  zone->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source));
  if (!source) {
    Shutdown();
    return 0;
  }
  source->AddReceiver(this, &change_sender);
  source->Release();
  if (!city->GetSpecificLayer(kEcLandValueLayerType)
          ->QueryInterface(IID_CtLandValueLayer, reinterpret_cast<void **>(&land_value)))
    return 0;
  if (!city->GetSpecificLayer(kCtAuraLayerType)->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura)))
    return 0;
  if (!city->GetSpecificLayer(kCtPowerSimulationLayerType)
          ->QueryInterface(IID_CtPowerLayer, reinterpret_cast<void **>(&power)))
    return 0;
  if (!city->GetSpecificLayer(kCtCrimeLayerType)->QueryInterface(IID_CtCrimeLayer, reinterpret_cast<void **>(&crime)))
    return 0;
  if (!city->GetSpecificLayer(kCtPoliceLayerType)
          ->QueryInterface(IID_CtPoliceLayer, reinterpret_cast<void **>(&police)))
    return 0;
  if (!city->GetSpecificLayer(kEcPollutionLayerType)
          ->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution)))
    return 0;
  pollution_maximum = (u32)pollution->GetMaxExpectedAirValue();
  pollution_scale = (f32)(31.0 / ((f64)pollution_maximum - 1.0));
  pollution_threshold = 1.0f;
  if (!city->GetSpecificLayer(kCtFireLayerType)->QueryInterface(IID_CtFireLayer, reinterpret_cast<void **>(&fire)))
    return 0;
  if (!city->GetSpecificLayer(kCtFlammabilityLayerType)
          ->QueryInterface(IID_EnHazardMap, reinterpret_cast<void **>(&flammability)))
    return 0;
  if (!city->GetSpecificLayer(kNwTransitLayerType)
          ->QueryInterface(IID_NwTrafficLayer, reinterpret_cast<void **>(&traffic)))
    return 0;
  if (!city->GetSpecificLayer(kNwManagerLayerType)->QueryInterface(IID_NwManager, reinterpret_cast<void **>(&networks)))
    return 0;
  if (!city->GetSpecificLayer(kCtPlumbingSimulationLayerType)
          ->QueryInterface(IID_CtPlumbingLayer, reinterpret_cast<void **>(&plumbing)))
    return 0;
  if (!city->GetSpecificLayer(kEcDemandLayerType)->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves)))
    return 0;
  dirt = ScAddRefNotNull(city->DirtBag());
  SpGetMessageServer()->AddNotification(this, kTrMsg_TerrainChanged);
  SpGetMessageServer()->AddNotification(this, kTrMsg_AltitudePaletteChanged);
  return 1;
}

// Remove real change receivers and notification subscriptions before releasing retained city services. Each field is
// cleared after its release callback, preserving source reentrancy order.
u8 SpManager::Unregister() {
  if (animated_occupants) {
    animated_occupants->UnregisterChangeReceiver(this);
    animated_occupants->Release();
    animated_occupants = nullptr;
  }
  ScRelease(&pipe_occupants);
  ScRelease(&subway_occupants);
  ScRelease(&surface_occupants);
  if (cursor) {
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_CursorCellsChanged);
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_CursorRectangleChanged);
    SpGetMessageServer()->RemoveNotification(this, kSpMsg_CursorCellChanged);
    CtChangeSource *source = nullptr;
    cursor->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source));
    if (source) {
      source->RemoveReceiver(this);
      source->Release();
    }
    cursor->Release();
    cursor = nullptr;
  }
  if (zone) {
    CtChangeSource *source = nullptr;
    zone->QueryInterface(IID_CtChangeSource, reinterpret_cast<void **>(&source));
    if (source) {
      source->RemoveReceiver(this);
      source->Release();
    }
    zone->Release();
    zone = nullptr;
  }
  ScRelease(&land_value);
  ScRelease(&aura);
  ScRelease(&crime);
  ScRelease(&police);
  ScRelease(&fire);
  ScRelease(&flammability);
  ScRelease(&traffic);
  ScRelease(&networks);
  ScRelease(&plumbing);
  ScRelease(&power);
  ScRelease(&pollution);
  ScRelease(&valves);
  if (dirt) {
    SpGetMessageServer()->RemoveNotification(this, kTrMsg_AltitudePaletteChanged);
    SpGetMessageServer()->RemoveNotification(this, kTrMsg_TerrainChanged);
    dirt->Release();
    dirt = nullptr;
  }
  ScRelease(&city);
  return 1;
}

// Test the original surface-manager identity before querying the zone color. The two color outputs are scratch values
// and are never read here.
u8 SpManager::CellNormallyHasZoneColor(u32 x, u32 z) {
  if (current_occupants != surface_occupants)
    return 0;
  u8 type;
  u16 color;
  return zone->GetZoneColor(x, z, &type, &color);
}

// Apply the reached 1001879E visibility sequence under the model lock followed by the graphics lock.
void SpManager::ShowAllOccupants(ScOccupantManager *manager, bool show) {
  ScLock *model = city->GetCriticalSection();
  model->Lock();
  ScLock *graphics = city->GetGraphicsCriticalSection();
  graphics->Lock();
  OcIterator *iterator;
  if (manager->GetIterator(&iterator)) {
    iterator->Init(nullptr, nullptr);
    for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next())
      if (show)
        InsertCellAlignedSprite(occupant, true);
      else
        RemoveCellAlignedSprite(occupant);
    iterator->Shutdown();
    iterator->Release();
  }
  graphics->Unlock();
  model->Unlock();
}

// Preserve 100184D9 terrain and aligned-occupant visibility effects, including the special zone-color refresh path.
void SpManager::ShowOrHideOccupants(ScOccupantManager *manager, OcIteratorTest *filter, bool zone_update) {
  ScLock *model = city->GetCriticalSection();
  model->Lock();
  ScLock *graphics = city->GetGraphicsCriticalSection();
  graphics->Lock();
  if (!filter)
    ShowAllOccupants(manager, true);
  else if (zone_update && manager == surface_occupants) {
    for (u32 x = 0; x < city_width; ++x)
      for (u32 z = 0; z < city_height; ++z) {
        ScOccupant *occupant;
        if (manager->GetOccupantAtCell(x, z, &occupant)) {
          u32 origin_x, origin_z;
          occupant->GetLocationInCellCoords(&origin_x, &origin_z);
          if (x == origin_x && z == origin_z) {
            bool show = filter->Test(occupant);
            city->LockGraphics();
            SpSprite *sprite = cell_map->GetSprite((i32)x, (i32)z);
            city->UnlockGraphics();
            if (OccupantIsDirtClod(sprite, false) && CellNormallyHasZoneColor(x, z)) {
              if (OccupantIsDirtClod(sprite, true)) {
                u32 max_z = occupant->SizeInCellsZ() + z - 1;
                u32 max_x = occupant->SizeInCellsX() + x - 1;
                UpdateTerrainMesh(x, z, max_x, max_z, true);
              } else
                RemoveCellAlignedSprite(occupant);
              if (show)
                InsertCellAlignedSprite(occupant, false);
            } else if (show)
              InsertCellAlignedSprite(occupant, true);
            else if (OccupantIsDirtClod(sprite, true)) {
              u32 max_z = occupant->SizeInCellsZ() + z - 1;
              u32 max_x = occupant->SizeInCellsX() + x - 1;
              UpdateTerrainMesh(x, z, max_x, max_z, true);
            } else
              RemoveCellAlignedSprite(occupant);
          }
          occupant->Release();
        } else if (CellNormallyHasZoneColor(x, z))
          UpdateTerrainQuad(x, z, true);
      }
  } else {
    OcIterator *iterator;
    if (manager->GetIterator(&iterator)) {
      iterator->Init(nullptr, nullptr);
      for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next())
        if (filter->Test(occupant))
          InsertCellAlignedSprite(occupant, true);
        else
          RemoveCellAlignedSprite(occupant);
      iterator->Shutdown();
      iterator->Release();
    }
  }
  graphics->Unlock();
  model->Unlock();
}

// Preserve 10018CDF view switching: replace filters/subscriptions, queue full invalidation, then refresh terrain.
void SpManager::SetOccupantView(ScOccupantManager *manager, OcIteratorTest *terrain_filter,
    OcIteratorTest *animation_filter, bool zone_update, bool force) {
  bool changed = current_occupants != manager;
  invalidate_changes = 0;
  cursor_visible = 0;
  if (current_occupants) {
    if (current_occupants == surface_occupants) {
      if (changed) {
        ShowAllOccupants(current_occupants, false);
        if (surface_effects)
          surface_effects->ShowOrHide(0);
        current_occupants->ClearAnimatedOccupantList(true);
        current_occupants->UnregisterChangeReceiver(this);
        SpGetMessageServer()->RemoveNotification(this, kEcMsg_WaterPollutionChanged);
      } else
        ShowOrHideOccupants(current_occupants, terrain_filter, force ? false : zone_update);
    } else if (current_occupants == pipe_occupants) {
      if (changed) {
        ShowAllOccupants(current_occupants, false);
        if (pipe_effects)
          pipe_effects->ShowOrHide(0);
        current_occupants->ClearAnimatedOccupantList(true);
        SpGetMessageServer()->RemoveNotification(this, kOcMsg_PipeInserted);
        SpGetMessageServer()->RemoveNotification(this, kOcMsg_PipeRemoved);
        SpGetMessageServer()->RemoveNotification(this, kOcMsg_PipeChanged);
        SpGetMessageServer()->RemoveNotification(this, kCtMsg_WaterCapacityUpdated);
      } else
        ShowOrHideOccupants(current_occupants, terrain_filter, force ? false : zone_update);
    } else if (current_occupants == subway_occupants) {
      if (changed) {
        ShowAllOccupants(current_occupants, false);
        if (subway_effects)
          subway_effects->ShowOrHide(0);
        current_occupants->ClearAnimatedOccupantList(true);
        SpGetMessageServer()->RemoveNotification(this, kOcMsg_SubwayInserted);
        SpGetMessageServer()->RemoveNotification(this, kOcMsg_SubwayRemoved);
        SpGetMessageServer()->RemoveNotification(this, kOcMsg_SubwayChanged);
      } else
        ShowOrHideOccupants(current_occupants, terrain_filter, force ? false : zone_update);
    }
  }
  occupant_filter = terrain_filter;
  current_occupants = manager;
  view_filter = animation_filter;
  animated_occupants->SetTest(animation_filter, true);
  bool update_surface = false;
  if (manager == surface_occupants) {
    sprite_layer = 1;
    if (changed) {
      ShowOrHideOccupants(current_occupants, terrain_filter, false);
      if (surface_effects)
        surface_effects->ShowOrHide(1);
      current_occupants->RegisterChangeReceiver(this, &surface_sender, false);
      SpGetMessageServer()->AddNotification(this, kEcMsg_WaterPollutionChanged);
    }
    update_surface = true;
  } else if (manager == pipe_occupants) {
    sprite_layer = 2;
    if (changed) {
      ShowOrHideOccupants(current_occupants, terrain_filter, false);
      if (pipe_effects)
        pipe_effects->ShowOrHide(1);
      SpGetMessageServer()->AddNotification(this, kOcMsg_PipeInserted);
      SpGetMessageServer()->AddNotification(this, kOcMsg_PipeRemoved);
      SpGetMessageServer()->AddNotification(this, kOcMsg_PipeChanged);
      SpGetMessageServer()->AddNotification(this, kCtMsg_WaterCapacityUpdated);
    }
    update_surface = true;
  } else if (manager == subway_occupants) {
    sprite_layer = 4;
    if (changed) {
      ShowOrHideOccupants(current_occupants, terrain_filter, false);
      if (subway_effects)
        subway_effects->ShowOrHide(1);
      SpGetMessageServer()->AddNotification(this, kOcMsg_SubwayInserted);
      SpGetMessageServer()->AddNotification(this, kOcMsg_SubwayRemoved);
      SpGetMessageServer()->AddNotification(this, kOcMsg_SubwayChanged);
    }
    update_surface = true;
  } else if (!manager && cell_map)
    update_surface = true;
  if (update_surface) {
    city->LockGraphics();
    cell_map->InvalidateEntireScreen();
    city->UnlockGraphics();
  }
  ScLock *graphics = city->GetGraphicsCriticalSection();
  graphics->Lock();
  u32 end_x = city_width;
  for (u32 x = 0; x < end_x; ++x) {
    u32 end_z = city_height;
    for (u32 z = 0; z < end_z; ++z) {
      SpSprite *sprite = cell_map->GetSprite((i32)x, (i32)z);
      if (sprite) {
        if ((!changed && !force) || !OccupantIsDirtClod(sprite, true))
          continue;
      } else if (!manager)
        continue;
      UpdateTerrainQuad(x, z, true);
    }
  }
  invalidate_changes = cursor_visible = 1;
  graphics->Unlock();
}

void SpManager::DoDataViewChange() {
  if (view_id >= 10 && view_id <= 30)
    SetOccupantView(current_occupants, occupant_filter, view_filter, false, true);
}

void SpManager::SetGrid(bool enabled) {
  if (enabled != ((view_flags & kSpCityViewTerrainGrid) != 0))
    SetCityViewFlag(kSpCityViewTerrainGrid, enabled, false);
}

// Refresh the original packed X/Z pairs, suppressing repeated wide-occupant changes in a small batch.
// Large batches defer invalidation until all cell callbacks finish, then invalidate the screen once under the city lock.
bool SpManager::CellChanged(u32 sender, const u32 *coordinates, u32 count) {
  if (count == 1)
    return CellChanged(sender, coordinates[0], coordinates[1]);
  u32 words = count * 2u;
  if (words >= 600)
    invalidate_changes = 0;
  else if (current_occupants) {
    ScOccupant *seen[1024];
    u32 seen_count = 0;
    for (u32 at = 0; at < words; at += 2) {
      u32 x = coordinates[at], z = coordinates[at + 1];
      ScOccupant *occupant;
      if (current_occupants->GetOccupantAtCell(x, z, &occupant)) {
        if (!occupant_filter || occupant_filter->Test(occupant)) {
          if (occupant->SizeInCellsX() <= 1)
            ChangeCellAlignedSprite(occupant, 1, nullptr, 0);
          else {
            u32 index = 0;
            while (index < seen_count && seen[index] != occupant)
              ++index;
            if (index == seen_count) {
              u32 previous = seen_count;
              seen[seen_count++] = occupant;
              ChangeCellAlignedSprite(occupant, 1, nullptr, 0);
              if (previous == 1023)
                seen_count = 0;
            }
          }
        } else
          UpdateTerrainQuad(x, z, false);
        occupant->Release();
      } else
        UpdateTerrainQuad(x, z, false);
    }
    return true;
  }
  for (u32 at = 0; at < words; at += 2)
    CellChanged(sender, coordinates[at], coordinates[at + 1]);
  if (!invalidate_changes) {
    city->LockGraphics();
    cell_map->InvalidateEntireScreen();
    invalidate_changes = 1;
    city->UnlockGraphics();
  }
  return true;
}

// Lock the city's graphics section while retaining the city/map, registering real layers and preparing view predicates.
// Keep original dry-only byte +217 clear independently of +117; surface terrain must include its water plane.
bool SpManager::Init(CtCity *source_city, SpCellMap *map) {
  ScLock *critical = source_city->GetGraphicsCriticalSection();
  critical->Lock();
  city = ScAddRefNotNull(source_city);
  cell_map = ScAddRefNotNull(map);
  city_width = city->CellCountX();
  city_height = city->CellCountZ();
  cell_bits = new u32[(city_width * city_height) >> 5];
  animations_visible = SpGetApplication()->GetAppPreferences()->VisibilityPrefs()->other_animations_visibility != 0;
  SpGetGraphics()->ConvertRGBValueToNative16Bit(&good_cursor_color, 0, 138, 180);
  SpGetGraphics()->ConvertRGBValueToNative16Bit(&bad_cursor_color, 223, 44, 44);
  bool good = false;
  if (Register()) {
    dry_only = 0;
    terrain_kind = 4;
    flag117 = 1;
    view_id = 0;
    view_flags = 127;
    dirt->SetGrid((view_flags & kSpCityViewTerrainGrid) != 0);
    if (RegisterTerrain()) {
      AcquireOccTests();
      SetupViewFilters();
      SetOccupantView(surface_occupants, nullptr, nullptr, false, false);
      InitEdges();
      SpGetMessageServer()->AddNotification(this, kWnMsg_RefreshCityDataView);
      SpGetMessageServer()->AddNotification(this, kApMsgPreferencesChanged);
      InitCityViewEffects();
      good = true;
    }
  }
  critical->Unlock();
  return good;
}

// Shut down the actual power, radiation, neighbor, weather and shared effects in their original order.
void SpManager::ShutdownCityViewEffects() {
  if (power_display) {
    power_display->Shutdown();
    power_display->Release();
    power_display = nullptr;
  }
  if (radiation_display) {
    radiation_display->Shutdown();
    radiation_display->Release();
    radiation_display = nullptr;
  }
  if (surface_effects) {
    surface_effects->Shutdown();
    surface_effects->Release();
    surface_effects = nullptr;
  }
  if (pipe_effects) {
    pipe_effects->Shutdown();
    pipe_effects->Release();
    pipe_effects = nullptr;
  }
  if (subway_effects) {
    subway_effects->Shutdown();
    subway_effects->Release();
    subway_effects = nullptr;
  }
  if (weather_sim) {
    weather_sim->Shutdown();
    weather_sim->Release();
    weather_sim = nullptr;
    animations_applied = 0;
  }
  if (effects) {
    effects->ShutdownEffects();
    effects->Release();
    effects = nullptr;
  }
}

// Remove effect producers and notifications, clear animation lists and detach the current view under the graphics lock.
// Release filters in the original order before removing retained map sprites and dropping the manager's map reference.
bool SpManager::Shutdown() {
  ScLock *critical = city->GetGraphicsCriticalSection();
  critical->Lock();
  ShutdownCityViewEffects();
  SpGetMessageServer()->RemoveNotification(this, kWnMsg_RefreshCityDataView);
  SpGetMessageServer()->RemoveNotification(this, kApMsgPreferencesChanged);
  surface_occupants->ClearAnimatedOccupantList(true);
  pipe_occupants->ClearAnimatedOccupantList(true);
  subway_occupants->ClearAnimatedOccupantList(true);
  SetOccupantView(nullptr, nullptr, nullptr, false, false);
  if (cell_bits) {
    delete[] cell_bits;
    cell_bits = nullptr;
  }
  ShutdownEdges();
  Unregister();
  ScRelease(&surface_test);
  ScRelease(&subway_test);
  ScRelease(&pipe_test);
  ScRelease(&empty_test);
  ScRelease(&road_test);
  ScRelease(&rail_test);
  ScRelease(&network_test);
  ScRelease(&flammability_test);
  ScRelease(&crime_test);
  ScRelease(&traffic_test);
  ScRelease(&power_test);
  ScRelease(&spare_test);
  if (cell_map) {
    cell_map->RemoveAllDynamicSprites(false);
    cell_map->RemoveAll();
    cell_map->Release();
    cell_map = nullptr;
  }
  critical->Unlock();
  return true;
}

// Ask plumbing for its packed difference map only while displaying the pipe occupant manager.
// Walk changed bits in row order and dispatch each real cell change without clearing the producer's words.
void SpManager::RefreshPipeCells() {
  u32 words = (city_height * city_width) >> 5;
  if (current_occupants != pipe_occupants || !plumbing || !cell_bits || !plumbing->GetDiffMap(cell_bits, words))
    return;
  u32 index = 0;
  for (u32 z = 0; z < city_height; ++z) {
    u32 x = 0;
    for (u32 chunk = 0; chunk < city_width; chunk += 32) {
      u32 bits = cell_bits[index++];
      if (bits) {
        if (!(bits & 0xffff)) {
          x += 16;
          bits >>= 16;
        }
        if (!(bits & 0xff)) {
          x += 8;
          bits >>= 8;
        }
        for (; bits; bits >>= 1) {
          if (bits & 1)
            CellChanged(0, x, z);
          ++x;
        }
      }
      x = chunk + 32;
    }
  }
}

// Preserve filter gates, packed coordinate order, callback results and the original false result for unknown messages.
bool SpManager::DoMessage(ScMessage *message) {
  ScMsgCode type = message->type;
  switch (type) {
  case kSpMsg_CursorCellChanged:
    CellChanged(0, (u32)message->data1, (u32)message->data2);
    return true;
  case kSpMsg_CursorRectangleChanged:
  case kTrMsg_TerrainChanged: {
    const ScRect *rectangle = reinterpret_cast<const ScRect *>(message->data1);
    CellChanged(0, (u32)rectangle->left, (u32)rectangle->top, (u32)rectangle->right, (u32)rectangle->bottom);
    return true;
  }
  case kSpMsg_CursorCellsChanged:
    CellChanged(0, reinterpret_cast<const u32 *>(message->data1), (u32)message->data2);
    return true;
  case kCtMsg_WaterCapacityUpdated:
    if (current_occupants == pipe_occupants)
      RefreshPipeCells();
    return true;
  case kTrMsg_AltitudePaletteChanged:
    CellChangedAll(0);
    return true;
  case kEcMsg_WaterPollutionChanged:
    if (message->data1) {
      u32 packed = (u32)message->data2, x = packed >> 16, z = packed & 0xffffu;
      if (dirt->IsWater(x, z)) {
        u32 left = x ? x - 1 : 0, top = z ? z - 1 : 0;
        if (x < city_width - 1)
          ++x;
        if (z < city_height - 1)
          ++z;
        for (u32 column = left; column <= x; ++column)
          for (u32 row = top; row <= z; ++row)
            if (dirt->IsWater(column, row)) {
              CtCoord position = {(i32)(column << kCtMicroCoordinateShift), (i32)(row << kCtMicroCoordinateShift), 0};
              ChangeCellAlignedSpriteInst(position, 1, nullptr);
            }
      }
    }
    return true;
  case kWnMsg_CameraChanging:
    SetEdgeCompass((i32)message->data2);
    return true;
  case kApMsgPreferencesChanged:
    animations_visible = SpGetApplication()->GetAppPreferences()->VisibilityPrefs()->other_animations_visibility != 0;
    if (animations_visible != animations_applied) {
      animations_applied = animations_visible;
      if (weather_sim)
        weather_sim->ShowOrHide(animations_visible);
    }
    return true;
  case kWnMsg_RefreshCityDataView:
    if (!view_id)
      return false;
    DoDataViewChange();
    return true;
  default:
    break;
  }
  bool pipe = current_occupants == pipe_occupants, subway = current_occupants == subway_occupants;
  if ((type == kOcMsg_PipeInserted && pipe) || (type == kOcMsg_SubwayInserted && subway)) {
    ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1);
    if (!occupant_filter || occupant_filter->Test(occupant))
      return InsertCellAlignedSprite(occupant, false);
    return true;
  }
  if ((type == kOcMsg_PipeRemoved && pipe) || (type == kOcMsg_SubwayRemoved && subway)) {
    ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1);
    if (!occupant_filter || occupant_filter->Test(occupant))
      return RemoveCellAlignedSprite(occupant);
    return true;
  }
  if ((type == kOcMsg_PipeChanged && pipe) || (type == kOcMsg_SubwayChanged && subway)) {
    ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1);
    if (!occupant) {
      u32 packed = (u32)message->data3;
      return CellChanged(0, packed >> 24, (packed >> 16) & 255u, (packed >> 8) & 255u, packed & 255u);
    }
    if (!occupant_filter || occupant_filter->Test(occupant))
      return ChangeCellAlignedSprite(occupant, (u32)message->data2, nullptr, 0);
    return true;
  }
  if (type == kOcMsg_PipeInserted || type == kOcMsg_SubwayInserted || type == kOcMsg_PipeRemoved ||
      type == kOcMsg_SubwayRemoved || type == kOcMsg_PipeChanged || type == kOcMsg_SubwayChanged)
    return true;
  return false;
}

// Initialize the registered shared effects, then construct the original power, radiation, neighbor and weather displays.
// Neighbor membership follows the active occupant filter; weather visibility is applied only after its real Init.
void SpManager::InitCityViewEffects() {
  SpGetCom()->GetClassObject(kSpEffectsClassId, IID_SpEffects, reinterpret_cast<void **>(&effects));
  if (effects) {
    effects->AddRef();
    effects->InitEffects(cell_map);
  }
  power_display = ScAddRefNotNull(new SpPower);
  power_display->Init(city_width, city_height);
  radiation_display = ScAddRefNotNull(new SpRadiation);
  radiation_display->Init(1);
  surface_effects = ScAddRefNotNull(new SpNeighbors);
  surface_effects->ShowOrHide(0);
  surface_effects->Init(city->SurfaceOccupantManager(), 0, 1);
  pipe_effects = ScAddRefNotNull(new SpNeighbors);
  pipe_effects->ShowOrHide(0);
  pipe_effects->Init(city->UndergroundOccupantManagerLevelOne(), 1, 1);
  subway_effects = ScAddRefNotNull(new SpNeighbors);
  subway_effects->ShowOrHide(0);
  subway_effects->Init(city->UndergroundOccupantManagerLevelTwo(), 2, 1);
  if (view_filter == pipe_test)
    pipe_effects->ShowOrHide(1);
  else if (view_filter == subway_test)
    subway_effects->ShowOrHide(1);
  else
    surface_effects->ShowOrHide(1);
  weather_sim = ScAddRefNotNull(new SpWeatherSim);
  weather_sim->Init();
  weather_sim->ShowOrHide(animations_visible);
  animations_applied = animations_visible;
}

// The city-window owner supplies the original4C962 secondary-interface factory in city_window.cpp.
ScUnknown *SpCreateCityWindow();
ScUnknown *SpCreateCitySpriteWindow();

// The module's actual director adds only registration, its identity and the image-service startup hook.
// Common reference, framework and factory-query behavior stays in the recovered DLL-director base.
class SpDirector : public ScDllDirector {
public:
  SpDirector();
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

static ScMessageServer *SpMessageServer;
static RsManager *SpResourceManager;
static ScLanguageManager *SpLanguageManager;
static RsFileSystem *SpFileSystem;
static GxGraphics *SpGraphics;
static WnResources *SpWindowResources;
static ApGameInterface *SpApplication;
static bool SpApplicationInitialized;

// Obtain the framework from the actual module director. This helper returns a framework pointer, not a COM object.
ScFramework *SpGetFramework() {
  return SpGetDirector()->FrameWork();
}

// Publish the module's initialization guard before querying the framework application into its retained cache.
// Failed first acquisition remains cached as null; the original registered exit callback has no effects.
ApGameInterface *SpGetApplication() {
  if (!SpApplicationInitialized) {
    SpApplicationInitialized = true;
    SpGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&SpApplication));
  }
  return SpApplication;
}

// Return the module director's actual GZCOM interface, distinct from the separate class-factory service.
ScCom *SpGetCom() {
  return SpGetDirector()->GZCOM();
}

// Cache the framework's message service before dropping its acquisition reference. Recursive callers see the cache.
ScMessageServer *SpGetMessageServer() {
  if (!SpMessageServer) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *server = static_cast<ScMessageServer *>(object);
    SpMessageServer = server;
    if (server)
      server->Release();
  }
  return SpMessageServer;
}

// Cache the real resource-manager service before releasing its temporary acquisition reference.
// Reentrant lookups during Release observe that borrowed cache; unsuccessful acquisition remains retryable.
RsManager *SpGetResourceManager() {
  if (!SpResourceManager) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    SpResourceManager = manager;
    if (manager)
      manager->Release();
  }
  return SpResourceManager;
}

// Cache the language-manager interface before releasing its acquisition reference. Failed lookups remain retryable.
ScLanguageManager *SpGetLanguageManager() {
  if (!SpLanguageManager) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    ScLanguageManager *manager = static_cast<ScLanguageManager *>(object);
    SpLanguageManager = manager;
    if (manager)
      manager->Release();
  }
  return SpLanguageManager;
}

// Return the application's original preferences interface when the module application accessor is available.
ApPreferences *SpGetPreferences() {
  ApGameInterface *application = SpGetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Acquire the real sprite image-cache service and release its temporary query reference before returning it.
// This getter performs a fresh service lookup on every call; the returned interface remains borrowed.
SpImageCache *SpGetImageCache() {
  void *object = nullptr;
  SpGetFramework()->GetSystemService(kSpImageCacheServiceId, IID_SpImageCache, &object);
  SpImageCache *cache = static_cast<SpImageCache *>(object);
  if (cache)
    cache->Release();
  return cache;
}

// Cache the real filesystem service before releasing its temporary acquisition reference. Recursive calls during
// Release observe the same borrowed pointer; a failed lookup remains retryable on the next call.
RsFileSystem *SpGetFileSystem() {
  if (!SpFileSystem) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(RsFileSystem::ServiceId, RsFileSystem::Iid, &object);
    RsFileSystem *system = static_cast<RsFileSystem *>(object);
    SpFileSystem = system;
    if (system)
      system->Release();
  }
  return SpFileSystem;
}

// Cache the actual graphics service before releasing its acquisition reference. Reentrant Release observes that
// borrowed pointer, and a failed lookup leaves the next call free to retry.
GxGraphics *SpGetGraphics() {
  if (!SpGraphics) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &object);
    GxGraphics *graphics = static_cast<GxGraphics *>(object);
    SpGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return SpGraphics;
}

// Obtain the actual effects service on each call. Release its acquisition reference and return the borrowed identity.
GxBlendService *SpGetBlendService() {
  void *object = nullptr;
  SpGetFramework()->GetSystemService(kGxBlendServiceId, IID_GxBlendService, &object);
  GxBlendService *service = static_cast<GxBlendService *>(object);
  if (service)
    service->Release();
  return service;
}

// Cache the real window-resource service before dropping its temporary acquisition reference.
// Its GetColor operation supplies the sprite manager's key; a missing acquisition remains retryable.
WnResources *SpGetWindowResources() {
  if (!SpWindowResources) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
    WnResources *resources = static_cast<WnResources *>(object);
    SpWindowResources = resources;
    if (resources)
      resources->Release();
  }
  return SpWindowResources;
}

// Borrow the actual application's complete performance record through its original virtual getter.
// Preserve the null application result rather than inventing a renderer-specific budget object.
ApPerformanceInfo *SpGetPerformanceInfo() {
  ApGameInterface *application = SpGetApplication();
  return application ? application->GetSystemPerformanceInfo() : nullptr;
}

// Return the application's city-view window when the cached application exists. No reference is acquired.
SpCityWindowView *SpGetCityWindowView() {
  ApGameInterface *application = SpGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

bool SpHasCity() {
  return SpGetCity() != nullptr;
}

// Borrow the application's current city. An unavailable application produces the original null result.
CtCity *SpGetCity() {
  ApGameInterface *application = SpGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the active view from the city's view window. The lookup does not acquire an object reference.
SpViewInterface *SpGetCurrentView() {
  SpCityWindowView *window = SpGetCityWindowView();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the active isometric view's public sprite-manager identity. Preserve the original null-view path.
SpManagerView *SpGetSpriteManager() {
  SpViewInterface *view = SpGetCurrentView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteManager() : nullptr;
}

// Borrow the active isometric view's cell map. No module-global replacement map is introduced.
SpCellMap *SpGetCellMap() {
  SpViewInterface *view = SpGetCurrentView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteCellMap() : nullptr;
}

// Borrow the active view's animation manager. Missing views return null without changing any cached service.
SpTickManager *SpGetTickManager() {
  SpViewInterface *view = SpGetCurrentView();
  return view ? view->GetAnimTickManager() : nullptr;
}

// Construct the original nuclear simulator and return its unretained leading control identity.
// The separate sprite and occupant bases remain genuine parts of that complete receiver.
static ScUnknown *SpCreateNuclearExplosionSimulator() {
  return static_cast<SpNuclearView *>(new SpNuclear);
}

// Construct the actual water-debris sprite and publish its separate controller identity without a reference.
// Native C++ inheritance represents the original secondary receiver at200.
static ScUnknown *SpCreateWaterDebris() {
  return static_cast<SpWaterDebrisView *>(new SpWaterDebris);
}

// Return the original image-store singleton's primary cache identity without acquiring another reference.
// The director obtains the requested service identity through that object's genuine QueryInterface.
static ScUnknown *SpCreateImageStore() {
  return static_cast<SpImageCache *>(SpGetImageStore());
}

// Construct the effects coordinator and return its unretained primary identity.
// Shared effects dependencies are acquired by its actual initialization method.
static ScUnknown *SpCreateEffects() {
  return new SpEffects;
}

// Construct the water-splash simulator and return its leading controller identity.
// Its basic sprite and effects occupant remain genuine secondary bases.
static ScUnknown *SpCreateSplashSimulator() {
  return static_cast<SpWaterSplash *>(new SpSplash);
}

// Construct explosion debris and return the original leading simulator identity.
// No artwork or animation reference is acquired at this factory boundary.
static ScUnknown *SpCreateDebrisSimulator() {
  return static_cast<SpDebrisView *>(new SpDebris);
}

// Construct the explosion simulator and return its leading control identity.
// Initialization and all effects lifetime transitions stay in the real object's methods.
static ScUnknown *SpCreateExplosionSimulator() {
  return static_cast<SpExplosionView *>(new SpExplosion);
}

// Construct the trajectory object and return its leading trajectory interface without a caller reference.
// The subsequent director query establishes the requested identity and retention.
static ScUnknown *SpCreateTrajectoryObject() {
  return static_cast<SpTrajectoryView *>(new SpTrajectory);
}

// Construct the UFO simulator and return its leading public simulator identity.
// Its constructor registers the actual object in the module-wide UFO pool.
static ScUnknown *SpCreateUfoSimulator() {
  return static_cast<SpUfoSimulator *>(new SpUfo);
}

// Construct the weather simulator and return its leading weather identity.
// Its separate tick and message identities share the same complete receiver lifetime.
static ScUnknown *SpCreateWeatherSimulator() {
  return static_cast<SpWeatherView *>(new SpWeatherSim);
}

// Construct the whirlpool-debris simulator and return its leading controller identity.
// Sprite and occupant destruction remain part of the concrete producer's real lifetime.
static ScUnknown *SpCreateWhirlpoolDebris() {
  return static_cast<SpWhirlpoolDebris *>(new SpWhirlpoolPiece);
}

// Register the original forty classes in their installed constructor order without instantiating the products.
// Common DLL-director storage replaces only library bookkeeping; the factories preserve genuine interface identities.
SpDirector::SpDirector() {
  AddClass(kSpCityWindowViewClassId, SpCreateCityWindow, 0);
  AddClass(kSpImageStoreClassId, SpCreateImageStore, 0);
  AddClass(kSpInstanceClassId, SpCreateInstance, 0);
  AddClass(kSpScriptSpriteClassId, SpCreateScriptSprite, 0);
  AddClass(kSpCitySpritePreviewClassId, SpCreateCitySpriteWindow, 0);
  AddClass(kSpLayerSpriteClassId, SpCreateLayerSprite, 0);
  AddClass(kSpSlopedDownClassId, SpCreateSlopedDown, 0);
  AddClass(kSpSlopedUpClassId, SpCreateSlopedUp, 0);
  AddClass(kSpSlopedDownTwoClassId, SpCreateSlopedDown2, 0);
  AddClass(kSpSlopedUpTwoClassId, SpCreateSlopedUp2, 0);
  AddClass(kSpCsmSpriteClassId, SpCreateCsmSprite, 0);
  AddClass(kSpSlopedCsmDownClassId, SpCreateSlopedCsmDown, 0);
  AddClass(kSpSlopedCsmUpClassId, SpCreateSlopedCsmUp, 0);
  AddClass(kSpSlopedCsmDownTwoClassId, SpCreateSlopedCsmDown2, 0);
  AddClass(kSpSlopedCsmUpTwoClassId, SpCreateSlopedCsmUp2, 0);
  AddClass(kSpEffectsClassId, SpCreateEffects, 0);
  AddClass(kSpParticleEffectClassId, SpCreateParticleSimulator, 0);
  AddClass(kSpFireSimulatorClassId, SpCreateFireSimulator, 0);
  AddClass(kSpWaterSplashClassId, SpCreateSplashSimulator, 0);
  AddClass(kSpDebrisClassId, SpCreateDebrisSimulator, 0);
  AddClass(kSpExplosionViewClassId, SpCreateExplosionSimulator, 0);
  AddClass(kSpTrajectoryViewClassId, SpCreateTrajectoryObject, 0);
  AddClass(kSpTornadoSimulatorClassId, SpCreateTornadoSimulator, 0);
  AddClass(kSpUfoClassId, SpCreateUfoSimulator, 0);
  AddClass(kSpUfoGroupSimulatorClassId, SpCreateUfoGroup, 0);
  AddClass(kSpLocustClassId, SpCreateLocustSimulator, 0);
  AddClass(kSpLocustGroupSimulatorClassId, SpCreateLocustGroupSimulator, 0);
  AddClass(kSpEarthquakeViewClassId, SpCreateEarthquakeSimulator, 0);
  AddClass(kSpNuclearViewClassId, SpCreateNuclearExplosionSimulator, 0);
  AddClass(kSpWeatherSimClassId, SpCreateWeatherSimulator, 0);
  AddClass(kSpToxicCloudDisplayClassId, SpCreateToxicCloudDisplay, 0);
  AddClass(kSpAcidRainClassId, SpCreateAcidRainDisplay, 0);
  AddClass(kSpWhirlpoolSimulatorClassId, SpCreateWhirlpoolSimulator, 0);
  AddClass(kSpWaterDebrisClassId, SpCreateWaterDebris, 0);
  AddClass(kSpWhirlpoolDebrisClassId, SpCreateWhirlpoolDebris, 0);
  AddClass(kSpDebrisDisplayClassId, SpCreateSpaceDebrisDisplay, 0);
  AddClass(kSpImpactDisplayClassId, SpCreateSpaceDebrisImpactDisplay, 0);
  AddClass(kSpDefinitionFactoryClassId, SpCreateDefinitionFactory, 0);
  AddClass(kSpDefinitionClassId, SpCreateDefinition, 0);
  AddClass(kSpScriptDefinitionClassId, SpCreateScriptDefinition, 0);
}

u32 SpDirector::GetDirectorID() const {
  return kSpDirectorId;
}

// Ask the director's current COM root for its image service and install any published service pointer.
// The output pointer governs installation even if the class query reports failure; release its temporary reference last.
bool SpDirector::OnStart(ScCom *) {
  ScSystemService *service = nullptr;
  GZCOM()->GetClassObject(kSpImageStoreClassId, IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    SpGetFramework()->AddSystemService(service);
    service->Release();
  }
  return true;
}

alignas(SpDirector) static u8 SpDirectorStorage[sizeof(SpDirector)];
static bool SpDirectorInitialized;

static void SpDestroyDirector() {
  reinterpret_cast<SpDirector *>(SpDirectorStorage)->~SpDirector();
}

// Publish the lazy guard before constructing the real module director, then register its independent exit cleanup.
// Return its borrowed primary identity without incrementing the module reference count.
ScDirector *SpGetDirector() {
  if (!SpDirectorInitialized) {
    SpDirectorInitialized = true;
    new (SpDirectorStorage) SpDirector;
    atexit(SpDestroyDirector);
  }
  return reinterpret_cast<SpDirector *>(SpDirectorStorage);
}

// Export the original nonretaining module lookup through a unique native C symbol.
// SIMSPR.def publishes the original entry-point spelling expected by the game loader.
SC3_C_LINKAGE ScDirector *SpDllGetGZCOMDirector() {
  return SpGetDirector();
}
