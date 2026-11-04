#ifndef SC3_MISC_NEIGHBORS_H
#define SC3_MISC_NEIGHBORS_H

#include <list>
#include <set>

#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_scenario.h"
#include "common/sc3.h"
#include "common/thread.h"

class CtNeighborDeal;

// Original list element is one retained deal pointer; its actual copy and destruction operations own the reference.
// This fixed game type preserves node retirement callbacks without a generic pointer or allocator framework.
struct CtDealRef {
  explicit CtDealRef(CtNeighborDeal *input);
  CtDealRef(const CtDealRef &source);
  ~CtDealRef();
  CtDealRef &operator=(const CtDealRef &) = delete;
  CtNeighborDeal *deal;
};

class CtDateInterface;

// Original84-byte retained deal owns two Strings, raw scalar terms and its independent starting date.
// Numeric field names remain neutral where the private original declaration has not been recovered.
class CtNeighborDeal : public CtDeal {
public:
  CtNeighborDeal();
  virtual ~CtNeighborDeal();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetFirstName(ScStringView *out) override;
  u8 GetSecondName(ScStringView *out) override;
  u32 GetValue08() override;
  u32 GetValue0c() override;
  u32 GetNeighborId() override;
  u32 GetType() override;
  u32 GetValue14() override;
  u32 GetValue18() override;
  u32 GetValue1c() override;
  u32 GetValue20() override;
  u8 GetDate(CtDateInterface *out) override;
  u8 SetFirstName(const ScStringView &text) override;
  u8 SetSecondName(const ScStringView &text) override;
  u32 SetValue08(u32 value) override;
  u32 SetValue0c(u32 value) override;
  u8 SetNeighborId(u8 value) override;
  u8 SetType(u8 value) override;
  u32 SetValue14(u32 value) override;
  u32 SetValue18(u32 value) override;
  u32 SetValue1c(u32 value) override;
  u32 SetValue20(u32 value) override;
  u8 SetDate(const CtDateInterface &value) override;
  u8 GetRecord(CtDealRecord *out) override;
  u8 SetRecord(const CtDealRecord *record) override;
  u8 CopyFrom(CtDeal *source) override;
  ScString firstName, secondName;
  u32 value08, value0c;
  u8 neighborId, dealType;
  u32 value14, value18, value1c, value20;
  CtDate date;
  u32 references;
};

class ScStopwatch;
extern ScStopwatch ScNeighborClock;

// Original six deal-type globals retain zero initialization and independent values after individual lookup failures.
extern u32 CtNeighborRates[6], CtNeighborQuantities[6], CtNeighborMinimumCosts[6];
extern u32 CtDealRateVariable, CtDealBasePenalty, CtDealPenaltyMultiplier;
extern u32 CtDealSellPopulation, CtDealBuyPopulation, CtDealPowerUnits, CtDealWaterUnits, CtDealGarbageUnits;

class CtCity;
class RsStream;
class CtNeighborDeal;
class GeBuildingLayer;
class EcLayer;
class CtResidentialLayer;
class CtPowerLayer;
class CtPlumbingLayer;
class UiNeighborWindow;

// Each original84-byte entry owns two Strings followed by population, its ceiling and six connection counters.
// The constructor initializes only the Strings; initialization or loading establishes the remaining fields.
struct CtNeighbor {
  ScString name, mayor;
  u32 population, populationLimit;
  u8 available;
  u32 road, rail, water, power, highway, subway;
  u8 flags[6];
};

// Original primary neighbor layer and secondary city-agent identities share one receiver and retained services.
// Reached methods use real fields and services; private declaration limits remain recorded in the DLL evidence.
class CtNeighbors : public CtNeighborsLayer, public CtAgent {
public:
  CtNeighbors();
  virtual ~CtNeighbors();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool HasDealType(u8 type) override;
  bool HasPendingDealType(u8 type) override;
  bool GetDealObject(u32 type, CtDeal **out) override;
  virtual bool GetPendingDealObject(u32 type, CtDeal **out) override;
  u8 GetDeal(u8 type, CtDealRecord *out) override;
  u8 GetPendingDeal(u8 type, CtDealRecord *out) override;
  bool StaticInit(CtDefinition *definition) override;
  bool ReadConfiguration();
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool InitializeNeighbors() override;
  bool Simulate(i32 phase) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  u8 LoadNeighborDealWindowList(UiNeighborWindow *query);
  u8 PublishDeal(u8 type, i32 amount) override;
  u8 GrowPopulation();
  u8 ValidateActiveDeals();
  u8 SimulateMonth();
  u8 SimulateYear();
  u8 SelectNeighbor(u8 type) override;
  bool CanDeal(u8 neighbor, u8 type) override;
  u8 ComputeOffer(u8 neighbor, u8 type, u32 maximum) override;
  u8 MakeOffer(u8 type) override;
  u8 GetDescriptionStringForDeal(u8 type, ScString *out);
  u8 GetRateStringForDeal(u8 type, i32 value, ScString *out);
  u8 GetContributionStringForDeal(i32 value, ScString *out);
  u8 RemoveDeal(u8 type) override;
  u8 RemovePendingDeal(u8 type) override;
  u8 CancelDeal(u8 type) override;
  u8 GetCanceledDeal(CtDeal **out) override;
  u8 AcceptPendingDeal(u8 type) override;
  bool Shutdown() override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  u8 ReadNeighborRecords(RsStream *stream);
  bool DoMessage(ScMessage *message) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool DebugInstanceTag(ScStringView *out) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  u8 AddConnection(i32 networkType, i32 x, i32 z) override;
  virtual u8 RemoveConnection(i32 networkType, i32 x, i32 z) override;
  bool HasConnection(i32 networkType, i32 x, i32 z) override;
  virtual u8 ConnectionSide(i32 x, i32 z) override;
  virtual bool HasSideConnection(i32 networkType, u8 side) override;
  u8 EdgeSide(i32 x, i32 z);
  u8 IncrementConnection(i32 networkType, i32 x, i32 z);
  u8 DecrementConnection(i32 networkType, i32 x, i32 z);
  std::set<u32> *ConnectionSet(i32 networkType);
  u32 *ConnectionCount(i32 networkType, u8 side);
  u8 NeighborFlag(u8 index) override;
  bool GetNeighborName(u8 index, ScStringView *out) override;
  virtual u8 SetNeighborName(u8 index, const ScStringView &name) override;
  u8 GetNeighborMayorName(u8 index, ScStringView *out) override;
  u32 GetNeighborPopulation(u8 index) override;
  u32 CountNeighbors() override;
  u32 CountNeighborsWithField140() override;
  u32 CountNeighborsWithField144() override;
  u32 CountNeighborsWithDeals() override;
  u32 GetRailConnections() override;
  u32 GetRoadConnections() override;
  u32 GetHighwayConnections() override;
  u32 GetSubwayConnections() override;
  u32 GetTotalRailConnections() override;
  u32 GetTotalRoadConnections() override;
  u32 GetTotalWaterConnections() override;
  u32 GetTotalPowerConnections() override;
  u32 GetTotalGarbageConnections() override;
  u32 GetTotalHighwayConnections() override;
  u32 GetTotalSubwayConnections() override;
  i32 GetTotalMonthlyNeighborDealIncome() override;
  i32 GetTotalMonthlyNeighborDealExpense() override;
  i32 GetNeighborDealPenaltyExpense() override;
  virtual u32 GetNumberOfNeighborDeals() override;
  virtual u8 GetNeighborDeals(u8 *count, CtDealRecord *out) override;
  virtual CtAgent *AsISC3CityAgent() override;
  u32 references;
  CtCity *city;
  GeBuildingLayer *buildings;
  EcLayer *pollution;
  CtPowerLayer *power;
  CtPlumbingLayer *plumbing;
  CtResidentialLayer *residential;
  std::list<CtDealRef> activeDeals, pendingDeals;
  ScLock lock;
  CtNeighbor neighbors[4];
  u32 width, height, penaltyExpense;
  u8 field428;
  std::set<u32> connections[6]; // Original order: rail, road, water, power, highway, subway.
  u32 day, month, year;
};

#endif
