#ifndef SC3_UI_UI_GRAPH_WINDOW_H
#define SC3_UI_UI_GRAPH_WINDOW_H

#include "common/rect.h"
#include "common/sc3_string.h"
#include "ui/controls.h"
#include "ui/graph_window.h"

struct UiGraphNode {
  UiGraphNode *next, *previous;
  UiGraphDynamicObject *object;
};

// The graph window owns its drawable-object list and retained fonts and buffers.
// Rendering candidates and font acquisition remain unverified; no graph factory is published yet.
class UiGraphWindow : public UiBaseWindow, public UiGraph {
public:
  UiGraphWindow();
  ~UiGraphWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  void SetBackgroundOutline(bool enabled) override;
  void SetBackgroundFillColor(u32 color) override;
  void SetBackgroundOutlineColor(u32 color) override;
  void SetBackgroundPattern(GxBuffer *pattern) override;
  void SetGraphAreaOutlineAndFill(bool outline, bool fill) override;
  void SetGraphAreaOutlineColor(u32 color) override;
  void SetGraphAreaFillColor(u32 color) override;
  void SetGraphBackgroundPattern(GxBuffer *pattern) override;
  void SetGraphArea(const ScRect &area) override;
  void GetGraphArea(ScRect *area) override;
  void CompleteRedraw(GxBuffer *buffer) override;
  void SetAxisMinValue(UiGraphAxis axis, f32 value) override;
  void SetAxisMaxValue(UiGraphAxis axis, f32 value) override;
  void SetAxisZeroPosition(UiGraphAxis axis, i32 position) override;
  void SetAxisLineColor(u32 color) override;
  void SetAxisLabel(UiGraphAxis axis, const ScString &label, bool automatic) override;
  void SetAxisLabelPosition(UiGraphAxis axis, ScRect *position) override;
  using UiGraph::SetAxisLabelFont;
  void SetAxisLabelFont(UiGraphAxis axis, i32 size, u32 first, u32 second, u32 third) override;
  void SetAxisLabelFont(UiGraphAxis axis, FtFont *font, u32 color) override;
  void SetAxisLabelRotation(UiGraphAxis axis, UiGraphRotation rotation) override;
  void SetMajorTickUsage(UiGraphAxis axis, UiGraphUsage usage) override;
  void SetMajorTickSpacing(UiGraphAxis axis, f32 spacing) override;
  void SetMajorTickDivisions(UiGraphAxis axis, u32 divisions, bool enabled) override;
  void SetMajorTickGridUsage(UiGraphAxis axis, UiGraphUsage usage) override;
  void SetMajorTickDecimalPlaces(UiGraphAxis axis, i32 places) override;
  void SetMajorTickLength(UiGraphAxis axis, i32 length) override;
  using UiGraph::SetAxisTickFont;
  void SetAxisTickFont(UiGraphAxis axis, i32 size, u32 first, u32 second, u32 third) override;
  void SetAxisTickFont(UiGraphAxis axis, FtFont *font, u32 color) override;
  void SetAxisTickTextDrawing(UiGraphAxis axis, UiGraphUsage usage) override;
  void SetMinorTickUsage(UiGraphAxis axis, UiGraphUsage usage) override;
  void SetMinorTickSpacing(UiGraphAxis axis, f32 spacing) override;
  void SetMinorTickDivisions(UiGraphAxis axis, u32 divisions, bool enabled) override;
  void SetMinorTickGridUsage(UiGraphAxis axis, UiGraphUsage usage) override;
  void SetMinorTickLength(UiGraphAxis axis, i32 length) override;
  void SetTickColor(u32 color) override;
  void SetTitle(const ScString &text, bool automatic) override;
  void SetTitlePosition(ScRect *position) override;
  using UiGraph::SetTitleFont;
  void SetTitleFont(i32 size, u32 first, u32 second, u32 third) override;
  void SetTitleFont(FtFont *font, u32 color) override;
  bool DoHitTest(ScPoint *point, u32 *result) override;
  bool AllocateHitTestData(u32 size) override;
  void *GetHitTestResultData() override;
  void AddDynamicGraphObject(UiGraphDynamicObject *object) override;
  void RemoveDynamicGraphObject(UiGraphDynamicObject *object) override;
  UiGraphTextInfo *AddMiscGraphTextInfo() override;
  UiGraphLineInfo *AddMiscGraphLine() override;
  UiGraphRectInfo *AddMiscGraphRect() override;
  UiGraphArrow *AddMiscGraphArrow() override;
  UiGraphPicture *AddMiscGraphPicture() override;

protected:
  virtual void LoadDefaultFonts();
  virtual void DrawBackground(GxBuffer *buffer);
  virtual void DrawGraphAreaBackground(GxBuffer *buffer);
  virtual void DrawAxes(GxBuffer *buffer);
  virtual void DrawTicks(GxBuffer *buffer, UiGraphAxis axis);
  virtual void DrawTickText(GxBuffer *buffer, UiGraphAxis axis);
  virtual void DrawGraphData(GxBuffer *buffer);
  virtual void DrawDynamicObjects(GxBuffer *buffer);
  virtual void DrawTitles(GxBuffer *buffer);
  virtual i32 ValueToPixel(UiGraphAxis axis, f32 value, bool clamp);
  virtual f32 PixelToValue(UiGraphAxis axis, i32 pixel, bool clamp);
  virtual i32 ValueAmountToPixelAmount(UiGraphAxis axis, f32 value);
  virtual f32 PixelAmountToValueAmount(UiGraphAxis axis, i32 pixels);
  virtual f32 GetHighestAxisValue(UiGraphAxis axis);
  virtual f32 GetLowestAxisValue(UiGraphAxis axis);
  virtual void CalculateAxisZeroPosition(UiGraphAxis axis);
  virtual i32 WindowPixelToGraphPixel(UiGraphAxis axis, i32 pixel);
  virtual i32 GraphPixelToWindowPixel(UiGraphAxis axis, i32 pixel);
  virtual i32 GetMaxTickLengthForAxis(UiGraphAxis axis);
  virtual void CommitDefaultValues();
  virtual void GetAutomaticTitleRect(ScRect *out);
  virtual void GetAutomaticGraphAreaRect(ScRect *out);
  virtual void GetAutomaticAxisLabelRect(UiGraphAxis axis, ScRect *out);
  virtual void GetAutomaticAxisMinValue(UiGraphAxis axis, f32 *out);
  virtual void GetAutomaticAxisMaxValue(UiGraphAxis axis, f32 *out);
  virtual void GetAutomaticAxisMajorTickSpacing(UiGraphAxis axis, f32 *out);
  virtual void GetAutomaticAxisMinorTickSpacing(UiGraphAxis axis, f32 *out);

  u32 unknown168;
  ScRect graph_area;
  u8 fonts_loaded;
  ScString title;
  ScRect title_position;
  FtFont *title_font;
  u32 title_color;
  i32 title_height;
  u32 unknown240, background_fill_color;
  u8 background_outline;
  u32 background_outline_color;
  u8 graph_fill;
  u32 graph_fill_color;
  u8 graph_outline;
  u32 graph_outline_color;
  GxBuffer *background_pattern, *graph_background_pattern;
  u8 axes_visible, major_tick_usage[2], minor_tick_usage[2], major_tick_grid[2], minor_tick_grid[2], tick_text[2],
      unknown291;
  u32 axis_line_color;
  f32 axis_min[2], axis_max[2], major_tick_spacing[2], minor_tick_spacing[2];
  u8 major_tick_decimals[2];
  i32 major_tick_length[2], minor_tick_length[2];
  u32 tick_color;
  u32 unknown352[2];
  i32 axis_zero_position[2];
  ScString axis_label[2];
  ScRect axis_label_position[2];
  u32 unknown440[2];
  FtFont *axis_label_font[2];
  u32 axis_label_color[2];
  FtFont *axis_tick_font[2];
  u32 axis_tick_color[2];
  UiGraphRotation axis_label_rotation[2];
  u32 unknown488;
  u8 *hit_test_result;
  u32 hit_test_size;
  UiGraphNode *dynamic_head;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGraphWindow) == 504);

#endif
