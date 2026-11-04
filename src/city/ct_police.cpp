#include "city/ct_police.h"

#include <math.h>
#include <string.h>

#include <cassert>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/oc_service_building.h"
#include "city/public_services.h"
#include "city/sp_service_map_sprite.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_residential.h"
#include "geometry/ge_types.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/stream.h"
#include "terrain/api.h"
#include "ui/api.h"

// Serialized keys retain their record-format role independently of live class and interface identities.
enum CtPoliceArchiveGroup : u32 {
  kCtPoliceRecordGroup = 0xabf2ec,
};

CtDailyFire::CtDailyFire()
    : random(kScRandomSeedFromClock), city(nullptr), layer(nullptr), zones(nullptr), cells_per_tick(0), scan_x(4),
      scan_z(12), scan_state(0), update_period(0) {
}

// Retire the actual secondary module reference base without implicitly invoking the separate Shutdown method.
CtDailyFire::~CtDailyFire() = default;

bool CtDailyFire::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtDailyFire::AddRef() {
  return CtService::ScRefCount::AddRef();
}

u32 CtDailyFire::Release() {
  return CtService::ScRefCount::Release();
}

bool CtDailyFire::IsLockingCity() {
  return true;
}

bool CtDailyFire::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtDailyFire::DebugClassTag(ScStringView *) {
  return false;
}

bool CtDailyFire::DebugTypeTag(ScStringView *) {
  return false;
}

u8 CtDailyFire::Shutdown() {
  ScRelease(&city);
  ScRelease(&layer);
  ScRelease(&zones);
  return 1;
}

// Initialize only native placeholders for fields whose original values were unwritten until Init.
CtDailyPolice::CtDailyPolice()
    : random(kScRandomSeedFromClock), city(nullptr), layer(nullptr), crime(nullptr), zones(nullptr), cells_per_tick(0),
      scan_x(4), scan_z(8), scan_state(0), update_period(0), daily_release_rate(0), conviction_rate(0),
      no_jail_penalty(0) {
}

// Retire the actual secondary module reference base without implicitly invoking the separate Shutdown method.
CtDailyPolice::~CtDailyPolice() = default;

bool CtDailyPolice::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtDailyPolice::AddRef() {
  return CtService::ScRefCount::AddRef();
}

u32 CtDailyPolice::Release() {
  return CtService::ScRefCount::Release();
}

bool CtDailyPolice::IsLockingCity() {
  return true;
}

bool CtDailyPolice::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtDailyPolice::DebugClassTag(ScStringView *) {
  return false;
}

bool CtDailyPolice::DebugTypeTag(ScStringView *) {
  return false;
}

u8 CtDailyPolice::Shutdown() {
  ScRelease(&city);
  ScRelease(&layer);
  ScRelease(&crime);
  ScRelease(&zones);
  return 1;
}

// Retain the city, primary fire receiver and zoning view in that order, then copy the live period.
// Query height before width, initialize the scan state and retain the original cursor defaults4/12.
u8 CtDailyFire::Init(CtCity *source) {
  city = ScAddRefNotNull(source);
  layer = ScAddRefNotNull(static_cast<CtFire *>(city->FireLayer()));
  zones = ScAddRefNotNull(city->ZoneLayer());
  update_period = CtFire::update_period;
  if (!update_period)
    update_period = 1;
  CtFire *target = layer;
  u32 rows = target->CellCountZ(), columns = target->CellCountX();
  scan_state = 0;
  cells_per_tick = columns * rows / (16u * update_period);
  scan_x = 4;
  scan_z = 12;
  return 1;
}

// Retain the city before querying its real police layer; refusal leaves that city hold for explicit Shutdown.
// On success retain crime and zoning, copy the current tuning, and initialize the original scan defaults4/8.
u8 CtDailyPolice::Init(CtCity *source) {
  city = ScAddRefNotNull(source);
  void *identity = nullptr;
  CtLayer *selected = city->GetSpecificLayer(kCtPoliceLayerType);
  if (!selected->QueryInterface(IID_CtPoliceLayer, &identity))
    return 0;
  layer = static_cast<CtPolice *>(static_cast<CtPoliceLayer *>(identity));
  crime = ScAddRefNotNull(city->CrimeLayer());
  zones = ScAddRefNotNull(city->ZoneLayer());
  update_period = CtPolice::update_period;
  if (!update_period)
    update_period = 1;
  daily_release_rate = CtPolice::daily_jail_release_rate;
  conviction_rate = CtPolice::conviction_rate;
  no_jail_penalty = CtPolice::no_jail_penalty;
  CtPolice *target = layer;
  u32 rows = target->CellCountZ(), columns = target->CellCountX();
  scan_state = 0;
  cells_per_tick = columns * rows / (16u * update_period);
  scan_x = 4;
  scan_z = 8;
  return 1;
}

// Publish no manipulator first; for the actual daily agent type, lazily construct and retain the fire manipulator.
// Initialize through the bound city and return the live retained pointer and original flag2 even if Init refuses.
bool CtFire::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new CtDailyFire);
      manipulator->Init(city);
    }
    *out = manipulator;
    *flags = 2;
  }
  return true;
}

// Publish no manipulator first, then lazily construct the police daily manipulator only for the original agent type.
// Preserve the retained instance, ignored Init result and flag2 rather than manufacturing a new status contract.
bool CtPolice::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new CtDailyPolice);
      manipulator->Init(city);
    }
    *out = manipulator;
    *flags = 2;
  }
  return true;
}

// Update strike state under the actual station lock, then scan scheduled cells through the real working and zone maps.
// Accumulate coverage only for zoned blocks and retain the original interleaved cursor and month-end ordering.
bool CtDailyFire::Apply(CtAgent *) {
  static const u32 offsets[16][2] = {{4, 12}, {4, 4}, {4, 0}, {8, 8}, {8, 4}, {0, 8}, {8, 0}, {0, 0}, {4, 8}, {0, 12},
      {12, 0}, {8, 12}, {12, 8}, {12, 12}, {0, 4}, {12, 4}};
  layer->station_lock.Lock();
  if (layer->on_strike) {
    ++layer->strike_event;
    if (layer->strike_event > CtFire::max_strike_duration || layer->funding_percentage >= 100 ||
        !layer->funded_buildings.first)
      layer->EndStrike();
  } else {
    u32 chance = layer->ChanceOfStrike();
    if (chance && random.Bounded(10000) < chance) {
      layer->on_strike = true;
      layer->strike_event = 0;
      ScMessage message(kCtMsg_FireStrikeChanged, 1, 0, 0);
      CtService::CtGetMessageServer()->MessagePost(message, false);
    }
  }
  layer->station_lock.Unlock();
  for (u32 i = 0; i < cells_per_tick; ++i) {
    layer->station_lock.Lock();
    i32 working_value;
    layer->working.GetValue(scan_x, scan_z, &working_value);
    if (layer->on_strike)
      working_value = i32(u32(working_value) * u32(CtFire::strike_efficiency)) / 100;
    bool zoned = false;
    for (u32 j = 0; j < 16 && !zoned; ++j) {
      u8 zone = 0;
      zones->GetValue(scan_x + (j & 3), scan_z + (j >> 2), &zone);
      zoned = zone != 0;
    }
    i32 scaled = working_value / 16;
    u8 value = scaled > 255 ? 255 : scaled < 0 ? 0 : u8(scaled);
    if (zoned) {
      ++layer->coverage_samples;
      layer->coverage_sum += value;
      if (!value)
        ++layer->uncovered_samples;
    }
    layer->SetValue(scan_x, scan_z, &value);
    scan_x += 16;
    if (scan_x >= layer->CellCountX()) {
      scan_z += 16;
      if (scan_z >= layer->CellCountZ()) {
        ++scan_state;
        if (scan_state >= 16) {
          scan_state = 0;
          layer->EndOfMonth();
        }
        scan_z = offsets[scan_state][1];
      }
      scan_x = offsets[scan_state][0];
    }
    layer->station_lock.Unlock();
  }
  return true;
}

// Preserve the installed magic-add conversion and low-DWORD wrap used for police population calculations.
static u32 CtRoundPolice(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Sum live jail values under the original lock, then redistribute through each retained jail view in list order.
void CtPolice::RedistributeInmates() {
  jail_capacity = 0;
  inmate_count = 0;
  if (!jails.first) {
    criminals_at_large = 95u * criminals_at_large / 100u;
    return;
  }
  station_lock.Lock();
  for (CtJailLink *entry = jails.first; entry; entry = entry->next) {
    u32 capacity = entry->jail->GetInmateCapacity();
    u32 inmates = entry->jail->GetInmateCount();
    jail_capacity += capacity;
    inmate_count += inmates;
  }
  criminals_at_large += inmate_count;
  f64 exact_ratio = f64(criminals_at_large) / f64(i32(jail_capacity));
  f32 ratio = f32(exact_ratio);
  if (exact_ratio > max_jail_overcrowding)
    ratio = max_jail_overcrowding;
  for (CtJailLink *entry = jails.first; entry; entry = entry->next) {
    if (!criminals_at_large) {
      entry->jail->SetInmateCount(0);
      continue;
    }
    u32 capacity = entry->jail->GetInmateCapacity();
    if (ratio > 1.0f) {
      u32 assigned = CtRoundPolice(f64(capacity) * ratio);
      entry->jail->SetInmateCount(assigned);
      u32 available = criminals_at_large;
      criminals_at_large = assigned <= available ? available - assigned : 0;
    } else if (capacity >= criminals_at_large) {
      entry->jail->SetInmateCount(criminals_at_large);
      u32 remaining = criminals_at_large;
      criminals_at_large -= remaining;
    } else {
      entry->jail->SetInmateCount(capacity);
      criminals_at_large -= capacity;
    }
  }
  station_lock.Unlock();
}

// The caller may already hold the recursive station lock; this method takes and releases its own original hold.
void CtDailyPolice::RehabilitateCriminals() {
  layer->station_lock.Lock();
  for (CtJailLink *entry = layer->jails.first; entry; entry = entry->next) {
    u32 count = entry->jail->GetInmateCount();
    u32 released = CtRoundPolice(f64(count) * daily_release_rate);
    entry->jail->SetInmateCount(count - released);
  }
  layer->station_lock.Unlock();
}

// Nonpositive inputs leave the live criminal count and jail allocations untouched.
void CtDailyPolice::IncarcerateCriminals(i32 count) {
  if (count > 0) {
    layer->criminals_at_large += u32(count);
    layer->RedistributeInmates();
  }
}

// Accumulate actual coverage and caught crime, roll month-end state and distribute positive convictions afterward.
bool CtDailyPolice::Apply(CtAgent *) {
  static const u32 offsets[16][2] = {{4, 8}, {0, 12}, {12, 0}, {8, 12}, {12, 8}, {12, 12}, {0, 4}, {12, 4}, {4, 12},
      {4, 4}, {4, 0}, {8, 8}, {8, 4}, {0, 8}, {8, 0}, {0, 0}};
  layer->station_lock.Lock();
  if (layer->on_strike) {
    ++layer->strike_event;
    if (layer->strike_event > CtPolice::max_strike_duration || layer->funding_percentage >= 100 ||
        !layer->funded_buildings.first)
      layer->EndStrike();
  } else {
    u32 chance = layer->ChanceOfStrike();
    if (chance && random.Bounded(10000) < chance) {
      layer->on_strike = true;
      layer->strike_event = 0;
      ScMessage message(kCtMsg_PoliceStrikeChanged, 1, 0, 0);
      CtService::CtGetMessageServer()->MessagePost(message, false);
    }
  }
  layer->station_lock.Unlock();
  layer->station_lock.Lock();
  RehabilitateCriminals();
  layer->station_lock.Unlock();
  u32 caught = 0;
  for (u32 i = 0; i < cells_per_tick; ++i) {
    layer->station_lock.Lock();
    i32 working_value;
    layer->working.GetValue(scan_x, scan_z, &working_value);
    if (layer->on_strike)
      working_value = i32(u32(working_value) * u32(CtPolice::strike_efficiency)) / 100;
    if (layer->criminals_at_large)
      working_value = i32(CtRoundPolice(f64(working_value) * no_jail_penalty));
    bool zoned = false;
    for (u32 j = 0; j < 16 && !zoned; ++j) {
      u8 zone = 0;
      zones->GetValue(scan_x + (j & 3), scan_z + (j >> 2), &zone);
      zoned = zone != 0;
    }
    i32 scaled = working_value / 16;
    u8 value = scaled > 255 ? 255 : scaled < 0 ? 0 : u8(scaled);
    if (zoned) {
      ++layer->coverage_samples;
      layer->coverage_sum += value;
      if (!value)
        ++layer->uncovered_samples;
    }
    layer->SetValue(scan_x, scan_z, &value);
    u8 crime_value = 0;
    crime->GetValue(scan_x, scan_z, &crime_value);
    if (crime_value && value)
      caught += value <= crime_value ? value : crime_value;
    scan_x += 16;
    if (scan_x >= layer->CellCountX()) {
      scan_z += 16;
      if (scan_z >= layer->CellCountZ()) {
        ++scan_state;
        if (scan_state >= 16) {
          scan_state = 0;
          layer->EndOfMonth();
        }
        scan_z = offsets[scan_state][1];
      }
      scan_x = offsets[scan_state][0];
    }
    layer->station_lock.Unlock();
  }
  layer->station_lock.Lock();
  i32 convicted = i32(CtRoundPolice(f64(i32(caught)) * conviction_rate));
  if (convicted > 0)
    IncarcerateCriminals(convicted);
  layer->station_lock.Unlock();
  return true;
}

CtFireStationList::~CtFireStationList() {
  Clear();
}

// Remove the stored nodes without changing their borrowed or explicitly retained payloads.
void CtFireStationList::Clear() {
  while (first) {
    CtFireStationLink *next = first->next;
    delete first;
    first = next;
  }
}

CtServiceFundingList::~CtServiceFundingList() {
  Clear();
}

// Remove the stored nodes without changing their borrowed or explicitly retained payloads.
void CtServiceFundingList::Clear() {
  while (first) {
    CtServiceFundingLink *next = first->next;
    delete first;
    first = next;
  }
}

CtServiceDispatchList::~CtServiceDispatchList() {
  Clear();
}

// Remove the stored nodes without changing their borrowed or explicitly retained payloads.
void CtServiceDispatchList::Clear() {
  while (first) {
    CtServiceDispatchLink *next = first->next;
    delete first;
    first = next;
  }
  last = nullptr;
}

CtFire::CtFire()
    : CtSizedByteMap(2), working(2), random(kScRandomSeedFromClock), hazard_identity(nullptr), funding_percentage(100),
      coverage_samples(0), coverage_sum(0), uncovered_samples(0), bound(false), agent_tree(nullptr), start_sim(nullptr),
      animations(nullptr), city(nullptr), manipulator(nullptr), occupants(nullptr), budget(nullptr),
      department(nullptr), upkeep(0), on_strike(false), strike_event(0), average_coverage(0), percent_coverage(0) {
}

// Destroy the lock, dispatch list, working map and the two station lists without implicitly calling Shutdown.
CtFire::~CtFire() = default;

// Bind the city and its service dependencies in original acquisition order, then initialize both shifted maps.
// A failure invokes the real virtual Shutdown; its unbound early return preserves the original partial-acquisition behavior.
bool CtFire::Init(CtCity *source) {
  if (bound)
    Shutdown();
  if (!source) {
    Shutdown();
    return false;
  }
  city = ScAddRefNotNull(source);
  budget = city->BudgetLayer();
  if (!budget) {
    Shutdown();
    return false;
  }
  budget->AddRef();
  start_sim = city->StrtSimLayer();
  if (!start_sim) {
    Shutdown();
    return false;
  }
  start_sim->AddRef();
  if (!source->GetSpecificLayer(kCtFlammabilityLayerType)->QueryInterface(IID_EnHazardMap, &hazard_identity)) {
    Shutdown();
    return false;
  }
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    Shutdown();
    return false;
  }
  occupants->AddRef();
  animations = city->AnimOccupantManager();
  if (!animations) {
    Shutdown();
    return false;
  }
  animations->AddRef();
  agent_tree = CtService::CtGetAgentTree();
  if (!agent_tree) {
    Shutdown();
    return false;
  }
  agent_tree->AddRef();
  u8 empty = 0;
  bound = Init(city, &empty);
  i32 zero = 0;
  working.Init(city, &zero);
  stations.Clear();
  funded_buildings.Clear();
  upkeep = 0;
  on_strike = false;
  coverage_samples = coverage_sum = uncovered_samples = 0;
  average_coverage = percent_coverage = 0;
  if (!bound)
    Shutdown();
  return bound;
}

// Release the retained service dependencies in the original order and retire both map stores explicitly.
// The unbound path returns immediately; dispatch payloads are released only while the animation manager exists.
bool CtFire::Shutdown() {
  if (!bound)
    return true;
  if (occupants) {
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
    occupants->Release();
    occupants = nullptr;
  }
  ScRelease(&budget);
  ScRelease(&department);
  if (hazard_identity) {
    static_cast<CtFlammabilityLayer *>(hazard_identity)->Release();
    hazard_identity = nullptr;
  }
  if (animations) {
    for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next) {
      void *identity = nullptr;
      if (entry->marker->QueryInterface(IID_ScOccupant, &identity)) {
        ScOccupant *occupant = static_cast<ScOccupant *>(identity);
        animations->RemoveOccupant(occupant);
        occupant->Release();
      }
      entry->marker->Release();
      if (entry->vehicle) {
        entry->vehicle->Release();
        entry->vehicle = nullptr;
      }
    }
  }
  dispatches.Clear();
  ScRelease(&animations);
  ScRelease(&start_sim);
  ScRelease(&city);
  if (manipulator) {
    manipulator->Shutdown();
    manipulator->Release();
    manipulator = nullptr;
  }
  if (agent_tree)
    agent_tree->Release();
  agent_tree = nullptr;
  working.Shutdown();
  stations.Clear();
  funded_buildings.Clear();
  bound = false;
  return CtSizedByteMap::Shutdown();
}

// Load the daily scan cursor and service strike/coverage words from the original city-layer record.
// Stop at the first refused field, release the serial interface and close the opened record in that order.
bool CtFire::Init(CtCity *, RsDatabaseSegment *segment) {
  RsRecord *opened = nullptr;
  if (!segment->OpenRecord(RsKey{kRsCityLayerRecordType, kCtFireRecordGroup, 0}, &opened))
    return false;
  bool result = false;
  void *identity = nullptr;
  if (opened->QueryInterface(IID_RsStream, &identity)) {
    RsStream *record = static_cast<RsStream *>(identity);
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new CtDailyFire);
      manipulator->Init(city);
    }
    u8 strike = 0;
    result = record->ReadU8(&manipulator->scan_state) && record->ReadU32(&manipulator->scan_x) &&
        record->ReadU32(&manipulator->scan_z) && record->ReadU8(&strike);
    if (result) {
      on_strike = strike != 0;
      result = record->ReadU32(&strike_event) && record->ReadU32(&coverage_samples) && record->ReadU32(&coverage_sum) &&
          record->ReadU32(&uncovered_samples) && record->ReadU32(&average_coverage) &&
          record->ReadU32(&percent_coverage);
    }
    record->Release();
  }
  segment->CloseRecord(&opened);
  return result;
}

// Save the daily cursor followed by strike state and coverage words to the original city-layer record.
// Lazily create the actual daily manipulator before writing, and always close an opened record after serial release.
bool CtFire::Save(CtCity *, RsDatabaseSegment *segment) {
  RsRecord *opened = nullptr;
  if (!segment->OpenRecordForWrite(RsKey{kRsCityLayerRecordType, kCtFireRecordGroup, 0}, &opened))
    return false;
  bool result = false;
  void *identity = nullptr;
  if (opened->QueryInterface(IID_RsStream, &identity)) {
    RsStream *record = static_cast<RsStream *>(identity);
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new CtDailyFire);
      manipulator->Init(city);
    }
    result = record->WriteU8(manipulator->scan_state) && record->WriteU32(manipulator->scan_x) &&
        record->WriteU32(manipulator->scan_z) && record->WriteU8(on_strike) && record->WriteU32(strike_event) &&
        record->WriteU32(coverage_samples) && record->WriteU32(coverage_sum) && record->WriteU32(uncovered_samples) &&
        record->WriteU32(average_coverage) && record->WriteU32(percent_coverage);
    record->Release();
  }
  segment->CloseRecord(&opened);
  return result;
}

bool CtFire::OccupantInserted(u32, ScOccupant *occupant) {
  return HandleOccupantChange(0, occupant) != 0;
}

bool CtFire::OccupantRemoved(u32, ScOccupant *occupant) {
  return HandleOccupantChange(1, occupant) != 0;
}

// Apply the original circular contribution to the signed working grid and its visible byte map.
void CtFire::UpdateMaps(const GeCellRect &rect, u8 radius, i16 maximum, i16 minimum) {
  if (!radius)
    return;
  u32 left = u32(rect.left), top = u32(rect.top), right = u32(rect.right), bottom = u32(rect.bottom);
  u32 center_x = left + ((right - left) >> 1), center_z = top + ((bottom - top) >> 1);
  u32 first_x = left >= radius ? left - radius : 0, first_z = top >= radius ? top - radius : 0;
  u32 last_x = right + radius, last_z = bottom + radius;
  last_x = UintMin(last_x, CellCountX() - 1);
  last_z = UintMin(last_z, CellCountZ() - 1);
  station_lock.Lock();
  for (u32 x = first_x; x <= last_x; ++x) {
    for (u32 z = first_z; z <= last_z; ++z) {
      u32 dx = center_x - x, dz = center_z - z;
      i32 squared = i32(dx * dx + dz * dz);
      i32 distance = i32(CtServiceRoundCoverage(floor(sqrt(f64(squared)))));
      if (distance <= radius) {
        i32 value;
        working.GetValue(x, z, &value);
        value = i32(u32(value) + u32(maximum - distance * (maximum - minimum) / radius));
        working.SetValue(x, z, &value);
        i32 visible = value / 16;
        u8 byte = visible > 255 ? 255 : visible < 0 ? 0 : u8(visible);
        SetValue(x, z, &byte);
      }
    }
  }
  station_lock.Unlock();
}

// Recalculate upkeep and station contributions under the real recursive lock after publishing the new funding byte.
// Reset the signed grid before traversing station and dispatch lists in their original order.
void CtFire::SetFundingPercentage(u8 percentage) {
  funding_percentage = percentage;
  upkeep = 0;
  station_lock.Lock();
  for (CtServiceFundingLink *entry = funded_buildings.first; entry; entry = entry->next) {
    upkeep += u32(entry->upkeep->GetMonthlyUpkeep());
    entry->upkeep->SetFundingPercentage(percentage);
  }
  i32 zero = 0;
  working.SetAllCells(&zero);
  for (CtFireStationLink *entry = stations.first; entry; entry = entry->next) {
    if (entry->station) {
      u8 radius = entry->station->GetCoverageRadius();
      u8 maximum = entry->station->GetMaxCoverage(), minimum = entry->station->GetMinCoverage();
      GeCellRect rect{};
      entry->station->AsISC3Occupant()->GetBoundingRectInCellCoords(&rect);
      UpdateMaps(rect, radius, maximum, minimum);
    }
  }
  for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next) {
    GeCellRect rect{entry->x, entry->z, entry->x, entry->z};
    UpdateMaps(rect, dispatch_radius, dispatch_effect, dispatch_effect);
  }
  station_lock.Unlock();
}

// Queried station and upkeep identities are borrowed by the node lists after their temporary holds are released.
u8 CtFire::HandleOccupantChange(i32 change, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  u32 type = occupant->TypeId();
  if (!agent_tree->IsAncestor(kCtAgentFireStation, type))
    return 1;
  void *station_identity = nullptr;
  if (!occupant->QueryInterface(IID_BdFireStationView, &station_identity))
    return 1;
  BdFireStationView *station = static_cast<BdFireStationView *>(station_identity);
  if (station->AsISC3Building()->GetOccupancyState() == 1) {
    void *funding_identity = nullptr;
    occupant->QueryInterface(IID_BdUpkeepView, &funding_identity);
    BdUpkeepView *funding = static_cast<BdUpkeepView *>(funding_identity);
    i32 amount = funding ? funding->GetMonthlyUpkeep() : 0;
    if (change == 0) {
      station_lock.Lock();
      if (funding) {
        funding->SetFundingPercentage(funding_percentage);
        CtServiceFundingLink **slot = &funded_buildings.first;
        while (*slot)
          slot = &(*slot)->next;
        *slot = new CtServiceFundingLink{nullptr, funding};
      }
      CtFireStationLink **slot = &stations.first;
      while (*slot)
        slot = &(*slot)->next;
      *slot = new CtFireStationLink{nullptr, station};
      station_lock.Unlock();
    }
    u8 radius = station->GetCoverageRadius();
    i16 maximum = station->GetMaxCoverage(), minimum = station->GetMinCoverage();
    if (change == 1) {
      maximum = i16(-maximum);
      minimum = i16(-minimum);
      amount = i32(0u - u32(amount));
      station_lock.Lock();
      CtFireStationLink **slot = &stations.first;
      while (*slot && (*slot)->station != station)
        slot = &(*slot)->next;
      if (*slot) {
        CtFireStationLink *removed = *slot;
        *slot = removed->next;
        delete removed;
      }
      u32 dispatch_count = 0, station_count = 0;
      for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next)
        ++dispatch_count;
      for (CtFireStationLink *entry = stations.first; entry; entry = entry->next)
        ++station_count;
      if (dispatch_count > station_count + 1)
        RevokeDispatch(dispatches.last->x, dispatches.last->z);
      if (funding) {
        CtServiceFundingLink **funded = &funded_buildings.first;
        while (*funded && (*funded)->upkeep != funding)
          funded = &(*funded)->next;
        if (*funded) {
          CtServiceFundingLink *removed = *funded;
          *funded = removed->next;
          delete removed;
        }
      }
      station_lock.Unlock();
    }
    upkeep += u32(amount);
    department->SetTotalFundingNeeded(i64(i32(upkeep)));
    GeCellRect bounds{};
    occupant->GetBoundingRectInCellCoords(&bounds);
    UpdateMaps(bounds, radius, maximum, minimum);
    if (funding)
      funding->Release();
  }
  station->Release();
  return 1;
}

// Select a dispatch origin from the live station list or independent city coordinates. A selected station must publish
// its occupant identity before its coordinates are consumed.
void CtFire::SelectDispatchOrigin(u32 *x, u32 *z) {
  u32 count = 0;
  for (CtFireStationLink *entry = stations.first; entry; entry = entry->next)
    ++count;
  if (!count) {
    *x = random.Bounded(city->CellCountX());
    *z = random.Bounded(city->CellCountZ());
    return;
  }
  u32 index = random.Bounded(count);
  station_lock.Lock();
  CtFireStationLink *entry = stations.first;
  while (index--)
    entry = entry->next;
  void *identity = nullptr;
  entry->station->QueryInterface(IID_ScOccupant, &identity);
  station_lock.Unlock();
  assert(identity);
  ScOccupant *occupant = static_cast<ScOccupant *>(identity);
  occupant->GetLocationInCellCoords(x, z);
  occupant->Release();
}

// Dispatch the actual helicopter, create its map marker, and append both real identities to the service list.
// Preserve the original limit, event and coverage update ordering, including the adjusted marker X coordinate.
bool CtFire::DispatchCrew(u32 x, u32 z) {
  u32 station_count = 0, dispatch_count = 0;
  for (CtFireStationLink *entry = stations.first; entry; entry = entry->next)
    ++station_count;
  for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next)
    ++dispatch_count;
  if (dispatch_count >= station_count + 1)
    RevokeDispatch(dispatches.last->x, dispatches.last->z);
  u32 origin_x, origin_z;
  SelectDispatchOrigin(&origin_x, &origin_z);
  ScOccupant *vehicle = reinterpret_cast<ScOccupant *>(start_sim->DispatchHelicopter(origin_x, origin_z, x, z));
  if (vehicle)
    vehicle->AddRef();
  if (x < CellCountX() - 1)
    ++x;
  CtService::OcBuilding *marker = new CtService::OcStaticAnimBuilding;
  marker->AddRef();
  RsKey key{kRsBuildingDataResourceType, 1406u, 13004u};
  marker->Init(&key);
  marker->SetLocationInCellCoords(x, z);
  u8 altitude = 0;
  city->DirtBag()->GetVertexAltitude(x, z, &altitude);
  marker->SetAlt(altitude);
  void *identity = nullptr;
  if (marker->QueryInterface(IID_ScOccupant, &identity)) {
    ScOccupant *occupant = static_cast<ScOccupant *>(identity);
    animations->InsertOccupant(occupant, 1, 0, true);
    occupant->Release();
  }
  station_lock.Lock();
  CtServiceDispatchLink *entry =
      new CtServiceDispatchLink{dispatches.last, nullptr, vehicle, x, z, static_cast<BdBuilding *>(marker)};
  if (dispatches.last)
    dispatches.last->next = entry;
  else
    dispatches.first = entry;
  dispatches.last = entry;
  station_lock.Unlock();
  if (ApBoxX *box = CtService::CtGetBoxX())
    box->Event(kAuEventPlayEffect, 35, 0, 0, 0);
  GeCellRect bounds{x, z, x, z};
  UpdateMaps(bounds, dispatch_radius, dispatch_effect, dispatch_effect);
  return true;
}

// Remove the matching dispatch under the real lock, revoke its vehicle and remove its marker before updating coverage.
// Capture coordinates before deleting the node so that the original police read-after-free is not reproduced.
bool CtFire::RevokeDispatch(u32 x, u32 z) {
  station_lock.Lock();
  CtServiceDispatchLink *entry = dispatches.first;
  while (entry && (entry->x != x || entry->z != z))
    entry = entry->next;
  if (!entry) {
    station_lock.Unlock();
    return false;
  }
  if (entry->vehicle) {
    start_sim->RevokeDispatchedHelicopter(entry->vehicle);
    entry->vehicle->Release();
    entry->vehicle = nullptr;
  }
  void *identity = nullptr;
  if (entry->marker->QueryInterface(IID_ScOccupant, &identity)) {
    ScOccupant *occupant = static_cast<ScOccupant *>(identity);
    animations->RemoveOccupant(occupant);
    occupant->Release();
  }
  entry->marker->Release();
  entry->marker = nullptr;
  u32 dispatch_x = entry->x, dispatch_z = entry->z;
  if (entry->previous)
    entry->previous->next = entry->next;
  else
    dispatches.first = entry->next;
  if (entry->next)
    entry->next->previous = entry->previous;
  else
    dispatches.last = entry->previous;
  delete entry;
  GeCellRect bounds{dispatch_x, dispatch_z, dispatch_x, dispatch_z};
  i16 removal = -i16(dispatch_effect);
  UpdateMaps(bounds, dispatch_radius, removal, removal);
  station_lock.Unlock();
  return true;
}

u32 CtFire::CellCountX() const {
  return CtSizedByteMap::CellCountX();
}

u32 CtFire::CellCountZ() const {
  return CtSizedByteMap::CellCountZ();
}

// The byte is obtained through the qualified sized-map body, not another virtual override.
void CtFire::GetValueSint32(u32 x, u32 z, i32 *value) const {
  u8 cell;
  CtSizedByteMap::GetValue(x, z, &cell);
  *value = i32(u32(cell) * kCtByteMapSint32Scale);
}

bool CtFire::InBounds(u32 x, u32 z) const {
  return CtSizedByteMap::InBounds(x, z);
}

bool CtFire::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::InBounds(x0, z0, x1, z1);
}

bool CtFire::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::ValidRect(x0, z0, x1, z1);
}

bool CtFire::SerialRead(RsStream *record) {
  return CtSizedByteMap::SerialRead(record);
}

bool CtFire::SerialWrite(RsStream *record) const {
  return CtSizedByteMap::SerialWrite(record);
}

bool CtFire::Init(CtCity *source_city, const u8 *initial) {
  return CtSizedByteMap::Init(source_city, initial);
}

void CtFire::GetValue(u32 x, u32 z, u8 *value) const {
  CtSizedByteMap::GetValue(x, z, value);
}

void CtFire::SetValue(u32 x, u32 z, const u8 *value) {
  CtSizedByteMap::SetValue(x, z, value);
}

void CtFire::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  CtSizedByteMap::SetValue(x0, z0, x1, z1, value);
}

void CtFire::SetAllCells(const u8 *value) {
  CtSizedByteMap::SetAllCells(value);
}

// Publish the actual fire, city, change or query-string view before retaining through the primary map lifetime. Other
// identities delegate to the genuine sized-map query, which clears rejected outputs.
bool CtFire::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtFireLayer, CtFireLayer);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  HANDLE_QUERY_INTERFACE(IID_ScQueryInfoString, ScQueryInfoString);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  return CtSizedByteMap::QueryInterface(iid, out);
}

u32 CtFire::AddRef() {
  return CtSizedByteMap::AddRef();
}

u32 CtFire::Release() {
  return CtSizedByteMap::Release();
}

u32 CtFire::GetLayerType() {
  return kCtFireLayerType;
}

bool CtFire::DoMessage(ScMessage *) {
  return false;
}

bool CtFire::StaticShutdown() {
  return true;
}

bool CtFire::Init(CtCity *, ScImporter *) {
  return true;
}

bool CtFire::SimulationEnd() {
  return true;
}

bool CtFire::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool CtFire::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool CtFire::CellChangedAll(u32) {
  return true;
}

bool CtFire::CellChanged(u32, u32, u32) {
  return true;
}

bool CtFire::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

bool CtFire::DebugClassTag(ScStringView *) {
  return false;
}

bool CtFire::DebugTypeTag(ScStringView *) {
  return false;
}

u32 CtFire::GetInfoStringCount(u32) {
  return 0;
}

u8 CtFire::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

u32 CtFire::GetMapSpriteCount() {
  return 1;
}

bool CtFire::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10029;
  return true;
}

u32 CtFire::GetTotalCityFireUpkeep() {
  return upkeep;
}

bool CtFire::IsOnStrike() {
  return on_strike;
}

u32 CtFire::GetAverageCoverage() {
  return average_coverage;
}

u32 CtFire::GetPercentCoverage() {
  return percent_coverage;
}

// Clear an active strike before posting the original notification with its retained event word.
// An inactive strike performs no message lookup or field mutation.
void CtFire::EndStrike() {
  if (on_strike) {
    on_strike = false;
    ScMessage message(kCtMsg_FireStrikeChanged, 0, strike_event, 0);
    CtService::CtGetMessageServer()->MessagePost(message, false);
  }
}

// While on strike, return up to the caller capacity of borrowed station occupants, retaining each published result.
// Count the original list before taking the lock, then traverse the live list under the lock.
bool CtFire::GetStrikingBuildings(ScOccupant **out, u32 capacity, u32 *count) {
  if (!on_strike)
    return false;
  u32 available = 0;
  for (CtFireStationLink *entry = stations.first; entry; entry = entry->next)
    ++available;
  if (available > capacity)
    available = capacity;
  station_lock.Lock();
  u32 written = 0;
  for (CtFireStationLink *entry = stations.first; written < available && entry; entry = entry->next) {
    ScOccupant *occupant = entry->station->AsISC3Occupant();
    out[written++] = occupant;
    occupant->AddRef();
  }
  station_lock.Unlock();
  *count = written;
  return true;
}

// Capture the number of dispatches, then repeatedly revoke the live tail coordinates through the virtual method.
// Individual refusal results are ignored and the original method always reports success.
bool CtFire::RevokeAllDispatches() {
  u32 remaining = 0;
  for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next)
    ++remaining;
  while (remaining--) {
    CtServiceDispatchLink *entry = dispatches.last;
    RevokeDispatch(entry->x, entry->z);
  }
  return true;
}

// Acquire the original department budget into its live member and retain it before subscribing the change view.
// A missing occupant manager or refused budget lookup reports false without invoking the later subscription.
bool CtFire::SimulationBegin() {
  if (!occupants || !budget->GetDepartmentBudget(kEcFireDepartmentId, &department))
    return false;
  department->AddRef();
  u32 subscription;
  return occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &subscription, true);
}

// Otherwise compare live funding and the actual hazard average only while a funded building exists, then square the
// live funding shortfall when both original thresholds are crossed.
u32 CtFire::ChanceOfStrike() {
  if (on_strike)
    return 10000;
  if (funded_buildings.first && funding_percentage < min_funding &&
      static_cast<CtFlammabilityLayer *>(hazard_identity)->GetAverageValue() > max_flammability) {
    i32 shortfall = i32(min_funding) - i32(funding_percentage);
    return u32(shortfall * shortfall);
  }
  return 0;
}

// Publish average and percentage coverage from the completed samples, retaining original DWORD multiplication.
// An empty interval publishes zero for both results, and all three running totals are cleared afterward.
u8 CtFire::EndOfMonth() {
  u32 count = coverage_samples;
  if (count) {
    u32 total = coverage_sum;
    percent_coverage = 100u - 100u * uncovered_samples / count;
    average_coverage = total / count;
  } else {
    average_coverage = 0;
    percent_coverage = 0;
  }
  coverage_samples = 0;
  coverage_sum = 0;
  uncovered_samples = 0;
  return 1;
}

// Enable the original post-filter through the qualified common map-sprite method.
CtFireMapSprite::CtFireMapSprite(CtFire *source, CtZoneLayer *zone) : zones(nullptr) {
  if (zone)
    zones = ScAddRefNotNull(zone);
  layer = ScAddRefNotNull(source);
  u32 rows = source->CellCountZ(), columns = source->CellCountX();
  SetId(10029);
  SetBaseSize(columns, rows);
  CtService::SpMapSprite::SetPostFilter(true);
}

CtFireMapSprite::~CtFireMapSprite() {
  ScRelease(&layer);
  ScRelease(&zones);
}

bool CtFireMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

// Select the optional current-city zone, construct the original raster and query the requested interface.
// A refused query originally raw-frees storage without releasing constructor holds, so detach those owners first.
bool CtFire::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10029)
    return false;
  CtZoneLayer *zone = nullptr;
  if (CtService::CtHasCity())
    zone = CtService::CtGetCity()->ZoneLayer();
  CtFireMapSprite *sprite = new CtFireMapSprite(this, zone);
  if (sprite->QueryInterface(iid, out))
    return true;
  sprite->layer = nullptr;
  sprite->zones = nullptr;
  delete sprite;
  return false;
}

// Draw a cyan coverage circle and center dot for each station whose occupant identity can be acquired.
// Use the live station list and bounds, releasing each temporary occupant before reading its coverage radius.
bool CtFireMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  for (CtFireStationLink *entry = layer->stations.first; entry; entry = entry->next) {
    void *identity = nullptr;
    if (entry->station->QueryInterface(IID_ScOccupant, &identity)) {
      GeCellRect bounds{};
      static_cast<ScOccupant *>(identity)->GetBoundingRectInCellCoords(&bounds);
      static_cast<ScOccupant *>(identity)->Release();
      u32 x = (bounds.right + bounds.left) >> 1;
      u32 z = (bounds.bottom + bounds.top) >> 1;
      u8 radius = entry->station->GetCoverageRadius();
      u32 color = CtService::CtGetGraphics()->ConvertRGBValueToNative16Bit(0, 203, 255);
      DrawCircle(i32(x), i32(z), radius, 1, destination, rectangle, color, clip);
      DrawDot(i32(x), i32(z), 1, destination, rectangle, color, clip);
    }
  }
  return true;
}

// Route the actual query-create subtypes through their acquired views and return each helper result directly.
// For raster creation, release a published temporary before releasing the query interface.
bool CtFire::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type != kWnMsg_QueryOpened)
    return false;
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagDialog:
    if (!source || !source->QueryInterface(IID_UiQueryWindow, &selected))
      return false;
    {
      u8 result = DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagDebug:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return false;
    {
      u8 result = DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagMaps:
    if (!source || !source->QueryInterface(IID_UiRasterQuery, &selected))
      return false;
    {
      void *raster = nullptr;
      u8 result = static_cast<SpMapSpriteFactory *>(this)->CreateMapSprite(10029, IID_GxRasterSource, &raster);
      if (result)
        result = static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(raster));
      if (raster) {
        static_cast<ScUnknown *>(raster)->Release();
        raster = nullptr;
      }
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagStandard:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return false;
    {
      u8 result = DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  default:
    return false;
  }
}

u8 CtFire::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 CtFire::DoStandardQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u8 CtFire::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u16 CtFire::update_period = 30;
u8 CtFire::dispatch_radius = 4, CtFire::dispatch_effect = 200, CtFire::min_funding = 80, CtFire::max_flammability = 50,
   CtFire::strike_efficiency = 20;
u32 CtFire::max_strike_duration = 700;

// Read the actual fire tuning values in installed order and retain previous values when entries are missing.
// Narrow the update period before applying its zero-to30 rule; all other byte values retain their raw low byte.
bool CtFire::StaticInit(CtDefinition *) {
  ScString path, value;
  ApCompressedIni ini;
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3FireLayer.INI");
  ini.SetPath(path);
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  const char *kKeys[] = {"UpdatePeriod", "DispatchRadius", "DispatchEffect", "MinAcceptableFunding",
      "MaxAcceptableFlam", "StrikeEfficiency", "MaxStrikeDuration"};
  for (u32 i = 0; i < 7; ++i) {
    if (!ini.ReadEntry("TuningParameters", kKeys[i], &value))
      continue;
    u32 parsed = ScConvertToUint32(value.begin);
    switch (i) {
    case 0:
      update_period = u16(parsed);
      if (!update_period)
        update_period = 30;
      break;
    case 1:
      dispatch_radius = u8(parsed);
      break;
    case 2:
      dispatch_effect = u8(parsed);
      break;
    case 3:
      min_funding = u8(parsed);
      break;
    case 4:
      max_flammability = u8(parsed);
      break;
    case 5:
      strike_efficiency = u8(parsed);
      break;
    case 6:
      max_strike_duration = parsed;
      break;
    }
  }
  return true;
}

CtPoliceStationList::~CtPoliceStationList() {
  Clear();
}

// Remove the stored nodes without changing their borrowed or explicitly retained payloads.
void CtPoliceStationList::Clear() {
  while (first) {
    CtPoliceStationLink *next = first->next;
    delete first;
    first = next;
  }
}

CtJailList::~CtJailList() {
  Clear();
}

// Remove the stored nodes without changing their borrowed or explicitly retained payloads.
void CtJailList::Clear() {
  while (first) {
    CtJailLink *next = first->next;
    delete first;
    first = next;
  }
}

// Construct the shifted maps, service lists, random stream, lock and jail list in their original lifetime order.
CtPolice::CtPolice()
    : CtSizedByteMap(2), working(2), random(kScRandomSeedFromClock), hazard(nullptr), funding_percentage(100),
      coverage_samples(0), coverage_sum(0), uncovered_samples(0), bound(false), agent_tree(nullptr), start_sim(nullptr),
      animations(nullptr), city(nullptr), manipulator(nullptr), occupants(nullptr), budget(nullptr),
      department(nullptr), on_strike(false), strike_event(0), jail_capacity(0), inmate_count(0), criminals_at_large(0),
      upkeep(0), average_coverage(0), percent_coverage(0) {
}

// Destroy the jail list, lock, dispatch list, working map and station lists without implicitly invoking Shutdown.
CtPolice::~CtPolice() = default;

// Acquire the city, budget, simulation, crime and occupant services before initializing both shifted maps.
// Preserve the original virtual Shutdown on any unavailable dependency or refused map initialization.
bool CtPolice::Init(CtCity *source) {
  if (bound)
    Shutdown();
  if (!source) {
    Shutdown();
    return false;
  }
  city = ScAddRefNotNull(source);
  budget = city->BudgetLayer();
  if (!budget) {
    Shutdown();
    return false;
  }
  budget->AddRef();
  start_sim = city->StrtSimLayer();
  if (!start_sim) {
    Shutdown();
    return false;
  }
  start_sim->AddRef();
  hazard = city->CrimeLayer();
  if (!hazard) {
    Shutdown();
    return false;
  }
  hazard->AddRef();
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    Shutdown();
    return false;
  }
  occupants->AddRef();
  animations = city->AnimOccupantManager();
  if (!animations) {
    Shutdown();
    return false;
  }
  animations->AddRef();
  agent_tree = CtService::CtGetAgentTree();
  if (!agent_tree) {
    Shutdown();
    return false;
  }
  agent_tree->AddRef();
  u8 empty = 0;
  bound = Init(city, &empty);
  i32 zero = 0;
  working.Init(city, &zero);
  stations.Clear();
  jails.Clear();
  jail_capacity = inmate_count = criminals_at_large = 0;
  funded_buildings.Clear();
  upkeep = 0;
  on_strike = false;
  coverage_samples = coverage_sum = uncovered_samples = 0;
  average_coverage = percent_coverage = 0;
  if (!bound)
    Shutdown();
  return bound;
}

// Unregister and release the retained services, dispatch markers and manipulator in their original callback order.
// Guard the dispatch and map/list cleanup with the actual recursive station lock.
bool CtPolice::Shutdown() {
  if (occupants) {
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
    occupants->Release();
    occupants = nullptr;
  }
  ScRelease(&budget);
  ScRelease(&department);
  ScRelease(&hazard);
  station_lock.Lock();
  if (animations) {
    for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next) {
      void *identity = nullptr;
      if (entry->marker->QueryInterface(IID_ScOccupant, &identity)) {
        ScOccupant *occupant = static_cast<ScOccupant *>(identity);
        animations->RemoveOccupant(occupant);
        occupant->Release();
      }
      entry->marker->Release();
      if (entry->vehicle) {
        entry->vehicle->Release();
        entry->vehicle = nullptr;
      }
    }
  }
  dispatches.Clear();
  station_lock.Unlock();
  ScRelease(&start_sim);
  ScRelease(&animations);
  ScRelease(&city);
  ScRelease(&agent_tree);
  if (manipulator) {
    manipulator->Shutdown();
    manipulator->Release();
    manipulator = nullptr;
  }
  station_lock.Lock();
  working.Shutdown();
  stations.Clear();
  jails.Clear();
  funded_buildings.Clear();
  station_lock.Unlock();
  bound = false;
  return CtSizedByteMap::Shutdown();
}

// Load the daily scan cursor and service strike/coverage words from the original city-layer record.
// Stop at the first refused field, release the serial interface and close the opened record in that order.
bool CtPolice::Init(CtCity *, RsDatabaseSegment *segment) {
  RsRecord *opened = nullptr;
  if (!segment->OpenRecord(RsKey{kRsCityLayerRecordType, kCtPoliceRecordGroup, 0}, &opened))
    return false;
  bool result = false;
  void *identity = nullptr;
  if (opened->QueryInterface(IID_RsStream, &identity)) {
    RsStream *record = static_cast<RsStream *>(identity);
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new CtDailyPolice);
      manipulator->Init(city);
    }
    u8 strike = 0;
    result = record->ReadU8(&manipulator->scan_state) && record->ReadU32(&manipulator->scan_x) &&
        record->ReadU32(&manipulator->scan_z) && record->ReadU8(&strike);
    if (result) {
      on_strike = strike != 0;
      result = record->ReadU32(&strike_event) && record->ReadU32(&coverage_samples) && record->ReadU32(&coverage_sum) &&
          record->ReadU32(&uncovered_samples) && record->ReadU32(&average_coverage) &&
          record->ReadU32(&percent_coverage);
    }
    record->Release();
  }
  segment->CloseRecord(&opened);
  return result;
}

// Save the daily cursor followed by strike state and coverage words to the original city-layer record.
// Lazily create the actual daily manipulator before writing, and always close an opened record after serial release.
bool CtPolice::Save(CtCity *, RsDatabaseSegment *segment) {
  RsRecord *opened = nullptr;
  if (!segment->OpenRecordForWrite(RsKey{kRsCityLayerRecordType, kCtPoliceRecordGroup, 0}, &opened))
    return false;
  bool result = false;
  void *identity = nullptr;
  if (opened->QueryInterface(IID_RsStream, &identity)) {
    RsStream *record = static_cast<RsStream *>(identity);
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new CtDailyPolice);
      manipulator->Init(city);
    }
    result = record->WriteU8(manipulator->scan_state) && record->WriteU32(manipulator->scan_x) &&
        record->WriteU32(manipulator->scan_z) && record->WriteU8(on_strike) && record->WriteU32(strike_event) &&
        record->WriteU32(coverage_samples) && record->WriteU32(coverage_sum) && record->WriteU32(uncovered_samples) &&
        record->WriteU32(average_coverage) && record->WriteU32(percent_coverage);
    record->Release();
  }
  segment->CloseRecord(&opened);
  return result;
}

bool CtPolice::OccupantInserted(u32, ScOccupant *occupant) {
  return HandleOccupantChange(0, occupant) != 0;
}

bool CtPolice::OccupantRemoved(u32, ScOccupant *occupant) {
  return HandleOccupantChange(1, occupant) != 0;
}

// Apply the original circular contribution to the signed working grid and its visible byte map.
void CtPolice::UpdateMaps(const GeCellRect &rect, u8 radius, i16 maximum, i16 minimum) {
  if (!radius)
    return;
  u32 left = u32(rect.left), top = u32(rect.top), right = u32(rect.right), bottom = u32(rect.bottom);
  u32 center_x = left + ((right - left) >> 1), center_z = top + ((bottom - top) >> 1);
  u32 first_x = left >= radius ? left - radius : 0, first_z = top >= radius ? top - radius : 0;
  u32 last_x = right + radius, last_z = bottom + radius;
  last_x = UintMin(last_x, CellCountX() - 1);
  last_z = UintMin(last_z, CellCountZ() - 1);
  station_lock.Lock();
  for (u32 x = first_x; x <= last_x; ++x) {
    for (u32 z = first_z; z <= last_z; ++z) {
      u32 dx = center_x - x, dz = center_z - z;
      i32 squared = i32(dx * dx + dz * dz);
      i32 distance = i32(CtServiceRoundCoverage(floor(sqrt(f64(squared)))));
      if (distance <= radius) {
        i32 value;
        working.GetValue(x, z, &value);
        value = i32(u32(value) + u32(maximum - distance * (maximum - minimum) / radius));
        working.SetValue(x, z, &value);
        i32 visible = (criminals_at_large ? i32(CtServiceRoundCoverage(f64(value) * no_jail_penalty)) : value) / 16;
        u8 byte = visible > 255 ? 255 : visible < 0 ? 0 : u8(visible);
        SetValue(x, z, &byte);
      }
    }
  }
  station_lock.Unlock();
}

// Recalculate upkeep and station contributions under the real recursive lock after publishing the new funding byte.
// Reset the signed grid before traversing station and dispatch lists in their original order.
void CtPolice::SetFundingPercentage(u8 percentage) {
  funding_percentage = percentage;
  upkeep = 0;
  station_lock.Lock();
  for (CtServiceFundingLink *entry = funded_buildings.first; entry; entry = entry->next) {
    upkeep += u32(entry->upkeep->GetMonthlyUpkeep());
    entry->upkeep->SetFundingPercentage(percentage);
  }
  i32 zero = 0;
  working.SetAllCells(&zero);
  for (CtPoliceStationLink *entry = stations.first; entry; entry = entry->next) {
    if (entry->station) {
      u8 radius = entry->station->GetCoverageRadius();
      u8 maximum = entry->station->GetMaxCoverage(), minimum = entry->station->GetMinCoverage();
      GeCellRect rect{};
      entry->station->AsISC3Occupant()->GetBoundingRectInCellCoords(&rect);
      UpdateMaps(rect, radius, maximum, minimum);
    }
  }
  for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next) {
    GeCellRect rect{entry->x, entry->z, entry->x, entry->z};
    UpdateMaps(rect, dispatch_radius, dispatch_effect, dispatch_effect);
  }
  station_lock.Unlock();
}

// List entries borrow queried interfaces after their temporary holds are released, and all original callbacks stay
// direct.
u8 CtPolice::HandleOccupantChange(i32 change, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  u32 type = occupant->TypeId();
  if (agent_tree->IsAncestor(kCtAgentPoliceStation, type)) {
    void *station_identity = nullptr;
    if (!occupant->QueryInterface(IID_BdPoliceStationView, &station_identity))
      return 1;
    BdPoliceStationView *station = static_cast<BdPoliceStationView *>(station_identity);
    if (station->AsISC3Building()->GetOccupancyState() == 1) {
      void *funding_identity = nullptr;
      occupant->QueryInterface(IID_BdUpkeepView, &funding_identity);
      BdUpkeepView *funding = static_cast<BdUpkeepView *>(funding_identity);
      i32 amount = funding ? funding->GetMonthlyUpkeep() : 0;
      if (change == 0) {
        station_lock.Lock();
        if (funding) {
          funding->SetFundingPercentage(funding_percentage);
          CtServiceFundingLink **slot = &funded_buildings.first;
          while (*slot)
            slot = &(*slot)->next;
          *slot = new CtServiceFundingLink{nullptr, funding};
        }
        CtPoliceStationLink **slot = &stations.first;
        while (*slot)
          slot = &(*slot)->next;
        *slot = new CtPoliceStationLink{nullptr, station};
        station_lock.Unlock();
      }
      u8 radius = station->GetCoverageRadius();
      i16 maximum = station->GetMaxCoverage(), minimum = station->GetMinCoverage();
      if (change == 1) {
        maximum = i16(-maximum);
        minimum = i16(-minimum);
        amount = i32(0u - u32(amount));
        station_lock.Lock();
        CtPoliceStationLink **slot = &stations.first;
        while (*slot && (*slot)->station != station)
          slot = &(*slot)->next;
        if (*slot) {
          CtPoliceStationLink *removed = *slot;
          *slot = removed->next;
          delete removed;
        }
        u32 dispatch_count = 0, station_count = 0;
        for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next)
          ++dispatch_count;
        for (CtPoliceStationLink *entry = stations.first; entry; entry = entry->next)
          ++station_count;
        if (dispatch_count >= station_count + 1)
          RevokeDispatch(dispatches.last->x, dispatches.last->z);
        if (funding) {
          CtServiceFundingLink **funded = &funded_buildings.first;
          while (*funded && (*funded)->upkeep != funding)
            funded = &(*funded)->next;
          if (*funded) {
            CtServiceFundingLink *removed = *funded;
            *funded = removed->next;
            delete removed;
          }
        }
        station_lock.Unlock();
      }
      upkeep += u32(amount);
      department->SetTotalFundingNeeded(i64(i32(upkeep)));
      GeCellRect bounds{};
      occupant->GetBoundingRectInCellCoords(&bounds);
      UpdateMaps(bounds, radius, maximum, minimum);
      if (funding)
        funding->Release();
    }
    station->Release();
    return 1;
  }
  if (!agent_tree->IsAncestor(kCtAgentJail, type))
    return 1;
  void *jail_identity = nullptr;
  if (!occupant->QueryInterface(IID_BdJailView, &jail_identity))
    return 1;
  BdJailView *jail = static_cast<BdJailView *>(jail_identity);
  if (jail->AsISC3Building()->GetOccupancyState() == 1) {
    void *funding_identity = nullptr;
    occupant->QueryInterface(IID_BdUpkeepView, &funding_identity);
    BdUpkeepView *funding = static_cast<BdUpkeepView *>(funding_identity);
    i32 amount = funding ? funding->GetMonthlyUpkeep() : 0;
    if (change == 0) {
      station_lock.Lock();
      if (funding) {
        funding->SetFundingPercentage(funding_percentage);
        CtServiceFundingLink **slot = &funded_buildings.first;
        while (*slot)
          slot = &(*slot)->next;
        *slot = new CtServiceFundingLink{nullptr, funding};
      }
      CtJailLink **slot = &jails.first;
      while (*slot)
        slot = &(*slot)->next;
      *slot = new CtJailLink{nullptr, jail};
      RedistributeInmates();
      station_lock.Unlock();
    } else if (change == 1) {
      amount = i32(0u - u32(amount));
      station_lock.Lock();
      u32 released = jail->GetInmateCount();
      CtJailLink **slot = &jails.first;
      while (*slot && (*slot)->jail != jail)
        slot = &(*slot)->next;
      if (*slot) {
        CtJailLink *removed = *slot;
        *slot = removed->next;
        delete removed;
      }
      criminals_at_large += released;
      RedistributeInmates();
      if (funding) {
        CtServiceFundingLink **funded = &funded_buildings.first;
        while (*funded && (*funded)->upkeep != funding)
          funded = &(*funded)->next;
        if (*funded) {
          CtServiceFundingLink *removed = *funded;
          *funded = removed->next;
          delete removed;
        }
      }
      station_lock.Unlock();
    }
    if (amount) {
      upkeep += u32(amount);
      department->SetTotalFundingNeeded(i64(i32(upkeep)));
    }
    if (funding)
      funding->Release();
  }
  jail->Release();
  return 1;
}

// Select a dispatch origin from the live station list or independent city coordinates. Police takes the station lock
// before drawing the station index, unlike the fire service. A selected station must publish its occupant identity
// before its coordinates are consumed.
void CtPolice::SelectDispatchOrigin(u32 *x, u32 *z) {
  u32 count = 0;
  for (CtPoliceStationLink *entry = stations.first; entry; entry = entry->next)
    ++count;
  if (!count) {
    *x = random.Bounded(city->CellCountX());
    *z = random.Bounded(city->CellCountZ());
    return;
  }
  station_lock.Lock();
  u32 index = random.Bounded(count);
  CtPoliceStationLink *entry = stations.first;
  while (index--)
    entry = entry->next;
  void *identity = nullptr;
  entry->station->QueryInterface(IID_ScOccupant, &identity);
  station_lock.Unlock();
  assert(identity);
  ScOccupant *occupant = static_cast<ScOccupant *>(identity);
  occupant->GetLocationInCellCoords(x, z);
  occupant->Release();
}

// Dispatch the actual helicopter, create its map marker, and append both real identities to the service list.
// Preserve the original limit, event and coverage update ordering, including the adjusted marker X coordinate.
bool CtPolice::DispatchCrew(u32 x, u32 z) {
  u32 station_count = 0, dispatch_count = 0;
  for (CtPoliceStationLink *entry = stations.first; entry; entry = entry->next)
    ++station_count;
  for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next)
    ++dispatch_count;
  if (dispatch_count >= station_count + 1)
    RevokeDispatch(dispatches.last->x, dispatches.last->z);
  u32 origin_x, origin_z;
  SelectDispatchOrigin(&origin_x, &origin_z);
  ScOccupant *vehicle = reinterpret_cast<ScOccupant *>(start_sim->DispatchHelicopter(origin_x, origin_z, x, z));
  if (vehicle)
    vehicle->AddRef();
  if (x < CellCountX() - 1)
    ++x;
  CtService::OcBuilding *marker = new CtService::OcStaticAnimBuilding;
  marker->AddRef();
  RsKey key{kRsBuildingDataResourceType, 1406u, 13003u};
  marker->Init(&key);
  marker->SetLocationInCellCoords(x, z);
  u8 altitude = 0;
  city->DirtBag()->GetVertexAltitude(x, z, &altitude);
  marker->SetAlt(altitude);
  void *identity = nullptr;
  if (marker->QueryInterface(IID_ScOccupant, &identity)) {
    ScOccupant *occupant = static_cast<ScOccupant *>(identity);
    animations->InsertOccupant(occupant, 1, 0, true);
    occupant->Release();
  }
  station_lock.Lock();
  CtServiceDispatchLink *entry =
      new CtServiceDispatchLink{dispatches.last, nullptr, vehicle, x, z, static_cast<BdBuilding *>(marker)};
  if (dispatches.last)
    dispatches.last->next = entry;
  else
    dispatches.first = entry;
  dispatches.last = entry;
  station_lock.Unlock();
  if (ApBoxX *box = CtService::CtGetBoxX())
    box->Event(kAuEventPlayEffect, 34, 0, 0, 0);
  GeCellRect bounds{x, z, x, z};
  UpdateMaps(bounds, dispatch_radius, dispatch_effect, dispatch_effect);
  return true;
}

// Remove the matching dispatch under the real lock, revoke its vehicle and remove its marker before updating coverage.
// Capture coordinates before deleting the node so that the original police read-after-free is not reproduced.
bool CtPolice::RevokeDispatch(u32 x, u32 z) {
  station_lock.Lock();
  CtServiceDispatchLink *entry = dispatches.first;
  while (entry && (entry->x != x || entry->z != z))
    entry = entry->next;
  if (!entry) {
    station_lock.Unlock();
    return false;
  }
  if (entry->vehicle) {
    start_sim->RevokeDispatchedHelicopter(entry->vehicle);
    entry->vehicle->Release();
    entry->vehicle = nullptr;
  }
  void *identity = nullptr;
  if (entry->marker->QueryInterface(IID_ScOccupant, &identity)) {
    ScOccupant *occupant = static_cast<ScOccupant *>(identity);
    animations->RemoveOccupant(occupant);
    occupant->Release();
  }
  entry->marker->Release();
  entry->marker = nullptr;
  u32 dispatch_x = entry->x, dispatch_z = entry->z;
  if (entry->previous)
    entry->previous->next = entry->next;
  else
    dispatches.first = entry->next;
  if (entry->next)
    entry->next->previous = entry->previous;
  else
    dispatches.last = entry->previous;
  delete entry;
  GeCellRect bounds{dispatch_x, dispatch_z, dispatch_x, dispatch_z};
  i16 removal = -i16(dispatch_effect);
  UpdateMaps(bounds, dispatch_radius, removal, removal);
  station_lock.Unlock();
  return true;
}

u32 CtPolice::CellCountX() const {
  return CtSizedByteMap::CellCountX();
}

u32 CtPolice::CellCountZ() const {
  return CtSizedByteMap::CellCountZ();
}

// The byte is obtained through the qualified sized-map body, not another virtual override.
void CtPolice::GetValueSint32(u32 x, u32 z, i32 *value) const {
  u8 cell;
  CtSizedByteMap::GetValue(x, z, &cell);
  *value = i32(u32(cell) * kCtByteMapSint32Scale);
}

bool CtPolice::InBounds(u32 x, u32 z) const {
  return CtSizedByteMap::InBounds(x, z);
}

bool CtPolice::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::InBounds(x0, z0, x1, z1);
}

bool CtPolice::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::ValidRect(x0, z0, x1, z1);
}

bool CtPolice::SerialRead(RsStream *record) {
  return CtSizedByteMap::SerialRead(record);
}

bool CtPolice::SerialWrite(RsStream *record) const {
  return CtSizedByteMap::SerialWrite(record);
}

bool CtPolice::Init(CtCity *source_city, const u8 *initial) {
  return CtSizedByteMap::Init(source_city, initial);
}

void CtPolice::GetValue(u32 x, u32 z, u8 *value) const {
  CtSizedByteMap::GetValue(x, z, value);
}

void CtPolice::SetValue(u32 x, u32 z, const u8 *value) {
  CtSizedByteMap::SetValue(x, z, value);
}

void CtPolice::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  CtSizedByteMap::SetValue(x0, z0, x1, z1, value);
}

void CtPolice::SetAllCells(const u8 *value) {
  CtSizedByteMap::SetAllCells(value);
}

// Publish the actual police, city, change or query-string view before retaining through the primary map lifetime. Other
// identities delegate to the genuine sized-map query, which clears rejected outputs.
bool CtPolice::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtPoliceLayer);
  HANDLE_QUERY_INTERFACE(IID_CtPoliceLayer, CtPoliceLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  HANDLE_QUERY_INTERFACE(IID_ScQueryInfoString, ScQueryInfoString);
  return CtSizedByteMap::QueryInterface(iid, out);
}

u32 CtPolice::AddRef() {
  return CtSizedByteMap::AddRef();
}

u32 CtPolice::Release() {
  return CtSizedByteMap::Release();
}

u32 CtPolice::GetLayerType() {
  return kCtPoliceLayerType;
}

bool CtPolice::DoMessage(ScMessage *) {
  return false;
}

bool CtPolice::StaticShutdown() {
  return true;
}

bool CtPolice::Init(CtCity *, ScImporter *) {
  return true;
}

bool CtPolice::SimulationEnd() {
  return true;
}

bool CtPolice::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool CtPolice::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool CtPolice::CellChangedAll(u32) {
  return true;
}

bool CtPolice::CellChanged(u32, u32, u32) {
  return true;
}

bool CtPolice::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

bool CtPolice::DebugClassTag(ScStringView *) {
  return false;
}

bool CtPolice::DebugTypeTag(ScStringView *) {
  return false;
}

u32 CtPolice::GetInfoStringCount(u32) {
  return 0;
}

u8 CtPolice::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

u32 CtPolice::GetMapSpriteCount() {
  return 1;
}

bool CtPolice::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = 10014;
  return true;
}

u32 CtPolice::GetTotalCityPoliceUpkeep() {
  return upkeep;
}

bool CtPolice::IsOnStrike() {
  return on_strike;
}

u32 CtPolice::GetAverageCoverage() {
  return average_coverage;
}

u32 CtPolice::GetPercentCoverage() {
  return percent_coverage;
}

u32 CtPolice::GetTotalJailCapacity() {
  return jail_capacity;
}

u32 CtPolice::GetInmatePopulation() {
  return inmate_count;
}

u32 CtPolice::GetCriminalsAtLarge() {
  return criminals_at_large;
}

// Clear an active police strike before posting the original notification with its retained event word.
// An inactive strike performs no message-server lookup or field mutation.
void CtPolice::EndStrike() {
  if (on_strike) {
    on_strike = false;
    ScMessage message(kCtMsg_PoliceStrikeChanged, 0, strike_event, 0);
    CtService::CtGetMessageServer()->MessagePost(message, false);
  }
}

// While on strike, return up to the caller capacity of borrowed station occupants, retaining each published result.
// Take the lock before counting and traversing the original live station list.
bool CtPolice::GetStrikingBuildings(ScOccupant **out, u32 capacity, u32 *count) {
  if (!on_strike)
    return false;
  station_lock.Lock();
  u32 available = 0;
  for (CtPoliceStationLink *entry = stations.first; entry; entry = entry->next)
    ++available;
  if (available > capacity)
    available = capacity;
  u32 written = 0;
  for (CtPoliceStationLink *entry = stations.first; written < available && entry; entry = entry->next) {
    ScOccupant *occupant = entry->station->AsISC3Occupant();
    out[written++] = occupant;
    occupant->AddRef();
  }
  station_lock.Unlock();
  *count = written;
  return true;
}

// Capture the number of dispatches, then repeatedly revoke the live tail coordinates through the virtual method.
// Individual refusal results are ignored and the original method always reports success.
bool CtPolice::RevokeAllDispatches() {
  u32 remaining = 0;
  for (CtServiceDispatchLink *entry = dispatches.first; entry; entry = entry->next)
    ++remaining;
  while (remaining--) {
    CtServiceDispatchLink *entry = dispatches.last;
    RevokeDispatch(entry->x, entry->z);
  }
  return true;
}

// Acquire the original department budget into its live member and retain it before subscribing the change view.
// A missing occupant manager or refused budget lookup reports false without invoking the later subscription.
bool CtPolice::SimulationBegin() {
  if (!occupants || !budget->GetDepartmentBudget(kEcPoliceDepartmentId, &department))
    return false;
  department->AddRef();
  u32 subscription;
  return occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &subscription, true);
}

// Otherwise compare live funding and the actual hazard average only while a funded building exists, then square the
// live funding shortfall when both original thresholds are crossed.
u32 CtPolice::ChanceOfStrike() {
  if (on_strike)
    return 10000;
  if (funded_buildings.first && funding_percentage < min_funding && hazard->GetAverageValue() > max_crime) {
    i32 shortfall = i32(min_funding) - i32(funding_percentage);
    return u32(shortfall * shortfall);
  }
  return 0;
}

// Publish average and percentage coverage from the completed samples, retaining original DWORD multiplication.
// An empty interval publishes zero for both results, and all three running totals are cleared afterward.
u8 CtPolice::EndOfMonth() {
  u32 count = coverage_samples;
  if (count) {
    u32 total = coverage_sum;
    percent_coverage = 100u - 100u * uncovered_samples / count;
    average_coverage = total / count;
  } else {
    average_coverage = 0;
    percent_coverage = 0;
  }
  coverage_samples = 0;
  coverage_sum = 0;
  uncovered_samples = 0;
  return 1;
}

// Enable the original post-filter through the qualified common map-sprite method.
CtPoliceMapSprite::CtPoliceMapSprite(CtPolice *source, CtZoneLayer *zone) : zones(nullptr) {
  if (zone)
    zones = ScAddRefNotNull(zone);
  layer = ScAddRefNotNull(source);
  u32 rows = CtService::CtGetCity()->CellCountZ(), columns = CtService::CtGetCity()->CellCountX();
  SetId(10014);
  SetBaseSize(columns, rows);
  CtService::SpMapSprite::SetPostFilter(true);
}

CtPoliceMapSprite::~CtPoliceMapSprite() {
  ScRelease(&layer);
  ScRelease(&zones);
}

bool CtPoliceMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

// Select the optional current-city zone, construct the original raster and query the requested interface.
// A refused query originally raw-frees storage without releasing constructor holds, so detach those owners first.
bool CtPolice::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != 10014)
    return false;
  CtZoneLayer *zone = nullptr;
  if (CtService::CtHasCity())
    zone = CtService::CtGetCity()->ZoneLayer();
  CtPoliceMapSprite *sprite = new CtPoliceMapSprite(this, zone);
  if (sprite->QueryInterface(iid, out))
    return true;
  sprite->layer = nullptr;
  sprite->zones = nullptr;
  delete sprite;
  return false;
}

// Draw a cyan coverage circle and center dot for each station whose occupant identity can be acquired.
// Use the live station list and bounds, releasing each temporary occupant before reading its coverage radius.
bool CtPoliceMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  for (CtPoliceStationLink *entry = layer->stations.first; entry; entry = entry->next) {
    void *identity = nullptr;
    if (entry->station->QueryInterface(IID_ScOccupant, &identity)) {
      GeCellRect bounds{};
      static_cast<ScOccupant *>(identity)->GetBoundingRectInCellCoords(&bounds);
      static_cast<ScOccupant *>(identity)->Release();
      u32 x = (bounds.right + bounds.left) >> 1;
      u32 z = (bounds.bottom + bounds.top) >> 1;
      u8 radius = entry->station->GetCoverageRadius();
      u32 color = CtService::CtGetGraphics()->ConvertRGBValueToNative16Bit(0, 203, 255);
      DrawCircle(i32(x), i32(z), radius, 1, destination, rectangle, color, clip);
      DrawDot(i32(x), i32(z), 1, destination, rectangle, color, clip);
    }
  }
  return true;
}

// Route the actual query-create subtypes through their acquired views and return each helper result directly.
// For raster creation, release a published temporary before releasing the query interface.
bool CtPolice::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type != kWnMsg_QueryOpened)
    return false;
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagDialog:
    if (!source || !source->QueryInterface(IID_UiQueryWindow, &selected))
      return false;
    {
      u8 result = DoQueryViews(message, static_cast<UiQueryWindow *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagDebug:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return false;
    {
      u8 result = DoDebugQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagMaps:
    if (!source || !source->QueryInterface(IID_UiRasterQuery, &selected))
      return false;
    {
      void *raster = nullptr;
      u8 result = static_cast<SpMapSpriteFactory *>(this)->CreateMapSprite(10014, IID_GxRasterSource, &raster);
      if (result)
        result = static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(raster));
      if (raster) {
        static_cast<ScUnknown *>(raster)->Release();
        raster = nullptr;
      }
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  case kUiQueryTagStandard:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return false;
    {
      u8 result = DoStandardQueryViewInfo(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
      return result != 0;
    }
  default:
    return false;
  }
}

u8 CtPolice::DoQueryViews(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 CtPolice::DoStandardQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u8 CtPolice::DoDebugQueryViewInfo(ScMessage *, UiQuery *) {
  return 0;
}

u16 CtPolice::update_period = 30;
u8 CtPolice::dispatch_radius = 4, CtPolice::dispatch_effect = 200, CtPolice::min_funding = 80, CtPolice::max_crime = 50,
   CtPolice::strike_efficiency = 20;
u32 CtPolice::max_strike_duration = 700;
f32 CtPolice::max_jail_overcrowding = 2.0f, CtPolice::no_jail_penalty = 0.25f, CtPolice::conviction_rate = 0.056f,
    CtPolice::daily_jail_release_rate = 0.015f;

// Read the police INI and package in their original order, preserving old values for missing entries.
// Keep byte and word narrowing, zero-period replacement and the original binary32 scale factors for floating values.
bool CtPolice::StaticInit(CtDefinition *) {
  ScString path, value;
  ApCompressedIni ini;
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Police.INI");
  ini.SetPath(path);
  CtService::CtGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  const char *kKeys[] = {"UpdatePeriod", "DailyJailReleaseRate", "ConvictionRate", "NoJailPenalty", "DispatchRadius",
      "DispatchEffect", "MaxJailOvercrowding", "MinAcceptableFunding", "MaxAcceptableCrime", "StrikeEfficiency",
      "MaxStrikeDuration"};
  for (u32 i = 0; i < 11; ++i) {
    if (!ini.ReadEntry("TuningParameters", kKeys[i], &value))
      continue;
    u32 parsed = ScConvertToUint32(value.begin);
    switch (i) {
    case 0:
      update_period = u16(parsed);
      if (!update_period)
        update_period = 30;
      break;
    case 1:
      daily_jail_release_rate = f32(f64(parsed) * 0.001);
      break;
    case 2:
      conviction_rate = f32(f64(parsed) * 0.001);
      break;
    case 3:
      no_jail_penalty = f32(f64(parsed) * f64(0.01f));
      break;
    case 4:
      dispatch_radius = u8(parsed);
      break;
    case 5:
      dispatch_effect = u8(parsed);
      break;
    case 6:
      max_jail_overcrowding = f32(f64(parsed) * f64(0.01f));
      break;
    case 7:
      min_funding = u8(parsed);
      break;
    case 8:
      max_crime = u8(parsed);
      break;
    case 9:
      strike_efficiency = u8(parsed);
      break;
    case 10:
      max_strike_duration = parsed;
      break;
    }
  }
  return true;
}

namespace CtService {

static i32 SpAnnotationCoordinate(f64 value) {
  assert(value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return i32(u32(i64(value)));
}

SpMapSprite::SpMapSprite() : references(0), kind(0), palette(nullptr), width(0), height(0), enabled(0) {
  SpMapSprite::SetDestinationCoords(0, 0, 0, 1, 1, 0);
}

SpMapSprite::~SpMapSprite() {
  ScRelease(&palette);
}

// Publish the actual unknown/raster identity before the virtual retain, clearing the output.
bool SpMapSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpMapSprite);
  HANDLE_QUERY_INTERFACE(IID_GxRasterSource, SpMapSprite);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpMapSprite::AddRef() {
  return ++references;
}

u32 SpMapSprite::Release() {
  return ScReleaseImpl(this, &references);
}

// Set the raster kind before its dimensions through the actual virtual entries, ignoring both setter replies.
bool SpMapSprite::Init(u32 value, u32 x, u32 z) {
  SetId(value);
  SetBaseSize(x, z);
  return true;
}

u32 SpMapSprite::GetId() const {
  return kind;
}

bool SpMapSprite::SetId(u32 value) {
  kind = value;
  return true;
}

// Retain the incoming palette before inspecting and releasing the current palette, then publish the new owner.
bool SpMapSprite::SetPalette(GxPaletteView *source) {
  if (source)
    source->AddRef();
  GxPaletteView *previous = palette;
  if (previous) {
    previous->Release();
    palette = nullptr;
  }
  palette = source;
  return true;
}

// Borrow the current palette without another retain; the original reads the field at+12.
GxPaletteView *SpMapSprite::GetPalette() {
  return palette;
}

i32 SpMapSprite::GetBaseWidth() const {
  return width;
}

i32 SpMapSprite::GetBaseHeight() const {
  return width;
}

// Publish both original unsigned dimension arguments as their stored DWORD bits and report success.
bool SpMapSprite::SetBaseSize(u32 x, u32 z) {
  width = i32(x);
  height = i32(z);
  return true;
}

u8 SpMapSprite::IsPostFilter() const {
  return enabled;
}

// Require a destination, run the virtual pixel pass, and invoke completion only when that pass reports success.
bool SpMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  if (!destination)
    return false;
  if (!DrawCells(destination, rectangle, clip))
    return false;
  return DrawAnnotations(destination, rectangle, clip);
}

bool SpMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

bool SpMapSprite::DrawAnnotations(GxBuffer *, const ScRect &, ScRect *) {
  return true;
}

u8 SpMapSprite::SetPostFilter(u8 value) {
  enabled = value;
  return value;
}

void SpMapSprite::SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) {
  corner_x[1] = x1;
  corner_x[0] = x0;
  corner_z[1] = z1;
  corner_x[2] = i32(u32(x3) + u32(x1) - u32(x0));
  corner_z[0] = z0;
  corner_x[3] = x3;
  corner_z[2] = i32(u32(z3) + u32(z1) - u32(z0));
  corner_z[3] = z3;
}

// Scale columns before rows with a vertical source flip and destination clipping.
// Wrap DWORD coordinate arithmetic before signed scaling, matching the installed SUB/IMUL/IDIV sequence.
bool SpMapSprite::DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  i32 span_x = i32(u32(rectangle.right) - u32(rectangle.left)),
      span_z = i32(u32(rectangle.bottom) - u32(rectangle.top));
  i32 left = 0, top = 0, right = span_x, bottom = span_z;
  if (clip) {
    i32 x0 = i32(u32(clip->left) - u32(rectangle.left)), z0 = i32(u32(clip->top) - u32(rectangle.top));
    i32 x1 = i32(u32(clip->right) - u32(rectangle.left)), z1 = i32(u32(clip->bottom) - u32(rectangle.top));
    if (x0 > left)
      left = x0;
    if (z0 > top)
      top = z0;
    if (x1 < right)
      right = x1;
    if (z1 < bottom)
      bottom = z1;
  }
  u32 pixel = 0;
  i32 last_z = i32(u32(height) - 1);
  for (i32 x = left; x < right; ++x) {
    i32 source_x = i32(u32(x) * u32(width)) / span_x;
    for (i32 z = top; z < bottom; ++z) {
      i32 source_z = i32(u32(z) * u32(height)) / span_z;
      if (GetColor(u32(source_x), u32(last_z) - u32(source_z), &pixel))
        destination->SetPixel(u32(x) + u32(rectangle.left), u32(z) + u32(rectangle.top), pixel);
    }
  }
  return true;
}

u8 SpMapSprite::DebugInstanceTag(ScStringView *) {
  return 0;
}

u8 SpMapSprite::DebugClassTag(ScStringView *) {
  return 0;
}

u8 SpMapSprite::DebugTypeTag(ScStringView *) {
  return 0;
}

u8 SpMapSprite::SetDebugInstanceTag(const ScStringView &) {
  return 0;
}

u8 SpMapSprite::SetDebugClassTag(const ScStringView &) {
  return 0;
}

// Transform an original map point through the stored three-corner basis, preserving wrapped 32-bit products.
void SpMapSprite::TransformVector(i32 *x, i32 *z) {
  u32 x_product = u32(*x) * (u32(corner_x[1]) - u32(corner_x[0])) +
      u32(*z) * (u32(corner_x[3]) - u32(corner_x[0]));
  u32 z_product = u32(*x) * (u32(corner_z[1]) - u32(corner_z[0])) +
      u32(*z) * (u32(corner_z[3]) - u32(corner_z[0]));
  assert(width && !(width == -1 && (x_product == kScIntegerSignBit || z_product == kScIntegerSignBit)));
  i32 result_z = i32(u32(corner_z[0]) + u32(i32(z_product) / width));
  *x = i32(u32(corner_x[0]) + u32(i32(x_product) / width));
  *z = result_z;
}

// Scale an original annotation distance by the transformed horizontal basis length.
void SpMapSprite::TransformDistance(i32 *distance) {
  u32 dx = u32(corner_x[0]) - u32(corner_x[1]), dz = u32(corner_z[0]) - u32(corner_z[1]);
  i32 squared = i32(dx * dx + dz * dz);
  assert(squared >= 0 && width);
  f64 result = sqrt(f64(squared)) * f64(*distance) / f64(width);
  assert(result >= -9223372036854775808.0 && result < 9223372036854775808.0);
  *distance = i32(u32(i64(result)));
}

// Draw the original service annotation dot in transformed or city-relative coordinates.
u8 SpMapSprite::DrawDot(i32 x, i32 z, u8 radius, GxBuffer *destination, const ScRect &rectangle, u32 color, ScRect *) {
  CtCity *city = CtGetCity();
  if (!city)
    return 0;
  i32 px, pz;
  if (!enabled) {
    f32 x_fraction = f32(x) / f32(city->CellCountX());
    f32 z_fraction = f32(z) / f32(city->CellCountZ());
    f32 x_pixel = f32(destination->Width()) * x_fraction;
    f32 z_pixel = f32(destination->Height()) * z_fraction;
    z_pixel = f32(destination->Height()) - z_pixel;
    u8 locked = destination->Lock(GxLockFlags::FillCircle);
    if (locked != 1)
      return locked;
    u8 result = destination->FillCircle(SpAnnotationCoordinate(f64(x_pixel) - radius),
        SpAnnotationCoordinate(f64(z_pixel) - radius), SpAnnotationCoordinate(f64(x_pixel) + radius),
        SpAnnotationCoordinate(f64(z_pixel) + radius), color);
    destination->Unlock(GxLockFlags::FillCircle);
    return result;
  } else {
    i32 flipped = i32(u32(height) - u32(z) - 1);
    if (x < 0 || x >= width || flipped < 0 || flipped >= height)
      return 1;
    px = x;
    pz = flipped;
    TransformVector(&px, &pz);
    px = i32(u32(px) + u32(rectangle.left));
    pz = i32(u32(pz) + u32(rectangle.top));
  }
  u8 locked = destination->Lock(GxLockFlags::FillCircle);
  if (locked != 1)
    return locked;
  u8 result = destination->FillCircle(i32(u32(px) - radius), i32(u32(pz) - radius), i32(u32(px) + radius),
      i32(u32(pz) + radius), color);
  destination->Unlock(GxLockFlags::FillCircle);
  return result;
}

// Clip an annotation segment against one oriented map edge using the original wrapped integer cross products.
void SpMapSprite::ClipLeft(i32 *x0, i32 *z0, i32 *x1, i32 *z1, i32 ax, i32 az, i32 bx, i32 bz) {
  i32 dx = i32(u32(bx) - u32(ax)), dz = i32(u32(bz) - u32(az));
  i32 first = i32(u32(dx) * (u32(*z0) - u32(az)) - u32(dz) * (u32(*x0) - u32(ax)));
  i32 second = i32(u32(dx) * (u32(*z1) - u32(az)) - u32(dz) * (u32(*x1) - u32(ax)));
  if (first <= 0 && second <= 0) {
    *x1 = *x0;
    *z1 = *z0;
    return;
  }
  if (first > 0 && second > 0)
    return;
  i32 xdiff = i32(u32(*x1) - u32(*x0));
  i32 denominator = i32(u32(dz) * u32(xdiff) - u32(dx) * (u32(*z1) - u32(*z0)));
  assert(denominator);
  f64 fraction = f64(first) / denominator;
  f64 result_x = f64(xdiff) * fraction + f64(*x0);
  f64 result_z = f64(i32(u32(*z1) - u32(*z0))) * fraction + f64(*z0);
  assert(result_x >= -9223372036854775808.0 && result_x < 9223372036854775808.0 && result_z >= -9223372036854775808.0 &&
      result_z < 9223372036854775808.0);
  i32 intersect_x = i32(u32(i64(result_x))), intersect_z = i32(u32(i64(result_z)));
  if (first <= 0) {
    *x0 = intersect_x;
    *z0 = intersect_z;
  } else {
    *x1 = intersect_x;
    *z1 = intersect_z;
  }
}

// Clip the original segment against all four transformed map edges in their stored order.
void SpMapSprite::PostClip(i32 *x0, i32 *z0, i32 *x1, i32 *z1) {
  ClipLeft(x0, z0, x1, z1, corner_x[0], corner_z[0], corner_x[1], corner_z[1]);
  ClipLeft(x0, z0, x1, z1, corner_x[1], corner_z[1], corner_x[2], corner_z[2]);
  ClipLeft(x0, z0, x1, z1, corner_x[2], corner_z[2], corner_x[3], corner_z[3]);
  ClipLeft(x0, z0, x1, z1, corner_x[3], corner_z[3], corner_x[0], corner_z[0]);
}

// Test the point against the four stored oriented edges in order, excluding points lying on an edge.
// Linux establishes the original class method; SIMSERV's clip routine uses the same wrapped edge cross products.
u8 SpMapSprite::IsInside(i32 x, i32 z) {
  for (u32 edge = 0; edge < 4; ++edge) {
    u32 next = (edge + 1) & 3;
    u32 dx = u32(corner_x[next]) - u32(corner_x[edge]);
    u32 dz = u32(corner_z[next]) - u32(corner_z[edge]);
    i32 cross = i32((u32(z) - u32(corner_z[edge])) * dx - (u32(x) - u32(corner_x[edge])) * dz);
    if (cross <= 0)
      return 0;
  }
  return 1;
}

// Flip the endpoints, optionally transform and clip them, then draw a nonempty segment under the original buffer lock.
u8 SpMapSprite::DrawLine(i32 x0, i32 z0, i32 x1, i32 z1, u32 mode, GxBuffer *destination,
    const ScRect &rectangle, u32 color, ScRect *) {
  z0 = i32(u32(height) - u32(z0) - 1);
  z1 = i32(u32(height) - u32(z1) - 1);
  if (enabled) {
    TransformVector(&x0, &z0);
    TransformVector(&x1, &z1);
    PostClip(&x0, &z0, &x1, &z1);
  }
  if ((x0 != x1 || z0 != z1) && destination->Lock(GxLockFlags::Line)) {
    destination->DrawLine(i32(u32(x0) + u32(rectangle.left)), i32(u32(z0) + u32(rectangle.top)),
        i32(u32(x1) + u32(rectangle.left)), i32(u32(z1) + u32(rectangle.top)), i32(color), mode);
    destination->Unlock(GxLockFlags::Line);
  }
  return 1;
}

// Draw the original circular annotation, including its clipped eight-segment iteration in post-filter mode.
u8 SpMapSprite::DrawCircle(
    i32 x, i32 z, u8 radius, u32 mode, GxBuffer *destination, const ScRect &rectangle, u32 color, ScRect *) {
  CtCity *city = CtGetCity();
  if (!city)
    return 0;
  if (!enabled) {
    f32 x_fraction = f32(x) / f32(city->CellCountX());
    f32 z_fraction = f32(z) / f32(city->CellCountZ());
    f32 radius_fraction = f32(radius) / f32(city->CellCountX());
    f32 x_pixel = f32(destination->Width()) * x_fraction;
    f32 z_pixel = f32(destination->Height()) * z_fraction;
    z_pixel = f32(destination->Height()) - z_pixel;
    f64 extent = f64(destination->Width()) * radius_fraction;
    return destination->DrawCircle(SpAnnotationCoordinate(f64(x_pixel) - extent),
        SpAnnotationCoordinate(f64(z_pixel) - extent), SpAnnotationCoordinate(f64(x_pixel) + extent),
        SpAnnotationCoordinate(f64(z_pixel) + extent), color, mode ? mode : 1);
  }
  i32 flipped = i32(u32(height) - u32(z) - 1);
  i32 center_x = x, center_z = flipped, scaled_radius = radius;
  TransformVector(&center_x, &center_z);
  TransformDistance(&scaled_radius);
  if (destination->Lock(GxLockFlags::Line)) {
    i32 previous = 0, remaining = scaled_radius;
    if (remaining >= 0) {
      i32 next, offset;
      do {
        next = remaining - 1;
        i32 radicand = i32(u32(scaled_radius) * u32(scaled_radius) - u32(next) * u32(next));
        assert(radicand >= 0);
        offset = i32(sqrt(f64(radicand)));
        i32 segments[8][4] = {
            {i32(u32(center_x) + u32(remaining)), i32(u32(center_z) + u32(previous)),
                i32(u32(center_x) + u32(next)), i32(u32(center_z) + u32(offset))},
            {i32(u32(center_x) + u32(remaining)), i32(u32(center_z) - u32(previous)),
                i32(u32(center_x) + u32(next)), i32(u32(center_z) - u32(offset))},
            {i32(u32(center_x) - u32(remaining)), i32(u32(center_z) + u32(previous)),
                i32(u32(center_x) - u32(next)), i32(u32(center_z) + u32(offset))},
            {i32(u32(center_x) - u32(remaining)), i32(u32(center_z) - u32(previous)),
                i32(u32(center_x) - u32(next)), i32(u32(center_z) - u32(offset))},
            {i32(u32(center_x) + u32(previous)), i32(u32(center_z) + u32(remaining)),
                i32(u32(center_x) + u32(offset)), i32(u32(center_z) + u32(next))},
            {i32(u32(center_x) + u32(previous)), i32(u32(center_z) - u32(remaining)),
                i32(u32(center_x) + u32(offset)), i32(u32(center_z) - u32(next))},
            {i32(u32(center_x) - u32(previous)), i32(u32(center_z) + u32(remaining)),
                i32(u32(center_x) - u32(offset)), i32(u32(center_z) + u32(next))},
            {i32(u32(center_x) - u32(previous)), i32(u32(center_z) - u32(remaining)),
                i32(u32(center_x) - u32(offset)), i32(u32(center_z) - u32(next))}};
        for (u32 index = 0; index < 8; ++index) {
          i32 *line = segments[index];
          PostClip(&line[0], &line[1], &line[2], &line[3]);
          if (line[0] != line[2] || line[1] != line[3])
            destination->DrawLine(i32(u32(line[0]) + u32(rectangle.left)), i32(u32(line[1]) + u32(rectangle.top)),
                i32(u32(line[2]) + u32(rectangle.left)), i32(u32(line[3]) + u32(rectangle.top)), i32(color), mode);
        }
        remaining = next;
        previous = offset;
      } while (next >= offset);
    }
    destination->Unlock(GxLockFlags::Line);
  }
  return 1;
}

} // namespace CtService
