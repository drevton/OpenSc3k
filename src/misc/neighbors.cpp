#include "misc/neighbors.h"

#include <stdlib.h>

#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/sc_random.h"
#include "city/sc_stopwatch.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/api.h"
#include "misc/api.h"
#include "misc/module.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/neighbor_window.h"

// A negative deal term marks the deal canceled for the original canceled-deal lookup.
enum CtNeighborDealTermMarker : u32 {
  kCtCanceledNeighborDealTerm = 0xffffffff,
};

// Neighbor deals convert the configured population percentage to the original quantity-group count.
enum CtDealPopulationScale : u32 {
  kCtDealPopulationPercentageDivisor = 0x186a0,
};

// Each generated local neighbor starts with the original population ceiling.
enum CtNeighborPopulationLimit : u32 {
  kCtNeighborInitialPopulationLimit = 0x3d0900,
};

// Serialized record identities remain distinct from interface and layer lookup IDs with equal bits.
enum CtNeighborArchiveKey : u32 {
  kCtNeighborsRecordType = 0xc2910e7d,
  kCtNeighborsRecordGroup = 0x82937b60,
};

// Neighbor text follows the containing city's last-save language on input and the current save language on output.
// Keep the byte-oriented reference build free of added service calls.
static u32 CtNeighborTextLanguage([[maybe_unused]] CtCity *city, [[maybe_unused]] bool writing) {
  if (writing)
    return ScGetMiscLanguageManager()->GetCurrentLanguage();
  i32 language, country;
  city->GetLastLanguageAndCountry(&language, &country);
  return static_cast<u32>(language);
}

CtNeighbors::CtNeighbors()
    : references(0), city(nullptr), buildings(nullptr), pollution(nullptr), power(nullptr), plumbing(nullptr),
      residential(nullptr), field428(1) {
}

CtNeighbors::~CtNeighbors() {
  CtNeighbors::Shutdown();
}

// Retain the city and each required live service in original acquisition order, then capture both map dimensions.
// A missing provider invokes the live Shutdown operation and leaves the original failure predicate.
bool CtNeighbors::Init(CtCity *input) {
  if (!input)
    return false;
  city = input;
  input->AddRef();
  buildings = city->BuildingLayer();
  if (buildings) {
    buildings->AddRef();
    pollution = city->PollutionLayer();
    if (pollution) {
      pollution->AddRef();
      power = city->PowerLayer();
      if (power) {
        power->AddRef();
        residential = city->ResidentialLayer();
        if (residential) {
          residential->AddRef();
          plumbing = city->PlumbingLayer();
          if (plumbing) {
            plumbing->AddRef();
            width = city->CellCountX();
            height = city->CellCountZ();
            penaltyExpense = 0;
            return true;
          }
        }
      }
    }
  }
  Shutdown();
  return false;
}

// Retire each live retained service in original order and clear both deal rosters afterward.
// Connection sets, neighbor records and map dimensions survive this operation until later initialization or
// destruction.
bool CtNeighbors::Shutdown() {
  ScRelease(&city);
  ScRelease(&pollution);
  ScRelease(&buildings);
  ScRelease(&power);
  ScRelease(&residential);
  ScRelease(&plumbing);
  activeDeals.clear();
  pendingDeals.clear();
  return true;
}

bool CtNeighbors::DoMessage(ScMessage *) {
  return false;
}

bool CtNeighbors::GetManipulator(const CtAgentType &, CtLayerManipulator **, i32 *) {
  return true;
}

u32 CtNeighbors::GetLayerType() {
  return kCtNeighborsLayerType;
}

bool CtNeighbors::DebugClassTag(ScStringView *) {
  return false;
}

bool CtNeighbors::DebugTypeTag(ScStringView *) {
  return false;
}

bool CtNeighbors::DebugInstanceTag(ScStringView *) {
  return false;
}

i32 CtNeighbors::MinimumExecutionInterval() {
  return 4;
}

bool CtNeighbors::IsAgentLocking() {
  return false;
}

u32 CtNeighbors::TypeId() const {
  return kCtNeighborsLayerType;
}

// Return the original absent agent-type identity without manufacturing a retained type object.
CtAgentType *CtNeighbors::AgentType() {
  return nullptr;
}

bool CtNeighbors::SetAgentType(CtAgentType *) {
  return false;
}

u8 CtNeighbors::NeighborFlag(u8 index) {
  return index < 4 ? neighbors[index].available : 0;
}

// Copy the owned neighbor name through the caller's live String operation and ignore that operation's return.
// Refused indices leave the output untouched.
bool CtNeighbors::GetNeighborName(u8 index, ScStringView *out) {
  if (index >= 4)
    return false;
  out->Copy(neighbors[index].name);
  return true;
}

// Copy the separate mayor name for an accepted index and preserve the original raw-byte success transport.
u8 CtNeighbors::GetNeighborMayorName(u8 index, ScStringView *out) {
  if (index >= 4)
    return 0;
  out->Copy(neighbors[index].mayor);
  return 1;
}

u32 CtNeighbors::GetNeighborPopulation(u8 index) {
  return index < 4 ? neighbors[index].population : 0;
}

// Count available neighbors by testing each full raw flag byte for nonzero.
u32 CtNeighbors::CountNeighbors() {
  u32 count = 0;
  for (const CtNeighbor &neighbor : neighbors)
    if (neighbor.available)
      ++count;
  return count;
}

// Count the four entries whose original DWORD140 water-connection count is nonzero.
u32 CtNeighbors::CountNeighborsWithField140() {
  u32 count = 0;
  for (const CtNeighbor &neighbor : neighbors)
    if (neighbor.water)
      ++count;
  return count;
}

u32 CtNeighbors::CountNeighborsWithField144() {
  u32 count = 0;
  for (const CtNeighbor &neighbor : neighbors)
    if (neighbor.power)
      ++count;
  return count;
}

// Count entries with either road or rail connections; the existing canonical name is descriptive rather than original.
u32 CtNeighbors::CountNeighborsWithDeals() {
  u32 count = 0;
  for (const CtNeighbor &neighbor : neighbors)
    if (neighbor.road || neighbor.rail)
      ++count;
  return count;
}

// Count entries with a nonzero rail-connection word independently of the road count.
u32 CtNeighbors::GetRailConnections() {
  u32 count = 0;
  for (const CtNeighbor &neighbor : neighbors)
    if (neighbor.rail)
      ++count;
  return count;
}

// Publish the original primary layer/message/query views or the separate agent identity before retaining. Unsupported
// identifiers clear the output.
bool CtNeighbors::QueryInterface(u32 iid, void **out) {
  switch (iid) {
  case IID_ScUnknown:
  case IID_CtLayer:
  case IID_CtNeighborsLayer:
    *out = static_cast<CtNeighborsLayer *>(this);
    break;
  case IID_ScMessageTarget:
    *out = static_cast<ScMessageTarget *>(static_cast<CtLayer *>(this));
    break;
  case IID_ScQueryTarget:
    *out = static_cast<ScQueryTarget *>(static_cast<CtLayer *>(this));
    break;
  case IID_CtAgent:
    *out = static_cast<CtAgent *>(this);
    break;
  default:
    RETURN_QUERY_INTERFACE_FAILURE();
  }
  AddRef();
  return true;
}

// Retain the object before the real module director, then return the live object counter after that call.
u32 CtNeighbors::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

// Decrement only nonfinal references before module retirement; zero and one both delete after removing the module hold.
u32 CtNeighbors::Release() {
  if (references > 1) {
    --references;
    ScGetMiscDirector()->RemoveRef();
    return references;
  }
  ScGetMiscDirector()->RemoveRef();
  delete this;
  return 0;
}

// Assign a valid neighbor name from a real independent String temporary, preserving input that aliases the old name.
// Out-of-range indices return zero before any input String operation.
u8 CtNeighbors::SetNeighborName(u8 index, const ScStringView &name) {
  if (index >= 4)
    return 0;
  ScString temporary(name);
  neighbors[index].name = temporary;
  return 1;
}

CtNeighborDeal::CtNeighborDeal()
    : value08(0), value0c(0), neighborId(0), dealType(0), value14(0), value18(0), value1c(0), value20(0),
      references(0) {
}

CtNeighborDeal::~CtNeighborDeal() {
}

// Publish the primary Unknown/deal identity before the live retain, clearing rejected outputs.
bool CtNeighborDeal::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtDeal);
  HANDLE_QUERY_INTERFACE(IID_CtDeal, CtDeal);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining its actual module director, then reread the live object count.
u32 CtNeighborDeal::AddRef() {
  ++references;
  ScGetMiscDirector()->AddRef();
  return references;
}

// Release the module after decrementing nonfinal object references; zero and one both take the deleting path.
u32 CtNeighborDeal::Release() {
  if (references > 1) {
    --references;
    ScGetMiscDirector()->RemoveRef();
    return references;
  }
  ScGetMiscDirector()->RemoveRef();
  delete this;
  return 0;
}

u8 CtNeighborDeal::GetFirstName(ScStringView *out) {
  out->Copy(firstName);
  return 1;
}

u8 CtNeighborDeal::GetSecondName(ScStringView *out) {
  out->Copy(secondName);
  return 1;
}

u32 CtNeighborDeal::GetValue08() {
  return value08;
}

u32 CtNeighborDeal::SetValue08(u32 value) {
  value08 = value;
  return value;
}

u32 CtNeighborDeal::GetValue0c() {
  return value0c;
}

u32 CtNeighborDeal::SetValue0c(u32 value) {
  value0c = value;
  return value;
}

u32 CtNeighborDeal::GetValue14() {
  return value14;
}

u32 CtNeighborDeal::SetValue14(u32 value) {
  value14 = value;
  return value;
}

u32 CtNeighborDeal::GetValue18() {
  return value18;
}

u32 CtNeighborDeal::SetValue18(u32 value) {
  value18 = value;
  return value;
}

u32 CtNeighborDeal::GetValue1c() {
  return value1c;
}

u32 CtNeighborDeal::SetValue1c(u32 value) {
  value1c = value;
  return value;
}

u32 CtNeighborDeal::GetValue20() {
  return value20;
}

u32 CtNeighborDeal::SetValue20(u32 value) {
  value20 = value;
  return value;
}

u32 CtNeighborDeal::GetNeighborId() {
  return neighborId;
}

u32 CtNeighborDeal::GetType() {
  return dealType;
}

u8 CtNeighborDeal::SetNeighborId(u8 value) {
  neighborId = value;
  return value;
}

u8 CtNeighborDeal::SetType(u8 value) {
  dealType = value;
  return value;
}

u8 CtNeighborDeal::GetDate(CtDateInterface *out) {
  out->Set(date.serial);
  return 1;
}

// Capture the complete input String in a separate temporary before assigning the first name, preserving self-input.
u8 CtNeighborDeal::SetFirstName(const ScStringView &text) {
  ScString temporary(text);
  firstName = temporary;
  return 1;
}

u8 CtNeighborDeal::SetSecondName(const ScStringView &text) {
  ScString temporary(text);
  secondName = temporary;
  return 1;
}

u8 CtNeighborDeal::SetDate(const CtDateInterface &value) {
  date.serial = value.DayNumber();
  return 1;
}

// Publish borrowed name identities and every original scalar field before deriving the year from the stored date.
// No name hold, descriptor allocation or mutable reference wrapper is introduced.
u8 CtNeighborDeal::GetRecord(CtDealRecord *out) {
  out->first_name = &firstName;
  out->second_name = &secondName;
  out->value_08 = value08;
  out->value_0c = value0c;
  out->value_10 = neighborId;
  out->type = dealType;
  out->value_14 = value14;
  out->value_18 = value18;
  out->value_1c = value1c;
  out->value_20 = value20;
  out->year = date.Year();
  return 1;
}

// Apply the live virtual setters in original order, reloading each descriptor field after preceding callbacks.
// The final date comes from ordinal day one of the supplied year through the existing real date-interface wrapper.
u8 CtNeighborDeal::SetRecord(const CtDealRecord *record) {
  SetFirstName(*record->first_name);
  SetSecondName(*record->second_name);
  SetValue08(record->value_08);
  SetValue0c(record->value_0c);
  SetNeighborId(record->value_10);
  SetType(record->type);
  SetValue14(record->value_14);
  SetValue18(record->value_18);
  SetValue1c(record->value_1c);
  SetValue20(record->value_20);
  EcDate firstDay(1, record->year);
  SetDate(static_cast<const CtDateInterface &>(firstDay));
  return 1;
}

// Copy names and terms through the source's live generic interface in original getter/setter order.
// The final serial date passes through the actual temporary date interface; getter and setter replies are ignored.
u8 CtNeighborDeal::CopyFrom(CtDeal *source) {
  ScString text;
  source->GetFirstName(&text);
  SetFirstName(text);
  source->GetSecondName(&text);
  SetSecondName(text);
  SetValue08(source->GetValue08());
  SetValue0c(source->GetValue0c());
  SetNeighborId(static_cast<u8>(source->GetNeighborId()));
  SetType(static_cast<u8>(source->GetType()));
  SetValue14(source->GetValue14());
  SetValue18(source->GetValue18());
  SetValue1c(source->GetValue1c());
  SetValue20(source->GetValue20());
  EcDate copiedDate;
  source->GetDate(static_cast<CtDateInterface *>(&copiedDate));
  SetDate(static_cast<const CtDateInterface &>(copiedDate));
  return 1;
}

CtDealRef::CtDealRef(CtNeighborDeal *input) : deal(input) {
  if (deal)
    deal->AddRef();
}

CtDealRef::CtDealRef(const CtDealRef &source) : deal(source.deal) {
  if (deal)
    deal->AddRef();
}

CtDealRef::~CtDealRef() {
  if (deal)
    deal->Release();
}

// Borrow this receiver's actual secondary city-agent identity without querying or taking another reference.
CtAgent *CtNeighbors::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Count the four neighbors that currently have at least one road connection.
u32 CtNeighbors::GetRoadConnections() {
  u32 count = 0;
  for (u32 i = 0; i < 4; ++i)
    if (neighbors[i].road)
      ++count;
  return count;
}

u32 CtNeighbors::GetHighwayConnections() {
  u32 count = 0;
  for (u32 i = 0; i < 4; ++i)
    if (neighbors[i].highway)
      ++count;
  return count;
}

// Count the four current nonzero subway connection words without inspecting other network kinds.
u32 CtNeighbors::GetSubwayConnections() {
  u32 count = 0;
  for (u32 i = 0; i < 4; ++i)
    if (neighbors[i].subway)
      ++count;
  return count;
}

// Sum the four rail connection words with the original wrapping DWORD arithmetic.
u32 CtNeighbors::GetTotalRailConnections() {
  u32 total = 0;
  for (u32 i = 0; i < 4; ++i)
    total += neighbors[i].rail;
  return total;
}

// Sum all four road connection words rather than counting the neighbors that have roads.
u32 CtNeighbors::GetTotalRoadConnections() {
  u32 total = 0;
  for (u32 i = 0; i < 4; ++i)
    total += neighbors[i].road;
  return total;
}

// Sum the actual water counters at original word140, preserving the complete DWORD result.
u32 CtNeighbors::GetTotalWaterConnections() {
  u32 total = 0;
  for (u32 i = 0; i < 4; ++i)
    total += neighbors[i].water;
  return total;
}

u32 CtNeighbors::GetTotalPowerConnections() {
  u32 total = 0;
  for (u32 i = 0; i < 4; ++i)
    total += neighbors[i].power;
  return total;
}

u32 CtNeighbors::GetTotalGarbageConnections() {
  u32 road = GetTotalRoadConnections();
  return road + GetTotalRailConnections();
}

// Sum the four highway connection words independently of the garbage route total.
u32 CtNeighbors::GetTotalHighwayConnections() {
  u32 total = 0;
  for (u32 i = 0; i < 4; ++i)
    total += neighbors[i].highway;
  return total;
}

// Sum all four subway connection words using the original DWORD accumulation.
u32 CtNeighbors::GetTotalSubwayConnections() {
  u32 total = 0;
  for (u32 i = 0; i < 4; ++i)
    total += neighbors[i].subway;
  return total;
}

// Recalculate stored costs for active types1/3/5 under the real lock and accumulate their monthly income.
// The product and sum wrap at32 bits before the unsigned division; canceled ages are not filtered here.
i32 CtNeighbors::GetTotalMonthlyNeighborDealIncome() {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  u32 total = 0;
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal) {
      u8 type = deal->dealType;
      if (type == 1 || type == 3 || type == 5) {
        u32 cost = deal->value08 * deal->value20 / CtNeighborQuantities[type];
        total += cost;
        deal->value0c = cost;
      }
    }
    ++entry;
  }
  lock.Unlock();
  return static_cast<i32>(total);
}

// Preserve the initial cost store, subsequent minimum replacement and inclusion of negative-age entries.
i32 CtNeighbors::GetTotalMonthlyNeighborDealExpense() {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  u32 total = 0;
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal) {
      u8 type = deal->dealType;
      if (type == 0 || type == 2 || type == 4) {
        u32 cost = deal->value08 * deal->value20 / CtNeighborQuantities[type];
        deal->value0c = cost;
        if (static_cast<i32>(cost) < static_cast<i32>(deal->value18))
          deal->value0c = deal->value18;
        total += deal->value0c;
      }
    }
    ++entry;
  }
  lock.Unlock();
  return static_cast<i32>(total);
}

// Consume the accumulated cancellation expense by capturing it before the original unlocked zero store.
i32 CtNeighbors::GetNeighborDealPenaltyExpense() {
  u32 expense = penaltyExpense;
  penaltyExpense = 0;
  return static_cast<i32>(expense);
}

u32 CtNeighbors::GetNumberOfNeighborDeals() {
  return static_cast<u32>(activeDeals.size());
}

// Fill at most the caller's live byte capacity, skipping null deals and ignoring each descriptor operation's reply.
// Read the capacity again between callbacks, then publish the number written only after unlocking.
u8 CtNeighbors::GetNeighborDeals(u8 *count, CtDealRecord *out) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  u32 written = 0;
  while (entry != end) {
    if (written >= *count)
      break;
    CtNeighborDeal *deal = entry->deal;
    if (deal) {
      deal->GetRecord(out);
      ++written;
      ++out;
    }
    ++entry;
  }
  lock.Unlock();
  *count = static_cast<u8>(written);
  return 1;
}

u32 CtNeighborRates[6], CtNeighborQuantities[6], CtNeighborMinimumCosts[6];
u32 CtDealRateVariable = 5, CtDealBasePenalty = 500, CtDealPenaltyMultiplier = 10;
u32 CtDealSellPopulation = 20, CtDealBuyPopulation = 10;
u32 CtDealPowerUnits = 500, CtDealWaterUnits = 200, CtDealGarbageUnits = 65;

// Select hexadecimal for a leading 0x or any A-F letter, otherwise decimal, then use the original unsigned CRT parser.
// The entire C string participates in base selection, including letters after the eventual numeric prefix.
u32 ScParseMiscNumber(const char *text) {
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    return static_cast<u32>(strtoul(text, nullptr, 16));
  int base = 10;
  for (const char *byte = text; *byte; ++byte)
    if ((*byte >= 'a' && *byte <= 'f') || (*byte >= 'A' && *byte <= 'F')) {
      base = 16;
      break;
    }
  return static_cast<u32>(strtoul(text, nullptr, base));
}

static u8 CtReadNeighborSetting(EcIni *ini, const char *sectionName, const char *keyName, ScString *out) {
  ScString key(keyName), section(sectionName);
  return ini->GetValue(&section, &key, out);
}

// Read every original neighbor deal tuning key through the real packaged INI reader, preserving prior values on misses.
bool CtNeighbors::ReadConfiguration() {
  bool missing = false;
  ScString path, spare;
  EcIni ini;
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\NeighborDeals.INI");
  ini.SelectFile(&path);
  ScGetMiscApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(path);
  ScString value;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "POWER_BUY_RATE", &value))
    CtNeighborRates[0] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "POWER_SELL_RATE", &value))
    CtNeighborRates[1] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "WATER_BUY_RATE", &value))
    CtNeighborRates[2] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "WATER_SELL_RATE", &value))
    CtNeighborRates[3] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "GARBAGE_IMPORT_RATE", &value))
    CtNeighborRates[5] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "GARBAGE_EXPORT_RATE", &value))
    CtNeighborRates[4] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_RATE_CONSTANTS", "DEAL_RATE_VARIABLE", &value))
    CtDealRateVariable = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "BASE_POWER", &value))
    CtNeighborQuantities[0] = CtNeighborQuantities[1] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "BASE_WATER", &value))
    CtNeighborQuantities[2] = CtNeighborQuantities[3] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "BASE_GARBAGE", &value))
    CtNeighborQuantities[4] = CtNeighborQuantities[5] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "PercentPopForSell", &value))
    CtDealSellPopulation = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "PercentPopForBuy", &value))
    CtDealBuyPopulation = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "PowerUnitsPer1K", &value))
    CtDealPowerUnits = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "WaterUnitsPer1K", &value))
    CtDealWaterUnits = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "BASE_DEAL_QUANTITIES", "GarbageUnitsPer1K", &value))
    CtDealGarbageUnits = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "MINIMUM_DEAL_COSTS", "MIN_POWER_BUY", &value))
    CtNeighborMinimumCosts[0] = CtNeighborMinimumCosts[1] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "MINIMUM_DEAL_COSTS", "MIN_WATER_BUY", &value))
    CtNeighborMinimumCosts[2] = CtNeighborMinimumCosts[3] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "MINIMUM_DEAL_COSTS", "MIN_GARBAGE_EXPORT", &value))
    CtNeighborMinimumCosts[5] = CtNeighborMinimumCosts[4] = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_PENALTIES", "BASE_DEAL_PENALTY", &value))
    CtDealBasePenalty = ScParseMiscNumber(value.begin);
  else
    missing = true;
  if (CtReadNeighborSetting(&ini, "DEAL_PENALTIES", "DEAL_PENALTY_MULTIPLIER", &value))
    CtDealPenaltyMultiplier = ScParseMiscNumber(value.begin);
  else
    missing = true;
  return !missing;
}

// Select the original network's ordered packed-cell set, preserving the nonnumeric member order.
// Unsupported network types have no set and do not inspect map dimensions or cell storage.
std::set<u32> *CtNeighbors::ConnectionSet(i32 networkType) {
  switch (networkType) {
  case 1:
    return &connections[1];
  case 2:
    return &connections[0];
  case 3:
    return &connections[3];
  case 4:
    return &connections[4];
  case 5:
    return &connections[2];
  case 6:
    return &connections[5];
  default:
    return nullptr;
  }
}

// Select one original per-side connection word without adding an internal bounds check absent from the source.
// Unsupported network types return no word before inspecting the side index.
u32 *CtNeighbors::ConnectionCount(i32 networkType, u8 side) {
  switch (networkType) {
  case 1:
    return &neighbors[side].road;
  case 2:
    return &neighbors[side].rail;
  case 3:
    return &neighbors[side].power;
  case 4:
    return &neighbors[side].highway;
  case 5:
    return &neighbors[side].water;
  case 6:
    return &neighbors[side].subway;
  default:
    return nullptr;
  }
}

// Classify the original edge, assigning successive corner cells west, north, east and south by the original tests.
// In particular the southwest corner belongs to the south side, unlike a simple first-x-edge rule.
u8 CtNeighbors::EdgeSide(i32 x, i32 z) {
  if (!x && (u32)z != height - 1)
    return 0;
  if ((u32)x == width - 1 && z)
    return 2;
  if (!z && x)
    return 1;
  if ((u32)z == height - 1 && (u32)x != width - 1)
    return 3;
  return 4;
}

// Increment the selected edge's network count with the original DWORD wrapping behavior.
// An unsupported type or a nonedge cell leaves all counters unchanged.
u8 CtNeighbors::IncrementConnection(i32 networkType, i32 x, i32 z) {
  u8 side = EdgeSide(x, z);
  if (side == 4)
    return 0;
  u32 *count = ConnectionCount(networkType, side);
  if (!count)
    return 0;
  ++*count;
  return 1;
}

// Decrement a nonzero selected edge count, preserving the original zero-count refusal.
// This method does not inspect or mutate the corresponding packed-cell set.
u8 CtNeighbors::DecrementConnection(i32 networkType, i32 x, i32 z) {
  u8 side = EdgeSide(x, z);
  if (side == 4)
    return 0;
  u32 *count = ConnectionCount(networkType, side);
  if (!count || !*count)
    return 0;
  --*count;
  return 1;
}

// Insert the packed cell while holding the real layer lock, then increment its side count even for a duplicate key.
// Original packing adds z to x shifted16; it is not replaced with an OR that would change raw signed inputs.
u8 CtNeighbors::AddConnection(i32 networkType, i32 x, i32 z) {
  lock.Lock();
  std::set<u32> *cells = ConnectionSet(networkType);
  if (!cells || EdgeSide(x, z) == 4) {
    lock.Unlock();
    return 0;
  }
  u32 cell = (u32(x) << 16) + u32(z);
  cells->insert(cell);
  IncrementConnection(networkType, x, z);
  lock.Unlock();
  return 1;
}

// Erase an existing packed cell, then request the independent guarded side-count decrement before unlocking.
// Missing cells and unsupported types return zero without changing the counters.
u8 CtNeighbors::RemoveConnection(i32 networkType, i32 x, i32 z) {
  lock.Lock();
  std::set<u32> *cells = ConnectionSet(networkType);
  if (!cells) {
    lock.Unlock();
    return 0;
  }
  u32 cell = (u32(x) << 16) + u32(z);
  auto entry = cells->find(cell);
  if (entry == cells->end()) {
    lock.Unlock();
    return 0;
  }
  cells->erase(entry);
  DecrementConnection(networkType, x, z);
  lock.Unlock();
  return 1;
}

// Check the selected network's packed-cell membership under the layer's own lock, without imposing an edge test.
bool CtNeighbors::HasConnection(i32 networkType, i32 x, i32 z) {
  lock.Lock();
  std::set<u32> *cells = ConnectionSet(networkType);
  bool found = cells && cells->find((u32(x) << 16) + u32(z)) != cells->end();
  lock.Unlock();
  return found;
}

// Search all six connection sets in original member order, then classify a found cell after releasing the lock.
// A cell absent from every network returns the original sentinel four.
u8 CtNeighbors::ConnectionSide(i32 x, i32 z) {
  u32 cell = (u32(x) << 16) + u32(z);
  lock.Lock();
  bool found = false;
  for (const std::set<u32> &cells : connections) {
    if (cells.find(cell) != cells.end()) {
      found = true;
      break;
    }
  }
  lock.Unlock();
  return found ? EdgeSide(x, z) : 4;
}

bool CtNeighbors::HasSideConnection(i32 networkType, u8 side) {
  u32 *count = ConnectionCount(networkType, side);
  return count && *count != 0;
}

// Search active deals under the real lock, comparing each live full-word type result with the byte input.
bool CtNeighbors::HasDealType(u8 type) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      lock.Unlock();
      return true;
    }
    ++entry;
  }
  lock.Unlock();
  return false;
}

bool CtNeighbors::HasPendingDealType(u8 type) {
  lock.Lock();
  auto entry = pendingDeals.begin(), end = pendingDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      lock.Unlock();
      return true;
    }
    ++entry;
  }
  lock.Unlock();
  return false;
}

// Retain the first active match before publishing it, then unlock; refused output remains untouched.
bool CtNeighbors::GetDealObject(u32 type, CtDeal **out) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      deal->AddRef();
      *out = deal;
      lock.Unlock();
      return true;
    }
    ++entry;
  }
  lock.Unlock();
  return false;
}

// Publish a pending match before retaining it; unlike active lookup, refusal clears output after unlocking.
bool CtNeighbors::GetPendingDealObject(u32 type, CtDeal **out) {
  lock.Lock();
  auto entry = pendingDeals.begin(), end = pendingDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      *out = deal;
      deal->AddRef();
      lock.Unlock();
      return true;
    }
    ++entry;
  }
  lock.Unlock();
  *out = nullptr;
  return false;
}

// Obtain the active deal through the live lookup and forward its actual record operation.
// The acquired hold remains live after this call, preserving the owning object behind borrowed record names.
u8 CtNeighbors::GetDeal(u8 type, CtDealRecord *out) {
  CtDeal *deal = nullptr;
  if (GetDealObject(type, &deal) && deal)
    return deal->GetRecord(out);
  return 0;
}

// Copy a pending descriptor and then release the live returned identity, retaining its raw record-operation result.
u8 CtNeighbors::GetPendingDeal(u8 type, CtDealRecord *out) {
  u8 result = 0;
  CtDeal *deal = nullptr;
  if (GetPendingDealObject(type, &deal) && deal) {
    result = deal->GetRecord(out);
    deal->Release();
  }
  return result;
}

// Update the first active deal matching the complete live type under the real lock.
// Types1/3/5 cancel their term only when the signed stored amount is below the input; other types replace the amount.
u8 CtNeighbors::PublishDeal(u8 type, i32 amount) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      if (type == kCtDealExportPower || type == kCtDealExportWater || type == kCtDealImportGarbage) {
        if (static_cast<i32>(deal->GetValue08()) < amount)
          deal->SetValue14(kCtCanceledNeighborDealTerm);
      } else
        deal->SetValue08(static_cast<u32>(amount));
      lock.Unlock();
      return 1;
    }
    ++entry;
  }
  lock.Unlock();
  return 0;
}

// Test the actual neighbor connection counters and reached utility grids, locking only the grid-search branches.
bool CtNeighbors::CanDeal(u8 neighbor, u8 type) {
  if (neighbor >= 4)
    return false;
  CtNeighbor *entry = &neighbors[neighbor];
  if (type == kCtDealImportPower)
    return entry->power != 0;
  if (type == kCtDealImportWater)
    return entry->water != 0;
  if (type == kCtDealExportPower || type == kCtDealExportWater) {
    if (!(type == kCtDealExportPower ? entry->power : entry->water))
      return false;
    lock.Lock();
    const std::set<u32> *cells = &connections[type == kCtDealExportPower ? 3 : 2];
    auto cell = cells->begin(), end = cells->end();
    while (cell != end) {
      u32 x = *cell >> 16, z = *cell & 0xffff;
      if (EdgeSide(static_cast<i32>(x), static_cast<i32>(z)) == neighbor) {
        bool supplied;
        if (type == kCtDealExportPower)
          power->GetValue(x, z, &supplied);
        else
          supplied = plumbing->IsWatered(x, z) != 0;
        if (supplied) {
          lock.Unlock();
          return true;
        }
      }
      ++cell;
    }
    lock.Unlock();
    return false;
  }
  if (type > kCtDealImportGarbage)
    return true;
  i32 tracked = buildings->CheckOccupantTracking(12330);
  u8 available = entry->available;
  if (!available && !tracked)
    return false;
  if (available != 1 || entry->road || entry->rail || entry->highway)
    return true;
  return tracked != 0;
}

// Age nonnegative active deals and validate their current connection/grid requirements under the original lock.
// Capture the next roster node after service calls but before live cancellation, which may erase the current node.
u8 CtNeighbors::ValidateActiveDeals() {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (!deal || static_cast<i32>(deal->value14) < 0) {
      ++entry;
      continue;
    }
    ++deal->value14;
    u32 neighbor = deal->GetNeighborId();
    if (neighbor > 3)
      neighbor = 3;
    CtNeighbor *record = &neighbors[neighbor];
    u8 type = deal->dealType;
    bool invalid = false;
    if (type == 0)
      invalid = !record->power;
    else if (type == 2)
      invalid = !record->water;
    else if (type == 1 || type == 3) {
      invalid = true;
      if (type == 1 ? record->power : record->water) {
        const std::set<u32> *cells = &connections[type == 1 ? 3 : 2];
        auto cell = cells->begin(), cellEnd = cells->end();
        while (cell != cellEnd) {
          u32 x = *cell >> 16, z = *cell & 0xffff;
          if (EdgeSide(static_cast<i32>(x), static_cast<i32>(z)) == neighbor) {
            bool supplied;
            if (type == 1)
              power->GetValue(x, z, &supplied);
            else
              supplied = plumbing->IsWatered(x, z) != 0;
            if (supplied) {
              invalid = false;
              break;
            }
          }
          ++cell;
        }
      }
    } else if (type <= 5 && !(record->available && (record->road || record->rail || record->highway)))
      invalid = buildings->CheckOccupantTracking(12330) == 0;
    ++entry;
    if (invalid)
      CancelDeal(deal->dealType);
  }
  lock.Unlock();
  return 1;
}

// Copy the localized description for one of the six original deal types, keeping its resource alive through assignment.
// An unsupported type preserves the caller's String and returns the original refusal byte.
u8 CtNeighbors::GetDescriptionStringForDeal(u8 type, ScString *out) {
  static const u32 instances[6] = {323, 320, 324, 321, 322, 325};
  if (type > kCtDealImportGarbage)
    return 0;
  CtMiscTextResource resource(instances[type], kRsGameTextGroup);
  ScString text = resource.GetText();
  *out = text;
  return 1;
}

// Resolve the deal's unit suffix before formatting the signed amount through the actual current-language utility.
// Append the complete suffix after the money operation, ignoring that operation's predicate as in the original.
u8 CtNeighbors::GetRateStringForDeal(u8 type, i32 value, ScString *out) {
  ScString suffix;
  if (type <= kCtDealExportPower) {
    CtMiscTextResource resource(kRsGame1000MWHText, kRsGameTextGroup);
    suffix = resource.GetText();
  } else if (type <= kCtDealExportWater) {
    CtMiscTextResource resource(kRsGame1000M3Text, kRsGameTextGroup);
    suffix = resource.GetText();
  } else if (type <= kCtDealImportGarbage) {
    CtMiscTextResource resource(kRsGame100TonsText, kRsGameTextGroup);
    suffix = resource.GetText();
  } else
    return 0;
  ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(static_cast<i64>(value), out);
  out->Append(suffix.begin, static_cast<u32>(suffix.end - suffix.begin));
  return 1;
}

u8 CtNeighbors::GetContributionStringForDeal(i32 value, ScString *out) {
  ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(static_cast<i64>(value), out);
  return 1;
}

ScStopwatch ScNeighborClock(ScStopwatch::Milliseconds, 0);

// Reset and start the original module stopwatch before reading configuration, ignoring that lookup result.
bool CtNeighbors::StaticInit(CtDefinition *) {
  ScNeighborClock.Reset();
  ScNeighborClock.Start();
  ReadConfiguration();
  return true;
}

bool CtNeighbors::StaticShutdown() {
  ScNeighborClock.Stop();
  return true;
}

bool CtNeighbors::Init(CtCity *, ScImporter *) {
  return true;
}

// The millisecond stopwatch seeds a real local generator; no configurable random or resource callback layer is
// introduced.
bool CtNeighbors::InitializeNeighbors() {
  ScRandom random(ScNeighborClock.GetElapsedTime());
  for (u32 i = 0; i < 4; ++i) {
    CtNeighbor *neighbor = &neighbors[i];
    neighbor->population = random.Bounded(13000) + 2000;
    neighbor->populationLimit = kCtNeighborInitialPopulationLimit;
    neighbor->available = 1;
    neighbor->road = 0;
    neighbor->rail = 0;
    neighbor->water = 0;
    neighbor->power = 0;
    neighbor->highway = 0;
    neighbor->subway = 0;
    for (u32 j = 0; j < 6; ++j)
      neighbor->flags[j] = 0;
  }
  if (pollution->IsSaltWater(width >> 1, 0))
    neighbors[1].available = 0;
  if (pollution->IsSaltWater(0, height >> 1))
    neighbors[0].available = 0;
  if (pollution->IsSaltWater(width >> 1, height - 1))
    neighbors[3].available = 0;
  if (pollution->IsSaltWater(width - 1, height >> 1))
    neighbors[2].available = 0;
  u32 selected[4];
  for (u32 i = 0; i < 4; ++i) {
    u32 instance;
    bool duplicate;
    do {
      instance = random.Bounded(36) + 41;
      duplicate = false;
      for (u32 j = 0; j < i; ++j)
        if (selected[j] == instance) {
          duplicate = true;
          break;
        }
    } while (duplicate);
    selected[i] = instance;
    CtMiscTextResource resource(instance, kAdAdvisorNameGroup);
    neighbors[i].name = resource.GetText();
  }
  for (u32 i = 0; i < 4; ++i) {
    u32 instance;
    bool duplicate;
    do {
      instance = random.Bounded(36) + 2;
      duplicate = false;
      for (u32 j = 0; j < i; ++j)
        if (selected[j] == instance) {
          duplicate = true;
          break;
        }
    } while (duplicate);
    selected[i] = instance;
    CtMiscTextResource resource(instance, kAdAdvisorNameGroup);
    neighbors[i].mayor = resource.GetText();
  }
  return true;
}

// Capture the stored city's calendar before optional fresh-neighbor generation, then register the real secondary agent.
// Simulator acquisition uses the current global city after initialization callbacks, just as in the installed body.
bool CtNeighbors::SimulationBegin() {
  city->GetDate(&month, &day, &year);
  if (field428)
    InitializeNeighbors();
  ScGetMiscCity()->CitySimulator()->AddAgent(static_cast<CtAgent *>(this));
  return true;
}

// Remove the actual agent from the current global simulator before restoring fresh-neighbor initialization for next
// begin.
bool CtNeighbors::SimulationEnd() {
  ScGetMiscCity()->CitySimulator()->RemoveAgent(static_cast<CtAgent *>(this));
  field428 = 1;
  return true;
}

// Apply the original twelve annual population increments and rare halving, then the original unsigned lower/upper
// loops.
u8 CtNeighbors::GrowPopulation() {
  ScRandom random(ScNeighborClock.GetElapsedTime());
  for (u32 i = 0; i < 4; ++i) {
    CtNeighbor *neighbor = &neighbors[i];
    for (u32 j = 0; j < 12; ++j)
      neighbor->population += random.Bounded(3800) - 1200;
    if (random.Bounded(1000) < 15)
      neighbor->population >>= 1;
    while (neighbor->population < 1000)
      neighbor->population += 500;
    while (neighbor->population > neighbor->populationLimit)
      neighbor->population -= 10000;
  }
  return 1;
}

u8 CtNeighbors::SimulateYear() {
  GrowPopulation();
  return 1;
}

// Refresh the stored calendar, run the January update when selected, then validate all active deals.
u8 CtNeighbors::SimulateMonth() {
  city->GetDate(&month, &day, &year);
  if (month == 1)
    SimulateYear();
  ValidateActiveDeals();
  return 1;
}

// Dispatch only phase four to the original monthly operation through the real secondary city-agent override.
bool CtNeighbors::Simulate(i32 phase) {
  if (phase == 4)
    return SimulateMonth() != 0;
  return false;
}

// Read the actual descriptor fields directly so failed operations retain their partial writes and previous values.
// The caller owns both String outputs and reuses this same descriptor across the active and pending rosters.
static u8 CtReadNeighborDeal(RsStream *stream, CtDealRecord *data, u32 language) {
  u8 valid = CtReadSavedText(stream, data->first_name, language);
  valid = valid && CtReadSavedText(stream, data->second_name, language) &&
      stream->ReadI32(reinterpret_cast<i32 *>(&data->value_08)) &&
      stream->ReadI32(reinterpret_cast<i32 *>(&data->value_0c)) && stream->ReadU8(&data->value_10) &&
      stream->ReadU8(&data->type) && stream->ReadI32(reinterpret_cast<i32 *>(&data->value_14)) &&
      stream->ReadI32(reinterpret_cast<i32 *>(&data->value_1c)) &&
      stream->ReadI32(reinterpret_cast<i32 *>(&data->value_18)) &&
      stream->ReadI32(reinterpret_cast<i32 *>(&data->value_20)) && stream->ReadU32(&data->year);
  return valid;
}

// Find a later same-type deal, or the paired type for the same neighbor, using the original direct byte fields.
// This extracted load phase adds no virtual type queries or reference holds to the roster scan.
static bool CtLaterNeighborDeal(std::list<CtDealRef>::const_iterator entry,
    std::list<CtDealRef>::const_iterator end, u8 type, u8 neighbor) {
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal->dealType == type ||
        (type <= 5 && deal->dealType == (type ^ 1) && deal->neighborId == neighbor))
      return true;
    ++entry;
  }
  return false;
}

// Retain only the last conflicting active entry, retiring each earlier node through its real retained element.
// The next node is captured before scanning and erasure, matching the original load-side traversal.
static void CtPruneActiveDeals(std::list<CtDealRef> *deals) {
  auto entry = deals->begin(), end = deals->end();
  while (entry != end) {
    auto next = entry;
    ++next;
    CtNeighborDeal *deal = entry->deal;
    if (CtLaterNeighborDeal(next, end, deal->dealType, deal->neighborId))
      deals->erase(entry);
    entry = next;
  }
}

// Drop pending entries whose live type is already active, then retain only the last conflicting pending entry.
// Capture the next node after the live active lookup, preserving any insertion made during that operation.
static void CtPrunePendingDeals(CtNeighbors *neighbors) {
  auto entry = neighbors->pendingDeals.begin(), end = neighbors->pendingDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    u8 type = deal->dealType, neighbor = deal->neighborId;
    bool discard = neighbors->HasDealType(type);
    auto next = entry;
    ++next;
    if (discard || CtLaterNeighborDeal(next, end, type, neighbor))
      neighbors->pendingDeals.erase(entry);
    entry = next;
  }
}

// Load neighbor entries, retained deal rosters and merged connection sets in the original order.
// Fresh native scratch storage is defined at allocation; failed reads otherwise preserve prior outputs and cleanup.
bool CtNeighbors::Init(CtCity *, RsDatabaseSegment *segment) {
  RsKey key = {kCtNeighborsRecordType, kCtNeighborsRecordGroup, 0};
  RsRecord *record = nullptr;
  RsStream *stream = nullptr;
  if (!segment->OpenRecord(key, &record) || !record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream)))
    return false;
  field428 = 0;
  u8 valid = ReadNeighborRecords(stream);
  u32 count = 0;
  if (valid)
    valid = stream->ReadU32(&count);
  CtDealRecord *data = new CtDealRecord{};
  ScString firstName, secondName;
  data->first_name = &firstName;
  data->second_name = &secondName;
  u32 language = CtNeighborTextLanguage(city, false);
  for (; count && valid; --count) {
    valid = CtReadNeighborDeal(stream, data, language);
    CtNeighborDeal *deal = new CtNeighborDeal;
    deal->SetRecord(data);
    CtDealRef retained(deal);
    activeDeals.push_back(retained);
  }
  CtPruneActiveDeals(&activeDeals);
  if (valid)
    valid = stream->ReadU32(&count);
  for (; count && valid; --count) {
    valid = CtReadNeighborDeal(stream, data, language);
    CtNeighborDeal *deal = new CtNeighborDeal;
    deal->SetRecord(data);
    CtDealRef retained(deal);
    pendingDeals.push_back(retained);
  }
  CtPrunePendingDeals(this);
  delete data;
  for (u32 i = 0; i < 6; ++i) {
    if (valid)
      valid = stream->ReadU32(&count);
    for (; count && valid; --count) {
      u32 cell;
      valid = stream->ReadU32(&cell);
      if (valid)
        connections[i].insert(cell);
    }
  }
  stream->Release();
  if (!segment->CloseRecord(&record))
    segment->AbortRecord(&record);
  return valid != 0;
}

// Search the four neighbors cyclically from the position after a random starting point, preserving the paired flags.
u8 CtNeighbors::SelectNeighbor(u8 type) {
  ScRandom random(ScNeighborClock.GetElapsedTime());
  if (HasDealType(type))
    return 4;
  u32 selected = random.Bounded(4);
  for (u32 tried = 0; tried < 4; ++tried) {
    selected = (selected + 1) & 3;
    CtNeighbor *neighbor = &neighbors[selected];
    bool free = false;
    if (type <= kCtDealExportPower)
      free = !neighbor->flags[0] && !neighbor->flags[1];
    else if (type <= kCtDealExportWater)
      free = !neighbor->flags[2] && !neighbor->flags[3];
    else if (type <= kCtDealImportGarbage)
      free = !neighbor->flags[5] && !neighbor->flags[4];
    if (free && CanDeal(static_cast<u8>(selected), type))
      return static_cast<u8>(selected);
  }
  return 4;
}

// Reject existing offers and ineligible neighbors before creating a real pending deal with population-derived terms.
u8 CtNeighbors::ComputeOffer(u8 neighbor, u8 type, u32 maximum) {
  ScRandom random(ScNeighborClock.GetElapsedTime());
  if (HasDealType(type) || HasPendingDealType(type) || !CanDeal(neighbor, type))
    return 0;
  u32 rate = CtNeighborRates[type] + random.Bounded(CtDealRateVariable);
  if (type <= kCtDealImportGarbage) {
    CtDealRecord counterpart;
    u8 opposite = type ^ 1;
    if (GetDeal(opposite, &counterpart) || GetPendingDeal(opposite, &counterpart)) {
      if (counterpart.value_10 == neighbor)
        return 0;
      if (!(type & 1) && static_cast<i32>(counterpart.value_20) > static_cast<i32>(rate))
        rate = counterpart.value_20;
    }
  }
  CtNeighborDeal *deal = new CtNeighborDeal;
  deal->SetNeighborId(neighbor);
  deal->SetType(type);
  deal->SetValue14(0);
  deal->SetValue18(CtNeighborMinimumCosts[type]);
  deal->SetValue20(rate);
  if (type <= kCtDealImportGarbage) {
    u32 population, percentage, units;
    if (type & 1) {
      population = neighbors[neighbor].population;
      percentage = CtDealSellPopulation;
    } else {
      population = static_cast<u32>(residential->GetPopulation());
      percentage = CtDealBuyPopulation;
    }
    if (type <= kCtDealExportPower)
      units = CtDealPowerUnits;
    else if (type <= kCtDealExportWater)
      units = CtDealWaterUnits;
    else
      units = CtDealGarbageUnits;
    u32 groups = static_cast<u32>(static_cast<u64>(percentage) * population / kCtDealPopulationPercentageDivisor);
    if (!groups)
      groups = 1;
    u32 quantity = groups * units, step = CtNeighborQuantities[type];
    if ((type & 1) && quantity > maximum)
      quantity = maximum;
    if (quantity % step)
      quantity += step - quantity % step;
    u32 cost = rate * quantity / step;
    if (type & 1) {
      deal->SetValue0c(cost);
      deal->SetValue08(quantity);
      deal->SetValue1c(cost * CtDealPenaltyMultiplier);
    } else {
      if (static_cast<i32>(deal->GetValue18()) <= static_cast<i32>(cost)) {
        deal->SetValue0c(cost);
        deal->SetValue18(cost);
      } else
        deal->SetValue0c(deal->GetValue18());
      deal->SetValue1c(cost * CtDealPenaltyMultiplier);
      deal->SetValue08(0);
    }
  }
  deal->SetFirstName(neighbors[neighbor].name);
  ScString secondName = CtMiscTextResource(kRsCommonNEEDSC3STRINGTABLEAPPSTRINGText, kRsCommonTextGroup).GetText();
  deal->SetSecondName(secondName);
  lock.Lock();
  {
    CtDealRef retained(deal);
    pendingDeals.push_back(retained);
  }
  lock.Unlock();
  return 1;
}

// Renegotiate an existing active deal using the current randomized rate and the original paired-rate constraints.
// Keep its borrowed descriptor alive, build real current-date terms, and append only an eligible changed offer.
u8 CtNeighbors::MakeOffer(u8 type) {
  ScRandom random(ScNeighborClock.GetElapsedTime());
  CtDealRecord previous;
  if (!GetDeal(type, &previous))
    return 0;
  u32 rate = CtNeighborRates[type] + random.Bounded(CtDealRateVariable);
  if (type == kCtDealImportPower || type == kCtDealImportWater || type == kCtDealExportGarbage) {
    CtDealRecord counterpart;
    u8 opposite = type + 1;
    if ((GetDeal(opposite, &counterpart) || GetPendingDeal(opposite, &counterpart)) &&
        static_cast<i32>(counterpart.value_20) > static_cast<i32>(rate))
      rate = counterpart.value_20;
    if (static_cast<i32>(rate) <= static_cast<i32>(previous.value_20))
      return 0;
  } else if (static_cast<i32>(rate) < static_cast<i32>(previous.value_20))
    rate = previous.value_20;
  CtNeighborDeal *deal = new CtNeighborDeal;
  deal->SetRecord(&previous);
  deal->SetValue18(CtNeighborMinimumCosts[type]);
  deal->SetFirstName(neighbors[previous.value_10].name);
  ScString secondName = CtMiscTextResource(kRsCommonNEEDSC3STRINGTABLEAPPSTRINGText, kRsCommonTextGroup).GetText();
  deal->SetSecondName(secondName);
  deal->SetValue14(0);
  {
    EcDate currentDate{CtDate(month, day, year)};
    deal->SetDate(static_cast<const CtDateInterface &>(currentDate));
  }
  deal->SetValue20(rate);
  if (type <= kCtDealImportGarbage) {
    u32 population, percentage, units;
    if (type & 1) {
      population = neighbors[previous.value_10].population;
      percentage = CtDealSellPopulation;
    } else {
      population = static_cast<u32>(residential->GetPopulation());
      percentage = CtDealBuyPopulation;
    }
    if (type <= kCtDealExportPower)
      units = CtDealPowerUnits;
    else if (type <= kCtDealExportWater)
      units = CtDealWaterUnits;
    else
      units = CtDealGarbageUnits;
    u32 groups = static_cast<u32>(static_cast<u64>(percentage) * population / kCtDealPopulationPercentageDivisor);
    if (!groups)
      groups = 1;
    u32 quantity = groups * units, step = CtNeighborQuantities[type];
    if (quantity % step)
      quantity += step - quantity % step;
    if ((type & 1) && (static_cast<i32>(quantity) < static_cast<i32>(previous.value_08) ||
        (quantity == previous.value_08 && rate == previous.value_20))) {
      delete deal;
      return 0;
    }
    u32 cost = rate * quantity / step;
    if (type & 1) {
      deal->SetValue0c(cost);
      deal->SetValue08(quantity);
      deal->SetValue1c(cost * CtDealPenaltyMultiplier);
    } else {
      if (static_cast<i32>(deal->GetValue18()) <= static_cast<i32>(cost)) {
        deal->SetValue0c(cost);
        deal->SetValue18(cost);
      } else
        deal->SetValue0c(deal->GetValue18());
      deal->SetValue1c(cost * CtDealPenaltyMultiplier);
      deal->SetValue08(0);
    }
  }
  lock.Lock();
  {
    CtDealRef retained(deal);
    pendingDeals.push_back(retained);
  }
  lock.Unlock();
  return 1;
}

// Accept only the original neighbor-query message and selector, then acquire the real query sink through Unknown.
// Capture the query result before releasing the acquired sink. A failed QI publication receives no extra release.
bool CtNeighbors::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  UiNeighborWindow *query = nullptr;
  if (message->type != kWnMsg_QueryOpened || static_cast<u32>(message->data1) != kUiQueryTagNeighbors ||
      !source->QueryInterface(IID_UiNeighborWindow, reinterpret_cast<void **>(&query)))
    return false;
  u8 result = LoadNeighborDealWindowList(query);
  query->Release();
  return result != 0;
}

// Publish four live neighbor summaries, then describe nonnegative-age active deals under the actual roster lock.
// Reuse the three real output Strings across rows and reload live deal fields after each formatting operation.
u8 CtNeighbors::LoadNeighborDealWindowList(UiNeighborWindow *query) {
  for (u32 i = 0; i < 4; ++i) {
    u8 available = NeighborFlag(static_cast<u8>(i));
    query->SetNeighbor(i, neighbors[i].name, neighbors[i].population, available);
  }
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  ScString description, rate, contribution;
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && static_cast<i32>(deal->value14) >= 0) {
      GetDescriptionStringForDeal(deal->dealType, &description);
      GetRateStringForDeal(deal->dealType, static_cast<i32>(deal->value20), &rate);
      GetContributionStringForDeal(static_cast<i32>(deal->value0c), &contribution);
      query->AddNeighborDeal(deal->neighborId, deal->dealType, &description, &rate, &contribution);
    }
    ++entry;
  }
  lock.Unlock();
  return 1;
}

// Erase the first matching active deal through its retained element destructor, then unlock before clearing all flags.
// The original acknowledges removal even when no matching roster entry exists.
u8 CtNeighbors::RemoveDeal(u8 type) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      activeDeals.erase(entry);
      break;
    }
    ++entry;
  }
  lock.Unlock();
  for (u32 i = 0; i < 4; ++i)
    neighbors[i].flags[type] = 0;
  return 1;
}

// Remove the first matching pending deal under the real lock, allowing the element destructor to retire its hold.
// Unlike active removal, a missing pending entry returns zero and no neighbor flag is cleared.
u8 CtNeighbors::RemovePendingDeal(u8 type) {
  lock.Lock();
  auto entry = pendingDeals.begin(), end = pendingDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type) {
      pendingDeals.erase(entry);
      lock.Unlock();
      return 1;
    }
    ++entry;
  }
  lock.Unlock();
  return 0;
}

// Remove an old-enough active deal immediately; a younger deal receives the original cancellation term and charge.
u8 CtNeighbors::CancelDeal(u8 type) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && deal->GetType() == type)
      break;
    ++entry;
  }
  if (entry == end || !entry->deal) {
    lock.Unlock();
    return 0;
  }
  if (static_cast<i32>(entry->deal->GetValue14()) >= 60)
    RemoveDeal(type);
  else {
    entry->deal->SetValue14(kCtCanceledNeighborDealTerm);
    penaltyExpense += entry->deal->GetValue1c();
  }
  lock.Unlock();
  return 1;
}

// Publish and retain the first negative-term deal before invoking live active removal under the same lock.
// A missing canceled deal preserves the caller's output and returns zero.
u8 CtNeighbors::GetCanceledDeal(CtDeal **out) {
  lock.Lock();
  auto entry = activeDeals.begin(), end = activeDeals.end();
  while (entry != end) {
    CtNeighborDeal *deal = entry->deal;
    if (deal && static_cast<i32>(deal->GetValue14()) < 0) {
      *out = deal;
      deal->AddRef();
      RemoveDeal(static_cast<u8>(deal->GetType()));
      lock.Unlock();
      return 1;
    }
    ++entry;
  }
  lock.Unlock();
  return 0;
}

// Copy the first pending match into a real new deal, retire the pending entry, then establish its current start date.
// Replacing an active match keeps that deal's date; append with original temporary/node holds before setting its flag.
u8 CtNeighbors::AcceptPendingDeal(u8 type) {
  lock.Lock();
  auto pending = pendingDeals.begin(), pendingEnd = pendingDeals.end();
  while (pending != pendingEnd) {
    CtNeighborDeal *deal = pending->deal;
    if (deal && deal->GetType() == type)
      break;
    ++pending;
  }
  if (pending == pendingEnd || !pending->deal) {
    lock.Unlock();
    return 0;
  }
  CtNeighborDeal *deal = new CtNeighborDeal;
  deal->CopyFrom(pending->deal);
  pendingDeals.erase(pending);
  u32 dayOfYear, currentYear;
  city->GetDate(&dayOfYear, &currentYear);
  EcDate currentDate(dayOfYear, currentYear);
  deal->SetDate(static_cast<const CtDateInterface &>(currentDate));
  deal->SetValue14(0);
  auto active = activeDeals.begin(), activeEnd = activeDeals.end();
  while (active != activeEnd) {
    CtNeighborDeal *previous = active->deal;
    if (previous && previous->GetType() == type)
      break;
    ++active;
  }
  if (active != activeEnd && active->deal) {
    EcDate previousDate;
    active->deal->GetDate(static_cast<CtDateInterface *>(&previousDate));
    deal->SetDate(static_cast<const CtDateInterface &>(previousDate));
    activeDeals.erase(active);
  }
  {
    CtDealRef retained(deal);
    activeDeals.push_back(retained);
  }
  lock.Unlock();
  neighbors[deal->GetNeighborId()].flags[type] = 1;
  return 1;
}

// Write each real deal descriptor in original wire order, preserving short-circuiting and untouched failed outputs.
// The word at descriptor32 uses WriteU32 although its load-side operation is ReadI32; both original calls are retained.
static u8 CtWriteNeighborDeals(RsStream *stream, const std::list<CtDealRef> &deals, u8 valid, u32 language) {
  auto entry = deals.begin(), end = deals.end();
  while (entry != end && valid) {
    CtNeighborDeal *deal = entry->deal;
    if (!deal)
      return 0;
    CtDealRecord data;
    valid = deal->GetRecord(&data);
    valid = valid && CtWriteSavedText(stream, data.first_name, language) &&
        CtWriteSavedText(stream, data.second_name, language) &&
        stream->WriteI32((i32)data.value_08) && stream->WriteI32((i32)data.value_0c) && stream->WriteU8(data.value_10) &&
        stream->WriteU8(data.type) && stream->WriteI32((i32)data.value_14) && stream->WriteI32((i32)data.value_1c) &&
        stream->WriteI32((i32)data.value_18) && stream->WriteU32(data.value_20) && stream->WriteU32(data.year);
    ++entry;
  }
  return valid;
}

// Release the queried stream before closing the record even after a write failure; the original ignores CloseRecord's
// result.
bool CtNeighbors::Save(CtCity *, RsDatabaseSegment *segment) {
  RsKey key = {kCtNeighborsRecordType, kCtNeighborsRecordGroup, 0};
  RsRecord *record;
  if (!segment->OpenRecordForWrite(key, &record))
    return false;
  u8 valid = 1;
  RsStream *stream;
  if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
    u32 language = CtNeighborTextLanguage(city, true);
    for (const CtNeighbor &neighbor : neighbors) {
      valid = valid && CtWriteSavedText(stream, &neighbor.name, language) &&
          CtWriteSavedText(stream, &neighbor.mayor, language) &&
          stream->WriteU32(neighbor.population) && stream->WriteU32(neighbor.populationLimit) &&
          stream->WriteU8(neighbor.available) && stream->WriteU32(neighbor.road) && stream->WriteU32(neighbor.rail) &&
          stream->WriteU32(neighbor.power) && stream->WriteU32(neighbor.water) && stream->WriteU32(neighbor.highway) &&
          stream->WriteU32(neighbor.subway);
      for (u32 i = 0; i < 6; ++i)
        if (valid)
          valid = stream->WriteU8(neighbor.flags[i]);
    }
    if (valid)
      valid = stream->WriteU32((u32)activeDeals.size());
    valid = CtWriteNeighborDeals(stream, activeDeals, valid, language);
    if (valid)
      valid = stream->WriteU32((u32)pendingDeals.size());
    valid = CtWriteNeighborDeals(stream, pendingDeals, valid, language);
    for (const std::set<u32> &cells : connections) {
      if (valid)
        valid = stream->WriteU32((u32)cells.size());
      auto entry = cells.begin(), end = cells.end();
      while (entry != end && valid) {
        valid = stream->WriteU32(*entry);
        ++entry;
      }
    }
    stream->Release();
    stream = nullptr;
  } else
    valid = 0;
  segment->CloseRecord(&record);
  return valid != 0;
}

// Word outputs are passed directly to preserve any partial write on failure; this phase does not create or repair
// deals.
u8 CtNeighbors::ReadNeighborRecords(RsStream *stream) {
  u8 valid = 1, flag;
  u32 language = CtNeighborTextLanguage(city, false);
  for (u32 index = 0; index < 4; ++index) {
    CtNeighbor *neighbor = &neighbors[index];
    valid = valid && CtReadSavedText(stream, &neighbor->name, language) &&
        CtReadSavedText(stream, &neighbor->mayor, language) &&
        stream->ReadU32(&neighbor->population) && stream->ReadU32(&neighbor->populationLimit);
    if (valid) {
      valid = stream->ReadU8(&flag);
      if (valid)
        neighbor->available = flag != 0;
    }
    valid = valid && stream->ReadU32(&neighbor->road) && stream->ReadU32(&neighbor->rail) &&
        stream->ReadU32(&neighbor->power) && stream->ReadU32(&neighbor->water) && stream->ReadU32(&neighbor->highway) &&
        stream->ReadU32(&neighbor->subway);
    for (u32 i = 0; i < 6; ++i) {
      if (valid) {
        valid = stream->ReadU8(&flag);
        if (valid)
          neighbor->flags[i] = flag != 0;
      }
    }
  }
  return valid;
}
