#include "window/text_edit.h"

#include <assert.h>
#include <string.h>

#include "audio/api.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/utf8.h"
#include "fonts/sc3_bitmap_font.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/input.h"
#include "window/window.h"

WnEditor::WnEditor(i32 initial_flags) : elapsed(ScStopwatch::Milliseconds, 0) {
  editor_flags = 0;
  InitializeMemberVariables(initial_flags);
}

WnEditor::~WnEditor() {
  WnEditor::Shutdown();
}

// Resolve the missing font and reserve line storage before selecting the editable cursor, timer and language mode.
// An already initialized window returns immediately; inherited initialization follows the editor-specific effects.
bool WnEditor::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  if (!font)
    SetFont(WnGetUiResources()->GetFont(kWnFontDefault));
  lines.reserve(32);
  text_width = CalculateVisibleWidth();
  if (editor_flags & 1) {
    WnCursorManager *manager = owner->GetCursorManager();
    SetCursor(manager->GetIGZCursor(2), false);
  }
  if ((editor_flags & 0x40) && (editor_flags & 1) && !caret_timer.IsRunning())
    caret_timer.StartPeriodic(caret_period >> 1, nullptr);
  ScLanguageManager *manager = WnGetLanguageManager();
  language = manager ? manager->GetCurrentLanguage() : 1;
  double_byte = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
  return WnRootWidget::Init();
}

// Release the font, stop each running timer and wait in the original millisecond unit before releasing bars and images.
// Each pointer is cleared after its release callback; inherited window cleanup follows all editor-owned resources.
bool WnEditor::Shutdown() {
  ScRelease(&font);
  if (caret_timer.IsRunning()) {
    caret_timer.Stop();
    ScStopwatch waiting(ScStopwatch::Milliseconds, 0);
    waiting.Delay(100);
  }
  if (scroll_timer.IsRunning()) {
    scroll_timer.Stop();
    ScStopwatch waiting(ScStopwatch::Milliseconds, 0);
    waiting.Delay(100);
  }
  ScRelease(&vertical_scrollbar);
  ScRelease(&horizontal_scrollbar);
  ScRelease(&vertical_image);
  ScRelease(&horizontal_image);
  return WnRootWidget::Shutdown();
}

// Initialize editor state in the source order, including its real mask string, font roles and typed timer callbacks.
// Finish through the live signed flag setter so timer, cursor and layout changes retain their original order.
void WnEditor::InitializeMemberVariables(i32 requested) {
  WnGetGraphics();
  insert_mode = true;
  language = 0;
  double_byte = false;
  ignore_next_character = false;
  {
    ScString mask("-", 1);
    mask_text = mask;
  }
  maximum_text = UINT32_MAX;
  preferred_x = -1;
  gutter_x = 5;
  gutter_y = 0;
  line_height = 0;
  text_width = text_height = visible_rows = first_visible = 0;
  undo_limit = 0x4000;
  caret_color = 0;
  caret_visible = false;
  outline_color = 0;
  font = nullptr;
  vertical_scrollbar = horizontal_scrollbar = nullptr;
  vertical_image = horizontal_image = nullptr;
  vertical_image_area = {};
  horizontal_image_area = {};
  selection_finish = selection_start = insertion_index = insertion_column = insertion_line = 0;
  highlight_color = 0;
  WnRootWidget::SetFillColor(255, 255, 255);
  for (i32 i = 0; i < 6; ++i)
    ClearFontColor((WnFontColor)i);
  elapsed.SetResolution(ScStopwatch::Milliseconds);
  caret_period = 1000;
  double_click_ms = 500;
  caret_timer.SetCallbackFunction(CaretTimer, this);
  scroll_timer.SetCallbackFunction(ScrollTimer, this);
  SetFlags(requested);
}

// Normalize wrapping and single-line scroll flags, synchronize the caret timer, then relayout only the source change bits.
// Cursor selection follows invalidation and uses the requested editable bit with the borrowed manager cursor.
void WnEditor::SetFlags(i32 requested) {
  if (requested == editor_flags)
    return;
  u8 changed = (u8)(requested ^ editor_flags);
  editor_flags = requested;
  if (editor_flags & 2)
    editor_flags &= ~4;
  if (editor_flags & 0x200)
    editor_flags &= ~6;
  if ((editor_flags & 0x40) && (editor_flags & 1)) {
    if (!caret_timer.IsRunning())
      caret_timer.StartPeriodic(caret_period >> 1, nullptr);
  } else if (caret_timer.IsRunning())
    caret_timer.Stop();
  if (changed & 0x0B) {
    RecalculateAll();
    InvalidateSelfAndParents();
    if (requested & 1)
      SetCursor(owner->GetCursorManager()->GetIGZCursor(2), false);
    else
      SetCursor(nullptr, false);
  }
}

bool WnEditor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnEditorControl, WnEditorControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnEditor::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnEditor::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window from the actual secondary editor identity without another reference.
WnWindow *WnEditor::AsIGZWin() {
  return this;
}

void WnEditor::SetFillColor(u32 color) {
  WnRootWidget::SetFillColor(color);
  if (editor_flags & 0x20)
    InvalidateSelfAndParents();
}

bool WnEditor::SetFontColor(WnFontColor index, u32 color) {
  font_colors[(i32)index] = color;
  return true;
}

bool WnEditor::SetFontColor(u32 color) {
  font_colors[0] = color;
  return true;
}

bool WnEditor::GetFontColor(WnFontColor index, u32 *color) const {
  *color = font_colors[(i32)index];
  return true;
}

bool WnEditor::ClearFontColor(WnFontColor index) {
  switch (index) {
  case WnFontColor::Count:
    break;
  case WnFontColor::Foreground:
  case WnFontColor::DisabledForeground:
    font_colors[(i32)index] = 0;
    break;
  case WnFontColor::Background:
  case WnFontColor::DisabledBackground:
  case WnFontColor::HilitedBackground:
    font_colors[(i32)index] = kWnEditorBackgroundFromForeground;
    break;
  case WnFontColor::HilitedForeground:
    font_colors[(i32)index] = kWnEditorHighlightTextColor;
    break;
  }
  return true;
}

u32 WnEditor::GetHighlightFillColor() {
  return highlight_color;
}

u32 WnEditor::GetMaximumTextToAllow() {
  return maximum_text;
}

u32 WnEditor::GetFirstVisibleLine() {
  return first_visible;
}

u32 WnEditor::GetLineCount() {
  return (u32)lines.size();
}

u32 WnEditor::GetLineHeight() {
  return line_height;
}

u32 WnEditor::GetTextGutterX() {
  return gutter_x;
}

u32 WnEditor::GetTextGutterY() {
  return gutter_y;
}

// Return the concrete text's borrowed begin pointer, including a null begin for a never-allocated string.
const char *WnEditor::GetText() {
  return text.begin;
}

u32 WnEditor::GetTextLength() {
  return text.ScString::Strlen();
}

// Preserve the source's final rejection of every result at text length, including empty-needle end positions.
u32 WnEditor::FindText(const char *bytes, u32 count, u32 position, bool forward) {
  u32 length = text.ScString::Strlen();
  if (forward) {
    if (position >= length || count > length - position)
      return kWnInvalidIndex;
    if (!count)
      return position;
    for (u32 candidate = position; candidate <= length - count; ++candidate)
      if (!memcmp(text.begin + candidate, bytes, count))
        return candidate;
  } else {
    if (count > length)
      return kWnInvalidIndex;
    u32 candidate = position < length - count ? position : length - count;
    if (!count)
      return candidate < length ? candidate : kWnInvalidIndex;
    for (;;) {
      if (!memcmp(text.begin + candidate, bytes, count))
        return candidate;
      if (!candidate)
        break;
      --candidate;
    }
  }
  return kWnInvalidIndex;
}

// Read the next line-offset difference or the last line's remaining bytes, without stripping its newline byte.
// Empty text returns zero before any line-table access; nonempty text requires the source's populated offset table.
u32 WnEditor::GetCharacterCountOfLine(u32 line) {
  u32 length = text.ScString::Strlen();
  if (!length)
    return 0;
  if (line < (u32)lines.size() - 1)
    return lines[line + 1] - lines[line];
  return length - lines[(u32)lines.size() - 1];
}

// Convert a byte index through the actual line offsets. A trailing newline after the last stored offset yields count,0.
// Preserve column-before-line zeroing for empty input and line-before-column writes for an ordinary match.
void WnEditor::GetLineAndColumnFromIndex(u32 *line, u32 *column, u32 index) {
  index = ScUtf8Floor(text.begin, text.ScString::Strlen(), index);
  if (!(u32)lines.size() || !text.ScString::Strlen() || !index) {
    *column = 0;
    *line = 0;
    return;
  }
  u32 selected = 0;
  while (selected < (u32)lines.size() && lines[selected] <= index)
    ++selected;
  if (selected == (u32)lines.size() && text.begin[index - 1] == '\n') {
    *line = (u32)lines.size();
    *column = 0;
    return;
  }
  --selected;
  *line = selected;
  *column = index - lines[selected];
}

// Add a column to the selected offset, using the final stored line for out-of-range line indices.
bool WnEditor::GetIndexFromLineAndColumn(u32 line, u32 column, u32 *index) {
  u32 length = text.ScString::Strlen();
  if (!length) {
    *index = 0;
    return false;
  }
  u32 selected = line < (u32)lines.size() ? line : (u32)lines.size() - 1;
  *index = lines[selected] + column;
  if (*index <= length) {
    *index = ScUtf8Floor(text.begin, length, *index);
    return true;
  }
  *index = length;
  return false;
}

// Clamp the requested byte index to the current text, derive its line and column, then always invalidate the parent chain.
bool WnEditor::SetInsertionIndex(u32 index) {
  u32 length = text.ScString::Strlen();
  insertion_index = ScUtf8Floor(text.begin, length, index);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  InvalidateSelfAndParents();
  return true;
}

// Clamp a line/column against stored offsets and that line's full byte extent, then derive the insertion index.
// Empty line storage resets the three insertion fields; neither path adds an invalidation absent from the source.
bool WnEditor::SetInsertionLineAndColumn(u32 line, u32 column) {
  if (!(u32)lines.size()) {
    insertion_index = 0;
    insertion_column = 0;
    insertion_line = 0;
    return true;
  }
  u32 selected = line < (u32)lines.size() ? line : (u32)lines.size() - 1, length = GetCharacterCountOfLine(selected);
  insertion_line = selected;
  insertion_column = column < length ? column : length;
  GetIndexFromLineAndColumn(insertion_line, insertion_column, &insertion_index);
  insertion_column = insertion_index - lines[selected];
  return true;
}

// Clamp selection endpoints independently without sorting them, place insertion at the finish and always invalidate.
bool WnEditor::SetSelection(u32 start, u32 finish) {
  u32 length = text.ScString::Strlen();
  selection_finish = ScUtf8Floor(text.begin, length, finish);
  insertion_index = selection_finish;
  selection_start = ScUtf8Floor(text.begin, length, start);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  InvalidateSelfAndParents();
  return true;
}

// Move upward only from a positive signed first-line value, then synchronize the live bar before invalidating parents.
bool WnEditor::ScrollLineUp() {
  if ((i32)first_visible <= 0)
    return false;
  --first_visible;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Move downward while the signed remaining-row calculation is positive.
bool WnEditor::ScrollLineDown() {
  if ((i32)((u32)lines.size() - visible_rows - first_visible) <= 0)
    return false;
  ++first_visible;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Reset a nonzero first line, including a negative raw word, and synchronize the bar before parent invalidation.
bool WnEditor::ScrollToStart() {
  if (!first_visible)
    return false;
  first_visible = 0;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Jump to the last complete viewport only while the source signed remainder is positive; do not add clamping otherwise.
bool WnEditor::ScrollToEnd() {
  if ((i32)((u32)lines.size() - visible_rows - first_visible) <= 0)
    return false;
  first_visible = (u32)lines.size() - visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Subtract a page from a nonzero first line, choosing zero when the signed first-line value is below the page size.
bool WnEditor::ScrollPageUp() {
  if (!first_visible)
    return false;
  first_visible = (i32)first_visible < (i32)visible_rows ? 0 : first_visible - visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

bool WnEditor::ScrollPageDown() {
  if ((i32)((u32)lines.size() - visible_rows - first_visible) <= 0)
    return false;
  first_visible =
      first_visible >= (u32)lines.size() - visible_rows ? (u32)lines.size() - 1 : first_visible + visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Synchronize the optional live vertical control. Recalculation queries the editor's virtual count twice around SetRange.
void WnEditor::SetScrollBarValues(bool recalculate) {
  if (!vertical_scrollbar)
    return;
  if (recalculate && visible_rows) {
    u32 count = GetLineCount();
    i32 maximum = (i32)(count - visible_rows);
    if (maximum < 0)
      maximum = 0;
    vertical_scrollbar->SetRange(0, maximum);
    count = GetLineCount();
    vertical_scrollbar->SetDivisors(maximum, (i32)(count / visible_rows + 1));
  }
  vertical_scrollbar->SetValue((i32)first_visible);
}

void WnEditor::CaretTimer(WnEditor *editor) {
  WnGetWindowManager()->PostMessageToTarget(editor, kWnMsg_EditorCaret, 0, 0, 0);
}

void WnEditor::ScrollTimer(WnEditor *editor) {
  WnGetWindowManager()->PostMessageToTarget(editor, kWnMsg_EditorScroll, 0, 0, 0);
}

void WnEditor::SetCaretColor(u32 color) {
  caret_color = color;
}

void WnEditor::SetHighlightFillColor(u32 color) {
  highlight_color = color;
}

void WnEditor::SetMaximumTextToAllow(u32 value) {
  maximum_text = value;
}

// Set only the outline flag and its stored color, retaining every other editor flag and viewport field.
void WnEditor::SetOutlineUse(bool enabled, u32 color) {
  if (enabled)
    editor_flags |= 0x10;
  else
    editor_flags &= ~0x10u;
  outline_color = color;
}

bool WnEditor::SetInsertMode(bool enabled) {
  insert_mode = enabled;
  return true;
}

void WnEditor::GetText(ScStringView *output) {
  output->FromChar(text.begin, text.Strlen());
}

// Copy a complete UTF-8 prefix within the caller's byte capacity, without adding a terminator.
void WnEditor::GetText(char *output, u32 *length) {
  *length = ScUtf8Prefix(text.begin, text.ScString::Strlen(), *length);
  if (*length)
    memcpy(output, text.begin, *length);
}

// Rebuild the line-offset suffix through the real font fitter, retaining existing offsets before the requested line.
// Preserve the source's nonwrapped scan and terminal-newline behavior, including its remaining-span offset calculation.
void WnEditor::CalculateAllLineLengths(u32 first_line) {
  if (!font)
    return;
  if (first_line == kWnInvalidIndex)
    first_line = 0;
  u32 length = text.Strlen();
  if (!length) {
    lines.clear();
    insertion_index = insertion_column = insertion_line = 0;
    return;
  }
  u32 position;
  if (first_line)
    position = lines[first_line];
  else {
    if (!(u32)lines.size())
      lines.push_back(0);
    position = 0;
  }
  u32 count = first_line + 1;
  while (position < length) {
    u32 increment;
    if (editor_flags & 2)
      increment = font->WrapFit(text.begin + position, length - position, text_width, 1);
    else {
      while (text.begin[position] != '\n') {
        if (position >= length)
          break;
        ++position;
      }
      if (position + 1 < length)
        ++position;
      increment = length - position;
    }
    position += increment;
    if (position >= length && text.begin[position - 1] != '\n')
      break;
    ++count;
    AdjustLinePositionArray(increment, count);
  }
  if (count != (u32)lines.size())
    lines.resize(count, 0);
}

// The previous offset is read before growth, preserving the original caller's populated-prefix requirement.
void WnEditor::AdjustLinePositionArray(u32 increment, u32 count) {
  if (count <= (u32)lines.size()) {
    if (count <= 1)
      lines[0] = 0;
    else
      lines[count - 1] = increment + lines[count - 2];
  } else {
    u32 offset = increment + lines[count - 2];
    lines.resize(count, offset);
  }
}

void WnEditor::ClearFlag(i32 mask) {
  if (editor_flags & mask)
    SetFlags(editor_flags & ~mask);
}

// Measure the source viewport width after the live vertical window query, preserving wrapped subtraction of the two gutters.
u32 WnEditor::CalculateVisibleWidth() {
  i32 width = vertical_scrollbar ? vertical_scrollbar->GetWindow()->GetW() : 0;
  u32 window_width = (u32)GetW();
  return window_width - 2 * gutter_x - (u32)width;
}

// Preserve the original asymmetric height dependency: a horizontal bar enables subtraction of the vertical bar's height.
// A horizontal-only state dereferences an unavailable vertical receiver in the original and is outside defined execution.
u32 WnEditor::CalculateVisibleHeight() {
  i32 height = horizontal_scrollbar ? vertical_scrollbar->GetWindow()->GetH() : 0;
  u32 window_height = (u32)GetH();
  return window_height - 2 * gutter_y - (u32)height;
}

// Recompute the font height, viewport and line suffix before live scrollbar synchronization and caret normalization.
// Scrollbar visibility follows the first-visible clamp and may recursively relayout after creation or removal.
void WnEditor::RecalculateAll() {
  line_height = font ? font->Height(0) : 16;
  text_height = CalculateVisibleHeight();
  visible_rows = text_height / line_height;
  text_width = CalculateVisibleWidth();
  CalculateAllLineLengths(0);
  SetScrollBarValues(true);
  FixInsertionPositionIfBroken();
  if (!(u32)lines.size())
    first_visible = 0;
  else if (first_visible >= (u32)lines.size())
    first_visible = (u32)lines.size() - 1;
  TestForPossibleScrollBarAdditionOrRemoval();
}

// Normalize actual caret fields after reflow, clearing all five only for empty text and leaving a nonempty start untouched.
// Index conversion occurs before the finish clamp, matching the original state publication sequence.
void WnEditor::FixInsertionPositionIfBroken() {
  u32 length = text.Strlen();
  if (!length) {
    selection_finish = 0;
    selection_start = 0;
    insertion_index = 0;
    insertion_column = 0;
    insertion_line = 0;
    return;
  }
  if (insertion_index > length)
    insertion_index = length;
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  if (selection_finish > length)
    selection_finish = length;
}

// Each create/remove path owns recursive layout and reference updates; the original returns true on every path.
bool WnEditor::TestForPossibleScrollBarAdditionOrRemoval() {
  if (editor_flags & 8) {
    if ((u32)lines.size() <= visible_rows) {
      if (vertical_scrollbar)
        RemoveScrollBar(true, false);
    } else if (!vertical_scrollbar)
      AddScrollBar(true, false);
  }
  if ((editor_flags & 4) && !(editor_flags & 2)) {
    if ((i32)GetPixelWidthOfLongestLine(true) <= (i32)text_width) {
      if (horizontal_scrollbar)
        RemoveScrollBar(false, true);
    } else if (!horizontal_scrollbar)
      AddScrollBar(false, true);
  }
  return true;
}

// Create requested missing controls through the actual resource provider, publish each before image/child/focus callbacks.
// A factory miss clears its source flag through the virtual setter; any attempted change then relayouts and resizes.
bool WnEditor::AddScrollBar(bool vertical, bool horizontal) {
  bool changed = false;
  if (vertical && (editor_flags & 8) && !vertical_scrollbar) {
    changed = true;
    vertical_scrollbar = WnGetUiResources()->CreateScrollbar(0, 2, 0, 100, 0);
    if (vertical_scrollbar) {
      if (vertical_image) {
        const ScRect *rectangle =
            vertical_image_area.right > vertical_image_area.left && vertical_image_area.bottom > vertical_image_area.top
            ? &vertical_image_area
            : vertical_image->GetBufferArea();
        vertical_scrollbar->ImageSetup(*rectangle, vertical_image, 0);
      }
      WnWindow *window = vertical_scrollbar->GetWindow();
      ChildAdd(window);
      SetFocus();
    } else
      ClearFlag(8);
  }
  if (horizontal && (editor_flags & 4) && !horizontal_scrollbar) {
    changed = true;
    horizontal_scrollbar = WnGetUiResources()->CreateScrollbar(1, 1, 0, 100, 0);
    if (horizontal_scrollbar) {
      if (horizontal_image) {
        const ScRect *rectangle = horizontal_image_area.right > horizontal_image_area.left &&
                horizontal_image_area.bottom > horizontal_image_area.top
            ? &horizontal_image_area
            : horizontal_image->GetBufferArea();
        horizontal_scrollbar->ImageSetup(*rectangle, horizontal_image, 0);
      }
      WnWindow *window = horizontal_scrollbar->GetWindow();
      ChildAdd(window);
      SetFocus();
    } else
      ClearFlag(4);
  }
  if (changed) {
    RecalculateAll();
    SetScrollBarSizes();
    SetScrollBarValues(true);
  }
  return true;
}

// Remove a requested control's window child before releasing the live control field, then clear and recursively relayout.
// The source reads each field again after the child callback rather than retaining a second private owner.
void WnEditor::RemoveScrollBar(bool vertical, bool horizontal) {
  bool changed = false;
  if (vertical && vertical_scrollbar) {
    changed = true;
    WnWindow *window = vertical_scrollbar->GetWindow();
    ChildRemove(window);
    vertical_scrollbar->Release();
    vertical_scrollbar = nullptr;
  }
  if (horizontal && horizontal_scrollbar) {
    changed = true;
    WnWindow *window = horizontal_scrollbar->GetWindow();
    ChildRemove(window);
    horizontal_scrollbar->Release();
    horizontal_scrollbar = nullptr;
  }
  if (changed)
    RecalculateAll();
}

// Place each live scrollbar using two independent borrowed-window queries and the source's repeated live size calls.
// The vertical left edge includes the original extra pixel; horizontal placement preserves the current bar height.
void WnEditor::SetScrollBarSizes() {
  if (vertical_scrollbar) {
    WnWindow *measure = vertical_scrollbar->GetWindow(), *target = vertical_scrollbar->GetWindow();
    i32 bottom = GetH(), right = GetW();
    u32 left = 1u - (u32)measure->GetW();
    left += (u32)GetW();
    target->SetArea((i32)left, 0, right, bottom);
  }
  if (horizontal_scrollbar) {
    WnWindow *measure = horizontal_scrollbar->GetWindow(), *target = horizontal_scrollbar->GetWindow();
    i32 bottom = GetH(), right = GetW(), height = measure->GetH();
    i32 top = (i32)((u32)GetH() - (u32)height);
    target->SetArea(0, top, right, bottom);
  }
}

// Store root geometry first, then refresh text width, full layout and scrollbar placement before final invalidation.
// Repeated width and synchronization calls are retained because the source observes the live virtual window state.
void WnEditor::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  text_width = CalculateVisibleWidth();
  RecalculateAll();
  SetScrollBarSizes();
  SetScrollBarValues(true);
  InvalidateSelfAndParents();
}

// Publish the stored text bounds after capturing every source field, preserving defined output aliases.
void WnEditor::GetTextArea(ScRect *output) {
  u32 left = gutter_x, top = gutter_y, width = text_width, height = text_height;
  output->left = (i32)left;
  output->top = (i32)top;
  output->right = (i32)(left + width);
  output->bottom = (i32)(top + height);
}

// Retain the incoming face before releasing the live old field, including same-font replacement.
// Publish it before checking initialized state; only initialized editors relayout and invalidate here.
void WnEditor::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  if (flags & 0x4000) {
    RecalculateAll();
    InvalidateSelfAndParents();
  }
}

// Parent invalidation follows the source's initialized-bit check after all layout callbacks.
void WnEditor::SetTextGutterX(u32 value) {
  if (gutter_x == value)
    return;
  gutter_x = value;
  text_width = CalculateVisibleWidth();
  RecalculateAll();
  if (flags & 0x4000)
    InvalidateSelfAndParents();
}

// Store a changed vertical gutter without recomputing layout; initialized editors are invalidated afterward.
void WnEditor::SetTextGutterY(u32 value) {
  if (gutter_y == value)
    return;
  gutter_y = value;
  if (flags & 0x4000)
    InvalidateSelfAndParents();
}

// Round the current inner height down to full stored line heights, preserving unsigned gutter arithmetic.
bool WnEditor::ResizeWindowForExactLineHeights() {
  u32 gutters = 2 * gutter_y, height = (u32)GetH() - gutters;
  u32 fitted = line_height * (height / line_height);
  if (fitted != height)
    SetH((i32)(fitted + gutters));
  SetScrollBarSizes();
  SetScrollBarValues(true);
  InvalidateSelfAndParents();
  return true;
}

// Measure full line-byte extents through the real visible-width method, initializing its genuine output length each time.
// A false mode uses the original unclamped viewport range; only actual returned unsigned widths select the maximum.
u32 WnEditor::GetPixelWidthOfLongestLine(bool all_lines) {
  u32 first = all_lines ? 0 : first_visible, last = all_lines ? (u32)lines.size() : first_visible + visible_rows;
  u32 maximum = 0;
  for (u32 line = first; line < last; ++line) {
    const char *bytes = text.begin;
    u32 offset = lines[line];
    if (offset)
      bytes += offset;
    u32 length = GetCharacterCountOfLine(line), visible = length;
    u32 width = font->GetStringVisibleWidth(bytes, length, &visible);
    if (width > maximum)
      maximum = width;
  }
  return maximum;
}

// Construct the registered editor with its original flags and return the zero-reference primary interface.
ScUnknown *WnConstructEditor() {
  return static_cast<WnWindow *>(new WnEditor(0x1FF));
}

u32 WnEditor::NextCharSizeFromText(const char *bytes, u32 length) {
  return ScUtf8Decode(bytes, length, nullptr);
}

// Convert a local point into the original line and nearest measured byte column without changing the insertion state.
// Step from the current UTF-8 byte boundary before measuring each complete prefix.
void WnEditor::GetLineAndColumnFromPosition(u32 *line, u32 *column, i32 x, i32 y) {
  if (y < 0) {
    x = 0;
    y = 0;
  }
  if (x < 0)
    x = 0;
  i32 horizontal = (i32)((u32)x - gutter_x);
  *line = first_visible;
  *line += (u32)y / line_height;
  if (*line >= (u32)lines.size())
    *line = (u32)lines.size() - 1;
  if (text.begin == text.end) {
    *column = 0;
    *line = 0;
    return;
  }
  const char *begin = text.begin + lines[*line];
  i32 previous = 0;
  u32 length = GetCharacterCountOfLine(*line);
  *column = 0;
  if (!length)
    return;
  for (;;) {
    u32 remaining = text.ScString::Strlen() - lines[*line] - *column;
    u32 step = NextCharSizeFromText(begin + *column, remaining);
    *column += step;
    i32 width = font->Measure(begin, *column);
    if (horizontal < width) {
      i32 before = (i32)((u32)horizontal - (u32)previous - 1);
      i32 after = (i32)((u32)width - (u32)horizontal);
      if (before < after)
        *column -= step;
      return;
    }
    previous = width;
    if (*column >= length) {
      *column = length;
      return;
    }
  }
}

void WnEditor::StartScrollTimer() {
  if (!scroll_timer.IsRunning())
    scroll_timer.StartPeriodic(250, nullptr);
}

void WnEditor::StopScrollTimer() {
  if (scroll_timer.IsRunning())
    scroll_timer.Stop();
}

// Move insertion back one byte and column when it lies immediately after a newline within a nonzero column.
void WnEditor::MoveInsertionPositionToBeforeReturnIfNecessary() {
  if (insertion_index && insertion_column && text.begin[insertion_index - 1] == '\n') {
    --insertion_column;
    --insertion_index;
  }
}

// While editable and captured, start drag scrolling, select the measured point and publish the adjusted finish index.
// Editable movement without capture stops drag scrolling; every path returns the source's constant success.
bool WnEditor::OnMouseMove(i32 x, i32 y, u32) {
  if (editor_flags & 1) {
    if (owner->GetCapture() == this) {
      StartScrollTimer();
      u32 line, column;
      GetLineAndColumnFromPosition(&line, &column, x, y);
      SetInsertionLineAndColumn(line, column);
      MoveInsertionPositionToBeforeReturnIfNecessary();
      if (selection_finish != insertion_index) {
        selection_finish = insertion_index;
        InvalidateSelfAndParents();
      }
    } else
      StopScrollTimer();
  }
  return true;
}

// Expand the existing selection to adjacent space/newline word boundaries, updating insertion at the finish.
// Clicking the separator after a word first backs up one byte; consecutive separators return without expansion.
bool WnEditor::SelectCurrentWord() {
  if (text.begin == text.end)
    return false;
  const char *begin = text.begin;
  u32 index = insertion_index;
  char code = begin[index];
  if (code == ' ' || code == '\n') {
    if (!index)
      return true;
    code = begin[index - 1];
    if (code == ' ' || code == '\n')
      return true;
    insertion_index = index - 1;
    selection_finish = index - 1;
    selection_start = index - 1;
  }
  while (selection_start) {
    code = begin[selection_start - 1];
    if (code == ' ' || code == '\n')
      break;
    --selection_start;
  }
  while (selection_finish < text.ScString::Strlen()) {
    code = begin[selection_finish];
    if (code == ' ' || code == '\n')
      break;
    ++selection_finish;
    insertion_index = selection_finish;
  }
  FixInsertionPositionIfBroken();
  return true;
}

// Use the elapsed click clock before capture and point insertion, preserving shift selection and word expansion.
// Every editable press invalidates and resets the saved horizontal column, even if capture is refused.
bool WnEditor::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  if (!(editor_flags & 1))
    return true;
  bool second_click = false;
  if (!elapsed.IsTimerRunning() || elapsed.GetElapsedTime() >= double_click_ms)
    elapsed.Restart();
  else {
    second_click = true;
    elapsed.Reset();
  }
  owner->SetCapture(this);
  u32 line, column;
  GetLineAndColumnFromPosition(&line, &column, x, y);
  SetInsertionLineAndColumn(line, column);
  MoveInsertionPositionToBeforeReturnIfNecessary();
  selection_finish = insertion_index;
  if (!(modifiers & kWnModifierShift))
    selection_start = insertion_index;
  if (second_click)
    SelectCurrentWord();
  InvalidateSelfAndParents();
  preferred_x = -1;
  return true;
}

// Release this editable window's capture before stopping an active drag-scroll timer; coordinates are ignored.
bool WnEditor::OnMouseUpL(i32, i32, u32) {
  if ((editor_flags & 1) && owner->GetCapture() == this) {
    owner->ReleaseCapture(this);
    StopScrollTimer();
  }
  return true;
}

// Convert each complete signed32 wheel hundred into a line scroll while this window does not own capture.
// Scroll results are ignored, so all requested iterations run even after the viewport reaches its limit.
bool WnEditor::GZOnMouseWheel(i32, i32, u32, i32 wheel) {
  if (owner->GetCapture() != this) {
    i32 count = wheel > 0 ? wheel / 100 : wheel / -100;
    for (i32 i = 0; i < count; ++i)
      if (wheel > 0)
        ScrollLineUp();
      else
        ScrollLineDown();
  }
  return true;
}

// Forward Escape release to the notification target with the original key/modifier payload; other releases are handled.
bool WnEditor::OnKeyUp(u32 key, u32 modifiers) {
  if (key == kWnKeyEscape)
    return SendMsg(notification_target, kWnMsg_KeyUp, kWnKeyEscape, modifiers, 0);
  return true;
}

// Invalidate editable state on focus loss before preserving the root's unhandled result.
bool WnEditor::GZOnKillFocus(WnWindow *window) {
  string236.Erase(0, kScStringToEnd);
  if (editor_flags & 1)
    InvalidateSelfAndParents();
  return WnRootWidget::GZOnKillFocus(window);
}

// Invalidate editable state before the root's hidden-window focus redirection and unhandled result.
bool WnEditor::GZOnSetFocus(WnWindow *window) {
  if (editor_flags & 1)
    InvalidateSelfAndParents();
  return WnRootWidget::GZOnSetFocus(window);
}

// Find a clipped line-local UTF-8 span through whole-character measurements.
// Keep the original inclusive last-byte output and pixel offset without exposing partial scalar prefixes.
void WnEditor::FindFirstAndLastCharacterIndexForALineLongerThanTheScreenWidth(
    u32 line, u32 horizontal, u32 *first, u32 *last, u32 *offset) {
  u32 length = GetCharacterCountOfLine(line), column = 0;
  const char *bytes = text.begin + lines[line];
  u32 width = 0;
  while (column < length && width < horizontal) {
    u32 count = ScUtf8Decode(bytes + column, length - column, nullptr);
    assert(count);
    column += count;
    width = static_cast<u32>(font->Measure(bytes, column));
  }
  *offset = gutter_x - horizontal + width;
  *first = lines[line] + column;
  u32 visible = 0, available = text_width > *offset ? text_width - *offset : 0;
  while (column + visible < length) {
    u32 count = ScUtf8Decode(bytes + column + visible, length - column - visible, nullptr);
    assert(count);
    visible += count;
    if (static_cast<u32>(font->Measure(bytes + column, visible)) >= available)
      break;
  }
  *last = *first + visible - 1;
}

// Draw the captured viewport with original font colors, clipped selection fills and text, then the live focused caret/outline.
// Scrollbar reads update stored first-visible state after the drawing range is captured; unwrapped line offsets accumulate.
bool WnEditor::GZPaint() {
  u32 y = (u32)draw_area.top + gutter_y, x = gutter_x + (u32)draw_area.left;
  u32 first = first_visible, line = first, finish = visible_rows + first;
  u32 horizontal = 0;
  if (vertical_scrollbar)
    first_visible = (u32)vertical_scrollbar->GetValue();
  if (horizontal_scrollbar)
    horizontal = (u32)horizontal_scrollbar->GetValue();
  if (finish > (u32)lines.size())
    finish = (u32)lines.size();
  if ((editor_flags & 0x20) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  u32 saved_color = font->Color(), saved_end = font->EndColor();
  font->SetColors(
      font_colors[0], font_colors[1] == kWnEditorBackgroundFromForeground ? font_colors[0] : font_colors[1]);
  while (line < finish) {
    u32 line_start = lines[first], length;
    const char *bytes;
    if (horizontal_scrollbar || !(editor_flags & 2)) {
      u32 begin, end, offset;
      FindFirstAndLastCharacterIndexForALineLongerThanTheScreenWidth(line, horizontal, &begin, &end, &offset);
      x += offset;
      length = end - begin + 1;
      bytes = text.begin + begin;
    } else {
      length = GetCharacterCountOfLine(line);
      bytes = text.begin + line_start;
    }
    if (!length)
      break;
    if (bytes[length - 1] == '\n')
      --length;
    bool selected = selection_start != selection_finish;
    u32 low = selection_start < selection_finish ? selection_start : selection_finish;
    u32 high = selection_start < selection_finish ? selection_finish : selection_start;
    if (selected) {
      if (high <= line_start || low >= length + line_start)
        selected = false;
      else {
        if ((i32)line_start >= (i32)low)
          low = line_start;
        u32 end = length + line_start;
        if ((i32)high >= (i32)end)
          high = end;
        if (low == high)
          selected = false;
      }
    }
    if (!selected)
      font->DrawTextA(draw_buffer, x, y, bytes, length, &draw_area);
    else {
      u32 width = font->Measure(bytes, length), start_x = x, selected_length = length;
      const char *selected_bytes = bytes;
      bool partial = low > line_start || high < line_start + length;
      if (partial) {
        font->DrawTextA(draw_buffer, x, y, bytes, length, &draw_area);
        u32 prefix = low - line_start;
        u32 before = low == line_start ? 0 : font->Measure(bytes, prefix);
        selected_length = high - low;
        selected_bytes = bytes + prefix;
        u32 selected_width = font->Measure(selected_bytes, selected_length);
        start_x = x + before;
        width = selected_width;
      }
      ScRect selected_area{(i32)start_x, (i32)y, (i32)(start_x + width), (i32)(y + line_height)};
      if (draw_buffer->Lock(GxLockFlags::Fill)) {
        ScRect clipped;
        clipped.IntersectInclusive(&selected_area, &draw_area);
        draw_buffer->Fill(highlight_color, clipped, nullptr);
        draw_buffer->Unlock(GxLockFlags::Fill);
      }
      u32 previous = font->Color(), previous_end = font->EndColor();
      font->SetColors(
          font_colors[4], font_colors[5] == kWnEditorBackgroundFromForeground ? font_colors[4] : font_colors[5]);
      font->DrawTextA(draw_buffer, start_x, y, selected_bytes, selected_length, &draw_area);
      font->SetColors(previous, previous_end);
    }
    y += line_height;
    ++line;
    first = line;
  }
  font->SetColors(saved_color, saved_end);
  if (caret_visible && owner->GetFocus() == this) {
    u32 current = insertion_line, first_line = first_visible;
    if (current >= first_line && current < visible_rows + first_line) {
      u32 width = line_height >= 24 ? 2 : 1;
      const char *bytes = text.begin + lines[current];
      u32 measured = font->Measure(bytes, insertion_column);
      i32 right = draw_area.right, caret_x = (i32)(gutter_x + (u32)draw_area.left + measured);
      if (caret_x >= right)
        caret_x = (i32)((u32)right - width);
      u32 caret_y = (u32)draw_area.top + gutter_y + line_height * (insertion_line - first_visible);
      if (draw_buffer->Lock(GxLockFlags::Line)) {
        draw_buffer->DrawLineNoClip(
            caret_x, (i32)caret_y, caret_x, (i32)(caret_y + line_height), static_cast<i32>(caret_color), width);
        draw_buffer->Unlock(GxLockFlags::Line);
      }
    }
  }
  if ((editor_flags & 0x10) && owner->GetFocus() == this && draw_buffer->Lock(GxLockFlags::Rect)) {
    draw_buffer->DrawRectangleOutline(draw_area, outline_color, 1);
    draw_buffer->Unlock(GxLockFlags::Rect);
  }
  return true;
}

void WnEditor::ToggleCaretVisibility() {
  if (owner->GetFocus() == this) {
    caret_visible = !caret_visible;
    InvalidateSelfAndParents();
  }
}

// Scroll a captured pointer beyond the original three-pixel vertical thresholds, then reenter the real drag method.
void WnEditor::DoCursorScroll() {
  if (owner->GetCapture() != this)
    return;
  GetW();
  i32 height = GetH(), x, y;
  owner->GetCursorRelativePosition(this, &x, &y);
  if (y < -3) {
    if (ScrollLineUp())
      OnMouseMove(x, (i32)(line_height >> 1), 0);
  } else if (y >= (i32)((u32)height + 3))
    if (ScrollLineDown())
      OnMouseMove(x, (i32)((u32)height - (line_height >> 1)), 0);
}

// Consume the two actual editor timer messages and delegate every other message to the real root dispatcher.
bool WnEditor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_EditorCaret) {
    ToggleCaretVisibility();
    return true;
  }
  if (message->type == kWnMsg_EditorScroll) {
    DoCursorScroll();
    return true;
  }
  return WnRootWidget::DoMessage(message);
}

// Keep the insertion line within the vertical viewport, synchronizing only when its first row changes.
void WnEditor::PutInsertionPositionInView() {
  u32 first = insertion_line;
  if (first >= first_visible) {
    if (first < visible_rows + first_visible)
      return;
    first = first - visible_rows + 1;
  }
  first_visible = first;
  SetScrollBarValues(false);
}

// Copy the removed span to an optional genuine output before erasing, reflowing and adjusting the live viewport.
// Return the clamped byte count directly; positions at or past the text end return zero without effects.
u32 WnEditor::RemoveText(u32 index, u32 count, ScStringView *removed) {
  u32 length = text.ScString::Strlen();
  if (index >= length)
    return 0;
  u32 end = count > length - index ? length : index + count;
  index = ScUtf8Floor(text.begin, length, index);
  end = ScUtf8Floor(text.begin, length, end);
  count = end - index;
  if (removed)
    removed->FromChar(text.begin + index, count);
  text.Erase(index, count);
  insertion_index = index;
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, index);
  CalculateAllLineLengths(insertion_line - 1);
  SetScrollBarValues(true);
  i32 first = (i32)first_visible, excess = (i32)(first_visible + visible_rows - (u32)lines.size());
  if (excess > 0 && first > 0) {
    i32 reduce = first;
    if (first >= excess)
      reduce = excess;
    first_visible = (u32)first - (u32)reduce;
    SetScrollBarValues(false);
  }
  PutInsertionPositionInView();
  TestForPossibleScrollBarAdditionOrRemoval();
  InvalidateSelfAndParents();
  return count;
}

u32 WnEditor::PreviousCharSizeFromText(const char *bytes, u32 length) {
  return length ? ScUtf8PreviousSize(bytes - length, length) : 0;
}

// Move by complete UTF-8 scalars while storing caret and line/column positions as byte offsets.
void WnEditor::IncrementInsertionPosition(i32 delta) {
  u32 length = text.ScString::Strlen();
  if (delta > 0) {
    for (u32 count = static_cast<u32>(delta); count && insertion_index < length; --count)
      insertion_index += NextCharSizeFromText(text.begin + insertion_index, length - insertion_index);
  } else if (delta < 0) {
    for (u32 count = 0u - static_cast<u32>(delta); count && insertion_index; --count)
      insertion_index -= PreviousCharSizeFromText(text.begin + insertion_index, insertion_index);
  }
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
}

// Remove the character at the clamped existing byte position, optionally copying it through the genuine output interface.
// Retain both index conversions around reflow/synchronization and the final all-lines-fit viewport reset.
bool WnEditor::RemoveChar(u32 index, bool, ScStringView *removed) {
  u32 length = text.ScString::Strlen(), position = index;
  assert(length);
  position = ScUtf8Floor(text.begin, length, index >= length ? length - 1 : index);
  u32 count = NextCharSizeFromText(text.begin + position, text.ScString::Strlen() - position);
  if (removed)
    removed->FromChar(text.begin + position, count);
  text.Erase(position, count);
  insertion_index = position;
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, position);
  CalculateAllLineLengths(insertion_line - 1);
  SetScrollBarValues(true);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  i32 first = (i32)first_visible, excess = (i32)(first_visible + visible_rows - (u32)lines.size());
  if (excess > 0 && first > 0) {
    i32 reduce = first;
    if (first >= excess)
      reduce = excess;
    first_visible = (u32)first - (u32)reduce;
    SetScrollBarValues(false);
  }
  PutInsertionPositionInView();
  TestForPossibleScrollBarAdditionOrRemoval();
  if ((u32)lines.size() <= visible_rows) {
    first_visible = 0;
    SetScrollBarValues(false);
  }
  InvalidateSelfAndParents();
  return true;
}

// Store any of the four editor sound identifiers. Negative indices lie outside the original array's defined contract.
bool WnEditor::SetSound(i32 index, u32 incoming) {
  assert(index >= 0);
  if (index >= 4)
    return false;
  sounds[index] = incoming;
  return true;
}

// Resolve borrowed sound and resource services only for a configured sound, then forward the device's raw byte result.
// Sound fields remain unwritten until configured, as in the original constructor.
u8 WnEditor::PlaySound(u32 index) {
  u32 *sound = sounds + index;
  if (!*sound)
    return 0;
  AuSoundDevice *device = WnGetSoundDevice();
  if (!device)
    return 0;
  WnResources *resources = WnGetUiResources();
  if (!resources)
    return 0;
  i32 volume = resources->GetVolume();
  return device->SndPlay((i32)*sound, volume, 0);
}

bool WnEditor::IsCharOK(const char *bytes, u32 length, u32 *count) {
  *count = ScUtf8Decode(bytes, length, nullptr);
  return *count != 0;
}

// Filter complete UTF-8 scalars and the existing single-line newline restriction without splitting accepted text.
u32 WnEditor::RemoveBadCharacters(ScString *value) {
  u32 position = 0, length = value->ScString::Strlen(), removed = 0;
  ScString filtered;
  while (position < length) {
    u32 count;
    bool accepted = IsCharOK(value->begin + position, length - position, &count);
    if (!count) {
      removed += length - position;
      break;
    }
    if (!accepted || ((editor_flags & 0x200) && value->begin[position] == '\n'))
      removed += count;
    else
      filtered.Append(value->begin + position, count);
    position += count;
  }
  if (removed)
    *value = filtered;
  return removed;
}

void WnEditor::AddFirstLineLength() {
  if (!(u32)lines.size())
    lines.push_back(0);
}

// Insert validated UTF-8 at a scalar boundary and apply the existing byte capacity without cutting a character.
// Advance by the inserted byte extent before the original relayout and notification effects.
u32 WnEditor::InsertText(const char *bytes, u32 count, u32 index) {
  if (!ScUtf8Validate(bytes, count))
    return 0;
  ScString value(bytes, count);
  index = ScUtf8Floor(text.begin, text.ScString::Strlen(), index);
  RemoveBadCharacters(&value);
  count = value.ScString::Strlen();
  u32 available = text.ScString::Strlen() < maximum_text ? maximum_text - text.ScString::Strlen() : 0;
  if (count > available) {
    count = ScUtf8Prefix(value.begin, count, available);
    PlaySound(1);
  }
  if (!count)
    return 0;
  if (lines.empty())
    AddFirstLineLength();
  text.Replace(index, 0, value.begin, count);
  insertion_index = index + count;
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  i32 newline = -1;
  for (u32 i = 0; i < count; ++i)
    if (value.begin[i] == '\n') {
      newline = static_cast<i32>(i);
      break;
    }
  if (newline != 0 || (editor_flags & 2) || index < text.ScString::Strlen()) {
    u32 line, column;
    GetLineAndColumnFromIndex(&line, &column, index);
    CalculateAllLineLengths(line - 1);
    SetScrollBarValues(true);
  }
  PutInsertionPositionInView();
  TestForPossibleScrollBarAdditionOrRemoval();
  InvalidateSelfAndParents();
  return count;
}

// Record the inverse erase plus any replaced text, trimming a quarter of the oldest undo entries at the source limit.
// Preserve the unused inserted-byte argument and clear redo only after the complete action has been copied into undo.
bool WnEditor::AddUndoRecordForTextInsertion(
    const WnEditPosition &position, u32 index, const char *, u32 count, const char *removed, u32 removed_count) {
  if (!(editor_flags & 0x100))
    return false;
  if (undo.size() >= undo_limit)
    undo.erase(undo.begin(), undo.begin() + static_cast<isize>(undo.size() >> 2));
  WnEditAction action;
  action.erase = 1;
  action.erase_index = index;
  action.erase_count = count;
  if (removed && removed_count) {
    action.insert = 1;
    action.insert_index = index;
    action.text.ScString::FromChar(removed, removed_count);
  } else {
    action.insert = 0;
    action.insert_index = 0;
  }
  action.position = position;
  undo.push_back(action);
  redo.clear();
  return true;
}

// Record the removed bytes and saved caret as a future insertion, without applying insertion-history trimming.
// Clear the redo range after appending the copied action; disabled history leaves both arrays unchanged.
bool WnEditor::AddUndoRecordForTextTextDeletion(
    const WnEditPosition &position, u32 index, const char *removed, u32 count) {
  if (!(editor_flags & 0x100))
    return false;
  WnEditAction action;
  action.erase = 0;
  action.erase_index = 0;
  action.erase_count = 0;
  action.insert = 1;
  action.insert_index = index;
  action.text.ScString::FromChar(removed, count);
  action.position = position;
  undo.push_back(action);
  redo.clear();
  return true;
}

// Capture the opposite action before changing text, then restore the saved five-word caret state and retire the source action.
// Layout follows the history pop; inactive inverse fields are initialized rather than copying undefined stack bytes.
bool WnEditor::Undo() {
  if (!(editor_flags & 0x100) || !undo.size())
    return false;
  WnEditAction *action = &undo.back();
  WnEditAction inverse{};
  if (action->insert) {
    inverse.erase = 1;
    inverse.erase_index = action->insert_index;
    inverse.erase_count = action->text.ScString::Strlen();
  }
  if (action->erase) {
    inverse.insert = 1;
    inverse.insert_index = action->erase_index;
    inverse.text.ScString::FromChar(text.begin + action->erase_index, action->erase_count);
  }
  inverse.position = {insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
  redo.push_back(inverse);
  if (action->erase)
    text.Erase(action->erase_index, action->erase_count);
  if (action->insert)
    text.Replace(action->insert_index, 0, action->text.begin, action->text.ScString::Strlen());
  insertion_line = action->position.line;
  insertion_column = action->position.column;
  insertion_index = action->position.index;
  selection_start = action->position.selection_start;
  selection_finish = action->position.selection_finish;
  undo.pop_back();
  RecalculateAll();
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
  return true;
}

// Capture the opposite action before changing text, then restore the saved five-word caret state and retire the source action.
// Layout follows the history pop; inactive inverse fields are initialized rather than copying undefined stack bytes.
bool WnEditor::Redo() {
  if (!(editor_flags & 0x100) || !redo.size())
    return false;
  WnEditAction *action = &redo.back();
  WnEditAction inverse{};
  if (action->insert) {
    inverse.erase = 1;
    inverse.erase_index = action->insert_index;
    inverse.erase_count = action->text.ScString::Strlen();
  }
  if (action->erase) {
    inverse.insert = 1;
    inverse.insert_index = action->erase_index;
    inverse.text.ScString::FromChar(text.begin + action->erase_index, action->erase_count);
  }
  inverse.position = {insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
  undo.push_back(inverse);
  if (action->erase)
    text.Erase(action->erase_index, action->erase_count);
  if (action->insert)
    text.Replace(action->insert_index, 0, action->text.begin, action->text.ScString::Strlen());
  insertion_line = action->position.line;
  insertion_column = action->position.column;
  insertion_index = action->position.index;
  selection_start = action->position.selection_start;
  selection_finish = action->position.selection_finish;
  redo.pop_back();
  RecalculateAll();
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
  return true;
}

bool WnEditor::IsCharOK(u8 character) {
  return character >= 0x20 || character == 10;
}

// Assemble complete UTF-8 through the original char callback before changing selection, text or history.
// Snapshot completed input across editing callbacks; incomplete input stays in the existing pending string.
bool WnEditor::GZOnCharacter(char incoming) {
  u8 character = static_cast<u8>(incoming);
  bool control = keyboard->IsKeyDown(kWnKeyControl), alt = keyboard->IsKeyDown(kWnKeyAlt);
  if (ignore_next_character) {
    ignore_next_character = false;
    string236.Erase(0, kScStringToEnd);
    return true;
  }
  if (character == '\r')
    character = '\n';
  u32 scalar;
  if (!ScAppendUtf8Byte(&string236, character, &scalar))
    return true;
  ScString input(string236);
  string236.Erase(0, kScStringToEnd);
  if ((editor_flags & 1) && (scalar >= 0x20 || scalar == 10) && !control && !alt &&
      owner->GetCapture() != static_cast<WnWindow *>(this)) {
    if ((editor_flags & 0x200) && *input.begin == 10) {
      u32 index = insertion_index;
      selection_finish = index;
      selection_start = index;
      InvalidateSelfAndParents();
    } else {
      ScString removed;
      u32 start = selection_start, index = insertion_index;
      WnEditPosition position{insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
      if (start != selection_finish) {
        u32 first = selection_finish >= start ? start : selection_finish;
        u32 last = selection_start < selection_finish ? selection_finish : selection_start;
        index = first;
        RemoveText(first, last - first, &removed);
        SetInsertionIndex(index);
        u32 current = insertion_index;
        selection_finish = current;
        selection_start = current;
      }
      if (!insert_mode && !removed.ScString::Strlen() && insertion_index < text.ScString::Strlen())
        RemoveChar(insertion_index, false, &removed);
      if (InsertText(input.begin, input.ScString::Strlen(), insertion_index))
        AddUndoRecordForTextInsertion(
            position, index, input.begin, input.ScString::Strlen(), removed.begin, removed.ScString::Strlen());
      else
        PlaySound(1);
      u32 current = insertion_index;
      preferred_x = -1;
      selection_finish = current;
      selection_start = current;
      PutInsertionPositionInView();
    }
  }
  return false;
}

// Measure the clamped line prefix before publishing its x coordinate, then compute y from the live viewport fields.
void WnEditor::GetPositionFromLineAndColumn(u32 line, u32 column, i32 *x, i32 *y) {
  if (!(u32)lines.size()) {
    *x = (i32)gutter_x;
    *y = (i32)gutter_y;
    return;
  }
  if (line >= (u32)lines.size())
    line = (u32)lines.size() - 1;
  u32 first = lines[line];
  if (column + first >= text.ScString::Strlen())
    column = text.ScString::Strlen() - first;
  column = ScUtf8Prefix(text.begin + first, text.ScString::Strlen() - first, column);
  u32 width = font->Measure(text.begin + first, column);
  *x = (i32)(width + gutter_x);
  *y = (i32)(gutter_y + line_height * (line - first_visible));
}

// Select the character's left column whose measured right edge first reaches the requested signed x coordinate.
void WnEditor::GetColumnFromPosition(u32 line, u32 *column, i32 x) {
  if (!(u32)lines.size() || x <= (i32)gutter_x) {
    *column = 0;
    return;
  }
  *column = 0;
  u32 length = GetCharacterCountOfLine(line);
  const char *begin = text.begin + lines[line], *end = text.end;
  while (*column < length) {
    u32 count = NextCharSizeFromText(begin + *column, (u32)(end - begin) - *column);
    u32 width = font->Measure(begin, *column + count);
    if (x <= (i32)(width + gutter_x))
      return;
    *column += count;
  }
  *column = length;
}

// Advance across space/newline word boundaries, then clamp and convert the live insertion index.
void WnEditor::IncrementInsertionPositionByWord(i32 delta) {
  if (!text.ScString::Strlen() || !delta)
    return;
  if (delta > 0) {
    u32 moved = 0;
    for (;;) {
      u32 position = insertion_index, last = text.ScString::Strlen() - 1;
      if ((i32)position >= (i32)last) {
        insertion_index = text.ScString::Strlen();
        break;
      }
      const char *begin = text.begin;
      while ((i32)position < (i32)last) {
        if ((begin[position] == ' ' || begin[position] == '\n') && begin[position + 1] != ' ' &&
            begin[position + 1] != '\n')
          break;
        ++position;
      }
      if ((i32)position >= (i32)last) {
        insertion_index = text.ScString::Strlen();
        break;
      }
      ++moved;
      insertion_index = position + 1;
      if (moved >= (u32)delta)
        break;
    }
  }
  if ((i32)insertion_index < 0)
    insertion_index = 0;
  else if (insertion_index > text.ScString::Strlen())
    insertion_index = text.ScString::Strlen();
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
}

// Dispatch source navigation, deletion, insert-mode and history commands through the real editor methods.
// Preserve separate vertical saved-column behavior, boundary sounds, and Escape's live target reply.
bool WnEditor::OnKeyDown(u32 key, u32 modifiers) {
  string236.Erase(0, kScStringToEnd);
  bool shift = (modifiers & kWnModifierShift) != 0;
  switch (key) {
  case kWnKeyEscape: {
    if (!(editor_flags & 1))
      goto UndoCommand;
    if (selection_start != selection_finish) {
      u32 index = insertion_index;
      selection_finish = index;
      selection_start = index;
      InvalidateSelfAndParents();
    }
    return SendMsg(notification_target, kWnMsg_KeyDown, kWnKeyEscape, modifiers, 0);
  }
  case kWnKeyY:
    if ((modifiers & kWnModifierControl) && !(modifiers & kWnModifierAlt)) {
      if (redo.size())
        Redo();
      else
        PlaySound(3);
    }
    return true;
  case kWnKeyZ:
    goto UndoCommand;
  case kWnKeyInsert:
    if ((editor_flags & 1) && (editor_flags & 0x80))
      insert_mode = !insert_mode;
    return true;
  case kWnKeyBackspace:
  case kWnKeyDelete: {
    if (!(editor_flags & 1))
      return true;
    if (selection_start != selection_finish) {
      u32 first = selection_finish >= selection_start ? selection_start : selection_finish;
      u32 last = selection_start >= selection_finish ? selection_start : selection_finish;
      u32 count = last - first;
      ScString removed;
      WnEditPosition position{insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
      u32 erased = RemoveText(first, count, &removed);
      if (erased)
        AddUndoRecordForTextTextDeletion(position, first, removed.begin, erased);
      else
        PlaySound(1);
      SetInsertionIndex(first);
    } else if (key == kWnKeyBackspace ? insertion_index != 0 : insertion_index < text.ScString::Strlen()) {
      ScString removed;
      WnEditPosition position{insertion_line, insertion_column, insertion_index, selection_start, selection_finish};
      u32 index = insertion_index;
      if (key == kWnKeyBackspace)
        index = insertion_index - PreviousCharSizeFromText(text.begin + index, index);
      if (RemoveChar(index, key == kWnKeyBackspace, &removed))
        AddUndoRecordForTextTextDeletion(position, index, removed.begin, removed.ScString::Strlen());
      else
        PlaySound(1);
    } else
      PlaySound(1);
    u32 index = insertion_index;
    selection_finish = index;
    selection_start = index;
    goto ResetColumn;
  }
  case kWnKeyLeft:
  case kWnKeyRight: {
    if (!(editor_flags & 1))
      return true;
    bool move = key == kWnKeyLeft ? insertion_index != 0 : insertion_index < text.ScString::Strlen();
    if (move) {
      i32 delta = key == kWnKeyLeft ? -1 : 1;
      if (modifiers & kWnModifierControl)
        IncrementInsertionPositionByWord(delta);
      else
        IncrementInsertionPosition(delta);
    } else if (selection_start == selection_finish) {
      PlaySound(2);
      goto ResetColumn;
    }
    goto SelectionChanged;
  }
  case kWnKeyHome: {
    if (!(editor_flags & 1))
      return true;
    u32 column = insertion_column;
    if (column || selection_start != selection_finish) {
      if (modifiers & kWnModifierControl) {
        insertion_index = 0;
        GetLineAndColumnFromIndex(&insertion_line, &insertion_column, 0);
      } else if (column) {
        insertion_column = 0;
        GetIndexFromLineAndColumn(insertion_line, 0, &insertion_index);
      }
      goto SelectionChanged;
    }
    goto ResetColumn;
  }
  case kWnKeyEnd: {
    if (!(editor_flags & 1))
      return true;
    u32 length = GetCharacterCountOfLine(insertion_line), column = insertion_column;
    if (column != length || selection_start != selection_finish) {
      if (modifiers & kWnModifierControl) {
        u32 index = text.ScString::Strlen();
        insertion_index = index;
        GetLineAndColumnFromIndex(&insertion_line, &insertion_column, index);
      } else if (column != length) {
        insertion_column = length;
        GetIndexFromLineAndColumn(insertion_line, length, &insertion_index);
        MoveInsertionPositionToBeforeReturnIfNecessary();
      }
      goto SelectionChanged;
    }
    goto ResetColumn;
  }
  case kWnKeyPageUp:
  case kWnKeyPageDown:
  case kWnKeyUp:
  case kWnKeyDown: {
    if (!(editor_flags & 1))
      return true;
    u32 line = insertion_line;
    bool boundary = key == kWnKeyPageUp || key == kWnKeyUp ? line == 0 : line >= (u32)lines.size() - 1;
    if (boundary) {
      if (selection_start != selection_finish)
        goto VerticalSelectionChanged;
      PlaySound(2);
      return true;
    }
    i32 x, y;
    GetPositionFromLineAndColumn(line, insertion_column, &x, &y);
    if (key == kWnKeyUp)
      --insertion_line;
    else if (key == kWnKeyDown)
      ++insertion_line;
    else if (key == kWnKeyPageUp) {
      u32 current = insertion_line, rows = visible_rows;
      insertion_line = current < rows ? 0 : current - rows;
    } else {
      u32 rows = visible_rows, current = insertion_line;
      insertion_line = current >= (u32)lines.size() - rows ? (u32)lines.size() - 1 : current + rows;
    }
    if (preferred_x == -1)
      preferred_x = x;
    GetColumnFromPosition(
        insertion_line, &insertion_column, key == kWnKeyPageUp || key == kWnKeyPageDown ? x : preferred_x);
    GetIndexFromLineAndColumn(insertion_line, insertion_column, &insertion_index);
    MoveInsertionPositionToBeforeReturnIfNecessary();
    goto VerticalSelectionChanged;
  }
  default:
    return true;
  }
UndoCommand:
  if ((modifiers & kWnModifierControl) && !(modifiers & kWnModifierAlt)) {
    if (undo.size())
      Undo();
    else
      PlaySound(3);
  }
  return true;
SelectionChanged: {
  u32 index = insertion_index;
  selection_finish = index;
  if (!shift)
    selection_start = index;
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
}
ResetColumn:
  preferred_x = -1;
  return true;
VerticalSelectionChanged: {
  u32 index = insertion_index;
  selection_finish = index;
  if (!shift)
    selection_start = index;
  PutInsertionPositionInView();
  InvalidateSelfAndParents();
  return true;
}
}

// Reject malformed UTF-8 before replacing text, then filter and truncate only at complete scalar boundaries.
// Retain invalidation before history retirement and clear pending input when replacing the string.
bool WnEditor::SetText(const char *bytes, u32 count) {
  if (!ScUtf8Validate(bytes, count))
    return false;
  ScString value(bytes, count);
  text.Erase(0, kScStringToEnd);
  RemoveBadCharacters(&value);
  u32 length = value.ScString::Strlen();
  if (length > maximum_text) {
    length = ScUtf8Prefix(value.begin, length, maximum_text);
    PlaySound(1);
  }
  string236.Erase(0, kScStringToEnd);
  if (!length) {
    text.Erase(0, kScStringToEnd);
    RecalculateAll();
  } else {
    text.ScString::FromChar(value.begin, length);
    CalculateAllLineLengths(0);
    SetScrollBarValues(true);
    insertion_index = insertion_line = insertion_column = 0;
    selection_start = selection_finish = 0;
    TestForPossibleScrollBarAdditionOrRemoval();
  }
  InvalidateSelfAndParents();
  undo.clear();
  redo.clear();
  return true;
}

// Read the incoming interface's length before its Data pointer and forward the installed two-argument byte setter.
bool WnEditor::SetText(const ScStringView &value) {
  u32 length = value.Strlen();
  const char *bytes = value.Data();
  return SetText(bytes, length);
}

// Convert the requested line and column into the genuine index output, ignoring the lookup Boolean before insertion.
u32 WnEditor::InsertText(const char *bytes, u32 count, u32 line, u32 column) {
  u32 index;
  GetIndexFromLineAndColumn(line, column, &index);
  return InsertText(bytes, count, index);
}

u32 WnEditor::RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) {
  u32 index;
  GetIndexFromLineAndColumn(line, column, &index);
  return RemoveText(index, count, removed);
}

// Convert both endpoints and preserve the first endpoint as the erase start even when the range is reversed.
// The installed method ignores its fifth output argument and explicitly forwards a null removed-text output.
u32 WnEditor::RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *) {
  u32 first, last;
  GetIndexFromLineAndColumn(from_line, from_column, &first);
  GetIndexFromLineAndColumn(to_line, to_column, &last);
  return RemoveText(first, first <= last ? last - first : first - last, nullptr);
}

// Add flags only when none of the requested bits are currently present; retain the original partial-overlap no-op.
// The original signed flag argument and live SetFlags dispatch preserve all32 flag bits.
void WnEditor::SetFlag(i32 requested) {
  i32 current = editor_flags;
  if (!(current & requested))
    SetFlags(current | requested);
}

// Store the source's250..10000 millisecond period and restart caret blinking only on an initialized window.
// Restart uses the actual clear/add flag virtuals, preserving their timer and layout side effects.
bool WnEditor::SetCaretPeriod(u32 milliseconds) {
  caret_period = milliseconds;
  if (milliseconds < 250)
    caret_period = 250;
  if (caret_period > 10000)
    caret_period = 10000;
  if (GetFlag(kWnFlagInitialized)) {
    ClearFlag(0x40);
    SetFlag(0x40);
  }
  return true;
}

bool WnEditor::IsFlagSet(i32 mask) {
  return (mask & editor_flags) != 0;
}

i32 WnEditor::GetFlags() {
  return editor_flags;
}

void WnEditor::SetMaxUndoLevelCount(u32 count) {
  undo_limit = count;
}

// Retain the incoming vertical atlas before releasing the live old image, then configure an existing scrollbar.
// The original ignores its rectangle argument and uses the stored rectangle or the incoming buffer's area.
bool WnEditor::SetScrollBarImageVertical(GxBuffer *incoming, const ScRect &) {
  if (incoming)
    incoming->AddRef();
  if (vertical_image)
    vertical_image->Release();
  vertical_image = incoming;
  if (incoming && vertical_scrollbar) {
    if (vertical_image_area.right > vertical_image_area.left && vertical_image_area.bottom > vertical_image_area.top)
      vertical_scrollbar->ImageSetup(vertical_image_area, incoming, 0);
    else {
      const ScRect *bounds = incoming->GetBufferArea();
      vertical_scrollbar->ImageSetup(*bounds, incoming, 0);
    }
  }
  return true;
}

// Replace the retained horizontal atlas before applying it through the live scrollbar, ignoring its setup reply.
// Preserve the unused rectangle input and reload the scrollbar after the fallback buffer-area call.
bool WnEditor::SetScrollBarImageHorizontal(GxBuffer *incoming, const ScRect &) {
  if (incoming)
    incoming->AddRef();
  if (horizontal_image)
    horizontal_image->Release();
  horizontal_image = incoming;
  if (incoming && horizontal_scrollbar) {
    if (horizontal_image_area.right > horizontal_image_area.left &&
        horizontal_image_area.bottom > horizontal_image_area.top)
      horizontal_scrollbar->ImageSetup(horizontal_image_area, incoming, 0);
    else {
      const ScRect *bounds = incoming->GetBufferArea();
      horizontal_scrollbar->ImageSetup(*bounds, incoming, 0);
    }
  }
  return true;
}

static ScLock WnEditorTimerLock;
static ScArray<WnEditorTimer *> WnEditorTimers;

// Locate a registered timer by identity while the caller holds the original recursive registry lock.
static u32 WnFindEditorTimer(WnEditorTimer *timer) {
  for (u32 i = 0; i < WnEditorTimers.size; ++i)
    if (WnEditorTimers.data[i] == timer)
      return i;
  return WnEditorTimers.size;
}

// Initialize inactive timing state before registering this live timer under the shared recursive lock.
// The source leaves its callback context unwritten until SetCallbackFunction, so construction does not synthesize one.
WnEditorTimer::WnEditorTimer()
    : owner(nullptr), callback(nullptr), resolution(100), interval(1000), running(false), once(false), identifier(0) {
  WnEditorTimerLock.Lock();
  *WnEditorTimers.Append() = this;
  WnEditorTimerLock.Unlock();
}

// Remove registry membership before stopping an active schedule, then release any remaining owner and clear callback state.
WnEditorTimer::~WnEditorTimer() {
  WnEditorTimerLock.Lock();
  u32 index = WnFindEditorTimer(this);
  if (index != WnEditorTimers.size) {
    memmove(WnEditorTimers.data + index, WnEditorTimers.data + index + 1,
        (WnEditorTimers.size - index - 1) * sizeof(WnEditorTimer *));
    --WnEditorTimers.size;
  }
  WnEditorTimerLock.Unlock();
  if (running)
    WnEditorTimer::Stop();
  ScUnknown *remaining = owner;
  if (remaining) {
    remaining->Release();
    owner = nullptr;
  }
  callback = nullptr;
  context = nullptr;
}

// Retain the captured owner and snapshot running before changing resolution, then restart through live virtual methods.
// The final conditional release tests the live owner but releases the captured identity, matching the source ordering.
bool WnEditorTimer::SetResolution(u32 value) {
  ScUnknown *saved = owner;
  if (saved)
    saved->AddRef();
  bool active = running;
  resolution = value;
  if (active) {
    Stop();
    if (once)
      StartOneShot(0, nullptr);
    else
      StartPeriodic(0, nullptr);
  }
  if (saved) {
    if (owner)
      saved->Release();
    owner = saved;
  }
  return true;
}

bool WnEditorTimer::GetResolution(u32 *value) {
  *value = resolution;
  return true;
}

// Capture running before changing the stored repeat interval, protecting the saved owner through the virtual restart.
// Stop may clear the one-shot flag before the restart decision; do not cache its earlier value.
bool WnEditorTimer::SetPeriod(u32 value) {
  ScUnknown *saved = owner;
  if (saved)
    saved->AddRef();
  bool active = running;
  interval = value;
  if (active) {
    Stop();
    if (once)
      StartOneShot(0, nullptr);
    else
      StartPeriodic(0, nullptr);
  }
  if (saved) {
    if (owner)
      saved->Release();
    owner = saved;
  }
  return true;
}

bool WnEditorTimer::GetPeriod(u32 *value) {
  *value = interval;
  return true;
}

// Start only an inactive timer with a callback, retaining the supplied owner before publishing running state.
// A nonzero interval replaces the stored interval; failed scheduling retires the supplied hold and clears the live owner.
bool WnEditorTimer::StartPeriodic(u32 requested, ScUnknown *incoming) {
  if (running || !callback)
    return false;
  if (incoming) {
    incoming->AddRef();
    owner = incoming;
  }
  if (requested)
    interval = requested;
  running = true;
  identifier = WnTimers.AddEvent(InternalCallbackFunction, this, interval, 0, 0);
  if (!identifier) {
    running = false;
    if (incoming)
      incoming->Release();
    owner = nullptr;
    return false;
  }
  return true;
}

// Schedule one shot using the supplied delay or stored interval without replacing that interval.
// Publish both mode bytes before scheduling; failure clears both and releases only the supplied owner hold.
bool WnEditorTimer::StartOneShot(u32 delay, ScUnknown *incoming) {
  if (running || !callback)
    return false;
  if (!delay)
    delay = interval;
  if (incoming) {
    incoming->AddRef();
    owner = incoming;
  }
  running = true;
  once = true;
  identifier = WnTimers.AddEvent(InternalCallbackFunction, this, delay, 1, 0);
  if (!identifier) {
    running = false;
    once = false;
    if (incoming)
      incoming->Release();
    owner = nullptr;
    return false;
  }
  return true;
}

// Remove a nonzero schedule before releasing its live owner and clearing the identifier and mode bytes.
// A zero identifier leaves every field untouched, even if a reentrant caller changed the mode bytes.
bool WnEditorTimer::Stop() {
  if (identifier) {
    WnTimers.RemoveEvent(identifier);
    ScUnknown *held = owner;
    if (held) {
      held->Release();
      owner = nullptr;
    }
    identifier = 0;
    once = false;
    running = false;
  }
  return true;
}

bool WnEditorTimer::IsRunning() {
  return running;
}

// Publish the actual editor callback and its typed receiver together without stopping or restarting the schedule.
// Store the function before its context; pending schedule state is unchanged.
bool WnEditorTimer::SetCallbackFunction(WnEditorTimerCallback incoming, WnEditor *editor) {
  callback = incoming;
  context = editor;
  return true;
}

// Capture a registered timer's callback and owner under the registry lock, then invoke outside it.
// Read context only for a captured callback; retire one-shot state before delivery and retain the null-callback owner hold.
u32 WnEditorTimer::InternalCallbackFunction(WnEditorTimer *timer) {
  WnEditorTimerLock.Lock();
  if (WnFindEditorTimer(timer) == WnEditorTimers.size) {
    WnEditorTimerLock.Unlock();
    return 0;
  }
  ScUnknown *saved = timer->owner;
  WnEditorTimerCallback invoke = timer->callback;
  WnEditor *editor = invoke ? timer->context : nullptr;
  bool one_shot = timer->once;
  if (saved)
    saved->AddRef();
  if (one_shot) {
    ScUnknown *live = timer->owner;
    if (live) {
      live->Release();
      timer->owner = nullptr;
    }
    timer->running = false;
    timer->once = false;
    timer->identifier = 0;
  }
  WnEditorTimerLock.Unlock();
  if (invoke) {
    invoke(editor);
    if (saved)
      saved->Release();
  }
  return 0;
}

// Initialize the actual editing fields before minimum-size layout, then apply the provider background and color defaults.
WnTextEdit::WnTextEdit(u8 initial_editable)
    : double_byte(false), position(0), selection_start(0), selection_finish(0), first_visible(0), language(0), text(),
      changing_text(false), font(nullptr), text_height(1), caret_width(1), border(1), selection_color(kWnDefaultColor),
      opacity(1.0f), background(1), editable(initial_editable), limit(UINT32_MAX), margins{2, 0, 2, 0} {
  SetArea(0, 0, 128, 64);
  SetFillColor(WnGetUiResources()->GetColor(21));
  for (u32 &color : text_colors)
    color = kWnDefaultColor;
  for (u32 &color : border_colors)
    color = 0;
  caret_character = '|';
  caret_visible = false;
}

WnTextEdit::~WnTextEdit() {
  Shutdown();
}

bool WnTextEdit::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnTextControl, WnTextControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnTextEdit::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnTextEdit::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed canonical window from the text-control interface.
WnWindow *WnTextEdit::AsIGZWin() {
  return this;
}

// Resolve missing resource defaults before selecting the text cursor and language-specific caret, then resize and initialize.
bool WnTextEdit::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  first_visible = 0;
  WnResources *resources = WnGetUiResources();
  if (resources) {
    if (!font)
      SetFont(resources->GetFont(kWnFontDefault));
    if (text_colors[1] == kWnDefaultColor)
      SetFontColor(WnInputColor::Disabled, resources->GetColor(24));
    if (text_colors[2] == kWnDefaultColor)
      SetFontColor(WnInputColor::Hilited, resources->GetColor(25));
    if (GetFillColor() == kWnDefaultColor)
      SetFillColor(resources->GetColor(21));
    if (selection_color == kWnDefaultColor)
      SetHighlightFillColor(resources->GetColor(22));
  }
  if (editable) {
    WnCursorManager *manager = owner->GetCursorManager();
    WnCursor *selected = manager->GetIGZCursor(2);
    SetCursor(selected, false);
  }
  ScLanguageManager *manager = WnGetLanguageManager();
  language = manager ? manager->GetCurrentLanguage() : 1;
  double_byte = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
  caret_character = double_byte ? 31 : '|';
  i32 height = GetH();
  i32 width = GetW();
  SetSize(width, height);
  return WnRootWidget::Init();
}

// Hide the live window before clearing its font through the virtual setter and closing the inherited window state.
bool WnTextEdit::Shutdown() {
  HideWindow();
  SetFont(nullptr);
  return WnRootWidget::Shutdown();
}

// Clamp both requested dimensions to the live text-control extent before delegating the stored area and child updates.
void WnTextEdit::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  ScRect extent;
  GetMinMaxInfo(&extent);
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width < extent.left)
    width = extent.left;
  if (height < extent.top)
    height = extent.top;
  WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
}

// Update the editable cursor before the root enable flag changes; disabling clears the cursor through the live window view.
u32 WnTextEdit::SetFlag(u32 mask, bool enabled) {
  if (mask == kWnFlagEnabled) {
    if (enabled) {
      if (editable) {
        WnCursorManager *manager = owner->GetCursorManager();
        WnCursor *selected = manager->GetIGZCursor(2);
        SetCursor(selected, false);
      }
    } else
      AsIGZWin()->SetCursor(nullptr, true);
  }
  return WnRootWidget::SetFlag(mask, enabled);
}

// Retain the incoming font before releasing the old one. A null font restores the one-pixel text and caret extents.
bool WnTextEdit::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  if (incoming) {
    text_height = incoming->Height(0);
    caret_width = font->Width((u32)(i32)caret_character);
  } else {
    text_height = 1;
    caret_width = 1;
  }
  return true;
}

// Store one of the three explicit text colors and invalidate only when its word changes.
bool WnTextEdit::SetFontColor(WnInputColor index, u32 color) {
  assert((i32)index >= 0);
  if ((i32)index >= 3)
    return false;
  u32 old = text_colors[(i32)index];
  text_colors[(i32)index] = color;
  if (old != color)
    InvalidateSelfAndParents();
  return true;
}

// Copy one explicit text-color word, leaving the output untouched when the source upper bound rejects the index.
bool WnTextEdit::GetFontColor(WnInputColor index, u32 *out) const {
  assert((i32)index >= 0);
  if ((i32)index >= 3)
    return false;
  *out = text_colors[(i32)index];
  return true;
}

// Preserve the raw background byte and dirty the same parent chain as the original setter.
bool WnTextEdit::SetFilled(u8 enabled) {
  background = enabled;
  InvalidateSelfAndParents();
  return true;
}

u8 WnTextEdit::IsFilled() const {
  return background;
}

bool WnTextEdit::SetOutlineType(WnInputOutline mode) {
  border = (i32)mode;
  return true;
}

// Store a border-color word and invalidate only on change. Negative indices are outside the defined source array contract.
bool WnTextEdit::SetOutlineColor(WnInputEdge index, u32 color) {
  assert((i32)index >= 0);
  if ((i32)index >= 4)
    return false;
  u32 old = border_colors[(i32)index];
  border_colors[(i32)index] = color;
  if (old != color)
    InvalidateSelfAndParents();
  return true;
}

// Copy the selected border color without changing its output on the original upper-bound failure.
bool WnTextEdit::GetOutlineColor(WnInputEdge index, u32 *out) const {
  assert((i32)index >= 0);
  if ((i32)index >= 4)
    return false;
  *out = border_colors[(i32)index];
  return true;
}

bool WnTextEdit::SetHighlightFillColor(u32 color) {
  selection_color = color;
  InvalidateSelfAndParents();
  return true;
}

bool WnTextEdit::GetHighlightFillColor(u32 *out) const {
  *out = selection_color;
  return true;
}

// Clamp negative or unordered opacity to zero and values above one to one, then invalidate.
bool WnTextEdit::SetHighlightFillAlpha(f32 requested) {
  opacity = requested;
  if (!(requested >= 0.0f))
    opacity = 0.0f;
  if (opacity > 1.0f)
    opacity = 1.0f;
  InvalidateSelfAndParents();
  return true;
}

bool WnTextEdit::GetHighlightFillAlpha(f32 *out) const {
  *out = opacity;
  return true;
}

// Clamp a requested byte caret offset to a complete UTF-8 boundary and collapse the selection there.
bool WnTextEdit::SetCursorLoc(u32 requested) {
  position = ScUtf8Floor(text.begin, text.Strlen(), requested);
  selection_start = position;
  selection_finish = position;
  return true;
}

u8 WnTextEdit::IsEditable() const {
  return editable;
}

// Preserve the raw editable byte before selecting the owner's text cursor, or clear the cursor with activation requested.
bool WnTextEdit::SetEditable(u8 enabled) {
  editable = enabled;
  if (enabled) {
    WnCursorManager *manager = owner->GetCursorManager();
    WnCursor *selected = manager->GetIGZCursor(2);
    SetCursor(selected, false);
  } else
    SetCursor(nullptr, true);
  return true;
}

bool WnTextEdit::SetCharacterLimit(u32 requested) {
  limit = requested;
  return true;
}

// Store the four signed margin bytes in argument order before invalidating the parent chain.
bool WnTextEdit::SetGutters(i8 left, i8 top, i8 right, i8 bottom) {
  margins[0] = left;
  margins[1] = top;
  margins[2] = right;
  margins[3] = bottom;
  InvalidateSelfAndParents();
  return true;
}

// Publish the current margin bytes in source order through four distinct output parameters.
bool WnTextEdit::GetGutters(i8 *left, i8 *top, i8 *right, i8 *bottom) const {
  *left = margins[0];
  *top = margins[1];
  *right = margins[2];
  *bottom = margins[3];
  return true;
}

// Compute the minimum dimensions from signed margins, border mode and text height; maxima remain signed32 maximum.
bool WnTextEdit::GetMinMaxInfo(ScRect *out) {
  i32 edge = border == 1 || border == 2 ? 2 : border == 3 ? 4 : 0;
  out->left = margins[2] + edge + margins[0];
  i32 height = (i32)((u32)text_height + (u32)(i32)margins[1] + (u32)(i32)margins[3]);
  out->right = INT32_MAX;
  out->bottom = INT32_MAX;
  out->top = (i32)((u32)edge + (u32)height);
  return true;
}

bool WnTextEdit::SetFontColor(u32 color) {
  SetFontColor(WnInputColor::Normal, color);
  return true;
}

bool WnTextEdit::ClearFontColor(WnInputColor index) {
  return SetFontColor(index, kWnDefaultColor);
}

bool WnTextEdit::GetOutlineType(WnInputOutline *out) const {
  *out = (WnInputOutline)border;
  return true;
}

bool WnTextEdit::SetOutline(u8 enabled) {
  return SetOutlineType(enabled ? WnInputOutline::Normal : WnInputOutline::None);
}

bool WnTextEdit::SetOutlineColor(u32 color) {
  return SetOutlineColor(WnInputEdge::LeftTop, color);
}

// Return the text control's secondary interface at zero references; the class registry performs its own retained query.
ScUnknown *WnConstructTextEdit() {
  return static_cast<WnTextControl *>(new WnTextEdit(1));
}

// Store one of three sound identifiers; the source's negative-index read/write remains outside the defined array contract.
bool WnTextEdit::SetSoundID(i32 index, i32 incoming) {
  assert(index >= 0);
  if (index >= 3)
    return false;
  sounds[index] = (u32)incoming;
  return true;
}

// Resolve sound and resource services only for a nonzero sound, then preserve the device's raw byte result and volume.
u8 WnTextEdit::PlaySoundA(i32 index) {
  u32 *sound = sounds + index;
  if (!*sound)
    return 0;
  AuSoundDevice *device = WnGetSoundDevice();
  if (!device)
    return 0;
  WnResources *resources = WnGetUiResources();
  if (!resources)
    return 0;
  i32 volume = resources->GetVolume();
  return device->SndPlay((i32)*sound, volume, 0);
}

bool WnTextEdit::NotifyAccepted() {
  u32 id = GetID();
  return SendMsg(notification_target, kWnMsg_Command, kWnCommandTextAccepted, id, 0);
}

// Insert one validated UTF-8 span atomically so change callbacks never observe a partial character.
// The original limit remains a byte capacity, and the original byte entry forwards its complete ASCII character here.
bool WnTextEdit::InsertUtf8(const char *bytes, u32 count, u32 index) {
  if (!ScUtf8Validate(bytes, count))
    return false;
  u32 length = text.Strlen();
  if (length >= limit || count > limit - length) {
    PlaySoundA(1);
    return false;
  }
  assert(index <= length);
  index = ScUtf8Floor(text.begin, length, index);
  text.Replace(index, 0, bytes, count);
  NotifyChanged();
  return true;
}

bool WnTextEdit::InsertCharacter(i8 character, u32 index) {
  char byte = static_cast<char>(character);
  return InsertUtf8(&byte, 1, index);
}

// Keep the original one-based deletion position while snapping both ends to complete UTF-8 byte boundaries.
bool WnTextEdit::DeleteText(u32 selected, u32 count) {
  u32 length = text.Strlen();
  if (!selected || selected > length || !count)
    return false;
  u32 start = ScUtf8Floor(text.begin, length, selected - 1);
  u32 end = count > length - (selected - 1) ? length : selected - 1 + count;
  end = ScUtf8Floor(text.begin, length, end);
  if (end == start)
    return false;
  text.Erase(start, end - start);
  NotifyChanged();
  return true;
}


// Assemble the original byte callbacks into complete UTF-8 before applying selection replacement and the byte limit.
// Publish one text change per complete character, preserving the original insertion/scroll/invalidation order.
bool WnTextEdit::GZOnCharacter(char character) {
  if (!editable) {
    input_pending.Erase(0, kScStringToEnd);
    return false;
  }
  u8 byte = static_cast<u8>(character);
  if (byte < 0x20) {
    input_pending.Erase(0, kScStringToEnd);
    PlaySoundA(0);
    return false;
  }
  if (!ScAppendUtf8Byte(&input_pending, byte, nullptr))
    return true;
  ScString input(input_pending);
  input_pending.Erase(0, kScStringToEnd);
  u32 length = text.Strlen(), low = selection_start < selection_finish ? selection_start : selection_finish;
  u32 high = selection_start > selection_finish ? selection_start : selection_finish;
  u32 retained = length - (high - low);
  if (retained >= limit || input.ScString::Strlen() > limit - retained) {
    PlaySoundA(1);
    return false;
  }
  if (selection_start != selection_finish)
    OnKeyDown(kWnKeyBackspace, 0);
  if (!InsertUtf8(input.begin, input.ScString::Strlen(), position))
    return false;
  position = ScUtf8Floor(text.begin, text.Strlen(), position + input.ScString::Strlen());
  selection_start = selection_finish = position;
  UpdateScroll();
  InvalidateSelfAndParents();
  return true;
}

// Move and delete complete UTF-8 characters while retaining byte offsets and notification-before-caret ordering.
bool WnTextEdit::OnKeyDown(u32 key, u32 modifiers) {
  input_pending.Erase(0, kScStringToEnd);
  bool changed = false, shift = (modifiers & kWnModifierShift) != 0;
  u32 step = 1;
  switch (key) {
  case kWnKeyTab:
  case kWnKeyEscape:
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  case kWnKeyReturn:
    if (editable)
      NotifyAccepted();
    if (selection_start == selection_finish)
      return true;
    selection_start = position;
    selection_finish = position;
    break;
  case kWnKeyEnd:
    if (position == text.Strlen())
      return true;
    position = text.Strlen();
    selection_start = position;
    selection_finish = position;
    break;
  case kWnKeyHome:
    if (!position)
      return true;
    position = 0;
    selection_start = 0;
    selection_finish = 0;
    break;
  case kWnKeyLeft:
    if (!position) {
      if (shift)
        return true;
      SetCursorLoc(0);
      selection_start = 0;
      selection_finish = 0;
      break;
    }
    step = ScUtf8PreviousSize(text.begin, position);
    if (shift) {
      if (position)
        position -= step;
      if ((i32)selection_finish > 0)
        selection_finish -= ScUtf8PreviousSize(text.begin, selection_finish);
    } else {
      SetCursorLoc(position - step);
      selection_start = position;
      selection_finish = position;
    }
    break;
  case kWnKeyRight:
    if (position >= text.Strlen()) {
      if (shift)
        return true;
      SetCursorLoc(text.Strlen());
      selection_start = text.Strlen();
      selection_finish = text.Strlen();
      break;
    }
    step = ScUtf8Decode(text.begin + position, text.Strlen() - position, nullptr);
    if (shift) {
      if (position < text.Strlen())
        position += step;
      if (selection_finish < text.Strlen())
        selection_finish += ScUtf8Decode(text.begin + selection_finish, text.Strlen() - selection_finish, nullptr);
    } else {
      SetCursorLoc(position + step);
      selection_start = position;
      selection_finish = position;
    }
    break;
  case kWnKeyBackspace:
  case kWnKeyDelete:
    if (selection_start != selection_finish) {
      u32 low = selection_start < selection_finish ? selection_start : selection_finish;
      u32 high = selection_start >= selection_finish ? selection_start : selection_finish;
      if (editable)
        DeleteText(low + 1, high - low);
      SetCursorLoc(low);
      changed = true;
    } else if (key == kWnKeyBackspace) {
      if (position && editable) {
        step = ScUtf8PreviousSize(text.begin, position);
        if (DeleteText(position - step + 1, step)) {
          position = ScUtf8Floor(text.begin, text.Strlen(), position >= step ? position - step : 0);
          selection_start = selection_finish = position;
          changed = true;
          if (first_visible == position)
            first_visible = position > 10 ? ScUtf8Floor(text.begin, text.Strlen(), position - 10) : 0;
        }
      }
    } else if (editable && position < text.Strlen()) {
      step = ScUtf8Decode(text.begin + position, text.Strlen() - position, nullptr);
      changed = DeleteText(position + 1, step);
    }
    if (selection_start != selection_finish) {
      selection_start = position;
      selection_finish = position;
      changed = true;
    }
    if (!changed)
      return true;
    break;
  default:
    return true;
  }
  UpdateScroll();
  InvalidateSelfAndParents();
  return true;
}

// An editable control dirties its parent chain on focus entry; a read-only control leaves the event unhandled.
bool WnTextEdit::GZOnSetFocus(WnWindow *) {
  if (!editable)
    return false;
  InvalidateSelfAndParents();
  return true;
}

// Always invalidate the control on focus exit, replacing the root window's separate focus-redirection behavior.
bool WnTextEdit::GZOnKillFocus(WnWindow *) {
  input_pending.Erase(0, kScStringToEnd);
  InvalidateSelfAndParents();
  return true;
}

// Capture editable presses before moving the caret or extending selection.
bool WnTextEdit::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  if (editable) {
    if (owner->SetCapture(this)) {
      u32 selected;
      if (modifiers & kWnModifierShift) {
        selected = PositionFromX(x);
        position = selected;
      } else {
        SetCursorLoc(PositionFromX(x));
        selected = position;
        selection_start = selected;
      }
      selection_finish = selected;
      UpdateScroll();
      InvalidateSelfAndParents();
    }
    return true;
  }
  void *slot = nullptr;
  if (!parent->QueryInterface(IID_WnComboControl, &slot))
    return true;
  bool result = SendMsg(notification_target, kWnMsg_MouseDownL, (u32)x, (u32)y, modifiers);
  static_cast<ScUnknown *>(slot)->Release();
  return result;
}

// Query and release capture through the original root helpers before converting the final pointer position.
// This source path adds no invalidation or scroll adjustment.
bool WnTextEdit::OnMouseUpL(i32 x, i32, u32) {
  if (GZAmCapturing()) {
    GZReleaseCapture();
    selection_finish = PositionFromX(x);
  }
  return true;
}

// A captured drag changes the live selection endpoint and invalidates only when that endpoint moves.
bool WnTextEdit::OnMouseMove(i32 x, i32, u32) {
  if (owner->GetCapture() == this) {
    u32 selected = PositionFromX(x);
    if (selection_finish != selected) {
      selection_finish = selected;
      InvalidateSelfAndParents();
    }
  }
  return true;
}

// Hit-test measured complete UTF-8 prefixes and return the nearest scalar boundary as a byte offset.
u32 WnTextEdit::PositionFromX(i32 x) {
  i32 local = static_cast<i32>(static_cast<u32>(x) - static_cast<u32>(static_cast<i32>(margins[0])));
  if (local <= 0)
    return first_visible;
  u32 selected = first_visible, length = text.Strlen();
  i32 previous = 0;
  while (selected < length) {
    u32 step = ScUtf8Decode(text.begin + selected, length - selected, nullptr);
    assert(step);
    i32 measured = font->Measure(text.begin + first_visible, selected + step - first_visible);
    if (measured > local)
      return measured - local >= local - previous ? selected : selected + step;
    selected += step;
    previous = measured;
  }
  return length;
}

bool WnTextEdit::GZOnCaptureChanged(WnWindow *) {
  return true;
}

static i32 WnTextEditAdd(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

static i32 WnTextEditSubtract(i32 first, i32 second) {
  return (i32)((u32)first - (u32)second);
}

// Callback-sensitive dispatch and intermediate arithmetic remain under review.
// Fill the background, draw visible text and selection or caret, then render the border under its own lock.
bool WnTextEdit::GZPaint() {
  // Delegate complete UTF-8 spans to the selected font; all offsets remain byte extents.
  auto measure = [](FtFont *selected, const ScString *value, i32 start, i32 count, u8) -> i32 {
    return selected->Measure(value->begin + start, static_cast<u32>(count));
  };
  // Snapshot explicitly colored text before the font callback, matching the original temporary byte-vector lifetime.
  auto draw = [this](i32 x, i32 y, u32 start, u32 count, i32 index) {
    u32 color = text_colors[index];
    if (color == kWnDefaultColor)
      font->DrawTextA(draw_buffer, (u32)x, (u32)y, text.begin + start, count);
    else {
      ScArray<char> copy;
      for (u32 i = 0; i < count; ++i)
        *copy.Append() = text.begin[start + i];
      font->DrawTextA(draw_buffer, (u32)x, (u32)y, copy.data, copy.size, color);
    }
  };
  if (background && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (!font)
    return false;
  i32 right = WnTextEditSubtract(draw_area.right, margins[2]);
  i32 bottom = WnTextEditSubtract(draw_area.bottom, margins[3]);
  i32 top = WnTextEditAdd(draw_area.top, margins[1]), left = WnTextEditAdd(draw_area.left, margins[0]);
  i32 y = WnTextEditAdd(top, WnTextEditSubtract(WnTextEditSubtract(bottom, text_height), top) / 2);
  if (owner->GetFocus() != this) {
    if (text.Strlen()) {
      u32 count = font->Fit(
          text.begin + first_visible, text.Strlen() - first_visible, (u32)WnTextEditSubtract(right, left));
      if (count) {
        i32 color_index = !GetFlag(kWnFlagEnabled);
        draw(left, y, first_visible, count, color_index);
      }
    }
  } else if (selection_start != selection_finish) {
    i32 count = (i32)font->Fit(
          text.begin + first_visible, text.Strlen() - first_visible, (u32)WnTextEditSubtract(right, left));
    i32 start = (i32)selection_finish < (i32)selection_start ? (i32)selection_finish : (i32)selection_start;
    if (start < 0)
      start = 0;
    if (start < (i32)first_visible)
      start = (i32)first_visible;
    i32 finish = (i32)selection_start >= (i32)selection_finish ? (i32)selection_start : (i32)selection_finish;
    if (finish > (i32)text.Strlen())
      finish = (i32)text.Strlen();
    if (finish > WnTextEditAdd((i32)first_visible, count))
      finish = WnTextEditAdd((i32)first_visible, count);
    i32 first_x = start ? measure(font, &text, 0, start, double_byte) : 0;
    i32 last_x = measure(font, &text, 0, finish, double_byte);
    if (first_visible) {
      i32 offset = measure(font, &text, 0, (i32)first_visible, double_byte);
      first_x = WnTextEditSubtract(first_x, offset);
      last_x = WnTextEditSubtract(last_x, offset);
    }
    if (opacity < 0.0f || opacity > 0.0f) {
      ScRect rectangle{WnTextEditAdd(left, first_x), top, WnTextEditAdd(left, last_x), bottom};
      if (opacity == 1.0f) {
        if (draw_buffer->Lock(GxLockFlags::Fill)) {
          ScRect clipped;
          if (rectangle.right < draw_area.left || rectangle.left > draw_area.right ||
              rectangle.bottom < draw_area.top || rectangle.top > draw_area.bottom)
            clipped = {};
          else {
            clipped.left = rectangle.left > draw_area.left ? rectangle.left : draw_area.left;
            clipped.top = rectangle.top > draw_area.top ? rectangle.top : draw_area.top;
            clipped.right = rectangle.right < draw_area.right ? rectangle.right : draw_area.right;
            clipped.bottom = rectangle.bottom < draw_area.bottom ? rectangle.bottom : draw_area.bottom;
          }
          draw_buffer->Fill(selection_color, clipped, nullptr);
          draw_buffer->Unlock(GxLockFlags::Fill);
        }
      } else if (draw_buffer->Lock(GxLockFlags::Bits)) {
        draw_buffer->AlphaFill(selection_color, rectangle, opacity, nullptr);
        draw_buffer->Unlock(GxLockFlags::Bits);
      }
    }
    if (count)
      draw(left, y, first_visible, (u32)count, 0);
    if (WnTextEditSubtract(finish, start) > 0)
      draw(WnTextEditAdd(left, first_x), y, (u32)start, (u32)WnTextEditSubtract(finish, start), 2);
  } else {
    if (text.Strlen()) {
      u32 count = font->Fit(
          text.begin + first_visible, text.Strlen() - first_visible, (u32)WnTextEditSubtract(right, left));
      i32 color_index = !GetFlag(kWnFlagEnabled);
      draw(left, y, first_visible, count, color_index);
    }
    if (editable) {
      i32 offset = 0, count = (i32)(position - first_visible);
      if (position && count) {
        offset = measure(font, &text, (i32)first_visible, count, double_byte);
        if (!double_byte)
          offset = WnTextEditSubtract(offset, caret_width / 2);
        offset = WnTextEditSubtract(offset, 1);
      }
      font->DrawTextA(
          draw_buffer, (u32)WnTextEditAdd(left, offset), (u32)y, reinterpret_cast<const char *>(&caret_character), 1);
    }
  }
  i32 mode = border;
  if (mode == 1) {
    if (draw_buffer->Lock(GxLockFlags::Rect)) {
      draw_buffer->DrawRectangleOutline(draw_area, border_colors[0], 1);
      draw_buffer->Unlock(GxLockFlags::Rect);
    }
  } else if (mode == 2) {
    if (draw_buffer->Lock(GxLockFlags::Line)) {
      i32 r = WnTextEditSubtract(draw_area.right, 1), b = WnTextEditSubtract(draw_area.bottom, 1);
      draw_buffer->DrawLine(
          draw_area.left, draw_area.top, draw_area.left, b, static_cast<i32>(border_colors[0]), 1);
      draw_buffer->DrawLine(r, draw_area.top, r, b, static_cast<i32>(border_colors[1]), 1);
      draw_buffer->DrawLine(
          draw_area.left, draw_area.top, r, draw_area.top, static_cast<i32>(border_colors[0]), 1);
      draw_buffer->DrawLine(draw_area.left, b, r, b, static_cast<i32>(border_colors[1]), 1);
      draw_buffer->Unlock(GxLockFlags::Line);
    }
  } else if (mode == 3) {
    if (draw_buffer->Lock(GxLockFlags::Line)) {
      ScRect rectangle = draw_area;
      rectangle.right = WnTextEditSubtract(rectangle.right, 1);
      rectangle.bottom = WnTextEditSubtract(rectangle.bottom, 1);
      draw_buffer->DrawLine(rectangle.right, rectangle.top, rectangle.right, rectangle.bottom,
          static_cast<i32>(border_colors[1]), 1);
      rectangle.right = WnTextEditSubtract(rectangle.right, 1);
      draw_buffer->DrawLine(rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom,
          static_cast<i32>(border_colors[1]), 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.top, rectangle.right, rectangle.top, static_cast<i32>(border_colors[0]), 1);
      rectangle.bottom = WnTextEditSubtract(rectangle.bottom, 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.top, rectangle.left, rectangle.bottom, static_cast<i32>(border_colors[0]), 1);
      rectangle.top = WnTextEditAdd(rectangle.top, 1);
      draw_buffer->DrawLine(rectangle.right, rectangle.top, rectangle.right, rectangle.bottom,
          static_cast<i32>(border_colors[3]), 1);
      rectangle.left = WnTextEditAdd(rectangle.left, 1);
      draw_buffer->DrawLine(rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom,
          static_cast<i32>(border_colors[3]), 1);
      rectangle.right = WnTextEditSubtract(rectangle.right, 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.top, rectangle.right, rectangle.top, static_cast<i32>(border_colors[2]), 1);
      rectangle.bottom = WnTextEditSubtract(rectangle.bottom, 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.top, rectangle.left, rectangle.bottom, static_cast<i32>(border_colors[2]), 1);
      draw_buffer->Unlock(GxLockFlags::Line);
    }
  }
  return true;
}

// Validate incoming UTF-8 before changing live text and truncate only at complete scalar boundaries.
// Retain the original change notification under its reentry lock and reset the byte caret and pending input afterward.
bool WnTextEdit::SetText(const ScStringView *incoming) {
  ScString value;
  if (incoming) {
    value.Copy(*incoming);
    if (!ScUtf8Validate(value.begin, value.ScString::Strlen()))
      return false;
  }
  u32 count = ScUtf8Prefix(value.begin, value.ScString::Strlen(), limit);
  input_pending.Erase(0, kScStringToEnd);
  if (incoming)
    text.ScString::FromChar(value.begin, count);
  else
    text.Erase(0, kScStringToEnd);
  if (!changing_text) {
    changing_text = true;
    NotifyChanged();
    changing_text = false;
  }
  position = selection_start = selection_finish = first_visible = 0;
  changing_text = false;
  InvalidateSelfAndParents();
  return true;
}

i32 WnTextEdit::GetText(ScStringView *out) const {
  out->Copy(text);
  return (i32)text.Strlen();
}

// Select a byte extent without splitting UTF-8; an out-of-range start retains the original last-character clamp.
bool WnTextEdit::SetSelection(u32 start, u32 count) {
  u32 length = text.Strlen();
  u32 selected = start >= length && length ? ScUtf8Floor(text.begin, length, length - 1)
                                         : ScUtf8Floor(text.begin, length, start);
  u32 end = count > length - selected ? length : selected + count;
  selection_start = selected;
  selection_finish = ScUtf8Floor(text.begin, length, end);
  position = selection_finish;
  UpdateScroll();
  InvalidateSelfAndParents();
  return true;
}

bool WnTextEdit::NotifyChanged() {
  u32 id = GetID();
  return SendMsg(notification_target, kWnMsg_Command, kWnCommandTextChanged, id, 0);
}


// Keep the caret visible by advancing only whole UTF-8 characters, retaining byte offsets for the viewport and font.
bool WnTextEdit::UpdateScroll() {
  u32 length = text.Strlen();
  position = ScUtf8Floor(text.begin, length, position);
  first_visible = ScUtf8Floor(text.begin, length, first_visible);
  if (!length)
    position = first_visible = 0;
  else if (position > first_visible) {
    i32 available = static_cast<i32>(static_cast<u32>(draw_area.right) -
        static_cast<u32>(static_cast<i32>(margins[2])) - static_cast<u32>(static_cast<i32>(margins[0])) -
        static_cast<u32>(draw_area.left));
    i32 measured = font->Measure(text.begin + first_visible, position - first_visible);
    while (measured > available && first_visible < position) {
      u32 step = ScUtf8Decode(text.begin + first_visible, length - first_visible, nullptr);
      assert(step);
      first_visible += step;
      measured = font->Measure(text.begin + first_visible, position - first_visible);
    }
  } else
    first_visible = position;
  return true;
}

bool WnTextEdit::SetText(char *incoming) {
  ScString value(incoming, (u32)strlen(incoming));
  return SetText(&value);
}

bool WnTextEdit::SetText(const ScStringView &incoming) {
  return SetText(&incoming);
}
