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

// The installed road-port rule stores six weighted lists of borrowed extended port descriptors per group.
struct EcPortRoadGroup {
  EcPortRoadGroup *next;
  ScArray<GePortType *> types[6];
};

// Original port-building groups hold five port descriptor lists and one general building list.
struct EcPortBuildingGroup {
  EcPortBuildingGroup *next;
  ScArray<GePortType *> ports[5];
  ScArray<GeBuildingType *> buildings;
};

static bool EcBlockTuningLoaded;
static u32 EcBlockMinimum = 2, EcBlockMaximum = 7, EcBlockDemandThreshold = 5, EcBlockShoreChance = 33;
static bool EcTransportBlockTuningLoaded;
static u32 EcTransportBlockMinimum = 2, EcTransportBlockMaximum = 7;
static u32 EcTransportBlockDemandThreshold = 5, EcTransportBlockShoreChance = 33;
static bool EcPortBlockTuningLoaded;
static u32 EcPortBlockMinimum = 2, EcPortBlockMaximum = 7, EcPortBlockShoreChance = 53;
static u32 EcPortBlockDemandThreshold = 5;
static bool EcLotTuningLoaded;
static u32 EcLotMinimum = 2, EcLotMaximum = 7, EcLotDemandThreshold = 5, EcLotShoreChance = 33;

EcBlockRule::EcBlockRule() : groups(nullptr) {
  if (!EcBlockTuningLoaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), key;
    key.Sprintf("0x%08X", kEcBlockRuleClassId);
    if (ini.GetValue(&section, &key, &value)) {
      ScString field = value.SplitToken(",");
      EcBlockMinimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      EcBlockMaximum = EcParseTuningNumber(field.Data());
      if (EcBlockMaximum > 10)
        EcBlockMaximum = 10;
      field = value.SplitToken(",");
      EcBlockDemandThreshold = EcParseTuningNumber(field.Data());
      EcBlockShoreChance = EcParseTuningNumber(value.Data());
    }
    EcBlockTuningLoaded = true;
  }
}

// Free weighted groups without releasing their borrowed building descriptors.
static void EcFreeBlockGroups(EcBlockGroup *groups) {
  while (groups) {
    EcBlockGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

EcBlockRule::~EcBlockRule() {
  EcFreeBlockGroups(groups);
}

EcRuleBase::EcRuleBase() : random(kScRandomSeedFromClock), max_distance(0), buildings(nullptr), zones(nullptr) {
}

EcRuleBase::~EcRuleBase() {
}

// Publish the separate Unknown or development-rule identity and retain the shared object.
bool EcRuleBase::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcRuleUnknown);
  HANDLE_QUERY_INTERFACE(IID_EcZoneRule, EcZoneRule);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcRuleBase::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcRuleBase::Release() {
  return EcCommandOwner::Release();
}

// Borrow the city's real building and zoning layers in order, retaining neither interface.
bool EcRuleBase::Init(CtCity *city) {
  buildings = city->BuildingLayer();
  if (!buildings)
    return false;
  zones = city->ZoneLayer();
  return zones != nullptr;
}

// Clear both borrowed city-layer pointers without releasing them.
u8 EcRuleBase::Shutdown() {
  buildings = nullptr;
  zones = nullptr;
  return 1;
}

u32 EcRuleBase::SetMaxDistance(u32 distance) {
  max_distance = distance;
  return distance;
}

// Copy an installed configuration into weighted dry and shoreline descriptor groups.
static void EcAddBlockConfig(EcBlockGroup **groups, GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcBlockGroup *group = new EcBlockGroup{};
    group->identifier = source->GetLandUse();
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags();
      i32 weight = i32(entries[index].weight);
      while (weight > 0) {
        if (flags & 0x100)
          *group->shoreline.Append() = building;
        else
          *group->dry.Append() = building;
        --weight;
      }
    }
    if (group->dry.size && group->shoreline.size) {
      EcBlockGroup **tail = groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

void EcBlockRule::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

u8 EcBlockRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceBlock(groups, &random, zones, max_distance, false, EcBlockMinimum, EcBlockMaximum,
      EcBlockDemandThreshold, EcBlockShoreChance, rectangle, identifier, demand, maximum);
}

// The private constructor declaration is not documented in the SDK.
// Initialize the transport-aware rule and load its independently configured block dimensions and chances.
EcTransportBlockRule::EcTransportBlockRule() : groups(nullptr) {
  if (!EcTransportBlockTuningLoaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), key;
    key.Sprintf("0x%08X", kEcTransportBlockRuleClassId);
    if (ini.GetValue(&section, &key, &value)) {
      ScString field = value.SplitToken(",");
      EcTransportBlockMinimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      EcTransportBlockMaximum = EcParseTuningNumber(field.Data());
      if (EcTransportBlockMaximum > 10)
        EcTransportBlockMaximum = 10;
      field = value.SplitToken(",");
      EcTransportBlockDemandThreshold = EcParseTuningNumber(field.Data());
      EcTransportBlockShoreChance = EcParseTuningNumber(value.Data());
    }
    EcTransportBlockTuningLoaded = true;
  }
}

EcTransportBlockRule::~EcTransportBlockRule() {
  EcFreeBlockGroups(groups);
}

void EcTransportBlockRule::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

u8 EcTransportBlockRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceBlock(groups, &random, zones, max_distance, true, EcTransportBlockMinimum, EcTransportBlockMaximum,
      EcTransportBlockDemandThreshold, EcTransportBlockShoreChance, rectangle, identifier, demand, maximum);
}

// The private constructor declaration is not documented in the SDK.
// Initialize the port rule and load its independent dimensions and weighted-building tuning.
EcPortBlockRule::EcPortBlockRule() : groups(nullptr), port(nullptr) {
  if (!EcPortBlockTuningLoaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), key;
    key.Sprintf("0x%08X", kEcPortBlockRuleClassId);
    if (ini.GetValue(&section, &key, &value)) {
      ScString field = value.SplitToken(",");
      EcPortBlockMinimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      EcPortBlockMaximum = EcParseTuningNumber(field.Data());
      if (EcPortBlockMaximum > 10)
        EcPortBlockMaximum = 10;
      field = value.SplitToken(",");
      EcPortBlockDemandThreshold = EcParseTuningNumber(field.Data());
      EcPortBlockShoreChance = EcParseTuningNumber(value.Data());
    }
    EcPortBlockTuningLoaded = true;
  }
}

EcPortBlockRule::~EcPortBlockRule() {
  EcFreeBlockGroups(groups);
}

// Publish only the port rule's development-rule identity and retain its owner.
bool EcPortBlockRule::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_EcZoneRule, EcZoneRule);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Borrow the city layers and acquire the port map through its original city-layer identity.
bool EcPortBlockRule::Init(CtCity *city) {
  buildings = city->BuildingLayer();
  if (buildings) {
    zones = city->ZoneLayer();
    if (zones) {
      CtLayer *layer = city->GetSpecificLayer(kGePortLayerType);
      if (layer) {
        layer->QueryInterface(IID_GePortMap, reinterpret_cast<void **>(&port));
        if (port)
          return true;
      }
    }
  }
  Shutdown();
  return false;
}

// Clear borrowed layers and release the acquired port-map interface.
u8 EcPortBlockRule::Shutdown() {
  buildings = nullptr;
  zones = nullptr;
  if (port)
    port->Release();
  port = nullptr;
  return 1;
}

void EcPortBlockRule::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

// Place occupied port buildings on cells marked by the port map and served by transport.
u8 EcPortBlockRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32) {
  if (demand < 1)
    return 0;
  EcBlockGroup *group = groups;
  while (group && group->identifier != identifier)
    group = group->next;
  if (!group)
    return 0;
  GeCellRect area = rectangle;
  i32 width = i32(area.right - area.left + 1u), height = i32(area.bottom - area.top + 1u);
  if (width < i32(EcPortBlockMinimum) || height < i32(EcPortBlockMinimum))
    return 0;
  if (width > i32(EcPortBlockMaximum)) {
    width = i32(EcPortBlockMaximum);
    area.right = area.left + EcPortBlockMaximum - 1u;
  }
  if (height > i32(EcPortBlockMaximum)) {
    height = i32(EcPortBlockMaximum);
    area.bottom = area.top + EcPortBlockMaximum - 1u;
  }
  u8 used[100]{};
  bool found = false;
  for (u32 x = 0; x < EcPortBlockMaximum; ++x) {
    for (u32 z = 0; z < EcPortBlockMaximum; ++z)
      if (port->HasAdjacency(area.left + x, area.top + z))
        found = true;
      else
        used[10u * x + z] = 1;
  }
  if (!found)
    return 0;
  for (u32 x = area.left; x <= area.right; ++x) {
    for (u32 z = area.top; z <= area.bottom; ++z) {
      u32 offset_x = x - area.left, offset_z = z - area.top;
      u8 *cell = &used[10u * offset_x + offset_z];
      ScPointXZ point{x, z};
      if (*cell || !zones->IsNearTransport(point, max_distance)) {
        *cell = 1;
        continue;
      }
      GeBuildingType *building;
      if (random.Bounded(100) >= EcPortBlockShoreChance)
        building = group->shoreline.data[random.Bounded(group->shoreline.size)];
      else
        building = group->dry.data[random.Bounded(group->dry.size)];
      u32 size = building->SizeInCellsX();
      if (size + offset_x - 1u < u32(width) && size + offset_z - 1u < u32(height) &&
          zones->PlaceBuilding(building, point, 1, 0, true) && size) {
        for (u32 row = 0; row < size; ++row)
          for (u32 column = 0; column < size; ++column)
            used[10u * (offset_x + row) + offset_z + column] = 1;
      }
    }
  }
  return 1;
}

// The private constructor declaration is not documented in the SDK.
// Initialize the earlier lot rule and load its independent dimensions and weighting parameters.
EcLotRule::EcLotRule() : groups(nullptr) {
  if (!EcLotTuningLoaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), key;
    key.Sprintf("0x%08X", kEcLotRuleClassId);
    if (ini.GetValue(&section, &key, &value)) {
      ScString field = value.SplitToken(",");
      EcLotMinimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      EcLotMaximum = EcParseTuningNumber(field.Data());
      if (EcLotMaximum > 10)
        EcLotMaximum = 10;
      field = value.SplitToken(",");
      EcLotDemandThreshold = EcParseTuningNumber(field.Data());
      EcLotShoreChance = EcParseTuningNumber(value.Data());
    }
    EcLotTuningLoaded = true;
  }
}

// Building descriptors are borrowed.
// Release the lot rule's owned groups and weighted pointer arrays.
EcLotRule::~EcLotRule() {
  while (groups) {
    EcLotGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Sort weighted building descriptors into small, large and shoreline lot groups.
void EcLotRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcLotGroup *group = new EcLotGroup{};
    group->identifier = source->GetLandUse();
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      i32 weight = i32(entries[index].weight);
      while (weight > 0) {
        if (flags & 0x100) {
          *group->shoreline.Append() = building;
        } else if (i32(size) < 3) {
          *group->small.Append() = building;
        } else {
          *group->small.Append() = building;
          *group->large.Append() = building;
        }
        --weight;
      }
    }
    if (group->small.size && group->shoreline.size) {
      EcLotGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

u8 EcLotRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceCornerLots(groups, &random, zones, EcLotMinimum, EcLotMaximum, EcLotDemandThreshold, EcLotShoreChance,
      rectangle, identifier, demand, maximum);
}

static bool commercial_tuning_loaded;
static u32 commercial_minimum = 3, commercial_maximum = 7, commercial_threshold = 5, commercial_chance = 33;

// Its private constructor declaration is not documented in the SDK.
// Initialize the original rule and load its independent block dimensions and weighted-building tuning.
EcCommercialBlockRule::EcCommercialBlockRule() : groups(nullptr) {
  if (!commercial_tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), row;
    row.Sprintf("0x%08X", kEcCommercialBlockRuleClassId);
    if (ini.GetValue(&section, &row, &value)) {
      ScString field = value.SplitToken(",");
      commercial_minimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      commercial_maximum = EcParseTuningNumber(field.Data());
      if (commercial_maximum > 10)
        commercial_maximum = 10;
      field = value.SplitToken(",");
      commercial_threshold = EcParseTuningNumber(field.Data());
      commercial_chance = EcParseTuningNumber(value.Data());
    }
    commercial_tuning_loaded = true;
  }
}

EcCommercialBlockRule::~EcCommercialBlockRule() {
  EcFreeBlockGroups(groups);
}

void EcCommercialBlockRule::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

static bool industrial_tuning_loaded;
static u32 industrial_minimum = 2, industrial_maximum = 7, industrial_threshold = 5, industrial_chance = 33;

// Its private constructor declaration is not documented in the SDK.
// Initialize the original rule and load its independent block dimensions and weighted-building tuning.
EcIndustrialBlockRule::EcIndustrialBlockRule() : groups(nullptr) {
  if (!industrial_tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), row;
    row.Sprintf("0x%08X", kEcIndustrialBlockRuleClassId);
    if (ini.GetValue(&section, &row, &value)) {
      ScString field = value.SplitToken(",");
      industrial_minimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      industrial_maximum = EcParseTuningNumber(field.Data());
      if (industrial_maximum > 10)
        industrial_maximum = 10;
      field = value.SplitToken(",");
      industrial_threshold = EcParseTuningNumber(field.Data());
      industrial_chance = EcParseTuningNumber(value.Data());
    }
    industrial_tuning_loaded = true;
  }
}

EcIndustrialBlockRule::~EcIndustrialBlockRule() {
  EcFreeBlockGroups(groups);
}

void EcIndustrialBlockRule::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

static bool industrial2_tuning_loaded;
static u32 industrial2_minimum = 2, industrial2_maximum = 7, industrial2_threshold = 5, industrial2_chance = 15;

// Its private constructor declaration is not documented in the SDK.
// Initialize the original rule and load its independent block dimensions and weighted-building tuning.
EcIndustrialBlockRule2::EcIndustrialBlockRule2() : groups(nullptr) {
  if (!industrial2_tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), row;
    row.Sprintf("0x%08X", kEcIndustrialBlockRule2ClassId);
    if (ini.GetValue(&section, &row, &value)) {
      ScString field = value.SplitToken(",");
      industrial2_minimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      industrial2_maximum = EcParseTuningNumber(field.Data());
      if (industrial2_maximum > 10)
        industrial2_maximum = 10;
      field = value.SplitToken(",");
      industrial2_threshold = EcParseTuningNumber(field.Data());
      industrial2_chance = EcParseTuningNumber(value.Data());
    }
    industrial2_tuning_loaded = true;
  }
}

EcIndustrialBlockRule2::~EcIndustrialBlockRule2() {
  EcFreeBlockGroups(groups);
}

void EcIndustrialBlockRule2::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

static bool industrial3_tuning_loaded;
static u32 industrial3_minimum = 2, industrial3_maximum = 7, industrial3_threshold = 5, industrial3_chance = 33;

// Its private constructor declaration is not documented in the SDK.
// Initialize the original rule and load its independent block dimensions and weighted-building tuning.
EcIndustrialBlockRule3::EcIndustrialBlockRule3() : groups(nullptr) {
  if (!industrial3_tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), row;
    row.Sprintf("0x%08X", kEcIndustrialBlockRule3ClassId);
    if (ini.GetValue(&section, &row, &value)) {
      ScString field = value.SplitToken(",");
      industrial3_minimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      industrial3_maximum = EcParseTuningNumber(field.Data());
      if (industrial3_maximum > 10)
        industrial3_maximum = 10;
      field = value.SplitToken(",");
      industrial3_threshold = EcParseTuningNumber(field.Data());
      industrial3_chance = EcParseTuningNumber(value.Data());
    }
    industrial3_tuning_loaded = true;
  }
}

EcIndustrialBlockRule3::~EcIndustrialBlockRule3() {
  EcFreeBlockGroups(groups);
}

void EcIndustrialBlockRule3::AddConfig(GeBuildingGroupView *source) {
  EcAddBlockConfig(&groups, source);
}

u8 EcCommercialBlockRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceBlock(groups, &random, zones, max_distance, true, commercial_minimum, commercial_maximum,
      commercial_threshold, commercial_chance, rectangle, identifier, demand, maximum);
}

u8 EcIndustrialBlockRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceBlock(groups, &random, zones, max_distance, true, industrial_minimum, industrial_maximum,
      industrial_threshold, industrial_chance, rectangle, identifier, demand, maximum);
}

u8 EcIndustrialBlockRule2::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceBlock(groups, &random, zones, max_distance, true, industrial2_minimum, industrial2_maximum,
      industrial2_threshold, industrial2_chance, rectangle, identifier, demand, maximum);
}

u8 EcIndustrialBlockRule3::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceBlock(groups, &random, zones, max_distance, true, industrial3_minimum, industrial3_maximum,
      industrial3_threshold, industrial3_chance, rectangle, identifier, demand, maximum);
}

// Free the lot groups and their pointer arrays while leaving the borrowed building descriptors alive.
static void EcFreeLotGroups(EcLotGroup *groups) {
  while (groups) {
    EcLotGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Build the three commercial weighted lists, publishing only entries containing all three categories.
static void EcAddCommercialLotConfig(EcLotGroup **groups, GeBuildingGroupView *source, bool single_cell) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcLotGroup *group = new EcLotGroup{};
    group->identifier = source->GetLandUse();
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      i32 weight = i32(entries[index].weight);
      while (weight > 0) {
        if (flags & 0x100)
          *group->shoreline.Append() = building;
        else if (single_cell ? size == 1 : i32(size) < 3)
          *group->small.Append() = building;
        else
          *group->large.Append() = building;
        --weight;
      }
    }
    if (group->small.size && group->large.size && group->shoreline.size) {
      EcLotGroup **tail = groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

static bool commercial2_tuning_loaded;
static u32 commercial2_minimum = 2, commercial2_maximum = 7, commercial2_threshold = 5, commercial2_chance = 75;

// Load the second commercial rule's independent block and weighting parameters.
EcCommercialLotRule2::EcCommercialLotRule2() : groups(nullptr) {
  if (!commercial2_tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), row;
    row.Sprintf("0x%08X", kEcCommercialLotRule2ClassId);
    if (ini.GetValue(&section, &row, &value)) {
      ScString field = value.SplitToken(",");
      commercial2_minimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      commercial2_maximum = EcParseTuningNumber(field.Data());
      if (commercial2_maximum > 10)
        commercial2_maximum = 10;
      field = value.SplitToken(",");
      commercial2_threshold = EcParseTuningNumber(field.Data());
      commercial2_chance = EcParseTuningNumber(value.Data());
    }
    commercial2_tuning_loaded = true;
  }
}

EcCommercialLotRule2::~EcCommercialLotRule2() {
  EcFreeLotGroups(groups);
}

void EcCommercialLotRule2::AddConfig(GeBuildingGroupView *source) {
  EcAddCommercialLotConfig(&groups, source, true);
}

u8 EcCommercialLotRule2::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceCommercialLots(groups, &random, zones, commercial2_minimum, commercial2_maximum, commercial2_threshold,
      commercial2_chance, rectangle, identifier, demand, maximum);
}

static bool commercial3_tuning_loaded;
static u32 commercial3_minimum = 2, commercial3_maximum = 6, commercial3_threshold = 5, commercial3_chance = 50;

// Load the third commercial rule's independent block and weighting parameters.
EcCommercialLotRule3::EcCommercialLotRule3() : groups(nullptr) {
  if (!commercial3_tuning_loaded) {
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ZoneDeveloperRules"), row;
    row.Sprintf("0x%08X", kEcCommercialLotRule3ClassId);
    if (ini.GetValue(&section, &row, &value)) {
      ScString field = value.SplitToken(",");
      commercial3_minimum = EcParseTuningNumber(field.Data());
      field = value.SplitToken(",");
      commercial3_maximum = EcParseTuningNumber(field.Data());
      if (commercial3_maximum > 10)
        commercial3_maximum = 10;
      field = value.SplitToken(",");
      commercial3_threshold = EcParseTuningNumber(field.Data());
      commercial3_chance = EcParseTuningNumber(value.Data());
    }
    commercial3_tuning_loaded = true;
  }
}

EcCommercialLotRule3::~EcCommercialLotRule3() {
  EcFreeLotGroups(groups);
}

void EcCommercialLotRule3::AddConfig(GeBuildingGroupView *source) {
  EcAddCommercialLotConfig(&groups, source, false);
}

u8 EcCommercialLotRule3::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  return EcPlaceCornerLots(groups, &random, zones, commercial3_minimum, commercial3_maximum, commercial3_threshold,
      commercial3_chance, rectangle, identifier, demand, maximum);
}

EcPortRoadRule::EcPortRoadRule() : groups(nullptr), occupants(nullptr), dirt(nullptr), port(nullptr) {
}

// Port descriptors in the groups are borrowed.
// Retire each owned group and its six weighted arrays before destroying the common rule owner.
EcPortRoadRule::~EcPortRoadRule() {
  while (groups) {
    EcPortRoadGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

// Expose the road-port rule identity and retain the shared owner for a successful query.
bool EcPortRoadRule::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_EcZoneRule, EcZoneRule);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Borrow the city's occupant, zoning and terrain services and acquire its port-map identity.
bool EcPortRoadRule::Init(CtCity *city) {
  if (!city)
    return false;
  occupants = city->SurfaceOccupantManager();
  if (occupants) {
    zones = city->ZoneLayer();
    if (zones) {
      CtLayer *layer = city->GetSpecificLayer(kGePortLayerType);
      if (layer) {
        layer->QueryInterface(IID_GePortMap, reinterpret_cast<void **>(&port));
        if (port) {
          dirt = city->DirtBag();
          return true;
        }
      }
    }
  }
  Shutdown();
  return false;
}

// Clear borrowed services and release the acquired port-map interface when present.
u8 EcPortRoadRule::Shutdown() {
  occupants = nullptr;
  zones = nullptr;
  dirt = nullptr;
  ScRelease(&port);
  return 1;
}

// Build the six weighted port-type lists and retain a group only when the original required lists are populated.
void EcPortRoadRule::AddConfig(GeBuildingGroupView *source) {
  if (!source)
    return;
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcPortRoadGroup *group = new EcPortRoadGroup{};
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      GePortType *type = nullptr;
      if (building->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&type))) {
        u32 kind = type->GetPortType();
        i32 first = kind == 7 ? 0 : kind == 8 ? 2 : kind == 9 ? 4 : -1;
        i32 weight = i32(entries[index].weight);
        if (first >= 0 && size == 1 && (flags & 0x200) && weight > 0) {
          if (flags & 0x400) {
            while (weight > 0) {
              *group->types[first].Append() = type;
              --weight;
            }
          } else if (flags & 0x800) {
            while (weight > 0) {
              *group->types[first + 1].Append() = type;
              --weight;
            }
          }
        }
        if (type)
          type->Release();
      }
    }
    if (group->types[4].size && group->types[5].size && (group->types[0].size || group->types[1].size) &&
        group->types[2].size && group->types[3].size) {
      EcPortRoadGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

// Choose the longest five-cell-deep navigable waterfront, then place its weighted road and port pieces in order.
// Placement requires an initialized group with each selected piece list populated by the tuning loader.
u8 EcPortRoadRule::Place(const GeCellRect &rectangle, u32, i32, i32) {
  i32 left = i32(rectangle.left), top = i32(rectangle.top);
  i32 right = i32(rectangle.right), bottom = i32(rectangle.bottom);
  i32 best_x0 = -1, best_z0 = -1, best_x1 = -1, best_z1 = -1;
  u8 best = 0, direction = 0;
  for (u32 edge = 0; edge < 4; ++edge) {
    i32 first = edge < 2 ? left : top, last = edge < 2 ? right : bottom;
    i32 run_x = -1, run_z = -1;
    u8 run = 0;
    for (i32 position = first; position <= last; ++position) {
      i32 x = edge < 2 ? position : edge == 2 ? right : left;
      i32 z = edge < 2 ? (edge == 0 ? top : bottom) : position;
      if (!run) {
        run_x = x;
        run_z = z;
      }
      i32 dx = edge == 2 ? 1 : edge == 3 ? -1 : 0;
      i32 dz = edge == 0 ? -1 : edge == 1 ? 1 : 0;
      bool in_bounds = edge == 0 ? z - 5 >= 0 : edge == 1 ? z + 5 < i32(zones->CellCountZ()) :
          edge == 2 ? x + 5 < i32(zones->CellCountX()) : x - 5 >= 0;
      bool valid = in_bounds;
      if (in_bounds) {
        for (i32 step = 1; step <= 5; ++step) {
          u32 check_x = u32(x + dx * step), check_z = u32(z + dz * step);
          if (!dirt->IsWater(check_x, check_z) || occupants->GetOccupantAtCell(check_x, check_z, nullptr)) {
            valid = false;
            break;
          }
        }
        u32 distance;
        if (!dirt->Distance(u32(x + 5 * dx), u32(z + 5 * dz), &distance))
          valid = false;
      }
      if (!valid) {
        run = 0;
      } else {
        ++run;
        if (run > best) {
          best = run;
          direction = edge == 1 ? 2 : edge == 2 ? 1 : edge == 3 ? 3 : 0;
          best_x0 = run_x;
          best_z0 = run_z;
          best_x1 = x;
          best_z1 = z;
        }
      }
    }
  }
  if (best < 5)
    return 0;
  if (best > 12) {
    best = 12;
    if (best_x0 == best_x1)
      best_z1 = best_z0 + 11;
    else
      best_x1 = best_x0 + 11;
  }
  u32 group_count = 0;
  for (EcPortRoadGroup *entry = groups; entry; entry = entry->next)
    ++group_count;
  assert(group_count);
  u32 selected = random.Bounded(group_count);
  EcPortRoadGroup *group = groups;
  while (selected--)
    group = group->next;
  auto choose = [&](u32 index) {
    ScArray<GePortType *> *choices = &group->types[index];
    assert(choices->size);
    return choices->data[random.Bounded(choices->size)];
  };
  bool horizontal = direction == 0 || direction == 2;
  GePortType *special = choose(horizontal ? 2 : 3);
  GePortType *extension = choose(horizontal ? 5 : 4);
  u32 normal_list = horizontal ? 0 : 1;
  u16 special_rotation = direction == 0 || direction == 3 ? 180 : 0;
  u16 normal_rotation = special_rotation;
  if (!group->types[normal_list].size) {
    normal_list = horizontal ? 1 : 0;
    normal_rotation += 90;
  }
  GePortType *normal = choose(normal_list);
  i32 dx = direction == 1 ? 1 : direction == 3 ? -1 : 0;
  i32 dz = direction == 2 ? 1 : direction == 0 ? -1 : 0;
  u32 gap = 5, special_count = 0, placed = 0;
  bool previous_special = false;
  for (i32 x = best_x0; x <= best_x1; ++x) {
    for (i32 z = best_z0; z <= best_z1; ++z) {
      bool use_special = false;
      if (gap > 4 && random.Bounded(best) >= u32(best - placed + special_count - 1))
        use_special = true;
      GePortType *building = use_special ? special : normal;
      u16 rotation = use_special ? special_rotation : normal_rotation;
      ScPointXZ point{u32(x + dx), u32(z + dz)};
      if (!occupants->GetOccupantAtCell(point.x, point.z, nullptr) &&
          zones->PlaceBuilding(building, point, 1, rotation, false)) {
        if (use_special && !previous_special && port->IncrementAdjacency(point.x, point.z)) {
          for (u32 index = 0; index < 4; ++index) {
            point.x += u32(dx);
            point.z += u32(dz);
            if (!occupants->GetOccupantAtCell(point.x, point.z, nullptr))
              zones->PlaceBuilding(extension, point, 1, rotation, false);
          }
          gap = 0;
          ++special_count;
          previous_special = true;
        } else {
          previous_special = false;
          ++gap;
        }
        ++placed;
      }
    }
  }
  return 0;
}

// Acquire the common port-building services in the original order, leaving earlier fields published on failure.
static bool EcBindPortServices(
    CtCity *city, ScOccupantManager **occupants, GeBuildingLayer **buildings, CtZoneLayer **zones, GePortMap **port) {
  if (!city)
    return false;
  *occupants = city->SurfaceOccupantManager();
  if (!*occupants)
    return false;
  *buildings = city->BuildingLayer();
  if (!*buildings)
    return false;
  *zones = city->ZoneLayer();
  if (!*zones)
    return false;
  CtLayer *layer = city->GetSpecificLayer(kGePortLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GePortMap, reinterpret_cast<void **>(port));
  return *port != nullptr;
}

// Clear the original borrowed services and release the port interface while retaining the stored zone pointer.
static u8 EcReleasePortServices(ScOccupantManager **occupants, GeBuildingLayer **buildings, GePortMap **port) {
  *buildings = nullptr;
  *occupants = nullptr;
  if (*port) {
    (*port)->Release();
    *port = nullptr;
  }
  return 1;
}

EcPortBuildingRule::EcPortBuildingRule() : groups(nullptr), occupants(nullptr), port(nullptr) {
}

// Score an eligible port cell and distinguish a neighboring type-two port building.
u8 EcPortBuildingRule::ScoreNeighbor(u32 x, u32 z, u8 direction) {
  if (!port->HasFlag1(x, z) || port->HasNearbyFlag2(x, z))
    return 0;
  u32 next_x = 0, next_z = 0;
  if (direction == 0) {
    next_x = x - 1;
    next_z = z;
  } else if (direction == 1) {
    next_x = x;
    next_z = z + 1;
  } else if (direction == 2) {
    next_x = x + 1;
    next_z = z;
  } else if (direction == 3) {
    next_x = x;
    next_z = z - 1;
  }
  if (!occupants->IsInBoundsCell(next_x, next_z))
    return 1;
  u8 result = 1;
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(next_x, next_z, &occupant)) {
    BdBuilding *building = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
      GeAttrib *attributes = building->GetBaseAttribData();
      GePortType *type = nullptr;
      if (attributes && attributes->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&type))) {
        if (type->GetPortType() == 2)
          result = 2;
        type->Release();
      }
      building->Release();
    }
    occupant->Release();
  }
  return result;
}

// Check whether an eligible adjacent port cell contains a type-two port building.
u8 EcPortBuildingRule::HasPortNeighbor(u32 x, u32 z, u8 direction) {
  if (!port->HasFlag1(x, z) || !port->HasFlag2(x, z))
    return 0;
  u32 next_x = 0, next_z = 0;
  if (direction == 0) {
    next_x = x - 1;
    next_z = z;
  } else if (direction == 1) {
    next_x = x;
    next_z = z + 1;
  } else if (direction == 2) {
    next_x = x + 1;
    next_z = z;
  } else if (direction == 3) {
    next_x = x;
    next_z = z - 1;
  }
  if (!occupants->IsInBoundsCell(next_x, next_z))
    return 0;
  u8 result = 0;
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(next_x, next_z, &occupant)) {
    BdBuilding *building = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
      GeAttrib *attributes = building->GetBaseAttribData();
      GePortType *type = nullptr;
      if (attributes && attributes->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&type))) {
        if (type->GetPortType() == 2)
          result = 1;
        type->Release();
      }
      building->Release();
    }
    occupant->Release();
  }
  return result;
}

// Classify the neighboring port building as type one, type two, or neither.
u8 EcPortBuildingRule::ClassifyNeighbor(u32 x, u32 z, u8 direction) {
  u32 next_x = 0, next_z = 0;
  if (direction == 0) {
    next_x = x - 1;
    next_z = z;
  } else if (direction == 1) {
    next_x = x;
    next_z = z + 1;
  } else if (direction == 2) {
    next_x = x + 1;
    next_z = z;
  } else if (direction == 3) {
    next_x = x;
    next_z = z - 1;
  }
  if (!occupants->IsInBoundsCell(next_x, next_z))
    return 0;
  u8 result = 0;
  ScOccupant *occupant = nullptr;
  if (occupants->GetOccupantAtCell(next_x, next_z, &occupant)) {
    BdBuilding *building = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
      GeAttrib *attributes = building->GetBaseAttribData();
      GePortType *type = nullptr;
      if (attributes && attributes->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&type))) {
        u32 kind = type->GetPortType();
        if (kind == 1 || kind == 2)
          result = u8(kind);
        type->Release();
      }
      building->Release();
    }
    occupant->Release();
  }
  return result;
}

// Search the rectangle edges for an eligible cell next to a type-two port, then place a type-four building there.
u8 EcPortBuildingRule::PlaceAdjacentPort(const GeCellRect &rectangle) {
  ScPointXZ point{};
  bool found = false;
  for (u32 z = rectangle.top; z <= rectangle.bottom; ++z) {
    if (ScoreNeighbor(rectangle.left, z, 0) >= 2) {
      point = {rectangle.left, z};
      found = true;
      break;
    }
  }
  if (!found) {
    for (u32 x = rectangle.left; x <= rectangle.right; ++x) {
      if (ScoreNeighbor(x, rectangle.bottom, 1) >= 2) {
        point = {x, rectangle.bottom};
        found = true;
        break;
      }
    }
  }
  if (!found) {
    for (u32 z = rectangle.top; z <= rectangle.bottom; ++z) {
      if (ScoreNeighbor(rectangle.right, z, 2) >= 2) {
        point = {rectangle.right, z};
        found = true;
        break;
      }
    }
  }
  if (!found) {
    for (u32 x = rectangle.left; x <= rectangle.right; ++x) {
      if (ScoreNeighbor(x, rectangle.top, 3) >= 2) {
        point = {x, rectangle.top};
        found = true;
        break;
      }
    }
  }
  if (!found)
    return 0;
  u32 count = 0;
  for (EcPortBuildingGroup *group = groups; group; group = group->next)
    ++count;
  u32 index = random.Bounded(count);
  EcPortBuildingGroup *group = groups;
  while (index--)
    group = group->next;
  GePortType *building = group->ports[0].data[random.Bounded(group->ports[0].size)];
  if (!port->SetNearbyFlag2(point.x, point.z))
    return 0;
  if (!zones->PlaceBuilding(building, point, 1, 0, true)) {
    port->ClearNearbyFlag2(point.x, point.z);
    return 0;
  }
  return 1;
}

// Find two neighboring port buildings along an edge and place a matching two-cell port building.
u8 EcPortBuildingRule::PlaceDoublePort(const GeCellRect &rectangle) {
  if (rectangle.right - rectangle.left + 1 < 2 || rectangle.bottom - rectangle.top + 1 < 2)
    return 0;
  ScPointXZ point{};
  u8 chosen = 0;
  bool found = false;
  for (u8 edge = 0; edge < 4 && !found; ++edge) {
    u32 first = (edge == 0 || edge == 2) ? rectangle.top : rectangle.left;
    u32 last = (edge == 0 || edge == 2) ? rectangle.bottom : rectangle.right;
    bool cached = false;
    u8 next = 0;
    for (u32 position = first; edge == 3 ? position <= last : position < last; ++position) {
      u32 x = edge == 0 ? rectangle.left : edge == 2 ? rectangle.right : position;
      u32 z = edge == 1 ? rectangle.bottom : edge == 3 ? rectangle.top : position;
      u8 current;
      if (cached) {
        current = next;
        cached = false;
      } else {
        current = ClassifyNeighbor(x, z, edge);
      }
      if (current) {
        u32 following_x = x + (edge == 1 || edge == 3 ? 1u : 0u);
        u32 following_z = z + (edge == 0 || edge == 2 ? 1u : 0u);
        next = ClassifyNeighbor(following_x, following_z, edge);
        cached = true;
        if (next) {
          point = {x - (edge == 2 ? 1u : 0u), z - (edge == 1 ? 1u : 0u)};
          chosen = edge;
          found = true;
          break;
        }
      }
    }
  }
  if (!found)
    return 0;
  u32 count = 0;
  for (EcPortBuildingGroup *group = groups; group; group = group->next)
    ++count;
  u32 index = random.Bounded(count);
  EcPortBuildingGroup *group = groups;
  while (index--)
    group = group->next;
  u32 kind = chosen == 0 || chosen == 2 ? 4 : 3;
  GePortType *building = group->ports[kind].data[random.Bounded(group->ports[kind].size)];
  return zones->PlaceBuilding(building, point, 1, 0, true);
}

// Choose the edge with most eligible port neighbors and place up to six matching one-cell buildings.
u8 EcPortBuildingRule::PlacePortRow(const GeCellRect &rectangle) {
  u8 matches[4][10]{}, counts[4]{};
  for (u8 edge = 0; edge < 4; ++edge) {
    u32 first = edge == 0 || edge == 2 ? rectangle.top : rectangle.left;
    u32 last = edge == 0 || edge == 2 ? rectangle.bottom : rectangle.right;
    for (u32 position = first; position <= last; ++position) {
      u32 x = edge == 0 ? rectangle.left : edge == 2 ? rectangle.right : position;
      u32 z = edge == 1 ? rectangle.bottom : edge == 3 ? rectangle.top : position;
      u8 value = HasPortNeighbor(x, z, edge);
      matches[edge][position - first] = value;
      counts[edge] += value;
    }
  }
  u8 chosen = 0;
  for (u8 edge = 1; edge < 4; ++edge)
    if (counts[edge] > counts[chosen])
      chosen = edge;
  if (counts[chosen] < 2)
    return 0;
  u32 count = 0;
  for (EcPortBuildingGroup *group = groups; group; group = group->next)
    ++count;
  u32 index = random.Bounded(count);
  EcPortBuildingGroup *group = groups;
  while (index--)
    group = group->next;
  u32 kind = chosen == 0 || chosen == 2 ? 1 : 2;
  GePortType *building = group->ports[kind].data[random.Bounded(group->ports[kind].size)];
  u32 first = chosen == 0 || chosen == 2 ? rectangle.top : rectangle.left;
  u32 last = chosen == 0 || chosen == 2 ? rectangle.bottom : rectangle.right;
  u32 placed = 0;
  for (u32 position = first; position <= last && placed <= 5; ++position) {
    ScPointXZ point{chosen == 0 ? rectangle.left
            : chosen == 2       ? rectangle.right
                                : position,
        chosen == 1       ? rectangle.bottom
            : chosen == 3 ? rectangle.top
                          : position};
    if (matches[chosen][position - first] && zones->PlaceBuilding(building, point, 1, 0, true))
      ++placed;
  }
  return placed != 0;
}

// Try adjacent, paired and edge-row port placements before choosing an eligible random cell.
u8 EcPortBuildingRule::Place(const GeCellRect &rectangle, u32, i32 demand, i32) {
  if (demand <= 0 || !groups)
    return 0;
  GeCellRect area = rectangle;
  if (area.right - area.left + 1 > 10)
    area.right = area.left + 9;
  if (area.bottom - area.top + 1 > 10)
    area.bottom = area.top + 9;
  if (PlaceAdjacentPort(area))
    return 1;
  u8 placed = random.Bounded(100) >= 55 ? PlaceDoublePort(area) : PlacePortRow(area);
  if (placed)
    return 1;
  u32 count = 0;
  for (EcPortBuildingGroup *group = groups; group; group = group->next)
    ++count;
  u32 index = random.Bounded(count);
  EcPortBuildingGroup *group = groups;
  while (index--)
    group = group->next;
  GeBuildingType *building = group->buildings.data[random.Bounded(group->buildings.size)];
  ScPointXZ point{
      area.left + random.Bounded(area.right - area.left + 1), area.top + random.Bounded(area.bottom - area.top + 1)};
  if (!port->HasFlag1(point.x, point.z))
    return 0;
  return zones->PlaceBuilding(building, point, 1, 0, true);
}

// Create the port-building rule and publish its separate original Unknown identity.
ScUnknown *EcCreateRciClass10036C86() {
  return static_cast<EcRuleUnknown *>(new EcPortBuildingRule);
}

// Group descriptors are borrowed.
// Release each port-building group and its owned descriptor arrays.
EcPortBuildingRule::~EcPortBuildingRule() {
  while (groups) {
    EcPortBuildingGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

bool EcPortBuildingRule::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcZoneRule);
  HANDLE_QUERY_INTERFACE(IID_EcZoneRule, EcZoneRule);
  RETURN_QUERY_INTERFACE_FAILURE();
}

bool EcPortBuildingRule::Init(CtCity *city) {
  return EcBindPortServices(city, &occupants, &buildings, &zones, &port);
}

u8 EcPortBuildingRule::Shutdown() {
  return EcReleasePortServices(&occupants, &buildings, &port);
}

// Group one-cell and two-cell port types by direction, along with eligible general buildings.
void EcPortBuildingRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcPortBuildingGroup *group = new EcPortBuildingGroup{};
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      GePortType *type = nullptr;
      u32 kind = 0;
      if (building->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&type)))
        kind = type->GetPortType();
      if (size == 1 && kind == 4)
        *group->ports[0].Append() = type;
      if ((flags & 0x400) && size == 1 && kind == 5)
        *group->ports[2].Append() = type;
      if ((flags & 0x800) && size == 1 && kind == 5)
        *group->ports[1].Append() = type;
      if ((flags & 0x400) && size == 2 && kind == 6)
        *group->ports[4].Append() = type;
      if ((flags & 0x800) && size == 2 && kind == 6)
        *group->ports[3].Append() = type;
      if (((flags & 0x100) || kind == 0) && size == 1)
        *group->buildings.Append() = building;
      if (type)
        type->Release();
    }
    bool complete = group->buildings.size != 0;
    for (u32 index = 0; index < 5; ++index)
      if (!group->ports[index].size)
        complete = false;
    if (complete) {
      EcPortBuildingGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

EcPortPieceRule::EcPortPieceRule() : groups(nullptr), occupants(nullptr), port(nullptr) {
}

// Group descriptors are borrowed.
// Release each port-piece group and its owned general-building array.
EcPortPieceRule::~EcPortPieceRule() {
  while (groups) {
    EcPortPieceGroup *group = groups;
    groups = group->next;
    delete group;
  }
}

bool EcPortPieceRule::QueryInterface(u32 iid, void **out) {
  return EcRuleBase::QueryInterface(iid, out);
}

bool EcPortPieceRule::Init(CtCity *city) {
  return EcBindPortServices(city, &occupants, &buildings, &zones, &port);
}

u8 EcPortPieceRule::Shutdown() {
  return EcReleasePortServices(&occupants, &buildings, &port);
}

// Populate directional port pieces and general buildings, retaining complete configurations in source order.
void EcPortPieceRule::AddConfig(GeBuildingGroupView *source) {
  u32 count = source->GetBuildingCount();
  if (!count)
    return;
  GeWeightedBuilding *entries = new GeWeightedBuilding[count];
  if (source->GetBuildings(entries)) {
    EcPortPieceGroup *group = new EcPortPieceGroup{};
    for (u32 index = 0; index < count; ++index) {
      GeBuildingType *building = entries[index].attributes;
      if (!building)
        continue;
      u32 flags = building->Flags(), size = building->SizeInCellsX();
      GePortType *type = nullptr;
      u32 kind = 0;
      if (building->QueryInterface(IID_GePortType, reinterpret_cast<void **>(&type))) {
        kind = type->GetPortType();
        if (kind == 1 && size == 1) {
          if (flags & 0xf0) {
            if (flags & 0x20)
              group->pieces[2] = type;
            if (flags & 0x10)
              group->pieces[3] = type;
            if (flags & 0x80)
              group->pieces[4] = type;
            if (flags & 0x40)
              group->pieces[5] = type;
          } else {
            if (flags & 0x400)
              group->pieces[1] = type;
            if (flags & 0x800)
              group->pieces[0] = type;
          }
        }
      }
      if ((flags & 0x400) && size == 1 && kind == 2)
        group->pieces[7] = type;
      if ((flags & 0x800) && size == 1 && kind == 2)
        group->pieces[6] = type;
      if ((flags & 0x400) && size == 1 && kind == 3)
        group->pieces[9] = type;
      if ((flags & 0x800) && size == 1 && kind == 3)
        group->pieces[8] = type;
      if ((flags & 0x100) && size == 1)
        *group->buildings.Append() = building;
      if (type)
        type->Release();
    }
    if (EcPortPieceGroupValid(group)) {
      EcPortPieceGroup **tail = &groups;
      while (*tail)
        tail = &(*tail)->next;
      *tail = group;
    } else {
      delete group;
    }
  }
  delete[] entries;
}

void EcPortPieceRule::ScoreRegion(i32 *score, const GeCellRect &rectangle, u32 identifier) {
  GeBounds bounds{{i32(rectangle.left << 8), i32(rectangle.top << 8), 0},
      {i32(rectangle.right << 8), i32(rectangle.bottom << 8), 0}};
  ScOccupant *entries[10];
  u32 count = 0;
  if (occupants->GetOccupantsInCell(bounds, 10, entries, &count, nullptr)) {
    for (u32 index = 0; index < count; ++index) {
      ScOccupant *occupant = entries[index];
      if (occupant) {
        u32 type = occupant->TypeId();
        u32 width = occupant->SizeInCellsX();
        *score = i32(u32(*score) + (type == identifier ? 2u * width : 0u - width));
        occupant->Release();
      }
    }
  }
  for (u32 x = rectangle.left; x <= rectangle.right; ++x) {
    for (u32 z = rectangle.top; z <= rectangle.bottom; ++z) {
      u8 zone;
      zones->GetValue(x, z, &zone);
      if (zone == 14)
        *score = i32(u32(*score) + 1);
    }
  }
}

// Score the four neighboring edges, break ties using the original random stream, and place the selected port row.
u8 EcPortPieceRule::Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32) {
  if (demand <= 0 || !groups)
    return 0;
  i32 width = i32(rectangle.right - rectangle.left + 1), height = i32(rectangle.bottom - rectangle.top + 1);
  if (height < 3 || width < 3 || (width < 5 && height < 5))
    return 0;
  i32 scores[4]{};
  if (height < 5) {
    scores[0] = -20;
    scores[2] = -20;
  } else {
    if (rectangle.left) {
      GeCellRect edge{rectangle.left - 1, rectangle.top, rectangle.left - 1, rectangle.bottom};
      ScoreRegion(&scores[0], edge, identifier);
    }
    if (rectangle.right < occupants->GetCellsInX() - 1) {
      GeCellRect edge{rectangle.right + 1, rectangle.top, rectangle.right + 1, rectangle.bottom};
      ScoreRegion(&scores[2], edge, identifier);
    }
  }
  if (width < 5) {
    scores[1] = -20;
    scores[3] = -20;
  } else {
    if (rectangle.bottom < occupants->GetCellsInZ() - 1) {
      GeCellRect edge{rectangle.left, rectangle.bottom + 1, rectangle.right, rectangle.bottom + 1};
      ScoreRegion(&scores[1], edge, identifier);
    }
    if (rectangle.top) {
      GeCellRect edge{rectangle.left, rectangle.top - 1, rectangle.right, rectangle.top - 1};
      ScoreRegion(&scores[3], edge, identifier);
    }
  }
  u32 chosen = 0;
  i32 best = scores[0];
  for (u32 edge = 1; edge < 4; ++edge) {
    if (scores[edge] > best || (scores[edge] == best && random.Bounded(100) > 49)) {
      best = scores[edge];
      chosen = edge;
    }
  }
  if (best < 0 && width == 5 && height == 3)
    return 0;
  return PlaceEdgeRow(i32(chosen), best, rectangle);
}

// Select the configured direction, falling back to the alternate descriptor with its original rotation.
static GePortType *EcSelectPortPiece(
    EcPortPieceGroup *group, u32 preferred, u32 alternate, u16 fallback_rotation, u16 *rotation) {
  *rotation = 0;
  GePortType *piece = group->pieces[preferred];
  if (!piece) {
    piece = group->pieces[alternate];
    *rotation = fallback_rotation;
  }
  return piece;
}

// Reserve the selected port edge, then place its end pieces and interior rows in the original order.
u8 EcPortPieceRule::PlaceEdgeRow(i32 direction, i32 score, const GeCellRect &rectangle) {
  if (direction < 0 || direction > 3)
    return 0;
  bool vertical = direction == 0 || direction == 2;
  u32 length = vertical ? rectangle.bottom - rectangle.top + 1 : rectangle.right - rectangle.left + 1;
  if (length > 10)
    length = 10;
  i32 depth = vertical ? i32(rectangle.right - rectangle.left + 1) : i32(rectangle.bottom - rectangle.top + 1);
  ScPointXZ start, end;
  i32 step;
  if (vertical) {
    u32 x = direction == 0 ? rectangle.right : rectangle.left;
    if (direction == 0 && score > 0)
      x = rectangle.right;
    else if (direction == 2 && score > 0)
      x = rectangle.left;
    start = {x, rectangle.top};
    end = {x, rectangle.top + length - 1};
    step = direction == 0 ? -1 : 1;
  } else {
    u32 z = direction == 1 ? rectangle.top : rectangle.bottom;
    if (direction == 1 && score > 0)
      z = rectangle.top;
    else if (direction == 3 && score > 0)
      z = rectangle.bottom;
    start = {rectangle.left, z};
    end = {rectangle.left + length - 1, z};
    step = direction == 1 ? 1 : -1;
  }
  if (!depth || !port->SetSegmentFlag(start, end))
    return 0;
  u32 count = 0;
  for (EcPortPieceGroup *group = groups; group; group = group->next)
    ++count;
  u32 index = random.Bounded(count);
  EcPortPieceGroup *group = groups;
  while (index--)
    group = group->next;
  u16 rotation;
  GePortType *first =
      vertical ? EcSelectPortPiece(group, 3, 2, 270, &rotation) : EcSelectPortPiece(group, 2, 3, 270, &rotation);
  ScPointXZ point = start;
  zones->PlaceBuilding(first, point, 1, rotation, true);
  GePortType *back = nullptr, *inner = nullptr;
  u16 back_rotation = 0, inner_rotation = 0;
  if (depth > 2) {
    if (vertical)
      point.x += u32(step);
    else
      point.z += u32(step);
    back = vertical ? EcSelectPortPiece(group, 8, 9, 270, &back_rotation)
                    : EcSelectPortPiece(group, 9, 8, 270, &back_rotation);
    zones->PlaceBuilding(back, point, 1, back_rotation, true);
  }
  if (depth > 1) {
    if (vertical)
      point.x += u32(step);
    else
      point.z += u32(step);
    inner = vertical ? EcSelectPortPiece(group, 6, 7, 270, &inner_rotation)
                     : EcSelectPortPiece(group, 7, 6, 270, &inner_rotation);
    zones->PlaceBuilding(inner, point, 1, inner_rotation, true);
  }
  u16 middle_rotation;
  GePortType *middle = vertical ? EcSelectPortPiece(group, 0, 1, 90, &middle_rotation)
                                : EcSelectPortPiece(group, 1, 0, 270, &middle_rotation);
  for (u32 offset = 1; offset + 1 < length; ++offset) {
    point = start;
    if (vertical)
      point.z += offset;
    else
      point.x += offset;
    zones->PlaceBuilding(middle, point, 1, middle_rotation, true);
    if (depth > 2) {
      if (vertical)
        point.x += u32(step);
      else
        point.z += u32(step);
      zones->PlaceBuilding(group->buildings.data[0], point, 1, 0, true);
    }
    if (depth > 1) {
      if (vertical)
        point.x += u32(step);
      else
        point.z += u32(step);
      zones->PlaceBuilding(inner, point, 1, inner_rotation, true);
    }
  }
  point = end;
  GePortType *last =
      vertical ? EcSelectPortPiece(group, 4, 5, 270, &rotation) : EcSelectPortPiece(group, 2, 3, 90, &rotation);
  zones->PlaceBuilding(last, point, 1, rotation, true);
  if (depth > 2) {
    if (vertical)
      point.x += u32(step);
    else
      point.z += u32(step);
    zones->PlaceBuilding(back, point, 1, back_rotation, true);
  }
  if (depth > 1) {
    if (vertical)
      point.x += u32(step);
    else
      point.z += u32(step);
    zones->PlaceBuilding(inner, point, 1, inner_rotation, true);
  }
  return 1;
}

// Create the port-piece rule and expose its separate original Unknown identity.
ScUnknown *EcCreateRciClass10036CC1() {
  return static_cast<EcRuleUnknown *>(new EcPortPieceRule);
}

// Original commercial growth records contain one identifier and three independent byte properties.
struct EcCommercialGrowth {
  EcCommercialGrowth *next;
  u32 identifier;
  u8 category, minimum, maximum;
};

// The original two ordered indexes retain duplicate keys pointing to growth identifiers.
struct EcCommercialIndex {
  EcCommercialIndex *next;
  u32 key, identifier;
};

static bool EcCommercialTuningLoaded;
static u32 EcCommercialTuning[7] = {35, 3, 75, 30, 20, 30, 10};
