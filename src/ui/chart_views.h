#ifndef SC3_UI_CHART_VIEWS_H
#define SC3_UI_CHART_VIEWS_H

#include "common/sc3_array.h"
#include "ui/graph_window.h"
#include "ui/map_navigation.h"

struct UiBarGraphSeriesInfo;
enum class UiBarGraphType : i32;
enum class UiBarOrientation : i32;
enum class UiBarDirection : i32;

// Retail cGZBarGraph declarations establish these slots and returns; mutable outputs use pointers.
class UiBarGraph : public ScUnknown {
public:
  virtual void SetBarGraphType(UiBarGraphType type) = 0;
  virtual void SetGraphOrientation(UiBarOrientation orientation) = 0;
  virtual void SetSeriesCount(u32 count) = 0;
  virtual void InsertSeries(u32 index) = 0;
  virtual void RemoveSeries(u32 index) = 0;
  virtual void SetSeriesName(const ScString &name, u32 index) = 0;
  virtual void SetSeriesInfo(UiBarGraphSeriesInfo *info, u32 index) = 0;
  virtual void InsertSeriesInfo(UiBarGraphSeriesInfo *info, u32 index) = 0;
  virtual void RemoveSeriesInfo(u32 index) = 0;
  virtual void InsertCategoryName(const ScString &name, u32 index) = 0;
  virtual void RemoveCategoryName(u32 index) = 0;
  virtual void SetBarColorUse(UiGraphUsage outline, UiGraphUsage fill, u32 index) = 0;
  virtual void SetBarFillColor(u32 color, u32 index) = 0;
  virtual void SetBarOutlineColor(u32 color, u32 index) = 0;
  virtual void SetBarPattern(GxBuffer *pattern, u32 index) = 0;
  virtual void SetBarWidthPercentage(f32 value) = 0;
  virtual void InsertData(f32 value, u32 series, u32 index) = 0;
  virtual void RemoveData(u32 index, u32 series) = 0;
  virtual void SetData(f32 value, u32 index, u32 series) = 0;
  virtual void SetData(UiGraphFloatVector *values, u32 series) = 0;
  virtual void AppendData(UiGraphFloatVector *values, u32 series) = 0;
  virtual i32 GetLeftOfBar(u32 category, u32 series) = 0;
  virtual i32 GetRightOfBar(u32 category, u32 series) = 0;
  virtual i32 GetCenterOfBar(u32 category, u32 series) = 0;
  virtual bool GetRectOfBar(u32 category, u32 series, ScRect *out) = 0;
  virtual bool GetRectOfBarsInCategory(u32 category, ScRect *out) = 0;
  virtual bool DoHitTest(ScPoint *point, u32 *out) = 0;
  virtual void CompleteRedraw(GxBuffer *buffer) = 0;
  virtual void DrawGraphData(GxBuffer *buffer) = 0;
  virtual void DrawAllBars(GxBuffer *buffer) = 0;
  virtual void DrawBar(GxBuffer *buffer, ScRect *rectangle, u32 series, UiBarDirection direction) = 0;
  virtual f32 GetHighestAxisValue(UiGraphAxis axis) = 0;
  virtual f32 GetLowestAxisValue(UiGraphAxis axis) = 0;
  virtual void CalculateBarWidthAndSpacing() = 0;
  virtual u32 CalculateMaximumBarWidth() = 0;
};

// Private installed helper's nominal bool/BYTE spelling remains unresolved.
u8 SC3_STDCALL UiChartsCreateGraphOutline(WnWindow *parent);
u8 SC3_STDCALL UiChartsCreateOptions(WnWindow *parent);
// The private nominal return and caption spelling remain unresolved.
u8 SC3_STDCALL UiChartsCreatePieGraph(WnWindow *parent, u32 identifier, const ScStringView &caption);
// The private source return and caption/flag spellings remain unresolved; the installed body consumes nine arguments.
u8 SC3_STDCALL UiChartsCreateLabel(WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height,
    const ScStringView &caption, u32 background, u32 color);
class UiPieGraph;
void SC3_STDCALL UiChartsPaintPattern(GxBuffer *destination, u32 instance);
// Original205DB takes exactly three stack arguments and has no charts-view receiver; its nominal return is unresolved.
void SC3_STDCALL UiChartsSetTextEffects(WnWindow *window, u32 identifier, const ScStringView &text);
// The private helper's nominal return is unresolved; its callers use the pie and window effects.
void SC3_STDCALL UiChartsSetPieSliceEffects(
    WnWindow *parent, UiPieGraph *pie, f32 value, u32 identifier, u32 color, u32 *index);

// Original chart-view IID5E7 is a separate Unknown identity after the shared maps-view base.
class UiChartsViewIdentity : public ScUnknown {};

// The chart-view owner retains the actual shared view and chart identities.
// Recovered initialization and drawing keep their separately documented resource and callback verification limits.
class UiChartsView : public UiMapsView, public UiChartsViewIdentity {
public:
  static UiMapsViewInterface *CreateChartsView();
  UiChartsView();
  ~UiChartsView() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateView(UiMapsWindow *owner, ScRect *area, ScPoint *location) override;
  u8 PrepareHideView() override;
  u8 PrepareShowView() override;
  u8 EnumCityItems(CtCity *city, bool enabled) override;
  u8 RefreshData(CtCity *city) override;
  u8 OnInit(WnWindow *window) override;
  u8 OnShutdown(WnWindow *window) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool DoMessage(ScMessage *message) override;

private:
  // Original AL is zero or one; the private button handler's nominal return remains unresolved.
  u8 OnButtonActivated(WnWindow *window, u32 identifier);
  // The caption is unused by the installed body; its private source spelling and return remain unresolved.
  u8 CreateBarGraph(WnWindow *window, u32 identifier, const ScString &caption);
  // The chart update operation's nominal return and drawing implementations remain unresolved.
  void UpdateViewEffects(WnWindow *window);
  void DrawEducationChartEffects(WnWindow *window);
  void DrawElectricityChartEffects(WnWindow *window);
  void DrawGarbageChartEffects(WnWindow *window);
  void DrawHealthChartEffects(WnWindow *window);
  void DrawPopulationChartEffects(WnWindow *window);
  // The installed helper's incidental return is unresolved; callers use only window visibility effects.
  void SetChartVisibilityEffects(WnWindow *window, u32 chart);
  u32 chart_colors[17];
  u8 legend_visible[15], unknown187;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiChartsView) == 188);

class ScGraphInfoView;

// Only the source helper's used effects are known; its private nominal return remains unresolved.
void SC3_STDCALL UiGraphsSetMarkerEffects(WnWindow *window, u32 identifier, i32 first, i32 second);
// The final color input is unused by the installed body; private return and caption spelling remain unresolved.
u8 SC3_STDCALL UiGraphsCreateCheckbox(
    WnWindow *parent, u32 identifier, i32 row, i32 column, const ScStringView &caption, u32 color);
// The private nominal return and source caption spelling remain unresolved.
u8 SC3_STDCALL UiGraphsCreateLineGraph(WnWindow *parent, u32 identifier, const ScStringView &caption);
// The installed byte result is known; the private nominal return spelling remains unresolved.
u8 SC3_STDCALL UiGraphsCreatePeriodOptions(WnWindow *parent);

// The separate graphs-view identity accepts a graph-information interface.
class UiGraphsViewIdentity : public ScUnknown {
public:
  virtual u8 AddGraphInfo(ScGraphInfoView *graph) = 0;
};

// One original graph line holds its visibility flag and retained graph-information interface.
struct UiGraphLine {
  u8 enabled;
  ScGraphInfoView *graph;
};

// The graphs view owns the lines and label-placement slots used by its drawing callbacks.
// Recovered rendering and initialization retain their separately documented callback and resource limits.
class UiGraphsView : public UiMapsView, public UiGraphsViewIdentity {
public:
  static UiMapsViewInterface *CreateGraphsView();
  UiGraphsView();
  ~UiGraphsView() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 AddGraphInfo(ScGraphInfoView *graph) override;
  u8 CreateView(UiMapsWindow *owner, ScRect *area, ScPoint *location) override;
  u8 PrepareHideView() override;
  u8 PrepareShowView() override;
  u8 RefreshData(CtCity *city) override;
  u8 EnumCityItems(CtCity *city, bool enabled) override;
  u8 OnInit(WnWindow *window) override;
  u8 OnShutdown(WnWindow *window) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool DoMessage(ScMessage *message) override;

private:
  // Original AL is zero or one; the private button handler's nominal return remains unresolved.
  u8 OnButtonActivated(WnWindow *window, u32 identifier);
  // The private nominal bool/BYTE result and source caption spelling remain unresolved.
  u8 CreateGraphLegend(WnWindow *parent, u32 identifier, const ScStringView &caption, u32 color);
  // The draw operation's nominal return and complete implementation remain unresolved.
  void DrawLineGraphEffects(WnWindow *window);
  void StartPlacingLabelsEffects();
  void EndPlacingLabels();
  // Original AL is always zero or one; the private nominal return type is unresolved.
  u8 PlaceLabel(i32 *position);
  ScArray<UiGraphLine> lines;
  u8 label_slots[16];
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGraphsView) == 132);

struct UiPieGraphSeriesInfo;
enum class UiPieGraphView : i32;

// Retail cIGZPieGraph slots, including the two percentage-position overloads.
class UiPieGraph : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual void SetViewType(UiPieGraphView view) = 0;
  virtual void Set3DViewMaskBuffers(GxBuffer *first, GxBuffer *second) = 0;
  virtual void GetLineFromCenterOfDiskToEdge(f32 angle, ScPoint *center, ScPoint *edge) = 0;
  virtual void GetPositionForPercentageText(u32 index, ScPoint *out) = 0;
  virtual void GetPositionForPercentageText(f32 first, f32 last, ScPoint *out) = 0;
  virtual void MakeNormalizedDataArray(UiGraphFloatVector *source, UiGraphFloatVector *out) = 0;
  virtual void CompleteRedraw(GxBuffer *buffer) = 0;
  virtual void DrawGraphData(GxBuffer *buffer) = 0;
  virtual void SetGraphArea(const ScRect &area) = 0;
  virtual void SetPercentageTextDecimalPlaces(u32 places) = 0;
  virtual void SetPercentageTextFont(i32 size, u32 flags, u32 group, u32 instance) = 0;
  virtual void SetPercentageTextFont(FtFont *font, u32 color) = 0;
  virtual void SetMinPercentageToDisplay(f32 value) = 0;
  virtual void SetSeriesCount(u32 count) = 0;
  virtual void InsertSeries(u32 index) = 0;
  virtual void RemoveSeries(u32 index) = 0;
  virtual void RemoveAllSeries() = 0;
  virtual void SetSeriesColor(u32 color, u32 index, u32 secondary) = 0;
  virtual void SetSeriesName(const ScString &name, u32 index) = 0;
  virtual void SetSeriesInfo(UiPieGraphSeriesInfo *info, u32 index) = 0;
  virtual void InsertSeriesInfo(UiPieGraphSeriesInfo *info, u32 index) = 0;
  virtual void RemoveSeriesInfo(u32 index) = 0;
  virtual void InsertData(f32 value, u32 index) = 0;
  virtual void RemoveData(u32 index) = 0;
  virtual void SetData(f32 value, u32 index) = 0;
};

#endif
