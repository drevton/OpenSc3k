#ifndef SC3_GRAPHICS_SC3_GENERAL_BUFFER_H
#define SC3_GRAPHICS_SC3_GENERAL_BUFFER_H

#include "graphics/image_codecs.h"

i32 GxCountDifferences(GxBuffer *first, GxBuffer *second);
u32 GxAdjustRgb(GxBuffer *buffer, u8 red, u8 green, u8 blue, i32 delta);
i32 GxCountSecondaryDifferences(GxBuffer *first, GxBuffer *second);
// Native raster phase of1000E746; original entry dispatch, validation and callback ordering remain separate.
void GxStrokeCirclePixels(u8 *pixels, u32 stride, i32 cx, i32 cy, i32 radius, u32 thickness, u32 pixel);

// The secondary identity shares the buffer count and exposes its borrowed color and depth drivers at slots12 and16.
// Linux names are GetColorSurface/GetDepthSurface; GetBackend retains current cross-module spelling.
// Exact Windows pointer return types remain provisional.
class GxBufferSecondary : public ScUnknown {
public:
  virtual GxDrawSurface *GetBackend() = 0;
  virtual GxDrawSurface *GetDepthSurface() = 0;
};

class GxGeneralBuffer : public GxBuffer, public GxBufferSecondary {
public:
  GxGeneralBuffer();
  virtual ~GxGeneralBuffer();
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
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  GxDrawSurface *GetBackend() override;
  GxDrawSurface *GetDepthSurface() override;
  virtual u8 PaletteChangedNotification();
  // Linux cGZBuffer names and parameter types match the Windows conversion flow; PC nominal byte return remains unresolved.
  virtual u8 PaletteBlitNoClip(GxBuffer *source, const ScRect &source_rect, const ScRect &destination, void *palette);
  virtual u8 PaletteBlitStretchNoClip(
      GxBuffer *source, const ScRect &source_rect, const ScRect &destination, void *palette);

protected:
  virtual bool Setup();
  virtual bool InitBackends();
  virtual u8 InitSecondary();
  virtual bool CloseBackends();
  u8 CopySnapshot(GxBuffer *snapshot, u8 capture);
  bool initialized = false;
  i32 format[2] = {4, 16};
  GxPaletteView *palette = nullptr;
  i32 secondary_format = 0;
  ScRect area = {0, 0, 0, 0};
  GxBltFlags color = {0, 0, 0};
  u32 locks = 0, mode = 1;
  u8 mode_flag;
  GxDrawDeviceSurface *backend = nullptr;
  GxDrawDepthSurface *secondary_backend = nullptr;
  u32 lock_flags = 0, secondary_lock_flags = 0;
  bool counting_enabled = false;
  u32 references = 0;
  GxRestoreType restore_type = GxRestoreType::None;

  GxRestoreCallback restore_callback = nullptr;
  void *cache_data = nullptr, *restore_argument = nullptr;
};

#endif
