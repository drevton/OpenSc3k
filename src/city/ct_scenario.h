#ifndef SC3_CITY_CT_SCENARIO_H
#define SC3_CITY_CT_SCENARIO_H

#include <map>
#include <vector>

#include "application/newscast_ui.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/sc_random.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/thread.h"
#include "occupants/animation_manager.h"
#include "resources/factory.h"
#include "resources/resource.h"
#include "ui/generic_dialogs.h"

// Progress, goal/rank and final-choice controls identified by their construction and installed captions.
enum CtScenarioControlId : u32 {
  kCtScenarioProgressWindowId = 0x443d97fc,
  kCtScenarioProgressPortraitTitleId = 0x443d97fd,
  kCtScenarioGoalsHeadingId = 0x443d97ff,
  kCtScenarioProgressPortraitId = 0x443d9800,
  kCtScenarioProgressPortraitPanelId = 0x443d9801,
  kCtScenarioProgressFooterPanelId = 0x443d9802,
  kCtScenarioProgressLeftBorderId = 0x443d9803,
  kCtScenarioGoalsListId = 0x443d9804,
  kCtScenarioProgressTextBackgroundId = 0x443d9805,
  kCtScenarioProgressCloseId = 0x443d9806,
  kCtScenarioProgressTextEditorId = 0x443d9807,
  kCtScenarioStatusHeadingId = 0x443d9808,
  kCtScenarioMayorLabelId = 0x443d9809,
  kCtScenarioMayorEditId = 0x443d980a,
  kCtScenarioChoiceWindowId = 0xc44ff6ce,
  kCtScenarioChoicePortraitTitleId = 0xc44ff6cf,
  kCtScenarioChoicePortraitId = 0xc44ff6d1,
  kCtScenarioChoicePortraitPanelId = 0xc44ff6d2,
  kCtScenarioChoiceLeftBorderId = 0xc44ff6d3,
  kCtScenarioChoiceTopDividerId = 0xc44ff6d4,
  kCtScenarioChoiceFirstDividerId = 0xc44ff6d5,
  kCtScenarioChoiceSecondDividerId = 0xc44ff6d6,
  kCtScenarioChoiceRightBorderId = 0xc44ff6d7,
  kCtScenarioChoiceBottomBorderId = 0xc44ff6d8,
  kCtScenarioRanksBackgroundId = 0xc44ff6d9,
  kCtScenarioChoiceOptionBackgroundId = 0xc44ff6da,
  kCtScenarioResultsTextEditorId = 0xc44ff6db,
  kCtScenarioCompletedHeadingId = 0xc44ff6dc,
  kCtScenarioRanksListId = 0xc44ff6dd,
  kCtScenarioReplayId = 0xc44ff6de,
  kCtScenarioPlayAnotherId = 0xc44ff6df,
  kCtScenarioMainMenuId = 0xc44ff6e0,
  kCtScenarioMayorResultsHeadingId = 0xc44ff6e4,
};

class ScStringView;

// Actual output topology of SIMMISC10019242, reached through neighboring-layer slot140.
// String views are borrowed; scalar meanings/private nominal field names remain unresolved and are not guessed.
struct CtDealRecord {
  ScStringView *first_name, *second_name; // Original +0/+4.
  u32 value_08, value_0c;
  u8 value_10, type;
  u32 value_14, value_18, value_1c, value_20, year;
};

class CtAuraLayer;
class CtCrimeLayer;
class CtLandValueLayer;
class CtOrdinanceLayer;
class CtPlumbingLayer;
class CtPowerLayer;
class CtResidentialLayer;
class EcBudgetLayer;
class EcLayer;
class GeBuildingLayer;
class NwTrafficLayer;
class ScOccupantManager;
class CtScenarioText;
class CtScenarioLayerImpl;
class CtAgentTree;
class WnIconListControl;
class WnEditorControl;

// The original substitution token owns its spelling and three type-specific payload words.
class CtScenarioToken : public ScObject {
public:
  CtScenarioToken();
  CtScenarioToken(i32 kind, const ScStringView &name, i32 first, i32 second);
  CtScenarioToken(i32 kind, const ScStringView &name, i32 value);
  ~CtScenarioToken() override;
  CtScenarioToken *Assign(const CtScenarioToken &source); // Original private nominal return is unidentified.
  i32 kind;
  ScString name;
  i32 first, second, value;
};

using CtScenarioTokenCallback = u8 (*)(
    ScString *text, u32 offset, CtScenarioToken *replacement, CtScenarioLayerImpl *layer);
u8 CtScenarioExpandToken(ScString *text, u32 offset, CtScenarioToken *replacement, CtScenarioLayerImpl *layer);
// These original token-replacement helpers are not yet recovered; they may advance the supplied offset.
u8 CtScenarioApplyBuiltinToken(ScString *text, u32 *offset);
u8 CtScenarioApplyToken(ScString *text, CtScenarioToken *token, u32 *offset);
u8 CtScenarioApplyBuiltin(ScString *text, const ScStringView &marker, u32 *offset);
u8 CtGetScenarioPetitionInfo(u32 id, RsKey *portrait, u32 *mode, ScStringView *title, ScStringView *description);
u8 CtGetScenarioAdvisorInfo(u32 id, i32 mood, u8 alternate, RsKey *portrait, u32 *mode,
    ScStringView *personalName, ScStringView *name);
u8 CtScenarioOpenCity(const ScStringView *path); // Observed AL return; nominal private type remains unidentified.

// The original occupant filter compares a requested type against the city's agent-type tree.
class CtScenarioTypeTest final : public OcIteratorTest {
public:
  explicit CtScenarioTypeTest(u32 type);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;
  ~CtScenarioTypeTest();

private:
  u32 references, type;
  CtAgentTree *tree;
};

// Each progress entry stores a goal string followed by its achieved flag.
struct CtScenarioProgressEntry {
  ScString text;
  u8 achieved;
};

// The original progress controller has separate window-callback and message-target identities.
// Window initialization and close operations remain unresolved until their dependencies are recovered.
class CtScenarioProgress final : public UiWindowCallback, public ScMessageTarget {
public:
  CtScenarioProgress();
  ~CtScenarioProgress();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  // Private helper declarations record observed AL returns; their nominal source types remain unidentified.
  u8 Bind(CtScenarioLayerImpl *layer);
  u8 Unbind();
  u8 OnButton(WnWindow *window, u32 command);
  u8 OnShutdown(WnWindow *window);
  u8 OnInit(WnWindow *window);
  u8 PopulateGoals(WnIconListControl *list);
  u8 SetDialogText(WnEditorControl *editor);
  u8 CloseWindow(WnWindow *window);
  u8 CreateProgressWindow(WnWindow **out, bool persist);

private:
  u32 references;
  u8 windowOpen;
  CtScenarioLayerImpl *owner;
  u8 bound;
  std::vector<CtScenarioProgressEntry> entries;
  u8 persistText;
};

// The three-choice dialog owns its displayed strings and retains the scenario while bound.
// Its initialization path remains unresolved until its control dependencies are recovered.
class CtScenarioChoiceDialog final : public UiWindowCallback {
public:
  CtScenarioChoiceDialog();
  ~CtScenarioChoiceDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  // Private helper declarations record observed AL returns; their nominal source types remain unidentified.
  u8 Bind(CtScenarioLayerImpl *layer);
  u8 Unbind();
  u8 OnShutdown(WnWindow *window);
  u8 OnInit(WnWindow *window);
  u8 OnButton(WnWindow *window, u32 command);
  u8 PopulateRanks(WnIconListControl *list);
  u8 CloseWindow(WnWindow *window, i32 result);
  u8 CreateChoiceWindow(WnWindow **out);

private:
  u32 references;
  u8 windowOpen;
  CtScenarioLayerImpl *owner;
  u8 bound;
  std::vector<CtScenarioProgressEntry> entries;
  u8 persistText;
};

// The confirmation dialog retains its owner and owns the text displayed by the scenario popup.
class CtScenarioConfirmDialog final : public UiWindowCallback {
public:
  CtScenarioConfirmDialog();
  ~CtScenarioConfirmDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  // These private helper return types describe observed AL values, not established nominal declarations.
  u8 Bind(CtScenarioLayerImpl *layer);
  u8 Unbind();
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnButton(WnWindow *window, u32 command);
  u8 CloseWindow(WnWindow *window, i32 result);
  u8 CreateConfirmWindow(WnWindow **out, const ScStringView &message, u8 allow);

private:
  friend class CtScenarioLayerImpl;
  u32 references;
  u8 windowOpen;
  CtScenarioLayerImpl *owner;
  u8 bound, allowConfirm;
  ScString text;
  i32 fourth, sixth, fifth;
};

// The scenario's two manipulators borrow the layer and carry the original tick mode.
class CtScenarioManipulator final : public CtLayerManipulator {
public:
  CtScenarioManipulator(CtScenarioLayerImpl *owner, i32 mode);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

private:
  u32 references;
  CtScenarioLayerImpl *owner;
  i32 mode;
};

// The registered layer factory constructs the real scenario identity; unrecovered callees remain explicit.
ScUnknown *CtCreateScenarioLayer();
ScUnknown *CtCreateScenarioResourceFactory();
ScDirector *CtGetScenarioDirector();
ScFramework *CtGetScenarioFramework();
RsManager *CtGetScenarioResourceManager();

class CtScenarioDirector : public ScDllDirector {
public:
  CtScenarioDirector();
  ~CtScenarioDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

// The script node's first word and final byte are not yet identified. Each argument holds a variable flag and value.
struct CtScenarioArgument {
  u8 variable, unknown1, unknown2, unknown3;
  i32 value;
};

struct CtScenarioCommand {
  u32 unknown0;
  i16 opcode;
  u8 argumentCount, unknown7;
  CtScenarioArgument *arguments;
};

// The popup message owns six original argument values; their individual meanings remain unidentified.
struct CtScenarioPopupMessage {
  i32 first, second;
  u8 third;
  i32 fourth, sixth, fifth;
};

static_assert(sizeof(CtScenarioArgument) == 8);

// The registered resource factory creates this script object and owns its command and argument arrays.
class CtScenarioScript : public RsResource {
public:
  CtScenarioScript();
  ~CtScenarioScript() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Clear();
  u8 Read(RsStream *stream); // AL observed; original Boolean or byte declaration remains unidentified.

private:
  friend class CtScenarioLayerImpl;
  i16 commandCount;
  CtScenarioCommand *commands;
};

// Original 03DFAE27 factory; the script reader retains explicit boundaries for incomplete parsing evidence.
class CtScenarioResourceFactory : public RsFactory {
public:
  CtScenarioResourceFactory();
  ~CtScenarioResourceFactory() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Load(RsRecord *record, u32 iid, void **out) override;
  bool New(u32 type, u32 iid, void **out) override;
  bool ReadRecord(RsResource *resource, RsRecord *record) override;
  bool WriteRecord(const RsResource *resource, RsRecord *record) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

// One element of the original scenario script cursor vector, initially inserted with index minus one.
struct CtScenarioCursor {
  u32 index;
  CtScenarioScript *script;
};

// The installed goal map stores this 16-byte value and owns its optional deleting-interface object.
class CtScenarioGoal {
public:
  CtScenarioGoal();
  ~CtScenarioGoal();
  CtScenarioGoal(const CtScenarioGoal &) = delete;
  CtScenarioGoal &operator=(const CtScenarioGoal &) = delete;
  i32 Initialize(i32 first, i32 second); // EAX projection; original private nominal return is unidentified.
  void EnsureText();                     // Private nominal return is unidentified; callers use the published object.
  i32 IsTextValid();
  u8 GetText(ScStringView *out); // Nominal private return remains unidentified.
  CtScenarioText *object;
  u8 achieved;
  i32 first, second;
};

struct CtScenarioRank {
  i32 first, second;
};

// The scenario dialog map stores an image ID and two text-resource instances for each key.
struct CtScenarioDialogEntry {
  u32 image, title, description;
};

// Original resource-backed text object owns one String identity and retains its separate resource key.
class CtScenarioText : public ScObject {
public:
  CtScenarioText(u32 instance, u32 group);
  ~CtScenarioText() override;
  bool IsValid();
  ScString GetText();
  ScStringView *GetString();
  RsKey *GetResourceKey(RsKey *out); // Observed pointer return; nominal private type is unidentified.
  void Resolve(); // Original private nominal return is unidentified; the constructor uses only its effects.
  u8 Format(ScStringView *out, CtScenarioTokenCallback callback, CtScenarioLayerImpl *layer);
  void Substitute(ScString *text, CtScenarioTokenCallback callback, CtScenarioLayerImpl *layer);
  ScStringView *text;
  RsKey key;
};

// Original layer receiver; several nonvirtual dialog and saved-state dependencies remain unresolved.
// Handler return and execution-flag source types are unidentified: i32 records the observed full-word ABI.
class CtScenarioLayerImpl final : public CtScenarioLayer, public CtLayer {
public:
  CtScenarioLayerImpl();
  ~CtScenarioLayerImpl();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateProgressDlgWin(WnWindow **out, bool persist) override;
  bool IsScenarioRunning() override;
  bool SaveUncompressedScenarioRelatedRecords(RsDatabaseSegment *segment) override;
  void GetScenarioFilePath(ScStringView *out) override;
  void GetProgressCaption(ScStringView *out);
  void GetDialogText(ScStringView *out); // Private nominal return remains unidentified.
  void GetResultsDialogText(ScStringView *out); // Private nominal return remains unidentified.
  u8 ResolveToken(ScString *text, u32 offset, CtScenarioToken *replacement);
  void SetProgressCaption(i32 group, i32 instance);       // Nominal private return remains unidentified.
  void SetScenarioAdvisor(i32 id, i32 mode);              // Nominal private return remains unidentified.
  i32 GetScenarioAdvisor(i32 *id, i32 *group, i32 *mode); // EAX projection; nominal return is unidentified.
  u8 ReadScenarioHeader(RsRecord *record);          // Observed AL return; nominal private type remains unidentified.
  u8 ReadDialogEntries(RsDatabaseSegment *segment); // Observed AL return; nominal private type is unidentified.
  u8 ReadTickerEntries(RsDatabaseSegment *segment); // Observed AL return; nominal private type is unidentified.
  u8 ReadSavedState(RsStream *stream);              // Observed AL return; nominal private type remains unidentified.
  u8 GetDialogEntry(u32 id, RsKey *image, u32 *mode, ScString *title, ScString *description);
  void GetGoalList(std::vector<CtScenarioProgressEntry> *out); // Private nominal return is unidentified.
  void GetRankList(std::vector<ScString> *out, i32 *selected); // Private nominal return is unidentified.
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  i32 EvaluateArgument(CtScenarioCommand *command, u32 index);
  u8 ResolveResultVariable(CtScenarioCommand *command, u32 *out);
  u8 ResolveVariable(CtScenarioCommand *command, u32 index, u32 *out);
  i32 Add(CtScenarioCommand *command, i32 execute);
  i32 Subtract(CtScenarioCommand *command, i32 execute);
  i32 Multiply(CtScenarioCommand *command, i32 execute);
  i32 Divide(CtScenarioCommand *command, i32 execute);
  i32 ComputePercentage(CtScenarioCommand *command, i32 execute);
  i32 Increment(CtScenarioCommand *command, i32 execute);
  i32 Decrement(CtScenarioCommand *command, i32 execute);
  i32 SetVariable(CtScenarioCommand *command, i32 execute);
  i32 Equal(CtScenarioCommand *command, i32 execute);
  i32 NotEqual(CtScenarioCommand *command, i32 execute);
  i32 GreaterThan(CtScenarioCommand *command, i32 execute);
  i32 LessThan(CtScenarioCommand *command, i32 execute);
  i32 ElapsedMonths();
  i32 GetMonthsElapsed(CtScenarioCommand *command, i32 execute);
  i32 GetCurrentDate(CtScenarioCommand *command, i32 execute);
  i32 GetAura(CtScenarioCommand *command, i32 execute);
  i32 GetCrimeValue(CtScenarioCommand *command, i32 execute);
  i32 GetPopulation(CtScenarioCommand *command, i32 execute);
  i32 GetFundsAvailable(CtScenarioCommand *command, i32 execute);
  i32 GetTotalDebt(CtScenarioCommand *command, i32 execute);
  i32 GetRciTaxes(CtScenarioCommand *command, i32 execute);
  i32 CountUnpoweredBuildings(CtScenarioCommand *command, i32 execute);
  i32 GetSurplusPower(CtScenarioCommand *command, i32 execute);
  i32 CountUnwateredBuildings(CtScenarioCommand *command, i32 execute);
  i32 GetSurplusWater(CtScenarioCommand *command, i32 execute);
  i32 GetLandValue(CtScenarioCommand *command, i32 execute);
  i32 GetPollutionValue(CtScenarioCommand *command, i32 execute);
  i32 GetTrafficDensity(CtScenarioCommand *command, i32 execute);
  i32 IsOrdinance(CtScenarioCommand *command, i32 execute);
  i32 IsNeighborConnection(CtScenarioCommand *command, i32 execute);
  i32 IsNeighborDealPresent(CtScenarioCommand *command, i32 execute);
  i32 IsBusinessDealActive(CtScenarioCommand *command, i32 execute);
  i32 OfferBusinessDeal(CtScenarioCommand *command, i32 execute);
  i32 OfferRewardBuilding(CtScenarioCommand *command, i32 execute);
  i32 SetDisastersEnabled(CtScenarioCommand *command, i32 execute);
  i32 DisableNewNeighborDeals(CtScenarioCommand *command, i32 execute);
  i32 EnableNewNeighborDeals(CtScenarioCommand *command, i32 execute);
  i32 Subroutine(CtScenarioCommand *command, i32 execute);
  i32 RepeatRegular(CtScenarioCommand *command, i32 execute);
  i32 GenerateRandomNum(CtScenarioCommand *command, i32 execute);
  i32 StartEarthquake(CtScenarioCommand *command, i32 execute);
  i32 StartFire(CtScenarioCommand *command, i32 execute);
  i32 StartUfoAttack(CtScenarioCommand *command, i32 execute);
  i32 AddGoal(CtScenarioCommand *command, i32 execute);
  i32 AllGoalsMet(CtScenarioCommand *command, i32 execute);
  i32 GoalStatusIs(CtScenarioCommand *command, i32 execute);
  i32 MarkGoalStatus(CtScenarioCommand *command, i32 execute);
  i32 AddRankInfo(CtScenarioCommand *command, i32 execute);
  i32 EnableDisableBusinessDeal(CtScenarioCommand *command, i32 execute);
  i32 MakeDialogText(CtScenarioCommand *command, i32 execute);
  i32 SetResultsDialogText(CtScenarioCommand *command, i32 execute);
  i32 SetTextGroup(i32 group);
  CtScenarioText *SetDialogText(i32 group, i32 instance);
  CtScenarioText *SetResultsText(i32 group, i32 instance);
  i32 Assign(CtScenarioCommand *command, i32 execute);
  i32 If(CtScenarioCommand *command, i32 execute);
  i32 IfElse(CtScenarioCommand *command, i32 execute);
  i32 Dispatch(CtScenarioCommand *command, i32 execute);

  // Registered original handlers whose bodies are not yet recovered.
  i32 AndCondition(CtScenarioCommand *command, i32 execute);
  i32 BeginBlock(CtScenarioCommand *command, i32 execute);
  i32 CountStructures(CtScenarioCommand *command, i32 execute);
  i32 CountStructuresNearLocation(CtScenarioCommand *command, i32 execute);
  i32 CountStructuresWithAppearanceInCity(CtScenarioCommand *command, i32 execute);
  i32 CountStructuresWithAppearanceNearLocation(CtScenarioCommand *command, i32 execute);
  // Private helper argument and return declarations retain the observed pointer and full-word ABI only.
  i32 CountAppearance(u32 destination, const RsKey &key, u32 x, u32 z, u32 radius);
  i32 EndScenario(CtScenarioCommand *command, i32 execute);
  i32 MoveCamera(CtScenarioCommand *command, i32 execute);
  i32 OrCondition(CtScenarioCommand *command, i32 execute);
  i32 PlaySound(CtScenarioCommand *command, i32 execute);
  i32 PopUpMsg(CtScenarioCommand *command, i32 execute);
  i32 SetScenarioSpeed(CtScenarioCommand *command, i32 execute);
  i32 ShowNewsticker(CtScenarioCommand *command, i32 execute);
  i32 StartLocusts(CtScenarioCommand *command, i32 execute);
  i32 StartRiot(CtScenarioCommand *command, i32 execute);
  i32 StartSpaceJunk(CtScenarioCommand *command, i32 execute);
  i32 StartTornado(CtScenarioCommand *command, i32 execute);
  i32 StartToxicCloud(CtScenarioCommand *command, i32 execute);
  i32 StartWhirlpool(CtScenarioCommand *command, i32 execute);

  // Nominal private return declarations remain unresolved for script execution and cursor operations.
  CtScenarioCommand *NextCommand();
  void PushCursor(const CtScenarioCursor &cursor);
  void PopCursor();
  i32 RunScript(CtScenarioScript *script);
  u8 AddPeriodicScript(u32 period, const RsKey &key);
  void HandleSimulationMessage(ScMessage *message);
  void HandleDialog(bool endScenario);
  void HandlePopup(ScMessage *message);
  void OnSimulationBoundary();
  void OnFinishDialog();
  void TickScenario(i32 mode);
  void SetRunning(u8 value);
  void DisableLoadSave();
  void EnableLoadSave();
  u8 LoadScript(const RsKey &key, i32 initialIndex);
  void RunScheduledScripts(const ScArray<CtScenarioScript *> &scripts);
  void StopScenario();
  u8 StartScenario(u8 restored); // Observed AL return; nominal private type remains unidentified.
  void FlushNotifications();

private:
  // Prefix fields are identified from the installed 32-bit receiver; the remaining owning members are not yet declared.
  u8 initialized, running, unknown10, unknown11;
  u32 references;
  u8 scenarioFlag16, dispatching, unknown18, unknown19;
  CtCity *city;
  u32 width, height;
  CtCrimeLayer *crime;
  GeBuildingLayer *buildings;
  CtLandValueLayer *landValue;
  CtPlumbingLayer *water;
  EcLayer *pollution;
  CtPowerLayer *power;
  CtResidentialLayer *residential;
  EcBudgetLayer *budget;
  NwTrafficLayer *traffic;
  CtAuraLayer *aura;
  ScOccupantManager *surfaceOccupants;
  CtOrdinanceLayer *ordinances;
  CtLayerManipulator *dailyManipulator, *otherManipulator;
  u32 elapsedTicks;
  u32 startMonth, startYear, currentDay, currentMonth, currentYear;
  i32 variables[512];
  // The four original script-pointer vectors precede the active script cursor stack.
  ScArray<CtScenarioScript *> scheduledScripts[4];
  ScArray<CtScenarioCursor> cursors;
  CtScenarioCommand *nextCommand;
  i32 scenarioAdvisor, textGroup, scenarioAdvisorMode, scenarioTextGroup;
  // Installed SCENARIO uses ordered standard-library trees for the unsigned-key goal and rank maps.
  std::map<u32, CtScenarioGoal> goals;
  std::map<u32, CtScenarioRank> ranks;
  i32 selectedRank;
  CtScenarioText *resultsText, *dialogText;
  CtScenarioProgress *progressDialog;
  CtScenarioConfirmDialog *confirmDialog;
  CtScenarioChoiceDialog *choiceDialog;
  WnWindow *finishWindow;
  CtScenarioText *progressCaption;
  ScRandom random;
  ScArray<ScMessage> pendingMessages;
  ScString scenarioFilePath;
  std::map<u32, ApTickerInfo> tickerEntries;
  RsKey initialScriptKey;
  RsDatabaseSegment *scenarioSegment;
  std::map<u32, CtScenarioDialogEntry> dialogEntries;
  ScArray<u32> pendingNotifications;
  u8 scriptPaused, popupFlag;
  ScLock goalLock;
  u8 finishNotificationRegistered, scenarioSpeedEnabled;
};

// The original dynamically populated command table stores member-function pointers and their receiver adjustments.
// INI-backed registration populates this table; member-function signatures retain their unresolved private ABI limits.
struct CtScenarioHandler {
  i32 (CtScenarioLayerImpl::*method)(CtScenarioCommand *command, i32 execute);
};

extern CtScenarioHandler *CtScenarioHandlers;
extern i32 CtScenarioLastCommand;
extern RsManager *CtScenarioResourceManager;

#endif
