#ifndef SC3_UI_OVERRIDE_EDITOR_H
#define SC3_UI_OVERRIDE_EDITOR_H

#include "application/application_core.h"
#include "city/city.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "resources/resource.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"
#include "ui/query_dialogs.h"

// Active-list and directory controls in the installed building override editor.
enum UiOverrideEditorControlId : u32 {
  kUiOverrideEditorAcceptId = 0x258a0353,
  kUiOverrideEditorBottomDividerId = 0x258a0354,
  kUiOverrideEditorPreviewBitmapId = 0x258a0355,
  kUiOverrideEditorActiveListId = 0x258a0356,
  kUiOverrideEditorActiveListLabelId = 0x258a0357,
  kUiOverrideEditorDirectoryLabelId = 0x258a0358,
  kUiOverrideEditorDriveComboId = 0x258a0359,
  kUiOverrideEditorFileListId = 0x258a035a,
  kUiOverrideEditorAddBuildingId = 0x258a035b,
  kUiOverrideEditorRemoveBuildingId = 0x258a035c,
  kUiOverrideEditorPreviewOutlineId = 0x258a035d,
  kUiOverrideEditorPreviewCaptionId = 0x258a035e,
};

class CtCity;
class WnListControl;
class WnLabelControl;
class WnFlatControl;
class WnButton;
class SpCitySpritePreview;

// ClassE3091435 extends the real query collection with picker24, callback28 and city-enumerator32 views.
// Owns resource-backed controls, selected-row preview and the landmark availability state.
class UiCatalogPicker : public UiDataRows, public UiBuildingPicker, public UiWindowCallback, public CtCityEnumerator {
public:
  UiCatalogPicker();
  ~UiCatalogPicker() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetCategoryEffects(u32 category) override;
  u8 Init() override;
  WnWindow *GetWindow() const override;
  bool HasSelection() const override;
  u8 GetSelection(GeAttrib **out) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  virtual u8 NotifyTarget(CtCity *city, u8 opening);

protected:
  u32 category;
  u8 has_selection, can_select;
  u32 selected_index, next_label_id;
  UiGenericInterface *window;
  SpCitySpritePreview *preview;
  WnListControl *list;
  WnLabelControl *labels[8];
  WnFlatControl *frame;
  WnButton *action_button;

  u8 Attach(WnWindow *recipient);
  u8 Detach(WnWindow *recipient);
  u8 EndModal(WnWindow *recipient, u32 result);
  u8 SelectEntry(u32 index);
  u8 BuildCatalog();
  void CenterEffects();
  WnLabelControl *CreateRegion(i32 left, i32 top, i32 width, i32 height);
};

class SpDefinition;

// Actual cSC3WinProcEditOverRidesDialog::mBuildingInfo retains its name, lot size, sprite, file and resource key.
// The two strings own storage; the sprite is borrowed and never released by this record.
class UiEditBuilding {
public:
  UiEditBuilding();
  ~UiEditBuilding();
  u8 pending;
  ScString name;
  u8 lot_size;
  SpDefinition *sprite;
  i32 zoom;
  ScString file;
  RsKey key;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiEditBuilding) == 68);
static_assert(sizeof(uptr) != 4 || offsetof(UiEditBuilding, key) == 56);

// Original4591A5AD identity precedes the inherited command and its separate lifetime view.
class UiEditOverridesIdentity : public ScUnknown {};

// Actual cSC3CmdEditOverRidesDialog owns no fields beyond its inherited command queues and parameter descriptors.
class UiEditOverridesCommand : public UiEditOverridesIdentity, public UiCommand {
public:
  UiEditOverridesCommand();
  ~UiEditOverridesCommand() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Execute() override;
};

class GxBuffer;
class BdDocument;
class WnListControl;
class WnComboControl;

i32 UiShowResourceMessage(u32 body_group, u32 body_instance, u32 caption_group, u32 caption_instance, u32 style);

// Native directory-list links preserve the source's stable borrowed String identities without STL allocator machinery.
struct UiEditorPathLink {
  UiEditorPathLink *next, *previous;
};

struct UiEditorPath : UiEditorPathLink {
  ScString value;
};

// The sentinel has no String lifetime; only actual directory entries construct and retire their owned String.
class UiEditorPaths {
public:
  UiEditorPaths();
  ~UiEditorPaths();
  void Clear();
  void Append(const ScString &value);
  // Keep the native sentinel and directory allocations under one owner.
  UiEditorPaths(const UiEditorPaths &) = delete;
  // Assignment cannot transfer or duplicate the native list ownership.
  UiEditorPaths &operator=(const UiEditorPaths &) = delete;
  UiEditorPathLink *head;
};

// The private158A00A5 identity is distinct from the inherited default-procedure callback.
class UiEditorIdentity : public ScUnknown {};

// Actual cSC3WinProcEditOverRidesDialog owns its preferences, directory inventory, preview and pending building edits.
// The nested command and factory publication remain separate integration work.
class UiOverrideEditor : public UiEditorIdentity, public UiWindowProcedure {
public:
  UiOverrideEditor();
  ~UiOverrideEditor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;

protected:
  ApEditBldgPrefs preferences;
  ScString directory, drive, file;
  ScStringView *selected_name, *selected_architect;
  u8 lot_size;
  UiEditorPaths paths;
  UiEditorPathLink *path_cursor;
  GxBuffer *preview;
  BdDocument *document;
  u8 directory_busy, reading_directory, file_selected, active_selected, selection_busy;
  ScArray<UiEditBuilding *> buildings;
  ScArray<RsKey> removed;
  u32 added_count, disabled_color, highlight_color, first_color, second_color, third_color;
  // Byte-returning helpers preserve AL; their nominal bool-versus-BYTE spelling remains unproved.
  u8 AddBuilding(WnWindow *window);
  u8 InitActiveList(WnListControl *active);
  u8 InitControls(WnWindow *window);
  u8 SetupPath(WnComboControl *combo);
  u8 InitDriveCombo(WnComboControl *combo);
  u8 ReadDirectory(WnComboControl *combo, WnListControl *files);
  u8 DriveChange(WnWindow *window);
  u8 HandleFileListDoubleClick(WnWindow *window);
  static u8 SC3_STDCALL InitDriveList(WnComboControl *combo);
  u8 HandleActiveListSelected(WnWindow *window);
  u8 HandleFileListSelected(WnWindow *window);
  u8 RenderBuildings(WnWindow *window);
  u8 RemoveBuilding(WnWindow *window);
  u8 DeselectActiveList(WnWindow *window);
  u8 DeselectFileList(WnWindow *window);
  static u8 SC3_STDCALL SelectActiveList(WnWindow *window);
  static u8 SC3_STDCALL SelectFileList(WnWindow *window);
  static u8 SC3_STDCALL UpdateAddButton(WnWindow *window, bool enabled);
  static u8 SC3_STDCALL UpdateRemoveButton(WnWindow *window, bool enabled);
  static u8 SC3_STDCALL UpdateLotSizeLabel(WnWindow *window, u8 size);
  static u8 SC3_STDCALL HideLotSizeLabel(WnWindow *window);
  static i32 SC3_STDCALL FindBestFrame(SpDefinition *definition, u32 width, u32 height);
  static u8 SC3_STDCALL GetFrame(GxBuffer **out, SpDefinition *definition, i32 frame);
  static u8 SC3_STDCALL FitBuffer(WnWindow *window, GxBuffer **buffer, u32 width, u32 height);
  u8 OnInit(WnWindow *window) override;
  u8 OnCommand(WnWindow *window, u32 kind, uptr first, uptr second) override;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiOverrideEditor) == 180);

#endif
