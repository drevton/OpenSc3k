
#include "advisors/ad_ini.h"
#include "application/api.h"
#include "buildings/api.h"
#include "buildings/architect/manager.h"
#include "city/api.h"
#include "city/ct_init_text.h"
#include "city/ct_scheme_manager_impl.h"
#include "city/initialization.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "geometry/api.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/manager_object.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"

// Local scheme directories enumerate separate BAT overrides, flora and building resource types.
enum CtSchemeResourceType : u32 {
  kCtBatOverrideResourceType = 0x39ef105f,
  kCtFloraSchemeResourceType = 0x55a65b5,
  kCtBuildingSchemeResourceType = 0x4522864a,
};

// Serialized keys retain their record-format role independently of live class and interface identities.
enum CtReplacementArchiveKey : u32 {
  kCtReplacementSetRecordType = 0x99f47014,
  kCtReplacementSetRecordGroup = 0x79f4702d,
};

// Probe a directory using its installed wildcard search, closing a successful Windows enumeration immediately.
static bool SC3_CDECL CtSchemeDirectoryExists(const ScString *path) {
  return ScDirectory::DoesDirectoryExist(*path);
}

// Create missing directory components in order, undoing directories created by this call after a failure.
static bool SC3_CDECL CtCreateSchemeDirectory(const ScString *path) {
  return ScDirectory::CreateDirectoryA(*path);
}

// Search the separate key array for all three words of a resource key in the original input order.
static bool SC3_CDECL CtContainsSchemeKey(const RsKey &key, const RsKey *keys, u32 count) {
  for (u32 i = 0; i < count; ++i)
    if (keys[i].instance == key.instance && keys[i].group == key.group && keys[i].type == key.type)
      return true;
  return false;
}

static i32 SC3_CDECL CtEqualSchemeKeys(const RsKey &left, const RsKey &right) {
  return left.instance == right.instance && left.group == right.group && left.type == right.type;
}

static void CtCollectBATKey(const RsKey &key, void *context) {
  if (key.type == kRsBuildingOverrideResourceType)
    *static_cast<ScArray<RsKey> *>(context)->Append() = key;
}

static bool SC3_CDECL CtGetSchemeSegmentKeys(RsDatabaseSegment *segment, u32 iid, void **out) {
  if (!segment)
    return false;
  RsKeyListInterface *keys = nullptr;
  if (!CtGetInitDirector()->GZCOM()->GetClassObject(
          kRsKeyListClassId, IID_RsKeyListInterface, reinterpret_cast<void **>(&keys)) ||
      !keys)
    return false;
  segment->GetResourceKeyList(keys);
  bool result = keys->QueryInterface(iid, out);
  keys->Release();
  return result;
}

u8 SC3_CDECL CtOpenSchemeSegment(u32 identifier, const ScStringView *path, RsDatabaseSegment **out) {
  if (!CtGetInitDirector()->GZCOM()->GetClassObject(
          kRsIndexedFileClassId, IID_RsDatabaseSegment, reinterpret_cast<void **>(out)))
    return false;
  if ((*out)->Init(identifier, path, true))
    return true;
  (*out)->Release();
  *out = nullptr;
  return false;
}

static void CtInsertSchemeKey(const RsKey &key, void *context) {
  static_cast<RsKeyListInterface *>(context)->Insert(key);
}

static void CtMergeSchemeKeys(RsKeyListInterface **source, RsKeyListInterface **destination) {
  if (!*destination) {
    *destination = *source;
    *source = nullptr;
  } else if (*source) {
    (*source)->EnumKeys(CtInsertSchemeKey, *destination);
    (*source)->Release();
    *source = nullptr;
  }
}

// Register directory DAT files with the sprite cache, then open and retain each database segment in enumeration order.
// Merge its keys into the caller's list; the first refused open stops the scan without undoing earlier registrations.
void SC3_CDECL CtLoadSchemeSegments(
    const ScString *path, RsKeyListInterface **keys, u32 id, std::vector<CtSchemeSegment> *segments) {
  RsDirectoryListing listing(*path, false, false, false);
  std::list<ScString> names;
  RsManager *resources = CtGetInitResourceManager();
  SpImageCache *cache = CtGetInitImageCache();
  if (!resources || !cache)
    return;
  {
    ScString filter("*.DAT");
    listing.SetNewDirectoryEntryFilter(filter, true);
  }
  listing.ReadDirectoryFileEntriesIntoStringList(&names);
  for (const ScString &name : names) {
    CtSchemeSegment entry{};
    entry.path = *path;
    entry.path.Append(name.Data(), name.Strlen());
    cache->AddDBSegmentSpriteToSearchList(entry.path, 0);
    if (!CtOpenSchemeSegment(id, &entry.path, &entry.segment))
      break;
    entry.segment->OpenDBSegment(true, false);
    resources->RegisterDBSegment(entry.segment);
    RsKeyListInterface *found = nullptr;
    if (CtGetSchemeSegmentKeys(entry.segment, IID_RsKeyListInterface, reinterpret_cast<void **>(&found)))
      CtMergeSchemeKeys(&found, keys);
    // The original copies an unwritten, unused record identifier; native storage leaves that field zero instead.
    segments->push_back(entry);
  }
}

u8 SC3_STDCALL CtRemoveSchemeSpriteSegment(const ScString *path) {
  SpImageCache *cache = CtGetInitImageCache();
  return cache ? cache->RemoveDBSegmentSpriteToSearchList(*path) : 0;
}

// Unregister the segment records in order and merge their keys before retiring their retained segment references.
// Each sprite path is removed before resource unregistration and the final segment release.
void CtSchemeManagerImpl::UnloadSegments(std::vector<CtSchemeSegment> *segments, RsKeyListInterface **removed) {
  usize count = segments->size();
  for (usize i = 0; i < count; ++i) {
    CtSchemeSegment &entry = (*segments)[i];
    CtRemoveSchemeSpriteSegment(&entry.path);
    CtGetInitResourceManager()->UnregisterDBSegment(entry.segment);
    // The installed caller can read this local unset when key-list creation fails.
    RsKeyListInterface *keys = nullptr;
    CtGetSchemeSegmentKeys(entry.segment, IID_RsKeyListInterface, reinterpret_cast<void **>(&keys));
    CtMergeSchemeKeys(&keys, removed);
    entry.segment->Release();
  }
  segments->clear();
}

// Retire active building segments and clear the loaded flag after all their resources have been unregistered.
void CtSchemeManagerImpl::UnloadBuildingSegments(RsKeyListInterface **removed) {
  *removed = nullptr;
  if (!buildingLoaded)
    return;
  UnloadSegments(&buildingSegments, removed);
  buildingLoaded = false;
}

// Retire active flora segments and clear their loaded flag after unregistering their resources.
void CtSchemeManagerImpl::UnloadFloraSegments(RsKeyListInterface **removed) {
  *removed = nullptr;
  if (!floraLoaded)
    return;
  UnloadSegments(&floraSegments, removed);
  floraLoaded = false;
}

// Clear BAT keys, remove each segment's sprite path and database registration, then release its keys and segment.
// Segment records are discarded only after all original cleanup calls have completed.
void CtSchemeManagerImpl::UnloadBATSegment() {
  batKeys.Clear();
  usize count = batSegments.size();
  for (usize i = 0; i < count; ++i) {
    CtSchemeSegment &entry = batSegments[i];
    CtRemoveSchemeSpriteSegment(&entry.path);
    CtGetInitResourceManager()->UnregisterDBSegment(entry.segment);
    RsKeyListInterface *keys = nullptr;
    CtGetSchemeSegmentKeys(entry.segment, IID_RsKeyListInterface, reinterpret_cast<void **>(&keys));
    if (keys)
      keys->Release();
    entry.segment->Release();
  }
  batSegments.clear();
}

// Load the BARender directory and append BAT override keys from the returned retained resource-key list.
// Retain each discovered DAT segment through its real cache and resource registrations.
void CtSchemeManagerImpl::LoadBATSegment() {
  RsKeyListInterface *keys = nullptr;
  ScString path;
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("\\BARender\\");
  CtLoadSchemeSegments(&path, &keys, kCtBatOverrideResourceType, &batSegments);
  if (keys) {
    keys->EnumKeys(CtCollectBATKey, &batKeys);
    keys->Release();
  }
}

// Export the requested BAT changes, reload the segment, then clear replacements whose targets disappeared.
// Existing keys are filtered before the old segment is unloaded, preserving their original order in the export.
void CtSchemeManagerImpl::UpdateBATSegment(u32 fileCount, ScStringView **files, u32 removeCount, RsKey *removeKeys) {
  if (!fileCount && !removeCount)
    return;
  u32 oldCount = batKeys.size, keepCount = 0;
  RsKey *kept = oldCount ? new RsKey[oldCount] : nullptr;
  for (u32 i = 0; i < oldCount; ++i)
    if (!CtContainsSchemeKey(batKeys.data[i], removeKeys, removeCount))
      kept[keepCount++] = batKeys.data[i];
  ScRelease(&batContainer);
  UnloadBATSegment();
  ScString path;
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("\\BARender\\");
  if (!CtSchemeDirectoryExists(&path))
    CtCreateSchemeDirectory(&path);
  path.Append("BAT.dat");
  architect->ExportBuildings(&path, fileCount, &files, keepCount, kept);
  LoadBATSegment();
  ScArray<RsKey> stale;
  for (auto it = replacements.begin(); it != replacements.end(); ++it) {
    u32 index = 0;
    while (index < batKeys.size && !CtEqualSchemeKeys(batKeys.data[index], it->second))
      ++index;
    if (index == batKeys.size)
      *stale.Append() = it->first;
  }
  for (u32 i = 0; i < stale.size; ++i)
    ClearBATReplacement(stale.data[i]);
  delete[] kept;
}

// Load a newly selected flora-set directory and publish its selection after updating the stored loaded state.
// An already loaded matching selection returns before querying the application or posting a message.
bool CtSchemeManagerImpl::LoadFloraSet(u32 selection, RsKeyListInterface **keys) {
  *keys = nullptr;
  if (floraLoaded && selection == currentFlora)
    return true;
  ScString path;
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("Res\\Sprites\\FloraSets\\");
  if (selection) {
    ScString suffix;
    suffix.Sprintf("%08lX\\", static_cast<unsigned long>(selection));
    path.Append(suffix.Data(), suffix.Strlen());
    CtLoadSchemeSegments(&path, keys, kCtFloraSchemeResourceType, &floraSegments);
  }
  floraLoaded = true;
  currentFlora = selection;
  ScMessage message(2, selection, 0, 0);
  CtGetInitMessageServer()->MessagePost(message, false);
  return true;
}

// Load a newly selected building-set directory and publish its selection after updating the stored loaded state.
// A zero selection records the choice without opening a directory.
bool CtSchemeManagerImpl::LoadBuildingSet(u32 selection, RsKeyListInterface **keys) {
  *keys = nullptr;
  if (buildingLoaded && selection == currentBuilding)
    return true;
  ScString path;
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("Res\\Sprites\\BuildingSets\\");
  if (selection) {
    ScString suffix;
    suffix.Sprintf("%08lX\\", static_cast<unsigned long>(selection));
    path.Append(suffix.Data(), suffix.Strlen());
    CtLoadSchemeSegments(&path, keys, kCtBuildingSchemeResourceType, &buildingSegments);
  }
  buildingLoaded = true;
  currentBuilding = selection;
  ScMessage message(1, selection, 0, 0);
  CtGetInitMessageServer()->MessagePost(message, false);
  return true;
}

// Advance the displayed progress position by the supplied amount, clamped to the stored total.
void CtSchemeManagerImpl::UpdateProgress(u32 increment) {
  if (!progressVisible)
    return;
  progressCurrent += increment;
  if (progressCurrent > progressTotal)
    progressCurrent = progressTotal;
  CtGetInitApplication()->UpdateProgressBar(progressCurrent);
}

// Refresh occupant attributes from the supplied keys and post one change message after successful setup.
// Progress mode submits at most 200 keys at a time and updates the displayed position after each batch.
bool CtSchemeManagerImpl::RebuildOccupantAttribs(RsKeyListInterface *keys) {
  if (!keys)
    return false;
  OcAttribCache *cache = CtGetInitApplication()->GetAppOccupantAttribCache();
  if (!cache)
    return false;
  if (!progressVisible) {
    bool result = cache->AddAttribs(*keys);
    cache->PostOccAttribChangedMessage();
    return result;
  }
  u32 count = keys->Size();
  if (!count)
    return true;
  f32 position = static_cast<f32>(progressCurrent), increment = 60.0f / static_cast<f32>(count);
  RsKeyListInterface *batch = nullptr;
  if (!CtGetInitDirector()->GZCOM()->GetClassObject(
          kRsKeyListClassId, IID_RsKeyListInterface, reinterpret_cast<void **>(&batch)))
    return false;
  u32 index = 0;
  do {
    u32 end = index + 200;
    if (end > count)
      end = count;
    while (index < end) {
      RsKey key = *keys->GetKey(index);
      batch->Insert(key);
      ++index;
      position += increment;
    }
    cache->AddAttribs(*batch);
    batch->EraseAll();
    u32 current = static_cast<u32>(static_cast<i64>(position));
    if (current > progressCurrent)
      UpdateProgress(current - progressCurrent);
  } while (index < count);
  batch->Release();
  cache->PostOccAttribChangedMessage();
  return true;
}

// Remove cached building-description resources, then ask the current zone layer to reload its descriptions.
void CtSchemeManagerImpl::RefreshBuildingAssets() {
  RsKeyListInterface *keys = nullptr;
  if (resourceManager->GetResourceListForType(&keys, kRsBuildingGroupResourceType) && keys) {
    u32 count = keys->Size();
    for (u32 i = 0; i < count; ++i) {
      RsKey key = *keys->GetKey(i);
      resourceManager->RemoveResource(key);
    }
    keys->Release();
  }
  CtCity *city = CtGetInitCity();
  if (city) {
    CtZoneLayer *zones = city->ZoneLayer();
    if (zones)
      zones->ReadZoneDeveloperDescriptions(true);
  }
}

// Change landscape, flora and building selections while holding the current city's lock.
// Merge changed segment keys before refreshing attributes and reapply building replacements in stored order.
bool CtSchemeManagerImpl::SetCityScheme(u32 landscape, u32 floraSet, u32 buildingSet, bool showProgress) {
  RsKeyListInterface *added = nullptr, *removed = nullptr;
  bool landscapeChanged = landscape != currentLandscape;
  bool floraChanged = floraSet != currentFlora;
  bool buildingChanged = buildingSet != currentBuilding;
  ApGameInterface *application = CtGetInitApplication();
  CtCity *city = CtGetInitCity();
  if (city)
    city->Lock();
  progressVisible = showProgress;
  if (progressVisible) {
    ScString title = CtInitText(kRsSupplementalGameChangeInProgressText, kRsSupplementalGameTextGroup);
    progressTotal = 2;
    progressCurrent = 2;
    if (landscapeChanged)
      progressTotal = 17;
    if (floraChanged)
      progressTotal += 30;
    if (buildingChanged)
      progressTotal += 20;
    if (floraChanged || buildingChanged)
      progressTotal += 60;
    if (!progressTotal)
      progressTotal = 3;
    application->ShowProgressBar(progressTotal, &title);
  }
  if (landscape != currentLandscape) {
    ScMessage before(kCtMsg_BeforeLandscapeChange, landscape, 0, 0);
    CtGetInitMessageServer()->MessageSend(before);
    currentLandscape = landscape;
    ScMessage after(0, landscape, 0, 0);
    CtGetInitMessageServer()->MessagePost(after, false);
    if (progressVisible)
      UpdateProgress(15);
  }
  if (floraLoaded && floraSet != currentFlora && currentFlora)
    UnloadFloraSegments(&removed);
  bool result = LoadFloraSet(floraSet, &added);
  if (result) {
    CtMergeSchemeKeys(&added, &removed);
    if (progressVisible && floraChanged)
      UpdateProgress(30);
    if (buildingLoaded && buildingSet != currentBuilding && currentBuilding) {
      UnloadBuildingSegments(&added);
      CtMergeSchemeKeys(&added, &removed);
    }
    result = LoadBuildingSet(buildingSet, &added);
    if (result) {
      if (progressVisible && buildingChanged)
        UpdateProgress(20);
      CtMergeSchemeKeys(&added, &removed);
      if (removed) {
        result = RebuildOccupantAttribs(removed);
        removed->Release();
      }
      if (buildingChanged) {
        for (auto it = replacements.begin(); it != replacements.end(); ++it)
          MakeBATReplacement(it->first, it->second);
        RefreshBuildingAssets();
      }
      if ((floraChanged || buildingChanged) && progressVisible)
        UpdateProgress(60);
      if (progressVisible) {
        application->UpdateProgressBar(progressTotal);
        application->HideProgressBar();
      }
    }
  }
  if (city)
    city->Unlock();
  return result;
}

// Publish the scheme manager for its own interface identity and retain it through the virtual AddRef method.
bool CtSchemeManagerImpl::QueryInterface(u32 iid, void **out) {
  if (iid != IID_CtSchemeManager || !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<CtSchemeManager *>(this);
  AddRef();
  return true;
}

// Initialize the scheme state and retain the published building-architect service after its temporary acquisition.
// A service published alongside a false framework result is retained in the same way as a successful publication.
CtSchemeManagerImpl::CtSchemeManagerImpl() {
  BdManagerView *value = nullptr;
  CtGetInitFramework()->GetSystemService(kBdManagerServiceId, IID_BdManagerView, reinterpret_cast<void **>(&value));
  architect = value;
  if (architect)
    architect->AddRef();
  if (value)
    value->Release();
}

CtSchemeManagerImpl::~CtSchemeManagerImpl() {
  CtSchemeManagerImpl::Shutdown();
  ScRelease(&architect);
}

u32 CtSchemeManagerImpl::AddRef() {
  return ++references;
}

u32 CtSchemeManagerImpl::Release() {
  return ScReleaseImpl(this, &references);
}

// Release the BAT container, unload active building segments, then retire the plugin and resource managers.
// Segment unloading remains a separate original dependency and any returned key list is released before shutdown.
bool CtSchemeManagerImpl::Shutdown() {
  RsKeyListInterface *removed = nullptr;
  ScRelease(&batContainer);
  if (buildingLoaded)
    UnloadBuildingSegments(&removed);
  if (removed)
    removed->Release();
  if (pluginManager) {
    pluginManager->Shutdown();
    pluginManager->Release();
    pluginManager = nullptr;
  }
  ScRelease(&resourceManager);
  return true;
}

void CtSchemeManagerImpl::SetDefaultCityScheme() {
  SetCityScheme(defaultLandscape, defaultFlora, defaultBuilding, false);
}

// Read the replacement record and apply each successfully read pair in its stored order.
// An early clear is observable even when opening or reading the record subsequently fails.
bool CtSchemeManagerImpl::LoadBATReplacementSet(RsDatabaseSegment *segment, bool clear) {
  RsKey recordKey(kCtReplacementSetRecordType, kCtReplacementSetRecordGroup, 0), header, original, replacement;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (clear)
    ClearReplacementSet();
  if (!segment->OpenRecord(recordKey, &record))
    return false;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    u32 count = 0;
    result = stream->ReadKey(&header);
    if (result)
      result = stream->ReadU32(&count);
    for (u32 i = 0; i < count && result; ++i) {
      result = stream->ReadKey(&original);
      if (result) {
        result = stream->ReadKey(&replacement);
        if (result)
          MakeBATReplacement(original, replacement);
      }
    }
    stream->Release();
  }
  segment->CloseRecord(&record);
  return result;
}

// Write the replacement count and ordered key pairs to the original database record.
// Close the record after either a successful write or the first failed stream operation.
bool CtSchemeManagerImpl::SaveBATReplacementSet(RsDatabaseSegment *segment) {
  RsKey recordKey(kCtReplacementSetRecordType, kCtReplacementSetRecordGroup, 0), header;
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (!segment->OpenRecordForWrite(recordKey, &record))
    return false;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    result = stream->WriteKey(&header);
    if (result)
      result = stream->WriteU32(static_cast<u32>(replacements.size()));
    for (auto it = replacements.begin(); it != replacements.end() && result; ++it) {
      result = stream->WriteKey(&it->first);
      if (result)
        result = stream->WriteKey(&it->second);
    }
    stream->Release();
  }
  segment->CloseRecord(&record);
  return result;
}

// Open a file-backed database segment and request a replacement-set load without clearing the current set.
bool CtSchemeManagerImpl::ImportReplacementSet(ScStringView *path) {
  RsDatabaseSegment *segment = nullptr;
  if (!CtGetInitDirector()->GZCOM()->GetClassObject(
          kRsIndexedFileClassId, IID_RsDatabaseSegment, reinterpret_cast<void **>(&segment)))
    return false;
  if (!segment->Init(0, path, true)) {
    segment->Release();
    return false;
  }
  segment->OpenDBSegment(true, false);
  LoadBATReplacementSet(segment, false);
  segment->Release();
  return true;
}

// Convert a landscape section entry into its ten-word catalog record and append it in enumeration order.
void SC3_CDECL CtReadLandscapeScheme(const ScString &key, const ScString &value, void *owner) {
  CtSchemeManagerImpl *manager = static_cast<CtSchemeManagerImpl *>(owner);
  CtLandscapeScheme entry;
  ScString token, remaining(value);
  entry.id = ScConvertToUint32(key.ToChar());
  token = remaining.SplitToken(",");
  entry.iconGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.iconInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.paletteGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.paletteInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.navPalette = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.dataPalette = ScConvertToUint32(token.begin);
  entry.sound = ScConvertToUint32(remaining.begin);
  *manager->landscapes.Append() = entry;
}

// Convert a flora section entry into its eight-word record and append it without discarding duplicate IDs.
void SC3_CDECL CtReadFloraScheme(const ScString &key, const ScString &value, void *owner) {
  CtSchemeManagerImpl *manager = static_cast<CtSchemeManagerImpl *>(owner);
  CtContentScheme entry;
  ScString token, remaining(value);
  entry.id = ScConvertToUint32(key.ToChar());
  token = remaining.SplitToken(",");
  entry.iconGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.iconInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageGroup2 = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageInstance2 = ScConvertToUint32(token.begin);
  entry.sound = ScConvertToUint32(remaining.begin);
  *manager->flora.Append() = entry;
}

// Convert a building-set section entry into its eight-word record and append it in enumeration order.
void SC3_CDECL CtReadBuildingScheme(const ScString &key, const ScString &value, void *owner) {
  CtSchemeManagerImpl *manager = static_cast<CtSchemeManagerImpl *>(owner);
  CtContentScheme entry;
  ScString token, remaining(value);
  entry.id = ScConvertToUint32(key.ToChar());
  token = remaining.SplitToken(",");
  entry.iconGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.iconInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageGroup = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageInstance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageGroup2 = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.imageInstance2 = ScConvertToUint32(token.begin);
  entry.sound = ScConvertToUint32(remaining.begin);
  *manager->buildings.Append() = entry;
}

// Parse the three resource-key words from the entry name and retain the first occurrence of each key.
// Split an owned key copy so enumeration cannot observe mutations of its borrowed name.
void SC3_CDECL CtReadReplaceableScheme(const ScString &key, const ScString &, void *owner) {
  CtSchemeManagerImpl *manager = static_cast<CtSchemeManagerImpl *>(owner);
  ScString token, remaining(key);
  RsKey entry;
  token = remaining.SplitToken(",");
  entry.type = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  entry.group = ScConvertToUint32(token.begin);
  entry.instance = ScConvertToUint32(remaining.begin);
  for (u32 i = 0; i < manager->replaceable.size; ++i) {
    const RsKey &existing = manager->replaceable.data[i];
    if (existing.type == entry.type && existing.group == entry.group && existing.instance == entry.instance)
      return;
  }
  *manager->replaceable.Append() = entry;
}

void SC3_CDECL CtReadRewardScheme(const ScString &key, const ScString &, void *owner) {
  CtSchemeManagerImpl *manager = static_cast<CtSchemeManagerImpl *>(owner);
  *manager->rewards.Append() = ScConvertToUint32(key.ToChar());
}

// Restore SIMINIT's configuration-loading effects using the existing shared packed-INI implementation.
void CtSchemeManagerImpl::LoadConfiguration() {
  ScString path;
  AdIni ini;
  ScString unused;
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3CityScheme.ini");
  ini.SetPath(path);
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  {
    ScString section("LandScapes");
    ini.EnumEntries(section, CtReadLandscapeScheme, this);
  }
  {
    ScString section("FloraSets");
    ini.EnumEntries(section, CtReadFloraScheme, this);
  }
  {
    ScString section("BuildingSets");
    ini.EnumEntries(section, CtReadBuildingScheme, this);
  }
  {
    ScString section("ReplaceableBuildings");
    ini.EnumEntries(section, CtReadReplaceableScheme, this);
  }
}

// Restore SIMINIT's reward-configuration effects through the shared loose/SYS.PAK reader.
void CtSchemeManagerImpl::LoadRewardConfiguration() {
  ScString path;
  AdIni ini;
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\Advisor-CityPlanner.INI");
  ini.SetPath(path);
  CtGetInitApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  ScString section("REWARD_BUILDINGS");
  ini.EnumEntries(section, CtReadRewardScheme, this);
}

// Acquire the resource manager before loading configuration, then initialize a successfully acquired plugin manager.
// Reset the three defaults and invoke the real BAT-segment loader regardless of the plugin acquisition result.
bool CtSchemeManagerImpl::Init() {
  resourceManager = CtGetInitResourceManager();
  if (!resourceManager)
    return false;
  resourceManager->AddRef();
  LoadConfiguration();
  LoadRewardConfiguration();
  if (CtGetInitDirector()->GZCOM()->GetClassObject(
          kBdPluginManagerClassId, IID_BdPluginManager, reinterpret_cast<void **>(&pluginManager))) {
    ScString path(CtGetInitFileSystem()->PlugInDirectory());
    path.Append("PlugIn\\");
    pluginManager->Init(path);
  }
  defaultLandscape = 0;
  defaultFlora = 0;
  defaultBuilding = 0;
  LoadBATSegment();
  return true;
}

// Test each configured reward ancestor against the requested agent type using the current borrowed agent tree.
bool CtSchemeManagerImpl::IsRewardBuilding(u32 id) {
  CtAgentTree *tree = CtGetInitAgentTree();
  if (!tree)
    return false;
  u32 count = rewards.size;
  for (u32 i = 0; i < count; ++i)
    if (tree->IsAncestor(rewards.data[i], id))
      return true;
  return false;
}

u32 CtSchemeManagerImpl::GetNumBATBuildings() {
  return batKeys.size;
}

// Copy BAT building keys in their original stored order, limited by the caller's capacity.
void CtSchemeManagerImpl::GetBATBuildingKeys(RsKey *keys, u32 capacity, u32 *count) {
  *count = batKeys.size < capacity ? batKeys.size : capacity;
  for (u32 i = 0; i < *count; ++i) {
    keys[i].type = batKeys.data[i].type;
    keys[i].group = batKeys.data[i].group;
    keys[i].instance = batKeys.data[i].instance;
  }
}

// Compare resource keys in the replacement map's group, instance and type order.
bool CtSchemeKeyOrder::operator()(const RsKey &left, const RsKey &right) const {
  if (left.group != right.group)
    return left.group < right.group;
  if (left.instance != right.instance)
    return left.instance < right.instance;
  return left.type < right.type;
}

// Borrow the cached BAT override container, creating and populating it from registered BAT keys on first access.
iptr CtSchemeManagerImpl::GetBATBuildings() {
  if (!batContainer) {
    RsManager *resources = CtGetInitResourceManager();
    if (resources) {
      if (batContainer) {
        batContainer->Clear();
      } else if (!CtGetInitDirector()->GZCOM()->GetClassObject(
                     kOcOverrideContainerClassId, IID_OcOverrideContainer, reinterpret_cast<void **>(&batContainer))) {
        return reinterpret_cast<iptr>(batContainer);
      }
      RsKey *keys = batKeys.data;
      u32 count = batKeys.size;
      for (u32 i = 0; i < count; ++i) {
        OcOverride *value;
        if (resources->GetResource(keys[i], IID_OcOverride, reinterpret_cast<void **>(&value))) {
          batContainer->PutObject(value);
          value->Release();
        }
      }
    }
  }
  return reinterpret_cast<iptr>(batContainer);
}

// Clear the destination and copy each available BAT override whose byte lot size equals the requested DWORD.
void CtSchemeManagerImpl::GetBATBuildings(OcOverrideContainer *out, u32 lotSize) {
  OcOverrideContainer *source = reinterpret_cast<OcOverrideContainer *>(GetBATBuildings());
  out->Clear();
  if (!source)
    return;
  u32 count = source->Size();
  for (u32 i = 0; i < count; ++i) {
    OcOverride *item;
    if (source->GetObject(i, &item) && item->GetLotSize() == lotSize)
      out->PutObject(item);
  }
}

// Resolve the replacement key to an override, dispatch the pointer overload, then release a successful acquisition.
bool CtSchemeManagerImpl::MakeBATReplacement(const RsKey &original, const RsKey &replacement) {
  OcOverride *item = nullptr;
  if (!resourceManager->GetResource(replacement, IID_OcOverride, reinterpret_cast<void **>(&item)))
    item = nullptr;
  bool result = MakeBATReplacement(original, item);
  if (item)
    item->Release();
  return result;
}

// Apply the requested override to the live attribute cache before updating or removing its stored replacement key.
bool CtSchemeManagerImpl::MakeBATReplacement(const RsKey &original, OcOverride *replacement) {
  OcAttribCache *cache = CtGetInitAttribCache();
  if (!cache)
    return false;
  cache->ModifyOccupantAttrib(original, replacement);
  auto found = replacements.find(original);
  if (replacement) {
    RsKey key = *replacement->Key();
    if (found == replacements.end())
      replacements[original] = key;
    else
      found->second = key;
  } else if (found != replacements.end()) {
    replacements.erase(found);
  }
  return true;
}

// Reset the live attribute when a cache exists, then erase and report the matching stored replacement if present.
bool CtSchemeManagerImpl::ClearBATReplacement(const RsKey &original) {
  OcAttribCache *cache = CtGetInitAttribCache();
  if (cache)
    cache->ModifyOccupantAttrib(original, nullptr);
  auto found = replacements.find(original);
  if (found == replacements.end())
    return false;
  replacements.erase(found);
  return true;
}

// Reset stored replacement keys in map order, consulting the current attribute cache for each key, then clear the map.
void CtSchemeManagerImpl::ClearReplacementSet() {
  for (auto it = replacements.begin(); it != replacements.end(); ++it) {
    OcAttribCache *cache = CtGetInitAttribCache();
    if (cache)
      cache->ModifyOccupantAttrib(it->first, nullptr);
  }
  replacements.clear();
}

// Ask the attribute for its borrowed resource key, then test the configured replaceable-building set.
bool CtSchemeManagerImpl::IsReplaceableBuilding(const GeAttrib *attribute) {
  const RsKey *key = attribute->Key();
  for (u32 i = 0; i < replaceable.size; ++i) {
    const RsKey &entry = replaceable.data[i];
    if (entry.type == key->type && entry.group == key->group && entry.instance == key->instance)
      return true;
  }
  return false;
}

// Find the first landscape entry with the requested identifier, preserving configuration order.
CtLandscapeScheme *CtSchemeManagerImpl::FindLandscape(u32 id) {
  for (u32 i = 0; i < landscapes.size; ++i)
    if (landscapes.data[i].id == id)
      return &landscapes.data[i];
  return nullptr;
}

// Find the first flora entry with the requested identifier, or return null when none exists.
CtContentScheme *CtSchemeManagerImpl::FindFlora(u32 id) {
  for (u32 i = 0; i < flora.size; ++i)
    if (flora.data[i].id == id)
      return &flora.data[i];
  return nullptr;
}

// Find the first building-set entry with the requested identifier, or return null when absent.
CtContentScheme *CtSchemeManagerImpl::FindBuilding(u32 id) {
  for (u32 i = 0; i < buildings.size; ++i)
    if (buildings.data[i].id == id)
      return &buildings.data[i];
  return nullptr;
}

// Borrow the current plugin-building manager without changing its reference count.
BdPluginManager *CtSchemeManagerImpl::GetPluginBuildingMgr() {
  return pluginManager;
}

bool CtSchemeManagerImpl::GetCurrentLandScape(u32 *out) const {
  *out = currentLandscape;
  return true;
}

u32 CtSchemeManagerImpl::GetDefaultLandScape() const {
  return defaultLandscape;
}

u32 CtSchemeManagerImpl::GetNumLandScapes() const {
  return landscapes.size;
}

// Copy landscape identifiers in configuration order, limited by the caller's capacity.
void CtSchemeManagerImpl::GetLandScapeIds(u32 *ids, u32 capacity, u32 *count) {
  *count = landscapes.size < capacity ? landscapes.size : capacity;
  for (u32 i = 0; i < *count; ++i)
    ids[i] = landscapes.data[i].id;
}

bool CtSchemeManagerImpl::GetLandScapeIconKey(u32 id, RsKey *out) {
  CtLandscapeScheme *entry = FindLandscape(id);
  if (!entry)
    return false;
  out->type = kRsImageResourceType;
  out->group = entry->iconGroup;
  out->instance = entry->iconInstance;
  return true;
}

bool CtSchemeManagerImpl::GetLandScapePalette(u32 id, u32 *group, u32 *instance) {
  CtLandscapeScheme *entry = FindLandscape(id);
  if (!entry)
    return false;
  *group = entry->paletteGroup;
  *instance = entry->paletteInstance;
  return true;
}

bool CtSchemeManagerImpl::GetUiImageForLandScape(u32 id, u32 *group, u32 *instance) {
  CtLandscapeScheme *entry = FindLandscape(id);
  if (!entry)
    return false;
  *group = entry->imageGroup;
  *instance = entry->imageInstance;
  return true;
}

// Look up the current landscape's sound identifier, leaving the output untouched on a miss.
bool CtSchemeManagerImpl::GetSoundIdForCurrentLandScape(u32 *out) {
  CtLandscapeScheme *entry = FindLandscape(currentLandscape);
  if (!entry)
    return false;
  *out = entry->sound;
  return true;
}

void CtSchemeManagerImpl::GetNavViewTerrainPaletteForCurrentLandScape(u32 *out) {
  CtLandscapeScheme *entry = FindLandscape(currentLandscape);
  *out = entry ? entry->navPalette : 3;
}

void CtSchemeManagerImpl::GetDataMapsTerrainPaletteForCurrentLandScape(u32 *out) {
  CtLandscapeScheme *entry = FindLandscape(currentLandscape);
  *out = entry ? entry->dataPalette : 4;
}

bool CtSchemeManagerImpl::GetCurrentFloraSet(u32 *out) const {
  if (!floraLoaded)
    return false;
  *out = currentFlora;
  return true;
}

u32 CtSchemeManagerImpl::GetDefaultFloraSet() const {
  return defaultFlora;
}

u32 CtSchemeManagerImpl::GetNumFloraSets() const {
  return flora.size;
}

// Copy flora identifiers in configuration order, limited by the caller's capacity.
void CtSchemeManagerImpl::GetFloraSetIds(u32 *ids, u32 capacity, u32 *count) {
  *count = flora.size < capacity ? flora.size : capacity;
  for (u32 i = 0; i < *count; ++i)
    ids[i] = flora.data[i].id;
}

bool CtSchemeManagerImpl::GetFloraSetIconKey(u32 id, RsKey *out) {
  CtContentScheme *entry = FindFlora(id);
  if (!entry)
    return false;
  out->type = kRsImageResourceType;
  out->group = entry->iconGroup;
  out->instance = entry->iconInstance;
  return true;
}

// Return the four configured flora UI image words when the requested entry exists.
bool CtSchemeManagerImpl::GetUiImageForFloraSet(u32 id, u32 *group, u32 *instance, u32 *group2, u32 *instance2) {
  CtContentScheme *entry = FindFlora(id);
  if (!entry)
    return false;
  *group = entry->imageGroup;
  *instance = entry->imageInstance;
  *group2 = entry->imageGroup2;
  *instance2 = entry->imageInstance2;
  return true;
}

// Look up the current flora set's sound identifier, leaving the output untouched on a miss.
bool CtSchemeManagerImpl::GetSoundIdForCurrentFloraSet(u32 *out) {
  CtContentScheme *entry = FindFlora(currentFlora);
  if (!entry)
    return false;
  *out = entry->sound;
  return true;
}

bool CtSchemeManagerImpl::GetCurrentBuildingSet(u32 *out) const {
  if (!buildingLoaded)
    return false;
  *out = currentBuilding;
  return true;
}

u32 CtSchemeManagerImpl::GetDefaultBuildingSet() const {
  return defaultBuilding;
}

u32 CtSchemeManagerImpl::GetNumBuildingSets() const {
  return buildings.size;
}

// Copy building-set identifiers in configuration order, limited by the caller's capacity.
void CtSchemeManagerImpl::GetBuildingSetIds(u32 *ids, u32 capacity, u32 *count) {
  *count = buildings.size < capacity ? buildings.size : capacity;
  for (u32 i = 0; i < *count; ++i)
    ids[i] = buildings.data[i].id;
}

bool CtSchemeManagerImpl::GetBuildingSetIconKey(u32 id, RsKey *out) {
  CtContentScheme *entry = FindBuilding(id);
  if (!entry)
    return false;
  out->type = kRsImageResourceType;
  out->group = entry->iconGroup;
  out->instance = entry->iconInstance;
  return true;
}

bool CtSchemeManagerImpl::GetUiImageForBuildingSet(u32 id, u32 *group, u32 *instance, u32 *group2, u32 *instance2) {
  CtContentScheme *entry = FindBuilding(id);
  if (!entry)
    return false;
  *group = entry->imageGroup;
  *instance = entry->imageInstance;
  *group2 = entry->imageGroup2;
  *instance2 = entry->imageInstance2;
  return true;
}

// Look up the current building set's sound identifier, leaving the output untouched on a miss.
bool CtSchemeManagerImpl::GetSoundIdForCurrentBuildingSet(u32 *out) {
  CtContentScheme *entry = FindBuilding(currentBuilding);
  if (!entry)
    return false;
  *out = entry->sound;
  return true;
}
