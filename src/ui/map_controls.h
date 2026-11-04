#ifndef SC3_UI_MAP_CONTROLS_H
#define SC3_UI_MAP_CONTROLS_H

#include "common/sc3.h"
#include "graphics/api.h"
#include "ui/api.h"
#include "ui/controls.h"

// SIMUI's buffer-backed filter owns one retained buffer; its clone's null-buffer path is undefined in the source.
class UiMapBufferFilter : public UiMapFilter {
public:
  UiMapBufferFilter(u32 identifier, GxBuffer *buffer);
  ~UiMapBufferFilter() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetFilterId(u32 identifier) override;
  u32 GetFilterId() override;
  u32 GetWidth() override;
  u32 GetHeight() override;
  u8 SetVisible(bool enabled) override;
  u8 IsVisible() override;
  u8 SetPalette(GxPaletteView *palette) override;
  GxPaletteView *GetPalette() override;
  u8 Draw(GxBuffer *destination, ScRect *area, ScRect *clip) override;
  u8 CreateBuffer(u32 width, u32 height, GxBuffer **out) override;
  u8 CloneFilter(UiMapFilter **out) override;
  void SetPostFilter(bool enabled) override;
  void SetDestinationCoords(i32 x0, i32 y0, i32 x1, i32 y1, i32 x3, i32 y3) override;
  u8 IsPostFilter() const override;

protected:
  u32 references, identifier;
  GxBuffer *buffer;
  u8 visible;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapBufferFilter) == 20);

struct UiMapPaletteNode;
struct UiMapFilterNode;

struct UiMapPoint {
  i16 x, y;
};

// Actual SIMUI map control with its secondary interface at164 on x86.
// The original map control combines its primary window with the secondary map interface.
class UiMapControlObject : public UiBaseWindow, public UiMapControl {
public:
  UiMapControlObject();
  ~UiMapControlObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  u32 SetFlag(u32 mask, bool enabled) override;
  u8 CreateControl(u32 identifier) override;
  u8 CloneControl(u32 identifier, UiMapControl **out) override;
  u8 DestroyControl() override;
  u8 Prepare() override;
  u8 Unprepare() override;
  WnWindow *AsIGZWin() override;
  using UiBaseWindow::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  u8 SetRotation(i32 degrees) override;
  i32 GetRotation() override;
  u8 SetDrawWindowOutline(bool enabled) override;
  u8 IsDrawWindowOutline() override;
  u8 SetWindowOutlineColors(u32 first, u32 second) override;
  u8 SetDrawImageOutline(bool enabled) override;
  u8 IsDrawImageOutline() override;
  u8 SetImageOutlineColors(u32 first, u32 second) override;
  u32 GetFillColor() override;
  void SetFillColor(u32 pixel) override;
  using UiBaseWindow::SetFillColor;
  u8 SetCoordinateForwarding(u32 flags, bool enabled, ScMessageTarget *target) override;
  u32 GetCoordinateForwardingFlags() override;
  u8 SetActualMapSizeInCells(u32 width, u32 height) override;
  u8 GetActualMapSizeInCells(u32 *width, u32 *height) override;
  void DirtyCellRegion(ScRect *area) override;
  u8 GetPalette(u32 id, GxPaletteView **out) override;
  u8 AddPalette(u32 id, GxPaletteView *palette) override;
  u8 ReleasePalette(u32 id) override;
  u8 ReleaseAllPalettes() override;
  u32 GetFilterCount() override;
  u32 GetVisibleFilterCount() override;
  UiMapFilter *GetFilter(u32 id) override;
  u8 GetFilter(u32 id, UiMapFilter **out) override;
  UiMapFilter *GetFilterByIndex(u32 index) override;
  u8 ShowFilter(u32 id) override;
  u8 ShowAllFilters() override;
  u8 HideFilter(u32 id) override;
  u8 HideAllFilters() override;
  u8 AddFilter(UiMapFilter *filter) override;
  u8 AddFilter(GxRasterSource *sprite) override;
  u8 AddFilter(u32 id, GxBuffer *buffer) override;
  u8 AddFilterFront(UiMapFilter *filter) override;
  u8 AddFilterBack(UiMapFilter *filter) override;
  u8 ReleaseFilter(u32 id) override;
  u8 ReleaseAllFilters() override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseDownR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpR(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  virtual bool GZReleaseCapture();

protected:
  u32 word168;
  i32 rotation;
  f32 radians;
  bool window_outline;
  u32 window_first, window_second;
  bool image_outline;
  u32 image_first, image_second, word204, forwarding_flags, map_width, map_height;
  bool image_dirty, rotation_dirty;
  f32 scale;
  u32 word228;
  GxBuffer *buffer;
  ScMessageTarget *forwarding_target;
  UiMapPaletteNode *palette_head;
  UiMapFilterNode *filter_head;
  u32 words248[6];
  u8 TranslateWindowCoord(UiMapPoint point, UiMapPoint *out) const;
  u8 DoWindowCoordMessage(u32 type, i16 x, i16 y);
  u8 CreateMapBuffer();
  u8 ReleaseMapBuffer();
  u8 UpdateBufferImage();
  u8 UpdateBufferSize();
  void RecalculatePostFilterCoordinatesEffects();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapControlObject) == 272);

class GxPaletteView;
class UiMapFilter;

// The installed palette list holds a key and retained interface in each circular node.
struct UiMapPaletteNode {
  UiMapPaletteNode *next, *previous;
  u32 id;
  GxPaletteView *palette;
};

// The installed filter list retains one interface in each circular node.
struct UiMapFilterNode {
  UiMapFilterNode *next, *previous;
  UiMapFilter *filter;
};

// SIMUI's linked cSC3MapSprite owns one palette and four projected corner coordinates.
// Its general cell renderer uses the virtual source pixel lookup and the real destination buffer.
class UiMapSprite : public GxRasterSource {
public:
  UiMapSprite();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 id, u32 width, u32 height) override;
  u32 GetId() const override;
  bool SetId(u32 id) override;
  bool SetPalette(GxPaletteView *source) override;
  GxPaletteView *GetPalette() override;
  i32 GetBaseWidth() const override;
  i32 GetBaseHeight() const override;
  bool SetBaseSize(u32 width, u32 height) override;
  u8 SetPostFilter(u8 enabled) override;
  void SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) override;
  u8 IsPostFilter() const override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;
  u8 DebugInstanceTag(ScStringView *out) override;
  u8 DebugClassTag(ScStringView *out) override;
  u8 DebugTypeTag(ScStringView *out) override;
  virtual bool GetColor(u32 x, u32 z, u32 *out);
  virtual bool DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual bool DrawAnnotations(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual u8 SetDebugInstanceTag(const ScStringView &value);
  virtual u8 SetDebugClassTag(const ScStringView &value);
  virtual ~UiMapSprite();

protected:
  u32 references, kind;
  GxPaletteView *palette;
  i32 width, height;
  u8 enabled;
  i32 corner_x[4], corner_z[4];
  void TransformVectorEffects(i32 *x, i32 *z);
  void ClipLineEffects(i32 *x0, i32 *z0, i32 *x1, i32 *z1);
  u8 DrawLine(i32 x0, i32 z0, i32 x1, i32 z1, u32 mode, GxBuffer *destination, const ScRect &rectangle, u32 color,
      ScRect *clip);
  static u16 ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapSprite) == 60);

class GxRasterSource;

// The SIMUI map-sprite filter retains one raster source and forwards its palette, dimensions and post-filter settings.
class UiMapSpriteFilter : public UiMapFilter {
public:
  UiMapSpriteFilter(GxRasterSource *sprite, GxPaletteView *palette);
  ~UiMapSpriteFilter() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetFilterId(u32 identifier) override;
  u32 GetFilterId() override;
  u32 GetWidth() override;
  u32 GetHeight() override;
  u8 SetVisible(bool enabled) override;
  u8 IsVisible() override;
  u8 SetPalette(GxPaletteView *palette) override;
  GxPaletteView *GetPalette() override;
  u8 Draw(GxBuffer *destination, ScRect *area, ScRect *clip) override;
  u8 CreateBuffer(u32 width, u32 height, GxBuffer **out) override;
  u8 CloneFilter(UiMapFilter **out) override;
  void SetPostFilter(bool enabled) override;
  void SetDestinationCoords(i32 x0, i32 y0, i32 x1, i32 y1, i32 x3, i32 y3) override;
  u8 IsPostFilter() const override;

protected:
  u32 references;
  GxRasterSource *sprite;
  u8 visible;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapSpriteFilter) == 16);

#endif
