#include "ui/graph_window.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include "common/api.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "fonts/sc3_font_manager.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "ui/api.h"
#include "ui/ui_graph_window.h"
#include "ui/ui_module.h"

// Graph rectangles and axis positions start unset until explicit placement or automatic layout supplies them.
inline constexpr i32 kUiGraphUnsetCoordinate = INT32_MAX;

UiGraphArrow::UiGraphArrow()
    : unknown4(0), first{0, 0}, second{0, 0}, color(0), thickness(1), first_head(1), second_head(0), head_length(8),
      head_angle(0.7854f) {
}

// Retire the arrow without releasing additional resources.
UiGraphArrow::~UiGraphArrow() = default;

// Draw the line and perform the original head calculations when either head is enabled.
// The installed build discards both calculation results; floating-point side effects remain unverified.
void UiGraphArrow::DrawSelf(UiGraph *, GxBuffer *buffer) {
  if (!buffer->Lock(GxLockFlags::Line))
    return;
  buffer->DrawLine(first.x, first.y, second.x, second.y, (i32)color, thickness);
  if (first_head || second_head) {
    if (first.x == second.x && first.y == second.y)
      second.x = (i32)((u32)second.x + 1);
    i32 dx = (i32)((u32)first.x - (u32)second.x), dy = (i32)((u32)first.y - (u32)second.y);
    i32 squared = (i32)((u32)dx * (u32)dx + (u32)dy * (u32)dy);
    volatile f64 length = sqrt((f64)squared), slope = tan((f64)head_angle);
    (void)length;
    (void)slope;
  }
  buffer->Unlock(GxLockFlags::Line);
}

// Create an arrow record, append it to the owned drawable list and return it to the caller.
UiGraphArrow *UiGraphWindow::AddMiscGraphArrow() {
  UiGraphArrow *arrow = new UiGraphArrow;
  AddDynamicGraphObject(arrow);
  return arrow;
}

// Load a graph font with the original temporary language switch, restoring the prior language afterwards.
FtFont *UiGraphLoadFont(i32 type, u32 size, u32 flags, u32 color) {
  FtFontSystem *manager = UiGetFontManager();
  if (!manager)
    return nullptr;
  ScLanguageManager *language = UiGetLanguageManager();
  u32 previous = 0;
  if (language) {
    previous = language->GetCurrentLanguage();
    language->SetCurrentLanguage(1);
  }
  FtFont *font = manager->FontAcquire(static_cast<FtType>(type), size, flags, color);
  if (language)
    language->SetCurrentLanguage(previous);
  return font;
}

UiGraphLineInfo::UiGraphLineInfo() : unknown4(0), first{0, 0}, second{0, 0}, color(0), thickness(1) {
}

// Destroy a line record without releasing any additional resources.
UiGraphLineInfo::~UiGraphLineInfo() = default;

// Lock the line surface, draw the stored segment and unlock it when locking succeeds.
void UiGraphLineInfo::DrawSelf(UiGraph *, GxBuffer *buffer) {
  if (buffer->Lock(GxLockFlags::Line)) {
    buffer->DrawLine(first.x, first.y, second.x, second.y, (i32)color, thickness);
    buffer->Unlock(GxLockFlags::Line);
  }
}

// Create a line record, append it to the graph's owned objects and return it to the caller.
UiGraphLineInfo *UiGraphWindow::AddMiscGraphLine() {
  UiGraphLineInfo *line = new UiGraphLineInfo;
  AddDynamicGraphObject(line);
  return line;
}

UiGraphRectInfo::UiGraphRectInfo()
    : unknown4(0), area{0, 0, 0, 0}, outline(1), thickness(1), outline_color(0), fill(0),
      fill_color(kUiGraphDefaultFillColor) {
}

// Destroy a rectangle record without releasing any additional resources.
UiGraphRectInfo::~UiGraphRectInfo() = default;

// Draw the optional fill first, followed by the optional outline, with independent locks.
void UiGraphRectInfo::DrawSelf(UiGraph *, GxBuffer *buffer) {
  if (fill && buffer->Lock(GxLockFlags::Fill)) {
    buffer->Fill(fill_color, &area);
    buffer->Unlock(GxLockFlags::Fill);
  }
  if (outline && buffer->Lock(GxLockFlags::Rect)) {
    buffer->DrawRectangleOutline(area, outline_color, thickness);
    buffer->Unlock(GxLockFlags::Rect);
  }
}

// Create a rectangle record, append it to the graph's owned objects and return it to the caller.
UiGraphRectInfo *UiGraphWindow::AddMiscGraphRect() {
  UiGraphRectInfo *rectangle = new UiGraphRectInfo;
  AddDynamicGraphObject(rectangle);
  return rectangle;
}

UiGraphPicture::UiGraphPicture() : unknown4(0), image(nullptr), x(0), y(0), relative_to_graph(0) {
}

UiGraphPicture::~UiGraphPicture() {
  ScRelease(&image);
}

bool UiGraphPicture::Init() {
  return true;
}

// Retain the replacement picture before releasing its previous image.
void UiGraphPicture::SetBuffer(GxBuffer *buffer) {
  if (buffer)
    buffer->AddRef();
  if (image)
    image->Release();
  image = buffer;
}

// Blit the whole image at its stored position, optionally relative to the graph area.
void UiGraphPicture::DrawSelf(UiGraph *graph, GxBuffer *buffer) {
  ScRect source = *image->GetBufferArea();
  ScRect destination{
      x, y, (i32)((u32)x + (u32)source.right - (u32)source.left), (i32)((u32)y + (u32)source.bottom - (u32)source.top)};
  if (relative_to_graph && graph) {
    ScRect bounds;
    graph->GetGraphArea(&bounds);
    destination.left = (i32)((u32)destination.left + (u32)bounds.left);
    destination.right = (i32)((u32)destination.right + (u32)bounds.left);
    destination.top = (i32)((u32)destination.top + (u32)bounds.top);
    destination.bottom = (i32)((u32)destination.bottom + (u32)bounds.top);
  }
  if (buffer->Lock(GxLockFlags::Blt)) {
    buffer->Blt(image, source, destination, static_cast<const ScRect *>(nullptr));
    buffer->Unlock(GxLockFlags::Blt);
  }
}

// Create a picture record, append it to the owned drawable list and return it to the caller.
UiGraphPicture *UiGraphWindow::AddMiscGraphPicture() {
  UiGraphPicture *picture = new UiGraphPicture;
  AddDynamicGraphObject(picture);
  return picture;
}

UiGraphTextInfo::UiGraphTextInfo()
    : unknown4(0), area{0, 0, 0, 0}, alignment(0), outline(0), outline_color(0), fill(0),
      fill_color(kUiGraphDefaultFillColor), font(nullptr), font_color(kUiGraphUseFontColor) {
}

UiGraphTextInfo::~UiGraphTextInfo() {
  ScRelease(&font);
}

// Load and retain the default font only when no font was already supplied.
bool UiGraphTextInfo::Init() {
  if (!font)
    font = ScAddRefNotNull(UiGraphLoadFont(0, 12, 0, 0));
  return font != nullptr;
}

// Retain the replacement font before releasing the previous one and recording the color.
void UiGraphTextInfo::SetFont(FtFont *value, u32 color) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  font_color = color;
}

void UiGraphTextInfo::DrawSelf(UiGraph *, GxBuffer *buffer) {
  if (fill && buffer->Lock(GxLockFlags::Fill)) {
    buffer->Fill(fill_color, &area);
    buffer->Unlock(GxLockFlags::Fill);
  }
  if (outline && buffer->Lock(GxLockFlags::Rect)) {
    buffer->DrawRectangleOutline(area, outline_color, 1);
    buffer->Unlock(GxLockFlags::Rect);
  }
  if (font) {
    const char *bytes = text.begin;
    u32 length = (u32)(text.end - text.begin);
    if (font_color == kUiGraphUseFontColor)
      font->DrawTextPara(buffer, area, bytes, length, static_cast<FtOutputStyle>(alignment));
    else
      font->DrawTextPara(buffer, area, bytes, length, font_color, static_cast<FtOutputStyle>(alignment));
  }
}

// Create a text record, append it to the owned drawable list and return it to the caller.
UiGraphTextInfo *UiGraphWindow::AddMiscGraphTextInfo() {
  UiGraphTextInfo *text = new UiGraphTextInfo;
  AddDynamicGraphObject(text);
  return text;
}

void UiGraphWindow::SetGraphArea(const ScRect &plot_area) {
  graph_area = plot_area;
}

void UiGraphWindow::GetGraphArea(ScRect *plot_area) {
  *plot_area = graph_area;
}

void UiGraphWindow::SetBackgroundOutline(bool enabled) {
  background_outline = enabled;
}

void UiGraphWindow::SetBackgroundFillColor(u32 color) {
  background_fill_color = color;
}

void UiGraphWindow::SetBackgroundOutlineColor(u32 color) {
  background_outline_color = color;
}

void UiGraphWindow::SetGraphAreaOutlineAndFill(bool outline, bool fill) {
  graph_outline = outline;
  graph_fill = fill;
}

void UiGraphWindow::SetGraphAreaOutlineColor(u32 color) {
  graph_outline_color = color;
}

void UiGraphWindow::SetGraphAreaFillColor(u32 color) {
  graph_fill_color = color;
}

// Retain the new background pattern before releasing the previous pattern.
void UiGraphWindow::SetBackgroundPattern(GxBuffer *pattern) {
  if (pattern)
    pattern->AddRef();
  if (background_pattern)
    background_pattern->Release();
  background_pattern = pattern;
}

// Retain the new plotting-area pattern before releasing the previous pattern.
void UiGraphWindow::SetGraphBackgroundPattern(GxBuffer *pattern) {
  if (pattern)
    pattern->AddRef();
  if (graph_background_pattern)
    graph_background_pattern->Release();
  graph_background_pattern = pattern;
}

void UiGraphWindow::SetAxisMinValue(UiGraphAxis axis, f32 value) {
  axis_min[(i32)axis] = value;
}

void UiGraphWindow::SetAxisMaxValue(UiGraphAxis axis, f32 value) {
  axis_max[(i32)axis] = value;
}

void UiGraphWindow::SetAxisZeroPosition(UiGraphAxis axis, i32 position) {
  axis_zero_position[(i32)axis] = position;
}

void UiGraphWindow::SetAxisLabel(UiGraphAxis axis, const ScString &label, bool automatic) {
  i32 index = (i32)axis;
  axis_label[index] = label;
  if (automatic)
    axis_label_position[index].left = kUiGraphUnsetCoordinate;
}

void UiGraphWindow::SetAxisLabelPosition(UiGraphAxis axis, ScRect *position) {
  axis_label_position[(i32)axis] = *position;
}

void UiGraphWindow::SetAxisLabelFont(UiGraphAxis axis, FtFont *font, u32 color) {
  i32 index = (i32)axis;
  if (font)
    font->AddRef();
  if (axis_label_font[index])
    axis_label_font[index]->Release();
  axis_label_font[index] = font;
  axis_label_color[index] = color;
}

void UiGraphWindow::SetAxisLabelRotation(UiGraphAxis axis, UiGraphRotation rotation) {
  axis_label_rotation[(i32)axis] = rotation;
}

void UiGraphWindow::SetAxisLineColor(u32 color) {
  axis_line_color = color;
}

void UiGraphWindow::SetMajorTickUsage(UiGraphAxis axis, UiGraphUsage usage) {
  major_tick_usage[(i32)axis] = (i32)usage != 0;
}

void UiGraphWindow::SetMajorTickSpacing(UiGraphAxis axis, f32 spacing) {
  major_tick_spacing[(i32)axis] = spacing;
}

void UiGraphWindow::SetMajorTickGridUsage(UiGraphAxis axis, UiGraphUsage usage) {
  major_tick_grid[(i32)axis] = (i32)usage != 0;
}

// Clamp the selected axis's major-tick decimal count to zero through nine.
void UiGraphWindow::SetMajorTickDecimalPlaces(UiGraphAxis axis, i32 places) {
  if (places < 0)
    places = 0;
  else if (places > 9)
    places = 9;
  major_tick_decimals[(i32)axis] = (u8)places;
}

void UiGraphWindow::SetMajorTickLength(UiGraphAxis axis, i32 length) {
  major_tick_length[(i32)axis] = length;
}

void UiGraphWindow::SetAxisTickFont(UiGraphAxis axis, FtFont *font, u32 color) {
  i32 index = (i32)axis;
  if (font)
    font->AddRef();
  if (axis_tick_font[index])
    axis_tick_font[index]->Release();
  axis_tick_font[index] = font;
  axis_tick_color[index] = color;
}

void UiGraphWindow::SetAxisTickTextDrawing(UiGraphAxis axis, UiGraphUsage usage) {
  tick_text[(i32)axis] = (i32)usage != 0;
}

void UiGraphWindow::SetMinorTickUsage(UiGraphAxis axis, UiGraphUsage usage) {
  minor_tick_usage[(i32)axis] = (i32)usage == 1;
}

void UiGraphWindow::SetMinorTickSpacing(UiGraphAxis axis, f32 spacing) {
  minor_tick_spacing[(i32)axis] = spacing;
}

void UiGraphWindow::SetMinorTickGridUsage(UiGraphAxis axis, UiGraphUsage usage) {
  minor_tick_grid[(i32)axis] = (i32)usage == 1;
}

void UiGraphWindow::SetMinorTickLength(UiGraphAxis axis, i32 length) {
  minor_tick_length[(i32)axis] = length;
}

void UiGraphWindow::SetTickColor(u32 color) {
  tick_color = color;
}

void UiGraphWindow::SetTitle(const ScString &text, bool automatic) {
  title = text;
  if (automatic)
    title_position.left = kUiGraphUnsetCoordinate;
}

void UiGraphWindow::SetTitlePosition(ScRect *position) {
  title_position = *position;
}

// Retain the new title font before releasing the previous font and storing its color.
void UiGraphWindow::SetTitleFont(FtFont *font, u32 color) {
  if (font)
    font->AddRef();
  if (title_font)
    title_font->Release();
  title_font = font;
  title_color = color;
}

// Return the graph's borrowed hit-test result storage.
void *UiGraphWindow::GetHitTestResultData() {
  return hit_test_result;
}

bool UiGraphWindow::DoHitTest(ScPoint *, u32 *) {
  return false;
}

void UiGraphWindow::SetAxisLabelFont(UiGraphAxis axis, i32 size, u32 first, u32 second, u32 third) {
  FtFont *font = UiGraphLoadFont(size, first, second, third);
  SetAxisLabelFont(axis, font, kUiGraphUseFontColor);
}

void UiGraphWindow::SetAxisTickFont(UiGraphAxis axis, i32 size, u32 first, u32 second, u32 third) {
  FtFont *font = UiGraphLoadFont(size, first, second, third);
  SetAxisTickFont(axis, font, kUiGraphUseFontColor);
}

void UiGraphWindow::SetTitleFont(i32 size, u32 first, u32 second, u32 third) {
  FtFont *font = UiGraphLoadFont(size, first, second, third);
  SetTitleFont(font, kUiGraphUseFontColor);
}

// Fill missing title, axis-label and axis-tick fonts, then mark the font-loading pass complete.
void UiGraphWindow::LoadDefaultFonts() {
  if (!title_font)
    SetTitleFont(32, 0, 1, kUiGraphUseFontColor);
  for (i32 index = 0; index < 2; ++index) {
    UiGraphAxis axis = static_cast<UiGraphAxis>(index);
    if (!axis_label_font[index])
      SetAxisLabelFont(axis, 1, 16, 0, kUiGraphUseFontColor);
    if (!axis_tick_font[index])
      SetAxisTickFont(axis, 1, 14, 0, kUiGraphUseFontColor);
  }
  fonts_loaded = 1;
}

// Fill unset graph bounds, layout rectangles and tick settings before drawing.
void UiGraphWindow::CommitDefaultValues() {
  for (i32 index = 0; index < 2; ++index) {
    UiGraphAxis axis = (UiGraphAxis)index;
    bool automatic = true;
    f32 value;
    if (axis_min[index] == 0.0f || axis_min[index] != axis_min[index]) {
      GetAutomaticAxisMinValue(axis, &value);
      axis_min[index] = value;
    } else
      automatic = false;
    if (axis_max[index] == 0.0f || axis_max[index] != axis_max[index]) {
      GetAutomaticAxisMaxValue(axis, &value);
      axis_max[index] = value;
    } else
      automatic = false;
    if (automatic &&
        (axis_min[index] == axis_max[index] || axis_min[index] != axis_min[index] ||
            axis_max[index] != axis_max[index])) {
      if (axis_max[index] == 0.0f || axis_max[index] != axis_max[index])
        axis_max[index] = 1.0f;
      else
        axis_max[index] = (f32)((f64)axis_max[index] * (f64)1.1111111640930176f);
    }
    if (major_tick_decimals[index] == 0x81) {
      f64 range = (f64)axis_max[index] - (f64)axis_min[index];
      if (range >= 10.0)
        major_tick_decimals[index] = 0;
      else if (!(range >= (f64)0.0001f))
        major_tick_decimals[index] = 5;
      else if (!(range >= (f64)0.001f))
        major_tick_decimals[index] = 4;
      else if (!(range >= (f64)0.01f))
        major_tick_decimals[index] = 3;
      else if (!(range >= (f64)0.1f))
        major_tick_decimals[index] = 2;
      else
        major_tick_decimals[index] = 1;
    }
  }
  ScRect rect;
  if (graph_area.left == kUiGraphUnsetCoordinate) {
    GetAutomaticGraphAreaRect(&rect);
    SetGraphArea(rect);
  }
  if (title_position.left == kUiGraphUnsetCoordinate) {
    GetAutomaticTitleRect(&rect);
    title_position = rect;
  }
  for (i32 index = 0; index < 2; ++index) {
    UiGraphAxis axis = (UiGraphAxis)index;
    f32 value;
    if (axis_label_position[index].left == kUiGraphUnsetCoordinate) {
      GetAutomaticAxisLabelRect(axis, &rect);
      axis_label_position[index] = rect;
    }
    if (major_tick_spacing[index] == 0.0f || major_tick_spacing[index] != major_tick_spacing[index]) {
      GetAutomaticAxisMajorTickSpacing(axis, &value);
      major_tick_spacing[index] = value;
    }
    if (minor_tick_spacing[index] == 0.0f || minor_tick_spacing[index] != minor_tick_spacing[index]) {
      GetAutomaticAxisMinorTickSpacing(axis, &value);
      minor_tick_spacing[index] = value;
    }
    CalculateAxisZeroPosition(axis);
  }
}

// Reserve room for automatic labels, major ticks and the title around the graph area.
void UiGraphWindow::GetAutomaticGraphAreaRect(ScRect *out) {
  i32 tick_length[2], tick_size[2], label_size[2];
  for (i32 index = 0; index < 2; ++index) {
    tick_length[index] = major_tick_usage[index] ? major_tick_length[index] : 0;
    if (!tick_text[index])
      tick_size[index] = 0;
    else if (!axis_tick_font[index])
      tick_size[index] = 16;
    else if (!index)
      tick_size[index] = axis_tick_font[index]->Height(0);
    else {
      char format[] = "%._f";
      ScString value;
      format[2] = (char)(major_tick_decimals[index] + '0');
      value.Sprintf(format, (f64)axis_max[index]);
      tick_size[index] = axis_tick_font[index]->Measure(value.begin, (u32)(value.end - value.begin));
    }
    if (axis_label[index].begin == axis_label[index].end)
      label_size[index] = 0;
    else if (axis_label_font[index])
      label_size[index] = axis_label_font[index]->Height(0);
    else
      label_size[index] = 16;
  }
  i32 bottom = (i32)((u32)GetH() - 16), right = (i32)((u32)GetW() - 16);
  out->bottom = bottom;
  out->left = 16;
  out->top = 16;
  out->right = right;
  if (title_position.left == kUiGraphUnsetCoordinate)
    out->top = (i32)((u32)title_height + 4);
  if (axis_label_position[0].left == kUiGraphUnsetCoordinate)
    out->bottom = (i32)((u32)GetH() - 4 - (u32)label_size[0] - (u32)tick_size[0] - (u32)tick_length[0]);
  if (axis_label_position[1].left == kUiGraphUnsetCoordinate)
    out->left = (i32)((u32)tick_size[1] + (u32)label_size[1] + (u32)tick_length[1] + 4);
}

// Position an automatic axis label beside its ticks and center it along the graph area.
void UiGraphWindow::GetAutomaticAxisLabelRect(UiGraphAxis axis, ScRect *out) {
  i32 index = (i32)axis, tick_length = major_tick_usage[index] ? major_tick_length[index] : 0;
  i32 tick_size, label_height, label_width;
  if (!tick_text[index])
    tick_size = 0;
  else if (!axis_tick_font[index])
    tick_size = 16;
  else if (!index)
    tick_size = axis_tick_font[index]->Height(0);
  else {
    char format[] = "%._f";
    ScString value;
    format[2] = (char)(major_tick_decimals[index] + '0');
    value.Sprintf(format, (f64)axis_max[index]);
    tick_size = axis_tick_font[index]->Measure(value.begin, (u32)(value.end - value.begin));
  }
  if (axis_label[index].begin == axis_label[index].end) {
    label_height = 0;
    label_width = 0;
  } else if (axis_label_font[index]) {
    label_height = axis_label_font[index]->Height(0);
    label_width = axis_label_font[index]->Measure(
        axis_label[index].begin, (u32)(axis_label[index].end - axis_label[index].begin));
  } else {
    label_height = 16;
    label_width = (i32)((u32)graph_area.right - (u32)graph_area.left);
  }
  if (index == 0) {
    i32 bottom = graph_area.bottom, left = graph_area.left, half = label_width / 2;
    i32 middle = (i32)((u32)graph_area.right - (u32)left) / 2;
    i32 top = (i32)((u32)bottom + (u32)tick_length + (u32)tick_size);
    i32 right = (i32)((u32)left + (u32)middle + (u32)half + 1);
    out->top = top;
    out->bottom = (i32)((u32)top + (u32)label_height);
    middle = (i32)((u32)graph_area.right - (u32)graph_area.left) / 2;
    out->left = (i32)((u32)left + (u32)middle - (u32)half);
    out->right = right;
  } else if (index == 1) {
    i32 top = graph_area.top, left = graph_area.left;
    i32 middle = (i32)((u32)graph_area.bottom - (u32)top) / 2;
    i32 rotation = (i32)axis_label_rotation[1];
    i32 half = (rotation == 0 || rotation == 2 ? label_height : label_width) / 2;
    i32 width = rotation == 0 || rotation == 2 ? label_width : label_height;
    i32 bottom = (i32)((u32)top + (u32)middle + (u32)half + 1);
    i32 right = (i32)((u32)left - (u32)tick_size - (u32)tick_length);
    out->right = right;
    middle = (i32)((u32)graph_area.bottom - (u32)graph_area.top) / 2;
    out->top = (i32)((u32)top + (u32)middle - (u32)half);
    out->left = (i32)((u32)right - (u32)width);
    out->bottom = bottom;
  }
}

static i32 UiGraphTruncate(f64 value) {
  assert(value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return (i32)(u32)(i64)value;
}

// Interpret an out-of-range axis word as the original x87 fallback float bits.
static f32 UiGraphAxisBits(UiGraphAxis axis) {
  i32 bits = (i32)axis;
  f32 result;
  memcpy(&result, &bits, sizeof(result));
  return result;
}

void UiGraphWindow::DrawGraphData(GxBuffer *) {
}

f32 UiGraphWindow::GetHighestAxisValue(UiGraphAxis) {
  return 90.0f;
}

f32 UiGraphWindow::GetLowestAxisValue(UiGraphAxis) {
  return 0.0f;
}

i32 UiGraphWindow::WindowPixelToGraphPixel(UiGraphAxis axis, i32 pixel) {
  if ((i32)axis == 0)
    return (i32)((u32)pixel - (u32)graph_area.left);
  if ((i32)axis == 1)
    return (i32)((u32)pixel - (u32)graph_area.top);
  return 0;
}

// Convert a graph-relative pixel to the corresponding window coordinate.
i32 UiGraphWindow::GraphPixelToWindowPixel(UiGraphAxis axis, i32 pixel) {
  if ((i32)axis == 0)
    return (i32)((u32)pixel + (u32)graph_area.left);
  if ((i32)axis == 1)
    return (i32)((u32)pixel + (u32)graph_area.top);
  return 0;
}

i32 UiGraphWindow::GetMaxTickLengthForAxis(UiGraphAxis axis) {
  i32 index = (i32)axis, result = 0;
  if (major_tick_usage[index] && major_tick_length[index] > result)
    result = major_tick_length[index];
  if (minor_tick_usage[index] && minor_tick_length[index] > result)
    result = minor_tick_length[index];
  return result;
}

void UiGraphWindow::GetAutomaticAxisMajorTickSpacing(UiGraphAxis axis, f32 *out) {
  i32 index = (i32)axis;
  *out = (f32)(((f64)axis_max[index] - (f64)axis_min[index]) * 0.125);
}

void UiGraphWindow::GetAutomaticAxisMinorTickSpacing(UiGraphAxis axis, f32 *out) {
  *out = major_tick_spacing[(i32)axis] * 0.5f;
}

// Divide the axis range into major ticks and optionally enforce a four-pixel minimum interval.
void UiGraphWindow::SetMajorTickDivisions(UiGraphAxis axis, u32 divisions, bool enabled) {
  i32 index = (i32)axis;
  f64 spacing = ((f64)axis_max[index] - (f64)axis_min[index]) / (f64)divisions;
  if (enabled) {
    i32 pixels = ValueAmountToPixelAmount(axis, (f32)spacing);
    if (pixels <= 4)
      pixels = 4;
    spacing = PixelAmountToValueAmount(axis, pixels);
  }
  major_tick_spacing[index] = (f32)spacing;
}

// Divide major ticks into minor ticks and optionally enforce a two-pixel minimum interval.
// The original also converts the previous spacing before checking the minimum-size option.
void UiGraphWindow::SetMinorTickDivisions(UiGraphAxis axis, u32 divisions, bool enabled) {
  i32 index = (i32)axis;
  f32 spacing = (f32)((f64)major_tick_spacing[index] / (f64)divisions);
  ValueAmountToPixelAmount(axis, minor_tick_spacing[index]);
  if (enabled && ValueAmountToPixelAmount(axis, spacing) < 2) {
    spacing = PixelAmountToValueAmount(axis, 2);
    f64 maximum = (f64)major_tick_spacing[index] * 0.5;
    if (maximum < spacing || maximum != maximum || spacing != spacing)
      minor_tick_usage[index] = 0;
  }
  minor_tick_spacing[index] = spacing;
}

// Position the automatic title rectangle four pixels inside the graph window.
void UiGraphWindow::GetAutomaticTitleRect(ScRect *out) {
  i32 bottom = (i32)((u32)title_height + 4), right = (i32)((u32)GetW() - 4);
  out->left = 4;
  out->top = 4;
  out->bottom = bottom;
  out->right = right;
}

// Expand a negative lowest data value by the original scale; otherwise use zero.
void UiGraphWindow::GetAutomaticAxisMinValue(UiGraphAxis axis, f32 *out) {
  f32 value = GetLowestAxisValue(axis);
  *out = value;
  *out = value < 0.0f || value != value ? (f32)((f64)value * (f64)1.1111111640930176f) : 0.0f;
}

// Expand a positive highest data value by the original scale; otherwise use zero.
void UiGraphWindow::GetAutomaticAxisMaxValue(UiGraphAxis axis, f32 *out) {
  f32 value = GetHighestAxisValue(axis);
  *out = value;
  *out = value > 0.0f ? (f32)((f64)value * (f64)1.1111111640930176f) : 0.0f;
}

// Place the selected axis's zero line at an edge or its calculated offset from that edge.
void UiGraphWindow::CalculateAxisZeroPosition(UiGraphAxis axis) {
  i32 index = (i32)axis, position;
  f32 low = axis_min[index], high = axis_max[index];
  if (low == 0.0f || low != low)
    position = index ? graph_area.bottom : graph_area.left;
  else if (high == 0.0f || high != high)
    position = index ? graph_area.top : graph_area.right;
  else {
    i32 delta = ValueAmountToPixelAmount(axis, high);
    position = index ? (i32)((u32)graph_area.top + (u32)delta) : (i32)((u32)graph_area.right - (u32)delta);
  }
  axis_zero_position[index] = position;
}

// Convert an axis value to a pixel relative to its zero line, optionally clipping to the graph area.
i32 UiGraphWindow::ValueToPixel(UiGraphAxis axis, f32 value, bool clamp) {
  i32 index = (i32)axis;
  assert((u32)index < 2);
  i32 low = index ? graph_area.top : graph_area.left, high = index ? graph_area.bottom : graph_area.right;
  i32 width = (i32)((u32)high - (u32)low);
  f64 scale = ((f64)axis_max[index] - (f64)axis_min[index]) / (f64)width;
  i32 delta = UiGraphTruncate((f64)value / scale);
  i32 result =
      index ? (i32)((u32)axis_zero_position[index] - (u32)delta) : (i32)((u32)axis_zero_position[index] + (u32)delta);
  if (clamp) {
    if (result < low)
      result = low;
    if (result > high)
      result = high;
  }
  return result;
}

// Convert a graph pixel back to an axis value, optionally clipping to the stored value bounds.
f32 UiGraphWindow::PixelToValue(UiGraphAxis axis, i32 pixel, bool clamp) {
  i32 index = (i32)axis;
  assert((u32)index < 2);
  i32 low = index ? graph_area.top : graph_area.left, high = index ? graph_area.bottom : graph_area.right;
  i32 width = (i32)((u32)high - (u32)low);
  i32 delta =
      index ? (i32)((u32)axis_zero_position[index] - (u32)pixel) : (i32)((u32)pixel - (u32)axis_zero_position[index]);
  f64 result = ((f64)axis_max[index] - (f64)axis_min[index]) / (f64)width * (f64)delta;
  if (clamp) {
    if (result < axis_min[index] || result != result || axis_min[index] != axis_min[index])
      result = axis_min[index];
    if (result > axis_max[index])
      result = axis_max[index];
  }
  return (f32)result;
}

// Convert an axis-value interval to an integer pixel interval using the graph's current scale.
i32 UiGraphWindow::ValueAmountToPixelAmount(UiGraphAxis axis, f32 value) {
  i32 index = (i32)axis;
  f64 scale;
  if ((u32)index < 2) {
    i32 low = index ? graph_area.top : graph_area.left, high = index ? graph_area.bottom : graph_area.right;
    i32 width = (i32)((u32)high - (u32)low);
    scale = ((f64)axis_max[index] - (f64)axis_min[index]) / (f64)width;
  } else {
    scale = UiGraphAxisBits(axis);
  }
  return UiGraphTruncate((f64)value / scale);
}

// Convert a pixel interval to an axis-value interval using the graph's current scale.
f32 UiGraphWindow::PixelAmountToValueAmount(UiGraphAxis axis, i32 pixels) {
  i32 index = (i32)axis;
  f64 scale;
  if ((u32)index < 2) {
    i32 low = index ? graph_area.top : graph_area.left, high = index ? graph_area.bottom : graph_area.right;
    i32 width = (i32)((u32)high - (u32)low);
    scale = ((f64)axis_max[index] - (f64)axis_min[index]) / (f64)width;
  } else {
    scale = UiGraphAxisBits(axis);
  }
  return (f32)(scale * (f64)pixels);
}

bool UiGraphWindow::OnMouseDownL(i32, i32, u32) {
  owner->SetCapture(this);
  return true;
}

// Release capture only when this graph still owns it and consume the left-button release.
bool UiGraphWindow::OnMouseUpL(i32, i32, u32) {
  if (owner->GetCapture() == this)
    owner->ReleaseCapture(this);
  return true;
}

// Evaluate the current point while the graph holds capture and consume the mouse movement.
bool UiGraphWindow::OnMouseMove(i32 x, i32 y, u32) {
  if (owner->GetCapture() == this)
    IsPointInWindow(x, y);
  return true;
}

void UiGraphWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  CompleteRedraw(draw_buffer);
}

// Allocate the requested hit-test storage; the installed method discards a prior allocation only when growing.
bool UiGraphWindow::AllocateHitTestData(u32 size) {
  if (size > hit_test_size)
    delete[] hit_test_result;
  hit_test_size = size;
  hit_test_result = new u8[size];
  return true;
}

UiGraphWindow::UiGraphWindow()
    : unknown168(1),
      graph_area{kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate},
      fonts_loaded(0), title_position{kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate,
                           kUiGraphUnsetCoordinate},
      title_font(nullptr), title_color(kUiGraphUseFontColor), title_height(32), unknown240(1),
      background_fill_color(kUiGraphDefaultFillColor), background_outline(1), background_outline_color(0),
      graph_fill(0), graph_fill_color(kUiGraphInitialFillColor), graph_outline(1), graph_outline_color(0),
      background_pattern(nullptr), graph_background_pattern(nullptr), axes_visible(0), major_tick_usage{1, 1},
      minor_tick_usage{0, 0}, major_tick_grid{1, 1}, minor_tick_grid{0, 0}, tick_text{0, 1}, axis_line_color(0),
      axis_min{0, 0}, axis_max{0, 0}, major_tick_spacing{0, 0}, minor_tick_spacing{0, 0},
      major_tick_decimals{0x81, 0x81}, major_tick_length{4, 4}, minor_tick_length{2, 2}, tick_color(0),
      unknown352{1, 2}, axis_zero_position{kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate},
      axis_label_position{
          {kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate},
          {kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate, kUiGraphUnsetCoordinate}},
      unknown440{1, 0}, axis_label_font{nullptr, nullptr}, axis_label_color{kUiGraphUseFontColor, kUiGraphUseFontColor},
      axis_tick_font{nullptr, nullptr}, axis_tick_color{kUiGraphUseFontColor, kUiGraphUseFontColor},
      axis_label_rotation{static_cast<UiGraphRotation>(0), static_cast<UiGraphRotation>(0)}, unknown488(0),
      hit_test_result(nullptr), hit_test_size(0), dynamic_head(new UiGraphNode) {
  dynamic_head->next = dynamic_head;
  dynamic_head->previous = dynamic_head;
  unknown488 = UiGetGraphics()->ConvertRGBValueToNative16Bit(255, 0, 255);
}

UiGraphWindow::~UiGraphWindow() {
  UiGraphWindow::Shutdown();
  delete dynamic_head;
}

// Delete owned drawable objects, then release fonts, patterns and hit-test storage before window shutdown.
bool UiGraphWindow::Shutdown() {
  for (UiGraphNode *node = dynamic_head->next; node != dynamic_head; node = node->next)
    delete node->object;
  UiGraphNode *node = dynamic_head->next;
  while (node != dynamic_head) {
    UiGraphNode *next = node->next;
    delete node;
    node = next;
  }
  dynamic_head->next = dynamic_head;
  dynamic_head->previous = dynamic_head;
  ScRelease(&title_font);
  ScRelease(&background_pattern);
  ScRelease(&graph_background_pattern);
  for (u32 i = 0; i < 2; ++i) {
    if (axis_label_font[i]) {
      axis_label_font[i]->Release();
      axis_label_font[i] = nullptr;
    }
    if (axis_tick_font[i]) {
      axis_tick_font[i]->Release();
      axis_tick_font[i] = nullptr;
    }
  }
  delete[] hit_test_result;
  hit_test_result = nullptr;
  fonts_loaded = 0;
  return UiBaseWindow::Shutdown();
}

bool UiGraphWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnGraphView, UiGraph);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 UiGraphWindow::AddRef() {
  return WnRootWidget::AddRef();
}

u32 UiGraphWindow::Release() {
  return WnRootWidget::Release();
}

// Initialize only once, enabling private buffering before the shared window initialization.
bool UiGraphWindow::Init() {
  if (GetInitFlag())
    return true;
  SetFlag(kWnFlagPrivateBuffer, true);
  return UiBaseWindow::Init();
}

bool UiGraphWindow::GZPaint() {
  CompleteRedraw(nullptr);
  return true;
}

// Retire the base drawable without additional state.
UiGraphDynamicObject::~UiGraphDynamicObject() = default;

void UiGraphDynamicObject::DrawSelf(UiGraph *, GxBuffer *) {
}

// Append a drawable to the graph's owned sequence, preserving insertion order.
void UiGraphWindow::AddDynamicGraphObject(UiGraphDynamicObject *object) {
  UiGraphNode *node = new UiGraphNode{dynamic_head, dynamic_head->previous, object};
  dynamic_head->previous->next = node;
  dynamic_head->previous = node;
}

// Remove every matching list entry without destroying the caller's drawable object.
void UiGraphWindow::RemoveDynamicGraphObject(UiGraphDynamicObject *object) {
  UiGraphNode *node = dynamic_head->next;
  while (node != dynamic_head) {
    UiGraphNode *next = node->next;
    if (node->object == object) {
      node->previous->next = next;
      next->previous = node->previous;
      delete node;
    }
    node = next;
  }
}

void UiGraphWindow::DrawDynamicObjects(GxBuffer *buffer) {
  for (UiGraphNode *node = dynamic_head->next; node != dynamic_head; node = node->next)
    node->object->DrawSelf(static_cast<UiGraph *>(this), buffer);
}

// Resolve the destination, prepare the axes and draw graph components in their installed order.
void UiGraphWindow::CompleteRedraw(GxBuffer *buffer) {
  if (!buffer)
    buffer = draw_buffer;
  if (!buffer)
    return;
  if (!fonts_loaded)
    LoadDefaultFonts();
  CommitDefaultValues();
  CalculateAxisZeroPosition(static_cast<UiGraphAxis>(0));
  CalculateAxisZeroPosition(static_cast<UiGraphAxis>(1));
  DrawBackground(buffer);
  DrawGraphAreaBackground(buffer);
  DrawAxes(buffer);
  DrawTicks(buffer, static_cast<UiGraphAxis>(7));
  DrawTickText(buffer, static_cast<UiGraphAxis>(7));
  DrawGraphData(buffer);
  DrawDynamicObjects(buffer);
  DrawTitles(buffer);
}

void UiGraphWindow::DrawBackground(GxBuffer *buffer) {
  ScRect destination{
      0, 0, (i32)((u32)draw_area.right - (u32)draw_area.left), (i32)((u32)draw_area.bottom - (u32)draw_area.top)};
  if (background_pattern) {
    ScRect source = destination;
    if (buffer->Lock(GxLockFlags::Blt)) {
      buffer->Blt(background_pattern, source, destination, static_cast<const ScRect *>(nullptr));
      buffer->Unlock(GxLockFlags::Blt);
    }
  } else {
    if (buffer->Lock(GxLockFlags::Fill)) {
      buffer->Fill(background_fill_color, &destination);
      buffer->Unlock(GxLockFlags::Fill);
    }
    if (background_outline && buffer->Lock(GxLockFlags::Rect)) {
      buffer->DrawRectangleOutline(destination, background_outline_color, 1);
      buffer->Unlock(GxLockFlags::Rect);
    }
  }
}

// Paint the plot area with its pattern or independently enabled fill and outline.
void UiGraphWindow::DrawGraphAreaBackground(GxBuffer *buffer) {
  if (graph_background_pattern) {
    ScRect source{
        0, 0, (i32)((u32)graph_area.right - (u32)graph_area.left), (i32)((u32)graph_area.bottom - (u32)graph_area.top)};
    if (buffer->Lock(GxLockFlags::Blt)) {
      buffer->Blt(graph_background_pattern, source, graph_area, static_cast<const ScRect *>(nullptr));
      buffer->Unlock(GxLockFlags::Blt);
    }
  } else {
    if (graph_fill && buffer->Lock(GxLockFlags::Fill)) {
      buffer->Fill(graph_fill_color, &graph_area);
      buffer->Unlock(GxLockFlags::Fill);
    }
    if (graph_outline && buffer->Lock(GxLockFlags::Rect)) {
      buffer->DrawRectangleOutline(graph_area, graph_outline_color, 1);
      buffer->Unlock(GxLockFlags::Rect);
    }
  }
}

void UiGraphWindow::DrawAxes(GxBuffer *buffer) {
  if (axes_visible && buffer->Lock(GxLockFlags::Line)) {
    buffer->DrawLine(graph_area.left, graph_area.top, graph_area.left, graph_area.bottom, (i32)axis_line_color, 1);
    buffer->DrawLine(graph_area.left, graph_area.bottom, graph_area.right, graph_area.bottom, (i32)axis_line_color, 1);
    buffer->Unlock(GxLockFlags::Line);
  }
}

// Draw the enabled major and minor ticks, with optional grid lines on each selected axis.
void UiGraphWindow::DrawTicks(GxBuffer *buffer, UiGraphAxis axes) {
  if ((!major_tick_usage[0] && !major_tick_usage[1]) || !buffer->Lock(GxLockFlags::Line))
    return;
  if (((i32)axes & 1) && major_tick_usage[0]) {
    i32 major_end = (i32)((u32)graph_area.bottom + (u32)major_tick_length[0]);
    i32 major_start = major_tick_grid[0] ? graph_area.top : graph_area.bottom;
    i32 minor_end = (i32)((u32)graph_area.bottom + (u32)minor_tick_length[0]);
    i32 minor_start = minor_tick_grid[0] ? graph_area.top : graph_area.bottom;
    for (f32 value = axis_min[0]; (f64)axis_max[0] + (f64)major_tick_spacing[0] * 0.25 >= value;
        value = (f32)((f64)value + major_tick_spacing[0])) {
      i32 x = ValueToPixel(static_cast<UiGraphAxis>(0), value, false);
      buffer->DrawLine(x, major_start, x, major_end, (i32)tick_color, 1);
      if (minor_tick_usage[0]) {
        for (f32 minor = (f32)((f64)value + minor_tick_spacing[0]); (f64)value + major_tick_spacing[0] > minor;
            minor = (f32)((f64)minor + minor_tick_spacing[0])) {
          x = ValueToPixel(static_cast<UiGraphAxis>(0), minor, false);
          buffer->DrawLine(x, minor_start, x, minor_end, (i32)tick_color, 1);
        }
      }
    }
  }
  if (((i32)axes & 2) && major_tick_usage[1]) {
    i32 major_start = (i32)((u32)graph_area.left - (u32)major_tick_length[1]);
    i32 major_end = major_tick_grid[1] ? graph_area.right : graph_area.left;
    i32 minor_start = (i32)((u32)graph_area.left - (u32)minor_tick_length[1]);
    i32 minor_end = minor_tick_grid[1] ? graph_area.right : graph_area.left;
    for (f32 value = axis_min[1]; (f64)axis_max[1] + (f64)major_tick_spacing[1] * 0.25 >= value;
        value = (f32)((f64)value + major_tick_spacing[1])) {
      i32 y = ValueToPixel(static_cast<UiGraphAxis>(1), value, false);
      buffer->DrawLine(major_start, y, major_end, y, (i32)tick_color, 1);
      if (minor_tick_usage[1]) {
        for (f32 minor = (f32)((f64)value + minor_tick_spacing[1]); (f64)value + major_tick_spacing[1] > minor;
            minor = (f32)((f64)minor + minor_tick_spacing[1])) {
          y = ValueToPixel(static_cast<UiGraphAxis>(1), minor, false);
          buffer->DrawLine(minor_start, y, minor_end, y, (i32)tick_color, 1);
        }
      }
    }
  }
  buffer->Unlock(GxLockFlags::Line);
}

// Format and draw enabled tick labels using the selected axis font and color.
void UiGraphWindow::DrawTickText(GxBuffer *buffer, UiGraphAxis axes) {
  ScString text;
  char format[] = "%._f";
  if (((i32)axes & 1) && tick_text[0] && axis_tick_font[0]) {
    i32 height = axis_tick_font[0]->Height(0);
    ScRect label_area;
    label_area.top = (i32)((u32)major_tick_length[0] + (u32)graph_area.bottom + 2);
    label_area.bottom = (i32)((u32)label_area.top + (u32)height + 2);
    for (f32 value = axis_min[0]; (f64)axis_max[0] + (f64)major_tick_spacing[0] * 0.25 >= value;
        value = (f32)((f64)value + major_tick_spacing[0])) {
      format[2] = (char)(major_tick_decimals[0] + '0');
      text.Sprintf(format, (f64)value);
      i32 width = axis_tick_font[0]->Measure(text.begin, (u32)(text.end - text.begin));
      i32 x = ValueToPixel(static_cast<UiGraphAxis>(0), value, false);
      u32 alignment = unknown352[0];
      if (alignment == 0) {
        label_area.left = x;
        label_area.right = (i32)((u32)x + (u32)width + 2);
      } else if (alignment == 1) {
        label_area.left = (i32)((u32)x - (u32)(width / 2) - 1);
        label_area.right = (i32)((u32)x + (u32)(width / 2) + 1);
      } else {
        label_area.left = (i32)((u32)x - (u32)width - 2);
        label_area.right = x;
      }
      if (axis_tick_color[0] == kUiGraphUseFontColor)
        axis_tick_font[0]->DrawTextPara(
            buffer, label_area, text.begin, (u32)(text.end - text.begin), static_cast<FtOutputStyle>(alignment));
      else
        axis_tick_font[0]->DrawTextPara(buffer, label_area, text.begin, (u32)(text.end - text.begin),
            axis_tick_color[0], static_cast<FtOutputStyle>(alignment));
    }
  }
  if (((i32)axes & 2) && tick_text[1] && axis_tick_font[1]) {
    i32 half_height = axis_tick_font[1]->Height(0) / 2;
    ScRect label_area;
    label_area.right = (i32)((u32)graph_area.left - (u32)major_tick_length[1] - 2);
    for (f32 value = axis_min[1]; (f64)axis_max[1] + (f64)major_tick_spacing[1] * 0.25 >= value;
        value = (f32)((f64)value + major_tick_spacing[1])) {
      format[2] = (char)(major_tick_decimals[1] + '0');
      text.Sprintf(format, (f64)value);
      i32 width = axis_tick_font[1]->Measure(text.begin, (u32)(text.end - text.begin));
      label_area.left = (i32)((u32)label_area.right - (u32)width);
      i32 y = ValueToPixel(static_cast<UiGraphAxis>(1), value, false);
      label_area.top = (i32)((u32)y - (u32)half_height);
      label_area.bottom = (i32)((u32)y + (u32)half_height + 2);
      if (axis_tick_color[1] == kUiGraphUseFontColor)
        axis_tick_font[1]->DrawTextPara(
            buffer, label_area, text.begin, (u32)(text.end - text.begin), static_cast<FtOutputStyle>(unknown352[1]));
      else
        axis_tick_font[1]->DrawTextPara(buffer, label_area, text.begin, (u32)(text.end - text.begin),
            axis_tick_color[1], static_cast<FtOutputStyle>(unknown352[1]));
    }
  }
}

// Draw axis labels and the title, rotating the vertical label through a temporary transparent buffer when requested.
void UiGraphWindow::DrawTitles(GxBuffer *buffer) {
  if (axis_label[0].begin != axis_label[0].end && axis_label_font[0]) {
    if (axis_label_color[0] == kUiGraphUseFontColor)
      axis_label_font[0]->DrawTextPara(buffer, axis_label_position[0], axis_label[0].begin,
          (u32)(axis_label[0].end - axis_label[0].begin), static_cast<FtOutputStyle>(unknown440[0]));
    else
      axis_label_font[0]->DrawTextPara(buffer, axis_label_position[0], axis_label[0].begin,
          (u32)(axis_label[0].end - axis_label[0].begin), axis_label_color[0],
          static_cast<FtOutputStyle>(unknown440[0]));
  }
  if (axis_label[1].begin != axis_label[1].end && axis_label_font[1]) {
    if ((i32)axis_label_rotation[1] != 0) {
      ScRect temporary_area{0, 0, 0, 0};
      GxBuffer *temporary = nullptr;
      UiGetGraphics()->CreateBuffer(&temporary);
      i32 rotation = (i32)axis_label_rotation[1];
      i32 width = (i32)((u32)axis_label_position[1].right - (u32)axis_label_position[1].left);
      i32 height = (i32)((u32)axis_label_position[1].bottom - (u32)axis_label_position[1].top);
      temporary_area.right = (i32)((u32)(rotation == 0 || rotation == 2 ? width : height) + 4);
      temporary_area.bottom = (i32)((u32)(rotation == 0 || rotation == 2 ? height : width) + 4);
      temporary->Init((u32)temporary_area.right, (u32)temporary_area.bottom, GxColorType(GxColorType::Color::RGB16),
          nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::VideoMemoryLowPriority, false);
      if (temporary->Lock(GxLockFlags::Fill)) {
        temporary->FillNoClip(unknown488, temporary_area);
        temporary->Unlock(GxLockFlags::Fill);
      }
      if (axis_label_font[1]) {
        if (axis_label_color[1] == kUiGraphUseFontColor)
          axis_label_font[1]->DrawTextPara(temporary, temporary_area, axis_label[1].begin,
              (u32)(axis_label[1].end - axis_label[1].begin), static_cast<FtOutputStyle>(unknown440[1]));
        else
          axis_label_font[1]->DrawTextPara(temporary, temporary_area, axis_label[1].begin,
              (u32)(axis_label[1].end - axis_label[1].begin), axis_label_color[1],
              static_cast<FtOutputStyle>(unknown440[1]));
      }
      temporary->SetTransparency(unknown488);
      if (buffer->Lock(GxLockFlags::BltRotate)) {
        u32 angle = (i32)axis_label_rotation[1] == 1 ? 90 : (i32)axis_label_rotation[1] == 2 ? 180 : 270;
        ScPoint destination{axis_label_position[1].left, axis_label_position[1].top};
        buffer->RotateBltSimple(temporary, temporary_area, destination, angle, nullptr);
        buffer->Unlock(GxLockFlags::BltRotate);
      }
      temporary->Release();
    } else if (axis_label_color[1] == kUiGraphUseFontColor) {
      axis_label_font[1]->DrawTextPara(buffer, axis_label_position[1], axis_label[1].begin,
          (u32)(axis_label[1].end - axis_label[1].begin), static_cast<FtOutputStyle>(unknown440[1]));
    } else {
      axis_label_font[1]->DrawTextPara(buffer, axis_label_position[1], axis_label[1].begin,
          (u32)(axis_label[1].end - axis_label[1].begin), axis_label_color[1],
          static_cast<FtOutputStyle>(unknown440[1]));
    }
  }
  if (title.begin != title.end && title_font) {
    if (title_color == kUiGraphUseFontColor)
      title_font->DrawTextPara(
          buffer, title_position, title.begin, (u32)(title.end - title.begin), static_cast<FtOutputStyle>(unknown240));
    else
      title_font->DrawTextPara(buffer, title_position, title.begin, (u32)(title.end - title.begin), title_color,
          static_cast<FtOutputStyle>(unknown240));
  }
}
