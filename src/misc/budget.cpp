#include "misc/budget.h"

#include <stdio.h>
#include <stdlib.h>

#include "advisors/ad_advisors.h"
#include "advisors/api.h"
#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_residential.h"
#include "fonts/sc3_bitmap_font.h"
#include "misc/api.h"
#include "misc/module.h"
#include "misc/sc_graph_info.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/game_menu.h"
#include "ui/generic_dialogs.h"
#include "ui/neighbor_window.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// Budget and bond dialogs retain separate button frames, divider panels and department year-value box identities.
enum CtBudgetPanelIdentity : u32 {
  kCtBudgetCloseButtonPanelId = 0x63095f4d,
  kCtBudgetHelpButtonPanelId = 0x23095dcf,
  kCtBudgetTopDividerPanelId = 0x62df08d6,
  kCtBudgetBottomDividerPanelId = 0xa2df09ce,
  kCtBudgetMiddleDividerPanelId = 0x22df0a62,
  kCtBudgetRightDividerPanelId = 0x62df0a69,
  kCtBudgetYearBoxIdBase = 0x22dff397,
  kCtBondsAdvisorButtonPanelId = 0xe30be9a0,
  kCtBondsCloseButtonPanelId = 0x630beaa2,
};

// The budget history graph retains its original series color.
enum CtBudgetGraphColor : u32 {
  kCtBudgetGraphColor = 0x7060a0,
};

// Budget windows retain distinct summary, accrued and projected amount controls; additional income rows share their
// original IDs.
enum CtBudgetControlId : u32 {
  kCtBudgetWindowId = 0x42dd813a,
  kCtBudgetExpenseViewId = 0x42dddc93,
  kCtBudgetIncomeViewId = 0x82e00e34,
  kCtBondsDialogId = 0x42e0509b,
  kCtBudgetYearEndFundsBoxId = 0x82df2404,
  kCtBudgetCurrentFundsBoxId = 0x22df248d,
  kCtBudgetEstimatedCashFlowBoxId = 0xe2df2608,
  kCtBudgetEstimatedExpenseBoxId = 0x22dff1c5,
  kCtBudgetEstimatedIncomeBoxId = 0xc2dff1ef,
  kCtBudgetFirstAccruedBoxId = 0x2dff25c,
  kCtBudgetSecondAccruedBoxId = 0x2dff32e,
  kCtBudgetThirdAccruedBoxId = 0x2dff347,
  kCtBudgetFirstProjectedBoxId = 0x22dff2d6,
  kCtBudgetSecondProjectedBoxId = 0x42dff528,
  kCtBudgetThirdProjectedBoxId = 0xe2dff549,
  kCtBudgetAdditionalAccruedBoxId = 0x82dff35f,
  kCtBudgetAdditionalProjectedBoxId = 0xe2dff565,
  kCtBudgetHeaderFrameId = 0xa30a3de9,
  kCtBondRowsFrameId = 0x430be8af,
  kCtBondSummaryPanelId = 0x30c01bf,
};

// Serialized record identities remain distinct from interface and layer lookup IDs with equal bits.
static constexpr u32 kEcBudgetRecordType = 0xe11bcc69;
static constexpr u32 kEcBudgetRecordGroup = 0xa11bcc54;

// Construct the real lifetime, String and map members and clear only the pointers and flags initialized by the source.
// Funds, tax scalars and history remain unwritten until the original initialization path assigns them.
EcBudget::EcBudget()
    : initialized(false), marxism(false), limit_deficit(false), city(nullptr), ordinances(nullptr), traffic(nullptr),
      land_value(nullptr), neighbors(nullptr), residential(nullptr), agents(nullptr), valves(nullptr), chart(nullptr),
      advisor(nullptr), bonds(nullptr) {
}

EcBudget::~EcBudget() {
  if (initialized)
    Shutdown();
}

// Remove subscriptions before releasing held simulation dependencies in their original order and unregistering cheats.
// Department pointers remain borrowed; the chart releases through its actual lifetime primary after the bond array retires.
bool EcBudget::Shutdown() {
  if (initialized) {
    ScGetMiscApplication()->GetCheatCodeManager()->RemoveNotification(static_cast<CtLayer *>(this), kEcIAmWeakCheatId);
    ScGetMiscApplication()->GetCheatCodeManager()->RemoveNotification(static_cast<CtLayer *>(this), kEcSoSueMeCheatId);
    ScGetMiscMessageServer()->RemoveNotification(static_cast<CtLayer *>(this), kCtMsg_AutoBudgetChanged);
    for (u32 i = 0; i < 3; ++i) {
      if (taxes[i].valve) {
        taxes[i].valve->Release();
        taxes[i].valve = nullptr;
      }
    }
    ScRelease(&city);
    agents = nullptr;
    ScRelease(&ordinances);
    ScRelease(&neighbors);
    ScRelease(&traffic);
    ScRelease(&land_value);
    ScRelease(&residential);
    ScRelease(&valves);
    ScRelease(&advisor);
    ScGetMiscApplication()->GetCheatCodeManager()->UnregisterCheatCode(kEcIAmWeakCheatId);
    ScGetMiscApplication()->GetCheatCodeManager()->UnregisterCheatCode(kEcSoSueMeCheatId);
    delete[] bonds;
    bonds = nullptr;
    if (chart)
      chart->Release();
    chart = nullptr;
    initialized = false;
  }
  return true;
}

// Publish the primary budget controlling identity or the actual separate common city-layer identity as requested.
// Refusal clears the output; either accepted view retains through the primary virtual operation.
bool EcBudget::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcBudgetLayer);
  HANDLE_QUERY_INTERFACE(IID_EcBudgetLayer, EcBudgetLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcBudget::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 EcBudget::Release() {
  return ScMiscRefCount::Release();
}

void EcBudget::SetTotalFunds(i64 value) {
  funds = value;
  funds_copy = value;
}

i64 EcBudget::GetTotalFunds() {
  return funds;
}

// Publish both funds words before posting the original low-priority notification, leaving refusal state unchanged.
bool EcBudget::WithdrawFunds(u32 amount) {
  if (!amount)
    return true;
  i64 remaining = i64(u64(funds) - amount);
  if (remaining < 0)
    return false;
  funds = remaining;
  funds_copy = funds;
  ScMessage message(kEcMsg_FundsChanged);
  ScGetMiscMessageServer()->MessagePost(message, false);
  return true;
}

// Add the original unsigned amount with word wrapping and update the mirror before posting the funds notification.
void EcBudget::DepositFunds(u32 amount) {
  funds = i64(u64(funds) + amount);
  funds_copy = funds;
  ScMessage message(kEcMsg_FundsChanged);
  ScGetMiscMessageServer()->MessagePost(message, false);
}

// Add relief only for an initialized budget, using the city month minus one and the original upper clamp to December.
// This changes the monthly ledger alone; it does not update the annual accumulator, live funds or any notification.
void EcBudget::DepositDisasterReliefFunds(u32 amount) {
  if (initialized) {
    u32 month, day, year;
    city->GetDate(&month, &day, &year);
    --month;
    if (month > 11)
      month = 11;
    disaster_relief.monthly[month] = i64(u64(disaster_relief.monthly[month]) + amount);
  }
}

// Capture a supplied department's ID and funding need before inserting or replacing its full source line item.
// The initial percentage is100 with empty monthly history; neither insertion nor replacement retains the borrowed pointer.
bool EcBudget::AddDepartmentBudget(EcDepartmentBudget *department) {
  if (!department)
    return false;
  EcLineItem entry;
  entry.id = department->GetDepartmentID();
  entry.department = department;
  entry.percentage = 100;
  entry.funding = department->GetTotalFundingNeeded();
  for (u32 i = 0; i < 12; ++i)
    entry.monthly[i] = 0;
  entry.yearTotal = 0;
  departments[entry.id] = entry;
  return true;
}

void EcBudget::RemoveDepartmentBudget(EcDepartmentBudget *department) {
  departments.erase(department->GetDepartmentID());
}

// Clear the output before lookup and publish a found borrowed department without adding an output reference.
bool EcBudget::GetDepartmentBudget(u32 id, EcDepartmentBudget **out) {
  *out = nullptr;
  auto found = departments.find(id);
  if (found == departments.end())
    return false;
  *out = found->second.department;
  return true;
}

u8 EcBudget::GetTaxRate(i32 type) {
  return taxes[type].rate;
}

// Clamp only above the original maximum rate22, then return that receiver's actual floating-point valve modifier.
f32 EcBudget::GetValveModifier(u8 rate) {
  if (rate > 22)
    rate = 22;
  return valve_modifiers[rate];
}

i64 EcBudget::GetTotalBorrowed() {
  return borrowed;
}

i64 EcBudget::GetCurrentBorrowingLimit() {
  return (EcBudgetMaximumBonds * EcBudgetBondValue);
}

void EcBudget::SetMarxism(bool value) {
  marxism = value;
}

bool EcBudget::MarxismIsOn() {
  return marxism;
}

// Sum the actual year-to-date income categories in source order, including all three tax profiles before relief.
// The source component getters only read these fields; unsigned accumulation preserves the original word arithmetic.
i64 EcBudget::GetYTDIncome() {
  u64 total = u64(fare_income.yearTotal);
  total += u64(ordinance_income.yearTotal);
  total += u64(neighbor_income.yearTotal);
  total += u64(business_income.yearTotal);
  for (u32 i = 0; i < 3; ++i)
    total += u64(taxes[i].yearTotal);
  total += u64(disaster_relief.yearTotal);
  return i64(total);
}

// Sum borrowed-department ledger totals in key order, then the source ordinance, neighbor and bond expense accumulators.
i64 EcBudget::GetYTDExpenses() {
  u64 total = 0;
  for (auto entry = departments.begin(); entry != departments.end(); ++entry)
    total += u64(entry->second.yearTotal);
  total += u64(ordinance_expenses.yearTotal);
  total += u64(neighbor_expenses.yearTotal);
  total += u64(yearly_bond_expenses);
  return i64(total);
}

// Recalculate a registered department from its cached percentage and current live funding need.
// Look up the destination again after the callback, preserving the source map insertion behavior if it mutates the registry.
void EcBudget::NeededFundingChanged(EcDepartmentBudget *department) {
  if (department) {
    u32 id = department->GetDepartmentID();
    if (departments.find(id) != departments.end()) {
      u32 percentage = departments[id].percentage;
      i64 needed = department->GetTotalFundingNeeded();
      i64 funding = i64(u64(percentage) * u64(needed)) / 100;
      departments[id].funding = funding;
    }
  }
}

// Store the first live percentage, then read it again before querying the funding need and recomputing the allocation.
// Each source map access remains separate across callbacks so registry changes retain their original ordering.
void EcBudget::FundingPercentageChanged(EcDepartmentBudget *department) {
  if (department) {
    u32 id = department->GetDepartmentID();
    if (departments.find(id) != departments.end()) {
      u8 storedPercentage = department->GetFundingPercentage();
      departments[id].percentage = storedPercentage;
      u32 percentage = department->GetFundingPercentage();
      i64 needed = department->GetTotalFundingNeeded();
      i64 funding = i64(u64(percentage) * u64(needed)) / 100;
      departments[id].funding = funding;
    }
  }
}

// Record the dated payment schedule and debt before calling the live virtual deposit operation.
bool EcBudget::IssueBond(u32 amount) {
  if (bond_count >= EcBudgetMaximumBonds)
    return false;
  amount = 5000 * (amount / 5000);
  if (!amount || amount > EcBudgetBondValue)
    return false;
  i64 limit = GetCurrentBorrowingLimit();
  if (i64(u64(borrowed) + amount) > limit)
    return false;
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  bonds[bond_count].month = u8(month);
  bonds[bond_count].principal = amount;
  bonds[bond_count].maturity_year = year + EcBudgetBondYears;
  bonds[bond_count].monthly_payment = (amount * EcBudgetBondRate) / 1000;
  bonds[bond_count].total_payments = 12 * EcBudgetBondYears * bonds[bond_count].monthly_payment;
  bonds[bond_count].remaining_payments = bonds[bond_count].total_payments;
  bonds[bond_count].paid = 0;
  borrowed = i64(u64(borrowed) + amount);
  ++bond_count;
  DepositFunds(amount);
  return true;
}

bool EcBudget::StaticShutdown() {
  return true;
}

bool EcBudget::SimulationEnd() {
  return true;
}

u32 EcBudget::GetLayerType() {
  return kEcBudgetLayerType;
}

bool EcBudget::DebugClassTag(ScStringView *) {
  return false;
}

bool EcBudget::DebugTypeTag(ScStringView *) {
  return false;
}

// Sum the actual department, ordinance, neighbor and bond estimators in their original call order.
// Each estimator retains its independent date lookup and original32/64-bit arithmetic.
i64 EcBudget::GetEstExpenses() {
  u64 total = u64(GetEstDepartmentExpenses());
  total += u64(GetEstOrdinanceExpenses());
  total += u64(GetEstNeighborDealExpenses());
  total += u64(GetEstBondExpenses());
  return i64(total);
}

// Restore department percentages, acquire the advisor and existing tax valves, then register budget subscriptions.
// Population interpolation retains the source use of the whole population rather than subtracting the lower threshold.
bool EcBudget::SimulationBegin() {
  for (auto entry = departments.begin(); entry != departments.end(); ++entry)
    entry->second.department->SetFundingPercentage(entry->second.percentage);
  CtLayer *specific = city->GetSpecificLayer(kEcValveLayerType);
  void *found;
  if (!specific || !specific->QueryInterface(IID_EcValveLayer, &found))
    return false;
  EcValveLayer *layer = static_cast<EcValveLayer *>(found);
  advisor = ScAddRefNotNull(city->GetBudgetAdvisor());
  u32 population = u32(residential->GetPopulation());
  auto lower = EcBudgetTaxModifiers.end(), upper = EcBudgetTaxModifiers.end();
  for (auto entry = EcBudgetTaxModifiers.begin(); entry != EcBudgetTaxModifiers.end(); ++entry) {
    if (population < entry->second.population) {
      upper = entry;
      break;
    }
    lower = entry;
  }
  if (lower != EcBudgetTaxModifiers.end()) {
    if (population == lower->second.population || upper == EcBudgetTaxModifiers.end()) {
      for (u32 i = 0; i < 23; ++i)
        valve_modifiers[i] = lower->second.modifiers[i];
    } else {
      for (u32 i = 0; i < 23; ++i) {
        f64 difference = f64(upper->second.modifiers[i]) - lower->second.modifiers[i];
        i32 distance = i32(upper->second.population - lower->second.population);
        valve_modifiers[i] = f32(difference * f64(population) / f64(distance) + lower->second.modifiers[i]);
      }
    }
  }
  for (u32 i = 0; i < 3; ++i) {
    void *found_valve;
    if (!layer->GetValvePointer(taxes[i].type, IID_EcValve, &found_valve)) {
      layer->Release();
      return false;
    }
    EcValve *valve = static_cast<EcValve *>(found_valve);
    f32 modifier = GetValveModifier(taxes[i].rate);
    valve->SetTaxModifier(modifier);
    taxes[i].valve = valve;
  }
  layer->Release();
  ScGetMiscApplication()->GetCheatCodeManager()->AddNotification(static_cast<CtLayer *>(this), kEcIAmWeakCheatId);
  ScGetMiscApplication()->GetCheatCodeManager()->AddNotification(static_cast<CtLayer *>(this), kEcSoSueMeCheatId);
  ScGetMiscMessageServer()->AddNotification(static_cast<CtLayer *>(this), kCtMsg_AutoBudgetChanged);
  ScMessage message(kEcMsg_FundsChanged);
  ScGetMiscMessageServer()->MessagePost(message, false);
  return true;
}

// Import signed legacy funds, using zero for an absent property, then mirror them before reading the three tax rates.
// Missing tax properties leave their current rates untouched; present rates clamp to each profile's original maximum.
bool EcBudget::Init(CtCity *, ScImporter *importer) {
  u32 word;
  if (importer->GetMiscValue(5, reinterpret_cast<i32 *>(&word)))
    funds = i64(i32(word));
  else
    funds = 0;
  funds_copy = funds;
  for (u32 i = 0; i < 3; ++i)
    if (importer->GetMiscValue(480 + 27 * i, reinterpret_cast<i32 *>(&word))) {
      i32 rate = i32(word);
      if (rate > taxes[i].maximumRate)
        rate = taxes[i].maximumRate;
      else if (rate < 0)
        rate = 0;
      taxes[i].rate = u8(rate);
    }
  return true;
}

// Cache the auto-budget option notification and handle only the two original cheat identifiers.
// Toggle Marxism through its live virtual methods before marking the cheat; the deficit floor toggles its own stored flag.
bool EcBudget::DoMessage(ScMessage *message) {
  if (message->type == kCtMsg_AutoBudgetChanged) {
    auto_budget = u32(message->data1) != 0;
    return true;
  }
  if (message->type == kWnMsg_CheatCommand) {
    u32 identifier = u32(message->data1);
    if (identifier == kEcIAmWeakCheatId) {
      SetMarxism(!MarxismIsOn());
      ScGetMiscApplication()->GetCheatCodeManager()->SetCheatHasBeenSet(kEcIAmWeakCheatId, true);
      return true;
    }
    if (identifier == kEcSoSueMeCheatId) {
      limit_deficit = !limit_deficit;
      ScGetMiscApplication()->GetCheatCodeManager()->SetCheatHasBeenSet(kEcSoSueMeCheatId, true);
      return true;
    }
  }
  return false;
}

// Publish the genuine secondary graph view through the original collection query or single-model notification.
// The single-model notification is acknowledged even without a compatible receiver; a collection query requires one.
bool EcBudget::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  void *selected = nullptr;
  if (message->type == kWnMsg_QueryOpened) {
    if (u32(message->data1) != 1541 || !source || !source->QueryInterface(IID_UiGraphsViewIdentity, &selected))
      return false;
    ScObjectCollection *collection = static_cast<ScObjectCollection *>(selected);
    collection->Add(static_cast<ScGraphInfoView *>(chart));
    collection->Release();
    return true;
  }
  if (message->type == kUiMsg_CollectSnapshotGraphData && u32(message->data1) == kUiSnapshotCommandClassId) {
    if (source && source->QueryInterface(IID_ScModelReceiver, &selected)) {
      ScModelReceiver *receiver = static_cast<ScModelReceiver *>(selected);
      receiver->DumpGraphData(static_cast<ScGraphInfoView *>(chart));
      receiver->Release();
    }
    return true;
  }
  return false;
}

// A null department has zero funding; otherwise obtain its live ID before the actual virtual64-bit lookup.
// Truncate only at this original32-bit return boundary, distinct from the ID overload's genuine64-bit result.
u32 EcBudget::GetFunding(EcDepartmentBudget *department) {
  if (!department)
    return 0;
  u32 id = department->GetDepartmentID();
  return u32(GetFunding(id));
}

// Return zero for an unregistered ID, otherwise read its borrowed department's percentage before its current funding need.
// Retain the original wrapping64-bit product and signed division rather than narrowing the source quotient.
i64 EcBudget::GetFunding(u32 id) {
  auto entry = departments.find(id);
  if (entry == departments.end())
    return 0;
  EcDepartmentBudget *department = entry->second.department;
  u32 percentage = department->GetFundingPercentage();
  i64 needed = department->GetTotalFundingNeeded();
  return i64(u64(percentage) * u64(needed)) / 100;
}

// Compute the selected development tax from live average land value, taxable density and its stored raw rate byte.
// Density-rate multiplication wraps32 before signed division; the land-value product then wraps32 and converts unsigned.
i64 EcBudget::ComputeTax(EcTaxType type) {
  u32 average = land_value->GetAverageValue();
  i32 density = 0;
  f32 scale = 1;
  switch (type) {
  case EcTaxType::Residential:
    scale = EcBudgetResidentialTaxScale;
    density = i32(valves->GetTaxableResidentialDensity());
    break;
  case EcTaxType::Commercial:
    scale = EcBudgetCommercialTaxScale;
    density = i32(valves->GetTaxableCommercialDensity());
    break;
  case EcTaxType::Industrial:
    scale = EcBudgetIndustrialTaxScale;
    density = i32(valves->GetTaxableIndustrialDensity());
    break;
  }
  if (density > 0 && average < EcBudgetMinimumLandValue)
    average = EcBudgetMinimumLandValue;
  u32 rate = taxes[i32(type)].rate;
  i32 weighted = i32(u32(density) * rate) / 100;
  u32 base = u32(weighted) * average;
  return i64(f64(base) * scale);
}

// Estimate all three tax categories using one original city-date query and each live development tax calculation.
// Capture each year-to-date value before its callbacks, then add the remaining-month product with wrapping64 arithmetic.
i64 EcBudget::GetEstTaxIncome() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 total = 0;
  for (u32 i = 0; i < 3; ++i) {
    u64 accrued = u64(taxes[i].yearTotal) + total;
    i64 monthly = ComputeTax(EcTaxType(i));
    total = u64(12 - month) * u64(monthly) + accrued;
  }
  return i64(total);
}

// Query the date before capturing fare receipts, then add the remaining-month product of live signed64 fare income.
// Both accumulator operations preserve the original wrapping64 bits.
i64 EcBudget::GetEstFareIncome() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 income = u64(fare_income.yearTotal);
  i64 recurring = traffic->GetFareIncome();
  return i64(income + u64(12 - month) * u64(recurring));
}

// Estimate ordinance receipts from a separate date query and captured year total before asking the live layer.
// Sign-extend the original monthly DWORD result before multiplying by unsigned remaining months.
i64 EcBudget::GetEstOrdinanceIncome() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 income = u64(ordinance_income.yearTotal);
  i64 recurring = i32(ordinances->GetTotalMonthlyOrdinanceIncome());
  return i64(income + u64(12 - month) * u64(recurring));
}

// Capture accrued neighbor income after its own date query, then obtain the original full signed64 recurring value.
i64 EcBudget::GetEstNeighborDealIncome() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 income = u64(neighbor_income.yearTotal);
  i64 recurring = neighbors->GetTotalMonthlyNeighborDealIncome();
  return i64(income + u64(12 - month) * u64(recurring));
}

// Retain accrued business receipts across the advisor callback and extend its signed32 monthly result to64 bits.
// Preserve the date-before-capture ordering independently of every other income category.
i64 EcBudget::GetEstBusinessDealIncome() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 income = u64(business_income.yearTotal);
  i64 recurring = i32(advisor->GetTotalMonthlyBusinessDealIncome());
  return i64(income + u64(12 - month) * u64(recurring));
}

// Select the original monthly disaster-relief entry after decrementing the city month, clamping unsigned overflow to11.
// This category returns that saved receipt rather than multiplying a recurring estimate.
i64 EcBudget::GetEstDisasterReliefIncome() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  if (month > 11)
    month = 11;
  return disaster_relief.monthly[month];
}

// Sum the six actual estimate methods in source order, retaining their independent date queries and live captures.
// Unsigned addition preserves signed64 result bits without introducing signed-overflow behavior.
i64 EcBudget::GetEstIncome() {
  u64 total = u64(GetEstFareIncome());
  total += u64(GetEstOrdinanceIncome());
  total += u64(GetEstNeighborDealIncome());
  total += u64(GetEstBusinessDealIncome());
  total += u64(GetEstTaxIncome());
  total += u64(GetEstDisasterReliefIncome());
  return i64(total);
}

u32 EcBudgetMaximumBonds = 10, EcBudgetBondValue = 25000, EcBudgetBondRate = 15, EcBudgetBondYears = 10;
f32 EcBudgetResidentialTaxScale = 0.0143f, EcBudgetCommercialTaxScale = 0.0143f, EcBudgetIndustrialTaxScale = 0.0143f;
u8 EcBudgetMinimumLandValue = 5;
std::map<i32, i32> EcBudgetSpecialCosts;
std::map<u32, i32> EcBudgetPlacementCosts;
std::map<u32, u32> EcBudgetDiscounts;
std::map<u32, EcTaxModifier> EcBudgetTaxModifiers;

static void EcBudgetTakeToken(ScString *input, char delimiter, ScString *out) {
  for (char *byte = input->begin; byte != input->end; ++byte)
    if (*byte == delimiter) {
      u32 length = u32(byte - input->begin);
      out->FromChar(input->begin, length);
      input->Erase(0, length + 1);
      return;
    }
  out->FromChar("", 0);
}

// Retain the original placement callback's key/value copies and first-space extraction before parsing the numeric cost.
// The original ignores its generic enumeration context; the real callback receives the original String references.
static void EcBudgetParsePlacementCost(const ScString &key, const ScString &value, void *) {
  ScString identifier(key), remaining(value), token;
  u32 type = ScParseMiscNumber(identifier.begin);
  EcBudgetTakeToken(&remaining, ' ', &token);
  if (token.begin == token.end)
    token = remaining;
  EcBudgetPlacementCosts[type] = i32(ScParseMiscNumber(token.begin));
}

static u8 EcBudgetReadSetting(EcIni *ini, const char *sectionName, const char *keyName, ScString *out) {
  ScString key(keyName), section(sectionName);
  return ini->GetValue(&section, &key, out);
}

// Clear the original three cost tables, read packaged budget settings and recompute every stored tax-demand curve.
// Missing settings preserve module defaults or previous values; the source does not clear its separate curve table.
bool EcBudget::StaticInit(CtDefinition *) {
  EcBudgetDiscounts.clear();
  EcBudgetPlacementCosts.clear();
  EcBudgetSpecialCosts.clear();
  {
    ScString path, value;
    EcIni ini;
    ScGetMiscApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SC3BudgetLayer.INI");
    ini.SelectFile(&path);
    ScGetMiscApplication()->GetAppDataDirectory(&path);
    path.Append("\\Sys\\SYS.PAK");
    ini.SelectPackage(path);
    {
      ScString section("BuildingPlacementCosts");
      ini.EnumerateSection(&section, EcBudgetParsePlacementCost, nullptr);
    }
    const char *const kSpecialCostNames[] = {"LowDensityZone", "MedDensityZone", "HighDensityZone", "LandfillZone",
        "AirportZone", "SeaportZone", "Dezone", "Demolish", "LevelTerrain", "RaiseTerrain", "LowerTerrain",
        "DecorativeWater", "DemolishRubbleTile"};
    for (i32 i = kEcCostLowDensityZone; i <= kEcCostDemolishRubble; ++i)
      if (EcBudgetReadSetting(&ini, "SpecialCosts", kSpecialCostNames[i], &value))
        EcBudgetSpecialCosts[i] = i32(ScParseMiscNumber(value.begin));
    if (EcBudgetReadSetting(&ini, "Bonds", "MaxBonds", &value))
      EcBudgetMaximumBonds = ScParseMiscNumber(value.begin);
    if (EcBudgetReadSetting(&ini, "Bonds", "MonthlyPaymentPer1K", &value))
      EcBudgetBondRate = ScParseMiscNumber(value.begin);
    if (EcBudgetReadSetting(&ini, "Bonds", "MaxBondAmt", &value))
      EcBudgetBondValue = 1000 * ScParseMiscNumber(value.begin);
    if (EcBudgetReadSetting(&ini, "Bonds", "BondLifespan", &value))
      EcBudgetBondYears = ScParseMiscNumber(value.begin);
    if (EcBudgetReadSetting(&ini, "TuningParameters", "LVToResTaxTransmogrifier", &value))
      EcBudgetResidentialTaxScale = f32(f64(ScParseMiscNumber(value.begin)) * f64(0.0001f));
    if (EcBudgetReadSetting(&ini, "TuningParameters", "LVToComTaxTransmogrifier", &value))
      EcBudgetCommercialTaxScale = f32(f64(ScParseMiscNumber(value.begin)) * f64(0.0001f));
    if (EcBudgetReadSetting(&ini, "TuningParameters", "LVToIndTaxTransmogrifier", &value))
      EcBudgetIndustrialTaxScale = f32(f64(ScParseMiscNumber(value.begin)) * f64(0.0001f));
    if (EcBudgetReadSetting(&ini, "TuningParameters", "MinAverageLV", &value)) {
      u32 minimum = ScParseMiscNumber(value.begin);
      EcBudgetMinimumLandValue = u8(minimum > 255 ? 255 : minimum);
    }
    if (EcBudgetReadSetting(&ini, "TuningParameters", "TireRecyclingOrdEffect", &value)) {
      ScString token;
      EcBudgetTakeToken(&value, ':', &token);
      u32 discount = ScParseMiscNumber(token.begin);
      if (discount <= 100) {
        bool done = false;
        do {
          EcBudgetTakeToken(&value, ',', &token);
          if (token.begin == token.end) {
            token = value;
            done = true;
          }
          if (token.begin == token.end)
            done = true;
          else
            EcBudgetDiscounts[ScParseMiscNumber(token.begin)] = discount;
        } while (!done);
      }
    }
    if (EcBudgetReadSetting(&ini, "TuningParameters", "TaxationDemandEffect", &value)) {
      ScString token;
      bool done = false;
      for (;;) {
        EcTaxModifier curve = {};
        EcBudgetTakeToken(&value, ',', &token);
        if (token.begin == token.end)
          break;
        curve.bonus = f32(f64(i32(atol(token.begin))) * f64(0.0001f));
        EcBudgetTakeToken(&value, ',', &token);
        if (token.begin == token.end)
          break;
        curve.penalty = f32(f64(i32(atol(token.begin))) * f64(0.0001f));
        EcBudgetTakeToken(&value, ',', &token);
        if (token.begin == token.end)
          break;
        u32 neutral = ScParseMiscNumber(token.begin);
        curve.neutral = u8(neutral > 22 ? 22 : neutral);
        EcBudgetTakeToken(&value, ';', &token);
        if (token.begin == token.end) {
          token = value;
          done = true;
        }
        if (token.begin == token.end)
          break;
        curve.population = ScParseMiscNumber(token.begin);
        EcBudgetTaxModifiers[curve.population] = curve;
        if (done)
          break;
      }
    }
  }
  for (auto entry = EcBudgetTaxModifiers.begin(); entry != EcBudgetTaxModifiers.end(); ++entry) {
    EcTaxModifier curve = entry->second;
    u32 i = 0;
    for (; i < curve.neutral; ++i)
      curve.modifiers[i] = f32(f64(curve.neutral - i) * curve.bonus / f64(curve.neutral));
    curve.modifiers[i] = 0;
    for (++i; i <= 22; ++i)
      curve.modifiers[i] = f32(f64(i - curve.neutral) * curve.penalty / f64(22 - curve.neutral));
    EcBudgetTaxModifiers[curve.population] = curve;
  }
  return true;
}

// Return the development cost from the signed32-bit key table, inserting a missing zero entry unless Marxism is enabled.
i32 EcBudget::GetCost(i32 development) {
  if (marxism)
    return 0;
  return EcBudgetSpecialCosts[development];
}

// Cache the closest ancestor placement cost in unsigned key order, keeping the first equal-distance match.
// The tire-recycling ordinance applies its per-type percentage with original32-bit multiplication and unsigned division.
i32 EcBudget::GetCost(u32 type) {
  if (marxism)
    return 0;
  i32 cost = 0;
  if (EcBudgetPlacementCosts.find(type) == EcBudgetPlacementCosts.end()) {
    u32 best = 50000;
    for (auto entry = EcBudgetPlacementCosts.begin(); entry != EcBudgetPlacementCosts.end(); ++entry) {
      u32 depth;
      if (agents->IsAncestor(entry->first, type, &depth) && depth < best) {
        best = depth;
        cost = entry->second;
        if (!depth)
          break;
      }
    }
    EcBudgetPlacementCosts[type] = cost;
  } else {
    cost = EcBudgetPlacementCosts[type];
  }
  if (ordinances->QueryOrdinanceOn(kCtOrdinance_TireRecycling)) {
    u32 discount = EcBudgetDiscounts[type];
    if (discount)
      cost = i32(u32(cost) - u32(cost) * discount / 100);
  }
  return cost;
}

static ScString EcBudgetDeficitCheatName("so sue me");

// Initialize the actual city dependencies, registered cheats, tax profiles, loan storage and retained history object.
// Preserve the source's early failure behavior and resource/callback ordering before publishing initialized at the end.
bool EcBudget::Init(CtCity *inputCity) {
  if (initialized)
    return true;
  marxism = false;
  limit_deficit = false;
  auto_budget = inputCity->IsAutoBudgetEnabled();
  {
    ScString name("I am weak");
    ScGetMiscApplication()->GetCheatCodeManager()->RegisterCheatCode(kEcIAmWeakCheatId, name);
  }
  ScGetMiscApplication()->GetCheatCodeManager()->RegisterCheatCode(kEcSoSueMeCheatId, EcBudgetDeficitCheatName);
  funds = 0;
  funds_copy = 0;
  city = ScAddRefNotNull(inputCity);
  agents = ScGetMiscApplication()->GetAppAgentTypeTree();
  if (!agents)
    return false;
  ordinances = city->OrdinanceLayer();
  if (!ordinances) {
    Shutdown();
    return false;
  }
  ordinances->AddRef();
  neighbors = city->NeighborsLayer();
  if (!neighbors) {
    Shutdown();
    return false;
  }
  neighbors->AddRef();
  traffic = city->TrafficLayer();
  if (!traffic) {
    Shutdown();
    return false;
  }
  traffic->AddRef();
  land_value = city->LandValueLayer();
  if (!land_value) {
    Shutdown();
    return false;
  }
  land_value->AddRef();
  residential = city->ResidentialLayer();
  if (!residential) {
    Shutdown();
    return false;
  }
  residential->AddRef();
  CtLayer *specific = city->GetSpecificLayer(kEcValveLayerType);
  if (!specific || !specific->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valves))) {
    Shutdown();
    return false;
  }
  for (u32 i = 0; i < 3; ++i) {
    taxes[i].rate = 7;
    taxes[i].maximumRate = 22;
    for (u32 month = 0; month < 12; ++month)
      taxes[i].monthly[month] = 0;
    taxes[i].yearTotal = 0;
    taxes[i].valve = nullptr;
  }
  const u32 types[] = {3000, 5000, 8000};
  for (u32 i = 0; i < 3; ++i) {
    {
      CtMiscTextResource resource(163 + i, kRsGameTextGroup);
      ScString text = resource.GetText();
      ScString copy(text);
      taxes[i].name = copy;
    }
    taxes[i].type = types[i];
  }
  ordinance_income.yearTotal = 0;
  ordinance_expenses.yearTotal = 0;
  neighbor_income.yearTotal = 0;
  neighbor_expenses.yearTotal = 0;
  penalty_expenses = 0;
  business_income.yearTotal = 0;
  for (u32 month = 0; month < 12; ++month) {
    ordinance_expenses.monthly[month] = 0;
    ordinance_income.monthly[month] = 0;
    neighbor_expenses.monthly[month] = 0;
    neighbor_income.monthly[month] = 0;
    business_income.monthly[month] = 0;
    bond_expenses[month] = 0;
    disaster_relief.monthly[month] = 0;
    fare_income.monthly[month] = 0;
  }
  disaster_relief.yearTotal = 0;
  fare_income.yearTotal = 0;
  bonds = new EcBond[EcBudgetMaximumBonds];
  bond_count = 0;
  yearly_bond_expenses = 0;
  borrowed = 0;
  chart = ScAddRefNotNull(new ScGraphInfo(100));
  chart->SetAutoMaxFactors(200, 1.1f);
  chart->SetAutoMax(true);
  {
    CtMiscTextResource resource(kRsGameFundsText, kRsGameTextGroup);
    ScString text = resource.GetText();
    ScString copy(text);
    ScString argument(copy);
    chart->SetSelectionLabel(argument);
  }
  {
    CtMiscTextResource resource(kRsGameFundsText, kRsGameTextGroup);
    ScString text = resource.GetText();
    ScString copy(text);
    label = copy;
  }
  chart->SetCurrentValueLabel(ScGraphDataSet::Month, label);
  chart->SetCurrentValueLabel(ScGraphDataSet::Year, label);
  chart->SetCurrentValueLabel(ScGraphDataSet::Decade, label);
  u32 selection = kEcFundsGraphSelectionId, value = kEcFundsGraphValueId;
  chart->SetSelectionLabelID(selection);
  chart->SetValueLabelID(value);
  chart->SetColor(kCtBudgetGraphColor);
  initialized = true;
  return true;
}

// Read the complete budget record, retaining partial field writes and registered borrowed department pointers.
// Native scratch starts defined; the source's unconditional department-count read still resets earlier tax-phase failure.
bool EcBudget::Init(CtCity *loadCity, RsDatabaseSegment *segment) {
  RsKey key = {kEcBudgetRecordType, kEcBudgetRecordGroup, 0};
  RsRecord *record;
  if (!segment->OpenRecord(key, &record))
    return false;
  u8 valid = 0;
  void *selected;
  if (record->QueryInterface(IID_RsStream, &selected)) {
    RsStream *stream = static_cast<RsStream *>(selected);
    valid = stream->ReadI64(&funds);
    funds_copy = funds;
    u32 profiles = 0, count = 0;
    if (valid)
      stream->ReadU32(&profiles);
    valid = profiles == 3;
    for (u32 i = 0; i < 3 && valid; ++i) {
      valid = stream->ReadU32(&taxes[i].type);
      if (valid)
        valid = stream->ReadU8(&taxes[i].rate);
      for (u32 month = 0; month < 12 && valid; ++month)
        valid = stream->ReadI64(&taxes[i].monthly[month]);
      if (valid)
        valid = stream->ReadI64(&taxes[i].yearTotal);
    }
    valid = stream->ReadU32(&count);
    EcLineItem row = {};
    for (u32 i = 0; i < count && valid; ++i) {
      valid = stream->ReadU32(&row.id);
      valid = valid && stream->ReadU8(&row.percentage) && stream->ReadI64(&row.funding);
      for (u32 month = 0; month < 12 && valid; ++month)
        valid = stream->ReadI64(&row.monthly[month]);
      if (valid)
        valid = stream->ReadI64(&row.yearTotal);
      if (departments.find(row.id) != departments.end()) {
        row.department = departments[row.id].department;
        departments[row.id] = row;
      }
    }
    valid = valid && stream->ReadI64(&ordinance_expenses.yearTotal) && stream->ReadI64(&ordinance_income.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month) {
      valid = stream->ReadI64(&ordinance_expenses.monthly[month]);
      if (valid)
        valid = stream->ReadI64(&ordinance_income.monthly[month]);
    }
    if (valid)
      valid = stream->ReadU32(&bond_count);
    for (u32 i = 0; i < bond_count && valid; ++i) {
      if (i < EcBudgetMaximumBonds) {
        valid = stream->ReadU8(&bonds[i].month);
        valid = valid && stream->ReadU32(&bonds[i].maturity_year) && stream->ReadU32(&bonds[i].principal) &&
            stream->ReadU32(&bonds[i].total_payments) && stream->ReadU32(&bonds[i].remaining_payments) &&
            stream->ReadU32(&bonds[i].paid) && stream->ReadU32(&bonds[i].monthly_payment);
      } else {
        u8 month;
        u32 discarded;
        valid = stream->ReadU8(&month);
        for (u32 field = 0; field < 6 && valid; ++field)
          valid = stream->ReadU32(&discarded);
      }
    }
    if (valid)
      valid = stream->ReadI64(&yearly_bond_expenses);
    for (u32 month = 0; month < 12 && valid; ++month)
      valid = stream->ReadU32(&bond_expenses[month]);
    valid = valid && stream->ReadI64(&borrowed) && stream->ReadI64(&disaster_relief.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month)
      valid = stream->ReadI64(&disaster_relief.monthly[month]);
    valid = valid && chart->SerialRead(loadCity, stream) && stream->ReadI64(&fare_income.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month)
      valid = stream->ReadI64(&fare_income.monthly[month]);
    valid = valid && stream->ReadI64(&neighbor_expenses.yearTotal) && stream->ReadI64(&neighbor_income.yearTotal) &&
        stream->ReadI64(&business_income.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month) {
      valid = stream->ReadI64(&neighbor_expenses.monthly[month]);
      valid =
          valid && stream->ReadI64(&neighbor_income.monthly[month]) && stream->ReadI64(&business_income.monthly[month]);
    }
    if (valid)
      valid = stream->ReadI64(&penalty_expenses);
    stream->Release();
  }
  segment->CloseRecord(&record);
  return valid != 0;
}

EcBudgetManipulator::EcBudgetManipulator() : budget(nullptr) {
}

EcBudgetManipulator::~EcBudgetManipulator() {
  Shutdown();
}

bool EcBudgetManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcBudgetManipulator::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 EcBudgetManipulator::Release() {
  return ScMiscRefCount::Release();
}

bool EcBudgetManipulator::Init(EcBudget *owner) {
  budget = owner;
  owner->AddRef();
  return true;
}

bool EcBudgetManipulator::Shutdown() {
  ScRelease(&budget);
  return true;
}

// Lock the application's current city around the budget's own monthly operation and its separate internal city lock.
// Reload the current application city before the outer unlock, as the original does after callbacks and notifications.
bool EcBudgetManipulator::Apply(CtAgent *) {
  ScGetMiscCity()->Lock();
  budget->EndOfMonth();
  ScGetMiscCity()->Unlock();
  return true;
}

bool EcBudgetManipulator::IsLockingCity() {
  return true;
}

bool EcBudgetManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcBudgetManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcBudgetManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

// Publish flags4 before testing only the supplied agent type's ID; a matching request constructs a fresh owner-held
// object.
bool EcBudget::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  if (type.Id() == kCtAgentType_AreaAgent) {
    EcBudgetManipulator *manipulator = new EcBudgetManipulator;
    manipulator->Init(this);
    *out = manipulator;
  } else {
    *out = nullptr;
  }
  return true;
}

// Book the previous month under the real city lock, settle annual balances and update the original graph histories.
// Retain callback order, source loan-removal traversal and all wrapping arithmetic before unlocked notifications.
bool EcBudget::EndOfMonth() {
  city->Lock();
  if (funds != funds_copy)
    funds = funds_copy;
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  if (month) {
    --month;
    if (month > 11)
      month = 11;
  } else {
    --year;
    month = 11;
  }
  u32 population = u32(residential->GetPopulation());
  auto lower = EcBudgetTaxModifiers.end(), upper = EcBudgetTaxModifiers.end();
  for (auto entry = EcBudgetTaxModifiers.begin(); entry != EcBudgetTaxModifiers.end(); ++entry) {
    if (population < entry->second.population) {
      upper = entry;
      break;
    }
    lower = entry;
  }
  if (lower != EcBudgetTaxModifiers.end()) {
    if (population == lower->second.population || upper == EcBudgetTaxModifiers.end()) {
      for (u32 i = 0; i < 23; ++i)
        valve_modifiers[i] = lower->second.modifiers[i];
    } else {
      for (u32 i = 0; i < 23; ++i) {
        f64 difference = f64(upper->second.modifiers[i]) - lower->second.modifiers[i];
        i32 distance = i32(upper->second.population - lower->second.population);
        valve_modifiers[i] = f32(difference * f64(population) / f64(distance) + lower->second.modifiers[i]);
      }
    }
  }
  for (u32 i = 0; i < 3; ++i) {
    i64 tax = ComputeTax(EcTaxType(i));
    taxes[i].monthly[month] = tax;
    taxes[i].yearTotal = i64(u64(taxes[i].yearTotal) + u64(tax));
    taxes[i].name.ToChar();
  }
  i64 current = i32(ordinances->GetTotalMonthlyOrdinanceIncome());
  ordinance_income.monthly[month] = current;
  ordinance_income.yearTotal = i64(u64(ordinance_income.yearTotal) + u64(current));
  current = neighbors->GetTotalMonthlyNeighborDealIncome();
  neighbor_income.monthly[month] = current;
  neighbor_income.yearTotal = i64(u64(neighbor_income.yearTotal) + u64(current));
  current = i32(advisor->GetTotalMonthlyBusinessDealIncome());
  business_income.monthly[month] = current;
  business_income.yearTotal = i64(u64(business_income.yearTotal) + u64(current));
  current = traffic->GetFareIncome();
  fare_income.monthly[month] = current;
  fare_income.yearTotal = i64(u64(fare_income.yearTotal) + u64(current));
  current = disaster_relief.monthly[month];
  disaster_relief.monthly[month == 11 ? 0 : month + 1] = 0;
  disaster_relief.yearTotal = i64(u64(disaster_relief.yearTotal) + u64(current));
  current = i32(ordinances->GetTotalMonthlyOrdinanceExpense());
  ordinance_expenses.monthly[month] = current;
  ordinance_expenses.yearTotal = i64(u64(ordinance_expenses.yearTotal) + u64(current));
  i32 penalty = neighbors->GetNeighborDealPenaltyExpense();
  penalty_expenses = i64(u64(penalty_expenses) + u64(i64(penalty)));
  i32 recurring = neighbors->GetTotalMonthlyNeighborDealExpense();
  current = i64(u64(penalty_expenses) + u64(i64(recurring)));
  penalty_expenses = 0;
  neighbor_expenses.monthly[month] = current;
  neighbor_expenses.yearTotal = i64(u64(neighbor_expenses.yearTotal) + u64(current));
  for (auto entry = departments.begin(); entry != departments.end(); ++entry) {
    EcDepartmentBudget *department = entry->second.department;
    u32 percentage = department->GetFundingPercentage();
    i64 needed = department->GetTotalFundingNeeded();
    entry->second.funding = i64(u64(percentage) * u64(needed)) / 100;
    current = entry->second.funding;
    entry->second.monthly[month] = current;
    entry->second.yearTotal = i64(u64(entry->second.yearTotal) + u64(current));
    entry->second.department->GetDepartmentName()->ToChar();
  }
  u64 monthlyBonds = 0;
  for (u32 i = 0; i < bond_count; ++i) {
    u32 payment = bonds[i].monthly_payment;
    yearly_bond_expenses = i64(u64(yearly_bond_expenses) + payment);
    monthlyBonds += payment;
    bonds[i].remaining_payments -= payment;
    bonds[i].paid += payment;
    if (!bonds[i].remaining_payments || (bonds[i].month == month && bonds[i].maturity_year == year)) {
      borrowed = i64(u64(borrowed) - bonds[i].principal);
      for (u32 moved = i; moved + 1 < bond_count; ++moved)
        bonds[moved] = bonds[moved + 1];
      --bond_count;
    }
  }
  bond_expenses[month] = u32(monthlyBonds);
  if (month == 11) {
    for (u32 i = 0; i < 3; ++i) {
      funds = i64(u64(funds) + u64(taxes[i].yearTotal));
      taxes[i].yearTotal = 0;
    }
    funds = i64(u64(funds) + u64(ordinance_income.yearTotal));
    ordinance_income.yearTotal = 0;
    funds = i64(u64(funds) + u64(neighbor_income.yearTotal));
    neighbor_income.yearTotal = 0;
    funds = i64(u64(funds) + u64(business_income.yearTotal));
    business_income.yearTotal = 0;
    funds = i64(u64(funds) + u64(fare_income.yearTotal));
    fare_income.yearTotal = 0;
    funds = i64(u64(funds) + u64(disaster_relief.yearTotal));
    disaster_relief.yearTotal = 0;
    for (auto entry = departments.begin(); entry != departments.end(); ++entry) {
      i64 surplus = entry->second.department->GetLastYearSurplus();
      funds = i64(u64(funds) + u64(surplus));
      funds = i64(u64(funds) - u64(entry->second.yearTotal));
      entry->second.yearTotal = 0;
    }
    funds = i64(u64(funds) - u64(ordinance_expenses.yearTotal));
    ordinance_expenses.yearTotal = 0;
    funds = i64(u64(funds) - u64(neighbor_expenses.yearTotal));
    neighbor_expenses.yearTotal = 0;
    funds = i64(u64(funds) - u64(yearly_bond_expenses));
    yearly_bond_expenses = 0;
  }
  if (limit_deficit && funds <= -kEcBudgetDebtMagnitudeLimit)
    funds = -99999;
  i32 lowFunds = i32(u32(u64(funds)));
  if (lowFunds < -kEcBudgetDebtMagnitudeLimit)
    lowFunds = -kEcBudgetDebtMagnitudeLimit;
  i32 graphValue = i32(u32(lowFunds) + kEcBudgetGraphValueBias) / 1000;
  chart->SetMonthData(u32(graphValue));
  bool showBudget = false;
  if (month == 11) {
    u32 average = chart->GetMonthAverage();
    chart->SetYearData(average);
    if ((year + 1) % 10 == 0) {
      average = chart->GetYearAverage();
      chart->SetDecadeData(average);
    }
    if (funds > -kEcBudgetDebtMagnitudeLimit && (!auto_budget || funds < 0))
      showBudget = true;
  }
  {
    ScString formatted, money;
    ScLanguageUtility *language = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    for (u32 i = 0; i < 3; ++i) {
      u32 value;
      if (i == 0)
        chart->GetMonthData(&value);
      else if (i == 1)
        chart->GetYearData(&value);
      else
        chart->GetDecadeData(&value);
      language->MakeMoneyString(i64(value * 1000 - kEcBudgetGraphValueBias), &money);
      const char *text = money.begin, *pattern = label.ToChar();
      formatted.Sprintf(pattern, text);
      chart->SetCurrentValueLabel(ScGraphDataSet(i), formatted);
    }
  }
  funds_copy = funds;
  city->Unlock();
  ScMessage message(kEcMsg_FundsChanged);
  ScGetMiscMessageServer()->MessagePost(message, false);
  if (showBudget) {
    message = ScMessage(kWnMsg_GameMenuActivateItem, kUiMenuBudgetItemId, 0, 0);
    ScGetMiscMessageServer()->MessagePost(message, false);
  }
  return true;
}

// Write the complete unframed budget record, including live department percentages and the real graph history transport.
// The department-count write deliberately resets earlier tax-phase failure, matching the source rather than repairing it.
bool EcBudget::Save(CtCity *saveCity, RsDatabaseSegment *segment) {
  RsKey key = {kEcBudgetRecordType, kEcBudgetRecordGroup, 0};
  RsRecord *record;
  if (!segment->OpenRecordForWrite(key, &record))
    return false;
  u8 valid = 0;
  void *selected;
  if (record->QueryInterface(IID_RsStream, &selected)) {
    RsStream *stream = static_cast<RsStream *>(selected);
    valid = stream->WriteI64(funds);
    if (valid)
      valid = stream->WriteU32(3);
    for (u32 i = 0; i < 3 && valid; ++i) {
      valid = stream->WriteU32(taxes[i].type);
      if (valid)
        valid = stream->WriteU8(taxes[i].rate);
      for (u32 month = 0; month < 12 && valid; ++month)
        valid = stream->WriteI64(taxes[i].monthly[month]);
      if (valid)
        valid = stream->WriteI64(taxes[i].yearTotal);
    }
    valid = stream->WriteU32(u32(departments.size()));
    auto entry = departments.begin(), end = departments.end();
    while (entry != end && valid) {
      valid = stream->WriteU32(entry->second.id);
      if (valid) {
        u8 percentage = entry->second.department->GetFundingPercentage();
        valid = stream->WriteU8(percentage);
      }
      if (valid)
        valid = stream->WriteI64(entry->second.funding);
      for (u32 month = 0; month < 12 && valid; ++month)
        valid = stream->WriteI64(entry->second.monthly[month]);
      if (valid)
        valid = stream->WriteI64(entry->second.yearTotal);
      ++entry;
    }
    valid = valid && stream->WriteI64(ordinance_expenses.yearTotal) && stream->WriteI64(ordinance_income.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month) {
      valid = stream->WriteI64(ordinance_expenses.monthly[month]);
      if (valid)
        valid = stream->WriteI64(ordinance_income.monthly[month]);
    }
    if (valid)
      valid = stream->WriteU32(bond_count);
    for (u32 i = 0; i < bond_count && valid; ++i) {
      valid = stream->WriteU8(bonds[i].month);
      valid = valid && stream->WriteU32(bonds[i].maturity_year) && stream->WriteU32(bonds[i].principal) &&
          stream->WriteU32(bonds[i].total_payments) && stream->WriteU32(bonds[i].remaining_payments) &&
          stream->WriteU32(bonds[i].paid) && stream->WriteU32(bonds[i].monthly_payment);
    }
    if (valid)
      valid = stream->WriteI64(yearly_bond_expenses);
    for (u32 month = 0; month < 12 && valid; ++month)
      valid = stream->WriteU32(bond_expenses[month]);
    valid = valid && stream->WriteI64(borrowed) && stream->WriteI64(disaster_relief.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month)
      valid = stream->WriteI64(disaster_relief.monthly[month]);
    valid = valid && chart->SerialWrite(saveCity, stream) && stream->WriteI64(fare_income.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month)
      valid = stream->WriteI64(fare_income.monthly[month]);
    valid = valid && stream->WriteI64(neighbor_expenses.yearTotal) && stream->WriteI64(neighbor_income.yearTotal) &&
        stream->WriteI64(business_income.yearTotal);
    for (u32 month = 0; month < 12 && valid; ++month) {
      valid = stream->WriteI64(neighbor_expenses.monthly[month]);
      valid =
          valid && stream->WriteI64(neighbor_income.monthly[month]) && stream->WriteI64(business_income.monthly[month]);
    }
    if (valid)
      valid = stream->WriteI64(penalty_expenses);
    stream->Release();
  }
  segment->CloseRecord(&record);
  return valid != 0;
}

// Preserve the five ordered budget reads, live balance checks after formatting and repeated window queries while
// aligning.
void CtBudgetWindow::UpdateTotals() {
  ScString text;
  i64 funds = budget->GetTotalFunds(), yearIncome = budget->GetYTDIncome(), estimatedIncome = budget->GetEstIncome();
  i64 yearExpense = budget->GetYTDExpenses(), estimatedExpense = budget->GetEstExpenses();
  year_balance = i64(u64(yearIncome) - u64(yearExpense));
  estimated_balance = i64(u64(estimatedIncome) - u64(estimatedExpense));
  estimated_funds = i64(u64(funds) + u64(estimated_balance));
  i64 negativeYearExpense = i64(u64(0) - u64(yearExpense));
  i64 negativeEstimatedExpense = i64(u64(0) - u64(estimatedExpense));
  for (u32 i = 0; i < 8; ++i) {
    i64 value;
    switch (i) {
    case 0:
      value = estimated_funds;
      break;
    case 1:
      value = funds;
      break;
    case 2:
      value = estimated_balance;
      break;
    case 3:
      value = year_balance;
      break;
    case 4:
      value = negativeEstimatedExpense;
      break;
    case 5:
      value = negativeYearExpense;
      break;
    case 6:
      value = estimatedIncome;
      break;
    default:
      value = yearIncome;
      break;
    }
    language->MakeMoneyString(value, &text);
    if (i < 6) {
      bool negative = value < 0;
      if (i == 0)
        negative = estimated_funds < 0;
      else if (i == 2)
        negative = estimated_balance < 0;
      else if (i == 3)
        negative = year_balance < 0;
      WnWindow *window = totals[i]->GetWindow();
      u32 color = negative ? window->MakeFillColor(255, 0, 0) : window->MakeFillColor(0, 0, 0);
      totals[i]->SetTextColor(color);
    }
    totals[i]->SetCaption(text);
    totals[i]->Fit();
    WnWindow *measured = totals[i]->GetWindow();
    i32 measuredWidth = measured->GetW();
    i32 width = i == 3 || i == 5 || i == 7 ? column_width >> 1 : column_width;
    u32 right = u32(column_left) + u32(width);
    i32 left = i32(right - u32(measuredWidth) - 2);
    WnWindow *vertical = totals[i]->GetWindow(), *moving = totals[i]->GetWindow();
    i32 top = vertical->GetT();
    moving->GZWinMoveTo(left, top);
  }
}

// Refresh the five income estimates and three tax estimates in source order, then invoke the real parent virtual phase.
void CtBudgetIncomeView::UpdateTotals() {
  ScString text;
  const u32 slots[5] = {1, 3, 4, 2, 0};
  for (u32 i = 0; i < 5; ++i) {
    i64 value;
    switch (i) {
    case 0:
      value = budget->GetEstOrdinanceIncome();
      break;
    case 1:
      value = budget->GetEstNeighborDealIncome();
      break;
    case 2:
      value = budget->GetEstBusinessDealIncome();
      break;
    case 3:
      value = budget->GetEstFareIncome();
      break;
    default:
      value = budget->GetEstDisasterReliefIncome();
      break;
    }
    language->MakeMoneyString(value, &text);
    u32 slot = slots[i];
    totals[slot]->SetCaption(text);
    totals[slot]->Fit();
    i32 left = i32(column_width + column_left - u32(totals[slot]->GetWindow()->GetW()) - 2);
    WnWindow *vertical = totals[slot]->GetWindow(), *moving = totals[slot]->GetWindow();
    i32 top = vertical->GetT();
    moving->GZWinMoveTo(left, top);
  }
  for (u32 i = 0; i < 3; ++i) {
    i64 monthly = budget->ComputeTax(EcTaxType(i));
    u64 projected = u64(remaining_months) * u64(monthly);
    i64 value = i64(projected + u64(budget->taxes[i].yearTotal));
    language->MakeMoneyString(value, &text);
    tax_totals[i]->SetCaption(text);
    tax_totals[i]->Fit();
    i32 left = i32(column_width + column_left - u32(tax_totals[i]->GetWindow()->GetW()) - 2);
    WnWindow *vertical = tax_totals[i]->GetWindow(), *moving = tax_totals[i]->GetWindow();
    i32 top = vertical->GetT();
    moving->GZWinMoveTo(left, top);
  }
  owner->UpdateTotals();
}

// Accept only the three tax spinners, comparing the full input before narrowing the stored rate to its original byte.
bool CtBudgetIncomeView::OnSpinnerChanged(WnWindow *, u32 identifier, i32 value) {
  if (identifier < 150 || identifier >= 153)
    return false;
  u32 index = identifier - 150;
  if (value != budget->taxes[index].rate) {
    ApBoxX *box = ScGetMiscApplication()->GetBoxX();
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    budget->taxes[index].rate = u8(value);
    f32 modifier = budget->GetValveModifier(u8(value));
    budget->taxes[index].valve->SetTaxModifier(modifier);
    UpdateTotals();
  }
  return true;
}

// Query the date before summing accrued department receipts, then traverse the ordered registry for cached funding.
// Remaining-month products and accumulation retain the original full64-bit representation.
i64 EcBudget::GetEstDepartmentExpenses() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 total = 0;
  for (auto entry = departments.begin(); entry != departments.end(); ++entry)
    total += u64(entry->second.yearTotal);
  for (auto entry = departments.begin(); entry != departments.end(); ++entry)
    total += u64(12 - month) * u64(entry->second.funding);
  return i64(total);
}

// Capture accrued ordinance expense before reading the live monthly total after its separate date lookup.
// The recurring product wraps to32 bits before being zero-extended into the64-bit accumulator.
i64 EcBudget::GetEstOrdinanceExpenses() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 total = u64(ordinance_expenses.yearTotal);
  u32 recurring = ordinances->GetTotalMonthlyOrdinanceExpense();
  return i64(total + ((12 - month) * recurring));
}

// Consume new penalties into the persistent accumulator before obtaining recurring neighbor expense.
// Preserve the source's captured year total, signed penalty extension and32-bit recurring product.
i64 EcBudget::GetEstNeighborDealExpenses() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u64 total = u64(neighbor_expenses.yearTotal);
  i32 penalty = neighbors->GetNeighborDealPenaltyExpense();
  penalty_expenses = i64(u64(penalty_expenses) + u64(i64(penalty)));
  total += u64(penalty_expenses);
  u32 recurring = u32(neighbors->GetTotalMonthlyNeighborDealExpense());
  return i64(total + ((12 - month) * recurring));
}

// Start with accrued bond costs, then use either remaining year months or the bond's final payment month.
// Capture the original bond count while retaining live record reads and32-bit payment products.
i64 EcBudget::GetEstBondExpenses() {
  u32 month, day, year;
  city->GetDate(&month, &day, &year);
  --month;
  u32 remaining = 12 - month;
  u64 total = u64(yearly_bond_expenses);
  u32 count = bond_count;
  for (u32 i = 0; i < count; ++i) {
    u32 months = bonds[i].maturity_year > year ? remaining : u32(bonds[i].month) - month + 1;
    total += months * bonds[i].monthly_payment;
  }
  return i64(total);
}

// Refresh the three negative expense estimates and every registered department's projected cost before the parent.
void CtBudgetExpenseView::UpdateTotals() {
  ScString text;
  for (u32 i = 0; i < 3; ++i) {
    i64 amount;
    if (!i)
      amount = budget->GetEstOrdinanceExpenses();
    else if (i == 1)
      amount = budget->GetEstNeighborDealExpenses();
    else
      amount = budget->GetEstBondExpenses();
    i64 value = i64(u64(0) - u64(amount));
    language->MakeMoneyString(value, &text);
    WnWindow *window = totals[i]->GetWindow();
    u32 color = value < 0 ? window->MakeFillColor(255, 0, 0) : window->MakeFillColor(0, 0, 0);
    totals[i]->SetTextColor(color);
    totals[i]->SetCaption(text);
    totals[i]->Fit();
    i32 left = i32(column_width + column_left - u32(totals[i]->GetWindow()->GetW()) - 2);
    WnWindow *vertical = totals[i]->GetWindow(), *moving = totals[i]->GetWindow();
    i32 top = vertical->GetT();
    moving->GZWinMoveTo(left, top);
  }
  for (u32 i = 0; i < department_count; ++i)
    if (budget->departments.find(departments[i].identifier) != budget->departments.end()) {
      u64 accrued = u64(budget->departments[departments[i].identifier].yearTotal);
      u64 funding = u64(budget->departments[departments[i].identifier].funding);
      i64 value = i64(u64(0) - (accrued + funding * remaining_months));
      language->MakeMoneyString(value, &text);
      WnWindow *window = departments[i].estimated_total->GetWindow();
      u32 color = value < 0 ? window->MakeFillColor(255, 0, 0) : window->MakeFillColor(0, 0, 0);
      departments[i].estimated_total->SetTextColor(color);
      departments[i].estimated_total->SetCaption(text);
      departments[i].estimated_total->Fit();
      i32 left = i32(column_width + column_left - u32(departments[i].estimated_total->GetWindow()->GetW()) - 2);
      WnWindow *vertical = departments[i].estimated_total->GetWindow();
      WnWindow *moving = departments[i].estimated_total->GetWindow();
      i32 top = vertical->GetT();
      moving->GZWinMoveTo(left, top);
    }
  owner->UpdateTotals();
}

static bool CtBudgetLocationSet;
u8 CtBudgetLastView;
static i32 CtBudgetLastX, CtBudgetLastY;
