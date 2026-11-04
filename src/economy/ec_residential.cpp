#include "economy/ec_residential.h"

#include <assert.h>
#include <string.h>

#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_graph_info.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_text.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"

// Residential graph models publish residents, education and health with distinct label IDs and colors.
enum EcResidentialGraphUi : u32 {
  kEcResidentsGraphSelectionId = 0x303e40d,
  kEcResidentsGraphValueId = 0xc303e41d,
  kEcEducationGraphSelectionId = 0x4303e4e7,
  kEcEducationGraphValueId = 0x303e4f0,
  kEcHealthGraphSelectionId = 0x6303e4f7,
  kEcHealthGraphValueId = 0x2303e4ff,
  kEcResidentsGraphColor = 0x9000,
  kEcEducationGraphColor = 0xaf6050,
  kEcHealthGraphColor = 0x20c060,
};

// Installed mutable defaults; StaticInit reloads their configured values before normal simulation.
u8 EcResidential::education_strike_funding = 80;
u8 EcResidential::education_strike_rating = 3;
u8 EcResidential::health_strike_funding = 80;
u8 EcResidential::health_strike_rating = 3;
f32 EcResidential::newborn_population = 0.0136f;
f32 EcResidential::cohort_decay = 0.997f;
f32 EcResidential::education_decay = 0.999f;
f32 EcResidential::optimal_health_rate = 0.3f;
i32 EcResidential::external_fun_supply = 25000;
i32 EcResidential::education_strike_duration = 26, EcResidential::health_strike_duration = 20;
f32 EcResidential::minimum_newborn_population = 0.0f, EcResidential::maximum_newborn_population = 0.0f;
f32 EcResidential::baby_trend_probability = 0.2f, EcResidential::health_decay = 0.999f;
f32 EcResidential::optimal_school_rate = 1.0f, EcResidential::optimal_college_rate = 1.4f;
f32 EcResidential::hospitalization_rate = 0.05f, EcResidential::youth_curfew_effect = 0.001f;
f32 EcResidential::reading_adult_effect = 0.01f, EcResidential::reading_child_effect = 0.02f;
f32 EcResidential::junior_sports_effect = 0.004f, EcResidential::crossing_guards_effect = 0.001f;
f32 EcResidential::free_clinics_effect = 0.007f, EcResidential::cpr_training_effect = 0.002f;
f32 EcResidential::smoking_ban_effect = 0.004f;
f64 EcResidential::pollution_factor = 0.000003125;
u8 EcResidential::age_of_rebellion = 15, EcResidential::youth_curfew_min_age = 1,
   EcResidential::youth_curfew_max_age = 21;
u8 EcResidential::reading_min_age = 4, EcResidential::reading_max_age = 16;
u8 EcResidential::junior_sports_min_age = 8, EcResidential::junior_sports_max_age = 16;
u8 EcResidential::education_strike_efficiency = 20, EcResidential::health_strike_efficiency = 20;

// Native records replace the original library-container storage.
// Initialize the reached native fields and empty service lists in the same logical state as the original constructor.
EcResidential::EcResidential()
    : city(nullptr), pollution(nullptr), valve_layer(nullptr), ordinances(nullptr), residential(nullptr),
      total_valve(nullptr), occupant_manager(nullptr), random(kScRandomSeedFromClock), education_upkeep(0),
      health_upkeep(0), education_funding(0), health_funding(0), schools(nullptr), colleges(nullptr),
      other_education(nullptr), hospitals(nullptr), education_types(nullptr), health_services(nullptr),
      population_flag(0), education_budget(nullptr), health_budget(nullptr), budget(nullptr), school_strike_id(0),
      college_strike_id(0), health_strike_id(0), first_graph(nullptr), third_graph(nullptr), second_graph(nullptr) {
}

EcResidential::~EcResidential() {
  EcResidential::Shutdown();
  ClearUpkeepServices();
}

// Initialize the age distributions from the live tuning values, then normalize the first 65 population cohorts.
void EcResidential::InitializeCohorts() {
  baby_trend = 0;
  f64 population_sum = 0.0;
  for (int age = 0; age < 90; ++age) {
    education[age] = age <= 20 ? f32(85 * age / 20) : f32(f64(education_decay) * education[age - 1]);
    if (age == 0) {
      cohort_population[age] = newborn_population;
      health[age] = 65.0f;
      health_factor[age] = f32(f64(optimal_health_rate) * 55.625);
    } else {
      cohort_population[age] = age < 65 ? f32(f64(cohort_decay) * cohort_population[age - 1]) : 0.0f;
      f64 health_value = f64(0.9969672560691833f) * health[age - 1];
      health[age] = f32(health_value);
      f64 rate = (health_value - 50.0) * optimal_health_rate * f64(3.7083332538604736f);
      health_factor[age] = rate >= 0.0 ? f32(rate) : 0.0f;
    }
    population_sum += cohort_population[age];
  }
  if (population_sum < 1.0 || population_sum > 1.0) {
    f64 adjustment = (1.0 - population_sum) * f64(0.015384615398943424f);
    for (int age = 0; age < 65; ++age)
      cohort_population[age] = f32(adjustment + cohort_population[age]);
  }
}

// Publish the actual primary residential or secondary city-layer identity before acquiring its shared reference.
bool EcResidential::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtResidentialLayer);
  HANDLE_QUERY_INTERFACE(IID_CtResidentialLayer, CtResidentialLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcResidential::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcResidential::Release() {
  return EcCommandOwner::Release();
}

bool EcResidential::DoMessage(ScMessage *) {
  return false;
}

// Dispatch a graph collection request or deliver the three current models in their original callback order.
// A model notification is acknowledged even when the sender is absent or rejects its queried interface.
bool EcResidential::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  if (message->type == kWnMsg_QueryOpened)
    return OnWindowCreate(message, source) != 0;
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    void *selected = nullptr;
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(
          first_graph ? static_cast<ScGraphInfoView *>(first_graph) : nullptr);
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(
          second_graph ? static_cast<ScGraphInfoView *>(second_graph) : nullptr);
      static_cast<ScModelReceiver *>(selected)->DumpGraphData(
          third_graph ? static_cast<ScGraphInfoView *>(third_graph) : nullptr);
      static_cast<ScUnknown *>(selected)->Release();
    }
    return true;
  }
  return false;
}

// Add the three current graph views to the requested collection and release the acquired collection afterward.
u8 EcResidential::OnWindowCreate(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  if (u32(message->data1) != 1541 || !source || !source->QueryInterface(IID_UiGraphsViewIdentity, &selected))
    return 0;
  static_cast<ScObjectCollection *>(selected)->Add(first_graph ? static_cast<ScGraphInfoView *>(first_graph) : nullptr);
  static_cast<ScObjectCollection *>(selected)->Add(
      second_graph ? static_cast<ScGraphInfoView *>(second_graph) : nullptr);
  static_cast<ScObjectCollection *>(selected)->Add(third_graph ? static_cast<ScGraphInfoView *>(third_graph) : nullptr);
  static_cast<ScUnknown *>(selected)->Release();
  return 1;
}

bool EcResidential::SimulationEnd() {
  return true;
}

u32 EcResidential::GetLayerType() {
  return kEcResidentialLayerType;
}

bool EcResidential::DebugClassTag(ScStringView *) {
  return false;
}

bool EcResidential::DebugTypeTag(ScStringView *) {
  return false;
}

bool EcResidential::SchoolIsOnStrike() {
  return school_strike;
}

bool EcResidential::CollegeIsOnStrike() {
  return college_strike;
}

bool EcResidential::HealthIsOnStrike() {
  return health_strike;
}

i32 EcResidential::ChanceOfSchoolStrike() {
  if (school_strike)
    return 10000;
  if (school_rating < education_strike_rating && education_funding < education_strike_funding && schools) {
    i32 difference = education_strike_funding - education_funding;
    return difference * difference;
  }
  return 0;
}

i32 EcResidential::ChanceOfCollegeStrike() {
  if (college_strike)
    return 10000;
  if (college_rating < education_strike_rating && education_funding < education_strike_funding && colleges) {
    i32 difference = education_strike_funding - education_funding;
    return difference * difference;
  }
  return 0;
}

i32 EcResidential::ChanceOfHealthStrike() {
  if (health_strike)
    return 10000;
  if (health_rating < health_strike_rating && health_funding < health_strike_funding && hospitals) {
    i32 difference = health_strike_funding - health_funding;
    return difference * difference;
  }
  return 0;
}

// Clear the active school strike before posting its original notification with the saved strike identifier.
void EcResidential::EndSchoolStrike() {
  if (school_strike) {
    school_strike = false;
    ScMessage message(kEcMsg_SchoolStrikeChanged, 0, school_strike_id, 0);
    EcGetRciMessageServer()->MessagePost(message, false);
  }
}

// Clear the active college strike before posting its original notification with the saved strike identifier.
void EcResidential::EndCollegeStrike() {
  if (college_strike) {
    college_strike = false;
    ScMessage message(kEcMsg_CollegeStrikeChanged, 0, college_strike_id, 0);
    EcGetRciMessageServer()->MessagePost(message, false);
  }
}

// Clear the active health strike before posting its original notification with the saved strike identifier.
void EcResidential::EndHealthStrike() {
  if (health_strike) {
    health_strike = false;
    ScMessage message(kEcMsg_HealthStrikeChanged, 0, health_strike_id, 0);
    EcGetRciMessageServer()->MessagePost(message, false);
  }
}

u8 EcResidential::GetHealthSystemRating() {
  return health_rating;
}

u8 EcResidential::GetSchoolSystemRating() {
  return school_rating;
}

u8 EcResidential::GetCollegeSystemRating() {
  return college_rating;
}

i32 EcResidential::GetPatientCount() {
  return patients;
}

i32 EcResidential::GetDoctorCount() {
  return doctors;
}

i32 EcResidential::GetSchoolStudentCount() {
  return school_students;
}

i32 EcResidential::GetTeacherCount() {
  return teachers;
}

i32 EcResidential::GetCollegeStudentCount() {
  return college_students;
}

i32 EcResidential::GetProfessorCount() {
  return professors;
}

i32 EcResidential::GetHealthSystemCapacity() {
  return health_capacity;
}

i32 EcResidential::GetSchoolSystemCapacity() {
  return school_capacity;
}

i32 EcResidential::GetCollegeSystemCapacity() {
  return college_capacity;
}

i32 EcResidential::GetLibrarySystemCapacity() {
  return library_capacity;
}

i32 EcResidential::GetMuseumSystemCapacity() {
  return museum_capacity;
}

i32 EcResidential::GetTotalCityEducationUpkeep() {
  return education_upkeep;
}

i32 EcResidential::GetTotalCityHealthUpkeep() {
  return health_upkeep;
}

u8 EcResidential::GetDevelopmentCap() {
  return residential->GetDemandCap();
}

i32 EcResidential::GetPopulation() {
  return residential ? residential->QuerySupplyValue() : 0;
}

f32 EcResidential::GetWorkforcePopPct() {
  return GetAgeCohortPopPct(20, 65);
}

// Order the endpoints, cap only the upper age at 89, and sum the inclusive population fractions.
f32 EcResidential::GetAgeCohortPopPct(u8 first, u8 last) {
  if (first > last) {
    u8 saved = first;
    first = last;
    last = saved;
  }
  if (last > 89)
    last = 89;
  f64 total = 0;
  for (u32 age = first; age <= last; ++age)
    total += cohort_population[age];
  return f32(total);
}

u8 EcResidential::GetWorkforceEQ() {
  return GetAgeCohortEQ(20, 65);
}

u8 EcResidential::GetWorkforceLE() {
  return GetAgeCohortLE(20, 65);
}

// Sum education only for populated ages, preserving the original unswapped, unclamped divisor.
// Return the low byte of the biased rounded double, including the original reversed-endpoint behavior.
u8 EcResidential::GetAgeCohortEQ(u8 first, u8 last) {
  u8 low = first, high = last;
  if (low > high) {
    low = last;
    high = first;
  }
  if (high > 89)
    high = 89;
  i32 count = i32(last) - i32(first) + 1;
  f64 total = 0;
  for (u32 age = low; age <= high; ++age)
    if (cohort_population[age] > 0)
      total += education[age];
    else
      --count;
  if (!count)
    return 0;
  f64 rounded = total / count + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u8(bits);
}

// Sum health only for populated ages, preserving the original unswapped, unclamped divisor.
// Return the low byte of the biased rounded double, including the original reversed-endpoint behavior.
u8 EcResidential::GetAgeCohortLE(u8 first, u8 last) {
  u8 low = first, high = last;
  if (low > high) {
    low = last;
    high = first;
  }
  if (high > 89)
    high = 89;
  i32 count = i32(last) - i32(first) + 1;
  f64 total = 0;
  for (u32 age = low; age <= high; ++age)
    if (cohort_population[age] > 0)
      total += health[age];
    else
      --count;
  if (!count)
    return 0;
  f64 rounded = total / count + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u8(bits);
}

// Publish the requested funding before taking the service lock, then accumulate original full funding needs.
// Update each live service in list order; reread its interface after the funding-need callback.
void EcResidential::SetEducationFundingPercentage(u8 percentage) {
  education_upkeep = 0;
  education_funding = percentage;
  service_lock.Lock();
  for (EcResidentialServiceEntry *entry = schools; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    education_upkeep = i32(u32(education_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(percentage);
  }
  for (EcResidentialServiceEntry *entry = colleges; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    education_upkeep = i32(u32(education_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(percentage);
  }
  for (EcResidentialServiceEntry *entry = other_education; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    education_upkeep = i32(u32(education_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(percentage);
  }
  service_lock.Unlock();
}

// Publish the requested funding before taking the service lock, then accumulate original full funding needs.
// Update each live service in list order; reread its interface after the funding-need callback.
void EcResidential::SetHealthFundingPercentage(u8 percentage) {
  health_upkeep = 0;
  health_funding = percentage;
  service_lock.Lock();
  for (EcResidentialServiceEntry *entry = hospitals; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    health_upkeep = i32(u32(health_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(percentage);
  }
  service_lock.Unlock();
}

// An inactive strike leaves both outputs untouched. Under the service lock, cap the initial list length and retain
// each published occupant in order, rereading links after callbacks before publishing the actual output count.
bool EcResidential::GetStrikingSchoolBuildings(ScOccupant **out, u32 capacity, u32 *count) {
  if (!school_strike)
    return false;
  service_lock.Lock();
  u32 limit = 0;
  for (EcResidentialServiceEntry *entry = schools; entry; entry = entry->next)
    ++limit;
  if (limit > capacity)
    limit = capacity;
  u32 written = 0;
  for (EcResidentialServiceEntry *entry = schools; entry && written < limit; entry = entry->next) {
    ScOccupant *occupant = entry->service->AsISC3Occupant();
    ++written;
    *out++ = occupant;
    occupant->AddRef();
  }
  *count = written;
  service_lock.Unlock();
  return true;
}

// An inactive strike leaves both outputs untouched. Under the service lock, cap the initial list length and retain
// each published occupant in order, rereading links after callbacks before publishing the actual output count.
bool EcResidential::GetStrikingCollegeBuildings(ScOccupant **out, u32 capacity, u32 *count) {
  if (!college_strike)
    return false;
  service_lock.Lock();
  u32 limit = 0;
  for (EcResidentialServiceEntry *entry = colleges; entry; entry = entry->next)
    ++limit;
  if (limit > capacity)
    limit = capacity;
  u32 written = 0;
  for (EcResidentialServiceEntry *entry = colleges; entry && written < limit; entry = entry->next) {
    ScOccupant *occupant = entry->service->AsISC3Occupant();
    ++written;
    *out++ = occupant;
    occupant->AddRef();
  }
  *count = written;
  service_lock.Unlock();
  return true;
}

// An inactive strike leaves both outputs untouched. Under the service lock, cap the initial list length and retain
// each published occupant in order, rereading links after callbacks before publishing the actual output count.
bool EcResidential::GetStrikingHealthBuildings(ScOccupant **out, u32 capacity, u32 *count) {
  if (!health_strike)
    return false;
  service_lock.Lock();
  u32 limit = 0;
  for (EcResidentialServiceEntry *entry = hospitals; entry; entry = entry->next)
    ++limit;
  if (limit > capacity)
    limit = capacity;
  u32 written = 0;
  for (EcResidentialServiceEntry *entry = hospitals; entry && written < limit; entry = entry->next) {
    ScOccupant *occupant = entry->service->AsISC3Occupant();
    ++written;
    *out++ = occupant;
    occupant->AddRef();
  }
  *count = written;
  service_lock.Unlock();
  return true;
}

bool EcResidential::StaticShutdown() {
  return true;
}

// Import legacy residential supply and scale its demand adjustment against the live valve limit.
// Failed properties become zero independently; the product and supply/demand sums wrap before signed division.
bool EcResidential::Init(CtCity *, ScImporter *importer) {
  u32 adjustment_bits, population = 0;
  if (importer->MapIsPresent(kCtLegacyMiscPlane)) {
    if (!importer->GetMiscValue(454, reinterpret_cast<i32 *>(&adjustment_bits)))
      adjustment_bits = 0;
    u32 first, second;
    if (!importer->GetMiscValue(1032, reinterpret_cast<i32 *>(&first)))
      first = 0;
    if (!importer->GetMiscValue(1035, reinterpret_cast<i32 *>(&second)))
      second = 0;
    population = first + second;
  } else
    adjustment_bits = 0;
  i32 adjustment = i32(adjustment_bits);
  i32 limit = adjustment < 0 ? residential->QueryValveMin() : residential->QueryValveMax();
  i32 divisor = adjustment < 0 ? -20000 : 20000;
  i32 product = i32(u32(adjustment) * u32(limit));
  i32 correction = product / divisor;
  residential->SetSupplyValue(i32(population));
  residential->SetDemandValue(i32(population + u32(correction)));
  return true;
}

// Publish flags and a null result before inspecting the agent type.
bool EcResidential::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    EcResidentialManipulator *manipulator = new EcResidentialManipulator;
    manipulator->Init(this);
    *out = manipulator;
  }
  return true;
}

static u8 EcReadResidentialTuning(EcIni *ini, const char *name, ScString *value) {
  ScString key(name, u32(strlen(name))), section("TuningParameters", 16);
  return ini->GetValue(&section, &key, value);
}

bool EcResidential::StaticInit(CtDefinition *) {
  return LoadTuning() != 0;
}

// Reload residential tuning in the original key order, preserving absent defaults and original integer conversions.
u8 EcResidential::LoadTuning() {
  ScString path, value;
  EcIni ini;
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SC3ResLayer.ini", 19);
  ini.SelectFile(&path);
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK", 12);
  ini.SelectPackage(path);
  if (EcReadResidentialTuning(&ini, "ExternalFunSupply", &value)) {
    external_fun_supply = i32(EcParseTuningNumber(value.Data()));
    if (external_fun_supply < 0)
      external_fun_supply = 0;
  }
  if (EcReadResidentialTuning(&ini, "NewbornPopPct", &value))
    newborn_population = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MinNewbornPopPct", &value))
    minimum_newborn_population = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MaxNewbornPopPct", &value))
    maximum_newborn_population = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "AgeCohortDecay", &value))
    cohort_decay = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "BabyTrendChangeProb", &value))
    baby_trend_probability = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "EQDecay", &value))
    education_decay = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "HQDecay", &value))
    health_decay = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "OptimalYearlyHQ", &value))
    optimal_health_rate = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.01f));
  if (EcReadResidentialTuning(&ini, "OptimalYearlySchoolEQ", &value))
    optimal_school_rate = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.01f));
  if (EcReadResidentialTuning(&ini, "OptimalYearlyCollegeEQ", &value))
    optimal_college_rate = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.01f));
  if (EcReadResidentialTuning(&ini, "HospitalizationRate", &value))
    hospitalization_rate = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "PollutionFactor", &value))
    pollution_factor = f64(EcParseTuningNumber(value.Data())) * 0.00000001;
  if (EcReadResidentialTuning(&ini, "AgeOfRebellion", &value))
    age_of_rebellion = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MonthlyYouthCurfewOrdEffect", &value))
    youth_curfew_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "YouthCurfewOrdMinAge", &value))
    youth_curfew_min_age = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "YouthCurfewOrdMaxAge", &value))
    youth_curfew_max_age = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MonthlyReadingOrdEffectAdult", &value))
    reading_adult_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MonthlyReadingOrdEffectChild", &value))
    reading_child_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "ReadingOrdMinChildAge", &value))
    reading_min_age = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "ReadingOrdMaxChildAge", &value))
    reading_max_age = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MonthlyJrSportsOrdEffect", &value))
    junior_sports_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "JrSportsOrdMinAge", &value))
    junior_sports_min_age = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "JrSportsOrdMaxAge", &value))
    junior_sports_max_age = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MonthlyCrossingGuardsOrdEffect", &value))
    crossing_guards_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MonthlyFreeClinicsOrdEffect", &value))
    free_clinics_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MonthlyCPRTrainingOrdEffect", &value))
    cpr_training_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MonthlySmokingBanOrdEffect", &value))
    smoking_ban_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.0001f));
  if (EcReadResidentialTuning(&ini, "MinAcceptableEducationFunding", &value))
    education_strike_funding = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MinAcceptableEducationRating", &value))
    education_strike_rating = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "EducationStrikeEfficiency", &value))
    education_strike_efficiency = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MaxEducationStrikeDuration", &value))
    education_strike_duration = i32(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MinAcceptableHealthFunding", &value))
    health_strike_funding = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MinAcceptableHealthRating", &value))
    health_strike_rating = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "HealthStrikeEfficiency", &value))
    health_strike_efficiency = u8(EcParseTuningNumber(value.Data()));
  if (EcReadResidentialTuning(&ini, "MaxHealthStrikeDuration", &value))
    health_strike_duration = i32(EcParseTuningNumber(value.Data()));
  return 1;
}

// Original developer growth records contain one identifier and three independent byte properties.
struct EcGrowthRecord {
  EcGrowthRecord *next;
  u32 identifier;
  u8 category, maximum, minimum;
};

// The two original ordered indexes retain duplicate keys and each points to a growth identifier.
struct EcGrowthIndex {
  EcGrowthIndex *next;
  u32 key, identifier;
};

static bool EcResidentialDeveloperTuningLoaded;
static u32 EcResidentialDeveloperTuning[7] = {15, 4, 75, 30, 10, 30, 10};

// Construct the separate SIMRCI lifetime base and empty growth tables, then load the residential tuning keys once.
EcResidentialDeveloper::EcResidentialDeveloper()
    : initialized(0), growth_by_minimum(nullptr), growth_by_class(nullptr), growth_records(nullptr),
      minimum_growth(255), maximum_growth(0), random(kScRandomSeedFromClock), city(nullptr), attributes(nullptr),
      agent_tree(nullptr), buildings(nullptr), power(nullptr), water(nullptr), occupants(nullptr),
      residential_valve(nullptr), zones(nullptr), land_value(nullptr), traffic(nullptr), pollution(nullptr),
      ordinances(nullptr) {
  if (!EcResidentialDeveloperTuningLoaded) {
    static const char *const keys[] = {"NoServicePercentage", "MaxDistFromTransport", "ConstructionTimeFactor",
        "HigherLandValueRedevelopmentPct", "LowerLandValueRedevelopmentPct", "HigherDensityRedevelopmentPct",
        "AnarchyFactor"};
    ScString path, value;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI");
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    ScString section("ResidentialZoneDeveloper");
    for (u32 index = 0; index < 7; ++index) {
      ScString key(keys[index], u32(strlen(keys[index])));
      if (ini.GetValue(&section, &key, &value))
        EcResidentialDeveloperTuning[index] = EcParseTuningNumber(value.Data());
    }
    EcResidentialDeveloperTuningLoaded = true;
  }
}

// Shut down an initialized developer before retiring its original three owned growth collections and lifetime base.
EcResidentialDeveloper::~EcResidentialDeveloper() {
  if (initialized)
    Shutdown();
  while (growth_records) {
    EcGrowthRecord *entry = growth_records;
    growth_records = entry->next;
    delete entry;
  }
  while (growth_by_class) {
    EcGrowthIndex *entry = growth_by_class;
    growth_by_class = entry->next;
    delete entry;
  }
  while (growth_by_minimum) {
    EcGrowthIndex *entry = growth_by_minimum;
    growth_by_minimum = entry->next;
    delete entry;
  }
}

// Clear unsupported output and retain the primary developer identity for Unknown or its original IID.
bool EcResidentialDeveloper::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtZoneDeveloper);
  HANDLE_QUERY_INTERFACE(IID_CtZoneDeveloper, CtZoneDeveloper);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcResidentialDeveloper::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcResidentialDeveloper::Release() {
  return EcCommandOwner::Release();
}

bool EcResidentialDeveloper::BindDemand() {
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  EcValveLayer *demand = nullptr;
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand)))
    return false;
  bool result = demand->GetValvePointer(kEcResidentialValveId, IID_EcValve,
      reinterpret_cast<void **>(&residential_valve));
  demand->Release();
  return result;
}

// Retain the actual city services in original order and parse the supplied residential growth rows.
// Growth bounds use the parsed low bytes; a missing service leaves prior acquisitions retained until Shutdown.
u8 EcResidentialDeveloper::Init(CtCity *owner, const char *growth_data) {
  if (initialized)
    Shutdown();
  initialized = 1;
  city = owner;
  if (!city)
    return 0;
  city->AddRef();
  attributes = EcGetRciApplication()->GetAppOccupantAttribCache();
  if (!attributes)
    return 0;
  attributes->AddRef();
  agent_tree = EcGetRciApplication()->GetAppAgentTypeTree();
  if (!agent_tree)
    return 0;
  agent_tree->AddRef();
  buildings = city->BuildingLayer();
  if (!buildings)
    return 0;
  buildings->AddRef();
  power = city->PowerLayer();
  if (!power)
    return 0;
  power->AddRef();
  water = city->PlumbingLayer();
  if (!water)
    return 0;
  water->AddRef();
  occupants = city->SurfaceOccupantManager();
  if (!occupants)
    return 0;
  occupants->AddRef();
  zones = city->ZoneLayer();
  if (!zones)
    return 0;
  zones->AddRef();
  traffic = city->TrafficLayer();
  if (!traffic)
    return 0;
  traffic->AddRef();
  land_value = city->LandValueLayer();
  if (!land_value)
    return 0;
  land_value->AddRef();
  pollution = city->PollutionLayer();
  if (!pollution)
    return 0;
  pollution->AddRef();
  ordinances = city->OrdinanceLayer();
  if (!ordinances)
    return 0;
  ordinances->AddRef();
  ScString remaining(growth_data), field = remaining.SplitToken(",");
  field = remaining.SplitToken(":");
  u32 count = 0;
  bool done = false;
  do {
    field = remaining.SplitToken(",");
    if (!field.Strlen())
      break;
    u32 identifier = EcParseTuningNumber(field.Data());
    field = remaining.SplitToken(",");
    u8 category = u8(EcParseTuningNumber(field.Data()));
    field = remaining.SplitToken(",");
    u8 maximum = u8(EcParseTuningNumber(field.Data()));
    field = remaining.SplitToken(";");
    if (!field.Strlen()) {
      field = remaining;
      done = true;
    }
    u8 minimum = u8(EcParseTuningNumber(field.Data()));
    if (minimum < minimum_growth)
      minimum_growth = minimum;
    if (maximum > maximum_growth)
      maximum_growth = maximum;
    EcGrowthIndex **index = &growth_by_minimum;
    while (*index && (*index)->key <= minimum)
      index = &(*index)->next;
    *index = new EcGrowthIndex{*index, minimum, identifier};
    index = &growth_by_class;
    while (*index && (*index)->key <= category)
      index = &(*index)->next;
    *index = new EcGrowthIndex{*index, category, identifier};
    EcGrowthRecord **record = &growth_records;
    while (*record && (*record)->identifier < identifier)
      record = &(*record)->next;
    if (!*record || (*record)->identifier != identifier)
      *record = new EcGrowthRecord{*record, identifier, 0, 0, 0};
    (*record)->category = category;
    (*record)->maximum = maximum;
    (*record)->minimum = minimum;
    ++count;
  } while (!done);
  return count != 0;
}

// Select the matching growth entry with the greatest minimum, preserving the original class-index iteration order.
u32 EcResidentialDeveloper::SelectGrowth(u8 value, u8 include_all) {
  u32 selected = 0;
  u8 best_minimum = 0;
  for (EcGrowthIndex *index = growth_by_class; index; index = index->next) {
    EcGrowthRecord *record = growth_records;
    while (record->identifier != index->identifier)
      record = record->next;
    if (record->minimum <= value && value <= record->maximum && record->minimum > best_minimum &&
        (include_all || record->category == 1)) {
      best_minimum = record->minimum;
      selected = index->identifier;
    }
  }
  return selected;
}

// Retain a real development rule, set its configured transport distance and append it to the owner's rule list.
u8 EcResidentialDeveloper::AddRule(EcZoneRule *rule) {
  if (!rule)
    return 0;
  rule->AddRef();
  rule->SetMaxDistance(EcResidentialDeveloperTuning[1]);
  *rules.Append() = rule;
  return 1;
}

u8 EcResidentialDeveloper::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 EcResidentialDeveloper::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

// Release each retained city service in its original order, clear the growth tables, then retire rules in reverse order.
u8 EcResidentialDeveloper::Shutdown() {
  if (initialized) {
    initialized = 0;
    ScRelease(&city);
    ScRelease(&attributes);
    ScRelease(&agent_tree);
    ScRelease(&buildings);
    ScRelease(&power);
    ScRelease(&water);
    ScRelease(&occupants);
    ScRelease(&residential_valve);
    ScRelease(&zones);
    ScRelease(&traffic);
    ScRelease(&land_value);
    ScRelease(&pollution);
    ScRelease(&ordinances);
    while (growth_by_minimum) {
      EcGrowthIndex *entry = growth_by_minimum;
      growth_by_minimum = entry->next;
      delete entry;
    }
    while (growth_by_class) {
      EcGrowthIndex *entry = growth_by_class;
      growth_by_class = entry->next;
      delete entry;
    }
    while (growth_records) {
      EcGrowthRecord *entry = growth_records;
      growth_records = entry->next;
      delete entry;
    }
    while (rules.size) {
      EcZoneRule *rule = rules.data[rules.size - 1];
      if (rule) {
        rule->Shutdown();
        rule->Release();
      }
      --rules.size;
    }
  }
  return 1;
}

// Try up to four distinct registered development rules in random order, stopping at the first accepted placement.
u8 EcResidentialDeveloper::TryRules(const GeCellRect &rectangle, u32 identifier, i32 demand, i32 maximum) {
  ScArray<u32> candidates;
  candidates.Reserve(rules.size);
  for (u32 index = 0; index < rules.size; ++index)
    *candidates.Append() = index;
  u32 attempts = rules.size > 4 ? 4 : rules.size;
  u8 placed = 0;
  while (attempts-- && !placed) {
    u32 position = random.Bounded(candidates.size);
    EcZoneRule *rule = rules.data[candidates.data[position]];
    if (rule)
      placed = rule->Place(rectangle, identifier, demand, maximum);
    for (u32 index = position + 1; index < candidates.size; ++index)
      candidates.data[index - 1] = candidates.data[index];
    --candidates.size;
  }
  return placed;
}

// Form the real buildable rectangle and try the registered rules with the live unfilled residential demand.
u8 EcResidentialDeveloper::TryDevelop(const ScPointXZ &point, u32 identifier, u32 existing, u32 distance) {
  if (!identifier)
    return 0;
  EcGrowthRecord *record = growth_records;
  while (record && record->identifier < identifier)
    record = record->next;
  if (!record || record->identifier != identifier)
    return 0;
  GeCellRect rectangle{};
  zones->FindBuildableRect(&rectangle, point.x, point.z, distance, record->minimum, existing, false);
  i32 value = residential_valve->QueryValveValue();
  u32 valve_id = residential_valve->GetId();
  i32 demand = i32(u32(value) - zones->GetFilledDemand(valve_id));
  i32 maximum = residential_valve->QueryValveMax();
  residential_valve->QueryValveMin();
  return TryRules(rectangle, identifier, demand, maximum);
}

// Select a growth entry with a higher minimum value and no lower category, preserving ordered tie behavior.
u32 EcResidentialDeveloper::SelectHigherValue(u32 current, u8 value, u8 include_all) {
  EcGrowthRecord *previous = growth_records;
  while (previous && previous->identifier < current)
    previous = previous->next;
  if (!previous || previous->identifier != current)
    return 0;
  u8 best = previous->minimum, category = previous->category;
  u32 selected = 0;
  for (EcGrowthIndex *index = growth_by_minimum; index; index = index->next) {
    if (index->key < previous->minimum)
      continue;
    EcGrowthRecord *record = growth_records;
    while (record->identifier != index->identifier)
      record = record->next;
    if (value >= index->key && value <= record->maximum && record->category >= category && index->key > best &&
        (include_all || record->category == 1)) {
      best = u8(index->key);
      selected = index->identifier;
    }
  }
  return selected;
}

u32 EcResidentialDeveloper::SelectLowerValue(u32, u8, u8) {
  return 0;
}

// Find the matching higher-category entry with the greatest qualifying minimum growth value.
u32 EcResidentialDeveloper::SelectHigherDensity(u32 current, u8 value) {
  EcGrowthRecord *previous = growth_records;
  while (previous && previous->identifier < current)
    previous = previous->next;
  if (!previous || previous->identifier != current)
    return 0;
  u32 next_category = u32(previous->category) + 1;
  EcGrowthIndex *index = growth_by_class;
  while (index && index->key < next_category)
    index = index->next;
  if (!index || index->key != next_category)
    return 0;
  u32 selected = 0;
  u8 best = 0;
  for (; index; index = index->next) {
    EcGrowthRecord *record = growth_records;
    while (record->identifier != index->identifier)
      record = record->next;
    if (record->minimum <= value && record->maximum >= value && record->minimum > best) {
      best = record->minimum;
      selected = index->identifier;
    }
  }
  return selected;
}

// Resolve an exact building growth ID, then search the live agent-type ancestry tree when no exact record exists.
u32 EcResidentialDeveloper::FindGrowthType(u32 identifier) {
  EcGrowthRecord *record = growth_records;
  while (record && record->identifier < identifier)
    record = record->next;
  if (record && record->identifier == identifier)
    return record->identifier;
  for (EcGrowthIndex *index = growth_by_class; index; index = index->next)
    if (agent_tree->IsAncestor(index->identifier, identifier))
      return index->identifier;
  return 0;
}

// Re-place the current building using its real attribute key, occupant rotation and requested occupancy state.
u8 EcResidentialDeveloper::ReplaceBuilding(BdBuilding *building, const ScPointXZ &point, u32 state) {
  if (!building)
    return 0;
  GeAttrib *base = building->GetBaseAttribData();
  if (!base)
    return 0;
  u16 rotation = 0, pitch = 0, roll = 0;
  ScOccupant *occupant = nullptr;
  if (building->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&occupant))) {
    occupant->GetOrientation(&rotation, &pitch, &roll);
    occupant->Release();
  }
  RsKey *key = base->Key();
  return zones->PlaceBuilding(*key, point, i32(state), u16(360u * rotation / 65535u), true);
}

// Evaluate the real residential tile against utilities, transport, pollution, demand and existing occupancy.
// Preserve the original random decisions and building replacement order; placement remains delegated to actual rules.
i32 EcResidentialDeveloper::Evaluate(const ScPointXZ &point) {
  ScOccupant *occupant = nullptr;
  bool occupied = occupants->GetOccupantAtCell(point.x, point.z, &occupant);
  u32 distance_bonus = ordinances->QueryOrdinanceOn(kCtOrdinance_ShuttleService) ? 1u : 0u;
  u32 distance = distance_bonus + EcResidentialDeveloperTuning[1];
  bool near_transport = zones->IsNearTransport(point, distance);
  bool powered = false;
  power->GetValue(point.x, point.z, &powered);
  u8 watered = water->IsWatered(point.x, point.z);
  u8 land = 0;
  land_value->GetValue(point.x, point.z, &land);
  bool radioactive = pollution->IsRadioactive(point.x, point.z);
  u8 removable = 1;
  bool has_building = false;
  BdBuilding *building = nullptr;
  u32 building_x = 0, building_z = 0;
  if (occupied) {
    removable = occupant->CanSimRemove();
    has_building = occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building));
    occupant->GetLocationInCellCoords(&building_x, &building_z);
    occupant->Release();
  }
  i32 valve_value = residential_valve->QueryValveValue();
  u32 valve_id = residential_valve->GetId();
  i32 unfilled = i32(u32(valve_value) - zones->GetFilledDemand(valve_id));
  i32 maximum = residential_valve->QueryValveMax();
  i32 minimum = residential_valve->QueryValveMin();
  if (!has_building) {
    if (valve_value > 0 && unfilled > 0) {
      if (radioactive)
        return 10;
      if (!powered)
        return 14;
      if (!near_transport)
        return 16;
      if (land < minimum_growth)
        return 11;
      if (land > maximum_growth)
        return 12;
      u32 selected = SelectGrowth(land, watered);
      if (!selected)
        return watered ? 18 : 15;
      if (removable && powered && near_transport && !radioactive)
        TryDevelop(point, selected, 0, distance);
      return 0;
    }
    return valve_value < 0 ? 13 : 0;
  }
  if (point.x != building_x || point.z != building_z) {
    building->Release();
    return 0;
  }
  GeAttrib *base = building->GetBaseAttribData();
  CtAgentType *type = base->CityAgentType();
  bool locked = building->GetOccupancyStateFlag(kGeBuildingHistorical);
  u32 pending_flags = locked ? kGeBuildingHistorical : 0u;
  bool service_sensitive = false, pending_change = false;
  GeBuildingType *building_type = nullptr;
  if (base->QueryInterface(IID_GeBuildingType, reinterpret_cast<void **>(&building_type))) {
    service_sensitive = (building_type->Flags() & 0x100) != 0;
    building_type->Release();
    if (service_sensitive) {
      bool garbage = pollution->IsGarbageAccumulated(point.x, point.z);
      bool flagged = building->GetOccupancyStateFlag(kGeBuildingCoveredInGarbage);
      if (garbage && !flagged) {
        pending_flags |= kGeBuildingCoveredInGarbage;
        pending_change = true;
      } else if (!garbage && flagged) {
        pending_change = true;
      }
    }
  }
  u32 size = base->SizeInCellsX();
  u32 current = FindGrowthType(type->Id());
  u8 category = 1;
  if (current) {
    EcGrowthRecord *record = growth_records;
    while (record && record->identifier < current)
      record = record->next;
    assert(record && record->identifier == current);
    category = record->category;
  }
  u8 trip = traffic->IsTripAvailable(building_x, building_z);
  u32 state = building->GetOccupancyState(), selected = 0;
  i32 result = 0;
  bool develop = false;
  if (state == kGeBuildingUnderConstruction) {
    if (!current && !service_sensitive) {
      if (valve_value > 0 && unfilled > 0 && !locked && removable)
        buildings->RemoveBuilding(building);
    } else if (powered && near_transport && !radioactive &&
        random.Bounded(100u * size) < EcResidentialDeveloperTuning[2]) {
      ReplaceBuilding(building, point, pending_flags | kGeBuildingOccupied);
      pending_change = false;
    }
  } else if (state == kGeBuildingAbandoned) {
    if (current || service_sensitive) {
      if (powered && near_transport && !trip && !radioactive && valve_value > 0 && unfilled > 0) {
        if (locked || (selected = SelectGrowth(land, watered)) == current) {
          ReplaceBuilding(building, point, pending_flags | kGeBuildingOccupied);
          pending_change = false;
        } else {
          develop = true;
        }
      }
    } else if (valve_value > 0 && unfilled > 0 && !radioactive && !locked) {
      selected = SelectGrowth(land, watered);
      develop = selected != 0;
    }
  } else if (state == kGeBuildingOccupied) {
    if (radioactive) {
      ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
      building->Release();
      return 1;
    }
    if (!powered || !near_transport || trip == 1 || (!watered && category > 1)) {
      assert(size != 0);
      if (random.Bounded(100) < EcResidentialDeveloperTuning[0] / size) {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        pending_change = false;
        if (!powered) {
          building->Release();
          return 4;
        }
        if (!watered && category > 1) {
          building->Release();
          return 5;
        }
        result = 6;
      }
    } else {
      assert(size != 0);
      if (valve_value < 0 && i32(random.Bounded(0u - u32(minimum))) < i32(0u - u32(unfilled / i32(size)))) {
        ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
        building->Release();
        return 3;
      }
      if (current) {
        EcGrowthRecord *record = growth_records;
        while (record && record->identifier < current)
          record = record->next;
        assert(record && record->identifier == current);
        if (land < (record->minimum >> 1)) {
          selected = SelectLowerValue(current, land, watered);
          if (!selected || random.Bounded(100) >= EcResidentialDeveloperTuning[4]) {
            ReplaceBuilding(building, point, pending_flags | kGeBuildingAbandoned);
            pending_change = false;
            result = 2;
          } else {
            develop = true;
          }
          goto finish_building;
        }
      }
      if (!locked && valve_value > 0 && current && (selected = SelectHigherValue(current, land, watered))) {
        if (random.Bounded(100) < EcResidentialDeveloperTuning[3])
          develop = true;
      } else if (watered && !locked) {
        if (valve_value > 0 && current && i32(random.Bounded(u32(maximum) / size)) < valve_value) {
          selected = SelectHigherDensity(current, land);
          if (selected && random.Bounded(100) < EcResidentialDeveloperTuning[5])
            develop = true;
        } else if (valve_value > 0 && current && random.Bounded(10000) < EcResidentialDeveloperTuning[6]) {
          selected = current;
          develop = true;
        }
      }
    }
  }
finish_building:
  if (pending_change)
    ReplaceBuilding(building, point, pending_flags | state);
  building->Release();
  if (develop && removable && powered && near_transport && !radioactive)
    TryDevelop(point, selected, current, distance);
  return result;
}

// Create and retain the three residential history graphs in original order, loading their real localized labels.
void EcResidential::InitializeGraphs() {
  ScString label;
  first_graph = ScAddRefNotNull(new EcRci::ScGraphInfo(0));
  first_graph->SetAutoMaxFactors(10000, 1.1f);
  first_graph->SetAutoMax(true);
  {
    EcRci::ScTextResource text(kRsGameResidentsText, kRsGameTextGroup);
    ScString translated = text.GetText(), copy(translated), value(copy);
    first_graph->SetSelectionLabel(value);
  }
  {
    EcRci::ScTextResource text(kRsGameResFormatText, kRsGameTextGroup);
    ScString translated = text.GetText(), copy(translated);
    first_graph_format = copy;
  }
  label.Sprintf(first_graph_format.ToChar(), "0");
  first_graph->SetCurrentValueLabel(ScGraphDataSet::Month, label);
  first_graph->SetCurrentValueLabel(ScGraphDataSet::Year, label);
  first_graph->SetCurrentValueLabel(ScGraphDataSet::Decade, label);
  u32 selection = kEcResidentsGraphSelectionId, value_id = kEcResidentsGraphValueId;
  first_graph->SetSelectionLabelID(selection);
  first_graph->SetValueLabelID(value_id);
  first_graph->SetColor(kEcResidentsGraphColor);

  second_graph = ScAddRefNotNull(new EcRci::ScGraphInfo(0));
  second_graph->SetMaxValue(150);
  {
    EcRci::ScTextResource text(kRsGameEducationText, kRsGameTextGroup);
    ScString translated = text.GetText(), copy(translated), value(copy);
    second_graph->SetSelectionLabel(value);
  }
  {
    EcRci::ScTextResource text(kRsGameEducationFormatText, kRsGameTextGroup);
    ScString translated = text.GetText(), copy(translated);
    second_graph_format = copy;
  }
  label.Sprintf(second_graph_format.ToChar(), "0");
  second_graph->SetCurrentValueLabel(ScGraphDataSet::Month, label);
  second_graph->SetCurrentValueLabel(ScGraphDataSet::Year, label);
  second_graph->SetCurrentValueLabel(ScGraphDataSet::Decade, label);
  selection = kEcEducationGraphSelectionId;
  value_id = kEcEducationGraphValueId;
  second_graph->SetSelectionLabelID(selection);
  second_graph->SetValueLabelID(value_id);
  second_graph->SetColor(kEcEducationGraphColor);

  third_graph = ScAddRefNotNull(new EcRci::ScGraphInfo(0));
  third_graph->SetMaxValue(89);
  {
    EcRci::ScTextResource text(365, kRsGameTextGroup);
    ScString translated = text.GetText(), copy(translated), value(copy);
    third_graph->SetSelectionLabel(value);
  }
  {
    EcRci::ScTextResource text(kRsGameHealthFormatText, kRsGameTextGroup);
    ScString translated = text.GetText(), copy(translated);
    third_graph_format = copy;
  }
  label.Sprintf(third_graph_format.ToChar(), "0");
  third_graph->SetCurrentValueLabel(ScGraphDataSet::Month, label);
  third_graph->SetCurrentValueLabel(ScGraphDataSet::Year, label);
  third_graph->SetCurrentValueLabel(ScGraphDataSet::Decade, label);
  selection = kEcHealthGraphSelectionId;
  value_id = kEcHealthGraphValueId;
  third_graph->SetSelectionLabelID(selection);
  third_graph->SetValueLabelID(value_id);
  third_graph->SetColor(kEcHealthGraphColor);
}

void EcResidential::RefreshGraphLabels() {
  ScString formatted, number;
  ScLanguageUtility *language = EcGetRciLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  EcRci::ScGraphInfo **graphs[] = {&first_graph, &second_graph, &third_graph};
  ScString *formats[] = {&first_graph_format, &second_graph_format, &third_graph_format};
  for (u32 group = 0; group < 3; ++group) {
    for (u32 set = 0; set < 3; ++set) {
      u32 value = 0;
      if (set == 0)
        (*graphs[group])->GetMonthData(&value);
      else if (set == 1)
        (*graphs[group])->GetYearData(&value);
      else
        (*graphs[group])->GetDecadeData(&value);
      language->MakeNumberString(i64(i32(value)), &number);
      formatted.Sprintf(formats[group]->ToChar(), number.ToChar());
      (*graphs[group])->SetCurrentValueLabel(ScGraphDataSet(set), formatted);
    }
  }
}

// Retain the city's actual services, create its residential and total-population valves, and initialize cohorts and graphs.
// Failed service or valve acquisition calls the live layer shutdown after preserving the already published fields.
bool EcResidential::Init(CtCity *owner) {
  city = ScAddRefNotNull(owner);
  budget = ScAddRefNotNull(city->BudgetLayer());
  ordinances = ScAddRefNotNull(city->OrdinanceLayer());
  pollution = ScAddRefNotNull(city->PollutionLayer());
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer))) {
    Shutdown();
    return false;
  }
  residential = ScAddRefNotNull(new EcRci::EcResidentialValve);
  if (!residential->Init(city, kEcResidentialValveId, static_cast<CtLayer *>(this))) {
    Shutdown();
    return false;
  }
  residential->SetValveMax(2000);
  residential->SetValveMin(-2000);
  valve_layer->AddValveToLayer(residential);
  if (!valve_layer->CreateNewValve(kEcFunValveId, IID_EcValve, reinterpret_cast<void **>(&total_valve))) {
    Shutdown();
    return false;
  }
  total_valve->SetValveMax(INT32_MAX);
  total_valve->SetValveMin(i32(kScIntegerSignBit));
  total_valve->SetInitialSupplyValue(external_fun_supply);
  InitializeCohorts();
  education_upkeep = 0;
  education_funding = 0;
  health_upkeep = 0;
  health_funding = 0;
  service_lock.Lock();
  ClearUpkeepServices();
  ClearEducationAndHealth();
  service_lock.Unlock();
  InitializeGraphs();
  school_strike = false;
  college_strike = false;
  health_strike = false;
  school_rating = 0;
  college_rating = 0;
  health_rating = 0;
  patients = 0;
  school_students = 0;
  college_students = 0;
  school_capacity = 0;
  college_capacity = 0;
  health_capacity = 0;
  library_capacity = 0;
  museum_capacity = 0;
  teachers = 0;
  professors = 0;
  doctors = 0;
  return true;
}

// Retain health and education department budgets, then subscribe to the surface occupant manager.
// Preserve earlier acquisitions when a later operation fails, and refresh the real graph labels only after registration.
bool EcResidential::SimulationBegin() {
  if (!budget->GetDepartmentBudget(kEcHealthDepartmentId, &health_budget))
    return false;
  health_budget->AddRef();
  if (!budget->GetDepartmentBudget(kEcEducationDepartmentId, &education_budget))
    return false;
  education_budget->AddRef();
  occupant_manager = city->SurfaceOccupantManager();
  u32 sender = 0;
  if (!occupant_manager ||
      !occupant_manager->RegisterChangeReceiver(static_cast<CtChangeReceiver *>(this), &sender, true))
    return false;
  RefreshGraphLabels();
  return true;
}

// Unregister the borrowed manager and retire retained city services in the original order.
// Clear borrowed education and health records under their lock, then release the three owned graphs in publication order.
bool EcResidential::Shutdown() {
  if (occupant_manager) {
    occupant_manager->UnregisterChangeReceiver(static_cast<CtChangeReceiver *>(this));
    occupant_manager = nullptr;
  }
  ScRelease(&city);
  ScRelease(&valve_layer);
  ScRelease(&ordinances);
  ScRelease(&pollution);
  if (residential) {
    residential->Shutdown();
    residential->Release();
    residential = nullptr;
  }
  ScRelease(&total_valve);
  ScRelease(&budget);
  ScRelease(&education_budget);
  ScRelease(&health_budget);
  service_lock.Lock();
  ClearEducationAndHealth();
  service_lock.Unlock();
  if (first_graph)
    first_graph->Release();
  first_graph = nullptr;
  if (second_graph)
    second_graph->Release();
  second_graph = nullptr;
  if (third_graph)
    third_graph->Release();
  third_graph = nullptr;
  return true;
}

EcResidentialManipulator::EcResidentialManipulator() : layer(nullptr) {
}

EcResidentialManipulator::~EcResidentialManipulator() {
  Shutdown();
}

// Publish the manipulator identity for Unknown or its public IID, then acquire its shared lifetime reference.
bool EcResidentialManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcResidentialManipulator::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcResidentialManipulator::Release() {
  return EcCommandOwner::Release();
}

bool EcResidentialManipulator::Apply(CtAgent *) {
  layer->SimulatePopulation();
  return true;
}

bool EcResidentialManipulator::IsLockingCity() {
  return true;
}

bool EcResidentialManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcResidentialManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcResidentialManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

u8 EcResidentialManipulator::Init(EcResidential *owner) {
  layer = ScAddRefNotNull(owner);
  return 1;
}

// Release the layer before clearing its field. The original assumes successful binding before retirement.
u8 EcResidentialManipulator::Shutdown() {
  layer->Release();
  layer = nullptr;
  return 1;
}

// Persist the two actual valves, three graph histories, strike and capacity state, and four age arrays in original order.
// Abort the framed record on the first failed write and preserve the original version-three format.
bool EcResidential::Save(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcResidentialLayerType, 0}, 3, 0);
  u8 result = frame.good;
  RsStream *stream = frame.GetStream();
  result = result && stream->WriteU32(2) && stream->WriteU32(residential->GetId()) &&
      residential->SerialWrite(owner, stream) && stream->WriteU32(total_valve->GetId()) &&
      total_valve->SerialWrite(owner, stream) && first_graph->SerialWrite(owner, stream) &&
      second_graph->SerialWrite(owner, stream) && third_graph->SerialWrite(owner, stream) &&
      stream->WriteU8(school_strike) && stream->WriteU8(health_strike) && stream->WriteU32(school_strike_id) &&
      stream->WriteU32(health_strike_id) && stream->WriteU8(school_rating) && stream->WriteU8(health_rating) &&
      stream->WriteU32(u32(patients)) && stream->WriteU8(college_strike) && stream->WriteU32(college_strike_id) &&
      stream->WriteU8(college_rating) && stream->WriteU32(u32(school_students)) &&
      stream->WriteU32(u32(college_students)) && stream->WriteU32(u32(school_capacity)) &&
      stream->WriteU32(u32(college_capacity)) && stream->WriteU32(u32(health_capacity)) &&
      stream->WriteU32(u32(museum_capacity)) && stream->WriteU32(u32(library_capacity)) &&
      stream->WriteU8(population_flag) && stream->WriteU8(u8(baby_trend)) && stream->WriteF32Array(education, 90) &&
      stream->WriteF32Array(health_factor, 90) && stream->WriteF32Array(cohort_population, 90) &&
      stream->WriteF32Array(health, 90) && stream->WriteU32(u32(teachers)) && stream->WriteU32(u32(professors)) &&
      stream->WriteU32(u32(doctors));
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Restore the framed valve, graph and demographic state in source order, retaining partial writes on stream refusal.
// Old record versions omit staff totals and may contain leading or trailing text records.
bool EcResidential::Init(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame *frame =
      new EcRci::EcArchiveFrame(owner, segment, RsKey{kRsCityLayerRecordType, kEcResidentialLayerType, 0});
  u8 result = frame->good;
  if (!result) {
    delete frame;
    return false;
  }
  RsStream *stream = frame->GetStream();
  if (frame->text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  u32 count = 0, identifier = 0;
  if (result)
    result = stream->ReadU32(&count);
  bool have_count = result != 0;
  if (result && count) {
    result = stream->ReadU32(&identifier);
    if (residential->GetId() != identifier)
      result = 0;
    if (result)
      result = residential->SerialRead(owner, stream);
  }
  if (have_count && count > 1) {
    if (result)
      result = stream->ReadU32(&identifier);
    if (total_valve->GetId() != identifier)
      result = 0;
    if (result)
      result = total_valve->SerialRead(owner, stream);
  }
  if (result)
    result = first_graph->SerialRead(owner, stream);
  if (result)
    result = second_graph->SerialRead(owner, stream);
  if (result)
    result = third_graph->SerialRead(owner, stream);
  u8 byte = 0;
  if (result) {
    result = stream->ReadU8(&byte);
    if (result)
      school_strike = byte != 0;
  }
  if (result) {
    result = stream->ReadU8(&byte);
    if (result)
      health_strike = byte != 0;
  }
  result = result && stream->ReadU32(&school_strike_id) && stream->ReadU32(&health_strike_id) &&
      stream->ReadU8(&school_rating) && stream->ReadU8(&health_rating) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&patients)) && stream->ReadU8(&byte);
  college_strike = byte != 0;
  result = result && stream->ReadU32(&college_strike_id) && stream->ReadU8(&college_rating) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&school_students)) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&college_students)) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&school_capacity)) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&college_capacity)) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&health_capacity)) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&museum_capacity)) &&
      stream->ReadU32(reinterpret_cast<u32 *>(&library_capacity)) && stream->ReadU8(&population_flag) &&
      stream->ReadU8(&byte);
  baby_trend = byte;
  result = result && stream->ReadF32Array(education, 90) && stream->ReadF32Array(health_factor, 90) &&
      stream->ReadF32Array(cohort_population, 90) && stream->ReadF32Array(health, 90);
  if (frame->version > 2 && result) {
    result = stream->ReadU32(reinterpret_cast<u32 *>(&teachers));
    result = result && stream->ReadU32(reinterpret_cast<u32 *>(&professors)) &&
        stream->ReadU32(reinterpret_cast<u32 *>(&doctors));
  }
  if (frame->version > 1 && result && frame->text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  if (!result)
    frame->MarkFailed();
  delete frame;
  return result != 0;
}

// Preserve the installed biased-double integer extraction and wrapped low DWORD instead of host rounding conversion.
static u32 EcResidentialRound(f64 value) {
  volatile f64 rounded = value + kScDoubleIntegerBias;
  f64 stored = rounded;
  u64 bits;
  memcpy(&bits, &stored, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Update monthly population, service effects, strikes and the real history graphs in their installed callback order.
void EcResidential::SimulatePopulation() {
  i32 population = GetPopulation();
  ApBoxX *box = EcGetRciApplication()->GetBoxX();
  bool curfew = ordinances->QueryOrdinanceOn(kCtOrdinance_YouthCurfew);
  bool reading = ordinances->QueryOrdinanceOn(kCtOrdinance_ProReading);
  bool junior_sports = ordinances->QueryOrdinanceOn(kCtOrdinance_JuniorSports);
  bool crossing = ordinances->QueryOrdinanceOn(kCtOrdinance_CrossingGuards);
  bool clinics = ordinances->QueryOrdinanceOn(kCtOrdinance_FreeClinics);
  bool cpr = ordinances->QueryOrdinanceOn(kCtOrdinance_CommunityCPRTraining);
  bool smoking = ordinances->QueryOrdinanceOn(kCtOrdinance_PublicSmokingBan);
  box->Event(kAuEventCityPopulationChanged, u32(population), 0, 0, 0);
  i32 supply = total_valve->QuerySupplyValue();
  i32 demand = total_valve->QueryDemandValue();
  u8 cap = 0;
  if (demand) {
    if (supply < demand) {
      cap = 100;
    } else {
      i32 scaled = i32(100u * u32(demand));
      // The installed IDIV has no defined result for these divisor and quotient cases.
      assert(supply && (scaled != INT32_MIN || supply != -1));
      cap = u8(scaled / supply);
    }
  }
  residential->SetDemandCap(cap);
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  service_lock.Lock();
  education_upkeep = 0;
  for (EcResidentialServiceEntry *entry = schools; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    education_upkeep = i32(u32(education_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(education_funding);
  }
  for (EcResidentialServiceEntry *entry = colleges; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    education_upkeep = i32(u32(education_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(education_funding);
  }
  for (EcResidentialServiceEntry *entry = other_education; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    education_upkeep = i32(u32(education_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(education_funding);
  }
  health_upkeep = 0;
  for (EcResidentialServiceEntry *entry = hospitals; entry; entry = entry->next) {
    i32 upkeep = entry->service->GetMonthlyUpkeep();
    health_upkeep = i32(u32(health_upkeep) + u32(upkeep));
    entry->service->SetFundingPercentage(health_funding);
  }
  f32 beds = 0, health_total = 0;
  doctors = 0;
  for (EcResidentialHealthEntry *entry = health_services; entry; entry = entry->next) {
    u32 count = entry->service->GetBedCount();
    f32 count_float = f32(count);
    beds = f32(f64(beds) + f64(count));
    health_total = f32(f64(health_total) + f64(entry->service->GetHealthEffect()) * count_float);
    u32 staff = entry->service->GetDoctorCount();
    doctors = i32(u32(doctors) + staff);
  }
  school_capacity = 0;
  college_capacity = 0;
  museum_capacity = 0;
  library_capacity = 0;
  teachers = 0;
  professors = 0;
  for (EcResidentialEducationType *group = education_types; group; group = group->next) {
    for (u32 age = 0; age < 90; ++age) {
      group->cohort_effect[age] = 0;
      group->cohort_count[age] = 0;
    }
    for (EcResidentialEducationEntry *entry = group->services; entry; entry = entry->next) {
      u32 kind = entry->service->GetKind();
      if (kind == 0) {
        u32 staff = entry->service->GetStaffCount();
        teachers = i32(u32(teachers) + staff);
        u32 capacity = entry->service->GetCapacity();
        school_capacity = i32(u32(school_capacity) + capacity);
      } else if (kind == 1) {
        u32 staff = entry->service->GetStaffCount();
        professors = i32(u32(professors) + staff);
        u32 capacity = entry->service->GetCapacity();
        college_capacity = i32(u32(college_capacity) + capacity);
      } else if (kind == 2) {
        u32 capacity = entry->service->GetCapacity();
        library_capacity = i32(u32(library_capacity) + capacity);
      } else if (kind == 3) {
        u32 capacity = entry->service->GetCapacity();
        museum_capacity = i32(u32(museum_capacity) + capacity);
      }
      u8 first = entry->service->GetMinimumAge(), last = entry->service->GetMaximumAge();
      if (last > 89)
        last = 89;
      f32 served = 0;
      for (u32 age = first; age <= last; ++age)
        served = f32(f64(served) + cohort_population[age]);
      if (served > 0) {
        for (u32 age = first; age <= last; ++age) {
          u32 count = EcResidentialRound(f64(entry->service->GetCapacity()) * cohort_population[age] / served);
          group->cohort_count[age] += count;
          f64 effect = f64(entry->service->GetEducationEffect()) * i32(count);
          if ((college_strike && kind == 1) || (school_strike && kind == 0))
            effect *= education_strike_efficiency;
          group->cohort_effect[age] = f32(effect + group->cohort_effect[age]);
        }
      }
    }
  }
  service_lock.Unlock();
  u8 workforce_education = GetAgeCohortEQ(20, 40);
  if (workforce_education > 120)
    workforce_education = 120;
  f32 youth_bonus = f32(f64(workforce_education) / (f64(age_of_rebellion) * 12.0));
  u32 air = u32(pollution->GetAverageAirValue());
  u32 water = u32(pollution->GetAverageWaterValue());
  f32 city_population = f32(u32(population));
  i64 patient_value = i64((f64((air + water) >> 1) * pollution_factor + hospitalization_rate) * city_population);
  patients = i32(u32(patient_value));
  f32 patient_count = f32(u32(patient_value));
  health_capacity = i32(EcResidentialRound(beds));
  if (patient_count <= 0)
    health_total = 0.00001f;
  else if (patient_count > beds || beds <= 0)
    health_total = f32(f64(health_total) / patient_count);
  else
    health_total = f32(f64(health_total) / beds);
  i32 rating;
  if (patient_count == 0 && health_total > 0)
    rating = 4;
  else
    rating = i32(EcResidentialRound(f64(health_total) * 4.0 / (optimal_health_rate * f64(0.083333336f)) - f64(0.4f)));
  health_rating = u8(rating > 4 ? 4 : rating < 0 ? 0 : rating);
  if (city_population != 0) {
    if (crossing)
      health_total += crossing_guards_effect;
    if (clinics)
      health_total += free_clinics_effect;
    if (cpr)
      health_total += cpr_training_effect;
    if (smoking)
      health_total += smoking_ban_effect;
  }
  if (health_strike)
    health_total = f32(f64(health_total) * health_strike_efficiency);
  i32 last_age = 0;
  bool found_age = false;
  f64 population_sum = 0;
  f32 school_effect = 0, college_effect = 0;
  school_students = 0;
  college_students = 0;
  for (i32 age = 89; age >= 0; --age) {
    f32 &portion = cohort_population[age];
    if (portion <= 0) {
      portion = 0;
      continue;
    }
    if (!found_age) {
      last_age = age;
      found_age = true;
    }
    if (f64(age) > health[age])
      portion = f32(f64(portion) * f64(0.90909088f));
    if (city_population * f64(portion) < 1.0)
      portion = 0;
    population_sum = f32(f64(population_sum) + portion);
    f32 effect = 0, ordinance_effect = 0;
    if (curfew && age >= youth_curfew_min_age && age <= youth_curfew_max_age)
      ordinance_effect = youth_curfew_effect;
    if (reading)
      ordinance_effect += age < reading_min_age || age > reading_max_age ? reading_adult_effect : reading_child_effect;
    if (junior_sports && age >= junior_sports_min_age && age <= junior_sports_max_age)
      ordinance_effect += junior_sports_effect;
    f32 age_population = f32(f64(city_population) * portion);
    for (EcResidentialEducationType *group = education_types; group; group = group->next) {
      i32 count = i32(group->cohort_count[age]);
      if (age_population > count || count <= 0) {
        if (age_population > 0)
          effect = f32(f64(effect) + group->cohort_effect[age] / f64(age_population));
      } else {
        effect = f32(f64(effect) + group->cohort_effect[age] / f64(count));
      }
    }
    education[age] = f32(f64(education[age]) + ordinance_effect + effect);
    if (age >= 6 && age <= 18) {
      school_effect = f32(f64(school_effect) + effect);
      school_students = i32(u32(school_students) + EcResidentialRound(age_population));
    }
    if (age >= 19 && age <= 24) {
      college_effect = f32(f64(college_effect) + effect);
      college_students = i32(u32(college_students) + EcResidentialRound(age_population));
    }
    if (age < age_of_rebellion)
      education[age] = f32(f64(education[age]) + youth_bonus);
    if (age <= 26) {
      f64 minimum = f64(age * 85) * f64(0.03846154f);
      if (minimum > education[age])
        education[age] = f32(minimum);
    }
    if (education[age] > 150.0f)
      education[age] = 150.0f;
    if (health_total <= 0) {
      if (health_factor[age] > 0)
        health_factor[age] = f32(f64(health_decay) * health_factor[age]);
    } else {
      health_factor[age] = f32(f64(health_factor[age]) + health_total);
    }
    f64 life_expectancy = f64(health_factor[age]) / optimal_health_rate * f64(0.26966292f) + 50.0;
    health[age] = life_expectancy > 89.0 ? 89.0f : f32(life_expectancy);
  }
  rating = i32(EcResidentialRound(
      f64(school_effect) * f64(0.07692308f) * 4.0 / (optimal_school_rate * f64(0.083333336f)) - f64(0.4f)));
  school_rating = u8(rating > 4 ? 4 : rating < 0 ? 0 : rating);
  rating = i32(EcResidentialRound(
      f64(college_effect) * f64(0.16666667f) * 4.0 / (optimal_college_rate * f64(0.083333336f)) - f64(0.4f)));
  college_rating = u8(rating > 4 ? 4 : rating < 0 ? 0 : rating);
  if (!health_strike) {
    u32 chance = u32(ChanceOfHealthStrike());
    if (chance && random.Bounded(10000) < chance) {
      health_strike = true;
      health_strike_id = 0;
      ScMessage message(kEcMsg_HealthStrikeChanged, 1, 0, 0);
      EcGetRciMessageServer()->MessagePost(message, false);
    }
  } else if (++health_strike_id > u32(health_strike_duration) || health_funding >= 100 || !hospitals) {
    EndHealthStrike();
  }
  if (school_strike) {
    if (++school_strike_id > u32(education_strike_duration) || education_funding >= 100 || !schools)
      EndSchoolStrike();
  } else {
    u32 chance = u32(ChanceOfSchoolStrike());
    if (chance && random.Bounded(10000) < chance) {
      school_strike = true;
      school_strike_id = 0;
      ScMessage message(kEcMsg_SchoolStrikeChanged, 1, 0, 0);
      EcGetRciMessageServer()->MessagePost(message, false);
    }
  }
  if (college_strike) {
    if (++college_strike_id > u32(education_strike_duration) || education_funding >= 100 || !colleges)
      EndCollegeStrike();
  } else {
    u32 chance = u32(ChanceOfCollegeStrike());
    if (chance && random.Bounded(10000) < chance) {
      college_strike = true;
      college_strike_id = 0;
      ScMessage message(kEcMsg_CollegeStrikeChanged, 1, 0, 0);
      EcGetRciMessageServer()->MessagePost(message, false);
    }
  }
  if (month == 1) {
    for (i32 age = 89; age > 0; --age) {
      cohort_population[age] = cohort_population[age - 1];
      if (age <= last_age) {
        education[age] = age > 26 ? f32(f64(education_decay) * education[age - 1]) : education[age - 1];
        health[age] = health[age - 1];
      } else {
        education[age] = 0;
        health[age] = 0;
      }
    }
    ++last_age;
    if (baby_trend <= 2) {
      f64 base = baby_trend == 0 ? 0.75 : baby_trend == 1 ? 0.25 : 0.5;
      cohort_population[0] =
          f32((base - random.RandomDoubleUniform()) * 0.001 + f64(cohort_population[1]) / cohort_decay);
    }
    if (minimum_newborn_population > cohort_population[0]) {
      cohort_population[0] = minimum_newborn_population;
      baby_trend = 0;
    } else if (maximum_newborn_population >= cohort_population[0]) {
      if (baby_trend_probability > random.RandomDoubleUniform()) {
        if (baby_trend <= 1)
          baby_trend = 2;
        else if (baby_trend == 2)
          baby_trend = random.RandomDoubleUniform() >= 0.5 ? 1 : 0;
      }
    } else {
      cohort_population[0] = maximum_newborn_population;
      baby_trend = 1;
    }
    population_sum += cohort_population[0];
    health[0] = health[1];
    education[0] = 0;
  }
  if (population_sum < 1.0 || population_sum > 1.0) {
    f64 adjustment = (1.0 - population_sum) / f64(last_age + 1);
    for (i32 age = 0; age <= last_age && age < 90; ++age)
      cohort_population[age] = f32(f64(cohort_population[age]) + adjustment);
  }
  i32 current_population = GetPopulation();
  first_graph->SetMonthData(u32(current_population));
  if (current_population) {
    u32 average = GetAgeCohortEQ(0, 89);
    second_graph->SetMonthData(average);
    average = GetAgeCohortLE(0, 89);
    third_graph->SetMonthData(average);
  } else {
    second_graph->SetMonthData(0);
    third_graph->SetMonthData(0);
  }
  if (month == 1) {
    u32 average = first_graph->GetMonthAverage();
    first_graph->SetYearData(average);
    average = second_graph->GetMonthAverage();
    second_graph->SetYearData(average);
    average = third_graph->GetMonthAverage();
    third_graph->SetYearData(average);
    if (year % 10 == 0) {
      average = first_graph->GetYearAverage();
      first_graph->SetDecadeData(average);
      average = second_graph->GetYearAverage();
      second_graph->SetDecadeData(average);
      average = third_graph->GetYearAverage();
      third_graph->SetDecadeData(average);
    }
  }
  RefreshGraphLabels();
}

static void EcInsertResidentialService(EcResidentialServiceEntry **head, BdUpkeepView *service) {
  *head = new EcResidentialServiceEntry{*head, service};
}

// Erase only the first matching borrowed upkeep interface; report whether the original list contained it.
static bool EcEraseResidentialService(EcResidentialServiceEntry **head, BdUpkeepView *service) {
  while (*head && (*head)->service != service)
    head = &(*head)->next;
  if (!*head)
    return false;
  EcResidentialServiceEntry *entry = *head;
  *head = entry->next;
  delete entry;
  return true;
}

// Retire the native education groups and health list without releasing their borrowed service interfaces.
// The caller holds the original residential service lock during the matching init or shutdown phase.
void EcResidential::ClearEducationAndHealth() {
  while (education_types) {
    EcResidentialEducationType *group = education_types;
    education_types = group->next;
    while (group->services) {
      EcResidentialEducationEntry *entry = group->services;
      group->services = entry->next;
      delete entry;
    }
    delete group;
  }
  while (health_services) {
    EcResidentialHealthEntry *entry = health_services;
    health_services = entry->next;
    delete entry;
  }
}

// Destroy upkeep list nodes without releasing the borrowed interface values stored in those nodes.
void EcResidential::ClearUpkeepServices() {
  EcResidentialServiceEntry **lists[] = {&schools, &colleges, &other_education, &hospitals};
  for (u32 i = 0; i < 4; ++i) {
    while (*lists[i]) {
      EcResidentialServiceEntry *entry = *lists[i];
      *lists[i] = entry->next;
      delete entry;
    }
  }
}

// Update the original education and health service groups for a city occupant insertion or removal.
// Preserve borrowed list ownership, callback ordering and the different education and health budget notifications.
u8 EcResidential::UpdateBuilding(u32 operation, ScOccupant *occupant) {
  if (!occupant)
    return 0;
  BdBuilding *building = nullptr;
  if (occupant->QueryInterface(IID_BdBuilding, reinterpret_cast<void **>(&building))) {
    bool occupied = building->GetOccupancyState() == 1;
    building->Release();
    if (!occupied)
      return 1;
  }
  EcEducationService *education_service = nullptr;
  EcHealthService *health_service = nullptr;
  BdUpkeepView *upkeep = nullptr;
  occupant->QueryInterface(IID_EcEducationService, reinterpret_cast<void **>(&education_service));
  occupant->QueryInterface(IID_EcHealthService, reinterpret_cast<void **>(&health_service));
  if (!education_service && !health_service)
    return 1;
  occupant->QueryInterface(IID_BdUpkeepView, reinterpret_cast<void **>(&upkeep));
  u32 type = occupant->TypeId();
  if (education_service) {
    i32 change = upkeep ? upkeep->GetMonthlyUpkeep() : 0;
    service_lock.Lock();
    if (operation == 1) {
      EcResidentialEducationType *group = education_types;
      while (group && group->type < type)
        group = group->next;
      if (group && group->type == type) {
        EcResidentialEducationEntry **entry = &group->services;
        while (*entry && (*entry)->service != education_service)
          entry = &(*entry)->next;
        if (*entry) {
          EcResidentialEducationEntry *removed = *entry;
          *entry = removed->next;
          delete removed;
        }
      }
      if (upkeep) {
        u32 kind = education_service->GetKind();
        EcResidentialServiceEntry **list = kind == 0 ? &schools : kind == 1 ? &colleges : &other_education;
        if (!EcEraseResidentialService(list, upkeep))
          change = 0;
      }
      change = i32(0u - u32(change));
    } else if (operation == 0) {
      EcResidentialEducationType **group = &education_types;
      while (*group && (*group)->type < type)
        group = &(*group)->next;
      if (!*group || (*group)->type != type) {
        EcResidentialEducationType *created = new EcResidentialEducationType{};
        created->next = *group;
        created->type = type;
        *group = created;
      }
      (*group)->services = new EcResidentialEducationEntry{(*group)->services, education_service};
      if (upkeep) {
        u32 kind = education_service->GetKind();
        upkeep->SetFundingPercentage(education_funding);
        EcResidentialServiceEntry **list = kind == 0 ? &schools : kind == 1 ? &colleges : &other_education;
        EcInsertResidentialService(list, upkeep);
      }
    } else {
      change = 0;
    }
    service_lock.Unlock();
    if (change) {
      education_upkeep = i32(u32(education_upkeep) + u32(change));
      education_budget->SetTotalFundingNeeded(education_upkeep);
    }
    education_service->Release();
  }
  if (health_service) {
    i32 change = upkeep ? upkeep->GetMonthlyUpkeep() : 0;
    service_lock.Lock();
    if (operation == 1) {
      EcResidentialHealthEntry **entry = &health_services;
      while (*entry && (*entry)->service != health_service)
        entry = &(*entry)->next;
      if (*entry) {
        EcResidentialHealthEntry *removed = *entry;
        *entry = removed->next;
        delete removed;
      }
      if (upkeep && !EcEraseResidentialService(&hospitals, upkeep))
        change = 0;
      change = i32(0u - u32(change));
    } else if (operation == 0) {
      health_services = new EcResidentialHealthEntry{health_services, health_service};
      if (upkeep) {
        upkeep->SetFundingPercentage(health_funding);
        EcInsertResidentialService(&hospitals, upkeep);
      }
    } else {
      change = 0;
    }
    service_lock.Unlock();
    health_upkeep = i32(u32(health_upkeep) + u32(change));
    health_budget->SetTotalFundingNeeded(health_upkeep);
    health_service->Release();
  }
  if (upkeep)
    upkeep->Release();
  return 1;
}

bool EcResidential::OccupantInserted(u32, ScOccupant *occupant) {
  return UpdateBuilding(0, occupant) != 0;
}

bool EcResidential::OccupantRemoved(u32, ScOccupant *occupant) {
  return UpdateBuilding(1, occupant) != 0;
}

bool EcResidential::OccupantMoved(u32, ScOccupant *, const CtCoord &) {
  return true;
}

bool EcResidential::OccupantChanged(u32, ScOccupant *, i32, void *) {
  return true;
}

bool EcResidential::CellChangedAll(u32) {
  return true;
}

bool EcResidential::CellChanged(u32, u32, u32) {
  return true;
}

bool EcResidential::CellChanged(u32, u32, u32, u32, u32) {
  return true;
}
