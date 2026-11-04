#include "application/application_core.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "resources/api.h"
#include "resources/file.h"
#undef LoadCursor
#undef SetCursor
#include "ui/ui_file_cursor.h"
#include "ui/ui_window.h"
#include "window/api.h"
#include "window/cursor_platform.h"
#undef LoadCursor
#undef SetCursor
#include <assert.h>
#include <string.h>

// Copy the concrete filename before initializing the owned image/handle, capture graphics when available, then load.
UiFileCursor::UiFileCursor(const ScString &name)
    : hardware(false), graphics(nullptr), filename(name), image(nullptr), tail{0, 0, 0}, cursor(nullptr) {
  if (ApGetFramework()) {
    graphics = ApGetGraphics();
    if (graphics)
      hardware = graphics->DoesSystemHaveCursorSupport();
  }
  hardware = WnPlatformFileCursorHardware(hardware);
  LoadCursor(&filename);
}

// Clear the Windows shared handle or destroy the SDL-owned cursor, then release image ownership.
UiFileCursor::~UiFileCursor() {
  if (cursor) {
    WnPlatformDestroyCursor(cursor);
    cursor = nullptr;
  }
  ScRelease(&image);
}

// Clear the caller's output before either supported cursor IID, then publish this interface before virtual retention.
bool UiFileCursor::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnCursor);
  HANDLE_QUERY_INTERFACE(IID_WnCursor, WnCursor);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiFileCursor::AddRef() {
  return ++tail.references;
}

u32 UiFileCursor::Release() {
  u32 result = --tail.references;
  if (!result)
    delete this;
  return result;
}

// Hardware loads CUR directly. Software replaces the last three filename bytes, loads/falls back to an image, then reads BMP hotspots.
bool UiFileCursor::LoadCursor(ScStringView *name) {
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
  u32 size = filename.Strlen();
  assert(size >= 3);
  memcpy(filename.begin + size - 3, "bmp", 3);
  RsImageOptions *reader = nullptr;
  if (ApGetCom()->GetClassObject(kRsImageFactoryClassId, IID_RsImageOptions, reinterpret_cast<void **>(&reader))) {
    const GxColorType format(GxColorType::Color::RGB16);
    reader->LoadFile(&filename, &image, 1, &format, 0);
    reader->Release();
    reader = nullptr;
  }
  if (!image) {
    ApGetGraphics()->CreateBuffer(&image);
    image->Init(32, 32, GxColorType(static_cast<GxColorType::Color>(4)), nullptr, GxDepthType(GxDepthType::Depth::None),
        GxVramRequest::VideoMemoryLowPriority, false);
  }
  WnResources *resources = ApGetUiResources();
  GxBuffer *current = image;
  current->SetTransparency(resources->GetColor(kWnColorUiTransparency));
  RsFile file(filename);
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    i32 length = (i32)file.Size();
    file.SeekBegin();
    u32 argument = (u32)(uptr)name;
    file.Read(reinterpret_cast<u8 *>(&argument) + 2, 2);
    if ((u16)(argument >> 16) == 0x4d42) {
      file.SeekAbsolute(2);
      u32 bitmap_size, read = 4;
      file.Read(&bitmap_size, &read);
      assert(read == 4);
      if (length >= (i32)(bitmap_size + 8u)) {
        file.SeekAbsolute((i32)bitmap_size);
        file.Read(&tail, 4);
        file.Read(reinterpret_cast<u8 *>(&tail) + 2, 4);
        i16 x = tail.x;
        i32 width = image->Width();
        if (x >= width || x < 0)
          tail.x = 0;
        i16 y = tail.y;
        i32 height = image->Height();
        if (y >= height || y < 0)
          tail.y = 0;
      }
    }
  }
  return image != nullptr;
}

// Prefer the existing platform handle when allowed; otherwise replace the software cursor image at hotspot zero.
void UiFileCursor::SetCursor() {
  if (cursor) {
    if (WnPlatformSelectsCursorDirectly() || hardware || !graphics || graphics->DoesSystemHaveCursorSupport()) {
      WnPlatformSetCursor(cursor, graphics);
      return;
    }
  } else if (!graphics)
    return;
  WnPlatformClearCursorOverlay();
  graphics->SetCursorBuffer(image, 0, 0);
}

void UiFileCursor::GetHotSpot(i32 *x, i32 *y) {
  *x = tail.x;
  *y = tail.y;
}
