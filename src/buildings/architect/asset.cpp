#include "buildings/architect/asset.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "buildings/architect/api.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/image.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/file.h"

// Asset image hashes use the big-endian CRC-32 polynomial with an all-one initial state and no final XOR.
enum BdImageCrcField : u32 {
  kBdImageCrcTopBit = 0x80000000,
  kBdImageCrcPolynomial = 0x4c11db7,
  kBdImageCrcInitialValue = 0xffffffff,
};

BdCpuTexture::BdCpuTexture(u32 w, u32 h, bool has_alpha, bool is_rgb565, bool keyed)
    : pixels(new u16[static_cast<usize>(w) * h]), width(w), height(h), references(1), alpha(has_alpha),
      rgb565(is_rgb565), color_key(keyed) {
}

BdCpuTexture::~BdCpuTexture() {
  delete[] pixels;
}

void BdCpuTexture::AddRef() {
  ++references;
}

void BdCpuTexture::Release() {
  if (!--references)
    delete this;
}


// Construct empty asset strings and clear only the original constructor's defined scalar and pointer fields.
// Inverse dimensions and rectangles remain unwritten until image loading supplies them.
BdAsset::BdAsset() : image(nullptr), kind(-1), definition_id(0), flags(0), height(0), width(0) {
  field_080 = 0;
  surface = nullptr;
  texture = nullptr;
}

BdAsset::~BdAsset() {
}

// Publish the current texture view and retain it when a nonnull output is supplied.
// An empty asset clears the output; a null output performs no texture access or retain.
void BdAsset::GetTexture(BdAssetTexture **out) {
  if (out) {
    *out = nullptr;
    if (texture) {
      texture->AddRef();
      *out = texture;
    }
  }
}

i32 BdAsset::IsKind6Or7Or16() const {
  return kind == 6 || kind == 7 || kind == 16;
}

i32 BdAsset::IsKind8Through16() const {
  u8 value = static_cast<u8>(kind);
  return value >= 8 && value <= 16;
}

u32 BdCrcTable[256];

// Build the catalog's big-endian polynomial table before asset image keys are calculated.
u32 BdInitializeCrc() {
  u32 value = 0;
  for (u32 i = 0; i < 256; ++i) {
    value = i << 24;
    for (u32 bit = 0; bit < 8; ++bit)
      value = (value << 1) ^ ((value & kBdImageCrcTopBit) ? kBdImageCrcPolynomial : 0);
    BdCrcTable[i] = value;
  }
  return value;
}

// Hash all four bytes of each image pixel in row order, excluding row padding and applying no final XOR.
// Empty dimensions retain the initial all-one accumulator without touching pixel storage.
u32 BD_FASTCALL BdHashImage(const u8 *pixels, i32 width, i32 height, i32 pitch) {
  u32 crc = kBdImageCrcInitialValue;
  if (!width || !height)
    return crc;
  for (i32 rows = height; rows; --rows) {
    const u8 *pixel = pixels;
    for (i32 count = width; count; --count)
      for (u32 byte = 0; byte < 4; ++byte)
        crc = (crc << 8) ^ BdCrcTable[*pixel++ ^ (crc >> 24)];
    if (rows != 1)
      pixels += pitch;
  }
  return crc;
}

static i32 BdImageInteger(f32 value) {
  return static_cast<i32>(static_cast<i64>(value));
}


// Expand a 16-bit surface pixel to eight-bit RGB using the source's integer channel scaling.
// Preserve the integer division rather than replacing it with bit replication.
static u32 BdRenderSurfaceRgb(u16 pixel, bool rgb565) {
  u32 red = rgb565 ? pixel >> 11 : pixel >> 10;
  u32 green = rgb565 ? (pixel >> 5) & 63 : (pixel >> 5) & 31;
  u32 blue = pixel & 31;
  return ((255 * red / 31) << 16) | ((255 * green / (rgb565 ? 63 : 31)) << 8) | (255 * blue / 31);
}

static u16 BdRenderPackRgb(u32 color, bool rgb565) {
  u32 red = (color >> 16) & 255, green = (color >> 8) & 255, blue = color & 255;
  return static_cast<u16>(((red >> 3) << (rgb565 ? 11 : 10)) | ((green >> (rgb565 ? 2 : 3)) << 5) | (blue >> 3));
}

// Blend the source's four-bit ARGB channels with the requested background before packing to the display format.
// The source expands the alpha nibble by shifting, so its maximum value is 240 rather than 255.
static u16 BdRenderBlend4444(u16 pixel, u32 background, bool rgb565) {
  u32 alpha = (pixel >> 8) & 0xf0, inverse = 255 - alpha;
  f64 scale = static_cast<f64>(1.0f / 255.0f);
  f64 foreground = alpha * scale, back = inverse * scale;
  u32 red = static_cast<u32>(((pixel >> 4) & 0xf0) * foreground + ((background >> 16) & 255) * back);
  u32 green = static_cast<u32>((pixel & 0xf0) * foreground + ((background >> 8) & 255) * back);
  u32 blue = static_cast<u32>(((pixel << 4) & 0xf0) * foreground + (background & 255) * back);
  return BdRenderPackRgb((red << 16) | (green << 8) | blue, rgb565);
}

// Retain the source image for white backgrounds; otherwise convert the stored texels into a display-format buffer.
// The rectangle controls output size without offsetting the source; alpha and keyed pixels use the supplied background.
// The reference DirectDraw path retains its original lock retry and first-attempt busy cleanup behavior.
GxBuffer *BdAsset::RenderImage(const BdRect *area, u32 color) {
  if (image && color == kBdAssetWhiteRenderBackground) {
    image->AddRef();
    return image;
  }
  if (!surface)
    return nullptr;
  GxColorType format = BdGetGraphics()->GetScreenColorType();
  i32 code = static_cast<i32>(format.color);
  bool rgb565 = code != 5 && code != 10, keyed = (flags & 2) != 0, alpha = !(flags & 1) && surface->alpha;
  GxBuffer *result = nullptr;
  BdGetGraphics()->CreateBuffer(&result);
  if (!result)
    return nullptr;
  i32 left = area ? area->left : 0, top = area ? area->top : 0;
  i32 right = area ? area->right : static_cast<i32>(surface->width);
  i32 bottom = area ? area->bottom : static_cast<i32>(surface->height);
  i32 pixel_width = right - left, pixel_height = bottom - top;
  if (!result->Init(pixel_width, pixel_height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
          GxVramRequest::VideoMemoryLowPriority, false)) {
    result->Release();
    return nullptr;
  }
  if (!result->Lock(GxLockFlags::Bits))
    return nullptr;
  u8 *destination = static_cast<u8 *>(result->GetColorSurfaceBits());
  if (pixel_width > 0 && pixel_height > 0 && !destination) {
    result->Unlock(GxLockFlags::Bits);
    result->Release();
    return nullptr;
  }
  i32 destination_pitch = static_cast<i32>(result->GetColorSurfaceStride());
  u16 key = surface->rgb565 ? 0xf81f : 0x7c1f;
  // The original rectangle sets output dimensions; its left/top do not offset the source pixels.
  for (i32 y = 0; y < pixel_height && pixel_width > 0; ++y) {
    const u16 *in = surface->pixels + static_cast<usize>(y) * surface->width;
    u8 *out = destination + static_cast<isize>(y) * destination_pitch;
    for (i32 x = 0; x < pixel_width; ++x) {
      u16 pixel = in[x], converted;
      if (alpha)
        converted = BdRenderBlend4444(pixel, color, rgb565);
      else if (keyed)
        converted = BdRenderPackRgb(pixel == key ? color : BdRenderSurfaceRgb(pixel, surface->rgb565), rgb565);
      else
        converted = pixel;
      memcpy(out + 2 * static_cast<usize>(x), &converted, sizeof(converted));
    }
  }
  result->Unlock(GxLockFlags::Bits);
  return result;
}

// Upload a nonempty source image to a sixteen-bit texture, preserving lock, bounds and ownership order.
void BdAsset::SetImage(GxBuffer *source) {
  [[maybe_unused]] bool color_key = (flags & 2) != 0, all_opaque = true, alpha = false;
  if (!source) {
    ScRelease(&image);
    if (surface) {
      surface->Release();
      surface = nullptr;
    }
    if (texture) {
      texture->Release();
      texture = nullptr;
    }
    return;
  }
  if (!source->Width())
    return;
  if (!source->Height())
    return;
  width = static_cast<f32>(source->Width());
  height = static_cast<f32>(source->Height());
  opaque_bounds = {0, BdImageInteger(width), 0, BdImageInteger(height)};
  inverse_width = 1.0f / width;
  inverse_height = 1.0f / height;
  i32 code = static_cast<i32>(BdGetGraphics()->GetScreenColorType().color);
  [[maybe_unused]] bool rgb565;
  if (code == 4 || code == 7)
    rgb565 = true;
  else if (code == 5 || code == 10)
    rgb565 = false;
  else
    return;
  if (source->Lock(GxLockFlags::Bits)) {
    const u8 *pixels = static_cast<u8 *>(source->GetColorSurfaceBits());
    u32 pitch = source->GetColorSurfaceStride();
    definition_id = BdHashImage(pixels, BdImageInteger(width), BdImageInteger(height), static_cast<i32>(pitch));
    const u8 *opacity = static_cast<u8 *>(source->GetColorSurfaceBits()) + 3;
    pixels = static_cast<u8 *>(source->GetColorSurfaceBits());
    u32 left = static_cast<u32>(BdImageInteger(width)), top = static_cast<u32>(BdImageInteger(height)), right = 0,
        bottom = 0;
    // The stock opacity scan is contiguous; only CRC and texture conversion use the supplied row pitch.
    for (u32 y = 0; static_cast<f64>(static_cast<i32>(y)) < height; ++y) {
      for (u32 x = 0; static_cast<f64>(static_cast<i32>(x)) < width; ++x) {
        bool transparent;
        if (color_key) {
          transparent = pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 255 && pixels[3] == 255;
        } else {
          transparent = *opacity != 255;
          if (transparent)
            alpha = true;
        }
        if (transparent) {
          all_opaque = false;
        } else {
          if (x < left)
            left = x;
          else if (x > right)
            right = x;
          if (y < top)
            top = y;
          else if (y > bottom)
            bottom = y;
        }
        pixels += 4;
        opacity += 4;
      }
    }
    if (all_opaque)
      flags |= 1;
    opaque_bounds = {static_cast<i32>(left), static_cast<i32>(top), static_cast<i32>(right), static_cast<i32>(bottom)};
    source->Unlock(GxLockFlags::Bits);
  }
  u32 tex_width = static_cast<u32>(BdImageInteger(width)), tex_height = static_cast<u32>(BdImageInteger(height));
  if (static_cast<usize>(tex_width) > static_cast<usize>(-1) / sizeof(u16) / tex_height)
    ScFatal(SC_ERROR_INVALID, "Architect texture dimensions overflow");
  BdCpuTexture *converted = new BdCpuTexture(tex_width, tex_height, alpha, rgb565, color_key);
  if (!source->Lock(GxLockFlags::Bits)) {
    converted->Release();
    return;
  }
  u8 *input = static_cast<u8 *>(source->GetColorSurfaceBits());
  i32 source_pitch = static_cast<i32>(source->GetColorSurfaceStride());
  i32 target_pitch = static_cast<i32>(tex_width * sizeof(u16));
  if (alpha)
    BdConvert4444(reinterpret_cast<u8 *>(converted->pixels), input, target_pitch, source_pitch, tex_width, tex_height);
  else if (rgb565)
    BdConvert565(reinterpret_cast<u8 *>(converted->pixels), input, target_pitch, source_pitch, tex_width, tex_height);
  else
    BdConvert555(reinterpret_cast<u8 *>(converted->pixels), input, target_pitch, source_pitch, tex_width, tex_height);
  source->Unlock(GxLockFlags::Blt);
  surface = converted;
  converted->AddRef();
  texture = converted;
  if (kind == 6 || kind == 16 || kind == 7) {
    ScRelease(&image);
    source->AddRef();
    image = source;
  }
}

// Load a named asset lazily through the real architect image-loader interface after its original kind/path/texture checks.
// Preserve full icon bounds and the two distinct preview-cropping rules before image upload and buffer release.
void BdLoadAsset(const ScString *name, BdCatalog *catalog, BdAssetLoadContext *) {
  BdAsset *asset = nullptr;
  if (!catalog->FindAsset(name, &asset))
    return;
  if (asset->kind == 3)
    return;
  if (asset->kind == 6 || asset->kind == 16 || asset->kind == 7) {
    ScString empty("", 0);
    if (asset->path.Strlen() == empty.Strlen())
      return;
  }
  {
    ScString empty("", 0);
    if (asset->path.Strlen() == empty.Strlen())
      return;
  }
  if (!RsFile::FileExists(asset->path))
    return;
  BdAssetTexture *texture = nullptr;
  asset->GetTexture(&texture);
  if (texture) {
    texture->Release();
    return;
  }
  BdImageLoader *loader = BdGetImageLoader();
  GxBuffer *image = nullptr;
  BdRect bounds;
  u8 kind = static_cast<u8>(asset->kind);
  if (loader->LoadTexture(&asset->path, &image, &bounds)) {
    BdRect preview;
    if (kind <= 2 || kind == 6) {
      preview = {bounds.left, bounds.bottom - 32, bounds.left + 32, bounds.bottom};
      if (bounds.bottom - 32 < 0 || bounds.left + 32 > bounds.right)
        preview = bounds;
    } else {
      preview = bounds;
      if (bounds.right - bounds.left > 156)
        preview.right = 156;
      if (preview.bottom - preview.top > 156)
        preview.bottom = 156;
    }
    asset->icon_bounds = bounds;
    asset->preview_bounds = preview;
    asset->SetImage(image);
    image->Release();
  }
}

// Convert BGRA bytes to little-endian RGB555 words, traversing both images from their final row toward the first.
// Direct shifts replace only the original constant channel tables; source alpha and row padding are ignored.
void BdConvert555(u8 *destination, const u8 *source, i32 destination_pitch, i32 source_pitch, u32 width, u32 height) {
  for (u32 y = height; y; --y) {
    u8 *out = destination + static_cast<isize>(destination_pitch) * static_cast<isize>(y - 1);
    const u8 *in = source + static_cast<isize>(source_pitch) * static_cast<isize>(y - 1);
    for (u32 x = 0; x < width; ++x) {
      u16 pixel = static_cast<u16>((in[0] >> 3) | ((in[1] >> 3) << 5) | ((in[2] >> 3) << 10));
      out[0] = static_cast<u8>(pixel);
      out[1] = static_cast<u8>(pixel >> 8);
      in += 4;
      out += 2;
    }
  }
}

// Convert BGRA bytes to little-endian RGB565 words in original forward row order.
// Keep source and destination pitch independent and leave every padding byte untouched.
void BdConvert565(u8 *destination, const u8 *source, i32 destination_pitch, i32 source_pitch, u32 width, u32 height) {
  for (u32 y = 0; y < height; ++y) {
    u8 *out = destination + static_cast<isize>(destination_pitch) * static_cast<isize>(y);
    const u8 *in = source + static_cast<isize>(source_pitch) * static_cast<isize>(y);
    for (u32 x = 0; x < width; ++x) {
      u16 pixel = static_cast<u16>((in[0] >> 3) | ((in[1] >> 2) << 5) | ((in[2] >> 3) << 11));
      out[0] = static_cast<u8>(pixel);
      out[1] = static_cast<u8>(pixel >> 8);
      in += 4;
      out += 2;
    }
  }
}

// Truncate each component exactly as the original channel-table lookup does.
void BdConvert4444(u8 *destination, const u8 *source, i32 destination_pitch, i32 source_pitch, u32 width, u32 height) {
  for (u32 y = 0; y < height; ++y) {
    u8 *out = destination + static_cast<isize>(destination_pitch) * static_cast<isize>(y);
    const u8 *in = source + static_cast<isize>(source_pitch) * static_cast<isize>(y);
    for (u32 x = 0; x < width; ++x) {
      u16 pixel = static_cast<u16>((in[0] >> 4) | ((in[1] >> 4) << 4) | ((in[2] >> 4) << 8) | ((in[3] >> 4) << 12));
      out[0] = static_cast<u8>(pixel);
      out[1] = static_cast<u8>(pixel >> 8);
      in += 4;
      out += 2;
    }
  }
}

// Check a vertical run through the buffer's actual transparency operation.
// An empty run is transparent and the first opaque pixel ends the scan.
bool BdTransparentColumn(GxBuffer *buffer, i32 x, i32 top, i32 height) {
  bool transparent = true;
  for (i32 y = top; y < top + height && transparent; ++y)
    transparent = buffer->IsPixelTransparent(static_cast<u32>(x), static_cast<u32>(y));
  return transparent;
}

// Check a horizontal run through the buffer's actual transparency operation.
// An empty run is transparent and the first opaque pixel ends the scan.
bool BdTransparentRow(GxBuffer *buffer, i32 y, i32 left, i32 width) {
  bool transparent = true;
  for (i32 x = left; x < left + width && transparent; ++x)
    transparent = buffer->IsPixelTransparent(static_cast<u32>(x), static_cast<u32>(y));
  return transparent;
}

// Find the nontransparent image bounds by trimming columns before rows while the buffer is locked.
// A failed lock or a completely transparent dimension retains that dimension's original extent.
BdRect *BdFindImageBounds(GxBuffer *buffer, BdRect *out) {
  i32 left = 0, top = 0;
  const ScRect *area = buffer->GetBufferArea();
  i32 width = area->right - area->left;
  area = buffer->GetBufferArea();
  i32 height = area->bottom - area->top;
  if (buffer->Lock(GxLockFlags::Bits)) {
    i32 first = 0;
    while (first < width && BdTransparentColumn(buffer, first, 0, height))
      ++first;
    if (first < width) {
      width -= first;
      left = first;
    }
    i32 last = left + width - 1;
    while (last >= left && BdTransparentColumn(buffer, last, 0, height))
      --last;
    if (last >= left)
      width = last - left + 1;
    first = 0;
    while (first < height && BdTransparentRow(buffer, first, left, width))
      ++first;
    if (first < height) {
      height -= first;
      top = first;
    }
    last = top + height - 1;
    while (last >= top && BdTransparentRow(buffer, last, left, width))
      --last;
    if (last >= top)
      height = last - top + 1;
    buffer->Unlock(GxLockFlags::Bits);
  }
  *out = {left, top, left + width, top + height};
  return out;
}

static u16 BdPackThumbnailPixel(u32 pixel, bool six_green_bits) {
  u32 blue = pixel & 255, green = (pixel >> 8) & 255, red = (pixel >> 16) & 255;
  if (six_green_bits)
    return static_cast<u16>((blue >> 3) | ((green & 0xfc) << 3) | ((red & 0xf8) << 8));
  return static_cast<u16>((blue >> 3) | ((green & 0xf8) << 2) | ((red & 0xf8) << 7));
}

// Create the original format-7 buffer and convert its source pixels while both buffers are locked.
// Preserve the source color key and leave target ownership unchanged on the original failed-init paths.
static void BdConvertThumbnail(GxBuffer *source, GxBuffer **out, bool six_green_bits) {
  if (!source || !out)
    return;
  *out = nullptr;
  i32 width = source->Width(), height = source->Height();
  bool has_key = source->GetTransparency();
  u32 source_key = 0;
  if (has_key)
    source->GetTransparentColor(&source_key);
  GxBuffer *target = nullptr;
  BdGetGraphics()->CreateBuffer(&target);
  if (!target)
    return;
  if (!target->Init(static_cast<u32>(width), static_cast<u32>(height), GxColorType(static_cast<GxColorType::Color>(7)),
          nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::VideoMemoryLowPriority, false))
    return;
  u16 target_key = 0;
  if (has_key) {
    target_key = BdPackThumbnailPixel(source_key, six_green_bits);
    target->SetTransparency(target_key);
  }
  u8 source_locked = source->Lock(GxLockFlags::Bits), target_locked = target->Lock(GxLockFlags::Bits);
  if (source_locked && target_locked) {
    i32 source_pitch = static_cast<i32>(source->GetColorSurfaceStride()),
        target_pitch = static_cast<i32>(target->GetColorSurfaceStride());
    u8 *source_data = static_cast<u8 *>(source->GetColorSurfaceBits()),
       *target_data = static_cast<u8 *>(target->GetColorSurfaceBits());
    for (i32 y = 0; y < height; ++y) {
      u8 *row = source_data + static_cast<iptr>(source_pitch) * y;
      u8 *destination = target_data + static_cast<iptr>(target_pitch) * y;
      for (i32 x = 0; x < width; ++x) {
        u32 pixel;
        memcpy(&pixel, row + 4 * x, 4);
        u16 packed = has_key && pixel == source_key ? target_key : BdPackThumbnailPixel(pixel, six_green_bits);
        memcpy(destination + 2 * x, &packed, 2);
      }
    }
    source->Unlock(GxLockFlags::Bits);
    target->Unlock(GxLockFlags::Bits);
    *out = target;
    return;
  }
  if (source_locked)
    source->Unlock(GxLockFlags::Bits);
  if (target_locked)
    target->Unlock(GxLockFlags::Bits);
}

void BdConvertToRgb555(GxBuffer *source, GxBuffer **out) {
  BdConvertThumbnail(source, out, false);
}

void BdConvertToRgb565(GxBuffer *source, GxBuffer **out) {
  BdConvertThumbnail(source, out, true);
}

// Copy the selected 32-bit pixels into a new retained buffer and preserve its source color key.
// Both locks are attempted in source order; failures release the new buffer with the original lock behavior.
void BdCropImage(GxBuffer *source, const BdRect *bounds, GxBuffer **out) {
  *out = nullptr;
  GxBuffer *target = nullptr;
  GxColorType format(GxColorType::Color::RGB16);
  if (!BdGetGraphics()->CreateBuffer(&target))
    return;
  i32 width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
  format = source->GetColorType();
  if (!target->Init(static_cast<u32>(width), static_cast<u32>(height), format, nullptr,
          GxDepthType(GxDepthType::Depth::None), GxVramRequest::VideoMemoryLowPriority, false)) {
    target->Release();
    return;
  }
  u8 source_locked = source->Lock(GxLockFlags::Bits), target_locked = target->Lock(GxLockFlags::Bits);
  if (!source_locked || !target_locked) {
    if (source_locked)
      source->Unlock(GxLockFlags::Bits);
    if (target_locked)
      target->Unlock(GxLockFlags::Bits);
    target->Release();
    return;
  }
  i32 source_pitch = static_cast<i32>(source->GetColorSurfaceStride()),
      target_pitch = static_cast<i32>(target->GetColorSurfaceStride());
  u8 *source_data = static_cast<u8 *>(source->GetColorSurfaceBits()),
     *target_data = static_cast<u8 *>(target->GetColorSurfaceBits());
  if (!source_data || !target_data) {
    target->Release();
    return;
  }
  for (i32 y = 0; y < height; ++y) {
    u8 *row = source_data + static_cast<iptr>(source_pitch) * (y + bounds->top) + 4 * bounds->left;
    u8 *destination = target_data + static_cast<iptr>(target_pitch) * y;
    for (i32 x = 0; x < width; ++x)
      memcpy(destination + 4 * x, row + 4 * x, 4);
  }
  source->Unlock(GxLockFlags::Bits);
  target->Unlock(GxLockFlags::Bits);
  if (source->GetTransparency()) {
    u32 key = 0;
    source->GetTransparentColor(&key);
    target->SetTransparency(key);
  }
  *out = target;
}

struct BdScaleWeight {
  i32 source;
  f32 weight;
};

struct BdScaleColumn {
  i32 count;
  BdScaleWeight *weights;
};

f64 BD_STDCALL BdScaleKernel(f32 value) {
  f64 distance = value < 0 ? -static_cast<f64>(value) : static_cast<f64>(value);
  return distance >= 1 ? 0 : (distance + distance - 3) * distance * distance + 1;
}

// Build the per-output-pixel source indices and cubic weights used by one scaling pass.
// The source reflects indices once at each input edge and stores the weights as floats.
static void BdBuildScaleWeights(
    BdScaleColumn *columns, BdScaleWeight *weights, i32 count, i32 input_count, i32 capacity, f32 ratio, f32 radius) {
  for (i32 destination = 0; destination < count; ++destination) {
    BdScaleColumn *column = &columns[destination];
    column->count = 0;
    column->weights = weights + static_cast<iptr>(destination) * capacity;
    f64 position = static_cast<f64>(destination) / ratio;
    f32 rounded_position = static_cast<f32>(position);
    i32 first = static_cast<i32>(ceil(position - radius));
    i32 last = static_cast<i32>(floor(static_cast<f64>(rounded_position) + radius));
    for (i32 sample = first; sample <= last; ++sample) {
      f32 distance = static_cast<f32>((static_cast<f64>(rounded_position) - sample) / radius);
      i32 reflected = sample < 0 ? -sample : sample < input_count ? sample : 2 * input_count - sample - 1;
      BdScaleWeight *entry = &column->weights[column->count++];
      entry->source = reflected;
      entry->weight = static_cast<f32>(BdScaleKernel(distance) / radius);
    }
  }
}

// Round and clamp one accumulated color channel to the original unsigned-byte range.
static u8 BdScaleChannel(f64 value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return static_cast<u8>(value + 0.5);
}

// Accumulate a single filtered pixel, skipping the source's transparent color key when enabled.
// The source rounds three channels to float after each contribution and normalizes only above its threshold.
static u32 BdScalePixel(const u8 *pixels, iptr stride, const BdScaleColumn *column, bool keyed, u32 key) {
  f32 alpha = 0, red = 0, green = 0;
  f64 blue = 0, total = 0;
  for (i32 i = 0; i < column->count; ++i) {
    const BdScaleWeight *entry = &column->weights[i];
    u32 pixel;
    memcpy(&pixel, pixels + stride * entry->source, 4);
    if (!keyed || pixel != key) {
      f64 weight = entry->weight;
      alpha = static_cast<f32>(alpha + static_cast<f64>(pixel >> 24) * weight);
      red = static_cast<f32>(red + static_cast<f64>((pixel >> 16) & 255) * weight);
      green = static_cast<f32>(green + static_cast<f64>((pixel >> 8) & 255) * weight);
      blue += static_cast<f64>(pixel & 255) * weight;
      total += weight;
    }
  }
  if (total < static_cast<f64>(0.4f))
    return key;
  f64 inverse = 1.0 / total;
  alpha = static_cast<f32>(alpha * inverse);
  red = static_cast<f32>(red * inverse);
  green = static_cast<f32>(green * inverse);
  return (static_cast<u32>(BdScaleChannel(alpha)) << 24) | (static_cast<u32>(BdScaleChannel(red)) << 16) |
      (static_cast<u32>(BdScaleChannel(green)) << 8) | BdScaleChannel(blue * inverse);
}

// Scale a 32-bit image in horizontal and vertical cubic passes, preserving the source color key.
// Optional existing intermediate and output buffers follow the original ownership and lock order.
GxBuffer *BdScaleRgba(f32 scale, GxBuffer *source, const BdRect *bounds, GxBuffer *intermediate, GxBuffer *target) {
  GxBltFlags color = {};
  source->GetBltFlags(&color);
  bool keyed = (color.flags & 2) != 0;
  u32 key = keyed ? color.pixel : 0;
  i32 left = 0, top = 0, width, height;
  if (bounds) {
    left = bounds->left;
    top = bounds->top;
    width = bounds->right - bounds->left;
    height = bounds->bottom - bounds->top;
  } else {
    width = source->Width();
    height = source->Height();
  }
  i32 output_width = static_cast<i32>(ceil(static_cast<f64>(width) * scale));
  i32 output_height = static_cast<i32>(ceil(static_cast<f64>(height) * scale));
  f32 horizontal = static_cast<f32>(static_cast<f64>(output_width) / static_cast<f32>(width));
  f32 vertical = static_cast<f32>(static_cast<f64>(output_height) / static_cast<f32>(height));
  if (!horizontal || !vertical)
    return nullptr;
  if (!target) {
    BdGetGraphics()->CreateBuffer(&target);
    if (!target)
      return nullptr;
    GxColorType current = source->GetColorType();
    if (!target->Init(static_cast<u32>(output_width), static_cast<u32>(output_height), current, nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
      target->Release();
      return nullptr;
    }
  }
  if (source->GetTransparency()) {
    u32 value = 0;
    source->GetTransparentColor(&value);
    target->SetTransparency(value);
  }
  if (!intermediate) {
    BdGetGraphics()->CreateBuffer(&intermediate);
    if (!intermediate) {
      target->Release();
      return nullptr;
    }
    GxColorType current = source->GetColorType();
    if (!intermediate->Init(static_cast<u32>(output_width), static_cast<u32>(height), current, nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
      intermediate->Release();
      target->Release();
      return nullptr;
    }
  }
  if (source->GetTransparency()) {
    u32 value = 0;
    source->GetTransparentColor(&value);
    intermediate->SetTransparency(value);
  }
  i32 largest = output_width >= output_height ? output_width : output_height;
  f32 radius = horizontal < 1 ? 1.0f / horizontal : 1.0f;
  i32 capacity = static_cast<i32>(static_cast<f64>(radius) * 3.0 + 1.0);
  f32 vertical_radius = vertical < 1 ? 1.0f / vertical : 1.0f;
  i32 vertical_capacity = static_cast<i32>(static_cast<f64>(vertical_radius) * 3.0 + 1.0);
  if (vertical_capacity > capacity)
    capacity = vertical_capacity;
  BdScaleColumn *columns = new BdScaleColumn[largest];
  BdScaleWeight *weights = new BdScaleWeight[static_cast<usize>(largest) * capacity];
  BdBuildScaleWeights(columns, weights, output_width, width, capacity, horizontal, radius);
  if (!intermediate->Lock(GxLockFlags::Bits) || !source->Lock(GxLockFlags::Bits)) {
    delete[] columns;
    delete[] weights;
    intermediate->Release();
    target->Release();
    return nullptr;
  }
  u8 *intermediate_data = static_cast<u8 *>(intermediate->GetColorSurfaceBits()),
     *source_data = static_cast<u8 *>(source->GetColorSurfaceBits());
  u32 source_pitch = source->GetColorSurfaceStride() >> 2,
      intermediate_pitch = intermediate->GetColorSurfaceStride() >> 2;
  i32 intermediate_width = intermediate->Width(), intermediate_height = intermediate->Height();
  for (i32 y = 0; y < intermediate_height; ++y) {
    u8 *row = source_data + 4 * (static_cast<iptr>(top + y) * source_pitch + left);
    u8 *destination = intermediate_data + 4 * static_cast<iptr>(y) * intermediate_pitch;
    for (i32 x = 0; x < intermediate_width; ++x) {
      u32 pixel = BdScalePixel(row, 4, &columns[x], keyed, key);
      memcpy(destination + 4 * x, &pixel, 4);
    }
  }
  source->Unlock(GxLockFlags::Bits);
  radius = vertical_radius;
  BdBuildScaleWeights(columns, weights, output_height, intermediate_height, capacity, vertical, radius);
  if (!target->Lock(GxLockFlags::Bits)) {
    delete[] columns;
    delete[] weights;
    intermediate->Release();
    target->Release();
    return nullptr;
  }
  intermediate_pitch = intermediate->GetColorSurfaceStride() >> 2;
  u32 target_pitch = target->GetColorSurfaceStride() >> 2;
  u8 *target_data = static_cast<u8 *>(target->GetColorSurfaceBits());
  intermediate_data = static_cast<u8 *>(intermediate->GetColorSurfaceBits());
  for (i32 x = 0; x < output_width; ++x) {
    const u8 *column = intermediate_data + 4 * x;
    u8 *destination = target_data + 4 * x;
    for (i32 y = 0; y < output_height; ++y) {
      u32 pixel = BdScalePixel(column, 4 * static_cast<iptr>(intermediate_pitch), &columns[y], keyed, key);
      memcpy(destination + 4 * static_cast<iptr>(target_pitch) * y, &pixel, 4);
    }
  }
  intermediate->Unlock(GxLockFlags::Bits);
  target->Unlock(GxLockFlags::Bits);
  delete[] columns;
  delete[] weights;
  intermediate->Release();
  return target;
}

// Expand the source's 555 or 565 pixel without replicating its low channel bits.
static u32 BdExpandScalePixel(u16 pixel, bool six_green_bits) {
  if (six_green_bits)
    return (static_cast<u32>(pixel & 0xf800) << 8) | (static_cast<u32>(pixel & 0x07e0) << 5) |
        (static_cast<u32>(pixel & 0x001f) << 3);
  return (static_cast<u32>(pixel & 0x7c00) << 9) | (static_cast<u32>(pixel & 0x03e0) << 6) |
      (static_cast<u32>(pixel & 0x001f) << 3);
}

// Pack the three rounded channels into the original 555 or 565 destination format.
static u16 BdPackScalePixel(u8 red, u8 green, u8 blue, bool six_green_bits) {
  if (six_green_bits)
    return static_cast<u16>(((red & 0xf8u) << 8) | ((green & 0xfcu) << 3) | (blue >> 3));
  return static_cast<u16>(((red & 0xf8u) << 7) | ((green & 0xf8u) << 2) | (blue >> 3));
}

// Filter one 16-bit pixel, skipping samples whose expanded RGB matches the source color key.
static u16 BdScalePalettedPixel(const u8 *pixels, iptr stride, const BdScaleColumn *column, bool six_green_bits,
    bool keyed, u16 raw_key, u32 expanded_key) {
  f32 red = 0, green = 0, blue = 0, total = 0;
  for (i32 i = 0; i < column->count; ++i) {
    const BdScaleWeight *entry = &column->weights[i];
    u16 packed;
    memcpy(&packed, pixels + stride * entry->source, 2);
    u32 pixel = BdExpandScalePixel(packed, six_green_bits);
    if (!keyed || pixel != expanded_key) {
      f64 weight = entry->weight;
      red = static_cast<f32>(red + ((pixel >> 16) & 255) * weight);
      green = static_cast<f32>(green + ((pixel >> 8) & 255) * weight);
      blue = static_cast<f32>(blue + (pixel & 255) * weight);
      total = static_cast<f32>(total + weight);
    }
  }
  if (total < 0.4f)
    return raw_key;
  f64 inverse = 1.0 / total;
  red = static_cast<f32>(red * inverse);
  green = static_cast<f32>(green * inverse);
  return BdPackScalePixel(BdScaleChannel(red), BdScaleChannel(green), BdScaleChannel(blue * inverse), six_green_bits);
}

// Reject reflected indices that would read outside the source's initialized pixel extent.
static bool BdScaleWeightsValid(const BdScaleColumn *columns, i32 count, i32 input_count) {
  for (i32 i = 0; i < count; ++i)
    for (i32 j = 0; j < columns[i].count; ++j)
      if (columns[i].weights[j].source < 0 || columns[i].weights[j].source >= input_count)
        return false;
  return true;
}

// Scale a 555 or 565 image in two cubic passes, preserving the original expanded-key comparison and optional buffers.
// Source rectangles whose reflected sample coordinates escape the input are excluded from defined execution.
GxBuffer *BdScalePalettedBicubic(
    f32 scale, GxBuffer *source, const BdRect *bounds, GxBuffer *intermediate, GxBuffer *target) {
  GxColorType format = source->GetColorType();
  i32 code = static_cast<i32>(format.color);
  GxBltFlags color = {};
  source->GetBltFlags(&color);
  if (code != 5 && code != 7 && code != 10)
    return nullptr;
  bool six_green_bits = code == 7, keyed = (color.flags & 2) != 0;
  u16 raw_key = keyed ? static_cast<u16>(color.pixel) : 0;
  u32 expanded_key = keyed ? BdExpandScalePixel(raw_key, six_green_bits) : 0;
  i32 left = 0, top = 0, width, height;
  if (bounds) {
    left = bounds->left;
    top = bounds->top;
    width = bounds->right - bounds->left;
    height = bounds->bottom - bounds->top;
  } else {
    width = source->Width();
    height = source->Height();
  }
  if (width <= 0 || height <= 0)
    return nullptr;
  i32 output_width = static_cast<i32>(ceil(static_cast<f64>(width) * scale));
  i32 output_height = static_cast<i32>(ceil(static_cast<f64>(height) * scale));
  f32 horizontal = static_cast<f32>(static_cast<f64>(output_width) / static_cast<f32>(width));
  f32 vertical = static_cast<f32>(static_cast<f64>(output_height) / static_cast<f32>(height));
  if (output_width <= 0 || output_height <= 0 || !horizontal || !vertical)
    return nullptr;
  bool own_target = target == nullptr, own_intermediate = intermediate == nullptr;
  if (own_target) {
    BdGetGraphics()->CreateBuffer(&target);
    if (!target)
      return nullptr;
    format = source->GetColorType();
    if (!target->Init(output_width, output_height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
            GxVramRequest::SystemMemory, false)) {
      target->Release();
      return nullptr;
    }
  }
  if (own_intermediate) {
    BdGetGraphics()->CreateBuffer(&intermediate);
    if (!intermediate) {
      if (own_target)
        target->Release();
      return own_target ? nullptr : target;
    }
    format = source->GetColorType();
    if (!intermediate->Init(output_width, height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
            GxVramRequest::SystemMemory, false)) {
      intermediate->Release();
      if (own_target)
        target->Release();
      return own_target ? nullptr : target;
    }
  }
  i32 largest = output_width >= output_height ? output_width : output_height;
  f32 horizontal_radius = horizontal < 1 ? 1.0f / horizontal : 1.0f;
  f32 vertical_radius = vertical < 1 ? 1.0f / vertical : 1.0f;
  f32 radius = horizontal_radius > vertical_radius ? horizontal_radius : vertical_radius;
  i32 capacity = static_cast<i32>((static_cast<f64>(radius) + 1.0) * 2.0);
  BdScaleColumn *columns = new BdScaleColumn[largest];
  BdScaleWeight *weights = new BdScaleWeight[static_cast<usize>(largest) * capacity];
  BdBuildScaleWeights(columns, weights, output_width, width, capacity, horizontal, horizontal_radius);
  bool valid = BdScaleWeightsValid(columns, output_width, width);
  if (valid && intermediate->Lock(GxLockFlags::Bits) && source->Lock(GxLockFlags::Bits)) {
    u8 *intermediate_data = static_cast<u8 *>(intermediate->GetColorSurfaceBits()),
       *source_data = static_cast<u8 *>(source->GetColorSurfaceBits());
    u32 source_pitch = source->GetColorSurfaceStride() >> 1,
        intermediate_pitch = intermediate->GetColorSurfaceStride() >> 1;
    i32 intermediate_width = intermediate->Width(), intermediate_height = intermediate->Height();
    for (i32 y = 0; y < intermediate_height; ++y) {
      u8 *row = source_data + 2 * (static_cast<iptr>(top + y) * source_pitch + left);
      u8 *destination = intermediate_data + 2 * static_cast<iptr>(y) * intermediate_pitch;
      for (i32 x = 0; x < intermediate_width; ++x) {
        u16 pixel = BdScalePalettedPixel(row, 2, &columns[x], six_green_bits, keyed, raw_key, expanded_key);
        memcpy(destination + 2 * x, &pixel, 2);
      }
    }
    source->Unlock(GxLockFlags::Bits);
    BdBuildScaleWeights(columns, weights, output_height, intermediate_height, capacity, vertical, vertical_radius);
    valid = BdScaleWeightsValid(columns, output_height, intermediate_height);
    if (valid && target->Lock(GxLockFlags::Bits)) {
      intermediate_pitch = intermediate->GetColorSurfaceStride() >> 1;
      u32 target_pitch = target->GetColorSurfaceStride() >> 1;
      u8 *target_data = static_cast<u8 *>(target->GetColorSurfaceBits());
      intermediate_data = static_cast<u8 *>(intermediate->GetColorSurfaceBits());
      for (i32 x = 0; x < output_width; ++x) {
        const u8 *column = intermediate_data + 2 * x;
        u8 *destination = target_data + 2 * x;
        for (i32 y = 0; y < output_height; ++y) {
          u16 pixel = BdScalePalettedPixel(column, 2 * static_cast<iptr>(intermediate_pitch), &columns[y],
              six_green_bits, keyed, raw_key, expanded_key);
          memcpy(destination + 2 * static_cast<iptr>(target_pitch) * y, &pixel, 2);
        }
      }
      intermediate->Unlock(GxLockFlags::Bits);
      target->Unlock(GxLockFlags::Bits);
      delete[] columns;
      delete[] weights;
      if (own_intermediate)
        intermediate->Release();
      return target;
    }
  }
  delete[] columns;
  delete[] weights;
  if (own_intermediate)
    intermediate->Release();
  if (own_target) {
    target->Release();
    return nullptr;
  }
  return target;
}

// Clamp the original signed channel calculation to an unsigned byte.
static u8 BdClampThumbnailChannel(i32 value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return static_cast<u8>(value);
}

// Apply the source's scaled and offset channel calculations to a 32-bit thumbnail while preserving its alpha.
// Color-key pixels are skipped, and a locked buffer of another reported format follows the source's early return.
void BdShadeThumbnail(GxBuffer *buffer, f32 scale, u8 red, u8 green, u8 blue) {
  if (!buffer || !buffer->Lock(GxLockFlags::Bits))
    return;
  GxColorType current = buffer->GetColorType();
  if (static_cast<i32>(current.color) != 32)
    return;
  bool keyed = buffer->GetTransparency();
  i32 width = buffer->Width(), height = buffer->Height();
  u32 key = 0;
  buffer->GetTransparentColor(&key);
  u8 *data = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  i32 pitch = static_cast<i32>(buffer->GetColorSurfaceStride());
  for (i32 y = 0; y < height; ++y) {
    u8 *row = data + static_cast<iptr>(pitch) * y;
    for (i32 x = 0; x < width; ++x) {
      u32 pixel;
      memcpy(&pixel, row + 4 * x, 4);
      if (keyed && pixel == key)
        continue;
      u32 scaled_red = static_cast<u32>(static_cast<i64>(static_cast<f64>((pixel >> 16) & 255) * scale));
      u32 scaled_green = static_cast<u32>(static_cast<i64>(static_cast<f64>((pixel >> 8) & 255) * scale));
      u32 scaled_blue = static_cast<u32>(static_cast<i64>(static_cast<f64>(pixel & 255) * scale));
      u32 result = (pixel & kGxArgbAlphaMask) |
          (static_cast<u32>(BdClampThumbnailChannel(static_cast<i32>(scaled_red + red))) << 16) |
          (static_cast<u32>(BdClampThumbnailChannel(static_cast<i32>(scaled_green + green))) << 8) |
          BdClampThumbnailChannel(static_cast<i32>(scaled_blue * blue));
      memcpy(row + 4 * x, &result, 4);
    }
  }
  buffer->Unlock(GxLockFlags::Bits);
}
