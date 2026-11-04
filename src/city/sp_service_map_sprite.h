#ifndef SC3_CITY_SP_SERVICE_MAP_SPRITE_H
#define SC3_CITY_SP_SERVICE_MAP_SPRITE_H

#include "common/rect.h"
#include "graphics/api.h"

class GxBuffer;
class GxPaletteView;
class CtAuraLayer;
class CtZoneLayer;
class ScStringView;

// Original cSC3MapSprite copy linked into SIMSERV; the actual raster fields and virtual calls remain direct.
// The shared IID5DD prefix follows the installed slot order; private nominal declarations remain qualified.
namespace CtService {
class SpMapSprite : public GxRasterSource {
public:
  SpMapSprite();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 id, u32 width, u32 height) override;
  u32 GetId() const override;
  bool SetId(u32 id) override;
  bool SetPalette(GxPaletteView *source) override;
  GxPaletteView *GetPalette() override;
  i32 GetBaseWidth() const override;
  i32 GetBaseHeight() const override;
  bool SetBaseSize(u32 width, u32 height) override;
  // The Windows byte carrier retains raw inputs and results; Linux names the input bool, and PC spelling is unresolved.
  u8 SetPostFilter(u8 enabled) override;
  // Native effects of private10034564; the discarded nominal result declaration is undocumented.
  void SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) override;
  u8 IsPostFilter() const override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;
  // Folded debug entries return byte zero; original nominal Boolean/BYTE declarations remain unresolved.
  u8 DebugInstanceTag(ScStringView *out) override;
  u8 DebugClassTag(ScStringView *out) override;
  u8 DebugTypeTag(ScStringView *out) override;
  virtual bool GetColor(u32 x, u32 z, u32 *out);
  virtual bool DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual bool DrawAnnotations(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual u8 SetDebugInstanceTag(const ScStringView &value);
  virtual u8 SetDebugClassTag(const ScStringView &value);
  virtual ~SpMapSprite();

protected:
  // Original annotation primitives; private nominal return types remain unresolved.
  void TransformVector(i32 *x, i32 *z);
  void TransformDistance(i32 *distance);
  void PostClip(i32 *x0, i32 *z0, i32 *x1, i32 *z1);
  void ClipLeft(i32 *x0, i32 *z0, i32 *x1, i32 *z1, i32 ax, i32 az, i32 bx, i32 bz);
  // The original point predicate returns zero or one; its private nominal bool/BYTE spelling remains unresolved.
  u8 IsInside(i32 x, i32 z);
  u8 DrawLine(i32 x0, i32 z0, i32 x1, i32 z1, u32 mode, GxBuffer *destination, const ScRect &rectangle,
      u32 color, ScRect *clip);
  u8 DrawCircle(
      i32 x, i32 z, u8 radius, u32 mode, GxBuffer *destination, const ScRect &rectangle, u32 color, ScRect *clip);
  u8 DrawDot(i32 x, i32 z, u8 radius, GxBuffer *destination, const ScRect &rectangle, u32 color, ScRect *clip);
  u32 references, kind;
  GxPaletteView *palette;
  i32 width, height;
  u8 enabled;
  // Actual four corner coordinates at original+28..56; constructor's three defining points produce the unit square.
  i32 corner_x[4], corner_z[4];
};

} // namespace CtService

#endif
