#include "geometry/selection.h"

#include <assert.h>
#include <stdlib.h>

#include "city/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_zones.h"
#include "geometry/api.h"
#include "network/api.h"
#include "network/nw_manager.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "terrain/api.h"

// Grouped geometry selects type-group removal instead of the ordinary per-layer occupant removal path.
enum GeDemolitionGeometry : u32 {
  kGeGroupedDemolitionGeometry = 0x40000000,
};

// Each enabled demolition selector accepts any matching bit from the original occupied-layer flag mask.
enum GeDemolitionLayerSelector : u32 {
  kGeDemolitionSurfaceSelectorMask = 0x400,
  kGeDemolitionFirstSelectorMask = 0x400800,
  kGeDemolitionSecondSelectorMask = 0x800800,
  kGeDemolitionCombinedSelectorMask = 0x40800,
};

GeAnimationTest::GeAnimationTest() : zeroZoneEnabled(true), otherZoneEnabled(true), power(nullptr), zones(nullptr) {
}

GeAnimationTest::~GeAnimationTest() {
  ScRelease(&power);
  ScRelease(&zones);
}

// Reject nonanimated occupants and unpowered buildings before applying the live zone-category switches. Successful
// building queries release the original occupant identity after capturing the result, rather than the adjusted output.
bool GeAnimationTest::Test(ScOccupant *occupant) {
  if (!occupant->AnimType())
    return false;
  void *building;
  if (!occupant->QueryInterface(IID_BdBuilding, &building))
    return true;
  bool result = false;
  if (static_cast<BdBuilding *>(building)->PowerRequired()) {
    u32 x, z;
    bool powered;
    occupant->GetLocationInCellCoords(&x, &z);
    power->GetValue(x, z, &powered);
    if (powered) {
      if (zeroZoneEnabled && otherZoneEnabled)
        result = true;
      else {
        u8 zone;
        zones->GetValue(x, z, &zone);
        result = zone && zone != 22 ? otherZoneEnabled : zeroZoneEnabled;
      }
    }
  } else if (zeroZoneEnabled && otherZoneEnabled)
    result = true;
  else {
    u32 x, z;
    u8 zone;
    occupant->GetLocationInCellCoords(&x, &z);
    zones->GetValue(x, z, &zone);
    result = zone && zone != 22 ? otherZoneEnabled : zeroZoneEnabled;
  }
  occupant->Release();
  return result;
}

GeBuildingTest::GeBuildingTest() {
}

GeBuildingTest::~GeBuildingTest() {
}

bool GeBuildingTest::Test(ScOccupant *occupant) {
  return occupant->MajorType() == 1;
}

GeDemolition::GeDemolition()
    : references(0), bounds{{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
                         {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}},
      extent(bounds), budget(nullptr), zones(nullptr), transit(nullptr), surface(nullptr), undergroundTwo(nullptr),
      undergroundOne(nullptr), terrain(nullptr), flora(nullptr), buildings(nullptr), neighbors(nullptr) {
}

// Construct the registered demolition layer with its original initial reference count and primary identity.
ScUnknown *GeCreateDemolition() {
  return new GeDemolition();
}

u8 GeDemolition::CanDemolishForBuildingSite(u32, const GeBounds *, u32, u32 *, GeBounds *, SpSelection **) {
  assert(false && "Original building-site demolition dereferences a null selection");
  abort();
}

GeDemolition::~GeDemolition() {
}

bool GeDemolition::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeDemolitionView);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, GeDemolitionView);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, GeDemolitionView);
  HANDLE_QUERY_INTERFACE(IID_GeDemolitionView, GeDemolitionView);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, GeDemolitionView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining the module director and return the count after that callback.
u32 GeDemolition::AddRef() {
  ++references;
  GeGetDirector()->AddRef();
  return references;
}

// Destroy exactly at a live count of one; other counts wrap downward. This producer does not release the module director.
u32 GeDemolition::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Capture city dimensions, then acquire each original service in attachment order before reading the demolition price.
bool GeDemolition::Init(CtCity *city) {
  u32 width = city->CellCountX(), height = city->CellCountZ();
  bounds.minimum = {0, 0, 0};
  bounds.maximum = {i32((width << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell),
      i32((height << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell), 0};
  extent.minimum = bounds.minimum;
  extent.maximum = {i32(width << kCtMicroCoordinateShift), i32(height << kCtMicroCoordinateShift), 0};
  budget = ScAddRefNotNull(city->BudgetLayer());
  zones = ScAddRefNotNull(city->ZoneLayer());
  transit = ScAddRefNotNull(reinterpret_cast<NwManager *>(city->TransitLayer()));
  surface = ScAddRefNotNull(city->SurfaceOccupantManager());
  undergroundOne = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
  undergroundTwo = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
  terrain = ScAddRefNotNull(city->DirtBag());
  flora = ScAddRefNotNull(city->FloraLayer());
  buildings = ScAddRefNotNull(city->BuildingLayer());
  neighbors = ScAddRefNotNull(city->NeighborsLayer());
  unitCost = u32(budget->GetCost(kEcCostDemolish));
  return true;
}

// Release live service publications in the original order and clear each field after its callback.
bool GeDemolition::Shutdown() {
  ScRelease(&budget);
  ScRelease(&zones);
  ScRelease(&transit);
  ScRelease(&surface);
  ScRelease(&undergroundOne);
  ScRelease(&undergroundTwo);
  ScRelease(&terrain);
  ScRelease(&flora);
  ScRelease(&buildings);
  ScRelease(&neighbors);
  return true;
}

bool GeDemolition::DoMessage(ScMessage *) {
  return false;
}

bool GeDemolition::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool GeDemolition::StaticInit(CtDefinition *) {
  return true;
}

bool GeDemolition::StaticShutdown() {
  return true;
}

bool GeDemolition::Init(CtCity *, ScImporter *) {
  return true;
}

bool GeDemolition::Init(CtCity *, RsDatabaseSegment *) {
  return true;
}

bool GeDemolition::Save(CtCity *, RsDatabaseSegment *) {
  return true;
}

bool GeDemolition::SimulationBegin() {
  return true;
}

bool GeDemolition::SimulationEnd() {
  return true;
}

bool GeDemolition::GetManipulator(const CtAgentType &, CtLayerManipulator **, i32 *) {
  return true;
}

u32 GeDemolition::GetLayerType() {
  return kGeDemolitionLayerType;
}

bool GeDemolition::DebugClassTag(ScStringView *) {
  return false;
}

bool GeDemolition::DebugTypeTag(ScStringView *) {
  return false;
}

// Select user removal only for mode one; buildings with occupancy bit512 refuse an otherwise permitted removal.
u8 GeDemolition::CanRemove(u32 mode, ScOccupant *occupant) {
  u8 allowed = mode == 1 ? occupant->CanUserRemove() : occupant->CanSimRemove();
  if (allowed && (occupant->GetGeomFlag() & 0x100)) {
    void *publication = nullptr;
    if (occupant->QueryInterface(IID_BdBuilding, &publication)) {
      if (static_cast<BdBuilding *>(publication)->GetOccupancyStateFlag(kGeBuildingHistorical))
        allowed = 0;
      static_cast<BdBuilding *>(publication)->Release();
    }
  }
  return allowed;
}

u8 GeDemolition::DemolishForBuildingSite(u32, const GeBounds *, u32, bool, u32 *, GeBounds *) {
  return 0;
}

// Replace type300 flora at its terrain altitude, or recreate a building from its borrowed base attributes and set state.
// Keep refused publications and the original removal/insertion order; only the flora branch checks removal permission.
u8 GeDemolition::ChangeOccupancyState(
    ScOccupantManager *manager, ScOccupant *occupant, u32 privilege, i32 state, bool enforcePermission) {
  u8 result = 0;
  if (occupant->TypeId() == 300) {
    RsKey key(kRsFloraAttributesResourceType, kRsFloraAttributesGroup, 7057);
    if (!enforcePermission || CanRemove(privilege, occupant)) {
      void *publication;
      result = flora->CreateOccupant(&key, IID_ScOccupant, &publication);
      if (result) {
        auto *replacement = static_cast<ScOccupant *>(publication);
        u32 x, z;
        u8 altitude;
        occupant->GetLocationInCellCoords(&x, &z);
        terrain->GetVertexAltitude(x, z, &altitude);
        CtCoord position{i32(x << kCtMicroCoordinateShift), i32(z << kCtMicroCoordinateShift),
            i32(u32(altitude) << kCtMicroCoordinateShift)};
        replacement->SetLocation(position);
        manager->RemoveOccupant(occupant);
        result = manager->InsertOccupant(replacement);
        replacement->Release();
      }
    }
  } else {
    void *publication;
    if (occupant->QueryInterface(IID_BdBuilding, &publication)) {
      auto *building = static_cast<BdBuilding *>(publication);
      GeAttrib *attributes = building->GetBaseAttribData();
      void *replacementPublication;
      result = buildings->CreateBuilding(attributes, IID_BdBuilding, &replacementPublication);
      if (result) {
        auto *replacement = static_cast<BdBuilding *>(replacementPublication);
        void *occupantPublication;
        if (replacement->QueryInterface(IID_ScOccupant, &occupantPublication)) {
          auto *replacementOccupant = static_cast<ScOccupant *>(occupantPublication);
          u32 x, z;
          occupant->GetLocationInCellCoords(&x, &z);
          replacementOccupant->SetLocationInCellCoords(x, z);
          replacementOccupant->Release();
          replacement->SetOccupancyState(u32(state));
          manager->RemoveOccupant(occupant);
          result = buildings->InsertBuilding(replacement);
        }
        replacement->Release();
      }
      building->Release();
    }
  }
  return result;
}

// Collect each removable occupant's complete footprint and charge its byte-sized dimensions at the live unit price.
// A refusal only fails the result when exactly one occupant was visited; the selection and cost remain published.
u8 GeDemolition::CanDemolishOccupants(ScOccupantManager *manager, const GeBounds *region, OcIteratorTest *test,
    u32 mode, u32 *cost, GeBounds *affected, SpSelection **selection) {
  GeSelection *result = new GeSelection;
  result->AddRef();
  bool refused = false;
  *cost = 0;
  OcIterator *iterator = nullptr;
  u32 visited = 0;
  manager->GetOccupants(&iterator, region, test);
  if (iterator->Current())
    do {
      ScOccupant *occupant = iterator->Current();
      if (CanRemove(mode, occupant)) {
        u32 x, z;
        occupant->GetLocationInCellCoords(&x, &z);
        GeAttrib *attributes = occupant->GetAttribData();
        u8 width = u8(attributes->SizeInCellsX()), height = u8(attributes->SizeInCellsZ());
        *cost += u32(width) * height * unitCost;
        u32 lastX = x + width, lastZ = z + height;
        for (u32 cellX = x; cellX < lastX; ++cellX)
          for (u32 cellZ = z; cellZ < lastZ; ++cellZ)
            result->Append({i32(cellX << kCtMicroCoordinateShift), i32(cellZ << kCtMicroCoordinateShift), 0});
      } else
        refused = true;
      ++visited;
    } while (iterator->Next());
  iterator->Release();
  u8 allowed = visited != 1 || !refused;
  result->GetBounds(affected);
  *selection = result;
  return allowed;
}

// Scan only cells changed by the last terrain operation, using the layer mode to select surface and underground checks.
// Accumulate permitted surface costs even when another inspected occupant refuses the operation.
u8 GeDemolition::CanDemolishFromDirtChange(u32 layers, const GeBounds *region, u32 mode, u32 *cost) {
  terrain->LockUpdates(true);
  bool checkSurface = false, checkOne = false, checkTwo = false;
  *cost = 0;
  u32 total = 0;
  u8 allowed = 1;
  switch (layers) {
  case 0:
    checkSurface = checkOne = checkTwo = true;
    break;
  case 1:
    checkSurface = true;
    break;
  case 2:
    checkSurface = checkTwo = true;
    break;
  case 3:
    checkSurface = checkOne = true;
    break;
  }
  ScOccupant *occupant = nullptr;
  u32 firstZ = u32(region->minimum.z >> kCtMicroCoordinateShift),
      lastZ = u32(region->maximum.z >> kCtMicroCoordinateShift);
  u32 lastX = u32(region->maximum.x >> kCtMicroCoordinateShift);
  for (u32 x = u32(region->minimum.x >> kCtMicroCoordinateShift); x <= lastX; ++x)
    for (u32 z = firstZ; z <= lastZ; ++z)
      if (terrain->ChangedByLastOperation(x, z)) {
        if (checkSurface && surface->GetOccupantAtCell(x, z, &occupant)) {
          if (occupant->IsGeomFlag(1024) && CanRemove(mode, occupant))
            total += unitCost;
          else
            allowed = 0;
          occupant->Release();
        }
        if (checkOne && undergroundOne->GetOccupantAtCell(x, z, &occupant)) {
          allowed = 0;
          occupant->Release();
        }
        if (checkTwo && undergroundTwo->GetOccupantAtCell(x, z, &occupant)) {
          allowed = 0;
          occupant->Release();
        }
      }
  terrain->LockUpdates(false);
  *cost = total;
  return allowed;
}

// Scan the inclusive cell region under the terrain update lock, optionally filtering by the last terrain operation.
// Publish the original empty retained selection and affected bounds even when an occupant refuses demolition.
u8 GeDemolition::CanDemolishFromDirtChange(
    const GeBounds *region, u32 mode, u32 *cost, GeBounds *affected, SpSelection **selection, u8 changed_only) {
  GeSelection *result = new GeSelection;
  result->AddRef();
  *cost = 0;
  terrain->LockUpdates(true);
  u32 total = 0, firstZ = u32(region->minimum.z >> kCtMicroCoordinateShift),
      lastZ = u32(region->maximum.z >> kCtMicroCoordinateShift);
  u32 lastX = u32(region->maximum.x >> kCtMicroCoordinateShift);
  u8 allowed = 1;
  for (u32 x = u32(region->minimum.x >> kCtMicroCoordinateShift); x <= lastX; ++x)
    for (u32 z = firstZ; z <= lastZ; ++z)
      if (!changed_only || terrain->ChangedByLastOperation(x, z)) {
        ScOccupant *occupant = nullptr;
        if (surface->GetOccupantAtCell(x, z, &occupant)) {
          if (occupant->IsGeomFlag(1024) && CanRemove(mode, occupant))
            total += unitCost;
          else
            allowed = 0;
          occupant->Release();
          occupant = nullptr;
        }
        if (undergroundOne->GetOccupantAtCell(x, z, &occupant)) {
          allowed = 0;
          occupant->Release();
          occupant = nullptr;
        }
        if (undergroundTwo->GetOccupantAtCell(x, z, &occupant)) {
          allowed = 0;
          occupant->Release();
          occupant = nullptr;
        }
      }
  terrain->LockUpdates(false);
  result->GetBounds(affected);
  *selection = result;
  *cost = total;
  return allowed;
}

// Enumerate and remove permitted occupants, accumulating their costs and the changed network bounds.
u8 GeDemolition::DemolishOccupants(ScOccupantManager *manager, const GeBounds *region, OcIteratorTest *test, u32 mode,
    bool freeOfCharge, u32 *cost, GeBounds *affected) {
  GeBounds normalized(*region);
  normalized.NormalizeXZ();
  u32 changes = 0, count = 0;
  affected->minimum.x = INT32_MAX;
  *cost = 0;
  OcIterator *iterator = nullptr;
  manager->GetOccupants(&iterator, &normalized, test);
  ScOccupant *entry = iterator->Current();
  while (entry) {
    ScOccupant *occupant = iterator->Current();
    if (CanRemove(mode, occupant)) {
      GeBounds removed;
      u32 removalCost = 0;
      DemolitionRemoveOccupant(occupant, mode, &removalCost, &removed, &changes);
      if (removed.minimum.x != INT32_MAX) {
        if (affected->minimum.x == INT32_MAX)
          *affected = removed;
        else
          affected->UnionXZ(*affected, removed);
      }
      *cost += removalCost;
      ++count;
    }
    entry = iterator->Next();
  }
  iterator->Release();
  if (changes) {
    u32 first = (u32(affected->minimum.x >> kCtMicroCoordinateShift) << 16) |
        u32(affected->minimum.z >> kCtMicroCoordinateShift);
    u32 last = (u32(affected->maximum.x >> kCtMicroCoordinateShift) << 16) |
        u32(affected->maximum.z >> kCtMicroCoordinateShift);
    ScMessage message(kNwMsg_RefreshNetwork, changes, first, last);
    GeGetMessageServer()->MessagePost(message, false);
  }
  if (!freeOfCharge)
    budget->WithdrawFunds(*cost);
  return count != 0;
}

// Remove occupants in the terrain-changed cells, clear their zones and aggregate the original affected region and cost.
u8 GeDemolition::DemolishFromDirtChange(
    const GeBounds *region, u32 mode, bool freeOfCharge, u32 *cost, GeBounds *affected, bool changedOnly) {
  GeBounds clipped(*region);
  clipped.IntersectXZ(clipped, bounds);
  affected->minimum.x = INT32_MAX;
  *cost = 0;
  u32 changes = 0;
  terrain->LockUpdates(true);
  u32 lastX = u32(clipped.maximum.x >> kCtMicroCoordinateShift),
      firstZ = u32(clipped.minimum.z >> kCtMicroCoordinateShift),
      lastZ = u32(clipped.maximum.z >> kCtMicroCoordinateShift);
  for (u32 x = u32(clipped.minimum.x >> kCtMicroCoordinateShift); x <= lastX; ++x) {
    for (u32 z = firstZ; z <= lastZ; ++z) {
      if (changedOnly && !terrain->ChangedByLastOperation(x, z))
        continue;
      u8 zone;
      zones->GetValue(x, z, &zone);
      if (zone) {
        u8 empty = 0;
        zones->SetValue(x, z, &empty);
      }
      GeBounds removed;
      u32 removalCost = 0;
      ScOccupant *occupant = nullptr;
      for (u32 index = 0; index < 3; ++index) {
        ScOccupantManager *manager = index == 0 ? surface : index == 1 ? undergroundOne : undergroundTwo;
        if (manager->GetOccupantAtCell(x, z, &occupant)) {
          removed.minimum.x = INT32_MAX;
          DemolitionRemoveOccupant(occupant, mode, &removalCost, &removed, &changes);
          if (removed.minimum.x != INT32_MAX) {
            if (affected->minimum.x == INT32_MAX)
              *affected = removed;
            else
              affected->UnionXZ(*affected, removed);
          }
          *cost += removalCost;
          occupant->Release();
          occupant = nullptr;
        }
      }
    }
  }
  if (changes) {
    u32 first = (u32(affected->minimum.x >> kCtMicroCoordinateShift) << 16) |
        u32(affected->minimum.z >> kCtMicroCoordinateShift);
    u32 last = (u32(affected->maximum.x >> kCtMicroCoordinateShift) << 16) |
        u32(affected->maximum.z >> kCtMicroCoordinateShift);
    ScMessage message(kNwMsg_RefreshNetwork, changes, first, last);
    GeGetMessageServer()->MessagePost(message, false);
  }
  u8 result = freeOfCharge ? 1 : budget->WithdrawFunds(*cost);
  terrain->LockUpdates(false);
  return result;
}

// Remove occupants from terrain-changed cells and enlarge the affected network bounds before notifying listeners.
// The original ignores the layer selector, charges the accumulated cost unless free and unlocks terrain afterward.
u8 GeDemolition::DemolishFromDirtChange(u32, const GeBounds *region, u32 mode, bool freeOfCharge, u32 *cost) {
  GeBounds clipped(*region), affected;
  clipped.IntersectXZ(clipped, bounds);
  affected.minimum.x = INT32_MAX;
  *cost = 0;
  u32 changes = 0;
  terrain->LockUpdates(true);
  u32 lastX = u32(clipped.maximum.x >> kCtMicroCoordinateShift),
      firstZ = u32(clipped.minimum.z >> kCtMicroCoordinateShift),
      lastZ = u32(clipped.maximum.z >> kCtMicroCoordinateShift);
  for (u32 x = u32(clipped.minimum.x >> kCtMicroCoordinateShift); x <= lastX; ++x) {
    for (u32 z = firstZ; z <= lastZ; ++z) {
      if (!terrain->ChangedByLastOperation(x, z))
        continue;
      u8 zone;
      zones->GetValue(x, z, &zone);
      if (zone) {
        u8 empty = 0;
        zones->SetValue(x, z, &empty);
      }
      GeBounds removed;
      u32 removalCost = 0;
      ScOccupant *occupant = nullptr;
      for (u32 index = 0; index < 3; ++index) {
        ScOccupantManager *manager = index == 0 ? surface : index == 1 ? undergroundOne : undergroundTwo;
        if (manager->GetOccupantAtCell(x, z, &occupant)) {
          removed.minimum.x = INT32_MAX;
          DemolitionRemoveOccupant(occupant, mode, &removalCost, &removed, &changes);
          if (removed.minimum.x != INT32_MAX) {
            if (affected.minimum.x == INT32_MAX)
              affected = removed;
            else
              affected.UnionXZ(affected, removed);
          }
          *cost += removalCost;
          occupant->Release();
          occupant = nullptr;
        }
      }
    }
  }
  affected.GrowXZCell((changes & (0x10 | 2)) ? 2 : 1);
  if (changes) {
    u32 first =
        (u32(affected.minimum.x >> kCtMicroCoordinateShift) << 16) | u32(affected.minimum.z >> kCtMicroCoordinateShift);
    u32 last =
        (u32(affected.maximum.x >> kCtMicroCoordinateShift) << 16) | u32(affected.maximum.z >> kCtMicroCoordinateShift);
    ScMessage message(kNwMsg_RefreshNetwork, changes, first, last);
    GeGetMessageServer()->MessagePost(message, false);
  }
  u8 result = freeOfCharge ? 1 : budget->WithdrawFunds(*cost);
  terrain->LockUpdates(false);
  return result;
}

// Check the occupant's captured network flags against neighbor connections in the original short-circuit order.
// The occupant cell location is queried once after the network-occupant flag gate.
u8 GeDemolition::HasNeighborConnection(ScOccupant *occupant) {
  u32 flags = occupant->GetGeomFlag();
  if (!(flags & kOcGeomNetwork))
    return 0;
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  if ((flags & kOcGeomPower) && neighbors->HasConnection(3, i32(x), i32(z)))
    return 1;
  if ((flags & kOcGeomRoad) && neighbors->HasConnection(1, i32(x), i32(z)))
    return 1;
  if ((flags & kOcGeomHighway) && neighbors->HasConnection(4, i32(x), i32(z)))
    return 1;
  if ((flags & kOcGeomPipe) && neighbors->HasConnection(5, i32(x), i32(z)))
    return 1;
  if ((flags & kOcGeomSubway) && neighbors->HasConnection(6, i32(x), i32(z)))
    return 1;
  if ((flags & kOcGeomRail) && neighbors->HasConnection(2, i32(x), i32(z)))
    return 1;
  return 0;
}

// Remove the permitted target, then matching types within twice its dimensions, clipped to the layer's signed bounds.
// The original target is deliberately retested for every matching neighbor; neighbor permission is not queried.
u8 GeDemolition::RemoveTypeGroup(u32 mode, ScOccupantManager *manager, ScOccupant *occupant) {
  u32 type = occupant->TypeId();
  u16 width = occupant->SizeInCellsX(), height = occupant->SizeInCellsZ();
  CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  occupant->GetLocation(&position);
  u32 radiusX = u32(u16(width * 2)) << kCtMicroCoordinateShift,
      radiusZ = u32(u16(height * 2)) << kCtMicroCoordinateShift;
  GeBounds region{{i32(u32(position.x) - radiusX), i32(u32(position.z) - radiusZ), kCtUnknownCoordinate},
      {i32(u32(position.x) + radiusX), i32(u32(position.z) + radiusZ), kCtUnknownCoordinate}};
  region.IntersectXZ(region, bounds);
  u32 firstX = u32(region.minimum.x >> kCtMicroCoordinateShift),
      firstZ = u32(region.minimum.z >> kCtMicroCoordinateShift);
  u32 lastX = u32(region.maximum.x >> kCtMicroCoordinateShift),
      lastZ = u32(region.maximum.z >> kCtMicroCoordinateShift);
  if (!CanRemove(mode, occupant))
    return 0;
  manager->RemoveOccupant(occupant);
  for (u32 x = firstX; x < lastX; ++x)
    for (u32 z = firstZ; z < lastZ; ++z) {
      ScOccupant *current = nullptr;
      if (manager->GetOccupantAtCell(x, z, &current)) {
        if (current->TypeId() == type && CanRemove(mode, occupant))
          manager->RemoveOccupant(current);
        current->Release();
      }
    }
  return 1;
}

// Compute demolition cost and remove the occupant from each live layer, preserving network permission and neighbor
// handling.
void GeDemolition::DemolitionRemoveOccupant(
    ScOccupant *occupant, u32 mode, u32 *cost, GeBounds *affected, u32 *changes) {
  u32 flags = occupant->GetGeomFlag();
  GeAttrib *attributes = occupant->GetAttribData();
  u8 width = u8(attributes->SizeInCellsX()), depth = u8(attributes->SizeInCellsZ());
  *cost = u32(width) * u32(depth) * unitCost;
  bool network = (flags & kOcGeomNetwork) != 0, grouped = (flags & kGeGroupedDemolitionGeometry) != 0;
  u32 layers = occupant->GetIsInCityFlag();
  if (!network && !grouped) {
    if (layers & 1)
      surface->RemoveOccupant(occupant);
    if (layers & 2)
      undergroundOne->RemoveOccupant(occupant);
    if (layers & 4)
      undergroundTwo->RemoveOccupant(occupant);
    return;
  }
  if (grouped) {
    RemoveTypeGroup(mode, surface, occupant);
    return;
  }
  if (flags & kOcGeomRoad)
    *changes |= 1;
  if (flags & kOcGeomHighway)
    *changes |= 2;
  if (flags & kOcGeomRail)
    *changes |= 4;
  if (flags & kOcGeomPower)
    *changes |= 0x20;
  if (flags & kOcGeomPipe)
    *changes |= 8;
  if (flags & kOcGeomSubway)
    *changes |= 0x10;
  u16 piece = 0;
  u8 rotation = 5;
  NwConnection *connection = nullptr;
  if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
    piece = u16(connection->TileId());
    rotation = u8(connection->GetRotationFlags());
    connection->Release();
  }
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  GeBounds collapsed;
  for (u32 index = 0; index < 3; ++index) {
    if (!(layers & (1u << index)))
      continue;
    ScOccupantManager *manager = index == 0 ? surface : index == 1 ? undergroundOne : undergroundTwo;
    if (HasNeighborConnection(occupant)) {
      if (mode == 1) {
        if (!transit->RemoveConnectionInteractive(manager, occupant))
          continue;
      } else {
        u32 x, z;
        occupant->GetLocationInCellCoords(&x, &z);
        if (index == 0) {
          neighbors->RemoveConnection(1, i32(x), i32(z));
          neighbors->RemoveConnection(4, i32(x), i32(z));
          neighbors->RemoveConnection(3, i32(x), i32(z));
          neighbors->RemoveConnection(2, i32(x), i32(z));
        } else {
          neighbors->RemoveConnection(index == 1 ? 5 : 6, i32(x), i32(z));
        }
      }
    }
    manager->RemoveOccupant(occupant);
    collapsed.minimum.x = INT32_MAX;
    transit->CollapseSupport(manager, position, piece, rotation, &collapsed, mode == 1);
    if (collapsed.minimum.x != INT32_MAX) {
      if (affected->minimum.x == INT32_MAX)
        *affected = collapsed;
      else
        affected->UnionXZ(*affected, collapsed);
    }
  }
}

GeDemolitionTest::GeDemolitionTest(u32 layers)
    : references(0), surface(0), first((layers & 1) != 0), combined((layers & 3) != 0), second((layers & 2) != 0) {
}

GeDemolitionTest::~GeDemolitionTest() {
}

// Publish this filter for the two supported identities and acquire a reference through the live virtual method. A
// refused query clears the output.
bool GeDemolitionTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeDemolitionTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, GeDemolitionTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeDemolitionTest::AddRef() {
  return ++references;
}

// Decrement a nonzero count and return it, including when that decrement reaches zero.
// An already unreferenced filter is deleted through its virtual destructor.
u32 GeDemolitionTest::Release() {
  if (references)
    return --references;
  delete this;
  return 0;
}

// Read the occupant's layer flags once, then test the enabled selectors in the original order.
// Each selector accepts any bit in its own mask rather than requiring all mask bits.
bool GeDemolitionTest::Test(ScOccupant *occupant) {
  u32 layers = occupant->GetOccLayerFlag();
  return (surface && (layers & kGeDemolitionSurfaceSelectorMask)) ||
      (first && (layers & kGeDemolitionFirstSelectorMask)) || (second && (layers & kGeDemolitionSecondSelectorMask)) ||
      (combined && (layers & kGeDemolitionCombinedSelectorMask));
}

GeIteratorTest::GeIteratorTest() : references(0) {
}

GeIteratorTest::~GeIteratorTest() {
}

// Publish either supported identity before acquiring one filter reference. Failed queries clear the output.
bool GeIteratorTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, GeIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeIteratorTest::AddRef() {
  return ++references;
}

u32 GeIteratorTest::Release() {
  return ScReleaseImpl(this, &references);
}

bool GeIteratorTest::Test(ScOccupant *) {
  return true;
}

GeSelection::GeSelection() : begin(nullptr), end(nullptr), storageEnd(nullptr), cursor(0), references(0) {
  Reserve(128);
}

// Copy all coordinates in stored order and then the source cursor; the new owner starts without references.
GeSelection::GeSelection(const GeSelection &source)
    : begin(nullptr), end(nullptr), storageEnd(nullptr), cursor(0), references(0) {
  const CtCoord *first = source.begin, *last = source.end;
  while (first != last)
    Append(*first++);
  cursor = source.cursor;
}

// Publish the actual selection/unknown interface before the live virtual retain; failed queries clear the output.
bool GeSelection::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpSelection);
  HANDLE_QUERY_INTERFACE(IID_SpSelection, SpSelection);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeSelection::AddRef() {
  return ++references;
}

// Release the owner at its final reference through the actual virtual destructor and return zero after deletion.
u32 GeSelection::Release() {
  u32 result = --references;
  if (!result) {
    delete this;
    return 0;
  }
  return result;
}

// Copy at most the requested number from the current cursor, preserving the source's wrapped cursor arithmetic.
// End of storage still returns one; the genuine count output reports how many positions were copied.
u8 GeSelection::Next(u32 maximum, CtCoord *out, u32 *count) {
  u32 copied = 0, size = begin ? u32(end - begin) : 0;
  while (copied < maximum && cursor + copied < size) {
    out[copied] = begin[cursor + copied];
    ++copied;
  }
  cursor += copied;
  *count = copied;
  return 1;
}

u8 GeSelection::Skip(u32 count) {
  cursor += count;
  return 1;
}

u8 GeSelection::Reset() {
  cursor = 0;
  return 1;
}

// Allocate an independent copy, publish its interface first, then retain that published clone once.
u8 GeSelection::Clone(SpSelection **out) {
  GeSelection *copy = new GeSelection(*this);
  *out = copy;
  copy->AddRef();
  return 1;
}

// Compute signed X/Z extents while reproducing the original empty -256 coordinates and altitude sentinels.
// The Boolean result compares the final first X word to -1, including the original true result for empty storage.
bool GeSelection::GetBounds(GeBounds *bounds) {
  bounds->minimum = {-256, -256, 0};
  bounds->maximum = {-256, -256, 0};
  if (begin != end) {
    bounds->minimum = *begin;
    bounds->maximum = *begin;
    for (CtCoord *position = begin; position != end; ++position)
      bounds->UnionXZ(*bounds, *position);
  }
  return bounds->minimum.x != -1;
}

void GeSelection::Clear() {
  end = begin;
}

// Grow coordinate storage with ordinary array ownership and preserve every initialized stored position.
void GeSelection::Reserve(u32 capacity) {
  u32 size = begin ? u32(end - begin) : 0;
  if (capacity > UINT32_MAX / sizeof(CtCoord))
    ScFatal(SC_ERROR_NOMEM, "Selection coordinate allocation is too large.");
  CtCoord *storage = new CtCoord[capacity];
  for (u32 index = 0; index < size; ++index)
    storage[index] = begin[index];
  delete[] begin;
  begin = storage;
  end = begin + size;
  storageEnd = begin + capacity;
}

// Capture a possibly aliased coordinate before growth and append it to the actual contiguous range.
void GeSelection::Append(const CtCoord &position) {
  CtCoord value = position;
  if (end == storageEnd) {
    u32 capacity = begin ? u32(storageEnd - begin) : 0;
    if (capacity > UINT32_MAX / 2)
      ScFatal(SC_ERROR_NOMEM, "Selection coordinate capacity overflow.");
    Reserve(capacity ? capacity * 2 : 1);
  }
  *end++ = value;
}

// Retire coordinate storage after clearing the logical range and cursor as the original destructor does.
GeSelection::~GeSelection() {
  Clear();
  cursor = 0;
  delete[] begin;
}

static bool GeSelectionLess(const CtCoord &first, const CtCoord &second) {
  return first.x < second.x || (first.x == second.x && first.z < second.z);
}

// Select the original median-of-three pivot, retaining its tie decisions without moving any coordinate.
static CtCoord GeSelectionMedian(const CtCoord &first, const CtCoord &middle, const CtCoord &last) {
  if (GeSelectionLess(first, middle)) {
    if (GeSelectionLess(middle, last))
      return middle;
    return GeSelectionLess(first, last) ? last : first;
  }
  if (GeSelectionLess(first, last))
    return first;
  return GeSelectionLess(middle, last) ? last : middle;
}

// Restore a coordinate heap by descending through the larger child, choosing the right child on equal keys.
static void GeSelectionAdjustHeap(CtCoord *first, i32 hole, i32 count, const CtCoord &value) {
  i32 top = hole, child = hole * 2 + 2;
  while (child < count) {
    if (GeSelectionLess(first[child], first[child - 1]))
      --child;
    first[hole] = first[child];
    hole = child;
    child = child * 2 + 2;
  }
  if (child == count) {
    first[hole] = first[child - 1];
    hole = child - 1;
  }
  i32 parent = (hole - 1) / 2;
  while (hole > top && GeSelectionLess(first[parent], value)) {
    first[hole] = first[parent];
    hole = parent;
    parent = (parent - 1) / 2;
  }
  first[hole] = value;
}

// Execute the original coordinate-specific heap fallback when the partition depth limit is exhausted.
// Every exchange copies the full altitude payload while ordering only the signed X/Z keys.
static void GeSelectionHeapSort(CtCoord *first, CtCoord *last) {
  i32 count = i32(last - first);
  if (count >= 2)
    for (i32 parent = (count - 2) / 2;; --parent) {
      CtCoord value = first[parent];
      GeSelectionAdjustHeap(first, parent, count, value);
      if (!parent)
        break;
    }
  while (last - first > 1) {
    --last;
    CtCoord value = *last;
    *last = *first;
    GeSelectionAdjustHeap(first, 0, i32(last - first), value);
  }
}

// Reproduce the original partition exchanges and right-first recursion down to sixteen-coordinate ranges.
// The partition pivot is a copied coordinate; equal keys exchange their payloads rather than being stably sorted.
static void GeSelectionPartitionSort(CtCoord *first, CtCoord *last, u32 depth) {
  while (last - first > 16) {
    if (!depth) {
      GeSelectionHeapSort(first, last);
      return;
    }
    --depth;
    CtCoord pivot = GeSelectionMedian(*first, first[(last - first) / 2], last[-1]);
    CtCoord *left = first, *right = last;
    for (;;) {
      while (GeSelectionLess(*left, pivot))
        ++left;
      do
        --right;
      while (GeSelectionLess(pivot, *right));
      if (left >= right)
        break;
      CtCoord value = *left;
      *left++ = *right;
      *right = value;
    }
    GeSelectionPartitionSort(left, last, depth);
    last = left;
  }
}

// Insert a saved coordinate behind the established minimum sentinel, shifting only strictly greater keys.
static void GeSelectionInsertBack(CtCoord *position, const CtCoord &value) {
  CtCoord *previous = position - 1;
  while (GeSelectionLess(value, *previous)) {
    *position = *previous;
    position = previous--;
  }
  *position = value;
}

// Sort the exact signed X/Z keys and compact distinct-coordinate payloads without shrinking the stored range.
// The original discards unique's returned end, so tail coordinates, cursor and reference count remain observable.
u8 GeSelection::SortAndCompact() {
  if (begin != end) {
    u32 count = u32(end - begin), depth = 0;
    for (u32 remaining = count; remaining != 1; remaining >>= 1)
      ++depth;
    GeSelectionPartitionSort(begin, end, depth * 2);
    CtCoord *prefix = count > 16 ? begin + 16 : end;
    for (CtCoord *position = begin + 1; position != prefix; ++position) {
      CtCoord value = *position;
      if (GeSelectionLess(value, *begin)) {
        for (CtCoord *destination = position; destination != begin; --destination)
          *destination = destination[-1];
        *begin = value;
      } else
        GeSelectionInsertBack(position, value);
    }
    for (CtCoord *position = prefix; position != end; ++position) {
      CtCoord value = *position;
      GeSelectionInsertBack(position, value);
    }
    CtCoord *last = begin;
    for (CtCoord *position = begin + 1; position != end; ++position)
      if (position->x != last->x || position->z != last->z) {
        ++last;
        if (last != position)
          *last = *position;
      }
  }
  return 1;
}

// Append each successful source batch plus its initialized history entry when that entry is inside the array.
u8 GeSelection::AppendFrom(SpSelection *source) {
  CtCoord positions[32];
  for (u32 i = 0; i < 32; ++i)
    positions[i] = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u32 count = 0;
  source->Reset();
  while (source->Next(32, positions, &count))
    for (u32 i = 0; i < 32 && i <= count; ++i)
      Append(positions[i]);
  return 1;
}

GeTypeTest::GeTypeTest(u32 value) : references(0), type(value), tree(GeGetAgentTypeTree()) {
  tree->AddRef();
}

GeTypeTest::~GeTypeTest() {
  tree->Release();
}

// Publish the same filter identity for Unknown and iterator-test queries, retaining virtually after publication.
// Unsupported identifiers clear the output.
bool GeTypeTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeTypeTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, GeTypeTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeTypeTest::AddRef() {
  return ++references;
}

u32 GeTypeTest::Release() {
  return ScReleaseImpl(this, &references);
}

bool GeTypeTest::Test(ScOccupant *occupant) {
  u32 value = occupant->TypeId();
  return tree->IsAncestor(type, value);
}
