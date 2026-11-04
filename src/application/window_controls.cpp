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

// Releasing clears every pressed part; mouse updates clear arrow and track bits while retaining the thumb bit.
enum ApScrollPartMask : u32 {
  kApScrollWithoutPressedParts = 0xffffffe0,
  kApScrollWithoutArrowAndTrackParts = 0xffffffe4,
};

WnAppLabel::WnAppLabel()
    : font(nullptr), padding{2, 2, 0, 0}, text_area{0, 0, 8, 8}, label_flags(6), alignment(0),
      text_colors{kWnDefaultColor, kWnDefaultColor}, text_alignment(0) {
}

WnAppLabel::~WnAppLabel() {
  Shutdown();
}

// Publish the label secondary identity before live AddRef; its own IID has the original null-output check.
bool WnAppLabel::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnLabelControl)
    return UiWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnLabelControl *>(this);
  AddRef();
  return true;
}

u32 WnAppLabel::AddRef() {
  return UiWindow::AddRef();
}

u32 WnAppLabel::Release() {
  return UiWindow::Release();
}

// Return the borrowed canonical window view of this label.
WnWindow *WnAppLabel::GetWindow() {
  return static_cast<WnWindow *>(this);
}

// Before base initialization, acquire the default font only when the label has no font and a UI provider is present.
bool WnAppLabel::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  if (!font)
    if (WnResources *resources = ApGetUiResources())
      SetFont(resources->GetFont(kWnFontDefault));
  return UiWindow::Init();
}

// Hide the live window before releasing its font and delegating the base close result.
bool WnAppLabel::Shutdown() {
  HideWindow();
  ScRelease(&font);
  return UiWindow::Shutdown();
}

bool WnAppLabel::GetLabelFlag(i32 mask) {
  return (label_flags & (u32)mask) != 0;
}

// Update the label flag, recalculating wrapped layout or mirroring background transparency through real window flags.
bool WnAppLabel::SetLabelFlag(i32 mask, bool enabled) {
  if (enabled)
    label_flags |= (u32)mask;
  else
    label_flags &= ~(u32)mask;
  if (mask == 2) {
    Layout();
    InvalidateSelfAndParents();
  } else if (mask == 4) {
    SetFlag(kWnFlagTransparentBuffer, !enabled);
    SetFlag(kWnFlagClearTransparentBuffer, GetFlag(kWnFlagTransparentBuffer));
    InvalidateSelfAndParents();
  }
  return true;
}

// Store one of two explicit colors, recompute whether they match the live font's default pair, then dirty the label.
bool WnAppLabel::SetTextColor(i32 index, u32 color) {
  if ((u32)index > 1)
    return false;
  text_colors[index] = color;
  bool matches = false;
  if (font) {
    u32 foreground = font->Color();
    if (text_colors[0] == foreground) {
      u32 background = font->EndColor();
      matches = text_colors[1] == background;
    }
  }
  SetLabelFlag(kWnLabelUseFontColors, matches);
  InvalidateSelfAndParents();
  return true;
}

bool WnAppLabel::SetTextColor(u32 color) {
  return SetTextColor(0, color);
}

bool WnAppLabel::SetBackground(bool enabled) {
  return SetLabelFlag(kWnLabelFillBackground, enabled);
}

// Retain the incoming font before releasing the live old one; fill sentinel colors, re-evaluate both colors and lay out.
bool WnAppLabel::SetFont(FtFont *value) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  if (value) {
    if (text_colors[0] == kWnDefaultColor)
      SetTextColor(0, value->Color());
    if (text_colors[1] == kWnDefaultColor)
      SetTextColor(1, font->EndColor());
  }
  SetTextColor(0, text_colors[0]);
  SetTextColor(1, text_colors[1]);
  Layout();
  InvalidateSelfAndParents();
  return true;
}

// Select one of nine original placements and its three-way text alignment; unchanged values skip layout and dirtiness.
bool WnAppLabel::SetAlignment(u32 value) {
  if (value == alignment)
    return true;
  if (value > 8)
    return false;
  text_alignment = value / 3;
  alignment = value;
  Layout();
  InvalidateSelfAndParents();
  return true;
}

// Store the two signed byte gutters and recompute text placement. The source returns true without dirtying the label.
bool WnAppLabel::SetGutters(i8 horizontal, i8 vertical) {
  padding[0] = horizontal;
  padding[1] = vertical;
  Layout();
  return true;
}

// Copy through the actual string interface before recomputing text placement and invalidating the label.
void WnAppLabel::SetCaption(const ScStringView &text) {
  UiWindow::SetCaption(text);
  Layout();
  InvalidateSelfAndParents();
}

// Preserve base area changes and relayout only when the clamped requested dimensions differ from the old dimensions.
void WnAppLabel::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width < 0)
    width = 0;
  if (height < 0)
    height = 0;
  bool width_changed = width != GetW(), height_changed = height != GetH();
  UiWindow::SetArea(left, top, right, bottom);
  if (width_changed || height_changed)
    Layout();
}

// Calculate wrapped text bounds or obtain the actual unwrapped font rectangle through its genuine output.
void WnAppLabel::GetTextExtent(ScRect *out) {
  if (!font) {
    *out = {0, 0, 1, 1};
    return;
  }
  if (GetLabelFlag(kWnLabelWrapText)) {
    i32 window_width = GetW();
    i32 width = (i32)((u32)window_width - (u32)(2 * padding[0]));
    if (width < 1)
      width = 1;
    FtFont *selected = font;
    u32 lines = selected->WrapLines(caption.Data(), caption.Strlen(), (u32)width);
    *out = {0, 0, width, (i32)(selected->Height(0) * lines)};
  } else
    font->TextArea(caption.Data(), caption.Strlen(), out);
}

// Recompute local text bounds from signed BYTE margins and the original top/center/bottom placement groups.
void WnAppLabel::Layout() {
  if (!caption.Strlen())
    return;
  u32 current_alignment = alignment;
  switch (current_alignment) {
  case 0:
  case 3:
  case 6: {
    GetArea(&text_area);
    i32 width = (i32)((u32)text_area.right - (u32)text_area.left);
    i32 height = (i32)((u32)text_area.bottom - (u32)text_area.top);
    text_area = {padding[0], padding[1], (i32)((u32)width - (u32)(i32)padding[0]),
        (i32)((u32)height - (u32)(i32)padding[1])};
    break;
  }
  case 1:
  case 4:
  case 7:
  case 2:
  case 5:
  case 8: {
    ScRect extent;
    GetTextExtent(&extent);
    i32 height = (i32)((u32)extent.bottom - (u32)extent.top);
    i32 window_height = GetH();
    i32 top = current_alignment % 3 == 1 ? (i32)((u32)window_height - (u32)height) / 2 :
        (i32)((u32)window_height - (u32)(i32)padding[1] - (u32)height);
    i32 gutter = padding[0];
    i32 bottom = (i32)((u32)top + (u32)extent.bottom - (u32)extent.top);
    i32 right = (i32)((u32)GetW() - (u32)gutter);
    text_area = {gutter, top, right, bottom};
    break;
  }
  }
}

// Fit the actual window to its current text extent plus both signed margins through the live SetSize virtual method.
bool WnAppLabel::Fit() {
  ScRect extent;
  GetTextExtent(&extent);
  SetSize((i32)((u32)extent.right + (u32)(2 * padding[0]) - (u32)extent.left),
      (i32)((u32)extent.bottom + (u32)(2 * padding[1]) - (u32)extent.top));
  return true;
}

// Fill the selected background, then render caption text with the original single- or dual-color branch.
bool WnAppLabel::GZPaint() {
  if ((label_flags & 4) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  FtFont *selected = font;
  if (selected && caption.Strlen()) {
    ScRect rectangle = text_area;
    rectangle.left = (i32)((u32)rectangle.left + (u32)draw_area.left);
    rectangle.right = (i32)((u32)rectangle.right + (u32)draw_area.left);
    rectangle.top = (i32)((u32)rectangle.top + (u32)draw_area.top);
    rectangle.bottom = (i32)((u32)rectangle.bottom + (u32)draw_area.top);
    if (label_flags & 0x10000)
      selected->DrawTextPara(draw_buffer, rectangle, caption.Data(), caption.Strlen(), FtOutputStyle(text_alignment));
    else
      selected->DrawTextPara(draw_buffer, rectangle, caption.Data(), caption.Strlen(), text_colors[0], text_colors[1],
          FtOutputStyle(text_alignment));
  }
  return true;
}

// Publish a valid indexed text pixel; invalid indices preserve the caller's output.
bool WnAppLabel::GetTextColor(i32 index, u32 *color) const {
  if ((u32)index > 1)
    return false;
  *color = text_colors[index];
  return true;
}

bool WnAppLabel::GetTextColor(u32 *color) {
  return GetTextColor(0, color);
}

// Restore a valid text color from the current font through the live indexed setter.
bool WnAppLabel::ClearTextColor(i32 index) {
  if (!index)
    return SetTextColor(0, font->Color());
  if (index == 1)
    return SetTextColor(1, font->EndColor());
  return false;
}

bool WnAppLabel::ClearTextColor() {
  return ClearTextColor(0);
}

bool WnAppLabel::IsBackgroundOpaque() {
  return GetLabelFlag(kWnLabelFillBackground);
}

// Publish signed gutter bytes in order, retaining alias-visible output stores.
bool WnAppLabel::GetGutters(i8 *horizontal, i8 *vertical) {
  *horizontal = padding[0];
  *vertical = padding[1];
  return true;
}

// Store both signed text offsets without layout or invalidation side effects.
bool WnAppLabel::SetTextOffsets(i8 horizontal, i8 vertical) {
  padding[2] = horizontal;
  padding[3] = vertical;
  return true;
}

// Publish the stored signed text offsets through their original byte outputs.
bool WnAppLabel::GetTextOffsets(i8 *horizontal, i8 *vertical) {
  *horizontal = padding[2];
  *vertical = padding[3];
  return true;
}

void WnAppLabel::SetFillColor(u32 color) {
  UiWindow::SetFillColor(color);
  InvalidateSelfAndParents();
}

// Dispatch keys through the root; permitted pointer messages translate into the current parent's coordinates before delivery.
bool WnAppLabel::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp)
    return UiWindow::DoMessage(message);
  if (!GetLabelFlag(kWnLabelForwardMessages) || !parent)
    return false;
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel) {
    i32 left = GetL();
    message->data1 = (u32)message->data1 + (u32)left;
    i32 top = GetT();
    message->data2 = (u32)message->data2 + (u32)top;
  }
  return parent->DoMessage(message);
}

bool WnAppLabel::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnAppLabel::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

WnAppCombo::WnAppCombo(bool incoming)
    : selection(-1), drop_open(0), font(nullptr), font_colors{kWnDefaultColor, kWnDefaultColor, kWnDefaultColor},
      highlight_color(kWnDefaultColor), highlight_opacity(1.0f), dropdown_outline(0), editable(incoming),
      arrow(nullptr), arrow_x(0), arrow_y(0), arrow_width(16), arrow_height(8), arrow_left(0), arrow_top(0),
      arrow_source_x(0), arrow_color(kWnDefaultColor), button_gutter(1), horizontal_gutter(3), vertical_gutter(0) {
  SetFillColor(ApGetUiResources()->GetColor(21));
}

WnAppCombo::~WnAppCombo() {
  WnAppCombo::Shutdown();
}

// Publish the actual secondary interface for the combo IID. Other identities use the root query.
bool WnAppCombo::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnComboControl)
    return UiWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnAppComboControl *>(this);
  AddRef();
  return true;
}

u32 WnAppCombo::AddRef() {
  return UiWindow::AddRef();
}

u32 WnAppCombo::Release() {
  return UiWindow::Release();
}

// Return the borrowed primary window from the secondary control. Its reference count stays unchanged.
WnWindow *WnAppCombo::GetWindow() {
  return static_cast<WnWindow *>(this);
}

// Obtain the embedded text through its real child query. Release the temporary hold before returning the borrowed view.
WnTextControl *WnAppCombo::LookupLineInput() {
  void *slot = nullptr;
  if (!GetChildAs(kWnComboInputId, IID_WnTextControl, &slot))
    return nullptr;
  static_cast<WnTextControl *>(slot)->Release();
  return static_cast<WnTextControl *>(slot);
}

// Return an existing borrowed input when available. Otherwise create it and retry the lookup only after success.
WnTextControl *WnAppCombo::GetLineInput() {
  WnTextControl *text = LookupLineInput();
  if (!text && CreateLineInput())
    text = LookupLineInput();
  return text;
}

// Release the old field before publishing and retaining the new font. Forward the live field to an existing input.
void WnAppCombo::SetFont(FtFont *incoming) {
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
bool WnAppCombo::SetFontColor(i32 index, u32 color) {
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

bool WnAppCombo::ClearFontColor(i32 index) {
  return SetFontColor(index, kWnDefaultColor);
}

// Store the highlight pixel. Then forward it to an existing input's selection-color operation.
bool WnAppCombo::SetHighlightColor(u32 color) {
  highlight_color = color;
  WnTextControl *text = LookupLineInput();
  if (text)
    text->SetHighlightFillColor(color);
  return true;
}

// Map negative or unordered opacity to zero and clamp values above one. Forward the live stored value to the input.
bool WnAppCombo::SetHighlightFillAlpha(f32 opacity) {
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

bool WnAppCombo::GetEditable() const {
  return editable;
}

bool WnAppCombo::SetEditable(bool enabled) {
  WnTextControl *text = LookupLineInput();
  if (text)
    text->SetEditable(enabled);
  editable = enabled;
  return true;
}

bool WnAppCombo::SetDropDownOutlineColor(u32 color) {
  dropdown_outline = color;
  return true;
}

// Publish the arrow-button color. Always invalidate the parent chain and report success.
bool WnAppCombo::SetComboDownColor(u32 color) {
  arrow_color = color;
  InvalidateSelfAndParents();
  return true;
}

// Store the signed gutter. Capture height before width and invoke live layout, ignoring its result.
bool WnAppCombo::SetBtnGutter(i8 gutter) {
  button_gutter = gutter;
  i32 height = GetH(), width = GetW();
  Layout(width, height);
  return true;
}

// Store both signed gutters. For an existing input, set symmetric margins before resizing the combo through SetSize.
bool WnAppCombo::SetTextGutters(i8 horizontal, i8 vertical) {
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
bool WnAppCombo::GetMinMaxInfo(ScRect *limits) {
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

// Clamp requested dimensions to the source minima. Lay out changed sizes before moving the root; equal sizes keep requested edges.
void WnAppCombo::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  ScRect limits;
  GetMinMaxInfo(&limits);
  if (width < limits.left)
    width = limits.left;
  if (height < limits.top)
    height = limits.top;
  if (width == GetW() && height == GetH()) {
    UiWindow::SetArea(left, top, right, bottom);
  } else {
    Layout(width, height);
    UiWindow::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  }
}

// Convert the pixel through the qualified root setter. Forward the original pixel to the existing input window.
void WnAppCombo::SetFillColor(u32 color) {
  UiWindow::SetFillColor(color);
  WnTextControl *text = LookupLineInput();
  if (text)
    text->AsIGZWin()->SetFillColor(color);
}

bool WnAppCombo::Shutdown() {
  HideWindow();
  ScRelease(&arrow);
  ScRelease(&font);
  return UiWindow::Shutdown();
}

bool WnAppCombo::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

i32 WnAppCombo::GetSelection() const {
  return selection;
}

// Send command eleven with the live control ID to the current target. The source reports success even if delivery fails.
bool WnAppCombo::NotifyChanged() {
  u32 control_id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandComboSelectionChanged, control_id, 0);
  return true;
}

// Handle only command kind nine by dispatching the real changed notification. The source ignores the child identifier.
bool WnAppCombo::OnCommand(u32 kind, u32) {
  if (kind != kWnCommandTextAccepted)
    return false;
  NotifyChanged();
  return true;
}

u32 WnAppCombo::GetFontColor(i32 index) const {
  assert(static_cast<u32>(index) <= 2);
  return font_colors[index];
}

bool WnAppCombo::GetDropDownOutlineColor(u32 *color) const {
  *color = dropdown_outline;
  return true;
}

bool WnAppCombo::GetComboDownColor(u32 *color) const {
  *color = arrow_color;
  return true;
}

bool WnAppCombo::GetBtnGutter(i8 *gutter) const {
  *gutter = button_gutter;
  return true;
}

// Publish horizontal then vertical signed gutters, preserving the order when outputs alias.
bool WnAppCombo::GetTextGutters(i8 *horizontal, i8 *vertical) const {
  *horizontal = horizontal_gutter;
  *vertical = vertical_gutter;
  return true;
}

// Position the arrow and size the existing input. Invalidate after the callbacks and return success.
bool WnAppCombo::Layout(i32 width, i32 height) {
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

// Configure a registered input before attaching it to the root. Release the acquired interface after the attachment call.
bool WnAppCombo::CreateLineInput() {
  if (LookupLineInput())
    return true;
  WnResources *resources = ApGetUiResources();
  void *slot = nullptr;
  if (!ApGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, &slot))
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
bool WnAppCombo::SetComboDownArrow(GxBuffer *incoming, const ScRect *rectangle) {
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
bool WnAppCombo::LookupString(i32 index, const ScString **out) const {
  WnListNode *node = strings.At((u32)index);
  if (!node)
    return false;
  *out = &node->text;
  return true;
}

// Select a valid item and copy it through the input's real string setter. Notify after temporary text has been destroyed.
bool WnAppCombo::SetSelection(i32 index, bool notify) {
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

// Resolve missing defaults through one captured provider before creating and configuring the input. Warm calls skip work.
bool WnAppCombo::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnResources *resources = ApGetUiResources();
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
  return UiWindow::Init();
}

// Move selection for arrow keys, opening the dropdown for Alt-down. Forward other keys to the live notification target.
bool WnAppCombo::OnKeyDown(u32 key, u32 modifiers) {
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

bool WnAppCombo::OnMouseDownL(i32, i32, u32) {
  OpenDropDown();
  return true;
}

// Insert an owned concrete copy before the requested item; zero prepends and minus two appends. Other negative indices fail.
bool WnAppCombo::InsertString(const ScStringView &text, i32 index) {
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
bool WnAppCombo::RemoveString(i32 index) {
  WnListNode *node = strings.At((u32)index);
  if (!node)
    return false;
  strings.Erase(node);
  return index == 0;
}

// Find the first string through the live interface before delegating removal. Preserve the indexed method's unusual result.
bool WnAppCombo::RemoveString(const ScStringView &text) {
  i32 index;
  if (!GetIndexFromString(&index, text))
    return false;
  return RemoveString(index);
}

void WnAppCombo::RemoveAllStrings() {
  strings.Clear();
  selection = -1;
}

// Copy a found concrete string through the genuine output interface. Invalid indices leave that output untouched.
bool WnAppCombo::GetStringFromIndex(i32 index, ScStringView *text) const {
  const ScString *stored;
  if (!LookupString(index, &stored))
    return false;
  text->Copy(*stored);
  return true;
}

// Ask the input string to compare each stored item in order. Follow its live next link after callbacks can insert neighbors.
bool WnAppCombo::GetIndexFromString(i32 *index, const ScStringView &text) const {
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
bool WnAppCombo::GetSelection(i32 *index, ScStringView *text) const {
  *index = selection;
  GetStringFromIndex(selection, text);
  return true;
}

// Paint the arrow cell at its local destination, or a filled bevel when no atlas is installed. Failed atlas locking stops paint.
bool WnAppCombo::GZPaint() {
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

// Block enabled-bit mutations while the dropdown is open. Exact enabled changes also update the input and arrow cell.
u32 WnAppCombo::SetFlag(u32 mask, bool enabled) {
  if ((mask & 2) && drop_open)
    return 0;
  u32 result = UiWindow::SetFlag(mask, enabled);
  if (mask == kWnFlagEnabled) {
    WnTextControl *text = LookupLineInput();
    assert(text);
    text->AsIGZWin()->SetFlag(kWnFlagEnabled, enabled);
    arrow_source_x = enabled ? (i32)((u32)arrow_x + (u32)arrow_width) : arrow_x;
    InvalidateSelfAndParents();
  }
  return result;
}

// Configure the executable popup list from the combo, run it modally, and apply its selection before restoring the arrow.
bool WnAppCombo::OpenDropDown() {
  if (drop_open)
    return false;
  WnAppList *list = new WnAppList(true);
  list->AddRef();
  drop_open = 1;
  arrow_source_x = (i32)((u32)arrow_x + 2 * (u32)arrow_width);
  InvalidateSelfAndParents();
  WnAppListControl *control = list;
  control->AsIGZWin()->SetID(kWnComboPopupId);
  control->AsIGZWin()->SetFlag(kWnFlagPrivateBuffer, true);
  WnWindow *window = control->AsIGZWin();
  i32 bottom = absolute_area.bottom, height = GetH();
  window->SetArea(absolute_area.left, bottom, absolute_area.right, (i32)((u32)bottom + (u32)height));
  window = control->AsIGZWin();
  window->SetFillColor(GetFillColor());
  control->SetFont(font);
  if (font_colors[0] != kWnDefaultColor)
    control->SetFontColor(0, font_colors[0]);
  if (font_colors[1] != kWnDefaultColor)
    control->SetFontColor(1, font_colors[1]);
  if (font_colors[2] != kWnDefaultColor)
    control->SetFontColor(2, font_colors[2]);
  control->SetOutlineDrawing(true);
  control->SetOutlineColors(dropdown_outline, dropdown_outline);
  control->SetHighlightColor(highlight_color);
  control->SetHighlightFillAlpha(highlight_opacity);
  control->SetScrollbarGutters(0, 0);
  control->SetTextGutters(horizontal_gutter, vertical_gutter);
  for (WnListLink *node = strings.head->next; node != strings.head; node = node->next)
    list->InsertString(static_cast<WnListNode *>(node)->text, -2);
  control->SetSelection(selection, false);
  WnWindowManager *manager = GetWindowManager();
  i32 result = manager->DoModalWin(control->AsIGZWin());
  SetSelection(result, true);
  list->Shutdown();
  list->Release();
  drop_open = 0;
  arrow_source_x = (i32)((u32)arrow_x + (u32)arrow_width);
  InvalidateSelfAndParents();
  return true;
}

bool WnAppEditor::SetFontColor(WnFontColor index, u32 color) {
  font_colors[(i32)index] = color;
  return true;
}

bool WnAppEditor::SetFontColor(u32 color) {
  font_colors[0] = color;
  return true;
}

bool WnAppEditor::GetFontColor(WnFontColor index, u32 *color) const {
  *color = font_colors[(i32)index];
  return true;
}

bool WnAppEditor::ClearFontColor(WnFontColor index) {
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

u32 WnAppEditor::GetHighlightFillColor() {
  return highlight_color;
}

u32 WnAppEditor::GetMaximumTextToAllow() {
  return maximum_text;
}

u32 WnAppEditor::GetFirstVisibleLine() {
  return first_visible;
}

u32 WnAppEditor::GetLineCount() {
  return (u32)lines.size();
}

u32 WnAppEditor::GetLineHeight() {
  return line_height;
}

u32 WnAppEditor::GetTextGutterX() {
  return gutter_x;
}

u32 WnAppEditor::GetTextGutterY() {
  return gutter_y;
}

u32 WnAppEditor::GetTextLength() {
  return text.ScString::Strlen();
}

// Read the next line-offset difference or the last line's remaining bytes, without stripping its newline byte.
// Empty text returns zero before any line-table access; nonempty text requires the source's populated offset table.
u32 WnAppEditor::GetCharacterCountOfLine(u32 line) {
  u32 length = text.ScString::Strlen();
  if (!length)
    return 0;
  if (line < (u32)lines.size() - 1)
    return lines[line + 1] - lines[line];
  return length - lines[(u32)lines.size() - 1];
}

// Convert a byte index through the actual line offsets. A trailing newline after the last stored offset yields count,0.
// Preserve column-before-line zeroing for empty input and line-before-column writes for an ordinary match.
void WnAppEditor::GetLineAndColumnFromIndex(u32 *line, u32 *column, u32 index) {
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

// Clamp the requested byte index to the current text, derive its line and column, then always invalidate the parent chain.
bool WnAppEditor::SetInsertionIndex(u32 index) {
  u32 length = text.ScString::Strlen();
  insertion_index = ScUtf8Floor(text.begin, length, index);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  InvalidateSelfAndParents();
  return true;
}

// Clamp selection endpoints independently without sorting them, place insertion at the finish and always invalidate.
bool WnAppEditor::SetSelection(u32 start, u32 finish) {
  u32 length = text.ScString::Strlen();
  selection_finish = ScUtf8Floor(text.begin, length, finish);
  insertion_index = selection_finish;
  selection_start = ScUtf8Floor(text.begin, length, start);
  GetLineAndColumnFromIndex(&insertion_line, &insertion_column, insertion_index);
  InvalidateSelfAndParents();
  return true;
}

// Move upward only from a positive signed first-line value, then synchronize the live bar before invalidating parents.
bool WnAppEditor::ScrollLineUp() {
  if ((i32)first_visible <= 0)
    return false;
  --first_visible;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Move downward while the signed remaining-row calculation is positive.
bool WnAppEditor::ScrollLineDown() {
  if ((i32)((u32)lines.size() - visible_rows - first_visible) <= 0)
    return false;
  ++first_visible;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Reset a nonzero first line, including a negative raw word, and synchronize the bar before parent invalidation.
bool WnAppEditor::ScrollToStart() {
  if (!first_visible)
    return false;
  first_visible = 0;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Jump to the last complete viewport only while the source signed remainder is positive; do not add clamping otherwise.
bool WnAppEditor::ScrollToEnd() {
  if ((i32)((u32)lines.size() - visible_rows - first_visible) <= 0)
    return false;
  first_visible = (u32)lines.size() - visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

// Subtract a page from a nonzero first line, choosing zero when the signed first-line value is below the page size.
bool WnAppEditor::ScrollPageUp() {
  if (!first_visible)
    return false;
  first_visible = (i32)first_visible < (i32)visible_rows ? 0 : first_visible - visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

bool WnAppEditor::ScrollPageDown() {
  if ((i32)((u32)lines.size() - visible_rows - first_visible) <= 0)
    return false;
  first_visible =
      first_visible >= (u32)lines.size() - visible_rows ? (u32)lines.size() - 1 : first_visible + visible_rows;
  SetScrollBarValues(false);
  InvalidateSelfAndParents();
  return true;
}

void WnAppEditor::SetCaretColor(u32 color) {
  caret_color = color;
}

void WnAppEditor::SetHighlightFillColor(u32 color) {
  highlight_color = color;
}

void WnAppEditor::SetMaximumTextToAllow(u32 value) {
  maximum_text = value;
}

// Set only the outline flag and its stored color, retaining every other editor flag and viewport field.
void WnAppEditor::SetOutlineUse(bool enabled, u32 color) {
  if (enabled)
    editor_flags |= 0x10;
  else
    editor_flags &= ~0x10u;
  outline_color = color;
}

bool WnAppEditor::SetInsertMode(bool enabled) {
  insert_mode = enabled;
  return true;
}

void WnAppEditor::GetText(ScStringView *output) {
  output->FromChar(text.begin, text.Strlen());
}

bool WnAppEditor::IsFlagSet(i32 mask) {
  return (mask & editor_flags) != 0;
}

i32 WnAppEditor::GetFlags() {
  return editor_flags;
}

void WnAppEditor::SetMaxUndoLevelCount(u32 count) {
  undo_limit = count;
}

// Return the borrowed mutable start of the editor's current text storage.
char *WnAppEditor::GetText() {
  return text.begin;
}

WnAppEditor::WnAppEditor(i32 initial_flags) : elapsed(ScStopwatch::Milliseconds, 0) {
  editor_flags = 0;
  InitializeMemberVariables(initial_flags);
}

WnAppEditor::~WnAppEditor() {
  WnAppEditor::Shutdown();
}

// Resolve the missing font and reserve line storage before selecting the editable cursor, timer and language mode.
// An already initialized window returns immediately; inherited initialization follows the editor-specific effects.
bool WnAppEditor::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  if (!font)
    SetFont(ApGetUiResources()->GetFont(kWnFontDefault));
  lines.reserve(32);
  text_width = CalculateVisibleWidth();
  if (editor_flags & 1) {
    WnCursorManager *manager = owner->GetCursorManager();
    SetCursor(manager->GetIGZCursor(2), false);
  }
  if ((editor_flags & 0x40) && (editor_flags & 1) && !caret_timer.IsRunning())
    caret_timer.StartPeriodic(caret_period >> 1, nullptr);
  ScLanguageManager *manager = ApGetLanguageManager();
  language = manager ? manager->GetCurrentLanguage() : 1;
  double_byte = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
  return UiWindow::Init();
}

// Release the font, stop each running timer and wait in the original millisecond unit before releasing bars and images.
// Each pointer is cleared after its release callback; inherited window cleanup follows all editor-owned resources.
bool WnAppEditor::Shutdown() {
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
  return UiWindow::Shutdown();
}

// Initialize editor state in the source order, including its real mask string, font roles and typed timer callbacks.
// Finish through the live signed flag setter so timer, cursor and layout changes retain their original order.
void WnAppEditor::InitializeMemberVariables(i32 requested) {
  ApGetGraphics();
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
  UiWindow::SetFillColor(255, 255, 255);
  for (i32 i = 0; i < 6; ++i)
    ClearFontColor((WnFontColor)i);
  elapsed.SetResolution(ScStopwatch::Milliseconds);
  caret_period = 1000;
  double_click_ms = 500;
  caret_timer.SetCallbackFunction(CaretTimer, reinterpret_cast<uptr>(this));
  scroll_timer.SetCallbackFunction(ScrollTimer, reinterpret_cast<uptr>(this));
  SetFlags(requested);
}

// Normalize wrapping and single-line scroll flags, synchronize the caret timer, then relayout only the source change bits.
// Cursor selection follows invalidation and uses the requested editable bit with the borrowed manager cursor.
void WnAppEditor::SetFlags(i32 requested) {
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

bool WnAppEditor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnEditorControl, WnAppEditorControl);
  return UiWindow::QueryInterface(iid, out);
}

u32 WnAppEditor::AddRef() {
  return UiWindow::AddRef();
}

u32 WnAppEditor::Release() {
  return UiWindow::Release();
}

// Return the borrowed primary window from the actual secondary editor identity without another reference.
WnWindow *WnAppEditor::AsIGZWin() {
  return this;
}

// Store the root fill color first, then invalidate only when the editor's own opaque-text flag is set.
void WnAppEditor::SetFillColor(u32 color) {
  UiWindow::SetFillColor(color);
  if (editor_flags & 0x20)
    InvalidateSelfAndParents();
}

// Add a column to the selected offset, using the final stored line for out-of-range line indices.
bool WnAppEditor::GetIndexFromLineAndColumn(u32 line, u32 column, u32 *index) {
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

// Clamp a line/column against stored offsets and that line's full byte extent, then derive the insertion index.
// Empty line storage resets the three insertion fields; neither path adds an invalidation absent from the source.
bool WnAppEditor::SetInsertionLineAndColumn(u32 line, u32 column) {
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

// Synchronize the optional live vertical control. Recalculation queries the editor's virtual count twice around SetRange.
void WnAppEditor::SetScrollBarValues(bool recalculate) {
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

void WnAppEditor::CaretTimer(uptr editor) {
  ApGetDialogManager()->PostMessageToTarget(reinterpret_cast<WnAppEditor *>(editor), kWnMsg_EditorCaret, 0, 0, 0);
}

void WnAppEditor::ScrollTimer(uptr editor) {
  ApGetDialogManager()->PostMessageToTarget(reinterpret_cast<WnAppEditor *>(editor), kWnMsg_EditorScroll, 0, 0, 0);
}

// Copy up to the caller's byte capacity without a terminator. SDL stops before an incomplete UTF-8 scalar.
void WnAppEditor::GetText(char *output, u32 *length) {
  *length = ScUtf8Prefix(text.begin, text.Strlen(), *length);
  if (*length)
    memcpy(output, text.begin, *length);
}

void WnAppEditor::ClearFlag(i32 mask) {
  if (editor_flags & mask)
    SetFlags(editor_flags & ~mask);
}

// Measure the source viewport width after the live vertical window query, preserving wrapped subtraction of the two gutters.
u32 WnAppEditor::CalculateVisibleWidth() {
  i32 width = vertical_scrollbar ? vertical_scrollbar->GetWindow()->GetW() : 0;
  u32 window_width = (u32)GetW();
  return window_width - 2 * gutter_x - (u32)width;
}

// Preserve the original asymmetric height dependency: a horizontal bar enables subtraction of the vertical bar's height.
// A horizontal-only state dereferences an unavailable vertical receiver in the original and is outside defined execution.
u32 WnAppEditor::CalculateVisibleHeight() {
  assert(!horizontal_scrollbar || vertical_scrollbar);
  i32 height = horizontal_scrollbar ? vertical_scrollbar->GetWindow()->GetH() : 0;
  u32 window_height = (u32)GetH();
  return window_height - 2 * gutter_y - (u32)height;
}

// Recompute the font height, viewport and line suffix before live scrollbar synchronization and caret normalization.
// Scrollbar visibility follows the first-visible clamp and may recursively relayout after creation or removal.
void WnAppEditor::RecalculateAll() {
  line_height = font ? font->Height(0) : 16;
  assert(line_height);
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
void WnAppEditor::FixInsertionPositionIfBroken() {
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

// Store root geometry first, then refresh text width, full layout and scrollbar placement before final invalidation.
// Repeated width and synchronization calls are retained because the source observes the live virtual window state.
void WnAppEditor::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiWindow::SetArea(left, top, right, bottom);
  text_width = CalculateVisibleWidth();
  RecalculateAll();
  SetScrollBarSizes();
  SetScrollBarValues(true);
  InvalidateSelfAndParents();
}

// Publish the stored text bounds after capturing every source field, preserving defined output aliases.
void WnAppEditor::GetTextArea(ScRect *output) {
  u32 left = gutter_x, top = gutter_y, width = text_width, height = text_height;
  output->left = (i32)left;
  output->top = (i32)top;
  output->right = (i32)(left + width);
  output->bottom = (i32)(top + height);
}

// Retain the incoming face before releasing the live old field, including same-font replacement.
// Publish it before checking initialized state; only initialized editors relayout and invalidate here.
void WnAppEditor::SetFont(FtFont *incoming) {
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
void WnAppEditor::SetTextGutterX(u32 value) {
  if (gutter_x == value)
    return;
  gutter_x = value;
  text_width = CalculateVisibleWidth();
  RecalculateAll();
  if (flags & 0x4000)
    InvalidateSelfAndParents();
}

// Store a changed vertical gutter without recomputing layout; initialized editors are invalidated afterward.
void WnAppEditor::SetTextGutterY(u32 value) {
  if (gutter_y == value)
    return;
  gutter_y = value;
  if (flags & 0x4000)
    InvalidateSelfAndParents();
}

// Round the current inner height down to full stored line heights, preserving unsigned gutter arithmetic.
bool WnAppEditor::ResizeWindowForExactLineHeights() {
  assert(line_height);
  u32 gutters = 2 * gutter_y, height = (u32)GetH() - gutters;
  u32 fitted = line_height * (height / line_height);
  if (fitted != height)
    SetH((i32)(fitted + gutters));
  SetScrollBarSizes();
  SetScrollBarValues(true);
  InvalidateSelfAndParents();
  return true;
}

void WnAppEditor::StartScrollTimer() {
  if (!scroll_timer.IsRunning())
    scroll_timer.StartPeriodic(250, nullptr);
}

void WnAppEditor::StopScrollTimer() {
  if (scroll_timer.IsRunning())
    scroll_timer.Stop();
}

// Move insertion back one byte and column when it lies immediately after a newline within a nonzero column.
void WnAppEditor::MoveInsertionPositionToBeforeReturnIfNecessary() {
  if (insertion_index && insertion_column && text.begin[insertion_index - 1] == '\n') {
    --insertion_column;
    --insertion_index;
  }
}

// Keep the insertion line within the vertical viewport, synchronizing only when its first row changes.
void WnAppEditor::PutInsertionPositionInView() {
  u32 first = insertion_line;
  if (first >= first_visible) {
    if (first < visible_rows + first_visible)
      return;
    first = first - visible_rows + 1;
  }
  first_visible = first;
  SetScrollBarValues(false);
}

// Add flags only when none of the requested bits are currently present; retain the original partial-overlap no-op.
// The original signed flag argument and live SetFlags dispatch preserve all32 flag bits.
void WnAppEditor::SetFlag(i32 requested) {
  i32 current = editor_flags;
  if (!(current & requested))
    SetFlags(current | requested);
}

// Store the source's250..10000 millisecond period and restart caret blinking only on an initialized window.
// Restart uses the actual clear/add flag virtuals, preserving their timer and layout side effects.
bool WnAppEditor::SetCaretPeriod(u32 milliseconds) {
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

// Recognize the original language19 continuation bytes; the zero table terminator is excluded.
[[maybe_unused]] static bool WnAppEditorContinuation(u8 code) {
  static const u8 kContinuationBytes[] = {
      0xD1, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0};
  for (const u8 *entry = kContinuationBytes; *entry; ++entry)
    if (*entry == code)
      return true;
  return false;
}

// Classify the editor's own language-specific lead values, whose ranges differ from the single-line text control.
// Language19 consumes only the low byte while the other languages compare the entire input word.
[[maybe_unused]] static bool WnAppEditorLeadByte(i32 language, u32 code) {
  switch (language) {
  case 15:
    return (code >= 0x81 && code <= 0x9F) || (code >= 0xE0 && code <= 0xFC);
  case 17:
  case 20:
    return code >= 0x81 && code <= 0xFE;
  case 18:
    return code >= 0xA1 && code <= 0xFE;
  case 19:
    return WnAppEditorContinuation((u8)code);
  default:
    return false;
  }
}

// Rebuild the line-offset suffix through the real font fitter, retaining existing offsets before the requested line.
// Preserve the source's nonwrapped scan and terminal-newline behavior, including its remaining-span offset calculation.
void WnAppEditor::CalculateAllLineLengths(u32 first_line) {
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
void WnAppEditor::AdjustLinePositionArray(u32 increment, u32 count) {
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

// Each create/remove path owns recursive layout and reference updates; the original returns true on every path.
bool WnAppEditor::TestForPossibleScrollBarAdditionOrRemoval() {
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
bool WnAppEditor::AddScrollBar(bool vertical, bool horizontal) {
  bool changed = false;
  if (vertical && (editor_flags & 8) && !vertical_scrollbar) {
    changed = true;
    vertical_scrollbar = ApGetUiResources()->CreateScrollbar(0, 2, 0, 100, 0);
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
    horizontal_scrollbar = ApGetUiResources()->CreateScrollbar(1, 1, 0, 100, 0);
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
void WnAppEditor::RemoveScrollBar(bool vertical, bool horizontal) {
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
void WnAppEditor::SetScrollBarSizes() {
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

// Measure full line-byte extents through the real visible-width method, initializing its genuine output length each time.
// A false mode uses the original unclamped viewport range; only actual returned unsigned widths select the maximum.
u32 WnAppEditor::GetPixelWidthOfLongestLine(bool all_lines) {
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

// Return the original one-to-three-byte character span, with language19 bounded lookahead and the other lead ranges.
// SDL returns a complete UTF-8 scalar width; the reference two-byte result is not clamped to the supplied count.
u32 WnAppEditor::NextCharSizeFromText(const char *bytes, u32 length) {
  return ScUtf8Decode(bytes, length, nullptr);
}

// Scan the preceding bytes with the source's two-state lead-byte rule, returning zero only for no preceding bytes.
// SDL steps backward over a complete UTF-8 scalar; reference language19 may consume three bytes going forward.
u32 WnAppEditor::PreviousCharSizeFromText(const char *bytes, u32 length) {
  return length ? ScUtf8PreviousSize(bytes - length, length) : 0;
}

// Convert a local point into the original line and nearest measured byte column without changing the insertion state.
// SDL advances over complete UTF-8 characters; reference builds retain the original repeated line-start query.
void WnAppEditor::GetLineAndColumnFromPosition(u32 *line, u32 *column, i32 x, i32 y) {
  assert(line_height);
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

// While editable and captured, start drag scrolling, select the measured point and publish the adjusted finish index.
// Editable movement without capture stops drag scrolling; every path returns the source's constant success.
bool WnAppEditor::OnMouseMove(i32 x, i32 y, u32) {
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
bool WnAppEditor::SelectCurrentWord() {
  if (text.begin == text.end)
    return false;
  const char *begin = text.begin;
  u32 index = insertion_index;
  assert(index <= text.ScString::Strlen());
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
bool WnAppEditor::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
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
bool WnAppEditor::OnMouseUpL(i32, i32, u32) {
  if ((editor_flags & 1) && owner->GetCapture() == this) {
    owner->ReleaseCapture(this);
    StopScrollTimer();
  }
  return true;
}

// Convert each complete signed32 wheel hundred into a line scroll while this window does not own capture.
// Scroll results are ignored, so all requested iterations run even after the viewport reaches its limit.
bool WnAppEditor::GZOnMouseWheel(i32, i32, u32, i32 wheel) {
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
bool WnAppEditor::OnKeyUp(u32 key, u32 modifiers) {
  if (key == kWnKeyEscape)
    return SendMsg(notification_target, kWnMsg_KeyUp, kWnKeyEscape, modifiers, 0);
  return true;
}

// Invalidate editable state on focus loss before preserving the root's unhandled result.
bool WnAppEditor::GZOnKillFocus(WnWindow *window) {
  string236.Erase(0, kScStringToEnd);
  if (editor_flags & 1)
    InvalidateSelfAndParents();
  return UiWindow::GZOnKillFocus(window);
}

// Invalidate editable state before the root's hidden-window focus redirection and unhandled result.
bool WnAppEditor::GZOnSetFocus(WnWindow *window) {
  if (editor_flags & 1)
    InvalidateSelfAndParents();
  return UiWindow::GZOnSetFocus(window);
}

void WnAppEditor::ToggleCaretVisibility() {
  if (owner->GetFocus() == this) {
    caret_visible = !caret_visible;
    InvalidateSelfAndParents();
  }
}

// Scroll a captured pointer beyond the original three-pixel vertical thresholds, then reenter the real drag method.
void WnAppEditor::DoCursorScroll() {
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
bool WnAppEditor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_EditorCaret) {
    ToggleCaretVisibility();
    return true;
  }
  if (message->type == kWnMsg_EditorScroll) {
    DoCursorScroll();
    return true;
  }
  return UiWindow::DoMessage(message);
}

// Publish the actual character span before applying the language17/20 lead-byte restriction.
// Single-byte spans and other languages are accepted without changing their bytes.
bool WnAppEditor::IsCharOK(const char *bytes, u32 length, u32 *count) {
  *count = ScUtf8Decode(bytes, length, nullptr);
  return *count != 0;
}

// Accumulate accepted character spans, excluding disallowed leads and newlines under flag200.
u32 WnAppEditor::RemoveBadCharacters(ScString *value) {
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

void WnAppEditor::AddFirstLineLength() {
  if (!(u32)lines.size())
    lines.push_back(0);
}

bool WnAppEditor::IsCharOK(u8 character) {
  return character >= 0x20 || character == 10;
}

// Measure the clamped line prefix before publishing its x coordinate, then compute y from the live viewport fields.
void WnAppEditor::GetPositionFromLineAndColumn(u32 line, u32 column, i32 *x, i32 *y) {
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
void WnAppEditor::GetColumnFromPosition(u32 line, u32 *column, i32 x) {
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
