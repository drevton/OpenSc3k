#ifndef SC3_WINDOW_CONTROLS_H
#define SC3_WINDOW_CONTROLS_H

#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "window/api.h"

// Synchronous GZWIND hint payload. Its returned token is written directly by the real message target.
struct WnHint {
  WnHint();
  u32 kind, flags;
  const ScStringView *text;
  ScRect rectangle;
  u32 word28, color, word36, token;
};

class FtFont;
class WnImageButton;

enum WnTipPlacement : u32 {
  kWnTipAtAnchor = 0x00010000,
  kWnTipLeftTop = 0x00020000,
  kWnTipLeftMiddle = 0x00040000,
  kWnTipLeftBottom = 0x00080000,
  kWnTipRightTop = 0x00100000,
  kWnTipRightMiddle = 0x00200000,
  kWnTipRightBottom = 0x00400000,
  kWnTipAboveLeft = 0x00800000,
  kWnTipAboveCenter = 0x01000000,
  kWnTipAboveRight = 0x02000000,
  kWnTipBelowLeft = 0x04000000,
  kWnTipBelowCenter = 0x08000000,
  kWnTipBelowRight = 0x10000000,
  kWnTipPlacementMask = 0xffff0000,
};

// Button state, caption layout and hint-forwarding bits interpreted by WnImageButton.
enum WnButtonFlag : i32 {
  kWnButtonChecked = 0x00000001,
  kWnButtonToggleOnPress = 0x00000002,
  kWnButtonShowText = 0x00000004,
  kWnButtonNoFallbackPainting = 0x00000010,
  kWnButtonAutoFit = 0x00000020,
  kWnButtonWrapText = 0x00000040,
  kWnButtonOffsetPressedText = 0x00000080,
  kWnButtonShowHint = 0x00000100,
  kWnButtonHintFlagOne = 0x00000200,
  kWnButtonHintFlagTwo = 0x00000400,
  kWnButtonPressed = 0x00001000,
  kWnButtonHovered = 0x00002000,
  kWnButtonKeyPressed = 0x00004000,
  kWnButtonPressedOrChecked = 0x00001001,
};

// Image-button font-color slots, selected as enabled, hovered or disabled start/end pairs.
enum WnButtonTextColor : i32 {
  kWnButtonTextEnabled = 0x00000000,
  kWnButtonTextEnabledEnd = 0x00000001,
  kWnButtonTextHovered = 0x00000002,
  kWnButtonTextHoveredEnd = 0x00000003,
  kWnButtonTextDisabled = 0x00000004,
  kWnButtonTextDisabledEnd = 0x00000005,
};

// Image-button atlas selectors, preserving their distinct stored mode even for matching draw paths.
enum WnButtonStyle : i32 {
  kWnButtonTextOnly = 0x00000001,
  kWnButtonPushStretch = 0x00000002,
  kWnButtonPushFixed = 0x00000003,
  kWnButtonToggleStretch = 0x00000004,
  kWnButtonToggleFixed = 0x00000005,
  kWnButtonToggleEightState = 0x00000006,
  kWnButtonToggleEightStateAlternate = 0x00000007,
};

// Original 46-slot button interface. Overload groups preserve the installed MSVC slot order.
class WnButton : public ScUnknown {
public:
  virtual void SetID(u32 identifier) = 0;
  virtual u32 GetID() const = 0;
  virtual void SetCaption(const ScStringView &text) = 0;
  virtual const ScStringView *GetCaption() const = 0;
  virtual WnWindow *GetWindow() = 0;
  virtual bool GetButtonFlag(i32 mask) const = 0;
  virtual bool SetButtonFlag(i32 mask, bool enabled) = 0;
  virtual bool ToggleOn() = 0;
  virtual bool ToggleOff() = 0;
  virtual bool IsOn() const = 0;
  virtual bool SetChecked(bool enabled) = 0;
  virtual bool IsChecked() const = 0;
  virtual i32 GetStyle() = 0;
  virtual bool SetStyle(i32 mode, GxBuffer *image, const ScRect *rectangle) = 0;
  virtual i32 GetImageW() const = 0;
  virtual i32 GetImageH() const = 0;
  virtual bool GetImageArea(ScRect *rectangle) const = 0;
  virtual bool SetImage(i32 index, GxBuffer *image, const ScRect &rectangle) = 0;
  virtual bool GetImage(i32 index, GxBuffer **image, ScRect *rectangle) const = 0;
  virtual bool SetBuffer(GxBuffer *image) = 0;
  virtual bool GetBuffer(GxBuffer **image) const = 0;
  virtual bool SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(i32 index, u32 color) = 0;
  virtual bool ClearFontColor(i32 index) = 0;
  virtual bool SetCaptionStyle(i32 style) = 0;
  virtual bool GetCaptionStyle(i32 *style) const = 0;
  virtual bool SetGutters(u8 first, u8 second) = 0;
  virtual bool SetGutters(u8 first, u8 second, u8 third, u8 fourth) = 0;
  virtual bool GetGutters(i32 *first, i32 *second) const = 0;
  virtual bool GetGutters(i32 *first, i32 *second, i32 *third, i32 *fourth) const = 0;
  virtual bool Fit() = 0;
  virtual bool GetSizeLimits(ScRect *limits) = 0;
  virtual bool SetTipText(const ScStringView &text) = 0;
  virtual bool GetTipText(ScStringView *text) const = 0;
  virtual bool ClearTipText() = 0;
  virtual bool SetTipPlacementType(i32 type) = 0;
  virtual bool GetTipPlacementType(i32 *type) const = 0;
  virtual bool SetTipPlacementOffsets(i32 x, i32 y) = 0;
  virtual bool GetTipPlacementOffsets(i32 *x, i32 *y) const = 0;
  virtual bool SetSoundID(i32 index, u32 identifier) = 0;
  virtual u32 GetSoundID(i32 index) const = 0;
  virtual bool InstallImage(GxBuffer *image, const ScRect &rectangle, i32 layout, u8 unused) = 0;
  virtual bool Activate(u32 unused) = 0;
};

// The GZWIND image button owns its image/font references and both embedded strings.
class WnImageButton : public WnRootWidget, public WnButton {
public:
  WnImageButton();
  ~WnImageButton() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetID(u32 identifier) override;
  u32 GetID() const override;
  const ScStringView *GetCaption() const override;
  void SetCaption(const ScStringView &text) override;
  void SetKeyEquivalent(u32 key) override;
  WnWindow *GetWindow() override;
  bool GetButtonFlag(i32 mask) const override;
  bool SetButtonFlag(i32 mask, bool enabled) override;
  bool ToggleOn() override;
  bool ToggleOff() override;
  bool IsOn() const override;
  bool SetChecked(bool enabled) override;
  bool IsChecked() const override;
  i32 GetStyle() override;
  bool SetStyle(i32 mode, GxBuffer *image, const ScRect *rectangle) override;
  bool SetImage(i32 index, GxBuffer *image, const ScRect &rectangle) override;
  bool GetImage(i32 index, GxBuffer **image, ScRect *rectangle) const override;
  i32 GetImageW() const override;
  i32 GetImageH() const override;
  bool GetImageArea(ScRect *rectangle) const override;
  bool SetBuffer(GxBuffer *image) override;
  bool GetBuffer(GxBuffer **image) const override;
  bool SetFont(FtFont *font) override;
  bool SetFontColor(i32 index, u32 color) override;
  bool ClearFontColor(i32 index) override;
  bool SetCaptionStyle(i32 style) override;
  bool GetCaptionStyle(i32 *style) const override;
  bool SetGutters(u8 first, u8 second, u8 third, u8 fourth) override;
  bool SetGutters(u8 first, u8 second) override;
  bool GetGutters(i32 *first, i32 *second, i32 *third, i32 *fourth) const override;
  bool GetGutters(i32 *first, i32 *second) const override;
  bool Fit() override;
  bool GetSizeLimits(ScRect *limits) override;
  bool InstallImage(GxBuffer *image, const ScRect &rectangle, i32 layout, u8 unused) override;
  bool Activate(u32 unused) override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u32 SetFlag(u32 mask, bool enabled) override;
  bool GZPaint() override;
  // Primary488 GetGZClassName remains absent until its bool-versus-byte result is established.
  // Layout therefore remains displaced from the original primary492 slot.
  virtual bool Layout(i32 width, i32 height);
  bool SetTipText(const ScStringView &text) override;
  bool GetTipText(ScStringView *text) const override;
  bool ClearTipText() override;
  bool SetTipPlacementType(i32 type) override;
  bool GetTipPlacementType(i32 *type) const override;
  bool SetTipPlacementOffsets(i32 x, i32 y) override;
  bool GetTipPlacementOffsets(i32 *x, i32 *y) const override;
  bool SetSoundID(i32 index, u32 identifier) override;
  u32 GetSoundID(i32 index) const override;
  bool StartHint();
  bool CancelHint();

protected:
  u32 button_flags;
  ScString control_text;
  i16 text_x, text_y, text_width, text_height;
  i32 alignment;
  u16 text_alignment;
  u8 gutters[4];
  FtFont *font;
  u32 text_colors[6], sound_ids[1];
  u32 image_mode;
  i32 image_layout;
  ScRect image_area;
  i32 selected_image_x, image_x, image_y;
  GxBuffer *image;
  ScString hint_text;
  u32 hint_token, hint_flags;
  i16 hint_x, hint_y;

  void MeasureText();
  void PlaceText(const ScRect &rectangle);
  bool LayoutCurrent();
  bool PaintImage();
  bool PaintFallback();
  void PaintText();
  void Notify(u32 code, uptr second, uptr third);
  void PlaySound(u32 index);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
};

class ScCom;

// Original 22-slot label interface; overload declaration order preserves the installed MSVC table.
// Label message forwarding, wrapping, background fill and font-color selection bits.
enum WnLabelFlag : i32 {
  kWnLabelForwardMessages = 0x00000001,
  kWnLabelWrapText = 0x00000002,
  kWnLabelFillBackground = 0x00000004,
  kWnLabelUseFontColors = 0x00010000,
};

// Label horizontal/vertical placement, decoded by SetAlignment and Layout.
enum WnLabelAlignment : u32 {
  kWnLabelLeftTop = 0x00000000,
  kWnLabelLeftMiddle = 0x00000001,
  kWnLabelLeftBottom = 0x00000002,
  kWnLabelCenterTop = 0x00000003,
  kWnLabelCenterMiddle = 0x00000004,
  kWnLabelCenterBottom = 0x00000005,
  kWnLabelRightTop = 0x00000006,
  kWnLabelRightMiddle = 0x00000007,
  kWnLabelRightBottom = 0x00000008,
};

class WnLabelControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual void SetCaption(const ScStringView &text) = 0;
  virtual bool Fit() = 0;
  virtual bool GetLabelFlag(i32 mask) = 0;
  virtual bool SetLabelFlag(i32 mask, bool enabled) = 0;
  virtual bool SetTextColor(u32 color) = 0;
  virtual bool SetTextColor(i32 index, u32 color) = 0;
  virtual bool GetTextColor(u32 *color) = 0;
  virtual bool GetTextColor(i32 index, u32 *color) const = 0;
  virtual bool ClearTextColor() = 0;
  virtual bool ClearTextColor(i32 index) = 0;
  virtual bool SetBackground(bool enabled) = 0;
  virtual bool IsBackgroundOpaque() = 0;
  virtual bool SetFont(FtFont *font) = 0;
  virtual bool SetAlignment(u32 alignment) = 0;
  virtual bool SetGutters(i8 horizontal, i8 vertical) = 0;
  virtual bool GetGutters(i8 *horizontal, i8 *vertical) = 0;
  virtual bool SetTextOffsets(i8 horizontal, i8 vertical) = 0;
  virtual bool GetTextOffsets(i8 *horizontal, i8 *vertical) = 0;
};

// Actual GZWIND216-byte source label. Unused source word196 is not read or modeled as initialized metadata.
class WnLabel : public WnRootWidget, public WnLabelControl {
public:
  WnLabel();
  ~WnLabel() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool DoMessage(ScMessage *message) override;
  void SetFillColor(u32 color) override;
  using WnRootWidget::SetFillColor;
  bool Init() override;
  bool Shutdown() override;
  void SetCaption(const ScStringView &text) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  bool Fit() override;
  bool GetLabelFlag(i32 mask) override;
  bool SetLabelFlag(i32 mask, bool enabled) override;
  bool SetTextColor(i32 index, u32 color) override;
  bool SetTextColor(u32 color) override;
  bool GetTextColor(u32 *color) override;
  bool GetTextColor(i32 index, u32 *color) const override;
  bool ClearTextColor() override;
  bool ClearTextColor(i32 index) override;
  bool SetBackground(bool enabled) override;
  bool IsBackgroundOpaque() override;
  bool SetFont(FtFont *font) override;
  bool SetAlignment(u32 alignment) override;
  bool SetGutters(i8 horizontal, i8 vertical) override;
  bool GetGutters(i8 *horizontal, i8 *vertical) override;
  bool SetTextOffsets(i8 horizontal, i8 vertical) override;
  bool GetTextOffsets(i8 *horizontal, i8 *vertical) override;

protected:
  FtFont *font;
  i8 padding[4];
  ScRect text_area;
  u32 label_flags;
  u8 unclassified196[4];
  u32 alignment, text_colors[2], text_alignment;
  void GetTextExtent(ScRect *out);
  void Layout();
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
};

ScUnknown *WnConstructLabel();

// Low bits select the outline edge treatment; polygon and fill independently control the painted shape.
// Multiple edge bits retain the original inset-before-outset precedence rather than forming a separate mode.
enum WnOutlineMode : u32 {
  kWnOutlinePlain = 1,
  kWnOutlineInset = 2,
  kWnOutlineOutset = 4,
  kWnOutlineStrokeMask = kWnOutlinePlain | kWnOutlineInset | kWnOutlineOutset,
  kWnOutlinePolygon = 0x10,
  kWnOutlineFill = 0x20,
};

// Original 15-slot E303E5AE outline interface; its color getters are separate from the root's const methods.
class WnOutlineControl : public ScUnknown {
public:
  virtual void SetFlags(u32 mode) = 0;
  virtual u32 GetFlags() = 0;
  virtual void SetFillColor(u32 pixel) = 0;
  virtual void SetDarkOutlineColor(u32 pixel) = 0;
  virtual void SetLightOutlineColor(u32 pixel) = 0;
  virtual u32 GetFillColor() = 0;
  virtual u32 GetDarkOutlineColor() = 0;
  virtual u32 GetLightOutlineColor() = 0;
  virtual void SetColor(u8 index, u8 red, u8 green, u8 blue) = 0;
  virtual void GetColor(u8 index, u8 *red, u8 *green, u8 *blue) = 0;
  virtual bool SetShapePoly(i32 count, ScPoint *points) = 0;
  virtual WnWindow *GetWindow() = 0;
};

// Class4303E5B6 has its control before the root and owns a rotated point array plus two logical edge colors.
class WnOutline : public WnOutlineControl, public WnRootWidget {
public:
  WnOutline();
  ~WnOutline() override;
  bool QueryInterface(u32 iid, void **out) override;
  bool DoMessage(ScMessage *message) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  void SetFlags(u32 mode) override;
  u32 GetFlags() override;
  void SetFillColor(u32 pixel) override;
  using WnRootWidget::SetFillColor;
  void SetDarkOutlineColor(u32 pixel) override;
  void SetLightOutlineColor(u32 pixel) override;
  u32 GetFillColor() override;
  using WnRootWidget::GetFillColor;
  u32 GetDarkOutlineColor() override;
  u32 GetLightOutlineColor() override;
  void SetColor(u8 index, u8 red, u8 green, u8 blue) override;
  void GetColor(u8 index, u8 *red, u8 *green, u8 *blue) override;
  bool SetShapePoly(i32 count, ScPoint *points) override;
  bool GZPaint() override;

protected:
  u32 mode, first_color, second_color;
  i32 point_count;
  ScPoint *points;
};

enum class WnSliderDirection : i32 { Null = 0, Horizontal = 1, Vertical = 2 };

// Actual slider secondary interface 21325207. Host vtable offsets follow the rebuilt C++ declarations.
class WnSliderControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual i32 GetValue() const = 0;
  virtual bool SetValue(i32 value) = 0;
  virtual void SetMinimum(i32 value) = 0;
  virtual void SetMaximum(i32 value) = 0;
  virtual void SetDirection(WnSliderDirection orientation) = 0;
  virtual bool SetImage(GxBuffer *image, const ScRect *rectangle) = 0;
};

// The concrete base slider ends its primary virtual table after the two thumb dimensions; scrollbar actions are derived-only.
class WnSlider : public WnRootWidget, public WnSliderControl {
public:
  WnSlider();
  ~WnSlider() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::SetArea;
  i32 GetValue() const override;
  bool SetValue(i32 value) override;
  void SetMinimum(i32 value) override;
  void SetMaximum(i32 value) override;
  void SetDirection(WnSliderDirection value) override;
  bool SetImage(GxBuffer *image, const ScRect *rectangle) override;
  bool GZPaint() override;

protected:
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  i32 orientation, minimum, maximum, value, track_start, thumb_position, track_finish, inset;
  ScRect thumb_area;
  bool setting_value;
  GxBuffer *image;
  ScRect image_area;

  virtual bool ValueFromPoint(i32 *out, i32 x, i32 y);
  virtual void UpdateThumb();
  virtual void Layout();
  virtual i32 GetThumbWidth();
  virtual i32 GetThumbHeight();
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool PaintPlain();
  bool PaintImage();
};

// Reached scrollbar interface 61325A2D, distinct from the inherited slider view.
class WnScrollbarControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual WnSliderControl *GetSlider() = 0;
  virtual bool SetValue(i32 value) = 0;
  virtual i32 GetValue() const = 0;
  virtual bool SetRange(i32 minimum, i32 maximum) = 0;
  virtual bool GetRange(i32 *minimum, i32 *maximum) const = 0;
  virtual bool SetOrientation(i32 orientation) = 0;
  virtual i32 GetOrientation() const = 0;
  virtual bool SetPageCount(i32 count) = 0;
  virtual i32 GetPageCount() const = 0;
  virtual bool SetLineCount(i32 count) = 0;
  virtual i32 GetLineCount() const = 0;
  virtual void SetDivisors(u32 line, u32 page) = 0;
  virtual bool ImageSetup(const ScRect &area, GxBuffer *image, i32 unknown) = 0;
  virtual bool GetExtent(ScRect *out) const = 0;
};

class WnScrollbar : public WnSlider, public WnScrollbarControl {
public:
  WnScrollbar();
  ~WnScrollbar() override;
  bool Init() override;
  bool Shutdown() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  WnSliderControl *GetSlider() override;
  bool SetValue(i32 value) override;
  i32 GetValue() const override;
  bool SetRange(i32 minimum, i32 maximum) override;
  bool GetRange(i32 *minimum, i32 *maximum) const override;
  bool SetOrientation(i32 value) override;
  i32 GetOrientation() const override;
  bool SetPageCount(i32 count) override;
  i32 GetPageCount() const override;
  bool SetLineCount(i32 count) override;
  i32 GetLineCount() const override;
  void SetDivisors(u32 line, u32 page) override;
  bool ImageSetup(const ScRect &area, GxBuffer *image, i32 unknown) override;
  bool GetExtent(ScRect *out) const override;
  bool SetImage(GxBuffer *image, const ScRect *rectangle) override;
  bool GZPaint() override;

protected:
  u32 line_divisor, page_divisor;
  ScStopwatch repeat_timer;
  i32 repeat_part;
  bool repeat_enabled;
  void Layout() override;
  virtual void LineDown();
  virtual void LineUp();
  virtual void PageDown();
  virtual void PageUp();
  virtual bool HitPart(i32 *out, i32 x, i32 y);
  virtual i32 GetPrevNextButtonWidth();
  virtual i32 GetPrevNextButtonHeight();
  // Original primary536..548 contain four line/page conversions with unresolved nominal return declarations.
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool PaintPlain();
  bool PaintImage();
};

ScUnknown *WnConstructScrollbar();
ScUnknown *WnConstructSlider();

// Installed612CE0C3 interface. Its smaller method set and full-width flag predicate differ from the later SDK.
class WnSpinnerControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual bool SetSpinnerType(i32 type) = 0;
  virtual bool GetSpinnerType(i32 *out) const = 0;
  virtual bool SetSpinnerFlag(i32 mask, bool enabled) = 0;
  virtual int GetSpinnerFlag(i32 mask) const = 0;
  virtual i32 GetValue() = 0;
  virtual bool SetValue(i32 value) = 0;
  virtual bool SetMinMax(i32 minimum, i32 maximum) = 0;
  virtual bool GetMinMax(i32 *minimum, i32 *maximum) = 0;
  virtual bool SetStepSize(i32 step) = 0;
  virtual bool GetStepSize(i32 *out) = 0;
  virtual bool SetImage(GxBuffer *image, const ScRect *area) = 0;
  virtual bool SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(i32 index, u32 color) = 0;
  virtual bool ResetFontColor(i32 index) = 0;
  virtual bool SetTextAlignment(i32 alignment) = 0;
  virtual bool GetTextAlignment(i32 *out) const = 0;
  virtual bool SetGutters(u8 horizontal, u8 vertical) = 0;
  virtual bool GetGutters(u8 *horizontal, u8 *vertical) const = 0;
  virtual bool SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) = 0;
  virtual bool GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) = 0;
};

// Actual spinner combines a root window with its secondary control, retained image/font and shared repeat timer.
class WnSpinner : public WnRootWidget, public WnSpinnerControl {
public:
  WnSpinner();
  ~WnSpinner() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::SetArea;
  bool GZPaint() override;
  bool SetSpinnerType(i32 type) override;
  bool GetSpinnerType(i32 *out) const override;
  bool SetSpinnerFlag(i32 mask, bool enabled) override;
  int GetSpinnerFlag(i32 mask) const override;
  i32 GetValue() override;
  bool SetValue(i32 value) override;
  bool SetMinMax(i32 minimum, i32 maximum) override;
  bool GetMinMax(i32 *minimum, i32 *maximum) override;
  bool SetStepSize(i32 step) override;
  bool GetStepSize(i32 *out) override;
  bool SetImage(GxBuffer *image, const ScRect *area) override;
  bool SetFont(FtFont *font) override;
  bool SetFontColor(i32 index, u32 color) override;
  bool ResetFontColor(i32 index) override;
  bool SetTextAlignment(i32 alignment) override;
  bool GetTextAlignment(i32 *out) const override;
  bool SetGutters(u8 horizontal, u8 vertical) override;
  bool GetGutters(u8 *horizontal, u8 *vertical) const override;
  bool SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) override;
  bool GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) override;

protected:
  void PaintImageEffects(i32 offset);
  void PaintFallbackEffects();
  bool RefreshCaption();
  void RequeryAreaEffects();
  bool PartFromPoint(i32 *out, i32 x, i32 y);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  u32 spinner_flags;
  i32 spinner_type;
  ScStopwatch timer;
  u32 repeat_delay;
  i32 pressed_part, hover_part, minimum, maximum, value, step;
  FtFont *font;
  i16 font_height;
  u8 alignment;
  u32 text_colors[3];
  GxBuffer *image;
  u16 source_x, source_y;
  i32 cell_width, cell_height;
  u8 horizontal_gutter, vertical_gutter;
  u16 outline_colors[4], maximum_button_width, minimum_button_extent;
  // Original bytes310..319 are unclassified and unwritten by construction; keep their storage without reading them.
  u8 unclassified_tail[10];
};

// Original 27-slot A1336CC0 interface. Later SDK font/outline getters are absent; both axis-layout entries return false.
class WnOptionControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual int GetOptionFlag(u32 mask) const = 0;
  virtual bool SetOptionFlag(u32 mask, u8 enabled) = 0;
  virtual bool SetGutters(u8 horizontal, u8 vertical) = 0;
  virtual bool GetGutters(i32 *horizontal, i32 *vertical) const = 0;
  virtual bool SetLabelFont(FtFont *font) = 0;
  virtual bool SetButtonFont(FtFont *font) = 0;
  virtual bool SetFontColor(i32 index, u32 color) = 0;
  virtual bool ClearFontColor(i32 index) = 0;
  virtual u32 GetValue() const = 0;
  virtual bool SetValue(u32 identifier) = 0;
  virtual bool AddButton(
      u32 identifier, const ScStringView &caption, u32 key, GxBuffer *image, const ScRect *rectangle) = 0;
  virtual bool DeleteButton(u32 identifier) = 0;
  virtual bool MoveButton(u32 identifier, i32 x, i32 y) = 0;
  virtual bool SizeButton(u32 identifier, i32 width, i32 height) = 0;
  virtual bool GetButton(u32 identifier, WnButton **out) = 0;
  virtual bool SetBackground(GxBuffer *image) = 0;
  virtual bool SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) = 0;
  virtual bool SetButtonClass(u32 identifier) = 0;
  virtual bool AutoSize() = 0;
  virtual bool LayoutW(i32 width) = 0;
  virtual bool LayoutH(i32 height) = 0;
  virtual bool GetMinMaxInfo(ScRect *limits) = 0;
  virtual void SetAutoFillType(i32 type, i32 limit) = 0;
};

// Option group owns its two font references and backdrop while the root owns the actual label/button children.
class WnOptionGroup : public WnRootWidget, public WnOptionControl {
public:
  WnOptionGroup();
  ~WnOptionGroup() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  int GetOptionFlag(u32 mask) const override;
  bool SetOptionFlag(u32 mask, u8 enabled) override;
  bool SetGutters(u8 horizontal, u8 vertical) override;
  bool GetGutters(i32 *horizontal, i32 *vertical) const override;
  bool SetLabelFont(FtFont *font) override;
  bool SetButtonFont(FtFont *font) override;
  bool SetFontColor(i32 index, u32 color) override;
  bool ClearFontColor(i32 index) override;
  u32 GetValue() const override;
  bool SetValue(u32 identifier) override;
  bool AddButton(
      u32 identifier, const ScStringView &caption, u32 key, GxBuffer *image, const ScRect *rectangle) override;
  bool DeleteButton(u32 identifier) override;
  bool MoveButton(u32 identifier, i32 x, i32 y) override;
  bool SizeButton(u32 identifier, i32 width, i32 height) override;
  bool GetButton(u32 identifier, WnButton **out) override;
  bool SetBackground(GxBuffer *image) override;
  bool SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) override;
  bool SetButtonClass(u32 identifier) override;
  bool AutoSize() override;
  bool LayoutW(i32 width) override;
  bool LayoutH(i32 height) override;
  bool GetMinMaxInfo(ScRect *limits) override;
  void SetAutoFillType(i32 type, i32 limit) override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u32 SetFlag(u32 mask, bool enabled) override;
  void SetCaption(const ScStringView &text) override;
  bool GZPaint() override;

protected:
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  u32 option_flags, button_class;
  i32 autofill_mode, autofill_limit;
  u8 horizontal_gutter, vertical_gutter;
  u16 outline_colors[4];
  u32 value;
  GxBuffer *background;
  FtFont *label_font, *button_font;
  u32 font_colors[5];
  bool OnCommand(u32 kind, u32 identifier) override;
  static bool EnumFuncBtnFont(WnWindow *parent, u32 id, void *view, void *context);
  static bool EnumFuncBtnColors(WnWindow *parent, u32 id, void *view, void *context);
  static bool EnumFuncEnable(WnWindow *parent, u32 id, void *view, void *context);
  bool EnsureLabel();
  bool EnsureLabelFont();
  bool EnsureButtonFont();
  void RefreshLabelColors();
  void RefreshButtonColors();
  void ApplyButtonColors();
  void LayoutEffects(i32 width, i32 height);
};

#endif
