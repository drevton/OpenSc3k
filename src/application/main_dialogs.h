#ifndef SC3_APPLICATION_MAIN_DIALOGS_H
#define SC3_APPLICATION_MAIN_DIALOGS_H

#include <list>
#include <map>
#include <vector>

#include "application/application_core.h"
#include "application/window_controls.h"
#include "city/sc_stopwatch.h"
#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_string.h"
#include "geometry/ge_types.h"
#include "sprites/api.h"
#include "ui/ui_main_window.h"
#include "ui/ui_window.h"
#include "window/api.h"

class WnSuperSpy;

// Original cWinSC3: root window at0, main-window identity at164, complete x86 extent288.
// The application and city view are borrowed; the title buffer holds the original retained reference.
class ApMainWindow : public WnRootWidget, public UiMainWindow {
public:
  ApMainWindow();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  WnWindow *AsIGZWin() override;
  bool PreInit(ApGameInterface *application) override;
  bool DoDancingBear() override;
  bool DoCopyRightScreen(bool delay) override;
  bool DoTitleBackgroundScreen() override;
  void ReleaseTitleBackgroundBuffer() override;
  bool DoMainMenu() override;
  bool ActivateMainMenu() override;
  bool DoLoadCity(ScStringView *filename) override;
  bool DoShowWinCityView() override;
  bool DbgDoDebugWindow(ScMessage *message) override;
  bool DbgDoDebugWindow(u32 first, u32 second, u32 third, u32 fourth) override;

private:
  ~ApMainWindow() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 command) override;
  bool ProcessDialogRequest(u32 command);
  bool DebugKeyDown(u32 key, u32 modifiers);
  bool DoSystemInfoWindow();
  bool InitToolbar(bool enable);

  enum class PaintMode : i32 { Nothing = 1, Black, Background, FrameRate, MemoryMonitor };
  ApGameInterface *application;
  PaintMode paint_mode;
  bool menu_shown, city_loaded;
  GxBuffer *title_buffer;
  ScRect title_source, title_destination, fill_rectangles[4];
  WnSuperSpy *super_spy;
  SpCityWindowView *city_view;
};

static_assert(sizeof(uptr) != 4 || sizeof(ApMainWindow) == 288);

// The executable's automatic cursor records whether its requested cursor was pushed successfully.
class ScAutoCursor {
public:
  explicit ScAutoCursor(u32 identifier);
  ~ScAutoCursor();

private:
  bool pushed;
};

class FtFont;
class GxBuffer;
class ScStringView;
struct WnDialogCallbackContext;
using WnDialogCallback = void (*)(u32 command, u32 value, WnDialogCallbackContext *context);

enum class WnDialogStyle : i32 { Edge, Plain, None };

// Retail cIGZWinGenDlg type 0x2BF5, in its original secondary-interface order.
class WnGeneralDialogView : public ScUnknown {
public:
  virtual bool SetBackground(GxBuffer *buffer) = 0;
  virtual WnWindow *AddListBox(const ScRect &area) = 0;
  virtual WnWindow *AddBMP(const ScRect &area, GxBuffer *buffer) = 0;
  virtual WnWindow *AddBtn(const ScRect &area, const ScStringView &caption) = 0;
  virtual WnWindow *AddCombo(const ScRect &area, bool flag) = 0;
  virtual WnWindow *AddLineInput(const ScRect &area) = 0;
  virtual WnWindow *AddSlider(const ScRect &area, i32 first, i32 second, i32 third, i32 fourth) = 0;
  virtual WnWindow *Scrollbar(const ScRect &area, i32 first, i32 second, i32 third, i32 fourth) = 0;
  virtual WnWindow *AddSpinner(const ScRect &area, i32 first, i32 second, i32 third) = 0;
  virtual WnWindow *AddText(const ScRect &area, const ScStringView &text, u32 flags, bool flag) = 0;
  virtual WnWindow *AddTextEdit(const ScRect &area, const ScStringView &text) = 0;
  virtual WnWindow *AddCheck(const ScRect &area, const ScStringView &text) = 0;
  virtual WnWindow *AddOptionGroup(const ScRect &area, const ScStringView &text) = 0;
  virtual WnWindow *AddLineGraph(const ScRect &area) = 0;
  virtual WnWindow *AddBarGraph(const ScRect &area) = 0;
  virtual WnWindow *AddScatterGraph(const ScRect &area) = 0;
  virtual ScRect GetBorderWidths() = 0;
  virtual WnDialogStyle GetStyle() = 0;
  virtual void SetCommandCallback(WnDialogCallback callback, WnDialogCallbackContext *context) = 0;
};

// Original executable cGZWinGenDlg: root at0, dialog interface at164 and complete x86 extent216.
// Unrecovered controls remain pure virtual until their genuine implementations are available.
class WnGeneralDialog : public UiWindow, public WnGeneralDialogView {
public:
  explicit WnGeneralDialog(WnDialogStyle style);
  ~WnGeneralDialog() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 command) override;
  bool SetBackground(GxBuffer *buffer) override;
  WnWindow *AddListBox(const ScRect &area) override;
  WnWindow *AddBMP(const ScRect &area, GxBuffer *buffer) override;
  WnWindow *AddBtn(const ScRect &area, const ScStringView &caption) override;
  WnWindow *AddCombo(const ScRect &area, bool flag) override;
  WnWindow *AddLineInput(const ScRect &area) override;
  WnWindow *AddSlider(const ScRect &area, i32 first, i32 second, i32 third, i32 fourth) override;
  WnWindow *Scrollbar(const ScRect &area, i32 first, i32 second, i32 third, i32 fourth) override;
  WnWindow *AddSpinner(const ScRect &area, i32 first, i32 second, i32 third) override;
  WnWindow *AddText(const ScRect &area, const ScStringView &text, u32 flags, bool flag) override;
  WnWindow *AddTextEdit(const ScRect &area, const ScStringView &text) override;
  WnWindow *AddCheck(const ScRect &area, const ScStringView &text) override;
  WnWindow *AddOptionGroup(const ScRect &area, const ScStringView &text) override;
  WnWindow *AddLineGraph(const ScRect &area) override;
  WnWindow *AddBarGraph(const ScRect &area) override;
  WnWindow *AddScatterGraph(const ScRect &area) override;
  ScRect GetBorderWidths() override;
  WnDialogStyle GetStyle() override;
  void SetCommandCallback(WnDialogCallback callback, WnDialogCallbackContext *context) override;

protected:
  FtFont *font, *title_font;
  bool owns_font;
  GxBuffer *background;
  bool owns_background;
  ScRect border_widths;
  WnDialogCallback command_callback;
  WnDialogCallbackContext *callback_context;
  WnDialogStyle style;
};

static_assert(sizeof(uptr) != 4 || sizeof(WnGeneralDialog) == 216);

class ApAnimationPlayer;
class GxPrimary;

// Retail cISC3WinDancingBear has a distinct Unknown identity followed by its two original methods.
class ApDancingBearView : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual i32 GetCurrentStatus() const = 0;
};

// Original executable intro window uses the primary root at zero and its secondary identity at x86 offset164.
class ApDancingBear : public WnRootWidget, public ApDancingBearView {
public:
  ApDancingBear();
  ~ApDancingBear() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  WnWindow *AsIGZWin() override;
  i32 GetCurrentStatus() const override;

protected:
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool InitIntroVideo();
  bool ContinueIntroVideo();
  bool EndIntroVideo();
  ApAnimationPlayer *animation;
  i32 intro_status;
  u16 video_x, video_y;
  GxPrimary *screen_buffer;
  u8 page_flipping, screen_buffer_locked;
  i32 saved_thread_priority;
};

static_assert(sizeof(uptr) != 4 || sizeof(ApDancingBear) == 192);

class FtFont;
class GxBuffer;
struct WnGraphObject;
struct WnGraphText;
struct WnGraphLine;
struct WnGraphRectangle;
struct WnGraphArrow;
struct WnGraphPicture;
enum class WnGraphAxis : i32;
enum class WnGraphUsage : i32;
enum class WnGraphRotation : i32;
enum class WnGraphOrientation : i32;
enum class WnBarType : i32;
enum class WnBarDirection : i32;

// The secondary interface follows the installed cGZGraph table.
// Unimplemented graph drawing and owned-object operations remain explicit virtual dependencies.
class WnGraphView : public ScUnknown {
public:
  virtual void SetBackgroundOutline(bool) = 0;
  virtual void SetBackgroundFillColor(u32) = 0;
  virtual void SetBackgroundOutlineColor(u32) = 0;
  virtual void SetBackgroundPattern(GxBuffer *) = 0;
  virtual void SetGraphAreaOutlineAndFill(bool, bool) = 0;
  virtual void SetGraphAreaOutlineColor(u32) = 0;
  virtual void SetGraphAreaFillColor(u32) = 0;
  virtual void SetGraphBackgroundPattern(GxBuffer *) = 0;
  virtual void SetGraphArea(const ScRect &) = 0;
  virtual void GetGraphArea(ScRect *) = 0;
  virtual void CompleteRedraw(GxBuffer *) = 0;
  virtual void SetAxisMinValue(WnGraphAxis, f32) = 0;
  virtual void SetAxisMaxValue(WnGraphAxis, f32) = 0;
  virtual void SetAxisZeroPosition(WnGraphAxis, i32) = 0;
  virtual void SetAxisLineColor(u32) = 0;
  virtual void SetAxisLabel(WnGraphAxis, const ScString &, bool) = 0;
  virtual void SetAxisLabelPosition(WnGraphAxis, ScRect *) = 0;
  virtual void SetAxisLabelFont(WnGraphAxis, i32, u32, u32, u32) = 0;
  virtual void SetAxisLabelFont(WnGraphAxis, FtFont *, u32) = 0;
  virtual void SetAxisLabelRotation(WnGraphAxis, WnGraphRotation) = 0;
  virtual void SetMajorTickUsage(WnGraphAxis, WnGraphUsage) = 0;
  virtual void SetMajorTickSpacing(WnGraphAxis, f32) = 0;
  virtual void SetMajorTickDivisions(WnGraphAxis, u32, bool) = 0;
  virtual void SetMajorTickGridUsage(WnGraphAxis, WnGraphUsage) = 0;
  virtual void SetMajorTickDecimalPlaces(WnGraphAxis, i32) = 0;
  virtual void SetMajorTickLength(WnGraphAxis, i32) = 0;
  virtual void SetAxisTickFont(WnGraphAxis, i32, u32, u32, u32) = 0;
  virtual void SetAxisTickFont(WnGraphAxis, FtFont *, u32) = 0;
  virtual void SetAxisTickTextDrawing(WnGraphAxis, WnGraphUsage) = 0;
  virtual void SetTickColor(u32) = 0;
  virtual void SetMinorTickUsage(WnGraphAxis, WnGraphUsage) = 0;
  virtual void SetMinorTickSpacing(WnGraphAxis, f32) = 0;
  virtual void SetMinorTickDivisions(WnGraphAxis, u32, bool) = 0;
  virtual void SetMinorTickGridUsage(WnGraphAxis, WnGraphUsage) = 0;
  virtual void SetMinorTickLength(WnGraphAxis, i32) = 0;
  virtual void SetTitle(const ScString &, bool) = 0;
  virtual void SetTitlePosition(ScRect *) = 0;
  virtual void SetTitleFont(i32, u32, u32, u32) = 0;
  virtual void SetTitleFont(FtFont *, u32) = 0;
  virtual bool DoHitTest(ScPoint *, u32 *) = 0;
  virtual bool AllocateHitTestData(u32) = 0;
  // Borrow the current hit-test storage; allocating a replacement or shutting down can invalidate it.
  virtual void *GetHitTestResultData() = 0;
  // Transfer the object to the graph, which deletes registered objects at shutdown.
  virtual void AddDynamicGraphObject(WnGraphObject *) = 0;
  // Detach without deleting; the caller takes responsibility for the removed object.
  virtual void RemoveDynamicGraphObject(WnGraphObject *) = 0;
  // These factories register an owned object and return a borrowed pointer to it.
  virtual WnGraphText *AddMiscGraphTextInfo() = 0;
  virtual WnGraphLine *AddMiscGraphLine() = 0;
  virtual WnGraphRectangle *AddMiscGraphRect() = 0;
  virtual WnGraphArrow *AddMiscGraphArrow() = 0;
  virtual WnGraphPicture *AddMiscGraphPicture() = 0;
};

// The real graph owns a root window and the secondary graph view at x86 offset 164.
// Graph drawing and specialized graph interfaces remain incomplete; no graph factory may construct this abstract base.
class WnGraph : public WnRootWidget, public WnGraphView {
public:
  WnGraph();
  ~WnGraph() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  using WnRootWidget::SetArea;
  void SetBackgroundOutline(bool) override;
  void SetBackgroundFillColor(u32) override;
  void SetBackgroundOutlineColor(u32) override;
  void SetBackgroundPattern(GxBuffer *) override;
  void SetGraphAreaOutlineAndFill(bool, bool) override;
  void SetGraphAreaOutlineColor(u32) override;
  void SetGraphAreaFillColor(u32) override;
  void SetGraphBackgroundPattern(GxBuffer *) override;
  void SetGraphArea(const ScRect &) override;
  void GetGraphArea(ScRect *) override;
  void CompleteRedraw(GxBuffer *) override;
  void SetAxisMinValue(WnGraphAxis, f32) override;
  void SetAxisMaxValue(WnGraphAxis, f32) override;
  void SetAxisZeroPosition(WnGraphAxis, i32) override;
  void SetAxisLineColor(u32) override;
  void SetAxisLabel(WnGraphAxis, const ScString &, bool) override;
  void SetAxisLabelPosition(WnGraphAxis, ScRect *) override;
  void SetAxisLabelFont(WnGraphAxis, i32, u32, u32, u32) override;
  using WnGraphView::SetAxisLabelFont;
  void SetAxisLabelFont(WnGraphAxis, FtFont *, u32) override;
  void SetAxisLabelRotation(WnGraphAxis, WnGraphRotation) override;
  void SetMajorTickUsage(WnGraphAxis, WnGraphUsage) override;
  void SetMajorTickSpacing(WnGraphAxis, f32) override;
  void SetMajorTickDivisions(WnGraphAxis, u32, bool) override;
  void SetMajorTickGridUsage(WnGraphAxis, WnGraphUsage) override;
  void SetMajorTickDecimalPlaces(WnGraphAxis, i32) override;
  void SetMajorTickLength(WnGraphAxis, i32) override;
  void SetAxisTickFont(WnGraphAxis, i32, u32, u32, u32) override;
  using WnGraphView::SetAxisTickFont;
  void SetAxisTickFont(WnGraphAxis, FtFont *, u32) override;
  void SetAxisTickTextDrawing(WnGraphAxis, WnGraphUsage) override;
  void SetTickColor(u32) override;
  void SetMinorTickUsage(WnGraphAxis, WnGraphUsage) override;
  void SetMinorTickSpacing(WnGraphAxis, f32) override;
  void SetMinorTickDivisions(WnGraphAxis, u32, bool) override;
  void SetMinorTickGridUsage(WnGraphAxis, WnGraphUsage) override;
  void SetMinorTickLength(WnGraphAxis, i32) override;
  void SetTitle(const ScString &, bool) override;
  void SetTitlePosition(ScRect *) override;
  void SetTitleFont(i32, u32, u32, u32) override;
  using WnGraphView::SetTitleFont;
  void SetTitleFont(FtFont *, u32) override;
  bool DoHitTest(ScPoint *, u32 *) override;
  bool AllocateHitTestData(u32) override;
  void *GetHitTestResultData() override;
  void AddDynamicGraphObject(WnGraphObject *) override;
  void RemoveDynamicGraphObject(WnGraphObject *) override;
  WnGraphText *AddMiscGraphTextInfo() override;
  WnGraphLine *AddMiscGraphLine() override;
  WnGraphRectangle *AddMiscGraphRect() override;
  WnGraphArrow *AddMiscGraphArrow() override;
  WnGraphPicture *AddMiscGraphPicture() override;

protected:
  // Original primary graph virtuals follow the inherited root destructor in the executable's table.
  virtual void LoadDefaultFonts();
  virtual void DrawBackground(GxBuffer *);
  virtual void DrawGraphAreaBackground(GxBuffer *);
  virtual void DrawAxes(GxBuffer *);
  virtual void DrawTicks(GxBuffer *, WnGraphAxis);
  virtual void DrawTickText(GxBuffer *, WnGraphAxis);
  virtual void DrawGraphData(GxBuffer *);
  virtual void DrawDynamicObjects(GxBuffer *);
  virtual void DrawTitles(GxBuffer *);
  virtual i32 ValueToPixel(WnGraphAxis, f32, bool);
  virtual f32 PixelToValue(WnGraphAxis, i32, bool);
  virtual i32 ValueAmountToPixelAmount(WnGraphAxis, f32);
  virtual f32 PixelAmountToValueAmount(WnGraphAxis, i32);
  virtual f32 GetHighestAxisValue(WnGraphAxis);
  virtual f32 GetLowestAxisValue(WnGraphAxis);
  virtual void CalculateAxisZeroPosition(WnGraphAxis);
  virtual i32 WindowPixelToGraphPixel(WnGraphAxis, i32);
  virtual i32 GraphPixelToWindowPixel(WnGraphAxis, i32);
  virtual i32 GetMaxTickLengthForAxis(WnGraphAxis);
  virtual void CommitDefaultValues();
  virtual void GetAutomaticTitleRect(ScRect *);
  virtual void GetAutomaticGraphAreaRect(ScRect *);
  virtual void GetAutomaticAxisLabelRect(WnGraphAxis, ScRect *);
  virtual void GetAutomaticAxisMinValue(WnGraphAxis, f32 *);
  virtual void GetAutomaticAxisMaxValue(WnGraphAxis, f32 *);
  virtual void GetAutomaticAxisMajorTickSpacing(WnGraphAxis, f32 *);
  virtual void GetAutomaticAxisMinorTickSpacing(WnGraphAxis, f32 *);

  u32 unknown_168;
  ScRect graph_area;
  bool unknown_188;
  ScString title;
  ScRect title_position;
  FtFont *title_font;
  u32 title_color, unknown_236, unknown_240, background_color;
  bool background_outline;
  u32 background_outline_color;
  bool graph_fill;
  u32 graph_fill_color;
  bool graph_outline;
  u32 graph_outline_color;
  GxBuffer *background_pattern, *graph_background_pattern;
  bool draw_axes, major_tick_usage[2], minor_tick_usage[2], major_tick_grid[2], minor_tick_grid[2],
      tick_text_drawing[2];
  u32 axis_line_color;
  f32 axis_min[2], axis_max[2], major_tick_spacing[2], minor_tick_spacing[2];
  i8 major_tick_decimals[2];
  i32 major_tick_length[2], minor_tick_length[2];
  u32 tick_color;
  i32 unknown_352[2], axis_zero_position[2];
  ScString axis_labels[2];
  ScRect axis_label_positions[2];
  u32 unknown_440, unknown_444;
  FtFont *axis_label_fonts[2];
  u32 axis_label_colors[2];
  FtFont *axis_tick_fonts[2];
  u32 axis_tick_colors[2];
  WnGraphRotation axis_label_rotations[2];
  u32 transparent_color;
  u8 *hit_data; // Owns the current allocation; graph-view callers borrow it.
  u32 hit_data_size;
  std::list<WnGraphObject *> objects; // Owns registered objects until explicitly detached or Shutdown deletes them.
};

// The executable's dynamic graph objects have a virtual destructor and a drawing operation.
struct WnGraphObject {
  virtual ~WnGraphObject();
  virtual void DrawSelf(WnGraphView *graph, GxBuffer *buffer);
  u32 unknown_4 = 0;
};

struct WnGraphLine : WnGraphObject {
  WnGraphLine();
  ~WnGraphLine() override;
  void DrawSelf(WnGraphView *graph, GxBuffer *buffer) override;
  i32 x1, y1, x2, y2, color;
  u32 thickness;
};

struct WnGraphRectangle : WnGraphObject {
  WnGraphRectangle();
  ~WnGraphRectangle() override;
  void DrawSelf(WnGraphView *graph, GxBuffer *buffer) override;
  ScRect rectangle;
  bool outline;
  u32 thickness, outline_color;
  bool fill;
  u32 fill_color;
};

// The executable dynamic text object retains its caption, styling and optional font.
struct WnGraphText : WnGraphObject {
  WnGraphText();
  ~WnGraphText() override;
  void DrawSelf(WnGraphView *, GxBuffer *) override;
  virtual bool Init();
  virtual void SetFont(FtFont *font, u32 color);
  ScString text;
  ScRect rectangle;
  u32 alignment;
  bool outline;
  u32 outline_color;
  bool fill;
  u32 fill_color;
  FtFont *font;
  u32 font_color;
};

struct WnGraphPicture : WnGraphObject {
  WnGraphPicture();
  ~WnGraphPicture() override;
  void DrawSelf(WnGraphView *, GxBuffer *) override;
  virtual bool Init();
  virtual void SetBuffer(GxBuffer *buffer);
  GxBuffer *buffer;
  i32 x, y;
  bool relative_to_graph;
};

struct WnGraphArrow : WnGraphObject {
  WnGraphArrow();
  ~WnGraphArrow() override;
  void DrawSelf(WnGraphView *, GxBuffer *) override;
  i32 x1, y1, x2, y2, color;
  u32 thickness;
  bool first_head, second_head;
  i32 head_length;
  f32 head_angle;
};

// The executable's LineGraphSeriesInfo copies these fields and its owned string at x86 offset 20.
struct WnLineSeriesInfo {
  WnLineSeriesInfo();
  u8 symbol_enabled, symbol_character;
  u32 symbol_size;
  u8 line_enabled;
  u32 color, unknown_16;
  ScString name;
};

// SC3U CodeView and the installed table establish the line graph's nested vectors and virtual order.
class WnLineGraphView : public ScUnknown {
public:
  virtual void SetGraphOrientation(WnGraphOrientation) = 0;
  virtual void SetSymbolFont(i32, u32, u32, u32) = 0;
  virtual void SetSymbolFont(FtFont *, u32) = 0;
  virtual void SetSeriesCount(u32) = 0;
  virtual void InsertSeries(u32) = 0;
  virtual void RemoveSeries(u32) = 0;
  virtual void RemoveAllSeries() = 0;
  virtual void SetSymbolUse(WnGraphUsage, WnGraphUsage, u32) = 0;
  virtual void SetSymbolCharacter(char, u32) = 0;
  virtual void SetSymbolSize(u32, u32) = 0;
  virtual void SetSeriesColor(u32, u32) = 0;
  virtual void SetSeriesName(const ScString &, u32) = 0;
  virtual void SetSeriesInfo(WnLineSeriesInfo *, u32) = 0;
  virtual void InsertSeriesInfo(WnLineSeriesInfo *, u32) = 0;
  virtual void RemoveSeriesInfo(u32) = 0;
  virtual void InsertCategoryName(const ScString &, u32) = 0;
  virtual void RemoveCategoryName(u32) = 0;
  virtual void InsertData(f32, u32, u32) = 0;
  virtual void RemoveData(u32, u32) = 0;
  virtual void SetData(f32, u32, u32) = 0;
  virtual void SetData(std::vector<f32> *, u32) = 0;
  virtual void AppendData(std::vector<f32> *, u32) = 0;
  virtual f32 GetHighestAxisValue(WnGraphAxis) = 0;
  virtual f32 GetLowestAxisValue(WnGraphAxis) = 0;
  virtual void CompleteRedraw(GxBuffer *) = 0;
  virtual void DrawGraphData(GxBuffer *) = 0;
  virtual void LoadDefaultFonts() = 0;
  virtual i32 GetCenterOfDataPoint(u32) = 0;
  virtual void DrawSymbol(GxBuffer *, ScPoint *, u32) = 0;
  virtual void DrawLine(GxBuffer *, ScRect *, u32) = 0;
};

// Unrecovered series creation, specialized rendering, and inherited graph methods keep this class abstract.
class WnLineGraph : public WnGraph, public WnLineGraphView {
public:
  WnLineGraph();
  ~WnLineGraph() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void SetGraphOrientation(WnGraphOrientation) override;
  void SetSymbolFont(i32, u32, u32, u32) override;
  using WnLineGraphView::SetSymbolFont;
  void SetSymbolFont(FtFont *, u32) override;
  void SetSeriesCount(u32) override;
  void InsertSeries(u32) override;
  void RemoveSeries(u32) override;
  void RemoveAllSeries() override;
  void SetSymbolUse(WnGraphUsage, WnGraphUsage, u32) override;
  void SetSymbolCharacter(char, u32) override;
  void SetSymbolSize(u32, u32) override;
  void SetSeriesColor(u32, u32) override;
  void SetSeriesName(const ScString &, u32) override;
  void SetSeriesInfo(WnLineSeriesInfo *, u32) override;
  void InsertSeriesInfo(WnLineSeriesInfo *, u32) override;
  void RemoveSeriesInfo(u32) override;
  void InsertCategoryName(const ScString &, u32) override;
  void RemoveCategoryName(u32) override;
  void InsertData(f32, u32, u32) override;
  void RemoveData(u32, u32) override;
  using WnLineGraphView::SetData;
  void SetData(f32, u32, u32) override;
  void SetData(std::vector<f32> *, u32) override;
  void AppendData(std::vector<f32> *, u32) override;
  f32 GetHighestAxisValue(WnGraphAxis) override;
  f32 GetLowestAxisValue(WnGraphAxis) override;
  void CompleteRedraw(GxBuffer *) override;
  void DrawGraphData(GxBuffer *) override;
  void LoadDefaultFonts() override;
  i32 GetCenterOfDataPoint(u32) override;
  void DrawSymbol(GxBuffer *, ScPoint *, u32) override;
  void DrawLine(GxBuffer *, ScRect *, u32) override;
  static void SetDefaultSeriesInfo();

protected:
  void DrawTicks(GxBuffer *, WnGraphAxis) override;
  void DrawTickText(GxBuffer *, WnGraphAxis) override;
  WnGraphOrientation orientation;
  std::vector<std::vector<f32>> series;
  std::vector<WnLineSeriesInfo> series_info;
  std::vector<ScString> categories;
  FtFont *symbol_font;
  u32 symbol_color;
  static std::vector<WnLineSeriesInfo> default_series_info;
};

// The executable's float point and scatter-series record contain the original two coordinates and owning name.
struct ScVec2F {
  f32 x, y;
};

struct WnScatterSeriesInfo {
  u8 symbol_character = 'A';
  u32 symbol_size = 12, color = 0;
  ScString name;
};

// Installed SC3U table 004D5FC0 establishes this secondary scatter-graph interface order.
class WnScatterGraphView : public ScUnknown {
public:
  virtual void SetSymbolFont(i32, u32, u32, u32) = 0;
  virtual void SetSymbolFont(FtFont *, u32) = 0;
  virtual void SetSeriesCount(u32) = 0;
  virtual void InsertSeries(u32) = 0;
  virtual void RemoveSeries(u32) = 0;
  virtual void SetSymbolCharacter(char, u32) = 0;
  virtual void SetSymbolSize(u32, u32) = 0;
  virtual void SetSeriesColor(u32, u32) = 0;
  virtual void SetSeriesName(const ScString &, u32) = 0;
  virtual void SetSeriesInfo(WnScatterSeriesInfo *, u32) = 0;
  virtual void InsertSeriesInfo(WnScatterSeriesInfo *, u32) = 0;
  virtual void RemoveSeriesInfo(u32) = 0;
  virtual void InsertData(const ScVec2F &, u32, u32) = 0;
  virtual void InsertData(f32, f32, u32, u32) = 0;
  virtual void RemoveData(u32, u32) = 0;
  virtual void SetData(const ScVec2F &, u32, u32) = 0;
  virtual void SetData(f32, f32, u32, u32) = 0;
  virtual void SetData(const std::vector<ScVec2F> &, u32) = 0;
  virtual void AppendData(const std::vector<ScVec2F> &, u32) = 0;
  virtual void GetAutomaticGraphAreaRect(ScRect *) = 0;
  virtual void GetAutomaticAxisMinValue(WnGraphAxis, f32 *) = 0;
  virtual void GetAutomaticAxisMaxValue(WnGraphAxis, f32 *) = 0;
  virtual f32 GetHighestAxisValue(WnGraphAxis) = 0;
  virtual f32 GetLowestAxisValue(WnGraphAxis) = 0;
  virtual void CompleteRedraw(GxBuffer *) = 0;
  virtual void DrawGraphData(GxBuffer *) = 0;
  virtual void LoadDefaultFonts() = 0;
  virtual void DrawSymbol(GxBuffer *, ScPoint *, u32) = 0;
};

// Original scatter graph adds its own point and series vectors and one retained symbol font.
class WnScatterGraph : public WnGraph, public WnScatterGraphView {
public:
  WnScatterGraph();
  ~WnScatterGraph() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void SetSymbolFont(i32, u32, u32, u32) override;
  using WnScatterGraphView::SetSymbolFont;
  void SetSymbolFont(FtFont *, u32) override;
  void SetSeriesCount(u32) override;
  void InsertSeries(u32) override;
  void RemoveSeries(u32) override;
  void SetSymbolCharacter(char, u32) override;
  void SetSymbolSize(u32, u32) override;
  void SetSeriesColor(u32, u32) override;
  void SetSeriesName(const ScString &, u32) override;
  void SetSeriesInfo(WnScatterSeriesInfo *, u32) override;
  void InsertSeriesInfo(WnScatterSeriesInfo *, u32) override;
  void RemoveSeriesInfo(u32) override;
  using WnScatterGraphView::InsertData;
  void InsertData(const ScVec2F &, u32, u32) override;
  void InsertData(f32, f32, u32, u32) override;
  void RemoveData(u32, u32) override;
  using WnScatterGraphView::SetData;
  void SetData(const ScVec2F &, u32, u32) override;
  void SetData(f32, f32, u32, u32) override;
  void SetData(const std::vector<ScVec2F> &, u32) override;
  void AppendData(const std::vector<ScVec2F> &, u32) override;
  void GetAutomaticAxisMinValue(WnGraphAxis, f32 *) override;
  void GetAutomaticAxisMaxValue(WnGraphAxis, f32 *) override;
  void GetAutomaticGraphAreaRect(ScRect *) override;
  f32 GetHighestAxisValue(WnGraphAxis) override;
  f32 GetLowestAxisValue(WnGraphAxis) override;
  void CompleteRedraw(GxBuffer *) override;
  void DrawGraphData(GxBuffer *) override;
  void LoadDefaultFonts() override;
  void DrawSymbol(GxBuffer *, ScPoint *, u32) override;
  static void SetDefaultSeriesInfo();

protected:
  u8 shared_axis_scale;
  std::vector<std::vector<ScVec2F>> series;
  std::vector<WnScatterSeriesInfo> series_info;
  FtFont *symbol_font;
  u32 symbol_color;
  static std::vector<WnScatterSeriesInfo> default_series_info;
};

// The executable copies this 40-byte x86 series record, including its borrowed pattern and owning name.
struct WnBarSeriesInfo {
  WnBarSeriesInfo();
  u8 outline_enabled;
  u32 outline_color;
  u8 fill_enabled;
  u32 fill_color;
  GxBuffer *pattern;
  ScString name;
};

// Original bar-graph secondary table, with mutable references expressed as pointers.
class WnBarGraphView : public ScUnknown {
public:
  virtual void SetBarGraphType(WnBarType) = 0;
  virtual void SetGraphOrientation(WnGraphOrientation) = 0;
  virtual void SetSeriesCount(u32) = 0;
  virtual void InsertSeries(u32) = 0;
  virtual void RemoveSeries(u32) = 0;
  virtual void SetSeriesName(const ScString &, u32) = 0;
  virtual void SetSeriesInfo(WnBarSeriesInfo *, u32) = 0;
  virtual void InsertSeriesInfo(WnBarSeriesInfo *, u32) = 0;
  virtual void RemoveSeriesInfo(u32) = 0;
  virtual void InsertCategoryName(const ScString &, u32) = 0;
  virtual void RemoveCategoryName(u32) = 0;
  virtual void SetBarColorUse(WnGraphUsage, WnGraphUsage, u32) = 0;
  virtual void SetBarFillColor(u32, u32) = 0;
  virtual void SetBarOutlineColor(u32, u32) = 0;
  virtual void SetBarPattern(GxBuffer *, u32) = 0;
  virtual void SetBarWidthPercentage(f32) = 0;
  virtual void InsertData(f32, u32, u32) = 0;
  virtual void RemoveData(u32, u32) = 0;
  virtual void SetData(f32, u32, u32) = 0;
  virtual void SetData(std::vector<f32> *, u32) = 0;
  virtual void AppendData(std::vector<f32> *, u32) = 0;
  virtual i32 GetLeftOfBar(u32, u32) = 0;
  virtual i32 GetRightOfBar(u32, u32) = 0;
  virtual i32 GetCenterOfBar(u32, u32) = 0;
  virtual bool GetRectOfBar(u32, u32, ScRect *) = 0;
  virtual bool GetRectOfBarsInCategory(u32, ScRect *) = 0;
  virtual bool DoHitTest(ScPoint *, u32 *) = 0;
  virtual void CompleteRedraw(GxBuffer *) = 0;
  virtual void DrawGraphData(GxBuffer *) = 0;
  virtual void DrawAllBars(GxBuffer *) = 0;
  virtual void DrawBar(GxBuffer *, ScRect *, u32, WnBarDirection) = 0;
  virtual f32 GetHighestAxisValue(WnGraphAxis) = 0;
  virtual f32 GetLowestAxisValue(WnGraphAxis) = 0;
  virtual void CalculateBarWidthAndSpacing() = 0;
  virtual u32 CalculateMaximumBarWidth() = 0;
};

// The original bar graph owns nested data, series settings, category names, and three width/spacing floats.
class WnBarGraph : public WnGraph, public WnBarGraphView {
public:
  WnBarGraph();
  ~WnBarGraph() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  void SetBarGraphType(WnBarType) override;
  void SetGraphOrientation(WnGraphOrientation) override;
  void SetSeriesCount(u32) override;
  void InsertSeries(u32) override;
  void RemoveSeries(u32) override;
  void SetSeriesName(const ScString &, u32) override;
  void SetSeriesInfo(WnBarSeriesInfo *, u32) override;
  void InsertSeriesInfo(WnBarSeriesInfo *, u32) override;
  void RemoveSeriesInfo(u32) override;
  void InsertCategoryName(const ScString &, u32) override;
  void RemoveCategoryName(u32) override;
  void SetBarColorUse(WnGraphUsage, WnGraphUsage, u32) override;
  void SetBarFillColor(u32, u32) override;
  void SetBarOutlineColor(u32, u32) override;
  void SetBarPattern(GxBuffer *, u32) override;
  void SetBarWidthPercentage(f32) override;
  void InsertData(f32, u32, u32) override;
  void RemoveData(u32, u32) override;
  using WnBarGraphView::SetData;
  void SetData(f32, u32, u32) override;
  void SetData(std::vector<f32> *, u32) override;
  void AppendData(std::vector<f32> *, u32) override;
  i32 GetLeftOfBar(u32, u32) override;
  i32 GetRightOfBar(u32, u32) override;
  i32 GetCenterOfBar(u32, u32) override;
  bool GetRectOfBar(u32, u32, ScRect *) override;
  bool GetRectOfBarsInCategory(u32, ScRect *) override;
  bool DoHitTest(ScPoint *, u32 *) override;
  void CompleteRedraw(GxBuffer *) override;
  void DrawGraphData(GxBuffer *) override;
  void DrawAllBars(GxBuffer *) override;
  void DrawBar(GxBuffer *, ScRect *, u32, WnBarDirection) override;
  f32 GetHighestAxisValue(WnGraphAxis) override;
  f32 GetLowestAxisValue(WnGraphAxis) override;
  void CalculateBarWidthAndSpacing() override;
  u32 CalculateMaximumBarWidth() override;
  static void SetDefaultSeriesInfo();

protected:
  void DrawTicks(GxBuffer *, WnGraphAxis) override;
  void DrawTickText(GxBuffer *, WnGraphAxis) override;
  WnBarType type;
  WnGraphOrientation orientation;
  std::vector<std::vector<f32>> series;
  std::vector<WnBarSeriesInfo> series_info;
  std::vector<ScString> categories;
  f32 bar_width, bar_spacing, bar_width_percentage;
  static std::vector<WnBarSeriesInfo> default_series_info;
};

class GxBuffer;
class ScStringView;

// Executable cISC3ImageList stores the original ordered map from signed identifiers to buffer pointers.
// Retail CodeView identifies map<long, cIGZBuffer *> and its unique-insertion operation.
class UiImageList {
public:
  UiImageList();
  ~UiImageList();
  bool AddImage(ScStringView *name, i32 first, i32 second);
  bool RemoveImage(i32 identifier);
  bool AddImage(i32 identifier, GxBuffer *buffer);
  bool DrawImage(i32 identifier, GxBuffer *target, i32 *x, i32 y);

private:
  std::map<i32, GxBuffer *> images;
};

class GxBuffer;
class FtFont;
class ScStringView;
class WnResources;

// The executable's copyright window extends its own window base by seven DWORD fields.
class UiLogoWindow : public UiWindow {
public:
  UiLogoWindow();
  ~UiLogoWindow() override;
  bool Init() override;
  bool GZPaint() override;
  bool Shutdown() override;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  WnWindow *AsIGZWin();
  bool SetWaitType(i32 value);
  bool SetBuffer(GxBuffer *buffer, bool fit);
  bool SetLabelText(i32 index, const ScStringView *text);

protected:
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;

private:
  bool Layout(i32 width, i32 height);
  bool IsTimedOut() const;
  bool EndDialog(u32 result);
  bool IsValidLabelId(i32 index) const;
  bool CreateSpecialLabel(WnResources *resources, i32 index, u32 color, FtFont *font);
  bool SyncShadowArea(u32 identifier);
  i32 wait_type;
  u32 paint_count;
  GxBuffer *buffer;
  const ScStringView *labels[2];
  u32 identifiers[2];
};

class FtFont;
class GxBuffer;

// The original tip descriptor carries flags, borrowed text, four coordinates, three colors and a mutable token.
struct UiTipInfo {
  u32 unknown, flags;
  ScStringView *text;
  i32 coordinates[4];
  u32 foreground, background, line, identifier;
};

// Installed cIGZWinToolTip secondary identity, IID 22C010CF.
class UiToolTipControl : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
  virtual bool SetFont(FtFont *font, i8 horizontal, i8 vertical) = 0;
  virtual bool CopyTipInfo(const UiTipInfo &info) = 0;
};

// Original application tooltip window, including its owned offscreen surface.
class UiToolTipWindow : public UiWindow, public UiToolTipControl {
public:
  UiToolTipWindow();
  ~UiToolTipWindow() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  static ScUnknown *CreateNewObject();
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool IsPointInWindow(i32 x, i32 y) const override;
  bool IsPointInWindowParentCoordinates(i32 x, i32 y) const override;
  WnWindow *AsIGZWin() override;
  bool SetFont(FtFont *font, i8 horizontal, i8 vertical) override;
  bool CopyTipInfo(const UiTipInfo &info) override;

protected:
  virtual bool UpdateOffscreenBuffer();
  virtual void TranslateColor(u16 *out, u32 color);
  FtFont *font;
  i8 horizontal, vertical;
  GxBuffer *offscreen;
  ScRect source;
  i32 coordinates[4];
  u16 foreground, background, line;
  u32 tip_flags;
};

// Original cGZWinToolTipMgr owns its timer and tracks a single current tooltip token.
class UiToolTipController : public UiToolTipManager {
public:
  UiToolTipController();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool Init(FtFont *font, i8 horizontal, i8 vertical) override;
  bool Shutdown() override;

private:
  virtual ~UiToolTipController();
  UiToolTipControl *GetTipWindow();
  bool ShowTip(u32 identifier);
  bool HideTip(u32 identifier);
  bool PostMsgToTipWindow(u32 type, u32 first, u32 second);
  bool OnTipStart(ScMessage *message);
  bool OnTipUpdate(const ScMessage &message);
  bool OnTipStop(const ScMessage &message);
  bool OnTipForecolor(const ScMessage &message);
  bool OnTipBackcolor(const ScMessage &message);
  bool OnTipLinecolor(const ScMessage &message);
  bool OnTimerTicked(const ScMessage &message);
  static void StaticTipTimerCallback(uptr argument);
  i32 references;
  bool initialized;
  ScCallbackTimer timer;
  u32 start_ticks, stop_ticks, elapsed;
  bool visible;
  u32 next_identifier, current_identifier;
};

class WnWindowManager;
class SpCityWindowView;
class SpViewInterface;
class UiCityCursor;
class EcBudgetLayer;
class SpSelection;
class ScOccupantManager;

namespace ApRuntime {
// Executable copy of the original common city-view selection handler; identity remains with concrete subclasses.
class ScSelectionHandler : public SpViewHandler {
public:
  ScSelectionHandler();
  virtual ~ScSelectionHandler();
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnKeyDown(u32 key, u32 flags) override;
  bool OnKeyUp(u32 key, u32 flags) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseDownR(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpR(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  bool GetCursor(u32 *out) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  bool Dettach(SpCityWindowView *window);
  bool Attach(SpCityWindowView *window);
  void ClearStatus();
  void SetStatus(i32 status, bool enabled);
  void UpdateStatusDisplay();
  void CheckBudget();

protected:
  WnWindowManager *manager;
  SpCityWindowView *window;
  SpViewInterface *view;
  UiCityCursor *cursor;
  EcBudgetLayer *budget;
  GeBounds bounds;
  u8 panning, cursor_visible, edge_scroll, cost_visible;
  i32 cost;
  u8 invalid, unaffordable, conflict, retiring;
};

// Original point-selection extension stores its drag flag and two distinct city coordinates.
class ScPointSelectionHandler : public ScSelectionHandler {
public:
  ScPointSelectionHandler();
  ~ScPointSelectionHandler() override;
  bool DoMessage(ScMessage *message) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(const CtCoord &point);
  virtual bool ExecuteOnMouseDrag(const CtCoord &point);
  virtual bool ExecuteOnMouseMove(const CtCoord &point);
  virtual bool ExecuteCellDraw(const CtCoord &point, GeBounds *damage);
  virtual void ExecuteOnResetState();

protected:
  u8 dragging;
  CtCoord current, previous;
};

// Executable rectangle-selection handler owns two original bounds and a drag flag.
class ScRectSelectionHandler : public ScSelectionHandler {
public:
  ScRectSelectionHandler();
  ~ScRectSelectionHandler() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(const GeBounds &rectangle);
  virtual bool ExecuteOnMouseDrag(const GeBounds &rectangle);
  virtual bool ExecuteOnMouseMove(const CtCoord &point);
  virtual bool ExecuteRectDraw(const GeBounds &rectangle, GeBounds *damage);
  virtual void ExecuteOnResetState();

protected:
  u8 dragging;
  GeBounds rectangle, previous_rectangle;
};

// Executable line-selection handler retains the endpoints and prior coordinate of a drag.
class ScLineSelectionHandler : public ScSelectionHandler {
public:
  ScLineSelectionHandler();
  ~ScLineSelectionHandler() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(SpSelection *selection);
  virtual bool ExecuteOnMouseDrag(SpSelection *selection);
  virtual bool ExecuteOnMouseMove(SpSelection *selection);
  virtual bool ExecuteLineDraw(const CtCoord &first, const CtCoord &second, SpSelection **selection);
  virtual void ExecuteOnResetState();

protected:
  u8 dragging;
  CtCoord first, second, previous;
};

// Executable occupant-selection handler stores its picked coordinate and previous cursor position.
class ScOccupantSelectionHandler : public ScSelectionHandler {
public:
  ScOccupantSelectionHandler();
  ~ScOccupantSelectionHandler() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  virtual bool ExecuteOnMouseDown(const CtCoord &point);
  virtual bool ExecuteOnMouseUp(const CtCoord &point);
  virtual bool ExecuteOnMouseMove(const CtCoord &point);
  virtual bool ExecuteOccDraw(const CtCoord &point, GeBounds *damage);
  virtual void ExecuteOnResetState();
  virtual bool Dettach(SpCityWindowView *window);

protected:
  u8 dragging;
  CtCoord current, previous;
};

// Executable occupant-area handler tracks both selected bounds and their combined preview.
class ScOccupantAreaSelectionHandler : public ScSelectionHandler {
public:
  ScOccupantAreaSelectionHandler();
  ~ScOccupantAreaSelectionHandler() override;
  bool DoMessage(ScMessage *message) override;
  bool OnMouseDownL(i32 x, i32 y, u32 flags) override;
  bool OnMouseUpL(i32 x, i32 y, u32 flags) override;
  bool OnMouseMove(i32 x, i32 y, u32 flags) override;
  void OnResetState() override;
  bool IsValidHandlerByView(i32 view_kind, u32 flags) const override;
  virtual bool ExecuteOnMouseDown(const GeBounds &selection);
  virtual bool ExecuteOnMouseUp(const GeBounds &selection);
  virtual bool ExecuteOnMouseDrag(const GeBounds &selection);
  virtual bool ExecuteOnMouseMove(const GeBounds &selection);
  virtual bool ExecuteRectDraw(const GeBounds &selection, GeBounds *damage);
  virtual void ExecuteOnResetState();
  virtual bool Dettach(SpCityWindowView *window);

protected:
  u8 dragging;
  GeBounds first, second, combined;
  CtCoord previous;
  ScOccupantManager *occupants;
};
} // namespace ApRuntime

class WnTextControl;

// Executable cSC3CheatCodeEntryDialog extends the general dialog with one retained input interface.
class ApCheatDialog : public WnGeneralDialog {
public:
  ApCheatDialog();
  ~ApCheatDialog() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 command) override;
  void PlayCheatCodeSound(bool success);

protected:
  WnTextControl *input;
};

static_assert(sizeof(uptr) != 4 || sizeof(ApCheatDialog) == 220);

// Retail cSC3WinCredits owns the original list<cRZString>, timer and three retained graphics buffers.
class ApCreditsWindow : public WnGeneralDialog {
public:
  ApCreditsWindow();
  ~ApCreditsWindow() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;

protected:
  GxBuffer *fade_buffer, *text_buffer, *logo_buffer;
  FtFont *title_font, *names_font;
  ScStopwatch timer;
  u32 start_time;
  i32 next_line_space;
  f32 scroll_rate;
  u32 minimum_scroll, maximum_scroll;
  std::list<ScString> lines;
  std::list<ScString>::iterator current;
  ScRect fade_in, fade_out;
  u32 title_color, names_color;
};

#endif
