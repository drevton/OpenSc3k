#ifndef SC3_NETWORK_NW_STREET_PRODUCER_H
#define SC3_NETWORK_NW_STREET_PRODUCER_H

#include <list>
#include <map>
#include <set>
#include <vector>

#include "city/ct_date.h"
#include "common/api.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "geometry/ge_types.h"

class CtCity;
class CtAgentTree;
class CtStartSimLayer;
class CtNeighborsLayer;
class CtResidentialLayer;
class GeBuildingLayer;
class ApBoxX;
class EcLayer;
class NwTrafficLayer;
class NwPeopleInterface;
class NwPeopleProducerAttribInterface;
class NwVehicle;
class NwVehicleProducerAttrib;
class NwTrainProducerAttrib;
class NwAirTrafficAttrib;
class NwAeroplane;
class NwHelicopter;
class NwBoatVehicle;
class NwShipVehicle;
class NwStrikeCrowd;
class NwRailVehicle;
class NwSubwayVehicle;
class NwVehicleProducerAttribInterface;
class ScOccupant;
class ScOccupantManager;
class ScDirtBag;
class OcAnimManager;
class NwBoatFilter;
class RsResource;
struct NwPeopleMode;
struct RsKey;

// The original SpiralIterator visits a bounded rectangle in expanding horizontal and vertical runs.
class ScSpiralIterator {
  friend class NwStrikeCrowdProducer;
public:
  ScSpiralIterator();
  void Reset(i32 x, i32 z); // Discarded nominal return remains unresolved.
  u8 Next(i32 *x, i32 *z); // Private bool/BYTE spelling remains unresolved.

private:
  u8 DoXZEnd(); // Private bool/BYTE spelling remains unresolved.
  void SetNextEndpoint(); // Discarded nominal return remains unresolved.
  i32 x, z, endX, endZ, left, top, right, bottom, direction;
  u32 unresolved36;
  i32 step;
  u8 state, alongX, finishedEdges;
};

struct NwVehicleFrequency {
  i32 weight, cumulativeWeight;
};

// Original vehicle-producer interface through the disappearance decision.
class NwVehicleProducerInterface : public ScUnknown {
public:
  virtual void Shutdown() = 0; // Discarded nominal return remains unresolved.
  virtual void VehicleDisappears(NwVehicle *vehicle) = 0;
  virtual bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) = 0;
};

// Original strike producer: the Linux symbols establish an occupant set and six vectors of strike-crowd pointers.
class NwStrikeCrowdProducer final : public NwVehicleProducerInterface, public ScMessageTarget {
public:
  NwStrikeCrowdProducer();
  ~NwStrikeCrowdProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  bool DoMessage(ScMessage *message) override;
  u8 Init(); // Nominal bool/BYTE spelling remains unresolved.
  u8 OccupantInserted(ScOccupant *occupant);
  u8 OccupantRemoved(ScOccupant *occupant);
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();

private:
  void EndStrike(std::vector<NwStrikeCrowd *> *crowds); // Discarded nominal return remains unresolved.
  void CreateStrikeVisuals(ScOccupant **occupants, u32 count, std::vector<NwStrikeCrowd *> *crowds);
  NwStrikeCrowd *CreateAndPlaceCrowdNearOccupant(ScOccupant *occupant);
  NwStrikeCrowd *CreateAStrikeCrowd();
  static void ExpandRectToIncludeNeighborhood(GeCellRect *rect, u32 distance);
  static bool IncludeRoads(ScOccupant *occupant);
  void BeginSchoolStrike();
  void BeginCollegeStrike();
  void BeginHealthStrike();
  void BeginFireStrike();
  void BeginPoliceStrike();
  void BeginTransitStrike();
  u8 initialized;
  u32 references;
  std::set<ScOccupant *> strikeOccupants;
  u8 unresolvedSetStorage[4]; // Modern x86 set is one word smaller than the original set.
  std::vector<NwStrikeCrowd *> schoolCrowds, collegeCrowds, healthCrowds, fireCrowds, policeCrowds, transitCrowds;
  ScLock lock;
  static CtCity *city;
  static CtAgentTree *agentTypeTree;
  static ScOccupantManager *surfaceOccupantManager;
  static OcAnimManager *animationManager;
  static CtResidentialLayer *residentialLayer;
  static u32 searchRadius;
};

// The ship requests route recomputation from its original producer after detecting a blocked water path.
// The producer's remaining methods and fields are not yet reconstructed.
class NwShipProducer final : public NwVehicleProducerInterface {
public:
  NwShipProducer();
  ~NwShipProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init(ScOccupant *occupant);
  void Tick(u32 elapsed); // Discarded nominal return remains unresolved.
  void PlaceInitialShip(); // Discarded nominal return remains unresolved.
  void InitPathRelatedInfo(); // The nominal return remains unresolved.
  void ReEvaluateSeaportMapAndPath(); // Discarded nominal return remains unresolved.
  i32 PierDirection();
  bool InCityBounds(u32 x, u32 z);
  bool AddPierTileIfConnected(ScOccupant *occupant);
  bool PierPresent(u32 x, u32 z);
  bool PierToSeawallConnectorPresent(u32 x, u32 z);
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static u8 StaticInit();
  static u8 StaticShutdown();
private:
  NwShipVehicle *CreateAShip();
  void CreateAnArrivingShip(); // Discarded nominal return remains unresolved.
  u8 rotation, initialized;
  u32 references, elapsedTicks, nextTick;
  ScPointXZ route[256];
  u32 routeCount;
  u8 routeReady;
  u32 pierX, pierZ;
  ScOccupant *pier;
  i8 pierDirection;
  u32 shipCount;
  static ScDirtBag *dirtBag;
  static ScOccupantManager *occupantManager;
  static GeBuildingLayer *buildingLayer;
  static CtNeighborsLayer *neighborsLayer;
  static u32 cellsX, cellsZ;
  static u8 seaportMapCreated;
  static NwVehicleProducerAttrib *shipAttributes[4];
};

// The original producer owns per-grid maps and arrays; its traffic calculations remain unresolved.
class NwVehicleProducer final : public NwVehicleProducerInterface {
  friend class NwStreetLayer;
  friend class NwStreetManipulator;
public:
  NwVehicleProducer();
  ~NwVehicleProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init(NwVehicleProducerAttribInterface *attributes); // Nominal bool/BYTE spelling remains unresolved.
  void SetEmergencyMode(bool enabled); // Discarded nominal return remains unresolved.
  void SetSimRect(const GeCellRect &rect); // Discarded nominal return remains unresolved.
  u8 SetVehicleProduceState(bool enabled); // Nominal return remains unresolved.
  void Tick(u32 elapsed); // Discarded nominal return remains unresolved.
  u8 GetAVehicleResourceInstance(u32 grid, u32 *out); // Nominal bool/BYTE spelling remains unresolved.
  u32 GetGridIndex(u32 x, u32 z);
  void AddVehicleTypeInfoForOccupant(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  void RemoveVehicleTypeInfoForOccupant(ScOccupant *occupant); // Discarded nominal return remains unresolved.
  static bool IncludeRoadsAndAvenues(ScOccupant *occupant);
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();

private:
  void AddAttribute(u32 grid, NwVehicleProducerAttrib *attributes); // Discarded nominal return remains unresolved.
  void RemoveAttribute(u32 grid, NwVehicleProducerAttrib *attributes); // Discarded nominal return remains unresolved.
  void UpdateFrequencies(u32 grid, f32 scale); // Discarded nominal return remains unresolved.
  void UpdateSimRect(); // The discarded nominal return remains unresolved.
  u8 PutAVehicleInRect(); // Original bool/BYTE spelling remains unresolved.
  void PutVehiclesInRect(const GeCellRect &rect); // Discarded nominal return remains unresolved.
  i32 PutVehiclesOnRoadTile(ScOccupant *occupant, u32 count); // Original vehicle construction remains unresolved.
  u32 references;
  u8 initialized;
  u32 vehicleCount, targetVehicles;
  f32 *gridWeights;
  std::map<u32, NwVehicleFrequency> *gridFrequencies;
  u32 *gridWeightTotals;
  u8 emergencyMode;
  GeCellRect simulationRect;
  u8 trafficThreshold, produceVehicles;
  static u32 maxX, maxZ, gridsX, totalGrids;
  static ScOccupantManager *occupantManager;
  static CtStartSimLayer *streetLayer;
  static NwTrafficLayer *trafficLayer;
  static ApBoxX *boxX;
};

// Original people-producer interface order. Discarded private return spellings remain unresolved.
class NwPeopleProducerInterface : public ScUnknown {
public:
  virtual u8 Init(NwPeopleProducerAttribInterface *attributes, u32 x, u32 z) = 0;
  virtual u8 Shutdown() = 0;
  virtual void ChangeLocation(u32 x, u32 z) = 0;
  virtual void PersonDisappears(NwPeopleInterface *person) = 0;
  virtual void SpreadSpectators(u32 x, u32 z, u32 count) = 0;
};

// Original producer object; selection, population and spectator operations remain unresolved dependencies.
class NwPeopleProducer final : public NwPeopleProducerInterface {
public:
  NwPeopleProducer();
  ~NwPeopleProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(NwPeopleProducerAttribInterface *attributes, u32 x, u32 z) override;
  u8 Shutdown() override;
  void ChangeLocation(u32 x, u32 z) override;
  void PersonDisappears(NwPeopleInterface *person) override;
  void SpreadSpectators(u32 x, u32 z, u32 count) override;
  u8 SetRemovalCompensation(bool enabled); // Original nominal return spelling remains unresolved.
  void Tick(u32 elapsed); // Original discarded nominal return remains unresolved.
  bool RectIntersect(const GeCellRect &first, const GeCellRect &second, GeCellRect *out);
  void ComputeSurroundRect(u32 x, u32 z, GeCellRect *out);
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static bool IncludeRoads(ScOccupant *occupant);

private:
  void SelectBehaviorMode(); // The nominal private return remains unresolved.
  void PutPeopleInRect(const GeCellRect &rect); // Its discarded source return remains unresolved.
  void PutPeopleOnRoadTile(ScOccupant *occupant, u32 count); // Its discarded return remains unresolved.
  u32 references;
  NwPeopleProducerAttribInterface *attributes;
  u32 x, z;
  u8 initialized;
  GeCellRect rect;
  NwPeopleMode *mode;
  u32 peopleCount, targetPeople;
  u8 removalCompensation;
  CtDate date;
  static u32 maxX, maxZ, totalPeople;
  static ScOccupantManager *occupantManager;
  static CtStartSimLayer *streetLayer;
};

// These abstract candidates expose only the recovered city-wide services; their instance layouts remain unresolved.
class NwTrainProducer final : public NwVehicleProducerInterface {
public:
  NwTrainProducer();
  ~NwTrainProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init(ScOccupant *occupant, NwTrainProducerAttrib *attributes);
  bool SetVehicleProduceState(bool enabled);
  void Tick(u32 elapsed); // The discarded nominal return remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  bool IncludeRail(ScOccupant *occupant);

private:
  u8 CreateAndAddRailVehicle(const RsKey &key, NwRailVehicle **vehicles, u32 *count);
  void CreateATrain(); // The original nominal return remains unresolved.
  NwTrainProducerAttrib *attributes;
  u32 references;
  u8 initialized;
  u32 unknown16; // The original constructor leaves this field unwritten; its role remains unresolved.
  ScOccupant *occupant;
  u8 direction;
  u32 elapsedTicks, nextTick;
  bool produceVehicles;
  static u32 maxX, maxZ;
  static ScOccupantManager *occupantManager;
};

class NwSubwayProducer final : public NwVehicleProducerInterface {
public:
  NwSubwayProducer();
  ~NwSubwayProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init(ScOccupant *occupant, NwTrainProducerAttrib *attributes);
  bool SetVehicleProduceState(bool enabled);
  void Tick(u32 elapsed); // The discarded nominal return remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  bool IncludeSubway(ScOccupant *occupant);

private:
  u8 CreateAndAddSubwayVehicle(const RsKey &key, NwSubwayVehicle **vehicles, u32 *count);
  void CreateASubwayTrain(); // The original nominal return remains unresolved.
  NwTrainProducerAttrib *attributes;
  u32 references;
  u8 initialized;
  u32 unknown16; // The original constructor leaves this field unwritten; its role remains unresolved.
  ScOccupant *occupant;
  u8 direction;
  u32 elapsedTicks, nextTick;
  bool produceVehicles;
  static u32 maxX, maxZ;
  static ScOccupantManager *occupantManager;
  static CtAgentTree *agentTypeTree;
};

class NwBoatProducer final : public NwVehicleProducerInterface {
public:
  NwBoatProducer();
  ~NwBoatProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init();
  void Tick(u32 elapsed); // Discarded nominal return remains unresolved.
  void BoatHitLand(u32 x, u32 z); // Discarded nominal return remains unresolved.
  bool CanPlaceBoat(u32 x, u32 z);
  void MarinaInsertedIntoCity(ScOccupant *occupant);
  void MarinaRemovedFromCity(ScOccupant *occupant);
  void SetNumberOfBoatsToProduce(u32 count); // Discarded nominal return remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static u8 StaticInit();
  static u8 StaticShutdown();

private:
  void RemoveBoats(u32 count); // Discarded nominal return remains unresolved.
  void DeterminePointsWhereBoatsCanBePlaced();
  bool FindAWaterPointToPlaceBoat(u32 *x, u32 *z);
  bool IsCloseToMarina(i32 x, i32 z);
  bool IsGridContainingCellUnoccupiedByBoats(u32 x, u32 z);
  void PlaceInitialBoats();
  u8 CreateAndPlaceABoat();
  NwBoatVehicle *CreateABoat(NwVehicleProducerAttrib *attributes);
  u8 initialized, byte5;
  u32 references, elapsedTicks, nextTick, boatCount, targetBoatCount;
  // Linux _List_base symbols identify the original list facilities and element types. Native list layout differs.
  std::list<u32> blockedCells, availableCells;
  std::list<ScOccupant *> marinas;
  std::list<NwBoatVehicle *> boats;
  static u32 cellsX, cellsZ;
  static ScDirtBag *dirtBag;
  static EcLayer *pollutionLayer;
  static NwBoatFilter *boatFilter;
  static OcAnimManager *animationManager;
  static RsResource *saltWaterAttributes, *freshWaterAttributes;
  static u32 maxMarinaDistanceSquared;
};

// The helicopter producer retains an air-traffic resource and tracks routine flights.
class NwHelicopterProducer final : public NwVehicleProducerInterface {
  friend class NwStreetLayer;
public:
  NwHelicopterProducer();
  ~NwHelicopterProducer();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init(NwAirTrafficAttrib *value); // The private bool/BYTE source spelling remains unresolved.
  void Tick(u32 elapsed); // The discarded nominal return remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  void SetNumNormalRoutineHelicopters(u32 count); // The discarded nominal return remains unresolved.
  NwHelicopter *DispatchHelicopter(u32 startX, u32 startZ, u32 targetX, u32 targetZ);

private:
  void CreateANormalRoutineHelicopter(); // The discarded nominal return remains unresolved.
  NwHelicopter *CreateAHelicopter();
  NwAirTrafficAttrib *attributes;
  u8 initialized;
  u32 references, elapsedTicks, nextTick, currentRoutineCount, targetRoutineCount;
  static CtCity *city;
};

// Original Linux symbols identify both plane collections as lists of cSSAeroplane pointers.
// The host standard library's list representation need not match the original compiler's layout.
class NwAirTrafficManager final : public NwVehicleProducerInterface {
  friend class NwStreetLayer;
public:
  NwAirTrafficManager();
  ~NwAirTrafficManager();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 Init(ScOccupant *occupant, NwAirTrafficAttrib *value);
  void Tick(u32 elapsed); // The discarded nominal return remains unresolved.
  void PlaceInitialPlanes(); // The discarded nominal return remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  bool RunwayTilePresent(u32 cellX, u32 cellZ);
  ScOccupant *AnchorTile();
  void GetRunwayBegin(u32 *cellX, u32 *cellZ); // Discarded source return remains unresolved.
  void GetRunwayEnd(u32 *cellX, u32 *cellZ); // Discarded source return remains unresolved.
  u8 TaxiwaySide();
  bool IsRunwayInXDirection();
  bool CanOperate();
  void EvaluateRunwayConfig(); // The discarded nominal return remains unresolved.
  u8 AddRunwayTileIfConnected(ScOccupant *occupant); // Private bool/BYTE spelling remains unresolved.
  i32 MicroYAtRunwayBegin(); // Source signedness remains unresolved.
  u8 GetLandingBeginCell(u32 *cellX, u32 *cellZ, u32 distance);
  void GetTaxiwayEnd(u32 *cellX, u32 *cellZ); // Discarded source return remains unresolved.
  void PlaneEnteredTaxiway(NwAeroplane *plane); // Discarded source return remains unresolved.
  void PlaneLeftTaxiway(NwAeroplane *plane); // Discarded source return remains unresolved.
  u8 TaxiwayCellEmpty(u32 cellX, u32 cellZ); // Private bool/BYTE spelling remains unresolved.
  u8 ReserveRunway(NwAeroplane *plane); // Private bool/BYTE spelling remains unresolved.
  void RelinquishRunway(NwAeroplane *plane); // Discarded source return remains unresolved.

private:
  void CreateAnArrivingPlane(); // The discarded nominal return remains unresolved.
  NwAeroplane *CreateAPlane();
  void DetermineTaxiwaySide(); // The discarded nominal return remains unresolved.
  void KillAllPlanes(); // The discarded nominal return remains unresolved.
  NwAirTrafficAttrib *attributes;
  u32 references;
  u8 initialized, runwayChanged;
  ScOccupant *anchor;
  u32 anchorX, anchorZ;
  std::list<NwAeroplane *> taxiwayPlanes;
  NwAeroplane *runwayPlane;
  u32 elapsedTicks, nextTick;
  u8 runwayDirection, taxiwaySide;
  u32 runwayBeginX, runwayBeginZ, runwayEndX, runwayEndZ, maxPlanes;
  std::list<NwAeroplane *> planes;
  static u32 cityXMicroUnits, cityZMicroUnits, maxCellX, maxCellZ;
  static ScOccupantManager *occupantManager;
  static OcAnimManager *animationManager;
  static CtStartSimLayer *streetLayer;
  static ScDirtBag *dirtBag;
};

#endif
