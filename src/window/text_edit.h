#ifndef SC3_WINDOW_TEXT_EDIT_H
#define SC3_WINDOW_TEXT_EDIT_H

#include <vector>

#include "city/sc_stopwatch.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "window/api.h"

class WnEditor;
class WnEditorTimer;
using WnEditorTimerCallback = void (*)(WnEditor *editor);
using WnTimerCallback = u32 (*)(WnEditorTimer *timer);

// The installed 32-byte timer is constructed only by the multiline editor, with actual typed editor callbacks.
// Registry membership replaces the private generic set; no foreign addresses or execution adapters enter this class.
class WnEditorTimer {
public:
  WnEditorTimer();
  virtual ~WnEditorTimer();
  virtual bool SetResolution(u32 value);
  virtual bool GetResolution(u32 *value);
  virtual bool SetPeriod(u32 value);
  virtual bool GetPeriod(u32 *value);
  virtual bool StartPeriodic(u32 interval, ScUnknown *owner);
  virtual bool StartOneShot(u32 delay, ScUnknown *owner);
  virtual bool Stop();
  virtual bool IsRunning();
  virtual bool SetCallbackFunction(WnEditorTimerCallback callback, WnEditor *context);
  // Original callback returns zero after dispatch; its external context is represented by the actual timer pointer.
  static u32 InternalCallbackFunction(WnEditorTimer *timer);

  // Publish observed shared loads/stores with the same Win32 compiler semantics used by the queue.
  ScUnknown *volatile owner;
  WnEditorTimerCallback volatile callback;
  WnEditor *volatile context;
  volatile u32 resolution, interval;
  volatile bool running, once;
  volatile i32 identifier;
};

// Retail4662/4664/4666 define the original signed32 font, outline and edge-color indices.
enum class WnInputColor : i32 { Normal = 0, Disabled = 1, Hilited = 2, Count = 3 };
enum class WnInputOutline : i32 { None = 0, Normal = 1, ThreeD = 2, ThreeDEx = 3 };
enum class WnInputEdge : i32 { LeftTop = 0, RightBottom = 1, LeftTopInside = 2, RightBottomInside = 3, Count = 4 };

// Original 35-slot single-line input interface21335C59, verified against the installed secondary table.
// Raw byte contracts retain noncanonical results where the PC Boolean-vs-byte declaration remains unresolved.
class WnTextControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool SetFont(FtFont *font) = 0;
  virtual bool SetFontColor(u32 color) = 0;
  virtual bool SetFontColor(WnInputColor index, u32 color) = 0;
  virtual bool GetFontColor(WnInputColor index, u32 *out) const = 0;
  virtual bool ClearFontColor(WnInputColor index) = 0;
  virtual bool SetFilled(u8 enabled) = 0;
  virtual u8 IsFilled() const = 0;
  virtual bool SetOutlineType(WnInputOutline mode) = 0;
  virtual bool GetOutlineType(WnInputOutline *out) const = 0;
  virtual bool SetOutlineColor(WnInputEdge index, u32 color) = 0;
  virtual bool SetOutlineColor(u32 color) = 0;
  virtual bool GetOutlineColor(WnInputEdge index, u32 *out) const = 0;
  virtual bool SetOutline(u8 enabled) = 0;
  virtual bool SetHighlightFillColor(u32 color) = 0;
  virtual bool GetHighlightFillColor(u32 *out) const = 0;
  virtual bool SetHighlightFillAlpha(f32 opacity) = 0;
  virtual bool GetHighlightFillAlpha(f32 *out) const = 0;
  virtual i32 GetText(ScStringView *out) const = 0;
  virtual bool SetText(const ScStringView &text) = 0;
  virtual bool SetText(const ScStringView *text) = 0;
  virtual bool SetText(char *text) = 0;
  virtual bool SetCursorLoc(u32 position) = 0;
  virtual u8 IsEditable() const = 0;
  virtual bool SetEditable(u8 enabled) = 0;
  virtual bool SetCharacterLimit(u32 limit) = 0;
  virtual bool SetSelection(u32 start, u32 count) = 0;
  virtual bool SetGutters(i8 left, i8 top, i8 right, i8 bottom) = 0;
  virtual bool GetGutters(i8 *left, i8 *top, i8 *right, i8 *bottom) const = 0;
  virtual bool GetMinMaxInfo(ScRect *out) = 0;
  virtual bool SetSoundID(i32 index, i32 identifier) = 0;
  virtual u8 PlaySoundA(i32 index) = 0;
};

// Recovered text-editing receiver with its actual owned string, borrowed drawing dependencies and source input overrides.
class WnTextEdit : public WnRootWidget, public WnTextControl {
public:
  explicit WnTextEdit(u8 editable);
  ~WnTextEdit() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool Init() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::SetArea;
  u32 SetFlag(u32 mask, bool enabled) override;
  bool GZPaint() override;
  bool SetFont(FtFont *font) override;
  bool SetFontColor(WnInputColor index, u32 color) override;
  bool SetFontColor(u32 color) override;
  bool ClearFontColor(WnInputColor index) override;
  bool GetFontColor(WnInputColor index, u32 *out) const override;
  bool SetFilled(u8 enabled) override;
  u8 IsFilled() const override;
  bool SetOutlineType(WnInputOutline mode) override;
  bool GetOutlineType(WnInputOutline *out) const override;
  bool SetOutline(u8 enabled) override;
  bool SetOutlineColor(u32 color) override;
  bool SetOutlineColor(WnInputEdge index, u32 color) override;
  bool GetOutlineColor(WnInputEdge index, u32 *out) const override;
  bool SetHighlightFillColor(u32 color) override;
  bool GetHighlightFillColor(u32 *out) const override;
  bool SetHighlightFillAlpha(f32 opacity) override;
  bool GetHighlightFillAlpha(f32 *out) const override;
  bool SetCursorLoc(u32 position) override;
  u8 IsEditable() const override;
  bool SetEditable(u8 enabled) override;
  bool SetCharacterLimit(u32 limit) override;
  bool SetGutters(i8 left, i8 top, i8 right, i8 bottom) override;
  bool GetGutters(i8 *left, i8 *top, i8 *right, i8 *bottom) const override;
  bool GetMinMaxInfo(ScRect *out) override;
  bool SetText(const ScStringView &text) override;
  bool SetText(const ScStringView *text) override;
  bool SetText(char *text) override;
  i32 GetText(ScStringView *out) const override;
  bool SetSoundID(i32 index, i32 identifier) override;
  u8 PlaySoundA(i32 index) override;
  bool SetSelection(u32 start, u32 count) override;

protected:
  bool double_byte;
  u32 position, selection_start, selection_finish, first_visible;
  i32 language;
  i8 caret_character;
  bool caret_visible;
  ScString text;
  bool changing_text;
  FtFont *font;
  i32 text_height, caret_width;
  u32 text_colors[3];
  i32 border;
  u32 border_colors[4], selection_color;
  f32 opacity;
  u8 background, editable;
  u32 limit;
  i8 margins[4];
  u32 sounds[3];
  // Native UTF-8 input pending completion through the original one-byte character callback.
  ScString input_pending;
  bool InsertUtf8(const char *bytes, u32 count, u32 index);
  bool UpdateScroll();
  bool NotifyChanged();
  u32 PositionFromX(i32 x);
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnKillFocus(WnWindow *window) override;
  bool GZOnSetFocus(WnWindow *window) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  bool GZOnCharacter(char character) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool InsertCharacter(i8 character, u32 index);
  bool DeleteText(u32 position, u32 count);
  bool NotifyAccepted();
};

ScUnknown *WnConstructTextEdit();

class WnScrollbarControl;
class FtFont;

// The original saved caret state is copied as five consecutive words before editing and history replay.
struct WnEditPosition {
  u32 line, column, index, selection_start, selection_finish;
};

// Actual editor history entries contain insertion/removal effects, one owned byte string and the five saved caret words.
struct WnEditAction {
  u8 erase;
  u32 erase_index, erase_count;
  u8 insert;
  u32 insert_index;
  ScString text;
  WnEditPosition position;
};

// Class231A1C58 is root-first with the editor control at original164. Only recovered live state is represented here.
// The original initializer and public contracts own real strings, history arrays, clock and timers.
class WnEditor : public WnRootWidget, public WnEditorControl {
public:
  explicit WnEditor(i32 flags);
  bool GZOnCharacter(char character) override;
  bool GZOnSetFocus(WnWindow *window) override;
  bool GZOnKillFocus(WnWindow *window) override;
  bool GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) override;
  virtual void InitializeMemberVariables(i32 flags);
  virtual void IncrementInsertionPositionByWord(i32 delta);
  ~WnEditor() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::SetArea;
  void SetFillColor(u32 color) override;
  using WnRootWidget::SetFillColor;
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
  using WnRootWidget::SetFlag;
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
  std::vector<WnEditAction> undo, redo;
  FtFont *font;
  u32 font_colors[6], highlight_color, line_height, maximum_text, outline_color;
  WnScrollbarControl *vertical_scrollbar, *horizontal_scrollbar;
  GxBuffer *vertical_image, *horizontal_image;
  ScRect vertical_image_area, horizontal_image_area;
  ScStopwatch elapsed;
  u32 double_click_ms, caret_period, caret_color;
  bool caret_visible;
  WnEditorTimer caret_timer, scroll_timer;
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
  void AddFirstLineLength();
  bool AddUndoRecordForTextInsertion(const WnEditPosition &position, u32 index, const char *inserted, u32 count,
      const char *removed, u32 removed_count);
  bool AddUndoRecordForTextTextDeletion(const WnEditPosition &position, u32 index, const char *removed, u32 count);
  // Convert between signed local coordinates and byte-oriented line/column outputs.
  void GetLineAndColumnFromPosition(u32 *line, u32 *column, i32 x, i32 y);
  void GetPositionFromLineAndColumn(u32 line, u32 column, i32 *x, i32 *y);
  void GetColumnFromPosition(u32 line, u32 *column, i32 x);
  static void CaretTimer(WnEditor *editor);
  static void ScrollTimer(WnEditor *editor);
};

#endif
