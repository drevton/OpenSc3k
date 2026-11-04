#include "ui/graph_series.h"

#include <assert.h>
#include <math.h>

#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "ui/api.h"
#include "ui/graph_window.h"

// Pie rendering retains its disk fill word, initial maximum-coordinate sentinel and skipped-extrusion marker.
enum UiPieRasterConstant : u32 {
  kUiPieDiskFillPattern = 0xeeeeeeee,
  kUiPieInitialMaximumCoordinate = 0x80000001,
  kUiPieSkipExtrusion = 0xffffffff,
};

// Insert one value into a line's backing range, doubling its allocation when it is full.
static void UiBarGraphInsertValue(UiGraphFloatVector *series, u32 index, f32 value) {
  u32 size = series->begin ? (u32)(series->end - series->begin) : 0;
  u32 capacity = series->begin ? (u32)(series->storage_end - series->begin) : 0;
  if (size == capacity) {
    if (size > INT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Bar graph data exceeds its storage range");
    u32 next = size ? size * 2 : 1;
    f32 *storage = new f32[next];
    for (u32 i = 0; i < index; ++i)
      storage[i] = series->begin[i];
    storage[index] = value;
    for (u32 i = index; i < size; ++i)
      storage[i + 1] = series->begin[i];
    delete[] series->begin;
    series->begin = storage;
    series->end = storage + size + 1;
    series->storage_end = storage + next;
  } else {
    for (u32 i = size; i > index; --i)
      series->begin[i] = series->begin[i - 1];
    series->begin[index] = value;
    ++series->end;
  }
}

// Insert a data point in one series; the all-ones series selects the sole series when exactly one exists.
void UiBarGraphWindow::InsertData(f32 value, u32 series, u32 index) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *values = &series_begin[series];
    u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
    if (index == kUiAppendGraphValue)
      index = size;
    assert(index <= size);
    UiBarGraphInsertValue(values, index, value);
  }
}

// Replace one point or append it when the index is the all-ones sentinel.
void UiBarGraphWindow::SetData(f32 value, u32 index, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *values = &series_begin[series];
    if (index == kUiAppendGraphValue) {
      u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
      UiBarGraphInsertValue(values, size, value);
    } else {
      [[maybe_unused]] u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
      assert(index < size);
      values->begin[index] = value;
    }
  }
}

// Replace a series with the supplied vector, preserving storage when its capacity is sufficient.
void UiBarGraphWindow::SetData(UiGraphFloatVector *values, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series >= count || values == &series_begin[series])
    return;
  UiGraphFloatVector *target = &series_begin[series];
  u32 length = values->begin ? (u32)(values->end - values->begin) : 0;
  u32 capacity = target->begin ? (u32)(target->storage_end - target->begin) : 0;
  if (length > capacity) {
    f32 *storage = new f32[length];
    for (u32 i = 0; i < length; ++i)
      storage[i] = values->begin[i];
    delete[] target->begin;
    target->begin = storage;
    target->storage_end = storage + length;
  } else {
    for (u32 i = 0; i < length; ++i)
      target->begin[i] = values->begin[i];
  }
  target->end = target->begin ? target->begin + length : nullptr;
}

// Append the supplied values in order to the selected series.
void UiBarGraphWindow::AppendData(UiGraphFloatVector *values, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *target = &series_begin[series];
    for (u32 i = 0; values->begin && i < (u32)(values->end - values->begin); ++i) {
      u32 size = target->begin ? (u32)(target->end - target->begin) : 0;
      UiBarGraphInsertValue(target, size, values->begin[i]);
    }
  }
}

// Set a category label, expanding the label range with empty strings through the requested index.
void UiBarGraphWindow::InsertCategoryName(const ScString &name, u32 index) {
  u32 count = category_begin ? (u32)(category_end - category_begin) : 0;
  if (index == kUiAppendGraphCategory)
    index = count;
  if (index >= count) {
    if (index == UINT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Bar graph categories exceed their storage range");
    u32 length = index + 1, capacity = category_begin ? (u32)(category_storage_end - category_begin) : 0;
    if (length > capacity) {
      u32 added = length - count, growth = count > added ? count : added;
      if (growth > UINT32_MAX - count)
        ScFatal(SC_ERROR_INVALID, "Bar graph categories exceed their storage range");
      capacity = count + growth;
      ScString *storage = new ScString[capacity];
      for (u32 i = 0; i < count; ++i)
        storage[i] = category_begin[i];
      delete[] category_begin;
      category_begin = storage;
      category_storage_end = storage + capacity;
    }
    category_end = category_begin + length;
  }
  category_begin[index] = name;
}

static i32 UiBarDrawCoordinate(f64 value) {
  assert(value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return (i32)(u32)(i64)value;
}

// Draw grouped or stacked bar segments in category order using the original virtual drawing calls.
void UiBarGraphWindow::DrawAllBars(GxBuffer *buffer) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  i32 type = (i32)graph_type;
  if (!count || (type != 0 && type != 1))
    return;
  u32 categories = 0;
  if (type == 1) {
    categories = series_begin[0].begin ? (u32)(series_begin[0].end - series_begin[0].begin) : 0;
  } else {
    for (u32 i = 0; i < count; ++i) {
      u32 length = series_begin[i].begin ? (u32)(series_begin[i].end - series_begin[i].begin) : 0;
      if (length > categories)
        categories = length;
    }
  }
  UiGraphAxis value_axis = static_cast<UiGraphAxis>((i32)orientation == 0);
  i32 zero = axis_zero_position[(i32)orientation == 0];
  f64 initial = (f64)graph_area.left + bar_spacing;
  f32 left = (f32)initial, right = (f32)(initial + bar_width);
  for (u32 category = 0; category < categories; ++category) {
    i32 positive = zero, negative = zero;
    for (u32 series = 0; series < count; ++series) {
      UiGraphFloatVector *values = &series_begin[series];
      u32 length = values->begin ? (u32)(values->end - values->begin) : 0;
      assert(type != 1 || category < length);
      if (category < length) {
        f32 value = values->begin[category];
        assert(value == value);
        if (value != 0.0f) {
          i32 top, bottom, direction;
          if (type == 1) {
            i32 pixel = ValueToPixel(value_axis, value, true);
            direction = value > 0.0f ? 0 : 1;
            top = direction ? zero : pixel;
            bottom = direction ? pixel : zero;
          } else {
            i32 pixels = ValueAmountToPixelAmount(value_axis, value);
            direction = value > 0.0f ? 0 : 1;
            if (direction) {
              top = negative;
              bottom = (i32)((u32)negative + (u32)pixels);
              negative = UiBarDrawCoordinate((f32)bottom);
            } else {
              bottom = positive;
              top = (i32)((u32)positive - (u32)pixels);
              positive = UiBarDrawCoordinate((f32)top);
            }
          }
          ScRect rectangle = {UiBarDrawCoordinate(left), UiBarDrawCoordinate((f32)top), UiBarDrawCoordinate(right),
              UiBarDrawCoordinate((f32)bottom)};
          DrawBar(buffer, &rectangle, series, static_cast<UiBarDirection>(direction));
        }
      }
      if (type == 1) {
        left = (f32)((f64)left + bar_width);
        right = (f32)((f64)right + bar_width);
      }
    }
    if (type == 1) {
      left = (f32)((f64)left + bar_spacing);
      right = (f32)((f64)right + bar_spacing);
    } else {
      left = (f32)((f64)left + ((f64)bar_spacing + bar_width));
      right = (f32)((f64)left + bar_width);
    }
  }
}

// Find the highest individual value or positive category total, depending on the bar grouping.
f32 UiBarGraphWindow::GetHighestAxisValue(UiGraphAxis) {
  f32 result = -3.4028234663852886e+38f;
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if ((i32)graph_type != 0) {
    for (u32 i = 0; i < count; ++i)
      for (f32 *value = series_begin[i].begin; value != series_begin[i].end; ++value)
        if (*value > result)
          result = *value;
    return result;
  }
  if (!count)
    return 100.0f;
  u32 categories = 0;
  for (u32 i = 0; i < count; ++i) {
    u32 length = series_begin[i].begin ? (u32)(series_begin[i].end - series_begin[i].begin) : 0;
    if (length > categories)
      categories = length;
  }
  for (u32 category = 0; category < categories; ++category) {
    f64 sum = 0.0;
    for (u32 i = 0; i < count; ++i) {
      u32 length = series_begin[i].begin ? (u32)(series_begin[i].end - series_begin[i].begin) : 0;
      if (category < length && series_begin[i].begin[category] > 0.0f)
        sum += series_begin[i].begin[category];
    }
    if (sum > result)
      result = (f32)sum;
  }
  return result;
}

// Find the lowest individual value or negative category total, depending on the bar grouping.
f32 UiBarGraphWindow::GetLowestAxisValue(UiGraphAxis) {
  f32 result = 3.4028234663852886e+38f;
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if ((i32)graph_type != 0) {
    for (u32 i = 0; i < count; ++i)
      for (f32 *value = series_begin[i].begin; value != series_begin[i].end; ++value)
        if (*value < result || *value != *value || result != result)
          result = *value;
    return result;
  }
  if (!count)
    return 100.0f;
  u32 categories = 0;
  for (u32 i = 0; i < count; ++i) {
    u32 length = series_begin[i].begin ? (u32)(series_begin[i].end - series_begin[i].begin) : 0;
    if (length > categories)
      categories = length;
  }
  for (u32 category = 0; category < categories; ++category) {
    f64 sum = 0.0;
    for (u32 i = 0; i < count; ++i) {
      u32 length = series_begin[i].begin ? (u32)(series_begin[i].end - series_begin[i].begin) : 0;
      if (category < length) {
        f32 value = series_begin[i].begin[category];
        if (value < 0.0f || value != value)
          sum += value;
      }
    }
    if (sum < result || sum != sum || result != result)
      result = (f32)sum;
  }
  return result;
}

UiBarGraphSeriesInfo::UiBarGraphSeriesInfo()
    : outline(1), outline_color(0), fill(1), fill_color(kUiGraphDefaultFillColor), pattern(nullptr) {
}

// Copy the drawing flags, colors and raw pattern before assigning the owned label.
UiBarGraphSeriesInfo &UiBarGraphSeriesInfo::operator=(const UiBarGraphSeriesInfo &source) {
  outline = source.outline;
  outline_color = source.outline_color;
  fill = source.fill;
  fill_color = source.fill_color;
  pattern = source.pattern;
  name = source.name;
  return *this;
}

// Start with empty bar collections, horizontal orientation and the original width defaults.
UiBarGraphWindow::UiBarGraphWindow()
    : graph_type(static_cast<UiBarGraphType>(0)), orientation(static_cast<UiBarOrientation>(0)), series_begin(nullptr),
      series_end(nullptr), series_storage_end(nullptr), info_begin(nullptr), info_end(nullptr),
      info_storage_end(nullptr), category_begin(nullptr), category_end(nullptr), category_storage_end(nullptr),
      bar_width(1.0f), bar_spacing(1.0f), bar_width_percentage(0.75f) {
  major_tick_usage[0] = 0;
  major_tick_usage[1] = 1;
  major_tick_grid[0] = 0;
  major_tick_grid[1] = 1;
  tick_text[0] = 1;
}

// Shut down the graph and destroy its category, bar-setting and data collections.
UiBarGraphWindow::~UiBarGraphWindow() {
  UiGraphWindow::Shutdown();
  delete[] category_begin;
  delete[] info_begin;
  for (UiGraphFloatVector *series = series_begin; series != series_end; ++series)
    delete[] series->begin;
  delete[] series_begin;
}

bool UiBarGraphWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnBarGraphView, UiBarGraph);
  return UiGraphWindow::QueryInterface(iid, out);
}

u32 UiBarGraphWindow::AddRef() {
  return UiGraphWindow::AddRef();
}

u32 UiBarGraphWindow::Release() {
  return UiGraphWindow::Release();
}

void UiBarGraphWindow::SetBarGraphType(UiBarGraphType value) {
  graph_type = value;
}

void UiBarGraphWindow::SetGraphOrientation(UiBarOrientation value) {
  orientation = value;
}

// Copy one label, or all labels when the original all-series sentinel is supplied.
void UiBarGraphWindow::SetSeriesName(const ScString &value, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].name = value;
}

// Copy the supplied settings into an existing bar series.
void UiBarGraphWindow::SetSeriesInfo(UiBarGraphSeriesInfo *value, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index < count)
    info_begin[index] = *value;
}

// Clear one category label; the all-ones selector refers to the last label.
void UiBarGraphWindow::RemoveCategoryName(u32 index) {
  u32 count = category_begin ? (u32)(category_end - category_begin) : 0;
  if (index == kUiLastGraphCategory)
    index = count - 1;
  if (index < count)
    category_begin[index].Erase(0, category_begin[index].Strlen());
}

void UiBarGraphWindow::SetBarColorUse(UiGraphUsage outline, UiGraphUsage fill, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i) {
    if (index == kUiAllGraphSeries || index == i) {
      info_begin[i].outline = (i32)outline != 0;
      info_begin[i].fill = (i32)fill != 0;
    }
  }
}

void UiBarGraphWindow::SetBarFillColor(u32 color, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].fill_color = color;
}

void UiBarGraphWindow::SetBarOutlineColor(u32 color, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].outline_color = color;
}

// Retain the replacement pattern before releasing the selected series' previous pattern.
void UiBarGraphWindow::SetBarPattern(GxBuffer *pattern, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index < count) {
    if (pattern)
      pattern->AddRef();
    if (info_begin[index].pattern)
      info_begin[index].pattern->Release();
    info_begin[index].pattern = pattern;
  }
}

// Clamp the width fraction to the original one-tenth through one range.
void UiBarGraphWindow::SetBarWidthPercentage(f32 value) {
  if (!(value >= 0.1f))
    value = 0.1f;
  else if (value > 1.0f)
    value = 1.0f;
  bar_width_percentage = value;
}

// Remove the specified point from one series, resolving the sole-series sentinel when applicable.
void UiBarGraphWindow::RemoveData(u32 index, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *values = &series_begin[series];
    f32 *position = values->begin + index;
    for (f32 *next = position + 1; next != values->end; ++next)
      next[-1] = *next;
    --values->end;
  }
}

static i32 UiBarGraphTruncate(f64 value) {
  assert(value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return (i32)(u32)(i64)value;
}

// Compute the near edge from the category spacing and, for grouped bars, the series offset.
i32 UiBarGraphWindow::GetLeftOfBar(u32 category, u32 series) {
  if (series_begin == series_end || ((i32)graph_type != 0 && (i32)graph_type != 1))
    return UiBarGraphTruncate(bar_spacing);
  u32 count = (u32)(series_end - series_begin);
  f64 position = (f64)(category + 1) * bar_spacing;
  if ((i32)graph_type == 1)
    position += (f64)count * (f64)category * bar_width + (f64)series * bar_width;
  else
    position += (f64)category * bar_width;
  position += (i32)orientation ? graph_area.top : graph_area.left;
  return UiBarGraphTruncate(position);
}

i32 UiBarGraphWindow::GetRightOfBar(u32 category, u32 series) {
  return UiBarGraphTruncate((f64)GetLeftOfBar(category, series) + (f64)bar_width);
}

i32 UiBarGraphWindow::GetCenterOfBar(u32 category, u32 series) {
  return UiBarGraphTruncate((f64)GetLeftOfBar(category, series) + (f64)bar_width * 0.5);
}

// Return the selected bar's bounds, accumulating earlier positive and negative values for stacked bars.
bool UiBarGraphWindow::GetRectOfBar(u32 category, u32 series, ScRect *out) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series >= count ||
      category >= (series_begin[series].begin ? (u32)(series_begin[series].end - series_begin[series].begin) : 0)) {
    *out = {};
    return false;
  }
  bool horizontal = (i32)orientation != 0;
  UiGraphAxis axis = static_cast<UiGraphAxis>(!horizontal);
  i32 first = GetLeftOfBar(category, series);
  i32 last = UiBarGraphTruncate((f64)first + bar_width);
  u32 low = category, high = category;
  u32 zero = (u32)axis_zero_position[!horizontal];
  if ((i32)graph_type == 1) {
    f32 value = series_begin[series].begin[category];
    u32 pixels = (u32)ValueAmountToPixelAmount(axis, value);
    if (value <= 0.0f) {
      low = zero;
      high = zero + pixels;
    } else {
      high = zero;
      low = zero - pixels;
    }
  } else if ((i32)graph_type == 0) {
    u32 positive = zero, negative = zero;
    for (u32 i = 0; i <= category; ++i) {
      f32 value = series_begin[series].begin[i];
      if (value > 0.0f) {
        high = positive;
        low = positive - (u32)ValueAmountToPixelAmount(axis, value);
        positive = low;
      } else if (value < 0.0f) {
        low = negative;
        high = negative + (u32)ValueAmountToPixelAmount(axis, value);
        negative = high;
      }
    }
  }
  if (horizontal)
    *out = {(i32)low, first, (i32)high, last};
  else
    *out = {first, (i32)low, last, (i32)high};
  return true;
}

// Combine the selected category's bar rectangles, starting with the first series.
bool UiBarGraphWindow::GetRectOfBarsInCategory(u32 category, ScRect *out) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (!count || category >= (series_begin[0].begin ? (u32)(series_begin[0].end - series_begin[0].begin) : 0)) {
    *out = {};
    return false;
  }
  GetRectOfBar(category, 0, out);
  for (u32 i = 1; i < count; ++i) {
    ScRect next;
    GetRectOfBar(category, i, &next);
    if (next.left < out->left)
      out->left = next.left;
    if (next.top < out->top)
      out->top = next.top;
    if (next.right > out->right)
      out->right = next.right;
    if (next.bottom > out->bottom)
      out->bottom = next.bottom;
  }
  return true;
}

bool UiBarGraphWindow::DoHitTest(ScPoint *point, u32 *out) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  for (u32 series = 0; series < count; ++series) {
    u32 length = series_begin[series].begin ? (u32)(series_begin[series].end - series_begin[series].begin) : 0;
    for (u32 category = 0; category < length; ++category) {
      ScRect rectangle;
      GetRectOfBar(category, series, &rectangle);
      if (point->x >= rectangle.left && point->y >= rectangle.top && point->x <= rectangle.right &&
          point->y <= rectangle.bottom) {
        AllocateHitTestData(12);
        u32 *data = (u32 *)hit_test_result;
        data[1] = series;
        data[2] = category;
        *out = 1001;
        return true;
      }
    }
  }
  return UiGraphWindow::DoHitTest(point, out);
}

// Recalculate the bar layout before the shared graph redraw when a drawing buffer exists.
void UiBarGraphWindow::CompleteRedraw(GxBuffer *buffer) {
  if (!buffer)
    buffer = draw_buffer;
  if (buffer) {
    CalculateBarWidthAndSpacing();
    UiGraphWindow::CompleteRedraw(buffer);
  }
}

void UiBarGraphWindow::DrawGraphData(GxBuffer *buffer) {
  DrawAllBars(buffer);
}

// Tile a series pattern when present, otherwise draw its enabled fill and outline passes.
void UiBarGraphWindow::DrawBar(GxBuffer *buffer, ScRect *rectangle, u32 series, UiBarDirection) {
  assert(info_begin && series < (u32)(info_end - info_begin));
  UiBarGraphSeriesInfo *info = &info_begin[series];
  if (info->pattern) {
    if (buffer->Lock(GxLockFlags::Blt)) {
      ScRect source = {0, 0, (i32)info->pattern->Width(), (i32)info->pattern->Height()};
      buffer->BltTiled(info->pattern, source, *rectangle, 0, 0, nullptr);
      buffer->Unlock(GxLockFlags::Blt);
    }
  } else {
    if (info->fill && buffer->Lock(GxLockFlags::Fill)) {
      buffer->Fill(info->fill_color, *rectangle, nullptr);
      buffer->Unlock(GxLockFlags::Fill);
    }
    if (info->outline && buffer->Lock(GxLockFlags::Rect)) {
      buffer->DrawRectangleOutline(*rectangle, info->outline_color, 1);
      buffer->Unlock(GxLockFlags::Rect);
    }
  }
}

u32 UiBarGraphWindow::CalculateMaximumBarWidth() {
  i32 extent = (i32)((u32)graph_area.right - (u32)graph_area.left);
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  f64 result = (f64)extent * (f64)bar_width_percentage;
  if ((i32)graph_type == 1 && count)
    result /= (f64)(i32)count;
  return (u32)UiBarGraphTruncate(result);
}

// Recalculate bar width and spacing from graph size, grouping type and the first series' category count.
void UiBarGraphWindow::CalculateBarWidthAndSpacing() {
  u32 extent =
      (i32)orientation ? (u32)graph_area.bottom - (u32)graph_area.top : (u32)graph_area.right - (u32)graph_area.left;
  i32 type = (i32)graph_type;
  if (type != 0 && type != 1)
    return;
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  u32 categories = count && series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
  if (!categories) {
    categories = 1;
    bar_width = (f32)CalculateMaximumBarWidth();
  } else {
    i32 divisor = type == 1 ? (i32)(categories * count) : (i32)categories;
    bar_width = (f32)((f64)extent * (f64)bar_width_percentage / (f64)divisor);
  }
  f64 occupied = (f64)categories * (f64)bar_width;
  if (type == 1)
    occupied *= (f64)count;
  bar_spacing = (f32)(((f64)extent - occupied) / (f64)(i32)(categories + 1));
}

// The installed module constructs eight default series records once and reuses them for count changes.
static UiBarGraphSeriesInfo UiBarGraphDefaults[8];

// Copy one data range, preserving its destination allocation when it has sufficient capacity.
static void UiBarGraphAssignSeries(UiGraphFloatVector *target, const UiGraphFloatVector *source) {
  u32 size = source->begin ? (u32)(source->end - source->begin) : 0;
  u32 capacity = target->begin ? (u32)(target->storage_end - target->begin) : 0;
  if (size > capacity) {
    f32 *storage = new f32[size];
    for (u32 i = 0; i < size; ++i)
      storage[i] = source->begin[i];
    delete[] target->begin;
    target->begin = storage;
    target->storage_end = storage + size;
  } else {
    for (u32 i = 0; i < size; ++i)
      target->begin[i] = source->begin[i];
  }
  target->end = target->begin ? target->begin + size : nullptr;
}

// Grow the bar-data range by copying each existing series into independently owned storage.
static void UiBarGraphReserveSeries(
    UiGraphFloatVector **begin, UiGraphFloatVector **end, UiGraphFloatVector **storage_end, u32 required) {
  u32 count = *begin ? (u32)(*end - *begin) : 0;
  u32 capacity = *begin ? (u32)(*storage_end - *begin) : 0;
  if (required <= capacity)
    return;
  u32 added = required - count, growth = count > added ? count : added;
  if (growth > UINT32_MAX - count)
    ScFatal(SC_ERROR_INVALID, "Bar graph series exceed their storage range");
  capacity = count + growth;
  UiGraphFloatVector *storage = new UiGraphFloatVector[capacity]{};
  for (u32 i = 0; i < count; ++i)
    UiBarGraphAssignSeries(&storage[i], &(*begin)[i]);
  for (u32 i = 0; i < count; ++i)
    delete[] (*begin)[i].begin;
  delete[] *begin;
  *begin = storage;
  *end = storage + count;
  *storage_end = storage + capacity;
}

// Grow the bar-info range and copy the existing owned labels into its new storage.
static void UiBarGraphReserveInfo(
    UiBarGraphSeriesInfo **begin, UiBarGraphSeriesInfo **end, UiBarGraphSeriesInfo **storage_end, u32 required) {
  u32 count = *begin ? (u32)(*end - *begin) : 0;
  u32 capacity = *begin ? (u32)(*storage_end - *begin) : 0;
  if (required <= capacity)
    return;
  u32 added = required - count, growth = count > added ? count : added;
  if (growth > UINT32_MAX - count)
    ScFatal(SC_ERROR_INVALID, "Bar graph series information exceeds its storage range");
  capacity = count + growth;
  UiBarGraphSeriesInfo *storage = new UiBarGraphSeriesInfo[capacity];
  for (u32 i = 0; i < count; ++i)
    storage[i] = (*begin)[i];
  delete[] *begin;
  *begin = storage;
  *end = storage + count;
  *storage_end = storage + capacity;
}

// Resize both bar collections and restore the initialized default information for each bar series.
void UiBarGraphWindow::SetSeriesCount(u32 count) {
  assert(count <= 8);
  u32 old_count = series_begin ? (u32)(series_end - series_begin) : 0;
  UiBarGraphReserveSeries(&series_begin, &series_end, &series_storage_end, count);
  for (u32 i = count; i < old_count; ++i) {
    delete[] series_begin[i].begin;
    series_begin[i] = {};
  }
  for (u32 i = old_count; i < count; ++i)
    series_begin[i] = {};
  series_end = series_begin ? series_begin + count : nullptr;
  {
    UiBarGraphSeriesInfo defaults;
    UiBarGraphReserveInfo(&info_begin, &info_end, &info_storage_end, count);
  }
  u32 old_info_count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = count; i < old_info_count; ++i)
    info_begin[i] = UiBarGraphDefaults[0];
  for (u32 i = 0; i < count; ++i)
    info_begin[i] = UiBarGraphDefaults[i];
  info_end = info_begin ? info_begin + count : nullptr;
}

// Insert an empty data series and its default settings, or append both for the all-ones index.
void UiBarGraphWindow::InsertSeries(u32 index) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (index == kUiAppendGraphSeries)
    index = count;
  else if (index >= count)
    return;
  if (count == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "Bar graph series exceed their storage range");
  u32 capacity = series_begin ? (u32)(series_storage_end - series_begin) : 0;
  if (count == capacity) {
    if (count > INT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Bar graph series exceed their storage range");
    u32 next = count ? count * 2 : 1;
    UiGraphFloatVector *storage = new UiGraphFloatVector[next]{};
    for (u32 i = 0; i < index; ++i)
      UiBarGraphAssignSeries(&storage[i], &series_begin[i]);
    for (u32 i = index; i < count; ++i)
      UiBarGraphAssignSeries(&storage[i + 1], &series_begin[i]);
    for (u32 i = 0; i < count; ++i)
      delete[] series_begin[i].begin;
    delete[] series_begin;
    series_begin = storage;
    series_end = storage + count;
    series_storage_end = storage + next;
  } else if (index < count) {
    UiBarGraphAssignSeries(&series_begin[count], &series_begin[count - 1]);
    for (u32 i = count - 1; i > index; --i)
      UiBarGraphAssignSeries(&series_begin[i], &series_begin[i - 1]);
    series_begin[index].end = series_begin[index].begin;
  } else {
    series_begin[index] = {};
  }
  ++series_end;
  UiBarGraphSeriesInfo defaults;
  u32 info_count = info_begin ? (u32)(info_end - info_begin) : 0;
  assert(index <= info_count);
  UiBarGraphWindow::InsertSeriesInfo(&defaults, index == info_count ? kUiAllGraphSeries : index);
}

// Erase a data series and its corresponding settings, or remove the last pair for the all-ones index.
void UiBarGraphWindow::RemoveSeries(u32 index) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (index == kUiLastGraphSeries)
    index = count - 1;
  else if (index >= count)
    return;
  for (u32 i = index + 1; i < count; ++i)
    UiBarGraphAssignSeries(&series_begin[i - 1], &series_begin[i]);
  --series_end;
  delete[] series_end->begin;
  *series_end = {};
  UiBarGraphWindow::RemoveSeriesInfo(index);
}

// Insert a copy of the supplied series settings before an existing entry or append it.
void UiBarGraphWindow::InsertSeriesInfo(UiBarGraphSeriesInfo *info, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index == kUiAppendGraphSeries)
    index = count;
  else if (index >= count)
    return;
  if (count == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "Bar graph series information exceeds its storage range");
  UiBarGraphSeriesInfo copy = *info;
  UiBarGraphReserveInfo(&info_begin, &info_end, &info_storage_end, count + 1);
  for (u32 i = count; i > index; --i)
    info_begin[i] = info_begin[i - 1];
  info_begin[index] = copy;
  ++info_end;
}

// Erase one settings record and preserve the order of the following records.
void UiBarGraphWindow::RemoveSeriesInfo(u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index == kUiLastGraphSeries)
    index = count - 1;
  else if (index >= count)
    return;
  for (u32 i = index + 1; i < count; ++i)
    info_begin[i - 1] = info_begin[i];
  --info_end;
  UiBarGraphSeriesInfo defaults;
  *info_end = defaults;
}

static i32 UiBarTickCoordinate(f64 value) {
  assert(value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return (i32)(u32)(i64)value;
}

// Draw numeric value ticks and category ticks around each bar group.
// The grouped vertical minor-tick branch reads uninitialized coordinates in the original.
void UiBarGraphWindow::DrawTicks(GxBuffer *buffer, UiGraphAxis axes) {
  i32 direction = (i32)orientation, mask = (i32)axes;
  if (direction == 0 && (mask & 2))
    UiGraphWindow::DrawTicks(buffer, static_cast<UiGraphAxis>(2));
  direction = (i32)orientation;
  if (direction == 1 && (mask & 1))
    UiGraphWindow::DrawTicks(buffer, static_cast<UiGraphAxis>(1));
  direction = (i32)orientation;
  if ((!major_tick_usage[0] && !major_tick_usage[1] && !minor_tick_usage[0] && !minor_tick_usage[1]) ||
      series_begin == series_end || !buffer->Lock(GxLockFlags::Line))
    return;
  if ((direction == 0 && (mask & 1)) || (direction == 1 && (mask & 2))) {
    i32 axis = direction ? 1 : 0;
    u32 series_count = (u32)(series_end - series_begin);
    u32 categories = series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
    i32 major_start = direction ? (i32)((u32)graph_area.left - (u32)major_tick_length[axis])
                                : (major_tick_grid[axis] ? graph_area.top : graph_area.bottom);
    i32 major_end = direction ? (major_tick_grid[axis] ? graph_area.right : graph_area.left)
                              : (i32)((u32)graph_area.bottom + (u32)major_tick_length[axis]);
    i32 minor_start = direction ? (i32)((u32)graph_area.left - (u32)minor_tick_length[axis])
                                : (minor_tick_grid[axis] ? graph_area.top : graph_area.bottom);
    i32 minor_end = direction ? (minor_tick_grid[axis] ? graph_area.right : graph_area.left)
                              : (i32)((u32)graph_area.bottom + (u32)minor_tick_length[axis]);
    for (u32 i = 0; i < categories; ++i) {
      i32 left = GetLeftOfBar(i, 0);
      if (major_tick_usage[axis]) {
        i32 position = UiBarTickCoordinate((f64)left - (f64)bar_spacing * 0.5);
        if (direction)
          buffer->DrawLine(major_start, position, major_end, position, (i32)tick_color, 1);
        else
          buffer->DrawLine(position, major_start, position, major_end, (i32)tick_color, 1);
      }
      if (minor_tick_usage[axis]) {
        assert(!direction || (i32)graph_type == 0 || series_count == 1);
        f64 width = (i32)graph_type == 0 || series_count == 1 ? bar_width : (f64)series_count * bar_width;
        i32 position = UiBarTickCoordinate((f64)left + width * 0.5);
        if (direction)
          buffer->DrawLine(minor_start, position, minor_end, position, (i32)tick_color, 1);
        else
          buffer->DrawLine(position, minor_start, position, minor_end, (i32)tick_color, 1);
      }
    }
  }
  buffer->Unlock(GxLockFlags::Line);
}

// Draw the numeric value-axis labels and the named categories on the other axis.
void UiBarGraphWindow::DrawTickText(GxBuffer *buffer, UiGraphAxis axes) {
  i32 direction = (i32)orientation, mask = (i32)axes;
  if (direction == 0 && (mask & 2) && tick_text[1])
    UiGraphWindow::DrawTickText(buffer, static_cast<UiGraphAxis>(2));
  direction = (i32)orientation;
  if (direction == 1 && (mask & 1) && tick_text[0])
    UiGraphWindow::DrawTickText(buffer, static_cast<UiGraphAxis>(1));
  direction = (i32)orientation;
  if (direction != 0 && direction != 1)
    return;
  i32 axis = direction ? 1 : 0;
  if (!(mask & (1 << axis)) || !tick_text[axis] || series_begin == series_end || !axis_tick_font[axis])
    return;
  FtFont *font = axis_tick_font[axis];
  u32 color = axis_tick_color[axis];
  i32 height = font->Height(0), tick_length = GetMaxTickLengthForAxis(static_cast<UiGraphAxis>(axis));
  u32 count = category_begin ? (u32)(category_end - category_begin) : 0;
  u32 data_count = series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
  if (count > data_count)
    count = data_count;
  for (u32 index = 0; index < count; ++index) {
    ScString *label = &category_begin[index];
    if (label->begin == label->end)
      continue;
    ScRect rectangle;
    GetRectOfBarsInCategory(index, &rectangle);
    i32 width = font->Measure(label->begin, (u32)(label->end - label->begin));
    if (!axis) {
      rectangle.top = (i32)((u32)graph_area.bottom + (u32)tick_length);
      rectangle.bottom = (i32)((u32)rectangle.top + (u32)height);
      i32 span = (i32)((u32)rectangle.right - (u32)rectangle.left);
      rectangle.left = (i32)((u32)rectangle.left + (u32)(span / 2) - (u32)(width / 2) - 1);
      rectangle.right = (i32)((u32)rectangle.left + (u32)width + 2);
    } else {
      rectangle.right = (i32)((u32)graph_area.left - (u32)tick_length);
      rectangle.left = (i32)((u32)rectangle.right - (u32)width);
      i32 span = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
      rectangle.top = (i32)((u32)rectangle.top + (u32)(span / 2) - (u32)(height / 2));
      rectangle.bottom = (i32)((u32)rectangle.top + (u32)height + 1);
    }
    if (color == kUiGraphUseFontColor)
      font->DrawTextPara(
          buffer, rectangle, label->begin, (u32)(label->end - label->begin), static_cast<FtOutputStyle>(1));
    else
      font->DrawTextPara(
          buffer, rectangle, label->begin, (u32)(label->end - label->begin), color, static_cast<FtOutputStyle>(1));
  }
}

// Insert one value into a line's backing range, doubling its allocation when it is full.
static void UiLineGraphInsertValue(UiGraphFloatVector *series, u32 index, f32 value) {
  u32 size = series->begin ? (u32)(series->end - series->begin) : 0;
  u32 capacity = series->begin ? (u32)(series->storage_end - series->begin) : 0;
  if (size == capacity) {
    if (size > INT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Line graph data exceeds its storage range");
    u32 next = size ? size * 2 : 1;
    f32 *storage = new f32[next];
    for (u32 i = 0; i < index; ++i)
      storage[i] = series->begin[i];
    storage[index] = value;
    for (u32 i = index; i < size; ++i)
      storage[i + 1] = series->begin[i];
    delete[] series->begin;
    series->begin = storage;
    series->end = storage + size + 1;
    series->storage_end = storage + next;
  } else {
    for (u32 i = size; i > index; --i)
      series->begin[i] = series->begin[i - 1];
    series->begin[index] = value;
    ++series->end;
  }
}

// Insert a data point in one series; the all-ones series selects the sole series when exactly one exists.
void UiLineGraphWindow::InsertData(f32 value, u32 series, u32 index) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *values = &series_begin[series];
    u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
    if (index == kUiAppendGraphValue)
      index = size;
    assert(index <= size);
    UiLineGraphInsertValue(values, index, value);
  }
}

// Remove one data point from a selected series and close the remaining values over its position.
void UiLineGraphWindow::RemoveData(u32 index, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *values = &series_begin[series];
    [[maybe_unused]] u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
    assert(index < size);
    f32 *position = values->begin + index;
    for (f32 *next = position + 1; next != values->end; ++next)
      next[-1] = *next;
    --values->end;
  }
}

// Replace one point or append it when the index is the all-ones sentinel.
void UiLineGraphWindow::SetData(f32 value, u32 index, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series < count) {
    UiGraphFloatVector *values = &series_begin[series];
    if (index == kUiAppendGraphValue) {
      u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
      UiLineGraphInsertValue(values, size, value);
    } else {
      [[maybe_unused]] u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
      assert(index < size);
      values->begin[index] = value;
    }
  }
}

// Replace a series with the supplied vector, preserving storage when its capacity is sufficient.
void UiLineGraphWindow::SetData(UiGraphFloatVector *values, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series >= count || values == &series_begin[series])
    return;
  UiGraphFloatVector *target = &series_begin[series];
  u32 length = values->begin ? (u32)(values->end - values->begin) : 0;
  u32 capacity = target->begin ? (u32)(target->storage_end - target->begin) : 0;
  if (length > capacity) {
    f32 *storage = new f32[length];
    for (u32 i = 0; i < length; ++i)
      storage[i] = values->begin[i];
    delete[] target->begin;
    target->begin = storage;
    target->storage_end = storage + length;
  } else {
    for (u32 i = 0; i < length; ++i)
      target->begin[i] = values->begin[i];
  }
  target->end = target->begin ? target->begin + length : nullptr;
}

// Append the supplied values in order to the selected series.
void UiLineGraphWindow::AppendData(UiGraphFloatVector *values, u32 series) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (series == kUiOnlyGraphSeries && count == 1)
    series = 0;
  if (series < count) {
    UiGraphFloatVector *target = &series_begin[series];
    for (u32 i = 0; values->begin && i < (u32)(values->end - values->begin); ++i) {
      u32 size = target->begin ? (u32)(target->end - target->begin) : 0;
      UiLineGraphInsertValue(target, size, values->begin[i]);
    }
  }
}

// Set a category label, expanding the label range with empty strings through the requested index.
void UiLineGraphWindow::InsertCategoryName(const ScString &name, u32 index) {
  u32 count = category_begin ? (u32)(category_end - category_begin) : 0;
  if (index == kUiAppendGraphCategory)
    index = count;
  if (index >= count) {
    if (index == UINT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Line graph categories exceed their storage range");
    u32 length = index + 1, capacity = category_begin ? (u32)(category_storage_end - category_begin) : 0;
    if (length > capacity) {
      u32 added = length - count, growth = count > added ? count : added;
      if (growth > UINT32_MAX - count)
        ScFatal(SC_ERROR_INVALID, "Line graph categories exceed their storage range");
      capacity = count + growth;
      ScString *storage = new ScString[capacity];
      for (u32 i = 0; i < count; ++i)
        storage[i] = category_begin[i];
      delete[] category_begin;
      category_begin = storage;
      category_storage_end = storage + capacity;
    }
    category_end = category_begin + length;
  }
  category_begin[index] = name;
}

void UiLineGraphWindow::CompleteRedraw(GxBuffer *buffer) {
  UiGraphWindow::CompleteRedraw(buffer);
}

// Draw enabled line segments first, then symbols using the original shared data-count rule.
void UiLineGraphWindow::DrawGraphData(GxBuffer *buffer) {
  u32 series_count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (!series_count)
    return;
  u32 data_count = series_begin[0].begin ? (u32)(series_begin[0].end - series_begin[0].begin) : 0;
  if (!data_count)
    return;
  UiGraphAxis value_axis = static_cast<UiGraphAxis>((i32)orientation == 0);
  if (buffer->Lock(GxLockFlags::Line)) {
    for (u32 series = 0; series < series_count; ++series) {
      assert(info_begin && series < (u32)(info_end - info_begin));
      if (info_begin[series].line_use) {
        UiGraphFloatVector *values = &series_begin[series];
        data_count = values->begin ? (u32)(values->end - values->begin) : 0;
        for (u32 i = 0; i + 1 < data_count; ++i) {
          f32 first = values->begin[i], second = values->begin[i + 1];
          ScRect segment;
          segment.left = GetCenterOfDataPoint(i);
          segment.top = ValueToPixel(value_axis, first, true);
          segment.right = GetCenterOfDataPoint(i + 1);
          segment.bottom = ValueToPixel(value_axis, second, true);
          if ((i32)orientation == 1) {
            i32 x = segment.left, y = segment.top;
            segment.left = segment.bottom;
            segment.top = x;
            segment.bottom = segment.right;
            segment.right = y;
          }
          DrawLine(buffer, &segment, series);
        }
      }
    }
    buffer->Unlock(GxLockFlags::Line);
  }
  if (symbol_font && symbol_font->BeginDraw(buffer)) {
    for (u32 series = 0; series < series_count; ++series) {
      assert(info_begin && series < (u32)(info_end - info_begin));
      if (info_begin[series].symbol_use) {
        UiGraphFloatVector *values = &series_begin[series];
        assert(!data_count || (values->begin && data_count <= (u32)(values->end - values->begin)));
        for (u32 i = 0; i < data_count; ++i) {
          f32 value = values->begin[i];
          ScPoint point;
          point.x = GetCenterOfDataPoint(i);
          point.y = ValueToPixel(value_axis, value, true);
          if ((i32)orientation == 1) {
            i32 x = point.x;
            point.x = point.y;
            point.y = x;
          }
          DrawSymbol(buffer, &point, series);
        }
      }
    }
    symbol_font->EndDraw(buffer);
  }
}

// Find the greatest data value over all series, beginning at the original negative float limit.
f32 UiLineGraphWindow::GetHighestAxisValue(UiGraphAxis) {
  f32 result = -3.4028234663852886e38f;
  for (UiGraphFloatVector *series = series_begin; series != series_end; ++series)
    for (f32 *value = series->begin; value != series->end; ++value)
      if (*value > result)
        result = *value;
  return result;
}

f32 UiLineGraphWindow::GetLowestAxisValue(UiGraphAxis) {
  f32 result = 3.4028234663852886e38f;
  for (UiGraphFloatVector *series = series_begin; series != series_end; ++series)
    for (f32 *value = series->begin; value != series->end; ++value)
      if (*value < result || *value != *value || result != result)
        result = *value;
  return result;
}

// Locate a category along the oriented graph area using the first series' data count.
i32 UiLineGraphWindow::GetCenterOfDataPoint(u32 index) {
  u32 count = 2;
  if (series_begin != series_end) {
    u32 size = series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
    if (size > 1)
      count = size;
  }
  i32 start = (i32)orientation ? graph_area.top : graph_area.left;
  i32 end = (i32)orientation ? graph_area.bottom : graph_area.right;
  i32 width = (i32)((u32)end - (u32)start);
  f64 scaled = (f64)width * (f64)index / (f64)(count - 1);
  assert(scaled >= -9223372036854775808.0 && scaled < 9223372036854775808.0);
  i32 offset = (i32)(u32)(i64)scaled;
  return (i32)orientation ? (i32)((u32)end - (u32)offset) : (i32)((u32)start + (u32)offset);
}

// Center the series character at the requested point and temporarily apply its color when specified.
void UiLineGraphWindow::DrawSymbol(GxBuffer *buffer, ScPoint *point, u32 series) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (series >= count || !symbol_font)
    return;
  u32 color = info_begin[series].color;
  i32 character = (i8)info_begin[series].symbol_character;
  u32 width = (u32)symbol_font->Width((u32)character), height = (u32)symbol_font->Height(0);
  if (color == kUiGraphUseFontColor) {
    symbol_font->DrawChar(
        buffer, (i32)((u32)point->x - (width >> 1)), (i32)((u32)point->y - (height >> 1)), (u32)character);
  } else {
    u32 old_color = symbol_font->Color();
    symbol_font->SetColor(color);
    symbol_font->DrawChar(
        buffer, (i32)((u32)point->x - (width >> 1)), (i32)((u32)point->y - (height >> 1)), (u32)character);
    symbol_font->SetColor(old_color);
  }
}

void UiLineGraphWindow::DrawLine(GxBuffer *buffer, ScRect *line, u32 series) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (series < count)
    buffer->DrawLine(
        line->left, line->top, line->right, line->bottom, (i32)info_begin[series].color, info_begin[series].thickness);
}

void UiLineGraphWindow::SetGraphOrientation(UiGraphOrientation value) {
  orientation = value;
}

// Retain the new symbol font before releasing the old one, then store its color.
void UiLineGraphWindow::SetSymbolFont(FtFont *font, u32 color) {
  if (font)
    font->AddRef();
  if (symbol_font)
    symbol_font->Release();
  symbol_font = font;
  symbol_color = color;
}

void UiLineGraphWindow::SetSymbolFont(i32 size, u32 first, u32 second, u32 third) {
  SetSymbolFont(UiGraphLoadFont(size, first, second, third), kUiGraphUseFontColor);
}

void UiLineGraphWindow::SetSymbolUse(UiGraphUsage symbol, UiGraphUsage line, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i) {
    if (index == kUiAllGraphSeries || index == i) {
      info_begin[i].symbol_use = (i32)symbol != 0;
      info_begin[i].line_use = (i32)line != 0;
    }
  }
}

void UiLineGraphWindow::SetSymbolCharacter(char character, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].symbol_character = (u8)character;
}

void UiLineGraphWindow::SetSymbolSize(u32 size, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].symbol_size = size;
}

void UiLineGraphWindow::SetSeriesColor(u32 color, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].color = color;
}

// Copy the supplied label to one series, or to all series for the all-series sentinel.
void UiLineGraphWindow::SetSeriesName(const ScString &name, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].name = name;
}

// Copy the complete drawing settings and owned label into an existing series record.
void UiLineGraphWindow::SetSeriesInfo(UiLineGraphSeriesInfo *info, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index < count) {
    UiLineGraphSeriesInfo *target = &info_begin[index];
    target->symbol_use = info->symbol_use;
    target->symbol_character = info->symbol_character;
    target->symbol_size = info->symbol_size;
    target->line_use = info->line_use;
    target->color = info->color;
    target->thickness = info->thickness;
    target->name = info->name;
  }
}

// Remove the original number of series through the virtual remove operation.
void UiLineGraphWindow::RemoveAllSeries() {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  while (count--)
    RemoveSeries(kUiLastGraphSeries);
}

// Clear the selected category label; the all-ones index selects the last label.
void UiLineGraphWindow::RemoveCategoryName(u32 index) {
  u32 count = category_begin ? (u32)(category_end - category_begin) : 0;
  if (index == kUiLastGraphCategory)
    index = count - 1;
  if (index < count)
    category_begin[index].Erase(0, category_begin[index].Strlen());
}

// Initialize empty series and categories, then configure ticks for the default horizontal orientation.
UiLineGraphWindow::UiLineGraphWindow()
    : orientation(static_cast<UiGraphOrientation>(0)), series_begin(nullptr), series_end(nullptr),
      series_storage_end(nullptr), info_begin(nullptr), info_end(nullptr), info_storage_end(nullptr),
      category_begin(nullptr), category_end(nullptr), category_storage_end(nullptr), symbol_font(nullptr),
      symbol_color(kUiGraphUseFontColor) {
  major_tick_usage[0] = 0;
  major_tick_usage[1] = 1;
  major_tick_grid[0] = 0;
  major_tick_grid[1] = 1;
}

// Release the line-graph font and data before the inherited graph destructor runs.
UiLineGraphWindow::~UiLineGraphWindow() {
  UiLineGraphWindow::Shutdown();
  delete[] category_begin;
  delete[] info_begin;
  for (UiGraphFloatVector *series = series_begin; series != series_end; ++series)
    delete[] series->begin;
  delete[] series_begin;
}

bool UiLineGraphWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnLineGraphView, UiLineGraph);
  return UiGraphWindow::QueryInterface(iid, out);
}

u32 UiLineGraphWindow::AddRef() {
  return UiGraphWindow::AddRef();
}

u32 UiLineGraphWindow::Release() {
  return UiGraphWindow::Release();
}

bool UiLineGraphWindow::Shutdown() {
  ScRelease(&symbol_font);
  return UiGraphWindow::Shutdown();
}

void UiLineGraphWindow::LoadDefaultFonts() {
  if (!symbol_font)
    SetSymbolFont(1, 12, 0, kUiGraphUseFontColor);
  UiGraphWindow::LoadDefaultFonts();
}

UiLineGraphSeriesInfo::UiLineGraphSeriesInfo()
    : symbol_use(1), symbol_character('A'), symbol_size(12), line_use(1), color(0), thickness(1) {
}

UiLineGraphSeriesInfo::UiLineGraphSeriesInfo(const UiLineGraphSeriesInfo &source)
    : symbol_use(source.symbol_use), symbol_character(source.symbol_character), symbol_size(source.symbol_size),
      line_use(source.line_use), color(source.color), thickness(source.thickness), name(source.name) {
}

UiLineGraphSeriesInfo::~UiLineGraphSeriesInfo() = default;

// Copy all drawing fields in their original order, then assign the owned label.
UiLineGraphSeriesInfo &UiLineGraphSeriesInfo::operator=(const UiLineGraphSeriesInfo &source) {
  symbol_use = source.symbol_use;
  symbol_character = source.symbol_character;
  symbol_size = source.symbol_size;
  line_use = source.line_use;
  color = source.color;
  thickness = source.thickness;
  name = source.name;
  return *this;
}

// The installed module constructs eight default series records once and reuses them for count changes.
static UiLineGraphSeriesInfo UiLineGraphDefaults[8];

// Copy one line's data, keeping the destination allocation when it has enough capacity.
static void UiLineGraphAssignSeries(UiGraphFloatVector *target, const UiGraphFloatVector *source) {
  u32 size = source->begin ? (u32)(source->end - source->begin) : 0;
  u32 capacity = target->begin ? (u32)(target->storage_end - target->begin) : 0;
  if (size > capacity) {
    f32 *storage = new f32[size];
    for (u32 i = 0; i < size; ++i)
      storage[i] = source->begin[i];
    delete[] target->begin;
    target->begin = storage;
    target->storage_end = storage + size;
  } else {
    for (u32 i = 0; i < size; ++i)
      target->begin[i] = source->begin[i];
  }
  target->end = target->begin ? target->begin + size : nullptr;
}

// Grow the line-data range by copying each existing series into independently owned storage.
static void UiLineGraphReserveSeries(
    UiGraphFloatVector **begin, UiGraphFloatVector **end, UiGraphFloatVector **storage_end, u32 required) {
  u32 count = *begin ? (u32)(*end - *begin) : 0;
  u32 capacity = *begin ? (u32)(*storage_end - *begin) : 0;
  if (required <= capacity)
    return;
  u32 added = required - count, growth = count > added ? count : added;
  if (growth > UINT32_MAX - count)
    ScFatal(SC_ERROR_INVALID, "Line graph series exceed their storage range");
  capacity = count + growth;
  UiGraphFloatVector *storage = new UiGraphFloatVector[capacity]{};
  for (u32 i = 0; i < count; ++i)
    UiLineGraphAssignSeries(&storage[i], &(*begin)[i]);
  for (u32 i = 0; i < count; ++i)
    delete[] (*begin)[i].begin;
  delete[] *begin;
  *begin = storage;
  *end = storage + count;
  *storage_end = storage + capacity;
}

// Grow the line-info range and copy the existing owned labels into its new storage.
static void UiLineGraphReserveInfo(
    UiLineGraphSeriesInfo **begin, UiLineGraphSeriesInfo **end, UiLineGraphSeriesInfo **storage_end, u32 required) {
  u32 count = *begin ? (u32)(*end - *begin) : 0;
  u32 capacity = *begin ? (u32)(*storage_end - *begin) : 0;
  if (required <= capacity)
    return;
  u32 added = required - count, growth = count > added ? count : added;
  if (growth > UINT32_MAX - count)
    ScFatal(SC_ERROR_INVALID, "Line graph series information exceeds its storage range");
  capacity = count + growth;
  UiLineGraphSeriesInfo *storage = new UiLineGraphSeriesInfo[capacity];
  for (u32 i = 0; i < count; ++i)
    storage[i] = (*begin)[i];
  delete[] *begin;
  *begin = storage;
  *end = storage + count;
  *storage_end = storage + capacity;
}

// Resize both line collections and restore the initialized default information for each line.
void UiLineGraphWindow::SetSeriesCount(u32 count) {
  assert(count <= 8);
  u32 old_count = series_begin ? (u32)(series_end - series_begin) : 0;
  UiLineGraphReserveSeries(&series_begin, &series_end, &series_storage_end, count);
  for (u32 i = count; i < old_count; ++i) {
    delete[] series_begin[i].begin;
    series_begin[i] = {};
  }
  for (u32 i = old_count; i < count; ++i)
    series_begin[i] = {};
  series_end = series_begin ? series_begin + count : nullptr;
  {
    UiLineGraphSeriesInfo defaults;
    UiLineGraphReserveInfo(&info_begin, &info_end, &info_storage_end, count);
  }
  u32 old_info_count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = count; i < old_info_count; ++i)
    info_begin[i] = UiLineGraphDefaults[0];
  for (u32 i = 0; i < count; ++i)
    info_begin[i] = UiLineGraphDefaults[i];
  info_end = info_begin ? info_begin + count : nullptr;
}

// Insert an empty data series and its default settings, or append both for the all-ones index.
void UiLineGraphWindow::InsertSeries(u32 index) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (index == kUiAppendGraphSeries)
    index = count;
  else if (index >= count)
    return;
  if (count == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "Line graph series exceed their storage range");
  u32 capacity = series_begin ? (u32)(series_storage_end - series_begin) : 0;
  if (count == capacity) {
    if (count > INT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Line graph series exceed their storage range");
    u32 next = count ? count * 2 : 1;
    UiGraphFloatVector *storage = new UiGraphFloatVector[next]{};
    for (u32 i = 0; i < index; ++i)
      UiLineGraphAssignSeries(&storage[i], &series_begin[i]);
    for (u32 i = index; i < count; ++i)
      UiLineGraphAssignSeries(&storage[i + 1], &series_begin[i]);
    for (u32 i = 0; i < count; ++i)
      delete[] series_begin[i].begin;
    delete[] series_begin;
    series_begin = storage;
    series_end = storage + count;
    series_storage_end = storage + next;
  } else if (index < count) {
    UiLineGraphAssignSeries(&series_begin[count], &series_begin[count - 1]);
    for (u32 i = count - 1; i > index; --i)
      UiLineGraphAssignSeries(&series_begin[i], &series_begin[i - 1]);
    series_begin[index].end = series_begin[index].begin;
  } else {
    series_begin[index] = {};
  }
  ++series_end;
  UiLineGraphSeriesInfo defaults;
  u32 info_count = info_begin ? (u32)(info_end - info_begin) : 0;
  assert(index <= info_count);
  UiLineGraphWindow::InsertSeriesInfo(&defaults, index == info_count ? kUiAllGraphSeries : index);
}

// Erase a data series and its corresponding settings, or remove the last pair for the all-ones index.
void UiLineGraphWindow::RemoveSeries(u32 index) {
  u32 count = series_begin ? (u32)(series_end - series_begin) : 0;
  if (index == kUiLastGraphSeries)
    index = count - 1;
  else if (index >= count)
    return;
  for (u32 i = index + 1; i < count; ++i)
    UiLineGraphAssignSeries(&series_begin[i - 1], &series_begin[i]);
  --series_end;
  delete[] series_end->begin;
  *series_end = {};
  UiLineGraphWindow::RemoveSeriesInfo(index);
}

// Insert a copy of the supplied series settings before an existing entry or append it.
void UiLineGraphWindow::InsertSeriesInfo(UiLineGraphSeriesInfo *info, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index == kUiAppendGraphSeries)
    index = count;
  else if (index >= count)
    return;
  if (count == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "Line graph series information exceeds its storage range");
  UiLineGraphSeriesInfo copy = *info;
  UiLineGraphReserveInfo(&info_begin, &info_end, &info_storage_end, count + 1);
  for (u32 i = count; i > index; --i)
    info_begin[i] = info_begin[i - 1];
  info_begin[index] = copy;
  ++info_end;
}

// Erase one settings record and preserve the order of the following records.
void UiLineGraphWindow::RemoveSeriesInfo(u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index == kUiLastGraphSeries)
    index = count - 1;
  else if (index >= count)
    return;
  for (u32 i = index + 1; i < count; ++i)
    info_begin[i - 1] = info_begin[i];
  --info_end;
  UiLineGraphSeriesInfo defaults;
  *info_end = defaults;
}

// Draw numeric ticks on the value axis and category ticks along the first series' data points.
void UiLineGraphWindow::DrawTicks(GxBuffer *buffer, UiGraphAxis axes) {
  i32 mask = (i32)axes;
  i32 last_center = 0;
  [[maybe_unused]] bool has_last_center = false;
  if ((i32)orientation == 0 && (mask & 2))
    UiGraphWindow::DrawTicks(buffer, static_cast<UiGraphAxis>(2));
  if ((i32)orientation == 1 && (mask & 1))
    UiGraphWindow::DrawTicks(buffer, static_cast<UiGraphAxis>(1));
  if ((!major_tick_usage[0] && !minor_tick_usage[0] && !major_tick_usage[1] && !minor_tick_usage[1]) ||
      series_begin == series_end || !buffer->Lock(GxLockFlags::Line))
    return;
  if ((i32)orientation == 0 && (mask & 1) && (major_tick_usage[0] || minor_tick_usage[0])) {
    i32 major_end = (i32)((u32)graph_area.bottom + (u32)major_tick_length[0]);
    i32 major_start = major_tick_grid[0] ? graph_area.top : graph_area.bottom;
    i32 minor_end = (i32)((u32)graph_area.bottom + (u32)minor_tick_length[0]);
    i32 minor_start = minor_tick_grid[0] ? graph_area.top : graph_area.bottom;
    u32 count = series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
    for (u32 index = 0; index < count; ++index) {
      i32 center = GetCenterOfDataPoint(index);
      last_center = center;
      has_last_center = true;
      if (minor_tick_usage[0]) {
        i32 x = index == 0 ? graph_area.left : index == count - 1 ? graph_area.right : center;
        buffer->DrawLine(x, minor_start, x, minor_end, (i32)tick_color, 1);
      }
      if (major_tick_usage[0]) {
        i32 next = GetCenterOfDataPoint(index + 1);
        i32 x = (i32)((u32)center + (u32)next) / 2;
        buffer->DrawLine(x, major_start, x, major_end, (i32)tick_color, 1);
      }
    }
  }
  if ((i32)orientation == 1 && (mask & 2) && (major_tick_usage[1] || minor_tick_usage[1])) {
    i32 major_start = (i32)((u32)graph_area.left - (u32)major_tick_length[1]);
    i32 major_end = major_tick_grid[1] ? graph_area.right : graph_area.left;
    i32 minor_start = (i32)((u32)graph_area.left - (u32)minor_tick_length[1]);
    i32 minor_end = minor_tick_grid[1] ? graph_area.right : graph_area.left;
    u32 count = series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
    for (u32 index = 0; index < count; ++index) {
      i32 center = GetCenterOfDataPoint(index);
      if (minor_tick_usage[1]) {
        i32 y = index == 0 ? graph_area.top : index == count - 1 ? graph_area.bottom : center;
        buffer->DrawLine(minor_start, y, minor_end, y, (i32)tick_color, 1);
      }
      if (major_tick_usage[1]) {
        i32 next = GetCenterOfDataPoint(index + 1);
        assert(has_last_center);
        i32 y = (i32)((u32)last_center + (u32)next) / 2;
        buffer->DrawLine(major_start, y, major_end, y, (i32)tick_color, 1);
      }
    }
  }
  buffer->Unlock(GxLockFlags::Line);
}

// Draw numeric labels on the value axis and named category labels beside the first data series.
void UiLineGraphWindow::DrawTickText(GxBuffer *buffer, UiGraphAxis axes) {
  i32 mask = (i32)axes, direction = (i32)orientation;
  if (direction == 0 && (mask & 2) && tick_text[1])
    UiGraphWindow::DrawTickText(buffer, static_cast<UiGraphAxis>(2));
  if (direction == 1 && (mask & 1) && tick_text[0])
    UiGraphWindow::DrawTickText(buffer, static_cast<UiGraphAxis>(1));
  i32 axis = direction == 0 ? 0 : 1;
  if ((direction != 0 && direction != 1) || !(mask & (1 << axis)) || !tick_text[axis] || series_begin == series_end ||
      !axis_tick_font[axis])
    return;
  FtFont *font = axis_tick_font[axis];
  u32 color = axis_tick_color[axis];
  i32 height = font->Height(0), tick_length = GetMaxTickLengthForAxis(static_cast<UiGraphAxis>(axis));
  u32 count = category_begin ? (u32)(category_end - category_begin) : 0;
  u32 data_count = series_begin->begin ? (u32)(series_begin->end - series_begin->begin) : 0;
  if (count > data_count)
    count = data_count;
  for (u32 index = 0; index < count; ++index) {
    ScString *label = &category_begin[index];
    if (label->begin == label->end)
      continue;
    i32 width = font->Measure(label->begin, (u32)(label->end - label->begin));
    ScRect label_area;
    if (!axis) {
      label_area.top = (i32)((u32)tick_length + (u32)graph_area.bottom);
      label_area.bottom = (i32)((u32)height + (u32)label_area.top);
      i32 center = GetCenterOfDataPoint(index);
      label_area.left = (i32)((u32)center - (u32)(width / 2) - 1);
      label_area.right = (i32)((u32)width + (u32)label_area.left + 2);
      if (label_area.left < graph_area.left) {
        i32 shift = (i32)((u32)graph_area.left - (u32)label_area.left);
        label_area.left = (i32)((u32)label_area.left + (u32)shift);
        label_area.right = (i32)((u32)label_area.right + (u32)shift);
      }
      if (label_area.right > graph_area.right) {
        i32 shift = (i32)((u32)graph_area.right - (u32)label_area.right);
        label_area.left = (i32)((u32)label_area.left + (u32)shift);
        label_area.right = (i32)((u32)label_area.right + (u32)shift);
      }
    } else {
      label_area.right = (i32)((u32)graph_area.left - (u32)tick_length);
      label_area.left = (i32)((u32)label_area.right - (u32)width);
      i32 center = GetCenterOfDataPoint(index);
      label_area.top = (i32)((u32)center - (u32)(height / 2));
      label_area.bottom = (i32)((u32)label_area.top + (u32)height + 2);
      if (label_area.top < graph_area.top) {
        i32 shift = (i32)((u32)graph_area.top - (u32)label_area.top);
        label_area.top = (i32)((u32)label_area.top + (u32)shift);
        label_area.bottom = (i32)((u32)label_area.bottom + (u32)shift);
      }
      if (label_area.bottom > graph_area.bottom) {
        i32 shift = (i32)((u32)graph_area.bottom - (u32)label_area.bottom);
        label_area.top = (i32)((u32)label_area.top + (u32)shift);
        label_area.bottom = (i32)((u32)label_area.bottom + (u32)shift);
      }
    }
    if (color == kUiGraphUseFontColor)
      font->DrawTextPara(
          buffer, label_area, label->begin, (u32)(label->end - label->begin), static_cast<FtOutputStyle>(1));
    else
      font->DrawTextPara(
          buffer, label_area, label->begin, (u32)(label->end - label->begin), color, static_cast<FtOutputStyle>(1));
  }
}

// Render flat and tilted pies in the installed pass order, including masks and percentage labels.
// Missing masks, series records and unwritten radial points are undefined original inputs, expressed as invariants.
void UiPieGraphWindow::DrawGraphData(GxBuffer *buffer) {
  UiGraphFloatVector normalized{};
  MakeNormalizedDataArray(&data, &normalized);
  i32 view = (i32)view_type;
  if (view == 1) {
    if (buffer->Lock(GxLockFlags::Blt)) {
      ScRect destination = pie_bounds;
      if (unknown580[4] != kUiPieSkipExtrusion) {
        u32 i = 0;
        do {
          assert(mask_alpha);
          const ScRect *source = mask_alpha->GetBufferArea();
          buffer->Blt(mask_alpha, *source, destination, static_cast<const ScRect *>(nullptr));
          destination.top = (i32)((u32)destination.top + 1);
          destination.bottom = (i32)((u32)destination.bottom + 1);
          ++i;
        } while (i < unknown580[4] + 1);
      }
      buffer->Unlock(GxLockFlags::Blt);
    }
    u32 count = normalized.begin ? (u32)(normalized.end - normalized.begin) : 0;
    f32 position = 0.0f;
    ScPoint first_edge{}, edge{}, center{};
    bool edge_written = false, first_written = false;
    for (u32 i = 0; i < count; ++i) {
      assert(info_begin && i < (u32)(info_end - info_begin));
      u32 color = info_begin[i].color;
      if (buffer->Lock(GxLockFlags::Line)) {
        GetLineFromCenterOfDiskToEdge(position, &center, &edge);
        edge_written = true;
        if (!i) {
          first_edge = edge;
          first_written = true;
        }
        buffer->DrawLine(center.x, center.y, edge.x, edge.y, (i32)color, 1);
        if (edge.y > center.y)
          buffer->DrawLine(edge.x, edge.y, edge.x, (i32)((u32)edge.y + unknown580[4]), (i32)color, 1);
        f32 next = (f32)((f64)position + normalized.begin[i]);
        GetLineFromCenterOfDiskToEdge(next, &center, &edge);
        if (i == count - 1) {
          assert(first_written);
          edge = first_edge;
        }
        buffer->DrawLine(center.x, center.y, edge.x, edge.y, (i32)color, 1);
        if (edge.y > center.y)
          buffer->DrawLine(edge.x, edge.y, edge.x, (i32)((u32)edge.y + unknown580[4]), (i32)color, 1);
        buffer->Unlock(GxLockFlags::Line);
      }
      ScPoint fill;
      f32 next = (f32)((f64)position + normalized.begin[i]);
      GetPositionForPercentageText(position, next, &fill);
      assert(edge_written);
      if ((fill.x != edge.x || fill.y != edge.y) && buffer->Lock(GxLockFlags::FloodFill)) {
        buffer->FloodFill(fill.x, fill.y, color);
        buffer->Unlock(GxLockFlags::FloodFill);
      }
      position = (f32)((f64)position + normalized.begin[i]);
    }
    assert(mask);
    const ScRect *source = mask->GetBufferArea();
    UiPieGraphShadeMaskEffects(buffer, mask, source, &graph_area, nullptr);
    count = normalized.begin ? (u32)(normalized.end - normalized.begin) : 0;
    position = 0.0f;
    for (u32 i = 0; i < count; ++i) {
      f32 next = (f32)((f64)position + normalized.begin[i]);
      ScPoint label;
      GetPositionForPercentageText(position, next, &label);
      if (unknown512 && percentage_font) {
        ScString text;
        char format[] = "%._f%%";
        format[2] = (char)((u8)decimal_places + '0');
        text.Sprintf(format, (f64)normalized.begin[i] * 100.0);
        u32 length = (u32)(text.end - text.begin);
        i32 text_height = percentage_font->Height(0);
        u32 visible;
        i32 text_width = percentage_font->GetStringVisibleWidth(text.begin, length, &visible);
        i32 x = (i32)((u32)label.x - (u32)(text_width / 2));
        i32 y = (i32)((u32)label.y - (u32)(text_height / 2));
        assert(info_begin && i < (u32)(info_end - info_begin));
        u32 color = info_begin[i].secondary_color;
        if (color == kUiGraphUseFontColor)
          percentage_font->DrawTextA(buffer, x, y, text.begin, visible);
        else
          percentage_font->DrawTextA(buffer, x, y, text.begin, visible, color);
      }
      position = (f32)((f64)position + normalized.begin[i]);
    }
    delete[] normalized.begin;
    return;
  }
  if (view != 0) {
    delete[] normalized.begin;
    return;
  }
  pie_bounds = graph_area;
  pie_bounds.left = (i32)((u32)pie_bounds.left + 1);
  pie_bounds.top = (i32)((u32)pie_bounds.top + 1);
  pie_bounds.right = (i32)((u32)pie_bounds.right - 1);
  pie_bounds.bottom = (i32)((u32)pie_bounds.bottom - 1);
  i32 width = (i32)((u32)pie_bounds.right - (u32)pie_bounds.left);
  i32 height = (i32)((u32)pie_bounds.bottom - (u32)pie_bounds.top);
  if (width > height)
    pie_bounds.right = (i32)((u32)pie_bounds.left + (u32)height);
  else if (height > width)
    pie_bounds.bottom = (i32)((u32)pie_bounds.top + (u32)width);
  if (buffer->Lock(GxLockFlags::FillCircle)) {
    buffer->FillCircle(pie_bounds.left, pie_bounds.top, pie_bounds.right, pie_bounds.bottom, kUiPieDiskFillPattern);
    buffer->Unlock(GxLockFlags::FillCircle);
  }
  u32 count = normalized.begin ? (u32)(normalized.end - normalized.begin) : 0;
  f32 position = 0.0f;
  ScPoint first_edge{}, edge{}, center{};
  bool edge_written = false, first_written = false;
  for (u32 i = 0; i < count; ++i) {
    assert(info_begin && i < (u32)(info_end - info_begin));
    u32 color = info_begin[i].color;
    if (buffer->Lock(GxLockFlags::Line)) {
      GetLineFromCenterOfDiskToEdge(position, &center, &edge);
      edge_written = true;
      if (!i) {
        first_edge = edge;
        first_written = true;
      }
      buffer->DrawLine(center.x, center.y, edge.x, edge.y, (i32)color, 1);
      f32 next = (f32)((f64)position + normalized.begin[i]);
      GetLineFromCenterOfDiskToEdge(next, &center, &edge);
      if (i == count - 1) {
        assert(first_written);
        edge = first_edge;
      }
      buffer->DrawLine(center.x, center.y, edge.x, edge.y, (i32)color, 1);
      buffer->Unlock(GxLockFlags::Line);
    }
    f32 next = (f32)((f64)position + normalized.begin[i]);
    ScPoint fill;
    GetPositionForPercentageText(position, next, &fill);
    assert(edge_written);
    if ((fill.x != edge.x || fill.y != edge.y) && buffer->Lock(GxLockFlags::FloodFill)) {
      buffer->FloodFill(fill.x, fill.y, color);
      buffer->Unlock(GxLockFlags::FloodFill);
    }
    position = (f32)((f64)position + normalized.begin[i]);
  }
  if (buffer->Lock(GxLockFlags::Circle)) {
    buffer->DrawCircle(pie_bounds.left, pie_bounds.top, pie_bounds.right, pie_bounds.bottom, 0, 1);
    buffer->Unlock(GxLockFlags::Circle);
  }
  count = normalized.begin ? (u32)(normalized.end - normalized.begin) : 0;
  position = 0.0f;
  for (u32 i = 0; i < count; ++i) {
    if (buffer->Lock(GxLockFlags::Line)) {
      GetLineFromCenterOfDiskToEdge(position, &center, &edge);
      if (!i) {
        first_edge = edge;
        first_written = true;
      }
      buffer->DrawLine(center.x, center.y, edge.x, edge.y, 0, 1);
      f32 next = (f32)((f64)position + normalized.begin[i]);
      GetLineFromCenterOfDiskToEdge(next, &center, &edge);
      if (i == count - 1) {
        assert(first_written);
        edge = first_edge;
      }
      buffer->DrawLine(center.x, center.y, edge.x, edge.y, 0, 1);
      buffer->Unlock(GxLockFlags::Line);
    }
    if (unknown512 && percentage_font && normalized.begin[i] >= minimum_percentage) {
      ScString text;
      ScPoint label;
      f32 next = (f32)((f64)position + normalized.begin[i]);
      GetPositionForPercentageText(position, next, &label);
      char format[] = "%._f%%";
      format[2] = (char)((u8)decimal_places + '0');
      text.Sprintf(format, (f64)normalized.begin[i] * 100.0);
      u32 length = (u32)(text.end - text.begin);
      i32 text_height = percentage_font->Height(0);
      u32 visible;
      i32 text_width = percentage_font->GetStringVisibleWidth(text.begin, length, &visible);
      i32 x = (i32)((u32)label.x - (u32)(text_width / 2));
      i32 y = (i32)((u32)label.y - (u32)(text_height / 2));
      assert(info_begin && i < (u32)(info_end - info_begin));
      u32 color = info_begin[i].secondary_color;
      if (color == kUiGraphUseFontColor)
        percentage_font->DrawTextA(buffer, x, y, text.begin, visible);
      else
        percentage_font->DrawTextA(buffer, x, y, text.begin, visible, color);
    }
    position = (f32)((f64)position + normalized.begin[i]);
  }
  delete[] normalized.begin;
}

void UiPieGraphWindow::SetViewType(UiPieGraphView view) {
  view_type = view;
}

// Retain both masks and locate the non-background bounds in the locked alpha mask.
void UiPieGraphWindow::Set3DViewMaskBuffers(GxBuffer *first, GxBuffer *second) {
  if (first)
    first->AddRef();
  if (mask)
    mask->Release();
  mask = first;
  if (second)
    second->AddRef();
  if (mask_alpha)
    mask_alpha->Release();
  mask_alpha = second;
  if (mask && second)
    view_type = static_cast<UiPieGraphView>(1);
  if (mask_alpha && mask_alpha->Lock(GxLockFlags::GetSetPixel)) {
    i32 width = (i32)mask_alpha->Width(), height = (i32)mask_alpha->Height();
    u32 background = mask_alpha->GetPixel(0, 0);
    unknown580[0] = INT32_MAX;
    unknown580[1] = INT32_MAX;
    unknown580[2] = kUiPieInitialMaximumCoordinate;
    unknown580[3] = kUiPieInitialMaximumCoordinate;
    for (i32 y = 0; y < height; ++y) {
      for (i32 x = 0; x < width; ++x) {
        if (mask_alpha->GetPixel((u32)x, (u32)y) != background) {
          if (x < (i32)unknown580[0])
            unknown580[0] = (u32)x;
          if (y < (i32)unknown580[1])
            unknown580[1] = (u32)y;
          if (x > (i32)unknown580[2])
            unknown580[2] = (u32)x;
          if (y > (i32)unknown580[3])
            unknown580[3] = (u32)y;
        }
      }
    }
    i32 left = (i32)unknown580[0], top = (i32)unknown580[1];
    i32 right = (i32)unknown580[2], bottom = (i32)unknown580[3];
    if (left == INT32_MAX) {
      pie_bounds = {0, 0, width, height};
      unknown580[4] = 0;
    } else {
      ++right;
      ++bottom;
      unknown580[2] = (u32)right;
      unknown580[3] = (u32)bottom;
      pie_bounds = {(i32)((u32)graph_area.left + (u32)left), (i32)((u32)graph_area.top + (u32)top),
          (i32)((u32)graph_area.left + (u32)right), (i32)((u32)graph_area.top + (u32)bottom)};
      unknown580[4] = mask->Height() - (u32)bottom;
    }
    mask_alpha->Unlock(GxLockFlags::GetSetPixel);
  }
}

void UiPieGraphWindow::SetPercentageTextDecimalPlaces(u32 places) {
  decimal_places = places > 9 ? 9 : places;
}

void UiPieGraphWindow::SetPercentageTextFont(i32 size, u32 font_flags, u32 group, u32 instance) {
  SetPercentageTextFont(UiGraphLoadFont(size, font_flags, group, instance), kUiGraphUseFontColor);
}

// Retain the new percentage font before releasing the old font and storing the color.
void UiPieGraphWindow::SetPercentageTextFont(FtFont *font, u32 color) {
  if (font)
    font->AddRef();
  if (percentage_font)
    percentage_font->Release();
  percentage_font = font;
  percentage_color = color;
}

// Accept fractions directly, convert values above one from percent, and clamp to the unit interval.
void UiPieGraphWindow::SetMinPercentageToDisplay(f32 value) {
  f64 result = value;
  if (!(result >= 0.0))
    result = 0.0;
  if (result > 1.0) {
    result *= (f64)0.01f;
    if (result > 1.0)
      result = 1.0;
  }
  minimum_percentage = (f32)result;
}

void UiPieGraphWindow::SetSeriesColor(u32 color, u32 index, u32 secondary) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i) {
    if (index == kUiAllGraphSeries || index == i) {
      info_begin[i].color = color;
      info_begin[i].secondary_color = secondary;
    }
  }
}

// Copy the supplied label to one pie series or every series for the all-ones selector.
void UiPieGraphWindow::SetSeriesName(const ScString &name, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  for (u32 i = 0; i < count; ++i)
    if (index == kUiAllGraphSeries || index == i)
      info_begin[i].name = name;
}

// Copy the colors and owned label into an existing pie-series record.
void UiPieGraphWindow::SetSeriesInfo(UiPieGraphSeriesInfo *info, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index < count) {
    info_begin[index].color = info->color;
    info_begin[index].secondary_color = info->secondary_color;
    info_begin[index].name = info->name;
  }
}

// Replace a data value, resolving the all-ones index only when exactly one value exists.
void UiPieGraphWindow::SetData(f32 value, u32 index) {
  u32 count = data.begin ? (u32)(data.end - data.begin) : 0;
  if (index == kUiOnlyGraphValue && count == 1)
    index = 0;
  if (index < count)
    data.begin[index] = value;
}

static i32 UiPieGraphTruncate(f64 value) {
  assert(value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return (i32)(u32)(i64)value;
}

// Append a normalized value, growing the supplied output vector when it has no spare capacity.
static void UiPieGraphAppend(UiGraphFloatVector *values, f32 value) {
  u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
  u32 capacity = values->begin ? (u32)(values->storage_end - values->begin) : 0;
  if (size == capacity) {
    if (size > INT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Pie graph normalized data exceeds its storage range");
    u32 next = size ? size * 2 : 1;
    f32 *storage = new f32[next];
    for (u32 i = 0; i < size; ++i)
      storage[i] = values->begin[i];
    storage[size] = value;
    delete[] values->begin;
    values->begin = storage;
    values->end = storage + size + 1;
    values->storage_end = storage + next;
  } else {
    *values->end++ = value;
  }
}

// Compute the center and radial edge point for the selected two-dimensional or tilted view.
void UiPieGraphWindow::GetLineFromCenterOfDiskToEdge(f32 angle, ScPoint *center, ScPoint *edge) {
  center->x = (i32)((u32)pie_bounds.left + (u32)pie_bounds.right) / 2;
  center->y = (i32)((u32)pie_bounds.top + (u32)pie_bounds.bottom) / 2;
  i32 view = (i32)view_type;
  if (view != 0 && view != 1)
    return;
  i32 width = (i32)((u32)pie_bounds.right - (u32)pie_bounds.left);
  i32 height = (i32)((u32)pie_bounds.bottom - (u32)pie_bounds.top);
  f64 radians = (f64)angle * (f64)6.2831854820251465f + (f64)1.5707963705062866f;
  f32 dx = (f32)(cos(radians) * (f64)width * 0.5);
  f64 dy = sin(radians) * (f64)(view ? width : height) * 0.5;
  if (view)
    dy *= (f64)height / (f64)width;
  edge->x = (i32)((u32)center->x + (u32)UiPieGraphTruncate(dx));
  edge->y = (i32)((u32)center->y - (u32)UiPieGraphTruncate(dy));
}

// Normalize the pie data into an appended range using the original minimum total.
void UiPieGraphWindow::MakeNormalizedDataArray(UiGraphFloatVector *source, UiGraphFloatVector *out) {
  f32 total = 0.0f;
  f64 last = 0.0;
  for (f32 *value = source->begin; value != source->end; ++value) {
    last = fabs((f64)*value) + total;
    total = (f32)last;
  }
  if (source->begin == source->end || !(last > (f64)0.00009999999747378752f))
    last = (f64)0.00009999999747378752f;
  f32 factor = (f32)(1.0 / last);
  for (f32 *value = source->begin; value != source->end; ++value)
    UiPieGraphAppend(out, (f32)(fabs((f64)*value) * factor));
}

// Sum normalized slices before the selected index and dispatch the angle-based position calculation.
void UiPieGraphWindow::GetPositionForPercentageText(u32 index, ScPoint *out) {
  UiGraphFloatVector normalized{};
  MakeNormalizedDataArray(&data, &normalized);
  assert(normalized.begin && index < (u32)(normalized.end - normalized.begin));
  f64 start = 0.0;
  for (u32 i = 0; i < index; ++i)
    start += normalized.begin[i];
  f32 first = (f32)start, last = (f32)(start + normalized.begin[index]);
  GetPositionForPercentageText(first, last, out);
  delete[] normalized.begin;
}

// Place a percentage label partway along the radial line through the center of its slice.
void UiPieGraphWindow::GetPositionForPercentageText(f32 first, f32 last, ScPoint *out) {
  ScPoint center, edge;
  f32 midpoint = (f32)(((f64)first + last) * 0.5);
  GetLineFromCenterOfDiskToEdge(midpoint, &center, &edge);
  f64 fraction = !((f64)last - first > (f64)0.1f) ? (f64)0.800000011920929f : (f64)0.6000000238418579f;
  i32 x = (i32)((u32)edge.x - (u32)center.x), y = (i32)((u32)edge.y - (u32)center.y);
  out->x = (i32)((u32)center.x + (u32)UiPieGraphTruncate((f64)x * fraction));
  out->y = (i32)((u32)center.y + (u32)UiPieGraphTruncate((f64)y * fraction));
}

// Move the pie bounds by the graph origin using the stored non-background mask bounds.
void UiPieGraphWindow::SetGraphArea(const ScRect &bounds) {
  UiGraphWindow::SetGraphArea(bounds);
  pie_bounds = {(i32)((u32)graph_area.left + unknown580[0]), (i32)((u32)graph_area.top + unknown580[1]),
      (i32)((u32)graph_area.left + unknown580[2]), (i32)((u32)graph_area.top + unknown580[3])};
}

void UiPieGraphWindow::CompleteRedraw(GxBuffer *buffer) {
  UiGraphWindow::CompleteRedraw(buffer);
}

void UiPieGraphWindow::LoadDefaultFonts() {
  if (!percentage_font)
    SetPercentageTextFont(1, 12, 0, kUiGraphUseFontColor);
  UiGraphWindow::LoadDefaultFonts();
}

UiPieGraphWindow::UiPieGraphWindow()
    : view_type(static_cast<UiPieGraphView>(0)), unknown512(1), data{}, decimal_places(0), info_begin(nullptr),
      info_end(nullptr), info_storage_end(nullptr), minimum_percentage(0.0f), percentage_font(nullptr),
      percentage_color(kUiGraphUseFontColor), mask(nullptr), mask_alpha(nullptr),
      pie_bounds{INT32_MAX, INT32_MAX, -INT32_MAX, -INT32_MAX}, unknown580{} {
  axes_visible = 0;
  for (u32 i = 0; i < 2; ++i) {
    major_tick_usage[i] = 0;
    minor_tick_usage[i] = 0;
    major_tick_grid[i] = 0;
    minor_tick_grid[i] = 0;
    tick_text[i] = 0;
  }
}

// Release the pie-owned resources, settings and data before the base graph is destroyed.
UiPieGraphWindow::~UiPieGraphWindow() {
  UiPieGraphWindow::Shutdown();
  delete[] info_begin;
  delete[] data.begin;
}

bool UiPieGraphWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiPieGraph, UiPieGraph);
  return UiGraphWindow::QueryInterface(iid, out);
}

u32 UiPieGraphWindow::AddRef() {
  return UiGraphWindow::AddRef();
}

u32 UiPieGraphWindow::Release() {
  return UiGraphWindow::Release();
}

bool UiPieGraphWindow::Init() {
  return UiGraphWindow::Init();
}

// Release the two retained masks and percentage font before shutting down the shared graph.
bool UiPieGraphWindow::Shutdown() {
  ScRelease(&mask);
  ScRelease(&mask_alpha);
  ScRelease(&percentage_font);
  return UiGraphWindow::Shutdown();
}

// Clip the shading mask, lock both surfaces and adjust each native 16-bit destination pixel.
void UiPieGraphShadeMaskEffects(
    GxBuffer *destination, GxBuffer *mask, const ScRect *source, const ScRect *position, const ScRect *clip) {
  ScRect input = *source;
  ScRect output = {position->left, position->top, (i32)((u32)position->left + (u32)source->right - (u32)source->left),
      (i32)((u32)position->top + (u32)source->bottom - (u32)source->top)};
  if (!destination->GenericBufferClip(destination, &input, &output, clip))
    return;
  if (!destination->Lock(GxLockFlags::Bits))
    return;
  if (mask->Lock(GxLockFlags::Bits)) {
    GxColorType format = destination->GetColorType();
    if (format.depth == 16) {
      u32 width = (u32)input.right - (u32)input.left;
      u32 source_stride = mask->GetColorSurfaceStride();
      u32 destination_stride = destination->GetColorSurfaceStride() >> 1;
      GxPackFunction pack = destination->GetRGBTo16ConverterPtr();
      GxUnpackFunction unpack = destination->Get16ToRGBConverterPtr();
      if (pack && unpack) {
        u16 *pixels = (u16 *)destination->GetColorSurfaceBits();
        pixels += (u32)output.left + destination_stride * (u32)output.top;
        const u8 *shades = (const u8 *)mask->GetColorSurfaceBits();
        shades += (u32)input.left + source_stride * (u32)input.top;
        u32 height = (u32)input.bottom - (u32)input.top;
        for (u32 y = 0; y < height; ++y) {
          for (u32 x = 0; x < width; ++x) {
            u8 red, green, blue, shade = shades[x];
            unpack(pixels[x], &red, &green, &blue);
            if (shade <= 124) {
              red = (u8)((u32)red * shade / 128);
              green = (u8)((u32)green * shade / 128);
              blue = (u8)((u32)blue * shade / 128);
            } else if (shade >= 132) {
              red = (u8)(red + (255 - (u32)red) * (shade - 128) / 128);
              green = (u8)(green + (255 - (u32)green) * (shade - 128) / 128);
              blue = (u8)(blue + (255 - (u32)blue) * (shade - 128) / 128);
            }
            pack(&pixels[x], red, green, blue);
          }
          shades += source_stride;
          pixels += destination_stride;
        }
      }
    }
    mask->Unlock(GxLockFlags::Bits);
  }
  destination->Unlock(GxLockFlags::Bits);
}

UiPieGraphSeriesInfo::UiPieGraphSeriesInfo() : color(0), secondary_color(kUiGraphUseFontColor) {
}

UiPieGraphSeriesInfo::UiPieGraphSeriesInfo(const UiPieGraphSeriesInfo &source)
    : color(source.color), secondary_color(source.secondary_color), name(source.name) {
}

// Resize the pie data range, preserving old values and filling new entries with zero.
static void UiPieGraphResizeData(UiGraphFloatVector *values, u32 count) {
  u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
  u32 capacity = values->begin ? (u32)(values->storage_end - values->begin) : 0;
  if (count > capacity) {
    u32 added = count - size, growth = size > added ? size : added;
    if (growth > UINT32_MAX - size)
      ScFatal(SC_ERROR_INVALID, "Pie graph data exceeds its storage range");
    capacity = size + growth;
    f32 *storage = new f32[capacity];
    for (u32 i = 0; i < size; ++i)
      storage[i] = values->begin[i];
    delete[] values->begin;
    values->begin = storage;
    values->storage_end = storage + capacity;
  }
  for (u32 i = size; i < count; ++i)
    values->begin[i] = 0.0f;
  values->end = values->begin ? values->begin + count : nullptr;
}

// Insert a single value in a pie data range, growing its allocation when full.
static void UiPieGraphInsertValue(UiGraphFloatVector *values, u32 index, f32 value) {
  u32 size = values->begin ? (u32)(values->end - values->begin) : 0;
  u32 capacity = values->begin ? (u32)(values->storage_end - values->begin) : 0;
  if (size == capacity) {
    if (size > INT32_MAX)
      ScFatal(SC_ERROR_INVALID, "Pie graph data exceeds its storage range");
    u32 next = size ? size * 2 : 1;
    f32 *storage = new f32[next];
    for (u32 i = 0; i < index; ++i)
      storage[i] = values->begin[i];
    storage[index] = value;
    for (u32 i = index; i < size; ++i)
      storage[i + 1] = values->begin[i];
    delete[] values->begin;
    values->begin = storage;
    values->end = storage + size + 1;
    values->storage_end = storage + next;
  } else {
    for (u32 i = size; i > index; --i)
      values->begin[i] = values->begin[i - 1];
    values->begin[index] = value;
    ++values->end;
  }
}

// Resize the pie's settings storage while preserving the current record values and labels.
static void UiPieGraphReserveInfo(
    UiPieGraphSeriesInfo **begin, UiPieGraphSeriesInfo **end, UiPieGraphSeriesInfo **storage_end, u32 required) {
  u32 count = *begin ? (u32)(*end - *begin) : 0;
  u32 capacity = *begin ? (u32)(*storage_end - *begin) : 0;
  if (required <= capacity)
    return;
  u32 added = required - count, growth = count > added ? count : added;
  if (growth > UINT32_MAX - count)
    ScFatal(SC_ERROR_INVALID, "Pie graph series information exceeds its storage range");
  capacity = count + growth;
  UiPieGraphSeriesInfo *storage = new UiPieGraphSeriesInfo[capacity];
  for (u32 i = 0; i < count; ++i)
    storage[i] = (*begin)[i];
  delete[] *begin;
  *begin = storage;
  *end = storage + count;
  *storage_end = storage + capacity;
}

// Resize both pie collections and replace empty labels with the corresponding original default record.
void UiPieGraphWindow::SetSeriesCount(u32 count) {
  UiPieGraphResizeData(&data, count);
  u32 old_count = info_begin ? (u32)(info_end - info_begin) : 0;
  UiPieGraphReserveInfo(&info_begin, &info_end, &info_storage_end, count);
  UiPieGraphSeriesInfo defaults;
  for (u32 i = count; i < old_count; ++i)
    info_begin[i] = defaults;
  info_end = info_begin ? info_begin + count : nullptr;
  for (u32 i = 0; i < count; ++i) {
    if (info_begin[i].name.begin == info_begin[i].name.end) {
      assert(i < 8);
      info_begin[i] = defaults;
    }
  }
}

// Insert a default data value and its settings record before the selected series, or append both.
void UiPieGraphWindow::InsertSeries(u32 index) {
  u32 count = data.begin ? (u32)(data.end - data.begin) : 0;
  bool append = index == kUiAppendGraphSeries;
  if (append)
    index = count;
  else if (index >= count)
    return;
  UiPieGraphInsertValue(&data, index, 1.0f);
  UiPieGraphSeriesInfo defaults;
  u32 info_count = info_begin ? (u32)(info_end - info_begin) : 0;
  assert(append || index <= info_count);
  UiPieGraphWindow::InsertSeriesInfo(&defaults, append || index == info_count ? kUiAllGraphSeries : index);
}

// Remove the selected data value and its settings, or the last pair for the all-ones selector.
void UiPieGraphWindow::RemoveSeries(u32 index) {
  u32 count = data.begin ? (u32)(data.end - data.begin) : 0;
  if (index == kUiLastGraphSeries)
    index = count - 1;
  else if (index >= count)
    return;
  for (u32 i = index + 1; i < count; ++i)
    data.begin[i - 1] = data.begin[i];
  --data.end;
  UiPieGraphWindow::RemoveSeriesInfo(index);
}

// Remove the initial number of series through the original virtual removal method.
void UiPieGraphWindow::RemoveAllSeries() {
  u32 count = data.begin ? (u32)(data.end - data.begin) : 0;
  while (count--)
    RemoveSeries(kUiLastGraphSeries);
}

// Insert a settings record before an existing entry or append it for the all-ones selector.
void UiPieGraphWindow::InsertSeriesInfo(UiPieGraphSeriesInfo *info, u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index == kUiAppendGraphSeries)
    index = count;
  else if (index >= count)
    return;
  if (count == UINT32_MAX)
    ScFatal(SC_ERROR_INVALID, "Pie graph series information exceeds its storage range");
  UiPieGraphSeriesInfo copy = *info;
  UiPieGraphReserveInfo(&info_begin, &info_end, &info_storage_end, count + 1);
  for (u32 i = count; i > index; --i)
    info_begin[i] = info_begin[i - 1];
  info_begin[index] = copy;
  ++info_end;
}

// Erase one settings record and preserve the remaining series order.
void UiPieGraphWindow::RemoveSeriesInfo(u32 index) {
  u32 count = info_begin ? (u32)(info_end - info_begin) : 0;
  if (index == kUiLastGraphSeries)
    index = count - 1;
  else if (index >= count)
    return;
  for (u32 i = index + 1; i < count; ++i)
    info_begin[i - 1] = info_begin[i];
  --info_end;
  UiPieGraphSeriesInfo defaults;
  *info_end = defaults;
}

// Replace one existing pie value, or extend the data and settings before appending a value.
void UiPieGraphWindow::InsertData(f32 value, u32 index) {
  u32 count = data.begin ? (u32)(data.end - data.begin) : 0;
  if (index != kUiAppendGraphValue && index >= count)
    return;
  if (index == kUiAppendGraphValue) {
    index = count;
    u32 size = count + 1;
    u32 info_count = info_begin ? (u32)(info_end - info_begin) : 0;
    if (info_count < size)
      SetSeriesCount(size);
    count = data.begin ? (u32)(data.end - data.begin) : 0;
    if (count < size)
      UiPieGraphResizeData(&data, size);
  }
  SetData(value, index);
}

// Remove a data value, resolving the all-ones selector only for a single-entry range.
void UiPieGraphWindow::RemoveData(u32 index) {
  u32 count = data.begin ? (u32)(data.end - data.begin) : 0;
  if (index == kUiOnlyGraphValue && count == 1)
    index = 0;
  if (index < count) {
    for (u32 i = index + 1; i < count; ++i)
      data.begin[i - 1] = data.begin[i];
    --data.end;
  }
}
