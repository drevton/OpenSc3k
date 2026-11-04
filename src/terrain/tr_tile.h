#ifndef SC3_TERRAIN_TR_TILE_H
#define SC3_TERRAIN_TR_TILE_H

#include "graphics/image_codecs.h"
#include "sprites/api.h"

struct TrRandom;

// Original cSC3LCVertex stores three RGB bytes; its four-element static array occupies12 bytes.
struct TrColorVertex {
  u8 rgb[3];
};

static_assert(sizeof(TrColorVertex) == 3);

struct TrTilePoint {
  i16 x, y;
};

// Remaining shared strip and wall scratch is serialized by the terrain module lock.
extern u16 TrTileStrip;
extern i16 TrEdgeGroundHeights[2];
extern TrTilePoint TrWallWaterVertices[4], TrWallGroundVertices[4];
extern i32 TrWallRows[4], TrWallZoom;
extern u16 TrWallPalettes[2][512];
u16 TrClassifyEdge(const TrTilePoint *points);
extern i32 TrTileZoom;
extern const u8 TrTileTopology[55][19];
extern GxPackFunction TrPackColor;
extern GxUnpackFunction TrUnpackColor;
i16 TrClassifyTile(TrTilePoint *points, u8 mixed_water);
// Native effects of private converter/overlay helpers; unused nominal return declarations remain unresolved.
bool TrRefreshPixelConverters(GxBuffer *device);
u16 TrAttenuateAndPackColor(GxBuffer *device, i32 zoom, u8 *rgb);
void TrLoadOverlayColor(GxBuffer *device, i32 zoom, u8 index);
void TrMixOverlayColor(u8 *rgb);

// Shared IID7100 terrain tile base with real identity, ownership and recovered sprite operations.
class TrTile : public SpSprite {
public:
  using SpSprite::Draw;
  // Original static screenPoint and vertex arrays hold four projected points and four packed RGB vertices.
  static TrTilePoint screenPoint[4];
  static TrColorVertex vertex[4];
  // Original independent dcRandom stream and the two1024-byte bump maps belong to this tile family.
  static TrRandom dcRandom;
  static u8 landBumpMap[1024], waterBumpMap[1024];
  // Complete private128E1 effects; the discarded original nominal return declaration remains unresolved.
  static void GenerateBumpMaps();
  // Native constructor phase embedded in the three allocation entries; no separate original entry is asserted.
  TrTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag);
  virtual ~TrTile();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool SetDefinition(SpDefinition *definition) override;
  void GetRotationOffset(u8 *out) const override;
  u32 GetLayerFlags() const override;
  u8 SetEffect(u32 effect) override;
  void SetRotationOffset(u8 rotation) override;
  void SetLayer(u8 layer) override;
  void SetNext(SpSprite *next) override;
  u8 HasGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  SpSprite *GetNext() override;
  i32 Tick(u32 delta) override;
  // Original const receivers and signed inputs are verified; literal private return declarations remain qualified.
  virtual bool IsPixelOpaque(i32 point_x, i32 point_y, i32 zoom, i32 rotation) const override;
  i32 GetW(i32 zoom, i32 rotation) const override;
  virtual i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  virtual u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 anchor_x, i32 anchor_y) const override;
  u8 Draw(GxBuffer *target, i32 anchor_x, i32 anchor_y, i32 zoom, i32 rotation, ScRect *clip) const override;

protected:
  // Complete private10012CF3 effects; its discarded nominal return remains unresolved.
  void InvalidateCache() const;
  // Original virtual104 prepares shared vertices/strip; its private nominal return declaration remains unresolved.
  virtual void PregetPoints(i32 zoom, i32 rotation) const = 0;
  // Original virtual108 writes RGB; its discarded nominal return declaration remains unresolved.
  virtual void GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const = 0;
  void ReadSurfaceColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, u8 *rgb, u32 surface) const;
  // Native shared mutation phase of dry/water/relief geometry; original virtual methods keep their two inputs.
  void PrepareSurfaceGeometry(i32 zoom, i32 rotation, u32 surface) const;
  // Original WORD coordinates4/6 and packed palette kind/overlay/reference bytes8/9/10.
  u16 x, z;
  u8 kind;
  // Original const dry drawing updates only this cached automatic slope overlay byte.
  mutable u8 overlay;
  u8 references;
};

// Actual surface specializations share recovered fields and drawing.
class TrDryTile : public TrTile {
public:
  TrDryTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag);
  u8 Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const override;

protected:
  void PregetPoints(i32 zoom, i32 rotation) const override;
  void GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const override;
};

class TrWaterTile : public TrTile {
public:
  TrWaterTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag);
  u8 Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const override;

protected:
  void PregetPoints(i32 zoom, i32 rotation) const override;
  void GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const override;
};

class TrReliefTile : public TrTile {
public:
  TrReliefTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag);
  u8 Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const override;

protected:
  void PregetPoints(i32 zoom, i32 rotation) const override;
  void GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const override;
};

// Original single-surface tile selects a fixed palette row and optionally projects the higher water surface.
class TrSingleTile : public TrTile {
public:
  TrSingleTile(u16 cell_x, u16 cell_z, u8 palette_kind, u16 row, bool dry_only);
  u8 Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const override;

protected:
  void PregetPoints(i32 zoom, i32 rotation) const override;
  void GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const override;
};

// Original edge wall geometry and direct palette raster share the same tile fields and reference ownership.
class TrEdgeTile : public TrTile {
public:
  TrEdgeTile(u16 cell_x, u16 cell_z, bool reverse_side, bool dry_only);
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const override;

protected:
  void PregetPoints(i32 zoom, i32 rotation) const override;
  void GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const override;
};

// Complete effects of private10013556 with its ten separate arguments and actual converter callbacks.
void TrRasterTile(GxBuffer *device, const ScRect *rectangle, const ScRect *clip, i16 strip, const TrTilePoint *points,
    const TrColorVertex *colors, const u8 *noise, u32 seed, i32 rgb_noise, i32 brightness);

// Complete private148D7 effects; packed RGB inputs consume only their meaningful low three bytes.
// Original nominal return and literal private parameter declarations remain unresolved.
void TrRasterRelief(GxBuffer *device, const ScRect *rectangle, const ScRect *clip, i16 strip, const TrTilePoint *points,
    const TrColorVertex *colors, const u8 *noise, u32 seed, i32 rgb_noise, i32 brightness, u32 match, i32 outer,
    i32 inner, u32 replacement, const u8 *replacement_noise);
// Complete private15F44 effects with nine separate inputs; its unused nominal return remains unresolved.
void TrRasterWall(GxBuffer *device, const ScRect *rectangle, const ScRect *clip, i16 strip, const TrTilePoint *points,
    const i32 *rows, const u16 *palette, const u8 *noise, u32 seed);

#endif
