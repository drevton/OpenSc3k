#define _CRT_SECURE_NO_WARNINGS
#include "economy/ec_landfill_developer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "application/api.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_module.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_building_data.h"
#include "geometry/ge_types.h"
#include "geometry/grid.h"
#include "network/nw_rules.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"

// Original pending landfill coordinates are stored in insertion order and may contain duplicates.
struct EcLandfillCell {
  EcLandfillCell *next;
  u32 x, z;
};

// Encode a landfill occupant's low-word resource instance, sprite rotation and position relative to the center.
static u8 EcEncodeLandfillOccupant(NwRuleRecord *record, const ScPointXZ &center, ScOccupant *occupant) {
  record->piece = 0;
  record->state = 0;
  record->direction = 31;
  u32 piece = occupant->GetAttribData()->Key()->instance;
  u8 rotation;
  occupant->GetSpriteInst()->GetRotationOffset(&rotation);
  record->piece = u16(piece);
  record->state = rotation;
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  if (x != center.x || z != center.z)
    NwRuleDirection(i8(z - center.z), i8(x - center.x), &record->direction);
  return 1;
}

// Clears each owned tiling rule's groups before the file collections are retired.
static void EcClearLandfillRules(NwRuleCollection *collection) {
  for (u32 file_index = 0; file_index < collection->files.size; ++file_index) {
    NwRuleTable *table = &collection->files.data[file_index]->rules;
    for (NwRule *rule = table->begin; rule != table->end; ++rule) {
      delete[] rule->begin;
      rule->begin = rule->end = rule->storageEnd = nullptr;
      rule->selector = 0;
    }
  }
}

bool EcLandfillDeveloper::tuning_loaded = false;
f32 EcLandfillDeveloper::decay_rate = 0.001f;

// Initializes the developer and loads its module-wide landfill-decay rate once.
EcLandfillDeveloper::EcLandfillDeveloper()
    : initialized(0), random(kScRandomSeedFromClock), city(nullptr), attributes(nullptr), agent_tree(nullptr),
      valve(nullptr), buildings(nullptr), occupants(nullptr), zones(nullptr), pollution(nullptr), traffic(nullptr),
      maximum_x(0), maximum_z(0), cells(nullptr), grid(nullptr), rules{} {
  if (!tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("LandfillZoneDeveloper"), key("LandfillDecayRate");
    if (ini.GetValue(&section, &key, &value)) {
      u32 rate = EcParseTuningNumber(value.Data());
      if (rate > 10000)
        rate = 10000;
      decay_rate = f32(f64(rate) * f64(0.0001f));
    }
    tuning_loaded = true;
  }
}

// Shuts down initialized city services before retiring its stored cell records.
EcLandfillDeveloper::~EcLandfillDeveloper() {
  if (initialized)
    Shutdown();
  while (cells) {
    EcLandfillCell *cell = cells;
    cells = cell->next;
    delete cell;
  }
}

// Publish the original primary developer or separate landfill-decay view and retain the shared owner.
bool EcLandfillDeveloper::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_ScUnknown || iid == IID_CtZoneDeveloper)
    *out = static_cast<CtZoneDeveloper *>(this);
  else if (iid == IID_CtLandfillDecay)
    *out = static_cast<CtLandfillDecay *>(this);
  if (!*out)
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

u32 EcLandfillDeveloper::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcLandfillDeveloper::Release() {
  return EcCommandOwner::Release();
}

// Load the seven original rule collections, acquire city services and allocate the packed landfill scratch grid.
u8 EcLandfillDeveloper::Init(CtCity *owner, const char *) {
  ScString directory;
  RsFileSystem *filesystem = EcGetRciFileSystem();
  if (filesystem) {
    const char *data = filesystem->DataDirectory();
    if (data)
      directory.FromChar(data);
  }
  directory.Append("Res");
  if (!owner) {
    Shutdown();
    return 0;
  }
  if (initialized)
    Shutdown();
  for (u32 index = 0; index < 6; ++index) {
    char suffix[64];
    snprintf(suffix, sizeof(suffix), "\\TilingRules\\LandfillRules%u.txt", index + 1);
    ScString path(directory);
    path.Append(suffix);
    rules[index] = new NwRuleCollection;
    if (!rules[index]->AddFile(path)) {
      for (u32 i = 0; i <= index; ++i) {
        delete rules[i];
        rules[i] = nullptr;
      }
      return 0;
    }
  }
  ScString path(directory);
  path.Append("\\TilingRules\\LandfillStartRule.txt");
  rules[6] = new NwRuleCollection;
  if (!rules[6]->AddFile(path)) {
    for (u32 index = 0; index < 7; ++index) {
      delete rules[index];
      rules[index] = nullptr;
    }
    return 0;
  }
  city = owner;
  attributes = EcGetRciAttribCache();
  if (!attributes) {
    Shutdown();
    return 0;
  }
  agent_tree = EcGetRciAgentTree();
  if (!agent_tree) {
    Shutdown();
    return 0;
  }
  buildings = city->BuildingLayer();
  if (!buildings) {
    Shutdown();
    return 0;
  }
  CtLayer *layer = city->GetSpecificLayer(kEcPollutionLayerType);
  if (!layer->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution))) {
    Shutdown();
    return 0;
  }
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    Shutdown();
    return 0;
  }
  maximum_x = occupants->GetCellsInX() - 1;
  maximum_z = occupants->GetCellsInZ() - 1;
  zones = city->ZoneLayer();
  if (!zones) {
    Shutdown();
    return 0;
  }
  traffic = city->TrafficLayer();
  if (!traffic) {
    Shutdown();
    return 0;
  }
  u8 fill = 0;
  // The installed call uses city CellCountY for the second grid dimension, despite subsequent Z-indexed accesses.
  u32 rows = city->CellCountY(), columns = city->CellCountX();
  grid = new EcLandfillGrid(columns, rows, &fill);
  initialized = 1;
  return 1;
}

// Acquire the landfill demand valve from the city's valve layer, preserving the installed success release order.
bool EcLandfillDeveloper::BindDemand() {
  CtLayer *layer = city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  EcValveLayer *demand = nullptr;
  if (!layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand)))
    return false;
  if (!demand->GetValvePointer(kEcDisposalDemandValveId, IID_EcValve, reinterpret_cast<void **>(&valve)))
    return false;
  demand->Release();
  return true;
}

// Clear each tiling-rule collection, release retained services and retire the owned bit grid in original order.
u8 EcLandfillDeveloper::Shutdown() {
  initialized = 0;
  for (u32 index = 0; index < 7; ++index) {
    if (rules[index]) {
      EcClearLandfillRules(rules[index]);
      delete rules[index];
      rules[index] = nullptr;
    }
  }
  city = nullptr;
  attributes = nullptr;
  agent_tree = nullptr;
  buildings = nullptr;
  occupants = nullptr;
  zones = nullptr;
  if (pollution)
    pollution->Release();
  pollution = nullptr;
  if (valve)
    valve->Release();
  valve = nullptr;
  traffic = nullptr;
  if (grid)
    delete grid;
  grid = nullptr;
  return 1;
}

u8 EcLandfillDeveloper::AddRule(EcZoneRule *) {
  return 0;
}

u8 EcLandfillDeveloper::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 EcLandfillDeveloper::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

f32 EcLandfillDeveloper::GetDecayRate() {
  return decay_rate;
}

// Decay a landfill cell, remove an emptied occupant and account for active-landfill demand in the original order.
void EcLandfillDeveloper::DecayCell(const ScPointXZ &point) {
  u32 garbage;
  pollution->GetGarbageValue(point.x, point.z, &garbage);
  if (!garbage)
    return;
  u32 decayed = u32(nearbyint(f64(garbage) * decay_rate));
  if (!decayed)
    decayed = 1;
  garbage -= decayed;
  pollution->SetGarbageValue(point.x, point.z, garbage);
  if (!garbage) {
    occupants->RemoveOccupantAtCell(point.x, point.z);
    EcLandfillCell **entry = &cells;
    while (*entry) {
      EcLandfillCell *cell = *entry;
      if (cell->x == point.x && cell->z == point.z) {
        *entry = cell->next;
        delete cell;
      } else {
        entry = &cell->next;
      }
    }
  }
  if (pollution->IsActiveLandfill(point.x, point.z))
    valve->AddToDemandValue(i32(decayed));
}

// Return the unused portion of the original 32767-unit capacity for the selected landfill cell.
i32 EcLandfillDeveloper::AvailableCapacity(const ScPointXZ &point) {
  u32 garbage;
  pollution->GetGarbageValue(point.x, point.z, &garbage);
  return garbage >= 32767 ? 0 : i32(32767 - garbage);
}

// Change the active flag only when road access changes, and adjust demand by the cell's available capacity.
void EcLandfillDeveloper::SetActiveCapacity(const ScPointXZ &point, i16 capacity, u8 road_access) {
  bool active = pollution->IsActiveLandfill(point.x, point.z);
  if (road_access) {
    if (active)
      return;
    pollution->SetIsActiveLandfill(point.x, point.z, true);
    valve->AddToDemandValue(capacity);
  } else {
    if (!active)
      return;
    pollution->SetIsActiveLandfill(point.x, point.z, false);
    valve->AddToDemandValue(-i32(capacity));
  }
}

// Record the midpoint of a buildable landfill region and place its center when permitted.
u8 EcLandfillDeveloper::PlaceCenter(const ScPointXZ &point) {
  GeCellRect bounds;
  zones->FindBuildableRect(&bounds, point.x, point.z, 10, 0, kCtAgentLandfill, true);
  if (bounds.right == bounds.left || bounds.bottom == bounds.top)
    return 0;
  ScPointXZ center{bounds.left + ((bounds.right - bounds.left) >> 1), bounds.top + ((bounds.bottom - bounds.top) >> 1)};
  EcLandfillCell **tail = &cells;
  while (*tail)
    tail = &(*tail)->next;
  *tail = new EcLandfillCell{nullptr, center.x, center.z};
  ScOccupant *existing = nullptr;
  bool found = occupants->GetOccupantAtCell(center.x, center.z, &existing);
  if (!found || existing->IsGeomFlag(1024)) {
    RsKey key{kRsBuildingDataResourceType, kRsBuildingDataGroup, 10006};
    if (zones->PlaceBuilding(key, center, 1, 0, true)) {
      ScOccupant *created = nullptr;
      if (occupants->GetOccupantAtCell(center.x, center.z, &created)) {
        created->SetUserRemove(false);
        created->Release();
      }
    }
  }
  if (existing)
    existing->Release();
  return 1;
}

// Mark the connected landfill-zoned component in the original east, west, south, north recursion order.
// The packed grid must cover the visited word; the original reads it directly before testing the zone.
void EcLandfillDeveloper::MarkZone(u32 x, u32 z) {
  assert(x < grid->columns && (z >> 5) < grid->words_per_column);
  if (grid->data[x][z >> 5] & (1u << (z & 31)))
    return;
  u8 zone;
  zones->GetValue(x, z, &zone);
  if (zone != 17)
    return;
  u8 value = 1;
  grid->Set(x, z, &value);
  if (x < zones->CellCountX() - 1)
    MarkZone(x + 1, z);
  if (x)
    MarkZone(x - 1, z);
  if (z < zones->CellCountZ() - 1)
    MarkZone(x, z + 1);
  if (z)
    MarkZone(x, z - 1);
}

// Search marked landfill cells in expanding clockwise square rings, returning the first active cell with capacity.
u8 EcLandfillDeveloper::FindGrowableNeighbor(const ScPointXZ &center, ScPointXZ *result) {
  assert(center.x < grid->columns && (center.z >> 5) < grid->words_per_column);
  if (!(grid->data[center.x][center.z >> 5] & (1u << (center.z & 31))))
    return 0;
  if (pollution->IsActiveLandfill(center.x, center.z) && AvailableCapacity(center) > 0) {
    *result = center;
    return 1;
  }
  i32 limit_x = i32(zones->CellCountX() - 1), limit_z = i32(zones->CellCountZ() - 1);
  i32 center_x = i32(center.x), center_z = i32(center.z), marked;
  u8 radius = 0;
  auto examine = [&](i32 x, i32 z) {
    if (x < 0 || x > limit_x || z < 0 || z > limit_z)
      return false;
    assert(u32(x) < grid->columns && (u32(z) >> 5) < grid->words_per_column);
    if (!(grid->data[x][u32(z) >> 5] & (1u << (u32(z) & 31))))
      return false;
    if (pollution->IsActiveLandfill(u32(x), u32(z))) {
      *result = {u32(x), u32(z)};
      if (AvailableCapacity(*result) > 0)
        return true;
    }
    ++marked;
    return false;
  };
  do {
    marked = 0;
    ++radius;
    i32 r = radius;
    for (i32 z = center_z - r + 1; z <= center_z + r; ++z)
      if (examine(center_x - r, z))
        return 1;
    for (i32 x = center_x - r + 1; x <= center_x + r; ++x)
      if (examine(x, center_z + r))
        return 1;
    for (i32 z = center_z + r - 1; z >= center_z - r; --z)
      if (examine(center_x + r, z))
        return 1;
    for (i32 x = center_x + r - 1; x >= center_x - r; --x)
      if (examine(x, center_z - r))
        return 1;
  } while (marked && radius < limit_x);
  return 0;
}

u8 EcLandfillDeveloper::CheckRoadAccess(const ScPointXZ &point, i32 *reason) {
  *reason = 0;
  if (!zones->IsNearTransport(point, 10) ||
      !traffic->RouteCellTrip(point.x, point.z, 3, i32(traffic->GetMaxDimension()), 0, 1, 0, 0)) {
    *reason = 16;
    return 0;
  }
  auto connected = [&](u32 x, u32 z) {
    u8 zone;
    zones->GetValue(x, z, &zone);
    if (zone != 17)
      return false;
    return zones->IsNearTransport(ScPointXZ{x, z}, 10);
  };
  bool west = false, east = false, north = false, south = false;
  if (point.x)
    west = connected(point.x - 1, point.z);
  if (point.x < zones->CellCountX() - 1)
    east = connected(point.x + 1, point.z);
  if (west || east) {
    if (point.z)
      north = connected(point.x, point.z - 1);
    if (point.z < zones->CellCountZ() - 1)
      south = connected(point.x, point.z + 1);
    if (north || south) {
      if (west && north && connected(point.x - 1, point.z - 1))
        return 1;
      if (east && north && connected(point.x + 1, point.z - 1))
        return 1;
      if (west && south && connected(point.x - 1, point.z + 1))
        return 1;
      if (east && south && connected(point.x + 1, point.z + 1))
        return 1;
    }
  }
  *reason = 9;
  return 0;
}

// Apply matched landfill tiles in rule order, preserving each offset and rotation.
u8 EcLandfillDeveloper::ApplyTiling(const ScPointXZ &center, const NwRuleRecords &records) {
  for (u32 index = 0; index < records.records.size; ++index) {
    const NwRuleRecord *record = &records.records.data[index];
    i8 dx, dz;
    NwRuleOffset(record->direction, &dx, &dz);
    ScPointXZ point{center.x + u32(i32(dx)), center.z + u32(i32(dz))};
    if (!occupants->IsInBoundsCell(point.x, point.z))
      continue;
    RsKey key{kRsBuildingDataResourceType, kRsBuildingDataGroup, record->piece};
    if (zones->PlaceBuilding(key, point, 1, u16(90 * record->state), true)) {
      ScOccupant *created = nullptr;
      if (occupants->GetOccupantAtCell(point.x, point.z, &created)) {
        created->SetUserRemove(false);
        created->Release();
      }
    }
  }
  return 1;
}

// Capture the five-cell landfill neighborhood as ordered rule observations, releasing each queried occupant.
u8 EcLandfillDeveloper::BuildNeighborhood(NwNeighborhood **out, const ScPointXZ &center) {
  NwNeighborhood *neighborhood = new NwNeighborhood;
  *out = neighborhood;
  u32 first_x = center.x > 1 ? center.x - 2 : 0, first_z = center.z > 1 ? center.z - 2 : 0;
  u32 last_x = center.x >= maximum_x - 1 ? maximum_x : center.x + 2;
  u32 last_z = center.z >= maximum_z - 1 ? maximum_z : center.z + 2;
  for (u32 x = first_x; x <= last_x; ++x) {
    for (u32 z = first_z; z <= last_z; ++z) {
      ScOccupant *occupant = nullptr;
      bool found = occupants->GetOccupantAtCell(x, z, &occupant);
      NwRuleRecord record;
      bool include;
      if (found && occupant->TypeId() == kCtAgentLandfill) {
        include = EcEncodeLandfillOccupant(&record, center, occupant) != 0;
      } else {
        include = !occupant || occupant->IsGeomFlag(1024);
        if (include) {
          record.piece = 0;
          record.state = 0;
          NwRuleDirection(i8(z - center.z), i8(x - center.x), &record.direction);
        }
      }
      if (include)
        *neighborhood->observations.records.Append() = record;
      if (occupant)
        occupant->Release();
    }
  }
  return 1;
}

// The private road-access byte and its traffic calls remain unvalidated.
// Decay and fill active landfill cells, then sort and apply the corresponding installed tiling rules.
// Newly placed landfill markers disable user removal rather than simulation removal.
i32 EcLandfillDeveloper::Evaluate(const ScPointXZ &point) {
  EcLandfillCell *pending = nullptr, **pending_tail = &pending;
  auto append = [&](const ScPointXZ &value) {
    *pending_tail = new EcLandfillCell{nullptr, value.x, value.z};
    pending_tail = &(*pending_tail)->next;
  };
  i32 reason = 0;
  u8 road_access = CheckRoadAccess(point, &reason);
  DecayCell(point);
  append(point);
  SetActiveCapacity(point, i16(AvailableCapacity(point)), road_access);
  i32 supply = valve->QuerySupplyValue();
  if (supply > 0) {
    bool center_placed = false;
    if (!cells && road_access) {
      center_placed = true;
      PlaceCenter(point);
    }
    for (EcLandfillCell *cell = cells; cell && supply > 0; cell = cell->next) {
      ScPointXZ center{cell->x, cell->z}, current = center;
      bool marked = false, keep_growing = true;
      do {
        if (supply <= 0)
          break;
        if (pollution->IsActiveLandfill(current.x, current.z)) {
          i32 capacity = AvailableCapacity(current);
          if (capacity > 0) {
            i32 amount = capacity > supply ? supply : capacity;
            u32 garbage;
            pollution->GetGarbageValue(current.x, current.z, &garbage);
            garbage = u16(amount + garbage);
            pollution->SetGarbageValue(current.x, current.z, garbage);
            valve->AddToSupplyValue(-amount);
            valve->AddToDemandValue(-amount);
            pollution->GarbagePutInLandfill(u32(amount));
            supply = valve->QuerySupplyValue();
            append(current);
            ScOccupant *existing = nullptr;
            if (!occupants->GetOccupantAtCell(current.x, current.z, &existing) || existing->IsGeomFlag(1024)) {
              RsKey key{kRsBuildingDataResourceType, kRsBuildingDataGroup, 10006};
              if (zones->PlaceBuilding(key, current, 1, 0, true)) {
                ScOccupant *created = nullptr;
                if (occupants->GetOccupantAtCell(current.x, current.z, &created)) {
                  created->SetUserRemove(false);
                  created->Release();
                }
              }
            }
            if (existing)
              existing->Release();
          }
        }
        if (supply > 0) {
          if (!marked) {
            u8 clear = 0;
            grid->Fill(&clear);
            MarkZone(center.x, center.z);
            marked = true;
          }
          keep_growing = FindGrowableNeighbor(center, &current) != 0;
        } else {
          keep_growing = false;
        }
      } while (keep_growing);
      if (!cell->next && supply > 0 && !center_placed && road_access) {
        center_placed = true;
        PlaceCenter(point);
      }
    }
  }
  EcLandfillCell *sorted = nullptr;
  while (pending) {
    EcLandfillCell *cell = pending;
    pending = cell->next;
    EcLandfillCell **slot = &sorted;
    while (*slot && ((*slot)->x < cell->x || ((*slot)->x == cell->x && (*slot)->z <= cell->z)))
      slot = &(*slot)->next;
    cell->next = *slot;
    *slot = cell;
  }
  while (sorted) {
    EcLandfillCell *cell = sorted;
    sorted = cell->next;
    ScPointXZ center{cell->x, cell->z};
    NwNeighborhood *neighborhood = nullptr;
    BuildNeighborhood(&neighborhood, center);
    NwRuleRecords records{};
    u8 matched = 0;
    for (u32 index = 0; index < 6; ++index) {
      matched = rules[index]->Match(neighborhood, &records);
      if (matched)
        break;
    }
    if (!matched || !records.records.size)
      matched = rules[6]->Match(neighborhood, &records);
    if (matched && records.records.size)
      ApplyTiling(center, records);
    delete neighborhood;
    delete cell;
  }
  return reason;
}

// Each original group stores a land-use identifier and four weighted lists of borrowed building descriptions.
struct EcMixedLotGroup {
  EcMixedLotGroup *next;
  u32 identifier;
  ScArray<GeBuildingType *> lists[4];
};

bool EcMixedLotRule::tuning_loaded = false;
u32 EcMixedLotRule::minimum_size = 6;
u32 EcMixedLotRule::maximum_size = 18;
u32 EcMixedLotRule::air_limit = 3;
u32 EcMixedLotRule::water_limit = 3;

// Initialize the real rule and load its once-per-module size and pollution settings. The four tuning conversions must
// supply the values subsequently consumed by the original constructor.
EcMixedLotRule::EcMixedLotRule() : groups(nullptr), pollution(nullptr) {
  if (!tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), key;
    key.Sprintf("0x%08X", kEcMixedLotRuleClassId);
    if (ini.GetValue(&section, &key, &value)) {
      i32 first, second, air, water;
      [[maybe_unused]] i32 fields = sscanf(value.Data(), "%d,%d,%d,%d", &first, &second, &air, &water);
      assert(fields == 4);
      minimum_size = u32(first);
      maximum_size = u32(second);
      if (minimum_size < 4)
        minimum_size = 4;
      if (maximum_size < minimum_size)
        maximum_size = minimum_size;
      if (maximum_size > 22)
        maximum_size = 22;
      if (minimum_size > 22)
        minimum_size = 22;
      air_limit = u32(air) > 32767 ? 32767 : u32(air);
      water_limit = u32(water) > 32767 ? 32767 : u32(water);
    }
    tuning_loaded = true;
  }
}

// Destroys the owned groups without releasing borrowed building descriptions.
EcMixedLotRule::~EcMixedLotRule() {
  while (groups) {
    EcMixedLotGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Borrow the building and zoning services, then acquire the pollution layer's retained interface.
bool EcMixedLotRule::Init(CtCity *city) {
  buildings = city->BuildingLayer();
  if (!buildings)
    return false;
  zones = city->ZoneLayer();
  if (!zones)
    return false;
  CtLayer *layer = city->GetSpecificLayer(kEcPollutionLayerType);
  return layer->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution));
}

// Clear borrowed services and release the acquired pollution view.
u8 EcMixedLotRule::Shutdown() {
  buildings = nullptr;
  zones = nullptr;
  ScRelease(&pollution);
  return 1;
}

// Collect weighted large, medium, small and shoreline descriptions and retain complete four-list groups.
void EcMixedLotRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcMixedLotGroup *group = new EcMixedLotGroup{};
    group->identifier = source->GetLandUse();
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), list;
      if (flags & 0x100) {
        if (building->SizeInCellsX() != 1)
          continue;
        list = 3;
      } else if (building->SizeInCellsX() > 2) {
        list = 0;
      } else {
        list = building->SizeInCellsX() > 1 ? 1 : 2;
      }
      for (i32 weight = i32(entries[index].weight); weight > 0; --weight)
        *group->lists[list].Append() = building;
    }
    if (group->lists[0].size && group->lists[1].size && group->lists[2].size && group->lists[3].size) {
      EcMixedLotGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

// Check neighboring zones, pollution and road frontage before placing large lots and randomized small infill.
u8 EcMixedLotRule::Place(const GeCellRect &rectangle, u32 identifier, i32, i32) {
  EcMixedLotGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right, bottom = rectangle.bottom;
  i32 width = i32(right - left + 1), height = i32(bottom - top + 1);
  if (width < i32(minimum_size) || height < i32(minimum_size))
    return 0;
  auto acceptable = [&](u32 x, u32 z) {
    u8 zone;
    zones->GetValue(x, z, &zone);
    return zone < 2 || zone == 5 || zone == 22 || zone == 9;
  };
  if (top) {
    for (u32 x = left; x <= right; ++x)
      if (!acceptable(x, top - 1))
        return 0;
  }
  if (left) {
    for (u32 z = top; z <= bottom; ++z)
      if (!acceptable(left - 1, z))
        return 0;
  }
  if (right < zones->CellCountX() - 1) {
    for (u32 z = top; z <= bottom; ++z)
      if (!acceptable(right + 1, z))
        return 0;
  }
  if (bottom < zones->CellCountZ() - 1) {
    for (u32 x = left; x <= right; ++x)
      if (!acceptable(x, bottom + 1))
        return 0;
  }
  u32 road[4] = {};
  for (u32 edge = 0; edge < 4; ++edge) {
    u32 first = (edge == 0 || edge == 2) ? left + 1 : top + 1;
    u32 end = (edge == 0 || edge == 2) ? right : bottom;
    for (u32 position = first; position < end; ++position) {
      ScPointXZ point = edge == 0 ? ScPointXZ{position, top} : edge == 1 ? ScPointXZ{right, position} :
          edge == 2 ? ScPointXZ{position, bottom} : ScPointXZ{left, position};
      u32 value;
      pollution->GetAirValue(point.x, point.z, &value);
      if (value > air_limit)
        return 0;
      pollution->GetWaterValue(point.x, point.z, &value);
      if (value > water_limit)
        return 0;
      if (zones->IsNearTransport(point, 0))
        ++road[edge];
    }
  }
  u32 road_edges = (road[0] != 0) + (road[1] != 0) + (road[2] != 0) + (road[3] != 0);
  if (!road_edges || road_edges > 2)
    return 0;
  if (width > i32(maximum_size)) {
    if (road[1])
      left = right - maximum_size + 1;
    else
      right = left + maximum_size - 1;
    width = i32(maximum_size);
  }
  if (height > i32(maximum_size)) {
    if (road[0])
      bottom = top + maximum_size - 1;
    else
      top = bottom - maximum_size + 1;
    height = i32(maximum_size);
  }
  u32 direction = 5, most = 0;
  for (u32 edge = 0; edge < 4; ++edge) {
    if (road[edge] > most) {
      most = road[edge];
      direction = edge;
    }
  }
  u8 occupied[22][22] = {};
  i32 lot_width, lot_height;
  if (direction == 0 || direction == 2) {
    lot_width = width / 2;
    lot_height = height / 3;
    if (lot_width < 3)
      lot_width = 3;
    if (lot_width > 8)
      lot_width = 8;
    if (lot_height < 3)
      lot_height = 3;
    if (lot_height > 4)
      lot_height = 4;
  } else {
    lot_width = width / 3;
    lot_height = height / 2;
    if (lot_width < 3)
      lot_width = 3;
    if (lot_width > 4)
      lot_width = 4;
    if (lot_height < 3)
      lot_height = 3;
    if (lot_height > 8)
      lot_height = 8;
  }
  u32 x0, z0;
  if (direction == 0 || direction == 2) {
    x0 = left + random.Bounded(u32(width - lot_width + 1));
    z0 = direction == 0 ? top : bottom - lot_height + 1;
  } else {
    x0 = direction == 1 ? right - lot_width + 1 : left;
    z0 = top + random.Bounded(u32(height - lot_height + 1));
  }
  u32 x1 = x0 + lot_width - 1, z1 = z0 + lot_height - 1;
  auto choose = [&](u32 list) {
    ScArray<GeBuildingType *> *choices = &group->lists[list];
    return choices->data[random.Bounded(choices->size)];
  };
  auto mark = [&](const ScPointXZ &point, u32 size) {
    for (u32 x = 0; x < size; ++x)
      for (u32 z = 0; z < size; ++z)
        occupied[point.x - left + x][point.z - top + z] = 1;
  };
  GeBuildingType *building = choose(0);
  u32 size = building->SizeInCellsX();
  ScPointXZ point{x0 + random.Bounded(u32(lot_width - size + 1)),
      z0 + random.Bounded(u32(lot_height - size + 1))};
  if (zones->PlaceBuilding(building, point, 4, 0, true))
    mark(point, size);
  u32 placed = 0;
  for (u32 attempt = 0; attempt < 5 && placed < 2; ++attempt) {
    building = choose(1);
    size = building->SizeInCellsX();
    point = {x0 + random.Bounded(u32(lot_width - size + 1)), z0 + random.Bounded(u32(lot_height - size + 1))};
    bool overlap = false;
    for (u32 x = 0; x < size && !overlap; ++x)
      for (u32 z = 0; z < size && !overlap; ++z)
        overlap = occupied[point.x - left + x][point.z - top + z] != 0;
    if (!overlap && zones->PlaceBuilding(building, point, 4, 0, true)) {
      ++placed;
      mark(point, size);
    }
  }
  for (u32 x = x0; x <= x1; ++x) {
    for (u32 z = z0; z <= z1; ++z) {
      if (occupied[x - left][z - top])
        continue;
      building = choose(2);
      point = {x, z};
      if (zones->PlaceBuilding(building, point, 4, 0, true))
        occupied[x - left][z - top] = 1;
    }
  }
  u16 rotation = random.Bounded(100) > 50 ? 0 : 90;
  building = choose(3);
  u32 x = left, z = top;
  for (;;) {
    if (!occupied[x - left][z - top]) {
      point = {x, z};
      zones->PlaceBuilding(building, point, 1, rotation, true);
      occupied[x - left][z - top] = 1;
      if (random.Bounded(100) < 30)
        building = choose(3);
    } else {
      building = choose(3);
    }
    if (rotation) {
      if (++x > right) {
        x = left;
        if (++z > bottom)
          break;
      }
    } else if (++z > bottom) {
      z = top;
      if (++x > right)
        break;
    }
  }
  return 1;
}

// Create the industrial port developer registered with the RCI director.
ScUnknown *EcCreateRciClass10036878() {
  return static_cast<CtZoneDeveloper *>(new EcIndustrialPortDeveloper);
}

// Create the commercial port developer registered with the RCI director.
ScUnknown *EcCreateRciClass100368AA() {
  return static_cast<CtZoneDeveloper *>(new EcCommercialPortDeveloper);
}

// Initialize the industrial port developer with empty rules and no borrowed or retained services.
EcIndustrialPortDeveloper::EcIndustrialPortDeveloper()
    : initialized(0), random(kScRandomSeedFromClock), city(nullptr), attributes(nullptr), agent_tree(nullptr),
      valve(nullptr), buildings(nullptr), power(nullptr), occupants(nullptr), zones(nullptr), port(nullptr),
      water(nullptr), pollution(nullptr), traffic(nullptr) {
}

EcIndustrialPortDeveloper::~EcIndustrialPortDeveloper() {
  if (initialized)
    Shutdown();
}

// Publish and retain the industrial developer identity for the original supported interface identifiers.
bool EcIndustrialPortDeveloper::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtZoneDeveloper);
  HANDLE_QUERY_INTERFACE(IID_CtZoneDeveloper, CtZoneDeveloper);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcIndustrialPortDeveloper::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcIndustrialPortDeveloper::Release() {
  return EcCommandOwner::Release();
}

// Borrow city services in original order and acquire the required retained port-map identity.
u8 EcIndustrialPortDeveloper::Init(CtCity *owner, const char *) {
  if (!owner) {
    Shutdown();
    return 0;
  }
  if (initialized)
    Shutdown();
  city = owner;
  attributes = EcGetRciApplication()->GetAppOccupantAttribCache();
  if (!attributes) {
    Shutdown();
    return 0;
  }
  agent_tree = EcGetRciApplication()->GetAppAgentTypeTree();
  if (!agent_tree) {
    Shutdown();
    return 0;
  }
  buildings = city->BuildingLayer();
  if (!buildings) {
    Shutdown();
    return 0;
  }
  power = city->PowerLayer();
  if (!power) {
    Shutdown();
    return 0;
  }
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    Shutdown();
    return 0;
  }
  zones = city->ZoneLayer();
  if (!zones) {
    Shutdown();
    return 0;
  }
  CtLayer *layer = city->GetSpecificLayer(kGePortLayerType);
  if (!layer) {
    Shutdown();
    return 0;
  }
  layer->QueryInterface(IID_GePortMap, reinterpret_cast<void **>(&port));
  if (!port) {
    Shutdown();
    return 0;
  }
  pollution = city->PollutionLayer();
  water = city->PlumbingLayer();
  traffic = city->TrafficLayer();
  initialized = 1;
  return 1;
}

// Clear borrowed services and release retained port, valve and rule references after initialization.
u8 EcIndustrialPortDeveloper::Shutdown() {
  if (initialized) {
    initialized = 0;
    city = nullptr;
    attributes = nullptr;
    agent_tree = nullptr;
    buildings = nullptr;
    power = nullptr;
    occupants = nullptr;
    zones = nullptr;
    water = nullptr;
    pollution = nullptr;
    traffic = nullptr;
    ScRelease(&port);
    ScRelease(&valve);
    while (rules.size) {
      EcZoneRule *rule = rules.data[rules.size - 1];
      rule->Shutdown();
      rule->Release();
      --rules.size;
    }
  }
  return 1;
}

bool EcIndustrialPortDeveloper::BindDemand() {
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  EcValveLayer *demand = nullptr;
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand));
  if (!demand)
    return false;
  bool result = demand->GetValvePointer(kEcIndustrialConnectionsValveId, IID_EcValve,
      reinterpret_cast<void **>(&valve));
  demand->Release();
  return result;
}

// Try up to five randomly selected rules with industrial port identifier 12330 and the current valve maximum.
u8 EcIndustrialPortDeveloper::TryRules(const GeCellRect &rectangle) {
  ScArray<u32> candidates;
  candidates.Reserve(rules.size);
  valve->QueryValveValue();
  i32 maximum = valve->QueryValveMax();
  u32 attempts = rules.size > 5 ? 5 : rules.size;
  for (u32 index = 0; index < rules.size; ++index)
    *candidates.Append() = index;
  u8 placed = 0;
  while (attempts-- && !placed) {
    u32 position = random.Bounded(candidates.size);
    EcZoneRule *rule = rules.data[candidates.data[position]];
    if (rule)
      placed = rule->Place(rectangle, kCtAgentSeaport, maximum, maximum);
    for (u32 index = position + 1; index < candidates.size; ++index)
      candidates.data[index - 1] = candidates.data[index];
    --candidates.size;
  }
  return placed;
}

u8 EcIndustrialPortDeveloper::ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state) {
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

// Evaluate industrial port services and existing occupancy, or try a development rule on a suitable empty cell.
i32 EcIndustrialPortDeveloper::Evaluate(const ScPointXZ &point) {
  bool near_transport = zones->IsNearTransport(point, 6);
  u8 watered = water->IsWatered(point.x, point.z);
  bool radioactive = pollution->IsRadioactive(point.x, point.z), powered = false;
  power->GetValue(point.x, point.z, &powered);
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(point.x, point.z, &occupant)) {
    if (!occupant)
      return 0;
    if (occupant->IsGeomFlag(256)) {
      u32 x, z;
      occupant->GetLocationInCellCoords(&x, &z);
      bool removable = occupant->CanSimRemove();
      BdBuilding *building = nullptr;
      i32 result = 0;
      if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
        if (point.x != x || point.z != z) {
          building->Release();
          occupant->Release();
          return 0;
        }
        building->GetBaseAttribData();
        u32 flags = 0;
        if (building->GetOccupancyStateFlag(kGeBuildingHistorical)) {
          removable = false;
          flags = kGeBuildingHistorical;
        }
        bool adjacent = port->HasAdjacency(x, z) != 0;
        u32 state = building->GetOccupancyState();
        if (state == kGeBuildingAbandoned || state == kGeBuildingUnderConstruction) {
          if (watered && powered && near_transport && adjacent && !radioactive)
            ReplaceBuilding(building, point, flags | kGeBuildingOccupied);
          else if (removable)
            buildings->RemoveBuilding(building);
        } else if (state == kGeBuildingOccupied && !adjacent) {
          ReplaceBuilding(building, point, flags | kGeBuildingAbandoned);
          result = powered ? watered ? 6 : 5 : 4;
        }
        building->Release();
      }
      occupant->Release();
      return result;
    }
  }
  if (occupant)
    occupant->Release();
  if (radioactive)
    return 10;
  if (watered && powered && near_transport) {
    GeCellRect rectangle{};
    zones->FindBuildableRect(&rectangle, point.x, point.z, 6, 0, 0, false);
    return TryRules(rectangle) ? 0 : 17;
  }
  if (!powered)
    return 14;
  if (!near_transport)
    return 16;
  if (!watered)
    return 15;
  return 0;
}

// Retain the port rule and set the original six-cell transport-distance limit.
u8 EcIndustrialPortDeveloper::AddRule(EcZoneRule *rule) {
  if (!rule)
    return 0;
  rule->AddRef();
  rule->SetMaxDistance(6);
  *rules.Append() = rule;
  return 1;
}

u8 EcIndustrialPortDeveloper::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 EcIndustrialPortDeveloper::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

// Initialize the commercial port developer with no rules or acquired services.
EcCommercialPortDeveloper::EcCommercialPortDeveloper()
    : initialized(0), random(kScRandomSeedFromClock), city(nullptr), attributes(nullptr), agent_tree(nullptr),
      valve(nullptr), occupants(nullptr), buildings(nullptr), power(nullptr), zones(nullptr), land_value(nullptr),
      port(nullptr), water(nullptr), pollution(nullptr), traffic(nullptr) {
}

EcCommercialPortDeveloper::~EcCommercialPortDeveloper() {
  if (initialized)
    Shutdown();
}

// Publish and retain the commercial developer identity for the original supported interface identifiers.
bool EcCommercialPortDeveloper::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtZoneDeveloper);
  HANDLE_QUERY_INTERFACE(IID_CtZoneDeveloper, CtZoneDeveloper);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcCommercialPortDeveloper::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcCommercialPortDeveloper::Release() {
  return EcCommandOwner::Release();
}

// Borrow the city's commercial port services and retain its land-value and port-map interfaces.
u8 EcCommercialPortDeveloper::Init(CtCity *owner, const char *) {
  if (!owner)
    return 0;
  if (initialized)
    Shutdown();
  city = owner;
  attributes = EcGetRciAttribCache();
  if (!attributes)
    return 0;
  agent_tree = EcGetRciAgentTree();
  if (!agent_tree)
    return 0;
  buildings = city->BuildingLayer();
  if (!buildings)
    return 0;
  power = city->PowerLayer();
  if (!power)
    return 0;
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return 0;
  zones = city->ZoneLayer();
  if (!zones)
    return 0;
  pollution = city->PollutionLayer();
  water = city->PlumbingLayer();
  traffic = city->TrafficLayer();
  land_value = ScAddRefNotNull(city->LandValueLayer());
  CtLayer *layer = city->GetSpecificLayer(kGePortLayerType);
  if (layer)
    layer->QueryInterface(IID_GePortMap, reinterpret_cast<void **>(&port));
  if (!layer || !port) {
    land_value->Release();
    return 0;
  }
  initialized = 1;
  return 1;
}

// Clear borrowed services, release retained land value, port and valve, then retire registered rules in reverse.
u8 EcCommercialPortDeveloper::Shutdown() {
  if (initialized) {
    initialized = 0;
    city = nullptr;
    attributes = nullptr;
    agent_tree = nullptr;
    buildings = nullptr;
    power = nullptr;
    occupants = nullptr;
    zones = nullptr;
    land_value->Release();
    land_value = nullptr;
    pollution = nullptr;
    water = nullptr;
    traffic = nullptr;
    ScRelease(&port);
    ScRelease(&valve);
    while (rules.size) {
      EcZoneRule *rule = rules.data[rules.size - 1];
      rule->Shutdown();
      rule->Release();
      --rules.size;
    }
  }
  return 1;
}

bool EcCommercialPortDeveloper::BindDemand() {
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  EcValveLayer *demand = nullptr;
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand));
  if (!demand)
    return false;
  bool result = demand->GetValvePointer(kEcCommercialConnectionsValveId, IID_EcValve,
      reinterpret_cast<void **>(&valve));
  demand->Release();
  return result;
}

// Try at most five rules in random order using commercial port growth identifier 12310.
u8 EcCommercialPortDeveloper::TryRules(const GeCellRect &rectangle, i32 demand, i32 maximum) {
  ScArray<u32> candidates;
  candidates.Reserve(rules.size);
  for (u32 index = 0; index < rules.size; ++index)
    *candidates.Append() = index;
  u32 attempts = rules.size > 5 ? 5 : rules.size;
  u8 placed = 0;
  while (attempts-- && !placed) {
    u32 position = random.Bounded(candidates.size);
    EcZoneRule *rule = rules.data[candidates.data[position]];
    if (rule)
      placed = rule->Place(rectangle, 12310, demand, maximum);
    for (u32 index = position + 1; index < candidates.size; ++index)
      candidates.data[index - 1] = candidates.data[index];
    --candidates.size;
  }
  return placed;
}

u8 EcCommercialPortDeveloper::ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state) {
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

// Evaluate commercial port services and existing occupancy, then attempt suitable empty-cell development.
// The existing-building branch requires its real base attributes, which the original immediately queries.
i32 EcCommercialPortDeveloper::Evaluate(const ScPointXZ &point) {
  bool near_transport = zones->IsNearTransport(point, 6);
  u8 watered = water->IsWatered(point.x, point.z);
  bool radioactive = pollution->IsRadioactive(point.x, point.z), powered = false;
  power->GetValue(point.x, point.z, &powered);
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(point.x, point.z, &occupant)) {
    if (!occupant)
      return 0;
    if (occupant->IsGeomFlag(256)) {
      u32 x, z;
      occupant->GetLocationInCellCoords(&x, &z);
      BdBuilding *building = nullptr;
      if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
        if (point.x == x && point.z == z) {
          GeAttrib *base = building->GetBaseAttribData();
          assert(base);
          GePortType *port_type = nullptr;
          if (base->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&port_type))) {
            port_type->GetPortType();
            port_type->Release();
          }
          u32 flags = building->GetOccupancyStateFlag(kGeBuildingHistorical) ? kGeBuildingHistorical : 0u;
          u32 state = building->GetOccupancyState();
          if ((state == kGeBuildingAbandoned || state == kGeBuildingUnderConstruction) && watered && powered &&
              near_transport && !radioactive)
            ReplaceBuilding(building, point, flags | kGeBuildingOccupied);
        }
        building->Release();
      }
      occupant->Release();
      return 0;
    }
  }
  if (occupant)
    occupant->Release();
  if (radioactive)
    return 10;
  if (watered && powered && near_transport) {
    GeCellRect rectangle{};
    zones->FindBuildableRect(&rectangle, point.x, point.z, 6, 0, 0, false);
    return TryRules(rectangle, 2000, 2000) ? 0 : 9;
  }
  if (!powered)
    return 14;
  if (!near_transport)
    return 16;
  if (!watered)
    return 15;
  return 0;
}

// Retain the port rule and configure its original six-cell transport limit.
u8 EcCommercialPortDeveloper::AddRule(EcZoneRule *rule) {
  if (!rule)
    return 0;
  rule->AddRef();
  rule->SetMaxDistance(6);
  *rules.Append() = rule;
  return 1;
}

u8 EcCommercialPortDeveloper::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 EcCommercialPortDeveloper::SerialWrite(CtCity *, RsStream *) {
  return 1;
}
