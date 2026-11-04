#ifndef SC3_BUILDINGS_ARCHITECT_ASSET_H
#define SC3_BUILDINGS_ARCHITECT_ASSET_H

#include "buildings/architect/bd_geometry.h"
#include "common/sc3.h"
#include "common/sc3_string.h"

// A white render background permits the original asset image to be retained without conversion.
enum BdAssetRenderColor : u32 {
  kBdAssetWhiteRenderBackground = 0xffffff,
};

class GxBuffer;
struct IDirectDrawSurface4;
struct IDirect3DTexture2;

// Retain the original sixteen-bit converted texels independently of a transient SDL renderer.
class BdCpuTexture {
public:
  BdCpuTexture(u32 width, u32 height, bool alpha, bool rgb565, bool color_key);
  ~BdCpuTexture();
  void AddRef();
  void Release();

  u16 *pixels;
  u32 width, height, references;
  bool alpha, rgb565, color_key;
};
using BdAssetTexture = BdCpuTexture;
using BdAssetSurface = BdCpuTexture;

// Image, surface and texture views are held independently, including two references when SDL views share storage.
class BdAsset {
public:
  BdAsset();
  ~BdAsset();
  // Native effect spelling; the private image setter has no verified source return declaration.
  void SetImage(GxBuffer *source);
  void GetTexture(BdAssetTexture **out);
  // Original retained-image and DirectDraw-backed image conversion method.
  GxBuffer *RenderImage(const BdRect *area, u32 color);
  i32 IsKind6Or7Or16() const;
  i32 IsKind8Through16() const;

  GxBuffer *image;
  BdAssetSurface *surface;
  BdAssetTexture *texture;
  i8 kind;
  u32 definition_id, flags;
  f32 height, width, inverse_height, inverse_width;
  BdRect icon_bounds, preview_bounds;
  ScString name, path;
  BdRect opaque_bounds;
  u32 field_080;
};

// Native effect spellings for the three private texture converters; literal source return declarations remain unresolved.
// All original callers discard their incidental final register contents; pitches and dimensions retain separate arguments.
void BdConvert555(u8 *destination, const u8 *source, i32 destination_pitch, i32 source_pitch, u32 width, u32 height);
void BdConvert565(u8 *destination, const u8 *source, i32 destination_pitch, i32 source_pitch, u32 width, u32 height);
void BdConvert4444(u8 *destination, const u8 *source, i32 destination_pitch, i32 source_pitch, u32 width, u32 height);

#endif
