#ifndef SC3_UI_CITY_SCHEME_H
#define SC3_UI_CITY_SCHEME_H

#include "common/sc3_array.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"

// City-setup controls and the original contiguous option-button ranges, identified by their captions and setters.
enum UiCitySetupControlId : u32 {
  kUiCitySetupLeftBorderPanelId = 0x2552483a,
  kUiCitySetupCenterBorderPanelId = 0x2552483b,
  kUiCitySetupAcceptButtonPanelId = 0x2552483c,
  kUiCitySetupAcceptId = 0x2552483d,
  kUiCitySetupCityNameLabelId = 0x2552483e,
  kUiCitySetupCityNameEditId = 0x2552483f,
  kUiCitySetupMayorNameLabelId = 0x25524840,
  kUiCitySetupMayorNameEditId = 0x25524841,
  kUiCitySetupDescriptionLabelId = 0x25524842,
  kUiCitySetupDescriptionEditId = 0x25524843,
  kUiCitySetupDifficultyOptionsId = 0x25524844,
  kUiCitySetupEasyId = 0x25524845,
  kUiCitySetupMediumId = 0x25524846,
  kUiCitySetupHardId = 0x25524847,
  kUiCitySetupStartDateOptionsId = 0x25524849,
  kUiCitySetupStartYear1900Id = 0x2552484a,
  kUiCitySetupStartYear1950Id = 0x2552484b,
  kUiCitySetupStartYear2000Id = 0x2552484c,
  kUiCitySetupCitySizeOptionsId = 0x2552484e,
  kUiCitySetupCitySize64Id = 0x2552484f,
  kUiCitySetupCitySize128Id = 0x25524850,
  kUiCitySetupCitySize192Id = 0x25524851,
  kUiCitySetupCitySize256Id = 0x25524852,
  kUiCitySetupDisastersId = 0x25524854,
  kUiCitySetupAutoBudgetId = 0x25524855,
  kUiCitySetupLandscapeLabelId = 0x25524856,
  kUiCitySetupTreesLabelId = 0x25524857,
  kUiCitySetupBuildingsLabelId = 0x25524858,
  kUiCitySetupUserBuildingsLabelId = 0x25524859,
  kUiCitySetupSelectUserBuildingsId = 0x2552485a,
  kUiCitySetupLandscapeChoiceLowerBoundId = 0xe558c350,
  kUiCitySetupFirstLandscapeChoiceId = 0xe558c351,
};

class UiSelectCitySchemeCommand;
class CtSchemeManager;
class SpImageCache;
class TrGeneratorInterface;
class WnOptionControl;

u8 UiOffsetOptionButtons(WnOptionControl *group, i32 offset, ScArray<u32> *identifiers);

// Actual cSC3WinProcCityScheme has a generic window and separate callback228, confirmed by the Linux220 thunk family.
// The unpublished candidate's initialization and pane bodies still require installed-binary comparison.
class UiCitySchemeWindow : public UiGenericWindow, public UiWindowCallback {
  friend class UiSelectCitySchemeCommand;

public:
  explicit UiCitySchemeWindow(u32 mode);
  ~UiCitySchemeWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GZPaint() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;

protected:
  u32 callback_references, mode;
  u8 options[4];
  u32 landscape_last, flora_first, flora_last, buildings_first, buildings_last;
  u32 *landscape_ids, *flora_ids, *building_ids;
  u32 landscape, flora, buildings;
  ScString city_name, mayor_name, city_description;
  i64 funds;
  u8 funds_debt;
  i32 difficulty;
  u32 start_year, city_size;
  TrGeneratorInterface *dirt_generator;
  u8 auto_budget, disasters;
  GxBuffer *landscape_image, *building_front, *building_back, *flora_front, *flora_back;
  SpImageCache *images;
  CtSchemeManager *schemes;
  u8 saved_auto_budget, saved_disasters;
  u32 saved_landscape, saved_flora, saved_buildings;
  u32 word428;
  u8 OnInit();
  u8 OnBtnClicked(u32 id);
  u8 OnShutdown();
  u8 ReadValuesFromWindow();
  u8 DrawLeftPane();
  u8 DrawRightPane();
  void SetLandScapeSelectionEffects(u32 id);
  void SetFloraSelectionEffects(u32 id);
  void SetBuildingSelectionEffects(u32 id);
  u32 GetButtonIdForLandScape(u32 id) const;
  u32 GetButtonIdForFloraSet(u32 id) const;
  u32 GetButtonIdForBuildingSet(u32 id) const;
  void LandScapeBtnClickedEffects(u32 id);
  void FloraBtnClickedEffects(u32 id);
  void BuildingBtnClickedEffects(u32 id);
  void CauseRedrawEffects();
  void EndDialogEffects(u32 result);
  void ToggleButtonEffects(u32 id, bool enabled);
  u8 OnLineInputChanged(u32 id);
  static void AddTextLabelEffects(
      WnWindow *parent_window, u32 id, u32 x, u32 y, const ScStringView &text, FtFont *font, bool enabled);
  static u8 AddOutlineBox(WnWindow *parent_window, u32 x, u32 y, u32 width, u32 height);
  static u8 AddOutlinedTextEdit(
      WnWindow *parent_window, u32 id, u32 x, u32 y, u32 width, u32 height, const ScStringView &text);
  u8 AddCheckBox(WnWindow *parent_window, u32 id, u32 x, u32 y, bool enabled, const ScStringView &text);
  void AddOptionButtons(ScArray<RsKey> *keys, u32 first, i32 bottom, i32 stride, u32 selected);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiCitySchemeWindow) == 432);

class UiCitySchemeWindow;
class ScString;
class TrGeneratorInterface;

// Reached656202D7 result view. DWORD IDs, signed difficulty and64-bit funds preserve the observed carriers.
// Private nominal enum/money/string-pointee declarations and bool/BYTE spellings remain qualified, not asserted SDK parity.
class UiCitySchemeSelection : public ScUnknown {
public:
  virtual u32 GetSelectedLandScape() const = 0;
  virtual u32 GetSelectedFloraSet() const = 0;
  virtual u32 GetSelectedBuildingSet() const = 0;
  virtual ScString *GetCityName() const = 0;
  virtual ScString *GetMayorName() const = 0;
  virtual i64 GetFunds() const = 0;
  virtual u8 AreFundsDebt() const = 0;
  virtual i32 GetDifficulty() const = 0;
  virtual u32 GetStartYear() const = 0;
  virtual u32 GetCitySize() const = 0;
  virtual u8 IsAutoBudgetEnabled() const = 0;
  virtual u8 AreDisastersEnabled() const = 0;
  virtual TrGeneratorInterface *GetDirtGenerator() const = 0;
  virtual ScString *GetCityDescription() const = 0;
};

// Actual cSC3CmdSelectCityScheme retains its dialog after Execute; its destructor shuts down and releases that owner.
// The original Boolean Execute contract publishes this owner; complete interactive dialog verification remains outstanding.
class UiSelectCitySchemeCommand : public UiCitySchemeSelection, public UiCommand {
public:
  UiSelectCitySchemeCommand();
  ~UiSelectCitySchemeCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
  u32 GetSelectedLandScape() const override;
  u32 GetSelectedFloraSet() const override;
  u32 GetSelectedBuildingSet() const override;
  ScString *GetCityName() const override;
  ScString *GetCityDescription() const override;
  i64 GetFunds() const override;
  u8 AreFundsDebt() const override;
  i32 GetDifficulty() const override;
  u32 GetStartYear() const override;
  u32 GetCitySize() const override;
  u8 IsAutoBudgetEnabled() const override;
  u8 AreDisastersEnabled() const override;
  TrGeneratorInterface *GetDirtGenerator() const override;
  ScString *GetMayorName() const override;

protected:
  UiCitySchemeWindow *dialog;
};

#endif
