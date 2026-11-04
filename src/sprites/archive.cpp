#include <string.h>

#include "common/framework.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "sprites/api.h"
#include "sprites/sp_archive.h"

// Archive headers select raw, run-length, palette or QFS payloads with these original format flags.
// The two legacy encodings have genuine zero-return decoder stubs; their names describe only their stored codes.
enum SpArchivePixelFlag : u32 {
  kSpArchiveRawPixels = 1,
  kSpArchiveRowRuns = 2,
  kSpArchiveColumnRuns = 4,
  kSpArchiveTransparentRows = 8,
  kSpArchiveTransparentColumns = 16,
  kSpArchivePaletteRows = 128,
  kSpArchivePaletteColumns = 256,
  kSpArchiveLegacy8000 = 0x8000,
  kSpArchiveLegacyC000 = 0xc000,
  kSpArchiveQfsPixels = 0x80000,
  kSpArchiveIndexedPixels = 0x10000000,
};

static u32 SpArchiveWord(const u8 *bytes) {
  return (u32)bytes[0] | (u32)bytes[1] << 8 | (u32)bytes[2] << 16 | (u32)bytes[3] << 24;
}

SpArchive::SpArchive() : RsIndexedFile(nullptr), color_key(0) {
}

// The derived destructor forwards to its indexed-file base. Native C++ destruction supplies the real receiver adjustment.
SpArchive::~SpArchive() = default;

bool SpArchive::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpImageSource, SpImageSource);
  return RsIndexedFile::QueryInterface(iid, out);
}

u32 SpArchive::AddRef() {
  return RsIndexedFile::AddRef();
}

u32 SpArchive::Release() {
  return RsIndexedFile::Release();
}

// Pack the magenta key through the live graphics service, then query it again for the target format.
bool SpArchive::Init(u32 id, const ScStringView *name, bool resolved) {
  SpGetGraphics()->ConvertRGBValueToNative16Bit(&color_key, 255, 0, 255);
  target_format = static_cast<i16>(SpGetGraphics()->GetScreenColorType().color);
  return RsIndexedFile::Init(id, name, resolved);
}

bool SpArchive::Shutdown() {
  return RsIndexedFile::Shutdown();
}

// Read the original eight rectangle bytes through the record's actual data interface, ignoring size/read results.
// Key words are loaded after stream Release; a failed stream query still closes the current record, unlike ReadImage.
u8 SpArchive::ReadMetadata(SpImage *out, const RsKey *key) {
  RsRecord *record = nullptr;
  if (!DoOpenRecord(*key, &record))
    return 0;
  RsRecordData *data;
  if (record->QueryInterface(IID_RsRecordData, reinterpret_cast<void **>(&data))) {
    u32 size;
    i16 rectangle[4];
    data->GetSize(&size);
    data->ReadBlock(rectangle, 8);
    data->Release();
    out->references = 0;
    out->resource = key->instance;
    out->group = key->group;
    out->left = rectangle[0];
    i16 top = rectangle[1];
    out->buffer = nullptr;
    out->top = top;
    out->right = rectangle[2];
    out->bottom = rectangle[3];
    DoCloseRecord(&record);
    return 1;
  }
  DoCloseRecord(&record);
  return 0;
}

// Build a real buffer from the DAT header and pixel payload, preserving ordinary versus QFS ownership paths.
// Decode/graphics helper results are discarded where the source discards them; stream-query failure leaves the record open.
u8 SpArchive::ReadImage(GxBuffer **out, const RsKey *key) {
  RsRecord *record = nullptr;
  RsRecordData *data;
  GxBuffer *target = nullptr;
  u8 *allocation = nullptr;
  if (!DoOpenRecord(*key, &record) || !record->QueryInterface(IID_RsRecordData, reinterpret_cast<void **>(&data)))
    return 0;
  u32 size;
  data->GetSize(&size);
  u8 local[0xc010], *input = local;
  if (size > sizeof(local)) {
    allocation = new u8[size];
    input = allocation;
  }
  data->ReadBlock(input, size);
  data->Release();
  u8 pixel_format = input[0], kind = input[1];
  u32 flags = SpArchiveWord(input + 4), width = SpArchiveWord(input + 8), height = SpArchiveWord(input + 12);
  const u8 *pixels = input + 16;
  size -= 16;
  if (!kind) {
    SpGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&target));
    bool indexed = (flags & kSpArchiveIndexedPixels) != 0;
    if (target->Init(width, height, GxColorType(static_cast<GxColorType::Color>(indexed ? 3 : 4)), nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
      DecodePixels(target, pixels, size, flags, pixel_format);
      DoCloseRecord(&record);
      *out = target;
      if (!indexed) {
        target->SetTransparency(color_key);
        GxImageBuilder *builder = nullptr;
        if (SpGetCom()->GetClassObject(
                kGxImageBuilderClassId, IID_GxImageBuilder, reinterpret_cast<void **>(&builder))) {
          builder->ConvertStandardBufferToData(target);
          target->Release();
          *out = builder->AsIGZBuffer();
        }
      }
      delete[] allocation;
      return 1;
    }
  } else if (kind == 1 && !(flags & kSpArchiveLegacy8000) && (flags & kSpArchiveQfsPixels)) {
    u32 decoded_size = codec.GetSize(pixels);
    GxImageBuilder *builder = nullptr;
    SpGetCom()->GetClassObject(kGxImageBuilderClassId, IID_GxImageBuilder, reinterpret_cast<void **>(&builder));
    target = builder->AsIGZBuffer();
    auto decoded = new u8[decoded_size];
    if (codec.Decompress(pixels, size, decoded, &decoded_size)) {
      builder->ImportFromData(decoded, 1);
      *out = target;
      i32 desired = target_format;
      if (static_cast<i32>(target->GetColorType().color) != desired) {
        u32 actual = static_cast<u32>(target->GetColorType().color);
        target->Convert16BitBufferFromFormatToFormat(
            static_cast<GxColorType::Color>(actual), static_cast<GxColorType::Color>((u32)desired));
      }
      DoCloseRecord(&record);
      delete[] allocation;
      return 1;
    }
  }
  if (target)
    target->Release();
  DoCloseRecord(&record);
  *out = nullptr;
  delete[] allocation;
  return 0;
}

// Select the original pixel decoder without normalizing its byte result. The two legacy format stubs really
// return zero, while unrecognized flags return their low byte without accessing the source or target.
u8 SpArchive::DecodePixels(GxBuffer *target, const u8 *source, u32 payload_size, u32 flags, i32 source_format) {
  if (flags & kSpArchiveIndexedPixels) {
    if (flags & kSpArchiveLegacy8000)
      return 0;
    if (flags & kSpArchiveQfsPixels)
      return DecodePlainBitmap(target, source, payload_size);
  } else {
    switch (flags) {
    case kSpArchiveRawPixels:
      return SpDecodeRaw(target, source, payload_size, source_format);
    case kSpArchiveRowRuns:
      return SpDecodeRowRuns(target, source, payload_size, source_format);
    case kSpArchiveColumnRuns:
      return SpDecodeColumnRuns(target, source, payload_size, source_format);
    case kSpArchiveTransparentRows:
      return DecodeTransparentRows(target, source, payload_size, source_format);
    case kSpArchiveTransparentColumns:
      return DecodeTransparentColumns(target, source, payload_size, source_format);
    case kSpArchivePaletteRows:
      return DecodePaletteRows(target, source, payload_size, source_format);
    case kSpArchivePaletteColumns:
      return DecodePaletteColumns(target, source, payload_size, source_format);
    case kSpArchiveLegacy8000:
    case kSpArchiveLegacyC000:
      return 0;
    }
  }
  return (u8)flags;
}

// Expand a plain QFS bitmap directly when pitch equals byte width, otherwise copy from a temporary packed image.
// Preserve the original lock/row-query order and the branch-specific free/unlock ordering on zero-size output.
u8 SpArchive::DecodePlainBitmap(GxBuffer *target, const u8 *source, u32 payload_size) {
  if (!target->Lock(GxLockFlags::Bits))
    return 0;
  u8 *destination = static_cast<u8 *>(target->GetColorSurfaceBits());
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), written;
  if (pitch != width) {
    u32 size = width * height;
    u8 stack[0x10000], *heap = size >= sizeof(stack) ? new u8[size] : nullptr;
    u8 *pixels = heap ? heap : stack;
    if (codec.Decompress(source, payload_size, pixels, &written)) {
      for (u32 row = height; row; --row) {
        memcpy(destination, pixels, width);
        destination += pitch;
        pixels += width;
      }
      target->Unlock(GxLockFlags::Bits);
      delete[] heap;
      return 1;
    }
    delete[] heap;
    target->Unlock(GxLockFlags::Bits);
    return 0;
  }
  bool result = codec.Decompress(source, payload_size, destination, &written);
  target->Unlock(GxLockFlags::Bits);
  return result;
}

// Decode literal WORDs and sentinel-one transparent runs across rows. Transparent stores reload the archive's
// live color key, while only literal colors pass through the buffer's conversion method.
u8 SpArchive::DecodeTransparentRows(GxBuffer *target, const u8 *source, u32, i32 source_format) {
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
      u16 pixel;
      memcpy(&pixel, source, sizeof(pixel));
      if (pixel == 1) {
        for (u32 left = source[2]; left; --left) {
          u16 key = color_key;
          memcpy(pixels + destination, &key, sizeof(key));
          destination += 2;
        }
        source += 3;
      } else {
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += 2;
        source += 2;
      }
    } while (destination < end);
    destination += (isize)pitch - span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Decode the same sentinel-one packets down pitch-separated columns. Preserve the first packet for a zero
// height span, packet overshoot and each transparent store's live color-key read.
u8 SpArchive::DecodeTransparentColumns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
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
      u16 pixel;
      memcpy(&pixel, source, sizeof(pixel));
      if (pixel == 1) {
        for (u32 left = source[2]; left; --left) {
          u16 key = color_key;
          memcpy(pixels + destination, &key, sizeof(key));
          destination += pitch;
        }
        source += 3;
      } else {
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += pitch;
        source += 2;
      }
    } while (destination < end);
    destination += 2 - (isize)span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Resolve literal byte indices through the payload's WORD palette and decode escape runs across rows.
// Each escape packet captures the live color key before reading its count, even when that count is zero.
u8 SpArchive::DecodePaletteRows(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height(), span = width * 2;
  u8 escape = source[1];
  const u8 *palette = source + 2;
  usize input = 2 + 2 * source[0];
  for (u32 row = height; row; --row) {
    usize end = destination + span;
    do {
      u16 pixel;
      if (source[input] == escape) {
        pixel = color_key;
        for (u32 left = source[input + 1]; left; --left) {
          memcpy(pixels + destination, &pixel, sizeof(pixel));
          destination += 2;
        }
        input += 2;
      } else {
        memcpy(&pixel, palette + 2 * source[input], sizeof(pixel));
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += 2;
        ++input;
      }
    } while (destination < end);
    destination += (isize)pitch - span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}

// Decode palette indices and captured-color escape runs down columns. Palette header reads still occur when
// no columns are drawn, and only literal palette entries invoke pixel conversion.
u8 SpArchive::DecodePaletteColumns(GxBuffer *target, const u8 *source, u32, i32 source_format) {
  bool different = source_format != static_cast<i32>(target->GetColorType().color);
  u8 result = target->Lock(GxLockFlags::Bits);
  if (!result)
    return result;
  u8 *pixels = static_cast<u8 *>(target->GetColorSurfaceBits());
  usize destination = 0;
  u32 pitch = target->GetColorSurfaceStride(), width = target->Width(), height = target->Height();
  u8 escape = source[1];
  const u8 *palette = source + 2;
  usize input = 2 + 2 * source[0];
  u32 span = pitch * height;
  for (u32 column = width; column; --column) {
    usize end = destination + span;
    do {
      u16 pixel;
      if (source[input] == escape) {
        pixel = color_key;
        for (u32 left = source[input + 1]; left; --left) {
          memcpy(pixels + destination, &pixel, sizeof(pixel));
          destination += pitch;
        }
        input += 2;
      } else {
        memcpy(&pixel, palette + 2 * source[input], sizeof(pixel));
        if (different)
          target->Convert16BitValueFromFormatToFormat(
              &pixel, static_cast<GxColorType::Color>(source_format), static_cast<GxColorType::Color>(1));
        memcpy(pixels + destination, &pixel, sizeof(pixel));
        destination += pitch;
        ++input;
      }
    } while (destination < end);
    destination += 2 - (isize)span;
  }
  target->Unlock(GxLockFlags::Bits);
  return 1;
}
