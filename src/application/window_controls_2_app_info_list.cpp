#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "application/window_controls.h"
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "application/application_core.h"
#include "audio/local_radio.h"
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/utf8.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "ui/ui_resource_handles.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/double_click.h"
#include "window/input.h"
#include "window/text_edit.h"

// Snapshot incoming bytes before clearing text, then filter and limit them through the real editor rules.
// Invalidate before clearing both histories so reentrant observers see the source's old history until notification returns.
bool WnAppEditor::SetText(const char *bytes, u32 count) {
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
bool WnAppEditor::SetText(const ScStringView &value) {
  u32 length = value.Strlen();
  const char *bytes = value.Data();
  return SetText(bytes, length);
}

// Convert the requested line and column into the genuine index output, ignoring the lookup Boolean before insertion.
u32 WnAppEditor::InsertText(const char *bytes, u32 count, u32 line, u32 column) {
  u32 index;
  GetIndexFromLineAndColumn(line, column, &index);
  return InsertText(bytes, count, index);
}

u32 WnAppEditor::RemoveText(u32 line, u32 column, u32 count, ScStringView *removed) {
  u32 index;
  GetIndexFromLineAndColumn(line, column, &index);
  return RemoveText(index, count, removed);
}

// Convert both endpoints and preserve the first endpoint as the erase start even when the range is reversed.
// The installed method ignores its fifth output argument and explicitly forwards a null removed-text output.
u32 WnAppEditor::RemoveText(u32 from_line, u32 from_column, u32 to_line, u32 to_column, ScStringView *) {
  u32 first, last;
  GetIndexFromLineAndColumn(from_line, from_column, &first);
  GetIndexFromLineAndColumn(to_line, to_column, &last);
  return RemoveText(first, first <= last ? last - first : first - last, nullptr);
}

// Retain the incoming vertical atlas before releasing the live old image, then configure an existing scrollbar.
// The original ignores its rectangle argument and uses the stored rectangle or the incoming buffer's area.
bool WnAppEditor::SetScrollBarImageVertical(GxBuffer *incoming, const ScRect &) {
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
bool WnAppEditor::SetScrollBarImageHorizontal(GxBuffer *incoming, const ScRect &) {
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

// Preserve the source's final rejection of every result at text length, including empty-needle end positions.
u32 WnAppEditor::FindText(const char *bytes, u32 count, u32 position, bool forward) {
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

// Find the source inclusive visible byte span and horizontal drawing offset through actual in/out font measurements.
// SDL measures complete UTF-8 spans; reference builds retain the source's original prefix scan and line length.
void WnAppEditor::FindFirstAndLastCharacterIndexForALineLongerThanTheScreenWidth(
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
bool WnAppEditor::GZPaint() {
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

// Copy the removed span to an optional genuine output before erasing, reflowing and adjusting the live viewport.
// Return the clamped byte count directly; positions at or past the text end return zero without effects.
u32 WnAppEditor::RemoveText(u32 index, u32 count, ScStringView *removed) {
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

// Step the insertion index by source characters in double-byte mode or by raw bytes otherwise, then clamp and convert.
void WnAppEditor::IncrementInsertionPosition(i32 delta) {
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
bool WnAppEditor::RemoveChar(u32 index, [[maybe_unused]] bool move, ScStringView *removed) {
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
bool WnAppEditor::SetSound(i32 index, i32 incoming) {
  if (index >= 4)
    return false;
  assert(index >= 0);
  sounds[index] = incoming;
  return true;
}

// Resolve borrowed sound and resource services only for a configured sound, then forward the device's raw byte result.
// Sound fields remain unwritten until configured, as in the original constructor.
u8 WnAppEditor::PlaySound(i32 index) {
  assert(index >= 0 && index < 4);
  u32 *sound = sounds + index;
  if (!*sound)
    return 0;
  AuSoundDevice *device = AuGetAppSoundDevice();
  if (!device)
    return 0;
  WnResources *resources = ApGetUiResources();
  if (!resources)
    return 0;
  i32 volume = resources->GetVolume();
  return device->SndPlay((i32)*sound, volume, 0);
}

// Filter a concrete input snapshot, apply the source's character-aware byte limit, then insert and move the live caret.
// Return the chosen byte count directly after original reflow, viewport, scrollbar and invalidation effects.
u32 WnAppEditor::InsertText(const char *bytes, u32 count, u32 index) {
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
bool WnAppEditor::AddUndoRecordForTextInsertion(
    const WnEditPosition &position, u32 index, const char *, u32 count, const char *removed, u32 removed_count) {
  if (!(editor_flags & 0x100))
    return false;
  if (undo.size() >= undo_limit)
    undo.erase(undo.begin(), undo.begin() + static_cast<isize>(undo.size() >> 2));
  WnAppEditAction action;
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
bool WnAppEditor::AddUndoRecordForTextTextDeletion(
    const WnEditPosition &position, u32 index, const char *removed, u32 count) {
  if (!(editor_flags & 0x100))
    return false;
  WnAppEditAction action;
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
bool WnAppEditor::Undo() {
  if (!(editor_flags & 0x100) || !undo.size())
    return false;
  WnAppEditAction *action = &undo.back();
  WnAppEditAction inverse{};
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
bool WnAppEditor::Redo() {
  if (!(editor_flags & 0x100) || !redo.size())
    return false;
  WnAppEditAction *action = &redo.back();
  WnAppEditAction inverse{};
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

// Assemble a complete input character before editing, preserving keyboard and capture gating and source history order.
// SDL assembles UTF-8; reference builds retain the original double-byte rules. Pending input returns true.
bool WnAppEditor::GZOnCharacter(char incoming) {
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

// Advance across space/newline word boundaries, then clamp and convert the live insertion index.
void WnAppEditor::IncrementInsertionPositionByWord(i32 delta) {
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
bool WnAppEditor::OnKeyDown(u32 key, u32 modifiers) {
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

// Initialize the editor's owned state and apply the supplied rectangle through the base window implementation.
WnAppEditor::WnAppEditor(const ScRect &area, i32 initial_flags) : elapsed(ScStopwatch::Milliseconds, 0) {
  editor_flags = 0;
  InitializeMemberVariables(initial_flags);
  UiWindow::SetArea(area.left, area.top, area.right, area.bottom);
}

WnAppEditAction::WnAppEditAction(const WnAppEditAction &other)
    : erase(other.erase), erase_index(other.erase_index), erase_count(other.erase_count), insert(other.insert),
      insert_index(other.insert_index), text(other.text), position(other.position) {
}

WnAppEditAction::~WnAppEditAction() {
}

// Replace the saved editing operations and text before copying the recorded caret position.
WnAppEditAction &WnAppEditAction::operator=(const WnAppEditAction &other) {
  erase = other.erase;
  erase_index = other.erase_index;
  erase_count = other.erase_count;
  insert = other.insert;
  insert_index = other.insert_index;
  text = other.text;
  position = other.position;
  return *this;
}

bool WnAppEditor::IsInsertionPositionInView() {
  return insertion_line >= first_visible && insertion_line < first_visible + visible_rows;
}

// Convert a local position to a byte index through the current line offsets and measured column.
void WnAppEditor::GetIndexFromPosition(u32 *index, i32 x, i32 y) {
  if (!text.ScString::Strlen()) {
    *index = 0;
    return;
  }
  u32 line, column;
  GetLineAndColumnFromPosition(&line, &column, x, y);
  *index = lines[line] + column;
}

u32 WnAppEditor::CharCountFromText(const char *bytes, u32 length) {
  if (!double_byte)
    return length;
  u32 position = 0, result = length;
  while (position < length) {
    position += NextCharSizeFromText(bytes + position, length - position);
    ++result;
  }
  return result;
}

// Filter a mutable byte buffer according to the editor's character rules and update its length only when bytes are removed.
// The installed routine repeatedly copies from the start of the supplied buffer for each accepted character.
u32 WnAppEditor::RemoveBadCharacters(char *bytes, u32 *length) {
  ScString filtered(bytes, *length);
  u32 removed = RemoveBadCharacters(&filtered);
  if (removed) {
    *length = filtered.ScString::Strlen();
    memcpy(bytes, filtered.Data(), *length);
  }
  return removed;
}

// Classify the byte at a character boundary using the installed language-specific continuation rules.
[[maybe_unused]] static i32 WnAppCharacterBoundary(const char *bytes, u32 offset, i32 language) {
  if (language == kScLanguageThai)
    return WnAppEditorContinuation((u8)bytes[offset]) ? 1 : 0;
  i32 state = 0;
  for (u32 i = 0; i <= offset; ++i)
    state = state == 1 ? 2 : WnAppEditorLeadByte(language, (u8)bytes[i]);
  return state;
}

// Return the backward displacement to a character boundary; SDL can move up to three UTF-8 bytes.
// The index must belong to the text; reference builds retain the original line-based double-byte rule.
i32 WnAppEditor::FixCharacterPositionBoundaryIfNecessary(u32 index) {
  u32 length = text.ScString::Strlen();
  assert(index <= length);
  u32 boundary = ScUtf8Floor(text.begin, length, index);
  return -static_cast<i32>(index - boundary);
}

WnInfoPair::WnInfoPair() : colors{0, 0}, tag(0) {
}

WnInfoPair::WnInfoPair(const WnInfoPair &other)
    : first(other.first), second(other.second), colors{other.colors[0], other.colors[1]}, tag(other.tag) {
}

WnInfoPair::~WnInfoPair() {
}

bool WnAppInfoList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnTextListControl, WnAppInfoListControl);
  return UiWindow::QueryInterface(iid, out);
}

u32 WnAppInfoList::AddRef() {
  return UiWindow::AddRef();
}

u32 WnAppInfoList::Release() {
  return UiWindow::Release();
}

// Borrow the embedded window without an interface query or reference operation.
WnWindow *WnAppInfoList::AsIGZWin() {
  return this;
}

// Store a changed full-width selection-background pixel before invalidating the window and its parents.
// Unchanged values have no side effects, and the original method returns no value.
void WnAppInfoList::SetHighlightColor(u32 pixel) {
  if (background_color != pixel) {
    background_color = pixel;
    InvalidateSelfAndParents();
  }
}

// Replace both retained column fonts in order through the original void setter.
// Each incoming retain precedes release of that column's live previous font and publication of the new pointer.
void WnAppInfoList::SetFonts(FtFont *first, FtFont *second) {
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
void WnAppInfoList::SetHighlightFillAlpha(f32 opacity) {
  if (opacity < background_opacity || opacity > background_opacity) {
    if (opacity > 1.0f)
      opacity = 1.0f;
    else if (opacity < 0.0f)
      opacity = 0.0f;
    background_opacity = opacity;
    InvalidateSelfAndParents();
  }
}

void WnAppInfoList::SetRightColumnTextStart(i32 width) {
  first_width = width;
  CalculateAllMetrics();
}

void WnAppInfoList::SetJustification(bool value) {
  alternate_alignment = value;
}

void WnAppInfoList::SetOpaque(bool value) {
  fill_background = value;
}

// Select rectangle outlining and clear the mutually exclusive sunken outline before parent-chain invalidation.
// The original Boolean setter returns no value.
void WnAppInfoList::SetOutlineDrawing(bool value) {
  line_outline = 0;
  rectangle_outline = value;
  InvalidateSelfAndParents();
}

// Select the sunken outline and clear the mutually exclusive rectangle outline before parent-chain invalidation.
// The original Boolean setter returns no value.
void WnAppInfoList::SetDrawSunkenRect(bool value) {
  rectangle_outline = 0;
  line_outline = value;
  InvalidateSelfAndParents();
}

void WnAppInfoList::SetSunkenRectColors(u32 first, u32 second) {
  outline_colors[0] = first;
  outline_colors[1] = second;
}

// Store both original Boolean scrollbar options and return true without creating or deleting a child.
bool WnAppInfoList::EnableScrollBar(bool first, bool second) {
  allow_scrollbar = first;
  state262 = second;
  return true;
}

// Update one of three font pixels and invalidate only when its full DWORD value changes.
bool WnAppInfoList::SetFontColor(i32 index, u32 pixel) {
  if (index < 0 || index >= 3)
    return false;
  if (text_colors[index] != pixel) {
    text_colors[index] = pixel;
    InvalidateSelfAndParents();
  }
  return true;
}

// Write the selected font pixel through the genuine output pointer; invalid indices leave it untouched.
bool WnAppInfoList::GetFontColor(i32 index, u32 *pixel) {
  if (index < 0 || index >= 3)
    return false;
  *pixel = text_colors[index];
  return true;
}

bool WnAppInfoList::ClearFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

bool WnAppInfoList::IsIndexInView(i32 index) {
  return index >= first_visible && index < (i32)((u32)first_visible + visible_rows);
}

// Count wrapped display rows rather than source rows; wrapping may give the two lists different lengths.
i32 WnAppInfoList::GetLineCount() const {
  u32 count = 0;
  for (WnTextRow *row = wrapped_rows->next; row != wrapped_rows; row = row->next)
    ++count;
  return (i32)count;
}

i32 WnAppInfoList::GetIndexOfTopLine() const {
  return first_visible;
}

i32 WnAppInfoList::GetSelection() const {
  return selection;
}

// Borrow the vertical scrollbar after retiring the successful child-query acquisition; failed outputs are ignored.
WnScrollbarControl *WnAppInfoList::GetVerticalScrollbar() {
  void *value = nullptr;
  if (!GetChildAs(kWnTextListVerticalScrollbarId, IID_WnScrollbarControl, &value))
    return nullptr;
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(value);
  scrollbar->Release();
  return static_cast<WnScrollbarControl *>(value);
}

// Borrow the horizontal scrollbar through its distinct child ID and release only a successful interface acquisition.
WnScrollbarControl *WnAppInfoList::GetHorizontalScrollbar() {
  void *value = nullptr;
  if (!GetChildAs(kWnTextListHorizontalScrollbarId, IID_WnScrollbarControl, &value))
    return nullptr;
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(value);
  scrollbar->Release();
  return static_cast<WnScrollbarControl *>(value);
}

WnAppInfoList::WnAppInfoList()
    : language(0), background_opacity(1.0f), background_color(kWnDefaultColor),
      text_colors{kWnDefaultColor, kWnDefaultColor, kWnDefaultColor}, border_colors{kWnDefaultColor, kWnDefaultColor},
      rows(new WnTextRow), wrapped_rows(new WnTextRow), fonts{}, selection(-1), visible_rows(0), first_visible(0),
      row_height(10), first_width(-1), alternate_alignment(0), fill_background(0), rectangle_outline(0),
      line_outline(0), outline_colors{kWnDefaultColor, kWnDefaultColor}, gutters{3, 0, 3, 1}, gap(5), last_click(-1),
      double_click(0), allow_scrollbar(1), state262(0) {
  rows->next = rows->previous = rows;
  wrapped_rows->next = wrapped_rows->previous = wrapped_rows;
  WnResources *provider = ApGetUiResources();
  if (provider) {
    outline_colors[0] = provider->GetColor(19);
    outline_colors[1] = provider->GetColor(20);
  }
  UiWindow::SetFillColor(provider->GetColor(kWnColorUiBackground));
}

// Release retained fonts before destroying wrapped rows, source rows and their sentinels, then enter the window base.
WnAppInfoList::~WnAppInfoList() {
  Shutdown();
  ClearRows(wrapped_rows);
  delete wrapped_rows;
  ClearRows(rows);
  delete rows;
}

// Fill only missing fonts and sentinel theme colors before selecting language, entering base startup and wrapping rows.
bool WnAppInfoList::Init() {
  if (!GetFlag(kWnFlagInitialized)) {
    WnResources *provider = ApGetUiResources();
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
      for (i32 index = 0; index < 3; ++index)
        if (text_colors[index] == kWnDefaultColor)
          SetFontColor(index, provider->GetColor(23u + (u32)index));
      if (background_color == kWnDefaultColor)
        SetHighlightColor(provider->GetColor(22));
    }
    ScLanguageManager *language_manager = ApGetLanguageManager();
    language = language_manager ? language_manager->GetCurrentLanguage() : 1;
    UiWindow::Init();
    CalculateAllMetrics();
  }
  return true;
}

// Release and clear the first font before reading the live second font, then close the inherited root window.
bool WnAppInfoList::Shutdown() {
  if (fonts[0]) {
    fonts[0]->Release();
    fonts[0] = nullptr;
  }
  if (fonts[1]) {
    fonts[1]->Release();
    fonts[1] = nullptr;
  }
  return UiWindow::Shutdown();
}

// Snap the live height down to a whole number of text rows.
// Position and refresh the scrollbar before invalidation.
bool WnAppInfoList::ResizeWindowForExactLineHeights() {
  if (!row_height)
    return false;
  i32 height = GetH(), inset = 2 * (i32)gutters[3];
  i32 inner = (i32)((u32)height - inset);
  assert(inner != INT32_MIN || row_height != -1);
  i32 count = inner / row_height;
  i32 fitted = (i32)((u32)inset + (u32)count * row_height);
  if (fitted != height) {
    SetH(fitted);
    visible_rows = count;
  }
  SetScrollbarSizes();
  SetScrollbarValue();
  InvalidateSelfAndParents();
  return true;
}

// Copy the second input before the first and insert their concrete values.
// Retire both copies before invalidating and relayout only an initialized list.
bool WnAppInfoList::InsertStringPair(
    const ScStringView &first, const ScStringView &second, u32 first_color, u32 second_color, i32 index) {
  {
    ScString second_copy(second), first_copy(first);
    InsertStringPairAtIndex(index, first_copy, second_copy, first_color, second_color);
  }
  InvalidateSelfAndParents();
  if (GetFlag(kWnFlagInitialized))
    CalculateAllMetrics();
  return true;
}

bool WnAppInfoList::InsertStringPair(const ScStringView &first, const ScStringView &second, i32 index) {
  return InsertStringPair(first, second, kWnDefaultColor, kWnDefaultColor, index);
}

// Index zero prepends; negative or out-of-range nonzero indices append. New rows copy both concrete strings.
bool WnAppInfoList::InsertStringPairAtIndex(
    i32 index, const ScString &first, const ScString &second, u32 first_color, u32 second_color) {
  WnTextRow *position = rows;
  if (index == 0)
    position = rows->next;
  else if (index > 0) {
    WnTextRow *row = rows->next;
    for (i32 current = 0; row != rows; row = row->next, ++current)
      if (current == index) {
        position = row;
        break;
      }
  }
  WnTextRow *row = new WnTextRow;
  row->pair.first = first;
  row->pair.second = second;
  row->pair.colors[0] = first_color;
  row->pair.colors[1] = second_color;
  row->pair.tag = 0;
  row->next = position;
  row->previous = position->previous;
  position->previous->next = row;
  position->previous = row;
  return true;
}

// Copy each concrete row's bytes into the caller's actual string outputs, preserving first-before-second dispatch.
bool WnAppInfoList::GetStringPair(ScStringView *first, ScStringView *second, i32 index) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  first->FromChar(row->pair.first.begin, row->pair.first.ScString::Strlen());
  second->FromChar(row->pair.second.begin, row->pair.second.ScString::Strlen());
  return true;
}

// Unlink and destroy one source row before relayout and invalidation; an absent index leaves both lists intact.
bool WnAppInfoList::RemoveStringPair(i32 index) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  row->previous->next = row->next;
  row->next->previous = row->previous;
  delete row;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

bool WnAppInfoList::RemoveAllStringPairs() {
  ClearRows(rows);
  ClearRows(wrapped_rows);
  InvalidateSelfAndParents();
  return true;
}

// Copy into the selected concrete column before retiring the temporary, then relayout and invalidate.
bool WnAppInfoList::ModifyString(const ScStringView &input, i32 index, bool first) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  {
    ScString copy(input);
    (first ? row->pair.first : row->pair.second) = copy;
  }
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

// Store the full pixel in the selected row column before requesting layout and invalidation.
bool WnAppInfoList::ModifyStringColor(u32 pixel, i32 index, bool first) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  row->pair.colors[first ? 0 : 1] = pixel;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

void WnAppInfoList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiWindow::SetArea(left, top, right, bottom);
  CalculateAllMetrics();
}

// Propagate exactly flag2 through the independently borrowed scrollbar windows before mutating the copied root.
u32 WnAppInfoList::SetFlag(u32 mask, bool enabled) {
  if (mask == kWnFlagEnabled) {
    WnScrollbarControl *scrollbar = GetVerticalScrollbar();
    if (scrollbar)
      scrollbar->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
    scrollbar = GetHorizontalScrollbar();
    if (scrollbar)
      scrollbar->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
  }
  return UiWindow::SetFlag(mask, enabled);
}

// Create the actual provider scrollbar only when allowed and absent, then attach, release, position and refresh it.
bool WnAppInfoList::AddScrollbar(bool enabled, bool) {
  bool result = false;
  WnResources *provider = ApGetUiResources();
  if (provider && enabled && allow_scrollbar && !ChildExists(kWnTextListVerticalScrollbarId)) {
    WnScrollbarControl *scrollbar = provider->CreateScrollbar(kWnTextListVerticalScrollbarId, 2, 0, 0, 0);
    result = ChildAdd(scrollbar->GetWindow());
    scrollbar->Release();
    SetScrollbarSizes();
    SetScrollbarValue();
  }
  return result;
}

// Retire the actual scrollbar child only when the first Boolean is enabled.
// The original second Boolean is unused, and the method returns no value.
void WnAppInfoList::RemoveScrollbar(bool enabled, bool) {
  if (enabled) {
    WnScrollbarControl *scrollbar = GetVerticalScrollbar();
    if (scrollbar)
      ChildDelete(scrollbar->GetWindow());
  }
}

// Refresh the scrollbar range, divisors and position through the original void method.
// Retain repeated live item-count queries and scrollbar acquisitions, including creation and removal.
void WnAppInfoList::SetScrollbarValue() {
  if (!visible_rows)
    return;
  i32 count = GetLineCount();
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
  count = GetLineCount();
  assert(count != INT32_MIN || visible_rows != -1);
  i32 pages = (i32)((u32)(count / visible_rows) + 1);
  scrollbar->SetDivisors(maximum, pages);
  scrollbar->SetValue(GetIndexOfTopLine());
}

// Position the live scrollbar through the original void geometry method.
// Preserve height, width, child width and fresh parent width query ordering.
void WnAppInfoList::SetScrollbarSizes() {
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  WnWindow *window = scrollbar ? scrollbar->GetWindow() : nullptr;
  if (window) {
    i32 bottom = GetH(), right = GetW();
    i32 left = (i32)(1u - (u32)window->GetW());
    left = (i32)((u32)left + GetW());
    window->SetArea(left, 0, right, bottom);
  }
}

bool WnAppInfoList::MoveLineUp() {
  PutIndexInView((i32)((u32)first_visible - 1));
  return true;
}

bool WnAppInfoList::MoveLineDown() {
  PutIndexInView((i32)((u32)first_visible + 1));
  return true;
}

// Refuse an empty wrapped list; otherwise move its scrolling origin to zero.
bool WnAppInfoList::ScrollToStart() {
  if (GetLineCount() <= 0)
    return false;
  SetIndexOfTopLine(0);
  return true;
}

// Move to the last full page, or zero when every wrapped row fits; empty lists still return true.
bool WnAppInfoList::ScrollToEnd() {
  i32 count = GetLineCount();
  if (count > 0)
    SetIndexOfTopLine(visible_rows < count ? (i32)((u32)count - visible_rows) : 0);
  return true;
}

// Capture the old origin before querying count, then scroll one overlapping page upward and clamp only at zero.
bool WnAppInfoList::ScrollPageUp() {
  i32 first = first_visible;
  if (GetLineCount() > 0) {
    i32 next = (i32)(1u - (u32)visible_rows + first);
    if (next < 0)
      next = 0;
    SetIndexOfTopLine(next);
  }
  return true;
}

// Preserve the captured old origin and the later live end-of-list test before scrolling one overlapping page down.
bool WnAppInfoList::ScrollPageDown() {
  i32 first = first_visible, count = GetLineCount();
  if (count > 0) {
    i32 visible = visible_rows;
    if (first_visible < (i32)((u32)count - visible))
      SetIndexOfTopLine((i32)((u32)first + visible - 1));
  }
  return true;
}

// Expose the requested row through the live origin setter without introducing an item-range clamp.
// Refresh the scrollbar and invalidate after any nested origin change; the original method returns no value.
void WnAppInfoList::PutIndexInView(i32 index) {
  if (index < first_visible)
    SetIndexOfTopLine(index);
  else if (index > (i32)((u32)visible_rows + first_visible - 1))
    SetIndexOfTopLine((i32)((u32)index - visible_rows + 1));
  SetScrollbarValue();
  InvalidateSelfAndParents();
}

// Publish the first row, clamp through repeated live counts, then refresh the scrollbar and invalidate.
// The original viewport setter returns no value.
void WnAppInfoList::SetIndexOfTopLine(i32 index) {
  first_visible = index;
  i32 count = GetLineCount();
  if (count > visible_rows) {
    count = GetLineCount();
    if (first_visible > (i32)((u32)count - visible_rows)) {
      count = GetLineCount();
      first_visible = (i32)((u32)count - visible_rows);
    }
  }
  if (first_visible < 0)
    first_visible = 0;
  SetScrollbarValue();
  InvalidateSelfAndParents();
}

// Clamp the requested selection except the explicit -1 sentinel, expose changed rows and optionally notify afterward.
bool WnAppInfoList::SetSelection(i32 index, bool notify) {
  i32 count = GetLineCount(), selected = index;
  if (index != -1) {
    if (index < 0)
      selected = 0;
    if (selected >= count)
      selected = (i32)((u32)count - 1);
  }
  if (selected != selection) {
    selection = selected;
    if (selected >= 0 && !IsIndexInView(selected))
      PutIndexInView(selected);
    if (notify)
      SendSelectionChangedMessage();
    InvalidateSelfAndParents();
  }
  return true;
}

// Convert the vertical point through the live row height and origin, retaining the original Boolean edge clamp.
u32 WnAppInfoList::GetItemIndexFromCursorPosition(i32, i32 y, bool clamp) const {
  i32 count = GetLineCount();
  if (!count)
    return kWnInvalidIndex;
  if (y < 0 && clamp)
    y = 0;
  if (y >= GetH() && clamp)
    y = (i32)((u32)GetH() - 1);
  i32 relative = (i32)((u32)y - (i32)gutters[3]);
  assert(row_height && (relative != INT32_MIN || row_height != -1));
  i32 index = (i32)((u32)first_visible + relative / row_height);
  if (index >= count)
    return clamp ? (u32)count - 1 : UINT32_MAX;
  return (u32)index;
}

bool WnAppInfoList::GZCharacter(u32) {
  return owner->GetCapture() != static_cast<WnWindow *>(this);
}

// Capture the live window ID before delivering the original synchronous selection notification and ignore its reply.
bool WnAppInfoList::SendSelectionChangedMessage() {
  u32 window_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListSelectionChanged, window_id, 0);
  return true;
}

// Capture the live window ID before the distinct activation notification and return true independently of its reply.
bool WnAppInfoList::SendDoubleClickMessage() {
  u32 window_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClicked, window_id, 0);
  return true;
}

// Ignore keys while captured; Enter activates, paging/arrows dispatch live scrolling and other keys reach the target.
bool WnAppInfoList::OnKeyDown(u32 key, u32 modifiers) {
  if (owner->GetCapture() == static_cast<WnWindow *>(this))
    return false;
  i32 previous = selection;
  if (key == kWnKeyReturn) {
    SetSelection(previous, 1);
    return SendDoubleClickMessage();
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

bool WnAppInfoList::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

bool WnAppInfoList::OnMouseDownR(i32 x, i32 y, u32 modifiers) {
  return OnMouseDownL(x, y, modifiers);
}

bool WnAppInfoList::OnMouseUpR(i32 x, i32 y, u32 modifiers) {
  return OnMouseDownR(x, y, modifiers);
}

// Find a live source row by signed index; the original searches return no row for negative or past-end indices.
WnAppInfoList::WnTextRow *WnAppInfoList::FindRow(i32 index) {
  i32 current = 0;
  for (WnTextRow *row = rows->next; row != rows; row = row->next, ++current)
    if (current == index)
      return row;
  return nullptr;
}

// Destroy all actual rows while preserving the circular sentinel and its identity.
void WnAppInfoList::ClearRows(WnTextRow *head) {
  WnTextRow *row = head->next;
  while (row != head) {
    WnTextRow *next = row->next;
    delete row;
    row = next;
  }
  head->next = head->previous = head;
}

// Recalculate the original wrapped-row metrics through the protected void method.
// Rebuild both columns and align their trailing lines, preserving captured spacing and the direct final row count.
void WnAppInfoList::CalculateAllMetrics() {
  ScString first_value, second_value;
  u32 first_color = 0, second_color = 0;
  if (!fonts[0]) {
    fonts[0] = ApGetUiResources()->GetFont(kWnFontDefault);
    fonts[0]->AddRef();
  }
  if (!fonts[1]) {
    fonts[1] = ApGetUiResources()->GetFont(kWnFontDefault);
    fonts[1]->AddRef();
  }
  row_height = fonts[1]->Height(0);
  i32 height = (i32)((u32)draw_area.bottom - draw_area.top);
  assert(row_height && (height != INT32_MIN || row_height != -1));
  visible_rows = height / row_height;
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
    i32 spacing = gap, width = GetW();
    first_space = (i32)((u32)width - 2u * (u32)(i32)gutters[2] - scrollbar_width) / 2;
    first_space = (i32)((u32)first_space - (u32)spacing);
    second_space = (i32)((u32)first_space + (u32)spacing);
  } else {
    first_space = (i32)((u32)first_width - (i32)gap - (i32)gutters[2]);
    i32 width = GetW();
    second_space = (i32)((u32)width - (i32)gutters[2] - first_width - scrollbar_width);
  }
  if (first_space < 0)
    first_space = 0;
  if (second_space < 0)
    second_space = 0;
  for (WnTextRow *source = rows->next; source != rows; source = source->next) {
    i32 first_lines = 0;
    ScString first_remaining(source->pair.first);
    u32 fitted = fonts[0]->WrapFit(first_remaining.begin, first_remaining.ScString::Strlen(), (u32)first_space, 1);
    WnTextRow *line_cursor = wrapped_rows->previous;
    while (fitted < first_remaining.ScString::Strlen()) {
      first_value.FromChar(first_remaining.begin, fitted);
      first_remaining.Erase(0, fitted);
      first_color = source->pair.colors[0];
      AppendWrappedRow(first_value, second_value, first_color, second_color);
      fitted = fonts[0]->WrapFit(first_remaining.begin, first_remaining.ScString::Strlen(), (u32)first_space, 1);
      ++first_lines;
    }
    first_value = first_remaining;
    first_color = source->pair.colors[0];
    AppendWrappedRow(first_value, second_value, first_color, second_color);
    ++first_lines;
    ScString second_remaining(source->pair.second);
    i32 second_lines =
        (i32)fonts[1]->WrapLines(second_remaining.begin, second_remaining.ScString::Strlen(), (u32)second_space);
    fitted = fonts[1]->WrapFit(second_remaining.begin, second_remaining.ScString::Strlen(), (u32)second_space, 1);
    if (first_lines > second_lines) {
      i32 blanks = (i32)((u32)first_lines - second_lines);
      first_lines = second_lines;
      while (blanks--) {
        line_cursor = line_cursor->next;
        line_cursor->pair.second.FromChar("", 0);
        line_cursor->pair.colors[1] = source->pair.colors[1];
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
        first_color = source->pair.colors[0];
        second_color = source->pair.colors[1];
        AppendWrappedRow(first_value, second_value, first_color, second_color);
      } else {
        line_cursor = line_cursor->next;
        line_cursor->pair.second = segment;
        line_cursor->pair.colors[1] = source->pair.colors[1];
        --first_lines;
      }
      fitted = fonts[1]->WrapFit(second_remaining.begin, second_remaining.ScString::Strlen(), (u32)second_space, 1);
    }
    if (second_remaining.ScString::Strlen()) {
      first_value.FromChar("", 0);
      second_value = second_remaining;
      first_color = source->pair.colors[0];
      second_color = source->pair.colors[1];
      AppendWrappedRow(first_value, second_value, first_color, second_color);
    }
  }
  if (scrollbar_present || WnAppInfoList::GetLineCount() <= visible_rows) {
    SetScrollbarSizes();
    SetScrollbarValue();
  } else {
    AddScrollbar(1, 0);
    CalculateAllMetrics();
  }
}

// Draw the live wrapped rows and selection, preserving independent buffer locks and the source second-column formula.
// Scrollbar acquisition precedes background fill; optional outlines follow every visible row.
bool WnAppInfoList::GZPaint() {
  WnTextRow *row = wrapped_rows->next;
  ScRect bounds = draw_area;
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  if (scrollbar) {
    i32 width = scrollbar->GetWindow()->GetW();
    bounds.right = (i32)((u32)bounds.right - gutters[0] - (u32)width);
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
    u32 first_color = row->pair.colors[0], second_color = row->pair.colors[1];
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
      if (background_opacity < 0.0f || background_opacity > 0.0f) {
        ScRect highlight = line;
        if (background_opacity == 1.0f) {
          if (draw_buffer->Lock(GxLockFlags::Fill)) {
            draw_buffer->Fill(background_color, highlight, nullptr);
            draw_buffer->Unlock(GxLockFlags::Fill);
          }
        } else if (draw_buffer->Lock(GxLockFlags::Bits)) {
          draw_buffer->AlphaFill(background_color, highlight, background_opacity, nullptr);
          draw_buffer->Unlock(GxLockFlags::Bits);
        }
      }
    }
    fonts[0]->DrawTextA(
        draw_buffer, (u32)line.left, (u32)top, row->pair.first.begin, row->pair.first.ScString::Strlen(), first_color);
    i32 second_x = split;
    if (alternate_alignment) {
      u32 width = fonts[1]->Measure(row->pair.second.begin, row->pair.second.ScString::Strlen());
      second_x = (i32)((u32)split * 2 - line.right + width);
    }
    fonts[1]->DrawTextA(
        draw_buffer, (u32)second_x, (u32)top, row->pair.second.begin, row->pair.second.ScString::Strlen(), second_color);
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

// Resolve and select the clicked row before updating the shared elapsed timer and per-window double-click state.
bool WnAppInfoList::OnMouseDownL(i32 x, i32 y, u32) {
  static ScStopwatch timer(ScStopwatch::Milliseconds, 0);
  static u32 last_time = 0;
  i32 index = (i32)GetItemIndexFromCursorPosition(x, y, true);
  owner->SetCapture(this);
  if (index < GetLineCount() && index != GetSelection())
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

// Forward captured scrollbar releases in child coordinates; otherwise release capture and deliver pending activation.
bool WnAppInfoList::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
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
      SendDoubleClickMessage();
      last_click = -1;
      double_click = 0;
    }
  }
  return true;
}

// Append a complete native display row only after copying both actual strings and their separate pixels.
WnAppInfoList::WnTextRow *WnAppInfoList::AppendWrappedRow(
    const ScString &first, const ScString &second, u32 first_color, u32 second_color) {
  WnTextRow *row = new WnTextRow;
  row->pair.first = first;
  row->pair.second = second;
  row->pair.colors[0] = first_color;
  row->pair.colors[1] = second_color;
  row->pair.tag = 0;
  row->next = wrapped_rows;
  row->previous = wrapped_rows->previous;
  wrapped_rows->previous->next = row;
  wrapped_rows->previous = row;
  return row;
}
