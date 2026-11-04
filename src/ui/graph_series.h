#ifndef SC3_UI_GRAPH_SERIES_H
#define SC3_UI_GRAPH_SERIES_H

#include "common/sc3.h"
#include "ui/api.h"
#include "ui/chart_views.h"
#include "ui/ui_graph_window.h"

// The installed forty-byte record stores drawing flags, colors, a buffer pointer and its owned label.
struct UiBarGraphSeriesInfo {
  UiBarGraphSeriesInfo();
  UiBarGraphSeriesInfo(const UiBarGraphSeriesInfo &) = default;
  UiBarGraphSeriesInfo &operator=(const UiBarGraphSeriesInfo &source);
  u8 outline;
  u32 outline_color;
  u8 fill;
  u32 fill_color;
  GxBuffer *pattern;
  ScString name;
};

// The original bar identity follows the shared graph window; its rendering candidates remain unverified.
class UiBarGraphWindow : public UiGraphWindow, public UiBarGraph {
public:
  UiBarGraphWindow();
  ~UiBarGraphWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetBarGraphType(UiBarGraphType type) override;
  void SetGraphOrientation(UiBarOrientation orientation) override;
  void SetSeriesCount(u32 count) override;
  void InsertSeries(u32 index) override;
  void RemoveSeries(u32 index) override;
  void InsertCategoryName(const ScString &name, u32 index) override;
  void SetSeriesName(const ScString &name, u32 index) override;
  void SetSeriesInfo(UiBarGraphSeriesInfo *info, u32 index) override;
  void InsertSeriesInfo(UiBarGraphSeriesInfo *info, u32 index) override;
  void RemoveSeriesInfo(u32 index) override;
  void RemoveCategoryName(u32 index) override;
  void SetBarColorUse(UiGraphUsage outline, UiGraphUsage fill, u32 index) override;
  void SetBarFillColor(u32 color, u32 index) override;
  void SetBarOutlineColor(u32 color, u32 index) override;
  void SetBarPattern(GxBuffer *pattern, u32 index) override;
  void SetBarWidthPercentage(f32 value) override;
  void InsertData(f32 value, u32 series, u32 index) override;
  void RemoveData(u32 index, u32 series) override;
  void SetData(f32 value, u32 index, u32 series) override;
  void SetData(UiGraphFloatVector *values, u32 series) override;
  void AppendData(UiGraphFloatVector *values, u32 series) override;
  i32 GetLeftOfBar(u32 category, u32 series) override;
  i32 GetRightOfBar(u32 category, u32 series) override;
  i32 GetCenterOfBar(u32 category, u32 series) override;
  bool GetRectOfBar(u32 category, u32 series, ScRect *out) override;
  bool GetRectOfBarsInCategory(u32 category, ScRect *out) override;
  bool DoHitTest(ScPoint *point, u32 *out) override;
  void CompleteRedraw(GxBuffer *buffer) override;
  void DrawGraphData(GxBuffer *buffer) override;
  void DrawAllBars(GxBuffer *buffer) override;
  void DrawBar(GxBuffer *buffer, ScRect *rectangle, u32 series, UiBarDirection direction) override;
  f32 GetHighestAxisValue(UiGraphAxis axis) override;
  f32 GetLowestAxisValue(UiGraphAxis axis) override;
  void CalculateBarWidthAndSpacing() override;
  u32 CalculateMaximumBarWidth() override;

protected:
  void DrawTicks(GxBuffer *buffer, UiGraphAxis axes) override;
  void DrawTickText(GxBuffer *buffer, UiGraphAxis axes) override;
  UiBarGraphType graph_type;
  UiBarOrientation orientation;
  UiGraphFloatVector *series_begin, *series_end, *series_storage_end;
  UiBarGraphSeriesInfo *info_begin, *info_end, *info_storage_end;
  ScString *category_begin, *category_end, *category_storage_end;
  f32 bar_width, bar_spacing, bar_width_percentage;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiBarGraphSeriesInfo) == 40);
static_assert(sizeof(uptr) != 4 || sizeof(UiBarGraphWindow) == 564);

// One line's drawing settings and owned label use the original forty-byte x86 record.
struct UiLineGraphSeriesInfo {
  UiLineGraphSeriesInfo();
  UiLineGraphSeriesInfo(const UiLineGraphSeriesInfo &source);
  ~UiLineGraphSeriesInfo();
  UiLineGraphSeriesInfo &operator=(const UiLineGraphSeriesInfo &source);
  u8 symbol_use, symbol_character;
  u32 symbol_size;
  u8 line_use;
  u32 color, thickness;
  ScString name;
};

// The line-graph identity follows the base graph window; storage and rendering remain incomplete.
class UiLineGraphWindow : public UiGraphWindow, public UiLineGraph {
public:
  UiLineGraphWindow();
  ~UiLineGraphWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Shutdown() override;
  void SetGraphOrientation(UiGraphOrientation orientation) override;
  void SetSymbolFont(FtFont *font, u32 color) override;
  void SetSymbolFont(i32 size, u32 first, u32 second, u32 third) override;
  void SetSymbolUse(UiGraphUsage symbol, UiGraphUsage line, u32 index) override;
  void SetSymbolCharacter(char character, u32 index) override;
  void SetSymbolSize(u32 size, u32 index) override;
  void SetSeriesColor(u32 color, u32 index) override;
  void SetSeriesName(const ScString &name, u32 index) override;
  void SetSeriesInfo(UiLineGraphSeriesInfo *info, u32 index) override;
  void SetSeriesCount(u32 count) override;
  void InsertSeries(u32 index) override;
  void RemoveSeries(u32 index) override;
  void InsertSeriesInfo(UiLineGraphSeriesInfo *info, u32 index) override;
  void RemoveSeriesInfo(u32 index) override;
  void CompleteRedraw(GxBuffer *buffer) override;
  void DrawGraphData(GxBuffer *buffer) override;
  void RemoveAllSeries() override;
  void InsertCategoryName(const ScString &name, u32 index) override;
  void RemoveCategoryName(u32 index) override;
  void InsertData(f32 value, u32 series, u32 index) override;
  void RemoveData(u32 index, u32 series) override;
  void SetData(f32 value, u32 index, u32 series) override;
  void SetData(UiGraphFloatVector *values, u32 series) override;
  void AppendData(UiGraphFloatVector *values, u32 series) override;
  f32 GetHighestAxisValue(UiGraphAxis axis) override;
  f32 GetLowestAxisValue(UiGraphAxis axis) override;
  i32 GetCenterOfDataPoint(u32 index) override;
  void DrawSymbol(GxBuffer *buffer, ScPoint *point, u32 series) override;
  void DrawLine(GxBuffer *buffer, ScRect *line, u32 series) override;
  void LoadDefaultFonts() override;

protected:
  void DrawTicks(GxBuffer *buffer, UiGraphAxis axes) override;
  void DrawTickText(GxBuffer *buffer, UiGraphAxis axes) override;
  UiGraphOrientation orientation;
  UiGraphFloatVector *series_begin, *series_end, *series_storage_end;
  UiLineGraphSeriesInfo *info_begin, *info_end, *info_storage_end;
  ScString *category_begin, *category_end, *category_storage_end;
  FtFont *symbol_font;
  u32 symbol_color;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiLineGraphSeriesInfo) == 40);
static_assert(sizeof(uptr) != 4 || sizeof(UiGraphFloatVector) == 12);
static_assert(sizeof(uptr) != 4 || sizeof(UiLineGraphWindow) == 556);

class GxBuffer;
struct ScRect;

// Apply the observed shading effects; the source helper's private nominal return remains unresolved.
void UiPieGraphShadeMaskEffects(
    GxBuffer *destination, GxBuffer *mask, const ScRect *source, const ScRect *position, const ScRect *clip);

// Each original pie series stores two colors followed by an owned label.
struct UiPieGraphSeriesInfo {
  UiPieGraphSeriesInfo();
  UiPieGraphSeriesInfo(const UiPieGraphSeriesInfo &source);
  UiPieGraphSeriesInfo &operator=(const UiPieGraphSeriesInfo &) = default;
  u32 color, secondary_color;
  ScString name;
};

// The pie identity follows the shared graph window; its rendering candidate remains unverified.
class UiPieGraphWindow : public UiGraphWindow, public UiPieGraph {
public:
  UiPieGraphWindow();
  ~UiPieGraphWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void SetViewType(UiPieGraphView view) override;
  void Set3DViewMaskBuffers(GxBuffer *first, GxBuffer *second) override;
  void GetLineFromCenterOfDiskToEdge(f32 angle, ScPoint *center, ScPoint *edge) override;
  void GetPositionForPercentageText(u32 index, ScPoint *out) override;
  void GetPositionForPercentageText(f32 first, f32 last, ScPoint *out) override;
  void MakeNormalizedDataArray(UiGraphFloatVector *source, UiGraphFloatVector *out) override;
  void CompleteRedraw(GxBuffer *buffer) override;
  void DrawGraphData(GxBuffer *buffer) override;
  using UiGraphWindow::SetGraphArea;
  void SetGraphArea(const ScRect &area) override;
  void SetPercentageTextDecimalPlaces(u32 places) override;
  void SetPercentageTextFont(i32 size, u32 flags, u32 group, u32 instance) override;
  void SetPercentageTextFont(FtFont *font, u32 color) override;
  void SetMinPercentageToDisplay(f32 value) override;
  void SetSeriesCount(u32 count) override;
  void InsertSeries(u32 index) override;
  void RemoveSeries(u32 index) override;
  void RemoveAllSeries() override;
  void SetSeriesColor(u32 color, u32 index, u32 secondary) override;
  void SetSeriesName(const ScString &name, u32 index) override;
  void SetSeriesInfo(UiPieGraphSeriesInfo *info, u32 index) override;
  void InsertSeriesInfo(UiPieGraphSeriesInfo *info, u32 index) override;
  void RemoveSeriesInfo(u32 index) override;
  void InsertData(f32 value, u32 index) override;
  void RemoveData(u32 index) override;
  void SetData(f32 value, u32 index) override;

protected:
  void LoadDefaultFonts() override;
  UiPieGraphView view_type;
  u8 unknown512;
  UiGraphFloatVector data;
  u32 decimal_places;
  UiPieGraphSeriesInfo *info_begin, *info_end, *info_storage_end;
  f32 minimum_percentage;
  FtFont *percentage_font;
  u32 percentage_color;
  GxBuffer *mask, *mask_alpha;
  ScRect pie_bounds;
  u32 unknown580[5];
};

static_assert(sizeof(uptr) != 4 || sizeof(UiPieGraphSeriesInfo) == 28);
static_assert(sizeof(uptr) != 4 || sizeof(UiPieGraphWindow) == 600);

#endif
