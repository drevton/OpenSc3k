#ifndef SC3_BUILDINGS_ARCHITECT_IMAGE_H
#define SC3_BUILDINGS_ARCHITECT_IMAGE_H

#include "buildings/architect/bd_geometry.h"
#include "common/sc3_unknown.h"

struct BdCodecFile;
struct BdCodecState;
class GxBuffer;

// Readers allocate1660-byte records; the original writer uses a larger3468-byte allocation with this same prefix.
struct BdCodecInfo {
  u32 signature, length, version, field_00C, width, height, bits, source_bits, palette_first, palette_count;
  u32 palette[256];
  u32 subtype, compressed, field_430, file_size, alpha_bits, red_bits, green_bits, blue_bits;
  u32 field_448, field_44C;
  i32 x_origin, y_origin;
  u32 field_458;
  u8 field_45C, unrecovered_45D[543];
};

struct BdCodecPixels {
  BdCodecInfo *info;
  u8 *pixels;
  u32 pitch;
};

struct BdCodecDescriptor {
  u32 signature, length, version, flags, field_010, field_014, quality, format, field_020;
  u8 field_024[24];
  char extensions[8][8], author[32], release[8], short_name[8], type[16], description[32];
};

// Original reader dispatch and decoded-image ownership entry points.
i32 BD_STDCALL BdSelectCodec(u32 index);
u32 BdSelectedCodec();
u32 BD_STDCALL BdSelectCodecExtension(const char *extension, i32 *slot);
BdCodecFile *BD_STDCALL BdCreateCodecFile(const char *path);
i32 BD_STDCALL BdOpenCodecWriter(BdCodecState **state, BdCodecFile *file, const char *path, i32 field_0C);
i32 BD_STDCALL BdWriteCodecPixels(BdCodecState *state, const BdCodecInfo *info, const u8 *source, i32 pitch);
i32 BD_STDCALL BdCloseCodecWriter(BdCodecState *state);
BdCodecFile *BD_STDCALL BdOpenCodec(const char *path, char *resolved_path);
BdCodecDescriptor *BdDescribeCodec();
i32 BD_STDCALL BdReadCodecPixels(BdCodecState *state, const BdCodecInfo *info, u8 *out, i32 pitch);
i32 BD_STDCALL BdCanReadCodec(BdCodecFile *file);
i32 BD_STDCALL BdReadCodecInfo(BdCodecState **state, BdCodecFile *file, const char *path);
BdCodecInfo *BD_STDCALL BdCodecInfoAt(BdCodecState *state, i32 index);
BdCodecPixels *BD_STDCALL BdDecodeImage(const char *path, i32 index);
void BD_STDCALL BdDestroyCodecPixels(BdCodecPixels *pixels);
i32 BD_STDCALL BdCloseCodecState(BdCodecState *state);
i32 BD_STDCALL BdCloseCodecFile(BdCodecFile *file);
i32 BD_STDCALL BdFreeCodec(void *allocation);

bool BD_STDCALL BdReadBufferPixels(GxBuffer *source, const BdCodecInfo *info, BdCodecPixels *pixels);
u8 BD_STDCALL BdCopyTexturePixels(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);

class GxGraphics;
class GxBuffer;
class ScStringView;
struct BdCodecInfo;
struct BdCodecPixels;

// IID02FFBB60 exposes the shared image-object, load, save and black-region operations.
class BdImageIoInterface : public ScUnknown {
public:
  virtual bool CreateObject(u32 iid, void **out) = 0;
  virtual bool LoadImage(const ScStringView *path, GxBuffer **out, u32 mode, i32 format, i32 bits) = 0;
  virtual bool SaveImage(const ScStringView *path, GxBuffer *source, const BdRect *bounds, u32 quality) = 0;
  virtual bool SaveImage(const ScStringView *path, GxBuffer *source, u32 quality) = 0;
  virtual bool IsBlack(GxBuffer *source) = 0;
  virtual bool IsBlack(GxBuffer *source, const BdRect *bounds) = 0;
};

// Registered classA2FFB5D3 provides the architect's implementation of the image interface.
class BdImageIo : public BdImageIoInterface {
public:
  BdImageIo();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool CreateObject(u32 iid, void **out) override;
  bool LoadImage(const ScStringView *path, GxBuffer **out, u32 mode, i32 format, i32 bits) override;
  bool SaveImage(const ScStringView *path, GxBuffer *source, const BdRect *bounds, u32 quality) override;
  bool SaveImage(const ScStringView *path, GxBuffer *source, u32 quality) override;
  bool IsBlack(GxBuffer *source) override;
  bool IsBlack(GxBuffer *source, const BdRect *bounds) override;
  virtual ~BdImageIo();

  bool CopyImage(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);
  bool CopyIndexed(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);
  bool CopyColor(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);

  u32 references;
  GxGraphics *graphics;
};

class GxGraphics;
class GxBuffer;
class ScStringView;
struct BdCodecInfo;
struct BdCodecPixels;

// ClassA2FFB5D4 is registered by SIMBABLD and exposes IID02FFBB61; its source object is12 bytes on the original PC.
// Preserve its actual virtual operation sequence and direct codec-factory ownership.
class BdImageLoader : public ScUnknown {
public:
  BdImageLoader();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual bool CreateObject(u32 iid, void **out);
  virtual bool LoadImage(const ScStringView *path, GxBuffer **out, u32 mode, i32 format, i32 bits);
  virtual bool LoadTexture(const ScStringView *path, GxBuffer **out, BdRect *bounds);
  virtual bool LoadMasked(const ScStringView *path, GxBuffer **color, GxBuffer **mask);
  virtual bool SaveImage(const ScStringView *path, GxBuffer *source, const BdRect *bounds);
  virtual bool SaveImage(const ScStringView *path, GxBuffer *source);
  virtual ~BdImageLoader();

  bool CopyImage(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);
  bool CopyIndexed(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);
  bool CopyColor(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *out);
  bool CopyMasked(const BdCodecInfo *info, const BdCodecPixels *pixels, GxBuffer *color, GxBuffer *mask);

  u32 references;
  GxGraphics *graphics;
};

// Original18942 manager accessor; manager+48 owns this concrete image-loader interface.
BdImageLoader *BdGetImageLoader();

#endif
