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

void WnAppList::SetDrawSunkenRect(bool enabled) {
  line_outline = enabled;
  outline = 0;
  InvalidateSelfAndParents();
}

// Store the two outline pixels before invalidating the window and its parents.
void WnAppList::SetOutlineColors(u32 first, u32 second) {
  outline_first = first;
  outline_second = second;
  InvalidateSelfAndParents();
}

i32 WnAppList::GetLineCount() const {
  return (i32)strings.Count();
}

i32 WnAppList::GetSelection() const {
  return selection;
}

i32 WnAppList::GetLineHeight() const {
  return row_height;
}

// Store the unsigned byte minimum as the signed row-height floor before recalculating all metrics.
// Invalidate the window and its parents after the live metric update, then report success.
bool WnAppList::SetMinimumLineHeight(u8 height) {
  minimum_height = height;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

i32 WnAppList::GetVisibleLines() const {
  return visible_rows;
}

i32 WnAppList::GetIndexOfTopLine() const {
  return first_visible;
}

void WnAppList::SetDrop(bool enabled) {
  popup = enabled;
}

// Apply the Boolean plain-outline setting and disable the mutually exclusive line outline before invalidating.
// The original setter returns void after notifying the window and its parents.
void WnAppList::SetOutlineDrawing(bool enabled) {
  outline = enabled;
  line_outline = 0;
  InvalidateSelfAndParents();
}

// Store the outer horizontal and vertical gutters, then recompute rows before invalidating.
bool WnAppList::SetScrollbarGutters(i8 horizontal, i8 vertical) {
  gutter252 = horizontal;
  gutter253 = vertical;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

// Store the text gutters and immediately recalculate the visible row count before invalidating.
bool WnAppList::SetTextGutters(i8 horizontal, i8 vertical) {
  gutter254 = horizontal;
  gutter255 = vertical;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

bool WnAppList::ClearFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

// Return the borrowed primary window identity of this list control.
WnWindow *WnAppList::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Construct the executable list state and select the source outline after retrieving its default fill color.
WnAppList::WnAppList(bool incoming)
    : background_opacity(0.6f), background_color(kWnDefaultColor), language(0), font(nullptr), selection(-1),
      visible_rows(0), first_visible(0), row_height(10), minimum_height(0), byte220(0), outline(0), line_outline(0),
      popup(incoming), byte224(1), byte225(0), popup_image(nullptr), row_image(nullptr), double_click(0),
      last_click(-1), outline_first(kWnDefaultColor), outline_second(kWnDefaultColor), gutter252(3), gutter253(0),
      gutter254(3), gutter255(1), byte256(1), byte257(0) {
  SetFillColor(ApGetUiResources()->GetColor(kWnColorWhite));
  font_colors[0] = kWnDefaultColor;
  font_colors[1] = kWnDefaultColor;
  font_colors[2] = kWnDefaultColor;
  if (popup)
    WnAppList::SetOutlineDrawing(true);
  else
    WnAppList::SetDrawSunkenRect(true);
}

WnAppList::~WnAppList() {
  WnAppList::Shutdown();
}

// Publish the actual list secondary interface before retaining the live root. Other identities delegate to root query.
bool WnAppList::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnListControl)
    return UiWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnAppListControl *>(this);
  AddRef();
  return true;
}

u32 WnAppList::AddRef() {
  return UiWindow::AddRef();
}

u32 WnAppList::Release() {
  return UiWindow::Release();
}

// Hide the live window, release both images in order, then its font. Clear each field after release before closing the root.
bool WnAppList::Shutdown() {
  HideWindow();
  ScRelease(&popup_image);
  ScRelease(&row_image);
  ScRelease(&font);
  return UiWindow::Shutdown();
}

// Store a valid indexed font color. Preserve the source's equality-only invalidation rather than reversing its condition.
bool WnAppList::SetFontColor(i32 index, u32 color) {
  if ((u32)index > 2)
    return false;
  u32 old = font_colors[index];
  font_colors[index] = color;
  if (old == color)
    InvalidateSelfAndParents();
  return true;
}

void WnAppList::SetHighlightColor(u32 color) {
  background_color = color;
  InvalidateSelfAndParents();
}

void WnAppList::SetHighlightFillAlpha(f32 opacity) {
  if (opacity > 1)
    opacity = 1;
  if (!(opacity >= 0))
    opacity = 0;
  background_opacity = opacity;
  InvalidateSelfAndParents();
}

// Query the first scrollbar child and immediately release the query hold. The returned live interface is borrowed.
WnScrollbarControl *WnAppList::GetScrollbarV() {
  void *slot = nullptr;
  if (!GetChildAs(kWnListVerticalScrollbarId, IID_WnScrollbarControl, &slot))
    return nullptr;
  static_cast<WnScrollbarControl *>(slot)->Release();
  return static_cast<WnScrollbarControl *>(slot);
}

// Query the second scrollbar child with the same borrowed-result ordering. Failed child lookup returns null.
WnScrollbarControl *WnAppList::GetScrollbarH() {
  void *slot = nullptr;
  if (!GetChildAs(kWnListHorizontalScrollbarId, IID_WnScrollbarControl, &slot))
    return nullptr;
  static_cast<WnScrollbarControl *>(slot)->Release();
  return static_cast<WnScrollbarControl *>(slot);
}

// Publish a borrowed const concrete string only for a valid signed index.
// Failed lookup leaves the genuine output untouched.
bool WnAppList::GetStringAddressFromIndex(i32 index, const ScString **text) const {
  WnListNode *node = strings.At((u32)index);
  if (!node)
    return false;
  *text = &node->text;
  return true;
}

// Copy a located concrete string without invoking interface setters on the output. Preserve concrete self-assignment behavior.
bool WnAppList::GetStringFromIndex(i32 index, ScString *text) const {
  const ScString *stored;
  if (!GetStringAddressFromIndex(index, &stored))
    return false;
  if (stored != text)
    text->ScString::FromChar(stored->begin, stored->begin ? (u32)(stored->end - stored->begin) : 0);
  return true;
}

// Compare concrete string lengths and bytes directly, without calling the input's virtual equality method.
// Publish the first match through the genuine output; the original concrete overload is nonconst.
bool WnAppList::GetIndexFromString(i32 *index, const ScString &text) {
  u32 length = text.begin ? (u32)(text.end - text.begin) : 0, position = 0;
  for (WnListLink *link = strings.head->next; link != strings.head; link = link->next, ++position) {
    const ScString *stored = &static_cast<WnListNode *>(link)->text;
    if ((stored->begin ? (u32)(stored->end - stored->begin) : 0) == length &&
        (!length || !memcmp(stored->begin, text.begin, length))) {
      *index = (i32)position;
      return true;
    }
  }
  return false;
}

// Retrieve into a temporary concrete string before copying through the genuine public output interface. Ignore its Copy result.
bool WnAppList::GetStringFromIndex(i32 index, ScStringView *text) const {
  ScString value;
  bool found = GetStringFromIndex(index, &value);
  if (found)
    text->Copy(value);
  return found;
}

// Ask the incoming interface to compare each owned item. Follow stable items across callbacks that move neighboring entries.
bool WnAppList::GetIndexFromString(i32 *index, const ScStringView &text) const {
  i32 visited = 0;
  for (WnListLink *link = strings.head->next; link != strings.head; link = link->next, ++visited) {
    if (text.IsEqual(&static_cast<WnListNode *>(link)->text)) {
      *index = visited;
      return true;
    }
  }
  return false;
}

bool WnAppList::IsIndexInView(i32 index) const {
  i32 end = (i32)((u32)first_visible + (u32)visible_rows);
  return index >= first_visible && index < end;
}

// Select the preceding row without notification only when the live count is nonzero and the stored selection is positive.
// Ignore the selection setter's result and report whether that operation was attempted.
bool WnAppList::MoveLineUp() {
  if (!GetLineCount() || selection <= 0)
    return false;
  SetSelection(selection - 1, 0);
  return true;
}

// Select the following row only below the last row of a positive live count. Preserve negative starting selections.
bool WnAppList::MoveLineDown() {
  i32 count = GetLineCount();
  if (count <= 0 || selection >= count - 1)
    return false;
  SetSelection((i32)((u32)selection + 1), 0);
  return true;
}

// Forward a character's low byte to the live first-letter selector only while this list does not hold capture.
// The original DWORD character virtual always reports false, independently of the selector's Boolean result.
bool WnAppList::GZCharacter(u32 character) {
  if (owner->GetCapture() != static_cast<WnWindow *>(this))
    ScrollToFirstLetter((char)character);
  return false;
}

// Consume key releases in popup mode. Ordinary lists forward the complete key and modifier words to their live target.
bool WnAppList::OnKeyUp(u32 key, u32 modifiers) {
  if (popup)
    return true;
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Read the live control ID before dispatching command six. Delivery failure does not change the source's success result.
bool WnAppList::SendSelectionChangedMessage() {
  u32 control_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListSelectionChanged, control_id, 0);
  return true;
}

// Send the current selection before command seven with a freshly read ID. The first callback can change the second target.
bool WnAppList::SendDoubleClickMessage() {
  i32 selected = GetSelection();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClickSelection, (u32)selected, 0);
  u32 control_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClicked, control_id, 0);
  return true;
}

// Post the original unsigned modal result only for popup lists.
// Capture the manager before obtaining the borrowed window, retaining the full cancellation word.
bool WnAppList::EndDialog(u32 result) {
  if (!popup)
    return false;
  WnWindowManager *manager = GetWindowManager();
  WnWindow *window = AsIGZWin();
  manager->GZWinMsgPost(kWnMsg_EndModal, (uptr)window, (uptr)result);
  return true;
}

// Retain the incoming font before releasing the live old field, publish it and dispatch the original virtual row update.
// The genuine public setter returns void after the row metrics have been updated.
void WnAppList::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  CalculateAllMetrics();
}

// Recompute row height from the saved minimum, row image and font before signed visible-row division through live GetH.
// Preserve repeated metric calls; the original protected method returns no value.
void WnAppList::CalculateAllMetrics() {
  row_height = minimum_height;
  if (row_image) {
    i32 previous = row_height;
    if (previous <= (i32)row_image->Height())
      row_height = row_image->Height();
    else
      row_height = previous;
  }
  if (font) {
    i32 previous = row_height;
    if (previous <= font->Height(0))
      row_height = font->Height(0);
    else
      row_height = previous;
  }
  if (row_height) {
    i32 height = GetH();
    i32 inner = (i32)((u32)height - 2u * (u32)(i32)gutter255);
    assert(inner != INT32_MIN || row_height != -1);
    visible_rows = inner / row_height;
  } else
    visible_rows = 0;
}

// Store both original Boolean scrollbar-use inputs and report success.
// The setter creates or removes no scrollbar.
bool WnAppList::EnableScrollBar(bool first, bool second) {
  byte256 = first;
  byte257 = second;
  return true;
}

// Publish a valid indexed font color through its genuine output. Invalid indices leave the caller's word untouched.
bool WnAppList::GetFontColor(i32 index, u32 *color) {
  if ((u32)index > 2)
    return false;
  *color = font_colors[index];
  return true;
}

// Retain the incoming popup frame before releasing the old image and publishing the replacement.
// The original public virtual returns void without recalculating rows or invalidating.
void WnAppList::SetPopupBackgroundBuffer(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (popup_image)
    popup_image->Release();
  popup_image = incoming;
}

// Retain the new row image before releasing the live old field and publishing the replacement.
// This void setter performs no row recalculation or invalidation.
void WnAppList::SetLineItemBackgroundBuffer(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (row_image)
    row_image->Release();
  row_image = incoming;
}

// Normalize the actual area to local coordinates before shrinking by signed text gutters and the live scrollbar width.
// The original const method publishes its rectangle through the caller's mutable output pointer.
void WnAppList::GetTextArea(ScRect *rectangle) const {
  GetArea(rectangle);
  i32 left = rectangle->left, top = rectangle->top;
  rectangle->top = 0;
  rectangle->bottom = (i32)((u32)rectangle->bottom - (u32)top);
  rectangle->left = 0;
  rectangle->right = (i32)((u32)rectangle->right - (u32)left);
  i32 right = rectangle->right, bottom = rectangle->bottom;
  rectangle->left = (i32)((u32)rectangle->left + (u32)(i32)gutter254);
  rectangle->right = (i32)((u32)right - (u32)(i32)gutter254);
  rectangle->top = (i32)((u32)rectangle->top + (u32)(i32)gutter255);
  rectangle->bottom = (i32)((u32)bottom - (u32)(i32)gutter255);
  if (WnScrollbarControl *scrollbar = const_cast<WnAppList *>(this)->GetScrollbarV()) {
    i32 width = scrollbar->GetWindow()->GetW();
    rectangle->right = (i32)((u32)rectangle->right - (u32)width - (u32)(i32)gutter252);
  }
}

// Find the concrete string's first equal item, then remove it through the live indexed control operation.
bool WnAppList::RemoveString(const ScString &text) {
  i32 index;
  if (!GetIndexFromString(&index, text))
    return false;
  return RemoveString(index);
}

// Normalize the insertion position and retain selection identity as preceding items shift.
// Popup sizing runs before the live scrollbar creation or synchronization decision.
bool WnAppList::InsertString(const ScString &text, i32 index) {
  i32 count = GetLineCount(), position = index;
  if (index == -2 || index > count)
    position = count;
  else if (index < 0)
    position = 0;
  InsertStringAtIndex(position, text);
  if (count != 0 || selection == -1) {
    i32 selected = selection;
    if (position <= selected && selected != -1)
      SetSelection((i32)((u32)selected + 1), 1);
  } else
    SetSelection(0, 1);
  if (popup) {
    if (byte224)
      ResizeBoxToFitContents();
    if (byte225)
      ResizeBoxToFitContentsHorizontal();
  }
  if (GetScrollbarV() || GetLineCount() <= visible_rows) {
    SetScrollBarSizes();
    SetScrollBarValue();
  } else
    AddScrollBar(1, 0);
  InvalidateSelfAndParents();
  return true;
}

// Erase first for zero, last for minus two or an index past the count, and a matching middle item otherwise.
// Empty lists return false; other invalid negative indices are successful no-ops, and selection remains unchanged.
bool WnAppList::RemoveStringAtIndex(i32 index) {
  i32 count = GetLineCount();
  if (!count)
    return false;
  WnListLink *node;
  if (index == 0)
    node = strings.head->next;
  else if (index == -2 || index >= count)
    node = strings.head->previous;
  else {
    node = strings.At((u32)index);
    if (!node)
      return true;
  }
  strings.Erase(node);
  return true;
}

// Copy concrete bytes before inserting at zero, the tail sentinel or a matching middle index. Invalid negative indices do nothing.
// This raw helper always returns true, leaving selection and layout to its original caller.
bool WnAppList::InsertStringAtIndex(i32 index, const ScString &text) {
  bool append = index != 0 && (index == -2 || index >= GetLineCount());
  WnListLink *position = strings.head;
  if (index == 0)
    position = strings.head->next;
  else if (!append) {
    position = strings.At((u32)index);
    if (!position)
      return true;
  }
  strings.InsertBefore(position, text);
  return true;
}

// Destroy all owned strings before clearing selection and origin, then recompute rows and remove both scrollbar children.
// Finish by invalidating the list and its parents through the original live window callback.
void WnAppList::RemoveAllStrings() {
  strings.Clear();
  selection = -1;
  first_visible = 0;
  CalculateAllMetrics();
  RemoveScrollBar(1, 1);
  InvalidateSelfAndParents();
}

// Clamp a requested selection using the source's independent count queries, ensuring visibility only after a changed row.
// Notify for every nonzero request outside popup mode and always invalidate, including unchanged selections.
bool WnAppList::SetSelection(i32 index, bool notify) {
  i32 chosen = index;
  if (index != -1) {
    bool in_range = index >= 0 && index < GetLineCount();
    if (!in_range) {
      if (index < 0)
        chosen = 0;
      if (chosen >= GetLineCount())
        chosen = (i32)((u32)GetLineCount() - 1);
    }
  }
  if (chosen != selection) {
    selection = chosen;
    if (chosen >= 0 && !IsIndexInView(chosen))
      PutIndexInView(chosen);
  }
  if (notify && !popup)
    SendSelectionChangedMessage();
  InvalidateSelfAndParents();
  return true;
}

// Move a positive-count list backward by its stored page-minus-one, then select the captured requested first row.
// Both virtual results are ignored and the source returns success even when there are no items.
bool WnAppList::ScrollPageUp() {
  if (GetLineCount() > 0) {
    i32 first = (i32)((u32)first_visible - (u32)visible_rows + 1);
    if (first < 0)
      first = 0;
    SetIndexOfTopLine(first);
    SetSelection(first, 0);
  }
  return true;
}

// Capture the old first row before querying count, then advance only below the live final-page threshold.
// Selection uses the captured requested first row plus the freshly read visible-row count after viewport callbacks.
bool WnAppList::ScrollPageDown() {
  i32 first = first_visible, count = GetLineCount();
  if (count > 0) {
    i32 rows = visible_rows;
    if (first_visible < (i32)((u32)count - (u32)rows)) {
      i32 next = (i32)((u32)first + (u32)rows - 1);
      SetIndexOfTopLine(next);
      SetSelection((i32)((u32)next + (u32)visible_rows), 0);
    }
  }
  return true;
}

// Select the first owned string whose first stored byte matches, without case folding or a virtual string query.
// Empty strings never match, while a stored leading zero remains a valid one-byte prefix.
bool WnAppList::ScrollToFirstLetter(char character) {
  u32 index = 0;
  for (WnListLink *link = strings.head->next; link != strings.head; link = link->next, ++index) {
    const ScString *text = &static_cast<WnListNode *>(link)->text;
    if (text->begin != text->end && *text->begin == character) {
      SetSelection((i32)index, 1);
      return true;
    }
  }
  return false;
}

// Bring the requested index into the stored viewport through the live first-row operation, without item clamping.
// This void method synchronizes and invalidates again after a nested first-row setter.
void WnAppList::PutIndexInView(i32 index) {
  i32 first = first_visible;
  if (index < first)
    SetIndexOfTopLine(index);
  else {
    i32 rows = visible_rows;
    if (index > (i32)((u32)rows + (u32)first - 1))
      SetIndexOfTopLine((i32)((u32)index - (u32)rows + 1));
  }
  SetScrollBarValue();
  InvalidateSelfAndParents();
}

// Store and clamp the first row through the original independent live count queries, then synchronize and invalidate.
// The public viewport setter returns no value after the parent-chain invalidation.
void WnAppList::SetIndexOfTopLine(i32 index) {
  first_visible = index;
  i32 count = GetLineCount();
  if (count > visible_rows) {
    count = GetLineCount();
    i32 last = (i32)((u32)count - (u32)visible_rows);
    if (first_visible > last) {
      count = GetLineCount();
      first_visible = (i32)((u32)count - (u32)visible_rows);
    }
  }
  if (first_visible < 0)
    first_visible = 0;
  SetScrollBarValue();
  InvalidateSelfAndParents();
}

// Derive a row from y after the source's optional viewport clamp; x is intentionally unused.
// Nonempty lists always query height, and only indices above the item range receive the source's final clamp.
u32 WnAppList::GetItemIndexFromCursorPosition(i32, i32 y, bool clamp) const {
  i32 count = GetLineCount();
  if (!count)
    return kWnInvalidIndex;
  i32 target = y;
  if (y < 0 && clamp)
    target = 0;
  if (target >= GetH() && clamp)
    target = (i32)((u32)GetH() - 1);
  i32 delta = (i32)((u32)target - (u32)(i32)gutter255);
  assert(row_height && (delta != INT32_MIN || row_height != -1));
  i32 index = (i32)((u32)first_visible + (u32)(delta / row_height));
  if (index >= count)
    return clamp ? (u32)count - 1 : UINT32_MAX;
  return (u32)index;
}

// Fit the current height to complete signed rows when row height is nonzero, preserving the source's captured quotient.
// Scrollbar sizing/synchronization and invalidation still run when the existing height already fits exactly.
bool WnAppList::ResizeWindowForExactLineHeights() {
  if (!row_height)
    return false;
  i32 height = GetH(), row = row_height, gutters = 2 * (i32)gutter255;
  i32 inner = (i32)((u32)height - (u32)gutters);
  assert(inner != INT32_MIN || row != -1);
  i32 rows = inner / row;
  i32 exact = (i32)((u32)gutters + (u32)rows * (u32)row);
  if (exact != height) {
    SetH(exact);
    visible_rows = rows;
  }
  SetScrollBarSizes();
  SetScrollBarValue();
  InvalidateSelfAndParents();
  return true;
}

// Expand a popup toward its item height, limited by the live parent bottom, creating/removing its vertical scrollbar.
// The count, desired height and parent limit are captured at the original points across nested size callbacks.
bool WnAppList::ResizeBoxToFitContents() {
  if (!popup)
    return false;
  i32 count = GetLineCount();
  byte224 = 1;
  if (count < visible_rows || count <= 0)
    return false;
  i32 desired = (i32)((u32)row_height * (u32)count + 2u * (u32)(i32)gutter255);
  i32 parent_height = INT32_MAX;
  if (GetParentWin())
    parent_height = GetParentWin()->GetH();
  i32 top = GetT(), available = (i32)((u32)parent_height - (u32)top - 2);
  i32 width = GetW();
  if (desired > available) {
    SetSize(width, available);
    i32 inner = (i32)((u32)available - 2u * (u32)(i32)gutter255);
    assert(row_height && (inner != INT32_MIN || row_height != -1));
    visible_rows = inner / row_height;
    AddScrollBar(1, 0);
    ResizeWindowForExactLineHeights();
  } else {
    SetSize(width, desired);
    visible_rows = count;
    SetIndexOfTopLine(0);
    RemoveScrollBar(1, 0);
  }
  SetScrollBarSizes();
  SetScrollBarValue();
  InvalidateSelfAndParents();
  return true;
}

// Measure the live popup strings through genuine visible-width outputs and set the maximum width plus signed gutters.
// Missing font acquisition precedes a fresh traversal; the source's one-pixel minimum also applies to empty lists.
bool WnAppList::ResizeBoxToFitContentsHorizontal() {
  if (!popup)
    return false;
  byte225 = 1;
  u32 width = 1;
  if (strings.head->next != strings.head) {
    if (!font)
      SetFont(ApGetUiResources()->GetFont(kWnFontDefault));
    assert(font);
    WnListLink *current = strings.head->next;
    while (current != strings.head) {
      const ScString *text = &static_cast<WnListNode *>(current)->text;
      u32 length = text->ScString::Strlen();
      u32 measured = font->GetStringVisibleWidth(text->begin, length, &length);
      if (width < measured)
        width = measured;
      current = current->next;
    }
  }
  SetW((i32)(width + 2u * (u32)(i32)gutter254));
  return true;
}

// Resolve missing font/colors, prepare popup sizing/selection/capture and frame, then initialize real inherited buffers.
// Preserve captured resource ownership, separate language lookup, and final size/sync/row-update order.
bool WnAppList::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnResources *resources = ApGetUiResources();
  if (resources) {
    if (!font)
      SetFont(resources->GetFont(kWnFontDefault));
    if (outline_first == kWnDefaultColor)
      outline_first = resources->GetColor(19);
    if (outline_second == kWnDefaultColor)
      outline_second = resources->GetColor(20);
    if (font_colors[1] == kWnDefaultColor)
      SetFontColor(1, resources->GetColor(24));
    if (font_colors[2] == kWnDefaultColor)
      SetFontColor(2, resources->GetColor(25));
    if (background_color == kWnDefaultColor)
      SetHighlightColor(resources->GetColor(22));
  }
  if (popup) {
    CalculateAllMetrics();
    if (byte224)
      ResizeBoxToFitContents();
    if (byte225)
      ResizeBoxToFitContentsHorizontal();
    SetIndexOfTopLine(0);
    PutIndexInView(selection);
    owner->SetCapture(this);
    if (!popup_image && resources)
      SetPopupBackgroundBuffer(resources->GetImage(kWnThemePopupBackgroundImageId));
  }
  ScLanguageManager *manager = ApGetLanguageManager();
  language = manager ? manager->GetCurrentLanguage() : 1;
  SetScrollBarSizes();
  SetScrollBarValue();
  CalculateAllMetrics();
  return UiWindow::Init();
}

// Compare requested size before root geometry, always recalculate rows, and size/synchronize scrollbars only on size change.
// Preserve short-circuit width/height queries and unconditional final parent-chain invalidation.
void WnAppList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  bool changed = width != GetW() || height != GetH();
  UiWindow::SetArea(left, top, right, bottom);
  CalculateAllMetrics();
  if (changed) {
    SetScrollBarSizes();
    SetScrollBarValue();
  }
  InvalidateSelfAndParents();
}

// Forward an exact enabled-bit request to both existing scrollbar windows before changing the actual root flags.
u32 WnAppList::SetFlag(u32 mask, bool enabled) {
  if (mask == kWnFlagEnabled) {
    if (WnScrollbarControl *control = GetScrollbarV())
      control->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
    if (WnScrollbarControl *control = GetScrollbarH())
      control->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
  }
  return UiWindow::SetFlag(mask, enabled);
}

// Erase an index no greater than the captured last item, then adjust the live viewport and selected index.
// Preserve negative-index behavior and ignore the raw erase reply before popup and scrollbar updates.
bool WnAppList::RemoveString(i32 index) {
  i32 last = (i32)((u32)GetLineCount() - 1);
  if (index > last)
    return false;
  RemoveStringAtIndex(index);
  i32 first = first_visible;
  if (first > index)
    first = (i32)((u32)first - 1);
  SetIndexOfTopLine(first);
  i32 selected = selection;
  if ((index == selected && index == last) || index < selected)
    SetSelection((i32)((u32)selected - 1), 1);
  if (popup) {
    if (byte224)
      ResizeBoxToFitContents();
    if (byte225)
      ResizeBoxToFitContentsHorizontal();
  }
  if (GetScrollbarV() && GetLineCount() <= visible_rows) {
    RemoveScrollBar(1, 0);
    SetIndexOfTopLine(0);
  } else {
    SetScrollBarSizes();
    SetScrollBarValue();
  }
  InvalidateSelfAndParents();
  return true;
}

bool WnAppList::RemoveString(const ScStringView &text) {
  ScString value(text);
  return RemoveString(value);
}

bool WnAppList::InsertString(const ScStringView &text, i32 index) {
  ScString value(text);
  return InsertString(value, index);
}

// Replace the clamped existing item's text through its actual string interface, then resize an automatic popup.
// An empty list or failed concrete lookup returns false without invalidation.
bool WnAppList::ModifyString(const ScStringView &text, i32 index) {
  i32 count = GetLineCount(), position = index;
  if (!count)
    return false;
  if (index == -2 || index >= count)
    position = (i32)((u32)count - 1);
  else if (index < 0)
    position = 0;
  const ScString *stored;
  if (!GetStringAddressFromIndex(position, &stored))
    return false;
  const_cast<ScString *>(stored)->Copy(text);
  if (popup) {
    if (byte224)
      ResizeBoxToFitContents();
    if (byte225)
      ResizeBoxToFitContentsHorizontal();
  }
  InvalidateSelfAndParents();
  return true;
}

// Request the first viewport only when the list has items. Empty lists return false without moving the viewport.
bool WnAppList::ScrollToStart() {
  if (GetLineCount() <= 0)
    return false;
  SetIndexOfTopLine(0);
  return true;
}

// Move the final item into the last visible row, using zero when the entire list fits.
bool WnAppList::ScrollToEnd() {
  i32 count = GetLineCount();
  if (count > 0) {
    i32 rows = visible_rows;
    if (rows < count)
      SetIndexOfTopLine((i32)((u32)count - (u32)rows));
    else
      SetIndexOfTopLine(0);
  }
  return true;
}

// Enable sorting before dispatching the live sort operation on the first request.
// Warm calls leave the existing order untouched, and the original method returns no value.
void WnAppList::SetShouldSort() {
  if (!byte220) {
    byte220 = 1;
    Sort();
  }
}

// Dispatch navigation through the live list interface, notifying only when its stored selection changes.
// Enter reselects before activating or completing a popup; ordinary unhandled keys retain their complete message words.
bool WnAppList::OnKeyDown(u32 key, u32 modifiers) {
  i32 previous = selection;
  if (key == kWnKeyReturn) {
    SetSelection(selection, 1);
    if (popup) {
      EndDialog((u32)selection);
      return true;
    }
    return SendDoubleClickMessage();
  }
  if (key == kWnKeyEscape) {
    if (popup) {
      EndDialog(UINT32_MAX);
      return true;
    }
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
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
    return popup ? true : SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  }
  if (selection != previous)
    SetSelection(selection, 1);
  return true;
}

// Resolve the clicked row before checking capture, forwarding captured scrollbar presses or completing a popup.
// Fresh presses capture the list and select through its live interface before updating the shared double-click timer.
bool WnAppList::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  static ScStopwatch timer(ScStopwatch::Milliseconds, 0);
  static u32 last_time = 0;
  i32 index = (i32)GetItemIndexFromCursorPosition(x, y, 1);
  if (owner->GetCapture() == static_cast<WnWindow *>(this)) {
    if (IsPointInWindow(x, y)) {
      if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
        WnWindow *window = scrollbar->GetWindow();
        i32 child_x = x, child_y = y;
        WindowToWindowCoordinates(window, &child_x, &child_y);
        if (child_x >= 0 && child_y >= 0 && child_x < window->GetW() && child_y < window->GetH()) {
          ScMessage message{kWnMsg_MouseDownL, (u32)child_x, (u32)child_y, modifiers};
          bool handled = window->DoMessage(&message);
          owner->SetCapture(this);
          return handled;
        }
      }
      EndDialog((u32)index);
    } else
      EndDialog(UINT32_MAX);
  } else {
    owner->SetCapture(this);
    if (index < GetLineCount() && index != GetSelection())
      SetSelection(index, 1);
    if (last_time) {
      if (index == last_click && timer.GetElapsedTime() - last_time < WnDoubleClickTime())
        double_click = 1;
      last_click = index;
      last_time = timer.GetElapsedTime();
    } else {
      timer.Restart();
      last_time = 1;
      last_click = index;
    }
  }
  return true;
}

// Forward a captured release inside the scrollbar after translating coordinates, then recapture the list.
// Other captured releases drop ordinary-list capture before dispatching pending activation and clearing click state.
bool WnAppList::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  if (owner->GetCapture() != static_cast<WnWindow *>(this))
    return true;
  if (IsPointInWindow(x, y))
    if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
      WnWindow *window = scrollbar->GetWindow();
      i32 child_x = x, child_y = y;
      WindowToWindowCoordinates(window, &child_x, &child_y);
      if (child_x >= 0 && child_y >= 0 && child_x < window->GetW() && child_y < window->GetH()) {
        ScMessage message{kWnMsg_MouseUpL, (u32)child_x, (u32)child_y, modifiers};
        bool handled = window->DoMessage(&message);
        owner->SetCapture(this);
        return handled;
      }
    }
  if (!popup)
    owner->ReleaseCapture(this);
  if (double_click) {
    SendDoubleClickMessage();
    last_click = -1;
    double_click = 0;
  }
  return true;
}

// Route captured popup movement into its scrollbar before considering list rows. A changed row below the item
// count reaches the live selection virtual without notification; negative rows retain the original signed behavior.
bool WnAppList::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  if (owner->GetCapture() != static_cast<WnWindow *>(this) || !popup)
    return true;
  if (!IsPointInWindow(x, y))
    return true;
  if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
    WnWindow *window = scrollbar->GetWindow();
    i32 child_x = x, child_y = y;
    WindowToWindowCoordinates(window, &child_x, &child_y);
    if (child_x >= 0 && child_y >= 0 && child_x < window->GetW() && child_y < window->GetH()) {
      ScMessage message{kWnMsg_MouseMove, (u32)child_x, (u32)child_y, modifiers};
      bool handled = window->DoMessage(&message);
      owner->SetCapture(this);
      return handled;
    }
  }
  i32 relative_y = (i32)((u32)y - (u32)(i32)gutter255);
  assert(row_height && (relative_y != INT32_MIN || row_height != -1));
  i32 index = (i32)((u32)first_visible + (u32)(relative_y / row_height));
  if (selection != index && index < GetLineCount())
    SetSelection(index, 0);
  return true;
}

// Publish the first scrollbar range, two real divisors and live first-visible value through its borrowed interface.
// The original void method skips synchronization when the bar is absent or no rows are visible.
void WnAppList::SetScrollBarValue() {
  WnScrollbarControl *scrollbar = GetScrollbarV();
  if (!scrollbar || !visible_rows)
    return;
  i32 range = (i32)((u32)GetLineCount() - (u32)visible_rows);
  if (range < 0)
    range = 0;
  scrollbar->SetRange(0, range);
  i32 count = GetLineCount();
  i32 pages = (i32)((u32)(count / visible_rows) + 1);
  scrollbar->SetDivisors(range, pages);
  scrollbar->SetValue(GetIndexOfTopLine());
}

// Position the borrowed first scrollbar using the original independent window and size queries.
// The void geometry update leaves an absent scrollbar untouched.
void WnAppList::SetScrollBarSizes() {
  WnScrollbarControl *scrollbar = GetScrollbarV();
  if (!scrollbar)
    return;
  WnWindow *window = scrollbar->GetWindow();
  i32 width = window->GetW(), right = GetW();
  i32 left = (i32)((u32)right - (u32)width), bottom = GetH();
  scrollbar->GetWindow()->SetArea(left, 0, right, bottom);
}

// Create the enabled missing vertical child, preserving independent borrowed-window queries and ignored attachment replies.
// Retain the original unused horizontal Boolean and return true on every defined path.
bool WnAppList::AddScrollBar(bool vertical, bool) {
  if (vertical && byte256 && !ChildExists(kWnListVerticalScrollbarId)) {
    WnScrollbarControl *scrollbar = ApGetUiResources()->CreateScrollbar(kWnListVerticalScrollbarId, 2, 0, 0, 0);
    scrollbar->GetWindow()->SetW(16);
    ChildAdd(scrollbar->GetWindow());
    ChildToFront(scrollbar->GetWindow());
    SetScrollBarSizes();
    SetScrollBarValue();
    scrollbar->Release();
  }
  return true;
}

// Delete each independently requested existing scrollbar child by its actual fixed ID, ignoring deletion replies.
// Both original inputs are Boolean, and the method returns no value.
void WnAppList::RemoveScrollBar(bool vertical, bool horizontal) {
  if (vertical) {
    WnWindow *window = GetChildWindowFromID(kWnListVerticalScrollbarId);
    if (window)
      ChildDelete(window);
  }
  if (horizontal) {
    WnWindow *window = GetChildWindowFromID(kWnListHorizontalScrollbarId);
    if (window)
      ChildDelete(window);
  }
}

// Draw the background, popup border, selection and visible text, then apply the requested outline.
bool WnAppList::GZPaint() {
  WnListLink *current = strings.head->next;
  i32 item_index = 0;
  ScRect rectangle = draw_area;
  if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
    i32 width = scrollbar->GetWindow()->GetW();
    rectangle.right = (i32)((u32)rectangle.right - (u32)(i32)gutter252 - (u32)width);
    first_visible = scrollbar->GetValue();
  }
  if (row_image) {
    if (draw_buffer->Lock(GxLockFlags::Blt)) {
      GxBuffer *target = draw_buffer;
      const ScRect *source = row_image->GetBufferArea();
      target->BltTiled(row_image, *(source), rectangle, 0, 0, nullptr);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  } else if (draw_buffer->Lock(GxLockFlags::Fill)) {
    GxBuffer *target = draw_buffer;
    u32 color = GetFillColor();
    target->Fill(color, &rectangle);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (popup && popup_image && draw_buffer->Lock(GxLockFlags::Blt)) {
    u32 height = popup_image->Height() / 3, width = popup_image->Width() / 3;
    ScRect slice{0, 0, (i32)width, (i32)height};
    draw_buffer->BltEdge(popup_image, slice, rectangle, static_cast<u8>(0));
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if (background_opacity != 0 && selection > -1 && IsIndexInView(selection)) {
    i32 offset = (i32)(((u32)selection - (u32)first_visible) * (u32)row_height);
    i32 top = (i32)((u32)offset + (u32)rectangle.top + (u32)(i32)gutter255);
    ScRect selected{
        (i32)((u32)rectangle.left + 1), top, (i32)((u32)rectangle.right - 1), (i32)((u32)top + (u32)row_height)};
    if (selected.bottom > rectangle.bottom)
      selected.bottom = rectangle.bottom;
    if (background_opacity == 1) {
      if (draw_buffer->Lock(GxLockFlags::Fill)) {
        draw_buffer->Fill(background_color, selected, nullptr);
        draw_buffer->Unlock(GxLockFlags::Fill);
      }
    } else if (draw_buffer->Lock(GxLockFlags::Bits)) {
      draw_buffer->AlphaFill(background_color, selected, background_opacity, nullptr);
      draw_buffer->Unlock(GxLockFlags::Bits);
    }
  }
  FtFont *initial_font = font;
  if (!initial_font)
    return true;
  i32 skip = first_visible;
  while (current != strings.head && item_index < skip) {
    ++item_index;
    current = current->next;
  }
  i32 left = (i32)((u32)rectangle.left + (u32)(i32)gutter254);
  i32 right = (i32)((u32)rectangle.right - (u32)(i32)gutter254), height = row_height;
  i32 offset = (i32)((u32)height - initial_font->Height(0)) / 2;
  i32 top = (i32)((u32)offset + (u32)rectangle.top + (u32)(i32)gutter255);
  ScString text;
  u32 color = 0;
  i32 drawn = 0, width = (i32)((u32)right - (u32)left);
  while (current != strings.head && drawn < visible_rows) {
    text = static_cast<WnListNode *>(current)->text;
    bool enabled = GetFlag(kWnFlagEnabled);
    u32 selected_color = enabled ? font_colors[item_index == selection ? 2 : 0] : font_colors[1];
    bool has_color = selected_color != kWnDefaultColor;
    if (has_color)
      color = selected_color;
    u32 length = text.Strlen();
    if (length && text.begin[0] == '#') {
      if (length > 1 && text.begin[1] == '#')
        text.Erase(0, 1);
      else if (length >= 7) {
        if (enabled) {
          text.Append("", 1);
          // Stock reads are defined only when parsing writes all three channels.
          u32 red = 0, green = 0, blue = 0;
          [[maybe_unused]] int parsed = sscanf(text.begin, "#%02x%02x%02x", &red, &green, &blue);
          assert(parsed == 3);
          has_color = true;
          color = MakeFillColor((u8)red, (u8)green, (u8)blue);
        }
        text.Erase(0, 7);
      } else
        text.Erase(0, length);
    }
    u32 leading = 0;
    i32 advance = 0;
    length = text.Strlen();
    if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
        language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai) {
      while (leading < length && (u8)text.begin[leading] <= 0x20)
        ++leading;
      if (leading)
        if (FtFont *prefix_font = ApGetUiResources()->GetFont(kWnFontUiFileDialogAlternate)) {
          advance = prefix_font->Measure(text.begin, leading);
          prefix_font->DrawTextA(draw_buffer, left, top, text.begin, leading);
        }
    }
    u32 remaining = length - leading;
    if (remaining) {
      u32 x = (u32)left + (u32)advance;
      u32 count = font->Fit(text.begin + leading, remaining, (u32)width);
      if (has_color)
        font->DrawTextA(draw_buffer, x, top, text.begin + leading, count, color);
      else
        font->DrawTextA(draw_buffer, (i32)((u32)left + (u32)advance), top, text.begin + leading, count);
    }
    top = (i32)((u32)top + (u32)row_height);
    ++item_index;
    ++drawn;
    current = current->next;
  }
  if (line_outline) {
    if (draw_buffer->Lock(GxLockFlags::Line)) {
      rectangle.right = (i32)((u32)rectangle.right - 1);
      rectangle.bottom = (i32)((u32)rectangle.bottom - 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.top, rectangle.right, rectangle.top, static_cast<i32>(outline_first), 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.top, rectangle.left, rectangle.bottom, static_cast<i32>(outline_first), 1);
      draw_buffer->DrawLine(
          rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom, static_cast<i32>(outline_second), 1);
      draw_buffer->DrawLine(
          rectangle.right, rectangle.top, rectangle.right, rectangle.bottom, static_cast<i32>(outline_second), 1);
      draw_buffer->Unlock(GxLockFlags::Line);
    }
  } else if (outline && draw_buffer->Lock(GxLockFlags::Rect)) {
    draw_buffer->DrawRectangleOutline(rectangle, outline_first, 1);
    draw_buffer->Unlock(GxLockFlags::Rect);
  }
  return true;
}

// Preserve each list entry and its original relative position while sorting its concrete string bytes.
struct WnAppSortEntry {
  WnListNode *node;
  u32 position;
};

// Compare the installed string byte ranges and preserve the source stable order for equal strings.
static int WnCompareAppSortEntries(const void *first, const void *second) {
  const WnAppSortEntry *left = static_cast<const WnAppSortEntry *>(first);
  const WnAppSortEntry *right = static_cast<const WnAppSortEntry *>(second);
  const ScString *left_text = &left->node->text, *right_text = &right->node->text;
  u32 left_length = left_text->begin ? static_cast<u32>(left_text->end - left_text->begin) : 0;
  u32 right_length = right_text->begin ? static_cast<u32>(right_text->end - right_text->begin) : 0;
  u32 common = left_length < right_length ? left_length : right_length;
  int order = common ? memcmp(left_text->begin, right_text->begin, common) : 0;
  if (order)
    return order;
  if (left_length != right_length)
    return left_length < right_length ? -1 : 1;
  return left->position < right->position ? -1 : left->position > right->position;
}

void WnAppList::Sort() {
  ScString selected;
  if (selection >= 0)
    GetStringFromIndex(selection, &selected);
  u32 count = strings.Count();
  if (count > 1) {
    WnAppSortEntry *entries = new WnAppSortEntry[count];
    WnListLink *link = strings.head->next;
    for (u32 i = 0; i < count; ++i) {
      entries[i] = {static_cast<WnListNode *>(link), i};
      link = link->next;
    }
    qsort(entries, count, sizeof(*entries), WnCompareAppSortEntries);
    WnListLink *previous = strings.head;
    for (u32 i = 0; i < count; ++i) {
      WnListNode *node = entries[i].node;
      previous->next = node;
      node->previous = previous;
      previous = node;
    }
    previous->next = strings.head;
    strings.head->previous = previous;
    delete[] entries;
  }
  if (selection != -1) {
    i32 index;
    [[maybe_unused]] bool found = GetIndexFromString(&index, selected);
    assert(found);
    SetSelection(index, true);
  }
}

// Initialize the original scrollbar state and construct its owned callback timer.
ApScrollBar::ApScrollBar()
    : scroll_flags(0), orientation(0), mouse_offset(0), pressed_part(0), hover_part(0), image_width(16),
      image_height(16), image_mode(0), image(nullptr), image_left(0), image_top(0), thumb_position(0), minimum(0),
      maximum(0), value(0), page_size(0), line_size(0) {
}

ApScrollBar::~ApScrollBar() {
}

bool ApScrollBar::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnScrollbarControl, WnScrollbarControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 ApScrollBar::AddRef() {
  return WnRootWidget::AddRef();
}

u32 ApScrollBar::Release() {
  return WnRootWidget::Release();
}

bool ApScrollBar::Shutdown() {
  timer.Stop();
  return WnRootWidget::Shutdown();
}

// Enforce the three-image minimum along the selected axis, set the area, and recalculate the thumb position.
void ApScrollBar::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = right - left, height = bottom - top;
  if (orientation == 2) {
    i32 limit = 3 * image_height;
    if (height < limit)
      height = limit;
  } else if (orientation == 1) {
    i32 limit = 3 * image_width;
    if (width < limit)
      width = limit;
  }
  WnRootWidget::SetArea(left, top, left + width, top + height);
  CalcThumbPositionFromValue();
}

bool ApScrollBar::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(parent, kWnMsg_KeyDown, key, modifiers, 0);
}

bool ApScrollBar::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(parent, kWnMsg_KeyUp, key, modifiers, 0);
}

// Return the primary window identity without changing its reference count.
WnWindow *ApScrollBar::GetWindow() {
  return this;
}

// Return the distinct slider identity without adding a reference.
WnSliderControl *ApScrollBar::GetSlider() {
  return this;
}

i32 ApScrollBar::GetValue() const {
  return value;
}

// Clamp the new value to the current endpoints, update the thumb and report whether the value changed.
bool ApScrollBar::SetValue(i32 incoming) {
  i32 previous = value;
  if (incoming < minimum)
    incoming = minimum;
  else if (incoming > maximum)
    incoming = maximum;
  value = incoming;
  CalcThumbPositionFromValue();
  return value != previous;
}

// Store both endpoints, reapply the current value through virtual dispatch, and refresh the thumb.
bool ApScrollBar::SetRange(i32 lower, i32 upper) {
  minimum = lower;
  maximum = upper;
  SetValue(GetValue());
  CalcThumbPositionFromValue();
  return true;
}

// Copy both stored range endpoints to the caller's outputs.
bool ApScrollBar::GetRange(i32 *lower, i32 *upper) const {
  *lower = minimum;
  *upper = maximum;
  return true;
}

// Accept horizontal or vertical orientation, then reapply the current rectangle through virtual dispatch.
bool ApScrollBar::SetOrientation(i32 incoming) {
  if (incoming <= 0 || incoming > 2)
    return false;
  orientation = static_cast<i8>(incoming);
  i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
  SetArea(left, top, right, bottom);
  return true;
}

i32 ApScrollBar::GetOrientation() const {
  return orientation;
}

bool ApScrollBar::SetPageCount(i32 count) {
  page_size = count;
  return true;
}

i32 ApScrollBar::GetPageCount() const {
  return page_size;
}

bool ApScrollBar::SetLineCount(i32 count) {
  line_size = count;
  return true;
}

i32 ApScrollBar::GetLineCount() const {
  return line_size;
}

// Derive line and page increments from unsigned division of the original range difference.
void ApScrollBar::SetDivisors(u32 line, u32 page) {
  u32 difference = static_cast<u32>(maximum) - static_cast<u32>(minimum);
  SetLineCount(static_cast<i32>(difference / line));
  difference = static_cast<u32>(maximum) - static_cast<u32>(minimum);
  SetPageCount(static_cast<i32>(difference / page));
}

// Return the original fixed-axis minimum and maximum sizes for the selected orientation.
bool ApScrollBar::GetExtent(ScRect *out) const {
  if (orientation == 2) {
    *out = {image_width, 3 * image_height, image_width, 65535};
    return true;
  }
  if (orientation == 1) {
    *out = {3 * image_width, image_height, 65535, image_height};
    return true;
  }
  return false;
}

void ApScrollBar::SetMinimum(i32) {
}

void ApScrollBar::SetMaximum(i32) {
}

void ApScrollBar::SetDirection(WnSliderDirection) {
}

bool ApScrollBar::SetImage(GxBuffer *, const ScRect *) {
  return false;
}

// Convert the thumb position into the stored range value along the active scrollbar axis.
bool ApScrollBar::CalcValueFromThumbPosition() {
  i32 extent, image_extent;
  if (orientation == 1) {
    image_extent = image_width;
    extent = GetW();
  } else if (orientation == 2) {
    image_extent = image_height;
    extent = GetH();
  } else {
    return false;
  }
  i32 track = extent - 3 * image_extent;
  if (!track)
    return false;
  value = (track * minimum + (thumb_position - image_extent) * (maximum - minimum)) / track;
  return true;
}

// Convert the current value into an axis-relative thumb position, using the first position for an empty range.
bool ApScrollBar::CalcThumbPositionFromValue() {
  i32 extent, image_extent;
  if (orientation == 1) {
    image_extent = image_width;
    extent = GetW();
  } else if (orientation == 2) {
    image_extent = image_height;
    extent = GetH();
  } else {
    return false;
  }
  i32 range = maximum - minimum;
  if (range)
    thumb_position = image_extent + (extent - 3 * image_extent) * (value - minimum) / range;
  else
    thumb_position = image_extent;
  return true;
}

// Configure the repeat timer and optional scrollbar image before initializing the root window.
bool ApScrollBar::Init() {
  if (GetInitFlag())
    return true;
  timer.SetResolution(50);
  timer.SetCallbackFunction(TimerCallback, reinterpret_cast<uptr>(this));
  GxBuffer *source = ApGetUiResources()->GetImage(kWnThemeScrollbarImageId);
  if (source) {
    u32 height = source->Height();
    u32 width = source->Width();
    ScRect rectangle{0, 0, static_cast<i32>(width / 12), static_cast<i32>(height)};
    ImageSetup(rectangle, source, 0);
  }
  i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
  SetArea(left, top, right, bottom);
  return WnRootWidget::Init();
}

// Configure the borrowed image atlas, reject out-of-bounds image cells, and reapply the current window rectangle.
bool ApScrollBar::ImageSetup(const ScRect &rectangle, GxBuffer *source, i32 mode) {
  bool invalid = false;
  image_mode = static_cast<i8>(mode);
  if (image_mode)
    image_mode = 0;
  image_width = static_cast<i8>(static_cast<u8>(rectangle.right) - static_cast<u8>(rectangle.left));
  if (!image_width)
    invalid = true;
  image_height = static_cast<i8>(static_cast<u8>(rectangle.bottom) - static_cast<u8>(rectangle.top));
  if (!image_height)
    invalid = true;
  if (source) {
    image_left = rectangle.left;
    image_top = rectangle.top;
    image = source;
    if (!image_mode) {
      i32 height = image_height;
      if (image_left < 0 || image_left + 12 * image_width > static_cast<i32>(source->Width())) {
        image = nullptr;
        invalid = true;
      }
      if (image_top < 0 || image_top + height > static_cast<i32>(source->Height())) {
        image = nullptr;
        invalid = true;
      }
    }
  } else {
    SetFillColor(MakeFillColor(222, 222, 222));
  }
  i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
  SetArea(left, top, right, bottom);
  return !invalid;
}

// Recompute the hovered scrollbar part and its drawing flags from the current pointer position.
bool ApScrollBar::UpdateMouseInfo(i32 x, i32 y) {
  InvalidateSelfAndParents();
  if (IsPointInWindow(x, y))
    scroll_flags |= 0x20;
  else
    scroll_flags &= ~0x20u;
  scroll_flags &= kApScrollWithoutArrowAndTrackParts;
  if (orientation != 1 && orientation != 2)
    return false;
  if (!(scroll_flags & 0x20)) {
    hover_part = 0;
    return true;
  }
  i32 position = orientation == 2 ? y : x;
  i32 size = orientation == 2 ? image_height : image_width;
  if (position < size) {
    scroll_flags |= 1;
    hover_part = 1;
  } else {
    i32 extent = orientation == 2 ? GetH() : GetW();
    if (position > extent - size) {
      scroll_flags |= 16;
      hover_part = 5;
    } else if (position < thumb_position) {
      scroll_flags |= 2;
      hover_part = 4;
    } else if (position < thumb_position + size) {
      scroll_flags |= 4;
      hover_part = 3;
    } else {
      scroll_flags |= 8;
      hover_part = 2;
    }
  }
  return true;
}

// Clamp the dragged thumb to its track, update the value, and invalidate the scrollbar.
bool ApScrollBar::SetThumbPosition(i32 x, i32 y) {
  i32 size, limit, position;
  if (orientation == 1) {
    size = image_width;
    limit = GetW() - 2 * size;
    position = x + mouse_offset;
  } else if (orientation == 2) {
    size = image_height;
    limit = GetH() - 2 * size;
    position = y + mouse_offset;
  } else {
    return false;
  }
  if (position < size)
    position = size;
  else if (position > limit)
    position = limit;
  thumb_position = position;
  CalcValueFromThumbPosition();
  InvalidateSelfAndParents();
  return true;
}

bool ApScrollBar::DoScrollVertical(u32 command) {
  switch (command) {
  case 0:
    return SetValue(minimum);
  case 1:
    return SetValue(maximum);
  case 2:
    return SetValue(value - line_size);
  case 3:
    return SetValue(value + line_size);
  case 6:
    return SetValue(value - page_size);
  case 7:
    return SetValue(value + page_size);
  default:
    return false;
  }
}

bool ApScrollBar::DoScrollHorizontal(u32 command) {
  switch (command) {
  case 0:
    return SetValue(minimum);
  case 1:
    return SetValue(maximum);
  case 4:
    return SetValue(value - line_size);
  case 5:
    return SetValue(value + line_size);
  case 8:
    return SetValue(value - page_size);
  case 9:
    return SetValue(value + page_size);
  default:
    return false;
  }
}

// Process matching scroll commands and forward changed-value messages to the parent window.
bool ApScrollBar::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ScrollHorizontal || message->type == kWnMsg_ScrollVertical) {
    if (message->data1 == identifier) {
      bool changed = message->type == kWnMsg_ScrollHorizontal ? DoScrollHorizontal(static_cast<u32>(message->data2))
                                                              : DoScrollVertical(static_cast<u32>(message->data2));
      if (changed)
        parent->DoMessage(message);
    }
    return true;
  }
  return WnRootWidget::DoMessage(message);
}

// Update a captured drag or hover state and send live thumb movement to the parent.
bool ApScrollBar::OnMouseMove(i32 x, i32 y, u32) {
  if (!GZAmCapturing())
    return false;
  if (pressed_part == 3) {
    SetThumbPosition(x, y);
    u32 id = GetID();
    SendMsg(parent, orientation == 2 ? kWnMsg_ScrollVertical : kWnMsg_ScrollHorizontal, id, 11, 0);
  } else {
    UpdateMouseInfo(x, y);
  }
  return true;
}

// Capture the mouse, identify the pressed part, and begin repeating an arrow or page action.
bool ApScrollBar::OnMouseDownL(i32 x, i32 y, u32) {
  if (owner->GetCapture() || !GZSetCapture())
    return false;
  UpdateMouseInfo(x, y);
  pressed_part = hover_part;
  if (pressed_part == 3) {
    mouse_offset = static_cast<i8>(static_cast<u8>(thumb_position) - static_cast<u8>(orientation == 2 ? y : x));
  } else if (pressed_part > 0 && pressed_part <= 5) {
    PostScrollMessageToSelfMouse();
    timer.Stop();
    timer.SetPeriod(200);
    timer.StartPeriodic(0, nullptr);
  }
  return true;
}

// Release capture and the repeat timer, then post the final thumb-release notification to the parent.
bool ApScrollBar::OnMouseUpL(i32, i32, u32) {
  scroll_flags &= ~0x20u;
  GZReleaseCapture();
  timer.Stop();
  scroll_flags &= kApScrollWithoutPressedParts;
  mouse_offset = 0;
  u32 id = GetID();
  PostMsg(parent, orientation == 2 ? kWnMsg_ScrollVertical : kWnMsg_ScrollHorizontal, id, 12, 0);
  InvalidateSelfAndParents();
  return true;
}

// Post the matching arrow or page command while the pressed part remains under the captured mouse.
bool ApScrollBar::PostScrollMessageToSelfMouse() {
  if (pressed_part != hover_part || !(scroll_flags & 0x20))
    return false;
  u32 command;
  if (orientation == 2) {
    switch (pressed_part) {
    case 1:
      command = 2;
      break;
    case 2:
      command = 7;
      break;
    case 4:
      command = 6;
      break;
    case 5:
      command = 3;
      break;
    default:
      return false;
    }
  } else {
    switch (pressed_part) {
    case 1:
      command = 4;
      break;
    case 2:
      command = 9;
      break;
    case 4:
      command = 8;
      break;
    case 5:
      command = 5;
      break;
    default:
      return false;
    }
  }
  u32 id = GetID();
  return PostMsg(this, orientation == 2 ? kWnMsg_ScrollVertical : kWnMsg_ScrollHorizontal, id, command, 0);
}

// Recheck the captured mouse and accelerate the periodic scroll while the same part remains active.
void ApScrollBar::ProcessTimerCallback() {
  i32 x, y;
  GetWindowManager()->GetCursorScreenPosition(&x, &y);
  ScreenToWindowCoordinates(&x, &y);
  UpdateMouseInfo(x, y);
  if (PostScrollMessageToSelfMouse()) {
    u32 period;
    timer.GetPeriod(&period);
    if (period > 50)
      timer.SetPeriod(period - 50);
  }
}

void ApScrollBar::TimerCallback(uptr receiver) {
  if (receiver)
    reinterpret_cast<ApScrollBar *>(receiver)->ProcessTimerCallback();
}

// Allocate the executable scrollbar and return its unretained primary window identity.
ScUnknown *ApScrollBar::CreateNewObject() {
  return static_cast<WnWindow *>(new ApScrollBar);
}

// Paint the selected horizontal or vertical scrollbar, locking the image target when an atlas is present.
bool ApScrollBar::GZPaint() {
  if (orientation == 2) {
    if (image) {
      if (!draw_buffer->Lock(static_cast<GxLockFlags>(16)))
        return false;
      DrawVerticalImage(image_left + 6 * image_width);
      draw_buffer->Unlock(static_cast<GxLockFlags>(16));
    } else {
      DrawVerticalFlat();
    }
    return true;
  }
  if (orientation != 1)
    return false;
  if (image) {
    if (!draw_buffer->Lock(static_cast<GxLockFlags>(16)))
      return false;
    DrawHorizontalImage(image_left);
    draw_buffer->Unlock(static_cast<GxLockFlags>(16));
  } else {
    DrawHorizontalFlat();
  }
  return true;
}

// Draw the horizontal arrow cells, tiled track and thumb from the selected image atlas.
bool ApScrollBar::DrawHorizontalImage(i32 atlas_x) const {
  ScRect source{atlas_x, image_top, atlas_x + image_width, image_top + image_height};
  ScRect target{draw_area.left, draw_area.top, draw_area.left + image_width, draw_area.bottom};
  if (scroll_flags & 1) {
    source.left += image_width;
    source.right += image_width;
  }
  draw_buffer->Blt(image, source, target, static_cast<const ScRect *>(nullptr));
  target.left = target.right;
  target.right = draw_area.right - image_width;
  source.left = atlas_x + 3 * image_width;
  source.right = source.left + image_width;
  draw_buffer->BltTiled(image, source, target, 0, 0, nullptr);
  target.left = target.right;
  target.right += image_width;
  source.left = atlas_x + ((scroll_flags & 16) ? 5 : 4) * image_width;
  source.right = source.left + image_width;
  draw_buffer->Blt(image, source, target, static_cast<const ScRect *>(nullptr));
  target.left = draw_area.left + thumb_position;
  target.right = target.left + image_width;
  source.left = atlas_x + 2 * image_width;
  source.right = source.left + image_width;
  draw_buffer->Blt(image, source, target, static_cast<const ScRect *>(nullptr));
  return true;
}

// Draw the vertical arrow cells, tiled track and thumb from the selected image atlas.
bool ApScrollBar::DrawVerticalImage(i32 atlas_x) const {
  ScRect source{atlas_x, image_top, atlas_x + image_width, image_top + image_height};
  ScRect target{draw_area.left, draw_area.top, draw_area.right, draw_area.top + image_height};
  if (scroll_flags & 1) {
    source.left += image_width;
    source.right += image_width;
  }
  draw_buffer->Blt(image, source, target, static_cast<const ScRect *>(nullptr));
  target.top = target.bottom;
  target.bottom = draw_area.bottom - image_height;
  source.left = atlas_x + 3 * image_width;
  source.right = source.left + image_width;
  draw_buffer->BltTiled(image, source, target, 0, 0, nullptr);
  target.top = target.bottom;
  target.bottom = draw_area.bottom;
  source.left = atlas_x + ((scroll_flags & 16) ? 5 : 4) * image_width;
  source.right = source.left + image_width;
  draw_buffer->Blt(image, source, target, static_cast<const ScRect *>(nullptr));
  target.top = draw_area.top + thumb_position;
  target.bottom = target.top + image_height;
  source.left = atlas_x + 2 * image_width;
  source.right = source.left + image_width;
  draw_buffer->Blt(image, source, target, static_cast<const ScRect *>(nullptr));
  return true;
}

// Paint the horizontal fallback using raised arrow cells, the current fill color and a raised thumb.
bool ApScrollBar::DrawHorizontalFlat() const {
  ScRect rectangle{draw_area.left, draw_area.top, draw_area.left + image_width, draw_area.top + image_height};
  draw_buffer->DrawRaisedRect(rectangle, 191, 191, 191, !(scroll_flags & 1), true);
  if (!draw_buffer->Lock(static_cast<GxLockFlags>(1)))
    return false;
  rectangle.left = rectangle.right;
  rectangle.right = draw_area.right - image_width;
  u32 color = GetFillColor();
  draw_buffer->Fill(color, rectangle, nullptr);
  draw_buffer->Unlock(static_cast<GxLockFlags>(1));
  rectangle.left = rectangle.right;
  rectangle.right = draw_area.right;
  draw_buffer->DrawRaisedRect(rectangle, 191, 191, 191, !(scroll_flags & 16), true);
  rectangle.left = draw_area.left + thumb_position;
  rectangle.right = rectangle.left + image_width;
  draw_buffer->DrawRaisedRect(rectangle, 191, 191, 191, true, true);
  return true;
}

// Paint the vertical fallback using raised arrow cells, the current fill color and a raised thumb.
bool ApScrollBar::DrawVerticalFlat() const {
  ScRect rectangle{draw_area.left, draw_area.top, draw_area.left + image_width, draw_area.top + image_height};
  draw_buffer->DrawRaisedRect(rectangle, 191, 191, 191, !(scroll_flags & 1), true);
  if (!draw_buffer->Lock(static_cast<GxLockFlags>(1)))
    return false;
  rectangle.top = rectangle.bottom;
  rectangle.bottom = draw_area.bottom - image_height;
  u32 color = GetFillColor();
  draw_buffer->Fill(color, rectangle, nullptr);
  draw_buffer->Unlock(static_cast<GxLockFlags>(1));
  rectangle.top = rectangle.bottom;
  rectangle.bottom = draw_area.bottom;
  draw_buffer->DrawRaisedRect(rectangle, 191, 191, 191, !(scroll_flags & 16), true);
  rectangle.top = draw_area.top + thumb_position;
  rectangle.bottom = rectangle.top + image_height;
  draw_buffer->DrawRaisedRect(rectangle, 191, 191, 191, true, true);
  return true;
}

// Construct the original empty item map and initialize the list's owned state, leaving the unused view rectangle untouched.
ApListView::ApListView()
    : list_flags(0), heading(nullptr), font(nullptr), selected(0), first_visible(0), last_visible(0), row_height(0),
      scroll_x(0), scroll_y(0), scroll_max(0), virtual_width(0), virtual_height(0), horizontal(nullptr),
      vertical(nullptr) {
}

ApListView::~ApListView() {
}

bool ApListView::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ApListViewControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<ApListViewControl *>(this);
  AddRef();
  return true;
}

u32 ApListView::AddRef() {
  return WnRootWidget::AddRef();
}

u32 ApListView::Release() {
  return WnRootWidget::Release();
}

// Return the primary window identity without taking a reference.
WnWindow *ApListView::AsIGZWin() {
  return this;
}

bool ApListView::SetListViewFlag(i32 mask, bool enabled) {
  if (enabled)
    list_flags |= static_cast<u32>(mask);
  else
    list_flags &= ~static_cast<u32>(mask);
  return true;
}

bool ApListView::GetListViewFlag(i32 mask) {
  return (list_flags & static_cast<u32>(mask)) != 0;
}

// Borrow the font, update the row height, and reapply the current rectangle when a font is supplied.
bool ApListView::SetFont(FtFont *incoming) {
  font = incoming;
  if (incoming) {
    row_height = incoming->Height(0);
    i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
    SetArea(left, top, right, bottom);
  }
  return true;
}

i32 ApListView::GetSelectedItemID() {
  return selected;
}

// Return the original base limits, adding each enabled scrollbar's minimum dimensions when available.
bool ApListView::GetMinMaxInfo(ScRect *out) const {
  *out = {8, 8, 65535, 65535};
  ScRect rectangle;
  if ((list_flags & 0x40000u) && vertical->GetExtent(&rectangle)) {
    out->left += rectangle.left;
    out->top += rectangle.top;
  }
  if ((list_flags & 0x20000u) && horizontal->GetExtent(&rectangle)) {
    out->left += rectangle.left;
    out->top += rectangle.top;
  }
  return true;
}

bool ApListView::AddColumn(i32 column, i32 width, const ScStringView *text) {
  if (!heading) {
    heading = ScAddRefNotNull(new ApListItem);
    heading->SetColText(column, text);
  }
  if (column < static_cast<i32>(heading->columns.size()) - 1)
    return false;
  heading->SetColWidth(column, width);
  heading->SetColText(column, text);
  return true;
}

// Find the first row by its full signed item identifier and return the borrowed item.
ApListItem *ApListView::GetItem(i32 item_id) {
  auto position = rows.find(item_id);
  return position == rows.end() ? nullptr : position->second;
}

bool ApListView::SetItemText(i32 item, i32 column, const ScStringView *text) {
  ApListItem *row = GetItem(item);
  return row ? row->SetColText(column, text) : false;
}

// Return a row's borrowed column text and report whether the stored text pointer is non-null.
bool ApListView::GetItemText(i32 item, i32 column, const ScStringView **out) {
  ApListItem *row = GetItem(item);
  if (!row)
    return false;
  *out = row->GetColText(column);
  return *out != nullptr;
}

// Initialize only a list that has a font, calculating its visible row range before the root initialization.
bool ApListView::Init() {
  if (GetInitFlag())
    return true;
  if (!font)
    return false;
  CalcViewIdRange();
  return WnRootWidget::Init();
}

bool ApListView::Shutdown() {
  HideWindow();
  while (!rows.empty())
    RemoveItem(rows.begin()->first);
  ScRelease(&heading);
  ScRelease(&vertical);
  ScRelease(&horizontal);
  return WnRootWidget::Shutdown();
}

// Apply the root rectangle, then update the viewport, scroll range, visible rows and scrollbar state.
void ApListView::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  CalcViewArea();
  CalcScrollMax();
  CalcViewIdRange();
  UpdateScrollBars();
}

// Dispatch original horizontal and vertical scrollbar messages and pass other messages to the root window.
bool ApListView::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ScrollHorizontal)
    return OnScrollHorizontal(*message);
  if (message->type == kWnMsg_ScrollVertical)
    return OnScrollVertical(*message);
  return WnRootWidget::DoMessage(message);
}

// Create a retained row for a nonzero identifier, preserving the original duplicate-insertion behavior.
bool ApListView::AddItem(i32 item, i32 image, const ScStringView *text) {
  if (!heading || !item)
    return false;
  ApListItem *row = new ApListItem;
  row->AddRef();
  row->SetImageID(image);
  row->SetColText(0, text);
  if (!rows.emplace(item, row).second)
    return false;
  CalcVirtualSize();
  CalcScrollMax();
  CalcViewIdRange();
  UpdateScrollBars();
  return true;
}

// Clear a matching selection, release the first matching row, and refresh the dependent layout.
bool ApListView::RemoveItem(i32 item) {
  if (selected == item)
    selected = 0;
  auto position = rows.find(item);
  if (position == rows.end())
    return false;
  position->second->Release();
  rows.erase(position);
  CalcVirtualSize();
  CalcScrollMax();
  CalcViewIdRange();
  UpdateScrollBars();
  return true;
}

// Release and erase rows in map order, then refresh the original three dependent layout values.
bool ApListView::RemoveAllItems() {
  while (!rows.empty()) {
    auto position = rows.begin();
    position->second->Release();
    rows.erase(position);
  }
  CalcVirtualSize();
  CalcScrollMax();
  CalcViewIdRange();
  return true;
}

// Find the requested row in sorted order and scroll to its top if it lies outside the visible window.
bool ApListView::EnsureVisibility(i32 item) {
  if (rows.empty())
    return false;
  i32 top = 0;
  auto position = rows.begin();
  while (position != rows.end() && position->first != item) {
    top += row_height;
    ++position;
  }
  if (position == rows.end())
    return false;
  if (top < scroll_y)
    ScrollTo(scroll_x, top);
  if (top + row_height > scroll_y + GetH())
    ScrollTo(scroll_x, top);
  return true;
}

bool ApListView::SetSelectedItem(i32 item) {
  return SelectItem(item);
}

// Return the sorted row identifier at a point inside the current view, or zero when no row is present.
i32 ApListView::GetItemAt(i32 x, i32 y) {
  if (rows.empty() || x < view_area.left || y < view_area.top || x > view_area.right || y > view_area.bottom)
    return 0;
  i32 position = y + scroll_y - view_area.top;
  if (position < 0 || position >= virtual_height)
    return 0;
  i32 top = 0;
  for (const auto &row : rows) {
    if (position >= top && position < top + row_height)
      return row.first;
    top += row_height;
  }
  return 0;
}

// Clear the previous selected flag when applicable, select an existing row and notify the parent in order.
bool ApListView::SelectItem(i32 item) {
  if (selected) {
    auto old = rows.find(selected);
    if (old != rows.end() && !(list_flags & 0x10)) {
      old->second->SetListItemFlag(1, false);
      u32 id = GetID();
      SendMsg(parent, kWnMsg_ListViewItemDeselected, id, selected, 0);
    }
  }
  auto position = rows.find(item);
  if (position != rows.end()) {
    position->second->SetListItemFlag(1, true);
    selected = item;
    AsIGZWin()->InvalidateSelfAndParents();
    u32 id = GetID();
    SendMsg(parent, kWnMsg_ListViewItemSelected, id, item, 0);
  }
  return true;
}

// Recompute the virtual content dimensions from the heading and row count, updating scrollbars only when they change.
bool ApListView::CalcVirtualSize() {
  if (!heading) {
    virtual_height = 0;
    virtual_width = 0;
    return false;
  }
  i32 previous_width = virtual_width, previous_height = virtual_height;
  virtual_height = static_cast<i32>(rows.size()) * row_height;
  virtual_width = heading->GetTotalWidth();
  if (previous_width != virtual_width || previous_height != virtual_height)
    UpdateScrollBars();
  return true;
}

// Compute the vertical scroll limit, clamp the current position and update a present scrollbar range.
bool ApListView::CalcScrollMax() {
  scroll_max = view_area.top + virtual_height - view_area.bottom;
  if (scroll_max < 0)
    scroll_max = 0;
  if (scroll_y > scroll_max)
    ScrollTo(scroll_x, scroll_max);
  if (vertical)
    vertical->SetRange(0, scroll_max);
  return true;
}

// Store the horizontal offset, clamp the vertical offset, update the scrollbar and recompute visible rows.
bool ApListView::ScrollTo(i32 x, i32 y) {
  scroll_x = x;
  scroll_y = y;
  if (scroll_y < 0)
    scroll_y = 0;
  if (scroll_y > scroll_max)
    scroll_y = scroll_max;
  if (vertical)
    vertical->SetValue(scroll_y);
  return CalcViewIdRange();
}

bool ApListView::OnScrollHorizontal(const ScMessage &) {
  return false;
}

// Handle selected vertical scroll commands from the current scrollbar, then keep the first visible row in view.
bool ApListView::OnScrollVertical(const ScMessage &message) {
  if (!vertical || vertical->GetWindow()->GetID() != message.data1)
    return false;
  u32 command = static_cast<u32>(message.data2);
  if (command < 2 || (command > 3 && (command <= 5 || (command > 7 && command != 11))))
    return false;
  i32 position = vertical->GetValue();
  ScrollTo(scroll_x, position);
  EnsureVisibility(first_visible);
  return true;
}

// Calculate the first and last visible row identifiers in sorted order and invalidate when a first row is found.
bool ApListView::CalcViewIdRange() {
  if (rows.empty()) {
    first_visible = 0;
    last_visible = 0;
    return true;
  }
  i32 bottom = scroll_y + view_area.bottom - view_area.top, position = 0;
  bool found_first = false, found_last = false;
  for (const auto &row : rows) {
    position += row_height;
    if (!found_first) {
      if (position > scroll_y) {
        found_first = true;
        first_visible = row.first;
      }
    } else if (position >= bottom) {
      found_last = true;
      last_visible = row.first;
      break;
    }
  }
  if (!found_first) {
    first_visible = 0;
    last_visible = 0;
    return false;
  }
  if (!found_last)
    last_visible = rows.rbegin()->first;
  InvalidateSelfAndParents();
  return true;
}

// Create the original scrollbar class, attach its window and set the requested axis before relayout.
bool ApListView::CreateScrollBar(i32 axis, WnScrollbarControl **out) {
  *out = nullptr;
  if (axis != 1 && axis != 2)
    return false;
  if (!ApGetCom()->GetClassObject(kWnAppScrollbarClassId, IID_WnScrollbarControl, reinterpret_cast<void **>(out)) ||
      !*out)
    return false;
  WnWindow *window = (*out)->GetWindow();
  window->SetID(static_cast<u32>(axis));
  ScRect rectangle{0, 0, 16, 16};
  if (!(*out)->ImageSetup(rectangle, nullptr, 0) || !ChildAdd(window)) {
    if (GetWindowManager())
      GetWindowManager()->DestroyWindow(window);
    (*out)->Release();
    *out = nullptr;
    return false;
  }
  (*out)->SetOrientation(axis == 1 ? 2 : 1);
  i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
  SetArea(left, top, right, bottom);
  return true;
}

// Show or hide the vertical scrollbar as the virtual content crosses the current viewport height.
bool ApListView::UpdateScrollBars() {
  if (virtual_height <= view_area.bottom - view_area.top) {
    if (!(list_flags & 0x40000u))
      return true;
    if (vertical)
      vertical->GetWindow()->HideWindow();
    list_flags &= ~0x40000u;
    CalcViewArea();
    return true;
  }
  if (list_flags & 0x40000u)
    return true;
  if (!vertical && !CreateScrollBar(1, &vertical))
    return false;
  vertical->GetWindow()->ShowWindow();
  list_flags |= 0x40000u;
  CalcViewArea();
  return true;
}

// Set the content viewport and size a visible vertical scrollbar using its actual window dimensions.
bool ApListView::CalcViewArea() {
  i32 border = GetListViewFlag(4) ? 1 : 0;
  view_area.left = border + 4;
  view_area.top = border + ((list_flags & 1) ? row_height : 0);
  view_area.right = GetW() - 4 - border;
  view_area.bottom = GetH() - border;
  if (list_flags & 0x40000u) {
    UpdateScrollBars();
    WnWindow *window = vertical->GetWindow();
    vertical->SetPageCount(view_area.bottom - view_area.top);
    vertical->SetLineCount(row_height);
    i32 right_width = window->GetW();
    i32 own_width = GetW();
    view_area.right = own_width - right_width;
    i32 width = window->GetW();
    window->SetArea(view_area.right, view_area.top, view_area.right + width, view_area.bottom);
  }
  return true;
}

// Select and reveal the adjacent row for up/down keys, forwarding all other keys to the parent.
bool ApListView::OnKeyDown(u32 key, u32 modifiers) {
  if (key != kWnKeyUp && key != kWnKeyDown)
    return SendMsg(parent, kWnMsg_KeyDown, key, modifiers, 0);
  auto position = rows.find(selected);
  if (position != rows.end()) {
    if (key == kWnKeyUp) {
      if (position != rows.begin())
        --position;
    } else {
      ++position;
      if (position == rows.end())
        --position;
    }
    SelectItem(position->first);
    EnsureVisibility(position->first);
  }
  return true;
}

// Select the row under the mouse and send the original double-click notification using the shared click timer.
bool ApListView::OnMouseDownL(i32 x, i32 y, u32) {
  static ScStopwatch timer(ScStopwatch::Milliseconds, 0);
  static u32 last_time;
  static i32 last_item;
  i32 item = GetItemAt(x, y);
  if (item) {
    SelectItem(item);
    last_item = item;
  }
  if (last_time) {
    if (item == last_item && timer.GetElapsedTime() - last_time < WnDoubleClickTime()) {
      i32 selected_item = GetSelectedItemID();
      u32 id = GetID();
      SendMsg(parent, kWnMsg_ListViewDoubleClick, id, selected_item, 0);
    }
    last_time = timer.GetElapsedTime();
  } else {
    timer.Restart();
    last_time = 1;
  }
  return true;
}

// Format and draw each populated row column, restoring the font color after drawing its clipped text.
void ApListView::DrawItem(ApListItem *item, i32 x, i32 y) {
  i32 count = static_cast<i32>(heading->columns.size());
  for (i32 column = 0; column < count; ++column) {
    i32 width = count <= 1 ? view_area.right - view_area.left : heading->GetColWidth(column);
    const ScStringView *source = item->GetColText(column);
    if (source) {
      ScString text;
      if (list_flags & 0x20) {
        ScString copy(*source);
        ScString prefix("\x04 ");
        prefix.Append(copy.begin, static_cast<u32>(copy.end - copy.begin));
        text = prefix;
      } else {
        ScString copy(*source);
        text = copy;
      }
      UiTextHandle::FormatForEllipsis(&text, width, font);
      if (text.begin != text.end) {
        u32 previous = font->Color();
        u32 color = item->GetListItemFlag(1) ? MakeFillColor(255, 255, 255) : MakeFillColor(49, 54, 124);
        font->SetColor(color);
        font->DrawTextA(draw_buffer, x, y, text.begin, static_cast<u32>(text.end - text.begin), &draw_area);
        font->SetColor(previous);
      }
    }
    x += width;
  }
}

// Paint the list background, border, visible selected rows and optional heading through the original buffer interface.
bool ApListView::GZPaint() {
  if (list_flags & 8) {
    if (!draw_buffer->Lock(static_cast<GxLockFlags>(1)))
      return false;
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(static_cast<GxLockFlags>(1));
  }
  if (list_flags & 4) {
    i32 right = draw_area.right - 1, bottom = draw_area.bottom - 1;
    if (!draw_buffer->Lock(static_cast<GxLockFlags>(4)))
      return false;
    u32 light = ApGetUiResources()->GetColor(19);
    u32 dark = ApGetUiResources()->GetColor(20);
    draw_buffer->DrawLine(draw_area.left, draw_area.top, draw_area.left, bottom, light, 1);
    draw_buffer->DrawLine(draw_area.left, draw_area.top, right, draw_area.top, light, 1);
    draw_buffer->DrawLine(right, draw_area.top, right, bottom, dark, 1);
    draw_buffer->DrawLine(draw_area.left, bottom, right, bottom, dark, 1);
    draw_buffer->Unlock(static_cast<GxLockFlags>(4));
  }
  if (heading && !rows.empty()) {
    auto position = rows.find(first_visible), last = rows.find(last_visible);
    i32 x = view_area.left + draw_area.left;
    ScRect rectangle{x, 0, draw_area.left + view_area.right, 0};
    i32 y = draw_area.top + view_area.top;
    i32 offset = scroll_y % row_height;
    if (offset)
      y -= offset;
    if (!draw_buffer->Lock(static_cast<GxLockFlags>(16)))
      return false;
    for (;;) {
      ApListItem *item = position->second;
      if (item->GetListItemFlag(1) && draw_buffer->Lock(static_cast<GxLockFlags>(1))) {
        rectangle.top = y + 1;
        rectangle.bottom = y + row_height;
        u32 color = MakeFillColor(0, 0, 255);
        draw_buffer->Fill(color, rectangle, &draw_area);
        draw_buffer->Unlock(static_cast<GxLockFlags>(1));
      }
      DrawItem(item, x, y);
      if (position == last)
        break;
      y += row_height;
      ++position;
    }
    draw_buffer->Unlock(static_cast<GxLockFlags>(16));
    if (list_flags & 1) {
      rectangle = {draw_area.left, draw_area.top, draw_area.right, draw_area.top + row_height};
      draw_buffer->DrawRaisedRect(rectangle, 128, 128, 128, true, true);
      DrawItem(heading, x, rectangle.top);
    }
  }
  return true;
}

// Construct the real executable list view and return its unretained primary window identity.
ScUnknown *ApListView::CreateNewObject() {
  return static_cast<WnWindow *>(new ApListView);
}

ApListItem::ApListItem() : flags(0), image_id(0), references(0) {
}

// Release each retained column text in insertion order before erasing its column and retiring the vector.
ApListItem::~ApListItem() {
  while (!columns.empty()) {
    const ScStringView *text = columns.front().text;
    if (text) {
      const_cast<ScStringView *>(text)->Release();
      columns.front().text = nullptr;
    }
    columns.erase(columns.begin());
  }
}

bool ApListItem::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ApListItem::AddRef() {
  ++references;
  return static_cast<u32>(static_cast<i32>(static_cast<i16>(references)));
}

// Decrement the stored count and destroy the item when its signed value is no longer positive.
u32 ApListItem::Release() {
  --references;
  i16 count = static_cast<i16>(references);
  if (count > 0)
    return static_cast<u32>(count);
  delete this;
  return 0;
}

// Grow the original column vector through the requested index and return the live column record.
ApListColumn *ApListItem::GetColInfo(u32 column) {
  if (column + 1 >= columns.size()) {
    try {
      columns.resize(column + 1, ApListColumn{0, 0, nullptr});
    } catch (const std::length_error &) {
      ScFatal(SC_ERROR_NOMEM, "List column count exceeds available storage");
    }
  }
  return &columns[column];
}

// Retain the supplied text before storing it in the selected column, including replacement of a prior pointer.
bool ApListItem::SetColText(i32 column, const ScStringView *text) {
  if (text)
    const_cast<ScStringView *>(text)->AddRef();
  GetColInfo(static_cast<u32>(column))->text = text;
  return true;
}

// Return the selected column's borrowed text, growing the column array when necessary.
const ScStringView *ApListItem::GetColText(i32 column) {
  return GetColInfo(static_cast<u32>(column))->text;
}

bool ApListItem::SetListItemFlag(i32 mask, bool enabled) {
  if (enabled)
    flags |= static_cast<u32>(mask);
  else
    flags &= ~static_cast<u32>(mask);
  return true;
}

bool ApListItem::GetListItemFlag(i32 mask) {
  return (flags & static_cast<u32>(mask)) != 0;
}

bool ApListItem::SetImageID(i32 image) {
  image_id = static_cast<i16>(image);
  return true;
}

i32 ApListItem::GetImageID() {
  return image_id;
}

i32 ApListItem::GetColWidth(i32 column) {
  return GetColInfo(static_cast<u32>(column))->width;
}

bool ApListItem::SetColWidth(i32 column, i32 width) {
  GetColInfo(static_cast<u32>(column))->width = static_cast<i16>(width);
  return true;
}

// Sum the signed widths of all currently allocated columns.
i32 ApListItem::GetTotalWidth() {
  i32 total = 0;
  for (const ApListColumn &column : columns)
    total += column.width;
  return total;
}
