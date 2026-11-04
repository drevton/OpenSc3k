#ifndef SC3_GRAPHICS_IMAGE_H
#define SC3_GRAPHICS_IMAGE_H

#ifdef __cplusplus
#include "common/sc3_unknown.h"
#endif

#include "common/sc3.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/module.h"

// Original internal dispatch uses ECX/EDX on 32-bit PC; other native targets use their normal platform convention.
#if defined(_MSC_VER)
#define GX_COLOR_CALL __fastcall
#elif defined(__i386__)
#define GX_COLOR_CALL __attribute__((fastcall))
#else
#define GX_COLOR_CALL
#endif

using GxBlendFunction = u32(GX_COLOR_CALL *)(u32 source, u32 destination, i32 amount);
using GxGammaFunction = u32(GX_COLOR_CALL *)(u32 pixel, i32 amount);
extern GxBlendFunction GxBlendColor;
extern GxGammaFunction GxGammaColor;
bool GxSelectColorFunctions(const i32 *format);
u32 GX_COLOR_CALL GxBlendColor555(u32 source, u32 destination, i32 amount);
u32 GX_COLOR_CALL GxBlendColor565(u32 source, u32 destination, i32 amount);
u32 GX_COLOR_CALL GxGammaColor555(u32 pixel, i32 amount);
u32 GX_COLOR_CALL GxGammaColor565(u32 pixel, i32 amount);

// Image instances retain the original all-ones transparency key before initialization replaces it.
enum GxImageInitialKey : u32 {
  kGxImageInitialTransparencyKey = 0xffffffff,
};

// Original A41381FD identity selects the module's actual color-operation functions through slot12.
class GxColorFunctions : public ScUnknown {
public:
  virtual bool Init(const i32 *format) = 0;
};

class GxColorService : public GxSystemServiceBase, public GxColorFunctions {
public:
  GxColorService();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(const i32 *format) override;
};

// Native linked storage replaces the original std::list without retaining comparison identities, pins or erased nodes.
class GxFrameList {
  struct Node {
    Node *next, *previous;
    GxFrameListener *listener;
  };

  Node *head;
  Node *Find(GxFrameListener *listener);
  void Erase(Node *node);

public:
  GxFrameList();
  ~GxFrameList();
  bool Add(GxFrameListener *listener);
  bool Remove(GxFrameListener *listener);
  void GSInit(GxGraphics *graphics, GxBuffer *buffer);
  void ForceFullSystemRedraw();
  void Draw();
  void ProcessWindowMove();
  void CompleteSwitchToFullScreenMode();
  void CompleteSwitchToWindowedMode();
  void ReleaseAll();
  void ClearRaw();
};

// Original cIGZSpanBuffer identity0487534F, with all37 own entries in installed order12..156.
// Point targets are eight-byte const inputs; the four mutable Windows stretch targets remain explicit outputs.
class GxImageBuilder : public ScUnknown {
public:
  virtual bool ImportFromData(u8 *allocation, bool take) = 0;
  virtual bool ExportToData(u8 **allocation, u32 *size) = 0;
  virtual bool ConvertStandardBufferToData(GxBuffer *source) = 0;
  virtual bool ConvertStandardBufferToData(GxBuffer *source, u8 **allocation, u32 *size) = 0;
  virtual bool ValidateStandardBufferToDataConversion(GxBuffer *buffer) = 0;
  virtual bool ValidateStandardBufferToDataConversion(GxBuffer *buffer, u8 *allocation) = 0;
  virtual u8 ConvertDataToStandardBuffer(GxBuffer **out, u8 *allocation) = 0;
  virtual u8 ConvertDataToStandardBuffer(GxBuffer *destination, u8 *allocation) = 0;
  virtual u8 GetMemoryUsage(u32 *first, u32 *second) = 0;
  virtual u8 SetAntialiasingOfBoundingPixels(bool value) = 0;
  virtual GxBuffer *AsIGZBuffer() = 0;
  virtual u8 SpanBufferAlphaBlt(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
      const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferAlphaBltStretch(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
      const ScRect &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, ScRect *dr) = 0;
  virtual u8 SpanBufferAlphaBltWithSeparateAlphaRect(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
      const ScRect &mask_area_input, const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltWithSeparateAlphaRectNoClip(GxBuffer *destination, GxBuffer *mask,
      const ScRect &sr_input, const ScRect &mask_area_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferAlphaBltWithSeparateAlphaRectStretch(GxBuffer *destination, GxBuffer *mask,
      const ScRect &source_input, const ScRect &mask_area_input, const ScRect &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltWithSeparateAlphaRectStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, const ScRect &mask_area_input, ScRect *dr) = 0;
  virtual u8 SpanBufferAlphaBltConstAlpha(GxBuffer *destination, f32 opacity, const ScRect &source_input,
      const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltConstAlphaNoClip(
      GxBuffer *destination, f32 opacity, const ScRect &sr_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferAlphaBltConstAlphaStretch(GxBuffer *destination, f32 opacity, const ScRect &source_input,
      const ScRect &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltConstAlphaStretchNoClip(
      GxBuffer *destination, f32 opacity, const ScRect &sr_input, const ScRect &dr_input) = 0;
  virtual u8 SpanBufferAlphaBltWithAdditionalAlphaFactor(GxBuffer *destination, GxBuffer *mask, f32 opacity,
      const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltWithAdditionalAlphaFactorNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &sr_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferAlphaBltWithAdditionalAlphaFactorStretch(GxBuffer *destination, GxBuffer *mask, f32 opacity,
      const ScRect &source_input, const ScRect &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltWithAdditionalAlphaFactorStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &sr_input, ScRect *dr) = 0;
  virtual u8 SpanBufferAlphaBltSelfAlpha(
      GxBuffer *destination, const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferAlphaBltSelfAlphaNoClip(
      GxBuffer *destination, const ScRect &sr_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferHighlightBlt(
      GxBuffer *destination, u32 tint, const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferHighlightBltNoClip(
      GxBuffer *destination, u32 tint, const ScRect &sr_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferHighlightBltStretch(
      GxBuffer *destination, u32 tint, const ScRect &source_input, const ScRect &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferHighlightBltStretchNoClip(
      GxBuffer *destination, u32 tint, const ScRect &sr_input, const ScRect &dr_input) = 0;
  virtual u8 SpanBufferBltWith2Alphas(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &source_input, const ScRect &second_area_input, const ScPoint &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferBltWith2AlphasNoClip(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &sr_input, const ScRect &second_area_input, const ScPoint &dr_input) = 0;
  virtual u8 SpanBufferBltWith2AlphasStretch(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &source_input, const ScRect &second_area_input, const ScRect &target_input, const ScRect *clip) = 0;
  virtual u8 SpanBufferBltWith2AlphasStretchNoClip(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &sr_input, const ScRect &second_area_input, ScRect *dr) = 0;
};

// Actual graphics-owned cropped-row image, implementing the real buffer and builder identities.
class GxImage : public GxBuffer, public GxImageBuilder {
public:
  GxImage();
  virtual ~GxImage();
  bool Init(u32 w, u32 h, GxColorType requested, GxPaletteView *colors, GxDepthType secondary,
      GxVramRequest options_value, bool flag) override;
  bool Shutdown() override;
  u8 Restore() override;
  bool Uninitialize() override;
  u8 Lock(GxLockFlags requested_flags) override;
  u8 Unlock(GxLockFlags requested_flags) override;
  bool IsLocked() const override;
  u8 LockDepthSurface(GxLockFlags requested_flags) override;
  u8 UnlockDepthSurface(GxLockFlags requested_flags) override;
  bool IsDepthSurfaceLocked() const override;
  bool IsReady() override;
  u32 Width() const override;
  u32 Height() const override;
  const ScRect *GetBufferArea() const override;
  void GetBufferArea(ScRect *out) const override;
  u32 GetMemoryUsage() const override;
  GxColorType GetColorType() const override;
  void GetColorType(GxColorType *out) const override;
  GxPaletteView *GetPalette() const override;
  GxPaletteView *ReplacePalette(GxPaletteView *colors) override;
  u32 GetBitsPerPixel() const override;
  u32 GetBytesPerPixel() const override;
  bool IsInVideoMemory() const override;
  u8 IsTexture() const override;
  GxDepthType GetZDepth() const override;
  bool IsZBuffered() const override;
  u8 Fill(u32 pixel, const ScRect &box, ScRect *clip) override;
  u8 Fill(u32 pixel, i32 left, i32 top, i32 right, i32 bottom, ScRect *clip) override;
  u8 Fill(u32 pixel, ScRect *clip) override;
  u8 FillNoClip(u32 pixel, const ScRect &rectangle) override;
  u8 FillNoClip(u32 pixel, i32 left, i32 top, i32 right, i32 bottom) override;
  u8 FillNoClip(u32 pixel) override;
  u8 ZFill(u32 pixel, const ScRect &rectangle) override;
  u8 ZFill(u32 pixel) override;
  u8 DrawRectangleOutline(const ScRect &box_input, u32 pixel, u32 thickness) override;
  u8 DrawRectangleOutlineNoClip(const ScRect &box_input, u32 pixel, u32 thickness) override;
  u8 FloodFill(i32 x, i32 y, u32 pixel) override;
  u8 FloodFillNoClip(i32 x, i32 y, u32 pixel) override;
  u8 BoundaryFill(i32 x, i32 y, u32 pixel, u32 border) override;
  u8 BoundaryFillNoClip(i32 x, i32 y, u32 pixel, u32 border) override;
  u8 GenericBufferClip(GxBuffer *receiver, ScRect *source, ScRect *destination, const ScRect *clip) override;
  u8 GenericBufferClipStretch(GxBuffer *receiver, ScRect *source, ScRect *destination, const ScRect *clip,
      f32 *horizontal_scale, f32 *vertical_scale) override;
  u32 GetPixel(u32 x, u32 y) const override;
  void SetPixel(u32 x, u32 y, u32 pixel) override;
  void SetPixelAlpha(u32 x, u32 y, u32 pixel, f32 opacity) override;
  u32 GetZValue(u32 x, u32 y) const override;
  void SetZValue(u32 x, u32 y, u32 pixel) override;
  void SetTransparency(u32 pixel) override;
  u8 GetTransparency() const override;
  u8 IsPixelTransparent(u32 x, u32 y) override;
  u8 GetTransparentColor(u32 *out) const override;
  void RemoveTransparency() override;
  void EnableZBufferBlit(GxBltFlags::Options flags_value) override;
  void DisableZBufferBlit() override;
  void SetBltFlags(const GxBltFlags &value_input) override;
  void GetBltFlags(GxBltFlags *out) const override;
  const GxBltFlags *GetBltFlags() const override;
  u8 DrawLine(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness) override;
  u8 DrawLineNoClip(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness) override;
  u8 DrawLineAlpha(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness_value, f32 opacity) override;
  u8 DrawLineNoClipAlpha(i32 x1, i32 y1, i32 x2, i32 y2, i32 pixel_value, u32 thickness_value, f32 opacity) override;
  u8 DrawCircle(i32 left, i32 top, i32 right, i32, u32 pixel, u32 thickness) override;
  u8 DrawCircleNoClip(i32 left, i32 top, i32 right, i32, u32 pixel, u32 thickness) override;
  u8 FillCircle(i32 left, i32 top, i32 right, i32, u32 pixel) override;
  u8 FillCircleNoClip(i32 left, i32 top, i32 right, i32, u32 pixel) override;
  u8 Duplicate(GxBuffer **out, GxVramRequest requested_mode_value) override;
  u8 Blt(
      GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const ScRect *clip) override;
  u8 Blt(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) override;
  u8 BltNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input) override;
  u8 BltNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) override;
  u8 BltStretch(
      GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, const ScRect *clip) override;
  u8 BltStretch(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) override;
  u8 BltStretchNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input) override;
  u8 BltStretchNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input,
      const GxBltFlags &value_input) override;
  u8 ZBlt(GxBuffer *source, u32) override;
  u8 BltSrc(GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input,
      const ScRect *clip) override;
  u8 BltNoClipSrc(GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input) override;
  u8 BltStretchSrc(GxBuffer *destination, const ScRect &source_box_input, const ScRect &destination_box_input,
      const ScRect *clip) override;
  u8 BltStretchNoClipSrc(GxBuffer *destination, const ScRect &source_box_input, ScRect *destination_box) override;
  void CopyBuffer(GxBuffer *source) override;
  void CopyZBuffer(GxBuffer *source) override;
  u8 BltTiled(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, i32 x_offset,
      i32 y_offset, const ScRect *clip) override;
  u8 BltTiledNoClip(GxBuffer *source, const ScRect &source_box_input, const ScRect &destination_input, i32 x_offset,
      i32 y_offset) override;
  u8 BltEdge(GxBuffer *source, const ScRect &slice_input, const ScRect &destination_input, u8 flags_value) override;
  u8 RotateBltSimple(
      GxBuffer *source, const ScRect &source_rect, const ScPoint &destination, u32 angle, const ScRect *) override;
  u8 RotateBlt(GxBuffer *source, const ScRect &source_rect_input, const ScRect &destination_input, f32 angle,
      const ScRect *clip) override;
  u8 RotateBltStretch(GxBuffer *source, const ScRect &source_rect_input, const ScRect &destination_input, f32 angle,
      f32 scale, const ScRect *clip) override;
  u8 AlphaBlt(GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity,
      const ScRect *clip) override;
  u8 AlphaBltNoClip(
      GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity) override;
  u8 AlphaSourcePrecalculate(const ScRect &rectangle, f32 opacity) override;
  u8 AlphaBltSourcePrecalculated(GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input,
      f32 opacity, const ScRect *clip) override;
  u8 AlphaBltNoClipSourcePrecalculated(
      GxBuffer *source, const ScRect &source_rect_input, const ScPoint &destination_input, f32 opacity) override;
  u8 AlphaFill(u32 pixel, const ScRect &rectangle_input, f32 opacity, const ScRect *clip) override;
  u8 AlphaFillNoClip(u32 pixel, const ScRect &rectangle_input, f32 opacity) override;
  u8 DrawRaisedRect(const ScRect &box_input, u8 red, u8 green, u8 blue, bool raised, bool filled) override;
  u8 DrawRaisedRect(const ScRect &box_input, u32 pixel, bool raised, bool filled) override;
  u8 ConvertRGBValueToNative16Bit(u16 *out, u8 red, u8 green, u8 blue) override;
  u16 ConvertRGBValueToNative16Bit(u8 red, u8 green, u8 blue) override;
  u8 Convert16BitNativeValueToRGB(u16 pixel, u8 *red, u8 *green, u8 *blue) override;
  u8 Convert16BitValueFromFormatToFormat(
      u16 *pixel, GxColorType::Color source_value, GxColorType::Color target_value) override;
  u8 Convert16BitBufferFromFormatToFormat(GxColorType::Color source_value, GxColorType::Color target_value) override;
  GxPackFunction GetRGBTo16ConverterPtr() const override;
  GxUnpackFunction Get16ToRGBConverterPtr() const override;
  void *GetColorSurfaceBits() override;
  u32 GetColorSurfaceStride() override;
  void *GetDepthSurfaceBits() override;
  u32 GetDepthSurfaceStride() override;
  void SetSurfaceRestoreType(GxRestoreType requested) override;
  void SetSurfaceRestoreFunction(GxRestoreCallback callback, void *first, void *second) override;
  u8 RefreshSurfaceRestoreImage() override;
  u32 CompareToBuffer(GxBuffer *other) override;
  u32 CompareZToZBuffer(GxBuffer *other) override;
  bool ImportFromData(u8 *allocation, bool take) override;
  bool ExportToData(u8 **allocation, u32 *size) override;
  bool ConvertStandardBufferToData(GxBuffer *source) override;
  bool ConvertStandardBufferToData(GxBuffer *source, u8 **allocation, u32 *size) override;
  bool ValidateStandardBufferToDataConversion(GxBuffer *buffer) override;
  bool ValidateStandardBufferToDataConversion(GxBuffer *buffer, u8 *allocation) override;
  u8 ConvertDataToStandardBuffer(GxBuffer **out, u8 *allocation) override;
  u8 ConvertDataToStandardBuffer(GxBuffer *destination, u8 *allocation) override;
  u8 GetMemoryUsage(u32 *first, u32 *second) override;
  u8 SetAntialiasingOfBoundingPixels(bool value) override;
  GxBuffer *AsIGZBuffer() override;
  u8 SpanBufferAlphaBlt(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input, const ScPoint &target_input,
      const ScRect *clip) override;
  u8 SpanBufferAlphaBltNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, const ScPoint &dr_input) override;
  u8 SpanBufferAlphaBltStretch(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
      const ScRect &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input, ScRect *dr) override;
  u8 SpanBufferAlphaBltWithSeparateAlphaRect(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
      const ScRect &mask_area_input, const ScPoint &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltWithSeparateAlphaRectNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input,
      const ScRect &mask_area_input, const ScPoint &dr_input) override;
  u8 SpanBufferAlphaBltWithSeparateAlphaRectStretch(GxBuffer *destination, GxBuffer *mask, const ScRect &source_input,
      const ScRect &mask_area_input, const ScRect &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltWithSeparateAlphaRectStretchNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &sr_input,
      const ScRect &mask_area_input, ScRect *dr) override;
  u8 SpanBufferAlphaBltConstAlpha(GxBuffer *destination, f32 opacity, const ScRect &source_input,
      const ScPoint &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltConstAlphaNoClip(
      GxBuffer *destination, f32 opacity, const ScRect &sr_input, const ScPoint &dr_input) override;
  u8 SpanBufferAlphaBltConstAlphaStretch(GxBuffer *destination, f32 opacity, const ScRect &source_input,
      const ScRect &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltConstAlphaStretchNoClip(
      GxBuffer *destination, f32 opacity, const ScRect &sr_input, const ScRect &dr_input) override;
  u8 SpanBufferAlphaBltWithAdditionalAlphaFactor(GxBuffer *destination, GxBuffer *mask, f32 opacity,
      const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltWithAdditionalAlphaFactorNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &sr_input, const ScPoint &dr_input) override;
  u8 SpanBufferAlphaBltWithAdditionalAlphaFactorStretch(GxBuffer *destination, GxBuffer *mask, f32 opacity,
      const ScRect &source_input, const ScRect &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltWithAdditionalAlphaFactorStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &sr_input, ScRect *dr) override;
  u8 SpanBufferAlphaBltSelfAlpha(
      GxBuffer *destination, const ScRect &source_input, const ScPoint &target_input, const ScRect *clip) override;
  u8 SpanBufferAlphaBltSelfAlphaNoClip(GxBuffer *destination, const ScRect &sr_input, const ScPoint &dr_input) override;
  u8 SpanBufferHighlightBlt(GxBuffer *destination, u32 tint, const ScRect &source_input, const ScPoint &target_input,
      const ScRect *clip) override;
  u8 SpanBufferHighlightBltNoClip(
      GxBuffer *destination, u32 tint, const ScRect &sr_input, const ScPoint &dr_input) override;
  u8 SpanBufferHighlightBltStretch(GxBuffer *destination, u32 tint, const ScRect &source_input,
      const ScRect &target_input, const ScRect *clip) override;
  u8 SpanBufferHighlightBltStretchNoClip(
      GxBuffer *destination, u32 tint, const ScRect &sr_input, const ScRect &dr_input) override;
  u8 SpanBufferBltWith2Alphas(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &source_input, const ScRect &second_area_input, const ScPoint &target_input,
      const ScRect *clip) override;
  u8 SpanBufferBltWith2AlphasNoClip(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &sr_input, const ScRect &second_area_input, const ScPoint &dr_input) override;
  u8 SpanBufferBltWith2AlphasStretch(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &source_input, const ScRect &second_area_input, const ScRect &target_input,
      const ScRect *clip) override;
  u8 SpanBufferBltWith2AlphasStretchNoClip(GxBuffer *destination, GxBuffer *first_mask, GxBuffer *second_mask,
      const ScRect &sr_input, const ScRect &second_area_input, ScRect *dr) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // One-argument copy entries expose reached effects only; their private return declarations remain unavailable.

  // The Linux span-buffer class names this private five-argument entry; the Windows nominal byte return remains provisional.
  virtual u8 BltBlur(GxBuffer *destination, const ScRect &source, const ScPoint &point, f32 amount, const ScRect *clip);
  virtual u8 BltBlurNoClip(GxBuffer *destination, const ScRect &source, const ScPoint &point, f32 amount);

protected:
  u8 *data = nullptr;
  u8 clipped = 0;
  ScRect area = {};
  u32 key = kGxImageInitialTransparencyKey;
  GxBltFlags color = {0, 0, 2};
  bool counting_enabled = false;
  u32 references = 0;
};

#ifdef __cplusplus

// Actual cIGZPalette interface, verified by retail CodeView15A7 and the installed palette vtables.
class GxPaletteView : public ScUnknown {
public:
  virtual bool Init() = 0;
  virtual bool Shutdown() = 0;
  virtual i32 GetNumberEntries() = 0;
  virtual i32 GetBitsPerPixel() = 0;
  virtual bool GetColor(i32 index, u32 *out) = 0;
  virtual bool SetColor(i32 index, const u32 &pixel) = 0;
  virtual i32 GetNearestIndex(const u32 &pixel) = 0;
  virtual bool MakeGenericPalette(i32 count, i32 bits) = 0;
  virtual void *GetPaletteArray() = 0;
  virtual bool IsSystemPalette() = 0;
  virtual bool IsReserved(i32 index) = 0;
};

// Concrete cRZPalette storage publicly implements that interface. Its allocation belongs to this object.
class GxPalette : public GxPaletteView {
public:
  u32 references;
  i32 count, bits;
  u8 *pixels;
  GxPalette();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  i32 GetNumberEntries() override;
  i32 GetBitsPerPixel() override;
  bool GetColor(i32 index, u32 *out) override;
  bool SetColor(i32 index, const u32 &pixel) override;
  i32 GetNearestIndex(const u32 &pixel) override;
  bool MakeGenericPalette(i32 count, i32 bits) override;
  void *GetPaletteArray() override;
  bool IsSystemPalette() override;
  bool IsReserved(i32 index) override;
  virtual ~GxPalette();
};
#endif

#endif
