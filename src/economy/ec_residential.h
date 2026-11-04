#ifndef SC3_ECONOMY_EC_RESIDENTIAL_H
#define SC3_ECONOMY_EC_RESIDENTIAL_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/ct_police.h"
#include "city/simulation.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "economy/api.h"
#include "economy/ec_rci_random.h"

class ScOccupant;

// The installed school, college, library, museum and hospital primary identities share these borrowed views.
// School, college and library also have two private trailing slots whose declarations remain unresolved.
class EcCivicView : public ScUnknown {
public:
  virtual BdBuilding *AsISC3Building() = 0;
  virtual ScOccupant *AsISC3Occupant() = 0;
};

// Original education view slots12..32 classify services, capacities, staff, age range and the education contribution.
// Integer enum/signedness and float/double nominal declarations remain unresolved; no foreign vtable overlay is asserted.
class EcEducationService : public ScUnknown {
public:
  virtual u32 GetKind() = 0;
  virtual u32 GetCapacity() = 0;
  virtual u32 GetStaffCount() = 0;
  virtual u8 GetMinimumAge() = 0;
  virtual u8 GetMaximumAge() = 0;
  virtual f32 GetEducationEffect() = 0;
};

// Original hospital view supplies unsigned bed/staff effects and its stored float health contribution.
// The private nominal declarations remain unresolved independently of the proved field/caller behavior.
class EcHealthService : public ScUnknown {
public:
  virtual u32 GetBedCount() = 0;
  virtual u32 GetDoctorCount() = 0;
  virtual f32 GetHealthEffect() = 0;
};

// Ordered service records store borrowed interface pointers; the original library-list sentinel is not a game dependency.
struct EcResidentialServiceEntry {
  EcResidentialServiceEntry *next;
  BdUpkeepView *service;
};

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3DepartmentBudget.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class EcDepartmentBudget : public ScUnknown {

public:
  virtual u32 GetDepartmentID() = 0;
  virtual i64 GetLastYearSurplus() = 0;
  virtual i64 GetTotalFundingNeeded() = 0;
  virtual void SetTotalFundingNeeded(i64) = 0;
  virtual ScStringView *GetDepartmentName() = 0;

  virtual void SetFundingPercentage(u8) = 0;
  virtual u8 GetFundingPercentage() = 0;

  virtual u32 GetAdvisorID() = 0;
  virtual u32 GetAdvisorGroupID() = 0;
};

class EcValve;
class EcResidentialManipulator;
struct EcResidentialServiceEntry;
struct EcResidentialEducationType;
struct EcResidentialHealthEntry;
class EcDepartmentBudget;
class EcBudgetLayer;
class EcLayer;
class CtOrdinanceLayer;
class EcValveLayer;
class ScOccupantManager;

namespace EcRci {
class ScGraphInfo;
}

// Recovered residential and city-layer identities of SIMRCI's cSC3ResidentialLayer.
// These named fields model the reached state, not its 1752-byte binary layout. The population simulation candidate retains unresolved private signature and precision details.
class EcResidential : public CtResidentialLayer, public EcCommandOwner, public CtChangeReceiver, public CtLayer {
public:
  EcResidential();
  virtual ~EcResidential();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
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
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  i32 GetPopulation() override;
  u8 GetWorkforceEQ() override;
  u8 GetWorkforceLE() override;
  bool SchoolIsOnStrike() override;
  bool CollegeIsOnStrike() override;
  bool HealthIsOnStrike() override;
  i32 ChanceOfSchoolStrike() override;
  i32 ChanceOfCollegeStrike() override;
  i32 ChanceOfHealthStrike() override;
  bool GetStrikingSchoolBuildings(ScOccupant **out, u32 capacity, u32 *count) override;
  bool GetStrikingCollegeBuildings(ScOccupant **out, u32 capacity, u32 *count) override;
  bool GetStrikingHealthBuildings(ScOccupant **out, u32 capacity, u32 *count) override;
  void EndSchoolStrike() override;
  void EndCollegeStrike() override;
  void EndHealthStrike() override;
  u8 GetHealthSystemRating() override;
  u8 GetSchoolSystemRating() override;
  u8 GetCollegeSystemRating() override;
  i32 GetPatientCount() override;
  i32 GetDoctorCount() override;
  i32 GetSchoolStudentCount() override;
  i32 GetTeacherCount() override;
  i32 GetCollegeStudentCount() override;
  i32 GetProfessorCount() override;
  i32 GetHealthSystemCapacity() override;
  i32 GetSchoolSystemCapacity() override;
  i32 GetCollegeSystemCapacity() override;
  i32 GetLibrarySystemCapacity() override;
  i32 GetMuseumSystemCapacity() override;
  u8 GetDevelopmentCap() override;
  f32 GetWorkforcePopPct() override;
  f32 GetAgeCohortPopPct(u8 first, u8 last) override;
  u8 GetAgeCohortEQ(u8 first, u8 last) override;
  u8 GetAgeCohortLE(u8 first, u8 last) override;
  i32 GetTotalCityEducationUpkeep() override;
  i32 GetTotalCityHealthUpkeep() override;
  void SetEducationFundingPercentage(u8 percentage) override;
  void SetHealthFundingPercentage(u8 percentage) override;

protected:
  friend class EcResidentialManipulator;
  // Original24347 population simulation has an unresolved unused nominal return and x87 precision details.
  void SimulatePopulation();
  void InitializeCohorts();
  // Original 10022AC6 returns raw AL; its private Boolean/BYTE spelling remains unresolved.
  u8 LoadTuning();
  void InitializeGraphs();
  void RefreshGraphLabels();
  void ClearEducationAndHealth();
  void ClearUpkeepServices();
  u8 OnWindowCreate(ScMessage *message, ScUnknown *source);
  u8 UpdateBuilding(u32 operation, ScOccupant *occupant);
  CtCity *city;
  EcLayer *pollution;
  EcValveLayer *valve_layer;
  CtOrdinanceLayer *ordinances;
  EcValve *residential; // Complete+36, not the total-population valve at+40.
  EcValve *total_valve;
  ScOccupantManager *occupant_manager;
  ScLock service_lock;    // Actual recursive service/list lock at complete+48.
  EcRci::ScRandom random; // Original complete+104; separate integer and floating streams.
  u32 baby_trend;         // Complete+128; 0 rises, 1 falls and 2 holds the newborn-population trend.
  f32 education[90], health_factor[90], cohort_population[90], health[90]; // Complete+132,+492,+852,+1212.
  i32 education_upkeep, health_upkeep;                                     // Complete+1576,+1600.
  u8 education_funding, health_funding;                                    // Complete+1580,+1604.
  EcResidentialServiceEntry *schools, *colleges, *other_education,
      *hospitals;                                            // Original lists+1588,+1592,+1612; native empty heads.
  EcResidentialEducationType *education_types;               // Ordered service groups at complete+84.
  EcResidentialHealthEntry *health_services;                 // Borrowed health interfaces at complete+96.
  u8 population_flag;                                        // Original complete+100; meaning remains unresolved.
  EcDepartmentBudget *education_budget, *health_budget;      // Retained department views at complete+1584,+1608.
  EcBudgetLayer *budget;                                     // Original complete+1572.
  bool school_strike, college_strike, health_strike;         // Complete+1616..1618.
  u32 school_strike_id, college_strike_id, health_strike_id; // Complete+1620,+1624,+1628.
  u8 school_rating, college_rating, health_rating;           // Complete+1632..1634.
  i32 patients, health_capacity, school_students, school_capacity, college_students, college_capacity;
  i32 library_capacity, museum_capacity, teachers, professors, doctors; // Complete+1636..1676 in this order.
  EcRci::ScGraphInfo *first_graph, *third_graph, *second_graph;         // Original complete+1680,+1684,+1688.
  ScString first_graph_format, third_graph_format, second_graph_format; // Complete+1692,+1712,+1732.
  // Original mutable tuning bytes+57C74/75/7C/7D are reloaded by StaticInit.
  static u8 education_strike_funding, education_strike_rating, health_strike_funding, health_strike_rating;
  static f32 newborn_population, cohort_decay, education_decay, optimal_health_rate;
  static i32 external_fun_supply, education_strike_duration, health_strike_duration;
  static f32 minimum_newborn_population, maximum_newborn_population, baby_trend_probability, health_decay;
  static f32 optimal_school_rate, optimal_college_rate, hospitalization_rate, youth_curfew_effect;
  static f32 reading_adult_effect, reading_child_effect, junior_sports_effect, crossing_guards_effect;
  static f32 free_clinics_effect, cpr_training_effect, smoking_ban_effect;
  static f64 pollution_factor;
  static u8 age_of_rebellion, youth_curfew_min_age, youth_curfew_max_age, reading_min_age, reading_max_age;
  static u8 junior_sports_min_age, junior_sports_max_age, education_strike_efficiency, health_strike_efficiency;
};

class CtCity;
class CtAgentTree;
class OcAttribCache;
class GeBuildingLayer;
class ScOccupantManager;
class CtZoneLayer;
class CtLandValueLayer;
class NwTrafficLayer;
class EcLayer;
class CtOrdinanceLayer;
class CtPowerLayer;
class CtPlumbingLayer;
class EcValve;
class EcZoneRule;
class BdBuilding;
struct ScPointXZ;
struct GeCellRect;
struct EcGrowthIndex;
struct EcGrowthRecord;

// SIMRCI residential zone-developer receiver. Private nominal returns remain unresolved until further source evidence.
class EcResidentialDeveloper : public CtZoneDeveloper, public EcCommandOwner {
public:
  EcResidentialDeveloper();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init(CtCity *city, const char *growth_data) override;
  u8 Shutdown() override;
  bool BindDemand() override;
  // Zone simulation passes the actual X/Z point and consumes the resulting development-result integer.
  i32 Evaluate(const ScPointXZ &cell) override;
  u8 AddRule(EcZoneRule *rule) override;
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) override;
  virtual ~EcResidentialDeveloper();

private:
  // Original private identifier return and second-byte nominal types remain unresolved.
  u32 FindGrowthType(u32 identifier);
  u32 SelectGrowth(u8 value, u8 include_all);
  u32 SelectHigherValue(u32 current, u8 value, u8 include_all);
  u32 SelectLowerValue(u32 current, u8 value, u8 include_all);
  u32 SelectHigherDensity(u32 current, u8 value);
  u8 TryRules(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum);
  u8 TryDevelop(const ScPointXZ &point, u32 identifier, u32 existing, u32 distance);
  u8 ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state);
  u8 initialized;
  EcGrowthIndex *growth_by_minimum, *growth_by_class;
  EcGrowthRecord *growth_records;
  u8 minimum_growth, maximum_growth;
  ScArray<EcZoneRule *> rules;
  EcRci::ScRandom random;
  CtCity *city;
  OcAttribCache *attributes;
  CtAgentTree *agent_tree;
  GeBuildingLayer *buildings;
  CtPowerLayer *power;
  CtPlumbingLayer *water;
  ScOccupantManager *occupants;
  EcValve *residential_valve;
  CtZoneLayer *zones;
  CtLandValueLayer *land_value;
  NwTrafficLayer *traffic;
  EcLayer *pollution;
  CtOrdinanceLayer *ordinances;
};

class EcResidential;

// Actual residential monthly manipulator with its separate lifetime base and retained residential owner.
class EcResidentialManipulator : public CtLayerManipulator, public EcCommandOwner {
public:
  EcResidentialManipulator();
  virtual ~EcResidentialManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Private effects are known; their nominal bool/BYTE source declarations remain unresolved.
  u8 Init(EcResidential *owner);
  u8 Shutdown();

private:
  EcResidential *layer;
};

// These records replace the original ordered type tree and linked lists without retaining the borrowed interfaces.
struct EcResidentialEducationEntry {
  EcResidentialEducationEntry *next;
  EcEducationService *service;
};

struct EcResidentialEducationType {
  EcResidentialEducationType *next;
  u32 type;
  EcResidentialEducationEntry *services;
  u32 unknown_count; // Original field exists; its nominal meaning is unresolved.
  f32 cohort_effect[90];
  u32 cohort_count[90];
};

struct EcResidentialHealthEntry {
  EcResidentialHealthEntry *next;
  EcHealthService *service;
};

#endif
