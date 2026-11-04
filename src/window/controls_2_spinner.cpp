#include "window/controls.h"
#include <assert.h>
#include <stdio.h>
#include <utility>
#include "audio/api.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"
#include "window/wn_resource_provider.h"

bool WnOptionGroup::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnOptionGroup::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Select manual child bounds for any nonzero mode, otherwise measure reverse-ordered button columns and the caption.
bool WnOptionGroup::GetMinMaxInfo(ScRect *limits) {
  if (autofill_mode) {
    *limits = {0, 0, 0xFFFF, 0xFFFF};
    for (WnChild *node = children.head->next; node != children.head; node = node->next) {
      i32 right = node->window->GetR();
      if (right > limits->left)
        limits->left = node->window->GetR();
      i32 bottom = node->window->GetB();
      if (bottom > limits->top)
        limits->top = node->window->GetB();
    }
    limits->left = (i32)((u32)limits->left + horizontal_gutter);
    limits->top = (i32)((u32)limits->top + vertical_gutter);
    return true;
  }
  i32 widest = 0, start_y = vertical_gutter;
  i32 bottom = (i32)((u32)autofill_limit - (u32)start_y);
  *limits = {horizontal_gutter, vertical_gutter, 0xFFFF, 0xFFFF};
  if (option_flags & 1) {
    void *slot = nullptr;
    if (GetChildAs(kWnOptionCaptionId, IID_WnLabelControl, &slot)) {
      WnLabelControl *label = static_cast<WnLabelControl *>(slot);
      label->Fit();
      WnWindow *window = label->GetWindow();
      i32 width = window->GetW();
      i32 edge = (i32)((u32)width + horizontal_gutter);
      if (edge > limits->left)
        limits->left = edge;
      window = label->GetWindow();
      i32 height = window->GetH();
      limits->top = (i32)((u32)limits->top + (u32)height);
      window = label->GetWindow();
      height = window->GetH();
      start_y = (i32)((u32)start_y + (u32)height + vertical_gutter);
      label->Release();
    }
  }
  i32 y = start_y, x = horizontal_gutter;
  WnChild *iterator = children.head;
  while (iterator != children.head->next) {
    void *slot;
    if (iterator->previous->window->QueryInterface(IID_WnButton, &slot)) {
      WnButton *button = static_cast<WnButton *>(slot);
      button->Fit();
      WnWindow *window = button->GetWindow();
      i32 width = window->GetW();
      if (width > widest)
        widest = width;
      window = button->GetWindow();
      i32 height = window->GetH();
      if ((i32)((u32)y + (u32)height) > bottom) {
        x = (i32)((u32)x + (u32)widest + horizontal_gutter);
        widest = width;
        y = start_y;
      }
      window = button->GetWindow();
      height = window->GetH();
      y = (i32)((u32)y + (u32)height);
      i32 right = (i32)((u32)widest + (u32)x);
      if (right > limits->left)
        limits->left = right;
      if (y > limits->top)
        limits->top = y;
      iterator->previous->window->Release();
    }
    iterator = iterator->previous;
  }
  limits->left = (i32)((u32)limits->left + horizontal_gutter);
  limits->top = (i32)((u32)limits->top + vertical_gutter);
  return true;
}

// Place the caption first, then flow reverse-ordered buttons by height unless the exact manual mode is one.
void WnOptionGroup::LayoutEffects(i32, i32 height) {
  i32 start_y = vertical_gutter;
  if (option_flags & 1) {
    void *slot = nullptr;
    GetChildAs(kWnOptionCaptionId, IID_WnLabelControl, &slot);
    if (slot) {
      WnLabelControl *label = static_cast<WnLabelControl *>(slot);
      label->Fit();
      WnWindow *window = label->GetWindow();
      window->GZWinMoveTo(horizontal_gutter, vertical_gutter);
      window = label->GetWindow();
      i32 label_height = window->GetH();
      start_y = (i32)((u32)label_height + (u32)start_y + vertical_gutter);
      label->Release();
    }
  }
  if (autofill_mode == 1)
    return;
  i32 x = horizontal_gutter, widest = 0, y = start_y;
  i32 bottom = (i32)((u32)height - vertical_gutter);
  WnChild *iterator = children.head;
  while (iterator != children.head->next) {
    void *slot;
    if (iterator->previous->window->QueryInterface(IID_WnButton, &slot)) {
      WnButton *button = static_cast<WnButton *>(slot);
      WnWindow *window = button->GetWindow();
      i32 item_height = window->GetH();
      if ((i32)((u32)y + (u32)item_height) > bottom) {
        x = (i32)((u32)x + (u32)widest + horizontal_gutter);
        y = start_y;
      }
      i32 width = window->GetW();
      if (width > widest)
        widest = width;
      window->GZWinMoveTo(x, y);
      item_height = window->GetH();
      y = (i32)((u32)y + (u32)item_height);
      button->Release();
    }
    iterator = iterator->previous;
  }
}

// Tile a retained backdrop or fill when requested, then draw the four stored native-pixel border colors.
bool WnOptionGroup::GZPaint() {
  if (background) {
    if (!draw_buffer->Lock(GxLockFlags::Blt))
      return false;
    const ScRect *source = background->GetBufferArea();
    draw_buffer->BltTiled(background, *(source), draw_area, 0, 0, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  } else if (option_flags & 4) {
    if (!draw_buffer->Lock(GxLockFlags::Fill))
      return false;
    u32 pixel = GetFillColor();
    draw_buffer->Fill(pixel, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (option_flags & 8) {
    if (!draw_buffer->Lock(GxLockFlags::Line))
      return false;
    i32 right = (i32)((u32)draw_area.right - 1), bottom = (i32)((u32)draw_area.bottom - 1);
    i32 left = draw_area.left, top = draw_area.top;
    draw_buffer->DrawLine(left, top, left, bottom, static_cast<i32>(outline_colors[0]), 1);
    draw_buffer->DrawLine(left, top, right, top, static_cast<i32>(outline_colors[1]), 1);
    draw_buffer->DrawLine(right, top, right, bottom, static_cast<i32>(outline_colors[2]), 1);
    draw_buffer->DrawLine(left, bottom, right, bottom, static_cast<i32>(outline_colors[3]), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

// Construct the displaced root and empty point array before fetching the two edge colors and the background.
// Each provider query precedes its actual color setter; no uninitialized color field is read between those writes.
WnOutline::WnOutline() : mode(kWnOutlinePlain), point_count(0), points(nullptr) {
  u8 red, green, blue;
  WnGetUiResources()->GetColor(19, &red, &green, &blue);
  SetColor(1, red, green, blue);
  WnGetUiResources()->GetColor(20, &red, &green, &blue);
  SetColor(2, red, green, blue);
  WnGetUiResources()->GetColor(kWnColorUiBackground, &red, &green, &blue);
  SetColor(0, red, green, blue);
}

WnOutline::~WnOutline() {
  delete[] reinterpret_cast<u8 *>(points);
  points = nullptr;
}

bool WnOutline::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnOutlineControl, WnOutlineControl);
  return WnRootWidget::QueryInterface(iid, out);
}

// Forward to the live parent, translating mouse messages from local coordinates before dispatch.
// Publish the x adjustment before querying y, preserve DWORD wrapping and leave other payload words unchanged.
bool WnOutline::DoMessage(ScMessage *message) {
  if (!parent)
    return false;
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel) {
    i32 left = GetL();
    message->data1 = (u32)message->data1 + (u32)left;
    i32 top = GetT();
    message->data2 = (u32)message->data2 + (u32)top;
  }
  return parent->DoMessage(message);
}

u32 WnOutline::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnOutline::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed root view from the leading control without acquiring another reference.
WnWindow *WnOutline::GetWindow() {
  return static_cast<WnWindow *>(this);
}

u32 WnOutline::GetFlags() {
  return mode;
}

void WnOutline::SetFillColor(u32 pixel) {
  WnRootWidget::SetFillColor(pixel);
}

u32 WnOutline::GetFillColor() {
  return WnRootWidget::GetFillColor();
}

// Store a selected logical RGB word; index zero uses the root's real background setter and invalid indices do nothing.
void WnOutline::SetColor(u8 index, u8 red, u8 green, u8 blue) {
  if (!index)
    WnRootWidget::SetFillColor(red, green, blue);
  else if (index == 1)
    first_color = ((u32)red << 16) | ((u32)green << 8) | blue;
  else if (index == 2)
    second_color = ((u32)red << 16) | ((u32)green << 8) | blue;
}

// Publish individual logical color bytes in order, preserving untouched outputs for an invalid index.
void WnOutline::GetColor(u8 index, u8 *red, u8 *green, u8 *blue) {
  if (!index) {
    WnRootWidget::GetFillColor(red, green, blue);
    return;
  }
  const u32 *color;
  if (index == 1)
    color = &first_color;
  else if (index == 2)
    color = &second_color;
  else
    return;
  *red = (u8)(*color >> 16);
  *green = (u8)(*color >> 8);
  *blue = (u8)*color;
}

// Unpack the incoming native pixel before dispatching the live first-edge RGB setter; the unpack result is ignored.
void WnOutline::SetDarkOutlineColor(u32 pixel) {
  u8 red, green, blue;
  WnGetGraphics()->Convert16BitNativeValueToRGB((u16)pixel, &red, &green, &blue);
  SetColor(1, red, green, blue);
}

// Convert the native pixel through graphics before dispatching the live second-edge setter.
void WnOutline::SetLightOutlineColor(u32 pixel) {
  u8 red, green, blue;
  WnGetGraphics()->Convert16BitNativeValueToRGB((u16)pixel, &red, &green, &blue);
  SetColor(2, red, green, blue);
}

// Acquire graphics before reading the live first logical color and return only the original low-word pixel.
u32 WnOutline::GetDarkOutlineColor() {
  GxGraphics *graphics = WnGetGraphics();
  u32 color = first_color;
  return graphics->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
}

// Convert the independently stored second logical RGB word through the current graphics service.
u32 WnOutline::GetLightOutlineColor() {
  GxGraphics *graphics = WnGetGraphics();
  u32 color = second_color;
  return graphics->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
}

// Replace point storage and rotate the input around the source's zero-origin lexicographic pivot, then mark it ready.
// Preserve negative-count byte arithmetic; assert before a wrapped positive allocation would be overrun by point copies.
bool WnOutline::SetShapePoly(i32 count, ScPoint *incoming) {
  assert(count <= 0 || u32(count) <= UINT32_MAX / sizeof(ScPoint));
  delete[] reinterpret_cast<u8 *>(points);
  points = reinterpret_cast<ScPoint *>(new u8[(u32)count * (u32)sizeof(ScPoint)]);
  point_count = count;
  i32 first = 0, best_x = 0, best_y = 0;
  for (i32 i = 0; i < count; ++i)
    if (incoming[i].y < best_y || (incoming[i].y == best_y && incoming[i].x < best_x)) {
      first = i;
      best_x = incoming[i].x;
      best_y = incoming[i].y;
    }
  for (i32 i = first; i < count; ++i)
    points[i - first] = incoming[i];
  for (i32 i = 0; i < first; ++i)
    points[count - first + i] = incoming[i];
  mode |= kWnOutlinePolygon;
  return true;
}

// Return the new outline's displaced root identity at zero references for the class registry's retained query.
ScUnknown *WnConstructOutline() {
  return static_cast<WnWindow *>(new WnOutline);
}

void WnOutline::SetFlags(u32 requested) {
  mode = requested;
}

static i32 WnOutlineOffset(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

// Keep the scanline's integer intersections in the same ascending order as the original temporary list sort.
static void WnOutlineInsert(ScArray<i32> *values, i32 value) {
  values->Append();
  u32 position = values->size - 1;
  while (position && values->data[position - 1] > value) {
    values->data[position] = values->data[position - 1];
    --position;
  }
  values->data[position] = value;
}

// Fill and stroke either the current polygon or the original rectangular outline using independent lock phases.
// Polygon scanlines retain horizontal-vertex rules, paired intersections and live point/color reads between drawing calls.
bool WnOutline::GZPaint() {
  ScRect rectangle = draw_area;
  if (mode & kWnOutlinePolygon) {
    if ((mode & kWnOutlineFill) && point_count >= 3) {
      if (!draw_buffer->Lock(GxLockFlags::Line))
        return false;
      i32 height = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
      for (i32 row = 0; row < height; ++row) {
        ScArray<i32> intersections;
        for (i32 i = 0; i < point_count; ++i) {
          i32 count = point_count;
          ScPoint first = points[i], second = points[i == count - 1 ? 0 : i + 1];
          if (first.y == second.y) {
            if (first.y == row) {
              i32 previous_y = points[i ? i - 1 : count - 1].y, following_y = points[(i + 2) % count].y;
              if ((previous_y < row && following_y < row) || (previous_y > row && following_y > row))
                WnOutlineInsert(&intersections, first.x < second.x ? first.x : second.x);
            }
            continue;
          }
          if (second.y == row)
            continue;
          if (first.y == row)
            WnOutlineInsert(&intersections, first.x);
          else if ((first.y < row && second.y > row) || (first.y > row && second.y < row)) {
            i32 delta_y = (i32)((u32)row - (u32)first.y), delta_x = (i32)((u32)second.x - (u32)first.x);
            i32 denominator = (i32)((u32)second.y - (u32)first.y);
            i32 product = (i32)((u32)delta_y * (u32)delta_x);
            WnOutlineInsert(&intersections, WnOutlineOffset(first.x, product / denominator));
          }
        }
        u32 pixel = GetFillColor();
        for (u32 i = 0; i + 1 < intersections.size; i += 2) {
          i32 first = intersections.data[i], second = intersections.data[i + 1];
          if (first != second)
            draw_buffer->DrawLine(WnOutlineOffset(rectangle.left, first), WnOutlineOffset(rectangle.top, row),
                WnOutlineOffset(rectangle.left, second), WnOutlineOffset(rectangle.top, row),
                static_cast<i32>(pixel), 1);
        }
      }
      draw_buffer->Unlock(GxLockFlags::Line);
    }
    if (!(mode & kWnOutlineStrokeMask))
      return true;
    if (!draw_buffer->Lock(GxLockFlags::Line))
      return false;
    for (i32 i = 0; i < point_count; ++i) {
      i32 count = point_count;
      ScPoint first = points[i], second = points[i == count - 1 ? 0 : i + 1];
      bool descending = second.y > first.y || (second.y == first.y && second.x < first.x);
      u32 forward, backward;
      if (mode & kWnOutlineInset) {
        forward = WnOutline::GetDarkOutlineColor();
        backward = WnOutline::GetLightOutlineColor();
      } else if (mode & kWnOutlineOutset) {
        forward = WnOutline::GetLightOutlineColor();
        backward = WnOutline::GetDarkOutlineColor();
      } else
        forward = backward = WnOutline::GetDarkOutlineColor();
      draw_buffer->DrawLine(WnOutlineOffset(rectangle.left, first.x), WnOutlineOffset(rectangle.top, first.y),
          WnOutlineOffset(rectangle.left, second.x), WnOutlineOffset(rectangle.top, second.y),
          static_cast<i32>((u16)(descending ? forward : backward)), 1);
    }
    draw_buffer->Unlock(GxLockFlags::Line);
    return true;
  }
  if (mode & kWnOutlineFill) {
    if (!draw_buffer->Lock(GxLockFlags::Fill))
      return false;
    u32 pixel = GetFillColor();
    draw_buffer->Fill(pixel, &rectangle);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  u32 first, second;
  if (mode & kWnOutlineInset) {
    first = WnOutline::GetDarkOutlineColor();
    second = WnOutline::GetLightOutlineColor();
  } else if (mode & kWnOutlineOutset) {
    first = WnOutline::GetLightOutlineColor();
    second = WnOutline::GetDarkOutlineColor();
  } else
    first = second = WnOutline::GetDarkOutlineColor();
  if (!draw_buffer->Lock(GxLockFlags::Line))
    return false;
  rectangle.right = WnOutlineOffset(rectangle.right, -1);
  rectangle.bottom = WnOutlineOffset(rectangle.bottom, -1);
  draw_buffer->DrawLine(rectangle.left, rectangle.top, rectangle.right, rectangle.top, static_cast<i32>((u16)first), 1);
  draw_buffer->DrawLine(
      rectangle.left, rectangle.top, rectangle.left, rectangle.bottom, static_cast<i32>((u16)first), 1);
  draw_buffer->DrawLine(
      rectangle.right, rectangle.top, rectangle.right, rectangle.bottom, static_cast<i32>((u16)second), 1);
  draw_buffer->DrawLine(
      rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom, static_cast<i32>((u16)second), 1);
  draw_buffer->Unlock(GxLockFlags::Line);
  return true;
}

// Construct the base slider and real millisecond repeat counter, then initialize scrollbar-only state in source order.
WnScrollbar::WnScrollbar() : repeat_timer(ScStopwatch::Milliseconds, 0) {
  i32 upper = maximum;
  repeat_part = 0;
  inset = 0;
  repeat_enabled = true;
  page_divisor = (u32)upper - (u32)minimum;
  line_divisor = page_divisor;
}

WnScrollbar::~WnScrollbar() {
}

bool WnScrollbar::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnScrollbarControl)
    return WnSlider::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnScrollbarControl *>(this);
  AddRef();
  return true;
}

u32 WnScrollbar::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnScrollbar::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window from the scrollbar interface.
WnWindow *WnScrollbar::GetWindow() {
  return this;
}

// Return the actual inherited slider secondary without querying or retaining it.
WnSliderControl *WnScrollbar::GetSlider() {
  return this;
}

bool WnScrollbar::SetValue(i32 requested) {
  return WnSlider::SetValue(requested);
}

i32 WnScrollbar::GetValue() const {
  return WnSlider::GetValue();
}

// Capture the slider receiver once, set each bound through its live virtual methods and report success.
bool WnScrollbar::SetRange(i32 lower, i32 upper) {
  WnSliderControl *slider = this;
  slider->SetMinimum(lower);
  slider->SetMaximum(upper);
  return true;
}

// Publish the current range in order through the original two output pointers.
bool WnScrollbar::GetRange(i32 *lower, i32 *upper) const {
  *lower = minimum;
  *upper = maximum;
  return true;
}

bool WnScrollbar::SetOrientation(i32 requested) {
  orientation = requested == 1 || requested == 2 ? requested : 0;
  return true;
}

i32 WnScrollbar::GetOrientation() const {
  return orientation == 1 || orientation == 2 ? orientation : 0;
}

// Convert a nonzero signed requested page count to the source divisor, then run the live layout method.
bool WnScrollbar::SetPageCount(i32 count) {
  if (!count)
    return false;
  i32 range = (i32)((u32)maximum - (u32)minimum);
  page_divisor = (u32)(range / count);
  Layout();
  return true;
}

i32 WnScrollbar::GetPageCount() const {
  u32 divisor = page_divisor;
  return divisor ? (i32)(((u32)maximum - (u32)minimum + 1) / divisor) : 0;
}

// Convert a nonzero signed requested line count to its source divisor, then run the live layout method.
bool WnScrollbar::SetLineCount(i32 count) {
  if (!count)
    return false;
  i32 range = (i32)((u32)maximum - (u32)minimum);
  line_divisor = (u32)(range / count);
  Layout();
  return true;
}

i32 WnScrollbar::GetLineCount() const {
  u32 divisor = line_divisor;
  return divisor ? (i32)(((u32)maximum - (u32)minimum) / divisor) : 0;
}

// A zero line divisor selects the full range; a zero page divisor then selects the newly stored line divisor.
void WnScrollbar::SetDivisors(u32 line, u32 page) {
  line_divisor = line ? line : (u32)maximum - (u32)minimum;
  page_divisor = page ? page : line_divisor;
  Layout();
}

bool WnScrollbar::ImageSetup(const ScRect &, GxBuffer *incoming, i32) {
  return static_cast<WnSliderControl *>(this)->SetImage(incoming, nullptr);
}

// Return the orientation-specific minimum and maximum extents, leaving the output untouched for unknown orientation.
bool WnScrollbar::GetExtent(ScRect *out) const {
  WnScrollbar *window = const_cast<WnScrollbar *>(this);
  if (orientation == 1) {
    out->left = (i32)((u32)window->GetThumbWidth() * 3);
    out->top = window->GetThumbHeight();
    out->right = 0xFFFF;
    out->bottom = window->GetThumbHeight();
    return true;
  }
  if (orientation == 2) {
    out->left = window->GetThumbWidth();
    out->top = (i32)((u32)window->GetThumbHeight() * 3);
    out->right = window->GetThumbWidth();
    out->bottom = 0xFFFF;
    return true;
  }
  return false;
}

// Retain the new twelve-cell atlas before releasing the old one, then size the cross axis before vertical-bank adjustment.
bool WnScrollbar::SetImage(GxBuffer *incoming, const ScRect *rectangle) {
  if (incoming)
    incoming->AddRef();
  if (image)
    image->Release();
  image = incoming;
  if (!incoming)
    return true;
  ScRect source = *incoming->GetBufferArea();
  if (rectangle)
    source = *rectangle;
  i32 width = (i32)((u32)source.right - (u32)source.left), cell = width / 12;
  image_area = {source.left, source.top, (i32)((u32)source.left + (u32)cell), source.bottom};
  if (orientation == 1) {
    SetH((i32)((u32)source.bottom - (u32)source.top));
    return true;
  }
  if (orientation == 2) {
    SetW(cell);
    u32 offset = (u32)cell * 6;
    image_area.left = (i32)((u32)image_area.left + offset);
    image_area.right = (i32)((u32)image_area.right + offset);
    return true;
  }
  return false;
}

// Reserve live end-cap extents on the active axis and update the thumb. This override does not add base invalidation.
void WnScrollbar::Layout() {
  if (orientation == 1) {
    track_start = GetPrevNextButtonWidth();
    i32 start = GetPrevNextButtonWidth();
    i32 width = GetW(), finish = (i32)((u32)width - (u32)start);
    i32 thumb = GetThumbWidth();
    track_finish = (i32)((u32)finish - (u32)thumb);
  } else {
    track_start = GetPrevNextButtonHeight();
    i32 start = GetPrevNextButtonHeight();
    i32 height = GetH(), finish = (i32)((u32)height - (u32)start);
    i32 thumb = GetThumbHeight();
    track_finish = (i32)((u32)finish - (u32)thumb);
  }
  UpdateThumb();
}

i32 WnScrollbar::GetPrevNextButtonWidth() {
  return GetThumbWidth();
}

i32 WnScrollbar::GetPrevNextButtonHeight() {
  return GetThumbHeight();
}

// Subtract the unsigned line quotient, clamp against the live signed minimum and dispatch through the slider setter.
void WnScrollbar::LineDown() {
  u32 divisor = line_divisor;
  if (divisor) {
    u32 step = ((u32)maximum - (u32)minimum) / divisor;
    if (value > minimum) {
      i32 current = GetValue(), lower = minimum;
      i32 requested = (i32)((u32)current - step);
      if (requested < lower)
        requested = lower;
      SetValue(requested);
    }
  }
}

// Add the unsigned line quotient, clamp against the live signed maximum and dispatch through the slider setter.
void WnScrollbar::LineUp() {
  u32 divisor = line_divisor;
  if (divisor) {
    u32 step = ((u32)maximum - (u32)minimum) / divisor;
    if (value < maximum) {
      i32 current = GetValue(), upper = maximum;
      i32 requested = (i32)((u32)current + step);
      if (requested > upper)
        requested = upper;
      SetValue(requested);
    }
  }
}

// Page subtraction uses the source inclusive unsigned range before signed clamping and value dispatch.
void WnScrollbar::PageDown() {
  u32 divisor = page_divisor;
  if (divisor) {
    u32 step = ((u32)maximum - (u32)minimum + 1) / divisor;
    if (value > minimum) {
      i32 current = GetValue(), lower = minimum;
      i32 requested = (i32)((u32)current - step);
      if (requested < lower)
        requested = lower;
      SetValue(requested);
    }
  }
}

// Page addition uses the source inclusive unsigned range before signed clamping and value dispatch.
void WnScrollbar::PageUp() {
  u32 divisor = page_divisor;
  if (divisor) {
    u32 step = ((u32)maximum - (u32)minimum + 1) / divisor;
    if (value < maximum) {
      i32 current = GetValue(), upper = maximum;
      i32 requested = (i32)((u32)current + step);
      if (requested > upper)
        requested = upper;
      SetValue(requested);
    }
  }
}

// Classify a local point as the leading cap, trailing cap, either track region or thumb after the inherited hit test.
bool WnScrollbar::HitPart(i32 *out, i32 x, i32 y) {
  if (!IsPointInWindow(x, y)) {
    *out = 0;
    return false;
  }
  if (orientation == 1) {
    i32 leading = GetPrevNextButtonWidth();
    if (x < leading)
      *out = 1;
    else if (x < thumb_position)
      *out = 3;
    else {
      i32 thumb = GetThumbWidth(), end = (i32)((u32)thumb_position + (u32)thumb);
      if (x < end)
        *out = 5;
      else {
        i32 width = GetW(), limit = (i32)((u32)width - (u32)leading);
        *out = x >= limit ? 2 : 4;
      }
    }
  } else {
    i32 leading = GetPrevNextButtonHeight();
    if (y < leading)
      *out = 1;
    else if (y < thumb_position)
      *out = 3;
    else {
      i32 thumb = GetThumbHeight(), end = (i32)((u32)thumb_position + (u32)thumb);
      if (y < end)
        *out = 5;
      else {
        i32 height = GetH(), limit = (i32)((u32)height - (u32)leading);
        *out = y >= limit ? 2 : 4;
      }
    }
  }
  return true;
}

// Determine the pressed region before capture, restart repetition after capture and apply the initial arrow/page action.
bool WnScrollbar::OnMouseDownL(i32 x, i32 y, u32) {
  if (HitPart(&repeat_part, x, y)) {
    repeat_enabled = true;
    if (owner->SetCapture(this)) {
      repeat_timer.Restart();
      InvalidateSelfAndParents();
      switch (repeat_part) {
      case 1:
        LineDown();
        break;
      case 2:
        LineUp();
        break;
      case 3:
        PageDown();
        break;
      case 4:
        PageUp();
        break;
      }
    }
  }
  return true;
}

// Release capture first, apply a final thumb value when appropriate, then reset the pressed region and invalidate.
bool WnScrollbar::OnMouseUpL(i32 x, i32 y, u32) {
  if (owner->GetCapture() == this) {
    owner->ReleaseCapture(this);
    i32 requested;
    if (repeat_part == 5 && ValueFromPoint(&requested, x, y))
      SetValue(requested);
    repeat_part = 0;
    InvalidateSelfAndParents();
  }
  return true;
}

// Drag the thumb directly, or enable held repetition only while the pointer remains over its originally pressed region.
bool WnScrollbar::OnMouseMove(i32 x, i32 y, u32) {
  if (owner->GetCapture() == this) {
    i32 requested;
    if (repeat_part == 5) {
      if (ValueFromPoint(&requested, x, y))
        SetValue(requested);
    } else
      repeat_enabled = HitPart(&requested, x, y) && requested == repeat_part;
  }
  return true;
}

// Repeat a held cap/track action after 200 milliseconds, invalidate active repetition, then draw the real atlas or plain body.
bool WnScrollbar::GZPaint() {
  if (owner->GetCapture() == this && repeat_enabled && repeat_part >= 1 && repeat_part <= 4) {
    if (repeat_timer.GetElapsedTime() > 200) {
      switch (repeat_part) {
      case 1:
        LineDown();
        break;
      case 2:
        LineUp();
        break;
      case 3:
        PageDown();
        break;
      case 4:
        PageUp();
        break;
      }
      repeat_timer.Restart();
    }
    InvalidateSelfAndParents();
  }
  if (image)
    PaintImage();
  else
    PaintPlain();
  return true;
}

// Return the new scrollbar's secondary unknown identity at zero references through the registry's uniform constructor ABI.
ScUnknown *WnConstructScrollbar() {
  return static_cast<WnScrollbarControl *>(new WnScrollbar);
}

bool WnScrollbar::Init() {
  return WnSlider::Init();
}

bool WnScrollbar::Shutdown() {
  return WnSlider::Shutdown();
}

// Initialize the inherited slider state before applying its original 32-by-96 area through the base sizing body.
WnSlider::WnSlider()
    : orientation(2), minimum(0), maximum(32), value(0), track_start(0), thumb_position(0), track_finish(0), inset(0),
      setting_value(false), image(nullptr) {
  WnSlider::SetArea(0, 0, 32, 96);
}

WnSlider::~WnSlider() {
}

bool WnSlider::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnSliderControl, WnSliderControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnSlider::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnSlider::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window from the real slider secondary interface.
WnWindow *WnSlider::GetWindow() {
  return this;
}

// Lay out once before the inherited initialization unless the source initialized flag is already set.
bool WnSlider::Init() {
  if (!GetFlag(kWnFlagInitialized))
    Layout();
  return WnRootWidget::Init();
}

// Release the current atlas before clearing the field and running the inherited close operation.
bool WnSlider::Shutdown() {
  if (image)
    image->Release();
  image = nullptr;
  return WnRootWidget::Shutdown();
}

// An atlas fixes the cross-axis size. Invalid atlas orientation preserves the area but still calls live layout.
void WnSlider::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  if (!image)
    WnRootWidget::SetArea(left, top, right, bottom);
  else if (orientation == 1)
    WnRootWidget::SetArea(left, top, right, (i32)((u32)top + (u32)image_area.bottom - (u32)image_area.top));
  else if (orientation == 2)
    WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)image_area.right - (u32)image_area.left), bottom);
  Layout();
}

i32 WnSlider::GetValue() const {
  return value;
}

// Guard source reentry, clamp against live limits, then notify the target twice before clearing the lock and invalidating.
bool WnSlider::SetValue(i32 requested) {
  if (setting_value)
    return false;
  setting_value = true;
  if (requested == value) {
    setting_value = false;
    return false;
  }
  if (requested > maximum)
    requested = maximum;
  if (requested < minimum)
    requested = minimum;
  value = requested;
  UpdateThumb();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandSliderValueChangedByWindow,
      reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), 0);
  u32 id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandSliderValueChanged, id, 0);
  setting_value = false;
  InvalidateSelfAndParents();
  return true;
}

// Store the minimum before live layout, then dirty the same parent chain as the source.
void WnSlider::SetMinimum(i32 requested) {
  minimum = requested;
  Layout();
  InvalidateSelfAndParents();
}

// Store the maximum before live layout, then dirty the same parent chain as the source.
void WnSlider::SetMaximum(i32 requested) {
  maximum = requested;
  Layout();
  InvalidateSelfAndParents();
}

void WnSlider::SetDirection(WnSliderDirection requested) {
  orientation = static_cast<i32>(requested);
}

// Retain the incoming atlas before releasing the old one. Preserve the source's four-cell rectangle arithmetic.
bool WnSlider::SetImage(GxBuffer *incoming, const ScRect *rectangle) {
  if (incoming)
    incoming->AddRef();
  if (image)
    image->Release();
  image = incoming;
  ScRect source{};
  if (incoming)
    incoming->GetBufferArea(&source);
  else
    image_area = {0, 0, 16, 16};
  if (rectangle)
    source = *rectangle;
  if (orientation == 1) {
    if (image) {
      i32 width = (i32)((u32)source.right - (u32)source.left);
      i32 height = (i32)((u32)source.bottom - (u32)source.top);
      image_area = {source.left, source.top, width / 4, height};
    }
    SetH((i32)((u32)image_area.bottom - (u32)image_area.top));
    return true;
  }
  if (orientation == 2) {
    if (image) {
      i32 width = (i32)((u32)source.right - (u32)source.left);
      i32 height = (i32)((u32)source.bottom - (u32)source.top);
      image_area = {source.left, source.top, width, height / 4};
    }
    SetW((i32)((u32)image_area.right - (u32)image_area.left));
    return true;
  }
  return false;
}

// Lay out the base slider in opposite value directions on the two axes, then recompute its thumb and invalidate.
void WnSlider::Layout() {
  if (orientation == 1) {
    track_start = inset;
    i32 width = GetW(), finish = (i32)((u32)width - (u32)inset);
    i32 thumb = GetThumbWidth();
    track_finish = (i32)((u32)finish - (u32)thumb);
  } else {
    i32 height = GetH(), finish = (i32)((u32)height - (u32)inset);
    i32 thumb = GetThumbHeight(), start = (i32)((u32)finish - (u32)thumb);
    track_start = start;
    track_finish = inset;
  }
  UpdateThumb();
  InvalidateSelfAndParents();
}

// Reproduce the source FMUL-then-FDIV interpolation under the game's normal 53-bit precision, then truncate to signed64.
void WnSlider::UpdateThumb() {
  i32 range = (i32)((u32)maximum - (u32)minimum), position;
  if (range) {
    i32 length = (i32)((u32)track_finish - (u32)track_start);
    i32 relative = (i32)((u32)value - (u32)minimum);
    volatile f64 product = (f64)length * (f64)relative;
    i64 offset = (i64)(product / (f64)range);
    position = (i32)((u32)track_start + (u32)offset);
  } else
    position = track_start;
  thumb_position = position;
  if (orientation == 1) {
    thumb_area.left = position;
    i32 width = GetThumbWidth();
    thumb_area.right = (i32)((u32)thumb_area.left + (u32)width);
    i32 half = GetThumbHeight() / 2;
    thumb_area.top = GetH() / 2 - half;
  } else {
    i32 half = GetThumbWidth() / 2;
    thumb_area.left = GetW() / 2 - half;
    i32 width = GetThumbWidth();
    thumb_area.right = (i32)((u32)thumb_area.left + (u32)width);
    thumb_area.top = thumb_position;
  }
  i32 height = GetThumbHeight();
  thumb_area.bottom = (i32)((u32)thumb_area.top + (u32)height);
}

// Both original thumb dimensions select the atlas cross-axis size, or the window cross-axis size without an atlas.
i32 WnSlider::GetThumbWidth() {
  if (image) {
    if (orientation == 1)
      return (i32)((u32)image_area.bottom - (u32)image_area.top);
    return (i32)((u32)image_area.right - (u32)image_area.left);
  }
  return orientation == 1 ? GetH() : GetW();
}

i32 WnSlider::GetThumbHeight() {
  return WnSlider::GetThumbWidth();
}

// Clamp the local pointer coordinate to the track, convert it by the source floating calculation and publish the value.
bool WnSlider::ValueFromPoint(i32 *out, i32 x, i32 y) {
  i32 length = (i32)((u32)track_finish - (u32)track_start);
  i32 range = (i32)((u32)maximum - (u32)minimum), position;
  if (orientation == 1) {
    i32 half = GetThumbWidth() / 2;
    position = (i32)((u32)x - (u32)half);
    if (position < inset)
      position = inset;
    i32 width = GetW(), limit = (i32)((u32)width - (u32)inset);
    if (position > limit) {
      width = GetW();
      position = (i32)((u32)width - (u32)inset);
    }
  } else {
    i32 half = GetThumbHeight() / 2;
    position = (i32)((u32)y - (u32)half);
    if (position < inset)
      position = inset;
    i32 height = GetH(), limit = (i32)((u32)height - (u32)inset);
    if (position > limit) {
      height = GetH();
      position = (i32)((u32)height - (u32)inset);
    }
  }
  assert(length != 0);
  i32 relative = (i32)((u32)position - (u32)track_start);
  volatile f64 product = (f64)relative * (f64)range;
  i64 offset = (i64)(product / (f64)length);
  *out = (i32)(u32)offset;
  *out = (i32)((u32)minimum + (u32)offset);
  return true;
}

// A successful capture precedes point conversion and dispatch through the live slider value setter.
bool WnSlider::OnMouseDownL(i32 x, i32 y, u32) {
  i32 requested;
  if (!owner->SetCapture(this) || !ValueFromPoint(&requested, x, y))
    return false;
  SetValue(requested);
  return true;
}

// Drop capture before converting release coordinates, then send the release command with the current control ID.
bool WnSlider::OnMouseUpL(i32 x, i32 y, u32) {
  if (owner->GetCapture() != this)
    return false;
  owner->ReleaseCapture(this);
  i32 requested;
  if (!ValueFromPoint(&requested, x, y))
    return false;
  SetValue(requested);
  u32 id = GetID();
  SendMsg(notification_target, kWnMsg_Command, kWnCommandSliderDragFinished, id, 0);
  return true;
}

// Dragging changes the value only while this actual window owns mouse capture.
bool WnSlider::OnMouseMove(i32 x, i32 y, u32) {
  i32 requested;
  if (owner->GetCapture() != this || !ValueFromPoint(&requested, x, y))
    return false;
  SetValue(requested);
  return true;
}

// Select the original atlas or plain render body; the public paint result is always true after it returns.
bool WnSlider::GZPaint() {
  if (image)
    PaintImage();
  else
    PaintPlain();
  return true;
}

// Return the base slider's primary window identity at zero references; the registry performs its own retained interface query.
ScUnknown *WnConstructSlider() {
  return static_cast<WnWindow *>(new WnSlider);
}

bool WnSlider::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnSlider::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

static i32 WnAddSlider(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

static i32 WnSubSlider(i32 first, i32 second) {
  return (i32)((u32)first - (u32)second);
}

// Move a local rectangle while preserving its size, as the source rectangle helper does.
static void WnMoveSliderRect(ScRect *rectangle, i32 x, i32 y) {
  i32 dx = WnSubSlider(x, rectangle->left), dy = WnSubSlider(y, rectangle->top);
  rectangle->right = WnAddSlider(rectangle->right, dx);
  rectangle->bottom = WnAddSlider(rectangle->bottom, dy);
  rectangle->left = x;
  rectangle->top = y;
}

static void WnOffsetSliderCell(ScRect *rectangle, i32 x) {
  rectangle->left = WnAddSlider(rectangle->left, x);
  rectangle->right = WnAddSlider(rectangle->right, x);
}

// Draw the background, inverted center track and inverted square thumb using the original three lock phases.
bool WnSlider::PaintPlain() {
  if (draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(GetFillColor(), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (draw_buffer->Lock(GxLockFlags::Line)) {
    bool horizontal = orientation == 1;
    u32 color = ~GetFillColor();
    if (horizontal) {
      i32 half = GetH() / 2;
      i32 y1 = (i32)((u32)draw_area.top + (u32)half);
      i32 end = (i32)((u32)draw_area.right - (u32)inset);
      i32 second_half = GetH() / 2, y2 = (i32)((u32)draw_area.top + (u32)second_half);
      i32 start = (i32)((u32)draw_area.left + (u32)inset);
      draw_buffer->DrawLine(start, y2, end, y1, static_cast<i32>(color), 1);
    } else {
      i32 bottom = (i32)((u32)draw_area.bottom - (u32)inset - 1);
      i32 half = GetW() / 2, x2 = (i32)((u32)draw_area.right - (u32)half);
      i32 top = (i32)((u32)draw_area.top + (u32)inset);
      i32 second_half = GetW() / 2, x1 = (i32)((u32)draw_area.left + (u32)second_half);
      draw_buffer->DrawLine(x1, top, x2, bottom, static_cast<i32>(color), 1);
    }
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  if (draw_buffer->Lock(GxLockFlags::Fill)) {
    i32 left = (i32)((u32)draw_area.left + (u32)thumb_area.left);
    i32 top = (i32)((u32)draw_area.top + (u32)thumb_area.top);
    i32 width = GetThumbWidth(), right = (i32)((u32)left + (u32)width);
    i32 height = GetThumbHeight(), bottom = (i32)((u32)top + (u32)height);
    ScRect rectangle{left, top, right, bottom};
    draw_buffer->Fill(~GetFillColor(), rectangle, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  return true;
}

// Blit the slider's first and third end caps, tile its track cell and place the fourth cell at the live thumb position.
bool WnSlider::PaintImage() {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return true;
  ScRect source = image_area;
  ScRect destination{WnAddSlider(draw_area.left, image_area.left), WnAddSlider(draw_area.top, image_area.top),
      WnAddSlider(draw_area.left, image_area.right), WnAddSlider(draw_area.top, image_area.bottom)};
  draw_buffer->Blt(image, source, destination, nullptr);
  if (orientation == 1) {
    i32 width = WnSubSlider(image_area.right, image_area.left);
    source.left = WnAddSlider(source.left, width);
    source.right = WnAddSlider(source.right, width);
    destination.left = WnAddSlider(destination.left, width);
    destination.right = WnSubSlider(draw_area.right, width);
    draw_buffer->BltTiled(image, source, destination, 0, 0, nullptr);
    source.left = WnAddSlider(source.left, width);
    source.right = WnAddSlider(source.right, width);
    destination.left = destination.right;
    destination.right = draw_area.right;
    draw_buffer->Blt(image, source, destination, nullptr);
    source.left = WnAddSlider(source.left, width);
    source.right = WnAddSlider(source.right, width);
  } else if (orientation == 2) {
    i32 height = WnSubSlider(image_area.bottom, image_area.top);
    source.top = WnAddSlider(source.top, height);
    source.bottom = WnAddSlider(source.bottom, height);
    destination.top = destination.bottom;
    destination.bottom = WnSubSlider(draw_area.bottom, height);
    draw_buffer->BltTiled(image, source, destination, 0, 0, nullptr);
    source.top = WnAddSlider(source.top, height);
    source.bottom = WnAddSlider(source.bottom, height);
    destination.top = destination.bottom;
    destination.bottom = draw_area.bottom;
    draw_buffer->Blt(image, source, destination, nullptr);
    source.top = WnAddSlider(source.top, height);
    source.bottom = WnAddSlider(source.bottom, height);
  } else {
    draw_buffer->Unlock(GxLockFlags::Blt);
    return true;
  }
  destination = {WnAddSlider(draw_area.left, image_area.left), WnAddSlider(draw_area.top, image_area.top),
      WnAddSlider(draw_area.left, image_area.right), WnAddSlider(draw_area.top, image_area.bottom)};
  destination.left = WnAddSlider(destination.left, thumb_area.left);
  destination.right = WnAddSlider(destination.right, thumb_area.left);
  destination.top = WnAddSlider(destination.top, thumb_area.top);
  destination.bottom = WnAddSlider(destination.bottom, thumb_area.top);
  draw_buffer->Blt(image, source, destination, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Draw the plain scrollbar end caps, inverted track and thumb. Preserve the source's Lock1/Unlock16 pairing.
bool WnScrollbar::PaintPlain() {
  if (!draw_buffer->Lock(GxLockFlags::Fill))
    return true;
  i32 width = GetThumbWidth(), height = GetThumbHeight();
  ScRect rectangle{draw_area.left, draw_area.top, WnAddSlider(draw_area.left, width), WnAddSlider(draw_area.top, height)};
  if (orientation == 1) {
    draw_buffer->Fill(GetFillColor(), rectangle, nullptr);
    i32 finish = WnSubSlider(draw_area.right, width);
    rectangle = {WnAddSlider(draw_area.left, width), draw_area.top, finish, WnAddSlider(draw_area.top, height)};
    draw_buffer->Fill(~GetFillColor(), rectangle, nullptr);
    rectangle = {finish, draw_area.top, WnAddSlider(finish, width), WnAddSlider(draw_area.top, height)};
    draw_buffer->Fill(GetFillColor(), rectangle, nullptr);
    rectangle = {WnAddSlider(draw_area.left, thumb_area.left), draw_area.top,
        WnAddSlider(WnAddSlider(draw_area.left, thumb_area.left), width), WnAddSlider(draw_area.top, height)};
  } else {
    draw_buffer->Fill(GetFillColor(), rectangle, nullptr);
    i32 finish = WnSubSlider(draw_area.bottom, height);
    rectangle = {draw_area.left, WnAddSlider(draw_area.top, height), WnAddSlider(draw_area.left, width), finish};
    draw_buffer->Fill(~GetFillColor(), rectangle, nullptr);
    rectangle = {draw_area.left, finish, WnAddSlider(draw_area.left, width), WnAddSlider(finish, height)};
    draw_buffer->Fill(GetFillColor(), rectangle, nullptr);
    rectangle = {draw_area.left, WnAddSlider(draw_area.top, thumb_area.top), WnAddSlider(draw_area.left, width),
        WnAddSlider(WnAddSlider(draw_area.top, thumb_area.top), height)};
  }
  draw_buffer->Fill(GetFillColor(), rectangle, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Select pressed end-cap cells from the twelve-state atlas, tile the middle cell and blit the thumb at its live offset.
bool WnScrollbar::PaintImage() {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return true;
  ScRect source = image_area, destination = image_area;
  i32 width = WnSubSlider(image_area.right, image_area.left);
  i32 height = WnSubSlider(image_area.bottom, image_area.top);
  if (repeat_part == 1)
    WnOffsetSliderCell(&source, width);
  WnMoveSliderRect(&destination, draw_area.left, draw_area.top);
  draw_buffer->Blt(image, source, destination, nullptr);
  source = image_area;
  WnOffsetSliderCell(&source, (i32)((u32)width * 3));
  if (orientation == 1) {
    destination.left = destination.right;
    destination.right = WnSubSlider(draw_area.right, width);
  } else {
    destination.top = destination.bottom;
    destination.bottom = WnSubSlider(draw_area.bottom, height);
  }
  draw_buffer->BltTiled(image, source, destination, 0, 0, nullptr);
  source = image_area;
  WnOffsetSliderCell(&source, (i32)((u32)width * (repeat_part == 2 ? 5u : 4u)));
  if (orientation == 1) {
    destination.left = destination.right;
    destination.right = draw_area.right;
  } else {
    destination.top = destination.bottom;
    destination.bottom = draw_area.bottom;
  }
  draw_buffer->Blt(image, source, destination, nullptr);
  source = image_area;
  destination = image_area;
  WnOffsetSliderCell(&source, (i32)((u32)width * 2));
  if (orientation == 1)
    WnMoveSliderRect(&destination, WnAddSlider(draw_area.left, thumb_area.left), draw_area.top);
  else
    WnMoveSliderRect(&destination, draw_area.left, WnAddSlider(draw_area.top, thumb_area.top));
  draw_buffer->Blt(image, source, destination, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

WnSpinner::WnSpinner()
    : spinner_flags(0x1000), spinner_type(0), timer(ScStopwatch::Milliseconds, 0), repeat_delay(400), pressed_part(0),
      hover_part(0), minimum(0), maximum(0), value(0), step(1), font(nullptr), font_height(0), alignment(2),
      text_colors{kWnDefaultColor, kWnDefaultColor, kWnDefaultColor}, image(nullptr), source_x(0), source_y(0),
      cell_width(0), cell_height(0), horizontal_gutter(4), vertical_gutter(1), outline_colors{},
      maximum_button_width(16), minimum_button_extent(8) {
}

WnSpinner::~WnSpinner() {
  WnSpinner::Shutdown();
}

// Publish the real secondary spinner interface for its own IID; all other identities use the primary root contract.
bool WnSpinner::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnSpinnerControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnSpinnerControl *>(this);
  AddRef();
  return true;
}

u32 WnSpinner::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnSpinner::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window from the displaced spinner control without acquiring another reference.
WnWindow *WnSpinner::GetWindow() {
  return static_cast<WnWindow *>(this);
}

// Resolve resource defaults on every call, enforce positive dimensions, refresh the caption and post the initial value.
bool WnSpinner::Init() {
  if ((u32)spinner_type > 2)
    return false;
  WnResources *resources = WnGetUiResources();
  if (!image && resources)
    SetImage(resources->GetImage(kWnThemeSpinnerImageId), nullptr);
  if (spinner_type && resources)
    SetFont(resources->GetFont(kWnFontDefault));
  if (GetW() < 1)
    SetW(1);
  if (GetH() < 1)
    SetH(1);
  RefreshCaption();
  i32 selected = value;
  u32 control_id = GetID();
  PostMsg(notification_target, kWnMsg_None, control_id, (u32)selected, 0);
  return WnRootWidget::Init();
}

// Hide before releasing image then font, clearing each live field after release, and then run the root close phase.
bool WnSpinner::Shutdown() {
  HideWindow();
  ScRelease(&image);
  ScRelease(&font);
  return WnRootWidget::Shutdown();
}

// Enforce the source image/font/button minima, update unskinned cell dimensions and invalidate after root geometry.
void WnSpinner::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (image) {
    i32 minimum_width, minimum_height;
    if (spinner_type) {
      minimum_width = (i32)((u32)cell_width + 2u * horizontal_gutter);
      minimum_height = font_height + 2 * vertical_gutter;
      i32 both_cells = (i32)(2u * (u32)cell_height);
      if (both_cells > minimum_height)
        minimum_height = both_cells;
    } else {
      minimum_width = cell_width;
      minimum_height = (i32)(2u * (u32)cell_height);
    }
    if (width < minimum_width)
      width = minimum_width;
    if (height < minimum_height)
      height = minimum_height;
  } else if (spinner_type) {
    i32 lower = minimum_button_extent, minimum_height = font_height + 2 * vertical_gutter;
    if (height < minimum_height)
      height = minimum_height;
    if (width < lower + 2 * horizontal_gutter)
      width = lower + 2 * horizontal_gutter;
    cell_height = height / 2;
    cell_width = height / 2;
    if (height / 2 < lower || (lower = maximum_button_width, height / 2 > lower))
      cell_width = lower;
  } else {
    if (width < minimum_button_extent)
      width = minimum_button_extent;
    if (height < minimum_button_extent)
      height = minimum_button_extent;
    cell_width = width;
    cell_height = height / 2;
  }
  WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  InvalidateSelfAndParents();
}

void WnSpinner::RequeryAreaEffects() {
  const ScRect *rectangle = GetArea();
  SetArea(rectangle->left, rectangle->top, rectangle->right, rectangle->bottom);
}

// Accept only the three source spinner modes, obtaining a missing digit font before publishing and relaying out.
bool WnSpinner::SetSpinnerType(i32 type) {
  if (type < 0 || type > 2)
    return false;
  if (type && !font) {
    WnResources *resources = WnGetUiResources();
    if (resources)
      SetFont(resources->GetFont(kWnFontDefault));
  }
  spinner_type = type;
  RequeryAreaEffects();
  return true;
}

bool WnSpinner::GetSpinnerType(i32 *out) const {
  *out = spinner_type;
  return true;
}

// Update flags and preserve the exact-mask invalidation and mutually exclusive number-format selections.
bool WnSpinner::SetSpinnerFlag(i32 mask, bool enabled) {
  if (enabled)
    spinner_flags |= (u32)mask;
  else
    spinner_flags &= ~(u32)mask;
  if (mask > 0) {
    if (mask <= 2)
      InvalidateSelfAndParents();
    else if (mask == 0x2000 || mask == 0x4000) {
      if (enabled)
        spinner_flags &= ~(mask == 0x2000 ? 0x4000u : 0x2000u);
      RefreshCaption();
    }
  }
  return true;
}

int WnSpinner::GetSpinnerFlag(i32 mask) const {
  return (spinner_flags & (u32)mask) != 0;
}

i32 WnSpinner::GetValue() {
  return value;
}

// Clamp minimum first and maximum second.
// Only a changed value refreshes text and synchronously notifies the live target.
bool WnSpinner::SetValue(i32 incoming) {
  if (incoming < minimum)
    incoming = minimum;
  if (incoming > maximum)
    incoming = maximum;
  bool changed = value != incoming;
  value = incoming;
  if (changed) {
    RefreshCaption();
    i32 selected = value;
    u32 control_id = GetID();
    SendMsg(notification_target, kWnMsg_None, control_id, (u32)selected, 0);
  }
  return true;
}

// Store both bounds without clamping the current value, swapping bounds or repainting.
bool WnSpinner::SetMinMax(i32 low, i32 high) {
  minimum = low;
  maximum = high;
  return true;
}

// Copy both signed bounds into their separate original outputs.
bool WnSpinner::GetMinMax(i32 *low, i32 *high) {
  *low = minimum;
  *high = maximum;
  return true;
}

bool WnSpinner::SetStepSize(i32 incoming) {
  step = incoming;
  return true;
}

bool WnSpinner::GetStepSize(i32 *out) {
  *out = step;
  return true;
}

// Release before publishing and retaining an image, derive four-by-two cells, then reapply the current root rectangle.
bool WnSpinner::SetImage(GxBuffer *incoming, const ScRect *rectangle) {
  if (image)
    image->Release();
  image = incoming;
  if (incoming) {
    incoming->AddRef();
    ScRect selected;
    if (rectangle)
      selected = *rectangle;
    else
      image->GetBufferArea(&selected);
    cell_width = (i32)((u32)selected.right - (u32)selected.left) / 4;
    cell_height = (i32)((u32)selected.bottom - (u32)selected.top) / 2;
    source_x = (u16)selected.left;
    source_y = (u16)selected.top;
  }
  const ScRect *current = GetArea();
  SetArea(current->left, current->top, current->right, current->bottom);
  return true;
}

// Release the old font before publishing and retaining its replacement, store signed WORD height and reapply geometry.
bool WnSpinner::SetFont(FtFont *incoming) {
  if (font)
    font->Release();
  font = incoming;
  if (incoming) {
    incoming->AddRef();
    font_height = (i16)font->Height(0);
  } else
    font_height = 0;
  RequeryAreaEffects();
  return true;
}

// Store a changed font-color entry and invalidate ancestors only when its actual DWORD differs.
bool WnSpinner::SetFontColor(i32 index, u32 color) {
  if (text_colors[index] != color) {
    text_colors[index] = color;
    InvalidateSelfAndParents();
  }
  return true;
}

bool WnSpinner::ResetFontColor(i32 index) {
  SetFontColor(index, kWnDefaultColor);
  return true;
}

// Accept only left/center/right, store the source byte and invalidate even when the accepted alignment is unchanged.
bool WnSpinner::SetTextAlignment(i32 incoming) {
  if (incoming < 0 || incoming > 2)
    return false;
  alignment = (u8)incoming;
  InvalidateSelfAndParents();
  return true;
}

// Expand a valid alignment byte into its original DWORD output, leaving the output unchanged for other stored bytes.
bool WnSpinner::GetTextAlignment(i32 *out) const {
  if (alignment > 2)
    return false;
  *out = alignment;
  return true;
}

// Store both unsigned gutter bytes before reapplying the current rectangle through the live scalar overload.
bool WnSpinner::SetGutters(u8 horizontal, u8 vertical) {
  horizontal_gutter = horizontal;
  vertical_gutter = vertical;
  RequeryAreaEffects();
  return true;
}

// Copy the exact gutter bytes into the separate original outputs.
bool WnSpinner::GetGutters(u8 *horizontal, u8 *vertical) const {
  *horizontal = horizontal_gutter;
  *vertical = vertical_gutter;
  return true;
}

// Truncate the four DWORD color inputs to their real WORD fields before invalidating ancestors.
bool WnSpinner::SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) {
  outline_colors[0] = (u16)left;
  outline_colors[1] = (u16)top;
  outline_colors[2] = (u16)right;
  outline_colors[3] = (u16)bottom;
  InvalidateSelfAndParents();
  return true;
}

// Expand each stored outline WORD into its genuine DWORD output.
bool WnSpinner::GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) {
  *left = outline_colors[0];
  *top = outline_colors[1];
  *right = outline_colors[2];
  *bottom = outline_colors[3];
  return true;
}

// Format the signed value using the selected language utility when available; otherwise use plain decimal text.
bool WnSpinner::RefreshCaption() {
  if (spinner_flags & 0x1000) {
    if (spinner_flags & (0x2000 | 0x4000)) {
      bool number = (spinner_flags & 0x2000) != 0;
      ScLanguageManager *manager = WnGetLanguageManager();
      ScLanguageUtility *utility = manager ? manager->GetLanguageUtility(kScLanguageCurrent) : nullptr;
      if (utility) {
        if (number)
          utility->MakeNumberString((i64)value, &caption);
        else
          utility->MakeMoneyString((i64)value, &caption);
        InvalidateSelfAndParents();
        return true;
      }
    }
    char digits[12];
    snprintf(digits, sizeof(digits), "%d", (int)value);
    caption.FromChar(digits);
  }
  InvalidateSelfAndParents();
  return true;
}

// Identify an up/down half only within the active button column; an inside text hit still returns true with part zero.
bool WnSpinner::PartFromPoint(i32 *out, i32 x, i32 y) {
  if (!IsPointInWindow(x, y)) {
    *out = 0;
    return false;
  }
  i32 height = GetH();
  if (spinner_type == 1) {
    i32 width = GetW();
    if (x < (i32)((u32)width - (u32)cell_width)) {
      *out = 0;
      return true;
    }
  } else if (spinner_type && x > cell_width) {
    *out = 0;
    return true;
  }
  *out = (y >= height / 2) + 1;
  return true;
}

// Capture the pressed half, restart the repeat timer and apply one live signed step before invalidation.
bool WnSpinner::OnMouseDownL(i32 x, i32 y, u32) {
  PartFromPoint(&pressed_part, x, y);
  repeat_delay = 400;
  timer.Restart();
  if (pressed_part == 1)
    SetValue((i32)((u32)value + (u32)step));
  else if (pressed_part == 2)
    SetValue((i32)((u32)value - (u32)step));
  InvalidateSelfAndParents();
  return true;
}

bool WnSpinner::OnMouseUpL(i32, i32, u32) {
  repeat_delay = 400;
  if (!hover_part)
    owner->ReleaseCapture(this);
  pressed_part = 0;
  InvalidateSelfAndParents();
  return true;
}

// Track hover changes, resetting delay during a pressed drag.
// Unpressed transitions acquire or release the actual owner capture.
bool WnSpinner::OnMouseMove(i32 x, i32 y, u32) {
  i32 previous = hover_part;
  PartFromPoint(&hover_part, x, y);
  if (pressed_part) {
    if (previous != hover_part) {
      repeat_delay = 400;
      InvalidateSelfAndParents();
    }
  } else if (previous != hover_part) {
    if (hover_part)
      owner->SetCapture(this);
    else
      owner->ReleaseCapture(this);
    InvalidateSelfAndParents();
  }
  return true;
}

// Accept self capture notification without changing state.
// A different window clears the press and invalidates while leaving the event unhandled.
bool WnSpinner::GZOnCaptureChanged(WnWindow *window) {
  if (window == this)
    return true;
  pressed_part = 0;
  InvalidateSelfAndParents();
  return false;
}

// Apply End/Home and up/down through the live value setter; other keys retain the target's synchronous handled result.
bool WnSpinner::OnKeyDown(u32 key, u32 modifiers) {
  if (key == kWnKeyEnd)
    SetValue(maximum);
  else if (key == kWnKeyHome)
    SetValue(minimum);
  else if (key == kWnKeyUp)
    SetValue((i32)((u32)value + (u32)step));
  else if (key == kWnKeyDown)
    SetValue((i32)((u32)value - (u32)step));
  else
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  return true;
}

bool WnSpinner::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Return the registered secondary spinner identity at zero references for the registry's retained query.
ScUnknown *WnConstructSpinner() {
  return static_cast<WnSpinnerControl *>(new WnSpinner);
}

static i32 WnSpinnerAdd(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

static i32 WnSpinnerSubtract(i32 first, i32 second) {
  return (i32)((u32)first - (u32)second);
}

// Assert before the fallback consumes its original unwritten bevel color.
// Failed fallback locks remain defined and never enter this boundary.
[[noreturn]] static void WnUnavailableSpinnerBevelColor() {
  assert(false && "The original spinner fallback color is uninitialized");
  std::unreachable();
}

void WnSpinner::PaintFallbackEffects() {
  if (draw_buffer->Lock(GxLockFlags::Fill))
    WnUnavailableSpinnerBevelColor();
}

// Draw the atlas's upper and lower rows using live button state and the captured enabled flag and half height.
void WnSpinner::PaintImageEffects(i32 offset) {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return;
  i32 half = GetH() / 2;
  bool enabled = GetFlag(kWnFlagEnabled) != 0;
  i32 x = WnSpinnerAdd(offset, draw_area.left);
  i32 y = WnSpinnerAdd(draw_area.top, WnSpinnerSubtract(half, cell_height) / 2);
  ScRect destination{x, y, WnSpinnerAdd(x, cell_width), WnSpinnerAdd(y, cell_height)};
  i32 column = enabled ? 1 : 0;
  if (enabled) {
    if (pressed_part == 1 && hover_part == 1)
      column = 2;
    else if (!pressed_part && hover_part == 1)
      column = 3;
  }
  i32 left = WnSpinnerAdd(source_x, (i32)((u32)column * (u32)cell_width));
  ScRect source{left, source_y, WnSpinnerAdd(left, cell_width), WnSpinnerAdd(source_y, cell_height)};
  draw_buffer->Blt(image, source, destination, nullptr);
  i32 height = cell_height;
  source.top = WnSpinnerAdd(source.top, height);
  source.bottom = WnSpinnerAdd(source.bottom, height);
  destination.top = WnSpinnerAdd(destination.top, half);
  destination.bottom = WnSpinnerAdd(destination.bottom, half);
  if (enabled) {
    column = 1;
    if (pressed_part == 2 && hover_part == 2)
      column = 2;
    else if (!pressed_part && hover_part == 2)
      column = 3;
    source.left = WnSpinnerAdd(source_x, (i32)((u32)column * (u32)cell_width));
    source.right = WnSpinnerAdd(source.left, cell_width);
  }
  draw_buffer->Blt(image, source, destination, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
}

// Draw the optional number panel and atlas buttons, then repeat a held arrow after its elapsed delay.
// Border and fill lock failures stop painting; image and font results do not suppress the repeat phase.
bool WnSpinner::GZPaint() {
  ScRect panel{}, text{};
  i32 type = spinner_type;
  i32 offset = 0;
  bool outline = false, fill = false, draw_text = false;
  if (type == 1 || type == 2) {
    if (type == 1) {
      i32 width = GetW();
      offset = WnSpinnerSubtract(width, cell_width);
      i32 gutter = horizontal_gutter;
      panel = {draw_area.left, draw_area.top, WnSpinnerAdd(WnSpinnerSubtract(draw_area.left, gutter), offset),
          draw_area.bottom};
      text.left = WnSpinnerAdd(draw_area.left, gutter);
    } else {
      i32 gutter = horizontal_gutter;
      panel = {WnSpinnerAdd(WnSpinnerAdd(draw_area.left, cell_width), gutter), draw_area.top, draw_area.right,
          draw_area.bottom};
      text.left = WnSpinnerAdd(panel.left, gutter);
    }
    i32 height = font_height;
    i32 window_height = GetH();
    text.top = WnSpinnerAdd(draw_area.top, WnSpinnerSubtract(window_height, height) / 2);
    text.right = WnSpinnerSubtract(type == 1 ? panel.right : draw_area.right, horizontal_gutter);
    text.bottom = WnSpinnerAdd(text.top, height);
    outline = (spinner_flags & 1) != 0;
    fill = (spinner_flags & 2) != 0;
    draw_text = font != nullptr;
  }
  if (outline) {
    if (!draw_buffer->Lock(GxLockFlags::Line))
      return false;
    i32 bottom = WnSpinnerSubtract(panel.bottom, 1);
    draw_buffer->DrawLine(panel.left, panel.top, panel.left, bottom, static_cast<i32>(outline_colors[0]), 1);
    draw_buffer->DrawLine(
        panel.left, panel.top, WnSpinnerSubtract(panel.right, 1), panel.top, static_cast<i32>(outline_colors[1]), 1);
    draw_buffer->DrawLine(WnSpinnerSubtract(panel.right, 1), panel.top, WnSpinnerSubtract(panel.right, 1),
        WnSpinnerSubtract(panel.bottom, 1), static_cast<i32>(outline_colors[2]), 1);
    draw_buffer->DrawLine(
        panel.left, bottom, WnSpinnerSubtract(panel.right, 1), bottom, static_cast<i32>(outline_colors[3]), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
    panel.left = WnSpinnerAdd(panel.left, 1);
    panel.right = WnSpinnerSubtract(panel.right, 1);
    panel.top = WnSpinnerAdd(panel.top, 1);
    panel.bottom = WnSpinnerSubtract(panel.bottom, 1);
  }
  if (fill) {
    if (!draw_buffer->Lock(GxLockFlags::Fill))
      return false;
    u32 color = GetFillColor();
    draw_buffer->Fill(color, panel, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (image)
    PaintImageEffects(offset);
  else
    PaintFallbackEffects();
  if (draw_text) {
    u32 color = text_colors[GetFlag(kWnFlagEnabled) ? (hover_part ? 1 : 0) : 2];
    if (color == kWnDefaultColor) {
      u32 length = caption.Strlen();
      const char *bytes = caption.Data();
      font->DrawTextPara(draw_buffer, text, bytes, length, FtOutputStyle(alignment));
    } else {
      font->DrawTextPara(
          draw_buffer, text, caption.begin, (u32)(caption.end - caption.begin), color, FtOutputStyle(alignment));
    }
  }
  i32 direction = 0;
  if (pressed_part == 2 && hover_part == 2)
    direction = -1;
  else if (pressed_part == 1 && hover_part == 1)
    direction = 1;
  if (direction) {
    u32 elapsed = timer.GetElapsedTime();
    if (elapsed > repeat_delay) {
      SetValue(direction > 0 ? WnSpinnerAdd(value, step) : WnSpinnerSubtract(value, step));
      if (repeat_delay > 50)
        repeat_delay -= 50;
      timer.Restart();
    }
    InvalidateSelfAndParents();
  }
  return true;
}
