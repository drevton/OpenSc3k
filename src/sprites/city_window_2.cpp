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

bool SpCityWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpCityWindowView, SpCityWindowView);
  return WnSpriteRootWidget::QueryInterface(iid, out);
}

// Dispatch city-window notifications to the original view, lifecycle, resource and handler operations.
// Unrecognized messages are delegated to the window base.
bool SpCityWindow::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kGeMsg_QueryCommandStarting:
    unknown541 = true;
    return true;
  case kGeMsg_QueryCommandCompleted:
    unknown541 = false;
    return true;
  case kWnMsg_AcceleratorCommand:
    DefOnHotKey(u32(message->data1), u32(message->data2), u32(message->data3));
    return true;
  case kWnMsg_ShowCitySnapshotAlbum:
    if (message->data1)
      DefOnHotKey(114, u32(message->data1), 0);
    else
      DefOnHotKey(113, 0, 0);
    return true;
  case kWnMsg_ActivateApp:
    if (message->data1)
      return true;
    SetCostDisplay(false, 0, 0);
    SetMouseScroll(0, 0, false);
    SetEdgeScroll(false);
    if (cellCursorVisible)
      cellCursor->Clear(true);
    if (activeHandler)
      activeHandler->OnResetState();
    return true;
  case kWnMsg_ConstructionPromptOpened:
    unknown540 = true;
    SetCostDisplay(false, 0, 0);
    SetMouseScroll(0, 0, false);
    SetEdgeScroll(false);
    return true;
  case kWnMsg_FocusCityCameraAndZoom:
    FocusCameraOn(u32(message->data1), u32(message->data2), true);
    SetZoom(i32(u32(message->data3)), 0, true);
    return true;
  case kApMsgCityReady:
    return true;
  case kWnMsg_EnterTerrainEditor:
  case kWnMsg_EnterCity: {
    bool first = message->type == kWnMsg_EnterTerrainEditor;
    RsKey key(kRsAcceleratorResourceType, kRsArchitectAcceleratorGroup, first ? 2 : 1);
    WnAcceleratorResources *resources = nullptr;
    SpGetResourceManager()->GetResource(key, IID_WnAcceleratorResources, reinterpret_cast<void **>(&resources));
    if (resources) {
      WnAccelerator *newAccelerator = nullptr;
      if (SpGetCom()->GetClassObject(
              kWnKeyAcceleratorClassId, IID_WnAccelerator, reinterpret_cast<void **>(&newAccelerator))) {
        if (newAccelerator->Init(resources))
          SetKeyboardAccelerator(newAccelerator);
        newAccelerator->Release();
      }
      resources->Release();
    }
    inputReentrant = first;
    return true;
  }
  case kWnMsg_CityViewportChanged: {
    u32 first = u32(message->data1), second = u32(message->data2);
    viewportBounds = {i16(first >> 16), i16(first), i16(second >> 16), i16(second)};
    if (!isoView || !isoView->SetScreenClipRect(viewportBounds))
      return false;
    UpdateScrollRegions();
    return true;
  }
  case kWnMsg_ModalBegin:
    if (SpGetCity())
      SpGetCity()->SimHiddenPause();
    SetCostDisplay(false, 0, 0);
    SetMouseScroll(0, 0, false);
    SetEdgeScroll(false);
    if (cellCursorVisible)
      cellCursor->Clear(true);
    if (activeHandler)
      activeHandler->OnResetState();
    return true;
  case kWnMsg_ModalEnd:
    if (SpGetCity())
      SpGetCity()->SimHiddenUnpause();
    if (handlerMode == kSpHandlerLocation || handlerMode == kSpHandlerDemolition)
      SpDeferredHandlerReset = true;
    return true;
  case kWnMsg_InitCityViewLayers:
    InitWinLayers(reinterpret_cast<UiLayerDialogInterface *>(message->data1));
    return true;
  case kWnMsg_FocusCityCamera:
    FocusCameraOn(u32(message->data1), u32(message->data2), message->data3 != 0);
    return true;
  case kWnMsg_ConstructionPromptClosed:
    unknown540 = false;
    return true;
  case kWnMsg_ProcessDeferredCityHandlerReset:
    if (SpDeferredHandlerReset) {
      SwitchHandler(kSpHandlerRestore, nullptr);
      SpDeferredHandlerReset = false;
    }
    return true;
  case kWnMsg_SetCityCameraZoomRotation:
    isoView->SetZoomAndRotation(i32(u32(message->data1)), i32(u32(message->data2)), message->data3 != 0);
    return true;
  case kWnMsg_UpdateCityViewLayers:
    UpdateWinLayers(reinterpret_cast<UiLayerDialogInterface *>(message->data1));
    return true;
  case kApMsgPreCityChange:
    PreCityChange();
    return true;
  case kApMsgPostCityChange:
    PostCityChange();
    return true;
  case kWnMsg_SetCityViewParams:
    SetViewParam(u32(message->data1), u32(message->data2), message->data3 != 0);
    return true;
  case kCtMsg_MonthChanged:
    UpdateDataView();
    return true;
  default:
    return WnSpriteRootWidget::DoMessage(message);
  }
}

// Paint scrolling or the active view, release a hidden pause, and draw the optional cursor coordinate overlay.
bool SpCityWindow::GZPaint() {
  if (mouseScrolling || edgeScrollFlags[3] || edgeScrollFlags[2] || edgeScrollFlags[1] || edgeScrollFlags[0]) {
    PaintScroll();
    return true;
  }
  f32 frequency = 60.0f;
  currentView->SetAnimFrequency(frequency);
  currentView->Plot();
  CtCity *city = nullptr;
  if (simulationPausedByView) {
    city = SpGetCity();
    simulationPausedByView = false;
    city->SimHiddenUnpause();
  }
  if (showCellCoordinates && font) {
    i32 x, y;
    windowManager->GetCursorRelativePosition(static_cast<WnWindow *>(this), &x, &y);
    SpCellMap *map = isoView->GetSpriteCellMap();
    if (map) {
      if (!city)
        city = SpGetCity();
      SpCityLockGuard guard(city->GetGraphicsCriticalSection());
      i32 pixelX, pixelY, microX, microZ;
      map->SpCellMap::WindowPixelToCityPixel_NonInline(x, y, &pixelX, &pixelY);
      if (map->CityPixelToMicroDrawGrid(pixelX, pixelY, &microX, &microZ, false)) {
        i32 actualX, actualZ;
        map->DrawGridToActualGrid(microX / 256, microZ / 256, &actualX, &actualZ);
        if (actualX >= 0 && actualZ >= 0) {
          i32 width = i32(application->GetCity()->CellCountX());
          if (actualX < width && actualZ < width) {
            char text[256];
            int length = std::snprintf(text, sizeof(text), "%u, %u", u32(actualX) + 1, u32(actualZ) + 1);
            font->DrawTextA(
                draw_buffer, i32(u32(x) + 1), i32(u32(y) + 33), text, u32(length), kSpCoordinateTextShadowColor);
            font->DrawTextA(draw_buffer, x, i32(u32(y) + 32), text, u32(length), kSpCoordinateTextColor);
          }
        }
      }
    }
  }
  return true;
}

// At, move the view for mouse or edge scrolling and draw the original anchor image.
void SpCityWindow::PaintScroll() {
  bool pause = false;
  if (mouseScrolling) {
    Translate(mouseScrollDeltaX, mouseScrollDeltaY, true);
    pause = true;
  } else {
    bool any = edgeScrollFlags[0] || edgeScrollFlags[1] || edgeScrollFlags[3] || edgeScrollFlags[2];
    if (!any && !edgeScrollEnabled) {
      SetEdgeScroll(false);
    } else {
      if (!any) {
        edgeScrollFlags[0] = keyboardState->IsKeyDownNow(kWnKeyUp);
        edgeScrollFlags[1] = keyboardState->IsKeyDownNow(kWnKeyDown);
        edgeScrollFlags[3] = keyboardState->IsKeyDownNow(kWnKeyLeft);
        edgeScrollFlags[2] = keyboardState->IsKeyDownNow(kWnKeyRight);
      }
      SetEdgeScroll(true);
      SetMouseScroll(0, 0, false);
    }
    if (edgeScrollEnabled &&
        (edgeScrollFlags[0] || edgeScrollFlags[3] || edgeScrollFlags[1] || edgeScrollFlags[2])) {
      u32 count = u32(edgeScrollFlags[0]) + u32(edgeScrollFlags[1]) + u32(edgeScrollFlags[3]) +
          u32(edgeScrollFlags[2]);
      if (count == 1) {
        if (edgeScrollFlags[0])
          Translate(0.0f, -scrollRate, true);
        else if (edgeScrollFlags[1])
          Translate(0.0f, scrollRate, true);
        else if (edgeScrollFlags[2])
          Translate(scrollRate, 0.0f, true);
        else if (edgeScrollFlags[3])
          Translate(-scrollRate, 0.0f, true);
        pause = true;
      } else if (count == 2) {
        if (edgeScrollFlags[0] && edgeScrollFlags[3])
          Translate(-scrollRate, -scrollRate, true);
        else if (edgeScrollFlags[1] && edgeScrollFlags[3])
          Translate(-scrollRate, scrollRate, true);
        else if (edgeScrollFlags[0] && edgeScrollFlags[2])
          Translate(scrollRate, -scrollRate, true);
        else if (edgeScrollFlags[1] && edgeScrollFlags[2])
          Translate(scrollRate, scrollRate, true);
        pause = true;
      }
    }
  }
  if (pause) {
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
  }
  DoSetBoxXPosition();
  currentView->Plot();
  if (mouseScrolling) {
    ScRect anchorArea{mouseScrollX - 16, mouseScrollY - 16, mouseScrollX + 16, mouseScrollY + 16};
    if (draw_buffer->Lock(GxLockFlags::Blt)) {
      draw_buffer->Blt(anchorCursorImage, *anchorCursorImage->GetBufferArea(), anchorArea, nullptr);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  }
}

u32 SpCityWindow::AddRef() {
  return WnSpriteRootWidget::AddRef();
}

u32 SpCityWindow::Release() {
  return WnSpriteRootWidget::Release();
}

// Borrow the secondary window identity belonging to this city-view object.
WnWindow *SpCityWindow::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

bool SpCityWindow::CellCursorVisible() {
  return cellCursorVisible;
}

void SpCityWindow::SetCellCursorSize(u32 width, u32 height) {
  cursorWidth = width;
  cursorHeight = height;
}

void SpCityWindow::SetCellCursorVisible(bool visible) {
  cellCursorVisible = visible;
  if (!visible && cellCursor)
    cellCursor->Clear(true);
}

// Borrow the current cell-cursor interface without adding a reference.
UiCityCursor *SpCityWindow::GetCellCursorMap() const {
  return cellCursor;
}

// Return the manager interface provided by the retained isometric view.
SpManagerView *SpCityWindow::GetViewManager() {
  return isoView->GetViewManager();
}

// Borrow the selected view without changing its reference count.
SpViewInterface *SpCityWindow::GetCurrentView() {
  return currentView;
}

// Borrow the retained isometric view through its public interface.
SpIsoView *SpCityWindow::GetIsoView() {
  return isoView;
}

bool SpCityWindow::SetCurrentView(SpViewInterface *view) {
  currentView = view;
  return true;
}

void SpCityWindow::PreCityChange() {
  inputReentrant = true;
  ScMessageServer *server = SpGetMessageServer();
  ScMessage message(kWnMsg_CityViewClosing, kSpCityViewLifecycleTag, 0, 0);
  server->MessageSend(message);
  if (isoView)
    isoView->Shutdown();
  if (activeHandler)
    SetActiveHandler(nullptr);
  if (cellCursor)
    cellCursor->Shutdown();
  message.type = kWnMsg_CityViewClosed;
  server->MessageSend(message);
}

// At, initialize the cursor and isometric view for the application's new city.
void SpCityWindow::PostCityChange() {
  ScMessageServer *server = SpGetMessageServer();
  CtCity *city = application->GetCity();
  if (!city)
    return;
  unknown200[0] = u32(i32(city->SizeX()));
  unknown200[1] = u32(i32(city->SizeY()));
  unknown200[2] = u32(i32(city->SizeZ()));
  unknown200[3] = u32(city->GetTileSize());
  if (!cellCursor->Init(city)) {
    Shutdown();
    return;
  }
  ScMessage message(kWnMsg_CityViewOpening, kSpCityViewLifecycleTag, 0, 0);
  server->MessageSend(message);
  if (!isoView->Init(application, city)) {
    isoView->Release();
    isoView = nullptr;
    Shutdown();
    return;
  }
  viewportBounds.left = 0;
  viewportBounds.top = 0;
  viewportBounds.right = i32(u32(screenBufferBounds.right) - u32(screenBufferBounds.left));
  viewportBounds.bottom = i32(u32(screenBufferBounds.bottom) - u32(screenBufferBounds.top));
  UpdateScrollRegions();
  message.type = kWnMsg_CityViewReady;
  server->MessagePost(message, false);
  SetActiveCursor(kSpCenterCursorId);
  inputReentrant = false;
}

void SpCityWindow::GetCameraHint(ScRect *first, ScRect *second) {
  currentView->GetCameraHint(first, second);
}

// Acquire window services and cursor resources, subscribe to city messages, then create the cursor and isometric view.
bool SpCityWindow::Init() {
  if (GetInitFlag())
    return true;
  GxGraphics *graphics = SpGetGraphics();
  WnResources *resources = SpGetWindowResources();
  ApGameInterface *app = SpGetApplication();
  boxX = app ? app->GetBoxX() : nullptr;
  framework = SpGetFramework();
  windowManager = SpWindowManager();
  viewportBuffer = graphics->ScreenBuffer()->ActiveDrawBuffer();
  screenBufferBounds = *viewportBuffer->GetBufferArea();
  viewportBounds = screenBufferBounds;
  WnCursorManager *cursors = windowManager->GetCursorManager();
  ScString dataDirectory(SpGetFileSystem()->DataDirectory());
  ScString cursorDirectory(ScString(dataDirectory).Append("res\\ui\\shared\\cursors\\"));
  for (const SpCityCursorResource &resource : kSpCityCursorResources) {
    WnFileCursor *fileCursor = new WnFileCursor(ScString(ScString(cursorDirectory).Append(resource.filename)));
    cursors->AddCursor(fileCursor, resource.id);
  }
  if (!font && resources)
    font = ScAddRefNotNull(resources->GetFont(kWnFontDefault));
  if (!anchorCursorImage) {
    ScString path;
    RsFileSystem *filesystem = SpGetFileSystem();
    if (filesystem) {
      const char *directory = filesystem->DataDirectory();
      if (directory) {
        ScString value(directory);
        path = value;
      }
    }
    path.Append("Res\\");
    {
      ScString subdirectory("UI\\Shared\\cursors\\");
      path.Append(subdirectory.Data(), subdirectory.Strlen());
    }
    {
      ScString filename("anchor point cursor.BMP");
      path.Append(filename.Data(), filename.Strlen());
    }
    GxColorType color(static_cast<GxColorType::Color>(4));
    SpGetApplication()->GetBufferResourceFactory()->LoadFile(&path, &anchorCursorImage, 0, &color, 0);
    u16 transparent = anchorCursorImage->ConvertRGBValueToNative16Bit(255, 0, 255);
    anchorCursorImage->SetTransparency(transparent);
  }
  ScMessageServer *server = SpGetMessageServer();
  ScMessageTarget *target = static_cast<WnWindow *>(this);
  for (u32 id : kSpCityWindowMessages)
    server->AddNotification(target, id);
  SetFocus();
  WnWindow *mainWindow = GetMainWindow();
  SetArea(*mainWindow->GetArea());
  UpdateScrollRegions();
  ResetScrollRates();
  costHint = new WnHint();
  costColors[0] = kSpAffordableCostColor;
  costColors[1] = kSpInsufficientFundsCostColor;
  keyboardState = windowManager->GetKeyboard();
  WnSpriteRootWidget::Init();
  cellCursor = ScAddRefNotNull(new SpCityCursor());
  isoView = ScAddRefNotNull(new SpView());
  currentView = isoView;
  return true;
}

// Remove subscriptions and owned objects, persist the scroll preference and retire registered cursors before shutdown.
bool SpCityWindow::Shutdown() {
  if (!GetInitFlag())
    return true;
  ScMessageServer *server = SpGetMessageServer();
  ScMessageTarget *target = static_cast<WnWindow *>(this);
  for (u32 id : kSpCityWindowMessages)
    server->RemoveNotification(target, id);
  ScRelease(&font);
  ScMessage message(kWnMsg_CityViewClosing, kSpCityViewLifecycleTag, 0, 0);
  server->MessageSend(message);
  if (isoView) {
    isoView->Shutdown();
    isoView->Release();
    isoView = nullptr;
  }
  currentView = nullptr;
  ScRelease(&activeHandler);
  ScRelease(&previousHandler);
  if (cellCursor) {
    cellCursor->Shutdown();
    cellCursor->Release();
    cellCursor = nullptr;
  }
  ScRelease(&anchorCursorImage);
  message.type = kWnMsg_CityViewClosed;
  message.data1 = kSpCityViewLifecycleTag;
  server->MessageSend(message);
  viewportBuffer = nullptr;
  delete costHint;
  costHint = nullptr;
  SaveScrollMargin();
  WnCursorManager *cursors = windowManager->GetCursorManager();
  for (const SpCityCursorResource &resource : kSpCityCursorResources)
    cursors->RemoveCursor(resource.id);
  return WnSpriteRootWidget::Shutdown();
}

// At, report the focused cell when the active view is the original isometric view.
void SpCityWindow::DoSetBoxXPosition() {
  if (currentView && currentView == static_cast<SpViewInterface *>(isoView)) {
    u32 x = 0, z = 0;
    isoView->GetFocusCell(&x, &z);
    boxX->Event(1021, x, 0, z, 0);
  }
}

// At, report the isometric view's rotation using the original reversed rotation mapping.
void SpCityWindow::DoSetBoxXRotation() {
  if (currentView && currentView == static_cast<SpViewInterface *>(isoView)) {
    i32 zoom = 0, rotation = 0;
    isoView->GetZoomAndRotation(&zoom, &rotation);
    u32 value = rotation >= 1 && rotation <= 3 ? u32(4 - rotation) : 0;
    boxX->Event(1022, value, 0, 0, 0);
  }
}

// At, report the zoom level when the active view is the isometric view.
void SpCityWindow::DoSetBoxXZoom() {
  if (currentView && currentView == static_cast<SpViewInterface *>(isoView)) {
    i32 zoom = 0, rotation = 0;
    isoView->GetZoomAndRotation(&zoom, &rotation);
    boxX->Event(1023, u32(zoom), 0, 0, 0);
  }
}

void SpCityWindow::ResetScrollRates() {
  scrollRates[4] = 32.0f;
  scrollRates[3] = 32.0f;
  scrollRates[2] = 32.0f;
  scrollRates[1] = 32.0f;
  scrollRates[0] = 32.0f;
  scrollRate = 32.0f;
}

void SpCityWindow::SaveScrollMargin() {
  ApIniFile ini;
  ScString path, filename;
  {
    ScString directory(SpGetFileSystem()->AppDirectory());
    path = directory;
  }
  application->GetAppIniFileName(&filename);
  path.Append(filename.Data(), filename.Strlen());
  ini.SetPath(path);
  path.Sprintf("%f", 5.0 / f64(GetH()));
  ScString section("Navigation"), key("ScrollMarginFactor");
  ini.WriteEntry(section, key, path);
}

// Initialize the layers dialog from the isometric view's flags and current view selection.
void SpCityWindow::InitWinLayers(UiLayerDialogInterface *dialog) {
  if (!dialog)
    return;
  u32 view = u32(isoView->GetCityViewID());
  u32 viewFlags = isoView->GetCityViewFlags();
  dialog->SetViewItem(kUiLayerTransportCheckId, (viewFlags & kSpCityViewTransport) != 0);
  dialog->SetViewItem(kUiLayerPowerLinesCheckId, (viewFlags & kSpCityViewPowerLines) != 0);
  dialog->SetViewItem(kUiLayerFloraCheckId, (viewFlags & kSpCityViewFlora) != 0);
  dialog->SetViewItem(kUiLayerZonedBuildingsCheckId, (viewFlags & kSpCityViewZonedBuildings) != 0);
  dialog->SetViewItem(kUiLayerOtherBuildingsCheckId, (viewFlags & kSpCityViewOtherBuildings) != 0);
  dialog->SetViewItem(kUiLayerZonesCheckId, (viewFlags & kSpCityViewZones) != 0);
  dialog->SetViewOption(kUiUndergroundLayerSectionId, kUiLayerWaterPipesOptionId);
  dialog->SetViewOption(kUiDataLayerSectionId, kUiLayerAuraOptionId);
  if (view == kSpCityViewSurface) {
    dialog->SetView(kUiAboveGroundLayerSectionId);
  } else if (view == kSpCityViewSubway || view == kSpCityViewWaterPipes) {
    dialog->SetView(kUiUndergroundLayerSectionId);
    dialog->SetViewOption(
        kUiUndergroundLayerSectionId, view == kSpCityViewSubway ? kUiLayerSubwayOptionId : kUiLayerWaterPipesOptionId);
  } else if (view >= kSpCityViewAura && view <= kSpCityViewDensity) {
    u32 option = view == kSpCityViewDensity ? kUiLayerDensityOptionId
        : view == kSpCityViewAura           ? kUiLayerAuraOptionId
        : view == kSpCityViewCrime          ? kUiLayerCrimeOptionId
                                            : view + kSpDataLayerOptionOffset;
    dialog->SetView(kUiDataLayerSectionId);
    dialog->SetViewOption(kUiDataLayerSectionId, option);
  }
}

// Translate the layer-dialog selection into a deferred view-change message while the city is hidden-paused.
void SpCityWindow::UpdateWinLayers(UiLayerDialogInterface *dialog) {
  if (!dialog)
    return;
  CtCity *city = SpGetCity();
  ScMessageServer *server = SpGetMessageServer();
  city->SimHiddenPause();
  u32 view = dialog->GetView(), viewFlags = isoView->GetCityViewFlags();
  if (view == kUiAboveGroundLayerSectionId) {
    if (dialog->GetViewItem(kUiLayerTransportCheckId))
      viewFlags |= kSpCityViewTransport;
    else
      viewFlags &= ~kSpCityViewTransport;
    if (dialog->GetViewItem(kUiLayerZonesCheckId))
      viewFlags |= kSpCityViewZones;
    else
      viewFlags &= ~kSpCityViewZones;
    if (dialog->GetViewItem(kUiLayerPowerLinesCheckId))
      viewFlags |= kSpCityViewPowerLines;
    else
      viewFlags &= ~kSpCityViewPowerLines;
    if (dialog->GetViewItem(kUiLayerFloraCheckId))
      viewFlags |= kSpCityViewFlora;
    else
      viewFlags &= ~kSpCityViewFlora;
    if (dialog->GetViewItem(kUiLayerZonedBuildingsCheckId))
      viewFlags |= kSpCityViewZonedBuildings;
    else
      viewFlags &= ~kSpCityViewZonedBuildings;
    if (dialog->GetViewItem(kUiLayerOtherBuildingsCheckId))
      viewFlags |= kSpCityViewOtherBuildings;
    else
      viewFlags &= ~kSpCityViewOtherBuildings;
    ScMessage message(kWnMsg_SetCityViewParams, 0, viewFlags, 1);
    server->MessagePost(message, false);
  } else if (view == kUiUndergroundLayerSectionId) {
    u32 option = dialog->GetViewOption(kUiUndergroundLayerSectionId);
    if (option == kUiLayerSubwayOptionId || option == kUiLayerWaterPipesOptionId) {
      ScMessage message(kWnMsg_SetCityViewParams, option - kSpUndergroundLayerOptionOffset, viewFlags, 1);
      server->MessagePost(message, false);
    }
  } else if (view == kUiDataLayerSectionId) {
    u32 option = dialog->GetViewOption(kUiDataLayerSectionId), viewId = kSpCityViewSurface;
    if (option == kUiLayerAuraOptionId || option == kUiLayerCrimeOptionId)
      viewId = option - kSpLeadingDataLayerOptionOffset;
    else if (option == kUiLayerDensityOptionId)
      viewId = kSpCityViewDensity;
    else if (option >= kUiLayerPowerOptionId && option <= kUiLayerTrafficOptionId)
      viewId = option - kSpDataLayerOptionOffset;
    if (viewId) {
      ScMessage message(kWnMsg_SetCityViewParams, viewId, viewFlags, 1);
      server->MessagePost(message, false);
    }
  }
  city->SimHiddenUnpause();
}

void SpCityWindow::Translate(f32 x, f32 z, bool update) {
  currentView->Translate(x, z, update);
  DoSetBoxXPosition();
}

// Move the camera while holding the automatic cursor and hidden simulation pause, then refresh the active handler.
void SpCityWindow::FocusCameraOn(u32 x, u32 z, bool update) {
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
    currentView->FocusCameraOn(x, z, update);
    DoSetBoxXPosition();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      SpGetCity()->SimHiddenUnpause();
    }
    if (activeHandler)
      activeHandler->OnResetState();
    i32 mouseX = 0, mouseY = 0;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
}

// Rotate clockwise while pausing the view and city, then reset the handler, cursor and pending input.
void SpCityWindow::RotateCameraCW(bool update) {
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    boxX->Event(kAuEventPlayEffect, kAuEffectUiRotate, 0, 0, 0);
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
    if (cellCursorVisible)
      cellCursor->Clear(true);
    currentView->RotateCameraCW(update);
    DoSetBoxXRotation();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      SpGetCity()->SimHiddenUnpause();
    }
    if (activeHandler)
      activeHandler->OnResetState();
    i32 mouseX = 0, mouseY = 0;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
  owner->FlushInputMessageQueue();
}

// Rotate counterclockwise while pausing the view and city, then reset the handler, cursor and pending input.
void SpCityWindow::RotateCameraCCW(bool update) {
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    boxX->Event(kAuEventPlayEffect, kAuEffectUiRotate, 0, 0, 0);
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
    if (cellCursorVisible)
      cellCursor->Clear(true);
    currentView->RotateCameraCCW(update);
    DoSetBoxXRotation();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      SpGetCity()->SimHiddenUnpause();
    }
    if (activeHandler)
      activeHandler->OnResetState();
    i32 mouseX = 0, mouseY = 0;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
  owner->FlushInputMessageQueue();
}

// At, apply an explicit zoom and optional rotation while temporarily pausing the view.
void SpCityWindow::SetZoom(i32 zoom, i32 rotation, bool update) {
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    i32 currentZoom, currentRotation;
    currentView->GetZoomAndRotation(&currentZoom, &currentRotation);
    if (currentZoom != zoom)
      boxX->Event(kAuEventPlayEffect, currentZoom <= zoom ? 1 : 2, 0, 0, 0);
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
    if (cellCursorVisible)
      cellCursor->Clear(true);
    currentView->SetZoomAndRotation(zoom, rotation == -1 ? currentRotation : rotation, update);
    u32 index = zoom == 0 ? 4 : zoom == 1 ? 3 : zoom == 2 ? 2 : zoom == 4 ? 0 : 1;
    scrollRate = scrollRates[index];
    DoSetBoxXZoom();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      SpGetCity()->SimHiddenUnpause();
    }
    if (activeHandler)
      activeHandler->OnResetState();
    i32 mouseX, mouseY;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
  owner->FlushInputMessageQueue();
}

// At, change the isometric view and flags under the city's lock and hidden pause.
void SpCityWindow::SetViewParam(u32 key, u32 value, bool update) {
  CtCity *city = SpGetCity();
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      city->SimHiddenPause();
    }
    city->Lock();
    isoView->SetCityViewID(i32(key), false);
    u32 viewFlags = value == UINT32_MAX ? isoView->GetCityViewFlags() | 0xfeu : value;
    isoView->SetCityViewFlags(viewFlags, update);
    city->Unlock();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      city->SimHiddenUnpause();
    }
    if (activeHandler) {
      activeHandler->OnResetState();
      if (!activeHandler->IsValidHandlerByView(i32(key), value))
        SetActiveHandler(nullptr);
    }
    i32 mouseX, mouseY;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
}

// At, refresh an active data view while the city is locked and the animation is paused.
void SpCityWindow::UpdateDataView() {
  if (u32(isoView->GetCityViewID()) < 2)
    return;
  CtCity *city = SpGetCity();
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      city->SimHiddenPause();
    }
    city->Lock();
    ScMessage message(kWnMsg_RefreshCityDataView, 0, 0, 0);
    SpGetMessageServer()->MessageSend(message);
    city->Unlock();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      city->SimHiddenUnpause();
    }
    i32 mouseX, mouseY;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
}

// Zoom in while pausing the view and city, then update scroll speed and restore the handler and input state.
void SpCityWindow::ZoomIn(bool update) {
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    boxX->Event(kAuEventPlayEffect, kAuEffectUiZoomIn, 0, 0, 0);
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
    if (cellCursorVisible)
      cellCursor->Clear(true);
    currentView->ZoomIn(update);
    i32 zoom = 0, rotation = 0;
    currentView->GetZoomAndRotation(&zoom, &rotation);
    u32 index = zoom == 0 ? 4 : zoom == 1 ? 3 : zoom == 2 ? 2 : zoom == 4 ? 0 : 1;
    scrollRate = scrollRates[index];
    DoSetBoxXZoom();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      SpGetCity()->SimHiddenUnpause();
    }
    if (activeHandler)
      activeHandler->OnResetState();
    i32 mouseX = 0, mouseY = 0;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
  owner->FlushInputMessageQueue();
}

// Zoom out while pausing the view and city, then update scroll speed and restore the handler and input state.
void SpCityWindow::ZoomOut(bool update) {
  {
    SpAutoCursor automaticCursor(14);
    activeCursor = 14;
    boxX->Event(kAuEventPlayEffect, kAuEffectUiZoomOut, 0, 0, 0);
    f32 frequency = 0.0f;
    currentView->SetAnimFrequency(frequency);
    if (!simulationPausedByView) {
      simulationPausedByView = true;
      SpGetCity()->SimHiddenPause();
    }
    if (cellCursorVisible)
      cellCursor->Clear(true);
    currentView->ZoomOut(update);
    i32 zoom = 0, rotation = 0;
    currentView->GetZoomAndRotation(&zoom, &rotation);
    u32 index = zoom == 0 ? 4 : zoom == 1 ? 3 : zoom == 2 ? 2 : zoom == 4 ? 0 : 1;
    scrollRate = scrollRates[index];
    DoSetBoxXZoom();
    frequency = 60.0f;
    currentView->SetAnimFrequency(frequency);
    if (simulationPausedByView) {
      simulationPausedByView = false;
      SpGetCity()->SimHiddenUnpause();
    }
    if (activeHandler)
      activeHandler->OnResetState();
    i32 mouseX = 0, mouseY = 0;
    windowManager->GetCursorScreenPosition(&mouseX, &mouseY);
    OnMouseMove(mouseX, mouseY, 0);
  }
  u32 cursorId = kSpCenterCursorId;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
  owner->FlushInputMessageQueue();
}

// Read the saved view selector and close its archive before initializing a selected isometric view.
// The original scratch starts with the temporary key's address; failed reads retain their reached writes.
bool SpCityWindow::Init(CtCity *city, RsDatabaseSegment *segment) {
  RsKey key{kSpCityViewRecordType, kSpCityViewRecordGroup, 0};
  bool result;
  u32 selector = u32(reinterpret_cast<uptr>(&key));
  {
    ScArchiveFrame frame(city, segment, key);
    result = frame.valid != 0;
    RsStream *stream = frame.GetStream();
    if (result && frame.flag) {
      ScString extension;
      result = stream->ReadString(&extension);
    }
    if (result)
      result = stream->ReadU32(&selector);
    if (result && frame.flag) {
      ScString extension;
      result = stream->ReadString(&extension);
    }
    if (!result)
      frame.Fail();
  }
  if (!selector)
    return isoView->Init(city, segment);
  return result;
}

// Save the selected-view flag, close its archive frame and save the isometric view when it is selected.
bool SpCityWindow::Save(CtCity *city, RsDatabaseSegment *segment) {
  bool result;
  {
    ScArchiveFrame frame(city, segment, {kSpCityViewRecordType, kSpCityViewRecordGroup, 0}, 2, 0);
    result = frame.valid != 0;
    RsStream *stream = frame.GetStream();
    if (!result || !(result = stream->WriteU32(currentView != isoView)))
      frame.Fail();
  }
  if (currentView == isoView)
    isoView->Save(city, segment);
  return result;
}

bool SpCityWindow::SetActiveHandler(SpViewHandler *handler) {
  return SwitchHandler(handler ? kSpHandlerTool : kSpHandlerDefault, handler);
}

// Switch or restore the active tool under the city lock, retaining the original handler and command ownership order.
bool SpCityWindow::SwitchHandler(i32 mode, SpViewHandler *handler) {
  CtCity *city = SpGetCity();
  if (!city)
    return false;
  SpCityLockGuard guard(city->GetCriticalSection());
  bool result = false;
  if (handler || handlerMode != mode) {
    if (mode == kSpHandlerLocation || mode == kSpHandlerDemolition) {
      if (previousHandler) {
        previousHandler->Shutdown();
        previousHandler->Release();
        previousHandler = nullptr;
      }
      previousHandler = activeHandler;
      previousHandlerMode = handlerMode;
      activeHandler = nullptr;
      handlerMode = kSpHandlerUnset;
      SpCityCommandFactoryRef factory;
      UiCommandInterface *command = nullptr;
      u32 commandId = handler ? 0 : mode == kSpHandlerLocation ? kGeLocationToolClassId : kGeDemolitionToolClassId;
      factory.factory->GetCommand(commandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command));
      command->Execute();
      command->Release();
      handlerMode = mode;
    } else if (mode == kSpHandlerTool) {
      if (activeHandler) {
        activeHandler->Shutdown();
        activeHandler->Release();
        activeHandler = nullptr;
      }
      handlerMode = kSpHandlerUnset;
      handler->AddRef();
      handler->Init();
      u32 cursorId = u32(mode);
      handler->GetCursor(&cursorId);
      handler->OnMouseMove(lastMouseX, lastMouseY, 0);
      activeHandler = handler;
      handlerMode = kSpHandlerTool;
      if (activeCursor != cursorId)
        SetActiveCursor(cursorId);
    } else if (mode == kSpHandlerRestore &&
        (handlerMode == kSpHandlerLocation || handlerMode == kSpHandlerDemolition)) {
      if (activeHandler) {
        activeHandler->Shutdown();
        activeHandler->Release();
        activeHandler = nullptr;
      }
      handlerMode = kSpHandlerUnset;
      if (previousHandler) {
        activeHandler = previousHandler;
        handlerMode = previousHandlerMode;
        activeHandler->Init();
        u32 cursorId = u32(mode);
        activeHandler->GetCursor(&cursorId);
        activeHandler->OnMouseMove(lastMouseX, lastMouseY, 0);
        if (activeCursor != cursorId)
          SetActiveCursor(cursorId);
        previousHandlerMode = kSpHandlerUnset;
        previousHandler = nullptr;
      } else {
        handlerMode = kSpHandlerDefault;
        if (activeCursor != kSpCenterCursorId)
          SetActiveCursor(kSpCenterCursorId);
      }
    } else if (!mode) {
      if (activeHandler) {
        activeHandler->Shutdown();
        activeHandler->Release();
        activeHandler = nullptr;
      }
      handlerMode = kSpHandlerUnset;
      if (previousHandler) {
        previousHandler->Shutdown();
        previousHandler->Release();
        previousHandler = nullptr;
      }
      previousHandlerMode = kSpHandlerUnset;
      handlerMode = kSpHandlerDefault;
      if (activeCursor != kSpCenterCursorId)
        SetActiveCursor(kSpCenterCursorId);
    }
    result = true;
  }
  return result;
}

// Resolve the default cursor identifier, and update the window cursor only when the selected identifier changes.
void SpCityWindow::SetActiveCursor(u32 id) {
  if (id == kSpCenterCursorId)
    defaultCursor = id;
  else if (!id)
    id = defaultCursor;
  if (activeCursor != id) {
    activeCursor = id;
    WnCursor *selectedCursor = GetCursorManager()->GetIGZCursor(id);
    if (selectedCursor)
      SetCursor(selectedCursor, true);
  }
}

void SpCityWindow::AnimationPause(bool pause) {
  SpGetTickManager()->PauseAnimationSystem(pause);
}

bool SpCityWindow::DefActiveCursor(u32 *id) {
  *id = defaultCursor;
  return true;
}

// Refresh arrow-key scroll flags and cancel mouse scrolling, cost display and cell cursor when any arrow is down.
bool SpCityWindow::DefOnKeyDown(u32 key, u32) {
  bool handled = false;
  if (key == kWnKeyUp || key == kWnKeyDown || key == kWnKeyLeft || key == kWnKeyRight) {
    edgeScrollFlags[0] = keyboardState->IsKeyDownNow(kWnKeyUp);
    edgeScrollFlags[1] = keyboardState->IsKeyDownNow(kWnKeyDown);
    edgeScrollFlags[3] = keyboardState->IsKeyDownNow(kWnKeyLeft);
    edgeScrollFlags[2] = keyboardState->IsKeyDownNow(kWnKeyRight);
    handled = true;
  }
  if (edgeScrollFlags[0] || edgeScrollFlags[1] || edgeScrollFlags[3] || edgeScrollFlags[2]) {
    SetEdgeScroll(true);
    SetMouseScroll(0, 0, false);
    SetCostDisplay(false, 0, 0);
    SetCellCursorVisible(false);
  }
  return handled;
}

// Refresh released arrow keys and handle the original screenshot and system-information keyboard shortcuts.
bool SpCityWindow::DefOnKeyUp(u32 key, u32 modifiers) {
  bool handled = false;
  if (key >= kWnKeyLeft && key <= kWnKeyDown) {
    edgeScrollFlags[0] = keyboardState->IsKeyDownNow(kWnKeyUp);
    edgeScrollFlags[1] = keyboardState->IsKeyDownNow(kWnKeyDown);
    edgeScrollFlags[3] = keyboardState->IsKeyDownNow(kWnKeyLeft);
    edgeScrollFlags[2] = keyboardState->IsKeyDownNow(kWnKeyRight);
    handled = true;
  } else if (key == kWnKeyC && !(modifiers & kWnModifierControl) && !(modifiers & kWnModifierAlt) &&
      (modifiers & kWnModifierShift)) {
    DefOnHotKey(115, 0, 0);
    handled = true;
  } else if (key == kWnKeyV && (modifiers & kWnModifierControl) && !(modifiers & kWnModifierShift)) {
    ApSysInfo *info = nullptr;
    WnWindow *window = nullptr;
    SpGetCom()->GetClassObject(kApSysInfoWindowClassId, IID_ApSysInfoWindow, reinterpret_cast<void **>(&info));
    if (info) {
      if (info->CreateSysInfoWindow(&window)) {
        windowManager->DoModalWin(nullptr, window);
        window->Shutdown();
        window->Release();
        handled = true;
      }
      info->Release();
    }
  }
  if (edgeScrollFlags[3] || edgeScrollFlags[2] || edgeScrollFlags[0] || edgeScrollFlags[1]) {
    SetEdgeScroll(true);
    SetMouseScroll(0, 0, false);
  } else
    SetEdgeScroll(false);
  return handled;
}

bool SpCityWindow::DefOnMouseDownL(i32, i32, u32) {
  return true;
}

// Start mouse scrolling at the press position and disable edge scrolling.
bool SpCityWindow::DefOnMouseDownR(i32 x, i32 y, u32) {
  SetMouseScroll(i16(x), i16(y), true);
  SetEdgeScroll(false);
  return true;
}

// Focus the camera on the picked cell after a left-button release when mouse scrolling is inactive.
bool SpCityWindow::DefOnMouseUpL(i32 x, i32 y, u32) {
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  if (mouseScrolling || !currentView->PerformPick(x, y, &position, false))
    return false;
  FocusCameraOn(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), true);
  return true;
}

bool SpCityWindow::DefOnMouseUpR(i32 x, i32 y, u32) {
  SetMouseScroll(i16(x), i16(y), false);
  return true;
}

bool SpCityWindow::DefOnMouseMove(i32 x, i32 y, u32) {
  UpdateScroll(i16(x), i16(y));
  return true;
}

// Recover the command-factory and direct navigation branches.
bool SpCityWindow::DefOnHotKey(u32 key, u32 option, u32) {
  u32 command = 0, argument = 0, flag = 0;
  switch (key) {
  case kSpHotkeyResidentialLowDensity:
    command = kEcZoneResidentialLowCommandClassId;
    break;
  case kSpHotkeyResidentialMediumDensity:
    command = kEcZoneResidentialMediumCommandClassId;
    break;
  case kSpHotkeyResidentialHighDensity:
    command = kEcZoneResidentialHighCommandClassId;
    break;
  case kSpHotkeyCommercialLowDensity:
    command = kEcZoneCommercialLowCommandClassId;
    break;
  case kSpHotkeyCommercialMediumDensity:
    command = kEcZoneCommercialMediumCommandClassId;
    break;
  case kSpHotkeyCommercialHighDensity:
    command = kEcZoneCommercialHighCommandClassId;
    break;
  case kSpHotkeyIndustrialLowDensity:
    command = kEcZoneIndustrialLowCommandClassId;
    break;
  case kSpHotkeyIndustrialMediumDensity:
    command = kEcZoneIndustrialMediumCommandClassId;
    break;
  case kSpHotkeyIndustrialHighDensity:
    command = kEcZoneIndustrialHighCommandClassId;
    break;
  case kSpHotkeyDezone:
    command = kEcDezoneCommandClassId;
    break;
  case kSpHotkeyPowerLines:
    command = kNwPowerToolClassId;
    break;
  case kSpHotkeyBusStop: {
    CtCity *city = SpGetCity();
    AdTransportation *advisor = city ? city->GetTransportationAdvisor() : nullptr;
    if (!advisor || !advisor->InventionAvailable(2))
      return false;
    command = kGeBusStopCommandClassId;
    break;
  }
  case kSpHotkeyRoads:
    command = kNwRoadToolClassId;
    break;
  case kSpHotkeyRails:
    command = kNwRailToolClassId;
    break;
  case kSpHotkeyTrainStation:
    command = kGeTrainStationCommandClassId;
    break;
  case kSpHotkeyDemolish:
    command = kGeDemolitionToolClassId;
    break;
  case kSpHotkeyMeetAdvisors: {
    ScMessage message(kWnMsg_MeetAdvisors);
    SpGetMessageServer()->MessagePost(message, false);
    return true;
  }
  case kSpHotkeyBudget:
    command = kCtBudgetCommandClassId;
    break;
  case kSpHotkeyOrdinances: {
    ScMessage message(kWnMsg_OrdinanceDialog);
    SpGetMessageServer()->MessagePost(message, false);
    return true;
  }
  case kSpHotkeyDataMaps:
    command = kUiRasterVisibilityCommandClassId;
    flag = 1;
    break;
  case kSpHotkeyPoliceStation:
    command = kGePoliceStationCommandClassId;
    break;
  case kSpHotkeyFireStation:
    command = kGeFireCommandClassId;
    break;
  case kSpHotkeyHospital:
    command = kGeHospitalCommandClassId;
    break;
  case kSpHotkeySchool:
    command = kGeSchoolCommandClassId;
    break;
  case kSpHotkeyOpenCity:
  case kSpHotkeyOpenCityAlternate:
    command = kCtLoadCityCommandId;
    argument = kCtFileOpenCity;
    break;
  case kSpHotkeyNewCity:
  case kSpHotkeyNewCityAlternate:
    command = kCtNewCityCommandId;
    break;
  case kSpHotkeySaveCity:
  case kSpHotkeySaveCityAlternate:
    command = kCtSaveCityCommandId;
    break;
  case kSpHotkeySaveCityAs:
  case kSpHotkeySaveCityAsAlternate:
    command = kCtSaveCityCommandId;
    argument = kCtFileSaveCity;
    break;
  case kSpHotkeyQuit:
  case kSpHotkeyQuitAlternate:
  case kSpHotkeyQuitSecondAlternate:
    SpGetApplication()->Quit(true, true);
    return true;
  case kSpHotkeyTogglePause: {
    CtCity *city = SpGetCity();
    if (!city->IsDisasterActive() && city->CanPlayerPauseUnpause()) {
      if (city->IsPaused())
        city->Unpause();
      else
        city->Pause();
    }
    return true;
  }
  case kSpHotkeyDecreaseSpeed: {
    CtCity *city = SpGetCity();
    if (city->CanPlayerChangeSpeed()) {
      i32 speed = city->GetGameSpeed();
      if (speed >= 2 && speed <= 4)
        city->SetGameSpeed(speed - 1);
    }
    return true;
  }
  case kSpHotkeyIncreaseSpeed: {
    CtCity *city = SpGetCity();
    if (city->CanPlayerChangeSpeed()) {
      i32 speed = city->GetGameSpeed();
      if (speed >= 1 && speed <= 3)
        city->SetGameSpeed(speed + 1);
    }
    return true;
  }
  case kSpHotkeyCheatDialog: {
    SwitchHandler(kSpHandlerDefault, nullptr);
    WnWindow *dialog = nullptr;
    SpGetCom()->GetClassObject(kApCheatDialogClassId, IID_WnWindow, reinterpret_cast<void **>(&dialog));
    if (!dialog)
      return false;
    GetWindowManager()->DoModalWin(parent, dialog);
    dialog->Release();
    return true;
  }
  case kSpHotkeyZoomIn:
    ZoomIn(true);
    return true;
  case kSpHotkeyZoomOut:
    ZoomOut(true);
    return true;
  case kSpHotkeyRotateClockwise:
    RotateCameraCW(true);
    return true;
  case kSpHotkeyRotateCounterclockwise:
    RotateCameraCCW(true);
    return true;
  case kSpHotkeyToggleUiPanels: {
    ScMessage message(kWnMsg_QueryUiVisibility);
    ScMessageServer *server = SpGetMessageServer();
    server->MessageSend(message);
    message.type = (message.data1 & 1) ? kWnMsg_HideUiPanels : kWnMsg_ShowUiPanels;
    message.data1 = 0;
    server->MessagePost(message, false);
    return false;
  }
  case kSpHotkeyFocusAtPointer: {
    if (windowManager && currentView) {
      CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
      if (currentView->PerformPick(lastMouseX, lastMouseY, &position, false))
        FocusCameraOn(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), true);
    }
    return true;
  }
  case kSpHotkeyPowerPlants:
    command = kUiBuildingCommandClassId;
    argument = kUiQueryPowerBuildings;
    break;
  case kSpHotkeyHighways:
  case kSpHotkeySubways:
  case kSpHotkeySubwayStation: {
    CtCity *city = SpGetCity();
    AdTransportation *advisor = city ? city->GetTransportationAdvisor() : nullptr;
    u32 invention = key == kSpHotkeyHighways ? 4 : 1;
    if (!advisor || !advisor->InventionAvailable(invention))
      return false;
    command = key == kSpHotkeyHighways ? kNwHighwayToolClassId
        : key == kSpHotkeySubways      ? kNwSubwayToolClassId
                                       : kGeSubwayStationCommandClassId;
    break;
  }
  case kSpHotkeyJail:
    command = kGeJailCommandClassId;
    break;
  case kSpHotkeyCollege:
    command = kGeCollegeCommandClassId;
    break;
  case kSpHotkeySmallPark:
    command = kGeSmallParkCommandClassId;
    break;
  case kSpHotkeyLargePark:
    command = kGeBigParkCommandClassId;
    break;
  case kSpHotkeyRewards:
    command = kUiBuildingCommandClassId;
    argument = kUiQueryRewards;
    break;
  case kSpHotkeyPreferences:
  case kSpHotkeyPreferencesAlternate:
    command = kUiPreferencesCommandClassId;
    break;
  case 63: {
    u32 viewFlags = currentView->GetCityViewFlags();
    currentView->SetCityViewFlags(viewFlags ^ 1u, true);
    return false;
  }
  case kSpHotkeyOpenTerrain:
  case kSpHotkeyOpenTerrainAlternate:
    command = kCtLoadCityCommandId;
    argument = kCtFileOpenTerrain;
    break;
  case kSpHotkeyWaterBuildings:
    command = kUiBuildingCommandClassId;
    argument = kUiQueryWaterBuildings;
    break;
  case kSpHotkeyPipes:
    command = kNwPipeToolClassId;
    break;
  case kSpHotkeyTrees:
    command = kGeFloraToolClassId;
    break;
  case kSpHotkeyPlaceWater:
    command = kTrWaterToolClassId;
    break;
  case kSpHotkeyLandmarks:
    command = kUiBuildingCommandClassId;
    argument = kUiQueryLandmarks;
    break;
  case kSpHotkeyNeighbors:
    command = kUiNeighborCommandClassId;
    flag = 1;
    break;
  case kSpHotkeyZoomLevel0:
  case kSpHotkeyZoomLevel0Alternate:
  case kSpHotkeyZoomLevel1:
  case kSpHotkeyZoomLevel1Alternate:
  case kSpHotkeyZoomLevel2:
  case kSpHotkeyZoomLevel2Alternate:
  case kSpHotkeyZoomLevel3:
  case kSpHotkeyZoomLevel3Alternate:
  case kSpHotkeyZoomLevel4:
  case kSpHotkeyZoomLevel4Alternate:
    SetZoom(i32((key - kSpHotkeyZoomLevel0) / 2), -1, true);
    return false;
  case kSpHotkeyCancelTool:
    SetActiveHandler(nullptr);
    return false;
  case kSpHotkeyOpenStarterTown:
  case kSpHotkeyOpenStarterTownAlternate:
    command = kCtLoadCityCommandId;
    argument = kCtFileOpenStarterTown;
    break;
  case kSpHotkeyLandfill:
    command = kEcZoneLandfillCommandClassId;
    break;
  case kSpHotkeyLayers:
    command = kUiModalCommand12576ClassId;
    break;
  case kSpHotkeyGarbageBuildings:
    command = kUiBuildingCommandClassId;
    argument = kUiQueryWasteBuildings;
    break;
  case kSpHotkeyViewAlbum:
  case kSpHotkeyTakeSnapshot: {
    NwInternet *internet = SpGetApplication()->GetSC3Internet();
    NwTransferSystem *transfer = internet ? internet->GetTransferSystem() : nullptr;
    if (transfer) {
      if (key == kSpHotkeyViewAlbum)
        transfer->ShowImageBrowser(true, true, nullptr, true);
      else
        transfer->TakeSnapshot(option >= 1 && option <= 3 ? option : 0);
    }
    return false;
  }
  case kSpHotkeyToggleCellCoordinates:
    showCellCoordinates = !showCellCoordinates;
    return false;
  default:
    return false;
  }
  UiCommandInterface *operation = nullptr;
  if (!commandFactory->GetCommand(command, IID_UiCommandInterface, reinterpret_cast<void **>(&operation)))
    return false;
  ScValue *first = new ScValue(argument, kScValueUint32);
  first->AddRef();
  operation->PushBackInput(first);
  ScValue *second = new ScValue(flag, kScValueUint32);
  second->AddRef();
  operation->PushBackInput(second);
  ScValue *third = new ScValue(u32(0), kScValueUint32);
  third->AddRef();
  operation->PushBackInput(third);
  bool result = operation->Execute();
  operation->Release();
  first->Release();
  second->Release();
  third->Release();
  return result;
}

void SpCityWindow::CameraGoHome() {
  currentView->CameraGoHome(true);
}

// Toggle edge scrolling and clear its four directional flags when disabling it.
void SpCityWindow::SetEdgeScroll(bool enabled) {
  edgeScrollEnabled = enabled;
  if (!enabled) {
    edgeScrollFlags[0] = false;
    edgeScrollFlags[1] = false;
    edgeScrollFlags[2] = false;
    edgeScrollFlags[3] = false;
  }
}

// Start scrolling at the supplied coordinates or clear it and restore the handler's cursor.
void SpCityWindow::SetMouseScroll(i16 x, i16 y, bool enabled) {
  mouseScrollX = x;
  mouseScrollY = y;
  mouseScrolling = enabled;
  if (enabled) {
    defaultCursor = activeCursor;
    return;
  }
  mouseScrollX = 0;
  mouseScrollY = 0;
  mouseScrollDeltaX = 0.0f;
  mouseScrollDeltaY = 0.0f;
  u32 cursorId = 0;
  if (activeHandler)
    activeHandler->GetCursor(&cursorId);
  SetActiveCursor(cursorId);
}

// At, partition the viewport into a central region and four fixed-width scroll margins.
void SpCityWindow::UpdateScrollRegions() {
  i32 left = viewportBounds.left, top = viewportBounds.top;
  i32 right = viewportBounds.right, bottom = viewportBounds.bottom;
  i32 leftMargin = i32(u32(left) + 64), rightMargin = i32(u32(right) - 64);
  i32 topMargin = i32(u32(top) + 48), bottomMargin = i32(u32(bottom) - 48);
  scrollCenter = {leftMargin, topMargin, rightMargin, bottomMargin};
  scrollLeft = {left, top, leftMargin, bottom};
  scrollTop = {left, top, right, topMargin};
  scrollRight = {rightMargin, top, right, bottom};
  scrollBottom = {left, bottomMargin, right, bottom};
}

// Update mouse-scroll displacement and directional cursor, or refresh the active edge-scroll regions.
void SpCityWindow::UpdateScroll(i16 x, i16 y) {
  if (mouseScrolling) {
    i32 dx = (i32(x) - i32(mouseScrollX)) / 2, dy = (i32(y) - i32(mouseScrollY)) / 2;
    i32 absX = dx < 0 ? -dx : dx, absY = dy < 0 ? -dy : dy;
    bool farX = absX > 80, farY = absY > 80, moveX = absX > 12, moveY = absY > 12;
    if (farX)
      mouseScrollDeltaX = dx > 0 ? 80.0f : -80.0f;
    if (farY)
      mouseScrollDeltaY = dy > 0 ? 80.0f : -80.0f;
    if (moveX && !farX)
      mouseScrollDeltaX = f32(dx);
    if (moveY && !farY)
      mouseScrollDeltaY = f32(dy);
    if (!farX && !moveX)
      mouseScrollDeltaX = 0.0f;
    if (!farY && !moveY)
      mouseScrollDeltaY = 0.0f;
    u32 cursorId;
    if (!farX && !moveX && !farY && !moveY) {
      cursorId = kSpEarlyMoverCursorId;
      if (activeCursor != cursorId)
        SetActiveCursor(cursorId);
    } else if (dy >= 12) {
      cursorId = dx >= 12 ? kSpPanDownRightArrowCursorId
          : dx <= -12     ? kSpPanDownLeftArrowCursorId
                          : kSpPanDownArrowCursorId;
    } else if (dy <= -12) {
      cursorId = dx >= 12 ? kSpPanUpRightArrowCursorId : dx <= -12 ? kSpPanUpLeftArrowCursorId : kSpPanUpArrowCursorId;
    } else {
      cursorId = dx >= 12 ? kSpPanRightArrowCursorId : dx <= -12 ? kSpPanLeftArrowCursorId : kSpEarlyMoverCursorId;
    }
    if (!rightDragMoved && cursorId != kSpEarlyMoverCursorId)
      rightDragMoved = true;
    if (activeCursor != cursorId)
      SetActiveCursor(cursorId);
  } else if (edgeScrollEnabled) {
    if (x >= scrollCenter.left && y >= scrollCenter.top && x < scrollCenter.right && y < scrollCenter.bottom) {
      edgeScrollFlags[3] = false;
      edgeScrollFlags[2] = false;
      edgeScrollFlags[1] = false;
      edgeScrollFlags[0] = false;
    } else {
      edgeScrollFlags[3] = x >= scrollLeft.left && y >= scrollLeft.top && x < scrollLeft.right && y < scrollLeft.bottom;
      edgeScrollFlags[0] = x >= scrollTop.left && y >= scrollTop.top && x < scrollTop.right && y < scrollTop.bottom;
      edgeScrollFlags[1] =
          x >= scrollBottom.left && y >= scrollBottom.top && x <= scrollBottom.right && y <= scrollBottom.bottom;
      edgeScrollFlags[2] =
          x >= scrollRight.left && y >= scrollRight.top && x <= scrollRight.right && y <= scrollRight.bottom;
      edgeStateChanged = true;
    }
  }
}

bool SpCityWindow::GZOnCaptureChanged(WnWindow *) {
  return true;
}

// Record a left press, acquire capture and dispatch the event while guarding temporary tool changes.
bool SpCityWindow::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  bool previous = inputReentrant;
  inputReentrant = true;
  lastMouseX = x;
  lastMouseY = y;
  lastMouseModifiers = modifiers;
  leftButtonDown = true;
  GZSetCapture();
  bool result = activeHandler ? activeHandler->OnMouseDownL(x, y, modifiers) : DefOnMouseDownL(x, y, modifiers);
  inputReentrant = previous;
  return result;
}

// Record a right press and its focus state, then dispatch the captured coordinates to the active or default handler.
bool SpCityWindow::OnMouseDownR(i32 x, i32 y, u32 modifiers) {
  bool previous = inputReentrant;
  inputReentrant = true;
  lastMouseX = x;
  lastMouseY = y;
  lastMouseModifiers = modifiers;
  rightButtonDown = true;
  rightFocusPending = true;
  rightDragMoved = false;
  GZSetCapture();
  bool result = activeHandler ? activeHandler->OnMouseDownR(lastMouseX, lastMouseY, lastMouseModifiers)
                              : DefOnMouseDownR(lastMouseX, lastMouseY, lastMouseModifiers);
  inputReentrant = previous;
  return result;
}

// Dispatch a matching left release and release capture when the right button is also up.
bool SpCityWindow::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  bool previous = inputReentrant;
  inputReentrant = true;
  lastMouseX = x;
  lastMouseY = y;
  lastMouseModifiers = modifiers;
  bool result = false;
  if (leftButtonDown) {
    leftButtonDown = false;
    if (!rightButtonDown)
      GZReleaseCapture();
    result = activeHandler ? activeHandler->OnMouseUpL(x, y, modifiers) : DefOnMouseUpL(x, y, modifiers);
  }
  inputReentrant = previous;
  return result;
}

// Finish a matching right release, focus an undragged click and forward the event before restoring the input guard.
bool SpCityWindow::OnMouseUpR(i32 x, i32 y, u32 modifiers) {
  bool previous = inputReentrant;
  inputReentrant = true;
  lastMouseX = x;
  lastMouseY = y;
  lastMouseModifiers = modifiers;
  bool result = false;
  if (rightButtonDown) {
    rightButtonDown = false;
    if (!leftButtonDown)
      GZReleaseCapture();
    if (rightFocusPending && !rightDragMoved) {
      rightFocusPending = false;
      CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
      if (currentView->PerformPick(lastMouseX, lastMouseY, &position, false))
        FocusCameraOn(u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), true);
    }
    result = activeHandler ? activeHandler->OnMouseUpR(x, y, modifiers) : DefOnMouseUpR(x, y, modifiers);
  }
  inputReentrant = previous;
  return result;
}

void SpCityWindow::OnMouseExit() {
  if (!unknown540 && !leftButtonDown && !rightButtonDown) {
    if (cellCursorVisible)
      cellCursor->Clear(true);
    if (activeHandler)
      activeHandler->OnResetState();
  }
  SetCostDisplay(false, 0, 0);
  SetMouseScroll(0, 0, false);
}

// Track the mouse position, process departures from the viewport and dispatch movement under the input guard.
bool SpCityWindow::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  bool previous = inputReentrant;
  inputReentrant = true;
  lastMouseX = x;
  lastMouseY = y;
  lastMouseModifiers = modifiers;
  if (x < viewportBounds.left || y < viewportBounds.top || x >= viewportBounds.right || y >= viewportBounds.bottom)
    OnMouseExit();
  bool result = activeHandler ? activeHandler->OnMouseMove(x, y, modifiers) : DefOnMouseMove(x, y, modifiers);
  inputReentrant = previous;
  return result;
}

bool SpCityWindow::GZOnMouseEnter(WnWindow *) {
  return true;
}

bool SpCityWindow::GZOnMouseExit(WnWindow *) {
  OnMouseExit();
  return true;
}

// Balance cost-display suppression; enabling only decrements an existing suppression count.
void SpCityWindow::EnableCostDisplay(bool enabled) {
  if (enabled) {
    if (costDisplayDisableCount)
      --costDisplayDisableCount;
  } else
    ++costDisplayDisableCount;
}

// Update the localized cost hint, place it around the mouse and send the appropriate tooltip notification.
void SpCityWindow::SetCostDisplay(bool visible, i32 value, i32 mode) {
  if (value != costValue) {
    costValue = value;
    SpGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(i64(costValue), &costText);
  }
  if (mode != costMode) {
    costMode = i8(mode);
    if (u8(mode) == 0)
      costHint->word28 = costColors[0];
    else if (u8(mode) == 1)
      costHint->word28 = costColors[1];
  }
  i32 x = lastMouseX, y = lastMouseY;
  if (x != costRectangle.left || y != costRectangle.top) {
    i32 right = i32(u32(x) + 44), bottom = i32(u32(y) + 44);
    costRectangle = {x, y, right, bottom};
    if (x < viewportBounds.left || y < viewportBounds.top || x > viewportBounds.right || y > viewportBounds.bottom ||
        right < viewportBounds.left || bottom < viewportBounds.top || right > viewportBounds.right ||
        bottom > viewportBounds.bottom) {
      costRectangle = {i32(u32(x) - 44), i32(u32(y) - 32), x, y};
    }
  }
  if (x >= viewportBounds.left && y >= viewportBounds.top && x <= viewportBounds.right && y <= viewportBounds.bottom)
    costShown = false;
  if (costHint) {
    costHint->flags |= 1;
    costHint->flags |= 2;
    costHint->text = &costText;
    costHint->rectangle = costRectangle;
  }
  u32 message;
  if (costShown && visible && !costDisplayDisableCount)
    message = kWnMsg_UpdateHint;
  else if (!costShown && visible && !costDisplayDisableCount)
    message = kWnMsg_StartHint;
  else
    message = kWnMsg_CancelHint;
  ScMessage notification(message, reinterpret_cast<uptr>(costHint), 0, 0);
  SpGetMessageServer()->MessageSend(notification);
  costShown = visible;
}

bool SpCityWindow::OnKeyDown(u32 key, u32 modifiers) {
  if (!inputReentrant) {
    if (!(modifiers & kWnModifierAlt) && key == kWnKeyControl)
      SwitchHandler(kSpHandlerLocation, nullptr);
    if (!(modifiers & kWnModifierControl) && key == kWnKeyAlt)
      SwitchHandler(kSpHandlerDemolition, nullptr);
  }
  return activeHandler ? activeHandler->OnKeyDown(key, modifiers) : DefOnKeyDown(key, modifiers);
}

// Restore the previous temporary tool for modifier releases, then dispatch the key-up event.
bool SpCityWindow::OnKeyUp(u32 key, u32 modifiers) {
  if (!inputReentrant && (key == kWnKeyAlt || key == kWnKeyControl))
    SwitchHandler(kSpHandlerRestore, nullptr);
  return activeHandler ? activeHandler->OnKeyUp(key, modifiers) : DefOnKeyUp(key, modifiers);
}

bool SpCityWindow::GZOnMouseWheel(i32, i32, u32 modifiers, i32 wheel) {
  if (wheel) {
    u32 key = wheel > 0 ? ((modifiers & kWnModifierShiftControlMask) ? kWnKeyRight : kWnKeyUp)
                        : ((modifiers & kWnModifierShiftControlMask) ? kWnKeyLeft : kWnKeyDown);
    OnKeyDown(key, 0);
    OnKeyUp(key, 0);
  }
  return true;
}

// Handle the four navigation commands by forwarding them to the current view.
bool SpCityWindow::OnCommand(u32 kind, u32 command) {
  if (kind != kWnCommandListDoubleClickSelection)
    return false;
  if (!currentView)
    return true;
  switch (command) {
  case kSpNavigationZoomIn:
    currentView->ZoomIn(true);
    return true;
  case kSpNavigationZoomOut:
    currentView->ZoomOut(true);
    return true;
  case kSpNavigationRotateCW:
    currentView->RotateCameraCW(true);
    return true;
  case kSpNavigationRotateCCW:
    currentView->RotateCameraCCW(true);
    return true;
  default:
    return false;
  }
}

#define WN_ROOT_CLASS WnSpriteRootWidget
#define WN_ROOT_EXTERNAL_CONSTRUCTOR
#define WN_ROOT_MANAGER SpWindowManager
#define WN_ROOT_GRAPHICS SpGetGraphics
#define WN_ROOT_RESOURCES SpGetWindowResources
#include "window/wn_root_widget_methods.inc"
#undef WN_ROOT_RESOURCES
#undef WN_ROOT_GRAPHICS
#undef WN_ROOT_MANAGER
#undef WN_ROOT_EXTERNAL_CONSTRUCTOR
#undef WN_ROOT_CLASS

SpEmptyTest::SpEmptyTest() : references(0) {
}

// Acquire the original Unknown or occupant-iterator-test identity. Rejected identities clear the output.
bool SpEmptyTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpEmptyTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, SpEmptyTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpEmptyTest::AddRef() {
  return ++references;
}

// Decrement before releasing the allocation on the final hold. These original filters have no destruction callback.
u32 SpEmptyTest::Release() {
  u32 remaining = --references;
  if (!remaining)
    ::operator delete(this);
  return remaining;
}

bool SpEmptyTest::Test(ScOccupant *) {
  return false;
}

SpSurfaceTest::SpSurfaceTest(CtZoneLayer *layer)
    : show_flora(false), show_transport(false), show_civic(false), show_zoned(false), show_unzoned(false), zone(layer) {
}

// Apply the original geometry-category precedence before checking a building's zone. Null occupants pass this filter.
bool SpSurfaceTest::Test(ScOccupant *occupant) {
  if (!occupant)
    return true;
  u32 flags = occupant->GetGeomFlag();
  if ((flags & kSpCivicGeomMask) == kSpCivicGeomMask)
    return show_civic;
  if (flags & kSpFloraGeomMask)
    return show_flora;
  if ((flags & kSpTransportGeomMask) == kSpTransportGeomMask)
    return show_transport;
  u8 type = occupant->MajorType();
  if (type != 1)
    return type != 6 || show_civic;
  if (show_unzoned && show_zoned)
    return true;
  u32 x, z;
  u8 value;
  occupant->GetLocationInCellCoords(&x, &z);
  zone->GetValue(x, z, &value);
  return value == 0 || value == 22 ? show_unzoned : show_zoned;
}

// Store all six original category switches in order. The installed predicate does not consult the final switch.
void SpSurfaceTest::SetTest(bool flora, bool transport, bool civic, bool zoned, bool unzoned, bool value) {
  show_flora = flora;
  show_transport = transport;
  show_civic = civic;
  show_zoned = zoned;
  show_unzoned = unzoned;
  extra = value;
}

bool SpSubwayTest::Test(ScOccupant *occupant) {
  return occupant && (occupant->GetGeomFlag() & kSpSubwayGeomMask) != 0;
}

bool SpPipeTest::Test(ScOccupant *occupant) {
  return occupant && (occupant->GetGeomFlag() & kSpPipeGeomMask) != 0;
}

bool SpFlammabilityTest::Test(ScOccupant *occupant) {
  u32 flags = occupant->GetGeomFlag();
  return (flags & kSpCivicGeomMask) == kSpCivicGeomMask ||
      ((flags & kSpBuildingGeomMask) && occupant->TypeId() == kCtAgentFireStation);
}

// Keep civic geometry or the original police-service type in the crime view. Other occupants fail this predicate.
bool SpCrimeTest::Test(ScOccupant *occupant) {
  u32 flags = occupant->GetGeomFlag();
  return (flags & kSpCivicGeomMask) == kSpCivicGeomMask ||
      ((flags & kSpBuildingGeomMask) && occupant->TypeId() == kCtAgentPoliceStation);
}

bool SpTrafficTest::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kSpTrafficGeomMask);
}

bool SpPowerTest::Test(ScOccupant *occupant) {
  return occupant->IsGeomFlag(kSpPowerGeomMask);
}

SpCellSelection::SpCellSelection() : cursor(0), references(0) {
  points.Reserve(128);
}

// Copy coordinates in stored order before copying the source's enumeration cursor. The clone begins unretained.
SpCellSelection::SpCellSelection(const SpCellSelection &source) : cursor(0), references(0) {
  for (u32 i = 0; i < source.points.size; ++i)
    Append(source.points.data[i]);
  cursor = source.cursor;
}

// Publish either supported identity before retaining it through the original virtual call. Failure leaves output alone.
bool SpCellSelection::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpSelection);
  HANDLE_QUERY_INTERFACE(IID_SpSelection, SpSelection);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpCellSelection::AddRef() {
  return ++references;
}

// Decrement the count before invoking the original virtual destruction path at zero. Unsigned underflow is preserved.
u32 SpCellSelection::Release() {
  u32 remaining = --references;
  if (!remaining)
    delete this;
  return remaining;
}

// Copy the requested prefix from the live cursor and report its actual length. End of storage still returns byte one.
u8 SpCellSelection::Next(u32 maximum, CtCoord *out, u32 *count) {
  u32 copied = 0;
  while (copied < maximum && cursor + copied < points.size) {
    out[copied] = points.data[cursor + copied];
    ++copied;
  }
  cursor += copied;
  *count = copied;
  return 1;
}

u8 SpCellSelection::Skip(u32 count) {
  cursor += count;
  return 1;
}

u8 SpCellSelection::Reset() {
  cursor = 0;
  return 1;
}

// Publish an independently allocated copy and then retain the published interface once. The source cursor is preserved.
u8 SpCellSelection::Clone(SpSelection **out) {
  SpCellSelection *copy = new SpCellSelection(*this);
  *out = copy;
  copy->AddRef();
  return 1;
}

// Compute signed X/Z bounds while preserving the original empty sentinel and nonempty altitude sentinels.
// Even an empty enumeration reports true because the source compares its first X coordinate against minus one.
bool SpCellSelection::GetBounds(GeBounds *out) {
  out->minimum = {-256, -256, 0};
  out->maximum = {-256, -256, 0};
  if (points.size) {
    out->minimum = out->maximum = points.data[0];
    for (u32 i = 0; i < points.size; ++i) {
      const CtCoord &position = points.data[i];
      if (position.x < out->minimum.x)
        out->minimum.x = position.x;
      if (position.z < out->minimum.z)
        out->minimum.z = position.z;
      if (position.x > out->maximum.x)
        out->maximum.x = position.x;
      if (position.z > out->maximum.z)
        out->maximum.z = position.z;
      out->minimum.altitude = out->maximum.altitude = INT32_MAX;
    }
  }
  return out->minimum.x != -1;
}

// Append batches using the source's reset/next protocol and its defined extra history coordinate for short replies.
// A full32-coordinate reply excludes the source's subsequent out-of-array read; exhaustion depends on Next's byte.
u8 SpCellSelection::AppendFrom(SpSelection *source) {
  CtCoord batch[32];
  for (u32 i = 0; i < 32; ++i)
    batch[i] = {INT32_MAX, INT32_MAX, INT32_MAX};
  u32 count = 0;
  source->Reset();
  while (source->Next(32, batch, &count)) {
    u32 defined_count = count < 32 ? count + 1 : 32;
    for (u32 i = 0; i < defined_count; ++i)
      Append(batch[i]);
  }
  return 1;
}

// Sort signed X/Z keys and compact duplicate coordinates while retaining the original vector size and tail.
// All21 original sort/unique code bodies match SIMNTWRK's specialization, including equal-key altitude permutations.
u8 SpCellSelection::SortAndCompact() {
  CtCoord *end = points.data;
  if (points.size)
    end += points.size;
  ScSortCellPoints(points.data, end);
  return 1;
}

SpCellSelection::~SpCellSelection() {
  points.Clear();
  cursor = 0;
}

void SpCellSelection::Append(const CtCoord &position) {
  CtCoord value = position;
  *points.Append() = value;
}
