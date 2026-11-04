#include "sprites/view_camera.h"

#include <cmath>
#include <cstring>

#include "application/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "sprites/api.h"
#include "sprites/module.h"
#include "sprites/sp_tick_manager.h"
#include "terrain/tr_dirt_bag.h"

// NotifyClients sends this view notification synchronously through ScMessageServer with first/second unchanged.
// Its original trigger is unresolved; unlike ordinary view changes it does not publish a fresh view snapshot.
enum SpUnresolvedViewMessage : u32 {
  kWnMsg_Unresolved34 = 0xb47df4e2,
};

// A new shake starts with a backdated frame timestamp so its first update is immediately due.
enum SpShakeTiming : i32 {
  kSpShakeInitialFrameBackdate = 0xf4240,
};

// Serialized keys retain their record-format role independently of live class and interface identities.
enum SpViewArchiveKey : u32 {
  kSpViewRecordType = 0x620edfce,
  kSpViewRecordGroup = 0xa20fb3b9,
};

// Announce the requested view, then apply rotation before zoom while holding the optional city graphics lock.
// Re-read live object pointers after callbacks and emit the after-change notification even for unchanged requests.
void SpView::SetZoomAndRotation(i32 new_zoom, i32 new_rotation, bool update) {
  NotifyClients(kWnMsg_CameraChanging, new_zoom, new_rotation);
  i32 old_zoom = cell_map->GetZoom(), old_rotation = cell_map->GetRotate();
  if (city)
    city->LockGraphics();
  if (new_zoom == old_zoom) {
    if (new_rotation != old_rotation)
      cell_map->SetRotate(new_rotation, update, focus_x, focus_z);
  } else {
    SetBackgroundBitmapForZoom(new_zoom);
    if (new_rotation != old_rotation)
      cell_map->SetRotate(new_rotation, false, focus_x, focus_z);
    cell_map->SetZoom(new_zoom, update, focus_x, focus_z);
  }
  if (city)
    city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraChanged, new_zoom, new_rotation);
}

// Publish zoom before querying and publishing rotation while holding the actual city graphics critical section.
// The two original outputs may alias, so rotation is deliberately the last stored word.
void SpView::GetZoomAndRotation(i32 *zoom, i32 *rotation) {
  ScLock *critical = city->GetGraphicsCriticalSection();
  critical->Lock();
  *zoom = cell_map->GetZoom();
  *rotation = cell_map->GetRotate();
  critical->Unlock();
}

// Expose the original Unknown, city-view and isometric-view identities on the primary receiver. Unsupported requests
// clear the output.
bool SpView::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpIsoView);
  HANDLE_QUERY_INTERFACE(IID_SpViewInterface, SpIsoView);
  HANDLE_QUERY_INTERFACE(IID_SpIsoView, SpIsoView);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpView::AddRef() {
  return ++references;
}

// Answer the original zoom/rotation, view flags and focus query messages in place. The two pointer outputs retain their
// original virtual getter dispatch.
bool SpView::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_QueryViewZoomAndRotation) {
    i32 zoom, rotation;
    GetZoomAndRotation(&zoom, &rotation);
    message->data1 = (u32)zoom;
    message->data2 = (u32)rotation;
  } else if (message->type == kWnMsg_QueryCityViewParams) {
    message->data1 = (u32)GetCityViewID();
    message->data2 = GetCityViewFlags();
  } else if (message->type == kWnMsg_QueryViewFocusCell) {
    u32 x, z;
    GetFocusCell(&x, &z);
    message->data1 = x;
    message->data2 = z;
  } else
    return false;
  return true;
}

// Maintain the original nested plot-disable counter. Enabling decrements the word, including its defined unsigned wrap
// when unbalanced.
u32 SpView::EnablePlot(bool enabled) {
  if (enabled)
    --plot_disabled;
  else
    ++plot_disabled;
  return plot_disabled;
}

u32 SpView::GetViewID() const {
  return kSpViewRecordGroup;
}

// Translate the camera by the original zoom-dependent offset when crossing the underground boundary. Dispatch the
// manager change afterward and always notify clients.
void SpView::SetCityViewID(i32 id, bool update) {
  i32 previous;
  state->GetViewID(&previous);
  i32 offset = 0;
  if ((id == 1 || id == 2) && previous != 1 && previous != 2) {
    i32 zoom, rotation;
    GetZoomAndRotation(&zoom, &rotation);
    offset = (i32)(0u - 8u * (u32)zoom);
  }
  if (id != 1 && id != 2 && (previous == 1 || previous == 2)) {
    i32 zoom, rotation;
    GetZoomAndRotation(&zoom, &rotation);
    offset = (i32)(8u * (u32)zoom);
  }
  Translate(0.0f, (f32)offset, true);
  state->SetViewID(id, update);
  NotifyClients(kWnMsg_CityDataViewChanged, 0, 0);
}

// Publish the cell map view clip while holding the city graphics lock. The original absent-map path returns false
// without touching the output.
bool SpView::GetScreenClipRect(ScRect *out) const {
  if (!cell_map)
    return false;
  city->LockGraphics();
  cell_map->GetViewClipRect(out);
  city->UnlockGraphics();
  return true;
}

void SpView::CameraGoHome(bool update) {
  FocusCameraOn((u32)width >> 1, (u32)height >> 1, update);
}

// Move the view to a valid cell after projecting it into city pixels and subtracting half the client area. Rejected
// cells unlock without changing focus or notifying clients.
void SpView::FocusCameraOn(u32 x, u32 z, bool update) {
  city->LockGraphics();
  if (!cell_map->InCellBounds((i32)x, (i32)z)) {
    city->UnlockGraphics();
    return;
  }
  focus_x = (i32)x;
  focus_z = (i32)z;
  i32 pixel_x, pixel_y;
  cell_map->ActualGridToCityPixel((i32)x, (i32)z, &pixel_x, &pixel_y);
  pixel_x = (i32)((u32)pixel_x - ((u32)screen_width >> 1));
  pixel_y = (i32)((u32)pixel_y - ((u32)screen_height >> 1));
  cell_map->MoveAbsolute(pixel_x, pixel_y, update);
  city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraFocusChanged, 0, 0);
}

void SpView::GetFocusCell(u32 *x, u32 *z) {
  *x = (u32)focus_x;
  *z = (u32)focus_z;
}

// Ask the cell map for both visible rectangles under the actual graphics critical section. The original optional
// rectangle pointers pass through unchanged.
void SpView::GetCameraHint(ScRect *first, ScRect *second) {
  ScLock *graphics = city->GetGraphicsCriticalSection();
  graphics->Lock();
  cell_map->GetVisibleAreaRectangle(first, second);
  graphics->Unlock();
}

// Advance one zoom level unless already at four, notifying before the graphics lock and after unlock. Background
// selection precedes the cell-map zoom operation.
void SpView::ZoomIn(bool update) {
  i32 zoom = cell_map->GetZoom();
  if (zoom == 4)
    return;
  i32 rotation = cell_map->GetRotate(), next_zoom = zoom + 1;
  NotifyClients(kWnMsg_CameraChanging, (u32)next_zoom, (u32)rotation);
  city->LockGraphics();
  SetBackgroundBitmapForZoom(next_zoom);
  cell_map->ZoomIn(update, focus_x, focus_z);
  city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraChanged, (u32)next_zoom, (u32)rotation);
}

// Reduce one zoom level unless already at zero. Retain the original background, lock and notification ordering around
// the cell-map operation.
void SpView::ZoomOut(bool update) {
  i32 zoom = cell_map->GetZoom();
  if (zoom == 0)
    return;
  i32 rotation = cell_map->GetRotate(), next_zoom = zoom - 1;
  NotifyClients(kWnMsg_CameraChanging, (u32)next_zoom, (u32)rotation);
  city->LockGraphics();
  SetBackgroundBitmapForZoom(next_zoom);
  cell_map->ZoomOut(update, focus_x, focus_z);
  city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraChanged, (u32)next_zoom, (u32)rotation);
}

// Announce the decremented compass with zero wrapping to three, then rotate the map left under the graphics lock.
// Notify afterward using the previously captured zoom and compass.
void SpView::RotateCameraCW(bool update) {
  i32 zoom = cell_map->GetZoom(), rotation = cell_map->GetRotate();
  i32 next_rotation = rotation ? rotation - 1 : 3;
  NotifyClients(kWnMsg_CameraChanging, (u32)zoom, (u32)next_rotation);
  city->LockGraphics();
  cell_map->RotateLeft(update, focus_x, focus_z);
  city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraChanged, (u32)zoom, (u32)next_rotation);
}

// Announce the incremented compass with three wrapping to zero, then rotate the map right under lock. Preserve the
// captured zoom and resulting compass in the after-change event.
void SpView::RotateCameraCCW(bool update) {
  i32 zoom = cell_map->GetZoom(), rotation = cell_map->GetRotate();
  i32 next_rotation = rotation != 3 ? rotation + 1 : 0;
  NotifyClients(kWnMsg_CameraChanging, (u32)zoom, (u32)next_rotation);
  city->LockGraphics();
  cell_map->RotateRight(update, focus_x, focus_z);
  city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraChanged, (u32)zoom, (u32)next_rotation);
}

// Store the supplied animation frequency before deciding whether ticking is enabled. The source compares the float
// input against the double constant 0.1.
void SpView::SetAnimFrequency(const f32 &frequency) {
  f64 value = frequency;
  animation_frequency = frequency;
  animation_enabled = value >= 0.1;
}

void SpView::GetAnimFrequency(f32 *frequency) {
  *frequency = animation_frequency;
}

// Return the borrowed original animation manager. The view retains its existing ownership.
SpTickManager *SpView::GetAnimTickManager() {
  return tick_manager;
}

// Ask the live sprite manager for its currently selected occupant manager. The returned pointer is borrowed.
ScOccupantManager *SpView::CurrentOccupantManager() {
  return state->GetCurrentOccupantManager();
}

// Run the real cell-map pick under the city graphics lock. Successful picks publish fixed X/Z coordinates and zero
// altitude only after unlocking.
bool SpView::PerformPick(i32 x, i32 y, CtCoord *out, bool artwork) {
  city->LockGraphics();
  i32 cell_x, cell_z;
  bool found = cell_map->WindowPixelToActualGrid_NonInline(x, y, &cell_x, &cell_z, artwork, nullptr, nullptr);
  city->UnlockGraphics();
  if (!found)
    return false;
  out->x = (i32)((u32)cell_x << 8);
  out->altitude = 0;
  out->z = (i32)((u32)cell_z << 8);
  return true;
}

// Query the manager only when the original unaligned flag is enabled. Its result is deliberately ignored; the view
// returns true even when no query is requested.
bool SpView::GetOccupantsInVolume(const GeBounds &bounds, ScOccupant **out, u32 *count, bool, bool unaligned) {
  if (unaligned) {
    city->LockGraphics();
    state->GetUnalignedOccupantsInVolume(bounds, out, count);
    city->UnlockGraphics();
  }
  return true;
}

// Return the borrowed public city-sprite-manager interface. It is the same primary identity stored by the view.
SpManagerView *SpView::GetCitySpriteManager() {
  return state;
}

// Return the original borrowed cell map. No new reference is acquired.
SpCellMap *SpView::GetCitySpriteCellMap() {
  return cell_map;
}

// Start a shake only when none is active and the duration is at least 1000. Preserve the original byte narrowing,
// pending flag and interval writes.
bool SpView::DoScreenShake(i32 mode, i32 duration, i32 first, i32 second, bool update, i32 interval) {
  if (shaking || duration < 1000)
    return false;
  shaking = 1;
  shake_update = update;
  shake_mode = (u8)mode;
  shake_elapsed = 0;
  shake_first = (i8)first;
  shake_duration = duration;
  shake_second = (i8)second;
  shake_interval = interval;
  shake_direction = 1;
  return true;
}

bool SpView::IsScreenShaking() {
  return shaking != 0;
}

u8 SpView::SetSmoothNavigation(bool enabled) {
  smooth_navigation = enabled;
  return smooth_navigation;
}

u8 SpView::GetSmoothNavigation() {
  return smooth_navigation;
}

u8 SpView::SetNotify(bool enabled) {
  events_enabled = enabled;
  return events_enabled;
}

// Return the same stored manager primary used by the public getter. The private concrete/interface pointee spelling
// remains qualified.
SpManagerView *SpView::GetViewManager() {
  return state;
}

// Return the same stored cell-map object used by the public getter. This private getter does not acquire ownership.
SpCellMap *SpView::GetSpriteCellMap() {
  return cell_map;
}

// Round the screen displacement with the original double-store conversion and attempt both terrain pick modes.
// Only accepted picks move the map and replace focus, but the focus notification is emitted on either result.
void SpView::Translate(f32 x, f32 y, bool update) {
  f64 rounded_x = (f64)x + kScDoubleIntegerBias, rounded_y = (f64)y + kScDoubleIntegerBias;
  u64 bits_x, bits_y;
  memcpy(&bits_x, &rounded_x, sizeof(bits_x));
  memcpy(&bits_y, &rounded_y, sizeof(bits_y));
  u32 delta_x = (u32)bits_x + kScDoubleIntegerLowWordBias, delta_y = (u32)bits_y + kScDoubleIntegerLowWordBias;
  i32 pixel_x = (i32)(delta_x + ((u32)screen_width >> 1));
  i32 pixel_y = (i32)(delta_y + ((u32)screen_height >> 1));
  city->LockGraphics();
  i32 cell_x, cell_z;
  if (cell_map->WindowPixelToActualGrid_NonInline(pixel_x, pixel_y, &cell_x, &cell_z, false, nullptr, nullptr) ||
      cell_map->WindowPixelToActualGrid_NonInline(pixel_x, pixel_y, &cell_x, &cell_z, true, nullptr, nullptr)) {
    cell_map->MoveRelative((i32)delta_x, (i32)delta_y, update);
    focus_x = cell_x;
    focus_z = cell_z;
  }
  city->UnlockGraphics();
  NotifyClients(kWnMsg_CameraFocusChanged, 0, 0);
}

// Update the client dimensions, cell-map clip and first retained copy rectangle using the source's live fields.
// The original missing-map path returns false; a captured live-city predicate controls both optional lock calls.
bool SpView::SetScreenClipRect(const ScRect &clip) {
  bool has_city = SpHasCity();
  if (has_city && city)
    city->LockGraphics();
  screen_width = (i32)((u32)clip.right - (u32)clip.left);
  screen_height = (i32)((u32)clip.bottom - (u32)clip.top);
  if (cell_map) {
    cell_map->SetViewClipRect(clip);
    // An empty native copy vector has no first record; exclude the original invalid-storage write.
    if (ScRect *first = cell_map->copy_rects[0].data) {
      first->left = clip.left;
      first->top = clip.top;
      first->right = clip.right;
      first->bottom = clip.bottom;
    }
    if (has_city && city)
      city->UnlockGraphics();
    return true;
  }
  if (has_city && city)
    city->UnlockGraphics();
  return false;
}

// Load the original zoom-specific BackN.bmp through the real image factory, then install it under the city lock.
void SpView::SetBackgroundBitmapForZoom(i32 zoom) {
  ScString path;
  SpGetApplication()->GetAppDataDirectory(&path);
  path.Append("Res\\Sprites\\BackN.bmp");
  path.end[-5] = (char)((u32)zoom + 48u);
  GxBuffer *image = nullptr;
  void *identity = nullptr;
  if (SpGetCom()->GetClassObject(kRsImageFactoryClassId, IID_RsImageOptions, &identity)) {
    RsImageOptions *factory = static_cast<RsImageOptions *>(identity);
    GxColorType format(GxColorType::Color::RGB16);
    factory->LoadFile(&path, &image, 0, &format, 0);
    factory->Release();
  }
  ScLock *graphics = city->GetGraphicsCriticalSection();
  graphics->Lock();
  cell_map->SetBackgroundBuffer(image, false);
  graphics->Unlock();
}

static u32 SpViewHighlightColor;
static bool SpViewTickActive;

// Initialize the original view state and its three real stopwatches, then start frame timing and pack the highlight.
// Native zero seeds cover only original unwritten storage; valid initialization still supplies the city and surfaces.
SpView::SpView()
    : frame_clock(ScStopwatch::Seconds, 0), animation_clock(ScStopwatch::Milliseconds, 0),
      shake_clock(ScStopwatch::Milliseconds, 0) {
  references = 0;
  initialized = events_enabled = flag41 = 0;
  animation_enabled = 1;
  frame_total = 0;
  frame_rate = 0.0f;
  animation_frequency = 60.0f;
  canvas_buffer = nullptr;
  width = height = 0;
  flag200 = 1;
  plot_disabled = 0;
  shaking = shake_update = 0;
  shake_duration = 0;
  shake_mode = 0;
  shake_first = shake_second = shake_direction = 0;
  shake_elapsed = 0;
  application = nullptr;
  city = nullptr;
  data = nullptr;
  controller = state = nullptr;
  cell_map = nullptr;
  tick_manager = nullptr;
  // These fields were not initialized by the original constructor; do not expose unwritten native memory.
  focus_x = focus_z = screen_width = screen_height = shake_last = shake_interval = 0;
  multiplier = 0;
  smooth_navigation = 0;
  screen_rectangle = shake_source = shake_destination = {0, 0, 0, 0};
  frame_clock.Start();
  SpViewHighlightColor = SpGetGraphics()->ConvertRGBValueToNative16Bit(0, 138, 180);
}

// Destroy the three stopwatches in reverse member order. The original destructor does not call city shutdown.
SpView::~SpView() = default;

// Delete through the real virtual destructor when the count is zero or one, without decrementing it first.
// Higher counts are decremented and returned directly; no module reference is involved.
u32 SpView::Release() {
  if (references <= 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Create the city map, retained view snapshot and manager under the original city graphics lock.
// Adopt the active draw surface and animation manager before registering and enabling the view's notifications.
bool SpView::Init(ApGameInterface *new_application, CtCity *source_city) {
  ScLock *graphics = source_city->GetGraphicsCriticalSection();
  graphics->Lock();
  application = new_application;
  city = source_city;
  multiplier = u32(i64(new_application->GetCity()->CellSizeInWorldUnitsX()));
  width = i32(application->GetCity()->CellCountX() - 1);
  height = i32(application->GetCity()->CellCountZ() - 1);
  focus_x = focus_z = 0;
  data = ScAddRefNotNull(new SpViewData);
  canvas_buffer = SpGetGraphics()->ScreenBuffer()->ActiveDrawBuffer();
  screen_width = i32(canvas_buffer->Width());
  screen_height = i32(canvas_buffer->Height());
  screen_rectangle = {0, 0, screen_width, screen_height};
  cell_map = ScAddRefNotNull(new SpCellMap);
  CtCity *z_city = new_application->GetCity(), *x_city = new_application->GetCity();
  u32 client_height = u32(screen_rectangle.bottom) - u32(screen_rectangle.top);
  u32 client_width = u32(screen_rectangle.right) - u32(screen_rectangle.left);
  u32 cells_z = z_city->CellCountZ(), cells_x = x_city->CellCountX();
  cell_map->Init(cells_x, cells_z, client_width, client_height, 0, 0, 0, 0, false);
  cell_map->back_buffer = canvas_buffer;
  cell_map->copy_index = 0;
  flag200 = SpGetGraphics()->ScreenBuffer()->IsPageFlipping();
  *cell_map->copy_rects[0].Append() = screen_rectangle;
  SetBackgroundBitmapForZoom(cell_map->GetZoom());
  if (!tick_manager)
    application->GetCity()->AnimOccupantManager()->QueryInterface(
        IID_SpTickManager, reinterpret_cast<void **>(&tick_manager));
  controller = ScAddRefNotNull(new SpManager);
  state = ScAddRefNotNull(controller);
  state->Init(application->GetCity(), cell_map);
  ScMessageServer *server = SpGetMessageServer();
  server->AddNotification(this, kWnMsg_QueryViewFocusCell);
  server->AddNotification(this, kWnMsg_QueryViewZoomAndRotation);
  server->AddNotification(this, kWnMsg_QueryCityViewParams);
  ScMessage message = {kWnMsg_AnimationTickManagerReady, uptr(tick_manager), 0, 0};
  SpGetMessageServer()->MessagePost(message, false);
  SetNotify(true);
  CameraGoHome(true);
  initialized = 1;
  graphics->Unlock();
  return true;
}

// Remove the view's query notifications and release the controller, manager, map, snapshot and tick manager in order.
// The borrowed application, city and canvas pointers are cleared only after owned interfaces have been released.
bool SpView::Shutdown() {
  if (!initialized)
    return false;
  ScLock *graphics = city->GetGraphicsCriticalSection();
  graphics->Lock();
  SetNotify(false);
  ScMessageServer *server = SpGetMessageServer();
  server->RemoveNotification(this, kWnMsg_QueryViewFocusCell);
  server->RemoveNotification(this, kWnMsg_QueryViewZoomAndRotation);
  server->RemoveNotification(this, kWnMsg_QueryCityViewParams);
  if (controller) {
    controller->Shutdown();
    controller->Release();
    controller = nullptr;
  }
  ScRelease(&state);
  if (cell_map) {
    cell_map->Shutdown();
    cell_map->Release();
    cell_map = nullptr;
  }
  ScRelease(&data);
  ScRelease(&tick_manager);
  initialized = 0;
  canvas_buffer = nullptr;
  city = nullptr;
  application = nullptr;
  graphics->Unlock();
  return true;
}

// Tick animation through the original module-wide recursion guard, then draw or copy the city surface under lock.
// Frame statistics and shake progression retain their original order, including plotting-disabled shake updates.
void SpView::Plot() {
  if ((i32)plot_disabled > 0 || (shaking && !shake_update))
    cell_map->IsCurrentZoomAndRotationLoaded();
  else {
    if (animation_enabled) {
      if (!SpViewTickActive) {
        SpViewTickActive = true;
        if (tick_manager)
          tick_manager->DoAnimationTick();
        SpViewTickActive = false;
      }
      ++frame_total;
      // The stopped float timer leaves its output untouched; seed only that original undefined-read case.
      f32 seconds = 0.0f;
      frame_clock.GetElapsedTime(&seconds);
      if (seconds > 3.0f) {
        frame_rate = (f32)(1.0 / ((f64)seconds * 3.0) * (i32)frame_total);
        frame_clock.Reset();
        frame_total = 0;
      }
    }
    city->LockGraphics();
    if (cell_map->IsChanged()) {
      if (!shaking && canvas_buffer->Lock(GxLockFlags::Blt)) {
        GxBuffer *source = cell_map->GetCityBufferPtr();
        canvas_buffer->BltNoClip(source, screen_rectangle, screen_rectangle);
        canvas_buffer->Unlock(GxLockFlags::Blt);
      }
    } else
      cell_map->UpdateAllDirtyRects(true, !shaking);
    city->UnlockGraphics();
  }
  if (shaking)
    DoNextShakeFrame();
}

void SpView::DoNextShakeFrame() {
  if (!shake_elapsed) {
    shake_clock.Restart();
    shake_elapsed = 1;
    shake_last = -kSpShakeInitialFrameBackdate;
  } else
    shake_elapsed = (i32)shake_clock.GetElapsedTime();
  if (shake_elapsed > shake_duration) {
    shaking = 0;
    return;
  }
  if ((i32)((u32)shake_elapsed - (u32)shake_last) > shake_interval) {
    shake_last = shake_elapsed;
    f64 fraction = (f64)shake_elapsed / shake_duration;
    f64 position = fraction + fraction;
    f64 value =
        (f64)(shake_second - shake_first) * (position >= 1.0 ? position - 1.0 : position) + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &value, sizeof(bits));
    u32 amount = position >= 1.0 ? (u32)shake_second - (u32)bits + kScDoubleIntegerLowWordBias
                                 : (u32)shake_first + (u32)bits + kScDoubleIntegerLowWordBias;
    shake_direction = (i8)-shake_direction;
    shake_source = screen_rectangle;
    shake_destination = screen_rectangle;
    if (!shake_mode) {
      if (shake_direction <= 0)
        shake_source.bottom = (i32)((u32)shake_source.bottom - amount);
      else
        shake_source.top = (i32)((u32)shake_source.top + amount);
      shake_destination.bottom = (i32)((u32)shake_destination.bottom - amount);
    } else {
      if (shake_direction <= 0)
        shake_source.right = (i32)((u32)shake_source.right - amount);
      else
        shake_source.left = (i32)((u32)shake_source.left + amount);
      shake_destination.right = (i32)((u32)shake_destination.right - amount);
    }
  }
  if (canvas_buffer->Lock(GxLockFlags::Blt)) {
    GxBuffer *source = cell_map->GetCityBufferPtr();
    canvas_buffer->BltNoClip(source, shake_source, shake_destination);
    canvas_buffer->Unlock(GxLockFlags::Blt);
  }
}

SpViewData::SpViewData() : references(0), focus{INT32_MAX, INT32_MAX, INT32_MAX}, bounds(), zoom(2), rotation(0) {
}

SpViewData::~SpViewData() {
}

// Publish supported view-data interfaces before retaining the object. Unsupported identifiers clear the output.
bool SpViewData::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpViewData);
  HANDLE_QUERY_INTERFACE(IID_SpIsoViewInfo, SpViewData);
  HANDLE_QUERY_INTERFACE(IID_SpViewInfo, SpViewData);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining its module. Return the live object count after the module operation.
u32 SpViewData::AddRef() {
  ++references;
  SpGetDirector()->AddRef();
  return references;
}

// Decrement before removing the module reference, then delete only when the live object count is zero.
u32 SpViewData::Release() {
  --references;
  SpGetDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool SpViewData::SetType(i32 value) {
  view_id = value;
  return true;
}

bool SpViewData::SetFlags(u32 value) {
  flags = value;
  return true;
}

// Copy the six original coordinate words in their reached store order. No bounds validation is added.
bool SpViewData::SetInfo(const GeBounds &source) {
  bounds.minimum.x = source.minimum.x;
  bounds.minimum.altitude = source.minimum.altitude;
  bounds.minimum.z = source.minimum.z;
  bounds.maximum.x = source.maximum.x;
  bounds.maximum.altitude = source.maximum.altitude;
  bounds.maximum.z = source.maximum.z;
  return true;
}

// Copy the focus x, altitude and z words in the source order, including its altitude sentinel.
bool SpViewData::SetInfo(const CtCoord &position) {
  focus.x = position.x;
  focus.altitude = position.altitude;
  focus.z = position.z;
  return true;
}

bool SpViewData::SetZoom(const i32 &value) {
  zoom = value;
  return true;
}

bool SpViewData::SetCompass(const i32 &value) {
  rotation = value;
  return true;
}

u32 SpViewData::GetViewId() const {
  return kSpViewRecordGroup;
}

bool SpViewData::GetInfo(u32 *) const {
  return false;
}

bool SpViewData::SetInfo(i32) {
  return false;
}

bool SpViewData::GetType(i32 *out) const {
  *out = view_id;
  return true;
}

bool SpViewData::GetFlags(u32 *out) const {
  *out = flags;
  return true;
}

// Copy focus in the original x, altitude, z store order. Its sentinel altitude is preserved verbatim.
bool SpViewData::GetInfo(CtCoord *out) const {
  out->x = focus.x;
  out->altitude = focus.altitude;
  out->z = focus.z;
  return true;
}

// Copy both coordinate triples in the original x, altitude, z order. The operation does not normalize the bounds.
bool SpViewData::GetInfo(GeBounds *out) const {
  out->minimum.x = bounds.minimum.x;
  out->minimum.altitude = bounds.minimum.altitude;
  out->minimum.z = bounds.minimum.z;
  out->maximum.x = bounds.maximum.x;
  out->maximum.altitude = bounds.maximum.altitude;
  out->maximum.z = bounds.maximum.z;
  return true;
}

bool SpViewData::GetZoom(i32 *out) const {
  *out = zoom;
  return true;
}

bool SpViewData::GetCompass(i32 *out) const {
  *out = rotation;
  return true;
}

void SpViewData::GetTilingOriginWindowPixel(i32 *x, i32 *y) const {
  *x = tiling_x;
  *y = tiling_y;
}

// Ask the selection object to publish its current city-view ID, then return that actual output word.
i32 SpView::GetCityViewID() const {
  i32 value;
  state->GetViewID(&value);
  return value;
}

u32 SpView::GetCityViewFlags() const {
  return state->GetCityViewFlags();
}

static i32 SpViewInterpolate(i32 base, i32 first, i32 second) {
  i32 delta = (i32)(((u32)first - (u32)second) * 2u);
  return (i32)((u32)base + (u32)(delta / 3));
}

// Populate the live snapshot through the original setter, geometry and city graphics-lock sequence, then publish its
// pointer.
// The right-edge interpolation intentionally uses the first top edge and the already-updated second top edge.
u8 SpView::GetViewInfo(SpViewInfo **out) {
  if (!data || !cell_map)
    return 0;
  CtCoord focus = {INT32_MAX, INT32_MAX, INT32_MAX};
  i32 id = GetCityViewID();
  data->SetType(id);
  u32 flags = GetCityViewFlags();
  data->SetFlags(flags);
  i32 value = cell_map->GetZoom();
  data->SetZoom(value);
  value = cell_map->GetRotate();
  data->SetCompass(value);
  ScRect first, second;
  city->LockGraphics();
  cell_map->GetVisibleAreaRectangle(&first, &second);
  city->UnlockGraphics();
  ScRect *rectangles[] = {&first, &second};
  for (ScRect *rectangle : rectangles) {
    if (rectangle->left > rectangle->right) {
      i32 saved = rectangle->left;
      rectangle->left = rectangle->right;
      rectangle->right = saved;
    }
    if (rectangle->top > rectangle->bottom) {
      i32 saved = rectangle->top;
      rectangle->top = rectangle->bottom;
      rectangle->bottom = saved;
    }
  }
  second.left = SpViewInterpolate(second.left, first.left, second.left);
  second.top = SpViewInterpolate(second.top, first.top, second.top);
  second.right = SpViewInterpolate(second.right, first.top, second.top);
  second.bottom = SpViewInterpolate(second.bottom, first.bottom, second.bottom);
  second.left = ScIntMax(second.left, 0);
  second.top = ScIntMax(second.top, 0);
  second.right = ScIntMin(second.right, width);
  second.bottom = ScIntMin(second.bottom, height);
  GeBounds region = {{(i32)((u32)second.left << 8), (i32)((u32)second.top << 8), 0},
      {(i32)((u32)second.right << 8), (i32)((u32)second.bottom << 8), 0}};
  data->SetInfo(region);
  u32 scale = multiplier;
  focus.x = (i32)(scale * (u32)focus_x);
  focus.z = (i32)(scale * (u32)focus_z);
  data->SetInfo(focus);
  city->LockGraphics();
  cell_map->GetTilingOriginWindowPixel(&data->tiling_x, &data->tiling_y);
  city->UnlockGraphics();
  *out = data;
  return 1;
}

// Save focus as floats followed by zoom and rotation through the actual versioned record scope.
// Failed writes abort the record through the original SetFailed path; the scope closes it on destruction.
bool SpView::Save(CtCity *source_city, RsDatabaseSegment *segment) {
  TrRecordFrame frame(source_city, segment, RsKey(kSpViewRecordType, kSpViewRecordGroup, 0), 1, false);
  bool good = frame.IsValid() != 0;
  RsStream *stream = frame.GetDBSerialRecordPointer();
  if (good) {
    good = stream->WriteF32((f32)focus_x);
    good = good && stream->WriteF32((f32)focus_z) && stream->WriteI32(cell_map->GetZoom()) &&
        stream->WriteI32(cell_map->GetRotate());
  }
  if (!good)
    frame.SetFailed();
  return good;
}

// Restore the saved focus, zoom and rotation, including optional debug delimiters and the original event ordering.
// Version zero skips the payload; actual read failures abort through the real record scope.
bool SpView::Init(CtCity *source_city, RsDatabaseSegment *segment) {
  TrRecordFrame frame(source_city, segment, RsKey(kSpViewRecordType, kSpViewRecordGroup, 0));
  bool good = frame.IsValid() != 0;
  RsStream *stream = nullptr;
  if (good) {
    stream = frame.GetDBSerialRecordPointer();
    good = stream != nullptr;
  }
  if (frame.RecordVersion()) {
    // Seed only for partial external readers that report success without writing their required outputs.
    f32 saved_x = 0, saved_z = 0;
    i32 zoom = 0, rotation = 0;
    if (good && frame.IsDebug()) {
      ScString delimiter;
      good = stream->ReadString(&delimiter);
    }
    good = good && stream->ReadF32(&saved_x) && stream->ReadF32(&saved_z) && stream->ReadI32(&zoom) &&
        stream->ReadI32(&rotation);
    if (good && frame.IsDebug()) {
      ScString delimiter;
      good = stream->ReadString(&delimiter);
    }
    // Exclude undefined float-to-integer conversion at this external saved-record boundary.
    if (good &&
        (!std::isfinite(saved_x) || !std::isfinite(saved_z) || (f64)saved_x < -0x1p63 || (f64)saved_x >= 0x1p63 ||
            (f64)saved_z < -0x1p63 || (f64)saved_z >= 0x1p63))
      good = false;
    if (good) {
      i32 current_zoom = cell_map->GetZoom(), current_rotation = cell_map->GetRotate();
      SetNotify(true);
      if (zoom == current_zoom && rotation == current_rotation) {
        FocusCameraOn((u32)(i64)saved_x, (u32)(i64)saved_z, true);
        NotifyClients(kWnMsg_CameraChanging, (u32)zoom, (u32)rotation);
      } else {
        FocusCameraOn((u32)(i64)saved_x, (u32)(i64)saved_z, false);
        SetZoomAndRotation(zoom, rotation, true);
      }
      initialized = 1;
    } else
      frame.SetFailed();
  }
  return good;
}

// Send before-change events synchronously; queue the normal after-change/focus/selection events on the captured server.
// Other enabled notifications still produce a fresh view snapshot, which travels as a real pointer in the final message.
void SpView::NotifyClients(u32 message, u32 first, u32 second) {
  if (!events_enabled)
    return;
  ScMessageServer *server = SpGetMessageServer();
  if (message == kWnMsg_CameraChanging || message == kWnMsg_Unresolved34) {
    server->MessageSend({message, first, second, 0});
    return;
  }
  switch (message) {
  case kWnMsg_CameraChanged:
    server->MessagePost({message, first, second, 0}, false);
    break;
  case kWnMsg_CameraFocusChanged:
    server->MessagePost({message, (u32)focus_x, (u32)focus_z, 0}, false);
    break;
  case kWnMsg_CityDataViewChanged: {
    u32 id = (u32)GetCityViewID();
    u32 flags = GetCityViewFlags();
    server->MessagePost({message, id, flags, 0}, false);
    break;
  }
  }
  SpViewInfo *view;
  if (GetViewInfo(&view))
    server->MessageSend({kWnMsg_ViewInfoUpdated, kSpViewUpdate_Snapshot, (uptr)view, 0});
}

bool SpView::GetCityViewFlag(u32 mask) const {
  return state->GetCityViewFlag(mask);
}

void SpView::SetCityViewFlag(u32 mask, bool enabled, bool update) {
  state->SetCityViewFlag(mask, enabled, update);
  NotifyClients(kWnMsg_CityDataViewChanged, 0, 0);
}

void SpView::SetCityViewFlags(u32 flags, bool update) {
  state->SetCityViewFlags(flags, update);
  NotifyClients(kWnMsg_CityDataViewChanged, 0, 0);
}

bool SpView::TestCityViewFlags(u32 mask) const {
  return state->TestCityViewFlags(mask);
}

void SpView::ClearCityViewFlags() {
  state->ClearCityViewFlags();
}
