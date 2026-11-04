#ifndef SC3_ADVISORS_AD_ADVISORS_H
#define SC3_ADVISORS_AD_ADVISORS_H

#include "advisors/ad_advice.h"
#include "advisors/ad_ref_count.h"
#include "buildings/api.h"
#include "city/simulation.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "economy/ec_residential.h"
#include "resources/resource.h"

// Declarations adapted from cISC3AdvisorForBudget.h, Copyright (C) 2025 Nicholas Hayes.
// LGPL-2.1-or-later; see SDK-LICENSE.txt. This is the original business-deal interface, not the shared advisor base.
class AdDealRecord;
class CtNeighborsLayer;
class EcValveLayer;
class EcValve;
class UiQueryRows;

class AdBudget : public ScUnknown {
public:
  virtual u32 GetTotalMonthlyBusinessDealIncome() = 0;
  virtual u32 GetBusinessDealIncomeForBuildingType(u32 type) = 0;
  virtual bool ActivateBusinessDeal(u32 type, BdBuilding *building) = 0;
  virtual bool RemoveBusinessDeal(u32 type) = 0;
  virtual void OfferBusinessDeal(u32 type) = 0;
  virtual bool IsBusinessDealActive(u32 type) = 0;
  virtual void SetDealEnabled(u32 type, bool enabled) = 0;
};

struct AdDealLink {
  AdDealLink *next, *previous;
};

struct AdDealNode : AdDealLink {
  AdDealRecord *record;
};

// Concrete budget advisor; unresolved historical private return spellings remain documented beside their native phases.
class AdBudgetAdvisor : public AdBudget, public AdBaseAdvisor {
public:
  AdBudgetAdvisor();
  ~AdBudgetAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  u32 GetLayerType() override;
  u32 GetTotalMonthlyBusinessDealIncome() override;
  u32 GetBusinessDealIncomeForBuildingType(u32 type) override;
  bool IsBusinessDealActive(u32 type) override;
  bool RemoveBusinessDeal(u32 type) override;
  bool ActivateBusinessDeal(u32 type, BdBuilding *building) override;
  void SetDealEnabled(u32 type, bool enabled) override;
  void OfferBusinessDeal(u32 type) override;
  bool HasBusinessDeal(u32 type);
  // Native6136 byte carrier and5F27 response effects; private nominal return spellings remain unresolved.
  u8 RemoveDealBuilding(u32 type);
  void RespondToPetitioner(u32 id, i32 button);
  bool LoadConfiguration();
  // Native ordinance insertion phase of4D58; its ignored private nominal return remains unresolved.
  void InitializeOrdinances();
  // Native configuration phase of4AE6; its private ignored nominal return remains unresolved.
  void ConfigureDeal(const ScString *key, const ScString *value);
  void GetSimulationData() override;
  bool DoAdvisorSimulation() override;
  bool CheckOnAdvice() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  // Native row-population phase of6383; its private Boolean-versus-byte result remains unresolved.
  void WriteQueryRows(UiQueryRows *rows);
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  // Observed true-byte result retained; the private PC Boolean-versus-byte source declaration remains unresolved.
  bool CheckBusinessDeals();
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  AdDealLink *deals;
  u8 tax_rates[3];
  f32 tax_modifiers[3];
  i64 treasury, borrowed, borrowing_limit, estimated_income, estimated_expenses, demand[3];
  u32 population;
  i32 months_without_offer;
  u8 offer_delay;
  u32 day_of_year, year;
  u8 deal_building_present[2];
  i64 treasury_low, lose_game_level;
  f32 tax_growth_threshold, tax_decline_threshold;
  u32 unknown128, unknown12c, unknown130;
  CtCity *simulation_city;
  EcBudgetLayer *budget;
  EcValveLayer *valve_layer;
  CtResidentialLayer *residential;
  CtOrdinanceLayer *ordinance_layer;
  CtNeighborsLayer *neighbors;
  GeBuildingLayer *buildings;
  AdCityPlanning *planning;
  EcValve *demand_valves[3];
  CtLayerManipulator *callback3, *callback4;
  u8 unknown168;
  u32 unknown16c;
};

// Actual56-byte business-deal object, with local references and an owned name rather than a module-reference base.
// The retained building view is the separate SIMMISC identity, with no occupant/building base relationship.
class AdDealRecord final : public ScUnknown {
public:
  AdDealRecord();
  ~AdDealRecord();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 *Value4();
  u32 *TypeData();
  u32 *Value12();
  u32 *IncomeData();
  u8 *ActiveData();
  ScStringView *Name();
  // Native6543/6562 setter phases retain unconsumed private nominal return gaps.
  void SetName(ScStringView *value);
  BdDealView *View();
  void SetView(BdDealView *value);
  u8 *EnabledData();
  u32 value4, type, value12;
  ScString name;
  u32 income;
  u8 active;
  u32 references;
  BdDealView *view;
  u8 enabled;
};

// Actual sixteen-byte phase-three/four objects: city-manipulator primary, reference base+4 and borrowed owner+12.
class AdBudgetCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdBudgetCallback3(AdBudgetAdvisor *owner);
  ~AdBudgetCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdBudgetAdvisor *owner;
};

class AdBudgetCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdBudgetCallback4(AdBudgetAdvisor *owner);
  ~AdBudgetCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdBudgetAdvisor *owner;
};

class GeAttrib;

// Distinct original 96-byte records created by the garbage and power/water INI entry handlers.
// Their private interfaces place the building type and availability data in different slots and storage fields.
// Native declarations expose the recovered operations as a subset, not a foreign vtable overlay.
// Text/key setters below are native effects phases; their unconsumed private nominal returns remain unresolved.
// The dangling temporary returned by original slot48 remains an explicit missing operation.
class AdGarbageEntry final : public ScUnknown {
public:
  AdGarbageEntry();
  ~AdGarbageEntry();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScStringView *FirstText();
  void SetFirstText(ScStringView *value);
  ScStringView *SecondText();
  void SetSecondText(ScStringView *value);
  ScStringView *ThirdText();
  void SetThirdText(ScStringView *value);
  void SetKey(const RsKey *value);
  RsKey *GetKey();
  GeAttrib *GetCachedAttrib();
  u32 *Value76();
  u32 *Value80();
  u32 *TypeData();
  u8 *AvailableData();
  ScString first_text, second_text, third_text;
  RsKey key;
  u32 value76, value80, type;
  u8 available;
  u32 references;
};

class AdUtilityEntry final : public ScUnknown {
public:
  AdUtilityEntry();
  ~AdUtilityEntry();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetFirstText(ScStringView *value);
  ScStringView *FirstText();
  void SetSecondText(ScStringView *value);
  ScStringView *SecondText();
  void SetThirdText(ScStringView *value);
  ScStringView *ThirdText();
  void SetKey(const RsKey *value);
  RsKey *GetKey();
  GeAttrib *GetCachedAttrib();
  u32 *Value76();
  u32 *Value80();
  u32 *TypeData();
  u8 *AvailableData();
  ScString first_text, second_text, third_text;
  RsKey key;
  u32 value76, value80;
  u8 available;
  u32 references, type;
};
// Distinct planning reward/deal record: three texts, three configuration DWORDs, four raw flags, count and key.
// Original slot48 returns a destroyed temporary String; that unresolved operation is omitted from this native subset.
class AdRewardEntry : public ScUnknown {
public:
  AdRewardEntry();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual void SetFirstText(ScStringView *value);
  virtual ScStringView *FirstText();
  virtual void SetSecondText(ScStringView *value);
  virtual ScStringView *SecondText();
  virtual void SetThirdText(ScStringView *value);
  virtual ScStringView *ThirdText();
  virtual void SetKey(const RsKey *value);
  virtual RsKey *GetKey();
  virtual GeAttrib *GetCachedAttrib();
  virtual u32 *Value64();
  virtual u32 *Value68();
  virtual u32 *Value72();
  virtual u8 *AvailableData();
  virtual u8 *Flag77Data();
  virtual u8 *Flag78Data();
  virtual u8 *Flag79Data();
  virtual ~AdRewardEntry();
  ScString first_text, second_text, third_text;
  u32 value64, value68, value72;
  u8 available, flag77, flag78, flag79;
  u32 references;
  RsKey key;
};

// Interface adapted from cISC3AdvisorForDemographics.h, Copyright (C) 2025 Nicholas Hayes.
// LGPL-2.1-or-later; see SDK-LICENSE.txt.
// SDK cISC3AdvisorForDemographics has only the Unknown interface; the separate shared advisor supplies its methods.
class AdDemographics : public ScUnknown {};

// Original domain state follows AdBaseAdvisor+4. Unknown fields remain explicit until their actual uses are recovered.
class AdDemographicsCallback3;
class AdDemographicsCallback4;

class AdDemographicsAdvisor : public AdDemographics, public AdBaseAdvisor {
public:
  AdDemographicsAdvisor();
  ~AdDemographicsAdvisor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetAdvisorID() override;
  bool CheckOnAdvice() override;
  bool DoMessage(ScMessage *message) override;
  void GetSimulationData() override;
  bool DoAdvisorSimulation() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  u32 GetLayerType() override;
  bool Shutdown() override;
  bool CheckPetitioners();
  // Native EEFE response effects; its ignored private return declaration remains unresolved.
  void RespondToPetitioner(u32 id, u32 button);
  bool LoadConfiguration();
  // Native effects phase of E427; its unconsumed private nominal return remains unresolved.
  void InitializeOrdinances();
  using AdBaseAdvisor::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  u8 health_funding_high, health_funding_medium, education_funding_high, education_funding_medium;
  u32 residential_growth_cap, stadium_growth_cutoff, hospital_population_minimum, library_population_minimum;
  u32 museum_population_minimum, school_population_minimum, college_population_minimum, dreary_population_minimum;
  u32 zoo_population_minimum, marina_population_minimum, hospital_ratio;
  i8 aura_low, aura_high;
  u32 le_eq_population_minimum;
  u8 eq_low, eq_average, eq_high, le_low, le_high, dreary_low, dreary_high;
  u32 unknown_e4, unknown_e8, unknown_ec;
  CtCity *simulation_city;
  CtResidentialLayer *residential;
  EcBudgetLayer *budget;
  ScUnknown *service_fc;
  GeBuildingLayer *buildings;
  CtAuraLayer *aura_layer;
  AdDemographicsCallback3 *callback3;
  AdDemographicsCallback4 *callback4;
  u8 health_funding, education_funding, life_expectancy, education;
  u32 health_rating, school_rating, college_rating;
  i32 hospitals, schools, colleges, stadiums;
  i8 aura;
  u8 development_cap;
  u32 population;
  u8 health_strike, school_strike, college_strike;
  u32 school_strike_chance, college_strike_chance, health_strike_chance;
  i32 zoos, marinas;
  u32 patients, school_students, college_students;
  u32 health_capacity, school_capacity, college_capacity, library_capacity, museum_capacity;
  u32 unknown_170, unknown_174;
  u8 unknown_178;
  EcDepartmentBudget *funding_variables[2];
};

class CtResidentialLayer;
class EcBudgetLayer;

// Actual56-byte demographics funding objects: reference base0, department interface8 and owned name16.
// City providers are borrowed; the department registry and advisor retain the objects through their real interfaces.
class AdEducationBudget : public ScRefCount, public EcDepartmentBudget {
public:
  AdEducationBudget(u32 id, ScString name);
  ~AdEducationBudget() override;
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
  CtResidentialLayer *residential;
  EcBudgetLayer *budget;
};

class AdHealthBudget : public ScRefCount, public EcDepartmentBudget {
public:
  AdHealthBudget(u32 id, ScString name);
  ~AdHealthBudget() override;
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
  CtResidentialLayer *residential;
  EcBudgetLayer *budget;
};

// Actual sixteen-byte phase-three/four objects: city-manipulator primary, reference base+4 and borrowed owner+12.
class AdDemographicsCallback3 : public CtLayerManipulator, public ScRefCount {
public:
  AdDemographicsCallback3(AdDemographicsAdvisor *owner);
  ~AdDemographicsCallback3() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdDemographicsAdvisor *owner;
};

class AdDemographicsCallback4 : public CtLayerManipulator, public ScRefCount {
public:
  AdDemographicsCallback4(AdDemographicsAdvisor *owner);
  ~AdDemographicsCallback4() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  AdDemographicsAdvisor *owner;
};

#endif
