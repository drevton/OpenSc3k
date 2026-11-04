#ifndef SC3_UI_GRAPH_WINDOW_H
#define SC3_UI_GRAPH_WINDOW_H

#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"

// Graph text uses the current font color for this sentinel; fill defaults retain their original stored colors.
inline constexpr u32 kUiGraphUseFontColor = 0x80000000;
inline constexpr u32 kUiGraphInitialFillColor = 0x12345678;
inline constexpr u32 kUiGraphDefaultFillColor = 0xffffffff;

// Graph selector meanings are distinct even where the original transport uses the same all-ones value.
enum UiGraphSelector : u32 {
  kUiOnlyGraphSeries = 0xffffffff,
  kUiOnlyGraphValue = 0xffffffff,
  kUiAllGraphSeries = 0xffffffff,
  kUiAppendGraphSeries = 0xffffffff,
  kUiAppendGraphValue = 0xffffffff,
  kUiAppendGraphCategory = 0xffffffff,
  kUiLastGraphSeries = 0xffffffff,
  kUiLastGraphCategory = 0xffffffff,
};

class FtFont;
class GxBuffer;
class ScString;
struct ScPoint;
struct ScRect;
struct UiGraphArrow;
struct UiGraphDynamicObject;
struct UiGraphLineInfo;
struct UiGraphPicture;
struct UiGraphRectInfo;
struct UiGraphTextInfo;

// Each graph data series stores a range of floating-point values in the original vector layout.
struct UiGraphFloatVector {
  f32 *begin, *end, *storage_end;
};
enum class UiGraphAxis : i32;
enum class UiGraphRotation : i32;
enum class UiGraphUsage : i32;

// The original graph interface begins at the graph window's secondary identity.
// Private object layouts and concrete graph rendering remain unrecovered.
class UiGraph : public ScUnknown {
public:
  virtual void SetBackgroundOutline(bool enabled) = 0;
  virtual void SetBackgroundFillColor(u32 color) = 0;
  virtual void SetBackgroundOutlineColor(u32 color) = 0;
  virtual void SetBackgroundPattern(GxBuffer *pattern) = 0;
  virtual void SetGraphAreaOutlineAndFill(bool outline, bool fill) = 0;
  virtual void SetGraphAreaOutlineColor(u32 color) = 0;
  virtual void SetGraphAreaFillColor(u32 color) = 0;
  virtual void SetGraphBackgroundPattern(GxBuffer *pattern) = 0;
  virtual void SetGraphArea(const ScRect &area) = 0;
  virtual void GetGraphArea(ScRect *area) = 0;
  virtual void CompleteRedraw(GxBuffer *buffer) = 0;
  virtual void SetAxisMinValue(UiGraphAxis axis, f32 value) = 0;
  virtual void SetAxisMaxValue(UiGraphAxis axis, f32 value) = 0;
  virtual void SetAxisZeroPosition(UiGraphAxis axis, i32 position) = 0;
  virtual void SetAxisLineColor(u32 color) = 0;
  virtual void SetAxisLabel(UiGraphAxis axis, const ScString &label, bool enabled) = 0;
  virtual void SetAxisLabelPosition(UiGraphAxis axis, ScRect *position) = 0;
  virtual void SetAxisLabelFont(UiGraphAxis axis, i32 size, u32 flags, u32 group, u32 instance) = 0;
  virtual void SetAxisLabelFont(UiGraphAxis axis, FtFont *font, u32 color) = 0;
  virtual void SetAxisLabelRotation(UiGraphAxis axis, UiGraphRotation rotation) = 0;
  virtual void SetMajorTickUsage(UiGraphAxis axis, UiGraphUsage usage) = 0;
  virtual void SetMajorTickSpacing(UiGraphAxis axis, f32 spacing) = 0;
  virtual void SetMajorTickDivisions(UiGraphAxis axis, u32 divisions, bool enabled) = 0;
  virtual void SetMajorTickGridUsage(UiGraphAxis axis, UiGraphUsage usage) = 0;
  virtual void SetMajorTickDecimalPlaces(UiGraphAxis axis, i32 places) = 0;
  virtual void SetMajorTickLength(UiGraphAxis axis, i32 length) = 0;
  virtual void SetAxisTickFont(UiGraphAxis axis, i32 size, u32 flags, u32 group, u32 instance) = 0;
  virtual void SetAxisTickFont(UiGraphAxis axis, FtFont *font, u32 color) = 0;
  virtual void SetAxisTickTextDrawing(UiGraphAxis axis, UiGraphUsage usage) = 0;
  virtual void SetTickColor(u32 color) = 0;
  virtual void SetMinorTickUsage(UiGraphAxis axis, UiGraphUsage usage) = 0;
  virtual void SetMinorTickSpacing(UiGraphAxis axis, f32 spacing) = 0;
  virtual void SetMinorTickDivisions(UiGraphAxis axis, u32 divisions, bool enabled) = 0;
  virtual void SetMinorTickGridUsage(UiGraphAxis axis, UiGraphUsage usage) = 0;
  virtual void SetMinorTickLength(UiGraphAxis axis, i32 length) = 0;
  virtual void SetTitle(const ScString &title, bool enabled) = 0;
  virtual void SetTitlePosition(ScRect *position) = 0;
  virtual void SetTitleFont(i32 size, u32 flags, u32 group, u32 instance) = 0;
  virtual void SetTitleFont(FtFont *font, u32 color) = 0;
  virtual bool DoHitTest(ScPoint *point, u32 *result) = 0;
  virtual bool AllocateHitTestData(u32 size) = 0;
  virtual void *GetHitTestResultData() = 0;
  virtual void AddDynamicGraphObject(UiGraphDynamicObject *object) = 0;
  virtual void RemoveDynamicGraphObject(UiGraphDynamicObject *object) = 0;
  virtual UiGraphTextInfo *AddMiscGraphTextInfo() = 0;
  virtual UiGraphLineInfo *AddMiscGraphLine() = 0;
  virtual UiGraphRectInfo *AddMiscGraphRect() = 0;
  virtual UiGraphArrow *AddMiscGraphArrow() = 0;
  virtual UiGraphPicture *AddMiscGraphPicture() = 0;
};

// The original graph owns these separately allocated drawable objects through a virtual destructor.
struct UiGraphDynamicObject {
  virtual ~UiGraphDynamicObject();
  virtual void DrawSelf(UiGraph *graph, GxBuffer *buffer);
};

// The original line record has a reserved word followed by endpoints, color and thickness.
struct UiGraphLineInfo : UiGraphDynamicObject {
  UiGraphLineInfo();
  ~UiGraphLineInfo() override;
  void DrawSelf(UiGraph *graph, GxBuffer *buffer) override;
  u32 unknown4;
  ScPoint first, second;
  u32 color, thickness;
};

// The original rectangle record independently controls its outline and fill passes.
struct UiGraphRectInfo : UiGraphDynamicObject {
  UiGraphRectInfo();
  ~UiGraphRectInfo() override;
  void DrawSelf(UiGraph *graph, GxBuffer *buffer) override;
  u32 unknown4;
  ScRect area;
  u8 outline;
  u32 thickness, outline_color;
  u8 fill;
  u32 fill_color;
};

// The arrow record stores a segment, independent end flags and its head dimensions.
struct UiGraphArrow : UiGraphDynamicObject {
  UiGraphArrow();
  ~UiGraphArrow() override;
  void DrawSelf(UiGraph *graph, GxBuffer *buffer) override;
  u32 unknown4;
  ScPoint first, second;
  u32 color, thickness;
  u8 first_head, second_head;
  u32 head_length;
  f32 head_angle;
};

// A text record owns its text and font and optionally draws a filled and outlined background.
struct UiGraphTextInfo : UiGraphDynamicObject {
  UiGraphTextInfo();
  ~UiGraphTextInfo() override;
  void DrawSelf(UiGraph *graph, GxBuffer *buffer) override;
  virtual bool Init();
  virtual void SetFont(FtFont *font, u32 color);
  u32 unknown4;
  ScString text;
  ScRect area;
  u32 alignment;
  u8 outline;
  u32 outline_color;
  u8 fill;
  u32 fill_color;
  FtFont *font;
  u32 font_color;
};

// The picture retains a buffer and optionally offsets its destination by the graph area.
struct UiGraphPicture : UiGraphDynamicObject {
  UiGraphPicture();
  ~UiGraphPicture() override;
  void DrawSelf(UiGraph *graph, GxBuffer *buffer) override;
  virtual bool Init();
  virtual void SetBuffer(GxBuffer *buffer);
  u32 unknown4;
  GxBuffer *image;
  i32 x, y;
  u8 relative_to_graph;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiGraphLineInfo) == 32);
static_assert(sizeof(uptr) != 4 || sizeof(UiGraphRectInfo) == 44);
static_assert(sizeof(uptr) != 4 || sizeof(UiGraphArrow) == 44);
static_assert(sizeof(uptr) != 4 || sizeof(UiGraphTextInfo) == 72);
static_assert(sizeof(uptr) != 4 || sizeof(UiGraphPicture) == 24);

#endif
