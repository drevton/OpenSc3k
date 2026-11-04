#include "disasters/ds_locust.h"

#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_locust_instance.h"
#include "disasters/ds_module.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "occupants/animation_manager.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "terrain/api.h"

enum DsLocustArchiveKey : u32 {
  kDsLocustHistoryRecordGroup = 0x45326359,
  kDsLocustInstancesRecordGroup = 0x8532635c,
};

// Save elapsed idle updates and capped history in version two, then independently save all live locust records.
// Keep the uncapped active count, filter each iterator result by its real type, and ignore individual block refusals.
bool DsLocustManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsLocustHistoryRecordGroup, 0}, 2, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      bool result = stream->WriteI32(elapsed_updates) != 0;
      u32 count = history.size >= 50000 ? 50000 : history.size;
      if (result && stream->WriteU32(count) && count) {
        for (u32 index = 0; index < count; ++index) {
          DsLocustRecord entry = history.data[index];
          stream->WriteBlock(&entry, 24);
        }
      }
    }
  }
  if (active_count) {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsLocustInstancesRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      if (stream->WriteU32(active_count)) {
        DsListIterator *iterator = layer->CreateDisasterListIterator();
        while (DsInstance *instance = iterator->Next()) {
          if (instance->TypeId() == kDsLocustTypeId) {
            DsLocustRecord entry;
            static_cast<DsLocustInstance *>(instance)->GetRecord(&entry);
            stream->WriteBlock(&entry, 24);
          }
        }
        layer->DeleteDisasterListIterator(iterator);
      }
    }
  }
  return true;
}

// Replace history from its archive and append restored live records to the separate pending collection.
// Version-two elapsed input precedes the uncapped history count; each refused read stops only its own archive.
bool DsLocustManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsLocustHistoryRecordGroup, 0});
    history.Clear();
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      // The unwind prologue leaves a nonzero byte in this flag slot; explicit initialization preserves old versions.
      bool result = true;
      if (archive.version > 1)
        result = stream->ReadI32(&elapsed_updates) != 0;
      if (result) {
        result = stream->ReadU32(&count) != 0;
        for (u32 index = 0; index < count && result; ++index) {
          DsLocustRecord entry;
          result = stream->ReadBlock(&entry, 24) != 0;
          if (result)
            *history.Append() = entry;
        }
      }
    }
  }
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsLocustInstancesRecordGroup, 0});
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      for (u32 index = 0; index < count && result; ++index) {
        DsLocustRecord entry;
        result = stream->ReadBlock(&entry, 24) != 0;
        if (result)
          *pending.Append() = entry;
      }
    }
  }
  return true;
}

// Retain the cell occupant and query both width and type before testing the one-cell crop ancestry.
// Require the real specialized building flag and resource instance, releasing the queried attribute before the occupant.
bool DsLocustManager::IsCropAtCell(u32 x, u32 z) {
  bool result = false;
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(x, z, &occupant)) {
    u8 width = occupant->SizeInCellsX();
    u32 type = occupant->TypeId();
    if (width == 1 && agent_types->IsAncestor(kCtAgentAgriculturalIndustry, type)) {
      GeAttrib *attributes = occupant->GetAttribData();
      void *value;
      if (attributes && attributes->QueryInterface(IID_GeBuildingType, &value)) {
        GeBuildingType *building = static_cast<GeBuildingType *>(value);
        if (building->Flags() & 0x100u)
          result = building->Key()->instance == 5062;
        building->Release();
      }
    }
    occupant->Release();
  }
  return result;
}

// An already-bound manager only shuts down; otherwise retain city services in their original publication order.
// Register the scoped cheat name before notifications, reset activity and flags, then mark initialized last.
bool DsLocustManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized) {
    Shutdown();
  } else {
    city = ScAddRefNotNull(retained_city);
    terrain = ScAddRefNotNull(city->DirtBag());
    // The SDK leaves this getter pointer-sized; this method consumes only the proven Unknown lifetime prefix.
    demolition = ScAddRefNotNull(static_cast<ScUnknown *>(city->DemolitionLayer()));
    simulation = ScAddRefNotNull(city->StrtSimLayer());
    agent_types = ScAddRefNotNull(DsGetAgentTree());
    box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
    occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
    layer = ScAddRefNotNull(retained_layer);
    cheats = DsGetApplication()->GetCheatCodeManager();
    {
      ScString name("Locust Swarm");
      cheats->RegisterCheatCode(kDsLocustSwarmCheatId, name);
    }
    cheats->AddNotification(static_cast<ScMessageTarget *>(this), 0);
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    active_count = 0;
    flags_112 = 0;
    initialized = true;
  }
  return true;
}

// A false input stops at zero; the original signed positive test also leaves negative counter bit patterns unchanged.
void DsLocustManager::ChangeNotice235(u8 enabled) {
  if (enabled) {
    if (++notice_235_count == 1)
      box->Event(kAuEventPlayEffect, kAuEffectDisasterLocustsEat, 0, 0, 0);
  } else if (i32(notice_235_count) >= 1) {
    if (--notice_235_count == 0)
      box->Event(kAuEventEffectSlot52, kAuEffectDisasterLocustsEat, 0, 0, 0);
  }
}

// Keep the counter update even when that notice is closed, as in the installed method.
void DsLocustManager::ChangeNotice235Mode(u8 enabled) {
  if (enabled) {
    if (++notice_235_mode_count == 1 && i32(notice_235_count) > 0)
      box->Event(kAuEventEffectSlot56, kAuEffectDisasterLocustsEat, 0, 1, 0);
  } else if (i32(notice_235_mode_count) > 0) {
    if (--notice_235_mode_count == 0 && i32(notice_235_count) > 0)
      box->Event(kAuEventEffectSlot56, kAuEffectDisasterLocustsEat, 0, 0, 0);
  }
}

void DsLocustManager::ChangeNotice233Mode(u8 enabled) {
  if (enabled) {
    if (++notice_233_mode_count == 1)
      box->Event(kAuEventEffectSlot56, kAuEffectDisasterLocustSwarm, 0, 1, 0);
  } else if (i32(notice_233_mode_count) >= 1) {
    if (--notice_233_mode_count == 0)
      box->Event(kAuEventEffectSlot56, kAuEffectDisasterLocustSwarm, 0, 0, 0);
  }
}

void DsLocustManager::EndNotice233() {
  if (i32(notice_233_count) >= 1) {
    if (--notice_233_count == 0)
      box->Event(kAuEventEffectSlot52, kAuEffectDisasterLocustSwarm, 0, 0, 0);
  }
}

void DsLocustManager::BeginNotice233() {
  if (++notice_233_count == 1)
    box->Event(kAuEventPlayEffect, kAuEffectDisasterLocustSwarm, 0, 0, 0);
}

u32 DsLocustSwarmSpeed = 50, DsLocustFirstNoticeTime = 0, DsLocustSecondNoticeTime = 60;
u32 DsLocustThirdNoticeTime = 120, DsLocustAttackTime = 180;
u32 DsLocustMinimumSwarms = 1, DsLocustMaximumSwarms = 20, DsLocustSwarmCropRatio = 40;
u32 DsLocustCheatSwarmMaximum = 50, DsLocustFirstVisitationYear = 1920, DsLocustMaximumCrops = 10;

// The terrain, street, cheat and agent-tree pointers remain unwritten until live city binding, as in the original.
DsLocustManager::DsLocustManager()
    : initialized(false), automatic_start(0), elapsed_updates(0), layer(nullptr), demolition(nullptr), city(nullptr),
      occupants(nullptr), random(kScRandomSeedFromClock), active_count(0), notice_233_count(0),
      notice_233_mode_count(0), notice_235_count(0), notice_235_mode_count(0), box(nullptr), flags_112(0) {
}

DsLocustManager::~DsLocustManager() {
  DsLocustManager::Shutdown();
}

// Publish the message target, common manager or locust-specific identity and acquire one root-agent reference.
// Other IDs retain the shared agent query's output-clearing behavior and its actual supported identities.
bool DsLocustManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsLocustControl, DsLocustControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsLocustManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsLocustManager::Release() {
  return DsAgent::Release();
}

// Remove the load notification and release retained services in the original non-field order.
// Keep every pointer published through its callback, remove the cheat subscription last, then clear both record ranges.
bool DsLocustManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&demolition);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&simulation);
    ScRelease(&city);
    if (cheats) {
      cheats->RemoveNotification(static_cast<ScMessageTarget *>(this), 0);
      cheats->Release();
      cheats = nullptr;
    }
    history.Clear();
    pending.Clear();
    initialized = false;
  }
  return true;
}

// Load the fifteen locust tunables through the real loose/package reader, retaining prior globals for missing keys.
// Temporary key/section Strings retire before numeric conversion; class-wide initialization succeeds even on misses.
bool DsLocustManager::StaticInit() {
  DsIni reader;
  ScString path, value;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  const char *keys[] = {"MaxLocustInstances", "MinMonthsBetweenLocustAttacks", "MaxMonthsBetweenLocustAttacks",
      "PercentageChanceOfLocustAttackEachYearTimes100", "SwarmSpeed", "TimeBeforeFirstPremonitionMessage",
      "TimeBeforeSecondPremonitionMessage", "TimeBeforeThirdPremonitionMessage", "TimeBeforeLocustAttack",
      "MinLocustSwarmsPerInstance", "MaxLocustSwarmsPerInstance", "SwarmToCropRatio", "MaxLocustsPerInstanceCheatSwarm",
      "YearOfFirstPossibleVisitationAppearance", "MaxNumCropsToAttack"};
  u32 *targets[] = {nullptr, &DsLocustMinimumDelay, &DsLocustMaximumDelay, &DsLocustChance, &DsLocustSwarmSpeed,
      &DsLocustFirstNoticeTime, &DsLocustSecondNoticeTime, &DsLocustThirdNoticeTime, &DsLocustAttackTime,
      &DsLocustMinimumSwarms, &DsLocustMaximumSwarms, &DsLocustSwarmCropRatio, &DsLocustCheatSwarmMaximum,
      &DsLocustFirstVisitationYear, &DsLocustMaximumCrops};
  for (u32 index = 0; index < 15; ++index) {
    u8 found;
    {
      ScString key(keys[index]), section("LocustDisasterTunables");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found) {
      u32 parsed = DsParseNumber(value.ToChar());
      if (index == 0)
        DsLocustMaximumActive = i32(parsed);
      else
        *targets[index] = parsed;
    }
  }
  return true;
}

bool DsLocustManager::StaticShutdown() {
  return true;
}

// Report history size or copy the requested capped prefix, publishing count before copying any record.
// Reread the mutable count and current history storage during iteration to preserve defined output aliasing.
u32 DsLocustManager::GetRecords(DsLocustRecord *records, u32 *count) const {
  if (!records) {
    *count = history.size;
    return *count;
  }
  u32 available = history.size;
  if (available > *count)
    available = *count;
  *count = available;
  for (u32 index = 0; index < *count; ++index)
    records[index] = history.data[index];
  return *count;
}

// Construct the real locust manager and return its common manager identity with the original zero reference count.
// The director performs the requested query and handles the factory product's first lifetime acquisition.
ScUnknown *DsCreateLocustManager() {
  return static_cast<DsManager *>(new DsLocustManager);
}

i32 DsLocustMaximumActive = 1;
u32 DsLocustMinimumDelay = 120, DsLocustMaximumDelay = 480, DsLocustChance = 500;

// Replace the raw automatic mode around the synchronous common-manager attempt, then restore the saved mode.
u8 DsLocustManager::Start(u8 automatic) {
  u8 previous = automatic_start;
  automatic_start = automatic;
  u8 result = AttemptDisasterStart();
  automatic_start = previous;
  return result;
}

// Scan columns before rows until the mutable output count reaches capacity, querying altitude only for crop cells.
// Publish coordinates before the terrain callback and reread count before writing altitude and incrementing it.
void DsLocustManager::FindCropCells(ScVec3U *cells, u32 *count, u32 capacity) {
  *count = 0;
  u32 columns = city->CellCountX(), rows = city->CellCountZ();
  for (u32 x = 0; x < columns && *count < capacity; ++x) {
    for (u32 z = 0; z < rows && *count < capacity; ++z) {
      if (IsCropAtCell(x, z)) {
        cells[*count].x = x;
        cells[*count].z = z;
        u8 altitude;
        terrain->GetVertexAltitude(x, z, &altitude);
        cells[*count].y = altitude;
        ++*count;
      }
    }
  }
}

// Advance the idle counter only below the signed active limit, then draw after the strict minimum delay.
// After a successful chance or expired maximum delay, require a crop cell before exposing automatic mode to the attempt.
bool DsLocustManager::Tick() {
  if (i32(active_count) < DsLocustMaximumActive && active_count == 0) {
    elapsed_updates = i32(u32(elapsed_updates) + 1);
    if (u32(elapsed_updates) > DsLocustMinimumDelay &&
        (random.Next(10000) < DsLocustChance || u32(elapsed_updates) > DsLocustMaximumDelay)) {
      ScVec3U cells[2];
      u32 count = 0;
      FindCropCells(cells, &count, 1);
      if (count) {
        automatic_start = 1;
        AttemptDisasterStart();
        automatic_start = 0;
      }
    }
  }
  return true;
}

// Preserve the original nonrecursive exclusion around the locust trigger, independently of the unrelated allocator.
static ScGate DsLocustStartLock;

// Require room below the signed active cap and the first visitation year before constructing a retained instance.
// Pass the stored mode as the premonition gate, then release the temporary hold before resetting cadence and counting it.
bool DsLocustManager::AttemptDisasterStart() {
  DsLocustStartLock.Lock();
  bool started = false;
  if (i32(active_count) < DsLocustMaximumActive) {
    CtDate current(0u, 0, 0);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    if (current.Year() >= DsLocustFirstVisitationYear) {
      auto *instance = new DsLocustInstance;
      instance->AddRef();
      instance->Init(this, 0, automatic_start, 1);
      instance->Release();
      elapsed_updates = 0;
      ++active_count;
      started = true;
    }
  }
  DsLocustStartLock.Unlock();
  return started;
}

// Toggle the recognized cheat or replay the pending-record count captured when loading completes.
// Reload pending storage for each construction and count only after the instance's temporary reference is released.
bool DsLocustManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    if (u32(message->data1) == kDsLocustSwarmCheatId) {
      bool enabled = cheats->GetCheatHasBeenSet(kDsLocustSwarmCheatId);
      cheats->SetCheatHasBeenSet(kDsLocustSwarmCheatId, !enabled);
    }
  } else if (message->type == kWnMsg_CityViewReady) {
    u32 count = pending.size;
    for (u32 index = 0; index < count; ++index) {
      auto *instance = new DsLocustInstance;
      instance->AddRef();
      instance->Init(this, pending.data + index);
      instance->Release();
      ++active_count;
    }
    pending.Clear();
  }
  return true;
}

void DsLocustManager::RecordInstance(DsLocustInstance *instance) {
  *history.Append() = instance->record;
}

// Capture history before active shutdown, then notify the current layer that the instance has been removed.
void DsLocustManager::FinishInstance(DsLocustInstance *instance) {
  RecordInstance(instance);
  instance->Shutdown();
  layer->DisasterInstanceChange(instance, 4);
}

void DsLocustManager::StopInstance(DsInstance *instance) {
  static_cast<DsLocustInstance *>(instance)->Shutdown();
}
