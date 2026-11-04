#include "buildings/architect/image.h"

#include <string.h>

#include "buildings/architect/api.h"
#include "buildings/architect/codec.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"

static void BdStoreDisplayPixel(u8 *out, u32 pixel) {
  out[0] = static_cast<u8>(pixel);
  out[1] = static_cast<u8>(pixel >> 8);
}

// Convert indexed source bytes through a256-entry lazy display-color cache, retaining the BMP-only vertical reversal.
// The decoded source is consumed continuously; destination pitch is truncated to an even byte count.
bool BdImageLoader::CopyIndexed(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  u32 colors[256];
  for (u32 &color : colors)
    color = kGxUnconvertedPaletteEntry;
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u32 pitch = (out->GetColorSurfaceStride() >> 1) * 2;
  u8 *row = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = pixels->pixels;
  isize step = pitch;
  if (info->signature == kGxBitmapCodecSignature && height > 0) {
    row += static_cast<usize>(pitch) * (height - 1);
    step = -static_cast<isize>(pitch);
  }
  for (i32 y = 0; y < height; ++y) {
    u8 *target = row;
    for (i32 x = 0; x < width; ++x) {
      u8 index = *source++;
      u32 *color = colors + index;
      if (static_cast<i32>(*color) < 0) {
        u32 palette = info->palette[index];
        *color = graphics->ConvertRGBValueToNative16Bit(
            static_cast<u8>(palette >> 16), static_cast<u8>(palette >> 8), static_cast<u8>(palette));
      }
      BdStoreDisplayPixel(target, *color);
      target += 2;
    }
    if (y + 1 < height)
      row += step;
  }
  return true;
}

// Convert continuous BGRA source pixels through the live display RGB method, ignoring source alpha and decoded pitch.
bool BdImageLoader::CopyColor(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u32 pitch = (out->GetColorSurfaceStride() >> 1) * 2;
  u8 *row = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = pixels->pixels;
  for (i32 y = 0; y < height; ++y) {
    u8 *target = row;
    for (i32 x = 0; x < width; ++x) {
      u8 blue = source[0], green = source[1], red = source[2];
      source += 4;
      BdStoreDisplayPixel(target, graphics->ConvertRGBValueToNative16Bit(red, green, blue));
      target += 2;
    }
    if (y + 1 < height)
      row += pitch;
  }
  return true;
}

// Split continuous BGRA into display-color WORDs and independent alpha bytes, preserving each output's own row pitch.
// Only the color stride is truncated to an even byte count; the genuine null-input rejection remains part of this method.
bool BdImageLoader::CopyMasked(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *color, GxBuffer *mask) {
  if (!color || !mask || !info || !pixels)
    return false;
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u32 color_pitch = (color->GetColorSurfaceStride() >> 1) * 2, mask_pitch = mask->GetColorSurfaceStride();
  u8 *color_row = static_cast<u8 *>(color->GetColorSurfaceBits()),
     *mask_row = static_cast<u8 *>(mask->GetColorSurfaceBits());
  const u8 *source = pixels->pixels;
  for (i32 y = 0; y < height; ++y) {
    u8 *target = color_row;
    for (i32 x = 0; x < width; ++x) {
      u8 blue = source[0], green = source[1], red = source[2], alpha = source[3];
      source += 4;
      BdStoreDisplayPixel(target, graphics->ConvertRGBValueToNative16Bit(red, green, blue));
      target += 2;
      mask_row[x] = alpha;
    }
    if (y + 1 < height) {
      color_row += color_pitch;
      mask_row += mask_pitch;
    }
  }
  return true;
}

// Select the original indexed or BGRA conversion only for a16-bit destination, preserving genuine metadata/pixel checks.
bool BdImageLoader::CopyImage(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  if (!info || !pixels)
    return false;
  i32 bits = out->GetBitsPerPixel();
  if (info->bits == 8 && bits == 16)
    return CopyIndexed(info, pixels, out);
  if (info->bits == 32 && bits == 16)
    return CopyColor(info, pixels, out);
  return false;
}

// Copy packed WORD rows to the locked buffer, respecting its pitch. Convert the complete buffer after unlocking
// when the original format query differs, and preserve the original lock failure byte.
u8 BD_STDCALL BdDecodeRaw(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height();
  u32 span = width * 2;
  for (u32 row = height; row; --row) {
    memcpy(pixels + destination, source, span);
    destination += pitch;
    source += span;
  }
  target->Unlock(GxLockFlags::Bits);
  if (different)
    target->Convert16BitBufferFromFormatToFormat(
        static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
  return 1;
}

// Decode byte-count/WORD packets across rows, converting each packet color before its stores. Zero-count packets
// still perform conversion, and a row's first packet is consumed even when its width is zero.
u8 BD_STDCALL BdDecodeRowRuns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), span = width * 2;
  for (u32 row = height; row; --row) {
    usize end = destination + span;
    do {
      u8 count = source[0];
      u16 pixel;
      memcpy(&pixel, source + 1, sizeof(pixel));
      source += 3;
      if (different)
        target->Convert16BitValueFromFormatToFormat(
            &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
      for (u32 left = count; left; --left) {
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += 2;
      }
    } while (destination < end);
    destination += (isize)pitch - span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Decode the same packets down columns using pitch-sized steps. Byte offsets keep unused endpoints defined while
// preserving packet overshoot and the first packet when the computed height span is zero.
u8 BD_STDCALL BdDecodeColumnRuns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(),
      span = pitch * height;
  for (u32 column = width; column; --column) {
    usize end = destination + span;
    do {
      u8 count = source[0];
      u16 pixel;
      memcpy(&pixel, source + 1, sizeof(pixel));
      source += 3;
      if (different)
        target->Convert16BitValueFromFormatToFormat(
            &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
      for (u32 left = count; left; --left) {
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += pitch;
      }
    } while (destination < end);
    destination += 2 - (isize)span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

BdImageIo::BdImageIo() : references(0), graphics(nullptr) {
  graphics = BdGetGraphics();
}

BdImageIo::~BdImageIo() {
}

// Publish and retain the loader identity for base or image-loader requests, clearing rejected outputs.
bool BdImageIo::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, BdImageIo);
  HANDLE_QUERY_INTERFACE(IID_GxGimexImage, BdImageIo);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object before retaining its actual DLL director, then return the current object count.
u32 BdImageIo::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Remove the director reference before decrementing a nonzero object count; enabled zero also destroys the loader.
u32 BdImageIo::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

// Construct the actual ordinary-member codec, retaining accepted QI or deleting a rejected zero-reference object.
bool BdImageIo::CreateObject(u32 iid, void **out) {
  BdLegacyCodec *codec = new BdLegacyCodec;
  if (codec->QueryInterface(iid, out))
    return true;
  delete codec;
  return false;
}

// Read the first image into the caller-selected display format. Missing metadata preserves the original state/file leaks.
// An absent output releases the resulting buffer through the original virtual operation instead of retaining it.
bool BdImageIo::LoadImage(const ScStringView *path, GxBuffer **out, u32 mode, i32 format, i32 bits) {
  BdCodecState *state = nullptr;
  ScString name(*path);
  GxBuffer *buffer = nullptr;
  bool result = graphics->CreateBuffer(&buffer);
  if (result) {
    BdCodecFile *file = BdOpenCodec(name.begin, nullptr);
    result = file != nullptr;
    if (result && BdCanReadCodec(file)) {
      BdReadCodecInfo(&state, file, name.begin);
      BdCodecInfo *info = BdCodecInfoAt(state, 0);
      if (info) {
        BdCodecPixels *pixels = BdDecodeImage(name.begin, 0);
        GxColorType requested(static_cast<GxColorType::Color>(format));
        requested.depth = static_cast<u32>(bits);
        result = buffer->Init(info->width, info->height, requested, nullptr, GxDepthType(GxDepthType::Depth::None),
            static_cast<GxVramRequest>(mode), false);
        if (result && buffer->Lock(GxLockFlags::Bits)) {
          result = CopyImage(info, pixels, buffer);
          buffer->Unlock(GxLockFlags::Bits);
        }
        BdDestroyCodecPixels(pixels);
        BdCloseCodecState(state);
        BdCloseCodecFile(file);
        BdFreeCodec(info);
      } else {
        result = false;
      }
    }
  }
  if (!result && buffer) {
    buffer->Release();
    buffer = nullptr;
  }
  if (out)
    *out = buffer;
  else if (buffer)
    buffer->Release();
  return result;
}

// Construct the real zero-reference image-I/O object registered by the child director.
ScUnknown *BdCreateClassA2FFB5D3() {
  return new BdImageIo;
}

static void BdStoreIoDisplayPixel(u8 *out, u32 pixel) {
  out[0] = static_cast<u8>(pixel);
  out[1] = static_cast<u8>(pixel >> 8);
}

// Convert indexed source bytes through a256-entry lazy display-color cache, retaining the BMP-only vertical reversal.
// The decoded source is consumed continuously; destination pitch is truncated to an even byte count.
bool BdImageIo::CopyIndexed(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  u32 colors[256];
  for (u32 &color : colors)
    color = kGxUnconvertedPaletteEntry;
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u32 pitch = (out->GetColorSurfaceStride() >> 1) * 2;
  u8 *row = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = pixels->pixels;
  isize step = pitch;
  if (info->signature == kGxBitmapCodecSignature && height > 0) {
    row += static_cast<usize>(pitch) * (height - 1);
    step = -static_cast<isize>(pitch);
  }
  for (i32 y = 0; y < height; ++y) {
    u8 *target = row;
    for (i32 x = 0; x < width; ++x) {
      u8 index = *source++;
      u32 *color = colors + index;
      if (static_cast<i32>(*color) < 0) {
        u32 palette = info->palette[index];
        *color = graphics->ConvertRGBValueToNative16Bit(
            static_cast<u8>(palette >> 16), static_cast<u8>(palette >> 8), static_cast<u8>(palette));
      }
      BdStoreIoDisplayPixel(target, *color);
      target += 2;
    }
    if (y + 1 < height)
      row += step;
  }
  return true;
}

// Convert continuous BGRA source pixels through the live display RGB method, ignoring source alpha and decoded pitch.
bool BdImageIo::CopyColor(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u32 pitch = (out->GetColorSurfaceStride() >> 1) * 2;
  u8 *row = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = pixels->pixels;
  for (i32 y = 0; y < height; ++y) {
    u8 *target = row;
    for (i32 x = 0; x < width; ++x) {
      u8 blue = source[0], green = source[1], red = source[2];
      source += 4;
      BdStoreIoDisplayPixel(target, graphics->ConvertRGBValueToNative16Bit(red, green, blue));
      target += 2;
    }
    if (y + 1 < height)
      row += pitch;
  }
  return true;
}

// Select the original indexed or BGRA conversion only for a16-bit destination, preserving genuine metadata/pixel checks.
bool BdImageIo::CopyImage(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  if (!info || !pixels)
    return false;
  i32 bits = out->GetBitsPerPixel();
  if (info->bits == 8 && bits == 16)
    return CopyIndexed(info, pixels, out);
  if (info->bits == 32 && bits == 16)
    return CopyColor(info, pixels, out);
  return false;
}

// Search the owned string's complete byte range for one literal four-byte image suffix.
static bool BdHasIoName(const ScString &name, const char *text) {
  usize length = static_cast<usize>(name.end - name.begin);
  if (length < 4)
    return false;
  for (usize i = 0; i <= length - 4; ++i)
    if (!memcmp(name.begin + i, text, 4))
      return true;
  return false;
}

// Read the source's top-left rectangle through real16-bit pixel/unpack operations into continuous BGRA with zero alpha.
// Bounds affect dimensions only; this original method does not offset reads by the save rectangle's origin.
static bool BD_STDCALL BdReadIoColor(GxBuffer *source, const BdCodecInfo *info, BdCodecPixels *pixels) {
  if (source->GetBitsPerPixel() != 16)
    return false;
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u8 *out = pixels->pixels;
  for (i32 y = 0; y < height; ++y) {
    for (i32 x = 0; x < width; ++x) {
      u32 pixel = source->GetPixel(x, y);
      u8 red, green, blue;
      source->Convert16BitNativeValueToRGB(static_cast<u16>(pixel), &red, &green, &blue);
      out[0] = blue;
      out[1] = green;
      out[2] = red;
      out[3] = 0;
      out += 4;
    }
  }
  return true;
}

// Reject absent metadata or decoded records before querying the actual source depth.
// Preserve the dispatcher's and converter's separate16-bit queries.
bool BD_STDCALL BdReadIoPixels(GxBuffer *source, const BdCodecInfo *info, BdCodecPixels *pixels) {
  if (info && pixels && source->GetBitsPerPixel() == 16)
    return BdReadIoColor(source, info, pixels);
  return false;
}

// Copy the source's actual area and forward it through the virtual rectangle-saving method.
// The borrowed area is read once and the rectangle copy stays alive through that call.
bool BdImageIo::SaveImage(const ScStringView *path, GxBuffer *source, u32 quality) {
  const ScRect *area = source->GetBufferArea();
  BdRect bounds = {area->left, area->top, area->right, area->bottom};
  return SaveImage(path, source, &bounds, quality);
}

// Select BMP/JPEG from literal lower/uppercase substrings and write the rectangle-sized top-left source pixels.
bool BdImageIo::SaveImage(const ScStringView *path, GxBuffer *source, const BdRect *bounds, u32 quality) {
  ScString name(*path);
  i32 type = 2;
  if (BdHasIoName(name, ".jpg") || BdHasIoName(name, ".JPG"))
    type = 0;
  else if (BdHasIoName(name, ".bmp") || BdHasIoName(name, ".BMP"))
    type = 1;
  BdCodecState *state = nullptr;
  BdCodecInfo *info = static_cast<BdCodecInfo *>(BdAllocateCodec(3468));
  memset(info, 0, 3468);
  if (type == 2) {
    BdFreeCodec(info);
    return false;
  }
  info->signature = type == 0 ? kGxJpegSignature : kGxBitmapImageSignature;
  info->length = 3468;
  info->version = 200;
  info->width = static_cast<u32>(bounds->right) - static_cast<u32>(bounds->left);
  info->height = static_cast<u32>(bounds->bottom) - static_cast<u32>(bounds->top);
  if (info->width > INT32_MAX / 4 || (info->height && info->width > INT32_MAX / 4 / info->height)) {
    BdFreeCodec(info);
    return false;
  }
  info->compressed = 1;
  info->field_430 = quality;
  info->red_bits = info->green_bits = info->blue_bits = 8;
  info->bits = info->source_bits = 32;
  if (!source->Lock(GxLockFlags::Bits))
    return false;
  BdCodecPixels pixels = {
      info, static_cast<u8 *>(BdAllocateCodec(static_cast<i32>(4u * info->width * info->height))), 4u * info->width};
  bool copied = BdReadIoPixels(source, info, &pixels);
  source->Unlock(GxLockFlags::Bits);
  if (copied) {
    BdSelectCodecExtension(type == 0 ? "JPG" : "BMP", nullptr);
    BdCodecFile *file = BdCreateCodecFile(name.begin);
    if (file) {
      BdOpenCodecWriter(&state, file, name.begin, 1);
      BdWriteCodecPixels(state, info, pixels.pixels, static_cast<i32>(info->width * info->bits) / 8);
      BdCloseCodecWriter(state);
      BdCloseCodecFile(file);
    }
    BdFreeCodec(info);
    BdFreeCodec(pixels.pixels);
  }
  return true;
}

// Copy the actual source area once and dispatch its bounds through the virtual black-region operation.
bool BdImageIo::IsBlack(GxBuffer *source) {
  const ScRect *area = source->GetBufferArea();
  BdRect bounds = {area->left, area->top, area->right, area->bottom};
  return IsBlack(source, &bounds);
}

// Reject out-of-buffer inclusive bounds and non16-bit sources, then compare every locked pixel with display black.
// A failed source lock retains the original successful result; the bounds are not normalized or made half-open.
bool BdImageIo::IsBlack(GxBuffer *source, const BdRect *bounds) {
  if (bounds->left < 0 || bounds->right >= (i32)source->Width() || bounds->top < 0 ||
      bounds->bottom >= (i32)source->Height())
    return false;
  if (source->GetBitsPerPixel() != 16)
    return false;
  u16 black = source->ConvertRGBValueToNative16Bit(0, 0, 0);
  bool result = true;
  if (source->Lock(GxLockFlags::Bits)) {
    for (i32 y = bounds->top; y <= bounds->bottom; ++y) {
      for (i32 x = bounds->left; x <= bounds->right; ++x) {
        if (source->GetPixel(x, y) != black) {
          result = false;
          break;
        }
      }
      if (!result)
        break;
    }
    source->Unlock(GxLockFlags::Bits);
  }
  return result;
}

static void BdDiscardRejectedImage(BdCodecFile *file, BdCodecState *state) {
  BdCloseCodecState(state);
  BdCloseCodecFile(file);
}

BdImageLoader::BdImageLoader() : references(0), graphics(nullptr) {
  graphics = BdGetGraphics();
}

BdImageLoader::~BdImageLoader() {
}

// Publish and retain the loader identity for base or image-loader requests, clearing rejected outputs.
bool BdImageLoader::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, BdImageLoader);
  HANDLE_QUERY_INTERFACE(IID_BdImageLoader, BdImageLoader);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object before retaining its actual DLL director, then return the current object count.
u32 BdImageLoader::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Remove the director reference before decrementing a nonzero object count; enabled zero also destroys the loader.
u32 BdImageLoader::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

// Read the first codec image into a square power-of-two BGRA buffer while publishing the original uncropped bounds.
bool BdImageLoader::LoadTexture(const ScStringView *path, GxBuffer **out, BdRect *bounds) {
  BdCodecState *state = nullptr;
  ScString name(*path);
  GxBuffer *buffer = nullptr;
  bool result = graphics->CreateBuffer(&buffer);
  if (result) {
    BdCodecFile *file = BdOpenCodec(name.begin, nullptr);
    result = file != nullptr;
    if (result && BdCanReadCodec(file)) {
      BdReadCodecInfo(&state, file, name.begin);
      BdCodecInfo *info = state ? BdCodecInfoAt(state, 0) : nullptr;
      if (!info) {
        BdDiscardRejectedImage(file, state);
        buffer->Release();
        *out = nullptr;
        return false;
      }
      BdCodecPixels *pixels = BdDecodeImage(name.begin, 0);
      *bounds = {0, 0, static_cast<i32>(info->width), static_cast<i32>(info->height)};
      u32 maximum = static_cast<i32>(info->width) < static_cast<i32>(info->height) ? info->height : info->width;
      u32 exponent = 0;
      if (maximum > 1) {
        do {
          ++exponent;
        } while ((maximum >> exponent) > 1);
      }
      if (static_cast<f64>(maximum) / static_cast<f64>(static_cast<i32>(1u << exponent)) > 1.0)
        ++exponent;
      u32 side = static_cast<u16>(1u << exponent);
      result = buffer->Init(side, side, GxColorType(static_cast<GxColorType::Color>(14)), nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, true);
      if (result) {
        result = buffer->Lock(GxLockFlags::Bits) != 0;
        if (result) {
          BdCopyTexturePixels(info, pixels, buffer);
          buffer->Unlock(GxLockFlags::Bits);
        }
      }
      BdDestroyCodecPixels(pixels);
      BdCloseCodecState(state);
      BdCloseCodecFile(file);
      BdFreeCodec(info);
    }
  }
  if (!result && buffer) {
    buffer->Release();
    buffer = nullptr;
  }
  *out = buffer;
  return result;
}

// Load the first decoded image into the caller-selected buffer format and mode, then apply its original16-bit conversion.
// A failed lock preserves successful initialization; the original non-readable-file branch retains its open file and buffer.
bool BdImageLoader::LoadImage(const ScStringView *path, GxBuffer **out, u32 mode, i32 format, i32 bits) {
  BdCodecState *state = nullptr;
  ScString name(*path);
  GxBuffer *buffer = nullptr;
  bool result = graphics->CreateBuffer(&buffer);
  if (result) {
    BdCodecFile *file = BdOpenCodec(name.begin, nullptr);
    result = file != nullptr;
    if (result && BdCanReadCodec(file)) {
      BdReadCodecInfo(&state, file, name.begin);
      BdCodecInfo *info = state ? BdCodecInfoAt(state, 0) : nullptr;
      if (!info) {
        BdDiscardRejectedImage(file, state);
        buffer->Release();
        *out = nullptr;
        return false;
      }
      BdCodecPixels *pixels = BdDecodeImage(name.begin, 0);
      GxColorType requested(static_cast<GxColorType::Color>(format));
      requested.depth = static_cast<u32>(bits);
      result = buffer->Init(info->width, info->height, requested, nullptr, GxDepthType(GxDepthType::Depth::None),
          static_cast<GxVramRequest>(mode), false);
      if (result && buffer->Lock(GxLockFlags::Bits)) {
        result = CopyImage(info, pixels, buffer);
        buffer->Unlock(GxLockFlags::Bits);
      }
      BdDestroyCodecPixels(pixels);
      BdCloseCodecState(state);
      BdCloseCodecFile(file);
      BdFreeCodec(info);
    }
  }
  if (!result && buffer) {
    buffer->Release();
    buffer = nullptr;
  }
  *out = buffer;
  return result;
}

// Load separate16-bit color and8-bit alpha buffers, retaining independent create/init/lock results and partial outputs.
// The original alpha unlock follows alpha initialization success even when color initialization failed and no lock occurred.
bool BdImageLoader::LoadMasked(const ScStringView *path, GxBuffer **color, GxBuffer **mask) {
  BdCodecFile *file = nullptr;
  BdCodecState *state = nullptr;
  ScString name(*path);
  GxBuffer *color_buffer = nullptr, *mask_buffer = nullptr;
  bool color_result = graphics->CreateBuffer(&color_buffer), mask_result = graphics->CreateBuffer(&mask_buffer);
  if (color_result) {
    if (mask_result) {
      file = BdOpenCodec(name.begin, nullptr);
      color_result = file != nullptr;
    }
    if (color_result && mask_result && BdCanReadCodec(file)) {
      BdReadCodecInfo(&state, file, name.begin);
      BdCodecInfo *info = state ? BdCodecInfoAt(state, 0) : nullptr;
      if (!info) {
        BdDiscardRejectedImage(file, state);
        color_buffer->Release();
        mask_buffer->Release();
        *color = nullptr;
        *mask = nullptr;
        return false;
      }
      BdCodecPixels *pixels = BdDecodeImage(name.begin, 0);
      bool initialized = color_buffer->Init(info->width, info->height, GxColorType(static_cast<GxColorType::Color>(4)),
          nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
      mask_result = mask_buffer->Init(info->width, info->height, GxColorType(static_cast<GxColorType::Color>(3)),
          nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
      if (initialized && mask_result) {
        color_result = color_buffer->Lock(GxLockFlags::Bits) != 0;
        mask_result = mask_buffer->Lock(GxLockFlags::Bits) != 0;
        if (color_result) {
          if (mask_result)
            CopyMasked(info, pixels, color_buffer, mask_buffer);
          color_buffer->Unlock(GxLockFlags::Bits);
        }
      } else
        color_result = false;
      if (mask_result)
        mask_buffer->Unlock(GxLockFlags::Bits);
      BdDestroyCodecPixels(pixels);
      BdCloseCodecState(state);
      BdCloseCodecFile(file);
      BdFreeCodec(info);
    }
  }
  if (!color_result && color_buffer) {
    color_buffer->Release();
    color_buffer = nullptr;
  }
  if (!mask_result && mask_buffer) {
    mask_buffer->Release();
    mask_buffer = nullptr;
  }
  *color = color_buffer;
  *mask = mask_buffer;
  return color_result && mask_result;
}

// Construct the actual codec wrapper and return its requested identity. Rejection deletes the unretained object.
bool BdImageLoader::CreateObject(u32 iid, void **out) {
  BdCodec *codec = new BdCodec;
  if (codec->QueryInterface(iid, out))
    return true;
  delete codec;
  return false;
}

// Copy the original decoded BGRA stream into the destination, truncating its byte pitch to a whole DWORD stride.
// The input advances continuously across rows; the decoded record's own pitch is deliberately not consulted.
u8 BD_STDCALL BdCopyTexturePixels(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out) {
  if (!out || !info || !pixels)
    return 0;
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u32 pitch = (out->GetColorSurfaceStride() >> 2) * 4;
  u8 *destination = static_cast<u8 *>(out->GetColorSurfaceBits());
  const u8 *source = pixels->pixels;
  for (i32 y = 0; y < height; ++y) {
    u8 *row = destination + static_cast<usize>(pitch) * static_cast<u32>(y);
    for (i32 x = 0; x < width; ++x) {
      row[0] = source[0];
      row[1] = source[1];
      row[2] = source[2];
      row[3] = source[3];
      source += 4;
      row += 4;
    }
  }
  return 1;
}

// Acquire the image factory's options, set its mode and format, then resolve the requested image.
// Each queried factory and options interface is released in the original order.
BdImageResource::BdImageResource(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format)
    : buffer(nullptr) {
  RsManager *manager = BdGetResourceManager();
  RsFactoryInterface *factory = nullptr;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options = nullptr;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      Resolve(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

BdImageResource::~BdImageResource() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

bool BdImageResource::Resolve(RsManager *manager, u32 group, u32 instance) {
  RsKey key = {kRsImageResourceType, group, instance};
  return manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

// Search the owned string's complete byte range for one literal four-byte image suffix.
static bool BdHasImageName(const ScString &name, const char *text) {
  usize length = static_cast<usize>(name.end - name.begin);
  if (length < 4)
    return false;
  for (usize i = 0; i <= length - 4; ++i)
    if (!memcmp(name.begin + i, text, 4))
      return true;
  return false;
}

// Read the source's top-left rectangle through real16-bit pixel/unpack operations into continuous BGRA with zero alpha.
// Bounds affect dimensions only; this original method does not offset reads by the save rectangle's origin.
static bool BD_STDCALL BdReadBufferColor(GxBuffer *source, const BdCodecInfo *info, BdCodecPixels *pixels) {
  if (source->GetBitsPerPixel() != 16)
    return false;
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u8 *out = pixels->pixels;
  for (i32 y = 0; y < height; ++y) {
    for (i32 x = 0; x < width; ++x) {
      u32 pixel = source->GetPixel(x, y);
      u8 red, green, blue;
      source->Convert16BitNativeValueToRGB(static_cast<u16>(pixel), &red, &green, &blue);
      out[0] = blue;
      out[1] = green;
      out[2] = red;
      out[3] = 0;
      out += 4;
    }
  }
  return true;
}

// Reject absent metadata or decoded records before querying the actual source depth.
// Preserve the dispatcher's and converter's separate16-bit queries.
bool BD_STDCALL BdReadBufferPixels(GxBuffer *source, const BdCodecInfo *info, BdCodecPixels *pixels) {
  if (info && pixels && source->GetBitsPerPixel() == 16)
    return BdReadBufferColor(source, info, pixels);
  return false;
}

// Copy the source's actual area and forward it through the virtual rectangle-saving method.
// The borrowed area is read once and the rectangle copy stays alive through that call.
bool BdImageLoader::SaveImage(const ScStringView *path, GxBuffer *source) {
  const ScRect *area = source->GetBufferArea();
  BdRect bounds = {area->left, area->top, area->right, area->bottom};
  return SaveImage(path, source, &bounds);
}

// Select BMP/Targa from literal lower/uppercase substrings and write the rectangle-sized top-left source pixels.
bool BdImageLoader::SaveImage(const ScStringView *path, GxBuffer *source, const BdRect *bounds) {
  ScString name(*path);
  i32 type = 2;
  if (BdHasImageName(name, ".tga") || BdHasImageName(name, ".TGA"))
    type = 0;
  else if (BdHasImageName(name, ".bmp") || BdHasImageName(name, ".BMP"))
    type = 1;
  BdCodecState *state = nullptr;
  BdCodecInfo *info = static_cast<BdCodecInfo *>(BdAllocateCodec(3468));
  memset(info, 0, 3468);
  if (type == 2) {
    BdFreeCodec(info);
    return false;
  }
  info->signature = type == 0 ? kGxTargaImageSignature : kGxBitmapImageSignature;
  info->length = 3468;
  info->version = 200;
  info->width = static_cast<u32>(bounds->right) - static_cast<u32>(bounds->left);
  info->height = static_cast<u32>(bounds->bottom) - static_cast<u32>(bounds->top);
  if (info->width > INT32_MAX / 4 || (info->height && info->width > INT32_MAX / 4 / info->height)) {
    BdFreeCodec(info);
    return false;
  }
  info->compressed = 1;
  info->field_430 = 100;
  info->red_bits = info->green_bits = info->blue_bits = 8;
  info->bits = info->source_bits = 32;
  if (!source->Lock(GxLockFlags::Bits))
    return false;
  BdCodecPixels pixels = {
      info, static_cast<u8 *>(BdAllocateCodec(static_cast<i32>(4u * info->width * info->height))), 4u * info->width};
  bool copied = BdReadBufferPixels(source, info, &pixels);
  source->Unlock(GxLockFlags::Bits);
  if (copied) {
    BdSelectCodecExtension(type == 0 ? "TGA" : "BMP", nullptr);
    BdCodecFile *file = BdCreateCodecFile(name.begin);
    if (file) {
      BdOpenCodecWriter(&state, file, name.begin, 1);
      BdWriteCodecPixels(state, info, pixels.pixels, static_cast<i32>(info->width * info->bits) / 8);
      BdCloseCodecWriter(state);
      BdCloseCodecFile(file);
    }
    BdFreeCodec(info);
    BdFreeCodec(pixels.pixels);
  }
  return true;
}
