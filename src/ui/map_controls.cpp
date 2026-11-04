#include "ui/map_controls.h"

#include <assert.h>
#include <math.h>

#include "city/api.h"
#include "common/api.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "ui/api.h"
#include "ui/ui_module.h"
#include "window/api.h"

UiMapBufferFilter::UiMapBufferFilter(u32 id, GxBuffer *source) : references(0), identifier(id), buffer(source) {
  buffer->AddRef();
}

UiMapBufferFilter::~UiMapBufferFilter() {
  buffer->Release();
}

// Publish either supported identity and retain it after the output write.
bool UiMapBufferFilter::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiMapFilter);
  HANDLE_QUERY_INTERFACE(IID_UiMapFilter, UiMapFilter);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiMapBufferFilter::AddRef() {
  return ++references;
}

// The original destroys the filter when its count is zero or one.
u32 UiMapBufferFilter::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

void UiMapBufferFilter::SetFilterId(u32 id) {
  identifier = id;
}

u32 UiMapBufferFilter::GetFilterId() {
  return identifier;
}

u32 UiMapBufferFilter::GetWidth() {
  return buffer->IsReady() ? buffer->Width() : 0;
}

u32 UiMapBufferFilter::GetHeight() {
  return buffer->IsReady() ? buffer->Height() : 0;
}

u8 UiMapBufferFilter::SetVisible(bool enabled) {
  visible = enabled;
  return visible;
}

u8 UiMapBufferFilter::IsVisible() {
  return visible;
}

u8 UiMapBufferFilter::SetPalette(GxPaletteView *) {
  return 0;
}

// This filter does not publish a separate palette.
GxPaletteView *UiMapBufferFilter::GetPalette() {
  return nullptr;
}

u8 UiMapBufferFilter::Draw(GxBuffer *destination, ScRect *area, ScRect *clip) {
  if (buffer->IsReady() && destination->Lock(GxLockFlags::Blt)) {
    const ScRect *source_area = buffer->GetBufferArea();
    destination->BltStretch(buffer, *source_area, *area, clip);
    destination->Unlock(GxLockFlags::Blt);
  }
  return 0;
}

u8 UiMapBufferFilter::CreateBuffer(u32, u32, GxBuffer **) {
  return 0;
}

u8 UiMapBufferFilter::CloneFilter(UiMapFilter **) {
  assert(buffer != nullptr);
  return 0;
}

void UiMapBufferFilter::SetPostFilter(bool) {
}

void UiMapBufferFilter::SetDestinationCoords(i32, i32, i32, i32, i32, i32) {
}

u8 UiMapBufferFilter::IsPostFilter() const {
  return 0;
}

// Construct the SIMUI map control and return its original secondary map-control identity without adding a reference.
// Candidate: the complete constructor and rendering lifecycle still require installed-code comparison.
ScUnknown *UiCreateClass000005A9() {
  return static_cast<UiMapControl *>(new UiMapControlObject);
}

// The fused residual distinguishes the side of the midpoint that a plain double intermediate can lose.
static f32 UiMapRotationRadians(i32 degrees) {
  const f64 pi = (f64)3.1415927f, scale = 1.0 / 180.0;
  f64 first = (f64)degrees * pi, first_error = fma((f64)degrees, pi, -first);
  f64 value = first * scale, error = fma(first, scale, -value) + first_error * scale;
  f32 result = (f32)value;
  if (error != 0.0 && (f64)result != value) {
    f32 other = nextafterf(result, (f64)result < value ? INFINITY : -INFINITY);
    if (((f64)result + (f64)other) * 0.5 == value) {
      int exponent = 0;
      frexp(value, &exponent);
      f64 half_x87_ulp = ldexp(1.0, exponent - 65);
      if (fabs(error) > half_x87_ulp && ((error > 0.0 && (f64)result < value) ||
          (error < 0.0 && (f64)result > value)))
        result = other;
    }
  }
  return result;
}

bool UiMapControlObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiMapControl, UiMapControl);
  return UiBaseWindow::QueryInterface(iid, out);
}

// Fit a positive requested area to an odd-sided square and flag changed rotation geometry.
// The original checks the live height before width and calls the base rectangle setter directly.
void UiMapControlObject::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width <= 0 || height <= 0)
    return;
  if (!(width & 1))
    width = (i32)((u32)width + 1);
  if (!(height & 1))
    height = (i32)((u32)height + 1);
  i32 side = width < height ? width : height;
  if (!rotation_dirty)
    rotation_dirty = side != GetH() || side != GetW();
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + (u32)side), (i32)((u32)top + (u32)side));
}

u32 UiMapControlObject::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiMapControlObject::Release() {
  return UiBaseWindow::Release();
}

u8 UiMapControlObject::CreateControl(u32 control_id) {
  UiBaseWindow::SetID(control_id);
  return 1;
}

// Mark both the image and rotated coordinates for refresh before the next paint.
u8 UiMapControlObject::Prepare() {
  rotation_dirty = true;
  image_dirty = true;
  return 1;
}

u8 UiMapControlObject::Unprepare() {
  ReleaseMapBuffer();
  return 1;
}

// Return the borrowed primary window identity without acquiring another reference.
WnWindow *UiMapControlObject::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Mark the transformed geometry dirty and invalidate only when the requested angle changes.
u8 UiMapControlObject::SetRotation(i32 degrees) {
  if (rotation != degrees) {
    rotation = degrees;
    radians = UiMapRotationRadians(degrees);
    rotation_dirty = true;
    InvalidateSelfAndParents();
  }
  return 1;
}

i32 UiMapControlObject::GetRotation() {
  return rotation;
}

u8 UiMapControlObject::SetDrawWindowOutline(bool enabled) {
  window_outline = enabled;
  InvalidateSelfAndParents();
  return 1;
}

u8 UiMapControlObject::IsDrawWindowOutline() {
  return window_outline;
}

u8 UiMapControlObject::SetWindowOutlineColors(u32 first, u32 second) {
  window_first = first;
  window_second = second;
  InvalidateSelfAndParents();
  return 1;
}

u8 UiMapControlObject::SetDrawImageOutline(bool enabled) {
  image_outline = enabled;
  InvalidateSelfAndParents();
  return 1;
}

u8 UiMapControlObject::IsDrawImageOutline() {
  return image_outline;
}

u8 UiMapControlObject::SetImageOutlineColors(u32 first, u32 second) {
  image_first = first;
  image_second = second;
  InvalidateSelfAndParents();
  return 1;
}

u32 UiMapControlObject::GetFillColor() {
  return UiBaseWindow::GetFillColor();
}

void UiMapControlObject::SetFillColor(u32 pixel) {
  if (word204 != pixel) {
    UiBaseWindow::SetFillColor(pixel);
    image_dirty = true;
    InvalidateSelfAndParents();
  }
}

// Change the selected forwarding bits, then publish the borrowed message target.
u8 UiMapControlObject::SetCoordinateForwarding(u32 mask, bool enabled, ScMessageTarget *target) {
  if (enabled)
    forwarding_flags |= mask;
  else
    forwarding_flags &= ~mask;
  forwarding_target = target;
  return 1;
}

u32 UiMapControlObject::GetCoordinateForwardingFlags() {
  return forwarding_flags;
}

// Store both unsigned map dimensions in the source's eight-bit fixed-point representation.
u8 UiMapControlObject::SetActualMapSizeInCells(u32 width, u32 height) {
  map_width = width << 8;
  map_height = height << 8;
  return 1;
}

// Return both map dimensions by removing the source's eight fractional bits.
u8 UiMapControlObject::GetActualMapSizeInCells(u32 *width, u32 *height) {
  *width = map_width >> 8;
  *height = map_height >> 8;
  return 1;
}

void UiMapControlObject::DirtyCellRegion(ScRect *) {
  image_dirty = true;
  InvalidateSelfAndParents();
}

// Clone the control's defined state, palettes and individually cloneable filters into a new retained window.
u8 UiMapControlObject::CloneControl(u32 control_id, UiMapControl **out) {
  UiMapControlObject *copy = new UiMapControlObject;
  copy->AddRef();
  copy->rotation = rotation;
  copy->radians = radians;
  copy->window_outline = window_outline;
  copy->window_first = window_first;
  copy->window_second = window_second;
  copy->image_outline = image_outline;
  copy->image_first = image_second;
  copy->word204 = word204;
  copy->image_dirty = true;
  copy->scale = 0.0f;
  copy->rotation_dirty = true;
  copy->word228 = 0;
  copy->buffer = nullptr;
  copy->map_width = map_width;
  copy->map_height = map_height;
  copy->forwarding_flags = 0;
  for (u32 i = 0; i < 6; ++i)
    copy->words248[i] = words248[i];
  UiMapPaletteNode *palette_end = palette_head;
  for (UiMapPaletteNode *node = palette_end->next; node != palette_end; node = node->next)
    copy->AddPalette(node->id, node->palette);
  UiMapFilterNode *filter_end = filter_head;
  for (UiMapFilterNode *node = filter_end->next; node != filter_end; node = node->next) {
    UiMapFilter *filter = nullptr;
    if (node->filter->CloneFilter(&filter) && filter) {
      copy->AddFilterBack(filter);
      filter->Release();
    }
  }
  copy->SetID(control_id);
  *out = static_cast<UiMapControl *>(copy);
  return 1;
}

// Create the source-sized RGB16 buffer, retaining failed-init storage.
// A successful fill lock initializes it to the graphics service's native gray before the matching unlock.
u8 UiMapControlObject::CreateMapBuffer() {
  UiGetGraphics()->CreateBuffer(&buffer);
  if (!buffer)
    return 0;
  GxColorType color(GxColorType::Color::RGB16);
  GxDepthType depth(GxDepthType::Depth::None);
  if (!buffer->Init(word228, word228, color, nullptr, depth, GxVramRequest::SystemMemory, false))
    return 0;
  if (buffer->Lock(GxLockFlags::Fill)) {
    u16 gray = UiGetGraphics()->ConvertRGBValueToNative16Bit(128, 128, 128);
    buffer->Fill(gray, (ScRect *)nullptr);
    buffer->Unlock(GxLockFlags::Fill);
  }
  return 1;
}

// Release the live image before resetting its size and both dirty flags.
u8 UiMapControlObject::ReleaseMapBuffer() {
  ScRelease(&buffer);
  word228 = 0;
  image_dirty = true;
  rotation_dirty = true;
  return 1;
}

// Draw every visible non-post filter into the current buffer in list order.
// A missing buffer leaves the dirty flag unchanged; filter callbacks may observe or replace the live buffer member.
u8 UiMapControlObject::UpdateBufferImage() {
  if (!buffer)
    return 0;
  ScRect rectangle = *buffer->GetBufferArea();
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next)
    if (node->filter->IsVisible() && !node->filter->IsPostFilter())
      node->filter->Draw(buffer, &rectangle, nullptr);
  image_dirty = false;
  return 1;
}

// The source reads the live window rectangle and changes the six post-filter coordinates only for four rotations.
void UiMapControlObject::RecalculatePostFilterCoordinatesEffects() {
  ScRect rectangle = *GetArea();
  i32 span = (i32)((u32)rectangle.right - (u32)rectangle.left - (window_outline ? 2u : 0u) - 1u);
  i32 half = span / 2;
  switch (rotation) {
  case 45:
    words248[0] = 0;
    words248[3] = 0;
    words248[1] = (u32)half;
    words248[2] = (u32)half;
    words248[4] = (u32)half;
    words248[5] = (u32)span;
    break;
  case 135:
    words248[1] = (u32)span;
    words248[2] = 0;
    words248[0] = (u32)half;
    words248[3] = (u32)half;
    words248[4] = (u32)span;
    words248[5] = (u32)half;
    break;
  case 225:
    words248[0] = (u32)span;
    words248[3] = (u32)span;
    words248[1] = (u32)half;
    words248[2] = (u32)half;
    words248[4] = (u32)half;
    words248[5] = 0;
    break;
  case 315:
    words248[1] = 0;
    words248[2] = (u32)span;
    words248[0] = (u32)half;
    words248[3] = (u32)half;
    words248[4] = 0;
    words248[5] = (u32)half;
    break;
  }
}

// Recreate the map buffer only when the diagonal crosses a source size threshold.
// Refresh post-filter coordinates and clear the rotation-dirty byte after preserving the original image callbacks.
u8 UiMapControlObject::UpdateBufferSize() {
  ScRect rectangle = *GetArea();
  i32 width = (i32)((u32)rectangle.right - (u32)rectangle.left - (window_outline ? 2u : 0u));
  f64 diagonal = (f64)width * (f64)0.70710677f;
  f32 stored_diagonal = (f32)diagonal;
  u32 new_size = diagonal <= 32.0 ? 32 : diagonal <= 64.0 ? 64 : diagonal <= 128.0 ? 128 : 256;
  if (new_size != word228) {
    ReleaseMapBuffer();
    word228 = new_size;
    CreateMapBuffer();
    image_dirty = true;
    scale = (f32)((f64)stored_diagonal / (f64)new_size);
  }
  RecalculatePostFilterCoordinatesEffects();
  rotation_dirty = false;
  return 1;
}

// Refresh derived map images, draw the optional frame and rotated image,
// then draw visible post-filters using the current six destination coordinates.
bool UiMapControlObject::GZPaint() {
  ScRect rectangle = draw_area;
  if (rotation_dirty)
    UpdateBufferSize();
  if (image_dirty)
    UpdateBufferImage();
  if (IsDrawWindowOutline()) {
    if (draw_buffer->Lock(GxLockFlags::Line)) {
      draw_buffer->DrawLine(rectangle.left, rectangle.top, rectangle.right, rectangle.top, (i32)window_first, 1);
      draw_buffer->DrawLine(rectangle.left, rectangle.top, rectangle.left, rectangle.bottom, (i32)window_first, 1);
      draw_buffer->DrawLine(rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom, (i32)window_second, 1);
      draw_buffer->DrawLine(rectangle.right, rectangle.top, rectangle.right, rectangle.bottom, (i32)window_second, 1);
      draw_buffer->Unlock(GxLockFlags::Line);
    }
    rectangle.left = (i32)((u32)rectangle.left + 1);
    rectangle.right = (i32)((u32)rectangle.right - 1);
    rectangle.top = (i32)((u32)rectangle.top + 1);
    rectangle.bottom = (i32)((u32)rectangle.bottom - 1);
  }
  if (draw_buffer->Lock(GxLockFlags::BltRotate)) {
    assert(buffer != nullptr);
    const ScRect *source_area = buffer->GetBufferArea();
    draw_buffer->RotateBltStretch(buffer, *source_area, rectangle, radians, scale, nullptr);
    draw_buffer->Unlock(GxLockFlags::BltRotate);
  }
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next) {
    if (node->filter->IsVisible() && node->filter->IsPostFilter()) {
      node->filter->SetDestinationCoords(
          (i32)words248[0], (i32)words248[1], (i32)words248[2], (i32)words248[3], (i32)words248[4], (i32)words248[5]);
      node->filter->Draw(draw_buffer, &rectangle, nullptr);
    }
  }
  return true;
}

// Transform packed screen-window coordinates to fixed-point map coordinates.
u8 UiMapControlObject::TranslateWindowCoord(UiMapPoint point, UiMapPoint *out) const {
  f32 width = (f32)map_width;
  i32 window_width = GetW();
  assert(window_width != 0);
  f32 factor = (f32)((f64)width / (f64)window_width);
  i32 center = GetW() / 2;
  i32 relative = (i32)((u32)(i32)point.x - (u32)center);
  f64 horizontal_value = (f64)relative * factor, vertical_value = (f64)point.y * factor;
  assert(horizontal_value >= -9223372036854775808.0 && horizontal_value < 9223372036854775808.0 &&
      vertical_value >= -9223372036854775808.0 && vertical_value < 9223372036854775808.0);
  i32 horizontal = (i32)(u32)(i64)horizontal_value, vertical = (i32)(u32)(i64)vertical_value;
  i32 difference = (i32)((u32)vertical - (u32)horizontal);
  i32 sum = (i32)((u32)vertical + (u32)horizontal);
  i32 x, y;
  switch (rotation) {
    case 45:
      x = (i32)(map_width - (u32)difference - 1);
      y = (i32)(map_height - (u32)sum - 1);
      break;
    case 135:
      x = (i32)(map_height - (u32)sum - 1);
      y = difference;
      break;
    case 225:
      x = difference;
      y = sum;
      break;
    case 315:
      x = sum;
      y = (i32)(map_width - (u32)difference - 1);
      break;
    default:
      return 0;
  }
  if (x < 0 || y < 0 || x >= (i32)map_width || y >= (i32)map_height)
    return 0;
  out->x = (i16)(x >> 8);
  out->y = (i16)(y >> 8);
  return 1;
}

// Translate coordinates and broadcast the pointer-identity packet or post the ordinary window message.
// The selector borrows the target identity without retaining it; payload coordinates remain packed DWORDs.
u8 UiMapControlObject::DoWindowCoordMessage(u32 type, i16 x, i16 y) {
  UiMapPoint point{x, y}, translated;
  if (!TranslateWindowCoord(point, &translated))
    return 0;
  u32 local = (u16)point.x | ((u32)(u16)point.y << 16);
  u32 map = (u16)translated.x | ((u32)(u16)translated.y << 16);
  if (forwarding_target) {
    uptr target = reinterpret_cast<uptr>(forwarding_target);
    ScMessage message(target, type, map, local);
    UiGetMessageServer()->MessagePost(message, false);
    return 0;
  }
  return PostMsg(notification_target, type, map, local, 0);
}

// Forward enabled left-button presses and capture only after successful posting.
bool UiMapControlObject::OnMouseDownL(i32 x, i32 y, u32) {
  if (!(forwarding_flags & 1) || !DoWindowCoordMessage(kWnMsg_MapMouseDownL, (i16)x, (i16)y))
    return false;
  GZSetCapture();
  return true;
}

// Forward enabled right-button presses and capture only after successful posting.
bool UiMapControlObject::OnMouseDownR(i32 x, i32 y, u32) {
  if (!(forwarding_flags & 4) || !DoWindowCoordMessage(kWnMsg_MapMouseDownR, (i16)x, (i16)y))
    return false;
  GZSetCapture();
  return true;
}

// Forward enabled left releases and release any capture after the attempted post.
bool UiMapControlObject::OnMouseUpL(i32 x, i32 y, u32) {
  u8 result = 1;
  if (forwarding_flags & 2)
    result = DoWindowCoordMessage(kWnMsg_MapMouseUpL, (i16)x, (i16)y);
  if (GZAmCapturing())
    GZReleaseCapture();
  return result != 0;
}

// Forward enabled right releases and release any capture after the attempted post.
bool UiMapControlObject::OnMouseUpR(i32 x, i32 y, u32) {
  u8 result = 1;
  if (forwarding_flags & 8)
    result = DoWindowCoordMessage(kWnMsg_MapMouseUpR, (i16)x, (i16)y);
  if (GZAmCapturing())
    GZReleaseCapture();
  return result != 0;
}

bool UiMapControlObject::OnMouseMove(i32 x, i32 y, u32) {
  return (forwarding_flags & 16) && DoWindowCoordMessage(kWnMsg_MapMouseMove, (i16)x, (i16)y);
}

bool UiMapControlObject::GZReleaseCapture() {
  PostMsg(notification_target, kWnMsg_MapCaptureReleased, 0, 0, 0);
  return WnRootWidget::GZReleaseCapture();
}

bool UiMapControlObject::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiMapControlObject::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Use ordinary window hit testing unless map-coordinate forwarding is enabled.
bool UiMapControlObject::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  if (!forwarding_flags)
    return WnRootWidget::IsPointInWindowScreenCoordinates(x, y);
  ScreenToWindowCoordinates(&x, &y);
  UiMapPoint point{(i16)x, (i16)y}, translated;
  return TranslateWindowCoord(point, &translated) != 0;
}

// Initialize both circular lists and the defined map state before configuring colors.
// The borrowed forwarding target and the six later coordinate words remain untouched until their source producers run.
UiMapControlObject::UiMapControlObject()
    : word168(0), rotation(0), radians(0), window_outline(false), window_first(0), window_second(0),
      image_outline(false), image_first(0), image_second(0), word204(0), forwarding_flags(0), map_width(0),
      map_height(0), image_dirty(true), rotation_dirty(true), scale(0), word228(32), buffer(nullptr) {
  palette_head = new UiMapPaletteNode;
  palette_head->next = palette_head;
  palette_head->previous = palette_head;
  filter_head = new UiMapFilterNode;
  filter_head->next = filter_head;
  filter_head->previous = filter_head;
  UiMapControlObject::SetArea(0, 0, 64, 64);
  u32 fill = UiGetWindowResources()->GetColor(kWnThemeBackgroundColorId);
  UiMapControlObject::SetFillColor(fill);
  u32 second = UiBaseWindow::MakeFillColor(255, 242, 255);
  u32 first = UiBaseWindow::MakeFillColor(127, 136, 255);
  UiMapControlObject::SetWindowOutlineColors(first, second);
  second = UiBaseWindow::MakeFillColor(255, 242, 255);
  first = UiBaseWindow::MakeFillColor(127, 136, 255);
  UiMapControlObject::SetImageOutlineColors(first, second);
  UiMapControlObject::SetRotation(225);
  if (UiGetCity()) {
    CtCity *first_city = UiGetCity(), *second_city = UiGetCity();
    u32 height = first_city->CellCountX(), width = second_city->CellCountX();
    UiMapControlObject::SetActualMapSizeInCells(width, height);
  }
}

// Release both retained collections before retiring their circular sentinels.
UiMapControlObject::~UiMapControlObject() {
  UiMapControlObject::DestroyControl();
  delete filter_head;
  delete palette_head;
}

bool UiMapControlObject::Init() {
  return GetFlag(kWnFlagInitialized) || UiBaseWindow::Init();
}

bool UiMapControlObject::Shutdown() {
  Unprepare();
  return UiBaseWindow::Shutdown();
}

u32 UiMapControlObject::SetFlag(u32 mask, bool enabled) {
  return UiBaseWindow::SetFlag(mask, enabled);
}

u8 UiMapControlObject::DestroyControl() {
  ReleaseAllPalettes();
  ReleaseAllFilters();
  return 1;
}

// Publish the first matching palette and retain it after the output write.
u8 UiMapControlObject::GetPalette(u32 id, GxPaletteView **out) {
  UiMapPaletteNode *head = palette_head;
  for (UiMapPaletteNode *node = head->next; node != head; node = node->next) {
    if (node->id == id) {
      *out = node->palette;
      node->palette->AddRef();
      return 1;
    }
  }
  return 0;
}

// Reject a null or duplicate palette, then append and retain the supplied view.
u8 UiMapControlObject::AddPalette(u32 id, GxPaletteView *palette) {
  if (!palette)
    return 0;
  GxPaletteView *previous = nullptr;
  if (GetPalette(id, &previous)) {
    previous->Release();
    return 0;
  }
  UiMapPaletteNode *head = palette_head, *node = new UiMapPaletteNode{head, head->previous, id, palette};
  head->previous->next = node;
  head->previous = node;
  palette->AddRef();
  return 1;
}

// Release and erase only the first palette with the requested key.
u8 UiMapControlObject::ReleasePalette(u32 id) {
  UiMapPaletteNode *head = palette_head;
  for (UiMapPaletteNode *node = head->next; node != head; node = node->next) {
    if (node->id == id) {
      node->palette->Release();
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
      return 1;
    }
  }
  return 0;
}

// Release every palette before clearing the existing circular list.
u8 UiMapControlObject::ReleaseAllPalettes() {
  UiMapPaletteNode *head = palette_head;
  for (UiMapPaletteNode *node = head->next; node != head; node = node->next) {
    node->palette->Release();
    node->palette = nullptr;
  }
  head = palette_head;
  UiMapPaletteNode *node = head->next;
  while (node != head) {
    UiMapPaletteNode *next = node->next;
    delete node;
    node = next;
  }
  head->next = head;
  head->previous = head;
  return 1;
}

// Count list entries by walking the original circular order.
u32 UiMapControlObject::GetFilterCount() {
  u32 count = 0;
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next)
    ++count;
  return count;
}

// The installed body visits each visibility getter and returns one.
u32 UiMapControlObject::GetVisibleFilterCount() {
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next)
    node->filter->IsVisible();
  return 1;
}

// Return the first borrowed filter whose live identifier matches.
UiMapFilter *UiMapControlObject::GetFilter(u32 id) {
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next)
    if (node->filter->GetFilterId() == id)
      return node->filter;
  return nullptr;
}

// Publish and retain the first matching filter without modifying a failed output.
u8 UiMapControlObject::GetFilter(u32 id, UiMapFilter **out) {
  UiMapFilter *filter = GetFilter(id);
  if (!filter)
    return 0;
  filter->AddRef();
  *out = filter;
  return 1;
}

// Count first, then return the borrowed filter at the requested list position.
UiMapFilter *UiMapControlObject::GetFilterByIndex(u32 index) {
  if (index >= GetFilterCount())
    return nullptr;
  UiMapFilterNode *node = filter_head->next;
  while (index--)
    node = node->next;
  return node->filter;
}

// Mark a previously hidden filter dirty, then request its visible state.
u8 UiMapControlObject::ShowFilter(u32 id) {
  UiMapFilter *filter = GetFilter(id);
  if (!filter)
    return 0;
  if (!filter->IsVisible())
    image_dirty = true;
  filter->SetVisible(true);
  return 1;
}

// Visit each filter in list order and mark any visibility change dirty.
u8 UiMapControlObject::ShowAllFilters() {
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next) {
    if (!node->filter->IsVisible())
      image_dirty = true;
    node->filter->SetVisible(true);
  }
  return 1;
}

// Mark a visible filter dirty before requesting its hidden state.
u8 UiMapControlObject::HideFilter(u32 id) {
  UiMapFilter *filter = GetFilter(id);
  if (!filter)
    return 0;
  if (filter->IsVisible())
    image_dirty = true;
  filter->SetVisible(false);
  return 1;
}

// Visit each filter in list order and mark any visibility change dirty.
u8 UiMapControlObject::HideAllFilters() {
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next) {
    if (node->filter->IsVisible())
      image_dirty = true;
    node->filter->SetVisible(false);
  }
  return 1;
}

u8 UiMapControlObject::AddFilter(UiMapFilter *filter) {
  return AddFilterBack(filter);
}

// Wrap a non-null raster source only when its live identifier is not present.
u8 UiMapControlObject::AddFilter(GxRasterSource *source) {
  if (!source)
    return 0;
  if (GetFilter(source->GetId()))
    return 0;
  UiMapSpriteFilter *filter = new UiMapSpriteFilter(source, source->GetPalette());
  filter->AddRef();
  u8 result = AddFilter(filter);
  filter->Release();
  return result;
}

// Wrap a non-null buffer only when its filter identifier is not already present.
u8 UiMapControlObject::AddFilter(u32 id, GxBuffer *source) {
  if (GetFilter(id) || !source)
    return 0;
  UiMapBufferFilter *filter = new UiMapBufferFilter(id, source);
  filter->AddRef();
  u8 result = AddFilter(filter);
  filter->Release();
  return result;
}

u8 UiMapControlObject::AddFilterFront(UiMapFilter *filter) {
  filter->SetVisible(true);
  filter->AddRef();
  UiMapFilterNode *after = filter_head->next, *node = new UiMapFilterNode{after, after->previous, filter};
  after->previous->next = node;
  after->previous = node;
  image_dirty = true;
  InvalidateSelfAndParents();
  return 1;
}

u8 UiMapControlObject::AddFilterBack(UiMapFilter *filter) {
  filter->SetVisible(true);
  filter->AddRef();
  UiMapFilterNode *after = filter_head, *node = new UiMapFilterNode{after, after->previous, filter};
  after->previous->next = node;
  after->previous = node;
  image_dirty = true;
  InvalidateSelfAndParents();
  return 1;
}

// Release and remove only the first matching filter, retaining callback order.
u8 UiMapControlObject::ReleaseFilter(u32 id) {
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next) {
    if (node->filter->GetFilterId() == id) {
      image_dirty = true;
      node->filter->Release();
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
      return 1;
    }
  }
  return 0;
}

// Release all retained filters before removing their nodes and marking dirty.
u8 UiMapControlObject::ReleaseAllFilters() {
  UiMapFilterNode *head = filter_head;
  for (UiMapFilterNode *node = head->next; node != head; node = node->next) {
    node->filter->Release();
    node->filter = nullptr;
  }
  head = filter_head;
  UiMapFilterNode *node = head->next;
  while (node != head) {
    UiMapFilterNode *next = node->next;
    delete node;
    node = next;
  }
  head->next = head;
  head->previous = head;
  image_dirty = true;
  return 1;
}

UiMapSprite::UiMapSprite() : references(0), kind(0), palette(nullptr), width(0), height(0), enabled(0) {
  UiMapSprite::SetDestinationCoords(0, 0, 0, 1, 1, 0);
}

UiMapSprite::~UiMapSprite() {
  ScRelease(&palette);
}

// Publish the primary identity for Unknown and the map-sprite IID, retaining it after the output write.
bool UiMapSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxRasterSource);
  HANDLE_QUERY_INTERFACE(IID_GxRasterSource, GxRasterSource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiMapSprite::AddRef() {
  return ++references;
}

u32 UiMapSprite::Release() {
  return ScReleaseImpl(this, &references);
}

// Publish the supplied ID and dimensions through the original virtual setters.
bool UiMapSprite::Init(u32 id, u32 x, u32 z) {
  SetId(id);
  SetBaseSize(x, z);
  return true;
}

u32 UiMapSprite::GetId() const {
  return kind;
}

bool UiMapSprite::SetId(u32 id) {
  kind = id;
  return true;
}

// Retain the incoming palette before releasing and replacing the existing palette.
bool UiMapSprite::SetPalette(GxPaletteView *source) {
  if (source)
    source->AddRef();
  ScRelease(&palette);
  palette = source;
  return true;
}

// Borrow the currently retained palette without adding another reference.
GxPaletteView *UiMapSprite::GetPalette() {
  return palette;
}

i32 UiMapSprite::GetBaseWidth() const {
  return width;
}

i32 UiMapSprite::GetBaseHeight() const {
  return width;
}

// Store both source dimensions using their original DWORD bit patterns.
bool UiMapSprite::SetBaseSize(u32 x, u32 z) {
  width = (i32)x;
  height = (i32)z;
  return true;
}

u8 UiMapSprite::SetPostFilter(u8 value) {
  enabled = value;
  return value;
}

// Store the three source corners and derive the remaining projected corner.
void UiMapSprite::SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) {
  corner_x[1] = x1;
  corner_x[0] = x0;
  corner_z[1] = z1;
  corner_x[2] = (i32)((u32)x3 + (u32)x1 - (u32)x0);
  corner_z[0] = z0;
  corner_x[3] = x3;
  corner_z[2] = (i32)((u32)z3 + (u32)z1 - (u32)z0);
  corner_z[3] = z3;
}

u8 UiMapSprite::IsPostFilter() const {
  return enabled;
}

bool UiMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  if (!destination || !DrawCells(destination, rectangle, clip))
    return false;
  return DrawAnnotations(destination, rectangle, clip);
}

u8 UiMapSprite::DebugInstanceTag(ScStringView *) {
  return 0;
}

u8 UiMapSprite::DebugClassTag(ScStringView *) {
  return 0;
}

u8 UiMapSprite::DebugTypeTag(ScStringView *) {
  return 0;
}

bool UiMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

bool UiMapSprite::DrawAnnotations(GxBuffer *, const ScRect &, ScRect *) {
  return true;
}

u8 UiMapSprite::SetDebugInstanceTag(const ScStringView &) {
  return 0;
}

u8 UiMapSprite::SetDebugClassTag(const ScStringView &) {
  return 0;
}

// Scale the destination columns and rows into source cells, vertically flip the source and honor the supplied clip.
bool UiMapSprite::DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  i32 span_x = (i32)((u32)rectangle.right - (u32)rectangle.left);
  i32 span_z = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
  i32 left = 0, top = 0, right = span_x, bottom = span_z;
  if (clip) {
    i32 x0 = (i32)((u32)clip->left - (u32)rectangle.left), z0 = (i32)((u32)clip->top - (u32)rectangle.top);
    i32 x1 = (i32)((u32)clip->right - (u32)rectangle.left), z1 = (i32)((u32)clip->bottom - (u32)rectangle.top);
    if (x0 > left)
      left = x0;
    if (z0 > top)
      top = z0;
    if (x1 < right)
      right = x1;
    if (z1 < bottom)
      bottom = z1;
  }
  u32 pixel = 0;
  i32 last_z = (i32)((u32)height - 1);
  for (i32 x = left; x < right; ++x) {
    i32 source_x = (i32)((u32)x * (u32)width) / span_x;
    for (i32 z = top; z < bottom; ++z) {
      i32 source_z = (i32)((u32)z * (u32)height) / span_z;
      if (GetColor((u32)source_x, (u32)last_z - (u32)source_z, &pixel))
        destination->SetPixel((u32)x + (u32)rectangle.left, (u32)z + (u32)rectangle.top, pixel);
    }
  }
  return true;
}

// Retain the source before forwarding the supplied palette to it.
UiMapSpriteFilter::UiMapSpriteFilter(GxRasterSource *source, GxPaletteView *palette)
    : references(0), sprite(source), visible(1) {
  sprite->AddRef();
  sprite->SetPalette(palette);
}

UiMapSpriteFilter::~UiMapSpriteFilter() {
  sprite->Release();
  sprite = nullptr;
}

// Publish either supported identity and retain it after the output write.
bool UiMapSpriteFilter::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiMapFilter);
  HANDLE_QUERY_INTERFACE(IID_UiMapFilter, UiMapFilter);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiMapSpriteFilter::AddRef() {
  return ++references;
}

// Destroy the source owner when its count is zero or one.
u32 UiMapSpriteFilter::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

void UiMapSpriteFilter::SetFilterId(u32) {
}

u32 UiMapSpriteFilter::GetFilterId() {
  return sprite->GetId();
}

u32 UiMapSpriteFilter::GetWidth() {
  return (u32)sprite->GetBaseWidth();
}

u32 UiMapSpriteFilter::GetHeight() {
  return (u32)sprite->GetBaseHeight();
}

u8 UiMapSpriteFilter::SetVisible(bool enabled) {
  visible = enabled;
  return visible;
}

u8 UiMapSpriteFilter::IsVisible() {
  return visible;
}

u8 UiMapSpriteFilter::SetPalette(GxPaletteView *palette) {
  return sprite->SetPalette(palette);
}

// Borrow the source's current palette without an additional retain.
GxPaletteView *UiMapSpriteFilter::GetPalette() {
  return sprite->GetPalette();
}

u8 UiMapSpriteFilter::Draw(GxBuffer *destination, ScRect *area, ScRect *clip) {
  u8 locked = destination->Lock(GxLockFlags::GetSetPixel);
  if (!locked)
    return locked;
  u8 result = sprite->Draw(destination, *area, clip);
  destination->Unlock(GxLockFlags::GetSetPixel);
  return result;
}

u8 UiMapSpriteFilter::CreateBuffer(u32, u32, GxBuffer **) {
  return 0;
}

// Retain a new filter around the same source and copy its visible byte.
u8 UiMapSpriteFilter::CloneFilter(UiMapFilter **out) {
  if (!sprite)
    return 0;
  UiMapSpriteFilter *copy = new UiMapSpriteFilter(sprite, sprite->GetPalette());
  copy->AddRef();
  copy->visible = visible;
  *out = copy;
  return 1;
}

void UiMapSpriteFilter::SetPostFilter(bool enabled) {
  if (sprite)
    sprite->SetPostFilter(enabled);
}

void UiMapSpriteFilter::SetDestinationCoords(i32 x0, i32 y0, i32 x1, i32 y1, i32 x3, i32 y3) {
  if (sprite)
    sprite->SetDestinationCoords(x0, y0, x1, y1, x3, y3);
}

u8 UiMapSpriteFilter::IsPostFilter() const {
  return sprite ? sprite->IsPostFilter() : 0;
}

// Transform a point through the original projected basis with the installed wrapped integer arithmetic.
void UiMapSprite::TransformVectorEffects(i32 *x, i32 *z) {
  u32 x_product = (u32)*x * ((u32)corner_x[1] - (u32)corner_x[0]) + (u32)*z * ((u32)corner_x[3] - (u32)corner_x[0]);
  u32 z_product = (u32)*x * ((u32)corner_z[1] - (u32)corner_z[0]) + (u32)*z * ((u32)corner_z[3] - (u32)corner_z[0]);
  assert(width && (width != -1 || (x_product != kScIntegerSignBit && z_product != kScIntegerSignBit)));
  i32 result_z = (i32)((u32)corner_z[0] + (u32)((i32)z_product / width));
  *x = (i32)((u32)corner_x[0] + (u32)((i32)x_product / width));
  *z = result_z;
}

// Clip one segment against an oriented edge while preserving the original endpoint updates.
static void UiMapClipEdgeEffects(i32 *x0, i32 *z0, i32 *x1, i32 *z1, i32 ax, i32 az, i32 bx, i32 bz) {
  i32 dx = (i32)((u32)bx - (u32)ax), dz = (i32)((u32)bz - (u32)az);
  i32 first = (i32)((u32)dx * ((u32)*z0 - (u32)az) - (u32)dz * ((u32)*x0 - (u32)ax));
  i32 second = (i32)((u32)dx * ((u32)*z1 - (u32)az) - (u32)dz * ((u32)*x1 - (u32)ax));
  if (first <= 0 && second <= 0) {
    *x1 = *x0;
    *z1 = *z0;
    return;
  }
  if (first > 0 && second > 0)
    return;
  i32 xdiff = (i32)((u32)*x1 - (u32)*x0);
  i32 denominator = (i32)((u32)dz * (u32)xdiff - (u32)dx * ((u32)*z1 - (u32)*z0));
  assert(denominator);
  f64 fraction = (f64)first / denominator;
  f64 result_x = (f64)xdiff * fraction + (f64)*x0;
  f64 result_z = (f64)(i32)((u32)*z1 - (u32)*z0) * fraction + (f64)*z0;
  assert(result_x >= -9223372036854775808.0 && result_x < 9223372036854775808.0);
  i32 x = (i32)(u32)(i64)result_x;
  if (first <= 0) {
    *x0 = x;
    assert(result_z >= -9223372036854775808.0 && result_z < 9223372036854775808.0);
    *z0 = (i32)(u32)(i64)result_z;
  } else {
    *x1 = x;
    assert(result_z >= -9223372036854775808.0 && result_z < 9223372036854775808.0);
    *z1 = (i32)(u32)(i64)result_z;
  }
}

// Clip a segment against all four stored projected edges in their original order.
void UiMapSprite::ClipLineEffects(i32 *x0, i32 *z0, i32 *x1, i32 *z1) {
  for (u32 i = 0; i < 4; ++i) {
    u32 next = (i + 1) & 3;
    UiMapClipEdgeEffects(x0, z0, x1, z1, corner_x[i], corner_z[i], corner_x[next], corner_z[next]);
  }
}

// Flip, optionally transform and clip the segment, then draw it under the original line lock.
u8 UiMapSprite::DrawLine(
    i32 x0, i32 z0, i32 x1, i32 z1, u32 mode, GxBuffer *destination, const ScRect &rectangle, u32 color, ScRect *) {
  z0 = (i32)((u32)height - (u32)z0 - 1);
  z1 = (i32)((u32)height - (u32)z1 - 1);
  if (enabled) {
    TransformVectorEffects(&x0, &z0);
    TransformVectorEffects(&x1, &z1);
    ClipLineEffects(&x0, &z0, &x1, &z1);
  }
  if ((x0 != x1 || z0 != z1) && destination->Lock(GxLockFlags::Line)) {
    destination->DrawLine((i32)((u32)x0 + (u32)rectangle.left), (i32)((u32)z0 + (u32)rectangle.top),
        (i32)((u32)x1 + (u32)rectangle.left), (i32)((u32)z1 + (u32)rectangle.top), (i32)color, mode);
    destination->Unlock(GxLockFlags::Line);
  }
  return 1;
}

u16 UiMapSprite::ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) {
  UiGetGraphics();
  return UiGetGraphics()->ConvertRGBValueToNative16Bit(red, green, blue);
}
