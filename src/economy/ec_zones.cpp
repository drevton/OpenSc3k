#include "economy/ec_zones.h"

#include <assert.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/maps.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_text.h"
#include "economy/ec_sector.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "graphics/api.h"
#include "network/nw_manager.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "window/api.h"

// Zone queries dispatch distinct rows for the zone name and its development-failure details.
enum EcZoneQueryCode : u32 {
  kEcZoneNameQueryCode = 0x45e5f4d5,
  kEcZoneDevelopmentFailureQueryCode = 0x45e5f4d6,
};

// Create the requested zone tool with its installed cursor, zone type, color and feedback sound.
bool EcZoneCommandFactory::CreateCommand(u32 class_id, u32 iid, void **out) {
  u32 cursor;
  i32 zone;
  u16 color, sound = 38;
  switch (class_id) {
  case kEcZoneResidentialHighCommandClassId:
    cursor = kSpZoneResidentialHighCursorId;
    zone = 3;
    color = 24;
    break;
  case kEcZoneCommercialMediumCommandClassId:
    cursor = kSpZoneCommercialMediumCursorId;
    zone = 6;
    color = 28;
    break;
  case kEcZoneIndustrialMediumCommandClassId:
    cursor = kSpZoneIndustrialMediumCursorId;
    zone = 10;
    color = 22;
    break;
  case kEcZoneIndustrialLowCommandClassId:
    cursor = kSpZoneIndustrialLowCursorId;
    zone = 9;
    color = 23;
    break;
  case kEcZoneLandfillCommandClassId:
    cursor = kSpZoneLandfillCursorId;
    zone = 17;
    color = 32;
    break;
  case kEcZoneAirportCommandClassId:
    cursor = kSpZoneAirportsCursorId;
    zone = 14;
    color = 30;
    break;
  case kEcDezoneCommandClassId:
    cursor = kSpDezoneCursorId;
    zone = 0;
    color = 35;
    sound = 44;
    break;
  case kEcZoneResidentialMediumCommandClassId:
    cursor = kSpZoneResidentialMediumCursorId;
    zone = 2;
    color = 25;
    break;
  case kEcZoneCommercialLowCommandClassId:
    cursor = kSpZoneCommercialLowCursorId;
    zone = 5;
    color = 29;
    break;
  case kEcZoneCommercialHighCommandClassId:
    cursor = kSpZoneCommercialHighCursorId;
    zone = 7;
    color = 27;
    break;
  case kEcZoneIndustrialHighCommandClassId:
    cursor = kSpZoneIndustrialHighCursorId;
    zone = 11;
    color = 21;
    break;
  case kEcZoneSeaportCommandClassId:
    cursor = kSpZoneSeaportsCursorId;
    zone = 15;
    color = 31;
    break;
  case kEcZoneResidentialLowCommandClassId:
    cursor = kSpZoneResidentialLowCursorId;
    zone = 1;
    color = 26;
    break;
  default:
    *out = nullptr;
    return false;
  }
  EcZoneTool *tool = new EcZoneTool(cursor, zone, color, sound);
  if (tool->QueryInterface(iid, out))
    return true;
  delete tool;
  *out = nullptr;
  return false;
}

// Create the original zoning command factory and expose its primary factory identity.
ScUnknown *EcCreateRciClass10036CFC() {
  return static_cast<ScCommandFactory *>(new EcZoneCommandFactory);
}

EcZoneCursor::EcZoneCursor()
    : phase(1), mode(1), refill(1), min_x(0), min_z(0), max_x(0), max_z(0), count(0), used(0), current(0), stride(1) {
}

EcZoneCursor::~EcZoneCursor() {
}

// Advance the configured arithmetic or packed-cell traversal, leaving output coordinates untouched on exhaustion.
u8 EcZoneCursor::Next(i32 *x, i32 *z) {
  if (phase == 2)
    return 0;
  if (mode) {
    used = i32(u32(used) + 1u);
    if (used <= count) {
      i32 width = i32(u32(max_x) - u32(min_x) + 1u);
      assert(count != 0 && width != 0);
      current = i32(u32(current) + u32(stride)) % count;
      *x = i32(u32(min_x) + u32(current % width));
      *z = i32(u32(min_z) + u32(current / width));
      if (used == count)
        phase = 2;
      return 1;
    }
  } else {
    i32 index = used, length = i32(positions.size);
    if (index < length) {
      assert(index >= 0);
      used = index + 1;
      u16 packed = positions.data[index];
      *x = packed >> 8;
      *z = packed & 255;
      if (used == length)
        phase = 2;
      return 1;
    }
  }
  phase = 2;
  return 0;
}

// Rebuild or shuffle the packed traversal, or choose the original coprime stride and random starting cell.
void EcZoneCursor::Reset(u8 shuffle) {
  static const i16 kStrides[] = {1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83,
      89, 97, 101, 103, 107, 109, 113, 179, 211, 257, 307, 373, 479, 577, 641, 719, 857, 977, 1063, 1237, 1471, 1663,
      1993, 2221, 2539, 2909, 3259, 3733, 4507, 5281, 5861, 6481, 6947, 7573, 9013, 10103, 12227, 15683, 17431, 19801,
      21569, 23633, 26003, 27529, 29683, 32051, 32611};
  phase = 1;
  used = 0;
  count = i32((u32(max_x) - u32(min_x) + 1) * (u32(max_z) - u32(min_z) + 1));
  if (!mode) {
    if (positions.size != u32(count)) {
      positions.Reserve(u32(count));
      positions.size = u32(count);
      refill = 1;
    }
    if (refill) {
      u32 index = 0;
      for (i32 x = min_x; x <= max_x; ++x)
        for (i32 z = min_z; z <= max_z; ++z)
          positions.data[index++] = u16((u16(u8(x)) << 8) | u8(z));
    }
    if (shuffle) {
      for (u32 index = 1; index < positions.size; ++index) {
        u32 chosen = u32(ScRand()) % (index + 1);
        u16 value = positions.data[index];
        positions.data[index] = positions.data[chosen];
        positions.data[chosen] = value;
      }
    }
    return;
  }
  u32 index = count < 29 ? 0
      : count < 71       ? 10
      : count < 113      ? 20
      : count < 857      ? 30
      : count < 3259     ? 40
      : count < 12227    ? 50
                         : 60;
  if (count >= kScPermutationMaximumCount)
    count = kScPermutationMaximumCount;
  while (kStrides[index] < count / 4) {
    ++index;
    assert(index < sizeof(kStrides) / sizeof(*kStrides));
  }
  while (count % kStrides[index] == 0) {
    ++index;
    assert(index < sizeof(kStrides) / sizeof(*kStrides));
  }
  stride = kStrides[index];
  current = ScRand() % count;
}

// Restore cursor scalars and descending packed words into the already allocated original sequence.
u8 EcZoneCursorRecord::SerialRead(CtCity *, RsStream *record) {
  u32 stored = 1;
  u8 result = record->ReadI8(&phase);
  if (result)
    result = record->ReadU32(&stored);
  if (!result)
    return result;
  mode = i32(stored);
  u8 enabled;
  result = record->ReadU8(&enabled);
  if (!result)
    return result;
  refill = enabled != 0;
  result = record->ReadI32(&min_x);
  result = result && record->ReadI32(&max_x) && record->ReadI32(&min_z) && record->ReadI32(&max_z) &&
      record->ReadI32(&count) && record->ReadU32(&stored);
  if (!result)
    return result;
  while (result) {
    u32 remaining = stored--;
    if (!remaining)
      break;
    if (stored < positions.capacity) {
      result = record->ReadU16(positions.data + stored);
    } else {
      // Stock permits a callback to leave this out-of-range output untouched; exclude any invalid write.
      u16 excluded = 0;
      result = record->ReadU16(&excluded);
    }
  }
  result = result && record->ReadI32(&used) && record->ReadI32(&current) && record->ReadI32(&stride);
  return result;
}

// Write cursor scalars and the captured packed sequence length in the installed descending element order.
u8 EcZoneCursorRecord::SerialWrite(CtCity *, RsStream *record) {
  u8 result = record->WriteI8(phase);
  result = result && record->WriteU32(u32(mode)) && record->WriteU8(refill != 0) && record->WriteI32(min_x) &&
      record->WriteI32(max_x) && record->WriteI32(min_z) && record->WriteI32(max_z) && record->WriteI32(count);
  u32 length = positions.size;
  if (result)
    result = record->WriteU32(length);
  while (result) {
    u32 remaining = length--;
    if (!remaining)
      break;
    result = record->WriteU16(positions.data[length]);
  }
  result = result && record->WriteI32(used) && record->WriteI32(current) && record->WriteI32(stride);
  return result;
}

// Create the configured developer, initialize it with the complete row, and register its low-byte zone identity.
static void EcParseZoneDeveloper(const ScString &key, const ScString &value, void *context) {
  CtCity *city = static_cast<CtCity *>(context);
  u8 zone = u8(EcParseTuningNumber(key.Data()));
  ScString remaining(value), field = remaining.SplitToken(",");
  u32 class_id = EcParseTuningNumber(field.Data());
  CtZoneDeveloper *developer = nullptr;
  if (EcGetRciCom()->GetClassObject(class_id, IID_CtZoneDeveloper, reinterpret_cast<void **>(&developer))) {
    developer->Init(city, value.Data());
    city->ZoneLayer()->RegisterZoneDeveloper(zone, developer, class_id);
    developer->Release();
  }
}

// Load each configured building group into a newly created rule, then attach a nonempty rule to its zone developer.
void EcParseZoneRules(const ScString &key, const ScString &value, void *context) {
  ScString name(key), remaining(value);
  EcZoneLayer *zone_layer = static_cast<EcZoneLayer *>(context);
  if (!zone_layer)
    return;
  RsManager *resources = EcGetRciResourceManager();
  ScString field = name.SplitToken(",");
  u32 zone = EcParseTuningNumber(field.Data());
  u32 class_id = EcParseTuningNumber(name.Data());
  assert(zone < sizeof(zone_layer->developers) / sizeof(*zone_layer->developers));
  if (!zone_layer->developers[zone].developer)
    return;
  EcZoneRule *rule = nullptr;
  if (!EcGetRciCom()->GetClassObject(class_id, IID_EcZoneRule, reinterpret_cast<void **>(&rule)))
    return;
  if (rule->Init(zone_layer->city)) {
    u32 loaded = 0;
    bool done = false;
    do {
      field = remaining.SplitToken(",");
      if (!field.Strlen())
        break;
      u32 type = EcParseTuningNumber(field.Data());
      field = remaining.SplitToken(",");
      if (!field.Strlen())
        break;
      u32 group = EcParseTuningNumber(field.Data());
      field = remaining.SplitToken(";");
      if (!field.Strlen()) {
        field = remaining;
        done = true;
      }
      u32 instance = EcParseTuningNumber(field.Data());
      RsKey resource_key{type, group, instance};
      GeBuildingGroupView *building_group = nullptr;
      if (resources->GetResource(resource_key, IID_GeBuildingGroupView, reinterpret_cast<void **>(&building_group))) {
        rule->AddConfig(building_group);
        building_group->Release();
        ++loaded;
      }
    } while (!done);
    if (loaded)
      zone_layer->AddDeveloperRule(u8(zone), rule);
  }
  rule->Release();
}

u8 EcZoneLayer::AddDeveloperRule(u8 zone, EcZoneRule *rule) {
  CtZoneDeveloper *developer = developers[zone].developer;
  if (!developer)
    return 0;
  developer->AddRule(rule);
  return 1;
}

// Enumerate the original packaged developer descriptions and rule groups, optionally binding their demand valves.
void EcZoneLayer::ReadZoneDeveloperDescriptions(bool bind_demand) {
  ScString path;
  EcIni ini;
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3ZoneLayer.INI");
  ini.SelectFile(&path);
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(path);
  ScString descriptions("ZoneDeveloperDescriptions");
  ini.EnumerateSection(&descriptions, EcParseZoneDeveloper, city);
  ScString rules_section("ZoneDeveloperRules");
  ini.EnumerateSection(&rules_section, EcParseZoneRules, this);
  if (bind_demand) {
    for (u32 zone = 0; zone < 23; ++zone) {
      CtZoneDeveloper *developer = developers[zone].developer;
      if (developer && !developer->BindDemand())
        break;
    }
  }
}

EcZoneFilter::EcZoneFilter() : references(0) {
}

EcZoneFilter::~EcZoneFilter() {
}

bool EcZoneFilter::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, OcIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcZoneFilter::AddRef() {
  return ++references;
}

u32 EcZoneFilter::Release() {
  return ScReleaseImpl(this, &references);
}

// Accept road geometry and the four special transit occupant types used by zone proximity.
bool EcZoneTransportFilter::Test(ScOccupant *occupant) {
  if (!occupant)
    return false;
  u32 type = occupant->TypeId();
  return occupant->IsGeomFlag(kOcGeomRoad) || type == kCtAgentBusStation || type == kCtAgentRailStation ||
      type == kCtAgentSubwayRailStation || type == kCtAgentSubwayStation;
}

bool EcZoneRoadFilter::Test(ScOccupant *occupant) {
  return occupant && occupant->IsGeomFlag(kOcGeomRoad);
}

static ScString EcZoneCheatName("nerdz rool");

// Acquire the city's real zone services, initialize both cell maps and prepare the original randomized traversal.
// The clamped bounds contain at least one cell, so the first cursor step supplies both coordinates.
bool EcZoneLayer::Init(CtCity *owner) {
  if (layer_initialized)
    Shutdown();
  if (!owner) {
    Shutdown();
    return false;
  }
  city = ScAddRefNotNull(owner);
  city_width = city->CellCountX();
  city_height = city->CellCountZ();
  transport_test = ScAddRefNotNull(new EcZoneTransportFilter);
  road_test = ScAddRefNotNull(new EcZoneRoadFilter);
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    Shutdown();
    return false;
  }
  occupants->AddRef();
  terrain = city->DirtBag();
  if (!terrain) {
    Shutdown();
    return false;
  }
  terrain->AddRef();
  building_layer = city->BuildingLayer();
  if (!building_layer) {
    Shutdown();
    return false;
  }
  building_layer->AddRef();
  power = city->PowerLayer();
  if (!power) {
    Shutdown();
    return false;
  }
  power->AddRef();
  land_value = city->LandValueLayer();
  if (!land_value) {
    Shutdown();
    return false;
  }
  land_value->AddRef();
  demolition = city->DemolitionLayer();
  if (!demolition) {
    Shutdown();
    return false;
  }
  demolition->AddRef();
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer))) {
    Shutdown();
    return false;
  }
  u8 zero = 0;
  if (!EcByteMap::Init(city, &zero)) {
    Shutdown();
    return false;
  }
  failure_map.Init(city, &zero);
  u32 height = CellCountZ(), width = CellCountX();
  cells_per_step = height * width / 30;
  if (cursor.mode) {
    cursor.mode = 0;
    cursor.refill = 1;
  }
  height = CellCountZ();
  width = CellCountX();
  cursor.min_x = cursor.min_z = 0;
  cursor.max_x = i32(width - 1);
  cursor.max_z = i32(height - 1);
  if (cursor.max_x < cursor.min_x)
    cursor.max_x = cursor.min_x;
  if (cursor.max_z < cursor.min_z)
    cursor.max_z = cursor.min_z;
  cursor.refill = 1;
  cursor.Reset(1);
  i32 x, z;
  cursor.Next(&x, &z);
  next_x = u32(x);
  next_z = u32(z);
  while (demand) {
    EcZoneDemand *entry = demand;
    demand = entry->next;
    delete entry;
  }
  EcZoneFailure **maps[] = {&failures, &current_failures};
  for (u32 index = 0; index < 2; ++index) {
    while (*maps[index]) {
      EcZoneFailure *entry = *maps[index];
      *maps[index] = entry->next;
      delete entry;
    }
  }
  for (u32 index = 0; index < 23; ++index) {
    zone_counts[index] = 0;
    undeveloped_counts[index] = 0;
    current_undeveloped_counts[index] = 0;
  }
  total_undeveloped = abandoned = current_undeveloped = current_abandoned = 0;
  ReadZoneDeveloperDescriptions(false);
  EcGetRciApplication()->GetCheatCodeManager()->RegisterCheatCode(kEcCheat_NerdsRule, EcZoneCheatName);
  nerds_rule = false;
  layer_initialized = true;
  return true;
}

// The static definition cache is retained by the original StaticInit and released by StaticShutdown.
OcAttribCache *EcZoneLayer::attributes;

// Prepare terrain, remove replaceable occupants and set each eligible zone cell in the installed traversal order.
// Collapse removed power networks through the real transit layer, then charge the cost after unlocking terrain.
bool EcZoneLayer::PlaceZone(i32 zone, const GeBounds &area, i32 *cost, bool) {
  EcBudgetLayer *budget = city->BudgetLayer();
  ScDirtBag *dirt = city->DirtBag();
  ScOccupantManager *surface = city->SurfaceOccupantManager();
  ScOccupantManager *underground_one = city->UndergroundOccupantManagerLevelOne();
  ScOccupantManager *underground_two = city->UndergroundOccupantManagerLevelTwo();
  bool valid = true;
  *cost = 0;
  u32 changed = 0, demolition_cost = 0;
  CtCity *global_city = EcGetRciCity();
  NwManager *transit = reinterpret_cast<NwManager *>(global_city->TransitLayer());
  CtNeighborsLayer *neighbors = global_city->NeighborsLayer();
  i32 demolition_price = budget->GetCost(kEcCostDemolish);
  GeBounds rectangle(area);
  rectangle.NormalizeXZ();
  rectangle.maximum.x = i32(u32(rectangle.maximum.x) + kCtMicroUnitsPerCell);
  rectangle.maximum.z = i32(u32(rectangle.maximum.z) + kCtMicroUnitsPerCell);
  if (!CanZone(zone, area, cost))
    return false;
  u32 first_x = u32(rectangle.minimum.x) >> kCtMicroCoordinateShift,
      first_z = u32(rectangle.minimum.z) >> kCtMicroCoordinateShift;
  u32 end_x = u32(rectangle.maximum.x) >> kCtMicroCoordinateShift,
      end_z = u32(rectangle.maximum.z) >> kCtMicroCoordinateShift;
  for (u32 x = first_x; x < end_x && valid; ++x) {
    for (u32 z = first_z; z < end_z && valid; ++z) {
      ScOccupant *occupant = nullptr;
      if (surface->GetOccupantAtCell(x, z, &occupant)) {
        if (!occupant->CanUserRemove())
          valid = false;
        if (valid && !occupant->IsGeomFlag(1024) &&
            (!occupant->IsGeomFlag(kOcGeomPower) || !occupant->IsGeomFlag(kOcGeomNetwork)))
          valid = false;
        occupant->Release();
      }
      if (valid && underground_one->GetOccupantAtCell(x, z, &occupant)) {
        occupant->Release();
        valid = false;
      }
      if (valid && underground_two->GetOccupantAtCell(x, z, &occupant)) {
        occupant->Release();
        valid = false;
      }
    }
  }
  if (zone) {
    u32 terrain_cost;
    dirt->SetupZone(rectangle, &terrain_cost, true, valid);
    dirt->LockUpdates(true);
  }
  for (u32 x = first_x; x < end_x; ++x) {
    for (u32 z = first_z; z < end_z; ++z) {
      bool eligible = true;
      if (zone) {
        u8 northwest, northeast, southeast, southwest;
        dirt->GetVertexAltitudeDirt(x, z, &northwest);
        dirt->GetVertexAltitudeDirt(x + 1, z, &northeast);
        dirt->GetVertexAltitudeDirt(x + 1, z + 1, &southeast);
        dirt->GetVertexAltitudeDirt(x, z + 1, &southwest);
        if (northwest != northeast || northwest != southeast || northwest != southwest) {
          u8 empty = 0;
          SetValue(x, z, &empty);
          eligible = false;
        }
        ScOccupant *occupant = nullptr;
        if (terrain->ChangedByLastOperation(x, z) && surface->GetOccupantAtCell(x, z, &occupant)) {
          if (occupant->IsGeomFlag(1024) ||
              (occupant->IsGeomFlag(kOcGeomPower) && occupant->IsGeomFlag(kOcGeomNetwork))) {
            surface->RemoveOccupantAtCell(x, z);
            demolition_cost += u32(demolition_price);
          }
          occupant->Release();
        }
      }
      u8 previous;
      GetValue(x, z, &previous);
      if (i32(previous) == zone)
        eligible = false;
      if (eligible && dirt->IsWater(x, z))
        eligible = false;
      if (eligible) {
        ScOccupant *occupant = nullptr;
        if (surface->GetOccupantAtCell(x, z, &occupant)) {
          if (!occupant->CanUserRemove())
            eligible = false;
          if (eligible && !occupant->IsGeomFlag(1024) &&
              (!occupant->IsGeomFlag(kOcGeomPower) || !occupant->IsGeomFlag(kOcGeomNetwork)))
            eligible = false;
          if (eligible && occupant->IsGeomFlag(kOcGeomPower) && occupant->IsGeomFlag(kOcGeomNetwork) &&
              !occupant->IsGeomFlag(kOcGeomRoad) && !occupant->IsGeomFlag(kOcGeomHighway) &&
              !occupant->IsGeomFlag(kOcGeomRail)) {
            if (neighbors->HasConnection(3, i32(x), i32(z))) {
              eligible = false;
            } else {
              surface->RemoveOccupantAtCell(x, z);
              demolition_cost += u32(demolition_price);
              GeAttrib *occupant_attributes = occupant->GetAttribData();
              CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
              GeBounds collapsed;
              occupant->GetLocation(&position);
              u16 piece = u16(occupant_attributes->Key()->instance);
              u8 rotation;
              occupant->GetSpriteInst()->GetRotationOffset(&rotation);
              transit->CollapseSupport(surface, position, piece, rotation, &collapsed, false);
            }
          }
          occupant->Release();
        }
        if (eligible) {
          u8 next = u8(zone);
          SetValue(x, z, &next);
          ++changed;
        }
      }
    }
  }
  if (zone)
    dirt->LockUpdates(false);
  i32 category = -1;
  switch (zone) {
  case 0:
    category = 6;
    break;
  case 1:
  case 5:
  case 9:
    category = 0;
    break;
  case 2:
  case 6:
  case 10:
    category = 1;
    break;
  case 3:
  case 7:
  case 11:
    category = 2;
    break;
  case 14:
    category = 4;
    break;
  case 15:
    category = 5;
    break;
  case 17:
    category = 3;
    break;
  }
  i32 unit_cost = category < 0 ? 0 : budget->GetCost(category);
  *cost = i32(demolition_cost + changed * u32(unit_cost));
  budget->WithdrawFunds(u32(*cost));
  return true;
}

// Restore the zoning map, traversal and counters, then load matching registered developer records.
bool EcZoneLayer::Init(CtCity *owner, RsDatabaseSegment *segment) {
  RsKey key{kRsCityLayerRecordType, kEcZoneLayerClassId, 0};
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (!segment->OpenRecord(key, &record))
    return false;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    segment->CloseRecord(&record);
    return false;
  }
  SerialRead(stream);
  u32 count = 0, identifier = 0, value = 0;
  bool good = stream->ReadU32(&count);
  while (good && count--) {
    good = stream->ReadU32(&identifier);
    if (good)
      good = stream->ReadU32(&value);
    if (good) {
      EcZoneDemand **slot = &demand;
      while (*slot && (*slot)->identifier < identifier)
        slot = &(*slot)->next;
      if (!*slot || (*slot)->identifier != identifier)
        *slot = new EcZoneDemand{*slot, identifier, 0};
      (*slot)->value = value;
    }
  }
  good = good && stream->ReadU32Array(current_undeveloped_counts, 23) && stream->ReadU32Array(undeveloped_counts, 23);
  EcZoneFailure **maps[] = {&failures, &current_failures};
  for (u32 map = 0; map < 2; ++map) {
    if (good)
      good = stream->ReadU32(&count);
    if (!good || count > 437) {
      good = false;
      continue;
    }
    for (u32 index = 0; index < count && good; ++index) {
      u8 reason = 0, zone = 0;
      good = stream->ReadU8(&reason);
      good = good && stream->ReadU8(&zone) && stream->ReadU32(&identifier);
      if (good && reason < 19 && zone < 23) {
        EcZoneFailure **slot = maps[map];
        while (*slot && ((*slot)->reason < reason || ((*slot)->reason == reason && (*slot)->zone < zone)))
          slot = &(*slot)->next;
        if (!*slot || (*slot)->reason != reason || (*slot)->zone != zone)
          *slot = new EcZoneFailure{*slot, reason, zone, 0};
        (*slot)->count = identifier;
      }
    }
  }
  if (good)
    good = stream->ReadU32Array(zone_counts, 23);
  if (good) {
    good = cursor.SerialRead(owner, stream);
    if (good)
      good = stream->ReadU32(&next_x);
  } else {
    for (u32 x = 0; x < CellCountX(); ++x) {
      for (u32 z = 0; z < CellCountZ(); ++z) {
        u8 zone;
        GetValue(x, z, &zone);
        if (zone < 23)
          ++zone_counts[zone];
      }
    }
  }
  if (next_x >= CellCountX())
    next_x = 0;
  if (good)
    good = stream->ReadU32(&next_z);
  if (next_z >= CellCountZ())
    next_z = 0;
  good = good && stream->ReadU32(&current_abandoned) && stream->ReadU32(&abandoned) &&
      stream->ReadU32(&current_undeveloped);
  if (good)
    stream->ReadU32(&total_undeveloped);
  stream->Release();
  bool result = segment->CloseRecord(&record);
  for (u32 zone = 0; result && zone < 23; ++zone) {
    key.instance = zone + 1;
    if (!segment->OpenRecord(key, &record)) {
      result = true;
      continue;
    }
    CtZoneDeveloper *developer = developers[zone].developer;
    if (developer && record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      stream->ReadU32(&identifier);
      if (developers[zone].flags != identifier) {
        segment->CloseRecord(&record);
        result = segment->CloseRecord(&record);
        continue;
      }
      developer->SerialRead(owner, stream);
      stream->Release();
    }
    result = segment->CloseRecord(&record);
  }
  return result;
}

// The city lifecycle that initializes these state fields remains incomplete.
// Write the zoning map, simulation counters and traversal state, then each registered developer's own record.
bool EcZoneLayer::Save(CtCity *owner, RsDatabaseSegment *segment) {
  RsKey key{kRsCityLayerRecordType, kEcZoneLayerClassId, 0};
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (!segment->OpenRecordForWrite(key, &record))
    return false;
  if (!record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    segment->CloseRecord(&record);
    return false;
  }
  SerialWrite(stream);
  u32 count = 0;
  for (EcZoneDemand *entry = demand; entry; entry = entry->next)
    ++count;
  EcZoneDemand *demand_entry = demand;
  bool good = stream->WriteU32(count);
  for (u32 index = 0; demand_entry && good && index < count; ++index, demand_entry = demand_entry->next) {
    good = stream->WriteU32(demand_entry->identifier);
    if (good)
      good = stream->WriteU32(demand_entry->value);
  }
  good = good && stream->WriteU32Array(current_undeveloped_counts, 23) && stream->WriteU32Array(undeveloped_counts, 23);
  for (u32 map = 0; map < 2; ++map) {
    count = 0;
    for (EcZoneFailure *entry = map ? current_failures : failures; entry; entry = entry->next)
      ++count;
    if (good)
      good = stream->WriteU32(count);
    EcZoneFailure *entry = map ? current_failures : failures;
    for (u32 index = 0; entry && index < count; ++index, entry = entry->next) {
      good =
          good && stream->WriteU8(u8(entry->reason)) && stream->WriteU8(entry->zone) && stream->WriteU32(entry->count);
    }
  }
  good = good && stream->WriteU32Array(zone_counts, 23) && cursor.SerialWrite(owner, stream) &&
      stream->WriteU32(next_x) && stream->WriteU32(next_z) && stream->WriteU32(current_abandoned) &&
      stream->WriteU32(abandoned) && stream->WriteU32(current_undeveloped);
  if (good)
    stream->WriteU32(total_undeveloped);
  stream->Release();
  bool result = segment->CloseRecord(&record);
  for (u32 zone = 0; result && zone < 23; ++zone) {
    CtZoneDeveloper *developer = developers[zone].developer;
    if (developer) {
      key.instance = zone + 1;
      result = segment->OpenRecordForWrite(key, &record);
      if (result) {
        if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
          if (stream->WriteU32(developers[zone].flags))
            developer->SerialWrite(city, stream);
          stream->Release();
        }
        result = segment->CloseRecord(&record);
      }
    }
  }
  return result;
}

EcZoneLayer::EcZoneLayer()
    : EcZoneChangeSource(kEcZoneLayerType), layer_initialized(false), city(nullptr), city_width(0), city_height(0),
      zone_counts{}, undeveloped_counts{}, current_undeveloped_counts{}, total_undeveloped(0), current_undeveloped(0),
      abandoned(0), current_abandoned(0), developers{}, failures(nullptr), current_failures(nullptr), demand(nullptr),
      occupants(nullptr), valve_layer(nullptr), terrain(nullptr), building_layer(nullptr), demolition(nullptr),
      power(nullptr), land_value(nullptr), transport_test(nullptr), road_test(nullptr), cells_per_step(0), next_x(0),
      next_z(0), nerds_rule(false) {
}

// Unported subobjects retain their own explicit lifetime dependencies.
EcZoneLayer::~EcZoneLayer() {
  while (failures) {
    EcZoneFailure *entry = failures;
    failures = entry->next;
    delete entry;
  }
  while (current_failures) {
    EcZoneFailure *entry = current_failures;
    current_failures = entry->next;
    delete entry;
  }
  while (demand) {
    EcZoneDemand *entry = demand;
    demand = entry->next;
    delete entry;
  }
}

// Publish the requested zone, layer, agent, change or raster-factory identity and retain the common map owner.
bool EcZoneLayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtZoneLayer, CtZoneLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  HANDLE_QUERY_INTERFACE(IID_SpMapSpriteFactory, SpMapSpriteFactory);
  HANDLE_QUERY_INTERFACE(IID_CtChangeSource, CtChangeSource);
  return EcByteMap::QueryInterface(iid, out);
}

u32 EcZoneLayer::AddRef() {
  return EcByteMap::AddRef();
}

u32 EcZoneLayer::Release() {
  return EcByteMap::Release();
}

u32 EcZoneLayer::CellCountX() const {
  return EcByteMap::CellCountX();
}

u32 EcZoneLayer::CellCountZ() const {
  return EcByteMap::CellCountZ();
}

void EcZoneLayer::GetValueSint32(u32 x, u32 z, i32 *value) const {
  EcByteMap::GetValueSint32(x, z, value);
}

bool EcZoneLayer::InBounds(u32 x, u32 z) const {
  return EcByteMap::InBounds(x, z);
}

bool EcZoneLayer::InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return EcByteMap::InBounds(x0, z0, x1, z1);
}

bool EcZoneLayer::ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const {
  return EcByteMap::ValidRect(x0, z0, x1, z1);
}

bool EcZoneLayer::SerialRead(RsStream *record) {
  return EcByteMap::SerialRead(record);
}

bool EcZoneLayer::SerialWrite(RsStream *record) const {
  return EcByteMap::SerialWrite(record);
}

bool EcZoneLayer::Init(CtCity *owner, const u8 *fill) {
  return EcByteMap::Init(owner, fill);
}

void EcZoneLayer::GetValue(u32 x, u32 z, u8 *value) const {
  EcByteMap::GetValue(x, z, value);
}

// Update one changed zone cell and its counts before notifying the registered cell-change receivers.
void EcZoneLayer::SetValue(u32 x, u32 z, const u8 *value) {
  u8 old = cells->columns[x][z];
  if (old != *value) {
    if (old < 23 && zone_counts[old])
      --zone_counts[old];
    if (*value < 23)
      ++zone_counts[*value];
    cells->columns[x][z] = *value;
    Notify(x, z);
  }
}

// Normalize the rectangle, adjust its previous and replacement counts, fill its cells, then notify the rectangle.
void EcZoneLayer::SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) {
  if (x0 > x1) {
    u32 swap = x0;
    x0 = x1;
    x1 = swap;
  }
  if (z0 > z1) {
    u32 swap = z0;
    z0 = z1;
    z1 = swap;
  }
  for (u32 x = x0; x <= x1; ++x) {
    for (u32 z = z0; z <= z1; ++z) {
      u8 old = cells->columns[x][z];
      if (old < 23 && zone_counts[old])
        --zone_counts[old];
    }
  }
  if (*value < 23)
    zone_counts[*value] += (z1 - z0 + 1u) * (x1 - x0 + 1u);
  CtByteCells *grid = cells;
  for (u32 x = x0; x <= x1; ++x)
    memset(grid->columns[x] + z0, *value, z1 - z0 + 1u);
  Notify(x0, z0, x1, z1);
}

void EcZoneLayer::SetAllCells(const u8 *value) {
  EcByteMap::SetAllCells(value);
  u32 last_z = CellCountZ() - 1u, last_x = CellCountX() - 1u;
  Notify(0, 0, last_x, last_z);
  memset(zone_counts, 0, sizeof(zone_counts));
  if (*value < 23) {
    u32 rows = CellCountZ(), columns = CellCountX();
    zone_counts[*value] = columns * rows;
  }
}

u32 EcZoneLayer::GetZoneCount(u8 zone) {
  return zone < 23 ? zone_counts[zone] : 0;
}

u32 EcZoneLayer::GetUndevelopedTileCount() {
  return total_undeveloped;
}

u32 EcZoneLayer::GetUndevelopedTileCount(u8 zone) {
  return zone < 23 ? undeveloped_counts[zone] : 0;
}

// Find or create the original zero-initialized failure count for a zone and development-result pair.
u32 EcZoneLayer::GetDevelopmentFailureCount(u8 zone, i32 reason) {
  if (zone >= 23 || reason >= 19)
    return 0;
  EcZoneFailure **slot = &failures;
  while (*slot && ((*slot)->reason < reason || ((*slot)->reason == reason && (*slot)->zone < zone)))
    slot = &(*slot)->next;
  if (!*slot || (*slot)->reason != reason || (*slot)->zone != zone)
    *slot = new EcZoneFailure{*slot, reason, zone, 0};
  return (*slot)->count;
}

u32 EcZoneLayer::GetAbandonedTileCount() {
  return abandoned;
}

// Release the previous developer when present, then publish the replacement and retain it.
bool EcZoneLayer::RegisterZoneDeveloper(u8 zone, CtZoneDeveloper *developer, u32 flags) {
  assert(zone < sizeof(developers) / sizeof(*developers));
  DeveloperSlot *slot = &developers[zone];
  if (slot->developer)
    UnregisterZoneDeveloper(zone);
  slot->developer = developer;
  slot->flags = flags;
  developer->AddRef();
  return true;
}

bool EcZoneLayer::UnregisterZoneDeveloper(u8 zone) {
  DeveloperSlot *slot = &developers[zone];
  if (slot->developer) {
    slot->developer->Release();
    slot->developer = nullptr;
    slot->flags = 0;
  }
  return true;
}

// Return the registered developer as a borrowed pointer without acquiring another reference.
CtZoneDeveloper *EcZoneLayer::GetZoneDeveloper(u8 zone) {
  return developers[zone].developer;
}

// Validate the requested ground and existing occupants, then create, orient and insert the selected building.
bool EcZoneLayer::PlaceBuilding(GeBuildingType *building, const ScPointXZ &point, i32 state, u16 rotation, bool check) {
  RsKey key = *building->Key();
  u32 right = point.x + building->SizeInCellsX() - 1;
  u32 bottom = point.z + building->SizeInCellsZ() - 1;
  if (check) {
    u32 last_x = right + 1, last_z = bottom + 1;
    last_x = UintMin(last_x, terrain->CellCountX());
    last_z = UintMin(last_z, terrain->CellCountZ());
    u8 first;
    terrain->GetVertexAltitude(point.x, point.z, &first);
    for (u32 x = point.x; x <= last_x; ++x) {
      for (u32 z = point.z; z <= last_z; ++z) {
        u8 altitude;
        terrain->GetVertexAltitude(x, z, &altitude);
        if (altitude != first)
          return false;
      }
    }
  }
  for (u32 x = point.x; x <= right; ++x) {
    for (u32 z = point.z; z <= bottom; ++z) {
      if (!occupants->IsInBoundsCell(x, z))
        return false;
      ScOccupant *occupant;
      if (occupants->GetOccupantAtCell(x, z, &occupant)) {
        if (!occupant->CanSimRemove()) {
          occupant->Release();
          return false;
        }
        if (occupant->IsGeomFlag(kOcGeomNetwork)) {
          constexpr i32 sentinel = INT32_MAX;
          GeBounds bounds{{sentinel, sentinel, sentinel}, {sentinel, sentinel, sentinel}};
          GeBounds affected{{sentinel, sentinel, sentinel}, {sentinel, sentinel, sentinel}};
          u32 cost;
          occupant->GetBounds(&bounds);
          demolition->DemolishOccupants(occupants, &bounds, nullptr, 0, true, &cost, &affected);
        } else {
          occupants->RemoveOccupant(occupant);
        }
        occupant->Release();
      }
    }
  }
  BdBuilding *created;
  bool result = building_layer->CreateBuilding(key, IID_BdBuilding, reinterpret_cast<void **>(&created));
  if (result) {
    ScOccupant *occupant = nullptr;
    if (created->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&occupant))) {
      occupant->SetLocationInCellCoords(point.x, point.z);
      occupant->SetOrientation(u16(65535u * rotation / 360u), 0, 0);
      occupant->Release();
    }
    created->SetOccupancyState(u32(state));
    result = building_layer->InsertBuilding(created);
    created->Release();
  }
  return result;
}

// Resolve the building attribute by resource key, place it through the original attribute overload, then release it.
bool EcZoneLayer::PlaceBuilding(const RsKey &key, const ScPointXZ &point, i32 state, u16 rotation, bool check) {
  GeBuildingType *building;
  if (!attributes->GetGenericAttrib(key, IID_GeBuildingType, reinterpret_cast<void **>(&building)))
    return false;
  bool result = PlaceBuilding(building, point, state, rotation, check);
  building->Release();
  return result;
}

// Search the clipped transport neighborhood and release the returned occupant iterator after checking its current item.
bool EcZoneLayer::IsNearTransport(const ScPointXZ &point, u32 distance) {
  OcIterator *iterator = nullptr;
  u32 width = occupants->GetCellsInX(), height = occupants->GetCellsInZ();
  if (point.x >= width || point.z >= height)
    return false;
  i32 left = i32(point.x - distance - 1u), top = i32(point.z - distance - 1u);
  if (left < 0)
    left = 0;
  if (top < 0)
    top = 0;
  u32 right = point.x + distance + 1u, bottom = point.z + distance + 1u;
  if (right >= width)
    right = width - 1u;
  if (bottom >= height)
    bottom = height - 1u;
  GeBounds bounds{{i32(u32(left) << kCtMicroCoordinateShift), i32(u32(top) << kCtMicroCoordinateShift), 0},
      {i32(right << kCtMicroCoordinateShift), i32(bottom << kCtMicroCoordinateShift), 0}};
  if (!occupants->GetOccupants(&iterator, &bounds, transport_test))
    return false;
  bool result = iterator->Current() != nullptr;
  iterator->Release();
  return result;
}

// Count road-test occupants beside an ordered horizontal or vertical edge of the requested zone block.
u32 EcZoneLayer::GetRoadCount(const ScPointXZ &first, const ScPointXZ &last) {
  if (first.x != last.x && first.z != last.z)
    return 0;
  if (first.x == last.x && first.z == last.z)
    return 0;
  u32 width = occupants->GetCellsInX(), height = occupants->GetCellsInZ();
  if (first.x >= width || last.x >= width || first.z >= height || last.z >= height)
    return 0;
  u32 left, right, top, bottom;
  if (first.x != last.x) {
    if (first.x <= last.x) {
      if (!first.z)
        return 0;
      left = first.x;
      right = last.x;
      top = bottom = first.z - 1u;
    } else {
      if (first.z + 1u >= height)
        return 0;
      left = last.x;
      right = first.x;
      top = bottom = first.z + 1u;
    }
  } else if (first.z <= last.z) {
    if (first.x + 1u >= width)
      return 0;
    left = right = first.x + 1u;
    top = first.z;
    bottom = last.z;
  } else {
    if (!first.x)
      return 0;
    left = right = first.x - 1u;
    top = last.z;
    bottom = first.z;
  }
  GeBounds bounds{{i32(left << kCtMicroCoordinateShift), i32(top << kCtMicroCoordinateShift), 0},
      {i32(right << kCtMicroCoordinateShift), i32(bottom << kCtMicroCoordinateShift), 0}};
  OcIterator *iterator = nullptr;
  if (!occupants->GetOccupants(&iterator, &bounds, road_test))
    return 0;
  u32 count = 0;
  for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next())
    ++count;
  iterator->Release();
  return count;
}

// Check the zone, terrain, utility and occupant constraints before consulting nearby transport.
bool EcZoneLayer::TileMatches(
    const u8 *altitude, const ScPointXZ &point, u8 zone, u32 distance, u8 minimum_value, u32 type, bool ignore_power) {
  u8 candidate;
  GetValue(point.x, point.z, &candidate);
  if (candidate != zone)
    return false;
  terrain->GetVertexAltitude(point.x, point.z, &candidate);
  if (*altitude != candidate)
    return false;
  if (!ignore_power) {
    bool powered;
    power->GetValue(point.x, point.z, &powered);
    if (!powered)
      return false;
  }
  land_value->GetValue(point.x, point.z, &candidate);
  if (candidate < minimum_value)
    return false;
  ScOccupant *occupant;
  if (occupants->GetOccupantAtCell(point.x, point.z, &occupant)) {
    if (!occupant->CanSimRemove()) {
      occupant->Release();
      return false;
    }
    BdBuilding *building = nullptr;
    bool has_building = occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    if (has_building && building->GetOccupancyStateFlag(kGeBuildingHistorical)) {
      occupant->Release();
      building->Release();
      return false;
    }
    if (occupant->MajorType() != 5 && occupant->TypeId() != type) {
      if (has_building) {
        u32 state = building->GetOccupancyState();
        if (state != kGeBuildingAbandoned) {
          GeAttrib *base = building->GetBaseAttribData();
          GeBuildingType *building_type = nullptr;
          bool accepted = false;
          if (state != 8 && state != 16 &&
              base->QueryInterface(IID_GeBuildingType, reinterpret_cast<void **>(&building_type))) {
            accepted = (building_type->Flags() & 0x100) != 0;
            building_type->Release();
          }
          if (!accepted) {
            occupant->TypeId();
            occupant->Release();
            building->Release();
            return false;
          }
        }
      } else {
        occupant->Release();
        return false;
      }
    }
    if (building)
      building->Release();
    occupant->Release();
  }
  return IsNearTransport(point, distance);
}

// Expand a rectangle by one eligible column, preferring the left edge before the right edge.
u8 EcZoneLayer::GrowRectX(GeCellRect *rectangle, u8 zone, u32 distance, u8 minimum_value, u32 type, bool ignore_power) {
  u32 width = occupants->GetCellsInX();
  u8 altitude;
  terrain->GetVertexAltitude(rectangle->left, rectangle->top, &altitude);
  GeCellRect expanded = *rectangle;
  if (rectangle->left) {
    ScPointXZ point{rectangle->left - 1, rectangle->top};
    while (TileMatches(&altitude, point, zone, distance, minimum_value, type, ignore_power)) {
      if (point.z++ >= rectangle->bottom) {
        expanded.left = point.x;
        *rectangle = expanded;
        return 1;
      }
    }
  }
  if (rectangle->right + 1 < width) {
    ScPointXZ point{rectangle->right + 1, rectangle->top};
    while (TileMatches(&altitude, point, zone, distance, minimum_value, type, ignore_power)) {
      if (point.z++ >= rectangle->bottom) {
        expanded.right = point.x;
        *rectangle = expanded;
        return 1;
      }
    }
  }
  return 0;
}

// Expand a rectangle by one eligible row, preferring the top edge before the bottom edge.
u8 EcZoneLayer::GrowRectZ(GeCellRect *rectangle, u8 zone, u32 distance, u8 minimum_value, u32 type, bool ignore_power) {
  u32 height = occupants->GetCellsInZ();
  u8 altitude;
  terrain->GetVertexAltitude(rectangle->left, rectangle->top, &altitude);
  GeCellRect expanded = *rectangle;
  if (rectangle->top) {
    ScPointXZ point{rectangle->left, rectangle->top - 1};
    while (TileMatches(&altitude, point, zone, distance, minimum_value, type, ignore_power)) {
      if (point.x++ >= rectangle->right) {
        expanded.top = point.z;
        *rectangle = expanded;
        return 1;
      }
    }
  }
  if (rectangle->bottom + 1 < height) {
    ScPointXZ point{rectangle->left, rectangle->bottom + 1};
    while (TileMatches(&altitude, point, zone, distance, minimum_value, type, ignore_power)) {
      if (point.x++ >= rectangle->right) {
        expanded.bottom = point.z;
        *rectangle = expanded;
        return 1;
      }
    }
  }
  return 0;
}

// Expand the starting tile in alternating column and row steps until neither direction can grow.
void EcZoneLayer::FindBuildableRect(
    GeCellRect *out, u32 x, u32 z, u32 distance, u8 minimum_value, u32 type, bool ignore_power) {
  *out = GeCellRect{x, z, x, z};
  u8 zone;
  GetValue(x, z, &zone);
  u8 grow_x = 1, grow_z = 1;
  do {
    do {
      if (grow_x)
        grow_x = GrowRectX(out, zone, distance, minimum_value, type, ignore_power);
      if (grow_z)
        grow_z = GrowRectZ(out, zone, distance, minimum_value, type, ignore_power);
    } while (grow_x);
  } while (grow_z);
}

// Return the filled-demand amount for the exact identifier, inserting a zero-valued entry when absent.
u32 EcZoneLayer::GetFilledDemand(u32 identifier) {
  EcZoneDemand **slot = &demand;
  while (*slot && (*slot)->identifier < identifier)
    slot = &(*slot)->next;
  if (!*slot || (*slot)->identifier != identifier)
    *slot = new EcZoneDemand{*slot, identifier, 0};
  return (*slot)->value;
}

// Add a signed demand change to the identifier's stored DWORD, preserving original wraparound.
void EcZoneLayer::AddToFilledDemand(u32 identifier, i32 amount) {
  EcZoneDemand **slot = &demand;
  while (*slot && (*slot)->identifier < identifier)
    slot = &(*slot)->next;
  if (!*slot || (*slot)->identifier != identifier)
    *slot = new EcZoneDemand{*slot, identifier, 0};
  (*slot)->value += u32(amount);
}

// Map supported zone bytes to their palette indices, clearing the color type before reading the live cell.
bool EcZoneLayer::GetZoneColor(u32 x, u32 z, u8 *color_type, u16 *color) {
  *color_type = 0;
  u8 zone;
  GetValue(x, z, &zone);
  static const u16 colors[23] = {0, 26, 25, 24, 0, 29, 28, 27, 0, 23, 22, 21, 0, 33, 30, 31, 34, 32, 0, 0, 0, 0, 34};
  *color = zone < 23 ? colors[zone] : 0;
  return *color != 0;
}

// Count eligible zone cells and their demolition costs, then compare the full charge with available city funds.
bool EcZoneLayer::CanZone(i32 zone, const GeBounds &bounds, i32 *cost) {
  EcBudgetLayer *budget = city->BudgetLayer();
  ScDirtBag *dirt = city->DirtBag();
  ScOccupantManager *surface = city->SurfaceOccupantManager();
  city->UndergroundOccupantManagerLevelOne();
  city->UndergroundOccupantManagerLevelTwo();
  i32 demolition_cost = budget->GetCost(kEcCostDemolish);
  GeBounds area = bounds;
  area.NormalizeXZ();
  u32 left = u32(area.minimum.x >> kCtMicroCoordinateShift), top = u32(area.minimum.z >> kCtMicroCoordinateShift);
  u32 right = u32(area.maximum.x >> kCtMicroCoordinateShift), bottom = u32(area.maximum.z >> kCtMicroCoordinateShift);
  u32 valid_count = 0, added_cost = 0;
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      bool valid = true;
      u8 old_zone;
      GetValue(x, z, &old_zone);
      if (i32(old_zone) == zone)
        valid = false;
      if (valid) {
        ScOccupant *occupant;
        if (surface->GetOccupantAtCell(x, z, &occupant)) {
          if (!occupant->CanUserRemove())
            valid = false;
          else if (occupant->IsGeomFlag(kOcGeomPower) && occupant->IsGeomFlag(kOcGeomNetwork))
            added_cost += u32(demolition_cost);
          else if (!occupant->IsGeomFlag(1024))
            valid = false;
          occupant->Release();
        }
        if (valid && dirt->IsWater(x, z))
          valid = false;
      }
      if (left == right && top == bottom) {
        u8 first, second, third, fourth;
        dirt->GetVertexAltitudeDirt(x, z, &first);
        dirt->GetVertexAltitudeDirt(x + 1, z, &second);
        dirt->GetVertexAltitudeDirt(x + 1, z + 1, &third);
        dirt->GetVertexAltitudeDirt(x, z + 1, &fourth);
        if (first != second || first != third || first != fourth)
          valid = false;
      }
      if (valid)
        ++valid_count;
    }
  }
  i32 category = -1;
  switch (zone) {
  case 0:
    category = 6;
    break;
  case 1:
  case 5:
  case 9:
    category = 0;
    break;
  case 2:
  case 6:
  case 10:
    category = 1;
    break;
  case 3:
  case 7:
  case 11:
    category = 2;
    break;
  case 14:
    category = 4;
    break;
  case 15:
    category = 5;
    break;
  case 17:
    category = 3;
    break;
  }
  i32 unit_cost = category < 0 ? 0 : budget->GetCost(category);
  *cost = i32(added_cost + valid_count * u32(unit_cost));
  return valid_count && (*cost == 0 || i64(*cost) <= budget->GetTotalFunds());
}

bool EcZoneLayer::NerdsRule() {
  return nerds_rule;
}

// Enable the original zoning cheat when its matching message arrives, then record it in the application manager.
bool EcZoneLayer::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CheatCommand || u32(message->data1) != kEcCheat_NerdsRule)
    return false;
  nerds_rule = true;
  EcGetRciApplication()->GetCheatCodeManager()->SetCheatHasBeenSet(kEcCheat_NerdsRule, true);
  return true;
}

// Import the legacy zone plane in descending cell order, translating its original ten zone categories.
bool EcZoneLayer::Init(CtCity *, ScImporter *importer) {
  static const u8 zone_map[10] = {0, 1, 3, 5, 7, 9, 11, 0, 14, 15};
  if (!importer->MapIsPresent(kCtLegacyXzonPlane)) {
    Shutdown();
    return false;
  }
  i32 last = i32(importer->GetMapSize(kCtLegacyXzonPlane)) - 1;
  for (i32 x = last; x >= 0; --x) {
    for (i32 z = last; z >= 0; --z) {
      u8 legacy_zone = 0;
      importer->GetZoneValue(u32(x), u32(z), &legacy_zone);
      if (legacy_zone >= 10)
        legacy_zone = 0;
      SetValue(u32(x), u32(z), &zone_map[legacy_zone]);
    }
  }
  return true;
}

// Bind each registered developer, remove zones on uneven terrain, and register the live simulation callbacks.
bool EcZoneLayer::SimulationBegin() {
  for (u32 zone = 0; zone < 23; ++zone) {
    CtZoneDeveloper *developer = developers[zone].developer;
    if (developer && !developer->BindDemand())
      return false;
  }
  u32 width = CellCountX(), height = CellCountZ();
  for (u32 x = 0; x < width; ++x) {
    for (u32 z = 0; z < height; ++z) {
      u8 zone;
      GetValue(x, z, &zone);
      if (zone) {
        u8 first, next;
        terrain->GetVertexAltitude(x, z, &first);
        terrain->GetVertexAltitude(x, z + 1, &next);
        bool uneven = first != next;
        if (!uneven) {
          terrain->GetVertexAltitude(x + 1, z, &next);
          uneven = first != next;
        }
        if (!uneven) {
          terrain->GetVertexAltitude(x + 1, z + 1, &next);
          uneven = first != next;
        }
        if (uneven) {
          u8 empty = 0;
          SetValue(x, z, &empty);
          ScOccupant *occupant;
          if (occupants->GetOccupantAtCell(x, z, &occupant)) {
            if (!occupant->IsGeomFlag(kOcGeomNetwork))
              occupants->RemoveOccupant(occupant);
            occupant->Release();
          }
        }
      }
    }
  }
  u32 sender;
  if (!occupants->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &sender, false))
    return false;
  EcGetRciCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  EcGetRciApplication()->GetCheatCodeManager()->AddNotification(static_cast<CtLayer *>(this), kEcCheat_NerdsRule);
  return true;
}

bool EcZoneLayer::SimulationEnd() {
  CtSimulatorInterface *simulator = EcGetRciCity()->CitySimulator();
  bool result = true;
  EcGetRciApplication()->GetCheatCodeManager()->RemoveNotification(static_cast<CtLayer *>(this), kEcCheat_NerdsRule);
  if (simulator)
    result = simulator->RemoveAgent(static_cast<CtAgent *>(this));
  if (occupants)
    occupants->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
  return result;
}

// Unregister developers and the zoning cheat, then release retained city services and the two owned filters.
bool EcZoneLayer::Shutdown() {
  EcGetRciApplication()->GetCheatCodeManager()->UnregisterCheatCode(kEcCheat_NerdsRule);
  for (u32 zone = 0; zone < 23; ++zone)
    UnregisterZoneDeveloper(u8(zone));
  ScRelease(&occupants);
  ScRelease(&terrain);
  ScRelease(&city);
  ScRelease(&building_layer);
  ScRelease(&power);
  ScRelease(&land_value);
  ScRelease(&demolition);
  ScRelease(&valve_layer);
  ScRelease(&transport_test);
  ScRelease(&road_test);
  failure_map.Shutdown();
  layer_initialized = false;
  return EcByteMap::Shutdown();
}

// Release the shared attribute cache when present and clear it after the callback.
bool EcZoneLayer::StaticShutdown() {
  if (attributes)
    attributes->Release();
  attributes = nullptr;
  return true;
}

bool EcZoneLayer::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *) {
  *out = nullptr;
  return true;
}

u32 EcZoneLayer::GetLayerType() {
  return kEcZoneLayerType;
}

bool EcZoneLayer::DebugClassTag(ScStringView *) {
  return true;
}

bool EcZoneLayer::DebugTypeTag(ScStringView *) {
  return true;
}

// Run the zone simulation on the original two-tick interval and acknowledge every agent call.
bool EcZoneLayer::Simulate(i32 elapsed) {
  if (elapsed == 2)
    SimulateZones();
  return true;
}

// Evaluate the configured number of zone cells and publish failure and demand counters at each traversal boundary.
void EcZoneLayer::SimulateZones() {
  // The original reuses these outputs across iterations; zero excludes an initially unwritten stack read.
  i32 x = 0, z = 0;
  for (u32 step = 0; step < cells_per_step; ++step) {
    u8 zone;
    GetValue(next_x, next_z, &zone);
    CtZoneDeveloper *developer = developers[zone].developer;
    if (developer) {
      city->Lock();
      ScPointXZ point{next_x, next_z};
      i32 result = developer->Evaluate(point);
      if (result != 0 && result < 19) {
        u8 failure = u8(result);
        failure_map.SetValue(next_x, next_z, &failure);
        EcZoneFailure **slot = &current_failures;
        while (*slot && ((*slot)->reason < result || ((*slot)->reason == result && (*slot)->zone < zone)))
          slot = &(*slot)->next;
        if (!*slot || (*slot)->reason != result || (*slot)->zone != zone)
          *slot = new EcZoneFailure{*slot, result, zone, 0};
        ++(*slot)->count;
        if (result >= 9) {
          ++current_undeveloped_counts[zone];
          ++current_undeveloped;
        } else if (result >= 1) {
          ++current_abandoned;
        }
      }
      city->Unlock();
    }
    if (cursor.phase == 2) {
      cursor.Reset(1);
      if (zone_counts[15])
        terrain->RecomputeSeaportMap();
      while (failures) {
        EcZoneFailure *entry = failures;
        failures = entry->next;
        delete entry;
      }
      EcZoneFailure **tail = &failures;
      for (EcZoneFailure *entry = current_failures; entry; entry = entry->next) {
        *tail = new EcZoneFailure{nullptr, entry->reason, entry->zone, entry->count};
        tail = &(*tail)->next;
        entry->count = 0;
      }
      for (u32 index = 0; index < 23; ++index) {
        undeveloped_counts[index] = current_undeveloped_counts[index];
        current_undeveloped_counts[index] = 0;
      }
      abandoned = current_abandoned;
      current_abandoned = 0;
      total_undeveloped = current_undeveloped;
      current_undeveloped = 0;
      for (EcZoneDemand *entry = demand; entry; entry = entry->next)
        entry->value = 0;
    }
    cursor.Next(&x, &z);
    next_x = u32(x);
    next_z = u32(z);
  }
}

i32 EcZoneLayer::MinimumExecutionInterval() {
  return 2;
}

bool EcZoneLayer::IsAgentLocking() {
  return true;
}

u32 EcZoneLayer::TypeId() const {
  return 0;
}

// Report that the zoning agent has no agent-type object.
CtAgentType *EcZoneLayer::AgentType() {
  return nullptr;
}

bool EcZoneLayer::SetAgentType(CtAgentType *) {
  return false;
}

bool EcZoneLayer::DebugInstanceTag(ScStringView *) {
  return false;
}

// Clear removed reserved cells and apply the occupant's signed area to each matching filled-demand coefficient.
u8 EcZoneLayer::UpdateOccupantDemand(i32 operation, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  GeCellRect rectangle;
  occupant->GetBoundingRectInCellCoords(&rectangle);
  if (operation == 1) {
    for (u32 x = rectangle.left; x <= rectangle.right; ++x) {
      for (u32 z = rectangle.top; z <= rectangle.bottom; ++z) {
        u8 zone;
        GetValue(x, z, &zone);
        if (zone == 22) {
          u8 empty = 0;
          SetValue(x, z, &empty);
        }
      }
    }
  }
  u8 area = 1;
  BdBuilding *building = nullptr;
  bool apply = true;
  if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
    u32 occupancy = building->GetOccupancyState();
    u8 size_z = occupant->SizeInCellsZ();
    area = u8(occupant->SizeInCellsX() * size_z);
    building->Release();
    apply = occupancy == 1 || occupancy == 4;
  }
  if (apply) {
    if (operation == 1)
      area = u8(0u - area);
    i32 signed_area = area < 128 ? area : i32(area) - 256;
    u32 agent = occupant->TypeId();
    for (EcZoneDemand *entry = demand; entry; entry = entry->next) {
      i32 effect = valve_layer->GetAgentSupplyEffect(entry->identifier, agent);
      if (effect)
        entry->value += u32(effect * signed_area);
    }
  }
  return 1;
}

bool EcZoneLayer::OccupantInserted(u32, ScOccupant *occupant) {
  return UpdateOccupantDemand(0, occupant) != 0;
}

bool EcZoneLayer::OccupantRemoved(u32, ScOccupant *occupant) {
  return UpdateOccupantDemand(1, occupant) != 0;
}

bool EcZoneLayer::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool EcZoneLayer::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool EcZoneLayer::CellChangedAll(u32) {
  return true;
}

bool EcZoneLayer::CellChanged(u32, u32, u32) {
  return true;
}

bool EcZoneLayer::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

EcZoneChangeSource::EcZoneChangeSource(u32 sender)
    : sender_id(sender), source_references(0), notify_enabled(1), receivers_begin(new CtChangeReceiver *[2]),
      receivers_end(receivers_begin), receivers_limit(receivers_begin + 2) {
}

EcZoneChangeSource::~EcZoneChangeSource() {
  RemoveAll();
  delete[] receivers_begin;
}

// Expand the receiver vector while preserving its retained pointers and order.
void EcZoneChangeSource::GrowReceivers() {
  usize count = receivers_end - receivers_begin, capacity = count ? count * 2 : 1;
  if (capacity < count || capacity > SIZE_MAX / sizeof(*receivers_begin))
    ScFatal(SC_ERROR_NOMEM, "City-change receiver array cannot grow");
  CtChangeReceiver **data = new CtChangeReceiver *[capacity];
  if (count)
    memcpy(data, receivers_begin, count * sizeof(*data));
  delete[] receivers_begin;
  receivers_begin = data;
  receivers_end = data + count;
  receivers_limit = data + capacity;
}

u32 EcZoneChangeSource::GetID() const {
  return sender_id;
}

// Publish the source identifier, remove an existing matching receiver, then append and retain the new receiver.
bool EcZoneChangeSource::AddReceiver(CtChangeReceiver *receiver, u32 *sender) {
  if (!receiver)
    return false;
  *sender = sender_id;
  RemoveReceiver(receiver);
  if (receivers_end == receivers_limit)
    GrowReceivers();
  *receivers_end++ = receiver;
  receiver->AddRef();
  return true;
}

// Find the last matching receiver, release it before shifting later entries, and remove one list element.
bool EcZoneChangeSource::RemoveReceiver(CtChangeReceiver *receiver) {
  if (!receiver)
    return false;
  isize last = receivers_end - receivers_begin - 1, found = last;
  while (found >= 0 && receivers_begin[found] != receiver)
    --found;
  if (found < 0)
    return false;
  receiver->Release();
  while (found < last) {
    receivers_begin[found] = receivers_begin[found + 1];
    ++found;
  }
  --receivers_end;
  return true;
}

// Release registered receivers from the end of the list, shrinking the live range after each callback.
bool EcZoneChangeSource::RemoveAll() {
  isize index = receivers_end - receivers_begin;
  while (--index >= 0) {
    receivers_begin[index]->Release();
    --receivers_end;
  }
  return true;
}

void EcZoneChangeSource::SetEnabled(u8 enabled) {
  notify_enabled = enabled;
}

u8 EcZoneChangeSource::GetEnabled() const {
  return notify_enabled;
}

// Notify registered receivers in reverse order when enabled, ignoring each callback's return.
bool EcZoneChangeSource::NotifyAll() {
  if (notify_enabled) {
    isize index = receivers_end - receivers_begin;
    while (--index >= 0)
      receivers_begin[index]->CellChangedAll(sender_id);
  }
  return true;
}

bool EcZoneChangeSource::Notify(u32 x, u32 z) {
  if (notify_enabled) {
    isize index = receivers_end - receivers_begin;
    while (--index >= 0)
      receivers_begin[index]->CellChanged(sender_id, x, z);
  }
  return true;
}

bool EcZoneChangeSource::Notify(u32 left, u32 top, u32 right, u32 bottom) {
  if (notify_enabled) {
    isize index = receivers_end - receivers_begin;
    while (--index >= 0)
      receivers_begin[index]->CellChanged(sender_id, left, top, right, bottom);
  }
  return true;
}

u32 EcZoneLayer::GetMapSpriteCount() {
  return 1;
}

// Publish the zoning raster identifier for index zero and leave other outputs untouched.
bool EcZoneLayer::GetSpriteIdFromIndex(u32 index, u32 *out) {
  if (index)
    return false;
  *out = kUiZoningMapRasterId;
  return true;
}

// Load a localized zone-query string or replace the destination with the original empty fallback.
static void EcReadZoneText(u32 group, u32 instance, ScStringView *out) {
  EcRci::ScTextResource resource(instance, group);
  if (resource.GetResource())
    out->Copy(*resource.GetResource());
  else
    out->FromChar("");
}

// Publish localized zone names or development-failure details, respecting the queried building's occupancy state.
u8 EcZoneLayer::DispatchZoneQuery(
    i32 x, i32 z, ScStringView *label, ScStringView *value, ScOccupant *target, u32 code) {
  if (code == kEcZoneNameQueryCode) {
    EcReadZoneText(kRsCityServiceTextGroup, 14, label);
    u8 zone = 0;
    GetValue(u32(x), u32(z), &zone);
    u32 resource = 405;
    switch (zone) {
    case 1:
      resource = 15;
      break;
    case 2:
      resource = 16;
      break;
    case 3:
      resource = 17;
      break;
    case 5:
      resource = 18;
      break;
    case 6:
      resource = 19;
      break;
    case 7:
      resource = 20;
      break;
    case 9:
      resource = 21;
      break;
    case 10:
      resource = 22;
      break;
    case 11:
      resource = 23;
      break;
    case 14:
      resource = 25;
      break;
    case 15:
      resource = 24;
      break;
    case 17:
      resource = 26;
      break;
    default:
      break;
    }
    ScString text;
    if (zone == 13) {
      text.FromChar("BUG: No military zones allowed");
    } else if (zone == 16) {
      text.FromChar("BUG: No spaceport zones allowed");
    } else {
      EcRci::ScTextResource loaded(resource, kRsCityServiceTextGroup);
      text = loaded.GetText();
    }
    value->Copy(text);
    return 1;
  }
  if (code != kEcZoneDevelopmentFailureQueryCode)
    return 0;
  u8 zone = 0, reason = 0;
  GetValue(u32(x), u32(z), &zone);
  if (!zone)
    return 0;
  failure_map.GetValue(u32(x), u32(z), &reason);
  if (reason > 18)
    return 0;
  static const u16 details[19] = {
      0, 715, 707, 711, 712, 713, 714, 708, 709, 716, 715, 717, 718, 711, 712, 713, 714, 719, 721};
  u32 first_group = 0, second_group = 0, first = 0, second = 0;
  if (reason) {
    first_group = second_group = kRsSupplementalGameTextGroup;
    first = reason <= 8 ? 705 : 706;
    second = details[reason];
  }
  if (!target) {
    if (!reason) {
      first_group = second_group = kRsSupplementalGameTextGroup;
      first = 706;
      second = 721;
    }
  } else {
    u32 state = 0;
    BdBuilding *building = nullptr;
    if (target->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
      state = building->GetOccupancyState();
      building->Release();
    }
    if (!(state & 2))
      return 0;
    if (!reason) {
      first_group = second_group = kRsSupplementalGameTextGroup;
      first = 705;
      second = 709;
    }
  }
  EcReadZoneText(first_group, first, label);
  EcReadZoneText(second_group, second, value);
  return 1;
}

// Add the zone name and available development-failure information for the query's actual city location.
u8 EcZoneLayer::QueryTile(ScMessage *, UiQuery *query) {
  if (!query)
    return 0;
  CtCoord location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  ScString label, value;
  if (!query->GetLocation(&location))
    return 0;
  ScOccupant *target = query->GetTarget();
  if (DispatchZoneQuery(location.x >> kCtMicroCoordinateShift, location.z >> kCtMicroCoordinateShift, &label, &value,
          target, kEcZoneNameQueryCode))
    query->AddText(&label, &value, 128, 0);
  if (DispatchZoneQuery(location.x >> kCtMicroCoordinateShift, location.z >> kCtMicroCoordinateShift, &label, &value,
          target, kEcZoneDevelopmentFailureQueryCode))
    query->AddText(&label, &value, 128, 0);
  return 1;
}

// Route supported city-query messages through the requested original query or raster interface.
bool EcZoneLayer::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type != kWnMsg_QueryOpened || !source)
    return false;
  u32 kind = u32(message->data1);
  if (kind == kUiQueryTagDialog || kind == kUiQueryTagDebug) {
    ScUnknown *view = nullptr;
    if (!source->QueryInterface(
        kind == kUiQueryTagDialog ? IID_UiQueryWindow : IID_UiQuerySection, reinterpret_cast<void **>(&view)))
      return false;
    view->Release();
    return false;
  }
  if (kind == kUiQueryTagStandard) {
    UiQuery *query = nullptr;
    if (!source->QueryInterface(IID_UiQuerySection, reinterpret_cast<void **>(&query)))
      return false;
    bool result = QueryTile(message, query) != 0;
    query->Release();
    return result;
  }
  if (kind == kUiQueryTagMaps) {
    UiRasterQuery *query = nullptr;
    if (!source->QueryInterface(IID_UiRasterQuery, reinterpret_cast<void **>(&query)))
      return false;
    GxRasterSource *raster = nullptr;
    bool result = CreateMapSprite(kUiZoningMapRasterId, IID_GxRasterSource, reinterpret_cast<void **>(&raster));
    if (result)
      result = query->AddRaster(raster) != 0;
    if (raster)
      raster->Release();
    query->Release();
    return result;
  }
  return false;
}

bool EcZoneLayer::StaticInit(CtDefinition *definition) {
  attributes = definition->GetOccupantAttribCache();
  if (!attributes)
    return false;
  attributes->AddRef();
  return RegisterCommandFactory();
}

bool EcZoneLayer::RegisterCommandFactory() {
  EcZoneCommandFactory *factory = new EcZoneCommandFactory;
  factory->AddRef();
  ScCommandFactory *manager = nullptr;
  EcGetRciFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&manager));
  manager->AddCommandFactory(factory);
  factory->Release();
  manager->Release();
  return true;
}

// Failed interface queries use matching delete under project allocation rules;
// the installed body frees storage without running the raster destructor.
// Create the zoning map raster for the advertised sprite identifier and return its requested interface.
bool EcZoneLayer::CreateMapSprite(u32 kind, u32 iid, void **out) {
  if (kind != kUiZoningMapRasterId)
    return false;
  GxRciZoneMap *sprite = new GxRciZoneMap(static_cast<EcByteMap *>(this));
  if (sprite->QueryInterface(iid, out))
    return true;
  delete sprite;
  return false;
}

// Borrow the application's current city window when an application is present.
static SpCityWindowView *EcRciCityWindow() {
  ApGameInterface *application = EcGetRciApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

EcZoneToolBase::EcZoneToolBase()
    : windows(nullptr), window(nullptr), view(nullptr), cursor_map(nullptr), budget(nullptr),
      bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      mouse_scroll(0), edge_scroll(0), cursor_visible(0), cost_visible(0), cost(0), invalid(0), insufficient_funds(0),
      field58(0), detach_after_input(0) {
}

// Ordinary destruction is reached through the deleting destructor.
// Release retained budget, cursor, view and window in order; the window-manager field is borrowed.
EcZoneToolBase::~EcZoneToolBase() {
  ScRelease(&budget);
  ScRelease(&cursor_map);
  ScRelease(&view);
  ScRelease(&window);
}

bool EcZoneToolBase::DoMessage(ScMessage *) {
  return false;
}

bool EcZoneToolBase::Init() {
  return true;
}

bool EcZoneToolBase::Shutdown() {
  window->SetCellCursorVisible(false);
  window->SetCostDisplay(false, 0, 0);
  cursor_visible = 0;
  return true;
}

bool EcZoneToolBase::OnKeyDown(u32 key, u32 flags) {
  return window->DefOnKeyDown(key, flags);
}

bool EcZoneToolBase::OnKeyUp(u32 key, u32 flags) {
  return window->DefOnKeyUp(key, flags);
}

// Preserve the default left-button reply across the live window lookup and optional detachment.
bool EcZoneToolBase::OnMouseDownL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseDownL(x, y, flags);
  DetachIfRequested(EcRciCityWindow());
  return result;
}

bool EcZoneToolBase::OnMouseDownR(i32 x, i32 y, u32) {
  mouse_scroll = 1;
  window->SetMouseScroll(i16(x), i16(y), true);
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  cost_visible = 0;
  window->SetCostDisplay(false, 0, 0);
  return true;
}

// Preserve the default left-button release reply while performing the optional live-window detachment.
bool EcZoneToolBase::OnMouseUpL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseUpL(x, y, flags);
  DetachIfRequested(EcRciCityWindow());
  return result;
}

// End mouse scrolling, restore the cursor and preserve the original optional detachment ordering.
bool EcZoneToolBase::OnMouseUpR(i32 x, i32 y, u32) {
  mouse_scroll = 0;
  window->SetMouseScroll(i16(x), i16(y), false);
  cursor_visible = 1;
  window->SetCellCursorVisible(true);
  cost_visible = 1;
  DetachIfRequested(EcRciCityWindow());
  return true;
}

bool EcZoneToolBase::OnMouseMove(i32 x, i32 y, u32 flags) {
  if (!cursor_visible) {
    window->SetCellCursorSize(1, 1);
    window->SetCellCursorVisible(true);
    cursor_visible = 1;
  }
  bool result = window->DefOnMouseMove(x, y, flags);
  DetachIfRequested(EcRciCityWindow());
  return result;
}

bool EcZoneToolBase::GetCursor(u32 *out) {
  *out = kSpCenterCursorId;
  return true;
}

// Cancel active scrolling and cost display, then reset the stored price.
void EcZoneToolBase::OnResetState() {
  if (mouse_scroll)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
}

bool EcZoneToolBase::IsValidHandlerByView(i32, u32) const {
  return true;
}

// Retain the host, view, cursor and budget before activating this handler and publishing its cursor settings.
bool EcZoneToolBase::AttachHost(SpCityWindowView *host) {
  windows = EcGetRciWindowManager();
  window = ScAddRefNotNull(host);
  view = ScAddRefNotNull(window->GetCurrentView());
  cursor_map = ScAddRefNotNull(window->GetCellCursorMap());
  CtCity *owner = EcGetRciCity();
  bounds.minimum = {0, 0, 0};
  u32 depth = owner->CellCountZ() - 1, width = owner->CellCountX();
  bounds.maximum.z = i32(depth << kCtMicroCoordinateShift);
  bounds.maximum.x = i32((width << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell);
  bounds.maximum.altitude = 0;
  budget = ScAddRefNotNull(owner->BudgetLayer());
  bool result = false;
  void *publication;
  if (QueryInterface(IID_SpViewHandler, &publication)) {
    result = window->SetActiveHandler(static_cast<SpViewHandler *>(publication));
    static_cast<SpViewHandler *>(publication)->Release();
  }
  cursor_map->SetDefaultColor(0, 35);
  u32 cursor;
  GetCursor(&cursor);
  window->SetActiveCursor(cursor);
  return result;
}

// The unused host input retains its actual application-window type.
// Stop active scrolling and detach the handler only when the original request byte is set.
bool EcZoneToolBase::DetachIfRequested(SpCityWindowView *) {
  if (!detach_after_input)
    return false;
  if (mouse_scroll) {
    mouse_scroll = 0;
    window->SetMouseScroll(0, 0, false);
  }
  if (edge_scroll) {
    edge_scroll = 0;
    window->SetEdgeScroll(false);
  }
  return window->SetActiveHandler(nullptr);
}

EcZoneAreaTool::EcZoneAreaTool()
    : active(0), drag{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
                     {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      extra{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
          {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}} {
}

EcZoneAreaTool::~EcZoneAreaTool() {
}

// Begin an area gesture at the picked city coordinate and activate edge scrolling.
bool EcZoneAreaTool::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  if (view->PerformPick(x, y, &picked, false)) {
    active = 1;
    edge_scroll = 1;
    window->SetEdgeScroll(true);
    if (bounds.CoordInBoundsXZ(picked)) {
      drag.minimum = picked;
      invalid = insufficient_funds = field58 = 0;
      BeginAt(&picked);
      i32 value = cost;
      insufficient_funds = value && i64(value) > budget->GetTotalFunds();
      if (!mouse_scroll)
        window->SetCostDisplay(true, value, insufficient_funds != 0);
    }
  }
  return true;
}

// Commit a valid dragged area before clearing edge scrolling, the cursor map and the displayed cost.
bool EcZoneAreaTool::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &picked, false)) {
      active = 0;
      if (bounds.CoordInBoundsXZ(drag.minimum) && bounds.CoordInBoundsXZ(picked))
        drag.maximum = picked;
    }
    if (!mouse_scroll && bounds.CoordInBoundsXZ(drag.minimum) && bounds.CoordInBoundsXZ(drag.maximum)) {
      if (cursor_visible)
        PaintCursor(&drag, &affected);
      invalid = insufficient_funds = field58 = 0;
      Commit(&drag);
      i32 value = cost;
      insufficient_funds = value && i64(value) > budget->GetTotalFunds();
      if (!mouse_scroll)
        window->SetCostDisplay(true, value, insufficient_funds != 0);
    }
  }
  edge_scroll = 0;
  window->SetEdgeScroll(false);
  cursor_map->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  drag.maximum.x = -1;
  drag.maximum.z = -1;
  active = 0;
  DetachIfRequested(EcRciCityWindow());
  return true;
}

// Update scrolling and preview the picked cell or dragged area, preserving the cursor and input-flush order.
bool EcZoneAreaTool::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord picked{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GeBounds affected{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
      {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
  if (!cursor_visible) {
    window->SetCellCursorVisible(true);
    cursor_visible = 1;
  }
  if (active && !mouse_scroll) {
    if (!edge_scroll) {
      edge_scroll = 1;
      window->SetEdgeScroll(true);
    }
  } else if (edge_scroll) {
    edge_scroll = 0;
    window->SetEdgeScroll(false);
  }
  if (mouse_scroll || edge_scroll)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &picked, false) && bounds.CoordInBoundsXZ(picked)) {
    if (active) {
      if (cursor_map->InBounds(drag.minimum) && cursor_map->InBounds(picked) && !mouse_scroll) {
        drag.maximum = picked;
        invalid = insufficient_funds = field58 = 0;
        PreviewDrag(&drag);
        i32 value = cost;
        insufficient_funds = value && i64(value) > budget->GetTotalFunds();
        if (!mouse_scroll)
          window->SetCostDisplay(true, value, insufficient_funds != 0);
        if (cursor_visible)
          PaintCursor(&drag, &affected);
        windows->FlushInputMessageQueue();
      }
    } else if (cursor_map->InBounds(picked) && !mouse_scroll) {
      drag.maximum = picked;
      drag.minimum = picked;
      invalid = insufficient_funds = field58 = 0;
      PreviewHover(&picked);
      i32 value = cost;
      insufficient_funds = value && i64(value) > budget->GetTotalFunds();
      if (!mouse_scroll)
        window->SetCostDisplay(true, value, insufficient_funds != 0);
      if (cursor_visible)
        PaintCursor(&drag, &affected);
    }
  }
  return true;
}

// Cancel active displays, clear the cost and invalidate both horizontal endpoints of the stored drag region.
void EcZoneAreaTool::OnResetState() {
  EcZoneToolBase::OnResetState();
  drag.maximum.z = kCtUnknownCoordinate;
  drag.maximum.x = kCtUnknownCoordinate;
  drag.minimum.z = kCtUnknownCoordinate;
  drag.minimum.x = kCtUnknownCoordinate;
}

u8 EcZoneAreaTool::BeginAt(const CtCoord *) {
  return 1;
}

u8 EcZoneAreaTool::Commit(const GeBounds *) {
  return 1;
}

u8 EcZoneAreaTool::PreviewDrag(const GeBounds *) {
  return 1;
}

u8 EcZoneAreaTool::PreviewHover(const CtCoord *) {
  return 1;
}

u8 EcZoneAreaTool::PaintCursor(const GeBounds *area, GeBounds *) {
  if (cursor_visible)
    cursor_map->SetRect(*area, 35);
  return 1;
}

// Recheck zone placement and funds, place while the city is paused, and issue the corresponding BoxX event.
u8 EcZoneTool::Commit(const GeBounds *area) {
  i32 value = 0;
  if (!zones->CanZone(zone_type, *area, &value))
    invalid = 1;
  cost = value;
  i64 funds = budget->GetTotalFunds();
  insufficient_funds = cost && i64(cost) > funds;
  u32 sound;
  if (invalid) {
    sound = 30;
  } else if (insufficient_funds) {
    sound = 32;
  } else {
    CtCity *owner = EcGetRciCity();
    if (owner)
      owner->SimHiddenPause();
    zones->PlaceZone(zone_type, *area, &value, false);
    if (owner)
      owner->SimHiddenUnpause();
    sound = success_sound;
  }
  void *publication = nullptr;
  if (EcGetRciFramework()->Application()->QueryInterface(IID_ApGameInterface, &publication)) {
    ApGameInterface *application = static_cast<ApGameInterface *>(publication);
    ApBoxX *audio = application->GetBoxX();
    application->Release();
    audio->Event(kAuEventPlayEffect, sound, 0, 0, 0);
  }
  return 1;
}

u8 EcZoneTool::PreviewDrag(const GeBounds *area) {
  i32 value = 0;
  if (!zones->CanZone(zone_type, *area, &value))
    invalid = 1;
  cost = value;
  i64 funds = budget->GetTotalFunds();
  insufficient_funds = cost && i64(cost) > funds;
  return 1;
}

u8 EcZoneTool::PreviewHover(const CtCoord *point) {
  GeBounds area{*point, *point};
  i32 value = 0;
  if (!zones->CanZone(zone_type, area, &value))
    invalid = 1;
  cost = value;
  i64 funds = budget->GetTotalFunds();
  insufficient_funds = cost && i64(cost) > funds;
  return 1;
}

// Paint the area with the selected color, or the error color when the current preview cannot be used.
u8 EcZoneTool::PaintCursor(const GeBounds *area, GeBounds *) {
  if (!mouse_scroll)
    cursor_map->SetRect(*area, invalid || insufficient_funds || field58 ? 36 : cursor_color);
  return 1;
}

EcZoneTool::EcZoneTool(u32 cursor, i32 zone, u16 color, u16 sound)
    : zones(nullptr), cursor_id(cursor), zone_type(zone), cursor_color(color), success_sound(sound) {
}

EcZoneTool::~EcZoneTool() {
  ScRelease(&zones);
}

bool EcZoneTool::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpViewHandler, SpViewHandler);
  return EcRciCommand::QueryInterface(iid, out);
}

u32 EcZoneTool::AddRef() {
  return EcRciCommand::AddRef();
}

u32 EcZoneTool::Release() {
  return EcRciCommand::Release();
}

bool EcZoneTool::Init() {
  cursor_map->SetDefaultColor(0, cursor_color);
  return true;
}

bool EcZoneTool::GetCursor(u32 *out) {
  *out = cursor_id;
  return true;
}

bool EcZoneTool::IsValidHandlerByView(i32 view_kind, u32 flags) const {
  return view_kind == 0 && (flags & 2) != 0;
}

// Retain the live zoning layer, post the original view-mode message, and attach the handler to the city window.
bool EcZoneTool::Execute() {
  zones = ScAddRefNotNull(EcGetRciCity()->ZoneLayer());
  SpCityWindowView *host = EcRciCityWindow();
  SpViewInterface *current = host ? host->GetCurrentView() : nullptr;
  ScMessage message(kWnMsg_SetCityViewParams, 0, current->GetCityViewFlags() | 2u, 1);
  EcGetRciMessageServer()->MessagePost(message, false);
  bool result = AttachHost(EcRciCityWindow());
  if (result)
    cursor_map->SetDefaultColor(0, cursor_color);
  return result;
}
