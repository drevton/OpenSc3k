#ifndef SC3_BUILDINGS_ARCHITECT_RENDERER_H
#define SC3_BUILDINGS_ARCHITECT_RENDERER_H

#include "buildings/architect/bd_geometry.h"
#include "buildings/architect/renderer_platform.h"
#include "common/sc3_unknown.h"

#ifndef BD_FASTCALL
#if defined(_WIN32)
#define BD_FASTCALL __fastcall
#else
#define BD_FASTCALL
#endif
#endif

class GxBuffer;

// Reached graphics interface C47B747C returns its borrowed alternate rendering object at the original first slot.
class GxRendererSource : public ScUnknown {
public:
  virtual ScUnknown *GetRenderer() = 0;
};

class BdAsset;
class BdCatalog;
class BdMaterials;
struct BdCell;
struct BdFace;
struct IDirectDraw4;
struct IDirectDrawSurface4;
struct IDirect3D3;
struct IDirect3DViewport3;

struct BdColorVertex {
  f32 x, y, z;
  u32 color;
};

struct BdColorState {
  u32 flags, field_004, field_008;
};

struct BdColorQuad {
  BdColorVertex vertices[4];
  BdColorState state;
};

struct BdColorLine {
  BdColorVertex vertices[2];
  BdColorState state;
};

struct BdTextureState {
  u32 flags;
  BdAsset *primary, *secondary;
};

// The original descriptor packs its attribute-order pointer immediately after both mapping bytes.
#pragma pack(push, 1)

struct BdFaceTextures {
  BdAsset *primary, *secondary;
  u8 primary_map, secondary_map;
  const u8 *order;
};

#pragma pack(pop)
static_assert(sizeof(BdFaceTextures) == 3 * sizeof(void *) + 2);

struct BdTextureVertex {
  f32 x, y, z;
  u32 color;
  f32 u, v;
};

struct BdTextureQuad {
  BdTextureVertex vertices[4];
  BdTextureState state;
};

struct BdDualVertex {
  f32 x, y, z;
  u32 color;
  f32 u, v, second_u, second_v;
};

struct BdDualQuad {
  BdDualVertex vertices[4];
  BdTextureState state;
};

// Concrete renderer fields reached by construction and lighting; additional initialized-later device state is unrecovered.
class BdRenderer {
public:
  BdRenderer();
  virtual ~BdRenderer();
  // Private setter return declarations remain unresolved; preserve their recovered effects.
  void SetScale(f32 value);
  void BD_FASTCALL RotateLights(i32 rotation);
  u32 *BD_FASTCALL Shade(u32 *out, f32 x, f32 y, f32 z, i32 mode);
  // Preserve the observed final color word; its private source void/integer declaration remains unresolved.
  u32 BD_FASTCALL SetLighting(i32 mode, i32 rotation);
  GxBuffer *GetBuffer();
  void SetBuffer(GxBuffer *target);
  // The private nominal return spelling is unresolved; preserve the observed HRESULT or success value.
  i32 InitializeDevice();
  i32 BeginScene();
  i32 EndScene();
  i32 ReleaseDevice();
  // State setters preserve recovered effects; their private nominal return declarations remain unresolved.
  void BD_FASTCALL SetColorKey(u8 enabled);
  void BD_FASTCALL SetColorState(const BdColorState *state);
  i32 BD_FASTCALL SetFiltering(u8 enabled);
  i32 BD_FASTCALL DrawLine(BdColorLine *line);
  i32 BD_FASTCALL DrawTriangle(BdColorQuad *triangle);
  i32 BD_FASTCALL DrawQuad(BdColorQuad *quad);
  void BD_FASTCALL SetTextureState(BdTextureState *state);
  i32 BD_FASTCALL DrawTextured(BdTextureQuad *quad);
  i32 BD_FASTCALL DrawDual(BdDualQuad *quad);
  i32 BD_FASTCALL DrawFace(const BdFace *face, u32 tint, u8 rotation);
  i32 BD_FASTCALL DrawEdges(const BdFace *face, u32 color, i32 mode);
  // Coordinate builders retain recovered effects; their private nominal return declarations remain unresolved.
  void BD_FASTCALL BuildSingleUV(BdTextureQuad *quad, const BdFaceTextures *textures);
  void BD_FASTCALL BuildDualUV(BdDualQuad *quad, const BdFaceTextures *textures);
  void BD_FASTCALL BuildObjectUV(BdTextureQuad *quad, const BdFace *face, const BdFaceTextures *textures);
  // The object-face private return and unused height declaration remain unresolved.
  void BD_FASTCALL DrawObjectFace(
      const BdFace *face, BdFaceTextures *textures, u8 rotation, i32 height, u8 use_color, u32 color);
  void BD_FASTCALL DrawTexturedFace(
      const BdFace *face, BdFaceTextures *textures, u8 rotation, i16 elevation, u8 use_color, u32 color);
  void DrawBackground(u32 color);
  void SetMaterials(BdMaterials *value);
  // Preserve observed register results; private nominal return declarations remain unresolved.
  i32 SetOffset(const BdPoint *value);
  BdCatalog *SetCatalog(BdCatalog *value);
  void BD_FASTCALL ProjectCell(const BdPoint *screen, u8 rotation);
  bool BD_FASTCALL HitFace(const BdFace *face, const BdPointF *point, u8 rotation);
  i8 BD_FASTCALL PickCellFace(BdPointF *point, const BdCell *cell);
  void BD_FASTCALL DrawCell(const BdPoint *screen, const BdCell *cell, u8 use_color, u32 color);
  void BD_FASTCALL DrawTopCell(const BdPoint *screen, const BdCell *cell, u8 use_color, u32 color);
  void BD_FASTCALL DrawHighlight(const BdPoint *screen, const BdCell *cell, u32 color);
  void BD_FASTCALL DrawMaskedCell(const BdPoint *screen, const BdCell *cell, u8 planes);
  u8 BD_FASTCALL ClassifyPlane(u8 direction);
  i32 BD_FASTCALL DrawPlane(
      const BdPoint *origin, i32 first_x, i32 first_y, i32 second_x, i32 second_y, i32 step, u32 color);
  i32 BD_FASTCALL DrawScreenQuad(
      const BdPoint *first, const BdPoint *second, const BdPoint *third, const BdPoint *fourth, u32 color);
  i32 BD_FASTCALL DrawSegment(const BdPoint *first, const BdPoint *second, u32 color);
  // Tile primitives preserve observed submission results; private nominal declarations remain unresolved.
  i32 BD_FASTCALL DrawTileTriangle(const BdPoint *screen, i32 first, i32 second, i32 third, u32 color);
  i32 BD_FASTCALL DrawTileQuad(const BdPoint *screen, i32 first, i32 second, i32 third, i32 fourth, u32 color);
  void BD_FASTCALL DrawBottomShadow(const BdPoint *screen, u8 mask, u32 color);
  void BD_FASTCALL DrawLeftShadow(const BdPoint *screen, u8 mask, u32 color);
  void BD_FASTCALL DrawRightShadow(const BdPoint *screen, u8 mask, u32 color);
  void BD_FASTCALL DrawObjectCell(const BdPoint *screen, const BdCell *cell, u8 use_color, u32 color);

  BdPointF points[12], scaled[12];
  u32 colors[64];
  BdPointF projected[12], object_uv[12];
  BdRect clip;
  BdPoint offset;
  BdCatalog *catalog;
  GxBuffer *buffer;
  BdMaterials *materials;
  f32 width, height, scale;
  u8 draw_edges, draw_empty_objects;
  IDirectDraw4 *direct_draw;
  IDirectDrawSurface4 *temporary_surface;
  IDirect3D3 *direct_3d;
  BdRenderDevice *device;
  IDirect3DViewport3 *viewport;
  u8 scene_active;
  u32 draw_calls, triangles, lines, texture_binds, state_changes;
  u8 use_sdk;
  ScUnknown *sdk_renderer;
  u8 device_ready, stage0_enabled, stage1_enabled, blend_enabled, keying_enabled, field_311, stage0_linear,
      stage1_linear, field_314, linear_filter;
};

u32 BdTintColor(u32 *color, u32 tint);
extern BdPointF BdTextureCorners[9];
extern const BdPointF BdShapePoints[12];
void BD_FASTCALL BdBuildTextureCorners(u8 mapping, f32 inverse_width, f32 inverse_height);

#endif
