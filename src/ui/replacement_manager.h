#ifndef SC3_UI_REPLACEMENT_MANAGER_H
#define SC3_UI_REPLACEMENT_MANAGER_H

#include "city/api.h"
#include "common/sc3_array.h"
#include "geometry/api.h"
#include "occupants/api.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_resources.h"
#include "window/api.h"

// Child controls used by the building replacement dialog and its preview/update callbacks.
enum UiReplacementControlId : u32 {
  kUiReplacementAcceptId = 0x157e0b30,
  kUiReplacementOverrideListId = 0x157e0b32,
  kUiReplacementEditOverridesId = 0x157e0b33,
  kUiReplacementSlideFrameId = 0x157e0b37,
  kUiReplacementOriginalAttributesId = 0x157e0b38,
  kUiReplacementOriginalPreviewId = 0x157e0b39,
  kUiReplacementReplacementAttributesId = 0x157e0b3a,
  kUiReplacementReplacementPreviewId = 0x157e0b3b,
  kUiReplacementRevertId = 0x157e0b3c,
  kUiReplacementReplaceId = 0x157e0b3d,
  kUiReplacementReplaceLabelId = 0x157e0b3f,
  kUiReplacementRevertLabelId = 0x157e0b40,
  kUiReplacementInstructionsId = 0x157e0b41,
};

// Bulk replacement controls and their contiguous type and size filter ranges.
enum UiReplacementManagerControlId : u32 {
  kUiReplacementManagerAcceptId = 0x65902b42,
  kUiReplacementManagerBottomDividerId = 0x65902b43,
  kUiReplacementManagerSlidesPanelId = 0x65902b44,
  kUiReplacementManagerPreviewPanelId = 0x65902b45,
  kUiReplacementManagerOverrideListId = 0x65902b46,
  kUiReplacementManagerEditOverridesId = 0x65902b47,
  kUiReplacementManagerAttributesId = 0x65902b48,
  kUiReplacementManagerSlideFrameId = 0x65902b49,
  kUiReplacementManagerPreviewId = 0x65902b4a,
  kUiReplacementManagerReplaceId = 0x65902b4b,
  kUiReplacementManagerRevertId = 0x65902b4c,
  kUiReplacementManagerReplaceLabelId = 0x65902b4d,
  kUiReplacementManagerRevertLabelId = 0x65902b4e,
  kUiReplacementManagerSelectionLabelId = 0x65902b4f,
  kUiReplacementManagerSlidesId = 0x65902b50,
  kUiReplacementManagerTypeOptionsId = 0x65902b51,
  kUiReplacementManagerFirstTypeChoiceId = 0x65902b52,
  kUiReplacementManagerLastTypeChoiceId = 0x65902b56,
  kUiReplacementManagerSizeOptionsId = 0x65902b57,
  kUiReplacementManagerSizeOneId = 0x65902b58,
  kUiReplacementManagerSizeFiveId = 0x65902b5c,
  kUiReplacementManagerReplacementSetLabelId = 0x65902b5d,
  kUiReplacementManagerCurrentSizeLabelId = 0x65902b5e,
  kUiReplacementManagerImportSetId = 0x65902b5f,
  kUiReplacementManagerClearSetId = 0x65902b60,
};

// The command's own156B56FE identity is separate from the inherited command6856F7 view.
class UiReplacementCommandIdentity : public ScUnknown {};

// Actual cSC3CmdReplaceBuildingDialog adds its primary identity before the command and lifetime bases, without fields.
// Publication remains withheld while its replacement procedure has explicit unresolved dependencies.
class UiReplacementCommand : public UiReplacementCommandIdentity, public UiCommand {
public:
  UiReplacementCommand();
  ~UiReplacementCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
};

// Actual stateless cSC3BATReplacementShared control builders retain separate window, ID and resource-key arguments.
// Their private nominal Windows bool/BYTE returns remain qualified; u8 preserves observed byte results.
class UiReplacementControls {
public:
  static u8 AddButton(WnWindow *window, u32 identifier, u32 group, u32 instance);
  static u8 AddSlideFrame(WnWindow *window, u32 identifier, u32 group, u32 instance);
  // A successful resource lookup transfers a retained string view to the caller.
  static u8 GetStringResource(RsManager *manager, const RsKey &key, ScStringView **out);
  static u8 SetCaptionFromResource(WnWindow *window, RsManager *manager, const RsKey &key);
  static void AutoSizeLabelEffects(WnWindow *window);
  static i32 GetSelection(WnWindow *window, u32 identifier);
  static void AutoSizeButton(WnWindow *window);
  static void GetLotSizeTextEffects(ScString *text, u32 size);
};

// Actual cSC3WPRBDInstanceData embedded at12 in the40-byte ReplaceBuildingDialog procedure.
// PC layout places the destructor view first; the Linux build places it after the four retained pointers.
// A non-null indexed payload belongs to the replacement callback and is deleted on shutdown.
// The procedure's embedded instance has its own lifetime.
class UiReplacementData {
public:
  UiReplacementData();
  virtual ~UiReplacementData();
  // Each non-null field owns a reference; destruction releases replacement, original, override and container in order.
  GeAttrib *original;
  OcOverride *override_attributes, *replacement;
  OcOverrideContainer *container;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiReplacementData) == 20);

class UiTokenData;

// Original tOccAttribInfo keeps the retained original and replacement alongside its classified occupant type.
struct UiReplacementInfo {
  GeAttrib *original;
  OcOverride *replacement;
  u32 kind;
};

// Actual45902C49 primary view adds the one-window layout method before the separate procedure callback view.
// The private layout return declaration remains qualified; u8 retains the observed byte carrier.
class UiReplacementManagerControl : public ScUnknown {
public:
  virtual u8 ResizeAndPositionControls(WnWindow *window) = 0;
};

// Actual cSC3WinProcBuildingReplacementMgr owns its procedure at4 and the retained row/container/scheme resources.
// Private effects projections keep unresolved nominal returns explicit alongside the concrete command callback.
class UiReplacementManager : public UiReplacementManagerControl, public UiWindowProcedure {
  friend class UiReplacementManagerCommand;

public:
  UiReplacementManager();
  ~UiReplacementManager() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 ResizeAndPositionControls(WnWindow *window) override;

protected:
  u32 word12;
  ScArray<UiReplacementInfo> infos;
  ScArray<u32> filtered;
  OcOverrideContainer *container;
  u32 type_filter, size_filter;
  CtSchemeManager *schemes;
  u32 word56, word60, enabled_color, disabled_color;
  UiTextRef *count_text;
  RsKey selected_building, selected_slide;
  static void ClearInfosEffects(ScArray<UiReplacementInfo> *infos);
  u8 OnInit(WnWindow *window) override;
  u8 OnShutdown(WnWindow *window) override;
  u8 OnCommand(WnWindow *window, u32 kind, uptr first, uptr second) override;
  u8 OnKeyDown(WnWindow *window, u32 key, u32 modifiers) override;
  u8 OnButton(WnWindow *window, u32 identifier);
  i32 DetermineOccAttribType(const GeAttrib *attributes);
  void SetReplaceEnabledEffects(WnWindow *window, bool enabled);
  void SetRevertEnabledEffects(WnWindow *window, bool enabled);
  static bool IsToken(const ScString &text, i32 position, const ScString &token);
  bool ReplaceToken(const ScString &text, i32 position, UiTokenData *data);
  static bool ReplaceTokenStatic(const ScString &text, i32 position, UiTokenData *data, UiReplacementManager *manager);
  void UpdateFilteredBuildingCountTextEffects(WnWindow *window);
  void FilterOccAttribsEffects(WnWindow *window);
  void InitOccAttribListEffects(WnWindow *window);
  void DetermineReplaceabilityEffects(WnWindow *window);
  u8 RequerySlides(WnWindow *window);
  u8 RequeryListbox(WnWindow *window);
  void TypeFilterSelectionChangedEffects(WnWindow *window);
  void SizeFilterSelectionChangedEffects(WnWindow *window);
  void DisplayCurrentSizeEffects(WnWindow *window, u32 size);
  void ListBoxSelectionChangedEffects(WnWindow *window);
  void SliderSelectionChangedEffects(WnWindow *window);
  u8 BATBuildingListEdited(WnWindow *window);
  void EndDialogEffects(WnWindow *window, u32 result);
  void ClearReplacementSet(WnWindow *window);
  u8 ImportReplacementSet(WnWindow *window);
  u8 InitControls(WnWindow *window);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiReplacementInfo) == 12);
static_assert(sizeof(uptr) != 4 || sizeof(UiReplacementManager) == 100);

class UiReplacementManager;

// Original7591A994 identity precedes the inherited command and its separate lifetime view.
class UiReplacementManagerIdentity : public ScUnknown {};

// Actual cSC3CmdBuildingReplacementMgrDialog adds no fields to its command and distinct primary identity.
class UiReplacementManagerCommand : public UiReplacementManagerIdentity, public UiCommand {
public:
  UiReplacementManagerCommand();
  ~UiReplacementManagerCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;

protected:
  void DoRequestedChangesEffects(UiReplacementManager *manager);
};

// Actual cISC3WinProcReplaceBuildingDialog primary256B5B93 view, separate from its window-procedure callback.
// Private nominal Windows bool/BYTE results remain qualified; u8 preserves their observed byte contracts.
class UiReplacementControl : public ScUnknown {
public:
  // The replacement is borrowed; each setter retains its incoming attributes before releasing the previous value.
  virtual OcOverride *GetReplacement() = 0;
  virtual u8 SetOriginalInfo(WnWindow *window, GeAttrib *attributes) = 0;
  virtual u8 SetOverRideInfo(WnWindow *window, OcOverride *attributes) = 0;
  virtual u8 Replace(WnWindow *window) = 0;
  virtual u8 Revert(WnWindow *window) = 0;
};

// Actual40-byte cSC3WinProcReplaceBuildingDialog owns its callback at4, instance data at12 and two label colors.
// Preview enable contracts and indexed-payload deletion remain explicit gaps.
// The class factory publishes its callback view; remaining preview/payload gaps stay explicit in the handlers.
class UiReplacementProcedure : public UiReplacementControl, public UiWindowProcedure {
public:
  UiReplacementProcedure();
  ~UiReplacementProcedure() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  OcOverride *GetReplacement() override;
  u8 SetOriginalInfo(WnWindow *window, GeAttrib *attributes) override;
  u8 SetOverRideInfo(WnWindow *window, OcOverride *attributes) override;
  u8 Replace(WnWindow *window) override;
  u8 Revert(WnWindow *window) override;

protected:
  UiReplacementData data;
  u32 disabled_color, enabled_color;
  u8 OnInit(WnWindow *window) override;
  u8 OnShutdown(WnWindow *window) override;
  u8 OnKeyDown(WnWindow *window, u32 key, u32 modifiers) override;
  u8 OnCommand(WnWindow *window, u32 kind, uptr first, uptr second) override;
  u8 OnButton(WnWindow *window, u32 identifier);
  u8 GetSelectedOverride(WnWindow *window, u32 identifier, OcOverride **out);
  u8 InitControls(WnWindow *window);
  u8 RequeryList(WnWindow *window);
  void RefreshAfterEditorEffects(WnWindow *window);
  u8 ResizeAndPositionControls(WnWindow *window);
  void EndDialogEffects(WnWindow *window, u32 result);
  void SetReplaceEnabledEffects(WnWindow *window, u8 enabled);
  void SetRevertEnabledEffects(WnWindow *window, u8 enabled);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiReplacementProcedure) == 40);

#endif
