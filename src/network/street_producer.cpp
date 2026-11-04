#include <vector>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_building_data.h"
#include "network/nw_street_producer.h"
#include "network/nw_street_vehicle.h"
#include "network/street.h"
#include "network/street_traffic.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "terrain/api.h"

// Cumulative vehicle weights start one below zero before the original final increment restores the total.
enum NwCumulativeWeightSeed : u32 {
  kNwCumulativeWeightInitialBias = 0xffffffff,
};

// Vehicle frequency rescaling targets the original total weight and permits its original deviation.
enum NwFrequencyWeight : i32 {
  kNwVehicleFrequencyTargetWeight = 0x989680,
  kNwVehicleFrequencyWeightTolerance = 0xf4240,
};

// Ships use the original random spawn interval; an unavailable route retries at its maximum delay.
enum NwShipSpawnDelay : i32 {
  kNwShipMinimumSpawnDelay = 0x7270e00,
  kNwShipMaximumSpawnDelay = 0x15752a00,
};

// Local traffic producers retain independent initial and random minimum/maximum spawn delays.
enum NwProducerDelay : i32 {
  kNwAirTrafficInitialDelay = 0x989680,
  kNwAirTrafficMinimumDelay = 0x1312d00,
  kNwAirTrafficMaximumDelay = 0x2625a00,
  kNwBoatMinimumDelay = 0x1e8480,
  kNwBoatMaximumDelay = 0x5b8d80,
  kNwHelicopterMinimumDelay = 0xf4240,
  kNwHelicopterMaximumDelay = 0x2dc6c0,
};

// Original vehicle-producer generator, initialized with the other street-simulation static objects.
extern ScRandom NwVehicleRuntimeProducerRandom;
extern ScRandom NwPeopleProducerRandom;
extern ScRandom NwTrainProducerRandom, NwSubwayProducerRandom;
extern ScRandom NwShipProducerRandom;
extern ScRandom NwBoatProducerRandom;

ScDirtBag *NwShipProducer::dirtBag;
CtCity *NwStrikeCrowdProducer::city;
CtAgentTree *NwStrikeCrowdProducer::agentTypeTree;
ScOccupantManager *NwStrikeCrowdProducer::surfaceOccupantManager;
OcAnimManager *NwStrikeCrowdProducer::animationManager;
CtResidentialLayer *NwStrikeCrowdProducer::residentialLayer;
u32 NwStrikeCrowdProducer::searchRadius = 4;
ScOccupantManager *NwShipProducer::occupantManager;
GeBuildingLayer *NwShipProducer::buildingLayer;
CtNeighborsLayer *NwShipProducer::neighborsLayer;
u32 NwShipProducer::cellsX, NwShipProducer::cellsZ;
u8 NwShipProducer::seaportMapCreated;
NwVehicleProducerAttrib *NwShipProducer::shipAttributes[4];

ScSpiralIterator::ScSpiralIterator()
    : left(-INT32_MAX), top(-INT32_MAX), right(INT32_MAX), bottom(INT32_MAX), direction(1) {
  Reset(0, 0);
}

// Restart the spiral at a point and mark it finished when that point is outside the bounds.
void ScSpiralIterator::Reset(i32 startX, i32 startZ) {
  x = startX;
  z = startZ;
  endX = i32(u32(startX) + (direction == 1 ? 1u : kScBackwardGridStep));
  endZ = startZ;
  alongX = 1;
  step = direction == 1 ? 1 : -1;
  finishedEdges = 0;
  state = 0;
  if (x < left || z < top || x > right || z > bottom) {
    state = 2;
    finishedEdges = 15;
  }
}

// Turn the spiral and extend its next horizontal or vertical endpoint.
void ScSpiralIterator::SetNextEndpoint() {
  if (alongX) {
    alongX = 0;
    endX = x;
    if (!direction)
      step = i32(0u - u32(step));
    endZ = i32(u32(z) + u32(step));
  } else {
    alongX = 1;
    if (direction == 1)
      step = i32(0u - u32(step));
    step = i32(u32(step) + (step <= 0 ? kScBackwardGridStep : 1u));
    endX = i32(u32(x) + u32(step));
    endZ = z;
  }
}

// Turn at a run endpoint, skipping sides already exhausted by the clipping rectangle.
u8 ScSpiralIterator::DoXZEnd() {
  while (finishedEdges != 15) {
    if (alongX) {
      if (step > 0) {
        if (x <= right) {
          SetNextEndpoint();
          if (z < top)
            z = top;
          return 1;
        }
        finishedEdges |= 1;
      } else {
        if (x >= left) {
          SetNextEndpoint();
          if (z > bottom)
            z = bottom;
          return 1;
        }
        finishedEdges |= 2;
      }
    } else if (step > 0) {
      if (z <= bottom) {
        SetNextEndpoint();
        if (x > right)
          x = right;
        return 1;
      }
      finishedEdges |= 4;
    } else {
      if (z >= top) {
        SetNextEndpoint();
        if (x < left)
          x = left;
        return 1;
      }
      finishedEdges |= 8;
    }
    SetNextEndpoint();
    x = endX;
    z = endZ;
  }
  return 0;
}

// Yield the next bounded spiral coordinate and advance its current run.
u8 ScSpiralIterator::Next(i32 *outX, i32 *outZ) {
  if (state == 2)
    return 0;
  if (!state) {
    state = 1;
  } else if (alongX) {
    x = i32(u32(x) + (step > 0 ? 1u : kScBackwardGridStep));
    if ((step > 0 && x > right) || (step <= 0 && x < left))
      x = endX;
    if (x == endX)
      DoXZEnd();
  } else {
    z = i32(u32(z) + (step > 0 ? 1u : kScBackwardGridStep));
    if ((step > 0 && z > bottom) || (step <= 0 && z < top))
      z = endZ;
    if (z == endZ)
      DoXZEnd();
  }
  if (finishedEdges == 15) {
    state = 2;
    return 0;
  }
  *outX = x;
  *outZ = z;
  return 1;
}

NwStrikeCrowdProducer::NwStrikeCrowdProducer() : initialized(0), references(0) {
}

NwStrikeCrowdProducer::~NwStrikeCrowdProducer() {
  if (initialized)
    Shutdown();
}

bool NwStrikeCrowdProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwStrikeCrowdProducer::AddRef() {
  return ++references;
}

u32 NwStrikeCrowdProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Subscribe the message identity to the nine original strike and occupant notifications.
u8 NwStrikeCrowdProducer::Init() {
  ScMessageServer *server = NwGetStreetMessageServer();
  if (server) {
    static constexpr u32 messages[] = {kOcMsg_SurfaceInserted, kOcMsg_SurfaceRemoved, kEcMsg_SchoolStrikeChanged,
        kEcMsg_CollegeStrikeChanged, kEcMsg_HealthStrikeChanged, kCtMsg_PoliceStrikeChanged, kCtMsg_FireStrikeChanged,
        kNwMsg_TransitStrikeStarted, kNwMsg_TransitStrikeEnded};
    for (u32 type : messages)
      server->AddNotification(static_cast<ScMessageTarget *>(this), type);
  }
  initialized = 1;
  return 1;
}

void NwStrikeCrowdProducer::EndStrike(std::vector<NwStrikeCrowd *> *crowds) {
  for (NwStrikeCrowd *crowd : *crowds) {
    animationManager->RemoveOccupant(static_cast<ScOccupant *>(crowd));
    crowd->Release();
  }
  crowds->clear();
}

// Unsubscribe, remove all strike crowds and release the retained strike-eligible occupants.
void NwStrikeCrowdProducer::Shutdown() {
  if (!initialized)
    return;
  initialized = 0;
  ScMessageServer *server = NwGetStreetMessageServer();
  if (server) {
    static constexpr u32 messages[] = {kOcMsg_SurfaceInserted, kOcMsg_SurfaceRemoved, kEcMsg_SchoolStrikeChanged,
        kEcMsg_CollegeStrikeChanged, kEcMsg_HealthStrikeChanged, kCtMsg_PoliceStrikeChanged, kCtMsg_FireStrikeChanged,
        kNwMsg_TransitStrikeStarted, kNwMsg_TransitStrikeEnded};
    for (u32 type : messages)
      server->RemoveNotification(static_cast<ScMessageTarget *>(this), type);
  }
  EndStrike(&schoolCrowds);
  EndStrike(&collegeCrowds);
  EndStrike(&healthCrowds);
  EndStrike(&fireCrowds);
  EndStrike(&policeCrowds);
  EndStrike(&transitCrowds);
  for (ScOccupant *occupant : strikeOccupants)
    occupant->Release();
  strikeOccupants.clear();
}

void NwStrikeCrowdProducer::VehicleDisappears(NwVehicle *) {
}

bool NwStrikeCrowdProducer::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

// Retain strike-eligible occupants and insert them into the protected ordered set.
u8 NwStrikeCrowdProducer::OccupantInserted(ScOccupant *occupant) {
  u32 type = occupant->TypeId();
  if (agentTypeTree->IsAncestor(kCtAgentCityHall, type) || agentTypeTree->IsAncestor(kCtAgentMayorsHouse, type)) {
    occupant->AddRef();
    lock.Lock();
    strikeOccupants.insert(occupant);
    lock.Unlock();
  }
  return 1;
}

// Remove and release a retained strike occupant after leaving the protected set.
u8 NwStrikeCrowdProducer::OccupantRemoved(ScOccupant *occupant) {
  lock.Lock();
  auto entry = strikeOccupants.find(occupant);
  if (entry == strikeOccupants.end()) {
    lock.Unlock();
  } else {
    strikeOccupants.erase(entry);
    lock.Unlock();
    occupant->Release();
  }
  return 1;
}

// Dispatch strike start and end notifications and maintain the eligible-occupant set.
bool NwStrikeCrowdProducer::DoMessage(ScMessage *message) {
  u32 value = u32(message->data1);
  switch (message->type) {
  case kOcMsg_SurfaceInserted:
    return OccupantInserted(reinterpret_cast<ScOccupant *>(message->data1)) != 0;
  case kOcMsg_SurfaceRemoved:
    return OccupantRemoved(reinterpret_cast<ScOccupant *>(message->data1)) != 0;
  case kEcMsg_SchoolStrikeChanged:
    if (value == 1)
      BeginSchoolStrike();
    else if (!value)
      EndStrike(&schoolCrowds);
    break;
  case kEcMsg_CollegeStrikeChanged:
    if (value == 1)
      BeginCollegeStrike();
    else if (!value)
      EndStrike(&collegeCrowds);
    break;
  case kEcMsg_HealthStrikeChanged:
    if (value == 1)
      BeginHealthStrike();
    else if (!value)
      EndStrike(&healthCrowds);
    break;
  case kCtMsg_FireStrikeChanged:
    if (value == 1)
      BeginFireStrike();
    else if (!value)
      EndStrike(&fireCrowds);
    break;
  case kCtMsg_PoliceStrikeChanged:
    if (value == 1)
      BeginPoliceStrike();
    else if (!value)
      EndStrike(&policeCrowds);
    break;
  case kNwMsg_TransitStrikeStarted:
    BeginTransitStrike();
    break;
  case kNwMsg_TransitStrikeEnded:
    EndStrike(&transitCrowds);
    break;
  default:
    break;
  }
  return true;
}

void NwStrikeCrowdProducer::CreateStrikeVisuals(
    ScOccupant **occupants, u32 count, std::vector<NwStrikeCrowd *> *crowds) {
  lock.Lock();
  for (ScOccupant *occupant : strikeOccupants) {
    NwStrikeCrowd *crowd = CreateAndPlaceCrowdNearOccupant(occupant);
    if (crowd)
      crowds->push_back(crowd);
  }
  lock.Unlock();
  for (u32 i = 0; i < count; ++i) {
    NwStrikeCrowd *crowd = CreateAndPlaceCrowdNearOccupant(occupants[i]);
    if (crowd)
      crowds->push_back(crowd);
    occupants[i]->Release();
    occupants[i] = nullptr;
  }
}

void NwStrikeCrowdProducer::BeginSchoolStrike() {
  ScOccupant *occupants[20];
  u32 count;
  if (residentialLayer->GetStrikingSchoolBuildings(occupants, 20, &count))
    CreateStrikeVisuals(occupants, count, &schoolCrowds);
}

void NwStrikeCrowdProducer::BeginCollegeStrike() {
  ScOccupant *occupants[20];
  u32 count;
  if (residentialLayer->GetStrikingCollegeBuildings(occupants, 20, &count))
    CreateStrikeVisuals(occupants, count, &collegeCrowds);
}

void NwStrikeCrowdProducer::BeginHealthStrike() {
  ScOccupant *occupants[20];
  u32 count;
  if (residentialLayer->GetStrikingHealthBuildings(occupants, 20, &count))
    CreateStrikeVisuals(occupants, count, &healthCrowds);
}

void NwStrikeCrowdProducer::BeginFireStrike() {
  CtFireLayer *layer = city->FireLayer();
  ScOccupant *occupants[20];
  u32 count;
  if (layer && layer->GetStrikingBuildings(occupants, 20, &count))
    CreateStrikeVisuals(occupants, count, &fireCrowds);
}

void NwStrikeCrowdProducer::BeginPoliceStrike() {
  CtPoliceLayer *layer = city->PoliceLayer();
  ScOccupant *occupants[20];
  u32 count;
  if (layer && layer->GetStrikingBuildings(occupants, 20, &count))
    CreateStrikeVisuals(occupants, count, &policeCrowds);
}

void NwStrikeCrowdProducer::BeginTransitStrike() {
  NwTrafficLayer *layer = city->TrafficLayer();
  ScOccupant *occupants[20];
  u32 count;
  if (layer && layer->EnumerateUpkeepOccupants(occupants, 20, &count))
    CreateStrikeVisuals(occupants, count, &transitCrowds);
}

// Create a strike crowd with the original vehicle resource and retain this producer on success.
NwStrikeCrowd *NwStrikeCrowdProducer::CreateAStrikeCrowd() {
  if (!initialized)
    return nullptr;
  NwStrikeCrowd *crowd = new NwStrikeCrowd;
  crowd->AddRef();
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, 83);
  if (!crowd->Init(key)) {
    crowd->Release();
    return nullptr;
  }
  crowd->SetProducer(static_cast<NwVehicleProducerInterface *>(this));
  return crowd;
}

// Expand a cell rectangle by the requested distance, clipping it to the current city.
void NwStrikeCrowdProducer::ExpandRectToIncludeNeighborhood(GeCellRect *rect, u32 distance) {
  u32 maxX = city->CellCountX() - 1, maxZ = city->CellCountZ() - 1;
  rect->left = rect->left <= distance ? 0 : rect->left - distance;
  rect->top = rect->top <= distance ? 0 : rect->top - distance;
  rect->right += distance;
  if (rect->right > maxX)
    rect->right = maxX;
  rect->bottom += distance;
  if (rect->bottom > maxZ)
    rect->bottom = maxZ;
}

bool NwStrikeCrowdProducer::IncludeRoads(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad;
}

// Search outward around an occupant for a road tile where a new strike crowd can be placed.
NwStrikeCrowd *NwStrikeCrowdProducer::CreateAndPlaceCrowdNearOccupant(ScOccupant *occupant) {
  ScSpiralIterator spiral;
  NwStrikeCrowd *crowd = CreateAStrikeCrowd();
  if (!crowd)
    return nullptr;
  GeCellRect rect;
  occupant->GetBoundingRectInCellCoords(&rect);
  ExpandRectToIncludeNeighborhood(&rect, searchRadius);
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  spiral.left = i32(rect.left);
  spiral.top = i32(rect.top);
  spiral.right = i32(rect.right);
  spiral.bottom = i32(rect.bottom);
  spiral.Reset(i32(cellX), i32(cellZ));
  i32 x, z;
  while (spiral.Next(&x, &z)) {
    ScOccupant *road;
    if (surfaceOccupantManager->GetOccupantAtCell(u32(x), u32(z), &road)) {
      if (IncludeRoads(road) && crowd->PlaceOnTransitTile(road)) {
        road->Release();
        return crowd;
      }
      road->Release();
    }
  }
  crowd->Release();
  return nullptr;
}

// Retain the city and the managers used to create strike crowds.
u8 NwStrikeCrowdProducer::CitySpecificStaticInit(CtCity *value) {
  city = ScAddRefNotNull(value);
  agentTypeTree = ScAddRefNotNull(NwGetStreetAgentTypeTree());
  surfaceOccupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  animationManager = ScAddRefNotNull(city->AnimOccupantManager());
  residentialLayer = city->ResidentialLayer();
  if (residentialLayer)
    residentialLayer->AddRef();
  return 1;
}

u8 NwStrikeCrowdProducer::CitySpecificStaticShutdown() {
  ScRelease(&residentialLayer);
  ScRelease(&animationManager);
  ScRelease(&surfaceOccupantManager);
  ScRelease(&agentTypeTree);
  ScRelease(&city);
  return 1;
}

NwShipProducer::NwShipProducer() : initialized(0), references(0), routeCount(0), routeReady(0), shipCount(0) {
}

NwShipProducer::~NwShipProducer() {
  Shutdown();
}

// Publish the vehicle-producer identity and acquire a reference.
bool NwShipProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwShipProducer::AddRef() {
  return ++references;
}

u32 NwShipProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Clear the initialized flag and release the retained pier occupant.
void NwShipProducer::Shutdown() {
  if (initialized) {
    initialized = 0;
    ScRelease(&pier);
  }
}

void NwShipProducer::VehicleDisappears(NwVehicle *) {
  --shipCount;
}

bool NwShipProducer::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

u8 NwShipProducer::Init(ScOccupant *occupant) {
  initialized = 1;
  pier = ScAddRefNotNull(occupant);
  pier->GetLocationInCellCoords(&pierX, &pierZ);
  pier->GetSpriteInst()->GetRotationOffset(&rotation);
  elapsedTicks = 0;
  nextTick = static_cast<u32>(NwShipProducerRandom.Range(kNwShipMinimumSpawnDelay, kNwShipMaximumSpawnDelay));
  return 1;
}

i32 NwShipProducer::PierDirection() {
  return pierDirection;
}

bool NwShipProducer::InCityBounds(u32 x, u32 z) {
  return x < cellsX && z < cellsZ;
}

// Accept a pier tile with the same rotation on the same row or column within ten cells.
bool NwShipProducer::AddPierTileIfConnected(ScOccupant *occupant) {
  u8 otherRotation = 0;
  occupant->GetSpriteInst()->GetRotationOffset(&otherRotation);
  if (rotation != otherRotation)
    return false;
  u32 otherX, otherZ;
  occupant->GetLocationInCellCoords(&otherX, &otherZ);
  i32 deltaX = static_cast<i32>(otherX - pierX), deltaZ = static_cast<i32>(otherZ - pierZ);
  return (otherX == pierX || deltaZ == 0) && deltaX >= -10 && deltaX <= 10 && deltaZ >= -10 && deltaZ <= 10;
}

// Check whether a cell holds a pier-to-seawall connector, releasing both queried references.
// Linux identifies the member and original port type 8.
bool NwShipProducer::PierToSeawallConnectorPresent(u32 x, u32 z) {
  ScOccupant *occupant;
  if (!occupantManager->GetOccupantAtCell(x, z, &occupant))
    return false;
  bool found = false;
  GePortType *port;
  if (NwStreetLayer::GetPortAttrib(occupant, &port)) {
    found = port->GetPortType() == 8;
    port->Release();
  }
  occupant->Release();
  return found;
}

// Check whether a cell holds a pier, releasing both queried references.
// Linux identifies the member and original port type 9.
bool NwShipProducer::PierPresent(u32 x, u32 z) {
  ScOccupant *occupant;
  if (!occupantManager->GetOccupantAtCell(x, z, &occupant))
    return false;
  bool found = false;
  GePortType *port;
  if (NwStreetLayer::GetPortAttrib(occupant, &port)) {
    found = port->GetPortType() == 9;
    port->Release();
  }
  occupant->Release();
  return found;
}

// Locate the pier connector and request the original water path from the terrain service.
void NwShipProducer::InitPathRelatedInfo() {
  static constexpr i32 stepX[4] = {-1, 0, 1, 0}, stepZ[4] = {0, -1, 0, 1};
  static constexpr i32 offsetX[4] = {5, -1, -5, 1}, offsetZ[4] = {1, 5, -1, -5};
  u32 endX = 0, endZ = 0;
  bool found = false;
  for (i32 direction = 0; direction < 4 && !found; ++direction) {
    u32 x = pierX, z = pierZ;
    for (u32 length = 1; length <= 9; ++length) {
      x += u32(stepX[direction]);
      z += u32(stepZ[direction]);
      if (!PierPresent(x, z)) {
        if (PierToSeawallConnectorPresent(x, z)) {
          found = true;
          pierDirection = static_cast<i8>(direction);
          endX = x;
          endZ = z;
        }
        break;
      }
    }
  }
  if (!found)
    return;
  u32 x = endX + u32(offsetX[pierDirection]), z = endZ + u32(offsetZ[pierDirection]);
  if (x >= cellsX || z >= cellsZ)
    return;
  if (!seaportMapCreated) {
    dirtBag->RecomputeSeaportMap();
    seaportMapCreated = 1;
  }
  routeReady = dirtBag->GetWaterPath(x, z, 256, route, &routeCount);
  if (routeCount < 10)
    routeReady = 0;
}

void NwShipProducer::ReEvaluateSeaportMapAndPath() {
  routeReady = 0;
  dirtBag->RecomputeSeaportMap();
  InitPathRelatedInfo();
}

// Create and place an arriving ship, releasing the producer's temporary reference afterward.
// Linux identifies the member and the producer's temporary ownership.
void NwShipProducer::CreateAnArrivingShip() {
  NwShipVehicle *ship = CreateAShip();
  if (ship) {
    if (!ship->MakeArrivingShip(route, routeCount))
      ship->Shutdown();
    ship->Release();
  }
}

// Locate the route and randomly decide whether to place the initial ship at the pier.
// Linux identifies the member and the randomized initial placement.
void NwShipProducer::PlaceInitialShip() {
  InitPathRelatedInfo();
  if (NwShipProducerRandom.Bounded(2)) {
    NwShipVehicle *ship = CreateAShip();
    if (ship) {
      if (!ship->MakeParkedShip(route, routeCount))
        ship->Shutdown();
      ship->Release();
    }
  }
}

// Periodically rebuild a missing route and schedule an arriving ship when the route is usable.
void NwShipProducer::Tick(u32 elapsed) {
  elapsedTicks += elapsed;
  if (elapsedTicks < nextTick)
    return;
  elapsedTicks = 0;
  if (!routeReady)
    InitPathRelatedInfo();
  if (routeReady) {
    CreateAnArrivingShip();
    nextTick = static_cast<u32>(NwShipProducerRandom.Range(kNwShipMinimumSpawnDelay, kNwShipMaximumSpawnDelay));
  } else {
    nextTick = kNwShipMaximumSpawnDelay;
  }
}

// Retain the city services used by ship production and record its dimensions.
u8 NwShipProducer::CitySpecificStaticInit(CtCity *value) {
  dirtBag = ScAddRefNotNull(value->DirtBag());
  occupantManager = ScAddRefNotNull(value->SurfaceOccupantManager());
  buildingLayer = ScAddRefNotNull(value->BuildingLayer());
  neighborsLayer = ScAddRefNotNull(value->NeighborsLayer());
  cellsX = value->CellCountX();
  cellsZ = value->CellCountZ();
  return 1;
}

// Release the city services held for ship production in the original order.
u8 NwShipProducer::CitySpecificStaticShutdown() {
  ScRelease(&neighborsLayer);
  ScRelease(&buildingLayer);
  ScRelease(&dirtBag);
  ScRelease(&occupantManager);
  return 1;
}

// Select the ship resource from city deals and seaport tracking, then create a retained ship.
NwShipVehicle *NwShipProducer::CreateAShip() {
  if (!routeReady || !initialized)
    return nullptr;
  u32 index =
      neighborsLayer->HasDealType(kCtDealExportGarbage) || neighborsLayer->HasDealType(kCtDealImportGarbage) ? 1 : 0;
  if (buildingLayer->CheckOccupantTracking(kCtAgentMilitaryBase) > 0)
    index |= 2;
  NwVehicleProducerAttrib *attributes = shipAttributes[index];
  if (!attributes || shipCount >= static_cast<u32>(static_cast<i64>(attributes->NumberOfVehicles())))
    return nullptr;
  NwShipVehicle *ship = new NwShipVehicle;
  ship->AddRef();
  RsKey key;
  attributes->GetAVehicleType(&key);
  if (!ship->Init(key)) {
    ship->Release();
    return nullptr;
  }
  ship->SetProducer(this);
  ++shipCount;
  return ship;
}

// Load the four ship-production attribute resources for the deal and port combinations.
u8 NwShipProducer::StaticInit() {
  RsManager *manager = NwGetStreetResourceManager();
  for (u32 i = 0; i < 4; ++i) {
    RsKey key(kRsVehicleProducerAttributesResourceType, kRsVehicleProducerAttributesGroup, 0x3fc + i);
    RsResource *resource;
    if (manager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
      shipAttributes[i] = static_cast<NwVehicleProducerAttrib *>(resource);
    else
      shipAttributes[i] = nullptr;
  }
  return 1;
}

// Release the four retained ship-production resources.
u8 NwShipProducer::StaticShutdown() {
  for (NwVehicleProducerAttrib *&attributes : shipAttributes) {
    ScRelease(&attributes);
  }
  return 1;
}
extern ScRandom NwHelicopterProducerRandom;
extern ScRandom NwAirTrafficManagerRandom;
u32 NwVehicleProducer::maxX, NwVehicleProducer::maxZ, NwVehicleProducer::gridsX, NwVehicleProducer::totalGrids;
ScOccupantManager *NwVehicleProducer::occupantManager;
CtStartSimLayer *NwVehicleProducer::streetLayer;
NwTrafficLayer *NwVehicleProducer::trafficLayer;
ApBoxX *NwVehicleProducer::boxX;
u32 NwPeopleProducer::maxX, NwPeopleProducer::maxZ, NwPeopleProducer::totalPeople;
ScOccupantManager *NwPeopleProducer::occupantManager;
CtStartSimLayer *NwPeopleProducer::streetLayer;
u32 NwTrainProducer::maxX, NwTrainProducer::maxZ;
ScOccupantManager *NwTrainProducer::occupantManager;
u32 NwSubwayProducer::maxX, NwSubwayProducer::maxZ;
ScOccupantManager *NwSubwayProducer::occupantManager;
CtAgentTree *NwSubwayProducer::agentTypeTree;
u32 NwBoatProducer::cellsX, NwBoatProducer::cellsZ;
ScDirtBag *NwBoatProducer::dirtBag;
EcLayer *NwBoatProducer::pollutionLayer;
NwBoatFilter *NwBoatProducer::boatFilter;
OcAnimManager *NwBoatProducer::animationManager;
RsResource *NwBoatProducer::saltWaterAttributes, *NwBoatProducer::freshWaterAttributes;
u32 NwBoatProducer::maxMarinaDistanceSquared = 900;
CtCity *NwHelicopterProducer::city;
u32 NwAirTrafficManager::cityXMicroUnits, NwAirTrafficManager::cityZMicroUnits;
u32 NwAirTrafficManager::maxCellX, NwAirTrafficManager::maxCellZ;
ScOccupantManager *NwAirTrafficManager::occupantManager;
OcAnimManager *NwAirTrafficManager::animationManager;
CtStartSimLayer *NwAirTrafficManager::streetLayer;
ScDirtBag *NwAirTrafficManager::dirtBag;

// Initialize airport state and create the two originally empty plane lists.
NwAirTrafficManager::NwAirTrafficManager()
    : attributes(nullptr), references(0), initialized(0), runwayChanged(1), anchor(nullptr), runwayPlane(nullptr),
      runwayDirection(0), taxiwaySide(0), runwayBeginX(0), runwayBeginZ(0), runwayEndX(0), runwayEndZ(0),
      maxPlanes(5) {
}

NwAirTrafficManager::~NwAirTrafficManager() {
  if (initialized)
    Shutdown();
}

// Publish the vehicle-producer identity and acquire a reference for the caller.
bool NwAirTrafficManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwAirTrafficManager::AddRef() {
  return ++references;
}

u32 NwAirTrafficManager::Release() {
  return ScReleaseImpl(this, &references);
}

// Retain the airport anchor and attributes and initialize runway and arrival state.
u8 NwAirTrafficManager::Init(ScOccupant *occupant, NwAirTrafficAttrib *value) {
  anchor = ScAddRefNotNull(occupant);
  anchor->GetLocationInCellCoords(&anchorX, &anchorZ);
  runwayBeginX = anchorX;
  runwayEndX = anchorX;
  u32 cellZ = anchorZ;
  attributes = value;
  runwayBeginZ = cellZ;
  runwayEndZ = cellZ;
  attributes->AddRef();
  elapsedTicks = 0;
  runwayChanged = 1;
  nextTick = kNwAirTrafficInitialDelay;
  u32 limit = static_cast<u32>(attributes->value);
  initialized = 1;
  maxPlanes = limit;
  return 1;
}

// Advance the arrival clock, updating a changed runway before scheduling the next plane.
void NwAirTrafficManager::Tick(u32 elapsed) {
  elapsedTicks += elapsed;
  if (elapsedTicks >= nextTick) {
    if (runwayChanged)
      EvaluateRunwayConfig();
    CreateAnArrivingPlane();
    elapsedTicks = 0;
    nextTick = NwAirTrafficManagerRandom.Range(kNwAirTrafficMinimumDelay, kNwAirTrafficMaximumDelay);
  }
}

// Create and retain a plane when the airport has capacity and its runway can operate.
NwAeroplane *NwAirTrafficManager::CreateAPlane() {
  if (planes.size() >= maxPlanes || !CanOperate())
    return nullptr;
  NwAeroplane *plane = new NwAeroplane;
  plane->AddRef();
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, attributes->GetAPlaneResInstId());
  if (!plane->Init(key)) {
    plane->Release();
    return nullptr;
  }
  plane->SetProducer(this);
  planes.push_back(plane);
  return plane;
}

// Create a plane at a randomly chosen city edge and start its arrival routine.
void NwAirTrafficManager::CreateAnArrivingPlane() {
  NwAeroplane *plane = CreateAPlane();
  if (!plane)
    return;
  i32 x = 0;
  u32 z = 0;
  switch (NwAirTrafficManagerRandom.Bounded(4)) {
    case 0:
      x = 5;
      z = static_cast<u32>(NwAirTrafficManagerRandom.Range(5, static_cast<i32>(cityZMicroUnits - 5)));
      break;
    case 1:
      x = NwAirTrafficManagerRandom.Range(5, static_cast<i32>(cityXMicroUnits - 5));
      z = 5;
      break;
    case 2:
      x = static_cast<i32>(cityXMicroUnits - 5);
      z = static_cast<u32>(NwAirTrafficManagerRandom.Range(5, static_cast<i32>(cityZMicroUnits - 5)));
      break;
    case 3:
      x = NwAirTrafficManagerRandom.Range(5, static_cast<i32>(cityXMicroUnits - 5));
      z = cityZMicroUnits - 5;
      break;
  }
  if (!plane->MakeArrivingPlane(x, z))
    plane->Shutdown();
  plane->Release();
}

// Populate the taxiway with initial departing planes when the runway can operate.
void NwAirTrafficManager::PlaceInitialPlanes() {
  if (runwayChanged)
    EvaluateRunwayConfig();
  if (!CanOperate())
    return;
  u32 length = runwayDirection ? runwayBeginZ - runwayEndZ : runwayBeginX - runwayEndX;
  u32 limit = length / 3;
  if (!limit)
    return;
  i32 count = NwAirTrafficManagerRandom.Range(1, static_cast<i32>(limit));
  u8 orientation = runwayDirection ? 24 : 16;
  u32 cellX, cellZ;
  GetTaxiwayEnd(&cellX, &cellZ);
  if (runwayDirection)
    --cellZ;
  else
    --cellX;
  for (i32 index = 0; index < count; ++index) {
    NwAeroplane *plane = CreateAPlane();
    if (!plane)
      break;
    if (!plane->MakeDepartingPlane(cellX, cellZ, orientation)) {
      plane->Shutdown();
      plane->Release();
      return;
    }
    plane->Release();
    if (runwayDirection)
      cellZ -= 3;
    else
      cellX -= 3;
  }
}

void NwAirTrafficManager::Shutdown() {
  if (initialized) {
    initialized = 0;
    KillAllPlanes();
    ScRelease(&anchor);
    ScRelease(&attributes);
  }
}

// Remove every tracked plane from animation before clearing the airport's list.
void NwAirTrafficManager::KillAllPlanes() {
  auto entry = planes.begin();
  while (entry != planes.end()) {
    NwAeroplane *plane = *entry++;
    animationManager->RemoveOccupant(plane);
  }
  planes.clear();
}

// Remove the first disappearing plane from the tracked airport list.
void NwAirTrafficManager::VehicleDisappears(NwVehicle *vehicle) {
  NwAeroplane *plane = static_cast<NwAeroplane *>(vehicle);
  for (auto entry = planes.begin(); entry != planes.end(); ++entry) {
    if (*entry == plane) {
      planes.erase(entry);
      return;
    }
  }
}

bool NwAirTrafficManager::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

// Cache city dimensions and retain the services used by airport traffic.
u8 NwAirTrafficManager::CitySpecificStaticInit(CtCity *city) {
  cityXMicroUnits = city->AnimCellCountX();
  cityZMicroUnits = city->AnimCellCountZ();
  occupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  animationManager = ScAddRefNotNull(city->AnimOccupantManager());
  maxCellX = city->CellCountX() - 1;
  maxCellZ = city->CellCountZ() - 1;
  streetLayer = city->StrtSimLayer();
  dirtBag = ScAddRefNotNull(city->DirtBag());
  return 1;
}

// Release the retained city services in their original order.
u8 NwAirTrafficManager::CitySpecificStaticShutdown() {
  ScRelease(&occupantManager);
  ScRelease(&animationManager);
  streetLayer = nullptr;
  ScRelease(&dirtBag);
  return 1;
}

bool NwAirTrafficManager::RunwayTilePresent(u32 cellX, u32 cellZ) {
  if (cellX > maxCellX || cellZ > maxCellZ)
    return false;
  bool result = false;
  ScOccupant *occupant;
  if (occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant)) {
    GePortType *portAttributes;
    if (NwStreetLayer::GetPortAttrib(occupant, &portAttributes)) {
      result = portAttributes->GetPortType() == 1;
      portAttributes->Release();
    }
    occupant->Release();
  }
  return result;
}

// Return the borrowed occupant that anchors this airport.
// Linux identifies the original member; the Windows entry remains unresolved.
ScOccupant *NwAirTrafficManager::AnchorTile() {
  return anchor;
}

void NwAirTrafficManager::GetRunwayBegin(u32 *cellX, u32 *cellZ) {
  *cellX = runwayBeginX;
  *cellZ = runwayBeginZ;
}

void NwAirTrafficManager::GetRunwayEnd(u32 *cellX, u32 *cellZ) {
  *cellX = runwayEndX;
  *cellZ = runwayEndZ;
}

u8 NwAirTrafficManager::TaxiwaySide() {
  return taxiwaySide;
}

bool NwAirTrafficManager::IsRunwayInXDirection() {
  return runwayDirection == 0;
}

// Check that the runway has enough space beyond each end for operations.
bool NwAirTrafficManager::CanOperate() {
  if (runwayDirection)
    return runwayBeginZ + 9 <= maxCellZ && runwayBeginZ >= runwayEndZ + 4;
  return runwayBeginX + 9 <= maxCellX && runwayBeginX >= runwayEndX + 4;
}

// Determine which side of the runway has a taxiway and remove planes if that side changes.
void NwAirTrafficManager::DetermineTaxiwaySide() {
  u8 previous = taxiwaySide;
  taxiwaySide = 0;
  u32 cellX, cellZ;
  if (runwayDirection) {
    cellX = runwayEndX + 2;
    if (cellX > maxCellX)
      goto finished;
    cellZ = runwayEndZ;
  } else {
    cellZ = runwayEndZ;
    if (cellZ < 2)
      goto finished;
    cellX = runwayEndX;
    cellZ -= 2;
  }
  ScOccupant *occupant;
  if (occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant)) {
    if (static_cast<NwStreetLayer *>(streetLayer)->IsTaxiway(occupant))
      taxiwaySide = 1;
    occupant->Release();
  }
finished:
  if (previous != taxiwaySide)
    KillAllPlanes();
}

// Find both runway ends around the airport anchor and update its taxiway side.
void NwAirTrafficManager::EvaluateRunwayConfig() {
  u32 oldBeginX = runwayBeginX, oldBeginZ = runwayBeginZ, oldEndX = runwayEndX, oldEndZ = runwayEndZ;
  if (RunwayTilePresent(anchorX + 1, anchorZ) || RunwayTilePresent(anchorX - 1, anchorZ)) {
    runwayDirection = 0;
  } else if (RunwayTilePresent(anchorX, anchorZ + 1) || RunwayTilePresent(anchorX, anchorZ - 1)) {
    runwayDirection = 1;
  } else {
    runwayEndX = anchorX;
    runwayBeginX = anchorX;
    runwayEndZ = anchorZ;
    runwayBeginZ = anchorZ;
    return;
  }
  u32 startX = anchorX, startZ = anchorZ;
  u32 dx = runwayDirection ? 0 : kScBackwardGridStep, dz = runwayDirection ? kScBackwardGridStep : 0;
  u32 cellX = startX, cellZ = startZ;
  for (u32 count = 1;; ++count) {
    cellX += dx;
    cellZ += dz;
    bool present = RunwayTilePresent(cellX, cellZ);
    if (count > 12)
      return;
    if (!present)
      break;
  }
  runwayEndX = cellX - dx;
  runwayEndZ = cellZ - dz;
  dx = runwayDirection ? 0 : 1;
  dz = runwayDirection ? 1 : 0;
  cellX = startX;
  cellZ = startZ;
  for (u32 count = 1;; ++count) {
    cellX += dx;
    cellZ += dz;
    bool present = RunwayTilePresent(cellX, cellZ);
    if (count > 12)
      return;
    if (!present)
      break;
  }
  runwayBeginX = cellX - dx;
  runwayBeginZ = cellZ - dz;
  DetermineTaxiwaySide();
  if (oldBeginZ != runwayBeginZ || oldBeginX != runwayBeginX || oldEndX != runwayEndX || oldEndZ != runwayEndZ)
    KillAllPlanes();
  runwayChanged = 0;
}

// Mark a runway change when a tile lies on the anchor's row or column within twelve cells.
u8 NwAirTrafficManager::AddRunwayTileIfConnected(ScOccupant *occupant) {
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  u32 dx = cellX - anchorX, dz = cellZ - anchorZ;
  if ((dx != 0 && dz != 0) || dx + 12 > 24 || dz + 12 > 24)
    return 0;
  runwayChanged = 1;
  if (!planes.empty())
    KillAllPlanes();
  return 1;
}

// Read the runway-start terrain altitude and convert it to animation units.
i32 NwAirTrafficManager::MicroYAtRunwayBegin() {
  u8 altitude;
  dirtBag->GetVertexAltitude(runwayBeginX, runwayBeginZ, &altitude);
  return static_cast<i32>(altitude) << 8;
}

// Write a point beyond the runway start, clamping it to the city and reporting whether it fit.
u8 NwAirTrafficManager::GetLandingBeginCell(u32 *cellX, u32 *cellZ, u32 distance) {
  if (runwayDirection) {
    *cellX = runwayBeginX;
    *cellZ = runwayBeginZ + distance;
    if (*cellZ > maxCellZ) {
      *cellZ = maxCellZ;
      return 0;
    }
  } else {
    *cellZ = runwayBeginZ;
    *cellX = runwayBeginX + distance;
    if (*cellX > maxCellX) {
      *cellX = maxCellX;
      return 0;
    }
  }
  return 1;
}

void NwAirTrafficManager::GetTaxiwayEnd(u32 *cellX, u32 *cellZ) {
  if (runwayDirection) {
    *cellZ = runwayBeginZ;
    *cellX = taxiwaySide ? runwayBeginX + 2 : runwayBeginX - 2;
  } else {
    *cellX = runwayBeginX;
    *cellZ = taxiwaySide ? runwayBeginZ - 2 : runwayBeginZ + 2;
  }
}

void NwAirTrafficManager::PlaneEnteredTaxiway(NwAeroplane *plane) {
  taxiwayPlanes.push_back(plane);
}

// Remove the first matching plane from the taxiway occupancy list.
void NwAirTrafficManager::PlaneLeftTaxiway(NwAeroplane *plane) {
  for (auto entry = taxiwayPlanes.begin(); entry != taxiwayPlanes.end(); ++entry) {
    if (*entry == plane) {
      taxiwayPlanes.erase(entry);
      return;
    }
  }
}

// Test whether any tracked taxiing plane occupies the requested cell.
u8 NwAirTrafficManager::TaxiwayCellEmpty(u32 cellX, u32 cellZ) {
  for (auto entry = taxiwayPlanes.begin(); entry != taxiwayPlanes.end(); ++entry) {
    NwAeroplane *plane = *entry;
    u32 x, z;
    if (plane->CurrentAction() == 7)
      static_cast<ScOccupant *>(plane)->GetLocationInCellCoords(&x, &z);
    else
      GetTaxiwayEnd(&x, &z);
    if (cellX == x && cellZ == z)
      return 0;
  }
  return 1;
}

u8 NwAirTrafficManager::ReserveRunway(NwAeroplane *plane) {
  if (runwayPlane)
    return 0;
  runwayPlane = plane;
  return 1;
}

void NwAirTrafficManager::RelinquishRunway(NwAeroplane *plane) {
  if (runwayPlane == plane)
    runwayPlane = nullptr;
}

NwVehicleProducer::NwVehicleProducer()
    : references(0), initialized(0), vehicleCount(0), targetVehicles(0), gridWeights(nullptr),
      gridFrequencies(nullptr), gridWeightTotals(nullptr), emergencyMode(0), produceVehicles(0) {
}

NwVehicleProducer::~NwVehicleProducer() {
  if (initialized)
    Shutdown();
}

// Construct an initialized rail vehicle, or destroy the already constructed consist after failure.
u8 NwTrainProducer::CreateAndAddRailVehicle(const RsKey &key, NwRailVehicle **vehicles, u32 *count) {
  NwRailVehicle *vehicle = new NwRailVehicle;
  if (vehicle->Init(key)) {
    vehicle->SetProducer(this);
    vehicles[(*count)++] = vehicle;
    return 1;
  }
  delete vehicle;
  for (u32 i = 0; i < *count; ++i)
    delete vehicles[i];
  *count = 0;
  return 0;
}

// Construct an initialized subway vehicle, or destroy the already constructed consist after failure.
u8 NwSubwayProducer::CreateAndAddSubwayVehicle(const RsKey &key, NwSubwayVehicle **vehicles, u32 *count) {
  NwSubwayVehicle *vehicle = new NwSubwayVehicle;
  if (vehicle->Init(key)) {
    vehicle->SetProducer(this);
    vehicles[(*count)++] = vehicle;
    return 1;
  }
  delete vehicle;
  for (u32 i = 0; i < *count; ++i)
    delete vehicles[i];
  *count = 0;
  return 0;
}

// Find a neighboring subway tile, build a consist and place its linked vehicles on that tile.
void NwSubwayProducer::CreateASubwayTrain() {
  static constexpr i32 dx[4] = {-1, 0, 1, 0}, dz[4] = {0, -1, 0, 1};
  if (!initialized || !produceVehicles)
    return;
  u32 stationX, stationZ;
  if (direction == 0xff) {
    occupant->GetLocationInCellCoords(&stationX, &stationZ);
    for (u32 i = 0; i < 4; ++i) {
      u32 cellX = stationX + u32(dx[i]), cellZ = stationZ + u32(dz[i]);
      ScOccupant *candidate;
      if (cellX <= maxX && cellZ <= maxZ && occupantManager->GetOccupantAtCell(cellX, cellZ, &candidate)) {
        if (IncludeSubway(candidate))
          direction = static_cast<u8>(8 * i);
        candidate->Release();
        if (direction != 0xff)
          break;
      }
    }
    if (direction == 0xff) {
      nextTick = NwSubwayProducerRandom.Range(attributes->minimum, attributes->maximum);
      return;
    }
  }
  occupant->GetLocationInCellCoords(&stationX, &stationZ);
  u32 cellX = stationX + u32(dx[direction >> 3]), cellZ = stationZ + u32(dz[direction >> 3]);
  ScOccupant *tile;
  if (cellX > maxX || cellZ > maxZ || !occupantManager->GetOccupantAtCell(cellX, cellZ, &tile))
    return;
  NwSubwayVehicle *vehicles[5];
  u32 count = 0;
  NwTrainType &type = attributes->types[NwSubwayProducerRandom.Bounded(u32(attributes->types.size()))];
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, u32(type.value));
  if (!CreateAndAddSubwayVehicle(key, vehicles, &count))
    return;
  if (!type.values2.empty()) {
    u32 cars = NwSubwayProducerRandom.Range(1, 4);
    for (u32 i = 0; i < cars; ++i) {
      key.instance = type.values2[NwSubwayProducerRandom.Bounded(u32(type.values2.size()))];
      if (!CreateAndAddSubwayVehicle(key, vehicles, &count))
        return;
    }
  }
  vehicles[0]->SetLeader(nullptr);
  for (u32 i = 1; i < count; ++i)
    vehicles[i]->SetLeader(static_cast<NwVehicle *>(vehicles[i - 1]));
  vehicles[count - 1]->SetFollower(nullptr);
  for (u32 i = count - 1; i > 0; --i)
    vehicles[i - 1]->SetFollower(static_cast<NwVehicle *>(vehicles[i]));
  for (u32 i = 0; i < count; ++i) {
    if (!vehicles[i]->PlaceOnTransitTile(tile, direction))
      delete vehicles[i];
  }
  tile->Release();
}

// Find a neighboring rail tile, build the selected consist and place its linked vehicles on that tile.
void NwTrainProducer::CreateATrain() {
  static constexpr i32 dx[4] = {-1, 0, 1, 0}, dz[4] = {0, -1, 0, 1};
  if (!initialized || !produceVehicles)
    return;
  u32 stationX, stationZ;
  if (direction == 0xff) {
    occupant->GetLocationInCellCoords(&stationX, &stationZ);
    for (u32 i = 0; i < 4; ++i) {
      u32 cellX = stationX + u32(dx[i]), cellZ = stationZ + u32(dz[i]);
      ScOccupant *candidate;
      if (cellX <= maxX && cellZ <= maxZ && occupantManager->GetOccupantAtCell(cellX, cellZ, &candidate)) {
        if (IncludeRail(candidate))
          direction = static_cast<u8>(8 * i);
        candidate->Release();
        if (direction != 0xff)
          break;
      }
    }
    if (direction == 0xff) {
      nextTick = NwTrainProducerRandom.Range(attributes->minimum, attributes->maximum);
      return;
    }
  }
  occupant->GetLocationInCellCoords(&stationX, &stationZ);
  u32 cellX = stationX + u32(dx[direction >> 3]), cellZ = stationZ + u32(dz[direction >> 3]);
  ScOccupant *tile;
  if (cellX > maxX || cellZ > maxZ || !occupantManager->GetOccupantAtCell(cellX, cellZ, &tile))
    return;
  NwRailVehicle *vehicles[20];
  u32 count = 0;
  NwTrainType &type = attributes->types[NwTrainProducerRandom.Bounded(u32(attributes->types.size()))];
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, u32(type.value));
  if (!CreateAndAddRailVehicle(key, vehicles, &count))
    return;
  if (!type.values0.empty()) {
    key.instance = type.values0[NwTrainProducerRandom.Bounded(u32(type.values0.size()))];
    if (!CreateAndAddRailVehicle(key, vehicles, &count))
      return;
  }
  if (!type.values2.empty()) {
    u32 cars = NwTrainProducerRandom.Range(1, 4);
    for (u32 i = 0; i < cars; ++i) {
      key.instance = type.values2[NwTrainProducerRandom.Bounded(u32(type.values2.size()))];
      if (!CreateAndAddRailVehicle(key, vehicles, &count))
        return;
    }
  }
  if (!type.values1.empty()) {
    u32 cars = NwTrainProducerRandom.Range(1, 5);
    for (u32 i = 0; i < cars; ++i) {
      key.instance = type.values1[NwTrainProducerRandom.Bounded(u32(type.values1.size()))];
      if (!CreateAndAddRailVehicle(key, vehicles, &count))
        return;
    }
  }
  if (!type.values3.empty()) {
    key.instance = type.values3[NwTrainProducerRandom.Bounded(u32(type.values3.size()))];
    if (!CreateAndAddRailVehicle(key, vehicles, &count))
      return;
  }
  vehicles[0]->SetLeader(nullptr);
  for (u32 i = 1; i < count; ++i)
    vehicles[i]->SetLeader(static_cast<NwVehicle *>(vehicles[i - 1]));
  vehicles[count - 1]->SetFollower(nullptr);
  for (u32 i = count - 1; i > 0; --i)
    vehicles[i - 1]->SetFollower(static_cast<NwVehicle *>(vehicles[i]));
  for (u32 i = 0; i < count; ++i)
    vehicles[i]->PlaceOnTransitTile(tile, direction);
  tile->Release();
}

NwTrainProducer::NwTrainProducer()
    : attributes(nullptr), references(0), initialized(0), occupant(nullptr), direction(0xff),
      elapsedTicks(0), nextTick(0), produceVehicles(true) {
}

NwTrainProducer::~NwTrainProducer() {
  if (initialized)
    Shutdown();
}

u32 NwTrainProducer::AddRef() {
  return ++references;
}

u32 NwTrainProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Retain the station and attributes and select the first production interval.
u8 NwTrainProducer::Init(ScOccupant *station, NwTrainProducerAttrib *value) {
  occupant = ScAddRefNotNull(station);
  attributes = ScAddRefNotNull(value);
  u32 next = NwTrainProducerRandom.Bounded(attributes->maximum);
  elapsedTicks = 0;
  nextTick = next;
  initialized = 1;
  return 1;
}

void NwTrainProducer::Shutdown() {
  if (initialized) {
    initialized = 0;
    occupant->Release();
    occupant = nullptr;
    attributes->Release();
    attributes = nullptr;
  }
}

bool NwTrainProducer::SetVehicleProduceState(bool enabled) {
  produceVehicles = enabled;
  return enabled;
}

// Advance the producer clock and schedule the next train after a production attempt.
void NwTrainProducer::Tick(u32 elapsed) {
  elapsedTicks += elapsed;
  if (elapsedTicks >= nextTick) {
    elapsedTicks = 0;
    CreateATrain();
    nextTick = NwTrainProducerRandom.Range(attributes->minimum, attributes->maximum);
  }
}

// Publish the vehicle-producer identity and retain it for a successful query.
bool NwVehicleProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwVehicleProducer::AddRef() {
  return ++references;
}

u32 NwVehicleProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Allocate the per-grid frequency maps and totals, then add the supplied attributes to each grid.
u8 NwVehicleProducer::Init(NwVehicleProducerAttribInterface *value) {
  NwVehicleProducerAttrib *resource = value ? static_cast<NwVehicleProducerAttrib *>(value) : nullptr;
  initialized = 1;
  gridFrequencies = new std::map<u32, NwVehicleFrequency>[totalGrids];
  gridWeights = new f32[totalGrids];
  gridWeightTotals = new u32[totalGrids];
  for (u32 i = 0; i < totalGrids; ++i) {
    gridWeights[i] = 0.0f;
    gridWeightTotals[i] = 0;
    AddAttribute(i, resource);
  }
  return 1;
}

// Clear and destroy the per-grid maps, followed by both separately allocated arrays.
void NwVehicleProducer::Shutdown() {
  if (initialized) {
    initialized = 0;
    if (gridFrequencies) {
      for (u32 i = 0; i < totalGrids; ++i)
        gridFrequencies[i].clear();
      delete[] gridFrequencies;
      gridFrequencies = nullptr;
    }
    if (gridWeights) {
      delete[] gridWeights;
      gridWeights = nullptr;
    }
    if (gridWeightTotals) {
      delete[] gridWeightTotals;
      gridWeightTotals = nullptr;
    }
  }
}

// Rescale a grid's frequencies, rebuild cumulative totals and renormalize large deviations.
void NwVehicleProducer::UpdateFrequencies(u32 grid, f32 scale) {
  auto &frequencies = gridFrequencies[grid];
  if (frequencies.empty())
    return;
  gridWeightTotals[grid] = kNwCumulativeWeightInitialBias;
  for (auto &entry : frequencies) {
    entry.second.weight = static_cast<i32>(u32(static_cast<i64>(f64(entry.second.weight) * scale + 0.5)));
    gridWeightTotals[grid] += u32(entry.second.weight);
    entry.second.cumulativeWeight = static_cast<i32>(gridWeightTotals[grid]);
  }
  ++gridWeightTotals[grid];
  i32 difference = static_cast<i32>(kNwVehicleFrequencyTargetWeight - gridWeightTotals[grid]);
  i64 magnitude = difference < 0 ? -i64(difference) : i64(difference);
  if (magnitude > kNwVehicleFrequencyWeightTolerance && gridWeightTotals[grid]) {
    f32 nextScale = static_cast<f32>(10000000.0 / static_cast<i32>(gridWeightTotals[grid]));
    UpdateFrequencies(grid, nextScale);
  }
}

// Add an attribute's weighted vehicle types to a grid and recompute its cumulative frequencies.
void NwVehicleProducer::AddAttribute(u32 grid, NwVehicleProducerAttrib *attributes) {
  f32 oldWeight = gridWeights[grid], addedWeight = attributes->vehicleCount;
  f32 incomingScale, existingScale;
  if (oldWeight == 0.0f) {
    existingScale = 1.0f;
    incomingScale = 1.0f;
    gridWeights[grid] = addedWeight;
  } else {
    incomingScale = static_cast<f32>(f64(addedWeight) / oldWeight);
    f64 total = f64(oldWeight) + addedWeight;
    existingScale = static_cast<f32>(f64(oldWeight) / total);
    gridWeights[grid] = static_cast<f32>(total);
  }
  auto &frequencies = gridFrequencies[grid];
  for (u32 i = 0; i < attributes->entryCount; ++i) {
    NwVehicleProductionEntry *item = &attributes->entries[i];
    u32 instance = item->key.instance;
    i32 contribution = static_cast<i32>(u32(static_cast<i64>(f64(item->weight) * incomingScale)));
    auto found = frequencies.find(instance);
    if (found == frequencies.end())
      frequencies.emplace(instance, NwVehicleFrequency{contribution, -1});
    else
      found->second.weight = static_cast<i32>(u32(found->second.weight) + u32(contribution));
  }
  UpdateFrequencies(grid, existingScale);
}

// Remove an attribute's weighted vehicle types and renormalize the remaining grid frequencies.
void NwVehicleProducer::RemoveAttribute(u32 grid, NwVehicleProducerAttrib *attributes) {
  f32 oldWeight = gridWeights[grid];
  if (oldWeight <= 0.0f)
    return;
  f64 fraction = f64(attributes->vehicleCount) / oldWeight;
  auto &frequencies = gridFrequencies[grid];
  for (u32 i = 0; i < attributes->entryCount; ++i) {
    NwVehicleProductionEntry *item = &attributes->entries[i];
    auto found = frequencies.find(item->key.instance);
    if (found != frequencies.end()) {
      i32 contribution = static_cast<i32>(u32(static_cast<i64>(f64(item->weight) * fraction)));
      found->second.weight = static_cast<i32>(u32(found->second.weight) - u32(contribution));
      if (found->second.weight <= 0)
        frequencies.erase(found);
    }
  }
  f64 remaining = f64(gridWeights[grid]) - attributes->vehicleCount;
  if (remaining > 0.0) {
    f32 scale = static_cast<f32>(f64(gridWeights[grid]) / remaining);
    gridWeights[grid] = static_cast<f32>(remaining);
    UpdateFrequencies(grid, scale);
  } else {
    frequencies.clear();
    gridWeights[grid] = 0.0f;
  }
}

// Populate road cells in the supplied rectangle according to their traffic volume.
void NwVehicleProducer::PutVehiclesInRect(const GeCellRect &area) {
  if (!produceVehicles)
    return;
  for (u32 cellZ = area.top; cellZ <= area.bottom; ++cellZ) {
    for (u32 cellX = area.left; cellX <= area.right; ++cellX) {
      ScOccupant *occupant;
      if (!occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant))
        continue;
      if (occupant->GetGeomFlag() & kOcGeomRoadOrHighway) {
        u8 volume = static_cast<u8>(uptr(&area) >> 24);
        trafficLayer->GetVolume(cellX, cellZ, &volume, 3);
        f64 weighted = f64(volume) * f64(0.0080000004f);
        f32 rounded = static_cast<f32>(weighted);
        u32 count = static_cast<u32>(weighted);
        if (f64(rounded) - count > NwVehicleRuntimeProducerRandom.RandomDoubleUniform())
          ++count;
        PutVehiclesOnRoadTile(occupant, count);
      }
      occupant->Release();
    }
  }
}

// Choose a starting cell and scan the simulation rectangle for a suitable road with traffic.
u8 NwVehicleProducer::PutAVehicleInRect() {
  u32 startX = static_cast<u32>(NwVehicleRuntimeProducerRandom.Range(
      static_cast<i32>(simulationRect.left), static_cast<i32>(simulationRect.right)));
  u32 startZ = static_cast<u32>(NwVehicleRuntimeProducerRandom.Range(
      static_cast<i32>(simulationRect.top), static_cast<i32>(simulationRect.bottom)));
  u32 cellX = startX, cellZ = startZ, threshold = trafficThreshold ? trafficThreshold : 1;
  do {
    ScOccupant *occupant;
    if (occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant)) {
      u32 flags = occupant->GetGeomFlag();
      if (flags & kOcGeomRoadOrHighway) {
        u8 volume = 0;
        trafficLayer->GetVolume(cellX, cellZ, &volume, 3);
        if (NwVehicleRuntimeProducerRandom.Bounded(threshold) <= volume && PutVehiclesOnRoadTile(occupant, 1)) {
          occupant->Release();
          return 1;
        }
      }
      occupant->Release();
    }
    if (++cellX > simulationRect.right) {
      cellX = simulationRect.left;
      if (++cellZ > simulationRect.bottom)
        cellZ = simulationRect.top;
    }
  } while (cellX != startX || cellZ != startZ);
  return 0;
}

// Inspect current traffic and increment the vehicle's low-traffic removal counter.
bool NwVehicleProducer::NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) {
  u32 cellX, cellZ;
  vehicle->GetLocationInCellCoords(&cellX, &cellZ);
  u8 volume = static_cast<u8>(reinterpret_cast<uptr>(vehicle) >> 24);
  trafficLayer->GetVolume(cellX, cellZ, &volume, 3);
  if (volume >= trafficThreshold) {
    *counter = 0;
  } else {
    ++*counter;
    if (NwVehicleRuntimeProducerRandom.Bounded(40) < *counter)
      return true;
  }
  return false;
}

void NwVehicleProducer::AddVehicleTypeInfoForOccupant(ScOccupant *occupant) {
  auto *view = reinterpret_cast<NwVehicleProducerAttribInterface *>(occupant->GetAttribData()->GetVehicleProducerAttrib());
  if (!view)
    return;
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  AddAttribute(GetGridIndex(cellX, cellZ), static_cast<NwVehicleProducerAttrib *>(view));
}

void NwVehicleProducer::RemoveVehicleTypeInfoForOccupant(ScOccupant *occupant) {
  auto *view = reinterpret_cast<NwVehicleProducerAttribInterface *>(occupant->GetAttribData()->GetVehicleProducerAttrib());
  if (!view)
    return;
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  RemoveAttribute(GetGridIndex(cellX, cellZ), static_cast<NwVehicleProducerAttrib *>(view));
}

// Select and place up to the requested number of road vehicles, returning successful placements.
i32 NwVehicleProducer::PutVehiclesOnRoadTile(ScOccupant *occupant, u32 count) {
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  u32 grid = (cellX >> 5) + gridsX * (cellZ >> 5);
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, 0);
  i32 placed = 0;
  for (u32 i = 0; i < count; ++i) {
    if (vehicleCount > targetVehicles || vehicleCount >= 200 || !GetAVehicleResourceInstance(grid, &key.instance))
      break;
    NwStreetVehicle *vehicle = new NwStreetVehicle;
    vehicle->AddRef();
    if (vehicle->Init(key)) {
      ++vehicleCount;
      vehicle->SetProducer(this);
      if (vehicle->PlaceOnTransitTile(occupant))
        ++placed;
    }
    vehicle->Release();
  }
  return placed;
}

void NwVehicleProducer::VehicleDisappears(NwVehicle *) {
  --vehicleCount;
}

void NwVehicleProducer::UpdateSimRect() {
  if (!produceVehicles)
    return;
  u32 volume = 0, count = 0;
  trafficLayer->SumVolume(simulationRect.left, simulationRect.top, simulationRect.right, simulationRect.bottom,
      &volume, &count, 3);
  targetVehicles = u32(static_cast<i64>(f64(volume) * f64(0.0080000004f)));
  if (targetVehicles > 200)
    targetVehicles = 200;
  if (emergencyMode)
    targetVehicles = u32(static_cast<i64>(f64(targetVehicles) * f64(0.2f)));
  trafficThreshold = count ? u8(volume / count) : 0;
  boxX->Event(kAuEventTrafficDensityChanged, (100 * targetVehicles) / 200, 0, 0, 0);
}

void NwVehicleProducer::SetEmergencyMode(bool enabled) {
  emergencyMode = enabled;
  UpdateSimRect();
}

void NwVehicleProducer::SetSimRect(const GeCellRect &rect) {
  simulationRect = rect;
  UpdateSimRect();
}

u8 NwVehicleProducer::SetVehicleProduceState(bool enabled) {
  produceVehicles = enabled;
  return produceVehicles;
}

// Attempt one placement per tenth of the remaining target, with at least one attempt when below target.
void NwVehicleProducer::Tick(u32) {
  if (produceVehicles && vehicleCount < targetVehicles) {
    u32 attempts = (targetVehicles - vehicleCount) / 10;
    if (!attempts)
      attempts = 1;
    for (u32 i = 0; i < attempts; ++i)
      PutAVehicleInRect();
  }
}

// Select an emergency instance or the first grid entry whose cumulative weight exceeds the random draw.
u8 NwVehicleProducer::GetAVehicleResourceInstance(u32 grid, u32 *out) {
  if (emergencyMode)
    return static_cast<NwStreetLayer *>(streetLayer)->GetAnEmergencyVehicleResInstId(out);
  i32 draw = i32(NwVehicleRuntimeProducerRandom.Bounded(gridWeightTotals[grid]));
  for (const auto &entry : gridFrequencies[grid]) {
    if (draw < entry.second.cumulativeWeight) {
      *out = entry.first;
      return 1;
    }
  }
  return 0;
}

u32 NwVehicleProducer::GetGridIndex(u32 cellX, u32 cellZ) {
  return (cellX >> 5) + gridsX * (cellZ >> 5);
}

bool NwVehicleProducer::IncludeRoadsAndAvenues(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRoadOrHighway) != 0;
}

// Retain city services and cache the original cell and 32-cell grid dimensions.
u8 NwVehicleProducer::CitySpecificStaticInit(CtCity *city) {
  occupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  u32 width = city->CellCountX(), depth = city->CellCountZ();
  maxX = width - 1;
  maxZ = depth - 1;
  gridsX = width >> 5;
  totalGrids = (width >> 5) * (depth >> 5);
  streetLayer = ScAddRefNotNull(city->StrtSimLayer());
  trafficLayer = ScAddRefNotNull(city->TrafficLayer());
  boxX = ScAddRefNotNull(NwGetStreetBoxX());
  return 1;
}

// Release the retained city services in their original order.
u8 NwVehicleProducer::CitySpecificStaticShutdown() {
  ScRelease(&boxX);
  ScRelease(&trafficLayer);
  ScRelease(&occupantManager);
  ScRelease(&streetLayer);
  return 1;
}

NwPeopleProducer::NwPeopleProducer()
    : references(0), attributes(nullptr), x(0), z(0), initialized(0), mode(nullptr), peopleCount(0),
      removalCompensation(1) {
}

NwPeopleProducer::~NwPeopleProducer() {
  if (initialized)
    Shutdown();
}

// Publish the people-producer identity and retain it for a successful query.
bool NwPeopleProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwPeopleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwPeopleProducerInterface, NwPeopleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwPeopleProducer::AddRef() {
  return ++references;
}

u32 NwPeopleProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Retain the attributes, select a behavior and populate the initial surrounding rectangle.
u8 NwPeopleProducer::Init(NwPeopleProducerAttribInterface *value, u32 cellX, u32 cellZ) {
  attributes = ScAddRefNotNull(value);
  x = cellX;
  z = cellZ;
  SelectBehaviorMode();
  ComputeSurroundRect(x, z, &rect);
  PutPeopleInRect(rect);
  initialized = 1;
  return 1;
}

u8 NwPeopleProducer::Shutdown() {
  if (initialized) {
    attributes->Release();
    attributes = nullptr;
    initialized = 0;
  }
  return 1;
}

// Move the production area and populate the portions newly covered by its rectangle.
void NwPeopleProducer::ChangeLocation(u32 cellX, u32 cellZ) {
  if (cellX != x || cellZ != z) {
    GeCellRect next, added[4];
    i32 count;
    ComputeSurroundRect(cellX, cellZ, &next);
    NwStreetLayer::SubtractRect(next, rect, added, &count);
    x = cellX;
    rect = next;
    z = cellZ;
    for (i32 i = 0; i < count; ++i)
      PutPeopleInRect(added[i]);
  }
}

// Populate a road tile with selected people while suppressing replacement callbacks during insertion.
// Reaching the original global population limit leaves the suppression byte cleared.
void NwPeopleProducer::PutPeopleOnRoadTile(ScOccupant *occupant, u32 count) {
  u8 previous = removalCompensation;
  removalCompensation = 0;
  for (u32 i = 0; i < count; ++i) {
    if (totalPeople >= 400)
      return;
    NwPerson *person = new NwPerson;
    person->AddRef();
    u32 choice = NwPeopleProducerRandom.Bounded(1000), index = 0;
    while (choice > mode->entries[index].threshold)
      ++index;
    person->SetProducer(this);
    if (person->Init(mode->entries[index].key)) {
      ++peopleCount;
      ++totalPeople;
      person->PlaceOnTransitTile(occupant);
    }
    person->Release();
  }
  removalCompensation = previous;
}

// Count a disappearing person and optionally replace it within the active production area.
void NwPeopleProducer::PersonDisappears(NwPeopleInterface *) {
  --peopleCount;
  --totalPeople;
  if (initialized && removalCompensation)
    PutPeopleInRect(rect);
}

void NwPeopleProducer::SpreadSpectators(u32 cellX, u32 cellZ, u32 count) {
  ScOccupant *occupant = nullptr;
  if (NwGetStreetCity()->SurfaceOccupantManager()->GetOccupantAtCell(cellX, cellZ, &occupant)) {
    if ((occupant->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad)
      PutPeopleOnRoadTile(occupant, count);
    occupant->Release();
  }
}

u8 NwPeopleProducer::SetRemovalCompensation(bool enabled) {
  removalCompensation = enabled;
  return removalCompensation;
}

void NwPeopleProducer::Tick(u32) {
  if (attributes->GetModeSelectionMethod() == 1) {
    u32 month, day, year;
    NwGetStreetCity()->GetDate(&month, &day, &year);
    CtDate current(month, day, year);
    if (current > date)
      SelectBehaviorMode();
  }
}

// Write the intersection endpoints in original order, leaving vertical outputs untouched if X does not overlap.
bool NwPeopleProducer::RectIntersect(const GeCellRect &first, const GeCellRect &second, GeCellRect *out) {
  out->left = first.left >= second.left ? first.left : second.left;
  out->right = first.right <= second.right ? first.right : second.right;
  if (out->left > out->right)
    return false;
  out->top = first.top >= second.top ? first.top : second.top;
  out->bottom = first.bottom <= second.bottom ? first.bottom : second.bottom;
  return out->bottom >= out->top;
}

// Clamp the selected mode's surrounding radius to the current city cell bounds.
void NwPeopleProducer::ComputeSurroundRect(u32 cellX, u32 cellZ, GeCellRect *out) {
  u32 radius = mode->radius;
  out->left = cellX > radius ? cellX - radius : 0;
  out->right = radius + cellX;
  if (out->right > maxX)
    out->right = maxX;
  out->top = cellZ > radius ? cellZ - radius : 0;
  out->bottom = radius + cellZ;
  if (out->bottom > maxZ)
    out->bottom = maxZ;
}

u8 NwPeopleProducer::CitySpecificStaticInit(CtCity *city) {
  occupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  maxX = city->CellCountX() - 1;
  maxZ = city->CellCountZ() - 1;
  streetLayer = ScAddRefNotNull(city->StrtSimLayer());
  return 1;
}

u8 NwPeopleProducer::CitySpecificStaticShutdown() {
  ScRelease(&occupantManager);
  ScRelease(&streetLayer);
  return 1;
}

bool NwPeopleProducer::IncludeRoads(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRoad) == kOcGeomRoad;
}

// Select a weighted or date-matched mode, then round its mean population using the original random stream.
void NwPeopleProducer::SelectBehaviorMode() {
  u32 count = attributes->GetNumberOfModes();
  u32 selection = attributes->GetModeSelectionMethod();
  if (selection == 0) {
    u32 draw = NwPeopleProducerRandom.Bounded(1000);
    for (u32 i = 0; i < count; ++i) {
      mode = attributes->GetMode(i);
      if (draw <= mode->threshold)
        break;
    }
  } else if (selection == 1) {
    u32 month, day, year;
    NwGetStreetCity()->GetDate(&month, &day, &year);
    for (u32 i = 0; i < count; ++i) {
      mode = attributes->GetMode(i);
      u32 startMonth = mode->threshold >> 24, startDay = (mode->threshold >> 16) & 0xff;
      u32 endMonth = (mode->threshold >> 8) & 0xff, endDay = mode->threshold & 0xff;
      if ((startMonth < endMonth || (startMonth == endMonth && startDay <= endDay)) && month >= startMonth &&
          month <= endMonth && (month != startMonth || day >= startDay) && (month != endMonth || day <= endDay)) {
        CtDate endDate(endMonth, endDay, year);
        date.serial = endDate.serial;
        break;
      }
    }
  }
  f32 mean = mode->meanPeople;
  // x87 returns the integer-indefinite value for non-finite or out-of-range conversions; its low word is zero.
  i64 whole = mean >= -0x1p63f && mean < 0x1p63f ? i64(mean) : INT64_MIN;
  targetPeople = u32(whole);
  f32 fraction = f32(f64(mean) - f64(targetPeople));
  if (fraction > NwPeopleProducerRandom.RandomDoubleUniform())
    ++targetPeople;
}

// Count roads in the visible intersection and distribute the requested people across matching occupied cells.
void NwPeopleProducer::PutPeopleInRect(const GeCellRect &area) {
  if (peopleCount >= targetPeople)
    return;
  GeCellRect intersection;
  if (!RectIntersect(area, *static_cast<NwStreetLayer *>(streetLayer)->GetStrtSimRect(), &intersection))
    return;
  u32 wanted = targetPeople - peopleCount;
  u32 cellCount = (intersection.right - intersection.left + 1) * (intersection.bottom - intersection.top + 1);
  GeBounds bounds(intersection);
  u32 roadCount = 0;
  occupantManager->GetOccupantsInCell(bounds, cellCount, nullptr, &roadCount, IncludeRoads);
  if (!roadCount)
    return;
  if (4 * roadCount < wanted)
    wanted = 4 * roadCount;
  if (roadCount < wanted) {
    u32 perRoad = targetPeople / roadCount, remainder = targetPeople % roadCount, carry = 0;
    for (u32 cellZ = intersection.top; cellZ <= intersection.bottom; ++cellZ) {
      for (u32 cellX = intersection.left; cellX <= intersection.right; ++cellX) {
        ScOccupant *occupant;
        if (occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant)) {
          if (IncludeRoads(occupant)) {
            carry += remainder;
            if (carry < roadCount) {
              PutPeopleOnRoadTile(occupant, perRoad);
            } else {
              PutPeopleOnRoadTile(occupant, perRoad + 1);
              carry -= roadCount;
            }
          }
          occupant->Release();
        }
      }
    }
  } else {
    u32 carry = 0;
    for (u32 cellZ = intersection.top; cellZ <= intersection.bottom; ++cellZ) {
      for (u32 cellX = intersection.left; cellX <= intersection.right; ++cellX) {
        ScOccupant *occupant;
        if (occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant)) {
          if (IncludeRoads(occupant)) {
            carry += wanted;
            if (carry >= roadCount) {
              PutPeopleOnRoadTile(occupant, 1);
              carry -= roadCount;
            }
          }
          occupant->Release();
        }
      }
    }
  }
}

NwSubwayProducer::NwSubwayProducer()
    : attributes(nullptr), references(0), initialized(0), occupant(nullptr), direction(0xff),
      elapsedTicks(0), nextTick(0), produceVehicles(true) {
}

NwSubwayProducer::~NwSubwayProducer() {
  if (initialized)
    Shutdown();
}

u32 NwSubwayProducer::AddRef() {
  return ++references;
}

u32 NwSubwayProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Retain the station and attributes and select the first production interval.
u8 NwSubwayProducer::Init(ScOccupant *station, NwTrainProducerAttrib *value) {
  occupant = ScAddRefNotNull(station);
  attributes = ScAddRefNotNull(value);
  u32 next = NwSubwayProducerRandom.Bounded(attributes->maximum);
  elapsedTicks = 0;
  nextTick = next;
  initialized = 1;
  return 1;
}

void NwSubwayProducer::Shutdown() {
  if (initialized) {
    initialized = 0;
    occupant->Release();
    occupant = nullptr;
    attributes->Release();
    attributes = nullptr;
  }
}

bool NwSubwayProducer::SetVehicleProduceState(bool enabled) {
  produceVehicles = enabled;
  return enabled;
}

// Advance the producer clock and schedule the next subway after a production attempt.
void NwSubwayProducer::Tick(u32 elapsed) {
  elapsedTicks += elapsed;
  if (elapsedTicks >= nextTick) {
    elapsedTicks = 0;
    CreateASubwayTrain();
    nextTick = NwSubwayProducerRandom.Range(attributes->minimum, attributes->maximum);
  }
}

// Publish the original train-producer identity and acquire its reference through the live receiver.
bool NwTrainProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

void NwTrainProducer::VehicleDisappears(NwVehicle *) {
}

bool NwTrainProducer::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

u8 NwTrainProducer::CitySpecificStaticInit(CtCity *city) {
  occupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  maxX = city->CellCountX() - 1;
  maxZ = city->CellCountZ() - 1;
  return 1;
}

u8 NwTrainProducer::CitySpecificStaticShutdown() {
  ScRelease(&occupantManager);
  return 1;
}

bool NwTrainProducer::IncludeRail(ScOccupant *candidate) {
  return (candidate->GetGeomFlag() & kOcGeomRail) == kOcGeomRail;
}

// Publish the original subway-producer identity and acquire its reference through the live receiver.
bool NwSubwayProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

void NwSubwayProducer::VehicleDisappears(NwVehicle *) {
}

bool NwSubwayProducer::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

u8 NwSubwayProducer::CitySpecificStaticInit(CtCity *city) {
  occupantManager = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
  maxX = city->CellCountX() - 1;
  maxZ = city->CellCountZ() - 1;
  agentTypeTree = ScAddRefNotNull(NwGetStreetAgentTypeTree());
  return 1;
}

u8 NwSubwayProducer::CitySpecificStaticShutdown() {
  ScRelease(&agentTypeTree);
  ScRelease(&occupantManager);
  return 1;
}

bool NwSubwayProducer::IncludeSubway(ScOccupant *candidate) {
  return agentTypeTree->IsAncestor(kCtAgentMovableSubway, candidate->TypeId());
}

NwBoatProducer::NwBoatProducer()
    : initialized(0), byte5(0), references(0), boatCount(0), targetBoatCount(20) {
}

// Create a boat from the selected production attributes and retain its producer.
NwBoatVehicle *NwBoatProducer::CreateABoat(NwVehicleProducerAttrib *attributes) {
  if (!initialized || !attributes)
    return nullptr;
  NwBoatVehicle *boat = new NwBoatVehicle;
  boat->AddRef();
  RsKey key;
  attributes->GetAVehicleType(&key);
  if (!boat->Init(key)) {
    boat->Release();
    return nullptr;
  }
  boat->SetProducer(this);
  ++boatCount;
  return boat;
}

NwBoatProducer::~NwBoatProducer() {
  Shutdown();
}

u32 NwBoatProducer::AddRef() {
  return ++references;
}

u32 NwBoatProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Release retained marina occupants and clear their list when the producer shuts down.
// Linux identifies the original member and its list element type.
void NwBoatProducer::Shutdown() {
  if (!initialized)
    return;
  initialized = 0;
  for (ScOccupant *marina : marinas)
    marina->Release();
  marinas.clear();
}

// Remove a disappeared boat from the producer's borrowed boat list.
// Linux identifies the original member and its list element type.
void NwBoatProducer::VehicleDisappears(NwVehicle *vehicle) {
  if (!initialized)
    return;
  --boatCount;
  auto *boat = static_cast<NwBoatVehicle *>(vehicle);
  for (auto it = boats.begin(); it != boats.end(); ++it) {
    if (*it == boat) {
      boats.erase(it);
      break;
    }
  }
}

bool NwBoatProducer::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

// Initialize the producer and schedule its first randomized boat-production interval.
u8 NwBoatProducer::Init() {
  initialized = 1;
  elapsedTicks = 0;
  nextTick = static_cast<u32>(NwBoatProducerRandom.Range(kNwBoatMinimumDelay, kNwBoatMaximumDelay));
  return 1;
}

// Classify the centers of the original sixteen-cell grid as available or blocked boat locations.
void NwBoatProducer::DeterminePointsWhereBoatsCanBePlaced() {
  for (u32 x = 8; x < cellsX; x += 16) {
    for (u32 z = 8; z < cellsZ; z += 16) {
      u32 cell = z + (x << 16);
      if (CanPlaceBoat(x, z))
        availableCells.push_back(cell);
      else
        blockedCells.push_back(cell);
    }
  }
}

// Check whether any retained marina lies within the configured squared distance of the cell.
bool NwBoatProducer::IsCloseToMarina(i32 x, i32 z) {
  for (ScOccupant *marina : marinas) {
    u32 marinaX, marinaZ;
    marina->GetLocationInCellCoords(&marinaX, &marinaZ);
    u32 deltaX = marinaX - u32(x), deltaZ = marinaZ - u32(z);
    if (deltaX * deltaX + deltaZ * deltaZ <= maxMarinaDistanceSquared)
      return true;
  }
  return false;
}

bool NwBoatProducer::IsGridContainingCellUnoccupiedByBoats(u32 x, u32 z) {
  for (NwBoatVehicle *boat : boats) {
    u32 boatX, boatZ;
    boat->GetLocationInCellCoords(&boatX, &boatZ);
    if ((boatX >> 4) == (x >> 4) && (boatZ >> 4) == (z >> 4))
      return false;
  }
  return true;
}

// Select the first available cell near a marina with no boat in its grid block.
bool NwBoatProducer::FindAWaterPointToPlaceBoat(u32 *outX, u32 *outZ) {
  for (auto it = availableCells.begin(); it != availableCells.end(); ++it) {
    u32 cell = *it;
    *outX = cell >> 16;
    *outZ = static_cast<u16>(cell);
    if (IsCloseToMarina(static_cast<i32>(*outX), static_cast<i32>(*outZ)) &&
        IsGridContainingCellUnoccupiedByBoats(*outX, *outZ)) {
      availableCells.erase(it);
      availableCells.push_front(cell);
      return true;
    }
  }
  return false;
}

void NwBoatProducer::MarinaInsertedIntoCity(ScOccupant *occupant) {
  occupant->AddRef();
  marinas.push_back(occupant);
}

// Release and remove the first matching marina from the producer's list.
void NwBoatProducer::MarinaRemovedFromCity(ScOccupant *occupant) {
  for (auto it = marinas.begin(); it != marinas.end(); ++it) {
    if (*it == occupant) {
      occupant->Release();
      marinas.erase(it);
      return;
    }
  }
}

// Collect up to the requested number of boat occupants before removing them from the animation manager.
void NwBoatProducer::RemoveBoats(u32 count) {
  std::vector<ScOccupant *> selected;
  OcIterator *iterator;
  if (!animationManager->GetIterator(&iterator))
    return;
  ScOccupant *occupant = iterator->Reset();
  while (occupant && selected.size() < count) {
    if (boatFilter->Test(occupant))
      selected.push_back(occupant);
    occupant = iterator->Next();
  }
  for (ScOccupant *boat : selected)
    animationManager->RemoveOccupant(boat);
  iterator->Release();
}

u8 NwBoatProducer::CreateAndPlaceABoat() {
  if (availableCells.empty())
    return 0;
  u32 x, z;
  if (!FindAWaterPointToPlaceBoat(&x, &z))
    return 0;
  auto *attributes = static_cast<NwVehicleProducerAttrib *>(
      pollutionLayer->IsSaltWater(x, z) ? saltWaterAttributes : freshWaterAttributes);
  NwBoatVehicle *boat = CreateABoat(attributes);
  if (!boat)
    return 0;
  u8 placed = boat->PutInCell(x, z);
  if (placed)
    boats.push_front(boat);
  else
    boat->Shutdown();
  boat->Release();
  return placed;
}

void NwBoatProducer::PlaceInitialBoats() {
  DeterminePointsWhereBoatsCanBePlaced();
  for (u32 i = 0; i < targetBoatCount; ++i) {
    if (!CreateAndPlaceABoat())
      break;
  }
}

void NwBoatProducer::Tick(u32 elapsed) {
  if (!byte5) {
    byte5 = 1;
    PlaceInitialBoats();
  }
  elapsedTicks += elapsed;
  if (elapsedTicks >= nextTick) {
    elapsedTicks = 0;
    if (boatCount < targetBoatCount)
      CreateAndPlaceABoat();
    if (!blockedCells.empty()) {
      u32 cell = blockedCells.front();
      blockedCells.pop_front();
      if (CanPlaceBoat(cell >> 16, static_cast<u16>(cell)))
        availableCells.push_front(cell);
      else
        blockedCells.push_back(cell);
    }
    nextTick = static_cast<u32>(NwBoatProducerRandom.Range(kNwBoatMinimumDelay, kNwBoatMaximumDelay));
  }
}

// Require an open water cell with enough contiguous water in both coordinate directions.
bool NwBoatProducer::CanPlaceBoat(u32 x, u32 z) {
  if (dirtBag->CellWaterVertCount(x, z) != 4)
    return false;
  u32 acrossX = 0, acrossZ = 0;
  for (u32 distance = 1; distance <= 8; ++distance) {
    if (dirtBag->CellWaterVertCount(x - distance, z) != 4)
      break;
    ++acrossX;
  }
  for (u32 distance = 1; distance <= 7; ++distance) {
    if (dirtBag->CellWaterVertCount(x + distance, z) != 4)
      break;
    ++acrossX;
  }
  if (acrossX < 4)
    return false;
  for (u32 distance = 1; distance <= 8; ++distance) {
    if (dirtBag->CellWaterVertCount(x, z - distance) != 4)
      break;
    ++acrossZ;
  }
  for (u32 distance = 1; distance <= 7; ++distance) {
    if (dirtBag->CellWaterVertCount(x, z + distance) != 4)
      break;
    ++acrossZ;
  }
  return acrossZ >= 4 && acrossX + acrossZ >= 16;
}

void NwBoatProducer::BoatHitLand(u32 x, u32 z) {
  u32 cell = z + (x << 16);
  if (CanPlaceBoat(x, z))
    availableCells.push_back(cell);
  else
    blockedCells.push_back(cell);
}

// Store the desired boat count and remove the excess when the current population is larger.
void NwBoatProducer::SetNumberOfBoatsToProduce(u32 count) {
  targetBoatCount = count;
  if (count < boatCount)
    RemoveBoats(boatCount - count);
}

// Publish the original boat-producer identity and acquire its reference through the live receiver.
bool NwBoatProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

NwHelicopterProducer::NwHelicopterProducer()
    : attributes(nullptr), initialized(0), references(0), currentRoutineCount(0), targetRoutineCount(0) {
}

NwHelicopterProducer::~NwHelicopterProducer() {
  Shutdown();
}

// Publish the producer interface and acquire its live reference.
bool NwHelicopterProducer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleProducerInterface);
  HANDLE_QUERY_INTERFACE(IID_NwVehicleProducerInterface, NwVehicleProducerInterface);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwHelicopterProducer::AddRef() {
  return ++references;
}

u32 NwHelicopterProducer::Release() {
  return ScReleaseImpl(this, &references);
}

// Release the retained air-traffic resource after clearing the initialized flag.
void NwHelicopterProducer::Shutdown() {
  if (initialized) {
    initialized = 0;
    ScRelease(&attributes);
  }
}

// Retain air-traffic attributes and choose the first routine-flight interval.
u8 NwHelicopterProducer::Init(NwAirTrafficAttrib *value) {
  initialized = 1;
  attributes = ScAddRefNotNull(value);
  elapsedTicks = 0;
  nextTick = NwHelicopterProducerRandom.Range(kNwHelicopterMinimumDelay, kNwHelicopterMaximumDelay);
  return 1;
}

// Advance the routine-flight clock and schedule the next attempt after creating a helicopter.
void NwHelicopterProducer::Tick(u32 elapsed) {
  elapsedTicks += elapsed;
  if (elapsedTicks >= nextTick) {
    CreateANormalRoutineHelicopter();
    elapsedTicks = 0;
    nextTick = NwHelicopterProducerRandom.Range(kNwHelicopterMinimumDelay, kNwHelicopterMaximumDelay);
  }
}

// Create a retained helicopter after its producer is initialized and the city's year reaches 1936.
NwHelicopter *NwHelicopterProducer::CreateAHelicopter() {
  if (!initialized)
    return nullptr;
  u32 dayOfYear, year;
  city->GetDate(&dayOfYear, &year);
  if (year < 1936)
    return nullptr;
  NwHelicopter *helicopter = new NwHelicopter;
  helicopter->AddRef();
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, attributes->GetAPlaneResInstId());
  if (!helicopter->Init(key)) {
    helicopter->Release();
    return nullptr;
  }
  helicopter->SetProducer(this);
  return helicopter;
}

void NwHelicopterProducer::CreateANormalRoutineHelicopter() {
  if (static_cast<i32>(currentRoutineCount) < static_cast<i32>(targetRoutineCount)) {
    NwHelicopter *helicopter = CreateAHelicopter();
    if (helicopter) {
      ++currentRoutineCount;
      if (!helicopter->MakeNormalRoutineHelicopter())
        helicopter->Shutdown();
      helicopter->Release();
    }
  }
}

// Create and dispatch a helicopter, releasing the producer's temporary reference.
NwHelicopter *NwHelicopterProducer::DispatchHelicopter(u32 startX, u32 startZ, u32 targetX, u32 targetZ) {
  NwHelicopter *helicopter = CreateAHelicopter();
  if (!helicopter)
    return nullptr;
  NwHelicopter *result = helicopter;
  if (!helicopter->MakeDisasterFightingHelicopter(static_cast<i32>(startX), static_cast<i32>(startZ),
      static_cast<i32>(targetX), static_cast<i32>(targetZ))) {
    helicopter->Shutdown();
    result = nullptr;
  }
  helicopter->Release();
  return result;
}

void NwHelicopterProducer::VehicleDisappears(NwVehicle *vehicle) {
  NwHelicopter *helicopter = static_cast<NwHelicopter *>(vehicle);
  if (helicopter->IsNormalRoutineHelicopter())
    --currentRoutineCount;
}

bool NwHelicopterProducer::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

u8 NwHelicopterProducer::CitySpecificStaticInit(CtCity *value) {
  city = ScAddRefNotNull(value);
  return 1;
}

u8 NwHelicopterProducer::CitySpecificStaticShutdown() {
  ScRelease(&city);
  return 1;
}

void NwHelicopterProducer::SetNumNormalRoutineHelicopters(u32 count) {
  targetRoutineCount = count;
}

u8 NwBoatProducer::CitySpecificStaticInit(CtCity *city) {
  dirtBag = ScAddRefNotNull(city->DirtBag());
  pollutionLayer = ScAddRefNotNull(city->PollutionLayer());
  cellsX = city->CellCountX();
  cellsZ = city->CellCountZ();
  boatFilter = ScAddRefNotNull(new NwBoatFilter);
  animationManager = ScAddRefNotNull(city->AnimOccupantManager());
  return 1;
}

u8 NwBoatProducer::CitySpecificStaticShutdown() {
  ScRelease(&animationManager);
  ScRelease(&boatFilter);
  ScRelease(&pollutionLayer);
  ScRelease(&dirtBag);
  return 1;
}

// Load the salt-water and fresh-water production resources and optional marina-distance tuning.
u8 NwBoatProducer::StaticInit() {
  RsManager *manager = NwGetStreetResourceManager();
  RsKey key(kRsVehicleProducerAttributesResourceType, kRsVehicleProducerAttributesGroup, 1024);
  RsResource *resource = nullptr;
  if (manager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
    saltWaterAttributes = resource;
  else
    saltWaterAttributes = nullptr;
  key.instance = 1025;
  if (manager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
    freshWaterAttributes = resource;
  else
    freshWaterAttributes = nullptr;
  ScString path, value;
  ApCompressedIni ini;
  NwGetStreetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3StrtSimLayer.INI");
  ini.SetPath(path);
  NwGetStreetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  ScString section("MiscStrtSimTunables"), setting("MaxMarinaToBoatDistance");
  if (ini.ReadEntry(section, setting, &value)) {
    u32 distance = ScConvertToUint32(value.ToChar());
    maxMarinaDistanceSquared = distance * distance;
  }
  return 1;
}

u8 NwBoatProducer::StaticShutdown() {
  ScRelease(&saltWaterAttributes);
  ScRelease(&freshWaterAttributes);
  return 1;
}
