#ifndef SC3_ADVISORS_AD_DOMAINS_H
#define SC3_ADVISORS_AD_DOMAINS_H

#include "advisors/ad_advice.h"
#include "advisors/ad_ref_count.h"
#include "city/simulation.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "economy/ec_residential.h"

// Primary interfaces adapted from cISC3AdvisorFor{CityPlanning,Environment,PublicSafety,Transportation,Utilities}.h.
// Copyright (C) 2025 Nicholas Hayes; LGPL-2.1-or-later. See SDK-LICENSE.txt.
// Installed constructors establish the separate primary interface and AdBaseAdvisor+4.
// Concrete declarations below retain explicit pure dependencies until their actual domain methods are recovered.
class DsInstance;
class DsLayer;
class ScLock;
class CtPoliceLayer;
class CtFireLayer;
class CtCrimeLayer;
class EcLayer;
class EcBudgetLayer;
class CtNeighborsLayer;
class CtOrdinanceLayer;
class CtResidentialLayer;
class GeBuildingLayer;
class UiQueryRows;
class CtZoneLayer;
class EcValveLayer;
class NwTrafficLayer;
class EcDepartmentBudget;
class AdTransportationCallback3;
class AdTransportationCallback4;
class AdUtilityEntry;
class AdBudget;
class AdUtilitiesCallback3;
class AdUtilitiesCallback4;
class AdSafetyCallback3;
class AdSafetyCallback4;
class AdGarbageEntry;
class AdRewardEntry;
class EcValve;
class EcCommercialLayer;
class EcIndustrialLayer;
class CtAuraLayer;
class CtLandValueLayer;
class NwManager;
class AdPlanningCallback3;
class AdPlanningCallback4;
class AdEnvironmentCallback3;
class AdEnvironmentCallback4;

struct AdGarbageLink {
  AdGarbageLink *next, *previous;
};

struct AdGarbageNode : AdGarbageLink {
  AdGarbageEntry *entry;
};

class AdCityPlanning : public ScUnknown {
public:
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual AdAdvisor *AsISC3BaseAdvisor() = 0;
  virtual bool AddBusinessDealBuildingToRewardList(u32 type) = 0;
  virtual bool IsBusinessDealBuildingInRewardList(u32 type) = 0;
  virtual bool RemoveBusinessDealBuildingFromRewardList(u32 type) = 0;
};

class AdEnvironment : public ScUnknown {
public:
  virtual bool CheckGarbageBuildingAvailibility(u32 type) = 0;
};

class AdPublicSafety : public ScUnknown {
public:
  virtual void DisasterStarted(DsInstance *instance) = 0;
  virtual void DisasterEnded(DsInstance *instance) = 0;
};

class AdTransportation : public ScUnknown {
public:
  virtual bool InventionAvailable(u32 type) const = 0;
};

class AdUtilities : public ScUnknown {
public:
  virtual AdAdvisor *AsISC3BaseAdvisor() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual bool CheckPowerBuildingAvailibility(u32 type) = 0;
  virtual bool CheckWaterBuildingAvailibility(u32 type) = 0;
  virtual f32 GetPowerFundingPercentage() = 0;
  virtual bool DisableNewDealsOfType(i32 type) = 0;
  virtual bool EnableNewDealsOfType(i32 type) = 0;
};

// The planning collections retain the original resource-key and building-type keys with their actual reward records.
struct AdKeyedReward {
  RsKey key;
  AdRewardEntry *entry;
};

struct AdTypedReward {
  u32 type;
  AdRewardEntry *entry;
};

// Original 792-byte planning allocation; unknown sampled words retain neutral names until their real producers are recovered.
// The three 12-word arrays and the isolated byte510 initialization are proven by the constructor's stores.
class AdCityPlanningAdvisor : public AdCityPlanning, public AdBaseAdvisor {
public:
  AdCityPlanningAdvisor();
  ~AdCityPlanningAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  u32 GetLayerType() override;
  CtAgent *AsISC3CityAgent() override;
  AdAdvisor *AsISC3BaseAdvisor() override;
  bool AddBusinessDealBuildingToRewardList(u32 type) override;
  bool IsBusinessDealBuildingInRewardList(u32 type) override;
  bool RemoveBusinessDealBuildingFromRewardList(u32 type) override;
  bool DoAdvisorSimulation() override;
  void GetSimulationData() override;
  bool CheckOnAdvice() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool LoadConfiguration();
  // Native89E7 effects retain the actual selector/key/value arguments; its private nominal return is unresolved.
  void ConfigureBuilding(u32 selector, const ScString *key, const ScString *value);
  // Native9304 keyed-record release phase; its ignored private nominal return remains unresolved.
  void ClearRewards();
  // Native B0DD/B1DD row effects retain unresolved ignored private nominal returns.
  void WriteRewardRows(UiQueryRows *rows);
  void WriteConfiguredRewardRows(UiQueryRows *rows);
  // Native A8D7 petitioner effects; its ignored private nominal return remains unresolved.
  void CheckOnPetitioners();
  // Native A324 reward eligibility effects; its ignored private nominal return remains unresolved.
  void CheckOnRewards();
  // Native936E ordinance insertion effects; its ignored private nominal return remains unresolved.
  void InitializeOrdinances();
  // Native8EF2 rebuild effects; the ignored metadata-destruction return is not a game result.
  void InitializeRewards();
  // Native9DBE/9E0C/9E5A count aggregates retain unresolved private nominal signedness.
  u32 ResidentialFailures(i32 reason);
  u32 CommercialFailures(i32 reason);
  u32 IndustrialFailures(i32 reason);
  // Native B32A/B458 byte carriers; private nominal bool/BYTE spellings remain unresolved.
  u8 RewardPlaced(ScOccupant *occupant);
  u8 RewardRemoved(ScOccupant *occupant);
  // Native ABB2 response effects; its ignored private nominal return remains unresolved.
  void RespondToPetitioner(u32 id, i32 button);
  ScArray<AdKeyedReward> rewards;
  ScArray<AdTypedReward> reward_types;
  u8 commercial_cap_problem, industrial_cap_problem;
  u32 small_city, medium_city, non_development_sensitivity, parks_for_national_park, airport_for_spaceport;
  u8 medical_center_le, science_center_eq, university_eq, high_tech_eq, high_tech_job_ratio, park_ratio, park_aura;
  u32 unknown_dc;
  i64 stock_exchange_funds;
  u32 residential_negative, commercial_negative, industrial_negative;
  u32 residential_positive, commercial_positive, industrial_positive;
  u8 responsiveness_months;
  u32 low_land_value;
  u8 farm_air_pollution, farm_water_pollution;
  u16 unknown_10a;
  u32 unknown_10c, unknown_110, unknown_114;
  u32 sample_118[12], sample_148[12], sample_178[12];
  u8 unknown_1a8, unknown_1a9, unknown_1aa, unknown_1ab, unknown_1ac, unknown_1ad, unknown_1ae, unknown_1af;
  u32 available_landfill_capacity, total_landfill_capacity;
  u32 unknown_1b8, unknown_1bc;
  i64 unknown_1c0, unknown_1c8, unknown_1d0, unknown_1d8, unknown_1e0, unknown_1e8;
  i64 unknown_1f0;
  u32 unknown_1f8;
  u8 unknown_1fc, unknown_1fd;
  u8 flag_1fe, unknown_1ff;
  u32 unknown_200, unknown_204, unknown_208, unknown_20c;
  u32 unknown_210, unknown_214, unknown_218, unknown_21c;
  u32 unknown_220, unknown_224, unknown_228, unknown_22c;
  u32 unknown_230, unknown_234, unknown_238, unknown_23c;
  u32 unknown_240, unknown_244, unknown_248;
  u8 unknown_24c, unknown_24d, unknown_24e, unknown_24f;
  u32 unknown_250, unknown_254, unknown_258, unknown_25c;
  u32 unknown_260, unknown_264, unknown_268, unknown_26c;
  u32 unknown_270, unknown_274, unknown_278, unknown_27c;
  u32 unknown_280, unknown_284, unknown_288, unknown_28c;
  u32 unknown_290, unknown_294, unknown_298, unknown_29c;
  u32 unknown_2a0, unknown_2a4, unknown_2a8, unknown_2ac;
  u32 unknown_2b0, unknown_2b4;
  EcBudgetLayer *budget;
  EcValveLayer *valves;
  CtNeighborsLayer *neighbors;
  CtResidentialLayer *residential;
  EcCommercialLayer *commercial;
  EcIndustrialLayer *industrial;
  GeBuildingLayer *buildings;
  CtAuraLayer *aura;
  AdBudget *budget_advisor;
  CtOrdinanceLayer *planning_ordinances;
  CtZoneLayer *zones;
  CtLandValueLayer *land_value;
  EcLayer *pollution;
  NwManager *networks;
  ScOccupantManager *occupants;
  EcValve *residential_valve, *commercial_valve, *industrial_valve;
  AdTransportation *transportation;
  AdPlanningCallback3 *callback3;
  AdPlanningCallback4 *callback4;
  u8 unknown_30c, flag_30d, flag_30e, unknown_30f;
  ScLock *reward_lock;
};

// Original sixteen-byte planning phase3 manipulator with a secondary reference base and borrowed owner at12.
class AdPlanningCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdPlanningCallback3(AdCityPlanningAdvisor *owner);
  ~AdPlanningCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdCityPlanningAdvisor *owner;
};

// Original sixteen-byte planning phase4 manipulator with a secondary reference base and borrowed owner at12.
class AdPlanningCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdPlanningCallback4(AdCityPlanningAdvisor *owner);
  ~AdPlanningCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdCityPlanningAdvisor *owner;
};

// Full allocation layout from100011B4 and the environment constructor/snapshot methods; unresolved words stay explicit.
// Query information and saved-city methods remain explicit dependencies; private service words retain unresolved names.
class AdEnvironmentAdvisor : public AdEnvironment, public AdBaseAdvisor {
public:
  AdEnvironmentAdvisor();
  ~AdEnvironmentAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  u32 GetLayerType() override;
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool DoAdvisorSimulation() override;
  void GetSimulationData() override;
  bool CheckOnAdvice() override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool DoMessage(ScMessage *message) override;
  bool CheckGarbageBuildingAvailibility(u32 type) override;
  // Native effects phases of10F37/10FC9/1105D; private nominal return declarations remain unproved.
  void EnableAvailableBuildings();
  void AnnounceAvailableBuilding();
  void CheckPetitioners();
  bool LoadConfiguration();
  // Native1019A phase retains selector/key/value; private selector and return spelling remain unresolved.
  void ConfigureBuilding(u32 selector, const ScString *key, const ScString *value);
  // Native11590 effects phase; its constant true carrier has an unresolved private nominal declaration.
  void WriteQueryRows(UiQueryRows *rows);
  AdGarbageLink *garbage_entries;
  u32 water_high, water_medium, air_high, air_medium, overall_high, overall_medium;
  u32 unknown_c0, unknown_c4, unknown_c8, unknown_cc;
  i32 average_air, average_water;
  u32 unknown_d8;
  i16 average_garbage;
  i32 available_landfill, monthly_facility_capacity;
  u32 projected_capacity;
  i64 produced, imported, exported, recycled, incinerated, landfilled;
  i64 monthly_input, monthly_disposed;
  u8 garbage_uncollected;
  i32 months_uncollected, building_12851, building_12820, building_12330;
  u8 garbage_deal_4, garbage_deal_5, flag326, ordinance_flag, flag328, building_available;
  u32 population, day_of_year, year, unknown158, unknown15c;
  u8 city_state;
  CtCity *simulation_city;
  EcLayer *pollution;
  ScUnknown *service_16c, *service_170;
  EcBudgetLayer *budget;
  ScUnknown *service_178;
  CtNeighborsLayer *neighbors;
  CtOrdinanceLayer *ordinances;
  GeBuildingLayer *buildings;
  CtResidentialLayer *residential;
  AdUtilities *utilities;
  AdEnvironmentCallback3 *callback3;
  AdEnvironmentCallback4 *callback4;
};

// Trivial game metadata retained by the public-safety disaster notification tree.
struct AdSafetyNotice {
  DsInstance *instance;
  u32 disaster_type, notification;
  u8 state;
};

// Original 364-byte public-safety receiver; remaining city services and simulation operations stay explicit.
// The project array stores notice payloads instead of rebuilding general-purpose tree allocation machinery.
class AdPublicSafetyAdvisor : public AdPublicSafety, public AdBaseAdvisor {
public:
  AdPublicSafetyAdvisor();
  bool DoAdvisorSimulation() override;
  void GetSimulationData() override;
  bool DoMessage(ScMessage *message) override;
  // Native13BA0/1406D/14B44 effects retain unresolved ignored private nominal returns.
  void ShowDisasterNotification(ScMessage *message);
  void ProcessEventState();
  void AdvanceHoliday();
  ~AdPublicSafetyAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  u32 GetLayerType() override;
  bool LoadConfiguration();
  bool CheckOnAdvice() override;
  // Native effects phases retain unresolved private nominal returns and preserve the separate original arguments.
  void CheckPetitioners();
  void InitializeOrdinances();
  void RespondToPetitioner(u32 id, i32 button);
  void RecordEventState(i32 first, i32 second);
  u8 AdvanceDisaster(DsInstance *current);
  u8 HasDisaster(u32 type);
  void DisasterStarted(DsInstance *instance) override;
  void DisasterEnded(DsInstance *instance) override;
  void AddDisasterTokens(u32 id, DsInstance *instance);
  void ShowNextDisaster();
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  u8 police_funding_high, police_funding_medium, fire_funding_high, fire_funding_medium;
  u8 fire_coverage_cutoff, crime_medium, crime_high;
  u32 fire_station_minimum, jail_minimum, police_station_ratio;
  f32 signal_b8, signal_bc;
  u8 police_funding, fire_funding, police_strike, fire_strike;
  u32 unknown_c4, unknown_c8, unknown_cc, unknown_d0, unknown_d4;
  u32 unknown_d8, unknown_dc, unknown_e0, unknown_e4, unknown_e8, unknown_ec;
  u32 unknown_f0, unknown_f4;
  u8 crime_level, ordinance_f9, ordinance_fa;
  u32 unknown_fc, unknown_100, unknown_104;
  CtCity *simulation_city;
  DsLayer *disasters;
  CtFireLayer *fire_layer;
  CtPoliceLayer *police_layer;
  CtCrimeLayer *crime_layer;
  EcBudgetLayer *budget;
  EcValveLayer *valves;
  GeBuildingLayer *buildings;
  CtResidentialLayer *residential;
  CtOrdinanceLayer *ordinances;
  ScOccupantManager *occupants;
  AdSafetyCallback3 *callback3;
  AdSafetyCallback4 *callback4;
  u8 event_state;
  ScArray<AdSafetyNotice> notices; // Entries retain original disaster-pointer order.
  DsInstance *selected_disaster;
  ScLock *notice_lock;
  u32 unknown_154, unknown_158, unknown_15c;
  u8 unknown_160;
  EcDepartmentBudget *police_budget, *fire_budget;
};

// Original384-byte transportation object; city simulation methods remain explicit inherited dependencies.
// The constructor initializes tuning, invention flags and services, leaving sampled fields for the real sampler.
class AdTransportationAdvisor : public AdTransportation, public AdBaseAdvisor {
public:
  AdTransportationAdvisor();
  ~AdTransportationAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  u32 GetLayerType() override;
  bool InventionAvailable(u32 type) const override;
  bool DoAdvisorSimulation() override;
  void GetSimulationData() override;
  bool CheckOnAdvice() override;
  // Native176A7/17915/16D13 effects retain unresolved ignored private nominal returns.
  void CheckPetitioners();
  void RespondToPetitioner(u32 id, i32 button);
  void InitializeOrdinances();
  bool DoMessage(ScMessage *message) override;
  bool LoadConfiguration();
  // Native17528 effects; the ignored private source return declaration remains unresolved.
  void EnableInventions();
  // Native1725D effects; observed AL0/1 has an unresolved private source declaration.
  void AnnounceInventions();
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Init(CtCity *city) override;
  bool Shutdown() override;
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  u8 road_funding_high, road_funding_medium, road_funding_low, rail_funding_high, rail_funding_medium;
  u32 subway_year, bus_station_year, airport_year, highway_year;
  u32 road_traffic_high, road_traffic_medium;
  f32 road_damage_cutoff;
  u32 carpool_level, carpool_year, crossing_guard_level, crossing_guard_year;
  u32 alternate_day_level, alternate_day_year, bus_stop_level, rail_usage_medium, rail_usage_high;
  u32 unknown_ec, day_of_year, year;
  bool inventions[4], announcements[4];
  u8 road_funding, rail_funding;
  u32 network_104, network_108, network_10c, network_110;
  f32 road_damage, road_traffic;
  u32 rail_total, rail_usage;
  i32 bus_stations, rail_stations, subway_stations, subway_rail_stations;
  u8 ordinance_carpool, ordinance_crossing_guard;
  u32 population, zone_14, zone_18;
  i32 airports;
  CtCity *simulation_city;
  NwManager *networks;
  NwTrafficLayer *traffic;
  ScUnknown *service_154;
  EcBudgetLayer *budget;
  EcValveLayer *valves;
  GeBuildingLayer *buildings;
  CtOrdinanceLayer *ordinances;
  CtResidentialLayer *residential;
  CtZoneLayer *zones;
  AdTransportationCallback3 *callback3;
  AdTransportationCallback4 *callback4;
  EcDepartmentBudget *funding_variables[2];
};

struct AdUtilityLink {
  AdUtilityLink *next, *previous;
};

struct AdUtilityNode : AdUtilityLink {
  AdUtilityEntry *entry;
};

// Original 496-byte utilities object; unbound services and sampled words remain explicit.
// Only constructor-defined tuning and the actual primary operations are recovered here.
struct CtDealRecord;
class CtPowerLayer;
class CtPlumbingLayer;

class AdUtilitiesAdvisor : public AdUtilities, public AdBaseAdvisor {
public:
  AdUtilitiesAdvisor();
  ~AdUtilitiesAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  u32 GetLayerType() override;
  AdAdvisor *AsISC3BaseAdvisor() override;
  CtAgent *AsISC3CityAgent() override;
  bool CheckPowerBuildingAvailibility(u32 type) override;
  bool CheckWaterBuildingAvailibility(u32 type) override;
  f32 GetPowerFundingPercentage() override;
  bool DisableNewDealsOfType(i32 type) override;
  bool EnableNewDealsOfType(i32 type) override;
  bool DoMessage(ScMessage *message) override;
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  // Native rule phases retain unresolved private nominal return declarations.
  void EnableAvailableBuildings();
  void AnnounceAvailableBuilding();
  void CheckPetitioners();
  bool CheckOnAdvice() override;
  void GetSimulationData() override;
  bool LoadConfiguration();
  void InitializeOrdinances();
  bool DoAdvisorSimulation() override;
  u8 CheckDeals();
  // Original copied deal aggregate; the proven byte-one result retains unresolved bool/BYTE nominal spelling.
  u8 AddDealTokens(u32 id, CtDealRecord record);
  void ConfigureBuilding(u32 selector, const ScString *key, const ScString *value);
  // Native1C191 raw byte result; private nominal BYTE/char spelling remains unresolved.
  u8 NewDealsEnabled(u32 type);
  // Native1C1A8 response effects; the ignored private nominal return remains unresolved.
  void RespondToPetitioner(u32 id, i32 button);
  void WritePowerRows(UiQueryRows *rows);
  void WriteWaterRows(UiQueryRows *rows);
  AdUtilityLink *power_entries, *water_entries;
  u32 unknown_ac;
  f64 power_high, power_low, water_high, water_low;
  u8 power_usage_critical, power_usage_high, power_usage_medium, water_usage_critical, water_usage_high,
      water_usage_medium;
  u32 low_treasury_level, water_conservation_year, minimum_deal_population, unknown_e4;
  CtCity *simulation_city;
  CtPowerLayer *service_ec;
  CtPlumbingLayer *service_f0;
  EcLayer *pollution;
  EcBudgetLayer *budget;
  EcValveLayer *valves;
  CtNeighborsLayer *neighbors;
  AdBudget *budget_advisor;
  CtOrdinanceLayer *ordinances;
  GeBuildingLayer *buildings;
  CtResidentialLayer *residential;
  AdEnvironment *environment_advisor;
  u32 unknown_118, unknown_11c;
  AdUtilitiesCallback3 *callback3;
  AdUtilitiesCallback4 *callback4;
  u32 day, year;
  u32 unknown_130, unknown_134, unknown_138, unknown_13c, unknown_140, unknown_144;
  i64 water_used, water_capacity, water_balance;
  u32 unknown_160, unknown_164, unknown_168;
  f32 power_funding_percent;
  u32 unknown_170, unknown_174;
  i64 treasury;
  u32 unknown_180, unknown_184;
  u8 valve1, valve0, valve3, valve2, valve5, valve4, ordinance_18e;
  bool nuclear_ban;
  u32 unknown_190, unknown_194, unknown_198, unknown_19c;
  u32 unknown_1a0, unknown_1a4, unknown_1a8, unknown_1ac, unknown_1b0, unknown_1b4;
  u32 unknown_1b8, unknown_1bc;
  i64 garbage_produced_last, garbage_imported_last, garbage_recycled_last, garbage_total, garbage_balance;
  u8 unknown_1e8, new_deals[6];
};

// Actual sixteen-byte phase-three/four objects: city-manipulator primary, reference base+4 and borrowed owner+12.
class AdEnvironmentCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdEnvironmentCallback3(AdEnvironmentAdvisor *owner);
  ~AdEnvironmentCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdEnvironmentAdvisor *owner;
};

class AdEnvironmentCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdEnvironmentCallback4(AdEnvironmentAdvisor *owner);
  ~AdEnvironmentCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdEnvironmentAdvisor *owner;
};

class AdTransportationCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdTransportationCallback3(AdTransportationAdvisor *owner);
  ~AdTransportationCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdTransportationAdvisor *owner;
};

class AdTransportationCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdTransportationCallback4(AdTransportationAdvisor *owner);
  ~AdTransportationCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdTransportationAdvisor *owner;
};

// Safety phase callbacks use their actual manipulator/reference bases and a borrowed complete advisor.
class AdSafetyCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdSafetyCallback3(AdPublicSafetyAdvisor *owner);
  ~AdSafetyCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdPublicSafetyAdvisor *owner;
};

class AdSafetyCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdSafetyCallback4(AdPublicSafetyAdvisor *owner);
  ~AdSafetyCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdPublicSafetyAdvisor *owner;
};

// Original utilities manipulators keep a borrowed complete owner and their distinct primary IID-only query.
class AdUtilitiesCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdUtilitiesCallback3(AdUtilitiesAdvisor *owner);
  ~AdUtilitiesCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdUtilitiesAdvisor *owner;
};

class AdUtilitiesCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdUtilitiesCallback4(AdUtilitiesAdvisor *owner);
  ~AdUtilitiesCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdUtilitiesAdvisor *owner;
};

// Original 56-byte safety department with its genuine civic provider and borrowed budget manager.
class AdPoliceBudget : public ScRefCount, public EcDepartmentBudget {
public:
  AdPoliceBudget(u32 id, ScString name);
  ~AdPoliceBudget() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetDepartmentID() override;
  i64 GetLastYearSurplus() override;
  i64 GetTotalFundingNeeded() override;
  void SetTotalFundingNeeded(i64 value) override;
  ScStringView *GetDepartmentName() override;
  void SetFundingPercentage(u8 value) override;
  u8 GetFundingPercentage() override;
  u32 GetAdvisorID() override;
  u32 GetAdvisorGroupID() override;
  u32 department_id;
  ScString name;
  u8 funding;
  i64 needed;
  CtPoliceLayer *police;
  EcBudgetLayer *budget;
};

// Original 56-byte safety department with its genuine civic provider and borrowed budget manager.
class AdFireBudget : public ScRefCount, public EcDepartmentBudget {
public:
  AdFireBudget(u32 id, ScString name);
  ~AdFireBudget() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetDepartmentID() override;
  i64 GetLastYearSurplus() override;
  i64 GetTotalFundingNeeded() override;
  void SetTotalFundingNeeded(i64 value) override;
  ScStringView *GetDepartmentName() override;
  void SetFundingPercentage(u8 value) override;
  u8 GetFundingPercentage() override;
  u32 GetAdvisorID() override;
  u32 GetAdvisorGroupID() override;
  u32 department_id;
  ScString name;
  u8 funding;
  i64 needed;
  CtFireLayer *fire;
  EcBudgetLayer *budget;
};

// Actual56-byte transportation department using the original typed traffic funding operation.
// The traffic QI view is retained, while the budget provider is borrowed.
class AdRoadBudget : public ScRefCount, public EcDepartmentBudget {
public:
  AdRoadBudget(u32 id, ScString name);
  ~AdRoadBudget() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetDepartmentID() override;
  i64 GetLastYearSurplus() override;
  i64 GetTotalFundingNeeded() override;
  void SetTotalFundingNeeded(i64 value) override;
  ScStringView *GetDepartmentName() override;
  void SetFundingPercentage(u8 value) override;
  u8 GetFundingPercentage() override;
  u32 GetAdvisorID() override;
  u32 GetAdvisorGroupID() override;
  u32 department_id;
  ScString name;
  u8 funding;
  i64 needed;
  NwTrafficLayer *traffic;
  EcBudgetLayer *budget;
};

// Actual56-byte transportation department using the original typed traffic funding operation.
// The traffic QI view is retained, while the budget provider is borrowed.
class AdRailBudget : public ScRefCount, public EcDepartmentBudget {
public:
  AdRailBudget(u32 id, ScString name);
  ~AdRailBudget() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetDepartmentID() override;
  i64 GetLastYearSurplus() override;
  i64 GetTotalFundingNeeded() override;
  void SetTotalFundingNeeded(i64 value) override;
  ScStringView *GetDepartmentName() override;
  void SetFundingPercentage(u8 value) override;
  u8 GetFundingPercentage() override;
  u32 GetAdvisorID() override;
  u32 GetAdvisorGroupID() override;
  u32 department_id;
  ScString name;
  u8 funding;
  i64 needed;
  NwTrafficLayer *traffic;
  EcBudgetLayer *budget;
};

#endif
