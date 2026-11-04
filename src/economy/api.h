#ifndef SC3_ECONOMY_API_H
#define SC3_ECONOMY_API_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "geometry/api.h"
#include "misc/api.h"
#include "ui/commands.h"

// Budget failure checks use a debt magnitude; serialized graph values use a separate positive bias.
enum EcBudgetBoundary : i32 {
  kEcBudgetGraphValueBias = 100000,
  kEcBudgetDebtMagnitudeLimit = 0x186a0,
};

// SC3BudgetLayer.ini [SpecialCosts] indices use the signed development-cost overload, not occupant taxonomy.
enum EcSpecialCost : i32 {
  kEcCostLowDensityZone = 0,
  kEcCostMedDensityZone = 1,
  kEcCostHighDensityZone = 2,
  kEcCostLandfillZone = 3,
  kEcCostAirportZone = 4,
  kEcCostSeaportZone = 5,
  kEcCostDezone = 6,
  kEcCostDemolish = 7,
  kEcCostLevelTerrain = 8,
  kEcCostRaiseTerrain = 9,
  kEcCostLowerTerrain = 10,
  kEcCostDecorativeWater = 11,
  kEcCostDemolishRubble = 12,
};

// Demand, external-supply and workforce keys identify real valves rather than occupant classes or COM interfaces.
// Pollution installs the separate disposal-demand key used by landfill development.
enum EcValveId : u32 {
  kEcResidentialValveId = 3000,
  kEcFunValveId = 3001,
  kEcCommercialValveId = 5000,
  kEcCommercialConnectionsValveId = 5001,
  kEcWorkforceValveId = 7000,
  kEcIndustrialValveId = 8000,
  kEcIndustrialConnectionsValveId = 8001,
  kEcDisposalDemandValveId = 0xa2c9e821,
};

class GeBuildingType;
class CtZoneLayer;
struct GeCellRect;

namespace EcRci {
class ScRandom;
}

// Budget cheat identities use the actual registered command names.
enum EcBudgetCheatId : u32 {
  kEcIAmWeakCheatId = 0x62e8630b,
  kEcSoSueMeCheatId = 0x23b4418f,
};

// One original land-use entry contains independent weighted dry and shoreline building lists.
struct EcBlockGroup {
  EcBlockGroup *next;
  u32 identifier;
  ScArray<GeBuildingType *> dry, shoreline;
};

// Apply the common block-placement path using the calling rule's tuning and transport policy.
u8 EcPlaceBlock(EcBlockGroup *groups, EcRci::ScRandom *random, CtZoneLayer *zones, u32 max_distance,
    bool check_transport, u32 minimum, u32 maximum_size, u32 threshold, u32 shore_chance, const GeCellRect &rectangle,
    u32 identifier, i32 demand, i32 maximum);

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Reached SIMMISC budget interface (IID E11BCC69), same-address primary.
// SDK cISC3BudgetLayer.h names apply; installed tax and borrowing-limit widths supersede its marked/incorrect declarations.
// Complete original order: appended SDK methods cannot follow a compact subset without shifting every later slot.
// MSVC reverses the GetFunding and GetCost overload groups; preserve their source declaration order.
class EcBudgetLayer : public ScUnknown {
public:
  virtual void SetTotalFunds(i64 funds) = 0;  // SDK30; +12,100051ED: paired funds/mirror writes, RET8.
  virtual i64 GetTotalFunds() = 0;            // +16,10005204: signed64 field+56.
  virtual bool WithdrawFunds(u32 amount) = 0; // SDK33; +20,10008946: AL0/1, one DWORD, RET4.
  virtual void DepositFunds(u32) = 0;
  virtual void DepositDisasterReliefFunds(u32) = 0;
  virtual i64 GetYTDIncome() = 0;
  virtual i64 GetEstIncome() = 0; // +36,10008AE1.
  virtual i64 GetYTDExpenses() = 0;
  virtual i64 GetEstExpenses() = 0; // +44,10008D92.
  virtual bool AddDepartmentBudget(EcDepartmentBudget *) = 0;
  virtual void RemoveDepartmentBudget(EcDepartmentBudget *) = 0;
  virtual bool GetDepartmentBudget(u32, EcDepartmentBudget **) = 0;
  virtual void NeededFundingChanged(EcDepartmentBudget *) = 0;
  virtual void FundingPercentageChanged(EcDepartmentBudget *) = 0;
  virtual u32 GetFunding(EcDepartmentBudget *) = 0;
  virtual i64 GetFunding(u32) = 0;            // SIMMISC +68,10008734: signed64; the SDK return width is incorrect.
  virtual u8 GetTaxRate(i32 type) = 0;        // +76,1000520B: raw byte, SDK's float is marked Verify.
  virtual f32 GetValveModifier(u8 rate) = 0;  // +80,1000892F: FLD f32, clamps rate to22.
  virtual i32 GetCost(i32 development) = 0;   // SDK53; +88,10008FA0: full signed development key.
  virtual i32 GetCost(u32 type) = 0;          // SDK54; +84,10008FBE: full occupant type ID, EAX result, RET4.
  virtual bool IssueBond(u32 amount) = 0;     // SDK56; +92,10009150: AL0/1, one DWORD, RET4.
  virtual i64 GetTotalBorrowed() = 0;         // +96,1000521F: signed64 field+1424.
  virtual i64 GetCurrentBorrowingLimit() = 0; // +100,100092AA: zero-extended32 product; SDK says int32.

  virtual void SetMarxism(bool) = 0;
  virtual bool MarxismIsOn() = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtCity;
class RsStream;

// Existing unsigned-DWORD specialization retains its recovered pointer input spelling.
class ScDwordMap : public ScCellMap<u32> {
public:
  virtual bool Init(CtCity *city, const u32 *fill) = 0;
  virtual bool Shutdown() = 0;
  virtual void GetValue(u32 x, u32 z, u32 *value) const = 0;
  virtual void SetValue(u32 x, u32 z, const u32 *value) = 0;
  virtual void SetAllCells(const u32 *value) = 0;
  virtual void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u32 *value) = 0;
};

// SIMRCI's shared lifetime base retains its own DLL director for commands, developers and zoning rules.
class EcCommandOwner {
public:
  EcCommandOwner();
  virtual ~EcCommandOwner();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// Original SIMRCI command receiver owns input/output descriptors and retained variant queues.
class EcRciCommand : public UiCommandInterface, public EcCommandOwner {
public:
  EcRciCommand();
  ~EcRciCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool PushBackInput(ScVariant *argument) override;
  bool PopFrontInput(ScVariant **out) override;
  u32 InputQueueSize() override;
  bool EnumInputParameters(void (*callback)(i32, u32, void *), void *context) override;
  bool PushBackOutput(ScVariant *argument) override;
  bool PopFrontOutput(ScVariant **out) override;
  u32 OutputQueueSize() override;
  bool EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) override;

protected:
  virtual bool AddInputParameter(i32 type, u32 flags);
  virtual bool AddOutputParameter(i32 type, u32 flags);
  ScArray<UiCommandParameter> input_parameters, output_parameters;
  ScArray<ScVariant *> inputs, outputs;
};

// SIMRCI's command-factory base owns its registered children and retains the SIMRCI director for each reference.
class EcCommandFactory : public ScCommandFactory {
public:
  EcCommandFactory();
  virtual ~EcCommandFactory();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetCommand(u32 class_id, u32 iid, void **out) override;
  bool AddCommandFactory(ScCommandFactory *factory) override;
  virtual bool CreateCommand(u32 class_id, u32 iid, void **out) = 0;

protected:
  u32 references;
  ScArray<ScCommandFactory *> children;
};

// The zone factory constructs the original command variants by class identifier.
class EcZoneCommandFactory : public EcCommandFactory {
public:
  bool CreateCommand(u32 class_id, u32 iid, void **out) override;
};

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3CommercialLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class EcCommercialLayer : public ScUnknown {
public:
  virtual u8 GetDevelopmentCap() = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtCity;
class CtLayer;
class RsStream;

// Reached cISC3Valve contract (IID F1EC30), base constructor17F8D and table1001BE90.
// The original public interface follows Unknown and places QuerySupplyValue at40. Private recomputation remains unresolved.
class EcValve : public ScUnknown {
public:
  virtual bool QueryInterface(u32 iid, void **out) = 0;
  virtual u32 AddRef() = 0;
  virtual u32 Release() = 0;
  virtual bool Init(CtCity *city, u32 identifier, CtLayer *listener) = 0;
  virtual bool Shutdown() = 0;
  virtual bool SimulationBegin() = 0;
  virtual bool SerialRead(CtCity *city, RsStream *record) = 0;
  virtual u8 SerialWrite(CtCity *city, RsStream *record) const = 0;
  virtual u32 GetId() = 0;
  virtual void SetId(u32 identifier) = 0;
  virtual i32 QuerySupplyValue() = 0;
  virtual i32 QueryDemandValue() = 0;
  virtual i32 QueryValveValue() = 0;
  virtual f32 QueryEconomyModifier() = 0;
  virtual i32 QueryValveMax() = 0;
  virtual i32 QueryValveMin() = 0;
  virtual bool AddToSupplyValue(i32 amount) = 0;
  virtual bool AddToDemandValue(i32 amount) = 0;
  virtual bool SetSupplyValue(i32 amount) = 0;
  virtual bool SetDemandValue(i32 amount) = 0;
  virtual void SetValveMax(i32 maximum) = 0;
  virtual void SetValveMin(i32 minimum) = 0;
  virtual void SetInitialSupplyValue(i32 amount) = 0;
  virtual void SetInitialDemandValue(i32 amount) = 0;
  virtual f32 GetEconomyModifier() = 0;
  virtual void SetEconomyModifier(f32 modifier) = 0;
  virtual f32 GetTaxModifier() = 0;
  virtual void SetTaxModifier(f32 modifier) = 0;
  virtual u8 GetDemandCap() = 0;
  virtual void SetDemandCap(u8 cap) = 0;
  virtual bool EndOfMonth() = 0;
  virtual void DebugLockValue(i32 value) = 0;
  virtual void DebugUnlockValue() = 0;
  virtual bool DebugIsValueLocked() = 0;
};

// SIMECO's concrete valve retains its own module and stores the original scalar simulation state.
class EcValveBase : public EcValve {
public:
  EcValveBase();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool Init(CtCity *city, u32 identifier, CtLayer *listener) override;
  virtual bool Shutdown() override;
  virtual bool SimulationBegin() override;
  virtual bool SerialRead(CtCity *city, RsStream *record) override;
  virtual u8 SerialWrite(CtCity *city, RsStream *record) const override;
  virtual u32 GetId() override;
  virtual void SetId(u32 identifier) override;
  virtual i32 QuerySupplyValue() override;
  virtual i32 QueryDemandValue() override;
  virtual i32 QueryValveValue() override;
  virtual f32 QueryEconomyModifier() override;
  virtual i32 QueryValveMax() override;
  virtual i32 QueryValveMin() override;
  virtual bool AddToSupplyValue(i32 amount) override;
  virtual bool AddToDemandValue(i32 amount) override;
  virtual bool SetSupplyValue(i32 amount) override;
  virtual bool SetDemandValue(i32 amount) override;
  virtual void SetValveMax(i32 maximum) override;
  virtual void SetValveMin(i32 minimum) override;
  virtual void SetInitialSupplyValue(i32 amount) override;
  virtual void SetInitialDemandValue(i32 amount) override;
  virtual f32 GetEconomyModifier() override;
  virtual void SetEconomyModifier(f32 modifier) override;
  virtual f32 GetTaxModifier() override;
  virtual void SetTaxModifier(f32 modifier) override;
  virtual u8 GetDemandCap() override;
  virtual void SetDemandCap(u8 cap) override;
  virtual bool EndOfMonth() override;
  virtual void DebugLockValue(i32 value) override;
  virtual void DebugUnlockValue() override;
  virtual bool DebugIsValueLocked() override;
  virtual ~EcValveBase();

protected:
  // Linux cSC3Valve::calculateValveValue(bool),0823731C, matches18381; its ignored nominal return remains unresolved.
  virtual void CalculateValveValue(bool smooth);
  void NotifyValue() const;
  u32 references, identifier;                 // Original +04,+08.
  i32 supply, demand;                         // +0C,+10.
  u32 supply_accumulator, demand_accumulator; // +14,+18.
  i32 initial_supply, initial_demand;         // +1C,+20.
  i32 maximum, minimum, balance;              // +24,+28,+2C.
  bool flag_30, flag_31;                      // Original constructor initializes both true.
  f32 economy_modifier, tax_modifier;         // +34,+38.
  u8 demand_cap;                              // +3C.
  CtCity *city;                               // +40, borrowed until Shutdown.
};

// The disposal valve overrides the signed supply/demand balance and updates current values immediately.
class EcDisposal : public EcValveBase {
public:
  EcDisposal();
  ~EcDisposal() override;
  i32 QueryValveValue() override;
  bool AddToSupplyValue(i32 amount) override;
  bool AddToDemandValue(i32 amount) override;
  bool EndOfMonth() override;
};

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3IndustrialLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class EcIndustrialLayer : public ScUnknown {
public:
  virtual u8 GetDevelopmentCap() = 0;
};

// Copyright (C) 2025 Nicholas Hayes
/*
 * Interface declarations adapted from sc3k-gzcom-dll, Copyright (C) 2024-2026 Nicholas Hayes.
 * Distributed under LGPL-2.1-or-later; see evidence/SIMCITY/sdk-license.txt.
 * Sources: cISC3Building.h, cISC3AgentTypeTree.h and cISC3CityChangeReceiver.h.
 */

class ScOccupant;
struct CtCoord;

// Complete public cISC3Building order, verified against SIMGEOM's primary table.
// MSVC reverses the Init overload group, placing the city, attributes and key variants at20,24 and28.
class BdBuilding : public ScUnknown {
public:
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual bool Init(RsKey *) = 0;
  virtual bool Init(GeAttrib *) = 0;
  virtual bool Init(CtCity *, OcPersistInfo *) = 0;
  // Actual cleanup has no result; this agrees with the SDK building view and avoids exposing residual EAX as bool.
  virtual void Shutdown() = 0;
  virtual bool Save(CtCity *, OcPersistInfo *) = 0;
  // Verified shared city-agent producer returns Boolean AL; SDK building view discards it.
  virtual bool Simulate(i32) = 0;
  virtual CtAgentType *AgentType() = 0;
  // Shared producer24E4B returns false without changing the pointer; SDK void is not a separate operation.
  virtual bool SetAgentType(CtAgentType *) = 0;
  virtual bool SetName(ScStringView const &) = 0;
  virtual bool IsNameChangeable() = 0;
  virtual bool HasBirthdate() const = 0;
  virtual bool GetBirthdate(CtDateInterface *) const = 0;
  virtual bool SetBirthdate(CtDateInterface const &) = 0;
  virtual void SetOccupancyState(u32) = 0;
  virtual u32 GetOccupancyState() = 0;
  virtual void SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState) = 0;
  virtual bool GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState) = 0;
  virtual void ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState) = 0;
  virtual u8 PowerRequired() = 0; // Installed byte producer; SDK bool does not preserve raw property values.
  virtual GeAttrib *GetBaseAttribData() = 0;
};

// Reached foreign IID041D994C view: original +12 receives the actual history model pointer.
// SIMUI snapshot primary0 is the writer; AL0/1 uses an observed byte carrier, with nominal bool/BYTE unresolved.
class ScModelReceiver : public ScUnknown {
public:
  virtual u8 DumpGraphData(ScGraphInfoView *model) = 0;
};

// Reached IID5E8 from SIMUI's real whole+100 collection view. Original+12 retains a nonnull Unknown and appends it;
// the verified AL0/1 carrier does not assert a nominal bool/BYTE declaration or source inheritance with the model receiver.
class ScObjectCollection : public ScUnknown {
public:
  virtual u8 Add(ScUnknown *object) = 0;
};

/*
 * Interface declaration adapted from sc3k-gzcom-dll cISC3ResidentialLayer.h.
 * Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.
 */

// Original queried IID4106C508 residential interface, in the SDK and installed primary-vtable order.
class CtResidentialLayer : public ScUnknown {
public:
  virtual bool SchoolIsOnStrike() = 0;
  virtual bool CollegeIsOnStrike() = 0;
  virtual bool HealthIsOnStrike() = 0;
  virtual i32 ChanceOfSchoolStrike() = 0;
  virtual i32 ChanceOfCollegeStrike() = 0;
  virtual i32 ChanceOfHealthStrike() = 0;
  virtual bool GetStrikingSchoolBuildings(ScOccupant **, u32, u32 *) = 0;
  virtual bool GetStrikingCollegeBuildings(ScOccupant **, u32, u32 *) = 0;
  virtual bool GetStrikingHealthBuildings(ScOccupant **, u32, u32 *) = 0;
  virtual void EndSchoolStrike() = 0;
  virtual void EndCollegeStrike() = 0;
  virtual void EndHealthStrike() = 0;
  virtual u8 GetHealthSystemRating() = 0;
  virtual u8 GetSchoolSystemRating() = 0;
  virtual u8 GetCollegeSystemRating() = 0;
  virtual i32 GetPatientCount() = 0;
  virtual i32 GetDoctorCount() = 0;
  virtual i32 GetSchoolStudentCount() = 0;
  virtual i32 GetTeacherCount() = 0;
  virtual i32 GetCollegeStudentCount() = 0;
  virtual i32 GetProfessorCount() = 0;
  virtual i32 GetHealthSystemCapacity() = 0;
  virtual i32 GetSchoolSystemCapacity() = 0;
  virtual i32 GetCollegeSystemCapacity() = 0;
  virtual i32 GetLibrarySystemCapacity() = 0;
  virtual i32 GetMuseumSystemCapacity() = 0;
  virtual u8 GetDevelopmentCap() = 0;
  virtual i32 GetPopulation() = 0;
  virtual f32 GetWorkforcePopPct() = 0;
  virtual f32 GetAgeCohortPopPct(u8, u8) = 0;
  virtual u8 GetWorkforceEQ() = 0;
  virtual u8 GetAgeCohortEQ(u8, u8) = 0;
  virtual u8 GetWorkforceLE() = 0;
  virtual u8 GetAgeCohortLE(u8, u8) = 0;
  virtual i32 GetTotalCityEducationUpkeep() = 0;
  virtual i32 GetTotalCityHealthUpkeep() = 0;
  virtual void SetEducationFundingPercentage(u8) = 0;
  virtual void SetHealthFundingPercentage(u8) = 0;
};

// Reached native effects view of SimTransit10006D17, primary slot96: DWORD coordinates, byte output and byte mask.
class ScOccupant;

// The installed traffic IID shares its city-layer prefix at0..68; this native grouping adds no subobject.
// Primary operations through188 follow the installed table; private nominal declarations remain qualified.
class NwTrafficLayer : public CtLayer {
public:
  // SimTransit primary72/1000706A is reached by SIMRCI landfill road access.
  virtual u8 RouteCellTrip(u32 x, u32 z, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) = 0;
  // SimTransit06FE3/slot76 has seven explicit arguments. Private nominal names/qualifiers remain unresolved.
  virtual u8 RouteOccupantTrip(ScOccupant *occupant, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) = 0;
  // Primary80/100015AD takes no arguments despite a caller's staged stack words.
  virtual u32 GetMaxDimension() = 0;
  virtual u8 GetRoadDistance() = 0;                                                // Primary84.
  virtual void GetModeVolume(u32 x, u32 z, u32 *out, u8 mode) = 0;                 // Primary88; reached effects.
  virtual u32 AverageVolume(u32 x0, u32 z0, u32 x1, u32 z1, u8 *out, u8 mask) = 0; // Primary92.
  virtual void GetVolume(u32 x, u32 z, u8 *volume, u8 mask) = 0;
  virtual void SumVolume(u32 x0, u32 z0, u32 x1, u32 z1, u32 *total, u32 *count, u8 mask) = 0; // Primary100.
  // SimTransit06965/slot104 preserves raw byte0/1/2; only exact1 permits the facility trip.
  virtual u8 IsTripAvailable(u32 x, u32 z) = 0;
  virtual void FillTripAvailability(u32 x0, u32 z0, u32 x1, u32 z1, u8 state) = 0; // Primary108.
  virtual void SetTripAvailability(u32 x, u32 z, u8 state) = 0;                    // Primary112.
  virtual u8 HasNetworkWithin(const u32 *point, i32 radius) = 0;                   // Primary116.
  // SimTransit primary120/0150F returns the stored f32 through x87; f64 is the native carrier.
  virtual f64 GetTrafficRatio() = 0;
  virtual u32 GetRailTotal() = 0;                                // Primary124/01516, raw DWORD188.
  virtual u32 GetRailUsage() = 0;                                // Primary128/0151D, raw DWORD192.
  virtual u8 HasBlockedTrip(u32 x0, u32 z0, u32 x1, u32 z1) = 0; // Primary132.
  virtual u8 SetFlag17(u8 value) = 0;                            // Primary136.
  virtual u8 GetFlag17() = 0;                                    // Primary140.
  virtual void ClearDensity() = 0;                               // Primary144; reached effects.
  virtual u32 GetCount220() = 0;                                 // Primary148.
  virtual u32 GetCount224() = 0;                                 // Primary152.
  virtual void SetRoadFundingPercentage(u8 value) = 0;           // Primary156/08D4B.
  virtual void SetRailFundingPercentage(u8 value) = 0;           // Primary160/08DBD; reached callers supply one byte.
  // SimTransit014EC/slot164 converts f32 whole+260 to signed64; historical method name remains unresolved.
  virtual i64 GetFareIncome() = 0;
  virtual i64 GetValue256() = 0;       // Primary168.
  virtual f64 GetRailMultiplier() = 0; // Primary172.
  virtual f64 GetRoadMultiplier() = 0; // Primary176.
  // Primary180/01502 materializes the predicate byte264==1; private nominal bool/BYTE remains unresolved.
  virtual u8 Flag108IsOne() = 0;
  // Primary184/089A3 native effects phase; its ignored private nominal return remains unresolved.
  virtual void BeginFlag108Phase() = 0;
  // Primary188/08968 returns low32 of _ftol or EAX0; EDX is undefined on the zero path.
  virtual i32 GetRailFundingDeficit() = 0;
  // Primary192/089EA enumerates the borrowed occupants associated with transit upkeep.
  virtual u8 EnumerateUpkeepOccupants(ScOccupant **out, u32 capacity, u32 *count) = 0;
};

/*
 * Interface subset adapted from sc3k-gzcom-dll cISC3ValveLayer.h.
 * Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.
 */

// Budget departments retain the identities used by their concrete funding variables and simulation callers.
enum EcDepartmentId : u32 {
  kEcEducationDepartmentId = 0x2922a5d,
  kEcHealthDepartmentId = 0x42922a6c,
  kEcPoliceDepartmentId = 0xa2963983,
  kEcFireDepartmentId = 0x62963928,
  kEcRoadDepartmentId = 0x8290f605,
  kEcRailDepartmentId = 0xc290f609,
};

class EcValve;

// cISC3ValveLayer IID40A42F1C: the installed SIMRCI primary table puts GetValvePointer at20 and AddValveToLayer at24.
// See reference-sc3kre/re/analysis/GZCOM_INTERFACE_CATALOGUE.md, cISC3ValveLayer vtable 1004CEE4.
class EcValveLayer : public ScUnknown {
public:
  virtual bool EndOfMonth() = 0;
  virtual bool CreateNewValve(u32, u32, void **) = 0;
  virtual bool GetValvePointer(u32, u32, void **) = 0;
  virtual bool AddValveToLayer(EcValve *valve) = 0;
  virtual i16 GetAgentSupplyEffect(u32, u32) = 0;
  virtual i16 GetAgentDemandEffect(u32, u32) = 0;
  virtual void GetDensity(u32, u32, u8 *) = 0;
  virtual u32 GetTaxableResidentialDensity() = 0;
  virtual u32 GetTaxableCommercialDensity() = 0;
  virtual u32 GetTaxableIndustrialDensity() = 0;
  virtual void DebugSetValve(ScStringView *, ScStringView *, ScStringView *) = 0;
};

// SDK constant; keep any verified Unlimited identity corrections explicit.
inline constexpr u32 kEcValveLayerType = 0x80f1e6d3;

struct GeCellRect;
class CtCity;
class GeBuildingGroupView;

// Reached native subset of SIMRCI's queried development-rule IID A1F3E1DB.
// Unrecovered intervening slots and private nominal return declarations are not asserted as a foreign vtable overlay.
class EcZoneRule : public ScUnknown {
public:
  virtual bool Init(CtCity *city) = 0;
  virtual u8 Shutdown() = 0;
  // Only the unused return spelling remains unresolved; the real group input is IID A1F592F9.
  virtual void AddConfig(GeBuildingGroupView *group) = 0;
  // The installed setter returns the same raw DWORD; its source signedness remains unresolved.
  virtual u32 SetMaxDistance(u32 distance) = 0;
  // The four original inputs are the candidate rectangle, growth identifier and two signed demand values.
  virtual u8 Place(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtAgentType;
class CtLayerManipulator;

// Original cISC3PollutionLayer is a separate city-map interface, not the primary implementation base.
// All mutable SDK references use explicit typed pointers as required by the reconstructed source convention.
class EcLayer : public ScDwordMap {
public:
  virtual bool Init(CtCity *city) = 0;
  virtual bool SimulationBegin() = 0;
  virtual bool GetManipulator(const CtAgentType &agent, CtLayerManipulator **manipulator, i32 *cost) = 0;
  virtual bool IsSaltWater(u32 x, u32 z) const = 0;
  virtual bool IsRadioactive(u32 x, u32 z) const = 0;
  virtual void GetAirValue(u32 x, u32 z, u32 *value) const = 0;
  virtual void GetWaterValue(u32 x, u32 z, u32 *value) const = 0;
  virtual void GetGarbageValue(u32 x, u32 z, u32 *value) const = 0;
  virtual bool IsAirPolluted(u32 x, u32 z) const = 0;
  virtual bool IsWaterPolluted(u32 x, u32 z) const = 0;
  virtual bool IsGarbageAccumulated(u32 x, u32 z) const = 0;
  virtual bool IsGarbageUncollected() = 0;
  virtual i32 GetTotalGarbageUncollected() = 0;
  virtual i32 GetAverageAirValue() const = 0;
  virtual i32 GetAverageWaterValue() const = 0;
  virtual i16 GetAverageGarbageValue() const = 0;
  virtual i32 GetMaxExpectedAirValue() const = 0;
  virtual i32 GetMaxExpectedWaterValue() const = 0;
  virtual i32 GetMaxExpectedGarbageValue() const = 0;
  virtual void SetIsSaltWater(u32 x, u32 z, bool enabled) = 0;
  virtual void SetIsRadioactive(u32 x, u32 z, bool enabled) = 0;
  virtual void SetAirValue(u32 x, u32 z, u32 value) = 0;
  virtual void SetWaterValue(u32 x, u32 z, u32 value) = 0;
  virtual void SetGarbageValue(u32 x, u32 z, u32 value) = 0;
  virtual i32 GetTotalLandfillCapacity() = 0;
  virtual i32 GetAvailableLandfillCapacity() = 0;
  virtual i32 GetDailyIncineratorCapacity() = 0;
  virtual i32 GetDailyWasteToEnergyCapacity() = 0;
  virtual i32 GetRecyclingCenterEffect() = 0;
  virtual i64 GetTotalGarbageProduced() = 0;
  virtual i64 GetTotalGarbageImported() = 0;
  virtual i64 GetTotalGarbageExported() = 0;
  virtual i64 GetTotalGarbageRecycled() = 0;
  virtual i64 GetTotalGarbageIncinerated() = 0;
  virtual i64 GetTotalGarbageConvertedToEnergy() = 0;
  virtual i64 GetTotalGarbageLandfilled() = 0;
  virtual i64 GetTotalGarbageProducedLastMonth() = 0;
  virtual i64 GetTotalGarbageImportedLastMonth() = 0;
  virtual i64 GetTotalGarbageExportedLastMonth() = 0;
  virtual i64 GetTotalGarbageRecycledLastMonth() = 0;
  virtual i64 GetTotalGarbageIncineratedLastMonth() = 0;
  virtual i64 GetTotalGarbageConvertedToEnergyLastMonth() = 0;
  virtual i64 GetTotalGarbageLandfilledLastMonth() = 0;
  virtual i32 GetGarbageBuildingCapacity(u32 kind) = 0;
  virtual i32 GetAgingWaterTreatmentCount() = 0;
  virtual i32 GetAgingIncineratorCount() = 0;
  virtual i32 GetAgingRecyclingCenterCount() = 0;
  virtual f32 GetGarbageScalingFactor() = 0;
  virtual bool IsActiveLandfill(u32 x, u32 z) = 0;
  virtual void SetIsActiveLandfill(u32 x, u32 z, bool enabled) = 0;
  virtual void GarbagePutInLandfill(u32 quantity) = 0;
};

#endif
