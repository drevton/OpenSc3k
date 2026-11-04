#ifndef SC3_APPLICATION_WINDOW_CONTROLS_H
#define SC3_APPLICATION_WINDOW_CONTROLS_H

#include <map>
#include <vector>

#include "application/api.h"
#include "common/sc3_callback_timer.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "ui/ui_window.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"

class WnTextControl;
class FtFont;

// SC3U CodeView and the executable secondary table establish the Boolean and font-interface contracts.
// Original combo interface412CE496: all32 installed slots through the extent query.
// Overload declarations preserve the installed MSVC order within each group.
class WnAppComboControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual WnTextControl *GetLineInput() = 0;
  virtual void SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(i32 index, u32 color) = 0;
  virtual u32 GetFontColor(i32 index) const = 0;
  virtual bool ClearFontColor(i32 index) = 0;
  virtual bool SetHighlightColor(u32 color) = 0;
  virtual bool SetHighlightFillAlpha(f32 opacity) = 0;
  virtual bool GetEditable() const = 0;
  virtual bool SetEditable(bool enabled) = 0;
  virtual bool SetDropDownOutlineColor(u32 color) = 0;
  virtual bool GetDropDownOutlineColor(u32 *color) const = 0;
  virtual bool RemoveString(const ScStringView &text) = 0;
  virtual bool RemoveString(i32 index) = 0;
  virtual void RemoveAllStrings() = 0;
  virtual bool InsertString(const ScStringView &text, i32 index) = 0;
  virtual bool SetSelection(i32 index, bool notify) = 0;
  virtual i32 GetSelection() const = 0;
  virtual bool GetSelection(i32 *index, ScStringView *text) const = 0;
  virtual bool GetStringFromIndex(i32 index, ScStringView *text) const = 0;
  virtual bool GetIndexFromString(i32 *index, const ScStringView &text) const = 0;
  virtual bool SetComboDownArrow(GxBuffer *image, const ScRect *area) = 0;
  virtual bool SetComboDownColor(u32 color) = 0;
  virtual bool GetComboDownColor(u32 *color) const = 0;
  virtual bool SetBtnGutter(i8 gutter) = 0;
  virtual bool GetBtnGutter(i8 *gutter) const = 0;
  virtual bool SetTextGutters(i8 horizontal, i8 vertical) = 0;
  virtual bool GetTextGutters(i8 *horizontal, i8 *vertical) const = 0;
  virtual bool GetMinMaxInfo(ScRect *limits) = 0;
};

class WnAppCombo : public UiWindow, public WnAppComboControl {
public:
  explicit WnAppCombo(bool editable);
  ~WnAppCombo() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  WnTextControl *GetLineInput() override;
  void SetFont(FtFont *font) override;
  bool SetFontColor(i32 index, u32 color) override;
  u32 GetFontColor(i32 index) const override;
  bool ClearFontColor(i32 index) override;
  bool SetHighlightColor(u32 color) override;
  bool SetHighlightFillAlpha(f32 opacity) override;
  bool GetEditable() const override;
  bool SetEditable(bool enabled) override;
  bool SetDropDownOutlineColor(u32 color) override;
  bool GetDropDownOutlineColor(u32 *color) const override;
  bool RemoveString(const ScStringView &text) override;
  bool RemoveString(i32 index) override;
  void RemoveAllStrings() override;
  bool InsertString(const ScStringView &text, i32 index) override;
  bool GetSelection(i32 *index, ScStringView *text) const override;
  bool GetStringFromIndex(i32 index, ScStringView *text) const override;
  bool GetIndexFromString(i32 *index, const ScStringView &text) const override;
  bool SetSelection(i32 index, bool notify) override;
  i32 GetSelection() const override;
  bool SetComboDownArrow(GxBuffer *image, const ScRect *area) override;
  bool SetComboDownColor(u32 color) override;
  bool GetComboDownColor(u32 *color) const override;
  bool SetBtnGutter(i8 gutter) override;
  bool GetBtnGutter(i8 *gutter) const override;
  bool SetTextGutters(i8 horizontal, i8 vertical) override;
  bool GetTextGutters(i8 *horizontal, i8 *vertical) const override;
  bool GetMinMaxInfo(ScRect *limits) override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using UiWindow::SetArea;
  u32 SetFlag(u32 mask, bool enabled) override;
  void SetFillColor(u32 color) override;
  using UiWindow::SetFillColor;
  bool GZPaint() override;

protected:
  virtual WnTextControl *LookupLineInput();
  virtual bool CreateLineInput();
  virtual bool LookupString(i32 index, const ScString **out) const;
  virtual bool Layout(i32 width, i32 height);
  virtual bool OpenDropDown();
  virtual bool NotifyChanged();
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 identifier) override;
  WnListStrings strings;
  i32 selection;
  u8 drop_open;
  FtFont *font;
  u32 font_colors[3], highlight_color;
  f32 highlight_opacity;
  u32 dropdown_outline;
  bool editable;
  GxBuffer *arrow;
  i32 arrow_x, arrow_y, arrow_width, arrow_height, arrow_left, arrow_top, arrow_source_x;
  u32 arrow_color;
  i8 button_gutter, horizontal_gutter, vertical_gutter;
};

// Executable cSC3WinScrollBar has a root window, scrollbar and slider interfaces at0/164/168 on x86.
// This factory product owns its original window and control interfaces.
class ApScrollBar : public WnRootWidget, public WnScrollbarControl, public WnSliderControl {
public:
  ApScrollBar();
  ~ApScrollBar() override;
  static ScUnknown *CreateNewObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  WnWindow *GetWindow() override;
  WnSliderControl *GetSlider() override;
  i32 GetValue() const override;
  bool SetValue(i32 value) override;
  bool SetRange(i32 minimum, i32 maximum) override;
  bool GetRange(i32 *minimum, i32 *maximum) const override;
  bool SetOrientation(i32 orientation) override;
  i32 GetOrientation() const override;
  bool SetPageCount(i32 count) override;
  i32 GetPageCount() const override;
  bool SetLineCount(i32 count) override;
  i32 GetLineCount() const override;
  void SetDivisors(u32 line, u32 page) override;
  bool ImageSetup(const ScRect &area, GxBuffer *image, i32 mode) override;
  bool GetExtent(ScRect *out) const override;
  void SetMinimum(i32 value) override;
  void SetMaximum(i32 value) override;
  void SetDirection(WnSliderDirection direction) override;
  bool SetImage(GxBuffer *image, const ScRect *rectangle) override;

protected:
  bool DrawHorizontalImage(i32 x) const;
  bool DrawVerticalImage(i32 x) const;
  bool DrawHorizontalFlat() const;
  bool DrawVerticalFlat() const;
  bool UpdateMouseInfo(i32 x, i32 y);
  bool SetThumbPosition(i32 x, i32 y);
  bool DoScrollVertical(u32 command);
  bool DoScrollHorizontal(u32 command);
  bool PostScrollMessageToSelfMouse();
  void ProcessTimerCallback();
  static void TimerCallback(uptr receiver);
  bool CalcValueFromThumbPosition();
  bool CalcThumbPositionFromValue();
  u32 scroll_flags;
  i8 orientation, mouse_offset, pressed_part, hover_part, image_width, image_height, image_mode;
  GxBuffer *image;
  i32 image_left, image_top, thumb_position, minimum, maximum, value, page_size, line_size;
  ScCallbackTimer timer;
};

static_assert(sizeof(uptr) != 4 || sizeof(ScCallbackTimer) == 32);
static_assert(sizeof(uptr) != 4 || sizeof(ApScrollBar) == 252);

class WnWindow;
class ScStringView;

// Executable 82FA8FD9 interface with the original font and string contracts.
// Its33 installed slots end at RemoveAllStringPairs128; later operations belong to each concrete class.
class WnAppInfoListControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool InsertStringPair(const ScStringView &first, const ScStringView &second, i32 index) = 0;
  virtual bool InsertStringPair(
      const ScStringView &first, const ScStringView &second, u32 first_color, u32 second_color, i32 index) = 0;
  virtual void SetJustification(bool value) = 0;
  virtual void SetOpaque(bool value) = 0;
  virtual void SetRightColumnTextStart(i32 width) = 0;
  virtual bool ResizeWindowForExactLineHeights() = 0;
  virtual void SetFonts(FtFont *first, FtFont *second) = 0;
  virtual bool SetFontColor(i32 index, u32 pixel) = 0;
  virtual bool GetFontColor(i32 index, u32 *pixel) = 0;
  virtual bool ClearFontColor(i32 index) = 0;
  virtual void SetHighlightColor(u32 pixel) = 0;
  virtual void SetHighlightFillAlpha(f32 opacity) = 0;
  virtual bool MoveLineUp() = 0;
  virtual bool MoveLineDown() = 0;
  virtual bool ScrollToStart() = 0;
  virtual bool ScrollToEnd() = 0;
  virtual bool ScrollPageUp() = 0;
  virtual bool ScrollPageDown() = 0;
  virtual bool IsIndexInView(i32 index) = 0;
  virtual void PutIndexInView(i32 index) = 0;
  virtual void SetIndexOfTopLine(i32 index) = 0;
  virtual bool EnableScrollBar(bool first, bool second) = 0;
  virtual i32 GetLineCount() const = 0;
  virtual i32 GetIndexOfTopLine() const = 0;
  virtual void SetOutlineDrawing(bool value) = 0;
  virtual void SetDrawSunkenRect(bool value) = 0;
  virtual void SetSunkenRectColors(u32 first, u32 second) = 0;
  virtual bool RemoveStringPair(i32 index) = 0;
  virtual bool RemoveAllStringPairs() = 0;
};

// Original cStringPair owns two strings and their color and tag words.
struct WnInfoPair {
  WnInfoPair();
  WnInfoPair(const WnInfoPair &other);
  ~WnInfoPair();
  ScString first, second;
  u32 colors[2], tag;
};

// Actual264-byte owner: primary list, inherited root window at4, two circular row lists and two retained fonts.
// The executable control remains abstract while its remaining methods are recovered.
class WnAppInfoList : public WnAppInfoListControl, public UiWindow {
public:
  WnAppInfoList();
  ~WnAppInfoList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  using UiWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u32 SetFlag(u32 mask, bool enabled) override;
  virtual bool GZCharacter(u32 character);
  virtual bool GetStringPair(ScStringView *first, ScStringView *second, i32 index);
  virtual bool ModifyString(const ScStringView &text, i32 index, bool first);
  virtual bool ModifyStringColor(u32 pixel, i32 index, bool first);
  virtual bool SetSelection(i32 index, bool notify);
  virtual i32 GetSelection() const;
  virtual u32 GetItemIndexFromCursorPosition(i32 x, i32 y, bool clamp) const;

protected:
  bool OnKeyUp(u32 key, u32 modifiers) override;
  virtual bool AddScrollbar(bool enabled, bool horizontal);
  virtual void RemoveScrollbar(bool enabled, bool horizontal);
  virtual void CalculateAllMetrics();
  virtual bool InsertStringPairAtIndex(
      i32 index, const ScString &first, const ScString &second, u32 first_color, u32 second_color);
  virtual void SetScrollbarValue();
  virtual void SetScrollbarSizes();

public:
  bool InsertStringPair(
      const ScStringView &first, const ScStringView &second, u32 first_color, u32 second_color, i32 index) override;
  bool InsertStringPair(const ScStringView &first, const ScStringView &second, i32 index) override;
  bool SetFontColor(i32 index, u32 pixel) override;
  bool GetFontColor(i32 index, u32 *pixel) override;
  bool ClearFontColor(i32 index) override;
  bool RemoveStringPair(i32 index) override;
  bool RemoveAllStringPairs() override;
  bool IsIndexInView(i32 index) override;
  i32 GetLineCount() const override;
  i32 GetIndexOfTopLine() const override;
  WnScrollbarControl *GetVerticalScrollbar();
  WnScrollbarControl *GetHorizontalScrollbar();
  void SetHighlightColor(u32 pixel) override;
  void SetFonts(FtFont *first, FtFont *second) override;
  void SetHighlightFillAlpha(f32 opacity) override;
  void SetRightColumnTextStart(i32 width) override;
  void SetJustification(bool value) override;
  void SetOpaque(bool value) override;
  void SetOutlineDrawing(bool value) override;
  void SetDrawSunkenRect(bool value) override;
  void SetSunkenRectColors(u32 first, u32 second) override;
  bool EnableScrollBar(bool first, bool second) override;
  bool ResizeWindowForExactLineHeights() override;
  bool MoveLineUp() override;
  bool MoveLineDown() override;
  bool ScrollToStart() override;
  bool ScrollToEnd() override;
  bool ScrollPageUp() override;
  bool ScrollPageDown() override;
  void PutIndexInView(i32 index) override;
  void SetIndexOfTopLine(i32 index) override;

protected:
  struct WnTextRow {
    WnTextRow *next, *previous;
    WnInfoPair pair;
  };

  static_assert(sizeof(uptr) != 4 || sizeof(WnTextRow) == 60);

  u32 language;
  f32 background_opacity;
  u32 background_color, text_colors[3], border_colors[2];
  WnTextRow *rows, *wrapped_rows;
  FtFont *fonts[2];
  i32 selection, visible_rows, first_visible, row_height, first_width;
  u8 alternate_alignment, fill_background, rectangle_outline, line_outline;
  u32 outline_colors[2];
  i8 gutters[4], gap;
  i32 last_click;
  u8 double_click, allow_scrollbar, state262;

  WnTextRow *AppendWrappedRow(const ScString &first, const ScString &second, u32 first_color, u32 second_color);
  bool SendSelectionChangedMessage();
  bool SendDoubleClickMessage();
  WnTextRow *FindRow(i32 index);
  static void ClearRows(WnTextRow *head);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpR(i32 x, i32 y, u32 modifiers) override;
};

static_assert(sizeof(uptr) != 4 || sizeof(WnAppInfoList) == 264);

// The executable owns this copy of cGZWinText and obtains services from its application window base.
class WnAppLabel : public UiWindow, public WnLabelControl {
public:
  WnAppLabel();
  ~WnAppLabel() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool DoMessage(ScMessage *message) override;
  void SetFillColor(u32 color) override;
  using UiWindow::SetFillColor;
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

class FtFont;

// Executable cIGZWinListBox uses the font interface and the original secondary virtual order.
class WnAppListControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool RemoveString(const ScStringView &text) = 0;
  virtual bool RemoveString(i32 index) = 0;
  virtual void RemoveAllStrings() = 0;
  virtual bool InsertString(const ScStringView &text, i32 index) = 0;
  virtual bool ModifyString(const ScStringView &text, i32 index) = 0;
  virtual bool ResizeWindowForExactLineHeights() = 0;
  virtual bool SetSelection(i32 index, bool notify) = 0;
  virtual i32 GetSelection() const = 0;
  virtual void SetShouldSort() = 0;
  virtual void Sort() = 0;
  virtual void SetDrop(bool enabled) = 0;
  virtual void GetTextArea(ScRect *rectangle) const = 0;
  virtual bool MoveLineUp() = 0;
  virtual bool MoveLineDown() = 0;
  virtual bool ScrollToStart() = 0;
  virtual bool ScrollToEnd() = 0;
  virtual bool ScrollPageUp() = 0;
  virtual bool ScrollPageDown() = 0;
  virtual bool IsIndexInView(i32 index) const = 0;
  virtual void PutIndexInView(i32 index) = 0;
  virtual bool ScrollToFirstLetter(char character) = 0;
  virtual void SetIndexOfTopLine(i32 index) = 0;
  virtual i32 GetLineCount() const = 0;
  virtual i32 GetLineHeight() const = 0;
  virtual i32 GetVisibleLines() const = 0;
  virtual i32 GetIndexOfTopLine() const = 0;
  virtual u32 GetItemIndexFromCursorPosition(i32 x, i32 y, bool clamp) const = 0;
  virtual bool EnableScrollBar(bool first, bool second) = 0;
  virtual void SetLineItemBackgroundBuffer(GxBuffer *image) = 0;
  virtual void SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(i32 index, u32 color) = 0;
  virtual bool GetFontColor(i32 index, u32 *color) = 0;
  virtual bool ClearFontColor(i32 index) = 0;
  virtual void SetHighlightColor(u32 color) = 0;
  virtual void SetHighlightFillAlpha(f32 opacity) = 0;
  virtual bool ResizeBoxToFitContents() = 0;
  virtual bool ResizeBoxToFitContentsHorizontal() = 0;
  virtual bool GetStringFromIndex(i32 index, ScStringView *text) const = 0;
  virtual bool GetIndexFromString(i32 *index, const ScStringView &text) const = 0;
  virtual void SetOutlineDrawing(bool enabled) = 0;
  virtual void SetDrawSunkenRect(bool enabled) = 0;
  virtual void SetOutlineColors(u32 first, u32 second) = 0;
  virtual bool SetScrollbarGutters(i8 horizontal, i8 vertical) = 0;
  virtual bool SetTextGutters(i8 horizontal, i8 vertical) = 0;
  virtual bool SetMinimumLineHeight(u8 height) = 0;
};

// The remaining executable list methods stay abstract until their original bodies are recovered.
class WnAppList : public UiWindow, public WnAppListControl {
public:
  explicit WnAppList(bool popup);
  ~WnAppList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool RemoveString(const ScString &text);
  virtual bool InsertString(const ScString &text, i32 index);
  virtual bool GZCharacter(u32 character);
  virtual void SetPopupBackgroundBuffer(GxBuffer *image);
  virtual bool GetStringFromIndex(i32 index, ScString *text) const;
  virtual bool GetIndexFromString(i32 *index, const ScString &text);

protected:
  virtual void CalculateAllMetrics();
  virtual bool GetStringAddressFromIndex(i32 index, const ScString **text) const;
  virtual bool RemoveStringAtIndex(i32 index);
  virtual bool InsertStringAtIndex(i32 index, const ScString &text);
  virtual void SetScrollBarValue();
  virtual void SetScrollBarSizes();
  virtual bool AddScrollBar(bool vertical, bool horizontal);
  virtual void RemoveScrollBar(bool vertical, bool horizontal);
  virtual bool SendSelectionChangedMessage();
  virtual bool SendDoubleClickMessage();
  virtual bool EndDialog(u32 result);

public:
  WnWindow *AsIGZWin() override;
  void RemoveAllStrings() override;
  void GetTextArea(ScRect *rectangle) const override;
  i32 GetSelection() const override;
  bool SetSelection(i32 index, bool notify) override;
  bool MoveLineUp() override;
  bool MoveLineDown() override;
  bool ScrollPageUp() override;
  bool ScrollPageDown() override;
  bool ScrollToFirstLetter(char character) override;
  bool IsIndexInView(i32 index) const override;
  i32 GetLineCount() const override;
  i32 GetLineHeight() const override;
  i32 GetVisibleLines() const override;
  i32 GetIndexOfTopLine() const override;
  void PutIndexInView(i32 index) override;
  void SetIndexOfTopLine(i32 index) override;
  u32 GetItemIndexFromCursorPosition(i32 x, i32 y, bool clamp) const override;
  bool EnableScrollBar(bool first, bool second) override;
  void SetDrop(bool enabled) override;
  bool ResizeWindowForExactLineHeights() override;
  bool ResizeBoxToFitContents() override;
  bool ResizeBoxToFitContentsHorizontal() override;
  bool GetFontColor(i32 index, u32 *color) override;
  bool GetStringFromIndex(i32 index, ScStringView *text) const override;
  bool GetIndexFromString(i32 *index, const ScStringView &text) const override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using UiWindow::SetArea;
  u32 SetFlag(u32 mask, bool enabled) override;
  bool GZPaint() override;
  void SetFont(FtFont *font) override;
  bool SetFontColor(i32 index, u32 color) override;
  void SetHighlightColor(u32 color) override;
  void SetHighlightFillAlpha(f32 opacity) override;
  void SetDrawSunkenRect(bool enabled) override;
  void SetOutlineColors(u32 first, u32 second) override;
  void SetOutlineDrawing(bool enabled) override;
  bool SetScrollbarGutters(i8 horizontal, i8 vertical) override;
  bool SetTextGutters(i8 horizontal, i8 vertical) override;
  bool RemoveString(i32 index) override;
  bool RemoveString(const ScStringView &text) override;
  bool InsertString(const ScStringView &text, i32 index) override;
  bool ModifyString(const ScStringView &text, i32 index) override;
  bool ScrollToStart() override;
  bool ScrollToEnd() override;
  void SetShouldSort() override;
  void Sort() override;
  void SetLineItemBackgroundBuffer(GxBuffer *image) override;
  bool ClearFontColor(i32 index) override;
  bool SetMinimumLineHeight(u8 height) override;

protected:
  WnScrollbarControl *GetScrollbarV();
  WnScrollbarControl *GetScrollbarH();
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  WnListStrings strings;
  f32 background_opacity;
  u32 background_color, language;
  FtFont *font;
  u32 font_colors[3];
  i32 selection, visible_rows, first_visible, row_height, minimum_height;
  u8 byte220, outline, line_outline, popup, byte224, byte225;
  GxBuffer *popup_image, *row_image;
  u8 double_click;
  i32 last_click;
  u32 outline_first, outline_second;
  i8 gutter252, gutter253, gutter254, gutter255;
  u8 byte256, byte257;
};

class ScStringView;

// Retail cISC3ListItem::cListItemColInfo: signed 16-bit width and retained text interface.
struct ApListColumn {
  i16 unknown, width;
  const ScStringView *text;
};

// The executable list item owns its original vector of column records and a signed 16-bit reference count.
// Retail CodeView explicitly identifies the source vector facility and its eight-byte x86 element type.
class ApListItem : public ScUnknown {
public:
  ApListItem();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool SetColText(i32 column, const ScStringView *text);
  const ScStringView *GetColText(i32 column);
  bool SetListItemFlag(i32 flags, bool enabled);
  bool GetListItemFlag(i32 flags);
  bool SetImageID(i32 image);
  i32 GetImageID();
  i32 GetColWidth(i32 column);
  bool SetColWidth(i32 column, i32 width);
  i32 GetTotalWidth();

private:
  friend class ApListView;
  virtual ~ApListItem();
  ApListColumn *GetColInfo(u32 column);
  u32 flags;
  i16 image_id;
  u16 references;
  std::vector<ApListColumn> columns;
};

static_assert(sizeof(uptr) != 4 || sizeof(ApListColumn) == 8);
static_assert(sizeof(uptr) != 4 || sizeof(ApListItem) == 24);

// The executable control retains its original mutable text pointer and signed sound arguments.
class WnAppEditorControl : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual WnWindow *AsIGZWin() = 0;
  virtual void SetArea(i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual void GetTextArea(ScRect *output) = 0;
  virtual void SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(u32 color) = 0;
  virtual bool SetFontColor(WnFontColor index, u32 color) = 0;
  virtual bool GetFontColor(WnFontColor index, u32 *color) const = 0;
  virtual bool ClearFontColor(WnFontColor index) = 0;
  virtual void SetCaretColor(u32 color) = 0;
  virtual void SetFillColor(u32 color) = 0;
  virtual u32 GetHighlightFillColor() = 0;
  virtual void SetHighlightFillColor(u32 color) = 0;
  virtual bool ScrollLineUp() = 0;
  virtual bool ScrollLineDown() = 0;
  virtual bool ScrollToStart() = 0;
  virtual bool ScrollToEnd() = 0;
  virtual bool ScrollPageUp() = 0;
  virtual bool ScrollPageDown() = 0;
  virtual u32 GetMaximumTextToAllow() = 0;
  virtual void SetMaximumTextToAllow(u32 value) = 0;
  virtual u32 GetFirstVisibleLine() = 0;
  virtual u32 GetLineCount() = 0;
  virtual u32 GetLineHeight() = 0;
  virtual void SetOutlineUse(bool enabled, u32 color) = 0;
  virtual u32 GetTextGutterX() = 0;
  virtual void SetTextGutterX(u32 value) = 0;
  virtual u32 GetTextGutterY() = 0;
  virtual void SetTextGutterY(u32 value) = 0;
  virtual bool ResizeWindowForExactLineHeights() = 0;
  virtual char *GetText() = 0;
  virtual void GetText(ScStringView *output) = 0;
  virtual void GetText(char *output, u32 *length) = 0;
  virtual u32 GetTextLength() = 0;
  virtual bool SetText(const char *bytes, u32 count) = 0;
  virtual bool SetText(const ScStringView &value) = 0;
  virtual u32 InsertText(const char *bytes, u32 count, u32 index) = 0;
  virtual u32 InsertText(const char *bytes, u32 count, u32 line, u32 column) = 0;
  virtual bool RemoveChar(u32 index, bool move, ScStringView *removed) = 0;
  virtual u32 RemoveText(u32 index, u32 count, ScStringView *removed) = 0;
  virtual u32 RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) = 0;
  virtual u32 RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *removed) = 0;
  virtual u32 FindText(const char *bytes, u32 count, u32 position, bool forward) = 0;
  virtual bool SetInsertMode(bool enabled) = 0;
  virtual bool SetInsertionIndex(u32 index) = 0;
  virtual bool SetInsertionLineAndColumn(u32 line, u32 column) = 0;
  virtual void IncrementInsertionPosition(i32 delta) = 0;
  virtual bool SetCaretPeriod(u32 milliseconds) = 0;
  virtual void SetFlag(i32 flags) = 0;
  virtual void SetFlags(i32 flags) = 0;
  virtual void ClearFlag(i32 flags) = 0;
  virtual bool IsFlagSet(i32 flags) = 0;
  virtual i32 GetFlags() = 0;
  virtual void SetMaxUndoLevelCount(u32 count) = 0;
  virtual bool Undo() = 0;
  virtual bool Redo() = 0;
  virtual bool SetScrollBarImageVertical(GxBuffer *image, const ScRect &area) = 0;
  virtual bool SetScrollBarImageHorizontal(GxBuffer *image, const ScRect &area) = 0;
  virtual bool SetSound(i32 index, i32 identifier) = 0;
  // Unlimited propagates a raw sound-device byte; the retail bool PlaySoundA(long) declaration differs.
  virtual u8 PlaySound(i32 index) = 0;
};

// The executable history record owns its saved text and copies the five original caret words.
struct WnAppEditAction {
  // Construct the owned string while leaving the scalar action fields for the original caller to fill.
  WnAppEditAction() = default;
  WnAppEditAction(const WnAppEditAction &other);
  ~WnAppEditAction();
  WnAppEditAction &operator=(const WnAppEditAction &other);
  u8 erase;
  u32 erase_index, erase_count;
  u8 insert;
  u32 insert_index;
  ScString text;
  WnEditPosition position;
};

// The executable editor owns the window, editing interface, history and two shared callback timers.
class WnAppEditor : public UiWindow, public WnAppEditorControl {
public:
  explicit WnAppEditor(i32 flags);
  WnAppEditor(const ScRect &area, i32 flags);
  bool GZOnCharacter(char character) override;
  bool GZOnSetFocus(WnWindow *window) override;
  bool GZOnKillFocus(WnWindow *window) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) override;
  virtual void InitializeMemberVariables(i32 flags);
  virtual void IncrementInsertionPositionByWord(i32 delta);
  ~WnAppEditor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using UiWindow::SetArea;
  void SetFillColor(u32 color) override;
  using UiWindow::SetFillColor;
  bool SetFontColor(WnFontColor index, u32 color) override;
  bool SetFontColor(u32 color) override;
  bool GetFontColor(WnFontColor index, u32 *color) const override;
  bool ClearFontColor(WnFontColor index) override;
  void GetTextArea(ScRect *output) override;
  void SetFont(FtFont *font) override;
  void SetCaretColor(u32 color) override;
  u32 GetHighlightFillColor() override;
  void SetHighlightFillColor(u32 color) override;
  u32 GetMaximumTextToAllow() override;
  void SetMaximumTextToAllow(u32 value) override;
  void SetOutlineUse(bool enabled, u32 color) override;
  bool SetInsertMode(bool enabled) override;
  void SetFlags(i32 flags) override;
  void ClearFlag(i32 flags) override;
  u32 GetFirstVisibleLine() override;
  u32 GetLineCount() override;
  u32 GetLineHeight() override;
  u32 GetTextGutterX() override;
  u32 GetTextGutterY() override;
  void SetTextGutterX(u32 value) override;
  void SetTextGutterY(u32 value) override;
  bool ResizeWindowForExactLineHeights() override;
  char *GetText() override;
  void GetText(ScStringView *output) override;
  void GetText(char *output, u32 *length) override;
  u32 GetTextLength() override;
  bool ScrollLineUp() override;
  bool ScrollLineDown() override;
  bool ScrollToStart() override;
  bool ScrollToEnd() override;
  bool ScrollPageUp() override;
  bool ScrollPageDown() override;
  bool SetInsertionIndex(u32 index) override;
  bool SetInsertionLineAndColumn(u32 line, u32 column) override;
  virtual bool SetSelection(u32 start, u32 finish);
  bool GetIndexFromLineAndColumn(u32 line, u32 column, u32 *index);
  u32 FindText(const char *bytes, u32 count, u32 position, bool forward) override;
  u32 RemoveText(u32 index, u32 count, ScStringView *removed) override;
  bool RemoveChar(u32 index, bool move, ScStringView *removed) override;
  void IncrementInsertionPosition(i32 delta) override;
  u32 InsertText(const char *bytes, u32 count, u32 index) override;
  bool SetSound(i32 index, i32 identifier) override;
  u8 PlaySound(i32 index) override;
  bool Undo() override;
  bool Redo() override;
  bool SetText(const char *bytes, u32 count) override;
  bool SetText(const ScStringView &value) override;
  u32 InsertText(const char *bytes, u32 count, u32 line, u32 column) override;
  u32 RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) override;
  u32 RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *removed) override;
  bool SetCaretPeriod(u32 milliseconds) override;
  void SetFlag(i32 flags) override;
  using UiWindow::SetFlag;
  bool IsFlagSet(i32 flags) override;
  i32 GetFlags() override;
  void SetMaxUndoLevelCount(u32 count) override;
  bool SetScrollBarImageVertical(GxBuffer *image, const ScRect &area) override;
  bool SetScrollBarImageHorizontal(GxBuffer *image, const ScRect &area) override;

protected:
  ScString text;
  // Retail type 3B58 proves the original unsigned-word vector at offset 188.
  std::vector<u32> lines;
  u32 insertion_line, insertion_column, insertion_index, selection_start, selection_finish;
  i32 editor_flags;
  bool insert_mode;
  i32 language;
  bool double_byte;
  u8 ignore_next_character;
  ScString string236, mask_text;
  u32 gutter_x, gutter_y, text_width, text_height, visible_rows, first_visible;
  i32 preferred_x;
  u32 undo_limit;
  // Retail type 3C8E proves inline vector<UndoRedoAction> fields at offsets 308/320 with 60-byte elements.
  std::vector<WnAppEditAction> undo, redo;
  FtFont *font;
  u32 font_colors[6], highlight_color, line_height, maximum_text, outline_color;
  WnScrollbarControl *vertical_scrollbar, *horizontal_scrollbar;
  GxBuffer *vertical_image, *horizontal_image;
  ScRect vertical_image_area, horizontal_image_area;
  ScStopwatch elapsed;
  u32 double_click_ms, caret_period, caret_color;
  bool caret_visible;
  ScCallbackTimer caret_timer, scroll_timer;
  u32 sounds[4];
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool SelectCurrentWord();
  void StartScrollTimer();
  void StopScrollTimer();
  void MoveInsertionPositionToBeforeReturnIfNecessary();
  void PutInsertionPositionInView();
  void ToggleCaretVisibility();
  void DoCursorScroll();
  // Recompute viewport/reflow and owned scrollbars in the original callback order.
  void RecalculateAll();
  u32 CalculateVisibleWidth();
  u32 CalculateVisibleHeight();
  void FixInsertionPositionIfBroken();
  bool TestForPossibleScrollBarAdditionOrRemoval();
  bool AddScrollBar(bool vertical, bool horizontal);
  void RemoveScrollBar(bool vertical, bool horizontal);
  void SetScrollBarSizes();
  // Measure either all stored offsets or the source unclamped visible range through the actual font interface.
  u32 GetPixelWidthOfLongestLine(bool all_lines);
  void FindFirstAndLastCharacterIndexForALineLongerThanTheScreenWidth(
      u32 line, u32 horizontal, u32 *first, u32 *last, u32 *offset);
  void CalculateAllLineLengths(u32 first_line);
  // Update the source one-based line-offset slot without changing earlier offsets.
  void AdjustLinePositionArray(u32 increment, u32 count);
  // Synchronize the optional vertical control, recalculating its range only when requested.
  void SetScrollBarValues(bool recalculate);
  u32 GetCharacterCountOfLine(u32 line);
  // Convert through the original line offsets using separate mutable outputs.
  void GetLineAndColumnFromIndex(u32 *line, u32 *column, u32 index);
  u32 NextCharSizeFromText(const char *bytes, u32 length);
  u32 PreviousCharSizeFromText(const char *bytes, u32 length);
  bool IsCharOK(u8 character);
  bool IsCharOK(const char *bytes, u32 length, u32 *count);
  u32 RemoveBadCharacters(ScString *value);
  u32 RemoveBadCharacters(char *bytes, u32 *length);
  u32 CharCountFromText(const char *bytes, u32 length);
  i32 FixCharacterPositionBoundaryIfNecessary(u32 index);
  void GetIndexFromPosition(u32 *index, i32 x, i32 y);
  bool IsInsertionPositionInView();
  void AddFirstLineLength();
  bool AddUndoRecordForTextInsertion(const WnEditPosition &position, u32 index, const char *inserted, u32 count,
      const char *removed, u32 removed_count);
  bool AddUndoRecordForTextTextDeletion(const WnEditPosition &position, u32 index, const char *removed, u32 count);
  // Convert between signed local coordinates and byte-oriented line/column outputs.
  void GetLineAndColumnFromPosition(u32 *line, u32 *column, i32 x, i32 y);
  void GetPositionFromLineAndColumn(u32 line, u32 column, i32 *x, i32 *y);
  void GetColumnFromPosition(u32 line, u32 *column, i32 x);
  static void CaretTimer(uptr editor);
  static void ScrollTimer(uptr editor);
};

class FtFont;
class WnScrollbarControl;

// Retail cSC3WinListView owns an ordered map of borrowed item pointers.
// Original std::map use and all fields are established by retail type 0x3878 and field list 0x3879.
class ApListView : public WnRootWidget, public ApListViewControl {
public:
  ApListView();
  static ScUnknown *CreateNewObject();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  WnWindow *AsIGZWin() override;
  bool SetListViewFlag(i32 mask, bool enabled) override;
  bool GetListViewFlag(i32 mask) override;
  bool AddColumn(i32 column, i32 width, const ScStringView *text) override;
  bool AddItem(i32 item, i32 image, const ScStringView *text) override;
  bool RemoveItem(i32 item) override;
  bool RemoveAllItems() override;
  bool EnsureVisibility(i32 item) override;
  bool SetSelectedItem(i32 item) override;
  i32 GetItemAt(i32 x, i32 y) override;
  bool SetItemText(i32 item, i32 column, const ScStringView *text) override;
  bool GetItemText(i32 item, i32 column, const ScStringView **out) override;
  bool SetFont(FtFont *font) override;
  i32 GetSelectedItemID() override;
  bool GetMinMaxInfo(ScRect *out) const override;

protected:
  ~ApListView() override;
  void DrawItem(ApListItem *item, i32 x, i32 y);
  ApListItem *GetItem(i32 identifier);
  bool SelectItem(i32 identifier);
  bool CalcVirtualSize();
  bool CalcViewArea();
  bool CalcViewIdRange();
  bool CalcScrollMax();
  bool ScrollTo(i32 x, i32 y);
  bool UpdateScrollBars();
  bool CreateScrollBar(i32 orientation, WnScrollbarControl **out);
  bool OnScrollHorizontal(const ScMessage &message);
  bool OnScrollVertical(const ScMessage &message);
  u32 list_flags;
  std::map<i32, ApListItem *> rows;
  ApListItem *heading;
  FtFont *font;
  i32 selected, first_visible, last_visible, row_height, scroll_x, scroll_y, scroll_max;
  ScRect view_area;
  i32 virtual_width, virtual_height;
  WnScrollbarControl *horizontal, *vertical;
};

#endif
