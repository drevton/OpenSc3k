#define WIN32_LEAN_AND_MEAN
#include "resources/resource.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "common/framework.h"
#include "common/sc3_error.h"
#include "common/sc3_qfs.h"
#include "common/sc3_string.h"
#include "common/thread.h"
#include "graphics/image_codecs.h"
#include "resources/accelerator_resource.h"
#include "resources/api.h"
#include "resources/bitmap_record.h"
#include "resources/factory.h"
#include "resources/property_set.h"
#include "resources/stream.h"

#define RS_SCAN_I32 "%" SCNi32
#define RS_SCAN_I64 "%" SCNi64

// Property streams distinguish TKT1 text from TKB1 binary records; bitmap writing uses its own encoding selector.
enum RsPropertyFormat : u32 {
  kRsTextPropertyFormatTag = 0x31544b54,
  kRsBinaryPropertyFormatTag = 0x31424b54,
  kRsBitmapWriteEncoding = 0x55d4fb53,
};

// Resolve the exact CRT scanner imported by the installed resource module.
class RsAcceleratorCrt {
public:
  // Acquire the native platform dependency when keyboard resource parsing is first reached.
  RsAcceleratorCrt()
  {
    Scan = sscanf;
  }

  // Release the module reference retained by this native import boundary.
  ~RsAcceleratorCrt() {
  }

  i32(SC3_CDECL *Scan)(const char *, const char *, ...);

private:
};

RsAcceleratorResources::RsAcceleratorResources() {
}

RsAcceleratorResources::~RsAcceleratorResources() {
}

bool RsAcceleratorResources::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnAcceleratorResources, WnAcceleratorResources);
  return RsResource::QueryInterface(iid, out);
}

u32 RsAcceleratorResources::AddRef() {
  return RsResource::AddRef();
}

u32 RsAcceleratorResources::Release() {
  return RsResource::Release();
}

// Register sorted bindings on the supplied accelerator, ignoring individual registration results.
bool RsAcceleratorResources::RegisterResources(WnAccelerator *accelerator) {
  for (const auto &entry : bindings)
    accelerator->RegisterAccelerator(entry.first & 0xffffu, entry.first >> 16, entry.second);
  return true;
}

// Clear the live String range before reading a carriage-return-terminated line. Drop line feeds and append the
// original logical NUL byte; the concrete erase operation takes a real count rather than std-string npos.
static u8 SC3_STDCALL RsReadAcceleratorLine(RsStream *stream, ScString *line) {
  line->Erase(0, line->ScString::Strlen());
  for (;;) {
    u8 ch;
    if (!stream->ReadBlock(&ch, 1))
      return 0;
    if (ch == 10)
      continue;
    if (ch == 13) {
      ch = 0;
      line->Append(&ch, 1);
      return 1;
    }
    line->Append(&ch, 1);
  }
}

// Parse complete binding lines into the existing map; duplicate keys retain their first command.
u8 RsAcceleratorResources::Read(RsStream *stream) {
  static RsAcceleratorCrt crt;
  ScString line;
  u32 control, shift, alt, virtualKey, command = 0, first = 0, second = 0, third = 0;
  while (RsReadAcceleratorLine(stream, &line)) {
    i32 count = crt.Scan(line.ToChar(), "%u %u %u %u = %u %u %u %u", &control, &shift, &alt, &virtualKey, &command,
        &first, &second, &third);
    if (count == -1)
      break;
    if (count == 8 && control <= 1 && shift <= 1 && alt <= 1 && virtualKey <= 0xffu) {
      u32 packed = (virtualKey << 16) | (alt ? 1u : 0u) | (control ? 2u : 0u) | (shift ? 4u : 0u);
      bindings.emplace(packed, ScMessage(command, first, second, third));
    }
  }
  return 1;
}

u8 RsAcceleratorResources::Write(RsStream *) {
  return 0;
}

RsAcceleratorFactory::RsAcceleratorFactory() {
}

// Allocate only keyboard-binding resources; unsupported types explicitly clear the interface output.
bool RsAcceleratorFactory::New(u32 type, u32 iid, void **out) {
  if (type != kRsAcceleratorResourceType) {
    *out = nullptr;
    return false;
  }
  return (new RsAcceleratorResources())->QueryInterface(iid, out);
}

bool RsAcceleratorFactory::SerialRead(RsResource *resource, RsStream *stream) {
  return static_cast<RsAcceleratorResources *>(resource)->Read(stream);
}

bool RsAcceleratorFactory::SerialWrite(RsResource *resource, RsStream *stream) {
  return static_cast<RsAcceleratorResources *>(resource)->Write(stream);
}

// Return the zero-reference concrete factory registered for class42E411C2 by the resource director.
ScUnknown *RsCreateClass42E411C2() {
  return static_cast<RsFactoryInterface *>(new RsAcceleratorFactory());
}

RsBitmapRecord::RsBitmapRecord() : bytes(nullptr), size(0), position(0) {
}

RsBitmapRecord::~RsBitmapRecord() {
  delete[] bytes;
  bytes = nullptr;
}

// Load a bitmap resource and prepend the original fourteen-byte BMP file header to its copied DIB bytes.
// Native module bitmap requests remain unsupported by the SDL resource backend and preserve load refusal.
bool RsBitmapRecord::Init([[maybe_unused]] HINSTANCE__ *module, [[maybe_unused]] u16 identifier) {
  return false;
}

// Publish the copied resource's complete BMP size only after initialization created its owned allocation.
bool RsBitmapRecord::GetSize(u32 *out) {
  if (!bytes)
    return 0;
  *out = size;
  return 1;
}

// Preserve the original wrapped end gate and advance only after copying, excluding out-of-allocation source accesses.
bool RsBitmapRecord::ReadBlock(void *out, u32 length) {
  if (!bytes || position + length > size)
    return 0;
  if (position > size || length > size - position)
    return false;
  if (length)
    memcpy(out, bytes + position, length);
  position += length;
  return 1;
}

bool RsBitmapRecord::WriteBlock(const void *, u32) {
  return 0;
}

// Add the DWORD count to the cursor without imposing a new logical range check.
bool RsBitmapRecord::SkipBytes(u32 distance) {
  if (!bytes)
    return false;
  position += distance;
  return true;
}

RsImageResource::RsImageResource() : buffer(nullptr) {
  RsGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

// Release the independent graphics owner; queried buffer interfaces can survive destruction of this resource.
RsImageResource::~RsImageResource() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

// Query the graphics owner before this resource, including for Unknown. Only the image IID checks a null output.
bool RsImageResource::QueryInterface(u32 iid, void **out) {
  if (buffer && buffer->QueryInterface(iid, out))
    return true;
  if (iid != IID_RsImageData)
    return RsResource::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<RsImageData *>(this);
  AddRef();
  return true;
}

u32 RsImageResource::AddRef() {
  return RsResource::AddRef();
}

u32 RsImageResource::Release() {
  return RsResource::Release();
}

// Retain the replacement before releasing the old graphics owner, preserving self-assignment and callback order.
u8 RsImageResource::SetBuffer(GxBuffer *replacement) {
  if (replacement)
    replacement->AddRef();
  if (buffer)
    buffer->Release();
  buffer = replacement;
  return 1;
}

bool RsImageResource::QueryBuffer(GxBuffer **out) {
  return buffer ? buffer->QueryInterface(IID_GxBuffer, reinterpret_cast<void **>(out)) : false;
}

bool RsImageResource::QueryBuffer(u32 iid, void **out) {
  return buffer ? buffer->QueryInterface(iid, out) : false;
}

// Read the original image header and dispatch BMP, raw, QFS or scalar-pixel records through their distinct readers.
u8 RsImageResource::Read(RsStream *stream, const GxColorType *format, u32 mode) {
  if (!stream || !buffer)
    return 0;
  // Exclude indeterminate first-use bytes if a custom stream reports success without writing them.
  u32 first[2] = {};
  if (!stream->ReadBlock(first, sizeof(first)))
    return 0;
  if ((u16)first[0] == 0x4d42) {
    stream->ReadBlock(first, 6);
    return ReadBmp(stream, format, mode);
  }
  if (first[0] == 1 || first[0] == 2) {
    RsImageHeader header = {};
    header.tag = first[0];
    header.size = first[1];
    u8 result = stream->ReadU32(&header.width);
    if (!result)
      return result;
    result = stream->ReadU32(&header.height);
    if (!result)
      return result;
    result = stream->ReadI32(&header.format);
    if (!result)
      return result;
    return header.tag == 1 ? ReadRaw(stream, &header, format, mode) : ReadQfs(stream, &header, format, mode);
  }
  if (first[0] == kRsImageResourceType) {
    RsRasterHeader header = {first[0], first[1]};
    u8 result = stream->ReadU32(&header.height);
    if (!result)
      return result;
    return ReadPixels(stream, &header, format, mode);
  }
  return 0;
}

// Actual on-disk DIB color records follow the forty-byte information header in the original stack aggregate.
struct RsBitmapColor {
  u8 blue, green, red, reserved;
};

struct RsBitmapInfo {
  u32 size;
  i32 width, height;
  u16 planes, bits;
  u32 compression, bytes;
  i32 horizontal, vertical;
  u32 colorsUsed, important;
  RsBitmapColor colors[256];
};

static_assert(offsetof(RsBitmapInfo, colors) == 40);

// Accept only uncompressed four-, eight- or twenty-four-bit DIBs, testing compression before bit depth.
static bool SC3_STDCALL RsBmpHeaderValid(const RsBitmapInfo *info) {
  if (info->compression)
    return false;
  return info->bits == 4 || info->bits == 8 || info->bits == 24;
}

static u32 RsBmpStorageSize(const RsBitmapInfo *info) {
  u32 bytes = (((u32)info->width * info->bits + 7u) & ~7u) >> 3;
  return (u32)info->height * ((bytes + 3u) & ~3u);
}

static void RsPackBmpPalette(const RsBitmapColor *colors, GxPackFunction pack, u16 *out, u32 count) {
  for (u32 i = 0; i < count; ++i)
    pack(out + i, colors[i].red, colors[i].green, colors[i].blue);
}

// Read the DIB and palette after the file header, using the original stack/heap split before native buffer upload.
// Bounded extra storage permits the original single palette read even when a stream writes fewer bytes than requested.
u8 RsImageResource::ReadBmp(RsStream *stream, const GxColorType *format, u32 mode) {
  // Exclude indeterminate bytes if an external stream reports success without filling its output.
  struct BitmapScratch {
    RsBitmapInfo info;
    u8 extra[65536 - sizeof(RsBitmapInfo::colors)];
  } scratch = {};
  RsBitmapInfo *info = &scratch.info;
  if (!stream->ReadBlock(info, 40) || !RsBmpHeaderValid(info))
    return 0;
  buffer->Shutdown();
  if (!buffer->Init((u32)info->width, (u32)info->height, *format, nullptr, GxDepthType(GxDepthType::Depth::None),
          static_cast<GxVramRequest>(mode), false) ||
      !buffer->Lock(GxLockFlags::GetSetPixel))
    return 0;
  u8 stack[65536] = {}, *heap = nullptr, *pixels = stack, result = 0;
  u32 size = RsBmpStorageSize(info);
  if (size > sizeof(stack))
    pixels = heap = new u8[size]();
  bool ready = info->bits == 24;
  if (info->bits == 4 || info->bits == 8) {
    u32 count = info->colorsUsed ? info->colorsUsed : (info->bits == 4 ? 16u : 256u);
    u32 paletteBytes = count * 4;
    u8 *palette = reinterpret_cast<u8 *>(&scratch) + offsetof(RsBitmapInfo, colors);
    ready = stream->ReadBlock(palette, paletteBytes);
  }
  if (ready && stream->ReadBlock(pixels, size))
    result = UploadBmp(info, pixels);
  delete[] heap;
  buffer->Unlock(GxLockFlags::GetSetPixel);
  return result;
}

// Read tag1 with its stored format, retaining the original pitched-input, packed-output row order before conversion.
// The original leaves the requested format pair unused.
u8 RsImageResource::ReadRaw(RsStream *stream, const RsImageHeader *header, const GxColorType *, u32 mode) {
  GxColorType format(static_cast<GxColorType::Color>(header->format));
  i32 code = static_cast<i32>(format.color), bits = static_cast<i32>(format.depth);
  buffer->Shutdown();
  if (!buffer->Init(header->width, header->height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
          static_cast<GxVramRequest>(mode), false) ||
      !buffer->Lock(GxLockFlags::Bits))
    return 0;
  u32 bytes = (u32)bits >> 3, pitch = buffer->GetColorSurfaceStride(), row = bytes * header->width;
  bool packed = pitch == row;
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  u32 temporaryCapacity = header->size;
  u8 *temporary = packed ? nullptr : new u8[temporaryCapacity](), result = 0;
  if (stream->ReadBlock(packed ? destination : temporary, header->size)) {
    if (!packed && header->height) {
      u32 copyRow = bytes * header->width;
      const u8 *source = temporary;
      u8 *target = destination;
      for (u32 y = 0; y < header->height; ++y) {
        memcpy(target, source, copyRow);
        if (y + 1 < header->height) {
          source += pitch;
          target += copyRow;
        }
      }
    }
    result = 1;
  }
  delete[] temporary;
  buffer->Unlock(GxLockFlags::Bits);
  if (result) {
    result = buffer->Convert16BitBufferFromFormatToFormat(
        static_cast<GxColorType::Color>((u32)code), static_cast<GxColorType::Color>(1));
    buffer->SetSurfaceRestoreType(GxRestoreType::AutoRam);
  }
  return result;
}

// Select the original upload path from the DIB bit depth and the live graphics buffer's target depth.
u8 RsImageResource::UploadBmp(const RsBitmapInfo *info, const u8 *pixels) {
  if (info->bits == 4)
    return buffer->GetBitsPerPixel() == 16 ? Upload4To16(info, pixels) : 0;
  if (info->bits == 8) {
    i32 bits = buffer->GetBitsPerPixel();
    if (bits == 8)
      return Upload8To8(info, pixels);
    if (bits == 16)
      return Upload8To16(info, pixels);
  } else if (info->bits == 24) {
    i32 bits = buffer->GetBitsPerPixel();
    if (bits == 16)
      return Upload24To16(info, pixels);
    if (bits == 24)
      return Upload24To24(info, pixels);
    if (bits == 32)
      return Upload24To32(info, pixels);
  }
  return 0;
}

// Pack the full sixteen-color palette, then expand both nibbles of each source byte into bottom-up WORD rows.
u8 RsImageResource::Upload4To16(const RsBitmapInfo *info, const u8 *pixels) {
  u16 palette[16];
  GxPackFunction pack = buffer->GetRGBTo16ConverterPtr();
  RsPackBmpPalette(info->colors, pack, palette, 16);
  i32 width = info->width;
  u32 row = (((u32)width * 4u + 7u) & ~7u) >> 3, padding = ((row + 3u) & ~3u) - row;
  u32 stride = buffer->GetColorSurfaceStride() >> 1;
  u32 height = (u32)info->height;
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  for (u32 remaining = height; remaining; --remaining) {
    u16 *target = reinterpret_cast<u16 *>(destination + 2u * stride * (remaining - 1));
    for (i32 x = 0; x < width; x += 2) {
      *target++ = palette[*pixels >> 4];
      *target++ = palette[*pixels++ & 15];
    }
    pixels += padding;
  }
  return 1;
}

// Convert the entire palette once and expand indexed rows in reverse vertical order through the actual buffer stride.
u8 RsImageResource::Upload8To16(const RsBitmapInfo *info, const u8 *pixels) {
  u16 palette[256];
  GxPackFunction pack = buffer->GetRGBTo16ConverterPtr();
  RsPackBmpPalette(info->colors, pack, palette, 256);
  i32 width = info->width;
  u32 padding = (((u32)width + 3u) & ~3u) - (u32)width, stride = buffer->GetColorSurfaceStride() >> 1;
  u32 height = (u32)info->height;
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  for (u32 remaining = height; remaining; --remaining) {
    u16 *target = reinterpret_cast<u16 *>(destination + 2u * stride * (remaining - 1));
    for (i32 x = 0; x < width; ++x)
      *target++ = palette[*pixels++];
    pixels += padding;
  }
  return 1;
}

// Capture the native converter and write BGR source triples into bottom-up sixteen-bit rows.
u8 RsImageResource::Upload24To16(const RsBitmapInfo *info, const u8 *pixels) {
  u32 row = 3u * (u32)info->width, padding = ((row + 3u) & ~3u) - row, stride = buffer->GetColorSurfaceStride() >> 1;
  GxPackFunction pack = buffer->GetRGBTo16ConverterPtr();
  u32 height = (u32)info->height;
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  for (u32 remaining = height; remaining; --remaining) {
    u16 *target = reinterpret_cast<u16 *>(destination + 2u * stride * (remaining - 1));
    for (i32 x = 0; x < info->width; ++x) {
      u8 blue = *pixels++, green = *pixels++, red = *pixels++;
      pack(target++, red, green, blue);
    }
    pixels += padding;
  }
  return 1;
}

// Copy indexed pixels into reversed destination rows without changing or installing a palette.
u8 RsImageResource::Upload8To8(const RsBitmapInfo *info, const u8 *pixels) {
  i32 width = info->width;
  u32 pitch = buffer->GetColorSurfaceStride(), padding = (((u32)width + 3u) & ~3u) - (u32)width;
  u32 height = (u32)info->height;
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  for (u32 remaining = height; remaining; --remaining) {
    u8 *target = destination + pitch * (remaining - 1);
    for (i32 x = 0; x < width; ++x)
      target[x] = *pixels++;
    pixels += padding;
  }
  return 1;
}

// Preserve the original twenty-four-bit path's literal white output, which does not read source pixel values.
u8 RsImageResource::Upload24To24(const RsBitmapInfo *info, const u8 *) {
  i32 width = info->width, height = info->height;
  u32 pitch = buffer->GetColorSurfaceStride();
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  for (i32 remaining = height; remaining > 0; --remaining) {
    u8 *target = destination + pitch * (u32)(remaining - 1);
    for (i32 x = 0; x < width; ++x) {
      *target++ = 255;
      *target++ = 255;
      *target++ = 255;
    }
  }
  return 1;
}

// Preserve the original DWORD reads and unusual byte-offset row walk within the supplied backing storage.
u8 RsImageResource::Upload24To32(const RsBitmapInfo *info, const u8 *pixels) {
  u32 height = (u32)info->height, width = (u32)info->width, pitch = buffer->GetColorSurfaceStride(), step = pitch >> 2;
  u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  if (!height || !width)
    return 1;
  u32 offset = height * step;
  for (u32 remaining = height; remaining; --remaining) {
    for (u32 x = 0; x < width; ++x) {
      u32 value;
      memcpy(&value, pixels + offset + x * 4, 4);
      value |= kGxArgbAlphaMask;
      memcpy(destination + offset + x * 4, &value, 4);
    }
    offset -= step;
  }
  return 1;
}

// Read scalar pixels through the DWORD-array virtual and forward each complete DWORD to the live pixel writer.
u8 RsImageResource::ReadPixels(RsStream *stream, const RsRasterHeader *header, const GxColorType *format, u32 mode) {
  buffer->Shutdown();
  if (!buffer->Init(header->width, header->height, *format, nullptr, GxDepthType(GxDepthType::Depth::None),
          static_cast<GxVramRequest>(mode), false) ||
      !buffer->Lock(GxLockFlags::GetSetPixel))
    return 0;
  u32 count = header->width * header->height, *pixels = new u32[count]();
  u8 result = stream->ReadU32Array(pixels, count);
  if (result) {
    for (u32 y = 0; result && y < header->height; ++y) {
      for (u32 x = 0; x < header->width; ++x) {
        u32 index = x + y * header->width;
        if (index >= count) {
          result = 0;
          break;
        }
        buffer->SetPixel(x, y, pixels[index]);
      }
    }
    if (result)
      buffer->SetSurfaceRestoreType(GxRestoreType::AutoRam);
  }
  delete[] pixels;
  buffer->Unlock(GxLockFlags::GetSetPixel);
  return result;
}

// Decode after the outer length word, publishing the declared output size.
bool SC3_STDCALL RsDecodeImageQfs(const u8 *source, u32 size, u8 *destination, u32 *written) {
  u32 result = ScQfsDecompress(source + 4, &size, destination);
  *written = result;
  return result != 0;
}

// Decode tag2 using its stored format into the buffer or a packed temporary, then copy rows to its pitch.
// The original leaves the requested format pair unused before conversion and selecting automatic RAM restoration.
u8 RsImageResource::ReadQfs(RsStream *stream, const RsImageHeader *header, const GxColorType *, u32 mode) {
  GxColorType format(static_cast<GxColorType::Color>(header->format));
  i32 code = static_cast<i32>(format.color), bits = static_cast<i32>(format.depth);
  buffer->Shutdown();
  if (!buffer->Init(header->width, header->height, format, nullptr, GxDepthType(GxDepthType::Depth::None),
          static_cast<GxVramRequest>(mode), false) ||
      !buffer->Lock(GxLockFlags::Bits))
    return 0;
  u32 bytes = (u32)bits >> 3, pitch = buffer->GetColorSurfaceStride(), row = bytes * header->width;
  bool packed = pitch == row;
  u32 total = row * header->height;
  // A custom stream may leave fresh bytes untouched; later writes retain their actual values.
  u32 compressedCapacity = header->size;
  u8 result = 0, *compressed = new u8[compressedCapacity]();
  if (stream->ReadBlock(compressed, header->size)) {
    u8 *destination = static_cast<u8 *>(buffer->GetColorSurfaceBits()), *temporary = packed ? nullptr : new u8[total]();
    u32 written = 0;
    if (RsDecodeImageQfs(compressed, header->size, packed ? destination : temporary, &written) && written == total) {
      if (!packed) {
        u32 copyRow = bytes * header->width, height = header->height;
        u8 *source = temporary, *target = destination;
        for (u32 remaining = height; remaining; --remaining) {
          memcpy(target, source, copyRow);
          source += copyRow;
          target += pitch;
        }
      }
      result = 1;
    }
    delete[] temporary;
  }
  delete[] compressed;
  buffer->Unlock(GxLockFlags::Bits);
  if (result) {
    result = buffer->Convert16BitBufferFromFormatToFormat(
        static_cast<GxColorType::Color>((u32)code), static_cast<GxColorType::Color>(1));
    buffer->SetSurfaceRestoreType(GxRestoreType::AutoRam);
  }
  return result;
}

// Lock the current buffer before selecting QFS or BMP output, unlocking it even when the encoding is unsupported.
u8 RsImageResource::Write(RsStream *stream, u32 encoding) {
  if (!stream || !buffer || !buffer->Lock(GxLockFlags::Bits))
    return 0;
  u8 result = 0;
  if (encoding == 2)
    result = WriteQfs(stream);
  else if (encoding == kRsBitmapWriteEncoding)
    result = WriteBmp(stream);
  buffer->Unlock(GxLockFlags::Bits);
  return result;
}

// Pack pitched buffer rows before QFS encoding, then write the tag, compressed extent, dimensions and format in order.
u8 RsImageResource::WriteQfs(RsStream *stream) {
  i32 width = buffer->Width(), height = buffer->Height();
  u32 bytes = buffer->GetBytesPerPixel(), pitch = buffer->GetColorSurfaceStride();
  const u8 *source = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  i32 code = static_cast<i32>(buffer->GetColorType().color);
  u32 total = (u32)width * (u32)height * bytes, row = (u32)width * bytes;
  u8 *temporary = nullptr;
  const u8 *input = source;
  if (pitch != row) {
    input = temporary = new u8[total];
    if (height) {
      u8 *target = temporary;
      for (u32 remaining = (u32)height; remaining; --remaining) {
        memcpy(target, source, row);
        if (remaining > 1) {
          target += row;
          source += pitch;
        }
      }
    }
  }
  u8 *compressed = new u8[(10u * total) / 9u + 1000u];
  i32 fast = 1;
  u32 size = RsQfsEncode(input, (i32)total, compressed + 4, &fast) + 4;
  memcpy(compressed, &size, 4);
  u8 result = size != 0;
  result = result && stream->WriteU32(2) && stream->WriteU32(size) && stream->WriteU32((u32)width) &&
      stream->WriteU32((u32)height) && stream->WriteI32(code) && stream->WriteBlock(compressed, size);
  delete[] temporary;
  delete[] compressed;
  return result;
}

#pragma pack(push, 2)

struct RsBitmapFile {
  u16 signature;
  u32 size;
  u16 reservedFirst, reservedSecond;
  u32 offset;
};

#pragma pack(pop)

static_assert(sizeof(RsBitmapFile) == 14);

// Export a sixteen-bit buffer as bottom-up BGR24. The original attempts all three writes and ignores their results.
u8 RsImageResource::WriteBmp(RsStream *stream) {
  if (buffer->GetBitsPerPixel() != 16)
    return 0;
  i32 width = buffer->Width(), height = buffer->Height();
  u32 stride = buffer->GetColorSurfaceStride() >> 1, row = (3u * (u32)width + 3u) & ~3u, size = (u32)height * row;
  RsBitmapFile file = {0x4d42, size + 54u, 0, 0, 54};
  RsBitmapInfo info = {};
  info.size = 40;
  info.width = width;
  info.height = height;
  info.planes = 1;
  info.bits = 24;
  info.bytes = size;
  u8 stack[65536] = {}, *heap = size > sizeof(stack) ? new u8[size]() : nullptr, *pixels = heap ? heap : stack;
  const u8 *source = static_cast<u8 *>(buffer->GetColorSurfaceBits());
  GxUnpackFunction unpack = buffer->Get16ToRGBConverterPtr();
  for (u32 remaining = (u32)height; remaining; --remaining) {
    u8 *target = pixels + row * (remaining - 1);
    for (u32 x = 0; x < (u32)width; ++x) {
      u16 value;
      memcpy(&value, source + x * 2, 2);
      unpack(value, target + 2, target + 1, target);
      target += 3;
    }
    if (remaining > 1)
      source += 2u * stride;
  }
  stream->WriteBlock(&file, sizeof(file));
  stream->WriteBlock(&info, 40);
  stream->WriteBlock(pixels, size);
  delete[] heap;
  return 1;
}

RsKeyList::RsKeyList() {
}

RsKeyList::~RsKeyList() {
}

bool RsKeyList::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsKeyListInterface, RsKeyListInterface);
  return RsModuleObject::QueryInterface(iid, out);
}

u32 RsKeyList::AddRef() {
  return RsModuleObject::AddRef();
}

u32 RsKeyList::Release() {
  return RsModuleObject::Release();
}

// Order keys by unsigned group, then instance, then type, exactly as the installed comparator does.
bool RsKeyList::Less(const RsKey &left, const RsKey &right) {
  if (left.group != right.group)
    return left.group < right.group;
  if (left.instance != right.instance)
    return left.instance < right.instance;
  return left.type < right.type;
}

bool RsKeyList::RsKeyOrder::operator()(const RsKey &left, const RsKey &right) const {
  return Less(left, right);
}

void RsKeyList::InsertKey(const RsKey &key) {
  keys.insert(key);
}

bool RsKeyList::Insert(const RsKey &key) {
  InsertKey(key);
  return true;
}

void RsKeyList::InsertCallback(const RsKey &key, void *context) {
  static_cast<RsKeyList *>(context)->InsertKey(key);
}

bool RsKeyList::Insert(const RsKeyListInterface &source) {
  source.EnumKeys(InsertCallback, this);
  return true;
}

bool RsKeyList::Erase(const RsKey &key) {
  keys.erase(key);
  return true;
}

bool RsKeyList::EraseAll() {
  keys.clear();
  return true;
}

void RsKeyList::EnumKeys(void (*callback)(const RsKey &, void *), void *context) const {
  for (const RsKey &key : keys)
    callback(key, context);
}

bool RsKeyList::IsPresent(const RsKey &key) {
  return keys.find(key) != keys.end();
}

u32 RsKeyList::Size() const {
  return (u32)keys.size();
}

// Walk from the first key, returning the sentinel's unused key address at the end index as the source does.
RsKey *RsKeyList::GetKey(u32 index) {
  assert(index <= keys.size());
  if (index == keys.size())
    return &endKey;
  auto entry = keys.begin();
  while (index--)
    ++entry;
  return const_cast<RsKey *>(&*entry);
}

// Return the zero-reference primary interface created by the director's genuine key-list constructor.
ScUnknown *RsCreateKeyList() {
  return static_cast<RsKeyListInterface *>(new RsKeyList());
}

// The installed resource module imports these exact Windows CRT operations for textual resource transport.
class RsPropertyCrt {
public:
  // Preserve the original platform dependency rather than substituting the build toolchain's different CRT behavior.
  RsPropertyCrt()
  {
    Scan = sscanf;
  }

  // Release only the module reference acquired for this native platform import boundary.
  ~RsPropertyCrt() {
  }

  i32(SC3_CDECL *Scan)(const char *, const char *, ...);

private:
};

// Acquire the real platform CRT only when a text conversion is reached; no game object or service is synthesized.
static RsPropertyCrt *RsGetPropertyCrt() {
  static RsPropertyCrt crt;
  return &crt;
}

// Binary property transport retains its original logical offset, including the key reader's eight-byte accounting.
// Declare String before key overloads so MSVC emits the original key-first read/write virtual groups.
class RsPropertyReader {
public:
  // Capture the real record size before beginning immediately after the four-byte format word.
  explicit RsPropertyReader(RsStream *input) : stream(input), position(4), size(0) {
    stream->GetSize(&size);
  }

  // Retire only the borrowed transport view; the property set owns its stream reference.
  virtual ~RsPropertyReader() = default;

  // Advance the logical offset before the strict size gate, then retain every reached descriptor output write.
  virtual bool ReadDescriptor(RsProperty *property) {
    position += 8;
    if (position >= size)
      return false;
    bool result = stream->ReadU32(&property->id);
    result = result && stream->ReadU16(&property->type) && stream->ReadU16(&property->count);
    return result;
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(u8 *out) {
    position += 1;
    if (position >= size)
      return false;
    return stream->ReadU8(out);
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(u16 *out) {
    position += 2;
    if (position >= size)
      return false;
    return stream->ReadU16(out);
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(u32 *out) {
    position += 4;
    if (position >= size)
      return false;
    return stream->ReadU32(out);
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(i32 *out) {
    position += 4;
    if (position >= size)
      return false;
    return stream->ReadI32(out);
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(i64 *out) {
    position += 8;
    if (position >= size)
      return false;
    return stream->ReadI64(out);
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(f32 *out) {
    position += 4;
    if (position >= size)
      return false;
    return stream->ReadF32(out);
  }

  // Read text before accounting its live length. A failed read retains its output effects without advancing the
  // counter.
  virtual bool Read(ScString *out) {
    bool result = stream->ReadString(out);
    if (result) {
      position += out->Strlen() + 4;
      if (position > size)
        return false;
    }
    return result;
  }

  // Advance the original logical byte count and forward the typed output only while strictly inside the record.
  virtual bool Read(RsKey *out) {
    position += 8;
    if (position >= size)
      return false;
    return stream->ReadKey(out);
  }

  // Preserve the identifier write result while still attempting both descriptor halves after a successful identifier
  // write. The original name and descriptor arguments are mutable, even though this binary path does not edit them.
  virtual bool WriteDescriptor(ScString *, RsProperty *property) {
    bool result = stream->WriteU32(property->id);
    if (result) {
      stream->WriteU16(property->type);
      stream->WriteU16(property->count);
    }
    return result;
  }

  // Transfer the concrete byte element and retain the Boolean stream result.
  virtual bool Write(u8 *value) {
    return stream->WriteU8(*value);
  }

  // Transfer the concrete word element and retain the Boolean stream result.
  virtual bool Write(u16 *value) {
    return stream->WriteU16(*value);
  }

  // Transfer the concrete unsigned integer element and retain the Boolean stream result.
  virtual bool Write(u32 *value) {
    return stream->WriteU32(*value);
  }

  // Transfer the concrete signed integer element and retain the Boolean stream result.
  virtual bool Write(i32 *value) {
    return stream->WriteI32(*value);
  }

  // Transfer the concrete wide integer element without narrowing either word.
  virtual bool Write(i64 *value) {
    return stream->WriteI64(*value);
  }

  // Transfer the concrete floating-point element through the genuine typed stream operation.
  virtual bool Write(f32 *value) {
    return stream->WriteF32(*value);
  }

  // Forward the owned String through its real interface rather than introducing a temporary character buffer.
  virtual bool Write(ScString *value) {
    return stream->WriteString(value);
  }

  // Preserve the key stream's group-first serialization and its Boolean return.
  virtual bool Write(RsKey *value) {
    return stream->WriteKey(value);
  }

protected:
  RsStream *stream;
  u32 position, size;
};

// Text property transport uses the same retained stream and logical cursor as its binary base.
class RsTextPropertyReader : public RsPropertyReader {
public:
  // Initialize the shared size/cursor fields; the derived transport adds no retained state.
  explicit RsTextPropertyReader(RsStream *stream) : RsPropertyReader(stream) {
  }

  // Discard the label and read the three bracketed integer fields. Publish the descriptor only after the equals sign.
  bool ReadDescriptor(RsProperty *property) override {
    ScString label;
    bool result = ReadToken(&label, '[');
    u32 id = 0, type = 0, count = 0;
    result = result && ReadNumber(&id, ',') && ReadNumber(&type, ',') && ReadNumber(&count, ']') &&
        ReadToken(&label, '=');
    if (result)
      *property = {id, (u16)type, (u16)count};
    return result;
  }

  // Read through the original 32-bit temporary before narrowing.
  // Zero initialization excludes its undefined failed read.
  bool Read(u8 *out) override {
    u32 value = 0;
    bool result = ReadNumber(&value, ',');
    *out = (u8)value;
    return result;
  }

  // Read through the original 32-bit temporary before narrowing.
  // Zero initialization excludes its undefined failed read.
  bool Read(u16 *out) override {
    u32 value = 0;
    bool result = ReadNumber(&value, ',');
    *out = (u16)value;
    return result;
  }

  // Parse the next comma-delimited integer into the original 32-bit unsigned storage.
  bool Read(u32 *out) override {
    return ReadNumber(out, ',');
  }

  // A missing token preserves the output; a present but invalid integer clears it and returns false.
  bool Read(i32 *out) override {
    ScString token;
    bool result = ReadToken(&token, ',');
    if (result) {
      long value = 0;
      if (RsGetPropertyCrt()->Scan(token.ToChar(), RS_SCAN_I32, &value) == 1)
        *out = (i32)value;
      else {
        *out = 0;
        result = false;
      }
    }
    return result;
  }

  // Parse the full signed wide integer with base detection. Conversion failure clears both output words.
  bool Read(i64 *out) override {
    ScString token;
    bool result = ReadToken(&token, ',');
    if (result && RsGetPropertyCrt()->Scan(token.ToChar(), RS_SCAN_I64, out) != 1) {
      *out = 0;
      result = false;
    }
    return result;
  }

  // Parse the comma-delimited floating-point field. A conversion failure clears the reached output.
  bool Read(f32 *out) override {
    ScString token;
    bool result = ReadToken(&token, ',');
    if (result && RsGetPropertyCrt()->Scan(token.ToChar(), "%f", out) != 1) {
      *out = 0;
      result = false;
    }
    return result;
  }

  // Read type, group and instance through live virtual integer operations, publishing the key only after all succeed.
  bool Read(RsKey *out) override {
    u32 type = 0, group = 0, instance = 0;
    bool result = Read(&type);
    result = result && Read(&group) && Read(&instance);
    if (result)
      *out = RsKey(type, group, instance);
    return result;
  }

  // Retain the token's reached byte edits, including its explicit terminating zero in the logical String length.
  bool Read(ScString *out) override {
    return ReadToken(out, ',');
  }

  // Capture descriptor fields before the name query, then emit the exact original CR/LF and bracket syntax.
  bool WriteDescriptor(ScString *name, RsProperty *property) override {
    ScString text;
    i32 id = (i32)property->id;
    u32 type = property->type, count = property->count;
    const char *label = name->ToChar();
    text.Sprintf("%c%c%s[ %d, %d, %d ]= ", 13, 10, label, id, type, count);
    return WriteText(&text);
  }

  // Format the original u8 element with the installed CRT and preserve the Boolean output result.
  bool Write(u8 *value) override {
    ScString text;
    text.Sprintf("%u, ", (u32)*value);
    return WriteText(&text);
  }

  // Format the original u16 element with the installed CRT and preserve the Boolean output result.
  bool Write(u16 *value) override {
    ScString text;
    text.Sprintf("%u, ", (u32)*value);
    return WriteText(&text);
  }

  // Format the original u32 element with the installed CRT and preserve the Boolean output result.
  bool Write(u32 *value) override {
    ScString text;
    text.Sprintf("%lu, ", (unsigned long)*value);
    return WriteText(&text);
  }

  // Format the original i32 element with the installed CRT and preserve the Boolean output result.
  bool Write(i32 *value) override {
    ScString text;
    text.Sprintf("%ld, ", (long)*value);
    return WriteText(&text);
  }

  // Format the original i64 element with the installed CRT and preserve the Boolean output result.
  bool Write(i64 *value) override {
    ScString text;
    text.Sprintf("%" SC3_PRId64 ", ", *value);
    return WriteText(&text);
  }

  // Format the original f32 element with the installed CRT and preserve the Boolean output result.
  bool Write(f32 *value) override {
    ScString text;
    text.Sprintf("%f, ", (f64)*value);
    return WriteText(&text);
  }

  // Format the String's null-terminated view without escaping its original punctuation or embedded terminator.
  bool Write(ScString *value) override {
    ScString text;
    text.Sprintf("%s, ", value->ToChar());
    return WriteText(&text);
  }

  // Write type, group and instance through the live virtual word operation, stopping on the first zero result.
  bool Write(RsKey *value) override {
    bool result = Write(&value->type);
    result = result && Write(&value->group) && Write(&value->instance);
    return result;
  }

private:
  // Write the concrete String with length queried before its data pointer, preserving the transport result.
  bool WriteText(ScString *text) {
    u32 length = text->Strlen();
    const char *data = text->ToChar();
    return stream->WriteBlock(data, length);
  }

  // Advance before the strict bound gate and normalize the real one-byte raw-block result to Boolean.
  bool ReadChar(char *out) {
    return ++position < size && stream->ReadBlock(out, 1) != 0;
  }

  // Clear the actual live String range, then scan signed-char tokens, comments and whitespace in place. A reached
  // delimiter wins even when its stream read returned false; concrete Erase takes a count rather than std-string npos.
  // The explicit NUL is part of the logical range, so the original Trim leaves spaces immediately before it intact.
  // The first character is initialized only to exclude the original uninitialized byte on an unwritten failed read.
  bool ReadToken(ScString *out, char delimiter) {
    out->Erase(0, out->ScString::Strlen());
    char value = 0;
    while (true) {
      bool result = ReadChar(&value);
      if (value == delimiter) {
        char zero = 0;
        out->Append(&zero, 1);
        out->Trim();
        return true;
      }
      if (value == ';') {
        while (result) {
          result = ReadChar(&value);
          if (value == '\r')
            break;
        }
        if (!result)
          return false;
        continue;
      }
      if (value != '\n') {
        if (value == '\r' || value == '\t')
          value = ' ';
        out->Append(&value, 1);
      }
      if (!result)
        return false;
    }
  }

  // Parse an original-width integer after token completion. Missing tokens preserve output; malformed tokens clear it.
  bool ReadNumber(u32 *out, char delimiter) {
    ScString token;
    bool result = ReadToken(&token, delimiter);
    if (result) {
      long value = 0;
      if (RsGetPropertyCrt()->Scan(token.ToChar(), RS_SCAN_I32, &value) == 1)
        *out = (u32)value;
      else {
        *out = 0;
        result = false;
      }
    }
    return result;
  }
};

// Original owned value objects carry the eight-byte descriptor and a concrete property-name String.
// Typed arrays are separate derived objects, matching the original specialized value vtables.
class RsPropertyValue {
public:
  // Construct the owned name before publishing the supplied descriptor, as in the original value constructors.
  explicit RsPropertyValue(const RsProperty &input) {
    property = input;
  }

  // Destroy the owned name after derived typed storage has retired.
  virtual ~RsPropertyValue() = default;
  virtual bool Read(RsPropertyReader *reader) = 0;
  virtual bool Write(RsPropertyReader *writer) = 0;
  // Original value tables expose key/String/float/wide/signed/word/short/byte getters, then matching setters.
  virtual bool GetKey(RsKey *out, u32 index) = 0;
  virtual bool GetText(ScStringView *out, u32 index) = 0;
  virtual bool GetF32(f32 *out, u32 index) = 0;
  virtual bool GetI64(i64 *out, u32 index) = 0;
  virtual bool GetI32(i32 *out, u32 index) = 0;
  virtual bool GetWord(u32 *out, u32 index) = 0;
  virtual bool GetU16(u16 *out, u32 index) = 0;
  virtual bool GetByte(u8 *out, u32 index) = 0;
  virtual bool SetKey(const RsKey &value, u32 index) = 0;
  virtual bool SetText(const ScStringView &value, u32 index) = 0;
  virtual bool SetF32(const f32 &value, u32 index) = 0;
  virtual bool SetI64(const i64 &value, u32 index) = 0;
  virtual bool SetI32(const i32 &value, u32 index) = 0;
  virtual bool SetWord(const u32 &value, u32 index) = 0;
  virtual bool SetU16(const u16 &value, u32 index) = 0;
  virtual bool SetByte(const u8 &value, u32 index) = 0;
  RsProperty property;
  ScString name;
};

// The original eight value constructors share descriptor/name/typed-array storage and specialized accessors.
// Native scalar zero initialization excludes original unread allocation bytes; it is not a stock default-value
// contract.
template <class T, u16 Kind> class RsArrayProperty final : public RsPropertyValue {
public:
  // Allocate the original number of typed elements; Strings and keys retain their ordinary value construction.
  explicit RsArrayProperty(const RsProperty &property) : RsPropertyValue(property), values(new T[property.count]()) {
  }

  // Free only the owned typed array before the shared descriptor/name base retires.
  ~RsArrayProperty() override {
    delete[] values;
  }

  // Read each live element in increasing order, retaining partial writes before a failed Boolean transfer.
  bool Read(RsPropertyReader *reader) override {
    bool result = true;
    for (u32 index = 0; index < property.count && result; ++index)
      result = reader->Read(values + index);
    return result;
  }

  // Write each live element through its mutable source pointer and stop on the first failed Boolean transfer.
  bool Write(RsPropertyReader *writer) override {
    bool result = true;
    for (u32 index = 0; index < property.count && result; ++index)
      result = writer->Write(values + index);
    return result;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool GetByte(u8 *out, u32 index) override {
    if constexpr (Kind == 1) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool GetWord(u32 *out, u32 index) override {
    if constexpr (Kind == 3) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool GetKey(RsKey *out, u32 index) override {
    if constexpr (Kind == 8) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool SetByte(const u8 &value, u32 index) override {
    if constexpr (Kind == 1) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool SetWord(const u32 &value, u32 index) override {
    if constexpr (Kind == 3) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool SetText(const ScStringView &value, u32 index) override {
    if constexpr (Kind == 7) {
      if (index >= property.count)
        return false;
      ScString copy(value);
      values[index] = copy;
      return true;
    } else
      return false;
  }

  // Apply only the original matching type and in-range element; unsupported views leave caller storage untouched.
  bool SetKey(const RsKey &value, u32 index) override {
    if constexpr (Kind == 8) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Transfer the matching u16 element only when the unsigned index is within the live descriptor count.
  bool GetU16(u16 *out, u32 index) override {
    if constexpr (Kind == 2) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Transfer the matching u16 element only when the unsigned index is within the live descriptor count.
  bool SetU16(const u16 &value, u32 index) override {
    if constexpr (Kind == 2) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Transfer the matching i32 element only when the unsigned index is within the live descriptor count.
  bool GetI32(i32 *out, u32 index) override {
    if constexpr (Kind == 4) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Transfer the matching i32 element only when the unsigned index is within the live descriptor count.
  bool SetI32(const i32 &value, u32 index) override {
    if constexpr (Kind == 4) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Transfer the matching i64 element only when the unsigned index is within the live descriptor count.
  bool GetI64(i64 *out, u32 index) override {
    if constexpr (Kind == 6) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Transfer the matching i64 element only when the unsigned index is within the live descriptor count.
  bool SetI64(const i64 &value, u32 index) override {
    if constexpr (Kind == 6) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Transfer the matching f32 element only when the unsigned index is within the live descriptor count.
  bool GetF32(f32 *out, u32 index) override {
    if constexpr (Kind == 5) {
      if (index >= property.count)
        return false;
      *out = values[index];
      return true;
    } else
      return false;
  }

  // Transfer the matching f32 element only when the unsigned index is within the live descriptor count.
  bool SetF32(const f32 &value, u32 index) override {
    if constexpr (Kind == 5) {
      if (index >= property.count)
        return false;
      values[index] = value;
      return true;
    } else
      return false;
  }

  // Assign the source's null-terminated text through the caller's one-argument String operation.
  bool GetText(ScStringView *out, u32 index) override {
    if constexpr (Kind == 7) {
      if (index >= property.count)
        return false;
      out->FromChar(values[index].ToChar());
      return true;
    } else
      return false;
  }

private:
  T *values;
};

static bool RsSameProperty(const RsProperty &first, const RsProperty &second) {
  return first.id == second.id && first.type == second.type && first.count == second.count;
}

RsProperties::RsProperties() : references(0), source(nullptr), format(0), initialized(0), text(0), writable(0) {
}

RsProperties::~RsProperties() {
  Shutdown();
}

// Publish the actual property-data identity before its virtual ownership acquisition. Failed queries clear the outputs.
bool RsProperties::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsPropertySet);
  HANDLE_QUERY_INTERFACE(IID_RsPropertySet, RsPropertySet);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 RsProperties::AddRef() {
  return ++references;
}

// Counts zero and one both destroy the object; higher counts decrement and return the live count.
u32 RsProperties::Release() {
  if (references)
    --references;
  u32 remaining = references;
  if (!remaining) {
    delete this;
    return 0;
  }
  return remaining;
}

// Preserve the original stream release before value destruction and clear the active state after map retirement.
// Both serializers run before releasing the source, through their original format-specific descriptor/value transport.
void RsProperties::Shutdown() {
  if (source) {
    if (writable) {
      if (text) {
        bool result = WriteCookieToSerialRecord(source, kRsTextPropertyFormatTag);
        RsTextPropertyReader writer(source);
        if (result)
          WriteProperties(&writer);
      } else {
        bool result = WriteCookieToSerialRecord(source, kRsBinaryPropertyFormatTag);
        RsPropertyReader writer(source);
        if (result)
          WriteProperties(&writer);
      }
    }
    source->Release();
    source = nullptr;
  }
  for (const auto &entry : entries)
    delete entry.second;
  entries.clear();
  initialized = 0;
}

// Publish the identifier match before comparing its complete descriptor.
bool RsProperties::Find(const RsProperty &property, RsPropertyValue **out) {
  auto entry = entries.find(property.id);
  if (entry == entries.end())
    return false;
  *out = entry->second;
  if (RsSameProperty(entry->second->property, property))
    return true;
  *out = nullptr;
  return false;
}

// Create a new nonempty descriptor, publishing an existing match even though that case returns false.
// A mismatched entry loses its tree node without destroying the displaced value, as in the installed method.
bool RsProperties::Create(const RsProperty &property, RsPropertyValue **out) {
  if (!property.count)
    return false;
  auto entry = entries.find(property.id);
  if (entry != entries.end()) {
    *out = entry->second;
    if (RsSameProperty(entry->second->property, property))
      return false;
    entries.erase(entry);
  }
  RsPropertyValue *value;
  switch (property.type) {
  case 1:
    value = new RsArrayProperty<u8, 1>(property);
    break;
  case 2:
    value = new RsArrayProperty<u16, 2>(property);
    break;
  case 3:
    value = new RsArrayProperty<u32, 3>(property);
    break;
  case 4:
    value = new RsArrayProperty<i32, 4>(property);
    break;
  case 5:
    value = new RsArrayProperty<f32, 5>(property);
    break;
  case 6:
    value = new RsArrayProperty<i64, 6>(property);
    break;
  case 7:
    value = new RsArrayProperty<ScString, 7>(property);
    break;
  case 8:
    value = new RsArrayProperty<RsKey, 8>(property);
    break;
  default:
    return false;
  }
  *out = value;
  entries.insert({property.id, value});
  return true;
}

// Read descriptors and values in order, retaining partial objects on failure and the ID-one terminator result.
bool RsProperties::ReadProperties(RsPropertyReader *reader) {
  bool result = true;
  while (result) {
    RsProperty property;
    result = reader->ReadDescriptor(&property);
    if (!result || property.id == 1)
      break;
    RsPropertyValue *value = nullptr;
    if (!Create(property, &value))
      return false;
    result = value->Read(reader);
  }
  return result;
}

// Emit ordered named descriptors and values until a failed write, then append the original ID-one, zero-word
// terminator.
bool RsProperties::WriteProperties(RsPropertyReader *writer) {
  bool result = true;
  for (const auto &entry : entries) {
    if (!result)
      return result;
    RsPropertyValue *value = entry.second;
    ScString name;
    name = value->name;
    result = writer->WriteDescriptor(&name, &value->property);
    if (result)
      result = value->Write(writer);
  }
  if (!result)
    return result;
  RsProperty descriptor{1, 3, 1};
  RsArrayProperty<u32, 3> terminal(descriptor);
  u32 zero = 0;
  terminal.SetWord(zero, 0);
  ScString name("TK_TKR_End");
  result = writer->WriteDescriptor(&name, &terminal.property);
  if (result)
    result = terminal.Write(writer);
  return result;
}

// Publish writable text state in original order before retaining the selected output stream.
bool RsProperties::InitText(RsStream *stream) {
  if (initialized)
    return false;
  initialized = 1;
  writable = 1;
  text = 1;
  format = kRsTextPropertyFormatTag;
  source = stream;
  stream->AddRef();
  return true;
}

// Publish writable binary state in original order before retaining the selected output stream.
bool RsProperties::InitBinary(RsStream *stream) {
  if (initialized)
    return false;
  text = 0;
  initialized = 1;
  writable = 1;
  format = kRsBinaryPropertyFormatTag;
  source = stream;
  stream->AddRef();
  return true;
}

// Read the selected real format while retaining the binary reader's original logical offset and size gates.
// Select the captured format once; a failed load retires its retained stream and all partially loaded values.
bool RsProperties::ReadFormat(RsStream *stream) {
  u32 selected = format;
  if (selected == kRsTextPropertyFormatTag) {
    text = 1;
    RsTextPropertyReader reader(stream);
    initialized = ReadProperties(&reader);
  } else if (selected == kRsBinaryPropertyFormatTag) {
    text = 0;
    RsPropertyReader reader(stream);
    initialized = ReadProperties(&reader);
  }
  if (!initialized)
    Shutdown();
  return initialized != 0;
}

// Retain the stream before reading its cookie, then select the original transport and retire partial state on failure.
bool RsProperties::Read(RsStream *stream) {
  if (initialized)
    return false;
  source = stream;
  stream->AddRef();
  writable = 0;
  format = ReadCookieFromSerialRecord(stream);
  return ReadFormat(stream);
}

// Retain the already-positioned stream with its supplied cookie and preserve the original failure cleanup.
bool RsProperties::ReadWithFormat(RsStream *stream, u32 inputFormat) {
  if (initialized)
    return false;
  writable = 0;
  source = stream;
  stream->AddRef();
  format = inputFormat;
  return ReadFormat(stream);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetByte(const RsProperty &property, u8 *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetByte(out, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetWord(const RsProperty &property, u32 *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetWord(out, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetKey(const RsProperty &property, RsKey *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetKey(out, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetByte(const RsProperty &property, const u8 &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetByte(value, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetWord(const RsProperty &property, const u32 &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetWord(value, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetText(const RsProperty &property, const ScStringView &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetText(value, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetKey(const RsProperty &property, const RsKey &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetKey(value, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetU16(const RsProperty &property, u16 *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetU16(out, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetU16(const RsProperty &property, const u16 &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetU16(value, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetI32(const RsProperty &property, i32 *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetI32(out, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetI32(const RsProperty &property, const i32 &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetI32(value, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetI64(const RsProperty &property, i64 *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetI64(out, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetI64(const RsProperty &property, const i64 &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetI64(value, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetF32(const RsProperty &property, f32 *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetF32(out, index);
}

// Find or create the original descriptor before dispatching its typed setter with the genuine separate index.
bool RsProperties::SetF32(const RsProperty &property, const f32 &value, u32 index) {
  RsPropertyValue *entry = nullptr;
  bool found = Find(property, &entry);
  if (!found)
    found = Create(property, &entry);
  if (!found)
    return false;
  return entry->SetF32(value, index);
}

// Require the complete descriptor before dispatching the original typed getter; a miss preserves the output.
bool RsProperties::GetText(const RsProperty &property, ScStringView *out, u32 index) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  return entry->GetText(out, index);
}

// Copy the original mutable String input into independent concrete storage before assigning the matched field name.
bool RsProperties::SetName(const RsProperty &property, ScStringView *name) {
  RsPropertyValue *entry = nullptr;
  if (!Find(property, &entry))
    return false;
  ScString copy(*name);
  entry->name = copy;
  return true;
}

// Copy the retained stream key through the original const receiver; absence preserves the genuine output.
bool RsProperties::CopyKey(RsKey *out) const {
  if (!source)
    return false;
  *out = *source->GetKey();
  return true;
}

u32 RsProperties::GetFormat() const {
  return initialized ? format : 0;
}

bool RsProperties::IsText() const {
  return text != 0;
}

bool RsProperties::HasProperty(const RsProperty &property) {
  auto entry = entries.find(property.id);
  return entry != entries.end() && RsSameProperty(entry->second->property, property);
}

u32 RsProperties::ReadCookieFromSerialRecord(RsStream *stream) {
  u32 cookie = 0;
  return stream->ReadU32(&cookie) ? cookie : 0;
}

bool RsProperties::WriteCookieToSerialRecord(RsStream *stream, u32 cookie) {
  return stream->WriteU32(cookie);
}

static ScLock RsRegion;

void RsEnterResourceRegion() {
  RsRegion.Lock();
}

void RsLeaveResourceRegion() {
  RsRegion.Unlock();
}

RsResource::RsResource() : key{}, references(0) {
}

RsResource::~RsResource() {
}

// Publish the resource identity before retaining it. Unsupported identifiers clear the output.
bool RsResource::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, RsResource);
  HANDLE_QUERY_INTERFACE(IID_RsResource, RsResource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Return the count captured under the lock, before another resource operation can change it.
u32 RsResource::AddRef() {
  RsEnterResourceRegion();
  u32 result = ++references;
  RsLeaveResourceRegion();
  return result;
}

// Capture the final-reference decision under the lock. The surviving branch rereads the count after unlock.
u32 RsResource::Release() {
  RsEnterResourceRegion();
  bool zero = --references == 0;
  RsLeaveResourceRegion();
  if (zero) {
    delete this;
    return 0;
  }
  return references;
}

// Return the original mutable key aggregate stored immediately after the resource interface.
RsKey *RsResource::GetKey() {
  return &key;
}

// The const resource-key view shares the original folded address with the mutable views and borrows the same aggregate.
const RsKey *RsResource::GetKey() const {
  return &key;
}

u32 RsResource::RefCount() const {
  return references;
}

// Borrow the mutable type word in the original key aggregate without copying or normalizing it.
u32 *RsResource::Type() {
  return &key.type;
}

// Borrow the const type word through the folded accessor, retaining its original address and ownership.
const u32 *RsResource::Type() const {
  return &key.type;
}

// Borrow the mutable group word in the original key aggregate without copying or normalizing it.
u32 *RsResource::Group() {
  return &key.group;
}

// Borrow the const group word through the folded accessor, retaining its original address and ownership.
const u32 *RsResource::Group() const {
  return &key.group;
}

// Borrow the mutable instance word in the original key aggregate without copying or normalizing it.
u32 *RsResource::Instance() {
  return &key.instance;
}

// Borrow the const instance word through the folded accessor, retaining its original address and ownership.
const u32 *RsResource::Instance() const {
  return &key.instance;
}

bool RsResource::DebugInstanceTag(ScStringView *) {
  return false;
}

bool RsResource::DebugClassTag(ScStringView *) {
  return false;
}

bool RsResource::DebugTypeTag(ScStringView *) {
  return false;
}

RsString::RsString() : ScString(), RsResource() {
}

RsString::~RsString() {
}

bool RsString::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_RsResource, RsResource);
  return ScString::QueryInterface(iid, out);
}

u32 RsString::AddRef() {
  return RsResource::AddRef();
}

u32 RsString::Release() {
  return RsResource::Release();
}
