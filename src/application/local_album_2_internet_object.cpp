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

// Return the viewer's separate graphics-viewer interface without acquiring a reference.
NwGraphicsViewer *NwTransferObject::GetImageBrowser() {
  return viewer ? static_cast<NwGraphicsViewer *>(viewer) : nullptr;
}

bool NwTransferObject::ProcessSendRecvRequest(bool, TransferType, const ScStringView &,
    const ScStringView &, u32, u32, u32) {
  return false;
}

// Create or close the image browser and select its requested file or album when showing it.
bool NwTransferObject::ShowImageBrowser(bool show, bool transfer, ScStringView *name, bool visible) {
  if (!show) {
    if (viewer) {
      viewer->DestroyGraphicsViewerWindow();
      viewer->Release();
      viewer = nullptr;
    }
    return true;
  }
  if (!parent)
    return false;
  if (!viewer) {
    viewer = ScAddRefNotNull(new NwGraphicsViewerObject);
    WnWindow *created = nullptr;
    if (viewer->CreateGraphicsViewerWindow(&created)) {
      parent->ChildAdd(created);
      created->Release();
    }
  }
  WnWindow *browser = viewer->GetIGZWin();
  if (!browser) {
    viewer->CreateGraphicsViewerWindow(&browser);
    if (browser) {
      parent->ChildAdd(browser);
      browser->Release();
    }
  }
  if (browser) {
    if (visible)
      browser->ShowWindow();
    else
      browser->HideWindow();
    browser->PullToFront();
    if (name) {
      ScString selected(*name);
      viewer->SetCurrentFile(selected, true);
    } else if (transfer) {
      viewer->GoToTransferAlbum();
    } else {
      viewer->GoToAlbumForCurrentCity(true);
    }
  }
  return true;
}

bool NwTransferObject::IsImageBrowserShown() {
  if (!viewer)
    return false;
  WnWindow *window = viewer->GetIGZWin();
  return window && window->IsVisible();
}

void NwTransferObject::DoIdleProcessing() {
  if (viewer)
    viewer->DoIdleProcessing();
}

// Load the watermark on first use and blend it into the lower-right corner of the supplied image.
bool ApSnapshotTaker::AddWatermarkToBuffer(GxBuffer *buffer) {
  if (!field180) {
    WnResources *resources = ApGetUiResources();
    if (resources) {
      GxColorType format(GxColorType::Color::RGB16);
      UiImageHandle image(kApSnapshotWatermarkImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
      field180 = image.GetBuffer();
      if (field180) {
        field180->AddRef();
        field180->SetTransparency(resources->GetColor(kWnColorUiTransparency));
      }
    }
  }
  if (!field180)
    return false;
  u32 width = field180->Width(), height = field180->Height();
  ScRect source{0, 0, static_cast<i32>(width), static_cast<i32>(height)};
  ScPoint target{static_cast<i32>(buffer->Width() - width - 4), static_cast<i32>(buffer->Height() - height - 4)};
  return ApServices::AB()->AlphaBltConstAlpha(field180, buffer, 0.7f, source, target, nullptr);
}

bool ApSnapshotTaker::GetGraphicDescriptionForCurrentCity(ScString *description) {
  if (description->Strlen())
    description->Erase(0, description->Strlen());
  return true;
}

void WnAppBitmapView::SetBMP(GxBuffer *buffer) {
  SetImage(buffer);
}

WnAppBitmapViewExtra::WnAppBitmapViewExtra()
    : size_in(true), size_in_area{0, 0, 16, 16}, size_out_area{0, 0, 32, 32},
      double_click_timer(ScStopwatch::Milliseconds, 0) {
}

WnAppBitmapViewExtra::~WnAppBitmapViewExtra() {
}

// Initialize the bitmap viewer and select its initial preview mode.
bool WnAppBitmapViewExtra::Init() {
  WnAppBitmapView::Init();
  SetBitmapFlag(2, true);
  SetSizeInOut(true);
  return true;
}

void WnAppBitmapViewExtra::SetSizeInAndOutAreas(const ScRect &in, const ScRect &out) {
  size_in_area = in;
  size_out_area = out;
}

// Remember the compact-view fit preference and apply the scroll-lock override in expanded view.
void WnAppBitmapViewExtra::SetShouldFitImageAtSizeIn(bool enabled) {
  fit_at_size_in = enabled;
  bool scroll_locked = WnIsToggleKeySet(145);
  SetShouldFitImageInView(size_in || !scroll_locked ? enabled : false);
}

// Switch preview areas, update image interaction settings and notify the window's target.
bool WnAppBitmapViewExtra::SetSizeInOut(bool in) {
  if (in != size_in) {
    size_in = in;
    PullToFront();
    bool scroll_locked = WnIsToggleKeySet(145);
    SetShouldFitImageInView(size_in || !scroll_locked ? fit_at_size_in : false);
    SetAlignment(static_cast<Alignment>(1));
    EnableZooming(false);
    EnableScrolling(false);
    const ScRect &target_area = size_in ? size_in_area : size_out_area;
    SetArea(target_area.left, target_area.top, target_area.right, target_area.bottom);
    SendMsg(notification_target, kWnMsg_BitmapViewSizeChanged, GetID(), size_in, 0);
  }
  return true;
}

bool WnAppBitmapViewExtra::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  if (double_click_timer.IsTimerRunning() && double_click_timer.GetElapsedTime() < 500) {
    double_click_timer.Reset();
    SetSizeInOut(!size_in);
    return true;
  }
  double_click_timer.Restart();
  return WnAppBitmapView::OnMouseDownL(x, y, modifiers);
}

bool WnAppBitmapViewExtra::DoMessage(ScMessage *message) {
  return WnAppBitmapView::DoMessage(message);
}

void WnAppBitmapView::FreeMemory(void *memory) {
  delete[] static_cast<u8 *>(memory);
}

void WnAppBitmapView::FreeMemoryStatic(void *memory) {
  delete[] static_cast<u8 *>(memory);
}

bool WnAppBitmapView::WriteBMPToFile(u32 format, ScStringView *path, u32 quality, const ScString &data) {
  return WriteBMPToFile(image, format, path, quality, data);
}

static GxUnpackFunction GxScaleUnpack;
static GxPackFunction GxScalePack;

// A filter contribution stores a reflected source coordinate and its double-precision weight.
struct GxScaleTap {
  i32 coordinate;
  f64 weight;
};

// Each output coordinate points at its consecutive filter contributions.
struct GxScaleSpan {
  i32 count;
  GxScaleTap *taps;
};

// Scale a bitmap through the reconstructed two-pass cubic filter and its live color converters.
// Release acquired interfaces normally; installed pixel parity remains under verification.
GxBuffer *GxScaleBitmapInterpolated(GxBuffer *buffer, f32 horizontal, f32 vertical, const ScRect *area) {
  if (area && (area->left >= area->right || area->top >= area->bottom))
    return nullptr;
  GxScaleUnpack = buffer->Get16ToRGBConverterPtr();
  GxScalePack = buffer->GetRGBTo16ConverterPtr();
  if (!GxScaleUnpack || !GxScalePack)
    return nullptr;
  bool has_transparency = buffer->GetTransparency() != 0;
  u32 transparent = 0;
  u8 transparent_red = 0, transparent_green = 0, transparent_blue = 0;
  if (has_transparency) {
    buffer->GetTransparentColor(&transparent);
    GxScaleUnpack(static_cast<u16>(transparent), &transparent_red, &transparent_green, &transparent_blue);
  }
  i32 width, height;
  if (area) {
    width = area->right - area->left;
    height = area->bottom - area->top;
  } else {
    width = static_cast<i32>(buffer->Width());
    height = static_cast<i32>(buffer->Height());
  }
  f64 rounded_width = std::ceil(static_cast<f64>(width) * horizontal);
  f64 rounded_height = std::ceil(static_cast<f64>(height) * vertical);
  i32 output_width = WnBitmapInteger(rounded_width + (rounded_width >= 0.0 ? 0.5 : -0.5));
  i32 output_height = WnBitmapInteger(rounded_height + (rounded_height >= 0.0 ? 0.5 : -0.5));
  if (!width || !height)
    return nullptr;
  f64 horizontal_ratio = static_cast<f64>(output_width) / static_cast<f32>(width);
  f32 vertical_ratio = static_cast<f32>(static_cast<f64>(output_height) / static_cast<f32>(height));
  if (horizontal_ratio == 0.0 || vertical_ratio == 0.0f || std::isnan(horizontal_ratio) || std::isnan(vertical_ratio))
    return nullptr;
  GxBuffer *result = nullptr, *intermediate = nullptr;
  ApGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&result));
  if (!result)
    return nullptr;
  if (!result->Init(output_width, output_height, GxColorType(GxColorType::Color::RGB16), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
    result->Release();
    return nullptr;
  }
  ApGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&intermediate));
  if (!intermediate || !intermediate->Init(output_width, height, GxColorType(GxColorType::Color::RGB16), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
    if (intermediate)
      intermediate->Release();
    result->Release();
    return nullptr;
  }
  i32 maximum = output_width >= output_height ? output_width : output_height;
  f32 ratio = static_cast<f32>(horizontal_ratio);
  f32 radius = ratio >= 1.0f ? 1.0f : 1.0f / ratio;
  i32 capacity = WnBitmapInteger(static_cast<f64>(radius) + 1.0 + radius + 1.0);
  // Successful filter construction requires the original thirty-two-bit allocation byte counts not to wrap.
  assert(maximum >= 0 && capacity >= 0 && static_cast<u32>(maximum) <= UINT32_MAX / 8u &&
      static_cast<u64>(maximum) * static_cast<u64>(capacity) <= UINT32_MAX / 16u);
  GxScaleSpan *spans = new GxScaleSpan[maximum];
  GxScaleTap *taps = new GxScaleTap[static_cast<usize>(maximum) * capacity];
  for (i32 output = 0; output < output_width; ++output) {
    GxScaleSpan *span = &spans[output];
    span->count = 0;
    span->taps = taps + static_cast<usize>(output) * capacity;
    f64 precise_position = static_cast<f64>(output) / ratio;
    f32 position = static_cast<f32>(precise_position);
    f64 lower = std::ceil(precise_position - radius);
    f64 upper = std::floor(static_cast<f64>(position) + radius);
    i32 first = WnBitmapInteger(lower + (lower >= 0.0 ? 0.5 : -0.5));
    i32 last = WnBitmapInteger(upper + (upper >= 0.0 ? 0.5 : -0.5));
    assert(first != INT_MIN && last != INT_MAX);
    for (i32 input = first; input <= last; ++input) {
      f64 distance = std::fabs((static_cast<f64>(position) - input) / radius);
      f64 weight = distance >= 1.0 ? 0.0 : distance * ((distance + distance - 3.0) * distance) + 1.0;
      assert(static_cast<usize>(output) * capacity + span->count < static_cast<usize>(maximum) * capacity);
      GxScaleTap *tap = &span->taps[span->count++];
      tap->coordinate = input < 0 ? -input : input < width ? input :
          static_cast<i32>(static_cast<u32>(width) * 2u - static_cast<u32>(input) - 1u);
      tap->weight = weight / radius;
    }
  }
  if (!buffer->Lock(GxLockFlags::Bits)) {
    delete[] spans;
    delete[] taps;
    intermediate->Release();
    result->Release();
    return nullptr;
  }
  if (!intermediate->Lock(GxLockFlags::Bits)) {
    buffer->Unlock(GxLockFlags::Bits);
    delete[] spans;
    delete[] taps;
    intermediate->Release();
    result->Release();
    return nullptr;
  }
  u16 *destination = static_cast<u16 *>(intermediate->GetColorSurfaceBits());
  u16 *source = static_cast<u16 *>(buffer->GetColorSurfaceBits());
  u32 source_stride = buffer->GetColorSurfaceStride() >> 1;
  u32 destination_stride = intermediate->GetColorSurfaceStride() >> 1;
  i32 actual_width = static_cast<i32>(intermediate->Width());
  i32 actual_height = static_cast<i32>(intermediate->Height());
  i32 left = area ? area->left : 0, top = area ? area->top : 0;
  for (i32 y = 0; y < actual_height; ++y) {
    assert(actual_width <= output_width);
    for (i32 x = 0; x < actual_width; ++x) {
      GxScaleSpan *span = &spans[x];
      f32 red_sum = 0.0f, green_sum = 0.0f, blue_sum = 0.0f, weight_sum = 0.0f;
      for (i32 index = 0; index < span->count; ++index) {
        GxScaleTap *tap = &span->taps[index];
        // Crop coordinates may cross scanlines; only their combined offset selects the actual source word.
        i64 source_offset = (static_cast<i64>(top) + y) * source_stride + left + tap->coordinate;
        assert(source_offset >= 0);
        u8 red, green, blue;
        GxScaleUnpack(source[static_cast<usize>(source_offset)], &red, &green, &blue);
        if (!has_transparency || red != transparent_red || green != transparent_green || blue != transparent_blue) {
          red_sum = static_cast<f32>(static_cast<f64>(red) * tap->weight + red_sum);
          green_sum = static_cast<f32>(static_cast<f64>(green) * tap->weight + green_sum);
          blue_sum = static_cast<f32>(static_cast<f64>(blue) * tap->weight + blue_sum);
          weight_sum = static_cast<f32>(tap->weight + weight_sum);
        }
      }
      u16 pixel = static_cast<u16>(transparent);
      if (weight_sum >= 0.40000001f) {
        f64 inverse = 1.0 / weight_sum;
        red_sum = static_cast<f32>(inverse * red_sum);
        green_sum = static_cast<f32>(inverse * green_sum);
        blue_sum = static_cast<f32>(inverse * blue_sum);
        u8 red = red_sum < 0.0f ? 0 : red_sum > 255.0f ? 255 : static_cast<u8>(red_sum + 0.5);
        u8 green = green_sum < 0.0f ? 0 : green_sum > 255.0f ? 255 : static_cast<u8>(green_sum + 0.5);
        u8 blue = blue_sum < 0.0f ? 0 : blue_sum > 255.0f ? 255 : static_cast<u8>(blue_sum + 0.5);
        GxScalePack(&pixel, red, green, blue);
      }
      destination[static_cast<usize>(y) * destination_stride + static_cast<usize>(x)] = pixel;
    }
  }
  buffer->Unlock(GxLockFlags::Bits);
  radius = vertical_ratio >= 1.0f ? 1.0f : 1.0f / vertical_ratio;
  for (i32 output = 0; output < output_height; ++output) {
    GxScaleSpan *span = &spans[output];
    span->count = 0;
    span->taps = taps + static_cast<usize>(output) * capacity;
    f64 precise_position = static_cast<f64>(output) / vertical_ratio;
    f32 position = static_cast<f32>(precise_position);
    f64 lower = std::ceil(precise_position - radius);
    f64 upper = std::floor(static_cast<f64>(position) + radius);
    i32 first = WnBitmapInteger(lower + (lower >= 0.0 ? 0.5 : -0.5));
    i32 last = WnBitmapInteger(upper + (upper >= 0.0 ? 0.5 : -0.5));
    assert(first != INT_MIN && last != INT_MAX);
    for (i32 input = first; input <= last; ++input) {
      f64 distance = std::fabs((static_cast<f64>(position) - input) / radius);
      f64 weight = distance >= 1.0 ? 0.0 : distance * ((distance + distance - 3.0) * distance) + 1.0;
      assert(static_cast<usize>(output) * capacity + span->count < static_cast<usize>(maximum) * capacity);
      GxScaleTap *tap = &span->taps[span->count++];
      tap->coordinate = input < 0 ? -input : input < actual_height ? input :
          static_cast<i32>(static_cast<u32>(actual_height) * 2u - static_cast<u32>(input) - 1u);
      tap->weight = weight / radius;
    }
  }
  if (!result->Lock(GxLockFlags::Bits)) {
    intermediate->Unlock(GxLockFlags::Bits);
    delete[] spans;
    delete[] taps;
    intermediate->Release();
    result->Release();
    return nullptr;
  }
  u32 intermediate_stride = intermediate->GetColorSurfaceStride() >> 1;
  u32 result_stride = result->GetColorSurfaceStride() >> 1;
  u16 *result_bits = static_cast<u16 *>(result->GetColorSurfaceBits());
  u16 *intermediate_bits = static_cast<u16 *>(intermediate->GetColorSurfaceBits());
  for (i32 x = 0; x < output_width; ++x) {
    for (i32 y = 0; y < output_height; ++y) {
      GxScaleSpan *span = &spans[y];
      f32 red_sum = 0.0f, green_sum = 0.0f, blue_sum = 0.0f, weight_sum = 0.0f;
      for (i32 index = 0; index < span->count; ++index) {
        GxScaleTap *tap = &span->taps[index];
        i64 intermediate_offset = static_cast<i64>(tap->coordinate) * intermediate_stride + x;
        assert(intermediate_offset >= 0 &&
            static_cast<u64>(intermediate_offset) < static_cast<u64>(actual_height) * intermediate_stride);
        u8 red, green, blue;
        GxScaleUnpack(intermediate_bits[static_cast<usize>(intermediate_offset)], &red, &green, &blue);
        if (!has_transparency || red != transparent_red || green != transparent_green || blue != transparent_blue) {
          red_sum = static_cast<f32>(static_cast<f64>(red) * tap->weight + red_sum);
          green_sum = static_cast<f32>(static_cast<f64>(green) * tap->weight + green_sum);
          blue_sum = static_cast<f32>(static_cast<f64>(blue) * tap->weight + blue_sum);
          weight_sum = static_cast<f32>(tap->weight + weight_sum);
        }
      }
      u16 pixel = static_cast<u16>(transparent);
      if (weight_sum >= 0.40000001f) {
        f64 inverse = 1.0 / weight_sum;
        red_sum = static_cast<f32>(inverse * red_sum);
        green_sum = static_cast<f32>(inverse * green_sum);
        blue_sum = static_cast<f32>(inverse * blue_sum);
        u8 red = red_sum < 0.0f ? 0 : red_sum > 255.0f ? 255 : static_cast<u8>(red_sum + 0.5);
        u8 green = green_sum < 0.0f ? 0 : green_sum > 255.0f ? 255 : static_cast<u8>(green_sum + 0.5);
        u8 blue = blue_sum < 0.0f ? 0 : blue_sum > 255.0f ? 255 : static_cast<u8>(blue_sum + 0.5);
        GxScalePack(&pixel, red, green, blue);
      }
      result_bits[static_cast<usize>(y) * result_stride + x] = pixel;
    }
  }
  intermediate->Unlock(GxLockFlags::Bits);
  result->Unlock(GxLockFlags::Bits);
  delete[] spans;
  delete[] taps;
  intermediate->Release();
  return result;
}

// Load an image, optionally reduce it to fit the window, and install the resulting buffer.
bool WnAppBitmapView::SetBMPFromFile(const ScStringView &path) {
  GxBuffer *loaded;
  if (!SetBMPFromFile(&loaded, path)) {
    SetImage(nullptr);
    return false;
  }
  if (fit_image) {
    f32 horizontal = static_cast<f32>(GetW()) / static_cast<f32>(loaded->Width());
    f32 vertical = static_cast<f32>(GetH()) / static_cast<f32>(loaded->Height());
    if (horizontal < 1.0f || vertical < 1.0f) {
      f32 scale = horizontal < vertical ? horizontal : vertical;
      GxBuffer *scaled = GxScaleBitmapInterpolated(loaded, scale, scale, nullptr);
      if (scaled) {
        scaled->AddRef();
        loaded->Release();
        loaded = scaled;
      }
    }
  }
  SetImage(loaded);
  loaded->Release();
  return true;
}

bool WnAppBitmapView::SetBMPFromMemory(void *data, u32 size) {
  return SetBMPFromMemory(&image, data, size);
}

bool WnAppBitmapView::WriteBMPToMemory(u32 *format, void **out, u32 *size, u32 quality, const ScString &data) {
  return WriteBMPToMemory(image, format, out, size, quality, data);
}

// Acquire the shared image factory if necessary and load a 16-bit image from the requested path.
bool WnAppBitmapView::SetBMPFromFile(GxBuffer **out, const ScStringView &path) {
  if (!out)
    return false;
  *out = nullptr;
  GxBuffer *loaded = nullptr;
  if (!image_factory)
    ApGetCom()->GetClassObject(kGxGimexImageClassId, IID_GxGimexImage, reinterpret_cast<void **>(&image_factory));
  if (!image_factory || !image_factory->LoadImage(&path, &loaded, 0, 4, 16))
    return false;
  *out = loaded;
  return loaded != nullptr;
}

// Select an image extension, remove a replaced old file and ask the image factory to save the buffer.
bool WnAppBitmapView::WriteBMPToFile(const GxBuffer *buffer, u32 format, ScStringView *path, u32 quality,
    const ScString &data) {
  ScString name(*path), previous(*path);
  u8 selected = static_cast<u8>(format);
  if (selected & 1)
    selected = 0xc0;
  if (!(selected & 0x40))
    return false;
  const char *extension = ".jpg";
  if (!name.EndsWith(extension, 4))
    name.Append(extension);
  if (!name.IsEqual(&previous)) {
    if (RsFile::FileExists(previous))
      RsFile::Delete(previous);
    path->FromChar(name.ToChar());
  }
  if (!image_factory)
    ApGetCom()->GetClassObject(kGxGimexImageClassId, IID_GxGimexImage, reinterpret_cast<void **>(&image_factory));
  if (!buffer || !image_factory || !image_factory->SaveImage(&name, const_cast<GxBuffer *>(buffer), quality))
    return false;
  if (data.Strlen())
    SetCustomDataInFile(name, data.Data(), data.Strlen());
  return true;
}

// Write image bytes to a temporary file and pass that file to the image loader.
bool WnAppBitmapView::SetBMPFromMemory(GxBuffer **out, void *data, u32 size) {
  ScString path;
  if (!RsFile::MakeTempPathName(&path))
    return false;
  RsFile file(path);
  if (!file.Open(kRsFileAccessWrite, kRsFileOpenAlways, kRsFileShareRead))
    return false;
  if (file.Write(data, &size)) {
    file.Close();
    SetBMPFromFile(out, path);
    RsFile::Delete(path);
    return true;
  }
  file.Close();
  RsFile::Delete(path);
  return false;
}

// Encode an image to a temporary JPEG and return its file contents in an allocated buffer.
bool WnAppBitmapView::WriteBMPToMemory(const GxBuffer *buffer, u32 *format, void **out, u32 *size, u32 quality,
    const ScString &data) {
  ScString path;
  if (!RsFile::MakeTempPathName(&path))
    return false;
  *format = 64;
  if (!WriteBMPToFile(buffer, 64, &path, quality, data))
    return false;
  RsFile file(path);
  if (file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead)) {
    *size = file.Size();
    if (*size) {
      *out = new u8[*size];
      file.Read(*out, size);
      file.Close();
      RsFile::Delete(path);
      return true;
    }
    file.Close();
    RsFile::Delete(path);
  }
  RsFile::Delete(path);
  return false;
}

bool WnAppBitmapView::GetCustomDataFromFile(const ScStringView &, void **, u32 *) {
  return false;
}

bool WnAppBitmapView::SetCustomDataInFile(const ScStringView &, const void *, u32) {
  return true;
}

// Record the current city's identity, simulation state and camera in the local snapshot metadata fields.
bool ApSnapshotTaker::GetGraphicInfoForCurrentCity(ScString *information) {
  ScString marker, nickname, city_name, mayor_name, date_text, population_text, funds_text, cheats_text, camera_text;
  ScString reserved;
  information->FromChar("");
  CtCity *city = ApGetCity();
  if (!city)
    return false;
  ApGameInterface *application = ApGetGameApp();
  marker.FromChar("SanityCheck");
  city_name = ScString(*city->GetCityName());
  mayor_name = ScString(*city->GetMayorName());
  CtClockInterface *clock = city->GetSimulationClock();
  if (clock) {
    CtDate date;
    clock->GetCurrentClockDate(&date);
    date_text.Sprintf("%u", date.serial);
  }
  CtResidentialLayer *residential = city->ResidentialLayer();
  if (residential)
    population_text.Sprintf("%u", static_cast<u32>(residential->GetPopulation()));
  EcBudgetLayer *budget = city->BudgetLayer();
  if (budget)
    funds_text.Sprintf("%lld", budget->GetTotalFunds());
  ScCheatCodeManager *cheats = application->GetCheatCodeManager();
  if (cheats && cheats->GetCheatCodeOccurrenceCount())
    cheats_text.FromChar("1");
  SpViewInterface *view = ApGetCurrentCityView();
  if (view) {
    i32 zoom, rotation;
    u32 x, y;
    view->GetZoomAndRotation(&zoom, &rotation);
    view->GetFocusCell(&x, &y);
    camera_text.Sprintf("%u,%u,%u,%u", static_cast<u32>(zoom), static_cast<u32>(rotation), x, y);
  }
  const ScString *fields[] = {&marker, &nickname, &city_name, &mayor_name, &date_text, &population_text,
      &funds_text, &cheats_text, &camera_text, &reserved};
  for (const ScString *field : fields) {
    information->Append(field->begin, field->ScString::Strlen());
    information->Append("\n");
  }
  return true;
}

bool ApSnapshotTaker::AddNewGraphicForCurrentCity(NwGraphicsViewer *viewer, const ScString &path, bool flag) {
  CtCity *city = ApGetCity();
  if (!city)
    return false;
  ScString description, information, name;
  GetGraphicInfoForCurrentCity(&information);
  GetGraphicDescriptionForCurrentCity(&description);
  u32 id = city->GetCityID();
  name = *city->GetCityName();
  return viewer->AddNewGraphic(path, information, description, name, id, flag);
}

ApSnapshotTaker::ApSnapshotTaker()
    : snapshot_initialized(false), field168(nullptr), field172(nullptr), size_index(0), field180(nullptr) {
  SetFillColor(255, 255, 255);
}

ApSnapshotTaker::~ApSnapshotTaker() {
  Shutdown();
}

// Initialize the snapshot sizes, localized help surface, placement, capture and cursor before the window base.
bool ApSnapshotTaker::Init() {
  if (snapshot_initialized)
    return true;
  WnResources *resources = ApGetUiResources();
  SetID(kApSnapshotWindowId);
  field172 = resources->GetFont(kWnFontUiSmall);
  if (field172) {
    field172->AddRef();
    if (sizes.empty()) {
      WnWindow *parent_window = GetParentWin();
      u32 width = parent_window ? static_cast<u32>(parent_window->GetW()) : 640;
      if (width <= 700) {
        sizes.push_back({320, 240});
        sizes.push_back({480, 360});
        sizes.push_back({512, 384});
      } else if (width <= 1000) {
        sizes.push_back({480, 360});
        sizes.push_back({512, 384});
        sizes.push_back({640, 480});
      } else if (width <= 1200) {
        sizes.push_back({480, 360});
        sizes.push_back({640, 480});
        sizes.push_back({800, 600});
      } else {
        sizes.push_back({640, 480});
        sizes.push_back({800, 600});
        sizes.push_back({1024, 768});
      }
    }
    if (help_text.empty()) {
      for (u32 id = 211; id <= 213; ++id) {
        UiTextHandle text(id, kRsSupplementalGameTextGroup);
        help_text.push_back(static_cast<ScString>(text));
      }
    }
    ApGetGraphics()->CreateBuffer(&field168);
    if (field168) {
      u32 line_height = field172->Height(0);
      u32 height = line_height * static_cast<u32>(help_text.size()) + 4;
      if (max_text_width == kApSnapshotTextWidthUnmeasured) {
        max_text_width = 10;
        for (const ScString &text : help_text) {
          u32 width = static_cast<u32>(field172->Measure(text.Data(), text.Strlen()));
          if (width > max_text_width)
            max_text_width = width;
        }
        max_text_width += 4;
      }
      if (!field168->Init(max_text_width, height, GxColorType(GxColorType::Color::RGB16), nullptr,
              GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
        field168->Release();
        field168 = nullptr;
      } else {
        if (field168->Lock(GxLockFlags::Fill)) {
          field168->Fill(resources->GetColor(kWnColorUiTransparency), nullptr);
          field168->Unlock(GxLockFlags::Fill);
        }
        field168->SetTransparency(resources->GetColor(kWnColorUiTransparency));
        u32 color = field172->Color();
        field172->SetColor(GetFillColor());
        u32 y = 4;
        for (const ScString &text : help_text) {
          field172->DrawTextA(field168, 4, y, text.Data(), text.Strlen());
          y += line_height;
        }
        field172->SetColor(color);
      }
    }
  }
  assert(!sizes.empty());
  const ScSize &size = sizes.back();
  i32 x = 0, y = 0;
  owner->GetCursorRelativePosition(GetParentWin(), &x, &y);
  GZWinMoveTo(x - size.width / 2, y - size.height / 2);
  SetSize(size.width, size.height);
  LoadWindowSettingsFromPrefs();
  GZSetCapture();
  SetFocus();
  WnCursor *snapshot_cursor = GetCursorManager()->GetIGZCursor(kSpSnapshotCursorId);
  if (snapshot_cursor)
    SetCursor(snapshot_cursor, true);
  snapshot_initialized = true;
  return UiWindow::Init();
}

// Show an existing snapshot window or attach a new one, select its requested size and notify its parent.
bool ApSnapshotTaker::CreateWinSnapshotTaker(WnWindow *parent_window, u32 kind) {
  WnWindow *existing = parent_window->GetChildWindowFromID(kApSnapshotWindowId);
  if (existing) {
    existing->ShowWindow();
    existing->PullToFront();
  } else {
    ApSnapshotTaker *window = new ApSnapshotTaker;
    window->AddRef();
    parent_window->ChildAdd(window);
    window->PullToFront();
    if (kind)
      window->GoToSizeIndex(kind - 1);
    window->Release();
    ScMessage message(kWnMsg_ModalBegin, 0, 0, 0);
    parent_window->DoMessage(&message);
  }
  return true;
}

// Capture the selected screen rectangle, add its watermark and save it in the current city's viewer album.
// Release both surfaces before issuing the original success notification.
bool ApSnapshotTaker::TakeSnapshot(bool close_window) {
  WnWindow *parent_window = GetParentWin();
  i32 width = parent_window ? parent_window->GetW() : 640;
  i32 height = parent_window ? parent_window->GetH() : 480;
  ScRect bounds{0, 0, width, height}, capture_area = *GetArea();
  bool result = false;
  NwInternet *internet = ApGetGameApp()->GetSC3Internet();
  NwTransferSystem *transfer_system = internet ? internet->GetTransferSystem() : nullptr;
  if (transfer_system) {
    GxBuffer *screen = transfer_system->GetScreen(static_cast<NwTransferSystem::TransferType>(3), false);
    assert(screen);
    GxBuffer *snapshot;
    if (ApGetGraphics()->CreateBuffer(&snapshot)) {
      u32 quality = transfer_system->GetGraphicsQuality0to100();
      ScString filename;
      capture_area.IntersectInclusive(&capture_area, &bounds);
      snapshot->Init(static_cast<u32>(capture_area.right - capture_area.left),
          static_cast<u32>(capture_area.bottom - capture_area.top),
          GxColorType(GxColorType::Color::RGB16), nullptr, GxDepthType(GxDepthType::Depth::None),
          GxVramRequest::SystemMemory, true);
      if (snapshot->Lock(GxLockFlags::Blt)) {
        ScRect destination{0, 0, capture_area.right - capture_area.left, capture_area.bottom - capture_area.top};
        snapshot->Blt(screen, capture_area, destination, static_cast<const ScRect *>(nullptr));
        snapshot->Unlock(GxLockFlags::Blt);
        AddWatermarkToBuffer(snapshot);
        if (close_window)
          HideWindow();
        transfer_system->ShowImageBrowser(true, true, nullptr, close_window);
        NwGraphicsViewer *viewer = transfer_system->GetImageBrowser();
        if (viewer) {
          viewer->GoToAlbumForCurrentCity(true);
          if (viewer->GetNewTempFileNameForCurrentAlbum(&filename)) {
            ScString data;
            if (WnAppBitmapView::WriteBMPToFile(snapshot, 64, &filename, quality, data)) {
              AddNewGraphicForCurrentCity(viewer, filename, false);
              result = true;
            }
          }
        }
      }
      snapshot->Release();
    }
    screen->Release();
    if (result)
      ApGetGameApp()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCamera, 0, 0, 0);
  }
  return result;
}

bool ApSnapshotTaker::Shutdown() {
  if (!snapshot_initialized)
    return true;
  SaveWindowSettingsToPrefs();
  ScRelease(&field172);
  ScRelease(&field168);
  ScRelease(&field180);
  snapshot_initialized = false;
  return UiWindow::Shutdown();
}

void ApSnapshotTaker::RemoveSelf() {
  WnWindow *parent_window = GetParentWin();
  WnWindowManager *manager = owner;
  if (GZAmCapturing())
    GZReleaseCapture();
  if (manager && parent_window) {
    parent_window->ChildRemove(this);
    manager->SetFocus(parent_window);
  }
}

bool ApSnapshotTaker::GZOnCharacter(char) {
  return true;
}

// Draw the translucent snapshot border and composite the optional help surface over the window.
bool ApSnapshotTaker::GZPaint() {
  if (!draw_buffer)
    return UiWindow::GZPaint();
  GxLockFlags lock_flags = static_cast<GxLockFlags>(4);
  if (draw_buffer->Lock(lock_flags)) {
    draw_buffer->DrawLineAlpha(draw_area.left, draw_area.top, draw_area.right, draw_area.top, GetFillColor(), 1, 0.5f);
    draw_buffer->DrawLineAlpha(draw_area.left, draw_area.top, draw_area.left, draw_area.bottom, GetFillColor(), 1, 0.5f);
    draw_buffer->DrawLineAlpha(draw_area.right, draw_area.top, draw_area.right, draw_area.bottom, GetFillColor(), 1, 0.5f);
    draw_buffer->DrawLineAlpha(draw_area.left, draw_area.bottom, draw_area.right, draw_area.bottom, GetFillColor(), 1,
        0.5f);
    draw_buffer->Unlock(lock_flags);
  }
  if (field168) {
    ScRect source{0, 0, static_cast<i32>(field168->Width()), static_cast<i32>(field168->Height())};
    ScPoint target{draw_area.left, draw_area.top};
    ApServices::AB()->AlphaBltConstAlpha(field168, draw_buffer, 0.5f, source, target, nullptr);
  }
  return true;
}

// Handle size changes, cancellation, capture requests and numeric graphics-quality shortcuts.
bool ApSnapshotTaker::OnKeyDown(u32 key, u32 modifiers) {
  if (key == kWnKeySpace) {
    CycleSize();
  } else if (key == kWnKeyEscape) {
    RemoveSelf();
  } else if (key == kWnKeyReturn) {
    HideWindow();
    PostMsg(this, kWnMsg_CaptureSnapshot, (modifiers & kWnModifierKeyboardMask) ? 0 : 1, 0, 0);
  } else if (key >= kWnKey0 && key <= kWnKey9) {
    NwInternet *internet = ApGetGameApp()->GetSC3Internet();
    NwTransferSystem *transfer = internet ? internet->GetTransferSystem() : nullptr;
    if (transfer)
      transfer->SetGraphicsQuality0to100(10 * key - 470);
  }
  return true;
}

bool ApSnapshotTaker::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CaptureSnapshot)
    return UiWindow::DoMessage(message);
  if (message->data2) {
    if (message->data1) {
      TakeSnapshot(true);
      RemoveSelf();
    } else {
      TakeSnapshot(false);
      ShowWindow();
    }
  } else {
    message->data2 = 1;
    PostMsg(this, kWnMsg_CaptureSnapshot, message->data1, 1, 0);
  }
  return true;
}

bool ApSnapshotTaker::OnKeyUp(u32, u32) {
  return true;
}

bool ApSnapshotTaker::OnMouseDownL(i32, i32, u32 modifiers) {
  return OnKeyDown(kWnKeyReturn, modifiers);
}

bool ApSnapshotTaker::OnMouseDownR(i32, i32, u32) {
  RemoveSelf();
  return true;
}

bool ApSnapshotTaker::OnMouseMove(i32 x, i32 y, u32) {
  WindowToWindowCoordinates(GetParentWin(), &x, &y);
  x = x - (GetW() / 2);
  y = y - (GetH() / 2);
  GZWinMoveTo(x, y);
  return true;
}

// Advance to the next configured snapshot size, wrapping to the first size.
bool ApSnapshotTaker::CycleSize() {
  u32 index = size_index + 1;
  if (index >= sizes.size())
    index = 0;
  return GoToSizeIndex(index);
}

// Select a configured size and center its rectangle on the current window.
bool ApSnapshotTaker::GoToSizeIndex(u32 index) {
  if (sizes.empty())
    return false;
  ScRect rect;
  GetArea(&rect);
  if (index >= sizes.size())
    index = static_cast<u32>(sizes.size() - 1);
  size_index = index;
  i32 width = sizes[index].width, height = sizes[index].height;
  i32 center_x = (rect.right + rect.left) / 2;
  i32 center_y = (rect.bottom + rect.top) / 2;
  rect.left = center_x - (width / 2);
  rect.top = center_y - (height / 2);
  rect.right = rect.left + width;
  rect.bottom = rect.top + height;
  SetArea(rect);
  return true;
}

// Save the window's position and selected size in the original miscellaneous preferences block.
bool ApSnapshotTaker::SaveWindowSettingsToPrefs() {
  ApMiscWindowPrefs *prefs = ApGetGameApp()->GetAppPreferences()->MiscWindowPrefs();
  prefs->values[0][0] = GetL();
  prefs->values[0][1] = GetT();
  prefs->values[0][2] = static_cast<i32>(size_index);
  return true;
}

// Restore the configured size index from the miscellaneous preferences block.
bool ApSnapshotTaker::LoadWindowSettingsFromPrefs() {
  ApMiscWindowPrefs *prefs = ApGetGameApp()->GetAppPreferences()->MiscWindowPrefs();
  GoToSizeIndex(static_cast<u32>(prefs->values[0][2]));
  return true;
}

bool NwTransferObject::CopyFileIntoViewerDirectory(const ScStringView &file) {
  ScString filename, destination(download_directory);
  RsFile::GetPathName(file, &filename, true);
  destination.Append(filename.Data(), filename.Strlen());
  return RsFile::Copy(file, destination, true);
}

// Copy the named viewer file to the supplied destination using the original overwrite policy.
bool NwTransferObject::CopyFileFromViewerDirectoryToAnotherDirectory(
    const ScStringView &file, const ScStringView &directory) {
  ScString source(download_directory), filename(file);
  source.Append(filename.Data(), filename.Strlen());
  return RsFile::Copy(source, directory, true);
}

bool NwTransferObject::TakeSnapshot(u32 kind) {
  return ApSnapshotTaker::CreateWinSnapshotTaker(ApGetGameApp()->GetSC3MainWindow()->AsIGZWin(), kind);
}

// Return either the city surface or primary display surface, optionally duplicating it through the real buffer interface.
// Retain the selected or successfully duplicated buffer before publishing it.
GxBuffer *NwTransferObject::GetScreen(TransferType type, bool capture) {
  GxBuffer *result = nullptr, *source;
  if (static_cast<i32>(type) == 2) {
    SpCellMap *map = ApGetCitySpriteCellMap();
    if (!map)
      return result;
    source = map->GetCityBufferPtr();
    if (!source)
      return result;
  } else if (static_cast<i32>(type) == 3) {
    GxGraphics *graphics = ApGetGraphics();
    if (!graphics)
      return result;
    GxPrimary *primary = graphics->ScreenBuffer();
    if (!primary)
      return result;
    source = primary->AsIGZBuffer();
  } else {
    return result;
  }
  if (!capture)
    result = ScAddRefNotNull(source);
  else if (source->Duplicate(&result, GxVramRequest::Default))
    result->AddRef();
  return result;
}

NwTransferObject::NwTransferObject()
    : references(0), initialized(false), quality(80), field24(5), internet(nullptr), parent(nullptr),
      viewer(nullptr), field60(120) {
}

NwTransferObject::~NwTransferObject() {
  Shutdown();
}

bool NwTransferObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwTransferSystem);
  HANDLE_QUERY_INTERFACE(IID_NwTransferSystem, NwTransferSystem);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwTransferObject::AddRef() {
  return ++references;
}

// Release the transfer system and destroy it when its count was one.
u32 NwTransferObject::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

bool NwTransferObject::DoMessage(ScMessage *) {
  return true;
}

// Set the default transfer directory under the data directory on first initialization.
bool NwTransferObject::Init() {
  if (!initialized) {
    {
      ScString directory(ApGetFileSystem()->DataDirectory());
      download_directory = directory;
    }
    download_directory.Append("Albums\\");
    download_directory.Append("Transfer\\");
    initialized = true;
  }
  return true;
}

bool NwTransferObject::Shutdown() {
  ScRelease(&internet);
  if (initialized) {
    ScRelease(&parent);
    ScRelease(&viewer);
    CleanUpUnfinishedTransfers();
    CleanUpTempFiles();
    initialized = false;
  }
  return true;
}

u32 NwTransferObject::GetGraphicsQuality0to100() {
  return quality;
}

void NwTransferObject::SetGraphicsQuality0to100(u32 value) {
  quality = value > 100 ? 100 : value;
}

// Retain the replacement internet object before releasing the previous one.
void NwTransferObject::SetSC3Internet(NwInternet *value) {
  if (value)
    value->AddRef();
  if (internet)
    internet->Release();
  internet = value;
}

// Retain the replacement parent window before releasing the previous one.
void NwTransferObject::SetParentWindow(WnWindow *value) {
  if (value)
    value->AddRef();
  if (parent)
    parent->Release();
  parent = value;
}

bool NwTransferObject::GetDownloadDirectory(ScStringView *out) {
  out->FromChar(download_directory.begin, download_directory.ScString::Strlen());
  return true;
}

// Try a replacement directory and restore the old directory if creation fails.
bool NwTransferObject::SetDownloadDirectory(const ScStringView &value, bool create) {
  ScString requested(value);
  if (RsDirectoryListing::DoesDirectoryExist(requested))
    return true;
  if (create) {
    ScString previous(download_directory);
    download_directory = requested;
    if (MakeSureDownloadDirectoryExists(false))
      return true;
    download_directory = previous;
  }
  return false;
}

// Ensure the stored directory exists, optionally retrying with the default transfer directory.
bool NwTransferObject::MakeSureDownloadDirectoryExists(bool reset) {
  ScString directory(download_directory);
  if (RsDirectoryListing::DoesDirectoryExist(directory) || RsDirectoryListing::CreateDirectoryA(download_directory))
    return true;
  if (!reset)
    return false;
  {
    ScString base(ApGetFileSystem()->DataDirectory());
    download_directory = base;
  }
  download_directory.Append("Albums\\");
  download_directory.Append("Transfer\\");
  return MakeSureDownloadDirectoryExists(false);
}

void NwTransferObject::CleanUpUnfinishedTransfers() {
}

void NwTransferObject::CleanUpTempFiles() {
}

void NwTransferObject::AbortAllTransfers() {
}

bool NwTransferObject::ReadPreferences(RsIndexedFile *) {
  if (internet) {
    NwInternetPrefs *prefs = internet->GetInternetPrefs();
    if (prefs) {
      if (prefs->text1552[0]) {
        prefs->text1552[383] = 0;
        ScString path(prefs->text1552);
        if (RsDirectoryListing::DoesDirectoryExist(path)) {
          ScString copy(prefs->text1552);
          download_directory = copy;
        }
      }
      SetGraphicsQuality0to100(prefs->field12);
    }
  }
  return true;
}

// Save at most 383 bytes of the current download directory in internet preferences.
bool NwTransferObject::WritePreferences(RsIndexedFile *) {
  if (internet) {
    NwInternetPrefs *prefs = internet->GetInternetPrefs();
    if (prefs) {
      u32 size = download_directory.ScString::Strlen();
      if (size > 383)
        size = 383;
      std::strncpy(prefs->text1552, download_directory.begin, size);
      prefs->text1552[size] = 0;
    }
  }
  return true;
}

NwInternetObject::NwInternetObject()
    : references(0), initialized(false), parent(nullptr), transfer(nullptr) {
}

bool NwInternetObject::DoMessage(ScMessage *) {
  return false;
}

NwInternetObject::~NwInternetObject() {
  Shutdown();
}

// Construct the original transfer owner for local snapshots and albums.
// No communication services or remote notifications are started.
bool NwInternetObject::Init() {
  if (initialized)
    return true;
  transfer = ScAddRefNotNull(new NwTransferObject);
  transfer->SetSC3Internet(this);
  transfer->Init();
  initialized = true;
  return true;
}

// Release the local transfer and parent window references.
bool NwInternetObject::Shutdown() {
  if (initialized) {
    if (transfer) {
      transfer->Shutdown();
      transfer->Release();
      transfer = nullptr;
    }
    ScRelease(&parent);
    initialized = false;
  }
  return true;
}

void NwInternetObject::DoIdleProcessing() {
  if (transfer)
    transfer->DoIdleProcessing();
}

// Route the local album command to the transfer owner.
bool NwInternetObject::DoUserStringCommand(const ScStringView &command) {
  ScCommandLine parsed(command.ToChar());
  if (parsed.argc() < 2)
    return false;
  ScString first(*parsed.GetArgument(0)), second(*parsed.GetArgument(1));
  first.MakeLower();
  second.MakeLower();
  if (std::strcmp(first.ToChar(), "/view") && std::strcmp(first.ToChar(), "\\view"))
    return false;
  if (std::strcmp(second.ToChar(), "album") && std::strcmp(second.ToChar(), "images") &&
      std::strcmp(second.ToChar(), "graphics"))
    return false;
  return transfer && transfer->ShowImageBrowser(true, false, nullptr, true);
}

bool NwInternetObject::ReadPreferences(RsIndexedFile *file) {
  char *bytes = reinterpret_cast<char *>(&preferences);
  u32 size = sizeof(preferences);
  return NwReadInternetPrefRecord(file, kApInternetPreferencesRecordInstance, &bytes, &size, false);
}

bool NwInternetObject::WritePreferences(RsIndexedFile *file) {
  NwInternetPrefs copy = preferences;
  return NwWriteInternetPrefRecord(
      file, kApInternetPreferencesRecordInstance, reinterpret_cast<const char *>(&copy), sizeof(copy));
}

// Load the saved preferences and restore the local album directory and graphics quality.
bool NwInternetObject::ReadPreferences() {
  ScString path;
  RsIndexedFile file(nullptr);
  GetInternetPrefsFilePath(&path);
  if (file.OpenRead(path)) {
    if (ReadPreferences(&file)) {
      ReadPreferenceOverrides();
      if (transfer)
        static_cast<NwTransferObject *>(transfer)->ReadPreferences(&file);
    } else {
      ReadPreferenceOverrides();
    }
    file.Shutdown();
    return true;
  }
  ReadPreferenceOverrides();
  if (transfer)
    static_cast<NwTransferObject *>(transfer)->ReadPreferences(nullptr);
  return false;
}

// Read the local screenshot quality and album directory overrides from the existing settings file.
bool NwInternetObject::ReadPreferenceOverrides() {
  ScString ini_name, path, value, section("Internet");
  ApGetGameApp()->GetAppIniFileName(&ini_name);
  ApServices::ADir(&path);
  path.Append(ini_name.Data(), ini_name.Strlen());
  ApIniFile ini(path);
  if (ini.ReadEntry(section, ScString("GraphicQuality0to100"), &value)) {
    errno = 0;
    long number = std::strtol(value.ToChar(), nullptr, 10);
    assert(errno != ERANGE && number >= INT32_MIN && number <= INT32_MAX);
    preferences.field12 = static_cast<u32>(number);
  }
  if (ini.ReadEntry(section, ScString("DownloadDirectory"), &value)) {
    if (value.Strlen() > 383)
      value.Erase(383, value.Strlen() - 383);
    std::memcpy(preferences.text1552, value.ToChar(), std::strlen(value.ToChar()) + 1);
  }
  return true;
}

// Replace or create a preference record, aborting the write when its stream operation fails.
bool NwWriteInternetPrefRecord(RsIndexedFile *file, u32 instance, const char *bytes, u32 size) {
  RsKey key(kApPreferenceRecordType, kApPreferenceRecordGroup, instance);
  RsRecord *record;
  bool result = file->TestForRecord(key) ? file->OpenRecordForWrite(key, &record) : file->CreateNewRecord(key, &record);
  if (result) {
    RsStream *stream;
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      result = stream->WriteBlock(bytes, size);
      stream->Release();
    }
    if (result)
      file->CloseRecord(&record);
    else
      file->AbortRecord(&record);
  }
  return result;
}

// Check the existing indexed archive before copying records. Reject truncated indexes, duplicate keys and payloads
// outside the file so the local-only save never replaces an unsupported or damaged preferences file.
static bool NwValidatePreferenceArchive(const ScStringView &path, RsKeyList *keys) {
  RsFile input(path);
  if (!input.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return false;
  u32 size = input.Size(), magic, lowest = size;
  if (size < 24 || !input.Read(&magic, sizeof(magic)) || magic != kRsIndexedFileSignature)
    return false;
  for (;;) {
    u32 entry[5];
    if (!input.Read(entry, sizeof(entry)))
      return false;
    if (!entry[0] && !entry[1] && !entry[2])
      return input.Position() <= lowest;
    if (entry[3] == kRsIndexedFileUnusedEntryValue && entry[4] == kRsIndexedFileUnusedEntryValue)
      continue;
    if (entry[3] == kRsIndexedFileUnusedEntryValue || entry[4] == kRsIndexedFileUnusedEntryValue || entry[3] > size ||
        entry[4] > size - entry[3])
      return false;
    RsKey key(entry[2], entry[0], entry[1]);
    if (keys->IsPresent(key))
      return false;
    keys->Insert(key);
    if (entry[3] < lowest)
      lowest = entry[3];
  }
}

// Read the exact indexed record payload into owned storage. A failed stream query or short read leaves no output.
static bool NwReadPreferenceArchiveRecord(RsIndexedFile *file, const RsKey &key, u8 **bytes, u32 *size) {
  RsRecord *record;
  if (!file->OpenRecord(key, &record))
    return false;
  RsStream *stream = nullptr;
  bool result = record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream));
  if (result) {
    result = stream->GetSize(size);
    if (result) {
      u8 *data = new u8[*size ? *size : 1];
      result = stream->ReadBlock(data, *size);
      if (result)
        *bytes = data;
      else
        delete[] data;
    }
    stream->Release();
  }
  file->CloseRecord(&record);
  return result;
}

// Copy one record through the source and destination indexed-file streams, retaining its original key and bytes.
static bool NwCopyPreferenceArchiveRecord(RsIndexedFile *source, RsIndexedFile *target, const RsKey &key) {
  u8 *bytes = nullptr;
  u32 size = 0;
  if (!NwReadPreferenceArchiveRecord(source, key, &bytes, &size))
    return false;
  RsRecord *record;
  bool result = target->CreateNewRecord(key, &record);
  if (result) {
    RsStream *stream = nullptr;
    result = record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream));
    if (result) {
      result = stream->WriteBlock(bytes, size);
      stream->Release();
    }
    if (result)
      result = target->CloseRecord(&record);
    else
      target->AbortRecord(&record);
  }
  delete[] bytes;
  return result;
}

// Check the local preference path and retain the native spelling used by the POSIX replacement operation.
static bool NwPreferencePathState(const ScStringView &path, bool *exists, [[maybe_unused]] ScString *native_path) {
#if defined(_WIN32)
  DWORD attributes = GetFileAttributesA(path.ToChar());
  *exists = attributes != INVALID_FILE_ATTRIBUTES;
  return *exists || GetLastError() == ERROR_FILE_NOT_FOUND;
#else
  *native_path = ScNativeFilePath(path.ToChar());
  struct stat attributes;
  *exists = stat(native_path->Data(), &attributes) == 0;
  return *exists || errno == ENOENT;
#endif
}

// Reserve the temporary archive in the target directory with the platform's existing name and creation rules.
static bool NwCreatePreferenceTemporary(const ScString &directory, ScString *temporary) {
#if defined(_WIN32)
  char temp_path[MAX_PATH];
  if (!GetTempFileNameA(directory.ToChar(), "sc3", 0, temp_path))
    return false;
  temporary->FromChar(temp_path);
#else
  *temporary = ScNativeFilePath(directory.ToChar());
  temporary->Append("/sc3XXXXXX");
  int temporary_fd = mkstemp(temporary->begin);
  if (temporary_fd < 0)
    return false;
  close(temporary_fd);
#endif
  return true;
}

// Replace the preference archive with the reserved temporary file using the platform's original replacement flags.
static bool NwReplacePreferenceFile(const ScString &temporary, [[maybe_unused]] const ScStringView &path,
    [[maybe_unused]] const ScString &native_path) {
#if defined(_WIN32)
  return MoveFileExA(temporary.ToChar(), path.ToChar(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(temporary.ToChar(), native_path.ToChar()) == 0;
#endif
}

static void NwRemovePreferenceTemporary(const ScString &temporary) {
#if defined(_WIN32)
  DeleteFileA(temporary.ToChar());
#else
  unlink(temporary.ToChar());
#endif
}

bool NwSaveLocalPreferences(const ScStringView &path, NwInternetPrefs *preferences, NwTransferObject *transfer) {
  ScString directory, native_path;
  bool exists;
  if (!NwPreferencePathState(path, &exists, &native_path))
    return false;
  RsKeyList keys;
  if (exists && !NwValidatePreferenceArchive(path, &keys))
    return false;
  if (!RsFile::GetPathDirectory(path, &directory))
    return false;
  ScString temporary;
  if (!NwCreatePreferenceTemporary(directory, &temporary))
    return false;
  bool result = false;
  {
    RsIndexedFile source(nullptr), target(nullptr);
    if ((!exists || source.OpenRead(path)) && target.OpenWrite(temporary)) {
      result = true;
      for (u32 i = 0; i < keys.Size() && result; ++i) {
        RsKey *key = keys.GetKey(i);
        if (key->type != kApPreferenceRecordType || key->group != kApPreferenceRecordGroup ||
            key->instance != kApInternetPreferencesRecordInstance)
          result = NwCopyPreferenceArchiveRecord(&source, &target, *key);
      }
      if (result && transfer)
        transfer->WritePreferences(&target);
      if (result)
        result = NwWriteInternetPrefRecord(&target, kApInternetPreferencesRecordInstance,
            reinterpret_cast<const char *>(preferences), sizeof(*preferences));
    }
    if (result)
      result = target.file.Flush();
    target.Shutdown();
    source.Shutdown();
  }
  if (result)
    result = NwReplacePreferenceFile(temporary, path, native_path);
  if (!result)
    NwRemovePreferenceTemporary(temporary);
  return result;
}

bool NwInternetObject::WritePreferences() {
  ScString path;
  GetInternetPrefsFilePath(&path);
  return NwSaveLocalPreferences(path, &preferences, static_cast<NwTransferObject *>(transfer));
}

// Open a preference record and read its available bytes into the caller's zero-filled buffer.
bool NwReadInternetPrefRecord(RsIndexedFile *file, u32 instance, char **bytes, u32 *size, bool allocate) {
  RsKey key(kApPreferenceRecordType, kApPreferenceRecordGroup, instance);
  RsRecord *record;
  bool result = false;
  if (file->OpenRecord(key, &record)) {
    RsStream *stream;
    if (record->QueryInterface(IID_RsStream, reinterpret_cast<void **>(&stream))) {
      u32 record_size = 1;
      stream->GetSize(&record_size);
      if (allocate) {
        if (*size == 0 || *size == kApPreferenceRecordSizeFromArchive)
          *size = record_size;
        *bytes = new char[*size];
      }
      std::memset(*bytes, 0, *size);
      u32 read_size = *size < record_size ? *size : record_size;
      result = stream->ReadBlock(*bytes, read_size);
      stream->Release();
    }
    file->CloseRecord(&record);
  }
  return result;
}

// Publish the internet or secondary message-target identity and retain the shared object.
bool NwInternetObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwInternet);
  HANDLE_QUERY_INTERFACE(IID_NwInternet, NwInternet);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwInternetObject::AddRef() {
  return ++references;
}

// Delete the object when its old reference count is one; otherwise decrement the count.
u32 NwInternetObject::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

NwInternet::ConnectionState NwInternetObject::GetConnectionState() {
  return static_cast<ConnectionState>(0);
}

bool NwInternetObject::IsConnected() {
  return false;
}

bool NwInternetObject::ConnectionIsDialup() {
  return false;
}

bool NwInternetObject::Connect() {
  return false;
}

bool NwInternetObject::Disconnect() {
  return false;
}

bool NwInternetObject::GetMainServerURL(ScStringView *) {
  return false;
}

bool NwInternetObject::SetMainServerURL(const ScStringView &) {
  return false;
}

// Retain the replacement parent before releasing the prior window reference.
void NwInternetObject::SetParentWindow(WnWindow *window) {
  if (window)
    window->AddRef();
  if (parent)
    parent->Release();
  parent = window;
}

// No chat service exists in the local game.
NwChatSystem *NwInternetObject::GetChatSystem() {
  return nullptr;
}

// Borrow the current transfer system without changing its reference count.
NwTransferSystem *NwInternetObject::GetTransferSystem() {
  return transfer;
}

// Borrow the object's embedded internet preferences.
NwInternetPrefs *NwInternetObject::GetInternetPrefs() {
  return &preferences;
}

// Append the original preferences filename to the current application directory.
bool NwInternetObject::GetInternetPrefsFilePath(ScStringView *out) {
  ScString path;
  RsFileSystem *files = ApGetFileSystem();
  if (files) {
    const char *directory = files->AppDirectory();
    if (directory)
      path = ScString(directory);
  }
  path.Append("SC3Net.cfg");
  out->FromChar(path.ToChar());
  return true;
}

bool NwInternetObject::SetInternetPrefs(NwInternetPrefs *value) {
  if (value != &preferences)
    std::memcpy(&preferences, value, sizeof(preferences));
  return true;
}

bool NwInternetObject::GetCityExchangeUserInfo(ScStringView *, ScStringView *, ScStringView *) {
  return false;
}

bool NwInternetObject::ViewExternalBrowser(const ScStringView &) {
  return false;
}

bool NwInternetObject::ViewInternalBrowser(const ScStringView &, const ScString *, bool) {
  return false;
}

bool NwInternetObject::IsExternalBrowserAvailable() {
  return false;
}

bool NwInternetObject::GetCityExchangeURL(ScStringView *) {
  return false;
}

bool NwInternetObject::RegisterURLMessage(const ScStringView &, const ScStringView &, bool) {
  return false;
}

bool NwInternetObject::UnregisterURLMessage(const ScStringView &) {
  return false;
}
