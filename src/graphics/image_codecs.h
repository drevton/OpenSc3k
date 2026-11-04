#ifndef SC3_GRAPHICS_IMAGE_CODECS_H
#define SC3_GRAPHICS_IMAGE_CODECS_H

#include <stdio.h>

#include <cstddef>
#include <cstdio>

#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"

// File codecs expose JPEG plus a nil selection for unsupported inputs.
enum GxGimexFormat : i32 {
  kGxGimexNone = 0,
  kGxGimexJpeg = 1,
  kGxGimexFormatCount = 2,
};

// Indexed image conversion marks each native-color cache entry as not yet converted.
enum GxPaletteCacheValue : u32 {
  kGxUnconvertedPaletteEntry = 0xffffffff,
};

// Image readers and codec descriptors share these format signatures and retain distinct image/codec forms where required.
enum GxImageFormatTag : i32 {
  kGxBitmapCodecSignature = 0x424d50,
  kGxNilCodecSignature = 0x4e494c,
  kGxJpegSignature = 0x4a504547,
  kGxBitmapImageSignature = 0x424d5020,
  kGxBitmapFormatTag = 0x42494e41,
  kGxBitmapExtensionTag = 0x2e424d50,
  kGxTargaFormatTag = 0x54504943,
  kGxTargaCodecSignature = 0x544741,
  kGxTargaImageSignature = 0x54474120,
};

// Use IJG's native ABI, including host long widths and all library-owned fields.
// Scope its C typedefs to avoid collisions with Windows INT32 and boolean in the single game unit.
namespace GxJpeg {
extern "C" {
#pragma push_macro("FAR")
#undef FAR
#include "../../third_party/jpeg6a/jpeglib.h"
#pragma pop_macro("FAR")
}
}

#if defined(_MSC_VER) && defined(_M_IX86)
#define GX_GIMEX_CALL __stdcall
#else
#define GX_GIMEX_CALL
#endif

class GxBuffer;
class GxGraphics;
class ScStringView;
struct GxGimexInfo;
struct GxGimexPixels;

// The image-I/O identity is shared with the architect implementation, while the GIMEX object owns its own lifetime.
class GxGimexImage : public ScUnknown {
public:
  GxGimexImage();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u8 GetGimexLib(u32 iid, void **out);
  virtual u8 LoadFromFile(const ScStringView &path, GxBuffer **out, GxVramRequest mode, GxColorType format);
  virtual u8 SaveToFile(const ScStringView &path, GxBuffer *source, u32 quality);
  virtual u8 SaveToFile(const ScStringView &path, GxBuffer *source, ScRect *bounds, u32 quality);
  virtual u8 IsBlackBuffer(GxBuffer *source);
  virtual u8 IsBlackBufferRect(GxBuffer *source, ScRect *bounds);
  virtual ~GxGimexImage();

  u8 CopyImage(GxGimexInfo *info, GxGimexPixels *pixels, GxBuffer *out);
  u8 CopyIndexed(GxGimexInfo *info, GxGimexPixels *pixels, GxBuffer *out);
  u8 CopyColor(GxGimexInfo *info, GxGimexPixels *pixels, GxBuffer *out);
  u8 TranslateSourceBufferToGimex(GxBuffer *source, GxGimexInfo *info, GxGimexPixels *pixels);
  u8 Translate16BufferToGimex(GxBuffer *source, GxGimexInfo *info, GxGimexPixels *pixels);

private:
  u32 references;
  GxGraphics *graphics;
};

// Original ARGB palette storage keeps its four byte channels in blue, green, red, alpha order.
struct GxGimexColor {
  u8 blue, green, red, alpha;
};

constexpr i32 kGxGimexLegacyInfoSize = 1660, kGxGimexRetailInfoSize = 3468;

// The installed reader allocates the older GINFO; the larger retail record shares this field prefix.
struct GxGimexInfo {
  i32 signature, length, version, frame_number, width, height, bits, source_bits, palette_first, palette_count;
  GxGimexColor palette[256];
  i32 subtype, compressed, quality, frame_size, alpha_bits, red_bits, green_bits, blue_bits;
  i32 center_x, center_y, default_x, default_y, hotspot_count;
  char frame_name[32], comment[256];
  u8 older_tail[256];
};

static_assert(sizeof(GxGimexInfo) == kGxGimexLegacyInfoSize);
static_assert(offsetof(GxGimexInfo, palette) == 0x28);
static_assert(offsetof(GxGimexInfo, frame_name) == 0x45c);
static_assert(offsetof(GxGimexInfo, comment) == 0x47c);
static_assert(offsetof(GxGimexInfo, older_tail) == 0x57c);

struct GxGimexPixels {
  GxGimexInfo *info;
  char *image;
  i32 row_bytes;
};

// The actual GIMEX director owns the two module-specific factory registrations.
class GxGimexDirector : public ScDllDirector {
public:
  GxGimexDirector();
  ~GxGimexDirector() override;
  bool OnStart(ScCom *com) override;
  bool OnUnload() override;
  u32 GetDirectorID() const override;
};

ScDirector *GxGetGimexDirector();

using GxJpegDecoder = GxJpeg::jpeg_decompress_struct;
using GxJpegEncoder = GxJpeg::jpeg_compress_struct;
using GxJpegCommon = GxJpeg::jpeg_common_struct;
using GxJpegErrorManager = GxJpeg::jpeg_error_mgr;
using GxJpegSource = GxJpeg::jpeg_source_mgr;
using GxJpegDestination = GxJpeg::jpeg_destination_mgr;
using GxJpegColorSpace = GxJpeg::J_COLOR_SPACE;
struct GxGimexFile;

// Retain the installed Win32 layout checks while allowing IJG's native layouts on other targets.

extern i32 GxGimexJpegArgbMarker;
i32 GxGimexJpegMarker(GxJpegDecoder *decoder);
// Only attachment effects are established; the original private nominal return remains unresolved.
void GxGimexAttachJpegSource(GxJpegDecoder *decoder, GxGimexFile *file);

#ifdef __cplusplus
#if defined(_MSC_VER)
#define GX_PIXEL_CDECL __cdecl
#elif defined(__i386__)
#define GX_PIXEL_CDECL __attribute__((cdecl))
#else
#define GX_PIXEL_CDECL
#endif

// Original cdecl void callbacks: retail procedure types102E/1033 and pointer types102F/1034.
// Mutable WORD/BYTE reference outputs use typed pointers, preserving their original argument order.
using GxPackFunction = void(GX_PIXEL_CDECL *)(u16 *out, u8 red, u8 green, u8 blue);
using GxUnpackFunction = void(GX_PIXEL_CDECL *)(u16 pixel, u8 *red, u8 *green, u8 *blue);

void GX_PIXEL_CDECL GxPack555(u16 *out, u8 red, u8 green, u8 blue);
void GX_PIXEL_CDECL GxPack655(u16 *out, u8 red, u8 green, u8 blue);
void GX_PIXEL_CDECL GxPack565(u16 *out, u8 red, u8 green, u8 blue);
void GX_PIXEL_CDECL GxPack556(u16 *out, u8 red, u8 green, u8 blue);
void GX_PIXEL_CDECL GxPack555Shifted(u16 *out, u8 red, u8 green, u8 blue);
void GX_PIXEL_CDECL GxUnpack555(u16 pixel, u8 *red, u8 *green, u8 *blue);
void GX_PIXEL_CDECL GxUnpack655(u16 pixel, u8 *red, u8 *green, u8 *blue);
void GX_PIXEL_CDECL GxUnpack565(u16 pixel, u8 *red, u8 *green, u8 *blue);
void GX_PIXEL_CDECL GxUnpack556(u16 pixel, u8 *red, u8 *green, u8 *blue);
void GX_PIXEL_CDECL GxUnpack555Shifted(u16 pixel, u8 *red, u8 *green, u8 *blue);
#endif
#ifdef RGB
#undef RGB
#endif
#ifdef __cplusplus
class GxPaletteView;
class GxDrawSurface;
class GxDrawDeviceSurface;
class GxDrawDepthSurface;
struct ScPoint;

// Original signed surface-restoration policy, from retail cIGZBuffer::SurfaceRestoreType.
enum class GxRestoreType : i32 { Default = -1, None = 0, Callback = 1, AutoRam = 2, AutoHd = 3 };

// Original cIGZBuffer::eLockFlags, with signed32 storage and the source's shared flood/boundary bit.
enum class GxLockFlags : i32 {
  None = 0,
  Fill = 1,
  Duplicate = 2,
  Line = 4,
  Rect = 8,
  Blt = 16,
  BltRotate = 32,
  Bits = 64,
  GetSetPixel = 128,
  FillCircle = 256,
  Circle = 512,
  FloodFill = 1024,
  BoundaryFill = 1024,
  All = 65535
};

// Original external buffer callback ABI: Linux PFPvPv_b identifies bool(void*, void*) and two opaque payloads.
using GxRestoreCallback = bool (*)(void *, void *);

// Original cGZBufferBltFlags: transparent color, constant Z depth and the blit flag mask, each one DWORD.
struct GxBltFlags {
  enum class Options : i32 {
    None = 0,
    Stretch = 1,
    Transparent = 2,
    FlipVertical = 4,
    FlipHorizontal = 8,
    ColorOnly = 16,
    ZOnly = 32,
    LessZ = 64,
    LessEqualZ = 128,
    OverwriteZ = 256,
    UseConstantZ = 512,
    AllZOptions = 992
  };
  u32 pixel, secondary, flags;
};

// Recovered general-buffer operations used by windows and fonts. Original vtable slot numbers are not a host-layout claim.
class GxBuffer : public ScUnknown {
public:
  virtual bool Init(u32 w, u32 h, GxColorType requested, GxPaletteView *colors, GxDepthType secondary,
      GxVramRequest options_value, bool flag) = 0;
  virtual bool Shutdown() = 0;
  virtual u8 Restore() = 0;
  virtual bool Uninitialize() = 0;
  virtual u8 Lock(GxLockFlags requested_flags) = 0;
  virtual u8 Unlock(GxLockFlags requested_flags) = 0;
  virtual bool IsLocked() const = 0;
  virtual u8 LockDepthSurface(GxLockFlags requested_flags) = 0;
  virtual u8 UnlockDepthSurface(GxLockFlags requested_flags) = 0;
  virtual bool IsDepthSurfaceLocked() const = 0;
  virtual bool IsReady() = 0;
  virtual u32 Width() const = 0;
  virtual u32 Height() const = 0;
  virtual const ScRect *GetBufferArea() const = 0;
  virtual void GetBufferArea(ScRect *out) const = 0;
  virtual u32 GetMemoryUsage() const = 0;
  virtual GxColorType GetColorType() const = 0;
  virtual void GetColorType(GxColorType *out) const = 0;
  virtual GxPaletteView *GetPalette() const = 0;
  virtual GxPaletteView *ReplacePalette(GxPaletteView *colors) = 0;
  virtual u32 GetBitsPerPixel() const = 0;
  virtual u32 GetBytesPerPixel() const = 0;
  virtual bool IsInVideoMemory() const = 0;
  virtual u8 IsTexture() const = 0;
  virtual GxDepthType GetZDepth() const = 0;
  virtual bool IsZBuffered() const = 0;
  // Installed GZGraphicD slots116,120,124 put the whole-buffer overload before the two rectangle overloads.
  virtual u8 Fill(u32 pixel, const ScRect &box, ScRect *clip) = 0;
  virtual u8 Fill(u32 pixel, i32 left, i32 top, i32 right, i32 bottom, ScRect *clip) = 0;
  virtual u8 Fill(u32 pixel, ScRect *clip) = 0;
  virtual u8 FillNoClip(u32 pixel, const ScRect &rectangle) = 0;
  virtual u8 FillNoClip(u32 pixel, i32 left, i32 top, i32 right, i32 bottom) = 0;
  virtual u8 FillNoClip(u32 pixel) = 0;
  virtual u8 ZFill(u32 pixel, const ScRect &rectangle) = 0;
  virtual u8 ZFill(u32 pixel) = 0;
  virtual u8 DrawRectangleOutline(const ScRect &box_input, u32 pixel, u32 thickness) = 0;
  virtual u8 DrawRectangleOutlineNoClip(const ScRect &box_input, u32 pixel, u32 thickness) = 0;
  virtual u8 FloodFill(i32 x, i32 y, u32 pixel) = 0;
  virtual u8 FloodFillNoClip(i32 x, i32 y, u32 pixel) = 0;
  virtual u8 BoundaryFill(i32 x, i32 y, u32 pixel, u32 border) = 0;
  virtual u8 BoundaryFillNoClip(i32 x, i32 y, u32 pixel, u32 border) = 0;
  virtual u8 GenericBufferClip(GxBuffer *receiver, ScRect *source, ScRect *destination, const ScRect *clip) = 0;
  virtual u8 GenericBufferClipStretch(GxBuffer *receiver, ScRect *source, ScRect *destination, const ScRect *clip,
      f32 *horizontal_scale, f32 *vertical_scale) = 0;
  virtual u32 GetPixel(u32 x, u32 y) const = 0;
  virtual void SetPixel(u32 x, u32 y, u32 pixel) = 0;
  virtual void SetPixelAlpha(u32 x, u32 y, u32 pixel, f32 opacity) = 0;
  virtual u32 GetZValue(u32 x, u32 y) const = 0;
  virtual void SetZValue(u32 x, u32 y, u32 pixel) = 0;
  virtual void SetTransparency(u32 pixel) = 0;
  virtual u8 GetTransparency() const = 0;
  virtual u8 IsPixelTransparent(u32 x, u32 y) = 0;
  virtual u8 GetTransparentColor(u32 *out) const = 0;
  virtual void RemoveTransparency() = 0;
  virtual void EnableZBufferBlit(GxBltFlags::Options flags_value) = 0;
  virtual void DisableZBufferBlit() = 0;
  virtual void SetBltFlags(const GxBltFlags &value_input) = 0;
  virtual void GetBltFlags(GxBltFlags *out) const = 0;
  virtual const GxBltFlags *GetBltFlags() const = 0;
  virtual u8 DrawLine(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness) = 0;
  virtual u8 DrawLineNoClip(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness) = 0;
  virtual u8 DrawLineAlpha(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness_value, f32 opacity) = 0;
  virtual u8 DrawLineNoClipAlpha(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness_value, f32 opacity) = 0;
  virtual u8 DrawCircle(i32 left, i32 top, i32 right, i32, u32 pixel, u32 thickness) = 0;
  virtual u8 DrawCircleNoClip(i32 left, i32 top, i32 right, i32, u32 pixel, u32 thickness) = 0;
  virtual u8 FillCircle(i32 left, i32 top, i32 right, i32, u32 pixel) = 0;
  virtual u8 FillCircleNoClip(i32 left, i32 top, i32 right, i32, u32 pixel) = 0;
  virtual u8 Duplicate(GxBuffer **out, GxVramRequest requested_mode_value) = 0;
  virtual u8 Blt(
      GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const ScRect *clip) = 0;
  virtual u8 Blt(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) = 0;
  virtual u8 BltNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input) = 0;
  virtual u8 BltNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) = 0;
  virtual u8 BltStretch(
      GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const ScRect *clip) = 0;
  virtual u8 BltStretch(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) = 0;
  virtual u8 BltStretchNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input) = 0;
  virtual u8 BltStretchNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) = 0;
  virtual u8 ZBlt(GxBuffer *source, u32) = 0;
  virtual u8 BltSrc(GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input,
      const ScRect *clip) = 0;
  virtual u8 BltNoClipSrc(
      GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input) = 0;
  virtual u8 BltStretchSrc(GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input,
      const ScRect *clip) = 0;
  virtual u8 BltStretchNoClipSrc(GxBuffer *destination, const ScRect &source_box_input, ScRect *destination_box) = 0;
  virtual void CopyBuffer(GxBuffer *source) = 0;
  virtual void CopyZBuffer(GxBuffer *source) = 0;
  virtual u8 BltTiled(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, i32 x_offset,
      i32 y_offset, const ScRect *clip) = 0;
  virtual u8 BltTiledNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      i32 x_offset, i32 y_offset) = 0;
  virtual u8 BltEdge(GxBuffer *source, const ScRect &slice_input, const ScRect &destination_input, u8 flags_value) = 0;
  virtual u8 RotateBltSimple(
      GxBuffer *source, const ScRect &source_rect, const ScPoint &destination, u32 angle, const ScRect *) = 0;
  virtual u8 RotateBlt(GxBuffer *source, const ScRect &source_rect_input, const ScRect &destination_input, f32 angle,
      const ScRect *clip) = 0;
  virtual u8 RotateBltStretch(GxBuffer *source, const ScRect &source_rect_input, const ScRect &destination_input,
      f32 angle, f32 scale, const ScRect *clip) = 0;
  virtual u8 AlphaBlt(GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity,
      const ScRect *clip) = 0;
  virtual u8 AlphaBltNoClip(
      GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity) = 0;
  virtual u8 AlphaSourcePrecalculate(const ScRect &rectangle, f32 opacity) = 0;
  virtual u8 AlphaBltSourcePrecalculated(GxBuffer *source, const ScRect &source_rect_input,
      const ScPoint &destination_input, f32 opacity, const ScRect *clip) = 0;
  virtual u8 AlphaBltNoClipSourcePrecalculated(
      GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity) = 0;
  virtual u8 AlphaFill(u32 pixel, const ScRect &rectangle_input, f32 opacity, const ScRect *clip) = 0;
  virtual u8 AlphaFillNoClip(u32 pixel, const ScRect &rectangle_input, f32 opacity) = 0;
  virtual u8 DrawRaisedRect(const ScRect &box_input, u8 red, u8 green, u8 blue, bool raised, bool filled) = 0;
  virtual u8 DrawRaisedRect(const ScRect &box_input, u32 pixel, bool raised, bool filled) = 0;
  virtual u8 ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) = 0;
  virtual u16 ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) = 0;
  virtual u8 Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) = 0;
  virtual u8 Convert16BitValueFromFormatToFormat(
      u16 *pixel, GxColorType::Color source_value, GxColorType::Color target_value) = 0;
  virtual u8 Convert16BitBufferFromFormatToFormat(GxColorType::Color source_value, GxColorType::Color target_value) = 0;
  virtual GxPackFunction GetRGBTo16ConverterPtr() const = 0;
  virtual GxUnpackFunction Get16ToRGBConverterPtr() const = 0;
  virtual void *GetColorSurfaceBits() = 0;
  virtual u32 GetColorSurfaceStride() = 0;
  virtual void *GetDepthSurfaceBits() = 0;
  virtual u32 GetDepthSurfaceStride() = 0;
  virtual void SetSurfaceRestoreType(GxRestoreType requested) = 0;
  virtual void SetSurfaceRestoreFunction(GxRestoreCallback callback, void *first, void *second) = 0;
  virtual u8 RefreshSurfaceRestoreImage() = 0;
  virtual u32 CompareToBuffer(GxBuffer *other) = 0;
  virtual u32 CompareZToZBuffer(GxBuffer *other) = 0;
};
#endif

// The retail GABOUT descriptor uses these original bit fields and eight fixed extension slots.
struct GxGimexDescriptor {
  i32 signature, length, version;
  u32 can_import : 1, can_export : 1, import_packed : 2, export_packed : 2, import_8 : 1, export_8 : 1;
  u32 import_32 : 1, export_32 : 1, multiframe : 1, multifile : 1, multisize : 1, framebuffer : 1;
  u32 external : 1, uses_file : 1, single_palette : 1, grayscale : 1, start_color : 1, do_subtype : 1;
  u32 resizable : 1, pad : 11;
  i32 max_colors, max_frame_name, quality, mac_type[8];
  char extensions[8][8], author[32], release[8], short_name[8], type[16], description[32];
};

static_assert(sizeof(GxGimexDescriptor) == 220);
static_assert(offsetof(GxGimexDescriptor, mac_type) == 0x1c);
static_assert(offsetof(GxGimexDescriptor, extensions) == 0x3c);

struct GxGimexFile;

// The retail GINSTANCE record keeps its externally opaque stream and char-pointer format state.
struct GxGimexState {
  i32 signature, size, frames, frame_number;
  void *stream;
  char *reference;
};

// IID C3023A9C retains the installed BMP, GIF and JPEG virtual order. The private nominal integer/Boolean
// return spellings remain unconfirmed; these declarations preserve the observed 32-bit return ABI.
class GxGimexCodec : public ScUnknown {
public:
  GxGimexCodec();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual i32 LibraryMajorVersion();
  virtual i32 LibraryMinorVersion();
  virtual GxGimexDescriptor *AboutGimex();
  virtual i32 IsGimex(void *file);
  virtual i32 OpenGimex(GxGimexState **out, void *file, const char *path);
  virtual GxGimexInfo *InfoGimex(GxGimexState *state, i32 index);
  virtual i32 ReadGimex(GxGimexState *state, GxGimexInfo *info, char *out, i32 pitch);
  virtual i32 CloseGimex(GxGimexState *state);
  virtual i32 OpenGimexWriter(GxGimexState **out, void *file, const char *path, i32 field);
  virtual i32 WriteGimex(GxGimexState *state, GxGimexInfo *info, char *source, i32 pitch);
  virtual i32 CloseGimexWriter(GxGimexState *state);
  virtual i32 OpenGimexStream(const char *path, void *file);
  virtual void *OpenGimexFile(const char *path, char *resolved);
  virtual i32 SetGimex(i32 index);
  virtual u32 GetGimex();
  virtual u32 FindGimex(char *extension, i32 *slot);
  virtual i32 GetNumGimex();
  // Disposal retains reached effects while its original nominal return remains unresolved.
  virtual void FreeBitmap(GxGimexPixels *pixels);
  virtual GxGimexPixels *EzLoadFrame(const char *path, i32 index);
  // Retain unavailable BMP/GIF slots to preserve the recovered virtual interface layout.
  virtual GxGimexDescriptor *AboutBmp();
  virtual i32 IsBmp(void *file);
  virtual i32 OpenBmp(GxGimexState **out, void *file, char *path);
  virtual GxGimexInfo *InfoBmp(GxGimexState *state, i32 index);
  virtual i32 ReadBmp(GxGimexState *state, GxGimexInfo *info, char *out, i32 pitch);
  virtual i32 CloseBmp(GxGimexState *state);
  virtual i32 OpenBmpWriter(GxGimexState **out, void *file, char *path, i32 field);
  virtual i32 WriteBmp(GxGimexState *state, GxGimexInfo *info, char *source, i32 pitch);
  virtual i32 CloseBmpWriter(GxGimexState *state);
  virtual GxGimexDescriptor *AboutGif();
  virtual i32 IsGif(void *file);
  virtual i32 OpenGif(GxGimexState **out, void *file, char *path);
  virtual GxGimexInfo *InfoGif(GxGimexState *state, i32 index);
  virtual i32 ReadGif(GxGimexState *state, GxGimexInfo *info, char *out, i32 pitch);
  virtual i32 CloseGif(GxGimexState *state);
  virtual i32 OpenGifWriter(GxGimexState **out, void *file, char *path, i32 field);
  virtual i32 WriteGif(GxGimexState *state, GxGimexInfo *info, char *source, i32 pitch);
  virtual i32 CloseGifWriter(GxGimexState *state);
  virtual GxGimexDescriptor *AboutJpeg();
  virtual i32 IsJpeg(void *file);
  virtual i32 OpenJpeg(GxGimexState **out, void *file, char *path);
  virtual GxGimexInfo *InfoJpeg(GxGimexState *state, i32 index);
  virtual i32 ReadJpeg(GxGimexState *state, GxGimexInfo *info, char *out, i32 pitch);
  virtual i32 CloseJpeg(GxGimexState *state);
  virtual i32 OpenJpegWriter(GxGimexState **out, void *file, char *path, i32 field);
  virtual i32 WriteJpeg(GxGimexState *state, GxGimexInfo *info, char *source, i32 pitch);
  virtual i32 CloseJpegWriter(GxGimexState *state);
  virtual ~GxGimexCodec();

private:
  u32 references;
};

void *GX_GIMEX_CALL GxGimexAlloc(i32 length);
i32 GX_GIMEX_CALL GxGimexFree(void *allocation);

GxGimexDescriptor *GxGimexDescribe();
GxGimexDescriptor *GxGimexDescribeNil();
GxGimexDescriptor *GxGimexDescribeJpeg();
i32 GX_GIMEX_CALL GxGimexSelect(i32 index);
u32 GxGimexSelected();
i32 GxGimexCodecCount();
u32 GX_GIMEX_CALL GxGimexSelectExtension(const char *extension, i32 *slot);
i32 GX_GIMEX_CALL GxGimexSelectForFile(const char *path, GxGimexFile *file);
GxGimexFile *GX_GIMEX_CALL GxGimexOpenFile(const char *path, char *resolved);
i32 GX_GIMEX_CALL GxGimexOpenReader(GxGimexState **out, GxGimexFile *file, const char *path);
i32 GX_GIMEX_CALL GxGimexCanRead(GxGimexFile *file);
GxGimexInfo *GX_GIMEX_CALL GxGimexInfoAt(GxGimexState *state, i32 index);
i32 GX_GIMEX_CALL GxGimexReadPixels(GxGimexState *state, GxGimexInfo *info, u8 *out, i32 pitch);
i32 GX_GIMEX_CALL GxGimexWritePixels(GxGimexState *state, GxGimexInfo *info, u8 *source, i32 pitch);
i32 GX_GIMEX_CALL GxGimexCloseReader(GxGimexState *state);
i32 GX_GIMEX_CALL GxGimexOpenWriter(GxGimexState **out, GxGimexFile *file, const char *path, i32 field);
i32 GX_GIMEX_CALL GxGimexCloseWriter(GxGimexState *state);
GxGimexPixels *GX_GIMEX_CALL GxGimexDecode(const char *path, i32 index);
// Only the reached disposal effects are known; the original nominal return remains unresolved.
void GX_GIMEX_CALL GxGimexDestroyPixels(GxGimexPixels *pixels);

// The original codec stream owns a C file and an 8192-byte buffered read window.
struct GxGimexFile {
  FILE *stream;
  i32 position, length;
  u8 *read_ptr, *read_end;
  u32 write_mode, field_018;
  u8 buffer[8192];
};

GxGimexFile *GX_GIMEX_CALL GxGimexOpenReadFile(const char *path);
GxGimexFile *GX_GIMEX_CALL GxGimexCreateFile(const char *path);
i32 GX_GIMEX_CALL GxGimexCloseFile(GxGimexFile *file);
i32 GX_GIMEX_CALL GxGimexReadBytes(GxGimexFile *file, u8 *out, i32 length);
u32 GX_GIMEX_CALL GxGimexWriteBytes(GxGimexFile *file, const void *source, u32 length);
i32 GX_GIMEX_CALL GxGimexSeek(GxGimexFile *file, i32 position);
i32 GX_GIMEX_CALL GxGimexTell(GxGimexFile *file);

#endif
