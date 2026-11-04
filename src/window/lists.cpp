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

WnCombo::WnCombo(u8 incoming)
    : selection(-1), drop_open(0), font(nullptr), font_colors{kWnDefaultColor, kWnDefaultColor, kWnDefaultColor},
      highlight_color(kWnDefaultColor), highlight_opacity(1.0f), dropdown_outline(0), editable(incoming),
      arrow(nullptr), arrow_x(0), arrow_y(0), arrow_width(16), arrow_height(8), arrow_left(0), arrow_top(0),
      arrow_source_x(0), arrow_color(kWnDefaultColor), button_gutter(1), horizontal_gutter(3), vertical_gutter(0) {
  SetFillColor(WnGetUiResources()->GetColor(21));
}

WnCombo::~WnCombo() {
  WnCombo::Shutdown();
}

bool WnCombo::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnComboControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnComboControl *>(this);
  AddRef();
  return true;
}

u32 WnCombo::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnCombo::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window from the secondary control. Its reference count stays unchanged.
WnWindow *WnCombo::GetWindow() {
  return static_cast<WnWindow *>(this);
}

// Obtain the embedded text through its real child query. Release the temporary hold before returning the borrowed view.
WnTextControl *WnCombo::LookupLineInput() {
  void *slot = nullptr;
  if (!GetChildAs(kWnComboInputId, IID_WnTextControl, &slot))
    return nullptr;
  static_cast<WnTextControl *>(slot)->Release();
  return static_cast<WnTextControl *>(slot);
}

// Return an existing borrowed input when available. Otherwise create it and retry the lookup only after success.
WnTextControl *WnCombo::GetLineInput() {
  WnTextControl *text = LookupLineInput();
  if (!text && CreateLineInput())
    text = LookupLineInput();
  return text;
}

// Release the old field before publishing and retaining the new font. Forward the live field to an existing input.
void WnCombo::SetFont(FtFont *incoming) {
  if (font)
    font->Release();
  font = incoming;
  if (incoming)
    incoming->AddRef();
  WnTextControl *text = LookupLineInput();
  if (text)
    text->SetFont(font);
}

// Accept only indices zero through two and store the pixel. On change, invalidate before forwarding to the input.
bool WnCombo::SetFontColor(i32 index, u32 color) {
  if ((u32)index > 2)
    return false;
  u32 old = font_colors[index];
  font_colors[index] = color;
  if (old != color) {
    InvalidateSelfAndParents();
    WnTextControl *text = LookupLineInput();
    if (text)
      text->SetFontColor((WnInputColor)index, color);
  }
  return true;
}

bool WnCombo::ClearFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

// Store the highlight pixel. Then forward it to an existing input's selection-color operation.
bool WnCombo::SetHighlightColor(u32 color) {
  highlight_color = color;
  WnTextControl *text = LookupLineInput();
  if (text)
    text->SetHighlightFillColor(color);
  return true;
}

// Map negative or unordered opacity to zero and clamp values above one. Forward the live stored value to the input.
bool WnCombo::SetHighlightFillAlpha(f32 opacity) {
  highlight_opacity = opacity;
  if (!(opacity >= 0.0f))
    highlight_opacity = 0.0f;
  if (highlight_opacity > 1.0f)
    highlight_opacity = 1.0f;
  WnTextControl *text = LookupLineInput();
  if (text)
    text->SetHighlightFillAlpha(highlight_opacity);
  return true;
}

u8 WnCombo::GetEditable() const {
  return editable;
}

// Forward the incoming byte when an input exists. Publish it locally after that callback returns.
bool WnCombo::SetEditable(u8 enabled) {
  WnTextControl *text = LookupLineInput();
  if (text)
    text->SetEditable(enabled);
  editable = enabled;
  return true;
}

bool WnCombo::SetDropDownOutlineColor(u32 color) {
  dropdown_outline = color;
  return true;
}

// Publish the arrow-button color. Always invalidate the parent chain and report success.
bool WnCombo::SetComboDownColor(u32 color) {
  arrow_color = color;
  InvalidateSelfAndParents();
  return true;
}

// Store the signed gutter. Capture height before width and invoke live layout, ignoring its result.
bool WnCombo::SetBtnGutter(i8 gutter) {
  button_gutter = gutter;
  i32 height = GetH(), width = GetW();
  Layout(width, height);
  return true;
}

// Store both signed gutters. For an existing input, set symmetric margins before resizing the combo through SetSize.
bool WnCombo::SetTextGutters(i8 horizontal, i8 vertical) {
  horizontal_gutter = horizontal;
  vertical_gutter = vertical;
  WnTextControl *text = LookupLineInput();
  if (text) {
    text->SetGutters(horizontal_gutter, vertical_gutter, horizontal_gutter, vertical_gutter);
    i32 height = GetH(), width = GetW();
    SetSize(width, height);
  }
  return true;
}

// Set upper bounds to INT_MAX.
bool WnCombo::GetMinMaxInfo(ScRect *limits) {
  limits->left = arrow_width;
  if (arrow) {
    limits->left = arrow_width;
    limits->top = arrow_height;
  } else {
    limits->left = 16;
    limits->top = 8;
  }
  WnTextControl *text = LookupLineInput();
  if (text) {
    ScRect extent;
    text->GetMinMaxInfo(&extent);
    limits->left = (i32)((u32)limits->left + (u32)extent.left + (u32)(i32)button_gutter);
    if (extent.top > limits->top)
      limits->top = extent.top;
  }
  limits->right = INT32_MAX;
  limits->bottom = INT32_MAX;
  return true;
}

// Position the arrow and size the existing input. Invalidate after the callbacks and return success.
bool WnCombo::Layout(i32 width, i32 height) {
  if (!arrow)
    arrow_height = height;
  arrow_left = (i32)((u32)width - (u32)arrow_width);
  arrow_top = (i32)((u32)height - (u32)arrow_height) / 2;
  WnTextControl *text = LookupLineInput();
  if (text)
    text->AsIGZWin()->SetSize((i32)((u32)arrow_left - (u32)(i32)button_gutter), height);
  InvalidateSelfAndParents();
  return true;
}

// Clamp requested dimensions to the source minima. Lay out changed sizes before moving the root; equal sizes keep requested edges.
void WnCombo::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  ScRect limits;
  GetMinMaxInfo(&limits);
  if (width < limits.left)
    width = limits.left;
  if (height < limits.top)
    height = limits.top;
  if (width == GetW() && height == GetH()) {
    WnRootWidget::SetArea(left, top, right, bottom);
  } else {
    Layout(width, height);
    WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  }
}

// Convert the pixel through the qualified root setter. Forward the original pixel to the existing input window.
void WnCombo::SetFillColor(u32 color) {
  WnRootWidget::SetFillColor(color);
  WnTextControl *text = LookupLineInput();
  if (text)
    text->AsIGZWin()->SetFillColor(color);
}

bool WnCombo::Shutdown() {
  HideWindow();
  ScRelease(&arrow);
  ScRelease(&font);
  return WnRootWidget::Shutdown();
}

bool WnCombo::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Block enabled-bit mutations while the dropdown is open. Exact enabled changes also update the input and arrow cell.
u32 WnCombo::SetFlag(u32 mask, bool enabled) {
  if ((mask & 2) && drop_open)
    return 0;
  u32 result = WnRootWidget::SetFlag(mask, enabled);
  if (mask == kWnFlagEnabled) {
    WnTextControl *text = LookupLineInput();
    text->AsIGZWin()->SetFlag(kWnFlagEnabled, enabled);
    arrow_source_x = enabled ? (i32)((u32)arrow_x + (u32)arrow_width) : arrow_x;
    InvalidateSelfAndParents();
  }
  return result;
}

// Construct an editable combo and return its primary identity at zero references for the registry's retained query.
ScUnknown *WnConstructCombo() {
  return static_cast<WnWindow *>(new WnCombo(1));
}

// Configure a registered input before attaching it to the root. Release the acquired interface after the attachment call.
bool WnCombo::CreateLineInput() {
  if (LookupLineInput())
    return true;
  WnResources *resources = WnGetUiResources();
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, &slot))
    return false;
  WnTextControl *text = static_cast<WnTextControl *>(slot);
  WnWindow *window = text->AsIGZWin();
  window->SetID(kWnComboInputId);
  window->GZWinMoveTo(0, 0);
  u32 color = GetFillColor();
  window->SetFillColor(color);
  text->SetFont(font);
  text->SetEditable(editable);
  text->SetGutters(horizontal_gutter, vertical_gutter, horizontal_gutter, vertical_gutter);
  if (resources) {
    text->SetOutlineType(WnInputOutline::ThreeD);
    u32 first = resources->GetColor(19);
    text->SetOutlineColor(WnInputEdge::LeftTop, first);
    u32 second = resources->GetColor(20);
    text->SetOutlineColor(WnInputEdge::RightBottom, second);
  } else {
    text->SetOutlineType(WnInputOutline::Normal);
    text->SetOutlineColor(0);
  }
  bool added = ChildAdd(window);
  text->Release();
  return added;
}

// Replace the retained atlas and derive its four-column cell. Request sufficient window size after publishing dimensions.
bool WnCombo::SetComboDownArrow(GxBuffer *incoming, const ScRect *rectangle) {
  if (arrow)
    arrow->Release();
  arrow = incoming;
  if (incoming)
    incoming->AddRef();
  i32 height;
  if (arrow) {
    if (rectangle) {
      i32 width = (i32)((u32)rectangle->right - (u32)rectangle->left);
      arrow_x = rectangle->left;
      arrow_y = rectangle->top;
      arrow_width = width / 4;
      height = (i32)((u32)rectangle->bottom - (u32)rectangle->top);
    } else {
      i32 width = arrow->Width();
      arrow_x = 0;
      arrow_y = 0;
      arrow_width = width / 4;
      height = arrow->Height();
    }
  } else {
    arrow_x = 0;
    arrow_y = 0;
    arrow_width = 16;
    height = GetH();
  }
  arrow_height = height;
  i32 width = GetW(), current_height = GetH();
  if (width < arrow_width)
    width = arrow_width;
  i32 minimum_height = arrow_height;
  if (current_height < minimum_height)
    current_height = minimum_height;
  SetSize(width, current_height);
  return true;
}

// Publish an existing owned string only for a valid signed index. Failed lookup leaves the genuine output untouched.
bool WnCombo::LookupString(i32 index, const ScString **out) const {
  WnListNode *node = strings.At((u32)index);
  if (!node)
    return false;
  *out = &node->text;
  return true;
}

// Select a valid item and copy it through the input's real string setter. Notify after temporary text has been destroyed.
bool WnCombo::SetSelection(i32 index, bool notify) {
  if (index < 0 || (u32)index >= strings.Count())
    return false;
  selection = index;
  WnTextControl *text = LookupLineInput();
  if (text) {
    ScString *stored = &strings.At((u32)index)->text;
    ScString value(stored->begin, stored->begin ? (u32)(stored->end - stored->begin) : 0);
    text->SetText(&value);
  }
  InvalidateSelfAndParents();
  if (notify)
    NotifyChanged();
  return true;
}

i32 WnCombo::GetSelection() const {
  return selection;
}

// Send command eleven with the live control ID to the current target. The source reports success even if delivery fails.
bool WnCombo::NotifyChanged() {
  u32 control_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandComboSelectionChanged, control_id, 0);
  return true;
}

// Handle only command kind nine by dispatching the real changed notification. The source ignores the child identifier.
bool WnCombo::OnCommand(u32 kind, u32) {
  if (kind != kWnCommandTextAccepted)
    return false;
  NotifyChanged();
  return true;
}

// Resolve missing defaults through one captured provider before creating and configuring the input. Warm calls skip work.
bool WnCombo::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnResources *resources = WnGetUiResources();
  if (resources) {
    if (!arrow)
      SetComboDownArrow(resources->GetImage(kWnThemeComboArrowImageId), nullptr);
    if (arrow_color == kWnDefaultColor)
      SetComboDownColor(resources->GetColor(kWnColorSilver));
    if (!font)
      SetFont(resources->GetFont(kWnFontDefault));
    if (font_colors[1] == kWnDefaultColor)
      SetFontColor(1, resources->GetColor(24));
    if (font_colors[2] == kWnDefaultColor)
      SetFontColor(2, resources->GetColor(25));
    if (highlight_color == kWnDefaultColor)
      SetHighlightColor(resources->GetColor(22));
  }
  if (!font)
    return false;
  CreateLineInput();
  bool enabled = GetFlag(kWnFlagEnabled);
  SetFlag(kWnFlagEnabled, enabled);
  SetSelection(selection, false);
  i32 height = GetH(), width = GetW();
  Layout(width, height);
  return WnRootWidget::Init();
}

// Move selection for arrow keys, opening the dropdown for Alt-down. Forward other keys to the live notification target.
bool WnCombo::OnKeyDown(u32 key, u32 modifiers) {
  if (key == kWnKeyUp)
    SetSelection((i32)((u32)selection - 1), true);
  else if (key == kWnKeyDown)
    if (modifiers & kWnModifierAlt)
      OpenDropDown();
    else
      SetSelection((i32)((u32)selection + 1), true);
  else
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  return true;
}

bool WnCombo::OnMouseDownL(i32, i32, u32) {
  OpenDropDown();
  return true;
}

// Insert an owned concrete copy before the requested item; zero prepends and minus two appends. Other negative indices fail.
bool WnCombo::InsertString(const ScStringView &text, i32 index) {
  u32 count = strings.Count();
  bool append = index != 0 && (index >= (i32)count || index == -2);
  if (index < 0 && !append)
    return false;
  WnListLink *position = !append && index != 0 ? strings.At((u32)index) : nullptr;
  ScString value(text);
  if (!position)
    position = append ? strings.head : strings.head->next;
  strings.InsertBefore(position, value);
  return true;
}

// Erase only a valid signed index without changing selection. The installed method reports true only for first-item removal.
bool WnCombo::RemoveString(i32 index) {
  WnListNode *node = strings.At((u32)index);
  if (!node)
    return false;
  strings.Erase(node);
  return index == 0;
}

// Find the first string through the live interface before delegating removal. Preserve the indexed method's unusual result.
bool WnCombo::RemoveString(const ScStringView &text) {
  i32 index;
  if (!GetIndexFromString(&index, text))
    return false;
  return RemoveString(index);
}

void WnCombo::RemoveAllStrings() {
  strings.Clear();
  selection = -1;
}

// Copy a found concrete string through the genuine output interface. Invalid indices leave that output untouched.
bool WnCombo::GetStringFromIndex(i32 index, ScStringView *text) const {
  const ScString *stored;
  if (!LookupString(index, &stored))
    return false;
  text->Copy(*stored);
  return true;
}

// Ask the input string to compare each stored item in order. Follow its live next link after callbacks can insert neighbors.
bool WnCombo::GetIndexFromString(i32 *index, const ScStringView &text) const {
  i32 visited = 0;
  for (WnListLink *node = strings.head->next; node != strings.head; node = node->next) {
    if (text.IsEqual(&static_cast<WnListNode *>(node)->text)) {
      *index = visited;
      return true;
    }
    ++visited;
  }
  return false;
}

// Publish the current signed selection before asking the live string getter to copy it. The source ignores lookup failure.
bool WnCombo::GetSelection(i32 *index, ScStringView *text) const {
  *index = selection;
  GetStringFromIndex(selection, text);
  return true;
}

// Paint the arrow cell at its local destination, or a filled bevel when no atlas is installed. Failed atlas locking stops paint.
bool WnCombo::GZPaint() {
  ScRect destination;
  destination.left = (i32)((u32)draw_area.left + (u32)arrow_left);
  destination.top = (i32)((u32)draw_area.top + (u32)arrow_top);
  destination.right = (i32)((u32)destination.left + (u32)arrow_width);
  destination.bottom = (i32)((u32)destination.top + (u32)arrow_height);
  if (!arrow)
    draw_buffer->DrawRaisedRect(destination, arrow_color, drop_open == 0, true);
  else {
    if (!draw_buffer->Lock(GxLockFlags::Blt))
      return false;
    ScRect source{arrow_source_x, arrow_y, (i32)((u32)arrow_source_x + (u32)arrow_width),
        (i32)((u32)arrow_y + (u32)arrow_height)};
    draw_buffer->Blt(arrow, source, destination, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

// Configure a concrete popup list with the combo's live appearance and strings, then enter the real modal loop.
// Apply its signed result through selection before shutting down the list and restoring the released arrow state.
bool WnCombo::OpenDropDown() {
  if (drop_open)
    return false;
  WnList *list = new WnList(1);
  list->AddRef();
  drop_open = 1;
  arrow_source_x = (i32)((u32)arrow_x + 2 * (u32)arrow_width);
  InvalidateSelfAndParents();
  WnListControl *control = list;
  control->GetWindow()->SetID(kWnComboPopupId);
  control->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
  WnWindow *window = control->GetWindow();
  i32 bottom = absolute_area.bottom, height = GetH();
  window->SetArea(absolute_area.left, bottom, absolute_area.right, (i32)((u32)bottom + (u32)height));
  window = control->GetWindow();
  window->SetFillColor(GetFillColor());
  control->SetFont(font);
  if (font_colors[0] != kWnDefaultColor)
    control->SetFontColor(0, font_colors[0]);
  if (font_colors[1] != kWnDefaultColor)
    control->SetFontColor(1, font_colors[1]);
  if (font_colors[2] != kWnDefaultColor)
    control->SetFontColor(2, font_colors[2]);
  control->SetOutlineDrawing(1);
  control->SetOutlineColors(dropdown_outline, dropdown_outline);
  control->SetHighlightColor(highlight_color);
  control->SetHighlightFillAlpha(highlight_opacity);
  control->SetScrollbarGutters(0, 0);
  control->SetTextGutters(horizontal_gutter, vertical_gutter);
  for (WnListLink *node = strings.head->next; node != strings.head; node = node->next)
    list->InsertString(static_cast<WnListNode *>(node)->text, -2);
  control->SetSelection(selection, 0);
  WnWindowManager *manager = GetWindowManager();
  i32 result = manager->DoModalWin(control->GetWindow());
  SetSelection(result, true);
  list->Shutdown();
  list->Release();
  drop_open = 0;
  arrow_source_x = (i32)((u32)arrow_x + (u32)arrow_width);
  InvalidateSelfAndParents();
  return true;
}

u32 WnCombo::GetFontColor(i32 index) const {
  return font_colors[index];
}

bool WnCombo::GetDropDownOutlineColor(u32 *color) const {
  *color = dropdown_outline;
  return true;
}

bool WnCombo::GetComboDownColor(u32 *color) const {
  *color = arrow_color;
  return true;
}

bool WnCombo::GetBtnGutter(i8 *gutter) const {
  *gutter = button_gutter;
  return true;
}

// Publish horizontal then vertical signed gutters, preserving the order when outputs alias.
bool WnCombo::GetTextGutters(i8 *horizontal, i8 *vertical) const {
  *horizontal = horizontal_gutter;
  *vertical = vertical_gutter;
  return true;
}

// Construct empty row storage and source state before resolving edge colors and the independently acquired fill color.
WnIconList::WnIconList()
    : items(new WnIconLink), font(nullptr), fill_background(0), draw_outline(0), position(0), extent(0), image_width(0),
      gutter(5), first_scroll_flag(0), second_scroll_flag(0) {
  items->next = items;
  items->previous = items;
  WnResources *resources = WnGetUiResources();
  if (resources) {
    first_edge = resources->GetColor(19);
    second_edge = resources->GetColor(20);
  }
  u32 color = WnGetUiResources()->GetColor(kWnColorUiBackground);
  WnRootWidget::SetFillColor(color);
}

// Close the actual control before list metadata and root destruction. The list's final cleanup releases strings only.
WnIconList::~WnIconList() {
  WnIconList::Shutdown();
  DestroyEntries();
  delete items;
}

bool WnIconList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnIconListControl, WnIconListControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnIconList::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnIconList::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed root-window interface without acquiring another hold.
WnWindow *WnIconList::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Destroy the concrete row strings before freeing their nodes, then restore the empty sentinel links.
void WnIconList::DestroyEntries() {
  WnIconLink *node = items->next;
  while (node != items) {
    WnIconLink *removed = node;
    node = node->next;
    delete static_cast<WnIconItem *>(removed);
  }
  items->next = items;
  items->previous = items;
}

// Release the font before traversing live rows and clearing each image after its callback, then close the root.
// All row images retire before any row string or node storage is destroyed.
bool WnIconList::Shutdown() {
  ScRelease(&font);
  WnIconLink *head = items;
  for (WnIconLink *node = head->next; node != head; node = node->next) {
    auto *item = static_cast<WnIconItem *>(node);
    if (item->image) {
      item->image->Release();
      item->image = nullptr;
    }
  }
  DestroyEntries();
  return WnRootWidget::Shutdown();
}

// Release the old font before publishing and retaining the incoming one, without relayout or invalidation.
void WnIconList::SetListItemFontEffects(FtFont *incoming) {
  if (font)
    font->Release();
  font = incoming;
  if (incoming)
    incoming->AddRef();
}

void WnIconList::SetIconToTextSpacingEffects(i32 spacing) {
  gutter = spacing;
  CalculateAllMetricsEffects();
}

void WnIconList::SetSunkenRectColorsEffects(u32 first, u32 second) {
  first_edge = first;
  second_edge = second;
}

u8 WnIconList::SetOpaque(u8 mode) {
  fill_background = mode;
  return mode;
}

// Count the linked rows between the list's sentinel and its next return to that sentinel.
u32 WnIconList::GetNumListItems() {
  u32 count = 0;
  for (WnIconLink *node = items->next; node != items; node = node->next)
    ++count;
  return count;
}

u8 WnIconList::SetDrawSunkenRect(u8 mode) {
  draw_outline = mode;
  return mode;
}

// Publish both scrollbar-control bytes in source order and report success without normalizing either byte.
bool WnIconList::EnableScrollBar(u8 first, u8 second) {
  first_scroll_flag = first;
  second_scroll_flag = second;
  return true;
}

// Return the displaced root-window identity of the new control at zero references, rather than its leading own interface.
ScUnknown *WnConstructIconList() {
  return static_cast<WnWindow *>(new WnIconList);
}

static i32 WnIconAdd(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

static i32 WnIconSubtract(i32 first, i32 second) {
  return (i32)((u32)first - (u32)second);
}

// Acquire a missing font before base initialization and row layout.
// Warm calls skip all work; cold calls ignore base failure.
bool WnIconList::Init() {
  if (!(flags & 0x4000)) {
    if (!font)
      font = ScAddRefNotNull(WnGetUiResources()->GetFont(kWnFontDefault));
    WnRootWidget::Init();
    CalculateAllMetricsEffects();
  }
  return true;
}

void WnIconList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  CalculateAllMetricsEffects();
}

// Attach a provider-created vertical scrollbar only when enabled and absent, then release the acquired control.
// The source resets the scroll position and updates geometry even when the attachment reports failure.
bool WnIconList::AddVerticalScrollbar() {
  WnResources *resources = WnGetUiResources();
  bool added = false;
  if (resources && first_scroll_flag && !ChildExists(kWnIconListScrollbarId)) {
    WnScrollbarControl *scrollbar = resources->CreateScrollbar(kWnIconListScrollbarId, 2, 0, 0, 0);
    added = ChildAdd(scrollbar->GetWindow());
    scrollbar->Release();
    position = 0;
    SetScrollbarSizeEffects();
    SetScrollbarValueEffects();
  }
  return added;
}

// Remove the borrowed scrollbar child through the original root operation when it exists.
void WnIconList::RemoveVerticalScrollbarEffects() {
  WnWindow *window = GetChildWindowFromID(kWnIconListScrollbarId);
  if (window)
    ChildDelete(window);
}

void WnIconList::SetScrollbarSizeEffects() {
  void *slot = nullptr;
  if (GetChildAs(kWnIconListScrollbarId, IID_WnWindow, &slot)) {
    i32 height = GetH(), right = GetW();
    i32 offset = WnIconSubtract(1, static_cast<WnWindow *>(slot)->GetW());
    i32 left = WnIconAdd(GetW(), offset);
    static_cast<WnWindow *>(slot)->SetArea(left, 0, right, height);
    static_cast<WnWindow *>(slot)->Release();
  }
}

// Publish the full content extent before the live scroll value, preserving callbacks between the two operations.
void WnIconList::SetScrollbarValueEffects() {
  void *slot = nullptr;
  if (GetChildAs(kWnIconListScrollbarId, IID_WnScrollbarControl, &slot)) {
    static_cast<WnScrollbarControl *>(slot)->SetRange(0, extent);
    static_cast<WnScrollbarControl *>(slot)->SetValue(position);
    static_cast<WnScrollbarControl *>(slot)->Release();
  }
}

// Recalculate image width and wrapped row intervals before creating, sizing or removing the scrollbar.
void WnIconList::CalculateAllMetricsEffects() {
  if (!font)
    font = ScAddRefNotNull(WnGetUiResources()->GetFont(kWnFontDefault));
  i32 font_height = font->Height(0), scrollbar_width = 0;
  void *slot = nullptr;
  bool has_scrollbar = GetChildAs(kWnIconListScrollbarId, IID_WnWindow, &slot);
  if (has_scrollbar) {
    scrollbar_width = static_cast<WnWindow *>(slot)->GetW();
    static_cast<WnWindow *>(slot)->Release();
  }
  WnIconLink *head = items;
  image_width = 0;
  for (WnIconLink *node = head->next; node != head; node = node->next) {
    GxBuffer *image = static_cast<WnIconItem *>(node)->image;
    if (image) {
      i32 width = image->Width();
      if (width > image_width)
        image_width = width;
    }
  }
  i32 available = GetW();
  i32 width = WnIconSubtract(WnIconSubtract(WnIconSubtract(available, image_width), scrollbar_width), gutter);
  width = WnIconSubtract(width, 6);
  if (width < 0)
    width = 0;
  head = items;
  i32 top = 5;
  for (WnIconLink *node = head->next; node != head; node = node->next) {
    auto *item = static_cast<WnIconItem *>(node);
    item->start = top;
    i32 image_bottom = top, text_top = top;
    if (item->image) {
      i32 height = item->image->Height();
      image_bottom = WnIconAdd(top, height);
      text_top = WnIconSubtract(WnIconAdd(WnIconAdd(top, top), height) / 2, font_height / 2);
      if (text_top < top)
        text_top = top;
    }
    i32 length = (i32)(item->text.end - item->text.begin), consumed = 0;
    while (consumed < length) {
      u32 fitted = font->WrapFit(item->text.begin + consumed, (u32)(length - consumed), (u32)width, 1);
      consumed = WnIconAdd(consumed, (i32)fitted);
      text_top = WnIconAdd(text_top, font_height);
    }
    top = WnIconAdd(text_top > image_bottom ? text_top : image_bottom, 5);
    item->finish = top;
  }
  extent = top;
  i32 height = GetH();
  if (extent <= height) {
    if (!has_scrollbar)
      return;
    RemoveVerticalScrollbarEffects();
  } else {
    if (has_scrollbar) {
      SetScrollbarSizeEffects();
      SetScrollbarValueEffects();
      return;
    }
    AddVerticalScrollbar();
  }
  CalculateAllMetricsEffects();
}

// Locate a concrete row in native linked storage. This replaces only the original generic-list traversal.
WnIconItem *WnIconList::ItemAt(u32 index) const {
  WnIconLink *node = items->next;
  for (u32 i = 0; i < index && node != items; ++i)
    node = node->next;
  return node != items ? static_cast<WnIconItem *>(node) : nullptr;
}

// Retain the image before copying the incoming interface through ToChar, then insert an owned concrete row.
// Negative or past-end indices append; the temporary text stays alive until layout and invalidation finish.
bool WnIconList::InsertListItem(GxBuffer *image, const ScStringView &text, u32 color, i32 index) {
  ScString temporary;
  if (image)
    image->AddRef();
  temporary.Copy(text);
  WnIconLink *before = items;
  if (index >= 0) {
    before = items->next;
    for (i32 i = 0; i < index && before != items; ++i)
      before = before->next;
  }
  auto *item = new WnIconItem{{nullptr, nullptr}, image, temporary, color, 0, 0};
  item->next = before;
  item->previous = before->previous;
  item->previous->next = item;
  before->previous = item;
  CalculateAllMetricsEffects();
  InvalidateSelfAndParents();
  return true;
}

// Unlink a valid row before destroying its string and storage, then recalculate and invalidate the remaining list.
// The original individual-removal path does not release that row's image hold.
bool WnIconList::RemoveListItem(u32 index) {
  WnIconItem *item = ItemAt(index);
  if (!item)
    return false;
  item->previous->next = item->next;
  item->next->previous = item->previous;
  delete item;
  CalculateAllMetricsEffects();
  InvalidateSelfAndParents();
  return true;
}

// Release and clear all live image holds before destroying any row strings, then layout the empty list.
// Preserve the existing scroll position rather than adding an empty-list reset absent from the source.
bool WnIconList::RemoveAllListItems() {
  WnIconLink *head = items;
  for (WnIconLink *node = head->next; node != head; node = node->next) {
    auto *item = static_cast<WnIconItem *>(node);
    if (item->image) {
      item->image->Release();
      item->image = nullptr;
    }
  }
  DestroyEntries();
  CalculateAllMetricsEffects();
  InvalidateSelfAndParents();
  return true;
}

// Validate the row before making an interface-copy temporary through Data, then assign its concrete bytes.
// Destroy the temporary before layout and invalidation, preserving the original callback ordering.
bool WnIconList::ModifyString(const ScStringView &text, u32 index) {
  WnIconItem *item = ItemAt(index);
  if (!item)
    return false;
  {
    ScString temporary(text);
    item->text = temporary;
  }
  CalculateAllMetricsEffects();
  InvalidateSelfAndParents();
  return true;
}

// Publish a valid row's font color before unconditional layout and invalidation. Invalid indices leave all state alone.
bool WnIconList::ModifyStringColor(u32 color, u32 index) {
  WnIconItem *item = ItemAt(index);
  if (!item)
    return false;
  item->color = color;
  CalculateAllMetricsEffects();
  InvalidateSelfAndParents();
  return true;
}

// Advance by the live viewport height and clamp only above the complete content extent before invalidating.
bool WnIconList::ScrollPageDown() {
  if (position < extent) {
    i32 height = GetH();
    position = (i32)((u32)position + (u32)height);
    if (position > extent)
      position = extent;
    InvalidateSelfAndParents();
  }
  return true;
}

// Retreat by the live viewport height, clamping a negative result to zero before invalidation.
bool WnIconList::ScrollPageUp() {
  if (position > 0) {
    i32 height = GetH();
    position = (i32)((u32)position - (u32)height);
    if (position < 0)
      position = 0;
    InvalidateSelfAndParents();
  }
  return true;
}

// Select a valid row's starting offset and synchronize the actual scrollbar without adding direct invalidation.
bool WnIconList::MakeTopVisibleItem(u32 index) {
  WnIconLink *node = items->next;
  for (u32 i = 0; i < index && node != items; ++i)
    node = node->next;
  if (node == items)
    return false;
  position = static_cast<WnIconItem *>(node)->start;
  SetScrollbarValueEffects();
  return true;
}

// Refuse keys while this window owns capture; page-up advances and page-down retreats through the live control methods.
// Other keys preserve the source's complete message words and delivery result.
bool WnIconList::OnKeyDown(u32 key, u32 modifiers) {
  if (owner->GetCapture() == this)
    return false;
  if (key == kWnKeyPageUp)
    ScrollPageDown();
  else if (key == kWnKeyPageDown)
    ScrollPageUp();
  else
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  return true;
}

bool WnIconList::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

// Consume only this scrollbar's command and copy its signed value directly.
// The source retains the acquired control hold.
bool WnIconList::OnCommand(u32 kind, u32 control_id) {
  if (kind != kWnCommandSliderValueChanged || control_id != kWnIconListScrollbarId)
    return false;
  void *slot = nullptr;
  if (!GetChildAs(kWnIconListScrollbarId, IID_WnScrollbarControl, &slot))
    return false;
  position = static_cast<WnScrollbarControl *>(slot)->GetValue();
  return true;
}

// Draw optional background and edge lines before visible image/caption rows, clipping partially exposed rows.
// Font color changes are per row and the saved color is restored only when the caption column has room.
bool WnIconList::GZPaint() {
  i32 height = GetH(), bottom = WnIconAdd(position, height), font_height = 0, scrollbar_width = 0;
  u32 saved_color = 0;
  if (font) {
    saved_color = font->Color();
    font_height = font->Height(0);
  }
  void *slot = nullptr;
  if (GetChildAs(kWnIconListScrollbarId, IID_WnScrollbarControl, &slot)) {
    auto *scrollbar = static_cast<WnScrollbarControl *>(slot);
    scrollbar_width = scrollbar->GetWindow()->GetW();
    scrollbar->Release();
  }
  if (fill_background && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (draw_outline && draw_buffer->Lock(GxLockFlags::Line)) {
    draw_buffer->DrawLine(
        draw_area.left, draw_area.top, draw_area.right, draw_area.top, static_cast<i32>(first_edge), 1);
    draw_buffer->DrawLine(
        draw_area.left, draw_area.top, draw_area.left, draw_area.bottom, static_cast<i32>(first_edge), 1);
    draw_buffer->DrawLine(
        draw_area.left, draw_area.bottom, draw_area.right, draw_area.bottom, static_cast<i32>(second_edge), 1);
    draw_buffer->DrawLine(
        draw_area.right, draw_area.top, draw_area.right, draw_area.bottom, static_cast<i32>(second_edge), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  ScRect image_area;
  image_area.left = WnIconAdd(draw_area.left, 3);
  ScRect text_area{WnIconAdd(image_area.left, WnIconAdd(image_width, gutter)), 0,
      WnIconSubtract(WnIconSubtract(draw_area.right, scrollbar_width), 3), draw_area.bottom};
  if (text_area.right >= text_area.left) {
    WnIconLink *head = items;
    for (WnIconLink *node = head->next; node != head; node = node->next) {
      auto *item = static_cast<WnIconItem *>(node);
      if (item->start > bottom || item->finish < position)
        continue;
      image_area.top = WnIconAdd(item->start, WnIconSubtract(draw_area.top, position));
      bool full = item->start >= position && item->finish < bottom;
      if (item->image) {
        i32 image_height = (i32)item->image->Height();
        image_area.bottom = WnIconAdd(image_area.top, image_height);
        i32 current_image_width = (i32)item->image->Width();
        image_area.right = WnIconAdd(image_area.left, current_image_width);
        if (draw_buffer->Lock(GxLockFlags::Blt)) {
          const ScRect *source = item->image->GetBufferArea();
          draw_buffer->Blt(item->image, *(source), image_area, full ? nullptr : &draw_area);
          draw_buffer->Unlock(GxLockFlags::Blt);
        }
        u32 half_image = item->image->Height() >> 1;
        text_area.top = WnIconSubtract(WnIconAdd(image_area.top, (i32)half_image), font_height / 2);
      } else
        text_area.top = image_area.top;
      u32 color = item->color;
      if (font->Color() != color)
        font->SetColor(color);
      u32 length = (u32)((uptr)item->text.end - (uptr)item->text.begin);
      if (full)
        font->DrawTextPara(draw_buffer, text_area, item->text.begin, length);
      else {
        ScRect clip = draw_area;
        font->DrawTextPara(draw_buffer, text_area, item->text.begin, length, &clip);
      }
    }
    if (font && font->Color() != saved_color)
      font->SetColor(saved_color);
  }
  return true;
}

WnListNode::WnListNode(const ScString &value) : text(value.begin, value.begin ? (u32)(value.end - value.begin) : 0) {
}

WnListStrings::WnListStrings() : head(new WnListLink) {
  head->next = head;
  head->previous = head;
}

WnListStrings::~WnListStrings() {
  Clear();
  delete head;
}

// Count live entries without caching a size in the original one-pointer owner.
u32 WnListStrings::Count() const {
  u32 count = 0;
  for (WnListLink *node = head->next; node != head; node = node->next)
    ++count;
  return count;
}

// Locate one live entry by its unsigned position; the sentinel is never treated as text.
WnListNode *WnListStrings::At(u32 index) const {
  WnListLink *node = head->next;
  while (node != head && index) {
    node = node->next;
    --index;
  }
  return node == head ? nullptr : static_cast<WnListNode *>(node);
}

// Construct an owned entry before splicing it immediately ahead of the supplied live position.
// Existing entry and text addresses remain stable across insertion.
void WnListStrings::InsertBefore(WnListLink *position, const ScString &text) {
  WnListNode *node = new WnListNode(text);
  node->next = position;
  node->previous = position->previous;
  node->previous->next = node;
  position->previous = node;
}

// Unlink and destroy a real entry; callers never pass the link-only sentinel.
void WnListStrings::Erase(WnListLink *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete static_cast<WnListNode *>(node);
}

void WnListStrings::Clear() {
  while (head->next != head)
    Erase(head->next);
}

// Initialize the list's source fields and propagate resource color seventeen through the root. Popup mode chooses the plain outline.
WnList::WnList(bool incoming)
    : background_opacity(0.6f), background_color(kWnDefaultColor), language(0), font(nullptr), selection(-1),
      visible_rows(0), first_visible(0), row_height(10), minimum_height(0), byte220(0), outline(0), line_outline(0),
      popup(incoming), byte224(1), byte225(0), popup_image(nullptr), row_image(nullptr), double_click(0),
      last_click(-1), outline_first(kWnDefaultColor), outline_second(kWnDefaultColor), gutter252(3), gutter253(0),
      gutter254(3), gutter255(1), byte256(1), byte257(0) {
  SetFillColor(WnGetUiResources()->GetColor(kWnColorWhite));
  font_colors[0] = kWnDefaultColor;
  font_colors[1] = kWnDefaultColor;
  font_colors[2] = kWnDefaultColor;
  if (popup) {
    outline = 1;
    line_outline = 0;
    InvalidateSelfAndParents();
  } else
    SetDrawSunkenRect(1);
}

WnList::~WnList() {
  WnList::Shutdown();
}

// Publish the actual list secondary interface before retaining the live root. Other identities delegate to root query.
bool WnList::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnListControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnListControl *>(this);
  AddRef();
  return true;
}

u32 WnList::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnList::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed canonical window view without acquiring a reference.
WnWindow *WnList::GetWindow() {
  return static_cast<WnWindow *>(this);
}

bool WnList::Shutdown() {
  HideWindow();
  ScRelease(&popup_image);
  ScRelease(&row_image);
  ScRelease(&font);
  return WnRootWidget::Shutdown();
}

// Store a valid indexed font color. Preserve the source's equality-only invalidation rather than reversing its condition.
bool WnList::SetFontColor(i32 index, u32 color) {
  if ((u32)index > 2)
    return false;
  u32 old = font_colors[index];
  font_colors[index] = color;
  if (old == color)
    InvalidateSelfAndParents();
  return true;
}

void WnList::SetHighlightColor(u32 color) {
  background_color = color;
  InvalidateSelfAndParents();
}

void WnList::SetHighlightFillAlpha(f32 opacity) {
  background_opacity = opacity;
  if (background_opacity > 1)
    background_opacity = 1;
  if (!(background_opacity >= 0))
    background_opacity = 0;
  InvalidateSelfAndParents();
}

// Set the sunken outline and clear the mutually exclusive plain outline before invalidating.
void WnList::SetDrawSunkenRect(bool enabled) {
  line_outline = enabled;
  outline = 0;
  InvalidateSelfAndParents();
}

// Store both outline pixels in source order, then invalidate the list and its ancestors.
void WnList::SetOutlineColors(u32 first, u32 second) {
  outline_first = first;
  outline_second = second;
  InvalidateSelfAndParents();
}

// Query the first scrollbar child and immediately release the query hold. The returned live interface is borrowed.
WnScrollbarControl *WnList::GetScrollbarV() {
  void *slot = nullptr;
  if (!GetChildAs(kWnListVerticalScrollbarId, IID_WnScrollbarControl, &slot))
    return nullptr;
  static_cast<WnScrollbarControl *>(slot)->Release();
  return static_cast<WnScrollbarControl *>(slot);
}

// Query the second scrollbar child with the same borrowed-result ordering. Failed child lookup returns null.
WnScrollbarControl *WnList::GetScrollbarH() {
  void *slot = nullptr;
  if (!GetChildAs(kWnListHorizontalScrollbarId, IID_WnScrollbarControl, &slot))
    return nullptr;
  static_cast<WnScrollbarControl *>(slot)->Release();
  return static_cast<WnScrollbarControl *>(slot);
}

// Forward an exact enabled-bit request to both existing scrollbar windows before changing the actual root flags.
u32 WnList::SetFlag(u32 mask, bool enabled) {
  if (mask == kWnFlagEnabled) {
    if (WnScrollbarControl *control = GetScrollbarV())
      control->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
    if (WnScrollbarControl *control = GetScrollbarH())
      control->GetWindow()->SetFlag(kWnFlagEnabled, enabled);
  }
  return WnRootWidget::SetFlag(mask, enabled);
}

// Construct an ordinary list and return its canonical primary identity at zero references. Initialization remains separate.
ScUnknown *WnConstructList() {
  return static_cast<WnWindow *>(new WnList(0));
}

i32 WnList::GetItemCount() const {
  return (i32)strings.Count();
}

// Publish a borrowed const concrete string only for a valid signed index.
// Failed lookup leaves the genuine output untouched.
bool WnList::GetStringAddressFromIndex(i32 index, const ScString **text) const {
  WnListNode *node = strings.At((u32)index);
  if (!node)
    return false;
  *text = &node->text;
  return true;
}

// Copy a located concrete string without invoking interface setters on the output. Preserve concrete self-assignment behavior.
bool WnList::GetStringFromIndex(i32 index, ScString *text) const {
  const ScString *stored;
  if (!GetStringAddressFromIndex(index, &stored))
    return false;
  if (stored != text)
    text->ScString::FromChar(stored->begin, stored->begin ? (u32)(stored->end - stored->begin) : 0);
  return true;
}

// Compare concrete string lengths and bytes directly, without calling the input's virtual equality method.
// Publish the first match through the genuine output; the original concrete overload is nonconst.
bool WnList::GetIndexFromString(i32 *index, const ScString &text) {
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
bool WnList::GetStringFromIndex(i32 index, ScStringView *text) const {
  ScString value;
  bool found = GetStringFromIndex(index, &value);
  if (found)
    text->Copy(value);
  return found;
}

// Ask the incoming interface to compare each owned item. Follow stable items across callbacks that move neighboring entries.
bool WnList::GetIndexFromString(i32 *index, const ScStringView &text) const {
  i32 visited = 0;
  for (WnListLink *link = strings.head->next; link != strings.head; link = link->next, ++visited) {
    if (text.IsEqual(&static_cast<WnListNode *>(link)->text)) {
      *index = visited;
      return true;
    }
  }
  return false;
}

// Erase first for zero, last for minus two or an index past the count, and a matching middle item otherwise.
// Empty lists return false; other invalid negative indices are successful no-ops, and selection remains unchanged.
bool WnList::RemoveStringAtIndex(i32 index) {
  i32 count = GetItemCount();
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
bool WnList::InsertStringAtIndex(i32 index, const ScString &text) {
  bool append = index != 0 && (index == -2 || index >= GetItemCount());
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

bool WnList::IsIndexInView(i32 index) const {
  i32 end = (i32)((u32)first_visible + (u32)visible_rows);
  return index >= first_visible && index < end;
}

// Paint background, popup frame, selected-row fill and visible strings through the original graphics and font interfaces.
// Preserve inline color escapes, language-specific leading text and the font-dependent final outline.
bool WnList::GZPaint() {
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
      const ScRect *source = row_image->GetBufferArea();
      draw_buffer->BltTiled(row_image, *(source), rectangle, 0, 0, nullptr);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  } else if (draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, &rectangle);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (popup && popup_image && draw_buffer->Lock(GxLockFlags::Blt)) {
    u32 height = popup_image->Height() / 3, width = popup_image->Width() / 3;
    ScRect slice{0, 0, (i32)width, (i32)height};
    draw_buffer->BltEdge(popup_image, slice, rectangle, static_cast<u8>(0));
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if ((background_opacity < 0 || background_opacity > 0) && selection > -1 && IsIndexInView(selection)) {
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
          sscanf(text.begin, "#%02x%02x%02x", &red, &green, &blue);
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
        if (FtFont *prefix_font = WnGetUiResources()->GetFont(kWnFontUiFileDialogAlternate)) {
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
        font->DrawTextA(draw_buffer, (u32)left + (u32)advance, top, text.begin + leading, count);
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
      draw_buffer->DrawLine(rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom,
          static_cast<i32>(outline_second), 1);
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


i32 WnList::GetSelection() const {
  return selection;
}

// Select the preceding row without notification only when the live count is nonzero and the stored selection is positive.
// Ignore the selection setter's result and report whether that operation was attempted.
bool WnList::MoveLineUp() {
  if (!GetItemCount() || selection <= 0)
    return false;
  SetSelection(selection - 1, 0);
  return true;
}

// Select the following row only below the last row of a positive live count. Preserve negative starting selections.
bool WnList::MoveLineDown() {
  i32 count = GetItemCount();
  if (count <= 0 || selection >= count - 1)
    return false;
  SetSelection((i32)((u32)selection + 1), 0);
  return true;
}

// Select the first owned string whose first stored byte matches, without case folding or a virtual string query.
// Empty strings never match, while a stored leading zero remains a valid one-byte prefix.
bool WnList::ScrollToFirstLetter(char character) {
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

// Forward a character's low byte to the live first-letter selector only while this list does not hold capture.
// The original DWORD character virtual always reports false, independently of the selector's Boolean result.
bool WnList::GZCharacter(u32 character) {
  if (owner->GetCapture() != static_cast<WnWindow *>(this))
    ScrollToFirstLetter((char)character);
  return false;
}

// Dispatch navigation through the live list interface, notifying only when its stored selection changes.
// Enter reselects before activating or completing a popup; ordinary unhandled keys retain their complete message words.
bool WnList::OnKeyDown(u32 key, u32 modifiers) {
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

// Consume key releases in popup mode. Ordinary lists forward the complete key and modifier words to their live target.
bool WnList::OnKeyUp(u32 key, u32 modifiers) {
  if (popup)
    return true;
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Read the live control ID before dispatching command six. Delivery failure does not change the source's success result.
bool WnList::SendSelectionChangedMessage() {
  u32 control_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListSelectionChanged, control_id, 0);
  return true;
}

// Send the current selection before command seven with a freshly read ID. The first callback can change the second target.
bool WnList::SendDoubleClickMessage() {
  i32 selected = GetSelection();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClickSelection, (u32)selected, 0);
  u32 control_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandListDoubleClicked, control_id, 0);
  return true;
}

// Post the original unsigned modal result only for popup lists.
// Capture the manager before obtaining the borrowed window, retaining the full cancellation word.
bool WnList::EndDialog(u32 result) {
  if (!popup)
    return false;
  WnWindowManager *manager = GetWindowManager();
  WnWindow *window = GetWindow();
  manager->GZWinMsgPost(kWnMsg_EndModal, (uptr)window, (uptr)result);
  return true;
}

// Check translated coordinates after each live child size callback. The callbacks may update either coordinate.
static bool WnListContainsTranslated(WnWindow *window, i32 *x, i32 *y) {
  if (*x < 0 || *y < 0)
    return false;
  i32 width = window->GetW();
  if (*x >= width)
    return false;
  i32 height = window->GetH();
  return *y < height;
}
