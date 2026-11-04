#ifndef SC3_BUILDINGS_BD_WATER_H
#define SC3_BUILDINGS_BD_WATER_H

#include <list>

#include "buildings/bd_power.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_scenario.h"
#include "city/simulation.h"
#include "common/thread.h"
#include "economy/ec_pollution.h"
#include "sprites/api.h"

// Actual IID62FD2588 plumbing view; the building, occupant and agent views follow its aging prefix.
// Setter nominal returns are unresolved and the unused result is represented by its observed effect.
class BdPlumbingView : public ScUnknown {
public:
  virtual void SetMaximumAge(u32 value) = 0;
  virtual u32 GetMaximumAge() = 0;
  virtual void SetDeclineAge(u32 value) = 0;
  virtual u32 GetDeclineAge() = 0;
  virtual u32 GetAge() = 0;
  virtual bool IsAging() = 0;
  virtual void Retire() = 0;
  virtual u8 GetRetired() = 0;
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual BdBuilding *AsISC3Building() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual void SetFullCapacity(u16 value) = 0;
  virtual u16 GetFullCapacity() const = 0;
  virtual void SetCapacity(u16 value) = 0;
  virtual u16 GetCapacity() const = 0;
  virtual void SetProduction(u16 value) = 0;
  virtual u16 GetProduction() const = 0;
  virtual void SetPumpType(u16 value) = 0;
  virtual u16 GetPumpType() const = 0;
  virtual void SetWaterDistance(u16 value) = 0;
  virtual u16 GetWaterDistance() const = 0;
  virtual void BecomeDefunctDueToAge() = 0;
};

// Shared static plumbing building, with the original adjusted plumbing receiver at offset44 on x86.
class BdPlumbingBuilding : public OcUtilityBuilding, public BdPlumbingView {
public:
  BdPlumbingBuilding();
  ~BdPlumbingBuilding() override;
  using OcUtilityBuilding::Init;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetMaximumAge(u32 value) override;
  u32 GetMaximumAge() override;
  void SetDeclineAge(u32 value) override;
  u32 GetDeclineAge() override;
  u32 GetAge() override;
  bool IsAging() override;
  void Retire() override;
  u8 GetRetired() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  void SetFullCapacity(u16 value) override;
  u16 GetFullCapacity() const override;
  void SetCapacity(u16 value) override;
  u16 GetCapacity() const override;
  void SetProduction(u16 value) override;
  u16 GetProduction() const override;
  void SetPumpType(u16 value) override;
  u16 GetPumpType() const override;
  void SetWaterDistance(u16 value) override;
  u16 GetWaterDistance() const override;
  void BecomeDefunctDueToAge() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  u32 GetGeomFlag() override;
  SpSprite *GetSpriteInst(u32 layer) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  SpSprite *plumbingSprite;
  u16 fullCapacity, capacity, production, pumpType, waterDistance;
  CtDate constructionDate;
  u32 maximumAge, declineAge;
  u8 retired;
};

// Animated plumbing is a separate original receiver with the same 84-byte field layout.
class BdPlumbingBuildingAnimated : public GeUtilityAnimatedBuilding, public BdPlumbingView {
public:
  BdPlumbingBuildingAnimated();
  ~BdPlumbingBuildingAnimated() override;
  using OcUtilityBuilding::Init;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetMaximumAge(u32 value) override;
  u32 GetMaximumAge() override;
  void SetDeclineAge(u32 value) override;
  u32 GetDeclineAge() override;
  u32 GetAge() override;
  bool IsAging() override;
  void Retire() override;
  u8 GetRetired() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  void SetFullCapacity(u16 value) override;
  u16 GetFullCapacity() const override;
  void SetCapacity(u16 value) override;
  u16 GetCapacity() const override;
  void SetProduction(u16 value) override;
  u16 GetProduction() const override;
  void SetPumpType(u16 value) override;
  u16 GetPumpType() const override;
  void SetWaterDistance(u16 value) override;
  u16 GetWaterDistance() const override;
  void BecomeDefunctDueToAge() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  u32 GetGeomFlag() override;
  SpSprite *GetSpriteInst(u32 layer) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  SpSprite *plumbingSprite;
  u16 fullCapacity, capacity, production, pumpType, waterDistance;
  CtDate constructionDate;
  u32 maximumAge, declineAge;
  u8 retired;
};

// The pump and tower install distinct original vtables but retain the shared static plumbing state.
class BdWaterPump : public BdPlumbingBuilding {
public:
  BdWaterPump();
  ~BdWaterPump() override;
};

class BdWaterTower : public BdPlumbingBuilding {
public:
  BdWaterTower();
  ~BdWaterTower() override;
};

// The desalination plant uses the distinct animated plumbing receiver and a specialized detail query.
class BdDesalinationPlant : public BdPlumbingBuildingAnimated {
public:
  BdDesalinationPlant();
  ~BdDesalinationPlant() override;

protected:
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
};

class CtPlumbingSimulation;
class UiQuery;
enum class CtDealType : i32;

// Original pump-list entry; fields not yet assigned semantic names retain their observed widths and order.
struct CtWaterPumpRecord {
  CtWaterPumpRecord();
  CtCoord position;
  u32 unknown12;
  u16 fullCapacity, capacity, production, unknown22, pumpType, unknown26, waterDistance;
};

bool operator==(CtCoord first, CtCoord second);
bool operator==(CtWaterPumpRecord first, CtWaterPumpRecord second);

// Original Bit1 square-grid routines. Mutating helpers expose their effects while nominal returns remain unresolved.
void CtWaterBitsClear(u32 *bits, i32 width);
i32 CtWaterBitsCount(u32 *bits, i32 width);
void CtWaterBitsXor(u32 *destination, u32 *source, i32 width);
void CtWaterBitsCopy(u32 *destination, u32 *source, i32 width);
void CtWaterBitsSelectionSet(u32 *destination, u32 *source, i32 width);
u8 CtWaterBitsGrowHorizontal(u32 *bits, i32 width, i32 *firstRow, i32 *endRow);
u8 CtWaterBitsGrowVertical(u32 *bits, i32 width, i32 *firstRow, i32 *endRow);
i32 CtWaterBitsGrow(u32 *bits, i32 width, i32 limit, i32 *firstRow, i32 *endRow);
u8 CtWaterBitsSelectionGrowHorizontal(u32 *bits, u32 *selection, i32 width, i32 *firstRow, i32 *endRow);
u8 CtWaterBitsSelectionGrowVertical(u32 *bits, u32 *selection, i32 width, i32 *firstRow, i32 *endRow);
i32 CtWaterBitsSelectionGrow(u32 *bits, u32 *selection, i32 width, i32 limit, i32 *firstRow, i32 *endRow);

// Original water raster retains its plumbing layer and reads city services when sampling.
class CtPlumbingMapSprite : public SpUtilityMapSprite {
public:
  explicit CtPlumbingMapSprite(CtPlumbingSimulation *layer);
  ~CtPlumbingMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  friend class CtPlumbingSimulation;
  CtPlumbingSimulation *layer;
};

// Shared plumbing manipulator base borrows its layer and holds the SIMUTIL module for each reference.
class CtPlumbingManipulatorBase : public CtLayerManipulator {
public:
  explicit CtPlumbingManipulatorBase(CtPlumbingSimulation *layer);
  virtual ~CtPlumbingManipulatorBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  ScOccupant *GetOccupantPtr(CtAgent *agent);

protected:
  u32 references;
  CtPlumbingSimulation *layer;
};

// Monthly manipulator uses the shared reference base and delegates simulation to its borrowed layer.
class CtPlumbingMonthlyManipulator : public CtPlumbingManipulatorBase {
public:
  explicit CtPlumbingMonthlyManipulator(CtPlumbingSimulation *layer);
  ~CtPlumbingMonthlyManipulator() override;
  bool Apply(CtAgent *agent) override;
};

// Installed primary plumbing layer and secondary map-sprite factory; foreign-vtable parity remains unresolved.
class CtPlumbingSimulation : public CtPlumbingLayer, public SpMapSpriteFactory {
public:
  CtPlumbingSimulation();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // The installed callers use only these synchronization effects; their nominal returns remain unresolved.
  void Lock();
  void Unlock();
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  u32 GetLayerType() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u8 IsWatered(const CtCoord &position) const override;
  u8 IsWatered(u32 x, u32 z) const override;
  // Linux identifies this three-output overload; its separate PC entry and nominal return remain unresolved.
  void GetWaterData(u32 *consumers, i64 *capacity, i64 *used);
  void GetWaterData(u32 *consumers, i64 *capacity, i64 *used, u32 *first, u32 *second) override;
  u32 GetProductionCapacity(u32 type) override;
  u32 GetNumPumpsAging() override;
  u32 GetAverageWaterPollutionInPumps() override;
  u8 GetDiffMap(u32 *out, u32 count) override;
  u32 GetMapSpriteCount() override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  virtual ~CtPlumbingSimulation();
  // The original monthly simulation's nominal return remains unidentified.
  void DoMonthlySimulation();
  // The original save-capacity helper's return is not used and its nominal source type is unresolved.
  void SaveCapacityForAllPumps();
  // The original capacity recalculation's nominal return is unresolved; callers use its state changes.
  void CalculateCapacityForAllPumps();
  // The four deal callbacks' nominal returns are unresolved and discarded by their original caller.
  void UpdateBuyerDeal(CtDealRecord deal, i64 *available);
  void UpdateSellerDeal(CtDealRecord deal, i64 *available, i64 *required);
  void FinalizeSellerDeal(CtDealRecord deal);
  void FinalizeBuyerDeal(CtDealRecord deal);
  // Linux supplies the deal selector and output record; the private nominal return is unresolved.
  void GetWaterDeal(CtDealType type, CtDealRecord *deal);
  u8 FindPump(u16 x, u16 z, CtWaterPumpRecord *out);
  // Both private methods expose their installed effects; the source return declarations remain unresolved.
  void AddFakePumps(CtDealType type, u32 *bits, CtDealRecord *deal);
  u8 FindBuyerOrSeller(CtDealType type, u32 *bits, CtDealRecord *deal);
  u32 GetPotentialCapacity(u16 pumpType);
  // Linux identifies these edge helpers; their PC entry points and private return declarations remain unresolved.
  u8 IsOnTheEdge(i16 x, i16 z);
  i32 WhoIsTheNeighbor(i16 x, i16 z);
  bool ValidPos(i16 x, i16 z) const;
  bool IsThereWater(i16 x0, i16 z0, i16 x1, i16 z1, u8 waterType);
  u16 BuildingConsumption(u16 x, u16 z);
  // The private drain helper's return register is ignored; its source return declaration remains unresolved.
  void ChangeLimit(u32 mask, u32 *buildings, i64 *remaining, i32 word, i32 row, i32 index);
  u8 GrowHorizontalWithDrain(
      u32 *bits, u32 *selection, u32 *buildings, u32 width, i64 *remaining, i32 *firstRow, i32 *endRow);
  u8 GrowVerticalWithDrain(
      u32 *bits, u32 *selection, u32 *buildings, u32 width, i64 *remaining, i32 *firstRow, i32 *endRow);
  // The private window handlers' nominal Boolean/BYTE returns remain unresolved.
  u8 OnWindowCreate(ScMessage *message, ScUnknown *source);
  u8 OnWindowDestroy(ScMessage *message, ScUnknown *source);
  u8 OnWindowRefresh(ScMessage *message, ScUnknown *source);
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  u32 GetInfoStringCount();
  // The original query formatter's nominal Boolean/BYTE return declaration remains unresolved.
  u8 GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 *categories, u32 index, bool *warning);
  static void RegisterAgentModifier(const ScString &key, const ScString &value, void *context);
  bool RegisterCityServices();
  // Original unregister operation's nominal return is unresolved and ignored by shutdown.
  void UnregisterCityServices();
  bool UnderOccupantInserted(ScOccupant *occupant);
  bool UnderOccupantRemoved(ScOccupant *occupant);
  bool SurfaceOccupantInserted(ScOccupant *occupant);
  bool SurfaceOccupantRemoved(ScOccupant *occupant);
  // Original specialized callbacks' nominal returns are unresolved.
  void WaterMainInserted(ScOccupant *occupant);
  void WaterMainRemoved(ScOccupant *occupant);
  void PumpInserted(ScOccupant *occupant);
  void PumpRemoved(ScOccupant *occupant);
  void TowerInserted(ScOccupant *occupant);
  void TowerRemoved(ScOccupant *occupant);
  void DesalinationInserted(ScOccupant *occupant);
  void DesalinationRemoved(ScOccupant *occupant);
  void BuildingInserted(ScOccupant *occupant);
  void BuildingRemoved(ScOccupant *occupant);

protected:
  u16 mapWidth;
  u8 initialized, unrecovered11;
  u32 agingPumps;
  u32 references;
  ScLock *referenceLock;
  CtCity *city;
  ScOccupantManager *surfaceOccupants, *undergroundOccupants;
  CtOrdinanceLayer *ordinances;
  CtPowerLayer *power;
  EcLayer *pollution;
  ScDirtBag *terrain;
  EcBudgetLayer *budget;
  CtNeighborsLayer *neighbors;
  ScGraphInfoView *waterHistory;
  u32 *differenceBits;
  ScLock operationLock;
  u32 *waterBits;
  u32 *buildingBits, *mainBits;
  std::list<CtWaterPumpRecord> *pumps;
  u32 simulationCycle;
  u8 conservationOrdinance, meterOrdinance, unrecovered126[34];
  u16 terrainWidth, terrainHeight;
  u32 pumpPollution;
  u8 sellerConnected, buyerConnected, unrecovered170[6];
  u32 totalCapacity[2], totalUsed[2];
  u32 firstWaterCount, secondWaterCount;
  u32 unrecovered200[4], unrecovered216[4];
};

// SIMUTIL water-treatment building and its distinct mortality/treatment view at x86 offset44.
// Descriptor, sprite and replacement operations remain explicit dependencies of the complete product.
class BdWaterTreatment : public OcUtilityBuilding, public BdTreatment {
public:
  BdWaterTreatment();
  ~BdWaterTreatment() override;
  using OcUtilityBuilding::Init;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetMaximumAge(u32 value) override;
  u32 GetMaximumAge() override;
  void SetDeclineAge(u32 value) override;
  u32 GetDeclineAge() override;
  u32 GetAge() override;
  bool IsAging() override;
  void Retire() override;
  u8 GetRetired() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *GetOccupant() override;
  u32 GetProduction() override;
  u32 GetCapacity() override;
  u32 Consume(u32 amount) override;
  u32 GetGeomFlag() override;
  SpSprite *GetSpriteInst(u32 layer) override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  bool Simulate(i32 phase) override;
  i32 MinimumExecutionInterval() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;

private:
  void DoWeeklySim();
  void DoMonthlySim();
  // Original private replacement effect and its nominal return remain unresolved.
  virtual void BecomeDefunctDueToAge();
  SpSprite *treatmentSprite;
  u32 dailyCapacity;
  CtDate constructionDate;
  u32 consumption, previousConsumption;
  u8 retired;
};

#endif
