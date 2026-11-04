#include "geometry/building_controller.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/grid.h"
#include "geometry/selection.h"
#include "geometry/tool.h"
#include "geometry/utilities.h"
#include "network/nw_manager.h"
#include "network/nw_records.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/city_window.h"
#include "terrain/api.h"

enum GeBuildingArchiveKey : u32 {
  kGeBuildingPayloadRecordType = 0xe0faadc7,
  kGeBuildingHeaderRecordType = 0x20631788,
  kGeBuildingRecordGroup = 0xa0ab89f0,
};

extern CtAgentTree *GeBuildingAgentTree;

// Read the original header variants and recreate descriptors from saved keys or the permitted type-and-size fallback.
// A data-stream failure aborts that record but returns success; header failure or unsupported framing refuses.
bool GeBuildingController::Init(CtCity *target, RsDatabaseSegment *segment) {
  u32 count = 0, codec = 0;
  u16 version;
  u8 debug;
  {
    NwRecordFrame header(target, segment, RsKey(kGeBuildingHeaderRecordType, kGeBuildingRecordGroup, 0));
    version = header.version;
    debug = header.debug;
    RsStream *stream = nullptr;
    u8 result = header.valid;
    if (result) {
      stream = header.Stream();
      result = stream != nullptr;
    }
    switch (version) {
    case 1:
    case 2:
      if (result && debug) {
        ScString tag;
        result = stream->ReadString(&tag);
      }
      if (result)
        result = stream->ReadU32(&count);
      if (result && version == 2)
        result = stream->ReadU32(&codec);
      result = result && stream->ReadU32(&centerX) && stream->ReadU32(&centerZ) && stream->ReadU32(&weightedX) &&
          stream->ReadU32(&weightedZ) && stream->ReadU32(&totalArea);
      centerX = 0;
      centerZ = 0;
      weightedX = 0;
      weightedZ = 0;
      totalArea = 0;
      if (result && debug) {
        ScString tag;
        result = stream->ReadString(&tag);
      }
      break;
    case 3:
      if (result && debug) {
        ScString tag;
        result = stream->ReadString(&tag);
      }
      result = result && stream->ReadU32(&count) && stream->ReadU32(&codec);
      if (result && debug) {
        ScString tag;
        result = stream->ReadString(&tag);
      }
      break;
    default:
      result = 0;
      break;
    }
    if (!result) {
      if (header.record)
        header.record->aborted = 1;
      header.valid = 0;
      return false;
    }
  }
  if (version != 2)
    return false;
  NwRecordFrame frame(target, segment, RsKey(kGeBuildingPayloadRecordType, kGeBuildingRecordGroup, 1));
  RsStream *stream = nullptr;
  u8 result = frame.valid;
  if (result) {
    stream = frame.Stream();
    result = stream != nullptr;
  }
  GeRecord info(1);
  RsKey key{};
  for (u32 index = 0; index < count; ++index) {
    if (!result)
      break;
    if (debug) {
      ScString tag;
      result = stream->ReadString(&tag);
    }
    if (result)
      result = info.Clear();
    if (result)
      result = info.SerialRead(target, codec, stream);
    if (result && debug) {
      ScString tag;
      result = stream->ReadString(&tag);
    }
    if (!result)
      continue;
    result = info.GetAttribKey(&key);
    if (!result)
      continue;
    void *publication = nullptr;
    if (CreateBuilding(key, IID_BdBuilding, &publication))
      info.SetSameKeyWroteResource(1);
    else {
      u32 type = 0, width = 0, depth = 0;
      u8 hasType = info.GetTypeId(&type);
      if (hasType)
        info.GetSizeInCells(&width, &depth);
      info.SetSameKeyWroteResource(0);
      if (!hasType || (GeBuildingAgentTree && GeBuildingAgentTree->IsAncestor(18000, type)) ||
          !CreateBuilding(type, width, IID_BdBuilding, &publication))
        continue;
    }
    if (static_cast<BdBuilding *>(publication)->Init(target, &info))
      InsertBuilding(static_cast<BdBuilding *>(publication));
    static_cast<BdBuilding *>(publication)->Release();
    publication = nullptr;
  }
  if (!result) {
    if (frame.record)
      frame.record->aborted = 1;
    frame.valid = 0;
  }
  return true;
}

// Serialize building-major occupants into the data frame, then write their count and descriptor codec into the header.
// Skip refused occupant saves and preserve the original filter, iterator, close and abort ordering on every result.
bool GeBuildingController::Save(CtCity *target, RsDatabaseSegment *segment) {
  GeBuildingTest *test = new GeBuildingTest;
  test->AddRef();
  OcIterator *iterator = nullptr;
  if (!surface->GetOccupants(&iterator, nullptr, test)) {
    test->Release();
    return false;
  }
  u32 count = 0;
  GeRecord info(1);
  {
    NwRecordFrame frame(target, segment, RsKey(kGeBuildingPayloadRecordType, kGeBuildingRecordGroup, 1), 2, 0);
    RsStream *stream = nullptr;
    u8 result = frame.valid;
    if (result) {
      stream = frame.Stream();
      result = stream != nullptr;
    }
    for (ScOccupant *occupant = iterator->Reset(); occupant; occupant = iterator->Next()) {
      if (!result)
        break;
      result = info.Clear();
      if (result) {
        if (occupant->Save(target, &info))
          result = info.SerialWrite(target, stream);
        else
          --count;
      }
      ++count;
    }
    if (!result) {
      if (frame.record)
        frame.record->aborted = 1;
      frame.valid = 0;
      test->Release();
      ScRelease(&iterator);
      return false;
    }
  }
  test->Release();
  ScRelease(&iterator);
  NwRecordFrame header(target, segment, RsKey(kGeBuildingHeaderRecordType, kGeBuildingRecordGroup, 0), 2, 0);
  RsStream *stream = nullptr;
  u8 result = header.valid;
  if (result) {
    stream = header.Stream();
    result = stream != nullptr;
    if (stream)
      result = stream->WriteU32(count);
  }
  u32 codec = info.GetPersistRecordVersion();
  if (result && stream->WriteU32(codec))
    return true;
  if (header.record)
    header.record->aborted = 1;
  header.valid = 0;
  return false;
}

// Original module static state is established by StaticInit and retired by StaticShutdown.
OcAttribCache *GeBuildingAttribCache;
u8 GeBuildingStaticReady;
GeRandom *GeBuildingRandom;
extern CtAgentTree *GeBuildingAgentTree;

GeBuildingController::GeBuildingController()
    : initialized(0), references(0), surface(nullptr), undergroundOne(nullptr), undergroundTwo(nullptr), zones(nullptr),
      terrain(nullptr), budget(nullptr), demolition(nullptr), neighbors(nullptr), city(nullptr),
      bounds{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}},
      extent{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}} {
}

GeBuildingController::~GeBuildingController() {
}

// Clear the output before selecting the original building, city-layer or change-receiver identity, then retain through
// the complete controller. The factory subobject is deliberately not exposed by this original query table.
bool GeBuildingController::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GeBuildingLayer);
  HANDLE_QUERY_INTERFACE(IID_GeBuildingLayer, GeBuildingLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtChangeReceiver, CtChangeReceiver);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before acquiring its module hold. Return the live object count after the director callback.
u32 GeBuildingController::AddRef() {
  ++references;
  GeGetDirector()->AddRef();
  return references;
}

// Drop a module hold only when the object count was nonzero, then inspect the live count after that callback. A zero
// count destroys the controller directly without implicitly invoking its separate city shutdown operation.
u32 GeBuildingController::Release() {
  if (references) {
    --references;
    GeGetDirector()->RemoveRef();
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool GeBuildingController::DoMessage(ScMessage *) {
  return false;
}

bool GeBuildingController::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool GeBuildingController::SimulationBegin() {
  return true;
}

bool GeBuildingController::SimulationEnd() {
  return true;
}

// Publish the original flag word before the null manipulator output, then report success. No agent-type query occurs.
bool GeBuildingController::GetManipulator(const CtAgentType &, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  *out = nullptr;
  return true;
}

u32 GeBuildingController::GetLayerType() {
  return kGeBuildingControllerLayerType;
}

bool GeBuildingController::DebugClassTag(ScStringView *) {
  return false;
}

bool GeBuildingController::DebugTypeTag(ScStringView *) {
  return false;
}

bool GeBuildingController::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return false;
}

bool GeBuildingController::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return false;
}

bool GeBuildingController::CellChangedAll(u32) {
  return true;
}

bool GeBuildingController::CellChanged(u32, u32, u32) {
  return true;
}

bool GeBuildingController::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

bool GeBuildingController::CreateOccupant(RsKey *key, u32 iid, void **out) {
  return CreateBuilding(*key, iid, out);
}

bool GeBuildingController::CreateOccupant(GeAttrib *attributes, u32 iid, void **out) {
  return CreateBuilding(attributes, iid, out);
}

void GeBuildingController::GetCityCenter(u32 *x, u32 *z) {
  *x = centerX;
  *z = centerZ;
}

u32 GeBuildingController::GetTotalAreaOfBuildings() const {
  return totalArea;
}

// Obtain the live terrain service before querying the building's occupant view. Set altitude from the four outer
// footprint vertices before inserting into selected managers; a later refusal leaves earlier inserts and altitude intact.
bool GeBuildingController::InsertBuilding(BdBuilding *building) {
  ScDirtBag *dirt = city->DirtBag();
  void *value = nullptr;
  if (!building->QueryInterface(IID_ScOccupant, &value))
    return false;
  GeCellRect rectangle;
  static_cast<ScOccupant *>(value)->GetBoundingRectInCellCoords(&rectangle);
  u32 left = rectangle.left, top = rectangle.top, right = rectangle.right + 1, bottom = rectangle.bottom + 1;
  u8 sampled, altitude;
  dirt->GetVertexAltitude(left, top, &sampled);
  altitude = sampled;
  dirt->GetVertexAltitude(left, bottom, &sampled);
  if (sampled < altitude)
    altitude = sampled;
  dirt->GetVertexAltitude(right, top, &sampled);
  if (sampled < altitude)
    altitude = sampled;
  dirt->GetVertexAltitude(right, bottom, &sampled);
  if (sampled < altitude)
    altitude = sampled;
  static_cast<ScOccupant *>(value)->SetAlt(altitude);
  u8 layers = u8(static_cast<ScOccupant *>(value)->GetOccLayerFlag());
  bool result = true;
  if (layers & 1)
    result = surface->InsertOccupant(static_cast<ScOccupant *>(value));
  if (result && (layers & 2))
    result &= undergroundOne->InsertOccupant(static_cast<ScOccupant *>(value));
  if (result && (layers & 4))
    result &= undergroundTwo->InsertOccupant(static_cast<ScOccupant *>(value));
  static_cast<ScOccupant *>(value)->Release();
  return result;
}

// Query the occupant and test each live membership flag before removing it from the corresponding manager. Earlier
// removal callbacks may change later membership, so do not replace the three virtual tests with a captured mask.
bool GeBuildingController::RemoveBuilding(BdBuilding *building) {
  void *value = nullptr;
  if (!building->QueryInterface(IID_ScOccupant, &value))
    return false;
  if (static_cast<ScOccupant *>(value)->TestIsInCityFlag(1))
    surface->RemoveOccupant(static_cast<ScOccupant *>(value));
  if (static_cast<ScOccupant *>(value)->TestIsInCityFlag(2))
    undergroundOne->RemoveOccupant(static_cast<ScOccupant *>(value));
  if (static_cast<ScOccupant *>(value)->TestIsInCityFlag(4))
    undergroundTwo->RemoveOccupant(static_cast<ScOccupant *>(value));
  static_cast<ScOccupant *>(value)->Release();
  return true;
}

// Retain city services in their original order, then publish cell bounds and reset the existing tracking counters.
// Register the change receiver before marking initialization complete; a failed acquisition invokes virtual Shutdown.
bool GeBuildingController::Init(CtCity *value) {
  if (initialized)
    return true;
  if (!value)
    return false;
  city = value;
  value->AddRef();
  centerX = city->CellCountX() >> 1;
  centerZ = city->CellCountZ() >> 1;
  weightedX = weightedZ = totalArea = 0;
  undergroundOne = city->UndergroundOccupantManagerLevelOne();
  if (!undergroundOne)
    goto failed;
  undergroundOne->AddRef();
  undergroundTwo = city->UndergroundOccupantManagerLevelTwo();
  if (!undergroundTwo)
    goto failed;
  undergroundTwo->AddRef();
  zones = city->ZoneLayer();
  if (!zones)
    goto failed;
  zones->AddRef();
  budget = city->BudgetLayer();
  if (!budget)
    goto failed;
  budget->AddRef();
  terrain = city->DirtBag();
  if (!terrain)
    goto failed;
  terrain->AddRef();
  demolition = city->DemolitionLayer();
  if (!demolition)
    goto failed;
  demolition->AddRef();
  neighbors = city->NeighborsLayer();
  if (!neighbors)
    goto failed;
  neighbors->AddRef();
  transit = reinterpret_cast<NwManager *>(city->TransitLayer());
  if (!transit)
    goto failed;
  transit->AddRef();
  surface = city->SurfaceOccupantManager();
  if (!surface)
    goto failed;
  surface->AddRef();
  bounds.minimum = {0, 0, 0};
  {
    u32 maximumZ = (city->CellCountZ() - 1) << kCtMicroCoordinateShift,
        maximumX = (city->CellCountX() << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell;
    bounds.maximum = {i32(maximumX), i32(maximumZ), 0};
    extent = bounds;
    extent.maximum.x = i32(u32(extent.maximum.x) + kCtMicroUnitsPerCell);
    extent.maximum.z = i32(u32(extent.maximum.z) + kCtMicroUnitsPerCell);
  }
  ResetTrackingCounts();
  {
    u32 sender;
    if (surface->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &sender, false)) {
      initialized = 1;
      return true;
    }
  }
failed:
  Shutdown();
  return false;
}

// Clear initialization before traversing the surface grid, removing only buildings and releasing every acquired lookup.
// Detach tracking before releasing retained services in original order, clearing each member only after its callback.
bool GeBuildingController::Shutdown() {
  if (initialized) {
    initialized = 0;
    u32 width = city->CellCountX(), height = city->CellCountZ();
    if (surface) {
      for (u32 x = 0; x < width; ++x) {
        for (u32 z = 0; z < height; ++z) {
          ScOccupant *occupant;
          if (surface->GetOccupantAtCell(x, z, &occupant)) {
            if (occupant->MajorType() == 1)
              surface->RemoveOccupant(occupant);
            occupant->Release();
          }
        }
      }
      surface->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
      surface->Release();
      surface = nullptr;
    }
    ScRelease(&undergroundOne);
    ScRelease(&undergroundTwo);
    ScRelease(&zones);
    ScRelease(&budget);
    ScRelease(&terrain);
    ScRelease(&demolition);
    ScRelease(&neighbors);
    ScRelease(&transit);
    ScRelease(&city);
  }
  return true;
}

// Convert the requested tile rectangle before optionally comparing every outer vertex with the first height. Only
// after the flatness walk succeeds, test bounds and vacancy through the live surface manager in that order.
bool GeBuildingController::IsBuildable(const GeCellRect &rectangle, bool flat) const {
  GeBounds converted(rectangle);
  if (flat) {
    u32 right = rectangle.right + 1, bottom = rectangle.bottom + 1;
    ScDirtBag *dirt = city->DirtBag();
    u8 reference, sampled;
    dirt->GetVertexAltitude(rectangle.left, rectangle.top, &reference);
    for (u32 x = rectangle.left; x <= right; ++x) {
      for (u32 z = rectangle.top; z <= bottom; ++z) {
        dirt->GetVertexAltitude(x, z, &sampled);
        if (sampled != reference)
          return false;
      }
    }
  }
  return surface->IsInBounds(converted) && surface->IsEmpty(converted);
}

// Query the module's retained attribute cache without touching the caller output on refusal. After acquisition, clear
// the output before virtual creation, capture its result and release the live attribute output after that callback.
bool GeBuildingController::CreateBuilding(const RsKey &key, u32 iid, void **out) {
  void *attributes = nullptr;
  if (!GeBuildingAttribCache->GetGenericAttrib(key, IID_GeAttrib, &attributes))
    return false;
  *out = nullptr;
  bool created = CreateBuilding(static_cast<GeAttrib *>(attributes), iid, out);
  static_cast<GeAttrib *>(attributes)->Release();
  return created;
}

// Ask the retained cache to select a key into zero-initialized storage. Only a successful selection invokes the live
// key-based building factory, preserving the caller's original IID and output pointer.
bool GeBuildingController::CreateBuilding(u32 first, u32 second, u32 iid, void **out) {
  RsKey key{};
  if (!GeBuildingAttribCache->GetABuildingOfType(first, second, &key))
    return false;
  return CreateBuilding(key, iid, out);
}

// Acquire the requested building class before clearing the caller output and querying its requested interface. Preserve
// the live escaped factory output across callbacks, balance both references on failed initialization and set birthdate
// only after successful initialization; the date setter's Boolean does not determine creation success.
bool GeBuildingController::CreateBuilding(GeAttrib *attributes, u32 iid, void **out) {
  u32 classId = 0;
  if (!attributes->GetOccupantGZCLSID(&classId))
    return false;
  void *value = nullptr;
  if (!GeGetCom()->GetClassObject(classId, IID_BdBuilding, &value) || !value)
    return false;
  *out = nullptr;
  if (!static_cast<BdBuilding *>(value)->QueryInterface(iid, out)) {
    static_cast<BdBuilding *>(value)->Shutdown();
    static_cast<BdBuilding *>(value)->Release();
    return false;
  }
  if (!static_cast<BdBuilding *>(value)->Init(attributes)) {
    static_cast<BdBuilding *>(value)->Release();
    static_cast<BdBuilding *>(value)->Release();
    *out = nullptr;
    return false;
  }
  if (static_cast<BdBuilding *>(value)->HasBirthdate()) {
    u32 month, day, year;
    city->GetDate(&month, &day, &year);
    GeDate date(month, day, year);
    static_cast<BdBuilding *>(value)->SetBirthdate(date);
  }
  static_cast<BdBuilding *>(value)->Release();
  return true;
}

// Track nonbuilding occupants directly; buildings contribute their footprint to the running center before their agent
// joins simulation. Preserve the original odd-dimension corrections and live queried outputs across callbacks.
bool GeBuildingController::OccupantInserted(u32, ScOccupant *occupant) {
  if (!occupant)
    return false;
  if (!occupant->IsGeomFlag(0x100)) {
    TrackInserted(occupant);
    return true;
  }
  void *value = nullptr;
  if (!occupant->QueryInterface(IID_BdBuilding, &value))
    return false;
  u32 state = static_cast<BdBuilding *>(value)->GetOccupancyState();
  static_cast<BdBuilding *>(value)->Release();
  if (state == 1)
    TrackInserted(occupant);
  GeCellRect rectangle;
  occupant->GetBoundingRectInCellCoords(&rectangle);
  u32 width = rectangle.right - rectangle.left + 1, height = rectangle.bottom - rectangle.top + 1;
  u32 area = width * height, total = totalArea + area;
  weightedX += area * (rectangle.left + ((width - 1) >> 1));
  if (width & 1)
    weightedX += (height * (width - 1)) >> 1;
  weightedZ += area * (rectangle.top + ((height - 1) >> 1));
  if (height & 1)
    weightedZ += (width * (height - 1)) >> 1;
  centerX = weightedX / total;
  centerZ = weightedZ / total;
  totalArea = total;
  CtSimulatorInterface *simulator = city->CitySimulator();
  void *agent;
  if (occupant->QueryInterface(IID_CtAgent, &agent)) {
    simulator->AddAgent(static_cast<CtAgent *>(agent));
    static_cast<CtAgent *>(agent)->Release();
  }
  return true;
}

// Remove tracking and simulation membership before adjusting a live initialized city's center. Removal deliberately
// uses its original width/height corrections, which differ from insertion; an empty total restores half-city coordinates.
bool GeBuildingController::OccupantRemoved(u32, ScOccupant *occupant) {
  if (!occupant)
    return false;
  if (!occupant->IsGeomFlag(0x100)) {
    TrackRemoved(occupant);
    return true;
  }
  void *value = nullptr;
  if (!occupant->QueryInterface(IID_BdBuilding, &value))
    return false;
  u32 state = static_cast<BdBuilding *>(value)->GetOccupancyState();
  static_cast<BdBuilding *>(value)->Release();
  if (state == 1)
    TrackRemoved(occupant);
  CtSimulatorInterface *simulator = city->CitySimulator();
  void *agent;
  if (occupant->QueryInterface(IID_CtAgent, &agent)) {
    simulator->RemoveAgent(static_cast<CtAgent *>(agent));
    static_cast<CtAgent *>(agent)->Release();
  }
  if (initialized) {
    GeCellRect rectangle;
    occupant->GetBoundingRectInCellCoords(&rectangle);
    u32 width = rectangle.right - rectangle.left + 1, height = rectangle.bottom - rectangle.top + 1;
    u32 area = width * height;
    totalArea -= area;
    weightedX -= area * (rectangle.left + ((width - 1) >> 1));
    if (width & 1)
      weightedX -= (width * (width - 1)) >> 1;
    weightedZ -= area * (rectangle.top + ((height - 1) >> 1));
    if (height & 1)
      weightedZ -= (height * (height - 1)) >> 1;
    if (totalArea) {
      centerX = weightedX / totalArea;
      centerZ = weightedZ / totalArea;
    } else {
      centerX = city->CellCountX() >> 1;
      centerZ = city->CellCountZ() >> 1;
    }
  }
  return true;
}

// Delegate to the original module-wide defaults receiver, which owns selection and fallback caching.
GeAttrib *GeBuildingController::GetDefaultAttribData(BdBuilding *building) {
  return GeDefaultBuildings->GetAttrib(building);
}

// Borrow the sprite selected by the module defaults receiver without adding a controller-level fallback or reference.
SpDefinition *GeBuildingController::GetDefaultSpriteData(BdBuilding *building) {
  return GeDefaultBuildings->GetSprite(building);
}

// Clear the module guard before releasing its tree and cache, then destroy random state and the loaded defaults.
// Callback-visible globals remain published until each corresponding release or destruction has completed.
bool GeBuildingController::StaticShutdown() {
  if (GeBuildingStaticReady) {
    GeBuildingStaticReady = 0;
    GeBuildingAgentTree->Release();
    GeBuildingAgentTree = nullptr;
    GeBuildingAttribCache->Release();
    GeBuildingAttribCache = nullptr;
    ScDelete(&GeBuildingRandom);
    ScDelete(&GeDefaultBuildings);
  }
  return true;
}

// Module30928 is retained by building static startup and shutdown, separately from the filter cache.
CtAgentTree *GeBuildingAgentTree;

// Game-specific records replace the original type-to-group and group-to-count trees. Count nodes remain stable while
// ancestry callbacks run, and the count chain preserves unsigned key order.
struct GeTrackedCount {
  u32 type, count;
  GeTrackedCount *next;
};

struct GeTrackedType {
  u32 type;
  ScArray<u32> groups;
  GeTrackedType *next;
};

struct GeTrackingState {
  GeTrackedCount *counts = nullptr;
  GeTrackedType *types = nullptr;

  // Release the two original module-lifetime tables at process teardown. Neither table owns game interfaces.
  ~GeTrackingState() {
    while (types) {
      GeTrackedType *next = types->next;
      delete types;
      types = next;
    }
    while (counts) {
      GeTrackedCount *next = counts->next;
      delete counts;
      counts = next;
    }
  }
};

static GeTrackingState GeTracking;

// Find the original group counter, inserting a zero counter only for the map-subscript operations that require one.
// Stable nodes preserve traversal across calls into the actual agent tree.
static GeTrackedCount *GeFindTrackedCount(u32 type, bool insert) {
  GeTrackedCount **slot = &GeTracking.counts;
  while (*slot && (*slot)->type < type)
    slot = &(*slot)->next;
  if (*slot && (*slot)->type == type)
    return *slot;
  if (!insert)
    return nullptr;
  *slot = new GeTrackedCount{type, 0, *slot};
  return *slot;
}

// Locate a cached occupant-type membership list. A first map subscript publishes its empty list immediately.
static GeTrackedType *GeFindTrackedType(u32 type, bool insert) {
  for (GeTrackedType *entry = GeTracking.types; entry; entry = entry->next)
    if (entry->type == type)
      return entry;
  if (!insert)
    return nullptr;
  GeTrackedType *entry = new GeTrackedType;
  entry->type = type;
  entry->next = GeTracking.types;
  GeTracking.types = entry;
  return entry;
}

void GeBuildingController::ResetTrackingCounts() {
  for (GeTrackedCount *count = GeTracking.counts; count; count = count->next)
    count->count = 0;
}

// Cache a type's matching tracked groups on first insertion, preserving unsigned group order and equality short circuit.
// Increment only those cached counters; registering another group does not rebuild existing type memberships.
bool GeBuildingController::TrackInserted(ScOccupant *occupant) {
  u32 type = occupant->TypeId();
  GeTrackedType *entry = GeFindTrackedType(type, false);
  if (!entry) {
    ScArray<u32> groups;
    for (GeTrackedCount *count = GeTracking.counts; count; count = count->next)
      if (type == count->type || GeBuildingAgentTree->IsAncestor(count->type, type))
        *groups.Append() = count->type;
    entry = GeFindTrackedType(type, true);
    entry->groups.Clear();
    for (u32 i = 0; i < groups.size; ++i)
      *entry->groups.Append() = groups.data[i];
  }
  for (u32 i = 0; i < entry->groups.size; ++i)
    ++GeFindTrackedCount(entry->groups.data[i], true)->count;
  return true;
}

// Decrement the cached groups with full DWORD wraparound. Removal of an unseen type publishes an empty membership
// list, which later insertion deliberately reuses rather than recomputing ancestry.
bool GeBuildingController::TrackRemoved(ScOccupant *occupant) {
  GeTrackedType *entry = GeFindTrackedType(occupant->TypeId(), true);
  for (u32 i = 0; i < entry->groups.size; ++i)
    --GeFindTrackedCount(entry->groups.data[i], true)->count;
  return true;
}

i32 GeBuildingController::CheckOccupantTracking(u32 type) {
  GeTrackedCount *count = GeFindTrackedCount(type, false);
  return count ? i32(count->count) : -1;
}

// Count borrowed occupants from a type-filtered manager iterator and release only a successfully acquired iterator.
u32 GeBuildingController::CountOccupantsOfType(ScOccupantManager *manager, u32 type) {
  OcIterator *iterator = nullptr;
  GeTypeTest *test = new GeTypeTest(type);
  test->AddRef();
  if (!manager->GetOccupants(&iterator, nullptr, test) || !iterator)
    return 0;
  u32 count = 0;
  for (ScOccupant *occupant = iterator->Current(); occupant; occupant = iterator->Next())
    ++count;
  iterator->Release();
  return count;
}

// Seed a previously unregistered type from the three live occupant managers in surface-to-underground order.
// Existing registrations return success without recounting or invalidating cached type memberships.
bool GeBuildingController::StartTrackingOccupantCount(u32 type) {
  if (!GeFindTrackedCount(type, false)) {
    u32 count = CountOccupantsOfType(surface, type);
    count += CountOccupantsOfType(undergroundOne, type);
    count += CountOccupantsOfType(undergroundTwo, type);
    GeFindTrackedCount(type, true)->count = count;
  }
  return true;
}

static void GeReadTrackingGroup(const ScString &key, const ScString &, void *) {
  u32 type = GeParseNumber(key.ToChar());
  GeFindTrackedCount(type, true)->count = 0;
}

// Directory lookup occurs separately for each path; loading rows resets existing group counts without clearing cached
// type memberships.
u8 GeBuildingController::LoadTrackingGroups() {
  GeIni reader;
  ScString file(GeGetFileSystem()->DataDirectory());
  file.Append("Sys\\SC3BuildingLayer.ini", 24);
  ScString package(GeGetFileSystem()->DataDirectory());
  package.Append("Sys\\SYS.PAK", 11);
  reader.SetPath(file);
  reader.SetCompressedPath(package);
  ScString section("BUILDING_TRACKING_GROUPS");
  return reader.EnumEntries(section, GeReadTrackingGroup, static_cast<GeBuildingLayer *>(this));
}

// Eight-byte tile mapping storage: a hexadecimal instance and a signed scan slot whose byte4 view is clamped.
// The caller consumes the instance; untouched upper bytes of the second slot are not read by this implementation.
struct GeImportTile {
  u32 instance;
  i32 field04;
};

static_assert(sizeof(GeImportTile) == 8);

// Keep the external decimal index within atoi's defined signed32 domain before applying its original unsigned range test.
// Overflow has no original game value to reproduce and stops at this native input boundary.
static u32 GeReadImportIndex(const char *text) {
  i64 value = strtoll(text, nullptr, 10);
  if (value < INT32_MIN || value > INT32_MAX)
    ScFatal(SC_ERROR_UNSUPPORTED, "Imported tile index is outside signed32 range.");
  return u32(value);
}

// Locate the two original scanf items without assigning narrow integers, then reject their undefined overflow domains.
static void GeCheckImportValue(const char *text) {
  intmax_t firstEnd = -1, secondStart = -1, secondEnd = -1;
#if defined(_MSC_VER)
  sscanf_s(text, "%*X%jn %jn%*d%jn", &firstEnd, &secondStart, &secondEnd);
#else
  sscanf(text, "%*X%jn %jn%*d%jn", &firstEnd, &secondStart, &secondEnd);
#endif
  if (firstEnd >= 0) {
    const char *digits = text;
    while (isspace(u8(*digits)))
      ++digits;
    if (*digits == '+' || *digits == '-')
      ++digits;
    if (strtoull(digits, nullptr, 16) > UINT32_MAX)
      ScFatal(SC_ERROR_UNSUPPORTED, "Imported tile instance is outside unsigned32 range.");
  }
  if (secondEnd >= 0) {
    i64 value = strtoll(text + usize(secondStart), nullptr, 10);
    if (value < INT32_MIN || value > INT32_MAX)
      ScFatal(SC_ERROR_UNSUPPORTED, "Imported tile option is outside signed32 range.");
  }
}

// Parse a bounded tile index and its hexadecimal instance, retaining the original low-byte option truncation and clamp.
static void GeParseImportTileRow(const ScString &key, const ScString &value, void *context) {
  ScString input(value);
  u32 index;
  {
    ScString label(key);
    index = GeReadImportIndex(label.ToChar());
  }
  if (index < 256) {
    GeImportTile *entry = static_cast<GeImportTile *>(context) + index;
    GeCheckImportValue(input.ToChar());
    i32 field04 = 0, assigned;
#if defined(_MSC_VER)
    assigned = sscanf_s(input.ToChar(), "%X %d", &entry->instance, &field04);
#else
    assigned = sscanf(input.ToChar(), "%X %d", &entry->instance, &field04);
#endif
    if (assigned == 2)
      entry->field04 = field04;
    u8 *byte = reinterpret_cast<u8 *>(&entry->field04);
    if (*byte > 3)
      *byte = 0;
  }
}

// Populate the attached building layer from legacy tile mappings, using the original footprint exclusions and scan
// order.
bool GeBuildingController::Init(CtCity *target, ScImporter *importer) {
  static constexpr u8 spans[144] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
      3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 4,
      4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4,
      4, 4};
  bool allowed = true;
  RsKey key{};
  void *occupantPublication, *buildingPublication;
  u8 tile, zone;
  if (!importer->MapIsPresent(7)) {
    Shutdown();
    return false;
  }
  GeImportTile *mapping = new GeImportTile[256];
  for (u32 i = 0; i < 256; ++i) {
    mapping[i].instance = UINT32_MAX;
    *reinterpret_cast<u8 *>(&mapping[i].field04) = UINT8_MAX;
  }
  GeIni reader;
  ScString file(GeGetFileSystem()->DataDirectory());
  file.Append("Sys\\Tiles.ini", 13);
  ScString package(GeGetFileSystem()->DataDirectory());
  package.Append("Sys\\SYS.PAK", 11);
  reader.SetPath(file);
  reader.SetCompressedPath(package);
  {
    ScString section("Tiles");
    reader.EnumEntries(section, GeParseImportTileRow, mapping);
  }
  i32 side = importer->GetMapSize(kCtLegacyXbldPlane);
  u8 fill = 0;
  GeFlagGrid cells(u32(side), u32(side), &fill);
  target->GetTileSize();
  for (i32 x = side - 1; x >= 0; --x)
    for (i32 z = side - 1; z >= 0; --z) {
      importer->GetValue(kCtLegacyXbldPlane, u32(x), u32(z), &tile);
      cells.columns[x][z] = tile;
    }
  for (i32 x = 0; x < side; ++x)
    for (i32 z = 0; z < side; ++z) {
      tile = cells.columns[x][z];
      if ((tile < 0x70 && tile != 13) || (tile >= 0x88 && tile <= 0x8b) || (tile >= 0xa6 && tile <= 0xad) ||
          (tile >= 0xc2 && tile <= 0xc5))
        continue;
      importer->GetZoneValue(u32(x), u32(z), &zone);
      if (zone >= 7)
        continue;
      i32 span = tile < 0x70 ? 1 : spans[tile - 0x70];
      key.type = kRsBuildingDataResourceType;
      key.group = 1406;
      key.instance = mapping[tile].instance;
      if (span + x - 1 >= side || span + z - 1 >= side)
        continue;
      allowed = CreateBuilding(key, IID_ScOccupant, &occupantPublication);
      if (allowed) {
        static_cast<ScOccupant *>(occupantPublication)->SetLocationInCellCoords(u32(x), u32(z));
        buildingPublication = nullptr;
        if (static_cast<ScOccupant *>(occupantPublication)->QueryInterface(IID_BdBuilding, &buildingPublication)) {
          static_cast<BdBuilding *>(buildingPublication)->SetOccupancyState(1);
          allowed = InsertBuilding(static_cast<BdBuilding *>(buildingPublication));
          static_cast<BdBuilding *>(buildingPublication)->Release();
          buildingPublication = nullptr;
        }
        static_cast<ScOccupant *>(occupantPublication)->Release();
      }
      if (span > 1)
        for (i32 dx = 0; dx < span; ++dx)
          for (i32 dz = 0; dz < span; ++dz)
            cells.columns[x + dx][z + dz] = 0;
    }
  delete[] mapping;
  return allowed;
}

// Construct the building controller and return its primary building-layer identity to the module registry.
ScUnknown *GeCreateBuildingController() {
  return static_cast<GeBuildingLayer *>(new GeBuildingController);
}

extern GeRandom *GeBuildingRandom;
extern CtAgentTree *GeBuildingAgentTree;

// Resolve and expand the requested news text, retaining its independent heap String across synchronous delivery.
// Return the identifier written into the stack payload, ignoring the message server's own reply.
u32 SC3_STDCALL GeRequestDialog(u32 instance) {
  GeTextResource resource(instance, kRsNewsTextGroup);
  ScString expanded;
  resource.Detokenize(&expanded);
  ScString *text = new ScString(expanded);
  text->AddRef();
  GeDialogRequest request{0, kRsNewsTextGroup, instance, text, {0, 0, 0, 0}};
  ScMessage message{kApMsgCreateNotification, reinterpret_cast<uptr>(&request), 0, 0};
  GeGetMessageServer()->MessageSend(message);
  text->Release();
  return request.identifier;
}

u32 GeEmitLandmarkNotify(u32 instance, ScOccupant *occupant) {
  ScString expanded;
  RsKey nameKey{};
  occupant->GetAttribData()->GetNameKey(&nameKey);
  GeTextToken token(1, ScString("%LANDMARK%"), nameKey.instance, nameKey.group);
  GeTextToken *tokens = &token;
  GeTextResource resource(instance, kRsNewsTextGroup);
  resource.Detokenize(&expanded, &tokens, 1);
  ScString *text = new ScString(expanded);
  text->AddRef();
  GeDialogRequest request{0, kRsNewsTextGroup, instance, text, {0, 0, 0, 0}};
  ScMessage message{kApMsgCreateNotification, reinterpret_cast<uptr>(&request), 0, 0};
  GeGetMessageServer()->MessageSend(message);
  text->Release();
  return request.identifier;
}

// Select the original building-specific news, preserving repeated live count queries and the two random variants.
// Unselected buildings fall through to the landmark ancestor test and its independent randomized template.
u32 GeBuildingController::NotifyPlacedBuilding(BdBuilding *building) {
  u32 type = building->AsISC3Occupant()->TypeId(), instance = 0;
  switch (type) {
  case 1001:
    instance = 681;
    break;
  case 1002:
    instance = 692;
    break;
  case 10110:
    instance = 676;
    break;
  case 10120:
    instance = 677;
    break;
  case 11001:
    instance = 683;
    break;
  case 11002:
    instance = 682;
    break;
  case 12210:
    if (CheckOccupantTracking(12210) == 1)
      instance = 522;
    else if (CheckOccupantTracking(12210) == 10)
      instance = 523;
    break;
  case 12350:
    instance = 693;
    break;
  case 12810:
    if (CheckOccupantTracking(12810) == 1)
      instance = 699;
    break;
  case 12820:
    instance = 702;
    break;
  case 12830:
    instance = 701;
    break;
  case 12840:
    instance = 700;
    break;
  case 12851:
    if (CheckOccupantTracking(12851) == 1)
      instance = 704;
    break;
  case 12852:
    if (CheckOccupantTracking(12852) == 1)
      instance = 703;
    break;
  case 13002:
    if (CheckOccupantTracking(13002) == 1)
      instance = 697;
    break;
  case 13010:
    if (CheckOccupantTracking(13010) == 1)
      instance = 694;
    break;
  case 13130:
    if (CheckOccupantTracking(13130) == 1)
      instance = 698;
    break;
  case 13310:
    if (CheckOccupantTracking(13310) == 1)
      instance = 696;
    break;
  case 13871:
    instance = 690;
    break;
  case 13872:
    instance = 687;
    break;
  case 13873:
    if (CheckOccupantTracking(13873) == 1)
      instance = 695;
    break;
  case 13874:
    instance = 688;
    break;
  case 14003:
    instance = 684;
    break;
  case 14170:
    instance = 689;
    break;
  case 14312:
    instance = 669;
    break;
  case 14313:
    instance = 670;
    break;
  case 14314:
    instance = 671;
    break;
  case 14325:
    instance = 691;
    break;
  case 14370:
    instance = 686;
    break;
  case 14430:
    instance = 673;
    break;
  case 14450:
    instance = 672;
    break;
  case 14550:
    instance = 685;
    break;
  case 14600:
    instance = 674;
    break;
  case 19001:
    instance = 678;
    break;
  case 19002:
    instance = 680;
    break;
  case 19003:
    instance = 675;
    break;
  case 19004:
    instance = 679;
    break;
  case 20001: {
    u32 choice = GeBuildingRandom->Next(3);
    instance = choice == 1 ? 1003 : choice == 2 ? 1004 : 1000;
    break;
  }
  case 20002: {
    u32 choice = GeBuildingRandom->Next(3);
    instance = choice == 1 ? 1007 : choice == 2 ? 1008 : 1006;
    break;
  }
  }
  if (instance)
    return GeRequestDialog(instance);
  u32 landmark = GeBuildingAgentTree->IsAncestor(18000, type);
  if (landmark) {
    u32 headline = GeBuildingRandom->Next(12) + 508;
    return GeEmitLandmarkNotify(headline, building->AsISC3Occupant());
  }
  return landmark;
}

extern OcAttribCache *GeBuildingAttribCache;

// Resolve a definition and check its footprint, then optionally check funds, create and position its retained building.
u8 GeBuildingController::CheckPlaceBuilding(const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price,
    bool freeOfCharge, i32 mode, bool apply) {
  void *occupantPublication = nullptr, *publication = nullptr;
  if (!GeBuildingAttribCache->GetGenericAttrib(key, IID_GeAttrib, &publication))
    return 0;
  u32 type = static_cast<GeAttrib *>(publication)->TypeId();
  u8 width = u8(static_cast<GeAttrib *>(publication)->SizeInCellsX()),
     depth = u8(static_cast<GeAttrib *>(publication)->SizeInCellsZ());
  static_cast<GeAttrib *>(publication)->Release();
  publication = nullptr;
  GeBounds footprint{position, position};
  footprint.maximum.x = i32(u32(footprint.maximum.x) + (u32(u16(width - 1)) << kCtMicroCoordinateShift));
  footprint.maximum.z = i32(u32(footprint.maximum.z) + (u32(u16(depth - 1)) << kCtMicroCoordinateShift));
  if (footprint.minimum.x < bounds.minimum.x || footprint.minimum.z < bounds.minimum.z ||
      footprint.minimum.x > bounds.maximum.x || footprint.minimum.z > bounds.maximum.z ||
      footprint.maximum.x < bounds.minimum.x || footprint.maximum.z < bounds.minimum.z ||
      footprint.maximum.x > bounds.maximum.x || footprint.maximum.z > bounds.maximum.z)
    return 0;
  u8 altitude, result = CheckBuildingSite(&footprint, type, affected, price, &altitude, freeOfCharge, mode);
  if (result && apply) {
    i64 funds = budget->GetTotalFunds();
    if (!freeOfCharge && *price && i64(*price) > funds)
      result = 0;
    if (result) {
      void *buildingPublication = nullptr;
      ScArray<BdBuilding *> buildings;
      result = CreateBuilding(key, IID_BdBuilding, &buildingPublication);
      *buildings.Append() = static_cast<BdBuilding *>(buildingPublication);
      if (result) {
        if (static_cast<BdBuilding *>(buildingPublication)->QueryInterface(IID_ScOccupant, &occupantPublication)) {
          static_cast<ScOccupant *>(occupantPublication)->SetLocation(footprint.minimum);
          static_cast<ScOccupant *>(occupantPublication)->Release();
          occupantPublication = nullptr;
        }
        // Shore placement never reads its unwritten terrain altitude; supply an unused defined byte at this boundary.
        result = PlaceBuilding(&footprint, &buildings, affected, *price, mode == 1 ? 0 : altitude, freeOfCharge, mode);
        for (u32 i = 0; i < buildings.size; ++i) {
          buildings.data[i]->Release();
          buildings.data[i] = nullptr;
        }
      }
    }
  }
  return result;
}

// Resolve all four definitions before testing their combined footprint and retain all successful building publications.
u8 GeBuildingController::CheckPlaceBuilding(const CtCoord &position, const RsKey &first, const RsKey &second,
    const RsKey &third, const RsKey &fourth, GeBounds *affected, u32 *price, bool freeOfCharge, i32 mode, bool apply) {
  void *firstPublication = nullptr, *secondPublication = nullptr, *thirdPublication = nullptr,
       *fourthPublication = nullptr, *occupantPublication = nullptr;
  bool firstFound = GeBuildingAttribCache->GetGenericAttrib(first, IID_GeAttrib, &firstPublication),
       secondFound = GeBuildingAttribCache->GetGenericAttrib(second, IID_GeAttrib, &secondPublication),
       thirdFound = GeBuildingAttribCache->GetGenericAttrib(third, IID_GeAttrib, &thirdPublication),
       fourthFound = GeBuildingAttribCache->GetGenericAttrib(fourth, IID_GeAttrib, &fourthPublication);
  if (!firstFound || !secondFound || !thirdFound || !fourthFound) {
    if (firstPublication) {
      static_cast<GeAttrib *>(firstPublication)->Release();
      firstPublication = nullptr;
    }
    if (secondPublication) {
      static_cast<GeAttrib *>(secondPublication)->Release();
      secondPublication = nullptr;
    }
    if (thirdPublication) {
      static_cast<GeAttrib *>(thirdPublication)->Release();
      thirdPublication = nullptr;
    }
    if (fourthPublication)
      static_cast<GeAttrib *>(fourthPublication)->Release();
    return 0;
  }
  u32 type = static_cast<GeAttrib *>(firstPublication)->TypeId();
  u8 firstWidth = u8(static_cast<GeAttrib *>(firstPublication)->SizeInCellsX()),
     firstDepth = u8(static_cast<GeAttrib *>(firstPublication)->SizeInCellsZ()),
     secondWidth = u8(static_cast<GeAttrib *>(secondPublication)->SizeInCellsX()),
     thirdDepth = u8(static_cast<GeAttrib *>(thirdPublication)->SizeInCellsZ());
  if (firstPublication) {
    static_cast<GeAttrib *>(firstPublication)->Release();
    firstPublication = nullptr;
  }
  if (secondPublication) {
    static_cast<GeAttrib *>(secondPublication)->Release();
    secondPublication = nullptr;
  }
  if (thirdPublication) {
    static_cast<GeAttrib *>(thirdPublication)->Release();
    thirdPublication = nullptr;
  }
  if (fourthPublication) {
    static_cast<GeAttrib *>(fourthPublication)->Release();
    fourthPublication = nullptr;
  }
  GeBounds footprint{position, position};
  footprint.maximum.x =
      i32(u32(footprint.maximum.x) + (u32(u16(firstWidth + secondWidth - 1)) << kCtMicroCoordinateShift));
  footprint.maximum.z =
      i32(u32(footprint.maximum.z) + (u32(u16(firstDepth + thirdDepth - 1)) << kCtMicroCoordinateShift));
  if (footprint.minimum.x < bounds.minimum.x || footprint.minimum.z < bounds.minimum.z ||
      footprint.minimum.x > bounds.maximum.x || footprint.minimum.z > bounds.maximum.z ||
      footprint.maximum.x < bounds.minimum.x || footprint.maximum.z < bounds.minimum.z ||
      footprint.maximum.x > bounds.maximum.x || footprint.maximum.z > bounds.maximum.z)
    return 0;
  u8 altitude, result = CheckBuildingSite(&footprint, type, affected, price, &altitude, freeOfCharge, mode);
  if (result && apply && *price) {
    i64 funds = budget->GetTotalFunds();
    if (i64(*price) > funds)
      result = 0;
  }
  if (result && apply) {
    void *firstBuilding = nullptr, *secondBuilding = nullptr, *thirdBuilding = nullptr, *fourthBuilding = nullptr;
    ScArray<BdBuilding *> buildings;
    bool firstCreated = CreateBuilding(first, IID_BdBuilding, &firstBuilding),
         secondCreated = CreateBuilding(second, IID_BdBuilding, &secondBuilding),
         thirdCreated = CreateBuilding(third, IID_BdBuilding, &thirdBuilding),
         fourthCreated = CreateBuilding(fourth, IID_BdBuilding, &fourthBuilding);
    if (!firstCreated || !secondCreated || !thirdCreated || !fourthCreated) {
      if (firstBuilding) {
        static_cast<BdBuilding *>(firstBuilding)->Release();
        firstBuilding = nullptr;
      }
      if (secondBuilding) {
        static_cast<BdBuilding *>(secondBuilding)->Release();
        secondBuilding = nullptr;
      }
      if (thirdBuilding) {
        static_cast<BdBuilding *>(thirdBuilding)->Release();
        thirdBuilding = nullptr;
      }
      if (fourthBuilding) {
        static_cast<BdBuilding *>(fourthBuilding)->Release();
        fourthBuilding = nullptr;
      }
      result = 0;
    }
    if (result) {
      *buildings.Append() = static_cast<BdBuilding *>(firstBuilding);
      *buildings.Append() = static_cast<BdBuilding *>(secondBuilding);
      *buildings.Append() = static_cast<BdBuilding *>(thirdBuilding);
      *buildings.Append() = static_cast<BdBuilding *>(fourthBuilding);
    }
    CtCoord firstPosition = position, secondPosition = position, thirdPosition = position, fourthPosition = position;
    secondPosition.x = i32(u32(position.x) + (u32(firstWidth) << kCtMicroCoordinateShift));
    thirdPosition.z = i32(u32(position.z) + (u32(firstDepth) << kCtMicroCoordinateShift));
    fourthPosition.x = secondPosition.x;
    fourthPosition.z = thirdPosition.z;
    if (result) {
      if (static_cast<BdBuilding *>(firstBuilding)->QueryInterface(IID_ScOccupant, &occupantPublication)) {
        static_cast<ScOccupant *>(occupantPublication)->SetLocation(firstPosition);
        static_cast<ScOccupant *>(occupantPublication)->Release();
        occupantPublication = nullptr;
      }
      if (static_cast<BdBuilding *>(secondBuilding)->QueryInterface(IID_ScOccupant, &occupantPublication)) {
        static_cast<ScOccupant *>(occupantPublication)->SetLocation(secondPosition);
        static_cast<ScOccupant *>(occupantPublication)->Release();
        occupantPublication = nullptr;
      }
      if (static_cast<BdBuilding *>(thirdBuilding)->QueryInterface(IID_ScOccupant, &occupantPublication)) {
        static_cast<ScOccupant *>(occupantPublication)->SetLocation(thirdPosition);
        static_cast<ScOccupant *>(occupantPublication)->Release();
        occupantPublication = nullptr;
      }
      if (static_cast<BdBuilding *>(fourthBuilding)->QueryInterface(IID_ScOccupant, &occupantPublication)) {
        static_cast<ScOccupant *>(occupantPublication)->SetLocation(fourthPosition);
        static_cast<ScOccupant *>(occupantPublication)->Release();
        occupantPublication = nullptr;
      }
      result = PlaceBuilding(&footprint, &buildings, affected, *price, mode == 1 ? 0 : altitude, freeOfCharge, mode);
      for (u32 i = 0; i < buildings.size; ++i) {
        buildings.data[i]->Release();
        buildings.data[i] = nullptr;
      }
    }
  }
  return result;
}

bool GeBuildingController::PlaceBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) {
  return CheckPlaceBuilding(position, key, affected, price, freeOfCharge, 0, 1) != 0;
}

bool GeBuildingController::PlaceShorlineBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) {
  return CheckPlaceBuilding(position, key, affected, price, freeOfCharge, 1, 1) != 0;
}

bool GeBuildingController::PlacePlumbingBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) {
  return CheckPlaceBuilding(position, key, affected, price, freeOfCharge, 2, 1) != 0;
}

bool GeBuildingController::PlaceSubwayBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price, bool freeOfCharge) {
  return CheckPlaceBuilding(position, key, affected, price, freeOfCharge, 3, 1) != 0;
}

bool GeBuildingController::PlaceMultiBuilding(const CtCoord &position, const RsKey &first, const RsKey &second,
    const RsKey &third, const RsKey &fourth, GeBounds *affected, u32 *price, bool freeOfCharge) {
  return CheckPlaceBuilding(position, first, second, third, fourth, affected, price, freeOfCharge, 0, 1) != 0;
}

// Check water and shoreline constraints, query terrain leveling and demolition permission, then inspect surface
// removals.
u8 GeBuildingController::CheckBuildingSite(
    const GeBounds *footprint, u32 type, GeBounds *affected, u32 *price, u8 *altitude, bool freeOfCharge, i32 mode) {
  u8 allowed = 1;
  OcIterator *iterator = nullptr;
  u32 terrainPrice = 0, removalPrice = 0;
  GeBounds leveling = *footprint;
  *price = 0;
  leveling.maximum.x = i32(u32(leveling.maximum.x) + kCtMicroUnitsPerCell);
  leveling.maximum.z = i32(u32(leveling.maximum.z) + kCtMicroUnitsPerCell);
  u32 unitPrice = u32(budget->GetCost(kEcCostDemolish));
  if (mode == 0 || mode == 2 || mode == 3) {
    u32 lastX = u32(footprint->maximum.x >> kCtMicroCoordinateShift);
    bool water = false;
    for (u32 x = u32(footprint->minimum.x >> kCtMicroCoordinateShift); x <= lastX && !water; ++x) {
      u32 lastZ = u32(footprint->maximum.z >> kCtMicroCoordinateShift);
      for (u32 z = u32(footprint->minimum.z >> kCtMicroCoordinateShift); z <= lastZ && !water; ++z)
        water = terrain->IsWater(x, z);
    }
    if (water)
      allowed = 0;
  } else if (mode == 1) {
    u32 lastX = u32(footprint->maximum.x >> kCtMicroCoordinateShift);
    bool refused = false;
    for (u32 x = u32(footprint->minimum.x >> kCtMicroCoordinateShift); x <= lastX && !refused; ++x) {
      u32 lastZ = u32(footprint->maximum.z >> kCtMicroCoordinateShift);
      for (u32 z = u32(footprint->minimum.z >> kCtMicroCoordinateShift); z <= lastZ && !refused; ++z) {
        u32 waterVertices = terrain->CellWaterVertCount(x, z);
        refused = waterVertices != 4;
        if (waterVertices == 4 && terrain->InVertexBounds(x, z) && terrain->InVertexBounds(x + 1, z + 1)) {
          u8 dirt, water;
          terrain->GetVertexAltitudeDirt(x, z, &dirt);
          terrain->GetVertexAltitudeWater(x, z, &water);
          if (water <= dirt)
            refused = true;
          terrain->GetVertexAltitudeDirt(x + 1, z, &dirt);
          terrain->GetVertexAltitudeWater(x + 1, z, &water);
          if (water <= dirt)
            refused = true;
          terrain->GetVertexAltitudeDirt(x + 1, z + 1, &dirt);
          terrain->GetVertexAltitudeWater(x + 1, z + 1, &water);
          if (water <= dirt)
            refused = true;
          terrain->GetVertexAltitudeDirt(x, z + 1, &dirt);
          terrain->GetVertexAltitudeWater(x, z + 1, &water);
          if (water <= dirt)
            refused = true;
        }
      }
    }
    if (refused) {
      allowed = 0;
    } else {
      u32 firstX = u32(footprint->minimum.x >> kCtMicroCoordinateShift),
          firstZ = u32(footprint->minimum.z >> kCtMicroCoordinateShift);
      u32 maximumX = u32(footprint->maximum.x >> kCtMicroCoordinateShift),
          maximumZ = u32(footprint->maximum.z >> kCtMicroCoordinateShift);
      GeBounds surroundings = *footprint;
      surroundings.minimum.x = i32(u32(surroundings.minimum.x) - 512);
      surroundings.minimum.z = i32(u32(surroundings.minimum.z) - 512);
      surroundings.maximum.x = i32(u32(surroundings.maximum.x) + 512);
      surroundings.maximum.z = i32(u32(surroundings.maximum.z) + 512);
      if (surroundings.minimum.x < bounds.minimum.x)
        surroundings.minimum.x = bounds.minimum.x;
      if (surroundings.minimum.z < bounds.minimum.z)
        surroundings.minimum.z = bounds.minimum.z;
      if (surroundings.maximum.x > bounds.maximum.x)
        surroundings.maximum.x = bounds.maximum.x;
      if (surroundings.maximum.z > bounds.maximum.z)
        surroundings.maximum.z = bounds.maximum.z;
      surroundings.minimum.altitude = kCtUnknownCoordinate;
      surroundings.maximum.altitude = kCtUnknownCoordinate;
      bool shore = false;
      u32 endX = u32(surroundings.maximum.x >> kCtMicroCoordinateShift),
          endZ = u32(surroundings.maximum.z >> kCtMicroCoordinateShift);
      for (u32 x = u32(surroundings.minimum.x >> kCtMicroCoordinateShift); x <= endX && !shore; ++x)
        for (u32 z = u32(surroundings.minimum.z >> kCtMicroCoordinateShift); z <= endZ && !shore; ++z)
          if ((x < firstX || x > maximumX) && (z < firstZ || z > maximumZ))
            shore = terrain->CellWaterVertCount(x, z) != 4;
      if (!shore)
        allowed = 0;
    }
  }
  bool levelRefused = false;
  if (mode != 1 && allowed) {
    levelRefused = !terrain->GetOptimalLevel(leveling, altitude, affected, &terrainPrice);
    u32 demolitionPrice = 0;
    GeBounds demolished = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
    SpSelection *selection = nullptr;
    if (!demolition->CanDemolishFromDirtChange(affected, 1, &demolitionPrice, &demolished, &selection, 1))
      allowed = 0;
    if (selection)
      selection->Release();
  }
  if (allowed) {
    surface->GetOccupants(&iterator, footprint, nullptr);
    if (iterator->Current())
      do {
        ScOccupant *occupant = iterator->Current();
        if (occupant) {
          if (!occupant->IsGeomFlag(1024) &&
              (!occupant->IsGeomFlag(kOcGeomPower) || !occupant->IsGeomFlag(kOcGeomNetwork))) {
            allowed = 0;
            break;
          }
          if (occupant->CanUserRemove())
            removalPrice += unitPrice;
          else
            allowed = 0;
        }
        if (!allowed)
          break;
      } while (iterator->Next());
    iterator->Release();
    iterator = nullptr;
  }
  if (!freeOfCharge)
    *price += u32(budget->GetCost(type));
  *price += terrainPrice + removalPrice;
  if (allowed && levelRefused)
    return 0;
  return allowed;
}

// The last insertion decides success; refusal removes every supplied building in reverse without undoing earlier
// effects.
u8 GeBuildingController::PlaceBuilding(const GeBounds *footprint, ScArray<BdBuilding *> *buildings, GeBounds *affected,
    u32 quotedPrice, u8 altitude, bool freeOfCharge, i32 mode) {
  GeBounds leveling = *footprint;
  leveling.maximum.x = i32(u32(leveling.maximum.x) + kCtMicroUnitsPerCell);
  leveling.maximum.z = i32(u32(leveling.maximum.z) + kCtMicroUnitsPerCell);
  u8 result = 1;
  u32 terrainPrice = 0;
  OcIterator *iterator = nullptr;
  if (mode != 1) {
    result = terrain->LevelTerrain(leveling, altitude, affected, &terrainPrice, true);
    u32 demolitionPrice;
    GeBounds demolished{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
    demolition->DemolishFromDirtChange(affected, 1, freeOfCharge, &demolitionPrice, &demolished, true);
  }
  ScArray<ScOccupant *> surfaceRemovals, networkRemovals;
  if (result) {
    surface->GetOccupants(&iterator, footprint, nullptr);
    if (iterator->Current())
      do {
        ScOccupant *occupant = iterator->Current();
        if (occupant) {
          if (occupant->IsGeomFlag(1024) && occupant->CanUserRemove()) {
            occupant->AddRef();
            *surfaceRemovals.Append() = occupant;
          }
          if (occupant->IsGeomFlag(kOcGeomNetwork) && occupant->IsGeomFlag(kOcGeomPower) && occupant->CanUserRemove()) {
            occupant->AddRef();
            *networkRemovals.Append() = occupant;
          }
        }
      } while (iterator->Next());
    iterator->Release();
    iterator = nullptr;
  }
  if (result)
    for (u32 i = 0, count = surfaceRemovals.size; i < count; ++i) {
      surface->RemoveOccupant(surfaceRemovals.data[i]);
      surfaceRemovals.data[i]->Release();
    }
  surfaceRemovals.Clear();
  GeBounds networkBounds{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  if (networkRemovals.size) {
    networkRemovals.data[0]->GetLocation(&position);
    networkBounds.minimum = networkBounds.maximum = position;
  }
  bool removedNetwork = result && networkRemovals.size;
  if (removedNetwork)
    for (u32 i = 0, count = networkRemovals.size; i < count; ++i) {
      u32 x, z;
      networkRemovals.data[i]->GetLocationInCellCoords(&x, &z);
      if (neighbors->HasConnection(3, i32(x), i32(z)))
        transit->RemoveConnectionInteractive(surface, networkRemovals.data[i]);
      else
        surface->RemoveOccupant(networkRemovals.data[i]);
      networkRemovals.data[i]->GetLocation(&position);
      networkBounds.UnionXZ(networkBounds, position);
      networkRemovals.data[i]->Release();
    }
  networkRemovals.Clear();
  if (removedNetwork)
    transit->RefreshNetwork(networkBounds, 32);
  if (result) {
    if (mode == 2 || mode == 3) {
      ScArray<ScOccupant *> removals;
      ScOccupantManager *manager = mode == 2 ? undergroundOne : undergroundTwo;
      manager->GetOccupants(&iterator, footprint, nullptr);
      if (iterator->Current())
        do {
          ScOccupant *occupant = iterator->Current();
          if (occupant && occupant->IsGeomFlag(kOcGeomNetwork) &&
              occupant->IsGeomFlag(mode == 2 ? kOcGeomPipe : kOcGeomSubway) && occupant->CanUserRemove()) {
            occupant->AddRef();
            *removals.Append() = occupant;
          }
        } while (iterator->Next());
      iterator->Release();
      iterator = nullptr;
      for (u32 i = 0, count = removals.size; i < count; ++i) {
        (mode == 2 ? undergroundOne : undergroundTwo)->RemoveOccupant(removals.data[i]);
        removals.data[i]->Release();
      }
      removals.Clear();
    }
    for (u32 i = 0; i < buildings->size; ++i) {
      buildings->data[i]->SetOccupancyState(1);
      result = InsertBuilding(buildings->data[i]);
      if (result && !i)
        NotifyPlacedBuilding(buildings->data[0]);
    }
    if (result) {
      if (mode == 2 || mode == 3)
        transit->RefreshNetwork(*footprint, mode == 2 ? 8 : 16);
      if (mode == 1) {
        i32 lastX = footprint->maximum.x >> kCtMicroCoordinateShift,
            firstZ = footprint->minimum.z >> kCtMicroCoordinateShift,
            lastZ = footprint->maximum.z >> kCtMicroCoordinateShift;
        for (i32 x = footprint->minimum.x >> kCtMicroCoordinateShift; x <= lastX; ++x)
          for (i32 z = firstZ; z <= lastZ; ++z)
            terrain->BlockCell(u32(x), u32(z), true);
      }
      u8 zone = 22;
      zones->SetValue(u32(footprint->minimum.x >> kCtMicroCoordinateShift),
          u32(footprint->minimum.z >> kCtMicroCoordinateShift), u32(footprint->maximum.x >> kCtMicroCoordinateShift),
          u32(footprint->maximum.z >> kCtMicroCoordinateShift), &zone);
      if (!freeOfCharge)
        budget->WithdrawFunds(quotedPrice);
    } else {
      u32 count = buildings->size;
      while (count)
        RemoveBuilding(buildings->data[--count]);
    }
  }
  return result;
}

bool GeBuildingController::CanPlaceBuilding(const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) {
  return CheckPlaceBuilding(position, key, affected, price, 0, 0, 0) != 0;
}

bool GeBuildingController::CanPlaceShorelineBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) {
  return CheckPlaceBuilding(position, key, affected, price, 0, 1, 0) != 0;
}

bool GeBuildingController::CanPlacePlumbingBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) {
  return CheckPlaceBuilding(position, key, affected, price, 0, 2, 0) != 0;
}

bool GeBuildingController::CanPlaceSubwayBuilding(
    const CtCoord &position, const RsKey &key, GeBounds *affected, u32 *price) {
  return CheckPlaceBuilding(position, key, affected, price, 0, 3, 0) != 0;
}

bool GeBuildingController::CanPlaceMultiBuilding(const CtCoord &position, const RsKey &first, const RsKey &second,
    const RsKey &third, const RsKey &fourth, GeBounds *affected, u32 *price) {
  return CheckPlaceBuilding(position, first, second, third, fourth, affected, price, 0, 0, 0) != 0;
}

extern u8 GeBuildingStaticReady;
extern OcAttribCache *GeBuildingAttribCache;
extern GeRandom *GeBuildingRandom;
extern CtAgentTree *GeBuildingAgentTree;

// Establish defaults, retained definition providers and independent random state before registering the real tool
// factory.
// Registration occurs even if default loading refused; tracking groups load only after the final live module guard
// check.
bool GeBuildingController::StaticInit(CtDefinition *definition) {
  GeBuildingStaticReady = 1;
  GeDefaultBuildings = new GeBuildingDefaults;
  if (GeBuildingStaticReady)
    GeBuildingStaticReady = GeDefaultBuildings->Load(definition);
  GeBuildingAgentTree = ScAddRefNotNull(definition->GetAgentTypeTree());
  GeBuildingAttribCache = ScAddRefNotNull(definition->GetOccupantAttribCache());
  GeBuildingRandom = new GeRandom(kScRandomSeedFromClock);
  GeBuildingFactory *factory = new GeBuildingFactory;
  factory->AddRef();
  GeGetClassFactory()->AddCommandFactory(factory);
  factory->Release();
  if (!GeBuildingStaticReady)
    return false;
  LoadTrackingGroups();
  return true;
}

extern OcAttribCache *GeBuildingAttribCache;

// Select the actual placement tool by resource family and instance, preserving the four-building rotations and ordering.
// The scoped factory service is retained even though selection does not call it; a refused tool query leaves allocation
// ownership unchanged, as in the original, while service release follows both normal completion and unwinding.
bool GeBuildingController::CreatePlacementTool(
    const RsKey &key, bool singleUse, bool freeOfCharge, u32 iid, void **out) {
  void *factory = nullptr;
  GeGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &factory);
  bool result = false;
  static GeRandom random(kScRandomSeedFromClock);
  GeCommand *tool;
  if (key.type != kRsBuildingDataResourceType || key.group != 1406) {
    tool = new GeBuildingTool(key, singleUse, freeOfCharge);
  } else {
    switch (key.instance) {
    case 1:
      tool = new GeShoreTool(key, singleUse, freeOfCharge);
      break;
    case 16010:
    case 233:
      tool = new GeSubwayTool(key, singleUse, freeOfCharge);
      break;
    case 220:
    case 235:
    case 244:
    case 250:
      tool = new GePlumbingTool(key, singleUse, freeOfCharge);
      break;
    case 8088: {
      u32 rotation = random.Draw() & 3;
      RsKey first(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + rotation),
          second(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + ((rotation + 1) & 3)),
          third(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + ((rotation + 2) & 3)),
          fourth(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + ((rotation + 3) & 3));
      tool = new GeMultiTool(first, second, third, fourth, singleUse, freeOfCharge);
      break;
    }
    case 8075: {
      RsKey first(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8078),
          second(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8077),
          third(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8075),
          fourth(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8076);
      tool = new GeMultiTool(first, second, third, fourth, singleUse, freeOfCharge);
      break;
    }
    case 8029: {
      u32 rotation = random.Draw() & 3;
      RsKey first(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + rotation),
          second(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + ((rotation + 1) & 3)),
          third(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + ((rotation + 2) & 3)),
          fourth(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + ((rotation + 3) & 3));
      tool = new GeMultiTool(first, second, third, fourth, singleUse, freeOfCharge);
      break;
    }
    default:
      tool = new GeBuildingTool(key, singleUse, freeOfCharge);
      break;
    }
  }
  result = tool->QueryInterface(iid, out);
  if (factory)
    static_cast<ScCommandFactory *>(factory)->Release();
  return result;
}

// Create the requested tool as a command, execute it, then release the live escaped command publication.
// Creation refusal leaves the entry point false without executing or releasing an unreturned command.
bool GeBuildingController::DoPlaceBuildingTool(const RsKey &key, bool singleUse, bool freeOfCharge) {
  void *publication = nullptr;
  if (!CreatePlacementTool(key, singleUse, freeOfCharge, IID_UiCommandInterface, &publication))
    return false;
  bool result = static_cast<UiCommandInterface *>(publication)->Execute();
  static_cast<UiCommandInterface *>(publication)->Release();
  return result;
}

// Copy the attribute's borrowed key before creating its command; keep the execution reply across live-publication release.
bool GeBuildingController::DoPlaceBuildingTool(GeAttrib *attributes, bool singleUse, bool freeOfCharge) {
  void *publication = nullptr;
  RsKey key = *attributes->Key();
  if (!CreatePlacementTool(key, singleUse, freeOfCharge, IID_UiCommandInterface, &publication))
    return false;
  bool result = static_cast<UiCommandInterface *>(publication)->Execute();
  static_cast<UiCommandInterface *>(publication)->Release();
  return result;
}

// Select a building key into zeroed storage before creating and executing its placement command.
// Failed selection or command creation does not execute a tool or release an unreturned publication.
bool GeBuildingController::DoPlaceBuildingTool(u32 first, u32 second, bool singleUse, bool freeOfCharge) {
  void *publication = nullptr;
  RsKey key;
  if (!GeBuildingAttribCache->GetABuildingOfType(first, second, &key) ||
      !CreatePlacementTool(key, singleUse, freeOfCharge, IID_UiCommandInterface, &publication))
    return false;
  bool result = static_cast<UiCommandInterface *>(publication)->Execute();
  static_cast<UiCommandInterface *>(publication)->Release();
  return result;
}
