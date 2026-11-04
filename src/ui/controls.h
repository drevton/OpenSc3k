#ifndef SC3_UI_CONTROLS_H
#define SC3_UI_CONTROLS_H

#include "audio/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/sc3_graphics.h"
#include "ui/api.h"
#include "ui/ui_module.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// The frame's first atlas button emits this control ID to request closure of its dialog.
enum UiFrameButtonId : u32 {
  kUiFrameWindowId = 0x42b7c351,
  kUiGenericScrollerId = 0x42b7c352,
  kUiFrameCloseButtonId = 0x42b7c353,
  kUiFrameSecondButtonId = 0x42b7c354,
  kUiFrameBackButtonId = 0x42b7c355,
};

// Frame button visibility and state selectors; Init retains its original state-polarity handling.
enum UiFrameStyle : u32 {
  kUiFrameCloseVisible = 0x00000001,
  kUiFrameSecondVisible = 0x00000002,
  kUiFrameBackVisible = 0x00000004,
  kUiFrameCloseState = 0x00000100,
  kUiFrameSecondState = 0x00000200,
  kUiFrameBackState = 0x00000400,
};

// The copied SIMUI window base binds its module's manager before derived members are constructed.
class UiBaseWindow : public WnRootWidget {
public:
  UiBaseWindow();
  bool Init() override;
  bool Shutdown() override;
  u32 SetFlag(u32 mask, bool enabled) override;
  bool PrivateBuffer(bool enabled) override;
  bool Plot() override;
  void SetBufferToDrawTo() override;
  using WnRootWidget::SetFillColor;
  void SetFillColor(u32 pixel) override;
  u32 GetFillColor() const override;
  u32 MakeFillColor(u8 red, u8 green, u8 blue) override;

protected:
  bool SetBufferKey(bool enabled);
};

// Actual cISC3WinMinMaxBtn secondary5324CC9B interface of the four-frame min/max button.
// The root window remains its canonical primary view.
class UiMinMaxButtonInterface : public ScUnknown {
public:
  enum class UiMode : i32;
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool SetMinMaxMode(UiMode mode) = 0;
  virtual u32 GetMinMaxMode() = 0;
};

// Actual cSC3WinMinMaxBtn owner has256 source bytes: three buffers, a source rectangle, frame dimensions and input state.
// Two separately owned captions provide its ordinary and alternate hints.
class UiMinMaxButton : public UiBaseWindow, public UiMinMaxButtonInterface {
public:
  UiMinMaxButton();
  static UiMinMaxButtonInterface *CreateNewObject();
  ~UiMinMaxButton() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool SetMinMaxMode(UiMode mode) override;
  u32 GetMinMaxMode() override;
  bool Init() override;
  bool Shutdown() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u32 SetFlag(u32 flag, bool enabled) override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool GZPaint() override;

protected:
  enum class UiImageIndex : i32;
  GxBuffer *atlas, *alpha, *opaque;
  ScRect source;
  i32 frame_width, frame_height;
  bool pressed, hovered;
  u32 mode;
  ScString ordinary_hint, alternate_hint;
  u32 hint_token;
  bool IsPointInWindow(i32 x, i32 y) const override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  void RequeryImageEffects();
  void SetImageIndexEffects(UiImageIndex index);
  void HideTipEffects();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMinMaxButton) == 256);

// The original scoped cursor holds only whether its push succeeded.
class UiCursorScope {
public:
  explicit UiCursorScope(u32 identifier);
  ~UiCursorScope();

private:
  u8 pushed;
};

// The original188-byte bordered panel extends the copied SIMUI root with two BYTE modes and four logical RGB colors.
class UiBorderPanel : public UiBaseWindow, public WnFlatControl {
public:
  UiBorderPanel();
  ~UiBorderPanel() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  void SetFillColor(u8 red, u8 green, u8 blue) override;
  void SetFillColor(u32 pixel) override;
  void GetFillColor(u8 *red, u8 *green, u8 *blue) const override;
  u32 GetFillColor() const override;
  using UiBaseWindow::SetArea;
  u8 AutoSize(const ScRect &rectangle) override;
  u8 SetOutlineFlags(u8 mode) override;
  u8 GetOutlineFlags() override;
  u8 GetOutlineFlag(i32 mask) override;
  void SetOutlineColor(u8 red, u8 green, u8 blue) override;
  void SetOutlineColor(u32 pixel) override;
  void SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) override;
  void GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) override;
  void SetOutlineColor(u8 edge, u8 red, u8 green, u8 blue) override;
  void GetOutlineColor(u8 edge, u8 *red, u8 *green, u8 *blue) const override;
  u8 SetSizeMode(u8 mode) override;
  u8 GetSizeMode() override;
  bool GZPaint() override;
  bool DoMessage(ScMessage *message) override;

protected:
  u8 layout_mode, border_mode;
  u32 border_colors[4];
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
};

// Actual cSC3WinBudgetSlider owns its primary SIMUI window and separate slider interface at offset164 on x86.
// Private helper return spellings remain qualified; coordinates and value operations retain their source arguments.
class UiBudgetSliderWindow : public UiBaseWindow, public UiBudgetSlider {
public:
  UiBudgetSliderWindow();
  ~UiBudgetSliderWindow() override;
  static UiBudgetSlider *CreateNewObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u8 SetValue(i32 value) override;
  i32 GetValue() const override;
  u8 SetMarkerValue(i32 value) override;
  u8 GetMarkerValue(i32 *value) const override;
  u8 SetMinMax(i32 minimum, i32 maximum) override;
  u8 GetMinMax(i32 *minimum, i32 *maximum) const override;
  bool Init() override;
  bool Shutdown() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;

protected:
  i32 value, maximum, minimum, marker;
  GxBuffer *bar_atlas, *thumb_atlas, *marker_image;
  i16 thumb_width, bar_width;
  ScRect bar_source, thumb_source, marker_area, thumb_area;
  u8 hovered, pressed;
  u8 SendSliderChangedMessage();
  u8 UpdateMetrics();
  u8 GetMousePosition(i32 x, i32 y);
  i32 GetValueFromMousePosition(i32 x, i32 y);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiBudgetSliderWindow) == 268);

class RsManager;
struct RsKey;

// Reached35906DA2 view of cSC3FixedBtnOptGrp; original slot32 writes the selected ID through a mutable reference.
class UiFixedOptionsControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual u8 AddOption(u32 option_id, GxBuffer *image, const ScRect &rectangle) = 0;
  virtual u8 RemoveOption(u32 option_id) = 0;
  virtual u8 LayoutTopToBottom() = 0;
  virtual u8 LayoutLeftToRight() = 0;
  virtual u8 GetSelection(u32 *value) const = 0;
  virtual u8 SetSelection(u32 option_id) = 0;
};

// Actual cSC3FixedBtnOptGrp keeps the root primary and35906DA2 secondary, followed by its selected ID and option IDs.
// The original ordered set is represented by sorted domain IDs; native storage is never a foreign tree overlay.
class UiFixedOptions : public UiBaseWindow, public UiFixedOptionsControl {
public:
  UiFixedOptions();
  ~UiFixedOptions() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u8 AddOption(u32 option_id, GxBuffer *image, const ScRect &rectangle) override;
  u8 RemoveOption(u32 option_id) override;
  u8 LayoutTopToBottom() override;
  u8 LayoutLeftToRight() override;
  u8 GetSelection(u32 *value) const override;
  u8 SetSelection(u32 option_id) override;
  static u8 AddOptionResource(UiFixedOptionsControl *options, u32 option_id, RsManager *resources, const RsKey &key);

protected:
  u32 selected;
  ScArray<u32> options;
  bool OnCommand(u32 kind, u32 command) override;
  u8 SetToggle(u32 option_id, bool enabled);
  u8 GetToggle(u32 option_id, bool *enabled);
  void SendChangedEffects();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiFixedOptions) == 184);

// The original248-byte frame derives through the bordered panel and owns its font, display caption and three buttons.
class UiFrame : public UiBorderPanel {
public:
  UiFrame();
  ~UiFrame() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  using UiBorderPanel::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u8 AutoSize(const ScRect &rectangle) override;
  void SetCaption(const ScStringView &text) override;
  virtual bool GetStyle(u32 flag);
  virtual bool SetStyle(u32 flag, u32 enabled);
  virtual bool SetFont(FtFont *font);
  bool GetMinimum(i32 *width, i32 *height);

protected:
  u32 frame_style;
  FtFont *font;
  u32 text_color;
  ScString display_caption;
  i32 text_x, text_y, text_right;
  u32 word232;
  WnButton *first_button, *second_button, *third_button;
  bool FormatTitle();
  bool Relayout();
  bool CreateButton(u32 identifier, GxBuffer *image, const ScRect *rectangle, WnButton **out);
};

#endif
