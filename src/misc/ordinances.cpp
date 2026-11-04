#include "misc/ordinances.h"

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/sc_random.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/api.h"
#include "misc/api.h"
#include "misc/module.h"
#include "misc/sc_graph_info.h"
#include "resources/api.h"
#include "resources/stream.h"

// Maxis' retained advisor-script table names these IDs omitted from the installed ordinance settings.
// The original cost switch retains their cases; a missing rule still returns zero before that dispatch.
enum CtRetainedOrdinanceId : u32 {
  kCtOrdinance_PoliceOutreach = 0xa2bf1f60,
  kCtOrdinance_BridgeTolls = 0x82bf1e0e,
  kCtOrdinance_WaterTowerAds = 0x62bf1e21,
};

// Serialized record identities remain distinct from interface and layer lookup IDs with equal bits.
static constexpr u32 kCtOrdinancesRecordType = 0x41193c3a;
static constexpr u32 kCtOrdinancesRecordGroup = 0xe1193c2a;

// Construct both interfaces and the empty rule map with the original tuning defaults and tail reference count.
// Native zeros exclude the source's unwritten world pointer and later tracking/unemployment scratch.
CtOrdinances::CtOrdinances()
    : city(nullptr), residential(nullptr), buildings(nullptr), world(nullptr), unemployment_threshold(10),
      building_threshold(300), commercial_threshold(25), aerospace_year(1960), day(0), year(1990), count_12360(0),
      count_12210(0), count_12230(0), count_12310(0), count_5000(0), count_13130(0), count_13010(0), total_tracked(0),
      count_14130(0), count_12851(0), unemployment(0), monthly_income(0), monthly_expense(0), initialized(false),
      references(0) {
}

CtOrdinances::~CtOrdinances() {
  if (initialized)
    Shutdown();
}

// Publish the original layer identity even for Unknown; only the ordinance IID selects the primary interface.
// Unsupported queries clear the output, while accepted queries retain through the live primary method.
bool CtOrdinances::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtOrdinanceLayer, CtOrdinanceLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtOrdinances::AddRef() {
  return ++references;
}

u32 CtOrdinances::Release() {
  return ScReleaseImpl(this, &references);
}

// Remove cheat registration and notification before releasing city, residential, building and world holds in order.
// Delete rules in unsigned key order before retiring each map node, then clear initialized last.
bool CtOrdinances::Shutdown() {
  ScGetMiscApplication()->GetCheatCodeManager()->UnregisterCheatCode(kCtCheat_AllOrdinances);
  ScGetMiscApplication()->GetCheatCodeManager()->RemoveNotification(
      static_cast<CtLayer *>(this), kCtCheat_AllOrdinances);
  ScRelease(&city);
  ScRelease(&residential);
  ScRelease(&buildings);
  ScRelease(&world);
  while (!rules.empty()) {
    auto entry = rules.begin();
    delete entry->second;
    rules.erase(entry);
  }
  initialized = false;
  return true;
}

u32 CtOrdinances::GetLayerType() {
  return kCtOrdinancesLayerType;
}

bool CtOrdinances::QueryOrdinanceOn(u32 id) {
  auto entry = rules.find(id);
  return entry != rules.end() && entry->second->on != 0;
}

bool CtOrdinances::QueryOrdinanceAvailable(u32 id) {
  auto entry = rules.find(id);
  return entry != rules.end() && entry->second->available != 0;
}

bool CtOrdinances::QueryOrdinanceAvailableAndOff(u32 id) {
  auto entry = rules.find(id);
  return entry != rules.end() && entry->second->available == 1 && entry->second->on == 0;
}

u32 CtOrdinances::TotalOrdinancesOn() {
  u32 count = 0;
  for (auto entry = rules.begin(); entry != rules.end(); ++entry)
    if (entry->second->on == 1)
      ++count;
  return count;
}

// Count available rules in the requested category without filtering their on state.
u32 CtOrdinances::GetOrdinanceListSize(u32 category) {
  u32 count = 0;
  for (auto entry = rules.begin(); entry != rules.end(); ++entry)
    if (entry->second->category == category && entry->second->available == 1)
      ++count;
  return count;
}

u32 CtOrdinances::GetOridnanceListSize(i32 category) {
  return GetOrdinanceListSize(u32(category));
}

// Fill the caller-provided row objects in unsigned key order, borrowing names without acquiring references.
// Return success when at least one matching available rule was copied; the second argument is a category, not capacity.
bool CtOrdinances::GetOrdinanceList(CtOrdinance **out, u32 category) {
  u32 count = 0;
  for (auto entry = rules.begin(); entry != rules.end(); ++entry) {
    CtOrdinanceRule *rule = entry->second;
    if (rule->category == category && rule->available == 1) {
      CtOrdinance *row = out[count++];
      row->id = entry->first;
      row->name = &rule->name;
      row->on = rule->on;
      row->monthly_cost = rule->monthly_cost;
    }
  }
  return count != 0;
}

// Store a changed on byte before querying its live cost, then adjust income or expense and publish the change.
// Turning off classifies by current cost and removes the stored previous amount, preserving callback order.
bool CtOrdinances::SetOrdinance(u32 id, bool on) {
  auto entry = rules.find(id);
  if (entry == rules.end())
    return false;
  if (entry->second->on != u8(on)) {
    entry->second->on = u8(on);
    i32 cost = i32(GetOrdinanceCost(id));
    if (on) {
      if (cost >= 0)
        monthly_income += u32(cost);
      else
        monthly_expense -= u32(cost);
      rules[id]->monthly_cost = u32(cost);
    } else {
      if (cost >= 0)
        monthly_income -= rules[id]->monthly_cost;
      else
        monthly_expense += rules[id]->monthly_cost;
      rules[id]->monthly_cost = 0;
    }
    ScMessage message(kCtMsg_OrdinanceChanged, id, u8(on), 0);
    ScGetMiscMessageServer()->MessagePost(message, false);
  }
  return true;
}

bool CtOrdinances::MakeOrdinanceAvailable(u32 id) {
  auto entry = rules.find(id);
  if (entry == rules.end() || entry->second->available != 0)
    return false;
  ScMessage message(kCtMsg_OrdinanceAvailable, id, 0, 0);
  ScGetMiscMessageServer()->MessagePost(message, false);
  rules[id]->available = 1;
  return true;
}

u32 CtOrdinances::GetTotalMonthlyOrdinanceExpense() {
  return monthly_expense;
}

u32 CtOrdinances::GetTotalMonthlyOrdinanceIncome() {
  return monthly_income;
}

bool CtOrdinances::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool CtOrdinances::StaticInit(CtDefinition *) {
  return false;
}

bool CtOrdinances::StaticShutdown() {
  return false;
}

bool CtOrdinances::Init(CtCity *, ScImporter *) {
  return true;
}

bool CtOrdinances::SimulationEnd() {
  return true;
}

bool CtOrdinances::DebugClassTag(ScStringView *) {
  return false;
}

bool CtOrdinances::DebugTypeTag(ScStringView *) {
  return false;
}

static const ScString CtOrdinanceCheatName("I love red tape");

// Extract the next semicolon-delimited field and consume its delimiter. Missing delimiters leave the source intact.
static ScString CtTakeOrdinanceToken(ScString *input) {
  for (char *byte = input->begin; byte != input->end; ++byte)
    if (*byte == ';') {
      u32 length = u32(byte - input->begin);
      ScString token(input->begin, length);
      input->Erase(0, length + 1);
      return ScString(token);
    }
  return ScString();
}

static ScString CtReadOrdinanceName(CtMiscTextResource *resource) {
  if (resource->text) {
    ScString expanded(*resource->text);
    CtExpandMiscTokens(&expanded);
    return ScString(expanded);
  }
  ScString empty;
  return ScString(empty);
}

// Parse every numeric field before rejecting zero IDs/categories, then create the actual owned rule and localized name.
// Replacing a duplicate map key retains the original displaced-record leak and resource-release ordering.
void CtOrdinances::OrdinanceLayerAdd(const ScString &key, const ScString &value, void *context) {
  ScString remaining(value), token;
  u32 id = ScParseMiscNumber(key.ToChar());
  token = CtTakeOrdinanceToken(&remaining);
  u32 category = ScParseMiscNumber(token.begin);
  token = CtTakeOrdinanceToken(&remaining);
  u32 starting_year = ScParseMiscNumber(token.begin);
  token = CtTakeOrdinanceToken(&remaining);
  u32 name = ScParseMiscNumber(token.begin), divisor = ScParseMiscNumber(remaining.begin);
  if (id && category) {
    CtOrdinanceRule *rule =
        new CtOrdinanceRule{category, 0, u8(starting_year == 0), starting_year, ScString(), 0, divisor};
    CtMiscTextResource resource(name, kAdAdvisorNameGroup);
    rule->name = CtReadOrdinanceName(&resource);
    CtOrdinances *owner = static_cast<CtOrdinances *>(context);
    owner->rules[id] = rule;
  }
}

static u8 CtReadOrdinanceSetting(EcIni *ini, const char *name, ScString *out) {
  ScString key(name), section("ORDINANCE_TRIGGERS");
  return ini->GetValue(&section, &key, out);
}

// Append enumerated rules before reading all four triggers, retaining previous values for missing settings.
bool CtOrdinances::LoadOrdinanceINI() {
  bool failed = false;
  ScString path, unused, value;
  EcIni ini;
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\Ordinances.INI");
  ini.SelectFile(&path);
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SYS.PAK");
  ini.SelectPackage(path);
  {
    ScString section("ListOfOrdinances");
    ini.EnumerateSection(&section, OrdinanceLayerAdd, this);
  }
  if (CtReadOrdinanceSetting(&ini, "HOMELESS_SHELTER_PERCENT", &value))
    unemployment_threshold = ScParseMiscNumber(value.begin);
  else
    failed = true;
  if (CtReadOrdinanceSetting(&ini, "EARTHQUAKE_PREP_BUILDINGS", &value))
    building_threshold = ScParseMiscNumber(value.begin);
  else
    failed = true;
  if (CtReadOrdinanceSetting(&ini, "TOURIST_PROMOTION_BUILDINGS", &value))
    commercial_threshold = ScParseMiscNumber(value.begin);
  else
    failed = true;
  if (CtReadOrdinanceSetting(&ini, "AEROSPACE_AVAILABLE_YEAR", &value))
    aerospace_year = ScParseMiscNumber(value.begin);
  else
    failed = true;
  return !failed;
}

// Acquire the three layer interfaces in sequence, preserving ignored query results and partial failure cleanup.
// Load tuning before registering the cheat and notification; publish initialized only after both external calls.
bool CtOrdinances::Init(CtCity *owner) {
  if (initialized)
    Shutdown();
  if (owner) {
    city = ScAddRefNotNull(owner);
    CtLayer *specific = city->GetSpecificLayer(kEcResidentialLayerType);
    if (specific) {
      specific->QueryInterface(IID_CtResidentialLayer, reinterpret_cast<void **>(&residential));
      specific = city->GetSpecificLayer(kGeBuildingControllerLayerType);
      if (specific) {
        specific->QueryInterface(IID_GeBuildingLayer, reinterpret_cast<void **>(&buildings));
        specific = city->GetSpecificLayer(kCtWorldLayerType);
        if (specific) {
          specific->QueryInterface(IID_CtWorldLayer, reinterpret_cast<void **>(&world));
          LoadOrdinanceINI();
          ScGetMiscApplication()->GetCheatCodeManager()->RegisterCheatCode(
              kCtCheat_AllOrdinances, CtOrdinanceCheatName);
          ScGetMiscApplication()->GetCheatCodeManager()->AddNotification(
              static_cast<CtLayer *>(this), kCtCheat_AllOrdinances);
          initialized = true;
          return true;
        }
      }
    }
  }
  Shutdown();
  return false;
}

// Query live population and total building area before looking up the rule, including for unknown IDs.
// Apply the original ID-specific cost formula and preserve signed income/expense meaning as complete DWORD return bits.
u32 CtOrdinances::GetOrdinanceCost(u32 id) {
  u32 population = u32(residential->GetPopulation());
  u32 area = buildings->GetTotalAreaOfBuildings();
  auto entry = rules.find(id);
  if (entry == rules.end())
    return 0;
  u32 divisor = entry->second->cost_divisor;
  if (!divisor)
    divisor = 100;
  switch (id) {
  case kCtOrdinance_PublicSmokingBan:
  case kCtOrdinance_AlternateDayDriving:
  case kCtOrdinance_CleanIndustryAssociation:
  case kCtOrdinance_BiotechTaxIncentives:
  case kCtOrdinance_PublicAccessCable:
  case kCtOrdinance_TireRecycling:
  case kCtOrdinance_MandatoryWaterMeters:
  case kCtOrdinance_NuclearFreeZone:
  case kCtOrdinance_CarSmogging:
  case kCtOrdinance_ElectronicsJobFair:
  case kCtOrdinance_CrossingGuards:
  case kCtOrdinance_ConservationCorps:
  case kCtOrdinance_CarpoolIncentive:
  case kCtOrdinance_ShuttleService:
  case kCtOrdinance_CleanAir:
  case kCtOrdinance_StairwellLighting:
  case kCtOrdinance_FreeClinics:
  case kCtOrdinance_HomelessShelters:
  case kCtOrdinance_CommunityCPRTraining:
  case kCtOrdinance_WaterConservation:
  case kCtOrdinance_ElectronicsTaxIncentive:
  case kCtOrdinance_AerospaceTaxIncentive:
  case kCtOrdinance_LandfillGasRecovery:
  case kCtOrdinance_BackyardComposting:
  case kCtOrdinance_PaperReduction:
  case kCtOrdinance_JuniorSports:
  case kCtOrdinance_LawnChemicalBan:
  case kCtOrdinance_NeighborhoodWatch:
  case kCtOrdinance_TouristPromotion:
    return 0u - 1 - population / divisor;
  case kCtOrdinance_IndustrialWasteDisposal:
  case kCtOrdinance_LegalizedGambling:
  case kCtOrdinance_IndustrialPollutionFee:
  case kCtOrdinance_ParkingFines:
    return population / divisor;
  case kCtOrdinance_EarthquakeRetrofitting:
  case kCtOrdinance_PowerConservation:
  case kCtOrdinance_MandatorySmokeDetectors:
    return 0u - 1 - area / divisor;
  case kCtOrdinance_YouthCurfew:
  case kCtOrdinance_PoliceOutreach:
    return 0u - 1 - population / divisor - u32(count_13130);
  case kCtOrdinance_ProReading:
    return 0u - 1 - population / divisor - u32(count_14130);
  case kCtOrdinance_TrashPresort:
    return 0u - 1 - population / divisor - u32(count_12851);
  case kCtOrdinance_BridgeTolls:
  case kCtOrdinance_WaterTowerAds:
    return 1;
  default:
    return 0;
  }
}

// Keep tracker reads ordered and preserve the two final calls through one captured building receiver.
void CtOrdinances::RefreshData() {
  city->GetDate(&day, &year);
  count_12360 = buildings->CheckOccupantTracking(12360);
  count_12210 = buildings->CheckOccupantTracking(12210);
  count_12230 = buildings->CheckOccupantTracking(12230);
  count_12310 = buildings->CheckOccupantTracking(12310);
  count_5000 = buildings->CheckOccupantTracking(5000);
  count_13130 = buildings->CheckOccupantTracking(13130);
  count_13010 = buildings->CheckOccupantTracking(13010);
  count_14130 = buildings->CheckOccupantTracking(14130);
  count_12851 = buildings->CheckOccupantTracking(12851);
  GeBuildingLayer *source = buildings;
  i32 residential_count = source->CheckOccupantTracking(3000);
  i32 industrial_count = source->CheckOccupantTracking(8000);
  total_tracked = u32(count_12210) + u32(count_12310) + u32(count_13130) + u32(count_14130) + u32(count_12360) +
      u32(count_12230) + u32(count_5000) + u32(industrial_count) + u32(residential_count);
  if (world)
    world->GetUnemploymentHistory()->GetMonthData(&unemployment);
}

// Restore existing rule flags only after each complete ID/on/available row, normalizing both loaded bytes.
// Preserve record close/abort decisions and partial updates; native scratch excludes the source's unwritten outputs.
bool CtOrdinances::Init(CtCity *, RsDatabaseSegment *segment) {
  RsKey key = {kCtOrdinancesRecordType, kCtOrdinancesRecordGroup, 0};
  RsRecord *record = nullptr;
  bool valid = segment->OpenRecord(key, &record);
  if (valid) {
    void *selected;
    if (record->QueryInterface(IID_RsStream, &selected)) {
      RsStream *stream = static_cast<RsStream *>(selected);
      u32 count = 0, id = 0;
      u8 on = 0, available = 0;
      valid = stream->ReadU32(&count);
      for (; count && valid; --count) {
        valid = stream->ReadU32(&id);
        valid = valid && stream->ReadU8(&on) && stream->ReadU8(&available);
        if (valid) {
          auto entry = rules.find(id);
          if (entry != rules.end()) {
            entry->second->on = on != 0;
            entry->second->available = available != 0;
          }
        }
      }
      stream->Release();
    }
    if (valid)
      return segment->CloseRecord(&record);
  }
  segment->AbortRecord(&record);
  return false;
}

// Write the live rule count and ordered IDs with their raw state bytes, stopping after the first failed stream write.
// Always close a successfully opened record after retiring its stream; the close reply does not replace stream success.
bool CtOrdinances::Save(CtCity *, RsDatabaseSegment *segment) {
  RsKey key = {kCtOrdinancesRecordType, kCtOrdinancesRecordGroup, 0};
  RsRecord *record;
  if (!segment->OpenRecordForWrite(key, &record))
    return false;
  bool valid = false;
  void *selected;
  if (record->QueryInterface(IID_RsStream, &selected)) {
    RsStream *stream = static_cast<RsStream *>(selected);
    valid = stream->WriteU32(u32(rules.size()));
    auto entry = rules.begin(), end = rules.end();
    while (entry != end && valid) {
      valid = stream->WriteU32(entry->first);
      valid = valid && stream->WriteU8(entry->second->on) && stream->WriteU8(entry->second->available);
      ++entry;
    }
    stream->Release();
    selected = nullptr;
  }
  segment->CloseRecord(&record);
  return valid;
}

CtOrdinanceManipulator::CtOrdinanceManipulator(CtOrdinances *owner) : ordinances(owner) {
}

CtOrdinanceManipulator::~CtOrdinanceManipulator() {
  ordinances = nullptr;
}

// Publish the same scheduler identity for Unknown and the manipulator IID, retaining through its live primary method.
// Unsupported requests clear the output.
bool CtOrdinanceManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtOrdinanceManipulator::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 CtOrdinanceManipulator::Release() {
  return ScMiscRefCount::Release();
}

bool CtOrdinanceManipulator::Apply(CtAgent *) {
  ordinances->EndOfMonth();
  return true;
}

bool CtOrdinanceManipulator::IsLockingCity() {
  return true;
}

bool CtOrdinanceManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool CtOrdinanceManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool CtOrdinanceManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Publish flags four and a null result before inspecting the type, then construct the matching monthly scheduler.
// The new result keeps its original zero initial reference count and borrowed ordinance owner.
bool CtOrdinances::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent)
    *out = new CtOrdinanceManipulator(this);
  return true;
}

void CtOrdinances::MakeOldOrdinancesAvailable() {
  auto entry = rules.begin(), end = rules.end();
  u32 current_day = 0, current_year = 0;
  city->GetDate(&current_day, &current_year);
  for (; entry != end; ++entry)
    if (entry->second->starting_year <= current_year)
      entry->second->available = 1;
}

bool CtOrdinances::CheckForNewOrdinances() {
  ScRandom random(kScRandomSeedFromClock);
  if (random.Bounded(100) >= 10)
    return false;
  for (auto entry = rules.begin(); entry != rules.end(); ++entry) {
    CtOrdinanceRule *rule = entry->second;
    if (rule->available)
      continue;
    bool available = false;
    if (rule->starting_year != 9999) {
      available = rule->starting_year < year;
    } else {
      switch (entry->first) {
      case kCtOrdinance_HomelessShelters:
        available = unemployment > unemployment_threshold;
        break;
      case kCtOrdinance_AerospaceTaxIncentive:
        available = count_12310 > 0 && year > aerospace_year;
        break;
      case kCtOrdinance_TouristPromotion:
        available = u32(count_5000) > commercial_threshold;
        break;
      case kCtOrdinance_ShuttleService:
        available = i32(u32(count_12230) + u32(count_12210) + u32(count_12360)) > 2;
        break;
      case kCtOrdinance_YouthCurfew:
        available = count_13130 > 0;
        break;
      case kCtOrdinance_EarthquakeRetrofitting:
        available = total_tracked > building_threshold;
        break;
      case kCtOrdinance_SubsidizedMassTransit:
        available = i32(u32(count_12210) + u32(count_12360)) > 2;
        break;
      }
    }
    if (available) {
      entry->second->available = 1;
      ScMessage message(kCtMsg_OrdinanceAvailable, entry->first, 0, 0);
      ScGetMiscMessageServer()->MessagePost(message, false);
      return true;
    }
  }
  return false;
}

// Refresh tracking and unlock past-year rules, then announce every available rule before recomputing active costs.
// Only exact on/available bytes of one contribute; costs retain their signed bits in the unsigned stored totals.
bool CtOrdinances::SimulationBegin() {
  RefreshData();
  MakeOldOrdinancesAvailable();
  auto end = rules.end();
  for (auto entry = rules.begin(); entry != end; ++entry)
    if (entry->second->available) {
      ScMessage message(kCtMsg_OrdinanceAvailable, entry->first, 0, 0);
      ScGetMiscMessageServer()->MessagePost(message, false);
    }
  monthly_income = 0;
  monthly_expense = 0;
  for (auto entry = rules.begin(); entry != end; ++entry)
    if (entry->second->available == 1 && entry->second->on == 1) {
      u32 cost = GetOrdinanceCost(entry->first);
      entry->second->monthly_cost = cost;
      if (i32(cost) >= 0)
        monthly_income += cost;
      else
        monthly_expense -= cost;
    }
  return true;
}

// Refresh monthly costs before offering one new ordinance, then post the original totals-changed notification.
u8 CtOrdinances::EndOfMonth() {
  RefreshData();
  monthly_expense = 0;
  monthly_income = 0;
  for (auto entry = rules.begin(); entry != rules.end(); ++entry)
    if (entry->second->available == 1 && entry->second->on == 1) {
      u32 cost = GetOrdinanceCost(entry->first);
      entry->second->monthly_cost = cost;
      if (i32(cost) >= 0)
        monthly_income += cost;
      else
        monthly_expense -= cost;
    }
  CheckForNewOrdinances();
  ScMessage message(kCtMsg_OrdinanceCostsUpdated, 0, 0, 0);
  ScGetMiscMessageServer()->MessagePost(message, false);
  return 1;
}

// Unlock each currently unavailable rule before posting its notification, preserving map traversal after callbacks.
void CtOrdinances::MakeAllOrdinancesAvailable() {
  auto end = rules.end();
  for (auto entry = rules.begin(); entry != end; ++entry)
    if (!entry->second->available) {
      entry->second->available = 1;
      ScMessage message(kCtMsg_OrdinanceAvailable, entry->first, 0, 0);
      ScGetMiscMessageServer()->MessagePost(message, false);
    }
}

// Unlock the rules for the matching cheat identity. Acknowledge other messages without changing ordinance state.
bool CtOrdinances::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand && u32(message->data1) == kCtCheat_AllOrdinances)
    MakeAllOrdinancesAvailable();
  return true;
}
