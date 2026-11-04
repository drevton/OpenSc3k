#ifndef SC3_WINDOW_LISTS_H
#define SC3_WINDOW_LISTS_H

#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "window/api.h"

class WnScrollbarControl;
class FtFont;

// Original4132242B list interface, secondary to the164-byte root in registered class598.
// Declaration groups preserve all49 installed MSVC slots and use the recovered font interface.
class WnListControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
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
  virtual i32 GetItemCount() const = 0;
  virtual i32 GetRowHeight() const = 0;
  virtual i32 GetVisibleRows() const = 0;
  virtual i32 GetFirstVisible() const = 0;
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

// A link-only sentinel has no string payload; actual entries own their concrete text in place.
// These small native links replace the original generic list machinery without adding receiver metadata.
struct WnListLink {
  WnListLink *next, *previous;
};

struct WnListNode : WnListLink {
  explicit WnListNode(const ScString &value);
  ScString text;
};

struct WnListStrings {
  WnListStrings();
  ~WnListStrings();
  u32 Count() const;
  WnListNode *At(u32 index) const;
  void InsertBefore(WnListLink *position, const ScString &text);
  void Erase(WnListLink *node);
  void Clear();
  WnListLink *head;
};

// Registered concrete list owns its strings and the actual root/control interfaces.
// Original primary additions and secondary overrides retain their installed order and separate contracts.
class WnList : public WnRootWidget, public WnListControl {
public:
  explicit WnList(bool popup);
  ~WnList() override;
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
  WnWindow *GetWindow() override;
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
  i32 GetItemCount() const override;
  i32 GetRowHeight() const override;
  i32 GetVisibleRows() const override;
  i32 GetFirstVisible() const override;
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
  using WnRootWidget::SetArea;
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

class FtFont;

// Original leading interface4505D77C of class6505D7B2. Recovered entries are in installed order.
// Slot64 returns the full observed item count; its original signed/unsigned source spelling remains unidentified.
// Configuration effects retain unresolved source return declarations.
class WnIconListControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool InsertListItem(GxBuffer *image, const ScStringView &text, u32 color, i32 index) = 0;
  virtual bool RemoveListItem(u32 index) = 0;
  virtual bool RemoveAllListItems() = 0;
  virtual bool ModifyString(const ScStringView &text, u32 index) = 0;
  virtual bool ModifyStringColor(u32 color, u32 index) = 0;
  virtual void SetListItemFontEffects(FtFont *font) = 0;
  virtual u8 SetOpaque(u8 enabled) = 0;
  virtual void SetIconToTextSpacingEffects(i32 spacing) = 0;
  virtual bool MakeTopVisibleItem(u32 index) = 0;
  virtual bool ScrollPageDown() = 0;
  virtual bool ScrollPageUp() = 0;
  virtual bool EnableScrollBar(u8 first, u8 second) = 0;
  virtual u32 GetNumListItems() = 0; // Provisional full-word ABI carrier; nominal signedness is unresolved.
  virtual u8 SetDrawSunkenRect(u8 enabled) = 0;
  virtual void SetSunkenRectColorsEffects(u32 first, u32 second) = 0;
};

// The sentinel owns only links; actual rows additionally hold an image pointer and a concrete caption.
struct WnIconLink {
  WnIconLink *next, *previous;
};

struct WnIconItem : WnIconLink {
  GxBuffer *image;
  ScString text;
  u32 color;
  i32 start, finish;
};

// Image/caption rows occupy the actual control-first, root-second class. No general list allocator is reproduced.
class WnIconList : public WnIconListControl, public WnRootWidget {
public:
  WnIconList();
  ~WnIconList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  u32 GetNumListItems() override;
  u8 SetOpaque(u8 enabled) override;
  u8 SetDrawSunkenRect(u8 enabled) override;
  bool EnableScrollBar(u8 first, u8 second) override;
  bool MakeTopVisibleItem(u32 index) override;
  bool ScrollPageDown() override;
  bool ScrollPageUp() override;
  bool InsertListItem(GxBuffer *image, const ScStringView &text, u32 color, i32 index) override;
  bool RemoveListItem(u32 index) override;
  bool RemoveAllListItems() override;
  bool ModifyString(const ScStringView &text, u32 index) override;
  bool ModifyStringColor(u32 color, u32 index) override;
  void SetListItemFontEffects(FtFont *font) override;
  void SetIconToTextSpacingEffects(i32 spacing) override;
  void SetSunkenRectColorsEffects(u32 first, u32 second) override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::SetArea;

protected:
  WnIconLink *items;
  FtFont *font;
  u8 fill_background, draw_outline;
  u32 first_edge, second_edge;
  i32 position, extent, image_width, gutter;
  u8 first_scroll_flag, second_scroll_flag;
  void DestroyEntries();
  WnIconItem *ItemAt(u32 index) const;
  bool AddVerticalScrollbar();
  void CalculateAllMetricsEffects();
  void RemoveVerticalScrollbarEffects();
  void SetScrollbarSizeEffects();
  void SetScrollbarValueEffects();
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 control_id) override;
};

class WnWindow;
class ScStringView;

// Canonical82FA8FD9 interface shared with the independently linked SIMUI copy.
// Its33 installed slots end at RemoveAllRows128; later operations belong to each concrete class.
class WnTextListControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual bool AddRow(const ScStringView &first, const ScStringView &second, i32 index) = 0;
  virtual bool AddRow(
      const ScStringView &first, const ScStringView &second, u32 first_color, u32 second_color, i32 index) = 0;
  virtual void SetJustification(bool value) = 0;
  virtual void SetOpaque(bool value) = 0;
  virtual void SetRightColumnTextStart(i32 width) = 0;
  virtual bool ResizeWindowForExactLineHeights() = 0;
  virtual void SetFonts(FtFont *first, FtFont *second) = 0;
  virtual bool SetFontColor(i32 index, u32 pixel) = 0;
  virtual bool GetFontColor(i32 index, u32 *pixel) = 0;
  virtual bool ResetFontColor(i32 index) = 0;
  virtual void SetHighlightColor(u32 pixel) = 0;
  virtual void SetHighlightFillAlpha(f32 opacity) = 0;
  virtual bool MoveLineUp() = 0;
  virtual bool MoveLineDown() = 0;
  virtual bool ScrollToStart() = 0;
  virtual bool ScrollToEnd() = 0;
  virtual bool ScrollPageUp() = 0;
  virtual bool ScrollPageDown() = 0;
  virtual bool IsItemVisible(i32 index) = 0;
  virtual void PutIndexInView(i32 index) = 0;
  virtual void SetIndexOfTopLine(i32 index) = 0;
  virtual bool EnableScrollBar(bool first, bool second) = 0;
  virtual i32 GetItemCount() const = 0;
  virtual i32 GetFirstVisible() const = 0;
  virtual void SetOutlineDrawing(bool value) = 0;
  virtual void SetDrawSunkenRect(bool value) = 0;
  virtual void SetSunkenRectColors(u32 first, u32 second) = 0;
  virtual bool RemoveRow(i32 index) = 0;
  virtual bool RemoveAllRows() = 0;
};

// Actual264-byte owner: primary list, inherited root window at4, two circular row lists and two retained fonts.
// The canonical control interface is shared with the independently linked SIMUI copy.
class WnTextList : public WnTextListControl, public WnRootWidget {
public:
  WnTextList();
  ~WnTextList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u32 SetFlag(u32 mask, bool enabled) override;
  virtual bool GZCharacter(u32 character);
  virtual bool GetRowText(ScStringView *first, ScStringView *second, i32 index);
  virtual bool SetRowText(const ScStringView &text, i32 index, bool first);
  virtual bool SetRowColor(u32 pixel, i32 index, bool first);
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
  bool AddRow(
      const ScStringView &first, const ScStringView &second, u32 first_color, u32 second_color, i32 index) override;
  bool AddRow(const ScStringView &first, const ScStringView &second, i32 index) override;
  bool SetFontColor(i32 index, u32 pixel) override;
  bool GetFontColor(i32 index, u32 *pixel) override;
  bool ResetFontColor(i32 index) override;
  bool RemoveRow(i32 index) override;
  bool RemoveAllRows() override;
  bool IsItemVisible(i32 index) override;
  i32 GetItemCount() const override;
  i32 GetFirstVisible() const override;
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
    ScString first, second;
    u32 colors[2], tag;
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

static_assert(sizeof(uptr) != 4 || sizeof(WnTextList) == 264);


// The original library callback exposes an opaque client value, passed unchanged after the item identifier.
using WnTickerCallback = void (*)(u32 identifier, void *context);

// Each queued item owns its string and font holds; the callback context remains borrowed client data.
class WnTickerItem {
public:
  WnTickerItem(u32 identifier, ScStringView *text, FtFont *font, u32 color, u32 end_color, u32 kind, u32 timestamp,
      u32 times_left, const ScMessage *message, WnTickerCallback callback, void *context);
  WnTickerItem(const WnTickerItem &source);
  virtual ~WnTickerItem();
  u32 identifier, kind;
  ScStringView *text;
  FtFont *font;
  ScRect span;
  u32 color, end_color, timestamp, times_left;
  ScMessage message;
  WnTickerCallback callback;
  void *context;
};

static_assert(sizeof(uptr) != 4 || sizeof(WnTickerItem) == 76);

struct WnTickerNode {
  WnTickerNode *next, *previous;
  WnTickerItem *item;
};

// Game-specific ticker queue nodes own no items; original ticker operations order item deletion separately.
struct WnTickerQueue {
  WnTickerQueue();
  ~WnTickerQueue();
  void Erase(WnTickerNode *node);
  void Append(WnTickerItem *item);
  WnTickerNode *head;
};

// Recovered 032F0B3C public interface through40; queue virtuals belong to the concrete ticker.
// Overloads are declared in reverse physical order for the MSVC vtable.
class WnTickerControl : public ScUnknown {
public:
  virtual u32 AddMessage(
      ScStringView *text, u32 color, u32 end_color, u32 kind, WnTickerCallback callback, void *context) = 0;
  virtual u32 AddMessage(ScStringView *text, u32 color, u32 end_color, u32 kind, const ScMessage *message) = 0;
  virtual bool RemoveItem(u32 identifier) = 0;
  virtual bool Clear() = 0;
  virtual bool SetFont(FtFont *font) = 0;
  virtual void SetSpeed(u32 speed) = 0;
  virtual void SetFrequency(u32 milliseconds) = 0;
  virtual WnWindow *GetWindow() = 0;
};

// The actual ticker has its control before its root window, two owned queues and the shared stopwatch receiver.
class WnTicker : public WnTickerControl, public WnRootWidget {
public:
  WnTicker();
  ~WnTicker() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  u32 AddMessage(ScStringView *text, u32 color, u32 end_color, u32 kind, const ScMessage *message) override;
  u32 AddMessage(
      ScStringView *text, u32 color, u32 end_color, u32 kind, WnTickerCallback callback, void *context) override;
  bool Clear() override;
  bool RemoveItem(u32 identifier) override;
  bool SetFont(FtFont *font) override;
  void SetSpeed(u32 speed) override;
  void SetFrequency(u32 milliseconds) override;
  bool Init() override;
  bool Shutdown() override;
  void SetCaption(const ScStringView &text) override;
  bool GZPaint() override;
  virtual void GetNextMessage();

protected:
  virtual void AddMessageInternal(WnTickerItem *item);
  u32 GetNewMessageID();
  WnTickerItem *FindClickedMessage(i32 x);
  ScStopwatch timer;
  void AdvanceMessage();
  void ShiftSpans();
  void DoDraw();
  i32 phase;
  GxBuffer *buffer;
  u32 current_kind;
  i32 language;
  WnTickerNode *item_cursor;
  const char *next_character;
  u32 next_id;
  FtFont *font, *active_font;
  u32 current_color, current_end_color;
  WnTickerQueue active, pending;
  u32 step, interval;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
};

class WnTextControl;

// Original combo interface412CE496: all32 installed slots through the extent query.
// Overload declarations preserve the installed MSVC order within each group.
class WnComboControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual WnTextControl *GetLineInput() = 0;
  virtual void SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(i32 index, u32 color) = 0;
  virtual u32 GetFontColor(i32 index) const = 0;
  virtual bool ClearFontColor(i32 index) = 0;
  virtual bool SetHighlightColor(u32 color) = 0;
  virtual bool SetHighlightFillAlpha(f32 opacity) = 0;
  virtual u8 GetEditable() const = 0;
  virtual bool SetEditable(u8 enabled) = 0;
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

// Actual combo owns its font and arrow atlas while the root owns its embedded text and popup children.
class WnCombo : public WnRootWidget, public WnComboControl {
public:
  explicit WnCombo(u8 editable);
  ~WnCombo() override;
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
  u8 GetEditable() const override;
  bool SetEditable(u8 enabled) override;
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
  using WnRootWidget::SetArea;
  u32 SetFlag(u32 mask, bool enabled) override;
  void SetFillColor(u32 color) override;
  using WnRootWidget::SetFillColor;
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
  u8 editable;
  GxBuffer *arrow;
  i32 arrow_x, arrow_y, arrow_width, arrow_height, arrow_left, arrow_top, arrow_source_x;
  u32 arrow_color;
  i8 button_gutter, horizontal_gutter, vertical_gutter;
};

#endif
