#include "window/sdl_input.h"

#include <assert.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "application/main_dialogs.h"
#include "common/api.h"
#include "common/locale.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"
#include "common/utf8.h"
#include "graphics/api.h"
#include "graphics/sdl_platform.h"
#include "resources/api.h"
#include "resources/file.h"
#include "window/api.h"
#include "window/input.h"

static u32 WnSdlMessageEvent;
static SDL_MouseButtonFlags WnSdlButtons;

struct WnSdlXorCursor {
  WnSdlXorCursor *next;
  SDL_Cursor *cursor;
  u8 *pixels;
  u32 width, height;
  i32 hotspot_x, hotspot_y;
};

static WnSdlXorCursor *WnSdlXorCursors, *WnSdlSelectedXorCursor;

enum WnCursorCompression : u32 {
  kWnCursorRgb = 0,
  kWnCursorRle8 = 1,
  kWnCursorRle4 = 2,
  kWnCursorBitfields = 3,
};

static u16 WnCursorWord(const u8 *bytes) {
  return u16(bytes[0] | u16(bytes[1]) << 8);
}

static u32 WnCursorDword(const u8 *bytes) {
  return u32(bytes[0]) | u32(bytes[1]) << 8 | u32(bytes[2]) << 16 | u32(bytes[3]) << 24;
}

// Preserve the original Win32 wheel line setting where that platform supplies it.
// Other hosts retain the existing SDL conversion without a Windows settings API.
static u32 WnSdlWheelLines() {
#ifdef _WIN32
  UINT lines = 1;
  if (SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
    return UintMin(lines, 10);
#endif
  return 1;
}

// Pack the original signed wheel amount below the game modifier bits.
// Large positive amounts reset to one hundred before the platform line multiplier, as in the installed translator.
static u32 WnSdlWheelData(i32 amount, u32 modifiers) {
  u16 steps = amount >= 1000 ? 100 : static_cast<u16>(amount);
  steps = static_cast<u16>(steps * WnSdlWheelLines());
  return steps | (modifiers << 16);
}

// Convert checked DIB rows through SDL's existing palette and packed-color converters.
// The temporary surface borrows file bytes; the returned RGBA surface owns its converted pixels.
static SDL_Surface *WnCursorSurface(const u8 *table, const u8 *pixels, u32 width, u32 height,
    u32 bits, u32 colors, u32 stride) {
  SDL_PixelFormat format;
  switch (bits) {
  case 1: format = SDL_PIXELFORMAT_INDEX1MSB; break;
  case 4: format = SDL_PIXELFORMAT_INDEX4MSB; break;
  case 8: format = SDL_PIXELFORMAT_INDEX8; break;
  case 16: format = SDL_PIXELFORMAT_XRGB1555; break;
  case 24: format = SDL_PIXELFORMAT_BGR24; break;
  default: format = SDL_PIXELFORMAT_BGRA32; break;
  }
  SDL_Surface *source = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height), format,
      const_cast<u8 *>(pixels), static_cast<int>(stride));
  if (!source)
    return nullptr;
  if (SDL_ISPIXELFORMAT_INDEXED(format)) {
    // Packed blitters can cache unused palette entries; supply the complete format table after validating indices.
    SDL_Palette *palette = SDL_CreatePalette(1u << bits);
    if (!palette) {
      SDL_DestroySurface(source);
      return nullptr;
    }
    for (u32 i = 0; i < colors; ++i)
      palette->colors[i] = {table[i * 4 + 2], table[i * 4 + 1], table[i * 4], 255};
    bool attached = SDL_SetSurfacePalette(source, palette);
    SDL_DestroyPalette(palette);
    if (!attached) {
      SDL_DestroySurface(source);
      return nullptr;
    }
  }
  SDL_Surface *surface = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(source);
  if (surface && !SDL_FlipSurface(surface, SDL_FLIP_VERTICAL)) {
    SDL_DestroySurface(surface);
    return nullptr;
  }
  return surface;
}

// Expand one DIB bitfield into an eight-bit channel using Windows' single high-bit replication step.
// Fields narrower than four bits deliberately do not repeat to fill every low bit.
static u8 WnCursorChannel(u32 value, u32 mask) {
  if (!mask)
    return 0;
  while (!(mask & 1)) {
    mask >>= 1;
    value >>= 1;
  }
  value &= mask;
  u32 bits = 0;
  for (u32 remaining = mask; remaining; remaining >>= 1)
    ++bits;
  if (bits > 8)
    return static_cast<u8>(value >> (bits - 8));
  value <<= 8 - bits;
  return static_cast<u8>(value | (value >> bits));
}

// Decode explicit RGB masks without requiring an SDL format enum for every valid DIB channel layout.
// Rows are bottom-up, and the caller decides whether the original cursor loader retains the high alpha byte.
static SDL_Surface *WnCursorBitfieldSurface(const u8 *pixels, const u8 *masks, u32 width, u32 height,
    u32 bits, u32 stride) {
  SDL_Surface *surface = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
  if (!surface)
    return nullptr;
  u32 red = WnCursorDword(masks), green = WnCursorDword(masks + 4), blue = WnCursorDword(masks + 8);
  for (u32 y = 0; y < height; ++y) {
    const u8 *row = pixels + (height - 1 - y) * stride;
    u8 *destination = static_cast<u8 *>(surface->pixels) + y * surface->pitch;
    for (u32 x = 0; x < width; ++x) {
      u32 value = bits == 16 ? WnCursorWord(row + x * 2) : WnCursorDword(row + x * 4);
      destination[x * 4] = WnCursorChannel(value, red);
      destination[x * 4 + 1] = WnCursorChannel(value, green);
      destination[x * 4 + 2] = WnCursorChannel(value, blue);
      destination[x * 4 + 3] = static_cast<u8>(value >> 24);
    }
  }
  return surface;
}

// Expand DIB RLE commands into zero-filled indexed rows, clipping horizontal runs to the destination extent.
// Cursor color decoding ends at its visible height; the following AND mask retains its separate file layout.
static bool WnCursorDecodeRle(const u8 *source, u32 length, u32 width, u32 height, u32 bits, u32 stride,
    ScArray<u8> *decoded) {
  decoded->Reserve(stride * height);
  decoded->size = stride * height;
  memset(decoded->data, 0, decoded->size);
  u64 x = 0, y = 0;
  u32 position = 0;
  while (y < height) {
    if (length - position < 2)
      return false;
    u32 count = source[position++], value = source[position++];
    if (!count && !value) {
      x = 0;
      ++y;
      continue;
    }
    if (!count && value == 1)
      return true;
    if (!count && value == 2) {
      if (length - position < 2)
        return false;
      x += source[position++];
      y += source[position++];
      continue;
    }
    u32 bytes = 0;
    if (!count) {
      count = value;
      bytes = (count * bits + 7) / 8;
      bytes += bytes & 1;
      if (bytes > length - position)
        return false;
    }
    for (u32 i = 0; i < count && x + i < width; ++i) {
      u32 index;
      if (bytes)
        index = bits == 8 ? source[position + i] : (source[position + i / 2] >> (4 - (i & 1) * 4)) & 15;
      else
        index = bits == 8 ? value : (value >> (4 - (i & 1) * 4)) & 15;
      u8 *pixel = decoded->data + static_cast<u32>(y) * stride + static_cast<u32>(x + i) * bits / 8;
      if (bits == 8)
        *pixel = static_cast<u8>(index);
      else {
        u32 shift = 4 - (static_cast<u32>(x + i) & 1) * 4;
        *pixel = static_cast<u8>((*pixel & ~(15u << shift)) | (index << shift));
      }
    }
    x += count;
    position += bytes;
  }
  return true;
}

// Select the closest directory dimensions to twice the system cursor size, preferring greater depth on ties.
// Windows supplies its cursor metrics; other SDL platforms use the original 32-logical-pixel nominal size.
static const u8 *WnCursorSelectEntry(const u8 *bytes, u32 size, u32 count) {
  if (count > (size - 6) / 16)
    return nullptr;
#ifdef _WIN32
  i32 metric_width = GetSystemMetrics(SM_CXCURSOR), metric_height = GetSystemMetrics(SM_CYCURSOR);
  i32 target_width = 2 * (metric_width > 0 ? metric_width : 32);
  i32 target_height = 2 * (metric_height > 0 ? metric_height : 32);
#else
  i32 target_width = 64, target_height = 64;
#endif
  const u8 *best = bytes + 6;
  u32 best_bits = 0, best_distance = UINT32_MAX;
  for (u32 index = 0; index < count; ++index) {
    const u8 *entry = bytes + 6 + 16 * index;
    u32 length = WnCursorDword(entry + 8), offset = WnCursorDword(entry + 12);
    if (offset > size || length > size - offset || length < 16)
      return nullptr;
    u32 bits = WnCursorWord(bytes + offset + 14);
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
      return nullptr;
    i32 width = entry[0] ? entry[0] : 256, height = entry[1] ? entry[1] : 256;
    u32 distance = static_cast<u32>(abs(width - target_width) + abs(height - target_height));
    if (distance < best_distance || (distance == best_distance && bits > best_bits)) {
      best = entry;
      best_bits = bits;
      best_distance = distance;
    }
  }
  return best;
}

// Convert the Windows physical cursor metrics to SDL logical pixels; SDL applies display scaling when creating it.
static void WnCursorLogicalSize(u32 *width, u32 *height) {
  *width = *height = 32;
#ifdef _WIN32
  float scale = GxSdlCursorScale();
  i32 physical_width = GetSystemMetrics(SM_CXCURSOR), physical_height = GetSystemMetrics(SM_CYCURSOR);
  if (physical_width > 0 && physical_height > 0) {
    *width = static_cast<u32>(physical_width / scale + 0.5f);
    *height = static_cast<u32>(physical_height / scale + 0.5f);
  }
#endif
}

// Scale cursor pixels using the Windows-observed nearest-neighbor sample selection in each source row.
// Copy all RGBA bytes, including RGB beneath transparent AND bits used by XOR cursors.
static SDL_Surface *WnCursorScale(SDL_Surface *source, u32 width, u32 height) {
  SDL_Surface *scaled = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
  if (!scaled)
    return nullptr;
  for (u32 y = 0; y < height; ++y) {
    u32 sy = static_cast<u32>((u64(y) * source->h + 3 * u64(source->h) / 4) / height);
    const u8 *input = static_cast<const u8 *>(source->pixels) + usize(sy) * source->pitch;
    u8 *output = static_cast<u8 *>(scaled->pixels) + usize(y) * scaled->pitch;
    for (u32 x = 0; x < width; ++x) {
      u32 sx = static_cast<u32>((u64(x) * source->w + 3 * u64(source->w) / 4) / width);
      memcpy(output + x * 4, input + sx * 4, 4);
    }
  }
  return scaled;
}

// Scale a signed CUR hotspot with the native 15-bit coordinate wrap and its sign-dependent rounding.
// The numerator is wrapped before rounding, including when the wrapped value crosses zero.
static i32 WnCursorScaleHotspot(i32 value, u32 source, u32 target) {
  constexpr i64 kCoordinatePeriod = 32768, kCoordinateHalfPeriod = 16384;
  i64 numerator = i64(value) * target, period = kCoordinatePeriod * source, bound = kCoordinateHalfPeriod * source;
  numerator %= period;
  if (numerator >= bound)
    numerator -= period;
  if (numerator < -bound)
    numerator += period;
  i64 denominator = 4 * i64(source);
  if (value >= 0 && numerator < 0) {
    i64 biased = 4 * numerator + source, quotient = biased / denominator;
    return static_cast<i32>(quotient - (biased % denominator < 0 ? 1 : 0));
  }
  if (value < 0 && numerator >= 0)
    return static_cast<i32>((4 * numerator + 5 * i64(source) - 2) / denominator);
  if (value < 0)
    return -static_cast<i32>((-4 * numerator + source + 1) / denominator);
  return static_cast<i32>((4 * numerator + 3 * i64(source)) / denominator);
}

// Load indexed, RGB, RLE or explicit-bitfield CUR pixels and transfer the hotspot to SDL's owned color cursor.
// Reject malformed bitmaps as an ordinary load failure; valid unimplemented encodings remain explicit.
static SDL_Cursor *WnSdlLoadCursorImage(const ScStringView &path, bool composited_only) {
  RsFile file(path);
  if (!file.Open(kRsFileAccessRead, kRsFileOpenExisting, kRsFileShareRead))
    return nullptr;
  u32 size = file.Size();
  if (size < 22)
    return nullptr;
  ScArray<u8> storage;
  storage.Reserve(size);
  if (!file.Read(storage.data, size))
    return nullptr;
  storage.size = size;
  const u8 *bytes = storage.data;
  if (WnCursorWord(bytes) != 0 || WnCursorWord(bytes + 2) != 2 || !WnCursorWord(bytes + 4))
    return nullptr;
  u32 count = WnCursorWord(bytes + 4);
  const u8 *entry = count == 1 ? bytes + 6 : WnCursorSelectEntry(bytes, size, count);
  if (!entry)
    return nullptr;
  u32 width, height;
  i32 hotspot_x = static_cast<i16>(WnCursorWord(entry + 4));
  i32 hotspot_y = static_cast<i16>(WnCursorWord(entry + 6));
  u32 image_size = WnCursorDword(entry + 8), offset = WnCursorDword(entry + 12);
  if (offset > size || image_size > size - offset || image_size < 40)
    return nullptr;
  const u8 *info = bytes + offset;
  u32 header = WnCursorDword(info), bitmap_width = WnCursorDword(info + 4), bitmap_height = WnCursorDword(info + 8);
  u32 planes = WnCursorWord(info + 12), bits = WnCursorWord(info + 14), colors = WnCursorDword(info + 32);
  u32 compression = WnCursorDword(info + 16);
  // LoadCursorFromFile rejects extended DIB headers, including otherwise valid V4/V5 RGB and bitfield images.
  // Preserve that recoverable file failure rather than stopping the application as missing reconstruction.
  if (header != 40)
    return nullptr;
  if (!bitmap_width || !bitmap_height || planes != 1 ||
      (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
      (compression == kWnCursorRle8 && bits != 8) || (compression == kWnCursorRle4 && bits != 4) ||
      (compression == kWnCursorBitfields && bits != 16 && bits != 32))
    return nullptr;
  bool rle = compression == kWnCursorRle4 || compression == kWnCursorRle8;
  if (bitmap_width > INT32_MAX || bitmap_height > INT32_MAX || bitmap_height < 2)
    return nullptr;
  // The native CUR loader rejects the other DIB compression codes, including embedded PNG/JPEG forms.
  if (compression != kWnCursorRgb && compression != kWnCursorBitfields && !rle)
    return nullptr;
  // Directory dimensions choose an image; its DIB dimensions govern decoding and hotspot scaling.
  width = bitmap_width;
  height = bitmap_height / 2;
  bool external_source = hotspot_x < 0 || hotspot_y < 0 || u32(hotspot_x) >= width || u32(hotspot_y) >= height;
  u32 table_offset = 40 + (compression == kWnCursorBitfields ? 12 : 0);
  if (image_size < table_offset)
    return nullptr;
  if (!colors && bits <= 8)
    colors = 1u << bits;
  if ((bits <= 8 && colors > (1u << bits)) || colors > (image_size - table_offset) / 4)
    return nullptr;
  u64 stride64 = ((u64(width) * bits + 31) / 32) * 4, mask_stride64 = ((u64(width) + 31) / 32) * 4;
  u32 pixel_offset = table_offset + colors * 4;
  u64 mask_offset64 = pixel_offset + stride64 * height;
  if (mask_offset64 > image_size || mask_stride64 * height > image_size - mask_offset64)
    return nullptr;
  u32 stride = static_cast<u32>(stride64), mask_stride = static_cast<u32>(mask_stride64);
  u32 mask_offset = static_cast<u32>(mask_offset64);
  const u8 *pixels = info + pixel_offset, *mask = info + mask_offset;
  ScArray<u8> decoded;
  if (rle) {
    // Windows locates the mask after nominal uncompressed color rows, independent of biSizeImage.
    if (!WnCursorDecodeRle(pixels, image_size - pixel_offset, width, height, bits, stride, &decoded))
      return nullptr;
    pixels = decoded.data;
  }
  if (bits <= 8) {
    u32 per_byte = 8 / bits, index_mask = (1u << bits) - 1;
    for (u32 y = 0; y < height; ++y)
      for (u32 x = 0; x < width; ++x)
        if (((pixels[y * stride + x / per_byte] >> (8 - bits - x % per_byte * bits)) & index_mask) >= colors)
          return nullptr;
  }
  bool alpha = false;
  // Windows retains ARGB bytes only for its canonical 32-bit layout; other bitfields undergo RGB conversion.
  // Those converted cursors use the AND mask even when their stored high bytes are nonzero.
  bool retains_alpha = compression == kWnCursorRgb ||
      (WnCursorDword(info + 40) == 0xff0000 && WnCursorDword(info + 44) == 0xff00 &&
          WnCursorDword(info + 48) == 0xff);
  if (bits == 32 && retains_alpha)
    for (u32 y = 0; y < height; ++y)
      for (u32 x = 0; x < width; ++x)
        alpha |= pixels[y * stride + x * 4 + 3] != 0;
  SDL_Surface *surface = compression == kWnCursorBitfields ?
      WnCursorBitfieldSurface(pixels, info + 40, width, height, bits, stride) :
      WnCursorSurface(info + table_offset, pixels, width, height, bits, colors, stride);
  if (!surface)
    return nullptr;
  bool inverted = false;
  for (u32 y = 0; y < height; ++y) {
    u8 *destination = static_cast<u8 *>(surface->pixels) + y * surface->pitch;
    u32 row = height - 1 - y;
    for (u32 x = 0; x < width; ++x) {
      if (alpha)
        continue;
      bool transparent = (mask[row * mask_stride + x / 8] & (0x80u >> (x % 8))) != 0;
      inverted |= transparent && (destination[x * 4] || destination[x * 4 + 1] || destination[x * 4 + 2]);
      destination[x * 4 + 3] = transparent ? 0 : 255;
    }
  }
  u32 target_width, target_height;
  WnCursorLogicalSize(&target_width, &target_height);
  if (width != target_width || height != target_height) {
    SDL_Surface *scaled = WnCursorScale(surface, target_width, target_height);
    SDL_DestroySurface(surface);
    if (!scaled)
      return nullptr;
    surface = scaled;
  }
  hotspot_x = WnCursorScaleHotspot(hotspot_x, width, target_width);
  hotspot_y = WnCursorScaleHotspot(hotspot_y, height, target_height);
  width = target_width;
  height = target_height;
  bool outside = hotspot_x < 0 || hotspot_y < 0 || u32(hotspot_x) >= width || u32(hotspot_y) >= height;
  if ((external_source || outside) && alpha) {
    // An alpha cursor with its hotspot outside the image is shown as SDL's native alpha cursor with the hotspot
    // clamped into the image. No cursor installed with the game has this layout; only custom cursors reach it.
    hotspot_x = hotspot_x < 0 ? 0 : u32(hotspot_x) >= width ? width - 1 : hotspot_x;
    hotspot_y = hotspot_y < 0 ? 0 : u32(hotspot_y) >= height ? height - 1 : hotspot_y;
    external_source = outside = false;
  }
  if (composited_only && !inverted && !external_source && !outside) {
    SDL_DestroySurface(surface);
    return nullptr;
  }
  WnSdlXorCursor *xor_cursor = nullptr;
  if (inverted || external_source || outside) {
    xor_cursor = new WnSdlXorCursor{};
    xor_cursor->pixels = new u8[usize(width) * height * 4];
    xor_cursor->width = width;
    xor_cursor->height = height;
    xor_cursor->hotspot_x = static_cast<i32>(hotspot_x);
    xor_cursor->hotspot_y = static_cast<i32>(hotspot_y);
    for (u32 y = 0; y < height; ++y)
      memcpy(xor_cursor->pixels + usize(y) * width * 4,
          static_cast<const u8 *>(surface->pixels) + usize(y) * surface->pitch, width * 4);
  }
  // The fallback handle is only shown outside the canvas, where SDL cannot represent an external hotspot.
  int native_x = hotspot_x >= 0 && u32(hotspot_x) < width ? hotspot_x : 0;
  int native_y = hotspot_y >= 0 && u32(hotspot_y) < height ? hotspot_y : 0;
  SDL_Cursor *cursor = SDL_CreateColorCursor(surface, native_x, native_y);
  SDL_DestroySurface(surface);
  if (xor_cursor) {
    if (cursor) {
      xor_cursor->cursor = cursor;
      xor_cursor->next = WnSdlXorCursors;
      WnSdlXorCursors = xor_cursor;
    } else {
      delete[] xor_cursor->pixels;
      delete xor_cursor;
    }
  }
  return cursor;
}

// Load any supported CUR and retain its AND/XOR pixels when SDL must compose them over the canvas.
SDL_Cursor *WnSdlLoadCursor(const ScStringView &path) {
  return WnSdlLoadCursorImage(path, false);
}

// Preserve ordinary BMP cursors while admitting inversion and external hotspots to canvas composition.
SDL_Cursor *WnSdlLoadCompositedCursor(const ScStringView &path) {
  return WnSdlLoadCursorImage(path, true);
}

// Deselect and free any borrowed composition data before destroying the SDL-owned handle.
void WnSdlDestroyCursor(SDL_Cursor *cursor) {
  WnSdlXorCursor **link = &WnSdlXorCursors;
  while (*link && (*link)->cursor != cursor)
    link = &(*link)->next;
  if (*link) {
    WnSdlXorCursor *entry = *link;
    *link = entry->next;
    if (WnSdlSelectedXorCursor == entry) {
      WnSdlSelectedXorCursor = nullptr;
      GxSdlSetXorCursor(nullptr, 0, 0, 0, 0);
    }
    delete[] entry->pixels;
    delete entry;
  }
  SDL_DestroyCursor(cursor);
}

// Select the native fallback and attach its owned composition data only for an inverting image.
void WnSdlSetCursor(SDL_Cursor *cursor) {
  WnSdlSelectedXorCursor = WnSdlXorCursors;
  while (WnSdlSelectedXorCursor && WnSdlSelectedXorCursor->cursor != cursor)
    WnSdlSelectedXorCursor = WnSdlSelectedXorCursor->next;
  SDL_SetCursor(cursor);
  WnSdlXorCursor *entry = WnSdlSelectedXorCursor;
  GxSdlSetXorCursor(entry ? entry->pixels : nullptr, entry ? entry->width : 0, entry ? entry->height : 0,
      entry ? entry->hotspot_x : 0, entry ? entry->hotspot_y : 0);
}

// Queue the game's native message words without narrowing pointer-sized modal results.
bool WnSdlPostMessage(u32 type, uptr first, uptr second) {
  if (!WnSdlMessageEvent)
    WnSdlMessageEvent = SDL_RegisterEvents(1);
  if (WnSdlMessageEvent == UINT32_MAX)
    return false;
  SDL_Event event{};
  event.type = WnSdlMessageEvent;
  event.user.code = static_cast<i32>(type);
  event.user.data1 = reinterpret_cast<void *>(first);
  event.user.data2 = reinterpret_cast<void *>(second);
  return SDL_PushEvent(&event);
}

// Recover posted game words so nested modal loops can recognize their own completion messages.
bool WnSdlPostedMessage(const SDL_Event *event, u32 *type, uptr *first, uptr *second) {
  if (!WnSdlMessageEvent || event->type != WnSdlMessageEvent)
    return false;
  *type = static_cast<u32>(event->user.code);
  *first = reinterpret_cast<uptr>(event->user.data1);
  *second = reinterpret_cast<uptr>(event->user.data2);
  return true;
}

// Convert SDL key identities to the virtual-key values consumed by game accelerators and controls.
static u32 WnSdlKey(SDL_Keycode key, SDL_Scancode scan) {
  if (key >= SDLK_A && key <= SDLK_Z)
    return key - SDLK_A + kWnKeyA;
  if (key >= SDLK_0 && key <= SDLK_9)
    return key;
  if (scan >= SDL_SCANCODE_F1 && scan <= SDL_SCANCODE_F12)
    return kWnKeyF1 + scan - SDL_SCANCODE_F1;
  if (scan >= SDL_SCANCODE_KP_1 && scan <= SDL_SCANCODE_KP_9)
    return kWnKeyNumpad1 + scan - SDL_SCANCODE_KP_1;
  switch (scan) {
  case SDL_SCANCODE_BACKSPACE:
    return kWnKeyBackspace;
  case SDL_SCANCODE_TAB:
    return kWnKeyTab;
  case SDL_SCANCODE_RETURN:
  case SDL_SCANCODE_KP_ENTER:
    return kWnKeyReturn;
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT:
    return kWnKeyShift;
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL:
    return kWnKeyControl;
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT:
    return kWnKeyAlt;
  case SDL_SCANCODE_PAUSE:
    return kWnKeyPause;
  case SDL_SCANCODE_CAPSLOCK:
    return kWnKeyCapsLock;
  case SDL_SCANCODE_ESCAPE:
    return kWnKeyEscape;
  case SDL_SCANCODE_SPACE:
    return kWnKeySpace;
  case SDL_SCANCODE_PAGEUP:
    return kWnKeyPageUp;
  case SDL_SCANCODE_PAGEDOWN:
    return kWnKeyPageDown;
  case SDL_SCANCODE_END:
    return kWnKeyEnd;
  case SDL_SCANCODE_HOME:
    return kWnKeyHome;
  case SDL_SCANCODE_LEFT:
    return kWnKeyLeft;
  case SDL_SCANCODE_UP:
    return kWnKeyUp;
  case SDL_SCANCODE_RIGHT:
    return kWnKeyRight;
  case SDL_SCANCODE_DOWN:
    return kWnKeyDown;
  case SDL_SCANCODE_PRINTSCREEN:
    return kWnKeyPrintScreen;
  case SDL_SCANCODE_INSERT:
    return kWnKeyInsert;
  case SDL_SCANCODE_DELETE:
    return kWnKeyDelete;
  case SDL_SCANCODE_KP_0:
    return kWnKeyNumpad0;
  case SDL_SCANCODE_KP_MULTIPLY:
    return kWnKeyNumpadMultiply;
  case SDL_SCANCODE_KP_PLUS:
    return kWnKeyNumpadAdd;
  case SDL_SCANCODE_KP_MINUS:
    return kWnKeyNumpadSubtract;
  case SDL_SCANCODE_KP_PERIOD:
    return kWnKeyNumpadDecimal;
  case SDL_SCANCODE_KP_DIVIDE:
    return kWnKeyNumpadDivide;
  case SDL_SCANCODE_NUMLOCKCLEAR:
    return kWnKeyNumLock;
  case SDL_SCANCODE_SCROLLLOCK:
    return kWnKeyScrollLock;
  default: return 0;
  }
}

// Read SDL's current physical keyboard state independently of the game's queued key list.
bool WnSdlIsKeyDown(u32 key) {
  int count;
  const bool *state = SDL_GetKeyboardState(&count);
  for (int i = 0; i < count; ++i) {
    SDL_Scancode scan = static_cast<SDL_Scancode>(i);
    if (state[i] && WnSdlKey(SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, false), scan) == key)
      return true;
  }
  return false;
}

// Remove queued input while retaining leading releases and the final unmatched key press, as the game expects.
u32 WnSdlFlushInput() {
  SDL_Event saved[256], event, pending{};
  u32 count = 0, removed = 0;
  bool key_down = false, pending_key = false, left_down = false, middle_down = false, right_down = false;
  while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_TEXT_INPUT) > 0) {
    if (event.type == SDL_EVENT_KEY_DOWN) {
      ++removed;
      key_down = pending_key = true;
      pending = event;
    } else if (event.type == SDL_EVENT_KEY_UP) {
      if (!key_down && count < 256)
        saved[count++] = event;
      else
        ++removed;
      pending_key = false;
    } else {
      ++removed;
    }
  }
  if (pending_key && count < 256)
    saved[count++] = pending;
  while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL) > 0) {
    bool retain = false;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      if (event.button.button == SDL_BUTTON_LEFT)
        left_down = true;
      else if (event.button.button == SDL_BUTTON_MIDDLE)
        middle_down = true;
      else if (event.button.button == SDL_BUTTON_RIGHT)
        right_down = true;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      retain = (event.button.button == SDL_BUTTON_LEFT && !left_down) ||
          (event.button.button == SDL_BUTTON_MIDDLE && !middle_down) ||
          (event.button.button == SDL_BUTTON_RIGHT && !right_down);
    }
    if (retain && count < 256)
      saved[count++] = event;
    else
      ++removed;
  }
  for (u32 i = 0; i < count; ++i)
    SDL_PushEvent(&saved[i]);
  return removed;
}

// Preserve the three game modifier bits and the original key-repeat flag.
static u32 WnSdlModifiers(SDL_Keymod mod, bool repeat) {
  u32 value = 0;
  if (mod & SDL_KMOD_SHIFT)
    value |= kWnModifierShift;
  if (mod & SDL_KMOD_CTRL)
    value |= kWnModifierControl;
  if (mod & SDL_KMOD_ALT)
    value |= kWnModifierAlt;
  if (repeat)
    value |= kWnKeyRepeatModifier;
  return value;
}

// Translate mouse button and modifier state to the bit layout consumed by game windows.
static u32 WnSdlMouseModifiers(SDL_MouseButtonFlags buttons) {
  u32 value = WnSdlModifiers(SDL_GetModState(), false) & kWnModifierShiftControlMask;
  if (buttons & SDL_BUTTON_LMASK)
    value |= kWnModifierMouseLeft;
  if (buttons & SDL_BUTTON_RMASK)
    value |= kWnModifierMouseRight;
  return value;
}

// Convert window coordinates to the framebuffer and clamp them to the game canvas.
static bool WnSdlCoordinates(WnWindowManager *manager, float x, float y, i32 *out_x, i32 *out_y) {
  float gx, gy;
  if (!GxSdlWindowToGame(x, y, &gx, &gy))
    return false;
  ScRect area = manager->GetCanvas()->GetArea();
  i32 width = area.right - area.left, height = area.bottom - area.top;
  if (width <= 0 || height <= 0)
    return false;
  *out_x = gx < 0 ? 0 : gx >= width ? width - 1 : static_cast<i32>(gx);
  *out_y = gy < 0 ? 0 : gy >= height ? height - 1 : static_cast<i32>(gy);
  return true;
}

// Deliver keyboard, text and mouse events to the same game interfaces used by the original canvas.
// Posted messages retain the original translator's no-op default instead of inventing an unsupported-operation failure.
void WnDispatchSdlEvent(const SDL_Event *event) {
  if (event->type == SDL_EVENT_MOUSE_MOTION || event->type == SDL_EVENT_WINDOW_MOUSE_ENTER ||
      event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
    GxSdlRefreshCursor();
  WnWindowManager *manager = WnGetWindowManager();
  if (!manager)
    return;
  ScMessage message{};
  if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    manager->GetKeyboard()->ClearKeyboard();
    manager->SetCapture(nullptr);
    WnSdlButtons = 0;
    return;
  }
  u32 posted_type;
  uptr first, second;
  if (WnSdlPostedMessage(event, &posted_type, &first, &second)) {
    if (posted_type == kWnMsg_KeyDown || posted_type == kWnMsg_KeyUp) {
      message.type = posted_type;
      message.data1 = first;
      message.data2 = WnSdlModifiers(SDL_GetModState(), (second & kWnKeyRepeatModifier) != 0);
      manager->ProcessKeyboardMessage(&message);
    } else if (posted_type >= kWnMsg_MouseDownL && posted_type <= kWnMsg_MouseWheel) {
      message.type = posted_type;
      i32 x = static_cast<i16>(second), y = static_cast<i16>(second >> 16);
      if (posted_type == kWnMsg_MouseDownL || posted_type == kWnMsg_MouseDownR)
        SDL_CaptureMouse(true);
      else if (posted_type == kWnMsg_MouseUpL || posted_type == kWnMsg_MouseUpR)
        SDL_CaptureMouse(false);
      if (posted_type == kWnMsg_MouseWheel) {
        i32 window_x, window_y;
        if (SDL_Window *window = GxSdlWindow(); window && SDL_GetWindowPosition(window, &window_x, &window_y)) {
          float game_x, game_y;
          if (GxSdlWindowToGame(static_cast<float>(x - window_x), static_cast<float>(y - window_y), &game_x, &game_y)) {
            x = static_cast<i32>(game_x);
            y = static_cast<i32>(game_y);
          }
        }
      }
      ScRect area = manager->GetCanvas()->GetArea();
      i32 width = area.right - area.left, height = area.bottom - area.top;
      if (width <= 0 || height <= 0)
        return;
      message.data1 = static_cast<u32>(x < 0 ? 0 : x >= width ? width - 1 : x);
      message.data2 = static_cast<u32>(y < 0 ? 0 : y >= height ? height - 1 : y);
      u32 modifiers = ((first & 1) ? 16 : 0) | ((first & 2) ? 32 : 0) |
          ((first & 4) ? 1 : 0) | ((first & 8) ? 2 : 0);
      message.data3 = posted_type == kWnMsg_MouseWheel ?
          WnSdlWheelData(100 * static_cast<i16>(first >> 16) / 120, modifiers) : modifiers;
      manager->ProcessMouseMessage(&message);
    } else if (posted_type == kWnMsg_ActivateApp) {
      message = {posted_type, first, 0, 0};
      manager->SendMessageToTarget(nullptr, &message);
    }
    return;
  }
  if (event->type == SDL_EVENT_RENDER_TARGETS_RESET || event->type == SDL_EVENT_RENDER_DEVICE_RESET) {
    GxSdlInvalidateTexture();
    manager->InvalidateAllWindows();
    return;
  }
  if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
    u32 key = WnSdlKey(event->key.key, event->key.scancode);
    if (!key)
      return;
    bool down = event->type == SDL_EVENT_KEY_DOWN;
    WnKeyboard *keyboard = manager->GetKeyboard();
    if (down)
      keyboard->ProcessKeyDown(key);
    else if (!((key == 16 && (event->key.mod & SDL_KMOD_SHIFT)) ||
        (key == 17 && (event->key.mod & SDL_KMOD_CTRL)) || (key == 18 && (event->key.mod & SDL_KMOD_ALT))))
      keyboard->ProcessKeyUp(key);
    message.type = down ? kWnMsg_KeyDown : kWnMsg_KeyUp;
    message.data1 = key;
    message.data2 = WnSdlModifiers(event->key.mod, event->key.repeat || !down);
    manager->ProcessKeyboardMessage(&message);
    if (down && (key == 8 || key == 9 || key == 13 || key == 27)) {
      message = ScMessage{};
      message.type = kWnMsg_Character;
      message.data1 = key;
      manager->ProcessKeyboardMessage(&message);
    }
    return;
  }
  if (event->type == SDL_EVENT_TEXT_INPUT) {
    const char *text = event->text.text;
    usize length = SDL_strlen(text);
    if (length > UINT32_MAX || !ScUtf8Validate(text, static_cast<u32>(length))) {
      SDL_SetError("SDL text input is not a valid UTF-8 game string");
      return;
    }
    for (usize index = 0; index < length; ++index) {
      message = ScMessage{};
      message.type = kWnMsg_Character;
      message.data1 = static_cast<u8>(text[index]);
      manager->ProcessKeyboardMessage(&message);
    }
    return;
  }
  float x, y;
  SDL_MouseButtonFlags buttons;
  if (event->type == SDL_EVENT_MOUSE_MOTION) {
    message.type = kWnMsg_MouseMove;
    x = event->motion.x;
    y = event->motion.y;
    buttons = WnSdlButtons = event->motion.state;
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    if (event->button.button != SDL_BUTTON_LEFT && event->button.button != SDL_BUTTON_RIGHT)
      return;
    bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    message.type = event->button.button == SDL_BUTTON_LEFT ? (down ? kWnMsg_MouseDownL : kWnMsg_MouseUpL)
                                                           : (down ? kWnMsg_MouseDownR : kWnMsg_MouseUpR);
    x = event->button.x;
    y = event->button.y;
    SDL_MouseButtonFlags mask = SDL_BUTTON_MASK(event->button.button);
    if (down)
      WnSdlButtons |= mask;
    else
      WnSdlButtons &= ~mask;
    buttons = WnSdlButtons;
  } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    message.type = kWnMsg_MouseWheel;
    x = event->wheel.mouse_x;
    y = event->wheel.mouse_y;
    buttons = WnSdlButtons;
  } else {
    return;
  }
  i32 ix, iy;
  if (!WnSdlCoordinates(manager, x, y, &ix, &iy))
    return;
  message.data1 = static_cast<u32>(ix);
  message.data2 = static_cast<u32>(iy);
  message.data3 = WnSdlMouseModifiers(buttons);
  if (message.type == kWnMsg_MouseWheel) {
    float amount = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
    message.data3 = WnSdlWheelData(static_cast<i32>(amount * 100), static_cast<u32>(message.data3));
  }
  manager->ProcessMouseMessage(&message);
}
