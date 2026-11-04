#ifndef SC3_GRAPHICS_SC3_EFFECTS_H
#define SC3_GRAPHICS_SC3_EFFECTS_H

#include "graphics/image_codecs.h"
#include "graphics/module.h"

// Actual module-wide color tables shared by graphics instances and sprite blend paths.
extern u8 GxShade[1024], GxGamma[1024], GxBlend[32768];
extern u16 GxTwice[65536], GxHalf[65536];
u8 GxInitAlphaSystem(const GxColorType &format);
u32 GxBlend555(u32 source, u32 destination, u32 alpha);
u32 GxBlend565(u32 source, u32 destination, u32 alpha);
bool GxGenericBufferClip(GxBuffer *target, ScRect *source, ScRect *destination, const ScRect *clip);
u8 GxAlphaBltNoClip(
    GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target);
u8 GxAlphaBlt(GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
    const ScRect *clip);
u8 GxAlphaBltConstAlphaNoClip(
    GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect, const ScPoint &destination_rect);
u8 GxAlphaBltConstAlpha(GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect,
    const ScPoint &destination_rect, const ScRect *clip);
GxBuffer *GxAlphaSurfaceFromFile(const char *filename, u32 levels);
GxBuffer *GxMaskSurfaceFromFile(const char *filename);
u8 GxAlphaBltConstSourceNoClip(
    u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target);
u8 GxAlphaBltConstSource(
    u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target, const ScRect *clip);
u8 GxShadeBltConstAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target, u16 *table);
u8 GxShadeBltConstAlpha(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target, u16 *table, const ScRect *clip);
u32 GxGamma555(u32 pixel, i32 amount);
u32 GxGamma565(u32 pixel, i32 amount);
u8 GxShadeBltNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target);
u8 GxShadeBlt(GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScPoint &target, const ScRect *clip);
u8 GxShadeBltStretchNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScRect &target);
u8 GxShadeBltStretch(
    GxBuffer *destination, GxBuffer *mask, const ScRect &source, const ScRect &target, const ScRect *clip);
u8 GxShadeBltWithAdditionalAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source, const ScPoint &target);
u8 GxShadeBltWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source,
    const ScPoint &target, const ScRect *clip);
u8 GxShadeBltStretchWithAdditionalAlphaNoClip(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source, ScRect *target);
u8 GxShadeBltStretchWithAdditionalAlpha(
    GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source, const ScRect &target, const ScRect *clip);
u8 GxAlphaBltSelfAlphaNoClip(GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target);
u8 GxAlphaBltSelfAlpha(
    GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target, const ScRect *clip);
// Linux source symbols establish the separate unsigned color and point target; Windows forwards but does not read color.
u8 GxAlphaBltHalfAlphaVariableSource1ConstSource2(GxBuffer *source, GxBuffer *destination, u32 color,
    const ScRect &source_rect, const ScPoint &target, const ScRect *clip);
u8 GxAlphaBltHalfAlphaVariableSource1ConstSource2NoClip(
    GxBuffer *source, GxBuffer *destination, u32 color, const ScRect &source_rect, const ScPoint &target);

// Original effects-service identity, IID E3260321. The two tail methods retain their Linux source argument
// types and installed Windows behavior; earlier private argument names/types remain under review.
class GxBlendService : public ScUnknown {
public:
  // Original slots12 through32; the two bitmap entries return newly created buffer identities directly.
  virtual bool GenericBufferClip(GxBuffer *target, ScRect *source, ScRect *destination, const ScRect *clip) = 0;
  virtual u8 InitAlphaSystem(const GxColorType &format) = 0;
  virtual u32 GetAlphaLevelCount() const = 0;
  virtual GxBuffer *AlphaSurfaceFromFile(const char *filename, u32 levels) = 0;
  virtual GxBuffer *MaskSurfaceFromFile(const char *filename) = 0;
  virtual u8 AlphaBlt(GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 AlphaBltNoClip(
      GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 AlphaBltConstAlpha(GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 AlphaBltConstAlphaNoClip(
      GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 AlphaBltConstSource(u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 AlphaBltConstSourceNoClip(
      u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 ShadeBltConstAlpha(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
      u16 *table, const ScRect *clip) = 0;
  virtual u8 ShadeBltConstAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, u16 *table) = 0;
  virtual u8 ShadowBlitConstAlpha(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 ShadowBlitConstAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 BrightBlitConstAlpha(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 BrightBlitConstAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 ShadeBlt(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 ShadeBltNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 ShadeBltStretch(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScRect &target, const ScRect *clip) = 0;
  virtual u8 ShadeBltStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScRect &target) = 0;
  virtual u8 ShadeBltWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 ShadeBltWithAdditionalAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 ShadeBltStretchWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity,
      const ScRect &source_rect, const ScRect &target, const ScRect *clip) = 0;
  virtual u8 ShadeBltStretchWithAdditionalAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect, ScRect *target) = 0;
  virtual u8 AlphaBltSelfAlpha(GxBuffer *source, GxBuffer *destination, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 AlphaBltSelfAlphaNoClip(
      GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target) = 0;
  virtual u8 AlphaBltHalfAlphaVariableSource1ConstSource2(GxBuffer *source, GxBuffer *destination, u32 color,
      const ScRect &source_rect, const ScPoint &target, const ScRect *clip) = 0;
  virtual u8 AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(
      GxBuffer *source, GxBuffer *destination, u32 color, const ScRect &source_rect, const ScPoint &target) = 0;
};

// Actual service has no fields beyond its system-service base and effects identity.
// All identities share the recovered system-service/module lifetime.
class GxEffectsService : public GxSystemServiceBase, public GxBlendService {
public:
  GxEffectsService();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GenericBufferClip(GxBuffer *target, ScRect *source, ScRect *destination, const ScRect *clip) override;
  u8 InitAlphaSystem(const GxColorType &format) override;
  u32 GetAlphaLevelCount() const override;
  GxBuffer *AlphaSurfaceFromFile(const char *filename, u32 levels) override;
  GxBuffer *MaskSurfaceFromFile(const char *filename) override;
  u8 AlphaBlt(GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
      const ScRect *clip) override;
  u8 AlphaBltNoClip(GxBuffer *source, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
      const ScPoint &target) override;
  u8 AlphaBltConstAlpha(GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) override;
  u8 AlphaBltConstAlphaNoClip(
      GxBuffer *source, GxBuffer *destination, f32 opacity, const ScRect &source_rect, const ScPoint &target) override;
  u8 AlphaBltConstSource(u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) override;
  u8 AlphaBltConstSourceNoClip(
      u32 pixel, GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) override;
  u8 ShadeBltConstAlpha(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
      u16 *table, const ScRect *clip) override;
  u8 ShadeBltConstAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target, u16 *table) override;
  u8 ShadowBlitConstAlpha(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
      const ScRect *clip) override;
  u8 ShadowBlitConstAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) override;
  u8 BrightBlitConstAlpha(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
      const ScRect *clip) override;
  u8 BrightBlitConstAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) override;
  u8 ShadeBlt(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target,
      const ScRect *clip) override;
  u8 ShadeBltNoClip(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScPoint &target) override;
  u8 ShadeBltStretch(GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScRect &target,
      const ScRect *clip) override;
  u8 ShadeBltStretchNoClip(
      GxBuffer *destination, GxBuffer *mask, const ScRect &source_rect, const ScRect &target) override;
  u8 ShadeBltWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect,
      const ScPoint &target, const ScRect *clip) override;
  u8 ShadeBltWithAdditionalAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect, const ScPoint &target) override;
  u8 ShadeBltStretchWithAdditionalAlpha(GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect,
      const ScRect &target, const ScRect *clip) override;
  u8 ShadeBltStretchWithAdditionalAlphaNoClip(
      GxBuffer *destination, GxBuffer *mask, f32 opacity, const ScRect &source_rect, ScRect *target) override;
  u8 AlphaBltSelfAlpha(GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target,
      const ScRect *clip) override;
  u8 AlphaBltSelfAlphaNoClip(
      GxBuffer *source, GxBuffer *destination, const ScRect &source_rect, const ScPoint &target) override;
  u8 AlphaBltHalfAlphaVariableSource1ConstSource2(GxBuffer *source, GxBuffer *destination, u32 color,
      const ScRect &source_rect, const ScPoint &target, const ScRect *clip) override;
  u8 AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(
      GxBuffer *source, GxBuffer *destination, u32 color, const ScRect &source_rect, const ScPoint &target) override;
};

#endif
