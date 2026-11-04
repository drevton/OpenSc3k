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

// Generate the original police, fire, crime, jail and floating warning advice from the sampled safety fields.
// Preserve unsigned products, signed station counts and the population recheck after police advice callbacks.
bool AdPublicSafetyAdvisor::CheckOnAdvice() {
  if (police_strike != 0)
    AddAdvice(kAdPoliceOnStrikeAdviceId, kAdReplacementPoliceBudget);
  else if (static_cast<i32>(unknown_cc) <= 0)
    AddAdvice(kAdThePoliceBudgetAdviceId, kAdReplacementPoliceBudget);
  else if (unknown_c4 > 15)
    AddAdvice(kAdPoliceThreatenToStrikeAdviceId, kAdReplacementPoliceBudget);
  else {
    u8 funding = police_funding;
    if (funding > police_funding_high)
      AddAdvice(crime_level <= crime_medium ? kAdPoliceOppressionAllegedAdviceId : kAdMoneyCanTCatchCriminalsAdviceId,
          kAdReplacementPoliceBudget);
    else if (funding <= police_funding_medium)
      AddAdvice(kAdPoliceThreatenToStrikeAdviceId, kAdReplacementPoliceBudget);
    else
      AddAdvice(kAdPoliceFundingSufficientAdviceId, kAdReplacementPoliceBudget);
  }
  if (fire_strike != 0)
    AddAdvice(kAdFirefightersOnStrikeAdviceId, kAdReplacementFireBudget);
  else if (static_cast<i32>(unknown_d0) <= 0)
    AddAdvice(unknown_f4 <= fire_station_minimum ? kAdTheFireBudgetAdviceId : kAdFireStationNeededAdviceId,
        kAdReplacementFireBudget);
  else if (unknown_c8 <= 15) {
    u8 funding = fire_funding;
    if (funding <= fire_funding_high)
      AddAdvice(funding <= fire_funding_medium ? 169 : kAdFireFundingAdequateAdviceId, kAdReplacementFireBudget);
    else
      AddAdvice(kAdFireFundingHighAdviceId, kAdReplacementFireBudget);
  } else
    AddAdvice(168, kAdReplacementFireBudget);
  u8 crime = crime_level;
  if (crime > crime_high)
    AddAdvice(188, 0);
  else if (crime > crime_medium)
    AddAdvice(197, 0);
  i32 jails = static_cast<i32>(unknown_d4);
  if (jails != 0) {
    if (jails > 0) {
      if (unknown_f0 != 0)
        AddAdvice(kAdJailsAtCapacityAdviceId, kAdReplacementJailCapacity);
      else if (10u * unknown_ec > 8u * unknown_e8)
        AddAdvice(kAdJailsOvercrowdedAdviceId, kAdReplacementJailCapacity);
    }
  } else if (unknown_f4 > 1000 && crime_level > crime_medium)
    AddAdvice(kAdNoJailInCityAdviceId, kAdReplacementJailCapacity);
  if (unknown_f4 > 1000) {
    u32 police_value = unknown_dc;
    if (police_value < 50)
      AddAdvice(kAdPoliceCoverageVeryPoorAdviceId, kAdReplacementPoliceCoverage);
    else if (police_value < 75)
      AddAdvice(kAdPoliceCoveragePoorAdviceId, kAdReplacementPoliceCoverage);
    else if (police_value < 99)
      AddAdvice(kAdPoliceCoveragePoorInSomeAreasAdviceId, kAdReplacementPoliceCoverage);
    else {
      u8 level = crime_level, medium = crime_medium;
      if (level > medium) {
        if (police_funding < 100)
          AddAdvice(kAdPoliceEffectivenessLowAdviceId, kAdReplacementPoliceCoverage);
      } else if (level < medium)
        AddAdvice(kAdPoliceCoverageGoodAdviceId, kAdReplacementPoliceCoverage);
    }
    if (unknown_f4 > 1000) {
      u32 fire_value = unknown_e4;
      if (fire_value < 50)
        AddAdvice(kAdFireCoverageVeryPoorAdviceId, kAdReplacementFireCoverage);
      else if (fire_value < 75)
        AddAdvice(kAdFireCoverageInadequateAdviceId, kAdReplacementFireCoverage);
      else if (fire_value < 99)
        AddAdvice(kAdFireCoverageFairAdviceId, kAdReplacementFireCoverage);
      else if (fire_funding >= 100)
        AddAdvice(kAdFireCoverageAdequateAdviceId, kAdReplacementFireCoverage);
    }
  }
  if (signal_b8 > 0.8f)
    AddAdvice(kAdIndustryEthicsRaiseMajorSafetyIssuesAdviceId, kAdReplacementIndustrialSafety);
  if (signal_bc > 0.8f)
    AddAdvice(kAdDangerTheSkyIsFallingAdviceId, kAdReplacementFallingSky);
  return true;
}

// Retain every reactivation short circuit and conditional draw, including the constructor's clock read on early return.
void AdPublicSafetyAdvisor::CheckPetitioners() {
  AdRandom random(kScRandomSeedFromClock);
  if (unknown_f4 < 100)
    return;
  if (police_strike != 0)
    AddPetitioner(kAdPoliceStrikePetitionerId, kAdPetitionReplacementPoliceStrike, 6);
  else if (unknown_c4 > 15)
    AddPetitioner(kAdPoliceThreatenWalkoutPetitionerId, kAdPetitionReplacementPoliceStrike, 6);
  if (fire_strike != 0)
    AddPetitioner(kAdFirefightersStrikePetitionerId, kAdPetitionReplacementFireStrike, 6);
  else if (unknown_c8 > 15)
    AddPetitioner(kAdFirefightersThreatenStrikePetitionerId, kAdPetitionReplacementFireStrike, 6);
  if (crime_level > crime_high && unknown_f4 > 50000 && !ReactivatePetitioner(kAdCrimeWaveStrikesPetitionerId) &&
      random.Next(100) < 4)
    AddPetitioner(kAdCrimeWaveStrikesPetitionerId, 0, 48);
  u8 crime = crime_level;
  if (crime > crime_high && unknown_f4 > 10000) {
    if (ordinance_f9 == 0 && !ReactivatePetitioner(kAdYouthCurfewProposedPetitionerId) && random.Next(100) < 2)
      AddPetitioner(kAdYouthCurfewProposedPetitionerId, 0, 84);
    if (ordinance_fa == 0 && !ReactivatePetitioner(kAdJuniorSportsProposedPetitionerId) && random.Next(100) < 3)
      AddPetitioner(kAdJuniorSportsProposedPetitionerId, 0, 84);
    if (!ReactivatePetitioner(kAdCrimeHurtsBusinessPetitionerId) && random.Next(100) < 4)
      AddPetitioner(kAdCrimeHurtsBusinessPetitionerId, 0, 84);
  } else {
    u8 medium = crime_medium;
    if (crime > medium && unknown_f4 > 1000) {
      if (ordinance_fa == 0 && !ReactivatePetitioner(kAdJuniorSportsProposedPetitionerId) && random.Next(100) < 2)
        AddPetitioner(kAdJuniorSportsProposedPetitionerId, 0, 84);
      if (!ReactivatePetitioner(kAdCrimeHurtsBusinessPetitionerId) && random.Next(100) < 2)
        AddPetitioner(kAdCrimeHurtsBusinessPetitionerId, 0, 84);
    } else if (crime < medium && unknown_f4 > 100) {
      if (ordinance_f9 != 0 && !ReactivatePetitioner(kAdRepealCurfewOrdinancePetitionerId) && random.Next(100) < 3)
        AddPetitioner(kAdRepealCurfewOrdinancePetitionerId, 0, 84);
      if (ordinance_fa != 0 && !ReactivatePetitioner(kAdRepealJuniorSportsPetitionerId) && random.Next(100) < 2)
        AddPetitioner(kAdRepealJuniorSportsPetitionerId, 0, 84);
    }
  }
  if (crime_level < crime_medium && (HasAdvice(188) || HasAdvice(197)) && random.Next(100) < 40)
    AddReplaceablePetitioner(kAdCrimeRateDropsPetitionerId, 0, 6);
  if (crime_level < crime_medium && unknown_f4 > 10000) {
    if (unknown_dc > 98 && !ReactivatePetitioner(kAdGoodPoliceCoveragePetitionerId) && random.Next(100) < 5)
      AddPetitioner(kAdGoodPoliceCoveragePetitionerId, 0, 60);
    if (police_funding > 105 && !ReactivatePetitioner(kAdPoliceOppressivePetitionerId) && random.Next(100) < 5)
      AddPetitioner(kAdPoliceOppressivePetitionerId, 0, 36);
  }
  if (static_cast<i32>(unknown_cc) > 0 && unknown_d4 == 0 && unknown_f4 > jail_minimum)
    AddPetitioner(kAdNeedsJailsPetitionerId, 0, 36);
  if (unknown_d0 == 0 && unknown_f4 > fire_station_minimum && !ReactivatePetitioner(kAdNoFireStationsPetitionerId) &&
      random.Next(100) < 3)
    AddPetitioner(kAdNoFireStationsPetitionerId, 0, 24);
}

// Re-read the selected field after the virtual getter and raise only percentages below110; other actions are unchanged.
void AdPublicSafetyAdvisor::RespondToPetitioner(u32 id, i32 button) {
  if (button != 1 || id < 159)
    return;
  EcDepartmentBudget **department;
  if (id <= 160)
    department = &police_budget;
  else if (id == 162 || id == 204)
    department = &fire_budget;
  else
    return;
  if (*department && (*department)->GetFundingPercentage() < 110)
    (*department)->SetFundingPercentage(110);
}

// Preserve the original unused random construction and its clock read before changing only recognized combinations.
void AdPublicSafetyAdvisor::RecordEventState(i32 first, i32 second) {
  AdRandom random(kScRandomSeedFromClock);
  if (first != 0) {
    if (first == 1) {
      if (second == 0)
        event_state = 3;
      else if (second == 1)
        event_state = 4;
    }
  } else
    event_state = second == 1 ? 1 : 2;
}

// Borrow the actual agent identity before acquiring its simulator and registering the advisor.
// Ignore registration refusal and retain the shared initial sampling/advice pass and its Boolean result.
bool AdPublicSafetyAdvisor::SimulationBegin() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->AddAgent(agent);
  return AdBaseAdvisor::SimulationBegin();
}

// Resolve and remove the same agent identity before returning the original shared successful end result.
bool AdPublicSafetyAdvisor::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->RemoveAgent(agent);
  return AdBaseAdvisor::SimulationEnd();
}

// The SDK context is opaque only at this external boundary; ordinary safety calls retain their typed receiver.
static void AdSafetyDisasterChange(DsInstance *instance, i32 change, void *context) {
  AdPublicSafety *owner = static_cast<AdPublicSafety *>(context);
  if (change == 1)
    owner->DisasterStarted(instance);
  else if (change == 4)
    owner->DisasterEnded(instance);
}

// Unsubscribe messages, cheats and disaster callbacks before releasing the tracked disaster keys under the city lock.
// Retire services and funding departments in source order, keeping the unconditional final service release.
bool AdPublicSafetyAdvisor::Shutdown() {
  CtLayer *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->RemoveNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetMessageServer()->RemoveNotification(identity, kAdMsg_ShowDisasterNotification);
  AdGetMessageServer()->RemoveNotification(identity, kAdMsg_ShowNextDisaster);
  AdGetMessageServer()->RemoveNotification(identity, kDsMsg_CounterStartRequested);
  AdGetMessageServer()->RemoveNotification(identity, kDsMsg_CounterEnded);
  AdGetMessageServer()->RemoveNotification(identity, kBdMsg_PowerPlantDestroyed);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForceMariaToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdForceMariaToSayCheatId);
  if (disasters)
    disasters->StopNotifyingOnChange(AdSafetyDisasterChange, static_cast<AdPublicSafety *>(this));
  notice_lock->Lock();
  // Release callbacks may change earlier entries, so resume by the original ordered pointer key.
  u32 index = 0;
  while (index < notices.size) {
    DsInstance *instance = notices.data[index].instance;
    uptr key = reinterpret_cast<uptr>(instance);
    instance->Release();
    index = 0;
    while (index < notices.size && reinterpret_cast<uptr>(notices.data[index].instance) <= key)
      ++index;
  }
  notices.Clear();
  notice_lock->Unlock();
  ScRelease(&simulation_city);
  ScRelease(&disasters);
  ScRelease(&buildings);
  ScRelease(&residential);
  if (budget) {
    budget->RemoveDepartmentBudget(fire_budget);
    budget->RemoveDepartmentBudget(police_budget);
    budget->Release();
    budget = nullptr;
  }
  ScRelease(&police_budget);
  ScRelease(&fire_budget);
  ScRelease(&valves);
  ScRelease(&police_layer);
  ScRelease(&crime_layer);
  ScRelease(&fire_layer);
  ScRelease(&ordinances);
  occupants->Release();
  return AdBaseAdvisor::Shutdown();
}

// Copy the by-value name before binding the actual civic provider and borrowing the city budget.
AdPoliceBudget::AdPoliceBudget(u32 id, ScString input) {
  department_id = id;
  name = input;
  needed = 0;
  funding = 100;
  police = AdGetCity()->PoliceLayer();
  budget = AdGetCity()->BudgetLayer();
}

AdPoliceBudget::~AdPoliceBudget() {
}

// Publish and retain the actual department identity for Unknown or its IID, clearing the output.
bool AdPoliceBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcDepartmentBudget);
  HANDLE_QUERY_INTERFACE(IID_EcDepartmentBudget, EcDepartmentBudget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdPoliceBudget::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdPoliceBudget::Release() {
  return ScRefCount::Release();
}

u32 AdPoliceBudget::GetDepartmentID() {
  return department_id;
}

i64 AdPoliceBudget::GetLastYearSurplus() {
  return 0;
}

i64 AdPoliceBudget::GetTotalFundingNeeded() {
  return needed;
}

void AdPoliceBudget::SetTotalFundingNeeded(i64 value) {
  needed = value;
  budget->NeededFundingChanged(static_cast<EcDepartmentBudget *>(this));
}

// Borrow the embedded name interface without an extra reference or String copy.
ScStringView *AdPoliceBudget::GetDepartmentName() {
  return &name;
}

u8 AdPoliceBudget::GetFundingPercentage() {
  return funding;
}

u32 AdPoliceBudget::GetAdvisorID() {
  return kAdPublicSafetyAdvisorId;
}

u32 AdPoliceBudget::GetAdvisorGroupID() {
  return kAdReplacementPoliceBudget;
}

// Store the raw funding byte before updating the civic provider, then notify the live budget field.
void AdPoliceBudget::SetFundingPercentage(u8 value) {
  funding = value;
  police->SetFundingPercentage(value);
  budget->FundingPercentageChanged(static_cast<EcDepartmentBudget *>(this));
}

// Copy the by-value name before binding the actual civic provider and borrowing the city budget.
AdFireBudget::AdFireBudget(u32 id, ScString input) {
  department_id = id;
  name = input;
  needed = 0;
  funding = 100;
  CtLayer *layer = AdGetCity()->GetSpecificLayer(kCtFireLayerType);
  layer->QueryInterface(IID_CtFireLayer, reinterpret_cast<void **>(&fire));
  budget = AdGetCity()->BudgetLayer();
}

AdFireBudget::~AdFireBudget() {
  if (fire)
    fire->Release();
}

// Publish and retain the actual department identity for Unknown or its IID, clearing the output.
bool AdFireBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcDepartmentBudget);
  HANDLE_QUERY_INTERFACE(IID_EcDepartmentBudget, EcDepartmentBudget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdFireBudget::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdFireBudget::Release() {
  return ScRefCount::Release();
}

u32 AdFireBudget::GetDepartmentID() {
  return department_id;
}

i64 AdFireBudget::GetLastYearSurplus() {
  return 0;
}

i64 AdFireBudget::GetTotalFundingNeeded() {
  return needed;
}

void AdFireBudget::SetTotalFundingNeeded(i64 value) {
  needed = value;
  budget->NeededFundingChanged(static_cast<EcDepartmentBudget *>(this));
}

// Borrow the embedded name interface without an extra reference or String copy.
ScStringView *AdFireBudget::GetDepartmentName() {
  return &name;
}

u8 AdFireBudget::GetFundingPercentage() {
  return funding;
}

u32 AdFireBudget::GetAdvisorID() {
  return kAdPublicSafetyAdvisorId;
}

u32 AdFireBudget::GetAdvisorGroupID() {
  return kAdReplacementFireBudget;
}

// Store the raw funding byte before updating the civic provider, then notify the live budget field.
void AdFireBudget::SetFundingPercentage(u8 value) {
  funding = value;
  fire->SetFundingPercentage(value);
  budget->FundingPercentageChanged(static_cast<EcDepartmentBudget *>(this));
}

void AdPublicSafetyAdvisor::InitializeOrdinances() {
  for (u32 id = 5000; id <= 5009; ++id)
    AddOrdinance(id);
}

// Bind the retained city services, real budget departments and simulation/disaster callbacks before shared initialization.
// Preserve partial binding failures, resource retirement, reversed department registration and separate cheat acquisitions.
bool AdPublicSafetyAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  simulation_city = input;
  input->AddRef();
  notice_lock = input->GetCriticalSection();
  disasters = nullptr;
  CtLayer *layer = simulation_city->GetSpecificLayer(kDsControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_DsLayer, reinterpret_cast<void **>(&disasters));
  layer = simulation_city->GetSpecificLayer(kCtPoliceLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtPoliceLayer, reinterpret_cast<void **>(&police_layer));
  layer = simulation_city->GetSpecificLayer(kCtCrimeLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtCrimeLayer, reinterpret_cast<void **>(&crime_layer));
  layer = simulation_city->GetSpecificLayer(kCtFireLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtFireLayer, reinterpret_cast<void **>(&fire_layer));
  layer = simulation_city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  layer = simulation_city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances));
  layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = simulation_city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves));
  layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  occupants = ScAddRefNotNull(simulation_city->SurfaceOccupantManager());
  police_budget = static_cast<EcDepartmentBudget *>(new AdPoliceBudget(
      kEcPoliceDepartmentId, ScString(AdTextHandle(kRsGamePoliceText, kAdAdvisorLabelGroup).ToString())));
  police_budget->AddRef();
  fire_budget = static_cast<EcDepartmentBudget *>(
      new AdFireBudget(kEcFireDepartmentId, ScString(AdTextHandle(kRsGameFireText, kAdAdvisorLabelGroup).ToString())));
  fire_budget->AddRef();
  budget->AddDepartmentBudget(fire_budget);
  budget->AddDepartmentBudget(police_budget);
  if (AdBaseAdvisor::ordinances)
    InitializeOrdinances();
  unknown_15c = 0;
  unknown_158 = 0;
  unknown_160 = 100;
  LoadConfiguration();
  callback3 = new AdSafetyCallback3(this);
  callback4 = new AdSafetyCallback4(this);
  CtAgentType *node = AdGetAgentTree()->GetNode(kCtAgentType_PublicSafetyAdvisorAgent);
  SetAgentType(node);
  node->AttachManipulator(callback3, 3);
  node->AttachManipulator(callback4, 4);
  ScMessageTarget *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->AddNotification(identity, kCtMsg_OrdinanceAvailable);
  disasters->NotifyOnChange(AdSafetyDisasterChange, static_cast<AdPublicSafety *>(this));
  AdGetMessageServer()->AddNotification(identity, kAdMsg_ShowDisasterNotification);
  AdGetMessageServer()->AddNotification(identity, kAdMsg_ShowNextDisaster);
  AdGetMessageServer()->AddNotification(identity, kDsMsg_CounterStartRequested);
  AdGetMessageServer()->AddNotification(identity, kDsMsg_CounterEnded);
  AdGetMessageServer()->AddNotification(identity, kBdMsg_PowerPlantDestroyed);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceMariaToSayCheatId, AdForceMariaToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdForceMariaToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(identity, kAdStopForcingAdviceCheatId);
  return AdBaseAdvisor::Init(input);
}

// Advance the selected disaster in unsigned pointer order, wrapping after the last entry under the cached lock.
u8 AdPublicSafetyAdvisor::AdvanceDisaster(DsInstance *current) {
  ScLock *lock = notice_lock;
  lock->Lock();
  u8 result = 0;
  if (notices.size == 0)
    selected_disaster = nullptr;
  else if (notices.size == 1) {
    selected_disaster = notices.data[0].instance;
    result = 1;
  } else {
    for (u32 index = 0; index < notices.size; ++index) {
      if (notices.data[index].instance != current)
        continue;
      selected_disaster = notices.data[index + 1 == notices.size ? 0 : index + 1].instance;
      result = 1;
      break;
    }
  }
  lock->Unlock();
  return result;
}

// Center the city view on a located disaster after the synchronous view query and optional target reset.
static u8 AdShowDisaster(DsInstance *instance) {
  u32 x, z;
  if (!instance->GetLocation(&x, &z))
    return 0;
  AdGetBoxX()->Event(kAuEventPlayEffect, kAuEffectGoToDisaster, 0, 0, 0);
  ScMessage query(kWnMsg_QueryCityViewParams, 0, 0, 0);
  AdGetMessageServer()->MessageSend(query);
  if (query.data1 != 0) {
    ScMessage reset(kWnMsg_SetCityViewParams, 0, kSpCityViewDefaultFlagsRequest, 1);
    AdGetMessageServer()->MessagePost(reset, false);
  }
  ScMessage center(kWnMsg_FocusCityCamera, x, z, 1);
  AdGetMessageServer()->MessagePost(center, false);
  return 1;
}

// Show the current disaster and advance the retained selection for the next request while preserving nested locking.
// With no notices, clear the selection and send the original alternate BoxX event.
void AdPublicSafetyAdvisor::ShowNextDisaster() {
  notice_lock->Lock();
  if (notices.size != 0) {
    if (notices.size == 1)
      selected_disaster = notices.data[0].instance;
    else if (!selected_disaster)
      selected_disaster = notices.data[0].instance;
    DsInstance *current = selected_disaster;
    if (notices.size != 1)
      AdvanceDisaster(current);
    notice_lock->Unlock();
    AdShowDisaster(current);
  } else {
    selected_disaster = nullptr;
    notice_lock->Unlock();
    AdGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiError, 0, 0, 0);
  }
}

// Inspect live disaster types in unsigned pointer order while holding the cached city lock.
u8 AdPublicSafetyAdvisor::HasDisaster(u32 type) {
  ScLock *lock = notice_lock;
  lock->Lock();
  u8 result = 0;
  u32 index = 0;
  while (index < notices.size) {
    DsInstance *instance = notices.data[index].instance;
    uptr key = reinterpret_cast<uptr>(instance);
    if (instance->TypeId() == type) {
      result = 1;
      break;
    }
    index = 0;
    while (index < notices.size && reinterpret_cast<uptr>(notices.data[index].instance) <= key)
      ++index;
  }
  lock->Unlock();
  return result;
}

// Resolve disaster text and synchronously obtain a notification identity with its actual instance callback.
// Retain the temporary String across dispatch and preserve the separate group, text, type and instance arguments.
u32 AdCreateDisasterNotification(u32 group, u32 text_id, u32 type, DsInstance *instance) {
  u32 result = 0;
  AdTextHandle resource(text_id, group);
  if (resource.IsValid()) {
    ScString *text = new ScString;
    resource.CopyTo(text);
    RsKey key = {};
    resource.GetKey(&key);
    text->AddRef();
    AdNotification notification = {
        0, key.group, key.instance, text, kAdMsg_ShowDisasterNotification, reinterpret_cast<uptr>(instance), type, 0};
    ScMessage message(kApMsgCreateNotification, reinterpret_cast<uptr>(&notification), 0, 0);
    AdGetMessageServer()->MessageSend(message);
    text->Release();
    result = notification.notification;
  }
  return result;
}

// Borrow the application's current city view through the two original nullable application/window getters.
// The advisor keeps no reference to either provider.
static SpViewInterface *AdSafetyCityView() {
  ApGameInterface *application = AdGetApplication();
  SpCityWindowView *window = application ? application->GetISC3WinCityView() : nullptr;
  return window ? window->GetCurrentView() : nullptr;
}

// Classify a newly reported disaster, reuse its category notification and retain the ordered instance record.
// Apply the original auto-centering preference and special-disaster zoom behavior before retaining the instance.
void AdPublicSafetyAdvisor::DisasterStarted(DsInstance *instance) {
  AdRandom random(kScRandomSeedFromClock);
  u32 type = instance->TypeId(), category = 0, notification_id = 0, group = 0, text_id = 0;
  switch (type) {
  case kDsRiotTypeId:
    category = kAdRiotNotificationCategory;
    group = kRsNewsTextGroup;
    text_id = random.Next(3) + 620;
    break;
  case kDsTrackTypeId:
    category = kAdEarthquakeNotificationCategory;
    group = kRsNewsTextGroup;
    text_id = random.Next(3) + 587;
    break;
  case kDsFireTypeId: {
    group = kRsNewsTextGroup;
    u32 x, z;
    instance->GetLocation(&x, &z);
    ScOccupant *occupant;
    if (occupants->GetOccupantAtCell(x, z, &occupant)) {
      category = kAdBuildingFireNotificationCategory;
      text_id = random.Next(4) + 636;
      occupant->Release();
    } else {
      category = kAdOpenGroundFireNotificationCategory;
      text_id = random.Next(2) + 640;
    }
    break;
  }
  case kDsUfoTypeId:
    category = kAdUfoNotificationCategory;
    group = kRsNewsTextGroup;
    text_id = random.Next(3) + 564;
    break;
  case kDsToxicTypeId:
    category = kAdToxicNotificationCategory;
    group = kRsDisasterNewsTextGroup;
    text_id = random.Next(3) + 103;
    break;
  case kDsTornadoTypeId:
    category = kAdTornadoNotificationCategory;
    group = kRsNewsTextGroup;
    text_id = random.Next(3) + 605;
    break;
  case kDsWaterTypeId:
    category = kAdFloodImpactNotificationCategory;
    group = kRsDisasterNewsTextGroup;
    text_id = random.Next(3) + 115;
    break;
  case kDsLocustTypeId:
    category = kAdLocustNotificationCategory;
    group = kRsDisasterNewsTextGroup;
    text_id = random.Next(3) + 126;
    break;
  case kDsImpactTypeId:
    category = kAdFloodImpactNotificationCategory;
    group = kRsDisasterNewsTextGroup;
    text_id = random.Next(2) + 141;
    break;
  }
  if (category != 0) {
    notice_lock->Lock();
    for (u32 index = 0; index < notices.size; ++index) {
      if (notices.data[index].disaster_type == category)
        notification_id = notices.data[index].notification;
      if (notification_id != 0)
        break;
    }
    notice_lock->Unlock();
    if (notification_id == 0)
      notification_id = AdCreateDisasterNotification(group, text_id, category, instance);
  }
  notice_lock->Lock();
  u32 index = 0;
  uptr key = reinterpret_cast<uptr>(instance);
  while (index < notices.size && reinterpret_cast<uptr>(notices.data[index].instance) < key)
    ++index;
  if (index == notices.size || notices.data[index].instance != instance) {
    notices.Append();
    for (u32 next = notices.size - 1; next > index; --next)
      notices.data[next] = notices.data[next - 1];
  }
  notices.data[index] = {instance, category, notification_id, 0};
  notice_lock->Unlock();
  if (*reinterpret_cast<const u8 *>(AdGetAppPreferences()->SimPrefs()) != 0 || instance->TypeId() == kDsParadeTypeId) {
    u32 x, z;
    instance->GetLocation(&x, &z);
    ScRect visible;
    AdSafetyCityView()->GetCameraHint(&visible, nullptr);
    ScMessage query(kWnMsg_QueryCityViewParams, 0, 0, 0);
    AdGetMessageServer()->MessageSend(query);
    if (query.data1 != 0) {
      ScMessage reset(kWnMsg_SetCityViewParams, 0, kSpCityViewDefaultFlagsRequest, 1);
      AdGetMessageServer()->MessagePost(reset, false);
    }
    bool changed_zoom = false;
    if (instance->TypeId() == kDsParadeTypeId) {
      i32 zoom, rotation;
      AdSafetyCityView()->GetZoomAndRotation(&zoom, &rotation);
      if (zoom < 3) {
        ScMessage zoom_message(kWnMsg_SetCityCameraZoomRotation, 3, static_cast<u32>(rotation), 1);
        AdGetMessageServer()->MessagePost(zoom_message, false);
        changed_zoom = true;
      }
    }
    i32 cell_x = static_cast<i32>(x), cell_z = static_cast<i32>(z);
    if (cell_x < visible.left || cell_z < visible.top || cell_x > visible.right || cell_z > visible.bottom ||
        changed_zoom) {
      ScMessage center(kWnMsg_FocusCityCamera, x, z, 1);
      AdGetMessageServer()->MessagePost(center, false);
      AdvanceDisaster(instance);
    }
  }
  instance->AddRef();
}

static AdToken AdMakeDisasterToken(u32 kind, const ScString &name, i32 value) {
  return AdToken(kind, &name, value);
}

void AdPublicSafetyAdvisor::AddDisasterTokens(u32 id, DsInstance *instance) {
  i32 destroyed = static_cast<i32>(instance->GetDestroyedCount()), cost = static_cast<i32>(instance->GetCost());
  AdToken first = AdMakeDisasterToken(2, ScString("%X%"), destroyed);
  AddAdviceToken(id, first);
  AdToken second = AdMakeDisasterToken(3, ScString("%Y%"), cost);
  AddAdviceToken(id, second);
}

// Remove a finished disaster and dismiss its shared notification only after the last matching category disappears.
void AdPublicSafetyAdvisor::DisasterEnded(DsInstance *instance) {
  AdRandom random(kScRandomSeedFromClock);
  if (!simulation_city) {
    if (instance)
      instance->Release();
    return;
  }
  notice_lock->Lock();
  if (instance == selected_disaster) {
    if (notices.size > 1)
      AdvanceDisaster(selected_disaster);
    else
      selected_disaster = nullptr;
  }
  u32 index = 0;
  while (index < notices.size && notices.data[index].instance != instance)
    ++index;
  if (index == notices.size) {
    notice_lock->Unlock();
    return;
  }
  u32 category = notices.data[index].disaster_type, notification_id = notices.data[index].notification;
  for (u32 next = index + 1; next < notices.size; ++next)
    notices.data[next - 1] = notices.data[next];
  --notices.size;
  if (category != 0) {
    bool shared = false;
    for (index = 0; index < notices.size; ++index) {
      if (notices.data[index].disaster_type == category) {
        shared = true;
        break;
      }
    }
    if (!shared) {
      notice_lock->Unlock();
      AdDismissNotification(notification_id);
      notice_lock->Lock();
    }
  }
  notice_lock->Unlock();
  u32 id = 0, replacement = 0;
  u8 delay = 8;
  switch (instance->TypeId()) {
  case kDsFireTypeId: {
    u32 x, z;
    if (instance->GetLocation(&x, &z)) {
      u8 coverage;
      fire_layer->GetValue(x, z, &coverage);
      if (coverage < fire_coverage_cutoff) {
        id = 154;
        delay = 6;
      }
    }
    break;
  }
  case kDsRiotTypeId:
    if (!HasDisaster(kDsRiotTypeId)) {
      u32 x, z;
      if (instance->GetLocation(&x, &z)) {
        u8 coverage;
        police_layer->GetValue(x, z, &coverage);
        if (coverage == 0) {
          id = 150;
          replacement = kAdReplacementRiotPoliceCoverage;
          delay = 6;
        }
      }
    }
    break;
  case kDsTrackTypeId: {
    DsTrackState *state;
    if (instance->QueryInterface(IID_DsTornadoMotion, reinterpret_cast<void **>(&state))) {
      if (instance->GetDestroyedCount() != 0) {
        u8 severity = state->GetSeverity();
        bool enabled = ordinances->QueryOrdinanceOn(kCtOrdinance_EarthquakeRetrofitting) != 0;
        if (severity == 1)
          id = 140 + enabled;
        else if (severity == 2)
          id = 192 + enabled;
        else if (severity == 3)
          id = enabled ? 195 : 190;
        if (id != 0) {
          AddReplaceableAdvice(id, kAdReplacementEarthquakeAftermath, 12);
          AddDisasterTokens(id, instance);
        }
      }
      state->Release();
    }
    instance->Release();
    return;
  }
  case kDsUfoTypeId:
    if (instance->GetDestroyedCount() != 0) {
      if (event_state == 0)
        id = 234;
      else if (event_state == 3)
        id = 235;
      else if (event_state == 4)
        id = 147;
    }
    break;
  case kDsTornadoTypeId:
    if (instance->GetDestroyedCount() != 0) {
      if (event_state == 0)
        id = 144;
      else if (event_state == 3)
        id = 236;
      else if (event_state == 4)
        id = 143;
      delay = 6;
    }
    break;
  case kDsWaterTypeId:
    if (instance->GetDestroyedCount() != 0)
      id = 404;
    break;
  case kDsImpactTypeId:
    if (instance->GetDestroyedCount() != 0)
      id = event_state == 4 ? 410 : 411;
    break;
  case kDsToxicTypeId:
    if (instance->GetDestroyedCount() != 0) {
      if (event_state == 0)
        id = 401;
      else if (event_state == 4)
        id = 402;
    }
    break;
  }
  if (id != 0) {
    AddReplaceableAdvice(id, replacement, delay);
    AddDisasterTokens(id, instance);
  }
  instance->Release();
}

// Refresh actual police/fire funding, strikes, coverage, civic building counts and optional disaster signals.
// Preserve output-pointer checks after ignored queries and leave existing signals untouched when the whole layer is absent.
void AdPublicSafetyAdvisor::GetSimulationData() {
  if (police_budget)
    police_funding = police_budget->GetFundingPercentage();
  if (fire_budget)
    fire_funding = fire_budget->GetFundingPercentage();
  if (residential)
    unknown_f4 = static_cast<u32>(residential->GetPopulation());
  if (crime_layer)
    crime_level = crime_layer->GetAverageValue();
  if (police_layer) {
    police_strike = police_layer->IsOnStrike();
    unknown_c4 = police_layer->ChanceOfStrike() / 100u;
    unknown_d8 = police_layer->GetAverageCoverage();
    unknown_dc = police_layer->GetPercentCoverage();
    unknown_e8 = police_layer->GetTotalJailCapacity();
    unknown_ec = police_layer->GetInmatePopulation();
    unknown_f0 = police_layer->GetCriminalsAtLarge();
  }
  if (fire_layer) {
    fire_strike = fire_layer->IsOnStrike();
    unknown_c8 = fire_layer->ChanceOfStrike() / 100u;
    unknown_e0 = fire_layer->GetAverageCoverage();
    unknown_e4 = fire_layer->GetPercentCoverage();
  }
  if (ordinances) {
    ordinance_f9 = ordinances->QueryOrdinanceOn(kCtOrdinance_YouthCurfew);
    ordinance_fa = ordinances->QueryOrdinanceOn(kCtOrdinance_JuniorSports);
  }
  unknown_cc = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentPoliceStation));
  unknown_d0 = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentFireStation));
  unknown_d4 = static_cast<u32>(buildings->CheckOccupantTracking(kCtAgentJail));
  DsLayer *layer = simulation_city->DisasterLayer();
  if (layer) {
    void *queried = nullptr;
    DsManager *manager = layer->GetDisasterManager(kDsToxicTypeId);
    if (manager)
      manager->QueryInterface(IID_DsToxicControl, &queried);
    if (queried) {
      DsToxicControl *control = static_cast<DsToxicControl *>(queried);
      signal_b8 = static_cast<f32>(control->IsTriggerEligible());
      control->Release();
    } else
      signal_b8 = 0.0f;
    queried = nullptr;
    manager = layer->GetDisasterManager(kDsImpactTypeId);
    if (manager)
      manager->QueryInterface(IID_DsImpactControl, &queried);
    if (queried) {
      DsImpactControl *control = static_cast<DsImpactControl *>(queried);
      signal_bc = static_cast<f32>(control->ActiveCountIsPositive());
      control->Release();
    } else
      signal_bc = 0.0f;
  }
}

// Exclude the original missing-selected sentinel read at the native array boundary without fabricating a game result.
void AdPublicSafetyAdvisor::ShowDisasterNotification(ScMessage *message) {
  DsInstance *instance = reinterpret_cast<DsInstance *>(message->data1);
  u32 type = static_cast<u32>(message->data2);
  if (instance == selected_disaster) {
    AdShowDisaster(instance);
    AdvanceDisaster(instance);
    return;
  }
  notice_lock->Lock();
  u32 index = 0;
  while (index < notices.size && notices.data[index].instance != selected_disaster)
    ++index;
  if (index == notices.size) {
    notice_lock->Unlock();
    return;
  }
  u32 remaining = notices.size;
  while (notices.data[index].disaster_type != type && remaining != 0) {
    --remaining;
    if (++index == notices.size)
      index = 0;
  }
  DsInstance *selected = notices.data[index].instance;
  notice_lock->Unlock();
  index = 0;
  while (index < notices.size && notices.data[index].instance != selected)
    ++index;
  if (index < notices.size && notices.data[index].disaster_type == type) {
    AdShowDisaster(selected);
    AdvanceDisaster(selected);
  }
}

// Clear the event after advice or text notification callbacks, including a value changed during those calls.
void AdPublicSafetyAdvisor::ProcessEventState() {
  AdRandom random(kScRandomSeedFromClock);
  if (event_state == 1)
    AddReplaceableAdvice(kAdEmergencyWarningFalseAlarmAdviceId, 0, 6);
  else if (event_state == 2)
    AdShowTextNotification(random.Next(3) + 611);
  event_state = 0;
}

void AdPublicSafetyAdvisor::AdvanceHoliday() {
  DsManager *manager = AdGetCity()->DisasterLayer()->GetDisasterManager(kDsParadeTypeId);
  void *queried = nullptr;
  if (manager->QueryInterface(IID_DsParadeControl, &queried)) {
    DsParadeControl *control = static_cast<DsParadeControl *>(queried);
    control->AdvanceSchedule();
    control->Release();
  }
}

// Run pending safety work for every nonzero pending byte, then perform the independent holiday eligibility check.
// Preserve completion posting before the holiday call and clear pending only after both phases.
bool AdPublicSafetyAdvisor::DoAdvisorSimulation() {
  if (simulation_pending != 0) {
    GetSimulationData();
    CheckOnAdvice();
    advice->Advance();
    advice->ResetRedisplay();
    CheckPetitioners();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdPublicSafetyAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  CtCity *input = AdGetCity();
  if (input && AdGetApplication()) {
    i32 country = AdGetAppPreferences()->GetCountry();
    if (country != 30 && country != 31 && input->AreHolidayFeaturesEnabled() == 1)
      AdvanceHoliday();
  }
  simulation_pending = 0;
  return true;
}

// Dispatch actual safety event, cheat, disaster-camera and ordinance-help messages from their separate payload words.
// Preserve the original true result even for unrelated notifications or an unrecognized command.
bool AdPublicSafetyAdvisor::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kDsMsg_CounterStartRequested:
    RecordEventState(static_cast<i32>(message->data1), static_cast<i32>(message->data2));
    break;
  case kWnMsg_CheatCommand:
    if (static_cast<u32>(message->data1) == kAdStopForcingAdviceCheatId)
      ClearForcedAdvice();
    else if (static_cast<u32>(message->data1) == kAdForceMariaToSayCheatId) {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceMariaToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceMariaToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
    }
    break;
  case kDsMsg_CounterEnded:
    ProcessEventState();
    break;
  case kBdMsg_PowerPlantDestroyed:
    if (message->data1 == 12115)
      AddReplaceableAdvice(kAdNuclearPlantExplodesAdviceId, 0, 8);
    break;
  case kAdMsg_ShowDisasterNotification:
    ShowDisasterNotification(message);
    break;
  case kCtMsg_OrdinanceAvailable:
    switch (static_cast<u32>(message->data1)) {
    case kCtOrdinance_MandatorySmokeDetectors:
      AddHelp(52);
      break;
    case kCtOrdinance_NeighborhoodWatch:
      AddHelp(51);
      break;
    case kCtOrdinance_YouthCurfew:
      AddHelp(53);
      break;
    case kCtOrdinance_CrossingGuards:
      AddHelp(50);
      break;
    }
    break;
  case kAdMsg_ShowNextDisaster:
    ShowNextDisaster();
    break;
  }
  return true;
}

// Construct the original zero-reference safety advisor and return its actual secondary advisor identity at complete+4.
// The registered factory protocol supplies the following query and cleanup without a factory retain.
ScUnknown *AdCreatePublicSafety() {
  return static_cast<AdAdvisor *>(new AdPublicSafetyAdvisor);
}

// Construct the shared advisor, clear its city services and resolve the four original resource-backed names.
// Select the locale portraits before installing transportation tuning and clearing invention/announcement flags.
AdTransportationAdvisor::AdTransportationAdvisor() {
  simulation_city = nullptr;
  networks = nullptr;
  traffic = nullptr;
  service_154 = nullptr;
  budget = nullptr;
  valves = nullptr;
  buildings = nullptr;
  ordinances = nullptr;
  residential = nullptr;
  zones = nullptr;
  callback3 = nullptr;
  callback4 = nullptr;
  funding_variables[0] = nullptr;
  funding_variables[1] = nullptr;
  ScString name = AdTextHandle(kRsAdvisorNameTransportationAdvisorText, kAdAdvisorNameGroup).ToString();
  AdBaseAdvisor::SetName(&name);
  name = AdTextHandle(kRsGameTransportationBriefingsText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameBriefings(&name);
  name = AdTextHandle(kRsGameTransportationOrdinancesText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetNameOrdinance(&name);
  name = AdTextHandle(kRsGameMoeBiehlText, kAdAdvisorLabelGroup).ToString();
  AdBaseAdvisor::SetPersonalName(&name);
  u32 language = AdGetLanguageManager()->GetCurrentLanguage();
  u32 portrait =
      language == kScLanguageJapanese || (language > kScLanguagePolish && language <= kScLanguageThai) ? 2 : 0;
  const RsKey small = {
      kRsImageResourceType, kRsAdvisorPortraitGroup, kAdTransportationSmallPortraitBase + (portrait << 16)};
  const RsKey large = {
      kRsImageResourceType, kRsAdvisorPortraitGroup, kAdTransportationLargePortraitBase + (portrait << 16)};
  AdBaseAdvisor::SetSmallAdvisorPicture(small);
  AdBaseAdvisor::SetLargeAdvisorPicture(large);
  road_funding_high = 105;
  road_funding_medium = 80;
  road_traffic_high = 30;
  rail_usage_high = 30;
  rail_usage_medium = 10;
  crossing_guard_level = 10;
  road_funding_low = 50;
  rail_funding_high = 110;
  rail_funding_medium = 90;
  subway_year = 1910;
  bus_station_year = 1920;
  airport_year = 1930;
  highway_year = 1940;
  road_damage_cutoff = 0.15f;
  road_traffic_medium = 15;
  carpool_level = 20;
  carpool_year = 1984;
  crossing_guard_year = 1925;
  bus_stop_level = 25;
  alternate_day_year = 1932;
  alternate_day_level = 35;
  for (u32 index = 0; index < 4; ++index)
    inventions[index] = false;
  for (u32 index = 0; index < 4; ++index)
    announcements[index] = false;
  outdated_response = 302;
}

AdTransportationAdvisor::~AdTransportationAdvisor() {
}

bool AdTransportationAdvisor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_AdTransportation, AdTransportation);
  return AdBaseAdvisor::QueryInterface(iid, out);
}

u32 AdTransportationAdvisor::AddRef() {
  return CtAgentBase::AddRef();
}

u32 AdTransportationAdvisor::Release() {
  return CtAgentBase::Release();
}

u32 AdTransportationAdvisor::GetAdvisorID() {
  return kAdTransportationAdvisorId;
}

u32 AdTransportationAdvisor::GetLayerType() {
  return kAdTransportationAdvisorLayerType;
}

// Read the selected original invention flag for IDs1 through4 and decline every other invention ID.
bool AdTransportationAdvisor::InventionAvailable(u32 type) const {
  if (type < 1 || type > 4)
    return false;
  return inventions[type - 1];
}

static bool AdReadTransportationSetting(AdIni *reader, const char *section_name, const char *key_name, ScString *out) {
  ScString key(key_name), section(section_name);
  return reader->ReadEntry(section, key, out);
}

// Read transportation tuning in original order from its loose INI and SYS.PAK fallback.
// Missing keys retain prior values and make the aggregate result false; byte fields keep their low eight bits.
bool AdTransportationAdvisor::LoadConfiguration() {
  bool missing = false;
  ScString path, unused;
  AdIni reader;
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\Advisor-Transportation.INI", 31);
  reader.SetPath(path);
  AdGetApplication()->GetAppDataDirectory(&path);
  path.ScString::Append("\\Sys\\SYS.PAK", 12);
  reader.SetCompressedPath(path);
  ScString value;
  if (AdReadTransportationSetting(&reader, "FUNDING_RATE_CUTOFFS", "ROAD_FUNDING_HIGH", &value))
    road_funding_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "FUNDING_RATE_CUTOFFS", "ROAD_FUNDING_MEDIUM", &value))
    road_funding_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "FUNDING_RATE_CUTOFFS", "ROAD_FUNDING_LOW", &value))
    road_funding_low = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "FUNDING_RATE_CUTOFFS", "RAIL_FUNDING_HIGH", &value))
    rail_funding_high = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "FUNDING_RATE_CUTOFFS", "RAIL_FUNDING_MEDIUM", &value))
    rail_funding_medium = static_cast<u8>(atoi(value.begin));
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_ADVANCEMENTS", "SUBWAY_AVAILABLE", &value))
    subway_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_ADVANCEMENTS", "BUS_STATION_AVAILABLE", &value))
    bus_station_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_ADVANCEMENTS", "AIRPORT_AVAILABLE", &value))
    airport_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_ADVANCEMENTS", "HIGHWAY_AVAILABLE", &value))
    highway_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_CUTOFFS", "ROAD_DAMAGE", &value))
    road_damage_cutoff = static_cast<f32>(atof(value.begin) * static_cast<f64>(0.01f));
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_CUTOFFS", "HIGH_ROAD_TRAFFIC", &value))
    road_traffic_high = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_CUTOFFS", "MED_ROAD_TRAFFIC", &value))
    road_traffic_medium = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_CUTOFFS", "MED_RAIL_USAGE", &value))
    rail_usage_medium = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "TRANSPORTATION_CUTOFFS", "HIGH_RAIL_USAGE", &value))
    rail_usage_high = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "CARPOOL_TRIGGER_LEVEL", &value))
    carpool_level = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "CARPOOL_TRIGGER_YEAR", &value))
    carpool_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "CROSSING_GUARD_TRIGGER_LEVEL", &value))
    crossing_guard_level = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "CROSSING_GUARD_TRIGGER_YEAR", &value))
    crossing_guard_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "BUS_STOP_TRIGGER_LEVEL", &value))
    bus_stop_level = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "ALTERNATE_DAY_DRIVING_YEAR", &value))
    alternate_day_year = AdParseIniNumber(value.begin);
  else
    missing = true;
  if (AdReadTransportationSetting(&reader, "PETITIONER_GENERATION", "ALTERNATE_DAY_DRIVING_LEVEL", &value))
    alternate_day_level = AdParseIniNumber(value.begin);
  else
    missing = true;
  return !missing;
}

// Enable each invention whose configured year has arrived without clearing previously enabled inventions.
// Keep all seven source message objects alive through publication, capturing each flag before acquiring its server.
void AdTransportationAdvisor::EnableInventions() {
  u32 day, current_year;
  simulation_city->GetDate(&day, &current_year);
  if (subway_year <= current_year)
    inventions[0] = true;
  if (bus_station_year <= current_year)
    inventions[1] = true;
  if (airport_year <= current_year)
    inventions[2] = true;
  if (highway_year <= current_year)
    inventions[3] = true;
  ScMessage subway_first = {
      kWnMsg_GameMenuSetItemEnabled, kUiMenuLaySubwayRailItemId, static_cast<u32>(inventions[0]), 0};
  AdGetMessageServer()->MessageSend(subway_first);
  ScMessage subway_second = {
      kWnMsg_GameMenuSetItemEnabled, kUiMenuSubwayStationItemId, static_cast<u32>(inventions[0]), 0};
  AdGetMessageServer()->MessageSend(subway_second);
  ScMessage subway_third = {
      kWnMsg_GameMenuSetItemEnabled, kUiMenuSubwayToRailConnectionItemId, static_cast<u32>(inventions[0]), 0};
  AdGetMessageServer()->MessageSend(subway_third);
  ScMessage bus = {kWnMsg_GameMenuSetItemEnabled, kUiMenuBusStopItemId, static_cast<u32>(inventions[1]), 0};
  AdGetMessageServer()->MessageSend(bus);
  ScMessage airport = {kWnMsg_GameMenuSetItemEnabled, kUiMenuAirportItemId, static_cast<u32>(inventions[2]), 0};
  AdGetMessageServer()->MessageSend(airport);
  ScMessage highway_first = {kWnMsg_GameMenuSetItemEnabled, kUiMenuHighwayItemId, static_cast<u32>(inventions[3]), 0};
  AdGetMessageServer()->MessageSend(highway_first);
  ScMessage highway_second = {kWnMsg_GameMenuSetItemEnabled, kUiMenuOnRampItemId, static_cast<u32>(inventions[3]), 0};
  AdGetMessageServer()->MessageSend(highway_second);
}

// Resolve the real agent before acquiring its simulator, then run the shared initial sample/advice pass.
bool AdTransportationAdvisor::SimulationBegin() {
  EnableInventions();
  if (simulation_city->GetCityType() == 4 || simulation_city->GetCityType() == 7)
    AddAdvice(kAdWelcomeFromMoeBiehlAdviceId);
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->AddAgent(agent);
  return AdBaseAdvisor::SimulationBegin();
}

// Resolve and remove the same city-agent identity before returning the original shared successful end result.
bool AdTransportationAdvisor::SimulationEnd() {
  CtAgent *agent = AsISC3CityAgent();
  simulation_city->CitySimulator()->RemoveAgent(agent);
  return AdBaseAdvisor::SimulationEnd();
}

bool AdTransportationAdvisor::Init(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Init(input, segment);
}

bool AdTransportationAdvisor::Save(CtCity *input, RsDatabaseSegment *segment) {
  return AdBaseAdvisor::Save(input, segment);
}

// Apply transportation forced-advice commands, calendar invention refresh and the five original ordinance help IDs.
// Recognized message families return true even for unknown commands; unrelated message types return false.
bool AdTransportationAdvisor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    switch (static_cast<u32>(message->data1)) {
    case kAdForceMoeToSayCheatId: {
      ScString *text = reinterpret_cast<ScString *>(message->data2);
      u32 length = static_cast<u32>(text->end - text->begin);
      u32 delimiter = static_cast<u32>(strlen(AdForceMoeToSay.begin));
      for (u32 index = 0; index <= length && delimiter <= length - index; ++index)
        if (!memcmp(text->begin + index, AdForceMoeToSay.begin, delimiter)) {
          text->ScString::Erase(0, index + delimiter);
          break;
        }
      AddForcedAdvice(static_cast<u32>(atoi(text->ToChar())));
      break;
    }
    case kAdStopForcingAdviceCheatId:
      ClearForcedAdvice();
      break;
    }
  } else if (message->type == kApMsgPostCityChange) {
    EnableInventions();
  } else {
    if (message->type != kCtMsg_OrdinanceAvailable)
      return false;
    switch (static_cast<u32>(message->data1)) {
    case kCtOrdinance_ParkingFines:
      AddHelp(62);
      break;
    case kCtOrdinance_CarpoolIncentive:
      AddHelp(61);
      break;
    case kCtOrdinance_ShuttleService:
      AddHelp(63);
      break;
    case kCtOrdinance_SubsidizedMassTransit:
      AddHelp(64);
      break;
    case kCtOrdinance_AlternateDayDriving:
      AddHelp(60);
      break;
    }
  }
  return true;
}

// Make at most one newly eligible invention announcement after the original ten-percent random gate.
// Preserve announcement-message lifetimes and the distinct original flag writes when existing use removes advice.
void AdTransportationAdvisor::AnnounceInventions() {
  AdRandom random(kScRandomSeedFromClock);
  if (random.Next(100) < 10) {
    if (!inventions[0] && subway_year <= year) {
      inventions[0] = true;
      announcements[0] = true;
      AddCategoryOneAdvice(kAdSubwayBecomesAvailableAdviceId, 0, 120);
      ScMessage notice1 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuLaySubwayRailItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice1);
      ScMessage notice2 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuSubwayStationItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice2);
      ScMessage notice3 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuSubwayToRailConnectionItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice3);
      return;
    }
    if (!inventions[1] && bus_station_year <= year) {
      inventions[1] = true;
      announcements[1] = true;
      AddCategoryOneAdvice(kAdBusTransitBecomesAvailableAdviceId, 0, 120);
      ScMessage notice1 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuBusStopItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice1);
      return;
    }
    if (!inventions[2] && airport_year <= year) {
      inventions[2] = true;
      announcements[2] = true;
      AddCategoryOneAdvice(kAdAirportAvailableAdviceId, 0, 120);
      ScMessage notice1 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuAirportItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice1);
      return;
    }
    if (!inventions[3] && highway_year <= year) {
      inventions[3] = true;
      announcements[3] = true;
      AddCategoryOneAdvice(kAdHighwaysInventedAdviceId, 0, 120);
      ScMessage notice1 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuHighwayItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice1);
      ScMessage notice2 = {kWnMsg_GameMenuSetItemEnabled, kUiMenuOnRampItemId, 1, 0};
      AdGetMessageServer()->MessageSend(notice2);
      return;
    }
  }
  if (announcements[0] &&
      network_110 + static_cast<u32>(subway_stations) + static_cast<u32>(subway_rail_stations) != 0) {
    advice->Remove(99);
    announcements[0] = false;
  }
  if (announcements[1] && bus_stations > 0) {
    advice->Remove(100);
    announcements[1] = true;
  }
  if (announcements[2] && zone_14 != 0) {
    advice->Remove(135);
    announcements[2] = true;
  }
  if (announcements[3] && network_108 != 0) {
    advice->Remove(272);
    announcements[3] = true;
  }
}

#if defined(_M_IX86)
static_assert(sizeof(AdTransportationCallback3) == 16);
static_assert(sizeof(AdTransportationCallback4) == 16);
#endif

AdTransportationCallback3::AdTransportationCallback3(AdTransportationAdvisor *input) : owner(input) {
}

AdTransportationCallback3::~AdTransportationCallback3() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdTransportationCallback3::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdTransportationCallback3::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdTransportationCallback3::Release() {
  return ScRefCount::Release();
}

bool AdTransportationCallback3::Apply(CtAgent *) {
  owner->DoAdvisorSimulation();
  return true;
}

bool AdTransportationCallback3::IsLockingCity() {
  return false;
}

bool AdTransportationCallback3::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdTransportationCallback3::DebugClassTag(ScStringView *) {
  return false;
}

bool AdTransportationCallback3::DebugTypeTag(ScStringView *) {
  return false;
}

AdTransportationCallback4::AdTransportationCallback4(AdTransportationAdvisor *input) : owner(input) {
}

AdTransportationCallback4::~AdTransportationCallback4() {
}

// Publish the primary manipulator for Unknown or its actual IID, clearing rejected outputs.
bool AdTransportationCallback4::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdTransportationCallback4::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdTransportationCallback4::Release() {
  return ScRefCount::Release();
}

bool AdTransportationCallback4::Apply(CtAgent *) {
  owner->simulation_pending = 1;
  return true;
}

bool AdTransportationCallback4::IsLockingCity() {
  return false;
}

bool AdTransportationCallback4::DebugInstanceTag(ScStringView *) {
  return false;
}

bool AdTransportationCallback4::DebugClassTag(ScStringView *) {
  return false;
}

bool AdTransportationCallback4::DebugTypeTag(ScStringView *) {
  return false;
}

// Unsubscribe both messages and each cheat before releasing the retained city services in their original order.
// Remove both funding departments while the budget is live, then release their views before shared advisor cleanup.
bool AdTransportationAdvisor::Shutdown() {
  CtLayer *identity = static_cast<CtLayer *>(this);
  AdGetMessageServer()->RemoveNotification(identity, kCtMsg_OrdinanceAvailable);
  AdGetMessageServer()->RemoveNotification(identity, kApMsgPostCityChange);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdStopForcingAdviceCheatId);
  AdGetApplication()->GetCheatCodeManager()->UnregisterCheatCode(kAdForceMoeToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RemoveNotification(identity, kAdForceMoeToSayCheatId);
  ScRelease(&simulation_city);
  ScRelease(&networks);
  ScRelease(&traffic);
  ScRelease(&buildings);
  ScRelease(&zones);
  ScRelease(&valves);
  ScRelease(&ordinances);
  ScRelease(&residential);
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

#if defined(_M_IX86)
static_assert(sizeof(AdRoadBudget) == 56);
static_assert(sizeof(AdRailBudget) == 56);
#endif

// Copy the by-value name, acquire the actual retained traffic view, then borrow the city budget provider.
AdRoadBudget::AdRoadBudget(u32 id, ScString input) {
  department_id = id;
  name = input;
  needed = 0;
  funding = 100;
  CtLayer *layer = AdGetCity()->GetSpecificLayer(kNwTransitLayerType);
  layer->QueryInterface(IID_NwTrafficLayer, reinterpret_cast<void **>(&traffic));
  budget = AdGetCity()->BudgetLayer();
}

AdRoadBudget::~AdRoadBudget() {
  if (traffic)
    traffic->Release();
}

// Publish and retain the actual department identity for Unknown or its IID, clearing the output.
bool AdRoadBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcDepartmentBudget);
  HANDLE_QUERY_INTERFACE(IID_EcDepartmentBudget, EcDepartmentBudget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdRoadBudget::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdRoadBudget::Release() {
  return ScRefCount::Release();
}

u32 AdRoadBudget::GetDepartmentID() {
  return department_id;
}

i64 AdRoadBudget::GetLastYearSurplus() {
  return 0;
}

i64 AdRoadBudget::GetTotalFundingNeeded() {
  return needed;
}

void AdRoadBudget::SetTotalFundingNeeded(i64 value) {
  needed = value;
  budget->NeededFundingChanged(static_cast<EcDepartmentBudget *>(this));
}

// Borrow the embedded name interface without an extra reference or String copy.
ScStringView *AdRoadBudget::GetDepartmentName() {
  return &name;
}

u8 AdRoadBudget::GetFundingPercentage() {
  return funding;
}

u32 AdRoadBudget::GetAdvisorID() {
  return kAdTransportationAdvisorId;
}

u32 AdRoadBudget::GetAdvisorGroupID() {
  return kAdReplacementRoadBudget;
}

// Copy the by-value name, acquire the actual retained traffic view, then borrow the city budget provider.
AdRailBudget::AdRailBudget(u32 id, ScString input) {
  department_id = id;
  name = input;
  needed = 0;
  funding = 100;
  CtLayer *layer = AdGetCity()->GetSpecificLayer(kNwTransitLayerType);
  layer->QueryInterface(IID_NwTrafficLayer, reinterpret_cast<void **>(&traffic));
  budget = AdGetCity()->BudgetLayer();
}

AdRailBudget::~AdRailBudget() {
  if (traffic)
    traffic->Release();
}

// Publish and retain the actual department identity for Unknown or its IID, clearing the output.
bool AdRailBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcDepartmentBudget);
  HANDLE_QUERY_INTERFACE(IID_EcDepartmentBudget, EcDepartmentBudget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 AdRailBudget::AddRef() {
  return ScRefCount::AddRef();
}

u32 AdRailBudget::Release() {
  return ScRefCount::Release();
}

u32 AdRailBudget::GetDepartmentID() {
  return department_id;
}

i64 AdRailBudget::GetLastYearSurplus() {
  return 0;
}

i64 AdRailBudget::GetTotalFundingNeeded() {
  return needed;
}

void AdRailBudget::SetTotalFundingNeeded(i64 value) {
  needed = value;
  budget->NeededFundingChanged(static_cast<EcDepartmentBudget *>(this));
}

// Borrow the embedded name interface without an extra reference or String copy.
ScStringView *AdRailBudget::GetDepartmentName() {
  return &name;
}

u8 AdRailBudget::GetFundingPercentage() {
  return funding;
}

u32 AdRailBudget::GetAdvisorID() {
  return kAdTransportationAdvisorId;
}

u32 AdRailBudget::GetAdvisorGroupID() {
  return kAdReplacementTransitBudget;
}

// Store the road department's raw funding byte before notifying its retained traffic provider and borrowed budget.
// Reload the budget field after the provider callback, preserving the original department identity.
void AdRoadBudget::SetFundingPercentage(u8 value) {
  funding = value;
  traffic->SetRoadFundingPercentage(value);
  budget->FundingPercentageChanged(static_cast<EcDepartmentBudget *>(this));
}

// Store rail funding before updating the actual traffic provider and notifying the borrowed budget manager.
// This ICF body shares code with health funding but keeps its genuine traffic receiver type.
void AdRailBudget::SetFundingPercentage(u8 value) {
  funding = value;
  traffic->SetRailFundingPercentage(value);
  budget->FundingPercentageChanged(static_cast<EcDepartmentBudget *>(this));
}

void AdTransportationAdvisor::InitializeOrdinances() {
  AddOrdinance(6000);
  AddOrdinance(6002);
  AddOrdinance(6003);
  AddOrdinance(6004);
  AddOrdinance(6010);
}

// Refresh transportation's date, funding, networks, traffic, building counts and zoning failure totals.
// Preserve optional-service guards and the captured receivers used by the original aggregate queries.
void AdTransportationAdvisor::GetSimulationData() {
  simulation_city->GetDate(&day_of_year, &year);
  if (funding_variables[0])
    road_funding = funding_variables[0]->GetFundingPercentage();
  if (funding_variables[1])
    rail_funding = funding_variables[1]->GetFundingPercentage();
  if (networks) {
    network_104 = networks->GetRoadCount();
    network_108 = networks->GetHighwayCount();
    network_10c = networks->GetRailCount();
    network_110 = networks->GetSubwayCount();
    road_damage = static_cast<f32>(networks->GetTransitDamage());
  }
  if (traffic) {
    road_traffic = static_cast<f32>(traffic->GetTrafficRatio());
    rail_total = traffic->GetRailTotal();
    rail_usage = traffic->GetRailUsage();
  }
  if (ordinances) {
    ordinance_carpool = ordinances->QueryOrdinanceOn(kCtOrdinance_CarpoolIncentive);
    ordinance_crossing_guard = ordinances->QueryOrdinanceOn(kCtOrdinance_CrossingGuards);
  }
  if (residential)
    population = static_cast<u32>(residential->GetPopulation());
  if (buildings) {
    bus_stations = buildings->CheckOccupantTracking(kCtAgentBusStation);
    rail_stations = buildings->CheckOccupantTracking(kCtAgentRailStation);
    subway_stations = buildings->CheckOccupantTracking(kCtAgentSubwayStation);
    subway_rail_stations = buildings->CheckOccupantTracking(kCtAgentSubwayRailStation);
    airports = buildings->CheckOccupantTracking(kCtAgentAirport);
  }
  if (zones) {
    zone_14 = zones->GetZoneCount(14);
    CtZoneLayer *layer = zones;
    u32 count = layer->GetDevelopmentFailureCount(14, 18);
    count += layer->GetDevelopmentFailureCount(14, 16);
    count += layer->GetDevelopmentFailureCount(14, 15);
    count += layer->GetDevelopmentFailureCount(14, 14);
    count += layer->GetDevelopmentFailureCount(14, 10);
    zone_18 = count + layer->GetDevelopmentFailureCount(14, 9);
  }
}

// Queue original road, rail, traffic and access advice from the current cached transportation samples.
// Preserve float32 cutoffs, wrapped DWORD totals and low-byte consumption of the full rail-funding calculation.
bool AdTransportationAdvisor::CheckOnAdvice() {
  AdRandom random(kScRandomSeedFromClock);
  if (network_110 + network_10c + network_104 + network_108 == 0)
    AddAdvice(kAdRoadsNeededAdviceId, 0);
  if (network_108 + network_104 != 0) {
    u8 funding = road_funding;
    if (funding <= road_funding_high || road_damage >= 0.01f) {
      if (funding <= road_funding_medium || road_damage >= 0.01f)
        if (funding <= road_funding_low || road_damage > road_damage_cutoff)
          AddAdvice(kAdRoadsBarelyUseableAdviceId, kAdReplacementRoadBudget);
        else
          AddAdvice(kAdNoFundsToRepairPotholesAdviceId, kAdReplacementRoadBudget);
      else
        AddAdvice(kAdRoadBudgetInLineWithExpensesAdviceId, kAdReplacementRoadBudget);
    } else
      AddAdvice(kAdRoadsPavedWithPorkAdviceId, kAdReplacementRoadBudget);
  } else
    AddAdvice(kAdTheRoadBudgetAdviceId, kAdReplacementRoadBudget);
  if (network_110 + network_10c != 0) {
    if (traffic->Flag108IsOne() != 0)
      AddAdvice(kAdMassTransitOnStrikeAdviceId, kAdReplacementTransitBudget);
    else if (static_cast<u8>(traffic->GetRailFundingDeficit()) > 10 || rail_funding < rail_funding_medium)
      AddAdvice(kAdTransitStrikeBrewingAdviceId, kAdReplacementTransitBudget);
    else if (rail_funding <= rail_funding_high)
      AddAdvice(kAdMassTransitFundingOnTrackAdviceId, kAdReplacementTransitBudget);
    else
      AddAdvice(kAdMassTransitFundingTopNotchAdviceId, kAdReplacementTransitBudget);
  } else
    AddAdvice(kAdTheMassTransitBudgetAdviceId, kAdReplacementTransitBudget);
  if (network_110 + network_10c > 2) {
    u32 total = rail_total, usage = rail_usage;
    if (total <= usage || total <= rail_usage_medium) {
      if (3u * total < usage)
        AddAdvice(kAdTransitSystemOffTrackAdviceId, kAdReplacementTransitSystem);
    } else
      AddAdvice(kAdMassPraiseForMassTransitAdviceId, kAdReplacementTransitSystem);
  }
  if (network_108 + network_104 != 0) {
    u32 usage = rail_usage;
    if (usage <= road_traffic_high) {
      if (usage <= road_traffic_medium) {
        if (road_traffic <= 0.2f)
          AddAdvice(kAdNoTrafficProblemsAdviceId, kAdReplacementTrafficCongestion);
      } else if (network_110 + network_10c != 0 ||
          static_cast<u32>(subway_stations) + static_cast<u32>(subway_rail_stations) > 0 || bus_stations > 0)
        AddAdvice(kAdTrafficVeryBadAdviceId, kAdReplacementTrafficCongestion);
      else
        AddAdvice(kAdTrafficCongestedAdviceId, kAdReplacementTrafficCongestion);
    } else
      AddAdvice(kAdSeriousTrafficProblemsAdviceId, kAdReplacementTrafficCongestion);
  }
  if (road_traffic > 0.2f)
    AddAdvice(kAdSimsCanTGetToWorkAdviceId, 0);
  if (zone_14 != 0 && zone_18 != 0 && static_cast<u32>(airports) == 0)
    AddAdvice(kAdAirportNotDevelopingAdviceId, 0);
  return true;
}

void AdTransportationAdvisor::CheckPetitioners() {
  AdRandom random(kScRandomSeedFromClock);
  if (population < 100)
    return;
  if (traffic->Flag108IsOne() != 0)
    AddPetitioner(kAdTransitWorkersStrikePetitionerId, kAdPetitionReplacementTransitStrike, 6);
  else if (static_cast<u8>(traffic->GetRailFundingDeficit()) > 15)
    AddPetitioner(kAdThreatOfTransitStrikePetitionerId, kAdPetitionReplacementTransitStrike, 6);
  if (rail_usage > carpool_level && year >= carpool_year && ordinance_carpool == 0 &&
      !ReactivatePetitioner(kAdMandatoryCarpoolOrdinanceProposedPetitionerId) && random.Next(100) == 0)
    AddPetitioner(kAdMandatoryCarpoolOrdinanceProposedPetitionerId, 0, 120);
  if (rail_usage > crossing_guard_level && year > crossing_guard_year && ordinance_crossing_guard == 0 &&
      !ReactivatePetitioner(kAdCrossingGuardOrdinancePetitionerId) && random.Next(100) == 0)
    AddPetitioner(kAdCrossingGuardOrdinancePetitionerId, 0, 120);
  if (rail_usage > alternate_day_level && year > alternate_day_year &&
      ordinances->QueryOrdinanceAvailableAndOff(kCtOrdinance_AlternateDayDriving) &&
      !ReactivatePetitioner(kAdAlternateDayDrivingPetitionerId) && random.Next(100) == 0)
    AddPetitioner(kAdAlternateDayDrivingPetitionerId, 0, 120);
  if (rail_usage > bus_stop_level && bus_stations < 2 && bus_stations != -1)
    AddPetitioner(kAdCityStuckInTrafficPetitionerId, 0, 24);
  if (rail_usage > road_traffic_high && network_10c + network_110 < 2)
    AddPetitioner(bus_stations >= 2 ? kAdTrainStationsNeededPetitionerId : kAdTooMuchTrafficPetitionerId, 0, 24);
  if (rail_total > rail_usage_high) {
    if (network_10c > 2 && static_cast<u32>(rail_stations) + static_cast<u32>(subway_rail_stations) > 2)
      AddPetitioner(kAdTrainsTooCrowdedPetitionerId, 0, 24);
    if (network_110 > 2 && static_cast<u32>(subway_stations) + static_cast<u32>(subway_rail_stations) > 2 &&
        !ReactivatePetitioner(kAdCityStreetsAreCloggedPetitionerId) &&
        !ReactivatePetitioner(kAdSubwaysCrowdedPetitionerId)) {
      u32 choice = random.Next(2);
      if (choice == 0)
        AddPetitioner(kAdCityStreetsAreCloggedPetitionerId, 0, 24);
      if (choice <= 1)
        AddPetitioner(kAdSubwaysCrowdedPetitionerId, 0, 24);
    }
  }
}

// The traffic transition is unconditional within the accepted-ID range, even when the department is absent.
void AdTransportationAdvisor::RespondToPetitioner(u32 id, i32 button) {
  if (button == 1 && id >= 163 && id <= 164) {
    EcDepartmentBudget *department = funding_variables[1];
    if (department && department->GetFundingPercentage() < 110)
      funding_variables[1]->SetFundingPercentage(110);
    traffic->BeginFlag108Phase();
  }
}

// Execute one pending transportation tick in its original sample/advice/invention/queue/petition order.
// Post completion after forced advice and clear pending even when callbacks changed it during the tick.
bool AdTransportationAdvisor::DoAdvisorSimulation() {
  if (simulation_pending == 1) {
    GetSimulationData();
    CheckOnAdvice();
    AnnounceInventions();
    advice->Advance();
    advice->ResetRedisplay();
    CheckPetitioners();
    ProcessForcedAdvice();
    ScMessage message = {kWnMsg_AdviceChanged, kAdTransportationAdvisorId, 0, 0};
    AdGetMessageServer()->MessagePost(message, false);
  }
  simulation_pending = 0;
  return true;
}

// Bind actual transportation city services and create the retained road/rail budget departments before configuration.
// Preserve each original missing-service cleanup choice, phase registration and independent notification acquisition.
bool AdTransportationAdvisor::Init(CtCity *input) {
  if (!input)
    return false;
  simulation_city = input;
  input->AddRef();
  CtLayer *layer = simulation_city->GetSpecificLayer(kNwManagerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_NwManager, reinterpret_cast<void **>(&networks));
  layer = simulation_city->GetSpecificLayer(kNwTransitLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_NwTrafficLayer, reinterpret_cast<void **>(&traffic));
  layer = simulation_city->GetSpecificLayer(kEcBudgetLayerType);
  if (!layer) {
    Shutdown();
    return false;
  }
  layer->QueryInterface(IID_EcBudgetLayer, reinterpret_cast<void **>(&budget));
  layer = simulation_city->GetSpecificLayer(kCtOrdinancesLayerType);
  if (!layer) {
    Shutdown();
    return false;
  }
  layer->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances));
  zones = simulation_city->ZoneLayer();
  if (!zones) {
    Shutdown();
    return false;
  }
  zones->AddRef();
  funding_variables[0] =
      new AdRoadBudget(kEcRoadDepartmentId, AdTextHandle(kRsGameRoadsText, kAdAdvisorLabelGroup).ToString());
  funding_variables[0]->AddRef();
  funding_variables[1] =
      new AdRailBudget(kEcRailDepartmentId, AdTextHandle(kRsGameMassTransitText, kAdAdvisorLabelGroup).ToString());
  funding_variables[1]->AddRef();
  layer = simulation_city->GetSpecificLayer(kEcDemandLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves));
  layer = simulation_city->GetSpecificLayer(kEcResidentialLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
  layer = simulation_city->GetSpecificLayer(kGeBuildingControllerLayerType);
  if (!layer)
    return false;
  layer->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
  if (AdBaseAdvisor::ordinances)
    InitializeOrdinances();
  budget->AddDepartmentBudget(funding_variables[0]);
  budget->AddDepartmentBudget(funding_variables[1]);
  LoadConfiguration();
  callback3 = new AdTransportationCallback3(this);
  callback4 = new AdTransportationCallback4(this);
  CtAgentType *node = AdGetAgentTree()->GetNode(kCtAgentType_TransportationAdvisorAgent);
  SetAgentType(node);
  node->AttachManipulator(callback3, 3);
  node->AttachManipulator(callback4, 4);
  ScMessageTarget *target = static_cast<CtLayer *>(this);
  AdGetMessageServer()->AddNotification(target, kCtMsg_OrdinanceAvailable);
  AdGetMessageServer()->AddNotification(target, kApMsgPostCityChange);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdForceMoeToSayCheatId, AdForceMoeToSay);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdForceMoeToSayCheatId);
  AdGetApplication()->GetCheatCodeManager()->RegisterCheatCode(kAdStopForcingAdviceCheatId, AdStopForcingAdvice);
  AdGetApplication()->GetCheatCodeManager()->AddNotification(target, kAdStopForcingAdviceCheatId);
  return AdBaseAdvisor::Init(input);
}

// Construct the zero-reference transportation object and return its original secondary advisor identity.
// The registered director factory protocol performs its own query and cleanup afterward.
ScUnknown *AdCreateTransportation() {
  return static_cast<AdAdvisor *>(new AdTransportationAdvisor);
}
