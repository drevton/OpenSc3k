#include "economy/ec_development.h"
#include <assert.h>
#include <string.h>
#include "application/api.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_random.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_building_data.h"
#include "geometry/ge_types.h"
#include "geometry/grid.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "terrain/api.h"

// Initialize the commercial growth state and load its seven shared tuning keys once in original order.
EcCommercialDeveloper::EcCommercialDeveloper()
    : initialized(0), minimum_growth(255), maximum_growth(0), growth_by_minimum(nullptr), growth_by_class(nullptr),
      growth_records(nullptr), random(kScRandomSeedFromClock), city(nullptr), attributes(nullptr), agent_tree(nullptr),
      buildings(nullptr), power(nullptr), water(nullptr), occupants(nullptr), commercial_valve(nullptr), zones(nullptr),
      land_value(nullptr), traffic(nullptr), pollution(nullptr), ordinances(nullptr) {
  if (!EcCommercialTuningLoaded) {
    static const char *const keys[] = {"NoServicePercentage", "MaxDistFromTransport", "ConstructionTimeFactor",
        "HigherLandValueRedevelopmentPct", "LowerLandValueRedevelopmentPct", "HigherDensityRedevelopmentPct",
        "AnarchyFactor"};
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("CommercialZoneDeveloper");
    for (u32 index = 0; index < 7; ++index) {
      ScString key(keys[index], u32(strlen(keys[index])));
      if (ini.GetValue(&section, &key, &value))
        EcCommercialTuning[index] = EcParseTuningNumber(value.Data());
    }
    EcCommercialTuningLoaded = true;
  }
}

// Stop initialized services before retiring the three growth collections.
EcCommercialDeveloper::~EcCommercialDeveloper() {
  if (initialized)
    Shutdown();
  while (growth_records) {
    EcCommercialGrowth *entry = growth_records;
    growth_records = entry->next;
    delete entry;
  }
  while (growth_by_class) {
    EcCommercialIndex *entry = growth_by_class;
    growth_by_class = entry->next;
    delete entry;
  }
  while (growth_by_minimum) {
    EcCommercialIndex *entry = growth_by_minimum;
    growth_by_minimum = entry->next;
    delete entry;
  }
}

// Clear unsupported output and retain the primary developer identity for Unknown or its original IID.
bool EcCommercialDeveloper::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtZoneDeveloper);
  HANDLE_QUERY_INTERFACE(IID_CtZoneDeveloper, CtZoneDeveloper);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcCommercialDeveloper::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcCommercialDeveloper::Release() {
  return EcCommandOwner::Release();
}

// Retain the original commercial city services and parse ordered growth rows without undoing partial acquisitions.
u8 EcCommercialDeveloper::Init(CtCity *owner, const char *growth_data) {
  if (initialized)
    Shutdown();
  initialized = 1;
  city = owner;
  if (!city)
    return 0;
  city->AddRef();
  attributes = EcGetRciApplication()->GetAppOccupantAttribCache();
  if (!attributes)
    return 0;
  attributes->AddRef();
  agent_tree = EcGetRciApplication()->GetAppAgentTypeTree();
  if (!agent_tree)
    return 0;
  agent_tree->AddRef();
  buildings = city->BuildingLayer();
  if (!buildings)
    return 0;
  buildings->AddRef();
  power = city->PowerLayer();
  if (!power)
    return 0;
  power->AddRef();
  water = city->PlumbingLayer();
  if (!water)
    return 0;
  water->AddRef();
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return 0;
  occupants->AddRef();
  zones = city->ZoneLayer();
  if (!zones)
    return 0;
  zones->AddRef();
  traffic = city->TrafficLayer();
  if (!traffic)
    return 0;
  traffic->AddRef();
  ordinances = city->OrdinanceLayer();
  if (!ordinances)
    return 0;
  ordinances->AddRef();
  land_value = city->LandValueLayer();
  if (!land_value)
    return 0;
  land_value->AddRef();
  pollution = city->PollutionLayer();
  if (!pollution)
    return 0;
  pollution->AddRef();
  ScString remaining(growth_data), field = remaining.SplitToken(",");
  field = remaining.SplitToken(":");
  bool done = false;
  do {
    field = remaining.SplitToken(",");
    if (!field.Strlen())
      break;
    u32 identifier = EcParseTuningNumber(field.Data());
    field = remaining.SplitToken(",");
    u8 category = u8(EcParseTuningNumber(field.Data()));
    field = remaining.SplitToken(",");
    u8 maximum = u8(EcParseTuningNumber(field.Data()));
    field = remaining.SplitToken(";");
    if (!field.Strlen()) {
      field = remaining;
      done = true;
    }
    u8 minimum = u8(EcParseTuningNumber(field.Data()));
    if (minimum < minimum_growth)
      minimum_growth = minimum;
    if (maximum > maximum_growth)
      maximum_growth = maximum;
    EcCommercialIndex **index = &growth_by_minimum;
    while (*index && (*index)->key <= minimum)
      index = &(*index)->next;
    *index = new EcCommercialIndex{*index, minimum, identifier};
    index = &growth_by_class;
    while (*index && (*index)->key <= category)
      index = &(*index)->next;
    *index = new EcCommercialIndex{*index, category, identifier};
    EcCommercialGrowth **record = &growth_records;
    while (*record && (*record)->identifier < identifier)
      record = &(*record)->next;
    if (!*record || (*record)->identifier != identifier)
      *record = new EcCommercialGrowth{*record, identifier, 0, 0, 0};
    (*record)->category = category;
    (*record)->minimum = minimum;
    (*record)->maximum = maximum;
  } while (!done);
  return 1;
}

// Release retained services in field order, clear growth collections and retire rules from the back.
u8 EcCommercialDeveloper::Shutdown() {
  if (initialized) {
    initialized = 0;
    ScRelease(&city);
    ScRelease(&attributes);
    ScRelease(&agent_tree);
    ScRelease(&buildings);
    ScRelease(&power);
    ScRelease(&water);
    ScRelease(&occupants);
    ScRelease(&commercial_valve);
    ScRelease(&zones);
    ScRelease(&land_value);
    ScRelease(&traffic);
    ScRelease(&pollution);
    ScRelease(&ordinances);
    while (growth_by_minimum) {
      EcCommercialIndex *entry = growth_by_minimum;
      growth_by_minimum = entry->next;
      delete entry;
    }
    while (growth_by_class) {
      EcCommercialIndex *entry = growth_by_class;
      growth_by_class = entry->next;
      delete entry;
    }
    while (growth_records) {
      EcCommercialGrowth *entry = growth_records;
      growth_records = entry->next;
      delete entry;
    }
    while (rules.size) {
      EcZoneRule *rule = rules.data[rules.size - 1];
      if (rule) {
        rule->Shutdown();
        rule->Release();
      }
      --rules.size;
    }
  }
  return 1;
}

u32 EcCommercialDeveloper::SelectGrowth(u8 value, u8 include_all) {
  u32 selected = 0;
  u8 best_minimum = 0;
  for (EcCommercialIndex *index = growth_by_class; index; index = index->next) {
    EcCommercialGrowth *record = growth_records;
    while (record->identifier != index->identifier)
      record = record->next;
    if (record->minimum <= value && value <= record->maximum && record->minimum > best_minimum &&
        (include_all || record->category == 1)) {
      best_minimum = record->minimum;
      selected = index->identifier;
    }
  }
  return selected;
}

// Select a matching higher-value entry without reducing the current development category.
u32 EcCommercialDeveloper::SelectHigherValue(u32 current, u8 value, u8 include_all) {
  EcCommercialGrowth *previous = growth_records;
  while (previous && previous->identifier < current)
    previous = previous->next;
  if (!previous || previous->identifier != current)
    return 0;
  u8 best = previous->minimum, category = previous->category;
  u32 selected = 0;
  for (EcCommercialIndex *index = growth_by_minimum; index; index = index->next) {
    if (index->key < previous->minimum)
      continue;
    EcCommercialGrowth *record = growth_records;
    while (record->identifier != index->identifier)
      record = record->next;
    if (value >= index->key && value <= record->maximum && record->category >= category && index->key > best &&
        (include_all || record->category == 1)) {
      best = u8(index->key);
      selected = index->identifier;
    }
  }
  return selected;
}

u32 EcCommercialDeveloper::SelectLowerValue(u32, u8, u8) {
  return 0;
}

// Select the qualifying higher-category entry with the greatest minimum value.
u32 EcCommercialDeveloper::SelectHigherDensity(u32 current, u8 value) {
  EcCommercialGrowth *previous = growth_records;
  while (previous && previous->identifier < current)
    previous = previous->next;
  if (!previous || previous->identifier != current)
    return 0;
  u32 next_category = u32(previous->category) + 1;
  EcCommercialIndex *index = growth_by_class;
  while (index && index->key < next_category)
    index = index->next;
  if (!index || index->key != next_category)
    return 0;
  u32 selected = 0;
  u8 best = 0;
  for (; index; index = index->next) {
    EcCommercialGrowth *record = growth_records;
    while (record->identifier != index->identifier)
      record = record->next;
    if (record->minimum <= value && value <= record->maximum && record->minimum > best) {
      best = record->minimum;
      selected = index->identifier;
    }
  }
  return selected;
}

// Try up to three distinct development rules in original randomized order, stopping at the first accepted placement.
u8 EcCommercialDeveloper::TryRules(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  ScArray<u32> candidates;
  candidates.Reserve(rules.size);
  for (u32 index = 0; index < rules.size; ++index)
    *candidates.Append() = index;
  u32 attempts = rules.size < 3 ? rules.size : 3;
  u8 placed = 0;
  while (attempts-- && !placed) {
    u32 selected = random.Bounded(candidates.size);
    u32 rule_index = candidates.data[selected];
    EcZoneRule *rule = rules.data[rule_index];
    if (rule)
      placed = rule->Place(rectangle, identifier, demand, maximum);
    for (u32 index = selected + 1; index < candidates.size; ++index)
      candidates.data[index - 1] = candidates.data[index];
    --candidates.size;
  }
  return placed;
}

bool EcCommercialDeveloper::BindDemand() {
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  EcValveLayer *demand = nullptr;
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand)))
    return false;
  bool result = demand->GetValvePointer(kEcCommercialValveId, IID_EcValve,
      reinterpret_cast<void **>(&commercial_valve));
  demand->Release();
  return result;
}

// Retain the rule, publish the configured transport distance and append it to the actual rule list.
u8 EcCommercialDeveloper::AddRule(EcZoneRule *rule) {
  if (!rule)
    return 0;
  rule->AddRef();
  rule->SetMaxDistance(EcCommercialTuning[1]);
  *rules.Append() = rule;
  return 1;
}

u8 EcCommercialDeveloper::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 EcCommercialDeveloper::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

// Form the buildable commercial rectangle and ask registered rules to place the chosen growth type.
u8 EcCommercialDeveloper::TryDevelop(const ScPointXZ &point, u32 identifier, u32 existing, u32 distance) {
  i32 value = commercial_valve->QueryValveValue();
  u32 valve_id = commercial_valve->GetId();
  i32 demand = i32(u32(value) - zones->GetFilledDemand(valve_id));
  i32 maximum = commercial_valve->QueryValveMax();
  commercial_valve->QueryValveMin();
  if (value < 0 || demand < 0 || !identifier)
    return 0;
  EcCommercialGrowth *record = growth_records;
  while (record && record->identifier < identifier)
    record = record->next;
  if (!record || record->identifier != identifier)
    return 0;
  GeCellRect rectangle{};
  zones->FindBuildableRect(&rectangle, point.x, point.z, distance, record->minimum, existing, false);
  return TryRules(rectangle, identifier, demand, maximum);
}

// Resolve a building's growth type using the ordered ID table, then the live agent-type ancestry tree.
u32 EcCommercialDeveloper::FindGrowthType(u32 identifier) {
  EcCommercialGrowth *record = growth_records;
  while (record && record->identifier < identifier)
    record = record->next;
  if (record && record->identifier == identifier)
    return record->identifier;
  for (EcCommercialIndex *index = growth_by_class; index; index = index->next)
    if (agent_tree->IsAncestor(index->identifier, identifier))
      return index->identifier;
  return 0;
}

// Re-place the current building using its real attribute key, occupant rotation and requested occupancy state.
u8 EcCommercialDeveloper::ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state) {
  if (!building)
    return 0;
  GeAttrib *base = building->GetBaseAttribData();
  if (!base)
    return 0;
  u16 rotation = 0, pitch = 0, roll = 0;
  ScOccupant *occupant = nullptr;
  if (building->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&occupant))) {
    occupant->GetOrientation(&rotation, &pitch, &roll);
    occupant->Release();
  }
  RsKey *key = base->Key();
  return zones->PlaceBuilding(*key, point, i32(state), u16(360u * rotation / 65535u), true);
}

// Evaluate a commercial tile against utilities, transport, pollution, demand and existing occupancy.
// Preserve the original random decisions and building replacement order; placement remains delegated to actual rules.
i32 EcCommercialDeveloper::Evaluate(const ScPointXZ &point) {
  ScOccupant *occupant = nullptr;
  bool occupied = occupants->GetOccupantAtCell(point.x, point.z, &occupant);
  u32 distance_bonus = ordinances->QueryOrdinanceOn(kCtOrdinance_ShuttleService) ? 1u : 0u;
  u32 distance = distance_bonus + EcCommercialTuning[1];
  bool near_transport = zones->IsNearTransport(point, distance);
  bool powered = false;
  power->GetValue(point.x, point.z, &powered);
  u8 watered = water->IsWatered(point.x, point.z);
  u8 land = 0;
  land_value->GetValue(point.x, point.z, &land);
  bool radioactive = pollution->IsRadioactive(point.x, point.z);
  u8 removable = 1;
  bool has_building = false;
  BdBuilding *building = nullptr;
  u32 building_x = 0, building_z = 0;
  if (occupied) {
    removable = occupant->CanSimRemove();
    has_building = occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    occupant->GetLocationInCellCoords(&building_x, &building_z);
    occupant->Release();
  }
  i32 valve_value = commercial_valve->QueryValveValue();
  u32 valve_id = commercial_valve->GetId();
  i32 unfilled = i32(u32(valve_value) - zones->GetFilledDemand(valve_id));
  i32 maximum = commercial_valve->QueryValveMax();
  i32 minimum = commercial_valve->QueryValveMin();
  if (!has_building) {
    if (valve_value > 0 && unfilled > 0) {
      if (radioactive)
        return 10;
      if (!powered)
        return 14;
      if (!near_transport)
        return 16;
      if (land < minimum_growth)
        return 11;
      if (land > maximum_growth)
        return 12;
      u32 selected = SelectGrowth(land, watered);
      if (!selected)
        return watered ? 18 : 15;
      if (removable && powered && near_transport && !radioactive)
        TryDevelop(point, selected, 0, distance);
      return 0;
    }
    return valve_value < 0 ? 13 : 0;
  }
  if (point.x != building_x || point.z != building_z) {
    building->Release();
    return 0;
  }
  GeAttrib *base = building->GetBaseAttribData();
  CtAgentType *type = base->CityAgentType();
  bool locked = building->GetOccupancyStateFlag(kGeBuildingHistorical);
  u32 pending_flags = locked ? kGeBuildingHistorical : 0u;
  bool service_sensitive = false, pending_change = false;
  GeBuildingType *building_type = nullptr;
  if (base->QueryInterface(IID_GeBuildingType, reinterpret_cast<void **>(&building_type))) {
    service_sensitive = (building_type->Flags() & 0x100) != 0;
    building_type->Release();
    if (service_sensitive) {
      bool garbage = pollution->IsGarbageAccumulated(point.x, point.z);
      bool flagged = building->GetOccupancyStateFlag(kGeBuildingCoveredInGarbage);
      if (garbage && !flagged) {
        pending_flags |= kGeBuildingCoveredInGarbage;
        pending_change = true;
      } else if (!garbage && flagged) {
        pending_change = true;
      }
    }
  }
  u32 size = base->SizeInCellsX();
  u32 current = FindGrowthType(type->Id());
  u8 category = 1;
  if (current) {
    EcCommercialGrowth *record = growth_records;
    while (record && record->identifier < current)
      record = record->next;
    assert(record && record->identifier == current);
    category = record->category;
  }
  u8 trip = traffic->IsTripAvailable(building_x, building_z);
  u32 state = building->GetOccupancyState(), selected = 0;
  i32 result = 0;
  bool develop = false;
  if (state == kGeBuildingUnderConstruction) {
    if (!current && !service_sensitive) {
      if (valve_value > 0 && unfilled > 0 && !locked && removable)
        buildings->RemoveBuilding(building);
    } else if (powered && near_transport && !radioactive && random.Bounded(100u * size) < EcCommercialTuning[2]) {
      ReplaceBuilding(building, point, pending_flags | kGeBuildingOccupied);
      pending_change = false;
    }
  } else if (state == kGeBuildingAbandoned) {
    if (current || service_sensitive) {
      if (powered && near_transport && !trip && !radioactive && valve_value > 0 && unfilled > 0) {
        if (locked || (selected = SelectGrowth(land, watered)) == current) {
          ReplaceBuilding(building, point, pending_flags | kGeBuildingOccupied);
          pending_change = false;
        } else {
          develop = true;
        }
      }
    } else if (valve_value > 0 && unfilled > 0 && !radioactive && !locked) {
      selected = SelectGrowth(land, watered);
      develop = selected != 0;
    }
  } else if (state == kGeBuildingOccupied) {
    if (radioactive) {
      ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
      building->Release();
      return 1;
    }
    if (!powered || !near_transport || trip == 1 || (!watered && category > 1)) {
      assert(size != 0);
      if (random.Bounded(100) < EcCommercialTuning[0] / size) {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        pending_change = false;
        if (!powered) {
          building->Release();
          return 4;
        }
        if (!watered && category > 1) {
          building->Release();
          return 5;
        }
        result = 6;
      }
    } else {
      assert(size != 0);
      if (valve_value < 0 && i32(random.Bounded(0u - u32(minimum))) < i32(0u - u32(unfilled / i32(size)))) {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        building->Release();
        return 3;
      }
      if (current) {
        EcCommercialGrowth *record = growth_records;
        while (record && record->identifier < current)
          record = record->next;
        assert(record && record->identifier == current);
        if (land < (record->minimum >> 1)) {
          selected = SelectLowerValue(current, land, watered);
          if (!selected || random.Bounded(100) >= EcCommercialTuning[4]) {
            ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
            pending_change = false;
            result = 2;
          } else {
            develop = true;
          }
          goto finish_building;
        }
      }
      if (!locked && valve_value > 0 && current && (selected = SelectHigherValue(current, land, watered))) {
        if (random.Bounded(100) < EcCommercialTuning[3])
          develop = true;
      } else if (watered && !locked) {
        if (valve_value > 0 && current && i32(random.Bounded(u32(maximum) / size)) < unfilled) {
          selected = SelectHigherDensity(current, land);
          if (selected && random.Bounded(100) < EcCommercialTuning[5])
            develop = true;
        } else if (valve_value > 0 && current && random.Bounded(10000) < EcCommercialTuning[6]) {
          selected = current;
          develop = true;
        }
      }
    }
  }
finish_building:
  if (pending_change)
    ReplaceBuilding(building, point, pending_flags | state);
  building->Release();
  if (develop && removable && powered && near_transport && !radioactive)
    TryDevelop(point, selected, current, distance);
  return result;
}

// Create the commercial developer registered with the RCI director.
ScUnknown *EcCreateRciClass1003680E() {
  return new EcCommercialDeveloper;
}

// Place large commercial corners and fill remaining cells using the original transport-proximity weighting.
u8 EcPlaceCommercialLots(EcLotGroup *groups, EcRci::ScRandom *random, CtZoneLayer *zones, u32 minimum, u32 maximum_size,
    u32 threshold, u32 shore_chance, const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  assert(maximum != 0);
  if (i32(100u * u32(demand)) / maximum < i32(threshold))
    return 0;
  EcLotGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  GeCellRect area = rectangle;
  i32 width = i32(area.right - area.left + 1u), height = i32(area.bottom - area.top + 1u);
  if (width < i32(minimum) || height < i32(minimum))
    return 0;
  if (width > i32(maximum_size)) {
    ScPointXZ left_bottom{area.left, area.bottom}, left_top{area.left, area.top};
    i32 left_roads = i32(zones->GetRoadCount(left_bottom, left_top));
    ScPointXZ right_top{area.right, area.top}, right_bottom{area.right, area.bottom};
    i32 right_roads = i32(zones->GetRoadCount(right_top, right_bottom));
    if (left_roads <= right_roads)
      area.left = area.right - maximum_size + 1u;
    else
      area.right = area.left + maximum_size - 1u;
    width = i32(maximum_size);
  }
  if (height > i32(maximum_size)) {
    ScPointXZ left_top{area.left, area.top}, right_top{area.right, area.top};
    i32 top_roads = i32(zones->GetRoadCount(left_top, right_top));
    ScPointXZ right_bottom{area.right, area.bottom}, left_bottom{area.left, area.bottom};
    i32 bottom_roads = i32(zones->GetRoadCount(right_bottom, left_bottom));
    if (top_roads <= bottom_roads)
      area.top = area.bottom - maximum_size + 1u;
    else
      area.bottom = area.top + maximum_size - 1u;
    height = i32(maximum_size);
  }
  ScPointXZ corners[4] = {
      {area.left, area.top}, {area.right, area.top}, {area.right, area.bottom}, {area.left, area.bottom}};
  u32 roads[4];
  for (u32 edge = 0; edge < 4; ++edge)
    roads[edge] = zones->GetRoadCount(corners[edge], corners[(edge + 1) & 3]);
  u8 used[100]{};
  for (u32 corner = 0; corner < 4; ++corner) {
    u32 previous = (corner + 3) & 3;
    i32 previous_length = corner == 0 || corner == 2 ? height : width;
    i32 next_length = corner == 0 || corner == 2 ? width : height;
    if (roads[previous] != u32(previous_length) || roads[corner] != u32(next_length))
      continue;
    GeBuildingType *building = group->large.data[random->Bounded(group->large.size)];
    i32 size = i32(building->SizeInCellsX());
    if (size > width || size > height)
      continue;
    ScPointXZ point{corner == 1 || corner == 2 ? area.right - u32(size) + 1u : area.left,
        corner >= 2 ? area.bottom - u32(size) + 1u : area.top};
    if (zones->PlaceBuilding(building, point, 4, 0, true) && size > 0) {
      for (i32 x = 0; x < size; ++x)
        for (i32 z = 0; z < size; ++z)
          used[10u * (point.x - area.left + u32(x)) + point.z - area.top + u32(z)] = 1;
    }
  }
  for (u32 x = area.left; x <= area.right; ++x) {
    for (u32 z = area.top; z <= area.bottom; ++z) {
      u32 offset_x = x - area.left, offset_z = z - area.top;
      if (used[10u * offset_x + offset_z])
        continue;
      ScPointXZ point{x, z};
      u32 proximity_threshold = zones->IsNearTransport(point, 1) ? 100u - shore_chance : shore_chance;
      GeBuildingType *building;
      if (random->Bounded(100) >= proximity_threshold)
        building = group->shoreline.data[random->Bounded(group->shoreline.size)];
      else
        building = group->small.data[random->Bounded(group->small.size)];
      u32 size = building->SizeInCellsX();
      if (size + offset_x - 1u < u32(width) && size + offset_z - 1u < u32(height) &&
          zones->PlaceBuilding(building, point, 4, 0, true) && i32(size) > 0) {
        for (u32 row = 0; row < size; ++row)
          for (u32 column = 0; column < size; ++column)
            used[10u * (offset_x + row) + offset_z + column] = 1;
      }
    }
  }
  return 1;
}

// Original group owns nine ordered lists of borrowed building attributes.
struct EcDirectionalLotGroup {
  EcDirectionalLotGroup *next;
  u32 identifier;
  ScArray<GeBuildingType *> directions[9];
};

// Fill missing opposite-direction lists from their partner and reject groups without all required directions.
static u8 EcNormalizeDirectionalLotGroup(EcDirectionalLotGroup *group) {
  static const u8 pairs[4][2] = {{0, 1}, {2, 3}, {4, 7}, {6, 5}};
  for (u32 pair = 0; pair < 4; ++pair) {
    ScArray<GeBuildingType *> *first = &group->directions[pairs[pair][0]];
    ScArray<GeBuildingType *> *second = &group->directions[pairs[pair][1]];
    if (!first->size && !second->size)
      return 0;
    if (!first->size)
      for (u32 index = 0; index < second->size; ++index)
        *first->Append() = second->data[index];
    if (!second->size)
      for (u32 index = 0; index < first->size; ++index)
        *second->Append() = first->data[index];
  }
  return group->directions[8].size != 0;
}

EcDirectionalLotRule::EcDirectionalLotRule() : groups(nullptr) {
}

// Release the original group arrays without releasing their borrowed building attributes.
EcDirectionalLotRule::~EcDirectionalLotRule() {
  while (groups) {
    EcDirectionalLotGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Borrow the city's building layer, then its zoning layer. A missing layer leaves the preceding stores intact.
bool EcDirectionalLotRule::Init(CtCity *city) {
  buildings = city->BuildingLayer();
  if (!buildings)
    return false;
  zones = city->ZoneLayer();
  return zones != nullptr;
}

// Forget the two borrowed city layers without releasing either service.
u8 EcDirectionalLotRule::Shutdown() {
  buildings = nullptr;
  zones = nullptr;
  return 1;
}

u32 EcDirectionalLotRule::SetMaxDistance(u32 distance) {
  max_distance = distance;
  return distance;
}

// Classify two-cell buildings by orientation and collect one-cell fillers, preserving each source entry once.
void EcDirectionalLotRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcDirectionalLotGroup *group = new EcDirectionalLotGroup{};
    group->identifier = source->GetLandUse();
    static const u32 flags[] = {1, 2, 4, 8, 0x20, 0x80, 0x10, 0x40};
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 bits = building->Flags(), size = building->SizeInCellsX();
      if (size == 2)
        for (u32 direction = 0; direction < 8; ++direction)
          if (bits & flags[direction])
            *group->directions[direction].Append() = building;
      if ((bits & 0x100) && size == 1)
        *group->directions[8].Append() = building;
    }
    if (EcNormalizeDirectionalLotGroup(group)) {
      EcDirectionalLotGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

// Place road-adjacent corner lots, then fill qualifying perimeter edges and the center in the original order.
u8 EcDirectionalLotRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  i32 scaled = i32(u32(demand) * 50u);
  assert(maximum && (scaled != INT32_MIN || maximum != -1));
  if (scaled / maximum < 1)
    return 0;
  EcDirectionalLotGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  i32 width = i32(right - left + 1), height = i32(bottom - top + 1);
  if (width < 6 || width % 2 || height < 6 || height % 2)
    return 0;
  if (width > 8)
    right = left + 7;
  if (height > 8)
    bottom = top + 7;
  u32 right_inner = right - 1, bottom_inner = bottom - 1;
  auto place = [&](u32 list, const ScPointXZ &point) {
    ScArray<GeBuildingType *> *choices = &group->directions[list];
    GeBuildingType *building = choices->data[random.Bounded(choices->size)];
    return zones->PlaceBuilding(building, point, 4, 0, true);
  };
  bool upper_left = false, lower_right = false, lower_left = false, upper_right = false;
  if (zones->IsNearTransport({left + 1, top}, 0) && zones->IsNearTransport({left, top + 1}, 0))
    upper_left = place(0, {left, top});
  if (zones->IsNearTransport({right, bottom_inner}, 0) && zones->IsNearTransport({right_inner, bottom}, 0))
    lower_right = place(1, {right_inner, bottom_inner});
  if (zones->IsNearTransport({left + 1, bottom}, 0) && zones->IsNearTransport({left, bottom_inner}, 0))
    lower_left = place(2, {left, bottom_inner});
  if (zones->IsNearTransport({right, top + 1}, 0) && zones->IsNearTransport({right_inner, top}, 0))
    upper_right = place(3, {right_inner, top});
  if (upper_left) {
    if (lower_left)
      for (u32 z = top + 2; z < bottom_inner; z += 2)
        place(4, {left, z});
    if (upper_right)
      for (u32 x = left + 2; x < right_inner; x += 2)
        place(6, {x, top});
  } else if (!lower_right) {
    return lower_left || upper_right;
  }
  if (lower_right) {
    if (lower_left)
      for (u32 x = left + 2; x < right_inner; x += 2)
        place(5, {x, bottom_inner});
    if (upper_right)
      for (u32 z = top + 2; z < bottom_inner; z += 2)
        place(7, {right_inner, z});
  }
  if (upper_left && lower_right && lower_left && upper_right) {
    for (u32 x = left + 2; x < right_inner; ++x) {
      for (u32 z = top + 2; z < bottom_inner; ++z) {
        ScArray<GeBuildingType *> *choices = &group->directions[8];
        GeBuildingType *building = choices->data[random.Bounded(choices->size)];
        RsKey key = *building->Key();
        zones->PlaceBuilding(key, {x, z}, 1, 0, true);
      }
    }
  }
  return 1;
}

// Industrial growth records retain the four independent bytes packed by the installed insertion method.
struct EcIndustrialGrowth {
  EcIndustrialGrowth *next;
  u32 identifier;
  u8 threshold, minimum, maximum, category;
};

// Each original ordered index holds duplicate byte keys and their associated full-width growth identifiers.
struct EcIndustrialIndex {
  EcIndustrialIndex *next;
  u32 key, identifier;
};

static bool EcIndustrialTuningLoaded;
static u32 EcIndustrialTuning[] = {
    30, 5, 75, 30, 10, 30, 10, 2065, 5, 45, 3010, 0, 95, 30, 5, 5, 15, 15, 15, 10, 5, 20, 10, 7, 5, 4, 4, 2, 4, 3};

EcIndustrialDeveloper::EcIndustrialDeveloper()
    : initialized(0), minimum_growth(255), maximum_growth(0), growth_indexes{}, growth_records(nullptr),
      random(kScRandomSeedFromClock), city(nullptr), attributes(nullptr), agent_tree(nullptr), buildings(nullptr),
      power(nullptr), water(nullptr), occupants(nullptr), industrial_valve(nullptr), zones(nullptr),
      land_value(nullptr), traffic(nullptr), pollution(nullptr), residential(nullptr), ordinances(nullptr),
      minimum_clean(u8(EcIndustrialTuning[8])), maximum_clean(u8(EcIndustrialTuning[11])), clean_initialized(0) {
  if (!EcIndustrialTuningLoaded) {
    static const char *const keys[] = {"NoServicePercentage", "MaxDistFromTransport", "ConstructionTimeFactor",
        "HigherLandValueRedevelopmentPct", "LowerLandValueRedevelopmentPct", "HigherDensityRedevelopmentPct",
        "AnarchyFactor", "MinCleanIndFlatYear", "MinMinCleanInd", "MaxMinCleanInd", "MaxCleanIndFlatYear",
        "MinMaxCleanInd", "MaxMaxCleanInd", "PctChanceAg", "MinAgAirPollution", "MinAgWaterPollution",
        "MaxAgAirPollution", "MaxAgWaterPollution", "CleanAirOrdEffect", "TouristPromotionOrdEffect",
        "ConservationCorpsOrdEffect", "IndPolluteFeeOrdEffect", "CleanIndAssocOrdEffect",
        "ElectronicsIncentiveOrdEffect", "AerospaceIncentiveOrdEffect", "ElectronicsJobFairOrdEffect",
        "BiotechIncentiveOrdEffect", "PublicAccessCableOrdEffect", "LawnChemBanOrdEffect", "FarmersMarketOrdEffect"};
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("IndustrialZoneDeveloper");
    for (u32 index = 0; index < 30; ++index) {
      ScString key(keys[index], u32(strlen(keys[index])));
      if (ini.GetValue(&section, &key, &value)) {
        u32 number = EcParseTuningNumber(value.Data());
        EcIndustrialTuning[index] =
            index == 8 || index == 9 || index == 11 || index == 12 || index >= 18 ? u8(number) : number;
      }
    }
    EcIndustrialTuningLoaded = true;
  }
}

// The ordinary destructor is reached through its secondary lifetime receiver.
// Release initialized services before retiring the growth indexes and identifier records.
EcIndustrialDeveloper::~EcIndustrialDeveloper() {
  if (initialized)
    Shutdown();
  for (u32 index = 0; index < 6; ++index) {
    while (growth_indexes[index]) {
      EcIndustrialIndex *entry = growth_indexes[index];
      growth_indexes[index] = entry->next;
      delete entry;
    }
  }
  while (growth_records) {
    EcIndustrialGrowth *entry = growth_records;
    growth_records = entry->next;
    delete entry;
  }
}

// Publish the real developer identity for the original two supported interface identifiers.
bool EcIndustrialDeveloper::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtZoneDeveloper);
  HANDLE_QUERY_INTERFACE(IID_CtZoneDeveloper, CtZoneDeveloper);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcIndustrialDeveloper::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcIndustrialDeveloper::Release() {
  return EcCommandOwner::Release();
}

// Retain the installed industrial services and load the three category-specific growth indexes in source order.
u8 EcIndustrialDeveloper::Init(CtCity *owner, const char *growth_data) {
  if (initialized)
    Shutdown();
  clean_initialized = 0;
  initialized = 1;
  city = owner;
  if (!city)
    return 0;
  city->AddRef();
  attributes = EcGetRciApplication()->GetAppOccupantAttribCache();
  if (!attributes)
    return 0;
  attributes->AddRef();
  agent_tree = EcGetRciApplication()->GetAppAgentTypeTree();
  if (!agent_tree)
    return 0;
  agent_tree->AddRef();
  buildings = city->BuildingLayer();
  if (!buildings)
    return 0;
  buildings->AddRef();
  power = city->PowerLayer();
  if (!power)
    return 0;
  power->AddRef();
  water = city->PlumbingLayer();
  if (!water)
    return 0;
  water->AddRef();
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return 0;
  occupants->AddRef();
  zones = city->ZoneLayer();
  if (!zones)
    return 0;
  zones->AddRef();
  traffic = city->TrafficLayer();
  if (!traffic)
    return 0;
  traffic->AddRef();
  land_value = city->LandValueLayer();
  if (!land_value)
    return 0;
  land_value->AddRef();
  pollution = city->PollutionLayer();
  if (!pollution)
    return 0;
  pollution->AddRef();
  residential = city->ResidentialLayer();
  if (!residential)
    return 0;
  residential->AddRef();
  ordinances = city->OrdinanceLayer();
  if (!ordinances)
    return 0;
  ordinances->AddRef();
  ScString remaining(growth_data), field = remaining.SplitToken(",");
  field = remaining.SplitToken(":");
  u32 count = 0;
  bool done = false;
  do {
    field = remaining.SplitToken(",");
    if (!field.Strlen())
      break;
    u32 identifier = EcParseTuningNumber(field.Data());
    field = remaining.SplitToken(",");
    char letter = field.Strlen() ? field.Data()[0] : 0;
    u8 category = letter == 'A' ? 2 : letter == 'C' ? 0 : 1;
    field = remaining.SplitToken(",");
    u8 threshold = u8(EcParseTuningNumber(field.Data()));
    field = remaining.SplitToken(",");
    u8 maximum = u8(EcParseTuningNumber(field.Data()));
    field = remaining.SplitToken(";");
    if (!field.Strlen()) {
      field = remaining;
      done = true;
    }
    u8 minimum = u8(EcParseTuningNumber(field.Data()));
    if (minimum < minimum_growth)
      minimum_growth = minimum;
    if (maximum > maximum_growth)
      maximum_growth = maximum;
    u32 keys[2] = {maximum, threshold};
    for (u32 index = 0; index < 2; ++index) {
      EcIndustrialIndex **entry = &growth_indexes[2 * category + index];
      while (*entry && (*entry)->key <= keys[index])
        entry = &(*entry)->next;
      *entry = new EcIndustrialIndex{*entry, keys[index], identifier};
    }
    EcIndustrialGrowth **entry = &growth_records;
    while (*entry && (*entry)->identifier < identifier)
      entry = &(*entry)->next;
    if (!*entry || (*entry)->identifier != identifier)
      *entry = new EcIndustrialGrowth{*entry, identifier, 0, 0, 0, 0};
    (*entry)->threshold = threshold;
    (*entry)->minimum = minimum;
    (*entry)->maximum = maximum;
    (*entry)->category = category;
    ++count;
  } while (!done);
  return count != 0;
}

// Release the retained city services in original order, clear the growth records and retire rules from the end.
u8 EcIndustrialDeveloper::Shutdown() {
  initialized = 0;
  if (city)
    city->Release();
  city = nullptr;
  if (attributes)
    attributes->Release();
  attributes = nullptr;
  if (agent_tree)
    agent_tree->Release();
  agent_tree = nullptr;
  if (buildings)
    buildings->Release();
  buildings = nullptr;
  if (power)
    power->Release();
  power = nullptr;
  if (water)
    water->Release();
  water = nullptr;
  if (occupants)
    occupants->Release();
  occupants = nullptr;
  if (industrial_valve)
    industrial_valve->Release();
  industrial_valve = nullptr;
  if (zones)
    zones->Release();
  zones = nullptr;
  if (traffic)
    traffic->Release();
  traffic = nullptr;
  if (land_value)
    land_value->Release();
  land_value = nullptr;
  if (pollution)
    pollution->Release();
  pollution = nullptr;
  if (residential)
    residential->Release();
  residential = nullptr;
  if (ordinances)
    ordinances->Release();
  ordinances = nullptr;
  for (u32 index = 0; index < 6; ++index) {
    while (growth_indexes[index]) {
      EcIndustrialIndex *entry = growth_indexes[index];
      growth_indexes[index] = entry->next;
      delete entry;
    }
  }
  while (growth_records) {
    EcIndustrialGrowth *entry = growth_records;
    growth_records = entry->next;
    delete entry;
  }
  while (rules.size) {
    EcZoneRule *rule = rules.data[rules.size - 1];
    if (rule) {
      rule->Shutdown();
      rule->Release();
    }
    --rules.size;
  }
  return 1;
}

// Bind the real industrial valve and initialize the date-based clean-industry limits when required.
bool EcIndustrialDeveloper::BindDemand() {
  if (!initialized)
    return false;
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  EcValveLayer *demand = nullptr;
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand));
  if (!demand)
    return false;
  bool result = demand->GetValvePointer(kEcIndustrialValveId, IID_EcValve,
      reinterpret_cast<void **>(&industrial_valve));
  demand->Release();
  if (!result)
    return false;
  if (!clean_initialized) {
    u32 month, day, year;
    city->GetDate(&month, &day, &year);
    UpdateCleanIndustry(year);
  }
  return true;
}

// Interpolate the industrial clean-industry bounds from the current year and the installed tuning thresholds.
void EcIndustrialDeveloper::UpdateCleanIndustry(u32 year) {
  if (year >= EcIndustrialTuning[7])
    minimum_clean = u8(EcIndustrialTuning[9]);
  else
    minimum_clean = u8(EcIndustrialTuning[8] +
        (year - 1900) * (EcIndustrialTuning[9] - EcIndustrialTuning[8]) / (EcIndustrialTuning[7] - 1900));
  if (year >= EcIndustrialTuning[10])
    maximum_clean = u8(EcIndustrialTuning[12]);
  else
    maximum_clean = u8(EcIndustrialTuning[11] +
        (year - 1900) * (EcIndustrialTuning[12] - EcIndustrialTuning[11]) / (EcIndustrialTuning[10] - 1900));
  clean_initialized = 1;
}

// Choose agricultural, clean or other industrial growth using the active ordinances and workforce education.
u32 EcIndustrialDeveloper::SelectGrowth(u8 value, u8 allow_agriculture, u8 include_all) {
  static const u32 kOrdinanceIds[] = {kCtOrdinance_CleanAir, kCtOrdinance_TouristPromotion,
      kCtOrdinance_ConservationCorps, kCtOrdinance_IndustrialPollutionFee, kCtOrdinance_CleanIndustryAssociation,
      kCtOrdinance_ElectronicsTaxIncentive, kCtOrdinance_AerospaceTaxIncentive, kCtOrdinance_ElectronicsJobFair,
      kCtOrdinance_BiotechTaxIncentives, kCtOrdinance_PublicAccessCable, kCtOrdinance_LawnChemicalBan,
      kCtOrdinance_FarmersMarket};
  bool enabled[12];
  for (u32 index = 0; index < 12; ++index)
    enabled[index] = ordinances->QueryOrdinanceOn(kOrdinanceIds[index]);
  i32 agriculture = i32(EcIndustrialTuning[13]);
  if (enabled[10])
    agriculture = i32(u32(agriculture) - EcIndustrialTuning[28]);
  if (enabled[11])
    agriculture = i32(u32(agriculture) + EcIndustrialTuning[29]);
  u8 category;
  if (growth_indexes[5] && agriculture > 0 && allow_agriculture && random.Bounded(100) < u32(agriculture)) {
    category = 2;
  } else if (zones->NerdsRule()) {
    category = 0;
  } else {
    i32 education = residential->GetWorkforceEQ();
    if (education < 85)
      education = 85;
    i32 clean = 100 * (education - 85) / 65;
    if (clean < minimum_clean)
      clean = minimum_clean;
    else if (clean > maximum_clean)
      clean = maximum_clean;
    i32 dirty = 100 - clean;
    if (enabled[0])
      dirty -= i32(EcIndustrialTuning[18]);
    if (enabled[1])
      clean += i32(EcIndustrialTuning[19]);
    if (enabled[2])
      clean += i32(EcIndustrialTuning[20]);
    if (enabled[3])
      dirty -= i32(EcIndustrialTuning[21]);
    if (enabled[4]) {
      clean += i32(EcIndustrialTuning[22]);
      dirty -= i32(EcIndustrialTuning[22]);
    }
    for (u32 index = 5; index < 10; ++index)
      if (enabled[index])
        clean += i32(EcIndustrialTuning[18 + index]);
    if (clean > 0 && random.Bounded(100) < u32(clean))
      category = 0;
    else if (dirty > 0)
      category = 1;
    else
      return 0;
  }
  u32 selected = 0;
  u8 best = 0;
  for (EcIndustrialIndex *entry = growth_indexes[2 * category + 1]; entry; entry = entry->next) {
    EcIndustrialGrowth *record = growth_records;
    while (record->identifier != entry->identifier)
      record = record->next;
    if (record->minimum <= value && record->maximum >= value && record->minimum > best &&
        (include_all || record->threshold == 1)) {
      best = record->minimum;
      selected = entry->identifier;
    }
  }
  return selected;
}

// Find a higher-value industrial growth entry in the current category without lowering its density.
u32 EcIndustrialDeveloper::SelectHigherValue(u32 current, u8 value, u8 include_all) {
  EcIndustrialGrowth *previous = growth_records;
  while (previous && previous->identifier < current)
    previous = previous->next;
  if (!previous || previous->identifier != current)
    return 0;
  EcIndustrialIndex *entry = growth_indexes[2 * previous->category];
  while (entry && entry->key < previous->minimum)
    entry = entry->next;
  if (!entry || entry->key != previous->minimum)
    return 0;
  u8 best = previous->minimum;
  u32 selected = 0;
  for (; entry; entry = entry->next) {
    EcIndustrialGrowth *record = growth_records;
    while (record->identifier != entry->identifier)
      record = record->next;
    if (entry->key <= value && record->maximum >= value && record->threshold >= previous->threshold &&
        entry->key > best && (include_all || record->threshold == 1)) {
      best = u8(entry->key);
      selected = entry->identifier;
    }
  }
  return selected;
}

u32 EcIndustrialDeveloper::SelectLowerValue(u32, u8, u8) {
  return 0;
}

// Select a higher-density entry in the existing industrial category whose value range contains the cell.
u32 EcIndustrialDeveloper::SelectHigherDensity(u32 current, u8 value) {
  EcIndustrialGrowth *previous = growth_records;
  while (previous && previous->identifier < current)
    previous = previous->next;
  if (!previous || previous->identifier != current)
    return 0;
  u32 key = u32(previous->threshold) + 1;
  EcIndustrialIndex *entry = growth_indexes[2 * previous->category + 1];
  while (entry && entry->key < key)
    entry = entry->next;
  if (!entry || entry->key != key)
    return 0;
  u8 best = 0;
  u32 selected = 0;
  for (; entry; entry = entry->next) {
    EcIndustrialGrowth *record = growth_records;
    while (record->identifier != entry->identifier)
      record = record->next;
    if (record->minimum <= value && record->maximum >= value && record->minimum > best) {
      best = record->minimum;
      selected = entry->identifier;
    }
  }
  return selected;
}

// Try up to three randomly selected registered rules without replacement until a placement succeeds.
u8 EcIndustrialDeveloper::TryRules(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  ScArray<u32> candidates;
  candidates.Reserve(rules.size);
  for (u32 index = 0; index < rules.size; ++index)
    *candidates.Append() = index;
  u32 attempts = rules.size > 3 ? 3 : rules.size;
  u8 placed = 0;
  while (attempts-- && !placed) {
    u32 position = random.Bounded(candidates.size);
    EcZoneRule *rule = rules.data[candidates.data[position]];
    if (rule)
      placed = rule->Place(rectangle, identifier, demand, maximum);
    for (u32 index = position + 1; index < candidates.size; ++index)
      candidates.data[index - 1] = candidates.data[index];
    --candidates.size;
  }
  return placed;
}

// Form the industrial buildable area and retry a failed agricultural type through the ordinary growth selection.
u8 EcIndustrialDeveloper::TryDevelop(const ScPointXZ &point, u32 identifier, u32 existing, u32 distance_bonus) {
  i32 value = industrial_valve->QueryValveValue();
  u32 valve_id = industrial_valve->GetId();
  i32 demand = i32(u32(value) - zones->GetFilledDemand(valve_id));
  i32 maximum = industrial_valve->QueryValveMax();
  industrial_valve->QueryValveMin();
  if (value < 0 || demand < 0 || !identifier)
    return 0;
  EcIndustrialGrowth *record = growth_records;
  while (record && record->identifier < identifier)
    record = record->next;
  if (!record || record->identifier != identifier)
    return 0;
  u32 distance = identifier == kCtAgentAgriculturalIndustry ? 2 * EcIndustrialTuning[1] :
      distance_bonus + EcIndustrialTuning[1];
  GeCellRect rectangle{};
  zones->FindBuildableRect(&rectangle, point.x, point.z, distance, record->minimum, existing, false);
  u8 placed = TryRules(rectangle, identifier, demand, maximum);
  if (!placed && identifier == kCtAgentAgriculturalIndustry) {
    u8 watered = water->IsWatered(point.x, point.z), land = 0;
    land_value->GetValue(point.x, point.z, &land);
    u32 replacement = SelectGrowth(land, 0, watered);
    if (replacement && replacement != kCtAgentAgriculturalIndustry)
      return TryDevelop(point, replacement, existing, distance_bonus);
  }
  return placed;
}

u32 EcIndustrialDeveloper::FindGrowthType(u32 identifier) {
  EcIndustrialGrowth *record = growth_records;
  while (record && record->identifier < identifier)
    record = record->next;
  if (record && record->identifier == identifier)
    return record->identifier;
  for (u32 category = 0; category < 3; ++category)
    for (EcIndustrialIndex *entry = growth_indexes[2 * category + 1]; entry; entry = entry->next)
      if (agent_tree->IsAncestor(entry->identifier, identifier))
        return entry->identifier;
  return 0;
}

// Re-place the current building using its base resource key, occupant orientation and requested occupancy state.
u8 EcIndustrialDeveloper::ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state) {
  if (!building)
    return 0;
  GeAttrib *base = building->GetBaseAttribData();
  if (!base)
    return 0;
  u16 rotation = 0, pitch = 0, roll = 0;
  ScOccupant *occupant = nullptr;
  if (building->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&occupant))) {
    occupant->GetOrientation(&rotation, &pitch, &roll);
    occupant->Release();
  }
  RsKey *key = base->Key();
  return zones->PlaceBuilding(*key, point, i32(state), u16(360u * rotation / 65535u), true);
}

// Evaluate industrial occupancy, utilities, pollution, demand and agriculture before applying development decisions.
i32 EcIndustrialDeveloper::Evaluate(const ScPointXZ &point) {
  ScOccupant *occupant = nullptr;
  bool occupied = occupants->GetOccupantAtCell(point.x, point.z, &occupant);
  u32 distance_bonus = ordinances->QueryOrdinanceOn(kCtOrdinance_ShuttleService) ? 1u : 0u;
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  if (month == 1) {
    if (!clean_initialized)
      UpdateCleanIndustry(year);
  } else {
    clean_initialized = 0;
  }
  bool powered = false;
  power->GetValue(point.x, point.z, &powered);
  u8 watered = water->IsWatered(point.x, point.z), land = 0;
  land_value->GetValue(point.x, point.z, &land);
  u32 air = 0, water_pollution = 0;
  pollution->GetAirValue(point.x, point.z, &air);
  pollution->GetWaterValue(point.x, point.z, &water_pollution);
  bool radioactive = pollution->IsRadioactive(point.x, point.z);
  bool agriculture = air <= EcIndustrialTuning[14] && water_pollution <= EcIndustrialTuning[15];
  bool near_transport = zones->IsNearTransport(point, distance_bonus + EcIndustrialTuning[1]);
  u8 removable = 1;
  bool has_building = false;
  BdBuilding *building = nullptr;
  u32 building_x = 0, building_z = 0;
  if (occupied) {
    removable = occupant->CanSimRemove();
    has_building = occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    occupant->GetLocationInCellCoords(&building_x, &building_z);
    occupant->Release();
  }
  i32 valve_value = industrial_valve->QueryValveValue();
  u32 valve_id = industrial_valve->GetId();
  i32 unfilled = i32(u32(valve_value) - zones->GetFilledDemand(valve_id));
  i32 maximum = industrial_valve->QueryValveMax();
  i32 minimum = industrial_valve->QueryValveMin();
  if (!has_building) {
    if (valve_value > 0 && unfilled > 0) {
      if (radioactive)
        return 10;
      if (!powered)
        return 14;
      if (!near_transport)
        return 16;
      if (land < minimum_growth)
        return 11;
      if (land > maximum_growth)
        return 12;
      u32 selected = SelectGrowth(land, agriculture, watered);
      if (!selected && agriculture)
        selected = SelectGrowth(land, 0, watered);
      if (!selected)
        return watered ? 12 : 15;
      if (removable && powered && near_transport && !radioactive)
        TryDevelop(point, selected, 0, distance_bonus);
    }
    return 0;
  }
  if (point.x != building_x || point.z != building_z) {
    building->Release();
    return 0;
  }
  GeAttrib *base = building->GetBaseAttribData();
  CtAgentType *type = base->CityAgentType();
  bool locked = building->GetOccupancyStateFlag(kGeBuildingHistorical);
  u32 pending_flags = locked ? kGeBuildingHistorical : 0u;
  bool service_sensitive = false, farm_flag = false, pending_change = false;
  GeBuildingType *building_type = nullptr;
  if (base->QueryInterface(IID_GeBuildingType, reinterpret_cast<void **>(&building_type))) {
    u32 flags = building_type->Flags();
    service_sensitive = (flags & 0x100) != 0;
    farm_flag = (flags & 0x400) != 0;
    building_type->Release();
    if (service_sensitive) {
      bool garbage = pollution->IsGarbageAccumulated(point.x, point.z);
      bool flagged = building->GetOccupancyStateFlag(kGeBuildingCoveredInGarbage);
      if (garbage && !flagged) {
        pending_flags |= kGeBuildingCoveredInGarbage;
        pending_change = true;
      } else if (!garbage && flagged) {
        pending_change = true;
      }
    }
  }
  u32 size = base->SizeInCellsX();
  u32 current = FindGrowthType(type->Id());
  u8 density = 1;
  if (current) {
    EcIndustrialGrowth *record = growth_records;
    while (record && record->identifier < current)
      record = record->next;
    assert(record && record->identifier == current);
    density = record->threshold;
  }
  bool special_farm = service_sensitive && farm_flag && current == kCtAgentAgriculturalIndustry;
  u8 trip = traffic->IsTripAvailable(building_x, building_z);
  u32 state = building->GetOccupancyState(), selected = 0;
  i32 result = 0;
  bool develop = false;
  if (state == kGeBuildingUnderConstruction) {
    if (!current && !service_sensitive) {
      if (valve_value > 0 && unfilled > 0 && !locked && removable)
        buildings->RemoveBuilding(building);
    } else if (powered && near_transport && !radioactive && random.Bounded(100u * size) < EcIndustrialTuning[2]) {
      ReplaceBuilding(building, point, pending_flags | kGeBuildingOccupied);
      pending_change = false;
    }
  } else if (state == kGeBuildingAbandoned) {
    if (current || service_sensitive) {
      if (powered && near_transport && !trip && !radioactive && valve_value > 0 && unfilled > 0) {
        if (locked || (selected = SelectGrowth(land, agriculture, watered)) == current) {
          ReplaceBuilding(building, point, pending_flags | kGeBuildingOccupied);
          pending_change = false;
        } else {
          develop = true;
        }
      }
    } else if (unfilled > 0 && valve_value > 0 && !radioactive && !locked) {
      selected = SelectGrowth(land, agriculture, watered);
      develop = selected != 0;
    }
  } else if (state == kGeBuildingOccupied) {
    if (radioactive) {
      if (special_farm) {
        if (removable && !locked) {
          buildings->RemoveBuilding(building);
          result = 1;
        }
      } else {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        pending_change = false;
        result = 1;
      }
      goto finish_building;
    }
    if ((!powered || !near_transport || trip == 1 || (density > 1 && !watered)) && !special_farm) {
      assert(size != 0);
      if (random.Bounded(100) < EcIndustrialTuning[0] / size) {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        pending_change = false;
        if (!powered) {
          building->Release();
          return 4;
        }
        if (!watered && density > 1) {
          building->Release();
          return 5;
        }
        result = 6;
      }
      goto finish_building;
    }
    if (current == kCtAgentAgriculturalIndustry &&
        (air > EcIndustrialTuning[16] || water_pollution > EcIndustrialTuning[17])) {
      if (special_farm) {
        if (removable && !locked) {
          buildings->RemoveBuilding(building);
          result = 7;
        }
      } else {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        pending_change = false;
        result = 7;
      }
      goto finish_building;
    }
    assert(size != 0);
    if (valve_value < 0 && i32(random.Bounded(0u - u32(minimum))) < i32(0u - u32(unfilled / i32(size))) &&
        !special_farm) {
      ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
      building->Release();
      return 3;
    }
    if (current) {
      EcIndustrialGrowth *record = growth_records;
      while (record && record->identifier < current)
        record = record->next;
      assert(record && record->identifier == current);
      if (land < (record->minimum >> 1)) {
        selected = SelectLowerValue(current, land, watered);
        if (!selected || random.Bounded(100) >= EcIndustrialTuning[4]) {
          if (service_sensitive && current == kCtAgentAgriculturalIndustry) {
            if (removable && !locked) {
              buildings->RemoveBuilding(building);
              result = 2;
            }
          } else {
            ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
            pending_change = false;
            result = 2;
          }
        } else {
          develop = true;
        }
        goto finish_building;
      }
    }
    if (!locked && valve_value > 0 && current && (selected = SelectHigherValue(current, land, watered))) {
      if (random.Bounded(100) < EcIndustrialTuning[3])
        develop = true;
    } else if (watered && !locked) {
      if (valve_value > 0 && current && i32(random.Bounded(u32(maximum) / size)) < unfilled) {
        selected = SelectHigherDensity(current, land);
        if (selected && random.Bounded(100) < EcIndustrialTuning[5])
          develop = true;
      } else if (valve_value > 0 && current && random.Bounded(10000) < EcIndustrialTuning[6]) {
        selected = current;
        develop = true;
      }
    }
  }
finish_building:
  if (pending_change)
    ReplaceBuilding(building, point, pending_flags | state);
  building->Release();
  if (develop && removable && powered && near_transport && !radioactive)
    TryDevelop(point, selected, current, distance_bonus);
  return result;
}

// Retain the development rule, set its configured transport distance and append it in original order.
u8 EcIndustrialDeveloper::AddRule(EcZoneRule *rule) {
  if (!rule)
    return 0;
  rule->AddRef();
  rule->SetMaxDistance(EcIndustrialTuning[1]);
  *rules.Append() = rule;
  return 1;
}

u8 EcIndustrialDeveloper::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 EcIndustrialDeveloper::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

// Construct the industrial zone developer registered by the SIMRCI director.
ScUnknown *EcCreateRciClass10036843() {
  return static_cast<CtZoneDeveloper *>(new EcIndustrialDeveloper);
}

// The original group stores one identifier and two ordered weighted lists of borrowed building types.
struct EcIndustrialStripGroup {
  EcIndustrialStripGroup *next;
  u32 identifier;
  ScArray<GeBuildingType *> buildings, shore;
};

EcIndustrialStripRule::EcIndustrialStripRule() : groups(nullptr), occupants(nullptr) {
}

// Retire the group's pointer arrays without releasing the borrowed building descriptions.
EcIndustrialStripRule::~EcIndustrialStripRule() {
  while (groups) {
    EcIndustrialStripGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Is shared with a related commercial rule.
// Borrow building, surface-occupant and zoning services in the original order.
bool EcIndustrialStripRule::Init(CtCity *city) {
  buildings = city->BuildingLayer();
  if (!buildings)
    return false;
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return false;
  zones = city->ZoneLayer();
  return zones != nullptr;
}

// Clear the three borrowed city-service pointers in the installed callback order.
u8 EcIndustrialStripRule::Shutdown() {
  buildings = nullptr;
  zones = nullptr;
  occupants = nullptr;
  return 1;
}

// Populate the weighted multi-cell and shoreline lists, retaining only a configuration containing both kinds.
void EcIndustrialStripRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcIndustrialStripGroup *group = new EcIndustrialStripGroup{};
    group->identifier = source->GetLandUse();
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      ScArray<GeBuildingType *> *list = nullptr;
      if ((flags & 0x100) && size == 1)
        list = &group->shore;
      else if (!(flags & 0x100) && size > 1)
        list = &group->buildings;
      if (list)
        for (i32 weight = i32(entries[index].weight); weight > 0; --weight)
          *list->Append() = building;
    }
    if (group->buildings.size && group->shore.size) {
      EcIndustrialStripGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

// Select the configured industrial building and dispatch to the original one-, two- or three-deep placement routine.
u8 EcIndustrialStripRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  i32 scaled = i32(u32(demand) * 100u);
  assert(maximum && (scaled != INT32_MIN || maximum != -1));
  if (scaled / maximum < 1)
    return 0;
  EcIndustrialStripGroup *group = groups;
  u32 index = 0;
  while (group && group->identifier != identifier) {
    group = group->next;
    ++index;
  }
  if (!group)
    return 0;
  GeBuildingType *selected = group->buildings.data[random.Bounded(group->buildings.size)];
  u32 choice = random.Bounded(100);
  if (choice < 33)
    return PlaceTwo(rectangle, index, selected);
  if (choice < 66)
    return PlaceThree(rectangle, index, selected);
  return PlaceOne(rectangle, index, selected);
}

void EcIndustrialStripRule::PlaceSelected(GeBuildingType *building, const ScPointXZ &point, u8 reverse) {
  RsKey key = *building->Key();
  zones->PlaceBuilding(key, point, 4, reverse ? 180 : 0, true);
}

// Find a road-lined corner, place three chosen buildings and fill the fourth square with weighted shoreline pieces.
u8 EcIndustrialStripRule::PlaceTwo(const GeCellRect &rectangle, u32 group_index, GeBuildingType *building) {
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  u32 size = building->SizeInCellsX(), span = 2 * size;
  if (right - left + 1 < span || i32(bottom - top + 1) < i32(span))
    return 0;
  u32 start = random.Bounded(4), direction = start;
  do {
    ScPointXZ first, last;
    if (direction == 0) {
      first = {left, top + span - 1};
      last = {left, top};
    } else if (direction == 1) {
      first = {right - span + 1, top};
      last = {right, top};
    } else if (direction == 2) {
      first = {right, bottom - span + 1};
      last = {right, bottom};
    } else {
      first = {left + span - 1, bottom};
      last = {left, bottom};
    }
    if (zones->GetRoadCount(first, last) == span) {
      if (direction == 0) {
        first = {left, top};
        last = {left + span - 1, top};
      } else if (direction == 1) {
        first = {right, top};
        last = {right, top + span - 1};
      } else if (direction == 2) {
        first = {right, bottom};
        last = {right - span + 1, bottom};
      } else {
        first = {left, bottom};
        last = {left, bottom - span + 1};
      }
      if (zones->GetRoadCount(first, last) == span)
        break;
    }
    direction = (direction + 1) & 3;
    if (direction == start)
      return 0;
  } while (true);
  u32 fill_x, fill_z;
  if (direction == 0) {
    PlaceSelected(building, {left, top}, 0);
    PlaceSelected(building, {left, top + size}, 0);
    PlaceSelected(building, {left + size, top}, 0);
    fill_x = left + size;
    fill_z = top + size;
  } else if (direction == 1) {
    u32 x = right - span + 1;
    PlaceSelected(building, {x, top}, 0);
    PlaceSelected(building, {x + size, top}, 0);
    PlaceSelected(building, {x + size, top + size}, 0);
    fill_x = x;
    fill_z = top + size;
  } else if (direction == 2) {
    u32 x = right - span + 1, z = bottom - size + 1;
    PlaceSelected(building, {x, z}, 0);
    PlaceSelected(building, {x + size, z}, 0);
    PlaceSelected(building, {x + size, z - size}, 0);
    fill_x = x;
    fill_z = bottom - span + 1;
  } else {
    u32 z = bottom - span + 1;
    PlaceSelected(building, {left, z}, 0);
    PlaceSelected(building, {left, z + size}, 0);
    PlaceSelected(building, {left + size, z + size}, 0);
    fill_x = left + size;
    fill_z = z;
  }
  EcIndustrialStripGroup *group = groups;
  while (group_index--)
    group = group->next;
  for (u32 x = fill_x; x < fill_x + size; ++x) {
    for (u32 z = fill_z; z < fill_z + size; ++z) {
      GeBuildingType *shore = group->shore.data[random.Bounded(group->shore.size)];
      PlaceSelected(shore, {x, z}, 0);
    }
  }
  return 1;
}

// Place two opposing industrial buildings and the weighted one-cell strip between them along a qualifying road edge.
u8 EcIndustrialStripRule::PlaceOne(const GeCellRect &rectangle, u32 group_index, GeBuildingType *building) {
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  i32 size = i32(building->SizeInCellsX());
  i32 width = i32(right - left + 1), height = i32(bottom - top + 1);
  if (width < size && height < size)
    return 0;
  if (width < 2 * size + 1 && height < 2 * size + 1)
    return 0;
  u32 start = random.Bounded(4), direction = start;
  do {
    ScPointXZ first, last;
    bool eligible = false;
    if (direction == 0 && width >= 2 * size + 1 && height >= size) {
      first = {left, top};
      last = {left + u32(2 * size), top};
      eligible = true;
    } else if (direction == 1 && height >= 2 * size + 1 && width >= size) {
      first = {right, top};
      last = {right, top + u32(2 * size)};
      eligible = true;
    } else if (direction == 2 && width >= 2 * size + 1 && height >= size) {
      first = {right, bottom};
      last = {right - u32(2 * size), bottom};
      eligible = true;
    } else if (direction == 3 && height >= 2 * size + 1 && width >= size) {
      first = {left, bottom};
      last = {left, bottom - u32(2 * size)};
      eligible = true;
    }
    if (eligible && zones->GetRoadCount(first, last) == u32(2 * size))
      break;
    direction = (direction + 1) & 3;
    if (direction == start)
      return 0;
  } while (true);
  EcIndustrialStripGroup *group = groups;
  while (group_index--)
    group = group->next;
  u32 extent = u32(size), x, z;
  if (direction == 0) {
    PlaceSelected(building, {left, top}, 0);
    x = left + extent;
    for (z = top; z < top + extent; ++z)
      PlaceSelected(group->shore.data[random.Bounded(group->shore.size)], {x, z}, 0);
    PlaceSelected(building, {x + 1, top}, 1);
  } else if (direction == 1) {
    x = right - extent + 1;
    PlaceSelected(building, {x, top}, 0);
    z = top + extent;
    for (u32 column = x; column <= right; ++column)
      PlaceSelected(group->shore.data[random.Bounded(group->shore.size)], {column, z}, 0);
    PlaceSelected(building, {x, z + 1}, 1);
  } else if (direction == 2) {
    x = right - 2 * extent;
    z = bottom - extent + 1;
    PlaceSelected(building, {x, z}, 0);
    x += extent;
    for (u32 row = z; row <= bottom; ++row)
      PlaceSelected(group->shore.data[random.Bounded(group->shore.size)], {x, row}, 0);
    PlaceSelected(building, {x + 1, z}, 1);
  } else {
    x = left;
    z = bottom - 2 * extent;
    PlaceSelected(building, {x, z}, 0);
    z += extent;
    for (u32 column = left; column < left + extent; ++column)
      PlaceSelected(group->shore.data[random.Bounded(group->shore.size)], {column, z}, 0);
    PlaceSelected(building, {left, z + 1}, 1);
  }
  return 1;
}

// Place three industrial lots and their weighted separating strips along the first qualifying road edge.
u8 EcIndustrialStripRule::PlaceThree(const GeCellRect &rectangle, u32 group_index, GeBuildingType *building) {
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  i32 size = i32(building->SizeInCellsX()), width = i32(right - left + 1), height = i32(bottom - top + 1);
  if ((width < size + 1 && height < size + 1) || (width < 3 * size && height < 3 * size))
    return 0;
  u32 span = u32(3 * size), extent = u32(size), start = random.Bounded(4), direction = start;
  do {
    ScPointXZ first, last;
    bool eligible = false;
    if (direction == 0 && width >= 3 * size && height >= size + 1) {
      first = {left, top};
      last = {left + span - 1, top};
      eligible = true;
    } else if (direction == 1 && height >= 3 * size && width >= size + 1) {
      first = {right, top};
      last = {right, top + span - 1};
      eligible = true;
    } else if (direction == 2 && width >= 3 * size && height >= size + 1) {
      first = {right, bottom};
      last = {right - span + 1, bottom};
      eligible = true;
    } else if (direction == 3 && height >= 3 * size && width >= size + 1) {
      first = {left, bottom};
      last = {left, bottom - span + 1};
      eligible = true;
    }
    if (eligible && zones->GetRoadCount(first, last) == span)
      break;
    direction = (direction + 1) & 3;
    if (direction == start)
      return 0;
  } while (true);
  EcIndustrialStripGroup *group = groups;
  while (group_index--)
    group = group->next;
  auto place_shore = [&](u32 x, u32 z) {
    PlaceSelected(group->shore.data[random.Bounded(group->shore.size)], {x, z}, 0);
  };
  if (direction == 0) {
    for (u32 x = left; x < left + extent; ++x)
      place_shore(x, top);
    for (u32 x = left + 2 * extent; x < left + span; ++x)
      place_shore(x, top);
    for (u32 x = left + extent; x < left + 2 * extent; ++x)
      place_shore(x, top + extent);
    PlaceSelected(building, {left, top + 1}, 0);
    PlaceSelected(building, {left + extent, top}, 1);
    PlaceSelected(building, {left + 2 * extent, top + 1}, 0);
  } else if (direction == 1) {
    u32 x = right - extent;
    PlaceSelected(building, {x, top}, 0);
    PlaceSelected(building, {x + 1, top + extent}, 1);
    PlaceSelected(building, {x, top + 2 * extent}, 0);
    for (u32 z = top; z < top + extent; ++z)
      place_shore(right, z);
    for (u32 z = top + 2 * extent; z < top + span; ++z)
      place_shore(right, z);
    for (u32 z = top + extent; z < top + 2 * extent; ++z)
      place_shore(x, z);
  } else if (direction == 2) {
    u32 x = right - span + 1, z = bottom - extent;
    for (u32 column = x; column <= right - 2 * extent; ++column)
      place_shore(column, z);
    for (u32 column = right - extent + 1; column <= right; ++column)
      place_shore(column, z);
    for (u32 column = right - 2 * extent + 1; column <= right - extent; ++column)
      place_shore(column, bottom);
    PlaceSelected(building, {x, z + 1}, 0);
    PlaceSelected(building, {x + extent, z}, 1);
    PlaceSelected(building, {x + 2 * extent, z + 1}, 0);
  } else {
    u32 z = bottom - span + 1;
    PlaceSelected(building, {left, z}, 0);
    PlaceSelected(building, {left + 1, z + extent}, 1);
    PlaceSelected(building, {left, z + 2 * extent}, 0);
    for (u32 row = z; row <= bottom - 2 * extent; ++row)
      place_shore(left + extent, row);
    for (u32 row = bottom - extent + 1; row <= bottom; ++row)
      place_shore(left + extent, row);
    for (u32 row = bottom - 2 * extent + 1; row <= bottom - extent; ++row)
      place_shore(left, row);
  }
  return 1;
}

// The original group owns fifteen borrowed-building vectors and twelve orientation-fallback flags.
struct EcLargeLotGroup {
  EcLargeLotGroup *next;
  u32 identifier;
  ScArray<GeBuildingType *> lists[15];
  u8 copied[12];
};

// Copy missing opposite-direction building lists and record which directions require an adjusted rotation.
static u8 EcNormalizeLargeLotGroup(EcLargeLotGroup *group) {
  static const u8 pairs[6][4] = {
      {0, 1, 0, 1}, {2, 3, 2, 3}, {4, 7, 4, 7}, {8, 11, 8, 11}, {6, 5, 5, 6}, {10, 9, 9, 10}};
  for (u32 index = 0; index < 6; ++index) {
    ScArray<GeBuildingType *> *first = &group->lists[pairs[index][0]];
    ScArray<GeBuildingType *> *second = &group->lists[pairs[index][1]];
    group->copied[pairs[index][2]] = 0;
    group->copied[pairs[index][3]] = 0;
    if (!first->size && !second->size)
      return 0;
    if (!first->size) {
      for (u32 entry = 0; entry < second->size; ++entry)
        *first->Append() = second->data[entry];
      group->copied[pairs[index][2]] = 1;
    }
    if (!second->size) {
      for (u32 entry = 0; entry < first->size; ++entry)
        *second->Append() = first->data[entry];
      group->copied[pairs[index][3]] = 1;
    }
  }
  return group->lists[12].size && group->lists[13].size && group->lists[14].size;
}

EcLargeLotRule::EcLargeLotRule() : groups(nullptr), occupants(nullptr) {
}

// Destroy all owned pointer arrays while leaving their building descriptions borrowed.
EcLargeLotRule::~EcLargeLotRule() {
  while (groups) {
    EcLargeLotGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Is shared with the industrial strip rule.
// Borrow building, surface-occupant and zoning services in their original order.
bool EcLargeLotRule::Init(CtCity *city) {
  buildings = city->BuildingLayer();
  if (!buildings)
    return false;
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return false;
  zones = city->ZoneLayer();
  return zones != nullptr;
}

// Clear borrowed building, zone and occupant pointers in the installed order.
u8 EcLargeLotRule::Shutdown() {
  buildings = nullptr;
  zones = nullptr;
  occupants = nullptr;
  return 1;
}

// Classify each building once into the original directional, filler and large-lot vectors.
void EcLargeLotRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcLargeLotGroup *group = new EcLargeLotGroup{};
    group->identifier = source->GetLandUse();
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      if ((flags & 0xf) && size == 1) {
        for (u32 bit = 0; bit < 4; ++bit)
          if (flags & (1u << bit))
            *group->lists[bit].Append() = building;
      } else if (flags & 0x100) {
        *group->lists[(flags & 0x400) ? 13 : 14].Append() = building;
      } else if (flags & 0xf0) {
        if (size == 1) {
          if (flags & 0x20)
            *group->lists[(flags & 0x800) ? 4 : 8].Append() = building;
          if (flags & 0x80)
            *group->lists[(flags & 0x400) ? 5 : 9].Append() = building;
          if (flags & 0x10)
            *group->lists[(flags & 0x400) ? 6 : 10].Append() = building;
          if (flags & 0x40)
            *group->lists[(flags & 0x800) ? 7 : 11].Append() = building;
        }
      } else if (size <= 3) {
        *group->lists[12].Append() = building;
      }
    }
    if (EcNormalizeLargeLotGroup(group)) {
      EcLargeLotGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

void EcLargeLotRule::PlaceSelected(GeBuildingType *building, const ScPointXZ &point, u8 reverse) {
  RsKey key = *building->Key();
  zones->PlaceBuilding(key, point, 4, reverse ? 180 : 0, true);
}

// Place a large central lot, directional road-edge pieces and weighted infill in the original callback order.
u8 EcLargeLotRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  i32 scaled = i32(u32(demand) * 10u);
  assert(maximum && (scaled != INT32_MIN || maximum != -1));
  if (scaled / maximum < 1)
    return 0;
  EcLargeLotGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  i32 width = i32(right - left + 1), height = i32(bottom - top + 1);
  if (width < 4 || height < 4)
    return 0;
  if (width > 6) {
    width = 6;
    right = left + 5;
  }
  if (height > 6) {
    height = 6;
    bottom = top + 5;
  }
  auto choose = [&](u32 list) {
    ScArray<GeBuildingType *> *choices = &group->lists[list];
    return choices->data[random.Bounded(choices->size)];
  };
  GeBuildingType *central = choose(12);
  u32 size = central->SizeInCellsX();
  ScPointXZ point{
      random.Bounded(u32(width - i32(size) - 1)) + left + 1, random.Bounded(u32(height - i32(size) - 1)) + top + 1};
  PlaceSelected(central, point, 0);
  u32 inner_left = point.x, inner_top = point.z, inner_right = point.x + size - 1, inner_bottom = point.z + size - 1;
  const ScPointXZ corners[4] = {{left, top}, {right, bottom}, {left, bottom}, {right, top}};
  for (u32 index = 0; index < 4; ++index) {
    point = corners[index];
    if (zones->IsNearTransport(point, max_distance))
      PlaceSelected(choose(index), point, group->copied[index]);
  }
  const u8 ordinary[4] = {4, 6, 5, 7}, alternate[4] = {8, 10, 9, 11};
  const u8 normal_rotation[4] = {4, 5, 6, 7}, alternate_rotation[4] = {8, 9, 10, 11};
  for (u32 edge = 0; edge < 4; ++edge) {
    bool used_alternate = false;
    if (edge == 0)
      point = {left, top + 1};
    else if (edge == 1)
      point = {left + 1, top};
    else if (edge == 2)
      point = {left + 1, bottom};
    else
      point = {right, top + 1};
    do {
      bool use_alternate = !used_alternate && random.Bounded(100) < 20;
      u32 list = use_alternate ? alternate[edge] : ordinary[edge];
      u8 rotation = group->copied[use_alternate ? alternate_rotation[edge] : normal_rotation[edge]];
      PlaceSelected(choose(list), point, rotation);
      if (use_alternate)
        used_alternate = true;
      if (edge == 0 || edge == 3)
        ++point.z;
      else
        ++point.x;
    } while ((edge == 0 || edge == 3) ? point.z < bottom : point.x < right);
  }
  for (u32 x = left + 1; x < right; ++x) {
    for (u32 z = top + 1; z < bottom; ++z) {
      if (x >= inner_left && x <= inner_right && z >= inner_top && z <= inner_bottom)
        continue;
      u32 list = random.Bounded(100) < 5 ? 14 : 13;
      GeBuildingType *building = choose(list);
      RsKey key = *building->Key();
      point = {x, z};
      zones->PlaceBuilding(key, point, 1, 0, true);
      x = point.x;
      z = point.z;
    }
  }
  return 1;
}

// Place corner lots, adjacent strips and the remaining cells using the caller's rule-specific tuning.
u8 EcPlaceCornerLots(EcLotGroup *groups, EcRci::ScRandom *random, CtZoneLayer *zones, u32 minimum, u32 maximum_size,
    u32 threshold, u32 shore_chance, const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  assert(maximum != 0);
  if (i32(100u * u32(demand)) / maximum < i32(threshold))
    return 0;
  EcLotGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  GeCellRect area = rectangle;
  i32 width = i32(area.right - area.left + 1u), height = i32(area.bottom - area.top + 1u);
  if (width < i32(minimum) || height < i32(minimum))
    return 0;
  if (width > i32(maximum_size)) {
    ScPointXZ left_bottom{area.left, area.bottom}, left_top{area.left, area.top};
    i32 left_roads = i32(zones->GetRoadCount(left_bottom, left_top));
    ScPointXZ right_top{area.right, area.top}, right_bottom{area.right, area.bottom};
    i32 right_roads = i32(zones->GetRoadCount(right_top, right_bottom));
    if (left_roads <= right_roads)
      area.left = area.right - maximum_size + 1u;
    else
      area.right = area.left + maximum_size - 1u;
    width = i32(maximum_size);
  }
  if (height > i32(maximum_size)) {
    ScPointXZ left_top{area.left, area.top}, right_top{area.right, area.top};
    i32 top_roads = i32(zones->GetRoadCount(left_top, right_top));
    ScPointXZ right_bottom{area.right, area.bottom}, left_bottom{area.left, area.bottom};
    i32 bottom_roads = i32(zones->GetRoadCount(right_bottom, left_bottom));
    if (top_roads <= bottom_roads)
      area.top = area.bottom - maximum_size + 1u;
    else
      area.bottom = area.top + maximum_size - 1u;
    height = i32(maximum_size);
  }
  ScPointXZ corners[4] = {
      {area.left, area.top}, {area.right, area.top}, {area.right, area.bottom}, {area.left, area.bottom}};
  u32 roads[4];
  for (u32 edge = 0; edge < 4; ++edge)
    roads[edge] = zones->GetRoadCount(corners[edge], corners[(edge + 1) & 3]);
  u8 used[100]{};
  if (group->large.size) {
    for (u32 corner = 0; corner < 4; ++corner) {
      u32 previous = (corner + 3) & 3;
      i32 previous_length = corner == 0 || corner == 2 ? height : width;
      i32 next_length = corner == 0 || corner == 2 ? width : height;
      if (roads[previous] != u32(previous_length) || roads[corner] != u32(next_length))
        continue;
      GeBuildingType *building = group->large.data[random->Bounded(group->large.size)];
      i32 size = i32(building->SizeInCellsX());
      ScPointXZ point;
      if ((corner == 1 && size > width) || (corner == 2 && (size > width || size > height)) ||
          (corner == 3 && size > height))
        continue;
      point.x = corner == 1 || corner == 2 ? area.right - u32(size) + 1u : area.left;
      point.z = corner >= 2 ? area.bottom - u32(size) + 1u : area.top;
      GeCellRect strip{};
      bool has_strip = false;
      u32 choice = random->Bounded(100);
      if (choice < 40 && height > size) {
        strip.left = point.x;
        strip.right = point.x + u32(size) - 1u;
        strip.top = corner >= 2 ? point.z - 1u : point.z + u32(size);
        strip.bottom = strip.top;
        has_strip = true;
      } else if (choice >= 40 && choice < 80 && width > size) {
        strip.left = corner == 1 || corner == 2 ? point.x - 1u : point.x + u32(size);
        strip.right = strip.left;
        strip.top = point.z;
        strip.bottom = point.z + u32(size) - 1u;
        has_strip = true;
      }
      if (point.x + u32(size) - 1u > area.right || point.z + u32(size) - 1u > area.bottom)
        continue;
      if (!zones->PlaceBuilding(building, point, 4, 0, true))
        continue;
      for (i32 x = 0; x < size; ++x)
        for (i32 z = 0; z < size; ++z)
          used[10u * (point.x - area.left + u32(x)) + point.z - area.top + u32(z)] = 1;
      if (!has_strip)
        continue;
      building = group->shoreline.data[random->Bounded(group->shoreline.size)];
      for (u32 x = strip.left; x <= strip.right; ++x) {
        for (u32 z = strip.top; z <= strip.bottom; ++z) {
          u32 offset_x = x - area.left, offset_z = z - area.top;
          if (used[10u * offset_x + offset_z])
            continue;
          u32 strip_size = building->SizeInCellsX();
          ScPointXZ strip_point{x, z};
          if (strip_size + offset_x - 1u < u32(width) && strip_size + offset_z - 1u < u32(height) &&
              zones->PlaceBuilding(building, strip_point, 4, 0, true) && strip_size) {
            for (u32 row = 0; row < strip_size; ++row)
              for (u32 column = 0; column < strip_size; ++column)
                used[10u * (offset_x + row) + offset_z + column] = 1;
          }
        }
      }
    }
  }
  for (u32 x = area.left; x <= area.right; ++x) {
    for (u32 z = area.top; z <= area.bottom; ++z) {
      u32 offset_x = x - area.left, offset_z = z - area.top;
      if (used[10u * offset_x + offset_z])
        continue;
      GeBuildingType *building;
      if (random->Bounded(100) >= shore_chance)
        building = group->shoreline.data[random->Bounded(group->shoreline.size)];
      else
        building = group->small.data[random->Bounded(group->small.size)];
      u32 size = building->SizeInCellsX();
      ScPointXZ point{x, z};
      if (size + offset_x - 1u < u32(width) && size + offset_z - 1u < u32(height) &&
          zones->PlaceBuilding(building, point, 4, 0, true) && size) {
        for (u32 row = 0; row < size; ++row)
          for (u32 column = 0; column < size; ++column)
            used[10u * (offset_x + row) + offset_z + column] = 1;
      }
    }
  }
  return 1;
}

// Check every directional fallback in the original order before testing the general-building list.
u8 EcPortPieceGroupValid(EcPortPieceGroup *group) {
  GePortType **piece = group->pieces;
  u8 valid = 1;
  if (!piece[1] && (!piece[0] || !(piece[0]->Flags() & 0x200)))
    valid = 0;
  if (!piece[0] && (!piece[1] || !(piece[1]->Flags() & 0x200)))
    valid = 0;
  if (!piece[3] && (!piece[2] || !(piece[2]->Flags() & 0x200)))
    valid = 0;
  if (!piece[4] && (!piece[5] || !(piece[5]->Flags() & 0x200)))
    valid = 0;
  if (!piece[2] && (!piece[3] || !(piece[3]->Flags() & 0x200)))
    valid = 0;
  if (!piece[5] && (!piece[4] || !(piece[4]->Flags() & 0x200)))
    valid = 0;
  if (!piece[7] && (!piece[6] || !(piece[6]->Flags() & 0x200)))
    valid = 0;
  if (!piece[6] && (!piece[7] || !(piece[7]->Flags() & 0x200)))
    valid = 0;
  if (!piece[9] && (!piece[8] || !(piece[8]->Flags() & 0x200)))
    valid = 0;
  if (!piece[8] && (!piece[9] || !(piece[9]->Flags() & 0x200)))
    valid = 0;
  return group->buildings.size ? valid : 0;
}
