#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "advisors/ad_advice.h"
#include "advisors/ad_advisors.h"
#include "advisors/ad_domains.h"
#include "advisors/ad_ini.h"
#include "advisors/ad_module.h"
#include "advisors/ad_petitions.h"
#include "advisors/ad_text.h"
#include "advisors/api.h"
#include "application/api.h"
#include "city/api.h"
#include "city/ct_scenario.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/api.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL3/SDL_timer.h>

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdUtilitiesPortraitId : u32 {
  kAdUtilitiesSmallPortraitBase = 0x1000103,
  kAdUtilitiesLargePortraitBase = 0x103,
};

// Serialized record identities remain distinct from interface and layer lookup IDs with equal bits.
static constexpr u32 kAdUtilitiesRecordType = kAdUtilitiesAdvisorId;
static constexpr u32 kAdUtilitiesRecordGroup = 0x422e28e8;

struct ScTimerSample {
  u64 first, second;
};

// Read the platform counter with the original zero-on-query-failure output and returned destination.
static u64 *ScReadCounter(u64 *out) {
  *out = SDL_GetPerformanceCounter();
  return out;
}

struct ScClockRatios {
  f64 values[5] = {};

  // Establish the five actual counter-unit ratios once, leaving their zero state when frequency acquisition fails.
  ScClockRatios() {
    u64 frequency = SDL_GetPerformanceFrequency();
    if (frequency) {
      f64 ticks = static_cast<f64>(frequency);
      values[0] = 1000000000.0 / ticks;
      values[1] = 1000000.0 / ticks;
      values[2] = 1000.0 / ticks;
      values[3] = 1.0 / ticks;
      values[4] = 0.01666666666666667 / ticks;
    }
  }
};

static ScClockRatios ScTimerRatios;

#pragma pack(push, 4)

// Native implementation of the actual module clock used for utilities random seeds.
// Trivial sample storage uses the project array rather than reconstructing private vector/allocator machinery.
class ScElapsedTimer {
public:
  // Preserve the original unit/count arguments, zero timestamps and requested zeroed sample range.
  ScElapsedTimer(i32 input_unit, u32 count)
      : unit(input_unit), started(0), elapsed(0), sample_index(0), sample_flags(0), sample_count(count) {
    samples.Reserve(count);
    samples.size = count;
    for (u32 index = 0; index < count; ++index)
      samples.data[index] = {};
  }

  // Retire only the actual sample owner; the clock does not own an advisor or platform service.
  virtual ~ScElapsedTimer() {
  }

  // Reset both timestamps and sample counters while retaining the original allocated sample range.
  void Reset() {
    started = elapsed = 0;
    sample_flags = sample_index = 0;
    for (u32 index = 0; index < sample_count; ++index)
      samples.data[index] = {};
  }

  // Start only an inactive clock, clearing accumulated ticks before the original platform counter read.
  void Start() {
    if (!started) {
      elapsed = 0;
      u64 current;
      ScReadCounter(&current);
      started = current;
    }
  }

  // Accumulate the current delta without clearing the start timestamp, as the original destructor-side call does.
  void Accumulate() {
    if (started) {
      u64 current;
      ScReadCounter(&current);
      elapsed += current - started;
    }
  }

  // Return the low word of the signed, truncated elapsed-unit value; an inactive clock reads no platform counter.
  u32 ElapsedLow() {
    if (!started)
      return 0;
    u64 current;
    ScReadCounter(&current);
    i64 delta = static_cast<i64>(current - started);
    u32 index = unit >= 0 && unit < 4 ? static_cast<u32>(unit) : 4;
    volatile f64 scaled = static_cast<f64>(delta) * ScTimerRatios.values[index];
    return static_cast<u32>(static_cast<i64>(scaled));
  }

  u32 unknown_04;
  i32 unit;
  u32 unknown_0c;
  u64 started, elapsed;
  ScArray<ScTimerSample> samples;
  u32 sample_index, sample_flags, sample_count;
};

#pragma pack(pop)

static ScElapsedTimer AdUtilitiesClock(2, 0);

#if defined(_M_IX86)
static_assert(sizeof(ScElapsedTimer) == 56);
static_assert(sizeof(AdUtilitiesAdvisor) == 496);
#endif

// Construct two original circular entry lists and initialize only the source-defined services and utility tuning.
// Resolve resource names/locale portraits before resetting and starting the shared utilities seed clock.
AdUtilitiesAdvisor::AdUtilitiesAdvisor() {
  AdUtilityNode *power_head = new AdUtilityNode;
  power_head->next = power_head;
  power_head->previous = power_head;
  power_entries = power_head;
  AdUtilityNode *water_head = new AdUtilityNode;
  water_head->next = water_head;
  water_head->previous = water_head;
  water_entries = water_head;
  service_ec = nullptr;
  service_f0 = nullptr;
  pollution = nullptr;
  budget = nullptr;
  valves = nullptr;
  neighbors = nullptr;
  budget_advisor = nullptr;
  residential = nullptr;
  callback3 = nullptr;
  callback4 = nullptr;
  ScString name = AdTextHandle(kRsAdvisorNameUtilitiesAdvisorText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetName(&name);
  name = AdTextHandle(kRsGameUtilitiesBriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGameUtilitiesOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameGusOddmanText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  u32 portrait = language == kScLanguageEnglish || language == kScLanguageBritishEnglish                 ? 0
      : language == kScLanguageJapanese || (language > kScLanguagePolish && language <= kScLanguageThai) ? 2
                                                                                                         : 1;
  const RsKey small = {kRsImageResourceType, kRsAdvisorPortraitGroup, kAdUtilitiesSmallPortraitBase + (portrait << 16)};
  const RsKey large = {kRsImageResourceType, kRsAdvisorPortraitGroup, kAdUtilitiesLargePortraitBase + (portrait << 16)};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  year = 0;
  power_high = static_cast<f64>(0.9f);
  power_usage_critical = 99;
  power_low = static_cast<f64>(0.6f);
  power_usage_high = 95;
  power_usage_medium = 40;
  water_high = static_cast<f64>(0.9f);
  water_usage_critical = 99;
  water_usage_high = 95;
  water_usage_medium = 40;
  water_low = static_cast<f64>(0.6f);
  low_treasury_level = 10000;
  water_conservation_year = 1929;
  minimum_deal_population = 5000;
  power_funding_percent = 100.0f;
  for (u32 index = 0; index < 6; ++index)
    new_deals[index] = 1;
  outdated_response = 300;
  AdUtilitiesClock.Reset();
  AdUtilitiesClock.Start();
}

// Accumulate the shared clock before deleting water-list storage, then power-list storage and shared advisor state.
// Record payload release belongs to the separate utilities shutdown, so this destructor retires only list nodes.
AdUtilitiesAdvisor::~AdUtilitiesAdvisor() {
  AdUtilitiesClock.Accumulate();
  AdUtilityLink *heads[] = {water_entries, power_entries};
  for (AdUtilityLink *head : heads) {
    AdUtilityLink *node = head->next;
    while (node != head) {
      AdUtilityLink *next = node->next;
      delete static_cast<AdUtilityNode *>(node);
      node = next;
    }
    delete static_cast<AdUtilityNode *>(head);
  }
}

bool AdUtilitiesAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdUtilities, AdUtilities);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdUtilitiesAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdUtilitiesAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdUtilitiesAdvisor::GetAdvisorID() {
  return kAdUtilitiesAdvisorId;
}

u32 AdUtilitiesAdvisor::GetLayerType() {
  return kAdUtilitiesAdvisorLayerType;
}

// Borrow the original secondary advisor identity without acquiring another reference.
AdAdvisor *AdUtilitiesAdvisor::AsISC3BaseAdvisor() {
  return static_cast<AdAdvisor *>(this);
}

// Borrow the genuine city-agent subobject from the complete utilities object.
CtAgent *AdUtilitiesAdvisor::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool AdUtilitiesAdvisor::CheckPowerBuildingAvailibility(u32 type) {
  for (AdUtilityLink *node = power_entries->next; node != power_entries; node = node->next)
    if (*static_cast<AdUtilityNode *>(node)->entry->TypeData() == type)
      return *static_cast<AdUtilityNode *>(node)->entry->AvailableData() != 0;
  return false;
}

bool AdUtilitiesAdvisor::CheckWaterBuildingAvailibility(u32 type) {
  for (AdUtilityLink *node = water_entries->next; node != water_entries; node = node->next)
    if (*static_cast<AdUtilityNode *>(node)->entry->TypeData() == type)
      return *static_cast<AdUtilityNode *>(node)->entry->AvailableData() != 0;
  return false;
}

f32 AdUtilitiesAdvisor::GetPowerFundingPercentage() {
  return power_funding_percent;
}

// Update one of the six original deal flags; every negative selector updates all flags and selectors6+ fail unchanged.
bool AdUtilitiesAdvisor::DisableNewDealsOfType(i32 type) {
  if (type < 0) {
    for (u32 index = 0; index < 6; ++index)
      new_deals[index] = 0;
  } else {
    if (type >= 6)
      return false;
    new_deals[type] = 0;
  }
  return true;
}

// Update one of the six original deal flags; every negative selector updates all flags and selectors6+ fail unchanged.
bool AdUtilitiesAdvisor::EnableNewDealsOfType(i32 type) {
  if (type < 0) {
    for (u32 index = 0; index < 6; ++index)
      new_deals[index] = 1;
  } else {
    if (type >= 6)
      return false;
    new_deals[type] = 1;
  }
  return true;
}

// Load shared advice first, then read the six deal flags only for saves newer than2.0.609.
// Retain completed flag reads and the original shared result even when the extra frame or byte transport fails.
bool AdUtilitiesAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  bool success = AdBaseAdvisor::Init(input, segment);
  if (success && input->IsFileVersionMoreRecentThan(2, 0, 609)) {
    ScArchiveFrame frame(input, segment, RsKey{kAdUtilitiesRecordType, kAdUtilitiesRecordGroup, 0});
    RsStream *stream = frame.valid ? frame.GetStream() : nullptr;
    u8 valid = stream != nullptr;
    for (u32 index = 0; valid && index < 6; ++index) {
      u8 value;
      valid = stream->ReadU8(&value);
      if (valid)
        new_deals[index] = value != 0;
    }
  }
  return success;
}

// Write shared advice before opening the separate version-one utilities deal-flag record.
// Stop on refused byte writes and close the frame normally, preserving the shared writer's Boolean result.
bool AdUtilitiesAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  bool success = AdBaseAdvisor::Save(input, segment);
  if (success) {
    ScArchiveFrame frame(input, segment, RsKey{kAdUtilitiesRecordType, kAdUtilitiesRecordGroup, 0}, 1, 0);
    RsStream *stream = frame.valid ? frame.GetStream() : nullptr;
    u8 valid = stream != nullptr;
    for (u32 index = 0; valid && index < 6; ++index)
      valid = stream->WriteU8(new_deals[index] != 0);
  }
  return success;
}

// Resolve the actual agent identity before acquiring its simulator and removing it.
// The original ignores simulator refusal and returns the shared successful simulation-end result.
bool AdUtilitiesAdvisor::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->RemoveAgent(agent);
  return AdBaseAdvisor::SimulationEnd();
}

#if defined(_M_IX86)
static_assert(sizeof(AdUtilitiesCallback3) == 16);
static_assert(sizeof(AdUtilitiesCallback4) == 16);
#endif

AdUtilitiesCallback3::AdUtilitiesCallback3(AdUtilitiesAdvisor *input) : owner(input) {
}

AdUtilitiesCallback3::~AdUtilitiesCallback3() {
}

bool AdUtilitiesCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdUtilitiesCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdUtilitiesCallback3::Release() {
  return ScRefCount::Release();
}

bool AdUtilitiesCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdUtilitiesCallback3::IsLockingCity() {
  return false;
}

bool AdUtilitiesCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdUtilitiesCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdUtilitiesCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdUtilitiesCallback4::AdUtilitiesCallback4(AdUtilitiesAdvisor *input) : owner(input) {
}

AdUtilitiesCallback4::~AdUtilitiesCallback4() {
}

bool AdUtilitiesCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdUtilitiesCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdUtilitiesCallback4::Release() {
  return ScRefCount::Release();
}

bool AdUtilitiesCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdUtilitiesCallback4::IsLockingCity() {
  return false;
}

bool AdUtilitiesCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdUtilitiesCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdUtilitiesCallback4::DebugTypeTag(ScStringView *) {
  return false;
}

// Remove message and cheat subscriptions before retiring retained services in their original callback order.
// Release every utility payload before erasing its list storage, then run the shared advisor shutdown.
bool AdUtilitiesAdvisor::Shutdown() {
  CtLayer *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->RemoveNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetMessageServer()->RemoveNotification(identity, kBdMsg_PowerPlantDestroyed);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdPowerToTheMassesCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdPowerToTheMassesCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdWaterInTheDesertCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdWaterInTheDesertCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForceGusToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdForceGusToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdStopForcingAdviceCheatId);
  ScRelease(&simulation_city);
  ScRelease(&service_ec);
  ScRelease(&budget);
  ScRelease(&valves);
  ScRelease(&neighbors);
  ScRelease(&pollution);
  ScRelease(&budget_advisor);
  ScRelease(&ordinances);
  ScRelease(&buildings);
  ScRelease(&service_f0);
  ScRelease(&residential);
  for (AdUtilityLink *node = power_entries->next; node != power_entries; node = node->next)
    static_cast<AdUtilityNode *>(node)->entry->Release();
  while (power_entries->next != power_entries) {
    AdUtilityLink *node = power_entries->next;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdUtilityNode *>(node);
  }
  for (AdUtilityLink *node = water_entries->next; node != water_entries; node = node->next)
    static_cast<AdUtilityNode *>(node)->entry->Release();
  while (water_entries->next != water_entries) {
    AdUtilityLink *node = water_entries->next;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdUtilityNode *>(node);
  }
  return AdBaseAdvisor::Shutdown();
}

// Route ordinance help, utility cheat activation and forced Gus advice through their actual shared dependencies.
bool AdUtilitiesAdvisor::DoMessage(ScMessage *message) {
  if (message->type == kCtMsg_OrdinanceAvailable) {
    switch (static_cast<u32>(message->data1)) {
    case kCtOrdinance_WaterConservation:
      AddHelp(71);
      break;
    case kCtOrdinance_PowerConservation:
      AddHelp(72);
      break;
    case kCtOrdinance_StairwellLighting:
      AddHelp(70);
      break;
    case kCtOrdinance_MandatoryWaterMeters:
      AddHelp(73);
      break;
    }
    return true;
  }
  if (message->type == kWnMsg_CheatCommand) {
    u32 command = static_cast<u32>(message->data1);
    if (command == kAdStopForcingAdviceCheatId)
      ClearForcedAdvice();
    else if (command == kAdForceGusToSayCheatId) {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceGusToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceGusToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
    } else if (command == kAdWaterInTheDesertCheatId || command == kAdPowerToTheMassesCheatId)
      AdGetApplication()->GetCheatCodeManager()->SetCheatHasBeenSet(command, true);
    return true;
  }
  if (message->type != kBdMsg_PowerPlantDestroyed)
    return false;
  if (message->data1 != 12115)
    AddReplaceableAdvice(kAdPowerPlantExplodesAdviceId, 0, 8);
  return true;
}

// Query the retained row collection only for the original power or water building selectors.
// Populate the chosen list through its real records and release the collection before reporting success.
bool AdUtilitiesAdvisor::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  u32 selector = static_cast<u32>(message->data1);
  if (message->type != kWnMsg_QueryOpened || (selector != kUiQueryPowerBuildings && selector != kUiQueryWaterBuildings))
    return false;
  UiQueryRows *rows = nullptr;
  if (!source->QueryInterface(IID_UiQueryRows, reinterpret_cast<void **>(&rows)))
    return false;
  if (selector == kUiQueryPowerBuildings)
    WritePowerRows(rows);
  else
    WriteWaterRows(rows);
  rows->Release();
  return true;
}

// DoQueryInfo retains the original constant-success effect.
void AdUtilitiesAdvisor::WritePowerRows(UiQueryRows *rows) {
  bool reveal = AdGetApplication()->GetCheatCodeManager()->GetCheatHasBeenSet(kAdPowerToTheMassesCheatId);
  nuclear_ban = ordinances->QueryOrdinanceOn(kCtOrdinance_NuclearFreeZone);
  for (AdUtilityLink *node = power_entries->next; node != power_entries; node = node->next) {
    if ((reveal || *static_cast<AdUtilityNode *>(node)->entry->AvailableData() == 1) &&
        (*static_cast<AdUtilityNode *>(node)->entry->TypeData() != 12115 || !nuclear_ban)) {
      RsKey key = *static_cast<AdUtilityNode *>(node)->entry->GetKey();
      UiQueryRow *row = rows->CreateRow();
      row->SetText(kUiCatalogNameField, static_cast<AdUtilityNode *>(node)->entry->ThirdText());
      row->SetU32(kUiCatalogResourceTypeField, key.type);
      row->SetU32(kUiCatalogResourceGroupField, key.group);
      row->SetU32(kUiCatalogResourceInstanceField, key.instance);
      row->SetText(kUiCatalogDescriptionField, static_cast<AdUtilityNode *>(node)->entry->FirstText());
      row->SetText(kUiCatalogSecondaryTextField, static_cast<AdUtilityNode *>(node)->entry->SecondText());
      rows->AppendRow(row);
    }
  }
}

void AdUtilitiesAdvisor::WriteWaterRows(UiQueryRows *rows) {
  bool reveal = AdGetApplication()->GetCheatCodeManager()->GetCheatHasBeenSet(kAdWaterInTheDesertCheatId);
  for (AdUtilityLink *node = water_entries->next; node != water_entries; node = node->next) {
    if (reveal || *static_cast<AdUtilityNode *>(node)->entry->AvailableData() == 1) {
      RsKey key = *static_cast<AdUtilityNode *>(node)->entry->GetKey();
      UiQueryRow *row = rows->CreateRow();
      row->SetText(kUiCatalogNameField, static_cast<AdUtilityNode *>(node)->entry->ThirdText());
      row->SetU32(kUiCatalogResourceTypeField, key.type);
      row->SetU32(kUiCatalogResourceGroupField, key.group);
      row->SetU32(kUiCatalogResourceInstanceField, key.instance);
      row->SetText(kUiCatalogDescriptionField, static_cast<AdUtilityNode *>(node)->entry->FirstText());
      if (*static_cast<AdUtilityNode *>(node)->entry->TypeData() != 12820)
        row->SetText(kUiCatalogSecondaryTextField, static_cast<AdUtilityNode *>(node)->entry->SecondText());
      rows->AppendRow(row);
    }
  }
}

// Sample the startup date and retain the actual budget-advisor identity before enabling dated utility entries.
// Queue special-city advice, register the adjusted agent and run the shared initial sample/advice pass in source order.
bool AdUtilitiesAdvisor::SimulationBegin() {
  simulation_city->GetDate(&day, &year);
  budget_advisor = ScAddRefNotNull(simulation_city->GetBudgetAdvisor());
  EnableAvailableBuildings();
  if (simulation_city->GetCityType() == 4 || simulation_city->GetCityType() == 7)
    AddAdvice(270);
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->AddAgent(agent);
  return AdBaseAdvisor::SimulationBegin();
}

// Read the actual city date independently of the advisor cache and leave already available entries enabled.
void AdUtilitiesAdvisor::EnableAvailableBuildings() {
  u32 current_day, current_year;
  simulation_city->GetDate(&current_day, &current_year);
  for (AdUtilityLink *node = power_entries->next; node != power_entries; node = node->next)
    if (*static_cast<AdUtilityNode *>(node)->entry->Value76() <= current_year)
      *static_cast<AdUtilityNode *>(node)->entry->AvailableData() = 1;
  for (AdUtilityLink *node = water_entries->next; node != water_entries; node = node->next)
    if (*static_cast<AdUtilityNode *>(node)->entry->Value76() <= current_year)
      *static_cast<AdUtilityNode *>(node)->entry->AvailableData() = 1;
}

// Enable and announce only the first eligible unavailable entry, searching power before water with a24-turn delay.
void AdUtilitiesAdvisor::AnnounceAvailableBuilding() {
  u32 current_day, current_year;
  simulation_city->GetDate(&current_day, &current_year);
  AdRandom random(AdUtilitiesClock.ElapsedLow());
  if (random.Next(100) >= 10)
    return;
  for (AdUtilityLink *node = power_entries->next; node != power_entries; node = node->next)
    if (*static_cast<AdUtilityNode *>(node)->entry->AvailableData() == 0 &&
        *static_cast<AdUtilityNode *>(node)->entry->Value76() <= current_year) {
      *static_cast<AdUtilityNode *>(node)->entry->AvailableData() = 1;
      AddCategoryOneAdvice(*static_cast<AdUtilityNode *>(node)->entry->Value80(), 0, 24);
      return;
    }
  for (AdUtilityLink *node = water_entries->next; node != water_entries; node = node->next)
    if (*static_cast<AdUtilityNode *>(node)->entry->AvailableData() == 0 &&
        *static_cast<AdUtilityNode *>(node)->entry->Value76() <= current_year) {
      *static_cast<AdUtilityNode *>(node)->entry->AvailableData() = 1;
      AddCategoryOneAdvice(*static_cast<AdUtilityNode *>(node)->entry->Value80(), 0, 24);
      return;
    }
}

// Preserve repeated reactivations, short-circuit order and conditional draws from the elapsed-clock generator.
void AdUtilitiesAdvisor::CheckPetitioners() {
  AdRandom random(AdUtilitiesClock.ElapsedLow());
  if (unknown_180 < 100)
    return;
  if (unknown_168 <= 90) {
    if (ordinance_18e != 0 && !ReactivatePetitioner(kAdWaterConservationUnderAttackPetitionerId) &&
        random.Next(100) == 0)
      AddPetitioner(kAdWaterConservationUnderAttackPetitionerId, 0, 120);
  } else if (ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_WaterConservation) &&
      !ReactivatePetitioner(kAdWaterConservationProposedPetitionerId) && random.Next(100) == 0)
    AddPetitioner(kAdWaterConservationProposedPetitionerId, 0, 120);
  if (water_capacity != 0 || water_used != 0 || ReactivatePetitioner(kAdSimsNeedWaterPetitionerId)) {
    if (unknown_164 <= unknown_160 - unknown_164 || ReactivatePetitioner(kAdWaterShortageCriticalPetitionerId)) {
      if (water_capacity == water_used) {
        if (unknown_164 <= 10 || unknown_164 >= 35) {
          if (valve2 == 0 && !ReactivatePetitioner(kAdCactusNotFavoriteSimShrubPetitionerId) && random.Next(100) < 10)
            AddPetitioner(kAdCactusNotFavoriteSimShrubPetitionerId, 0, 12);
        } else if (!ReactivatePetitioner(kAdWaterSupplyDwindlingPetitionerId) && random.Next(100) < 10)
          AddPetitioner(kAdWaterSupplyDwindlingPetitionerId, 0, 12);
      }
    } else if (random.Next(100) < 5)
      AddPetitioner(kAdWaterShortageCriticalPetitionerId, 0, 12);
  } else if (!ReactivatePetitioner(kAdSimsNeedWaterPetitionerId) && random.Next(100) < 5)
    AddPetitioner(kAdSimsNeedWaterPetitionerId, 0, 6);
  if (unknown_1e8 != 0) {
    if (!environment_advisor->CheckGarbageBuildingAvailibility(12853) || unknown_190 != 0 || ReactivatePetitioner(69)) {
      if ((!ReactivatePetitioner(kAdGarbagePilingUpInStreetsPetitionerId) || !ReactivatePetitioner(68)) &&
          random.Next(100) < 4) {
        u32 choice = random.Next(2);
        if (choice == 0)
          AddPetitioner(kAdGarbagePilingUpInStreetsPetitionerId, 0, 24);
        else if (choice == 1)
          AddPetitioner(68, 0, 24);
      }
    } else if (random.Next(100) < 3)
      AddPetitioner(69, 0, 12);
  }
  if (valve0 == 0 && unknown_180 > 100) {
    i32 used = static_cast<i32>(unknown_138);
    if (used > 0) {
      i32 disconnected = static_cast<i32>(unknown_13c);
      if (static_cast<u32>(disconnected) <= unknown_140 / 4 || disconnected <= 10) {
        i32 capacity = static_cast<i32>(unknown_134);
        if (capacity > used) {
          if (capacity <= 11 * used / 10)
            AddPetitioner(kAdBrownoutsPlagueCityPetitionerId, kAdPetitionReplacementPowerShortage, 9);
        } else
          AddPetitioner(kAdBlackoutsOccurringPetitionerId, kAdPetitionReplacementPowerShortage, 6);
      } else
        AddPetitioner(kAdMorePowerNeededPetitionerId, kAdPetitionReplacementPowerShortage, 12);
    }
  }
}

u8 AdUtilitiesAdvisor::NewDealsEnabled(u32 type) {
  return type < 6 ? new_deals[type] : 0;
}

// Queue utilities advice from cached power/water capacity, network damage and treatment samples.
// Preserve elapsed-clock random seeding, advice reactivation order and the original signed capacity arithmetic.
bool AdUtilitiesAdvisor::CheckOnAdvice() {
  AdRandom random(AdUtilitiesClock.ElapsedLow());
  if (valve1 == 0 && unknown_134 > 2u * unknown_130 && unknown_184 != 0 && neighbors->CountNeighborsWithField144() == 0)
    AddAdvice(5, 0);
  if (valve0 == 0 && unknown_130 > 95u * unknown_134 / 100 && unknown_184 != 0 &&
      neighbors->CountNeighborsWithField144() == 0)
    AddAdvice(6, 0);
  if (valve3 == 0 && water_capacity > static_cast<i64>(2u * static_cast<u64>(water_used)) && unknown_184 != 0 &&
      neighbors->CountNeighborsWithField140() == 0)
    AddAdvice(kAdWaterConnectionSuggestedAdviceId, 0);
  if (valve2 == 0 && water_used > static_cast<i64>(95u * static_cast<u64>(water_capacity)) / 100 && unknown_184 != 0 &&
      neighbors->CountNeighborsWithField140() == 0)
    AddAdvice(kAdWaterConnectionNeededAdviceId, 0);
  u8 imported = valve0;
  if (imported == 0 && unknown_194 == 0)
    AddAdvice(270, kAdReplacementPowerSupply);
  else if (static_cast<i32>(unknown_138) > 0) {
    if (imported != 0)
      AddAdvice(kAdPowerSupplyAdequateAdviceId, kAdReplacementPowerSupply);
    else if (unknown_130 > unknown_134 * power_usage_critical / 100) {
      if (!ReactivateAdvice(kAdPowerPlantsAtMaxCapacityAdviceId) && !ReactivateAdvice(9)) {
        u32 choice = random.Next(2);
        if (choice == 0)
          AddAdvice(kAdPowerPlantsAtMaxCapacityAdviceId, kAdReplacementPowerSupply);
        else if (choice == 1)
          AddAdvice(9, kAdReplacementPowerSupply);
      }
    } else if (unknown_130 > unknown_134 * power_usage_high / 100) {
      if (!ReactivateAdvice(kAdBlackoutsOccurringAdviceId) && !ReactivateAdvice(10)) {
        u32 choice = random.Next(2);
        if (choice == 0)
          AddAdvice(kAdBlackoutsOccurringAdviceId, kAdReplacementPowerSupply);
        else if (choice == 1)
          AddAdvice(10, kAdReplacementPowerSupply);
      }
    } else if (unknown_130 <= unknown_134 * power_usage_medium / 100) {
      if (valve1 == 0)
        AddAdvice(kAdExcessPowerWastedAdviceId, kAdReplacementPowerSupply);
    } else
      AddAdvice(kAdPowerSupplyAdequateAdviceId, kAdReplacementPowerSupply);
  }
  if (static_cast<i32>(unknown_194) > 0 && valve0 == 0 && static_cast<i32>(unknown_138) > static_cast<i32>(unknown_134))
    AddAdvice(kAdSimsNeedPowerAdviceId, kAdReplacementPowerAccess);
  if (static_cast<i32>(unknown_134) > static_cast<i32>(unknown_138) || valve0 != 0) {
    i32 damaged = static_cast<i32>(unknown_13c);
    if (damaged <= static_cast<i32>(unknown_140) / 2) {
      if (damaged <= 10) {
        if (HasAdvice(kAdManySimsOffPowerGridAdviceId) || HasAdvice(kAdSomeSimsOffPowerGridAdviceId))
          AddReplaceableAdvice(kAdPowerGridCoversCityAdviceId, kAdReplacementPowerGridCoverage, 6);
      } else
        AddAdvice(kAdSomeSimsOffPowerGridAdviceId, kAdReplacementPowerGridCoverage);
    } else
      AddAdvice(kAdManySimsOffPowerGridAdviceId, kAdReplacementPowerGridCoverage);
  }
  i64 capacity = water_capacity;
  if (water_capacity != 0) {
    if (water_used == capacity && valve2 == 0)
      if (unknown_184 < 4 && CheckWaterBuildingAvailibility(12830))
        AddAdvice(kAdWaterSupplyNotMeetingDemandAdviceId, kAdReplacementWaterSystem);
      else
        AddAdvice(kAdSimsNeedWaterAdviceId, kAdReplacementWaterSystem);
    else if (water_used <= static_cast<i64>(water_usage_medium * static_cast<u64>(capacity)) / 100)
      AddAdvice(kAdWaterToSpareAdviceId, kAdReplacementWaterSystem);
    else
      AddAdvice(kAdExcellentWaterSystemAdviceId, kAdReplacementWaterSystem);
  } else if (unknown_160 > 30)
    AddAdvice(kAdNotEnoughWaterAdviceId, kAdReplacementWaterSystem);
  u32 damaged = unknown_164;
  if (damaged > 20 && (water_capacity > water_used || valve2 != 0)) {
    u32 total = unknown_160;
    if (2u * damaged <= total || total <= 50)
      AddAdvice(kAdSomeTooFarFromWaterAdviceId, kAdReplacementWaterSystem);
    else
      AddAdvice(kAdWaterGridReachesFewAdviceId, kAdReplacementWaterSystem);
  } else if (HasAdvice(kAdWaterGridReachesFewAdviceId) || HasAdvice(kAdSomeTooFarFromWaterAdviceId))
    AddReplaceableAdvice(kAdEntireCityOnWaterGridAdviceId, kAdReplacementWaterSystem, 6);
  if (static_cast<i32>(unknown_1a4) > static_cast<i32>(unknown_194) / 4)
    AddAdvice(kAdManyPowerPlantsOldAdviceId, 0);
  if (unknown_1ac != 0)
    AddAdvice(kAdNuclearPlantNearEndOfLifeAdviceId, 0);
  if (static_cast<i32>(unknown_1a0) > static_cast<i32>(unknown_19c) / 4)
    AddAdvice(kAdPumpsGettingOldAdviceId, 0);
  if (static_cast<i32>(unknown_1a8) > static_cast<i32>(unknown_198) / 2)
    AddAdvice(kAdIncineratorsGettingOldAdviceId, 0);
  return true;
}

// Keep cancellation before removal, and remove rejected active deals before their pending proposals.
void AdUtilitiesAdvisor::RespondToPetitioner(u32 id, i32 button) {
  u8 type;
  if (button == 1) {
    switch (id) {
    case 165:
    case 166:
    case 167:
    case 168:
      neighbors->AcceptPendingDeal(kCtDealImportWater);
      break;
    case 170:
      neighbors->CancelDeal(kCtDealImportWater);
      break;
    case 172:
    case 173:
    case 175:
    case 202:
      neighbors->AcceptPendingDeal(kCtDealImportPower);
      break;
    case 177:
      neighbors->CancelDeal(kCtDealImportPower);
      break;
    case 179:
    case 180:
    case 181:
    case 248:
      neighbors->AcceptPendingDeal(kCtDealExportPower);
      break;
    case 185:
    case 186:
    case 187:
    case 189:
      neighbors->AcceptPendingDeal(kCtDealExportWater);
      break;
    case 191:
    case 192:
    case 193:
    case 194:
      neighbors->AcceptPendingDeal(kCtDealImportGarbage);
      break;
    case 195:
      neighbors->CancelDeal(kCtDealExportGarbage);
      break;
    case 196:
    case 197:
    case 199:
    case 247:
      neighbors->AcceptPendingDeal(kCtDealExportGarbage);
      break;
    case 233:
    case 234:
    case 235:
    case 236:
    case 237:
    case 238:
      type = static_cast<u8>((id - 233) ^ 1u);
      neighbors->CancelDeal(type);
      neighbors->RemoveDeal(type);
      break;
    }
  } else if (button == 2) {
    switch (id) {
    case 165:
    case 166:
    case 167:
    case 168:
      type = 2;
      break;
    case 172:
    case 173:
    case 175:
    case 202:
      type = 0;
      break;
    case 179:
    case 180:
    case 181:
    case 248:
      type = 1;
      break;
    case 185:
    case 186:
    case 187:
    case 189:
      type = 3;
      break;
    case 191:
    case 192:
    case 193:
    case 194:
      type = 5;
      break;
    case 196:
    case 197:
    case 199:
    case 247:
      type = 4;
      break;
    default:
      return;
    }
    neighbors->RemoveDeal(type);
    neighbors->RemovePendingDeal(type);
  }
}

// Sample actual utility production, consumers, water outputs, neighbor deals and garbage accounting in source call order.
// Preserve low-word wrapping arithmetic and repeated building queries while keeping the genuine wide totals.
void AdUtilitiesAdvisor::GetSimulationData() {
  if (simulation_city)
    simulation_city->GetDate(&day, &year);
  if (residential)
    unknown_180 = static_cast<u32>(residential->GetPopulation());
  if (budget)
    treasury = budget->GetTotalFunds();
  if (service_ec) {
    unknown_134 = service_ec->GetTotalCityPowerProduced();
    unknown_130 = service_ec->GetTotalCityPowerUsed();
    unknown_138 = service_ec->GetTotalPowerNeededByTheCity();
    unknown_13c = service_ec->GetNumberOfUnpoweredConsumers();
    unknown_140 = service_ec->GetNumberOfPoweredConsumers();
    unknown_144 = unknown_134 - unknown_138;
    unknown_1a4 = service_ec->GetNumberOfAgingPowerPlants();
    unknown_1ac = service_ec->GetNumberOfAgingNuclearPlants();
  }
  if (service_f0) {
    service_f0->GetWaterData(&unknown_168, &water_capacity, &water_used, &unknown_160, &unknown_164);
    water_balance = static_cast<i64>(static_cast<u64>(water_capacity) - static_cast<u64>(water_used));
    unknown_1a0 = service_f0->GetNumPumpsAging();
  }
  if (neighbors) {
    unknown_184 = neighbors->CountNeighbors();
    valve1 = neighbors->HasDealType(kCtDealExportPower);
    valve0 = neighbors->HasDealType(kCtDealImportPower);
    valve3 = neighbors->HasDealType(kCtDealExportWater);
    valve2 = neighbors->HasDealType(kCtDealImportWater);
    valve5 = neighbors->HasDealType(kCtDealImportGarbage);
    valve4 = neighbors->HasDealType(kCtDealExportGarbage);
  }
  if (pollution) {
    garbage_produced_last = pollution->GetTotalGarbageProducedLastMonth();
    garbage_imported_last = pollution->GetTotalGarbageImportedLastMonth();
    garbage_recycled_last = pollution->GetTotalGarbageRecycledLastMonth();
    unknown_1b0 = static_cast<u32>(pollution->GetAvailableLandfillCapacity());
    EcLayer *layer = pollution;
    u32 daily = static_cast<u32>(layer->GetDailyWasteToEnergyCapacity());
    unknown_1b4 = 30u * (static_cast<u32>(layer->GetDailyIncineratorCapacity()) + daily);
    unknown_1b8 = static_cast<u32>(garbage_recycled_last) + unknown_1b4 + unknown_1b0 / 12u;
    garbage_total = static_cast<i64>(static_cast<u64>(garbage_produced_last) + static_cast<u64>(garbage_imported_last));
    unknown_1e8 = pollution->IsGarbageUncollected();
    garbage_balance = static_cast<i64>(static_cast<u64>(unknown_1b8) - static_cast<u64>(garbage_total));
    unknown_1a8 = static_cast<u32>(pollution->GetAgingIncineratorCount());
  }
  if (ordinances) {
    ordinance_18e = ordinances->QueryOrdinanceOn(kCtOrdinance_WaterConservation);
    nuclear_ban = ordinances->QueryOrdinanceOn(kCtOrdinance_NuclearFreeZone);
  }
  unknown_190 = static_cast<u32>(buildings->CheckOccupantTracking(12853));
  unknown_194 = static_cast<u32>(buildings->CheckOccupantTracking(12101));
  GeBuildingLayer *layer = buildings;
  u32 count = static_cast<u32>(layer->CheckOccupantTracking(12853));
  unknown_198 = static_cast<u32>(layer->CheckOccupantTracking(12852)) + count;
  unknown_19c = static_cast<u32>(buildings->CheckOccupantTracking(12007));
}

static bool AdReadUtilitiesValue(AdIni *reader, const char *section_name, const char *key_name, ScString *out) {
  ScString key(key_name), section(section_name);
  return reader->ReadEntry(section, key, out);
}

static void AdUtilitiesPowerIniEntry(const ScString &key, const ScString &value, void *identity) {
  static_cast<AdUtilitiesAdvisor *>(static_cast<AdUtilities *>(identity))->ConfigureBuilding(0, &key, &value);
}

static void AdUtilitiesWaterIniEntry(const ScString &key, const ScString &value, void *identity) {
  static_cast<AdUtilitiesAdvisor *>(static_cast<AdUtilities *>(identity))->ConfigureBuilding(1, &key, &value);
}

// Load the original utility cutoffs and deal constants while retaining defaults for absent keys.
// Enumerate both building sections even after a scalar failure and report the combined scalar-key result.
bool AdUtilitiesAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-Utilities.INI", 26);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadUtilitiesValue(&reader, "FUNDING_RATE_CUTOFFS", "POWER_FUNDING_HIGH", &value))
    power_high = atof(value.begin) * 0.01;
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "FUNDING_RATE_CUTOFFS", "POWER_FUNDING_MEDIUM", &value))
    power_low = atof(value.begin) * 0.01;
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "FUNDING_RATE_CUTOFFS", "WATER_FUNDING_HIGH", &value))
    water_high = atof(value.begin) * 0.01;
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "FUNDING_RATE_CUTOFFS", "WATER_FUNDING_MEDIUM", &value))
    water_low = atof(value.begin) * 0.01;
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "CONSUMPTION_RATE_CUTOFFS", "POWER_CONSUMPTION_CRITICAL", &value))
    power_usage_critical = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "CONSUMPTION_RATE_CUTOFFS", "POWER_CONSUMPTION_HIGH", &value))
    power_usage_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "CONSUMPTION_RATE_CUTOFFS", "POWER_CONSUMPTION_MEDIUM", &value))
    power_usage_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "CONSUMPTION_RATE_CUTOFFS", "WATER_CONSUMPTION_CRITICAL", &value))
    water_usage_critical = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "CONSUMPTION_RATE_CUTOFFS", "WATER_CONSUMPTION_HIGH", &value))
    water_usage_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "CONSUMPTION_RATE_CUTOFFS", "WATER_CONSUMPTION_MEDIUM", &value))
    water_usage_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "NEIGHBOR_DEAL_CONSTANTS", "LOW_TREASURY_LEVEL", &value))
    low_treasury_level = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "NEIGHBOR_DEAL_CONSTANTS", "MINIMUM_DEAL_POPULATION", &value))
    minimum_deal_population = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadUtilitiesValue(&reader, "PETITIONER_GENERATION", "WATER_CONSERVATION_YEAR", &value))
    water_conservation_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  {
    ScString section("POWER_BUILDINGS");
    reader.EnumEntries(section, AdUtilitiesPowerIniEntry, static_cast<AdUtilities *>(this));
  }
  {
    ScString section("WATER_BUILDINGS");
    reader.EnumEntries(section, AdUtilitiesWaterIniEntry, static_cast<AdUtilities *>(this));
  }
  return !missing;
}

static ScString AdTakeUtilitiesField(ScString *input) {
  u32 length = static_cast<u32>(input->end - input->begin), position = 0;
  while (position < length && input->begin[position] != ';')
    ++position;
  if (position == length)
    return ScString();
  ScString prefix(input->begin, position);
  input->ScString::Erase(0, position + 1);
  return ScString(prefix);
}

static ScString AdResolveUtilitiesText(u32 instance, u32 group) {
  return AdTextHandle(instance, group).ToString();
}

void AdUtilitiesAdvisor::ConfigureBuilding(u32 selector, const ScString *key, const ScString *value) {
  GeBuildingType *attribute = nullptr;
  ScString remaining(*value), format;
  RsKey name_key = {0, 0, 0};
  ScString unused = AdTakeUtilitiesField(&remaining), field = AdTakeUtilitiesField(&remaining);
  u32 type = AdParseIniNumber(const_cast<char *>(key->ToChar()));
  u32 first = AdParseIniNumber(field.begin), second = AdParseIniNumber(remaining.begin);
  if (!type || !AdGetOccupantAttribCache()->GetBuildingAttrib(type, &attribute))
    return;
  AdUtilityEntry *entry = new AdUtilityEntry;
  entry->AddRef();
  *entry->TypeData() = type;
  entry->SetKey(attribute->Key());
  *entry->Value76() = first;
  *entry->Value80() = second;
  *entry->AvailableData() = static_cast<u8>(first == 0);
  entry->GetCachedAttrib()->GetNameKey(&name_key);
  format = AdResolveUtilitiesText(name_key.instance, name_key.group);
  entry->SetThirdText(&format);
  u32 cost = static_cast<u32>(budget->GetCost(type));
  if (cost) {
    format = AdResolveUtilitiesText(kRsGameCostText, kAdAdvisorLabelGroup);
    AdGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(static_cast<i64>(cost), &field);
    format.ScString::Append(field.begin, static_cast<u32>(field.end - field.begin));
    entry->SetFirstText(&format);
  }
  if (selector <= 1) {
    u32 capacity = selector;
    if (!selector) {
      service_ec->GetPlantCapacity(type, &capacity);
      format = AdResolveUtilitiesText(kRsBuildingCatalogPowerMwhFormatText, kRsBuildingCatalogTextGroup);
    } else {
      capacity = service_f0->GetProductionCapacity(type);
      format =
          AdResolveUtilitiesText(kRsBuildingCatalogCapacityCubicMetersFormatMonthText, kRsBuildingCatalogTextGroup);
    }
    char number[12];
    int length = snprintf(number, sizeof(number), "%i", static_cast<i32>(capacity));
    field.ScString::FromChar(number, static_cast<u32>(length));
    remaining.Sprintf(format.begin, field.begin);
    entry->SetSecondText(&remaining);
    AdUtilityLink *head = selector ? water_entries : power_entries;
    AdUtilityNode *node = new AdUtilityNode;
    node->next = head;
    node->previous = head->previous;
    node->entry = entry;
    node->previous->next = node;
    head->previous = node;
  }
  attribute->Release();
}

void AdUtilitiesAdvisor::InitializeOrdinances() {
  for (u32 id = 7001; id <= 7012; ++id)
    AddOrdinance(id);
}

// Acquire the actual utility service identities, load configuration and attach both original city phase manipulators.
// Preserve the source's partial initialization and failure cleanup before registering notifications and shared advisor setup.
bool AdUtilitiesAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  simulation_city = input;
  input->AddRef();
  ScUnknown *layer = simulation_city->GetSpecificLayer(kCtPowerSimulationLayerType);
  if (!layer) {
    simulation_city->Release();
    simulation_city = nullptr;
    return false;
  }
  layer->QueryInterface(IID_CtPowerLayer, reinterpret_cast<void **>(&service_ec));
  layer = simulation_city->GetSpecificLayer(kCtNeighborsLayerType);
  if (!layer) {
    simulation_city->Release();
    simulation_city = nullptr;
    // The original unconditionally reads the initial null neighbor view here; exclude that undefined dereference.
    if (neighbors)
      neighbors->Release();
    neighbors = nullptr;
    return false;
  }
  layer->QueryInterface(IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors));
  layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer) {
    simulation_city->Release();
    simulation_city = nullptr;
    service_ec->Release();
    service_ec = nullptr;
    return false;
  }
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = simulation_city->GetSpecificLayer(kCtPlumbingSimulationLayerType);
  layer->QueryInterface(IID_CtPlumbingLayer, reinterpret_cast<void **>(&service_f0));
  layer = simulation_city->GetSpecificLayer(kEcPollutionLayerType);
  layer->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution));
  layer = simulation_city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves));
  layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = simulation_city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances));
  layer = simulation_city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  environment_advisor = simulation_city->GetEnvironmentAdvisor();
  if (AdBaseAdvisor::ordinances)
    InitializeOrdinances();
  unknown_134 = 0;
  unknown_130 = 0;
  water_capacity = 0;
  water_used = 0;
  LoadConfiguration();
  callback3 = new AdUtilitiesCallback3(this);
  callback4 = new AdUtilitiesCallback4(this);
  CtAgentType *node = AdGetAgentTree()->GetNode(kCtAgentType_UtilityAdvisorAgent);
  SetAgentType(node);
  node->AttachManipulator(callback3, 3);
  node->AttachManipulator(callback4, 4);
  ScMessageTarget *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->AddNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetMessageServer()->AddNotification(identity, kBdMsg_PowerPlantDestroyed);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdPowerToTheMassesCheatId, AdPowerToTheMasses);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdPowerToTheMassesCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdWaterInTheDesertCheatId, AdWaterInTheDesert);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdWaterInTheDesertCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceGusToSayCheatId, AdForceGusToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdForceGusToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdStopForcingAdviceCheatId);
  return AdBaseAdvisor::Init(input);
}

static AdToken AdMakeUtilitiesToken(u32 kind, const char *name, i32 value) {
  ScString text(name);
  return AdToken(kind, &text, value);
}

static AdToken AdMakeUtilitiesNeighbor(u8 neighbor) {
  ScString text("%NEIGHBOR%");
  return AdToken(4, &text, static_cast<u32>(neighbor));
}

u8 AdUtilitiesAdvisor::AddDealTokens(u32 id, CtDealRecord record) {
  AdToken neighbor = AdMakeUtilitiesNeighbor(record.value_10);
  AddPetitionerToken(id, neighbor);
  if (record.type == 0 || record.type == 2 || record.type == 4) {
    AdToken yearly = AdMakeUtilitiesToken(3, "%Y%", static_cast<i32>(record.value_20));
    AddPetitionerToken(id, yearly);
    AdToken weekly = AdMakeUtilitiesToken(3, "%W%", static_cast<i32>(record.value_18));
    AddPetitionerToken(id, weekly);
  } else {
    AdToken yearly = AdMakeUtilitiesToken(3, "%Y%", static_cast<i32>(record.value_0c));
    AddPetitionerToken(id, yearly);
    i32 quantity = static_cast<i32>(record.value_08);
    if (record.type == 5) {
      volatile f64 scaled = static_cast<f64>(pollution->GetGarbageScalingFactor()) * quantity + kScDoubleIntegerBias;
      f64 rounded = scaled;
      u64 bits;
      memcpy(&bits, &rounded, sizeof(bits));
      quantity = static_cast<i32>(static_cast<u32>(bits) + kScDoubleIntegerLowWordBias);
    }
    AdToken amount = AdMakeUtilitiesToken(2, "%X%", quantity);
    AddPetitionerToken(id, amount);
  }
  AdToken monthly = AdMakeUtilitiesToken(3, "%Z%", static_cast<i32>(record.value_1c));
  AddPetitionerToken(id, monthly);
  static_cast<AdPetitioners *>(petitioners)->queue->RecreateNotification(id, 8);
  return 1;
}

static void AdSendUtilitiesDealMessage(u32 type) {
  ScMessage message = {type, 0, 0, 0};
  AdGetMessageServer()->MessageSend(message);
}

// This native extraction preserves offer checks and ignores the same pending-record result as the original.
static void AdRenewUtilitiesDeal(AdUtilitiesAdvisor *owner, u8 type, u32 id, CtDealRecord *record) {
  if (!owner->neighbors->GetDeal(type, record)) {
    owner->RemovePetitioner(id);
    return;
  }
  i32 age = static_cast<i32>(record->value_14);
  if (age > 66) {
    owner->RemovePetitioner(id);
    owner->neighbors->AcceptPendingDeal(type);
  } else if (age >= 60) {
    bool show = true;
    if (!owner->neighbors->HasPendingDealType(type) && !owner->neighbors->MakeOffer(type)) {
      CtDeal *deal = nullptr;
      if (owner->neighbors->GetDealObject(type, &deal) && deal) {
        deal->SetValue14(0);
        deal->Release();
      }
      show = false;
    }
    AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
    if (show && owner->AddDismissiblePetitioner(id, 0)) {
      owner->neighbors->GetPendingDeal(type, record);
      owner->AddDealTokens(id, *record);
    }
    AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
  } else
    owner->RemovePetitioner(id);
}

// Negotiate all six utility deal types in original order, then renew aging deals and present queued cancellations.
u8 AdUtilitiesAdvisor::CheckDeals() {
  AdRandom random(AdUtilitiesClock.ElapsedLow());
  CtDealRecord record;
  u32 id;
  if (!neighbors->HasDealType(kCtDealExportPower) && NewDealsEnabled(kCtDealExportPower)) {
    u8 neighbor = neighbors->SelectNeighbor(kCtDealExportPower);
    if (neighbor == 4 ||
        (unknown_138 >= 4u * unknown_134 / 10u || static_cast<i32>(unknown_144) <= 1000 ||
            unknown_180 <= minimum_deal_population))
      neighbors->RemovePendingDeal(kCtDealExportPower);
    else {
      id = GetPetitionerIdForReplacement(kAdPetitionReplacementPowerExportDeal);
      if (id) {
        if (neighbors->GetPendingDeal(kCtDealExportPower, &record)) {
          if (neighbors->CanDeal(record.value_10, kCtDealExportPower))
            ReactivatePetitioner(id);
          else {
            neighbors->RemovePendingDeal(kCtDealExportPower);
            RemovePetitioner(id);
          }
        }
      } else {
        u32 choice = random.Next(3);
        id = choice == 0 ? 179 : choice == 1 ? 180 : 181;
        neighbors->ComputeOffer(neighbor, kCtDealExportPower, unknown_144);
        if (neighbors->GetPendingDeal(kCtDealExportPower, &record)) {
          AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
          AddPetitioner(id, kAdPetitionReplacementPowerExportDeal, 1);
          AddDealTokens(id, record);
          AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
        }
      }
      if (treasury < static_cast<i32>(low_treasury_level) && ContainsActivePetitioner(id) && id != 181)
        static_cast<AdBudgetAdvisor *>(budget_advisor)->AddAdvice(kAdDealToSellPowerAdviceId);
    }
  }
  if (neighbors->GetDeal(kCtDealImportPower, &record)) {
    if (static_cast<i32>(record.value_14) > 24) {
      AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
      if (AddDismissiblePetitioner(kAdLetSEmpowerOurselvesPetitionerId, kAdPetitionReplacementPowerImportDeal))
        AddDealTokens(kAdLetSEmpowerOurselvesPetitionerId, record);
      AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
    }
  } else {
    RemovePetitioner(kAdLetSEmpowerOurselvesPetitionerId);
    u8 neighbor = neighbors->SelectNeighbor(kCtDealImportPower);
    if (!NewDealsEnabled(kCtDealImportPower) || neighbor == 4 ||
        (unknown_130 < 8u * unknown_134 / 10u && static_cast<i32>(unknown_13c) <= 30))
      neighbors->RemovePendingDeal(kCtDealImportPower);
    else {
      id = GetPetitionerIdForReplacement(kAdPetitionReplacementPowerImportDeal);
      if (id) {
        if (neighbors->GetPendingDeal(kCtDealImportPower, &record)) {
          if (neighbors->CanDeal(record.value_10, kCtDealImportPower))
            ReactivatePetitioner(id);
          else {
            neighbors->RemovePendingDeal(kCtDealImportPower);
            RemovePetitioner(id);
          }
        }
      } else {
        u32 choice = random.Next(3);
        id = choice == 0 ? 172 : choice == 1 ? 173 : 202;
        neighbors->ComputeOffer(neighbor, kCtDealImportPower, 0);
        if (neighbors->GetPendingDeal(kCtDealImportPower, &record)) {
          AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
          if (AddPetitioner(id, kAdPetitionReplacementPowerImportDeal, 1))
            AddDealTokens(id, record);
          AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
        }
      }
    }
  }
  bool water_deal = neighbors->HasDealType(kCtDealExportWater);
  if (NewDealsEnabled(kCtDealExportWater) && !water_deal) {
    u8 neighbor = neighbors->SelectNeighbor(kCtDealExportWater);
    if (neighbor == 4 ||
        (water_used >= static_cast<i64>(4u * static_cast<u64>(water_capacity)) / 10 || water_balance <= 1000 ||
            unknown_180 <= minimum_deal_population))
      neighbors->RemovePendingDeal(kCtDealExportWater);
    else {
      id = GetPetitionerIdForReplacement(kAdPetitionReplacementWaterExportDeal);
      if (id) {
        if (neighbors->GetPendingDeal(kCtDealExportWater, &record)) {
          if (neighbors->CanDeal(record.value_10, kCtDealExportWater))
            ReactivatePetitioner(id);
          else {
            neighbors->RemovePendingDeal(kCtDealExportWater);
            RemovePetitioner(id);
          }
        }
      } else {
        u32 choice = random.Next(3);
        id = choice == 0 ? 185 : choice == 1 ? 186 : 187;
        neighbors->ComputeOffer(neighbor, kCtDealExportWater, static_cast<u32>(water_balance));
        if (neighbors->GetPendingDeal(kCtDealExportWater, &record)) {
          AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
          if (AddPetitioner(id, kAdPetitionReplacementWaterExportDeal, 1))
            AddDealTokens(id, record);
          AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
        }
      }
    }
  }
  if (neighbors->GetDeal(kCtDealImportWater, &record)) {
    if (static_cast<i32>(record.value_14) > 24) {
      AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
      if (AddDismissiblePetitioner(kAdWaterDealDrainsTreasuryPetitionerId, kAdPetitionReplacementWaterImportDeal))
        AddDealTokens(kAdWaterDealDrainsTreasuryPetitionerId, record);
      AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
    }
  } else {
    RemovePetitioner(kAdWaterDealDrainsTreasuryPetitionerId);
    u8 neighbor = neighbors->SelectNeighbor(kCtDealImportWater);
    if (!NewDealsEnabled(kCtDealImportWater) || neighbor == 4 ||
        (water_used < static_cast<i64>(8u * static_cast<u64>(water_capacity)) / 10 && unknown_164 <= 30u))
      neighbors->RemovePendingDeal(kCtDealImportWater);
    else {
      id = GetPetitionerIdForReplacement(kAdPetitionReplacementWaterImportDeal);
      if (id) {
        if (neighbors->GetPendingDeal(kCtDealImportWater, &record)) {
          if (neighbors->CanDeal(record.value_10, kCtDealImportWater))
            ReactivatePetitioner(id);
          else {
            neighbors->RemovePendingDeal(kCtDealImportWater);
            RemovePetitioner(id);
          }
        }
      } else {
        u32 choice = random.Next(3);
        id = choice == 0 ? 165 : choice == 1 ? 166 : 167;
        neighbors->ComputeOffer(neighbor, kCtDealImportWater, 0);
        if (neighbors->GetPendingDeal(kCtDealImportWater, &record)) {
          AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
          if (AddPetitioner(id, kAdPetitionReplacementWaterImportDeal, 1))
            AddDealTokens(id, record);
          AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
        }
      }
    }
  }
  if (!neighbors->HasDealType(kCtDealImportGarbage) && NewDealsEnabled(kCtDealImportGarbage)) {
    u8 neighbor = neighbors->SelectNeighbor(kCtDealImportGarbage);
    if (neighbor == 4 ||
        (garbage_total >= static_cast<i64>(6u * unknown_1b8 / 10u) || garbage_balance <= 500 ||
            unknown_180 <= minimum_deal_population))
      neighbors->RemovePendingDeal(kCtDealImportGarbage);
    else {
      id = GetPetitionerIdForReplacement(kAdPetitionReplacementGarbageImportDeal);
      if (id) {
        if (neighbors->GetPendingDeal(kCtDealImportGarbage, &record)) {
          if (neighbors->CanDeal(record.value_10, kCtDealImportGarbage))
            ReactivatePetitioner(id);
          else {
            neighbors->RemovePendingDeal(kCtDealImportGarbage);
            RemovePetitioner(id);
          }
        }
      } else {
        u32 choice = random.Next(3);
        id = choice == 0 ? 191 : choice == 1 ? 192 : 193;
        neighbors->ComputeOffer(neighbor, kCtDealImportGarbage, static_cast<u32>(garbage_balance));
        if (neighbors->GetPendingDeal(kCtDealImportGarbage, &record)) {
          AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
          if (AddPetitioner(id, kAdPetitionReplacementGarbageImportDeal, 1))
            AddDealTokens(id, record);
          AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
        }
      }
    }
  }
  if (neighbors->GetDeal(kCtDealExportGarbage, &record)) {
    if (static_cast<i32>(record.value_14) > 24) {
      AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
      if (AddDismissiblePetitioner(kAdTrashExportingDealStinksPetitionerId, kAdPetitionReplacementGarbageExportDeal))
        AddDealTokens(kAdTrashExportingDealStinksPetitionerId, record);
      AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
    }
  } else {
    RemovePetitioner(kAdTrashExportingDealStinksPetitionerId);
    u8 neighbor = neighbors->SelectNeighbor(kCtDealExportGarbage);
    if (!NewDealsEnabled(kCtDealExportGarbage) || neighbor == 4 ||
        (garbage_total < static_cast<i64>(9u * unknown_1b8 / 10u)))
      neighbors->RemovePendingDeal(kCtDealExportGarbage);
    else {
      id = GetPetitionerIdForReplacement(kAdPetitionReplacementGarbageExportDeal);
      if (id) {
        if (neighbors->GetPendingDeal(kCtDealExportGarbage, &record)) {
          if (neighbors->CanDeal(record.value_10, kCtDealExportGarbage))
            ReactivatePetitioner(id);
          else {
            neighbors->RemovePendingDeal(kCtDealExportGarbage);
            RemovePetitioner(id);
          }
        }
      } else {
        u32 choice = random.Next(3);
        id = choice == 0 ? 196 : choice == 1 ? 197 : 247;
        neighbors->ComputeOffer(neighbor, kCtDealExportGarbage, 0);
        if (neighbors->GetPendingDeal(kCtDealExportGarbage, &record)) {
          AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
          if (AddPetitioner(id, kAdPetitionReplacementGarbageExportDeal, 1))
            AddDealTokens(id, record);
          AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
        }
      }
    }
  }
  AdRenewUtilitiesDeal(this, kCtDealImportPower, 175, &record);
  AdRenewUtilitiesDeal(this, kCtDealExportPower, 248, &record);
  AdRenewUtilitiesDeal(this, kCtDealImportWater, 168, &record);
  AdRenewUtilitiesDeal(this, kCtDealExportWater, 189, &record);
  AdRenewUtilitiesDeal(this, kCtDealImportGarbage, 194, &record);
  AdRenewUtilitiesDeal(this, kCtDealExportGarbage, 199, &record);
  CtDeal *canceled = nullptr;
  while (neighbors->GetCanceledDeal(&canceled)) {
    canceled->GetRecord(&record);
    bool possible = neighbors->CanDeal(record.value_10, record.type);
    if (possible) {
      if (record.type <= 1)
        id = 239;
      else if (record.type <= 3)
        id = 240;
      else {
        canceled->Release();
        continue;
      }
    } else if (record.type <= 1)
      id = 230;
    else if (record.type <= 3)
      id = 227;
    else if (record.type <= 5)
      id = 249;
    else {
      canceled->Release();
      continue;
    }
    AdSendUtilitiesDealMessage(kApMsgLockNotificationQueue);
    if (AddReplaceablePetitioner(id, 0, 12))
      AddDealTokens(id, record);
    AdSendUtilitiesDealMessage(kApMsgUnlockNotificationQueue);
    canceled->Release();
  }
  return 1;
}

// Run the actual pending utility sample/advice/building/petition/deal phases before posting completion.
bool AdUtilitiesAdvisor::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    GetSimulationData();
    CheckOnAdvice();
    AnnounceAvailableBuilding();
    advice->Advance();
    advice->ResetRedisplay();
    CheckPetitioners();
    CheckDeals();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdUtilitiesAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

// Construct the496-byte utility advisor and return its zero-reference secondary advisor identity at complete+4.
ScUnknown *AdCreateUtilities() {
  return static_cast<AdAdvisor *>(new AdUtilitiesAdvisor);
}
