#include "advisors/ad_advisors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cassert>

#include "advisors/ad_advice.h"
#include "advisors/ad_domains.h"
#include "advisors/ad_ini.h"
#include "advisors/ad_module.h"
#include "advisors/ad_text.h"
#include "advisors/api.h"
#include "application/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_residential.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "ui/api.h"

// The financial advisor starts with the original debt threshold used by its lose-game policy.
enum AdBudgetDefaultLimit : i32 {
  kAdBudgetDefaultDebtLimit = 0x186a0,
};

// Replaceable petitions coalesce healthcare strikes, teacher strikes and hospital availability independently.
enum AdServicePetitionReplacementId : u32 {
  kAdHealthcareStrikePetitionReplacement = 0xa38293de,
  kAdTeacherStrikePetitionReplacement = 0xa38293ef,
  kAdHospitalAvailabilityPetitionReplacement = 0xc3829422,
};

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdHealthEducationAuraPortraitId : u32 {
  kAdHealthEducationAuraSmallPortraitBase = 0x1000703,
  kAdHealthEducationAuraLargePortraitBase = 0x703,
};

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdEnvironmentPortraitId : u32 {
  kAdEnvironmentSmallPortraitBase = 0x1000403,
  kAdEnvironmentLargePortraitBase = 0x403,
  kAdEnvironmentAlternateSmallPortrait = 0x1020403,
  kAdEnvironmentAlternateLargePortrait = 0x20403,
};

// Advisor portraits retain separate small and large image instances; language variants preserve their instance offset.
enum AdBudgetPortraitId : u32 {
  kAdBudgetSmallPortraitBase = 0x1000203,
  kAdBudgetLargePortraitBase = 0x203,
  kAdBudgetAlternateSmallPortrait = 0x1020203,
  kAdBudgetAlternateLargePortrait = 0x20203,
};


#if defined(_M_IX86)
static_assert(sizeof(AdDealRecord) == 56);
#endif

AdDealRecord::AdDealRecord()
    : value4(0), type(0), value12(0), income(0), active(0), references(0), view(nullptr), enabled(1) {
}

AdDealRecord::~AdDealRecord() {
}

// Publish this identity for Unknown or the actual private deal IID, then retain through the virtual local counter.
// Refused identifiers clear the output.
bool AdDealRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_AdDealRecord, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdDealRecord::AddRef() {
  return ++references;
}

u32 AdDealRecord::Release() {
  return ScReleaseImpl(this, &references);
}

// Expose the first parsed DWORD at its actual storage address without copying or normalizing its bits.
u32 *AdDealRecord::Value4() {
  return &value4;
}

// Expose the full building type used by the original deal-list searches.
u32 *AdDealRecord::TypeData() {
  return &type;
}

// Expose the second parsed DWORD while its more-specific field meaning remains unresolved.
u32 *AdDealRecord::Value12() {
  return &value12;
}

// Expose the raw monthly income DWORD returned directly by the public type lookup.
u32 *AdDealRecord::IncomeData() {
  return &income;
}

// Borrow the actual active byte; callers distinguish exactly one from every other stored value.
u8 *AdDealRecord::ActiveData() {
  return &active;
}

// Borrow the embedded name interface without allocating another String or acquiring a reference.
ScStringView *AdDealRecord::Name() {
  return &name;
}

void AdDealRecord::SetName(ScStringView *value) {
  const char *bytes = value->ToChar();
  name.ScString::FromChar(bytes, static_cast<u32>(strlen(bytes)));
}

// Borrow the stored specific building view without acquiring another reference.
BdDealView *AdDealRecord::View() {
  return view;
}

// Apply6562's reference effects: retain the new view before loading and releasing the current view, then publish it.
void AdDealRecord::SetView(BdDealView *value) {
  if (value)
    value->AddRef();
  if (view)
    view->Release();
  view = value;
}

// Expose the independent enabled byte that the public setter updates across every duplicate type.
u8 *AdDealRecord::EnabledData() {
  return &enabled;
}

// Construct the real advisor bases and borrowed-deal list, then load the four names and locale-selected portraits.
// Initialize only the original tuning defaults, counters and six service pointers; other domain state awaits city Init.
AdBudgetAdvisor::AdBudgetAdvisor() {
  AdDealNode *sentinel = new AdDealNode;
  sentinel->next = sentinel;
  sentinel->previous = sentinel;
  deals = sentinel;
  budget = nullptr;
  valve_layer = nullptr;
  residential = nullptr;
  ordinance_layer = nullptr;
  neighbors = nullptr;
  buildings = nullptr;
  ScString name = AdTextHandle(kRsAdvisorNameFinancialAdvisorText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetNameAdvice(&name);
  name = AdTextHandle(kRsGameFinancialBriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGameFinancialOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameMortimerGreenText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  bool alternate = language == kScLanguageJapanese || (language > kScLanguagePolish && language <= kScLanguageThai);
  const RsKey small = {kRsImageResourceType, kRsAdvisorPortraitGroup,
      alternate ? kAdBudgetAlternateSmallPortrait : kAdBudgetSmallPortraitBase};
  const RsKey large = {kRsImageResourceType, kRsAdvisorPortraitGroup,
      alternate ? kAdBudgetAlternateLargePortrait : kAdBudgetLargePortraitBase};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  lose_game_level = kAdBudgetDefaultDebtLimit;
  treasury_low = 5000;
  offer_delay = 36;
  tax_growth_threshold = 0.05f;
  tax_decline_threshold = -0.1f;
  tax_rates[2] = 0;
  tax_rates[1] = 0;
  tax_rates[0] = 0;
  months_without_offer = 0;
  unknown168 = 0;
  outdated_response = 299;
}

// Free the circular deal-list nodes without releasing their borrowed records, then retire the actual shared advisor base.
AdBudgetAdvisor::~AdBudgetAdvisor() {
  AdDealLink *node = deals->next;
  while (node != deals) {
    AdDealLink *next = node->next;
    delete static_cast<AdDealNode *>(node);
    node = next;
  }
  delete static_cast<AdDealNode *>(deals);
}

bool AdBudgetAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdBudget, AdBudget);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdBudgetAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdBudgetAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdBudgetAdvisor::GetAdvisorID() {
  return kAdBudgetAdvisorId;
}

// Without a delimiter return an empty String and retain the complete input for the final numeric field.
static ScString AdTakeDealField(ScString *input) {
  u32 length = static_cast<u32>(input->end - input->begin), position = 0;
  while (position < length && input->begin[position] != ';')
    ++position;
  if (position == length)
    return ScString();
  ScString prefix(input->begin, position);
  input->ScString::Erase(0, position + 1);
  return prefix;
}

static bool AdReadBudgetValue(AdIni *reader, const char *section_name, const char *key_name, ScString *out) {
  ScString key(key_name), section(section_name);
  return reader->ReadEntry(section, key, out);
}

static void AdBudgetIniEntry(const ScString &key, const ScString &value, void *identity) {
  AdBudgetAdvisor *owner = static_cast<AdBudgetAdvisor *>(static_cast<AdBudget *>(identity));
  owner->ConfigureDeal(&key, &value);
}

// Read five scalar keys in their original order, retaining fields on misses and enumerating business deals afterward.
// Tax thresholds use a signed32-bit decimal integer scaled by the original float constant, not a decimal float parser.
bool AdBudgetAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-Budget.INI", 23);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadBudgetValue(&reader, "TAX_RATE_CUTOFFS", "TaxGrowthThreshold", &value))
    tax_growth_threshold = static_cast<f32>(static_cast<f64>(static_cast<i32>(atol(value.begin))) * 0.0001f);
  else
    missing = true;
  if (AdReadBudgetValue(&reader, "TAX_RATE_CUTOFFS", "TaxDeclineThreshold", &value))
    tax_decline_threshold = static_cast<f32>(static_cast<f64>(static_cast<i32>(atol(value.begin))) * 0.0001f);
  else
    missing = true;
  if (AdReadBudgetValue(&reader, "TREASURY_NUMBERS", "TREASURY_LOW", &value))
    treasury_low = atoi(value.begin);
  else
    missing = true;
  if (AdReadBudgetValue(&reader, "TREASURY_NUMBERS", "LOSE_GAME_LEVEL", &value))
    lose_game_level = atoi(value.begin);
  else
    missing = true;
  if (AdReadBudgetValue(&reader, "BUSINESS_DEALS", "MONTHS_TILL_BUSINESS_DEAL", &value))
    offer_delay = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  ScString section("BUSINESS_DEALS2");
  reader.EnumEntries(section, AdBudgetIniEntry, static_cast<AdBudget *>(this));
  return !missing;
}

// Append the retained record even when attribute lookup fails; a successful attribute supplies its copied name and is
// released.
void AdBudgetAdvisor::ConfigureDeal(const ScString *, const ScString *value) {
  AdDealRecord *record = new AdDealRecord;
  record->AddRef();
  ScString remaining(*value);
  // Preserve the first field's independent copy after retiring the split result.
  ScString field = [&]() {
    ScString first = AdTakeDealField(&remaining);
    return ScString(first);
  }();
  *record->TypeData() = AdParseIniNumber(field.begin);
  field = AdTakeDealField(&remaining);
  *record->Value12() = AdParseIniNumber(field.begin);
  field = AdTakeDealField(&remaining);
  *record->IncomeData() = static_cast<u32>(atoi(field.begin));
  *record->Value4() = AdParseIniNumber(remaining.begin);
  *record->ActiveData() = 0;
  GeBuildingType *attribute = nullptr;
  OcAttribCache *cache = AdGetOccupantAttribCache();
  if (cache->GetBuildingAttrib(*record->TypeData(), &attribute)) {
    ScString name;
    if (attribute->GetName(&name))
      record->SetName(&name);
    attribute->Release();
  }
  AdDealNode *node = new AdDealNode;
  node->next = deals;
  node->previous = deals->previous;
  node->record = record;
  node->previous->next = node;
  deals->previous = node;
}

// Retain the city and query the six actual services in order, preserving partial bindings when a layer is missing.
// Acquire the three demand valves, configure queues/deals, attach concrete callbacks and subscriptions, then initialize the base.
bool AdBudgetAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  simulation_city = input;
  input->AddRef();
  CtLayer *layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = simulation_city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer));
  layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = simulation_city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinance_layer));
  layer = simulation_city->GetSpecificLayer(kCtNeighborsLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors));
  layer = simulation_city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  valve_layer->GetValvePointer(8000, IID_EcValve, reinterpret_cast<void **>(&demand_valves[0]));
  valve_layer->GetValvePointer(5000, IID_EcValve, reinterpret_cast<void **>(&demand_valves[1]));
  valve_layer->GetValvePointer(3000, IID_EcValve, reinterpret_cast<void **>(&demand_valves[2]));
  if (ordinances)
    InitializeOrdinances();
  LoadConfiguration();
  callback3 = new AdBudgetCallback3(this);
  callback4 = new AdBudgetCallback4(this);
  CtAgentType *node = AdGetAgentTree()->GetNode(kCtAgentType_BudgetAdvisorAgent);
  SetAgentType(node);
  node->AttachManipulator(callback3, 3);
  node->AttachManipulator(callback4, 4);
  ScMessageTarget *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->AddNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceMortimerToSayCheatId, AdForceMortimerToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdForceMortimerToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdLetsMakeADealCheatId, AdLetsMakeADeal);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdLetsMakeADealCheatId);
  return AdBaseAdvisor::Init(input);
}

u32 AdBudgetAdvisor::GetLayerType() {
  return kAdBudgetAdvisorLayerType;
}

// Cache the borrowed planning advisor and add the introductory advice for either original qualifying city type.
// Resolve the actual agent identity before registering it, then run the shared sampler and advice check.
bool AdBudgetAdvisor::SimulationBegin() {
  planning = simulation_city->GetCityPlanningAdvisor();
  if (simulation_city->GetCityType() == 4 || simulation_city->GetCityType() == 7)
    AddAdvice(kAdWelcomeFromMortimerGreenAdviceId);
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->AddAgent(agent);
  return AdBaseAdvisor::SimulationBegin();
}

// Resolve and unregister the same city-agent identity, preserving the original ignored removal result.
// Return the shared locking leaf result after the simulator call.
bool AdBudgetAdvisor::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->RemoveAgent(agent);
  return CtAgentBase::IsAgentLocking();
}

// Unsubscribe budget messages and cheats, then release demand valves before the city and retained domain services.
// Clear each deal's linked view and release its record before unlinking its node; keep the sentinel for shared shutdown.
bool AdBudgetAdvisor::Shutdown() {
  CtLayer *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->RemoveNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForceMortimerToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdForceMortimerToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdLetsMakeADealCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdLetsMakeADealCheatId);
  if (demand_valves[2]) {
    demand_valves[2]->Release();
    demand_valves[2] = nullptr;
  }
  if (demand_valves[1]) {
    demand_valves[1]->Release();
    demand_valves[1] = nullptr;
  }
  if (demand_valves[0]) {
    demand_valves[0]->Release();
    demand_valves[0] = nullptr;
  }
  ScRelease(&simulation_city);
  ScRelease(&budget);
  ScRelease(&valve_layer);
  ScRelease(&residential);
  ScRelease(&ordinance_layer);
  ScRelease(&neighbors);
  ScRelease(&buildings);
  while (deals->next != deals) {
    AdDealLink *node = deals->next;
    AdDealRecord *record = static_cast<AdDealNode *>(node)->record;
    record->SetView(nullptr);
    record->Release();
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete static_cast<AdDealNode *>(node);
  }
  return AdBaseAdvisor::Shutdown();
}

void AdBudgetAdvisor::InitializeOrdinances() {
  for (u32 id = 3000; id <= 3008; ++id)
    AddOrdinance(id);
}

// Refresh population and date, signed valve demand values, budget totals, raw tax bytes and their float modifiers.
// Keep the source's service guards and reverse valve/cache order, then derive the two tracked-building presence bytes.
void AdBudgetAdvisor::GetSimulationData() {
  if (residential)
    population = static_cast<u32>(residential->GetPopulation());
  simulation_city->GetDate(&day_of_year, &year);
  if (demand_valves[2])
    demand[0] = demand_valves[2]->QueryValveValue();
  if (demand_valves[1])
    demand[1] = demand_valves[1]->QueryValveValue();
  if (demand_valves[0])
    demand[2] = demand_valves[0]->QueryValveValue();
  if (budget) {
    tax_rates[0] = budget->GetTaxRate(0);
    tax_rates[1] = budget->GetTaxRate(1);
    tax_rates[2] = budget->GetTaxRate(2);
    borrowing_limit = budget->GetCurrentBorrowingLimit();
    borrowed = budget->GetTotalBorrowed();
    treasury = budget->GetTotalFunds();
    estimated_income = budget->GetEstIncome();
    estimated_expenses = budget->GetEstExpenses();
  }
  if (budget) {
    tax_modifiers[0] = budget->GetValveModifier(tax_rates[0]);
    tax_modifiers[1] = budget->GetValveModifier(tax_rates[1]);
    tax_modifiers[2] = budget->GetValveModifier(tax_rates[2]);
  }
  deal_building_present[0] = static_cast<u8>(buildings->CheckOccupantTracking(kCtAgentToxicWastePlant) > 0);
  deal_building_present[1] = static_cast<u8>(buildings->CheckOccupantTracking(19003) > 0);
}

// Apply the actual budget forced-advice and deal-cheat commands, or add the budget ordinance help topic.
// Preserve the mutable command suffix, complete forced ID and unconditional true result for unrelated messages.
bool AdBudgetAdvisor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    switch (static_cast<u32>(message->data1)) {
    case kAdStopForcingAdviceCheatId:
      ClearForcedAdvice();
      break;
    case kAdLetsMakeADealCheatId:
      unknown168 = 1;
      break;
    case kAdForceMortimerToSayCheatId: {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceMortimerToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceMortimerToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
      break;
    }
    }
  } else if (message->type == kCtMsg_OrdinanceAvailable &&
      static_cast<u32>(message->data1) == kCtOrdinance_LegalizedGambling)
    AddHelp(31);
  return true;
}

// Run a pending budget cycle in original order: gather, evaluate, age/reset advice, evaluate deals and replay forced IDs.
// Post the completion message before clearing the raw pending byte; only the exact value one initiates work.
bool AdBudgetAdvisor::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    GetSimulationData();
    CheckOnAdvice();
    advice->Advance();
    advice->ResetRedisplay();
    CheckBusinessDeals();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdBudgetAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

static i64 AdBudgetScale(i64 value, u32 factor) {
  return static_cast<i64>(static_cast<u64>(value) * factor);
}

// Evaluate the actual tax and treasury rules, bankruptcy notice, neighboring-deal advice and petitioner replacement state.
// Preserve branch order, wrapped64-bit arithmetic, eager neighbor queries and the otherwise discarded petitioner lookup.
bool AdBudgetAdvisor::CheckOnAdvice() {
  const u32 replacements[3] = {
      kAdReplacementResidentialTaxes, kAdReplacementCommercialTaxes, kAdReplacementIndustrialTaxes};
  for (u32 index = 0; index < 3; ++index) {
    u32 id = 56 + 4 * index;
    if (tax_modifiers[index] >= tax_decline_threshold || tax_rates[index] < 2) {
      if (tax_modifiers[index] < tax_growth_threshold && tax_rates[index])
        ++id;
    } else
      id += 2;
    AddAdvice(id, replacements[index]);
  }
  i64 expenses = estimated_expenses;
  if (estimated_income <= AdBudgetScale(expenses, 110) / 100) {
    if (estimated_income >= AdBudgetScale(expenses, 98) / 100) {
      if (treasury > treasury_low && treasury < AdBudgetScale(treasury_low, 3))
        AddAdvice(kAdSmallSurplusForYearAdviceId, kAdReplacementBudgetBalance);
    } else {
      i64 threshold = static_cast<i64>(
          static_cast<u64>(treasury_low) + 2u * (static_cast<u64>(expenses) - static_cast<u64>(estimated_income)));
      AddAdvice(treasury <= threshold ? kAdCityLosingMoneyAdviceId : kAdCityExpensesOutpaceIncomeAdviceId,
          kAdReplacementBudgetBalance);
    }
  } else if (treasury > AdBudgetScale(treasury_low, 4))
    AddAdvice(kAdBudgetShowsLargeSurplusAdviceId, kAdReplacementBudgetBalance);
  i64 surplus_threshold =
      static_cast<i64>(5u * static_cast<u64>(estimated_expenses) + 2u * static_cast<u64>(treasury_low));
  if (treasury > surplus_threshold && population > 10000)
    AddAdvice(kAdExcessCashReservesAdviceId, kAdReplacementCashReserves);
  else if (treasury < treasury_low && treasury > 0)
    AddAdvice(kAdLimitedCashReservesAdviceId, kAdReplacementCashReserves);
  else if (treasury < 0) {
    if (treasury > static_cast<i64>(0u - static_cast<u64>(lose_game_level))) {
      if (treasury >= AdBudgetScale(lose_game_level / -10, 8))
        AddAdvice(population <= 50000 ? kAdCityIsBrokeAdviceId : kAdNoCashOnHandAdviceId, kAdReplacementCashReserves);
      else
        AddAdvice(kAdDebtDebtAndMoreDebtAdviceId, kAdReplacementCashReserves);
    } else {
      AddAdvice(kAdFiredAdviceId, kAdReplacementCashReserves);
      // Inline4E4D's complete notice effects; its unused private receiver/nominal return remain unproved.
      ScMessage message = {kWnMsg_ModalAdvisorEndGame, kAdBudgetAdvisorId, 206, 0};
      AdGetMessageServer()->MessagePost(message, false);
    }
  }
  if (treasury < treasury_low) {
    u32 deal_count = neighbors->CountNeighborsWithDeals();
    bool exporting = neighbors->HasDealType(kCtDealImportGarbage);
    i32 seaports = buildings->CheckOccupantTracking(kCtAgentSeaport);
    if (!exporting && !deal_count && !seaports)
      AddAdvice(
          neighbors->CountNeighbors() >= 4 ? kAdNeedConnectionForTrashDealAdviceId : kAdNeedSeaportForTrashDealAdviceId,
          kAdReplacementTrashDealConnection);
  }
  GetPetitionerIdForReplacement(kAdPetitionReplacementBusinessDealOffer);
  if (treasury < treasury_low && GetPetitionerIdForReplacement(kAdPetitionReplacementPowerExportDeal))
    AddAdvice(kAdDealToSellPowerAdviceId, 0);
  return true;
}

bool AdBudgetAdvisor::CheckBusinessDeals() {
  AdRandom random(kScRandomSeedFromClock);
  if (treasury >= treasury_low) {
    months_without_offer = static_cast<i32>(static_cast<u32>(months_without_offer) - 1u);
    if (months_without_offer < 0)
      months_without_offer = 0;
  } else
    months_without_offer = static_cast<i32>(static_cast<u32>(months_without_offer) + 1u);
  u32 selected = GetPetitionerIdForReplacement(kAdPetitionReplacementBusinessDealOffer), replacement = 0;
  AdDealLink *node = deals->next;
  bool evaluate = false, single = false;
  if (unknown168)
    evaluate = node != deals;
  else if (treasury < treasury_low && population > 1000) {
    if (selected) {
      ReactivatePetitioner(selected);
      months_without_offer = 0;
    } else if (offer_delay <= months_without_offer) {
      u32 count = 0;
      for (AdDealLink *entry = deals->next; entry != deals; entry = entry->next)
        ++count;
      // The original immediately dereferences a selected list entry; an empty resource list has no such entry.
      assert(count);
      for (u32 offset = random.Next(count); offset; --offset)
        node = node->next;
      replacement = kAdPetitionReplacementBusinessDealOffer;
      evaluate = single = true;
    }
  }
  if (evaluate) {
    do {
      AdDealNode *entry = static_cast<AdDealNode *>(node);
      switch (*entry->record->TypeData()) {
      case 19001:
        if (year >= *entry->record->Value12() && *entry->record->EnabledData() && !HasBusinessDeal(19001))
          selected = 16;
        break;
      case 19002:
        if (year >= *entry->record->Value12() && *entry->record->EnabledData() && !HasBusinessDeal(19002))
          selected = 206;
        break;
      case 19003:
        if (ordinance_layer->QueryOrdinanceOn(kCtOrdinance_LegalizedGambling) && year >= *entry->record->Value12() &&
            *entry->record->EnabledData() && !HasBusinessDeal(19003))
          selected = 149;
        break;
      case 19004:
        if (year >= *entry->record->Value12() && *entry->record->EnabledData() && !HasBusinessDeal(19004))
          selected = 210;
        break;
      }
      if (selected) {
        AddPetitioner(selected, replacement);
        AdDealRecord *record = entry->record;
        // Destroy the temporary name before the two original by-value token copies and retained parameter retirement.
        AdToken token = [&]() {
          ScString name("%X%");
          return AdToken(3, &name, static_cast<i32>(*record->IncomeData()));
        }();
        AddPetitionerToken(selected, token);
      }
      if (single)
        break;
      node = node->next;
    } while (node != deals);
  }
  if (planning->IsBusinessDealBuildingInRewardList(19001))
    AddPetitioner(kAdToxicWasteFactoryQuestionedPetitionerId, 0, 24);
  else if (deal_building_present[0] && random.Next(100) < 2)
    AddDismissiblePetitioner(kAdProtestToxicWasteConversionPlantPetitionerId, 0);
  if (deal_building_present[1] && random.Next(100) < 2)
    AddDismissiblePetitioner(kAdProtestCasinoPetitionerId, 0);
  if (population <= 100)
    return true;
  if (tax_modifiers[1] >= tax_decline_threshold) {
    if (treasury > treasury_low) {
      if (tax_modifiers[1] >= 0.0f) {
        if (!(tax_modifiers[1] >= tax_growth_threshold) && !ReactivatePetitioner(kAdLowerCommercialTaxesPetitionerId) &&
            !random.Next(100))
          AddPetitioner(kAdLowerCommercialTaxesPetitionerId, kAdPetitionReplacementCommercialTaxes, 12);
      } else if (!ReactivatePetitioner(kAdLowerTaxesPetitionerId) && random.Next(100) < 3)
        AddPetitioner(kAdLowerTaxesPetitionerId, kAdPetitionReplacementCommercialTaxes, 12);
    }
  } else if (!ReactivatePetitioner(kAdCommercialTaxesHighPetitionerId) && random.Next(100) < 4)
    AddPetitioner(kAdCommercialTaxesHighPetitionerId, kAdPetitionReplacementCommercialTaxes, 12);
  if (!(tax_modifiers[2] >= tax_decline_threshold)) {
    if (tax_modifiers[2] >= tax_modifiers[1] || tax_modifiers[2] >= tax_modifiers[0]) {
      if (demand[2] < 0 && !ReactivatePetitioner(kAdIndustrialTaxesVeryHighPetitionerId) && random.Next(100) < 4)
        AddPetitioner(kAdIndustrialTaxesVeryHighPetitionerId, kAdPetitionReplacementIndustrialTaxes, 12);
    } else if (!ReactivatePetitioner(kAdIndustrialTaxesHighPetitionerId) && random.Next(100) < 4)
      AddPetitioner(kAdIndustrialTaxesHighPetitionerId, kAdPetitionReplacementIndustrialTaxes, 12);
  } else if (!(tax_modifiers[2] >= tax_growth_threshold)) {
    i64 threshold = static_cast<i64>(static_cast<u64>(treasury_low) * 2u);
    if (treasury > threshold && !ReactivatePetitioner(kAdLowerIndustrialTaxesPetitionerId) && !random.Next(100))
      AddPetitioner(kAdLowerIndustrialTaxesPetitionerId, kAdPetitionReplacementIndustrialTaxes, 12);
  }
  if (tax_modifiers[0] >= tax_decline_threshold) {
    if (!(tax_modifiers[0] >= tax_growth_threshold)) {
      if (treasury <= treasury_low || ReactivatePetitioner(kAdSimsDemandTaxReliefPetitionerId)) {
        if (!ReactivatePetitioner(kAdResidentialTaxesTooHighPetitionerId) && !random.Next(100))
          AddPetitioner(kAdResidentialTaxesTooHighPetitionerId, kAdPetitionReplacementResidentialTaxes, 12);
      } else if (random.Next(100) < 2)
        AddPetitioner(kAdSimsDemandTaxReliefPetitionerId, kAdPetitionReplacementResidentialTaxes, 12);
    }
  } else if (!ReactivatePetitioner(kAdResidentialTaxesSkyrocketingPetitionerId) && random.Next(100) < 4)
    AddPetitioner(kAdResidentialTaxesSkyrocketingPetitionerId, kAdPetitionReplacementResidentialTaxes, 12);
  return true;
}

// Return the first matching record's raw income without filtering by activity or enabled state.
u32 AdBudgetAdvisor::GetBusinessDealIncomeForBuildingType(u32 type) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    if (*entry->record->TypeData() == type)
      return *entry->record->IncomeData();
  }
  return 0;
}

// Search every matching duplicate until an active byte equals exactly one.
// Other nonzero byte values remain inactive rather than being normalized to true.
bool AdBudgetAdvisor::IsBusinessDealActive(u32 type) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    if (*entry->record->TypeData() == type && *entry->record->ActiveData() == 1)
      return true;
  }
  return false;
}

// Write the actual Boolean byte to every record with this full-width type, including inactive duplicate entries.
void AdBudgetAdvisor::SetDealEnabled(u32 type, bool enabled) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    if (*entry->record->TypeData() == type)
      *entry->record->EnabledData() = enabled;
  }
}

// Clear the first matching record's activity and retained view, including an already inactive record.
// Later duplicates remain unchanged, and a missing type returns the original false result.
bool AdBudgetAdvisor::RemoveBusinessDeal(u32 type) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealRecord *record = static_cast<AdDealNode *>(node)->record;
    if (*record->TypeData() == type) {
      *record->ActiveData() = 0;
      record->SetView(nullptr);
      return true;
    }
  }
  return false;
}

// Activate the first matching deal before asking the supplied building for its retained private interface.
bool AdBudgetAdvisor::ActivateBusinessDeal(u32 type, BdBuilding *building) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealRecord *record = static_cast<AdDealNode *>(node)->record;
    if (*record->TypeData() == type) {
      *record->ActiveData() = 1;
      void *queried;
      if (building->QueryInterface(IID_BdDealView, &queried)) {
        BdDealView *view = static_cast<BdDealView *>(queried);
        record->SetView(view);
        view->Release();
      }
      return true;
    }
  }
  return false;
}

// Capture every record's view before testing exact activity, the gambling ordinance and its operational Boolean.
// Sum eligible raw income DWORDs with their original unsigned wrap, without acquiring temporary view references.
u32 AdBudgetAdvisor::GetTotalMonthlyBusinessDealIncome() {
  u32 total = 0;
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    BdDealView *view = entry->record->View();
    if (*entry->record->ActiveData() == 1 && view &&
        (*entry->record->TypeData() != 19003 || ordinance_layer->QueryOrdinanceOn(kCtOrdinance_LegalizedGambling)) &&
        view->IsOperational())
      total += *entry->record->IncomeData();
  }
  return total;
}

// Treat an exactly-active deal as already held, otherwise ask the actual planning advisor's reward list.
// Keep the first successful active match and the original full-width type argument.
bool AdBudgetAdvisor::HasBusinessDeal(u32 type) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    if (*entry->record->TypeData() == type && *entry->record->ActiveData() == 1)
      return true;
  }
  return planning->IsBusinessDealBuildingInRewardList(type);
}

// Reactivate the current offer before searching the first matching record and applying its year, enabled and reward
// gates.
void AdBudgetAdvisor::OfferBusinessDeal(u32 type) {
  u32 selected = GetPetitionerIdForReplacement(kAdPetitionReplacementBusinessDealOffer);
  if (selected)
    ReactivatePetitioner(selected);
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    if (*entry->record->TypeData() != type)
      continue;
    switch (*entry->record->TypeData()) {
    case 19001:
      if (year >= *entry->record->Value12() && *entry->record->EnabledData() && !HasBusinessDeal(19001))
        selected = 16;
      break;
    case 19002:
      if (year >= *entry->record->Value12() && *entry->record->EnabledData() && !HasBusinessDeal(19002))
        selected = 206;
      break;
    case 19003:
      if (ordinance_layer->QueryOrdinanceOn(kCtOrdinance_LegalizedGambling) && year >= *entry->record->Value12() &&
          *entry->record->EnabledData() && !HasBusinessDeal(19003))
        selected = 149;
      break;
    case 19004:
      if (year >= *entry->record->Value12() && *entry->record->EnabledData() && !HasBusinessDeal(19004))
        selected = 210;
      break;
    }
    if (selected) {
      AddPetitioner(selected, kAdPetitionReplacementBusinessDealOffer);
      AdDealRecord *record = entry->record;
      // Retire the temporary name before copying the complete token into the petitioner forwarding method.
      AdToken token = [&]() {
        ScString name("%X%");
        return AdToken(3, &name, static_cast<i32>(*record->IncomeData()));
      }();
      AddPetitionerToken(selected, token);
      ReactivatePetitioner(selected);
    }
    return;
  }
}

// Accept only the original business-deal query selector and acquire its actual query-row identity.
// Populate the rows before releasing the acquisition; the private writer's defined result is always true.
bool AdBudgetAdvisor::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  UiQueryRows *rows = nullptr;
  if (message->type != kWnMsg_QueryOpened || static_cast<u32>(message->data1) != kAdBusinessDealQuerySelector ||
      !source->QueryInterface(IID_UiQueryRows, reinterpret_cast<void **>(&rows)))
    return false;
  WriteQueryRows(rows);
  rows->Release();
  return true;
}

// Apply6383's complete active-deal query effects without asserting its ignored private Boolean-versus-byte declaration.
// Create zero-reference rows, copy the name and gated income, then let AppendRow retain each row without a caller release.
void AdBudgetAdvisor::WriteQueryRows(UiQueryRows *rows) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealNode *entry = static_cast<AdDealNode *>(node);
    if (*entry->record->ActiveData() == 1) {
      UiQueryRow *row = rows->CreateRow();
      row->SetText(kAdBusinessDealNameColumn, entry->record->Name());
      BdDealView *view = entry->record->View();
      if ((*entry->record->TypeData() != 19003 || ordinance_layer->QueryOrdinanceOn(kCtOrdinance_LegalizedGambling)) &&
          view && view->IsOperational())
        row->SetU32(kAdBusinessDealIncomeColumn, *entry->record->IncomeData());
      else
        row->SetU32(kAdBusinessDealIncomeColumn, 0);
      rows->AppendRow(row);
    }
  }
}

#if defined(_M_IX86)
static_assert(sizeof(ScRefCount) == 8);
static_assert(sizeof(AdBudgetCallback3) == 16);
static_assert(sizeof(AdBudgetCallback4) == 16);
#endif

AdBudgetCallback3::AdBudgetCallback3(AdBudgetAdvisor *input) : owner(input) {
}

AdBudgetCallback3::~AdBudgetCallback3() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdBudgetCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdBudgetCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdBudgetCallback3::Release() {
  return ScRefCount::Release();
}

bool AdBudgetCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdBudgetCallback3::IsLockingCity() {
  return false;
}

bool AdBudgetCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdBudgetCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdBudgetCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdBudgetCallback4::AdBudgetCallback4(AdBudgetAdvisor *input) : owner(input) {
}

AdBudgetCallback4::~AdBudgetCallback4() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdBudgetCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdBudgetCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdBudgetCallback4::Release() {
  return ScRefCount::Release();
}

bool AdBudgetCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdBudgetCallback4::IsLockingCity() {
  return false;
}

bool AdBudgetCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdBudgetCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdBudgetCallback4::DebugTypeTag(ScStringView *) {
  return false;
}

// Create the concrete zero-reference budget advisor and return its original secondary advisor identity.
// The director performs the interface query and any required cleanup; the factory adds no reference.
ScUnknown *AdCreateBudget() {
  return static_cast<AdAdvisor *>(new AdBudgetAdvisor);
}

// Apply6136's first matching linked building removal, skipping records whose retained deal view is absent.
// Release the queried building before the original occupant-manager call, then clear the record's retained view.
u8 AdBudgetAdvisor::RemoveDealBuilding(u32 type) {
  for (AdDealLink *node = deals->next; node != deals; node = node->next) {
    AdDealRecord *record = static_cast<AdDealNode *>(node)->record;
    if (*record->TypeData() != type || !record->View())
      continue;
    void *queried;
    if (record->View()->QueryInterface(IID_BdBuilding, &queried)) {
      BdBuilding *building = static_cast<BdBuilding *>(queried);
      building->Release();
      ScOccupantManager *manager = simulation_city->SurfaceOccupantManager();
      ScOccupant *occupant = building->AsISC3Occupant();
      manager->RemoveOccupant(occupant);
    }
    record->SetView(nullptr);
    return 1;
  }
  return 0;
}

// Apply5F27's budget petition responses through the original planning reward and linked-building operations.
// Remove the offer before accepting it, and retain the delayed follow-up for the military-base proposal.
void AdBudgetAdvisor::RespondToPetitioner(u32 id, i32 button) {
  switch (id) {
  case 16:
    RemovePetitioner(kAdFundsOfferedForDisposalPlantPetitionerId);
    if (button == 1) {
      planning->AddBusinessDealBuildingToRewardList(19001);
      AddPetitioner(kAdToxicWasteFactoryQuestionedPetitionerId, 0, 24);
    }
    break;
  case 17:
    if (button == 1)
      planning->RemoveBusinessDealBuildingFromRewardList(19001);
    break;
  case 18:
    if (button == 1)
      RemoveDealBuilding(19001);
    break;
  case 149:
    RemovePetitioner(kAdBuildCasinoPetitionerId);
    if (button == 1)
      planning->AddBusinessDealBuildingToRewardList(19003);
    break;
  case 206:
    RemovePetitioner(kAdGigamallProposedPetitionerId);
    if (button == 1)
      planning->AddBusinessDealBuildingToRewardList(19002);
    break;
  case 210:
    RemovePetitioner(kAdMaximumSecurityPrisonOfferedPetitionerId);
    if (button == 1)
      planning->AddBusinessDealBuildingToRewardList(19004);
    break;
  }
}

AdGarbageEntry::AdGarbageEntry() : key{0, 0, 0}, references(0) {
}

AdGarbageEntry::~AdGarbageEntry() {
}

bool AdGarbageEntry::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdGarbageEntry::AddRef() {
  return ++references;
}

u32 AdGarbageEntry::Release() {
  return ScReleaseImpl(this, &references);
}

// Borrow the record's first text storage without copying or retaining it.
ScStringView *AdGarbageEntry::FirstText() {
  return &first_text;
}

// Borrow the record's second text storage without copying or retaining it.
ScStringView *AdGarbageEntry::SecondText() {
  return &second_text;
}

// Borrow the record's third text storage without copying or retaining it.
ScStringView *AdGarbageEntry::ThirdText() {
  return &third_text;
}

// Borrow the record's key storage without copying or retaining it.
RsKey *AdGarbageEntry::GetKey() {
  return &key;
}

// Borrow the record's value76 storage without copying or retaining it.
u32 *AdGarbageEntry::Value76() {
  return &value76;
}

// Borrow the record's value80 storage without copying or retaining it.
u32 *AdGarbageEntry::Value80() {
  return &value80;
}

// Borrow the record's type storage without copying or retaining it.
u32 *AdGarbageEntry::TypeData() {
  return &type;
}

// Borrow the record's available storage without copying or retaining it.
u8 *AdGarbageEntry::AvailableData() {
  return &available;
}

// Resolve the real keyed occupant attribute, then drop the acquired reference before returning its cache-owned identity.
// Refused queries return null without inventing a release for an output published on failure.
GeAttrib *AdGarbageEntry::GetCachedAttrib() {
  GeAttrib *attribute = nullptr;
  if (!AdGetOccupantAttribCache()->GetOccupantAttrib(key, &attribute))
    return nullptr;
  attribute->Release();
  return attribute;
}

AdUtilityEntry::AdUtilityEntry() : key{0, 0, 0}, references(0) {
}

AdUtilityEntry::~AdUtilityEntry() {
}

bool AdUtilityEntry::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdUtilityEntry::AddRef() {
  return ++references;
}

u32 AdUtilityEntry::Release() {
  return ScReleaseImpl(this, &references);
}

// Borrow the record's first text storage without copying or retaining it.
ScStringView *AdUtilityEntry::FirstText() {
  return &first_text;
}

// Borrow the record's second text storage without copying or retaining it.
ScStringView *AdUtilityEntry::SecondText() {
  return &second_text;
}

// Borrow the record's third text storage without copying or retaining it.
ScStringView *AdUtilityEntry::ThirdText() {
  return &third_text;
}

// Borrow the record's key storage without copying or retaining it.
RsKey *AdUtilityEntry::GetKey() {
  return &key;
}

// Borrow the record's value76 storage without copying or retaining it.
u32 *AdUtilityEntry::Value76() {
  return &value76;
}

// Borrow the record's value80 storage without copying or retaining it.
u32 *AdUtilityEntry::Value80() {
  return &value80;
}

// Borrow the record's type storage without copying or retaining it.
u32 *AdUtilityEntry::TypeData() {
  return &type;
}

// Borrow the record's available storage without copying or retaining it.
u8 *AdUtilityEntry::AvailableData() {
  return &available;
}

// Resolve the real keyed occupant attribute, then drop the acquired reference before returning its cache-owned identity.
// Refused queries return null without inventing a release for an output published on failure.
GeAttrib *AdUtilityEntry::GetCachedAttrib() {
  GeAttrib *attribute = nullptr;
  if (!AdGetOccupantAttribCache()->GetOccupantAttrib(key, &attribute))
    return nullptr;
  attribute->Release();
  return attribute;
}

void AdGarbageEntry::SetFirstText(ScStringView *value) {
  first_text.FromChar(value->ToChar());
}

void AdGarbageEntry::SetSecondText(ScStringView *value) {
  second_text.FromChar(value->ToChar());
}

void AdGarbageEntry::SetThirdText(ScStringView *value) {
  third_text.FromChar(value->ToChar());
}

void AdGarbageEntry::SetKey(const RsKey *value) {
  key.type = value->type;
  key.group = value->group;
  key.instance = value->instance;
}

void AdUtilityEntry::SetFirstText(ScStringView *value) {
  first_text.FromChar(value->ToChar());
}

void AdUtilityEntry::SetSecondText(ScStringView *value) {
  second_text.FromChar(value->ToChar());
}

void AdUtilityEntry::SetThirdText(ScStringView *value) {
  third_text.FromChar(value->ToChar());
}

void AdUtilityEntry::SetKey(const RsKey *value) {
  key.type = value->type;
  key.group = value->group;
  key.instance = value->instance;
}

#if defined(_M_IX86)
static_assert(sizeof(AdRewardEntry) == 96);
#endif

AdRewardEntry::AdRewardEntry() : value64(0), value68(0), value72(0), available(0), flag77(0), flag78(0), flag79(0),
    references(0), key{0, 0, 0} {
}

AdRewardEntry::~AdRewardEntry() {
}

// Expose the private reward-record identity or Unknown and acquire one local reference on success. A refused IID clears
// the output.
bool AdRewardEntry::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, AdRewardEntry);
  HANDLE_QUERY_INTERFACE(IID_AdRewardEntry, AdRewardEntry);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdRewardEntry::AddRef() {
  return ++references;
}

u32 AdRewardEntry::Release() {
  return ScReleaseImpl(this, &references);
}

void AdRewardEntry::SetFirstText(ScStringView *value) {
  first_text.FromChar(value->ToChar());
}

// Borrow the record's first text without copying or changing its reference count.
ScStringView *AdRewardEntry::FirstText() {
  return &first_text;
}

void AdRewardEntry::SetSecondText(ScStringView *value) {
  second_text.FromChar(value->ToChar());
}

// Borrow the record's second text without copying or changing its reference count.
ScStringView *AdRewardEntry::SecondText() {
  return &second_text;
}

void AdRewardEntry::SetThirdText(ScStringView *value) {
  third_text.FromChar(value->ToChar());
}

// Borrow the record's third text without copying or changing its reference count.
ScStringView *AdRewardEntry::ThirdText() {
  return &third_text;
}

// ApplyB68B's separate key-word stores at original84,88 and92 in their original order.
void AdRewardEntry::SetKey(const RsKey *value) {
  key.type = value->type;
  key.group = value->group;
  key.instance = value->instance;
}

// Borrow the trailing resource key storage without copying or acquiring a reference.
RsKey *AdRewardEntry::GetKey() {
  return &key;
}

// Resolve the real keyed occupant attribute and drop its acquisition reference before returning the cache-owned object.
// Preserve the original nullable cache check and refused-query result.
GeAttrib *AdRewardEntry::GetCachedAttrib() {
  OcAttribCache *cache = AdGetOccupantAttribCache();
  GeAttrib *attribute;
  if (!cache || !cache->GetOccupantAttrib(key, &attribute))
    return nullptr;
  attribute->Release();
  return attribute;
}

// Borrow the first configuration DWORD at original64 for the actual parser and archive callers.
u32 *AdRewardEntry::Value64() {
  return &value64;
}

// Borrow the second configuration DWORD at original68 for the actual parser and archive callers.
u32 *AdRewardEntry::Value68() {
  return &value68;
}

// Borrow the third configuration DWORD at original72; the building ID is the separate owner-map key.
u32 *AdRewardEntry::Value72() {
  return &value72;
}

// Borrow the raw availability byte at original76; the shared code also serves unrelated DWORD getters.
u8 *AdRewardEntry::AvailableData() {
  return &available;
}

// Borrow the raw flag at original77 whose zero value is required by the public reward-list query.
u8 *AdRewardEntry::Flag77Data() {
  return &flag77;
}

// Borrow the third raw record flag at original78 without normalizing its value.
u8 *AdRewardEntry::Flag78Data() {
  return &flag78;
}

// Borrow the fourth raw record flag at original79 without normalizing its value.
u8 *AdRewardEntry::Flag79Data() {
  return &flag79;
}

#if defined(_M_IX86)
static_assert(sizeof(AdEducationBudget) == 56);
static_assert(sizeof(AdHealthBudget) == 56);
#endif

// Construct the real reference and department bases, copy the by-value name and borrow both city providers in order.
AdEducationBudget::AdEducationBudget(u32 id, ScString input) {
  department_id = id;
  name = input;
  needed = 0;
  funding = 100;
  residential = AdGetCity()->ResidentialLayer();
  budget = AdGetCity()->BudgetLayer();
}

AdEducationBudget::~AdEducationBudget() {
}

// Publish the actual department identity for Unknown or its IID, retaining through the complete reference object.
// Refusal clears the output.
bool AdEducationBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcDepartmentBudget);
  HANDLE_QUERY_INTERFACE(IID_EcDepartmentBudget, EcDepartmentBudget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdEducationBudget::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdEducationBudget::Release() {
  return ScRefCount::Release();
}

u32 AdEducationBudget::GetDepartmentID() {
  return department_id;
}

i64 AdEducationBudget::GetLastYearSurplus() {
  return 0;
}

i64 AdEducationBudget::GetTotalFundingNeeded() {
  return needed;
}

void AdEducationBudget::SetTotalFundingNeeded(i64 value) {
  needed = value;
  budget->NeededFundingChanged(static_cast<EcDepartmentBudget *>(this));
}

// Borrow the actual embedded name interface without another reference or String copy.
ScStringView *AdEducationBudget::GetDepartmentName() {
  return &name;
}

// Store the raw funding byte before updating residential funding, then notify the budget with the real department view.
void AdEducationBudget::SetFundingPercentage(u8 value) {
  funding = value;
  residential->SetEducationFundingPercentage(value);
  budget->FundingPercentageChanged(static_cast<EcDepartmentBudget *>(this));
}

u8 AdEducationBudget::GetFundingPercentage() {
  return funding;
}

u32 AdEducationBudget::GetAdvisorID() {
  return kAdDemographicsAdvisorId;
}

u32 AdEducationBudget::GetAdvisorGroupID() {
  return kAdReplacementEducationBudget;
}

// Construct the real reference and department bases, copy the by-value name and borrow both city providers in order.
AdHealthBudget::AdHealthBudget(u32 id, ScString input) {
  department_id = id;
  name = input;
  needed = 0;
  funding = 100;
  residential = AdGetCity()->ResidentialLayer();
  budget = AdGetCity()->BudgetLayer();
}

AdHealthBudget::~AdHealthBudget() {
}

// Publish the actual department identity for Unknown or its IID, retaining through the complete reference object.
// Refusal clears the output.
bool AdHealthBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcDepartmentBudget);
  HANDLE_QUERY_INTERFACE(IID_EcDepartmentBudget, EcDepartmentBudget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdHealthBudget::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdHealthBudget::Release() {
  return ScRefCount::Release();
}

u32 AdHealthBudget::GetDepartmentID() {
  return department_id;
}

i64 AdHealthBudget::GetLastYearSurplus() {
  return 0;
}

i64 AdHealthBudget::GetTotalFundingNeeded() {
  return needed;
}

void AdHealthBudget::SetTotalFundingNeeded(i64 value) {
  needed = value;
  budget->NeededFundingChanged(static_cast<EcDepartmentBudget *>(this));
}

// Borrow the actual embedded name interface without another reference or String copy.
ScStringView *AdHealthBudget::GetDepartmentName() {
  return &name;
}

// Store the raw funding byte before updating residential funding, then notify the budget with the real department view.
void AdHealthBudget::SetFundingPercentage(u8 value) {
  funding = value;
  residential->SetHealthFundingPercentage(value);
  budget->FundingPercentageChanged(static_cast<EcDepartmentBudget *>(this));
}

u8 AdHealthBudget::GetFundingPercentage() {
  return funding;
}

u32 AdHealthBudget::GetAdvisorID() {
  return kAdDemographicsAdvisorId;
}

u32 AdHealthBudget::GetAdvisorGroupID() {
  return kAdReplacementHealthcareBudget;
}

// Construct the actual shared advisor and four resource-backed names, then select the original locale portrait pair.
// Initialize tuning defaults and constructor-defined service pointers while leaving simulation data for city binding.
AdDemographicsAdvisor::AdDemographicsAdvisor() {
  simulation_city = nullptr;
  residential = nullptr;
  budget = nullptr;
  service_fc = nullptr;
  buildings = nullptr;
  aura_layer = nullptr;
  callback3 = nullptr;
  callback4 = nullptr;
  funding_variables[0] = nullptr;
  funding_variables[1] = nullptr;
  ScString name = AdTextHandle(kRsAdvisorNameHealthEducationAndAuraAdvisorText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetName(&name);
  name = AdTextHandle(kRsGameHEABriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGameHEAOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameRandallShoopText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  u32 portrait = language == kScLanguageEnglish || language == kScLanguageBritishEnglish                 ? 0
      : language == kScLanguageJapanese || (language > kScLanguagePolish && language <= kScLanguageThai) ? 2
                                                                                                         : 1;
  const RsKey small = {
      kRsImageResourceType, kRsAdvisorPortraitGroup, kAdHealthEducationAuraSmallPortraitBase + (portrait << 16)};
  const RsKey large = {
      kRsImageResourceType, kRsAdvisorPortraitGroup, kAdHealthEducationAuraLargePortraitBase + (portrait << 16)};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  college_population_minimum = 5000;
  hospital_ratio = 5000;
  hospital_population_minimum = 2000;
  library_population_minimum = 2000;
  health_funding_high = 105;
  health_funding_medium = 90;
  education_funding_high = 100;
  education_funding_medium = 85;
  residential_growth_cap = 15000;
  stadium_growth_cutoff = 50000;
  school_population_minimum = 1000;
  museum_population_minimum = 3000;
  zoo_population_minimum = 52000;
  marina_population_minimum = 80000;
  dreary_population_minimum = 10000;
  aura_low = -50;
  aura_high = 100;
  le_eq_population_minimum = 50000;
  eq_low = 70;
  eq_average = 100;
  eq_high = 120;
  le_low = 60;
  le_high = 80;
  dreary_low = 50;
  dreary_high = 75;
  outdated_response = 298;
}

AdDemographicsAdvisor::~AdDemographicsAdvisor() {
}

bool AdDemographicsAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdDemographics, AdDemographics);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdDemographicsAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdDemographicsAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdDemographicsAdvisor::GetAdvisorID() {
  return kAdDemographicsAdvisorId;
}

static bool AdReadDemographicsSetting(AdIni *reader, const char *section_name, const char *key_name, ScString *out) {
  ScString key(key_name), section(section_name);
  return reader->ReadEntry(section, key, out);
}

// Read every demographics cutoff and trigger in installed order from the loose INI with its SYS.PAK fallback.
// Missing keys retain their previous fields and make the final result false; successful byte fields preserve low eight bits.
bool AdDemographicsAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-Demographics.INI", 29);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadDemographicsSetting(&reader, "FUNDING_RATE_CUTOFFS", "HEALTH_FUNDING_HIGH", &value))
    health_funding_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "FUNDING_RATE_CUTOFFS", "HEALTH_FUNDING_MEDIUM", &value))
    health_funding_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "FUNDING_RATE_CUTOFFS", "EDUCATION_FUNDING_HIGH", &value))
    education_funding_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "FUNDING_RATE_CUTOFFS", "EDUCATION_FUNDING_MEDIUM", &value))
    education_funding_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "RESIDENTIAL_GROWTH_CAP", &value))
    residential_growth_cap = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "STADIUM_GROWTH_CUTOFF", &value))
    stadium_growth_cutoff = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "SCHOOL_POPULATION_MINIMUM", &value))
    school_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "COLLEGE_POPULATION_MINIMUM", &value))
    college_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "HOSPITAL_POPULATION_MINIMUM", &value))
    hospital_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "LIBRARY_POPULATION_MINIMUM", &value))
    library_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "MUSEUM_POPULATION_MINIMUM", &value))
    museum_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "ZOO_POPULATION_MINIMUM", &value))
    zoo_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "MARINA_POPULATION_MINIMUM", &value))
    marina_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "LE_AND_EQ_POPULATION_MINIMUM", &value))
    le_eq_population_minimum = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "LOW_EQ_CUTOFF", &value))
    eq_low = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "AVERAGE_EQ_CUTOFF", &value))
    eq_average = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "HIGH_EQ_CUTOFF", &value))
    eq_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "LOW_LIFE_EXPECTANCY", &value))
    le_low = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "HIGH_LIFE_EXPECTANCY", &value))
    le_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "CITY_AURA_DREARY_POPULATION", &value))
    dreary_population_minimum = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "LOW_DREARY_RATING", &value))
    dreary_low = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "HIGH_DREARY_RATING", &value))
    dreary_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "CITY_AURA_LOW", &value))
    aura_low = static_cast<i8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "ADVICE_TRIGGERS", "CITY_AURA_HIGH", &value))
    aura_high = static_cast<i8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadDemographicsSetting(&reader, "PETITIONER_GENERATION", "HOSPITAL_RATIO", &value))
    hospital_ratio = static_cast<u32>(atoi(value.begin));
  else
    missing = true;
  return !missing;
}

void AdDemographicsAdvisor::InitializeOrdinances() {
  for (u32 id = 4000; id <= 4005; ++id)
    AddOrdinance(id);
}

// Bind the actual retained city services, create/register both concrete funding departments and install simulation callbacks.
// Preserve partial failed bindings, temporary resource/name retirement, separate cheat acquisitions and shared initialization last.
bool AdDemographicsAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  simulation_city = input;
  input->AddRef();
  CtLayer *layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = simulation_city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&service_fc));
  layer = simulation_city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  layer = simulation_city->GetSpecificLayer(kCtAuraLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtAuraLayer, reinterpret_cast<void **>(&aura_layer));
  if (ordinances)
    InitializeOrdinances();
  unknown_174 = 0;
  unknown_170 = 0;
  unknown_178 = 100;
  funding_variables[1] = static_cast<EcDepartmentBudget *>(
      new AdEducationBudget(kEcEducationDepartmentId, ScString(AdTextHandle(150, kAdAdvisorLabelGroup).ToString())));
  funding_variables[1]->AddRef();
  funding_variables[0] = static_cast<EcDepartmentBudget *>(new AdHealthBudget(
      kEcHealthDepartmentId, ScString(AdTextHandle(kRsGamePublicHealthText, kAdAdvisorLabelGroup).ToString())));
  funding_variables[0]->AddRef();
  budget->AddDepartmentBudget(funding_variables[0]);
  budget->AddDepartmentBudget(funding_variables[1]);
  LoadConfiguration();
  callback3 = new AdDemographicsCallback3(this);
  callback4 = new AdDemographicsCallback4(this);
  CtAgentType *node = AdGetAgentTree()->GetNode(kCtAgentType_DemographicsAdvisorAgent);
  SetAgentType(node);
  node->AttachManipulator(callback3, 3);
  node->AttachManipulator(callback4, 4);
  ScMessageTarget *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->AddNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceRandallToSayCheatId, AdForceRandallToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdForceRandallToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdStopForcingAdviceCheatId);
  return AdBaseAdvisor::Init(input);
}

// Unsubscribe demographics messages and cheats before releasing the actual retained city/service identities.
// Detach the two registered funding departments before releasing their advisor references, then perform shared queue shutdown.
bool AdDemographicsAdvisor::Shutdown() {
  CtLayer *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->RemoveNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForceRandallToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdForceRandallToSayCheatId);
  ScRelease(&simulation_city);
  ScRelease(&residential);
  ScRelease(&buildings);
  ScRelease(&service_fc);
  ScRelease(&aura_layer);
  if (budget) {
    budget->RemoveDepartmentBudget(funding_variables[0]);
    budget->RemoveDepartmentBudget(funding_variables[1]);
    budget->Release();
    budget = nullptr;
  }
  if (funding_variables[0]) {
    funding_variables[0]->Release();
    funding_variables[0] = nullptr;
  }
  if (funding_variables[1]) {
    funding_variables[1]->Release();
    funding_variables[1] = nullptr;
  }
  return AdBaseAdvisor::Shutdown();
}

// Refresh demographics funding, mayor rating, residential service statistics and the six actual building counts.
// Keep original service guards, raw byte storage, unsigned strike-chance division and full signed building-count results.
void AdDemographicsAdvisor::GetSimulationData() {
  if (funding_variables[0])
    health_funding = funding_variables[0]->GetFundingPercentage();
  if (funding_variables[1])
    education_funding = funding_variables[1]->GetFundingPercentage();
  if (aura_layer)
    aura = aura_layer->GetMayorRating();
  if (residential) {
    life_expectancy = residential->GetWorkforceLE();
    education = residential->GetWorkforceEQ();
    population = static_cast<u32>(residential->GetPopulation());
    development_cap = residential->GetDevelopmentCap();
    health_rating = residential->GetHealthSystemRating();
    school_rating = residential->GetSchoolSystemRating();
    college_rating = residential->GetCollegeSystemRating();
    health_strike = static_cast<u8>(residential->HealthIsOnStrike());
    school_strike = static_cast<u8>(residential->SchoolIsOnStrike());
    college_strike = static_cast<u8>(residential->CollegeIsOnStrike());
    school_strike_chance = static_cast<u32>(residential->ChanceOfSchoolStrike()) / 100u;
    college_strike_chance = static_cast<u32>(residential->ChanceOfCollegeStrike()) / 100u;
    health_strike_chance = static_cast<u32>(residential->ChanceOfHealthStrike()) / 100u;
    patients = static_cast<u32>(residential->GetPatientCount());
    school_students = static_cast<u32>(residential->GetSchoolStudentCount());
    college_students = static_cast<u32>(residential->GetCollegeStudentCount());
    health_capacity = static_cast<u32>(residential->GetHealthSystemCapacity());
    school_capacity = static_cast<u32>(residential->GetSchoolSystemCapacity());
    college_capacity = static_cast<u32>(residential->GetCollegeSystemCapacity());
    library_capacity = static_cast<u32>(residential->GetLibrarySystemCapacity());
    museum_capacity = static_cast<u32>(residential->GetMuseumSystemCapacity());
  }
  hospitals = buildings->CheckOccupantTracking(13310);
  schools = buildings->CheckOccupantTracking(13010);
  colleges = buildings->CheckOccupantTracking(13873);
  stadiums = buildings->CheckOccupantTracking(14600);
  zoos = buildings->CheckOccupantTracking(kCtAgentZoo);
  marinas = buildings->CheckOccupantTracking(kCtAgentMarina);
}

// Apply demographics forced-advice commands and the six ordinance help mappings from actual message fields.
// Every message returns true, including unrelated types and unrecognized commands; command parsing keeps the mutable suffix.
bool AdDemographicsAdvisor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    switch (static_cast<u32>(message->data1)) {
    case kAdStopForcingAdviceCheatId:
      ClearForcedAdvice();
      break;
    case kAdForceRandallToSayCheatId: {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceRandallToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceRandallToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
      break;
    }
    }
  } else if (message->type == kCtMsg_OrdinanceAvailable) {
    switch (static_cast<u32>(message->data1)) {
    case kCtOrdinance_CommunityCPRTraining:
      AddHelp(45);
      break;
    case kCtOrdinance_FreeClinics:
      AddHelp(40);
      break;
    case kCtOrdinance_JuniorSports:
      AddHelp(41);
      break;
    case kCtOrdinance_NuclearFreeZone:
      AddHelp(42);
      break;
    case kCtOrdinance_ProReading:
      AddHelp(43);
      break;
    case kCtOrdinance_PublicSmokingBan:
      AddHelp(44);
      break;
    }
  }
  return true;
}

// Run data refresh, advice rules, queue aging/reset, petitioner checks and forced replay only for pending byte one.
// Post completion afterward, ignore all subordinate results and clear pending after the synchronous service call returns.
bool AdDemographicsAdvisor::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    GetSimulationData();
    CheckOnAdvice();
    advice->Advance();
    advice->ResetRedisplay();
    CheckPetitioners();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdDemographicsAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

// Register the actual city-agent interface before the shared initial data refresh and advice evaluation.
// The simulator's registration result does not change the base begin result.
bool AdDemographicsAdvisor::SimulationBegin() {
  CtAgent *agent = AsISC3CityAgent();
  city->CitySimulator()->AddAgent(agent);
  return AdBaseAdvisor::SimulationBegin();
}

bool AdDemographicsAdvisor::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  city->CitySimulator()->RemoveAgent(agent);
  return AdBaseAdvisor::SimulationEnd();
}

u32 AdDemographicsAdvisor::GetLayerType() {
  return kAdDemographicsAdvisorLayerType;
}

// Evaluate health, education, amenities and growth in source order using the retained simulator data.
bool AdDemographicsAdvisor::CheckOnAdvice() {
  AdRandom random(kScRandomSeedFromClock);
  if (health_strike)
    AddAdvice(kAdHealthcareWorkersOnStrikeAdviceId, kAdReplacementHealthcareBudget);
  else if (hospitals <= 0)
    AddAdvice(population <= hospital_population_minimum ? kAdTheHealthCareBudgetAdviceId : 0xbd,
        kAdReplacementHealthcareBudget);
  else if (health_strike_chance <= 15)
    if (patients <= ((3u * health_capacity) >> 1) || health_funding <= 99)
      AddAdvice(health_rating == 4 ? kAdHealthcareBudgetFeelsPerkyAdviceId
              : health_rating == 3 ? kAdHealthBudgetTemperature986AdviceId
                                   : kAdHealthcareBudgetSicklyAdviceId,
          kAdReplacementHealthcareBudget);
    else
      AddAdvice(kAdHealthcareSuffersDespiteHighBudgetAdviceId, kAdReplacementHealthcareBudget);
  else
    AddAdvice(kAdHealthcareStrikeImminentAdviceId, kAdReplacementHealthcareBudget);
  u32 education_advice;
  if (school_strike)
    education_advice = 0x6a;
  else if (college_strike)
    education_advice = 0x104;
  else if (schools > 0 && school_strike_chance > 15)
    education_advice = 0x6b;
  else if (colleges > 0 && college_strike_chance > 15)
    education_advice = 0x13a;
  else if (schools > 0 && school_students > ((3u * school_capacity) >> 1) && education_funding > 95)
    education_advice = 0x135;
  else if (schools > 0 && school_capacity > school_students && education_funding < 85)
    education_advice = 0x134;
  else if (colleges > 0 && college_students > ((3u * college_capacity) >> 1) && education_funding > 95)
    education_advice = 0x139;
  else if (colleges > 0 && college_capacity > college_students && education_funding < 85)
    education_advice = 0x138;
  else if (schools > 0 && school_rating == 3)
    education_advice = 0x6c;
  else if (colleges > 0 && college_rating == 3)
    education_advice = 0x137;
  else if (schools > 0 && school_rating == 4)
    education_advice = 0x6d;
  else if (colleges > 0 && college_rating == 4)
    education_advice = 0x136;
  else if (schools > 0)
    education_advice = 0x6b;
  else
    education_advice = population > school_population_minimum || colleges > 0 ? 0x6e : 0x126;
  AddAdvice(education_advice, kAdReplacementEducationBudget);
  if (population > school_population_minimum && school_students > school_capacity && school_rating != 4)
    AddAdvice(kAdSchoolsAndCollegesNeededAdviceId, 0);
  if (population > library_population_minimum && population > library_capacity)
    AddAdvice(kAdReadMyMindGetALibraryAdviceId, 0);
  if (population > museum_population_minimum && population > museum_capacity)
    AddAdvice(kAdStimulationStarvedSimsWantMuseumAdviceId, 0);
  if (population > hospital_population_minimum && patients > health_capacity && hospitals > 0)
    AddAdvice(0xb8, 0);
  u32 replacement = 0;
  if (population > le_eq_population_minimum) {
    if (life_expectancy < le_low && health_rating != 4 && health_rating != 3)
      AddAdvice(kAdTooMuchCoughingGoingOnAdviceId, kAdReplacementCityHealth);
    else if (life_expectancy > le_high)
      AddAdvice(kAdSimsBlushingWithGoodHealthAdviceId, kAdReplacementCityHealth);
    replacement = kAdReplacementEducationAndRecreation;
    if (education < eq_low && school_rating != 4 && school_rating != 3) {
      if (!ReactivateAdvice(kAdManySimsCanTReadAdviceId) && !ReactivateAdvice(kAdSimsCanTWriteRightAdviceId)) {
        u32 selected = random.Next(2);
        if (!selected)
          AddAdvice(kAdManySimsCanTReadAdviceId, replacement);
        else if (selected == 1)
          AddAdvice(kAdSimsCanTWriteRightAdviceId, replacement);
      }
    } else
      AddAdvice(education >= eq_high    ? kAdSimsMayBeSmarterThanYouAreAdviceId
              : education >= eq_average ? kAdSimsCouldBeSmarterAdviceId
                                        : kAdSimStudentsOnlyAverageAdviceId,
          replacement);
  }
  if (population <= stadium_growth_cutoff || stadiums) {
    if (population > residential_growth_cap && development_cap > dreary_high)
      AddAdvice(kAdRecreationalFacilitiesNeededAdviceId, replacement);
  } else
    AddAdvice(kAdStadiumWantedAdviceId, replacement);
  return true;
}

// Evaluate strikes, hospital/school capacity, aura and missing amenities in original order over the actual petitioner queue.
// Preserve wrapped school ratios, signed building counts and the distinct replaceable positive-aura petitioner.
bool AdDemographicsAdvisor::CheckPetitioners() {
  AdRandom random(kScRandomSeedFromClock);
  if (health_strike)
    AddPetitioner(kAdHealthcareWorkersOnStrikePetitionerId, kAdHealthcareStrikePetitionReplacement, 6);
  else if (health_strike_chance > 15)
    AddPetitioner(kAdHealthcareWorkersDemandRaisePetitionerId, kAdHealthcareStrikePetitionReplacement, 6);
  if (school_strike)
    AddPetitioner(kAdTeachersStrikeContinuesPetitionerId, kAdTeacherStrikePetitionReplacement, 6);
  else if (school_strike_chance > 15)
    AddPetitioner(kAdTeachersAreUnderpaidPetitionerId, kAdTeacherStrikePetitionReplacement, 6);
  if (population > 200) {
    if (hospitals > 0 && health_rating == 4 && !ReactivatePetitioner(kAdHospitalsPlentifulPetitionerId)) {
      if (random.Next(100) < 2)
        AddPetitioner(kAdHospitalsPlentifulPetitionerId, kAdHospitalAvailabilityPetitionReplacement, 40);
    } else if (hospitals == 0 && population > hospital_ratio && !ReactivatePetitioner(kAdNoHospitalsPetitionerId)) {
      if (random.Next(100) < 10)
        AddPetitioner(kAdNoHospitalsPetitionerId, kAdHospitalAvailabilityPetitionReplacement, 12);
    } else if (hospitals > 1 && patients > health_capacity && health_rating < 3 &&
        !ReactivatePetitioner(kAdNeedsMoreHospitalsPetitionerId) && random.Next(100) < 5)
      AddPetitioner(kAdNeedsMoreHospitalsPetitionerId, kAdHospitalAvailabilityPetitionReplacement, 12);
  }
  if (population > school_population_minimum) {
    if (schools == 0 && !ReactivatePetitioner(kAdSimsCanTSpellPetitionerId) && random.Next(100) < 10)
      AddPetitioner(kAdSimsCanTSpellPetitionerId, 0, 12);
    if (school_students > school_capacity && school_rating != 4 && school_rating != 3 &&
        !ReactivatePetitioner(kAdCityNeedsMoreSchoolsPetitionerId) && random.Next(100) < 10)
      AddPetitioner(kAdCityNeedsMoreSchoolsPetitionerId, 0, 12);
    if (school_capacity > school_students && 7u * school_capacity < 10u * school_students &&
        (school_rating == 4 || school_rating == 3) && !ReactivatePetitioner(kAdPlentyOfSchoolsPetitionerId) &&
        random.Next(100) < 10)
      AddPetitioner(kAdPlentyOfSchoolsPetitionerId, 0, 120);
  }
  if (population > 500) {
    if (aura < aura_low)
      AddPetitioner(development_cap > dreary_high ? kAdNoHappinessPetitionerId : kAdCityAuraLowPetitionerId, 0,
          development_cap > dreary_high ? 18 : 24);
    else if (development_cap > 70 && population > dreary_population_minimum)
      AddPetitioner(kAdCityAuraLowPetitionerId, 0, 24);
    else if (aura > aura_high && development_cap < dreary_low &&
        !ContainsActivePetitioner(kAdHolidayToHonorMayorPetitionerId) && random.Next(100) < 4)
      AddReplaceablePetitioner(kAdHolidayToHonorMayorPetitionerId, 0, 24);
  }
  if (zoos == 0 && population > zoo_population_minimum && !ReactivatePetitioner(kAdSimsWantAZooPetitionerId) &&
      random.Next(100) < 2)
    AddPetitioner(kAdSimsWantAZooPetitionerId, 0, 60);
  if (marinas == 0 && population > marina_population_minimum && !ReactivatePetitioner(kAdMarinaNeededPetitionerId) &&
      random.Next(100) < 2)
    AddPetitioner(kAdMarinaNeededPetitionerId, 0, 60);
  return true;
}

#if defined(_M_IX86)
static_assert(sizeof(ScRefCount) == 8);
static_assert(sizeof(AdDemographicsCallback3) == 16);
static_assert(sizeof(AdDemographicsCallback4) == 16);
#endif

AdDemographicsCallback3::AdDemographicsCallback3(AdDemographicsAdvisor *input) : owner(input) {
}

AdDemographicsCallback3::~AdDemographicsCallback3() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdDemographicsCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdDemographicsCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdDemographicsCallback3::Release() {
  return ScRefCount::Release();
}

bool AdDemographicsCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdDemographicsCallback3::IsLockingCity() {
  return false;
}

bool AdDemographicsCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdDemographicsCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdDemographicsCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdDemographicsCallback4::AdDemographicsCallback4(AdDemographicsAdvisor *input) : owner(input) {
}

AdDemographicsCallback4::~AdDemographicsCallback4() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdDemographicsCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdDemographicsCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdDemographicsCallback4::Release() {
  return ScRefCount::Release();
}

bool AdDemographicsCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdDemographicsCallback4::IsLockingCity() {
  return false;
}

bool AdDemographicsCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdDemographicsCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdDemographicsCallback4::DebugTypeTag(ScStringView *) {
  return false;
}

// Create the concrete zero-reference demographics advisor and return its original secondary advisor identity.
// Preserve the original allocation and constructor ordering without an extra retain or interface query.
ScUnknown *AdCreateDemographics() {
  return static_cast<AdAdvisor *>(new AdDemographicsAdvisor);
}

// Apply the demographics response effects after an accepted education or health funding petition.
// Reload the selected department after its getter, then raise funding below110 to exactly110.
void AdDemographicsAdvisor::RespondToPetitioner(u32 id, u32 button) {
  if (button != 1 || id < 155 || id > 158)
    return;
  u32 index = id <= 156 ? 0 : 1;
  if (funding_variables[index] && funding_variables[index]->GetFundingPercentage() < 110)
    funding_variables[index]->SetFundingPercentage(110);
}

bool AdBudgetAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Init(input, segment);
}

bool AdBudgetAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Save(input, segment);
}

bool AdDemographicsAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Init(input, segment);
}

bool AdDemographicsAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Save(input, segment);
}

bool AdEnvironmentAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Init(input, segment);
}

bool AdEnvironmentAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Save(input, segment);
}

#if defined(_M_IX86)
static_assert(sizeof(AdEnvironmentAdvisor) == 408);
#endif

// Construct the real advisor bases and garbage-entry list, then load four names and the locale-selected portrait pair.
// Clear only the original service pointers and initialized counters; averages and other sampled state await city binding.
AdEnvironmentAdvisor::AdEnvironmentAdvisor() {
  AdGarbageNode *sentinel = new AdGarbageNode;
  sentinel->next = sentinel;
  sentinel->previous = sentinel;
  garbage_entries = sentinel;
  simulation_city = nullptr;
  pollution = nullptr;
  service_16c = nullptr;
  service_170 = nullptr;
  budget = nullptr;
  service_178 = nullptr;
  neighbors = nullptr;
  ordinances = nullptr;
  buildings = nullptr;
  residential = nullptr;
  utilities = nullptr;
  callback3 = nullptr;
  callback4 = nullptr;
  ScString name = AdTextHandle(kRsAdvisorNameEnvironmentAdvisorText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetName(&name);
  name = AdTextHandle(kRsGameEnvironmentalBriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGameEnvironmentalOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameKarenFrawlText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  bool alternate = language == kScLanguageJapanese || (language > kScLanguagePolish && language <= kScLanguageThai);
  const RsKey small = {kRsImageResourceType, kRsAdvisorPortraitGroup,
      alternate ? kAdEnvironmentAlternateSmallPortrait : kAdEnvironmentSmallPortraitBase};
  const RsKey large = {kRsImageResourceType, kRsAdvisorPortraitGroup,
      alternate ? kAdEnvironmentAlternateLargePortrait : kAdEnvironmentLargePortraitBase};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  monthly_input = 0;
  monthly_disposed = 0;
  water_high = 66;
  air_high = 66;
  overall_high = 66;
  water_medium = 33;
  air_medium = 33;
  overall_medium = 33;
  produced = 0;
  imported = 0;
  exported = 0;
  recycled = 0;
  incinerated = 0;
  landfilled = 0;
  months_uncollected = 0;
  garbage_deal_4 = 0;
  garbage_deal_5 = 0;
  flag326 = 0;
  ordinance_flag = 0;
  flag328 = 0;
  building_available = 0;
  building_12851 = 0;
  building_12820 = 0;
  garbage_uncollected = 0;
  available_landfill = 0;
  monthly_facility_capacity = 0;
  projected_capacity = 0;
  outdated_response = 297;
}

// Retire circular garbage-list storage without releasing its borrowed entries, then destroy the actual advisor base.
// Normal service/entry ownership cleanup belongs to the separate domain Shutdown method.
AdEnvironmentAdvisor::~AdEnvironmentAdvisor() {
  AdGarbageLink *node = garbage_entries->next;
  while (node != garbage_entries) {
    AdGarbageLink *next = node->next;
    delete static_cast<AdGarbageNode *>(node);
    node = next;
  }
  delete static_cast<AdGarbageNode *>(garbage_entries);
}

bool AdEnvironmentAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdEnvironment, AdEnvironment);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdEnvironmentAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdEnvironmentAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdEnvironmentAdvisor::GetAdvisorID() {
  return kAdEnvironmentAdvisorId;
}

u32 AdEnvironmentAdvisor::GetLayerType() {
  return kAdEnvironmentAdvisorLayerType;
}

// The builder deliberately keeps its first, otherwise unused field alive until all subsequent service calls finish.
static ScString AdTakeBuildingField(ScString *input) {
  u32 length = static_cast<u32>(input->end - input->begin), position = 0;
  while (position < length && input->begin[position] != ';')
    ++position;
  if (position == length)
    return ScString();
  ScString prefix(input->begin, position);
  input->ScString::Erase(0, position + 1);
  return ScString(prefix);
}

static ScString AdResolveBuildingName(u32 instance, u32 group) {
  return AdTextHandle(instance, group).ToString();
}

static void AdFormatBuildingCapacity(ScString *out, const char *format, const char *value) {
  out->ScString::Sprintf(format, value);
}

static bool AdReadEnvironmentCutoff(AdIni *reader, const char *name, ScString *out) {
  ScString key(name), section("POLLUTION_RATE_CUTOFFS");
  return reader->ReadEntry(section, key, out);
}

static void AdEnvironmentIniEntry(const ScString &key, const ScString &value, void *identity) {
  AdEnvironmentAdvisor *owner = static_cast<AdEnvironmentAdvisor *>(static_cast<AdEnvironment *>(identity));
  owner->ConfigureBuilding(0, &key, &value);
}

// Load six cutoffs from the actual loose/package INI reader, truncating each successful atoi result to one byte.
// Missing scalar keys retain their fields and make the result false; garbage enumeration still runs afterward.
bool AdEnvironmentAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-Environment.INI", 28);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadEnvironmentCutoff(&reader, "WATER_POLLUTION_HIGH", &value))
    water_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadEnvironmentCutoff(&reader, "WATER_POLLUTION_MEDIUM", &value))
    water_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadEnvironmentCutoff(&reader, "AIR_POLLUTION_HIGH", &value))
    air_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadEnvironmentCutoff(&reader, "AIR_POLLUTION_MEDIUM", &value))
    air_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadEnvironmentCutoff(&reader, "OVERALL_POLLUTION_HIGH", &value))
    overall_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadEnvironmentCutoff(&reader, "OVERALL_POLLUTION_MEDIUM", &value))
    overall_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  ScString section("GARBAGE_BUILDINGS");
  reader.EnumEntries(section, AdEnvironmentIniEntry, static_cast<AdEnvironment *>(this));
  return !missing;
}

void AdEnvironmentAdvisor::ConfigureBuilding(u32 selector, const ScString *key, const ScString *value) {
  GeBuildingType *attribute = nullptr;
  ScString remaining(*value);
  RsKey name_key = {0, 0, 0};
  ScString unused = AdTakeBuildingField(&remaining), field = AdTakeBuildingField(&remaining);
  u32 type = AdParseIniNumber(const_cast<char *>(key->ToChar()));
  u32 first = AdParseIniNumber(field.begin), second = AdParseIniNumber(remaining.begin);
  if (!type || !AdGetOccupantAttribCache()->GetBuildingAttrib(type, &attribute))
    return;
  AdGarbageEntry *entry = new AdGarbageEntry;
  entry->AddRef();
  entry->SetKey(attribute->Key());
  *entry->Value76() = first;
  *entry->Value80() = second;
  *entry->TypeData() = type;
  *entry->AvailableData() = static_cast<u8>(first == 0);
  entry->GetCachedAttrib()->GetNameKey(&name_key);
  ScString name = AdResolveBuildingName(name_key.instance, name_key.group);
  entry->SetThirdText(&name);
  u32 cost = static_cast<u32>(budget->GetCost(type));
  if (cost) {
    AdTextHandle resource(kRsGameCostText, kAdAdvisorLabelGroup);
    ScString description;
    {
      ScString expanded = resource.ToExpandedString();
      description = expanded;
    }
    AdGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(static_cast<i64>(cost), &field);
    description.ScString::Append(field.begin, static_cast<u32>(field.end - field.begin));
    entry->SetFirstText(&description);
  }
  i32 capacity = pollution->GetGarbageBuildingCapacity(type);
  volatile f64 product = static_cast<f64>(pollution->GetGarbageScalingFactor()) * capacity;
  i32 scaled = static_cast<i32>(static_cast<i64>(product));
  ScString format = AdResolveBuildingName(kRsBuildingCatalogCapacityTonsFormatMonthText, kRsBuildingCatalogTextGroup);
  char number[12];
  int length = snprintf(number, sizeof(number), "%i", scaled);
  field.ScString::FromChar(number, static_cast<u32>(length));
  AdFormatBuildingCapacity(&remaining, format.begin, field.begin);
  entry->SetSecondText(&remaining);
  if (!selector) {
    AdGarbageNode *node = new AdGarbageNode;
    node->next = garbage_entries;
    node->previous = garbage_entries->previous;
    node->entry = entry;
    node->previous->next = node;
    garbage_entries->previous = node;
  }
  attribute->Release();
}

// Bind the actual city layers in source order, then load configuration and scale unsigned pollution cutoffs.
// Register the real phase-three/four manipulators, ordinance notifications and cheats before shared city initialization.
bool AdEnvironmentAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  simulation_city = ScAddRefNotNull(input);
  CtLayer *layer = simulation_city->GetSpecificLayer(kEcPollutionLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcLayer, reinterpret_cast<void **>(&pollution));
  layer = simulation_city->GetSpecificLayer(kCtPlumbingSimulationLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtPlumbingLayer, reinterpret_cast<void **>(&service_16c));
  layer = simulation_city->GetSpecificLayer(kCtNeighborsLayerType);
  if (!layer) {
    simulation_city->Release();
    simulation_city = nullptr;
    assert(neighbors);
    neighbors->Release();
    neighbors = nullptr;
    return false;
  }
  layer->QueryInterface(IID_CtNeighborsLayer, reinterpret_cast<void **>(&neighbors));
  service_170 = nullptr;
  layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = simulation_city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&service_178));
  layer = simulation_city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances));
  layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = simulation_city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  utilities = simulation_city->GetUtilityAdvisor();
  if (AdBaseAdvisor::ordinances)
    for (u32 id = 2000; id <= 2007; ++id)
      AddOrdinance(id);
  unknown15c = 0;
  unknown158 = 0;
  city_state = 100;
  LoadConfiguration();
  water_high = water_high * static_cast<u32>(pollution->GetMaxExpectedWaterValue()) / 100u;
  water_medium = water_medium * static_cast<u32>(pollution->GetMaxExpectedWaterValue()) / 100u;
  air_high = air_high * static_cast<u32>(pollution->GetMaxExpectedAirValue()) / 100u;
  air_medium = air_medium * static_cast<u32>(pollution->GetMaxExpectedAirValue()) / 100u;
  overall_high = 65534u * overall_high / 100u;
  overall_medium = 65534u * overall_medium / 100u;
  callback3 = new AdEnvironmentCallback3(this);
  callback4 = new AdEnvironmentCallback4(this);
  CtAgentType *type = AdGetAgentTree()->GetNode(kCtAgentType_EnvironmentAdvisorAgent);
  SetAgentType(type);
  type->AttachManipulator(callback3, 3);
  type->AttachManipulator(callback4, 4);
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  AdGetMessageServer()->AddNotification(target, kCtMsg_OrdinanceAvailable);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdGarbageInGarbageOutCheatId, AdGarbageInGarbageOut);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdGarbageInGarbageOutCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceKarenToSayCheatId, AdForceKarenToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdForceKarenToSayCheatId);
  return AdBaseAdvisor::Init(input);
}

// Unsubscribe the real layer identity and three cheats before releasing the retained city/domain services in source order.
// Release every garbage-entry payload before erasing its nodes, retain the sentinel, then perform shared advisor shutdown.
bool AdEnvironmentAdvisor::Shutdown() {
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  AdGetMessageServer()->RemoveNotification(target, kCtMsg_OrdinanceAvailable);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdGarbageInGarbageOutCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, kAdGarbageInGarbageOutCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForceKarenToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(target, kAdForceKarenToSayCheatId);
  ScRelease(&simulation_city);
  ScRelease(&pollution);
  ScRelease(&service_16c);
  ScRelease(&budget);
  ScRelease(&service_178);
  ScRelease(&neighbors);
  ScRelease(&buildings);
  ScRelease(&ordinances);
  ScRelease(&residential);
  for (AdGarbageLink *node = garbage_entries->next; node != garbage_entries; node = node->next)
    static_cast<AdGarbageNode *>(node)->entry->Release();
  while (garbage_entries->next != garbage_entries) {
    AdGarbageLink *node = garbage_entries->next;
    garbage_entries->next = node->next;
    node->next->previous = garbage_entries;
    delete static_cast<AdGarbageNode *>(node);
  }
  return AdBaseAdvisor::Shutdown();
}

// Sample real city, pollution, landfill, neighbor-deal, utility, building-count and ordinance state in source order.
// Preserve raw counter arithmetic, the game's53-bit rounding context and the signed uncollected-month clamp.
void AdEnvironmentAdvisor::GetSimulationData() {
  f32 decay_rate = 0.0f;
  if (simulation_city)
    simulation_city->GetDate(&day_of_year, &year);
  if (residential)
    population = static_cast<u32>(residential->GetPopulation());
  if (pollution) {
    average_air = pollution->GetAverageAirValue();
    average_water = pollution->GetAverageWaterValue();
    average_garbage = pollution->GetAverageGarbageValue();
    garbage_uncollected = static_cast<u8>(pollution->IsGarbageUncollected());
    produced = pollution->GetTotalGarbageProducedLastMonth();
    imported = pollution->GetTotalGarbageImportedLastMonth();
    exported = pollution->GetTotalGarbageExportedLastMonth();
    recycled = pollution->GetTotalGarbageRecycledLastMonth();
    incinerated = pollution->GetTotalGarbageIncineratedLastMonth();
    landfilled = pollution->GetTotalGarbageLandfilledLastMonth();
    available_landfill = pollution->GetAvailableLandfillCapacity();
    u32 total_landfill = static_cast<u32>(pollution->GetTotalLandfillCapacity());
    CtZoneLayer *zone = simulation_city->ZoneLayer();
    if (zone) {
      CtZoneDeveloper *developer = zone->GetZoneDeveloper(17);
      CtLandfillDecay *decay;
      if (developer && developer->QueryInterface(IID_CtLandfillDecay, reinterpret_cast<void **>(&decay))) {
        decay_rate = decay->GetDecayRate();
        developer->Release();
      }
    }
    EcLayer *capacity_source = pollution;
    u32 waste_to_energy = static_cast<u32>(capacity_source->GetDailyWasteToEnergyCapacity());
    u32 incinerator = static_cast<u32>(capacity_source->GetDailyIncineratorCapacity());
    monthly_facility_capacity = static_cast<i32>(30u * (incinerator + waste_to_energy));
    u32 available = static_cast<u32>(available_landfill);
    if (total_landfill < available)
      total_landfill = available;
    i64 used_landfill = total_landfill - available;
    volatile f64 product = static_cast<f64>(used_landfill) * decay_rate;
    f64 rounded = product + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &rounded, sizeof(bits));
    projected_capacity = static_cast<u32>(recycled) + static_cast<u32>(monthly_facility_capacity) + available / 12u +
        static_cast<u32>(bits) + kScDoubleIntegerLowWordBias;
    monthly_input = static_cast<i64>(static_cast<u64>(imported) + static_cast<u64>(produced));
    monthly_disposed = static_cast<i64>(static_cast<u64>(landfilled) + static_cast<u64>(exported) +
        static_cast<u64>(recycled) + static_cast<u64>(incinerated));
  }
  if (neighbors) {
    garbage_deal_4 = static_cast<u8>(neighbors->HasDealType(kCtDealExportGarbage));
    garbage_deal_5 = static_cast<u8>(neighbors->HasDealType(kCtDealImportGarbage));
  }
  building_available = static_cast<u8>(utilities->CheckWaterBuildingAvailibility(12820));
  building_12851 = buildings->CheckOccupantTracking(kCtAgentRecycling);
  building_12820 = buildings->CheckOccupantTracking(kCtAgentWaterTreatment);
  building_12330 = buildings->CheckOccupantTracking(kCtAgentSeaport);
  if (ordinances)
    ordinance_flag = static_cast<u8>(ordinances->QueryOrdinanceOn(kCtOrdinance_CleanAir));
  months_uncollected = static_cast<i32>(static_cast<u32>(months_uncollected) + (garbage_uncollected ? 1u : UINT32_MAX));
  if (months_uncollected < 0)
    months_uncollected = 0;
}

static AdToken AdEnvironmentDealToken(CtDeal *deal) {
  ScString name("%NEIGHBOR%");
  u32 neighbor = deal->GetNeighborId();
  return AdToken(4, &name, neighbor);
}

// Select water, air, garbage capacity and neighbor-deal advice from the actual sampled environment state.
bool AdEnvironmentAdvisor::CheckOnAdvice() {
  AdRandom random(kScRandomSeedFromClock);
  if (population < 100)
    return true;
  if (building_available) {
    u32 water = static_cast<u32>(average_water);
    if (water > water_high)
      AddAdvice(kAdWaterSupplyVeryPollutedAdviceId, kAdReplacementWaterPollution);
    else if (water > water_medium)
      AddAdvice(kAdWaterSystemPollutedAdviceId, kAdReplacementWaterPollution);
    else if (advice->Contains(kAdWaterSupplyVeryPollutedAdviceId) || advice->Contains(kAdWaterSystemPollutedAdviceId))
      AddReplaceableAdvice(kAdWaterPollutionProblemSolvedAdviceId, kAdReplacementWaterPollution, 6);
  }
  u32 air = static_cast<u32>(average_air);
  if (air > air_high)
    AddAdvice(kAdAirPollutionHighAdviceId, kAdReplacementAirPollution);
  else if (air > air_medium)
    AddAdvice(kAdAirPollutionModerateAdviceId, kAdReplacementAirPollution);
  else if (advice->Contains(kAdAirPollutionHighAdviceId) || advice->Contains(kAdAirPollutionModerateAdviceId))
    AddReplaceableAdvice(kAdAirPollutionLowAdviceId, kAdReplacementAirPollution, 6);
  u32 replacement = kAdReplacementGarbageCollectionCapacity;
  if (!garbage_deal_4) {
    u32 garbage_advice;
    if (garbage_uncollected) {
      if (year < 1920)
        garbage_advice = 118;
      else if (year <= 1969)
        garbage_advice = 239;
      else if (year <= 1982)
        garbage_advice = 240;
      else
        garbage_advice = CheckGarbageBuildingAvailibility(12853) ? 242 : 241;
    } else if (monthly_input <= static_cast<i64>(projected_capacity))
      garbage_advice = 120;
    else
      garbage_advice = CheckGarbageBuildingAvailibility(12851) ? 119 : 248;
    AddAdvice(garbage_advice, replacement);
    if (garbage_deal_5) {
      if (garbage_uncollected) {
        CtDeal *deal;
        if (neighbors->GetDealObject(kCtDealImportGarbage, &deal)) {
          ScMessage begin = {kApMsgLockNotificationQueue, 0, 0, 0};
          AdGetMessageServer()->MessageSend(begin);
          AddAdvice(kAdDealExacerbatesTrashProblemAdviceId, kAdReplacementGarbageImport);
          AdToken token = AdEnvironmentDealToken(deal);
          AddAdviceToken(kAdDealExacerbatesTrashProblemAdviceId, token);
          RecreateAdviceNotification(kAdDealExacerbatesTrashProblemAdviceId);
          ScMessage end = {kApMsgUnlockNotificationQueue, 0, 0, 0};
          AdGetMessageServer()->MessageSend(end);
          // The installed rule leaves this acquired deal reference retained after constructing its display token.
        }
      } else if (monthly_input > static_cast<i64>(projected_capacity))
        AddAdvice(kAdGarbageImportDealQuestionedAdviceId, kAdReplacementGarbageImport);
    }
    replacement = kAdReplacementGarbageExportAccess;
    if (garbage_uncollected) {
      if (months_uncollected <= 6)
        return true;
      u32 count = neighbors->CountNeighbors();
      if (count < 4 && building_12330 <= 0)
        AddAdvice(kAdNeedSeaportToShipTrashAdviceId, replacement);
      else if (!neighbors->CountNeighborsWithDeals() && count)
        AddAdvice(kAdNeedConnectionToExportTrashAdviceId, replacement);
    }
  }
  if (months_uncollected > 6 && !garbage_uncollected &&
      (garbage_deal_4 ||
          static_cast<i64>(7u * projected_capacity) > static_cast<i64>(10u * static_cast<u64>(monthly_input))))
    AddAdvice(kAdGarbageProblemSolvedAdviceId, replacement, 60);
  return true;
}

// Consume environment cheat and ordinance-change messages, retaining the mutable command suffix and full forced ID.
bool AdEnvironmentAdvisor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    switch (static_cast<u32>(message->data1)) {
    case kAdForceKarenToSayCheatId: {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceKarenToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceKarenToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
      break;
    }
    case kAdStopForcingAdviceCheatId:
      ClearForcedAdvice();
      break;
    case kAdGarbageInGarbageOutCheatId:
      AdGetApplication()->GetCheatCodeManager()->SetCheatHasBeenSet(kAdGarbageInGarbageOutCheatId, true);
      break;
    }
    return true;
  }
  if (message->type == kCtMsg_OrdinanceAvailable) {
    switch (static_cast<u32>(message->data1)) {
    case kCtOrdinance_CleanAir:
      AddHelp(21);
      break;
    case kCtOrdinance_IndustrialWasteDisposal:
      AddHelp(11);
      break;
    case kCtOrdinance_CarSmogging:
      AddHelp(20);
      break;
    case kCtOrdinance_TireRecycling:
      AddHelp(28);
      break;
    case kCtOrdinance_LawnChemicalBan:
      AddHelp(24);
      break;
    case kCtOrdinance_LeafBurningBan:
      AddHelp(22);
      break;
    case kCtOrdinance_LandfillGasRecovery:
      AddHelp(25);
      break;
    case kCtOrdinance_BackyardComposting:
      AddHelp(26);
      break;
    case kCtOrdinance_PaperReduction:
      AddHelp(27);
      break;
    case kCtOrdinance_TrashPresort:
      AddHelp(23);
      break;
    }
    return true;
  }
  return false;
}

// Existing enabled entries stay enabled; this native phase does not assert the private source return declaration.
void AdEnvironmentAdvisor::EnableAvailableBuildings() {
  u32 day, current_year;
  simulation_city->GetDate(&day, &current_year);
  for (AdGarbageLink *node = garbage_entries->next; node != garbage_entries; node = node->next) {
    AdGarbageEntry *entry = static_cast<AdGarbageNode *>(node)->entry;
    if (*entry->Value76() <= current_year)
      *entry->AvailableData() = 1;
  }
}

void AdEnvironmentAdvisor::AnnounceAvailableBuilding() {
  u32 day, current_year;
  simulation_city->GetDate(&day, &current_year);
  AdRandom random(kScRandomSeedFromClock);
  if (random.Next(100) >= 10)
    return;
  for (AdGarbageLink *node = garbage_entries->next; node != garbage_entries; node = node->next) {
    AdGarbageEntry *entry = static_cast<AdGarbageNode *>(node)->entry;
    if (!*entry->AvailableData() && *entry->Value76() <= current_year) {
      *entry->AvailableData() = 1;
      AddCategoryOneAdvice(*entry->Value80(), 0, 24);
      return;
    }
  }
}

// The real configuration and invention producers write only zero or one; absent types return false.
bool AdEnvironmentAdvisor::CheckGarbageBuildingAvailibility(u32 type) {
  for (AdGarbageLink *node = garbage_entries->next; node != garbage_entries; node = node->next) {
    AdGarbageEntry *entry = static_cast<AdGarbageNode *>(node)->entry;
    if (*entry->TypeData() == type)
      return *entry->AvailableData() != 0;
  }
  return false;
}

// Enable date-eligible garbage buildings before registering the real city-agent identity with the city simulator.
// Then run the shared initial data refresh and advice evaluation, preserving its return value.
bool AdEnvironmentAdvisor::SimulationBegin() {
  EnableAvailableBuildings();
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->AddAgent(agent);
  return AdBaseAdvisor::SimulationBegin();
}

// Remove the same city-agent identity before returning the shared simulation-end result.
// The installed entry receives the layer subobject at complete+20 despite its old inferred agent type.
bool AdEnvironmentAdvisor::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->RemoveAgent(agent);
  return AdBaseAdvisor::SimulationEnd();
}

// Run the environment refresh, rules, invention announcement, queue aging, petitions and forced advice in source order.
// Post completion only for pending byte one, and clear pending after the synchronous service call returns.
bool AdEnvironmentAdvisor::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    GetSimulationData();
    CheckOnAdvice();
    AnnounceAvailableBuilding();
    advice->Advance();
    advice->ResetRedisplay();
    CheckPetitioners();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdEnvironmentAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

void AdEnvironmentAdvisor::CheckPetitioners() {
  AdRandom random(kScRandomSeedFromClock);
  if (population < 100)
    return;
  if (static_cast<u32>(average_air) > air_high) {
    if (ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_CleanAir) &&
        !ReactivatePetitioner(kAdAirPollutionLevelsHighPetitionerId) && !random.Next(100))
      AddPetitioner(kAdAirPollutionLevelsHighPetitionerId, 0, 120);
    if (ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_CarSmogging) &&
        !ReactivatePetitioner(kAdCarSmoggingOrdinanceProposedPetitionerId) && !random.Next(100))
      AddPetitioner(kAdCarSmoggingOrdinanceProposedPetitionerId, 0, 120);
    if (ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_LeafBurningBan) &&
        !ReactivatePetitioner(kAdBanOnBurningLeavesPetitionerId) && !random.Next(100))
      AddPetitioner(kAdBanOnBurningLeavesPetitionerId, 0, 120);
    if (ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_LandfillGasRecovery) &&
        !ReactivatePetitioner(kAdLandfillGasRecoveryProposedPetitionerId) && !random.Next(100))
      AddPetitioner(kAdLandfillGasRecoveryProposedPetitionerId, 0, 120);
  } else if (static_cast<u32>(average_air) < air_medium && ordinance_flag &&
      !ReactivatePetitioner(kAdRepealCleanAirOrdinancePetitionerId) && !random.Next(100))
    AddPetitioner(kAdRepealCleanAirOrdinancePetitionerId, 0, 120);
  if (static_cast<u32>(average_water) > water_high) {
    if (ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_LawnChemicalBan) &&
        !ReactivatePetitioner(kAdLawnChemicalOrdinancePetitionerId) && !random.Next(100))
      AddPetitioner(kAdLawnChemicalOrdinancePetitionerId, 0, 120);
    if (building_available && !building_12820 && population > 2500 &&
        (!ReactivatePetitioner(kAdWaterIsDangerouslyPollutedPetitionerId) ||
            !ReactivatePetitioner(kAdChemicalOverloadInSWaterPetitionerId)) &&
        random.Next(100) < 6) {
      u32 selected = random.Next(2);
      if (!selected)
        AddPetitioner(kAdWaterIsDangerouslyPollutedPetitionerId, 0, 24);
      else if (selected == 1)
        AddPetitioner(kAdChemicalOverloadInSWaterPetitionerId, 0, 24);
    }
  } else if (static_cast<u32>(average_water) > water_medium && population > 2500 &&
      !ReactivatePetitioner(kAdWaterIsPollutedPetitionerId) && random.Next(100) < 4)
    AddPetitioner(kAdWaterIsPollutedPetitionerId, 0, 36);
  if (months_uncollected > 6 && !garbage_uncollected &&
      static_cast<i64>(7u * projected_capacity) > static_cast<i64>(10u * static_cast<u64>(monthly_input)))
    AddPetitioner(kAdRoomForMoreTrashPetitionerId, 0, 12);
  if (building_12851 > 0 && ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_TrashPresort) &&
      monthly_input > static_cast<i64>(projected_capacity) && !ReactivatePetitioner(kAdSortTrashSaveCashPetitionerId) &&
      !random.Next(100))
    AddPetitioner(kAdSortTrashSaveCashPetitionerId, 0, 120);
  if (monthly_input > static_cast<i64>(projected_capacity) && CheckGarbageBuildingAvailibility(12851) &&
      !building_12851 && !ReactivatePetitioner(kAdRecyclingCenterNeededPetitionerId) && random.Next(100) < 7)
    AddPetitioner(kAdRecyclingCenterNeededPetitionerId, 0, 36);
}

// Resolve the actual query collection only for the environment building selector, then release its retained identity.
bool AdEnvironmentAdvisor::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  UiQueryRows *rows = nullptr;
  if (message->type != kWnMsg_QueryOpened || static_cast<u32>(message->data1) != kUiQueryWasteBuildings ||
      !source->QueryInterface(IID_UiQueryRows, reinterpret_cast<void **>(&rows)))
    return false;
  WriteQueryRows(rows);
  rows->Release();
  return true;
}

// Newly created rows have zero references; AppendRow acquires ownership, so the original caller performs no extra
// release.
void AdEnvironmentAdvisor::WriteQueryRows(UiQueryRows *rows) {
  bool reveal = AdGetApplication()->GetCheatCodeManager()->GetCheatHasBeenSet(kAdGarbageInGarbageOutCheatId);
  for (AdGarbageLink *node = garbage_entries->next; node != garbage_entries; node = node->next) {
    AdGarbageEntry *entry = static_cast<AdGarbageNode *>(node)->entry;
    if (*entry->AvailableData() == 1 || reveal) {
      RsKey key = *entry->GetKey();
      UiQueryRow *row = rows->CreateRow();
      row->SetText(kUiCatalogNameField, entry->ThirdText());
      row->SetU32(kUiCatalogResourceTypeField, key.type);
      row->SetU32(kUiCatalogResourceGroupField, key.group);
      row->SetU32(kUiCatalogResourceInstanceField, key.instance);
      row->SetText(kUiCatalogDescriptionField, entry->FirstText());
      if (*entry->TypeData() != 12851)
        row->SetText(kUiCatalogSecondaryTextField, entry->SecondText());
      rows->AppendRow(row);
    }
  }
}

#if defined(_M_IX86)
static_assert(sizeof(ScRefCount) == 8);
static_assert(sizeof(AdEnvironmentCallback3) == 16);
static_assert(sizeof(AdEnvironmentCallback4) == 16);
#endif

AdEnvironmentCallback3::AdEnvironmentCallback3(AdEnvironmentAdvisor *input) : owner(input) {
}

AdEnvironmentCallback3::~AdEnvironmentCallback3() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdEnvironmentCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdEnvironmentCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdEnvironmentCallback3::Release() {
  return ScRefCount::Release();
}

bool AdEnvironmentCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdEnvironmentCallback3::IsLockingCity() {
  return false;
}

bool AdEnvironmentCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdEnvironmentCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdEnvironmentCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdEnvironmentCallback4::AdEnvironmentCallback4(AdEnvironmentAdvisor *input) : owner(input) {
}

AdEnvironmentCallback4::~AdEnvironmentCallback4() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdEnvironmentCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdEnvironmentCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdEnvironmentCallback4::Release() {
  return ScRefCount::Release();
}

bool AdEnvironmentCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdEnvironmentCallback4::IsLockingCity() {
  return false;
}

bool AdEnvironmentCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdEnvironmentCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdEnvironmentCallback4::DebugTypeTag(ScStringView *) {
  return false;
}

// Create the concrete zero-reference environment advisor and return its original secondary advisor identity.
// The director owns the subsequent interface query and any failed-query cleanup.
ScUnknown *AdCreateEnvironment() {
  return static_cast<AdAdvisor *>(new AdEnvironmentAdvisor);
}
