#define _CRT_SECURE_NO_WARNINGS
#include "application/local_album.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "application/api.h"
#include "application/application_core.h"
#include "application/main_dialogs.h"
#include "application/newscast_ui.h"
#include "application/window_controls.h"
#include "audio/api.h"
#include "buildings/architect/image.h"
#include "city/api.h"
#include "city/simulation.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/indexed_file.h"
#include "resources/manager_object.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_main_window.h"
#include "ui/ui_menu_config.h"
#include "ui/ui_resource_handles.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/input.h"

// The text editor clamps this endpoint to the text length when selecting the entire description.
enum ApViewerSelectionEndpoint : u32 {
  kApViewerSelectionToEnd = 0xffffffff,
};

// Viewer button handlers and installed tips identify the previous, next, zoom-in and back artwork.
enum ApViewerNavigationArtwork : u32 {
  kApViewerPreviousGraphicImageId = 0xa2defe93,
  kApViewerNextGraphicImageId = 0xa2defe94,
  kApViewerZoomInImageId = 0x651ad702,
  kApViewerBackImageId = 0x651ad701,
};

// Local preferences use a fixed record type/group and the original internet-preferences block instance.
enum ApPreferenceArchiveKey : u32 {
  kApPreferenceRecordType = 0x127b4623,
  kApPreferenceRecordGroup = 0x127b4624,
  kApInternetPreferencesRecordInstance = 0x127b4652,
};

// Allocated preference reads use the stored record size when the caller supplies zero or the all-ones size request.
enum ApPreferenceRecordSize : u32 {
  kApPreferenceRecordSizeFromArchive = 0xffffffff,
};

// The local viewer uses separate control-strip, scrollbar, compact, expanded and no-image artwork; snapshots retain their watermark.
enum ApLocalViewerArtwork : u32 {
  kApViewerControlStripImageId = 0x651ad703,
  kApViewerDescriptionScrollbarImageId = 0xe2b99001,
  kApViewerCompactFrameImageId = 0x2558a4cb,
  kApViewerExpandedFrameImageId = 0x2558a4cc,
  kApViewerNoImageId = 0x2558a4cd,
  kApSnapshotWatermarkImageId = 0x651ad637,
  kApGraphicsViewerWindowId = 0x423278d,
};

// Snapshot capture retains its root-window identity and delays help-text width calculation until fonts are available.
enum ApSnapshotIdentity : u32 {
  kApSnapshotWindowId = 0x85202c0e,
  kApSnapshotTextWidthUnmeasured = 0xffffffff,
};

// A local album-directory search reports this index when the directory is absent.
enum ApAlbumIndexResult : u32 {
  kApAlbumIndexNotFound = 0xffffffff,
};

std::vector<ScSize> ApSnapshotTaker::sizes;
std::vector<ScString> ApSnapshotTaker::help_text;
u32 ApSnapshotTaker::max_text_width = kApSnapshotTextWidthUnmeasured;
BdImageIoInterface *WnAppBitmapView::image_factory = nullptr;
// Create an image button from its resource key and attach its window to the supplied parent.
WnButton *UiAddImageButton(WnWindow *parent, u32 identifier, i32 x, i32 y, u32 image_id, char shortcut) {
  GxColorType format(GxColorType::Color::RGB16);
  UiImageHandle handle(image_id, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = handle.GetBuffer();
  if (!image)
    return nullptr;
  ScString caption;
  WnButton *button = ApGetUiResources()->CreateFixedButton(identifier, caption, image, image->GetBufferArea(),
      static_cast<u32>(static_cast<i32>(shortcut)));
  if (button) {
    WnWindow *button_window = button->GetWindow();
    button->SetButtonFlag(kWnButtonShowText, false);
    button_window->GZWinMoveTo(x, y);
    parent->ChildAdd(button_window);
    button->Release();
  }
  return button;
}

// Build a local screenshot name from the account name and time.
// The timestamp and filename-safe substitutions replace the installed formatter's undefined pointer argument.
bool NwTransferObject::GetNameOfScreen(TransferType type, ScStringView *out) {
  ScString username, formatted;
  CtTime current;
  ScVersion version;
  version.GetUserNameA(&username, nullptr);
  for (char *p = username.begin; p != username.end; ++p)
    if (static_cast<u8>(*p) < 32 || std::strchr("<>:\"/\\|?*", *p))
      *p = '_';
  char kind = type == static_cast<TransferType>(3) ? 's' : type == static_cast<TransferType>(2) ? 'v' : 'f';
  formatted.Sprintf("%s--%c--%u.jpg", username.ToChar(), kind, current.timestamp);
  out->FromChar(formatted.Data(), formatted.Strlen());
  return true;
}

// The viewer's editor selects its complete text on the first unmodified click after a focus change.
class WnViewerEditor : public WnAppEditor {
public:
  WnViewerEditor();
  ~WnViewerEditor() override;
  bool GZOnSetFocus(WnWindow *window) override;

protected:
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool focus_changed;
};

WnViewerEditor::WnViewerEditor() : WnAppEditor(511), focus_changed(false) {
}

WnViewerEditor::~WnViewerEditor() {
}

// Remember a focus transfer from another window before forwarding it to the editor.
bool WnViewerEditor::GZOnSetFocus(WnWindow *previous) {
  if (previous != this)
    focus_changed = true;
  return WnAppEditor::GZOnSetFocus(previous);
}

// Forward the click, then select all text after a focus change when no modifier is pressed.
bool WnViewerEditor::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  bool result = WnAppEditor::OnMouseDownL(x, y, modifiers);
  if (focus_changed) {
    focus_changed = false;
    if (!(modifiers & kWnModifierKeyboardMask))
      SetSelection(0, kApViewerSelectionToEnd);
  }
  return result;
}

// Create a styled status label, attach its window and return the parent-owned label interface.
WnLabelControl *UiAddStatusText(WnWindow *parent, u32 identifier, i32 left, i32 top, i32 right, i32 bottom,
    const ScStringView &text, u32 alignment) {
  WnResources *resources = ApGetUiResources();
  WnLabelControl *label = resources->CreateLabel(identifier, text);
  label->GetWindow()->SetID(identifier);
  label->SetFont(resources->GetFont(kWnFontUiSmall));
  label->SetTextColor(ApGetGraphics()->ConvertRGBValueToNative16Bit(121, 125, 181));
  label->GetWindow()->SetArea(left, top, right, bottom);
  label->SetAlignment(alignment);
  parent->ChildAdd(label->GetWindow());
  label->Release();
  return label;
}

// Create an outline window, set its original rectangle and color, and attach it to the parent.
WnOutlineControl *UiAddOutline(WnWindow *parent, u32 identifier, i32 left, i32 top, i32 right, i32 bottom,
    u32 color, bool) {
  WnOutlineControl *outline = nullptr;
  if (!ApGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline)))
    return nullptr;
  ScRect rectangle{left, top, right, bottom};
  WnWindow *outline_window = outline->GetWindow();
  outline->SetFlags(kWnOutlineInset);
  outline->SetFillColor(color);
  outline_window->SetArea(rectangle);
  outline_window->SetID(identifier);
  outline_window->SetFlag(kWnFlagReorderable, false);
  outline_window->GZWinMoveTo(left, top);
  parent->ChildAdd(outline_window);
  outline->Release();
  return outline;
}

// Open the local image album for the original album command.
// Commands requiring chat or remote transfers are explicitly unavailable.
bool NwTransferObject::DoUserStringCommand(const ScStringView &, NwInternetCommand type, const NwChatStrings &) {
  if (static_cast<u32>(type) == 42) {
    ShowImageBrowser(true, false, nullptr, true);
    return true;
  }
  return false;
}

static void UiEnableButton(WnWindow *window, u32 identifier, bool enabled) {
  WnWindow *child = window->GetChildWindowFromID(identifier);
  if (child)
    child->SetFlag(kWnFlagEnabled, enabled);
}

static i32 WnBitmapInteger(f64 value) {
  assert(std::isfinite(value) && value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return static_cast<i32>(static_cast<i64>(value));
}

WnAppBitmapView::WnAppBitmapView()
    : zoom_enabled(true), scrolling_enabled(true), fit_image(false), minimum_zoom(0.125f), maximum_zoom(16.0f),
      alignment(static_cast<Alignment>(0)), zoom(1.0f), image_area{}, origin_x(0), origin_y(0), mouse_x(0), mouse_y(0),
      last_scroll_time(0), min_scroll_time(20) {
}

WnAppBitmapView::~WnAppBitmapView() {
  WnAppBitmapView::Shutdown();
}

bool WnAppBitmapView::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnBitmapView, WnBitmapView);
  return WnAppBitmap::QueryInterface(iid, out);
}

bool WnAppBitmapView::Init() {
  if (!WnAppBitmap::Init())
    return false;
  CalculateAllMetrics();
  if (!image_factory)
    ApGetCom()->GetClassObject(kGxGimexImageClassId, IID_GxGimexImage, reinterpret_cast<void **>(&image_factory));
  return true;
}

bool WnAppBitmapView::Shutdown() {
  ScRelease(&image_factory);
  return WnAppBitmap::Shutdown();
}

void WnAppBitmapView::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnAppBitmap::SetArea(left, top, right, bottom);
  CalculateAllMetrics();
}

// Replace the bitmap, reset its scroll origin and recalculate the viewer geometry.
void WnAppBitmapView::SetImage(GxBuffer *incoming) {
  WnAppBitmap::SetImage(incoming);
  origin_x = 0;
  origin_y = 0;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
}

// Change the image alignment and recalculate its geometry when the value changes.
void WnAppBitmapView::SetAlignment(Alignment value) {
  if (alignment != value) {
    alignment = value;
    CalculateAllMetrics();
    InvalidateSelfAndParents();
  }
}

bool WnAppBitmapView::SetOrigin(i32 x, i32 y) {
  origin_x = x;
  origin_y = y;
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

// Translate a window pixel to image coordinates and report whether the result is inside the bitmap.
bool WnAppBitmapView::WindowPixelToImagePixel(i32 x, i32 y, i32 *image_x, i32 *image_y) {
  *image_x = x;
  *image_y = y;
  if (!image)
    return false;
  *image_x = WnBitmapInteger(static_cast<f64>(x - image_area.left) / zoom);
  *image_y = WnBitmapInteger(static_cast<f64>(y - image_area.top) / zoom);
  return *image_x >= 0 && static_cast<u32>(*image_x) < GetImageWidth() &&
      *image_y >= 0 && static_cast<u32>(*image_y) < GetImageHeight();
}

bool WnAppBitmapView::CenterOnWindowPixel(i32 x, i32 y) {
  WindowPixelToImagePixel(x, y, &x, &y);
  return CenterOnImagePixel(x, y);
}

// Center the image on an in-range pixel unless both image dimensions are smaller than the window.
bool WnAppBitmapView::CenterOnImagePixel(i32 x, i32 y) {
  u32 width = GetImageWidth(), height = GetImageHeight();
  if ((width < static_cast<u32>(GetW()) && height < static_cast<u32>(GetH())) || x < 0 ||
      static_cast<u32>(x) >= GetImageWidth() || y < 0 || static_cast<u32>(y) >= GetImageHeight())
    return false;
  u32 window_width = static_cast<u32>(GetW()), window_height = static_cast<u32>(GetH());
  f64 scaled_x = static_cast<f64>(x) * zoom, scaled_y = static_cast<f64>(y) * zoom;
  if (static_cast<i32>(alignment)) {
    origin_x = WnBitmapInteger(scaled_x);
    origin_y = WnBitmapInteger(scaled_y);
  } else {
    origin_x = WnBitmapInteger(scaled_x - static_cast<f64>(window_width >> 1));
    origin_y = WnBitmapInteger(scaled_y - static_cast<f64>(window_height >> 1));
  }
  CalculateAllMetrics();
  InvalidateSelfAndParents();
  return true;
}

bool WnAppBitmapView::SetZoom(f32 value) {
  i32 old_x = origin_x, old_y = origin_y;
  f32 old_zoom = zoom;
  if (value < minimum_zoom)
    value = minimum_zoom;
  else if (value > maximum_zoom)
    value = maximum_zoom;
  if (zoom != value) {
    i32 x, y;
    WindowPixelToImagePixel(GetW() / 2, GetH() / 2, &x, &y);
    zoom = value;
    CalculateAllMetrics();
    CenterOnImagePixel(x, y);
    CalculateAllMetrics();
    if (origin_x != old_x || origin_y != old_y || zoom != old_zoom)
      InvalidateSelfAndParents();
  }
  return true;
}

// Scroll by explicit offsets or one thirty-second of the image for unit direction commands.
bool WnAppBitmapView::Scroll(i32 x, i32 y) {
  i32 old_x = origin_x, old_y = origin_y;
  if (x || y) {
    if (x == 1 || x == -1) {
      u32 step = GetImageWidth() >> 5;
      if (!step)
        step = 1;
      x = x == 1 ? static_cast<i32>(step) : static_cast<i32>(0u - step);
    }
    if (y == 1 || y == -1) {
      u32 step = GetImageHeight() >> 5;
      if (!step)
        step = 1;
      y = y == 1 ? static_cast<i32>(step) : static_cast<i32>(0u - step);
    }
    origin_x = origin_x + x;
    origin_y = origin_y + y;
    CalculateAllMetrics();
    if (origin_x != old_x || origin_y != old_y)
      InvalidateSelfAndParents();
  }
  return true;
}

// Clamp the zoom and scroll origin, then derive the image rectangle for the selected alignment.
void WnAppBitmapView::CalculateAllMetrics() {
  u32 width = GetImageWidth(), height = GetImageHeight();
  u32 window_width = static_cast<u32>(GetW()), window_height = static_cast<u32>(GetH());
  if (!width || !height)
    return;
  if (zoom < minimum_zoom)
    zoom = minimum_zoom;
  else if (zoom > maximum_zoom)
    zoom = maximum_zoom;
  f64 scaled_width = static_cast<f64>(width) * zoom, scaled_height = static_cast<f64>(height) * zoom;
  f32 stored_width = static_cast<f32>(scaled_width);
  u32 pixel_width = static_cast<u32>(WnBitmapInteger(scaled_width));
  u32 pixel_height = static_cast<u32>(WnBitmapInteger(scaled_height));
  if (pixel_width > window_width) {
    if (static_cast<i32>(alignment)) {
      u32 half = window_width >> 1;
      f64 limit = scaled_width - half;
      if (static_cast<f64>(origin_x) > limit)
        origin_x = WnBitmapInteger(limit);
      if (static_cast<u32>(origin_x) < half)
        origin_x = static_cast<i32>(half);
    } else {
      f64 limit = scaled_width - static_cast<i32>(window_width);
      if (static_cast<f64>(origin_x) > limit)
        origin_x = WnBitmapInteger(limit);
      if (origin_x < 0)
        origin_x = 0;
    }
  } else {
    origin_x = static_cast<i32>(alignment) ? static_cast<i32>(width >> 1) : 0;
  }
  if (pixel_height > window_height) {
    if (static_cast<i32>(alignment)) {
      u32 half = window_height >> 1;
      f64 limit = scaled_height - half;
      if (static_cast<f64>(origin_y) > limit)
        origin_y = WnBitmapInteger(limit);
      if (static_cast<u32>(origin_y) < half)
        origin_y = static_cast<i32>(half);
    } else {
      f64 limit = scaled_height - window_height;
      if (static_cast<f64>(origin_y) > limit)
        origin_y = WnBitmapInteger(limit);
      if (origin_y < 0)
        origin_y = 0;
    }
  } else {
    origin_y = static_cast<i32>(alignment) ? static_cast<i32>(height >> 1) : 0;
  }
  if (static_cast<i32>(alignment)) {
    i32 left = static_cast<i32>((window_width >> 1) - static_cast<u32>(static_cast<i32>(pixel_width) / 2));
    i32 top = static_cast<i32>((window_height >> 1) - static_cast<u32>(static_cast<i32>(pixel_height) / 2));
    i32 right = WnBitmapInteger(static_cast<f64>(left) + stored_width);
    i32 bottom = WnBitmapInteger(static_cast<f64>(top) + scaled_height);
    u32 dx = (width >> 1) - static_cast<u32>(origin_x), dy = (height >> 1) - static_cast<u32>(origin_y);
    image_area = {static_cast<i32>(left + dx), static_cast<i32>(top + dy), static_cast<i32>(right + dx),
        static_cast<i32>(bottom + dy)};
  } else {
    u32 left = 0u - static_cast<u32>(origin_x), top = 0u - static_cast<u32>(origin_y);
    image_area = {static_cast<i32>(left), static_cast<i32>(top), static_cast<i32>(pixel_width + left),
        static_cast<i32>(pixel_height + top)};
  }
}

// Consume navigation keys and apply the enabled zoom or one-step scrolling actions.
bool WnAppBitmapView::OnKeyDown(u32 key, u32) {
  switch (key) {
  case kWnKeyNumpadAdd:
    if (zoom_enabled)
      ZoomIn(2.0f);
    break;
  case kWnKeyNumpadSubtract:
    if (zoom_enabled)
      ZoomOut(2.0f);
    break;
  case kWnKeyLeft:
    if (scrolling_enabled)
      Scroll(-1, 0);
    break;
  case kWnKeyUp:
    if (scrolling_enabled)
      Scroll(0, -1);
    break;
  case kWnKeyRight:
    if (scrolling_enabled)
      Scroll(1, 0);
    break;
  case kWnKeyDown:
    if (scrolling_enabled)
      Scroll(0, 1);
    break;
  default:
    break;
  }
  return true;
}

bool WnAppBitmapView::OnKeyUp(u32, u32) {
  return true;
}

// Center the image on a left-button click when scrolling is enabled.
bool WnAppBitmapView::OnMouseDownL(i32 x, i32 y, u32) {
  if (scrolling_enabled)
    CenterOnWindowPixel(x, y);
  return true;
}

// Begin right-button capture, record the drag origin and post the viewer's scroll message.
bool WnAppBitmapView::OnMouseDownR(i32 x, i32 y, u32) {
  if (GZSetCapture()) {
    mouse_x = x;
    mouse_y = y;
    owner->PostMessageToTarget(this, kWnMsg_BitmapViewScrollTick, 0, 0, 0);
  }
  return true;
}

// End right-button capture and record the final drag position.
bool WnAppBitmapView::OnMouseUpR(i32 x, i32 y, u32) {
  if (GZAmCapturing()) {
    mouse_x = x;
    mouse_y = y;
    GZReleaseCapture();
  }
  return true;
}

bool WnAppBitmapView::OnMouseMove(i32, i32, u32) {
  return true;
}

bool WnAppBitmapView::GZOnMouseWheel(i32, i32, u32, i32) {
  return true;
}

bool WnAppBitmapView::GZOnCaptureChanged(WnWindow *) {
  return true;
}

// Process timed captured scrolling and route ordinary mouse and other messages to their original base handlers.
bool WnAppBitmapView::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_BitmapViewScrollTick) {
    u32 now = static_cast<u32>(std::clock());
    if (last_scroll_time + min_scroll_time < now) {
      i32 x, y;
      owner->GetCursorRelativePosition(this, &x, &y);
      i32 direction = static_cast<i64>(x) > static_cast<i64>(mouse_x) + 8 ? 1 :
          static_cast<i64>(x) < static_cast<i64>(mouse_x) - 8 ? -1 : 0;
      i32 vertical = static_cast<i64>(y) > static_cast<i64>(mouse_y) + 8 ? 1 :
          static_cast<i64>(y) < static_cast<i64>(mouse_y) - 8 ? -1 : 0;
      Scroll(direction, vertical);
      last_scroll_time = now;
    }
    if (GZAmCapturing())
      owner->PostMessageToTarget(this, kWnMsg_BitmapViewScrollTick, 0, 0, 0);
    return true;
  }
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel)
    return UiWindow::DoMessage(message);
  return WnAppBitmap::DoMessage(message);
}

// Fill uncovered background and draw the image using a direct or stretched blit at the current view rectangle.
bool WnAppBitmapView::GZPaint() {
  i32 image_width = image_area.right - image_area.left;
  i32 image_height = image_area.bottom - image_area.top;
  i32 draw_width = draw_area.right - draw_area.left;
  i32 draw_height = draw_area.bottom - draw_area.top;
  if (!(bitmap_flags & 2) && (!image || image_width < draw_width || image_height < draw_height) &&
      draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(GetFillColor(), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (image && draw_buffer->Lock(GxLockFlags::Blt)) {
    ScRect source{0, 0, static_cast<i32>(image->Width()), static_cast<i32>(image->Height())};
    ScRect destination{image_area.left + draw_area.left, image_area.top + draw_area.top,
        image_area.right + draw_area.left, image_area.bottom + draw_area.top};
    if (zoom == 1.0f)
      draw_buffer->Blt(image, source, destination, &draw_area);
    else
      draw_buffer->BltStretch(image, source, destination, &draw_area);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

WnAppBitmap::WnAppBitmap() : image(nullptr), alpha_enabled(false), opacity(0.5f), source_area{}, bitmap_flags(2) {
}

WnAppBitmap::~WnAppBitmap() {
  WnAppBitmap::Shutdown();
}

bool WnAppBitmap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnBitmapControl, WnBitmapControl);
  return UiWindow::QueryInterface(iid, out);
}

u32 WnAppBitmap::AddRef() {
  return UiWindow::AddRef();
}

u32 WnAppBitmap::Release() {
  return UiWindow::Release();
}

// Borrow the primary window from the bitmap's secondary interface.
WnWindow *WnAppBitmap::GetWindow() {
  return this;
}

// Calculate the initial source rectangle before initializing the application window.
bool WnAppBitmap::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  i32 height = GetH(), width = GetW();
  ScRect bounds{0, 0, width, height};
  SetSourceBufferAreaFromWindowArea(bounds);
  return UiWindow::Init();
}

bool WnAppBitmap::Shutdown() {
  ScRelease(&image);
  return UiWindow::Shutdown();
}

// Reset the source rectangle to the window dimensions and clamp it to the retained bitmap dimensions.
void WnAppBitmap::SetSourceBufferAreaFromWindowArea(const ScRect &rectangle) {
  source_area = {0, 0, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top};
  if (image) {
    if (source_area.right > static_cast<i32>(image->Width()))
      source_area.right = static_cast<i32>(image->Width());
    if (source_area.bottom > static_cast<i32>(image->Height()))
      source_area.bottom = static_cast<i32>(image->Height());
  }
}

void WnAppBitmap::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiWindow::SetArea(left, top, right, bottom);
  SetSourceBufferAreaFromWindowArea(*GetArea());
}

// Retain the incoming bitmap before releasing the old one, then recompute its visible extent and invalidate.
void WnAppBitmap::SetImage(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (image)
    image->Release();
  image = incoming;
  SetSourceBufferAreaFromWindowArea(*GetArea());
  InvalidateSelfAndParents();
}

void WnAppBitmap::SetAlpha(bool enabled, f32 value) {
  alpha_enabled = enabled;
  opacity = value;
  InvalidateSelfAndParents();
}

bool WnAppBitmap::GetBitmapFlag(i32 mask) const {
  return (bitmap_flags & static_cast<u32>(mask)) != 0;
}

void WnAppBitmap::SetBitmapFlag(i32 mask, bool enabled) {
  if (enabled)
    bitmap_flags |= static_cast<u32>(mask);
  else
    bitmap_flags &= ~static_cast<u32>(mask);
}

bool WnAppBitmap::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  return !GetBitmapFlag(4) && UiWindow::IsPointInWindowScreenCoordinates(x, y);
}

// Handle keyboard input locally and translate mouse coordinates when forwarding flagged messages to the parent.
bool WnAppBitmap::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp)
    return UiWindow::DoMessage(message);
  if (!GetBitmapFlag(1) || !parent)
    return false;
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel) {
    message->data1 = static_cast<u32>(message->data1) + static_cast<u32>(GetL());
    message->data2 = static_cast<u32>(message->data2) + static_cast<u32>(GetT());
  }
  return parent->DoMessage(message);
}

bool WnAppBitmap::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnAppBitmap::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Fill the background when enabled, then blit the retained bitmap with the selected alpha mode.
bool WnAppBitmap::GZPaint() {
  if (!(bitmap_flags & 2) && draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(GetFillColor(), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (image && draw_buffer->Lock(GxLockFlags::Blt)) {
    if (alpha_enabled) {
      draw_buffer->AlphaBlt(image, source_area, draw_origin, opacity, nullptr);
    } else {
      ScRect destination{
          draw_area.left, draw_area.top, draw_area.left + source_area.right, draw_area.top + source_area.bottom};
      draw_buffer->Blt(image, source_area, destination, nullptr);
    }
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

void WnAppBitmapView::SetShouldFitImageInView(bool enabled) {
  fit_image = enabled;
}

void WnAppBitmapView::EnableZooming(bool enabled) {
  zoom_enabled = enabled;
}

void WnAppBitmapView::EnableScrolling(bool enabled) {
  scrolling_enabled = enabled;
}

void WnAppBitmapView::SetMinMsPerScroll(u32 milliseconds) {
  min_scroll_time = milliseconds;
}

void WnAppBitmapView::GetBMP(GxBuffer **out) {
  *out = image;
}

u32 WnAppBitmapView::GetImageWidth() {
  return image ? image->Width() : 0;
}

u32 WnAppBitmapView::GetImageHeight() {
  return image ? image->Height() : 0;
}

// Set the zoom bounds, clamping the lower and upper limits independently to the original hard limits.
bool WnAppBitmapView::SetZoomRangeMinMax(f32 minimum, f32 maximum) {
  minimum_zoom = minimum < 0.25f ? 0.25f : minimum;
  maximum_zoom = maximum > 256.0f ? 256.0f : maximum;
  return true;
}

bool WnAppBitmapView::ZoomIn(f32 factor) {
  return SetZoom(factor * zoom);
}

// Divide the current zoom when the requested factor meets the configured minimum.
bool WnAppBitmapView::ZoomOut(f32 factor) {
  if (factor < minimum_zoom)
    return false;
  return SetZoom(zoom / factor);
}

void WnAppBitmapView::ClearBMP() {
  SetImage(nullptr);
}

// Publish and retain the viewer's primary window-procedure identity for supported interface IDs.
bool NwGraphicsViewerObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwGraphicsViewerObject::AddRef() {
  return ++references;
}

// Create and configure the generic graphics-viewer window, retaining its returned primary window view.
bool NwGraphicsViewerObject::CreateGraphicsViewerWindow(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic = nullptr;
  if (!ApGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  *out = generic->GetWindow();
  generic->SetStyle(kUiGenericDraggable, true);
  generic->SetStyle(kUiGenericResizable, false);
  generic->SetStyle(kUiGenericScroller, false);
  generic->SetStyle(kUiGenericBackButton, false);
  generic->SetStyle(kUiGenericSecondButton, false);
  generic->SetCallback(this);
  (*out)->SetFlag(kWnFlagPrivateBuffer, true);
  (*out)->SetID(kApGraphicsViewerWindowId);
  window = ScAddRefNotNull(*out);
  return true;
}

// Construct the viewer controls, initialize its album path and restore its selected graphic and window position.
bool NwGraphicsViewerObject::OnInit(WnWindow *target) {
  if (!target)
    return false;
  if (viewer_initialized)
    return true;
  WnResources *resources = ApGetUiResources();
  ApServices::BoxX()->Event(1010, 0, 0, 0, 0);
  ScString caption;
  {
    UiTextHandle title(kRsSupplementalGameViewSnapshotsText, kRsSupplementalGameTextGroup);
    caption = title.Val();
  }
  target->SetCaption(caption);
  target->SetSize(512, 360);
  WnWindow *height_parent = target->GetParentWin();
  WnWindow *width_parent = target->GetParentWin();
  i32 y = (height_parent->GetH() - target->GetH()) / 2;
  i32 x = (width_parent->GetW() - target->GetW()) / 2;
  target->GZWinMoveTo(x, y);
  ScString label_caption;
  {
    UiTextHandle label_text(kRsSupplementalGameAlbumsText, kRsSupplementalGameTextGroup);
    label_caption = label_text.Val();
  }
  WnLabelControl *label = UiAddStatusText(target, 1000, 310, 36, 504, 54, label_caption, 0);
  label->SetBackground(false);
  label->SetTextColor(0, resources->GetColor(kWnThemeMutedTextColorId));
  label->SetTextColor(1, resources->GetColor(21));
  label->SetFont(resources->GetFont(kWnFontUiSmall));
  GxColorType format(GxColorType::Color::RGB16);
  UiImageHandle image(kApViewerControlStripImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  auto *background = new WnAppBitmap;
  background->AddRef();
  background->GetWindow()->SetFlag(kWnFlagReorderable, false);
  background->SetArea(6, 267, 302, 301);
  background->SetImage(image.GetBuffer());
  target->ChildAdd(background);
  background->Release();
  ScString button_caption;
  {
    UiTextHandle button_text(kRsSupplementalGameDeleteText, kRsSupplementalGameTextGroup);
    button_caption = button_text.Val();
  }
  WnButton *button = ApGetUiResources()->CreateButton(102, button_caption, nullptr, nullptr, 0);
  button->SetButtonFlag(kWnButtonShowText, true);
  button->SetButtonFlag(kWnButtonAutoFit, false);
  button->GetWindow()->SetCaption(button_caption);
  button->GetWindow()->SetArea(107, 268, 203, 298);
  button->GetWindow()->SetFlag(kWnFlagEnabled, true);
  target->ChildAdd(button->GetWindow());
  button->Release();
  UiAddImageButton(target, 100, 67, 269, kApViewerPreviousGraphicImageId, 0);
  WnWindow *child = target->GetChildWindowFromID(100);
  if (child)
    child->SetFlag(kWnFlagEnabled, true);
  UiAddImageButton(target, 101, 204, 269, kApViewerNextGraphicImageId, 0);
  child = target->GetChildWindowFromID(101);
  if (child)
    child->SetFlag(kWnFlagEnabled, true);
  UiAddImageButton(target, 103, 464, 321, kRsStandardButtonImageId, 0);
  child = target->GetChildWindowFromID(103);
  if (child)
    child->SetFlag(kWnFlagEnabled, true);
  can_navigate = true;
  ScString empty;
  image_count_label = ScAddRefNotNull(UiAddStatusText(target, 107, 8, 274, 61, 295, empty, 0));
  image_count_label->SetBackground(false);
  image_count_label->SetFont(resources->GetFont(kWnFontUiSmall));
  image_count_label->SetTextColor(0, resources->GetColor(kWnThemeBackgroundColorId));
  image_count_label->SetTextColor(1, resources->GetColor(kWnThemePanelFillColorId));
  image_count_label->SetAlignment(kWnLabelCenterMiddle);
  file_label = ScAddRefNotNull(UiAddStatusText(target, 105, 311, 179, 503, 311, empty, 0));
  file_label->SetBackground(false);
  file_label->SetFont(resources->GetFont(kWnFontUiSmall));
  file_label->SetTextColor(0, resources->GetColor(kWnThemeTextColorId));
  UiAddOutline(target, 0, 6, 307, 302, 355, resources->GetColor(21), true);
  auto *editor = new WnViewerEditor;
  description_editor = ScAddRefNotNull(editor);
  description_editor->AsIGZWin()->SetArea(8, 309, 300, 353);
  description_editor->AsIGZWin()->SetID(106);
  description_editor->AsIGZWin()->SetFillColor(resources->GetColor(21));
  description_editor->SetCaretColor(ApGetGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255));
  description_editor->SetFont(resources->GetFont(kWnFontUiSystem));
  description_editor->SetFontColor(WnFontColor::Foreground, resources->GetColor(kWnThemeTextColorId));
  description_editor->SetFontColor(WnFontColor::HilitedForeground, resources->GetColor(21));
  UiImageHandle scrollbar_image(
      kApViewerDescriptionScrollbarImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *scrollbar_buffer = scrollbar_image.GetBuffer();
  if (scrollbar_buffer) {
    ScRect rectangle = *scrollbar_buffer->GetBufferArea();
    rectangle.left = rectangle.right / 2;
    description_editor->SetScrollBarImageVertical(scrollbar_buffer, rectangle);
  }
  target->ChildAdd(description_editor->AsIGZWin());
  auto *list = new WnAppList(false);
  album_list = ScAddRefNotNull(list);
  album_list->AsIGZWin()->SetArea(311, 54, 505, 169);
  album_list->AsIGZWin()->SetID(104);
  album_list->AsIGZWin()->SetFillColor(resources->GetColor(21));
  album_list->SetHighlightColor(ApGetGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255));
  album_list->SetFont(resources->GetFont(kWnFontUiSystem));
  album_list->SetFontColor(0, resources->GetColor(kWnThemeTextColorId));
  album_list->SetFontColor(2, resources->GetColor(21));
  target->ChildAdd(album_list->AsIGZWin());
  if (!field56) {
    field56 = UiAddImageButton(target, 108, 267, 275, kApViewerZoomInImageId, 0);
    if (field56) {
      field56->AddRef();
      child = target->GetChildWindowFromID(108);
      if (child)
        child->SetFlag(kWnFlagEnabled, true);
      field56->GetWindow()->ShowWindow();
      ScString tip;
      {
        UiTextHandle text(kRsSupplementalGameZoomInText, kRsSupplementalGameTextGroup);
        tip = text.Val();
      }
      if (tip.Strlen()) {
        field56->SetTipText(tip);
        field56->SetButtonFlag(kWnButtonShowHint, true);
        field56->SetTipPlacementType(kWnTipAboveCenter);
      }
    }
  }
  if (!field60) {
    field60 = UiAddImageButton(target, 109, 556, 7, kApViewerBackImageId, 0);
    if (field60) {
      field60->AddRef();
      child = target->GetChildWindowFromID(109);
      if (child)
        child->SetFlag(kWnFlagEnabled, true);
      field60->GetWindow()->HideWindow();
      ScString tip;
      {
        UiTextHandle text(kRsSupplementalGameBackText, kRsSupplementalGameTextGroup);
        tip = text.Val();
      }
      if (tip.Strlen()) {
        field60->SetTipText(tip);
        field60->SetButtonFlag(kWnButtonShowHint, true);
        field60->SetTipPlacementType(kWnTipAboveCenter);
      }
    }
  }
  UiImageHandle blank_image(kApViewerCompactFrameImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  field24 = blank_image.GetBuffer();
  if (field24)
    field24->AddRef();
  field32 = ScAddRefNotNull(new WnAppBitmapView);
  target->ChildAdd(field32);
  field32->SetArea(6, 40, 302, 262);
  field32->SetImage(field24);
  preview = ScAddRefNotNull(new WnAppBitmapViewExtra);
  target->ChildAdd(preview);
  preview->SetAlignment(static_cast<WnBitmapView::Alignment>(1));
  preview->SetSizeInOut(true);
  preview->SetShouldFitImageAtSizeIn(true);
  preview->SetArea(8, 42, 300, 260);
  ScRect compact{8, 42, 300, 260}, expanded{6, 40, 582, 470};
  preview->SetSizeInAndOutAreas(compact, expanded);
  preview->SetZoomRangeMinMax(0.125f, 4.0f);
  preview->SetFillColor(resources->GetColor(22));
  last_idle = static_cast<u32>(std::time(nullptr));
  if (!base_directory.Strlen()) {
    base_directory = ScString(ApGetFileSystem()->DataDirectory());
    u32 length = base_directory.Strlen();
    if (length >= 2) {
      u32 position = length - 2;
      while (position && base_directory.begin[position] != '\\' && base_directory.begin[position] != '/')
        --position;
      if (base_directory.begin[position] == '\\' || base_directory.begin[position] == '/')
        ++position;
      base_directory.Erase(position, length - position);
    }
    base_directory.Append("Albums\\");
  }
  bool changed;
  UpdateListOfAlbumDirectories(&changed, true);
  SetInitialFile();
  LoadWindowSettingsFromPrefs(target);
  viewer_initialized = true;
  return true;
}

// Restore an internet window's saved position when it lies within the main window's original bounds.
bool NwLoadWindowSettingsFromPrefs(WnWindow *window, i32 index) {
  NwInternet *internet = ApGetGameApp()->GetSC3Internet();
  NwInternetPrefs *prefs = internet ? internet->GetInternetPrefs() : nullptr;
  if (prefs && index < 15) {
    WnWindow *main = window->GetMainWindow();
    i32 width = main ? main->GetW() : 16, height = main ? main->GetH() : 16;
    assert(index >= 0);
    const NwWindowPosition &position = prefs->windowPositions[index];
    if (position.left >= 0 && static_cast<u32>(position.left) <= static_cast<u32>(width) - 8 &&
        position.top >= 0 && static_cast<u32>(position.top) < static_cast<u32>(height) - 8)
      window->GZWinMoveTo(position.left, position.top);
  }
  return true;
}

bool NwSaveWindowSettingsToPrefs(WnWindow *window, i32 index) {
  NwInternet *internet = ApGetGameApp()->GetSC3Internet();
  NwInternetPrefs *prefs = internet ? internet->GetInternetPrefs() : nullptr;
  if (prefs && index < 15) {
    assert(index >= 0);
    prefs->windowPositions[index].left = window->GetL();
    prefs->windowPositions[index].top = window->GetT();
  }
  return true;
}

bool NwGraphicsViewerObject::LoadWindowSettingsFromPrefs(WnWindow *target) {
  return NwLoadWindowSettingsFromPrefs(target, 10);
}

bool NwGraphicsViewerObject::SaveWindowSettingsToPrefs(WnWindow *target) {
  return NwSaveWindowSettingsToPrefs(target, 10);
}

// Set the viewer's original minimum and maximum dimensions when both callback arguments are present.
bool NwGraphicsViewerObject::OnGetMinMaxInfo(WnWindow *target, ScRect *bounds) {
  if (!target || !bounds)
    return false;
  bounds->left = 100;
  bounds->top = 100;
  bounds->right = 1000;
  bounds->bottom = 1000;
  return true;
}

bool NwGraphicsViewerObject::OnUserAreaChanged(WnWindow *, const ScRect *) {
  return true;
}

static void NwViewerMetadataPath(const ScString &path, const char *extension, ScString *out) {
  char drive[260], directory[260], filename[260], old_extension[260];
  RsSplitFilePath(path.ToChar(), drive, directory, filename, old_extension);
  out->FromChar(drive);
  out->Append(directory);
  out->Append(filename);
  out->Append(extension);
}

void NwGraphicsViewerObject::GetPathForInfoFile(const ScString &path, ScString *out) {
  NwViewerMetadataPath(path, ".info", out);
}

void NwGraphicsViewerObject::GetPathForDescriptionFile(const ScString &path, ScString *out) {
  NwViewerMetadataPath(path, ".txt", out);
}

// Read the description sidecar in blocks, or return the localized missing-description text if it cannot be opened.
bool NwGraphicsViewerObject::GetDescriptionOfFile(const ScString &path, ScString *out) {
  ScString sidecar;
  GetPathForDescriptionFile(path, &sidecar);
  out->Erase(0, out->Strlen());
  RsFile file(sidecar);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    UiTextHandle text(kRsSupplementalGameNoDescriptionText, kRsSupplementalGameTextGroup);
    *out = text.Val();
    return false;
  }
  char buffer[512];
  for (;;) {
    u32 length = sizeof(buffer);
    if (!file.Read(buffer, &length) || !length)
      break;
    out->Append(buffer, length);
  }
  file.Close();
  return true;
}

// Encrypt and write the information sidecar, preserving the original open failure and write-result handling.
bool NwGraphicsViewerObject::SaveInformationOfStandardFile(const ScString &path, const ScString &information) {
  ScString sidecar, encoded;
  GetPathForInfoFile(path, &sidecar);
  EncryptData(information, &encoded);
  RsFile file(sidecar);
  if (!file.Open(kRsFileAccessWrite, kRsFileOpenAlways, kRsFileShareRead))
    return false;
  file.Resize(0);
  file.SeekAbsolute(0);
  file.Write(encoded.Data(), encoded.Strlen());
  file.Close();
  return true;
}

// Write a description sidecar, or remove the supplied graphic path when the description is empty.
bool NwGraphicsViewerObject::SaveDescriptionOfFile(const ScString &path, const ScString &description) {
  ScString sidecar;
  GetPathForDescriptionFile(path, &sidecar);
  if (!description.Strlen()) {
    RsFile::Delete(path);
    return true;
  }
  RsFile file(sidecar);
  if (!file.Open(kRsFileAccessWrite, kRsFileOpenAlways, kRsFileShareRead))
    return false;
  file.Resize(0);
  file.SeekAbsolute(0);
  file.Write(description.Data(), description.Strlen());
  file.Close();
  return true;
}

// Return the viewer's retained window without acquiring another reference.
WnWindow *NwGraphicsViewerObject::GetIGZWin() {
  return window;
}

void NwGraphicsViewerObject::GetViewerBaseDirectoryPath(ScStringView *out) {
  out->FromChar(base_directory.Data(), base_directory.Strlen());
}

bool NwGraphicsViewerObject::SetViewerBaseDirectoryPath(const ScStringView &path) {
  base_directory = ScString(path);
  u32 length = base_directory.Strlen();
  if (!length || (base_directory.begin[length - 1] != '\\' && base_directory.begin[length - 1] != '/'))
    base_directory.Append("\\");
  RefreshGraphicList(nullptr);
  return true;
}

void NwGraphicsViewerObject::GetViewerTransferDirectoryName(ScStringView *out) {
  out->FromChar(transfer_directory.ToChar());
}

void NwGraphicsViewerObject::GetViewerTransferDirectoryPath(ScStringView *out) {
  ScString path(base_directory);
  path.Append(transfer_directory.Data(), transfer_directory.Strlen());
  out->FromChar(path.Data(), path.Strlen());
}

void NwGraphicsViewerObject::GetCurrentViewerDirectoryName(ScStringView *out) {
  out->FromChar(current_directory.Data(), current_directory.Strlen());
}

// Select an existing album directory, adding a separator and refreshing when the name changes.
bool NwGraphicsViewerObject::SetCurrentViewerDirectoryName(const ScStringView &name) {
  ScString selected(name);
  if (!current_directory.IsEqual(&selected)) {
    u32 length = selected.Strlen();
    if (!length || (selected.begin[length - 1] != '\\' && selected.begin[length - 1] != '/'))
      selected.Append("\\");
    ScString path(base_directory);
    path.Append(selected.Data(), selected.Strlen());
    if (!ScDirectory::DoesDirectoryExist(path))
      return false;
    current_directory = selected;
    RefreshGraphicList(nullptr);
  }
  return true;
}

bool NwGraphicsViewerObject::MakeSureCurrentAlbumDirectoryExists(bool) {
  ScString path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  return ScDirectory::DoesDirectoryExist(path) || ScDirectory::CreateDirectoryA(path);
}

void NwGraphicsViewerObject::SetIdleProcessingInterval(u32 interval) {
  idle_interval = interval;
}

void NwGraphicsViewerObject::DoIdleProcessing() {
  u32 now = static_cast<u32>(std::time(nullptr));
  if (now > last_idle + idle_interval) {
    bool changed;
    RefreshGraphicList(&changed);
    last_idle = static_cast<u32>(std::time(nullptr));
  }
}

bool NwGraphicsViewerObject::CityNameToAlbumName(const ScString &city, ScString *out) {
  ScString copy(city);
  for (u32 index = 0; index < copy.Strlen(); ++index) {
    char value = copy.begin[index];
    if (!value || !std::strchr("\\/:*?\"<>|", value))
      out->Append(&value, 1);
  }
  return true;
}

// XOR each byte with the executable's metadata key and return the complete transformed string.
bool NwGraphicsViewerObject::EncryptData(const ScString &source, ScString *out) {
  ScString copy(source);
  for (u32 index = 0; index < copy.Strlen(); ++index)
    copy.begin[index] ^= 0x7b;
  *out = copy;
  return true;
}

// Reverse the same bytewise XOR used to store viewer metadata.
bool NwGraphicsViewerObject::DecryptData(const ScString &source, ScString *out) {
  ScString copy(source);
  for (u32 index = 0; index < copy.Strlen(); ++index)
    copy.begin[index] ^= 0x7b;
  *out = copy;
  return true;
}

bool NwGraphicsViewerObject::IsCurrentAlbumDirectoryTransferDirectory() {
  return current_directory.IsEqual(&transfer_directory);
}

// Find the first album directory with exactly the requested name and report its index.
bool NwGraphicsViewerObject::ConvertAlbumDirectoryNameToAlbumIndex(const ScString &directory, u32 *out) {
  for (u32 index = 0; index < album_directories.size(); ++index) {
    if (album_directories[index].IsEqual(&directory)) {
      *out = index;
      return true;
    }
  }
  *out = kApAlbumIndexNotFound;
  return false;
}

bool NwGraphicsViewerObject::ConvertAlbumIndexToAlbumDirectoryName(u32 index, ScString *out) {
  if (index >= album_directories.size())
    return false;
  *out = album_directories[index];
  return true;
}

// Remove an album's numeric suffix or trim the separator from the special transfer album name.
bool NwGraphicsViewerObject::ConvertAlbumDirectoryNameToAlbumName(const ScString &directory, ScString *out) {
  u32 length = directory.Strlen();
  if (!length)
    return false;
  u32 position = kScStringNotFound;
  for (u32 index = 1; index + 1 < length; ++index) {
    if (directory.begin[index] == '-' && directory.begin[index + 1] == '-') {
      position = index;
      break;
    }
  }
  if (position <= length - 3) {
    *out = ScString(directory.begin, position < length ? position : length);
    return true;
  }
  if (directory.IsEqual(&transfer_directory)) {
    *out = transfer_directory;
    u32 size = out->Strlen();
    if (size && (out->begin[size - 1] == '\\' || out->begin[size - 1] == '/'))
      out->Erase(size - 1, 1);
    return true;
  }
  return false;
}

bool NwGraphicsViewerObject::GoToTransferAlbum() {
  ScString name;
  ConvertAlbumDirectoryNameToAlbumName(transfer_directory, &name);
  return GoToAlbum(name);
}

// Check for the directory formed by the album name and its numeric identifier.
bool NwGraphicsViewerObject::AlbumExists(const ScString &name, u32 index) {
  ScString path;
  path.Sprintf("%s%s--%u\\", base_directory.ToChar(), name.ToChar(), index);
  return ScDirectory::DoesDirectoryExist(path);
}

// Check an album's marker file and, when requested, require at least one JPEG image.
bool NwGraphicsViewerObject::AlbumDirectoryHasAlbumEntries(const ScString &directory, bool require_images) {
  ScString path(base_directory), marker;
  path.Append(directory.Data(), directory.Strlen());
  marker = path;
  marker.Append("album.ini");
  path.Append("*.jpg");
  return RsFile::FileExists(marker) && (!require_images || ScDirectory::DoesAnyEntryExistThatMatchesPattern(path));
}

// Display the one-based current image number and total, or clear the label for an empty album.
void NwGraphicsViewerObject::SetImageCountText(u32 current, u32 count) {
  if (image_count_label) {
    ScString text;
    if (count)
      text.Sprintf("(%u/%u)", current + 1, count);
    image_count_label->SetCaption(text);
  }
}

// Rebuild the album directory list, preserving the selected album and reporting changes to its contents.
u32 NwGraphicsViewerObject::UpdateListOfAlbumDirectories(bool *changed, bool require_images) {
  ScString empty, selected_name;
  ScDirectory directory(empty, false, false, false);
  std::list<ScString> names;
  std::vector<ScString> previous(album_directories);
  *changed = false;
  album_directories.clear();
  if (album_list)
    album_list->RemoveAllStrings();
  directory.ChangeDirectory(base_directory, false);
  directory.ReadDirectoryDirectoryEntriesIntoStringList(&names, 4);
  if (include_transfer_album) {
    ScString transfer(transfer_directory);
    u32 length = transfer.Strlen();
    if (length && (transfer.begin[length - 1] == '\\' || transfer.begin[length - 1] == '/'))
      transfer.Erase(length - 1, 1);
    bool found = false;
    for (const ScString &name : names) {
      if (name.IsEqual(&transfer)) {
        found = true;
        break;
      }
    }
    if (!found) {
      ScString path(base_directory);
      path.Append(transfer_directory.Data(), transfer_directory.Strlen());
      if (!ScDirectory::DoesDirectoryExist(path) && ScDirectory::CreateDirectoryA(path))
        names.push_back(transfer);
    }
  }
  ScDirectory::AlphabetizeEntryList(&names);
  for (ScString &name : names) {
    ScString display;
    u32 length = name.Strlen();
    if (length) {
      char last = name.begin[length - 1];
      if (last != '\\' && last != '/')
        name.Append("\\");
      if (ConvertAlbumDirectoryNameToAlbumName(name, &display) &&
          (AlbumDirectoryHasAlbumEntries(name, require_images) || name.IsEqual(&transfer_directory))) {
        album_directories.push_back(name);
        bool found = false;
        for (const ScString &old : previous) {
          if (old.IsEqual(&name)) {
            found = true;
            break;
          }
        }
        if (!found)
          *changed = true;
        if (name.IsEqual(&current_directory))
          selected_name = display;
        if (album_list)
          album_list->InsertString(display, -2);
      }
    }
  }
  if (album_directories.size() != previous.size())
    *changed = true;
  if (album_list && selected_name.Strlen()) {
    i32 selected;
    album_list->GetIndexFromString(&selected, selected_name);
    album_list->SetSelection(selected, false);
    album_list->PutIndexInView(selected);
  } else {
    current_directory.Erase(0, current_directory.Strlen());
    if (album_directories.empty()) {
      SetToNoImageFile(true);
    } else {
      current_directory = album_directories.front();
      SetImageCountText(0, static_cast<u32>(graphic_files.size()));
      ScString first;
      SetCurrentFile(first, true);
    }
    if (album_list) {
      album_list->SetSelection(0, false);
      album_list->PutIndexInView(0);
    }
  }
  return static_cast<u32>(album_directories.size());
}

// Locate the selected file case-insensitively and display its position in the image list.
void NwGraphicsViewerObject::SetImageCountText() {
  u8 index = 0;
  for (const ScString &name : graphic_files) {
    if (ScCompareNoCase(name.ToChar(), current_file.ToChar()) == 0)
      break;
    ++index;
  }
  SetImageCountText(index, static_cast<u32>(graphic_files.size()));
}

NwGraphicsViewerObject::NwGraphicsViewerObject() : transfer_directory("Transfer\\"), last_idle(0), idle_interval(30) {
  image_extensions.push_back(ScString(".jpg"));
  image_extensions.push_back(ScString(".jpeg"));
  image_extensions.push_back(ScString(".jfif"));
}

bool NwGraphicsViewerObject::DestroyGraphicsViewerWindow() {
  return !window || EndDialog(window, kUiFrameCloseButtonId);
}

// Notify the game, save viewer settings and post the modal result or close request for the window.
bool NwGraphicsViewerObject::EndDialog(WnWindow *target, u32 result) {
  if (!target)
    return false;
  WnWindowManager *manager = target->GetWindowManager();
  ApServices::BoxX()->Event(1011, 0, 0, 0, 0);
  SaveWindowSettingsToPrefs(target);
  if (target->GetFlag(kWnFlagModal))
    manager->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(target), result);
  else
    manager->PostMessageToTarget(target->GetParentWin(), kWnMsg_ChildDelete, reinterpret_cast<uptr>(target), 0, 0);
  return true;
}

// Notify the game and dispatch viewer navigation, deletion, close and preview-size buttons.
bool NwGraphicsViewerObject::OnButtonActivated(WnWindow *target, u32 identifier) {
  ApServices::BoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  switch (identifier) {
    case 100:
      GoToPreviousGraphic();
      break;
    case 101:
      GoToNextGraphic();
      break;
    case 102:
      DeleteCurrentGraphic();
      break;
    case 103:
      SaveDescriptionOfFile();
      EndDialog(target, kUiFrameCloseButtonId);
      break;
    case 108:
      if (preview)
        preview->SetSizeInOut(false);
      return true;
    case 109:
      if (preview)
        preview->SetSizeInOut(true);
      return true;
    case kUiFrameCloseButtonId:
      break;
    default:
      return false;
  }
  return true;
}

void NwGraphicsViewerObject::DisplayErrorMessage(const ScString &message, const ScString &title) {
  UiMessageBox *dialog;
  if (ApGetCom()->GetClassObject(kUiMessageBoxClassId, IID_UiMessageBox, reinterpret_cast<void **>(&dialog))) {
    u32 result = 0;
    dialog->Show(&result, message, title, 0x10000);
    dialog->Release();
  }
}

// Dispatch viewer window events and update the background and window geometry when the preview size changes.
bool NwGraphicsViewerObject::DoWinMsg(WnWindow *target, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if (first == 1)
      return OnButtonActivated(target, static_cast<u32>(second));
    if (first == 6 && album_list && second == album_list->AsIGZWin()->GetID()) {
      GoToAlbum(static_cast<u32>(album_list->GetSelection()));
      return true;
    }
    return false;
  }
  if (message == kWnMsg_KeyDown) {
    if (first == 27 || ((first == 13 || first == 115) && (second & 4))) {
      EndDialog(target, kUiFrameCloseButtonId);
      return true;
    }
    return false;
  }
  switch (message) {
  case kWnMsg_Init:
    return OnInit(target);
  case kWnMsg_Shutdown:
    return OnShutdown(target);
  case kWnMsg_ClientConstraints:
    return OnGetMinMaxInfo(target, reinterpret_cast<ScRect *>(first));
  case kWnMsg_ClientLayout:
    if (first)
      OnUserAreaChanged(target, reinterpret_cast<const ScRect *>(first));
    return true;
  case kWnMsg_BitmapViewSizeChanged:
    if (preview) {
      if (preview->fit_at_size_in)
        SetCurrentFile(current_file, false);
      if (second) {
        field32->SetImage(field24);
        field32->SetArea(6, 40, 302, 262);
        target->SetSize(512, 360);
        field60->GetWindow()->HideWindow();
      } else {
        if (!field28) {
          GxColorType format(GxColorType::Color::RGB16);
          UiImageHandle image(kApViewerExpandedFrameImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
          field28 = image.GetBuffer();
          if (field28)
            field28->AddRef();
        }
        field32->SetImage(field28);
        field32->SetArea(6, 40, 582, 470);
        target->SetSize(588, 476);
        field60->GetWindow()->ShowWindow();
      }
      field32->PullToFront();
      preview->PullToFront();
    }
    return true;
  default:
    return false;
  }
}

// Clear the viewer's lists and release its retained window, image and control interfaces in their original order.
bool NwGraphicsViewerObject::OnShutdown(WnWindow *) {
  if (!viewer_initialized)
    return true;
  viewer_initialized = false;
  graphic_files.clear();
  album_directories.clear();
  ScRelease(&window);
  ScRelease(&no_image);
  ScRelease(&field24);
  ScRelease(&field28);
  ScRelease(&field32);
  ScRelease(&preview);
  ScRelease(&album_list);
  ScRelease(&file_label);
  ScRelease(&image_count_label);
  ScRelease(&description_editor);
  ScRelease(&field56);
  ScRelease(&field60);
  return true;
}

NwGraphicsViewerObject::~NwGraphicsViewerObject() {
}

// Destroy the viewer at its final reference; otherwise return the decremented reference count.
u32 NwGraphicsViewerObject::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

// Clear the selected file and description, optionally displaying the viewer's no-image resource.
void NwGraphicsViewerObject::SetToNoImageFile(bool display) {
  current_file.Erase(0, current_file.Strlen());
  if (preview) {
    if (display) {
      if (!no_image) {
        GxColorType format(GxColorType::Color::RGB16);
        UiImageHandle handle(kApViewerNoImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
        no_image = handle.GetBuffer();
        if (no_image) {
          no_image->AddRef();
          no_image->SetTransparency(ApGetUiResources()->GetColor(kWnColorUiTransparency));
        }
      }
      preview->SetImage(no_image);
    } else {
      preview->ClearBMP();
    }
  }
  ScString empty;
  if (file_label)
    file_label->SetCaption(empty);
  if (description_editor)
    description_editor->SetText(empty);
  SetImageCountText(0, 0);
}

// Traverse the image list; the retail debug operation emits no output and returns success.
bool NwGraphicsViewerObject::DebugOutputImageFileList() {
  for (auto it = graphic_files.begin(); it != graphic_files.end(); ++it) {
  }
  return true;
}

// Save edited description text for the selected image when it differs from the last saved text.
bool NwGraphicsViewerObject::SaveDescriptionOfFile() {
  if (!description_editor || !transfer_directory.Strlen() || !current_file.Strlen())
    return false;
  ScString text, path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  path.Append(current_file.Data(), current_file.Strlen());
  description_editor->GetText(&text);
  if (!text.IsEqual(&last_description)) {
    last_description = text;
    SaveDescriptionOfFile(path, text);
  }
  return true;
}

// Show the named file from the current album and update its information and description controls.
bool NwGraphicsViewerObject::GoToGraphic(const ScString &name) {
  ScString path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  path.Append(name.Data(), name.Strlen());
  if (!RsFile::FileExists(path))
    return false;
  current_file = name;
  preview->SetBMPFromFile(path);
  current_path = path;
  if (file_label) {
    ScString information;
    GetInformationOfFile(path, &information);
    file_label->SetCaption(information);
  }
  if (description_editor) {
    GetDescriptionOfFile(path, &last_description);
    description_editor->SetText(last_description);
  }
  SetImageCountText();
  return true;
}

bool NwGraphicsViewerObject::SetInitialFile() {
  if (!base_directory.Strlen() || album_directories.empty())
    return true;
  if (!current_directory.Strlen() && album_list) {
    i32 selected = album_list->GetSelection();
    if (selected < 0 || static_cast<u32>(selected) >= album_directories.size())
      album_list->SetSelection(0, true);
  }
  ScString path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  path.Append(current_file.Data(), current_file.Strlen());
  if (graphic_files.empty() || !current_file.Strlen() || !RsFile::FileExists(path)) {
    RefreshGraphicList(nullptr);
    if (graphic_files.empty())
      return false;
    current_file = graphic_files.front();
  }
  ScString selected(current_file);
  current_file.Erase(0, current_file.Strlen());
  SetImageCountText(0, static_cast<u32>(graphic_files.size()));
  return SetCurrentFile(selected, false);
}

// Move to the next image, wrapping to the first and refreshing an empty or unselected list.
bool NwGraphicsViewerObject::GoToNextGraphic() {
  if (!preview)
    return false;
  ScAutoCursor cursor(14);
  if (!current_file.Strlen() || graphic_files.empty()) {
    RefreshGraphicList(nullptr);
    if (graphic_files.empty()) {
      SetToNoImageFile(true);
      return false;
    }
    current_file = graphic_files.front();
  }
  SaveDescriptionOfFile();
  u32 index = 0;
  auto item = graphic_files.begin();
  while (item != graphic_files.end()) {
    ++index;
    bool selected = item->IsEqual(&current_file);
    ++item;
    if (selected)
      break;
  }
  if (item == graphic_files.end()) {
    item = graphic_files.begin();
    index = 0;
  }
  if (item == graphic_files.end())
    return false;
  SetImageCountText(index, static_cast<u32>(graphic_files.size()));
  return GoToGraphic(*item);
}

// Move to the previous image, wrapping to the last and using forward navigation for an empty selection.
bool NwGraphicsViewerObject::GoToPreviousGraphic() {
  if (!preview)
    return false;
  ScAutoCursor cursor(14);
  if (!current_file.Strlen() || graphic_files.empty())
    return GoToNextGraphic();
  SaveDescriptionOfFile();
  u32 index = static_cast<u32>(graphic_files.size() - 1);
  auto item = graphic_files.rbegin();
  while (item != graphic_files.rend()) {
    --index;
    bool selected = item->IsEqual(&current_file);
    ++item;
    if (selected)
      break;
  }
  if (item == graphic_files.rend()) {
    item = graphic_files.rbegin();
    index = static_cast<u32>(graphic_files.size() - 1);
  }
  if (item == graphic_files.rend())
    return false;
  SetImageCountText(index, static_cast<u32>(graphic_files.size()));
  return GoToGraphic(*item);
}

// Delete the selected image and metadata, then update the selected album and navigation controls.
bool NwGraphicsViewerObject::DeleteCurrentGraphic() {
  if (!preview || !current_file.Strlen())
    return false;
  ScString selected(current_file), path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  path.Append(selected.Data(), selected.Strlen());
  if (!RsFile::Delete(path)) {
    UiTextHandle message(kRsSupplementalGameCannotDeleteSnapshotText, kRsSupplementalGameTextGroup);
    ScString title, text(message.Val());
    DisplayErrorMessage(text, title);
    return false;
  }
  ScString metadata;
  GetPathForInfoFile(path, &metadata);
  RsFile::Delete(metadata);
  GetPathForDescriptionFile(path, &metadata);
  RsFile::Delete(metadata);
  if (graphic_files.size() <= 1) {
    SetToNoImageFile(true);
    if (ensure_transfer_directory && !current_directory.IsEqual(&transfer_directory) && album_list)
      DeleteAlbum(static_cast<u32>(album_list->GetSelection()));
  } else {
    GoToNextGraphic();
    for (auto item = graphic_files.begin(); item != graphic_files.end();) {
      if (item->IsEqual(&selected))
        item = graphic_files.erase(item);
      else
        ++item;
    }
    RefreshGraphicList(nullptr);
    if (graphic_files.size() <= 1 && can_navigate) {
      can_navigate = false;
      UiEnableButton(GetIGZWin(), 100, false);
      UiEnableButton(GetIGZWin(), 101, false);
    }
  }
  return true;
}

// Save pending edits, select an existing image and update the preview, text and navigation controls.
bool NwGraphicsViewerObject::SetCurrentFile(const ScString &name, bool refresh) {
  ScString selected(name), path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  path.Append(selected.Data(), selected.Strlen());
  if (selected.Strlen() && !RsFile::FileExists(path))
    return false;
  ScAutoCursor cursor(14);
  SaveDescriptionOfFile();
  if (refresh)
    RefreshGraphicList(nullptr);
  if (!selected.Strlen()) {
    if (graphic_files.empty())
      return false;
    selected = graphic_files.front();
    path.Append(selected.Data(), selected.Strlen());
  }
  current_file = selected;
  preview->SetBMPFromFile(path);
  current_path = path;
  if (file_label) {
    ScString information;
    GetInformationOfFile(path, &information);
    file_label->SetCaption(information);
  }
  if (description_editor) {
    GetDescriptionOfFile(path, &last_description);
    description_editor->SetText(last_description);
  }
  UiEnableButton(GetIGZWin(), 102, !graphic_files.empty());
  bool navigation = graphic_files.size() > 1;
  if (navigation != can_navigate) {
    can_navigate = navigation;
    UiEnableButton(GetIGZWin(), 100, navigation);
    UiEnableButton(GetIGZWin(), 101, navigation);
  }
  return true;
}

bool NwGraphicsViewerObject::RefreshGraphicList(bool *changed) {
  WnWindow *viewer_window = GetIGZWin();
  if (!base_directory.Strlen() || (viewer_window && !viewer_window->IsVisible()))
    return true;
  ScString empty;
  ScDirectory directory(empty, false, false, false);
  std::list<ScString> previous;
  if (!current_directory.Strlen()) {
    if (!album_list)
      return false;
    if (!album_list->GetLineCount()) {
      SetToNoImageFile(true);
      return false;
    }
    current_directory = album_directories[album_list->GetSelection()];
  }
  if (changed) {
    *changed = false;
    previous = graphic_files;
  }
  graphic_files.clear();
  ScString path(base_directory);
  path.Append(current_directory.Data(), current_directory.Strlen());
  directory.ChangeDirectory(path, false);
  directory.ReadDirectoryFileEntriesIntoStringList(&graphic_files);
  graphic_files.sort([](const ScString &first, const ScString &second) {
    return std::strcmp(first.ToChar(), second.ToChar()) < 0;
  });
  if (graphic_files.empty()) {
    if (album_list)
      DeleteAlbum(static_cast<u32>(album_list->GetSelection()));
    SetToNoImageFile(true);
  } else {
    for (auto item = graphic_files.begin(); item != graphic_files.end();) {
      ScString extension;
      RsFile::GetPathExtension(*item, &extension);
      bool found = false;
      for (const ScString &accepted : image_extensions) {
        u32 length = accepted.Strlen() < extension.Strlen() ? accepted.Strlen() : extension.Strlen();
        if (ScCompareNoCase(accepted.ToChar(), extension.ToChar(), length) == 0) {
          found = true;
          break;
        }
      }
      if (found)
        ++item;
      else
        item = graphic_files.erase(item);
    }
    SetImageCountText();
  }
  if (changed) {
    *changed = previous.size() != graphic_files.size();
    if (!*changed) {
      auto old_item = previous.begin(), new_item = graphic_files.begin();
      while (old_item != previous.end() && old_item->IsEqual(&*new_item)) {
        ++old_item;
        ++new_item;
      }
      *changed = old_item == previous.end();
    }
  }
  return true;
}

// Create the named album directory and its album.ini file, then refresh the list of album directories.
bool NwGraphicsViewerObject::CreateAlbum(const ScString &name, u32 index) {
  if (AlbumExists(name, index))
    return true;
  ScString directory_name, directory(base_directory), marker;
  directory_name.Sprintf("%s--%u\\", name.ToChar(), index);
  directory.Append(directory_name.Data(), directory_name.Strlen());
  marker = directory;
  marker.Append("album.ini");
  if (!ScDirectory::CreateDirectoryA(directory))
    return false;
  RsFile::FileCreate(marker, false);
  bool changed;
  UpdateListOfAlbumDirectories(&changed, false);
  return true;
}

// Copy an image into the transfer album, optionally deleting the source after the copy attempt.
bool NwGraphicsViewerObject::AddNewTransferGraphic(const ScString &path, bool remove_source) {
  ScString destination(base_directory), source_directory, filename;
  destination.Append(transfer_directory.Data(), transfer_directory.Strlen());
  RsFile::GetPathDirectory(path, &source_directory);
  if (source_directory.IsEqual(&destination))
    return true;
  if (!ScDirectory::DoesDirectoryExist(destination) && !ScDirectory::CreateDirectoryA(destination))
    return false;
  RsFile::GetPathName(path, &filename, true);
  destination.Append(filename.Data(), filename.Strlen());
  bool copied = RsFile::Copy(path, destination, true);
  if (remove_source)
    RsFile::Delete(path);
  return copied;
}

// Place an image and its metadata in the city's album, then select it in the viewer.
bool NwGraphicsViewerObject::AddNewGraphic(const ScString &path, const ScString &information,
    const ScString &description, const ScString &city, u32 identifier, bool remove_source) {
  ScString safe_city;
  CityNameToAlbumName(city, &safe_city);
  if (!AlbumExists(safe_city, identifier) && !CreateAlbum(safe_city, identifier)) {
    if (remove_source)
      RsFile::Delete(path);
    return false;
  }
  ScString source_directory, current_album(base_directory), album_name, destination(base_directory), filename, target;
  RsFile::GetPathDirectory(path, &source_directory);
  current_album.Append(current_directory.Data(), current_directory.Strlen());
  album_name.Sprintf("%s--%u\\", safe_city.ToChar(), identifier);
  destination.Append(album_name.Data(), album_name.Strlen());
  bool same_album = source_directory.IsEqual(&destination);
  source_directory.IsEqual(&current_album);
  if (same_album) {
    RsFile::GetPathName(path, &filename, true);
    target = path;
  } else {
    if (!GetNewTempFileName(destination, &filename))
      return false;
    target = destination;
    target.Append(filename.Data(), filename.Strlen());
    if (!RsFile::Copy(path, target, true))
      return false;
    if (remove_source)
      RsFile::Delete(path);
  }
  SaveInformationOfStandardFile(target, information.Strlen() ? information : filename);
  if (description.Strlen())
    SaveDescriptionOfFile(target, description);
  GoToAlbum(safe_city, identifier, true);
  RefreshGraphicList(nullptr);
  GoToGraphic(filename);
  return true;
}

// Select an album by sanitized city name and identifier, optionally creating its directory first.
bool NwGraphicsViewerObject::GoToAlbum(const ScString &name, u32 index, bool create) {
  ScString sanitized;
  CityNameToAlbumName(name, &sanitized);
  if (!AlbumExists(sanitized, index)) {
    if (!create)
      return false;
    CreateAlbum(sanitized, index);
  }
  if (AlbumExists(sanitized, index)) {
    ScString directory;
    directory.Sprintf("%s--%u\\", sanitized.ToChar(), index);
    u32 selected;
    if (ConvertAlbumDirectoryNameToAlbumIndex(directory, &selected))
      return GoToAlbum(selected);
  }
  return false;
}

bool NwGraphicsViewerObject::GoToAlbum(const ScString &name) {
  i32 index;
  return album_list && album_list->GetIndexFromString(&index, name) && GoToAlbum(static_cast<u32>(index));
}

// Select an indexed album, refresh its graphics and display the first file or the empty-image resource.
bool NwGraphicsViewerObject::GoToAlbum(u32 index) {
  if (index >= album_directories.size())
    return false;
  SaveDescriptionOfFile();
  if (album_list->GetSelection() != static_cast<i32>(index))
    album_list->SetSelection(static_cast<i32>(index), false);
  current_directory = album_directories[index];
  RefreshGraphicList(nullptr);
  if (graphic_files.empty()) {
    SetToNoImageFile(true);
  } else {
    SetImageCountText(0, static_cast<u32>(graphic_files.size()));
    SetCurrentFile(graphic_files.front(), false);
  }
  return true;
}

bool NwGraphicsViewerObject::DeleteAlbum(const ScString &name) {
  i32 index;
  return album_list && album_list->GetIndexFromString(&index, name) && DeleteAlbum(static_cast<u32>(index));
}

// Remove the album's image and metadata files, remove an empty directory and refresh the album list.
bool NwGraphicsViewerObject::DeleteAlbum(u32 index) {
  if (index >= album_directories.size())
    return false;
  ScString path(base_directory), empty;
  path.Append(album_directories[index].Data(), album_directories[index].Strlen());
  ScDirectory directory(empty, false, false, false);
  directory.ChangeDirectory(path, false);
  const char *patterns[] = {"*.jpg", "*.info", "*.txt"};
  std::list<ScString> entries;
  for (const char *pattern : patterns) {
    entries.clear();
    ScString filter(pattern);
    directory.SetNewDirectoryEntryFilter(filter, false);
    directory.ReadDirectoryFileEntriesIntoStringList(&entries);
    for (const ScString &entry : entries) {
      ScString file(path);
      file.Append(entry.Data(), entry.Strlen());
      RsFile::Delete(file);
    }
  }
  ScString marker(path);
  marker.Append("album.ini");
  RsFile::Delete(marker);
  entries.clear();
  ScString all("*.*");
  directory.SetNewDirectoryEntryFilter(all, false);
  directory.ReadDirectoryFileEntriesIntoStringList(&entries);
  if (entries.empty())
    ScDirectory::RemoveDirectoryA(path);
  bool changed;
  UpdateListOfAlbumDirectories(&changed, true);
  return false;
}

// Navigate to the current city's album, allowing its directory to be created when necessary.
bool NwGraphicsViewerObject::GoToAlbumForCurrentCity(bool) {
  CtCity *city = ApGetCity();
  if (!city)
    return false;
  ScString name(*city->GetCityName());
  return GoToAlbum(name, city->GetCityID(), true);
}

// Choose the transfer or standard metadata reader according to the selected album.
bool NwGraphicsViewerObject::GetInformationOfFile(const ScString &path, ScString *out) {
  if (IsCurrentAlbumDirectoryTransferDirectory())
    return GetInformationOfTransferFile(path, out);
  return GetInformationOfStandardFile(path, out);
}

// Read transfer-image information, falling back to a description parsed from its file name.
bool NwGraphicsViewerObject::GetInformationOfTransferFile(const ScString &path, ScString *out) {
  ScString filename;
  RsFile::GetPathName(path, &filename, true);
  *out = filename;
  void *data;
  u32 size;
  if (WnAppBitmapView::GetCustomDataFromFile(path, &data, &size) && size < 50000) {
    out->FromChar(static_cast<const char *>(data), size);
    DecryptData(*out, out);
    WnAppBitmapView::FreeMemoryStatic(data);
  } else if (!GetInformationOfStandardFile(path, out)) {
    std::string text(filename.Data(), filename.Strlen());
    usize first = text.find("--", 1);
    if (first != std::string::npos && text.size() > first + 5) {
      char type = text[first + 2];
      if (type == 'f' || type == 's' || type == 'v') {
        usize next = text.find("--", first + 3);
        if (next != std::string::npos && next > first + 3) {
          std::string prefix = text.substr(0, first), detail = text.substr(first + 3, next - first - 3);
          out->Sprintf("%s\n%s\n%s", text.c_str(), detail.c_str(), prefix.c_str());
          return true;
        }
      }
    }
    out->Sprintf("%s", text.c_str());
  }
  return true;
}

// Read and decrypt the image's information file, then format recognized city fields with localized labels.
bool NwGraphicsViewerObject::GetInformationOfStandardFile(const ScString &path, ScString *out) {
  ScString metadata_path;
  GetPathForInfoFile(path, &metadata_path);
  out->Erase(0, out->Strlen());
  RsFile file(metadata_path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  ScString contents;
  char buffer[512];
  u32 count = sizeof(buffer);
  while (file.Read(buffer, &count) && count) {
    contents.Append(buffer, count);
    count = sizeof(buffer);
  }
  file.Close();
  DecryptData(contents, &contents);
  ScString tokens[10];
  for (ScString &token : tokens)
    token = contents.SplitToken("\n");
  ScString marker("SanityCheck");
  if (tokens[0].IsEqual(&marker)) {
    ScString formatted, date_text;
    for (u32 index = 0; index < 5; ++index) {
      UiTextHandle text(409 + index, kRsSupplementalGameTextGroup);
      ScString format(text.Val());
      const ScString *value;
      if (index == 0) {
        value = &tokens[2];
      } else if (index == 1) {
        value = &tokens[3];
      } else if (index == 2) {
        CtDate date;
        date.Set(static_cast<u32>(std::atoi(tokens[4].ToChar())));
        ScLanguageUtility *language = ApGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
        language->MakeDateString(static_cast<i32>(date.Month()), static_cast<i32>(date.DayOfMonth()), date.Year(),
            &date_text, ScLanguageUtility::DateFormat::NumericMonthDayYear);
        value = &date_text;
      } else if (index == 3) {
        value = &tokens[5];
      } else {
        i32 amount = std::atoi(tokens[6].ToChar());
        ScLanguageUtility *language = ApGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
        language->MakeMoneyString(amount, &tokens[6]);
        value = &tokens[6];
      }
      formatted.Sprintf(format.ToChar(), value->ToChar());
      out->Append(formatted.Data(), formatted.Strlen());
      out->Append("\n");
    }
  }
  return true;
}

// Create a temporary image name inside the selected album and return its full path.
bool NwGraphicsViewerObject::GetNewTempFileNameForCurrentAlbum(ScString *out) {
  ScString directory(base_directory), name;
  directory.Append(current_directory.Data(), current_directory.Strlen());
  if (!GetNewTempFileName(directory, &name))
    return false;
  *out = directory;
  out->Append(name.Data(), name.Strlen());
  return true;
}

// Reserve a new image name from the city, localized game date and successive low-word time values.
bool NwGraphicsViewerObject::GetNewTempFileName(const ScString &directory, ScString *out) {
  u32 sequence = static_cast<u32>(std::time(nullptr)) & 0xffffu;
  CtCity *city = ApGetCity();
  ScString city_name(*city->GetCityName()), safe_name, date_text, prefix;
  if (city_name.Strlen() > 20)
    city_name.Erase(20, city_name.Strlen() - 20);
  CityNameToAlbumName(city_name, &safe_name);
  CtClockInterface *clock = city->GetSimulationClock();
  if (clock) {
    ScLanguageUtility *language = ApGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (language) {
      CtDate date;
      clock->GetCurrentClockDate(&date);
      language->MakeDateString(static_cast<i32>(date.Month()), static_cast<i32>(date.DayOfMonth()), date.Year(),
          &date_text, ScLanguageUtility::DateFormat::AbbrevAlphaMonthDayYear);
    }
  }
  prefix = safe_name;
  prefix.Append("-");
  prefix.Append(date_text.Data(), date_text.Strlen());
  RsFile file;
  for (u32 attempts = 0; attempts < 10000; ++attempts, ++sequence) {
    out->Sprintf("%s-%u.jpg", prefix.ToChar(), sequence);
    ScString full(directory);
    full.Append(out->Data(), out->Strlen());
    file.SetName(full);
    if (file.Open(kRsFileAccessWrite, kRsFileCreateNew, kRsFileShareRead)) {
      file.Close();
      return true;
    }
  }
  return false;
}
