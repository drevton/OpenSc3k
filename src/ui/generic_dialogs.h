#ifndef SC3_UI_GENERIC_DIALOGS_H
#define SC3_UI_GENERIC_DIALOGS_H

#include "common/api.h"
#include "common/sc3_string.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "window/api.h"

// The UI manager locates its panels and city viewport through these shared child identities.
enum UiMainControlId : u32 {
  kUiMinMaxToggleId = 0x42ebdafa,
  kUiMainRciPanelId = 0x42fb7dea,
  kUiMainDateCashPanelId = 0x42fb7deb,
  kUiMainMenuPanelId = 0x42fb7dec,
  kUiMainNavigationPanelId = 0x42fb7ded,
  kUiMainCityViewportId = 0x6104489c,
  kUiNavigationMapControlId = 0x22f96e25,
};

// Tip-dialog controls route previous/next navigation, startup preference and closing.
enum UiTipControlId : u32 {
  kUiTipStartupCheckboxId = 0xa2fbb0b3,
  kUiTipPreviousButtonId = 0xe2fba4b3,
  kUiTipNextButtonId = 0xe2fba4b4,
  kUiTipCloseButtonId = 0xe2fa5bc2,
  kUiTipDialogWindowId = 0xc2fa5860,
};

// Generic-window geometry, frame controls and painting flags, interpreted by UiGenericWindow::SetStyle.
enum UiGenericStyle : u32 {
  kUiGenericDraggable = 0x1,
  kUiGenericResizable = 0x2,
  kUiGenericCloseOnEscapeAltF4 = 0x4,
  kUiGenericCloseButton = 0x10,
  kUiGenericBackButton = 0x20,
  kUiGenericSecondButton = 0x40,
  kUiGenericCloseButtonState = 0x100,
  kUiGenericBackButtonState = 0x200,
  kUiGenericSecondButtonState = 0x400,
  kUiGenericNoFrame = 0x1000,
  kUiGenericNoBackground = 0x2000,
  kUiGenericNoBevel = 0x4000,
  kUiGenericNoPainting = 0x8000,
  kUiGenericScroller = 0x10000,
};

// Body, icon, panel and button-row identities used by the generic message-box callback.
enum UiMessageBoxControlId : u32 {
  kUiMessageBoxBodyId = 0x53743817,
  kUiMessageBoxIconId = 0x53743818,
  kUiMessageBoxPanelId = 0x53743819,
  kUiMessageBoxButtonRowId = 0x5374381a,
};

class GxBuffer;
// Borrowed caller-defined data passes through this view; each caller owns its payload layout and lifetime.
struct UiWindowData;

// The generic window calls this real callback interface with separate message words and pointer-valued payloads.
// Retail decorated overrides and the cIGZWinProc inheritance chain establish the Boolean DoWinMsg contract.
class UiWindowCallback : public ScUnknown {
public:
  virtual bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) = 0;
};

// Actual cIGZWinGen base occupies slots12..48 before SIMUI's cISC3WinGen extension.
// Existing native logical-data pointer spelling retains its separately recorded nominal DWORD qualification.
class UiGenericBase : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual u32 GetClassID() = 0;
  virtual bool GetStyle(u32 flag) = 0;
  virtual bool SetStyle(u32 flag, u32 enabled) = 0;
  virtual bool SetNotify(UiWindowCallback *callback) = 0;
  virtual u8 SetIndexedData(u32 index, UiWindowData *data) = 0;
  virtual u8 GetIndexedData(u32 index, UiWindowData **out) = 0;
  virtual bool GetClientArea(ScRect *out) = 0;
  virtual u8 SetGutters(i8 left, i8 top, i8 right, i8 bottom) = 0;
  virtual u8 GetGutters(i32 *left, i32 *top, i32 *right, i32 *bottom) const = 0;
};

// Retail cISC3WinGen derives the generic base and adds slots52..92 in this order.
// Both original IIDs publish the same secondary view, rather than two unrelated native interface subsets.
class UiGenericInterface : public UiGenericBase {
public:
  virtual u8 GetCallback(UiWindowCallback **out) = 0;
  // Original56/2D2BE forwards to SetNotify28/2D287 and retains the actual callback view (IID22E85D8E).
  virtual bool SetCallback(UiWindowCallback *callback) = 0;
  virtual bool SetImage(u32 index, GxBuffer *buffer) = 0;
  virtual u8 GetImage(u32 *index, GxBuffer **out) = 0;
  // Original68/2D311 stores borrowed data at secondary+56; that field offset is not the callback method slot.
  virtual u8 SetData(UiWindowData *data) = 0;
  virtual UiWindowData *GetData() = 0;
  virtual bool SetBorder(u8 horizontal, u8 vertical) = 0;
  virtual bool GetBorder(u32 *horizontal, u32 *vertical) = 0;
  virtual bool AddPanel(u32 identifier, u32 layout, u32 border, const ScRect *rectangle) = 0;
  virtual bool AddDivider(u32 identifier, i32 top) = 0;
  virtual bool RemovePanel(u32 identifier) = 0;
};

// Actual cSC3WinProcMinMax callback owns only its count; it is distinct from the default procedure implementation.
class UiMinMaxProcedure : public UiWindowCallback {
public:
  UiMinMaxProcedure();
  virtual ~UiMinMaxProcedure();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateMinMaxWindow(WnWindow **out);
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;

protected:
  u32 references;
  u8 OnInit(WnWindow *window);
  u8 OnShutdown(WnWindow *window);
  u8 OnUiMaximize(WnWindow *window, u8 maximized);
  u8 OnPointInWindow(WnWindow *window, i32 x, i32 y, bool *out);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMinMaxProcedure) == 8);

class UiTerrainEditorInterface;

class UiPanelInterface : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual void SetViewEffects(u32 view) = 0;
};

// Original cISC3MainUIMgr from retail CodeView299F/29A0: message dispatch12, initialization16 and shutdown20.
class UiMainManager : public ScMessageTarget {
public:
  virtual bool Init(WnWindow *window) = 0;
  virtual bool Shutdown() = 0;
};

// Original cSC3MainUIMgr owns the city's panels, circular child-identifier list and message subscriptions.
class UiMainManagerObject : public UiMainManager {
public:
  UiMainManagerObject();
  virtual ~UiMainManagerObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool Init(WnWindow *window) override;
  bool Shutdown() override;
  bool Layout();

protected:
  struct UiChild {
    UiChild *next, *previous;
    u32 identifier;
  };

  u32 references;
  bool active;
  ScRect client;
  WnWindow *owner;
  UiChild *children;
  bool first_visible, second_visible, dialog_visible;
  UiTerrainEditorInterface *dialog;
  bool saved_visibility;
  void UpdateExtentsEffects();
  void SetVisibility(u32 enabled);
  void AppendChild(u32 identifier);
};

// Concrete class035EE501 consumes queued operation/range/value arguments and an optional copied String.
class UiProgressCommand : public UiCommand {
public:
  UiProgressCommand();
  bool Execute() override;
  bool ReadInteger(u32 *out);
  bool ReadString(ScString **out);
  bool OpenRange(u32 first, u32 last, ScStringView *caption);
  bool SetValue(u32 value);
  bool Close();
};

// Original68-byte dialog payload owns two strings; callback paths borrow it through the generic window's data field.
struct UiMessageBoxData {
  ScString body, caption;
  u32 style, accept, cancel, word52, buttons[3];
};

// Actual8-byte callback owner shares one reference count while each modal invocation owns a separate two-string payload.
class UiMessageBox : public UiWindowCallback {
public:
  UiMessageBox();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual u8 Show(u32 *out, const ScStringView &body, const ScStringView &caption, u32 style);
  virtual ~UiMessageBox();
  UiMessageBoxData *GetData(WnWindow *window);
  u8 End(WnWindow *window, u32 command);
  u8 OnCommand(WnWindow *window, u32 command);
  u8 OnKey(WnWindow *window, u32 key, u32 modifiers);
  u8 MeasureText(WnWindow *window, ScRect *out);
  u8 MeasureButtons(WnWindow *window, ScRect *out);
  u8 Layout(WnWindow *window, const ScRect &client);

protected:
  u32 references;
  u8 Attach(WnWindow *window);
  u8 GetLimits(WnWindow *window, ScRect *out);
  u8 CreatePanel(WnWindow *window, u32 identifier);
  u8 CreateButton(WnWindow *window, u32 identifier);
  u8 CreateText(WnWindow *window, u32 identifier, const ScStringView *body);
  u8 CreateIcon(WnWindow *window, u32 identifier, u32 kind);
};

UiMessageBox *UiCreateMessageBox();

// Actual generic-window owner. Methods not yet reconstructed are declared explicitly rather than inherited as success.
class UiGenericWindow : public UiBaseWindow, public UiGenericInterface {
public:
  UiGenericWindow();
  ~UiGenericWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool GetStyle(u32 flag) override;
  bool SetStyle(u32 flag, u32 enabled) override;
  bool SetCallback(UiWindowCallback *callback) override;
  bool SetNotify(UiWindowCallback *callback) override;
  bool SetBorder(u8 horizontal, u8 vertical) override;
  bool SetImage(u32 index, GxBuffer *buffer) override;
  bool GetClientArea(ScRect *out) override;
  u32 GetClassID() override;
  u8 GetCallback(UiWindowCallback **out) override;
  u8 GetImage(u32 *index, GxBuffer **out) override;
  u8 SetData(UiWindowData *data) override;
  UiWindowData *GetData() override;
  u8 SetIndexedData(u32 index, UiWindowData *data) override;
  u8 GetIndexedData(u32 index, UiWindowData **out) override;
  bool GetBorder(u32 *horizontal, u32 *vertical) override;
  u8 SetGutters(i8 left, i8 top, i8 right, i8 bottom) override;
  u8 GetGutters(i32 *left, i32 *top, i32 *right, i32 *bottom) const override;
  virtual bool AddPanel(u32 identifier, u32 layout, u32 border, const ScRect *rectangle) override;
  bool AddDivider(u32 identifier, i32 top) override;
  bool RemovePanel(u32 identifier) override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  bool DoMessage(ScMessage *message) override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  void GZWinMoveTo(i32 x, i32 y) override;
  void SetCaption(const ScStringView &text) override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;

protected:
  u32 style, word172, word176;
  i32 minimum_width, minimum_height;
  UiFrame *frame;
  u32 resource_index;
  GxBuffer *image;
  ScRect client;
  UiWindowCallback *callback;
  UiWindowData *data;
  u8 border_x, border_y;
  bool CreateFrame();
  bool AddScroller();
  bool RemoveScroller();
  void SetClientEffects(i32 width, i32 height, ScRect *out);
  void SetOuterEffects(const ScRect *rectangle, ScRect *out);
  bool Relayout();
  i32 LayoutChildren();
  bool RequestClose();
  i32 HitTest(i32 x, i32 y);
  bool OnCommand(u32 kind, u32 command) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool IsPointInWindow(i32 x, i32 y) const override;
};

UiGenericInterface *UiCreateGenericWindow();

// One published callback owns the modeless progress window, range, caption and temporary flipping adjustment.
class UiProgressDialog : public UiWindowCallback {
public:
  UiProgressDialog();
  virtual ~UiProgressDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Attach(WnWindow *window);
  bool Detach(WnWindow *window);
  void Center();
  void UpdateBar();
  static bool Open();
  static bool Close();
  static bool SetRange(u32 first, u32 last);
  static bool SetCaption(ScStringView *text);
  static bool SetValue(u32 value);
  static UiProgressDialog *current;
  UiGenericInterface *window;
  u32 range_start, range_end, value;
  ScString *caption;
  bool flipping;
  u32 references;
};

class WnLabelControl;
class ScString;

// The original primary dialog and secondary window callback share one count and own two independent control references.
class UiTipDialog : public UiTipDialogInterface, public UiWindowCallback {
public:
  UiTipDialog();
  virtual ~UiTipDialog();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init() override;
  WnWindow *GetWindow() const override;
  bool DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr third) override;
  u8 Attach(WnWindow *recipient);
  u8 Detach(WnWindow *recipient);
  void CenterEffects();
  void SetTipEffects(u32 instance);
  void NextTipEffects();
  void PreviousTipEffects();
  WnLabelControl *CreateText(u32 left, u32 top, u32 width, u32 height, ScString *caption, u32 alignment);
  u8 CreateFrame();
  u8 CreateButton(u32 identifier, u32 left, u32 top, ScString *caption);
  u8 LoadPreferences();
  void SavePreferencesEffects();

  u32 references, current_tip;
  UiGenericInterface *window;
  WnLabelControl *text;
};

u8 SC3_STDCALL UiCloseTipDialog(WnWindow *window);

// SIMUI's actual eight-byte default window-procedure owner extends the callback view with routed virtual handlers.
// Actual cSC3WinProc owns this module's concrete callback copy, without a foreign-vtable overlay.
class UiWindowProcedure : public UiWindowCallback {
public:
  UiWindowProcedure();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  virtual ~UiWindowProcedure();

protected:
  u8 AddControl(WnWindow *window, u32 kind, u32 identifier) const;
  u8 GetFont(u32 identifier, FtFont **out) const;
  virtual u8 OnBeforeMessage(WnWindow *window, u32 message, uptr first, uptr second, uptr third);
  virtual u8 OnInit(WnWindow *window);
  virtual u8 OnShutdown(WnWindow *window);
  virtual u8 OnKeyDown(WnWindow *window, u32 key, u32 modifiers);
  virtual u8 OnGetLimits(WnWindow *window, ScRect *limits, ScRect *client);
  virtual u8 OnClientChanged(WnWindow *window, ScRect *client);
  virtual u8 OnCommand(WnWindow *window, u32 command, uptr first, uptr second);
  u32 references;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiWindowProcedure) == 8);

class WnLabelControl;
class WnTextControl;

// Private user-input view in its observed slot order; discarded setter returns remain unresolved.
class UiUserInputInterface : public ScUnknown {
public:
  virtual u8 GetResult() = 0;
  virtual void SetTitleText(const ScStringView &text) = 0;
  virtual void SetMessageText(const ScStringView &text) = 0;
  virtual void GetInputText(ScStringView *out) = 0;
  virtual void SetInputText(const ScStringView &text) = 0;
  virtual WnWindow *AsIGZWin() = 0;
};

// Generic user-input window with its private interface, callback, controls and two original strings.
// Text setter and private nominal return contracts remain explicit pending recovery.
class UiUserInput : public UiGenericWindow, public UiUserInputInterface, public UiWindowCallback {
public:
  UiUserInput();
  ~UiUserInput() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  u8 GetResult() override;
  void SetTitleText(const ScStringView &text) override;
  void SetMessageText(const ScStringView &text) override;
  void GetInputText(ScStringView *out) override;
  void SetInputText(const ScStringView &text) override;
  WnWindow *AsIGZWin() override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;

protected:
  u8 result;
  WnLabelControl *message_label;
  WnTextControl *input;
  ScString message_text, input_text;
  u8 Close(u32 command);
  u8 OnButtonActivated(u32 identifier);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiUserInput) == 288);

#endif
