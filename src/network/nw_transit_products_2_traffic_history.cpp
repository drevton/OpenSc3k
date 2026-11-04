#define _CRT_SECURE_NO_WARNINGS
#include "network/nw_transit_products.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cassert>
#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_residential.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/utilities.h"
#include "graphics/image.h"
#include "misc/api.h"
#include "misc/world.h"
#include "network/api.h"
#include "network/nw_manager.h"
#include "network/nw_module.h"
#include "network/nw_records.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/module.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "window/api.h"

// Test the actual network flags for the three recognized single-mode selectors without Boolean normalization of inputs.
bool NwTransitLayer::HasNetworkMode(u32 x, u32 z, u8 mode) {
  u16 flags = grid92->cells[x][z];
  if (mode == 2)
    return (flags & 2) != 0;
  if (mode == 8)
    return (flags & 8) != 0;
  return mode == 21 && (flags & 21) != 0;
}

static ScRandom *NwTransitRandom;
static NwTrafficStatsManipulator *NwTransitStatsManipulator;
static NwTripManipulator *NwTransitTripManipulators[4][4];
static u32 NwTransitManipulatorCursor;

// Load original tunables before obtaining the locale driving side and constructing the actual module-owned
// manipulators.
bool NwTransitLayer::StaticInit(CtDefinition *) {
  NwLoadTransitTunables();
  ScLanguageUtility *language = NwGetTransitLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language)
    return false;
  drivingSide = language->GetLanguageRoadDrivingSide();
  NwTransitRandom = new ScRandom(kScRandomSeedFromClock);
  if (options.mode != 1) {
    options.mode = 1;
    options.dirty = 1;
  }
  NwTransitStatsManipulator = ScAddRefNotNull(new NwTrafficStatsManipulator);
  for (u32 kind = 0; kind < 4; ++kind) {
    for (u32 index = 0; index < 4; ++index) {
      NwTransitTripManipulators[kind][index] = new NwTripManipulator(kind, index);
      NwTransitTripManipulators[kind][index]->AddRef();
    }
  }
  return true;
}

// Retire the random stream before releasing and explicitly deleting each live module-owned manipulator.
bool NwTransitLayer::StaticShutdown() {
  ScDelete(&NwTransitRandom);
  if (NwTransitStatsManipulator) {
    NwTransitStatsManipulator->Release();
    delete NwTransitStatsManipulator;
    NwTransitStatsManipulator = nullptr;
  }
  for (u32 kind = 0; kind < 4; ++kind) {
    for (u32 index = 0; index < 4; ++index) {
      if (NwTransitTripManipulators[kind][index]) {
        NwTransitTripManipulators[kind][index]->Release();
        delete NwTransitTripManipulators[kind][index];
        NwTransitTripManipulators[kind][index] = nullptr;
      }
    }
  }
  return true;
}

// Follow the actual parent chain until one of the original trip-producing type identifiers selects a category.
// An absent or unmatched chain leaves the genuine byte output untouched.
static u8 SC3_STDCALL NwClassifyTripType(const CtAgentType *type, u8 *out) {
  while (type) {
    switch (type->Id()) {
    case kCtAgentResidential:
      *out = 0;
      return 1;
    case kCtAgentCommercial:
    case kCtAgentGovernment:
    case kCtAgentAirport:
    case kCtAgentSpaceport:
    case kCtAgentLandmark:
      *out = 1;
      return 1;
    case kCtAgentIndustrial:
    case kCtAgentSeaport:
      *out = 2;
      return 1;
    case kCtAgentArcology:
    case kCtAgentMilitary:
    case kCtAgentGarbage:
    case kCtAgentPowerPlant:
    case kCtAgentCityStateServices:
    case kCtAgentBusinessDeal:
      *out = 3;
      return 1;
    }
    type = type->Parent();
  }
  return 0;
}

// Publish a borrowed manipulator and original flags for recognized types, preserving the shared four-phase cursor.
// Unsupported types return success with a null manipulator while leaving the flags output unchanged.
bool NwTransitLayer::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    *flags = 4;
    *out = NwTransitStatsManipulator;
    return true;
  }
  u8 category;
  if (!NwClassifyTripType(&type, &category))
    return true;
  *flags = 3;
  *out = NwTransitTripManipulators[category][NwTransitManipulatorCursor];
  NwTransitManipulatorCursor = (NwTransitManipulatorCursor + 1) & 3;
  return true;
}

// Run the actual monthly state workers against the live module singleton, reloading it after each original callback.
// The original predicate is singleton presence, not the erroneous FLIRT exception-runtime label.
bool NwTrafficStatsManipulator::Apply(CtAgent *) {
  if (NwTransitActiveLayer) {
    NwTransitActiveLayer->RecomputeStats();
    NwTransitActiveLayer->UpdateCongestionState();
    NwTransitActiveLayer->ApplyTunables();
    NwTransitActiveLayer->UpdateTransportBudgets();
    NwTransitActiveLayer->fareIncome = NwTransitActiveLayer->value256;
    NwTransitActiveLayer->value256 = 0.0f;
  }
  return true;
}

// Check mode-specific connected flags first, then the original kind-selected station catchment planes.
// Road modes may fall through to catchment scans; rail and subway connection replies return immediately.
bool NwTransitLayer::IsReachable(NwTripData *trip, u32 kind) {
  u16 flags = grid92->cells[trip->x0][trip->z0];
  switch (trip->mode) {
  case 1:
  case 65:
    if (flags & 0x400)
      return true;
    break;
  case 2:
  case 66:
    return (flags & 0x800) != 0;
  case 4:
    return (flags & 0x1000) != 0;
  case 8:
    return (flags & 0x2000) != 0;
  }
  if (kind < 3) {
    NwBitGrid *grid = kind == 0 ? grid132 : kind == 1 ? grid136 : grid140;
    if (!grid)
      return false;
    for (u32 x = trip->x0; x <= trip->x1; ++x)
      for (u32 z = trip->z0; z <= trip->z1; ++z)
        if (grid->words[x][z >> 5] & (1u << (z & 31)))
          return true;
  } else if (kind == 3) {
    NwBitGrid *grids[] = {grid132, grid136, grid140};
    for (u32 index = 0; index < 3; ++index)
      for (u32 x = trip->x0; x <= trip->x1; ++x)
        for (u32 z = trip->z0; z <= trip->z1; ++z)
          if (grids[index]->words[x][z >> 5] & (1u << (z & 31)))
            return true;
  }
  return false;
}

static u8 NwTransitDensityLimit = 255, NwTransitTripVolumePerCell = 1;
static u8 NwTransitTripCosts[10] = {4, 3, 2, 1, 5, 5, 5, 5, 2, 1};
static u8 NwTransitDensityDivisors[10] = {32, 32, 32, 32, 128, 128, 128, 128, 255, 255};
static u8 NwTransitAnnoyance = 150, NwTransitDamageCost = 32, NwTransitLobotomyDensity = 128;
static u8 NwTransitDestinationThresholds[4][3];
static u8 NwTransitReservedState;
static f32 NwTransitRoadUpkeep = 0.1f, NwTransitHighwayUpkeep = 0.5f;
static f32 NwTransitRailUpkeep = 0.1f, NwTransitSubwayUpkeep = 0.5f;

void NwTransitLayer::AddCongestion(u32 x, u32 z, i8 amount, u32 mode) {
  if (mode & 0x35) {
    i32 value = i32(grid108->cells[x][z]) + amount;
    if (value <= 0)
      value = 0;
    if (value >= NwTransitDensityLimit)
      value = NwTransitDensityLimit;
    grid108->cells[x][z] = u8(value);
  }
  if (mode & 2) {
    i32 value = i32(grid112->cells[x][z]) + amount;
    if (value <= 0)
      value = 0;
    if (value >= NwTransitDensityLimit)
      value = NwTransitDensityLimit;
    grid112->cells[x][z] = u8(value);
  }
  if (mode & 8) {
    i32 value = i32(grid116->cells[x][z]) + amount;
    if (value <= 0)
      value = 0;
    if (value >= NwTransitDensityLimit)
      value = NwTransitDensityLimit;
    grid116->cells[x][z] = u8(value);
  }
}

static bool SC3_STDCALL NwIsCongestionOccupant(ScOccupant *occupant) {
  return occupant && occupant->IsGeomFlag(kOcGeomTransitPiece);
}

// Actual route-debug drawing helpers, defined below with the real city-view and sprite-manager interfaces.
void SC3_STDCALL NwShowRouteOccupant(ScOccupant *occupant, u16 mask);
void SC3_STDCALL NwClearRouteOccupant(ScOccupant *occupant);

u8 NwTransitLayer::SelectTripKind(u32 kind, u32 *out) {
  if (kind >= 4)
    return 0;
  u8 value = u8(NwTransitRandom->Bounded(100));
  if (value < NwTransitDestinationThresholds[kind][0]) {
    *out = 0;
    return 1;
  }
  if (kind == 1 || kind == 2) {
    *out = value < NwTransitDestinationThresholds[kind][1] ? 1 : 2;
    return 1;
  }
  // A missing workforce leaves the original second ratio unwritten, outside the defined route domain.
  assert(trafficValve);
  u32 commercial, industrial;
  trafficValve->GetCIRatio(&commercial, &industrial);
  value = u8(NwTransitRandom->Bounded(commercial + industrial));
  *out = u32(value > commercial) + 1;
  return 1;
}

// Execute the original LIFO route search, preserving aggregate mutation, backtracking and explicit occupant releases.
// Original deque use is established by9EB9 push_back,9E8E back,9EED pop_back and the32-byte trip element copies.
u8 NwTransitLayer::RouteTrip(NwTripData *trip, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) {
  if (kind >= 4)
    return 0;
  ++roadTotal;
  u32 cost = u32(amount) * NwTransitTripCosts[0];
  std::deque<NwTripData> path;
  NwTripData next;
  bool previousSubway = false;
  u32 x0 = trip->x0, z0 = trip->z0, x1 = trip->x1, z1 = trip->z1;
  u32 rows = z1 - z0 + 1;
  i8 volume = i8(u8(u32(NwTransitTripVolumePerCell) * rows * (x1 - x0 + 1)));
  u8 zero = 0;
  grid120->Fill(&zero);
  for (u32 x = x0; x <= x1; ++x)
    memset(&grid120->cells[x][z0], trip->mode, rows);
  for (;;) {
    if (GetFlag17()) {
      bool stationMode = trip->mode == 65 || trip->mode == 66 || trip->mode == 80;
      u16 mask = flag ? (stationMode ? 65504 : 31) : (stationMode ? 65518 : 29151);
      NwShowRouteOccupant(trip->occupant, mask);
    }
    if (IsReachable(trip, kind)) {
      path.push_back(*trip);
      FillTripAvailability(x0, z0, x1, z1, 0);
      while (!path.empty()) {
        *trip = path.back();
        path.pop_back();
        if (flag && NwIsCongestionOccupant(trip->occupant)) {
          switch (trip->mode) {
          case 4:
          case 20:
            value256 = coefficient204 + value256;
            [[fallthrough]];
          case 1:
            for (u32 x = trip->x0; x <= trip->x1; ++x)
              for (u32 z = trip->z0; z <= trip->z1; ++z)
                AddCongestion(x, z, volume, 21);
            break;
          case 2:
            for (u32 x = trip->x0; x <= trip->x1; ++x)
              for (u32 z = trip->z0; z <= trip->z1; ++z)
                AddCongestion(x, z, volume, 2);
            break;
          case 48:
            value256 = coefficient204 + value256;
            [[fallthrough]];
          case 24:
            for (u32 x = trip->x0; x <= trip->x1; ++x)
              for (u32 z = trip->z0; z <= trip->z1; ++z)
                AddCongestion(x, z, volume, 21);
            [[fallthrough]];
          case 8:
            value256 = coefficient208 + value256;
            for (u32 x = trip->x0; x <= trip->x1; ++x)
              for (u32 z = trip->z0; z <= trip->z1; ++z)
                AddCongestion(x, z, volume, 8);
            break;
          case 65:
          case 66:
            value256 = coefficient212 + value256;
            break;
          case 80:
            value256 = coefficient212 + value256;
            for (u32 x = trip->x0; x <= trip->x1; ++x)
              for (u32 z = trip->z0; z <= trip->z1; ++z)
                AddCongestion(x, z, volume, 21);
            break;
          }
        }
        if (GetFlag17()) {
          NwShowRouteOccupant(trip->occupant, flag ? 63488 : 63950);
          NwClearRouteOccupant(trip->occupant);
        }
        if (trip->occupant)
          trip->occupant->Release();
      }
      return 1;
    }
    if (previousSubway && trip->mode == 24)
      trip->mode = 8;
    path.push_back(*trip);
    bool found = SelectNextCell(trip, &next, cost, first, second, last) != 0;
    if (!found) {
      while (!path.empty()) {
        *trip = path.back();
        if (trip->value29 && SelectNextCell(trip, &next, cost, first, second, last)) {
          found = true;
          break;
        }
        path.pop_back();
        if (GetFlag17())
          NwClearRouteOccupant(trip->occupant);
        if (trip->occupant)
          trip->occupant->Release();
      }
      if (!found) {
        FillTripAvailability(x0, z0, x1, z1, 1);
        ++roadUsage;
        return 0;
      }
    }
    previousSubway = trip->mode == 8;
    *trip = next;
  }
}

NwTransitRefCount::NwTransitRefCount() : references(0) {
}

NwTransitRefCount::~NwTransitRefCount() {
}

// Release through the actual nondeleting virtual operation, then destroy only on its zero result.
u32 NwTransitRefCount::Release() {
  u32 remaining = RemoveRef();
  if (!remaining)
    delete this;
  return remaining;
}

u32 NwTransitRefCount::AddRef() {
  NwGetTransitDirector()->AddRef();
  return ++references;
}

// Drop the director hold before decrementing the live nonzero count; this operation never deletes the receiver.
u32 NwTransitRefCount::RemoveRef() {
  if (references) {
    NwGetTransitDirector()->RemoveRef();
    --references;
  }
  return references;
}

NwTrafficHistory::NwTrafficHistory(u32 initial)
    : key(0), group(0), value(0), limit(initial), autoScale(0), minimum(10), factor(1.0f), series0(12, initial),
      series1(10, initial), series2(10, initial) {
}

NwTrafficHistory::~NwTrafficHistory() {
}

// Resolve only Unknown and the actual private history IID, clearing the output.
bool NwTrafficHistory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScGraphInfoView);
  HANDLE_QUERY_INTERFACE(IID_ScGraphInfoView, ScGraphInfoView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwTrafficHistory::AddRef() {
  return NwTransitRefCount::AddRef();
}

u32 NwTrafficHistory::Release() {
  return NwTransitRefCount::Release();
}

// Return the borrowed graph title String.
const ScStringView *NwTrafficHistory::GetSelectionLabel() const {
  return &name;
}

// Return the selected borrowed label; every index other than zero or one selects the third String.
const ScStringView *NwTrafficHistory::GetCurrentValueLabel(ScGraphDataSet set) const {
  return set == ScGraphDataSet::Month ? &label0 : set == ScGraphDataSet::Year ? &label1 : &label2;
}

// Expose the genuine mutable graph-key word rather than copying it into an invented result aggregate.
const u32 *NwTrafficHistory::GetSelectionLabelID() const {
  return &key;
}

// Return the address of the independent graph-group word.
const u32 *NwTrafficHistory::GetValueLabelID() const {
  return &group;
}

u32 NwTrafficHistory::GetLineColor() const {
  return value;
}

u32 NwTrafficHistory::GetMaxValue() const {
  return limit;
}

u8 NwTrafficHistory::GetMonthData(u32 *out) {
  *out = series0[0];
  return 1;
}

// Copy all12 entries of series0 in index order without changing the deque.
u8 NwTrafficHistory::GetMonthDataSet(u32 *out) const {
  for (u32 i = 0; i < 12; ++i)
    *out++ = series0[i];
  return 1;
}

// Accumulate series0 with the original DWORD wrap and divide the unsigned total by12.
u32 NwTrafficHistory::GetMonthAverage() const {
  u32 total = 0;
  for (u32 i = 0; i < 12; ++i)
    total += series0[i];
  return total / 12;
}

u8 NwTrafficHistory::GetYearData(u32 *out) {
  *out = series1[0];
  return 1;
}

// Copy all10 entries of series1 in index order without changing the deque.
u8 NwTrafficHistory::GetYearDataSet(u32 *out) const {
  for (u32 i = 0; i < 10; ++i)
    *out++ = series1[i];
  return 1;
}

// Accumulate series1 with the original DWORD wrap and divide the unsigned total by10.
u32 NwTrafficHistory::GetYearAverage() const {
  u32 total = 0;
  for (u32 i = 0; i < 10; ++i)
    total += series1[i];
  return total / 10;
}

u8 NwTrafficHistory::GetDecadeData(u32 *out) {
  *out = series2[0];
  return 1;
}

// Copy all10 entries of series2 in index order without changing the deque.
u8 NwTrafficHistory::GetDecadeDataSet(u32 *out) const {
  for (u32 i = 0; i < 10; ++i)
    *out++ = series2[i];
  return 1;
}

// Accumulate series2 with the original DWORD wrap and divide the unsigned total by10.
u32 NwTrafficHistory::GetDecadeAverage() const {
  u32 total = 0;
  for (u32 i = 0; i < 10; ++i)
    total += series2[i];
  return total / 10;
}

// Recompute an automatic limit or clamp each history entry to the fixed limit, reporting whether anything changed.
u8 NwTrafficHistory::VerifyData() {
  u32 oldLimit = limit;
  if (autoScale) {
    RecomputeLimit();
    return oldLimit == limit;
  }
  u8 unchanged = 1;
  for (u32 i = 0; i < 12; ++i) {
    if (series0[i] > limit) {
      series0[i] = limit;
      unchanged = 0;
    }
  }
  for (u32 i = 0; i < 10; ++i) {
    if (series1[i] > limit) {
      series1[i] = limit;
      unchanged = 0;
    }
  }
  for (u32 i = 0; i < 10; ++i) {
    if (series2[i] > limit) {
      series2[i] = limit;
      unchanged = 0;
    }
  }
  return unchanged;
}

// Copy the title through the original temporary String lifetime before assigning the owned name.
u8 NwTrafficHistory::SetSelectionLabel(const ScStringView &text) {
  ScString temporary(text);
  name = temporary;
  return 1;
}

// Update only labels zero through two, preserving the refusal for unsupported indices and the temporary String copy.
u8 NwTrafficHistory::SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &text) {
  u32 index = static_cast<u32>(set);
  if (index > 2)
    return 0;
  ScString temporary(text);
  if (index == 0)
    label0 = temporary;
  else if (index == 1)
    label1 = temporary;
  else
    label2 = temporary;
  return 1;
}

u8 NwTrafficHistory::SetSelectionLabelID(const u32 &input) {
  key = input;
  return 1;
}

u8 NwTrafficHistory::SetValueLabelID(const u32 &input) {
  group = input;
  return 1;
}

u8 NwTrafficHistory::SetColor(u32 input) {
  value = input;
  return 1;
}

u8 NwTrafficHistory::SetMaxValue(u32 input) {
  limit = input;
  return 1;
}

u8 NwTrafficHistory::SetAutoMax(u8 input) {
  autoScale = input;
  return input;
}

void NwTrafficHistory::SetAutoMaxFactors(u32 floor, f32 scale) {
  minimum = floor;
  factor = scale >= 1.0f ? scale : 1.0f;
  RecomputeLimit();
}

// Find the unsigned maximum across all three histories, apply the original floating scale and round through its bias.
// Store at least the configured minimum while returning the rounded value before that minimum is applied.
u32 NwTrafficHistory::RecomputeLimit() {
  u32 maximum = 0;
  for (u32 i = 0; i < 12; ++i)
    if (series0[i] > maximum)
      maximum = series0[i];
  for (u32 i = 0; i < 10; ++i)
    if (series1[i] > maximum)
      maximum = series1[i];
  for (u32 i = 0; i < 10; ++i)
    if (series2[i] > maximum)
      maximum = series2[i];
  f64 scaled = f64(maximum) * factor;
  if (scaled > 4294967296.0)
    scaled = 4294967296.0;
  f64 rounded = scaled + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  u32 result = u32(bits) + kScDoubleIntegerLowWordBias;
  limit = result < minimum ? minimum : result;
  return result;
}

// Update the limit before inserting the new front of series0, then discard its oldest back entry.
u8 NwTrafficHistory::SetMonthData(u32 input) {
  if (autoScale) {
    f64 rounded = f64(limit) / factor + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &rounded, sizeof(bits));
    if (input > u32(bits) + kScDoubleIntegerLowWordBias)
      RecomputeLimit();
  } else if (input > limit) {
    input = limit;
  }
  series0.push_front(input);
  series0.pop_back();
  return 1;
}

// Replace all12 entries of series0 before automatic recomputation or virtual fixed-limit clamping.
u8 NwTrafficHistory::SetMonthDataSet(u32 *values) {
  for (u32 i = 0; i < 12; ++i)
    series0[i] = *values++;
  if (autoScale)
    RecomputeLimit();
  else
    VerifyData();
  return 1;
}

// Update the limit before inserting the new front of series1, then discard its oldest back entry.
u8 NwTrafficHistory::SetYearData(u32 input) {
  if (autoScale)
    RecomputeLimit();
  else if (input > limit)
    input = limit;
  series1.push_front(input);
  series1.pop_back();
  return 1;
}

// Replace all10 entries of series1 before automatic recomputation or virtual fixed-limit clamping.
u8 NwTrafficHistory::SetYearDataSet(u32 *values) {
  for (u32 i = 0; i < 10; ++i)
    series1[i] = *values++;
  if (autoScale)
    RecomputeLimit();
  else
    VerifyData();
  return 1;
}

// Update the limit before inserting the new front of series2, then discard its oldest back entry.
u8 NwTrafficHistory::SetDecadeData(u32 input) {
  if (autoScale) {
    f64 rounded = f64(limit) / factor + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &rounded, sizeof(bits));
    if (input > u32(bits) + kScDoubleIntegerLowWordBias)
      RecomputeLimit();
  } else if (input > limit) {
    input = limit;
  }
  series2.push_front(input);
  series2.pop_back();
  return 1;
}

// Replace all10 entries of series2 before automatic recomputation or virtual fixed-limit clamping.
u8 NwTrafficHistory::SetDecadeDataSet(u32 *values) {
  for (u32 i = 0; i < 10; ++i)
    series2[i] = *values++;
  if (autoScale)
    RecomputeLimit();
  else
    VerifyData();
  return 1;
}

// Read the three history series and limit in original order, stopping transport after refusal but always applying scale policy.
u8 NwTrafficHistory::SerialRead(CtCity *, RsStream *stream) {
  u8 result = 1;
  for (u32 i = 0; i < 12 && result; ++i)
    result = stream->ReadU32(&series0[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = stream->ReadU32(&series1[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = stream->ReadU32(&series2[i]);
  if (result)
    result = stream->ReadU32(&limit);
  if (autoScale)
    RecomputeLimit();
  else
    VerifyData();
  return result;
}

// Write all three series followed by the limit, preserving the first refused transport reply.
u8 NwTrafficHistory::SerialWrite(CtCity *, RsStream *stream) const {
  u8 result = 1;
  for (u32 i = 0; i < 12 && result; ++i)
    result = stream->WriteU32(series0[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = stream->WriteU32(series1[i]);
  for (u32 i = 0; i < 10 && result; ++i)
    result = stream->WriteU32(series2[i]);
  if (result)
    result = stream->WriteU32(limit);
  return result;
}

// Copy the original rectangle, extend signed reversed maxima to their minima and mark the scan dirty.
void NwRouteOptions::SetBounds(const GeCellRect *bounds) {
  x0 = bounds->left;
  z0 = bounds->top;
  x1 = bounds->right;
  z1 = bounds->bottom;
  if (i32(x1) < i32(x0))
    x1 = x0;
  if (i32(z1) < i32(z0))
    z1 = z0;
  dirty = 1;
}

// Shuffle the original packed coordinates from the second entry onward using the shared C random stream.
static void NwShuffleRouteCells(u16 *begin, u16 *end) {
  if (begin != end) {
    for (u16 *current = begin + 1; current != end; ++current) {
      u16 *selected = begin + ScRand() % (current - begin + 1);
      u16 value = *current;
      *current = *selected;
      *selected = value;
    }
  }
}

static const u16 kNwRouteStrides[] = {1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73,
    79, 83, 89, 97, 101, 103, 107, 109, 113, 179, 211, 257, 307, 373, 479, 577, 641, 719, 857, 977, 1063, 1237, 1471,
    1663, 1993, 2221, 2539, 2909, 3259, 3733, 4507, 5281, 5861, 6481, 6947, 7573, 9013, 10103, 12227, 15683, 17431,
    19801, 21569, 23633, 26003, 27529, 29683, 32051, 32611};

// Prepare either the original packed-coordinate vector or its signed-count coprime-stride walk.
void NwRouteOptions::Prepare(u8 shuffle) {
  state = 1;
  index = 0;
  count = (x1 - x0 + 1) * (z1 - z0 + 1);
  if (!mode) {
    if (entries.size != count) {
      entries.Reserve(count);
      entries.size = count;
      dirty = 1;
    }
    if (dirty) {
      u32 offset = 0;
      for (u32 x = x0; i32(x) <= i32(x1); ++x)
        for (u32 z = z0; i32(z) <= i32(z1); ++z)
          entries.data[offset++] = u16((u32(u8(x)) << 8) | u8(z));
    }
    if (shuffle && entries.size)
      NwShuffleRouteCells(entries.data, entries.data + entries.size);
  } else {
    u32 slot;
    i32 signedCount = i32(count);
    if (signedCount < 29)
      slot = 0;
    else if (signedCount < 71)
      slot = 10;
    else if (signedCount < 113)
      slot = 20;
    else if (signedCount < 857)
      slot = 30;
    else if (signedCount < 3259)
      slot = 40;
    else if (signedCount < 12227)
      slot = 50;
    else {
      if (signedCount >= kScPermutationMaximumCount) {
        count = kScPermutationMaximumCount;
        signedCount = kScPermutationMaximumCount;
      }
      slot = 60;
    }
    while (i32(kNwRouteStrides[slot]) < signedCount / 4) {
      ++slot;
      assert(slot < sizeof(kNwRouteStrides) / sizeof(*kNwRouteStrides));
    }
    while (signedCount % i32(kNwRouteStrides[slot]) == 0) {
      ++slot;
      assert(slot < sizeof(kNwRouteStrides) / sizeof(*kNwRouteStrides));
    }
    stride = kNwRouteStrides[slot];
    position = u32(ScRand() % signedCount);
  }
}

// Advance the chosen scan with signed stride arithmetic and publish X/Z, marking exhaustion on the last successful cell.
u8 NwRouteOptions::Next(u32 *x, u32 *z) {
  if (state == 2)
    return 0;
  if (mode) {
    ++index;
    if (i32(index) > i32(count)) {
      state = 2;
      return 0;
    }
    i64 dividend = i32(position + stride), divisor = i32(count);
    assert(divisor && (dividend != INT32_MIN || divisor != -1));
    position = u32(i32(dividend % divisor));
    divisor = i32(x1 - x0 + 1);
    assert(divisor && (i32(position) != INT32_MIN || divisor != -1));
    *x = x0 + u32(i32(i64(i32(position)) % divisor));
    divisor = i32(x1 - x0 + 1);
    assert(divisor && (i32(position) != INT32_MIN || divisor != -1));
    *z = z0 + u32(i32(i64(i32(position)) / divisor));
    if (index == count)
      state = 2;
    return 1;
  }
  if (index >= entries.size) {
    state = 2;
    return 0;
  }
  u16 packed = entries.data[index++];
  *x = packed >> 8;
  *z = u8(packed);
  if (index == entries.size)
    state = 2;
  return 1;
}

// Process signed descending columns through ReadBytes, accumulating failures without stopping.
u8 NwByteGrid::Load(RsStream *stream) {
  u8 result = 1;
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!stream->ReadBytes(cells[x], height))
      result = 0;
  return result;
}

// Process signed descending columns through WriteBytes, accumulating failures without stopping.
u8 NwByteGrid::Save(RsStream *stream) {
  u8 result = 1;
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!stream->WriteBytes(cells[x], height))
      result = 0;
  return result;
}

// Capture the packed word count once, then read signed descending columns while accumulating failures.
u8 NwBitGrid::Load(RsStream *stream) {
  u8 result = 1;
  u32 count = (height >> 5) + ((height & 31) != 0);
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!stream->ReadU32Array(words[x], count))
      result = 0;
  return result;
}

// Capture the packed word count once, then write signed descending columns while accumulating failures.
u8 NwBitGrid::Save(RsStream *stream) {
  u8 result = 1;
  u32 count = (height >> 5) + ((height & 31) != 0);
  for (i32 x = i32(width - 1); x >= 0; --x)
    if (!stream->WriteU32Array(words[x], count))
      result = 0;
  return result;
}

// Reject already-visited requested modes, then publish actual network flags and a usable road/rail density and mode.
// Failed probes stamp the requested bits into the marker grid while leaving unused outputs untouched.
u8 NwTransitLayer::ProbeRouteCell(u8 requested, u32 x, u32 z, u8 *volume, u8 *resultMode, u16 *flags) {
  u8 visited = grid120->cells[x][z];
  if ((requested & visited) == requested)
    return 0;
  *flags = grid92->cells[x][z];
  if (!(*flags & 0x10)) {
    if (requested == 3 && (*flags & 1)) {
      *volume = grid108->cells[x][z];
      *resultMode = u8((*flags & 0x40) | 0x20) >> 5;
      return 1;
    }
    if ((requested == 2 || requested == 3) && (*flags & 2)) {
      *volume = grid112->cells[x][z];
      *resultMode = u8(u8(*flags) | 0xbf) >> 6;
      return 1;
    }
    if (requested == 67 && (*flags & 1)) {
      *volume = grid108->cells[x][z];
      *resultMode = (u8(*flags) >> 5 & 2) | 0x41;
      return 1;
    }
    if ((requested == 66 || requested == 67) && (*flags & 2)) {
      *volume = grid112->cells[x][z];
      *resultMode = (u8(*flags) >> 6) | 0x42;
      return 1;
    }
  }
  grid120->cells[x][z] = requested | visited;
  return 0;
}

// Search the original clipped neighborhood for road or highway network bits, refusing an origin outside the city.
u8 NwTransitLayer::HasNetworkWithin(const u32 *point, i32 radius) {
  u32 x = point[0], z = point[1];
  if (x >= width || z >= height)
    return 0;
  u32 left = x - u32(radius) - 1, top = z - u32(radius) - 1;
  if (i32(left) < 0)
    left = 0;
  if (i32(top) < 0)
    top = 0;
  u32 right = x + u32(radius) + 1, bottom = z + u32(radius) + 1;
  if (right >= width)
    right = width - 1;
  if (bottom >= height)
    bottom = height - 1;
  for (u32 column = left; column <= right; ++column)
    for (u32 row = top; row <= bottom; ++row)
      if (grid92->cells[column][row] & 0x11)
        return 1;
  return 0;
}

u32 NwTransitLayer::FactoryValue() {
  return 0;
}

u8 NwTransitLayer::FactoryOperation(u32, u32) {
  return 0;
}

// Retain the original city dependencies in callback order, allocate the live grids and publish the traffic graph and cheat.
// Failed initialization still performs the original shutdown before setting the initialized byte.
bool NwTransitLayer::Init(CtCity *value) {
  bool result = true;
  if (initialized)
    Shutdown();
  NwTransitActiveLayer = this;
  if (value) {
    city = value;
    value->AddRef();
  } else {
    result = false;
  }
  budget = city->BudgetLayer();
  if (budget)
    budget->AddRef();
  else
    result = false;
  neighbors = city->NeighborsLayer();
  if (neighbors) {
    neighbors->AddRef();
    if (result)
      surface = city->SurfaceOccupantManager();
  } else {
    result = false;
  }
  if (surface) {
    surface->AddRef();
    if (result)
      underground = city->UndergroundOccupantManagerLevelTwo();
  } else {
    result = false;
  }
  if (underground) {
    underground->AddRef();
    if (result)
      ordinances = city->OrdinanceLayer();
  } else {
    result = false;
  }
  if (ordinances) {
    ordinances->AddRef();
    if (result)
      network = reinterpret_cast<NwManager *>(city->TransitLayer());
  } else {
    result = false;
  }
  if (network)
    network->AddRef();
  else
    result = false;
  u32 cellsX = city->CellCountX(), cellsZ = city->CellCountZ();
  if (result) {
    u8 zero = 0;
    u16 zeroWord = 0;
    grid96 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid100 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid104 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid108 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid112 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid116 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid92 = new NwWordGrid(cellsX, cellsZ, &zeroWord);
    grid120 = new NwByteGrid(cellsX, cellsZ, &zero);
    grid128 = new NwBitGrid(cellsX, cellsZ, &zero);
    grid124 = new NwBitGrid(cellsX, cellsZ, &zero);
    grid132 = new NwBitGrid(cellsX, cellsZ, &zero);
    grid136 = new NwBitGrid(cellsX, cellsZ, &zero);
    grid140 = new NwBitGrid(cellsX, cellsZ, &zero);
    height = cellsZ;
    width = cellsX;
    maxDimension = width < height ? height : width;
  }
  lock->Lock();
  upkeep.Clear();
  lock->Unlock();
  count220 = 0;
  count224 = 0;
  history = ScAddRefNotNull(new NwTrafficHistory(0));
  history->SetMaxValue(220);
  {
    RsTransitText resource(kRsGameTrafficText, kRsGameTextGroup);
    ScString text = resource.GetText(), first(text), second(first);
    history->SetSelectionLabel(second);
  }
  {
    RsTransitText resource(kRsGameTrafficText, kRsGameTextGroup);
    ScString text = resource.GetText(), first(text), second(first);
    history->SetCurrentValueLabel(ScGraphDataSet::Month, second);
  }
  {
    RsTransitText resource(kRsGameTrafficText, kRsGameTextGroup);
    ScString text = resource.GetText(), first(text), second(first);
    history->SetCurrentValueLabel(ScGraphDataSet::Year, second);
  }
  {
    RsTransitText resource(kRsGameTrafficText, kRsGameTextGroup);
    ScString text = resource.GetText(), first(text), second(first);
    history->SetCurrentValueLabel(ScGraphDataSet::Decade, second);
  }
  static const u32 historyKey = kNwTrafficGraphSelectionId;
  history->SetSelectionLabelID(historyKey);
  u32 historyGroup = kNwTrafficGraphValueId;
  history->SetValueLabelID(historyGroup);
  history->SetColor(kNwTrafficHistoryColor);
  {
    ScString cheat("traffic_lights");
    NwGetTransitApplication()->GetCheatCodeManager()->RegisterCheatCode(kNwCheat_TrafficLights, cheat);
  }
  NwGetTransitApplication()->GetCheatCodeManager()->AddNotification(this, kNwCheat_TrafficLights);
  if (!result)
    Shutdown();
  initialized = 1;
  return result;
}

// Import the original64 traffic plane only for a matching doubled city size, repeating source cells over2x2 destinations.
// Each importer output is zeroed before the ignored read reply, as in the installed body.
bool NwTransitLayer::Init(CtCity *, ScImporter *importer) {
  if (importer->MapIsPresent(kCtLegacyXtrfPlane)) {
    u32 size = 2u * importer->GetMapSize(kCtLegacyXtrfPlane);
    if (width == size && height == size) {
      for (u32 x = 0; x < width; ++x) {
        for (u32 z = 0; z < height; ++z) {
          u8 value = 0;
          importer->GetValue(kCtLegacyXtrfPlane, x >> 1, z >> 1, &value);
          grid96->cells[x][z] = value;
          grid108->cells[x][z] = value;
        }
      }
    }
  }
  return true;
}

// Read the original versioned traffic record with ordered failure propagation and optional trace Strings.
// A rejected record is aborted, while the layer loader preserves its original unconditional true reply.
bool NwTransitLayer::Init(CtCity *value, RsDatabaseSegment *segment) {
  city = value;
  NwRecordFrame frame(value, segment, {kRsCityLayerRecordType, kNwTransitLayerType, 0});
  RsStream *stream = nullptr;
  u8 result = frame.valid;
  if (result) {
    stream = frame.Stream();
    if (!stream)
      result = 0;
    if (result) {
      if (frame.debug) {
        ScString trace;
        result = stream->ReadString(&trace);
      }
      if (result)
        result = grid96->Load(stream);
    }
  }
  if (frame.version >= 2 && result) {
    result = grid100->Load(stream);
    if (result)
      result = grid104->Load(stream);
    if (result)
      result = grid128->Load(stream);
    if (result)
      result = grid124->Load(stream);
  }
  if (frame.version >= 3 && result) {
    result = stream->ReadF32(&value256);
    if (result)
      result = stream->ReadF32(&fareIncome);
  }
  if (frame.version >= 5 && result) {
    result = stream->ReadU8(&phase);
    if (result)
      result = stream->ReadU32(&value268);
  }
  if (frame.version >= 6 && result)
    result = history->SerialRead(value, stream);
  if (frame.version >= 7 && result) {
    result = grid108->Load(stream);
    if (result)
      result = grid112->Load(stream);
    if (result)
      result = grid116->Load(stream);
  }
  if (frame.version >= 9 && result) {
    result = stream->ReadU32(&roadTotal);
    result = result && stream->ReadU32(&roadUsage) && stream->ReadF32(&trafficRatio) && stream->ReadU32(&railTotal) &&
        stream->ReadU32(&railUsage);
  }
  if (result && frame.debug) {
    ScString trace;
    result = stream->ReadString(&trace);
  }
  if (!result) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return true;
}

// Write version9 traffic state in its original grid/scalar/history order, aborting the record after any refusal.
bool NwTransitLayer::Save(CtCity *value, RsDatabaseSegment *segment) {
  NwRecordFrame frame(value, segment, {kRsCityLayerRecordType, kNwTransitLayerType, 0}, 9, 0);
  RsStream *stream = frame.Stream();
  bool result = stream && grid96->Save(stream) && grid100->Save(stream) && grid104->Save(stream) &&
      grid128->Save(stream) && grid124->Save(stream) && stream->WriteF32(value256) && stream->WriteF32(fareIncome) &&
      stream->WriteU8(phase) && stream->WriteU32(value268) && history->SerialWrite(value, stream) &&
      grid108->Save(stream) && grid112->Save(stream) && grid116->Save(stream) && stream->WriteU32(roadTotal) &&
      stream->WriteU32(roadUsage) && stream->WriteF32(trafficRatio) && stream->WriteU32(railTotal) &&
      stream->WriteU32(railUsage);
  if (!result) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return result;
}

static f32 NwTransitDecay = 0.25f, NwTransitDecayParkingFines = 0.02f, NwTransitDecayAlternateDay = 0.1f;
static f32 NwTransitDecayCarpool = 0.05f, NwTransitDecayCrossingGuards = 0.02f, NwTransitDecayTourist = 0.03f;
static f32 NwTransitUnderfundedDamage = 0.1f, NwTransitOverfundedRepair = 0.05f;
static u32 NwTransitCongestionTicks = 24;
static u8 NwTransitDensityTripChance = 30;
static f32 NwTransitDensityTripScale = 0.5f;

// Post the actual damage/repair percentage implied by road funding, preserving the original one-sided clamps.
void NwTransitLayer::UpdateTransportBudgets() {
  ScMessageServer *server = NwGetTransitMessageServer();
  f64 damage;
  if (roadMultiplier >= 1.0f) {
    if (roadMultiplier <= 1.0f)
      return;
    damage = network->GetTransitDamage();
    damage -= (f64(roadMultiplier) - 1.0) / (f64(NwTransitRoadFundingLimit) - 1.0) * NwTransitOverfundedRepair;
    if (damage < 0.0)
      damage = 0.0;
  } else {
    damage = network->GetTransitDamage();
    damage += (1.0 - f64(roadMultiplier)) * NwTransitUnderfundedDamage;
    if (damage > 1.0)
      damage = 1.0;
  }
  ScMessage message(kNwMsg_SetTransitDamage, u32(i64(damage * 100.0)), 0, 0);
  server->MessagePost(message, false);
}

// Advance the actual three-state rail-funding notification sequence, retaining counter resets and live callbacks.
// Random recovery uses the original independent transit random stream and the low-byte deficit threshold.
void NwTransitLayer::UpdateCongestionState() {
  if (phase == 0) {
    if (railFunding >= 100 || i32(count224) <= 0) {
      ScMessage message(kNwMsg_TransitStrikeEnded);
      NwGetTransitMessageServer()->MessagePost(message, false);
      phase = 2;
      return;
    }
    ScMessage message(kNwMsg_TransitStrikeStarted);
    NwGetTransitMessageServer()->MessagePost(message, false);
    phase = 1;
    value268 = 0;
    return;
  }
  if (phase == 1) {
    if (railFunding < 100 && value268 <= NwTransitCongestionTicks && i32(count224) > 0) {
      ++value268;
      return;
    }
    ScMessage message(kNwMsg_TransitStrikeEnded);
    NwGetTransitMessageServer()->MessagePost(message, false);
    phase = 2;
    value268 = 0;
    return;
  }
  if (phase == 2 && railFunding < 100 && i32(count224) > 0) {
    u8 threshold = u8(GetRailFundingDeficit());
    if (u8(NwTransitRandom->Bounded(100)) <= threshold) {
      ScMessage message(kNwMsg_TransitStrikePending);
      NwGetTransitMessageServer()->MessagePost(message, false);
      phase = 0;
    }
  }
}

// Decay the live density grids under current ordinances, recompute network averages and publish the graph histories.
void NwTransitLayer::RecomputeStats() {
  f32 road = NwTransitDecay, rail = NwTransitDecay, subway = NwTransitDecay;
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_ParkingFines))
    road = road - NwTransitDecayParkingFines;
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_AlternateDayDriving)) {
    road = road - NwTransitDecayAlternateDay;
    rail = rail - NwTransitDecayAlternateDay;
  }
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_CarpoolIncentive)) {
    road = road - NwTransitDecayCarpool;
    rail = rail - NwTransitDecayCarpool;
  }
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_CrossingGuards))
    road = NwTransitDecayCrossingGuards + road;
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_TouristPromotion)) {
    road = NwTransitDecayTourist + road;
    rail = NwTransitDecayTourist + rail;
    subway = NwTransitDecayTourist + subway;
  }
  u32 firstTotal = 0, secondTotal = 0, highwayCells = 0;
  for (u32 x = 0; x < width; ++x) {
    for (u32 z = 0; z < height; ++z) {
      u16 flags = grid92->cells[x][z];
      u8 roadDensity = u8(i64(f64(grid108->cells[x][z]) * ((flags & 1) && !(flags & 0x10) ? road : subway)));
      grid96->cells[x][z] = roadDensity;
      grid108->cells[x][z] = roadDensity;
      u8 railDensity = u8(i64(f64(grid112->cells[x][z]) * rail));
      grid100->cells[x][z] = railDensity;
      grid112->cells[x][z] = railDensity;
      u8 subwayDensity = u8(i64(f64(grid116->cells[x][z]) * subway));
      grid104->cells[x][z] = subwayDensity;
      grid116->cells[x][z] = subwayDensity;
      firstTotal += (flags & 1) && !(flags & 0x10) && roadDensity > railDensity ? roadDensity : railDensity;
      if ((flags & 0x14) && roadDensity > subwayDensity) {
        secondTotal += roadDensity;
        if (flags & 0x10)
          ++highwayCells;
      } else {
        secondTotal += subwayDensity;
      }
    }
  }
  NwManager *currentNetwork = network;
  u32 highwayCount = currentNetwork->GetHighwayCount();
  u32 firstCount = currentNetwork->GetRoadCount() + highwayCount;
  railUsage = firstCount ? firstTotal / firstCount : 0;
  currentNetwork = network;
  u32 subwayCount = currentNetwork->GetSubwayCount();
  u32 secondCount = subwayCount + highwayCells + currentNetwork->GetRailCount();
  railTotal = secondCount ? secondTotal / secondCount : 0;
  if (roadUsage)
    trafficRatio = f32(f64(roadUsage) / f64(roadTotal));
  roadTotal = 0;
  roadUsage = 0;
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  history->SetMonthData(railUsage);
  if (month == 1) {
    history->GetMonthAverage();
    history->SetYearData(railUsage);
    if (year % 10 == 0) {
      history->GetYearAverage();
      history->SetDecadeData(railUsage);
    }
  }
}

static u8 SC3_STDCALL NwClassifyCoverageOccupant(ScOccupant *occupant, u8 *out) {
  if (occupant && occupant->MajorType() == 1) {
    CtAgentType *type = occupant->CityAgentType();
    if (type)
      return NwClassifyTripType(type, out);
  }
  return 0;
}

// Stamp the applicable station catchment using the original category radius and clipped occupant bounds.
void NwTransitLayer::StampCoverage(ScOccupant *occupant) {
  u8 category;
  if (!NwClassifyCoverageOccupant(occupant, &category))
    return;
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  if (category >= 3)
    return;
  u32 radius = category == 0 ? roadDistanceRes : category == 1 ? roadDistanceCom : roadDistanceInd;
  u32 left = u32(bounds.minimum.x >> kCtMicroCoordinateShift) - radius,
      top = u32(bounds.minimum.z >> kCtMicroCoordinateShift) - radius;
  if (i32(left) < 0)
    left = 0;
  if (i32(top) < 0)
    top = 0;
  u32 right = u32(bounds.maximum.x >> kCtMicroCoordinateShift) + radius,
      bottom = u32(bounds.maximum.z >> kCtMicroCoordinateShift) + radius;
  if (right > width - 1)
    right = width - 1;
  if (bottom > height - 1)
    bottom = height - 1;
  NwBitGrid *grid = category == 0 ? grid132 : category == 1 ? grid136 : grid140;
  u8 set = 1;
  for (u32 x = left; x <= right; ++x)
    for (u32 z = top; z <= bottom; ++z)
      grid->Set(x, z, &set);
}

// Clear the three coverage planes in order and restamp occupied building views reached by the real surface iterator.
// Iterator initialization/current/next/shutdown and retained building-view releases keep the original callback order.
void NwTransitLayer::RebuildCoverage(u32 x0, u32 z0, u32 x1, u32 z1) {
  u8 zero = 0;
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid132->Set(x, z, &zero);
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid136->Set(x, z, &zero);
  for (u32 x = x0; x <= x1; ++x)
    for (u32 z = z0; z <= z1; ++z)
      grid140->Set(x, z, &zero);
  GeBounds bounds{{i32(x0 << kCtMicroCoordinateShift), i32(z0 << kCtMicroCoordinateShift), 0},
      {i32(x1 << kCtMicroCoordinateShift), i32(z1 << kCtMicroCoordinateShift), 0}};
  OcIterator *iterator = nullptr;
  surface->GetIterator(&iterator);
  iterator->Init(&bounds, nullptr);
  for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next()) {
    void *selected = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, &selected)) {
      BdBuilding *building = static_cast<BdBuilding *>(selected);
      if (building->GetOccupancyState() & 1)
        StampCoverage(occupant);
      building->Release();
    }
  }
  iterator->Shutdown();
  iterator->Release();
}

// Issue the actual weekly trips for the matching day phase, retaining threshold asymmetry and live singleton rereads.
// Quantity selection remains a direct missing method; pending option words are genuine trailing byte arguments.
bool NwTripManipulator::Apply(CtAgent *agent) {
  if (!NwTransitActiveLayer)
    return true;
  u32 month, day, year;
  NwTransitActiveLayer->city->GetDate(&month, &day, &year);
  if (day / 7 != phase)
    return true;
  void *selected = nullptr;
  if (!agent->QueryInterface(IID_ScOccupant, &selected))
    return true;
  ScOccupant *occupant = static_cast<ScOccupant *>(selected);
  u32 tripKind;
  if (NwTransitActiveLayer->SelectTripKind(kind, &tripKind)) {
    u8 chance = u8(NwTransitRandom->Bounded(100));
    CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
    occupant->GetLocation(&location);
    if (chance <= NwTransitActiveLayer->tripThresholdPercent) {
      u8 special = 0;
      if (occupant->MajorType() == 1) {
        void *buildingView = nullptr;
        if (occupant->QueryInterface(IID_BdBuilding, &buildingView)) {
          BdBuilding *building = static_cast<BdBuilding *>(buildingView);
          if (building->GetOccupancyState() & 1) {
            u8 random = u8(NwTransitRandom->Bounded(100)), density;
            NwTransitActiveLayer->valves->GetDensity(
                u32(location.x >> kCtMicroCoordinateShift), u32(location.z >> kCtMicroCoordinateShift), &density);
            u8 threshold = u8(i64(f64(density) * NwTransitDensityTripScale + NwTransitDensityTripChance));
            if (random <= threshold)
              special = 1;
          }
          building->Release();
        }
      }
      if (flag15 && NwTransitRandom->Bounded(100) > NwTransitActiveLayer->massTransitChance) {
        i32 distance = i32(NwTransitActiveLayer->GetMaxDimension());
        NwTransitActiveLayer->RouteOccupantTrip(occupant, tripKind, distance, special, 0, 0, 0);
      } else {
        u8 first = flag12, second = flag13, last = flag14;
        i32 distance = i32(NwTransitActiveLayer->GetMaxDimension());
        NwTransitActiveLayer->RouteOccupantTrip(occupant, tripKind, distance, special, first, second, last);
      }
    } else if (NwTransitActiveLayer->IsTripAvailable(
                   u32(location.x >> kCtMicroCoordinateShift), u32(location.z >> kCtMicroCoordinateShift)) == 2) {
      if (flag15 && NwTransitRandom->Bounded(100) >= NwTransitActiveLayer->massTransitChance) {
        i32 distance = i32(NwTransitActiveLayer->GetMaxDimension());
        NwTransitActiveLayer->RouteOccupantTrip(occupant, tripKind, distance, 0, 0, 0, 0);
      } else {
        u8 first = flag12, second = flag13, last = flag14;
        i32 distance = i32(NwTransitActiveLayer->GetMaxDimension());
        NwTransitActiveLayer->RouteOccupantTrip(occupant, tripKind, distance, 0, first, second, last);
      }
    }
  }
  occupant->Release();
  return true;
}

// Dispatch real query submessages, publish the history collection and hand the retained traffic raster to its query owner.
// Unsupported submessages and failed interface requests preserve the original acknowledged reply.
u8 NwTransitLayer::HandleQueryMessage(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagGraphs:
    if (source && source->QueryInterface(IID_UiGraphsViewIdentity, &selected)) {
      static_cast<ScObjectCollection *>(selected)->Add(history);
      static_cast<ScUnknown *>(selected)->Release();
    }
    break;
  case kUiQueryTagDialog:
  case kUiQueryTagDebug:
    if (source && source->QueryInterface(
        u32(message->data1) == kUiQueryTagDialog ? IID_UiQueryWindow : IID_UiQuerySection, &selected))
      static_cast<ScUnknown *>(selected)->Release();
    break;
  case kUiQueryTagMaps:
    if (source && source->QueryInterface(IID_UiRasterQuery, &selected)) {
      void *raster = nullptr;
      if (CreateAgent(kUiTrafficMapRasterId, IID_GxRasterSource, &raster))
        static_cast<UiRasterQuery *>(selected)->AddRaster(static_cast<GxRasterSource *>(raster));
      if (raster) {
        static_cast<ScUnknown *>(raster)->Release();
        raster = nullptr;
      }
      static_cast<ScUnknown *>(selected)->Release();
    }
    break;
  case kUiQueryTagStandard:
    if (source && source->QueryInterface(IID_UiQuerySection, &selected)) {
      QueryTrafficTile(message, static_cast<UiQuery *>(selected));
      static_cast<ScUnknown *>(selected)->Release();
    }
    break;
  }
  return 1;
}

// Build the original location-specific traffic query rows through the actual value formatter and query interface.
// Location refusal produces no rows; each successful row retains its raw warning byte and category mask.
u8 NwTransitLayer::QueryTrafficTile(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, text;
  u8 warning = 0;
  if (!query || !query->GetLocation(&location))
    return 0;
  for (u32 index = 0; index < QueryTrafficValueCount(); ++index)
    if (QueryTrafficValue(u32(location.x >> kCtMicroCoordinateShift), u32(location.z >> kCtMicroCoordinateShift),
            &label, &text, index, &warning))
      query->AddText(&label, &text, kUiQueryPrimaryDetailsCategory, warning);
  return 1;
}

u32 NwTransitLayer::QueryTrafficValueCount() {
  return 1;
}

// Parse the original unsigned tuning number, selecting hexadecimal for a prefix or any ASCII A-F digit.
// Preserve32-bit Windows overflow even when the host C runtime uses a wider unsigned long.
static u32 NwParseTransitNumber(const char *text) {
  i32 radix = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
  } else {
    for (const char *p = text; *p; ++p) {
      if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
        radix = 16;
        break;
      }
    }
  }
#if ULONG_MAX == UINT32_MAX
  return u32(strtoul(text, nullptr, radix));
#else
  const char *digits = text;
  while (isspace(u8(*digits)))
    ++digits;
  bool negative = *digits == '-';
  if (*digits == '-' || *digits == '+')
    ++digits;
  if (*digits == '-' || *digits == '+' || isspace(u8(*digits)))
    return 0;
  int previousError = errno;
  errno = 0;
  u64 magnitude = strtoull(digits, nullptr, radix);
  if (errno == ERANGE || magnitude > UINT32_MAX) {
    errno = ERANGE;
    return UINT32_MAX;
  }
  errno = previousError;
  return negative ? 0u - u32(magnitude) : u32(magnitude);
#endif
}

static bool NwReadTransitSetting(GeIni *ini, const char *section, const char *key, ScString *out) {
  ScString entry(key), group(section);
  return ini->ReadEntry(group, entry, out);
}

// Read the installed traffic tuning keys in original order, retaining defaults for absent scalar entries.
// Malformed destination triples take the original fallback without reproducing its transient unwritten-local reads.
u8 NwLoadTransitTunables() {
  ScString path, value;
  GeIni ini;
  NwGetTransitApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\STTraffic.INI");
  ini.SetPath(path);
  NwGetTransitApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseRoad", &value))
    NwTransitTripCosts[0] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseRoadBus", &value))
    NwTransitTripCosts[1] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseHighway", &value))
    NwTransitTripCosts[2] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseHighwayBus", &value))
    NwTransitTripCosts[3] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseBusStop", &value))
    NwTransitTripCosts[4] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseRailStation", &value))
    NwTransitTripCosts[5] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseSubwayRailStation", &value))
    NwTransitTripCosts[6] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseSubwayStation", &value))
    NwTransitTripCosts[7] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseRail", &value))
    NwTransitTripCosts[8] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripCellCostBaseSubway", &value))
    NwTransitTripCosts[9] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripMaxDistanceToRoadRes", &value))
    NwTransitRoadDistanceRes = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripMaxDistanceToRoadCom", &value))
    NwTransitRoadDistanceCom = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripMaxDistanceToRoadInd", &value))
    NwTransitRoadDistanceInd = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripMaxDistanceToRoadDefault", &value))
    NwTransitRoadDistanceInd = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripDensityAdder", &value))
    NwTransitTripVolumePerCell = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MonthlyDensityDecrement", &value))
    NwTransitDecay = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficMaxDensity", &value))
    NwTransitDensityLimit = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficAnnoyanceLevel", &value))
    NwTransitAnnoyance = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorRoad", &value))
    NwTransitDensityDivisors[0] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorRoadBus", &value))
    NwTransitDensityDivisors[1] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorHighway", &value))
    NwTransitDensityDivisors[2] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorHighwayBus", &value))
    NwTransitDensityDivisors[3] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorBusStop", &value))
    NwTransitDensityDivisors[4] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorRailStation", &value))
    NwTransitDensityDivisors[5] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorSubwayRailStation", &value))
    NwTransitDensityDivisors[6] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorSubwayStation", &value))
    NwTransitDensityDivisors[7] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorRail", &value))
    NwTransitDensityDivisors[8] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficDensityTripCostDivisorSubway", &value))
    NwTransitDensityDivisors[9] = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficTripThresholdPercent", &value))
    NwTransitTripThreshold = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripChanceCountingBase", &value))
    NwTransitDensityTripChance = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TripChanceDensityMultiplier", &value))
    NwTransitDensityTripScale = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficUseMassTransitChance", &value))
    NwTransitMassTransitChance = u8(NwParseTransitNumber(value.ToChar()));
  unsigned first, second, third;
  if (NwReadTransitSetting(&ini, "TrafficTripDestinationThresholds", "FromResidential", &value) &&
      sscanf(value.ToChar(), "%u,%u,%u", &first, &second, &third) == 3) {
    NwTransitDestinationThresholds[0][0] = u8(first);
    NwTransitDestinationThresholds[0][1] = u8(second);
    NwTransitDestinationThresholds[0][2] = u8(third);
  } else {
    NwTransitDestinationThresholds[0][0] = 10;
    NwTransitDestinationThresholds[0][1] = 40;
    NwTransitDestinationThresholds[0][2] = 100;
  }
  if (NwReadTransitSetting(&ini, "TrafficTripDestinationThresholds", "FromCommercial", &value) &&
      sscanf(value.ToChar(), "%u,%u,%u", &first, &second, &third) == 3) {
    NwTransitDestinationThresholds[1][0] = u8(first);
    NwTransitDestinationThresholds[1][1] = u8(second);
    NwTransitDestinationThresholds[1][2] = u8(third);
  } else {
    NwTransitDestinationThresholds[1][0] = 70;
    NwTransitDestinationThresholds[1][1] = 80;
    NwTransitDestinationThresholds[1][2] = 100;
  }
  if (NwReadTransitSetting(&ini, "TrafficTripDestinationThresholds", "FromIndustrial", &value) &&
      sscanf(value.ToChar(), "%u,%u,%u", &first, &second, &third) == 3) {
    NwTransitDestinationThresholds[2][0] = u8(first);
    NwTransitDestinationThresholds[2][1] = u8(second);
    NwTransitDestinationThresholds[2][2] = u8(third);
  } else {
    NwTransitDestinationThresholds[2][0] = 50;
    NwTransitDestinationThresholds[2][1] = 70;
    NwTransitDestinationThresholds[2][2] = 100;
  }
  if (NwReadTransitSetting(&ini, "TrafficTripDestinationThresholds", "FromMixedOrAll", &value) &&
      sscanf(value.ToChar(), "%u,%u,%u", &first, &second, &third) == 3) {
    NwTransitDestinationThresholds[3][0] = u8(first);
    NwTransitDestinationThresholds[3][1] = u8(second);
    NwTransitDestinationThresholds[3][2] = u8(third);
  } else {
    NwTransitDestinationThresholds[3][0] = 33;
    NwTransitDestinationThresholds[3][1] = 66;
    NwTransitDestinationThresholds[3][2] = 100;
  }
  if (NwReadTransitSetting(&ini, "TrafficLayer", "ParkingFinesRoadMonthlyDecrementSubtract", &value))
    NwTransitDecayParkingFines = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "AlternateDayCarsRoadHighwayMonthlyDecrementSubtract", &value))
    NwTransitDecayAlternateDay = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "CarpoolingRoadHighwayMonthlyDecrementSubtract", &value))
    NwTransitDecayCarpool = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "CrossingGuardsRoadMonthlyDecrementAdd", &value))
    NwTransitDecayCrossingGuards = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TouristPromotionRoadRailMonthlyDecrementAdd", &value))
    NwTransitDecayTourist = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "SubsidizedMassTransitUsePercentAdd", &value))
    NwTransitOrdinanceChance = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "SubsidizedMassTransitFaresPercentDecrease", &value))
    NwTransitOrdinanceScale = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "ShuttleServiceDistanceToRoadAdd", &value))
    NwTransitOrdinanceDistance = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MaxEfficiencyRoads", &value)) {
    f64 factor = f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f);
    NwTransitRoadFundingLimit = f32(factor);
    if (factor < 1.0)
      NwTransitRoadFundingLimit = 1.0f;
  }
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MysticalERoads", &value))
    NwTransitRoadFundingCurve = f32(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MaxEfficiencyTransit", &value)) {
    f64 factor = f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f);
    NwTransitRailFundingLimit = f32(factor);
    if (factor < 1.0)
      NwTransitRailFundingLimit = 1.0f;
  }
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MysticalETransit", &value))
    NwTransitRailFundingCurve = f32(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "OptimalMonthlyFundingCostPerRoad", &value))
    NwTransitRoadUpkeep = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "OptimalMonthlyFundingCostPerHighway", &value))
    NwTransitHighwayUpkeep = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "OptimalMonthlyFundingCostPerRail", &value))
    NwTransitRailUpkeep = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "OptimalMonthlyFundingCostPerSubway", &value))
    NwTransitSubwayUpkeep = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "PerTileTransitIncomeRail", &value))
    NwTransitCoefficient204 = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.001f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "PerTileTransitIncomeSubway", &value))
    NwTransitCoefficient208 = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.001f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "PerTileTransitIncomeBus", &value))
    NwTransitCoefficient212 = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.001f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "DamageEfficiencyMultiplier", &value))
    NwTransitUnderfundedDamage = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.001f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "RepairEfficiencyMultiplier", &value))
    NwTransitOverfundedRepair = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.001f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "DamagedCostAddToStep", &value))
    NwTransitDamageCost = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "StrikeChanceAtZeroFunding", &value))
    NwTransitDeficitScale = f32(f64(NwParseTransitNumber(value.ToChar())) * f64(0.01f));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MinFundingForNoStrike", &value))
    NwTransitMinimumRailFunding = u8(NwParseTransitNumber(value.ToChar()));
  if (NwReadTransitSetting(&ini, "TrafficLayer", "MaxLengthofStrikeInMonths", &value))
    NwTransitCongestionTicks = NwParseTransitNumber(value.ToChar());
  NwTransitReservedState = 0;
  if (NwReadTransitSetting(&ini, "TrafficLayer", "TrafficSimLobotomyDensity", &value))
    NwTransitLobotomyDensity = u8(NwParseTransitNumber(value.ToChar()));
  return 1;
}

// Recompute the signed road funding request from the live road and highway counts using the original ceiling order.
void NwTransitLayer::UpdateRoadUpkeep() {
  count220 = 0;
  u32 roads = network->GetRoadCount();
  count220 += u32(i64(ceil(f64(roads) * NwTransitRoadUpkeep)));
  u32 highways = network->GetHighwayCount();
  count220 += u32(i64(ceil(f64(highways) * NwTransitHighwayUpkeep)));
  roadBudget->SetTotalFundingNeeded(i64(i32(count220)));
}

// Add each station's live optimal upkeep while locked, then include rail and subway counts before publishing funding.
// Preserve wrapping DWORD accumulation and sign extension into the original64-bit budget setter.
void NwTransitLayer::UpdateRailUpkeep() {
  count224 = 0;
  lock->Lock();
  NwUpkeepLink *head = upkeep.head;
  for (NwUpkeepLink *link = head->next; link != head; link = link->next)
    count224 += u32(link->value->GetOptimalMonthlyUpkeep());
  lock->Unlock();
  u32 rails = network->GetRailCount();
  count224 += u32(i64(ceil(f64(rails) * NwTransitRailUpkeep)));
  u32 subways = network->GetSubwayCount();
  count224 += u32(i64(ceil(f64(subways) * NwTransitSubwayUpkeep)));
  railBudget->SetTotalFundingNeeded(i64(i32(count224)));
}

// Dispatch occupant additions/removals and the actual traffic-lights cheat, preserving message acknowledgment.
// Network stamp and clear receive their original kind constants; their result bytes are intentionally ignored here.
bool NwTransitLayer::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kOcMsg_SubwayInserted:
    StampNetwork(kOcSubwayManagerId, reinterpret_cast<ScOccupant *>(message->data1));
    return true;
  case kOcMsg_SubwayRemoved:
    ClearNetwork(kOcSubwayManagerId, reinterpret_cast<ScOccupant *>(message->data1));
    return true;
  case kOcMsg_SurfaceInserted:
    StampNetwork(kOcSurfaceManagerId, reinterpret_cast<ScOccupant *>(message->data1));
    return true;
  case kOcMsg_SurfaceRemoved:
    ClearNetwork(kOcSurfaceManagerId, reinterpret_cast<ScOccupant *>(message->data1));
    return true;
  case kWnMsg_CheatCommand:
    if (u32(message->data1) != kNwCheat_TrafficLights)
      return false;
    {
      ScString *command = reinterpret_cast<ScString *>(message->data2);
      ScString value = [&]() {
        ScString tail = command->Mid(15, kScStringToEnd);
        return ScString(tail);
      }();
      SetFlag17(strcmp(value.ToChar(), "on") == 0 || strcmp(value.ToChar(), "ON") == 0);
    }
    return true;
  }
  return false;
}

// Rebuild the removed station's clipped catchment, selecting the original default radius for the fourth category.
// The real classifier may decline an occupant without changing any grid; GetBounds retains its initialized outputs.
void NwTransitLayer::RemoveStationCoverage(ScOccupant *occupant) {
  u8 category;
  if (!occupant || !NwClassifyCoverageOccupant(occupant, &category))
    return;
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  u32 radius = category == 0 ? roadDistanceRes
      : category == 1        ? roadDistanceCom
      : category == 2        ? roadDistanceInd
                             : roadDistance200;
  u32 left = u32(bounds.minimum.x >> kCtMicroCoordinateShift) - radius,
      top = u32(bounds.minimum.z >> kCtMicroCoordinateShift) - radius;
  if (i32(left) < 0)
    left = 0;
  if (i32(top) < 0)
    top = 0;
  u32 right = u32(bounds.maximum.x >> kCtMicroCoordinateShift) + radius,
      bottom = u32(bounds.maximum.z >> kCtMicroCoordinateShift) + radius;
  if (right > width - 1)
    right = width - 1;
  if (bottom > height - 1)
    bottom = height - 1;
  RebuildCoverage(left, top, right, bottom);
}

// Stamp the occupant's actual network and neighbor flags, then update the relevant retained station or road budgets.
// Interface calls and independent geometry passes retain the original order and live receiver rereads.
u8 NwTransitLayer::StampNetwork(u32 kind, ScOccupant *occupant) {
  if (!occupant || !kind)
    return 0;
  if (kind == kOcSurfaceManagerId && occupant->MajorType() == 1) {
    void *selected = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, &selected)) {
      BdBuilding *building = static_cast<BdBuilding *>(selected);
      if (building->GetOccupancyState() & 1)
        StampCoverage(occupant);
      building->Release();
    }
  }
  if (!occupant->IsGeomFlag(kOcGeomTransitPiece))
    return 1;
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  void *selectedConnection = nullptr;
  occupant->QueryInterface(IID_NwConnection, &selectedConnection);
  NwConnection *connection = static_cast<NwConnection *>(selectedConnection);
  if (kind == kOcSurfaceManagerId) {
    u32 type = occupant->TypeId();
    if (type == kCtAgentRailStation || type == kCtAgentSubwayStation || type == kCtAgentSubwayRailStation ||
        type == kCtAgentBusStation) {
      for (u32 x = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
          x <= u32(bounds.maximum.x >> kCtMicroCoordinateShift); ++x) {
        for (u32 z = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
            z <= u32(bounds.maximum.z >> kCtMicroCoordinateShift); ++z) {
          u16 value = grid92->cells[x][z];
          if (type == kCtAgentBusStation)
            value |= kNwBusStationAccess;
          else if (type == kCtAgentRailStation)
            value |= kNwRailStationAccess;
          else if (type == kCtAgentSubwayStation)
            value |= kNwSubwayStationAccess;
          else
            value |= kNwSubwayRailStationAccess;
          grid92->cells[x][z] = value;
        }
      }
      void *selectedUpkeep = nullptr;
      occupant->QueryInterface(IID_BdUpkeepView, &selectedUpkeep);
      NwUpkeep *station = static_cast<NwUpkeep *>(selectedUpkeep);
      if (station) {
        i32 monthly = station->GetOptimalMonthlyUpkeep();
        lock->Lock();
        station->SetFundingPercentage(railFunding);
        NwUpkeepLink *head = upkeep.head, *link = new NwUpkeepLink;
        link->value = station;
        link->next = head;
        link->previous = head->previous;
        head->previous->next = link;
        head->previous = link;
        lock->Unlock();
        count224 += u32(monthly);
        railBudget->SetTotalFundingNeeded(i64(i32(count224)));
        station->Release();
      }
    }
  }
  if (occupant->IsGeomFlag(kOcGeomRoad)) {
    for (u32 x = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
        x <= u32(bounds.maximum.x >> kCtMicroCoordinateShift); ++x) {
      for (u32 z = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
          z <= u32(bounds.maximum.z >> kCtMicroCoordinateShift); ++z) {
        u16 value = grid92->cells[x][z] | 1;
        if (connection) {
          if (connection->ConnectionFlag0())
            value |= 0x100;
          if (u8(connection->NeedsRepair()))
            value |= 0x80;
          if (u8(connection->IsTeleport()))
            value |= 0x4000;
        }
        if (neighbors->HasConnection(1, i32(x), i32(z)))
          value |= 0x400;
        grid92->cells[x][z] = value;
      }
    }
    UpdateRoadUpkeep();
  }
  if (occupant->IsGeomFlag(kOcGeomHighway)) {
    for (u32 x = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
        x <= u32(bounds.maximum.x >> kCtMicroCoordinateShift); ++x) {
      for (u32 z = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
          z <= u32(bounds.maximum.z >> kCtMicroCoordinateShift); ++z) {
        u16 value = grid92->cells[x][z] | 2;
        if (connection) {
          if (connection->ConnectionFlag0())
            value |= 0x100;
          if (u8(connection->NeedsRepair()))
            value |= 0x80;
          if (u8(connection->IsTeleport()))
            value |= 0x4000;
        }
        if (neighbors->HasConnection(4, i32(x), i32(z)))
          value |= 0x800;
        grid92->cells[x][z] = value;
      }
    }
    UpdateRoadUpkeep();
  }
  if (occupant->IsGeomFlag(kOcGeomHighwayOnRamp))
    for (u32 x = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
        x <= u32(bounds.maximum.x >> kCtMicroCoordinateShift); ++x)
      for (u32 z = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
          z <= u32(bounds.maximum.z >> kCtMicroCoordinateShift); ++z)
        grid92->cells[x][z] |= 0x40;
  if (occupant->IsGeomFlag(kOcGeomRail)) {
    for (u32 x = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
        x <= u32(bounds.maximum.x >> kCtMicroCoordinateShift); ++x) {
      for (u32 z = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
          z <= u32(bounds.maximum.z >> kCtMicroCoordinateShift); ++z) {
        u16 value = grid92->cells[x][z] | 4;
        if (connection) {
          if (connection->ConnectionFlag0())
            value |= 0x100;
          if (u8(connection->IsTeleport()))
            value |= 0x4000;
        }
        if (neighbors->HasConnection(2, i32(x), i32(z)))
          value |= 0x1000;
        grid92->cells[x][z] = value;
      }
    }
    UpdateRailUpkeep();
  }
  if (occupant->IsGeomFlag(kOcGeomSubway)) {
    for (u32 x = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
        x <= u32(bounds.maximum.x >> kCtMicroCoordinateShift); ++x) {
      for (u32 z = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
          z <= u32(bounds.maximum.z >> kCtMicroCoordinateShift); ++z) {
        u16 value = grid92->cells[x][z] | 8;
        if (connection && connection->ConnectionFlag0())
          value |= 0x200;
        if (neighbors->HasConnection(6, i32(x), i32(z)))
          value |= 0x2000;
        grid92->cells[x][z] = value;
      }
    }
    UpdateRailUpkeep();
  }
  if (connection)
    connection->Release();
  return 1;
}

// Clear the removed occupant's station, road, highway, rail and subway contributions in the installed pass order.
// Each density plane is cleared by whole columns before its matching budget recomputation and interface release.
u8 NwTransitLayer::ClearNetwork(u32 kind, ScOccupant *occupant) {
  if (!occupant || !kind)
    return 0;
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  occupant->GetBounds(&bounds);
  if (occupant->MajorType() == 1) {
    void *selected = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, &selected)) {
      BdBuilding *building = static_cast<BdBuilding *>(selected);
      if (building->GetOccupancyState() & 1)
        RemoveStationCoverage(occupant);
      building->Release();
    }
  }
  u32 x0, z0, x1, z1, rows;
  if (!occupant->IsGeomFlag(kOcGeomTransitPiece)) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    FillTripAvailability(x0, z0, x1, z1, 2);
    return 1;
  }
  u32 type = occupant->TypeId();
  if (type == kCtAgentRailStation || type == kCtAgentSubwayStation || type == kCtAgentSubwayRailStation ||
      type == kCtAgentBusStation) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    rows = z1 - z0 + 1;
    u16 mask = static_cast<u16>(~(type == kCtAgentBusStation ? kNwBusStationAccess
            : type == kCtAgentRailStation                    ? kNwRailStationAccess
            : type == kCtAgentSubwayStation                  ? kNwSubwayStationAccess
                                                             : kNwSubwayRailStationAccess));
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        grid92->cells[x][z] &= mask;
    for (u32 x = x0; x <= x1; ++x)
      memset(grid96->cells[x] + z0, 0, rows);
    for (u32 x = x0; x <= x1; ++x)
      memset(grid108->cells[x] + z0, 0, rows);
    void *selected = nullptr;
    occupant->QueryInterface(IID_BdUpkeepView, &selected);
    NwUpkeep *station = static_cast<NwUpkeep *>(selected);
    if (station) {
      i32 monthly = station->GetOptimalMonthlyUpkeep();
      lock->Lock();
      NwUpkeepLink *head = upkeep.head;
      for (NwUpkeepLink *link = head->next; link != head; link = link->next) {
        if (link->value == station) {
          link->previous->next = link->next;
          link->next->previous = link->previous;
          delete link;
          break;
        }
      }
      lock->Unlock();
      count224 -= u32(monthly);
      railBudget->SetTotalFundingNeeded(i64(i32(count224)));
      station->Release();
    }
  }
  if (occupant->IsGeomFlag(kOcGeomRoad)) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    rows = z1 - z0 + 1;
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        grid92->cells[x][z] &= 0xfa7e;
    for (u32 x = x0; x <= x1; ++x)
      memset(grid96->cells[x] + z0, 0, rows);
    for (u32 x = x0; x <= x1; ++x)
      memset(grid108->cells[x] + z0, 0, rows);
    UpdateRoadUpkeep();
  }
  if (occupant->IsGeomFlag(kOcGeomHighway)) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    rows = z1 - z0 + 1;
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        grid92->cells[x][z] &= 0xf67d;
    for (u32 x = x0; x <= x1; ++x)
      memset(grid100->cells[x] + z0, 0, rows);
    for (u32 x = x0; x <= x1; ++x)
      memset(grid112->cells[x] + z0, 0, rows);
    UpdateRoadUpkeep();
  }
  if (occupant->IsGeomFlag(kOcGeomHighwayOnRamp)) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        grid92->cells[x][z] &= 0xffbf;
  }
  if (occupant->IsGeomFlag(kOcGeomRail)) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    rows = z1 - z0 + 1;
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        grid92->cells[x][z] &= 0xeefb;
    for (u32 x = x0; x <= x1; ++x)
      memset(grid96->cells[x] + z0, 0, rows);
    for (u32 x = x0; x <= x1; ++x)
      memset(grid108->cells[x] + z0, 0, rows);
    UpdateRailUpkeep();
  }
  if (occupant->IsGeomFlag(kOcGeomSubway)) {
    x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
    z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
    rows = z1 - z0 + 1;
    for (u32 x = x0; x <= x1; ++x)
      for (u32 z = z0; z <= z1; ++z)
        grid92->cells[x][z] &= 0xddf7;
    for (u32 x = x0; x <= x1; ++x)
      memset(grid104->cells[x] + z0, 0, rows);
    for (u32 x = x0; x <= x1; ++x)
      memset(grid116->cells[x] + z0, 0, rows);
    UpdateRailUpkeep();
  }
  return 1;
}

// Borrow the current city view through the actual application and window, returning null for either absent owner.
static SpViewInterface *NwGetTransitCurrentView() {
  ApGameInterface *application = NwGetTransitApplication();
  SpCityWindowView *window = application ? application->GetISC3WinCityView() : nullptr;
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the isometric view's sprite manager after the original current-view lookup.
static SpManagerView *NwGetTransitViewSpriteManager() {
  SpViewInterface *view = NwGetTransitCurrentView();
  return view ? static_cast<SpIsoView *>(view)->GetCitySpriteManager() : nullptr;
}

// Format the original station or network traffic row using the live view occupant and localized resources.
// Unsupported geometry refuses the row; the station's missing-language path retains its original early-return ownership.
u8 NwTransitLayer::QueryTrafficValue(u32 x, u32 z, ScStringView *label, ScStringView *out, u32 index, u8 *warning) {
  if (warning)
    *warning = 0;
  if (index)
    return 1;
  NwGetTransitCity();
  SpViewInterface *view = NwGetTransitCurrentView();
  ScOccupantManager *manager = view->CurrentOccupantManager();
  ScOccupant *occupant = nullptr;
  if (!manager->GetOccupantAtCell(x, z, &occupant) || !occupant)
    return 1;
  if (occupant->TypeId() == kCtAgentRailStation || occupant->TypeId() == kCtAgentSubwayStation ||
      occupant->TypeId() == kCtAgentSubwayRailStation || occupant->TypeId() == kCtAgentBusStation) {
    ScString title = NwTransitQueryText(kRsCityServicePassengersPerDayText);
    label->Copy(title);
    u8 density;
    GetVolume(x, z, &density, 16);
    i64 amount = i64(f64(density) * 4.3);
    if (phase == 1)
      amount = 0;
    ScLanguageUtility *language = NwGetTransitLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!language)
      return 0;
    ScString number, formatted, format = NwTransitQueryText(56);
    language->MakeNumberString(amount, &number);
    formatted.Sprintf(format.ToChar(), number.ToChar());
    out->Copy(formatted);
    occupant->Release();
    return 1;
  }
  u32 titleId, statusBase;
  u8 mask, density;
  bool subway = false, strike = false;
  if (occupant->IsGeomFlag(kOcGeomRoad)) {
    titleId = 60;
    statusBase = 61;
    mask = 1;
  } else if (occupant->IsGeomFlag(kOcGeomHighway)) {
    titleId = 60;
    statusBase = 61;
    mask = 2;
  } else if (occupant->IsGeomFlag(kOcGeomRail)) {
    titleId = 67;
    statusBase = 68;
    mask = 4;
    strike = true;
  } else if (occupant->IsGeomFlag(kOcGeomSubway)) {
    titleId = 73;
    statusBase = 74;
    mask = 8;
    strike = true;
    subway = true;
  } else {
    occupant->Release();
    return 0;
  }
  ScString title = NwTransitQueryText(titleId);
  label->Copy(title);
  GetVolume(x, z, &density, mask);
  if (strike && phase == 1)
    density = 0;
  u32 level;
  if (!density)
    level = 0;
  else if (density < (subway ? 72 : 65))
    level = 1;
  else if (density < (subway ? 129 : 130))
    level = 2;
  else if (density < (subway ? 187 : 180))
    level = 3;
  else {
    level = 4;
    if (warning)
      *warning = 1;
  }
  ScString status = NwTransitQueryText(statusBase + level);
  out->Copy(status);
  occupant->Release();
  return 1;
}

// Draw the transient route highlight while holding the graphics lock, then perform the original alertable10 ms wait.
// The current city is fetched again before every lock transition, preserving callback-visible ordering.
void SC3_STDCALL NwShowRouteOccupant(ScOccupant *occupant, u16 mask) {
  if (!occupant)
    return;
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  occupant->GetLocation(&location);
  NwGetTransitCity()->LockGraphics();
  NwGetTransitViewSpriteManager()->ChangeCellAlignedSprite(occupant, 0, nullptr, mask);
  NwGetTransitCity()->UnlockGraphics();
  NwGetTransitCity()->Unlock();
  ScThread::SleepCurrentThread(10000);
  NwGetTransitCity()->Lock();
}

void SC3_STDCALL NwClearRouteOccupant(ScOccupant *occupant) {
  if (!occupant)
    return;
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  occupant->GetLocation(&location);
  NwGetTransitCity()->LockGraphics();
  NwGetTransitViewSpriteManager()->ChangeCellAlignedSprite(occupant, 0, nullptr, 0);
  NwGetTransitCity()->UnlockGraphics();
}

// Select the next original route cell, retaining the actual occupant and preserving marker and backtracking effects.
// Byte options select station transitions; costs, teleports and rejected paths retain their original output ordering.
u8 NwTransitLayer::SelectNextCell(NwTripData *trip, NwTripData *next, u32 cost, u8 first, u8 second, u8 last) {
  next->occupant = nullptr;
  trip->value29 = 0;
  next->value29 = 1;
  if (trip->value24 > cost)
    return 0;
  u32 chosenX = 0, chosenZ = 0;
  u16 chosenFlags = 0;
  u8 chosenMode = 0, chosenDensity = 0, bestDensity = 0, goodCandidate = 0, found = 0;
  ScOccupantManager *chosenManager = nullptr;
  const u8 currentMode = trip->mode;
  if (currentMode == 2 || currentMode == 3 || currentMode == 66 || currentMode == 67) {
    if (!trip->occupant)
      return 0;
    void *selected = nullptr;
    trip->occupant->QueryInterface(IID_NwConnection, &selected);
    NwConnection *connection = static_cast<NwConnection *>(selected);
    if (!connection)
      return 0;
    u8 left, up, right, down;
    connection->GetConnectionSamples(0, drivingSide, trip->value28, &left, &up, &right, &down);
    connection->Release();
    auto probe = [&](u32 x, u32 z) {
      u8 density, mode;
      u16 flags;
      if (!ProbeRouteCell(currentMode, x, z, &density, &mode, &flags))
        return;
      if (found) {
        trip->value29 = 1;
        if (density >= NwTransitAnnoyance)
          return;
      }
      found = 1;
      chosenX = x;
      chosenZ = z;
      chosenDensity = density;
      chosenMode = mode;
      chosenFlags = flags;
    };
    if (up && trip->z0)
      probe(trip->x0, trip->z0 - 1);
    if (left && trip->x0)
      probe(trip->x0 - 1, trip->z0);
    if (down && trip->z0 < height - 1)
      probe(trip->x0, trip->z0 + 1);
    if (right && trip->x0 < width - 1)
      probe(trip->x0 + 1, trip->z0);
    if (!found) {
      next->value29 = 0;
      return 0;
    }
    chosenManager = surface;
  } else {
    bool expanded = currentMode == 128;
    u32 radius = 1;
    if (expanded) {
      u8 category;
      if (trip->occupant) {
        // The original reads an unwritten category when classification fails; that path has no defined route result.
        [[maybe_unused]] u8 classified = NwClassifyCoverageOccupant(trip->occupant, &category);
        assert(classified);
        radius = category == 0 ? roadDistanceRes
            : category == 1    ? roadDistanceCom
            : category == 2    ? roadDistanceInd
                               : roadDistance200;
      } else {
        radius = roadDistance200;
      }
    }
    i32 leftEdge = i32(trip->x0 - radius), topEdge = i32(trip->z0 - radius);
    i32 rightEdge = i32(trip->x1 + radius), bottomEdge = i32(trip->z1 + radius);
    u32 leftBound = leftEdge < 0 ? 0 : u32(leftEdge), topBound = topEdge < 0 ? 0 : u32(topEdge);
    u32 rightBound = rightEdge > i32(width - 1) ? width - 1 : u32(rightEdge);
    u32 bottomBound = bottomEdge > i32(height - 1) ? height - 1 : u32(bottomEdge);
    auto inspect = [&](u32 x, u32 z) {
      if ((i32(x) == leftEdge || i32(x) == rightEdge) && (i32(z) == topEdge || i32(z) == bottomEdge))
        return;
      u8 visited = grid120->cells[x][z];
      if ((visited & currentMode) == currentMode)
        return;
      u16 flags = grid92->cells[x][z];
      bool station = (flags & 0x10) != 0;
      u8 mode = 0, mark = currentMode;
      ScOccupantManager *manager = surface;
      NwByteGrid *densityGrid = grid108;
      if (expanded) {
        if (station) {
          if (second && (flags & 1))
            mode = 80;
          else if (!first && !last)
            mode = 0;
          else if (flags & 0x20)
            mode = 48;
          else if (first && (flags & 4))
            mode = 20;
          else if (last && flags)
            mode = 24;
        } else if (flags & 1) {
          mode = 1;
        }
      } else {
        switch (currentMode) {
        case 1:
          if (station) {
            if (second && (flags & 1))
              mode = 80;
            else if (!first && !last)
              mode = 0;
            else if (flags & 0x20)
              mode = 48;
            else if (first && (flags & 4))
              mode = 20;
            else if (last && flags)
              mode = 24;
          } else if (flags & 1) {
            mode = u8((flags & 0x40) | 0x20) >> 5;
          }
          break;
        case 4:
          if (flags & 4)
            mode = station ? ((flags & 0x20) ? 48 : 20) : 4;
          break;
        case 8:
          if (station && (flags & 0x20)) {
            mode = 48;
          } else if (station && (flags & 8) && !(flags & 5)) {
            mode = 24;
          } else if (flags & 8) {
            mode = 8;
            manager = underground;
            densityGrid = grid116;
          }
          break;
        case 20:
        case 24:
        case 48:
          mark = 16;
          if (!station && (currentMode == 20 || (currentMode == 48 && first)) && (flags & 4) && trip->value21 != 4) {
            mode = 4;
          } else if (!station && (currentMode == 24 || (currentMode == 48 && last)) && (flags & 8) &&
              trip->value21 != 8) {
            mode = 8;
            manager = underground;
            densityGrid = grid116;
          } else if (second && station && (flags & 1) && trip->value21 != 80) {
            mode = 80;
          }
          break;
        case 65:
          if (station) {
            if (!first && !last)
              mode = 0;
            else if (flags & 0x20)
              mode = 48;
            else if (first && (flags & 4))
              mode = 20;
            else if (last && flags)
              mode = 24;
          } else if (flags & 1) {
            mode = (u8(flags) >> 5 & 2) | 0x41;
          }
          break;
        case 80:
          if (!station && (flags & 1) && trip->value21 != 65)
            mode = 65;
          break;
        default:
          return;
        }
      }
      if (!mode) {
        grid120->cells[x][z] = visited | mark;
        return;
      }
      u8 density = densityGrid->cells[x][z];
      if (found)
        next->value29 = 1;
      if (density >= NwTransitAnnoyance) {
        if (found)
          return;
      } else if (station) {
        bestDensity = 255;
        goodCandidate = 1;
      } else {
        if ((expanded || goodCandidate) && density < bestDensity)
          return;
        bestDensity = density;
        goodCandidate = 1;
      }
      found = 1;
      chosenX = x;
      chosenZ = z;
      chosenDensity = density;
      chosenMode = mode;
      chosenFlags = flags;
      chosenManager = manager;
    };
    if (expanded) {
      GeCellRect bounds{leftBound, topBound, rightBound, bottomBound};
      options.SetBounds(&bounds);
      options.Prepare(1);
      u32 x, z;
      while (options.Next(&x, &z))
        inspect(x, z);
    } else {
      for (u32 x = leftBound; x <= rightBound; ++x)
        for (u32 z = topBound; z <= bottomBound; ++z)
          inspect(x, z);
    }
    if (!found) {
      next->value29 = 0;
      return 0;
    }
  }
  next->occupant = nullptr;
  chosenManager->GetOccupantAtCell(chosenX, chosenZ, &next->occupant);
  if (!next->occupant) {
    next->value29 = 0;
    return 0;
  }
  GeBounds bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  next->occupant->GetBounds(&bounds);
  next->x0 = u32(bounds.minimum.x >> kCtMicroCoordinateShift);
  next->z0 = u32(bounds.minimum.z >> kCtMicroCoordinateShift);
  next->x1 = u32(bounds.maximum.x >> kCtMicroCoordinateShift);
  next->z1 = u32(bounds.maximum.z >> kCtMicroCoordinateShift);
  next->mode = chosenMode;
  next->value21 = trip->mode;
  for (u32 x = next->x0; x <= next->x1; ++x)
    for (u32 z = next->z0; z <= next->z1; ++z)
      grid120->cells[x][z] |= trip->mode;
  u32 costIndex;
  bool applyScale = false, applyDamage = false;
  switch (chosenMode) {
  case 1:
    costIndex = 0;
    applyScale = true;
    applyDamage = true;
    break;
  case 2:
    costIndex = 2;
    applyScale = true;
    applyDamage = true;
    break;
  case 3:
    costIndex = 0;
    break;
  case 4:
    costIndex = 8;
    applyScale = true;
    break;
  case 8:
    costIndex = 9;
    applyScale = true;
    break;
  case 20:
    costIndex = trip->mode == 4 ? 8 : 5;
    break;
  case 24:
    costIndex = trip->mode == 8 ? 9 : 7;
    break;
  case 48:
    costIndex = trip->mode == 4 || trip->mode == 8 ? 9 : 6;
    break;
  case 65:
    costIndex = 1;
    applyScale = true;
    applyDamage = true;
    break;
  case 66:
    costIndex = 3;
    applyScale = true;
    applyDamage = true;
    break;
  case 67:
    costIndex = 1;
    break;
  case 80:
    costIndex = trip->mode == 65 ? 2 : 4;
    break;
  default:
    next->value29 = 0;
    return 0;
  }
  u8 divisor = NwTransitDensityDivisors[costIndex];
  assert(divisor != 0);
  u32 step = NwTransitTripCosts[costIndex] + chosenDensity / divisor;
  if (applyDamage && (chosenFlags & 0x80))
    step += NwTransitDamageCost;
  if (applyScale) {
    u16 scaleFlag = chosenMode == 8 ? 0x200 : 0x100;
    f32 multiplier = chosenFlags & scaleFlag ? 0.7f : 1.0f;
    step = u32(i64(ceil(f64(step) * multiplier)));
  }
  if (trip->z1 < next->z0)
    next->value28 = 1;
  else if (trip->x1 < next->x0)
    next->value28 = 0;
  else
    next->value28 = trip->z1 <= next->z1 ? 2 : 3;
  if (chosenManager == surface && (chosenFlags & 0x4000)) {
    void *selected = nullptr;
    if (!next->occupant->QueryInterface(IID_NwConnection, &selected)) {
      next->value29 = 0;
      return 0;
    }
    NwConnection *connection = static_cast<NwConnection *>(selected);
    i32 direction, length;
    connection->GetTeleport(&direction, &length);
    connection->Release();
    step *= u32(length);
    switch (direction) {
    case 0:
      next->x0 -= u32(length);
      next->x1 -= u32(length);
      break;
    case 1:
      next->z0 -= u32(length);
      next->z1 -= u32(length);
      break;
    case 2:
      next->x0 += u32(length);
      next->x1 += u32(length);
      break;
    case 3:
      next->z0 += u32(length);
      next->z1 += u32(length);
      break;
    default:
      next->value29 = 0;
      return 0;
    }
  }
  next->value24 = trip->value24 + step;
  return 1;
}

// On refusal the installed raw deallocation leaves both constructor retains in place; matching delete must not release
// them.
bool NwTransitLayer::CreateAgent(u32 type, u32 iid, void **out) {
  if (type != kUiTrafficMapRasterId)
    return false;
  NwTrafficRaster *raster = new NwTrafficRaster(city, this);
  if (raster->QueryInterface(iid, out))
    return true;
  raster->layer = nullptr;
  raster->city = nullptr;
  delete raster;
  return false;
}
