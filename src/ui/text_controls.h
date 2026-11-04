#ifndef SC3_UI_TEXT_CONTROLS_H
#define SC3_UI_TEXT_CONTROLS_H

#include "city/sc_stopwatch.h"
#include "common/sc3_callback_timer.h"
#include "fonts/sc3_bitmap_font.h"
#include "geometry/api.h"
#include "occupants/api.h"
#include "ui/controls.h"
#include "window/api.h"
#include "window/lists.h"

// Actual stateless cSC3OccDataToStringPairDisp writes the three value rows of a queried two-column display.
// The private Windows nominal bool/BYTE return remains unproven; both bodies return the observed zero byte.
class UiAttributeText {
public:
  u8 InitFieldNames(ScUnknown *display);
  u8 Transfer(GeAttrib *attributes, ScUnknown *display);
  u8 Transfer(OcOverride *attributes, ScUnknown *display);
};

// The private6580A947 view owns two independently formatted string columns; its original class spelling is unavailable.
class UiColumnsControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  virtual u8 SetBorderStyle(u32 style) = 0;
  virtual u8 GetBorderStyle(u32 *style) = 0;
  virtual u8 SetOutlineColor(u32 pixel) = 0;
  virtual u8 SetOutlineColors(u32 first, u32 second, u32 third, u32 fourth) = 0;
  virtual u8 GetOutlineColors(u32 *first, u32 *second, u32 *third, u32 *fourth) = 0;
  virtual u8 SetFont(i32 column, FtFont *font) = 0;
  virtual u8 SetFontColor(i32 column, u32 pixel) = 0;
  virtual u8 AddRow(u32 row_id, ScStringView *first, ScStringView *second) = 0;
  virtual u8 GetRowText(u32 row_id, u32 column, ScStringView **out) = 0;
  virtual u8 SetRowText(u32 row_id, u32 column, ScStringView *text) = 0;
  virtual i32 GetColumnWidth(i32 column) = 0;
  virtual u8 SetColumnWidth(i32 column, i32 width) = 0;
  virtual u8 GetMinMaxInfo(ScRect *limits) = 0;
  virtual u8 Fit() = 0;
};

// The source248-byte owner retains row strings, two fonts and an ellipsis string alongside its actual window base.
class UiColumns : public UiBaseWindow, public UiColumnsControl {
public:
  UiColumns();
  ~UiColumns() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool Init() override;
  bool Shutdown() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool GZPaint() override;
  u8 SetBorderStyle(u32 style) override;
  u8 GetBorderStyle(u32 *style) override;
  u8 SetOutlineColor(u32 pixel) override;
  u8 SetOutlineColors(u32 first, u32 second, u32 third, u32 fourth) override;
  u8 GetOutlineColors(u32 *first, u32 *second, u32 *third, u32 *fourth) override;
  u8 SetFont(i32 column, FtFont *font) override;
  u8 SetFontColor(i32 column, u32 pixel) override;
  u8 AddRow(u32 identifier, ScStringView *first, ScStringView *second) override;
  u8 GetRowText(u32 identifier, u32 column, ScStringView **out) override;
  u8 SetRowText(u32 identifier, u32 column, ScStringView *text) override;
  i32 GetColumnWidth(i32 column) override;
  u8 SetColumnWidth(i32 column, i32 width) override;
  u8 GetMinMaxInfo(ScRect *limits) override;
  u8 Fit() override;

protected:
  struct UiColumnRow {
    UiColumnRow *next, *previous;
    u32 identifier;
    ScStringView *text[2];
  };

  u32 border_style, outline_colors[4];
  i32 insets[4], first_width, minimum_widths[2], gap;
  u32 font_height;
  UiColumnRow *rows;
  FtFont *fonts[2];
  u32 text_colors[2];
  const ScStringView *ellipsis;
  u8 DrawSegment(GxBuffer *target, u32 pixel, i32 x1, i32 y1, i32 x2, i32 y2);
  u8 PaintBorder(GxBuffer *target, const ScRect *rectangle, u8 filled);
};

class WnScrollbarControl;
class FtFont;

void UiEraseEditorText(ScString *text, u32 index, u32 count);

// The original saved caret state is copied as five consecutive words before editing and history replay.
struct UiEditPosition {
  u32 line, column, index, selection_start, selection_finish;
};

// Actual editor history entries contain insertion/removal effects, one owned byte string and the five saved caret words.
struct UiEditAction {
  u8 erase;
  u32 erase_index, erase_count;
  u8 insert;
  u32 insert_index;
  ScString text;
  UiEditPosition position;
};

// Typed ownership keeps vector destruction at its original position among the editor's members.
struct UiEditLines {
  u32 *begin = nullptr, *end = nullptr, *capacity = nullptr;
  ~UiEditLines();
  void Resize(u32 count, u32 fill);
};

// History owns constructed action records; allocation and mutation stay editor-specific.
struct UiEditHistory {
  UiEditAction *begin = nullptr, *end = nullptr, *capacity = nullptr;
  ~UiEditHistory();
  u32 Size() const;
  void Append(const UiEditAction &value);
  void EraseFirst(u32 count);
  void Pop();
  void Clear();
};

// SIMUI-local editor has its own module-bound base and the shared editor interface at original164.
// Recovered methods use direct game contracts; runtime comparison and platform integration remain unfinished.
class UiEditor : public UiBaseWindow, public WnEditorControl {
public:
  explicit UiEditor(i32 flags);
  bool GZOnCharacter(char character) override;
  bool GZOnSetFocus(WnWindow *window) override;
  bool GZOnKillFocus(WnWindow *window) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) override;
  virtual void InitializeMemberVariables(i32 flags);
  virtual void IncrementInsertionPositionByWord(i32 delta);
  ~UiEditor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using UiBaseWindow::SetArea;
  void SetFillColor(u32 color) override;
  using UiBaseWindow::SetFillColor;
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
  const char *GetText() override;
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
  bool SetSound(i32 index, u32 identifier) override;
  u8 PlaySound(u32 index) override;
  bool Undo() override;
  bool Redo() override;
  bool SetText(const char *bytes, u32 count) override;
  bool SetText(const ScStringView &value) override;
  u32 InsertText(const char *bytes, u32 count, u32 line, u32 column) override;
  u32 RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) override;
  u32 RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *removed) override;
  bool SetCaretPeriod(u32 milliseconds) override;
  void SetFlag(i32 flags) override;
  using UiBaseWindow::SetFlag;
  bool IsFlagSet(i32 flags) override;
  i32 GetFlags() override;
  void SetMaxUndoLevelCount(u32 count) override;
  bool SetScrollBarImageVertical(GxBuffer *image, const ScRect &area) override;
  bool SetScrollBarImageHorizontal(GxBuffer *image, const ScRect &area) override;

protected:
  ScString text;
  // Retail type 3B58 proves the original unsigned-word vector at offset 188.
  UiEditLines lines;
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
  UiEditHistory undo, redo;
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
  void RecalculateAll();
  u32 CalculateVisibleWidth();
  void SetScrollBarSizes();
  void SetScrollBarValues(bool recalculate);
  void GetLineAndColumnFromIndex(u32 *line, u32 *column, u32 index);
  u32 GetCharacterCountOfLine(u32 line);
  u32 CalculateVisibleHeight();
  void CalculateAllLineLengths(u32 first_line);
  void FixInsertionPositionIfBroken();
  bool TestForPossibleScrollBarAdditionOrRemoval();
  void ToggleCaretVisibility();
  void DoCursorScroll();
  void StartScrollTimer();
  void StopScrollTimer();
  void MoveInsertionPositionToBeforeReturnIfNecessary();
  void AdjustLinePositionArray(u32 increment, u32 count);
  void AddFirstLineLength();
  bool AddScrollBar(bool vertical, bool horizontal);
  void RemoveScrollBar(bool vertical, bool horizontal);
  u32 GetPixelWidthOfLongestLine(bool all_lines);
  u32 NextCharSizeFromText(const char *bytes, u32 length);
  u32 PreviousCharSizeFromText(const char *bytes, u32 length);
  bool IsCharOK(u8 character);
  bool IsCharOK(const char *bytes, u32 length, u32 *count);
  u32 RemoveBadCharacters(ScString *value);
  void GetLineAndColumnFromPosition(u32 *line, u32 *column, i32 x, i32 y);
  void GetColumnFromPosition(u32 line, u32 *column, i32 x);
  void GetPositionFromLineAndColumn(u32 line, u32 column, i32 *x, i32 *y);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool SelectCurrentWord();
  void PutInsertionPositionInView();
  bool AddUndoRecordForTextInsertion(const UiEditPosition &position, u32 index, const char *inserted, u32 count,
      const char *removed, u32 removed_count);
  bool AddUndoRecordForTextTextDeletion(const UiEditPosition &position, u32 index, const char *removed, u32 count);
  void FindFirstAndLastCharacterIndexForALineLongerThanTheScreenWidth(
      u32 line, u32 horizontal, u32 *first, u32 *last, u32 *offset);
  static void SC3_CDECL CaretTimer(uptr argument);
  static void SC3_CDECL ScrollTimer(uptr argument);
};

// SIMUI owns its separately linked copy of the original text-list class and its two row lists.
// The common control interface ends at128; concrete additions retain their original introducing order.
class UiTextList : public WnTextListControl, public UiBaseWindow {
public:
  UiTextList();
  ~UiTextList() override;
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
  struct UiTextRow {
    UiTextRow *next, *previous;
    ScString first, second;
    u32 colors[2], tag;
  };

  static_assert(sizeof(uptr) != 4 || sizeof(UiTextRow) == 60);

  u32 language;
  f32 highlight_alpha;
  u32 highlight_color, text_colors[3], border_colors[2];
  UiTextRow *rows, *wrapped_rows;
  FtFont *fonts[2];
  i32 selection, visible_rows, first_visible, row_height, first_width;
  u8 alternate_alignment, fill_background, rectangle_outline, line_outline;
  u32 outline_colors[2];
  i8 gutters[4], gap;
  i32 last_click;
  u8 double_click, allow_scrollbar, state262;

  UiTextRow *AppendWrappedRow(const ScString &first, const ScString &second, u32 first_color, u32 second_color);
  u8 NotifySelection();
  u8 NotifyActivation();
  UiTextRow *FindRow(i32 index);
  static void ClearRows(UiTextRow *head);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpR(i32 x, i32 y, u32 modifiers) override;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiTextList) == 264);

#endif
