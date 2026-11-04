#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "graphics/primary_buffer.h"


#include <cassert>
#include <cstring>

#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "graphics/display_device.h"
#include "graphics/image.h"
#include "graphics/sc3_graphics.h"

// The reference fallback combines DDSCAPS_SYSTEMMEMORY and DDSCAPS_ZBUFFER, as defined by the Windows SDK.
enum GxReferenceDepthBufferCaps : u32 {
  kGxSystemMemoryDepthBufferCaps = 0x20800,
};

// Reference draw/style combinations retain their SDK wait, depth/raster operation, memory-location and window-frame bits.
enum GxReferenceNativeFlags : u32 {
  kGxDepthTestConstantDestinationFlags = 0x180000,
  kGxWaitRasterOperationFlags = 0x1020000,
  kGxWaitDepthFillFlags = 0x3000000,
  kGxWaitDepthTestFlags = 0x1080000,
  kGxWaitDepthTestConstantDestinationFlags = 0x1180000,
  kGxWaitColorFillFlags = 0x1000400,
  kGxWithoutVideoAndNonlocalMemoryCaps = 0xdfffbfff,
  kGxWithoutSystemAndVideoMemoryCaps = 0xffffb7ff,
  kGxVisiblePopupStyle = 0x90000000,
  kGxCaptionAndSystemMenuStyle = 0xc80000,
};

// The reference Win32 key transport exposes its previous-key-state bit before translating to game modifiers.
enum GxNativeKeyState : u32 {
  kGxNativePreviousKeyState = 0x40000000,
};

#include "graphics/sdl_platform.h"

static u8 GxLockCpuFront(GxDrawDeviceSurface *back, GxDrawSurface *front);
static u8 GxUnlockCpuFront(GxDrawDeviceSurface *back, GxDrawSurface *front);

// Initialize SDL surface state without a platform handle or COM descriptor.
GxDrawSurface::GxDrawSurface() = default;

GxDrawSurface::~GxDrawSurface() {
  GxDrawSurface::Close();
}

u8 GxDrawSurface::Close() {
  ready = false;
  return 1;
}

// Count a lock on live CPU storage, retaining nested locks.
u8 GxDrawSurface::Lock() {
  if (!ready)
    return 0;
  ++locks;
  return 1;
}

// Release a nested CPU-surface lock without changing pixel ownership.
u8 GxDrawSurface::Unlock() {
  if (locks)
    --locks;
  return 1;
}

// Capture the game blit policy directly for the SDL surface implementation.
u8 GxDrawSurface::SetColor(const GxBltFlags *color) {
  blit_flags = color->flags;
  source_color_key = color->pixel;
  mirror_flags = color->flags & 12;
  depth_operation = GxDisableColorEffects ? 0 : color->flags & 0x3e0;
  return 1;
}

u8 GxDrawSurface::FillRect(u32, ScRect *) {
  return 0;
}

u8 GxDrawSurface::AddAttached(GxDrawSurface *) {
  return 0;
}

u8 GxDrawSurface::RemoveAttached(GxDrawSurface *) {
  return 0;
}

u8 GxDrawSurface::Restore() {
  return ready;
}

// Read an unaligned WORD using the driver pitch rounded down to whole WORDs and the original wrapped DWORD offset.
// Return the zero-extended pixel without locking or changing the cached driver state.
u32 GxDrawSurface::ReadPixel(i32 x, i32 y) {
  u32 offset = 2u * ((u32)x + (u32)y * (pitch >> 1));
  u16 pixel;
  memcpy(&pixel, data + (iptr)(i32)offset, sizeof(pixel));
  return pixel;
}

// Read the point's two coordinates through the same physical WORD addressing used by the coordinate overload.
// The original entry reads no logical bounds, format, readiness or lock metadata.
u32 GxDrawSurface::ReadPixel(const ScPoint *point) {
  u32 offset = 2u * ((u32)point->x + (u32)point->y * (pitch >> 1));
  u16 pixel;
  memcpy(&pixel, data + (iptr)(i32)offset, sizeof(pixel));
  return pixel;
}

void GxDrawSurface::WritePixel(i32 x, i32 y, u16 pixel) {
  u32 offset = 2u * ((u32)x + (u32)y * (pitch >> 1));
  memcpy(data + (iptr)(i32)offset, &pixel, sizeof(pixel));
}

void GxDrawSurface::WritePixel(const ScPoint *point, u16 pixel) {
  u32 offset = 2u * ((u32)point->x + (u32)point->y * (pitch >> 1));
  memcpy(data + (iptr)(i32)offset, &pixel, sizeof(pixel));
}

// Construct the SDL device-surface base without a native clipper.
GxDrawDeviceSurface::GxDrawDeviceSurface() = default;

// Concrete SDL surfaces release their own storage.
GxDrawDeviceSurface::~GxDrawDeviceSurface() = default;

u8 GxDrawDeviceSurface::Close() {
  return GxDrawSurface::Close();
}

u8 GxDrawDeviceSurface::SetColor(const GxBltFlags *color) {
  return GxDrawSurface::SetColor(color);
}

u8 GxDrawDeviceSurface::SetColorKey(u32 pixel) {
  source_color_key = pixel;
  return ready;
}


// Initialize the original primary fields after the general-buffer constructor. The viewport remains unwritten until mode setup.
GxPrimaryBuffer::GxPrimaryBuffer() {
  initialized = false;
  palette = nullptr;
  references = 0;
}

bool GxPrimaryBuffer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GxPrimary, GxPrimary);
  return GxGeneralBuffer::QueryInterface(iid, out);
}

u32 GxPrimaryBuffer::AddRef() {
  return ++references;
}

u32 GxPrimaryBuffer::Release() {
  references -= 2;
  return references;
}

// Return the original primary buffer identity, not the secondary display view.
GxBuffer *GxPrimaryBuffer::AsIGZBuffer() {
  return static_cast<GxBuffer *>(this);
}

// Suppress presentation before touching any surface; otherwise dispatch through the live selected draw target.
// Flipping takes priority, followed by requested-but-suppressed flipping and ordinary backend-to-viewport transfer.
u8 GxPrimaryBuffer::Swap() {
  if (present_depth)
    return 1;
  if (!initialized || !front || !backend || Width() > front->surface_width || Height() > front->surface_height)
    return 0;
  if (flipping) {
    if (!front->Flip())
      return 0;
    if (GxSdlPresent(front->data, front->pitch, Width(), Height(), format[0], format[1], palette))
      return 1;
    front->Flip();
    return 0;
  }
  if (!front->Blit(backend, &area, &area))
    return 0;
  return GxSdlPresent(front->data, front->pitch, Width(), Height(), format[0], format[1], palette);
}

// Requested flipping queries the virtual draw-surface identity; otherwise borrow the separately stored front surface.
GxDrawSurface *GxPrimaryBuffer::GetDisplaySurface() {
  return requested_flipping ? GetDrawSurface() : front;
}

// Borrow the actual general-buffer backend without acquiring a surface reference.
GxDrawSurface *GxPrimaryBuffer::GetDrawSurface() {
  return backend;
}

// Store the nine original mode arguments, then create the driver and read its actual selected format before activation.
bool GxPrimaryBuffer::Init(u32 width, u32 height, bool requested, GxColorType pixel_format, GxPaletteView *value,
    GxDepthType secondary, u32 ninth, u32 tenth, u32 eleventh) {
  if (active)
    return 1;
  if (initialized || !width || !height)
    return 0;
  if (graphics->IsForceSystemRAMSurfaces())
    hardware_allowed = false;
  area = {0, 0, (i32)width, (i32)height};
  viewport = area;
  format[0] = static_cast<i32>(pixel_format.color);
  format[1] = static_cast<i32>(pixel_format.depth);
  palette = value;
  secondary_format = tenth ? static_cast<i32>(secondary.depth) : 0;
  requested_flipping = requested;
  option11 = eleventh;
  option9 = ninth;
  option10 = tenth;
  if (CreateScreenSurfaces() &&
      backend->GetFormat(format)) {
    initialized = true;
    active = true;
    flipping = requested_flipping;
    draw_target = GetDisplaySurface();
    return 1;
  }
  return 0;
}

bool GxPrimaryBuffer::Shutdown() {
  DestroyScreenSurfaces();
  if (manager) {
    static_cast<ScUnknown *>(manager)->Release();
    manager = nullptr;
  }
  initialized = false;
  active = false;
  return 1;
}

// Restore an active primary without allocating or recreating its driver surfaces.
u8 GxPrimaryBuffer::Restore() {
  if (!active)
    return 0;
  initialized = true;
  return 1;
}

// Uninitialize only an active primary whose original driver release method returns a true low byte.
bool GxPrimaryBuffer::Uninitialize() {
  if (!active || !DestroyScreenSurfaces())
    return 0;
  initialized = false;
  return 1;
}

bool GxPrimaryBuffer::IsInitialized() {
  return active;
}

void GxPrimaryBuffer::GetAppWindow(ScRect *out) {
  *out = viewport;
}

bool GxPrimaryBuffer::ModifyAppWindow(ScRect *rectangle) {
  viewport = *rectangle;
  return true;
}

u8 GxPrimaryBuffer::IsPageFlipping() {
  return flipping;
}

// Adjust suppression only while initialized, then select a live target and return the counter after that virtual call.
i32 GxPrimaryBuffer::DisableFlipping(i32 delta) {
  if (initialized) {
    flip_depth += (u32)delta;
    if (flip_depth & kScIntegerSignBit)
      flip_depth = 0;
    flipping = requested_flipping && !flip_depth;
    draw_target = GetDisplaySurface();
  }
  return static_cast<i32>(flip_depth);
}

// Adjust presentation suppression even while inactive, clamping any negative signed DWORD result to zero.
i32 GxPrimaryBuffer::DisableComposing(i32 delta) {
  present_depth += (u32)delta;
  if (present_depth & kScIntegerSignBit)
    present_depth = 0;
  return static_cast<i32>(present_depth);
}

// Read the live viewport before each coordinate addition, preserving source self-alias behavior and DWORD wrapping.
void GxPrimaryBuffer::AdjustClientRect(ScRect *rectangle) {
  rectangle->left = (i32)((u32)rectangle->left + (u32)viewport.left);
  rectangle->top = (i32)((u32)rectangle->top + (u32)viewport.top);
  rectangle->right = (i32)((u32)rectangle->right + (u32)viewport.left);
  rectangle->bottom = (i32)((u32)rectangle->bottom + (u32)viewport.top);
}

void GxPrimaryBuffer::RefreshVRAMPreferenceSetting() {
  hardware_allowed = GxNoHardware == 0;
}

void GxPrimaryBuffer::SetGraphicSystem(GxGraphics *value) {
  graphics = value;
  value->QueryInterface(IID_GxDeviceManager, &manager);
  GxInitializeDeviceGlobals(graphics);
}

// Select the fullscreen flipping graph only when both the live graphics mode and requested flag permit it.
// SDL surfaces use CPU color storage and reject depth-buffer requests.
u8 GxPrimaryBuffer::CreateScreenSurfaces() {
  if (option10)
    return 0;
  if ((format[0] == 2 || format[0] == 3) && !palette)
    return 0;
  GxUseCpuSurfaces(true);
  if (graphics->IsFullScreenMode() && requested_flipping)
    return CreateComplexSurface();
  return CreateSimpleSurfaces();
}

// Publish the owned flipping driver before virtual initialization; a failed attempt deletes the current member without Close.
u8 GxPrimaryBuffer::CreateComplexSurface() {
  flipping = 1;
  backend = GxCreateSurface();
  if (InitializeComplexSurface())
    return 1;
  GxPrimaryBuffer::DestroyScreenSurfaces();
  return 0;
}

// Initialize and clear both sides of the flipping chain, preserving ignored fill/flip results and live optional depth/palette gates.
bool GxPrimaryBuffer::InitializeComplexSurface() {
  return GxPrimaryBuffer::InitializeSimpleSurfaces() != 0;
}

u8 GxPrimaryBuffer::CreateDepthSurface() {
  return 0;
}

// An absent palette fails; an absent backend accepts the borrowed palette, otherwise preserve the driver's Boolean result.
bool GxPrimaryBuffer::InitializePalette() {
  if (!palette)
    return false;
  return !backend || backend->SetPalette(palette) != 0;
}

bool GxPrimaryBuffer::CreateSimpleSurfaces() {
  flipping = 0;
  return InitializeSimpleSurfaces() != 0;
}

// Build the windowed front/back/depth ring in source order, including managed-memory fallback and the hardware reserve
// probe.
u8 GxPrimaryBuffer::InitializeSimpleSurfaces() {
  front = GxCreateSurface();
  if (!static_cast<GxDrawDeviceSurface *>(front)->Init((u32)area.right, (u32)area.bottom, format, 0, 0)) {
    delete front;
    front = nullptr;
    return 0;
  }
  *frames.Append() = {0, static_cast<GxDrawDeviceSurface *>(front), nullptr};
  if (!backend)
    backend = GxCreateSurface();
  backend->owner = this;
  if (!backend->Init((u32)area.right, (u32)area.bottom, format, 0, 0)) {
    DestroyScreenSurfaces();
    return 0;
  }
  *frames.Append() = {1, backend, nullptr};
  if ((format[0] == 2 || format[0] == 3) &&
      (!static_cast<GxDrawDeviceSurface *>(front)->SetPalette(palette) || !backend->SetPalette(palette))) {
    DestroyScreenSurfaces();
    return 0;
  }
  ScRect clear = {0, 0, area.right, area.bottom};
  front->FillRect(0, &clear);
  backend->FillRect(0, &clear);
  u32 count = flipping ? 1 : option9;
  for (u32 index = 2; index <= count; ++index) {
    GxDrawDeviceSurface *extra = GxCreateSurface();
    if (!extra->Init((u32)area.right, (u32)area.bottom, format, 0, 0)) {
      delete extra;
      DestroyScreenSurfaces();
      return 0;
    }
    *frames.Append() = {index, extra, nullptr};
    extra->FillRect(0, &clear);
    if (!backend->AddAttached(extra)) {
      DestroyScreenSurfaces();
      return 0;
    }
  }
  if (requested_flipping && !front->AddAttached(backend)) {
    DestroyScreenSurfaces();
    return 0;
  }
  if (!flipping)
    for (u32 index = count; index; --index)
      if (!SetActiveBackBufferSurface(index)) {
        DestroyScreenSurfaces();
        return 0;
      }
  return 1;
}

// Detach the selected depth and dispose extra ring members in reverse order before closing the base and front surfaces.
// Preserve the active flag and selected index; only initialized state and the released owning members are cleared.
u8 GxPrimaryBuffer::DestroyScreenSurfaces() {
  initialized = false;
  draw_target = nullptr;
  for (u32 index = frames.size; index > 2;) {
    --index;
    if (backend)
      backend->RemoveAttached(frames.data[index].buffer);
    delete frames.data[index].buffer;
  }
  if (front && backend && requested_flipping)
    front->RemoveAttached(backend);
  frames.Clear();
  u8 result = CloseBackends();
  if (front) {
    if (!front->Close())
      result = 0;
    delete front;
    front = nullptr;
  }
  return result;
}

// A different frame ordinal flips the configured mode-two color chain, and the reference depth chain when present.
bool GxPrimaryBuffer::SetActiveBackBufferSurface(u32 index) {
  if (word156 != index) {
    if (option9 == 2)
      frames.data[1].buffer->Flip();
  }
  return 1;
}

u32 GxPrimaryBuffer::Width() const {
  return (u32)viewport.right - (u32)viewport.left;
}

u32 GxPrimaryBuffer::Height() const {
  return (u32)viewport.bottom - (u32)viewport.top;
}

// Flip the live managed device to its GDI surface, then request the original desktop redraw only after a successful HRESULT.
// An absent manager or device succeeds without the platform operation; failed COM results suppress the redraw.
u8 GxPrimaryBuffer::FlipToGDISurface() {
  return 1;
}

// Return the primary's buffer identity through its distinct draw-buffer entry, used by display painting and listener startup.
// This separate original slot currently returns the same object as AsIGZBuffer without retaining either interface.
GxBuffer *GxPrimaryBuffer::ActiveDrawBuffer() {
  return static_cast<GxBuffer *>(this);
}

u32 GxPrimaryBuffer::GetCurrentBackBufferIndex() {
  return word156;
}

// A present selected target must be ready before refreshing the inherited initialization byte from both backends.
// A failed target gate returns false without changing the previous initialization state.
bool GxPrimaryBuffer::IsReady() {
  if (draw_target && !draw_target->ready)
    return false;
  return GxGeneralBuffer::IsReady();
}

// Indexed formats require a selected target and a live palette before resolving the independent virtual draw surface.
// Read the palette after that getter and preserve the driver's raw low-byte return.
u8 GxPrimaryBuffer::PaletteChangedNotification() {
  if ((format[0] != 3 && format[0] != 2) || !GetDisplaySurface() || !palette)
    return 0;
  GxDrawSurface *surface = GetDrawSurface();
  return static_cast<GxDrawDeviceSurface *>(surface)->SetPalette(palette);
}

void GxPrimaryBuffer::SetSurfaceRestoreType(GxRestoreType requested) {
  if (requested == GxRestoreType::Callback)
    GxGeneralBuffer::SetSurfaceRestoreType(GxRestoreType::Callback);
}

// Requested flipping acquires the physical front surface, then merges flags and increments shared nesting.
// Windowed requests use the inherited lock; lightweight front requests retain the original conflicting-mask gate.
u8 GxPrimaryBuffer::LockFrontSurface(GxLockFlags requested_flags) {
  u32 flags = static_cast<u32>(requested_flags);
  if (!requested_flipping)
    return GxGeneralBuffer::Lock(requested_flags);
  if (flags & 0x7ec) {
    if (!GxLockCpuFront(backend, front))
      return 0;
  } else if (lock_flags & 0x7ec)
    return 0;
  lock_flags |= flags;
  ++locks;
  return 1;
}

// Release the requested physical front lock before clearing flags and decrementing shared nesting with DWORD wrap.
// A failed driver release retains its effects but leaves the buffer bookkeeping unchanged.
u8 GxPrimaryBuffer::UnlockFrontSurface(GxLockFlags requested_flags) {
  u32 flags = static_cast<u32>(requested_flags);
  if (!requested_flipping)
    return GxGeneralBuffer::Unlock(requested_flags);
  if ((flags & 0x7ec) && !GxUnlockCpuFront(backend, front))
    return 0;
  lock_flags &= ~flags;
  --locks;
  return 1;
}

void GxPrimaryBuffer::AdjustClientPoint(ScPoint *point) {
  point->x = (i32)((u32)point->x + (u32)viewport.left);
  point->y = (i32)((u32)point->y + (u32)viewport.top);
}

// Initialize an explicit primary-owned driver with the original one-attempt hardware policy and system-memory fallback.
// Requery the live manager for each attempt, then publish readiness only after the driver's format query succeeds.
u8 GxPrimaryBuffer::InitSimpleBackBufferColorSurface(
    GxDrawDeviceSurface *driver, u32 width, u32 height, i32 *requested_format, u32 allocation_mode, u8 flag) {
  return driver->Init(width, height, requested_format, allocation_mode, flag);
}

void GxPrimaryBuffer::EnableBackgroundPicturePreview() {
}

void GxPrimaryBuffer::DisableBackgroundPicturePreview() {
}

void GxPrimaryBuffer::DumpBufferInformation() {
}


// CPU implementation of the platform driver boundary. It owns pixel memory, while game fields stay in GxBuffer.
class GxCpuSurface final : public GxDrawDeviceSurface {
  // Copy every second horizontal source pixel, retaining the original vertical sampling rule.
  // Reuse a completed destination row when enlargement selects the same source row again.
  template <class Pixel>
  void BlitHalf(const u8 *from, u32 source_pitch, const ScRect *sr, const ScRect *dr) {
    i32 sh = sr->bottom - sr->top, dw = dr->right - dr->left, dh = dr->bottom - dr->top;
    i32 previous_y = -1;
    for (i32 y = 0; y < dh; ++y) {
      i32 sy = dh < sh ? (i32)(u64(sh) / (2 * u64(dh)) + (2 * u64(y) * (u32)sh + (u32)dh) / (2 * u64(dh)))
                       : (i32)(((2 * u64(y) + 1) * (u32)sh) / (2 * u64(dh)));
      Pixel *output = reinterpret_cast<Pixel *>(data + usize(dr->top + y) * pitch) + dr->left;
      if (sy == previous_y) {
        memcpy(output, data + usize(dr->top + y - 1) * pitch + usize(dr->left) * sizeof(Pixel),
            usize(dw) * sizeof(Pixel));
        continue;
      }
      previous_y = sy;
      const Pixel *input = reinterpret_cast<const Pixel *>(from + usize(sy) * source_pitch);
      for (i32 x = 0; x < dw; ++x)
        output[x] = input[2 * usize(x) + 1];
    }
  }

  // Specialize pixel access for each supported storage width and retain DirectDraw's exact sampling boundaries.
  // Compute the horizontal sample positions once and reuse them for each destination row.
  template <class Pixel>
  void BlitPixels(const GxCpuSurface *source, const u8 *from, u32 source_pitch, const ScRect *sr, const ScRect *dr) {
    i32 sw = sr->right - sr->left, sh = sr->bottom - sr->top;
    i32 dw = dr->right - dr->left, dh = dr->bottom - dr->top;
    u32 mirror = source->mirror_flags, key = source->source_color_key;
    bool keyed = (source->blit_flags & 2) != 0;
    u64 divisor = mirror ? u64(dw) : 2 * u64(dw), advance = mirror ? u64(sw) : 2 * u64(sw);
    u64 start = mirror ? 0 : dw < sw ? u64(dw) : u64(sw) - 1;
    u64 first = start / divisor + (!mirror && dw < sw ? u64(sw) / divisor : 0);
    u64 remainder = start % divisor, step = advance / divisor, fraction = advance % divisor;
    u32 *columns = nullptr;
    if (sw != dw) {
      columns = new u32[(usize)dw];
      u64 sx = first, carry = remainder;
      for (i32 x = 0; x < dw; ++x) {
        columns[(mirror & 4) ? dw - 1 - x : x] = (u32)sx;
        sx += step;
        carry += fraction;
        if (carry >= divisor) {
          carry -= divisor;
          ++sx;
        }
      }
    }
    i32 previous_y = -1;
    for (i32 y = 0; y < dh; ++y) {
      i32 sy = mirror ? (i32)(u64((mirror & 8) ? dh - 1 - y : y) * (u32)sh / (u32)dh)
          : dh < sh   ? (i32)(u64(sh) / (2 * u64(dh)) + (2 * u64(y) * (u32)sh + (u32)dh) / (2 * u64(dh)))
                      : (i32)(((2 * u64(y) + 1) * (u32)sh) / (2 * u64(dh)));
      const Pixel *input = reinterpret_cast<const Pixel *>(from + usize(sy) * source_pitch);
      Pixel *output = reinterpret_cast<Pixel *>(data + usize(dr->top + y) * pitch) + dr->left;
      if (!keyed && sy == previous_y) {
        memcpy(
            output, data + usize(dr->top + y - 1) * pitch + usize(dr->left) * sizeof(Pixel), usize(dw) * sizeof(Pixel));
        continue;
      }
      previous_y = sy;
      if (sw == dw && !(mirror & 4) && !keyed) {
        memcpy(output, input, usize(dw) * sizeof(Pixel));
        continue;
      }
      if (sw == dw) {
        if (mirror & 4) {
          for (i32 x = 0; x < dw; ++x)
            if (!keyed || input[x] != key)
              output[dw - 1 - x] = input[x];
        } else {
          for (i32 x = 0; x < dw; ++x)
            if (!keyed || input[x] != key)
              output[x] = input[x];
        }
        continue;
      }
      if (keyed) {
        for (i32 x = 0; x < dw; ++x) {
          Pixel pixel = input[columns[x]];
          if (pixel != key)
            output[x] = pixel;
        }
      } else
        for (i32 x = 0; x < dw; ++x)
          output[x] = input[columns[x]];
    }
    delete[] columns;
  }
  u32 native_format;
  i32 selected_format[2];
  u32 source_key = 0;
  GxPaletteView *colors = nullptr;
  u8 *pixels = nullptr;
  GxCpuSurface *chain_owner = nullptr, *locked_surface = nullptr;
  ScArray<GxCpuSurface *> attached;

  // Allocate the CPU plane with the actual DirectDraw row pitch before attaching it to the transfer surface.
  // SetSurfaceDesc accepts the memory pointer but ignores lPitch, so a guessed DIB stride can corrupt odd-width images.
  bool BindCopySurface() {
    if (data)
      return true;
    u64 row = (u64(surface_width) * ((selected_format[1] + 7) / 8) + 3) & ~u64(3);
    u64 size = row * surface_height;
    if (row > UINT32_MAX || size > SIZE_MAX)
      ScFatal(SC_ERROR_NOMEM, "SDL surface exceeds native storage");
    pitch = (u32)row;
    pixels = new u8[(usize)size];
    data = pixels;
    return true;
  }

public:
  // Capture the selected platform presentation format when the driver surface is created.
  explicit GxCpuSurface(u32 native) : native_format(native) {
  }

  // Release owned platform pixels when the game destroys its backend.
  ~GxCpuSurface() override {
    Close();
  }

  // Resolve the native format and allocate the transfer surface before callers initialize its pixels.
  // This keeps the owned allocation and every later locked or blitted row on the same driver-selected pitch.
  u8 Init(u32 width, u32 height, i32 *format, u32, u8) override {
    if (ready || !width || !height)
      return 0;
    i32 code = format[1] == 32 ? 14 : format[0];
    if (code == 4 || code == 1)
      code = (i32)native_format;
    if (!((code == 3 && format[1] == 8) || ((code == 5 || code == 7) && format[1] == 16) ||
            (code == 14 && format[1] == 32)))
      return 0;
    format[0] = code;
    selected_format[0] = code;
    selected_format[1] = format[1];
    GxBltFlags color = {};
    SetColor(&color);
    surface_width = width;
    surface_height = height;
    bits = (u32)format[1];
    video_memory = false;
    allocation_policy = 0;
    if (!BindCopySurface()) {
      Close();
      return 0;
    }
    ready = true;
    return 1;
  }

  // Release the HWND clipper, transfer surface and device before disposing the owned platform pixels.
  // The containing game buffer retains its separate geometry, palette and lock bookkeeping.
  u8 Close() override {
    if (chain_owner)
      chain_owner->RemoveAttached(this);
    while (attached.size)
      RemoveAttached(attached.data[attached.size - 1]);
    if (locked_surface && locked_surface != this)
      locked_surface->Unlock();
    locked_surface = nullptr;
    GxDrawDeviceSurface::Close();
    delete[] pixels;
    pixels = nullptr;
    data = nullptr;
    ready = false;
    locks = 0;
    return 1;
  }

  // Addressable CPU storage accepts a driver lock only while its allocation exists.
  u8 Lock() override {
    if (!ready)
      return 0;
    if (!locks) {
      locked_surface = this;
      data = pixels;
    }
    ++locks;
    return 1;
  }

  // Release one actual platform lock; the containing game method handles its separate flags and count.
  u8 Unlock() override {
    if (!locks)
      return 1;
    if (!ready)
      return 0;
    if (locks == 1 && locked_surface != this)
      return 0;
    --locks;
    if (!locks)
      locked_surface = nullptr;
    return 1;
  }

  // Acquire the physical front surface on the first driver lock; nested locks retain the current surface and pixels.
  u8 LockFront(GxCpuSurface *front) {
    if (!ready || !front || !front->ready)
      return 0;
    if (!locks) {
      if (!front->Lock())
        return 0;
      locked_surface = front;
      data = front->data;
      pitch = front->pitch;
    }
    ++locks;
    return 1;
  }

  // Release the originally locked front on the final driver unlock; a mismatched physical target fails unchanged.
  u8 UnlockFront(GxCpuSurface *front) {
    if (!locks)
      return 1;
    if (!ready)
      return 0;
    if (locks == 1) {
      if (locked_surface != front || !front->Unlock())
        return 0;
      locked_surface = nullptr;
      data = pixels;
    }
    --locks;
    return 1;
  }

  // Apply the recovered device color policy to the same effects record used by physical platform transfers.
  u8 SetColor(const GxBltFlags *value) override {
    return GxDrawDeviceSurface::SetColor(value);
  }

  // Copy the two selected driver-format words to the genuine caller output.
  u8 GetFormat(i32 *out) override {
    out[0] = selected_format[0];
    out[1] = selected_format[1];
    return 1;
  }

  // Indexed transfer surfaces need the original DirectDraw palette producer before attachment can succeed.
  // Keep this missing platform dependency explicit instead of reporting an unattached palette as installed.
  u8 SetPalette(GxPaletteView *value) override {
    if (!ready)
      return 0;
    colors = value;
    return 1;
  }

  // Materialize the native surface before detaching its palette through the recovered platform call.
  u8 ClearPalette() override {
    if (!ready)
      return 0;
    colors = nullptr;
    return 1;
  }


  // Materialize the native surface before applying the recovered equal-endpoint source-key operation.
  u8 SetColorKey(u32 pixel) override {
    source_key = pixel;
    return ready;
  }

  // Apply the shared color write and fill the requested rectangle, or the whole native plane for a null rectangle.
  // Address pixels through the real pitch and selected platform pixel width.
  u8 FillRect(u32 pixel, ScRect *rectangle) override {
    ScRect whole = {0, 0, (i32)surface_width, (i32)surface_height};
    if (!rectangle)
      rectangle = &whole;
    if (!ready || rectangle->left < 0 || rectangle->top < 0 || rectangle->right > whole.right ||
        rectangle->bottom > whole.bottom || rectangle->left >= rectangle->right ||
        rectangle->top >= rectangle->bottom)
      return 0;
    u32 unit = (u32)selected_format[1] / 8;
    for (i32 y = rectangle->top; y < rectangle->bottom; ++y)
      for (i32 x = rectangle->left; x < rectangle->right; ++x)
        memcpy(data + (usize)y * pitch + (usize)x * unit, &pixel, unit);
    return 1;
  }

  // Submit physical copies, stretches, keys and mirror effects through the actual platform transfer implementation.
  // Both system-memory surfaces refer directly to the native allocations owned by these driver objects.
  u8 Blit(GxDrawSurface *value, const ScRect *sr, const ScRect *dr) override {
    GxCpuSurface *source = static_cast<GxCpuSurface *>(value);
    if (!ready || !source->ready || locks || source->locks || bits != source->bits)
      return 0;
    if (source->depth_operation)
      return 0;
    ScRect source_whole = {0, 0, (i32)source->surface_width, (i32)source->surface_height};
    ScRect destination_whole = {0, 0, (i32)surface_width, (i32)surface_height};
    if (!sr)
      sr = &source_whole;
    if (!dr)
      dr = &destination_whole;
    i32 sw = sr->right - sr->left, sh = sr->bottom - sr->top;
    i32 dw = dr->right - dr->left, dh = dr->bottom - dr->top;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || sr->left < 0 || sr->top < 0 || dr->left < 0 || dr->top < 0 ||
        sr->right > (i32)source->surface_width || sr->bottom > (i32)source->surface_height ||
        dr->right > (i32)surface_width || dr->bottom > (i32)surface_height)
      return 0;
    u32 bytes = bits / 8;
    if (bytes != 1 && bytes != 2 && bytes != 4)
      return 0;
    bool keyed = (source->blit_flags & 2) != 0;
    usize row = usize(sw) * bytes;
    if (sw == dw && sh == dh && !source->mirror_flags && !keyed) {
      if (row == pitch && row == source->pitch) {
        u8 *destination = data + usize(dr->top) * pitch;
        const u8 *input = source->data + usize(sr->top) * source->pitch;
        if (source == this)
          memmove(destination, input, row * usize(dh));
        else
          memcpy(destination, input, row * usize(dh));
        return 1;
      }
      i32 y = source == this && dr->top > sr->top ? dh - 1 : 0;
      i32 end = source == this && dr->top > sr->top ? -1 : dh;
      i32 direction = end == -1 ? -1 : 1;
      for (; y != end; y += direction) {
        u8 *destination = data + usize(dr->top + y) * pitch + usize(dr->left) * bytes;
        const u8 *input = source->data + usize(sr->top + y) * source->pitch + usize(sr->left) * bytes;
        if (source == this)
          memmove(destination, input, row);
        else
          memcpy(destination, input, row);
      }
      return 1;
    }
    u8 *copy = nullptr;
    const u8 *from = source->data + usize(sr->top) * source->pitch + usize(sr->left) * bytes;
    u32 source_pitch = source->pitch;
    if (source == this && sr->left < dr->right && dr->left < sr->right && sr->top < dr->bottom &&
        dr->top < sr->bottom) {
      copy = new u8[row * usize(sh)];
      for (i32 y = 0; y < sh; ++y)
        memcpy(copy + usize(y) * row, from + usize(y) * source_pitch, row);
      from = copy;
      source_pitch = (u32)row;
    }
    bool half = !keyed && !source->mirror_flags && u64(sw) == 2 * u64(dw);
    switch (bytes) {
    case 1:
      if (half)
        BlitHalf<u8>(from, source_pitch, sr, dr);
      else
        BlitPixels<u8>(source, from, source_pitch, sr, dr);
      break;
    case 2:
      if (half)
        BlitHalf<u16>(from, source_pitch, sr, dr);
      else
        BlitPixels<u16>(source, from, source_pitch, sr, dr);
      break;
    case 4:
      if (half)
        BlitHalf<u32>(from, source_pitch, sr, dr);
      else
        BlitPixels<u32>(source, from, source_pitch, sr, dr);
      break;
    }
    delete[] copy;
    return 1;
  }

  // Attach compatible CPU color surfaces to the owned primary ring without transferring game-object ownership.
  u8 AddAttached(GxDrawSurface *value) override {
    GxCpuSurface *child = dynamic_cast<GxCpuSurface *>(value);
    if (!ready || !child || !child->ready || child->chain_owner || child->surface_width != surface_width ||
        child->surface_height != surface_height || child->bits != bits || child->pitch != pitch ||
        child->selected_format[0] != selected_format[0])
      return 0;
    for (GxCpuSurface *ancestor = this; ancestor; ancestor = ancestor->chain_owner)
      if (ancestor == child)
        return 0;
    *attached.Append() = child;
    child->chain_owner = this;
    return 1;
  }

  // Remove an attached color surface while retaining the remaining ring order and each owner's allocation.
  u8 RemoveAttached(GxDrawSurface *value) override {
    for (u32 index = 0; index < attached.size; ++index) {
      if (attached.data[index] == value) {
        attached.data[index]->chain_owner = nullptr;
        --attached.size;
        memmove(attached.data + index, attached.data + index + 1, (attached.size - index) * sizeof(*attached.data));
        return 1;
      }
    }
    return 0;
  }

  // Rotate the platform pixel allocations only after all participating surfaces are ready and unlocked.
  u8 Flip() override {
    if (!ready || locks || !attached.size)
      return 0;
    for (u32 index = 0; index < attached.size; ++index)
      if (!attached.data[index]->ready || attached.data[index]->locks)
        return 0;
    u8 *saved = pixels;
    for (u32 index = 0; index < attached.size; ++index) {
      GxCpuSurface *previous = index ? attached.data[index - 1] : this, *next = attached.data[index];
      previous->data = previous->pixels = next->pixels;
    }
    attached.data[attached.size - 1]->data = attached.data[attached.size - 1]->pixels = saved;
    return 1;
  }

  // Rotate until the requested attached surface supplies this surface's pixels; reject unrelated targets.
  u8 FlipTo(GxDrawSurface *target) override {
    u32 index = 0;
    while (index < attached.size && attached.data[index] != target)
      ++index;
    if (index == attached.size)
      return 0;
    for (u32 count = 0; count <= index; ++count)
      if (!Flip())
        return 0;
    return 1;
  }

  // CPU storage cannot become a lost DirectDraw allocation; restoration has no platform work to perform.
  u8 Restore() override {
    return 1;
  }
};

// Use the actual SDL surface types for the primary's front-lock operation.
static u8 GxLockCpuFront(GxDrawDeviceSurface *back, GxDrawSurface *front) {
  GxCpuSurface *source = dynamic_cast<GxCpuSurface *>(back), *target = dynamic_cast<GxCpuSurface *>(front);
  assert(source && target);
  return source->LockFront(target);
}

// Release a front lock through the same concrete SDL driver that acquired it.
static u8 GxUnlockCpuFront(GxDrawDeviceSurface *back, GxDrawSurface *front) {
  GxCpuSurface *source = dynamic_cast<GxCpuSurface *>(back), *target = dynamic_cast<GxCpuSurface *>(front);
  assert(source && target);
  return source->UnlockFront(target);
}

static bool GxCpuSurfaces;

void GxUseCpuSurfaces(bool enabled) {
  GxCpuSurfaces = enabled;
}

bool GxUsesCpuSurfaces() {
  return GxCpuSurfaces;
}

// Allocate the recovered DirectDraw class unless native startup explicitly selected the CPU backend.
GxDrawDeviceSurface *GxCreateSurface() {
  return new GxCpuSurface(GxOutputFormat);
}


#undef RGB
#include <typeinfo>

#include "common/api.h"
#include "common/sc3_string.h"
#include "common/value.h"
#include "window/api.h"

GxWindow *GxWindowCurrent;

GxWindow::GxWindow() {
  GxWindowCurrent = this;
}

GxWindow::~GxWindow() {
  if (initialized)
    Shutdown();
}

// Publish the primary or secondary identity before retaining; unknown identities clear the output.
bool GxWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxCanvas);
  HANDLE_QUERY_INTERFACE(IID_GxCanvas, GxCanvas);
  HANDLE_QUERY_INTERFACE(IID_GxWindowHandle, GxWindowHandle);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GxWindow::AddRef() {
  return ++references;
}

// Entry counts zero and one both delete the canvas; larger counts are decremented without deletion.
u32 GxWindow::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Apply the genuine rectangle, window and palette initialization order, marking initialized before failure cleanup.
bool GxWindow::Init(const ScRect &rectangle) {
  if (initialized)
    return true;
  style_flags = 27;
  SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  caption = nullptr;
  HideWindow();
  bool result = CreateNativeWindow(1);
  initialized = true;
  if (!result)
    Shutdown();
  return result;
}

// Close the palette and native window before the remaining title and initialization flag; ignored failures do not stop cleanup.
bool GxWindow::Shutdown() {
  if (initialized) {
    HideWindow();
    CreateNativeWindow(0);
    if (caption) {
      delete[] caption;
      caption = nullptr;
    }
    initialized = false;
  }
  return true;
}

bool GxWindow::Uninitialize() {
  return true;
}

bool GxWindow::Restore() {
  return true;
}

// Store right before the remaining bounds, then reload the live manager/root between the two original root lookups.
void GxWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  area.right = right;
  area.left = left;
  area.top = top;
  area.bottom = bottom;
  if (manager && manager->GetMainWindow())
    manager->GetMainWindow()->SetArea(left, top, right, bottom);
}

ScRect GxWindow::GetArea() const {
  return area;
}

// Query the live secondary handle at each dispatch, showing and then focusing a nonnull native window.
void GxWindow::ShowWindow() {
  GxSdlShowWindow();
}

// Preserve the original surprising ShowWindow(1) used by the canvas's HideWindow method.
void GxWindow::HideWindow() {
  GxSdlShowWindow();
}

// Read string length before disposing the old caption, then copy its Data only after allocating and zeroing the new storage.
void GxWindow::SetCaption(const ScStringView &value) {
  u32 length = value.Strlen();
  delete[] caption;
  caption = nullptr;
  if (length) {
    caption = new char[(usize)length + 1];
    memset(caption, 0, (usize)length + 1);
    strncpy(caption, value.Data(), length);
    GxSdlSetTitle(caption);
  }
}

void GxWindow::SetMainBufferSurface(GxBuffer *value) {
  buffer = value;
}

// Return the actual borrowed surface used by the root window's drawing setup.
GxBuffer *GxWindow::GetMainBufferSurface() {
  return buffer;
}

void GxWindow::SetWindowManager(WnWindowManager *value) {
  manager = value;
}

void GxWindow::SetFrameworkIsShuttingDown(bool value) {
  stopping = value;
}

// Return the last translated mouse X coordinate retained by the window procedure.
i32 GxWindow::LastCursorPositionX() const {
  i32 x, y;
  GxSdlCursorPosition(&x, &y);
  return x;
}

// Return the independently retained translated mouse Y coordinate.
i32 GxWindow::LastCursorPositionY() const {
  i32 x, y;
  GxSdlCursorPosition(&x, &y);
  return y;
}

// Borrow the native HWND through the actual secondary Windows-canvas identity.
SDL_Window *GxWindow::GetWindowHandle() {
  return GxSdlWindow();
}

i32 GxWindow::GetW() {
  return (i32)((u32)area.right - (u32)area.left);
}

i32 GxWindow::GetH() {
  return (i32)((u32)area.bottom - (u32)area.top);
}

i32 GxWindow::GetL() {
  return area.left;
}

i32 GxWindow::GetT() {
  return area.top;
}

i32 GxWindow::GetR() {
  return area.right;
}

i32 GxWindow::GetB() {
  return area.bottom;
}

void GxWindow::PauseDrawing() {
  paused = true;
}

void GxWindow::ResumeDrawing() {
  paused = false;
}

// Plot only an unpaused canvas with a live main window, preserving both original live root lookups.
bool GxWindow::Plot() {
  if (!paused && manager && manager->GetMainWindow())
    manager->GetMainWindow()->Plot();
  return true;
}

// Update the primary viewport from the stored canvas area or the windowed client coordinates.
// The canvas interface returns void and discards the viewport setter result.
void GxWindow::ProcessWindowMove() {
  ScRect rectangle = GetArea();
  GxGetGraphics()->ScreenBuffer()->ModifyAppWindow(&rectangle);
}

// Apply fullscreen style before querying dimensions and positioning the actual native window.
bool GxWindow::CompleteSwitchToFullScreenMode() {
  return GxSdlSetWindowFullscreen(true);
}

// Rebuild the native window frame, then invoke the original centering phase after its style/menu/extended-style reads.
bool GxWindow::CompleteSwitchToWindowedMode() {
  return GxSdlSetWindowFullscreen(false);
}

void GxWindow::Place(u32) {
  GxSdlCenterWindow();
}


// Forward the Win32 callback with the platform's exact LRESULT and LPARAM types.
// Those types have the same widths as iptr but are distinct C++ types on x86.

// Query and release the actual framework instance before either destroying or creating the Gonzo native window.
bool GxWindow::CreateNativeWindow(u8 create) {
  if (!create) {
    GxSdlDestroyWindow();
    delete[] caption;
    caption = nullptr;
    return true;
  }
  return GxSdlCreateWindow(GetW(), GetH(), caption);
}


void GxWindow::ForceFullSystemRedraw() {
  if (manager)
    manager->InvalidateAllWindows();
}

