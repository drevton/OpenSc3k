#include "ui/controls.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#include "application/api.h"
#include "audio/api.h"
#include "common/framework.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "graphics/sc3_effects.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"

// Fixed-option lists reserve this identity for an empty selection and reject it as a child option.
enum UiFixedOptionSelection : u32 {
  kUiFixedOptionNoSelectionId = 0x5907d22,
};

UiBorderPanel::UiBorderPanel() : layout_mode(0), border_mode(0), border_colors{} {
}

UiBorderPanel::~UiBorderPanel() {
  UiBaseWindow::Shutdown();
}

// Publish the genuine secondary layout identity before retaining the shared owner; base identities remain canonical.
bool UiBorderPanel::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnFlatControl)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnFlatControl *>(this);
  AddRef();
  return true;
}

u32 UiBorderPanel::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiBorderPanel::Release() {
  return UiBaseWindow::Release();
}

// Return the borrowed window from the secondary interface without acquiring another reference.
WnWindow *UiBorderPanel::GetWindow() {
  return this;
}

// Translate the three forwarded left-mouse messages into parent coordinates before dispatching the same live packet.
bool UiBorderPanel::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_MouseDownL || message->type == kWnMsg_MouseUpL || message->type == kWnMsg_MouseMove) {
    message->data1 = (u32)message->data1 + (u32)GetL();
    message->data2 = (u32)message->data2 + (u32)GetT();
  }
  WnWindow *target = parent;
  return target ? target->DoMessage(message) : false;
}

bool UiBorderPanel::OnKeyDown(u32 key, u32 modifiers) {
  ScMessage message{kWnMsg_KeyDown, key, modifiers, 0};
  return notification_target->DoMessage(&message);
}

bool UiBorderPanel::OnKeyUp(u32 key, u32 modifiers) {
  ScMessage message{kWnMsg_KeyUp, key, modifiers, 0};
  return notification_target->DoMessage(&message);
}

void UiBorderPanel::SetFillColor(u8 red, u8 green, u8 blue) {
  UiBaseWindow::SetFillColor(red, green, blue);
}

void UiBorderPanel::SetFillColor(u32 pixel) {
  UiBaseWindow::SetFillColor(pixel);
}

void UiBorderPanel::GetFillColor(u8 *red, u8 *green, u8 *blue) const {
  WnRootWidget::GetFillColor(red, green, blue);
}

u32 UiBorderPanel::GetFillColor() const {
  return UiBaseWindow::GetFillColor();
}

u8 UiBorderPanel::SetSizeMode(u8 mode) {
  layout_mode = mode;
  return true;
}

u8 UiBorderPanel::GetSizeMode() {
  return layout_mode;
}

// Apply the selected subset of supplied edges to the live outer area, then clamp negative dimensions.
// Modes one, two and six retain every existing edge; zero skips the area getter and resize entirely.
u8 UiBorderPanel::AutoSize(const ScRect &rectangle) {
  if (!layout_mode)
    return 1;
  ScRect current;
  GetArea(&current);
  switch (layout_mode) {
  case 5:
    current.left = rectangle.left;
    [[fallthrough]];
  case 3:
    current.right = rectangle.right;
    break;
  case 4:
    current.bottom = rectangle.bottom;
    break;
  case 1:
  case 2:
  case 6:
    break;
  default:
    return 0;
  }
  if (current.bottom < current.top)
    current.bottom = current.top;
  if (current.right < current.left)
    current.right = current.left;
  SetArea(current.left, current.top, current.right, current.bottom);
  return 1;
}

u8 UiBorderPanel::GetOutlineFlags() {
  return border_mode;
}

u8 UiBorderPanel::GetOutlineFlag(i32 mask) {
  return (border_mode & mask) != 0;
}

static void UiUnpackBorderPixel(u32 pixel, u8 *red, u8 *green, u8 *blue) {
  UiGetGraphics()->Convert16BitNativeValueToRGB((u16)pixel, red, green, blue);
}

// Pack the three bytes into logical RGB and assign all four edge colors in source store order.
void UiBorderPanel::SetOutlineColor(u8 red, u8 green, u8 blue) {
  u32 color = (u32)red << 16 | (u32)green << 8 | blue;
  border_colors[3] = color;
  border_colors[2] = color;
  border_colors[1] = color;
  border_colors[0] = color;
}

// Unpack one graphics-format pixel before dispatching the current three-byte color setter.
void UiBorderPanel::SetOutlineColor(u32 pixel) {
  u8 red, green, blue;
  UiUnpackBorderPixel(pixel, &red, &green, &blue);
  SetOutlineColor(red, green, blue);
}

// Reuse the same channel bytes for all edges, storing each color before acquiring the next graphics view.
void UiBorderPanel::SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) {
  const u32 pixels[4] = {left, top, right, bottom};
  u8 red, green, blue;
  for (u32 index = 0; index < 4; ++index) {
    UiUnpackBorderPixel(pixels[index], &red, &green, &blue);
    u32 color = (u32)red << 16 | (u32)green << 8 | blue;
    border_colors[index] = color;
  }
}

// Convert each current logical edge color through its own graphics acquisition and write the genuine DWORD outputs.
void UiBorderPanel::GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) {
  u32 *outputs[4] = {left, top, right, bottom};
  for (u32 index = 0; index < 4; ++index) {
    GxGraphics *graphics = UiGetGraphics();
    u32 color = border_colors[index];
    u32 pixel = graphics->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
    *outputs[index] = pixel;
  }
}

// Store the selected logical RGB edge color; unsupported selectors leave all colors unchanged.
void UiBorderPanel::SetOutlineColor(u8 edge, u8 red, u8 green, u8 blue) {
  if (edge > 3)
    return;
  border_colors[edge] = (u32)red << 16 | (u32)green << 8 | blue;
}

// Publish the selected RGB bytes in order and leave unsupported selectors untouched.
void UiBorderPanel::GetOutlineColor(u8 edge, u8 *red, u8 *green, u8 *blue) const {
  if (edge > 3)
    return;
  *red = (u8)(border_colors[edge] >> 16);
  *green = (u8)(border_colors[edge] >> 8);
  u8 value = (u8)border_colors[edge];
  *blue = value;
}

// Store the byte mode before optional theme-color lookup and live four-pixel border assignment.
// Raised/sunken styles acquire the provider once and force all four edge bits after dispatch.
u8 UiBorderPanel::SetOutlineFlags(u8 mode) {
  border_mode = mode;
  if (mode & 0x30) {
    WnResources *resources = UiGetWindowResources();
    u32 dark = 0, light = 0xffff;
    if (resources) {
      dark = resources->GetColor(19);
      light = resources->GetColor(20);
    }
    if (border_mode & 0x10)
      SetOutlineColors(light, light, dark, dark);
    else if (border_mode & 0x20)
      SetOutlineColors(dark, dark, light, light);
    border_mode |= 0xf;
  }
  return true;
}

static i32 UiSliderDivide(i32 numerator, i32 denominator) {
  assert(denominator && (numerator != INT_MIN || denominator != -1));
  return numerator / denominator;
}

UiBudgetSliderWindow::UiBudgetSliderWindow()
    : value(0), maximum(0), minimum(0), marker(0), bar_atlas(nullptr), thumb_atlas(nullptr), marker_image(nullptr),
      thumb_width(0), bar_width(0), bar_source{}, thumb_source{}, marker_area{}, thumb_area{}, hovered(0), pressed(0) {
}

UiBudgetSliderWindow::~UiBudgetSliderWindow() {
}

// Construct the zero-reference owner and publish its separate slider identity without retaining or querying it.
UiBudgetSlider *UiBudgetSliderWindow::CreateNewObject() {
  return static_cast<UiBudgetSlider *>(new UiBudgetSliderWindow);
}

// Adapt the real secondary factory result to the director's existing unknown-returning class registration callback.
ScUnknown *UiCreateClass230391B9() {
  return UiBudgetSliderWindow::CreateNewObject();
}

// Publish the slider's secondary identity for its own IID and delegate all other queries to the actual window base.
bool UiBudgetSliderWindow::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiBudgetSlider)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiBudgetSlider *>(this);
  AddRef();
  return true;
}

u32 UiBudgetSliderWindow::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiBudgetSliderWindow::Release() {
  return UiBaseWindow::Release();
}

// Return the primary window borrowed from the separately addressed slider view without changing its reference count.
WnWindow *UiBudgetSliderWindow::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Clamp to the live maximum then minimum, publish the value, and notify only when the stored value changes.
u8 UiBudgetSliderWindow::SetValue(i32 next) {
  if (next > maximum)
    next = maximum;
  if (next < minimum)
    next = minimum;
  bool changed = next != value;
  value = next;
  if (changed) {
    UpdateMetrics();
    SendSliderChangedMessage();
    InvalidateSelfAndParents();
  }
  return 1;
}

i32 UiBudgetSliderWindow::GetValue() const {
  return value;
}

// Clamp the marker in the original minimum-then-maximum order and refresh only when its stored value changes.
u8 UiBudgetSliderWindow::SetMarkerValue(i32 next) {
  if (next < minimum)
    next = minimum;
  if (next > maximum)
    next = maximum;
  bool changed = next != marker;
  marker = next;
  if (changed) {
    UpdateMetrics();
    InvalidateSelfAndParents();
  }
  return 1;
}

u8 UiBudgetSliderWindow::GetMarkerValue(i32 *out) const {
  *out = marker;
  return 1;
}

// Sort the two signed endpoints, publish minimum before maximum, and refresh without clamping existing values.
u8 UiBudgetSliderWindow::SetMinMax(i32 first, i32 second) {
  if (second < first) {
    minimum = second;
    maximum = first;
  } else {
    minimum = first;
    maximum = second;
  }
  UpdateMetrics();
  InvalidateSelfAndParents();
  return 1;
}

// Write minimum before reading and writing the live maximum through the original two output pointers.
u8 UiBudgetSliderWindow::GetMinMax(i32 *out_minimum, i32 *out_maximum) const {
  *out_minimum = minimum;
  *out_maximum = maximum;
  return 1;
}

// Acquire and retain the bar, thumb and marker resources in nested scope order before initializing window geometry.
// Failed image checks leave any earlier retained fields intact for the original explicit Shutdown path.
bool UiBudgetSliderWindow::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  GxColorType bar_format(GxColorType::Color::RGB16);
  UiImageRef bar(5, kRsOtherControlImageGroup, GxVramRequest::SystemMemory, bar_format);
  bar_atlas = bar.GetBuffer();
  if (!bar_atlas)
    return false;
  bar_atlas->AddRef();
  if (bar_atlas->Width() % 3)
    return false;
  bar_width = (i16)(bar_atlas->Width() / 3);
  i32 bar_height = (i32)bar_atlas->Height();
  bar_source = {0, 0, bar_width, bar_height};
  GxColorType thumb_format(GxColorType::Color::RGB16);
  UiImageRef thumb(28, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, thumb_format);
  thumb_atlas = thumb.GetBuffer();
  if (!thumb_atlas)
    return false;
  thumb_atlas->AddRef();
  if (thumb_atlas->Width() & 3)
    return false;
  thumb_width = (i16)(thumb_atlas->Width() >> 2);
  i32 thumb_height = (i32)thumb_atlas->Height();
  thumb_source = {0, 0, thumb_width, thumb_height};
  thumb_height = (i32)thumb_atlas->Height();
  thumb_area = {0, 4, thumb_width, (i32)((u32)thumb_height + 4)};
  GxColorType marker_format(GxColorType::Color::RGB16);
  UiImageRef marker_ref(6, kRsOtherControlImageGroup, GxVramRequest::SystemMemory, marker_format);
  marker_image = marker_ref.GetBuffer();
  if (!marker_image)
    return false;
  marker_image->AddRef();
  marker_image->GetBufferArea(&marker_area);
  UiBaseWindow::Init();
  i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
  SetArea(left, top, right, bottom);
  UpdateMetrics();
  return true;
}

// Hide first, retire each live image in order, and then run the actual base-window shutdown sequence.
bool UiBudgetSliderWindow::Shutdown() {
  HideWindow();
  ScRelease(&bar_atlas);
  ScRelease(&thumb_atlas);
  ScRelease(&marker_image);
  return UiBaseWindow::Shutdown();
}

// With a bar image, enforce the source's two-cap minimum width and bar-image height before updating slider metrics.
void UiBudgetSliderWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  if (bar_atlas) {
    i32 width = (i32)((u32)right - (u32)left), minimum_width = 2 * bar_width;
    if (width < minimum_width)
      width = minimum_width;
    i32 height = (i32)((u32)bar_source.bottom - (u32)bar_source.top);
    WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  } else
    WnRootWidget::SetArea(left, top, right, bottom);
  UpdateMetrics();
}

// Capture the value before fetching the live window ID, then send the original synchronous slider-change envelope.
u8 UiBudgetSliderWindow::SendSliderChangedMessage() {
  i32 captured = value;
  u32 id = GetID();
  return SendMsg(notification_target, kWnMsg_BudgetSliderChanged, id, (u32)captured, 0);
}

// Place the thumb and marker using wrapped signed32 products and the original marker-width callback order.
// A missing marker leaves the existing rectangles unchanged; zero value range places both left edges at zero.
u8 UiBudgetSliderWindow::UpdateMetrics() {
  if (!marker_image)
    return 1;
  i32 thumb_x = 0, marker_x = 0;
  i32 range = (i32)((u32)maximum - (u32)minimum);
  if (range) {
    i32 span = (i32)((u32)GetW() - 4u - (u32)thumb_width);
    i32 captured_minimum = minimum, marker_offset = (i32)((u32)marker - (u32)captured_minimum);
    i32 value_offset = (i32)((u32)value - (u32)captured_minimum);
    i32 value_product = (i32)((u32)span * (u32)value_offset);
    thumb_x = (i32)((u32)UiSliderDivide(value_product, range) + 2);
    u32 image_width = marker_image->Width();
    u32 center = ((u32)thumb_width - image_width) >> 1;
    i32 marker_product = (i32)((u32)span * (u32)marker_offset);
    marker_x = (i32)(center + (u32)UiSliderDivide(marker_product, range) + 2);
  }
  i32 width = thumb_width;
  GxBuffer *captured_marker = marker_image;
  thumb_area.left = thumb_x;
  thumb_area.right = (i32)((u32)thumb_x + (u32)width);
  marker_area.left = marker_x;
  marker_area.right = (i32)((u32)marker_x + captured_marker->Width());
  return 1;
}

// Return the drag or inclusive thumb-hit state, otherwise distinguish positions left and right of the thumb.
u8 UiBudgetSliderWindow::GetMousePosition(i32 x, i32 y) {
  if (pressed == 3)
    return 3;
  if (x >= thumb_area.left && y >= thumb_area.top && x <= thumb_area.right && y <= thumb_area.bottom)
    return 3;
  return x < thumb_area.left ? 1 : 2;
}

// Map horizontal mouse position to the live signed range, retaining the ignored second coordinate in the contract.
// Fetch width again before reading the live dimensions for the interior calculation, as the original callbacks can reenter.
i32 UiBudgetSliderWindow::GetValueFromMousePosition(i32 x, i32) {
  if (x < 2)
    return minimum;
  if (x > (i32)((u32)GetW() - 2))
    return maximum;
  i32 width = GetW(), thumb = thumb_width, captured_minimum = minimum, captured_maximum = maximum;
  i32 span = (i32)((u32)width - 4u - (u32)thumb);
  i32 offset = (i32)((u32)x - (u32)(thumb / 2) - 2u);
  i32 range = (i32)((u32)captured_maximum - (u32)captured_minimum);
  i32 product = (i32)((u32)range * (u32)offset);
  return (i32)((u32)captured_minimum + (u32)UiSliderDivide(product, span));
}

// Move one signed step for left/right arrows through the live slider interface, then clear the hover state.
// Other keys are forwarded unchanged to the current notification target with the original key-down message type.
bool UiBudgetSliderWindow::OnKeyDown(u32 key, u32 modifiers) {
  if (key == kWnKeyLeft || key == kWnKeyRight) {
    UiBudgetSlider *slider = static_cast<UiBudgetSlider *>(this);
    i32 current = slider->GetValue();
    slider->SetValue((i32)((u32)current + (key == kWnKeyLeft ? kScBackwardGridStep : 1u)));
    hovered = 0;
    return true;
  }
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiBudgetSliderWindow::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Request capture before publishing the drag state, then set the value obtained from the original mouse coordinates.
bool UiBudgetSliderWindow::OnMouseDownL(i32 x, i32 y, u32) {
  GZSetCapture();
  UiBudgetSlider *slider = static_cast<UiBudgetSlider *>(this);
  pressed = 3;
  i32 next = GetValueFromMousePosition(x, y);
  slider->SetValue(next);
  return true;
}

// Clear the drag state before releasing capture and invalidating the live window; the source returns false afterward.
bool UiBudgetSliderWindow::OnMouseUpL(i32, i32, u32) {
  pressed = 0;
  GZReleaseCapture();
  InvalidateSelfAndParents();
  return false;
}

// During a drag update the value; otherwise acquire or release capture according to the virtual hit test.
// Capture the old hover only after those callbacks, then invalidate if the fresh mouse classification changed.
bool UiBudgetSliderWindow::OnMouseMove(i32 x, i32 y, u32) {
  if (pressed == 3) {
    UiBudgetSlider *slider = static_cast<UiBudgetSlider *>(this);
    i32 next = GetValueFromMousePosition(x, y);
    slider->SetValue(next);
  } else if (IsPointInWindow(x, y))
    GZSetCapture();
  else
    GZReleaseCapture();
  u8 old_hover = hovered;
  u8 next_hover = GetMousePosition(x, y);
  hovered = next_hover;
  if (next_hover != old_hover)
    InvalidateSelfAndParents();
  return true;
}

// Clear both interaction bytes when a different window takes capture, without issuing an extra repaint.
bool UiBudgetSliderWindow::GZOnCaptureChanged(WnWindow *window) {
  if (window != static_cast<WnWindow *>(this)) {
    pressed = 0;
    hovered = 0;
  }
  return true;
}

// Draw the left cap, repeated middle, right cap, marker, and the selected thumb frame under one blit lock.
// Re-read live image, geometry and interaction fields between calls so callback ordering matches the installed owner.
bool UiBudgetSliderWindow::GZPaint() {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  ScRect source = bar_source, destination = bar_source;
  i32 left = draw_area.left, right = draw_area.right, width = bar_width;
  i32 right_edge = (i32)((u32)right - (u32)width);
  destination.left = (i32)((u32)destination.left + (u32)left);
  destination.right = (i32)((u32)destination.right + (u32)left);
  i32 top = draw_area.top;
  destination.top = (i32)((u32)destination.top + (u32)top);
  destination.bottom = (i32)((u32)destination.bottom + (u32)top);
  draw_buffer->Blt(bar_atlas, source, destination, nullptr);
  width = bar_width;
  destination.left = (i32)((u32)destination.left + (u32)width);
  destination.right = (i32)((u32)destination.right + (u32)width);
  source.left = (i32)((u32)source.left + (u32)width);
  source.right = (i32)((u32)source.right + (u32)width);
  while (destination.right < right_edge) {
    draw_buffer->Blt(bar_atlas, source, destination, nullptr);
    width = bar_width;
    destination.left = (i32)((u32)destination.left + (u32)width);
    destination.right = (i32)((u32)destination.right + (u32)width);
  }
  draw_buffer->Blt(bar_atlas, source, destination, nullptr);
  width = bar_width;
  destination.right = draw_area.right;
  destination.bottom = draw_area.bottom;
  destination.left = (i32)((u32)destination.right - (u32)width);
  destination.top = draw_area.top;
  i32 source_width = (i32)((u32)bar_source.right - (u32)bar_source.left);
  source.left = (i32)((u32)source.left + (u32)source_width);
  source.right = (i32)((u32)source.right + (u32)source_width);
  draw_buffer->Blt(bar_atlas, source, destination, nullptr);
  destination = marker_area;
  top = draw_area.top;
  left = draw_area.left;
  destination.top = (i32)((u32)destination.top + (u32)top);
  destination.bottom = (i32)((u32)destination.bottom + (u32)top);
  destination.left = (i32)((u32)destination.left + (u32)left);
  destination.right = (i32)((u32)destination.right + (u32)left);
  const ScRect *marker_source = marker_image->GetBufferArea();
  draw_buffer->Blt(marker_image, *marker_source, destination, nullptr);
  source = thumb_source;
  destination = thumb_area;
  top = draw_area.top;
  left = draw_area.left;
  destination.top = (i32)((u32)destination.top + (u32)top);
  destination.bottom = (i32)((u32)destination.bottom + (u32)top);
  destination.left = (i32)((u32)destination.left + (u32)left);
  destination.right = (i32)((u32)destination.right + (u32)left);
  if (GetFlag(kWnFlagEnabled)) {
    i32 offset = thumb_width;
    if (pressed == 3)
      offset *= 2;
    else if (hovered == 3)
      offset *= 3;
    source.left = (i32)((u32)source.left + (u32)offset);
    source.right = (i32)((u32)source.right + (u32)offset);
  }
  draw_buffer->Blt(thumb_atlas, source, destination, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Build the common root fields before acquiring and registering the live application window manager.
UiBaseWindow::UiBaseWindow() : WnRootWidget(nullptr, nullptr) {
  owner = UiGetWindowManager();
  if (owner)
    keyboard = UiGetWindowManager()->GetKeyboard();
  if (owner)
    owner->AddWindowToValidList(this);
}

// Publish initialized state through the live flag override before inspecting private-buffer bits and selecting buffers.
bool UiBaseWindow::Init() {
  SetFlag(kWnFlagInitialized, true);
  if ((flags & 0x10000) && (flags & 1))
    PrivateBuffer(true);
  SetBufferToDrawToRecursive();
  return true;
}

// Hide and retire child/private buffers before dispatching the initialized-state clear through the live flag override.
bool UiBaseWindow::Shutdown() {
  HideWindow();
  ChildDeleteAll();
  PrivateBuffer(false);
  SetFlag(kWnFlagInitialized, false);
  return true;
}

// Publish the flag change before applying its buffer, capture and focus effects. Key changes use SIMUI's provider.
u32 UiBaseWindow::SetFlag(u32 mask, bool enabled) {
  flags = enabled ? flags | mask : flags & ~mask;
  if (mask == kWnFlagVisible) {
    if (GetFlag(kWnFlagPrivateBuffer))
      PrivateBuffer(enabled);
    if (enabled)
      return flags;
  } else if (mask == kWnFlagEnabled) {
    if (enabled)
      return flags;
    if (owner->GetCapture() == this)
      owner->ReleaseCapture(this);
  } else if (mask == kWnFlagPrivateBuffer) {
    if (!enabled)
      PrivateBuffer(false);
    else if (IsVisible())
      PrivateBuffer(true);
    return flags;
  } else if (mask == kWnFlagTransparentBuffer) {
    if (GetFlag(kWnFlagPrivateBuffer)) {
      SetBufferKey(enabled);
      InvalidateSelfAndParents();
    }
    return flags;
  } else
    return flags;
  if (owner->GetFocus() == this && owner->IsWindowValid(notification_target))
    owner->SetFocus(notification_target);
  return flags;
}

// Set the private buffer's key from SIMUI's color-zero provider, or clear its key when disabling transparency.
bool UiBaseWindow::SetBufferKey(bool enabled) {
  if (!private_buffer)
    return false;
  if (enabled) {
    WnResources *resources = UiGetWindowResources();
    if (!resources)
      return false;
    u32 key = resources->GetColor(kWnColorUiTransparency);
    private_buffer->SetTransparency(key);
  } else
    private_buffer->RemoveTransparency();
  return true;
}

// Allocate and resize through SIMUI's graphics cache, then refresh descendants and invalidation in source order.
// An initialized buffer of the current size returns before that refresh tail.
bool UiBaseWindow::PrivateBuffer(bool enabled) {
  bool result = false;
  if (enabled) {
    GxGraphics *graphics = UiGetGraphics();
    if (!graphics)
      return false;
    i32 width = area.right - area.left, height = area.bottom - area.top;
    if (width <= 0 || height <= 0) {
      if (private_buffer) {
        private_buffer->Shutdown();
        private_buffer->Release();
        private_buffer = nullptr;
      }
    } else {
      if (!private_buffer)
        graphics->CreateBuffer(&private_buffer);
      if (private_buffer) {
        if (private_buffer->IsReady()) {
          if ((u32)(area.right - area.left) == private_buffer->Width() &&
              (u32)(area.bottom - area.top) == private_buffer->Height())
            return true;
          private_buffer->Shutdown();
        }
        u32 mode = GetFlag(kWnFlagPrivateBufferVideoMemory) != 0;
        GxColorType format = graphics->GetScreenColorType();
        width = area.right - area.left;
        height = area.bottom - area.top;
        if (private_buffer->Init(width, height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
                static_cast<GxVramRequest>(mode), false)) {
          flags |= 0x10000;
          if (GetFlag(kWnFlagTransparentBuffer))
            SetBufferKey(true);
          SetArea(area.left, area.top, area.right, area.bottom);
          result = true;
        } else {
          private_buffer->Release();
          private_buffer = nullptr;
        }
      }
    }
  } else {
    if (private_buffer) {
      if (private_buffer->IsReady())
        private_buffer->Shutdown();
      private_buffer->Release();
      private_buffer = nullptr;
    }
    result = true;
  }
  SetBufferToDrawToRecursive();
  ChildPrivateBuffers(enabled);
  InvalidateSelfAndParents();
  return result;
}

// Paint dirty children back to front, then composite the private buffer onto the parent's target or SIMUI's canvas.
bool UiBaseWindow::Plot() {
  u32 current_flags = flags;
  if (!(current_flags & 1))
    return false;
  if (dirty) {
    if (private_buffer) {
      if (parent)
        dirty = false;
      if ((current_flags & 0x20000) && (current_flags & 0x40000))
        FillPrivateBufferWithTransparentColor();
    }
    GZPaint();
    u32 count = WnRootWidget::GetChildCount();
    WnChild *node = children.head;
    while (count--) {
      node = node->previous;
      node->window->Plot();
    }
  }
  if (private_buffer) {
    GxBuffer *target;
    if (parent && parent->GetBufferToDrawTo())
      target = parent->GetBufferToDrawTo();
    else
      target = UiGetWindowManager()->GetCanvas()->GetMainBufferSurface();
    if (target->Lock(GxLockFlags::Blt)) {
      target->Blt(private_buffer, draw_area, buffer_area, nullptr);
      target->Unlock(GxLockFlags::Blt);
    }
  }
  return true;
}

// Select the nearest private drawing buffer before falling back to this DLL's window-manager canvas.
// Preserve the final live area-recursion call after publishing the selected buffer.
void UiBaseWindow::SetBufferToDrawTo() {
  draw_buffer = GetPrivateBuffer();
  if (!draw_buffer) {
    WnWindow *ancestor = GetParentWin();
    while (ancestor) {
      GxBuffer *buffer = ancestor->GetPrivateBuffer();
      if (buffer) {
        draw_buffer = buffer;
        break;
      }
      ancestor = ancestor->GetParentWin();
    }
    if (!draw_buffer) {
      GxCanvas *canvas = UiGetWindowManager()->GetCanvas();
      if (canvas)
        draw_buffer = canvas->GetMainBufferSurface();
    }
  }
  SetAreaToDrawToRecursive();
}

// Decode the pixel through this DLL's graphics getter before forwarding its logical RGB bytes to the live setter.
void UiBaseWindow::SetFillColor(u32 pixel) {
  u8 red, green, blue;
  UiGetGraphics()->Convert16BitNativeValueToRGB((u16)pixel, &red, &green, &blue);
  SetFillColor(red, green, blue);
}

u32 UiBaseWindow::GetFillColor() const {
  GxGraphics *graphics = UiGetGraphics();
  return graphics->ConvertRGBValueToNative16Bit((u8)(fill_color >> 16), (u8)(fill_color >> 8), (u8)fill_color);
}

// Convert through the current drawing buffer, falling back to SIMUI's graphics cache and preserving the WORD result.
u32 UiBaseWindow::MakeFillColor(u8 red, u8 green, u8 blue) {
  if (draw_buffer)
    return draw_buffer->ConvertRGBValueToNative16Bit(red, green, blue);
  GxGraphics *graphics = UiGetGraphics();
  return graphics ? graphics->ConvertRGBValueToNative16Bit(red, green, blue) : 0;
}

UiMinMaxButton::UiMinMaxButton()
    : atlas(nullptr), alpha(nullptr), opaque(nullptr), frame_width(0), frame_height(0), pressed(false), hovered(false),
      mode(0), ordinary_hint(), alternate_hint(), hint_token(0) {
}

UiMinMaxButton::~UiMinMaxButton() {
}

// Publish the secondary control interface for its own IID; all root identities retain the actual base behavior.
bool UiMinMaxButton::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiMinMaxButtonInterface)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiMinMaxButtonInterface *>(this);
  AddRef();
  return true;
}

u32 UiMinMaxButton::AddRef() {
  return WnRootWidget::AddRef();
}

u32 UiMinMaxButton::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window view from the secondary control identity.
WnWindow *UiMinMaxButton::AsIGZWin() {
  return this;
}

// Accept only the two source display modes and leave the current mode unchanged on rejection.
bool UiMinMaxButton::SetMinMaxMode(UiMode value) {
  u32 word = static_cast<u32>(value);
  if (word > 1)
    return false;
  mode = word;
  return true;
}

u32 UiMinMaxButton::GetMinMaxMode() {
  return mode;
}

// Release the three live buffers before SIMUI's base shutdown dispatches the initialized-state clear.
bool UiMinMaxButton::Shutdown() {
  ScRelease(&opaque);
  ScRelease(&atlas);
  ScRelease(&alpha);
  return UiBaseWindow::Shutdown();
}

void UiMinMaxButton::SetArea(i32 left, i32 top, i32, i32) {
  WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)frame_width), (i32)((u32)top + (u32)frame_height));
}

// Enabled-flag changes update the slice BEFORE the base flag mutation, preserving the original ordering quirk.
u32 UiMinMaxButton::SetFlag(u32 flag, bool enabled) {
  if (flag == kWnFlagEnabled)
    RequeryImageEffects();
  return UiBaseWindow::SetFlag(flag, enabled);
}

void UiMinMaxButton::RequeryImageEffects() {
  if (!GetFlag(kWnFlagEnabled))
    SetImageIndexEffects(static_cast<UiImageIndex>(0));
  else if (pressed)
    SetImageIndexEffects(static_cast<UiImageIndex>(2));
  else if (hovered)
    SetImageIndexEffects(static_cast<UiImageIndex>(3));
  else
    SetImageIndexEffects(static_cast<UiImageIndex>(1));
}

// Store the source rectangle from its original image-index enum using DWORD wrap arithmetic.
void UiMinMaxButton::SetImageIndexEffects(UiImageIndex index) {
  u32 slice = static_cast<u32>(index);
  i32 width = frame_width, height = frame_height;
  source.top = 0;
  source.bottom = height;
  source.right = (i32)((u32)width * (slice + 1));
  source.left = (i32)(slice * (u32)width);
}

// The alpha mode performs the actual locked pixel hit test, including source early bounds returns without unlock.
bool UiMinMaxButton::IsPointInWindow(i32 x, i32 y) const {
  if (!mode)
    return WnRootWidget::IsPointInWindow(x, y);
  if (!WnRootWidget::IsPointInWindow(x, y))
    return false;
  GxBuffer *image = atlas;
  if (!image || !image->Lock(GxLockFlags::GetSetPixel))
    return true;
  if (x < 0 || y < 0 || x >= frame_width || y >= frame_height)
    return false;
  bool result = atlas->GetPixel(x, y) != 0;
  atlas->Unlock(GxLockFlags::GetSetPixel);
  return result;
}

bool UiMinMaxButton::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  ScreenToWindowCoordinates(&x, &y);
  return IsPointInWindow(x, y);
}

// Return the actual secondary interface of a newly constructed control; its source reference count remains zero.
UiMinMaxButtonInterface *UiMinMaxButton::CreateNewObject() {
  return new UiMinMaxButton;
}

// Acquire the three image resources in source order and derive the alpha mask from the third image's blue channel.
// Finish through SIMUI's base Init; failure invokes live Shutdown and mask-lock failure retains the source lock.
bool UiMinMaxButton::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef opaque_image(kRsToolbarToggleOpaqueImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  UiImageRef atlas_image(kRsToolbarToggleAtlasImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  UiImageRef alpha_image(kRsToolbarToggleAlphaImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  opaque = opaque_image.GetBuffer();
  if (opaque) {
    opaque->AddRef();
    opaque->SetTransparency(MakeFillColor(255, 0, 255));
    atlas = atlas_image.GetBuffer();
    if (atlas) {
      atlas->AddRef();
      i32 width = atlas->Width(), height = atlas->Height();
      frame_width = width / 4;
      frame_height = height;
      SetSize(frame_width, frame_height);
      SetImageIndexEffects(static_cast<UiImageIndex>(1));
      GxBuffer *image = alpha_image.GetBuffer();
      if (image) {
        GxGraphics *graphics = UiGetGraphics();
        if (graphics && graphics->CreateBuffer(&alpha)) {
          alpha->Init((u32)width, (u32)height, GxColorType(static_cast<GxColorType::Color>(3)), nullptr,
              GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
          if (image->Lock(GxLockFlags::GetSetPixel)) {
            if (alpha->Lock(GxLockFlags::GetSetPixel)) {
              f32 factor = (f32)((f64)(i32)UiGetBlendService()->GetAlphaLevelCount() / 256.0);
              u8 red, green, blue;
              for (i32 y = 0; y < height; ++y) {
                for (i32 x = 0; x < width; ++x) {
                  image->Convert16BitNativeValueToRGB((u16)image->GetPixel(x, y), &red, &green, &blue);
                  blue = (u8)(i64)((f64)blue * factor);
                  alpha->SetPixel(x, y, blue);
                }
              }
              alpha->Unlock(GxLockFlags::GetSetPixel);
              image->Unlock(GxLockFlags::GetSetPixel);
              UiTextRef ordinary(kRsGuiHideToolbarsText, kRsGuiTextGroup),
                  alternate(kRsGuiShowToolbarsText, kRsGuiTextGroup);
              if (ordinary.IsResolved())
                ordinary.CopyTo(&ordinary_hint);
              if (alternate.IsResolved())
                alternate.CopyTo(&alternate_hint);
              return UiBaseWindow::Init();
            }
          }
        }
      }
    }
  }
  Shutdown();
  return false;
}

// Compose the atlas through its alpha mask in mode one; ordinary mode locks the destination for the keyed blit.
// The ordinary path reports success even when the destination lock fails, matching the original window contract.
bool UiMinMaxButton::GZPaint() {
  if (mode == 1)
    return UiGetBlendService()->AlphaBlt(atlas, draw_buffer, alpha, source, draw_origin, nullptr) != 0;
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    draw_buffer->Blt(opaque, source, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

i32 UiPlayControlSound(i32 sound) {
  ApBoxX *audio = UiGetBoxX();
  return audio ? audio->Event(kAuEventPlayEffect, (u32)sound, 0, 0, 0) : 0;
}

void UiMinMaxButton::HideTipEffects() {
  ScMessage message = {kWnMsg_StopHint, hint_token, 0, 0};
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->MessageSend(message);
}

bool UiMinMaxButton::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiMinMaxButton::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Play the click sound before changing press state, then cancel the current hint even when already pressed.
bool UiMinMaxButton::OnMouseDownL(i32, i32, u32) {
  UiPlayControlSound(171);
  if (!pressed) {
    pressed = true;
    RequeryImageEffects();
  }
  HideTipEffects();
  return true;
}

// Release press state, retest through the live virtual hit method and route a successful click to the notification target.
bool UiMinMaxButton::OnMouseUpL(i32 x, i32 y, u32) {
  if (pressed) {
    pressed = false;
    RequeryImageEffects();
  }
  bool hit = IsPointInWindow(x, y);
  if (hovered != hit) {
    hovered = hit;
    RequeryImageEffects();
  }
  if (hovered) {
    u32 id = GetID();
    SendMsg(notification_target, kWnMsg_Command, kWnCommandButtonActivated, id, 0);
  } else {
    UiPlayControlSound(171);
    owner->ReleaseCapture(this);
  }
  return true;
}

bool UiMinMaxButton::OnMouseMove(i32 x, i32 y, u32) {
  if (!pressed) {
    bool hit = IsPointInWindow(x, y);
    if (!hit) {
      if (hovered) {
        hovered = false;
        RequeryImageEffects();
        HideTipEffects();
        owner->ReleaseCapture(this);
      }
    } else if (!hovered) {
      hovered = true;
      RequeryImageEffects();
      WnHint hint;
      hint.text = mode == 1 ? &alternate_hint : &ordinary_hint;
      hint.rectangle = absolute_area;
      hint.flags |= 0x40003u;
      ScMessage message = {kWnMsg_StartHint, (uptr)&hint, 0, 0};
      ScMessageServer *server = UiGetMessageServer();
      if (server)
        server->MessageSend(message);
      hint_token = hint.token;
      owner->SetCapture(this);
    }
  }
  return true;
}

// Reset state for another capture target, retaining the source's repeated slice updates and unconditional final cancel.
bool UiMinMaxButton::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    if (pressed) {
      pressed = false;
      RequeryImageEffects();
    }
    if (hovered) {
      hovered = false;
      RequeryImageEffects();
    }
    RequeryImageEffects();
    HideTipEffects();
  }
  return true;
}

UiCursorScope::UiCursorScope(u32 identifier) : pushed(0) {
  WnWindowManager *manager = UiGetWindowManager();
  if (manager) {
    WnCursorManager *cursor = manager->GetCursorManager();
    if (cursor)
      pushed = cursor->PushAutoCursor(identifier);
  }
}

UiCursorScope::~UiCursorScope() {
  if (!pushed)
    return;
  WnWindowManager *manager = UiGetWindowManager();
  if (manager) {
    WnCursorManager *cursor = manager->GetCursorManager();
    if (cursor)
      cursor->PopAutoCursor();
  }
}

UiFixedOptions::UiFixedOptions() : selected(kUiFixedOptionNoSelectionId) {
}

UiFixedOptions::~UiFixedOptions() {
}

// Publish the secondary option identity and retain through the primary window, delegating other identities to its base.
bool UiFixedOptions::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiFixedOptionsControl)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiFixedOptionsControl *>(this);
  AddRef();
  return true;
}

u32 UiFixedOptions::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiFixedOptions::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window view from the option interface without adding another reference.
WnWindow *UiFixedOptions::AsIGZWin() {
  return this;
}

// Reject existing/reserved IDs, then create a mode-five button and remember its ID only after successful attachment.
// Fit and release a created button even when attachment fails; creation failure still preserves the original one byte.
u8 UiFixedOptions::AddOption(u32 option_id, GxBuffer *image, const ScRect &rectangle) {
  if (ChildExists(option_id) || option_id == kUiFixedOptionNoSelectionId)
    return 0;
  WnButton *button = nullptr;
  if (UiGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button))) {
    button->GetWindow()->SetID(option_id);
    button->SetStyle(kWnButtonToggleFixed, image, &rectangle);
    if (ChildAdd(button->GetWindow())) {
      u32 position = 0;
      while (position < options.size && options.data[position] < option_id)
        ++position;
      if (position == options.size || options.data[position] != option_id) {
        options.Append();
        memmove(options.data + position + 1, options.data + position, (options.size - position - 1) * sizeof(u32));
        options.data[position] = option_id;
      }
    }
    button->Fit();
    button->Release();
  }
  return 1;
}

// Delete the matching child before erasing its live option ID; removing the selected option clears and notifies.
u8 UiFixedOptions::RemoveOption(u32 option_id) {
  WnWindow *child = GetChildWindowFromID(option_id);
  if (!child)
    return 0;
  ChildDelete(child);
  for (u32 i = 0; i < options.size; ++i)
    if (options.data[i] == option_id) {
      memmove(options.data + i, options.data + i + 1, (options.size - i - 1) * sizeof(u32));
      --options.size;
      break;
    }
  if (selected == option_id) {
    selected = kUiFixedOptionNoSelectionId;
    SendChangedEffects();
  }
  return 1;
}

// Stack extant options in ascending ID order, capturing each width before moving and reading its height afterward.
u8 UiFixedOptions::LayoutTopToBottom() {
  i32 width = 0, height = 0;
  for (u32 i = 0; i < options.size;) {
    u32 option_id = options.data[i];
    WnWindow *child = GetChildWindowFromID(option_id);
    if (child) {
      i32 value = child->GetW();
      if (value > width)
        width = value;
      child->GZWinMoveTo(0, height);
      height += child->GetH();
    }
    i = 0;
    while (i < options.size && options.data[i] <= option_id)
      ++i;
  }
  SetSize(width, height);
  return 1;
}

// Place extant options left to right in ascending ID order, reading width only after each movement callback.
u8 UiFixedOptions::LayoutLeftToRight() {
  i32 width = 0, height = 0;
  for (u32 i = 0; i < options.size;) {
    u32 option_id = options.data[i];
    WnWindow *child = GetChildWindowFromID(option_id);
    if (child) {
      i32 value = child->GetH();
      if (value > height)
        height = value;
      child->GZWinMoveTo(width, 0);
      width += child->GetW();
    }
    i = 0;
    while (i < options.size && options.data[i] <= option_id)
      ++i;
  }
  SetSize(width, height);
  return 1;
}

u8 UiFixedOptions::GetSelection(u32 *value) const {
  *value = selected;
  return 1;
}

// Retoggle an unchanged valid selection without notification; changed selection untoggles the old ID before storing.
u8 UiFixedOptions::SetSelection(u32 option_id) {
  u32 position = 0;
  while (position < options.size && options.data[position] < option_id)
    ++position;
  if (position == options.size || options.data[position] != option_id)
    return 0;
  if (selected == option_id)
    SetToggle(selected, true);
  else {
    SetToggle(selected, false);
    selected = option_id;
    SetToggle(option_id, true);
    SendChangedEffects();
  }
  return 1;
}

// Accept button commands only for registered IDs and dispatch selection through the live secondary virtual interface.
bool UiFixedOptions::OnCommand(u32 kind, u32 command) {
  if (kind != kWnCommandButtonActivated)
    return false;
  u32 position = 0;
  while (position < options.size && options.data[position] < command)
    ++position;
  if (position == options.size || options.data[position] != command)
    return false;
  static_cast<UiFixedOptionsControl *>(this)->SetSelection(command);
  return true;
}

// Query the actual child button and change its toggle state before retiring the successfully acquired interface.
u8 UiFixedOptions::SetToggle(u32 option_id, bool enabled) {
  WnButton *button = nullptr;
  u8 result = GetChildAs(option_id, IID_WnButton, reinterpret_cast<void **>(&button));
  if (result) {
    if (enabled)
      button->ToggleOn();
    else
      button->ToggleOff();
    button->Release();
  }
  return result;
}

// Read toggle state only after a successful button query, preserving the caller's output on lookup failure.
u8 UiFixedOptions::GetToggle(u32 option_id, bool *enabled) {
  WnButton *button = nullptr;
  u8 result = GetChildAs(option_id, IID_WnButton, reinterpret_cast<void **>(&button));
  if (result) {
    *enabled = button->IsOn();
    button->Release();
  }
  return result;
}

// Send the original command-three envelope to the freshly queried parent after capturing this control's current ID.
void UiFixedOptions::SendChangedEffects() {
  u32 option_id = GetID();
  WnWindow *target = GetParentWin();
  SendMsg(target, kWnMsg_Command, kWnCommandOptionSelectionChanged, option_id, 0);
}

// Acquire the image resource and borrow its rectangle for AddOption, retiring the image after the option call.
u8 UiFixedOptions::AddOptionResource(
    UiFixedOptionsControl *options, u32 option_id, RsManager *resources, const RsKey &key) {
  if (!resources || !options)
    return 0;
  GxBuffer *image = nullptr;
  u8 result = 0;
  if (resources->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&image))) {
    const ScRect *rectangle = image->GetBufferArea();
    result = options->AddOption(option_id, image, *rectangle);
    image->Release();
  }
  return result;
}

// Create the provider's image button directly into the original output field and attach its borrowed window.
// Attachment failure releases the published button without clearing that field, preserving the source's live output.
bool UiFrame::CreateButton(u32 button_id, GxBuffer *image, const ScRect *rectangle, WnButton **out) {
  ScString empty;
  WnResources *resources = UiGetWindowResources();
  if (!resources || !image)
    return false;
  *out = resources->CreateButton(button_id, empty, image, rectangle, 0);
  if (!*out)
    return false;
  WnWindow *window = (*out)->GetWindow();
  if (ChildAdd(window)) {
    window->SetNotificationTarget(parent);
    (*out)->SetButtonFlag(kWnButtonShowText, false);
    return true;
  }
  (*out)->Release();
  return false;
}

// Acquire the frame font and scoped three-row button atlas before replacing first, third and second controls.
// Apply each button's visibility and inverted enable style immediately after creation; base initialization runs last.
bool UiFrame::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return false;
  if (!font) {
    FtFont *value = resources->GetFont(kWnFontUiHeading);
    if (!value)
      value = resources->GetFont(kWnFontDefault);
    if (!value)
      return false;
    SetFont(value);
  }
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef asset(kRsDialogFrameAtlasImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *atlas = asset.GetBuffer();
  if (!atlas)
    return false;
  u32 height = atlas->Height() >> 2;
  i32 width = atlas->Width();
  ScRect rectangle = {0, 0, width, (i32)height};
  const u32 ids[3] = {kUiFrameCloseButtonId, kUiFrameBackButtonId, kUiFrameSecondButtonId}, styles[3] = {1, 4, 2};
  for (u32 index = 0; index < 3; ++index) {
    WnButton **slot = index == 0 ? &first_button : index == 1 ? &third_button : &second_button;
    if (*slot) {
      (*slot)->Release();
      *slot = nullptr;
    }
    if (!CreateButton(ids[index], atlas, &rectangle, slot))
      return false;
    (*slot)->GetWindow()->SetFlag(kWnFlagVisible, GetStyle(styles[index]));
    (*slot)->GetWindow()->SetFlag(kWnFlagEnabled, !GetStyle(styles[index] << 8));
    if (index == 2)
      return UiBaseWindow::Init();
    rectangle.top = (i32)((u32)rectangle.top + height);
    rectangle.bottom = (i32)((u32)rectangle.bottom + height);
  }
  return false;
}

// Fill the panel unless suppressed, then draw enabled border edges under a separate write lock.
// Background lock failure is tolerated; border lock failure returns false before any edge is drawn.
bool UiBorderPanel::GZPaint() {
  if (!(border_mode & 0x40) && draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(GetFillColor(), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (border_mode & 0xbf) {
    u16 colors[4];
    for (u32 index = 0; index < 4; ++index) {
      GxGraphics *graphics = UiGetGraphics();
      u32 color = border_colors[index];
      colors[index] = graphics->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
    }
    if (!draw_buffer->Lock(GxLockFlags::Line))
      return false;
    i32 left = draw_area.left, top = draw_area.top, right = (i32)((u32)draw_area.right - 1),
        bottom = (i32)((u32)draw_area.bottom - 1);
    for (u32 index = 0; index < 4; ++index) {
      if (!(border_mode & (1u << index)))
        continue;
      i32 x1 = index == 2 ? right : left, y1 = index == 3 ? bottom : top, x2 = index == 0 ? left : right,
          y2 = index == 1 ? top : bottom;
      draw_buffer->DrawLine(x1, y1, x2, y2, static_cast<i32>(colors[index]), 1);
    }
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

// Draw the frame's fitted title after successful panel painting, selecting the optional explicit text color by style.
// The frame's own return remains true even when the panel lock or the delegated glyph operation reports failure.
bool UiFrame::GZPaint() {
  if (UiBorderPanel::GZPaint() && font) {
    bool tinted = GetStyle(0x1000);
    i32 x = (i32)((u32)draw_area.left + text_x), y = (i32)((u32)draw_area.top + text_y);
    if (tinted)
      font->DrawTextA(draw_buffer, (u32)x, (u32)y, display_caption.Data(), display_caption.Strlen(), text_color);
    else
      font->DrawTextA(draw_buffer, (u32)x, (u32)y, display_caption.Data(), display_caption.Strlen());
  }
  return true;
}

UiFrame::UiFrame()
    : frame_style(0), font(nullptr), text_color(0), display_caption(), text_x(0), text_y(0), text_right(0),
      first_button(nullptr), second_button(nullptr), third_button(nullptr) {
}

UiFrame::~UiFrame() {
  UiFrame::Shutdown();
}

bool UiFrame::Shutdown() {
  ScRelease(&first_button);
  ScRelease(&second_button);
  ScRelease(&third_button);
  HideWindow();
  SetFont(nullptr);
  UiBaseWindow::Shutdown();
  return true;
}

// Retain the replacement font before releasing the previous one, publish the field and invalidate the live window.
bool UiFrame::SetFont(FtFont *value) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  InvalidateSelf();
  return true;
}

bool UiFrame::GetStyle(u32 flag) {
  return (frame_style & flag) != 0;
}

// Publish the fixed minimums before applying font and first-button height contributions in their original order.
bool UiFrame::GetMinimum(i32 *width, i32 *height) {
  *width = 128;
  *height = 8;
  if (font) {
    i32 value = (i32)(font->Height(0) + 8u);
    if (*height < value)
      *height = value;
  }
  if (first_button) {
    i32 value = (i32)((u32)first_button->GetWindow()->GetH() + 2);
    if (*height < value)
      *height = value;
  }
  return true;
}

// Fit the caption to the available title width, using the provider's ellipsis or the original three-dot fallback.
bool UiFrame::FormatTitle() {
  if (!font) {
    display_caption.FromChar(nullptr, 0);
    return false;
  }
  u32 length = caption.Strlen();
  if (!length) {
    display_caption.FromChar(nullptr, 0);
    return true;
  }
  if (length == 1) {
    display_caption.Copy(caption);
    return true;
  }
  i32 width = (i32)((u32)text_right - text_x);
  if (width <= 0) {
    display_caption.FromChar(nullptr, 0);
    return true;
  }
  u32 fit = font->Fit(caption.Data(), length, (u32)width);
  if (!fit) {
    display_caption.FromChar(nullptr, 0);
    return true;
  }
  if (fit == length) {
    display_caption.Copy(caption);
    return true;
  }
  WnResources *resources = UiGetWindowResources();
  ScString fallback("...", 3);
  const ScStringView *ellipsis = resources ? resources->GetText(kWnEllipsisTextId) : nullptr;
  if (!ellipsis)
    ellipsis = &fallback;
  u32 ellipsis_length = ellipsis->Strlen(), extent = font->Measure(ellipsis->Data(), ellipsis_length);
  if ((i32)extent < width) {
    fit = font->Fit(caption.Data(), length, (u32)width - extent);
    display_caption.FromChar(caption.Data(), fit);
    if (fit) {
      u32 trimmed = fit;
      const char *data = display_caption.Data();
      while (trimmed && data[trimmed - 1] == ' ')
        --trimmed;
      assert(trimmed);
      if (trimmed != fit)
        display_caption.Erase(trimmed, fit - trimmed);
    }
    ellipsis_length = ellipsis->Strlen();
    display_caption.Append(ellipsis->Data(), ellipsis_length);
  } else
    display_caption.FromChar(nullptr, 0);
  return true;
}

void UiFrame::SetCaption(const ScStringView &text) {
  UiBaseWindow::SetCaption(text);
  FormatTitle();
}

// Position visible buttons right-to-left in first/third/second order and keep the original reused-view behavior.
// Refit the title only when its newly stored positive right boundary differs from the previous boundary.
bool UiFrame::Relayout() {
  i32 right = (i32)((u32)GetW() - 4);
  WnWindow *view = nullptr;
  for (u32 index = 0; index < 3; ++index) {
    WnButton *button = index == 0 ? first_button : index == 1 ? third_button : second_button;
    if (button)
      view = button->GetWindow();
    if (view && view->GetFlag(kWnFlagVisible)) {
      i32 height = view->GetH(), y = (i32)((u32)GetH() - height) / 2;
      right = (i32)((u32)right - view->GetW());
      view->GZWinMoveTo(right, y);
      view->SetFlag(kWnFlagReorderable, true);
      ChildToBack(view);
      view->SetFlag(kWnFlagReorderable, false);
      right = view->GetL();
      if (index < 2)
        view = nullptr;
    }
  }
  i32 previous = text_right;
  text_right = right;
  if (right > 0 && right != previous)
    FormatTitle();
  return true;
}

// Apply the selected button's visibility/enabled effect before changing the frame's own style bits.
// Visibility changes run live layout first, preserving the source's callback and field ordering.
bool UiFrame::SetStyle(u32 flag, u32 enabled) {
  WnButton *button = nullptr;
  if (flag == kUiFrameCloseVisible || flag == kUiFrameCloseState)
    button = first_button;
  else if (flag == kUiFrameSecondVisible || flag == kUiFrameSecondState)
    button = second_button;
  else if (flag == kUiFrameBackVisible || flag == kUiFrameBackState)
    button = third_button;
  if (button) {
    button->GetWindow()->SetFlag(flag < 256 ? 1 : 2, (u8)enabled != 0);
    if (flag < 256)
      Relayout();
  }
  frame_style = (u8)enabled ? frame_style | flag : frame_style & ~flag;
  return true;
}

// Commit the scalar outer rectangle through the copied root and relayout only when width or height changed.
void UiFrame::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  bool changed = (i32)((u32)right - left) != GetW() || (i32)((u32)bottom - top) != GetH();
  UiBaseWindow::SetArea(left, top, right, bottom);
  if (changed)
    Relayout();
}

// Apply the base panel layout only when a font exists, then enforce minimum height and recenter the title baseline.
u8 UiFrame::AutoSize(const ScRect &rectangle) {
  if (!font)
    return 0;
  i32 width, height;
  GetMinimum(&width, &height);
  UiBorderPanel::AutoSize(rectangle);
  if (GetH() < height)
    SetH(height);
  text_x = 4;
  i32 font_height = font->Height(0);
  text_y = (i32)((u32)GetH() - font_height) / 2;
  return 1;
}
