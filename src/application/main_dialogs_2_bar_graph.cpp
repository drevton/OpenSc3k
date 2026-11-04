#include "application/main_dialogs.h"
#include <float.h>
#include "audio/api.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "application/animation_player.h"
#include "application/api.h"
#include "application/application_core.h"
#include "application/window_controls.h"
#include "city/api.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"
#include "ui/graph_window.h"
#include "ui/ui_main_window.h"
#include "ui/ui_menu_config.h"
#include "ui/ui_menu_dialog.h"
#include "ui/ui_resource_handles.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/input.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

void WnGraph::DrawBackground(GxBuffer *buffer) {
  ScRect rectangle{0, 0, draw_area.right - draw_area.left, draw_area.bottom - draw_area.top};
  if (background_pattern) {
    ScRect source = rectangle;
    if (buffer->Lock(GxLockFlags::Blt)) {
      buffer->Blt(background_pattern, source, rectangle, static_cast<const ScRect *>(nullptr));
      buffer->Unlock(GxLockFlags::Blt);
    }
  } else {
    if (buffer->Lock(GxLockFlags::Fill)) {
      buffer->Fill(background_color, &rectangle);
      buffer->Unlock(GxLockFlags::Fill);
    }
    if (background_outline && buffer->Lock(GxLockFlags::Rect)) {
      buffer->DrawRectangleOutline(rectangle, background_outline_color, 1);
      buffer->Unlock(GxLockFlags::Rect);
    }
  }
}

void WnGraph::DrawGraphAreaBackground(GxBuffer *buffer) {
  if (graph_background_pattern) {
    ScRect source{0, 0, graph_area.right - graph_area.left, graph_area.bottom - graph_area.top};
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

void WnGraph::DrawAxes(GxBuffer *buffer) {
  if (draw_axes && buffer->Lock(GxLockFlags::Line)) {
    buffer->DrawLine(graph_area.left, graph_area.top, graph_area.left, graph_area.bottom, axis_line_color, 1);
    buffer->DrawLine(graph_area.left, graph_area.bottom, graph_area.right, graph_area.bottom, axis_line_color, 1);
    buffer->Unlock(GxLockFlags::Line);
  }
}

// Draw enabled major and minor ticks and grid lines for the requested axis mask under one line lock.
void WnGraph::DrawTicks(GxBuffer *buffer, WnGraphAxis axes) {
  i32 mask = static_cast<i32>(axes);
  if ((!major_tick_usage[0] && !major_tick_usage[1]) || !buffer->Lock(GxLockFlags::Line))
    return;
  if ((mask & 1) && major_tick_usage[0]) {
    i32 major_end = graph_area.bottom + major_tick_length[0];
    i32 major_start = major_tick_grid[0] ? graph_area.top : graph_area.bottom;
    i32 minor_end = graph_area.bottom + minor_tick_length[0];
    i32 minor_start = minor_tick_grid[0] ? graph_area.top : graph_area.bottom;
    for (f32 value = axis_min[0]; static_cast<f64>(major_tick_spacing[0]) * 0.25 + axis_max[0] >= value;
        value += major_tick_spacing[0]) {
      i32 pixel = ValueToPixel(static_cast<WnGraphAxis>(0), value, false);
      buffer->DrawLine(pixel, major_start, pixel, major_end, tick_color, 1);
      if (minor_tick_usage[0])
        for (f32 minor = value + minor_tick_spacing[0];
            static_cast<f64>(minor) < static_cast<f64>(value) + major_tick_spacing[0]; minor += minor_tick_spacing[0]) {
          pixel = ValueToPixel(static_cast<WnGraphAxis>(0), minor, false);
          buffer->DrawLine(pixel, minor_start, pixel, minor_end, tick_color, 1);
        }
    }
  }
  if ((mask & 2) && major_tick_usage[1]) {
    i32 major_start = graph_area.left - major_tick_length[1];
    i32 major_end = major_tick_grid[1] ? graph_area.right : graph_area.left;
    i32 minor_start = graph_area.left - minor_tick_length[1];
    i32 minor_end = minor_tick_grid[1] ? graph_area.right : graph_area.left;
    for (f32 value = axis_min[1]; static_cast<f64>(major_tick_spacing[1]) * 0.25 + axis_max[1] >= value;
        value += major_tick_spacing[1]) {
      i32 pixel = ValueToPixel(static_cast<WnGraphAxis>(1), value, false);
      buffer->DrawLine(major_start, pixel, major_end, pixel, tick_color, 1);
      if (minor_tick_usage[1])
        for (f32 minor = value + minor_tick_spacing[1];
            static_cast<f64>(minor) < static_cast<f64>(value) + major_tick_spacing[1]; minor += minor_tick_spacing[1]) {
          pixel = ValueToPixel(static_cast<WnGraphAxis>(1), minor, false);
          buffer->DrawLine(minor_start, pixel, minor_end, pixel, tick_color, 1);
        }
    }
  }
  buffer->Unlock(GxLockFlags::Line);
}

// Select the pixel representing zero from the axis bounds or its value-to-pixel scale.
void WnGraph::CalculateAxisZeroPosition(WnGraphAxis axis) {
  i32 index = static_cast<i32>(axis), position;
  if (axis_min[index] == 0.0f)
    position = index ? graph_area.bottom : graph_area.left;
  else if (axis_max[index] == 0.0f)
    position = index ? graph_area.top : graph_area.right;
  else {
    i32 offset = ValueAmountToPixelAmount(axis, axis_max[index]);
    position = index ? graph_area.top + offset : graph_area.right - offset;
  }
  axis_zero_position[index] = position;
}

void WnGraph::DrawGraphData(GxBuffer *) {
}

void WnGraph::DrawDynamicObjects(GxBuffer *buffer) {
  for (WnGraphObject *object : objects)
    object->DrawSelf(static_cast<WnGraphView *>(this), buffer);
}

f32 WnGraph::GetHighestAxisValue(WnGraphAxis) {
  return 90.0f;
}

f32 WnGraph::GetLowestAxisValue(WnGraphAxis) {
  return 0.0f;
}

// Subtract the graph area's horizontal or vertical origin from a window pixel coordinate.
i32 WnGraph::WindowPixelToGraphPixel(WnGraphAxis axis, i32 pixel) {
  if (static_cast<i32>(axis) == 0)
    return pixel - graph_area.left;
  if (static_cast<i32>(axis) == 1)
    return pixel - graph_area.top;
  return 0;
}

// Add the graph area's horizontal or vertical origin to a graph pixel coordinate.
i32 WnGraph::GraphPixelToWindowPixel(WnGraphAxis axis, i32 pixel) {
  if (static_cast<i32>(axis) == 0)
    return pixel + graph_area.left;
  if (static_cast<i32>(axis) == 1)
    return pixel + graph_area.top;
  return 0;
}

// Return the larger enabled tick length for the selected axis, with zero as the lower bound.
i32 WnGraph::GetMaxTickLengthForAxis(WnGraphAxis axis) {
  i32 index = static_cast<i32>(axis), length = 0;
  if (major_tick_usage[index] && major_tick_length[index] > 0)
    length = major_tick_length[index];
  if (minor_tick_usage[index] && minor_tick_length[index] > length)
    length = minor_tick_length[index];
  return length;
}

// Place an automatically positioned title four pixels inside the window and below its configured height.
void WnGraph::GetAutomaticTitleRect(ScRect *rectangle) {
  rectangle->left = 4;
  rectangle->top = 4;
  rectangle->right = GetW() - 4;
  rectangle->bottom = static_cast<i32>(unknown_236 + 4);
}

// Compute the graph area from window margins and the space occupied by automatic titles, labels, and ticks.
void WnGraph::GetAutomaticGraphAreaRect(ScRect *rectangle) {
  i32 tick_length[2], label_extent[2], text_extent[2];
  for (u32 axis = 0; axis < 2; ++axis) {
    tick_length[axis] = major_tick_usage[axis] ? major_tick_length[axis] : 0;
    if (tick_text_drawing[axis]) {
      if (axis_tick_fonts[axis]) {
        if (axis == 0)
          text_extent[axis] = axis_tick_fonts[axis]->Height(0);
        else {
          char format[] = "%._f";
          format[2] = static_cast<char>('0' + major_tick_decimals[axis]);
          ScString text;
          text.Sprintf(format, static_cast<f64>(axis_max[axis]));
          text_extent[axis] = axis_tick_fonts[axis]->Measure(text.Data(), text.Strlen());
        }
      } else
        text_extent[axis] = 16;
    } else
      text_extent[axis] = 0;
    if (axis_labels[axis].Strlen())
      label_extent[axis] = axis_label_fonts[axis] ? axis_label_fonts[axis]->Height(0) : 16;
    else
      label_extent[axis] = 0;
  }
  i32 height = GetH(), width = GetW();
  rectangle->left = 16;
  rectangle->top = 16;
  rectangle->right = width - 16;
  rectangle->bottom = height - 16;
  if (title_position.left == INT32_MAX)
    rectangle->top = static_cast<i32>(unknown_236 + 1);
  if (axis_label_positions[0].left == INT32_MAX) {
    height = GetH();
    rectangle->bottom = height - 4 - label_extent[0] - text_extent[0] - tick_length[0];
  }
  if (axis_label_positions[1].left == INT32_MAX)
    rectangle->left = text_extent[1] + label_extent[1] + tick_length[1] + 4;
}

// Place an axis label outside the graph while accounting for enabled ticks, numeric labels and label rotation.
void WnGraph::GetAutomaticAxisLabelRect(WnGraphAxis axis, ScRect *out) {
  i32 index = static_cast<i32>(axis);
  i32 ticks = major_tick_usage[index] ? major_tick_length[index] : 0;
  i32 tick_extent = 0, label_width = 0, label_height = 0;
  if (tick_text_drawing[index]) {
    FtFont *font = axis_tick_fonts[index];
    if (!font) {
      tick_extent = 16;
    } else if (index == 0) {
      tick_extent = font->Height(0);
    } else {
      char format[] = "%._f";
      format[2] = static_cast<char>('0' + major_tick_decimals[index]);
      ScString text;
      text.Sprintf(format, static_cast<f64>(axis_max[index]));
      tick_extent = font->Measure(text.Data(), text.Strlen());
    }
  }
  if (axis_labels[index].Strlen()) {
    FtFont *font = axis_label_fonts[index];
    if (font) {
      label_height = font->Height(0);
      label_width = font->Measure(axis_labels[index].Data(), axis_labels[index].Strlen());
    } else {
      label_height = 16;
      label_width = graph_area.right - graph_area.left;
    }
  }
  i32 width = graph_area.right - graph_area.left;
  i32 height = graph_area.bottom - graph_area.top;
  if (index == 0) {
    u32 center = static_cast<u32>(graph_area.left) + static_cast<u32>(width / 2);
    out->left = center - (label_width / 2);
    out->right = center + (label_width / 2) + 1;
    out->top = graph_area.bottom + ticks + tick_extent;
    out->bottom = out->top + label_height;
  } else if (index == 1) {
    u32 center = static_cast<u32>(graph_area.top) + static_cast<u32>(height / 2);
    u32 right = static_cast<u32>(graph_area.left) - static_cast<u32>(tick_extent) - static_cast<u32>(ticks);
    i32 rotation = static_cast<i32>(axis_label_rotations[1]);
    if (rotation == 0 || rotation == 2) {
      out->left = right - label_width;
      out->right = static_cast<i32>(right);
      out->top = center - (label_height / 2);
      out->bottom = center + (label_height / 2) + 1;
    } else {
      out->left = right - label_height;
      out->right = static_cast<i32>(right);
      out->top = center - (label_width / 2);
      out->bottom = center + (label_width / 2) + 1;
    }
  }
}

// Resolve automatic axis ranges, labels, areas, and tick spacing in the executable's original callback order.
void WnGraph::CommitDefaultValues() {
  for (i32 axis = 0; axis < 2; ++axis) {
    WnGraphAxis selected = static_cast<WnGraphAxis>(axis);
    bool both_automatic = true;
    f32 value;
    if (axis_min[axis] == FLT_MIN) {
      GetAutomaticAxisMinValue(selected, &value);
      axis_min[axis] = value;
    } else
      both_automatic = false;
    if (axis_max[axis] == FLT_MIN) {
      GetAutomaticAxisMaxValue(selected, &value);
      axis_max[axis] = value;
    } else
      both_automatic = false;
    if (both_automatic && axis_min[axis] == axis_max[axis])
      axis_max[axis] = axis_max[axis] == 0.0f ? 1.0f : static_cast<f32>(static_cast<f64>(axis_max[axis]) * 1.1111112f);
    if (major_tick_decimals[axis] == -127) {
      f64 range = static_cast<f64>(axis_max[axis]) - axis_min[axis];
      if (range >= 10.0)
        major_tick_decimals[axis] = 0;
      else if (range >= static_cast<f64>(0.1f))
        major_tick_decimals[axis] = 1;
      else if (range >= static_cast<f64>(0.01f))
        major_tick_decimals[axis] = 2;
      else if (range >= static_cast<f64>(0.001f))
        major_tick_decimals[axis] = 3;
      else if (range >= static_cast<f64>(0.0001f))
        major_tick_decimals[axis] = 4;
      else
        major_tick_decimals[axis] = 5;
    }
  }
  ScRect rectangle;
  if (graph_area.left == INT32_MAX) {
    GetAutomaticGraphAreaRect(&rectangle);
    SetGraphArea(rectangle);
  }
  if (title_position.left == INT32_MAX) {
    GetAutomaticTitleRect(&rectangle);
    title_position = rectangle;
  }
  for (i32 axis = 0; axis < 2; ++axis) {
    WnGraphAxis selected = static_cast<WnGraphAxis>(axis);
    f32 value;
    if (axis_label_positions[axis].left == INT32_MAX) {
      GetAutomaticAxisLabelRect(selected, &rectangle);
      axis_label_positions[axis] = rectangle;
    }
    if (major_tick_spacing[axis] == FLT_MIN) {
      GetAutomaticAxisMajorTickSpacing(selected, &value);
      major_tick_spacing[axis] = value;
    }
    if (minor_tick_spacing[axis] == FLT_MIN) {
      GetAutomaticAxisMinorTickSpacing(selected, &value);
      minor_tick_spacing[axis] = value;
    }
    CalculateAxisZeroPosition(selected);
  }
}

// Expand a negative data minimum by the original factor, or use zero when the minimum is nonnegative.
void WnGraph::GetAutomaticAxisMinValue(WnGraphAxis axis, f32 *out) {
  f32 value = GetLowestAxisValue(axis);
  *out = value;
  *out = value < 0.0f ? static_cast<f32>(static_cast<f64>(value) * 1.1111112f) : 0.0f;
}

// Expand a positive data maximum by the original factor, or use zero when the maximum is nonpositive.
void WnGraph::GetAutomaticAxisMaxValue(WnGraphAxis axis, f32 *out) {
  f32 value = GetHighestAxisValue(axis);
  *out = value;
  *out = value > 0.0f ? static_cast<f32>(static_cast<f64>(value) * 1.1111112f) : 0.0f;
}

void WnGraph::GetAutomaticAxisMajorTickSpacing(WnGraphAxis axis, f32 *out) {
  i32 index = static_cast<i32>(axis);
  *out = static_cast<f32>((static_cast<f64>(axis_max[index]) - axis_min[index]) * 0.125);
}

void WnGraph::GetAutomaticAxisMinorTickSpacing(WnGraphAxis axis, f32 *out) {
  *out = static_cast<f32>(static_cast<f64>(major_tick_spacing[static_cast<i32>(axis)]) * 0.5);
}

WnGraphText::WnGraphText()
    : rectangle{}, alignment(0), outline(false), outline_color(0), fill(false), fill_color(kUiGraphDefaultFillColor),
      font(nullptr), font_color(kUiGraphUseFontColor) {
}

// Release the retained font before retiring the text string and dynamic-object base.
WnGraphText::~WnGraphText() {
  if (font) {
    font->Release();
    font = nullptr;
  }
}

void WnGraphText::DrawSelf(WnGraphView *, GxBuffer *target) {
  if (fill && target->Lock(GxLockFlags::Fill)) {
    target->Fill(fill_color, &rectangle);
    target->Unlock(GxLockFlags::Fill);
  }
  if (outline && target->Lock(GxLockFlags::Rect)) {
    target->DrawRectangleOutline(rectangle, outline_color, 1);
    target->Unlock(GxLockFlags::Rect);
  }
  if (font) {
    u32 length = text.Strlen();
    if (font_color == kUiGraphUseFontColor)
      font->DrawTextPara(target, rectangle, text.Data(), length, static_cast<FtOutputStyle>(alignment));
    else
      font->DrawTextPara(target, rectangle, text.Data(), length, font_color, static_cast<FtOutputStyle>(alignment));
  }
}

// Retain the replacement text font before releasing the old font, then store its requested color.
void WnGraphText::SetFont(FtFont *value, u32 color) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  font_color = color;
}

WnGraphPicture::WnGraphPicture() : buffer(nullptr), x(0), y(0), relative_to_graph(false) {
}

// Release the picture's retained image before retiring the dynamic-object base.
WnGraphPicture::~WnGraphPicture() {
  if (buffer) {
    buffer->Release();
    buffer = nullptr;
  }
}

bool WnGraphPicture::Init() {
  return true;
}

// Retain the replacement image before releasing the previously held picture buffer.
void WnGraphPicture::SetBuffer(GxBuffer *value) {
  if (value)
    value->AddRef();
  if (buffer)
    buffer->Release();
  buffer = value;
}

// Blit the picture at its stored location, optionally offset by the graph area's origin.
void WnGraphPicture::DrawSelf(WnGraphView *graph, GxBuffer *target) {
  ScRect source = *buffer->GetBufferArea();
  ScRect destination{x, y, x + source.right - source.left, y + source.bottom - source.top};
  if (relative_to_graph && graph) {
    ScRect graph_rectangle;
    graph->GetGraphArea(&graph_rectangle);
    destination.left = destination.left + graph_rectangle.left;
    destination.right = destination.right + graph_rectangle.left;
    destination.top = destination.top + graph_rectangle.top;
    destination.bottom = destination.bottom + graph_rectangle.top;
  }
  if (target->Lock(GxLockFlags::Blt)) {
    target->Blt(buffer, source, destination, static_cast<const ScRect *>(nullptr));
    target->Unlock(GxLockFlags::Blt);
  }
}

WnGraphArrow::WnGraphArrow()
    : x1(0), y1(0), x2(0), y2(0), color(0), thickness(1), first_head(true), second_head(false), head_length(8),
      head_angle(0x1.921ff2p-1f) {
}

WnGraphArrow::~WnGraphArrow() {
}

// Draw the arrow's shaft and perform the original arrowhead calculations before releasing the line lock.
void WnGraphArrow::DrawSelf(WnGraphView *, GxBuffer *target) {
  if (!target->Lock(GxLockFlags::Line))
    return;
  target->DrawLine(x1, y1, x2, y2, color, thickness);
  if (first_head || second_head) {
    if (x1 == x2 && y1 == y2)
      x2 = static_cast<i32>(static_cast<u32>(x2) + 1);
    u32 dx = static_cast<u32>(x1) - static_cast<u32>(x2);
    u32 dy = static_cast<u32>(y1) - static_cast<u32>(y2);
    (void)std::sqrt(static_cast<f64>(static_cast<i32>(dx * dx + dy * dy)));
    (void)std::tan(static_cast<f64>(head_angle));
  }
  target->Unlock(GxLockFlags::Line);
}

WnLineSeriesInfo::WnLineSeriesInfo()
    : symbol_enabled(1), symbol_character('A'), symbol_size(12), line_enabled(1), color(0), unknown_16(1) {
}

// The executable constructs eight default records before graph methods use this shared original vector.
std::vector<WnLineSeriesInfo> WnLineGraph::default_series_info(8);

WnLineGraph::WnLineGraph()
    : orientation(static_cast<WnGraphOrientation>(0)), symbol_font(nullptr), symbol_color(kUiGraphUseFontColor) {
  major_tick_usage[0] = false;
  major_tick_usage[1] = true;
  major_tick_grid[0] = false;
  major_tick_grid[1] = true;
}

WnLineGraph::~WnLineGraph() {
  WnLineGraph::Shutdown();
}

bool WnLineGraph::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnLineGraphView, WnLineGraphView);
  return WnGraph::QueryInterface(iid, out);
}

u32 WnLineGraph::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnLineGraph::Release() {
  return WnRootWidget::Release();
}

bool WnLineGraph::Init() {
  return WnGraph::Init();
}

bool WnLineGraph::Shutdown() {
  if (symbol_font) {
    symbol_font->Release();
    symbol_font = nullptr;
  }
  return WnGraph::Shutdown();
}

void WnLineGraph::SetGraphOrientation(WnGraphOrientation value) {
  orientation = value;
}

// Retain the replacement symbol font before releasing the old font and saving its color.
void WnLineGraph::SetSymbolFont(FtFont *font, u32 color) {
  if (font)
    font->AddRef();
  if (symbol_font)
    symbol_font->Release();
  symbol_font = font;
  symbol_color = color;
}

// Resize both series vectors and replace their settings with the corresponding shared default records.
void WnLineGraph::SetSeriesCount(u32 count) {
  series.resize(count);
  series_info.resize(count);
  for (u32 i = 0; i < count; ++i)
    series_info[i] = default_series_info[i];
}

void WnLineGraph::SetDefaultSeriesInfo() {
  GxGraphics *graphics = ApGetGraphics();
  static const u8 colors[8][3] = {{0, 128, 128}, {128, 0, 128}, {255, 128, 0}, {0, 0, 128}, {128, 0, 0},
      {128, 128, 128}, {255, 255, 128}, {0, 0, 0}};
  static const u8 characters[8] = {108, 110, 169, 117, 162, 168, 178, 165};
  for (u32 i = 0; i < 8; ++i)
    default_series_info[i].color = graphics->ConvertRGBValueToNative16Bit(colors[i][0], colors[i][1], colors[i][2]);
  for (u32 i = 0; i < 8; ++i)
    default_series_info[i].symbol_character = characters[i];
}

// Insert an empty data series and its default settings, or append both for the original sentinel.
void WnLineGraph::InsertSeries(u32 index) {
  if (index == kUiAppendGraphSeries) {
    series.emplace_back();
    series_info.emplace_back();
  } else if (index < series.size()) {
    series.insert(series.begin() + index, std::vector<f32>());
    series_info.insert(series_info.begin() + index, WnLineSeriesInfo());
  }
}

// Remove a data series and its matching settings, using the last entries for the sentinel.
void WnLineGraph::RemoveSeries(u32 index) {
  if (index == kUiLastGraphSeries) {
    series.pop_back();
    series_info.pop_back();
  } else if (index < series.size()) {
    series.erase(series.begin() + index);
    series_info.erase(series_info.begin() + index);
  }
}

// Remove the initially present series one at a time through the original virtual removal method.
void WnLineGraph::RemoveAllSeries() {
  u32 count = static_cast<u32>(series.size());
  while (count--)
    RemoveSeries(kUiLastGraphSeries);
}

void WnLineGraph::SetSymbolUse(WnGraphUsage symbol, WnGraphUsage line, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i) {
    if (index == kUiAllGraphSeries || index == i) {
      series_info[i].symbol_enabled = static_cast<i32>(symbol) != 0;
      series_info[i].line_enabled = static_cast<i32>(line) != 0;
    }
  }
}

void WnLineGraph::SetSymbolCharacter(char character, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].symbol_character = static_cast<u8>(character);
}

void WnLineGraph::SetSymbolSize(u32 size, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].symbol_size = size;
}

void WnLineGraph::SetSeriesColor(u32 color, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].color = color;
}

// Copy a series name into the selected record or every record for the all-series sentinel.
void WnLineGraph::SetSeriesName(const ScString &name, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].name = name;
}

void WnLineGraph::SetSeriesInfo(WnLineSeriesInfo *info, u32 index) {
  if (index < series_info.size())
    series_info[index] = *info;
}

// Append series settings for the sentinel or insert them at an existing position.
void WnLineGraph::InsertSeriesInfo(WnLineSeriesInfo *info, u32 index) {
  if (index == kUiAppendGraphSeries)
    series_info.push_back(*info);
  if (index < series_info.size())
    series_info.insert(series_info.begin() + index, *info);
}

// Remove the final series settings for the sentinel or erase an existing indexed record.
void WnLineGraph::RemoveSeriesInfo(u32 index) {
  if (index == kUiLastGraphSeries)
    series_info.pop_back();
  else if (index < series_info.size())
    series_info.erase(series_info.begin() + index);
}

// Copy a category name at its index, expanding the name vector or appending for the sentinel.
void WnLineGraph::InsertCategoryName(const ScString &name, u32 index) {
  if (index == kUiAppendGraphCategory)
    index = static_cast<u32>(categories.size());
  if (index >= categories.size())
    categories.resize(static_cast<usize>(index) + 1);
  categories[index] = name;
}

// Clear an existing category name while retaining its vector position.
void WnLineGraph::RemoveCategoryName(u32 index) {
  if (index == kUiLastGraphCategory)
    index = static_cast<u32>(categories.size()) - 1;
  if (index < categories.size())
    categories[index].Erase(0, categories[index].ScString::Strlen());
}

void WnLineGraph::InsertData(f32 value, u32 series_index, u32 position) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size()) {
    auto &values = series[series_index];
    if (position == kUiAppendGraphValue)
      values.push_back(value);
    else
      values.insert(values.begin() + position, value);
  }
}

// Erase one data value from the selected series, accepting the single-series sentinel.
void WnLineGraph::RemoveData(u32 position, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    series[series_index].erase(series[series_index].begin() + position);
}

void WnLineGraph::SetData(f32 value, u32 position, u32 series_index) {
  if (series_index < series.size()) {
    if (position == kUiAppendGraphValue)
      series[series_index].push_back(value);
    else
      series[series_index][position] = value;
  }
}

// Replace the selected series with the supplied vector, accepting the single-series sentinel.
void WnLineGraph::SetData(std::vector<f32> *values, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    series[series_index] = *values;
}

// Append the supplied values in order to the selected series, accepting the single-series sentinel.
void WnLineGraph::AppendData(std::vector<f32> *values, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    for (usize i = 0; i < values->size(); ++i)
      series[series_index].push_back((*values)[i]);
}

// Find the largest data value across every series, retaining the original empty-data sentinel.
f32 WnLineGraph::GetHighestAxisValue(WnGraphAxis) {
  f32 result = -FLT_MAX;
  for (const auto &values : series)
    for (f32 value : values)
      if (value > result)
        result = value;
  return result;
}

// Find the smallest data value across every series, retaining the original empty-data sentinel.
f32 WnLineGraph::GetLowestAxisValue(WnGraphAxis) {
  f32 result = FLT_MAX;
  for (const auto &values : series)
    for (f32 value : values)
      if (value < result)
        result = value;
  return result;
}

void WnLineGraph::CompleteRedraw(GxBuffer *buffer) {
  WnGraph::CompleteRedraw(buffer);
}

void WnLineGraph::LoadDefaultFonts() {
  if (!symbol_font)
    SetSymbolFont(1, 12, 0, kUiGraphUseFontColor);
  WnGraph::LoadDefaultFonts();
}

// Center the selected series symbol at the point, temporarily applying its explicit color when present.
void WnLineGraph::DrawSymbol(GxBuffer *buffer, ScPoint *point, u32 index) {
  if (index >= series_info.size() || !symbol_font)
    return;
  u32 character = static_cast<u32>(static_cast<i32>(static_cast<i8>(series_info[index].symbol_character)));
  u32 width = static_cast<u32>(symbol_font->Width(character));
  u32 height = static_cast<u32>(symbol_font->Height(0));
  i32 x = static_cast<i32>(static_cast<u32>(point->x) - (width >> 1));
  i32 y = static_cast<i32>(static_cast<u32>(point->y) - (height >> 1));
  u32 color = series_info[index].color;
  if (color == kUiGraphUseFontColor)
    symbol_font->DrawChar(buffer, x, y, character);
  else {
    u32 previous = symbol_font->Color();
    symbol_font->SetColor(color);
    symbol_font->DrawChar(buffer, x, y, character);
    symbol_font->SetColor(previous);
  }
}

// Draw the selected series line using the supplied endpoints, color, and stored thickness.
void WnLineGraph::DrawLine(GxBuffer *buffer, ScRect *line, u32 index) {
  if (index < series_info.size())
    buffer->DrawLine(
        line->left, line->top, line->right, line->bottom, series_info[index].color, series_info[index].unknown_16);
}

WnScatterGraph::WnScatterGraph() : shared_axis_scale(1), symbol_font(nullptr), symbol_color(kUiGraphUseFontColor) {
  tick_text_drawing[0] = true;
  tick_text_drawing[1] = true;
}

WnScatterGraph::~WnScatterGraph() {
  WnScatterGraph::Shutdown();
}

bool WnScatterGraph::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnScatterGraphView, WnScatterGraphView);
  return WnGraph::QueryInterface(iid, out);
}

u32 WnScatterGraph::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnScatterGraph::Release() {
  return WnRootWidget::Release();
}

bool WnScatterGraph::Init() {
  return WnGraph::Init();
}

// Release the retained symbol font before shutting down the inherited graph resources.
bool WnScatterGraph::Shutdown() {
  if (symbol_font) {
    symbol_font->Release();
    symbol_font = nullptr;
  }
  return WnGraph::Shutdown();
}

// Retain a replacement symbol font before releasing the old font and storing the color.
void WnScatterGraph::SetSymbolFont(FtFont *font, u32 color) {
  if (font)
    font->AddRef();
  if (symbol_font)
    symbol_font->Release();
  symbol_font = font;
  symbol_color = color;
}

// Insert an empty point series and its default symbol settings, or append both for the sentinel.
void WnScatterGraph::InsertSeries(u32 index) {
  if (index == kUiAppendGraphSeries) {
    series.emplace_back();
    series_info.emplace_back();
  } else if (index < series.size()) {
    series.insert(series.begin() + index, std::vector<ScVec2F>());
    series_info.insert(series_info.begin() + index, WnScatterSeriesInfo());
  }
}

// Remove the indexed point series and settings, or remove their final entries for the sentinel.
void WnScatterGraph::RemoveSeries(u32 index) {
  if (index == kUiLastGraphSeries) {
    series.pop_back();
    series_info.pop_back();
  } else if (index < series.size()) {
    series.erase(series.begin() + index);
    series_info.erase(series_info.begin() + index);
  }
}

void WnScatterGraph::SetSymbolCharacter(char character, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].symbol_character = static_cast<u8>(character);
}

void WnScatterGraph::SetSymbolSize(u32 size, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].symbol_size = size;
}

void WnScatterGraph::SetSeriesColor(u32 color, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].color = color;
}

// Copy the supplied name into one scatter-series record or every record.
void WnScatterGraph::SetSeriesName(const ScString &name, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].name = name;
}

void WnScatterGraph::SetSeriesInfo(WnScatterSeriesInfo *info, u32 index) {
  if (index < series_info.size())
    series_info[index] = *info;
}

// Append scatter-series settings for the sentinel or insert them at an existing position.
void WnScatterGraph::InsertSeriesInfo(WnScatterSeriesInfo *info, u32 index) {
  if (index == kUiAppendGraphSeries)
    series_info.push_back(*info);
  if (index < series_info.size())
    series_info.insert(series_info.begin() + index, *info);
}

void WnScatterGraph::RemoveSeriesInfo(u32 index) {
  if (index == kUiLastGraphSeries)
    series_info.pop_back();
  else if (index < series_info.size())
    series_info.erase(series_info.begin() + index);
}

// The executable constructs eight default scatter-series records during static initialization.
std::vector<WnScatterSeriesInfo> WnScatterGraph::default_series_info(8);

// Resize both scatter-series vectors and copy their shared default symbol settings.
void WnScatterGraph::SetSeriesCount(u32 count) {
  series.resize(count);
  series_info.resize(count);
  for (u32 i = 0; i < count; ++i)
    series_info[i] = default_series_info[i];
}

void WnScatterGraph::SetDefaultSeriesInfo() {
  GxGraphics *graphics = ApGetGraphics();
  static const u8 colors[8][3] = {{0, 128, 128}, {128, 0, 128}, {255, 128, 0}, {0, 0, 128}, {128, 0, 0},
      {128, 128, 128}, {255, 255, 128}, {0, 0, 0}};
  static const u8 characters[8] = {108, 110, 169, 117, 162, 168, 178, 165};
  for (u32 i = 0; i < 8; ++i)
    default_series_info[i].color = graphics->ConvertRGBValueToNative16Bit(colors[i][0], colors[i][1], colors[i][2]);
  for (u32 i = 0; i < 8; ++i)
    default_series_info[i].symbol_character = characters[i];
}

// Insert or append a point in the selected series, accepting the single-series sentinel.
void WnScatterGraph::InsertData(const ScVec2F &point, u32 series_index, u32 position) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size()) {
    auto &values = series[series_index];
    if (position == kUiAppendGraphValue)
      values.push_back(point);
    else
      values.insert(values.begin() + position, point);
  }
}

void WnScatterGraph::InsertData(f32 x, f32 y, u32 series_index, u32 position) {
  ScVec2F point{x, y};
  WnScatterGraph::InsertData(point, series_index, position);
}

// Remove a point from the selected series, accepting the single-series sentinel.
void WnScatterGraph::RemoveData(u32 position, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    series[series_index].erase(series[series_index].begin() + position);
}

// Replace an indexed point or append it when the position is the original sentinel.
void WnScatterGraph::SetData(const ScVec2F &point, u32 position, u32 series_index) {
  if (series_index < series.size()) {
    if (position == kUiAppendGraphValue)
      series[series_index].push_back(point);
    else
      series[series_index][position] = point;
  }
}

void WnScatterGraph::SetData(f32 x, f32 y, u32 position, u32 series_index) {
  ScVec2F point{x, y};
  WnScatterGraph::SetData(point, position, series_index);
}

// Replace the selected point vector, accepting the single-series sentinel.
void WnScatterGraph::SetData(const std::vector<ScVec2F> &values, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    series[series_index] = values;
}

// Append the initially supplied number of points to the selected series in order.
void WnScatterGraph::AppendData(const std::vector<ScVec2F> &values, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size()) {
    usize count = values.size();
    for (usize i = 0; i < count; ++i)
      series[series_index].push_back(values[i]);
  }
}

// Find the highest X or Y point coordinate across every series, retaining the original empty sentinel.
f32 WnScatterGraph::GetHighestAxisValue(WnGraphAxis axis) {
  f32 result = -FLT_MAX;
  i32 index = static_cast<i32>(axis);
  if (index == 0 || index == 1)
    for (const auto &values : series)
      for (const ScVec2F &point : values) {
        f32 value = index == 0 ? point.x : point.y;
        if (value > result)
          result = value;
      }
  return result;
}

// Find the lowest X or Y point coordinate across every series, retaining the original empty sentinel.
f32 WnScatterGraph::GetLowestAxisValue(WnGraphAxis axis) {
  f32 result = FLT_MAX;
  i32 index = static_cast<i32>(axis);
  if (index == 0 || index == 1)
    for (const auto &values : series)
      for (const ScVec2F &point : values) {
        f32 value = index == 0 ? point.x : point.y;
        if (value < result)
          result = value;
      }
  return result;
}

// Use a shared negative minimum across both axes when shared scaling is enabled.
void WnScatterGraph::GetAutomaticAxisMinValue(WnGraphAxis axis, f32 *out) {
  if (!shared_axis_scale) {
    WnGraph::GetAutomaticAxisMinValue(axis, out);
    return;
  }
  f32 first = GetLowestAxisValue(static_cast<WnGraphAxis>(0));
  f32 second = GetLowestAxisValue(static_cast<WnGraphAxis>(1));
  if (first >= 0.0f)
    first = 0.0f;
  if (second >= 0.0f)
    second = 0.0f;
  *out = first < second ? first : second;
  *out = static_cast<f32>(static_cast<f64>(*out) * 1.1111112f);
}

// Use a shared positive maximum across both axes when shared scaling is enabled.
void WnScatterGraph::GetAutomaticAxisMaxValue(WnGraphAxis axis, f32 *out) {
  if (!shared_axis_scale) {
    WnGraph::GetAutomaticAxisMaxValue(axis, out);
    return;
  }
  f32 first = GetHighestAxisValue(static_cast<WnGraphAxis>(0));
  f32 second = GetHighestAxisValue(static_cast<WnGraphAxis>(1));
  if (first <= 0.0f)
    first = 0.0f;
  if (second <= 0.0f)
    second = 0.0f;
  *out = first > second ? first : second;
  *out = static_cast<f32>(static_cast<f64>(*out) * 1.1111112f);
}

// Obtain the base automatic graph area and reduce its longer side to form a square.
void WnScatterGraph::GetAutomaticGraphAreaRect(ScRect *rectangle) {
  ScRect value;
  WnGraph::GetAutomaticGraphAreaRect(&value);
  i32 height = value.bottom - value.top;
  i32 width = value.right - value.left;
  if (height < width)
    value.right = value.left + height;
  else if (height > width)
    value.bottom = value.top + width;
  *rectangle = value;
}

void WnScatterGraph::CompleteRedraw(GxBuffer *buffer) {
  WnGraph::CompleteRedraw(buffer);
}

void WnScatterGraph::LoadDefaultFonts() {
  if (!symbol_font)
    SetSymbolFont(4, 12, 0, kUiGraphUseFontColor);
  WnGraph::LoadDefaultFonts();
}

// Draw every scatter point using the first series' point count and the retained symbol font's drawing session.
// Each subsequent series supplies the corresponding points without a separate original length check.
void WnScatterGraph::DrawGraphData(GxBuffer *target) {
  if (series.empty() || series[0].empty())
    return;
  u32 count = static_cast<u32>(series[0].size()), series_count = static_cast<u32>(series.size());
  if (!symbol_font->BeginDraw(target))
    return;
  for (u32 index = 0; index < series_count; ++index) {
    for (u32 point_index = 0; point_index < count; ++point_index) {
      assert(point_index < series[index].size());
      const ScVec2F &value = series[index][point_index];
      f32 x = value.x, y = value.y;
      ScPoint point{
          ValueToPixel(static_cast<WnGraphAxis>(0), x, true), ValueToPixel(static_cast<WnGraphAxis>(1), y, true)};
      DrawSymbol(target, &point, index);
    }
  }
  symbol_font->EndDraw(target);
}

// Center the selected scatter-series symbol, temporarily applying its explicit color when present.
void WnScatterGraph::DrawSymbol(GxBuffer *buffer, ScPoint *point, u32 index) {
  if (index >= series_info.size() || !symbol_font)
    return;
  u32 character = static_cast<u32>(static_cast<i32>(static_cast<i8>(series_info[index].symbol_character)));
  u32 width = static_cast<u32>(symbol_font->Width(character));
  u32 height = static_cast<u32>(symbol_font->Height(0));
  i32 x = static_cast<i32>(static_cast<u32>(point->x) - (width >> 1));
  i32 y = static_cast<i32>(static_cast<u32>(point->y) - (height >> 1));
  u32 color = series_info[index].color;
  if (color == kUiGraphUseFontColor)
    symbol_font->DrawChar(buffer, x, y, character);
  else {
    u32 previous = symbol_font->Color();
    symbol_font->SetColor(color);
    symbol_font->DrawChar(buffer, x, y, character);
    symbol_font->SetColor(previous);
  }
}

// Draw enabled line segments under the buffer lock, then draw enabled symbols in the font's drawing session.
// Series settings and the retained symbol-loop count refer to initialized entries, as in the original direct reads.
void WnLineGraph::DrawGraphData(GxBuffer *target) {
  if (series.empty() || series[0].empty())
    return;
  u32 series_count = static_cast<u32>(series.size()), point_count = static_cast<u32>(series[0].size());
  bool horizontal = static_cast<i32>(orientation) == 0;
  WnGraphAxis value_axis = static_cast<WnGraphAxis>(horizontal);
  if (target->Lock(GxLockFlags::Line)) {
    for (u32 i = 0; i < series_count; ++i) {
      assert(i < series_info.size());
      if (series_info[i].line_enabled) {
        point_count = static_cast<u32>(series[i].size());
        for (u32 j = 0; j + 1 < point_count; ++j) {
          f32 first = series[i][j], second = series[i][j + 1];
          i32 first_category = GetCenterOfDataPoint(j);
          i32 first_value = ValueToPixel(value_axis, first, true);
          i32 second_category = GetCenterOfDataPoint(j + 1);
          i32 second_value = ValueToPixel(value_axis, second, true);
          ScRect line = horizontal ? ScRect{first_category, first_value, second_category, second_value}
                                   : ScRect{second_value, first_category, first_value, second_category};
          DrawLine(target, &line, i);
        }
      }
    }
    target->Unlock(GxLockFlags::Line);
  }
  if (symbol_font && symbol_font->BeginDraw(target)) {
    for (u32 i = 0; i < series_count; ++i) {
      assert(i < series_info.size());
      if (series_info[i].symbol_enabled) {
        for (u32 j = 0; j < point_count; ++j) {
          assert(j < series[i].size());
          f32 value = series[i][j];
          i32 category = GetCenterOfDataPoint(j), position = ValueToPixel(value_axis, value, true);
          ScPoint point = horizontal ? ScPoint{category, position} : ScPoint{position, category};
          DrawSymbol(target, &point, i);
        }
      }
    }
    symbol_font->EndDraw(target);
  }
}

// Draw category ticks on the borrowed target, retaining the original coordinate rectangles across callbacks.
// A vertical major tick requires the horizontal center that the original earlier pass produced.
void WnLineGraph::DrawTicks(GxBuffer *target, WnGraphAxis axes) {
  i32 mask = static_cast<i32>(axes), horizontal_center = 0;
  u32 horizontal_index = 0;
  ScRect major{0, 0, 0, 0}, minor{0, 0, 0, 0};
  if (static_cast<i32>(orientation) == 0 && (mask & 2))
    WnGraph::DrawTicks(target, static_cast<WnGraphAxis>(2));
  if (static_cast<i32>(orientation) == 1 && (mask & 1))
    WnGraph::DrawTicks(target, static_cast<WnGraphAxis>(1));
  if ((!major_tick_usage[0] && !minor_tick_usage[0] && !major_tick_usage[1] && !minor_tick_usage[1]) ||
      series.empty() || !target->Lock(GxLockFlags::Line))
    return;
  if (static_cast<i32>(orientation) == 0 && (mask & 1) && (major_tick_usage[0] || minor_tick_usage[0])) {
    if (major_tick_usage[0]) {
      major.bottom = graph_area.bottom + major_tick_length[0];
      major.top = major_tick_grid[0] ? graph_area.top : graph_area.bottom;
    }
    if (minor_tick_usage[0]) {
      minor.bottom = graph_area.bottom + minor_tick_length[0];
      minor.top = minor_tick_grid[0] ? graph_area.top : graph_area.bottom;
    }
    u32 count = static_cast<u32>(series[0].size());
    for (; horizontal_index < count; ++horizontal_index) {
      horizontal_center = GetCenterOfDataPoint(horizontal_index);
      if (minor_tick_usage[0]) {
        i32 pixel = horizontal_index == 0 ? graph_area.left :
            horizontal_index == count - 1 ? graph_area.right : horizontal_center;
        minor.left = minor.right = pixel;
        target->DrawLine(minor.left, minor.top, minor.right, minor.bottom, tick_color, 1);
      }
      if (major_tick_usage[0]) {
        i32 next = GetCenterOfDataPoint(horizontal_index + 1);
        major.left = major.right = i32(u32(horizontal_center) + u32(next)) / 2;
        target->DrawLine(major.left, major.top, major.right, major.bottom, tick_color, 1);
      }
    }
  }
  if (static_cast<i32>(orientation) == 1 && (mask & 2) && (major_tick_usage[1] || minor_tick_usage[1])) {
    if (major_tick_usage[1]) {
      major.left = graph_area.left - major_tick_length[1];
      major.right = major_tick_grid[1] ? graph_area.right : graph_area.left;
    }
    if (minor_tick_usage[1]) {
      minor.left = graph_area.left - minor_tick_length[1];
      minor.right = minor_tick_grid[1] ? graph_area.right : graph_area.left;
    }
    u32 count = static_cast<u32>(series[0].size());
    for (u32 i = 0; i < count; ++i) {
      i32 center = GetCenterOfDataPoint(i);
      if (minor_tick_usage[1]) {
        minor.top = minor.bottom = i == 0 ? graph_area.top : i == count - 1 ? graph_area.bottom : center;
        target->DrawLine(minor.left, minor.top, minor.right, minor.bottom, tick_color, 1);
      }
      if (major_tick_usage[1]) {
        i32 next = GetCenterOfDataPoint(i + 1);
        assert(horizontal_index != 0);
        major.top = major.bottom = i32(u32(horizontal_center) + u32(next)) / 2;
        target->DrawLine(major.left, major.top, major.right, major.bottom, tick_color, 1);
      }
    }
  }
  target->Unlock(GxLockFlags::Line);
}

// Draw category names along the chosen axis, clipping their rectangles to the graph after numeric labels are drawn.
void WnLineGraph::DrawTickText(GxBuffer *target, WnGraphAxis axes) {
  i32 mask = static_cast<i32>(axes), direction = static_cast<i32>(orientation);
  if (direction == 0 && (mask & 2) && tick_text_drawing[1])
    WnGraph::DrawTickText(target, static_cast<WnGraphAxis>(2));
  if (direction == 1 && (mask & 1) && tick_text_drawing[0])
    WnGraph::DrawTickText(target, static_cast<WnGraphAxis>(1));
  if ((direction != 0 && direction != 1) || !(mask & (direction == 0 ? 1 : 2)) || !tick_text_drawing[direction] ||
      series.empty() || !axis_tick_fonts[direction])
    return;
  FtFont *font = axis_tick_fonts[direction];
  u32 color = axis_tick_colors[direction];
  i32 height = font->Height(0), tick = GetMaxTickLengthForAxis(static_cast<WnGraphAxis>(direction));
  usize count = categories.size() < series[0].size() ? categories.size() : series[0].size();
  for (u32 i = 0; i < count; ++i) {
    const ScString &label = categories[i];
    if (label.begin == label.end)
      continue;
    i32 width = font->Measure(label.begin, static_cast<u32>(label.end - label.begin));
    ScRect box;
    if (direction == 0) {
      box.top = graph_area.bottom + tick;
      box.bottom = box.top + height;
      i32 center = GetCenterOfDataPoint(i);
      box.left = center - (width / 2) - 1;
      box.right = box.left + width + 2;
      if (box.left < graph_area.left) {
        u32 delta = static_cast<u32>(graph_area.left) - static_cast<u32>(box.left);
        box.left = box.left + delta;
        box.right = box.right + delta;
      }
      if (box.right > graph_area.right) {
        u32 delta = static_cast<u32>(graph_area.right) - static_cast<u32>(box.right);
        box.left = box.left + delta;
        box.right = box.right + delta;
      }
    } else {
      box.right = graph_area.left - tick;
      box.left = box.right - width;
      i32 center = GetCenterOfDataPoint(i);
      box.top = center - (height / 2);
      box.bottom = box.top + height + 2;
      if (box.top < graph_area.top) {
        u32 delta = static_cast<u32>(graph_area.top) - static_cast<u32>(box.top);
        box.top = box.top + delta;
        box.bottom = box.bottom + delta;
      }
      if (box.bottom > graph_area.bottom) {
        u32 delta = static_cast<u32>(graph_area.bottom) - static_cast<u32>(box.bottom);
        box.top = box.top + delta;
        box.bottom = box.bottom + delta;
      }
    }
    if (color == kUiGraphUseFontColor)
      font->DrawTextPara(target, box, label.begin, static_cast<u32>(label.end - label.begin), FtOutputStyle::Center);
    else
      font->DrawTextPara(
          target, box, label.begin, static_cast<u32>(label.end - label.begin), color, FtOutputStyle::Center);
  }
}

// Position a category along the graph extent, using at least two positions when the first series is empty or short.
i32 WnLineGraph::GetCenterOfDataPoint(u32 index) {
  u32 count = series.empty() ? 2 : static_cast<u32>(series[0].size());
  if (count <= 1)
    count = 2;
  bool horizontal = static_cast<i32>(orientation) == 0;
  i32 extent = horizontal ? graph_area.right - graph_area.left : graph_area.bottom - graph_area.top;
  i32 position = WnGraphTruncate(static_cast<f64>(extent) * static_cast<i32>(index) / static_cast<i32>(count - 1));
  u32 result = horizontal ? static_cast<u32>(graph_area.left) + static_cast<u32>(position)
                          : static_cast<u32>(graph_area.bottom) - static_cast<u32>(position);
  return static_cast<i32>(result);
}

WnBarSeriesInfo::WnBarSeriesInfo()
    : outline_enabled(1), outline_color(0), fill_enabled(1), fill_color(kUiGraphDefaultFillColor), pattern(nullptr) {
}

std::vector<WnBarSeriesInfo> WnBarGraph::default_series_info(8);

void WnBarGraph::SetDefaultSeriesInfo() {
  const u8 colors[8][3] = {{0, 128, 128}, {128, 0, 128}, {255, 128, 0}, {0, 0, 128}, {128, 0, 0}, {128, 128, 128},
      {255, 255, 128}, {0, 0, 0}};
  GxGraphics *graphics = ApGetGraphics();
  for (u32 i = 0; i < 8; ++i)
    default_series_info[i].fill_color =
        graphics->ConvertRGBValueToNative16Bit(colors[i][0], colors[i][1], colors[i][2]);
}

WnBarGraph::WnBarGraph()
    : type(static_cast<WnBarType>(0)), orientation(static_cast<WnGraphOrientation>(0)), bar_width(1.0f),
      bar_spacing(1.0f), bar_width_percentage(0.75f) {
  major_tick_usage[0] = false;
  major_tick_usage[1] = true;
  major_tick_grid[0] = false;
  major_tick_grid[1] = true;
  tick_text_drawing[0] = true;
}

WnBarGraph::~WnBarGraph() {
  WnBarGraph::Shutdown();
}

bool WnBarGraph::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnBarGraphView, WnBarGraphView);
  return WnGraph::QueryInterface(iid, out);
}

u32 WnBarGraph::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnBarGraph::Release() {
  return WnRootWidget::Release();
}

bool WnBarGraph::Init() {
  return WnGraph::Init();
}

bool WnBarGraph::Shutdown() {
  return WnGraph::Shutdown();
}

void WnBarGraph::SetBarGraphType(WnBarType value) {
  type = value;
}

void WnBarGraph::SetGraphOrientation(WnGraphOrientation value) {
  orientation = value;
}

// Resize the data and series-information arrays, then copy the corresponding original default settings.
// The requested count must stay within the original initialized defaults.
void WnBarGraph::SetSeriesCount(u32 count) {
  series.resize(count);
  series_info.resize(count);
  for (u32 i = 0; i < count; ++i) {
    assert(i < default_series_info.size());
    series_info[i] = default_series_info[i];
  }
}

// Insert an empty data series and its default bar settings, or append both for the sentinel.
void WnBarGraph::InsertSeries(u32 index) {
  if (index == kUiAppendGraphSeries) {
    series.emplace_back();
    series_info.emplace_back();
  } else if (index < series.size()) {
    series.insert(series.begin() + index, std::vector<f32>());
    series_info.insert(series_info.begin() + index, WnBarSeriesInfo());
  }
}

// Remove the indexed data series and settings, or their last entries for the sentinel.
void WnBarGraph::RemoveSeries(u32 index) {
  if (index == kUiLastGraphSeries) {
    series.pop_back();
    series_info.pop_back();
  } else if (index < series.size()) {
    series.erase(series.begin() + index);
    series_info.erase(series_info.begin() + index);
  }
}

// Copy the supplied name into one bar-series record or every record.
void WnBarGraph::SetSeriesName(const ScString &name, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].name = name;
}

void WnBarGraph::SetSeriesInfo(WnBarSeriesInfo *info, u32 index) {
  if (index < series_info.size())
    series_info[index] = *info;
}

// Append bar settings for the sentinel or insert them at an existing position.
void WnBarGraph::InsertSeriesInfo(WnBarSeriesInfo *info, u32 index) {
  if (index == kUiAppendGraphSeries)
    series_info.push_back(*info);
  if (index < series_info.size())
    series_info.insert(series_info.begin() + index, *info);
}

// Remove the final bar settings for the sentinel or erase an indexed record.
void WnBarGraph::RemoveSeriesInfo(u32 index) {
  if (index == kUiLastGraphSeries)
    series_info.pop_back();
  else if (index < series_info.size())
    series_info.erase(series_info.begin() + index);
}

// Copy a category name at its index, growing the vector or appending for the sentinel.
void WnBarGraph::InsertCategoryName(const ScString &name, u32 index) {
  if (index == kUiAppendGraphCategory)
    index = static_cast<u32>(categories.size());
  if (index >= categories.size())
    categories.resize(static_cast<usize>(index) + 1);
  categories[index] = name;
}

// Clear an existing category name without removing its position.
void WnBarGraph::RemoveCategoryName(u32 index) {
  if (index == kUiLastGraphCategory)
    index = static_cast<u32>(categories.size()) - 1;
  if (index < categories.size())
    categories[index].Erase(0, categories[index].ScString::Strlen());
}

void WnBarGraph::SetBarColorUse(WnGraphUsage outline, WnGraphUsage fill, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i) {
      series_info[i].outline_enabled = static_cast<i32>(outline) != 0;
      series_info[i].fill_enabled = static_cast<i32>(fill) != 0;
    }
}

void WnBarGraph::SetBarFillColor(u32 color, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].fill_color = color;
}

void WnBarGraph::SetBarOutlineColor(u32 color, u32 index) {
  for (u32 i = 0; i < series_info.size(); ++i)
    if (index == kUiAllGraphSeries || index == i)
      series_info[i].outline_color = color;
}

// Retain the selected bar series' replacement pattern before releasing its previous pattern.
void WnBarGraph::SetBarPattern(GxBuffer *pattern, u32 index) {
  if (index < series_info.size()) {
    if (pattern)
      pattern->AddRef();
    if (series_info[index].pattern)
      series_info[index].pattern->Release();
    series_info[index].pattern = pattern;
  }
}

// Clamp the bar-width fraction to the executable's range from one tenth through one.
void WnBarGraph::SetBarWidthPercentage(f32 percentage) {
  if (percentage >= 0.1f) {
    if (percentage > 1.0f)
      percentage = 1.0f;
  } else
    percentage = 0.1f;
  bar_width_percentage = percentage;
}

// Insert or append a data value in the selected bar series, accepting the single-series sentinel.
void WnBarGraph::InsertData(f32 value, u32 series_index, u32 position) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size()) {
    auto &values = series[series_index];
    if (position == kUiAppendGraphValue)
      values.push_back(value);
    else
      values.insert(values.begin() + position, value);
  }
}

void WnBarGraph::RemoveData(u32 position, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    series[series_index].erase(series[series_index].begin() + position);
}

// Replace or append one value in the selected series, accepting both original sentinels.
void WnBarGraph::SetData(f32 value, u32 position, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size()) {
    if (position == kUiAppendGraphValue)
      series[series_index].push_back(value);
    else
      series[series_index][position] = value;
  }
}

void WnBarGraph::SetData(std::vector<f32> *values, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    series[series_index] = *values;
}

void WnBarGraph::AppendData(std::vector<f32> *values, u32 series_index) {
  if (series_index == kUiOnlyGraphSeries && series.size() == 1)
    series_index = 0;
  if (series_index < series.size())
    for (usize i = 0; i < values->size(); ++i)
      series[series_index].push_back((*values)[i]);
}

// Compute the category's leading edge using the selected stacking mode, series count, spacing and graph origin.
i32 WnBarGraph::GetLeftOfBar(u32 category, u32 index) {
  f64 result = bar_spacing;
  if (!series.empty()) {
    i32 kind = static_cast<i32>(type);
    if (kind == 0 || kind == 1) {
      f64 origin = static_cast<i32>(orientation) == 0 ? graph_area.left : graph_area.top;
      result = static_cast<f64>(category + 1) * bar_spacing;
      if (kind == 0)
        result += static_cast<f64>(category) * bar_width;
      else
        result += static_cast<f64>(series.size()) * category * bar_width + static_cast<f64>(index) * bar_width;
      result += origin;
    }
  }
  return WnGraphTruncate(result);
}

i32 WnBarGraph::GetRightOfBar(u32 category, u32 index) {
  return WnGraphTruncate(static_cast<f64>(GetLeftOfBar(category, index)) + bar_width);
}

i32 WnBarGraph::GetCenterOfBar(u32 category, u32 index) {
  return WnGraphTruncate(static_cast<f64>(GetLeftOfBar(category, index)) + static_cast<f64>(bar_width) * 0.5);
}

// Calculate a bar's rectangle from its data value and the selected grouped or stacked layout.
bool WnBarGraph::GetRectOfBar(u32 category, u32 index, ScRect *out) {
  if (index >= series.size() || category >= series[index].size()) {
    *out = {0, 0, 0, 0};
    return false;
  }
  bool horizontal = static_cast<i32>(orientation) == 0;
  i32 left = GetLeftOfBar(category, index);
  i32 right = WnGraphTruncate(static_cast<f64>(left) + bar_width);
  u32 first = category, second = category, zero = static_cast<u32>(axis_zero_position[horizontal]);
  WnGraphAxis axis = static_cast<WnGraphAxis>(horizontal);
  i32 kind = static_cast<i32>(type);
  if (kind == 1) {
    f32 value = series[index][category];
    u32 amount = static_cast<u32>(ValueAmountToPixelAmount(axis, value));
    if (value <= 0.0f) {
      second = zero;
      first = zero + amount;
    } else {
      first = zero;
      second = zero - amount;
    }
  } else if (kind == 0) {
    u32 positive = zero, negative = zero;
    for (u32 i = 0; i <= category; ++i) {
      f32 value = series[index][i];
      if (value > 0.0f) {
        first = positive;
        second = positive - static_cast<u32>(ValueAmountToPixelAmount(axis, value));
        positive = second;
      } else if (value < 0.0f) {
        second = negative;
        first = negative + static_cast<u32>(ValueAmountToPixelAmount(axis, value));
        negative = first;
      }
    }
  }
  if (horizontal)
    *out = {left, static_cast<i32>(second), right, static_cast<i32>(first)};
  else
    *out = {static_cast<i32>(second), left, static_cast<i32>(first), right};
  return true;
}

// Combine the original per-series rectangles for a category, or clear the output when the first series has no entry.
bool WnBarGraph::GetRectOfBarsInCategory(u32 category, ScRect *out) {
  if (series.empty() || category >= series[0].size()) {
    *out = {0, 0, 0, 0};
    return false;
  }
  GetRectOfBar(category, 0, out);
  for (u32 i = 1; i < series.size(); ++i) {
    ScRect current;
    GetRectOfBar(category, i, &current);
    out->left = out->left < current.left ? out->left : current.left;
    out->top = out->top < current.top ? out->top : current.top;
    out->right = out->right > current.right ? out->right : current.right;
    out->bottom = out->bottom > current.bottom ? out->bottom : current.bottom;
  }
  return true;
}

// Find the first bar containing the point and publish its series and category in the hit-data allocation.
bool WnBarGraph::DoHitTest(ScPoint *point, u32 *result) {
  for (u32 index = 0; index < series.size(); ++index) {
    for (u32 category = 0; category < series[index].size(); ++category) {
      ScRect rectangle;
      GetRectOfBar(category, index, &rectangle);
      if (point->x >= rectangle.left && point->y >= rectangle.top && point->x <= rectangle.right &&
          point->y <= rectangle.bottom) {
        AllocateHitTestData(12);
        std::memcpy(hit_data + 4, &index, sizeof(index));
        std::memcpy(hit_data + 8, &category, sizeof(category));
        *result = 1001;
        return true;
      }
    }
  }
  return WnGraph::DoHitTest(point, result);
}

// Choose the current drawing buffer when none is supplied, then recalculate bar spacing before the base graph redraw.
void WnBarGraph::CompleteRedraw(GxBuffer *target) {
  if (!target)
    target = draw_buffer;
  if (target) {
    CalculateBarWidthAndSpacing();
    WnGraph::CompleteRedraw(target);
  }
}

void WnBarGraph::DrawGraphData(GxBuffer *target) {
  DrawAllBars(target);
}

// Draw bar-category ticks on the borrowed target, retaining coordinate rectangles across orientation and type changes.
// Grouped vertical minor ticks reuse the Y pair supplied by an earlier pass; assert if that pair was never produced.
void WnBarGraph::DrawTicks(GxBuffer *target, WnGraphAxis axes) {
  i32 mask = static_cast<i32>(axes);
  ScRect major{0, 0, 0, 0}, minor{0, 0, 0, 0};
  [[maybe_unused]] bool have_minor_y = false;
  if (static_cast<i32>(orientation) == 0 && (mask & 2))
    WnGraph::DrawTicks(target, static_cast<WnGraphAxis>(2));
  if (static_cast<i32>(orientation) == 1 && (mask & 1))
    WnGraph::DrawTicks(target, static_cast<WnGraphAxis>(1));
  if ((!major_tick_usage[0] && !minor_tick_usage[0] && !major_tick_usage[1] && !minor_tick_usage[1]) ||
      series.empty() || !target->Lock(GxLockFlags::Line))
    return;
  if (static_cast<i32>(orientation) == 0 && (mask & 1) && (major_tick_usage[0] || minor_tick_usage[0])) {
    if (major_tick_usage[0]) {
      major.bottom = graph_area.bottom + major_tick_length[0];
      major.top = major_tick_grid[0] ? graph_area.top : graph_area.bottom;
    }
    if (minor_tick_usage[0]) {
      minor.bottom = graph_area.bottom + minor_tick_length[0];
      minor.top = minor_tick_grid[0] ? graph_area.top : graph_area.bottom;
      have_minor_y = true;
    }
    u32 count = static_cast<u32>(series[0].size());
    for (u32 i = 0; i < count; ++i) {
      i32 left = GetLeftOfBar(i, 0);
      if (major_tick_usage[0]) {
        major.left = major.right = WnGraphTruncate(static_cast<f64>(left) - static_cast<f64>(bar_spacing) * 0.5);
        target->DrawLine(major.left, major.top, major.right, major.bottom, tick_color, 1);
      }
      if (minor_tick_usage[0]) {
        u32 count_series = static_cast<u32>(series.size());
        f64 width = static_cast<f64>(bar_width);
        if (static_cast<i32>(type) != 0 && count_series != 1)
          width *= count_series;
        minor.left = minor.right = WnGraphTruncate(width * 0.5 + left);
        assert(have_minor_y);
        target->DrawLine(minor.left, minor.top, minor.right, minor.bottom, tick_color, 1);
      }
    }
  }
  if (static_cast<i32>(orientation) == 1 && (mask & 2) && (major_tick_usage[1] || minor_tick_usage[1])) {
    if (major_tick_usage[1]) {
      major.left = graph_area.left - major_tick_length[1];
      major.right = major_tick_grid[1] ? graph_area.right : graph_area.left;
    }
    if (minor_tick_usage[1]) {
      minor.left = graph_area.left - minor_tick_length[1];
      minor.right = minor_tick_grid[1] ? graph_area.right : graph_area.left;
    }
    u32 count = static_cast<u32>(series[0].size());
    for (u32 i = 0; i < count; ++i) {
      i32 left = GetLeftOfBar(i, 0);
      if (major_tick_usage[1]) {
        major.top = major.bottom = WnGraphTruncate(static_cast<f64>(left) - static_cast<f64>(bar_spacing) * 0.5);
        target->DrawLine(major.left, major.top, major.right, major.bottom, tick_color, 1);
      }
      if (minor_tick_usage[1]) {
        u32 count_series = static_cast<u32>(series.size());
        if (static_cast<i32>(type) == 0 || count_series == 1) {
          minor.top = minor.bottom = WnGraphTruncate(static_cast<f64>(bar_width) * 0.5 + left);
          have_minor_y = true;
        } else {
          minor.left = minor.right = WnGraphTruncate(static_cast<f64>(count_series) * bar_width * 0.5 + left);
        }
        assert(have_minor_y);
        target->DrawLine(minor.left, minor.top, minor.right, minor.bottom, tick_color, 1);
      }
    }
  }
  target->Unlock(GxLockFlags::Line);
}

// Center category captions over the combined bar rectangles after drawing the numeric-axis labels.
void WnBarGraph::DrawTickText(GxBuffer *target, WnGraphAxis axes) {
  i32 mask = static_cast<i32>(axes), direction = static_cast<i32>(orientation);
  if (direction == 0 && (mask & 2) && tick_text_drawing[1])
    WnGraph::DrawTickText(target, static_cast<WnGraphAxis>(2));
  if (direction == 1 && (mask & 1) && tick_text_drawing[0])
    WnGraph::DrawTickText(target, static_cast<WnGraphAxis>(1));
  if ((direction != 0 && direction != 1) || !(mask & (direction == 0 ? 1 : 2)) || !tick_text_drawing[direction] ||
      series.empty() || !axis_tick_fonts[direction])
    return;
  FtFont *font = axis_tick_fonts[direction];
  u32 color = axis_tick_colors[direction];
  i32 height = font->Height(0), tick = GetMaxTickLengthForAxis(static_cast<WnGraphAxis>(direction));
  usize count = categories.size() < series[0].size() ? categories.size() : series[0].size();
  for (u32 i = 0; i < count; ++i) {
    const ScString &label = categories[i];
    if (label.begin == label.end)
      continue;
    ScRect box;
    GetRectOfBarsInCategory(i, &box);
    i32 width = font->Measure(label.begin, static_cast<u32>(label.end - label.begin));
    if (direction == 0) {
      i32 span = box.right - box.left;
      box.top = graph_area.bottom + tick;
      box.bottom = box.top + height;
      box.left = box.left + (span / 2) - (width / 2) - 1;
      box.right = box.left + width + 2;
    } else {
      i32 span = box.bottom - box.top;
      box.right = graph_area.left - tick;
      box.left = box.right - width;
      box.top = box.top + (span / 2) - (height / 2);
      box.bottom = box.top + height + 1;
    }
    if (color == kUiGraphUseFontColor)
      font->DrawTextPara(target, box, label.begin, static_cast<u32>(label.end - label.begin), FtOutputStyle::Center);
    else
      font->DrawTextPara(
          target, box, label.begin, static_cast<u32>(label.end - label.begin), color, FtOutputStyle::Center);
  }
}

// Draw stacked or grouped bars in category order, tracking positive and negative stacks independently.
// Skip zero and unordered values, preserving the original per-series size branches before their direct reads.
void WnBarGraph::DrawAllBars(GxBuffer *target) {
  u32 series_count = static_cast<u32>(series.size());
  if (!series_count)
    return;
  i32 kind = static_cast<i32>(type);
  if (kind != 0 && kind != 1)
    return;
  WnGraphAxis axis = static_cast<WnGraphAxis>(static_cast<i32>(orientation) == 0);
  if (kind == 1) {
    f64 initial = static_cast<f64>(graph_area.left) + bar_spacing;
    f32 left = static_cast<f32>(initial), right = static_cast<f32>(initial + bar_width);
    u32 category_count = static_cast<u32>(series[0].size());
    for (u32 category = 0; category < category_count; ++category) {
      for (u32 index = 0; index < series_count; ++index) {
        assert(category < series[index].size());
        f32 value = series[index][category];
        if (value != 0.0f && !std::isnan(value)) {
          f32 top, bottom;
          if (value > 0.0f) {
            top = static_cast<f32>(ValueToPixel(axis, value, true));
            bottom = static_cast<f32>(axis_zero_position[static_cast<i32>(axis)]);
          } else {
            top = static_cast<f32>(axis_zero_position[static_cast<i32>(axis)]);
            bottom = static_cast<f32>(ValueToPixel(axis, value, true));
          }
          ScRect rectangle{
              WnGraphTruncate(left), WnGraphTruncate(top), WnGraphTruncate(right), WnGraphTruncate(bottom)};
          DrawBar(target, &rectangle, index, static_cast<WnBarDirection>(value > 0.0f ? 0 : 1));
        }
        left = static_cast<f32>(static_cast<f64>(left) + bar_width);
        right = static_cast<f32>(static_cast<f64>(right) + bar_width);
      }
      left = static_cast<f32>(static_cast<f64>(left) + bar_spacing);
      right = static_cast<f32>(static_cast<f64>(right) + bar_spacing);
    }
    return;
  }
  std::vector<u32> sizes;
  u32 category_count = 0;
  for (u32 index = 0; index < series_count; ++index) {
    u32 count = static_cast<u32>(series[index].size());
    sizes.push_back(count);
    if (count > category_count)
      category_count = count;
  }
  f64 initial = static_cast<f64>(graph_area.left) + bar_spacing;
  f32 left = static_cast<f32>(initial), right = static_cast<f32>(initial + bar_width);
  for (u32 category = 0; category < category_count; ++category) {
    i32 positive = axis_zero_position[static_cast<i32>(axis)], negative = positive;
    for (u32 index = 0; index < series_count; ++index) {
      if (category >= sizes[index])
        continue;
      assert(category < series[index].size());
      f32 value = series[index][category];
      if (value == 0.0f || std::isnan(value))
        continue;
      i32 pixels = ValueAmountToPixelAmount(axis, value);
      f32 top, bottom;
      if (value > 0.0f) {
        i32 next = positive - pixels;
        top = static_cast<f32>(next);
        bottom = static_cast<f32>(positive);
        positive = WnGraphTruncate(top);
      } else {
        i32 next = negative + pixels;
        top = static_cast<f32>(negative);
        bottom = static_cast<f32>(next);
        negative = WnGraphTruncate(bottom);
      }
      ScRect rectangle{WnGraphTruncate(left), WnGraphTruncate(top), WnGraphTruncate(right), WnGraphTruncate(bottom)};
      DrawBar(target, &rectangle, index, static_cast<WnBarDirection>(value > 0.0f ? 0 : 1));
    }
    f64 next = static_cast<f64>(bar_spacing) + bar_width + left;
    left = static_cast<f32>(next);
    right = static_cast<f32>(next + bar_width);
  }
}

// Paint a patterned bar or its enabled solid fill and outline using separate original buffer locks.
void WnBarGraph::DrawBar(GxBuffer *target, ScRect *rectangle, u32 index, WnBarDirection) {
  WnBarSeriesInfo &info = series_info[index];
  if (info.pattern) {
    if (target->Lock(GxLockFlags::Blt)) {
      ScRect source{0, 0, static_cast<i32>(info.pattern->Width()), static_cast<i32>(info.pattern->Height())};
      target->BltTiled(info.pattern, source, *rectangle, 0, 0, nullptr);
      target->Unlock(GxLockFlags::Blt);
    }
  } else {
    if (info.fill_enabled && target->Lock(GxLockFlags::Fill)) {
      target->Fill(info.fill_color, *rectangle, nullptr);
      target->Unlock(GxLockFlags::Fill);
    }
    if (info.outline_enabled && target->Lock(GxLockFlags::Rect)) {
      target->DrawRectangleOutline(*rectangle, info.outline_color, 1);
      target->Unlock(GxLockFlags::Rect);
    }
  }
}

// Find the largest value or positive stacked total, retaining the original empty-series defaults.
f32 WnBarGraph::GetHighestAxisValue(WnGraphAxis) {
  f32 result = -FLT_MAX;
  if (static_cast<i32>(type) != 0) {
    for (const auto &values : series)
      for (f32 value : values)
        if (value > result)
          result = value;
    return result;
  }
  if (series.empty())
    return 100.0f;
  usize count = 0;
  for (const auto &values : series)
    if (values.size() > count)
      count = values.size();
  for (usize index = 0; index < count; ++index) {
    f64 sum = 0;
    for (const auto &values : series)
      if (index < values.size() && values[index] > 0.0f)
        sum += values[index];
    if (sum > result)
      result = static_cast<f32>(sum);
  }
  return result;
}

// Find the smallest value or negative stacked total, retaining the original empty-series defaults.
f32 WnBarGraph::GetLowestAxisValue(WnGraphAxis) {
  f32 result = FLT_MAX;
  if (static_cast<i32>(type) != 0) {
    for (const auto &values : series)
      for (f32 value : values)
        if (value < result)
          result = value;
    return result;
  }
  if (series.empty())
    return 100.0f;
  usize count = 0;
  for (const auto &values : series)
    if (values.size() > count)
      count = values.size();
  for (usize index = 0; index < count; ++index) {
    f64 sum = 0;
    for (const auto &values : series)
      if (index < values.size() && values[index] < 0.0f)
        sum += values[index];
    if (sum < result)
      result = static_cast<f32>(sum);
  }
  return result;
}

// Derive bar width and inter-category spacing from the first series and the selected graph orientation and type.
void WnBarGraph::CalculateBarWidthAndSpacing() {
  u32 extent = static_cast<i32>(orientation) == 0
      ? static_cast<u32>(graph_area.right) - static_cast<u32>(graph_area.left)
      : static_cast<u32>(graph_area.bottom) - static_cast<u32>(graph_area.top);
  i32 kind = static_cast<i32>(type);
  if (kind != 0 && kind != 1)
    return;
  u32 count = 1;
  if (series.empty() || series[0].empty()) {
    bar_width = static_cast<f32>(CalculateMaximumBarWidth());
  } else {
    count = static_cast<u32>(series[0].size());
    u32 denominator = kind == 1 ? count * static_cast<u32>(series.size()) : count;
    bar_width = static_cast<f32>(static_cast<f64>(extent) * bar_width_percentage / denominator);
  }
  f64 occupied = static_cast<f64>(count) * bar_width;
  if (kind == 1)
    occupied = static_cast<f64>(series.size()) * count * bar_width;
  bar_spacing = static_cast<f32>((static_cast<f64>(extent) - occupied) / (count + 1));
}

// Scale the graph's horizontal extent by the configured width percentage, dividing by the series count in grouped mode.
u32 WnBarGraph::CalculateMaximumBarWidth() {
  i32 width = graph_area.right - graph_area.left;
  f64 value = static_cast<f64>(width) * bar_width_percentage;
  if (static_cast<i32>(type) == 1 && !series.empty())
    value /= static_cast<f64>(series.size());
  return static_cast<u32>(WnGraphTruncate(value));
}

// Acquire a graph font in the original temporary language and restore the prior language after the service call.
static FtFont *WnAcquireGraphFont(i32 type, u32 size, u32 style, u32 source) {
  FtFontSystem *fonts = ApGetFontManager();
  if (!fonts)
    return nullptr;
  ScLanguageManager *language = ApGetLanguageManager();
  u32 previous = 0;
  if (language) {
    previous = language->GetCurrentLanguage();
    language->SetCurrentLanguage(1);
  }
  FtFont *font = fonts->FontAcquire(static_cast<FtType>(type), size, style, source);
  if (language)
    language->SetCurrentLanguage(previous);
  return font;
}

void WnGraph::SetAxisLabelFont(WnGraphAxis axis, i32 type, u32 size, u32 style, u32 source) {
  SetAxisLabelFont(axis, WnAcquireGraphFont(type, size, style, source), kUiGraphUseFontColor);
}

void WnGraph::SetAxisTickFont(WnGraphAxis axis, i32 type, u32 size, u32 style, u32 source) {
  SetAxisTickFont(axis, WnAcquireGraphFont(type, size, style, source), kUiGraphUseFontColor);
}

void WnGraph::SetTitleFont(i32 type, u32 size, u32 style, u32 source) {
  SetTitleFont(WnAcquireGraphFont(type, size, style, source), kUiGraphUseFontColor);
}

void WnLineGraph::SetSymbolFont(i32 type, u32 size, u32 style, u32 source) {
  SetSymbolFont(WnAcquireGraphFont(type, size, style, source), kUiGraphUseFontColor);
}

void WnScatterGraph::SetSymbolFont(i32 type, u32 size, u32 style, u32 source) {
  SetSymbolFont(WnAcquireGraphFont(type, size, style, source), kUiGraphUseFontColor);
}

// Acquire and retain the default text-object font only when the object does not already hold one.
// Successful acquisition must publish the receiver used by the original AddRef call.
bool WnGraphText::Init() {
  if (!font) {
    font = WnAcquireGraphFont(0, 12, 0, 0);
    assert(font);
    font->AddRef();
  }
  return font != nullptr;
}

// Construct a text object and register it with the graph's dynamic-object list before returning it.
WnGraphText *WnGraph::AddMiscGraphTextInfo() {
  WnGraphText *text = new WnGraphText;
  AddDynamicGraphObject(text);
  return text;
}

ApCheatDialog::ApCheatDialog() : WnGeneralDialog(WnDialogStyle::Edge), input(nullptr) {
}

ApCheatDialog::~ApCheatDialog() {
  ApCheatDialog::Shutdown();
}

// Load the cheat dialog background, create its input field, and focus the field in the window manager.
bool ApCheatDialog::Init() {
  if (GetInitFlag())
    return true;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageHandle image(kApCheatDialogBackgroundImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, format);
    background = image.GetBuffer();
  }
  if (background)
    background->AddRef();
  WnGeneralDialog::Init();
  ScRect rectangle{4, 4, 308, 24};
  WnWindow *window = AddLineInput(rectangle);
  window->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&input));
  window->Release();
  owner->SetFocus(window);
  SetW(rectangle.right - rectangle.left + 8);
  SetH(rectangle.bottom - rectangle.top + 8);
  SetFillColor(MakeFillColor(255, 0, 255));
  return true;
}

// Release the retained background and input interface before shutting down an initialized cheat dialog.
bool ApCheatDialog::Shutdown() {
  if (!GetInitFlag())
    return true;
  if (background) {
    background->Release();
    background = nullptr;
  }
  input->Release();
  SetFlag(kWnFlagVisible, false);
  return WnGeneralDialog::Shutdown();
}

bool ApCheatDialog::OnKeyDown(u32 key, u32 modifiers) {
  if (key == kWnKeyEscape || (key == kWnKeyF4 && (modifiers & kWnModifierAlt)))
    owner->GZWinMsgPost(kWnMsg_EndModal, (uptr)this, 0);
  return true;
}

// Process an entered cheat, keeping the dialog open with Shift or Control and otherwise posting its close result.
bool ApCheatDialog::OnCommand(u32 kind, u32) {
  if (kind != kWnCommandTextAccepted)
    return false;
  ScString text;
  ApGameInterface *game = nullptr;
  ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&game));
  game->Release();
  ScCheatCodeManager *cheats = game->GetCheatCodeManager();
  u32 cheat_id = 0;
  input->GetText(&text);
  if (text.begin == text.end) {
    owner->GZWinMsgPost(kWnMsg_EndModal, (uptr)this, 0);
  } else {
    cheats->DoDefaultCheatCodeProcessing(text, &cheat_id);
    if (cheat_id) {
      if (keyboard->IsKeyDown(kWnKeyShift) || keyboard->IsKeyDown(kWnKeyControl)) {
        char empty[] = "";
        input->SetText(empty);
      } else {
        owner->GZWinMsgPost(kWnMsg_EndModal, (uptr)this, 1);
      }
      PlayCheatCodeSound(true);
    } else {
      PlayCheatCodeSound(false);
    }
  }
  return true;
}

void ApCheatDialog::PlayCheatCodeSound(bool) {
}

UiLogoWindow::UiLogoWindow()
    : wait_type(kUiLogoWaitForPaints), paint_count(0), buffer(nullptr), labels{nullptr, nullptr},
      identifiers{kUiLogoPrimaryLabelId, kUiLogoSecondaryLabelId} {
}

UiLogoWindow::~UiLogoWindow() {
}

// Release the retained background and both strings before closing the application window.
bool UiLogoWindow::Shutdown() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
  for (i32 i = 0; i < 2; ++i) {
    if (labels[i])
      const_cast<ScStringView *>(labels[i])->Release();
    labels[i] = nullptr;
  }
  return UiWindow::Shutdown();
}

// Size the window to its image when present, clamp negative dimensions and relayout only on a size change.
void UiLogoWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = right - left;
  i32 height = bottom - top;
  if (buffer) {
    width = static_cast<i32>(buffer->Width());
    height = static_cast<i32>(buffer->Height());
  }
  if (width < 0)
    width = 0;
  if (height < 0)
    height = 0;
  bool changed = width != GetW() || height != GetH();
  UiWindow::SetArea(left, top, left + width, top + height);
  if (changed)
    Layout(width, height);
}

// Consume a left click, posting the manual completion only after two paint calls.
bool UiLogoWindow::OnMouseDownL(i32, i32, u32) {
  if (IsTimedOut())
    EndDialog(kUiLogoInputResult);
  return true;
}

// Consume a key press, posting the same manual completion after the initial paint interval.
bool UiLogoWindow::OnKeyDown(u32, u32) {
  if (IsTimedOut())
    EndDialog(kUiLogoInputResult);
  return true;
}

// Borrow this object's original window identity without acquiring another reference.
WnWindow *UiLogoWindow::AsIGZWin() {
  return this;
}

bool UiLogoWindow::SetWaitType(i32 value) {
  wait_type = value;
  return true;
}

// Retain the new background before releasing the old one, then size from the image or the default 640-by-480 rectangle.
bool UiLogoWindow::SetBuffer(GxBuffer *value, bool fit) {
  if (value)
    value->AddRef();
  if (buffer)
    buffer->Release();
  buffer = value;
  if (value && fit) {
    u32 height = value->Height();
    u32 width = value->Width();
    SetSize(static_cast<i32>(width), static_cast<i32>(height));
  } else
    SetSize(640, 480);
  return true;
}

// Retain a valid replacement label before releasing the old text, then update any existing foreground and shadow.
bool UiLogoWindow::SetLabelText(i32 index, const ScStringView *text) {
  if (!IsValidLabelId(index))
    return false;
  if (text)
    const_cast<ScStringView *>(text)->AddRef();
  if (labels[index])
    const_cast<ScStringView *>(labels[index])->Release();
  labels[index] = text;
  if (text) {
    u32 control_id = identifiers[index];
    WnWindow *foreground = GetChildWindowFromID(control_id);
    if (foreground)
      foreground->SetCaption(*labels[index]);
    WnWindow *shadow = GetChildWindowFromID(control_id + 1);
    if (shadow)
      shadow->SetCaption(*labels[index]);
  }
  return true;
}

bool UiLogoWindow::IsTimedOut() const {
  return paint_count >= 2;
}

bool UiLogoWindow::EndDialog(u32 result) {
  owner->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(this), result);
  return true;
}

bool UiLogoWindow::IsValidLabelId(i32 index) const {
  return index >= 0 && index < 2;
}

// Move the shadow rectangle one pixel down and right from its corresponding foreground rectangle.
bool UiLogoWindow::SyncShadowArea(u32 control_id) {
  WnWindow *foreground = GetChildWindowFromID(control_id);
  if (!foreground)
    return false;
  WnWindow *shadow = GetChildWindowFromID(control_id + 1);
  if (!shadow)
    return false;
  ScRect rectangle;
  foreground->GetArea(&rectangle);
  rectangle.left = rectangle.left + 1;
  rectangle.top = rectangle.top + 1;
  rectangle.right = rectangle.right + 1;
  rectangle.bottom = rectangle.bottom + 1;
  shadow->SetArea(rectangle);
  return true;
}

// Fit the two foreground labels against the lower right and lower left edges, then align their shadows.
bool UiLogoWindow::Layout(i32 width, i32 height) {
  WnLabelControl *label = nullptr;
  if (GetChildAs(kUiLogoPrimaryLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->Fit();
    WnWindow *height_window = label->GetWindow();
    WnWindow *width_window = label->GetWindow();
    WnWindow *target = label->GetWindow();
    i32 bottom = height - 4;
    i32 right = width - 2;
    i32 top = height - height_window->GetH() - 4;
    i32 left = width - width_window->GetW() - 2;
    target->SetArea(left, top, right, bottom);
    SyncShadowArea(kUiLogoPrimaryLabelId);
    label->Release();
    label = nullptr;
  }
  if (GetChildAs(kUiLogoSecondaryLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->Fit();
    WnWindow *width_window = label->GetWindow();
    WnWindow *height_window = label->GetWindow();
    WnWindow *target = label->GetWindow();
    i32 bottom = height - 4;
    i32 right = width_window->GetW() + 2;
    i32 top = height - height_window->GetH() - 4;
    target->SetArea(2, top, right, bottom);
    SyncShadowArea(kUiLogoSecondaryLabelId);
    label->Release();
  }
  return true;
}

// Create the shadow and foreground controls for a populated label, retaining their original colors and child order.
bool UiLogoWindow::CreateSpecialLabel(WnResources *resources, i32 index, u32 control_id, FtFont *font) {
  if (!IsValidLabelId(index) || !font || !labels[index])
    return false;
  const ScStringView *text = labels[index];
  WnLabelControl *label = resources->CreateLabel(identifiers[index] + 1, *text);
  if (label) {
    label->SetAlignment(kWnLabelCenterMiddle);
    label->SetFont(font);
    label->SetTextColor(0, MakeFillColor(32, 32, 32));
    label->SetTextColor(1, MakeFillColor(54, 54, 64));
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->GetWindow()->SetFlag(kWnFlagReorderable, false);
    label->Fit();
    ChildAdd(label->GetWindow());
    label->Release();
  }
  label = resources->CreateLabel(control_id, *text);
  if (label) {
    label->SetAlignment(kWnLabelCenterMiddle);
    label->SetFont(font);
    label->SetTextColor(0, MakeFillColor(212, 122, 69));
    label->SetTextColor(1, MakeFillColor(32, 32, 32));
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->GetWindow()->SetFlag(kWnFlagReorderable, false);
    label->Fit();
    ChildAdd(label->GetWindow());
    label->Release();
  }
  return true;
}

// Initialize the base, create the two optional label pairs through the acquired control manager, and center the window.
bool UiLogoWindow::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  UiWindow::Init();
  ScFrameworkRef<WnResources> resources(nullptr);
  ApGetFramework()->GetSystemService(
      kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&resources.object));
  if (!resources.object)
    return false;
  i32 width = GetW(), height = GetH();
  // The service ABI returns cIGZFont; the older shared getter still spells its result as the concrete bitmap type.
  FtFont *font = resources.object->GetFont(12);
  if (!font)
    font = resources.object->GetFont(0);
  CreateSpecialLabel(resources.object, 0, kUiLogoPrimaryLabelId, font);
  CreateSpecialLabel(resources.object, 1, kUiLogoSecondaryLabelId, font);
  Layout(width, height);
  WnWindow *main = GetMainWindow();
  if (main) {
    i32 main_height = main->GetH();
    i32 main_width = main->GetW();
    i32 x = (main_width - width) / 2;
    i32 y = (main_height - height) / 2;
    GZWinMoveTo(x, y);
  }
  return true;
}

// Count paints, post automatic completion for the selected wait mode, then draw the image or black background.
bool UiLogoWindow::GZPaint() {
  ++paint_count;
  if ((wait_type == kUiLogoWaitForPaints && IsTimedOut()) ||
      (wait_type != kUiLogoWaitForPaints && wait_type != kUiLogoWaitForInput))
    EndDialog(kUiLogoAutomaticResult);
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    if (buffer)
      draw_buffer->Blt(buffer, *buffer->GetBufferArea(), draw_area, static_cast<const ScRect *>(nullptr));
    else
      draw_buffer->Fill(0, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

ApDancingBear::ApDancingBear()
    : animation(nullptr), intro_status(0), video_x(0), video_y(0), screen_buffer(nullptr), page_flipping(false),
      screen_buffer_locked(false), saved_thread_priority(3) {
}

ApDancingBear::~ApDancingBear() {
  Shutdown();
}

bool ApDancingBear::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ApDancingBearView, ApDancingBearView);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 ApDancingBear::AddRef() {
  return WnRootWidget::AddRef();
}

u32 ApDancingBear::Release() {
  return WnRootWidget::Release();
}

// Initialize the intro window once, using the live main window's right and bottom bounds.
// Preserve the two independent main-window lookups before delegating to the original root initialization.
bool ApDancingBear::Init() {
  if (GetInitFlag())
    return true;
  SetID(kApDancingBearWindowId);
  SetFlag(kWnFlagReorderable, false);
  WnWindow *bottom_window = GetMainWindow();
  WnWindow *right_window = GetMainWindow();
  i32 bottom = bottom_window->GetB();
  i32 right = right_window->GetR();
  SetArea(0, 0, right, bottom);
  return WnRootWidget::Init();
}

// Start or advance the original movie according to its current state and post the end message when startup fails.
bool ApDancingBear::GZPaint() {
  if (intro_status == 0) {
    if (!InitIntroVideo())
      PostMsg(this, kWnMsg_KeyDown, kWnKeyEscape, 0, 0);
    return true;
  }
  if (intro_status == 1)
    return ContinueIntroVideo();
  return intro_status == 3;
}

// Find the original intro movie in the data directory or CD fallback, configure its player and begin playback.
// Preserve capture, screen clearing, page swaps, volume lookup and end-message ordering.
bool ApDancingBear::InitIntroVideo() {
  ScString movie(ApGetFileSystem()->DataDirectory());
  movie.Append("Res\\UI\\Shared\\Movies\\Intro.tgq");
  if (!RsFile::FileExists(movie)) {
    ScString fallback(ApGetFileSystem()->CDDataDirectory());
    movie = fallback;
    movie.Append("Res\\UI\\Shared\\Movies\\Intro.tgq");
    if (!RsFile::FileExists(movie))
      return false;
  }
  animation = new ApAnimationPlayer;
  animation->SetDestinationBufferColorType(draw_buffer);
  animation->SetDestinationType(ApAnimationPlayer::Destination::PrimaryScreen);
  animation->SetSize(640, 480);
  if (animation->Open(movie.begin)) {
    GZSetCapture();
    if (ApGetPreferences()->SoundPrefs()->music_volume <= 128)
      animation->SetVolume(0);
    else
      animation->SetVolume(ApGetPreferences()->SoundPrefs()->music_volume);
    screen_buffer = ApGetGraphics()->ScreenBuffer();
    page_flipping = screen_buffer->IsPageFlipping();
    GxBuffer *width_buffer = draw_buffer;
    u32 width = animation->GetDestinationWidth();
    video_x = static_cast<u16>((width_buffer->Width() - width) >> 1);
    GxBuffer *height_buffer = draw_buffer;
    u32 height = animation->GetDestinationHeight();
    video_y = static_cast<u16>((height_buffer->Height() - height) >> 1);
    ScPoint point{video_x, video_y};
    screen_buffer->AdjustClientPoint(&point);
    video_x = static_cast<u16>(point.x);
    video_y = static_cast<u16>(point.y);
    animation->SetPosition(video_x >> 1, video_y);
    for (u32 i = 0; i < 2; ++i) {
      if (draw_buffer->Lock(static_cast<GxLockFlags>(1))) {
        draw_buffer->Fill(0, static_cast<ScRect *>(nullptr));
        draw_buffer->Unlock(static_cast<GxLockFlags>(1));
      }
      screen_buffer->Swap();
    }
    bool playing;
    if (screen_buffer->LockFrontSurface(static_cast<GxLockFlags>(64))) {
      screen_buffer->UnlockFrontSurface(static_cast<GxLockFlags>(64));
      playing = animation->ComposeToGZBuffer(draw_buffer, video_x, video_y);
    } else {
      animation->Close();
      playing = false;
    }
    if (playing) {
      intro_status = 1;
      screen_buffer->DisableComposing(1);
      return true;
    }
  }
  PostMsg(this, kWnMsg_KeyDown, kWnKeyEscape, 0, 0);
  return false;
}

// Compose the next movie frame into the current draw buffer and post its end message when playback stops.
bool ApDancingBear::ContinueIntroVideo() {
  if (!animation->ComposeToGZBuffer(draw_buffer, video_x, video_y))
    PostMsg(this, kWnMsg_KeyDown, kWnKeyEscape, 0, 0);
  return true;
}

bool ApDancingBear::Shutdown() {
  ScDelete(&animation);
  return WnRootWidget::Shutdown();
}

bool ApDancingBear::OnKeyDown(u32, u32) {
  EndIntroVideo();
  return true;
}

// End an active movie on a left click; an already ended movie consumes the click without forwarding it.
// Other states use the actual root-window handler with the original coordinates and modifiers.
bool ApDancingBear::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  if (intro_status == 1) {
    PostMsg(this, kWnMsg_KeyDown, kWnKeyEscape, 0, 0);
    return true;
  }
  if (intro_status == 3)
    return true;
  return WnRootWidget::OnMouseDownL(x, y, modifiers);
}

// Release the movie's screen lock, composition hold and capture before closing the player.
// Switch to the copyright screen and post the window event only on the first transition to the ended state.
bool ApDancingBear::EndIntroVideo() {
  if (intro_status != 3) {
    if (screen_buffer_locked) {
      if (screen_buffer)
        screen_buffer->UnlockFrontSurface(static_cast<GxLockFlags>(64));
      screen_buffer_locked = false;
    }
    if (screen_buffer)
      screen_buffer->DisableComposing(-1);
    if (GZAmCapturing())
      GZReleaseCapture();
    if (animation) {
      animation->Close();
      delete animation;
      animation = nullptr;
    }
    intro_status = 3;
    ApGetGameApp()->GetSC3MainWindow()->DoCopyRightScreen(true);
    WnWindow *window = AsIGZWin();
    owner->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  }
  return true;
}

// Borrow the primary window identity from the secondary intro-window interface.
WnWindow *ApDancingBear::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

i32 ApDancingBear::GetCurrentStatus() const {
  return intro_status;
}

UiImageList::UiImageList() {
}

UiImageList::~UiImageList() {
}

bool UiImageList::AddImage(ScStringView *, i32, i32) {
  return false;
}

// Release the stored buffer and erase the matching entry; an absent identifier returns false.
bool UiImageList::RemoveImage(i32 identifier) {
  auto found = images.find(identifier);
  if (found == images.end())
    return false;
  found->second->Release();
  images.erase(found);
  return true;
}

bool UiImageList::AddImage(i32 identifier, GxBuffer *buffer) {
  return images.insert({identifier, buffer}).second;
}

// Blit the matching image at the supplied position and advance the caller's horizontal coordinate.
bool UiImageList::DrawImage(i32 identifier, GxBuffer *target, i32 *x, i32 y) {
  auto found = images.find(identifier);
  if (found == images.end() || !found->second)
    return false;
  GxBuffer *image = found->second;
  ScRect destination;
  image->GetBufferArea(&destination);
  destination.left = i32(u32(destination.left) + u32(*x));
  destination.right = i32(u32(destination.right) + u32(*x));
  destination.top = i32(u32(destination.top) + u32(y));
  destination.bottom = i32(u32(destination.bottom) + u32(y));
  *x = i32(u32(*x) - u32(destination.left) + u32(destination.right));
  const ScRect *source = image->GetBufferArea();
  return target->Blt(image, *source, destination, static_cast<const ScRect *>(nullptr));
}

static i32 UiTipAdd(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) + static_cast<u32>(second));
}

static i32 UiTipSubtract(i32 first, i32 second) {
  return static_cast<i32>(static_cast<u32>(first) - static_cast<u32>(second));
}

UiToolTipWindow::UiToolTipWindow()
    : font(nullptr), horizontal(0), vertical(0), offscreen(nullptr), coordinates{}, foreground(0), background(0),
      line(0), tip_flags(0) {
}

// Construct the original tooltip window and return its unretained primary identity.
ScUnknown *UiToolTipWindow::CreateNewObject() {
  return static_cast<WnWindow *>(new UiToolTipWindow);
}

UiToolTipWindow::~UiToolTipWindow() {
  Shutdown();
}

// Publish the tooltip or message-target identity, delegating other queries to the window base.
bool UiToolTipWindow::QueryInterface(u32 iid, void **out) {
  if (iid == IID_UiToolTipControl) {
    if (!out)
      RETURN_QUERY_INTERFACE_FAILURE();
    *out = static_cast<UiToolTipControl *>(this);
    AddRef();
    return true;
  }
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, WnWindow);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 UiToolTipWindow::AddRef() {
  return WnRootWidget::AddRef();
}

u32 UiToolTipWindow::Release() {
  return WnRootWidget::Release();
}

// Create the tooltip's 16-bit offscreen buffer using the current font height and signed vertical margin.
bool UiToolTipWindow::Init() {
  if (GetInitFlag())
    return true;
  GxGraphics *graphics = ApGetGraphics();
  if (!graphics || !font)
    return false;
  HideWindow();
  graphics->CreateBuffer(&offscreen);
  if (!offscreen)
    return false;
  i32 height = font->Height(0);
  height = static_cast<i32>(static_cast<u32>(height) + 2u * static_cast<u32>(static_cast<i32>(vertical)));
  GxColorType color(GxColorType::Color::RGB16);
  GxDepthType depth(GxDepthType::Depth::None);
  offscreen->Init(256, static_cast<u32>(height), color, nullptr, depth, GxVramRequest::SystemMemory, false);
  return WnRootWidget::Init();
}

bool UiToolTipWindow::Shutdown() {
  if (offscreen) {
    if (offscreen->IsReady())
      offscreen->Shutdown();
    offscreen->Release();
    offscreen = nullptr;
  }
  if (font) {
    font->Release();
    font = nullptr;
  }
  return WnRootWidget::Shutdown();
}

// Blit the tooltip's prepared offscreen surface into its current window drawing area.
bool UiToolTipWindow::GZPaint() {
  u8 locked = draw_buffer->Lock(GxLockFlags::Blt);
  if (locked) {
    draw_buffer->Blt(offscreen, source, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
    return true;
  }
  return locked != 0;
}

// Show or hide the tooltip and update its individual colors in response to the original window messages.
bool UiToolTipWindow::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_ShowTipWindow:
    if (UpdateOffscreenBuffer()) {
      SetFlag(kWnFlagReorderable, true);
      PullToFront();
      SetFlag(kWnFlagReorderable, false);
      ShowWindow();
    }
    return true;
  case kWnMsg_HideTipWindow:
    HideWindow();
    return true;
  case kWnMsg_TipForeground:
    if (offscreen) {
      TranslateColor(&foreground, static_cast<u32>(message->data1));
      UpdateOffscreenBuffer();
    }
    return true;
  case kWnMsg_TipBackground:
    if (offscreen) {
      TranslateColor(&background, static_cast<u32>(message->data1));
      UpdateOffscreenBuffer();
    }
    return true;
  case kWnMsg_TipLineColor:
    if (offscreen) {
      TranslateColor(&line, static_cast<u32>(message->data1));
      UpdateOffscreenBuffer();
    }
    return true;
  default:
    return false;
  }
}

bool UiToolTipWindow::IsPointInWindowScreenCoordinates(i32, i32) const {
  return false;
}

bool UiToolTipWindow::IsPointInWindow(i32, i32) const {
  return false;
}

bool UiToolTipWindow::IsPointInWindowParentCoordinates(i32, i32) const {
  return false;
}

// Borrow the root window identity without retaining it.
WnWindow *UiToolTipWindow::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Retain the new font before releasing the old font, then store the two signed margins.
bool UiToolTipWindow::SetFont(FtFont *value, i8 first, i8 second) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  horizontal = first;
  vertical = second;
  return true;
}

// Copy the tip text, placement, flags and colors into the window's retained presentation state.
bool UiToolTipWindow::CopyTipInfo(const UiTipInfo &info) {
  if (info.text) {
    ScString value(*info.text);
    caption = value;
    u32 length = caption.ScString::Strlen();
    const char *bytes = caption.Data();
    u32 fitted = font->Fit(bytes, length, 256);
    if (fitted && fitted != length) {
      WnResources *resources = ApGetUiResources();
      ScString fallback("...");
      const ScStringView *ellipsis = resources ? resources->GetText(kWnEllipsisTextId) : nullptr;
      if (!ellipsis)
        ellipsis = &fallback;
      u32 ellipsis_length = ellipsis->Strlen();
      const char *ellipsis_data = ellipsis->Data();
      i32 ellipsis_width = font->Measure(ellipsis_data, ellipsis_length);
      if (ellipsis_width < 256) {
        fitted = font->Fit(caption.begin, caption.ScString::Strlen(), static_cast<u32>(256 - ellipsis_width));
        if (!fitted || fitted - 1 > caption.ScString::Strlen())
          ScFatal(SC_ERROR_INVALID, "Tooltip truncation exceeds the string range");
        caption.Erase(fitted - 1, caption.ScString::Strlen() - (fitted - 1));
        caption.RTrim();
        ellipsis_length = ellipsis->Strlen();
        ellipsis_data = ellipsis->Data();
        caption.Append(ellipsis_data, ellipsis_length);
      } else {
        caption.Erase(0, caption.ScString::Strlen());
      }
    }
  } else {
    caption.Erase(0, caption.ScString::Strlen());
  }
  for (u32 i = 0; i < 4; ++i)
    coordinates[i] = info.coordinates[i];
  tip_flags = info.flags;
  if (!(tip_flags & kWnTipPlacementMask))
    tip_flags |= kWnTipBelowCenter;
  if (offscreen) {
    TranslateColor(&foreground, info.foreground);
    TranslateColor(&background, info.background);
    TranslateColor(&line, info.line);
  }
  return true;
}

void UiToolTipWindow::TranslateColor(u16 *out, u32 color) {
  offscreen->ConvertRGBValueToNative16Bit(out, static_cast<u8>(color >> 16), static_cast<u8>(color >> 8),
      static_cast<u8>(color));
}

// Draw the tooltip text and border to its offscreen surface, then place the window relative to its anchor.
bool UiToolTipWindow::UpdateOffscreenBuffer() {
  u32 length = caption.Strlen();
  if (!length || !offscreen || !font)
    return false;
  const char *bytes = caption.Data();
  font->TextArea(bytes, length, &source);
  source.right = static_cast<i32>(static_cast<u32>(source.right) + 2u * static_cast<u32>(static_cast<i32>(horizontal)));
  source.bottom = static_cast<i32>(static_cast<u32>(source.bottom) + 2u * static_cast<u32>(static_cast<i32>(vertical)));
  if (source.right > static_cast<i32>(offscreen->Width()))
    source.right = static_cast<i32>(offscreen->Width());
  if (source.bottom > static_cast<i32>(offscreen->Height()))
    source.bottom = static_cast<i32>(offscreen->Height());
  if (offscreen->Lock(GxLockFlags::Fill)) {
    offscreen->Fill(background, source, nullptr);
    offscreen->Unlock(GxLockFlags::Fill);
  }
  font->DrawTextA(offscreen, horizontal, vertical, caption.begin, caption.ScString::Strlen(), foreground);
  if (offscreen->Lock(GxLockFlags::Line)) {
    offscreen->DrawRectangleOutline(source, line, 1);
    offscreen->Unlock(GxLockFlags::Line);
  }
  i32 width = UiTipSubtract(source.right, source.left), height = UiTipSubtract(source.bottom, source.top), x, y;
  i32 left = coordinates[0], top = coordinates[1], right = coordinates[2], bottom = coordinates[3];
  if (tip_flags & kWnTipAtAnchor) {
    x = left;
    y = top;
  } else if (tip_flags & kWnTipLeftTop) {
    x = UiTipSubtract(UiTipSubtract(left, width), 2);
    y = top;
  } else if (tip_flags & kWnTipLeftMiddle) {
    x = UiTipSubtract(UiTipSubtract(left, width), 2);
    y = UiTipAdd(top, UiTipSubtract(UiTipSubtract(bottom, top), height) / 2);
  } else if (tip_flags & kWnTipLeftBottom) {
    x = UiTipSubtract(UiTipSubtract(left, width), 2);
    y = UiTipSubtract(bottom, height);
  } else if (tip_flags & kWnTipRightTop) {
    x = UiTipAdd(right, 2);
    y = top;
  } else if (tip_flags & kWnTipRightMiddle) {
    x = UiTipAdd(right, 2);
    y = UiTipAdd(top, UiTipSubtract(UiTipSubtract(bottom, top), height) / 2);
  } else if (tip_flags & kWnTipRightBottom) {
    x = UiTipAdd(right, 2);
    y = UiTipSubtract(bottom, height);
  } else if (tip_flags & kWnTipAboveLeft) {
    x = left;
    y = UiTipSubtract(UiTipSubtract(top, height), 2);
  } else if (tip_flags & kWnTipAboveCenter) {
    x = UiTipAdd(left, UiTipSubtract(UiTipSubtract(right, left), width) / 2);
    y = UiTipSubtract(UiTipSubtract(top, height), 2);
  } else if (tip_flags & kWnTipAboveRight) {
    x = UiTipSubtract(right, width);
    y = UiTipSubtract(UiTipSubtract(top, height), 2);
  } else {
    if (tip_flags & kWnTipBelowLeft)
      x = left;
    else if ((tip_flags & kWnTipBelowCenter) || !(tip_flags & kWnTipBelowRight))
      x = UiTipAdd(left, UiTipSubtract(UiTipSubtract(right, left), width) / 2);
    else
      x = UiTipSubtract(right, width);
    y = UiTipAdd(bottom, 2);
  }
  if (x < 2)
    x = 2;
  if (UiTipAdd(x, width) > UiTipSubtract(GetMainWindow()->GetR(), 2))
    x = UiTipSubtract(UiTipSubtract(GetMainWindow()->GetR(), width), 2);
  if (y < 2)
    y = 2;
  if (UiTipAdd(y, height) > UiTipSubtract(GetMainWindow()->GetB(), 2))
    y = UiTipSubtract(UiTipSubtract(GetMainWindow()->GetB(), height), 2);
  SetArea(x, y, UiTipAdd(x, width), UiTipAdd(y, height));
  return true;
}

UiToolTipController::UiToolTipController()
    : references(0), initialized(false), start_ticks(0), stop_ticks(0), elapsed(0), visible(false),
      next_identifier(0), current_identifier(UINT32_MAX) {
}

UiToolTipController::~UiToolTipController() {
}

// Publish the unknown or tooltip-manager identity only when the caller supplies an output pointer.
bool UiToolTipController::QueryInterface(u32 iid, void **out) {
  if ((iid != IID_ScUnknown && iid != IID_UiToolTipManager) || !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiToolTipManager *>(this);
  AddRef();
  return true;
}

u32 UiToolTipController::AddRef() {
  references = static_cast<i32>(static_cast<u32>(references) + 1);
  return static_cast<u32>(references);
}

// Delete the manager when its decremented signed count is not positive.
u32 UiToolTipController::Release() {
  references = static_cast<i32>(static_cast<u32>(references) - 1);
  if (references <= 0) {
    delete this;
    return 0;
  }
  return static_cast<u32>(references);
}

// Subscribe to tip messages, create the tooltip child and configure the original 750 ms callback timer.
bool UiToolTipController::Init(FtFont *font, i8 horizontal, i8 vertical) {
  if (initialized)
    return true;
  if (!font)
    return false;
  WnWindowManager *manager = ApGetDialogManager();
  if (!manager)
    return false;
  WnWindow *root = manager->GetMainWindow();
  if (!root)
    return false;
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    server->AddNotification(this, kWnMsg_HintTimer);
    server->AddNotification(this, kWnMsg_StartHint);
    server->AddNotification(this, kWnMsg_UpdateHint);
    server->AddNotification(this, kWnMsg_StopHint);
    server->AddNotification(this, kWnMsg_HintForeground);
    server->AddNotification(this, kWnMsg_HintBackground);
    server->AddNotification(this, kWnMsg_CancelHint);
  }
  UiToolTipWindow *window = new UiToolTipWindow;
  UiToolTipControl *tip = static_cast<UiToolTipControl *>(window);
  tip->AddRef();
  tip->SetFont(font, horizontal, vertical);
  WnWindow *child = tip->AsIGZWin();
  child->SetID(kUiToolTipWindowId);
  child->HideWindow();
  bool added = root->ChildAdd(child);
  tip->Release();
  if (!added) {
    Shutdown();
    return false;
  }
  timer.SetPeriod(750);
  timer.SetResolution(750);
  timer.SetCallbackFunction(StaticTipTimerCallback, 0);
  initialized = true;
  return true;
}

// Stop the timer, remove message subscriptions and delete the registered tooltip window.
bool UiToolTipController::Shutdown() {
  timer.Stop();
  ScMessageServer *server = ApGetMessageServer();
  if (server) {
    server->RemoveNotification(this, kWnMsg_HintTimer);
    server->RemoveNotification(this, kWnMsg_StartHint);
    server->RemoveNotification(this, kWnMsg_StopHint);
    server->RemoveNotification(this, kWnMsg_UpdateHint);
    server->RemoveNotification(this, kWnMsg_HintForeground);
    server->RemoveNotification(this, kWnMsg_HintBackground);
    server->RemoveNotification(this, kWnMsg_CancelHint);
  }
  WnWindowManager *manager = ApGetDialogManager();
  if (manager) {
    WnWindow *root = manager->GetMainWindow();
    if (root) {
      WnWindow *window = root->GetChildWindowFromID(kUiToolTipWindowId);
      if (window)
        root->ChildDelete(window);
    }
  }
  initialized = false;
  return true;
}

// Dispatch the original tooltip notifications and consume each recognized message.
bool UiToolTipController::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_StartHint:
    OnTipStart(message);
    return true;
  case kWnMsg_UpdateHint:
    OnTipUpdate(*message);
    return true;
  case kWnMsg_CancelHint:
    message->data1 = kUiAnyTipId;
    OnTipStop(*message);
    return true;
  case kWnMsg_StopHint:
    OnTipStop(*message);
    return true;
  case kWnMsg_HintForeground:
    OnTipForecolor(*message);
    return true;
  case kWnMsg_HintBackground:
    OnTipBackcolor(*message);
    return true;
  case kWnMsg_HintTimer:
    OnTimerTicked(*message);
    return true;
  default:
    return false;
  }
}

// Query the registered tooltip child and return its borrowed interface only when its window remains valid.
UiToolTipControl *UiToolTipController::GetTipWindow() {
  WnWindowManager *manager = ApGetDialogManager();
  if (!manager)
    return nullptr;
  WnWindow *root = manager->GetMainWindow();
  if (!root)
    return nullptr;
  UiToolTipControl *tip = nullptr;
  if (!root->GetChildAs(kUiToolTipWindowId, IID_UiToolTipControl, reinterpret_cast<void **>(&tip)))
    return nullptr;
  if (!manager->IsWindowValid(tip->AsIGZWin()))
    return nullptr;
  tip->Release();
  return tip;
}

// Post the requested message to the currently registered tooltip window.
bool UiToolTipController::PostMsgToTipWindow(u32 type, u32 first, u32 second) {
  UiToolTipControl *tip = GetTipWindow();
  if (!tip)
    return false;
  WnWindowManager *manager = ApGetDialogManager();
  if (!manager)
    return false;
  manager->PostMessageToTarget(tip->AsIGZWin(), type, first, second, 0);
  return true;
}

bool UiToolTipController::ShowTip(u32 identifier) {
  PostMsgToTipWindow(kWnMsg_ShowTipWindow, 0, 0);
  visible = true;
  current_identifier = identifier;
  return true;
}

bool UiToolTipController::HideTip(u32 identifier) {
  if (identifier != kUiAnyTipId && current_identifier != identifier)
    return false;
  current_identifier = UINT32_MAX;
  timer.Stop();
  visible = false;
  return PostMsgToTipWindow(kWnMsg_HideTipWindow, 0, 0);
}

bool UiToolTipController::OnTipStart(ScMessage *message) {
  UiTipInfo *info = reinterpret_cast<UiTipInfo *>(message->data1);
  if (!info || !ApGetDialogManager())
    return false;
  UiToolTipControl *tip = GetTipWindow();
  if (!tip)
    return false;
  HideTip(current_identifier);
  tip->CopyTipInfo(*info);
  info->identifier = ++next_identifier;
  current_identifier = info->identifier;
  if (info->flags & 1)
    ShowTip(info->identifier);
  if (info->flags & 2) {
    if (!visible)
      ShowTip(info->identifier);
    timer.Stop();
  } else {
    elapsed = 0;
    start_ticks = 1;
    stop_ticks = 3;
    timer.Stop();
    timer.StartPeriodic(0, nullptr);
  }
  return true;
}

bool UiToolTipController::OnTipUpdate(const ScMessage &message) {
  UiTipInfo *info = reinterpret_cast<UiTipInfo *>(message.data1);
  if (!visible || !info)
    return false;
  UiToolTipControl *tip = GetTipWindow();
  if (!tip)
    return false;
  tip->CopyTipInfo(*info);
  return ShowTip(info->identifier);
}

bool UiToolTipController::OnTipStop(const ScMessage &message) {
  return HideTip(static_cast<u32>(message.data1));
}

bool UiToolTipController::OnTipForecolor(const ScMessage &message) {
  u32 value = static_cast<u32>(message.data1);
  return value == current_identifier && PostMsgToTipWindow(kWnMsg_TipForeground, value, 0);
}

bool UiToolTipController::OnTipBackcolor(const ScMessage &message) {
  u32 value = static_cast<u32>(message.data1);
  return value == current_identifier && PostMsgToTipWindow(kWnMsg_TipBackground, value, 0);
}

bool UiToolTipController::OnTipLinecolor(const ScMessage &message) {
  u32 value = static_cast<u32>(message.data1);
  return value == current_identifier && PostMsgToTipWindow(kWnMsg_TipLineColor, value, 0);
}

// Advance the delay and post the corresponding show or stop notification when its threshold is reached.
bool UiToolTipController::OnTimerTicked(const ScMessage &) {
  ScMessageServer *server = ApGetMessageServer();
  if (!server)
    return false;
  ++elapsed;
  if (visible) {
    if (elapsed >= stop_ticks) {
      HideTip(current_identifier);
      ScMessage message(kWnMsg_HintHidden, current_identifier, 0, 0);
      server->MessagePost(message, false);
    }
  } else if (elapsed >= start_ticks) {
    ShowTip(current_identifier);
    ScMessage message(kWnMsg_HintShown, current_identifier, 0, 0);
    server->MessagePost(message, false);
  }
  return true;
}

void UiToolTipController::StaticTipTimerCallback(uptr) {
  static ScMessage message(kWnMsg_HintTimer);
  ScMessageServer *server = ApGetMessageServer();
  if (server)
    server->MessagePost(message, false);
}
