#ifndef SC3_CITY_API_H
#define SC3_CITY_API_H

#include <string.h>

#include "city/ct_layer.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_director.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "geometry/api.h"

// Byte-map adapters expand source bytes into their signed 32-bit map-value range using the truncated Q31 scale.
inline constexpr u32 kCtByteMapSint32Scale = 8421504u;

// Taxonomy keys from the installed SYS.PAK CitySim.ini [AgentTypes], distinct from COM class and interface IDs.
// Keep public u32 contracts unchanged; derived occupant types continue to use the real agent-tree ancestry queries.
enum CtAgentKind : u32 {
  kCtAgentOccupant = 50,
  kCtAgentBuilding = 100,
  kCtAgentFlora = 300,
  kCtAgentArcology = 1001,
  kCtAgentResidential = 3000,
  kCtAgentCommercial = 5000,
  kCtAgentIndustrial = 8000,
  kCtAgentCleanHighDensityIndustry = 9100,
  kCtAgentAgriculturalIndustry = 9300,
  kCtAgentGovernment = 10000,
  kCtAgentMayorsHouse = 10110,
  kCtAgentCityHall = 10120,
  kCtAgentMilitary = 11000,
  kCtAgentMilitaryBase = 11002,
  kCtAgentGarbage = 12005,
  kCtAgentPowerPlant = 12101,
  kCtAgentPowerNeighbor = 12102,
  kCtAgentNuclearPlant = 12115,
  kCtAgentBusStation = 12210,
  kCtAgentRailStation = 12230,
  kCtAgentAirport = 12310,
  kCtAgentSeaport = 12330,
  kCtAgentSpaceport = 12350,
  kCtAgentSubwayStation = 12360,
  kCtAgentSubwayRailStation = 12361,
  kCtAgentWaterTreatment = 12820,
  kCtAgentLandfill = 12850,
  kCtAgentRecycling = 12851,
  kCtAgentIncinerator = 12852,
  kCtAgentWasteToEnergy = 12853,
  kCtAgentCityStateServices = 13000,
  kCtAgentFireStation = 13002,
  kCtAgentPoliceStation = 13130,
  kCtAgentUniversity = 13870,
  kCtAgentJail = 13881,
  kCtAgentHistoricBuilding = 14160,
  kCtAgentMarina = 14430,
  kCtAgentZoo = 14450,  // SIMMISC zoo query 0x1000DA9B; evidence/SIMMISC/neighbors.tsv.
  kCtAgentRoad = 16000,
  kCtAgentHighway = 16001,
  kCtAgentBridgePart = 16002,
  kCtAgentGirderBridgePart = 16004,
  kCtAgentTunnel = 16006,
  kCtAgentRoadNeighbor = 16017,
  kCtAgentRail = 17000,
  kCtAgentRailEndcap = 17002,
  kCtAgentRailNeighbor = 17006,
  kCtAgentPowerLine = 17100,
  kCtAgentHighwayOffRamp = 17202,
  kCtAgentHighwayNeighbor = 17207,
  kCtAgentPipe = 17300,
  kCtAgentPipeNeighbor = 17310,
  kCtAgentSubway = 17400,
  kCtAgentSubwayNeighbor = 17410,
  kCtAgentMovableSubway = 17415,
  kCtAgentSubwayEndcap = 17420,
  kCtAgentLandmark = 18000,
  kCtAgentBusinessDeal = 19000,
  kCtAgentToxicWastePlant = 19001,
};

// Simulation-layer selectors returned by the concrete GetLayerType methods and consumed by GetSpecificLayer.
enum CtLayerType : u32 {
  kEcPollutionLayerType = 0xaaa1bb,
  kCtSimulatorLayerType = 0x7001,
  kGePortLayerType = 0x1fd7a8c,
  kAdUtilitiesAdvisorLayerType = 0x22e288e,
  kAdTransportationAdvisorLayerType = 0x28d0fc5,
  kNwTransitLayerType = 0x29ca804,
  kCtPlumbingSimulationLayerType = 0x2bf0033,
  kNwManagerLayerType = 0x2147c2dd,
  kTrDirtBagLayerType = 0x21737de5,
  kDsControllerLayerType = 0x21f6abca,
  kAdPetitionersLayerType = 0x22618fcc,
  kEcIndustrialLayerType = 0x4106cf1f,
  kCtWorldLayerType = 0x411bddda,
  kNwStreetLayerType = 0x412a2047,
  kEcCommercialLayerType = 0x60f1e6fb,
  kAdBudgetAdvisorLayerType = 0x628d0c45,
  kGeFloraControllerLayerType = 0x80ab8ab0,
  kCtPoliceLayerType = 0x80abf2be,
  kEcDemandLayerType = 0x80f1e6d3,
  kAdEnvironmentAdvisorLayerType = 0x828d04eb,
  kAdPublicSafetyAdvisorLayerType = 0x828d0a2f,
  kCtNeighborsLayerType = 0x82937b60,
  kGeDemolitionLayerType = 0x831c9087,
  kCtCrimeLayerType = 0xa0aa9dae,
  kGeBuildingControllerLayerType = 0xa0ab89f0,
  kEcBudgetLayerType = 0xa11bcc54,
  kEcZoneLayerType = 0xc0ab8a56,
  kEcLandValueLayerType = 0xc0ab8a88,
  kCtFireLayerType = 0xc0f42262,
  kEcResidentialLayerType = 0xc106c4f5,
  kCtFlammabilityLayerType = 0xc144aca9,
  kAdCityPlanningAdvisorLayerType = 0xc28d0b6e,
  kAdDemographicsAdvisorLayerType = 0xc28d0f40,
  kEnWeatherLayerType = 0xc2ed2a04,
  kCtScenarioLayerType = 0xc3de4d66,
  kCtPowerSimulationLayerType = 0xe0afdf68,
  kCtOrdinancesLayerType = 0xe1193c2a,
};

// Command-class identities dispatched by the original initialization factory.
enum CtInitCommandId : u32 {
  kCtShutdownCityCommandId = 0x010438e3,
  kCtLoadCityCommandId = 0x610438a4,
  kCtNewCityCommandId = 0xe3270fe9,
  kCtFileDialogCommandId = 0x310e8c85,
  kCtSaveCityCommandId = 0x410438dd,
};

// City-file picker modes passed through command values; the public transport remains u32.
enum CtFileDialogMode : u32 {
  kCtFileOpenCity = 0x83172ad7,
  kCtFileSaveCity = 0xc3172aea,
  kCtFileOpenTerrain = 0x23172af2,
  kCtFileOpenStarterTown = 0x63172b74,
  kCtFileOpenScenario = 0xa42c7725,
  kCtFileImportReplacementSet = 0xba2547ae,
};

// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.
/*
 * sc3k-gzcom-dll - an open-source DLL Plugin SDK for SimCity 3000
 *
 * cISC3CityAgent.h
 *
 * Copyright (C) 2025 Nicholas Hayes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, under
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see evidence/SIMCITY/sdk-license.txt.
 */

class ScStringView;
struct RsKey;

// Agent-tree type identities from the installed CitySim.ini hierarchy, separate from layer lookup IDs.
enum CtAgentTypeId : u32 {
  kCtAgentType_AreaAgent = 0x61190076,
  kCtAgentType_DisasterManager = 0x0288e4c8,
  kCtAgentType_DisasterInstanceDayTick = 0x2288e4eb,
  kCtAgentType_DisasterInstanceMinuteTick = 0xc3629afa,
  kCtAgentType_AuraLayerProxy = 0x0259c03f,
  kCtAgentType_WorldLayerProxy = 0x411bddda,
  kCtAgentType_NeighborsLayerProxy = 0x82937b60,
  kCtAgentType_FlammabilityLayerProxy = 0xc144aca9,
  kCtAgentType_PoliceAdvisorAgent = 0x413b4f54,
  kCtAgentType_BudgetAdvisorAgent = 0xa1536206,
  kCtAgentType_CityPlanningAdvisorAgent = 0x6292587c,
  kCtAgentType_DemographicsAdvisorAgent = 0x41576710,
  kCtAgentType_EnvironmentAdvisorAgent = 0xc1576708,
  kCtAgentType_PublicSafetyAdvisorAgent = 0xe1eb6d9a,
  kCtAgentType_TransportationAdvisorAgent = 0x82925329,
  kCtAgentType_UtilityAdvisorAgent = 0xc2505969,
  kCtAgentType_PetitionerManagerAgent = 0x22618fcc,
};

class CtAgentType;
class ScTreeNode;
class CtLayerManipulator;

// Original city simulation agent interface; mutable SDK references use explicit typed pointers.
class CtAgent : public ScUnknown {
public:
  virtual bool Simulate(i32 elapsed) = 0;
  virtual i32 MinimumExecutionInterval() = 0;
  virtual bool IsAgentLocking() = 0;
  virtual u32 TypeId() const = 0;
  virtual CtAgentType *AgentType() = 0;
  virtual bool SetAgentType(CtAgentType *type) = 0;
  virtual bool DebugInstanceTag(ScStringView *out) = 0;
  virtual bool DebugClassTag(ScStringView *out) = 0;
  virtual bool DebugTypeTag(ScStringView *out) = 0;
};

class CtAgentType : public ScUnknown {
public:
  virtual ScTreeNode *AsIGZTreeNode() = 0;
  virtual u32 Id() const = 0;
  virtual bool AttachManipulator(CtLayerManipulator *manipulator, i32 flags) = 0;
  virtual bool ReleaseManipulator(CtLayerManipulator *manipulator, i32 flags) = 0;
  virtual void ReleaseAllManipulators() = 0;
  virtual bool Simulate(CtAgent *agent, i32 elapsed) = 0;
  virtual i32 MinimumExecutionInterval() = 0;
  virtual CtAgentType *Parent() const = 0;
  virtual CtAgentType *FirstChild() const = 0;
  virtual CtAgentType *Sibling() const = 0;
  virtual void GetName(RsKey *out) = 0;
  virtual bool SetId(u32 id) = 0;
  virtual bool SetName(const RsKey &key) = 0;
  virtual bool SetParent(CtAgentType *parent) = 0;
  virtual bool SetFirstChild(CtAgentType *child) = 0;
  virtual bool SetSibling(CtAgentType *sibling) = 0;
  virtual bool DebugInstanceTag(ScStringView *out) = 0;
  virtual void DebugSetInstanceTag(ScStringView *tag) = 0;
  virtual bool DebugClassTag(ScStringView *out) = 0;
  virtual bool DebugTypeTag(ScStringView *out) = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// Interface declaration adapted from sc3k-gzcom-dll cISC3AgentTypeTree.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.

class CtAgentTreeIterator;

// Keep the complete installed virtual order. MSVC reverses the two ancestry overloads, so GetNode is +28,
// the distance overload is +32 and the two-argument IsAncestor is +36. Both node getters borrow agent-type views.
class CtAgentTree : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual u32 Count() = 0;
  virtual CtAgentType *GetRootNode() = 0;
  virtual CtAgentType *GetNode(u32 id) = 0;
  virtual bool IsAncestor(u32 ancestor, u32 descendant) = 0;
  virtual bool IsAncestor(u32, u32, u32 *) = 0;
  virtual bool CreateNodeIterator(CtAgentTreeIterator **out) = 0;
  virtual void DebugDumpTree(CtAgentType *) = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3AuraLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class CtAuraLayer : public ScCellMap<i8> {

public:
  virtual i8 GetMayorRating() = 0;
};

// SDK constant; keep any verified Unlimited identity corrections explicit.
inline constexpr u32 kCtAuraLayerType = 0x259c03f;

// Interface declaration adapted from sc3k-gzcom-dll cISC3CityChangeReceiver.h.
// Copyright (C) 2026 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.

class ScOccupant;
struct CtCoord;

// Original city-change receiver interface, SDK cISC3CityChangeReceiver.h:33-39.
// Pointer/reference widths and overload ordering agree with the installed SIMECO receiver table.
class CtChangeReceiver : public ScUnknown {
public:
  virtual bool OccupantInserted(u32 sender, ScOccupant *occupant) = 0;
  virtual bool OccupantRemoved(u32 sender, ScOccupant *occupant) = 0;
  virtual bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) = 0;
  virtual bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) = 0;
  virtual bool CellChangedAll(u32 sender) = 0;
  virtual bool CellChanged(u32 sender, u32 x, u32 z) = 0;
  virtual bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) = 0;
};

class CtChangeReceiver;

// Shared IIDC171B663 acquired from terrain, zone and cursor providers. The historical interface spelling is unknown;
// these are the actual source operations, separate from TrChangeSource's concrete receiver-vector ownership.
class CtChangeSource : public ScUnknown {
public:
  virtual u32 GetID() const = 0;
  virtual bool AddReceiver(CtChangeReceiver *receiver, u32 *sender) = 0;
  virtual bool RemoveReceiver(CtChangeReceiver *receiver) = 0;
  virtual bool RemoveAll() = 0;
  virtual void SetEnabled(u8 enabled) = 0;
  virtual u8 GetEnabled() const = 0;
  virtual bool NotifyAll() = 0;
  virtual bool Notify(u32 x, u32 z) = 0;
  virtual bool Notify(u32 left, u32 top, u32 right, u32 bottom) = 0;
};

// Prefix declarations adapted from cISC3CityCellMapBase.h/cISC3CityCellMap.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

class RsStream;
struct CtCoord;

// Actual IIDA0AFDF5D power identity at SIMUTIL receiver+20 extends the Boolean city-map interface.
// Linux constructor322C4 installs both map-base vtables before cISC3PowerLayer; installed PC slots agree.
class CtPowerLayer : public ScCellMap<bool> {
public:
  // Native effects/word projections; private nominal returns remain unresolved. Names and inputs have Linux symbols.
  virtual void UpdateLayer() = 0;                            // Original68, PC04C1F.
  virtual u8 GetPlantCapacity(u32 type, u32 *capacity) = 0;  // Original72, PC038B7; raw AL0/1.
  virtual u32 GetTotalAnnualConsumption() = 0;               // Original76, PC038E9.
  virtual u32 GetAnnualConsumptionOfPowerType(u32 type) = 0; // Original80, PC0380F.
  virtual u32 GetTotalCityPowerUsed() = 0;                   // Original84, PC1B5D6.
  virtual u32 GetTotalCityPowerProduced() = 0;               // Original88, PC033C6.
  virtual u32 GetTotalPowerNeededByTheCity() = 0;            // Original92, PC038FD.
  virtual u32 GetNumberOfUnpoweredConsumers() = 0;           // Original96, PC038B0.
  virtual u32 GetNumberOfPoweredConsumers() = 0;             // Original100, PC038A9.
  virtual u32 GetNumberOfAgingPowerPlants() = 0;             // Original104, PC033CA.
  virtual u32 GetNumberOfAgingNuclearPlants() = 0;           // Original108, PC033D1.
};

// IIDC2BF0039 is the real primary CtLayer identity. The first private operation tests the water grid.
// Windows reverses the coordinate overloads: coordinate inputs at72 and the coordinate object at76.
class CtPlumbingLayer : public CtLayer {
public:
  virtual u8 IsWatered(const CtCoord &position) const = 0; // Emitted76, PC0DEE7.
  virtual u8 IsWatered(u32 x, u32 z) const = 0;            // Emitted72, PC0DF02; private bool/BYTE spelling unresolved.
  // Native effects projection: Linux GetWaterData RUiRxT2T1T1 confirms five genuine output parameters.
  virtual void GetWaterData(u32 *consumers, i64 *capacity, i64 *used, u32 *first, u32 *second) = 0; // PC0F066,+80.
  virtual u32 GetProductionCapacity(u32 type) = 0;   // PC100BB,+84; source nominal signedness unresolved.
  virtual u32 GetNumPumpsAging() = 0;                // PC0C7BC,+88; source nominal signedness unresolved.
  virtual u32 GetAverageWaterPollutionInPumps() = 0; // PC0F6E1,+92; nominal signedness unresolved.
  // Linux GetDiffMap(unsigned int *, unsigned int) establishes both parameter types; nominal bool/byte is unresolved.
  virtual u8 GetDiffMap(u32 *out, u32 count) = 0;
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3CrimeLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class CtCrimeLayer : public ScCellMap<u8> {

public:
  virtual u8 GetAverageValue() = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtAgentTree;

// Reached cISC3CityDefinition subset; the complete SIMINIT provider remains a separate implementation dependency.
class CtDefinition : public ScUnknown {
public:
  // Retail and application406323/40632E consume Init12, Shutdown16, tree20 and cache24.
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual CtAgentTree *GetAgentTypeTree() = 0;
  virtual OcAttribCache *GetOccupantAttribCache() = 0;
  virtual bool RegisterLayer(u32, u32, u32) = 0;
  virtual u32 GetFileType(ScStringView const &) = 0;
  virtual bool ValidateSC2File(ScStringView const &) = 0;
  virtual bool ValidateSCXFile(ScStringView const &) = 0;
  virtual bool ValidateSC3File(ScStringView const &) = 0;
  virtual bool ValidateSNRFile(ScStringView const &) = 0;
};

class ScUnknown;

// Original registered constructors have no arguments and return an unretained Unknown-compatible interface.
// The agent-type registration returns its secondary node identity; private nominal factory declarations are unresolved.
ScUnknown *CtCreateSimulator();
ScUnknown *CtCreateCity();
ScUnknown *CtCreateAgentType();
ScUnknown *CtCreateAgent();

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3FireLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class CtFireLayer : public ScCellMap<u8> {

public:
  virtual bool DispatchCrew(u32, u32) = 0;
  virtual bool RevokeDispatch(u32, u32) = 0;
  virtual bool RevokeAllDispatches() = 0;

  virtual u32 GetTotalCityFireUpkeep() = 0;
  virtual void SetFundingPercentage(u8) = 0;
  virtual bool IsOnStrike() = 0;
  virtual u32 ChanceOfStrike() = 0;
  virtual void EndStrike() = 0;
  virtual bool GetStrikingBuildings(ScOccupant **, u32, u32 *) = 0;

  virtual u32 GetAverageCoverage() = 0;
  virtual u32 GetPercentCoverage() = 0;
};

// Interface declaration adapted from sc3k-gzcom-dll cISC3LandValueLayer.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.

// Actual city land-value interface IID6098556B, returned from SIMRCI1001D748 secondary+28.
// IID4259C018 is the separate aura interface and must not be queried into this type.
class CtLandValueLayer : public ScCellMap<u8> {
public:
  virtual u8 GetAverageValue() = 0; // SDK :28; installed +68.
};

// Native grouping of the verified same-address CityLayer prefix and reached neighbor operations (IID c2910e7d).
// Original source hierarchy/names are unavailable; count names are descriptive and their formal signedness is unresolved.
struct CtDealRecord;
class CtDateInterface;
class CtAgent;

// Complete generic deal interface from SIMMISC table40E08; setters and copy preserve the original record slots.
// Slot28 getter1937B zero-extends the neighbor byte to a full DWORD consumed by the original caller.
// Private numeric names and nominal setter return spellings retain the existing qualified raw carriers.
class CtDeal : public ScUnknown {
public:
  virtual u8 GetFirstName(ScStringView *out) = 0;  // Original +12.
  virtual u8 GetSecondName(ScStringView *out) = 0; // Original +16.
  virtual u32 GetValue08() = 0;                    // Original +20; full DWORD, nominal signedness unresolved.
  virtual u32 GetValue0c() = 0;                    // Original +24.
  virtual u32 GetNeighborId() = 0;                 // Original +28; zero-extended byte.
  virtual u32 GetType() = 0;                       // Original +32; zero-extended byte.
  virtual u32 GetValue14() = 0;                    // Original +36.
  virtual u32 GetValue18() = 0;                    // Original +40.
  virtual u32 GetValue1c() = 0;                    // Original +44.
  virtual u32 GetValue20() = 0;                    // Original +48.
  virtual u8 GetDate(CtDateInterface *out) = 0;    // Original +52.
  virtual u8 SetFirstName(const ScStringView &text) = 0;
  virtual u8 SetSecondName(const ScStringView &text) = 0;
  virtual u32 SetValue08(u32 value) = 0;
  virtual u32 SetValue0c(u32 value) = 0;
  virtual u8 SetNeighborId(u8 value) = 0;
  virtual u8 SetType(u8 value) = 0;
  virtual u32 SetValue14(u32 value) = 0; // SIMMISC +80,1001947C.
  virtual u32 SetValue18(u32 value) = 0;
  virtual u32 SetValue1c(u32 value) = 0;
  virtual u32 SetValue20(u32 value) = 0;
  virtual u8 SetDate(const CtDateInterface &value) = 0;
  virtual u8 CopyFrom(CtDeal *source) = 0;     // SIMMISC +100,100194BF.
  virtual u8 GetRecord(CtDealRecord *out) = 0; // SIMMISC +104,10019242; borrowed names and original40-byte record.
  virtual u8 SetRecord(const CtDealRecord *record) = 0;
};

// Utility deals use the city perspective; the six description strings establish import/export direction.
enum CtNeighborDealType : u8 {
  kCtDealImportPower = 0,
  kCtDealExportPower = 1,
  kCtDealImportWater = 2,
  kCtDealExportWater = 3,
  kCtDealExportGarbage = 4,
  kCtDealImportGarbage = 5,
};

// Installed SIMMISC primary table40BF4 extends the original CityLayer prefix at slots72..260.
// Private BOOL-versus-BYTE spellings retain their qualified raw returns where source evidence is incomplete.
class CtNeighborsLayer : public CtLayer {
public:
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual bool InitializeNeighbors() = 0;
  virtual u8 AddConnection(i32 networkType, i32 x, i32 z) = 0;
  virtual u8 RemoveConnection(i32 networkType, i32 x, i32 z) = 0;
  virtual bool HasConnection(i32 networkType, i32 x, i32 z) = 0;
  virtual u8 ConnectionSide(i32 x, i32 z) = 0;
  virtual bool HasSideConnection(i32 networkType, u8 side) = 0;
  virtual u8 MakeOffer(u8 type) = 0;
  virtual u8 ComputeOffer(u8 neighbor, u8 type, u32 maximum) = 0;
  virtual u8 GetPendingDeal(u8 type, CtDealRecord *out) = 0;
  virtual bool GetPendingDealObject(u32 type, CtDeal **out) = 0;
  virtual u8 RemovePendingDeal(u8 type) = 0;
  virtual u8 AcceptPendingDeal(u8 type) = 0;
  virtual u8 RemoveDeal(u8 type) = 0;
  virtual u8 CancelDeal(u8 type) = 0;
  virtual bool HasDealType(u8 type) = 0;
  virtual bool HasPendingDealType(u8 type) = 0;
  virtual u8 GetDeal(u8 type, CtDealRecord *out) = 0;
  virtual bool GetDealObject(u32 type, CtDeal **out) = 0;
  virtual u8 GetCanceledDeal(CtDeal **out) = 0;
  virtual u8 SelectNeighbor(u8 type) = 0;
  virtual bool CanDeal(u8 neighbor, u8 type) = 0;
  virtual u8 PublishDeal(u8 type, i32 amount) = 0;
  virtual u8 NeighborFlag(u8 index) = 0;
  virtual u32 CountNeighbors() = 0;
  virtual u32 GetNeighborPopulation(u8 index) = 0;
  virtual bool GetNeighborName(u8 index, ScStringView *out) = 0;
  virtual u8 SetNeighborName(u8 index, const ScStringView &name) = 0;
  virtual u8 GetNeighborMayorName(u8 index, ScStringView *out) = 0;
  virtual u32 GetRailConnections() = 0;
  virtual u32 GetRoadConnections() = 0;
  virtual u32 CountNeighborsWithField140() = 0;
  virtual u32 CountNeighborsWithField144() = 0;
  virtual u32 CountNeighborsWithDeals() = 0;
  virtual u32 GetHighwayConnections() = 0;
  virtual u32 GetSubwayConnections() = 0;
  virtual u32 GetTotalRailConnections() = 0;
  virtual u32 GetTotalRoadConnections() = 0;
  virtual u32 GetTotalWaterConnections() = 0;
  virtual u32 GetTotalPowerConnections() = 0;
  virtual u32 GetTotalGarbageConnections() = 0;
  virtual u32 GetTotalHighwayConnections() = 0;
  virtual u32 GetTotalSubwayConnections() = 0;
  virtual i32 GetTotalMonthlyNeighborDealIncome() = 0;
  virtual i32 GetTotalMonthlyNeighborDealExpense() = 0;
  virtual i32 GetNeighborDealPenaltyExpense() = 0;
  virtual u32 GetNumberOfNeighborDeals() = 0;
  virtual u8 GetNeighborDeals(u8 *count, CtDealRecord *out) = 0;
};

// Interface declarations adapted from sc3k-gzcom-dll cISC3OrdinanceLayer.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.

class ScStringView;

// Reached IID41193C3A ordinance operations, layerE1193C2A, in their established original slot order.
// This partial native interface is not a foreign vtable overlay.
class CtOrdinanceLayer : public ScUnknown {
public:
  // Original16-byte x86 list row. Name is borrowed; on is a raw byte and cost retains its complete DWORD bits.
  struct CtOrdinance {
    u32 id;
    ScStringView *name;
    u8 on;
    u32 monthly_cost;
  };

  virtual bool QueryOrdinanceOn(u32 id) = 0;                          // SDK :36; installed +12.
  virtual bool QueryOrdinanceAvailable(u32 id) = 0;                   // SDK :37; installed +16.
  virtual bool QueryOrdinanceAvailableAndOff(u32 id) = 0;             // SDK :38; installed +20.
  virtual u32 TotalOrdinancesOn() = 0;                                // Installed +24.
  virtual bool SetOrdinance(u32 id, bool on) = 0;                     // SDK :41; installed +28.
  virtual bool MakeOrdinanceAvailable(u32 id) = 0;                    // Installed +32.
  virtual u32 GetOrdinanceListSize(u32 category) = 0;                 // Installed +36.
  virtual u32 GetOridnanceListSize(i32 category) = 0;                 // Installed +40; SDK signed spelling retained.
  virtual bool GetOrdinanceList(CtOrdinance **out, u32 category) = 0; // Installed +44; AL success, no count return.
  virtual u32 GetOrdinanceCost(u32 id) = 0;                           // Installed +48; complete cost bits.
  virtual u32 GetTotalMonthlyOrdinanceExpense() = 0;                  // Installed +52.
  virtual u32 GetTotalMonthlyOrdinanceIncome() = 0;                   // Installed +56.
};

// Copyright (C) 2026 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3PoliceLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class CtPoliceLayer : public ScCellMap<u8> {

public:
  virtual bool DispatchCrew(u32, u32) = 0;
  virtual bool RevokeDispatch(u32, u32) = 0;
  virtual bool RevokeAllDispatches() = 0;

  virtual u32 GetTotalCityPoliceUpkeep() = 0;
  virtual void SetFundingPercentage(u8) = 0;
  virtual bool IsOnStrike() = 0;
  virtual u32 ChanceOfStrike() = 0;
  virtual void EndStrike() = 0;
  virtual bool GetStrikingBuildings(ScOccupant **, u32, u32 *) = 0;

  virtual u32 GetAverageCoverage() = 0;
  virtual u32 GetPercentCoverage() = 0;
  virtual u32 GetTotalJailCapacity() = 0;
  virtual u32 GetInmatePopulation() = 0;
  virtual u32 GetCriminalsAtLarge() = 0;
};

// Copyright (C) 2024 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class RsDatabaseSegment;

// SDK table order; SIMINIT and SIMDIRT calls corroborate Shutdown and palette slots16, 52, 64 and 68.
class CtSchemeManager : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual BdPluginManager *GetPluginBuildingMgr() = 0;
  virtual bool SetCityScheme(u32, u32, u32, bool) = 0;
  virtual void SetDefaultCityScheme() = 0;
  virtual bool GetCurrentLandScape(u32 *out) const = 0;
  virtual u32 GetDefaultLandScape() const = 0;
  virtual u32 GetNumLandScapes() const = 0;
  virtual void GetLandScapeIds(u32 *, u32, u32 *) = 0;
  virtual bool GetLandScapeIconKey(u32, RsKey *) = 0;
  virtual bool GetLandScapePalette(u32 selection, u32 *group, u32 *instance) = 0;
  virtual bool GetUiImageForLandScape(u32, u32 *, u32 *) = 0;
  virtual bool GetSoundIdForCurrentLandScape(u32 *) = 0;
  virtual void GetNavViewTerrainPaletteForCurrentLandScape(u32 *out) = 0;
  virtual void GetDataMapsTerrainPaletteForCurrentLandScape(u32 *) = 0;
  virtual bool GetCurrentFloraSet(u32 *out) const = 0;
  virtual u32 GetDefaultFloraSet() const = 0;
  virtual u32 GetNumFloraSets() const = 0;
  virtual void GetFloraSetIds(u32 *, u32, u32 *) = 0;
  virtual bool GetFloraSetIconKey(u32, RsKey *) = 0;
  virtual bool GetUiImageForFloraSet(u32, u32 *, u32 *, u32 *, u32 *) = 0;
  virtual bool GetSoundIdForCurrentFloraSet(u32 *) = 0;
  virtual bool GetCurrentBuildingSet(u32 *out) const = 0;
  virtual u32 GetDefaultBuildingSet() const = 0;
  virtual u32 GetNumBuildingSets() const = 0;
  virtual void GetBuildingSetIds(u32 *, u32, u32 *) = 0;
  virtual bool GetBuildingSetIconKey(u32, RsKey *) = 0;
  virtual bool GetUiImageForBuildingSet(u32, u32 *, u32 *, u32 *, u32 *) = 0;
  virtual bool GetSoundIdForCurrentBuildingSet(u32 *) = 0;
  virtual void LoadBATSegment() = 0;
  virtual void UnloadBATSegment() = 0;
  virtual u32 GetNumBATBuildings() = 0;
  virtual void GetBATBuildingKeys(RsKey *, u32, u32 *) = 0;
  virtual void UpdateBATSegment(u32, ScStringView **, u32, RsKey *) = 0;
  virtual bool IsRewardBuilding(u32) = 0;
  virtual bool IsReplaceableBuilding(GeAttrib const *) = 0;
  virtual void GetBATBuildings(OcOverrideContainer *, u32) = 0;
  virtual iptr GetBATBuildings() = 0;
  virtual bool MakeBATReplacement(RsKey const &, RsKey const &) = 0;
  virtual bool MakeBATReplacement(RsKey const &, OcOverride *) = 0;
  virtual bool ClearBATReplacement(RsKey const &) = 0;
  virtual bool LoadBATReplacementSet(RsDatabaseSegment *, bool) = 0;
  virtual bool SaveBATReplacementSet(RsDatabaseSegment *segment) = 0;
  virtual bool ImportReplacementSet(ScStringView *) = 0;
  virtual void ClearReplacementSet() = 0;
};

class ScDirector;

// Original SIMSERV accessor103FB returns its borrowed CtServiceDirector; unrecovered factory products remain explicit.
ScDirector *CtGetServiceDirector();

class ScFramework;
class ApGameInterface;
class ApBoxX;
class CtCity;
class CtAgentTree;
class RsManager;
class OcAttribCache;
class GxGraphics;
class WnResources;
class ScLanguageManager;
class ScMessageServer;

// Preserve the original magic-add rounding used by fire and police coverage updates.
inline u32 CtServiceRoundCoverage(f64 value) {
  volatile f64 addition = value + kScDoubleIntegerBias;
  f64 rounded = addition;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

namespace CtService {
ScFramework *CtGetFramework();
ApGameInterface *CtGetApplication();
CtCity *CtGetCity();
// Original city-presence helper returns zero or one; its nominal bool/BOOL declaration is unresolved.
u8 CtHasCity();
CtAgentTree *CtGetAgentTree();
OcAttribCache *CtGetOccupantAttribCache();
GxGraphics *CtGetGraphics();
WnResources *CtGetWindowResources();
ApBoxX *CtGetBoxX();
RsManager *CtGetResourceManager();
ScLanguageManager *CtGetLanguageManager();
ScMessageServer *CtGetMessageServer();
} // namespace CtService

class ScMessageServer;
class ScCommandFactory;
class ScQueueManager;
class ApGameInterface;
class ApPreferences;
class CtCity;
class ApBoxX;
class ScLanguageManager;
class CtAgentTree;
class CtLayer;
class CtDefinition;
class RsManager;
class GxGraphics;
class WnWindowManager;
class RsDatabaseSegment;
class RsImageOptions;
class SpCityWindowView;
class SpViewInterface;
class SpTickManager;

// Borrow the city director without binding another module's similarly named DLL entry.
ScDirector *CtGetDirector();
ScFramework *CtGetFramework();
ScCom *CtGetCom();
ScCommandFactory *CtGetCommandCatalog();
ScFramework *CtGetThreadFramework();
ScMessageServer *CtGetMessageServer();
ScQueueManager *CtGetQueueManager();
ApGameInterface *CtGetGameApp();
CtCity *CtGetCity();
ApPreferences *CtGetPreferences();
ApBoxX *CtGetBoxX();
SpCityWindowView *CtGetCityWindow();
i32 CtHasCityWindow();
RsImageOptions *CtGetBufferResourceFactory();
SpViewInterface *CtGetCurrentView();
SpTickManager *CtGetTickManager();
CtAgentTree *CtGetAgentTree();
ScLanguageManager *CtGetLanguageManager();
RsManager *CtGetResourceManager();
GxGraphics *CtGetGraphics();
WnWindowManager *CtGetWindowManager();
u32 SC3_STDCALL CtReadCityType(RsDatabaseSegment *segment);
u32 SC3_STDCALL CtRequestDialog(u32 textID);
bool SC3_STDCALL CtAcquireLayer(u32 classID, CtLayer **common, u32 iid, void **primary);
extern CtDefinition *CtCityDefinition;
extern bool CtCityStaticReady;
extern u32 CtCityStaticLayerCount;

// Interface declarations adapted from sc3k-gzcom-dll cICSCitySimulator.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See evidence/SIMCITY/sdk-license.txt.

class CtCity;
class CtAgent;
struct ScMessage;

// Original simulator interface; the worker interface is a distinct secondary base on its concrete receiver.
class CtSimulatorInterface : public ScUnknown {
public:
  virtual bool Init(CtCity *city) = 0;
  virtual bool Shutdown() = 0;
  virtual void OnIdle() = 0;
  virtual ScMessageQueue *MessageQueue() = 0;
  virtual bool ProcessMessage(ScMessage *message) = 0;
  virtual bool CommandStart(ScMessage *message) = 0;
  virtual bool CommandStop(ScMessage *message) = 0;
  virtual bool AddAgent(CtAgent *agent) = 0;
  virtual bool RemoveAgent(CtAgent *agent) = 0;
  virtual u32 GetLayerType() = 0;
  virtual bool DoSimImmediatePause() = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISSStrtSimLayer.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

class NwRiotCrowd;
class NwParadeBlock;

// The original cISSStrtSimLayer extends the city-layer/message/query primary without adding another identity.
// Linux constructor63040 installs those four base vtables before the street-layer interface; PC table1002C600 agrees.
class CtStartSimLayer : public CtLayer {

public:
  // Preserve the original slot72 instead of grouping this operation with MSVC's city Init overloads.
  virtual bool InitLayer() = 0;

  virtual bool SetNumZoomLevelsPeopleAreVisible(i32) = 0;
  virtual bool SetNumZoomLevelsTrafficVisible(i32) = 0;

  virtual iptr DispatchAirplane(u32, u32) = 0; // Returns cSSAeroplane*
  virtual bool RedirectAirplane(ScOccupant *, u32, u32) = 0;
  virtual void RevokeDispatchedAirplane(ScOccupant *) = 0;
  virtual iptr DispatchHelicopter(u32, u32, u32, u32) = 0; // Returns cSSHelicopter*
  virtual void RevokeDispatchedHelicopter(ScOccupant *) = 0;

  virtual NwRiotCrowd *CreateRiotCrowd() = 0;                 // Original104 returns cISSRiotCrowd, complete+348.
  virtual NwParadeBlock *CreateParadeBlock(u32 resource) = 0; // Original108 returns cISSParadeBlock, complete+348.
  virtual void CreateParadeSpectators(u32, u32, u32) = 0;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class EcZoneRule;

// The ordered operations are established by developer vtables and the zoning caller, not by the incomplete SDK.
class CtZoneDeveloper : public ScUnknown {
public:
  virtual u8 Init(CtCity *city, const char *growth_data) = 0;
  virtual u8 Shutdown() = 0;
  virtual bool BindDemand() = 0;
  virtual i32 Evaluate(const ScPointXZ &point) = 0;
  virtual u8 AddRule(EcZoneRule *rule) = 0;
  virtual u8 SerialRead(CtCity *city, RsStream *record) = 0;
  virtual u8 SerialWrite(CtCity *city, RsStream *record) = 0;
};

// Separate SIMRCI view23E15A6D at complete+4, table4C554; slot0 getter19774 returns the stored float.
// This view has no Unknown prefix. Its primary developer owns the acquisition reference.
class CtLandfillDecay {
public:
  virtual f32 GetDecayRate() = 0;
};

// Byte specialization of the SDK cell-map interface, shared by zone and animation maps.
// The generic interface describes the original virtual contract without a template container implementation.
class ScByteMap : public ScCellMap<u8> {
public:
  virtual void GetValue(u32 x, u32 z, u8 *value) const = 0;
};

// City getter332 supplies IID80902C70 to both network and pollution consumers. SDK cISC3ZoneLayer has this map base.
class CtZoneLayer : public ScByteMap {
public:
  // Remaining SDK operations are declarations only; no fabricated implementation defaults.
  virtual u32 GetZoneCount(u8) = 0;            // SIMRCI primary68/32C44, total typed tiles.
  virtual u32 GetUndevelopedTileCount() = 0;   // Original76/312A7; MSVC reverses this overload group.
  virtual u32 GetUndevelopedTileCount(u8) = 0; // Original72/32C5B, typed undeveloped tiles.
  virtual u32 GetDevelopmentFailureCount(u8, i32) = 0;
  virtual u32 GetAbandonedTileCount() = 0;
  virtual bool RegisterZoneDeveloper(u8, CtZoneDeveloper *, u32) = 0;
  virtual bool UnregisterZoneDeveloper(u8) = 0;
  virtual CtZoneDeveloper *GetZoneDeveloper(u8) = 0;
  virtual bool PlaceBuilding(GeBuildingType *, ScPointXZ const &, i32, u16, bool) = 0;
  virtual bool PlaceBuilding(RsKey const &, ScPointXZ const &, i32, u16, bool) = 0;
  virtual bool IsNearTransport(ScPointXZ const &, u32) = 0;
  virtual u32 GetRoadCount(ScPointXZ const &, ScPointXZ const &) = 0;
  virtual void FindBuildableRect(GeCellRect *, u32, u32, u32, u8, u32, bool) = 0;
  virtual u32 GetFilledDemand(u32) = 0;
  virtual void AddToFilledDemand(u32, i32) = 0;
  virtual bool GetZoneColor(u32, u32, u8 *, u16 *) = 0;
  virtual bool CanZone(i32, GeBounds const &, i32 *) = 0;
  virtual bool PlaceZone(i32, GeBounds const &, i32 *, bool) = 0;
  virtual bool NerdsRule() = 0;
  virtual void ReadZoneDeveloperDescriptions(bool) = 0;
};

class ScStringView;
struct CtMicroRecord;
struct CtThingRecord;

// Retail cISC2Importer and the installed SIMINIT table have the same Unknown prefix and ordered byte-return methods.
class ScImporter : public ScUnknown {
public:
  virtual u8 Init(const ScStringView &path) = 0;
  virtual u8 GetBaseMapSize(u32 *countX, u32 *countZ, u32 *countY) = 0;
  virtual u8 MapIsPresent(u32 plane) = 0;
  virtual u8 GetValue(i32 plane, u32 x, u32 z, u8 *cell) = 0;
  virtual u8 GetZoneValue(u32 x, u32 z, u8 *zone) = 0;
  virtual u8 GetFaceValue(u32 x, u32 z, u8 *direction) = 0;
  virtual u16 GetMapSize(i32 plane) = 0;
  virtual u8 GetMiscValue(u32 index, i32 *value) = 0;
  virtual u8 GetAltValue(u32 x, u32 z, u16 *out) = 0;
  virtual u8 GetTerrainAlt(u32 x, u32 z, u8 *altitude) = 0;
  virtual u8 GetWaterAlt(u32 x, u32 z, u8 *out) = 0;
  virtual u8 GetTunnelDepth(u32 x, u32 z, u8 *out) = 0;
  virtual u8 GetCityName(char *out) = 0;
  virtual u8 GetLabel(u32 index, char *out) = 0;
  virtual u8 GetMicroRecord(u32 index, CtMicroRecord *out) = 0;
  virtual u8 GetThingRecord(u32 index, CtThingRecord *out) = 0;
  virtual u8 GetGraphValue(i32 category, u8 scale, u8 index, u32 *out) = 0;
};

// Copyright (C) 2024, 2025, 2026 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Unspecified city-coordinate components use the original signed maximum sentinel.
inline constexpr i32 kCtUnknownCoordinate = INT32_MAX;

// City micro coordinates retain eight fractional bits per cell.
inline constexpr u32 kCtMicroCoordinateShift = 8;
inline constexpr i32 kCtMicroUnitsPerCell = 256;
// Clear the eight fractional micro-coordinate bits to select the containing whole cell.
inline constexpr u32 kCtMicroCellMask = 0xffffff00;

struct CtCoord {
  i32 x, z, altitude;
};
class ScOccupantManager;
// Retail cISTTransitLayer (type 2A10) is forward-only; no unproven base list or virtual suffix is asserted here.
class NwTransit;
class ScLock;
class ScDirtBag;
class CtClockInterface;
class AdPetitionerManager;
class CtNeighborsLayer;
class EcLayer;
class EcBudgetLayer;
class CtOrdinanceLayer;
class CtSimulatorInterface;
class GeFlora;
class GeBuildingLayer;
class CtZoneLayer;
class CtResidentialLayer;
class NwTrafficLayer;
class TrGeneratorInterface;
class ScStringView;
class CtLayer;
class CtScenarioLayer;
class AdUtilities;
class AdDemographics;
class AdPublicSafety;
class AdEnvironment;
class AdCityPlanning;
class AdTransportation;
class AdBudget;

// City mode bits select the original simulation-speed and UI transitions. Public methods retain raw DWORD masks.
enum CtMode : u32 {
  kCtModeDisaster = 1,
  kCtModeScenario = 2,
  kCtModeTutorial = 4,
  kCtModeDemo = 8,
};

// Retail cISC3City23F4 derives cISC3QueryInfo29AF, then cIGZMessageTarget15D3 and cIGZUnknown11DC.
// The SDK declaration order agrees with the installed calls, including EndCitySimulation at slot480.
class CtCity : public ScQueryTarget {
public:
  bool DoMessage(ScMessage *message) override = 0;
  bool DoQueryInfo(ScMessage *, ScUnknown *) override = 0;
  virtual bool Init(CtNewCityInfo *) = 0;
  virtual bool Init(ScImporter *) = 0;
  virtual bool Init(RsDatabaseSegment *) = 0;
  virtual bool Save(RsDatabaseSegment *) = 0;
  virtual bool Shutdown() = 0;
  virtual i32 GetEditMode() = 0;
  virtual void SetEditMode(i32) = 0;
  virtual bool IsDisasterActive() = 0;
  virtual bool IsScenarioActive() = 0;
  virtual bool IsTutorialActive() = 0;
  virtual bool IsDemoActive() = 0;
  virtual bool AreHolidayFeaturesEnabled() = 0; // SDK cISC3City.h:87; installed +64.
  virtual void SetDisasterActive(bool) = 0;
  virtual void SetTutorialActive(bool) = 0;
  virtual void SetDemoActive(bool) = 0;
  virtual void EnableHolidayFeatures(bool) = 0;
  virtual bool IsModeFlagActive(u32) = 0;
  virtual void SetCityModeFlag(u32) = 0;
  virtual void ClearCityModeFlag(u32) = 0;
  virtual u32 GetSerialNumber() = 0;
  virtual void SetSerialNumber(u32) = 0;
  virtual bool GetOriginalLanguageAndCountry(i32 *, i32 *) = 0;
  virtual bool GetLastLanguageAndCountry(i32 *, i32 *) = 0;
  virtual u32 GetCityType() = 0;
  virtual u32 GetCityID() = 0;
  virtual ScStringView *GetCityName() = 0; // City +120.
  virtual void SetCityName(ScStringView const &) = 0;
  virtual ScStringView *GetCityDescription() = 0;
  virtual void SetCityDescription(ScStringView const &) = 0;
  virtual ScStringView *GetMayorName() = 0; // City +136.
  virtual void SetMayorName(ScStringView const &) = 0;
  virtual bool IsAutoBudgetEnabled() const = 0;
  virtual void SetAutoBudgetEnabled(bool) = 0;
  virtual bool AreDisastersEnabled() const = 0;
  virtual void SetDisastersEnabled(bool) = 0;
  virtual bool CanPlayerPauseUnpause() const = 0;
  virtual bool CanPlayerChangeSpeed() const = 0;
  virtual bool CanPlayerLoadSave() const = 0;
  virtual void SetPlayerLoadSaveEnabled(bool) = 0;
  virtual i32 GetDifficulty() = 0;
  virtual void SetDifficulty(i32) = 0;
  virtual TrGeneratorInterface *DirtGenerator() = 0; // City +184.
  virtual bool DoNewCity(bool) = 0;
  virtual bool DoLoadCity(bool) = 0;
  virtual bool DoSaveCity(bool) = 0;
  virtual void DoQuitGame(bool) = 0;
  virtual u32 CellCountX() const = 0; // City +204.
  virtual u32 CellCountZ() const = 0; // City +208.
  virtual u32 CellCountY() const = 0;
  virtual u32 AnimCellCountX() const = 0;
  virtual u32 AnimCellCountZ() const = 0;
  virtual u32 AnimCellCountY() const = 0;
  virtual u32 CellSizeInAnimUnitsX() const = 0;
  virtual u32 CellSizeInAnimUnitsZ() const = 0;
  virtual u32 CellSizeInAnimUnitsY() const = 0;
  virtual i64 GetTileSize() = 0;
  virtual f32 CellSizeInWorldUnitsX() const = 0;
  virtual f32 CellSizeInWorldUnitsZ() const = 0;
  virtual f32 CellSizeInWorldUnitsY() const = 0; // City +252; SDK float.
  virtual f32 SizeX() = 0;
  virtual f32 SizeY() = 0;
  virtual f32 SizeZ() = 0;
  virtual f32 WorldToMeter(f32 value) const = 0; // City +268; SDK :149, SIMCITY2AD8.
  virtual f32 WorldToFeet(f32) const = 0;
  virtual f32 MeterToWorld(f32) const = 0;
  virtual f32 FeetToWorld(f32) const = 0;
  virtual ScOccupantManager *SurfaceOccupantManager() = 0;             // City +284.
  virtual ScOccupantManager *UndergroundOccupantManagerLevelOne() = 0; // City +288.
  virtual ScOccupantManager *UndergroundOccupantManagerLevelTwo() = 0; // City +292.
  virtual OcAnimManager *AnimOccupantManager() = 0;
  virtual bool CreateOccupant(RsKey *, u32, void **) = 0;
  virtual bool CreateOccupant(GeAttrib *, u32, void **) = 0;
  virtual bool RemoveAllOccupants() = 0;
  virtual GeBuildingLayer *BuildingLayer() = 0; // City +312; queried IID20631788.
  virtual ScDirtBag *DirtBag() = 0;             // City +316.
  virtual NwTransit *TransitLayer() = 0;
  virtual NwTrafficLayer *TrafficLayer() = 0; // City +324; queried IID029CA805.
  virtual GeFlora *FloraLayer() = 0;          // City +328; proven IID406B1196 receiver.
  virtual CtZoneLayer *ZoneLayer() = 0;       // City +332; proven IID80902C70 receiver.
  virtual CtPowerLayer *PowerLayer() = 0;
  virtual CtPlumbingLayer *PlumbingLayer() = 0;
  virtual CtOrdinanceLayer *OrdinanceLayer() = 0; // SDK cISC3City.h:170; installed +344.
  virtual EcBudgetLayer *BudgetLayer() = 0;       // City +348.
  virtual CtWorldLayer *WorldLayer() = 0;
  virtual CtStartSimLayer *StrtSimLayer() = 0;
  virtual DsLayer *DisasterLayer() = 0;
  virtual EcLayer *PollutionLayer() = 0; // City +364.
  virtual CtCrimeLayer *CrimeLayer() = 0;
  virtual CtPoliceLayer *PoliceLayer() = 0;
  virtual CtFireLayer *FireLayer() = 0;
  virtual CtResidentialLayer *ResidentialLayer() = 0; // City +380; queried IID4106C508.
  virtual EcCommercialLayer *CommercialLayer() = 0;
  virtual EcIndustrialLayer *IndustrialLayer() = 0;
  virtual CtLandValueLayer *LandValueLayer() = 0;
  virtual CtNeighborsLayer *NeighborsLayer() = 0; // City +396.
  virtual EnWeatherLayer *WeatherLayer() = 0;
  virtual GeDemolitionView *DemolitionLayer() = 0;
  virtual AdUtilities *GetUtilityAdvisor() = 0;             // SDK cISC3City.h; installed +408.
  virtual AdDemographics *GetDemographicsAdvisor() = 0;     // SDK cISC3City.h; installed +412.
  virtual AdPublicSafety *GetPublicSafetyAdvisor() = 0;     // SDK cISC3City.h; installed +416.
  virtual AdEnvironment *GetEnvironmentAdvisor() = 0;       // SDK cISC3City.h; installed +420.
  virtual AdCityPlanning *GetCityPlanningAdvisor() = 0;     // SDK cISC3City.h; installed +424.
  virtual AdTransportation *GetTransportationAdvisor() = 0; // SDK cISC3City.h; installed +428.
  virtual AdBudget *GetBudgetAdvisor() = 0;                 // SDK cISC3City.h; installed +432.
  virtual AdPetitionerManager *GetPetitionerManager() = 0;  // City +436.
  virtual CtLayer *GetSpecificLayer(u32 type) = 0;          // SDK :196; installed +440.
  virtual bool GetLayer(u32, u32, void **) = 0;
  virtual u32 NumberOfLayers() = 0;
  virtual bool GetLayerByIndex(u32, u32, void **) = 0;
  virtual u32 GetAdvisorListLength() = 0;
  virtual u32 GetAdvisorList(u32, AdAdvisor **) = 0;
  virtual u32 GetLayerList(u32, CtLayer **) = 0;
  virtual bool DoEnumLayers(ScMessage *, CtCityEnumerator *) = 0;
  virtual CtSimulatorInterface *CitySimulator() = 0; // City +472.
  virtual bool StartCitySimulation() = 0;
  virtual bool EndCitySimulation() = 0;
  virtual void GetDate(u32 *dayOfYear, u32 *year) = 0;       // City +488.
  virtual void GetDate(u32 *month, u32 *day, u32 *year) = 0; // City +484.
  virtual bool SetDate(u32 dayOfYear, u32 year) = 0;
  virtual bool SetDate(u32 month, u32 day, u32 year) = 0;
  virtual u32 GetYearBorn() = 0;
  virtual bool SetYearBorn(u32) = 0;
  virtual void SimCalendarNotification(i32 phase) = 0; // City +508.
  virtual CtClockInterface *GetSimulationClock() = 0;  // City +512.
  virtual bool SetGameSpeed(i32) = 0;
  virtual i32 GetGameSpeed() = 0;
  virtual u32 Pause() = 0;
  virtual u32 Unpause() = 0;
  virtual bool IsPaused() = 0;
  virtual u32 PauseCount() = 0;
  virtual u32 SimHiddenPause() = 0;   // City +540.
  virtual u32 SimHiddenUnpause() = 0; // City +544.
  virtual u32 SimHiddenPauseCount() = 0;
  virtual u32 TotalPauseCount() = 0;    // City +552.
  virtual u32 HiddenClockPause() = 0;   // City +556.
  virtual u32 HiddenClockUnpause() = 0; // City +560.
  virtual void Lock() = 0;              // City +564.
  virtual void Unlock() = 0;            // City +568.
  virtual ScLock *GetCriticalSection() = 0;
  virtual void LockGraphics() = 0;   // City +576.
  virtual void UnlockGraphics() = 0; // City +580.
  virtual ScLock *GetGraphicsCriticalSection() = 0;
  virtual void OnIdle() = 0;
  virtual bool CanCitySave() const = 0;
  virtual ScStringView *GetFilePath() = 0;
  virtual void SetFilePath(ScStringView const &) = 0;
  virtual u32 GetVersionComponent(u8 component) const = 0;             // SDK :248; installed +604.
  virtual u32 GetVersionComponentFromSaveFile(u8 component) const = 0; // SDK :249; installed +608.
  virtual bool IsWriteCitySaveFileDebug() const = 0;
  virtual bool IsReadCitySaveFileDebug() const = 0;
  virtual bool StaticInit(CtDefinition *) = 0;
  virtual bool StaticShutdown() = 0;
  virtual CtScenarioLayer *ScenarioLayer() = 0; // SDK :256; installed +628.
  virtual bool GetAdvisor(u32, AdAdvisor **) = 0;
  virtual void SetTipOfTheDayNeeded(bool) = 0;
  virtual bool IsTipOfTheDayNeeded() = 0;
  virtual bool IsFileVersionMoreRecentThan(i32, i32, i32) = 0;
};

#endif
