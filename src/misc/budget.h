#ifndef SC3_MISC_BUDGET_H
#define SC3_MISC_BUDGET_H

#include <map>

#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/sc3.h"
#include "common/sc3_string.h"
#include "economy/api.h"
#include "misc/module.h"
#include "ui/generic_dialogs.h"
#include "window/controls.h"

class EcBudget;
class ScLanguageUtility;

// Four retained labels describe one original bond row, independently of the budget's 28-byte accounting record.
struct CtBondRow {
  WnLabelControl *labels[4];
};

// Actual cSC3BondsDialogProc receiver owns the retained row, total, issue and spinner controls.
class CtBondsDialog : public UiWindowCallback, public ScMiscRefCount {
public:
  explicit CtBondsDialog(EcBudget *budget);
  ~CtBondsDialog() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual bool CreateBondsDialog(WnWindow **out);

protected:
  bool OnInit(WnWindow *window);
  bool OnShutdown(WnWindow *window);
  bool OnButtonActivated(WnWindow *window, u32 identifier);
  bool OnSpinnerChanged(WnWindow *window, i32 identifier, i32 value);
  // Native effects; the original private updateTotals return declaration remains unresolved.
  void RefreshTotals();
  EcBudget *budget;
  ScLanguageUtility *language;
  CtBondRow *rows;
  WnLabelControl *borrowed_total, *warning;
  WnButton *issue;
  i32 amount;
  WnSpinnerControl *spinner;
  ScRect area;
};

// Original cSC3CmdShowBudgetWin adds only Execute to the real game-command base and owns no extra state.
class CtShowBudgetCommand : public ScGameCommand {
public:
  CtShowBudgetCommand();
  ~CtShowBudgetCommand() override;
  bool Execute() override;
};

class CtCity;
class CtBudgetWindow;
class CtOrdinanceLayer;
class EcBudget;
class EcDepartmentBudget;
class ScLanguageUtility;
class UiBudgetSlider;

// Actual40-byte department row owns seven control holds and one retained department beside its identifier and height.
struct CtBudgetExpenseRow {
  u32 identifier;
  EcDepartmentBudget *department;
  WnLabelControl *name;
  UiBudgetSlider *funding;
  WnWindow *button;
  WnLabelControl *year_total, *estimated_total;
  WnWindow *year_box, *estimated_box;
  i32 height;
};

// Shared native control-access helper retains each original interface getter during the pane's layout phases.
WnWindow *CtExpenseRowWindow(CtBudgetExpenseRow *row, u32 column);

// Actual expense-pane callback and secondary lifetime base with retained department controls and viewport state.
class CtBudgetExpenseView : public UiWindowCallback, public ScMiscRefCount {
public:
  CtBudgetExpenseView(
      EcBudget *budget, CtBudgetWindow *owner, u32 columnLeft, u32 columnWidth, const ScRect &area, u32 rowHeight);
  ~CtBudgetExpenseView() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual bool CreateExpenseView(WnWindow **out);

protected:
  bool OnInit(WnWindow *window);
  bool OnShutdown(WnWindow *window);
  // Full native effects; private nominal returns remain qualified separately from the original interface.
  bool OnButtonActivated(WnWindow *window, u32 identifier);
  bool OnSliderChanged(WnWindow *window, WnWindow *control);
  bool OnBudgetSliderChanged(u32 identifier, i32 value);
  void UpdateTotals();
  void UpdateDepartmentView();
  CtCity *city;
  CtBudgetWindow *owner;
  EcBudget *budget;
  CtOrdinanceLayer *ordinances;
  ScLanguageUtility *language;
  u32 column_left, column_width, row_height;
  ScRect area;
  u32 remaining_months;
  WnLabelControl *totals[3];
  u8 unused76[4];
  u32 department_count, department_top, department_height;
  CtBudgetExpenseRow *departments;
  WnScrollbarControl *scrollbar;
};

class CtCity;
class CtBudgetWindow;
class CtOrdinanceLayer;
class EcBudget;
class ScLanguageUtility;

// Actual income-pane callback owns five summary labels and three tax estimate labels beside its module lifetime.
class CtBudgetIncomeView : public UiWindowCallback, public ScMiscRefCount {
public:
  CtBudgetIncomeView(
      EcBudget *budget, CtBudgetWindow *owner, u32 columnLeft, u32 columnWidth, const ScRect &area, u32 rowHeight);
  ~CtBudgetIncomeView() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual bool CreateIncomeView(WnWindow **out);

protected:
  bool OnInit(WnWindow *window);
  bool OnShutdown(WnWindow *window);
  bool OnButtonActivated(WnWindow *window, u32 identifier);
  // Full native effects; private nominal return declarations remain unresolved.
  bool OnSpinnerChanged(WnWindow *window, u32 identifier, i32 value);
  void UpdateTotals();
  CtCity *city;
  CtBudgetWindow *owner;
  EcBudget *budget;
  CtOrdinanceLayer *ordinances;
  ScLanguageUtility *language;
  u32 column_left, column_width, row_height;
  ScRect area;
  u32 remaining_months;
  WnLabelControl *totals[5];
  WnLabelControl **tax_totals;
};

class CtCity;
class EcBudgetLayer;
class ScLanguageUtility;

// Actual cSC3BudgetWinProc callback and secondary lifetime base. The expense pane retains its missing slider boundary.
// Native virtual refresh preserves the original slot; Recalculate's nominal return remains unresolved.
class CtBudgetWindow : public UiWindowCallback, public ScMiscRefCount {
  friend class CtBudgetIncomeView;
  friend class CtBudgetExpenseView;

public:
  CtBudgetWindow();
  ~CtBudgetWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual bool CreateBudgetWindow(WnWindow **out);
  // Native effects phases: the original static CreateLabel/CreateWrappedLabel/CreateDigitBox returns remain unresolved.
  static void ConfigureLabel(WnLabelControl **out, const ScString &text, u32 color, FtFont *font);
  static void ConfigureWrappedLabel(
      WnLabelControl **out, const ScString &text, i32 width, u32 color, FtFont *font);
  static void ConfigureDigitBox(WnWindow **out, i32 identifier, const ScRect &area, bool inset);
  static void ConfigureButton(WnButton **out, u32 instance, u32 group, u32 identifier);
  static void ConfigureResizableButton(WnButton **out, u32 instance, u32 identifier, bool radio);

protected:
  // Native effects of1E20C; the original virtual Recalculate return remains unresolved.
  virtual void UpdateTotals();
  bool OnInit(WnWindow *window);
  bool OnShutdown(WnWindow *window);
  bool OnButtonActivated(WnWindow *window, u32 identifier);
  CtCity *city;
  EcBudgetLayer *budget;
  ScLanguageUtility *language;
  WnLabelControl *totals[8];
  i64 year_balance, estimated_balance, estimated_funds;
  i32 column_left, column_width;
  WnButton *income_button, *expense_button;
  WnWindow *income_window;
  UiWindowCallback *income_view;
  WnWindow *expense_window;
  UiWindowCallback *expense_view;
};

// Original budget tTaxType selects the three development tax profiles.
enum class EcTaxType : i32 { Residential, Commercial, Industrial };

class CtOrdinanceLayer;
class NwTrafficLayer;
class CtLandValueLayer;
class CtNeighborsLayer;
class CtResidentialLayer;
class CtAgentTree;
class EcValveLayer;
class EcValve;
class AdBudget;
class ScGraphInfo;

// Original28-byte bond stores its issue month, maturity and separate accrued/remaining payment counters.
struct EcBond {
  u8 month;
  u32 maturity_year, principal, total_payments, remaining_payments, paid, monthly_payment;
};

// Original tLineItem is the128-byte mapped department record; the department pointer is borrowed.
struct EcLineItem {
  u32 id;
  EcDepartmentBudget *department;
  u8 percentage;
  i64 funding, monthly[12], yearTotal;
};

// Each original136-byte tax profile owns a String and a retained valve beside its type, rates and monthly receipts.
struct EcTaxProfile {
  ScString name;
  u32 type;
  EcValve *valve;
  u8 rate, maximumRate;
  i64 monthly[12], yearTotal;
};

// The paired monthly ledger and its year-to-date accumulator recur in the original receiver.
struct EcBudgetHistory {
  i64 monthly[12], yearTotal;
};

// Actual budget primary, city-layer secondary and lifetime base with the recovered city setup and monthly game state.
class EcBudget : public EcBudgetLayer, public CtLayer, public ScMiscRefCount {
  friend class CtBondsDialog;
  friend class CtBudgetIncomeView;
  friend class CtBudgetExpenseView;

public:
  EcBudget();
  ~EcBudget() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool StaticInit(CtDefinition *definition) override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool StaticShutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool EndOfMonth();
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetTotalFunds(i64 value) override;
  i64 GetTotalFunds() override;
  bool WithdrawFunds(u32 amount) override;
  void DepositFunds(u32 amount) override;
  void DepositDisasterReliefFunds(u32 amount) override;
  i64 GetYTDIncome() override;
  i64 GetYTDExpenses() override;
  i64 GetEstExpenses() override;
  i64 GetEstIncome() override;
  bool AddDepartmentBudget(EcDepartmentBudget *department) override;
  void RemoveDepartmentBudget(EcDepartmentBudget *department) override;
  bool GetDepartmentBudget(u32 id, EcDepartmentBudget **out) override;
  void NeededFundingChanged(EcDepartmentBudget *department) override;
  void FundingPercentageChanged(EcDepartmentBudget *department) override;
  u32 GetFunding(EcDepartmentBudget *department) override;
  i64 GetFunding(u32 id) override;
  u8 GetTaxRate(i32 type) override;
  f32 GetValveModifier(u8 rate) override;
  i32 GetCost(i32 development) override;
  i32 GetCost(u32 type) override;
  bool IssueBond(u32 amount) override;
  i64 GetTotalBorrowed() override;
  i64 GetCurrentBorrowingLimit() override;
  void SetMarxism(bool value) override;
  bool MarxismIsOn() override;

protected:
  i64 ComputeTax(EcTaxType type);
  i64 GetEstDepartmentExpenses();
  i64 GetEstOrdinanceExpenses();
  i64 GetEstNeighborDealExpenses();
  i64 GetEstBondExpenses();
  i64 GetEstTaxIncome();
  i64 GetEstFareIncome();
  i64 GetEstOrdinanceIncome();
  i64 GetEstNeighborDealIncome();
  i64 GetEstBusinessDealIncome();
  i64 GetEstDisasterReliefIncome();
  bool initialized, marxism, limit_deficit, auto_budget;
  CtCity *city;
  CtOrdinanceLayer *ordinances;
  NwTrafficLayer *traffic;
  CtLandValueLayer *land_value;
  CtNeighborsLayer *neighbors;
  CtResidentialLayer *residential;
  CtAgentTree *agents;
  EcValveLayer *valves;
  i64 funds, funds_copy;
  ScGraphInfo *chart;
  ScString label;
  std::map<u32, EcLineItem> departments;
  EcTaxProfile taxes[3];
  f32 valve_modifiers[23];
  EcBudgetHistory ordinance_expenses, ordinance_income, disaster_relief, fare_income, neighbor_expenses,
      neighbor_income;
  i64 penalty_expenses;
  AdBudget *advisor;
  EcBudgetHistory business_income;
  EcBond *bonds;
  u32 bond_count, bond_expenses[12];
  i64 yearly_bond_expenses, borrowed;
};

static_assert(sizeof(EcBudgetHistory) == 104);
static_assert(sizeof(EcBond) == 28);

class EcBudget;

// Original cSC3MonthlyBudgetManipulator holds the budget separately from its module lifetime and primary scheduler
// view.
class EcBudgetManipulator : public CtLayerManipulator, public ScMiscRefCount {
public:
  EcBudgetManipulator();
  ~EcBudgetManipulator() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(EcBudget *owner);
  bool Shutdown();
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  EcBudget *budget;
};

// Original108-byte tax-demand entry stores two slopes, the neutral rate and population beside23 computed modifiers.
struct EcTaxModifier {
  f32 bonus, penalty;
  u8 neutral;
  u32 population;
  f32 modifiers[23];
};

extern u32 EcBudgetMaximumBonds, EcBudgetBondValue, EcBudgetBondRate, EcBudgetBondYears;
extern f32 EcBudgetResidentialTaxScale, EcBudgetCommercialTaxScale, EcBudgetIndustrialTaxScale;
extern u8 EcBudgetMinimumLandValue;
extern std::map<i32, i32> EcBudgetSpecialCosts;
extern std::map<u32, i32> EcBudgetPlacementCosts;
extern std::map<u32, u32> EcBudgetDiscounts;
extern std::map<u32, EcTaxModifier> EcBudgetTaxModifiers;

static_assert(sizeof(EcTaxModifier) == 108);

#endif
