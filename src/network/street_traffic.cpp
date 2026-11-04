#define _CRT_SECURE_NO_WARNINGS
#include "network/street_traffic.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_tornado.h"
#include "disasters/ds_toxic.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_building_data.h"
#include "geometry/ge_types.h"
#include "network/api.h"
#include "network/nw_street_producer.h"
#include "network/nw_street_vehicle.h"
#include "network/street.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/sprite.h"
#include "sprites/view_camera.h"

// Vehicle-node selection starts with the original squared-distance cutoff.
enum NwVehicleNodeSearch : i32 {
  kNwVehicleNodeDistanceLimit = 0x989680,
};

// People-node searches, decimal date selection and train interval input retain their distinct scale values.
enum NwTrafficInputScale : i32 {
  kNwPeopleDateStartMonthScale = 1000000,
  kNwTrainIntervalInputScale = 1000000,
  kNwPeopleNodeDistanceLimit = 0xf4240,
};

// Weighted-mode and weighted-entry thresholds retain the original wrapped origin before the first weight.
enum NwCumulativeWeightOrigin : u32 {
  kNwCumulativeWeightBeforeFirst = 0xffffffff,
};

// People-mode selectors at or above this threshold use the packed date-range encoding rather than cumulative weights.
enum NwPeopleSelectionEncoding : u32 {
  kNwPeopleDateSelectionThreshold = 0xf69b5,
};

// Street animation checks emergency dispatch after this elapsed interval.
enum NwEmergencyDispatchDelay : i32 {
  kNwEmergencyDispatchInterval = 0xe4e1c0,
};

RsManager *NwStreetResourceManager;
NwTrafficAttrib *NwDefaultTrafficAttributes;
NwAirTrafficAttrib *NwStreetLayer::helicopterAttributes;
NwAirTrafficAttrib *NwStreetLayer::airTrafficAttributes;
NwTrainProducerAttrib *NwStreetLayer::trainAttributes;
NwTrainProducerAttrib *NwStreetLayer::subwayAttributes;
u8 NwStreetLayer::backgroundResourceLoadingEnabled = 1;
i32 NwStreetLayer::backgroundResourceLoadingCompleted;
i32 NwStreetLoaderState;

// Read a loader flag atomically while the background thread can update it.
static i32 NwReadLoaderFlag(i32 *flag) {
#ifdef _WIN32
  return InterlockedCompareExchange(reinterpret_cast<volatile LONG *>(flag), 0, 0);
#else
  return __atomic_load_n(flag, __ATOMIC_SEQ_CST);
#endif
}

// Publish a loader flag without racing the simulation or resource loader thread.
static void NwWriteLoaderFlag(i32 *flag, i32 value) {
#ifdef _WIN32
  InterlockedExchange(reinterpret_cast<volatile LONG *>(flag), value);
#else
  __atomic_store_n(flag, value, __ATOMIC_SEQ_CST);
#endif
}

// Initialize the layer's producer collections, visible rectangles and known retained interfaces.
NwStreetLayer::NwStreetLayer()
    : initialized(0), references(0), city(nullptr), disasterLayer(nullptr), trafficLayer(nullptr),
      residentialLayer(nullptr), emergencyMode(0), vehicleEmergencyMode(0), peopleActive(0), vehiclesActive(0),
      trainsActive(1), subwayActive(1), surfaceOccupantManager(nullptr), animationManager(nullptr),
      automataFilter(nullptr), peopleFilter(nullptr), vehicleFilter(nullptr), peopleRoadFilter(nullptr),
      railFilter(nullptr), subwayFilter(nullptr), disasterCallbackUsers(0), random(kScRandomSeedFromClock),
      simulationRect{}, peopleSimRect{}, vehicleSimRect{}, peopleOriginX(0), peopleOriginZ(0), peopleProducer(nullptr),
      vehicleProducer(nullptr), helicopterProducer(nullptr), boatProducer(nullptr), strikeProducer(nullptr),
      unresolvedBeforeBorders{}, borderRects{}, safetyValues{}, disasterCount(0), manipulator(nullptr), viewRect{},
      viewCenterX(0), viewCenterZ(0), currentZoom(0), currentViewType(5), agentTypeTree(nullptr), tickManager(nullptr) {
}

NwStreetLayer::~NwStreetLayer() {
  if (initialized)
    Shutdown();
}

u32 NwStreetLayer::Release() {
  return ScReleaseImpl(this, &references);
}

NwPeopleProducerAttrib *NwStreetLayer::globalPeopleAttributes;
NwVehicleProducerAttrib *NwStreetLayer::globalVehicleAttributes;

// Load street simulation resources and vehicle lists, then initialize the shared producer subsystems.
bool NwStreetLayer::StaticInit(CtDefinition *) {
  NwStreetResourceManager = NwGetStreetResourceManager();
  ScString path;
  ApCompressedIni ini;
  NwGetStreetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3StrtSimLayer.INI");
  ini.SetPath(path);
  NwGetStreetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SetCompressedPath(path);
  if (NwGetStreetPerformanceInfo()->memory_mb > 48) {
    ini.EnumEntries(ScString("KeyVehiclesForHighMemoryMachines"), AddKeyVehicleResInstId, nullptr);
    ini.EnumEntries(ScString("EmergencyVehiclesForHighMemoryMachines"), AddEmergencyVehicleResInstId, nullptr);
  } else {
    ini.EnumEntries(ScString("KeyVehiclesForLowMemoryMachines"), AddKeyVehicleResInstId, nullptr);
    ini.EnumEntries(ScString("EmergencyVehiclesForLowMemoryMachines"), AddEmergencyVehicleResInstId, nullptr);
  }
  NwPerson::StaticInit();
  NwStreetVehicle::StaticInit();
  NwAeroplane::StaticInit();
  NwHelicopter::StaticInit();
  NwShipVehicle::StaticInit();
  NwBoatVehicle::StaticInit();
  NwRiotCrowdObject::StaticInit();
  NwParadeBlockObject::StaticInit();
  NwBoatProducer::StaticInit();
  NwShipProducer::StaticInit();
  NwDefaultTrafficAttributes = GetSSTrBlockAttrib(7424);
  if (NwDefaultTrafficAttributes)
    NwDefaultTrafficAttributes->AddRef();
  RsResource *resource = nullptr;
  RsKey key(kRsPeopleProducerAttributesResourceType, kRsPeopleProducerAttributesGroup, 1);
  u8 result = NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource));
  globalPeopleAttributes = result == 1 ? static_cast<NwPeopleProducerAttrib *>(resource) : nullptr;
  key = RsKey(kRsVehicleProducerAttributesResourceType, kRsVehicleProducerAttributesGroup, 1);
  result = NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource));
  globalVehicleAttributes = result == 1 ? static_cast<NwVehicleProducerAttrib *>(resource) : nullptr;
  key = RsKey(kRsTrainProducerAttributesResourceType, kRsTrainProducerAttributesGroup, 1);
  NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource));
  trainAttributes = reinterpret_cast<NwTrainProducerAttrib *>(resource);
  key.instance = 2;
  NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource));
  subwayAttributes = reinterpret_cast<NwTrainProducerAttrib *>(resource);
  key = RsKey(kRsAirTrafficAttributesResourceType, kRsAirTrafficAttributesGroup, 1);
  NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource));
  airTrafficAttributes = reinterpret_cast<NwAirTrafficAttrib *>(resource);
  key.instance = 2;
  NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource));
  helicopterAttributes = reinterpret_cast<NwAirTrafficAttrib *>(resource);
  ApPreferences *preferences = NwGetStreetPreferences();
  if (preferences && preferences->VisibilityPrefs()->traffic_visibility <= 0)
    backgroundResourceLoadingEnabled = 0;
  return true;
}

// Stop background loading and release the shared street resources in their original shutdown order.
bool NwStreetLayer::StaticShutdown() {
  if (backgroundResourceLoadingEnabled)
    EndBackgroundResLoader();
  keyVehicles.clear();
  NwShipProducer::StaticShutdown();
  NwBoatProducer::StaticShutdown();
  NwRiotCrowdObject::StaticShutdown();
  NwParadeBlockObject::StaticShutdown();
  NwShipVehicle::StaticShutdown();
  NwShipVehicle::StaticShutdown();
  NwHelicopter::StaticShutdown();
  NwAeroplane::StaticShutdown();
  NwStreetVehicle::StaticShutdown();
  NwPerson::StaticShutdown();
  helicopterAttributes->Release();
  helicopterAttributes = nullptr;
  airTrafficAttributes->Release();
  airTrafficAttributes = nullptr;
  trainAttributes->Release();
  trainAttributes = nullptr;
  subwayAttributes->Release();
  subwayAttributes = nullptr;
  ScRelease(&globalVehicleAttributes);
  ScRelease(&globalPeopleAttributes);
  for (auto &entry : trafficAttributes)
    entry.second->Release();
  trafficAttributes.clear();
  ScRelease(&NwDefaultTrafficAttributes);
  for (auto &entry : vehicleAttributes)
    entry.second->Release();
  vehicleAttributes.clear();
  for (auto &entry : personAttributes)
    entry.second->Release();
  personAttributes.clear();
  for (auto &entry : animationAttributes)
    entry.second->Release();
  animationAttributes.clear();
  for (auto &entry : peoplePaths)
    entry.second->Release();
  peoplePaths.clear();
  for (auto &entry : vehiclePaths)
    entry.second->Release();
  vehiclePaths.clear();
  for (auto &entry : scripts)
    entry.second->Release();
  scripts.clear();
  emergencyVehicles.clear();
  return true;
}

bool NwStreetLayer::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    *out = manipulator;
    *flags = 3;
  }
  return true;
}

void NwStreetLayer::CreateParadeSpectators(u32 x, u32 z, u32 count) {
  if (peopleProducer)
    peopleProducer->SpreadSpectators(x, z, count);
}

// Reset the people safety grid and reapply each currently active disaster.
void NwStreetLayer::InitDisasterInfo() {
  if (!disasterCallbackUsers || !peopleActive)
    return;
  disasterCount = emergencyMode ? 1 : 0;
  i16 value = emergencyMode ? 150 : 0;
  for (i32 i = 0; i < 70 * 70; ++i)
    safetyValues[i] = value;
  DsListIterator *iterator = disasterLayer->CreateActiveDisasterIterator();
  DsInstance *instance;
  while ((instance = iterator->Next()) != nullptr)
    DisasterAdd(instance);
  disasterLayer->DeleteActiveDisasterIterator(iterator);
}

// Create people scenes once for each occupant whose origin lies inside the requested rectangle.
void NwStreetLayer::CreatePeopleScene(const GeCellRect &rect) {
  for (u32 z = rect.top; z <= rect.bottom; ++z) {
    for (u32 x = rect.left; x <= rect.right; ++x) {
      ScOccupant *occupant;
      if (surfaceOccupantManager->GetOccupantAtCell(x, z, &occupant)) {
        u32 originX, originZ;
        occupant->GetLocationInCellCoords(&originX, &originZ);
        if (x == originX && z == originZ)
          CreatePeopleSceneForOccupant(occupant);
        occupant->Release();
      }
    }
  }
}

// Remove people scenes once for each occupant whose origin lies inside the requested rectangle.
void NwStreetLayer::ErasePeopleScene(const GeCellRect &rect) {
  for (u32 z = rect.top; z <= rect.bottom; ++z) {
    for (u32 x = rect.left; x <= rect.right; ++x) {
      ScOccupant *occupant;
      if (surfaceOccupantManager->GetOccupantAtCell(x, z, &occupant)) {
        u32 originX, originZ;
        occupant->GetLocationInCellCoords(&originX, &originZ);
        if (x == originX && z == originZ)
          ErasePeopleSceneForOccupant(occupant);
        occupant->Release();
      }
    }
  }
}

void NwStreetLayer::CreatePeopleSceneForOccupant(ScOccupant *occupant) {
  if (peopleScenes.find(occupant) != peopleScenes.end())
    return;
  auto *attributes =
      reinterpret_cast<NwPeopleProducerAttribInterface *>(occupant->GetAttribData()->GetPeopleProducerAttrib());
  if (!attributes)
    return;
  NwPeopleProducer *producer = new NwPeopleProducer;
  producer->AddRef();
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  producer->Init(attributes, x, z);
  peopleScenes.insert({occupant, producer});
}

// Shut down and release an occupant's retained people producer before removing it from the scene map.
void NwStreetLayer::ErasePeopleSceneForOccupant(ScOccupant *occupant) {
  auto entry = peopleScenes.find(occupant);
  if (entry == peopleScenes.end())
    return;
  entry->second->Shutdown();
  entry->second->Release();
  peopleScenes.erase(entry);
}

// Remove people producers and their animated occupants when people simulation is active.
void NwStreetLayer::ShutdownPeople() {
  if (!peopleActive)
    return;
  if (peopleProducer) {
    peopleProducer->Shutdown();
    peopleProducer->Release();
    peopleProducer = nullptr;
  }
  for (auto &entry : peopleScenes) {
    entry.second->Shutdown();
    entry.second->Release();
  }
  peopleScenes.clear();
  animationManager->RemoveOccupants(nullptr, peopleFilter);
  peopleActive = 0;
}

// Stop vehicle production and remove the corresponding animated occupants.
void NwStreetLayer::ShutdownVehicles() {
  if (!vehiclesActive)
    return;
  vehicleProducer->SetVehicleProduceState(false);
  animationManager->RemoveOccupants(nullptr, vehicleFilter);
  vehiclesActive = 0;
}

// Enable, disable or update the people scenes as the visible simulation rectangle changes.
void NwStreetLayer::UpdatePeopleView() {
  if (currentZoom < NwPerson::BirthZoomLevel() || currentViewType != 0 || emergencyMode == 2) {
    if (peopleActive) {
      UnregisterDisasterCallback();
      ShutdownPeople();
    }
    return;
  }
  if (!peopleActive) {
    peopleActive = 1;
    RegisterDisasterCallback();
    InitDisasterInfo();
    peopleSimRect = simulationRect;
    CreateGlobalPplProducer();
    CreatePeopleScene(peopleSimRect);
    return;
  }
  if (IdenticalRects(peopleSimRect, simulationRect))
    return;
  InitDisasterInfo();
  GeCellRect strips[4];
  i32 count;
  SubtractRect(peopleSimRect, simulationRect, strips, &count);
  peopleProducer->SetRemovalCompensation(false);
  for (i32 i = 0; i < count; ++i)
    ErasePeopleScene(strips[i]);
  if (count > 0) {
    peopleRectFilter->SetRectangles(strips, &count);
    animationManager->RemoveOccupants(nullptr, peopleRectFilter);
  }
  SubtractRect(simulationRect, peopleSimRect, strips, &count);
  peopleSimRect = simulationRect;
  peopleProducer->SetRemovalCompensation(true);
  for (i32 i = 0; i < count; ++i)
    CreatePeopleScene(strips[i]);
  peopleProducer->ChangeLocation(peopleOriginX, peopleOriginZ);
}

// Enable vehicle production and remove vehicles outside the current visible simulation rectangle.
void NwStreetLayer::UpdateVehicleView() {
  if (currentZoom < NwStreetVehicle::BirthZoomLevel() || currentViewType != 0) {
    if (vehiclesActive) {
      UnregisterDisasterCallback();
      ShutdownVehicles();
    }
    return;
  }
  if (!vehiclesActive) {
    vehiclesActive = 1;
    vehicleProducer->SetVehicleProduceState(true);
    RegisterDisasterCallback();
    vehicleSimRect = simulationRect;
    vehicleProducer->SetSimRect(vehicleSimRect);
    if (emergencyMode == 2) {
      vehicleProducer->SetEmergencyMode(true);
      vehicleEmergencyMode = 1;
    }
    vehicleProducer->PutVehiclesInRect(vehicleSimRect);
    return;
  }
  if (emergencyMode == 2) {
    if (!vehicleEmergencyMode) {
      vehicleEmergencyMode = 1;
      vehicleProducer->SetEmergencyMode(true);
      animationManager->RemoveOccupants(nullptr, nonEmergencyVehicleFilter);
    }
  } else if (vehicleEmergencyMode) {
    vehicleProducer->SetEmergencyMode(false);
    vehicleEmergencyMode = 0;
  }
  if (IdenticalRects(vehicleSimRect, simulationRect))
    return;
  GeCellRect strips[4];
  i32 count;
  SubtractRect(vehicleSimRect, simulationRect, strips, &count);
  if (count > 0) {
    roadRectFilter->SetRectangles(strips, &count);
    animationManager->RemoveOccupants(nullptr, roadRectFilter);
  }
  SubtractRect(simulationRect, vehicleSimRect, strips, &count);
  vehicleSimRect = simulationRect;
  vehicleProducer->SetSimRect(vehicleSimRect);
  for (i32 i = 0; i < count; ++i)
    vehicleProducer->PutVehiclesInRect(vehicleSimRect);
}

void NwStreetLayer::UpdateTrainsView() {
  NwRailVehicle::SetCurrentViewType(currentViewType);
  if (currentZoom >= NwRailVehicle::BirthZoomLevel()) {
    if (!trainsActive) {
      trainsActive = 1;
      for (auto &entry : trainProducers)
        entry.second->SetVehicleProduceState(true);
    }
  } else if (trainsActive) {
    trainsActive = 0;
    for (auto &entry : trainProducers)
      entry.second->SetVehicleProduceState(false);
    animationManager->RemoveOccupants(nullptr, railFilter);
  }
}

// Refresh traffic targets and boat and helicopter counts for a city simulation step.
bool NwStreetManipulator::Apply(CtAgent *) {
  if (layer->vehiclesActive)
    layer->vehicleProducer->UpdateSimRect();
  layer->UpdateNumberOfBoats();
  layer->UpdateNumberOfHelicopters();
  return true;
}

void NwStreetLayer::UpdateSubwayView() {
  NwSubwayVehicle::SetCurrentViewType(currentViewType);
  if (currentZoom >= NwSubwayVehicle::BirthZoomLevel()) {
    if (!subwayActive) {
      subwayActive = 1;
      for (auto &entry : subwayProducers)
        entry.second->SetVehicleProduceState(true);
    }
  } else if (subwayActive) {
    subwayActive = 0;
    for (auto &entry : subwayProducers)
      entry.second->SetVehicleProduceState(false);
    animationManager->RemoveOccupants(nullptr, subwayFilter);
  }
}

void NwStreetLayer::PierAdded(ScOccupant *occupant) {
  for (auto &entry : shipProducers) {
    if (entry.second->AddPierTileIfConnected(occupant))
      return;
  }
  NwShipProducer *producer = new NwShipProducer;
  producer->AddRef();
  producer->Init(occupant);
  shipProducers.insert({occupant, producer});
}

void NwStreetLayer::PierRemoved(ScOccupant *occupant) {
  auto entry = shipProducers.find(occupant);
  if (entry == shipProducers.end())
    return;
  entry->second->Release();
  shipProducers.erase(entry);
}

void NwStreetLayer::RunwayTileAdded(ScOccupant *occupant) {
  for (NwAirTrafficManager *manager : airTrafficManagers) {
    if (manager->AddRunwayTileIfConnected(occupant))
      return;
  }
  NwAirTrafficManager *manager = new NwAirTrafficManager;
  manager->AddRef();
  manager->Init(occupant, airTrafficAttributes);
  airTrafficManagers.push_back(manager);
}

// Shut down the airport manager whose anchor runway tile was removed.
void NwStreetLayer::RunwayTileRemoved(ScOccupant *occupant) {
  for (auto entry = airTrafficManagers.begin(); entry != airTrafficManagers.end(); ++entry) {
    NwAirTrafficManager *manager = *entry;
    if (manager->AnchorTile() == occupant) {
      manager->Shutdown();
      manager->Release();
      airTrafficManagers.erase(entry);
      return;
    }
  }
}

// Add traffic producers, airport or pier resources, and a people scene for an inserted surface occupant.
bool NwStreetLayer::OccupantInserted(u32 sender, ScOccupant *occupant) {
  u32 type = occupant->TypeId(), x = 0, z = 0;
  occupant->GetLocationInCellCoords(&x, &z);
  if (sender != surfaceSender)
    return true;
  vehicleProducer->AddVehicleTypeInfoForOccupant(occupant);
  if (agentTypeTree->IsAncestor(kCtAgentRailEndcap, type)) {
    NwTrainProducer *producer = new NwTrainProducer;
    producer->AddRef();
    producer->SetVehicleProduceState(trainsActive != 0);
    producer->Init(occupant, trainAttributes);
    trainProducers.insert({occupant, producer});
  } else {
    GePortType *port;
    if (GetPortAttrib(occupant, &port)) {
      u32 portType = port->GetPortType();
      port->Release();
      if (portType == 1)
        RunwayTileAdded(occupant);
      else if (portType == 2) {
        for (NwAirTrafficManager *manager : airTrafficManagers)
          manager->DetermineTaxiwaySide();
      } else if (portType == 9)
        PierAdded(occupant);
    } else if (agentTypeTree->IsAncestor(kCtAgentMarina, type) && boatProducer) {
      boatProducer->MarinaInsertedIntoCity(occupant);
    }
  }
  if (peopleActive && CellIsInPeopleSimRect(x, z))
    CreatePeopleSceneForOccupant(occupant);
  return true;
}

// Remove the traffic and scene resources associated with a removed surface occupant.
bool NwStreetLayer::OccupantRemoved(u32 sender, ScOccupant *occupant) {
  if (!initialized)
    return true;
  u32 type = occupant->TypeId(), x = 0, z = 0;
  occupant->GetLocationInCellCoords(&x, &z);
  if (sender != surfaceSender)
    return true;
  if (agentTypeTree->IsAncestor(kCtAgentRailEndcap, type)) {
    auto entry = trainProducers.find(occupant);
    entry->second->Release();
    trainProducers.erase(entry);
  } else {
    GePortType *port;
    if (GetPortAttrib(occupant, &port)) {
      u32 portType = port->GetPortType();
      port->Release();
      if (portType == 1)
        RunwayTileRemoved(occupant);
      else if (portType == 9)
        PierRemoved(occupant);
    } else if (agentTypeTree->IsAncestor(kCtAgentMarina, type) && boatProducer) {
      boatProducer->MarinaRemovedFromCity(occupant);
    }
  }
  if (peopleActive)
    ErasePeopleSceneForOccupant(occupant);
  vehicleProducer->RemoveVehicleTypeInfoForOccupant(occupant);
  return true;
}

// Advance the emergency timer and tick each active street-simulation producer in its original order.
void NwStreetLayer::DoAnimationTick(u32 elapsed) {
  if (emergencyMode == 1) {
    emergencyTimer += elapsed;
    if (emergencyTimer >= kNwEmergencyDispatchInterval) {
      emergencyMode = 2;
      UpdatePeopleView();
      UpdateVehicleView();
    }
  }
  if (vehicleProducer)
    vehicleProducer->Tick(elapsed);
  for (NwAirTrafficManager *manager : airTrafficManagers)
    manager->Tick(elapsed);
  for (auto &entry : trainProducers)
    entry.second->Tick(elapsed);
  for (auto &entry : shipProducers)
    entry.second->Tick(elapsed);
  for (auto &entry : subwayProducers)
    entry.second->Tick(elapsed);
  if (helicopterProducer)
    helicopterProducer->Tick(elapsed);
  if (boatProducer)
    boatProducer->Tick(elapsed);
  if (peopleProducer)
    peopleProducer->Tick(elapsed);
  for (auto &entry : peopleScenes)
    entry.second->Tick(elapsed);
}

bool NwStreetLayer::SimulationBegin() {
  if (backgroundResourceLoadingEnabled && !NwReadLoaderFlag(&backgroundResourceLoadingCompleted) &&
      !NwReadLoaderFlag(&NwStreetLoaderState))
    StartBackgroundResLoader();
  for (NwAirTrafficManager *manager : airTrafficManagers)
    manager->PlaceInitialPlanes();
  for (auto &entry : shipProducers)
    entry.second->PlaceInitialShip();
  return true;
}

// Create and initialize a riot crowd using the original vehicle resource.
NwRiotCrowd *NwStreetLayer::CreateRiotCrowd() {
  if (!initialized)
    return nullptr;
  NwRiotCrowdObject *crowd = new NwRiotCrowdObject;
  crowd->AddRef();
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, 84);
  if (!crowd->Init(key)) {
    crowd->Release();
    return nullptr;
  }
  return static_cast<NwRiotCrowd *>(crowd);
}

// Create and initialize a parade block using the requested vehicle resource.
NwParadeBlock *NwStreetLayer::CreateParadeBlock(u32 resource) {
  if (!initialized)
    return nullptr;
  NwParadeBlockObject *block = new NwParadeBlockObject;
  block->AddRef();
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, resource);
  if (!block->Init(key)) {
    block->Release();
    return nullptr;
  }
  return static_cast<NwParadeBlock *>(block);
}

// Dispatch a disaster aircraft from a sufficiently distant runway or random city cell.
iptr NwStreetLayer::DispatchAirplane(u32 targetX, u32 targetZ) {
  u32 startX = 0, startZ = 0;
  bool found = false;
  if (!airTrafficManagers.empty()) {
    u32 index = random.Bounded(static_cast<u32>(airTrafficManagers.size()));
    auto entry = airTrafficManagers.begin();
    while (index--)
      ++entry;
    (*entry)->GetRunwayEnd(&startX, &startZ);
    found = u32(GetBlockDistance(startX, startZ, targetX, targetZ)) > 12;
    if (!found) {
      for (NwAirTrafficManager *manager : airTrafficManagers) {
        manager->GetRunwayEnd(&startX, &startZ);
        if (u32(GetBlockDistance(startX, startZ, targetX, targetZ)) > 12) {
          found = true;
          break;
        }
      }
    }
  }
  if (!found) {
    do {
      startX = random.Bounded(NwGetStreetCity()->CellCountX());
      startZ = random.Bounded(NwGetStreetCity()->CellCountZ());
    } while (u32(GetBlockDistance(startX, startZ, targetX, targetZ)) < 12);
  }
  NwAeroplane *plane = new NwAeroplane;
  plane->AddRef();
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, 110);
  if (!plane->Init(key)) {
    plane->Release();
    return 0;
  }
  if (!plane->MakeDisasterFightingPlane(i32(startX), i32(startZ), i32(targetX), i32(targetZ))) {
    plane->Shutdown();
    plane->Release();
    return 0;
  }
  return reinterpret_cast<iptr>(plane);
}

void NwStreetLayer::SubwayTileInserted(ScMessage *message) {
  auto *occupant = reinterpret_cast<ScOccupant *>(message->data1);
  if (!agentTypeTree->IsAncestor(kCtAgentSubwayEndcap, occupant->TypeId()))
    return;
  NwSubwayProducer *producer = new NwSubwayProducer;
  producer->AddRef();
  producer->SetVehicleProduceState(subwayActive != 0);
  producer->Init(occupant, subwayAttributes);
  subwayProducers.insert({occupant, producer});
}

void NwStreetLayer::SubwayTileRemoved(ScMessage *message) {
  auto *occupant = reinterpret_cast<ScOccupant *>(message->data1);
  if (!agentTypeTree->IsAncestor(kCtAgentSubwayEndcap, occupant->TypeId()))
    return;
  auto entry = subwayProducers.find(occupant);
  entry->second->Release();
  subwayProducers.erase(entry);
}

// Dispatch street-simulation messages to their original subway, emergency and view handlers.
bool NwStreetLayer::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kOcMsg_SubwayInserted:
    SubwayTileInserted(message);
    break;
  case kDsMsg_CounterStartRequested:
    if (u32(message->data2) == 1)
      BeginEmergencyMode();
    break;
  case kOcMsg_SubwayRemoved:
    SubwayTileRemoved(message);
    break;
  case kDsMsg_CounterEnded:
    EndEmergencyMode();
    break;
  case kWnMsg_ViewInfoUpdated:
    ProcessViewChange(message);
    break;
  default:
    break;
  }
  return true;
}

// Update the visible street-simulation rectangles, zoom and producers from a city-view snapshot.
void NwStreetLayer::ProcessViewChange(ScMessage *message) {
  if (u32(message->data1) != kSpViewUpdate_Snapshot)
    return;
  if (!tickManager) {
    tickManager = ScAddRefNotNull(NwGetStreetTickManager());
    tickManager->AddTickableObject(static_cast<SpTickable *>(this));
    ApPreferences *preferences = NwGetStreetPreferences();
    if (preferences) {
      ApVisibilitySettings *visibility = preferences->VisibilityPrefs();
      i32 trafficZoom = i32(5u - u32(visibility->traffic_visibility));
      NwStreetVehicle::SetBirthZoomLevel(trafficZoom);
      NwPerson::SetBirthZoomLevel(i32(5u - u32(visibility->sims_visibility)));
      NwRailVehicle::SetBirthZoomLevel(trafficZoom);
      NwSubwayVehicle::SetBirthZoomLevel(trafficZoom);
    }
    UpdateNumberOfBoats();
    currentZoom = 5;
  }
  auto *view = reinterpret_cast<SpIsoViewInfo *>(message->data2);
  GeBounds bounds;
  i32 zoom = 0, type = 0;
  view->GetInfo(&bounds);
  view->GetZoom(&zoom);
  view->GetType(&type);
  u32 left = u32(bounds.minimum.x >> 8), top = u32(bounds.minimum.z >> 8);
  u32 right = u32(bounds.maximum.x >> 8), bottom = u32(bounds.maximum.z >> 8);
  u32 centerX = (left + right) >> 1, centerZ = (top + bottom) >> 1;
  i32 dx = i32(centerX - viewCenterX), dz = i32(centerZ - viewCenterZ);
  i64 distance = (dx < 0 ? -i64(dx) : i64(dx)) + (dz < 0 ? -i64(dz) : i64(dz));
  if (zoom == currentZoom && type == currentViewType && distance < 2)
    return;
  currentZoom = zoom;
  currentViewType = type;
  viewRect = {left, top, right, bottom};
  viewCenterX = centerX;
  viewCenterZ = centerZ;
  ComputeSurroundRect(viewRect, &simulationRect, 2);
  peopleOriginX = (simulationRect.left + simulationRect.right) >> 1;
  peopleOriginZ = (simulationRect.top + simulationRect.bottom) >> 1;
  ComputeSurroundStrips(viewRect, 1, maxCellX, maxCellZ, borderRects, &borderCount);
  UpdatePeopleView();
  UpdateVehicleView();
  UpdateTrainsView();
  UpdatePlanesView();
  UpdateSubwayView();
  UpdateHelicopterView();
  UpdateBoatView();
  UpdateShipView();
}

// Initialize the city-specific street occupants and producers in their original order.
u8 NwStreetLayer::CitySpecificSubSystemsInit() {
  NwPerson::CitySpecificStaticInit(city);
  NwStreetVehicle::CitySpecificStaticInit(city);
  NwPeopleProducer::CitySpecificStaticInit(city);
  NwVehicleProducer::CitySpecificStaticInit(city);
  NwTrainProducer::CitySpecificStaticInit(city);
  NwSubwayProducer::CitySpecificStaticInit(city);
  NwAirTrafficManager::CitySpecificStaticInit(city);
  NwHelicopterProducer::CitySpecificStaticInit(city);
  NwHelicopter::CitySpecificStaticInit(city);
  NwShipProducer::CitySpecificStaticInit(city);
  NwShipVehicle::CitySpecificStaticInit(city);
  NwBoatProducer::CitySpecificStaticInit(city);
  NwBoatVehicle::CitySpecificStaticInit(city);
  return NwStrikeCrowdProducer::CitySpecificStaticInit(city);
}

// Release the city-specific street subsystems and their retained city interfaces.
u8 NwStreetLayer::CitySpecificSubSystemsShutdown() {
  NwBoatVehicle::CitySpecificStaticShutdown();
  NwBoatProducer::CitySpecificStaticShutdown();
  NwShipProducer::CitySpecificStaticShutdown();
  NwShipVehicle::CitySpecificStaticShutdown();
  NwHelicopter::CitySpecificStaticShutdown();
  NwHelicopterProducer::CitySpecificStaticShutdown();
  NwPerson::CitySpecificStaticShutdown();
  NwStreetVehicle::CitySpecificStaticShutdown();
  NwPeopleProducer::CitySpecificStaticShutdown();
  NwVehicleProducer::CitySpecificStaticShutdown();
  NwTrainProducer::CitySpecificStaticShutdown();
  NwSubwayProducer::CitySpecificStaticShutdown();
  NwAirTrafficManager::CitySpecificStaticShutdown();
  return NwStrikeCrowdProducer::CitySpecificStaticShutdown();
}
// These independent module generators use the original CRT initialization order and multimedia-time seed.
ScRandom NwAirTrafficManagerRandom(kScRandomSeedFromClock);
ScRandom NwAirTrafficAttribRandom(kScRandomSeedFromClock);
ScRandom NwAnimationRandom(kScRandomSeedFromClock);
ScRandom NwHelicopterProducerRandom(kScRandomSeedFromClock);
ScRandom NwPeopleProducerRandom(kScRandomSeedFromClock);
ScRandom NwPeopleNodeRandom(kScRandomSeedFromClock);
ScRandom NwPeopleGraphRandom(kScRandomSeedFromClock);
ScLock NwStreetLoaderLock;
ScThread NwStreetLoaderThread;

// Start the low-priority vehicle resource loader while holding its shared cache lock.
u8 NwStreetLayer::StartBackgroundResLoader() {
  if (NwStreetLoaderThread.IsRunning())
    return 0;
  NwStreetLoaderLock.Lock();
  NwWriteLoaderFlag(&NwStreetLoaderState, 2);
  NwStreetLoaderThread.Start(BackgroundResLoaderThreadFunction, nullptr);
  NwStreetLoaderThread.SetPriority(2);
  NwStreetLoaderLock.Unlock();
  return 1;
}

// Request the loader to stop, raise its priority to the caller's, and wait for it to exit.
u8 NwStreetLayer::EndBackgroundResLoader() {
  if (!NwStreetLoaderThread.IsRunning())
    return 0;
  NwWriteLoaderFlag(&NwStreetLoaderState, 1);
  NwStreetLoaderThread.SetPriority(ScThread::CurrentPriority());
  NwStreetLoaderThread.Wait();
  NwWriteLoaderFlag(&NwStreetLoaderState, 0);
  return 1;
}

// Load missing key vehicle resources into the shared cache until complete or stopped.
u32 SC_THREAD_CALL NwStreetLayer::BackgroundResLoaderThreadFunction(void *) {
  RsKey key(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, 0);
  for (u32 instance : keyVehicles) {
    if (NwReadLoaderFlag(&NwStreetLoaderState) != 2)
      break;
    NwStreetLoaderLock.Lock();
    if (vehicleAttributes.find(instance) == vehicleAttributes.end()) {
      key.instance = instance;
      RsResource *resource;
      if (NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(&resource)))
        vehicleAttributes[instance] = resource;
    }
    NwStreetLoaderLock.Unlock();
    ScThread::SleepCurrentThread(20000);
  }
  NwWriteLoaderFlag(&NwStreetLoaderState, 0);
  NwWriteLoaderFlag(&backgroundResourceLoadingCompleted, 1);
  return 0;
}

ScRandom NwSubwayProducerRandom(kScRandomSeedFromClock);
ScRandom NwShipProducerRandom(kScRandomSeedFromClock);
ScRandom NwBoatProducerRandom(kScRandomSeedFromClock);
ScRandom NwTrainProducerRandom(kScRandomSeedFromClock);
ScRandom NwVehicleRandom(kScRandomSeedFromClock);
ScRandom NwVehicleRuntimeProducerRandom(kScRandomSeedFromClock);
ScRandom NwVehicleProducerRandom(kScRandomSeedFromClock);
ScRandom NwVehicleNodeRandom(kScRandomSeedFromClock);
ScRandom NwVehicleGraphRandom(kScRandomSeedFromClock);
std::map<u32, RsResource *> NwStreetLayer::animationAttributes, NwStreetLayer::personAttributes;
std::map<u32, RsResource *> NwStreetLayer::peoplePaths, NwStreetLayer::vehiclePaths, NwStreetLayer::vehicleAttributes;
std::map<u32, RsResource *> NwStreetLayer::scripts;
// Windows15C0F and Linux65168 establish the original ordered resource-instance cache.
std::map<u32, NwTrafficAttrib *> NwStreetLayer::trafficAttributes;
// Original Linux vector<unsigned int> symbols identify these independent initialization lists.
std::vector<u32> NwStreetLayer::emergencyVehicles, NwStreetLayer::keyVehicles;

NwStreetManipulator::NwStreetManipulator(NwStreetLayer *value) : references(0), layer(value) {
}

NwStreetManipulator::~NwStreetManipulator() {
}

// Publish the manipulator identity and acquire a reference for the caller.
bool NwStreetManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwStreetManipulator::AddRef() {
  return ++references;
}

u32 NwStreetManipulator::Release() {
  return ScReleaseImpl(this, &references);
}

bool NwStreetManipulator::IsLockingCity() {
  return false;
}

bool NwStreetManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool NwStreetManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool NwStreetManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Retain the city services and create the filters before registering the layer's original message notifications.
// Initialize the city-specific subsystems, global traffic producer, strike producer and boat producer in source order.
bool NwStreetLayer::Init(CtCity *value) {
  if (initialized)
    return true;
  initialized = 1;
  emergencyMode = 0;
  vehicleEmergencyMode = 0;
  city = ScAddRefNotNull(value);
  maxCellX = city->CellCountX() - 1;
  maxCellZ = city->CellCountZ() - 1;
  maxAnimX = city->AnimCellCountX() - 1;
  maxAnimY = city->AnimCellCountY() - 1;
  maxAnimZ = city->AnimCellCountZ() - 1;
  disasterLayer = ScAddRefNotNull(city->DisasterLayer());
  trafficLayer = ScAddRefNotNull(city->TrafficLayer());
  residentialLayer = ScAddRefNotNull(city->ResidentialLayer());
  surfaceOccupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  surfaceOccupantManager->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &surfaceSender, false);
  animationManager = ScAddRefNotNull(city->AnimOccupantManager());
  ApGameInterface *application;
  NwGetStreetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application));
  agentTypeTree = ScAddRefNotNull(application->GetAppAgentTypeTree());
  application->Release();
  automataFilter = ScAddRefNotNull(new NwAutomataFilter);
  roadRectFilter = ScAddRefNotNull(new NwRoadRectFilter);
  peopleRectFilter = ScAddRefNotNull(new NwPeopleRectFilter);
  peopleFilter = ScAddRefNotNull(new NwPeopleFilter);
  vehicleFilter = ScAddRefNotNull(new NwRoadVehicleFilter);
  nonEmergencyVehicleFilter = ScAddRefNotNull(new NwNonEmergencyRoadFilter);
  peopleRoadFilter = ScAddRefNotNull(new NwPeopleRoadFilter);
  railFilter = ScAddRefNotNull(new NwRailVehicleFilter);
  subwayFilter = ScAddRefNotNull(new NwSubwayVehicleFilter);
  manipulator = ScAddRefNotNull(new NwStreetManipulator(this));
  ScMessageServer *messages = NwGetStreetMessageServer();
  messages->AddNotification(this, kWnMsg_ViewInfoUpdated);
  messages->AddNotification(this, kOcMsg_SubwayInserted);
  messages->AddNotification(this, kOcMsg_SubwayRemoved);
  messages->AddNotification(this, kDsMsg_CounterStartRequested);
  messages->AddNotification(this, kDsMsg_CounterEnded);
  CitySpecificSubSystemsInit();
  CreateGlobalVclProducer();
  strikeProducer = ScAddRefNotNull(new NwStrikeCrowdProducer);
  strikeProducer->Init();
  boatProducer = ScAddRefNotNull(new NwBoatProducer);
  boatProducer->Init();
  return true;
}

// Unregister notifications and animation/disaster callbacks before retiring the original producers and occupants.
// Release each retained city interface only after its dependent subsystems have shut down.
bool NwStreetLayer::Shutdown() {
  if (!initialized)
    return true;
  initialized = 0;
  ScMessageServer *messages = NwGetStreetMessageServer();
  messages->RemoveNotification(this, kDsMsg_CounterEnded);
  messages->RemoveNotification(this, kDsMsg_CounterStartRequested);
  messages->RemoveNotification(this, kOcMsg_SubwayInserted);
  messages->RemoveNotification(this, kOcMsg_SubwayRemoved);
  messages->RemoveNotification(this, kWnMsg_ViewInfoUpdated);
  surfaceOccupantManager->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
  // Release the live field. Clear it only after its callback returns.
  auto release = [](auto **slot) {
    if (*slot) {
      (*slot)->Release();
      *slot = nullptr;
    }
  };
  release(&toxicMotion);
  if (tickManager) {
    tickManager->RemoveTickableObject(static_cast<SpTickable *>(this));
    tickManager->Release();
    tickManager = nullptr;
  }
  if (peopleActive) {
    UnregisterDisasterCallback();
    ShutdownPeople();
  }
  if (vehiclesActive) {
    UnregisterDisasterCallback();
    ShutdownVehicles();
  }
  // Preserve the separate shutdown and release callbacks. Each reads the live producer field in original order.
  auto shutdown = [](auto **slot) {
    if (*slot) {
      (*slot)->Shutdown();
      (*slot)->Release();
      *slot = nullptr;
    }
  };
  shutdown(&vehicleProducer);
  release(&manipulator);
  for (auto &entry : shipProducers)
    entry.second->Release();
  shipProducers.clear();
  for (NwAirTrafficManager *manager : airTrafficManagers)
    manager->Release();
  airTrafficManagers.clear();
  for (auto &entry : subwayProducers)
    entry.second->Release();
  subwayProducers.clear();
  for (auto &entry : trainProducers)
    entry.second->Release();
  trainProducers.clear();
  shutdown(&boatProducer);
  shutdown(&helicopterProducer);
  shutdown(&strikeProducer);
  animationManager->RemoveOccupants(nullptr, automataFilter);
  CitySpecificSubSystemsShutdown();
  release(&surfaceOccupantManager);
  release(&animationManager);
  release(&subwayFilter);
  release(&railFilter);
  release(&peopleRoadFilter);
  release(&peopleFilter);
  release(&nonEmergencyVehicleFilter);
  release(&vehicleFilter);
  release(&peopleRectFilter);
  release(&roadRectFilter);
  release(&automataFilter);
  release(&residentialLayer);
  release(&trafficLayer);
  release(&disasterLayer);
  release(&city);
  release(&agentTypeTree);
  return true;
}

bool NwStreetLayer::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool NwStreetLayer::DebugClassTag(ScStringView *) {
  return false;
}

bool NwStreetLayer::DebugTypeTag(ScStringView *) {
  return false;
}

bool NwStreetLayer::Init(CtCity *, ScImporter *) {
  return true;
}

bool NwStreetLayer::Init(CtCity *, RsDatabaseSegment *) {
  return true;
}

bool NwStreetLayer::Save(CtCity *, RsDatabaseSegment *) {
  return true;
}

bool NwStreetLayer::SimulationEnd() {
  return true;
}

bool NwStreetLayer::InitLayer() {
  return true;
}

// Change the people birth zoom and refresh their view when the allowed range changes.
bool NwStreetLayer::SetNumZoomLevelsPeopleAreVisible(i32 levels) {
  if (static_cast<u32>(levels) > 2)
    return false;
  i32 zoom = 5 - levels;
  if (zoom != NwPerson::BirthZoomLevel()) {
    NwPerson::SetBirthZoomLevel(zoom);
    UpdatePeopleView();
  }
  return true;
}

// Change the vehicle birth zooms and refresh each traffic view when the range changes.
bool NwStreetLayer::SetNumZoomLevelsTrafficVisible(i32 levels) {
  if (static_cast<u32>(levels) >= 4)
    return false;
  i32 zoom = 5 - levels;
  if (zoom != NwStreetVehicle::BirthZoomLevel()) {
    NwStreetVehicle::SetBirthZoomLevel(zoom);
    NwRailVehicle::SetBirthZoomLevel(zoom);
    NwSubwayVehicle::SetBirthZoomLevel(zoom);
    UpdateVehicleView();
    UpdateTrainsView();
    UpdateSubwayView();
  }
  return true;
}

void NwStreetLayer::UpdateBoatView() {
  NwBoatVehicle::SetCurrentViewType(currentViewType);
}

void NwStreetLayer::UpdateShipView() {
  NwShipVehicle::SetCurrentViewType(currentViewType);
}

// Set the target boat population from the city's residential population, capped at fifteen.
void NwStreetLayer::UpdateNumberOfBoats() {
  if (!boatProducer)
    return;
  u32 population = u32(residentialLayer->GetPopulation());
  u32 count = (population >= 1000 ? 1u : 0u) + (population >= 4000 ? 1u : 0u);
  if (population > 4000)
    count += (population - 4000) / 10000;
  if (count > 15)
    count = 15;
  boatProducer->SetNumberOfBoatsToProduce(count);
}

void NwStreetLayer::UpdateNumberOfHelicopters() {
  if (!helicopterProducer)
    return;
  u32 population = u32(residentialLayer->GetPopulation());
  u32 count = (population >= 3000 ? 1u : 0u) + (population >= 20000 ? 1u : 0u);
  if (population > 20000)
    count += (population - 20000) / 30000;
  if (count > 5)
    count = 5;
  helicopterProducer->targetRoutineCount = count;
}

void NwStreetLayer::UpdateHelicopterView() {
  if (!helicopterProducer) {
    helicopterProducer = ScAddRefNotNull(new NwHelicopterProducer);
    helicopterProducer->Init(helicopterAttributes);
    UpdateNumberOfHelicopters();
  }
  NwHelicopter::SetCurrentViewType(currentViewType);
}

void NwStreetLayer::DisasterChangeCallback(DsInstance *instance, i32 change, void *context) {
  auto *layer = static_cast<NwStreetLayer *>(context);
  if (change == 1 || change == 3)
    layer->DisasterAdd(instance);
  else if (change == 2 || change == 4)
    layer->DisasterRemove(instance);
}

// Retain one user of the disaster callback, registering it only for the first user.
void NwStreetLayer::RegisterDisasterCallback() {
  if (disasterCallbackUsers++)
    return;
  disasterLayer->NotifyOnChange(&NwStreetLayer::DisasterChangeCallback, this);
}

void NwStreetLayer::UnregisterDisasterCallback() {
  if (disasterCallbackUsers-- == 1)
    disasterLayer->StopNotifyingOnChange(&NwStreetLayer::DisasterChangeCallback, this);
}

// Apply tornado motion and add a disaster's panic influence to the active people safety grid.
void NwStreetLayer::DisasterAdd(DsInstance *instance) {
  u32 type = instance->TypeId();
  if (type == kDsTornadoTypeId) {
    DsTornadoMotion *motion = nullptr;
    instance->QueryInterface(IID_DsTornadoMotion, reinterpret_cast<void **>(&motion));
    if (motion) {
      i32 pointX = 0, pointY = 0, pointZ = 0;
      if (motion->GetPosition(&pointX, &pointY, &pointZ)) {
        ScVec3F origin{static_cast<f32>(pointX), static_cast<f32>(pointY), static_cast<f32>(pointZ)}, velocity{};
        motion->GetVelocity(&velocity);
        i32 left = i32(u32(pointX) - 200), top = i32(u32(pointZ) - 200), low = i32(u32(pointY) - 100);
        i32 right = i32(u32(pointX) + 200), bottom = i32(u32(pointZ) + 200), high = i32(u32(pointY) + 100);
        if (left < 0)
          left = 0;
        if (top < 0)
          top = 0;
        if (low < 0)
          low = 0;
        if (right > maxAnimX)
          right = maxAnimX;
        if (bottom > maxAnimZ)
          bottom = maxAnimZ;
        if (high > maxAnimY)
          high = maxAnimY;
        GeBounds bounds(CtCoord{left, top, low}, CtCoord{right, bottom, high});
        ScOccupant *occupants[8]{};
        u32 count = 8;
        animationManager->GetOccupantsInVolume(bounds, occupants, &count);
        for (u32 i = 0; i < count && i < 8; ++i) {
          ScOccupant *occupant = occupants[i];
          if (!occupant)
            continue;
          if (occupant->MajorType() == 6) {
            if (occupant->SubTypeInfo() == 0) {
              auto *person = static_cast<NwPerson *>(occupant);
              if (!(person->personState & 2))
                person->SpiralMotion(origin, velocity);
            } else if (occupant->SubTypeInfo() == 1) {
              auto *vehicle = static_cast<NwStreetVehicle *>(occupant);
              if (!(vehicle->vehicleState & 1) && vehicle->Length() <= 100)
                vehicle->SpiralMotion(origin, velocity);
            }
          }
          occupant->Release();
        }
      }
      motion->Release();
    }
  } else if (type == kDsToxicTypeId && !toxicMotion) {
    instance->QueryInterface(IID_DsToxicMotion, reinterpret_cast<void **>(&toxicMotion));
  }
  if (!peopleActive)
    return;
  GeCellRect bounds{}, intersection;
  if (!instance->GetBounds(&bounds) || !RectIntersect(simulationRect, bounds, &intersection))
    return;
  ++disasterCount;
  GetSimRectIndices(&intersection);
  if (intersection.right > 70)
    intersection.right = 69;
  if (intersection.bottom > 70)
    intersection.bottom = 69;
  u8 effect = static_cast<u8>(instance->PanicEffect(0));
  for (u32 z = intersection.top; z <= intersection.bottom && z < 70; ++z) {
    for (u32 x = intersection.left; x <= intersection.right && x < 70; ++x) {
      u32 index = 70 * z + x;
      safetyValues[index] = i16(u16(safetyValues[index]) + effect);
    }
  }
  u32 radius = instance->PanicRadius();
  for (u32 distance = 1; distance <= radius; ++distance) {
    effect = static_cast<u8>(instance->PanicEffect(distance));
    GeCellRect strips[4];
    u32 count;
    ComputeSurroundStrips(intersection, distance, 69, 69, strips, &count);
    for (u32 i = 0; i < count; ++i) {
      for (u32 z = strips[i].top; z <= strips[i].bottom && z < 70; ++z) {
        for (u32 x = strips[i].left; x <= strips[i].right && x < 70; ++x) {
          u32 index = 70 * z + x;
          safetyValues[index] = i16(u16(safetyValues[index]) + effect);
        }
      }
    }
  }
}

// Remove a disaster's toxic-cloud hold and subtract its panic influence from the people safety grid.
void NwStreetLayer::DisasterRemove(DsInstance *instance) {
  if (instance->TypeId() == kDsToxicTypeId && toxicMotion) {
    toxicMotion->Release();
    toxicMotion = nullptr;
  }
  if (!peopleActive)
    return;
  GeCellRect bounds{}, intersection;
  instance->GetBounds(&bounds);
  if (!RectIntersect(simulationRect, bounds, &intersection))
    return;
  if (disasterCount > 0)
    --disasterCount;
  else
    disasterCount = 0;
  GetSimRectIndices(&intersection);
  i8 effect = instance->PanicEffect(0);
  for (u32 z = intersection.top; z <= intersection.bottom && z < 70; ++z) {
    for (u32 x = intersection.left; x <= intersection.right && x < 70; ++x) {
      u32 index = 70 * z + x;
      safetyValues[index] = i16(u16(safetyValues[index]) - u16(i16(effect)));
    }
  }
  u32 radius = instance->PanicRadius();
  for (u32 distance = 1; distance <= radius; ++distance) {
    effect = instance->PanicEffect(distance);
    GeCellRect strips[4];
    u32 count;
    ComputeSurroundStrips(intersection, distance, 69, 69, strips, &count);
    for (u32 i = 0; i < count; ++i) {
      for (u32 z = strips[i].top; z <= strips[i].bottom && z < 70; ++z) {
        for (u32 x = strips[i].left; x <= strips[i].right && x < 70; ++x) {
          u32 index = 70 * z + x;
          safetyValues[index] = i16(u16(safetyValues[index]) - u16(i16(effect)));
        }
      }
    }
  }
}

// Create the retained global people producer and initialize it at the layer's current origin.
// Static resource initialization must supply globalPeopleAttributes before this method is called.
u8 NwStreetLayer::CreateGlobalPplProducer() {
  peopleProducer = ScAddRefNotNull(new NwPeopleProducer);
  NwPeopleProducerAttribInterface *attributes = globalPeopleAttributes ?
      static_cast<NwPeopleProducerAttribInterface *>(globalPeopleAttributes) : nullptr;
  return peopleProducer->Init(attributes, peopleOriginX, peopleOriginZ);
}

// Create the retained global vehicle producer and initialize its per-grid attributes.
// Static resource initialization must supply globalVehicleAttributes before this method is called.
u8 NwStreetLayer::CreateGlobalVclProducer() {
  vehicleProducer = ScAddRefNotNull(new NwVehicleProducer);
  NwVehicleProducerAttribInterface *attributes = globalVehicleAttributes ?
      static_cast<NwVehicleProducerAttribInterface *>(globalVehicleAttributes) : nullptr;
  return vehicleProducer->Init(attributes);
}

void NwStreetLayer::UpdatePlanesView() {
  NwAeroplane::SetCurrentViewType(currentViewType);
}

// Dispatch a helicopter for distinct start and target cells when the producer exists.
// The shared interface's pointer-sized return spelling remains provisional.
iptr NwStreetLayer::DispatchHelicopter(u32 startX, u32 startZ, u32 targetX, u32 targetZ) {
  if (!helicopterProducer || (startX == targetX && startZ == targetZ))
    return 0;
  return reinterpret_cast<iptr>(helicopterProducer->DispatchHelicopter(startX, startZ, targetX, targetZ));
}

bool NwStreetLayer::RedirectAirplane(ScOccupant *plane, u32 x, u32 z) {
  return static_cast<NwAeroplane *>(plane)->Redirect(x, z) != 0;
}

void NwStreetLayer::RevokeDispatchedAirplane(ScOccupant *plane) {
  static_cast<NwAeroplane *>(plane)->EndDispatchMission();
}

void NwStreetLayer::RevokeDispatchedHelicopter(ScOccupant *helicopter) {
  static_cast<NwHelicopter *>(helicopter)->EndDispatchMission();
}

// Select a random emergency-vehicle resource instance, or report an empty collection.
u8 NwStreetLayer::GetAnEmergencyVehicleResInstId(u32 *out) {
  u32 count = static_cast<u32>(emergencyVehicles.size());
  if (!count) {
    *out = 0;
    return 0;
  }
  *out = emergencyVehicles[random.Bounded(count)];
  return 1;
}

// Return the layer's primary interface for its original supported identities and retain the receiver.
bool NwStreetLayer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtStartSimLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtStartSimLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtStartSimLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtStartSimLayer);
  HANDLE_QUERY_INTERFACE(IID_CtStartSimLayer, CtStartSimLayer);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwStreetLayer::AddRef() {
  return ++references;
}

// Borrow the resource manager retained by the street-simulation layer's static initialization.
// Linux identifies the original member; its distinct Windows entry remains unresolved.
RsManager *NwStreetLayer::GetResourceManager() {
  return NwStreetResourceManager;
}

u32 NwStreetLayer::GetLayerType() {
  return kNwStreetLayerType;
}

void NwStreetLayer::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 60;
  *interval = 4;
}

bool NwStreetLayer::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return false;
}

bool NwStreetLayer::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return false;
}

bool NwStreetLayer::CellChangedAll(u32) {
  return true;
}

bool NwStreetLayer::CellChanged(u32, u32, u32) {
  return true;
}

bool NwStreetLayer::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}

// Read a carriage-return-terminated text line, ignoring line feeds and preserving a failed read's partial string.
u8 NwStreetLayer::GetTextLine(RsStream *stream, ScString *line) {
  line->Erase(0, line->Strlen());
  char byte;
  while (stream->ReadBlock(&byte, 1)) {
    if (byte == '\n')
      continue;
    if (byte == '\r') {
      byte = 0;
      line->Append(&byte, 1);
      return 1;
    }
    line->Append(&byte, 1);
  }
  return 0;
}

void SC3_CDECL NwStreetLayer::AddEmergencyVehicleResInstId(const ScString &, const ScString &value, ApIniContext *) {
  emergencyVehicles.push_back(ScConvertToUint32(value.ToChar()));
}

void SC3_CDECL NwStreetLayer::AddKeyVehicleResInstId(const ScString &, const ScString &value, ApIniContext *) {
  keyVehicles.push_back(ScConvertToUint32(value.ToChar()));
}

// Return the original Manhattan distance between two signed cell positions using wrapped x86 arithmetic.
i32 NwStreetLayer::GetBlockDistance(i32 x0, i32 z0, i32 x1, i32 z1) {
  u32 dx = u32(x1) - u32(x0), dz = u32(z1) - u32(z0);
  if (i32(dx) < 0)
    dx = 0u - dx;
  if (i32(dz) < 0)
    dz = 0u - dz;
  return i32(dx + dz);
}

// The Linux member returns a borrowed pointer to the current street simulation rectangle.
GeCellRect *NwStreetLayer::GetStrtSimRect() {
  return &simulationRect;
}

u8 NwStreetLayer::CellIsInPeopleSimRect(u32 x, u32 z) {
  return x >= peopleSimRect.left && x <= peopleSimRect.right && z >= peopleSimRect.top && z <= peopleSimRect.bottom;
}

u8 NwStreetLayer::CellIsInVehicleSimRect(u32 x, u32 z) {
  return x >= vehicleSimRect.left && x <= vehicleSimRect.right && z >= vehicleSimRect.top && z <= vehicleSimRect.bottom;
}

bool NwStreetLayer::CellIsInBorderOfPeopleSimRect(u32 x, u32 z) {
  return x == peopleSimRect.left || x == peopleSimRect.right || z == peopleSimRect.top || z == peopleSimRect.bottom;
}

bool NwStreetLayer::IsPeopleSimRectCellUnsafe(u32 x, u32 z) {
  u32 localX = x - simulationRect.left, localZ = z - simulationRect.top;
  return localX >= 70 || localZ >= 70 || static_cast<u16>(safetyValues[70 * localZ + localX]) >= 40;
}

bool NwStreetLayer::IsPeopleSimRectCellClearlySafe(u32 x, u32 z) {
  u32 localX = x - simulationRect.left, localZ = z - simulationRect.top;
  return localX < 70 && localZ < 70 && static_cast<u16>(safetyValues[70 * localZ + localX]) < 10;
}

// Return the signed safety value at grid-relative coordinates, or zero outside the grid.
i32 NwStreetLayer::SafetyValueInPeopleSimRect(i32 x, i32 z) {
  if (u32(x) >= 70 || u32(z) >= 70)
    return 0;
  return safetyValues[70 * u32(z) + u32(x)];
}

bool NwStreetLayer::DisastersPresent() {
  return disasterCount > 0;
}

u8 NwStreetLayer::ToxicCloudPresent(u32 x, u32 z) const {
  return toxicMotion ? static_cast<u8>(toxicMotion->Contains(x, z)) : 0;
}

// Convert a cell rectangle to coordinates relative to the street simulation rectangle.
void NwStreetLayer::GetSimRectIndices(GeCellRect *rect) {
  rect->left -= simulationRect.left;
  rect->right -= simulationRect.left;
  rect->top -= simulationRect.top;
  rect->bottom -= simulationRect.top;
}

bool NwStreetLayer::InCityBounds(u32 x, u32 z) {
  return x <= maxCellX && z <= maxCellZ;
}

// Expand the requested rectangle by a cell distance and clip its edges to the city bounds.
void NwStreetLayer::ComputeSurroundRect(const GeCellRect &rect, GeCellRect *out, u32 distance) {
  out->right = rect.right + distance;
  if (out->right > maxCellX)
    out->right = maxCellX;
  out->bottom = rect.bottom + distance;
  if (out->bottom > maxCellZ)
    out->bottom = maxCellZ;
  out->left = rect.left <= distance ? 0 : rect.left - distance;
  out->top = rect.top <= distance ? 0 : rect.top - distance;
}

// Build an eighteen-cell-radius rectangle around a point, clipped to the city edges.
void NwStreetLayer::MakeRectAroundPoint(u32 x, u32 z, GeCellRect *out) {
  out->left = x > 18 ? x - 18 : 0;
  out->right = x + 18;
  if (out->right > maxCellX)
    out->right = maxCellX;
  out->top = z > 18 ? z - 18 : 0;
  out->bottom = z + 18;
  if (out->bottom > maxCellZ)
    out->bottom = maxCellZ;
}

// Return borrowed pointers to the layer's current border rectangles and their count.
void NwStreetLayer::GetBorderRects(GeCellRect **out, u32 *count) {
  *count = borderCount;
  for (u32 i = 0; i < borderCount; ++i)
    out[i] = &borderRects[i];
}

// Enter emergency mode once, counting the emergency and marking every safety-grid cell dangerous.
void NwStreetLayer::BeginEmergencyMode() {
  if (!emergencyMode) {
    emergencyTimer = 0;
    ++disasterCount;
    emergencyMode = 1;
    for (i16 &value : safetyValues)
      value = 150;
  }
}

// Leave emergency mode once and refresh the people and vehicle views.
void NwStreetLayer::EndEmergencyMode() {
  if (emergencyMode) {
    emergencyMode = 0;
    if (disasterCount <= 0)
      disasterCount = 0;
    else
      --disasterCount;
    UpdatePeopleView();
    UpdateVehicleView();
  }
}

u32 NwStreetLayer::IdenticalRects(const GeCellRect &first, const GeCellRect &second) {
  return second.left == first.left && second.right == first.right && second.top == first.top &&
      second.bottom == first.bottom;
}

u8 NwStreetLayer::Contains(const GeCellRect &outer, const GeCellRect &inner) {
  return inner.left >= outer.left && inner.right <= outer.right && inner.top >= outer.top &&
      inner.bottom <= outer.bottom;
}

// Write the inclusive horizontal intersection first, then the vertical intersection only if horizontal bounds overlap.
u8 NwStreetLayer::RectIntersect(const GeCellRect &first, const GeCellRect &second, GeCellRect *out) {
  out->left = first.left > second.left ? first.left : second.left;
  out->right = first.right < second.right ? first.right : second.right;
  if (out->left > out->right)
    return 0;
  out->top = first.top > second.top ? first.top : second.top;
  out->bottom = first.bottom < second.bottom ? first.bottom : second.bottom;
  return out->top <= out->bottom;
}

// Subtract the second inclusive cell rectangle from the first, emitting up to four strips in original order.
void NwStreetLayer::SubtractRect(const GeCellRect &first, const GeCellRect &second, GeCellRect *out, i32 *count) {
  *count = 0;
  u32 left = first.left;
  if (first.left > second.right || first.right < second.left || first.top > second.bottom || first.bottom < second.top) {
    out[0].left = left;
    out[0].right = first.right;
    out[0].top = first.top;
    out[0].bottom = first.bottom;
    *count = 1;
    return;
  }
  if (left < second.left) {
    out[*count].left = left;
    out[*count].right = second.left - 1;
    out[*count].top = first.top;
    out[*count].bottom = first.bottom;
    left = second.left;
    ++*count;
  }
  u32 right = first.right, limit = second.right;
  if (right > limit) {
    out[*count].left = limit + 1;
    out[*count].right = first.right;
    out[*count].top = first.top;
    out[*count].bottom = first.bottom;
    right = second.right;
    ++*count;
  }
  if (right >= left) {
    u32 top = first.top;
    if (top < second.top) {
      out[*count].top = top;
      out[*count].bottom = second.top - 1;
      out[*count].left = left;
      out[*count].right = right;
      ++*count;
    }
    u32 bottom = second.bottom;
    if (first.bottom > bottom) {
      out[*count].top = bottom + 1;
      out[*count].bottom = first.bottom;
      out[*count].left = left;
      out[*count].right = right;
      ++*count;
    }
  }
}

// Emit the unclipped sides around an inclusive rectangle in right, left, bottom and top order.
void NwStreetLayer::ComputeSurroundStrips(
    const GeCellRect &rect, u32 distance, u32 maxX, u32 maxZ, GeCellRect *out, u32 *count) {
  *count = 0;
  u32 right = rect.right + distance, bottom = rect.bottom + distance;
  bool hasRight = right <= maxX, hasBottom = bottom <= maxZ;
  if (!hasRight)
    right = maxX;
  if (!hasBottom)
    bottom = maxZ;
  bool hasLeft = rect.left >= distance, hasTop = rect.top >= distance;
  u32 left = hasLeft ? rect.left - distance : 0, top = hasTop ? rect.top - distance : 0;
  if (hasRight) {
    out[*count].left = right;
    out[*count].right = right;
    out[*count].top = top;
    out[*count].bottom = hasBottom ? bottom - 1 : bottom;
    ++*count;
  }
  if (hasLeft) {
    out[*count].right = left;
    out[*count].left = left;
    out[*count].top = hasTop ? top + 1 : top;
    out[*count].bottom = bottom;
    ++*count;
  }
  if (hasBottom) {
    out[*count].top = bottom;
    out[*count].bottom = bottom;
    out[*count].left = hasLeft ? left + 1 : left;
    out[*count].right = right;
    ++*count;
  }
  if (hasTop) {
    out[*count].bottom = top;
    out[*count].top = top;
    out[*count].left = left;
    out[*count].right = hasRight ? right - 1 : right;
    ++*count;
  }
}

u8 NwStreetLayer::GetPortAttrib(ScOccupant *occupant, GePortType **out) {
  return occupant->GetAttribData()->QueryInterface(IID_GePortType, reinterpret_cast<void **>(out));
}

// Query and release the port attribute, returning whether it identifies an airport runway.
u8 NwStreetLayer::IsRunway(ScOccupant *occupant) {
  GePortType *attributes;
  if (!GetPortAttrib(occupant, &attributes))
    return 0;
  u8 result = attributes->GetPortType() == 1;
  attributes->Release();
  return result;
}

// Query and release the port attribute, returning whether it identifies an airport taxiway.
u8 NwStreetLayer::IsTaxiway(ScOccupant *occupant) {
  GePortType *attributes;
  if (!GetPortAttrib(occupant, &attributes))
    return 0;
  u8 result = attributes->GetPortType() == 2;
  attributes->Release();
  return result;
}

// Load or find the animation resource by instance, then retain a separate reference for the caller.
u8 NwStreetLayer::GetAnimSpriteAttrib(const RsKey &key, RsResource **out) {
  auto found = animationAttributes.find(key.instance);
  if (found == animationAttributes.end()) {
    if (!NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(out)))
      return 0;
    animationAttributes[key.instance] = *out;
  } else {
    *out = found->second;
  }
  (*out)->AddRef();
  return 1;
}

// Load or find a person attribute resource by instance and retain it for the caller.
u8 NwStreetLayer::GetPersonAttrib(const RsKey &key, RsResource **out) {
  auto found = personAttributes.find(key.instance);
  if (found == personAttributes.end()) {
    if (!NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(out)))
      return 0;
    personAttributes[key.instance] = *out;
  } else {
    *out = found->second;
  }
  (*out)->AddRef();
  return 1;
}

u8 NwStreetLayer::GetPplPathAttrib(const RsKey &key, RsResource **out) {
  auto found = peoplePaths.find(key.instance);
  if (found == peoplePaths.end()) {
    if (!NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(out)))
      return 0;
    peoplePaths[key.instance] = *out;
  } else {
    *out = found->second;
  }
  (*out)->AddRef();
  return 1;
}

u8 NwStreetLayer::GetVclPathAttrib(const RsKey &key, RsResource **out) {
  auto found = vehiclePaths.find(key.instance);
  if (found == vehiclePaths.end()) {
    if (!NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(out)))
      return 0;
    vehiclePaths[key.instance] = *out;
  } else {
    *out = found->second;
  }
  (*out)->AddRef();
  return 1;
}

// Load or find a vehicle attribute resource using the conditional loader lock and retain it for the caller.
u8 NwStreetLayer::GetVehicleAttrib(const RsKey &key, RsResource **out) {
  if (NwReadLoaderFlag(&NwStreetLoaderState))
    NwStreetLoaderLock.ScLock::Lock();
  auto found = vehicleAttributes.find(key.instance);
  if (found == vehicleAttributes.end()) {
    if (!NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(out)))
      return 0;
    vehicleAttributes[key.instance] = *out;
  } else {
    *out = found->second;
  }
  (*out)->AddRef();
  if (NwReadLoaderFlag(&NwStreetLoaderState))
    NwStreetLoaderLock.ScLock::Unlock();
  return 1;
}

// Load or find a script resource and retain a separate reference for the caller.
u8 NwStreetLayer::GetScript(const RsKey &key, RsResource **out) {
  auto found = scripts.find(key.instance);
  if (found == scripts.end()) {
    if (!NwStreetResourceManager->GetResource(key, IID_RsResource, reinterpret_cast<void **>(out)))
      return 0;
    scripts[key.instance] = *out;
  } else {
    *out = found->second;
  }
  (*out)->AddRef();
  return 1;
}

u8 NwTrafficBlockObject::EnterPerson(NwPeoplePath *path, NwPeopleInterface *) {
  NwPeopleGraphEdge *edge = path->GetEdge();
  i8 index = edge->pathIndex;
  for (i8 crossing : edge->vehicleCrossings)
    if (lastVehicles[crossing])
      return 0;
  peopleCounts[index] = i8(u8(peopleCounts[index]) + 1);
  return 1;
}

void NwTrafficBlockObject::PersonLeftPath(i8 path, NwPeopleInterface *) {
  peopleCounts[path] = i8(u8(peopleCounts[path]) - 1);
}

// Borrow the block's traffic attributes without changing their reference count.
// Linux cSSStrtSimTrBlock::GetStrtSimAttrib establishes the operation; a distinct Windows entry remains unresolved.
NwTrafficAttrib *NwTrafficBlockObject::GetStrtSimAttrib() {
  return attributes;
}

u32 NwTrafficBlockObject::IsBlockedPath(i8 path) {
  return (u32(blockedPaths) >> (u8(path) & 31)) & 1;
}

NwPeoplePathAttrib::NwPeoplePathAttrib() : loaded(0), pointCount(0), x(nullptr), z(nullptr) {
}

NwPeoplePathAttrib::~NwPeoplePathAttrib() {
  if (loaded)
    Shutdown();
}

// Release loaded coordinate arrays in original order and clear the loaded flag.
u8 NwPeoplePathAttrib::Shutdown() {
  if (loaded) {
    if (x) {
      delete[] x;
      x = nullptr;
    }
    if (altitude) {
      delete[] altitude;
      altitude = nullptr;
    }
    if (z) {
      delete[] z;
      z = nullptr;
    }
    loaded = 0;
  }
  return 1;
}

// Read a people path's text coordinates and orientations, or dispatch a recognized binary marker.
u8 NwPeoplePathAttrib::SerialRead(RsStream *stream) {
  ScString line;
  if (NwStreetLayer::GetTextLine(stream, &line)) {
    if (line.BeginsWith("BIN", 3))
      return SerialReadBinary(stream);
    std::sscanf(line.ToChar(), "%d", &pointCount);
  }
  x = new i32[pointCount]();
  altitude = new i32[pointCount]();
  z = new i32[pointCount]();
  for (i32 i = 0; i < pointCount; ++i) {
    NwStreetLayer::GetTextLine(stream, &line);
    std::sscanf(line.ToChar(), "%d %d %d", &x[i], &altitude[i], &z[i]);
  }
  NwStreetLayer::GetTextLine(stream, &line);
  i32 orientation = 0;
  std::sscanf(line.ToChar(), "%d", &orientation);
  beginOrientation = u8(u32(orientation) >> 2);
  u8 result = NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%d", &orientation);
  loaded = 1;
  endOrientation = u8(u32(orientation) >> 2);
  return result;
}

// Read the binary people-path arrays and orientations in stream order, retaining each original short-read decision.
u8 NwPeoplePathAttrib::SerialReadBinary(RsStream *stream) {
  u8 result = stream->ReadI32(&pointCount);
  x = new i32[pointCount];
  altitude = new i32[pointCount];
  z = new i32[pointCount];
  for (i32 i = 0; i < pointCount; ++i) {
    if (result) {
      result = stream->ReadI32(&x[i]);
      if (result) {
        result = stream->ReadI32(&altitude[i]);
        if (result)
          result = stream->ReadI32(&z[i]);
      }
    }
  }
  if (result) {
    u32 orientation;
    result = stream->ReadU32(&orientation);
    if (result) {
      beginOrientation = u8(orientation >> 2);
      result = stream->ReadU32(&orientation);
      if (result)
        endOrientation = u8(orientation >> 2);
    }
  }
  loaded = result;
  return result;
}

// Write the binary people-path marker, coordinates and scaled orientation values, stopping stream writes on failure.
u8 NwPeoplePathAttrib::SerialWrite(RsStream *stream) {
  u8 result = stream->WriteU8('B');
  result = result && stream->WriteU8('I') && stream->WriteU8('N') && stream->WriteU8('\r') &&
      stream->WriteI32(pointCount);
  for (i32 i = 0; i < pointCount; ++i) {
    if (result) {
      result = stream->WriteI32(x[i]);
      if (result) {
        result = stream->WriteI32(altitude[i]);
        if (result)
          result = stream->WriteI32(z[i]);
      }
    }
  }
  if (result) {
    result = stream->WriteU32(u32(beginOrientation) * 4);
    if (result)
      result = stream->WriteU32(u32(endOrientation) * 4);
  }
  return result;
}

u8 NwPeoplePathAttrib::GetBeginOrientation() {
  return beginOrientation;
}

u8 NwPeoplePathAttrib::GetEndOrientation() {
  return endOrientation;
}

NwVehiclePathAttrib::NwVehiclePathAttrib()
    : loaded(0), pointCount(0), x(nullptr), altitude(nullptr), z(nullptr), distances(nullptr),
      beginOrientation(0), endOrientation(0), placingOrientation(0), pitch(0), duration(0) {
}

NwVehiclePathAttrib::~NwVehiclePathAttrib() {
  if (loaded)
    Shutdown();
}

u8 NwVehiclePathAttrib::GetBeginOrientation() {
  return beginOrientation;
}

u8 NwVehiclePathAttrib::GetEndOrientation() {
  return endOrientation;
}

// Release the loaded coordinate and distance arrays in original order, clearing each pointer after deletion.
u8 NwVehiclePathAttrib::Shutdown() {
  if (loaded) {
    delete[] x;
    x = nullptr;
    delete[] altitude;
    altitude = nullptr;
    delete[] z;
    z = nullptr;
    delete[] distances;
    distances = nullptr;
    loaded = 0;
  }
  return 1;
}

// Recompute the path's cumulative distances and pitch from its loaded coordinates.
static void NwComputeVehiclePathGeometry(NwVehiclePathAttrib *path) {
  i32 count = path->pointCount;
  path->distances[count - 1] = 0;
  for (i32 i = count - 2; i >= 0; --i) {
    i32 dx = i32(u32(path->x[i]) - u32(path->x[i + 1]));
    i32 dy = i32(u32(path->altitude[i]) - u32(path->altitude[i + 1]));
    i32 dz = i32(u32(path->z[i]) - u32(path->z[i + 1]));
    f64 vertical = f64(dy) * f64(0.204125f);
    i64 segment = static_cast<i64>(std::sqrt(f64(dz) * dz + vertical * vertical + f64(dx) * dx));
    path->distances[i] = i32(u32(path->distances[i + 1]) + u32(segment));
  }
  i32 dx = i32(u32(path->x[count - 1]) - u32(path->x[0]));
  i32 dy = i32(u32(path->altitude[count - 1]) - u32(path->altitude[0]));
  i32 dz = i32(u32(path->z[count - 1]) - u32(path->z[0]));
  f32 vertical = static_cast<f32>(dy);
  path->pitch = static_cast<f32>(f64(vertical) / std::sqrt(f64(dz) * dz + f64(dx) * dx));
}

// Select the initial path heading from its first two horizontal coordinates.
// The two original readers inline this calculation after their final stream reads.
static void NwComputeVehiclePathOrientation(NwVehiclePathAttrib *path) {
  f32 firstZ = static_cast<f32>(i32(u32(path->z[1]) - u32(path->z[0])));
  i32 firstX = i32(u32(path->x[1]) - u32(path->x[0]));
  i64 heading = static_cast<i64>((std::atan2(f64(firstZ), f64(firstX)) + f64(3.1415927f)) /
      f64(0.19634955f) + 0.5);
  path->placingOrientation = static_cast<u8>(heading);
  if (path->placingOrientation >= 32)
    path->placingOrientation = 0;
}

// Read a text vehicle path or dispatch its binary form, then derive its geometry and placement direction.
// Counts below one lead to undefined array access in the original, so they are rejected at this input boundary.
u8 NwVehiclePathAttrib::SerialRead(RsStream *stream) {
  ScString line;
  NwStreetLayer::GetTextLine(stream, &line);
  if (line.BeginsWith("BIN", 3))
    return SerialReadBinary(stream);
  std::sscanf(line.ToChar(), "%d", &pointCount);
  if (pointCount < 1)
    return 0;
  x = new i32[pointCount]();
  altitude = new i32[pointCount]();
  z = new i32[pointCount]();
  distances = new i32[pointCount];
  for (i32 i = 0; i < pointCount; ++i) {
    NwStreetLayer::GetTextLine(stream, &line);
    std::sscanf(line.ToChar(), "%d %d %d", &x[i], &altitude[i], &z[i]);
  }
  NwComputeVehiclePathGeometry(this);
  i32 orientation = 0;
  NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%d", &orientation);
  beginOrientation = static_cast<u8>(orientation);
  NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%d", &orientation);
  endOrientation = static_cast<u8>(orientation);
  NwStreetLayer::GetTextLine(stream, &line);
  NwStreetLayer::GetTextLine(stream, &line);
  std::sscanf(line.ToChar(), "%d", &duration);
  duration = i32(u32(duration) + 1);
  if (pointCount >= 2)
    NwComputeVehiclePathOrientation(this);
  loaded = 1;
  return 1;
}

// Read the binary vehicle-path coordinates and orientation bytes, then derive its geometry and placement direction.
// Counts below two lead to undefined array access in the original and are rejected at this input boundary.
u8 NwVehiclePathAttrib::SerialReadBinary(RsStream *stream) {
  stream->ReadI32(&pointCount);
  if (pointCount < 2)
    return 0;
  x = new i32[pointCount]();
  altitude = new i32[pointCount]();
  z = new i32[pointCount]();
  distances = new i32[pointCount];
  for (i32 i = 0; i < pointCount; ++i) {
    stream->ReadI32(&x[i]);
    stream->ReadI32(&altitude[i]);
    stream->ReadI32(&z[i]);
  }
  NwComputeVehiclePathGeometry(this);
  stream->ReadU8(&beginOrientation);
  stream->ReadU8(&endOrientation);
  stream->ReadI32(&duration);
  duration = i32(u32(duration) + 1);
  NwComputeVehiclePathOrientation(this);
  loaded = 1;
  return 1;
}

// Write the binary vehicle-path marker, coordinates, orientation bytes and stored duration minus one.
u8 NwVehiclePathAttrib::SerialWrite(RsStream *stream) {
  u8 result = stream->WriteU8('B');
  result = result && stream->WriteU8('I') && stream->WriteU8('N') && stream->WriteU8('\r') &&
      stream->WriteI32(pointCount);
  for (i32 i = 0; i < pointCount; ++i) {
    if (result) {
      result = stream->WriteI32(x[i]);
      if (result) {
        result = stream->WriteI32(altitude[i]);
        if (result)
          result = stream->WriteI32(z[i]);
      }
    }
  }
  if (result) {
    result = stream->WriteU8(beginOrientation);
    if (result) {
      result = stream->WriteU8(endOrientation);
      if (result)
        result = stream->WriteI32(i32(u32(duration) - 1));
    }
  }
  return result;
}
