#include <stdlib.h>
#include <string.h>

#include "advisors/ad_advice.h"
#include "advisors/ad_advisors.h"
#include "advisors/ad_domains.h"
#include "advisors/ad_ini.h"
#include "advisors/ad_module.h"
#include "advisors/ad_text.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_tornado.h"
#include "disasters/ds_toxic.h"
#include "disasters/ds_track.h"
#include "economy/api.h"
#include "economy/ec_residential.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "network/nw_manager.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "ui/api.h"
#include "ui/game_menu.h"

// City-planning advice uses this initial population threshold to classify a medium-sized city.
enum AdPlanningPopulationLimit : i32 {
  kAdPlanningMediumCityPopulation = 0x186a0,
};

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdTransportationPortraitId : u32 {
  kAdTransportationSmallPortraitBase = 0x1000603,
  kAdTransportationLargePortraitBase = 0x603,
};

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdSafetyPortraitId : u32 {
  kAdSafetySmallPortraitBase = 0x1000503,
  kAdSafetyLargePortraitBase = 0x503,
};

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdPlanningPortraitId : u32 {
  kAdPlanningSmallPortraitBase = 0x1000303,
  kAdPlanningLargePortraitBase = 0x303,
};

// Disaster notifications share a category identity for each displayed event family; flood and impact share one slot.
enum AdDisasterNotificationCategory : u32 {
  kAdRiotNotificationCategory = 0x236ba83c,
  kAdEarthquakeNotificationCategory = 0xa36ba86a,
  kAdBuildingFireNotificationCategory = 0x236ba814,
  kAdOpenGroundFireNotificationCategory = 0x36ba829,
  kAdUfoNotificationCategory = 0x636ba85d,
  kAdToxicNotificationCategory = 0xe47f6914,
  kAdTornadoNotificationCategory = 0x36ba84e,
  kAdFloodImpactNotificationCategory = 0x64fb51e0,
  kAdLocustNotificationCategory = 0x4fb51ff,
};

bool AdCityPlanningAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdCityPlanning, AdCityPlanning);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdCityPlanningAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdCityPlanningAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdCityPlanningAdvisor::GetAdvisorID() {
  return kAdCityPlanningAdvisorId;
}

u32 AdCityPlanningAdvisor::GetLayerType() {
  return kAdCityPlanningAdvisorLayerType;
}

// Borrow the genuine city-agent subobject from the complete planning advisor without adding a reference.
CtAgent *AdCityPlanningAdvisor::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Borrow the separate advisor identity while leaving the object's shared reference count unchanged.
AdAdvisor *AdCityPlanningAdvisor::AsISC3BaseAdvisor() {
  return static_cast<AdAdvisor *>(this);
}

#if defined(_M_IX86)
static_assert(sizeof(AdCityPlanningAdvisor) == 792);
static_assert(sizeof(AdPlanningCallback3) == 16);
static_assert(sizeof(AdPlanningCallback4) == 16);
static_assert(offsetof(AdCityPlanningAdvisor, rewards) == 164);
static_assert(offsetof(AdCityPlanningAdvisor, reward_types) == 176);
static_assert(offsetof(AdCityPlanningAdvisor, stock_exchange_funds) == 224);
static_assert(offsetof(AdCityPlanningAdvisor, sample_118) == 280);
static_assert(offsetof(AdCityPlanningAdvisor, sample_148) == 328);
static_assert(offsetof(AdCityPlanningAdvisor, sample_178) == 376);
static_assert(offsetof(AdCityPlanningAdvisor, flag_1fe) == 510);
static_assert(offsetof(AdCityPlanningAdvisor, budget) == 696);
static_assert(offsetof(AdCityPlanningAdvisor, reward_lock) == 784);
#endif

// Construct empty reward collections, defined service zeros and the owned lock before resolving advisor resources.
// Preserve the actual language portraits, tuning defaults and interleaved three-array initialization.
AdCityPlanningAdvisor::AdCityPlanningAdvisor() {
  budget = nullptr;
  valves = nullptr;
  neighbors = nullptr;
  residential = nullptr;
  commercial = nullptr;
  industrial = nullptr;
  buildings = nullptr;
  aura = nullptr;
  budget_advisor = nullptr;
  planning_ordinances = nullptr;
  zones = nullptr;
  land_value = nullptr;
  pollution = nullptr;
  occupants = nullptr;
  residential_valve = nullptr;
  commercial_valve = nullptr;
  industrial_valve = nullptr;
  callback3 = nullptr;
  callback4 = nullptr;
  reward_lock = nullptr;
  reward_lock = new ScLock;
  ScString name = AdTextHandle(kRsAdvisorNameCityPlannerText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetNameAdvice(&name);
  name = AdTextHandle(kRsGameCityPlanningBriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGameCityPlanningOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameConstanceLeeText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  u32 variant = language != kScLanguageCurrent &&
          (language <= kScLanguageBritishEnglish || language == kScLanguageJapanese ||
              (language > kScLanguagePolish && language <= kScLanguageThai))
      ? 0
      : 1;
  const RsKey small = {kRsImageResourceType, kRsAdvisorPortraitGroup, kAdPlanningSmallPortraitBase + (variant << 16)};
  const RsKey large = {kRsImageResourceType, kRsAdvisorPortraitGroup, kAdPlanningLargePortraitBase + (variant << 16)};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  flag_30d = 0;
  flag_30e = 0;
  flag_1fe = 0;
  non_development_sensitivity = 10;
  park_ratio = 10;
  stock_exchange_funds = 50000;
  industrial_negative = 10;
  commercial_negative = 10;
  residential_negative = 10;
  industrial_positive = 10;
  commercial_positive = 10;
  residential_positive = 10;
  commercial_cap_problem = 75;
  industrial_cap_problem = 75;
  small_city = 20000;
  medium_city = kAdPlanningMediumCityPopulation;
  high_tech_eq = 120;
  high_tech_job_ratio = 50;
  parks_for_national_park = 20;
  medical_center_le = 70;
  science_center_eq = 125;
  university_eq = 100;
  airport_for_spaceport = 30;
  responsiveness_months = 6;
  low_land_value = 40;
  farm_air_pollution = 33;
  farm_water_pollution = 33;
  park_aura = 70;
  outdated_response = 301;
  for (u32 index = 0; index < 12; ++index) {
    sample_118[index] = 0;
    sample_148[index] = 0;
    sample_178[index] = 0;
  }
}

AdCityPlanningAdvisor::~AdCityPlanningAdvisor() {
  ScDelete(&reward_lock);
}

// Find one exact building-type key in the native ordered reward map.
// Each result is consumed before a virtual call; later accesses repeat the lookup across callback mutations.
static AdTypedReward *AdFindPlanningReward(AdCityPlanningAdvisor *owner, u32 type) {
  for (u32 index = 0; index < owner->reward_types.size; ++index) {
    AdTypedReward *entry = &owner->reward_types.data[index];
    if (entry->type == type)
      return entry;
    if (entry->type > type)
      break;
  }
  return nullptr;
}

// Enable an existing configured business-deal reward under the originally cached lock.
// Missing types return false without inserting a record or changing other raw flags.
bool AdCityPlanningAdvisor::AddBusinessDealBuildingToRewardList(u32 type) {
  ScLock *lock = reward_lock;
  lock->Lock();
  AdTypedReward *entry = AdFindPlanningReward(this, type);
  bool found = entry != nullptr;
  if (found)
    *entry->entry->AvailableData() = 1;
  lock->Unlock();
  return found;
}

bool AdCityPlanningAdvisor::IsBusinessDealBuildingInRewardList(u32 type) {
  ScLock *lock = reward_lock;
  lock->Lock();
  AdTypedReward *entry = AdFindPlanningReward(this, type);
  bool found =
      entry && *entry->entry->AvailableData() == 1 && *AdFindPlanningReward(this, type)->entry->Flag77Data() == 0;
  lock->Unlock();
  return found;
}

// Disable an existing configured business-deal reward while retaining the record and its other state.
// Missing types return false and the original cached lock is released on either path.
bool AdCityPlanningAdvisor::RemoveBusinessDealBuildingFromRewardList(u32 type) {
  ScLock *lock = reward_lock;
  lock->Lock();
  AdTypedReward *entry = AdFindPlanningReward(this, type);
  bool found = entry != nullptr;
  if (found)
    *entry->entry->AvailableData() = 0;
  lock->Unlock();
  return found;
}

// Take one semicolon-delimited field and leave the rest of the original configuration value in place.
// A missing separator yields an empty field and retains the input, matching the defined splitter behavior.
static ScString AdTakePlanningField(ScString *input) {
  u32 length = static_cast<u32>(input->end - input->begin), position = 0;
  while (position < length && input->begin[position] != ';')
    ++position;
  if (position == length)
    return ScString();
  ScString prefix(input->begin, position);
  input->ScString::Erase(0, position + 1);
  return ScString(prefix);
}

static ScString AdResolvePlanningText(u32 instance, u32 group) {
  return AdTextHandle(instance, group).ToString();
}

// Retain the acquired building attribute until after the actual record's text, flags and map entry have been populated.
void AdCityPlanningAdvisor::ConfigureBuilding(u32 selector, const ScString *key, const ScString *value) {
  GeBuildingType *attribute = nullptr;
  ScString remaining(*value), name;
  RsKey name_key = {};
  ScString unused = AdTakePlanningField(&remaining);
  u32 type = AdParseIniNumber(const_cast<char *>(key->ToChar()));
  ScString field = AdTakePlanningField(&remaining);
  u32 first = AdParseIniNumber(field.begin);
  field = AdTakePlanningField(&remaining);
  u32 second = AdParseIniNumber(field.begin);
  field = AdTakePlanningField(&remaining);
  u32 third = AdParseIniNumber(field.begin), flag = AdParseIniNumber(remaining.begin);
  if (!type || !AdGetOccupantAttribCache()->GetBuildingAttrib(type, &attribute))
    return;
  AdRewardEntry *entry = new AdRewardEntry;
  entry->AddRef();
  entry->SetKey(attribute->Key());
  *entry->Value64() = first;
  *entry->Value68() = second;
  *entry->Value72() = third;
  *entry->AvailableData() = 0;
  *entry->Flag77Data() = 0;
  *entry->Flag79Data() = static_cast<u8>(selector);
  entry->GetCachedAttrib()->GetNameKey(&name_key);
  name = AdResolvePlanningText(name_key.instance, name_key.group);
  entry->SetThirdText(&name);
  u32 cost = static_cast<u32>(budget->GetCost(type));
  if (cost != 0) {
    ScString description = AdResolvePlanningText(kRsGameCostText, kAdAdvisorLabelGroup);
    AdGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(static_cast<i64>(cost), &field);
    description.ScString::Append(field.begin, static_cast<u32>(field.end - field.begin));
    entry->SetFirstText(&description);
  }
  *entry->Flag78Data() = static_cast<u8>(flag != 0);
  AdTypedReward *existing = AdFindPlanningReward(this, type);
  if (existing)
    existing->entry = entry;
  else {
    u32 index = 0;
    while (index < reward_types.size && reward_types.data[index].type < type)
      ++index;
    reward_types.Append();
    for (u32 next = reward_types.size - 1; next > index; --next)
      reward_types.data[next] = reward_types.data[next - 1];
    reward_types.data[index] = {type, entry};
  }
  attribute->Release();
}

static void AdPlanningRewardIniEntry(const ScString &key, const ScString &value, void *identity) {
  AdCityPlanningAdvisor *owner = static_cast<AdCityPlanningAdvisor *>(static_cast<AdCityPlanning *>(identity));
  owner->ConfigureBuilding(0, &key, &value);
}

static void AdPlanningBusinessIniEntry(const ScString &key, const ScString &value, void *identity) {
  AdCityPlanningAdvisor *owner = static_cast<AdCityPlanningAdvisor *>(static_cast<AdCityPlanning *>(identity));
  owner->ConfigureBuilding(1, &key, &value);
}

static bool AdReadPlanningSetting(AdIni *reader, const char *section_name, const char *key_name, ScString *out) {
  ScString key(key_name), section(section_name);
  return reader->ReadEntry(section, key, out);
}

// Missing scalar keys make the final result false but never skip the two ordered building enumerations.
bool AdCityPlanningAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-CityPlanner.INI", 28);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "SMALL_SIZE_CITY", &value))
    small_city = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "MEDIUM_SIZE_CITY", &value))
    medium_city = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "NON_DEV_SENSITIVITY", &value))
    non_development_sensitivity = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "MIN_HI_TECH_EQ", &value))
    high_tech_eq = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "HI_TECH_JOB_RATIO", &value))
    high_tech_job_ratio = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "PARK_RATIO", &value))
    park_ratio = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "PARK_AURA_CUTOFF", &value))
    park_aura = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "GROWTH_CAP_LIMITS", "COMMERCIAL_CAP_PROBLEM", &value))
    commercial_cap_problem = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "GROWTH_CAP_LIMITS", "INDUSTRIAL_CAP_PROBLEM", &value))
    industrial_cap_problem = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "REWARD_BUILDINGS_2", "PARKS_NEEDED_FOR_NATIONAL_PARK", &value))
    parks_for_national_park = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "REWARD_BUILDINGS_2", "LE_NEEDED_FOR_MEDICAL_CENTER", &value))
    medical_center_le = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "REWARD_BUILDINGS_2", "EQ_NEEDED_FOR_SCIENCE_CENTER", &value))
    science_center_eq = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "REWARD_BUILDINGS_2", "EQ_NEEDED_FOR_UNIVERSITY", &value))
    university_eq = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "REWARD_BUILDINGS_2", "FUNDS_NEEDED_FOR_STOCK_EXCHANGE", &value))
    stock_exchange_funds = atoi(value.begin);
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "REWARD_BUILDINGS_2", "AIRPORT_NEEDED_FOR_SPACEPORT", &value))
    airport_for_spaceport = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_CUTOFFS", "RESIDENTIAL_DEMAND_NEGATIVE", &value))
    residential_negative = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_CUTOFFS", "RESIDENTIAL_DEMAND_POSITIVE", &value))
    residential_positive = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_CUTOFFS", "COMMERCIAL_DEMAND_NEGATIVE", &value))
    commercial_negative = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_CUTOFFS", "COMMERCIAL_DEMAND_POSITIVE", &value))
    commercial_positive = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_CUTOFFS", "INDUSTRIAL_DEMAND_NEGATIVE", &value))
    industrial_negative = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_CUTOFFS", "INDUSTRIAL_DEMAND_POSITIVE", &value))
    industrial_positive = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "DEMAND_RESPONSIVENESS", "RESPONSIVENESS_IN_MONTHS", &value))
    responsiveness_months = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "LOW_LAND_VALUE", &value))
    low_land_value = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "LOW_AIR_POLLUTION_FARMS", &value))
    farm_air_pollution = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadPlanningSetting(&reader, "ADVICE_TRIGGERS", "LOW_WATER_POLLUTION_FARMS", &value))
    farm_water_pollution = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  {
    ScString section("REWARD_BUILDINGS");
    reader.EnumEntries(section, AdPlanningRewardIniEntry, static_cast<AdCityPlanning *>(this));
  }
  {
    ScString section("BUSINESS_DEAL_BUILDINGS");
    reader.EnumEntries(section, AdPlanningBusinessIniEntry, static_cast<AdCityPlanning *>(this));
  }
  return !missing;
}

// Load resource-keyed reward flags before the shared advisor load, retaining prior successful rows on refusal.
bool AdCityPlanningAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  const RsKey key = {kRsCityLayerRecordType, kAdCityPlanningAdvisorId, 0};
  ScArchiveFrame frame(input, segment, key);
  RsStream *stream = nullptr;
  u8 success = frame.valid;
  if (success) {
    stream = frame.GetStream();
    success = static_cast<u8>(stream != nullptr);
  }
  u32 count = 0;
  if (success)
    success = stream->ReadU32(&count);
  if (success) {
    RsKey saved = {};
    for (u32 row = 0; row < count && success; ++row) {
      success = stream->ReadKey(&saved);
      if (!success)
        break;
      u8 byte;
      success = stream->ReadU8(&byte);
      if (!success)
        break;
      u8 flag77 = static_cast<u8>(byte != 0);
      success = stream->ReadU8(&byte);
      if (!success)
        break;
      u8 available = static_cast<u8>(byte != 0);
      RsKey candidate = {};
      GeAttrib *attribute = nullptr;
      u32 index = 0;
      while (index < reward_types.size) {
        u32 type = reward_types.data[index].type;
        AdRewardEntry *entry = reward_types.data[index].entry;
        if (entry)
          attribute = entry->GetCachedAttrib();
        if (attribute) {
          const RsKey *resolved = attribute->Key();
          candidate.type = resolved->type;
          candidate.group = resolved->group;
          candidate.instance = resolved->instance;
        }
        if (saved.instance == candidate.instance && saved.group == candidate.group && saved.type == candidate.type) {
          *AdFindPlanningReward(this, type)->entry->Flag77Data() = flag77;
          *AdFindPlanningReward(this, type)->entry->AvailableData() = available;
        }
        index = 0;
        while (index < reward_types.size && reward_types.data[index].type <= type)
          ++index;
      }
    }
  }
  if (!success)
    frame.Fail();
  return AdBaseAdvisor::Init(input, segment);
}

// Save selected reward records in original building-type order without retaining the temporary record pointers.
// Retire the selection before marking refusal and close this frame before invoking the separate shared advisor save.
bool AdCityPlanningAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  {
    const RsKey key = {kRsCityLayerRecordType, kAdCityPlanningAdvisorId, 0};
    ScArchiveFrame frame(input, segment, key, 1, 0);
    RsStream *stream = nullptr;
    u8 success = frame.valid;
    if (success) {
      stream = frame.GetStream();
      success = static_cast<u8>(stream != nullptr);
    }
    if (success) {
      ScArray<AdRewardEntry *> selected;
      u32 index = 0;
      while (index < reward_types.size) {
        u32 type = reward_types.data[index].type;
        AdRewardEntry *entry = reward_types.data[index].entry;
        if (entry && (*entry->AvailableData() != 0 || *entry->Flag77Data() != 0))
          *selected.Append() = entry;
        index = 0;
        while (index < reward_types.size && reward_types.data[index].type <= type)
          ++index;
      }
      success = stream->WriteU32(selected.size);
      RsKey saved = {};
      for (index = 0; index < selected.size && success; ++index) {
        AdRewardEntry *entry = selected.data[index];
        GeAttrib *attribute = entry->GetCachedAttrib();
        success = static_cast<u8>(attribute != nullptr);
        if (attribute) {
          const RsKey *resolved = attribute->Key();
          saved.type = resolved->type;
          saved.group = resolved->group;
          saved.instance = resolved->instance;
          success = stream->WriteKey(&saved);
          u8 byte = *entry->Flag77Data();
          if (success)
            success = stream->WriteU8(byte);
          byte = *entry->AvailableData();
          if (success)
            success = stream->WriteU8(byte);
        }
      }
    }
    if (!success)
      frame.Fail();
  }
  return AdBaseAdvisor::Save(input, segment);
}

// Borrow the budget advisor, add special-city advice and register the actual planning agent before its two notifications.
// Post all surface occupants to the real layer target before returning the shared simulation-begin result.
bool AdCityPlanningAdvisor::SimulationBegin() {
  budget_advisor = city->GetBudgetAdvisor();
  if (city->GetCityType() == 4 || city->GetCityType() == 7)
    AddAdvice(kAdWelcomeFromConstanceLeeAdviceId);
  CtAgent *agent = static_cast<AdCityPlanning *>(this)->AsISC3CityAgent();
  city->CitySimulator()->AddAgent(agent);
  ScMessageServer *server = AdGetMessageServer();
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  if (server) {
    server->AddNotification(target, kOcMsg_SurfaceInserted);
    server->AddNotification(target, kOcMsg_SurfaceRemoved);
  }
  occupants->PostOccupantAll(target);
  return AdBaseAdvisor::SimulationBegin();
}

// Resolve and remove the agent before removing the two simulation-only notifications through the captured server.
// Simulator and notification refusals do not replace the original shared simulation-end result.
bool AdCityPlanningAdvisor::SimulationEnd() {
  CtAgent *agent = static_cast<AdCityPlanning *>(this)->AsISC3CityAgent();
  city->CitySimulator()->RemoveAgent(agent);
  ScMessageServer *server = AdGetMessageServer();
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  if (server) {
    server->RemoveNotification(target, kOcMsg_SurfaceInserted);
    server->RemoveNotification(target, kOcMsg_SurfaceRemoved);
  }
  return AdBaseAdvisor::SimulationEnd();
}

// Apply9304's ordered keyed-record release pass before clearing the whole collection's metadata.
// Resume after the original key across virtual releases so insertions before it are skipped as by the real iterator.
void AdCityPlanningAdvisor::ClearRewards() {
  u32 index = 0;
  while (index < rewards.size) {
    RsKey key = rewards.data[index].key;
    rewards.data[index].entry->Release();
    index = 0;
    while (index < rewards.size &&
        (rewards.data[index].key.type != key.type || rewards.data[index].key.group != key.group ||
            rewards.data[index].key.instance != key.instance))
      ++index;
    ++index;
  }
  rewards.Clear();
}

// Remove the two lifetime notifications and four cheats before releasing retained city services in their original order.
// Release all keyed rewards before clearing them, then release and erase each type-keyed record before shared shutdown.
bool AdCityPlanningAdvisor::Shutdown() {
  ScMessageServer *server = AdGetMessageServer();
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  if (server) {
    server->RemoveNotification(target, kCtMsg_OrdinanceAvailable);
    server->RemoveNotification(target, kOcMsg_AttributesChanged);
  }
  const u32 cheats[] = {
      kAdStopForcingAdviceCheatId, kAdForceConstanceToSayCheatId, kAdPayTributeToYourKingCheatId, kAdZyxwvuCheatId};
  for (u32 id : cheats) {
    AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(id);
    AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, id);
  }
  ScRelease(&city);
  ScRelease(&occupants);
  ScRelease(&budget);
  ScRelease(&neighbors);
  ScRelease(&valves);
  ScRelease(&residential);
  ScRelease(&commercial);
  ScRelease(&industrial);
  ScRelease(&buildings);
  ScRelease(&planning_ordinances);
  ScRelease(&aura);
  ScRelease(&zones);
  ScRelease(&land_value);
  ScRelease(&pollution);
  ScRelease(&networks);
  ScRelease(&residential_valve);
  ScRelease(&commercial_valve);
  ScRelease(&industrial_valve);
  ClearRewards();
  while (reward_types.size != 0) {
    u32 type = reward_types.data[0].type;
    reward_types.data[0].entry->Release();
    u32 index = 0;
    while (index < reward_types.size && reward_types.data[index].type != type)
      ++index;
    for (u32 next = index + 1; next < reward_types.size; ++next)
      reward_types.data[next - 1] = reward_types.data[next];
    --reward_types.size;
  }
  return AdBaseAdvisor::Shutdown();
}

AdPlanningCallback3::AdPlanningCallback3(AdCityPlanningAdvisor *input) : owner(input) {
}

AdPlanningCallback3::~AdPlanningCallback3() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdPlanningCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdPlanningCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdPlanningCallback3::Release() {
  return ScRefCount::Release();
}

bool AdPlanningCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdPlanningCallback3::IsLockingCity() {
  return false;
}

bool AdPlanningCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdPlanningCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdPlanningCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdPlanningCallback4::AdPlanningCallback4(AdCityPlanningAdvisor *input) : owner(input) {
}

AdPlanningCallback4::~AdPlanningCallback4() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdPlanningCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdPlanningCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdPlanningCallback4::Release() {
  return ScRefCount::Release();
}

bool AdPlanningCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdPlanningCallback4::IsLockingCity() {
  return false;
}

bool AdPlanningCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdPlanningCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdPlanningCallback4::DebugTypeTag(ScStringView *) {
  return false;
}

// Find the existing resource-keyed reward without making a cache or record reference.
// Repeat this native lookup after virtual calls so original mapped-payload changes remain visible.
static AdKeyedReward *AdFindKeyedPlanningReward(AdCityPlanningAdvisor *owner, const RsKey &key) {
  for (u32 index = 0; index < owner->rewards.size; ++index) {
    AdKeyedReward *entry = &owner->rewards.data[index];
    if (entry->key.type == key.type && entry->key.group == key.group && entry->key.instance == key.instance)
      return entry;
  }
  return nullptr;
}

// Apply B0DD's actual available-reward rows without adding the lock used only by the other writer.
// Snapshot resource-key words before creating a row, then reload each actual text/flag from the live mapped record.
void AdCityPlanningAdvisor::WriteRewardRows(UiQueryRows *rows) {
  u32 index = 0;
  while (index < rewards.size) {
    RsKey map_key = rewards.data[index].key;
    if (*rewards.data[index].entry->AvailableData() == 1) {
      RsKey key = *AdFindKeyedPlanningReward(this, map_key)->entry->GetKey();
      UiQueryRow *row = rows->CreateRow();
      row->SetText(kUiCatalogNameField, AdFindKeyedPlanningReward(this, map_key)->entry->ThirdText());
      row->SetU32(kUiCatalogResourceTypeField, key.type);
      row->SetU32(kUiCatalogResourceGroupField, key.group);
      row->SetU32(kUiCatalogResourceInstanceField, key.instance);
      row->SetText(kUiCatalogDescriptionField, AdFindKeyedPlanningReward(this, map_key)->entry->FirstText());
      row->SetU32(kUiCatalogAlreadyPlacedField, *AdFindKeyedPlanningReward(this, map_key)->entry->Flag77Data());
      rows->AppendRow(row);
    }
    index = 0;
    while (index < rewards.size &&
        (rewards.data[index].key.type != map_key.type || rewards.data[index].key.group != map_key.group ||
            rewards.data[index].key.instance != map_key.instance))
      ++index;
    ++index;
  }
}

// Apply B1DD's configured-reward rows while holding the originally cached lock.
// Preserve key lookup before filtering, exact raw-byte predicates and conditional reveal of selector0 records.
void AdCityPlanningAdvisor::WriteConfiguredRewardRows(UiQueryRows *rows) {
  ScLock *lock = reward_lock;
  lock->Lock();
  u32 index = 0;
  while (index < reward_types.size) {
    u32 type = reward_types.data[index].type;
    RsKey key = *reward_types.data[index].entry->GetKey();
    if (*AdFindPlanningReward(this, type)->entry->Flag77Data() == 0 &&
        (*AdFindPlanningReward(this, type)->entry->AvailableData() == 1 ||
            (flag_30d != 0 && *AdFindPlanningReward(this, type)->entry->Flag79Data() == 0))) {
      UiQueryRow *row = rows->CreateRow();
      row->SetText(kUiCatalogNameField, AdFindPlanningReward(this, type)->entry->ThirdText());
      row->SetU32(kUiCatalogResourceTypeField, key.type);
      row->SetU32(kUiCatalogResourceGroupField, key.group);
      row->SetU32(kUiCatalogResourceInstanceField, key.instance);
      row->SetText(kUiCatalogDescriptionField, AdFindPlanningReward(this, type)->entry->FirstText());
      rows->AppendRow(row);
    }
    index = 0;
    while (index < reward_types.size && reward_types.data[index].type <= type)
      ++index;
  }
  lock->Unlock();
}

// Populate the requested planning row collection through its actual queried interface and release that acquisition.
// The original returns false even after successful row population, so effects never imply a handled result here.
bool AdCityPlanningAdvisor::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  UiQueryRows *rows = nullptr;
  if (message->type == kWnMsg_QueryOpened) {
    u32 selector = static_cast<u32>(message->data1);
    if ((selector == kUiQueryLandmarks || selector == kUiQueryRewards) &&
        source->QueryInterface(IID_UiQueryRows, reinterpret_cast<void **>(&rows))) {
      if (selector == kUiQueryLandmarks)
        WriteRewardRows(rows);
      else
        WriteConfiguredRewardRows(rows);
      rows->Release();
    }
  }
  return false;
}

// Queue planning advice from the cached growth, demand, zoning and pollution samples.
// Retain the original signed demand tests and separate replacement groups for commercial and industrial caps.
bool AdCityPlanningAdvisor::CheckOnAdvice() {
  bool incomplete_neighbors = neighbors->CountNeighbors() != 4;
  if (unknown_24d > commercial_cap_problem) {
    i32 count = static_cast<i32>(unknown_21c);
    if (count > 0) {
      if (unknown_1a9 != 0)
        AddAdvice(
            unknown_240 == unknown_248 ? kAdCommerceNeedsAirportExpansionAdviceId : kAdCommerceNeedsConnectionsAdviceId,
            kAdReplacementCommercialConnections);
    } else if (count == 0 && unknown_1a8 != 0)
      AddAdvice(kAdConnectionsNeededForCommercialToGrowAdviceId, kAdReplacementCommercialConnections);
  }
  if (unknown_24e > industrial_cap_problem) {
    u32 count = unknown_248;
    if (count == 0 || !incomplete_neighbors || unknown_244 != 0 || unknown_218 != 0) {
      if (count <= unknown_244) {
        if (incomplete_neighbors && static_cast<i32>(unknown_218) > 0)
          AddAdvice(kAdIndustryNeedsBiggerSeaportAdviceId, kAdReplacementIndustrialConnections);
      } else
        AddAdvice(kAdIndustryRailingForMoreRailLinesAdviceId, kAdReplacementIndustrialConnections);
    } else
      AddAdvice(kAdIndustryNeedsConnectionsAdviceId, kAdReplacementIndustrialConnections);
  }
  u32 population = unknown_1f8;
  if (population >= small_city) {
    if (population < medium_city) {
      u32 cutoff = non_development_sensitivity;
      if ((unknown_250 > cutoff && unknown_1c0 > 0 && unknown_2ac > cutoff) ||
          (unknown_254 > cutoff && unknown_1c8 > 0 && unknown_2b0 > cutoff) ||
          (unknown_258 > cutoff && unknown_1d0 > 0 && unknown_2b4 > cutoff))
        AddAdvice(kAdZonesNotDevelopingAdviceId, 0);
    }
  } else {
    u32 cutoff = non_development_sensitivity;
    if (unknown_250 > cutoff && unknown_1c0 < 0 && unknown_1d8 < 0 && (unknown_254 < cutoff || unknown_258 < cutoff))
      AddAdvice(kAdCommercialAndIndustrialZonesNeededAdviceId, 0);
    cutoff = non_development_sensitivity;
    if (((unknown_254 > cutoff && unknown_1e0 < 0) || (unknown_258 > cutoff && unknown_1e8 < 0)) && unknown_1c0 > 0 &&
        unknown_250 < cutoff)
      AddAdvice(kAdResidentialZonesNeededAdviceId, 0);
    cutoff = non_development_sensitivity;
    if (unknown_250 < cutoff && unknown_1c0 > 0 && (unknown_290 > cutoff || unknown_294 > cutoff))
      AddAdvice(kAdBusinessesLeaveTownAdviceId, 0);
    cutoff = non_development_sensitivity;
    if (unknown_28c > cutoff && unknown_1c0 < 0 && (unknown_254 < cutoff || unknown_258 < cutoff))
      AddAdvice(kAdUnemployedSimsLeaveTownAdviceId, 0);
    if (unknown_1ad < farm_air_pollution && unknown_1ae < farm_water_pollution &&
        unknown_220 < non_development_sensitivity)
      AddAdvice(kAdFarmZonesSuggestedAdviceId, 0);
  }
  u32 cutoff = non_development_sensitivity;
  if (unknown_268 > cutoff || unknown_26c > cutoff || unknown_270 > cutoff)
    AddReplaceableAdvice(kAdBuildingsAbandonedNeedPowerAdviceId, 0, 4);
  cutoff = non_development_sensitivity;
  if (unknown_274 > cutoff || unknown_278 > cutoff || unknown_27c > cutoff)
    AddReplaceableAdvice(kAdBuildingsAbandonedNeedWaterAdviceId, 0, 4);
  cutoff = non_development_sensitivity;
  if (unknown_280 > cutoff || unknown_284 > cutoff || unknown_288 > cutoff)
    AddReplaceableAdvice(kAdPoorTransportationCausesZonesToAbandonAdviceId, 0, 4);
  if (unknown_298 != 0)
    AddReplaceableAdvice(kAdRadiationStopsGrowthInAreaAdviceId, 0, 6);
  if (unknown_29c != 0)
    AddReplaceableAdvice(kAdHighLandValuesSlowDevelopmentAdviceId, 0, 6);
  if (static_cast<i32>(unknown_224) > 0)
    AddDismissibleAdvice(kAdFirstCleanIndustryDevelopsAdviceId, 0, 8);
  if (static_cast<i32>(unknown_228) > 0)
    AddDismissibleAdvice(220, 0, 8);
  if (static_cast<i32>(unknown_22c) > 0)
    AddDismissibleAdvice(221, 0, 8);
  if (unknown_1b8 < low_land_value)
    AddAdvice(kAdLandValuesLowAdviceId, 0);
  if (unknown_260 > 5 && unknown_2a0 > 5)
    AddAdvice(kAdSeaportNotDevelopingAdviceId, 0);
  if (unknown_264 != 0 && unknown_2a8 != 0)
    AddAdvice(kAdNeedAccessToCityLandfillAdviceId, 0);
  else if (total_landfill_capacity != 0 && available_landfill_capacity == 0)
    AddAdvice(kAdLandfillFullAdviceId, 0);
  return true;
}

// Apply A8D7's cached planning petitioner triggers with the original random and reactivation ordering.
// A zero random choice deliberately queues both members of each paired petition.
void AdCityPlanningAdvisor::CheckOnPetitioners() {
  AdRandom random(kScRandomSeedFromClock);
  if ((HasAdvice(kAdCommerceNeedsAirportExpansionAdviceId) || HasAdvice(kAdCommerceNeedsConnectionsAdviceId)) &&
      unknown_24d < 40)
    AddPetitioner(kAdDonTExpandAirportPetitionerId, 0, 48);
  if (unknown_21c == 0 && unknown_24d > commercial_cap_problem)
    AddPetitioner(kAdCommercialNeedsAirportPetitionerId, 0, 48);
  if (static_cast<i32>(unknown_220) > 0 &&
      planning_ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_FarmersMarket) &&
      !ReactivatePetitioner(kAdFarmerSMarketProposedPetitionerId) && random.Next(100) < 2)
    AddPetitioner(kAdFarmerSMarketProposedPetitionerId, 0, 120);
  if (unknown_254 > non_development_sensitivity) {
    i64 demand = static_cast<i32>(commercial_positive);
    if (unknown_1c8 >= demand && unknown_1e0 >= demand && static_cast<i32>(unknown_1f8) > 500 &&
        !ReactivatePetitioner(41) && !ReactivatePetitioner(42)) {
      u32 choice = random.Next(2);
      if (choice == 0)
        AddPetitioner(41, 0, 12);
      if (choice <= 1)
        AddPetitioner(42, 0, 12);
    }
  }
  if (unknown_250 > non_development_sensitivity) {
    i64 demand = static_cast<i32>(residential_positive);
    if (unknown_1c0 >= demand && unknown_1d8 >= demand && !ReactivatePetitioner(kAdNeedHousingForSimsPetitionerId))
      AddPetitioner(kAdNeedHousingForSimsPetitionerId, 0, 12);
  }
  if (unknown_1fc > high_tech_eq &&
      static_cast<i32>(100u * unknown_224) > static_cast<i32>(unknown_214 * high_tech_job_ratio) &&
      !ReactivatePetitioner(kAdCityNeedsHighTechIndustriesPetitionerId) &&
      !ReactivatePetitioner(kAdHighTechJobsNeededPetitionerId) && random.Next(100) < 3) {
    u32 choice = random.Next(2);
    if (choice == 0)
      AddPetitioner(kAdCityNeedsHighTechIndustriesPetitionerId, 0, 30);
    if (choice <= 1)
      AddPetitioner(kAdHighTechJobsNeededPetitionerId, 0, 30);
  }
  if (unknown_238 - unknown_220 - unknown_234 > unknown_234 * static_cast<u32>(park_ratio) &&
      static_cast<i8>(flag_1fe) < static_cast<i8>(park_aura) && !ReactivatePetitioner(kAdMoreParksNeededPetitionerId) &&
      random.Next(100) < 2)
    AddPetitioner(kAdMoreParksNeededPetitionerId, 0, 120);
  if (unknown_238 - 2u * unknown_234 - unknown_220 > (unknown_23c >> 1) &&
      !ReactivatePetitioner(kAdGoesGreenPetitionerId) && random.Next(100) < 2)
    AddPetitioner(kAdGoesGreenPetitionerId, 0, 120);
}

// Compare the live reward's population and year thresholds after each original virtual data lookup.
// The two park rewards use the cached alternate population word instead of the ordinary city population.
static bool AdPlanningRewardThresholds(AdCityPlanningAdvisor *owner, u32 type) {
  u32 *minimum = AdFindPlanningReward(owner, type)->entry->Value64();
  u32 population = type == 14003 || type == 14550 ? owner->unknown_1bc : owner->unknown_1f8;
  if (population < *minimum)
    return false;
  minimum = AdFindPlanningReward(owner, type)->entry->Value68();
  return owner->unknown_204 >= *minimum;
}

// Unknown special types remain ineligible; ordinary records use only the original two configuration thresholds.
static bool AdPlanningRewardEligible(AdCityPlanningAdvisor *owner, u32 type, u32 month) {
  if (*AdFindPlanningReward(owner, type)->entry->Flag78Data() != 1)
    return AdPlanningRewardThresholds(owner, type);
  switch (type) {
  case 1002:
    return AdPlanningRewardThresholds(owner, type) && owner->unknown_1f0 > owner->stock_exchange_funds;
  case 11001:
    return AdPlanningRewardThresholds(owner, type) && *AdFindPlanningReward(owner, 11002)->entry->Flag77Data() == 1;
  case 12350:
    return AdPlanningRewardThresholds(owner, type) &&
        static_cast<i32>(owner->unknown_21c) > static_cast<i32>(owner->airport_for_spaceport);
  case 13871:
    return AdPlanningRewardThresholds(owner, type) && owner->unknown_1fc > owner->university_eq &&
        owner->unknown_1ac != 0;
  case 13872:
    return AdPlanningRewardThresholds(owner, type) && owner->unknown_1fd > owner->medical_center_le;
  case 13874:
    return AdPlanningRewardThresholds(owner, type) && owner->unknown_1fc > owner->science_center_eq;
  case 14003:
    return AdPlanningRewardThresholds(owner, type) &&
        static_cast<i32>(owner->unknown_230) > static_cast<i32>(owner->parks_for_national_park);
  case 14325:
    return AdPlanningRewardThresholds(owner, type) && static_cast<i32>(owner->unknown_21c) > 5 &&
        owner->unknown_1aa != 0;
  case 14550:
    return AdPlanningRewardThresholds(owner, type);
  case 20001: {
    if (!AdGetApplication())
      return false;
    CtCity *city = AdGetCity();
    if (!city)
      return false;
    i32 country = AdGetAppPreferences()->GetCountry();
    if (country == 30 || country == 31 || country == 32 || country == 34 || country == 49)
      return false;
    if (city->AreHolidayFeaturesEnabled() != 1)
      return false;
    u32 *minimum = AdFindPlanningReward(owner, type)->entry->Value64();
    return owner->unknown_1f8 >= *minimum && month >= 7 && month <= 9;
  }
  case 20002: {
    CtCity *city = AdGetCity();
    if (!city || city->AreHolidayFeaturesEnabled() != 1)
      return false;
    u32 *minimum = AdFindPlanningReward(owner, type)->entry->Value64();
    return owner->unknown_1f8 >= *minimum && month >= 9 && month <= 11 && static_cast<i32>(owner->unknown_230) >= 30;
  }
  default:
    return false;
  }
}

// Apply A324's live configured-reward eligibility pass under the cached reward lock.
// Successful special types become available directly; other eligible records queue dismissible petitions.
void AdCityPlanningAdvisor::CheckOnRewards() {
  ScLock *lock = reward_lock;
  lock->Lock();
  AdRandom random(kScRandomSeedFromClock);
  CtDate date(unknown_200, unknown_204);
  u32 month = date.Month();
  date.DayOfMonth();
  u32 index = 0;
  while (index < reward_types.size) {
    u32 type = reward_types.data[index].type;
    if (*AdFindPlanningReward(this, type)->entry->Flag77Data() != 1 &&
        *AdFindPlanningReward(this, type)->entry->Flag79Data() == 0) {
      if (AdPlanningRewardEligible(this, type, month)) {
        if (type == 1002 || type == 10110 || type == 10120 || type == 13005 || type == 13871 || type == 14150 ||
            type == 14170)
          *AdFindPlanningReward(this, type)->entry->AvailableData() = 1;
        if (*AdFindPlanningReward(this, type)->entry->Flag77Data() == 0)
          AddDismissiblePetitioner(*AdFindPlanningReward(this, type)->entry->Value72(), 0);
      } else if (*AdFindPlanningReward(this, type)->entry->AvailableData() == 0) {
        if (ContainsActivePetitioner(*AdFindPlanningReward(this, type)->entry->Value72()))
          RemovePetitioner(*AdFindPlanningReward(this, type)->entry->Value72());
      }
    }
    index = 0;
    while (index < reward_types.size && reward_types.data[index].type <= type)
      ++index;
  }
  lock->Unlock();
}

// Sample and process one pending planning tick, aging advice before evaluating rewards and petitions.
// Publish completion after forced advice, then clear the pending byte even if callbacks changed it.
bool AdCityPlanningAdvisor::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    GetSimulationData();
    CheckOnAdvice();
    advice->Advance();
    advice->ResetRedisplay();
    CheckOnRewards();
    CheckOnPetitioners();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdCityPlanningAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

// Apply B32A's reward placement effects while keeping the queried building retained through deal activation.
// A refused building query succeeds without touching either reward map; a null occupant returns zero under the lock.
u8 AdCityPlanningAdvisor::RewardPlaced(ScOccupant *occupant) {
  ScLock *lock = reward_lock;
  lock->Lock();
  if (!occupant) {
    lock->Unlock();
    return 0;
  }
  void *queried;
  if (!occupant->QueryInterface(IID_BdBuilding, &queried)) {
    lock->Unlock();
    return 1;
  }
  BdBuilding *building = static_cast<BdBuilding *>(queried);
  u32 type = building->AgentType()->Id();
  if (AdFindPlanningReward(this, type)) {
    *AdFindPlanningReward(this, type)->entry->Flag77Data() = 1;
    RemovePetitioner(*AdFindPlanningReward(this, type)->entry->Value72());
    if (budget_advisor->ActivateBusinessDeal(type, building))
      *AdFindPlanningReward(this, type)->entry->AvailableData() = 0;
  }
  building->Release();
  const RsKey *key = occupant->GetAttribData()->Key();
  AdKeyedReward *reward = AdFindKeyedPlanningReward(this, *key);
  if (reward)
    *reward->entry->Flag77Data() = 1;
  lock->Unlock();
  return 1;
}

// Apply B458's reward removal effects, releasing the queried building before inspecting configured records.
// Clear a matching deal and then the attribute-keyed reward while retaining the originally cached lock.
u8 AdCityPlanningAdvisor::RewardRemoved(ScOccupant *occupant) {
  ScLock *lock = reward_lock;
  lock->Lock();
  if (!occupant) {
    lock->Unlock();
    return 0;
  }
  void *queried;
  if (!occupant->QueryInterface(IID_BdBuilding, &queried)) {
    lock->Unlock();
    return 1;
  }
  BdBuilding *building = static_cast<BdBuilding *>(queried);
  u32 type = building->AgentType()->Id();
  building->Release();
  if (AdFindPlanningReward(this, type)) {
    *AdFindPlanningReward(this, type)->entry->Flag77Data() = 0;
    budget_advisor->RemoveBusinessDeal(type);
  }
  const RsKey *key = occupant->GetAttribData()->Key();
  AdKeyedReward *reward = AdFindKeyedPlanningReward(this, *key);
  if (reward)
    *reward->entry->Flag77Data() = 0;
  lock->Unlock();
  return 1;
}

// Apply936E's five ordinary planning ordinances and the configured scenario's city-specific ordinance.
// City type and ID are queried separately after the ordinary queue insertions.
void AdCityPlanningAdvisor::InitializeOrdinances() {
  for (u32 id = 1001; id <= 1005; ++id)
    AddOrdinance(id);
  if (city->GetCityType() == 5) {
    switch (city->GetCityID()) {
    case 1:
      AddOrdinance(10005);
      break;
    case 2:
      AddOrdinance(10009);
      break;
    case 3:
      AddOrdinance(10006);
      break;
    case 4:
      AddOrdinance(10003);
      break;
    case 5:
      AddOrdinance(10008);
      break;
    case 6:
      AddOrdinance(10004);
      break;
    case 7:
      AddOrdinance(10002);
      break;
    case 8:
      AddOrdinance(10007);
      break;
    case 9:
      AddOrdinance(10001);
      break;
    case 10:
      AddOrdinance(10010);
      break;
    }
  }
}

// Apply ABB2's accepted planning petition to its configured reward while holding the cached lock.
// An unrecognized accepted ID changes nothing; the original requires every recognized reward entry to exist.
void AdCityPlanningAdvisor::RespondToPetitioner(u32 id, i32 button) {
  ScLock *lock = reward_lock;
  lock->Lock();
  if (button != 1) {
    lock->Unlock();
    return;
  }
  u32 type;
  switch (id) {
  case 132:
    type = 13874;
    break;
  case 136:
    type = 14003;
    break;
  case 139:
    type = 14550;
    break;
  case 140:
    type = 14370;
    break;
  case 141:
    type = 14325;
    break;
  case 142:
    type = 13872;
    break;
  case 143:
    type = 11001;
    break;
  case 148:
    type = 14600;
    break;
  case 150:
    type = 12350;
    break;
  case 205:
    type = 11002;
    break;
  case 300:
    type = 20001;
    break;
  case 303:
    type = 20002;
    break;
  default:
    lock->Unlock();
    return;
  }
  *AdFindPlanningReward(this, type)->entry->AvailableData() = 1;
  lock->Unlock();
}

// Compare native planning map keys in B87C's unsigned group, instance and type order.
static bool AdPlanningKeyLess(const RsKey &left, const RsKey &right) {
  if (left.group != right.group)
    return left.group < right.group;
  if (left.instance != right.instance)
    return left.instance < right.instance;
  return left.type < right.type;
}

// Keep old records alive throughout enumeration, then clear them and replace the map before releasing the iterator.
void AdCityPlanningAdvisor::InitializeRewards() {
  CtAgentTree *tree = AdGetAgentTree();
  RsKey name_key = {};
  ScArray<AdKeyedReward> replacement;
  ScString text;
  OcCacheIterator *iterator = AdGetOccupantAttribCache()->CreateNodeIterator();
  for (GeAttrib *attribute = iterator->GetNext(); attribute; attribute = iterator->GetNext()) {
    if (!tree->IsAncestor(18000, attribute->TypeId()))
      continue;
    u8 available = 1, placed = 0;
    RsKey key = *attribute->Key();
    AdKeyedReward *old = AdFindKeyedPlanningReward(this, key);
    if (old) {
      AdRewardEntry *record = old->entry;
      available = *record->AvailableData();
      placed = *record->Flag77Data();
    }
    AdRewardEntry *record = new AdRewardEntry;
    record->AddRef();
    record->SetKey(attribute->Key());
    *record->AvailableData() = available;
    *record->Flag77Data() = placed;
    *record->Flag79Data() = 2;
    attribute->GetNameKey(&name_key);
    text = AdResolvePlanningText(name_key.instance, name_key.group);
    record->SetThirdText(&text);
    u32 cost = static_cast<u32>(budget->GetCost(attribute->TypeId()));
    if (cost != 0) {
      ScString money;
      text = AdResolvePlanningText(kRsGameCostText, kAdAdvisorLabelGroup);
      AdGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(static_cast<i64>(cost), &money);
      text.ScString::Append(money.begin, static_cast<u32>(money.end - money.begin));
      record->SetFirstText(&text);
    }
    u32 index = 0;
    while (index < replacement.size && AdPlanningKeyLess(replacement.data[index].key, key))
      ++index;
    if (index < replacement.size && !AdPlanningKeyLess(key, replacement.data[index].key))
      replacement.data[index].entry = record;
    else {
      replacement.Append();
      for (u32 cursor = replacement.size - 1; cursor > index; --cursor)
        replacement.data[cursor] = replacement.data[cursor - 1];
      replacement.data[index] = {key, record};
    }
  }
  ClearRewards();
  AdKeyedReward *old_data = rewards.data;
  u32 old_size = rewards.size, old_capacity = rewards.capacity;
  rewards.data = replacement.data;
  rewards.size = replacement.size;
  rewards.capacity = replacement.capacity;
  replacement.data = old_data;
  replacement.size = old_size;
  replacement.capacity = old_capacity;
  iterator->Release();
}

// Dispatch actual planning cheats, occupant placement/removal and ordinance help notifications.
// Rebuild cached rewards on attribute changes and preserve false for unrelated message types.
bool AdCityPlanningAdvisor::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_CheatCommand:
    switch (static_cast<u32>(message->data1)) {
    case kAdZyxwvuCheatId:
      if (flag_30e == 1)
        AddBusinessDealBuildingToRewardList(19999);
      break;
    case kAdPayTributeToYourKingCheatId:
      flag_30d = 1;
      break;
    case kAdStopForcingAdviceCheatId:
      ClearForcedAdvice();
      break;
    case kAdForceConstanceToSayCheatId: {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceConstanceToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceConstanceToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
      break;
    }
    }
    break;
  case kOcMsg_SurfaceRemoved:
    return RewardRemoved(reinterpret_cast<ScOccupant *>(message->data1)) != 0;
  case kOcMsg_SurfaceInserted:
    return RewardPlaced(reinterpret_cast<ScOccupant *>(message->data1)) != 0;
  case kCtMsg_OrdinanceAvailable:
    switch (static_cast<u32>(message->data1)) {
    case kCtOrdinance_IndustrialPollutionFee:
      AddHelp(32);
      break;
    case kCtOrdinance_ElectronicsTaxIncentive:
      AddHelp(6);
      break;
    case kCtOrdinance_HomelessShelters:
      AddHelp(2);
      break;
    case kCtOrdinance_AerospaceTaxIncentive:
      AddHelp(7);
      break;
    case kCtOrdinance_TouristPromotion:
      AddHelp(3);
      break;
    case kCtOrdinance_FarmersMarket:
      AddHelp(29);
      break;
    case kCtOrdinance_ConservationCorps:
      AddHelp(4);
      break;
    case kCtOrdinance_ElectronicsJobFair:
      AddHelp(8);
      break;
    case kCtOrdinance_EarthquakeRetrofitting:
      AddHelp(1);
      break;
    case kCtOrdinance_PublicAccessCable:
      AddHelp(10);
      break;
    case kCtOrdinance_CleanIndustryAssociation:
      AddHelp(5);
      break;
    case kCtOrdinance_BiotechTaxIncentives:
      AddHelp(9);
      break;
    }
    break;
  case kOcMsg_AttributesChanged:
    InitializeRewards();
    break;
  default:
    return false;
  }
  return true;
}

// Retain the actual city services, load and scale planning configuration, then construct reward records and phase callbacks.
// Preserve missing-layer refusals, ignored query/configuration results and each separate cheat-manager acquisition.
bool AdCityPlanningAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  city = input;
  input->AddRef();
  occupants = city->SurfaceOccupantManager();
  if (!occupants) {
    // The original early Shutdown reads an uninitialized network field; exclude that undefined read at this boundary.
    networks = nullptr;
    Shutdown();
    return false;
  }
  occupants->AddRef();
  CtLayer *layer;
  layer = city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = city->GetSpecificLayer(kCtNeighborsLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors));
  layer = city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = city->GetSpecificLayer(kEcCommercialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcCommercialLayer, reinterpret_cast<void **>(&commercial));
  layer = city->GetSpecificLayer(kEcIndustrialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcIndustrialLayer, reinterpret_cast<void **>(&industrial));
  layer = city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves));
  layer = city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  layer = city->GetSpecificLayer(kCtAuraLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura));
  layer = city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&planning_ordinances));
  layer = city->GetSpecificLayer(kEcZoneLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtZoneLayer, reinterpret_cast<void **>(&zones));
  layer = city->GetSpecificLayer(kEcLandValueLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtLandValueLayer, reinterpret_cast<void **>(&land_value));
  layer = city->GetSpecificLayer(kEcPollutionLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution));
  layer = city->GetSpecificLayer(kNwManagerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_NwManager, reinterpret_cast<void **>(&networks));
  transportation = city->GetTransportationAdvisor();
  valves->GetValvePointer(3000, IID_EcValve, reinterpret_cast<void **>(&residential_valve));
  valves->GetValvePointer(5000, IID_EcValve, reinterpret_cast<void **>(&commercial_valve));
  valves->GetValvePointer(8000, IID_EcValve, reinterpret_cast<void **>(&industrial_valve));
  if (ordinances)
    InitializeOrdinances();
  LoadConfiguration();
  if (residential_valve) {
    i32 limit = residential_valve->QueryValveMin();
    residential_negative = static_cast<u32>(static_cast<i32>(residential_negative * static_cast<u32>(limit)) / 100);
    limit = residential_valve->QueryValveMax();
    residential_positive = static_cast<u32>(static_cast<i32>(residential_positive * static_cast<u32>(limit)) / 100);
  }
  if (commercial_valve) {
    i32 limit = commercial_valve->QueryValveMin();
    commercial_negative = static_cast<u32>(static_cast<i32>(commercial_negative * static_cast<u32>(limit)) / 100);
    limit = commercial_valve->QueryValveMax();
    commercial_positive = static_cast<u32>(static_cast<i32>(commercial_positive * static_cast<u32>(limit)) / 100);
  }
  if (industrial_valve) {
    i32 limit = industrial_valve->QueryValveMin();
    industrial_negative = static_cast<u32>(static_cast<i32>(industrial_negative * static_cast<u32>(limit)) / 100);
    limit = industrial_valve->QueryValveMax();
    industrial_positive = static_cast<u32>(static_cast<i32>(industrial_positive * static_cast<u32>(limit)) / 100);
  }
  InitializeRewards();
  callback3 = new AdPlanningCallback3(this);
  callback4 = new AdPlanningCallback4(this);
  CtAgentType *node = AdGetAgentTree()->GetNode(kCtAgentType_CityPlanningAdvisorAgent);
  SetAgentType(node);
  node->AttachManipulator(callback3, 3);
  node->AttachManipulator(callback4, 4);
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  ScMessageServer *server = AdGetMessageServer();
  if (server) {
    server->AddNotification(target, kCtMsg_OrdinanceAvailable);
    server->AddNotification(target, kOcMsg_AttributesChanged);
  }
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceConstanceToSayCheatId, AdForceConstanceToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdForceConstanceToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdPayTributeToYourKingCheatId, AdPayTributeToYourKing);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdPayTributeToYourKingCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdZyxwvuCheatId, AdZyxwvu);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdZyxwvuCheatId);
  return AdBaseAdvisor::Init(input);
}

// Sum9DBE's residential failure counts in descending density order using the captured optional zone layer.
// Each count retains its full DWORD, with the original wrapped aggregate arithmetic.
u32 AdCityPlanningAdvisor::ResidentialFailures(i32 reason) {
  CtZoneLayer *layer = zones;
  if (!layer)
    return 0;
  u32 result = layer->GetDevelopmentFailureCount(3, reason);
  result += layer->GetDevelopmentFailureCount(2, reason);
  return result + layer->GetDevelopmentFailureCount(1, reason);
}

// Sum9E0C's commercial failure counts in descending density order through the same captured zone receiver.
u32 AdCityPlanningAdvisor::CommercialFailures(i32 reason) {
  CtZoneLayer *layer = zones;
  if (!layer)
    return 0;
  u32 result = layer->GetDevelopmentFailureCount(7, reason);
  result += layer->GetDevelopmentFailureCount(6, reason);
  return result + layer->GetDevelopmentFailureCount(5, reason);
}

// Sum9E5A's industrial failure counts in descending density order through the captured optional zone layer.
u32 AdCityPlanningAdvisor::IndustrialFailures(i32 reason) {
  CtZoneLayer *layer = zones;
  if (!layer)
    return 0;
  u32 result = layer->GetDevelopmentFailureCount(11, reason);
  result += layer->GetDevelopmentFailureCount(10, reason);
  return result + layer->GetDevelopmentFailureCount(9, reason);
}

// Refresh planning's actual service samples, zoning failure aggregates and signed demand histories.
// Keep original optional-service guards and query order, then shift the three twelve-month buffers after averaging.
void AdCityPlanningAdvisor::GetSimulationData() {
  city->GetDate(&unknown_200, &unknown_204);
  unknown_208 = city->GetYearBorn();
  if (residential) {
    unknown_1f8 = static_cast<u32>(residential->GetPopulation());
    unknown_1fc = residential->GetWorkforceEQ();
    unknown_1fd = residential->GetWorkforceLE();
    unknown_24c = residential->GetDevelopmentCap();
  }
  if (budget)
    unknown_1f0 = budget->GetTotalFunds();
  if (commercial)
    unknown_24d = commercial->GetDevelopmentCap();
  if (industrial)
    unknown_24e = industrial->GetDevelopmentCap();
  if (transportation) {
    unknown_1a8 = transportation->InventionAvailable(3);
    unknown_1a9 = transportation->InventionAvailable(4);
  }
  if (aura)
    flag_1fe = static_cast<u8>(aura->GetMayorRating());
  if (buildings) {
    unknown_20c = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentResidential));
    unknown_210 = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentCommercial));
    unknown_214 = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentIndustrial));
    unknown_218 = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentSeaport));
    unknown_21c = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentAirport));
    unknown_220 = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentAgriculturalIndustry));
    GeBuildingLayer *layer = buildings;
    u32 count = static_cast<u32>(layer->CheckOccupantTracking(kCtAgentCleanHighDensityIndustry));
    count += static_cast<u32>(layer->CheckOccupantTracking(8900));
    unknown_224 = count + static_cast<u32>(layer->CheckOccupantTracking(8700));
    layer = buildings;
    count = static_cast<u32>(layer->CheckOccupantTracking(6100));
    unknown_228 = count + static_cast<u32>(layer->CheckOccupantTracking(5700));
    layer = buildings;
    count = static_cast<u32>(layer->CheckOccupantTracking(6700));
    unknown_22c = count + static_cast<u32>(layer->CheckOccupantTracking(6300));
    unknown_230 = static_cast<u32>(buildings->CheckOccupantTracking(14310));
    unknown_234 = 0;
    i32 parks = buildings->CheckOccupantTracking(14311);
    if (parks > 0)
      unknown_234 += static_cast<u32>(parks);
    parks = buildings->CheckOccupantTracking(14312);
    if (parks > 0)
      unknown_234 += 9u * static_cast<u32>(parks);
    parks = buildings->CheckOccupantTracking(14313);
    if (parks > 0)
      unknown_234 += 16u * static_cast<u32>(parks);
    parks = buildings->CheckOccupantTracking(14314);
    if (parks > 0)
      unknown_234 += 4u * static_cast<u32>(parks);
    parks = buildings->CheckOccupantTracking(14315);
    if (parks > 0)
      unknown_234 += 4u * static_cast<u32>(parks);
    parks = buildings->CheckOccupantTracking(14316);
    if (parks > 0)
      unknown_234 += static_cast<u32>(parks);
    unknown_238 = buildings->GetTotalAreaOfBuildings();
  }
  if (networks) {
    NwManager *layer = networks;
    u32 count = layer->GetPowerCount();
    count += layer->GetRailCount();
    count += layer->GetHighwayCount();
    unknown_238 += count + layer->GetRoadCount();
  }
  if (city) {
    CtCity *input = city;
    u32 height = input->CellCountZ();
    unknown_23c = input->CellCountX() * height;
  }
  if (planning_ordinances) {
    unknown_1aa = planning_ordinances->QueryOrdinanceOn(kCtOrdinance_TouristPromotion);
    unknown_1ab = planning_ordinances->QueryOrdinanceOn(kCtOrdinance_NuclearFreeZone);
    unknown_1ac = planning_ordinances->QueryOrdinanceOn(kCtOrdinance_ProReading);
  }
  if (neighbors) {
    unknown_240 = neighbors->GetRoadConnections();
    unknown_244 = neighbors->GetRailConnections();
    unknown_248 = neighbors->CountNeighbors();
  }
  if (zones) {
    CtZoneLayer *layer = zones;
    u32 count = layer->GetUndevelopedTileCount(3);
    count += layer->GetUndevelopedTileCount(2);
    unknown_250 = count + layer->GetUndevelopedTileCount(1);
    layer = zones;
    count = layer->GetUndevelopedTileCount(7);
    count += layer->GetUndevelopedTileCount(6);
    unknown_254 = count + layer->GetUndevelopedTileCount(5);
    layer = zones;
    count = layer->GetUndevelopedTileCount(11);
    count += layer->GetUndevelopedTileCount(10);
    unknown_258 = count + layer->GetUndevelopedTileCount(9);
    unknown_25c = zones->GetUndevelopedTileCount(14);
    unknown_260 = zones->GetUndevelopedTileCount(15);
    unknown_264 = zones->GetUndevelopedTileCount(17);
    unknown_268 = ResidentialFailures(4);
    unknown_26c = CommercialFailures(4);
    unknown_270 = IndustrialFailures(4);
    unknown_274 = ResidentialFailures(5);
    unknown_278 = CommercialFailures(5);
    unknown_27c = IndustrialFailures(5);
    unknown_280 = ResidentialFailures(6);
    unknown_284 = CommercialFailures(6);
    unknown_288 = IndustrialFailures(6);
    unknown_28c = ResidentialFailures(3);
    unknown_290 = CommercialFailures(3);
    unknown_294 = IndustrialFailures(3);
    count = IndustrialFailures(1);
    count += CommercialFailures(1);
    unknown_298 = count + ResidentialFailures(1);
    count = IndustrialFailures(12);
    unknown_29c = count + ResidentialFailures(12);
    layer = zones;
    count = layer->GetDevelopmentFailureCount(15, 17);
    count += layer->GetDevelopmentFailureCount(15, 16);
    count += layer->GetDevelopmentFailureCount(15, 15);
    count += layer->GetDevelopmentFailureCount(15, 14);
    count += layer->GetDevelopmentFailureCount(15, 12);
    unknown_2a0 = count + layer->GetDevelopmentFailureCount(15, 9);
    layer = zones;
    count = layer->GetDevelopmentFailureCount(14, 16);
    count += layer->GetDevelopmentFailureCount(14, 15);
    count += layer->GetDevelopmentFailureCount(14, 14);
    unknown_2a4 = count + layer->GetDevelopmentFailureCount(14, 9);
    unknown_2a8 = zones->GetDevelopmentFailureCount(17, 16);
    count = ResidentialFailures(16);
    count += ResidentialFailures(15);
    unknown_2ac = count + ResidentialFailures(14);
    count = CommercialFailures(16);
    count += CommercialFailures(15);
    unknown_2b0 = count + CommercialFailures(14);
    count = IndustrialFailures(16);
    count += IndustrialFailures(15);
    unknown_2b4 = count + IndustrialFailures(14);
  }
  if (pollution) {
    unknown_1ad = static_cast<u8>(pollution->GetAverageAirValue());
    unknown_1ae = static_cast<u8>(pollution->GetAverageWaterValue());
    total_landfill_capacity = static_cast<u32>(pollution->GetTotalLandfillCapacity());
    available_landfill_capacity = static_cast<u32>(pollution->GetAvailableLandfillCapacity());
  }
  if (land_value)
    unknown_1b8 = land_value->GetAverageValue();
  if (residential_valve)
    unknown_1c0 = residential_valve->QueryValveValue();
  if (commercial_valve)
    unknown_1c8 = commercial_valve->QueryValveValue();
  if (industrial_valve) {
    unknown_1d0 = industrial_valve->QueryValveValue();
    unknown_1bc = static_cast<u32>(industrial_valve->QuerySupplyValue());
  }
  unknown_1d8 = unknown_1c0;
  unknown_1e0 = unknown_1c8;
  unknown_1e8 = unknown_1d0;
  u32 first = 12u - responsiveness_months;
  if (first <= 11)
    for (u32 index = first; index < 12; ++index) {
      unknown_1d8 = static_cast<i64>(static_cast<u64>(unknown_1d8) + static_cast<i32>(sample_118[index]));
      unknown_1e0 = static_cast<i64>(static_cast<u64>(unknown_1e0) + static_cast<i32>(sample_148[index]));
      unknown_1e8 = static_cast<i64>(static_cast<u64>(unknown_1e8) + static_cast<i32>(sample_178[index]));
    }
  unknown_1d8 /= 12;
  unknown_1e0 /= 12;
  unknown_1e8 /= 12;
  for (u32 index = 0; index < 11; ++index) {
    sample_118[index] = sample_118[index + 1];
    sample_148[index] = sample_148[index + 1];
    sample_178[index] = sample_178[index + 1];
  }
  sample_118[11] = static_cast<u32>(unknown_1c0);
  sample_148[11] = static_cast<u32>(unknown_1c8);
  sample_178[11] = static_cast<u32>(unknown_1d0);
}

// Construct the zero-reference planning advisor and return its actual secondary advisor identity at complete+4.
// The director owns the following query/cleanup protocol; this factory takes no additional reference.
ScUnknown *AdCreateCityPlanning() {
  return static_cast<AdAdvisor *>(new AdCityPlanningAdvisor);
}

#if defined(_M_IX86)
static_assert(sizeof(AdPublicSafetyAdvisor) == 364);
static_assert(sizeof(AdPoliceBudget) == 56);
static_assert(sizeof(AdFireBudget) == 56);
static_assert(sizeof(AdSafetyCallback3) == 16);
static_assert(sizeof(AdSafetyCallback4) == 16);
#endif

// Construct the actual safety advisor with its defined service zeros, resource names and locale-specific portraits.
// Keep sampled words undefined until the real sampler and retain the original funding, crime and population defaults.
AdPublicSafetyAdvisor::AdPublicSafetyAdvisor()
    : signal_b8(0.0f), signal_bc(0.0f), simulation_city(nullptr), fire_layer(nullptr), police_layer(nullptr),
      crime_layer(nullptr), budget(nullptr), valves(nullptr), buildings(nullptr), residential(nullptr),
      ordinances(nullptr), occupants(nullptr), police_budget(nullptr), fire_budget(nullptr) {
  ScString name = AdTextHandle(kRsAdvisorNamePublicSafetyAdvisorText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetName(&name);
  name = AdTextHandle(kRsGamePublicSafetyBriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGamePublicSafetyOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameMariaMontoyaText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  u32 portrait = language == kScLanguageEnglish || language == kScLanguageBritishEnglish                 ? 0
      : language == kScLanguageJapanese || (language > kScLanguagePolish && language <= kScLanguageThai) ? 2
                                                                                                         : 1;
  const RsKey small = {kRsImageResourceType, kRsAdvisorPortraitGroup, kAdSafetySmallPortraitBase + (portrait << 16)};
  const RsKey large = {kRsImageResourceType, kRsAdvisorPortraitGroup, kAdSafetyLargePortraitBase + (portrait << 16)};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  police_funding_high = 105;
  police_funding_medium = 90;
  fire_funding_high = 105;
  fire_funding_medium = 90;
  fire_station_minimum = 500;
  jail_minimum = 25000;
  fire_coverage_cutoff = 40;
  crime_high = 20;
  crime_medium = 10;
  police_station_ratio = 5000;
  event_state = 0;
  selected_disaster = nullptr;
  outdated_response = 303;
}

AdPublicSafetyAdvisor::~AdPublicSafetyAdvisor() {
}

bool AdPublicSafetyAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdPublicSafety, AdPublicSafety);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdPublicSafetyAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdPublicSafetyAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdPublicSafetyAdvisor::GetAdvisorID() {
  return kAdPublicSafetyAdvisorId;
}

u32 AdPublicSafetyAdvisor::GetLayerType() {
  return kAdPublicSafetyAdvisorLayerType;
}

bool AdPublicSafetyAdvisor::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool AdPublicSafetyAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Init(input, segment);
}

bool AdPublicSafetyAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Save(input, segment);
}

static bool AdReadSafetySetting(AdIni *reader, const char *section_name, const char *key_name, ScString *out) {
  ScString key(key_name), section(section_name);
  return reader->ReadEntry(section, key, out);
}

// Read the nine main safety settings in source order, preserving successful updates and defaults for missing keys.
bool AdPublicSafetyAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-PublicSafety.INI", 29);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadSafetySetting(&reader, "FUNDING_RATE_CUTOFFS", "POLICE_FUNDING_HIGH", &value))
    police_funding_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "FUNDING_RATE_CUTOFFS", "POLICE_FUNDING_MEDIUM", &value))
    police_funding_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "FUNDING_RATE_CUTOFFS", "FIRE_FUNDING_HIGH", &value))
    fire_funding_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "FUNDING_RATE_CUTOFFS", "FIRE_FUNDING_MEDIUM", &value))
    fire_funding_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "ADVICE_GENERATION", "FIRE_COVERAGE_CUTOFF", &value))
    fire_coverage_cutoff = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "ADVICE_GENERATION", "FIRE_STATION_MIN_POP", &value))
    fire_station_minimum = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "ADVICE_GENERATION", "JAIL_MIN_POP", &value))
    jail_minimum = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "ADVICE_GENERATION", "HIGH_CRIME_LEVEL", &value))
    crime_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadSafetySetting(&reader, "ADVICE_GENERATION", "MED_CRIME_LEVEL", &value))
    crime_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (missing || !AdReadSafetySetting(&reader, "PETITIONER_GENERATION", "POLICE_STATION_RATIO", &value))
    return false;
  police_station_ratio = AdParseIniNumber(value.begin);
  return true;
}

AdSafetyCallback3::AdSafetyCallback3(AdPublicSafetyAdvisor *input) : owner(input) {
}

AdSafetyCallback3::~AdSafetyCallback3() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdSafetyCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdSafetyCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdSafetyCallback3::Release() {
  return ScRefCount::Release();
}

bool AdSafetyCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdSafetyCallback3::IsLockingCity() {
  return false;
}

bool AdSafetyCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdSafetyCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdSafetyCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdSafetyCallback4::AdSafetyCallback4(AdPublicSafetyAdvisor *input) : owner(input) {
}

AdSafetyCallback4::~AdSafetyCallback4() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdSafetyCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdSafetyCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdSafetyCallback4::Release() {
  return ScRefCount::Release();
}

bool AdSafetyCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdSafetyCallback4::IsLockingCity() {
  return false;
}

bool AdSafetyCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdSafetyCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdSafetyCallback4::DebugTypeTag(ScStringView *) {
  return false;
}
