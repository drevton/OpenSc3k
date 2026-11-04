#include <assert.h>

#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "ui/api.h"
#include "ui/ui_map_view.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// Installed atlas colors identify these schemes; their original map-view associations remain unresolved.
enum UiMapColorSchemeId : u32 {
  kUiOrangeMapPaletteId = 0x630e212f,
  kUiGreenMapPaletteId = 0xc30e2132,
  kUiBlueMapPaletteId = 0x630e2135,
  kUiSevenColorMapPaletteId = 0xa30e2139,
  kUiMutedGoldMapPaletteId = 0x830e21d5,
  kUiMutedGreenMapPaletteId = 0x230e21db,
  kUiMutedBlueMapPaletteId = 0x830e21df,
  kUiSixColorMapPaletteId = 0xe30e21e2,
};

// Each concrete map view uses its own selection identity when creating and forwarding city-overlay requests.
enum UiMapViewId : u32 {
  kUiCrimeMapViewId = 0x5fd,
  kUiFlammabilityMapViewId = 0x5fe,
  kUiPollutionMapViewId = 0x603,
  kUiAuraMapViewId = 0x5fa,
  kUiPopulationMapViewId = 0x5fc,
  kUiPowerMapViewId = 0x600,
  kUiLandValueMapViewId = 0x5ff,
  kUiTrafficMapViewId = 0x602,
  kUiWaterMapViewId = 0x601,
  kUiZoneMapViewId = 0x5fb,
};

// Pollution map option identities match their installed air, water and garbage captions.
enum UiPollutionMapOptionId : u32 {
  kUiMapAirPollutionOptionId = 0x30d2537,
  kUiMapWaterPollutionOptionId = 0x230d2540,
  kUiMapGarbagePollutionOptionId = 0xe30d2561,
};

// Map-view child identities and the scalar palettes shared by their legends and raster filters.
enum UiMapIdentity : u32 {
  kUiIntensityMapPaletteId = 0xa30e2116,
  kUiPopulationMapPaletteId = 0x630e211a,
  kUiShowInCityOutlineId = 0xe30d2568,
  kUiShowInCityButtonId = 0x230b8721,
  kUiMapLegendOutlineId = 0xe30d2564,
  kUiMapControlId = 0xa2e75b39,
  kUiMapWindowId = 0xe30d2563,
  kUiMapLegendBitmapId = 0xe30d256a,
  kUiMapLegendCaptionId = 0xe30d256b,
  kUiMapTitleOutlineId = 0xe30d256c,
  kUiMapTitleBitmapId = 0xe30d256d,
  kUiMapTitleCaptionId = 0xe30d256e,
  kUiMapIconOutlineId = 0xe30d2569,
  kUiMapTitleSegmentIdBase = 0xc3499f0d,
  kUiPollutionMapOptionsId = 0xe30d2562,
};

// The terrain palette selects its image row through the current landscape's city scheme.
enum UiMapPaletteId : u32 {
  kUiAuraMapPaletteId = 0xc30e2111,
  kUiPowerMapPaletteId = 0xe30e2122,
  kUiWaterMapPaletteId = 0xa30e2125,
  kUiZoningMapPaletteId = 0x830e2128,
  kUiZoningLowerLegendPaletteId = 0xa30e212c,
  kUiTerrainMapPaletteId = 0x830fa4d9,
};

static constexpr u32 kUiMapLegendLabelId = 0xdeadbeef;

UiMapView::UiMapView()
    : application(nullptr), map(nullptr), rotation(-1), legend(nullptr), compass{nullptr, nullptr, nullptr, nullptr},
      palette_image(nullptr) {
  application = ScAddRefNotNull(UiGetApplication());
}

UiMapView::~UiMapView() {
  ScRelease(&application);
}

bool UiMapView::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiMapViewIdentity, UiMapViewIdentity);
  return UiMapsView::QueryInterface(iid, out);
}

u32 UiMapView::AddRef() {
  return UiMapsView::AddRef();
}

u32 UiMapView::Release() {
  return UiMapsView::Release();
}

// Create the common view, map control, compass and city-overlay controls before retaining the palette image.
// The retained palette image must exist before the installed method's unconditional retain.
u8 UiMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (!UiMapsView::CreateView(view_owner, area, location))
    return 0;
  if (!CreateMapControl(area) || !CreateCompassWindows() || !CreateShowInCityWindow(area))
    return 0;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(0x21, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    palette_image = image.GetBuffer();
  }
  assert(palette_image != nullptr);
  palette_image->AddRef();
  return 1;
}

// Unprepare the live map and remove the three camera-change subscriptions in their original order.
u8 UiMapView::PrepareHideView() {
  if (GetMapControl())
    GetMapControl()->Unprepare();
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kTrMsg_TerrainChanged);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kCtMsg_MonthChanged);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ViewInfoUpdated);
  }
  return 1;
}

// Prepare the live map, synchronize it with the city camera and subscribe to camera-change notifications.
u8 UiMapView::PrepareShowView() {
  if (GetMapControl())
    GetMapControl()->Prepare();
  UpdateView();
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->AddNotification(static_cast<ScMessageTarget *>(this), kTrMsg_TerrainChanged);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kCtMsg_MonthChanged);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ViewInfoUpdated);
  }
  return 1;
}

// Recompute the map's camera-dependent display for the three subscribed notification types.
bool UiMapView::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ViewInfoUpdated || message->type == kCtMsg_MonthChanged ||
      message->type == kTrMsg_TerrainChanged)
    UpdateView();
  return true;
}

u8 UiMapView::OnInit(WnWindow *) {
  return 1;
}

// Unprepare the view, release the map, legend, compass windows and palette image, then run base-view shutdown.
u8 UiMapView::OnShutdown(WnWindow *receiver) {
  PrepareHideView();
  ScRelease(&map);
  ScRelease(&legend);
  for (u32 i = 0; i < 4; ++i) {
    if (compass[i]) {
      compass[i]->Release();
      compass[i] = nullptr;
    }
  }
  ScRelease(&palette_image);
  return UiMapsView::OnShutdown(receiver);
}

// Return the borrowed map-control interface, which may not yet have been created.
UiMapControl *UiMapView::GetMapControl() {
  return map;
}

// Transfer visible map filters to the city map and retain its special overlay across removal.
void UiMapView::SetShowInCity() {
  ScMessage message(kWnMsg_QueryCityMapControl);
  UiGetMessageServer()->MessageSend(message);
  if (!message.data1 || !map)
    return;
  WnWindow *recipient = reinterpret_cast<WnWindow *>(message.data1);
  UiMapControl *city_map = nullptr;
  if (!recipient->QueryInterface(IID_UiMapControl, reinterpret_cast<void **>(&city_map)))
    return;
  UiMapFilter *special = nullptr;
  i32 index = (i32)(city_map->GetFilterCount() - 1u);
  while (index >= 0) {
    UiMapFilter *filter = city_map->GetFilterByIndex((u32)index);
    if (filter && filter->GetFilterId() != 10001) {
      if (!special && filter->GetFilterId() == 10049)
        special = ScAddRefNotNull(filter);
      u32 identifier = filter->GetFilterId();
      city_map->ReleaseFilter(identifier);
    }
    --index;
  }
  for (u32 i = 0; i < map->GetFilterCount(); ++i) {
    UiMapFilter *filter = map->GetFilterByIndex(i);
    if (filter && filter->GetFilterId() != 10001 && filter->IsVisible())
      city_map->AddFilter(filter);
  }
  if (special) {
    city_map->AddFilter(special);
    special->Release();
  }
  city_map->DirtyCellRegion(nullptr);
  city_map->Release();
}

// Update the map rotation and compass labels only when the city-camera direction changes.
// Each resource string is released before its corresponding label callback, as in the installed view.
u8 UiMapView::DoCityViewRotation(i32 direction) {
  if ((u32)direction >= 4)
    return 0;
  i32 previous = rotation;
  rotation = direction;
  if (previous == direction)
    return 1;
  if (map) {
    static const i32 angles[4] = {225, 315, 45, 135};
    map->SetRotation(angles[direction]);
  }
  if (compass[0] && compass[1] && compass[2] && compass[3]) {
    static const u32 instances[4] = {0x11d, 0x11e, 0x120, 0x11f};
    ScString text;
    for (u32 i = 0; i < 4; ++i) {
      {
        UiTextRef resource(instances[i], kRsGameTextGroup);
        ScString result(resource.GetText());
        ScString copy(result);
        ScString value(copy);
        text = value;
      }
      compass[((u32)rotation + i) % 4]->SetCaption(text);
    }
  }
  GetAssociatedIGZWin()->InvalidateSelfAndParents();
  return 1;
}

u8 UiMapView::DoCityViewZoom(i32) {
  return 1;
}

// Request the matching city overlay while suppressing recursive requests from its message callbacks.
// A supported map ID is required because the original otherwise reads an unwritten overlay selection.
u8 UiMapView::DoShowInCity() {
  static u8 active;
  if (active == 1)
    return 1;
  active = 1;
  SpViewInterface *view = UiGetCurrentView();
  if (!view) {
    active = 0;
    return 0;
  }
  i32 city_view;
  switch (*GetViewId()) {
  case kUiAuraMapViewId:
    city_view = 10;
    break;
  case kUiZoneMapViewId:
    city_view = 18;
    break;
  case kUiPopulationMapViewId:
    city_view = 17;
    break;
  case kUiCrimeMapViewId:
    city_view = 11;
    break;
  case kUiFlammabilityMapViewId:
    city_view = 13;
    break;
  case kUiLandValueMapViewId:
    city_view = 14;
    break;
  case kUiPowerMapViewId:
    city_view = 12;
    break;
  case kUiWaterMapViewId:
    city_view = 2;
    break;
  case kUiTrafficMapViewId:
    city_view = 16;
    break;
  case kUiPollutionMapViewId:
    city_view = 15;
    break;
  default:
    assert(false && "Map overlay requires a supported view identifier");
    return 0;
  }
  u32 flags = view->GetCityViewFlags();
  if (view->GetCityViewID() != city_view) {
    ScMessage message(kWnMsg_SetCityViewParams, (u32)city_view, flags, 1);
    UiGetMessageServer()->MessagePost(message, false);
  }
  active = 0;
  return 1;
}

i32 UiMapView::HasCityWindow() {
  return application->GetISC3WinCityView() != nullptr;
}

// Return the application's borrowed city-window interface.
SpCityWindowView *UiMapView::GetCityWindow() {
  return application->GetISC3WinCityView();
}

// Recheck the application's city window and borrow its current view when the first lookup succeeded.
SpViewInterface *UiMapView::GetCurrentView() {
  if (!HasCityWindow())
    return nullptr;
  return GetCityWindow()->GetCurrentView();
}

// Copy the current camera's zoom and rotation only when an active city view exists.
u8 UiMapView::GetZoomAndRotation(i32 *zoom, i32 *rotation_out) {
  SpViewInterface *view = GetCurrentView();
  if (!view)
    return 0;
  view->GetZoomAndRotation(zoom, rotation_out);
  return 1;
}

u8 UiMapView::UpdateView() {
  if (GetMapControl())
    GetMapControl()->DirtyCellRegion(nullptr);
  i32 zoom = 0, camera_rotation = 0;
  GetZoomAndRotation(&zoom, &camera_rotation);
  DoCityViewRotation(camera_rotation);
  DoCityViewZoom(zoom);
  return 1;
}

// Create the crime view with its circle legend, colored raster and point overlay.
// Palette requests and borrowed-source lookups follow the installed order even after intermediate failures.
u8 UiCrimeMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiCrimeMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 274);
  if (result)
    result = SetIconButton(0x5e5, 4);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiIntensityMapPaletteId);
  if (result)
    result = CreateCircleAndOutlineResource(area, 0x2a1);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiIntensityMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x23e, false, 0, 4);
    CreateLegendCaptionResource(area, 0x124, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiCrimeMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiIntensityMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(overlay);
    }
    GxRasterSource *markers = view_owner->FindRaster(kUiCrimeMarkersRasterId);
    if (markers && result)
      return GetMapControl()->AddFilter(markers);
  }
  return result;
}

// Palette requests and borrowed-source lookups follow the installed order even after intermediate failures.
u8 UiFlammabilityMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiFlammabilityMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 276);
  if (result)
    result = SetIconButton(0x5e5, 7);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiIntensityMapPaletteId);
  if (result)
    result = CreateCircleAndOutlineResource(area, 0x2a5);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiIntensityMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x240, false, 0, 4);
    CreateLegendCaptionResource(area, 0x241, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiFlammabilityMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiIntensityMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(overlay);
    }
    GxRasterSource *markers = view_owner->FindRaster(kUiFlammabilityMarkersRasterId);
    if (markers && result)
      return GetMapControl()->AddFilter(markers);
  }
  return result;
}

// Build the background outline, city-overlay button and caption in their original resource and attachment order.
// The caller's rectangle is unused; only failure to attach the caption changes the final success byte.
u8 UiMapView::CreateShowInCityWindow(ScRect *) {
  WnOutlineControl *outline = nullptr;
  if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
    outline->SetFlags(kWnOutlinePlain | kWnOutlinePolygon | kWnOutlineFill);
    ScRect area{20, 300, 117, 365};
    outline->SetFillColor(UiGetWindowResources()->GetColor(kWnThemePanelFillColorId));
    outline->SetDarkOutlineColor(UiGetWindowResources()->GetColor(kWnThemeBorderColorId));
    outline->GetWindow()->SetArea(area);
    outline->GetWindow()->SetID(kUiShowInCityOutlineId);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    ScPoint points[6] = {{0, 0}, {0, 65}, {97, 65}, {97, 30}, {30, 30}, {30, 0}};
    outline->SetShapePoly(6, points);
    WnWindow *parent = window->GetWindow();
    if (!parent->ChildAdd(outline->GetWindow()))
      UiGetWindowManager()->DestroyWindow(outline->GetWindow());
    outline->Release();
  }

  ScRect image_area{0, 0, 26, 25};
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(2, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  if (image)
    image->GetBufferArea(&image_area);
  ScString empty;
  WnButton *button = UiGetWindowResources()->CreateFixedButton(kUiShowInCityButtonId, empty, image, &image_area, 0);
  if (button) {
    WnWindow *button_window = button->GetWindow();
    if (!image) {
      button_window->SetFillColor(UiGetWindowResources()->GetColor(kWnColorSilver));
      button_window->SetSize(26, 25);
      button->SetButtonFlag(kWnButtonAutoFit, false);
    }
    button->GetWindow()->SetFlag(kWnFlagReorderable, false);
    button->SetButtonFlag(kWnButtonShowText, false);
    button_window->GZWinMoveTo(24, 305);
    WnWindow *parent = window->GetWindow();
    if (!parent->ChildAdd(button_window))
      window->GetWindow()->GetWindowManager()->DestroyWindow(button_window);
    button->Release();
  }

  WnLabelControl *label;
  {
    UiTextRef resource(kRsGameUseAsMapText, kRsGameTextGroup);
    ScString result(resource.GetText());
    ScString first(result);
    ScString second(first);
    label = UiGetWindowResources()->CreateLabel(kUiMapLegendLabelId, second);
  }
  if (!label)
    return 1;
  label->GetWindow()->SetFlag(kWnFlagReorderable, false);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  label->SetAlignment(kWnLabelCenterMiddle);
  label->SetBackground(false);
  label->SetLabelFlag(kWnLabelWrapText, true);
  label->GetWindow()->GZWinMoveTo(20, 330);
  label->GetWindow()->SetSize(97, 35);
  WnWindow *parent = window->GetWindow();
  if (parent->ChildAdd(label->GetWindow())) {
    label->Release();
    return 1;
  }
  UiGetWindowManager()->DestroyWindow(label->GetWindow());
  label->Release();
  return 0;
}

// Load the compass image and, when its bitmap control is available, attach the image and four directional labels.
// Failed resources leave their corresponding fields empty; the original method still reports success.
u8 UiMapView::CreateCompassWindows() {
  u32 transparent = UiGetWindowResources()->GetColor(kWnColorUiTransparency);
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(13, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  if (!image)
    return 1;
  ScRect area = *image->GetBufferArea();
  i32 offset_x = (i32)(25u - (u32)area.left), offset_y = (i32)(20u - (u32)area.top);
  area.left = 25;
  area.top = 20;
  area.right = (i32)((u32)area.right + (u32)offset_x);
  area.bottom = (i32)((u32)area.bottom + (u32)offset_y);
  image->SetTransparency(transparent);
  if (!UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&legend)))
    return 1;
  legend->SetImage(image);
  legend->GetWindow()->SetArea(area.left, area.top, area.right, area.bottom);
  legend->GetWindow()->SetID(kUiMapLegendOutlineId);
  legend->GetWindow()->SetFlag(kWnFlagReorderable, false);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(legend->GetWindow());
  area.left = (i32)((u32)area.left + 3);
  area.right = (i32)((u32)area.right - 3);
  static const u32 alignments[4] = {3, 1, 5, 7};
  for (u32 index = 0; index < 4; ++index) {
    {
      ScString empty;
      compass[index] = UiGetWindowResources()->CreateLabel(16896 + index, empty);
    }
    if (!compass[index])
      continue;
    compass[index]->GetWindow()->SetFlag(kWnFlagReorderable, false);
    compass[index]->GetWindow()->SetArea(area);
    compass[index]->SetFont(UiGetWindowResources()->GetFont(kWnFontUiTitle));
    u32 first = compass[index]->GetWindow()->MakeFillColor(214, 218, 255);
    compass[index]->SetTextColor(0, first);
    u32 second = compass[index]->GetWindow()->MakeFillColor(124, 144, 255);
    compass[index]->SetTextColor(1, second);
    compass[index]->SetAlignment(alignments[index]);
    compass[index]->GetWindow()->SetFlag(kWnFlagReorderable, false);
    WnWindow *view_window = window->GetWindow();
    view_window->ChildAdd(compass[index]->GetWindow());
  }
  return 1;
}

// The caller supplies a distinct local output and ignores the original helper's return register.
void UiMapView::CalculateMapControlRectEffects(const ScRect &area, ScRect *out) {
  i32 width = (i32)((u32)area.right - (u32)area.left);
  i32 height = (i32)((u32)area.bottom - (u32)area.top);
  i32 side = width < height ? width : height;
  i32 x = (i32)((u32)width - (u32)side) / 2;
  i32 y = (i32)((u32)height - (u32)side) / 2;
  out->left = x;
  out->top = y;
  out->right = (i32)((u32)x + (u32)side);
  out->bottom = (i32)((u32)y + (u32)side);
}

// Create the original map control, surround it with two outlines and attach its window to the generic view.
// Failed factory or control creation returns the source byte; the outline helper's return is discarded.
u8 UiMapView::CreateMapControl(ScRect *area) {
  u8 result = UiGetCom()->GetClassObject(kUiMapControlClassId, IID_UiMapControl, reinterpret_cast<void **>(&map));
  if (result) {
    if (map)
      result = map->CreateControl(kUiMapControlId);
    if (result) {
      ScRect bounds;
      CalculateMapControlRectEffects(*area, &bounds);
      bounds.left = (i32)((u32)bounds.left + 1);
      bounds.right = (i32)((u32)bounds.right + 1);
      bounds.top = (i32)((u32)bounds.top - 2);
      bounds.bottom = (i32)((u32)bounds.bottom - 2);
      CreateMapOutlines(&bounds, 2);
      bounds.left = (i32)((u32)bounds.left - 1);
      bounds.right = (i32)((u32)bounds.right - 1);
      bounds.top = (i32)((u32)bounds.top - 1);
      bounds.bottom = (i32)((u32)bounds.bottom - 1);
      map->AsIGZWin()->SetArea(bounds);
      map->AsIGZWin()->SetID(kUiMapWindowId);
      map->AsIGZWin()->SetFlag(kWnFlagReorderable, false);
      map->SetDrawWindowOutline(false);
      map->SetDrawImageOutline(false);
      WnWindow *parent = window->GetWindow();
      result = parent->ChildAdd(map->AsIGZWin());
    }
  }
  return result;
}

// Color the source legend mask with its selected map palette, then attach the resulting bitmap window.
// Failed source or target locks retain the original partial ownership and lock effects.
u8 UiMapView::CreateLegendBitmap(UiMapsWindow *view_owner, ScRect *, u32 identifier, bool upper) {
  GxPaletteView *palette = view_owner->GetPalette(identifier);
  if (!palette) {
    CreatePalette(view_owner, identifier);
    palette = view_owner->GetPalette(identifier);
    if (!palette)
      return 0;
  }
  i32 entries = palette->GetNumberEntries();
  GxBuffer *source;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(upper ? 46 : 45, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    source = image.GetBuffer();
  }
  GxBuffer *target = nullptr;
  UiGetGraphics()->CreateBuffer(&target);
  assert(source != nullptr && target != nullptr);
  GxDepthType depth = source->GetZDepth();
  GxPaletteView *source_palette = source->GetPalette();
  GxColorType color = source->GetColorType();
  u32 height = source->Height(), width = source->Width();
  if (!target->Init(width, height, color, source_palette, depth, GxVramRequest::SystemMemory, false) ||
      !source->Lock(GxLockFlags::GetSetPixel) || !target->Lock(GxLockFlags::GetSetPixel))
    return 0;
  u32 background = UiGetWindowResources()->GetColor(kWnColorUiTransparency);
  u32 y = 0, pixel;
  if (source->Height()) {
    f32 entry_count = (f32)entries;
    do {
      f32 row = (f32)y;
      u32 current_height = source->Height();
      f64 position = ((f64)row / (f64)current_height) * (f64)entry_count;
      assert(position >= -2147483648.0 && position < 2147483648.0);
      palette->GetColor((i32)position, &pixel);
      for (u32 x = 0; x < source->Width(); ++x)
        target->SetPixel(x, y, source->GetPixel(x, y) ? background : pixel);
      ++y;
    } while (y < source->Height());
  }
  source->Unlock(GxLockFlags::GetSetPixel);
  target->Unlock(GxLockFlags::GetSetPixel);
  target->SetTransparency(background);
  WnBitmapControl *const unwritten = reinterpret_cast<WnBitmapControl *>((uptr)1);
  WnBitmapControl *bitmap = unwritten;
  u8 result = UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap));
  assert(!result || bitmap != unwritten);
  if (result && bitmap) {
    ScRect bounds = *target->GetBufferArea();
    i32 top = upper ? 18 : 323;
    bounds.right = (i32)((u32)bounds.right + 254u - (u32)bounds.left);
    bounds.bottom = (i32)((u32)bounds.bottom + (u32)top - (u32)bounds.top);
    bounds.left = 254;
    bounds.top = top;
    bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
    bitmap->GetWindow()->SetArea(bounds.left, bounds.top, bounds.right, bounds.bottom);
    bitmap->GetWindow()->SetID(kUiMapLegendBitmapId);
    bitmap->SetImage(target);
    target->Release();
    WnWindow *parent = window->GetWindow();
    result = parent->ChildAdd(bitmap->GetWindow());
    bitmap->Release();
  }
  return result;
}

// Create and position one legend caption, then attach it to the generic view window.
// The source does not read the area argument; its last caption is aligned separately at the legend top.
u8 UiMapView::CreateLegendCaption(ScRect *, const ScStringView &text, bool upper, u32 index, u32 count) {
  i32 top = upper ? 13 : 318, height = upper ? 60 : 75;
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(0, text);
  assert(label != nullptr);
  label->GetWindow()->SetFlag(kWnFlagReorderable, false);
  label->SetBackground(false);
  label->GetWindow()->SetSize(98, height);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  u32 color = label->GetWindow()->MakeFillColor(121, 125, 181);
  label->SetTextColor(color);
  label->SetLabelFlag(kWnLabelWrapText, true);
  if (index == count - 1) {
    label->SetAlignment(kWnLabelLeftBottom);
    label->GetWindow()->GZWinMoveTo(292, top);
  } else {
    label->SetAlignment(kWnLabelLeftTop);
    WnWindow *label_window = label->GetWindow();
    assert(count != 0);
    f64 position = (1.0 / (f64)count) * (f64)height * (f64)(i32)index + (f64)top;
    assert(position >= -2147483648.0 && position < 2147483648.0);
    label_window->GZWinMoveTo(292, (i32)position);
  }
  label->GetWindow()->SetID(kUiMapLegendCaptionId);
  WnWindow *parent = window->GetWindow();
  if (parent->ChildAdd(label->GetWindow())) {
    label->Release();
    return 1;
  }
  UiGetWindowManager()->DestroyWindow(label->GetWindow());
  label->Release();
  return 0;
}

// Resolve and copy the caption resource before calling the original caption builder.
// These temporary strings and the resource remain live until that builder returns.
void UiMapView::CreateLegendCaptionResource(ScRect *area, u32 instance, bool upper, u32 index, u32 count) {
  UiTextRef resource(instance, kRsGameTextGroup);
  ScString result(resource.GetText());
  ScString first(result);
  ScString second(first);
  CreateLegendCaption(area, second, upper, index, count);
}

// Resolve and copy the resource used by the circle legend before invoking its original builder.
// The string and resource temporaries remain live until the builder returns.
u8 UiMapView::CreateCircleAndOutlineResource(ScRect *area, u32 instance) {
  UiTextRef resource(instance, kRsGameTextGroup);
  ScString result(resource.GetText());
  ScString first(result);
  ScString second(first);
  return CreateCircleAndOutline(area, second);
}

// Build the top legend outline, its circle image and caption, attaching each through the real window interfaces.
// The source does not read the area argument and returns the bitmap creation or attachment result after the caption.
u8 UiMapView::CreateCircleAndOutline(ScRect *, const ScStringView &text) {
  WnOutlineControl *outline = nullptr;
  if (!UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline)) ||
      !outline)
    return 0;
  outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
  outline->SetFlags(kWnOutlineInset | kWnOutlineFill);
  outline->SetFillColor(UiGetWindowResources()->GetColor(kWnThemeBackgroundColorId));
  ScRect outline_area{249, 8, 377, 51};
  outline->GetWindow()->SetArea(outline_area);
  outline->GetWindow()->SetID(kUiMapTitleOutlineId);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(outline->GetWindow());
  outline->Release();

  GxBuffer *image = nullptr;
  UiGetGraphics()->CreateBuffer(&image);
  assert(image != nullptr);
  GxColorType color(GxColorType::Color::RGB16);
  GxDepthType depth(GxDepthType::Depth::None);
  image->Init(24, 24, color, nullptr, depth, GxVramRequest::SystemMemory, false);
  if (image->Lock(GxLockFlags::Fill)) {
    image->Fill(UiGetWindowResources()->GetColor(kWnThemeBackgroundColorId), 0, 0, 24, 24, nullptr);
    image->Unlock(GxLockFlags::Fill);
  }
  if (image->Lock(GxLockFlags::Circle)) {
    image->DrawCircle(0, 0, 20, 20, 3, 1);
    image->Unlock(GxLockFlags::Circle);
  }
  if (image->Lock(GxLockFlags::FillCircle)) {
    image->FillCircle(8, 8, 12, 12, 3);
    image->Unlock(GxLockFlags::FillCircle);
  }
  WnBitmapControl *const unwritten = reinterpret_cast<WnBitmapControl *>((uptr)1);
  WnBitmapControl *bitmap = unwritten;
  u8 result = UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap));
  assert(!result || bitmap != unwritten);
  if (result && bitmap) {
    ScRect area = *image->GetBufferArea();
    area.right = (i32)((u32)area.right + 259u - (u32)area.left);
    area.bottom = (i32)((u32)area.bottom + 18u - (u32)area.top);
    area.left = 259;
    area.top = 18;
    bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
    bitmap->GetWindow()->SetArea(area.left, area.top, area.right, area.bottom);
    bitmap->GetWindow()->SetID(kUiMapTitleBitmapId);
    bitmap->SetImage(image);
    WnWindow *view_window = window->GetWindow();
    result = view_window->ChildAdd(bitmap->GetWindow());
    bitmap->Release();
  }
  image->Release();
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(0, text);
  assert(label != nullptr);
  label->GetWindow()->SetFlag(kWnFlagReorderable, false);
  label->SetBackground(false);
  label->GetWindow()->SetSize(90, 39);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  u32 label_color = label->GetWindow()->MakeFillColor(121, 125, 181);
  label->SetTextColor(label_color);
  label->SetLabelFlag(kWnLabelWrapText, true);
  label->GetWindow()->GZWinMoveTo(284, 10);
  label->GetWindow()->SetID(kUiMapTitleCaptionId);
  WnWindow *view_window = window->GetWindow();
  if (!view_window->ChildAdd(label->GetWindow())) {
    UiGetWindowManager()->DestroyWindow(label->GetWindow());
    label->Release();
    return 0;
  }
  label->Release();
  return result;
}

// Create the requested nested diamond outlines and attach each one to the map view's generic window.
// The mutable rectangle shrinks after every attempt, including failed outline creation or attachment.
u8 UiMapView::CreateMapOutlines(ScRect *area, u32 count) {
  for (u32 index = 0; index < count; ++index) {
    WnOutlineControl *outline = nullptr;
    if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
      outline->SetFlags(kWnOutlineInset | kWnOutlinePolygon);
      outline->GetWindow()->SetArea(*area);
      outline->GetWindow()->SetID(kUiMapTitleSegmentIdBase + index);
      outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
      i32 width = (i32)((u32)area->right - (u32)area->left);
      i32 height = (i32)((u32)area->bottom - (u32)area->top);
      i32 half_width = width / 2, half_height = height / 2;
      ScPoint points[8] = {{0, (i32)((u32)half_height - 1)}, {0, half_height},
          {(i32)((u32)half_width - 1), (i32)((u32)height - 1)}, {half_width, (i32)((u32)height - 1)},
          {(i32)((u32)width - 1), half_height}, {(i32)((u32)width - 1), (i32)((u32)half_height - 1)}, {half_width, 0},
          {(i32)((u32)half_width - 1), 0}};
      outline->SetShapePoly(8, points);
      WnWindow *parent = window->GetWindow();
      if (!parent->ChildAdd(outline->GetWindow()))
        UiGetWindowManager()->DestroyWindow(outline->GetWindow());
      outline->Release();
    }
    area->left = (i32)((u32)area->left + 1);
    area->right = (i32)((u32)area->right - 1);
    area->top = (i32)((u32)area->top + 1);
    area->bottom = (i32)((u32)area->bottom - 1);
  }
  return 1;
}

// Create the upper or lower six-point legend outline and attach it to the generic view window.
// The original method does not read the supplied rectangle and releases the outline after attachment.
u8 UiMapView::CreateLegendOutline(ScRect *, bool upper) {
  WnOutlineControl *outline = nullptr;
  u8 result = UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline));
  if (!result || !outline)
    return result;
  outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
  outline->SetFlags(kWnOutlineInset | kWnOutlinePolygon | kWnOutlineFill);
  outline->SetFillColor(UiGetWindowResources()->GetColor(kWnThemeBackgroundColorId));
  ScRect area{249, upper ? 13 : 318, 377, upper ? 73 : 393};
  outline->GetWindow()->SetArea(area);
  outline->GetWindow()->SetID(kUiMapIconOutlineId);
  i32 width = area.right - area.left, height = area.bottom - area.top;
  ScPoint points[6];
  if (upper) {
    points[0] = {0, 0};
    points[1] = {0, height - 18};
    points[2] = {18, height};
    points[3] = {width, height};
    points[4] = {width, 0};
    points[5] = {1, 0};
  } else {
    points[0] = {width, height};
    points[1] = {width, 0};
    points[2] = {18, 0};
    points[3] = {0, 18};
    points[4] = {0, height};
    points[5] = {width - 1, height};
  }
  outline->SetShapePoly(6, points);
  WnWindow *parent = window->GetWindow();
  result = parent->ChildAdd(outline->GetWindow());
  outline->Release();
  return result;
}

// Create a logical palette from the matching row of the retained map palette image and register it with the owner.
// The source retains its newly allocated palette on a failed image lock; this method preserves that ownership effect.
u8 UiMapView::CreatePalette(UiMapsWindow *view_owner, u32 identifier) {
  if (view_owner->GetPalette(identifier))
    return 1;
  if (!palette_image)
    return 0;
  u32 row, entries;
  switch (identifier) {
  case kUiAuraMapPaletteId:
    row = 0;
    entries = 32;
    break;
  case kUiIntensityMapPaletteId:
    row = 1;
    entries = 32;
    break;
  case kUiPopulationMapPaletteId:
    row = 2;
    entries = 32;
    break;
  case kUiTerrainMapPaletteId: {
    row = 4;
    CtSchemeManager *scheme = UiGetApplication()->CitySchemeManager();
    if (scheme)
      scheme->GetDataMapsTerrainPaletteForCurrentLandScape(&row);
    entries = 12;
    break;
  }
  case kUiPowerMapPaletteId:
    row = 5;
    entries = 2;
    break;
  case kUiWaterMapPaletteId:
    row = 6;
    entries = 2;
    break;
  case kUiZoningMapPaletteId:
    row = 7;
    entries = 3;
    break;
  case kUiZoningLowerLegendPaletteId:
    row = 8;
    entries = 4;
    break;
  case kUiOrangeMapPaletteId:
    row = 10;
    entries = 3;
    break;
  case kUiGreenMapPaletteId:
    row = 11;
    entries = 3;
    break;
  case kUiBlueMapPaletteId:
    row = 12;
    entries = 3;
    break;
  case kUiSevenColorMapPaletteId:
    row = 13;
    entries = 7;
    break;
  case kUiMutedGoldMapPaletteId:
    row = 14;
    entries = 3;
    break;
  case kUiMutedGreenMapPaletteId:
    row = 15;
    entries = 3;
    break;
  case kUiMutedBlueMapPaletteId:
    row = 16;
    entries = 3;
    break;
  case kUiSixColorMapPaletteId:
    row = 31;
    entries = 6;
    break;
  default:
    return 0;
  }
  GxPalette *palette = new GxPalette;
  palette->AddRef();
  palette->MakeGenericPalette((i32)entries, 32);
  if (!palette_image->Lock(GxLockFlags::GetSetPixel))
    return 0;
  for (u32 index = 0; index < entries; ++index) {
    u32 pixel = palette_image->GetPixel(index, row);
    palette->SetColor((i32)index, pixel);
  }
  palette_image->Unlock(GxLockFlags::GetSetPixel);
  view_owner->AddPalette(identifier, palette);
  palette->Release();
  return 1;
}

// Create the pollution view, its option group and the four borrowed terrain and pollution filters.
// The option group is built even when earlier view setup failed, matching the installed callback order.
// Defined execution requires a created group and successful attachment before the original later group calls.
u8 UiPollutionMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiPollutionMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 280);
  if (result)
    result = SetIconButton(0x5e5, 15);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiIntensityMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiIntensityMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x246, false, 0, 4);
    CreateLegendCaptionResource(area, 0x247, false, 3, 4);
  }
  ScRect option_area{249, 3, 369, 45};
  WnOptionControl *options;
  {
    UiTextRef resource(kRsGameShowText, kRsGameTextGroup);
    ScString text(resource.GetText());
    ScString first(text);
    ScString second(first);
    options = UiGetWindowResources()->CreateOptGrp(kUiPollutionMapOptionsId, second, &option_area);
  }
  assert(options);
  options->SetOptionFlag(1, 1);
  options->SetOptionFlag(4, 0);
  options->SetOptionFlag(8, 0);
  options->SetAutoFillType(1, 0);
  options->SetGutters(0, 0);
  options->GetWindow()->SetFlag(kWnFlagReorderable, false);
  options->SetButtonFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  u32 color = options->GetWindow()->MakeFillColor(121, 125, 181);
  options->SetFontColor(0, color);
  color = options->GetWindow()->MakeFillColor(121, 125, 181);
  options->SetFontColor(2, color);
  color = options->GetWindow()->MakeFillColor(121, 125, 181);
  options->SetFontColor(3, color);
  color = options->GetWindow()->MakeFillColor(128, 128, 128);
  options->SetFontColor(4, color);
  options->GetWindow()->SetArea(option_area);
  static const u32 names[3] = {0x2a2, 0x2a3, 0x2a4};
  static const u32 choices[3] = {
      kUiMapAirPollutionOptionId, kUiMapWaterPollutionOptionId, kUiMapGarbagePollutionOptionId};
  for (u32 index = 0; index < 3; ++index) {
    {
      UiTextRef resource(names[index], kRsGameTextGroup);
      ScString text(resource.GetText());
      ScString first(text);
      ScString second(first);
      options->AddButton(choices[index], second, 0, nullptr, nullptr);
    }
    options->MoveButton(choices[index], 0, (i32)(14 * (index + 1)));
  }
  WnWindow *parent = window->GetWindow();
  [[maybe_unused]] bool attached = parent->ChildAdd(options->GetWindow());
  if (!attached) {
    UiGetWindowManager()->DestroyWindow(options->GetWindow());
    options->Release();
  }
  assert(attached);
  options->AutoSize();
  options->SetValue(kUiMapAirPollutionOptionId);
  options->Release();
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *air = view_owner->FindRaster(kUiAirPollutionMapRasterId);
    if (air && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiIntensityMapPaletteId);
      result = air->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(air);
    }
    GxRasterSource *water = view_owner->FindRaster(kUiWaterPollutionMapRasterId);
    if (water && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiIntensityMapPaletteId);
      result = water->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(water);
    }
    GxRasterSource *garbage = view_owner->FindRaster(kUiGarbagePollutionMapRasterId);
    if (garbage && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiIntensityMapPaletteId);
      result = garbage->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(garbage);
    }
  }
  return result;
}

// Create the aura view, legend and matching raster filters in the installed callback order.
// Palette requests run even when earlier view creation fails; missing borrowed rasters leave the current result unchanged.
u8 UiAuraMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiAuraMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 273);
  if (result)
    result = SetIconButton(0x5e5, 1);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiAuraMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiAuraMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x127, false, 0, 4);
    CreateLegendCaptionResource(area, 0x129, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiAuraMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiAuraMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(overlay);
    }
  }
  return result;
}

// Create the population view, legend and matching raster filters in the installed callback order.
// Palette requests run even when earlier view creation fails; missing borrowed rasters leave the current result unchanged.
u8 UiPopulationMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiPopulationMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 277);
  if (result)
    result = SetIconButton(0x5e5, 16);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiPopulationMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiPopulationMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x242, false, 0, 4);
    CreateLegendCaptionResource(area, 0x243, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiPopulationMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiPopulationMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(overlay);
    }
  }
  return result;
}

// Create the power view, legend and matching raster filters in the installed callback order.
// Palette requests run even when earlier view creation fails; missing borrowed rasters leave the current result unchanged.
u8 UiPowerMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiPowerMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 275);
  if (result)
    result = SetIconButton(0x5e5, 17);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiPowerMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiPowerMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x12a, false, 0, 4);
    CreateLegendCaptionResource(area, 0x23f, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiPowerMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiPowerMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(overlay);
    }
  }
  return result;
}

// Create the land-value view, legend and matching raster filters in the installed callback order.
// Palette requests run even when earlier view creation fails; missing borrowed rasters leave the current result unchanged.
u8 UiLandValueMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiLandValueMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 278);
  if (result)
    result = SetIconButton(0x5e5, 12);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiPopulationMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiPopulationMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x244, false, 0, 4);
    CreateLegendCaptionResource(area, 0x245, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiLandValueMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiPopulationMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(overlay);
    }
  }
  return result;
}

// Create the traffic view, legend and matching raster filters in the installed callback order.
// Palette requests run even when earlier view creation fails; missing borrowed rasters leave the current result unchanged.
u8 UiTrafficMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiTrafficMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 282);
  if (result)
    result = SetIconButton(0x5e5, 21);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiIntensityMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiIntensityMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x248, false, 0, 4);
    CreateLegendCaptionResource(area, 0x249, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiTrafficMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiIntensityMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(overlay);
    }
  }
  return result;
}

// Create the water-supply view, legend and matching raster filters in the installed callback order.
// Palette requests run even when earlier view creation fails; missing borrowed rasters leave the current result unchanged.
u8 UiWaterMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiWaterMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 283);
  if (result)
    result = SetIconButton(0x5e5, 23);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiWaterMapPaletteId);
  if (result)
    result = CreateLegendOutline(area, false);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiWaterMapPaletteId, false);
  if (result) {
    CreateLegendCaptionResource(area, 0x136, false, 0, 4);
    CreateLegendCaptionResource(area, 0x24a, false, 3, 4);
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *overlay = view_owner->FindRaster(kUiWaterSupplyMapRasterId);
    if (overlay && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiWaterMapPaletteId);
      result = overlay->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(overlay);
    }
  }
  return result;
}

UiAuraMapView::UiAuraMapView() {
}

UiAuraMapView::~UiAuraMapView() {
}

UiZoneMapView::UiZoneMapView() {
}

UiZoneMapView::~UiZoneMapView() {
}

UiCrimeMapView::UiCrimeMapView() {
}

UiCrimeMapView::~UiCrimeMapView() {
}

UiPopulationMapView::UiPopulationMapView() {
}

UiPopulationMapView::~UiPopulationMapView() {
}

UiPowerMapView::UiPowerMapView() {
}

UiPowerMapView::~UiPowerMapView() {
}

UiFlammabilityMapView::UiFlammabilityMapView() {
}

UiFlammabilityMapView::~UiFlammabilityMapView() {
}

UiLandValueMapView::UiLandValueMapView() {
}

UiLandValueMapView::~UiLandValueMapView() {
}

UiPollutionMapView::UiPollutionMapView() {
}

UiPollutionMapView::~UiPollutionMapView() {
}

UiTrafficMapView::UiTrafficMapView() {
}

UiTrafficMapView::~UiTrafficMapView() {
}

UiWaterMapView::UiWaterMapView() {
}

UiWaterMapView::~UiWaterMapView() {
}

// Construct the aura owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapAura() {
  return new UiAuraMapView;
}

// Construct the zoning owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapZone() {
  return new UiZoneMapView;
}

// Construct the crime owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapCrime() {
  return new UiCrimeMapView;
}

// Construct the population owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapPopulation() {
  return new UiPopulationMapView;
}

// Construct the power owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapPower() {
  return new UiPowerMapView;
}

// Construct the flammability owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapFlammability() {
  return new UiFlammabilityMapView;
}

// Construct the land-value owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapLandValue() {
  return new UiLandValueMapView;
}

// Construct the pollution owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapPollution() {
  return new UiPollutionMapView;
}

// Construct the traffic owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapTraffic() {
  return new UiTrafficMapView;
}

// Construct the water-supply owner and return its leading view interface without adding a reference.
UiMapsViewInterface *UiMapView::CreateMapWaterSupply() {
  return new UiWaterMapView;
}

// Route map-view button and option messages, forwarding unrelated messages to the shared view procedure.
// The overlay update dispatches through the real virtual operation; the original caller discards its register result.
bool UiMapView::DoWinMsg(WnWindow *receiver, u32 message, uptr first, uptr second, uptr third) {
  if (message != kWnMsg_Command)
    return UiMapsView::DoWinMsg(receiver, message, first, second, third);
  u32 kind = (u32)first, identifier = (u32)second;
  if (kind == 1) {
    if (identifier == kUiShowInCityButtonId) {
      UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
      SetShowInCity();
      UpdateView();
    }
    return UiMapsView::DoWinMsg(receiver, message, first, second, third);
  }
  if (kind != 3) {
    if (kind == 11 || kind == kWnMsg_Shutdown)
      return OnShutdown(receiver) != 0;
    return UiMapsView::DoWinMsg(receiver, message, first, second, third);
  }
  if (identifier == kUiPollutionMapOptionsId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    WnOptionControl *options = nullptr;
    if (receiver->GetChildAs(kUiPollutionMapOptionsId, IID_WnOptionControl, reinterpret_cast<void **>(&options))) {
      u32 selected = options->GetValue();
      GetMapControl()->HideFilter(kUiAirPollutionMapRasterId);
      GetMapControl()->HideFilter(kUiWaterPollutionMapRasterId);
      GetMapControl()->HideFilter(kUiGarbagePollutionMapRasterId);
      switch (selected) {
      case kUiMapAirPollutionOptionId:
        GetMapControl()->ShowFilter(kUiAirPollutionMapRasterId);
        break;
      case kUiMapWaterPollutionOptionId:
        GetMapControl()->ShowFilter(kUiWaterPollutionMapRasterId);
        break;
      case kUiMapGarbagePollutionOptionId:
        GetMapControl()->ShowFilter(kUiGarbagePollutionMapRasterId);
        break;
      }
      options->GetWindow()->InvalidateSelfAndParents();
      options->Release();
    }
  }
  return true;
}

// Create the zoning view with upper and lower legends and the terrain and zoning raster filters.
// Palette setup is attempted even when earlier view initialization failed, matching the installed call order.
u8 UiZoneMapView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  u32 identifier = kUiZoneMapViewId;
  u8 result = SetViewId(identifier);
  if (result)
    result = SetNameResource(kRsGameTextGroup, 284);
  if (result)
    result = SetIconButton(0x5e5, 25);
  if (result)
    result = UiMapView::CreateView(view_owner, area, location);
  CreatePalette(view_owner, kUiTerrainMapPaletteId);
  CreatePalette(view_owner, kUiZoningMapPaletteId);
  CreatePalette(view_owner, kUiZoningLowerLegendPaletteId);
  if (result)
    result = CreateLegendOutline(area, true);
  if (result)
    result = CreateLegendBitmap(view_owner, area, kUiZoningMapPaletteId, true);
  if (result) {
    CreateLegendCaptionResource(area, 0x139, true, 0, 3);
    CreateLegendCaptionResource(area, 0x13a, true, 1, 3);
    CreateLegendCaptionResource(area, 0x13b, true, 2, 3);
    result = CreateLegendOutline(area, false);
    if (result)
      result = CreateLegendBitmap(view_owner, area, kUiZoningLowerLegendPaletteId, false);
    if (result) {
      CreateLegendCaptionResource(area, 0x13c, false, 0, 4);
      CreateLegendCaptionResource(area, 0x2d5, false, 1, 4);
      CreateLegendCaptionResource(area, 0x2d6, false, 2, 4);
      CreateLegendCaptionResource(area, 0x2d7, false, 3, 4);
    }
  }
  if (!GetMapControl())
    result = 0;
  if (result) {
    GxRasterSource *terrain = view_owner->FindRaster(kUiTerrainMapRasterId);
    if (terrain) {
      GxPaletteView *palette = view_owner->GetPalette(kUiTerrainMapPaletteId);
      result = terrain->SetPalette(palette);
      if (result)
        result = GetMapControl()->AddFilter(terrain);
    }
    GxRasterSource *zones = view_owner->FindRaster(kUiZoningMapRasterId);
    if (zones && result) {
      GxPaletteView *palette = view_owner->GetPalette(kUiZoningMapPaletteId);
      result = zones->SetPalette(palette);
      if (result)
        return GetMapControl()->AddFilter(zones);
    }
  }
  return result;
}
