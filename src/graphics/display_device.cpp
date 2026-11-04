#define WIN32_LEAN_AND_MEAN
#include "graphics/display_device.h"

#include <SDL3/SDL.h>
#include <math.h>

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>
#include <typeinfo>

#include "common/api.h"
#include "common/sc3_cmdline.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/module.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_general_buffer.h"
#include "graphics/sc3_effects.h"
#include "graphics/sdl_platform.h"
#include "window/api.h"


// The wrapped circle-span offset uses bit thirty as its sign bit.
enum GxCircleOffsetFlag : u32 {
  kGxCircleOffsetSignBit = 0x40000000,
};

u8 GxDefaultAdapter[16] = {};

// Native pixel displacements and circle word offsets retain their separate 32-bit and 31-bit wrap periods.
enum GxWrappedOffsetModulus : i64 {
  kGxPixelOffsetModulus = 0x100000000,
  kGxCircleOffsetModulus = 0x80000000,
};

GxAdapter::GxAdapter() {
}

// Copy strings before scalar identity and borrowed device, then replace the ordered mode rows without a COM retain.
GxAdapter &GxAdapter::operator=(const GxAdapter &source) {
  name = source.name;
  description = source.description;
  version_low = source.version_low;
  version_high = source.version_high;
  memcpy(guid, source.guid, 16);
  display_id = source.display_id;
  modes.Assign(source.modes);
  return *this;
}

// Discover the current SDL displays and preserve their reported names and stable display identifiers.
bool GxAdapters::Discover() {
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  if (!displays)
    return false;
  SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  const char *driver = SDL_GetCurrentVideoDriver();
  for (int index = 0; index < count; ++index) {
    GxAdapter *adapter = Append();
    adapter->display_id = displays[index];
    if (displays[index] != primary)
      memcpy(adapter->guid, &displays[index], sizeof(displays[index]));
    const char *name = SDL_GetDisplayName(displays[index]);
    adapter->name.FromChar(driver ? driver : "");
    adapter->description.FromChar(name ? name : "");
  }
  SDL_free(displays);
  return true;
}

bool GxAdapters::CreateDevices() {
  return true;
}

// Copy actual fullscreen modes and pixel masks from each discovered SDL display.
bool GxAdapters::EnumerateModes() {
  for (GxAdapterLink *link = head.next; link != &head; link = link->next) {
    GxAdapter &adapter = static_cast<GxAdapterNode *>(link)->adapter;
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(adapter.display_id, &count);
    if (!modes)
      return false;
    for (int index = 0; index < count; ++index) {
      const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(modes[index]->format);
      if (format)
        adapter.modes.Append({(u32)modes[index]->w, (u32)modes[index]->h, format->bits_per_pixel,
            format->Amask, format->Rmask, format->Gmask, format->Bmask});
    }
    SDL_free(modes);
  }
  return true;
}

bool GxDisplay::DiscoverAdapters() {
  return adapters.Discover();
}

bool GxDisplay::ClearAdapters() {
  adapters.Clear();
  return true;
}

bool GxDisplay::EnumerateModes() {
  return adapters.EnumerateModes();
}

bool GxDisplay::SupportsMode(const GxMode *mode, const u8 *guid) {
  return adapters.Supports(mode, guid);
}

// Query desktop dimensions and depth in the original API order, returning the display record by value.
GxMode GxDisplay::GetScreenMetrics() {
  bool temporaryVideo = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
  if (temporaryVideo && !SDL_InitSubSystem(SDL_INIT_VIDEO))
    return {};
  const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
  const SDL_PixelFormatDetails *format = mode ? SDL_GetPixelFormatDetails(mode->format) : nullptr;
  GxMode result = mode && format ? GxMode{(u32)mode->w, (u32)mode->h, format->bits_per_pixel, format->Amask,
      format->Rmask, format->Gmask, format->Bmask} : GxMode{};
  if (temporaryVideo)
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  return result;
}

GxMode GxDisplay::GetGameMetrics() {
  return GxRequestedMode;
}

static u16 GxPackColor(u32 format, u8 red, u8 green, u8 blue) {
  switch (format) {
  case 5:
  case 10:
    return (u16)(((red & 248u) << 7) | ((green & 248u) << 2) | (blue >> 3));
  case 6:
    return (u16)(((red & 124u) << 9) | ((green & 248u) << 3) | (blue >> 3));
  case 7:
    return (u16)(((red & 248u) << 8) | ((green & 252u) << 3) | (blue >> 3));
  case 8:
    return (u16)(((red & 248u) << 8) | ((green & 248u) << 3) | (blue >> 2));
  case 9:
    return (u16)(((red & 248u) << 8) | ((green & 248u) << 3) | ((blue >> 2) & 254u));
  default:
    return 0;
  }
}

static u8 GxSoftwareShade(u8 channel, i32 offset) {
  i32 value = (i32)((u32)channel + (u32)offset);
  return value < 0 ? 0 : value > 255 ? 255 : (u8)value;
}

static bool GxSoftwareLess(u32 first, u32 second) {
  return (first ^ kScIntegerSignBit) < (second ^ kScIntegerSignBit);
}

static bool GxSoftwarePositive(u32 value) {
  return value && !(value & kScIntegerSignBit);
}

GxGeneralBuffer::GxGeneralBuffer() {
  if (!GxUsesCpuSurfaces() && !GxDeviceInitialized)
    GxInitializeDeviceGlobals(nullptr);
}

GxGeneralBuffer::~GxGeneralBuffer() {
  Shutdown();
}

// Publish and retain the known original buffer identity; unsupported queries clear the output.
bool GxGeneralBuffer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxBuffer);
  HANDLE_QUERY_INTERFACE(IID_GxBuffer, GxBuffer);
  HANDLE_QUERY_INTERFACE(IID_GxBufferSecondary, GxBufferSecondary);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GxGeneralBuffer::AddRef() {
  u32 result = ++references;
  counting_enabled = true;
  return result;
}

// Delete the concrete buffer at its final reference, returning zero without reading deleted storage.
u32 GxGeneralBuffer::Release() {
  if (counting_enabled && references != 1)
    return --references;
  if (counting_enabled)
    references = 0;
  delete this;
  return 0;
}

// Borrow the current physical driver through the secondary buffer identity without querying or retaining it.
// The installed getter shares code with GxWindow::GetR; its secondary receiver bias makes the same load read this field.
GxDrawSurface *GxGeneralBuffer::GetBackend() {
  return backend;
}

// Borrow the current depth driver through the secondary identity without acquiring or changing its ownership.
// The Windows leaf is shared with the window bottom getter; its exact source pointer return type remains unresolved.
GxDrawSurface *GxGeneralBuffer::GetDepthSurface() {
  return secondary_backend;
}

// Allocate the selected platform surface after the original zero-size/already-initialized gate.
bool GxGeneralBuffer::Init(u32 w, u32 h, GxColorType requested, GxPaletteView *colors, GxDepthType secondary,
    GxVramRequest options_value, bool flag) {
  GxVramRequest options = options_value;
  if (initialized || !w || !h)
    return false;
  area = {0, 0, (i32)w, (i32)h};
  format[0] = static_cast<i32>(requested.color);
  format[1] = static_cast<i32>(requested.depth);
  palette = colors;
  secondary_format = static_cast<i32>(secondary.depth);
  mode = static_cast<u32>(options);
  mode_flag = flag;
  if (!Setup())
    return false;
  initialized = true;
  return true;
}

// Close the owned drivers and clear restoration policy before marking the buffer uninitialized.
// Repeated shutdown succeeds.
bool GxGeneralBuffer::Shutdown() {
  CloseBackends();
  SetSurfaceRestoreType(GxRestoreType::None);
  initialized = false;
  return true;
}

// Uninitialize through the original shutdown sequence; the two public entries share their installed body.
bool GxGeneralBuffer::Uninitialize() {
  CloseBackends();
  SetSurfaceRestoreType(GxRestoreType::None);
  initialized = false;
  return true;
}

// Acquire a device lock for the original flag mask and preserve the caller's separate lock nesting.
u8 GxGeneralBuffer::Lock(GxLockFlags requested_flags) {
  u32 flags = static_cast<u32>(requested_flags);
  if (flags & 0x7ec) {
    if (!backend->Lock())
      return 0;
  } else if (lock_flags & 0x7ec)
    return 0;
  lock_flags |= flags;
  ++locks;
  return 1;
}

// Release the device lock selected by the flags before changing the buffer's lock bookkeeping.
u8 GxGeneralBuffer::Unlock(GxLockFlags requested_flags) {
  u32 flags = static_cast<u32>(requested_flags);
  if ((flags & 0x7ec) && !backend->Unlock())
    return 0;
  lock_flags &= ~flags;
  --locks;
  return 1;
}

// Acquire the secondary driver only for the original mask, then merge flags and increment the shared buffer count.
// Non-driver requests consult the primary flags; callback changes remain live when the bookkeeping is updated.
u8 GxGeneralBuffer::LockDepthSurface(GxLockFlags requested_flags) {
  u32 flags = static_cast<u32>(requested_flags);
  if (flags & 0x7ec) {
    if (!secondary_backend->Lock())
      return 0;
  } else if (lock_flags & 0x7ec)
    return 0;
  secondary_lock_flags |= flags;
  ++locks;
  return 1;
}

// Release the selected secondary driver before clearing its flags and decrementing the shared count.
// Failed driver unlocks retain their callback effects without performing either bookkeeping change.
u8 GxGeneralBuffer::UnlockDepthSurface(GxLockFlags requested_flags) {
  u32 flags = static_cast<u32>(requested_flags);
  if ((flags & 0x7ec) && !secondary_backend->Unlock())
    return 0;
  secondary_lock_flags &= ~flags;
  --locks;
  return 1;
}

bool GxGeneralBuffer::IsLocked() const {
  return locks != 0;
}

bool GxGeneralBuffer::IsDepthSurfaceLocked() const {
  return secondary_lock_flags != 0;
}

u32 GxGeneralBuffer::GetBytesPerPixel() const {
  return (u32)format[1] >> 3;
}

u8 GxGeneralBuffer::IsTexture() const {
  return mode_flag;
}

// Estimate storage for each present driver from four-pixel width rounding and its own depth word.
// Preserve DWORD wrap; readiness, initialization and actual driver pitch do not affect the result.
u32 GxGeneralBuffer::GetMemoryUsage() const {
  u32 width = (((u32)area.right - 1) | 3u) + 1, height = (u32)area.bottom, result = 0;
  if (backend)
    result = width * ((u32)format[1] >> 3) * height;
  if (secondary_backend)
    result += width * ((u32)secondary_format >> 3) * height;
  return result;
}

bool GxGeneralBuffer::IsInVideoMemory() const {
  return (!secondary_backend || secondary_backend->video_memory) && (!backend || backend->video_memory);
}

bool GxGeneralBuffer::IsReady() {
  initialized = (!backend || backend->ready) && (!secondary_backend || secondary_backend->ready);
  return initialized;
}

u32 GxGeneralBuffer::Width() const {
  return area.right;
}

u32 GxGeneralBuffer::Height() const {
  return area.bottom;
}

// Borrow the original read-only four-word bounds without copying or retaining the buffer.
const ScRect *GxGeneralBuffer::GetBufferArea() const {
  return &area;
}

void GxGeneralBuffer::GetBufferArea(ScRect *out) const {
  *out = area;
}

void GxGeneralBuffer::GetColorType(GxColorType *out) const {
  out->color = static_cast<GxColorType::Color>(format[0]);
  out->depth = static_cast<u32>(format[1]);
}

// Return the complete stored color type by value. The installed pooled body also implements the output overload.
GxColorType GxGeneralBuffer::GetColorType() const {
  GxColorType result(static_cast<GxColorType::Color>(format[0]));
  result.depth = static_cast<u32>(format[1]);
  return result;
}

// Expose the actual borrowed color record consumed by general-buffer software compositors.
const GxBltFlags *GxGeneralBuffer::GetBltFlags() const {
  return &color;
}

void GxGeneralBuffer::GetBltFlags(GxBltFlags *out) const {
  *out = color;
}

u16 GxGeneralBuffer::ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) {
  return GxPackColor((u32)format[0], red, green, blue);
}

// Unpack the six original WORD formats into BYTE channels in red/green/blue order.
// Unsupported formats return zero without touching the outputs; high red bits are not masked.
u8 GxGeneralBuffer::Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) {
  switch (format[0]) {
  case 5:
  case 10:
    *red = (u8)(255u * (pixel >> 10) / 31);
    *green = (u8)(255u * ((pixel >> 5) & 31) / 31);
    *blue = (u8)(255u * (pixel & 31) / 31);
    break;
  case 6:
    *red = (u8)(255u * (pixel >> 11) / 63);
    *green = (u8)(255u * ((pixel >> 5) & 31) / 31);
    *blue = (u8)(255u * (pixel & 31) / 31);
    break;
  case 7:
    *red = (u8)(255u * (pixel >> 11) / 31);
    *green = (u8)(255u * ((pixel >> 5) & 63) / 63);
    *blue = (u8)(255u * (pixel & 31) / 31);
    break;
  case 8:
  case 9:
    *red = (u8)(255u * (pixel >> 11) / 31);
    *green = (u8)(255u * ((pixel >> 6) & 31) / 31);
    *blue = format[0] == 8 ? (u8)(255u * (pixel & 63) / 63) : (u8)(255u * ((pixel >> 1) & 31) / 31);
    break;
  default:
    return 0;
  }
  return 1;
}

// Select the actual fixed-format packer; formats5 and10 share one function and unsupported formats return null.
GxPackFunction GxGeneralBuffer::GetRGBTo16ConverterPtr() const {
  switch (format[0]) {
  case 5:
  case 10:
    return GxPack555;
  case 6:
    return GxPack655;
  case 7:
    return GxPack565;
  case 8:
    return GxPack556;
  case 9:
    return GxPack555Shifted;
  default:
    return nullptr;
  }
}

// Return the format's actual unpacker without touching pixels or manufacturing a fallback for unsupported formats.
// Every selected native definition matches the effects-only callback type rather than casting incompatible returns.
GxUnpackFunction GxGeneralBuffer::Get16ToRGBConverterPtr() const {
  switch (format[0]) {
  case 5:
  case 10:
    return GxUnpack555;
  case 6:
    return GxUnpack655;
  case 7:
    return GxUnpack565;
  case 8:
    return GxUnpack556;
  case 9:
    return GxUnpack555Shifted;
  default:
    return nullptr;
  }
}

// Apply the original DWORD displacement to the native pixel plane, including defined backward rows in padded storage.
static u8 *GxPixelAddress(u8 *pixels, u32 offset) {
  i64 displacement = offset;
  if (offset & kScIntegerSignBit)
    displacement -= kGxPixelOffsetModulus;
  return pixels + displacement;
}

// Count unequal pixels over the unsigned minimum dimensions, using virtual WORD reads or direct BYTE rows.
// Preserve lock order, repeated dimension/depth getters and the original held locks on unsupported depths.
i32 GxCountDifferences(GxBuffer *first, GxBuffer *second) {
  if (!first->Lock(GxLockFlags::GetSetPixel))
    return -1;
  if (!second->Lock(GxLockFlags::GetSetPixel)) {
    first->Unlock(GxLockFlags::GetSetPixel);
    return -1;
  }
  u32 width = first->Width(), height, differences = 0;
  if (second->Width() < width)
    width = second->Width();
  height = first->Height();
  if (second->Height() < height)
    height = second->Height();
  if (first->GetBitsPerPixel() == 16) {
    for (u32 y = 0; y < height; ++y)
      for (u32 x = 0; x < width; ++x) {
        u16 pixel = (u16)first->GetPixel((i32)x, (i32)y);
        if (pixel != (u16)second->GetPixel((i32)x, (i32)y))
          ++differences;
      }
  } else {
    if (first->GetBitsPerPixel() != 8)
      return -1;
    u32 first_pitch = first->GetColorSurfaceStride(), second_pitch = second->GetColorSurfaceStride();
    u8 *first_data = static_cast<u8 *>(first->GetColorSurfaceBits()),
       *second_data = static_cast<u8 *>(second->GetColorSurfaceBits());
    u32 first_row = 0, second_row = 0;
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x)
        if (*GxPixelAddress(first_data, first_row + x) != *GxPixelAddress(second_data, second_row + x))
          ++differences;
      first_row += first_pitch;
      second_row += second_pitch;
    }
  }
  first->Unlock(GxLockFlags::GetSetPixel);
  second->Unlock(GxLockFlags::GetSetPixel);
  return (i32)differences;
}

u32 GxGeneralBuffer::CompareToBuffer(GxBuffer *other) {
  return GxCountDifferences(this, other);
}

// Count unequal secondary WORD pixels after checking depth and both secondary-presence predicates.
// Preserve the original primary Lock128 calls followed by secondary Unlock128 calls on the successful path.
i32 GxCountSecondaryDifferences(GxBuffer *first, GxBuffer *second) {
  if (first->GetBitsPerPixel() != 16 || !first->IsZBuffered() || !second->IsZBuffered())
    return -1;
  u32 width = first->Width(), height, differences = 0;
  if (second->Width() < width)
    width = second->Width();
  height = first->Height();
  if (second->Height() < height)
    height = second->Height();
  if (!first->Lock(GxLockFlags::GetSetPixel))
    return -1;
  if (!second->Lock(GxLockFlags::GetSetPixel)) {
    first->Unlock(GxLockFlags::GetSetPixel);
    return -1;
  }
  for (u32 y = 0; y < height; ++y)
    for (u32 x = 0; x < width; ++x) {
      u16 pixel = (u16)first->GetZValue((i32)x, (i32)y);
      if (pixel != (u16)second->GetZValue((i32)x, (i32)y))
        ++differences;
    }
  first->UnlockDepthSurface(GxLockFlags::GetSetPixel);
  second->UnlockDepthSurface(GxLockFlags::GetSetPixel);
  return (i32)differences;
}

u32 GxGeneralBuffer::CompareZToZBuffer(GxBuffer *other) {
  return GxCountSecondaryDifferences(this, other);
}

// Resolve the live returned format before applying a nonnull palette to an indexed primary driver.
// The driver's raw byte is returned directly; other formats or an absent palette return zero.
u8 GxGeneralBuffer::PaletteChangedNotification() {
  i32 code = static_cast<i32>(GetColorType().color);
  if ((code == 3 || code == 2) && palette)
    return backend->SetPalette(palette);
  return 0;
}

// Read the selected width through the locked plane, retaining DWORD address wrap and WORD row rounding.
u32 GxGeneralBuffer::GetPixel(u32 x, u32 y) const {
  assert(x < (u32)area.right && y < (u32)area.bottom);
  u32 row = y * backend->pitch;
  if (format[1] == 32) {
    u32 pixel, offset = row + 4u * x;
    memcpy(&pixel, GxPixelAddress(backend->data, offset), 4);
    return pixel;
  }
  if (format[1] != 16)
    return *GxPixelAddress(backend->data, row + x);
  u16 pixel;
  u32 offset = 2u * (x + (row >> 1));
  memcpy(&pixel, GxPixelAddress(backend->data, offset), 2);
  return pixel;
}

// Store the low WORD at sixteen bits and the low BYTE otherwise, including the thirty-two-bit byte path.
// The original interface takes three unsigned DWORDs and returns void; preserve wrapped displacement.
void GxGeneralBuffer::SetPixel(u32 x, u32 y, u32 pixel) {
  u32 row = y * backend->pitch;
  if (format[1] == 16) {
    u32 offset = 2u * (x + (row >> 1));
    u16 word = (u16)pixel;
    memcpy(GxPixelAddress(backend->data, offset), &word, sizeof(word));
  } else
    *GxPixelAddress(backend->data, row + x) = (u8)pixel;
}

void GxGeneralBuffer::SetPixelAlpha(u32 x, u32 y, u32 pixel, f32 opacity) {
  u32 row = y * backend->pitch, offset = 2u * (x + (row >> 1));
  GxApplyColor(reinterpret_cast<u16 *>(GxPixelAddress(backend->data, offset)), (u16)pixel, opacity);
}

// Read the depth plane only at sixteen bits, retaining wrapped primary-pitch addressing for both planes.
// Other depths read the color plane; the DWORD branch rounds each row displacement down to a DWORD boundary.
u32 GxGeneralBuffer::GetZValue(u32 x, u32 y) const {
  u32 row = y * backend->pitch;
  if (format[1] == 16) {
    u16 pixel;
    u32 offset = 2u * (x + (row >> 1));
    memcpy(&pixel, GxPixelAddress(secondary_backend->data, offset), 2);
    return pixel;
  }
  if (format[1] == 32) {
    u32 pixel, offset = 4u * (x + (row >> 2));
    memcpy(&pixel, GxPixelAddress(backend->data, offset), 4);
    return pixel;
  }
  return *GxPixelAddress(backend->data, row + x);
}

// Store through the depth plane at sixteen bits and the color plane at other depths, preserving wrapped addressing.
void GxGeneralBuffer::SetZValue(u32 x, u32 y, u32 pixel) {
  u32 row = y * backend->pitch;
  if (format[1] == 16) {
    u16 word = (u16)pixel;
    u32 offset = 2u * (x + (row >> 1));
    memcpy(GxPixelAddress(secondary_backend->data, offset), &word, 2);
  } else if (format[1] == 32) {
    u32 offset = 4u * (x + (row >> 2));
    memcpy(GxPixelAddress(backend->data, offset), &pixel, 4);
  } else
    *GxPixelAddress(backend->data, row + x) = (u8)pixel;
}

// Return the actual mutable platform pixel allocation.
void *GxGeneralBuffer::GetColorSurfaceBits() {
  return backend->data;
}

u32 GxGeneralBuffer::GetColorSurfaceStride() {
  return backend->pitch;
}

// Borrow the current secondary driver's data pointer without locking, retaining or consulting the primary plane.
void *GxGeneralBuffer::GetDepthSurfaceBits() {
  return secondary_backend->data;
}

u32 GxGeneralBuffer::GetDepthSurfaceStride() {
  return secondary_backend->pitch;
}

// Exchange the actual borrowed palette pointer and return its previous value, matching buffer88.
GxPaletteView *GxGeneralBuffer::ReplacePalette(GxPaletteView *colors) {
  GxPaletteView *previous = palette;
  palette = colors;
  return previous;
}

// Test only source-supported unequal WORD format pairs; unsupported pairs never read the supplied pixel.
static bool GxConvertSupported(GxColorType::Color old, GxColorType::Color target) {
  using Color = GxColorType::Color;
  if (old == target)
    return 1;
  switch (old) {
  case Color::RGB555:
    return target >= Color::RGB655 && target <= Color::ARGB1555;
  case Color::RGB655:
    return target == Color::RGB555 || target == Color::RGB565 || target == Color::RGB556;
  case Color::RGB565:
    return target == Color::RGB555 || target == Color::RGB655 || (target >= Color::RGB556 && target <= Color::ARGB1555);
  case Color::RGB556:
    return target >= Color::RGB555 && target <= Color::RGB565;
  case Color::RGBA5551:
    return target == Color::RGB555 || target == Color::RGB565 || target == Color::ARGB1555;
  case Color::ARGB1555:
    return target == Color::RGB555 || target == Color::RGB565 || target == Color::RGBA5551;
  default:
    return 0;
  }
}

// Apply the actual source WORD arithmetic for a supported conversion pair.
static u16 GxConvertValue(u16 p, GxColorType::Color old, GxColorType::Color target) {
  using Color = GxColorType::Color;
  u32 q = p;
  switch (old) {
  case Color::RGB555:
    if (target == Color::RGB655)
      q += q & 0xfc00u;
    else if (target == Color::RGB565)
      q += q & 0xffe0u;
    else if (target == Color::RGB556 || target == Color::RGBA5551)
      q *= 2;
    else if (target == Color::ARGB1555)
      q |= 0x8000u;
    break;
  case Color::RGB655:
    if (target == Color::RGB555)
      q = (q & 0x3ffu) | ((q >> 1) & 0x7c00u);
    else if (target == Color::RGB565) {
      q = (q & 0x3ffu) | ((q >> 1) & 0x7c00u);
      q += q & 0xffe0u;
    } else if (target == Color::RGB556)
      q = (q & 0xf800u) | ((q & 0x3ffu) * 2);
    break;
  case Color::RGB565:
    if (target == Color::RGB555)
      q = (q & 31u) | ((q >> 1) & 0x7fe0u);
    else if (target == Color::RGB655) {
      q = (q & 31u) | ((q >> 1) & 0x7fe0u);
      q += q & 0xfc00u;
    } else if (target == Color::RGB556 || target == Color::RGBA5551)
      q = (q & 0xffc0u) | ((q & 31u) * 2);
    else if (target == Color::ARGB1555)
      q = (q & 31u) | ((q >> 1) & 0x7fe0u) | 0x8000u;
    break;
  case Color::RGB556:
    q >>= 1;
    if (target == Color::RGB655)
      q += q & 0x7c00u;
    else if (target == Color::RGB565)
      q += q & 0x7fe0u;
    break;
  case Color::RGBA5551:
    if (target == Color::RGB565)
      q = (q & 0xffc0u) | ((q >> 1) & 31u);
    else
      q >>= 1;
    break;
  case Color::ARGB1555:
    if (target == Color::RGB555)
      q |= 0x8000u;
    else if (target == Color::RGB565)
      q += q & 0xffe0u;
    else if (target == Color::RGBA5551)
      q = (q << 1) | (q >> 15);
    break;
  default:
    break;
  }
  return (u16)q;
}

// Convert native WORDs in place using the source conversion arithmetic, including its second scanline pass.
// The borrowed surface storage must cover every pixel actually reached by the selected conversion.
u8 GxGeneralBuffer::Convert16BitBufferFromFormatToFormat(
    GxColorType::Color source_value, GxColorType::Color target_value) {
  using Color = GxColorType::Color;
  Color source = source_value, target = target_value;
  if (target == Color::Native)
    target = static_cast<Color>(format[0]);
  if (source == target)
    return 1;
  u16 key = (u16)color.pixel;
  format[0] = (i32)target;
  Convert16BitValueFromFormatToFormat(&key, source, target);
  color.pixel = key;
  if (!Lock(GxLockFlags::Bits))
    return 0;
  u8 result = 0;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u32 width = (u32)area.right, height = (u32)area.bottom, stride = (GetColorSurfaceStride() >> 1) * 2;
  if (source >= Color::RGB555 && source <= Color::RGB556 && GxConvertSupported(source, target)) {
    result = 1;
    assert(format[1] == 16);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        if (source == Color::RGB555 && target == Color::ARGB1555)
          pixels[2 * (usize)x + 1] |= 128;
        else {
          u16 pixel;
          memcpy(&pixel, pixels + 2 * (usize)x, 2);
          pixel = GxConvertValue(pixel, source, target);
          memcpy(pixels + 2 * (usize)x, &pixel, 2);
        }
      }
      pixels = reinterpret_cast<u8 *>(reinterpret_cast<uptr>(pixels) + stride);
    }
    if ((source == Color::RGB555 || source == Color::RGB565) && target == Color::RGBA5551) {
      for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
          if (source == Color::RGB555) {
            pixels[2 * (usize)x + 1] |= 128;
          } else {
            u16 pixel;
            memcpy(&pixel, pixels + 2 * (usize)x, 2);
            pixel = GxConvertValue(pixel, Color::RGB565, Color::ARGB1555);
            memcpy(pixels + 2 * (usize)x, &pixel, 2);
          }
        }
        pixels = reinterpret_cast<u8 *>(reinterpret_cast<uptr>(pixels) + stride);
      }
    }
  }
  Unlock(GxLockFlags::Bits);
  return result;
}

// Install the actual key value and enable keying for subsequent copies.
void GxGeneralBuffer::SetTransparency(u32 pixel) {
  color.pixel = pixel;
  color.flags |= 2;
  if (color.flags & 16) {
    color.secondary = 0;
    color.flags |= 512;
  }
  SetBltFlags(color);
}

// Return the key only when its source flag is enabled.
u8 GxGeneralBuffer::GetTransparentColor(u32 *out) const {
  if (!(color.flags & 2))
    return 0;
  *out = color.pixel;
  return 1;
}

// Disable the actual key flag without altering the stored color.
void GxGeneralBuffer::RemoveTransparency() {
  color.flags &= ~2u;
  if (color.flags & 16) {
    color.secondary = 0;
    color.flags |= 512;
  }
  SetBltFlags(color);
}

u8 GxGeneralBuffer::GetTransparency() const {
  return (color.flags & 2) != 0;
}

// Compare an addressed source pixel with the current key.
u8 GxGeneralBuffer::IsPixelTransparent(u32 x, u32 y) {
  if (x >= (u32)area.right || y >= (u32)area.bottom)
    return false;
  u32 pixel = GetPixel((i32)x, (i32)y);
  bool result = (color.flags & 2) && pixel == color.pixel;
  if (format[1] == 32 && !result)
    return !(pixel & kGxArgbAlphaMask);
  return result;
}

// Clip local one-to-one rectangles through the real virtual receiver, then preserve the driver's raw fill byte.
u8 GxGeneralBuffer::Fill(u32 pixel, const ScRect &box, ScRect *clip) {
  ScRect destination = box;
  ScRect source = {0, 0, (i32)((u32)box.right - (u32)box.left), (i32)((u32)box.bottom - (u32)box.top)};
  if (!GenericBufferClip(this, &source, &destination, clip))
    return 0;
  return backend->FillRect(pixel, &destination);
}

u8 GxGeneralBuffer::Fill(u32 pixel, i32 left, i32 top, i32 right, i32 bottom, ScRect *clip) {
  ScRect rectangle = {left, top, right, bottom};
  return Fill(pixel, rectangle, clip);
}

u8 GxGeneralBuffer::FillNoClip(u32 pixel, i32 left, i32 top, i32 right, i32 bottom) {
  ScRect rectangle = {left, top, right, bottom};
  return FillNoClip(pixel, rectangle);
}

u8 GxGeneralBuffer::FillNoClip(u32 pixel, const ScRect &rectangle) {
  return backend->FillRect(pixel, const_cast<ScRect *>(&rectangle));
}

u8 GxGeneralBuffer::ZFill(u32 pixel, const ScRect &rectangle) {
  return secondary_backend->FillRect(pixel, const_cast<ScRect *>(&rectangle));
}

u8 GxGeneralBuffer::FillNoClip(u32 pixel) {
  return FillNoClip(pixel, *GetBufferArea());
}

u8 GxGeneralBuffer::ZFill(u32 pixel) {
  return ZFill(pixel, *GetBufferArea());
}

// Clip one-to-one rectangles in place, querying the explicit receiver's live bounds separately for each axis.
// Earlier horizontal writes remain visible to the second bounds callback and to genuine aliased rectangle inputs.
u8 GxGeneralBuffer::GenericBufferClip(GxBuffer *receiver, ScRect *source, ScRect *destination, const ScRect *clip) {
  u32 edge = (u32)destination->right;
  const ScRect *bounds = receiver->GetBufferArea();
  u32 delta = edge - (u32)bounds->right;
  if (GxSoftwarePositive(delta)) {
    destination->right = bounds->right;
    source->right = (i32)((u32)source->right - delta);
    if (!GxSoftwareLess((u32)destination->left, (u32)destination->right))
      return false;
  }
  delta = 0u - (u32)destination->left;
  if (!(delta & kScIntegerSignBit) && destination->left) {
    destination->left = 0;
    source->left = (i32)((u32)source->left + delta);
    if (!GxSoftwareLess((u32)destination->left, (u32)destination->right))
      return false;
  }
  edge = (u32)destination->bottom;
  bounds = receiver->GetBufferArea();
  delta = edge - (u32)bounds->bottom;
  if (GxSoftwarePositive(delta)) {
    destination->bottom = bounds->bottom;
    source->bottom = (i32)((u32)source->bottom - delta);
    if (!GxSoftwareLess((u32)destination->top, (u32)destination->bottom))
      return false;
  }
  delta = 0u - (u32)destination->top;
  if (!(delta & kScIntegerSignBit) && destination->top) {
    destination->top = 0;
    source->top = (i32)((u32)source->top + delta);
    if (!GxSoftwareLess((u32)destination->top, (u32)destination->bottom))
      return false;
  }
  if (clip) {
    delta = (u32)destination->right - (u32)clip->right;
    if (GxSoftwarePositive(delta)) {
      destination->right = (i32)((u32)destination->right - delta);
      source->right = (i32)((u32)source->right - delta);
    }
    delta = (u32)clip->left - (u32)destination->left;
    if (GxSoftwarePositive(delta)) {
      destination->left = clip->left;
      source->left = (i32)((u32)source->left + delta);
    }
    delta = (u32)destination->bottom - (u32)clip->bottom;
    if (GxSoftwarePositive(delta)) {
      destination->bottom = (i32)((u32)destination->bottom - delta);
      source->bottom = (i32)((u32)source->bottom - delta);
    }
    delta = (u32)clip->top - (u32)destination->top;
    if (GxSoftwarePositive(delta)) {
      destination->top = clip->top;
      source->top = (i32)((u32)source->top + delta);
    }
  }
  return GxSoftwareLess((u32)destination->left, (u32)destination->right) &&
      GxSoftwareLess((u32)destination->top, (u32)destination->bottom);
}

// Round the scaled leading-edge adjustment with the original double-bias operation, retaining its tie behavior.
static u32 GxScaledOffset(i32 delta, f32 scale) {
  f64 biased = (f64)delta * scale + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &biased, sizeof(bits));
  return (u32)bits + kScDoubleIntegerLowWordBias;
}

// Clip destination edges before mapping the remaining span back to source coordinates and optional float scale outputs.
// A clipped one-pixel half-scale span is rejected; source mirroring applies only after an actual clipping change.
u8 GxGeneralBuffer::GenericBufferClipStretch(GxBuffer *receiver, ScRect *source, ScRect *destination,
    const ScRect *clip, f32 *horizontal_scale, f32 *vertical_scale) {
  i32 left = destination->left, top = destination->top;
  f32 sx = (f32)((f64)(i32)((u32)source->right - (u32)source->left) /
      (i32)((u32)destination->right - (u32)destination->left));
  f32 sy = (f32)((f64)(i32)((u32)source->bottom - (u32)source->top) /
      (i32)((u32)destination->bottom - (u32)destination->top));
  bool changed = false;
  u32 edge = (u32)destination->right;
  const ScRect *bounds = receiver->GetBufferArea();
  u32 delta = edge - (u32)bounds->right;
  if (GxSoftwarePositive(delta)) {
    destination->right = (i32)((u32)destination->right - delta);
    if (!GxSoftwareLess((u32)destination->left, (u32)destination->right))
      return false;
    changed = true;
  }
  delta = 0u - (u32)destination->left;
  if (!(delta & kScIntegerSignBit) && destination->left) {
    destination->left = 0;
    if (!GxSoftwareLess((u32)destination->left, (u32)destination->right))
      return false;
    changed = true;
  }
  edge = (u32)destination->bottom;
  bounds = receiver->GetBufferArea();
  delta = edge - (u32)bounds->bottom;
  if (GxSoftwarePositive(delta)) {
    destination->bottom = (i32)((u32)destination->bottom - delta);
    if (!GxSoftwareLess((u32)destination->top, (u32)destination->bottom))
      return false;
    changed = true;
  }
  delta = 0u - (u32)destination->top;
  if (!(delta & kScIntegerSignBit) && destination->top) {
    destination->top = 0;
    if (!GxSoftwareLess((u32)destination->top, (u32)destination->bottom))
      return false;
    changed = true;
  }
  if (clip) {
    delta = (u32)destination->right - (u32)clip->right;
    if (GxSoftwarePositive(delta)) {
      destination->right = (i32)((u32)destination->right - delta);
      changed = true;
    }
    if (GxSoftwarePositive((u32)clip->left - (u32)destination->left)) {
      destination->left = clip->left;
      changed = true;
    }
    delta = (u32)destination->bottom - (u32)clip->bottom;
    if (GxSoftwarePositive(delta)) {
      destination->bottom = (i32)((u32)destination->bottom - delta);
      changed = true;
    }
    if (GxSoftwarePositive((u32)clip->top - (u32)destination->top)) {
      destination->top = clip->top;
      changed = true;
    }
  }
  if (!GxSoftwareLess((u32)destination->left, (u32)destination->right) ||
      !GxSoftwareLess((u32)destination->top, (u32)destination->bottom))
    return false;
  if (changed) {
    if ((sx == 0.5f && (u32)destination->right - (u32)destination->left == 1) ||
        (sy == 0.5f && (u32)destination->bottom - (u32)destination->top == 1))
      return false;
    if (destination->left != left)
      source->left = (i32)((u32)source->left + GxScaledOffset((i32)((u32)destination->left - (u32)left), sx));
    if (destination->top != top)
      source->top = (i32)((u32)source->top + GxScaledOffset((i32)((u32)destination->top - (u32)top), sy));
    source->right = (i32)(u32)(i64)((f64)(i32)((u32)destination->right - (u32)destination->left) * sx + source->left);
    source->bottom = (i32)(u32)(i64)((f64)(i32)((u32)destination->bottom - (u32)destination->top) * sy + source->top);
    if (source->left == source->right) {
      if (source->left)
        source->left = (i32)((u32)source->left - 1);
      else
        source->right = (i32)((u32)source->right + 1);
    }
    if (source->top == source->bottom) {
      if (source->top)
        source->top = (i32)((u32)source->top - 1);
      else
        source->bottom = (i32)((u32)source->bottom + 1);
    }
    if (color.flags & 4) {
      u32 width = Width(), first = width - (u32)source->right;
      source->right = (i32)(width - (u32)source->left);
      source->left = (i32)first;
    }
    if (color.flags & 8) {
      u32 height = Height(), first = height - (u32)source->bottom;
      source->bottom = (i32)(height - (u32)source->top);
      source->top = (i32)first;
    }
  }
  if (horizontal_scale)
    *horizontal_scale = sx;
  if (vertical_scale)
    *vertical_scale = sy;
  return true;
}

u8 GxGeneralBuffer::AlphaFill(u32 pixel, const ScRect &rectangle_input, f32 opacity, const ScRect *clip) {
  const ScRect *rectangle = &rectangle_input;
  ScRect source = {
      0, 0, (i32)((u32)rectangle->right - (u32)rectangle->left), (i32)((u32)rectangle->bottom - (u32)rectangle->top)};
  ScRect destination = *rectangle;
  if (!GenericBufferClip(this, &source, &destination, clip))
    return 0;
  return AlphaFillNoClip(pixel, destination, opacity);
}

// Recover the low byte of the original biased double after its integer-to-BYTE channel expansion and scaling.
static u8 GxFillAlphaChannel(u32 value, u32 maximum, f32 coefficient) {
  u8 expanded = (u8)(255u * value / maximum);
  f64 rounded = (f64)expanded * (f64)coefficient + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &rounded, sizeof(bits));
  return (u8)bits;
}

// Blend the source and destination channel bytes separately using the original format-specific extraction quirks.
static u16 GxFillAlphaPixel(u16 destination, u16 source, i32 format, f32 opacity, f32 inverse) {
  u32 red_shift = format == 5 ? 10u : 11u, green_shift = format == 5 || format == 6 || format == 7 ? 5u : 6u;
  u32 green_mask = format == 7 ? 63u : 31u, blue_mask = format == 5 || format == 6 || format == 7 ? 31u : 63u;
  u32 red_max = format == 6 ? 63u : 31u;
  u32 red = GxFillAlphaChannel(destination >> red_shift, red_max, inverse) +
      GxFillAlphaChannel(source >> red_shift, red_max, opacity);
  u32 green = GxFillAlphaChannel((destination >> green_shift) & green_mask, green_mask, inverse) +
      GxFillAlphaChannel((source >> green_shift) & green_mask, green_mask, opacity);
  u8 blue = (u8)(GxFillAlphaChannel(destination & blue_mask, blue_mask, inverse) +
      GxFillAlphaChannel(source & blue_mask, blue_mask, opacity));
  if (format == 5)
    return (u16)((blue >> 3) | ((green & 0xf8u) << 2) | ((red & 0xf8u) << 7));
  if (format == 7)
    return (u16)((blue >> 3) | ((green & 0xfcu) << 3) | ((red & 0xf8u) << 8));
  if (format == 6)
    return (u16)((blue >> 3) | ((green & 0xf8u) << 3) | ((red & 0x7cu) << 9));
  return (u16)((blue >> 2) | ((green & 0xf8u) << 3) | ((red & 0xf8u) << 8));
}

// Lock64, blend supported WORD rows in place, and unlock after any successful lock, including non-WORD buffers.
// The source returns zero even after writing pixels; preserve that result rather than manufacturing fill success.
u8 GxGeneralBuffer::AlphaFillNoClip(u32 pixel, const ScRect &rectangle_input, f32 opacity) {
  const ScRect *rectangle = &rectangle_input;
  f32 inverse = (f32)(1.0 - (f64)opacity);
  if (Lock(GxLockFlags::Bits)) {
    if (format[1] == 16) {
      u32 width = (u32)rectangle->right - (u32)rectangle->left;
      u32 height = (u32)rectangle->bottom - (u32)rectangle->top;
      u32 pitch = GetColorSurfaceStride();
      u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
      u32 offset = pitch * (u32)rectangle->top + 2 * (u32)rectangle->left;
      i32 code = format[0];
      for (u32 y = 0; y < height; ++y) {
        u8 *row = pixels + (usize)offset + (usize)y * pitch;
        for (u32 x = 0; x < width; ++x) {
          u16 old;
          memcpy(&old, row + 2 * x, 2);
          u16 value = GxFillAlphaPixel(old, (u16)pixel, code, opacity, inverse);
          memcpy(row + 2 * x, &value, 2);
        }
      }
    }
    Unlock(GxLockFlags::Bits);
  }
  return 0;
}

static u32 GxTileQuotient(u32 value, u32 divisor) {
  return (u32)((i64)(i32)value / (i32)divisor);
}

// Clip the tile grid to captured buffer/destination bounds, then submit directly to the live physical drivers.
// Source rectangles and the optional clip remain live between submissions; physical return bytes are ignored.
u8 GxGeneralBuffer::BltTiled(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
    i32 x_offset, i32 y_offset, const ScRect *clip) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  u32 left = (u32)destination->left, top = (u32)destination->top;
  u32 right = (u32)destination->right, bottom = (u32)destination->bottom;
  u32 width = (u32)source_box->right - (u32)source_box->left;
  u32 height = (u32)source_box->bottom - (u32)source_box->top;
  u32 ox = (u32)((i64)x_offset % (i32)width), oy = (u32)((i64)y_offset % (i32)height);
  if (GxSoftwarePositive(ox))
    ox -= width;
  if (GxSoftwarePositive(oy))
    oy -= height;
  if (left & kScIntegerSignBit) {
    ox += left - width * GxTileQuotient(left + ox, width);
    left = 0;
  }
  if (top & kScIntegerSignBit) {
    oy += top - height * GxTileQuotient(top + oy, height);
    top = 0;
  }
  if (GxSoftwareLess((u32)area.right, right))
    right = (u32)area.right;
  if (GxSoftwareLess((u32)area.bottom, bottom))
    bottom = (u32)area.bottom;
  if (clip) {
    if (GxSoftwarePositive((u32)clip->left - left)) {
      ox += left + width * GxTileQuotient((u32)clip->left - left - ox, width);
      left = (u32)clip->left;
    }
    if (GxSoftwarePositive((u32)clip->top - top)) {
      oy += top + height * GxTileQuotient((u32)clip->top - top - oy, height);
      top = (u32)clip->top;
    }
    if (GxSoftwarePositive(right - (u32)clip->right))
      right = (u32)clip->right;
    if (GxSoftwarePositive(bottom - (u32)clip->bottom))
      bottom = (u32)clip->bottom;
  }
  if (!GxSoftwareLess(top, bottom) || !GxSoftwareLess(left, right))
    return 1;
  u32 first_x = left + ox, x = first_x, y = top + oy, next_x = x + width, next_y = y + height;
  for (;;) {
    ScRect sr = *source_box, dr = {(i32)x, (i32)y, (i32)next_x, (i32)next_y};
    if (GxSoftwareLess(x, left)) {
      dr.left = (i32)left;
      sr.left = (i32)((u32)sr.left + left - x);
    }
    if (GxSoftwareLess(right, (u32)dr.right)) {
      sr.right = (i32)((u32)sr.right + right - (u32)dr.right);
      dr.right = (i32)right;
    }
    if (GxSoftwareLess((u32)dr.top, top)) {
      sr.top = (i32)((u32)sr.top + top - (u32)dr.top);
      dr.top = (i32)top;
    }
    if (GxSoftwareLess(bottom, (u32)dr.bottom)) {
      sr.bottom = (i32)((u32)sr.bottom + bottom - (u32)dr.bottom);
      dr.bottom = (i32)bottom;
    }
    if (clip) {
      u32 delta = (u32)dr.right - (u32)clip->right;
      if (GxSoftwarePositive(delta)) {
        dr.right = clip->right;
        sr.right = (i32)((u32)sr.right - delta);
      }
      delta = (u32)clip->left - (u32)dr.left;
      if (GxSoftwarePositive(delta)) {
        dr.left = clip->left;
        sr.left = (i32)((u32)sr.left + delta);
      }
      delta = (u32)dr.bottom - (u32)clip->bottom;
      if (GxSoftwarePositive(delta)) {
        dr.bottom = clip->bottom;
        sr.bottom = (i32)((u32)sr.bottom - delta);
      }
      delta = (u32)clip->top - (u32)dr.top;
      if (GxSoftwarePositive(delta)) {
        dr.top = clip->top;
        sr.top = (i32)((u32)sr.top + delta);
      }
    }
    backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
    next_x += width;
    x += width;
    if (!GxSoftwareLess(x, right)) {
      y = next_y;
      if (!GxSoftwareLess(next_y, bottom))
        return 1;
      next_y += height;
      x = first_x;
      next_x = first_x + width;
    }
  }
}

// Submit the offset tile grid directly, clipping only to the caller's live destination rectangle.
// The first submission is unconditional, even for an empty destination, and each driver result is ignored.
u8 GxGeneralBuffer::BltTiledNoClip(
    GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, i32 x_offset, i32 y_offset) {
  const ScRect *source_box = &source_box_input;
  const ScRect *destination = &destination_input;
  u32 width = (u32)source_box->right - (u32)source_box->left;
  u32 height = (u32)source_box->bottom - (u32)source_box->top;
  u32 ox = (u32)((i64)x_offset % (i32)width), oy = (u32)((i64)y_offset % (i32)height);
  if (GxSoftwarePositive(ox))
    ox -= width;
  if (GxSoftwarePositive(oy))
    oy -= height;
  u32 x = (u32)destination->left + ox, y = (u32)destination->top + oy;
  u32 next_x = x + width, next_y = y + height, right = (u32)destination->right;
  for (;;) {
    ScRect sr = *source_box, dr = {(i32)x, (i32)y, (i32)next_x, (i32)next_y};
    u32 left = (u32)destination->left;
    if (GxSoftwareLess(x, left)) {
      dr.left = (i32)left;
      sr.left = (i32)((u32)sr.left + left - x);
    }
    if (GxSoftwareLess(right, (u32)dr.right)) {
      sr.right = (i32)((u32)sr.right + right - (u32)dr.right);
      dr.right = (i32)right;
    }
    u32 top = (u32)destination->top;
    if (GxSoftwareLess((u32)dr.top, top)) {
      sr.top = (i32)((u32)sr.top + top - (u32)dr.top);
      dr.top = (i32)top;
    }
    u32 bottom = (u32)destination->bottom;
    if (GxSoftwareLess(bottom, (u32)dr.bottom)) {
      sr.bottom = (i32)((u32)sr.bottom + bottom - (u32)dr.bottom);
      dr.bottom = (i32)bottom;
    }
    backend->Blit(static_cast<GxGeneralBuffer *>(source)->backend, &sr, &dr);
    right = (u32)destination->right;
    x += width;
    next_x += width;
    if (!GxSoftwareLess(x, right)) {
      y = next_y;
      if (!GxSoftwareLess(next_y, (u32)destination->bottom))
        return 1;
      next_y += height;
      x = (u32)destination->left + ox;
      next_x = x + width;
    }
  }
}

// Encode the four clipping sides with the original top,bottom,left,right bit order.
static u32 GxLineCode(i32 x, i32 y, i32 left, i32 top, i32 right, i32 bottom) {
  u32 code = x < left ? 4 : 0;
  if (x > right)
    code += 8;
  if (y < top)
    ++code;
  if (y > bottom)
    code += 2;
  return code;
}

// Preserve the source wrapped product before signed division, excluding only the original unavailable CPU trap cases.
static i32 GxLineQuotient(u32 first, u32 second, u32 divisor) {
  i32 product = (i32)(first * second), denominator = (i32)divisor;
  assert(denominator && !(product == (-INT32_MAX - 1) && denominator == -1));
  return product / denominator;
}

// Clip both genuine endpoint outputs using inclusive bounds and the source's wrapped integer intersections.
static bool GxClipLine(i32 *x1, i32 *y1, i32 *x2, i32 *y2, i32 left, i32 top, i32 right, i32 bottom) {
  u32 first = GxLineCode(*x1, *y1, left, top, right, bottom);
  u32 second = GxLineCode(*x2, *y2, left, top, right, bottom);
  while (first || second) {
    if (first & second)
      return false;
    u32 outside = first ? first : second;
    i32 x, y;
    if (outside & 1) {
      y = top;
      x = (i32)((u32)*x1 + (u32)GxLineQuotient((u32)*x2 - (u32)*x1, (u32)top - (u32)*y1, (u32)*y2 - (u32)*y1));
    } else if (outside & 2) {
      y = bottom;
      x = (i32)((u32)*x1 + (u32)GxLineQuotient((u32)*x2 - (u32)*x1, (u32)bottom - (u32)*y1, (u32)*y2 - (u32)*y1));
    } else if (outside & 8) {
      x = right;
      y = (i32)((u32)*y1 + (u32)GxLineQuotient((u32)*y2 - (u32)*y1, (u32)right - (u32)*x1, (u32)*x2 - (u32)*x1));
    } else {
      x = left;
      y = (i32)((u32)*y1 + (u32)GxLineQuotient((u32)*y2 - (u32)*y1, (u32)left - (u32)*x1, (u32)*x2 - (u32)*x1));
    }
    if (outside == first) {
      *x1 = x;
      *y1 = y;
      first = GxLineCode(*x1, *y1, left, top, right, bottom);
    } else {
      *x2 = x;
      *y2 = y;
      second = GxLineCode(*x2, *y2, left, top, right, bottom);
    }
  }
  return true;
}

static void GxLineSpan(u8 *pixels, i64 offset, u32 count, u16 pixel) {
  for (u32 index = 0; index < count; ++index)
    memcpy(pixels + 2 * (offset + index), &pixel, sizeof(pixel));
}

// Draw directly into the captured WORD plane after inclusive endpoint clipping, retaining the source's thick-stroke
// anchors.
u8 GxGeneralBuffer::DrawLine(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness) {
  u16 pixel = static_cast<u16>(pixel_value);
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u32 stride = (u16)(GetColorSurfaceStride() >> 1);
  i32 width = area.right, height = area.bottom;
  if (!GxClipLine(&x1, &y1, &x2, &y2, 0, 0, (i32)((u32)width - 1), (i32)((u32)height - 1)))
    return 1;
  if (y1 == y2) {
    i32 left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
    i32 count = (i16)((u32)right - (u32)left + 1);
    if (count > 0)
      for (u32 row = 0; row < thickness; ++row)
        GxLineSpan(pixels, left + ((i64)y1 + row) * stride, (u32)count, pixel);
    return 1;
  }
  if (x1 == x2) {
    i32 top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
    for (i64 row = top; row <= bottom; ++row)
      GxLineSpan(pixels, x1 + row * stride, thickness, pixel);
    return 1;
  }
  i32 dx = x2 > x1 ? x2 - x1 : x1 - x2, dy = y2 > y1 ? y2 - y1 : y1 - y2;
  i32 sx = x1 <= x2 ? 1 : -1, sy = y1 <= y2 ? 1 : -1;
  i32 major = dx < dy ? dy : dx, minor = dx < dy ? dx : dy;
  i32 x_step = dx < dy ? 0 : sx, y_step = dx < dy ? sy : 0;
  i32 error = (i32)(2u * (u32)minor - (u32)major), straight = (i32)(2u * (u32)minor);
  i32 diagonal = (i32)(2u * ((u32)minor - (u32)major));
  bool interior = true;
  i32 minimum = (i32)(thickness - 1), maximum_x = (i32)((u32)width - thickness);
  i32 maximum_y = (i32)((u32)height - thickness);
  u32 remaining = (u32)major + 1;
  if (thickness == 1 && (i32)remaining < 1)
    return 1;
  for (; remaining; --remaining) {
    if (thickness == 1)
      GxLineSpan(pixels, x1 + (i64)y1 * stride, 1, pixel);
    else {
      if (interior && (x1 < minimum || x1 > maximum_x || y1 < minimum || y1 > maximum_y))
        interior = false;
      i64 left = sx == -1 ? (i64)x1 - thickness + 1 : x1;
      if (!interior && sx != -1 && left > width - 1)
        left = width - 1;
      for (u32 row = 0; row < (u16)thickness; ++row) {
        i64 offset = left + ((i64)y1 + (i64)row * sy) * stride;
        if (interior || (offset >= 0 && offset < (i64)height * stride)) {
          u32 count = thickness;
          if (!interior) {
            if (sx == -1 && x1 < minimum)
              count = (u32)x1 + 1;
            else if (sx != -1 && x1 > maximum_x)
              count = (u32)width - (u32)x1;
          }
          GxLineSpan(pixels, offset, count, pixel);
        }
      }
    }
    if (error >= 0) {
      error = (i32)((u32)error + (u32)diagonal);
      x1 = (i32)((u32)x1 + (u32)sx);
      y1 = (i32)((u32)y1 + (u32)sy);
    } else {
      error = (i32)((u32)error + (u32)straight);
      x1 = (i32)((u32)x1 + (u32)x_step);
      y1 = (i32)((u32)y1 + (u32)y_step);
    }
  }
  return 1;
}

// Draw directly into the captured WORD plane without reading logical bounds or clipping endpoints.
// Retain the source's signed-WORD horizontal count, low-WORD pitch and oriented thick diagonal squares.
u8 GxGeneralBuffer::DrawLineNoClip(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness) {
  u32 pixel = static_cast<u32>(pixel_value);
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u32 stride = (u16)(GetColorSurfaceStride() >> 1);
  u16 word = (u16)pixel;
  if (y1 == y2) {
    i32 left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
    i32 count = (i16)((u32)right - (u32)left + 1);
    if (count > 0)
      for (u32 row = 0; row < thickness; ++row)
        GxLineSpan(pixels, left + ((i64)y1 + row) * stride, (u32)count, word);
    return 1;
  }
  if (x1 == x2) {
    i32 top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
    u32 count = (u32)bottom - (u32)top + 1;
    for (u32 row = 0; row < count; ++row)
      GxLineSpan(pixels, x1 + ((i64)top + row) * stride, thickness, word);
    return 1;
  }
  i32 dx = (i32)((u32)x2 - (u32)x1), dy = (i32)((u32)y2 - (u32)y1);
  if (dx < 0)
    dx = (i32)(0u - (u32)dx);
  if (dy < 0)
    dy = (i32)(0u - (u32)dy);
  i32 sx = x1 <= x2 ? 1 : -1, sy = y1 <= y2 ? 1 : -1;
  i32 major = dx < dy ? dy : dx, minor = dx < dy ? dx : dy;
  i32 x_step = dx < dy ? 0 : sx, y_step = dx < dy ? sy : 0;
  i32 error = (i32)(2u * (u32)minor - (u32)major), straight = (i32)(2u * (u32)minor);
  i32 diagonal = (i32)(2u * ((u32)minor - (u32)major));
  u32 remaining = (u32)major + 1;
  if ((i32)remaining < 1)
    return 1;
  for (; remaining; --remaining) {
    if (thickness == 1)
      GxLineSpan(pixels, x1 + (i64)y1 * stride, 1, word);
    else {
      i64 left = sx == -1 ? (i64)x1 - thickness + 1 : x1;
      for (u32 row = 0; row < (u16)thickness; ++row)
        GxLineSpan(pixels, left + ((i64)y1 + (i64)row * sy) * stride, thickness, word);
    }
    if (error >= 0) {
      error = (i32)((u32)error + (u32)diagonal);
      x1 = (i32)((u32)x1 + (u32)sx);
      y1 = (i32)((u32)y1 + (u32)sy);
    } else {
      error = (i32)((u32)error + (u32)straight);
      x1 = (i32)((u32)x1 + (u32)x_step);
      y1 = (i32)((u32)y1 + (u32)y_step);
    }
  }
  return 1;
}

// Submit four inclusive outline edges through the original unclipped line entry in top, bottom, right, left order.
// Reload the caller rectangle between calls and retain the final line's raw return byte.
u8 GxGeneralBuffer::DrawRectangleOutlineNoClip(const ScRect &box_input, u32 pixel, u32 thickness) {
  const ScRect *box = &box_input;
  DrawLineNoClip(box->left, box->top, box->right, box->top, static_cast<i32>(pixel), thickness);
  DrawLineNoClip(box->left, (i32)((u32)box->bottom - thickness + 1), box->right,
      (i32)((u32)box->bottom - thickness + 1), static_cast<i32>(pixel), thickness);
  DrawLineNoClip((i32)((u32)box->right - thickness + 1), box->top, (i32)((u32)box->right - thickness + 1), box->bottom,
      static_cast<i32>(pixel), thickness);
  return DrawLineNoClip(box->left, box->top, box->left, box->bottom, static_cast<i32>(pixel), thickness);
}

static void GxLineBlend(u8 *pixels, u32 offset, u16 pixel, f32 opacity) {
  GxApplyColor(reinterpret_cast<u16 *>(pixels + (i32)offset), pixel, opacity);
}

// Apply the source alpha-line raster without clipping: axes retain a fixed span end, and diagonals sort both axes.
// Thick diagonals blend only pixels beyond both previous square edges; each blend reloads the live module callback.
u8 GxGeneralBuffer::DrawLineNoClipAlpha(
    i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness_value, f32 opacity) {
  u32 pixel = static_cast<u32>(pixel_value);
  i32 thickness = static_cast<i32>(thickness_value);
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u32 stride = (u16)(GetColorSurfaceStride() >> 1);
  u16 word = (u16)pixel;
  if (y1 == y2) {
    i32 left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
    i32 count = (i16)((u32)right - (u32)left + 1);
    u32 cursor = 2u * ((u32)left + (u32)y1 * stride), end = cursor + 2u * (u32)count;
    u32 advance = 2u * ((u32)left + stride - (u32)right) - 2;
    for (u32 row = 0; row < (u32)thickness; ++row) {
      while ((i32)cursor < (i32)end) {
        GxLineBlend(pixels, cursor, word, opacity);
        cursor += 2;
      }
      cursor += advance;
    }
    return 1;
  }
  if (x1 == x2) {
    i32 top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
    u32 cursor = 2u * ((u32)x1 + (u32)top * stride), end = cursor + 2u * (u32)thickness;
    u32 advance = 2u * (stride - (u32)thickness), count = (u32)bottom - (u32)top + 1;
    for (u32 row = 0; row < count; ++row) {
      while ((i32)cursor < (i32)end) {
        GxLineBlend(pixels, cursor, word, opacity);
        cursor += 2;
      }
      cursor += advance;
    }
    return 1;
  }
  i32 left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
  i32 top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
  i32 dx = (i32)((u32)right - (u32)left), dy = (i32)((u32)bottom - (u32)top);
  if (dx < 0)
    dx = (i32)(0u - (u32)dx);
  if (dy < 0)
    dy = (i32)(0u - (u32)dy);
  i32 major = dx < dy ? dy : dx, minor = dx < dy ? dx : dy;
  i32 x_step = dx < dy ? 0 : 1, y_step = dx < dy ? 1 : 0;
  i32 error = (i32)(2u * (u32)minor - (u32)major), straight = (i32)(2u * (u32)minor);
  i32 diagonal = (i32)(2u * ((u32)minor - (u32)major));
  u32 remaining = (u32)major + 1, x = (u32)left, y = (u32)top;
  i32 previous_right = (i32)(x - 1), previous_bottom = (i32)(y - 1);
  if ((i32)remaining < 1)
    return 1;
  for (; remaining; --remaining) {
    if (thickness == 1)
      GxLineBlend(pixels, 2u * (x + y * stride), word, opacity);
    else {
      if (thickness > 0)
        for (u32 row = 0; row < (u32)thickness; ++row)
          if ((i32)(y + row) > previous_bottom)
            for (u32 column = 0; column < (u32)thickness; ++column)
              if ((i32)(x + column) > previous_right)
                GxLineBlend(pixels, 2u * (x + column + (y + row) * stride), word, opacity);
      previous_right = (i32)(x + (u32)thickness - 1);
      previous_bottom = (i32)(y + (u32)thickness - 1);
    }
    if (error >= 0) {
      error = (i32)((u32)error + (u32)diagonal);
      ++x;
      ++y;
    } else {
      error = (i32)((u32)error + (u32)straight);
      x += (u32)x_step;
      y += (u32)y_step;
    }
  }
  return 1;
}

// Clip endpoints before blending the original axis or directional diagonal raster into the captured WORD plane.
// Thick strokes switch permanently from interior alpha blending to the source's guarded opaque edge writes.
u8 GxGeneralBuffer::DrawLineAlpha(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness_value, f32 opacity) {
  u32 pixel = static_cast<u32>(pixel_value);
  i32 thickness = static_cast<i32>(thickness_value);
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u32 stride = (u16)(GetColorSurfaceStride() >> 1);
  i32 width = area.right, height = area.bottom;
  if (!GxClipLine(&x1, &y1, &x2, &y2, 0, 0, (i32)((u32)width - 1), (i32)((u32)height - 1)))
    return 1;
  u16 word = (u16)pixel;
  if (y1 == y2) {
    i32 left = x1 < x2 ? x1 : x2, right = x1 < x2 ? x2 : x1;
    i32 span = (i16)((u32)right - (u32)left + 1);
    i32 rows = (u32)y2 + (u32)thickness - 1 > (u32)height ? (i32)((u32)height - (u32)y2) : thickness;
    u32 cursor = 2u * ((u32)left + (u32)y1 * stride), end = cursor + 2u * (u32)span;
    u32 advance = 2u * ((u32)left + stride - (u32)right) - 2;
    for (u32 row = 0; row < (u32)rows; ++row) {
      while ((i32)cursor < (i32)end) {
        GxLineBlend(pixels, cursor, word, opacity);
        cursor += 2;
      }
      cursor += advance;
      end = cursor + 2u * (u32)span;
    }
    return 1;
  }
  if (x1 == x2) {
    i32 top = y1 < y2 ? y1 : y2, bottom = y1 < y2 ? y2 : y1;
    i32 columns = (u32)x2 + (u32)thickness - 1 > (u32)width ? (i32)((u32)width - (u32)x2) : thickness;
    u32 cursor = 2u * ((u32)x1 + (u32)top * stride), end = cursor + 2u * (u32)thickness;
    u32 advance = 2u * (stride - (u32)columns), count = (u32)bottom - (u32)top + 1;
    for (u32 row = 0; row < count; ++row) {
      while ((i32)cursor < (i32)end) {
        GxLineBlend(pixels, cursor, word, opacity);
        cursor += 2;
      }
      cursor += advance;
      end = cursor + 2u * (u32)columns;
    }
    return 1;
  }
  if (x1 > x2) {
    i32 value = x1;
    x1 = x2;
    x2 = value;
    value = y1;
    y1 = y2;
    y2 = value;
  }
  i32 dx = (i32)((u32)x2 - (u32)x1), dy = (i32)((u32)y2 - (u32)y1);
  if (dx < 0)
    dx = (i32)(0u - (u32)dx);
  if (dy < 0)
    dy = (i32)(0u - (u32)dy);
  i32 sy = y1 <= y2 ? 1 : -1, major = dx < dy ? dy : dx, minor = dx < dy ? dx : dy;
  i32 x_step = dx < dy ? 0 : 1, y_step = dx < dy ? sy : 0;
  i32 error = (i32)(2u * (u32)minor - (u32)major), straight = (i32)(2u * (u32)minor);
  i32 diagonal = (i32)(2u * ((u32)minor - (u32)major));
  u32 remaining = (u32)major + 1;
  i32 x = x1, y = y1;
  if (thickness == 1) {
    if ((i32)remaining < 1)
      return 1;
    for (; remaining; --remaining) {
      GxLineBlend(pixels, 2u * ((u32)x + (u32)y * stride), word, opacity);
      if (error >= 0) {
        error = (i32)((u32)error + (u32)diagonal);
        x = (i32)((u32)x + 1);
        y = (i32)((u32)y + (u32)sy);
      } else {
        error = (i32)((u32)error + (u32)straight);
        x = (i32)((u32)x + (u32)x_step);
        y = (i32)((u32)y + (u32)y_step);
      }
    }
    return 1;
  }
  bool descending = y2 <= y1;
  i32 previous_right = (i32)((u32)x - 1), previous_bottom = (i32)((u32)y + (descending ? 1u : kScBackwardGridStep));
  i32 minimum = (i32)((u32)thickness - 1), maximum_x = (i32)((u32)width - (u32)thickness);
  i32 maximum_y = (i32)((u32)height - (u32)thickness);
  for (; remaining; --remaining) {
    if (x < minimum || x > maximum_x || y < minimum || y > maximum_y)
      break;
    if (thickness > 0)
      for (u32 row = 0; row < (u32)thickness; ++row) {
        i32 row_y = (i32)((u32)y + row * (u32)sy);
        bool fresh_row = descending ? row_y < previous_bottom : row_y > previous_bottom;
        for (u32 column = 0; column < (u32)thickness; ++column)
          if ((i32)((u32)x + column) > previous_right || fresh_row)
            GxLineBlend(pixels, 2u * ((u32)x + column + (u32)row_y * stride), word, opacity);
      }
    previous_right = (i32)((u32)x + (u32)thickness - 1);
    previous_bottom = descending ? (i32)((u32)y - (u32)thickness + 1) : (i32)((u32)y + (u32)thickness - 1);
    if (error >= 0) {
      error = (i32)((u32)error + (u32)diagonal);
      x = (i32)((u32)x + 1);
      y = (i32)((u32)y + (u32)sy);
    } else {
      error = (i32)((u32)error + (u32)straight);
      x = (i32)((u32)x + (u32)x_step);
      y = (i32)((u32)y + (u32)y_step);
    }
  }
  for (; remaining; --remaining) {
    i32 left = x < (i32)((u32)width - 1) ? x : (i32)((u32)width - 1);
    if (thickness > 0)
      for (u32 row = 0; row < (u32)thickness; ++row) {
        i32 row_y = (i32)((u32)y + row * (u32)sy);
        bool fresh_row = descending ? row_y < previous_bottom : row_y > previous_bottom;
        i64 offset = left + (i64)row_y * stride;
        if (fresh_row && offset >= 0 && offset < (i64)height * stride) {
          i32 count = x > maximum_x ? (i32)((u32)width - (u32)x) : thickness;
          if (descending) {
            if (count)
              GxLineSpan(pixels, offset, (u32)count, word);
          } else if (count > 0) {
            u32 counter = 0;
            i32 column = 0;
            do {
              GxLineSpan(pixels, offset + column, 1, word);
              column = (i16)++counter;
            } while (column < count);
          }
        }
      }
    previous_bottom = (i32)((u32)y - (u32)thickness + 1);
    if (error >= 0) {
      error = (i32)((u32)error + (u32)diagonal);
      x = (i32)((u32)x + 1);
      y = (i32)((u32)y + (u32)sy);
    } else {
      error = (i32)((u32)error + (u32)straight);
      x = (i32)((u32)x + (u32)x_step);
      y = (i32)((u32)y + (u32)y_step);
    }
  }
  return 1;
}

static bool GxCircleVisible(i32 x, i32 y, i32 width, i32 height) {
  return x >= 0 && x < width && y >= 0 && y < height;
}

static i64 GxCircleAddress(i64 words) {
  u32 value = (u32)words & INT32_MAX;
  return value & kGxCircleOffsetSignBit ? (i64)value - kGxCircleOffsetModulus : (i64)value;
}

static void GxCircleSpan(u8 *pixels, i64 offset, u32 count, u16 pixel) {
  GxLineSpan(pixels, GxCircleAddress(offset), count, pixel);
}

// Walk a source stroke span with its strict or inclusive endpoint and wrapping WORD displacement.
static void GxStrokeSpan(u8 *pixels, i64 begin, i64 end, u32 step, bool reverse, bool inclusive, u16 pixel) {
  i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end);
  while (reverse ? (inclusive ? position >= finish : position > finish)
                 : (inclusive ? position <= finish : position < finish)) {
    GxCircleSpan(pixels, position, 1, pixel);
    position = GxCircleAddress(position + (reverse ? -(i64)step : (i64)step));
  }
}

// Captured native raster locals shared by the two original stroke algorithms, without source-address overlays.
struct GxStrokePlane {
  u8 *pixels;
  u32 stride;
  i32 cx, cy, width, height;
  u16 pixel;
  bool clipped;
  i64 center;

  // Store a single WORD only when the clipped algorithm's captured logical bounds admit the point.
  void Point(i32 x, i32 y) {
    if (!clipped || GxCircleVisible(cx + x, cy + y, width, height))
      GxCircleSpan(pixels, center + x + (i64)y * stride, 1, pixel);
  }

  // Preserve endpoint-visible gating, the inclusive right padding column and the inner reverse-span clamp defect.
  void Horizontal(i32 first, i32 last, i32 y, bool reverse, bool inclusive, u32 clip_mode = 0) {
    if (clipped) {
      bool begin_visible = GxCircleVisible(cx + first, cy + y, width, height);
      bool end_visible = GxCircleVisible(cx + last, cy + y, width, height);
      if (!begin_visible && !end_visible)
        return;
      if (!begin_visible)
        first = reverse && !clip_mode ? (i32)((u32)width - (u32)cx) : -cx;
      if (!end_visible)
        last = reverse && clip_mode != 2 ? -cx : (i32)((u32)width - (u32)cx);
      inclusive = true;
    }
    GxStrokeSpan(
        pixels, center + first + (i64)y * stride, center + last + (i64)y * stride, 1, reverse, inclusive, pixel);
  }

  // Clip only when an endpoint is visible, retaining top zero and the last logical row as source endpoints.
  void Vertical(i32 first, i32 last, i32 x, bool reverse, bool inclusive) {
    if (clipped) {
      bool begin_visible = GxCircleVisible(cx + x, cy + first, width, height);
      bool end_visible = GxCircleVisible(cx + x, cy + last, width, height);
      if (!begin_visible && !end_visible)
        return;
      if (!begin_visible)
        first = reverse ? height - 1 - cy : -cy;
      if (!end_visible)
        last = reverse ? -cy : height - 1 - cy;
      inclusive = true;
    }
    GxStrokeSpan(
        pixels, center + x + (i64)first * stride, center + x + (i64)last * stride, stride, reverse, inclusive, pixel);
  }
};

// Apply the captured-plane stroke algorithm; clipped table lookups follow radius decrements into written rows.
static void GxStrokeRaster(GxStrokePlane *plane, i32 radius, u32 thickness) {
  i32 x = 0, y = radius, error = 1 - radius, straight = 3, diagonal = 5 - 2 * radius;
  i32 inner_radius = (i32)((u32)radius - thickness), inner_x = 0, inner_y = inner_radius;
  u32 inner_error = 1u - (u32)inner_radius, inner_straight = 3, inner_diagonal = 5u - 2u * (u32)inner_radius;
  if (plane->clipped) {
    plane->Point(radius, 0);
    plane->Point(-radius, 0);
    plane->Point(0, -radius);
    plane->Point(0, radius);
  } else {
    plane->Point(0, radius);
    plane->Point(0, -radius);
    plane->Point(-radius, 0);
    plane->Point(radius, 0);
  }
  if (thickness == 1) {
    if (radius > 0)
      do {
        ++x;
        if (error >= 0) {
          error += diagonal;
          straight += 2;
          diagonal += 4;
          --y;
        } else {
          error += straight;
          straight += 2;
          diagonal += 2;
        }
        plane->Point(-x, -y);
        plane->Point(x, -y);
        plane->Point(y, -x);
        plane->Point(y, x);
        plane->Point(x, y);
        plane->Point(-x, y);
        plane->Point(-y, x);
        plane->Point(-y, -x);
      } while (y > x);
    return;
  }
  if (radius > inner_radius)
    do {
      if (y <= x + 1)
        break;
      ++x;
      if (error >= 0) {
        error += diagonal;
        straight += 2;
        diagonal += 4;
        --y;
        plane->Horizontal(-x, x, -y, false, false);
        if (plane->clipped) {
          plane->Vertical(-x, x, y, false, false);
          plane->Horizontal(x, -x, y, true, false);
          plane->Vertical(x, -x, -y, true, false);
        } else {
          plane->Horizontal(x, -x, y, true, false);
          plane->Vertical(x, -x, -y, true, false);
          plane->Vertical(-x, x, y, false, false);
        }
      } else {
        error += straight;
        straight += 2;
        diagonal += 2;
      }
    } while (y > inner_radius);
  if (y <= inner_radius) {
    i32 outline[4097];
    [[maybe_unused]] i32 first_row = y;
    while (y > x + 1) {
      ++x;
      if (error >= 0) {
        error += diagonal;
        straight += 2;
        diagonal += 4;
        --y;
        outline[y] = x;
      } else {
        error += straight;
        straight += 2;
        diagonal += 2;
      }
    }
    if (inner_radius > x + 1) {
      assert(plane->clipped);
      i32 threshold = x + 1;
      do {
        ++inner_x;
        if (inner_error & kScIntegerSignBit) {
          inner_error += inner_straight;
          inner_straight += 2;
          inner_diagonal += 2;
        } else {
          inner_error += inner_diagonal;
          inner_straight += 2;
          inner_diagonal += 4;
          --inner_y;
          assert(inner_y < first_row && inner_y >= y);
          i32 extent = outline[inner_y];
          plane->Horizontal(-extent, -inner_x, -inner_y, false, true);
          plane->Horizontal(inner_x, extent, -inner_y, false, true);
          plane->Vertical(-extent, -inner_x, inner_y, false, true);
          plane->Vertical(inner_x, extent, inner_y, false, true);
          plane->Horizontal(extent, inner_x, inner_y, true, true, 1);
          plane->Horizontal(-inner_x, -extent, inner_y, true, true);
          plane->Vertical(extent, inner_x, -inner_y, true, true);
          plane->Vertical(-inner_x, -extent, -inner_y, true, true);
        }
      } while (inner_y > threshold);
    }
  } else {
    i32 step = x + 1;
    while (step > inner_radius) {
      --step;
      plane->Horizontal(-step, step, -step, false, false);
      if (plane->clipped) {
        plane->Vertical(-step, step, step, false, false);
        plane->Horizontal(step, -step, step, true, false);
        plane->Vertical(step, -step, -step, true, false);
      } else {
        plane->Horizontal(step, -step, step, true, false);
        plane->Vertical(step, -step, -step, true, false);
        plane->Vertical(-step, step, step, false, false);
      }
    }
  }
  while (inner_y >= inner_x) {
    ++inner_x;
    if (!(inner_error & kScIntegerSignBit)) {
      inner_error += inner_diagonal;
      inner_straight += 2;
      inner_diagonal += 4;
      --inner_y;
      plane->Horizontal(-inner_y, -inner_x, -inner_y, false, true);
      plane->Horizontal(inner_x, inner_y, -inner_y, false, false);
      plane->Vertical(-inner_y, -inner_x, inner_y, false, true);
      plane->Vertical(inner_x, inner_y, inner_y, false, false);
      plane->Horizontal(inner_y, inner_x, inner_y, true, true, 2);
      plane->Horizontal(-inner_x, -inner_y, inner_y, true, false, 2);
      plane->Vertical(inner_y, inner_x, -inner_y, true, true);
      plane->Vertical(-inner_x, -inner_y, -inner_y, true, true);
    } else {
      inner_error += inner_straight;
      inner_straight += 2;
      inner_diagonal += 2;
    }
  }
}

void GxStrokeCirclePixels(u8 *pixels, u32 stride, i32 cx, i32 cy, i32 radius, u32 thickness, u32 pixel) {
  GxStrokePlane plane = {pixels, stride, cx, cy, 0, 0, (u16)pixel, false, cx + (i64)cy * stride};
  GxStrokeRaster(&plane, radius, thickness);
}

// Delegate an entirely visible bounding square before width/range checks; otherwise preserve clipped stroke effects.
// Wide strokes call FillCircle before continuing; its return is ignored and callback mutations remain observable.
u8 GxGeneralBuffer::DrawCircle(i32 left, i32 top, i32 right, i32, u32 pixel, u32 thickness) {
  i32 bottom = (i32)((u32)right + (u32)top - (u32)left), width = area.right, height = area.bottom;
  i32 cx = (i32)((u32)left + (u32)right) / 2, radius = (i32)((u32)right - (u32)left) / 2;
  i32 cy = (i32)((u32)top + (u32)radius);
  if (GxCircleVisible(left, top, width, height) && GxCircleVisible(right, bottom, width, height))
    return DrawCircleNoClip(left, top, right, bottom, pixel, thickness);
  if (thickness >= ((u32)right - (u32)left) >> 1)
    FillCircle(left, top, right, bottom, pixel);
  if (!thickness || left < -4096 || left > 4096 || top < -4096 || top > 4096 || right < -4096 || right > 4096 ||
      bottom < -4096 || bottom > 4096 || right < left)
    return 0;
  u32 stride = GetColorSurfaceStride() >> 1;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  GxStrokePlane plane = {pixels, stride, cx, cy, width, height, (u16)pixel, true, cx + (i64)cy * stride};
  GxStrokeRaster(&plane, radius, thickness);
  return 1;
}

// Delegate out-of-bounds squares before the zero-width gate; capture the plane after any filled-circle call.
// Preserve the derived bottom, ignored fourth coordinate, DWORD thickness and raw clipped-delegate byte.
u8 GxGeneralBuffer::DrawCircleNoClip(i32 left, i32 top, i32 right, i32, u32 pixel, u32 thickness) {
  i32 bottom = (i32)((u32)right + (u32)top - (u32)left), width = area.right, height = area.bottom;
  if (!GxCircleVisible(left, top, width, height) || !GxCircleVisible(right, bottom, width, height))
    return DrawCircle(left, top, right, bottom, pixel, thickness);
  if (!thickness)
    return 0;
  if (thickness >= ((u32)right - (u32)left) >> 1)
    FillCircle(left, top, right, bottom, pixel);
  if (left > 4096 || top > 4096 || right > 4096 || bottom > 4096 || right < left)
    return 0;
  u32 stride = GetColorSurfaceStride() >> 1;
  i32 cx = (left + right) / 2, radius = (right - left) / 2, cy = top + radius;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  GxStrokePlane plane = {pixels, stride, cx, cy, width, height, (u16)pixel, false, cx + (i64)cy * stride};
  GxStrokeRaster(&plane, radius, thickness);
  return 1;
}

// Fill clipped circle spans and then the center through the original temporary lock transition.
// The diameter comes from left/right; the supplied bottom is ignored, and partial spans retain inclusive edge quirks.
u8 GxGeneralBuffer::FillCircle(i32 left, i32 top, i32 right, i32, u32 pixel) {
  i32 bottom = (i32)((u32)right + (u32)top - (u32)left);
  if (left < -4096 || left > 4096 || top < -4096 || top > 4096 || right < -4096 || right > 4096 || bottom < -4096 ||
      bottom > 4096 || right < left)
    return 0;
  i32 cx = (left + right) / 2, radius = (right - left) / 2, cy = top + radius;
  i32 width = area.right, height = area.bottom;
  if (GxCircleVisible(left, top, width, height) && GxCircleVisible(right, bottom, width, height))
    return FillCircleNoClip(left, top, right, bottom, pixel);
  u32 stride = GetColorSurfaceStride() >> 1;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u16 word = (u16)pixel;
  i64 center = cx + (i64)cy * stride;
  i32 step = 0, error = 1 - radius, straight = 3, diagonal = 5 - 2 * radius;
  if (GxCircleVisible(cx + radius, cy, width, height))
    GxCircleSpan(pixels, center + radius, 1, word);
  if (GxCircleVisible(cx - radius, cy, width, height))
    GxCircleSpan(pixels, center - radius, 1, word);
  if (GxCircleVisible(cx, cy - radius, width, height))
    GxCircleSpan(pixels, center - (i64)radius * stride, 1, word);
  if (GxCircleVisible(cx, cy + radius, width, height))
    GxCircleSpan(pixels, center + (i64)radius * stride, 1, word);
  if (radius > 1)
    do {
      ++step;
      if (error < 0) {
        error += straight;
        straight += 2;
        diagonal += 2;
      } else {
        error += diagonal;
        straight += 2;
        diagonal += 4;
        --radius;
        bool first = GxCircleVisible(cx - step, cy - radius, width, height);
        bool last = GxCircleVisible(cx + step, cy - radius, width, height);
        if (first || last) {
          i32 begin = first ? cx - step : 0, end = last ? cx + step : width;
          GxCircleSpan(pixels, begin + (i64)(cy - radius) * stride, (u32)(end - begin + 1), word);
        }
        first = GxCircleVisible(cx + radius, cy - step, width, height);
        last = GxCircleVisible(cx + radius, cy + step, width, height);
        if (first || last) {
          i64 begin = cx + radius + (i64)(first ? cy - step : 0) * stride;
          i64 end = cx + radius + (i64)(last ? cy + step : height - 1) * stride;
          for (i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end); position <= finish;
              position = GxCircleAddress(position + stride))
            GxCircleSpan(pixels, position, 1, word);
        }
        first = GxCircleVisible(cx - step, cy + radius, width, height);
        last = GxCircleVisible(cx + step, cy + radius, width, height);
        if (first || last) {
          i64 begin = (last ? cx + step : width) + (i64)(cy + radius) * stride;
          i64 end = (first ? cx - step : 0) + (i64)(cy + radius) * stride;
          for (i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end); position >= finish;
              position = GxCircleAddress(position - 1))
            GxCircleSpan(pixels, position, 1, word);
        }
        first = GxCircleVisible(cx - radius, cy - step, width, height);
        last = GxCircleVisible(cx - radius, cy + step, width, height);
        if (first || last) {
          i64 begin = cx - radius + (i64)(last ? cy + step : height - 1) * stride;
          i64 end = cx - radius + (i64)(first ? cy - step : 0) * stride;
          for (i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end); position >= finish;
              position = GxCircleAddress(position - stride))
            GxCircleSpan(pixels, position, 1, word);
        }
      }
    } while (radius > step + 1);
  Unlock(GxLockFlags::FillCircle);
  if (Lock(GxLockFlags::Fill)) {
    ScRect middle = {cx - step, cy - step, cx + step + 1, cy + step + 1};
    Fill(pixel, middle, nullptr);
    Unlock(GxLockFlags::Fill);
  }
  Lock(GxLockFlags::FillCircle);
  return 1;
}

// Out-of-bounds circles dispatch the clipped virtual; raw pitch/data capture precedes unlock256, lock1 and lock256.
u8 GxGeneralBuffer::FillCircleNoClip(i32 left, i32 top, i32 right, i32, u32 pixel) {
  i32 bottom = (i32)((u32)right + (u32)top - (u32)left);
  if (left < -4096 || left > 4096 || top < -4096 || top > 4096 || right < -4096 || right > 4096 || bottom < -4096 ||
      bottom > 4096 || right < left)
    return 0;
  i32 width = area.right, height = area.bottom;
  if (!GxCircleVisible(left, top, width, height) || !GxCircleVisible(right, bottom, width, height))
    return FillCircle(left, top, right, bottom, pixel);
  u32 stride = GetColorSurfaceStride() >> 1;
  i32 cx = (left + right) / 2, radius = (right - left) / 2, cy = top + radius;
  u8 *pixels = static_cast<u8 *>(GetColorSurfaceBits());
  u16 word = (u16)pixel;
  i64 center = cx + (i64)cy * stride;
  i32 step = 0, error = 1 - radius, straight = 3, diagonal = 5 - 2 * radius;
  GxCircleSpan(pixels, center + (i64)radius * stride, 1, word);
  GxCircleSpan(pixels, center - (i64)radius * stride, 1, word);
  GxCircleSpan(pixels, center - radius, 1, word);
  GxCircleSpan(pixels, center + radius, 1, word);
  if (radius > 1)
    do {
      ++step;
      if (error < 0) {
        error += straight;
        straight += 2;
        diagonal += 2;
      } else {
        error += diagonal;
        straight += 2;
        diagonal += 4;
        --radius;
        i64 begin = center - (i64)radius * stride - step, end = center - (i64)radius * stride + step;
        begin = GxCircleAddress(begin);
        end = GxCircleAddress(end);
        if (begin < end)
          GxCircleSpan(pixels, begin, (u32)(end - begin), word);
        begin = center + (i64)radius * stride + step;
        end = center + (i64)radius * stride - step;
        for (i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end); position > finish;
            position = GxCircleAddress(position - 1))
          GxCircleSpan(pixels, position, 1, word);
        begin = center + (i64)step * stride - radius;
        end = center - (i64)step * stride - radius;
        for (i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end); position > finish;
            position = GxCircleAddress(position - stride))
          GxCircleSpan(pixels, position, 1, word);
        begin = center - (i64)step * stride + radius;
        end = center + (i64)step * stride + radius;
        for (i64 position = GxCircleAddress(begin), finish = GxCircleAddress(end); position < finish;
            position = GxCircleAddress(position + stride))
          GxCircleSpan(pixels, position, 1, word);
      }
    } while (radius > step + 1);
  Unlock(GxLockFlags::FillCircle);
  if (Lock(GxLockFlags::Fill)) {
    ScRect middle = {cx - step, cy - step, cx + step + 1, cy + step + 1};
    FillNoClip(pixel, middle);
    Unlock(GxLockFlags::Fill);
  }
  Lock(GxLockFlags::FillCircle);
  return 1;
}

static i32 GxFloodLeft, GxFloodTop, GxFloodRight, GxFloodBottom;
static u32 GxFloodPitch, GxFloodTarget, GxFloodReplacement;

// Read one potentially unaligned WORD from the captured pixel plane without imposing native alignment requirements.
static u16 GxFloodRead(const u8 *pixels) {
  u16 value;
  memcpy(&value, pixels, sizeof(value));
  return value;
}

static void GxFloodWrite(u8 *pixels) {
  u16 value = (u16)GxFloodReplacement;
  memcpy(pixels, &value, sizeof(value));
}

static void GxFloodReplace(u8 *pixels, i32 x, i32 y) {
  if (GxFloodRead(pixels) != GxFloodTarget)
    return;
  i32 below = (i32)((u32)y + 1);
  do {
    GxFloodWrite(pixels);
    if (x < (i32)((u32)GxFloodRight - 1))
      GxFloodReplace(pixels + 2, (i32)((u32)x + 1), y);
    if (x > GxFloodLeft)
      GxFloodReplace(pixels - 2, (i32)((u32)x - 1), y);
    if (y < (i32)((u32)GxFloodBottom - 1))
      GxFloodReplace(pixels + 2u * (GxFloodPitch >> 1), x, below);
    if (y <= GxFloodTop)
      break;
    y = (i32)((u32)y - 1);
    below = (i32)((u32)below - 1);
    pixels -= 2u * (GxFloodPitch >> 1);
  } while (GxFloodRead(pixels) == GxFloodTarget);
}

// Preserve WORD-rounded vertical displacement and the upward tail; no logical rectangle participates in this kernel.
static void GxFloodReplaceUnclipped(u8 *pixels) {
  while (GxFloodRead(pixels) == GxFloodTarget) {
    GxFloodWrite(pixels);
    GxFloodReplaceUnclipped(pixels + 2);
    GxFloodReplaceUnclipped(pixels - 2);
    GxFloodReplaceUnclipped(pixels + 2u * (GxFloodPitch >> 1));
    pixels -= 2u * (GxFloodPitch >> 1);
  }
}

// The traversal matches the replacement kernel while permitting multiple interior colors.
static void GxFloodBorder(u8 *pixels, i32 x, i32 y) {
  u32 value = GxFloodRead(pixels);
  if (value == GxFloodTarget)
    return;
  i32 below = (i32)((u32)y + 1);
  do {
    if (value == GxFloodReplacement)
      break;
    GxFloodWrite(pixels);
    if (x < (i32)((u32)GxFloodRight - 1))
      GxFloodBorder(pixels + 2, (i32)((u32)x + 1), y);
    if (x > GxFloodLeft)
      GxFloodBorder(pixels - 2, (i32)((u32)x - 1), y);
    if (y < (i32)((u32)GxFloodBottom - 1))
      GxFloodBorder(pixels + 2u * (GxFloodPitch >> 1), x, below);
    if (y <= GxFloodTop)
      break;
    y = (i32)((u32)y - 1);
    below = (i32)((u32)below - 1);
    pixels -= 2u * (GxFloodPitch >> 1);
    value = GxFloodRead(pixels);
  } while (value != GxFloodTarget);
}

// This kernel uniquely uses the raw byte pitch vertically, including odd pitches.
static void GxFloodBorderUnclipped(u8 *pixels) {
  u32 value = GxFloodRead(pixels);
  if (value == GxFloodTarget)
    return;
  do {
    if (value == GxFloodReplacement)
      break;
    GxFloodWrite(pixels);
    GxFloodBorderUnclipped(pixels - GxFloodPitch);
    GxFloodBorderUnclipped(pixels + GxFloodPitch);
    GxFloodBorderUnclipped(pixels - 2);
    pixels += 2;
    value = GxFloodRead(pixels);
  } while (value != GxFloodTarget);
}
