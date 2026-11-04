#define _CRT_SECURE_NO_WARNINGS
#include "buildings/architect/codec.h"

#include <string.h>

#include "buildings/architect/api.h"
#include "common/path.h"
#include "common/sc3_director.h"
#include "common/sc3_qfs.h"
#include "common/sc3_string.h"
#include "graphics/image_codecs.h"

BdCodec::BdCodec() : references(0) {
}

BdCodec::~BdCodec() {
}

// Publish and retain only the unknown or codec identity. Rejected requests clear the output.
bool BdCodec::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, BdCodec);
  HANDLE_QUERY_INTERFACE(IID_BdCodec, BdCodec);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment this wrapper before retaining the actual module director. The original body is shared with the loader.
u32 BdCodec::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Release the director before decrementing a nonzero object count. Entry count zero also deletes the wrapper.
u32 BdCodec::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

i32 BdCodec::GetValue0C() {
  return 2;
}

i32 BdCodec::GetValue10() {
  return 21;
}

BdCodecDescriptor *BD_STDCALL BdCodec::Describe() {
  return BdDescribeCodec();
}

i32 BD_STDCALL BdCodec::CanRead(BdCodecFile *file) {
  return BdCanReadCodec(file);
}

i32 BD_STDCALL BdCodec::OpenReader(BdCodecState **out, BdCodecFile *file, const char *path) {
  return BdReadCodecInfo(out, file, path);
}

BdCodecInfo *BD_STDCALL BdCodec::InfoAt(BdCodecState *state, i32 index) {
  return BdCodecInfoAt(state, index);
}

i32 BD_STDCALL BdCodec::ReadPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  return BdReadCodecPixels(state, info, out, pitch);
}

i32 BD_STDCALL BdCodec::CloseReader(BdCodecState *state) {
  return BdCloseCodecState(state);
}

i32 BD_STDCALL BdCodec::OpenWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C) {
  return BdOpenCodecWriter(out, file, path, field_0C);
}

i32 BD_STDCALL BdCodec::WritePixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  return BdWriteCodecPixels(state, info, source, pitch);
}

i32 BD_STDCALL BdCodec::CloseWriter(BdCodecState *state) {
  return BdCloseCodecWriter(state);
}

i32 BD_STDCALL BdCodec::SelectForFile(const char *path, BdCodecFile *file) {
  return BdSelectCodecForFile(path, file);
}

BdCodecFile *BD_STDCALL BdCodec::OpenFile(const char *path, char *resolved) {
  return BdOpenCodec(path, resolved);
}

i32 BD_STDCALL BdCodec::SetSelected(u32 index) {
  return BdSelectCodec(index);
}

u32 BD_STDCALL BdCodec::GetSelected() {
  return BdSelectedCodec();
}

u32 BD_STDCALL BdCodec::SelectExtension(const char *extension, i32 *slot) {
  return BdSelectCodecExtension(extension, slot);
}

i32 BD_STDCALL BdCodec::GetCodecCount() {
  return BdCodecCount();
}

void BD_STDCALL BdCodec::DestroyPixels(BdCodecPixels *pixels) {
  BdDestroyCodecPixels(pixels);
}

BdCodecPixels *BD_STDCALL BdCodec::Decode(const char *path, i32 index) {
  return BdDecodeImage(path, index);
}

BdCodecDescriptor *BD_STDCALL BdCodec::DescribeBmp() {
  return BdDescribeBmpCodec();
}

i32 BD_STDCALL BdCodec::ProbeBmp(BdCodecFile *file) {
  return BdProbeBmp(file);
}

i32 BD_STDCALL BdCodec::OpenBmpReader(BdCodecState **out, BdCodecFile *file, const char *path) {
  return BdOpenBmpReader(out, file, path);
}

BdCodecInfo *BD_STDCALL BdCodec::BmpInfoAt(BdCodecState *state, i32 index) {
  return BdReadBmpInfo(state, index);
}

i32 BD_STDCALL BdCodec::ReadBmpPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  return BdReadBmpPixels(state, info, out, pitch);
}

i32 BD_STDCALL BdCodec::CloseBmpReader(BdCodecState *state) {
  return BdCloseCodecStream(state);
}

i32 BD_STDCALL BdCodec::OpenBmpWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C) {
  return BdCreateCodecWriterState(out, file, path, field_0C);
}

i32 BD_STDCALL BdCodec::WriteBmpPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  return BdWriteBmpPixels(state, info, source, pitch);
}

i32 BD_STDCALL BdCodec::CloseBmpWriter(BdCodecState *state) {
  return BdCloseCodecStream(state);
}

BdCodecDescriptor *BD_STDCALL BdCodec::DescribeTga() {
  return BdDescribeTgaCodec();
}

i32 BD_STDCALL BdCodec::ProbeTga(BdCodecFile *file) {
  return BdProbeTga(file);
}

i32 BD_STDCALL BdCodec::OpenTgaReader(BdCodecState **out, BdCodecFile *file, const char *path) {
  return BdOpenTgaReader(out, file, path);
}

BdCodecInfo *BD_STDCALL BdCodec::TgaInfoAt(BdCodecState *state, i32 index) {
  return BdReadTgaInfo(state, index);
}

i32 BD_STDCALL BdCodec::ReadTgaPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  return BdReadTgaPixels(state, info, out, pitch);
}

i32 BD_STDCALL BdCodec::CloseTgaReader(BdCodecState *state) {
  return BdCloseCodecStream(state);
}

i32 BD_STDCALL BdCodec::OpenTgaWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C) {
  return BdCreateCodecWriterState(out, file, path, field_0C);
}

i32 BD_STDCALL BdCodec::WriteTgaPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  return BdWriteTgaPixels(state, info, source, pitch);
}

i32 BD_STDCALL BdCodec::CloseTgaWriter(BdCodecState *state) {
  return BdCloseCodecStream(state);
}

// Extract a masked channel with the original half-denominator rounding and unsigned arithmetic.
static u8 BdBmpChannel(u32 pixel, u32 mask, u32 bits) {
  u32 shift = 0;
  if (mask)
    while (!(mask & (1u << shift)))
      ++shift;
  u32 scale = 1u << bits;
  return static_cast<u8>(((scale >> 1) + 255 * ((pixel & mask) >> shift)) / (scale - 1));
}

// Expand packed indices, BGR triples or masked WORD/DWORD pixels without unaligned or oversized source reads.
static u8 *BdBmpPixels(
    u8 *out, const u8 *source, u32 count, u32 depth, const BdCodecInfo *info, u32 alpha, u32 red, u32 green, u32 blue) {
  for (u32 x = 0; x < count; ++x) {
    if (depth == 1)
      *out++ = (source[x >> 3] >> (7 - (x & 7))) & 1;
    else if (depth == 4)
      *out++ = (source[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
    else if (depth == 8)
      *out++ = source[x];
    else if (depth == 24) {
      const u8 *pixel = source + 3 * x;
      out[0] = pixel[0];
      out[1] = pixel[1];
      out[2] = pixel[2];
      out[3] = 255;
      out += 4;
    } else if ((depth >= 15 && depth <= 16) || depth == 32) {
      u32 unit = depth == 32 ? 4 : 2;
      u32 pixel = BdCodecValue(source + unit * x, unit);
      out[3] = alpha ? BdBmpChannel(pixel, alpha, info->alpha_bits) : 255;
      out[2] = BdBmpChannel(pixel, red, info->red_bits);
      out[1] = BdBmpChannel(pixel, green, info->green_bits);
      out[0] = BdBmpChannel(pixel, blue, info->blue_bits);
      out += 4;
    }
  }
  return out;
}

// Decode one bitmap row with the original packet termination and final DWORD stream padding.
// Allocation lengths are native input-boundary arguments, separate from the original nine-argument row contract.
static i32 BdBmpRowInput(BdCodecFile *file, u8 *out, u8 *scratch, u32 depth, const BdCodecInfo *info, u32 alpha,
    u32 red, u32 green, u32 blue, u32 capacity, u32 *available, const u8 *end) {
  u32 width = info->width, cursor = 0, consumed = 0;
  u32 output_unit = depth == 1 || depth == 4 || depth == 8         ? 1
      : (depth >= 15 && depth <= 16) || depth == 24 || depth == 32 ? 4
                                                                   : 0;
  i32 result;
  if (!info->compressed) {
    u32 bytes = (width * (depth == 15 ? 16 : depth) + 31) / 8 & ~3u;
    u32 required = (width * (depth == 15 ? 16 : depth) + 7) / 8;
    result = BdReadCodecInput(file, scratch, capacity, 0, bytes, output_unit ? required : 0, available);
    if (result < 0 || !BdHasCodecOutput(out, end, width * output_unit))
      return -1;
    consumed = bytes;
    BdBmpPixels(out, scratch, width, depth, info, alpha, red, green, blue);
  } else {
    u32 pixels = 0;
    do {
      if (BdReadCodecInput(file, scratch, capacity, cursor, 1, 1, available) < 0)
        return -1;
      u32 count = scratch[cursor];
      cursor = 0;
      ++consumed;
      result = BdReadCodecInput(file, scratch, capacity, 0, 1, 1, available);
      if (result < 0)
        return -1;
      ++consumed;
      if (count) {
        pixels += count;
        if (depth == 1 || depth == 4 || depth == 8 || depth == 24) {
          if (!BdHasCodecOutput(out, end, count * output_unit))
            return -1;
          if (depth == 24) {
            result = BdReadCodecInput(file, scratch, capacity, 1, 2, 2, available);
            if (result < 0)
              return -1;
            consumed += 2;
          }
          u8 value = scratch[0];
          for (u32 x = 0; x < count; ++x)
            if (depth == 1)
              *out++ = (value >> (7 - (x & 7))) & 1;
            else if (depth == 4)
              *out++ = (value >> ((x & 1) ? 0 : 4)) & 15;
            else if (depth == 8)
              *out++ = value;
            else
              out = BdBmpPixels(out, scratch, 1, depth, info, alpha, red, green, blue);
        }
      } else {
        u32 literal_count = scratch[0];
        if (literal_count < 3) {
          if (pixels)
            break;
        } else {
          pixels += literal_count;
          u32 bytes = ((depth * literal_count + 15) / 8) & ~1u;
          bool packed = depth == 1 || depth == 4 || depth == 8 || depth == 24;
          result = BdReadCodecInput(
              file, scratch, capacity, 0, bytes, packed ? (depth * literal_count + 7) / 8 : 0, available);
          if (result < 0)
            return -1;
          consumed += bytes;
          if (depth == 1 || depth == 4 || depth == 8 || depth == 24) {
            if (!BdHasCodecOutput(out, end, literal_count * output_unit))
              return -1;
            out = BdBmpPixels(out, scratch, literal_count, depth, info, alpha, red, green, blue);
            cursor = depth == 1 ? literal_count / 8
                : depth == 4    ? literal_count / 2
                : depth == 8    ? literal_count
                                : literal_count * 3;
          }
        }
      }
    } while (pixels < width);
  }
  if ((consumed & 3) && BdReadCodecInput(file, scratch, capacity, cursor, (0u - consumed) & 3, 0, available) < 0)
    return -1;
  return result;
}

static bool BdCompleteBmpHeader(BdCodecFile *file, u8 *header, i32 read, i32 requested, bool alpha_mask) {
  if (requested == 26) {
    if (read != requested)
      return false;
    if (BdCodecValue(header + 14, 4) == 12)
      for (u32 i = 26; i < 34; ++i)
        header[i] = 0;
    else if (BdReadCodecBytes(file, header + 26, 8) != 8)
      return false;
    if (BdCodecValue(header + 30, 4) == 3)
      return false;
  } else {
    if (read < 34)
      return false;
    if (BdCodecValue(header + 30, 4) == 3 && read < (alpha_mask ? 70 : 66))
      return false;
  }
  return true;
}

// Read bottom-up bitmap rows using the original subtype offsets, channel masks and per-row integer results.
// Native input repair excludes the short-header branch's undefined stack read; full-header branches retain direct parity.
i32 BD_STDCALL BdReadBmpPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  BdCodecFile *file = state->file;
  i32 subtype = static_cast<i32>(info->subtype);
  BdSeekCodec(file, 0);
  u8 header[70];
  i32 requested = subtype < 2 ? 26 : 70;
  i32 result = BdReadCodecBytes(file, header, requested);
  if (!result)
    return result;
  if (!BdCompleteBmpHeader(file, header, result, requested, subtype == 2 && info->source_bits == 32))
    return 0;
  u32 depth = info->source_bits, alpha = 0, red = 0, green = 0, blue = 0;
  if (BdCodecValue(header + 30, 4) == 3) {
    red = BdCodecValue(header + 54, 4);
    green = BdCodecValue(header + 58, 4);
    blue = BdCodecValue(header + 62, 4);
    if (depth == 32 && subtype == 2)
      alpha = BdCodecValue(header + 66, 4);
  } else if (depth == 15) {
    red = 0x7C00;
    green = 0x03E0;
    blue = 0x001F;
  } else if (depth == 32) {
    red = kGxArgbRedMask;
    green = kGxArgbGreenMask;
    blue = kGxArgbBlueMask;
  }
  if (((depth >= 15 && depth <= 16) || depth == 32) &&
      (!info->red_bits || info->red_bits >= 32 || !info->green_bits || info->green_bits >= 32 ||
          !info->blue_bits || info->blue_bits >= 32 || (alpha && (!info->alpha_bits || info->alpha_bits >= 32))))
    return 0;
  u32 offset = BdCodecValue(header + 10, 4);
  if (!offset) {
    if (subtype == 1)
      offset = depth <= 8 ? 26 + 3 * info->palette_count : 26;
    else if (subtype == 2)
      offset = 122;
    else if (subtype == 3)
      offset = 66;
    else
      offset = depth <= 8 ? 54 + 4 * info->palette_count : 54;
  }
  result = BdSeekCodec(file, static_cast<i32>(offset));
  u64 row_size = ((static_cast<u64>(info->width) * (depth == 15 ? 16 : depth) + 31) >> 3) & ~3ull;
  if (row_size > INT32_MAX)
    return 0;
  u8 *scratch = static_cast<u8 *>(BdAllocateCodec(static_cast<i32>(row_size)));
  u32 available = 0;
  i32 height = static_cast<i32>(info->height);
  if (height > 0) {
    i64 stride = pitch < 0 ? -static_cast<i64>(pitch) : pitch;
    u64 row_output = static_cast<u64>(info->width) * (info->bits == 32 ? 4 : 1);
    u64 last_size = static_cast<u64>(stride) > row_output ? static_cast<u64>(stride) : row_output;
    if (static_cast<u64>(stride) * static_cast<u32>(height - 1) + last_size > INT32_MAX) {
      BdFreeCodec(scratch);
      return 0;
    }
    u8 *last_row = out + static_cast<isize>(pitch) * (height - 1);
    const u8 *end = (pitch < 0 ? out : last_row) + static_cast<isize>(last_size);
    u8 *row = last_row;
    for (i32 y = height; y; --y) {
      if (result)
        result = BdBmpRowInput(
            file, row, scratch, depth, info, alpha, red, green, blue, static_cast<u32>(row_size), &available, end);
      if (result < 0) {
        result = 0;
        break;
      }
      if (y > 1)
        row -= pitch;
    }
  }
  BdFreeCodec(scratch);
  return result;
}

// Quantize a channel with the original +128 integer rounding before left-aligning its bit field.
// Zero-width channels contribute zero without an undefined shift by32.
static u32 BdBmpQuantize(u8 value, u32 bits, u32 shift) {
  if (!bits)
    return 0;
  return ((((1u << bits) - 1) * value + 128) / 255) << shift;
}

static void BdPackBmpRow(
    const u8 *source, u8 *out, i32 width, i32 depth, i32, u32 alpha, u32 red, u32 green, u32 blue) {
  for (i32 x = 0; x < width; ++x) {
    if (depth == 16 || depth == 32) {
      u32 pixel = BdBmpQuantize(source[3], alpha, 32 - alpha) | BdBmpQuantize(source[2], red, 32 - alpha - red) |
          BdBmpQuantize(source[1], green, 32 - alpha - red - green) |
          BdBmpQuantize(source[0], blue, 32 - alpha - red - green - blue);
      if (depth == 16)
        pixel >>= 16;
      BdPutCodecValue(out, pixel, depth / 8);
      out += depth / 8;
      source += 4;
    } else if (depth == 24) {
      out[0] = source[0];
      out[1] = source[1];
      out[2] = source[2];
      out += 3;
      source += 4;
    } else if (depth == 8)
      *out++ = *source++;
    else if (depth == 4) {
      if (!(x & 1))
        *out = static_cast<u8>(*source << 4);
      else
        *out++ |= *source & 15;
      ++source;
    } else if (depth == 1) {
      if (!(x & 7))
        *out = 0;
      *out |= (*source++ & 1) << (7 - (x & 7));
      if ((x & 7) == 7)
        ++out;
    }
  }
}

static u32 BdBmpMask(u32 bits, u32 shift) {
  return bits ? ((1u << bits) - 1) << shift : 0;
}

// Write original Windows/V3/V4 bitmap headers and bottom-up packed rows, preserving the declared-size formula.
i32 BD_STDCALL BdWriteBmpPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  i32 source_depth = static_cast<i32>(info->source_bits), subtype = static_cast<i32>(info->subtype);
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  i32 depth = source_depth <= 1 ? 1
      : source_depth <= 4       ? 4
      : source_depth <= 8       ? 8
      : source_depth <= 16      ? 16
      : source_depth <= 24      ? 24
                                : 32;
  i32 count = depth > 8 ? 0 : static_cast<i32>(info->palette_count);
  if (count < 0 || count > 256)
    return 0;
  u8 header[122] = {};
  u32 dib = 40, extra = 0, compression = 0, alpha = 0, red = 0, green = 0, blue = 0;
  bool channels = false;
  if (subtype == 2) {
    compression = 3;
    dib = 108;
    if (depth == 16 && static_cast<i32>(info->red_bits) <= 5 && static_cast<i32>(info->blue_bits) <= 5 &&
        static_cast<i32>(info->green_bits) <= 6 && static_cast<i32>(info->alpha_bits) < 1) {
      red = blue = 5;
      green = static_cast<i32>(info->green_bits) > 5 ? 6 : 5;
      alpha = green == 6 ? 0 : 1;
      channels = true;
      BdPutCodecValue(header + 54, green == 6 ? 0xF800 : 0x7C00, 4);
      BdPutCodecValue(header + 58, green == 6 ? 0x07E0 : 0x03E0, 4);
      BdPutCodecValue(header + 62, 31, 4);
    } else if (depth == 32) {
      alpha = red = green = blue = 8;
      channels = true;
      BdPutCodecValue(header + 54, kGxArgbRedMask, 4);
      BdPutCodecValue(header + 58, kGxArgbGreenMask, 4);
      BdPutCodecValue(header + 62, kGxArgbBlueMask, 4);
      BdPutCodecValue(header + 66, kGxArgbAlphaMask, 4);
    }
    BdPutCodecValue(header + 70, 1, 4);
  } else if (subtype == 3) {
    compression = 3;
    extra = 12;
    if (depth == 16 && info->alpha_bits + info->red_bits + info->green_bits + info->blue_bits > 16)
      depth = 32;
    i32 values[4] = {static_cast<i32>(info->alpha_bits), static_cast<i32>(info->red_bits),
        static_cast<i32>(info->green_bits), static_cast<i32>(info->blue_bits)};
    for (i32 &value : values) {
      if (value < 0)
        return 0;
      if (value >= 8)
        value = 8;
    }
    alpha = static_cast<u32>(values[0]);
    red = static_cast<u32>(values[1]);
    green = static_cast<u32>(values[2]);
    blue = static_cast<u32>(values[3]);
    channels = true;
    BdPutCodecValue(header + 66, BdBmpMask(alpha, blue + green + red), 4);
    BdPutCodecValue(header + 54, BdBmpMask(red, blue + green), 4);
    BdPutCodecValue(header + 58, BdBmpMask(green, blue), 4);
    BdPutCodecValue(header + 62, BdBmpMask(blue, 0), 4);
  } else if (depth == 16) {
    red = blue = 5;
    green = subtype == 4 ? 6 : 5;
    alpha = subtype == 4 ? 0 : 1;
    channels = true;
  } else if (depth == 32) {
    alpha = red = green = blue = 8;
    channels = true;
  }
  u64 row_bytes = ((static_cast<u64>(info->width) * static_cast<u32>(depth) + 31) & ~31ull) / 8;
  if (row_bytes > INT32_MAX)
    return 0;
  u32 palette_bytes = static_cast<u32>(count) * 4;
  u32 declared = info->height * (static_cast<u32>(depth) * ((info->width + 3) & ~3u) / 8);
  header[0] = 'B';
  header[1] = 'M';
  BdPutCodecValue(header + 2, extra + declared + palette_bytes + dib + 14, 4);
  BdPutCodecValue(header + 10, palette_bytes + extra + dib + 14, 4);
  BdPutCodecValue(header + 14, dib, 4);
  BdPutCodecValue(header + 18, info->width, 4);
  BdPutCodecValue(header + 22, info->height, 4);
  BdPutCodecValue(header + 26, 1, 2);
  BdPutCodecValue(header + 28, static_cast<u32>(depth), 2);
  BdPutCodecValue(header + 30, compression, 4);
  BdPutCodecValue(header + 34, declared, 4);
  BdPutCodecValue(header + 46, static_cast<u32>(count), 4);
  i32 result = static_cast<i32>(BdWriteCodecBytes(state->file, header, extra + dib + 14));
  if (depth <= 8 && count) {
    u8 palette[1024];
    for (i32 i = 0; i < count; ++i)
      BdPutCodecValue(palette + i * 4, info->palette[i], 4);
    result = static_cast<i32>(BdWriteCodecBytes(state->file, palette, palette_bytes));
  }
  u8 *row = static_cast<u8 *>(BdAllocateCodec(static_cast<i32>(row_bytes)));
  for (u32 i = 0; i < row_bytes; ++i)
    row[i] = 0;
  if (height > 0) {
    const u8 *input = source + static_cast<isize>(pitch) * (height - 1);
    for (i32 y = 0; y < height && result; ++y) {
      if ((depth == 16 || depth == 32) && !channels) {
        BdFreeCodec(row);
        return 0;
      }
      BdPackBmpRow(input, row, width, depth, subtype, alpha, red, green, blue);
      result = static_cast<i32>(BdWriteCodecBytes(state->file, row, static_cast<u32>(row_bytes)));
      if (y + 1 < height)
        input -= pitch;
    }
  }
  BdFreeCodec(row);
  return result;
}

// Build one of the three proven descriptor records without changing global reader selection.
// Direct format methods and the selected dispatch share these fixed field and string initializers.
static BdCodecDescriptor *BdDescribeFormat(u32 codec) {
  BdCodecDescriptor *description = static_cast<BdCodecDescriptor *>(BdAllocateCodec(sizeof(BdCodecDescriptor)));
  memset(description, 0, sizeof(*description));
  description->length = 220;
  description->version = 200;
  description->quality = 100;
  strcpy(description->author, "FrANK G. Barchard");
  if (codec == 1) {
    description->signature = kGxBitmapCodecSignature;
    description->flags = 0x93C7;
    description->format = kGxBitmapFormatTag;
    description->field_020 = kGxBitmapExtensionTag;
    strcpy(description->extensions[0], ".bmp");
    strcpy(description->extensions[1], ".rle");
    strcpy(description->release, "1.21");
    strcpy(description->short_name, "BMP");
    strcpy(description->type, "BMP");
    strcpy(description->description, "Windows BMP");
  } else if (codec == 2) {
    description->signature = kGxTargaCodecSignature;
    description->flags = 0x93D7;
    description->format = kGxTargaFormatTag;
    strcpy(description->extensions[0], ".tga");
    strcpy(description->extensions[1], ".vda");
    strcpy(description->extensions[2], ".icb");
    strcpy(description->extensions[3], ".vst");
    strcpy(description->release, "1.35");
    strcpy(description->short_name, "TGA");
    strcpy(description->type, "Targa");
    strcpy(description->description, "Targa");
  } else {
    description->signature = kGxNilCodecSignature;
    description->flags = 0x1000;
    strcpy(description->release, "1.02");
    strcpy(description->short_name, "Nil");
    strcpy(description->type, "Nil");
    strcpy(description->description, "Nil Gimex");
  }
  return description;
}

// Return a fresh descriptor for the current linked codec. Selection remains a borrowed module global.
BdCodecDescriptor *BdDescribeCodec() {
  return BdDescribeFormat(BdCodecCurrent);
}

// Return the linked nil descriptor directly, preserving global selection during name-based scoring.
// Its callback body starts at the selected dispatch's original nil-table target1203CD80.
BdCodecDescriptor *BdDescribeNilCodec() {
  return BdDescribeFormat(0);
}

// Return the fixed BMP descriptor without changing the module's current codec selection.
BdCodecDescriptor *BdDescribeBmpCodec() {
  return BdDescribeFormat(1);
}

// Return the fixed Targa descriptor without changing the module's current codec selection.
BdCodecDescriptor *BdDescribeTgaCodec() {
  return BdDescribeFormat(2);
}

// Dispatch the original four pixel-reader arguments through the globally selected linked codec.
i32 BD_STDCALL BdReadCodecPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  if (BdCodecCurrent == 1)
    return BdReadBmpPixels(state, info, out, pitch);
  if (BdCodecCurrent == 2)
    return BdReadTgaPixels(state, info, out, pitch);
  return 0;
}

// Release separate metadata and pixel allocations unless the metadata is stored directly after the original image record.
void BD_STDCALL BdDestroyCodecPixels(BdCodecPixels *pixels) {
  if (pixels) {
    if (reinterpret_cast<u8 *>(pixels->info) != reinterpret_cast<u8 *>(pixels + 1)) {
      if (pixels->pixels)
        BdFreeCodec(pixels->pixels);
      if (pixels->info)
        BdFreeCodec(pixels->info);
    }
    BdFreeCodec(pixels);
  }
}

// Open the requested image, decode its selected frame and retain metadata, pixels and signed DWORD pitch in one result.
// Failed reads release their unreturned allocations.
BdCodecPixels *BD_STDCALL BdDecodeImage(const char *path, i32 index) {
  BdCodecState *state = nullptr;
  BdCodecPixels *pixels = nullptr;
  i32 complete = 0;
  u32 selected = BdSelectedCodec();
  BdCodecFile *file = BdOpenCodec(path, nullptr);
  if (file) {
    i32 count = BdReadCodecInfo(&state, file, path);
    if (count) {
      BdCodecDescriptor *description = BdDescribeCodec();
      if (index < count) {
        BdCodecInfo *info = BdCodecInfoAt(state, index);
        if (info) {
          u64 row_bits = static_cast<u64>(info->width) * info->bits;
          if (row_bits > INT32_MAX || (info->height && row_bits > INT32_MAX / info->height)) {
            BdFreeCodec(info);
          } else {
            i32 length = static_cast<i32>(row_bits * info->height) / 8, pitch = static_cast<i32>(row_bits) / 8;
            u8 *data = static_cast<u8 *>(BdAllocateCodec(length));
            complete = BdReadCodecPixels(state, info, data, pitch);
            if (complete) {
              pixels = static_cast<BdCodecPixels *>(BdAllocateCodec(sizeof(BdCodecPixels)));
              pixels->info = info;
              pixels->pixels = data;
              pixels->pitch = static_cast<u32>(pitch);
              complete = 1;
            } else {
              BdFreeCodec(data);
              BdFreeCodec(info);
            }
          }
        }
      }
      BdFreeCodec(description);
    }
  }
  if (state)
    BdCloseCodecState(state);
  if (file)
    BdCloseCodecFile(file);
  if (!complete) {
    BdDestroyCodecPixels(pixels);
    pixels = nullptr;
  }
  BdSelectCodec(selected);
  return pixels;
}

static_assert(sizeof(BdCodecInfo) == 1660);

// Recognize the original two-byte bitmap signature and preserve its100-point confidence result.
i32 BD_STDCALL BdProbeBmp(BdCodecFile *file) {
  BdSeekCodec(file, 0);
  u8 signature[2];
  i32 read = BdReadCodecBytes(file, signature, 2);
  if (read != 2)
    return 0;
  return signature[0] == 'B' && signature[1] == 'M' ? 100 : 0;
}

// Score valid Targa image/depth combinations at60, raising confidence to100 for the original bounded metadata ranges.
i32 BD_STDCALL BdProbeTga(BdCodecFile *file) {
  BdSeekCodec(file, 0);
  u8 header[18];
  i32 read = BdReadCodecBytes(file, header, 18);
  if (read < 17)
    return 0;
  u32 type = header[2], depth = header[16];
  if (header[1] > 1 || !((type >= 1 && type <= 3) || (type >= 9 && type <= 11)) ||
      !(depth == 8 || depth == 15 || depth == 16 || depth == 24 || depth == 32))
    return 0;
  u32 width = BdCodecValue(header + 12, 2), height = BdCodecValue(header + 14, 2);
  return width > 0 && width <= 2048 && height > 0 && height <= 2048 && BdCodecValue(header + 3, 2) < 256 &&
          BdCodecValue(header + 5, 2) <= 256 && header[7] <= 32 && BdCodecValue(header + 8, 2) <= 1024 &&
          BdCodecValue(header + 10, 2) <= 1024
      ? 100
      : 60;
}

// Allocate and zero the real reader record, setting its fixed version and eight-bit RGB-component defaults.
static BdCodecInfo *BdNewCodecInfo(u32 signature) {
  BdCodecInfo *info = static_cast<BdCodecInfo *>(BdAllocateCodec(sizeof(BdCodecInfo)));
  memset(info, 0, sizeof(*info));
  info->signature = signature;
  info->length = 1660;
  info->version = 200;
  info->field_430 = 100;
  info->red_bits = 8;
  info->green_bits = 8;
  info->blue_bits = 8;
  return info;
}

// Count only the first contiguous run of mask bits after its initial zeros, matching the original BITFIELDS helper.
static u32 BdMaskWidth(u32 mask) {
  u32 bits = 0;
  if (mask) {
    while (!(mask & 1))
      mask >>= 1;
    do {
      mask >>= 1;
      ++bits;
    } while (mask & 1);
  }
  return bits;
}

// Recover OS/2, Windows and V4 bitmap metadata and palette values in the original1660-byte reader record.
BdCodecInfo *BD_STDCALL BdReadBmpInfo(BdCodecState *state, i32) {
  BdCodecFile *file = state->file;
  BdSeekCodec(file, 0);
  u8 header[122] = {}, palette[1024];
  if (!BdReadCodecBytes(file, header, 18))
    return nullptr;
  u32 dib = BdCodecValue(header + 14, 4);
  if (dib > 108)
    return nullptr;
  i32 read = BdReadCodecBytes(file, header + 18, static_cast<i32>(dib) - 4);
  u32 width, height, bits, compression = 0, subtype = 0;
  i32 colors;
  if (dib == 12) {
    subtype = 1;
    width = BdCodecValue(header + 18, 2);
    height = BdCodecValue(header + 20, 2);
    bits = BdCodecValue(header + 24, 2);
    if (bits >= 32)
      return nullptr;
    colors = static_cast<i32>(1u << bits);
  } else {
    width = BdCodecValue(header + 18, 4);
    height = BdCodecValue(header + 22, 4);
    bits = BdCodecValue(header + 28, 2);
    colors = static_cast<i32>(BdCodecValue(header + 46, 4));
    compression = BdCodecValue(header + 30, 4);
    if (compression == 3 && dib == 40) {
      subtype = 3;
      read = BdReadCodecBytes(file, header + 54, 12);
    }
    if (dib == 108)
      subtype = 2;
  }
  if (!read)
    return nullptr;
  BdCodecInfo *info = BdNewCodecInfo(kGxBitmapCodecSignature);
  info->width = width;
  info->height = height;
  info->subtype = subtype;
  info->compressed = compression == 1 || compression == 2;
  info->bits = bits <= 8 ? 8 : 32;
  if (bits <= 8) {
    if (!bits)
      bits = 8;
    if (!colors)
      colors = 1 << bits;
    if (colors > 256)
      colors = 256;
    if (colors) {
      if (colors < 0) {
        BdFreeCodec(info);
        return nullptr;
      }
      i32 bytes = colors * (subtype == 1 ? 3 : 4);
      read = BdReadCodecBytes(file, palette, bytes);
      if (colors > 0) {
        if (read != bytes) {
          BdFreeCodec(info);
          return nullptr;
        }
        u32 alpha = 0;
        for (i32 i = 0; i < colors; ++i) {
          const u8 *color = palette + i * (subtype == 1 ? 3 : 4);
          u32 opacity = subtype == 1 ? 255 : color[3];
          info->palette[i] =
              color[0] | (static_cast<u32>(color[1]) << 8) | (static_cast<u32>(color[2]) << 16) | (opacity << 24);
          alpha += opacity != 0;
        }
        if (subtype != 1) {
          if (alpha)
            info->alpha_bits = 8;
          else
            for (i32 i = 0; i < colors; ++i)
              info->palette[i] |= kGxArgbAlphaMask;
        }
      }
    }
    info->source_bits = bits;
    info->palette_count = static_cast<u32>(colors);
  } else {
    info->source_bits = bits;
    if (compression == 3) {
      if (bits == 32 && subtype == 2)
        info->alpha_bits = BdMaskWidth(BdCodecValue(header + 66, 4));
      info->red_bits = BdMaskWidth(BdCodecValue(header + 54, 4));
      info->green_bits = BdMaskWidth(BdCodecValue(header + 58, 4));
      info->blue_bits = BdMaskWidth(BdCodecValue(header + 62, 4));
    } else if (bits == 16) {
      info->source_bits = 15;
      info->red_bits = 5;
      info->green_bits = 5;
      info->blue_bits = 5;
    }
  }
  if (!read) {
    BdFreeCodec(info);
    return nullptr;
  }
  return info;
}

// Recover Targa dimensions, orientation, alpha depth and color-map expansion without assuming the file extension.
// Preserve the original palette-length loops and grayscale defaults while excluding external palette overruns.
BdCodecInfo *BD_STDCALL BdReadTgaInfo(BdCodecState *state, i32) {
  BdCodecFile *file = state->file;
  BdSeekCodec(file, 0);
  u8 header[18], palette[1024];
  i32 read = BdReadCodecBytes(file, header, 18);
  if (read != 18)
    return nullptr;
  u32 width = BdCodecValue(header + 12, 2), height = BdCodecValue(header + 14, 2), bits = header[16];
  u32 first = BdCodecValue(header + 3, 2), length = BdCodecValue(header + 5, 2), palette_bits = header[7];
  u32 type = header[2], count = (type & 7) == 1 ? length : (type & 7) == 3 ? 256 : 0;
  read = BdSeekCodec(file, header[0] + 18);
  BdCodecInfo *info = BdNewCodecInfo(kGxTargaCodecSignature);
  info->width = width;
  info->height = height;
  info->file_size = static_cast<u32>(file->length);
  info->compressed = (type & 8) != 0;
  info->alpha_bits = bits > 24 ? bits - 24 : 0;
  if (info->alpha_bits > 8)
    info->alpha_bits = 8;
  info->x_origin = static_cast<i16>(BdCodecValue(header + 8, 2));
  info->y_origin = static_cast<i16>(BdCodecValue(header + 10, 2));
  if (!(header[17] & 0x20))
    info->y_origin = -info->y_origin;
  if (header[17] & 0x10)
    info->x_origin = -info->x_origin;
  if (bits < 15) {
    if (!bits)
      bits = 8;
    if (!count)
      count = 1u << bits;
    if (count > 256)
      count = 256;
    info->bits = 8;
    info->source_bits = bits;
    info->palette_first = first;
    info->palette_count = count;
    if (header[1]) {
      if (palette_bits == 15 || palette_bits == 16 || palette_bits == 24 || palette_bits == 32) {
        if (length && first + length > 256) {
          BdFreeCodec(info);
          return nullptr;
        }
        u32 stride = palette_bits <= 16 ? 2 : palette_bits / 8;
        read = BdReadCodecBytes(file, palette, static_cast<i32>(count * stride));
        if (read < static_cast<i32>(length * stride)) {
          BdFreeCodec(info);
          return nullptr;
        }
        if (palette_bits <= 16) {
          info->alpha_bits = 0;
          info->red_bits = 5;
          info->green_bits = 5;
          info->blue_bits = 5;
        } else {
          info->alpha_bits = palette_bits == 32 ? 8 : 0;
        }
        for (u32 i = 0; i < length; ++i) {
          const u8 *color = palette + i * stride;
          u32 blue, green, red, opacity = 255;
          if (stride == 2) {
            u32 pixel = BdCodecValue(color, 2);
            blue = kGxArgbBlueMask * (pixel & 31) / 31;
            green = kGxArgbBlueMask * ((pixel >> 5) & 31) / 31;
            red = kGxArgbBlueMask * ((pixel >> 10) & 31) / 31;
          } else {
            blue = color[0];
            green = color[1];
            red = color[2];
            if (stride == 4)
              opacity = color[3];
          }
          info->palette[first + i] = blue | (green << 8) | (red << 16) | (opacity << 24);
        }
      }
    } else {
      info->alpha_bits = 0;
      for (u32 i = 0; i < 256; ++i)
        info->palette[i] = i | (i << 8) | (i << 16) | kGxArgbAlphaMask;
    }
  } else {
    info->bits = 32;
    info->source_bits = bits == 16 ? 15 : bits;
    if (bits <= 16) {
      info->red_bits = 5;
      info->green_bits = 5;
      info->blue_bits = 5;
    }
    if (bits < 32)
      info->alpha_bits = 0;
  }
  if (!read) {
    BdFreeCodec(info);
    return nullptr;
  }
  return info;
}

// Bound consumption of external row input while retaining bytes initialized by preceding reads into the same buffer.
// Later short reads preserve defined previous-row bytes; return -1 when required bytes are unavailable.
i32 BdReadCodecInput(
    BdCodecFile *file, u8 *buffer, u32 capacity, u32 offset, u32 requested, u32 required, u32 *available) {
  if (offset > capacity || requested > capacity - offset || required > capacity - offset)
    return -1;
  i32 read = BdReadCodecBytes(file, buffer + offset, static_cast<i32>(requested));
  if (read && offset <= *available && offset + static_cast<u32>(read) > *available)
    *available = offset + static_cast<u32>(read);
  if (offset + required > *available)
    return -1;
  return read;
}

bool BdHasCodecOutput(const u8 *out, const u8 *end, u32 length) {
  return out <= end && static_cast<usize>(end - out) >= length;
}

u32 BdCodecCurrent;

// Publish the selected reader, clamping unsupported unsigned indexes to the nil codec.
// Return the index used by subsequent metadata and pixel operations.
i32 BD_STDCALL BdSelectCodec(u32 index) {
  if (index > 2)
    index = 0;
  BdCodecCurrent = index;
  return static_cast<i32>(index);
}

u32 BdSelectedCodec() {
  return BdCodecCurrent;
}

// Bind the original generic codec allocation to ordinary array allocation, excluding its sentinel heap machinery.
// Negative lengths are rejected; allocation exhaustion uses the project's global new handler.
void *BD_STDCALL BdAllocateCodec(i32 length) {
  if (length < 0)
    return nullptr;
  return new u8[static_cast<u32>(length)];
}

// Free a valid codec allocation through its matching native array owner; null retains the original zero result.
// The original allocator's diagnostic sentinel probing is not introduced into production objects.
i32 BD_STDCALL BdFreeCodec(void *allocation) {
  if (!allocation)
    return 0;
  delete[] static_cast<u8 *>(allocation);
  return 1;
}

// Consume the current read window, then refill up to8192 remaining logical bytes until the request is satisfied.
// Preserve actual partial counts and unread buffer bytes rather than manufacturing a terminated or zero-filled file.
i32 BD_STDCALL BdReadCodecBytes(BdCodecFile *file, u8 *out, i32 length) {
  i32 total = 0;
  while (length > 0) {
    iptr available = static_cast<iptr>(reinterpret_cast<uptr>(file->read_end) - reinterpret_cast<uptr>(file->read_ptr));
    i32 take = available > length ? length : static_cast<i32>(available);
    if (take > 0) {
      memcpy(out, file->read_ptr, static_cast<u32>(take));
      file->read_ptr += take;
      file->position += take;
      out += take;
      length -= take;
      total += take;
    }
    if (file->read_end == file->read_ptr) {
      if (!length)
        return total;
      i32 remaining = file->length - file->position;
      file->read_ptr = file->buffer;
      file->read_end = file->buffer;
      if (remaining > 8192)
        remaining = 8192;
      if (remaining <= 0)
        return total;
      usize read = fread(file->buffer, 1, static_cast<u32>(remaining), file->stream);
      if (!read)
        return total;
      file->read_end += read;
    }
  }
  return total;
}

// Seek within an existing read window when possible, otherwise reset it before the real C stream seek.
// Integer cursor arithmetic avoids forming an out-of-array C++ pointer before the original range test.
i32 BD_STDCALL BdSeekCodec(BdCodecFile *file, i32 position) {
  bool valid = true;
  if (position < 0 || (!file->write_mode && position > file->length)) {
    position = position < 0 ? 0 : file->length;
    valid = false;
    file->read_ptr = file->buffer;
    file->read_end = file->buffer;
  } else {
    file->read_ptr =
        reinterpret_cast<u8 *>(reinterpret_cast<uptr>(file->read_ptr) + static_cast<iptr>(position - file->position));
  }
  file->position = position;
  if (file->write_mode)
    return fseek(file->stream, position, SEEK_SET) == 0 && valid;
  iptr available = static_cast<iptr>(reinterpret_cast<uptr>(file->read_end) - reinterpret_cast<uptr>(file->read_ptr));
  iptr offset = static_cast<iptr>(reinterpret_cast<uptr>(file->read_ptr) - reinterpret_cast<uptr>(file->buffer));
  if (available <= 0 || available > 8192 || offset < 0) {
    valid = position >= 0 && position <= file->length && fseek(file->stream, position, SEEK_SET) == 0;
    file->read_ptr = file->buffer;
    file->read_end = file->buffer;
  }
  return valid;
}

// Read one little-endian scalar without imposing alignment on an external image header.
u32 BdCodecValue(const u8 *bytes, u32 count) {
  u32 value = 0;
  for (u32 i = 0; i < count; ++i)
    value |= static_cast<u32>(bytes[i]) << (8 * i);
  return value;
}

// Open the original read-only C stream, including the reserved con: input identity, and capture its32-bit length.
// A failed ftell remains negative so the original zero-readable-byte and close behavior can occur.
static BdCodecFile *BdOpenCodecFile(const char *path) {
  FILE *stream;
  if (!strncmp(path, "con:", 4))
    stream = stdin;
  else {
    ScString native = ScNativeFilePath(path);
    stream = fopen(native.Data(), "rb");
  }
  if (!stream)
    return nullptr;
  fseek(stream, 0, SEEK_END);
  long length = ftell(stream);
  fseek(stream, 0, SEEK_SET);
  if (length > INT32_MAX) {
    fclose(stream);
    return nullptr;
  }
  BdCodecFile *file = static_cast<BdCodecFile *>(BdAllocateCodec(sizeof(BdCodecFile)));
  file->stream = stream;
  file->position = 0;
  file->length = static_cast<i32>(length);
  file->read_ptr = file->buffer;
  file->read_end = file->buffer;
  file->write_mode = 0;
  file->field_018 = 0;
  return file;
}

// Close the real stream before freeing its descriptor, retaining the original success result for a null file.
i32 BD_STDCALL BdCloseCodecFile(BdCodecFile *file) {
  if (!file)
    return 1;
  i32 closed = fclose(file->stream) == 0;
  return BdFreeCodec(file) & closed;
}

// Return the selected reader's original confidence score; the nil codec has no readable formats.
i32 BD_STDCALL BdCanReadCodec(BdCodecFile *file) {
  if (BdCodecCurrent == 1)
    return BdProbeBmp(file);
  if (BdCodecCurrent == 2)
    return BdProbeTga(file);
  return 0;
}

// Score the actual three descriptors against their names or file probes, retaining extension preference and first-best ties.
// Direct format descriptors leave selection unchanged until the final selected index is published.
i32 BD_STDCALL BdSelectCodecForFile(const char *path, BdCodecFile *file) {
  i32 best = 0;
  u32 selected = 0;
  const char *extension = strrchr(path, '.');
  for (u32 codec = 0; codec < 3 && best < 100; ++codec) {
    BdCodecDescriptor *description = codec == 1 ? BdDescribeBmpCodec()
        : codec == 2                            ? BdDescribeTgaCodec()
                                                : BdDescribeNilCodec();
    i32 confidence = 0;
    if (!(description->flags & 0x8000)) {
      if (!ScCompareNoCase(path, description->short_name))
        confidence = 50;
      if (!ScCompareNoCase(path, description->type))
        confidence = 80;
      if (!ScCompareNoCase(path, description->description))
        confidence = 95;
    } else if (file) {
      confidence = codec == 1 ? BdProbeBmp(file) : BdProbeTga(file);
      if (confidence > 10) {
        bool matches = false;
        if (extension)
          for (u32 i = 0; i < 8; ++i)
            if (!ScCompareNoCase(extension, description->extensions[i]))
              matches = true;
        if (!matches)
          confidence -= 10;
      }
    }
    if (confidence > best) {
      best = confidence;
      selected = codec;
    }
    BdFreeCodec(description);
  }
  BdSelectCodec(selected);
  return static_cast<i32>(selected);
}

// Open a supplied path or try the original BMP/RLE/TGA/VDA/ICB/VST suffixes, then select the matching reader.
BdCodecFile *BD_STDCALL BdOpenCodec(const char *path, char *resolved_path) {
  usize length = strlen(path);
  if (length >= 512) {
    BdSelectCodec(0);
    if (resolved_path)
      resolved_path[0] = 0;
    return nullptr;
  }
  char resolved[512];
  strcpy(resolved, path);
  BdCodecFile *file = BdOpenCodecFile(resolved);
  if (!file) {
    if (length + 5 > 512) {
      BdSelectCodec(0);
      if (resolved_path)
        resolved_path[0] = 0;
      return nullptr;
    }
    const char *extensions[6] = {".bmp", ".rle", ".tga", ".vda", ".icb", ".vst"};
    for (u32 i = 0; i < 6; ++i) {
      strcpy(resolved + length, extensions[i]);
      file = BdOpenCodecFile(resolved);
      if (file) {
        i32 confidence = i < 2 ? BdProbeBmp(file) : BdProbeTga(file);
        if (confidence)
          break;
        BdCloseCodecFile(file);
        file = nullptr;
      }
    }
  }
  BdSelectCodec(file ? BdSelectCodecForFile(path, file) : 0);
  if (file && !BdCodecCurrent) {
    resolved[0] = 0;
    BdCloseCodecFile(file);
    file = nullptr;
  }
  if (resolved_path)
    strcpy(resolved_path, resolved);
  return file;
}

// Create the actual one-image reader state with its fixed PC record fields and borrowed native file pointer.
static i32 BdCreateReader(BdCodecState **out, BdCodecFile *file, u32 signature) {
  BdCodecState *state = static_cast<BdCodecState *>(BdAllocateCodec(sizeof(BdCodecState)));
  memset(state, 0, sizeof(*state));
  state->signature = signature;
  state->length = 24;
  state->field_008 = 1;
  state->file = file;
  *out = state;
  return 1;
}

i32 BD_STDCALL BdOpenBmpReader(BdCodecState **out, BdCodecFile *file, const char *) {
  return BdCreateReader(out, file, kGxBitmapCodecSignature);
}

i32 BD_STDCALL BdOpenTgaReader(BdCodecState **out, BdCodecFile *file, const char *) {
  return BdCreateReader(out, file, kGxTargaCodecSignature);
}

// Forward all three original reader-open arguments through the selected format. Nil leaves the output unchanged.
i32 BD_STDCALL BdReadCodecInfo(BdCodecState **out, BdCodecFile *file, const char *path) {
  if (BdCodecCurrent == 1)
    return BdOpenBmpReader(out, file, path);
  if (BdCodecCurrent == 2)
    return BdOpenTgaReader(out, file, path);
  return 0;
}

// Dispatch metadata through the global selected codec; both original readers ignore the single-image index argument.
BdCodecInfo *BD_STDCALL BdCodecInfoAt(BdCodecState *state, i32 index) {
  if (BdCodecCurrent == 1)
    return BdReadBmpInfo(state, index);
  if (BdCodecCurrent == 2)
    return BdReadTgaInfo(state, index);
  return nullptr;
}

i32 BD_STDCALL BdCloseCodecState(BdCodecState *state) {
  return BdCodecCurrent && state ? BdFreeCodec(state) : 0;
}

i32 BD_STDCALL BdCloseCodecStream(BdCodecState *state) {
  return BdFreeCodec(state);
}

i32 BdCodecCount() {
  return 3;
}

// Copy one index, expand a 5:5:5 source word, or copy BGR/BGRA bytes into the original output representation.
// Unsupported source depths leave the output pointer unchanged, matching the row converter's unused branches.
static u8 *BdTgaPixel(u8 *out, const u8 *pixel, u32 depth) {
  if (depth == 8) {
    *out++ = *pixel;
  } else if (depth >= 15 && depth <= 16) {
    u32 value = BdCodecValue(pixel, 2);
    out[0] = static_cast<u8>(255 * (value & 31) / 31);
    out[1] = static_cast<u8>(255 * ((value >> 5) & 31) / 31);
    out[2] = static_cast<u8>(255 * ((value >> 10) & 31) / 31);
    out[3] = 255;
    out += 4;
  } else if (depth == 24 || depth == 32) {
    out[0] = pixel[0];
    out[1] = pixel[1];
    out[2] = pixel[2];
    out[3] = depth == 32 ? pixel[3] : 255;
    out += 4;
  }
  return out;
}

// Decode one raw or packet-compressed row, returning the actual final input read count.
// Native boundary parameters protect the real scratch/plane owners while preserving defined stale bytes and run spans.
static i32 BdTgaRowInput(
    BdCodecFile *file, u8 *out, u8 *scratch, const BdCodecInfo *info, u32 *available, const u8 *end) {
  u32 width = info->width, depth = info->source_bits, unit = ((depth + 7) & ~7u) >> 3;
  u32 capacity = width * unit;
  u32 output_unit = depth == 8 ? 1 : (depth >= 15 && depth <= 16) || depth == 24 || depth == 32 ? 4 : 0;
  if (!info->compressed) {
    i32 read = BdReadCodecInput(file, scratch, capacity, 0, capacity, output_unit ? capacity : 0, available);
    if (read < 0 || !BdHasCodecOutput(out, end, width * output_unit))
      return -1;
    if (output_unit)
      for (u32 x = 0; x < width; ++x)
        out = BdTgaPixel(out, scratch + x * unit, depth);
    return read;
  }
  u32 pixels = 0, cursor = 0;
  i32 read;
  do {
    read = BdReadCodecInput(file, scratch, capacity, cursor, 1, 1, available);
    if (read < 0)
      return -1;
    u8 packet = scratch[cursor];
    u32 count = (packet & 127u) + 1;
    pixels += count;
    if (!BdHasCodecOutput(out, end, count * output_unit))
      return -1;
    cursor = 0;
    if (!(packet & 128)) {
      u32 bytes = count * unit;
      read = BdReadCodecInput(file, scratch, capacity, 0, bytes, output_unit ? bytes : 0, available);
      if (read < 0)
        return -1;
      if (output_unit) {
        for (u32 x = 0; x < count; ++x)
          out = BdTgaPixel(out, scratch + x * unit, depth);
        cursor = bytes;
      }
    } else if (output_unit) {
      read = BdReadCodecInput(file, scratch, capacity, 0, unit, unit, available);
      if (read < 0)
        return -1;
      for (u32 x = 0; x < count; ++x)
        out = BdTgaPixel(out, scratch, depth);
    }
  } while (pixels < width);
  return read;
}

// Reverse the original row's indices or DWORD pixels in place without touching padding or neighboring rows.
static void BdMirrorTgaRow(u8 *row, i32 width, u32 bits) {
  u32 unit = bits == 32 ? 4 : 1;
  for (i32 x = 0; x < (width >> 1); ++x) {
    u8 *first = row + static_cast<usize>(x) * unit;
    u8 *last = row + static_cast<usize>(width - x - 1) * unit;
    for (u32 byte = 0; byte < unit; ++byte) {
      u8 saved = first[byte];
      first[byte] = last[byte];
      last[byte] = saved;
    }
  }
}

// Read Targa rows after the ID and optional color map, preserving vertical order, packet results and horizontal mirroring.
// Byte-count and plane guards are separate native input boundaries; no new game status replaces the original integer result.
i32 BD_STDCALL BdReadTgaPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  BdCodecFile *file = state->file;
  BdSeekCodec(file, 0);
  u8 header[18];
  i32 result = BdReadCodecBytes(file, header, 18);
  if (result != 18)
    return 0;
  u32 colors = BdCodecValue(header + 5, 2);
  i32 offset = header[0] + 18;
  if (header[1])
    offset += static_cast<i32>(colors * ((header[7] + 7u) / 8));
  result = BdSeekCodec(file, offset);
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  u64 row_size = static_cast<u64>(info->width) * (((info->source_bits + 7u) & ~7u) >> 3);
  if (row_size > INT32_MAX)
    return 0;
  u8 *scratch = static_cast<u8 *>(BdAllocateCodec(static_cast<i32>(row_size)));
  u32 available = 0;
  if (height > 0) {
    i64 stride = pitch < 0 ? -static_cast<i64>(pitch) : pitch;
    u64 row_output = static_cast<u64>(info->width) * (info->bits == 32 ? 4 : 1);
    u64 span = static_cast<u64>(stride) * static_cast<u32>(height - 1) +
        (static_cast<u64>(stride) > row_output ? static_cast<u64>(stride) : row_output);
    if (span > INT32_MAX) {
      BdFreeCodec(scratch);
      return 0;
    }
    u8 *last_row = out + static_cast<isize>(pitch) * (height - 1);
    const u8 *end = (pitch < 0 ? out : last_row) +
        static_cast<isize>(static_cast<u64>(stride) > row_output ? static_cast<u64>(stride) : row_output);
    u8 *row = header[17] & 32 ? out : last_row;
    i64 step = header[17] & 32 ? pitch : -static_cast<i64>(pitch);
    for (i32 y = 0; y < height && result; ++y) {
      result = BdTgaRowInput(file, row, scratch, info, &available, end);
      if (result < 0) {
        result = 0;
        break;
      }
      if (header[17] & 16) {
        if (!BdHasCodecOutput(row, end, static_cast<u32>(row_output))) {
          result = 0;
          break;
        }
        BdMirrorTgaRow(row, width, info->bits);
      }
      if (y + 1 < height)
        row += static_cast<isize>(step);
    }
  }
  BdFreeCodec(scratch);
  return result;
}

// Convert one original index or BGRA source pixel into the writer's selected8/16/24/32-bit representation.
static u8 *BdPutTgaPixel(const u8 *source, u8 *out, i32 depth, const u8 *map) {
  if (depth == 8)
    *out++ = map[*source];
  else if (depth == 16) {
    u32 value = (source[0] >> 3) | (4u * ((source[1] & 0xF8u) | (32u * (source[2] & 0xF8u))));
    BdPutCodecValue(out, value, 2);
    out += 2;
  } else if (depth == 24 || depth == 32) {
    out[0] = source[0];
    out[1] = source[1];
    out[2] = source[2];
    if (depth == 32)
      out[3] = source[3];
    out += depth / 8;
  }
  return out;
}

// Compare the original complete source index/DWORD, including alpha even when the encoded output drops it.
static bool BdSameTgaPixel(const u8 *first, const u8 *second, u32 unit) {
  for (u32 i = 0; i < unit; ++i)
    if (first[i] != second[i])
      return false;
  return true;
}

// Pack one source row with the original three-equal-pixel run threshold and128-pixel raw/run limit.
// Color input is always BGRA, while8-bit input uses its actual palette-to-gray/index mapping.
i32 BdPackTgaRow(const u8 *source, u8 *out, i32 width, i32 depth, const u8 *map, i32 compressed) {
  if (depth == 15)
    depth = 16;
  u8 *begin = out;
  u32 unit = depth <= 8 ? 1 : 4;
  u32 advance = depth == 8 ? 1 : depth == 16 || depth == 24 || depth == 32 ? 4 : 0;
  if (!compressed) {
    for (i32 x = 0; x < width; ++x) {
      out = BdPutTgaPixel(source, out, depth, map);
      source += advance;
    }
    return static_cast<i32>(static_cast<u32>(depth) * static_cast<u32>(width)) / 8;
  }
  while (width > 0) {
    bool run = width > 2 && BdSameTgaPixel(source, source + unit, unit) &&
        BdSameTgaPixel(source + unit, source + 2 * unit, unit);
    i32 count;
    if (run) {
      count = 2;
      while (count < width && count < 128 && BdSameTgaPixel(source, source + count * unit, unit))
        ++count;
      *out++ = static_cast<u8>((count - 1) | 128);
      out = BdPutTgaPixel(source, out, depth, map);
      source += static_cast<usize>(count) * advance;
    } else {
      count = 1;
      while (count < width && count < 128) {
        const u8 *pixel = source + count * unit;
        if (width - count > 2 && BdSameTgaPixel(pixel, pixel + unit, unit) &&
            BdSameTgaPixel(pixel + unit, pixel + 2 * unit, unit))
          break;
        ++count;
      }
      *out++ = static_cast<u8>(count - 1);
      for (i32 x = 0; x < count; ++x) {
        out = BdPutTgaPixel(source, out, depth, map);
        source += unit;
      }
    }
    width -= count;
  }
  return static_cast<i32>(out - begin);
}

static i32 BdTgaGrayMap(i32 count, const u32 *palette, u8 *map) {
  i32 gray = 1;
  for (i32 i = 0; i < count; ++i) {
    const u8 *color = reinterpret_cast<const u8 *>(palette + i);
    u8 blue = color[0], green = color[1], red = color[2];
    if (red != green || red != blue)
      gray = 0;
    map[i] = green;
  }
  if (!gray)
    for (i32 i = 0; i < count; ++i)
      map[i] = static_cast<u8>(i);
  return gray;
}

// Emit the original bottom-up Targa header, optional color map and raw/RLE rows with actual final fwrite counts.
i32 BD_STDCALL BdWriteTgaPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  i32 count = static_cast<i32>(info->palette_count), first = static_cast<i32>(info->palette_first);
  i32 width = static_cast<i32>(info->width), height = static_cast<i32>(info->height);
  i32 source_depth = static_cast<i32>(info->source_bits), depth = 8;
  if (source_depth > 8) {
    depth = 16;
    if (source_depth >= 16 && (source_depth != 16 || static_cast<i32>(info->green_bits) > 5))
      depth = source_depth > 24 ? 32 : 24;
  }
  if (count < 0 || count > 256)
    return 0;
  u8 palette[1024], map[256], header[18];
  bool has_palette = count != 0, gray = false;
  i32 palette_depth = 32, type = depth > 8 ? 2 : 1;
  if (depth <= 8) {
    gray = BdTgaGrayMap(count, info->palette, map) != 0;
    if (gray) {
      has_palette = false;
      type = 3;
    }
  }
  if (info->compressed)
    type |= 8;
  if (has_palette) {
    if (count > 256 || first < 0 || first > 256 - count)
      return 0;
    if (info->red_bits == 5 && info->green_bits == 5 && info->blue_bits == 5)
      palette_depth = 16;
    else if (info->alpha_bits != 8)
      palette_depth = 24;
    for (i32 i = 0; i < count; ++i) {
      const u8 *pixel = reinterpret_cast<const u8 *>(info->palette + first + i);
      BdPutTgaPixel(pixel, palette + i * (palette_depth / 8), palette_depth, nullptr);
    }
  }
  header[0] = 0;
  header[1] = has_palette;
  header[2] = static_cast<u8>(type);
  BdPutCodecValue(header + 3, static_cast<u32>(first), 2);
  BdPutCodecValue(header + 5, static_cast<u32>(count), 2);
  header[7] = static_cast<u8>(palette_depth);
  BdPutCodecValue(header + 8, static_cast<u32>(info->x_origin), 2);
  BdPutCodecValue(header + 10, 0u - static_cast<u32>(info->y_origin), 2);
  BdPutCodecValue(header + 12, info->width, 2);
  BdPutCodecValue(header + 14, info->height, 2);
  header[16] = static_cast<u8>(depth);
  header[17] = 0;
  i32 result = static_cast<i32>(BdWriteCodecBytes(state->file, header, 18));
  if (result && !gray) {
    u32 bytes = static_cast<u32>(count * palette_depth / 8);
    if (bytes) {
      if (!has_palette)
        return 0;
      result = static_cast<i32>(BdWriteCodecBytes(state->file, palette, bytes));
    }
  }
  u64 row_bytes = static_cast<u64>(info->width) * static_cast<u32>(depth) / 8;
  if (row_bytes > INT32_MAX / 3)
    return 0;
  u8 *row = static_cast<u8 *>(BdAllocateCodec(static_cast<i32>(3 * row_bytes)));
  if (height > 0) {
    const u8 *input = source + static_cast<isize>(pitch) * (height - 1);
    for (i32 y = 0; y < height && result; ++y) {
      if (depth == 8)
        for (i32 x = 0; x < width; ++x)
          if (input[x] >= count) {
            BdFreeCodec(row);
            return 0;
          }
      i32 bytes = BdPackTgaRow(input, row, width, depth, map, static_cast<i32>(info->compressed));
      result = static_cast<i32>(BdWriteCodecBytes(state->file, row, static_cast<u32>(bytes)));
      if (y + 1 < height)
        input -= pitch;
    }
  }
  BdFreeCodec(row);
  return result;
}

u32 BD_STDCALL BdSelectCodecExtension(const char *extension, i32 *slot) {
  const u8 *text = reinterpret_cast<const u8 *>(extension);
  if (*text == '-' || *text == '+' || *text == '.')
    ++text;
  char token[80];
  token[0] = '.';
  u32 length = 1, selected = 0;
  if (*text > 32) {
    while (*text > 32 && *text != '?') {
      if (length == 79) {
        BdSelectCodec(0);
        return 0;
      }
      token[length++] = static_cast<char>(*text++);
    }
    token[length] = 0;
    for (i32 index = 0; index < 8 && !selected; ++index) {
      for (u32 codec = 1; codec < 3 && !selected; ++codec) {
        BdSelectCodec(codec);
        BdCodecDescriptor *description = BdDescribeCodec();
        if (!ScCompareNoCase(token, description->extensions[index])) {
          selected = codec;
          if (slot)
            *slot = index;
        }
        BdFreeCodec(description);
      }
    }
  }
  BdSelectCodec(selected);
  return selected;
}

// Create the original update-binary output stream, falling back to write-only and preserving the con: stdout identity.
BdCodecFile *BD_STDCALL BdCreateCodecFile(const char *path) {
  FILE *stream;
  if (!strncmp(path, "con:", 4))
    stream = stdout;
  else {
    ScString native = ScNativeFilePath(path);
    stream = fopen(native.Data(), "w+b");
    if (!stream)
      stream = fopen(native.Data(), "wb");
  }
  if (!stream)
    return nullptr;
  BdCodecFile *file = static_cast<BdCodecFile *>(BdAllocateCodec(sizeof(BdCodecFile)));
  file->stream = stream;
  file->position = 0;
  file->length = 0;
  file->read_ptr = file->buffer;
  file->read_end = file->buffer;
  file->write_mode = 1;
  file->field_018 = 0;
  return file;
}

// Advance the original logical position and high-water length before performing the real C stream write.
u32 BD_STDCALL BdWriteCodecBytes(BdCodecFile *file, const void *input, u32 length) {
  file->position = static_cast<i32>(static_cast<u32>(file->position) + length);
  if (file->position > file->length)
    file->length = file->position;
  return static_cast<u32>(fwrite(input, 1, length, file->stream));
}

// Create the shared zeroed24-byte writer state with its borrowed file, independent of global selection.
i32 BD_STDCALL BdCreateCodecWriterState(BdCodecState **out, BdCodecFile *file, const char *, i32) {
  BdCodecState *state = static_cast<BdCodecState *>(BdAllocateCodec(sizeof(BdCodecState)));
  memset(state, 0, sizeof(*state));
  state->file = file;
  *out = state;
  return 1;
}

i32 BD_STDCALL BdOpenCodecWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C) {
  return BdCodecCurrent ? BdCreateCodecWriterState(out, file, path, field_0C) : 0;
}

i32 BD_STDCALL BdCloseCodecWriter(BdCodecState *state) {
  return BdCodecCurrent ? BdFreeCodec(state) : 0;
}

void BdPutCodecValue(u8 *out, u32 value, u32 count) {
  for (u32 i = 0; i < count; ++i)
    out[i] = static_cast<u8>(value >> (8 * i));
}

// Dispatch all four genuine writer arguments through the current linked BMP/Targa selection.
i32 BD_STDCALL BdWriteCodecPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  if (BdCodecCurrent == 1)
    return BdWriteBmpPixels(state, info, source, pitch);
  if (BdCodecCurrent == 2)
    return BdWriteTgaPixels(state, info, source, pitch);
  return 0;
}

BdLegacyCodec::BdLegacyCodec() : references(0) {
}

BdLegacyCodec::~BdLegacyCodec() {
}

// Publish and retain only the unknown or codec identity. Rejected requests clear the output.
bool BdLegacyCodec::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, BdLegacyCodec);
  HANDLE_QUERY_INTERFACE(IID_GxGimexCodec, BdLegacyCodec);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment this wrapper before retaining the actual module director. The original body is shared with the loader.
u32 BdLegacyCodec::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Release the director before decrementing a nonzero object count. Entry count zero also deletes the wrapper.
u32 BdLegacyCodec::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

i32 BdLegacyCodec::GetValue0C() {
  return 2;
}

i32 BdLegacyCodec::GetValue10() {
  return 21;
}

// Allocate the selected codec's220-byte descriptor without acquiring a file or changing selection.
BdCodecDescriptor *BdLegacyCodec::Describe() {
  return BdDescribeCodec();
}

i32 BdLegacyCodec::CanRead(BdCodecFile *file) {
  return BdCanReadCodec(file);
}

i32 BdLegacyCodec::OpenReader(BdCodecState **out, BdCodecFile *file, const char *path) {
  return BdReadCodecInfo(out, file, path);
}

// Return selected-format metadata, retaining the original ignored single-image index argument.
BdCodecInfo *BdLegacyCodec::InfoAt(BdCodecState *state, i32 index) {
  return BdCodecInfoAt(state, index);
}

i32 BdLegacyCodec::ReadPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  return BdReadCodecPixels(state, info, out, pitch);
}

i32 BdLegacyCodec::CloseReader(BdCodecState *state) {
  return BdCloseCodecState(state);
}

i32 BdLegacyCodec::OpenWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C) {
  return BdOpenCodecWriter(out, file, path, field_0C);
}

i32 BdLegacyCodec::WritePixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  return BdWriteCodecPixels(state, info, source, pitch);
}

i32 BdLegacyCodec::CloseWriter(BdCodecState *state) {
  return BdCloseCodecWriter(state);
}

i32 BdLegacyCodec::SelectForFile(const char *path, BdCodecFile *file) {
  return BdSelectCodecForFile(path, file);
}

// Open the actual image path or probe its original suffixes, publishing the resolved name.
BdCodecFile *BdLegacyCodec::OpenFile(const char *path, char *resolved) {
  return BdOpenCodec(path, resolved);
}

i32 BdLegacyCodec::SetSelected(u32 index) {
  return BdSelectCodec(index);
}

u32 BdLegacyCodec::GetSelected() {
  return BdSelectedCodec();
}

u32 BdLegacyCodec::SelectExtension(const char *extension, i32 *slot) {
  return BdSelectCodecExtension(extension, slot);
}

i32 BdLegacyCodec::GetCodecCount() {
  return BdCodecCount();
}

void BdLegacyCodec::DestroyPixels(BdCodecPixels *pixels) {
  BdDestroyCodecPixels(pixels);
}

// Decode the requested image into owned metadata/pixels while restoring the prior module selection.
BdCodecPixels *BdLegacyCodec::Decode(const char *path, i32 index) {
  return BdDecodeImage(path, index);
}

// Allocate BMP metadata directly, preserving selection even when another codec is active.
BdCodecDescriptor *BdLegacyCodec::DescribeBmp() {
  return BdDescribeBmpCodec();
}

i32 BdLegacyCodec::ProbeBmp(BdCodecFile *file) {
  return BdProbeBmp(file);
}

i32 BdLegacyCodec::OpenBmpReader(BdCodecState **out, BdCodecFile *file, const char *path) {
  return BdOpenBmpReader(out, file, path);
}

// Read direct BMP dimensions, palette and masks; preserve the original index argument.
BdCodecInfo *BdLegacyCodec::BmpInfoAt(BdCodecState *state, i32 index) {
  return BdReadBmpInfo(state, index);
}

i32 BdLegacyCodec::ReadBmpPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch) {
  return BdReadBmpPixels(state, info, out, pitch);
}

i32 BdLegacyCodec::CloseBmpReader(BdCodecState *state) {
  return BdCloseCodecStream(state);
}

i32 BdLegacyCodec::OpenBmpWriter(BdCodecState **out, BdCodecFile *file, const char *path, i32 field_0C) {
  return BdCreateCodecWriterState(out, file, path, field_0C);
}

i32 BdLegacyCodec::WriteBmpPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch) {
  return BdWriteBmpPixels(state, info, source, pitch);
}

i32 BdLegacyCodec::CloseBmpWriter(BdCodecState *state) {
  return BdCloseCodecStream(state);
}

// Create the actual zero-reference codec exposing the distinct C3023A9C identity.
ScUnknown *BdCreateClassC3023BE2() {
  return new BdLegacyCodec;
}

BdQfsCodec::BdQfsCodec() : references(0) {
}

// Finish destruction after the deleting interface has performed its original final-release decision.
BdQfsCodec::~BdQfsCodec() = default;

// Publish either the unknown or codec identity and retain the returned interface.
bool BdQfsCodec::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, BdQfsCodec);
  HANDLE_QUERY_INTERFACE(IID_RsCompression, BdQfsCodec);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 BdQfsCodec::AddRef() {
  return ++references;
}

// Release a reference and destroy the codec when its count is zero or one.
u32 BdQfsCodec::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Encode after the four-byte outer length and publish the same complete byte count in both outputs.
bool BdQfsCodec::Compress(const u8 *source, i32 length, u8 *destination, u32 *written) {
  i32 option = 1;
  u32 size = BdQfsEncode(source, length, destination + 4, &option) + 4;
  *written = size;
  memcpy(destination, &size, sizeof(size));
  return size != 0;
}

// Decode the QFS stream after its four-byte outer length and publish the declared decoded length.
bool BdQfsCodec::Decompress(const u8 *source, u32 length, u8 *destination, u32 *written) {
  u32 size = ScQfsDecompress(source + 4, &length, destination);
  *written = size;
  return size != 0;
}

u32 BdQfsCodec::CompressCapacity(u32 length) {
  return 10u * length / 9u + 1000u;
}

// Read the stored outer compressed length without imposing alignment on the supplied bytes.
u32 BdQfsCodec::CompressedSize(const u8 *source) {
  u32 size;
  memcpy(&size, source, sizeof(size));
  return size;
}

u32 BdQfsCodec::DecompressedSize(const u8 *source) {
  return ScQfsGetSize(source + 4);
}

void BdQfsCodec::VersionEffects(ScStringView *out) {
  ScString version("0.90");
  out->Copy(version);
}

// Validate an external QFS packet before the original unbounded decoder reads or writes it.
// Only complete packets that initialize the declared output are passed to the decoder.
bool BdValidateQfsInput(const u8 *data, u32 size, u32 *decoded_size) {
  if (size < 9)
    return false;
  const u8 *source = data + 4;
  u32 available = size - 4, size_offset = source[0] == 0x10 && source[1] == 0xfb ? 2 : 5;
  u32 packet = (source[0] & 1 ? 5u : 2u) + 3;
  if (available < size_offset + 3 || available < packet)
    return false;
  u32 declared = (static_cast<u32>(source[size_offset]) << 16) |
      (static_cast<u32>(source[size_offset + 1]) << 8) | source[size_offset + 2];
  u32 actual = (static_cast<u32>(source[packet - 3]) << 16) |
      (static_cast<u32>(source[packet - 2]) << 8) | source[packet - 1];
  if (declared != actual)
    return false;
  u32 written = 0;
  while (packet < available) {
    u8 control = source[packet++];
    u32 literals, length = 0, distance = 0, extra;
    bool terminal = false;
    if (control < 0x80) {
      extra = 1;
      if (available - packet < extra)
        return false;
      literals = control & 3;
      length = ((control >> 2) & 7) + 3;
      distance = (static_cast<u32>(control & 0x60) << 3) + source[packet] + 1;
    } else if (control < 0xc0) {
      extra = 2;
      if (available - packet < extra)
        return false;
      literals = source[packet] >> 6;
      length = (control & 0x3f) + 4;
      distance = (static_cast<u32>(source[packet] & 0x3f) << 8) + source[packet + 1] + 1;
    } else if (control < 0xe0) {
      extra = 3;
      if (available - packet < extra)
        return false;
      literals = control & 3;
      length = (static_cast<u32>(control & 0x0c) << 6) + source[packet + 2] + 5;
      distance = (static_cast<u32>(control & 0x10) << 12) +
          (static_cast<u32>(source[packet]) << 8) + source[packet + 1] + 1;
    } else {
      extra = 0;
      literals = control < 0xfc ? (control & 0x1f) * 4 + 4 : control & 3;
      terminal = control >= 0xfc;
    }
    packet += extra;
    if (literals > available - packet || literals > declared - written)
      return false;
    packet += literals;
    written += literals;
    if (terminal) {
      *decoded_size = declared;
      return written == declared;
    }
    if (length > declared - written || (length && distance > written))
      return false;
    written += length;
  }
  return false;
}

static u32 BdQfsHash(const u8 *source) {
  return (static_cast<u32>(source[0]) << 8 | source[2]) ^ (static_cast<u32>(source[1]) << 4);
}

// Emit aligned literal runs in the original maximum 112-byte packet size.
static void BdQfsWriteLiterals(const u8 **source, u32 *count, u8 **destination) {
  while (*count > 3) {
    u32 size = *count & ~3u;
    if (size > 112)
      size = 112;
    *count -= size;
    *(*destination)++ = static_cast<u8>((size >> 2) - 33);
    memcpy(*destination, *source, size);
    *source += size;
    *destination += size;
  }
}

// Encode QFS literal and back-reference packets using the original hash chains and match-cost selection.
// The final one or two input bytes skip hash probes that would read outside the defined input.
u32 BD_STDCALL BdQfsEncodeBody(const u8 *source, i32 length, u8 *destination, i32, i32 fast) {
  i32 *heads = new i32[0x10000], *chains = new i32[0x20000];
  for (u32 i = 0; i < 0x10000; ++i)
    heads[i] = -1;
  const u8 *cursor = source, *literals = source;
  u8 *output = destination;
  u32 pending = 0;
  i32 remaining = length;
  while (remaining > 0) {
    u32 position = static_cast<u32>(cursor - source);
    if (remaining < 3) {
      pending += static_cast<u32>(remaining);
      cursor += remaining;
      break;
    }
    u32 hash = BdQfsHash(cursor), best_length = 2, best_header = 2, best_distance = 0;
    u32 limit = remaining < 1028 ? static_cast<u32>(remaining) : 1028;
    i32 candidate = heads[hash];
    i32 oldest = position > 0x1ffff ? static_cast<i32>(position - 0x1ffff) : 0;
    while (candidate >= oldest && best_length < limit) {
      const u8 *match = source + candidate;
      if (cursor[best_length] == match[best_length]) {
        u32 matched = 0;
        while (matched < limit && cursor[matched] == match[matched])
          ++matched;
        if (matched > best_length) {
          u32 distance = position - static_cast<u32>(candidate) - 1;
          u32 header = distance >= 0x400 || matched > 10 ? (distance >= 0x4000 || matched > 67 ? 4 : 3) : 2;
          if (matched - header + 4 > best_length - best_header + 4) {
            best_header = header;
            best_length = matched;
            best_distance = distance;
            if (matched >= 1028)
              break;
          }
        }
      }
      candidate = chains[static_cast<u32>(candidate) & 0x1ffff];
    }
    if (best_header >= best_length) {
      ++pending;
      --remaining;
      chains[position & 0x1ffff] = heads[hash];
      heads[hash] = static_cast<i32>(position);
      ++cursor;
      continue;
    }
    BdQfsWriteLiterals(&literals, &pending, &output);
    if (best_header == 2) {
      *output++ = static_cast<u8>(pending + 4 * (best_length + 61) + ((best_distance >> 3) & 0xe0));
      *output++ = static_cast<u8>(best_distance);
    } else if (best_header == 3) {
      *output++ = static_cast<u8>(best_length + 124);
      *output++ = static_cast<u8>((pending << 6) + (best_distance >> 8));
      *output++ = static_cast<u8>(best_distance);
    } else {
      *output++ = static_cast<u8>(pending + ((best_distance >> 12) & 0xf0) +
          (((best_length - 5) >> 6) & 0xfc) - 64);
      *output++ = static_cast<u8>(best_distance >> 8);
      *output++ = static_cast<u8>(best_distance);
      *output++ = static_cast<u8>(best_length - 5);
    }
    if (pending) {
      memcpy(output, literals, pending);
      output += pending;
      pending = 0;
    }
    if (fast) {
      chains[position & 0x1ffff] = heads[hash];
      heads[hash] = static_cast<i32>(position);
      cursor += best_length;
    } else {
      for (u32 i = 0; i < best_length; ++i) {
        u32 offset = static_cast<u32>(cursor - source);
        if (remaining - static_cast<i32>(i) >= 3) {
          u32 next_hash = BdQfsHash(cursor);
          chains[offset & 0x1ffff] = heads[next_hash];
          heads[next_hash] = static_cast<i32>(offset);
        }
        ++cursor;
      }
    }
    literals = cursor;
    remaining -= static_cast<i32>(best_length);
  }
  BdQfsWriteLiterals(&literals, &pending, &output);
  *output++ = static_cast<u8>(pending - 4);
  if (pending) {
    memcpy(output, literals, pending);
    output += pending;
  }
  delete[] chains;
  delete[] heads;
  return static_cast<u32>(output - destination);
}

// Write the original QFS signature and 24-bit uncompressed length before encoding the packet stream.
// The fourth argument is unused by this source implementation.
u32 BD_STDCALL BdQfsEncode(const u8 *source, i32 length, u8 *destination, const i32 *) {
  destination[0] = 0x10;
  destination[1] = 0xfb;
  destination[2] = static_cast<u8>(static_cast<u32>(length) >> 16);
  destination[3] = static_cast<u8>(static_cast<u32>(length) >> 8);
  destination[4] = static_cast<u8>(length);
  return BdQfsEncodeBody(source, length, destination + 5, 0x20000, 1) + 5;
}
