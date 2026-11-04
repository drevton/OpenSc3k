#include "sprites/city_window.h"

#include <cassert>
#include <new>

#include "advisors/ad_domains.h"
#include "advisors/ad_module.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "geometry/api.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/sp_tick_manager.h"
#include "sprites/sprite.h"
#include "sprites/view_camera.h"
#include "ui/api.h"
#include "window/api.h"
#include "window/cursor_platform.h"

// Coordinate text uses a white foreground and black shadow; cost hints independently distinguish insufficient funds.
enum SpCityViewColor : u32 {
  kSpCoordinateTextShadowColor = 0,
  kSpAffordableCostColor = 0,
  kSpCoordinateTextColor = 0xffffffff,
  kSpInsufficientFundsCostColor = 0xdf2c2c,
};

// Temporary location/demolition tools transfer the active reference into the previous-handler slot.
// The unset state is published during callbacks; restore returns that saved reference without acquiring another.
enum SpHandlerMode : i32 {
  kSpHandlerUnset = -1,
  kSpHandlerDefault = 0,
  kSpHandlerLocation = 1,
  kSpHandlerDemolition = 2,
  kSpHandlerRestore = 3,
  kSpHandlerTool = 4,
};

// These navigation control IDs are independent of accelerator actions and physical arrow-key codes.
enum SpNavigationCommand : u32 {
  kSpNavigationZoomIn = 1,
  kSpNavigationZoomOut = 2,
  kSpNavigationRotateCW = 3,
  kSpNavigationRotateCCW = 4,
};

// Software cursor metadata replaces the low reference word while retaining the high word.
enum WnCursorMetadataMask : u32 {
  kWnCursorReferenceHighWordMask = 0xffff0000,
};

// Underground options map to views1/2; leading data options map to10/11 and the remaining options to12..16.
enum SpLayerOptionOffset : u32 {
  kSpUndergroundLayerOptionOffset = 0x7cf,
  kSpLeadingDataLayerOptionOffset = 0xbae,
  kSpDataLayerOptionOffset = 0xbaf,
};

// Opening, closing and closed notifications identify the same city view in their first payload word.
enum SpCityViewLifecycleIdentity : u32 {
  kSpCityViewLifecycleTag = 0xa20fb3b9,
};

// Occupant predicates test these masks with their original all-bits or any-bit operation.
enum SpViewGeomMask : u32 {
  kSpBuildingGeomMask = 0x100,
  kSpFloraGeomMask = 0x400,
  kSpTrafficGeomMask = 0x4100,
  kSpCivicGeomMask = 0x4800,
  kSpSubwayGeomMask = 0x40000,
  kSpPowerGeomMask = 0x400000,
  kSpTransportGeomMask = 0x400800,
  kSpPipeGeomMask = 0x800000,
};

// Accelerator action IDs dispatched by the city window; these are distinct from physical key values.
enum SpCityHotkeyAction : i32 {
  kSpHotkeyResidentialLowDensity = 1,
  kSpHotkeyResidentialMediumDensity = 4,
  kSpHotkeyResidentialHighDensity = 5,
  kSpHotkeyCommercialLowDensity = 6,
  kSpHotkeyCommercialMediumDensity = 7,
  kSpHotkeyCommercialHighDensity = 8,
  kSpHotkeyIndustrialLowDensity = 9,
  kSpHotkeyIndustrialMediumDensity = 10,
  kSpHotkeyIndustrialHighDensity = 11,
  kSpHotkeyDezone = 12,
  kSpHotkeyPowerLines = 13,
  kSpHotkeyBusStop = 14,
  kSpHotkeyRoads = 15,
  kSpHotkeyRails = 16,
  kSpHotkeyTrainStation = 17,
  kSpHotkeyDemolish = 18,
  kSpHotkeyMeetAdvisors = 19,
  kSpHotkeyBudget = 20,
  kSpHotkeyOrdinances = 21,
  kSpHotkeyDataMaps = 22,
  kSpHotkeyPoliceStation = 24,
  kSpHotkeyFireStation = 25,
  kSpHotkeyHospital = 26,
  kSpHotkeySchool = 27,
  kSpHotkeyOpenCity = 28,
  kSpHotkeyNewCity = 29,
  kSpHotkeySaveCity = 30,
  kSpHotkeySaveCityAs = 31,
  kSpHotkeyQuit = 33,
  kSpHotkeyTogglePause = 35,
  kSpHotkeyDecreaseSpeed = 36,
  kSpHotkeyIncreaseSpeed = 37,
  kSpHotkeyCheatDialog = 40,
  kSpHotkeyZoomIn = 41,
  kSpHotkeyZoomOut = 42,
  kSpHotkeyRotateClockwise = 43,
  kSpHotkeyRotateCounterclockwise = 44,
  kSpHotkeyToggleUiPanels = 49,
  kSpHotkeyFocusAtPointer = 50,
  kSpHotkeyPowerPlants = 51,
  kSpHotkeyHighways = 52,
  kSpHotkeySubways = 53,
  kSpHotkeySubwayStation = 54,
  kSpHotkeyJail = 55,
  kSpHotkeyCollege = 56,
  kSpHotkeySmallPark = 57,
  kSpHotkeyLargePark = 58,
  kSpHotkeyRewards = 59,
  kSpHotkeyPreferences = 60,
  kSpHotkeyPreferencesAlternate = 61,
  kSpHotkeyOpenCityAlternate = 62,
  kSpHotkeyOpenTerrain = 64,
  kSpHotkeySaveCityAlternate = 65,
  kSpHotkeyWaterBuildings = 68,
  kSpHotkeyPipes = 69,
  kSpHotkeyTrees = 70,
  kSpHotkeyPlaceWater = 71,
  kSpHotkeyLandmarks = 72,
  kSpHotkeyNeighbors = 73,
  kSpHotkeyZoomLevel0 = 75,
  kSpHotkeyZoomLevel0Alternate = 76,
  kSpHotkeyZoomLevel1 = 77,
  kSpHotkeyZoomLevel1Alternate = 78,
  kSpHotkeyZoomLevel2 = 79,
  kSpHotkeyZoomLevel2Alternate = 80,
  kSpHotkeyZoomLevel3 = 81,
  kSpHotkeyZoomLevel3Alternate = 82,
  kSpHotkeyZoomLevel4 = 83,
  kSpHotkeyZoomLevel4Alternate = 84,
  kSpHotkeyCancelTool = 85,
  kSpHotkeyNewCityAlternate = 86,
  kSpHotkeyOpenStarterTown = 87,
  kSpHotkeyOpenStarterTownAlternate = 88,
  kSpHotkeyOpenTerrainAlternate = 89,
  kSpHotkeySaveCityAsAlternate = 90,
  kSpHotkeyQuitAlternate = 91,
  kSpHotkeyLandfill = 93,
  kSpHotkeyLayers = 94,
  kSpHotkeyGarbageBuildings = 95,
  kSpHotkeyQuitSecondAlternate = 102,
  kSpHotkeyViewAlbum = 113,
  kSpHotkeyTakeSnapshot = 114,
  kSpHotkeyToggleCellCoordinates = 115,
};

static constexpr u32 kSpCityViewRecordType = 0xa1634f05;
static constexpr u32 kSpCityViewRecordGroup = 0x1e1a5e6;

// Initialize the preview state and cache the window manager's background and border colors.
// The drawing rectangles remain untouched until the original drawing setup computes them.
SpCitySpriteWindow::SpCitySpriteWindow()
    : stretch(false), preserve_aspect(true), preview_dirty(true), unknown176(UINT32_MAX), sunken(false),
      keep_window_size(false), preview_buffer(nullptr), sprite(nullptr), zoom(3), compass(0) {
  preview_fill = SpGetWindowResources()->GetColor(1);
  sunken_first = SpGetWindowResources()->GetColor(19);
  sunken_second = SpGetWindowResources()->GetColor(20);
}

SpCitySpriteWindow::~SpCitySpriteWindow() {
  ScRelease(&preview_buffer);
  ScRelease(&sprite);
}

bool SpCitySpriteWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpCitySpritePreview, SpCitySpritePreview);
  return WnSpriteRootWidget::QueryInterface(iid, out);
}

u32 SpCitySpriteWindow::AddRef() {
  return WnSpriteRootWidget::AddRef();
}

u32 SpCitySpriteWindow::Release() {
  return WnSpriteRootWidget::Release();
}

// Replace the retained sprite when it changes and mark the preview for redraw.
// A null sprite is rejected without releasing the currently selected sprite.
bool SpCitySpriteWindow::SetCitySprite(SpSprite *value) {
  if (!value)
    return false;
  if (sprite != value) {
    ScRelease(&sprite);
    preview_dirty = true;
    sprite = ScAddRefNotNull(value);
  }
  return true;
}

// Create a temporary instance from a sprite definition and pass it to the instance overload.
// Release the temporary reference after the preview has retained its own reference.
bool SpCitySpriteWindow::SetCitySprite(SpDefView *definition) {
  SpSprite *instance = nullptr;
  if (!definition || !definition->CreateInstance(&instance))
    return false;
  bool result = SetCitySprite(instance);
  instance->Release();
  return result;
}

bool SpCitySpriteWindow::SetCitySprite(GeAttrib *attributes) {
  return attributes && SetCitySprite(attributes, attributes->GetDefaultLayerFlag());
}

bool SpCitySpriteWindow::SetCitySprite(GeAttrib *attributes, u32 layer) {
  SpDefinition *definition = attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
  return definition && SetCitySprite(static_cast<SpDefView *>(definition));
}

bool SpCitySpriteWindow::SetCitySprite(ScOccupant *occupant) {
  return occupant && SetCitySprite(occupant, occupant->GetDefaultLayerFlag());
}

bool SpCitySpriteWindow::SetCitySprite(ScOccupant *occupant, u32 layer) {
  SpDefinition *definition = occupant ? occupant->GetSpriteAttrib(layer) : nullptr;
  return definition && SetCitySprite(static_cast<SpDefView *>(definition));
}

// Accept the four compass directions and redraw only when the direction changes.
bool SpCitySpriteWindow::SetCompass(i32 value) {
  if (u32(value) >= 4)
    return false;
  if (compass != value) {
    preview_dirty = true;
    compass = value;
  }
  return true;
}

// Accept the five sprite zooms and redraw only when the zoom changes.
bool SpCitySpriteWindow::SetZoom(i32 value) {
  if (u32(value) > 4)
    return false;
  if (zoom != value) {
    preview_dirty = true;
    zoom = value;
  }
  return true;
}

i32 SpCitySpriteWindow::GetCompass() const {
  return compass;
}

i32 SpCitySpriteWindow::GetZoom() const {
  return zoom;
}

// Choose the largest sprite zoom whose dimensions fit in the window.
// If none fits, select zoom zero and report the failed fit.
bool SpCitySpriteWindow::SetSpriteZoomToFitInWindowArea() {
  if (!sprite)
    return false;
  for (i32 candidate = 4; candidate >= 0; --candidate) {
    i32 width = sprite->GetW(candidate, compass), height = sprite->GetH(candidate, compass);
    if (width <= GetW() && height <= GetH()) {
      SetZoom(candidate);
      return true;
    }
  }
  SetZoom(0);
  return false;
}

// Resize the window to the selected sprite dimensions when they differ from its current size.
bool SpCitySpriteWindow::SetWindowSizeToSpriteSize() {
  if (!sprite)
    return false;
  i32 width = sprite->GetW(zoom, compass), height = sprite->GetH(zoom, compass);
  if (GetW() != width || GetH() != height) {
    SetSize(width, height);
    preview_dirty = true;
  }
  return true;
}

// Return the borrowed primary window identity corresponding to this preview.
WnWindow *SpCitySpriteWindow::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Return the currently associated sprite without changing its reference count.
SpSprite *SpCitySpriteWindow::GetAssociatedCitySprite() {
  return sprite;
}

// Compute height divided by width for a selected sprite with positive width.
// Leave the output untouched when no valid ratio can be computed.
bool SpCitySpriteWindow::GetSpriteHeightToWidthRatio(f32 *ratio) const {
  if (!sprite)
    return false;
  i32 width = sprite->GetW(zoom, compass), height = sprite->GetH(zoom, compass);
  if (width <= 0)
    return false;
  *ratio = f32(f64(height) / f64(width));
  return true;
}

// Mark the preview dirty when the requested stretch state differs from the stored state.
// The installed body updates the aspect flag only for a nonzero stretch request.
bool SpCitySpriteWindow::SetStretch(bool value, bool aspect) {
  if (value != stretch)
    preview_dirty = true;
  if (value && aspect != preserve_aspect) {
    preview_dirty = true;
    preserve_aspect = aspect;
  }
  return true;
}

// Return the base window's borrowed rectangle through the preview identity.
const ScRect *SpCitySpriteWindow::GetArea() const {
  return WnSpriteRootWidget::GetArea();
}

void SpCitySpriteWindow::SetArea(const ScRect &rectangle) {
  SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
}

void SpCitySpriteWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  preview_dirty = true;
  WnSpriteRootWidget::SetArea(left, top, right, bottom);
}

void SpCitySpriteWindow::SetFillColor(u32 color) {
  if (preview_fill != color) {
    preview_dirty = true;
    preview_fill = color;
    WnSpriteRootWidget::SetFillColor(color);
  }
}

void SpCitySpriteWindow::SetAreaToDrawTo() {
  preview_dirty = true;
  WnSpriteRootWidget::SetAreaToDrawTo();
}

bool SpCitySpriteWindow::Shutdown() {
  ScRelease(&preview_buffer);
  return WnSpriteRootWidget::Shutdown();
}

// Acquire and initialize the preview's 256-by-512 buffer before initializing the base window.
// An already initialized window keeps its existing buffer and succeeds immediately.
bool SpCitySpriteWindow::Init() {
  if (GetInitFlag())
    return true;
  SpGetGraphics()->CreateBuffer(&preview_buffer);
  if (!preview_buffer ||
      !preview_buffer->Init(256, 512, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false))
    return false;
  return WnSpriteRootWidget::Init();
}

// Prepare the sprite artwork and render it into the preview buffer on its background color.
// Expand an undersized buffer first, and balance each successful buffer lock and artwork demand.
u8 SpCitySpriteWindow::SetupBuffer() {
  if (!sprite || !preview_buffer)
    return 0;
  u32 width = u32(sprite->GetW(zoom, compass)), height = u32(sprite->GetH(zoom, compass));
  if ((width > preview_buffer->Width() || height > preview_buffer->Height()) && preview_buffer->Uninitialize())
    preview_buffer->Init(width, height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  u8 result = sprite->DemandArt(zoom, compass, false);
  if (result) {
    result = preview_buffer->Lock(GxLockFlags::Fill);
    if (result) {
      preview_buffer->Fill(preview_fill, *preview_buffer->GetBufferArea(), nullptr);
      preview_buffer->Unlock(GxLockFlags::Fill);
      result = preview_buffer->Lock(GxLockFlags::Bits);
      if (result) {
        ScRect source = {0, 0, i32(width), i32(height)};
        result = sprite->Draw(preview_buffer, source, zoom, compass, nullptr);
        preview_buffer->Unlock(GxLockFlags::Bits);
      }
    }
    sprite->ReleaseArt(zoom, compass);
  }
  return result;
}

// Set the source and destination rectangles and center or crop the sprite within the drawing area.
void SpCitySpriteWindow::SetupBltRect() {
  destination_rect = draw_area;
  source_rect = {0, 0, sprite->GetW(zoom, compass), sprite->GetH(zoom, compass)};
  i32 width = destination_rect.right - destination_rect.left, height = destination_rect.bottom - destination_rect.top;
  i32 sprite_width = source_rect.right - source_rect.left, sprite_height = source_rect.bottom - source_rect.top;
  if (sprite_width == width && sprite_height == height)
    return;
  if (!stretch) {
    if (sprite_width > width)
      source_rect.right = width;
    else if (width > sprite_width) {
      destination_rect.left += (width - sprite_width) / 2;
      destination_rect.right = destination_rect.left + sprite_width;
    }
    if (sprite_height > height)
      source_rect.bottom = height;
    else if (height > sprite_height) {
      destination_rect.top += (height - sprite_height) / 2;
      destination_rect.bottom = destination_rect.top + sprite_height;
    }
  } else if (preserve_aspect && sprite_width && sprite_height) {
    f64 x_scale = f64(width) / sprite_width;
    f32 y_scale = f32(f64(height) / sprite_height);
    f64 scale = x_scale >= y_scale ? f64(y_scale) : x_scale;
    i32 scaled_width = i32(f64(width) * scale), scaled_height = i32(f64(height) * scale);
    i32 x_offset = width - scaled_width, y_offset = height - scaled_height;
    if (x_offset < 0)
      x_offset = 0;
    if (y_offset < 0)
      y_offset = 0;
    destination_rect.left += x_offset / 2;
    destination_rect.right = destination_rect.left + scaled_width;
    destination_rect.top += y_offset / 2;
    destination_rect.bottom = destination_rect.top + scaled_height;
  }
}

// Refresh the cached sprite image when dirty, then draw the background, sprite and optional sunken border.
bool SpCitySpriteWindow::GZPaint() {
  if (!sprite && !keep_window_size)
    return true;
  if ((preview_dirty || !preview_buffer) && SetupBuffer()) {
    SetupBltRect();
    blit_rect = destination_rect;
    --blit_rect.left;
    --blit_rect.top;
    ++blit_rect.right;
    ++blit_rect.bottom;
    blit_rect.Intersect(&blit_rect, &draw_area);
    preview_dirty = false;
  }
  if (keep_window_size && draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(preview_fill, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (sprite && preview_buffer && draw_buffer->Lock(GxLockFlags::Blt)) {
    if (stretch)
      draw_buffer->BltStretch(preview_buffer, source_rect, destination_rect, nullptr);
    else
      draw_buffer->Blt(preview_buffer, source_rect, destination_rect, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if (sunken && draw_buffer->Lock(GxLockFlags::Line)) {
    ScRect border = keep_window_size ? draw_area : blit_rect;
    draw_buffer->DrawLine(border.left, border.top, border.right, border.top, i32(sunken_first), 1);
    draw_buffer->DrawLine(border.left, border.top, border.left, border.bottom, i32(sunken_first), 1);
    draw_buffer->DrawLine(border.left, border.bottom, border.right, border.bottom, i32(sunken_second), 1);
    draw_buffer->DrawLine(border.right, border.top, border.right, border.bottom, i32(sunken_second), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

// Construct the preview and return its separate, initially unretained interface identity.
ScUnknown *SpCreateCitySpriteWindow() {
  return static_cast<SpCitySpritePreview *>(new SpCitySpriteWindow);
}

bool SpCitySpriteWindow::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool SpCitySpriteWindow::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

void SpCitySpriteWindow::SetDrawSunkenRect(bool enabled) {
  sunken = enabled;
}

void SpCitySpriteWindow::SetSunkenRectColors(u32 first, u32 second) {
  sunken_first = first;
  sunken_second = second;
}

void SpCitySpriteWindow::SetKeepWindowSize(bool enabled) {
  keep_window_size = enabled;
}

#undef CreateDialog
#undef CreateDialogA
#include <float.h>

#include <cstdio>
#include <cstring>

#include "ui/commands.h"
#include "ui/query_dialogs.h"
#include "window/controls.h"
#include "window/input.h"
#undef LoadCursor
#undef SetCursor

// The cursor's SIMSPR-local byte grid is the original column-based cell-map specialization.
class SpCursorCells {
public:
  SpCursorCells(u32 width, u32 height, const u8 *fill);
  virtual ~SpCursorCells();
  bool SerialRead(RsStream *stream);
  bool SerialWrite(RsStream *stream) const;
  void SetAll(const u8 *value);
  u32 width, height;
  u8 **columns;
};

// The installed cursor byte-map owns its grid and holds the SIMSPR director for each retained reference.
class SpCursorByteMap : public ScCellMap<u8> {
public:
  SpCursorByteMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 left, u32 top, u32 right, u32 bottom) const override;
  bool ValidRect(u32 left, u32 top, u32 right, u32 bottom) const override;
  bool SerialRead(RsStream *stream) override;
  bool SerialWrite(RsStream *stream) const override;
  bool Init(CtCity *city, const u8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  void SetValue(u32 left, u32 top, u32 right, u32 bottom, const u8 *value) override;
  virtual ~SpCursorByteMap();

private:
  u32 references;
  bool initialized;
  SpCursorCells *cells;
};

// SIMSPR's own file cursor uses the window cursor interface and its module-local graphics and resource services.
class WnFileCursor : public WnCursor {
public:
  explicit WnFileCursor(const ScStringView &name);
  virtual ~WnFileCursor();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool LoadCursor(ScStringView *name) override;
  void SetCursor() override;
  void GetHotSpot(i32 *x, i32 *y) override;

private:
  bool hardware;
  GxGraphics *graphics;
  ScString filename;
  GxBuffer *image;
  i16 hotspotX, hotspotY;
  u32 references;
  void *cursor;
};

// This private library copy uses SIMSPR's own service cache and remains distinct from the executable copy.
namespace {
class SpAutoCursor {
public:
  explicit SpAutoCursor(u32 identifier);
  ~SpAutoCursor();

private:
  bool pushed;
};
}

// Copy the cursor filename, acquire SIMSPR graphics when available and load the requested cursor.
// SDL selects the existing software BMP path instead of the Windows hardware CUR loader.
WnFileCursor::WnFileCursor(const ScStringView &name)
    : hardware(false), graphics(nullptr), filename(name), image(nullptr), hotspotX(0), hotspotY(0), references(0),
      cursor(nullptr) {
  if (SpGetFramework()) {
    graphics = SpGetGraphics();
    if (graphics)
      hardware = graphics->DoesSystemHaveCursorSupport();
  }
  hardware = WnPlatformFileCursorHardware(hardware);
  WnFileCursor::LoadCursor(&filename);
}

// Clear the Windows shared handle or destroy the SDL-owned cursor, then release the software image.
WnFileCursor::~WnFileCursor() {
  if (cursor) {
    WnPlatformDestroyCursor(cursor);
    cursor = nullptr;
  }
  ScRelease(&image);
}

bool WnFileCursor::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnCursor);
  HANDLE_QUERY_INTERFACE(IID_WnCursor, WnCursor);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 WnFileCursor::AddRef() {
  return ++references;
}

// Delete the cursor when its entry reference count was one; zero wraps without deletion.
u32 WnFileCursor::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

// Load a hardware CUR or its software BMP counterpart, retaining the original hotspot and reference-word writes.
// The software branch requires a three-character suffix, which the original overwrites directly.
bool WnFileCursor::LoadCursor(ScStringView *name) {
  {
    ScString copy(*name);
    filename = copy;
  }
  if (hardware) {
    cursor = WnPlatformLoadCursor(cursor, filename);
    return cursor != nullptr;
  }
  if (cursor) {
    WnPlatformDestroyCursor(cursor);
    cursor = nullptr;
  }
  cursor = WnPlatformLoadCompositedCursor(filename);
  if (cursor)
    return true;
  u32 length = filename.Strlen();
  assert(length >= 3);
  std::memcpy(filename.begin + length - 3, "bmp", 3);
  void *slot = nullptr;
  if (SpGetCom()->GetClassObject(kRsImageFactoryClassId, IID_RsImageOptions, &slot)) {
    const GxColorType format(GxColorType::Color::RGB16);
    static_cast<RsImageOptions *>(slot)->LoadFile(&filename, &image, 1, &format, 0);
    static_cast<RsImageOptions *>(slot)->Release();
    slot = nullptr;
  }
  if (!image) {
    SpGetGraphics()->CreateBuffer(&image);
    image->Init(32, 32, GxColorType(static_cast<GxColorType::Color>(4)), nullptr, GxDepthType(GxDepthType::Depth::None),
        GxVramRequest::VideoMemoryLowPriority, false);
  }
  WnResources *resources = SpGetWindowResources();
  u32 key = resources->GetColor(kWnColorUiTransparency);
  image->SetTransparency(key);
  RsFile file(filename);
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    i32 fileLength = i32(file.Size());
    file.SeekBegin();
    u32 argument = u32(uptr(name));
    file.Read(reinterpret_cast<u8 *>(&argument) + 2, 2);
    if (u16(argument >> 16) == 0x4d42) {
      file.SeekAbsolute(2);
      u32 bitmapSize, read = 4;
      file.Read(&bitmapSize, &read);
      assert(read == 4);
      if (fileLength >= i32(bitmapSize + 8u)) {
        file.SeekAbsolute(i32(bitmapSize));
        u32 first = u16(hotspotX) | (u32(u16(hotspotY)) << 16);
        file.Read(&first, 4);
        hotspotX = i16(first);
        hotspotY = i16(first >> 16);
        u32 second = u16(hotspotY) | (references << 16);
        file.Read(&second, 4);
        hotspotY = i16(second);
        references = (references & kWnCursorReferenceHighWordMask) | (second >> 16);
        i16 x = hotspotX;
        i32 width = i32(image->Width());
        if (x >= width || x < 0)
          hotspotX = 0;
        i16 y = hotspotY;
        i32 height = i32(image->Height());
        if (y >= height || y < 0)
          hotspotY = 0;
      }
    }
  }
  return image != nullptr;
}

// Install a usable platform cursor or provide the software image to the graphics service.
// Installing an SDL platform cursor clears the previous software image before changing the native handle.
void WnFileCursor::SetCursor() {
  if (cursor) {
    if (WnPlatformSelectsCursorDirectly() || hardware || !graphics || graphics->DoesSystemHaveCursorSupport()) {
      WnPlatformSetCursor(cursor, graphics);
      return;
    }
  } else if (!graphics) {
    return;
  }
  WnPlatformClearCursorOverlay();
  graphics->SetCursorBuffer(image, 0, 0);
}

void WnFileCursor::GetHotSpot(i32 *x, i32 *y) {
  *x = hotspotX;
  *y = hotspotY;
}

// Borrow and cache the SIMSPR-local window manager, releasing the framework's temporary service reference.
static WnWindowManager *SpWindowManager() {
  static WnWindowManager *manager = nullptr;
  if (!manager) {
    void *object = nullptr;
    SpGetFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    manager = static_cast<WnWindowManager *>(object);
    if (manager)
      manager->Release();
  }
  return manager;
}

WnSpriteRootWidget::WnSpriteRootWidget()
    : owner(nullptr), keyboard(nullptr), references(0), identifier(0), absolute_area{}, draw_area{},
      children_locked(false), parent(nullptr), notification_target(nullptr), caption(), private_buffer(nullptr),
      draw_buffer(nullptr), dirty(true), cursor(nullptr), accelerator(nullptr), fill_color(0), child_focus(nullptr),
      key_equivalent(0), mouse_down_x(0), mouse_down_y(0), area{}, buffer_area{}, flags(kWnDefaultFlags) {
  owner = SpWindowManager();
  if (owner)
    keyboard = SpWindowManager()->GetKeyboard();
  if (owner)
    owner->AddWindowToValidList(this);
}

// Push the requested automatic cursor if the module's window and cursor managers are available.
SpAutoCursor::SpAutoCursor(u32 identifier) : pushed(false) {
  WnWindowManager *manager = SpWindowManager();
  if (manager) {
    WnCursorManager *cursors = manager->GetCursorManager();
    if (cursors)
      pushed = cursors->PushAutoCursor(identifier);
  }
}

SpAutoCursor::~SpAutoCursor() {
  if (pushed) {
    WnWindowManager *manager = SpWindowManager();
    if (manager) {
      WnCursorManager *cursors = manager->GetCursorManager();
      if (cursors)
        cursors->PopAutoCursor();
    }
  }
}

// Ordered cursor resources registered during city-window initialization.
// The window initializer registers each resource in the order stored below.
struct SpCityCursorResource {
  const char *filename;
  u32 id;
};

// The installed city-window message handler shares this deferred handler-reset byte across window instances.
static bool SpDeferredHandlerReset = false;

// The city window subscribes and unsubscribes these original message IDs in this order.
static constexpr u32 kSpCityWindowMessages[] = {
    kApMsgPostCityChange,
    kApMsgUiStateChanged,
    kApMsgPreCityChange,
    kApMsgCityReady,
    kCtMsg_CitySaveFinished,
    kWnMsg_CityViewportChanged,
    kWnMsg_AcceleratorCommand,
    kWnMsg_InitCityViewLayers,
    kWnMsg_UpdateCityViewLayers,
    kWnMsg_FocusCityCamera,
    kWnMsg_FocusCityCameraAndZoom,
    kWnMsg_SetCityCameraZoomRotation,
    kWnMsg_SetCityViewParams,
    kWnMsg_ConstructionPromptOpened,
    kWnMsg_ConstructionPromptClosed,
    kWnMsg_EnterTerrainEditor,
    kWnMsg_EnterCity,
    kWnMsg_ModalBegin,
    kWnMsg_ModalEnd,
    kGeMsg_QueryCommandStarting,
    kGeMsg_QueryCommandCompleted,
    kWnMsg_ProcessDeferredCityHandlerReset,
    kWnMsg_ShowCitySnapshotAlbum,
    kWnMsg_ActivateApp,
    kCtMsg_MonthChanged,
};

// The installed handler transition holds the city's critical section until its scoped cleanup completes.
class SpCityLockGuard {
public:
  // Enter the supplied city lock before any handler transition callbacks.
  explicit SpCityLockGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Release the city lock on both ordinary return and stack unwinding.
  ~SpCityLockGuard() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// The original command-service temporary holds one acquired factory identity until its scope ends.
class SpCityCommandFactoryRef {
public:
  // Acquire the game-command factory through the sprite module's framework.
  SpCityCommandFactoryRef() : factory(nullptr) {
    SpGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  }

  // Release the acquired identity after command execution or stack unwinding.
  ~SpCityCommandFactoryRef() {
    if (factory)
      factory->Release();
  }

  ScCommandFactory *factory;
};

// Allocate columns in descending order and initialize each byte from the live fill value.
SpCursorCells::SpCursorCells(u32 count, u32 rows, const u8 *fill) : width(count), height(rows) {
  columns = new u8 *[count];
  for (i32 x = i32(width - 1u); x >= 0; --x) {
    columns[x] = new u8[height];
    for (i32 z = i32(height - 1u); z >= 0; --z)
      columns[x][z] = *fill;
  }
}

// Retire the allocated columns in reverse order before releasing the pointer array.
SpCursorCells::~SpCursorCells() {
  if (columns) {
    for (i32 x = i32(width - 1u); x >= 0; --x)
      delete[] columns[x];
    delete[] columns;
  }
}

// Read every column in descending order, retaining a failure result while continuing later reads.
bool SpCursorCells::SerialRead(RsStream *stream) {
  bool result = true;
  for (i32 x = i32(width - 1u); x >= 0; --x)
    if (!stream->ReadBytes(columns[x], height))
      result = false;
  return result;
}

// Write every column in descending order even if an earlier write fails.
bool SpCursorCells::SerialWrite(RsStream *stream) const {
  bool result = true;
  for (i32 x = i32(width - 1u); x >= 0; --x)
    if (!stream->WriteBytes(columns[x], height))
      result = false;
  return result;
}

// At, fill columns in ascending order, rereading the source byte for each column.
void SpCursorCells::SetAll(const u8 *value) {
  u32 rows = height;
  for (u32 x = 0; x < width; ++x)
    std::memset(columns[x], *value, rows);
}

SpCursorByteMap::SpCursorByteMap() : references(0), initialized(false), cells(nullptr) {
}

SpCursorByteMap::~SpCursorByteMap() {
  if (initialized)
    SpCursorByteMap::Shutdown();
}

// Publish the byte-map identity for Unknown and both map interfaces, retaining it before returning.
bool SpCursorByteMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpCursorByteMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapBase, SpCursorByteMap);
  HANDLE_QUERY_INTERFACE(IID_ScCellMapU8, SpCursorByteMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Add a map reference and its matching SIMSPR director hold.
u32 SpCursorByteMap::AddRef() {
  ++references;
  SpGetDirector()->AddRef();
  return references;
}

// Remove a nonzero reference and director hold, then delete the map whenever its count reaches zero.
u32 SpCursorByteMap::Release() {
  if (references) {
    SpGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

u32 SpCursorByteMap::CellCountX() const {
  return cells->width;
}

u32 SpCursorByteMap::CellCountZ() const {
  return cells->height;
}

void SpCursorByteMap::GetValueSint32(u32 x, u32 z, i32 *value) const {
  *value = cells->columns[x][z];
}

bool SpCursorByteMap::InBounds(u32 x, u32 z) const {
  return x < CellCountX() && z < CellCountZ();
}

bool SpCursorByteMap::InBounds(u32 left, u32 top, u32 right, u32 bottom) const {
  return InBounds(left, top) && InBounds(right, bottom);
}

bool SpCursorByteMap::ValidRect(u32 left, u32 top, u32 right, u32 bottom) const {
  return left <= right && top <= bottom;
}

bool SpCursorByteMap::SerialRead(RsStream *stream) {
  return cells->SerialRead(stream);
}

bool SpCursorByteMap::SerialWrite(RsStream *stream) const {
  return cells->SerialWrite(stream);
}

// Replace an initialized grid, querying the city's row count before its column count.
bool SpCursorByteMap::Init(CtCity *city, const u8 *fill) {
  if (initialized)
    Shutdown();
  u32 rows = city->CellCountZ(), columns = city->CellCountX();
  cells = new SpCursorCells(columns, rows, fill);
  initialized = true;
  return true;
}

// Delete initialized grid storage, clear its pointer and then clear the initialized flag.
bool SpCursorByteMap::Shutdown() {
  if (initialized && cells) {
    delete cells;
    cells = nullptr;
  }
  initialized = false;
  return true;
}

void SpCursorByteMap::GetValue(u32 x, u32 z, u8 *value) const {
  *value = cells->columns[x][z];
}

void SpCursorByteMap::SetValue(u32 x, u32 z, const u8 *value) {
  cells->columns[x][z] = *value;
}

void SpCursorByteMap::SetAllCells(const u8 *value) {
  cells->SetAll(value);
}

// Fill each inclusive column segment, rereading the source byte for each column.
void SpCursorByteMap::SetValue(u32 left, u32 top, u32 right, u32 bottom, const u8 *value) {
  SpCursorCells *grid = cells;
  for (u32 x = left; x <= right; ++x)
    std::memset(grid->columns[x] + top, *value, bottom - top + 1u);
}

static constexpr SpCityCursorResource kSpCityCursorResources[] = {
    {"BLANK.cur", kSpBlankCursorId},
    {"CENTER CURSOR.cur", kSpCenterCursorId},
    {"QUERY CURSOR.cur", kSpQueryCursorId},
    {"REWARDS CURSOR.cur", kSpRewardsCursorId},
    {"DEMOLISH CLEAR CURSOR.cur", kSpDemolishClearCursorId},
    {"WAND OF DESTRUCTION CURSOR.cur", kSpWandOfDestructionCursorId},
    {"snapshot cursor.cur", kSpSnapshotCursorId},
    {"PAN UP ARROW.cur", kSpPanUpArrowCursorId},
    {"PAN UP LEFT ARROW.cur", kSpPanUpLeftArrowCursorId},
    {"PAN LEFT ARROW.cur", kSpPanLeftArrowCursorId},
    {"PAN DOWN LEFT ARROW.cur", kSpPanDownLeftArrowCursorId},
    {"PAN DOWN ARROW.cur", kSpPanDownArrowCursorId},
    {"PAN DOWN RIGHT ARROW.cur", kSpPanDownRightArrowCursorId},
    {"PAN RIGHT ARROW.cur", kSpPanRightArrowCursorId},
    {"PAN UP RIGHT ARROW.cur", kSpPanUpRightArrowCursorId},
    {"EARLY MOVER CURSOR.cur", kSpEarlyMoverCursorId},
    {"LEVEL TERRAIN CURSOR.cur", kSpLevelTerrainCursorId},
    {"LOWER TERRAIN CURSOR.cur", kSpLowerTerrainCursorId},
    {"MANY TREES CURSOR.cur", kSpManyTreesCursorId},
    {"PAINT WATER CURSOR.cur", kSpPaintWaterCursorId},
    {"PLANT TREE CURSOR.cur", kSpPlantTreeCursorId},
    {"RAISE LOWER TERRAIN CURSOR.cur", kSpRaiseLowerTerrainCursorId},
    {"RAISE TERRAIN CURSOR.cur", kSpRaiseTerrainCursorId},
    {"RIVER CURSOR.cur", kSpRiverCursorId},
    {"DE-ZONE CURSOR.cur", kSpDezoneCursorId},
    {"ZONE AIR PORTS CURSOR.cur", kSpZoneAirportsCursorId},
    {"ZONE COM HIGH CURSOR.cur", kSpZoneCommercialHighCursorId},
    {"ZONE COM LOW CURSOR.cur", kSpZoneCommercialLowCursorId},
    {"ZONE COM MED CURSOR.cur", kSpZoneCommercialMediumCursorId},
    {"ZONE IND HIGH CURSOR.cur", kSpZoneIndustrialHighCursorId},
    {"ZONE IND LOW CURSOR.cur", kSpZoneIndustrialLowCursorId},
    {"ZONE IND MED CURSOR.cur", kSpZoneIndustrialMediumCursorId},
    {"ZONE LANDFILL CURSOR.cur", kSpZoneLandfillCursorId},
    {"ZONE RES HIGH CURSOR.cur", kSpZoneResidentialHighCursorId},
    {"ZONE RES LOW CURSOR.cur", kSpZoneResidentialLowCursorId},
    {"ZONE RES MED CURSOR.cur", kSpZoneResidentialMediumCursorId},
    {"ZONE SEA PORTS CURSOR.cur", kSpZoneSeaportsCursorId},
    {"POWER LINES CURSOR.cur", kSpPowerLinesCursorId},
    {"WATER PIPES CURSOR.cur", kSpWaterPipesCursorId},
    {"ROAD.cur", kSpRoadCursorId},
    {"AVENUE.cur", kSpAvenueCursorId},
    {"ROAD TUNNEL.cur", kSpRoadTunnelCursorId},
    {"RAIL ROAD.cur", kSpRailroadCursorId},
    {"SUBWAY TUBE.cur", kSpSubwayTubeCursorId},
    {"ONRAMP CURSOR.cur", kSpOnrampCursorId},
    {"DISPATCH FIREFIGHTERS CURSOR.cur", kSpDispatchFirefightersCursorId},
    {"DISPATCH POLICE CURSOR.cur", kSpDispatchPoliceCursorId},
    {"DISPATCH PLANE CURSOR.cur", kSpDispatchPlaneCursorId},
    {"EMERGENCY CURSOR.cur", kSpEmergencyCursorId},
    {"SIGNPOST CURSOR.cur", kSpSignpostCursorId},
    {"BIG PARK CURSOR.cur", kSpBigParkCursorId},
    {"COLLEGE CURSOR.cur", kSpCollegeCursorId},
    {"FIRE CURSOR.cur", kSpFireCursorId},
    {"HOSPITAL CURSOR.cur", kSpHospitalCursorId},
    {"JAIL CURSOR.cur", kSpJailCursorId},
    {"LANDMARKS CURSOR.cur", kSpLandmarksCursorId},
    {"LIBRARY CURSOR.cur", kSpLibraryCursorId},
    {"MARINA CURSOR.cur", kSpMarinaCursorId},
    {"MUSEUM CURSOR.cur", kSpMuseumCursorId},
    {"POLICE CURSOR.cur", kSpPoliceCursorId},
    {"REWARDS CURSOR.cur", kSpRewardsCursorAliasId},
    {"SCHOOL CURSOR.cur", kSpSchoolCursorId},
    {"SMALL PARK CURSOR.cur", kSpSmallParkCursorId},
    {"STADIUM CURSOR.cur", kSpStadiumCursorId},
    {"ZOO CURSOR.cur", kSpZooCursorId},
    {"BUS STOP.cur", kSpBusStopCursorId},
    {"SUBWAY RAIL CONNECT.cur", kSpSubwayRailConnectCursorId},
    {"SUBWAY STATION.cur", kSpSubwayStationCursorId},
    {"TRAIN STATION.cur", kSpTrainStationCursorId},
    {"DESALINIZATION CURSOR.cur", kSpDesalinizationCursorId},
    {"INCINERATOR CURSOR.cur", kSpIncineratorCursorId},
    {"POWER PLANT CURSOR.cur", kSpPowerPlantCursorId},
    {"RECYCLING CENTER CURSOR.cur", kSpRecyclingCenterCursorId},
    {"WASTE TO ENERGY CURSOR.cur", kSpWasteToEnergyCursorId},
    {"WATER PUMPING STATION CURSOR.cur", kSpWaterPumpingStationCursorId},
    {"WATER TOWER PUMP CURSOR.cur", kSpWaterTowerPumpCursorId},
    {"WATER TREATMENT CURSOR.cur", kSpWaterTreatmentCursorId},
    {"BALLPARK CURSOR.cur", kSpBallparkCursorId},
    {"POND CURSOR.cur", kSpPondCursorId},
    {"PLAYGROUND CURSOR.cur", kSpPlaygroundCursorId},
};

// Construct a city window and return its unretained window identity to the class factory.
ScUnknown *SpCreateCityWindow() {
  return static_cast<WnSpriteRootWidget *>(new SpCityWindow());
}

SpCityWindow::SpCityWindow()
    : WnSpriteRootWidget(), framework(nullptr), windowManager(nullptr), application(SpGetApplication()), boxX(nullptr),
      currentView(nullptr), isoView(nullptr), cellCursor(nullptr), viewportBuffer(nullptr), unknown200{},
      viewportBounds{0, 0, 1, 1}, screenBufferBounds{0, 0, 1, 1}, cellCursorVisible(false), cursorWidth(1),
      cursorHeight(1), defaultCursor(kSpCenterCursorId), activeCursor(0), showCellCoordinates(false), font(nullptr),
      unknown276(-1), unknown280(-1), costShown(false), costDisplayDisableCount(0), costMode(0), costValue(0),
      costRectangle{}, unknown344{}, costHint(nullptr), unknown364(0), unknown368(0), simulationPausedByView(false),
      rightFocusPending(false), rightDragMoved(false), edgeScrollEnabled(false), unknown480(false), edgeScrollFlags{},
      edgeStateChanged(false), mouseScrolling(false), unknown487{}, mouseScrollX(0), mouseScrollY(0), unknown496(0),
      mouseScrollDeltaX(0), mouseScrollDeltaY(0), anchorCursorImage(nullptr), leftButtonDown(false),
      rightButtonDown(false), lastMouseX(0), lastMouseY(0), lastMouseModifiers(0), keyboardState(nullptr), unknown532{},
      inputReentrant(true), unknown540(false), unknown541(false), handlerMode(kSpHandlerDefault),
      previousHandlerMode(kSpHandlerUnset),
      activeHandler(nullptr), previousHandler(nullptr), commandFactory(nullptr), unknown564(false) {
  SpCityCommandFactoryRef factory;
  commandFactory = factory.factory;
  WnSpriteRootWidget::SetFlag(kWnFlagReorderable, false);
}

SpCityWindow::~SpCityWindow() {
}

CtChangeSender::CtChangeSender(u32 sender) : senderId(sender), references(0), updatesEnabled(1) {
  receivers.Reserve(2);
}

CtChangeSender::~CtChangeSender() {
  CtChangeSender::RemoveAll();
}

// Publish the sender's Unknown or city-change identity and retain it through the live virtual operation.
bool CtChangeSender::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtChangeSource);
  HANDLE_QUERY_INTERFACE(IID_CtChangeSource, CtChangeSource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the sender and its owning SIMSPR director.
u32 CtChangeSender::AddRef() {
  ++references;
  SpGetDirector()->AddRef();
  return references;
}

// Release a nonzero sender and module hold, deleting the sender when its count reaches zero.
u32 CtChangeSender::Release() {
  if (references) {
    SpGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

SpCityCursor::SpCityCursor()
    : CtChangeSender(kSpCellCursorChangeSenderId), initialized(0), empty(1), palette(0), defaultColor(35), unknown48(0),
      city(nullptr), selection(nullptr), cells(nullptr) {
}

SpCityCursor::~SpCityCursor() {
}

// Publish the cursor or its change-source identity and retain the selected object; unsupported IDs clear the output.
bool SpCityCursor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, UiCityCursor);
  HANDLE_QUERY_INTERFACE(IID_UiCityCursor, UiCityCursor);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtChangeSource);
  HANDLE_QUERY_INTERFACE(IID_CtChangeSource, CtChangeSource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the cursor's source count, then retain its owning sprite-module director.
u32 SpCityCursor::AddRef() {
  ++references;
  SpGetDirector()->AddRef();
  return references;
}

// Release a held module reference and destroy the complete cursor when its count reaches zero.
u32 SpCityCursor::Release() {
  if (references) {
    SpGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool SpCityCursor::DoMessage(ScMessage *) {
  return false;
}

void SpCityCursor::DoAnimationTick(u32) {
}

void SpCityCursor::GetAnimationTickPriority(u8 *, i32 *) {
}

// Allocate the city-sized cursor map and selection, then center the changed bounds and enable notifications.
bool SpCityCursor::Init(CtCity *value) {
  u32 height = value->CellCountZ(), width = value->CellCountX();
  bounds.minimum = {0, 0, 0};
  bounds.maximum = {i32((width - 1u) << kCtMicroCoordinateShift), i32((height - 1u) << kCtMicroCoordinateShift), 0};
  city = value;
  empty = 1;
  cells = ScAddRefNotNull(new SpCursorByteMap());
  u8 fill = 0;
  if (cells->Init(city, &fill)) {
    selection = ScAddRefNotNull(new SpCellSelection());
    u32 z = (cells->CellCountZ() >> 1) << kCtMicroCoordinateShift;
    u32 x = (cells->CellCountX() >> 1) << kCtMicroCoordinateShift;
    changedBounds.minimum = {i32(x), i32(z), 0};
    z = (cells->CellCountZ() >> 1) << kCtMicroCoordinateShift;
    x = (cells->CellCountX() >> 1) << kCtMicroCoordinateShift;
    changedBounds.maximum = {i32(x), i32(z), 0};
    static_cast<CtChangeSource *>(this)->SetEnabled(1);
    initialized = 1;
    return true;
  }
  Shutdown();
  return false;
}

// Shut down and release the backing byte map, then release the selection and clear initialization state.
bool SpCityCursor::Shutdown() {
  if (cells) {
    cells->Shutdown();
    cells->Release();
    cells = nullptr;
  }
  ScRelease(&selection);
  initialized = 0;
  return true;
}

u32 SpCityCursor::CellCountX() {
  return cells->CellCountX();
}

u32 SpCityCursor::CellCountZ() {
  return cells->CellCountZ();
}

bool SpCityCursor::InBounds(const CtCoord &position) const {
  return cells->InBounds(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift));
}

// Test both world-coordinate bounds through the backing map's rectangle predicate.
bool SpCityCursor::InBounds(const GeBounds &value) const {
  return cells->InBounds(u32(value.minimum.x >> kCtMicroCoordinateShift),
      u32(value.minimum.z >> kCtMicroCoordinateShift), u32(value.maximum.x >> kCtMicroCoordinateShift),
      u32(value.maximum.z >> kCtMicroCoordinateShift));
}

// At, replace a cell's low nibble and append its fixed-point coordinate to the selection.
void SpCityCursor::SetCell(i32 x, i32 z, u32 color) {
  u8 encoded = color == defaultColor ? 1 : color == 36 ? 15 : 0;
  u8 value = u8(color >> 24);
  cells->GetValue(u32(x), u32(z), &value);
  value = u8((value & 0xf0) | encoded);
  cells->SetValue(u32(x), u32(z), &value);
  selection->Append({i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0});
  empty = 0;
}

// At, update an inclusive cell rectangle and append each coordinate in X-then-Z order.
void SpCityCursor::SetBounds(i32 left, i32 top, i32 right, i32 bottom, u32 color) {
  u8 encoded = color == defaultColor ? 1 : color == 36 ? 15 : 0;
  u8 value = u8(color >> 24);
  for (i32 x = left; x <= right; ++x) {
    for (i32 z = top; z <= bottom; ++z) {
      cells->GetValue(u32(x), u32(z), &value);
      value = u8((value & 0xf0) | encoded);
      cells->SetValue(u32(x), u32(z), &value);
      selection->Append({i32(u32(x) << kCtMicroCoordinateShift), i32(u32(z) << kCtMicroCoordinateShift), 0});
    }
  }
  empty = 0;
}

// At, clear low color nibbles in the changed bounds and erase the stored selection.
void SpCityCursor::Clear() {
  i32 left = changedBounds.minimum.x >> kCtMicroCoordinateShift,
      top = changedBounds.minimum.z >> kCtMicroCoordinateShift;
  i32 right = changedBounds.maximum.x >> kCtMicroCoordinateShift,
      bottom = changedBounds.maximum.z >> kCtMicroCoordinateShift;
  u8 value = 0;
  for (i32 x = left; x <= right; ++x) {
    for (i32 z = top; z <= bottom; ++z) {
      cells->GetValue(u32(x), u32(z), &value);
      value &= 0xf0;
      cells->SetValue(u32(x), u32(z), &value);
    }
  }
  selection->points.Clear();
}

void SpCityCursor::SetColor(const CtCoord &position, u32 color) {
  SetCell(position.x >> kCtMicroCoordinateShift, position.z >> kCtMicroCoordinateShift, color);
  changedBounds.UnionXZ(changedBounds, position);
}

// Set the cells in the supplied bounds and expand the accumulated changed bounds to include that rectangle.
void SpCityCursor::SetColor(const GeBounds &value, u32 color) {
  SetBounds(value.minimum.x >> kCtMicroCoordinateShift, value.minimum.z >> kCtMicroCoordinateShift,
      value.maximum.x >> kCtMicroCoordinateShift, value.maximum.z >> kCtMicroCoordinateShift, color);
  changedBounds.UnionXZ(changedBounds, value);
}

// Decode the cell's low color nibble into the default color, the special color or zero.
void SpCityCursor::GetColor(const CtCoord &position, u32 *color) {
  u8 value = 0;
  cells->GetValue(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &value);
  value &= 15;
  *color = value == 1 ? defaultColor : value == 15 ? 36 : 0;
}

void SpCityCursor::SetPalette(u32 value) {
  if (palette != value)
    palette = value;
}

// Replace the stored default palette and color together when either word differs.
void SpCityCursor::SetDefaultColor(u32 value, u32 color) {
  if (palette != value || defaultColor != color) {
    palette = value;
    defaultColor = color;
  }
}

void SpCityCursor::GetDefaultColor(u32 *value, u32 *color) {
  *value = palette;
  *color = defaultColor;
}

bool SpCityCursor::Clear(bool update) {
  if (!empty) {
    Clear();
    if (update)
      UpdateRegion(bounds);
    empty = 1;
  }
  return true;
}

void SpCityCursor::SetCell(const CtCoord &position, u32 color) {
  Clear();
  SetColor(position, color);
  GeBounds region(position);
  UpdateRegion(region);
}

// Replace the cursor with a dominant-axis cell walk between the endpoints, then update the clipped region.
void SpCityCursor::SetLine(const CtCoord &first, const CtCoord &last, u32 color) {
  GeBounds region(first, last);
  region.NormalizeXZ();
  region.IntersectXZ(region, bounds);
  i32 x = first.x >> kCtMicroCoordinateShift, z = first.z >> kCtMicroCoordinateShift,
      endX = last.x >> kCtMicroCoordinateShift, endZ = last.z >> kCtMicroCoordinateShift;
  Clear();
  while (x != endX || z != endZ) {
    i32 dx = endX - x, dz = endZ - z;
    i32 absX = dx < 0 ? -dx : dx, absZ = dz < 0 ? -dz : dz;
    SetCell(x, z, color);
    if (absX > absZ)
      x += dx > 0 ? 1 : -1;
    else
      z += dz > 0 ? 1 : -1;
  }
  SetCell(x, z, color);
  UpdateRegion(region);
}

// Replace the cursor with a normalized, clipped rectangle and update the resulting region.
void SpCityCursor::SetRect(const GeBounds &value, u32 color) {
  GeBounds region(value);
  region.NormalizeXZ();
  region.IntersectXZ(region, bounds);
  Clear();
  SetBounds(region.minimum.x >> kCtMicroCoordinateShift, region.minimum.z >> kCtMicroCoordinateShift,
      region.maximum.x >> kCtMicroCoordinateShift, region.maximum.z >> kCtMicroCoordinateShift, color);
  UpdateRegion(region);
}

#if defined(_M_IX86)
// Native x87 storage for the cursor's extended-precision branch. MSVC double temporaries cannot retain these bits.
struct SpX87 {
  // Keep assembly-written bytes observable when MSVC copies a returned aggregate.
  volatile u8 bytes[10];

  // Reserve scratch storage. A load or arithmetic operation writes all ten bytes before any use.
  SpX87() = default;

  // Promote an exact input double into x87 storage. Integer and stored-float inputs reach this point without loss.
  SpX87(f64 value) {
    __asm {
      mov eax, this
      fld qword ptr value
      fstp tbyte ptr [eax]
    }
  }

  // Add without a double spill. Retain the caller's x87 precision and rounding control.
  SpX87 operator+(const SpX87 &other) const {
    SpX87 result;
    __asm {
      mov eax, this
      mov edx, other
      fld tbyte ptr [eax]
      fld tbyte ptr [edx]
      faddp st(1), st(0)
      lea eax, result
      fstp tbyte ptr [eax]
    }
    return result;
  }

  // Subtract in source operand order. Retain the complete extended intermediate.
  SpX87 operator-(const SpX87 &other) const {
    SpX87 result;
    __asm {
      mov eax, this
      mov edx, other
      fld tbyte ptr [eax]
      fld tbyte ptr [edx]
      fsubp st(1), st(0)
      lea eax, result
      fstp tbyte ptr [eax]
    }
    return result;
  }

  // Multiply using the active x87 environment. Storing an extended result introduces no double rounding.
  SpX87 operator*(const SpX87 &other) const {
    SpX87 result;
    __asm {
      mov eax, this
      mov edx, other
      fld tbyte ptr [eax]
      fld tbyte ptr [edx]
      fmulp st(1), st(0)
      lea eax, result
      fstp tbyte ptr [eax]
    }
    return result;
  }

  // Perform an ordered comparison while leaving the FPU stack balanced. Return the hardware status for either predicate.
  u16 Compare(const SpX87 &other) const {
    u16 status;
    __asm {
      mov eax, this
      mov edx, other
      fld tbyte ptr [edx]
      fld tbyte ptr [eax]
      fcompp
      fnstsw status
    }
    return status;
  }

  // Accept only an ordered greater result. Unordered operands do not select the positive branch.
  bool operator>(const SpX87 &other) const {
    return (Compare(other) & 0x4500) == 0;
  }

  // Accept only an ordered less result. Preserve source operand order in the hardware comparison.
  bool operator<(const SpX87 &other) const {
    return (Compare(other) & 0x4500) == 0x0100;
  }

  // Round directly to the original stored float. Avoid a double conversion between the x87 value and its destination.
  explicit operator f32() const {
    f32 result;
    __asm {
      mov eax, this
      fld tbyte ptr [eax]
      fstp dword ptr result
    }
    return result;
  }
};

static_assert(sizeof(SpX87) == 10);

// Match the installed CRT _ftol helper's signed-QWORD truncation and retain its low DWORD.
// Restore the exact control word afterward, including the caller's precision and rounding mode.
static u32 SpEllipseTruncatedWord(const SpX87 &value) {
  u16 previous, chopped;
  i64 converted;
  __asm {
    fnstcw previous
    mov ax, previous
    or ax, 0x0c00
    mov chopped, ax
    fldcw chopped
    mov eax, value
    fld tbyte ptr [eax]
    fistp qword ptr converted
    fldcw previous
  }
  return u32(converted);
}
#endif

// Convert the original x87 truncation result to its low DWORD. Masked out-of-range conversion produces zero there.
static u32 SpEllipseTruncatedWord(f64 value) {
  constexpr f64 limit = 9223372036854775808.0;
  if (value < -limit || value >= limit)
    return 0;
  return u32(i64(value));
}

// Replace the cursor using the original ellipse walk, float stores and active rounding mode.
bool SpCityCursor::SetEllipse(const CtCoord &first, const CtCoord &last, u32 color) {
  // Draw the same cell walk with the precision selected by the original floating-point environment.
  auto render = [&]<typename Wide>() {
    i32 x = first.x >> kCtMicroCoordinateShift, z = first.z >> kCtMicroCoordinateShift;
    i32 endX = last.x >> kCtMicroCoordinateShift, endZ = last.z >> kCtMicroCoordinateShift;
    i32 radius = x >= endX ? x - endX : endX - x;
    i32 verticalRadius = z >= endZ ? z - endZ : endZ - z;
    f32 centerX = f32(Wide(u32(x))), centerZ = f32(Wide(u32(z)));
    GeBounds region;
    Clear();
    if (!radius && !verticalRadius) {
      i32 cellX = i32(SpEllipseTruncatedWord(centerX)), cellZ = i32(SpEllipseTruncatedWord(centerZ));
      SetCell(cellX, cellZ, color);
    } else {
      i32 dx = i32(SpEllipseTruncatedWord(Wide(radius) + Wide(0.5))), dz = 0;
      f32 a2 = f32(Wide(radius) * radius), b2 = f32(Wide(verticalRadius) * verticalRadius);
      f32 twoA = 2.0f * a2, twoB = 2.0f * b2, fourA = 2.0f * twoA, fourB = 2.0f * twoB;
      f32 decision = f32(Wide(twoB) * dx * (dx - 1) + (Wide(1) - b2) * twoA + Wide(b2) * Wide(0.5));
      if (Wide(dx) * twoB > 0.0) {
        i32 limit;
        do {
          i32 left = i32(SpEllipseTruncatedWord(Wide(centerX) - dx));
          i32 top = i32(SpEllipseTruncatedWord(Wide(centerZ) - dz));
          i32 right = i32(SpEllipseTruncatedWord(Wide(centerX) + dx));
          i32 bottom = i32(SpEllipseTruncatedWord(Wide(centerZ) + dz));
          SetBounds(left, top, right, bottom, color);
          region.UnionXZ(
              region, CtCoord{i32(u32(left) << kCtMicroCoordinateShift), i32(u32(top) << kCtMicroCoordinateShift), 0});
          region.UnionXZ(region,
              CtCoord{i32(u32(right) << kCtMicroCoordinateShift), i32(u32(bottom) << kCtMicroCoordinateShift), 0});
          if (decision >= 0.0f) {
            --dx;
            dz = i32(f32(f32(dz) + 1.0f));
            decision = f32(Wide(fourA) * dz - Wide(fourB) * dx + decision + twoA);
            limit = dx;
          } else {
            dz = i32(f32(f32(dz) + 1.0f));
            decision = f32(Wide(fourA) * dz + decision + twoA);
            limit = dx - 1;
          }
        } while (Wide(twoA) * dz < Wide(twoB) * limit);
      }
      i32 error = i32(SpEllipseTruncatedWord((Wide(dx) * dx + Wide(1)) * twoB - Wide(fourB) * dx +
          ((Wide(dz) + Wide(1)) * dz - b2) * twoA + Wide(a2) * Wide(0.5)));
      while (dx >= 0) {
        i32 left = i32(SpEllipseTruncatedWord(Wide(centerX) - dx));
        i32 top = i32(SpEllipseTruncatedWord(Wide(centerZ) - dz));
        i32 right = i32(SpEllipseTruncatedWord(Wide(centerX) + dx));
        i32 bottom = i32(SpEllipseTruncatedWord(Wide(centerZ) + dz));
        SetBounds(left, top, right, bottom, color);
        region.UnionXZ(
            region, CtCoord{i32(u32(left) << kCtMicroCoordinateShift), i32(u32(top) << kCtMicroCoordinateShift), 0});
        region.UnionXZ(region,
            CtCoord{i32(u32(right) << kCtMicroCoordinateShift), i32(u32(bottom) << kCtMicroCoordinateShift), 0});
        --dx;
        if (error >= 0)
          error = i32(u32(error) + SpEllipseTruncatedWord(Wide(twoB) - Wide(fourB) * dx));
        else {
          dz = i32(f32(f32(dz) + 1.0f));
          error = i32(u32(error) + SpEllipseTruncatedWord(Wide(fourA) * dz - Wide(fourB) * dx + twoB));
        }
      }
    }
    empty = 0;
    UpdateRegion(region);
    return true;
  };
#if defined(_M_IX86)
  unsigned int control;
  _controlfp_s(&control, 0, 0);
  if ((control & _MCW_PC) == _PC_24)
    return render.operator()<f32>();
  if ((control & _MCW_PC) == _PC_64)
    return render.operator()<SpX87>();
  assert((control & _MCW_PC) == _PC_53);
#endif
  return render.operator()<f64>();
}

// Replace the cursor from one or two enumerated selections and update their combined bounds.
void SpCityCursor::SetCellList(SpSelection *first, u32 firstColor, SpSelection *second, u32 secondColor) {
  Clear();
  CtCoord positions[64];
  for (u32 i = 0; i < 64; ++i)
    positions[i] = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds region;
  u32 count = 0;
  first->Reset();
  if (first->Next(1, positions, &count) && count == 1)
    region = GeBounds(positions[0]);
  first->Reset();
  while (first->Next(64, positions, &count)) {
    for (u32 i = 0; i < count; ++i) {
      SetColor(positions[i], firstColor);
      region.UnionXZ(region, positions[i]);
    }
  }
  if (second) {
    second->Reset();
    while (second->Next(64, positions, &count)) {
      for (u32 i = 0; i < count; ++i) {
        SetColor(positions[i], secondColor);
        region.UnionXZ(region, positions[i]);
      }
    }
  }
  UpdateRegion(region);
}

// Synchronize changed cursor nibbles and send the affected cell coordinates in batches of at most 512.
bool SpCityCursor::UpdateRegion(const GeBounds &value) {
  GeBounds requested(value);
  requested.NormalizeXZ();
  GeBounds region(requested);
  if (!empty)
    region.UnionXZ(changedBounds, region);
  region.IntersectXZ(bounds, region);
  requested.IntersectXZ(bounds, requested);
  i32 left = region.minimum.x >> kCtMicroCoordinateShift, top = region.minimum.z >> kCtMicroCoordinateShift;
  i32 right = region.maximum.x >> kCtMicroCoordinateShift, bottom = region.maximum.z >> kCtMicroCoordinateShift;
  ScMessageServer *server = SpGetMessageServer();
  i32 coordinates[1024];
  ScMessage message(kSpMsg_CursorCellsChanged, reinterpret_cast<uptr>(coordinates), 0, 0);
  u32 count = 0;
  u8 cell = 0;
  for (i32 x = left; x <= right; ++x) {
    for (i32 z = top; z <= bottom; ++z) {
      cells->GetValue(u32(x), u32(z), &cell);
      if ((cell >> 4) != (cell & 15)) {
        cell = u8((cell & 15) | ((cell & 15) << 4));
        cells->SetValue(u32(x), u32(z), &cell);
        coordinates[count++] = x;
        coordinates[count++] = z;
        if (count >= 1024) {
          message.data2 = count >> 1;
          server->MessageSend(message);
          count = 0;
        }
      }
    }
  }
  if (count) {
    message.data2 = count >> 1;
    server->MessageSend(message);
  }
  changedBounds = bounds.CoordInBoundsXZ(requested.minimum) && bounds.CoordInBoundsXZ(requested.maximum)
      ? requested : region;
  return true;
}

bool SpCityCursor::GetEnumerator(SpSelection **out) {
  return selection->Clone(out) != 0;
}

u32 CtChangeSender::GetID() const {
  return senderId;
}

void CtChangeSender::SetEnabled(u8 enabled) {
  updatesEnabled = enabled;
}

u8 CtChangeSender::GetEnabled() const {
  return updatesEnabled;
}

// Publish the sender ID, replace any existing registration and retain the newly appended receiver.
bool CtChangeSender::AddReceiver(CtChangeReceiver *receiver, u32 *sender) {
  if (!receiver)
    return false;
  *sender = senderId;
  RemoveReceiver(receiver);
  *receivers.Append() = receiver;
  receiver->AddRef();
  return true;
}

// Release the last matching receiver, shift later registrations and reduce the live range.
bool CtChangeSender::RemoveReceiver(CtChangeReceiver *receiver) {
  if (!receiver)
    return false;
  isize last = isize(receivers.size) - 1, index = last;
  while (index >= 0 && receivers.data[index] != receiver)
    --index;
  if (index < 0)
    return false;
  receiver->Release();
  while (index < last) {
    receivers.data[index] = receivers.data[index + 1];
    ++index;
  }
  --receivers.size;
  return true;
}

// Release the captured receiver count in reverse order, shortening the live range after each callback.
bool CtChangeSender::RemoveAll() {
  isize index = receivers.size;
  while (--index >= 0) {
    receivers.data[index]->Release();
    --receivers.size;
  }
  return true;
}

bool CtChangeSender::NotifyAll() {
  if (updatesEnabled) {
    isize index = receivers.size;
    while (--index >= 0)
      receivers.data[index]->CellChangedAll(senderId);
  }
  return true;
}

bool CtChangeSender::Notify(u32 x, u32 z) {
  if (updatesEnabled) {
    isize index = receivers.size;
    while (--index >= 0)
      receivers.data[index]->CellChanged(senderId, x, z);
  }
  return true;
}

// Notify retained receivers of a rectangular change in reverse order when notifications are enabled.
bool CtChangeSender::Notify(u32 left, u32 top, u32 right, u32 bottom) {
  if (updatesEnabled) {
    isize index = receivers.size;
    while (--index >= 0)
      receivers.data[index]->CellChanged(senderId, left, top, right, bottom);
  }
  return true;
}
