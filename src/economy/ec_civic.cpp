#include "economy/ec_civic.h"

#include <math.h>
#include <string.h>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_text.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "window/api.h"

// Civic-building descriptors persist staff, effectiveness, funded upkeep and waste-processing state.
enum EcCivicPersistenceKey : u32 {
  kEcSchoolStaffKey = 0x8352d5e9,
  kEcSchoolFundedUpkeepKey = 0x8352d5ea,
  kEcCollegeStaffKey = 0x2352d114,
  kEcCollegeEducationEffectKey = 0x2352d115,
  kEcCollegeFundedUpkeepKey = 0x2352d116,
  kEcHospitalBedsKey = 0xa352d404,
  kEcHospitalHealthEffectKey = 0xa352d405,
  kEcHospitalFundedUpkeepKey = 0xa352d406,
  kEcHospitalDoctorsKey = 0xa352d407,
  kEcLibraryFundedUpkeepKey = 0xa3544e00,
  kEcMuseumFundedUpkeepKey = 0x63544fdd,
  kEcIncineratorCapacityKey = 0xe352c57c,
  kEcIncineratorConsumedKey = 0xe352c57d,
  kEcIncineratorPreviousMonthConsumedKey = 0xe352c57e,
  kEcIncineratorRetiredKey = 0xe352c57f,
  kEcRecyclerEffectivenessKey = 0x4352c330,
  kEcRecyclerPopulationKey = 0x4352c331,
  kEcRecyclerRetiredKey = 0x4352c332,
};

// Adapted from the matched SIMECO native base; all lifetime calls use SIMRCI's own reconstructed occupant receiver.

EcCivicBuilding::EcCivicBuilding()
    : occupancy_flags(0), agent_type(nullptr), cached_attributes(nullptr), cached_sprite(nullptr) {
}

EcCivicBuilding::~EcCivicBuilding() {
  ScRelease(&agent_type);
}

// Clear output before selecting building or city-agent identity. Delegate other requests to the adjusted occupant base.
bool EcCivicBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  return EcCivicOccupant::QueryInterface(iid, out);
}

u32 EcCivicBuilding::AddRef() {
  return EcCivicOccupant::AddRef();
}

u32 EcCivicBuilding::Release() {
  return EcCivicOccupant::Release();
}

// Borrow the adjusted occupant interface without acquiring another reference.
ScOccupant *EcCivicBuilding::AsISC3Occupant() {
  return static_cast<EcCivicOccupant *>(this);
}

// Borrow the city-agent subobject without a query or reference acquisition.
CtAgent *EcCivicBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool EcCivicBuilding::Init(RsKey *key) {
  return EcCivicOccupant::Init(key);
}

// Bind the actual occupant attributes before clearing borrowed building caches. Capture and retain the default agent
// type only when none is stored; failed occupant initialization performs the original second virtual shutdown call.
bool EcCivicBuilding::Init(GeAttrib *value) {
  if (!EcCivicOccupant::Init(value)) {
    Shutdown();
    return false;
  }
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  if (!agent_type) {
    agent_type = GetBaseAttribData()->CityAgentType();
    if (agent_type)
      agent_type->AddRef();
  }
  return true;
}

// Restore generic occupant data before requiring the descriptor's raw state word. Optional name/date failures are ignored;
// a successful raw state load uses the original OR setter, and birthdate outputs are year first, then ordinal day.
bool EcCivicBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !EcCivicOccupant::Init(city, info) || !info->HasLayerData1())
    return false;
  u32 state;
  if (!info->GetLayerData1(&state))
    return false;
  SetOccupancyStateFlag(state);
  if (info->HasNameField()) {
    ScString name;
    if (info->GetNameField(&name))
      SetName(name);
  }
  if (info->HasSimCreateDate()) {
    i32 year = 0, day = 0;
    if (info->GetSimCreateDate(&year, &day)) {
      EcDate date(day, year);
      SetBirthdate(date);
    }
  }
  return true;
}

// Save generic occupant fields before optional custom names and construction dates. Publish a name only when its
// actual generic default exists and differs, then store the live raw occupancy word after all callbacks.
bool EcCivicBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicOccupant::Save(city, info))
    return false;
  if (IsNameChangeable()) {
    ScString name;
    if (GetName(&name)) {
      ScString default_name;
      if (attributes && attributes->GetName(&default_name) && !name.IsEqual(&default_name))
        info->SetNameField(&name);
    }
  }
  if (HasBirthdate()) {
    EcDate date;
    if (GetBirthdate(&date)) {
      u32 day = date.DayOfYear(), year = date.Year();
      info->SetSimCreateDate(year, day);
    }
  }
  info->SetLayerData1(occupancy_flags);
  return true;
}

// Retire the agent type before clearing both borrowed caches. Delegate directly to occupant cleanup after those stores.
void EcCivicBuilding::Shutdown() {
  ScRelease(&agent_type);
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  EcCivicOccupant::Shutdown();
}

// Borrow the original generic attribute pointer directly, bypassing the city-specific default-data selection.
GeAttrib *EcCivicBuilding::GetBaseAttribData() {
  return EcCivicOccupant::GetAttribData();
}

// Borrow cached attributes when present. Otherwise select the city's building default for inactive occupancy states,
// falling back to generic attributes on absent defaults and publishing that borrowed result after callbacks.
GeAttrib *EcCivicBuilding::GetAttribData() {
  if (cached_attributes)
    return cached_attributes;
  if (occupancy_flags != 1 && !(u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = EcGetRciCity()->BuildingLayer();
    if (layer) {
      cached_attributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cached_attributes)
        return cached_attributes;
    }
  }
  cached_attributes = EcCivicOccupant::GetAttribData();
  return cached_attributes;
}

// Select the virtual default layer before invoking this building's qualified indexed getter.
SpDefinition *EcCivicBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return EcCivicBuilding::GetSpriteAttrib(layer);
}

// Reuse the borrowed sprite cache for every layer. Active states use the generic indexed definition; other states
// ask the city's building defaults and fall back to the generic default definition when no result is available.
SpDefinition *EcCivicBuilding::GetSpriteAttrib(u32 layer) {
  if (cached_sprite)
    return cached_sprite;
  if (occupancy_flags == 1 || (u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    cached_sprite = EcCivicOccupant::GetSpriteAttrib(layer);
    return cached_sprite;
  }
  GeBuildingLayer *buildings = EcGetRciCity()->BuildingLayer();
  if (buildings) {
    cached_sprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cached_sprite)
      return cached_sprite;
  }
  cached_sprite = EcCivicOccupant::GetSpriteAttrib();
  return cached_sprite;
}

u32 EcCivicBuilding::GetGeomFlag() {
  return 260;
}

u32 EcCivicBuilding::GetMemUse() const {
  return 44;
}

u32 EcCivicBuilding::TypeId() const {
  return EcCivicOccupant::TypeId();
}

u32 EcCivicBuilding::GetOccupancyState() {
  return u8(occupancy_flags);
}

// Replace the complete occupancy word with the input's low byte. Changed values always invoke virtual sprite refresh.
void EcCivicBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancy_flags != state) {
    occupancy_flags = state;
    RefreshSprite();
  }
}

// Add the requested occupancy bits. Refresh only when the word changes and the input includes a low-byte bit.
void EcCivicBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags | mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Remove the requested occupancy bits. High-byte-only changes intentionally leave the original sprite untouched.
void EcCivicBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags & ~mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Actual stack guard captures the graphics critical section across sprite callbacks and exception retirement.
// This is a native spelling of the original scoped lock, not a substitute game service or lifetime callback.
class EcCivicSpriteLock {
public:
  explicit EcCivicSpriteLock(ScLock *lock);
  virtual ~EcCivicSpriteLock();

private:
  ScLock *lock;
};

EcCivicSpriteLock::EcCivicSpriteLock(ScLock *value) : lock(value) {
  lock->Lock();
}

EcCivicSpriteLock::~EcCivicSpriteLock() {
  lock->Unlock();
}

// Hold the real graphics lock while invalidating only the attribute cache and refreshing the selected sprite instance.
// Preserve definition/output ownership, failed creation outputs and rotation-only success effects.
void EcCivicBuilding::RefreshSprite() {
  EcCivicSpriteLock scope(EcGetRciCity()->GetGraphicsCriticalSection());
  cached_attributes = nullptr;
  SpDefinition *definition = GetSpriteAttrib();
  if (!definition)
    return;
  if (sprite) {
    definition->DestroyInstance(&sprite);
    sprite = nullptr;
  }
  if (definition->CreateInstance(&sprite))
    sprite->SetRotationOffset(u8(position >> kOcPositionRotationShift));
}

bool EcCivicBuilding::Simulate(i32 elapsed) {
  return agent_type->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 EcCivicBuilding::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool EcCivicBuilding::IsAgentLocking() {
  return true;
}

// Borrow the retained agent type without acquiring another reference.
CtAgentType *EcCivicBuilding::AgentType() {
  return agent_type;
}

bool EcCivicBuilding::SetAgentType(CtAgentType *) {
  return false;
}

bool EcCivicBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (u32(mask) & occupancy_flags) != 0;
}

// Query the real building-specific attribute view, capture its raw power byte, then release the live view.
u8 EcCivicBuilding::PowerRequired() {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  void *building = nullptr;
  if (!value->QueryInterface(IID_GeBuildingType, &building))
    return 0;
  u8 power = static_cast<GeBuildingType *>(building)->PowerRequired();
  static_cast<GeBuildingType *>(building)->Release();
  return power;
}

bool EcCivicBuilding::SetName(const ScStringView &) {
  return false;
}

bool EcCivicBuilding::IsNameChangeable() {
  return false;
}

bool EcCivicBuilding::HasBirthdate() const {
  return false;
}

bool EcCivicBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool EcCivicBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

bool EcCivicBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcCivicBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool EcCivicBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

bool EcCivicBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return EcCivicOccupant::DoQueryInfo(message, sender);
}

u8 EcCivicBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcCivicOccupant::QueryDetail(message, query);
}

// Apply the live query option through the real occupancy mutators before delegating to the generic response.
u8 EcCivicBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (query->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return EcCivicOccupant::QueryResponse(message, query);
}

// Installed mutable defaults; the actual constructors replace only values returned by the original INI reader.
u32 EcSchool::optimal_capacity = 300;
u32 EcSchool::optimal_staff = 22;
u8 EcSchool::minimum_age = 6;
u8 EcSchool::maximum_age = 18;
f32 EcSchool::optimal_effect = 0.105f;
u32 EcSchool::optimal_upkeep = 250;
f32 EcSchool::maximum_efficiency = 1.2f;
f32 EcSchool::mystical_e = 30.0f;
u32 EcCollege::optimal_capacity = 500;
u32 EcCollege::optimal_staff = 35;
u8 EcCollege::minimum_age = 19;
u8 EcCollege::maximum_age = 24;
f32 EcCollege::optimal_effect = 0.25f;
u32 EcCollege::optimal_upkeep = 650;
f32 EcCollege::maximum_efficiency = 1.2f;
f32 EcCollege::mystical_e = 30.0f;
u32 EcLibrary::optimal_capacity = 60000;
u8 EcLibrary::minimum_age = 6;
u8 EcLibrary::maximum_age = 18;
f32 EcLibrary::optimal_effect = 0.015f;
u32 EcLibrary::optimal_upkeep = 50;
u32 EcMuseum::optimal_capacity = 60000;
u8 EcMuseum::minimum_age = 6;
u8 EcMuseum::maximum_age = 18;
f32 EcMuseum::optimal_effect = 0.011f;
u32 EcMuseum::optimal_upkeep = 50;
u32 EcHospital::optimal_beds = 500;
u32 EcHospital::optimal_doctors = 60;
u32 EcHospital::optimal_upkeep = 950;
f32 EcHospital::optimal_effect = 0.25f;
f32 EcHospital::maximum_efficiency = 1.2f;
f32 EcHospital::mystical_e = 30.0f;

// Extract the original low DWORD after biased-double rounding, preserving wrapping instead of a signed conversion.
static u32 EcCivicRounded(f64 value) {
  f64 rounded = value + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return u32(bits) + kScDoubleIntegerLowWordBias;
}

// Apply the identical school/college/hospital overfunding curve with the original promoted float constants.
// The caller has already stored upkeep from the uncurved funding fraction.
static f64 EcCivicEfficiency(f64 fraction, f32 maximum, f32 mystical) {
  if (fraction > 1.0) {
    f64 divisor = 1.0 - maximum;
    f64 scale = (f64(1.2f) / divisor + mystical) * f64(4.999999f);
    return maximum - 1.0 / ((scale - 1.0 / divisor) * fraction - scale);
  }
  return fraction;
}

u32 EcSchool::GetKind() {
  return 0;
}

u32 EcSchool::GetCapacity() {
  return optimal_capacity;
}

u32 EcSchool::GetStaffCount() {
  return staff;
}

u8 EcSchool::GetMinimumAge() {
  return minimum_age;
}

u8 EcSchool::GetMaximumAge() {
  return maximum_age;
}

f32 EcSchool::GetEducationEffect() {
  return education_effect;
}

u32 EcCollege::GetKind() {
  return 1;
}

u32 EcCollege::GetCapacity() {
  return optimal_capacity;
}

u32 EcCollege::GetStaffCount() {
  return staff;
}

u8 EcCollege::GetMinimumAge() {
  return minimum_age;
}

u8 EcCollege::GetMaximumAge() {
  return maximum_age;
}

f32 EcCollege::GetEducationEffect() {
  return education_effect;
}

u32 EcLibrary::GetKind() {
  return 2;
}

u32 EcLibrary::GetCapacity() {
  return optimal_capacity;
}

u32 EcLibrary::GetStaffCount() {
  return 0;
}

u8 EcLibrary::GetMinimumAge() {
  return minimum_age;
}

u8 EcLibrary::GetMaximumAge() {
  return maximum_age;
}

f32 EcLibrary::GetEducationEffect() {
  return education_effect;
}

u32 EcMuseum::GetKind() {
  return 3;
}

u32 EcMuseum::GetCapacity() {
  return optimal_capacity;
}

u32 EcMuseum::GetStaffCount() {
  return 0;
}

u8 EcMuseum::GetMinimumAge() {
  return minimum_age;
}

u8 EcMuseum::GetMaximumAge() {
  return maximum_age;
}

f32 EcMuseum::GetEducationEffect() {
  return optimal_effect;
}

u32 EcHospital::GetBedCount() {
  return beds;
}

u32 EcHospital::GetDoctorCount() {
  return doctors;
}

f32 EcHospital::GetHealthEffect() {
  return health_effect;
}

i32 EcSchool::GetMonthlyUpkeep() {
  return i32(base_upkeep);
}

i32 EcCollege::GetMonthlyUpkeep() {
  return i32(base_upkeep);
}

i32 EcLibrary::GetMonthlyUpkeep() {
  return i32(optimal_upkeep);
}

i32 EcMuseum::GetMonthlyUpkeep() {
  return i32(optimal_upkeep);
}

i32 EcHospital::GetMonthlyUpkeep() {
  return i32(base_upkeep);
}

// Store funded upkeep from the original fraction before applying the overfunding curve to staffing and effect.
// Preserve the school-only minimum of one teacher for positive efficiency.
void EcSchool::SetFundingPercentage(u8 percentage) {
  f64 fraction = f64(percentage) * f64(0.01f);
  funded_upkeep = EcCivicRounded(f64(base_upkeep) * fraction);
  f64 efficiency = EcCivicEfficiency(fraction, maximum_efficiency, mystical_e);
  staff = EcCivicRounded(f64(optimal_staff) * efficiency);
  if (!staff && efficiency > 0.0)
    staff = 1;
  education_effect = f32(optimal_effect * efficiency);
}

// Store funded upkeep from the original fraction before applying the overfunding curve to staffing and effect.
// Preserve the absence of the school-only minimum-staff correction.
void EcCollege::SetFundingPercentage(u8 percentage) {
  f64 fraction = f64(percentage) * f64(0.01f);
  funded_upkeep = EcCivicRounded(f64(base_upkeep) * fraction);
  f64 efficiency = EcCivicEfficiency(fraction, maximum_efficiency, mystical_e);
  staff = EcCivicRounded(f64(optimal_staff) * efficiency);
  education_effect = f32(optimal_effect * efficiency);
}

// Store funded upkeep from the original fraction before applying the overfunding curve to staffing and effect.
// Preserve the absence of the school-only minimum-staff correction.
void EcHospital::SetFundingPercentage(u8 percentage) {
  f64 fraction = f64(percentage) * f64(0.01f);
  funded_upkeep = EcCivicRounded(f64(base_upkeep) * fraction);
  f64 efficiency = EcCivicEfficiency(fraction, maximum_efficiency, mystical_e);
  beds = optimal_beds;
  doctors = EcCivicRounded(f64(optimal_doctors) * efficiency);
  health_effect = f32(optimal_effect * efficiency);
}

void EcLibrary::SetFundingPercentage(u8 percentage) {
  funded_upkeep = EcCivicRounded(f64(percentage) * f64(0.01f) * i32(optimal_upkeep));
}

void EcMuseum::SetFundingPercentage(u8 percentage) {
  funded_upkeep = EcCivicRounded(f64(percentage) * f64(0.01f) * i32(optimal_upkeep));
}

u32 EcSchool::AddRef() {
  return EcCivicBuilding::AddRef();
}

u32 EcSchool::Release() {
  return EcCivicBuilding::Release();
}

// Borrow the actual embedded occupant view without acquiring a second reference.
ScOccupant *EcSchool::AsISC3Occupant() {
  return EcCivicBuilding::AsISC3Occupant();
}

u32 EcCollege::AddRef() {
  return EcCivicBuilding::AddRef();
}

u32 EcCollege::Release() {
  return EcCivicBuilding::Release();
}

// Borrow the actual embedded occupant view without acquiring a second reference.
ScOccupant *EcCollege::AsISC3Occupant() {
  return EcCivicBuilding::AsISC3Occupant();
}

u32 EcLibrary::AddRef() {
  return EcCivicBuilding::AddRef();
}

u32 EcLibrary::Release() {
  return EcCivicBuilding::Release();
}

// Borrow the actual embedded occupant view without acquiring a second reference.
ScOccupant *EcLibrary::AsISC3Occupant() {
  return EcCivicBuilding::AsISC3Occupant();
}

u32 EcMuseum::AddRef() {
  return EcCivicBuilding::AddRef();
}

u32 EcMuseum::Release() {
  return EcCivicBuilding::Release();
}

// Borrow the actual embedded occupant view without acquiring a second reference.
ScOccupant *EcMuseum::AsISC3Occupant() {
  return EcCivicBuilding::AsISC3Occupant();
}

u32 EcHospital::AddRef() {
  return EcCivicBuilding::AddRef();
}

u32 EcHospital::Release() {
  return EcCivicBuilding::Release();
}

// Borrow the actual embedded occupant view without acquiring a second reference.
ScOccupant *EcHospital::AsISC3Occupant() {
  return EcCivicBuilding::AsISC3Occupant();
}

bool EcSchool::tuning_loaded;
bool EcCollege::tuning_loaded;
bool EcLibrary::tuning_loaded;
bool EcMuseum::tuning_loaded;
bool EcHospital::tuning_loaded;

static u8 EcReadCivicTuning(EcIni *ini, const char *section_name, const char *key_name, ScString *value) {
  ScString key(key_name, u32(strlen(key_name))), section(section_name, u32(strlen(section_name)));
  return ini->GetValue(&section, &key, value);
}

// Construct the actual SIMRCI building base, then load this class's tuning once through the real packaged INI reader.
// Preserve both application lookups, unsigned conversion/clamps, late guard publication and final instance-store order.
EcSchool::EcSchool() {
  if (!tuning_loaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    if (EcReadCivicTuning(&ini, "School", "OptimalDeskCount", &value))
      optimal_capacity = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "School", "OptimalTeacherCount", &value))
      optimal_staff = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "School", "MinAgeServed", &value))
      minimum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "School", "MaxAgeServed", &value))
      maximum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "School", "OptimalEQEffect", &value)) {
      optimal_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.001f));
      if (optimal_effect < 0.001)
        optimal_effect = 0.001f;
    }
    if (EcReadCivicTuning(&ini, "School", "OptimalMonthlyUpkeep", &value))
      optimal_upkeep = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "School", "MaxEfficiency", &value)) {
      f64 converted = f64(EcParseTuningNumber(value.Data())) * f64(0.01f);
      maximum_efficiency = f32(converted);
      if (converted < 1.0)
        maximum_efficiency = 1.0f;
    }
    if (EcReadCivicTuning(&ini, "School", "MysticalE", &value))
      mystical_e = f32(EcParseTuningNumber(value.Data()));
    tuning_loaded = true;
  }
  staff = optimal_staff;
  education_effect = optimal_effect;
  base_upkeep = optimal_upkeep;
  funded_upkeep = optimal_upkeep;
}

// Construct the actual SIMRCI building base, then load this class's tuning once through the real packaged INI reader.
// Preserve both application lookups, unsigned conversion/clamps, late guard publication and final instance-store order.
EcCollege::EcCollege() {
  if (!tuning_loaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    if (EcReadCivicTuning(&ini, "College", "OptimalDeskCount", &value))
      optimal_capacity = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "College", "OptimalProfCount", &value))
      optimal_staff = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "College", "MinAgeServed", &value))
      minimum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "College", "MaxAgeServed", &value))
      maximum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "College", "OptimalEQEffect", &value)) {
      optimal_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.001f));
      if (optimal_effect < 0.001)
        optimal_effect = 0.001f;
    }
    if (EcReadCivicTuning(&ini, "College", "OptimalMonthlyUpkeep", &value))
      optimal_upkeep = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "College", "MaxEfficiency", &value)) {
      f64 converted = f64(EcParseTuningNumber(value.Data())) * f64(0.01f);
      maximum_efficiency = f32(converted);
      if (converted < 1.0)
        maximum_efficiency = 1.0f;
    }
    if (EcReadCivicTuning(&ini, "College", "MysticalE", &value))
      mystical_e = f32(EcParseTuningNumber(value.Data()));
    tuning_loaded = true;
  }
  staff = optimal_staff;
  education_effect = optimal_effect;
  base_upkeep = optimal_upkeep;
  funded_upkeep = optimal_upkeep;
}

// Construct the actual SIMRCI building base, then load this class's tuning once through the real packaged INI reader.
// Preserve both application lookups, unsigned conversion/clamps, late guard publication and final instance-store order.
EcLibrary::EcLibrary() {
  if (!tuning_loaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    if (EcReadCivicTuning(&ini, "Library", "PopServed", &value))
      optimal_capacity = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "Library", "MinAgeServed", &value))
      minimum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "Library", "MaxAgeServed", &value))
      maximum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "Library", "EQEffect", &value)) {
      optimal_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.001f));
      if (optimal_effect < 0.001)
        optimal_effect = 0.001f;
    }
    if (EcReadCivicTuning(&ini, "Library", "MonthlyUpkeep", &value))
      optimal_upkeep = EcParseTuningNumber(value.Data());
    tuning_loaded = true;
  }
  education_effect = optimal_effect;
  funded_upkeep = optimal_upkeep;
}

// Construct the actual SIMRCI building base, then load this class's tuning once through the real packaged INI reader.
// Preserve both application lookups, unsigned conversion/clamps, late guard publication and final instance-store order.
EcMuseum::EcMuseum() {
  if (!tuning_loaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    if (EcReadCivicTuning(&ini, "Museum", "PopServed", &value))
      optimal_capacity = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "Museum", "MinAgeServed", &value))
      minimum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "Museum", "MaxAgeServed", &value))
      maximum_age = u8(EcParseTuningNumber(value.Data()));
    if (EcReadCivicTuning(&ini, "Museum", "EQEffect", &value)) {
      optimal_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.001f));
      if (optimal_effect < 0.001)
        optimal_effect = 0.001f;
    }
    if (EcReadCivicTuning(&ini, "Museum", "MonthlyUpkeep", &value))
      optimal_upkeep = EcParseTuningNumber(value.Data());
    tuning_loaded = true;
  }
  funded_upkeep = optimal_upkeep;
}

// Construct the actual SIMRCI building base, then load this class's tuning once through the real packaged INI reader.
// Preserve both application lookups, unsigned conversion/clamps, late guard publication and final instance-store order.
EcHospital::EcHospital() {
  if (!tuning_loaded) {
    ScString path, unused_first, value, unused_last;
    EcIni ini;
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3Tune.INI", 16);
    ini.SelectFile(&path);
    EcGetRciApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK", 12);
    ini.SelectPackage(path);
    if (EcReadCivicTuning(&ini, "Hospital", "OptimalBedCount", &value))
      optimal_beds = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "Hospital", "OptimalDoctorCount", &value))
      optimal_doctors = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "Hospital", "OptimalHQEffect", &value)) {
      optimal_effect = f32(f64(EcParseTuningNumber(value.Data())) * f64(0.001f));
      if (optimal_effect < 0.001)
        optimal_effect = 0.001f;
    }
    if (EcReadCivicTuning(&ini, "Hospital", "OptimalMonthlyUpkeep", &value))
      optimal_upkeep = EcParseTuningNumber(value.Data());
    if (EcReadCivicTuning(&ini, "Hospital", "MaxEfficiency", &value)) {
      f64 converted = f64(EcParseTuningNumber(value.Data())) * f64(0.01f);
      maximum_efficiency = f32(converted);
      if (converted < 1.0)
        maximum_efficiency = 1.0f;
    }
    if (EcReadCivicTuning(&ini, "Hospital", "MysticalE", &value))
      mystical_e = f32(EcParseTuningNumber(value.Data()));
    tuning_loaded = true;
  }
  beds = optimal_beds;
  doctors = optimal_doctors;
  health_effect = optimal_effect;
  base_upkeep = optimal_upkeep;
  funded_upkeep = optimal_upkeep;
}

EcSchool::~EcSchool() {
}

EcCollege::~EcCollege() {
}

EcHospital::~EcHospital() {
}

EcLibrary::~EcLibrary() {
}

EcMuseum::~EcMuseum() {
}

// Restore common occupant state before requiring the original class-specific fields in order.
// Preserve earlier successful writes when a later required field is absent.
bool EcSchool::Init(CtCity *city, OcPersistInfo *info) {
  if (!info)
    return false;
  if (!EcCivicBuilding::Init(city, info))
    return false;
  if (!info->GetIntData(kEcSchoolStaffKey, &staff))
    return false;
  if (!info->GetIntData(kEcSchoolFundedUpkeepKey, &funded_upkeep))
    return false;
  return true;
}

// Write common occupant state before the original class-specific integer/float properties.
// Stop at the first refused write without clearing already-published descriptor data.
bool EcSchool::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Save(city, info))
    return false;
  if (!info->SetIntData(kEcSchoolStaffKey, staff))
    return false;
  return info->SetIntData(kEcSchoolFundedUpkeepKey, funded_upkeep) != 0;
}

// Restore common occupant state before requiring the original class-specific fields in order.
// Preserve earlier successful writes when a later required field is absent.
bool EcCollege::Init(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Init(city, info))
    return false;
  if (!info->GetIntData(kEcCollegeStaffKey, &staff))
    return false;
  if (!info->GetFloatData(kEcCollegeEducationEffectKey, &education_effect))
    return false;
  if (!info->GetIntData(kEcCollegeFundedUpkeepKey, &funded_upkeep))
    return false;
  return true;
}

// Write common occupant state before the original class-specific integer/float properties.
// Stop at the first refused write without clearing already-published descriptor data.
bool EcCollege::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Save(city, info))
    return false;
  if (!info->SetIntData(kEcCollegeStaffKey, staff))
    return false;
  if (!info->SetFloatData(kEcCollegeEducationEffectKey, education_effect))
    return false;
  return info->SetIntData(kEcCollegeFundedUpkeepKey, funded_upkeep) != 0;
}

// Restore hospital fields after common occupant state. Any common/field failure restores original hospital defaults
// and succeeds, while a null descriptor is refused before touching the base.
bool EcHospital::Init(CtCity *city, OcPersistInfo *info) {
  if (!info)
    return false;
  if (!EcCivicBuilding::Init(city, info) || !info->GetIntData(kEcHospitalBedsKey, &beds) ||
      !info->GetFloatData(kEcHospitalHealthEffectKey, &health_effect) ||
      !info->GetIntData(kEcHospitalFundedUpkeepKey, &funded_upkeep) ||
      !info->GetIntData(kEcHospitalDoctorsKey, &doctors)) {
    beds = optimal_beds;
    health_effect = optimal_effect;
    funded_upkeep = optimal_upkeep;
    doctors = optimal_doctors;
  }
  return true;
}

// Write common occupant state before the original class-specific integer/float properties.
// Stop at the first refused write without clearing already-published descriptor data.
bool EcHospital::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Save(city, info))
    return false;
  if (!info->SetIntData(kEcHospitalBedsKey, beds))
    return false;
  if (!info->SetFloatData(kEcHospitalHealthEffectKey, health_effect))
    return false;
  if (!info->SetIntData(kEcHospitalFundedUpkeepKey, funded_upkeep))
    return false;
  return info->SetIntData(kEcHospitalDoctorsKey, doctors) != 0;
}

// Restore common occupant state before reading funded upkeep. The original missing-field fallback retains
// the configured upkeep and succeeds after successful common initialization.
bool EcLibrary::Init(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Init(city, info))
    return false;
  if (!info->GetIntData(kEcLibraryFundedUpkeepKey, &funded_upkeep))
    funded_upkeep = optimal_upkeep;
  return true;
}

// Write common occupant state before the original class-specific integer/float properties.
// Stop at the first refused write without clearing already-published descriptor data.
bool EcLibrary::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Save(city, info))
    return false;
  return info->SetIntData(kEcLibraryFundedUpkeepKey, funded_upkeep) != 0;
}

// Restore common occupant state before reading funded upkeep. The original missing-field fallback retains
// the configured upkeep and succeeds after successful common initialization.
bool EcMuseum::Init(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Init(city, info))
    return false;
  if (!info->GetIntData(kEcMuseumFundedUpkeepKey, &funded_upkeep))
    funded_upkeep = optimal_upkeep;
  return true;
}

// Write common occupant state before the original class-specific integer/float properties.
// Stop at the first refused write without clearing already-published descriptor data.
bool EcMuseum::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcCivicBuilding::Save(city, info))
    return false;
  return info->SetIntData(kEcMuseumFundedUpkeepKey, funded_upkeep) != 0;
}

// Clear the caller output, select the actual primary, service or
// upkeep view, and retain through the qualified building base. Delegate all other identities to that same base while
// retaining its successful reference effects.
bool EcSchool::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_EcSchool)
    *out = static_cast<EcCivicView *>(this);
  else if (iid == IID_EcEducationService)
    *out = static_cast<EcEducationService *>(this);
  else if (iid == IID_BdUpkeepView)
    *out = static_cast<BdUpkeepView *>(this);
  else
    return EcCivicBuilding::QueryInterface(iid, out);
  EcCivicBuilding::AddRef();
  return true;
}

// Borrow the actual embedded building identity without acquiring another reference.
BdBuilding *EcSchool::AsISC3Building() {
  return static_cast<EcCivicBuilding *>(this);
}

// Clear the caller output, select the actual primary, service or
// upkeep view, and retain through the qualified building base. Delegate all other identities to that same base while
// retaining its successful reference effects.
bool EcCollege::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_EcCollege)
    *out = static_cast<EcCivicView *>(this);
  else if (iid == IID_EcEducationService)
    *out = static_cast<EcEducationService *>(this);
  else if (iid == IID_BdUpkeepView)
    *out = static_cast<BdUpkeepView *>(this);
  else
    return EcCivicBuilding::QueryInterface(iid, out);
  EcCivicBuilding::AddRef();
  return true;
}

// Borrow the actual embedded building identity without acquiring another reference.
BdBuilding *EcCollege::AsISC3Building() {
  return static_cast<EcCivicBuilding *>(this);
}

// Clear the caller output, select the actual primary, service or
// upkeep view, and retain through the qualified building base. Delegate all other identities to that same base while
// retaining its successful reference effects.
bool EcHospital::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_EcHospital)
    *out = static_cast<EcCivicView *>(this);
  else if (iid == IID_EcHealthService)
    *out = static_cast<EcHealthService *>(this);
  else if (iid == IID_BdUpkeepView)
    *out = static_cast<BdUpkeepView *>(this);
  else
    return EcCivicBuilding::QueryInterface(iid, out);
  EcCivicBuilding::AddRef();
  return true;
}

// Borrow the actual embedded building identity without acquiring another reference.
BdBuilding *EcHospital::AsISC3Building() {
  return static_cast<EcCivicBuilding *>(this);
}

// Clear the caller output, select the actual primary, service or
// upkeep view, and retain through the qualified building base. Delegate all other identities to that same base while
// retaining its successful reference effects.
bool EcLibrary::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_EcLibrary)
    *out = static_cast<EcCivicView *>(this);
  else if (iid == IID_EcEducationService)
    *out = static_cast<EcEducationService *>(this);
  else if (iid == IID_BdUpkeepView)
    *out = static_cast<BdUpkeepView *>(this);
  else
    return EcCivicBuilding::QueryInterface(iid, out);
  EcCivicBuilding::AddRef();
  return true;
}

// Borrow the actual embedded building identity without acquiring another reference.
BdBuilding *EcLibrary::AsISC3Building() {
  return static_cast<EcCivicBuilding *>(this);
}

// Clear the caller output, select the actual primary, service or
// upkeep view, and retain through the qualified building base. Delegate all other identities to that same base while
// retaining its successful reference effects.
bool EcMuseum::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_EcMuseum)
    *out = static_cast<EcCivicView *>(this);
  else if (iid == IID_EcEducationService)
    *out = static_cast<EcEducationService *>(this);
  else if (iid == IID_BdUpkeepView)
    *out = static_cast<BdUpkeepView *>(this);
  else
    return EcCivicBuilding::QueryInterface(iid, out);
  EcCivicBuilding::AddRef();
  return true;
}

// Borrow the actual embedded building identity without acquiring another reference.
BdBuilding *EcMuseum::AsISC3Building() {
  return static_cast<EcCivicBuilding *>(this);
}

u8 EcSchool::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcCivicBuilding::QueryDetail(message, query);
}

u8 EcCollege::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcCivicBuilding::QueryDetail(message, query);
}

u8 EcHospital::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcCivicBuilding::QueryDetail(message, query);
}

u8 EcLibrary::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcCivicBuilding::QueryDetail(message, query);
}

u8 EcMuseum::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcCivicBuilding::QueryDetail(message, query);
}

// SIMRCI's actual module-wide recursive packed-reference lock, original58898.
static ScLock EcCivicLock;

// Adapted from this project's SIMECO occupant implementation after matching the installed SIMRCI copy.
// Module lifetime, reference lock and destructor dispatch use the recovered SIMRCI receiver directly.

EcCivicOccupant::EcCivicOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

EcCivicOccupant::~EcCivicOccupant() {
  if (attributes)
    EcCivicOccupant::Shutdown();
}

// Expose the four identities sharing this occupant receiver. Successful queries acquire a virtual reference.
bool EcCivicOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScQueryTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the module before locking and increment only the packed reference word. Capture the count before unlocking.
u32 EcCivicOccupant::AddRef() {
  EcGetRciDirector()->AddRef();
  EcCivicLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  EcCivicLock.Unlock();
  return count;
}

// Release the module while locked and reload its callback changes before decrementing the live count.
// At zero, unlock before virtual shutdown and destruction; an initially zero count does not release another module hold.
u32 EcCivicOccupant::Release() {
  EcCivicLock.ScLock::Lock();
  if (u16(flags)) {
    EcGetRciDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    if (u16(flags)) {
      EcCivicLock.ScLock::Unlock();
      return u16(flags);
    }
  }
  EcCivicLock.ScLock::Unlock();
  Shutdown();
  delete this;
  return 0;
}

// Release and clear the live attributes before virtual sprite cleanup. Apply the original flag mask after callbacks;
// the shared void contract exposes the cleanup effects without inventing a success result.
void EcCivicOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

bool EcCivicOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool EcCivicOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 EcCivicOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void EcCivicOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void EcCivicOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

u8 EcCivicOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool EcCivicOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void EcCivicOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool EcCivicOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void EcCivicOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

void EcCivicOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool EcCivicOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude to micro coordinates. Exclude orientation bits from altitude.
void EcCivicOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void EcCivicOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 EcCivicOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Replace cell coordinates and altitude from micro coordinates in source store order. Preserve orientation.
void EcCivicOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void EcCivicOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void EcCivicOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool EcCivicOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void EcCivicOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void EcCivicOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void EcCivicOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Borrow the generic attribute pointer without acquiring a reference or consulting the city.
GeAttrib *EcCivicOccupant::GetAttribData() {
  return attributes;
}

u32 EcCivicOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

bool EcCivicOccupant::DoMessage(ScMessage *) {
  return false;
}

bool EcCivicOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 EcCivicOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 EcCivicOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 EcCivicOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 EcCivicOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void EcCivicOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 EcCivicOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u32 EcCivicOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 EcCivicOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

// Follow each borrowed sprite link before releasing its owner. Clear the published head after the final release.
void EcCivicOccupant::DestroySprites(SpSprite **out) {
  SpSprite *current = *out;
  if (current) {
    do {
      SpSprite *next = current->GetNext();
      current->Release();
      current = next;
    } while (current);
    *out = nullptr;
  }
}

// Keep an existing attribute binding unchanged. Otherwise acquire through the application cache, invoke the virtual
// attribute initializer and release its live temporary output; failed cache lookup clears the instance pointer.
bool EcCivicOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!EcGetRciAttribCache()->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool result = Init(value);
  if (value)
    value->Release();
  return result;
}

// Retire old attributes before the sprite chain, retain the new pointer and create its actual default-layer sprite.
// Failed creation invokes virtual shutdown; successful binding adopts the default subtype only when its byte is zero.
bool EcCivicOccupant::Init(GeAttrib *value) {
  if (!value)
    return false;
  ScRelease(&attributes);
  if (sprite)
    DestroySprites(&sprite);
  attributes = value;
  value->AddRef();
  u32 layer = GetDefaultLayerFlag();
  if (!CreateSprite(layer, &sprite)) {
    Shutdown();
    return false;
  }
  if (!u8(flags >> kOcSubtypeShift)) {
    u8 subtype = attributes->SubTypeInfo();
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
  }
  return true;
}

// Bind a descriptor key only when attributes are absent, then apply partial position and rotation outputs even on
// failure. Successful records supply removal flags; failed records enable both permissions without status wrappers.
bool EcCivicOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool result = true;
  if (!attributes) {
    RsKey key{};
    info->GetAttribKey(&key);
    result = Init(&key);
    u32 type = 0;
    if (result)
      result = info->GetTypeId(&type) != 0;
  }
  u32 x = 0, z = 0, altitude = 0, rotation = 0;
  if (result)
    result = info->GetLocationInCells(&x, &z, &altitude) != 0;
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
  if (result)
    result = info->GetRotationFlag(&rotation) != 0;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (result) {
    u32 simulation = info->GetCanSimRemoveFlag() & 1;
    flags = (flags & kOcFlagsWithoutSimRemove) | (simulation << kOcSimRemoveShift);
    u32 user = info->GetCanUserRemoveFlag() & 1;
    flags = (flags & kOcFlagsWithoutUserRemove) | (user << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return result;
}

// Write generic occupant properties to the actual descriptor in source order. The original invokes the X-size
// getter twice: first for the second size argument, then for the first; preserve these distinct virtual calls.
bool EcCivicOccupant::Save(CtCity *, OcPersistInfo *info) {
  if (!attributes)
    return false;
  info->SetAttribKey(*attributes->Key());
  u32 type = TypeId();
  info->SetTypeId(type);
  info->SetLocationInCells(position & kOcPositionCellValueMask,
      (position >> kOcPositionZShift) & kOcPositionCellValueMask, u8(position >> kOcPositionAltitudeShift));
  u32 second = SizeInCellsX();
  u32 first = SizeInCellsX();
  info->SetSizeInCells(first, second);
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  info->SetRemoveFlags(u8((flags >> kOcSimRemoveShift) & 1), u8((flags >> kOcUserRemoveShift) & 1));
  return true;
}

u32 EcCivicOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

// Borrow the default sprite definition from generic attributes without acquiring it.
SpDefinition *EcCivicOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the requested layer definition from generic attributes. Missing attributes produce a null pointer.
SpDefinition *EcCivicOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Borrow the stored sprite instance without changing ownership.
SpSprite *EcCivicOccupant::GetSpriteInst() {
  return sprite;
}

// Compare against the virtual default layer before fetching the virtual instance. Other layers have no instance.
SpSprite *EcCivicOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

// Replace the packed orientation only when its full input differs.
u8 EcCivicOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  u32 layer = GetDefaultLayerFlag();
  CreateSprite(layer, &sprite);
  return 1;
}

// Leave output untouched without attributes. Otherwise capture the minimum before callbacks and compute Y, Z then X
// extents, reloading the packed position after each virtual size getter before publishing maximum coordinates.
void EcCivicOccupant::GetBounds(GeBounds *out) const {
  if (!attributes)
    return;
  u32 value = position;
  out->minimum.x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->minimum.z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->minimum.altitude =
      i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
  u32 ymax = SizeInCellsY();
  ymax += ((position >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) - 1;
  u32 zmax = SizeInCellsZ();
  zmax += ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
  u32 xmax = SizeInCellsX();
  xmax += (position & kOcPositionCellValueMask) - 1;
  out->maximum.x = i32(xmax << kCtMicroCoordinateShift);
  out->maximum.z = i32(zmax << kCtMicroCoordinateShift);
  out->maximum.altitude = i32(ymax << kCtMicroCoordinateShift);
}

// Publish the live cell rectangle only when attributes exist. Size callbacks run X then Z and may change position
// before each corresponding maximum is computed and immediately stored.
void EcCivicOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (!attributes)
    return;
  out->left = position & kOcPositionCellValueMask;
  out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  u32 width = SizeInCellsX();
  out->right = width + (position & kOcPositionCellValueMask) - 1;
  u32 depth = SizeInCellsZ();
  out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
}

u8 EcCivicOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 EcCivicOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 EcCivicOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 EcCivicOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << 8;
}

u32 EcCivicOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << 8;
}

u32 EcCivicOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << 8;
}

u16 EcCivicOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Publish both cell coordinates through the virtual getter, then scale the live outputs in original store order.
void EcCivicOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

bool EcCivicOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Borrow the generic attribute agent type without acquiring a reference. Unbound occupants have no type.
CtAgentType *EcCivicOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

// Select the actual primary sprite-definition or agent-type subsystem through virtual getters.
// Acquire a reference before publishing the selected identity.
bool EcCivicOccupant::GetSubSystemData(u32 id, void **out) {
  if (!attributes)
    return false;
  ScUnknown *selected;
  if (id == IID_SpDefView)
    selected = static_cast<SpDefView *>(GetSpriteAttrib());
  else if (id == IID_CtAgentType)
    selected = CityAgentType();
  else
    return false;
  if (!selected)
    return false;
  selected->AddRef();
  *out = selected;
  return true;
}

u32 EcCivicOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Clear the real query-window output before testing attributes. Resolve the configured class through this module's
// actual COM service, passing the caller's live output directly across that external interface boundary.
bool EcCivicOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 class_id = attributes->GetQueryDialogClass();
  return EcGetRciCom()->GetClassObject(class_id, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

u32 EcCivicOccupant::GetMemUse() const {
  return 20;
}

// Create an instance through the selected generic attribute definition, then set rotation before geometry flags.
// Failure still performs the original borrowed-key getter call; it does not fabricate a sprite or clear output.
u8 EcCivicOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (!definition || !definition->CreateInstance(out)) {
    attributes->Key();
    return false;
  }
  (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
  u32 geometry = GetGeomFlag();
  (*out)->SetGeomFlag(geometry);
  return true;
}

void EcCivicOccupant::AnimStart() {
}

void EcCivicOccupant::AnimStop() {
}

i32 EcCivicOccupant::Tick(u32) {
  return 0;
}

u8 EcCivicOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

bool EcCivicOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcCivicOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool EcCivicOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

// Dispatch supported query notifications through the actual queried interfaces and virtual handlers.
// Release the live queried view after each handler; the original dispatcher always returns false.
bool EcCivicOccupant::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  ScMsgCode type = message->type;
  if (type != kWnMsg_QueryOpened && type != kWnMsg_QueryClosed)
    return false;
  u32 kind = u32(message->data1);
  if (kind == kUiQueryTagDialog) {
    void *view = nullptr;
    if (sender && sender->QueryInterface(IID_UiQueryWindow, &view)) {
      if (type == kWnMsg_QueryOpened)
        QueryBasic(message, static_cast<UiQueryWindow *>(view));
      else
        QueryResponse(message, static_cast<UiQueryWindow *>(view));
      static_cast<UiQueryWindow *>(view)->Release();
    }
  } else if (type == kWnMsg_QueryOpened && (kind == kUiQueryTagDebug || kind == kUiQueryTagStandard)) {
    void *view = nullptr;
    if (sender && sender->QueryInterface(IID_UiQuerySection, &view)) {
      if (kind == kUiQueryTagDebug)
        QueryDetail(message, static_cast<UiQuery *>(view));
      else
        QueryTile(message, static_cast<UiQuery *>(view));
      static_cast<UiQuery *>(view)->Release();
    }
  }
  return false;
}

u8 EcCivicOccupant::QueryBasic(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 EcCivicOccupant::QueryDetail(ScMessage *, UiQuery *) {
  return 0;
}

u8 EcCivicOccupant::QueryResponse(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Add the localized architect row only when the live override attribute supplies a nonempty name.
u8 EcCivicOccupant::QueryTile(ScMessage *, UiQuery *query) {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  OcOverride *override_attributes = value->GetOverRideAttrib();
  if (!override_attributes)
    return 0;
  ScString architect;
  override_attributes->GetArchitectName(&architect);
  if (!architect.Strlen())
    return 0;
  ScString label;
  {
    EcRci::ScTextResource resource(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    label = resource.GetText();
  }
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}
