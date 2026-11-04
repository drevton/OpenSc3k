#define _CRT_SECURE_NO_WARNINGS
#include "economy/ec_land_value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/maps.h"
#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_graph_info.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_text.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "graphics/image.h"
#include "graphics/sc3_graphics.h"
#include "misc/api.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "terrain/api.h"
#include "ui/api.h"

// The land-value history graph retains its original series color.
enum EcLandValueGraphColor : u32 {
  kEcLandValueGraphColor = 0xc09030,
};

// Empty curves would read undefined sentinel storage.
// Interpolate between consecutive ordered population thresholds using the original unsigned 64-bit arithmetic.
u8 EcInterpolateLandCurve(const EcLandCurvePoint *first, u32 population) {
  assert(first);
  const EcLandCurvePoint *selected = first, *next = first->next;
  while (next && next->threshold <= population) {
    selected = next;
    next = next->next;
  }
  if (!next)
    return selected->modifier;
  i32 difference = i32(next->modifier) - i32(selected->modifier);
  if (difference < 0)
    difference = -difference;
  u64 adjustment = (u64(population) - selected->threshold) * u32(difference) / (next->threshold - selected->threshold);
  return u8(selected->modifier + adjustment);
}

// These are the three original separately owned threshold maps; StaticInit populates them from installed tuning.
EcLandCurvePoint *EcLandValue::residential_curve = nullptr;
EcLandCurvePoint *EcLandValue::commercial_curve = nullptr;
EcLandCurvePoint *EcLandValue::industrial_curve = nullptr;
u8 EcLandValue::curve_start[3] = {100, 100, 100};
u8 EcLandValue::curve_limit[3] = {0, 0, 0};
EcLandAgentModifier *EcLandValue::agent_modifiers = nullptr;

// Parse signed agent influence contributions and publish a nonzero four-byte record under the original full type ID.
void EcParseLandAgentModifier(const ScString &key, const ScString &value, void *) {
  u32 type = EcParseTuningNumber(key.Data());
  ScString remaining(value);
  u8 effects[4] = {0, 0, 0, 0};
  bool done = false;
  do {
    ScString amount_text = remaining.SplitToken(":");
    if (!amount_text.Strlen())
      break;
    i32 amount = i32(EcParseTuningNumber(amount_text.Data()));
    if (amount < -127)
      amount = -127;
    if (amount > 127)
      amount = 127;
    ScString category = remaining.SplitToken(",");
    if (!category.Strlen()) {
      category = remaining;
      done = true;
    }
    char code = category.Data()[0];
    u32 index = code == 'r' || code == 'R' ? 0
        : code == 'c' || code == 'C'       ? 1
        : code == 'i' || code == 'I'       ? 2
        : code == 'a' || code == 'A'       ? 3
                                           : 4;
    if (index < 4)
      effects[index] = u8(effects[index] + amount);
  } while (!done);
  if (effects[0] || effects[1] || effects[2] || effects[3]) {
    EcLandAgentModifier **entry = &EcLandValue::agent_modifiers;
    while (*entry && (*entry)->type < type)
      entry = &(*entry)->next;
    if (!*entry || (*entry)->type != type)
      *entry = new EcLandAgentModifier{*entry, type, 0, 0, 0, 0};
    (*entry)->residential = effects[0];
    (*entry)->commercial = effects[1];
    (*entry)->industrial = effects[2];
    (*entry)->all = effects[3];
  }
}

// Read the original start/limit pair and append threshold/modifier pairs to the selected ordered demand curve.
u8 EcLoadLandCurve(ScString *input, u8 kind) {
  u8 limit = 255, start = 0;
  if (input->Strlen()) {
    ScString first = input->SplitToken(" ");
    if (!first.Strlen())
      return 0;
    ScString initial = first.SplitToken("+");
    if (!initial.Strlen())
      return 0;
    limit = u8(EcParseTuningNumber(initial.Data()));
    if (!first.Strlen())
      return 0;
    start = u8(EcParseTuningNumber(first.Data()));
    bool done = false;
    do {
      ScString point = input->SplitToken(" ");
      if (!point.Strlen()) {
        point = *input;
        done = true;
      }
      ScString threshold_text = point.SplitToken(":");
      if (threshold_text.Strlen() && point.Strlen()) {
        u32 threshold = 1000u * EcParseTuningNumber(threshold_text.Data());
        u8 modifier = u8(EcParseTuningNumber(point.Data()));
        EcLandCurvePoint **head = kind == 'r' ? &EcLandValue::residential_curve
            : kind == 'c'                     ? &EcLandValue::commercial_curve
            : kind == 'i'                     ? &EcLandValue::industrial_curve
                                              : nullptr;
        if (head) {
          EcLandCurvePoint **entry = head;
          while (*entry && (*entry)->threshold < threshold)
            entry = &(*entry)->next;
          if (!*entry || (*entry)->threshold != threshold)
            *entry = new EcLandCurvePoint{*entry, threshold, modifier};
          else
            (*entry)->modifier = modifier;
        }
      }
    } while (!done);
  }
  EcLandCurvePoint *head = kind == 'r' ? EcLandValue::residential_curve
      : kind == 'c'                    ? EcLandValue::commercial_curve
      : kind == 'i'                    ? EcLandValue::industrial_curve
                                       : nullptr;
  if (kind != 'r' && kind != 'c' && kind != 'i')
    return 1;
  if (!head)
    return 0;
  u32 index = kind == 'r' ? 0 : kind == 'c' ? 1 : 2;
  EcLandValue::curve_limit[index] = limit;
  EcLandValue::curve_start[index] = start;
  return 1;
}

// Refresh all three city-center modifiers from the same live residential population supply, in original order.
void EcLandValue::RefreshDemandModifiers() {
  u32 population = u32(valve->QuerySupplyValue());
  residential_modifier = EcInterpolateLandCurve(residential_curve, population);
  commercial_modifier = EcInterpolateLandCurve(commercial_curve, population);
  industrial_modifier = EcInterpolateLandCurve(industrial_curve, population);
}

i32 EcLandValue::WaterDistance(u32 x, u32 z) {
  return i32(water_influence_radius) - i32(water_coverage[(x >> 1) + coverage_width * (z >> 1)]) + 1;
}

// Rebuild the four influence grids from existing occupants and apply the original two in-place smoothing passes.
// An iterator initialization failure skips restamping, still smooths the existing grids, then releases the iterator.
void EcLandValue::RecomputeInfluenceLayers() {
  OcIterator *iterator = nullptr;
  bool ready = occupants->GetOccupants(&iterator, nullptr, nullptr);
  assert(iterator);
  if (ready) {
    for (u32 index = 0; index < 4; ++index) {
      i16 zero = 0;
      influences[index].SetAllCells(&zero);
    }
    for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next())
      StampOccupant(0, occupant);
  }
  for (u32 pass = 0; pass < 2; ++pass) {
    for (i32 x = 0; x < i32(CellCountX()); x += 4) {
      for (i32 z = 0; z < i32(CellCountZ()); z += 4) {
        u16 sums[4] = {0, 0, 0, 0};
        i16 count = 0;
        for (i32 dx = -4; dx <= 4; dx += 4) {
          for (i32 dz = -4; dz <= 4; dz += 4) {
            u32 sx = u32(x + dx), sz = u32(z + dz);
            if (!InBounds(sx, sz))
              continue;
            ++count;
            for (u32 index = 0; index < 4; ++index) {
              i16 cell;
              influences[index].GetValue(sx, sz, &cell);
              sums[index] = u16(sums[index] + u16(cell));
            }
          }
        }
        for (u32 index = 0; index < 4; ++index) {
          i16 value = i16(i16(sums[index]) / count);
          influences[index].SetValue(u32(x), u32(z), &value);
        }
      }
    }
  }
  iterator->Release();
}

// Refresh the demand and influence maps, publish the completed average and reset the next sweep's counters.
void EcLandValue::FinishSweep() {
  RefreshDemandModifiers();
  RecomputeInfluenceLayers();
  field196 = field200;
  if (field204) {
    if (field200 < 0)
      field200 = 0;
    average_value = u8(u32(field200) / field204);
  } else {
    average_value = 0;
  }
  field200 = 0;
  field204 = 0;
  average_altitude = terrain->GetAverageAltitude();
}

// Initial traversal coordinates from the installed module; the original land tuning loader may change both.
u32 EcLandValueManipulator::initial_x = 4;
u32 EcLandValueManipulator::initial_z = 4;

// Initialize the manipulator's retained and borrowed service references to their original empty values.
EcLandValueManipulator::EcLandValueManipulator()
    : city(nullptr), occupants(nullptr), land(nullptr), buildings(nullptr), pollution(nullptr),
      crime(nullptr), valve_layer(nullptr), zones(nullptr), ordinances(nullptr), residential_valve(nullptr),
      commercial_valve(nullptr), industrial_valve(nullptr), traversal_phase(0), traversal_x(initial_x),
      traversal_z(initial_z), cells_per_step(0), update_period(0), thresholds{}, low_factors{}, high_factors{},
      map_factors{} {
}

EcLandValueManipulator::~EcLandValueManipulator() {
  Shutdown();
}

bool EcLandValueManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcLandValueManipulator::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcLandValueManipulator::Release() {
  return EcCommandOwner::Release();
}

bool EcLandValueManipulator::IsLockingCity() {
  return true;
}

bool EcLandValueManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcLandValueManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcLandValueManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Retain the actual city simulation services and capture the current land-value tuning and traversal schedule.
u8 EcLandValueManipulator::Init(CtCity *owner) {
  city = ScAddRefNotNull(owner);
  pollution = city->PollutionLayer();
  if (!pollution)
    return 0;
  pollution->AddRef();
  crime = city->CrimeLayer();
  if (!crime)
    return 0;
  crime->AddRef();
  buildings = city->BuildingLayer();
  if (!buildings)
    return 0;
  buildings->AddRef();
  zones = city->ZoneLayer();
  if (!zones)
    return 0;
  zones->AddRef();
  ordinances = city->OrdinanceLayer();
  if (!ordinances)
    return 0;
  ordinances->AddRef();
  CtLandValueLayer *land_view = city->LandValueLayer();
  if (!land_view)
    return 0;
  land_view->AddRef();
  land = static_cast<EcLandValue *>(land_view);
  CtLayer *layer = city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer)))
    return 0;
  valve_layer->GetValvePointer(kEcResidentialValveId, IID_EcValve, reinterpret_cast<void **>(&residential_valve));
  valve_layer->GetValvePointer(kEcCommercialValveId, IID_EcValve, reinterpret_cast<void **>(&commercial_valve));
  valve_layer->GetValvePointer(kEcIndustrialValveId, IID_EcValve, reinterpret_cast<void **>(&industrial_valve));
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return 0;
  thresholds[0] = u8(EcLandThresholds[0]);
  thresholds[1] = u8(EcLandThresholds[1]);
  thresholds[2] = u8(EcLandThresholds[2]);
  thresholds[3] = u16(EcLandThresholds[3]);
  thresholds[4] = u8(EcLandThresholds[4]);
  for (u32 index = 0; index < 5; ++index) {
    low_factors[index] = EcLandLowFactors[index];
    high_factors[index] = EcLandHighFactors[index];
  }
  for (u32 index = 0; index < 4; ++index)
    map_factors[index] = EcLandMapFactors[index];
  update_period = EcLandUpdatePeriod;
  if (!update_period)
    update_period = 1;
  u32 height = land->CellCountZ(), width = land->CellCountX();
  traversal_phase = 0;
  cells_per_step = width * height / (4u * update_period);
  traversal_x = initial_x;
  traversal_z = initial_z;
  return 1;
}

// Release each retained city service in the original sequence, then clear the borrowed surface manager.
u8 EcLandValueManipulator::Shutdown() {
  ScRelease(&city);
  ScRelease(&valve_layer);
  ScRelease(&residential_valve);
  ScRelease(&commercial_valve);
  ScRelease(&industrial_valve);
  ScRelease(&land);
  ScRelease(&zones);
  ScRelease(&crime);
  ScRelease(&pollution);
  ScRelease(&buildings);
  ScRelease(&ordinances);
  occupants = nullptr;
  return 1;
}

// Resolve and cache the occupant's configured agent influences, then add or remove them over its occupied cells.
u8 EcLandValue::StampOccupant(u32 operation, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  if (operation > 1)
    return 1;
  BdBuilding *building = nullptr;
  if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
    bool active = building->GetOccupancyState() == 1;
    building->Release();
    if (!active)
      return 1;
  }
  u32 type = occupant->TypeId();
  EcLandAgentModifier **entry = &agent_modifiers;
  while (*entry && (*entry)->type < type)
    entry = &(*entry)->next;
  EcLandAgentModifier *modifier = *entry;
  if (!modifier || modifier->type != type) {
    u8 values[4] = {0, 0, 0, 0};
    for (EcLandAgentModifier *candidate = agent_modifiers; candidate; candidate = candidate->next) {
      if (agent_tree->IsAncestor(candidate->type, type)) {
        values[0] = candidate->residential;
        values[1] = candidate->commercial;
        values[2] = candidate->industrial;
        values[3] = candidate->all;
        break;
      }
    }
    modifier = new EcLandAgentModifier{*entry, type, values[0], values[1], values[2], values[3]};
    *entry = modifier;
  }
  i32 values[4] = {i8(modifier->all), i8(modifier->residential), i8(modifier->commercial), i8(modifier->industrial)};
  if (operation == 1)
    for (u32 index = 0; index < 4; ++index)
      values[index] = -values[index];
  if (!(values[0] || values[1] || values[2] || values[3]))
    return 1;
  GeCellRect bounds;
  occupant->GetBoundingRectInCellCoords(&bounds);
  for (i32 x = i32(bounds.left); x <= i32(bounds.right); ++x) {
    for (i32 z = i32(bounds.top); z <= i32(bounds.bottom); ++z) {
      for (u32 index = 0; index < 4; ++index) {
        i16 cell;
        influences[index].GetValue(u32(x), u32(z), &cell);
        cell = i16(u16(u16(cell) + values[index]));
        influences[index].SetValue(u32(x), u32(z), &cell);
      }
    }
  }
  return 1;
}

// Compute each scheduled two-by-two land-value block from the actual influence, terrain, zoning and pollution services.
bool EcLandValueManipulator::Apply(CtAgent *) {
  f32 ordinance_effect = ordinances->QueryOrdinanceOn(kCtOrdinance_HomelessShelters) ? EcLandHomelessEffect : 0;
  u32 center_x, center_z;
  buildings->GetCityCenter(&center_x, &center_z);
  u8 modifiers[3] = {land->residential_modifier, land->commercial_modifier, land->industrial_modifier};
  u32 city_radius = (city->CellCountX() + city->CellCountZ()) >> 1;
  u32 radii[3];
  f32 slopes[3];
  for (u32 index = 0; index < 3; ++index) {
    radii[index] = city_radius * land->curve_start[index] / 100;
    if (!radii[index])
      radii[index] = 1;
    slopes[index] = f32(f64(i32(land->curve_limit[index]) - i32(modifiers[index])) / i32(radii[index]));
  }
  static const u32 start_x[16] = {4, 0, 2, 4, 6, 0, 2, 6, 2, 0, 6, 2, 4, 6, 4, 0};
  static const u32 start_z[16] = {4, 6, 0, 6, 4, 0, 2, 2, 6, 2, 0, 4, 2, 6, 0, 4};
  for (u32 step = 0; step < cells_per_step; ++step) {
    EcGetRciCity()->Lock();
    f32 influence[4];
    for (u32 index = 0; index < 4; ++index) {
      i16 cell;
      land->influences[index].GetValue(traversal_x, traversal_z, &cell);
      if (cell < -150)
        cell = -150;
      if (cell > 150)
        cell = 150;
      influence[index] = f32(f64(cell) * f64(0.01f));
    }
    i32 distance = land->WaterDistance(traversal_x, traversal_z);
    u32 terrain_score = 0;
    if (distance <= land->water_influence_radius)
      terrain_score = u32(EcLandWaterEffect * (i32(land->water_influence_radius) - distance + 1)) /
          (u32(land->water_influence_radius) + 1);
    u8 altitude;
    land->terrain->GetVertexAltitudeDirt(traversal_x, traversal_z, &altitude);
    altitude = u8(altitude - land->terrain->GetGlobalSeaLevel());
    if (altitude > land->average_altitude) {
      u32 limit = 8u * land->average_altitude;
      if (!limit)
        limit = 1;
      u32 selected = altitude > limit ? limit : altitude;
      terrain_score += selected * EcLandHillEffect / limit;
    }
    u8 fallback = 0;
    for (u32 cell = 0; cell < 4; ++cell) {
      u8 zone;
      zones->GetValue(traversal_x + (cell & 1), traversal_z + (cell >> 1), &zone);
      if (zone >= 1 && zone <= 3) {
        if (fallback != 5)
          fallback = 1;
      } else if ((zone >= 5 && zone <= 7) || zone == 14 || zone == 22) {
        fallback = 5;
      } else if ((zone >= 9 && zone <= 11) || zone == 15) {
        if (!fallback)
          fallback = 9;
      }
    }
    // The original spills the scaled score to binary32 before adding one and storing the multiplier.
    f32 terrain_effect = f32(f64(i16(u16(terrain_score))) * f64(0.01f));
    f32 terrain_multiplier = f32(f64(terrain_effect) + 1.0);
    u32 sum_bits = 0;
    for (u32 cell = 0; cell < 4; ++cell) {
      u32 x = traversal_x + (cell & 1), z = traversal_z + (cell >> 1);
      u8 zone;
      zones->GetValue(x, z, &zone);
      if (!zone)
        zone = fallback;
      i32 dx = i32(center_x - x), dz = i32(center_z - z);
      if (dx < 0)
        dx = -dx;
      if (dz < 0)
        dz = -dz;
      u32 center_distance = u32(dx >= dz ? dx + dz / 2 : dz + dx / 2);
      i32 center_factor = 1;
      f32 zone_influence = influence[0];
      i32 sector = zone >= 1 && zone <= 3                          ? 0
          : ((zone >= 5 && zone <= 7) || zone == 14 || zone == 22) ? 1
          : ((zone >= 9 && zone <= 11) || zone == 15)              ? 2
                                                                   : -1;
      if (sector >= 0) {
        u32 index = u32(sector);
        i32 center_value = center_distance > radii[index]
            ? land->curve_limit[index]
            : i32(modifiers[index]) + i32(nearbyint(f64(center_distance) * slopes[index]));
        center_factor = center_value + 1;
        zone_influence = influence[index + 1];
      }
      f32 pollution_effect = pollution->IsRadioactive(x, z) ? low_factors[0] : 0;
      u32 air, water, garbage;
      pollution->GetAirValue(x, z, &air);
      u32 threshold = thresholds[2];
      if (air > threshold)
        pollution_effect = f32(f64(pollution_effect) + f64(air - threshold) * low_factors[2] / (32767 - threshold));
      else if (air < threshold)
        pollution_effect = f32(f64(pollution_effect) + f64(threshold - air) * high_factors[2] / threshold);
      pollution->GetWaterValue(x, z, &water);
      threshold = thresholds[1];
      if (water >= threshold) {
        if (water > thresholds[2])
          pollution_effect = f32(f64(pollution_effect) + f64(water - threshold) * low_factors[1] / (32767 - threshold));
      } else {
        pollution_effect = f32(f64(pollution_effect) + f64(threshold - water) * high_factors[1] / threshold);
      }
      pollution->GetGarbageValue(x, z, &garbage);
      threshold = thresholds[3];
      if (garbage > threshold)
        pollution_effect = f32(f64(pollution_effect) + f64(garbage - threshold) * low_factors[3] / (32767 - threshold));
      else if (garbage < threshold)
        pollution_effect = f32(f64(pollution_effect) + f64(threshold - garbage) * high_factors[3] / threshold);
      u8 crime_value;
      crime->GetValue(x, z, &crime_value);
      threshold = thresholds[4];
      f64 crime_effect = 0;
      if (crime_value > threshold)
        crime_effect = f64(crime_value - threshold) * low_factors[4] / (255 - threshold);
      else if (crime_value < threshold)
        crime_effect = f64(threshold - crime_value) * high_factors[4] / threshold;
      i32 base = i32(nearbyint(f64(center_factor) * terrain_multiplier));
      i32 result = i32(nearbyint((crime_effect + pollution_effect + zone_influence + ordinance_effect + 1.0) * base));
      sum_bits += u32(result);
      if (zone) {
        land->field200 = i32(u32(land->field200) + u32(result));
        ++land->field204;
      }
    }
    i32 average = i32(sum_bits) / 4;
    u8 result = average > 255 ? 255 : average < 1 ? 1 : u8(average);
    land->SetValue(traversal_x, traversal_z, &result);
    traversal_x += 8;
    if (traversal_x >= land->CellCountX()) {
      traversal_z += 8;
      if (traversal_z >= land->CellCountZ()) {
        ++traversal_phase;
        if (traversal_phase >= 16) {
          traversal_phase = 0;
          land->FinishSweep();
        }
        traversal_z = start_z[traversal_phase];
      }
      traversal_x = start_x[traversal_phase];
    }
    EcGetRciCity()->Unlock();
  }
  return true;
}

// Installed module defaults; StaticInit replaces only keys present in the game's tuning resource.
u16 EcLandUpdatePeriod = 180;
u32 EcLandThresholds[5] = {1, 1, 1, 1, 1};
f32 EcLandLowFactors[5] = {-1, -1, -1, -1, -1};
f32 EcLandHighFactors[5] = {1, 1, 1, 1, 1};
f32 EcLandMapFactors[4] = {1, 1, 1, 1};
f32 EcLandHomelessEffect = 0.05f;
u8 EcLandWaterEffect = 40, EcLandHillEffect = 20;

static bool EcReadLandTuning(EcIni *ini, const char *name, ScString *value) {
  ScString section("TuningParameters"), key(name);
  return ini->GetValue(&section, &key, value) != 0;
}

// Reload agent modifiers, pollution factors and city-center curves in their original order.
// Partial factor rows retain earlier parsed values; initially unwritten original stack values start at zero here.
bool EcLandValue::StaticInit(CtDefinition *) {
  while (agent_modifiers) {
    EcLandAgentModifier *entry = agent_modifiers;
    agent_modifiers = entry->next;
    delete entry;
  }
  ScString path, value;
  EcIni ini;
  EcGetRciApplication()->GetAppDataDirectory(&path);
  const char *name = "\\Sys\\SC3LandValue.INI";
  path.Append(name, u32(strlen(name)));
  ini.SelectFile(&path);
  EcGetRciApplication()->GetAppDataDirectory(&path);
  name = "\\Sys\\SYS.PAK";
  path.Append(name, u32(strlen(name)));
  ini.SelectPackage(path);
  ScString section("AgentIDModifiers");
  ini.EnumerateSection(&section, EcParseLandAgentModifier, nullptr);
  if (EcReadLandTuning(&ini, "UpdatePeriod", &value)) {
    EcLandUpdatePeriod = u16(EcParseTuningNumber(value.Data()));
    if (!EcLandUpdatePeriod)
      EcLandUpdatePeriod = 1;
  }
  static const char *factor_keys[] = {
      "RadPolluteFactor", "WaterPolluteFactor", "AirPolluteFactor", "GarbagePolluteFactor", "CrimeFactor"};
  i32 threshold = 0, low = 0, high = 0;
  for (u32 index = 0; index < 5; ++index) {
    if (!EcReadLandTuning(&ini, factor_keys[index], &value))
      continue;
    sscanf(value.Data(), "%d,%d,%d", &threshold, &low, &high);
    if (!index) {
      EcLandThresholds[index] = 0;
      EcLandLowFactors[index] = f32(f64(low) * f64(0.001f));
      EcLandHighFactors[index] = 0;
    } else {
      if (index <= 2) {
        EcLandThresholds[index] = u32(threshold) > 32767 ? 32767 : u32(threshold);
      } else {
        if (threshold < 0)
          threshold = 0;
        if (threshold > 255)
          threshold = 255;
        EcLandThresholds[index] = u32(threshold);
      }
      EcLandLowFactors[index] = f32(f64(low) * f64(0.001f));
      EcLandHighFactors[index] = f32(f64(high) * f64(0.001f));
    }
  }
  static const char *map_keys[] = {"ResModMapFactor", "ComModMapFactor", "IndModMapFactor", "AllModMapFactor"};
  for (u32 index = 0; index < 4; ++index)
    if (EcReadLandTuning(&ini, map_keys[index], &value))
      EcLandMapFactors[index] = f32(f64(atol(value.Data())) * f64(0.01f));
  if (EcReadLandTuning(&ini, "WaterEffectRadius", &value)) {
    water_influence_radius = u8(EcParseTuningNumber(value.Data()) >> 1);
    if (water_influence_radius > 126)
      water_influence_radius = 126;
  }
  if (EcReadLandTuning(&ini, "WaterEffect", &value))
    EcLandWaterEffect = u8(EcParseTuningNumber(value.Data()));
  if (EcReadLandTuning(&ini, "HillEffect", &value))
    EcLandHillEffect = u8(EcParseTuningNumber(value.Data()));
  if (EcReadLandTuning(&ini, "HomelessShelterOrdEffect", &value))
    EcLandHomelessEffect = f32(f64(atol(value.Data())) * f64(0.01f));
  if (EcReadLandTuning(&ini, "ResCityCenterEffect", &value))
    EcLoadLandCurve(&value, 'r');
  if (EcReadLandTuning(&ini, "ComCityCenterEffect", &value))
    EcLoadLandCurve(&value, 'c');
  if (EcReadLandTuning(&ini, "IndCityCenterEffect", &value))
    EcLoadLandCurve(&value, 'i');
  return true;
}

// Clear the original agent modifier map and each of the three city-center demand curves.
bool EcLandValue::StaticShutdown() {
  while (agent_modifiers) {
    EcLandAgentModifier *entry = agent_modifiers;
    agent_modifiers = entry->next;
    delete entry;
  }
  EcLandCurvePoint **curves[] = {&residential_curve, &commercial_curve, &industrial_curve};
  for (u32 index = 0; index < 3; ++index) {
    while (*curves[index]) {
      EcLandCurvePoint *entry = *curves[index];
      *curves[index] = entry->next;
      delete entry;
    }
  }
  return true;
}

// Installed initial value; the original static tuning loader can replace it.
u8 EcLandValue::water_influence_radius = 4;

EcLandInfluenceMap::EcLandInfluenceMap() : CtSizedShortMap(2) {
}

// Expose the original unknown, generic map and signed-short map identities.
bool EcLandInfluenceMap::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScCellMapBase && iid != IID_ScCellMapI16)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ScCellMap<i16> *>(this);
  AddRef();
  return true;
}

// Is shared by the module's map specializations.
// Increment the live map reference before acquiring the SIMRCI director hold.
u32 EcLandInfluenceMap::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Is shared by the module's map specializations.
// Release the SIMRCI director before decrementing the count and destroy the map when it reaches zero.
u32 EcLandInfluenceMap::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

// Initialize the owned map resolutions and the layer's empty service references.
EcLandValue::EcLandValue()
    : CtSizedByteMap(1), layer_initialized(false), city(nullptr), occupants(nullptr), zones(nullptr),
      valve_layer(nullptr), terrain(nullptr), valve(nullptr), manipulator(nullptr), agent_tree(nullptr),
      average_value(0), field196(0), field200(0), field204(0), graph(nullptr), water_coverage(nullptr),
      coverage_width(0), average_altitude(0), residential_modifier(0), commercial_modifier(0), industrial_modifier(0) {
}

EcLandValue::~EcLandValue() {
}

bool EcLandValue::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  HANDLE_QUERY_INTERFACE(IID_CtLandValueLayer, CtLandValueLayer);
  return CtSizedByteMap::QueryInterface(iid, out);
}

// Retain the owning DLL after incrementing the primary map count.
u32 EcLandValue::AddRef() {
  ++references;
  EcGetRciDirector()->AddRef();
  return references;
}

// Release the module hold and delete the real owner at zero references.
u32 EcLandValue::Release() {
  if (references) {
    EcGetRciDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

u32 EcLandValue::CellCountX() const {
  return width;
}

u32 EcLandValue::CellCountZ() const {
  return height;
}

void EcLandValue::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = i32(kCtByteMapSint32Scale * cells->columns[x >> shift][z >> shift]);
}

bool EcLandValue::InBounds(u32 x, u32 z) const {
  return CtSizedByteMap::InBounds(x, z);
}

bool EcLandValue::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::InBounds(x0, z0, x1, z1);
}

bool EcLandValue::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return CtSizedByteMap::ValidRect(x0, z0, x1, z1);
}

bool EcLandValue::SerialRead(RsStream *record) {
  return CtSizedByteMap::SerialRead(record);
}

bool EcLandValue::SerialWrite(RsStream *record) const {
  return CtSizedByteMap::SerialWrite(record);
}

bool EcLandValue::Init(CtCity *owner, const u8 *fill) {
  return CtSizedByteMap::Init(owner, fill);
}

void EcLandValue::GetValue(u32 x, u32 z, u8 *value) const {
  *value = cells->columns[x >> shift][z >> shift];
}

void EcLandValue::SetValue(u32 x, u32 z, const u8 *value) {
  cells->columns[x >> shift][z >> shift] = *value;
}

void EcLandValue::SetAllCells(const u8 *value) {
  CtSizedByteMap::SetAllCells(value);
}

void EcLandValue::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  CtSizedByteMap::SetValue(x0, z0, x1, z1, value);
}

u8 EcLandValue::GetAverageValue() {
  return average_value;
}

void EcLandValue::UpdateGraph() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  graph->SetMonthData(average_value);
  if (month == 1) {
    u32 average = graph->GetMonthAverage();
    graph->SetYearData(average);
    if (year % 10 == 0) {
      average = graph->GetYearAverage();
      graph->SetDecadeData(average);
    }
  }
  RebuildWaterCoverage();
}

// Sample each four-tile terrain block and repeatedly spread water horizontally and vertically into the coverage map.
void EcLandValue::RebuildWaterCoverage() {
  u32 size = coverage_width;
  assert(!water_influence_radius || size >= 2);
  u8 *spread = new u8[size * size];
  u32 offset = 0;
  for (u32 z = 0; z < size; ++z) {
    for (u32 x = 0; x < size; ++x) {
      u32 px = 2 * x, pz = 2 * z;
      u8 water = terrain->IsWater(px, pz) || terrain->IsWater(px + 1, pz) || terrain->IsWater(px, pz + 1) ||
          terrain->IsWater(px + 1, pz + 1);
      spread[offset] = water;
      water_coverage[offset++] = water;
    }
  }
  for (u32 pass = 0; pass < water_influence_radius; ++pass) {
    u32 row = 0;
    for (u32 z = 0; z < size; ++z) {
      u8 previous = spread[row], current = spread[row + 1];
      spread[row] = previous | current;
      ++row;
      for (u32 x = 1; x < size - 1; ++x) {
        u8 combined = previous | current;
        previous = current;
        current = spread[row + 1];
        spread[row++] = combined | current;
      }
      spread[row++] = previous | current;
    }
    for (u32 x = 0; x < size; ++x) {
      u8 previous = spread[x], current = spread[x + size];
      spread[x] = previous | current;
      water_coverage[x] += previous | current;
      u32 index = x + size;
      for (u32 z = 1; z < size - 1; ++z) {
        u8 combined = previous | current;
        previous = current;
        current = spread[index + size];
        spread[index] = combined | current;
        water_coverage[index] += combined | current;
        index += size;
      }
      spread[index] = previous | current;
      water_coverage[index] += previous | current;
    }
  }
  delete[] spread;
}

bool EcLandValue::DoMessage(ScMessage *message) {
  if (message->type != kCtMsg_MonthChanged)
    return false;
  UpdateGraph();
  return true;
}

// Dispatch window requests or deliver the current graph view to a queried model receiver.
bool EcLandValue::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return OnWindowCreate(message, source) != 0;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(graph ? static_cast<ScGraphInfoView *>(graph) : nullptr);
      static_cast<ScUnknown *>(selected)->Release();
    }
    return true;
  }
  return false;
}

// Route graph, tile and raster queries through their original acquired interfaces and release order.
u8 EcLandValue::OnWindowCreate(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  switch (u32(message->data1)) {
  case kUiQueryTagGraphs:
    if (!source || !source->QueryInterface(IID_UiGraphsViewIdentity, &selected))
      return 0;
    static_cast<ScObjectCollection *>(selected)->Add(graph ? static_cast<ScGraphInfoView *>(graph) : nullptr);
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagDialog:
  case kUiQueryTagDebug:
    if (!source || !source->QueryInterface(
        u32(message->data1) == kUiQueryTagDialog ? IID_UiQueryWindow : IID_UiQuerySection, &selected))
      return 0;
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  case kUiQueryTagMaps: {
    if (!source || !source->QueryInterface(IID_UiRasterQuery, &selected))
      return 0;
    static const u32 kRasterIds[] = {kUiLandValueMapRasterId, 10023, 10024, 10025, 10027};
    GxRasterSource *raster = nullptr;
    u8 result = 0;
    for (u32 kind : kRasterIds) {
      result = CreateMapSprite(kind, IID_GxRasterSource, reinterpret_cast<void **>(&raster));
      if (result) {
        result = static_cast<UiRasterQuery *>(selected)->AddRaster(raster);
        raster->Release();
        raster = nullptr;
      }
    }
    static_cast<ScUnknown *>(selected)->Release();
    return result;
  }
  case kUiQueryTagStandard:
    if (!source || !source->QueryInterface(IID_UiQuerySection, &selected))
      return 0;
    QueryTile(message, static_cast<UiQuery *>(selected));
    static_cast<ScUnknown *>(selected)->Release();
    return 1;
  default:
    return 0;
  }
}

u8 EcLandValue::QueryTile(ScMessage *, UiQuery *query) {
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  if (!query || !query->GetLocation(&location))
    return 0;
  if (BuildQueryRow(location.x >> 8, location.z >> 8, &label, &value, 0))
    query->AddText(&label, &value, 64, 0);
  return 1;
}

// Select the localized land-value label and rating for the cell, preserving the installed rating thresholds.
u8 EcLandValue::BuildQueryRow(i32 x, i32 z, ScString *label, ScString *value, i32 selector) {
  if (selector)
    return 0;
  u8 cell;
  GetValue(u32(x), u32(z), &cell);
  ScString text;
  {
    EcRci::ScTextResource resource(kRsCityServiceLandValueText, kRsCityServiceTextGroup);
    text = resource.GetText();
  }
  label->Copy(text);
  u32 rating = cell < 35 ? 448 : cell < 75 ? 9 : cell < 130 ? 10 : cell < 175 ? 11 : cell < 225 ? 12 : 13;
  {
    EcRci::ScTextResource resource(rating, kRsCityServiceTextGroup);
    text = resource.GetText();
  }
  value->Copy(text);
  return 1;
}

bool EcLandValue::SimulationEnd() {
  return true;
}

u32 EcLandValue::GetLayerType() {
  return kEcLandValueLayerType;
}

bool EcLandValue::DebugClassTag(ScStringView *) {
  return false;
}

bool EcLandValue::DebugTypeTag(ScStringView *) {
  return false;
}

bool EcLandValue::OccupantInserted(u32, ScOccupant *occupant) {
  return StampOccupant(0, occupant) != 0;
}

bool EcLandValue::OccupantRemoved(u32, ScOccupant *occupant) {
  return StampOccupant(1, occupant) != 0;
}

bool EcLandValue::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool EcLandValue::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool EcLandValue::CellChangedAll(u32) {
  return true;
}

bool EcLandValue::CellChanged(u32, u32, u32) {
  return true;
}

bool EcLandValue::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

// Unsupported identifiers would dereference a null raster in the installed body.
// Create the actual value or influence raster, then publish its requested interface through QueryInterface.
bool EcLandValue::CreateMapSprite(u32 kind, u32 iid, void **out) {
  GxRciMapSprite *sprite = nullptr;
  switch (kind) {
  case kUiLandValueMapRasterId:
    sprite = new GxLandValueSprite(static_cast<CtSizedByteMap *>(this), zones);
    break;
  case 10023:
  case 10024:
  case 10025:
    sprite = new GxLandInfluenceSprite(kind, &influences[kind - kUiLandValueMapRasterId]);
    break;
  case 10027:
    sprite = new GxLandInfluenceSprite(kind, &influences[0]);
    break;
  }
  assert(sprite);
  if (sprite->QueryInterface(iid, out))
    return true;
  delete sprite;
  return false;
}

u32 EcLandValue::GetMapSpriteCount() {
  return 1;
}

// Translate known indexes to raster identifiers. Index four succeeds without writing the caller's output.
bool EcLandValue::GetSpriteIdFromIndex(u32 index, u32 *out) {
  switch (index) {
  case 0:
    *out = kUiLandValueMapRasterId;
    break;
  case 1:
    *out = 10023;
    break;
  case 2:
    *out = 10024;
    break;
  case 3:
    *out = 10025;
    break;
  case 4:
    break;
  case 5:
    *out = 10027;
    break;
  default:
    return false;
  }
  return true;
}

// Acquire the city's land-value services, initialize its five cell maps and create the original history graph.
bool EcLandValue::Init(CtCity *owner) {
  if (layer_initialized || !owner)
    return false;
  city = ScAddRefNotNull(owner);
  agent_tree = ScAddRefNotNull(EcGetRciAgentTree());
  zones = ScAddRefNotNull(city->ZoneLayer());
  terrain = city->DirtBag();
  if (!terrain) {
    Shutdown();
    return false;
  }
  terrain->AddRef();
  CtLayer *layer = city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer)))
    return false;
  i16 zero_influence = 0;
  for (u32 index = 0; index < 4; ++index)
    influences[index].Init(city, &zero_influence);
  u8 zero = 0;
  layer_initialized = CtSizedByteMap::Init(city, &zero);
  average_value = 0;
  field196 = field200 = field204 = 0;
  graph = ScAddRefNotNull(new EcRci::ScGraphInfo(0));
  graph->SetMaxValue(260);
  {
    EcRci::ScTextResource resource(kRsGameLandValueText, kRsGameTextGroup);
    ScString label = resource.GetText();
    graph->SetSelectionLabel(label);
  }
  for (u32 index = 0; index < 3; ++index) {
    EcRci::ScTextResource resource(kRsGameLandValueText, kRsGameTextGroup);
    ScString label = resource.GetText();
    graph->SetCurrentValueLabel(static_cast<ScGraphDataSet>(index), label);
  }
  u32 selection_id = kEcLandValueGraphSelectionId, value_id = kEcLandValueGraphValueId;
  graph->SetSelectionLabelID(selection_id);
  graph->SetValueLabelID(value_id);
  graph->SetColor(kEcLandValueGraphColor);
  coverage_width = CellCountX() >> 1;
  water_coverage = new u8[coverage_width * coverage_width];
  if (!layer_initialized)
    city->Release();
  return layer_initialized;
}

// Unregister the live receiver, retire acquired city services and maps, and release the history graph and water storage.
bool EcLandValue::Shutdown() {
  if (!layer_initialized)
    return true;
  if (occupants) {
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
    occupants = nullptr;
  }
  ScRelease(&city);
  if (manipulator) {
    manipulator->Shutdown();
    manipulator->Release();
    manipulator = nullptr;
  }
  ScRelease(&terrain);
  if (agent_tree)
    agent_tree->Release();
  agent_tree = nullptr;
  if (zones)
    zones->Release();
  zones = nullptr;
  if (valve_layer)
    valve_layer->Release();
  valve_layer = nullptr;
  if (valve)
    valve->Release();
  valve = nullptr;
  for (u32 index = 0; index < 4; ++index)
    influences[index].Shutdown();
  EcGetRciMessageServer()->RemoveNotification(static_cast<CtLayer *>(this), kCtMsg_MonthChanged);
  if (graph)
    graph->Release();
  graph = nullptr;
  if (water_coverage) {
    delete[] water_coverage;
    water_coverage = nullptr;
  }
  layer_initialized = false;
  return CtSizedByteMap::Shutdown();
}

// Bind residential demand, rebuild influence inputs, subscribe to city changes and capture current terrain state.
bool EcLandValue::SimulationBegin() {
  valve_layer->GetValvePointer(kEcResidentialValveId, IID_EcValve, reinterpret_cast<void **>(&valve));
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return false;
  RecomputeInfluenceLayers();
  u32 sender;
  if (!occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &sender, false))
    return false;
  RefreshDemandModifiers();
  EcGetRciMessageServer()->AddNotification(static_cast<CtLayer *>(this), kCtMsg_MonthChanged);
  RebuildWaterCoverage();
  average_altitude = terrain->GetAverageAltitude();
  return true;
}

// Import legacy land values in descending cell order and expand each value into its two-by-two city-cell region.
// The original ignores the getter's return byte; its required cell output must have been written.
bool EcLandValue::Init(CtCity *, ScImporter *importer) {
  if (!importer->MapIsPresent(kCtLegacyXvalPlane)) {
    Shutdown();
    return false;
  }
  i32 last = i32(importer->GetMapSize(kCtLegacyXvalPlane)) - 1;
  for (i32 x = last; x >= 0; --x) {
    for (i32 z = last; z >= 0; --z) {
      u8 cell;
      importer->GetValue(kCtLegacyXvalPlane, u32(x), u32(z), &cell);
      u8 value = u8(1000u * cell / 2500u);
      if (!value)
        value = 1;
      SetValue(u32(2 * x), u32(2 * z), u32(2 * x + 1), u32(2 * z + 1), &value);
    }
  }
  return true;
}

// Lazily retain the original land-value manipulator for its agent type and leave flags unchanged for other types.
bool EcLandValue::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    if (!manipulator) {
      manipulator = ScAddRefNotNull(new EcLandValueManipulator);
      manipulator->Init(city);
    }
    *out = manipulator;
    *flags = 2;
  }
  return true;
}

// Read the real land-value map, manipulator traversal state, aggregate fields and graph from the framed city record.
bool EcLandValue::Init(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcLandValueLayerType, 0});
  u8 result = frame.good;
  RsStream *stream = frame.GetStream();
  if (result)
    result = SerialRead(stream);
  if (!manipulator) {
    manipulator = ScAddRefNotNull(new EcLandValueManipulator);
    manipulator->Init(city);
  }
  result = result && stream->ReadU8(&manipulator->traversal_phase) && stream->ReadU32(&manipulator->traversal_x) &&
      stream->ReadU32(&manipulator->traversal_z) && stream->ReadU8(&average_value) && stream->ReadI32(&field196) &&
      stream->ReadI32(&field200) && stream->ReadU32(&field204);
  if (result)
    result = graph->SerialRead(owner, stream);
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Save the byte map, live traversal state, aggregate fields and graph in the original version-one record.
bool EcLandValue::Save(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcLandValueLayerType, 0}, 1, 0);
  u8 result = frame.good;
  RsStream *stream = frame.GetStream();
  if (result)
    result = SerialWrite(stream);
  if (!manipulator) {
    manipulator = ScAddRefNotNull(new EcLandValueManipulator);
    manipulator->Init(city);
  }
  result = result && stream->WriteU8(manipulator->traversal_phase) && stream->WriteU32(manipulator->traversal_x) &&
      stream->WriteU32(manipulator->traversal_z) && stream->WriteU8(average_value) && stream->WriteI32(field196) &&
      stream->WriteI32(field200) && stream->WriteU32(field204);
  if (result)
    result = graph->SerialWrite(owner, stream);
  return result != 0;
}

GxLandValueSprite::GxLandValueSprite(ScCellMap<u8> *source, ScCellMap<u8> *mask) : values(source), zones(mask) {
  if (zones)
    zones->AddRef();
  values->AddRef();
  u32 map_height = values->CellCountZ(), map_width = values->CellCountX();
  SetId(kUiLandValueMapRasterId);
  SetBaseSize(map_width, map_height);
}

GxLandValueSprite::~GxLandValueSprite() {
  ScRelease(&zones);
  ScRelease(&values);
}

// Paint zoned cells with the palette entry selected by the inverse land-value byte.
bool GxLandValueSprite::GetColor(u32 x, u32 z, u32 *out) {
  if (!zones)
    return false;
  u8 zone;
  zones->GetValue(x, z, &zone);
  if (!zone)
    return false;
  u8 value;
  values->GetValue(x, z, &value);
  GxPaletteView *source = GetPalette();
  if (!source)
    return false;
  i32 count = source->GetNumberEntries();
  i32 index = i32(i64(count) * (255 - value) / 256);
  return source->GetColor(index, out);
}

// Borrow the influence map and initialize the selected raster identifier and size.
GxLandInfluenceSprite::GxLandInfluenceSprite(u32 value, ScCellMap<i16> *source) : values(source) {
  u32 map_height = values->CellCountZ(), map_width = values->CellCountX();
  SetId(value);
  SetBaseSize(map_width, map_height);
}

GxLandInfluenceSprite::~GxLandInfluenceSprite() {
}

// Clamp the signed influence to minus 100 through 100 and convert its grayscale RGB value through the live graphics service.
bool GxLandInfluenceSprite::GetColor(u32 x, u32 z, u32 *out) {
  i16 value = 0;
  values->GetValue(x, z, &value);
  if (value > 100)
    value = 100;
  if (value < -100)
    value = -100;
  u8 gray = u8(127 * value / 100 + 127);
  *out = EcGetRciGraphics()->ConvertRGBValueToNative16Bit(gray, gray, gray);
  return true;
}
