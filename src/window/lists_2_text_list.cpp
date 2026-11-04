#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "window/lists.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "city/sc_stopwatch.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "common/utf8.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/double_click.h"
#include "window/text_edit.h"

// Forward a captured release inside the scrollbar after translating coordinates, then recapture the list.
// Other captured releases drop ordinary-list capture before dispatching pending activation and clearing click state.
bool WnList::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  if (owner->GetCapture() != static_cast<WnWindow *>(this))
    return true;
  if (IsPointInWindow(x, y))
    if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
      WnWindow *window = scrollbar->GetWindow();
      i32 child_x = x, child_y = y;
      WindowToWindowCoordinates(window, &child_x, &child_y);
      if (WnListContainsTranslated(window, &child_x, &child_y)) {
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
bool WnList::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  if (owner->GetCapture() != static_cast<WnWindow *>(this) || !popup)
    return true;
  if (!IsPointInWindow(x, y))
    return true;
  if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
    WnWindow *window = scrollbar->GetWindow();
    i32 child_x = x, child_y = y;
    WindowToWindowCoordinates(window, &child_x, &child_y);
    if (WnListContainsTranslated(window, &child_x, &child_y)) {
      ScMessage message{kWnMsg_MouseMove, (u32)child_x, (u32)child_y, modifiers};
      bool handled = window->DoMessage(&message);
      owner->SetCapture(this);
      return handled;
    }
  }
  i32 relative_y = (i32)((u32)y - (u32)(i32)gutter255);
  i32 index = (i32)((u32)first_visible + (u32)(relative_y / row_height));
  if (selection != index && index < GetItemCount())
    SetSelection(index, 0);
  return true;
}

// Resolve the clicked row before checking capture, forwarding captured scrollbar presses or completing a popup.
// Fresh presses capture the list and select through its live interface before updating the shared double-click timer.
bool WnList::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  static ScStopwatch timer(ScStopwatch::Milliseconds, 0);
  static u32 last_time = 0;
  i32 index = (i32)GetItemIndexFromCursorPosition(x, y, 1);
  if (owner->GetCapture() == static_cast<WnWindow *>(this)) {
    if (IsPointInWindow(x, y)) {
      if (WnScrollbarControl *scrollbar = GetScrollbarV()) {
        WnWindow *window = scrollbar->GetWindow();
        i32 child_x = x, child_y = y;
        WindowToWindowCoordinates(window, &child_x, &child_y);
        if (WnListContainsTranslated(window, &child_x, &child_y)) {
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
    if (index < GetItemCount() && index != GetSelection())
      SetSelection(index, 1);
    if (last_time) {
      if (index == last_click) {
        u32 elapsed = timer.GetElapsedTime();
        elapsed -= last_time;
        u32 threshold = WnDoubleClickTime();
        if (elapsed < threshold)
          double_click = 1;
      }
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

// Retain the incoming font before releasing the live old field, publish it and dispatch the original virtual row update.
// The genuine public setter returns void after the row metrics have been updated.
void WnList::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  CalculateAllMetrics();
}

// Recompute row height from the saved minimum, row image and font before signed visible-row division through live GetH.
// Preserve repeated metric calls; the original protected method returns no value.
void WnList::CalculateAllMetrics() {
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
    visible_rows = inner / row_height;
  } else
    visible_rows = 0;
}

// Position the borrowed first scrollbar using the original independent window and size queries.
// The void geometry update leaves an absent scrollbar untouched.
void WnList::SetScrollBarSizes() {
  WnScrollbarControl *scrollbar = GetScrollbarV();
  if (!scrollbar)
    return;
  WnWindow *window = scrollbar->GetWindow();
  i32 width = window->GetW(), right = GetW();
  i32 left = (i32)((u32)right - (u32)width), bottom = GetH();
  scrollbar->GetWindow()->SetArea(left, 0, right, bottom);
}

// Publish the first scrollbar range, two real divisors and live first-visible value through its borrowed interface.
// The original void method skips synchronization when the bar is absent or no rows are visible.
void WnList::SetScrollBarValue() {
  WnScrollbarControl *scrollbar = GetScrollbarV();
  if (!scrollbar || !visible_rows)
    return;
  i32 count = GetItemCount();
  i32 range = (i32)((u32)count - (u32)visible_rows);
  if (range < 0)
    range = 0;
  scrollbar->SetRange(0, range);
  count = GetItemCount();
  i32 pages = (i32)((u32)(count / visible_rows) + 1);
  scrollbar->SetDivisors(range, pages);
  scrollbar->SetValue(GetFirstVisible());
}

// Compare requested size before root geometry, always recalculate rows, and size/synchronize scrollbars only on size change.
// Preserve short-circuit width/height queries and unconditional final parent-chain invalidation.
void WnList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  bool changed = width != GetW() || height != GetH();
  WnRootWidget::SetArea(left, top, right, bottom);
  CalculateAllMetrics();
  if (changed) {
    SetScrollBarSizes();
    SetScrollBarValue();
  }
  InvalidateSelfAndParents();
}

// Store and clamp the first row through the original independent live count queries, then synchronize and invalidate.
// The public viewport setter returns no value after the parent-chain invalidation.
void WnList::SetIndexOfTopLine(i32 index) {
  first_visible = index;
  i32 count = GetItemCount();
  if (count > visible_rows) {
    count = GetItemCount();
    i32 last = (i32)((u32)count - (u32)visible_rows);
    if (first_visible > last) {
      count = GetItemCount();
      first_visible = (i32)((u32)count - (u32)visible_rows);
    }
  }
  if (first_visible < 0)
    first_visible = 0;
  SetScrollBarValue();
  InvalidateSelfAndParents();
}

// Bring the requested index into the stored viewport through the live first-row operation, without item clamping.
// This void method synchronizes and invalidates again after a nested first-row setter.
void WnList::PutIndexInView(i32 index) {
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

// Clamp a requested selection using the source's independent count queries, ensuring visibility only after a changed row.
// Notify for every nonzero request outside popup mode and always invalidate, including unchanged selections.
bool WnList::SetSelection(i32 index, bool notify) {
  i32 chosen = index;
  if (index != -1) {
    bool in_range = index >= 0 && index < GetItemCount();
    if (!in_range) {
      if (index < 0)
        chosen = 0;
      if (chosen >= GetItemCount())
        chosen = (i32)((u32)GetItemCount() - 1);
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
bool WnList::ScrollPageUp() {
  if (GetItemCount() > 0) {
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
bool WnList::ScrollPageDown() {
  i32 first = first_visible, count = GetItemCount();
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

i32 WnList::GetRowHeight() const {
  return row_height;
}

// Store the unsigned byte minimum as the signed row-height floor before recalculating all metrics.
// Invalidate the window and its parents after the live metric update, then report success.
bool WnList::SetMinimumLineHeight(u8 height) {
  minimum_height = height;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

i32 WnList::GetVisibleRows() const {
  return visible_rows;
}

i32 WnList::GetFirstVisible() const {
  return first_visible;
}

// Derive a row from y after the source's optional viewport clamp; x is intentionally unused.
// Nonempty lists always query height, and only indices above the item range receive the source's final clamp.
u32 WnList::GetItemIndexFromCursorPosition(i32, i32 y, bool clamp) const {
  i32 count = GetItemCount();
  if (!count)
    return UINT32_MAX;
  i32 target = y;
  if (y < 0 && clamp)
    target = 0;
  if (target >= GetH() && clamp)
    target = (i32)((u32)GetH() - 1);
  i32 delta = (i32)((u32)target - (u32)(i32)gutter255);
  i32 index = (i32)((u32)first_visible + (u32)(delta / row_height));
  if (index >= count)
    return clamp ? (u32)count - 1 : UINT32_MAX;
  return (u32)index;
}

// Store both original Boolean scrollbar-use inputs and report success.
// The setter creates or removes no scrollbar.
bool WnList::EnableScrollBar(bool first, bool second) {
  byte256 = first;
  byte257 = second;
  return true;
}

void WnList::SetDrop(bool enabled) {
  popup = enabled;
}

// Publish a valid indexed font color through its genuine output. Invalid indices leave the caller's word untouched.
bool WnList::GetFontColor(i32 index, u32 *color) {
  if ((u32)index > 2)
    return false;
  *color = font_colors[index];
  return true;
}

// Retain the incoming popup frame before releasing the old image and publishing the replacement.
// The original public virtual returns void without recalculating rows or invalidating.
void WnList::SetPopupBackgroundBuffer(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (popup_image)
    popup_image->Release();
  popup_image = incoming;
}

// Create the enabled missing vertical child, preserving independent borrowed-window queries and ignored attachment replies.
// Retain the original unused horizontal Boolean and return true on every defined path.
bool WnList::AddScrollBar(bool vertical, bool) {
  if (vertical && byte256 && !ChildExists(kWnListVerticalScrollbarId)) {
    WnScrollbarControl *scrollbar = WnGetUiResources()->CreateScrollbar(kWnListVerticalScrollbarId, 2, 0, 0, 0);
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
void WnList::RemoveScrollBar(bool vertical, bool horizontal) {
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

// Fit the current height to complete signed rows when row height is nonzero, preserving the source's captured quotient.
// Scrollbar sizing/synchronization and invalidation still run when the existing height already fits exactly.
bool WnList::ResizeWindowForExactLineHeights() {
  if (!row_height)
    return false;
  i32 height = GetH(), row = row_height, gutters = 2 * (i32)gutter255;
  i32 inner = (i32)((u32)height - (u32)gutters), rows = inner / row;
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
bool WnList::ResizeBoxToFitContents() {
  if (!popup)
    return false;
  i32 count = GetItemCount();
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
bool WnList::ResizeBoxToFitContentsHorizontal() {
  if (!popup)
    return false;
  byte225 = 1;
  u32 width = 1;
  if (strings.head->next != strings.head) {
    if (!font)
      SetFont(WnGetUiResources()->GetFont(kWnFontDefault));
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
bool WnList::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnResources *resources = WnGetUiResources();
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
  ScLanguageManager *manager = WnGetLanguageManager();
  language = manager ? manager->GetCurrentLanguage() : 1;
  SetScrollBarSizes();
  SetScrollBarValue();
  CalculateAllMetrics();
  return WnRootWidget::Init();
}

// Apply the Boolean plain-outline setting and disable the mutually exclusive line outline before invalidating.
// The original setter returns void after notifying the window and its parents.
void WnList::SetOutlineDrawing(bool enabled) {
  outline = enabled;
  line_outline = 0;
  InvalidateSelfAndParents();
}

// Store the outer horizontal and vertical gutters, then recompute rows before invalidating.
bool WnList::SetScrollbarGutters(i8 horizontal, i8 vertical) {
  gutter252 = horizontal;
  gutter253 = vertical;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

// Store the text gutters and immediately recalculate the visible row count before invalidating.
bool WnList::SetTextGutters(i8 horizontal, i8 vertical) {
  gutter254 = horizontal;
  gutter255 = vertical;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

// Normalize the insertion position and retain selection identity as preceding items shift.
// Popup sizing runs before the live scrollbar creation or synchronization decision.
bool WnList::InsertString(const ScString &text, i32 index) {
  i32 count = GetItemCount(), position = index;
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
  bool synchronize = GetScrollbarV() != nullptr;
  if (!synchronize) {
    i32 live_count = GetItemCount();
    synchronize = live_count <= visible_rows;
  }
  if (synchronize) {
    SetScrollBarSizes();
    SetScrollBarValue();
  } else
    AddScrollBar(1, 0);
  InvalidateSelfAndParents();
  return true;
}

// Find the concrete string's first equal item, then remove it through the live indexed control operation.
bool WnList::RemoveString(const ScString &text) {
  i32 index;
  if (!GetIndexFromString(&index, text))
    return false;
  return RemoveString(index);
}

bool WnList::RemoveString(const ScStringView &text) {
  ScString value(text);
  return RemoveString(value);
}

bool WnList::InsertString(const ScStringView &text, i32 index) {
  ScString value(text);
  return InsertString(value, index);
}

// Replace the clamped existing item's text through its actual string interface, then resize an automatic popup.
// An empty list or failed concrete lookup returns false without invalidation.
bool WnList::ModifyString(const ScStringView &text, i32 index) {
  i32 count = GetItemCount(), position = index;
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

// Erase an index no greater than the captured last item, then adjust the live viewport and selected index.
// Preserve negative-index behavior and ignore the raw erase reply before popup and scrollbar updates.
bool WnList::RemoveString(i32 index) {
  i32 last = (i32)((u32)GetItemCount() - 1);
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
  bool remove_scrollbar = false;
  if (GetScrollbarV()) {
    i32 live_count = GetItemCount();
    remove_scrollbar = live_count <= visible_rows;
  }
  if (remove_scrollbar) {
    RemoveScrollBar(1, 0);
    SetIndexOfTopLine(0);
  } else {
    SetScrollBarSizes();
    SetScrollBarValue();
  }
  InvalidateSelfAndParents();
  return true;
}

// Request the first viewport only when the list has items. Empty lists return false without moving the viewport.
bool WnList::ScrollToStart() {
  if (GetItemCount() <= 0)
    return false;
  SetIndexOfTopLine(0);
  return true;
}

// Move the final item into the last visible row, using zero when the entire list fits.
bool WnList::ScrollToEnd() {
  i32 count = GetItemCount();
  if (count > 0) {
    i32 rows = visible_rows;
    if (rows < count)
      SetIndexOfTopLine((i32)((u32)count - (u32)rows));
    else
      SetIndexOfTopLine(0);
  }
  return true;
}

// Preserve equal-entry order while the C runtime sorts actual nodes for relinking.
struct WnSortEntry {
  WnListNode *node;
  u32 position;
};

// Compare raw byte prefixes and lengths as in the source list comparator, then preserve the original order of equal
// text.
static int WnCompareSortEntries(const void *first, const void *second) {
  const WnSortEntry *left = static_cast<const WnSortEntry *>(first), *right = static_cast<const WnSortEntry *>(second);
  const ScString *left_text = &left->node->text, *right_text = &right->node->text;
  u32 left_length = left_text->begin ? (u32)(left_text->end - left_text->begin) : 0;
  u32 right_length = right_text->begin ? (u32)(right_text->end - right_text->begin) : 0;
  u32 common = left_length < right_length ? left_length : right_length;
  int order = common ? memcmp(left_text->begin, right_text->begin, common) : 0;
  if (order)
    return order;
  if (left_length != right_length)
    return left_length < right_length ? -1 : 1;
  return left->position < right->position ? -1 : left->position > right->position;
}

// Enable sorting before dispatching the live sort operation on the first request.
// Warm calls leave the existing order untouched, and the original method returns no value.
void WnList::SetShouldSort() {
  if (!byte220) {
    byte220 = 1;
    Sort();
  }
}

// Sort bytewise while preserving equal-string order, then reselect the first matching text through the original virtuals.
// Temporary selected-text ownership ends before this void method returns.
void WnList::Sort() {
  ScString selected;
  if (selection >= 0)
    GetStringFromIndex(selection, &selected);
  u32 count = strings.Count();
  if (count > 1) {
    WnSortEntry *entries = new WnSortEntry[count];
    WnListLink *link = strings.head->next;
    for (u32 i = 0; i < count; ++i) {
      entries[i] = {static_cast<WnListNode *>(link), i};
      link = link->next;
    }
    qsort(entries, count, sizeof(*entries), WnCompareSortEntries);
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
    // A missing unpublished match is an undefined source read; keep the native local initialized.
    i32 index = 0;
    GetIndexFromString(&index, selected);
    SetSelection(index, 1);
  }
}

// Destroy all owned strings before clearing selection and origin, then recompute rows and remove both scrollbar children.
// Finish by invalidating the list and its parents through the original live window callback.
void WnList::RemoveAllStrings() {
  strings.Clear();
  selection = -1;
  first_visible = 0;
  CalculateAllMetrics();
  RemoveScrollBar(1, 1);
  InvalidateSelfAndParents();
}

// Retain the new row image before releasing the live old field and publishing the replacement.
// This void setter performs no row recalculation or invalidation.
void WnList::SetLineItemBackgroundBuffer(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (row_image)
    row_image->Release();
  row_image = incoming;
}

bool WnList::ClearFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

// Normalize the actual area to local coordinates before shrinking by signed text gutters and the live scrollbar width.
// The original const method publishes its rectangle through the caller's mutable output pointer.
void WnList::GetTextArea(ScRect *rectangle) const {
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
  if (WnScrollbarControl *scrollbar = const_cast<WnList *>(this)->GetScrollbarV()) {
    i32 width = scrollbar->GetWindow()->GetW();
    rectangle->right = (i32)((u32)rectangle->right - (u32)width - (u32)(i32)gutter252);
  }
}


WnTextList::WnTextList()
    : language(0), background_opacity(1.0f), background_color(kWnDefaultColor),
      text_colors{kWnDefaultColor, kWnDefaultColor, kWnDefaultColor}, border_colors{kWnDefaultColor, kWnDefaultColor},
      rows(new WnTextRow), wrapped_rows(new WnTextRow), fonts{}, selection(-1), visible_rows(0), first_visible(0),
      row_height(10), first_width(-1), alternate_alignment(0), fill_background(0), rectangle_outline(0),
      line_outline(0), outline_colors{kWnDefaultColor, kWnDefaultColor}, gutters{3, 0, 3, 1}, gap(5), last_click(-1),
      double_click(0), allow_scrollbar(1), state262(0) {
  rows->next = rows->previous = rows;
  wrapped_rows->next = wrapped_rows->previous = wrapped_rows;
  WnResources *provider = WnGetUiResources();
  if (provider) {
    outline_colors[0] = provider->GetColor(19);
    outline_colors[1] = provider->GetColor(20);
  }
  WnRootWidget::SetFillColor(provider->GetColor(kWnColorUiBackground));
}

// Release retained fonts before destroying wrapped rows, source rows and their sentinels, then enter the window base.
WnTextList::~WnTextList() {
  Shutdown();
  ClearRows(wrapped_rows);
  delete wrapped_rows;
  ClearRows(rows);
  delete rows;
}

bool WnTextList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnTextListControl, WnTextListControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnTextList::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnTextList::Release() {
  return WnRootWidget::Release();
}

// Borrow the embedded window without an interface query or reference operation.
WnWindow *WnTextList::GetWindow() {
  return this;
}

// Fill only missing fonts and sentinel theme colors before selecting language, entering base startup and wrapping rows.
bool WnTextList::Init() {
  if (!GetFlag(kWnFlagInitialized)) {
    WnResources *provider = WnGetUiResources();
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
    ScLanguageManager *language_manager = WnGetLanguageManager();
    language = language_manager ? language_manager->GetCurrentLanguage() : 1;
    WnRootWidget::Init();
    CalculateAllMetrics();
  }
  return true;
}

// Store a changed full-width selection-background pixel before invalidating the window and its parents.
// Unchanged values have no side effects, and the original method returns no value.
void WnTextList::SetHighlightColor(u32 pixel) {
  if (background_color != pixel) {
    background_color = pixel;
    InvalidateSelfAndParents();
  }
}

// Replace both retained column fonts in order through the original void setter.
// Each incoming retain precedes release of that column's live previous font and publication of the new pointer.
void WnTextList::SetFonts(FtFont *first, FtFont *second) {
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
void WnTextList::SetHighlightFillAlpha(f32 opacity) {
  if (opacity < background_opacity || opacity > background_opacity) {
    if (opacity > 1.0f)
      opacity = 1.0f;
    else if (opacity < 0.0f)
      opacity = 0.0f;
    background_opacity = opacity;
    InvalidateSelfAndParents();
  }
}

void WnTextList::SetRightColumnTextStart(i32 width) {
  first_width = width;
  CalculateAllMetrics();
}

void WnTextList::SetJustification(bool value) {
  alternate_alignment = value;
}

void WnTextList::SetOpaque(bool value) {
  fill_background = value;
}

// Select rectangle outlining and clear the mutually exclusive sunken outline before parent-chain invalidation.
// The original Boolean setter returns no value.
void WnTextList::SetOutlineDrawing(bool value) {
  line_outline = 0;
  rectangle_outline = value;
  InvalidateSelfAndParents();
}

// Select the sunken outline and clear the mutually exclusive rectangle outline before parent-chain invalidation.
// The original Boolean setter returns no value.
void WnTextList::SetDrawSunkenRect(bool value) {
  rectangle_outline = 0;
  line_outline = value;
  InvalidateSelfAndParents();
}

void WnTextList::SetSunkenRectColors(u32 first, u32 second) {
  outline_colors[0] = first;
  outline_colors[1] = second;
}

// Store both original Boolean scrollbar options and return true without creating or deleting a child.
bool WnTextList::EnableScrollBar(bool first, bool second) {
  allow_scrollbar = first;
  state262 = second;
  return true;
}

// Snap the live height down to a whole number of text rows.
// Position and refresh the scrollbar before invalidation.
bool WnTextList::ResizeWindowForExactLineHeights() {
  if (!row_height)
    return false;
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
  return true;
}

// Release and clear the first font before reading the live second font, then close the inherited root window.
bool WnTextList::Shutdown() {
  if (fonts[0]) {
    fonts[0]->Release();
    fonts[0] = nullptr;
  }
  if (fonts[1]) {
    fonts[1]->Release();
    fonts[1] = nullptr;
  }
  return WnRootWidget::Shutdown();
}

// Update one of three font pixels and invalidate only when its full DWORD value changes.
bool WnTextList::SetFontColor(i32 index, u32 pixel) {
  if (index < 0 || index >= 3)
    return false;
  if (text_colors[index] != pixel) {
    text_colors[index] = pixel;
    InvalidateSelfAndParents();
  }
  return true;
}

// Write the selected font pixel through the genuine output pointer; invalid indices leave it untouched.
bool WnTextList::GetFontColor(i32 index, u32 *pixel) {
  if (index < 0 || index >= 3)
    return false;
  *pixel = text_colors[index];
  return true;
}

bool WnTextList::ResetFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

// Copy the second input before the first and insert their concrete values.
// Retire both copies before invalidating and relayout only an initialized list.
bool WnTextList::AddRow(
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

bool WnTextList::AddRow(const ScStringView &first, const ScStringView &second, i32 index) {
  return AddRow(first, second, kWnDefaultColor, kWnDefaultColor, index);
}

// Index zero prepends; negative or out-of-range nonzero indices append. New rows copy both concrete strings.
bool WnTextList::InsertStringPairAtIndex(
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
  row->first = first;
  row->second = second;
  row->colors[0] = first_color;
  row->colors[1] = second_color;
  row->tag = 0;
  row->next = position;
  row->previous = position->previous;
  position->previous->next = row;
  position->previous = row;
  return true;
}

// Find a live source row by signed index; the original searches return no row for negative or past-end indices.
WnTextList::WnTextRow *WnTextList::FindRow(i32 index) {
  i32 current = 0;
  for (WnTextRow *row = rows->next; row != rows; row = row->next, ++current)
    if (current == index)
      return row;
  return nullptr;
}

// Copy each concrete row's bytes into the caller's actual string outputs, preserving first-before-second dispatch.
bool WnTextList::GetRowText(ScStringView *first, ScStringView *second, i32 index) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  first->FromChar(row->first.begin, row->first.ScString::Strlen());
  second->FromChar(row->second.begin, row->second.ScString::Strlen());
  return true;
}

// Unlink and destroy one source row before relayout and invalidation; an absent index leaves both lists intact.
bool WnTextList::RemoveRow(i32 index) {
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

// Destroy all actual rows while preserving the circular sentinel and its identity.
void WnTextList::ClearRows(WnTextRow *head) {
  WnTextRow *row = head->next;
  while (row != head) {
    WnTextRow *next = row->next;
    delete row;
    row = next;
  }
  head->next = head->previous = head;
}

bool WnTextList::RemoveAllRows() {
  ClearRows(rows);
  ClearRows(wrapped_rows);
  InvalidateSelfAndParents();
  return true;
}

// Copy into the selected concrete column before retiring the temporary, then relayout and invalidate.
bool WnTextList::SetRowText(const ScStringView &input, i32 index, bool first) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  {
    ScString copy(input);
    (first ? row->first : row->second) = copy;
  }
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

// Store the full pixel in the selected row column before requesting layout and invalidation.
bool WnTextList::SetRowColor(u32 pixel, i32 index, bool first) {
  WnTextRow *row = FindRow(index);
  if (!row)
    return false;
  row->colors[first ? 0 : 1] = pixel;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

bool WnTextList::IsItemVisible(i32 index) {
  return index >= first_visible && index < (i32)((u32)first_visible + visible_rows);
}

// Count wrapped display rows rather than source rows; wrapping may give the two lists different lengths.
i32 WnTextList::GetItemCount() const {
  u32 count = 0;
  for (WnTextRow *row = wrapped_rows->next; row != wrapped_rows; row = row->next)
    ++count;
  return (i32)count;
}

i32 WnTextList::GetFirstVisible() const {
  return first_visible;
}

void WnTextList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  CalculateAllMetrics();
}

// Borrow the vertical scrollbar after retiring the successful child-query acquisition; failed outputs are ignored.
WnScrollbarControl *WnTextList::GetVerticalScrollbar() {
  void *value = nullptr;
  if (!GetChildAs(kWnTextListVerticalScrollbarId, IID_WnScrollbarControl, &value))
    return nullptr;
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(value);
  scrollbar->Release();
  return static_cast<WnScrollbarControl *>(value);
}

// Borrow the horizontal scrollbar through its distinct child ID and release only a successful interface acquisition.
WnScrollbarControl *WnTextList::GetHorizontalScrollbar() {
  void *value = nullptr;
  if (!GetChildAs(kWnTextListHorizontalScrollbarId, IID_WnScrollbarControl, &value))
    return nullptr;
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(value);
  scrollbar->Release();
  return static_cast<WnScrollbarControl *>(value);
}

// Propagate exactly flag2 through the independently borrowed scrollbar windows before mutating the copied root.
u32 WnTextList::SetFlag(u32 mask, bool enabled) {
  if (mask == kWnFlagEnabled) {
    WnScrollbarControl *scrollbar = GetVerticalScrollbar();
    if (scrollbar)
      scrollbar->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
    scrollbar = GetHorizontalScrollbar();
    if (scrollbar)
      scrollbar->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
  }
  return WnRootWidget::SetFlag(mask, enabled);
}

// Append a complete native display row only after copying both actual strings and their separate pixels.
WnTextList::WnTextRow *WnTextList::AppendWrappedRow(
    const ScString &first, const ScString &second, u32 first_color, u32 second_color) {
  WnTextRow *row = new WnTextRow;
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

// Recalculate the original wrapped-row metrics through the protected void method.
// Rebuild both columns and align their trailing lines, preserving captured spacing and the direct final row count.
void WnTextList::CalculateAllMetrics() {
  ScString first_value, second_value;
  u32 first_color = 0, second_color = 0;
  if (!fonts[0]) {
    fonts[0] = WnGetUiResources()->GetFont(kWnFontDefault);
    fonts[0]->AddRef();
  }
  if (!fonts[1]) {
    fonts[1] = WnGetUiResources()->GetFont(kWnFontDefault);
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
    ScString first_remaining(source->first);
    u32 fitted = fonts[0]->WrapFit(first_remaining.begin, first_remaining.ScString::Strlen(), (u32)first_space, 1);
    WnTextRow *line_cursor = wrapped_rows->previous;
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
  if (scrollbar_present || WnTextList::GetItemCount() <= visible_rows) {
    SetScrollbarSizes();
    SetScrollbarValue();
  } else {
    AddScrollbar(1, 0);
    CalculateAllMetrics();
  }
}

// Create the actual provider scrollbar only when allowed and absent, then attach, release, position and refresh it.
bool WnTextList::AddScrollbar(bool enabled, bool) {
  bool result = false;
  WnResources *provider = WnGetUiResources();
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
void WnTextList::RemoveScrollbar(bool enabled, bool) {
  if (enabled) {
    WnScrollbarControl *scrollbar = GetVerticalScrollbar();
    if (scrollbar)
      ChildDelete(scrollbar->GetWindow());
  }
}

// Refresh the scrollbar range, divisors and position through the original void method.
// Retain repeated live item-count queries and scrollbar acquisitions, including creation and removal.
void WnTextList::SetScrollbarValue() {
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

// Position the live scrollbar through the original void geometry method.
// Preserve height, width, child width and fresh parent width query ordering.
void WnTextList::SetScrollbarSizes() {
  WnScrollbarControl *scrollbar = GetVerticalScrollbar();
  WnWindow *window = scrollbar ? scrollbar->GetWindow() : nullptr;
  if (window) {
    i32 bottom = GetH(), right = GetW();
    i32 left = (i32)(1u - (u32)window->GetW());
    left = (i32)((u32)left + GetW());
    window->SetArea(left, 0, right, bottom);
  }
}

bool WnTextList::MoveLineUp() {
  PutIndexInView((i32)((u32)first_visible - 1));
  return true;
}

bool WnTextList::MoveLineDown() {
  PutIndexInView((i32)((u32)first_visible + 1));
  return true;
}

// Refuse an empty wrapped list; otherwise move its scrolling origin to zero.
bool WnTextList::ScrollToStart() {
  if (GetItemCount() <= 0)
    return false;
  SetIndexOfTopLine(0);
  return true;
}

// Move to the last full page, or zero when every wrapped row fits; empty lists still return true.
bool WnTextList::ScrollToEnd() {
  i32 count = GetItemCount();
  if (count > 0)
    SetIndexOfTopLine(visible_rows < count ? (i32)((u32)count - visible_rows) : 0);
  return true;
}

// Capture the old origin before querying count, then scroll one overlapping page upward and clamp only at zero.
bool WnTextList::ScrollPageUp() {
  i32 first = first_visible;
  if (GetItemCount() > 0) {
    i32 next = (i32)(1u - (u32)visible_rows + first);
    if (next < 0)
      next = 0;
    SetIndexOfTopLine(next);
  }
  return true;
}

// Preserve the captured old origin and the later live end-of-list test before scrolling one overlapping page down.
bool WnTextList::ScrollPageDown() {
  i32 first = first_visible, count = GetItemCount();
  if (count > 0) {
    i32 visible = visible_rows;
    if (first_visible < (i32)((u32)count - visible))
      SetIndexOfTopLine((i32)((u32)first + visible - 1));
  }
  return true;
}

// Expose the requested row through the live origin setter without introducing an item-range clamp.
// Refresh the scrollbar and invalidate after any nested origin change; the original method returns no value.
void WnTextList::PutIndexInView(i32 index) {
  if (index < first_visible)
    SetIndexOfTopLine(index);
  else if (index > (i32)((u32)visible_rows + first_visible - 1))
    SetIndexOfTopLine((i32)((u32)index - visible_rows + 1));
  SetScrollbarValue();
  InvalidateSelfAndParents();
}

// Publish the first row, clamp through repeated live counts, then refresh the scrollbar and invalidate.
// The original viewport setter returns no value.
void WnTextList::SetIndexOfTopLine(i32 index) {
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
bool WnTextList::SetSelection(i32 index, bool notify) {
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
      SendSelectionChangedMessage();
    InvalidateSelfAndParents();
  }
  return true;
}

i32 WnTextList::GetSelection() const {
  return selection;
}

// Convert the vertical point through the live row height and origin, retaining the original Boolean edge clamp.
u32 WnTextList::GetItemIndexFromCursorPosition(i32, i32 y, bool clamp) const {
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

bool WnTextList::GZCharacter(u32) {
  return owner->GetCapture() != static_cast<WnWindow *>(this);
}

// Capture the live window ID before delivering the original synchronous selection notification and ignore its reply.
bool WnTextList::SendSelectionChangedMessage() {
  u32 window_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListSelectionChanged, window_id, 0);
  return true;
}

// Capture the live window ID before the distinct activation notification and return true independently of its reply.
bool WnTextList::SendDoubleClickMessage() {
  u32 window_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClicked, window_id, 0);
  return true;
}

// Ignore keys while captured; Enter activates, paging/arrows dispatch live scrolling and other keys reach the target.
bool WnTextList::OnKeyDown(u32 key, u32 modifiers) {
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

// Resolve and select the clicked row before updating the shared elapsed timer and per-window double-click state.
bool WnTextList::OnMouseDownL(i32 x, i32 y, u32) {
  static ScStopwatch timer(ScStopwatch::Milliseconds, 0);
  static u32 last_time = 0;
  i32 index = (i32)GetItemIndexFromCursorPosition(x, y, true);
  owner->SetCapture(this);
  if (index < GetItemCount() && index != GetSelection())
    SetSelection(index, 1);
  if (last_time) {
    if (index == last_click) {
      u32 elapsed = timer.GetElapsedTime();
      elapsed -= last_time;
      u32 threshold = WnDoubleClickTime();
      if (elapsed < threshold)
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

bool WnTextList::OnMouseDownR(i32 x, i32 y, u32 modifiers) {
  return OnMouseDownL(x, y, modifiers);
}

// Forward captured scrollbar releases in child coordinates; otherwise release capture and deliver pending activation.
bool WnTextList::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  if (owner->GetCapture() == static_cast<WnWindow *>(this)) {
    if (IsPointInWindow(x, y)) {
      WnScrollbarControl *scrollbar = GetVerticalScrollbar();
      if (scrollbar) {
        WnWindow *window = scrollbar->GetWindow();
        i32 child_x = x, child_y = y;
        WindowToWindowCoordinates(window, &child_x, &child_y);
        bool inside = false;
        if (child_x >= 0 && child_y >= 0) {
          i32 width = window->GetW();
          if (child_x < width) {
            i32 height = window->GetH();
            inside = child_y < height;
          }
        }
        if (inside) {
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

bool WnTextList::OnMouseUpR(i32 x, i32 y, u32 modifiers) {
  return OnMouseDownR(x, y, modifiers);
}

// Draw the live wrapped rows and selection, preserving independent buffer locks and the source second-column formula.
// Scrollbar acquisition precedes background fill; optional outlines follow every visible row.
bool WnTextList::GZPaint() {
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

// Construct the two-column list and return its displaced root window at zero references.
// The registration uses the window identity rather than the leading list-control interface.
ScUnknown *WnConstructTextList() {
  return static_cast<WnWindow *>(new WnTextList);
}

bool WnTextList::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}


WnTickerQueue::WnTickerQueue() : head(new WnTickerNode) {
  head->next = head;
  head->previous = head;
}

// Release queue nodes without deleting pointed items; ticker shutdown controls their separate destruction order.
WnTickerQueue::~WnTickerQueue() {
  while (head->next != head)
    Erase(head->next);
  delete head;
}

// Unlink the captured queue node before freeing its storage, without invoking any item destructor.
void WnTickerQueue::Erase(WnTickerNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

// Retain the font before the string, then initialize the item's entrance bounds and borrowed callback data.
WnTickerItem::WnTickerItem(u32 incoming_id, ScStringView *incoming_text, FtFont *incoming_font,
    u32 incoming_color, u32 incoming_end_color, u32 incoming_kind, u32 incoming_timestamp, u32 incoming_times_left,
    const ScMessage *incoming_message, WnTickerCallback incoming_callback, void *incoming_context) {
  font = incoming_font;
  if (font)
    font->AddRef();
  text = incoming_text;
  if (text)
    text->AddRef();
  span.left = 65534;
  span.top = 0;
  span.bottom = 0;
  identifier = incoming_id;
  kind = incoming_kind;
  color = incoming_color;
  end_color = incoming_end_color;
  timestamp = incoming_timestamp;
  times_left = incoming_times_left;
  callback = incoming_callback;
  context = incoming_context;
  span.right = 65535;
  if (incoming_message)
    message = *incoming_message;
  else
    message = {};
}

// Copy the original ticker item while retaining its text and font in the executable's callback order.
WnTickerItem::WnTickerItem(const WnTickerItem &source) {
  identifier = source.identifier;
  kind = source.kind;
  text = source.text;
  if (text)
    text->AddRef();
  font = source.font;
  if (font)
    font->AddRef();
  span = source.span;
  color = source.color;
  end_color = source.end_color;
  timestamp = source.timestamp;
  times_left = source.times_left;
  message = source.message;
  callback = source.callback;
  context = source.context;
}

WnTickerItem::~WnTickerItem() {
  ScRelease(&text);
  ScRelease(&font);
}

// Construct the root and shared millisecond counter before both queues, then ask the actual graphics service for a
// buffer.
WnTicker::WnTicker()
    : timer(ScStopwatch::Milliseconds, 0), phase(0), buffer(nullptr), current_kind(0), language(0),
      next_character(nullptr), next_id(0), font(nullptr), active_font(nullptr), current_color(0), current_end_color(0),
      step(2), interval(50) {
  WnGetGraphics()->CreateBuffer(&buffer);
}

WnTicker::~WnTicker() {
  WnTicker::Shutdown();
}

// Publish the true leading control identity for its own IID and otherwise delegate to the actual displaced root view.
bool WnTicker::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnTickerControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnTickerControl *>(this);
  AddRef();
  return true;
}

u32 WnTicker::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnTicker::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed root window from the leading ticker control without acquiring another reference.
WnWindow *WnTicker::GetWindow() {
  return static_cast<WnWindow *>(this);
}

void WnTicker::SetCaption(const ScStringView &) {
}

// Retain incoming font before releasing the old field, then publish it without relayout or invalidation.
bool WnTicker::SetFont(FtFont *incoming) {
  if (incoming)
    incoming->AddRef();
  if (font)
    font->Release();
  font = incoming;
  return true;
}

void WnTicker::SetSpeed(u32 speed) {
  step = speed;
}

void WnTicker::SetFrequency(u32 milliseconds) {
  interval = milliseconds;
}

// Size the backing buffer from the font-adjusted drawing area, then select the cursor and language before root setup.
bool WnTicker::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  phase = 0;
  timer.Restart();
  SetH(font->Height(0));
  u32 height = (u32)draw_area.bottom - (u32)draw_area.top;
  u32 width = (u32)draw_area.right - (u32)draw_area.left;
  if (!buffer->Init(width, height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false))
    return false;
  u32 transparency = WnGetUiResources()->GetColor(kWnColorUiTransparency);
  buffer->SetTransparency(transparency);
  if (buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    buffer->Fill(pixel, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  item_cursor = active.head;
  WnCursorManager *manager = owner->GetCursorManager();
  WnCursor *selected = manager->GetIGZCursor(17);
  SetCursor(selected, false);
  ScLanguageManager *languages = WnGetLanguageManager();
  language = languages ? languages->GetCurrentLanguage() : 1;
  return WnRootWidget::Init();
}

// Create a retained text item with a client callback and append it to the pending queue.
// A missing base font rejects the request before allocating an item or advancing the identifier.
u32 WnTicker::AddMessage(
    ScStringView *text, u32 color, u32 end_color, u32 kind, WnTickerCallback callback, void *context) {
  if (!font)
    return 0;
  FtFont *selected_font = font;
  u32 id = GetNewMessageID();
  auto *item = new WnTickerItem(id, text, selected_font, color, end_color, kind, 0, 0, nullptr, callback, context);
  pending.Append(item);
  return item->identifier;
}

// Copy the optional queued message into a new retained item before appending it to the pending queue.
// Return the created identifier after insertion, preserving unsigned identifier wrap.
u32 WnTicker::AddMessage(ScStringView *text, u32 color, u32 end_color, u32 kind, const ScMessage *message) {
  if (!font)
    return 0;
  FtFont *selected_font = font;
  u32 id = GetNewMessageID();
  auto *item = new WnTickerItem(id, text, selected_font, color, end_color, kind, 0, 0, message, nullptr, nullptr);
  pending.Append(item);
  return item->identifier;
}

bool WnTicker::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnTicker::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Notify the first active item under the horizontal coordinate, then post its live message if nonempty.
// Callback changes to the queued message are observed before the normal-priority post; every click succeeds.
bool WnTicker::OnMouseDownL(i32 x, i32, u32) {
  WnTickerItem *item = FindClickedMessage(x);
  if (item) {
    if (item->callback)
      item->callback(item->identifier, item->context);
    if (item->message.type)
      WnGetMessageServer()->MessagePost(item->message, false);
  }
  return true;
}

// Search only the pending queue, unlink its matching node before the captured item is virtually destroyed.
bool WnTicker::RemoveItem(u32 requested) {
  for (WnTickerNode *node = pending.head->next; node != pending.head; node = node->next) {
    WnTickerItem *item = node->item;
    if (item->identifier == requested) {
      pending.Erase(node);
      if (item)
        delete item;
      return true;
    }
  }
  return false;
}

// Destroy pending items before active items, then fill the owned buffer when its lock succeeds.
// Buffer lock and drawing failures do not change the original successful clear result.
bool WnTicker::Clear() {
  while (pending.head->next != pending.head) {
    WnTickerItem *item = pending.head->next->item;
    if (item)
      delete item;
    pending.Erase(pending.head->next);
  }
  while (active.head->next != active.head) {
    WnTickerItem *item = active.head->next->item;
    if (item)
      delete item;
    active.Erase(active.head->next);
  }
  if (buffer && buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    buffer->Fill(pixel, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  return true;
}

// Invoke live clear first, drain any remaining active entries, then release the live font and buffer before root
// shutdown.
bool WnTicker::Shutdown() {
  Clear();
  while (active.head->next != active.head) {
    WnTickerItem *item = active.head->next->item;
    if (item)
      delete item;
    active.Erase(active.head->next);
  }
  ScRelease(&font);
  ScRelease(&buffer);
  return WnRootWidget::Shutdown();
}

u32 WnTicker::GetNewMessageID() {
  return ++next_id;
}

// Return the first active item whose signed horizontal interval contains the coordinate; pending items are excluded.
WnTickerItem *WnTicker::FindClickedMessage(i32 x) {
  for (WnTickerNode *node = active.head->next; node != active.head; node = node->next) {
    WnTickerItem *item = node->item;
    if (x >= item->span.left && x < item->span.right)
      return item;
  }
  return nullptr;
}

// Return the actual leading ticker control at zero references, preserving its displaced root-window identity.
ScUnknown *WnConstructTicker() {
  return static_cast<WnTickerControl *>(new WnTicker);
}

static i32 WnTickerAdd(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

// Allocate only the game's node and link it at the queue tail; the receiver takes no additional item reference.
void WnTickerQueue::Append(WnTickerItem *item) {
  auto *node = new WnTickerNode{head, head->previous, item};
  node->previous->next = node;
  head->previous = node;
}

// Position a transferred item using the base font before appending it, then seed current drawing fields only if absent.
// Null input leaves the queue untouched; the concrete virtual retains the original item ownership.
void WnTicker::AddMessageInternal(WnTickerItem *item) {
  if (!item)
    return;
  item->span.left = WnTickerAdd(GetW(), 20);
  ScStringView *text = item->text;
  u32 length = text->Strlen();
  const char *bytes = text->Data();
  i32 width = font->Measure(bytes, length);
  item->span.right = WnTickerAdd(WnTickerAdd(width, GetW()), 20);
  active.Append(item);
  if (!active_font) {
    active_font = item->font;
    current_kind = item->kind;
    current_color = item->color;
    current_end_color = item->end_color;
  }
}

// Remove one pending front node before dispatching its item through the live active-item virtual.
// Empty input has no effect; ownership of a transferred item follows that virtual rather than another retain/release.
void WnTicker::GetNextMessage() {
  if (pending.head->next != pending.head) {
    WnTickerItem *item = pending.head->next->item;
    pending.Erase(pending.head->next);
    AddMessageInternal(item);
  }
}

// Transfer pending input first, then select the last active item and reset its entrance geometry and phase.
// Empty active input clears character traversal without reading the sentinel's unwritten item payload.
void WnTicker::AdvanceMessage() {
  GetNextMessage();
  if (active.head->next == active.head) {
    next_character = nullptr;
    item_cursor = active.head->next;
    return;
  }
  item_cursor = active.head->previous;
  WnTickerItem *item = item_cursor->item;
  active_font = item->font;
  current_kind = item->kind;
  current_color = item->color;
  current_end_color = item->end_color;
  next_character = item->text->Data();
  item->span.left = WnTickerAdd(GetW(), 20);
  ScStringView *text = item->text;
  u32 length = text->Strlen();
  const char *bytes = text->Data();
  i32 width = active_font->Measure(bytes, length);
  item->span.right = WnTickerAdd(WnTickerAdd(GetW(), width), 20);
  phase = -20;
}

// Shift active intervals by the live step and delete negative-ended items after selecting away from a captured cursor.
// Capture the next node before callbacks, then reload the removed node's item for virtual destruction before unlinking.
void WnTicker::ShiftSpans() {
  WnTickerNode *head = active.head, *node = head->next;
  while (node != head) {
    WnTickerItem *item = node->item;
    item->span.left = (i32)((u32)item->span.left - step);
    item->span.right = (i32)((u32)item->span.right - step);
    if (item->span.right >= 0) {
      node = node->next;
    } else {
      WnTickerNode *removed = node;
      node = node->next;
      if (item_cursor == removed)
        AdvanceMessage();
      item = removed->item;
      if (item)
        delete item;
      active.Erase(removed);
    }
  }
}

static i32 WnTickerSubtract(i32 first, u32 second) {
  return (i32)((u32)first - second);
}

// Execute the ticker's scroll-and-character phase through its actual buffer, queues and fonts.
// Consume complete UTF-8 scalars while retaining the source language-specific failed-lock consumption policy.
void WnTicker::DoDraw() {
  i32 width = buffer->Width(), height = buffer->Height();
  ScRect source{(i32)step, 0, width, height}, destination{0, 0, WnTickerSubtract(width, step), height};
  if (buffer->Lock(GxLockFlags::Blt)) {
    buffer->Blt(buffer, source, destination, nullptr);
    buffer->Unlock(GxLockFlags::Blt);
  }
  if (buffer->Lock(GxLockFlags::Fill)) {
    ScRect strip{WnTickerSubtract(width, step), 0, width, height};
    u32 pixel = GetFillColor();
    buffer->Fill(pixel, strip, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  phase = (i32)((u32)phase + step);
  ShiftSpans();
  if (active.head->next == active.head)
    AdvanceMessage();
  if (!next_character) {
    AdvanceMessage();
    return;
  }
  WnTickerItem *item = item_cursor->item;
  ScStringView *text = item->text;
  u32 length = text->Strlen();
  const char *bytes = text->Data();
  if ((uptr)next_character >= (uptr)bytes + length) {
    AdvanceMessage();
    return;
  }
  const GxBltFlags *color = buffer->GetBltFlags();
  if (item->end_color != color->pixel && buffer->Lock(GxLockFlags::Fill)) {
    ScRect strip{WnTickerSubtract(width, phase), 0, width, height};
    buffer->Fill(item->end_color, strip, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  if ((current_kind & 1) && buffer->Lock(GxLockFlags::Line)) {
    i32 bottom = WnTickerSubtract(height, 1);
    buffer->DrawLine(WnTickerSubtract(width, phase), bottom, width, bottom, static_cast<i32>(current_color), 1);
    buffer->Unlock(GxLockFlags::Line);
  }
  bool grouped_language = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageThai || language == kScLanguageKorean;
  if (grouped_language)
    active_font->Color();
  u32 scalar, count = ScUtf8Decode(next_character, static_cast<u32>(bytes + length - next_character), &scalar);
  assert(count);
  if (phase > 0) {
    bool drawn = active_font->BeginDraw(buffer) != 0;
    if (drawn) {
      active_font->DrawTextA(
          buffer, (u32)WnTickerSubtract(width, phase), 0, next_character, count, current_color, current_end_color);
      active_font->EndDraw(buffer);
    }
    if (drawn || !grouped_language) {
      i32 advance = font->Advance(scalar);
      if (phase >= advance) {
        next_character += count;
        phase = WnTickerSubtract(phase, advance);
      }
    }
  }
}

// Advance only after elapsed time strictly exceeds the interval, then invalidate before copying the live backing buffer.
// A failed drawing lock skips the copy while preserving the original successful paint result.
bool WnTicker::GZPaint() {
  if (timer.GetElapsedTime() > interval) {
    timer.Restart();
    DoDraw();
    InvalidateSelf();
  }
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    const ScRect *source = buffer->GetBufferArea();
    draw_buffer->Blt(buffer, *(source), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}
