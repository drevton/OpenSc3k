#ifndef SC3_UI_MAP_NAVIGATION_H
#define SC3_UI_MAP_NAVIGATION_H

#include "application/window_controls.h"
#include "city/city.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/map_controls.h"

class CtCity;
class WnButton;

// The maps window and the raster-query interface are the same primary object.
using UiMapsWindow = UiRasterQuery;

// Primary view interface, with the installed virtual slot order and borrowed owner/window results.
class UiMapsViewInterface : public ScUnknown {
public:
  virtual u8 CreateView(UiMapsWindow *owner, ScRect *area, ScPoint *location) = 0;
  virtual UiMapsWindow *GetMapsAndGraphsWindow() = 0;
  virtual WnWindow *GetAssociatedIGZWin() const = 0;
  // Linux map-view overrides identify the no-argument hide/show operations; the PC nominal returns remain qualified.
  virtual u8 PrepareHideView() = 0;
  virtual u8 PrepareShowView() = 0;
  virtual u8 EnumCityItems(CtCity *city, bool enabled) = 0;
  virtual u8 RefreshData(CtCity *city) = 0;
  virtual u32 *GetViewId() = 0;
  virtual u8 SetViewId(const u32 &identifier) = 0;
  virtual u8 GetViewArea(ScRect *out) = 0;
  virtual u8 SetViewArea(ScRect *area) = 0;
  virtual const ScStringView *GetName() const = 0;
  virtual u8 SetName(const ScStringView &text) = 0;
  virtual WnButton *GetIconButton() = 0;
  virtual u8 SetIconButton(GxBuffer *buffer) = 0;
  virtual u8 SetIconButton(u32 group, u32 instance) = 0;
  virtual i32 IsIconButtonReady() = 0;
  virtual u8 GetIconButtonLocation(ScPoint *out) = 0;
  virtual u8 SetIconButtonLocation(ScPoint *location) = 0;
  virtual u8 IconButtonToggleOn() = 0;
  virtual u8 IconButtonToggleOff() = 0;
  virtual u8 IsTearable() = 0;
  virtual u8 IsStandAlone() = 0;
  virtual u8 PerformTearOff(UiMapsViewInterface **out) = 0;
  virtual i32 IsReady() = 0;
  virtual u8 SetNameResource(u32 group, u32 instance) = 0;
  virtual u8 OnInit(WnWindow *window) = 0;
  virtual u8 OnShutdown(WnWindow *window) = 0;
  virtual u8 EndDialog(WnWindow *window, u32 result) = 0;
  virtual ~UiMapsViewInterface() = default;
};

// The base view owns three secondary interfaces in addition to its primary view interface.
// Unresolved callbacks remain abstract until their contracts are established.
class UiMapsView : public UiMapsViewInterface,
                   public UiWindowCallback,
                   public CtCityEnumerator,
                   public ScMessageTarget {
public:
  UiMapsView();
  ~UiMapsView() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 CreateView(UiMapsWindow *owner, ScRect *area, ScPoint *location) override;
  UiMapsWindow *GetMapsAndGraphsWindow() override;
  WnWindow *GetAssociatedIGZWin() const override;
  u8 EnumCityItems(CtCity *city, bool enabled) override;
  u8 RefreshData(CtCity *city) override;
  u32 *GetViewId() override;
  u8 SetViewId(const u32 &identifier) override;
  u8 GetViewArea(ScRect *out) override;
  u8 SetViewArea(ScRect *area) override;
  const ScStringView *GetName() const override;
  u8 SetName(const ScStringView &text) override;
  WnButton *GetIconButton() override;
  u8 SetIconButton(GxBuffer *buffer) override;
  u8 SetIconButton(u32 group, u32 instance) override;
  i32 IsIconButtonReady() override;
  u8 GetIconButtonLocation(ScPoint *out) override;
  u8 SetIconButtonLocation(ScPoint *location) override;
  u8 IconButtonToggleOn() override;
  u8 IconButtonToggleOff() override;
  u8 IsTearable() override;
  u8 IsStandAlone() override;
  u8 PerformTearOff(UiMapsViewInterface **out) override;
  i32 IsReady() override;
  u8 SetNameResource(u32 group, u32 instance) override;
  u8 OnInit(WnWindow *window) override;
  u8 OnShutdown(WnWindow *window) override;
  u8 EndDialog(WnWindow *window, u32 result) override;

  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool DoMessage(ScMessage *message) override;

protected:
  u32 references;
  UiMapsWindow *owner;
  UiGenericInterface *window;
  u8 enabled, standalone;
  ScString name;
  WnButton *icon;
  ScPoint icon_location;
  ScRect view_area;
  u32 unknown80[4], view_id;
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapsView) == 100);

// Original IID82F95BB8 secondary navigation view contains the borrowed primary window getter.
class UiNavigationInterface : public ScUnknown {
public:
  virtual WnWindow *AsIGZWin() = 0;
};

class FtFont;

// The navigation window owns three atlas buffers, their rectangles and the original callback timer.
// Initialization, message handling and clock updates are candidates awaiting connected verification.
class UiNavigation : public UiBaseWindow, public UiNavigationInterface {
public:
  UiNavigation();
  ~UiNavigation() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *AsIGZWin() override;
  bool Init() override;
  bool Shutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;
  bool IsPointInWindow(i32 x, i32 y) const override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 identifier) override;
  u8 EnableClock(bool enabled);

protected:
  GxBuffer *images[3];
  ScRect image_areas[3];
  i32 word228, word232, word236, word240, word244;
  bool clock_enabled;
  ScCallbackTimer timer;
  GxBuffer *clock_buffer;
  FtFont *clock_font;
  u32 RotationToInstance(i32 rotation, i32 index);
  bool GetBuffer(i32 rotation, i32 index);
  u8 UpdateBackgroundBuffers(i32 rotation);
  u8 UpdateClock();
  u8 OnRotationChanged(i32 rotation);
  u8 CreateMiniMap();
  u8 DestroyMiniMap();
  u8 CreatePalette(GxBuffer *source, u32 row, u32 count, GxPaletteView **out);
  static void ClockTimerCallback(uptr receiver);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiNavigation) == 292);

struct ScPoint;

// The original mini-map overlay draws the current isometric camera outline over a map sprite.
class UiNavigationMapSprite : public UiMapSprite {
public:
  UiNavigationMapSprite(u32 width, u32 height);
  ~UiNavigationMapSprite() override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;

protected:
  u32 color, grid_width, grid_height;
  u8 DrawBox(GxBuffer *destination, const ScRect &rectangle, ScPoint *points, u32 color, u32 mode, i32 expand);
};

static_assert(sizeof(uptr) != 4 || sizeof(UiNavigationMapSprite) == 72);

class FtFont;

// The navigation compass marker is a distinct 184-byte window with one retained font and two colors.
class UiNavigationMarker : public UiBaseWindow {
  friend class UiNavigation;

public:
  UiNavigationMarker();
  ~UiNavigationMarker() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;
  using WnRootWidget::SetArea;
  void SetArea(i32 left, i32 top, i32 right, i32 bottom) override;
  void SetCaption(const ScStringView &text) override;
  u8 SetFont(FtFont *font);

protected:
  FtFont *font;
  i32 text_width, text_height;
  u32 foreground, shadow;
  void UpdateBoundsEffects();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiNavigationMarker) == 184);

// The installed maps-and-graphs window owns a generic window, views, palettes and raster sources.
// Known creation and callback operations are recovered; the default setter retains its nominal-return qualification.
class UiMapsWindowObject : public UiRasterQuery,
                           public UiWindowCallback,
                           public CtCityEnumerator,
                           public ScMessageTarget {
public:
  UiMapsWindowObject();
  ~UiMapsWindowObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 Init() override;
  bool DoesMapViewExist() override;
  u32 GetCurrentViewId() override;
  u32 GetDefaultViewId() override;
  void SetDefaultViewId(const u32 &identifier) override;
  WnWindow *GetWindow() const override;
  u8 SetCaption(const ScStringView &caption) override;
  u8 GetCaption(ScStringView *out) const override;
  u8 SetFrameArea(const ScRect &area) override;
  u8 GetFrameArea(ScRect *out) const override;
  u8 ShowView(u32 identifier) override;
  u8 ShowView(UiMapsViewInterface *view) override;
  u8 GetView(const u32 &identifier, u32 iid, void **out) override;
  UiMapsViewInterface *GetView(const u32 &identifier) override;
  u8 AddPalette(u32 identifier, GxPaletteView *palette) override;
  GxPaletteView *GetPalette(u32 identifier) override;
  u32 GetPaletteCount() const override;
  GxPaletteView *GetPaletteByIndex(u32 index) override;
  u8 AddRaster(GxRasterSource *source) override;
  GxRasterSource *FindRaster(u32 kind) override;
  u32 GetRasterCount() const override;
  GxRasterSource *GetRasterByIndex(u32 index) override;
  i32 IsReady() override;
  u8 ReleaseAllPalettes() override;
  u8 ReleaseAllRasters() override;
  u8 RefreshData(CtCity *city) override;
  u8 EnumCityItems(CtCity *city, bool enabled) override;
  bool DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr third) override;
  bool Enumerate(ScMessage *message, CtLayer *layer) override;
  bool DoMessage(ScMessage *message) override;

protected:
  struct UiPaletteEntry {
    u32 identifier;
    GxPaletteView *palette;
  };

  u32 references, default_view;
  UiGenericInterface *window;
  WnWindow *sidebar;
  ScRect frame, content;
  u32 unknown64[8];
  ScString caption;
  u8 unknown116;
  UiMapsViewInterface **views, **views_end, **views_capacity;
  UiMapsViewInterface *current;
  UiPaletteEntry *palettes, *palettes_end, *palettes_capacity;
  GxRasterSource **rasters, **rasters_end, **rasters_capacity;
  u8 SetupView(UiMapsViewInterface *view, i32 index);
  u8 SetupSidebar();
  // Original44430 constructs ten map views followed by charts and graphs in its original partial-success order.
  u8 SetupViews();
  void ReleaseViewsEffects();
  void ReleaseSidebarEffects();
  u8 OnInit(WnWindow *recipient);
  u8 OnShutdown(WnWindow *recipient);
  u8 EndDialog(WnWindow *recipient, u32 result);
  u8 DoViewChange();
};

static_assert(sizeof(uptr) != 4 || sizeof(UiMapsWindowObject) == 160);

#endif
