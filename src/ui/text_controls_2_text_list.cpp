#include "ui/text_controls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/api.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/utf8.h"
#include "fonts/sc3_bitmap_font.h"
#include "resources/resource.h"
#include "ui/replacement_manager.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/double_click.h"
#include "window/input.h"

UiEditor::~UiEditor() {
  UiEditor::Shutdown();
}

// Release the font and stop each timer before releasing scrollbars and images in their original order.
// Timer shutdown retains the original100 millisecond wait; inherited cleanup follows owned dependencies.
bool UiEditor::Shutdown() {
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
  return UiBaseWindow::Shutdown();
}

// Normalize wrapping and single-line scroll flags, synchronize the caret timer, then relayout only the source change bits.
// Cursor selection follows invalidation and uses the requested editable bit with the borrowed manager cursor.
void UiEditor::SetFlags(i32 requested) {
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

// Consume the two actual editor timer messages and delegate every other message to the real root dispatcher.
bool UiEditor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_EditorCaret) {
    ToggleCaretVisibility();
    return true;
  }
  if (message->type == kWnMsg_EditorScroll) {
    DoCursorScroll();
    return true;
  }
  return UiBaseWindow::DoMessage(message);
}

u32 UiEditor::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiEditor::Release() {
  return UiBaseWindow::Release();
}

bool UiEditor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnEditorControl, WnEditorControl);
  return UiBaseWindow::QueryInterface(iid, out);
}

void UiEditor::ToggleCaretVisibility() {
  if (owner->GetFocus() == this) {
    caret_visible = !caret_visible;
    InvalidateSelfAndParents();
  }
}

// Measure the source viewport width after the live vertical window query, preserving wrapped subtraction of the two gutters.
u32 UiEditor::CalculateVisibleWidth() {
  i32 width = vertical_scrollbar ? vertical_scrollbar->GetWindow()->GetW() : 0;
  u32 window_width = (u32)GetW();
  return window_width - 2 * gutter_x - (u32)width;
}

// Preserve the original asymmetric height dependency: a horizontal bar enables subtraction of the vertical bar's height.
// A horizontal-only state dereferences an unavailable vertical receiver in the original and is outside defined execution.
u32 UiEditor::CalculateVisibleHeight() {
  i32 height = horizontal_scrollbar ? vertical_scrollbar->GetWindow()->GetH() : 0;
  u32 window_height = (u32)GetH();
  return window_height - 2 * gutter_y - (u32)height;
}

// Recompute the font height, viewport and line suffix before live scrollbar synchronization and caret normalization.
// Scrollbar visibility follows the first-visible clamp and may recursively relayout after creation or removal.
void UiEditor::RecalculateAll() {
  line_height = font ? font->Height(0) : 16;
  text_height = CalculateVisibleHeight();
  visible_rows = text_height / line_height;
  text_width = CalculateVisibleWidth();
  CalculateAllLineLengths(0);
  SetScrollBarValues(true);
  FixInsertionPositionIfBroken();
  if (!UiEditor::GetLineCount())
    first_visible = 0;
  else if (first_visible >= UiEditor::GetLineCount())
    first_visible = UiEditor::GetLineCount() - 1;
  TestForPossibleScrollBarAdditionOrRemoval();
}

// Normalize actual caret fields after reflow, clearing all five only for empty text and leaving a nonempty start untouched.
// Index conversion occurs before the finish clamp, matching the original state publication sequence.
void UiEditor::FixInsertionPositionIfBroken() {
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

// Synchronize the optional live vertical control. Recalculation queries the editor's virtual count twice around SetRange.
void UiEditor::SetScrollBarValues(bool recalculate) {
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

// Place each live scrollbar using two independent borrowed-window queries and the source's repeated live size calls.
// The vertical left edge includes the original extra pixel; horizontal placement preserves the current bar height.
void UiEditor::SetScrollBarSizes() {
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

// Read the next line-offset difference or the last line's remaining bytes, without stripping its newline byte.
// Empty text returns zero before any line-table access; nonempty text requires the source's populated offset table.
u32 UiEditor::GetCharacterCountOfLine(u32 line) {
  u32 length = text.ScString::Strlen();
  if (!length)
    return 0;
  if (line < UiEditor::GetLineCount() - 1)
    return lines.begin[line + 1] - lines.begin[line];
  return length - lines.begin[UiEditor::GetLineCount() - 1];
}

// Convert a byte index through the actual line offsets. A trailing newline after the last stored offset yields count,0.
// Preserve column-before-line zeroing for empty input and line-before-column writes for an ordinary match.
void UiEditor::GetLineAndColumnFromIndex(u32 *line, u32 *column, u32 index) {
  index = ScUtf8Floor(text.begin, text.ScString::Strlen(), index);
  if (!UiEditor::GetLineCount() || !text.ScString::Strlen() || !index) {
    *column = 0;
    *line = 0;
    return;
  }
  u32 selected = 0;
  while (selected < UiEditor::GetLineCount() && lines.begin[selected] <= index)
    ++selected;
  if (selected == UiEditor::GetLineCount() && text.begin[index - 1] == '\n') {
    *line = UiEditor::GetLineCount();
    *column = 0;
    return;
  }
  --selected;
  *line = selected;
  *column = index - lines.begin[selected];
}

// Add a column to the selected offset, using the final stored line for out-of-range line indices.
bool UiEditor::GetIndexFromLineAndColumn(u32 line, u32 column, u32 *index) {
  u32 length = text.ScString::Strlen();
  if (!length) {
    *index = 0;
    return false;
  }
  u32 selected = line < UiEditor::GetLineCount() ? line : UiEditor::GetLineCount() - 1;
  *index = lines.begin[selected] + column;
  if (*index <= length) {
    *index = ScUtf8Floor(text.begin, length, *index);
    return true;
  }
  *index = length;
  return false;
}

void UiEditor::StartScrollTimer() {
  if (!scroll_timer.IsRunning())
    scroll_timer.StartPeriodic(250, nullptr);
}

void UiEditor::StopScrollTimer() {
  if (scroll_timer.IsRunning())
    scroll_timer.Stop();
}

// Move insertion back one byte and column when it lies immediately after a newline within a nonzero column.
void UiEditor::MoveInsertionPositionToBeforeReturnIfNecessary() {
  if (insertion_index && insertion_column && text.begin[insertion_index - 1] == '\n') {
    --insertion_column;
    --insertion_index;
  }
}

// Draw the captured viewport with original font colors, clipped selection fills and text, then the live focused caret/outline.
// Scrollbar reads update stored first-visible state after the drawing range is captured; unwrapped line offsets accumulate.
bool UiEditor::GZPaint() {
  u32 y = (u32)draw_area.top + gutter_y, x = gutter_x + (u32)draw_area.left;
  u32 first = first_visible, line = first, finish = visible_rows + first;
  u32 horizontal = 0;
  if (vertical_scrollbar)
    first_visible = (u32)vertical_scrollbar->GetValue();
  if (horizontal_scrollbar)
    horizontal = (u32)horizontal_scrollbar->GetValue();
  if (finish > UiEditor::GetLineCount())
    finish = UiEditor::GetLineCount();
  if ((editor_flags & 0x20) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  u32 saved_color = font->Color(), saved_end = font->EndColor();
  font->SetColors(font_colors[0], font_colors[1] == kUiEditorUseForegroundColor ? font_colors[0] : font_colors[1]);
  while (line < finish) {
    u32 line_start = lines.begin[first], length;
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
      font->SetColors(font_colors[4], font_colors[5] == kUiEditorUseForegroundColor ? font_colors[4] : font_colors[5]);
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
      const char *bytes = text.begin + lines.begin[current];
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

// Find a clipped line-local UTF-8 span through whole-character measurements.
// Keep the original inclusive last-byte output and pixel offset without exposing partial scalar prefixes.
void UiEditor::FindFirstAndLastCharacterIndexForALineLongerThanTheScreenWidth(
    u32 line, u32 horizontal, u32 *first, u32 *last, u32 *offset) {
  u32 length = GetCharacterCountOfLine(line), column = 0;
  const char *bytes = text.begin + lines.begin[line];
  u32 width = 0;
  while (column < length && width < horizontal) {
    u32 count = ScUtf8Decode(bytes + column, length - column, nullptr);
    assert(count);
    column += count;
    width = static_cast<u32>(font->Measure(bytes, column));
  }
  *offset = gutter_x - horizontal + width;
  *first = lines.begin[line] + column;
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

// Rebuild the line-offset suffix through the real font fitter, retaining existing offsets before the requested line.
// Preserve the source's nonwrapped scan and terminal-newline behavior, including its remaining-span offset calculation.
void UiEditor::CalculateAllLineLengths(u32 first_line) {
  if (!font)
    return;
  if (first_line == kUiEditorRecalculateAllLines)
    first_line = 0;
  u32 length = text.Strlen();
  if (!length) {
    lines.end = lines.begin;
    insertion_index = insertion_column = insertion_line = 0;
    return;
  }
  u32 position;
  if (first_line)
    position = lines.begin[first_line];
  else {
    if (!UiEditor::GetLineCount())
      lines.Resize(UiEditor::GetLineCount() + 1, 0);
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
  if (count != UiEditor::GetLineCount())
    lines.Resize(count, 0);
}

// The previous offset is read before growth, preserving the original caller's populated-prefix requirement.
void UiEditor::AdjustLinePositionArray(u32 increment, u32 count) {
  if (count <= UiEditor::GetLineCount()) {
    if (count <= 1)
      lines.begin[0] = 0;
    else
      lines.begin[count - 1] = increment + lines.begin[count - 2];
  } else {
    u32 offset = increment + lines.begin[count - 2];
    lines.Resize(count, offset);
  }
}

void UiEditor::AddFirstLineLength() {
  if (!UiEditor::GetLineCount())
    lines.Resize(UiEditor::GetLineCount() + 1, 0);
}

// Each create/remove path owns recursive layout and reference updates; the original returns true on every path.
bool UiEditor::TestForPossibleScrollBarAdditionOrRemoval() {
  if (editor_flags & 8) {
    if (UiEditor::GetLineCount() <= visible_rows) {
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
bool UiEditor::AddScrollBar(bool vertical, bool horizontal) {
  bool changed = false;
  if (vertical && (editor_flags & 8) && !vertical_scrollbar) {
    changed = true;
    vertical_scrollbar = UiGetWindowResources()->CreateScrollbar(0, 2, 0, 100, 0);
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
    horizontal_scrollbar = UiGetWindowResources()->CreateScrollbar(1, 1, 0, 100, 0);
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
void UiEditor::RemoveScrollBar(bool vertical, bool horizontal) {
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

// Measure full line-byte extents through the real visible-width method, initializing its genuine output length each time.
// A false mode uses the original unclamped viewport range; only actual returned unsigned widths select the maximum.
u32 UiEditor::GetPixelWidthOfLongestLine(bool all_lines) {
  u32 first = all_lines ? 0 : first_visible, last = all_lines ? UiEditor::GetLineCount() : first_visible + visible_rows;
  u32 maximum = 0;
  for (u32 line = first; line < last; ++line) {
    const char *bytes = text.begin;
    u32 offset = lines.begin[line];
    if (offset)
      bytes += offset;
    u32 length = GetCharacterCountOfLine(line), visible = length;
    u32 width = font->GetStringVisibleWidth(bytes, length, &visible);
    if (width > maximum)
      maximum = width;
  }
  return maximum;
}

u32 UiEditor::NextCharSizeFromText(const char *bytes, u32 length) {
  return ScUtf8Decode(bytes, length, nullptr);
}

u32 UiEditor::PreviousCharSizeFromText(const char *bytes, u32 length) {
  return length ? ScUtf8PreviousSize(bytes - length, length) : 0;
}

bool UiEditor::IsCharOK(u8 character) {
  return character >= 0x20 || character == 10;
}

bool UiEditor::IsCharOK(const char *bytes, u32 length, u32 *count) {
  *count = ScUtf8Decode(bytes, length, nullptr);
  return *count != 0;
}

// Filter complete UTF-8 scalars and the existing single-line newline restriction without splitting accepted text.
u32 UiEditor::RemoveBadCharacters(ScString *value) {
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

// Convert a local point into the original line and nearest measured byte column without changing the insertion state.
// Step from the current UTF-8 byte boundary before measuring each complete prefix.
void UiEditor::GetLineAndColumnFromPosition(u32 *line, u32 *column, i32 x, i32 y) {
  if (y < 0) {
    x = 0;
    y = 0;
  }
  if (x < 0)
    x = 0;
  i32 horizontal = (i32)((u32)x - gutter_x);
  *line = first_visible;
  *line += (u32)y / line_height;
  if (*line >= UiEditor::GetLineCount())
    *line = UiEditor::GetLineCount() - 1;
  if (text.begin == text.end) {
    *column = 0;
    *line = 0;
    return;
  }
  const char *begin = text.begin + lines.begin[*line];
  i32 previous = 0;
  u32 length = GetCharacterCountOfLine(*line);
  *column = 0;
  if (!length)
    return;
  for (;;) {
    u32 remaining = text.ScString::Strlen() - lines.begin[*line] - *column;
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

// Select the character's left column whose measured right edge first reaches the requested signed x coordinate.
void UiEditor::GetColumnFromPosition(u32 line, u32 *column, i32 x) {
  if (!UiEditor::GetLineCount() || x <= (i32)gutter_x) {
    *column = 0;
    return;
  }
  *column = 0;
  u32 length = GetCharacterCountOfLine(line);
  const char *begin = text.begin + lines.begin[line], *end = text.end;
  while (*column < length) {
    u32 count = NextCharSizeFromText(begin + *column, (u32)(end - begin) - *column);
    u32 width = font->Measure(begin, *column + count);
    if (x <= (i32)(width + gutter_x))
      return;
    *column += count;
  }
  *column = length;
}

// Measure the clamped line prefix before publishing its x coordinate, then compute y from the live viewport fields.
void UiEditor::GetPositionFromLineAndColumn(u32 line, u32 column, i32 *x, i32 *y) {
  if (!UiEditor::GetLineCount()) {
    *x = (i32)gutter_x;
    *y = (i32)gutter_y;
    return;
  }
  if (line >= UiEditor::GetLineCount())
    line = UiEditor::GetLineCount() - 1;
  u32 first = lines.begin[line];
  if (column + first >= text.ScString::Strlen())
    column = text.ScString::Strlen() - first;
  column = ScUtf8Prefix(text.begin + first, text.ScString::Strlen() - first, column);
  u32 width = font->Measure(text.begin + first, column);
  *x = (i32)(width + gutter_x);
  *y = (i32)(gutter_y + line_height * (line - first_visible));
}

// Move by complete UTF-8 scalars while storing caret and line/column positions as byte offsets.
void UiEditor::IncrementInsertionPosition(i32 delta) {
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

// Preserve the original standard-string erase range check and count clamp before calling native storage plumbing.
void UiEraseEditorText(ScString *text, u32 index, u32 count) {
  u32 length = text->ScString::Strlen();
  if (index > length)
    ScFatal(SC_ERROR_INVALID, "SIMUI editor erase starts outside its text");
  u32 available = length - index;
  text->Erase(index, count > available ? available : count);
}

// Clamp selection endpoints independently without sorting them, place insertion at the finish and always invalidate.
bool UiEditor::SetSelection(u32 start, u32 finish) {
  u32 length = text.ScString::Strlen();
  selection_finish = ScUtf8Floor(text.begin, length, finish);
  insertion_index = selection_finish;
  selection_start = ScUtf8Floor(text.begin, length, start);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  InvalidateSelfAndParents();
  return true;
}

// Preserve the source's final rejection of every result at text length, including empty-needle end positions.
u32 UiEditor::FindText(const char *bytes, u32 count, u32 position, bool forward) {
  u32 length = text.ScString::Strlen();
  if (forward) {
    if (position >= length || count > length - position)
      return kScStringNotFound;
    if (!count)
      return position;
    for (u32 candidate = position; candidate <= length - count; ++candidate)
      if (!memcmp(text.begin + candidate, bytes, count))
        return candidate;
  } else {
    if (count > length)
      return kScStringNotFound;
    u32 candidate = position < length - count ? position : length - count;
    if (!count)
      return candidate < length ? candidate : kScStringNotFound;
    for (;;) {
      if (!memcmp(text.begin + candidate, bytes, count))
        return candidate;
      if (!candidate)
        break;
      --candidate;
    }
  }
  return kScStringNotFound;
}

// Reject malformed UTF-8 before replacing text, then filter and truncate only at complete scalar boundaries.
// Retain invalidation before history retirement and clear pending input when replacing the string.
bool UiEditor::SetText(const char *bytes, u32 count) {
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
  undo.Clear();
  redo.Clear();
  return true;
}

// Insert validated UTF-8 at a scalar boundary and apply the existing byte capacity without cutting a character.
// Advance by the inserted byte extent before the original relayout and notification effects.
u32 UiEditor::InsertText(const char *bytes, u32 count, u32 index) {
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
  if (!UiEditor::GetLineCount())
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

// Convert the requested line and column into the genuine index output, ignoring the lookup Boolean before insertion.
u32 UiEditor::InsertText(const char *bytes, u32 count, u32 line, u32 column) {
  u32 index;
  GetIndexFromLineAndColumn(line, column, &index);
  return InsertText(bytes, count, index);
}

// Remove the character at the clamped existing byte position, optionally copying it through the genuine output interface.
// Retain both index conversions around reflow/synchronization and the final all-lines-fit viewport reset.
bool UiEditor::RemoveChar(u32 index, bool, ScStringView *removed) {
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
  i32 first = (i32)first_visible, excess = (i32)(first_visible + visible_rows - UiEditor::GetLineCount());
  if (excess > 0 && first > 0) {
    i32 reduce = first;
    if (first >= excess)
      reduce = excess;
    first_visible = (u32)first - (u32)reduce;
    SetScrollBarValues(false);
  }
  PutInsertionPositionInView();
  TestForPossibleScrollBarAdditionOrRemoval();
  if (UiEditor::GetLineCount() <= visible_rows) {
    first_visible = 0;
    SetScrollBarValues(false);
  }
  InvalidateSelfAndParents();
  return true;
}

// Copy the removed span to an optional genuine output before erasing, reflowing and adjusting the live viewport.
// Return the clamped byte count directly; positions at or past the text end return zero without effects.
u32 UiEditor::RemoveText(u32 index, u32 count, ScStringView *removed) {
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
  i32 first = (i32)first_visible, excess = (i32)(first_visible + visible_rows - UiEditor::GetLineCount());
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

u32 UiEditor::RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) {
  u32 index;
  GetIndexFromLineAndColumn(line, column, &index);
  return RemoveText(index, count, removed);
}

// Convert both endpoints and preserve the first endpoint as the erase start even when the range is reversed.
// The installed method ignores its fifth output argument and explicitly forwards a null removed-text output.
u32 UiEditor::RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *) {
  u32 first, last;
  GetIndexFromLineAndColumn(from_line, from_column, &first);
  GetIndexFromLineAndColumn(to_line, to_column, &last);
  return RemoveText(first, first <= last ? last - first : first - last, nullptr);
}

// Advance across space/newline word boundaries, then clamp and convert the live insertion index.
void UiEditor::IncrementInsertionPositionByWord(i32 delta) {
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

UiTextList::UiTextList()
    : language(0), highlight_alpha(1.0f), highlight_color(kWnDefaultColor),
      text_colors{kWnDefaultColor, kWnDefaultColor, kWnDefaultColor}, border_colors{kWnDefaultColor, kWnDefaultColor},
      rows(new UiTextRow), wrapped_rows(new UiTextRow), fonts{}, selection(-1), visible_rows(0), first_visible(0),
      row_height(10), first_width(-1), alternate_alignment(0), fill_background(0), rectangle_outline(0),
      line_outline(0), outline_colors{kWnDefaultColor, kWnDefaultColor}, gutters{3, 0, 3, 1}, gap(5), last_click(-1),
      double_click(0), allow_scrollbar(1), state262(0) {
  rows->next = rows->previous = rows;
  wrapped_rows->next = wrapped_rows->previous = wrapped_rows;
  WnResources *provider = UiGetWindowResources();
  if (provider) {
    outline_colors[0] = provider->GetColor(19);
    outline_colors[1] = provider->GetColor(20);
  }
  UiBaseWindow::SetFillColor(provider->GetColor(kWnColorUiBackground));
}

// Release retained fonts before destroying wrapped rows, source rows and their sentinels, then enter the window base.
UiTextList::~UiTextList() {
  Shutdown();
  ClearRows(wrapped_rows);
  delete wrapped_rows;
  ClearRows(rows);
  delete rows;
}

bool UiTextList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnTextListControl, WnTextListControl);
  return UiBaseWindow::QueryInterface(iid, out);
}

u32 UiTextList::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiTextList::Release() {
  return UiBaseWindow::Release();
}

// Borrow the embedded window without an interface query or reference operation.
WnWindow *UiTextList::GetWindow() {
  return this;
}

// Fill only missing fonts and sentinel theme colors before selecting language, entering base startup and wrapping rows.
bool UiTextList::Init() {
  if (!GetFlag(kWnFlagInitialized)) {
    WnResources *provider = UiGetWindowResources();
    if (provider) {
      if (!fonts[0]) {
        fonts[0] = provider->GetFont(kWnFontDefault);
        fonts[0]->AddRef();
      }
      if (!fonts[1]) {
        fonts[1] = provider->GetFont(kWnFontDefault);
        fonts[1]->AddRef();
      }
      if (border_colors[0] == kWnDefaultColor)
        border_colors[0] = provider->GetColor(19);
      if (border_colors[1] == kWnDefaultColor)
        border_colors[1] = provider->GetColor(20);
      for (u32 index = 0; index < 3; ++index)
        if (text_colors[index] == kWnDefaultColor)
          SetFontColor(index, provider->GetColor(23 + index));
      if (highlight_color == kWnDefaultColor)
        SetHighlightColor(provider->GetColor(22));
    }
    ScLanguageManager *language_manager = UiGetLanguageManager();
    language = language_manager ? language_manager->GetCurrentLanguage() : 1;
    UiBaseWindow::Init();
    CalculateAllMetrics();
  }
  return true;
}

// Invalidate only after a changed full-width selection-background pixel has been stored.
void UiTextList::SetHighlightColor(u32 pixel) {
  if (highlight_color != pixel) {
    highlight_color = pixel;
    InvalidateSelfAndParents();
  }
}

// Each incoming retain precedes release of that column's live previous font and publication of the new pointer.
void UiTextList::SetFonts(FtFont *first, FtFont *second) {
  if (first)
    first->AddRef();
  if (fonts[0])
    fonts[0]->Release();
  fonts[0] = first;
  if (second)
    second->AddRef();
  if (fonts[1])
    fonts[1]->Release();
  fonts[1] = second;
}

// Changed ordered inputs clamp to zero through one before publication and invalidation.
void UiTextList::SetHighlightFillAlpha(f32 opacity) {
  if (opacity < highlight_alpha || opacity > highlight_alpha) {
    if (opacity > 1.0f)
      opacity = 1.0f;
    else if (opacity < 0.0f)
      opacity = 0.0f;
    highlight_alpha = opacity;
    InvalidateSelfAndParents();
  }
}

void UiTextList::SetRightColumnTextStart(i32 width) {
  first_width = width;
  CalculateAllMetrics();
}

void UiTextList::SetJustification(bool value) {
  alternate_alignment = value;
}

void UiTextList::SetOpaque(bool value) {
  fill_background = value;
}

// Publish the mutually exclusive rectangle-outline state before invalidating the window.
void UiTextList::SetOutlineDrawing(bool value) {
  line_outline = 0;
  rectangle_outline = value;
  InvalidateSelfAndParents();
}

// Publish the mutually exclusive sunken-outline state before invalidating the window.
void UiTextList::SetDrawSunkenRect(bool value) {
  rectangle_outline = 0;
  line_outline = value;
  InvalidateSelfAndParents();
}

void UiTextList::SetSunkenRectColors(u32 first, u32 second) {
  outline_colors[0] = first;
  outline_colors[1] = second;
}

// Store both original Boolean scrollbar options and report success.
bool UiTextList::EnableScrollBar(bool first, bool second) {
  allow_scrollbar = first;
  state262 = second;
  return 1;
}

// Snap the live height down to a whole number of text rows.
// Position and refresh the scrollbar before invalidation.
bool UiTextList::ResizeWindowForExactLineHeights() {
  if (!row_height)
    return 0;
  i32 height = GetH(), inset = 2 * (i32)gutters[3];
  i32 count = (i32)((u32)height - inset) / row_height;
  i32 fitted = (i32)((u32)inset + (u32)count * row_height);
  if (fitted != height) {
    SetH(fitted);
    visible_rows = count;
  }
  SetScrollbarSizes();
  SetScrollbarValue();
  InvalidateSelfAndParents();
  return 1;
}

// Release and clear the first font before reading the live second font, then close the copied window.
bool UiTextList::Shutdown() {
  if (fonts[0]) {
    fonts[0]->Release();
    fonts[0] = nullptr;
  }
  if (fonts[1]) {
    fonts[1]->Release();
    fonts[1] = nullptr;
  }
  return UiBaseWindow::Shutdown();
}

// Update one of three font pixels and invalidate only when its full DWORD value changes.
bool UiTextList::SetFontColor(i32 index, u32 pixel) {
  if (index < 0 || index >= 3)
    return 0;
  if (text_colors[index] != pixel) {
    text_colors[index] = pixel;
    InvalidateSelfAndParents();
  }
  return 1;
}

// Write the selected font pixel through the genuine output pointer; invalid indices leave it untouched.
bool UiTextList::GetFontColor(i32 index, u32 *pixel) {
  if (index < 0 || index >= 3)
    return 0;
  *pixel = text_colors[index];
  return 1;
}

bool UiTextList::ResetFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

// Copy the second input before the first and insert their concrete values.
// Retire both copies before invalidating and relayout only an initialized list.
bool UiTextList::AddRow(
    const ScStringView &first, const ScStringView &second, u32 first_color, u32 second_color, i32 index) {
  {
    ScString second_copy(second), first_copy(first);
    InsertStringPairAtIndex(index, first_copy, second_copy, first_color, second_color);
  }
  InvalidateSelfAndParents();
  if (GetFlag(kWnFlagInitialized))
    CalculateAllMetrics();
  return 1;
}

bool UiTextList::AddRow(const ScStringView &first, const ScStringView &second, i32 index) {
  return AddRow(first, second, kWnDefaultColor, kWnDefaultColor, index);
}

// Index zero prepends; negative or out-of-range nonzero indices append. New rows copy both concrete strings.
bool UiTextList::InsertStringPairAtIndex(
    i32 index, const ScString &first, const ScString &second, u32 first_color, u32 second_color) {
  UiTextRow *position = rows;
  if (index == 0)
    position = rows->next;
  else if (index > 0) {
    UiTextRow *row = rows->next;
    for (i32 current = 0; row != rows; row = row->next, ++current)
      if (current == index) {
        position = row;
        break;
      }
  }
  UiTextRow *row = new UiTextRow;
  row->first = first;
  row->second = second;
  row->colors[0] = first_color;
  row->colors[1] = second_color;
  row->tag = 0;
  row->next = position;
  row->previous = position->previous;
  position->previous->next = row;
  position->previous = row;
  return 1;
}

// Find a live source row by signed index; the original searches return no row for negative or past-end indices.
UiTextList::UiTextRow *UiTextList::FindRow(i32 index) {
  i32 current = 0;
  for (UiTextRow *row = rows->next; row != rows; row = row->next, ++current)
    if (current == index)
      return row;
  return nullptr;
}

// Copy each concrete row's bytes into the caller's actual string outputs, preserving first-before-second dispatch.
bool UiTextList::GetRowText(ScStringView *first, ScStringView *second, i32 index) {
  UiTextRow *row = FindRow(index);
  if (!row)
    return 0;
  first->FromChar(row->first.begin, row->first.ScString::Strlen());
  second->FromChar(row->second.begin, row->second.ScString::Strlen());
  return 1;
}

// Unlink and destroy one source row before relayout and invalidation; an absent index leaves both lists intact.
bool UiTextList::RemoveRow(i32 index) {
  UiTextRow *row = FindRow(index);
  if (!row)
    return 0;
  row->previous->next = row->next;
  row->next->previous = row->previous;
  delete row;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return 1;
}

// Destroy all actual rows while preserving the circular sentinel and its identity.
void UiTextList::ClearRows(UiTextRow *head) {
  UiTextRow *row = head->next;
  while (row != head) {
    UiTextRow *next = row->next;
    delete row;
    row = next;
  }
  head->next = head->previous = head;
}

bool UiTextList::RemoveAllRows() {
  ClearRows(rows);
  ClearRows(wrapped_rows);
  InvalidateSelfAndParents();
  return 1;
}

// Copy into the selected concrete column before retiring the temporary, then relayout and invalidate.
bool UiTextList::SetRowText(const ScStringView &input, i32 index, bool first) {
  UiTextRow *row = FindRow(index);
  if (!row)
    return 0;
  {
    ScString copy(input);
    (first ? row->first : row->second) = copy;
  }
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return 1;
}

// Store the full pixel in the selected row column before requesting layout and invalidation.
bool UiTextList::SetRowColor(u32 pixel, i32 index, bool first) {
  UiTextRow *row = FindRow(index);
  if (!row)
    return 0;
  row->colors[first ? 0 : 1] = pixel;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return 1;
}

bool UiTextList::IsItemVisible(i32 index) {
  return index >= first_visible && index < (i32)((u32)first_visible + visible_rows);
}

// Count wrapped display rows rather than source rows; wrapping may give the two lists different lengths.
i32 UiTextList::GetItemCount() const {
  u32 count = 0;
  for (UiTextRow *row = wrapped_rows->next; row != wrapped_rows; row = row->next)
    ++count;
  return (i32)count;
}

i32 UiTextList::GetFirstVisible() const {
  return first_visible;
}

void UiTextList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiBaseWindow::SetArea(left, top, right, bottom);
  CalculateAllMetrics();
}

// Borrow the vertical scrollbar after retiring the successful child-query acquisition; failed outputs are ignored.
WnScrollbarControl *UiTextList::GetVerticalScrollbar() {
  void *value = nullptr;
  if (!GetChildAs(kWnTextListVerticalScrollbarId, IID_WnScrollbarControl, &value))
    return nullptr;
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(value);
  scrollbar->Release();
  return static_cast<WnScrollbarControl *>(value);
}

// Borrow the horizontal scrollbar through its distinct child ID and release only a successful interface acquisition.
WnScrollbarControl *UiTextList::GetHorizontalScrollbar() {
  void *value = nullptr;
  if (!GetChildAs(kWnTextListHorizontalScrollbarId, IID_WnScrollbarControl, &value))
    return nullptr;
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(value);
  scrollbar->Release();
  return static_cast<WnScrollbarControl *>(value);
}

// Propagate exactly flag2 through the independently borrowed scrollbar windows before mutating the copied root.
u32 UiTextList::SetFlag(u32 mask, bool enabled) {
  if (mask == kWnFlagEnabled) {
    WnScrollbarControl *scrollbar = GetVerticalScrollbar();
    if (scrollbar)
      scrollbar->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
    scrollbar = GetHorizontalScrollbar();
    if (scrollbar)
      scrollbar->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
  }
  return UiBaseWindow::SetFlag(mask, enabled);
}

bool UiTextList::MoveLineUp() {
  PutIndexInView((i32)((u32)first_visible - 1));
  return 1;
}

bool UiTextList::MoveLineDown() {
  PutIndexInView((i32)((u32)first_visible + 1));
  return 1;
}

// Refuse an empty wrapped list; otherwise move its scrolling origin to zero.
bool UiTextList::ScrollToStart() {
  if (GetItemCount() <= 0)
    return 0;
  SetIndexOfTopLine(0);
  return 1;
}

bool UiTextList::ScrollToEnd() {
  i32 count = GetItemCount();
  if (count > 0)
    SetIndexOfTopLine(visible_rows < count ? (i32)((u32)count - visible_rows) : 0);
  return 1;
}

// Capture the old origin before querying count, then scroll one overlapping page upward and clamp only at zero.
bool UiTextList::ScrollPageUp() {
  i32 first = first_visible;
  if (GetItemCount() > 0) {
    i32 next = (i32)(1u - (u32)visible_rows + first);
    if (next < 0)
      next = 0;
    SetIndexOfTopLine(next);
  }
  return 1;
}

// Preserve the captured old origin and the later live end-of-list test before scrolling one overlapping page down.
bool UiTextList::ScrollPageDown() {
  i32 first = first_visible, count = GetItemCount();
  if (count > 0) {
    i32 visible = visible_rows;
    if (first_visible < (i32)((u32)count - visible))
      SetIndexOfTopLine((i32)((u32)first + visible - 1));
  }
  return 1;
}

// Bring the row into view, then refresh the scrollbar and invalidate after any nested origin change.
void UiTextList::PutIndexInView(i32 index) {
  if (index < first_visible)
    SetIndexOfTopLine(index);
  else if (index > (i32)((u32)visible_rows + first_visible - 1))
    SetIndexOfTopLine((i32)((u32)index - visible_rows + 1));
  SetScrollbarValue();
  InvalidateSelfAndParents();
}

// Publish the new origin before clamping through the live item count. Refresh the scrollbar before invalidation.
void UiTextList::SetIndexOfTopLine(i32 index) {
  first_visible = index;
  i32 count = GetItemCount();
  if (count > visible_rows) {
    count = GetItemCount();
    if (first_visible > (i32)((u32)count - visible_rows)) {
      count = GetItemCount();
      first_visible = (i32)((u32)count - visible_rows);
    }
  }
  if (first_visible < 0)
    first_visible = 0;
  SetScrollbarValue();
  InvalidateSelfAndParents();
}

// Clamp the requested selection except the explicit -1 sentinel, expose changed rows and optionally notify afterward.
bool UiTextList::SetSelection(i32 index, bool notify) {
  i32 count = GetItemCount(), selected = index;
  if (index != -1) {
    if (index < 0)
      selected = 0;
    if (selected >= count)
      selected = (i32)((u32)count - 1);
  }
  if (selected != selection) {
    selection = selected;
    if (selected >= 0 && !IsItemVisible(selected))
      PutIndexInView(selected);
    if (notify)
      NotifySelection();
    InvalidateSelfAndParents();
  }
  return 1;
}

i32 UiTextList::GetSelection() const {
  return selection;
}

// Convert the vertical point through the live row height and origin, retaining the original optional edge clamping.
u32 UiTextList::GetItemIndexFromCursorPosition(i32, i32 y, bool clamp) const {
  i32 count = GetItemCount();
  if (!count)
    return UINT32_MAX;
  if (y < 0 && clamp)
    y = 0;
  if (y >= GetH() && clamp)
    y = (i32)((u32)GetH() - 1);
  i32 relative = (i32)((u32)y - (i32)gutters[3]);
  i32 index = (i32)((u32)first_visible + relative / row_height);
  if (index >= count)
    return clamp ? (u32)count - 1 : UINT32_MAX;
  return (u32)index;
}

bool UiTextList::GZCharacter(u32) {
  return owner->GetCapture() != static_cast<WnWindow *>(this);
}

// Capture the live window ID before delivering the original synchronous selection notification and ignore its reply.
u8 UiTextList::NotifySelection() {
  u32 window_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListSelectionChanged, window_id, 0);
  return 1;
}

// Capture the live window ID before the distinct activation notification, preserving its constant success byte.
u8 UiTextList::NotifyActivation() {
  u32 window_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClicked, window_id, 0);
  return 1;
}

// Ignore keys while captured; Enter activates, paging/arrows dispatch live scrolling and other keys reach the target.
bool UiTextList::OnKeyDown(u32 key, u32 modifiers) {
  if (owner->GetCapture() == static_cast<WnWindow *>(this))
    return false;
  i32 previous = selection;
  if (key == kWnKeyReturn) {
    SetSelection(previous, 1);
    return NotifyActivation() != 0;
  }
  switch (key) {
  case kWnKeyPageUp:
    ScrollPageUp();
    break;
  case kWnKeyPageDown:
    ScrollPageDown();
    break;
  case kWnKeyUp:
    MoveLineUp();
    break;
  case kWnKeyDown:
    MoveLineDown();
    break;
  default:
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  }
  if (selection != previous)
    SetSelection(selection, 1);
  return true;
}

bool UiTextList::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Resolve and select the clicked row before updating the shared elapsed timer and per-window double-click state.
bool UiTextList::OnMouseDownL(i32 x, i32 y, u32) {
  static ScStopwatch timer(ScStopwatch::Milliseconds, 0);
  static u32 last_time = 0;
  i32 index = (i32)GetItemIndexFromCursorPosition(x, y, 1);
  owner->SetCapture(this);
  if (index < GetItemCount() && index != GetSelection())
    SetSelection(index, 1);
  if (last_time) {
    if (index == last_click) {
      u32 elapsed = timer.GetElapsedTime() - last_time;
      if (elapsed < WnDoubleClickTime())
        double_click = 1;
    }
    last_click = index;
    last_time = timer.GetElapsedTime();
  } else {
    timer.Restart();
    last_time = 1;
    last_click = index;
  }
  return true;
}

bool UiTextList::OnMouseDownR(i32 x, i32 y, u32 modifiers) {
  return OnMouseDownL(x, y, modifiers);
}

// Forward captured scrollbar releases in child coordinates; otherwise release capture and deliver pending activation.
bool UiTextList::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  if (owner->GetCapture() == static_cast<WnWindow *>(this)) {
    if (IsPointInWindow(x, y)) {
      WnScrollbarControl *scrollbar = GetVerticalScrollbar();
      if (scrollbar) {
        WnWindow *window = scrollbar->GetWindow();
        i32 child_x = x, child_y = y;
        WindowToWindowCoordinates(window, &child_x, &child_y);
        if (child_x >= 0 && child_y >= 0 && child_x < window->GetW() && child_y < window->GetH()) {
          ScMessage message{kWnMsg_MouseUpL, (u32)child_x, (u32)child_y, modifiers};
          bool result = window->DoMessage(&message);
          owner->SetCapture(this);
          return result;
        }
      }
    }
    owner->ReleaseCapture(this);
    if (double_click) {
      NotifyActivation();
      last_click = -1;
      double_click = 0;
    }
  }
  return true;
}

bool UiTextList::OnMouseUpR(i32 x, i32 y, u32 modifiers) {
  return OnMouseDownR(x, y, modifiers);
}

// Append a complete native display row only after copying both actual strings and their separate pixels.
UiTextList::UiTextRow *UiTextList::AppendWrappedRow(
    const ScString &first, const ScString &second, u32 first_color, u32 second_color) {
  UiTextRow *row = new UiTextRow;
  row->first = first;
  row->second = second;
  row->colors[0] = first_color;
  row->colors[1] = second_color;
  row->tag = 0;
  row->next = wrapped_rows;
  row->previous = wrapped_rows->previous;
  wrapped_rows->previous->next = row;
  wrapped_rows->previous = row;
  return row;
}

// Rebuild the two independently wrapped columns, align their trailing lines and refresh the real scrollbar afterward.
void UiTextList::CalculateAllMetrics() {
  ScString first_value, second_value;
  u32 first_color = 0, second_color = 0;
  if (!fonts[0]) {
    fonts[0] = UiGetWindowResources()->GetFont(kWnFontDefault);
    fonts[0]->AddRef();
  }
  if (!fonts[1]) {
    fonts[1] = UiGetWindowResources()->GetFont(kWnFontDefault);
    fonts[1]->AddRef();
  }
  row_height = fonts[1]->Height(0);
  visible_rows = (i32)((u32)draw_area.bottom - draw_area.top) / row_height;
  ClearRows(wrapped_rows);
  bool scrollbar_present = false;
  i32 scrollbar_width = 0;
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  if (scrollbar && scrollbar->GetWindow()) {
    scrollbar_present = true;
    scrollbar_width = scrollbar->GetWindow()->GetW();
  }
  i32 first_space, second_space;
  if (first_width == -1) {
    first_space = (i32)((u32)GetW() - 2u * (u32)(i32)gutters[2] - scrollbar_width) / 2;
    first_space = (i32)((u32)first_space - (i32)gap);
    second_space = (i32)((u32)first_space + (i32)gap);
  } else {
    first_space = (i32)((u32)first_width - (i32)gap - (i32)gutters[2]);
    second_space = (i32)((u32)GetW() - (i32)gutters[2] - first_width - scrollbar_width);
  }
  if (first_space < 0)
    first_space = 0;
  if (second_space < 0)
    second_space = 0;
  for (UiTextRow *source = rows->next; source != rows; source = source->next) {
    i32 first_lines = 0;
    ScString first_remaining(source->first);
    u32 fitted = fonts[0]->WrapFit(first_remaining.begin, first_remaining.ScString::Strlen(), (u32)first_space, 1);
    UiTextRow *line_cursor = wrapped_rows->previous;
    while (fitted < first_remaining.ScString::Strlen()) {
      first_value.FromChar(first_remaining.begin, fitted);
      first_remaining.Erase(0, fitted);
      first_color = source->colors[0];
      AppendWrappedRow(first_value, second_value, first_color, second_color);
      fitted = fonts[0]->WrapFit(first_remaining.begin, first_remaining.ScString::Strlen(), (u32)first_space, 1);
      ++first_lines;
    }
    first_value = first_remaining;
    first_color = source->colors[0];
    AppendWrappedRow(first_value, second_value, first_color, second_color);
    ++first_lines;
    ScString second_remaining(source->second);
    i32 second_lines =
        (i32)fonts[1]->WrapLines(second_remaining.begin, second_remaining.ScString::Strlen(), (u32)second_space);
    fitted = fonts[1]->WrapFit(second_remaining.begin, second_remaining.ScString::Strlen(), (u32)second_space, 1);
    if (first_lines > second_lines) {
      i32 blanks = (i32)((u32)first_lines - second_lines);
      first_lines = second_lines;
      while (blanks--) {
        line_cursor = line_cursor->next;
        line_cursor->second.FromChar("", 0);
        line_cursor->colors[1] = source->colors[1];
      }
    }
    while (fitted < second_remaining.ScString::Strlen() || first_lines > 0) {
      u32 length = second_remaining.ScString::Strlen();
      u32 consumed = fitted < length ? fitted : length;
      ScString segment(second_remaining.begin, consumed);
      second_remaining.Erase(0, consumed);
      if (first_lines <= 0) {
        first_value.FromChar("", 0);
        second_value = segment;
        first_color = source->colors[0];
        second_color = source->colors[1];
        AppendWrappedRow(first_value, second_value, first_color, second_color);
      } else {
        line_cursor = line_cursor->next;
        line_cursor->second = segment;
        line_cursor->colors[1] = source->colors[1];
        --first_lines;
      }
      fitted = fonts[1]->WrapFit(second_remaining.begin, second_remaining.ScString::Strlen(), (u32)second_space, 1);
    }
    if (second_remaining.ScString::Strlen()) {
      first_value.FromChar("", 0);
      second_value = second_remaining;
      first_color = source->colors[0];
      second_color = source->colors[1];
      AppendWrappedRow(first_value, second_value, first_color, second_color);
    }
  }
  if (scrollbar_present || GetItemCount() <= visible_rows) {
    SetScrollbarSizes();
    SetScrollbarValue();
  } else {
    AddScrollbar(1, 0);
    CalculateAllMetrics();
  }
}

// Create the actual provider scrollbar only when allowed and absent, then attach, release, position and refresh it.
bool UiTextList::AddScrollbar(bool enabled, bool) {
  bool result = false;
  WnResources *provider = UiGetWindowResources();
  if (provider && enabled && allow_scrollbar && !ChildExists(kWnTextListVerticalScrollbarId)) {
    WnScrollbarControl *scrollbar = provider->CreateScrollbar(kWnTextListVerticalScrollbarId, 2, 0, 0, 0);
    result = ChildAdd(scrollbar->GetWindow());
    scrollbar->Release();
    SetScrollbarSizes();
    SetScrollbarValue();
  }
  return result;
}

void UiTextList::RemoveScrollbar(bool enabled, bool) {
  if (enabled) {
    WnScrollbarControl *scrollbar = GetVerticalScrollbar();
    if (scrollbar)
      ChildDelete(scrollbar->GetWindow());
  }
}

void UiTextList::SetScrollbarValue() {
  if (!visible_rows)
    return;
  i32 count = GetItemCount();
  i32 maximum = (i32)((u32)count - visible_rows);
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  if (scrollbar) {
    if (maximum <= 0) {
      RemoveScrollbar(1, 0);
      return;
    }
  } else {
    if (maximum <= 0 || !AddScrollbar(1, 0))
      return;
    scrollbar = GetVerticalScrollbar();
    if (!scrollbar)
      return;
  }
  scrollbar->SetRange(0, maximum);
  count = GetItemCount();
  i32 pages = (i32)((u32)(count / visible_rows) + 1);
  scrollbar->SetDivisors(maximum, pages);
  scrollbar->SetValue(GetFirstVisible());
}

void UiTextList::SetScrollbarSizes() {
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  WnWindow *window = scrollbar ? scrollbar->GetWindow() : nullptr;
  if (window) {
    i32 bottom = GetH(), right = GetW();
    i32 left = (i32)(1u - (u32)window->GetW());
    left = (i32)((u32)left + GetW());
    window->SetArea(left, 0, right, bottom);
  }
}

// Draw the live wrapped rows and selection, preserving independent buffer locks and the source second-column formula.
// Scrollbar acquisition precedes background fill; optional outlines follow every visible row.
bool UiTextList::GZPaint() {
  UiTextRow *row = wrapped_rows->next;
  ScRect bounds = draw_area;
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  if (scrollbar) {
    bounds.right = (i32)((u32)bounds.right - gutters[0] - scrollbar->GetWindow()->GetW());
    scrollbar->GetWindow()->GetW();
    first_visible = scrollbar->GetValue();
  }
  if (fill_background && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    draw_buffer->Fill(pixel, bounds, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  ScRect line = bounds;
  line.left = (i32)((u32)line.left + gutters[2]);
  line.right = (i32)((u32)line.right - gutters[2]);
  i32 source_index = 0;
  while (source_index < first_visible && row != wrapped_rows) {
    ++source_index;
    row = row->next;
  }
  i32 split = first_width == -1 ? (i32)((u32)line.left + line.right) / 2 : (i32)((u32)bounds.left + first_width);
  for (i32 visible = 0; row != wrapped_rows && visible < visible_rows; ++visible) {
    u32 first_color = row->colors[0], second_color = row->colors[1];
    i32 top = (i32)((u32)bounds.top + gutters[3] + (u32)visible * row_height);
    line.top = top;
    line.bottom = (i32)((u32)top + row_height);
    i32 limit = (i32)((u32)bounds.bottom - gutters[3]);
    if (line.bottom > limit)
      line.bottom = limit;
    if (first_color == kWnDefaultColor)
      first_color = text_colors[0];
    if (second_color == kWnDefaultColor)
      second_color = text_colors[0];
    if (source_index == selection) {
      first_color = second_color = text_colors[2];
      // The original ordered x87 comparison skips zero and unordered opacity values.
      if (highlight_alpha < 0.0f || highlight_alpha > 0.0f) {
        ScRect highlight = line;
        if (highlight_alpha == 1.0f) {
          if (draw_buffer->Lock(GxLockFlags::Fill)) {
            draw_buffer->Fill(highlight_color, highlight, nullptr);
            draw_buffer->Unlock(GxLockFlags::Fill);
          }
        } else if (draw_buffer->Lock(GxLockFlags::Bits)) {
          draw_buffer->AlphaFill(highlight_color, highlight, highlight_alpha, nullptr);
          draw_buffer->Unlock(GxLockFlags::Bits);
        }
      }
    }
    fonts[0]->DrawTextA(
        draw_buffer, (u32)line.left, (u32)top, row->first.begin, row->first.ScString::Strlen(), first_color);
    i32 second_x = split;
    if (alternate_alignment) {
      u32 width = fonts[1]->Measure(row->second.begin, row->second.ScString::Strlen());
      second_x = (i32)((u32)split * 2 - line.right + width);
    }
    fonts[1]->DrawTextA(
        draw_buffer, (u32)second_x, (u32)top, row->second.begin, row->second.ScString::Strlen(), second_color);
    row = row->next;
    ++source_index;
  }
  if (line_outline) {
    if (draw_buffer->Lock(GxLockFlags::Line)) {
      draw_buffer->DrawLineNoClip(
          bounds.left, bounds.top, bounds.right, bounds.top, static_cast<i32>(outline_colors[0]), 1);
      draw_buffer->DrawLineNoClip(
          bounds.left, bounds.top, bounds.left, bounds.bottom, static_cast<i32>(outline_colors[0]), 1);
      draw_buffer->DrawLineNoClip(
          bounds.left, bounds.bottom, bounds.right, bounds.bottom, static_cast<i32>(outline_colors[1]), 1);
      draw_buffer->DrawLineNoClip(
          bounds.right, bounds.top, bounds.right, bounds.bottom, static_cast<i32>(outline_colors[1]), 1);
      draw_buffer->Unlock(GxLockFlags::Line);
    }
  } else if (rectangle_outline && draw_buffer->Lock(GxLockFlags::Rect)) {
    draw_buffer->DrawRectangleOutline(bounds, border_colors[0], 1);
    draw_buffer->Unlock(GxLockFlags::Rect);
  }
  return true;
}
