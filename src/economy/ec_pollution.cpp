#include "economy/ec_pollution.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "application/api.h"
#include "city/api.h"
#include "city/ct_scenario.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "economy/ec_history_model.h"
#include "economy/ec_module.h"
#include "economy/ec_sector.h"
#include "geometry/ge_types.h"
#include "misc/api.h"
#include "occupants/animation_manager.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/api.h"

// Garbage routing caps the unrouted remainder before adjusting the disposal supply.
enum EcGarbageRoutingLimit : i32 {
  kEcMaximumUnroutedGarbage = 0x9ffec,
};

// Pollution history uses its original series color; disposal demand is bounded symmetrically.
enum EcPollutionDisplayLimit : i32 {
  kEcPollutionGraphColor = 0x605030,
  kEcDisposalValveMagnitudeLimit = 0x77359400,
};

// Original initialized module data; the recovered tuning loader overwrites these globals.
u32 EcAirMaximum = 27000, EcWaterMaximum = 32000, EcGarbageMaximum = 27000;
u32 EcAirThreshold = 16000, EcWaterThreshold = 16000, EcGarbageThreshold = 16000;
u32 EcIncineratorCapacity = 75000, EcEnergyCapacity = 75000;
u32 EcGarbageAbductionLimit = 5000;
f32 EcGarbageScale = 0.125f;
u16 EcPollingPeriod = 30;
EcSavedModifiers EcAgentModifiers;

u32 EcPollution::GetInfoStringCount(u32) {
  return 0;
}

u8 EcPollution::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

// Construct the actual maps, lock, eight lists, modifier storage, String and polling cursor in member order.
// Leave original pre-Init totals, flags and capacities unwritten.
// Zero the disposal cleanup pointer so early service failure cannot copy the original undefined pre-init read.
EcPollution::EcPollution()
    : layer_initialized(false), owning_city(nullptr), ordinances(nullptr), zones(nullptr), traffic(nullptr),
      neighbors(nullptr), residential(nullptr), agent_types(nullptr), messages(nullptr), aging_treatment(0),
      aging_incinerator(0), aging_recycling(0), average_air(0), average_water(0), accumulated_air(0),
      accumulated_water(0), water_before_treatment(0), water_treated(0), treatment_denominator(0),
      treatment_numerator(0), history_model(nullptr), average_garbage(0), disposal(nullptr), total_uncollected(0),
      garbage_abducted(0) {
  cell_x = 0;
  cell_z = 0;
}

EcPollution::~EcPollution() {
  Shutdown();
}

bool EcPollution::IsSaltWater(u32 x, u32 z) const {
  return (published->columns[x >> coordinate_shift][z >> coordinate_shift] & kEcSaltWaterFlag) != 0;
}

bool EcPollution::IsRadioactive(u32 x, u32 z) const {
  return (published->columns[x >> coordinate_shift][z >> coordinate_shift] >> kEcRadioactiveShift) != 0;
}

void EcPollution::GetAirValue(u32 x, u32 z, u32 *value) const {
  *value = (published->columns[x >> coordinate_shift][z >> coordinate_shift] >> kEcAirPollutionShift) &
      kEcPollutionSampleMask;
}

void EcPollution::GetWaterValue(u32 x, u32 z, u32 *value) const {
  *value = published->columns[x >> coordinate_shift][z >> coordinate_shift] & kEcPollutionSampleMask;
}

// Call the original virtual value getter before reading the live threshold.
bool EcPollution::IsAirPolluted(u32 x, u32 z) const {
  u32 value = z;
  GetAirValue(x, z, &value);
  return value >= EcAirThreshold;
}

// Call the original virtual value getter before reading the live threshold.
bool EcPollution::IsWaterPolluted(u32 x, u32 z) const {
  u32 value = z;
  GetWaterValue(x, z, &value);
  return value >= EcWaterThreshold;
}

// Call the original virtual value getter before reading the live threshold.
bool EcPollution::IsGarbageAccumulated(u32 x, u32 z) const {
  u32 value = z;
  GetGarbageValue(x, z, &value);
  return value >= EcGarbageThreshold;
}

bool EcPollution::IsGarbageUncollected() {
  return garbage_uncollected;
}

i32 EcPollution::GetTotalGarbageUncollected() {
  return total_uncollected;
}

i32 EcPollution::GetAverageAirValue() const {
  return average_air;
}

i32 EcPollution::GetAverageWaterValue() const {
  return average_water;
}

i16 EcPollution::GetAverageGarbageValue() const {
  return static_cast<i16>(average_garbage);
}

i32 EcPollution::GetMaxExpectedAirValue() const {
  return static_cast<i32>(EcAirMaximum);
}

i32 EcPollution::GetMaxExpectedWaterValue() const {
  return static_cast<i32>(EcWaterMaximum);
}

i32 EcPollution::GetMaxExpectedGarbageValue() const {
  return static_cast<i32>(EcGarbageMaximum);
}

i32 EcPollution::GetTotalLandfillCapacity() {
  return static_cast<i32>(kEcLandfillCellCapacity * landfill_cells);
}

i32 EcPollution::GetDailyIncineratorCapacity() {
  return daily_incinerator;
}

i32 EcPollution::GetDailyWasteToEnergyCapacity() {
  return daily_energy;
}

i32 EcPollution::GetAgingWaterTreatmentCount() {
  return aging_treatment;
}

f32 EcPollution::GetGarbageScalingFactor() {
  return EcGarbageScale;
}

i64 EcPollution::GetTotalGarbageProduced() {
  return static_cast<i64>(totals[0]);
}

i64 EcPollution::GetTotalGarbageImported() {
  return static_cast<i64>(totals[1]);
}

i64 EcPollution::GetTotalGarbageExported() {
  return static_cast<i64>(totals[2]);
}

i64 EcPollution::GetTotalGarbageRecycled() {
  return static_cast<i64>(totals[3]);
}

i64 EcPollution::GetTotalGarbageIncinerated() {
  return static_cast<i64>(totals[4]);
}

i64 EcPollution::GetTotalGarbageConvertedToEnergy() {
  return static_cast<i64>(totals[5]);
}

i64 EcPollution::GetTotalGarbageLandfilled() {
  return static_cast<i64>(totals[6]);
}

i32 EcPollution::GetGarbageBuildingCapacity(u32 kind) {
  if (kind == kCtAgentIncinerator)
    return static_cast<i32>(EcIncineratorCapacity);
  if (kind == kCtAgentWasteToEnergy)
    return static_cast<i32>(EcEnergyCapacity);
  return 0;
}

void EcPollution::GarbagePutInLandfill(u32 quantity) {
  current[6] += quantity;
}

// Convert the recycling fraction with the original binary64 magic-add rounding and low-DWORD bias.
i32 EcPollution::GetRecyclingCenterEffect() {
  volatile f64 product = static_cast<f64>(reduction_fraction) * 100.0;
  volatile f64 addition = product + kScDoubleIntegerBias;
  f64 stored = addition;
  u64 bits;
  memcpy(&bits, &stored, sizeof(bits));
  return static_cast<i32>(static_cast<u32>(bits) + kScDoubleIntegerLowWordBias);
}

// Change the published flag only when it differs, preserving unrelated bits. Post after the live virtual map write.
void EcPollution::SetIsSaltWater(u32 x, u32 z, bool enabled) {
  if (enabled == IsSaltWater(x, z))
    return;
  u32 packed = published->columns[x >> coordinate_shift][z >> coordinate_shift];
  packed = enabled ? packed | kEcSaltWaterFlag : packed & ~kEcSaltWaterFlag;
  SetValue(x, z, &packed);
  ScMessage message = {
      kEcMsg_SaltWaterChanged, reinterpret_cast<uptr>(static_cast<EcLayer *>(this)), (z & 0xffffu) | (x << 16), 0};
  messages->MessagePost(message, false);
}

// Change the published flag only when it differs, preserving unrelated bits. Post after the live virtual map write.
void EcPollution::SetIsRadioactive(u32 x, u32 z, bool enabled) {
  if (enabled == IsRadioactive(x, z))
    return;
  u32 packed = published->columns[x >> coordinate_shift][z >> coordinate_shift];
  packed = enabled ? packed | kEcRadioactiveFlag : packed & ~kEcRadioactiveFlag;
  SetValue(x, z, &packed);
  ScMessage message = {
      kEcMsg_RadioactivityChanged, reinterpret_cast<uptr>(static_cast<EcLayer *>(this)), (z & 0xffffu) | (x << 16), 0};
  messages->MessagePost(message, false);
}

// Change the published value only when it differs, preserving unrelated bits. Post after the live virtual map write.
void EcPollution::SetAirValue(u32 x, u32 z, u32 value) {
  u32 previous;
  GetAirValue(x, z, &previous);
  if (value > kEcPollutionSampleMaximum)
    value = kEcPollutionSampleMaximum;
  if (value == previous)
    return;
  u32 packed = (value << kEcAirPollutionShift) |
      (published->columns[x >> coordinate_shift][z >> coordinate_shift] & kEcPollutionWithoutAirMask);
  SetValue(x, z, &packed);
  ScMessage message = {
      kEcMsg_AirPollutionChanged, reinterpret_cast<uptr>(static_cast<EcLayer *>(this)), (z & 0xffffu) | (x << 16), 0};
  messages->MessagePost(message, false);
}

// Change the published value only when it differs, preserving unrelated bits. Post after the live virtual map write.
void EcPollution::SetWaterValue(u32 x, u32 z, u32 value) {
  u32 previous;
  GetWaterValue(x, z, &previous);
  if (value > kEcPollutionSampleMaximum)
    value = kEcPollutionSampleMaximum;
  if (value == previous)
    return;
  u32 packed =
      value | (published->columns[x >> coordinate_shift][z >> coordinate_shift] & kEcPollutionWithoutWaterMask);
  SetValue(x, z, &packed);
  ScMessage message = {
      kEcMsg_WaterPollutionChanged, reinterpret_cast<uptr>(static_cast<EcLayer *>(this)), (z & 0xffffu) | (x << 16), 0};
  messages->MessagePost(message, false);
}

i32 EcPollution::GetAgingIncineratorCount() {
  return aging_incinerator;
}

i32 EcPollution::GetAgingRecyclingCenterCount() {
  return aging_recycling;
}

void EcPollution::GetGarbageValue(u32 x, u32 z, u32 *value) const {
  u16 packed = static_cast<u16>(z);
  garbage.GetValue(x, z, &packed);
  *value = packed & kEcGarbageAmountMask;
}

bool EcPollution::IsActiveLandfill(u32 x, u32 z) {
  u16 packed = static_cast<u16>(z >> 16);
  garbage.GetValue(x, z, &packed);
  return (packed >> kEcActiveLandfillShift) != 0;
}

// Clamp garbage to fifteen bits while preserving the active-landfill flag.
void EcPollution::SetGarbageValue(u32 x, u32 z, u32 value) {
  u16 packed;
  garbage.GetValue(x, z, &packed);
  if (value > kEcGarbageAmountMaximum)
    value = kEcGarbageAmountMaximum;
  if (packed != value) {
    packed = static_cast<u16>(value | (packed & kEcActiveLandfillFlag));
    garbage.SetValue(x, z, &packed);
  }
}

bool EcPollution::Init(CtCity *city, const u32 *fill) {
  return EcDwordMap::Init(city, fill);
}

void EcPollution::SetIsActiveLandfill(u32 x, u32 z, bool enabled) {
  u8 zone = static_cast<u8>(z >> 24);
  zones->GetValue(x, z, &zone);
  if (enabled && zone != 0x11)
    return;
  bool active = IsActiveLandfill(x, z);
  if (enabled == active)
    return;
  u16 packed;
  garbage.GetValue(x, z, &packed);
  if (enabled) {
    packed |= kEcActiveLandfillFlag;
    ++landfill_cells;
  } else {
    packed &= kEcWithoutActiveLandfillMask;
    if (landfill_cells)
      --landfill_cells;
  }
  garbage.SetValue(x, z, &packed);
}

bool EcPollution::GetManipulator(const CtAgentType &, CtLayerManipulator **manipulator, i32 *) {
  *manipulator = nullptr;
  return true;
}

i32 EcPollution::GetAvailableLandfillCapacity() {
  return disposal->QueryDemandValue();
}

u32 EcPollution::CellCountX() const {
  return EcDwordMap::CellCountX();
}

u32 EcPollution::CellCountZ() const {
  return EcDwordMap::CellCountZ();
}

void EcPollution::GetValueSint32(u32 x, u32 z, i32 *value) const {
  EcDwordMap::GetValueSint32(x, z, value);
}

bool EcPollution::InBounds(u32 x, u32 z) const {
  return EcDwordMap::InBounds(x, z);
}

bool EcPollution::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return EcDwordMap::InBounds(x0, z0, x1, z1);
}

bool EcPollution::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return EcDwordMap::ValidRect(x0, z0, x1, z1);
}

void EcPollution::GetValue(u32 x, u32 z, u32 *value) const {
  EcDwordMap::GetValue(x, z, value);
}

void EcPollution::SetValue(u32 x, u32 z, const u32 *value) {
  EcDwordMap::SetValue(x, z, value);
}

void EcPollution::SetAllCells(const u32 *value) {
  EcDwordMap::SetAllCells(value);
}

void EcPollution::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u32 *value) {
  EcDwordMap::SetValue(x0, z0, x1, z1, value);
}

// Read the previous calendar month's produced garbage total, wrapping January to December.
i64 EcPollution::GetTotalGarbageProducedLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[0][month]);
}

// Read the previous calendar month's imported garbage total, wrapping January to December.
i64 EcPollution::GetTotalGarbageImportedLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[1][month]);
}

// Read the previous calendar month's exported garbage total, wrapping January to December.
i64 EcPollution::GetTotalGarbageExportedLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[2][month]);
}

// Read the previous calendar month's recycled garbage total, wrapping January to December.
i64 EcPollution::GetTotalGarbageRecycledLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[3][month]);
}

// Read the previous calendar month's incinerated garbage total, wrapping January to December.
i64 EcPollution::GetTotalGarbageIncineratedLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[4][month]);
}

i64 EcPollution::GetTotalGarbageConvertedToEnergyLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[5][month]);
}

// Read the previous calendar month's landfilled garbage total, wrapping January to December.
i64 EcPollution::GetTotalGarbageLandfilledLastMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  month = month == 1 ? 11 : month - 2;
  return static_cast<i64>(history[6][month]);
}

// Saved modifiers are cleared after unlocking; primary-map retirement follows the separate layer flag.
bool EcPollution::Shutdown() {
  if (!layer_initialized)
    return true;
  ScRelease(&owning_city);
  ScRelease(&messages);
  if (zones)
    zones->Release();
  zones = nullptr;
  if (residential)
    residential->Release();
  residential = nullptr;
  if (neighbors)
    neighbors->Release();
  neighbors = nullptr;
  if (ordinances)
    ordinances->Release();
  ordinances = nullptr;
  if (traffic)
    traffic->Release();
  traffic = nullptr;
  if (agent_types)
    agent_types->Release();
  agent_types = nullptr;
  first_input.Shutdown();
  second_input.Shutdown();
  garbage_produced.Shutdown();
  garbage.Shutdown();
  if (disposal)
    disposal->Release();
  disposal = nullptr;
  if (history_model)
    history_model->Release();
  history_model = nullptr;
  occupant_lock.Lock();
  for (u32 index = 0; index < 4; ++index) {
    for (ScUnknown *entry : tracked[index])
      entry->Release();
    tracked[index].clear();
  }
  occupant_lock.Unlock();
  saved_modifiers.clear();
  layer_initialized = false;
  return EcDwordMap::Shutdown();
}

i32 EcPollution::MinimumExecutionInterval() {
  return 2;
}

bool EcPollution::IsAgentLocking() {
  return true;
}

u32 EcPollution::TypeId() const {
  return 0;
}

// Shared zero-return body 1000A887 also supplies the agent-type pointer slot.
CtAgentType *EcPollution::AgentType() {
  return nullptr;
}

bool EcPollution::SetAgentType(CtAgentType *) {
  return false;
}

bool EcPollution::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcPollution::DebugClassTag(ScStringView *) {
  return false;
}

bool EcPollution::DebugTypeTag(ScStringView *) {
  return false;
}

bool EcPollution::DoMessage(ScMessage *) {
  return false;
}

bool EcPollution::StaticShutdown() {
  EcAgentModifiers.clear();
  return true;
}

u32 EcPollution::GetLayerType() {
  return kEcPollutionLayerType;
}

// Accumulate one surrounding region in the original X-major order, using the nearest footprint point.
// Integer falloff is evaluated independently for each signed pollution map, including zero-valued coefficients.
void EcPollution::ApplyModifierHalo(
    const GeCellRect &bounds, const EcModifier &modifier, i32 x0, i32 z0, i32 x1, i32 z1) {
  for (i32 x = x0; x <= x1; ++x)
    for (i32 z = z0; z <= z1; ++z) {
      i32 dx = x < static_cast<i32>(bounds.left) ? static_cast<i32>(bounds.left) - x
          : x > static_cast<i32>(bounds.right)   ? x - static_cast<i32>(bounds.right)
                                                 : 0;
      i32 dz = z < static_cast<i32>(bounds.top) ? static_cast<i32>(bounds.top) - z
          : z > static_cast<i32>(bounds.bottom) ? z - static_cast<i32>(bounds.bottom)
                                                : 0;
      i32 distance = dx >= dz ? dx + dz / 2 : dz + dx / 2, value;
      if (distance <= modifier.air_radius) {
        first_input.GetValue(x, z, &value);
        value += modifier.air * (modifier.air_radius - distance + 1) / (modifier.air_radius + 1);
        first_input.SetValue(x, z, &value);
      }
      if (distance <= modifier.water_radius) {
        second_input.GetValue(x, z, &value);
        value += modifier.water * (modifier.water_radius - distance + 1) / (modifier.water_radius + 1);
        second_input.SetValue(x, z, &value);
      }
    }
}

// Apply the footprint and eight halo regions after the occupant's modifier has been resolved.
// Garbage uses only observable WORD arithmetic; discarded uninitialized upper halves are not reproduced.
void EcPollution::ApplyModifier(const GeCellRect &bounds, const EcModifier &modifier) {
  i32 left = static_cast<i32>(bounds.left), top = static_cast<i32>(bounds.top);
  i32 right = static_cast<i32>(bounds.right), bottom = static_cast<i32>(bounds.bottom);
  occupant_lock.Lock();
  for (i32 x = left; x <= right; ++x)
    for (i32 z = top; z <= bottom; ++z) {
      i32 value;
      first_input.GetValue(x, z, &value);
      value += modifier.air;
      first_input.SetValue(x, z, &value);
      second_input.GetValue(x, z, &value);
      value += modifier.water;
      second_input.SetValue(x, z, &value);
      i16 produced;
      garbage_produced.GetValue(x, z, &produced);
      u16 sum = static_cast<u16>(static_cast<u16>(produced) + static_cast<u16>(modifier.garbage));
      produced = (sum & 0x8000u) ? 0 : static_cast<i16>(sum);
      garbage_produced.SetValue(x, z, &produced);
    }
  occupant_lock.Unlock();
  i32 radius = modifier.air_radius > modifier.water_radius ? modifier.air_radius : modifier.water_radius;
  if (!radius)
    return;
  i32 x0 = left - radius, z0 = top - radius, x1 = right + radius, z1 = bottom + radius;
  if (x0 < 0)
    x0 = 0;
  if (z0 < 0)
    z0 = 0;
  if (x1 >= static_cast<i32>(CellCountX()))
    x1 = static_cast<i32>(CellCountX()) - 1;
  if (z1 >= static_cast<i32>(CellCountZ()))
    z1 = static_cast<i32>(CellCountZ()) - 1;
  occupant_lock.Lock();
  ApplyModifierHalo(bounds, modifier, x0, z0, left - 1, top - 1);
  ApplyModifierHalo(bounds, modifier, left, z0, right, top - 1);
  ApplyModifierHalo(bounds, modifier, right + 1, z0, x1, top - 1);
  ApplyModifierHalo(bounds, modifier, x0, top, left - 1, bottom);
  ApplyModifierHalo(bounds, modifier, right + 1, top, x1, bottom);
  ApplyModifierHalo(bounds, modifier, x0, bottom + 1, left - 1, z1);
  ApplyModifierHalo(bounds, modifier, left, bottom + 1, right, z1);
  ApplyModifierHalo(bounds, modifier, right + 1, bottom + 1, x1, z1);
  occupant_lock.Unlock();
}

// Resolve the live occupant modifier after occupancy and facility classification, then apply its signed contributions.
bool EcPollution::ChangeOccupant(u32 operation, ScOccupant *occupant) {
  if (!occupant)
    return false;
  if (operation > 1)
    return true;
  void *building;
  if (occupant->QueryInterface(IID_BdBuilding, &building)) {
    bool occupied = static_cast<BdBuilding *>(building)->GetOccupancyState() == 1;
    static_cast<BdBuilding *>(building)->Release();
    if (!occupied)
      return true;
  }
  u32 identifier = occupant->TypeId(), kind = 0;
  if (agent_types->IsAncestor(kCtAgentGarbage, identifier)) {
    if (agent_types->IsAncestor(kCtAgentRecycling, identifier))
      kind = kCtAgentRecycling;
    else if (agent_types->IsAncestor(kCtAgentIncinerator, identifier))
      kind = kCtAgentIncinerator;
  } else {
    if (agent_types->IsAncestor(kCtAgentWasteToEnergy, identifier))
      kind = kCtAgentWasteToEnergy;
    else if (agent_types->IsAncestor(kCtAgentWaterTreatment, identifier))
      kind = kCtAgentWaterTreatment;
  }
  if (kind)
    TrackFacility(occupant, operation, kind);
  occupant_lock.Lock();
  EcModifier modifier = {};
  const EcModifier *found = EcFindModifier(EcAgentModifiers, identifier);
  if (found) {
    modifier = *found;
  } else {
    // The SDK exposes intptr_t here; installed calls use the returned handle as the actual city-agent type interface.
    CtAgentType *node = agent_types->GetNode(identifier);
    if (node) {
      u32 ancestor = node->Id();
      while (ancestor != kCtAgentOccupant) {
        node = node->Parent();
        if (!node)
          break;
        ancestor = node->Id();
        found = EcFindModifier(EcAgentModifiers, ancestor);
        if (found) {
          modifier = *found;
          break;
        }
      }
    }
    EcAgentModifiers[identifier] = modifier;
  }
  occupant_lock.Unlock();
  if (operation == 1) {
    modifier.air = static_cast<i16>(-modifier.air);
    modifier.water = static_cast<i16>(-modifier.water);
    modifier.garbage = static_cast<i16>(-modifier.garbage);
  }
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  ApplyModifier(bounds, modifier);
  return true;
}

bool EcPollution::OccupantInserted(u32, ScOccupant *occupant) {
  return ChangeOccupant(0, occupant);
}

bool EcPollution::OccupantRemoved(u32, ScOccupant *occupant) {
  return ChangeOccupant(1, occupant);
}

bool EcPollution::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool EcPollution::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool EcPollution::CellChangedAll(u32) {
  return true;
}

bool EcPollution::CellChanged(u32, u32, u32) {
  return true;
}

bool EcPollution::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

// Maintain the actual private facility lists and counters while preserving external callback ordering.
void EcPollution::TrackFacility(ScOccupant *occupant, u32 operation, u32 kind) {
  if (kind == kCtAgentWaterTreatment) {
    TrackWaterTreatment(occupant, operation);
    return;
  }
  if (kind == kCtAgentIncinerator || kind == kCtAgentWasteToEnergy) {
    void *queried;
    if (!occupant->QueryInterface(IID_EcCombustion, &queried))
      return;
    occupant_lock.Lock();
    EcTrackedList *list = &tracked[kind == kCtAgentIncinerator ? 1 : 2];
    if (operation == 1) {
      auto node = list->begin();
      while (node != list->end() && *node != static_cast<EcCombustion *>(queried))
        ++node;
      if (node != list->end()) {
        if (static_cast<EcCombustion *>(queried)->IsAging() && aging_incinerator)
          --aging_incinerator;
        (*node)->Release();
        list->erase(node);
      }
    } else if (operation == 0) {
      static_cast<EcCombustion *>(queried)->AddRef();
      list->push_back(static_cast<EcCombustion *>(queried));
      if (static_cast<EcCombustion *>(queried)->IsAging())
        ++aging_incinerator;
    }
    occupant_lock.Unlock();
    static_cast<EcCombustion *>(queried)->Release();
  } else if (kind == kCtAgentRecycling) {
    void *queried;
    if (!occupant->QueryInterface(IID_EcRecycling, &queried))
      return;
    occupant_lock.Lock();
    EcTrackedList *list = &tracked[3];
    if (operation == 1) {
      auto node = list->begin();
      while (node != list->end() && *node != static_cast<EcRecycling *>(queried))
        ++node;
      if (node != list->end()) {
        if (static_cast<EcRecycling *>(queried)->IsAging() && aging_recycling)
          --aging_recycling;
        (*node)->Release();
        list->erase(node);
      }
    } else if (operation == 0) {
      static_cast<EcRecycling *>(queried)->AddRef();
      list->push_front(static_cast<EcRecycling *>(queried));
      if (static_cast<EcRecycling *>(queried)->IsAging())
        ++aging_recycling;
    }
    occupant_lock.Unlock();
    static_cast<EcRecycling *>(queried)->Release();
  }
}

// List removal releases the live stored view before unlinking, then retires the query temporary after unlocking.
void EcPollution::TrackWaterTreatment(ScOccupant *occupant, u32 operation) {
  void *queried;
  if (!occupant->QueryInterface(IID_BdTreatment, &queried))
    return;
  occupant_lock.Lock();
  EcTrackedList *list = &tracked[0];
  if (operation == 1) {
    auto node = list->begin();
    while (node != list->end() && *node != static_cast<BdTreatment *>(queried))
      ++node;
    if (node != list->end()) {
      if (static_cast<BdTreatment *>(queried)->IsAging() && aging_treatment)
        --aging_treatment;
      (*node)->Release();
      list->erase(node);
    }
  } else if (operation == 0) {
    if (static_cast<BdTreatment *>(queried)->IsAging())
      ++aging_treatment;
    static_cast<BdTreatment *>(queried)->AddRef();
    list->push_back(static_cast<BdTreatment *>(queried));
  }
  occupant_lock.Unlock();
  static_cast<BdTreatment *>(queried)->Release();
}

// Reorder live recycling interfaces by descending effectiveness, then weight complete capacity contributions.
// Preserve binary32 stores and separate binary64 operations from7FD4 without recreating its generic list machinery.
void EcPollution::UpdateRecycling() {
  f32 population = static_cast<f32>(static_cast<u32>(residential->GetPopulation()));
  EcTrackedList sorted;
  u32 count = static_cast<u32>(tracked[3].size());
  f32 weighted = 0, total = 0;
  if (count) {
    occupant_lock.Lock();
    aging_recycling = 0;
    for (auto node = tracked[3].begin(); node != tracked[3].end(); ++node) {
      u32 score = static_cast<EcRecycling *>(*node)->GetEffectiveness();
      auto position = sorted.begin();
      while (position != sorted.end() && static_cast<EcRecycling *>(*position)->GetEffectiveness() > score)
        ++position;
      sorted.insert(position, *node);
      if (static_cast<EcRecycling *>(*node)->IsAging())
        ++aging_recycling;
    }
    tracked[3] = sorted;
    auto node = tracked[3].begin();
    while (node != tracked[3].end() && total < population) {
      u32 amount = static_cast<EcRecycling *>(*node)->GetCapacity();
      EcRecycling *object = static_cast<EcRecycling *>(*node);
      f32 rounded_amount = static_cast<f32>(amount);
      total = static_cast<f32>(static_cast<f64>(amount) + total);
      u32 score = object->GetEffectiveness();
      ++node;
      volatile f64 product = static_cast<f64>(score) * rounded_amount;
      product = product * static_cast<f64>(0.01f);
      product = product + weighted;
      weighted = static_cast<f32>(product);
    }
    occupant_lock.Unlock();
  }
  reduction_fraction =
      total ? static_cast<f32>(static_cast<f64>(weighted) / (total < population ? population : total)) : 0;
}

// Publish the completed cycle's averages and treatment ratio before resetting its accumulators.
void EcPollution::FinalizeCycle() {
  UpdateRecycling();
  u32 marked = static_cast<u32>(total_uncollected), affected = affected_cells;
  garbage_uncollected = marked != 0;
  if (affected) {
    average_air = static_cast<i32>(accumulated_air) > 0 ? static_cast<i32>(accumulated_air / affected) : 0;
    average_water = static_cast<i32>(accumulated_water) > 0 ? static_cast<i32>(accumulated_water / affected) : 0;
    average_garbage = static_cast<u16>(marked / affected);
  } else {
    average_air = 0;
    average_water = 0;
    average_garbage = 0;
  }
  u32 treated = water_treated;
  treatment_denominator = water_before_treatment;
  affected_cells = 0;
  water_before_treatment = 0;
  accumulated_water = 0;
  water_treated = 0;
  accumulated_air = 0;
  treatment_numerator = treated;
}

// Route actual disposal through energy and incinerator reserves, landfill, an export deal and the abduction allowance.
// Keep the original mutable reserve argument order; delivered quantities update their distinct current-month counters.
i32 EcPollution::RouteGarbage(i32 quantity, i32 *incinerator, i32 *energy) {
  if (quantity <= 0)
    return 0;
  i32 remaining = quantity, delivered = 0;
  if (*energy) {
    i32 amount;
    if (remaining > *energy) {
      remaining -= *energy;
      amount = *energy;
      *energy = 0;
    } else {
      amount = remaining;
      *energy -= remaining;
      remaining = 0;
    }
    current[5] += static_cast<i64>(amount);
    delivered = amount;
  }
  if (remaining > 0 && *incinerator) {
    i32 amount;
    if (remaining > *incinerator) {
      amount = *incinerator;
      remaining -= amount;
      *incinerator = 0;
    } else {
      amount = remaining;
      *incinerator -= remaining;
      remaining = 0;
    }
    current[4] += static_cast<i64>(amount);
    delivered += amount;
  }
  if (remaining > 0) {
    i32 amount = disposal->QueryValveValue();
    if (amount > 0) {
      if (amount < remaining) {
        disposal->AddToSupplyValue(amount);
        remaining -= amount;
      } else {
        amount = remaining;
        disposal->AddToSupplyValue(amount);
        remaining = 0;
      }
      delivered += amount;
    }
    if (remaining > 0) {
      CtDealRecord deal;
      if (static_cast<CtNeighborsLayer *>(neighbors)->GetDeal(kCtDealExportGarbage, &deal)) {
        current[2] += static_cast<i64>(remaining);
        delivered += remaining;
        remaining = 0;
      }
      if (remaining > 0) {
        i32 allowance = static_cast<i32>(EcGarbageAbductionLimit - garbage_abducted);
        if (allowance > 0) {
          i32 abducted = remaining > allowance ? allowance : remaining;
          garbage_abducted += static_cast<u32>(abducted);
          delivered += abducted;
        }
      }
    }
  }
  return delivered;
}

u32 EcPollution::AddRef() {
  return EcDwordMap::AddRef();
}

u32 EcPollution::Release() {
  return EcDwordMap::Release();
}

bool EcPollution::SerialRead(RsStream *record) {
  return EcDwordMap::SerialRead(record);
}

bool EcPollution::SerialWrite(RsStream *record) const {
  return EcDwordMap::SerialWrite(record);
}

// Save the map, accounting, traversal and tracked facility coordinates through the original version-three frame.
// History checks only each month's seventh write; other failures mark the owned record for deferred abort.
bool EcPollution::Save(CtCity *city, RsDatabaseSegment *segment) {
  EcArchiveFrame frame(city, segment, RsKey{kRsCityLayerRecordType, kEcPollutionClassId, 0}, 3, 0);
  u8 result = frame.good;
  RsStream *record = frame.GetStream();
  if (result)
    result = static_cast<EcDwordMap *>(this)->SerialWrite(record);
  if (result)
    result = disposal->SerialWrite(city, record);
  if (result)
    result = garbage.SerialWrite(record);
  for (u32 category = 0; result && category < 7; ++category)
    result = record->WriteI64(static_cast<i64>(totals[category]));
  result = result && record->WriteU32(static_cast<u32>(average_air)) &&
      record->WriteU32(static_cast<u32>(average_water)) && record->WriteU16(average_garbage) &&
      record->WriteI32(static_cast<i32>(accumulated_air)) && record->WriteI32(static_cast<i32>(accumulated_water)) &&
      record->WriteU32(static_cast<u32>(total_uncollected)) && record->WriteU32(affected_cells);
  if (result)
    result = history_model->SerialWrite(city, record);
  for (u32 category = 0; result && category < 7; ++category)
    result = record->WriteI64(static_cast<i64>(current[category]));
  for (u32 month = 0; result && month < 12; ++month) {
    for (u32 category = 0; category < 6; ++category)
      record->WriteI64(static_cast<i64>(history[category][month]));
    result = record->WriteI64(static_cast<i64>(history[6][month]));
  }
  result = result && record->WriteU8(garbage_uncollected) && record->WriteU32(static_cast<u32>(daily_incinerator)) &&
      record->WriteU32(static_cast<u32>(daily_energy)) && record->WriteF32(reduction_fraction) &&
      record->WriteU32(garbage_abducted) && record->WriteU32(cell_x) && record->WriteU32(cell_z) &&
      record->WriteI32(static_cast<i32>(water_before_treatment)) &&
      record->WriteI32(static_cast<i32>(treatment_denominator)) && record->WriteI32(static_cast<i32>(water_treated)) &&
      record->WriteI32(static_cast<i32>(treatment_numerator)) && cursor.SerialWrite(city, record) &&
      record->WriteU32(landfill_cells);
  if (result)
    result = second_input.SerialWrite(record);
  if (result)
    result = first_input.SerialWrite(record);
  if (result)
    result = garbage_produced.SerialWrite(record);
  if (result) {
    i32 remaining = static_cast<i32>(EcAgentModifiers.size());
    result = record->WriteI32(remaining);
    auto node = EcAgentModifiers.begin();
    while (result && node != EcAgentModifiers.end() && remaining > 0) {
      result = record->WriteU32(node->first);
      result = result && record->WriteI32(node->second.air) && record->WriteI32(node->second.water) &&
          record->WriteU8(node->second.air_radius) && record->WriteU8(node->second.water_radius) &&
          record->WriteI32(node->second.garbage);
      --remaining;
      ++node;
    }
  }
  for (u32 kind = 0; result && kind < 4; ++kind) {
    auto node = tracked[kind].begin();
    u32 count = static_cast<u32>(tracked[kind].size());
    result = record->WriteI32(static_cast<i32>(count));
    while (result && node != tracked[kind].end()) {
      if (*node) {
        ScOccupant *occupant;
        if (kind == 0)
          occupant = static_cast<BdTreatment *>(*node)->GetOccupant();
        else if (kind == 3)
          occupant = static_cast<EcRecycling *>(*node)->GetOccupant();
        else
          occupant = static_cast<EcCombustion *>(*node)->GetOccupant();
        u32 x, z;
        occupant->GetLocationInCellCoords(&x, &z);
        result = record->WriteU32(x);
        if (result)
          result = record->WriteU32(z);
      }
      ++node;
    }
  }
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Restore a saved layer through its versioned archive, retaining partial reads and the original month-row checks.
// Load facility coordinates into pending lists; failed input still clamps traversal and recounts landfill as required.
bool EcPollution::Init(CtCity *city, RsDatabaseSegment *segment) {
  EcArchiveFrame frame(city, segment, RsKey{kRsCityLayerRecordType, kEcPollutionClassId, 0});
  u8 result = frame.good;
  RsStream *record = frame.GetStream();
  if (result && frame.text_flag) {
    ScString annotation;
    result = record->ReadString(&annotation);
  }
  if (result)
    result = static_cast<EcDwordMap *>(this)->SerialRead(record);
  if (result)
    result = disposal->SerialRead(city, record);
  if (result)
    result = garbage.SerialRead(record);
  for (u32 category = 0; result && category < 7; ++category)
    result = record->ReadI64(reinterpret_cast<i64 *>(&totals[category]));
  result = result && record->ReadU32(reinterpret_cast<u32 *>(&average_air)) &&
      record->ReadU32(reinterpret_cast<u32 *>(&average_water)) && record->ReadU16(&average_garbage) &&
      record->ReadI32(reinterpret_cast<i32 *>(&accumulated_air)) &&
      record->ReadI32(reinterpret_cast<i32 *>(&accumulated_water)) &&
      record->ReadU32(reinterpret_cast<u32 *>(&total_uncollected));
  if (result && frame.version > 2)
    result = record->ReadU32(&affected_cells);
  if (result)
    result = history_model->SerialRead(city, record);
  for (u32 category = 0; result && category < 7; ++category)
    result = record->ReadI64(reinterpret_cast<i64 *>(&current[category]));
  for (u32 month = 0; result && month < 12; ++month) {
    for (u32 category = 0; category < 6; ++category)
      record->ReadI64(reinterpret_cast<i64 *>(&history[category][month]));
    result = record->ReadI64(reinterpret_cast<i64 *>(&history[6][month]));
  }
  u8 uncollected = 0;
  if (result)
    result = record->ReadU8(&uncollected);
  garbage_uncollected = uncollected != 0;
  result = result && record->ReadU32(reinterpret_cast<u32 *>(&daily_incinerator)) &&
      record->ReadU32(reinterpret_cast<u32 *>(&daily_energy)) && record->ReadF32(&reduction_fraction) &&
      record->ReadU32(&garbage_abducted) && record->ReadU32(&cell_x);
  if (cell_x >= CellCountX())
    cell_x = 0;
  if (result)
    result = record->ReadU32(&cell_z);
  if (cell_z >= CellCountZ())
    cell_z = 0;
  result = result && record->ReadI32(reinterpret_cast<i32 *>(&water_before_treatment)) &&
      record->ReadI32(reinterpret_cast<i32 *>(&treatment_denominator)) &&
      record->ReadI32(reinterpret_cast<i32 *>(&water_treated)) &&
      record->ReadI32(reinterpret_cast<i32 *>(&treatment_numerator)) && cursor.SerialRead(city, record) &&
      record->ReadU32(&landfill_cells);
  if (!result) {
    landfill_cells = 0;
    for (u32 x = 0; x < garbage.CellCountX(); ++x)
      for (u32 z = 0; z < garbage.CellCountZ(); ++z)
        if (IsActiveLandfill(x, z))
          ++landfill_cells;
  }
  if (frame.version > 1) {
    if (result)
      result = second_input.SerialRead(record);
    if (result)
      result = first_input.SerialRead(record);
    if (result)
      result = garbage_produced.SerialRead(record);
    if (result) {
      i32 remaining = 0;
      result = record->ReadI32(&remaining);
      while (result && remaining > 0) {
        u32 identifier = 0;
        i32 value = 0;
        EcModifier modifier{};
        result = record->ReadU32(&identifier);
        if (result)
          result = record->ReadI32(&value);
        modifier.air = static_cast<i16>(value);
        if (result)
          result = record->ReadI32(&value);
        modifier.water = static_cast<i16>(value);
        result = result && record->ReadU8(&modifier.air_radius) && record->ReadU8(&modifier.water_radius) &&
            record->ReadI32(&value);
        modifier.garbage = static_cast<i16>(value);
        if (result)
          saved_modifiers[identifier] = modifier;
        --remaining;
      }
    }
    u32 x = 0, z = 0;
    for (u32 kind = 0; result && kind < 4; ++kind) {
      i32 remaining = 0;
      result = record->ReadI32(&remaining);
      for (;;) {
        i32 previous = remaining;
        remaining = static_cast<i32>(static_cast<u32>(remaining) - 1);
        if (previous <= 0 || !result)
          break;
        result = record->ReadU32(&x);
        if (result)
          result = record->ReadU32(&z);
        pending[kind].push_back({x, z});
      }
    }
  }
  if (result && frame.text_flag) {
    ScString annotation;
    result = record->ReadString(&annotation);
  }
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Expand legacy coarse pollution into descending two-by-two city cells, preserving the truncated conversion byte.
// Read each cell's radiation and salt flags separately; an unavailable import plane shuts down the existing layer.
bool EcPollution::Init(CtCity *, ScImporter *source) {
  if (!source->MapIsPresent(0xA2)) {
    static_cast<EcDwordMap *>(this)->Shutdown();
    return false;
  }
  i32 last = static_cast<i32>(source->GetMapSize(0x80)) - 1;
  u8 pollution = 0, terrain = 0, flags = 0;
  for (i32 x = last; x >= 0; --x)
    for (i32 z = last; z >= 0; --z) {
      source->GetValue(0x80, static_cast<u32>(x), static_cast<u32>(z), &pollution);
      for (i32 dx = 1; dx >= 0; --dx)
        for (i32 dz = 1; dz >= 0; --dz) {
          u32 target_x = static_cast<u32>(2 * x + dx), target_z = static_cast<u32>(2 * z + dz);
          SetAirValue(target_x, target_z, static_cast<u8>(pollution * 32767 / 255));
          SetWaterValue(target_x, target_z, static_cast<u8>(pollution * 32767 / 255));
          source->GetValue(2, target_x, target_z, &terrain);
          SetIsRadioactive(target_x, target_z, terrain == 5);
          source->GetValue(0x20, target_x, target_z, &flags);
          SetIsSaltWater(target_x, target_z, (flags & 1) != 0);
        }
    }
  return true;
}

// Remove this city agent from the application's current simulator, then unregister the owning city's change receiver.
// Preserve the simulator result while still attempting receiver removal, whose result the original ignores.
bool EcPollution::SimulationEnd() {
  CtSimulatorInterface *simulator = EcGetCity()->CitySimulator();
  bool result = true;
  if (simulator)
    result = simulator->RemoveAgent(static_cast<CtAgent *>(this));
  ScOccupantManager *world = owning_city->SurfaceOccupantManager();
  if (world)
    world->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
  return result;
}

// Reconcile saved modifiers with current defaults, rehydrate unchanged facilities and register for world changes.
// Recount uncollected garbage before publishing readouts and adding the actual city-agent interface to the simulator.
bool EcPollution::SimulationBegin() {
  ScOccupantManager *world = owning_city->SurfaceOccupantManager();
  if (!world)
    return false;
  bool changed = saved_modifiers.empty();
  if (!changed) {
    for (const auto &entry : EcAgentModifiers) {
      const EcModifier *saved = EcFindModifier(saved_modifiers, entry.first);
      if (!saved)
        continue;
      if (!saved->IsEqual(&entry.second)) {
        changed = true;
        break;
      }
      saved_modifiers.erase(entry.first);
    }
    if (!changed)
      for (const auto &entry : saved_modifiers) {
        EcModifier modifier = entry.second;
        if (!modifier.air && !modifier.water && !modifier.garbage)
          continue;
        u32 identifier = entry.first;
        CtAgentType *node = agent_types->GetNode(identifier);
        bool matched = false;
        if (node)
          while (identifier != 50) {
            node = node->Parent();
            if (!node)
              break;
            identifier = node->Id();
            const EcModifier *inherited = EcFindModifier(EcAgentModifiers, identifier);
            if (inherited) {
              matched = inherited->IsEqual(&modifier) != 0;
              break;
            }
          }
        if (!matched) {
          changed = true;
          break;
        }
      }
  }
  saved_modifiers.clear();
  if (changed) {
    i32 zero = 0;
    first_input.SetAllCells(&zero);
    zero = 0;
    second_input.SetAllCells(&zero);
    i16 empty = 0;
    garbage_produced.SetAllCells(&empty);
  } else {
    occupant_lock.Lock();
    static constexpr CtAgentKind kFacilityKinds[] = {
        kCtAgentWaterTreatment, kCtAgentIncinerator, kCtAgentWasteToEnergy, kCtAgentRecycling};
    ScOccupant *occupant = nullptr;
    for (u32 kind = 0; kind < 4; ++kind) {
      for (const EcPendingCell &cell : pending[kind])
        if (world->GetOccupantAtCell(cell.first, cell.second, &occupant)) {
          TrackFacility(occupant, 0, kFacilityKinds[kind]);
          occupant->Release();
        }
      pending[kind].clear();
    }
    occupant_lock.Unlock();
  }
  u32 sender;
  if (!world->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &sender, changed))
    return false;
  u32 total = 0;
  for (u32 x = 0; x < garbage.CellCountX(); ++x)
    for (u32 z = 0; z < garbage.CellCountZ(); ++z) {
      u8 zone;
      zones->GetValue(x, z, &zone);
      if (zone != 0x11) {
        u32 value;
        GetGarbageValue(x, z, &value);
        total += value;
      }
    }
  if (total_uncollected != static_cast<i32>(total))
    total_uncollected = static_cast<i32>(total);
  if (garbage_uncollected != (total != 0))
    garbage_uncollected = total != 0;
  UpdateReadout();
  EcGetCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  return true;
}

// Select the original primary or separate interface identity and publish it before virtual retention.
bool EcPollution::QueryInterface(u32 iid, void **out) {
  switch (iid) {
  case IID_ScUnknown:
  case IID_ScCellMapBase:
    *out = static_cast<EcDwordMap *>(this);
    break;
  case IID_EcLayer:
    *out = static_cast<EcLayer *>(this);
    break;
  case IID_ScMessageTarget:
    *out = static_cast<ScMessageTarget *>(static_cast<CtLayer *>(this));
    break;
  case IID_ScQueryTarget:
    *out = static_cast<ScQueryTarget *>(static_cast<CtLayer *>(this));
    break;
  case IID_CtLayer:
    *out = static_cast<CtLayer *>(this);
    break;
  case IID_CtAgent:
    *out = static_cast<CtAgent *>(this);
    break;
  case IID_CtChangeReceiver:
    *out = static_cast<CtChangeReceiver *>(this);
    break;
  case IID_ScQueryInfoString:
    *out = static_cast<ScQueryInfoString *>(this);
    break;
  case IID_SpMapSpriteFactory:
    *out = static_cast<SpMapSpriteFactory *>(this);
    break;
  default:
    RETURN_QUERY_INTERFACE_FAILURE();
  }
  AddRef();
  return true;
}

u32 EcPollution::GetMapSpriteCount() {
  return 5;
}

// Publish the four supported kind identifiers while leaving output untouched for index zero, four or other values.
bool EcPollution::GetSpriteIdFromIndex(u32 index, u32 *out) {
  switch (index) {
  case 1:
    *out = kUiAirPollutionMapRasterId;
    break;
  case 2:
    *out = kUiWaterPollutionMapRasterId;
    break;
  case 3:
    *out = kUiGarbagePollutionMapRasterId;
    break;
  case 5:
    *out = kUiRadiationMapRasterId;
    break;
  default:
    return false;
  }
  return true;
}

// Allocate the selected real raster subtype and publish it through its original QueryInterface call.
bool EcPollution::CreateMapSprite(u32 kind, u32 iid, void **out) {
  EcMapSource *source;
  switch (kind) {
  case kUiAirPollutionMapRasterId:
    source = new EcAirSource(this);
    break;
  case kUiWaterPollutionMapRasterId:
    source = new EcWaterSource(this);
    break;
  case kUiGarbagePollutionMapRasterId:
    source = new EcGarbageSource(this);
    break;
  case kUiRadiationMapRasterId:
    source = new EcRadiationSource(this);
    break;
  default:
    return false;
  }
  if (source->QueryInterface(iid, out))
    return true;
  // The original calls raw operator delete here. Detach its initialized parent so ordinary delete adds no release.
  source->owner = nullptr;
  delete source;
  return false;
}

// Acquire the city services in their original order, initialize all five maps and install the history/disposal models.
// Preserve partial-failure shutdown, live receiver reloads and the shuffled coarse-cell traversal used by simulation.
bool EcPollution::Init(CtCity *city) {
  if (layer_initialized || !city)
    return false;
  u32 fill = 0;
  layer_initialized = Init(city, &fill);
  if (!layer_initialized)
    return false;
  owning_city = ScAddRefNotNull(city);
  zones = owning_city->ZoneLayer();
  if (!zones) {
    Shutdown();
    return false;
  }
  zones->AddRef();
  residential = owning_city->ResidentialLayer();
  if (!residential) {
    Shutdown();
    return false;
  }
  residential->AddRef();
  neighbors = owning_city->NeighborsLayer();
  if (!neighbors) {
    Shutdown();
    return false;
  }
  neighbors->AddRef();
  ordinances = owning_city->OrdinanceLayer();
  if (!ordinances) {
    Shutdown();
    return false;
  }
  ordinances->AddRef();
  traffic = owning_city->TrafficLayer();
  if (!traffic) {
    Shutdown();
    return false;
  }
  traffic->AddRef();
  messages = ScAddRefNotNull(EcGetMessageServer());
  agent_types = ScAddRefNotNull(EcGetAgentTree());
  i32 input_fill = 0;
  first_input.Init(owning_city, &input_fill);
  input_fill = 0;
  second_input.Init(owning_city, &input_fill);
  i16 production_fill = 0;
  garbage_produced.Init(owning_city, &production_fill);
  u16 garbage_fill = 0;
  garbage.Init(owning_city, &garbage_fill);
  average_air = 0;
  average_water = 0;
  average_garbage = 0;
  affected_cells = 0;
  accumulated_air = 0;
  accumulated_water = 0;
  water_before_treatment = 0;
  water_treated = 0;
  treatment_denominator = 0;
  treatment_numerator = 0;
  garbage_uncollected = false;
  total_uncollected = 0;
  daily_incinerator = 0;
  daily_energy = 0;
  history_model = ScAddRefNotNull(new EcHistoryModel(0));
  history_model->SetMaxValue(100);
  {
    EcTextRef resource(kRsGamePollutionText, kRsGameTextGroup);
    ScString title = resource.CopyString();
    history_model->SetSelectionLabel(title);
  }
  {
    EcTextRef resource(kRsGamePollutionFormatText, kRsGameTextGroup);
    ScString format = resource.CopyString();
    text = format;
  }
  ScString formatted;
  EcFormatText(&formatted, text.ToChar(), "0");
  history_model->SetCurrentValueLabel(ScGraphDataSet::Month, formatted);
  history_model->SetCurrentValueLabel(ScGraphDataSet::Year, formatted);
  history_model->SetCurrentValueLabel(ScGraphDataSet::Decade, formatted);
  u32 metadata = kEcPollutionGraphSelectionId;
  history_model->SetSelectionLabelID(metadata);
  metadata = kEcPollutionGraphValueId;
  history_model->SetValueLabelID(metadata);
  history_model->SetColor(kEcPollutionGraphColor);
  CtLayer *layer = owning_city->GetSpecificLayer(kEcDemandLayerType);
  void *object;
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, &object)) {
    Shutdown();
    return false;
  }
  EcValveLayer *valves = static_cast<EcValveLayer *>(object);
  disposal = ScAddRefNotNull(new EcDisposal);
  if (!disposal->Init(owning_city, kEcDisposalDemandValveId, static_cast<CtLayer *>(this))) {
    Shutdown();
    return false;
  }
  disposal->SetValveMax(kEcDisposalValveMagnitudeLimit);
  disposal->SetValveMin(-kEcDisposalValveMagnitudeLimit);
  valves->AddValveToLayer(disposal);
  valves->Release();
  reduction_fraction = 0;
  memset(totals, 0, sizeof(totals));
  memset(current, 0, sizeof(current));
  garbage_abducted = 0;
  landfill_cells = 0;
  memset(history, 0, sizeof(history));
  occupant_lock.Lock();
  for (u32 index = 0; index < 4; ++index) {
    for (ScUnknown *entry : tracked[index])
      entry->Release();
    tracked[index].clear();
  }
  occupant_lock.Unlock();
  aging_treatment = 0;
  aging_incinerator = 0;
  aging_recycling = 0;
  for (u32 index = 0; index < 4; ++index)
    pending[index].clear();
  u32 cells_z = CellCountZ();
  blocks_per_tick = CellCountX() * cells_z / (4u * EcPollingPeriod);
  if (cursor.mode) {
    cursor.mode = 0;
    cursor.refill = 1;
  }
  i32 maximum_z = static_cast<i32>((CellCountZ() >> 1) - 1);
  i32 maximum_x = static_cast<i32>((CellCountX() >> 1) - 1);
  cursor.min_x = 0;
  cursor.min_z = 0;
  cursor.max_x = maximum_x < 0 ? 0 : maximum_x;
  cursor.max_z = maximum_z < 0 ? 0 : maximum_z;
  cursor.refill = 1;
  cursor.PrepareTraversal(1);
  i32 x, z;
  cursor.Next(&x, &z);
  cell_x = 2u * static_cast<u32>(x);
  cell_z = 2u * static_cast<u32>(z);
  saved_modifiers.clear();
  return layer_initialized;
}

// Update all three readout strings through the original signed numeric output and language utility interfaces.
void EcPollution::UpdateReadout() {
  ScString formatted, number;
  ScLanguageUtility *utility = EcGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  u32 value;
  history_model->GetMonthData(&value);
  utility->MakeNumberString(static_cast<i32>(value), &number);
  EcFormatText(&formatted, text.ToChar(), number.ToChar());
  history_model->SetCurrentValueLabel(ScGraphDataSet::Month, formatted);
  history_model->GetYearData(&value);
  utility->MakeNumberString(static_cast<i32>(value), &number);
  EcFormatText(&formatted, text.ToChar(), number.ToChar());
  history_model->SetCurrentValueLabel(ScGraphDataSet::Year, formatted);
  history_model->GetDecadeData(&value);
  utility->MakeNumberString(static_cast<i32>(value), &number);
  EcFormatText(&formatted, text.ToChar(), number.ToChar());
  history_model->SetCurrentValueLabel(ScGraphDataSet::Decade, formatted);
}

void EcPollution::UpdateMonth() {
  u32 month, day, year;
  owning_city->GetDate(&month, &day, &year);
  u32 slot = month > 1 ? month - 2 : 11;
  history[0][slot] =
      static_cast<i64>(current[0]) < static_cast<i64>(garbage_abducted) ? 0 : current[0] - garbage_abducted;
  for (u32 category = 1; category < 7; ++category)
    history[category][slot] = current[category];
  static_cast<CtNeighborsLayer *>(neighbors)->PublishDeal(kCtDealExportGarbage, static_cast<i32>(current[2]));
  for (u32 category = 0; category < 7; ++category) {
    current[category] = 0;
    totals[category] = 0;
  }
  garbage_abducted = 0;
  for (u32 position = 0; position < 12; ++position)
    for (u32 category = 0; category < 7; ++category)
      totals[category] += history[category][position];
  u32 index = static_cast<u32>(average_water) * 100u / EcWaterMaximum;
  index = (index + static_cast<u32>(average_air) * 100u / EcAirMaximum) >> 1;
  if (index > 100)
    index = 100;
  history_model->SetMonthData(index);
  if (month == 1) {
    u32 value = history_model->GetMonthAverage();
    history_model->SetYearData(value);
    if (year % 10 == 0) {
      value = history_model->GetYearAverage();
      history_model->SetDecadeData(value);
    }
  }
  UpdateReadout();
}

// Forward phase two to the live tick virtual and perform monthly accounting for phase four.
// Monthly cleanup does not supply a result; all other phases acknowledge with the original Boolean true.
bool EcPollution::Simulate(i32 phase) {
  if (phase == 2)
    return UpdateTick();
  if (phase == 4)
    UpdateMonth();
  return true;
}

static ScString EcQueryText(u32 instance) {
  EcTextRef resource(instance, kRsCityServiceTextGroup);
  return resource.CopyString();
}

static void EcAssignQueryText(ScString *out, u32 instance) {
  EcTextRef resource(instance, kRsCityServiceTextGroup);
  *out = resource.CopyString();
}

// Convert through the PC53 magic-add store while retaining the original low-DWORD wrap and current rounding mode.
static u32 EcRoundQuery(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Build one actual localized query row using live owner interfaces and module tuning.
u8 EcPollution::BuildAdvisorText(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 category, u8 *warning) {
  if (warning)
    *warning = 0;
  if (!category) {
    ScOccupantManager *world = owning_city->SurfaceOccupantManager();
    ScOccupant *occupant;
    if (!world || !world->GetOccupantAtCell(x, z, &occupant))
      return 0;
    u32 depth = occupant->SizeInCellsZ(), area = occupant->SizeInCellsX() * depth;
    ScString row = EcQueryText(kRsCityServicePollutionGeneratedText);
    label->Copy(row);
    u32 identifier = occupant->TypeId();
    EcModifier modifier{};
    if (const EcModifier *found = EcFindModifier(EcAgentModifiers, identifier))
      modifier = *found;
    f64 scale = static_cast<f64>(1.0466667f);
    u32 air_area = EcRoundQuery(f64(modifier.air_radius) * modifier.air_radius * scale) + area;
    u32 water_area = EcRoundQuery(f64(modifier.water_radius) * modifier.water_radius * scale) + area;
    i32 score = i32(u32(i32(modifier.air)) * air_area + u32(i32(modifier.water)) * water_area);
    u32 combined = EcWaterMaximum + EcAirMaximum, instance;
    if (score <= 0)
      instance = 42;
    else if (score < i32(combined) / 5)
      instance = 43;
    else if (score < i32(5u * combined))
      instance = 44;
    else if (score < i32(10u * combined))
      instance = 45;
    else if (score < i32(50u * combined))
      instance = 46;
    else
      instance = 47;
    EcAssignQueryText(&row, instance);
    if (instance == 47 && warning)
      *warning = 1;
    value->Copy(row);
    occupant->Release();
    return 1;
  }
  if (category <= 2) {
    ScString number, formatted;
    u8 zone;
    zones->GetValue(x, z, &zone);
    if (zone != 17)
      return 0;
    ScLanguageUtility *utility = EcGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    if (category == 1) {
      u32 capacity = u32(GetTotalLandfillCapacity());
      volatile f64 scaled = static_cast<f64>(capacity) * static_cast<f64>(EcGarbageScale);
      i32 quantity = i32(EcRoundQuery(scaled));
      ScString pattern = EcQueryText(kRsCityServiceCapacityCitywideText);
      label->Copy(pattern);
      EcAssignQueryText(&pattern, 269);
      utility->MakeNumberString(quantity, &number);
      EcFormatText(&formatted, pattern.Data(), number.Data());
      value->Copy(formatted);
    } else {
      i32 capacity = GetTotalLandfillCapacity(), available = GetAvailableLandfillCapacity();
      ScString pattern = EcQueryText(270);
      label->Copy(pattern);
      i32 percentage = 0;
      if (capacity > 0) {
        if (available > capacity)
          available = capacity;
        i32 used = i32(u32(capacity) - u32(available));
        volatile f64 product = static_cast<f64>(used) * 100.0;
        volatile f64 fraction = product / static_cast<f64>(capacity);
        percentage = i32(u32(static_cast<i64>(ceil(fraction))));
      }
      EcAssignQueryText(&pattern, 271);
      utility->MakeNumberString(percentage, &number);
      EcFormatText(&formatted, pattern.Data(), number.Data());
      value->Copy(formatted);
    }
    return 1;
  }
  if (category == 3 || category == 4) {
    ScString title = EcQueryText(category == 3 ? 391 : 398);
    label->Copy(title);
    u32 quantity;
    if (category == 3)
      GetAirValue(x, z, &quantity);
    else
      GetWaterValue(x, z, &quantity);
    u32 maximum = category == 3 ? EcAirMaximum : EcWaterMaximum, grade;
    if (!quantity)
      grade = 0;
    else if (quantity < (maximum >> 3))
      grade = 1;
    else if (quantity < (maximum >> 2))
      grade = 2;
    else if (quantity < (maximum >> 1))
      grade = 3;
    else if (quantity < 4u * maximum / 5u)
      grade = 4;
    else
      grade = 5;
    if (grade == 5 && warning)
      *warning = 1;
    ScString row = EcQueryText((category == 3 ? 392 : 399) + grade);
    value->Copy(row);
    return 1;
  }
  return 0;
}

// Publish up to five actual query rows after one location lookup. Reread the live coordinates before each row and
// preserve the original success byte even when every individual row is unavailable or an AddText result is false.
u8 EcPollution::QueryTile(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  u8 warning = 0;
  if (!query || !query->GetLocation(&location))
    return 0;
  static const u32 categories[] = {0x4000, 0x20000, 0x20000, 0x2000, 0x8000};
  for (u32 kind = 0; kind < 5; ++kind)
    if (BuildAdvisorText(u32(location.x >> 8), u32(location.z >> 8), &label, &value, kind, &warning))
      query->AddText(&label, &value, categories[kind], warning);
  return 1;
}

// Reload live interface outputs after callbacks, release every raster temporary even after failed creation and preserve
// the last raster add's raw byte result.
u8 EcPollution::HandleQuery(ScMessage *message, ScUnknown *sender) {
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagGraphs:
    if (!sender || !sender->QueryInterface(IID_UiGraphsViewIdentity, &selected))
      return 0;
    static_cast<ScObjectCollection *>(selected)->Add(history_model);
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagDialog:
  case kUiQueryTagDebug:
    if (!sender || !sender->QueryInterface(
        u32(message->data1) == kUiQueryTagDialog ? IID_UiQueryWindow : IID_UiQuerySection, &selected))
      return 0;
    // Original1589A(message, selected) is empty and its false return is ignored.
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagMaps: {
    if (!sender || !sender->QueryInterface(IID_UiRasterQuery, &selected))
      return 0;
    static const u32 kRasterIds[] = {
        kUiAirPollutionMapRasterId, kUiWaterPollutionMapRasterId, kUiGarbagePollutionMapRasterId, kUiRadiationMapRasterId};
    void *source = nullptr;
    u8 result = 0;
    for (u32 kind : kRasterIds) {
      result = CreateMapSprite(kind, IID_GxRasterSource, &source);
      if (result)
        result = static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(source));
      if (source) {
        static_cast<ScUnknown *>(source)->Release();
        source = nullptr;
      }
    }
    static_cast<ScUnknown *>(selected)->Release();
    return result;
  }
  case kUiQueryTagStandard:
    if (!sender || !sender->QueryInterface(IID_UiQuerySection, &selected))
      return 0;
    QueryTile(message, static_cast<UiQuery *>(selected));
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  default:
    return 0;
  }
}

// Route query requests through the original handler. The separate history message is acknowledged even when its
// sender is absent or rejects the interface; successful delivery uses the actual model pointer and releases the live view.
bool EcPollution::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  if (message->type == kWnMsg_QueryOpened)
    return HandleQuery(message, sender) != 0;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (sender && sender->QueryInterface(IID_ScModelReceiver, &selected)) {
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(history_model);
      static_cast<ScUnknown *>(selected)->Release();
    }
    return true;
  }
  return false;
}

u32 EcModifier::IsEqual(const EcModifier *other) const {
  return other->air == air && other->water == water && other->air_radius == air_radius &&
      other->water_radius == water_radius && other->garbage == garbage;
}

// Borrow a stable payload from the original unsigned-key ordered map. A missing key leaves the collection unchanged.
const EcModifier *EcFindModifier(const EcSavedModifiers &values, u32 identifier) {
  auto entry = values.find(identifier);
  return entry == values.end() ? nullptr : &entry->second;
}

u8 EcOrdinanceEffects[12] = {20, 20, 50, 40, 5, 10, 3, 4, 10, 4, 8, 4};
i32 EcTrafficPollution = 8;

// Reproduce the tick's PC53 round-to-nearest conversion through its original magic-add double store.
// Only the stored low DWORD contributes; no host integer-conversion rounding mode is substituted.
static i32 EcRoundPollution(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return static_cast<i32>(static_cast<u32>(bits) + kScDoubleIntegerLowWordBias);
}

// Update pollution, garbage routing and facility consumption for the live number of two-by-two city blocks.
bool EcPollution::UpdateTick() {
  auto water_end = tracked[0].end();
  u32 water_decay = EcWaterMaximum / 120, air_decay = EcAirMaximum >> 3;
  occupant_lock.Lock();
  i32 water_capacity = 0, aging_water = 0;
  auto node = tracked[0].begin();
  for (; node != water_end; ++node) {
    water_capacity += static_cast<i32>(static_cast<BdTreatment *>(*node)->GetCapacity());
    if (static_cast<BdTreatment *>(*node)->IsAging())
      ++aging_water;
  }
  i32 water_remaining = water_capacity, aging_combustion = 0;
  aging_treatment = aging_water;
  auto end = tracked[1].end();
  daily_incinerator = 0;
  u32 incinerator_capacity = 0;
  for (node = tracked[1].begin(); node != end; ++node) {
    u32 production = static_cast<EcCombustion *>(*node)->GetProduction();
    daily_incinerator = static_cast<i32>(static_cast<u32>(daily_incinerator) + production);
    incinerator_capacity += static_cast<EcCombustion *>(*node)->GetCapacity();
    if (static_cast<EcCombustion *>(*node)->IsAging())
      ++aging_combustion;
  }
  i32 incinerator_remaining = static_cast<i32>(incinerator_capacity);
  daily_energy = 0;
  u32 energy_capacity = 0;
  end = tracked[2].end();
  for (node = tracked[2].begin(); node != end; ++node) {
    u32 production = static_cast<EcCombustion *>(*node)->GetProduction();
    daily_energy = static_cast<i32>(static_cast<u32>(daily_energy) + production);
    energy_capacity += static_cast<EcCombustion *>(*node)->GetCapacity();
    if (static_cast<EcCombustion *>(*node)->IsAging())
      ++aging_combustion;
  }
  i32 energy_remaining = static_cast<i32>(energy_capacity);
  aging_incinerator = aging_combustion;
  occupant_lock.Unlock();
  CtOrdinanceLayer *rules = static_cast<CtOrdinanceLayer *>(ordinances);
  bool garbage_ordinance = rules->QueryOrdinanceOn(kCtOrdinance_TrashPresort);
  bool residential_air = rules->QueryOrdinanceOn(kCtOrdinance_LeafBurningBan);
  bool air_ordinance = rules->QueryOrdinanceOn(kCtOrdinance_CleanAir);
  bool traffic_ordinance = rules->QueryOrdinanceOn(kCtOrdinance_CarSmogging);
  bool water_ordinance = rules->QueryOrdinanceOn(kCtOrdinance_ConservationCorps);
  bool air_water_ordinance = rules->QueryOrdinanceOn(kCtOrdinance_IndustrialPollutionFee);
  bool additional_water = rules->QueryOrdinanceOn(kCtOrdinance_LawnChemicalBan);
  bool landfill_air = rules->QueryOrdinanceOn(kCtOrdinance_LandfillGasRecovery);
  bool industrial_garbage = rules->QueryOrdinanceOn(kCtOrdinance_IndustrialWasteDisposal);
  bool residential_garbage = rules->QueryOrdinanceOn(kCtOrdinance_BackyardComposting);
  bool commercial_garbage = rules->QueryOrdinanceOn(kCtOrdinance_PaperReduction);
  bool additional_garbage = rules->QueryOrdinanceOn(kCtOrdinance_TireRecycling);
  CtDealRecord deal;
  i32 imported = 0;
  if (static_cast<CtNeighborsLayer *>(neighbors)->GetDeal(kCtDealImportGarbage, &deal)) {
    imported = static_cast<i32>(deal.value_08) / 30;
    current[1] += static_cast<u64>(static_cast<i64>(imported));
    current[0] += static_cast<u64>(static_cast<i64>(imported));
  }
  occupant_lock.Lock();
  i32 delivered = RouteGarbage(imported, &incinerator_remaining, &energy_remaining);
  disposal->AddToSupplyValue(imported - delivered);
  occupant_lock.Unlock();
  for (u32 processed = 0; processed < blocks_per_tick; ++processed) {
    occupant_lock.Lock();
    i32 air, water;
    i16 produced;
    first_input.GetValue(cell_x, cell_z, &air);
    second_input.GetValue(cell_x, cell_z, &water);
    garbage_produced.GetValue(cell_x, cell_z, &produced);
    if (air_ordinance)
      air += air * EcOrdinanceEffects[0] / -100;
    if (garbage_ordinance)
      produced = static_cast<i16>(produced + EcOrdinanceEffects[2] * produced / -100);
    if (water_ordinance)
      water += water * EcOrdinanceEffects[4] / -100;
    if (air_water_ordinance) {
      water += water * EcOrdinanceEffects[5] / -100;
      air += air * EcOrdinanceEffects[5] / -100;
    }
    if (additional_water)
      water += water * EcOrdinanceEffects[6] / -100;
    if (additional_garbage)
      produced = static_cast<i16>(produced + EcOrdinanceEffects[11] * produced / -100);
    i32 valid_cells = 0, adjusted_air = air;
    u32 adjusted_water = static_cast<u32>(water);
    for (u32 cell = 0; cell < 4; ++cell) {
      u32 x = cell_x + (cell & 1), z = cell_z + (cell >> 1);
      u8 zone;
      zones->GetValue(x, z, &zone);
      if (zone != 17) {
        if (IsActiveLandfill(x, z)) {
          disposal->AddToDemandValue(-32767);
          SetIsActiveLandfill(x, z, false);
        }
        ++valid_cells;
      }
    }
    bool has_garbage = false;
    for (u32 cell = 0; cell < 4; ++cell) {
      u32 x = cell_x + (cell & 1), z = cell_z + (cell >> 1);
      u8 zone;
      zones->GetValue(x, z, &zone);
      bool is_landfill = zone == 17;
      if ((zone >= 9 && zone <= 11) || zone == 15) {
        if (industrial_garbage)
          produced = static_cast<i16>(produced + EcOrdinanceEffects[8] * produced / (-100 * valid_cells));
      } else if (zone == 1 || zone == 2) {
        if (residential_air)
          adjusted_air += air * EcOrdinanceEffects[1] / (-100 * valid_cells);
        if (residential_garbage)
          produced = static_cast<i16>(produced + EcOrdinanceEffects[9] * produced / (-100 * valid_cells));
      } else if (zone >= 5 && zone <= 7) {
        if (commercial_garbage)
          produced = static_cast<i16>(produced + EcOrdinanceEffects[10] * produced / (-100 * valid_cells));
      } else if (is_landfill && landfill_air) {
        adjusted_air += adjusted_air * EcOrdinanceEffects[7] / (100 * (valid_cells - 4));
      }
      u32 old_garbage;
      GetGarbageValue(x, z, &old_garbage);
      if (old_garbage) {
        has_garbage = true;
        adjusted_water += 8 * old_garbage / 32767;
      }
      u8 volume;
      static_cast<NwTrafficLayer *>(traffic)->GetVolume(x, z, &volume, 3);
      adjusted_air += EcTrafficPollution * volume;
      if (traffic_ordinance && volume)
        adjusted_air = static_cast<i32>(static_cast<u32>(adjusted_air) -
            static_cast<u32>(EcTrafficPollution * volume) * EcOrdinanceEffects[3] / 400);
      if (!is_landfill) {
        i32 quantity = produced / valid_cells;
        current[0] += static_cast<u64>(static_cast<i64>(quantity));
        if (reduction_fraction > 0 && quantity > 0) {
          volatile f64 product = static_cast<f64>(quantity) * reduction_fraction;
          f64 amount = product;
          if (amount > 0.5 && (cell & 1))
            amount -= 0.5;
          i32 recycled = EcRoundPollution(amount);
          if (recycled > quantity)
            recycled = quantity;
          current[3] += static_cast<u64>(static_cast<i64>(recycled));
          quantity -= recycled;
        }
        i32 total = static_cast<i32>(old_garbage + static_cast<u32>(quantity));
        SetGarbageValue(x, z, 0);
        if (static_cast<u32>(total_uncollected) >= old_garbage)
          total_uncollected = static_cast<i32>(static_cast<u32>(total_uncollected) - old_garbage);
        i32 remaining = total - RouteGarbage(total, &incinerator_remaining, &energy_remaining);
        if (remaining > 0) {
          if (remaining > 32767) {
            SetGarbageValue(x, z, 32767);
            total_uncollected += 32767;
            disposal->AddToSupplyValue(remaining - 32767);
          } else {
            i32 balance = disposal->QueryValveValue();
            if (balance >= 0 || remaining >= 32767) {
              SetGarbageValue(x, z, static_cast<u32>(remaining));
              total_uncollected += remaining;
            } else {
              i32 extra = -balance;
              if (extra > produced / valid_cells)
                extra = produced ? produced / valid_cells : 1;
              if (extra + remaining > 32767)
                extra = 32767 - remaining;
              disposal->AddToSupplyValue(-extra);
              SetGarbageValue(x, z, static_cast<u32>(extra + remaining));
              total_uncollected += remaining + extra;
            }
          }
        }
      }
    }
    i32 remaining_water = static_cast<i32>(adjusted_water);
    water_before_treatment += adjusted_water;
    if (water_remaining > 0 && remaining_water > 0) {
      i32 denominator = static_cast<i32>(treatment_denominator), numerator = static_cast<i32>(treatment_numerator);
      i32 treated;
      if (denominator <= 0 || numerator <= 0 || water_remaining > numerator) {
        treated = water_remaining;
      } else {
        volatile f64 ratio = static_cast<f64>(numerator) / denominator;
        volatile f64 product = ratio * remaining_water;
        treated = EcRoundPollution(product);
        if (!treated)
          treated = 1;
      }
      if (treated > remaining_water)
        treated = remaining_water;
      if (treated > water_remaining)
        treated = water_remaining;
      water_treated += static_cast<u32>(treated);
      remaining_water -= treated;
      water_remaining -= treated;
    }
    accumulated_air += static_cast<u32>(adjusted_air);
    accumulated_water += static_cast<u32>(remaining_water);
    i32 target_air = adjusted_air / 4, target_water = remaining_water / 4;
    if (target_air < 0)
      target_air = 0;
    else if (target_air > 32767)
      target_air = 32767;
    if (target_water < 0)
      target_water = 0;
    else if (target_water > 32767)
      target_water = 32767;
    if (air > 0 || water > 0 || has_garbage)
      affected_cells += 4;
    u32 previous;
    GetAirValue(cell_x, cell_z, &previous);
    if (previous < static_cast<u32>(target_air)) {
      u32 change = static_cast<u32>(target_air) - previous;
      if (change > air_decay)
        change = air_decay;
      SetAirValue(cell_x, cell_z, previous + change);
    } else if (previous > static_cast<u32>(target_air)) {
      u32 change = previous - static_cast<u32>(target_air);
      if (change > air_decay)
        change = air_decay;
      SetAirValue(cell_x, cell_z, previous - change);
    }
    GetWaterValue(cell_x, cell_z, &previous);
    if (previous < static_cast<u32>(target_water)) {
      u32 change = static_cast<u32>(target_water) - previous;
      if (change > EcWaterMaximum / 12)
        change = EcWaterMaximum / 12;
      SetWaterValue(cell_x, cell_z, previous + change);
    } else if (previous > static_cast<u32>(target_water)) {
      u32 change = previous - static_cast<u32>(target_water);
      if (change > water_decay)
        change = water_decay;
      u32 next = previous - change;
      if (!next) {
        bool clear = true;
        u32 neighbor;
        if (cell_x) {
          GetWaterValue(cell_x - 1, cell_z, &neighbor);
          if (neighbor > 1)
            clear = false;
        }
        if (cell_z && clear) {
          GetWaterValue(cell_x, cell_z - 1, &neighbor);
          if (neighbor > 1)
            clear = false;
        }
        u32 right = cell_x;
        if (right < CellCountX() - 1 && clear) {
          GetWaterValue(right + 1, cell_z, &neighbor);
          if (neighbor > 1)
            clear = false;
        }
        u32 bottom = cell_z;
        if (bottom < CellCountZ() - 1 && clear) {
          GetWaterValue(cell_x, bottom + 1, &neighbor);
          if (neighbor > 1)
            clear = false;
        }
        if (!clear)
          next = 1;
      }
      SetWaterValue(cell_x, cell_z, next);
    }
    if (cursor.phase == 2) {
      cursor.PrepareTraversal(1);
      FinalizeCycle();
    }
    i32 next_x, next_z;
    cursor.Next(&next_x, &next_z);
    cell_x = static_cast<u32>(2 * next_x);
    cell_z = static_cast<u32>(2 * next_z);
    occupant_lock.Unlock();
  }
  i32 balance = disposal->QueryValveValue();
  if (balance < 0) {
    i32 quantity = -balance, routed = RouteGarbage(quantity, &incinerator_remaining, &energy_remaining);
    i32 excess = quantity - routed;
    if (excess > kEcMaximumUnroutedGarbage)
      routed += excess - kEcMaximumUnroutedGarbage;
    disposal->AddToSupplyValue(-routed);
  }
  occupant_lock.Lock();
  node = tracked[0].begin();
  i32 demand = water_capacity - water_remaining;
  for (; demand > 0 && node != water_end; ++node) {
    i32 capacity = static_cast<i32>(static_cast<BdTreatment *>(*node)->GetCapacity());
    if (capacity > 0) {
      if (capacity >= demand) {
        static_cast<BdTreatment *>(*node)->Consume(static_cast<u32>(demand));
        demand = 0;
      } else {
        demand -= capacity;
        static_cast<BdTreatment *>(*node)->Consume(static_cast<u32>(capacity));
      }
    }
  }
  end = tracked[1].end();
  u32 combustion_demand = incinerator_capacity - static_cast<u32>(incinerator_remaining);
  node = tracked[1].begin();
  for (; combustion_demand && node != end; ++node) {
    u32 capacity = static_cast<EcCombustion *>(*node)->GetCapacity();
    if (capacity) {
      if (capacity >= combustion_demand) {
        static_cast<EcCombustion *>(*node)->Consume(combustion_demand);
        combustion_demand = 0;
      } else {
        combustion_demand -= capacity;
        static_cast<EcCombustion *>(*node)->Consume(capacity);
      }
    }
  }
  end = tracked[2].end();
  combustion_demand = energy_capacity - static_cast<u32>(energy_remaining);
  node = tracked[2].begin();
  for (; combustion_demand && node != end; ++node) {
    u32 capacity = static_cast<EcCombustion *>(*node)->GetCapacity();
    if (capacity) {
      if (capacity >= combustion_demand) {
        static_cast<EcCombustion *>(*node)->Consume(combustion_demand);
        combustion_demand = 0;
      } else {
        combustion_demand -= capacity;
        static_cast<EcCombustion *>(*node)->Consume(capacity);
      }
    }
  }
  occupant_lock.Unlock();
  return true;
}

// Consume one defined decimal scanf conversion; representable input is an invariant of the original %d conversion.
// Literal punctuation is checked by the caller and does not consume intervening whitespace.
static bool EcReadTuningDecimal(const char **cursor, i32 *out) {
  const char *text = *cursor;
  while (isspace(static_cast<u8>(*text)))
    ++text;
  bool negative = false;
  if (*text == '+' || *text == '-')
    negative = *text++ == '-';
  if (*text < '0' || *text > '9')
    return false;
  u32 value = 0;
  while (*text >= '0' && *text <= '9') {
    u32 digit = static_cast<u32>(*text++ - '0');
    assert(value <= ((negative ? kScIntegerSignBit : INT32_MAX) - digit) / 10);
    value = value * 10 + digit;
  }
  *out = negative ? static_cast<i32>(0u - value) : static_cast<i32>(value);
  *cursor = text;
  return true;
}

// Preserve the five-field row callback effects through the actual const String inputs and unused generic context.
// The original cleanup return is ignored by its caller; this native phase does not assert a private source return.
static void EcReadAgentModifier(const ScString &key, const ScString &value, void *) {
  ScString key_copy(key), value_copy(value);
  u32 identifier = EcParseTuningNumber(key_copy.Data());
  const char *text = value_copy.Data();
  i32 fields[5];
  static const char kPunctuation[] = ":,:,";
  for (u32 index = 0; index < 5; ++index)
    if (!EcReadTuningDecimal(&text, &fields[index]) || (index < 4 && *text++ != kPunctuation[index]))
      return;
  if (!fields[0] && !fields[2] && !fields[4])
    return;
  for (u32 index = 0; index < 5; ++index) {
    i32 minimum = index == 0 || index == 2 ? -32767 : 0;
    i32 maximum = index == 1 || index == 3 ? 255 : 32767;
    if (fields[index] < minimum)
      fields[index] = minimum;
    else if (fields[index] > maximum)
      fields[index] = maximum;
  }
  EcModifier modifier{static_cast<i16>(fields[0]), static_cast<i16>(fields[2]), static_cast<u8>(fields[1]),
      static_cast<u8>(fields[3]), static_cast<i16>(fields[4])};
  EcAgentModifiers[identifier] = modifier;
}

// Load the real pollution, incinerator and energy configuration in source order, preserving defaults for absent keys.
// Modifier enumeration retires the prior module map before any file lookup; each later key uses the live output String.
bool EcPollution::StaticInit(CtDefinition *) {
  static const char *const kKeys[] = {"UpdatePeriod", "CleanAirOrdEffect", "LeafBurningBanOrdEffect",
      "TrashPresortOrdEffect", "CarSmoggingOrdEffect", "ConservationCorpsOrdEffect", "IndPolluteFeeOrdEffect",
      "LawnChemBanOrdEffect", "LandfillGasRecoveryOrdEffect", "IndWasteDisposalOrdEffect",
      "BackyardCompostingOrdEffect", "PaperReductionOrdEffect", "TireRecyclingOrdEffect", "TrafficAirFactor",
      "MaxGarbagePolluteForUI", "MaxWaterPolluteForUI", "MaxAirPolluteForUI", "GarbagePollutedThreshold",
      "WaterPollutedThreshold", "AirPollutedThreshold", "GarbageAbductionAmount", "GarbageScalingFactor"};
  ScString path, value;
  EcIni pollution;
  EcAgentModifiers.clear();
  EcGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Pollution.INI", 21);
  pollution.SelectFile(&path);
  EcGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  pollution.SelectPackage(path);
  {
    ScString section("AgentIDModifiers", 16);
    pollution.EnumerateSection(&section, EcReadAgentModifier, this);
  }
  for (u32 index = 0; index < 22; ++index) {
    u8 found;
    {
      ScString key(kKeys[index], static_cast<u32>(strlen(kKeys[index]))), section("TuningParameters", 16);
      found = pollution.GetValue(&section, &key, &value);
    }
    if (!found)
      continue;
    u32 number = EcParseTuningNumber(value.Data());
    if (index == 0) {
      EcPollingPeriod = static_cast<u16>(number);
      if (!EcPollingPeriod)
        EcPollingPeriod = 1;
    } else if (index <= 12) {
      EcOrdinanceEffects[index - 1] = static_cast<u8>(number);
    } else if (index == 21) {
      volatile f64 product = static_cast<f64>(number) * static_cast<f64>(0.001f);
      EcGarbageScale = static_cast<f32>(product);
    } else {
      u32 *words[] = {reinterpret_cast<u32 *>(&EcTrafficPollution), &EcGarbageMaximum, &EcWaterMaximum, &EcAirMaximum,
          &EcGarbageThreshold, &EcWaterThreshold, &EcAirThreshold, &EcGarbageAbductionLimit};
      *words[index - 13] = number;
    }
  }
  EcIni capacity;
  EcGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Tune.INI", 16);
  capacity.SelectFile(&path);
  EcGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  capacity.SelectPackage(path);
  u8 found;
  {
    ScString key("OptimalGarbageCap", 17), section("Incinerator", 11);
    found = capacity.GetValue(&section, &key, &value);
  }
  if (found)
    EcIncineratorCapacity = 30u * EcParseTuningNumber(value.Data());
  EcGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3PowerLayer.INI", 22);
  capacity.SelectFile(&path);
  EcGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  capacity.SelectPackage(path);
  {
    ScString key("OptimalGarbageCap", 17), section("WasteToEnergyPowerPlant", 23);
    found = capacity.GetValue(&section, &key, &value);
  }
  if (found)
    EcEnergyCapacity = 30u * EcParseTuningNumber(value.Data());
  return true;
}
