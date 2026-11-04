#include "terrain/tr_tile.h"

#include <math.h>

#include "city/api.h"
#include "common/sc3_director.h"
#include "economy/api.h"
#include "graphics/image_codecs.h"
#include "terrain/api.h"
#include "terrain/tr_dirt_bag.h"
#include "terrain/tr_generator.h"

u8 TrTile::landBumpMap[1024], TrTile::waterBumpMap[1024];
TrRandom TrTile::dcRandom(kScRandomSeedFromClock);

// Reseed the renderer's independent stream from the real clock, then produce land jitter and alternating water runs.
// Consume the final zero-run draw even when its preceding nonzero run has already filled the water table.
void TrTile::GenerateBumpMaps() {
  dcRandom.Seed(kScRandomSeedFromClock);
  for (u32 i = 0; i < 1024; ++i)
    landBumpMap[i] = (u8)(8u * (dcRandom.RandomUint32Uniform() % 3) - 8u);
  u32 index = 0;
  do {
    u32 count = (dcRandom.RandomUint32Uniform() & 7) + 1;
    u8 value = dcRandom.RandomUint32Uniform() & 1 ? 8 : (u8)-9;
    while (count && index < 1024) {
      waterBumpMap[index++] = value;
      --count;
    }
    count = (dcRandom.RandomUint32Uniform() & 7) + 1;
    while (count && index < 1024) {
      waterBumpMap[index++] = 0;
      --count;
    }
  } while (index < 1024);
}

TrTilePoint TrTile::screenPoint[4];
u16 TrTileStrip;

// Actual complete polygon-chain topology at original10024258; final columns select normalization and bounds corners.
const u8 TrTileTopology[55][19] = {{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {2, 0, 3, 0, 1, 3, 0, 2, 1, 3, 2, 3, 0, 2, 3, 3, 2, 0, 3},
    {2, 0, 3, 0, 1, 3, 0, 1, 3, 2, 3, 0, 0, 2, 2, 3, 2, 0, 3},
    {1, 0, 3, 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 2, 0, 4, 0, 0, 3},
    {1, 0, 3, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0, 0, 3},
    {1, 0, 3, 2, 0, 1, 2, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0, 0, 2},
    {2, 0, 3, 1, 3, 0, 0, 2, 1, 3, 2, 3, 0, 2, 3, 2, 2, 2, 3},
    {2, 0, 3, 1, 3, 0, 0, 1, 3, 2, 3, 0, 0, 2, 2, 2, 2, 0, 3},
    {1, 0, 3, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0, 0, 3},
    {1, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 0, 3},
    {1, 0, 3, 2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 0, 0, 2},
    {1, 2, 1, 0, 3, 2, 3, 0, 0, 0, 0, 0, 0, 4, 0, 2, 0, 2, 3},
    {1, 1, 0, 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 0, 1, 3},
    {1, 1, 0, 3, 1, 2, 3, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0, 1, 3},
    {1, 1, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 0, 1, 3},
    {1, 1, 0, 3, 2, 1, 2, 0, 0, 0, 0, 0, 0, 4, 0, 2, 0, 1, 2},
    {1, 2, 1, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 0, 2, 0},
    {1, 1, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 1, 0},
    {1, 1, 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 1, 2, 0, 0, 2, 2, 2, 2, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 2, 2, 1, 2},
    {1, 2, 1, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0, 2, 0},
    {1, 1, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0, 1, 0},
    {1, 1, 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 2, 0, 4, 0, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 1, 2, 0, 0, 2, 2, 3, 2, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 3, 2, 1, 0},
    {2, 0, 3, 0, 1, 3, 0, 2, 1, 3, 2, 3, 0, 2, 3, 3, 2, 2, 3},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 3, 2, 1, 2},
    {2, 0, 3, 0, 1, 3, 0, 2, 1, 3, 2, 3, 0, 2, 3, 3, 2, 0, 3},
    {2, 0, 3, 0, 1, 3, 0, 1, 3, 2, 3, 0, 0, 2, 2, 3, 2, 0, 3},
    {2, 0, 3, 0, 1, 3, 0, 1, 3, 1, 2, 3, 0, 2, 2, 3, 3, 0, 3},
    {2, 0, 3, 0, 1, 3, 0, 1, 3, 1, 2, 0, 0, 2, 2, 3, 2, 0, 3},
    {2, 0, 3, 0, 1, 3, 0, 1, 3, 2, 1, 2, 0, 2, 3, 3, 2, 0, 2},
    {2, 0, 3, 1, 3, 0, 0, 2, 1, 3, 2, 3, 0, 2, 3, 2, 2, 2, 3},
    {2, 0, 3, 1, 3, 0, 0, 1, 3, 2, 3, 0, 0, 2, 2, 2, 2, 0, 3},
    {2, 0, 3, 1, 3, 0, 0, 1, 3, 1, 2, 3, 0, 2, 2, 2, 3, 0, 3},
    {2, 0, 3, 1, 3, 0, 0, 1, 3, 1, 2, 0, 0, 2, 2, 2, 2, 0, 3},
    {2, 0, 3, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 2, 2, 0, 2},
    {2, 1, 0, 3, 1, 3, 0, 2, 1, 3, 2, 3, 0, 3, 3, 2, 2, 2, 3},
    {2, 1, 0, 3, 1, 3, 0, 1, 3, 2, 3, 0, 0, 3, 2, 2, 2, 1, 3},
    {2, 1, 0, 3, 1, 3, 0, 1, 3, 1, 2, 3, 0, 3, 2, 2, 3, 1, 3},
    {2, 1, 0, 3, 1, 3, 0, 1, 3, 1, 2, 0, 0, 3, 2, 2, 2, 1, 3},
    {2, 1, 0, 3, 1, 3, 0, 1, 3, 2, 1, 2, 0, 3, 3, 2, 2, 1, 2},
    {2, 1, 0, 1, 3, 0, 0, 2, 1, 3, 2, 3, 0, 2, 3, 2, 2, 2, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 3, 0, 0, 2, 2, 2, 2, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 1, 2, 3, 0, 2, 2, 2, 3, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 1, 2, 0, 0, 2, 2, 2, 2, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 2, 2, 1, 2},
    {2, 1, 0, 1, 3, 0, 0, 2, 1, 3, 2, 3, 0, 2, 3, 3, 2, 2, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 3, 0, 0, 2, 2, 3, 2, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 1, 2, 3, 0, 2, 2, 3, 3, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 1, 2, 0, 0, 2, 2, 3, 2, 1, 0},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 3, 2, 1, 0},
    {2, 0, 3, 0, 1, 3, 0, 2, 1, 3, 2, 3, 0, 2, 3, 3, 2, 2, 3},
    {2, 1, 0, 1, 3, 0, 0, 1, 3, 2, 1, 2, 0, 2, 3, 3, 2, 1, 2}};

TrTile::TrTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag)
    : x(cell_x), z(cell_z), kind(palette_kind), overlay(overlay_flag), references(0) {
}

TrTile::~TrTile() {
}

i32 TrTile::GetW(i32 zoom, i32) const {
  return (i32)TrTileWidths[zoom];
}

// Prepare current geometry under the module lock and return the selected bottom vertex's signed ordinate.
i32 TrTile::GetH(i32 zoom, i32 rotation) const {
  TrLockScope lock;
  PregetPoints(zoom, rotation);
  return TrTileStrip ? TrTile::screenPoint[TrTileTopology[TrTileStrip][18]].y : 0;
}

// Prepare geometry before the live width callback, then derive signed vertical bounds from the current strip.
// Only literal rotations zero and two receive the half-height adjustment; corner selection uses the low two bits.
u8 TrTile::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  TrLockScope lock;
  PregetPoints(zoom, rotation);
  out->left = 0;
  out->right = GetW(zoom, rotation);
  if (TrTileStrip) {
    i16 half = i16(TrTileHeights[zoom] >> 1);
    u32 corner = (1u - u32(u8(rotation))) & 3;
    out->top = TrTile::screenPoint[corner].y;
    out->bottom = TrTile::screenPoint[TrTileTopology[TrTileStrip][18]].y - TrTile::screenPoint[corner].y;
    if (!rotation) {
      out->top += half;
      out->bottom -= half;
    } else if (rotation == 2) {
      out->top -= half;
      out->bottom += half;
    }
  } else {
    out->bottom = 0;
    out->top = 0;
  }
  return 1;
}

// Ignore the virtual bounds reply and preserve the original right/left/bottom/top translation store order.
u8 TrTile::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 anchor_x, i32 anchor_y) const {
  GetRegistration(zoom, rotation, out);
  out->right = i32(u32(out->right) + u32(anchor_x));
  out->left = i32(u32(anchor_x) - u32(out->left));
  i32 top = i32(u32(anchor_y) - u32(out->top));
  out->bottom = i32(u32(out->bottom) + u32(anchor_y));
  out->top = top;
  return 1;
}

// Obtain the genuine temporary rectangle before invoking the live pixel draw entry with its original arguments.
// Preserve its raw byte reply rather than normalizing the drawing result.
u8 TrTile::Draw(GxBuffer *target, i32 anchor_x, i32 anchor_y, i32 zoom, i32 rotation, ScRect *clip) const {
  ScRect rectangle;
  GetAreaFromPt(zoom, rotation, &rectangle, anchor_x, anchor_y);
  return Draw(target, rectangle, zoom, rotation, clip);
}

// Select the original strip topology from signed heights, then normalize all ordinates against its first vertex.
// Collapsed geometry reads only vertices three and one before clearing all heights in the original reverse order.
i16 TrClassifyTile(TrTilePoint *points, u8 mixed_water) {
  TrLockScope lock;
  i16 y3 = points[3].y, y1 = points[1].y;
  if (y3 <= y1) {
    points[3].y = 0;
    points[2].y = 0;
    points[1].y = 0;
    points[0].y = 0;
    return 0;
  }
  i16 y0 = points[0].y, y2 = points[2].y;
  u32 first = y0 < y1 ? 0 : y0 == y1 ? 1 : y0 < y3 ? 2 : y0 == y3 ? 3 : 4;
  u32 second = y2 < y1 ? 0 : y2 == y1 ? 1 : y2 < y3 ? 2 : y2 == y3 ? 3 : 4;
  u32 strip = 5 * first + second + 1;
  if (strip == 1 && y2 < y0)
    strip = 26;
  if (strip == 25 && y2 > y0)
    strip = 27;
  if (mixed_water)
    strip += 27;
  i16 minimum = points[TrTileTopology[strip][17]].y;
  points[2].y = i16(i32(y2) - minimum);
  points[0].y = i16(i32(y0) - minimum);
  points[1].y = i16(i32(y1) - minimum);
  points[3].y = i16(i32(y3) - minimum);
  return i16(strip);
}

bool TrTile::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, TrTile);
  HANDLE_QUERY_INTERFACE(IID_SpSprite, TrTile);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the actual byte reference count before retaining the module, then return the live zero-extended byte.
// Preserve byte wrapping and reload changes made by the intervening module callbacks.
u32 TrTile::AddRef() {
  ++references;
  TrGetDirector()->AddRef();
  return references;
}

// Drop a module pin before decrementing a nonzero byte count, deleting through the real virtual destructor at zero.
// An initially zero count also deletes; general allocator free-list machinery is replaced by ordinary native delete.
u32 TrTile::Release() {
  if (references) {
    TrGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

void TrTile::GetRotationOffset(u8 *out) const {
  *out = 0;
}

u32 TrTile::GetLayerFlags() const {
  return 0;
}

u8 TrTile::SetEffect(u32) {
  return 0;
}

void TrTile::SetRotationOffset(u8) {
}

void TrTile::SetLayer(u8) {
}

void TrTile::SetNext(SpSprite *) {
}

u8 TrTile::SetGeomFlag(u32) {
  return 0;
}

u8 TrTile::DemandArt(i32, i32, bool) {
  return 1;
}

i32 TrTile::ReleaseArt(i32, i32) {
  return 1;
}

// Return the original null next-sprite pointer without reading any tile fields.
SpSprite *TrTile::GetNext() {
  return nullptr;
}

i32 TrTile::Tick(u32) {
  return 0;
}

// Compute the original signed cross-product bits without introducing native signed-overflow behavior.
// The geometric inputs are live signed WORD vertices and the caller's separate signed point coordinates.
static i32 TrTileCross(i32 x, i32 y, const TrTilePoint &first, const TrTilePoint &second) {
  u32 dy = u32(y) - u32(i32(first.y)), dx = u32(x) - u32(i32(first.x)), edge_x = u32(i32(second.x)) - u32(i32(first.x)),
      edge_y = u32(i32(second.y)) - u32(i32(first.y));
  return i32(dy * edge_x - dx * edge_y);
}

// Prepare live geometry under the module lock, then test the original two closed triangles around diagonal1-3.
// Keep short-circuit edge ordering and the original degenerate-line behavior rather than adding a strip rejection.
bool TrTile::IsPixelOpaque(i32 point_x, i32 point_y, i32 zoom, i32 rotation) const {
  TrLockScope lock;
  PregetPoints(zoom, rotation);
  return (TrTileCross(point_x, point_y, TrTile::screenPoint[1], TrTile::screenPoint[0]) <= 0 &&
             TrTileCross(point_x, point_y, TrTile::screenPoint[0], TrTile::screenPoint[3]) <= 0 &&
             TrTileCross(point_x, point_y, TrTile::screenPoint[3], TrTile::screenPoint[1]) <= 0) ||
      (TrTileCross(point_x, point_y, TrTile::screenPoint[3], TrTile::screenPoint[2]) <= 0 &&
          TrTileCross(point_x, point_y, TrTile::screenPoint[2], TrTile::screenPoint[1]) <= 0 &&
          TrTileCross(point_x, point_y, TrTile::screenPoint[1], TrTile::screenPoint[3]) <= 0);
}

// Clear its four cache corners in the original northeast, southeast, southwest, northwest store order.
void TrTile::InvalidateCache() const {
  TrLockScope lock;
  u16 *cache = TrRendererSingleton->cache;
  u32 first = u32(z) + 257 * u32(x);
  cache[first + 257] = 0;
  cache[first + 258] = 0;
  cache[first + 1] = 0;
  cache[first] = 0;
}

bool TrTile::SetDefinition(SpDefinition *) {
  return true;
}

// Prepare the edge geometry under the module lock and derive its signed left/top/right/bottom registration.
// Preserve literal rotation adjustments and the collapsed-strip path that leaves horizontal outputs untouched.
u8 TrEdgeTile::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  TrLockScope lock;
  PregetPoints(zoom, rotation);
  if (TrTileStrip) {
    i16 half = i16(TrTileHeights[zoom] >> 1);
    out->left = -i32(TrTile::screenPoint[0].x);
    out->right = TrTile::screenPoint[1].x;
    out->top = -i32(TrTile::screenPoint[TrTileTopology[TrTileStrip][17]].y);
    out->bottom = TrTile::screenPoint[TrTileTopology[TrTileStrip][18]].y;
    if (!rotation) {
      out->top += half;
      out->bottom -= half;
    } else if (rotation == 2) {
      out->top -= half;
      out->bottom += half;
    }
  } else {
    out->bottom = 0;
    out->top = 0;
  }
  return 1;
}

u8 TrTile::HasGeomFlag(u32 mask) {
  return mask == (mask & 1u);
}

u8 TrTile::ClearGeomFlag(u32) {
  return 0;
}

u32 TrTile::GetGeomFlag() {
  return 1;
}

GxPackFunction TrPackColor;
GxUnpackFunction TrUnpackColor;
static u8 TrOverlayColor[3];

// These are the actual module function pointers; callers require both before entering pixel drawing.
bool TrRefreshPixelConverters(GxBuffer *device) {
  TrPackColor = device->GetRGBTo16ConverterPtr();
  TrUnpackColor = device->Get16ToRGBConverterPtr();
  return TrPackColor && TrUnpackColor;
}

u16 TrAttenuateAndPackColor(GxBuffer *, i32 zoom, u8 *rgb) {
  static const u8 addition[3][3] = {{49, 51, 53}, {33, 34, 35}, {16, 17, 18}};
  for (u32 channel = 0; channel < 3; ++channel) {
    u8 value = rgb[channel];
    rgb[channel] = u8(value + addition[zoom][channel] - (3 - zoom) * ((value + 5) / 10));
  }
  u16 packed;
  TrPackColor(&packed, rgb[0], rgb[1], rgb[2]);
  return packed;
}

void TrMixOverlayColor(u8 *rgb) {
  for (u32 channel = 0; channel < 3; ++channel)
    rgb[channel] = u8((u32(TrOverlayColor[channel]) + rgb[channel]) >> 1);
}

// The converter writes the actual shared RGB bytes before optional attenuation changes them.
void TrLoadOverlayColor(GxBuffer *device, i32 zoom, u8 index) {
  u16 pixel = TrRendererSingleton->GetColorInNative16Bit(0, index, TrPalette::LightIndexMax);
  TrUnpackColor(pixel, TrOverlayColor, TrOverlayColor + 1, TrOverlayColor + 2);
  if (zoom <= 2)
    TrAttenuateAndPackColor(device, zoom, TrOverlayColor);
}

// Share the actual cache/palette phase of dry, water and relief virtual108 without inventing a public game contract.
// Cache hits skip attenuation; callbacks see the original intermediate row and pixel stores on misses.
void TrTile::ReadSurfaceColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, u8 *rgb, u32 surface) const {
  TrRenderer *renderer = TrRendererSingleton;
  u16 *cached = renderer->cache + vertex_z + 257 * u32(vertex_x);
  if (surface == 2 && renderer->water->columns[vertex_x][vertex_z] >= renderer->ground->columns[vertex_x][vertex_z]) {
    rgb[0] = 160;
    rgb[1] = 160;
    rgb[2] = 160;
    if (zoom <= 2)
      TrAttenuateAndPackColor(device, zoom, rgb);
    return;
  }
  if (*cached) {
    TrUnpackColor(*cached, rgb, rgb + 1, rgb + 2);
  } else {
    u16 row;
    u8 column, family;
    if (surface == 1) {
      i32 depth = i32(renderer->water->columns[vertex_x][vertex_z]) - renderer->ground->columns[vertex_x][vertex_z];
      row = u16(depth > 0 ? depth : 0);
      column = 0;
      family = 6;
    } else {
      column = renderer->shade->columns[vertex_x][vertex_z] >> 1;
      row =
          u16(i32(renderer->water->columns[vertex_x][vertex_z]) - renderer->ground->columns[vertex_x][vertex_z] + 256);
      family = kind & 63;
    }
    *cached = row;
    u16 pixel = renderer->GetColorInNative16Bit(family, row, column);
    *cached = pixel;
    TrUnpackColor(pixel, rgb, rgb + 1, rgb + 2);
    if (zoom <= 2)
      *cached = TrAttenuateAndPackColor(device, zoom, rgb);
  }
  if (overlay) {
    TrMixOverlayColor(rgb);
    return;
  }
  if (surface == 1 &&
      TrGetCity()->PollutionLayer()->IsWaterPolluted(
          vertex_x ? u32(vertex_x) - 1 : 0, vertex_z ? u32(vertex_z) - 1 : 0)) {
    TrLoadOverlayColor(device, zoom, 51);
    TrMixOverlayColor(rgb);
  }
}

void TrDryTile::GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const {
  ReadSurfaceColor(device, vertex_x, vertex_z, zoom, color->rgb, 0);
}

void TrWaterTile::GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const {
  ReadSurfaceColor(device, vertex_x, vertex_z, zoom, color->rgb, 1);
}

void TrReliefTile::GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const {
  ReadSurfaceColor(device, vertex_x, vertex_z, zoom, color->rgb, 2);
}

void TrSingleTile::GetColor(GxBuffer *device, u16 vertex_x, u16 vertex_z, i32 zoom, TrColorVertex *color) const {
  TrRenderer *renderer = TrRendererSingleton;
  u8 shade = renderer->shade->columns[vertex_x][vertex_z] >> 1;
  u16 pixel = renderer->GetColorInNative16Bit(kind & 0x3f, overlay & 0x7f, shade);
  TrUnpackColor(pixel, color->rgb, color->rgb + 1, color->rgb + 2);
  if (zoom <= 2)
    TrAttenuateAndPackColor(device, zoom, color->rgb);
}

TrColorVertex TrTile::vertex[4];
i32 TrTileZoom = 100;

// Draw a dry tile under the terrain lock, preserving the entry-strip shortcut before touching the device.
// Refresh geometry and zoom cache, update the automatic slope overlay, then rasterize four live corner colors.
u8 TrDryTile::Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const {
  TrLockScope lock;
  if (!TrTileStrip)
    return 1;
  if (!TrRefreshPixelConverters(target))
    return 0;
  PregetPoints(zoom, rotation);
  if (zoom != TrTileZoom) {
    if (zoom <= 2 || TrTileZoom <= 2)
      TrRendererSingleton->InvalidateColorTable();
    TrTileZoom = zoom;
  }
  if ((overlay == 0 || overlay == 52) && kind == 5) {
    TrByteGrid *ground = TrRendererSingleton->ground;
    u8 northwest = ground->columns[x][z], southwest = ground->columns[x][u32(z) + 1],
       southeast = ground->columns[u32(x) + 1][u32(z) + 1], northeast = ground->columns[u32(x) + 1][z];
    i32 north_delta = i32(northwest) - northeast, west_delta = i32(northwest) - southwest;
    if (north_delta < 0)
      north_delta = -north_delta;
    if (west_delta < 0)
      west_delta = -west_delta;
    bool flat = (northwest == southwest && northeast == southeast && north_delta <= 2) ||
        (northwest == northeast && southwest == southeast && west_delta <= 2);
    overlay = flat ? 0 : 52;
  }
  if (overlay)
    TrLoadOverlayColor(target, zoom, overlay);
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0}, screen[4] = {1, 2, 3, 0};
  for (u32 i = 0; i < 4; ++i)
    GetColor(target, u16(u32(x) + dx[i]), u16(u32(z) + dz[i]), zoom, &TrTile::vertex[(screen[i] - u32(rotation)) & 3]);
  i32 brightness = TrRendererSingleton->grid ? -16 : 0;
  if (brightness && zoom < 2)
    brightness = -8;
  u32 seed = (0u - 169u * u32(z) - 327u * u32(x)) & 1023;
  TrRasterTile(target, &rectangle, clip, i16(TrTileStrip), TrTile::screenPoint, TrTile::vertex, TrTile::landBumpMap,
      seed, 1, brightness);
  return 1;
}

// Draw water geometry and four live water colors under the terrain lock, retaining the entry-strip shortcut.
// Preserve zoom cache invalidation and explicit overlay loading before the scalar-noise raster pass.
u8 TrWaterTile::Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const {
  TrLockScope lock;
  if (!TrTileStrip)
    return 1;
  if (!TrRefreshPixelConverters(target))
    return 0;
  PregetPoints(zoom, rotation);
  if (zoom != TrTileZoom) {
    if (zoom <= 2 || TrTileZoom <= 2)
      TrRendererSingleton->InvalidateColorTable();
    TrTileZoom = zoom;
  }
  if (overlay)
    TrLoadOverlayColor(target, zoom, overlay);
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0}, screen[4] = {1, 2, 3, 0};
  for (u32 i = 0; i < 4; ++i)
    GetColor(target, u16(u32(x) + dx[i]), u16(u32(z) + dz[i]), zoom, &TrTile::vertex[(screen[i] - u32(rotation)) & 3]);
  i32 brightness = TrRendererSingleton->grid ? -16 : 0;
  if (brightness && zoom < 2)
    brightness = -8;
  u32 seed = (0u - 169u * u32(z) - 327u * u32(x)) & 1023;
  TrRasterTile(target, &rectangle, clip, i16(TrTileStrip), TrTile::screenPoint, TrTile::vertex, TrTile::waterBumpMap,
      seed, 0, brightness);
  return 1;
}

// Draw the higher terrain surface with live corner colors and the original gray-to-water distance blend.
// Preserve replacement-color pollution queries and both discarded attenuation pack side effects before rasterization.
u8 TrReliefTile::Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const {
  TrLockScope lock;
  if (!TrTileStrip)
    return 1;
  if (!TrRefreshPixelConverters(target))
    return 0;
  PregetPoints(zoom, rotation);
  if (zoom != TrTileZoom) {
    if (zoom <= 2 || TrTileZoom <= 2)
      TrRendererSingleton->InvalidateColorTable();
    TrTileZoom = zoom;
  }
  if (overlay)
    TrLoadOverlayColor(target, zoom, overlay);
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0}, screen[4] = {1, 2, 3, 0};
  for (u32 i = 0; i < 4; ++i)
    GetColor(target, u16(u32(x) + dx[i]), u16(u32(z) + dz[i]), zoom, &TrTile::vertex[(screen[i] - u32(rotation)) & 3]);
  i32 brightness = TrRendererSingleton->grid ? -16 : 0;
  if (brightness && zoom < 2)
    brightness = -8;
  u8 water[3];
  TrUnpackColor(TrRendererSingleton->GetColorInNative16Bit(6, 0, 0), water, water + 1, water + 2);
  if (zoom <= 2)
    TrAttenuateAndPackColor(target, zoom, water);
  if (overlay) {
    TrMixOverlayColor(water);
  } else if (TrGetCity()->PollutionLayer()->IsWaterPolluted(x ? u32(x) - 1 : 0, z ? u32(z) - 1 : 0)) {
    TrLoadOverlayColor(target, zoom, 51);
    TrMixOverlayColor(water);
  }
  u8 gray[3] = {160, 160, 160};
  if (zoom <= 2)
    TrAttenuateAndPackColor(target, zoom, gray);
  u32 match = u32(gray[0]) | (u32(gray[1]) << 8) | (u32(gray[2]) << 16),
      replacement = u32(water[0]) | (u32(water[1]) << 8) | (u32(water[2]) << 16),
      seed = (0u - 169u * u32(z) - 327u * u32(x)) & 1023;
  TrRasterRelief(target, &rectangle, clip, i16(TrTileStrip), TrTile::screenPoint, TrTile::vertex, TrTile::landBumpMap,
      seed, 1, brightness, match, 1024, 256, replacement, TrTile::waterBumpMap);
  return 1;
}

// Draw the fixed-row surface under the terrain lock with live geometry and four rotated vertex colors.
// Grid brightness reaches the ordinary raster without noise, overlay mixing or terrain cache mutation.
u8 TrSingleTile::Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const {
  TrLockScope lock;
  if (!TrTileStrip)
    return 1;
  if (!TrRefreshPixelConverters(target))
    return 0;
  PregetPoints(zoom, rotation);
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0}, screen[4] = {1, 2, 3, 0};
  for (u32 i = 0; i < 4; ++i)
    GetColor(target, u16(u32(x) + dx[i]), u16(u32(z) + dz[i]), zoom, &TrTile::vertex[(screen[i] - u32(rotation)) & 3]);
  i32 brightness = TrRendererSingleton->grid ? -16 : 0;
  if (brightness && zoom < 2)
    brightness = -8;
  TrRasterTile(
      target, &rectangle, clip, i16(TrTileStrip), TrTile::screenPoint, TrTile::vertex, nullptr, 0, 0, brightness);
  return 1;
}

TrTilePoint TrWallWaterVertices[4];
TrTilePoint TrWallGroundVertices[4] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
i32 TrWallRows[4] = {-1, -1, -1, -1}, TrWallZoom = -1;
u16 TrWallPalettes[2][512];

// Rebuild the two zoom-dependent wall palettes before preparing edge geometry and capturing its top registration.
// Draw the water face first, then the ground face with live geometry/flags and the original scalar noise raster.
u8 TrEdgeTile::Draw(GxBuffer *target, const ScRect &rectangle, i32 zoom, i32 rotation, ScRect *clip) const {
  TrLockScope lock;
  if (!TrRefreshPixelConverters(target))
    return 0;
  if (TrWallZoom != zoom) {
    TrRenderer *renderer = TrRendererSingleton;
    for (u32 row = 0; row < 512; ++row) {
      TrWallPalettes[0][row] = renderer->GetColorInNative16Bit(7, u16(row), 0);
      TrWallPalettes[1][row] = renderer->GetColorInNative16Bit(8, u16(row), 0);
    }
    if (zoom < 3) {
      for (u32 row = 0; row < 512; ++row) {
        u8 rgb[3];
        TrUnpackColor(TrWallPalettes[0][row], rgb, rgb + 1, rgb + 2);
        TrWallPalettes[0][row] = TrAttenuateAndPackColor(target, zoom, rgb);
        TrUnpackColor(TrWallPalettes[1][row], rgb, rgb + 1, rgb + 2);
        TrWallPalettes[1][row] = TrAttenuateAndPackColor(target, zoom, rgb);
      }
    }
    TrWallZoom = zoom;
  }
  PregetPoints(zoom, rotation);
  u8 flags = kind;
  bool reverse = (flags & 0x80) != 0;
  u32 orientation = (reverse ? u32(rotation) : ~u32(rotation)) & 1;
  static const u32 first[4] = {2, 3, 0, 1}, second[4] = {1, 2, 3, 0};
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0};
  u32 edge = (u32(rotation) + u32(reverse)) & 3, corners[2] = {first[edge], second[edge]};
  TrRenderer *renderer = TrRendererSingleton;
  u8 ground[2], water[2];
  for (u32 i = 0; i < 2; ++i) {
    u32 cell_x = u32(x) + dx[corners[i]], cell_z = u32(z) + dz[corners[i]];
    ground[i] = renderer->ground->columns[cell_x][cell_z];
    water[i] = renderer->water->columns[cell_x][cell_z];
  }
  i16 left = TrTile::screenPoint[0].x, top = TrTile::screenPoint[TrTileTopology[TrTileStrip][17]].y;
  if (!(flags & 0x40) &&
      (TrEdgeGroundHeights[0] > TrTile::screenPoint[0].y || TrEdgeGroundHeights[1] > TrTile::screenPoint[1].y)) {
    i16 width = i16(i32(TrTile::screenPoint[1].x) - TrTile::screenPoint[0].x);
    TrWallWaterVertices[0] = {0, i16(i32(TrTile::screenPoint[0].y) - top)};
    TrWallWaterVertices[1] = {width, i16(i32(TrTile::screenPoint[1].y) - top)};
    TrWallWaterVertices[2] = {width, i16(i32(TrEdgeGroundHeights[1]) - top)};
    TrWallWaterVertices[3] = {0, i16(i32(TrEdgeGroundHeights[0]) - top)};
    TrWallRows[0] = 0;
    TrWallRows[3] = TrEdgeGroundHeights[0] > TrTile::screenPoint[0].y ? i32(water[0]) - ground[0] : 0;
    TrWallRows[1] = 0;
    TrWallRows[2] = TrEdgeGroundHeights[1] > TrTile::screenPoint[1].y ? i32(water[1]) - ground[1] : 0;
    u16 strip = TrClassifyEdge(TrWallWaterVertices);
    TrRasterWall(
        target, &rectangle, clip, i16(strip), TrWallWaterVertices, TrWallRows, TrWallPalettes[orientation], nullptr, 0);
  }
  if (TrEdgeGroundHeights[0] < TrTile::screenPoint[3].y || TrEdgeGroundHeights[1] < TrTile::screenPoint[2].y) {
    TrWallGroundVertices[0] = {i16(i32(TrTile::screenPoint[3].x) - left), i16(i32(TrEdgeGroundHeights[0]) - top)};
    TrWallGroundVertices[1] = {i16(i32(TrTile::screenPoint[2].x) - left), i16(i32(TrEdgeGroundHeights[1]) - top)};
    TrWallGroundVertices[2] = {i16(i32(TrTile::screenPoint[2].x) - left), i16(i32(TrTile::screenPoint[2].y) - top)};
    TrWallGroundVertices[3] = {i16(i32(TrTile::screenPoint[3].x) - left), i16(i32(TrTile::screenPoint[3].y) - top)};
    TrWallRows[0] = TrWallRows[1] = kind & 0x40 ? 260 : 256;
    TrWallRows[2] = i32(ground[1]) + 260;
    TrWallRows[3] = i32(ground[0]) + 260;
    if (TrWallRows[2] > 511)
      TrWallRows[2] = 511;
    if (TrWallRows[3] > 511)
      TrWallRows[3] = 511;
    u32 seed = (0u - 169u * u32(z) - 327u * u32(x)) & 1023;
    u16 strip = TrClassifyEdge(TrWallGroundVertices);
    TrRasterWall(target, &rectangle, clip, i16(strip), TrWallGroundVertices, TrWallRows, TrWallPalettes[orientation],
        TrTile::waterBumpMap, seed);
  }
  return 1;
}

TrDryTile::TrDryTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag)
    : TrTile(cell_x, cell_z, palette_kind, overlay_flag) {
  InvalidateCache();
}

TrWaterTile::TrWaterTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag)
    : TrTile(cell_x, cell_z, palette_kind, overlay_flag) {
  InvalidateCache();
}

TrReliefTile::TrReliefTile(u16 cell_x, u16 cell_z, u8 palette_kind, u8 overlay_flag)
    : TrTile(cell_x, cell_z, palette_kind, overlay_flag) {
  InvalidateCache();
}

// Restore the common field/plane phase of the three actual geometry entries without adding new game arguments.
// The real cached renderer must already be alive; geometry does not invoke its lazy constructor or acquire ownership.
void TrTile::PrepareSurfaceGeometry(i32 zoom, i32 rotation, u32 surface) const {
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0};
  static const u32 screen[4] = {1, 2, 3, 0};
  TrTile::screenPoint[0].x = 0;
  TrTile::screenPoint[1].x = i16(TrTileHalfWidths[zoom]);
  TrTile::screenPoint[3].x = TrTile::screenPoint[1].x;
  TrTile::screenPoint[2].x = i16(TrTileWidths[zoom]);
  i16 scale = i16(-i32(i16(TrTileScales[zoom])));
  u32 half = TrTileHeights[zoom] >> 1;
  TrRenderer *renderer = TrRendererSingleton;
  u8 wet[4];
  for (u32 i = 0; i < 4; ++i) {
    u32 index = (screen[i] - rotation) & 3;
    u32 cell_x = u32(x) + dx[i], cell_z = u32(z) + dz[i];
    u8 height;
    if (surface == 2) {
      u8 dry = renderer->ground->columns[cell_x][cell_z];
      u8 water = renderer->water->columns[cell_x][cell_z];
      wet[index] = water >= dry;
      height = water >= dry ? water : dry;
    } else if (surface == 3) {
      height = renderer->ground->columns[cell_x][cell_z];
      if (overlay & 0x80) {
        u8 water = renderer->water->columns[cell_x][cell_z];
        if (water > height)
          height = water;
      }
    } else {
      TrByteGrid *plane = surface ? renderer->water : renderer->ground;
      height = plane->columns[cell_x][cell_z];
    }
    TrTile::screenPoint[index].y = i16(i32(scale) * height);
  }
  bool diagonal = surface == 3 ? false
      : surface == 2
      ? wet[1] == wet[3] && wet[0] != wet[2]
      : TrTile::screenPoint[1].y == TrTile::screenPoint[3].y && TrTile::screenPoint[0].y != TrTile::screenPoint[2].y;
  TrTile::screenPoint[1].y = i16(i32(TrTile::screenPoint[1].y) - i32(u16(half)));
  TrTile::screenPoint[3].y = i16(i32(TrTile::screenPoint[3].y) + i32(u16(half)));
  TrTileStrip = u16(TrClassifyTile(TrTile::screenPoint, u8(diagonal)));
}

void TrDryTile::PregetPoints(i32 zoom, i32 rotation) const {
  PrepareSurfaceGeometry(zoom, rotation, 0);
}

void TrWaterTile::PregetPoints(i32 zoom, i32 rotation) const {
  PrepareSurfaceGeometry(zoom, rotation, 1);
}

void TrReliefTile::PregetPoints(i32 zoom, i32 rotation) const {
  PrepareSurfaceGeometry(zoom, rotation, 2);
}

TrSingleTile::TrSingleTile(u16 cell_x, u16 cell_z, u8 palette_kind, u16 row, bool dry_only)
    : TrTile(cell_x, cell_z, palette_kind, u8(row) | (dry_only ? 0 : 0x80)) {
}

void TrSingleTile::PregetPoints(i32 zoom, i32 rotation) const {
  PrepareSurfaceGeometry(zoom, rotation, 3);
}

i16 TrEdgeGroundHeights[2];

TrEdgeTile::TrEdgeTile(u16 cell_x, u16 cell_z, bool reverse_side, bool dry_only)
    : TrTile(cell_x, cell_z, u8(4 | (reverse_side ? 0x80 : 0) | (dry_only ? 0x40 : 0)), 0) {
}

// Select the original edge polygon strip from the two top and two bottom height orderings.
u16 TrClassifyEdge(const TrTilePoint *points) {
  u16 strip = points[0].y < points[1].y ? 3 : points[0].y > points[1].y ? 13 : 8;
  if (points[3].y < points[2].y)
    strip += 2;
  else if (points[3].y == points[2].y)
    ++strip;
  return strip;
}

// Restore complete private virtual104 effects for the four defined camera rotations.
// Retain separate ground-only top heights for the wall raster, even when visible tops include water.
void TrEdgeTile::PregetPoints(i32 zoom, i32 rotation) const {
  i32 half_width = i16(TrTileHalfWidths[zoom]);
  i32 left = rotation == 1 ? 0 : rotation == 3 ? -2 * half_width : -half_width;
  bool reverse = (kind & 0x80) != 0;
  if (!reverse)
    left += half_width;
  TrTile::screenPoint[0].x = TrTile::screenPoint[3].x = i16(left);
  TrTile::screenPoint[1].x = TrTile::screenPoint[2].x = i16(left + half_width);
  static const u32 first[4] = {2, 3, 0, 1}, second[4] = {1, 2, 3, 0};
  static const u32 dx[4] = {0, 0, 1, 1}, dz[4] = {0, 1, 1, 0};
  u32 edge = (rotation + u32(reverse)) & 3, corners[2] = {first[edge], second[edge]};
  TrRenderer *renderer = TrRendererSingleton;
  i16 scale = i16(-i32(i16(TrTileScales[zoom])));
  for (u32 i = 0; i < 2; ++i) {
    u32 cell_x = u32(x) + dx[corners[i]], cell_z = u32(z) + dz[corners[i]];
    u8 height = renderer->ground->columns[cell_x][cell_z];
    TrEdgeGroundHeights[i] = i16(i32(scale) * height);
    if (!(kind & 0x40)) {
      u8 water = renderer->water->columns[cell_x][cell_z];
      if (water > height)
        height = water;
    }
    TrTile::screenPoint[i].y = i16(i32(scale) * height);
  }
  i32 half_height = i32(TrTileHeights[zoom] >> 1);
  i32 shift = rotation == 0 ? 2 * half_height : rotation == 2 ? -2 * half_height : 0;
  for (u32 i = 0; i < 2; ++i) {
    TrTile::screenPoint[i].y = i16(TrTile::screenPoint[i].y + shift);
    TrEdgeGroundHeights[i] = i16(TrEdgeGroundHeights[i] + shift);
  }
  TrTile::screenPoint[2].y = TrTile::screenPoint[3].y = i16(shift);
  if (reverse) {
    TrTile::screenPoint[1].y = i16(TrTile::screenPoint[1].y + half_height);
    TrTile::screenPoint[2].y = i16(TrTile::screenPoint[2].y + half_height);
    TrEdgeGroundHeights[1] = i16(TrEdgeGroundHeights[1] + half_height);
  } else {
    TrTile::screenPoint[0].y = i16(TrTile::screenPoint[0].y + half_height);
    TrTile::screenPoint[3].y = i16(TrTile::screenPoint[3].y + half_height);
    TrEdgeGroundHeights[0] = i16(TrEdgeGroundHeights[0] + half_height);
  }
  if (!(kind & 0x40)) {
    TrTile::screenPoint[2].y = i16(TrTile::screenPoint[2].y - 4 * scale);
    TrTile::screenPoint[3].y = i16(TrTile::screenPoint[3].y - 4 * scale);
  }
  TrTileStrip = TrClassifyEdge(TrTile::screenPoint);
}

void TrEdgeTile::GetColor(GxBuffer *, u16, u16, i32, TrColorVertex *) const {
}

struct TrRasterEdge {
  const u8 *chain;
  u32 count, index;
  i32 next_y;
  f32 x, step_x, rgb[3], step_rgb[3];
};

static f32 TrRasterFloat(f64 value) {
  volatile f32 stored = f32(value);
  return stored;
}

// Reproduce the game's53-bit x87 nearest-even integer conversion for its finite, in-range interpolation values.
static i32 TrRasterRound(f64 value) {
  f64 integer = floor(value), fraction = value - integer;
  if (fraction > 0.5 || (fraction == 0.5 && fmod(integer, 2.0) != 0.0))
    integer += 1.0;
  return i32(integer);
}

// Begin the next live polygon-chain segment and load its two endpoint colors in RGB order.
static bool TrRasterStart(TrRasterEdge *edge, i32 y, const TrTilePoint *points, const TrColorVertex *colors) {
  if (y < edge->next_y || edge->index + 1 >= edge->count)
    return false;
  u32 first = edge->chain[edge->index], second = edge->chain[++edge->index];
  f64 reciprocal = 1.0 / (i32(points[second].y) - points[first].y);
  edge->step_x = TrRasterFloat((i32(points[second].x) - points[first].x) * reciprocal);
  edge->x = f32(points[first].x);
  for (u32 channel = 0; channel < 3; ++channel) {
    edge->step_rgb[channel] =
        TrRasterFloat((i32(colors[second].rgb[channel]) - colors[first].rgb[channel]) * reciprocal);
    edge->rgb[channel] = f32(colors[first].rgb[channel]);
  }
  edge->next_y = points[second].y;
  return true;
}

// Advance both geometric and RGB interpolation through the original float stores after one scanline.
static void TrRasterAdvance(TrRasterEdge *edge) {
  edge->x = TrRasterFloat(f64(edge->x) + edge->step_x);
  for (u32 channel = 0; channel < 3; ++channel)
    edge->rgb[channel] = TrRasterFloat(f64(edge->rgb[channel]) + edge->step_rgb[channel]);
}

// Form only the reached final pixel pointer, avoiding out-of-array intermediate pointers for clipped negative anchors.
static u16 *TrRasterPixel(u8 *data, i32 pitch, i32 origin_x, i32 origin_y, i32 x, i32 y) {
  i64 offset = i64(origin_x) + x + i64(pitch) * (i64(origin_y) + y);
  return reinterpret_cast<u16 *>(data + 2 * offset);
}

static u8 TrRasterBrighten(u8 value, i32 brightness) {
  i32 sum = i32(u32(brightness) + value);
  if (brightness >= 0)
    return sum >= 255 ? 255 : u8(sum);
  return sum <= 0 ? 0 : u8(sum);
}

// Edge callbacks occur even left of the clip; ordinary spans pack directly only for visible pixels.
void TrRasterTile(GxBuffer *device, const ScRect *rectangle, const ScRect *clip, i16 strip, const TrTilePoint *points,
    const TrColorVertex *colors, const u8 *noise, u32 seed, i32 rgb_noise, i32 brightness) {
  TrLockScope lock;
  u8 *data = static_cast<u8 *>(device->GetColorSurfaceBits());
  i32 pitch = i32(device->GetColorSurfaceStride()) / 2, origin_x = rectangle->left, origin_y = rectangle->top;
  i32 left = clip ? i32(u32(clip->left) - u32(origin_x)) : 0, top = clip ? i32(u32(clip->top) - u32(origin_y)) : 0,
      right = i32(u32(clip ? clip->right : rectangle->right) - u32(origin_x)),
      bottom = i32(u32(clip ? clip->bottom : rectangle->bottom) - u32(origin_y));
  const u8 *row = TrTileTopology[strip];
  for (u32 polygon = 0; polygon < row[0]; ++polygon) {
    const u8 *chain = row + 1 + 6 * polygon;
    u32 left_count = row[13 + polygon], right_count = row[15 + polygon];
    i32 y = points[chain[0]].y, end_y = points[(chain + left_count)[-1]].y;
    if (bottom < end_y)
      end_y = bottom;
    if (y >= end_y)
      continue;
    TrRasterEdge first, second;
    first.chain = chain;
    first.count = left_count;
    first.index = 0;
    first.next_y = y;
    second.chain = chain + left_count;
    second.count = right_count;
    second.index = 0;
    second.next_y = y;
    bool edge = false;
    u32 edge_width = 0, previous_width = 0;
    for (; y < end_y; ++y) {
      u32 sample = (seed + 143 * u32(y)) & 1023, previous_index = first.index;
      if (TrRasterStart(&first, y, points, colors) && brightness) {
        edge = !(polygon && first.chain[previous_index] == 1);
        if (edge) {
          i32 slope = TrRasterRound(first.step_x);
          edge_width = u32(slope < 0 ? -slope : slope) + 1;
        }
      }
      TrRasterStart(&second, y, points, colors);
      if (y >= top) {
        f64 width = f64(second.x) - first.x + 1.0;
        if (width > 0.0) {
          i32 x = TrRasterRound(first.x), endpoint = TrRasterRound(second.x);
          i32 end_x = endpoint < right ? endpoint : right;
          f64 reciprocal = 1.0 / width;
          f32 color[3], step[3];
          for (u32 channel = 0; channel < 3; ++channel) {
            color[channel] = first.rgb[channel];
            step[channel] = TrRasterFloat((f64(second.rgb[channel]) - first.rgb[channel]) * reciprocal);
          }
          u32 edge_remaining = 0;
          if (edge) {
            edge_remaining = previous_width > edge_width ? previous_width : edge_width;
            previous_width = edge_width;
          }
          for (; x < end_x; ++x) {
            u8 output[3];
            if (edge_remaining) {
              for (u32 channel = 0; channel < 3; ++channel)
                output[channel] = TrRasterBrighten(u8(TrRasterRound(color[channel])), brightness);
              u16 pixel;
              TrPackColor(&pixel, output[0], output[1], output[2]);
              if (x >= left)
                *TrRasterPixel(data, pitch, origin_x, origin_y, x, y) = pixel;
              if (noise)
                sample = (sample + (u8(rgb_noise) ? 3 : 1)) & 1023;
              --edge_remaining;
            } else {
              u8 texture[3] = {};
              if (noise) {
                texture[0] = noise[sample];
                sample = (sample + 1) & 1023;
                if (u8(rgb_noise)) {
                  texture[1] = noise[sample];
                  sample = (sample + 1) & 1023;
                  texture[2] = noise[sample];
                  sample = (sample + 1) & 1023;
                } else
                  texture[1] = texture[2] = texture[0];
              }
              if (x >= left) {
                for (u32 channel = 0; channel < 3; ++channel)
                  output[channel] = u8(TrRasterRound(color[channel]) + texture[channel]);
                TrPackColor(TrRasterPixel(data, pitch, origin_x, origin_y, x, y), output[0], output[1], output[2]);
              }
            }
            for (u32 channel = 0; channel < 3; ++channel)
              color[channel] = TrRasterFloat(f64(color[channel]) + step[channel]);
          }
        }
      }
      TrRasterAdvance(&first);
      TrRasterAdvance(&second);
    }
  }
  i32 y = points[1].y;
  if (brightness && y >= top && y < bottom) {
    i32 first = points[0].y == y ? points[0].x : points[1].x, last = points[2].y == y ? points[2].x : points[1].x;
    if (first < left)
      first = left;
    if (last > right)
      last = right;
    for (i32 x = first; x < last; ++x) {
      u16 *pixel = TrRasterPixel(data, pitch, origin_x, origin_y, x, y);
      u8 color[3];
      TrUnpackColor(*pixel, color, color + 1, color + 2);
      for (u32 channel = 0; channel < 3; ++channel)
        color[channel] = TrRasterBrighten(color[channel], brightness);
      TrPackColor(pixel, color[0], color[1], color[2]);
    }
  }
}

// Apply the relief helper's squared RGB distance replacement and signed DWORD-weighted transition.
// Report only a complete replacement so the caller can select the post-advance water-noise sample.
static bool TrRasterBlend(u8 *color, u32 match, i32 outer, i32 inner, u32 replacement, f32 reciprocal) {
  i32 distance = 0;
  for (u32 channel = 0; channel < 3; ++channel) {
    i32 delta = i32(color[channel]) - u8(match >> (8 * channel));
    distance += delta * delta;
  }
  if (distance >= outer)
    return false;
  if (distance <= inner) {
    for (u32 channel = 0; channel < 3; ++channel)
      color[channel] = u8(replacement >> (8 * channel));
    return true;
  }
  u32 first = u32(distance) - u32(inner), second = u32(outer) - u32(distance);
  for (u32 channel = 0; channel < 3; ++channel) {
    u32 word = color[channel] * first + u8(replacement >> (8 * channel)) * second;
    color[channel] = u8(i64(f64(i32(word)) * reciprocal));
  }
  return false;
}

// Inactive distance ranges delegate to the flat helper while retaining both original lock acquisitions.
void TrRasterRelief(GxBuffer *device, const ScRect *rectangle, const ScRect *clip, i16 strip, const TrTilePoint *points,
    const TrColorVertex *colors, const u8 *noise, u32 seed, i32 rgb_noise, i32 brightness, u32 match, i32 outer,
    i32 inner, u32 replacement, const u8 *replacement_noise) {
  TrLockScope lock;
  if (outer <= inner) {
    TrRasterTile(device, rectangle, clip, strip, points, colors, noise, seed, rgb_noise, brightness);
    return;
  }
  u8 *data = static_cast<u8 *>(device->GetColorSurfaceBits());
  i32 pitch = i32(device->GetColorSurfaceStride()) / 2, origin_x = rectangle->left, origin_y = rectangle->top;
  i32 left = clip ? i32(u32(clip->left) - u32(origin_x)) : 0, top = clip ? i32(u32(clip->top) - u32(origin_y)) : 0,
      right = i32(u32(clip ? clip->right : rectangle->right) - u32(origin_x)),
      bottom = i32(u32(clip ? clip->bottom : rectangle->bottom) - u32(origin_y));
  f32 blend_reciprocal = TrRasterFloat(1.0 / i32(u32(outer) - u32(inner)));
  const u8 *row = TrTileTopology[strip];
  for (u32 polygon = 0; polygon < row[0]; ++polygon) {
    const u8 *chain = row + 1 + 6 * polygon;
    u32 left_count = row[13 + polygon], right_count = row[15 + polygon];
    i32 y = points[chain[0]].y, end_y = points[(chain + left_count)[-1]].y;
    if (bottom < end_y)
      end_y = bottom;
    if (y >= end_y)
      continue;
    TrRasterEdge first, second;
    first.chain = chain;
    first.count = left_count;
    first.index = 0;
    first.next_y = y;
    second.chain = chain + left_count;
    second.count = right_count;
    second.index = 0;
    second.next_y = y;
    bool edge = false;
    u32 edge_width = 0;
    for (; y < end_y; ++y) {
      u32 sample = (seed + 143 * u32(y)) & 1023, previous_index = first.index;
      if (TrRasterStart(&first, y, points, colors) && brightness) {
        edge = !(polygon && first.chain[previous_index] == 1);
        if (edge) {
          i32 slope = TrRasterRound(first.step_x);
          edge_width = u32(slope < 0 ? -slope : slope) + 1;
        }
      }
      TrRasterStart(&second, y, points, colors);
      if (y >= top) {
        f64 width = f64(second.x) - first.x + 1.0;
        if (width > 0.0) {
          i32 x = TrRasterRound(first.x), endpoint = TrRasterRound(second.x);
          i32 end_x = endpoint < right ? endpoint : right;
          f64 reciprocal = 1.0 / width;
          f32 color[3], step[3];
          for (u32 channel = 0; channel < 3; ++channel) {
            color[channel] = first.rgb[channel];
            step[channel] = TrRasterFloat((f64(second.rgb[channel]) - first.rgb[channel]) * reciprocal);
          }
          u32 edge_remaining = 0;
          if (edge)
            edge_remaining = edge_width;
          for (; x < end_x; ++x) {
            u8 output[3];
            if (edge_remaining) {
              for (u32 channel = 0; channel < 3; ++channel)
                output[channel] = u8(TrRasterRound(color[channel]));
              TrRasterBlend(output, match, outer, inner, replacement, blend_reciprocal);
              for (u32 channel = 0; channel < 3; ++channel)
                output[channel] = TrRasterBrighten(output[channel], brightness);
              u16 pixel;
              TrPackColor(&pixel, output[0], output[1], output[2]);
              if (x >= left)
                *TrRasterPixel(data, pitch, origin_x, origin_y, x, y) = pixel;
              if (noise)
                sample = (sample + (u8(rgb_noise) ? 3 : 1)) & 1023;
              --edge_remaining;
            } else {
              u8 texture[3] = {};
              if (noise) {
                texture[0] = noise[sample];
                sample = (sample + 1) & 1023;
                if (u8(rgb_noise)) {
                  texture[1] = noise[sample];
                  sample = (sample + 1) & 1023;
                  texture[2] = noise[sample];
                  sample = (sample + 1) & 1023;
                } else
                  texture[1] = texture[2] = texture[0];
              }
              for (u32 channel = 0; channel < 3; ++channel)
                output[channel] = u8(TrRasterRound(color[channel]));
              if (TrRasterBlend(output, match, outer, inner, replacement, blend_reciprocal) && noise)
                texture[0] = texture[1] = texture[2] = replacement_noise[sample];
              if (x >= left) {
                for (u32 channel = 0; channel < 3; ++channel)
                  output[channel] = u8(output[channel] + texture[channel]);
                TrPackColor(TrRasterPixel(data, pitch, origin_x, origin_y, x, y), output[0], output[1], output[2]);
              }
            }
            for (u32 channel = 0; channel < 3; ++channel)
              color[channel] = TrRasterFloat(f64(color[channel]) + step[channel]);
          }
        }
      }
      TrRasterAdvance(&first);
      TrRasterAdvance(&second);
    }
  }
  i32 y = points[1].y;
  if (brightness && y >= top && y < bottom) {
    i32 first = points[0].y == y ? points[0].x : points[1].x, last = points[2].y == y ? points[2].x : points[1].x;
    if (first < left)
      first = left;
    if (last > right)
      last = right;
    for (i32 x = first; x < last; ++x) {
      u16 *pixel = TrRasterPixel(data, pitch, origin_x, origin_y, x, y);
      u8 color[3];
      TrUnpackColor(*pixel, color, color + 1, color + 2);
      for (u32 channel = 0; channel < 3; ++channel)
        color[channel] = TrRasterBrighten(color[channel], brightness);
      TrPackColor(pixel, color[0], color[1], color[2]);
    }
  }
}

struct TrWallEdge {
  const u8 *chain;
  u32 count, index;
  i32 next_y;
  f32 x, step_x, value, step_value;
};

// Begin the next scalar wall segment at the original single-precision interpolation stores.
// Each endpoint row is a genuine DWORD input; subtract with DWORD wrap before the original signed float conversion.
static void TrRasterWallStart(TrWallEdge *edge, i32 y, const TrTilePoint *points, const i32 *rows) {
  if (y < edge->next_y || edge->index + 1 >= edge->count)
    return;
  u32 first = edge->chain[edge->index], second = edge->chain[++edge->index];
  f64 reciprocal = 1.0 / (i32(points[second].y) - points[first].y);
  edge->step_x = TrRasterFloat((i32(points[second].x) - points[first].x) * reciprocal);
  edge->x = f32(points[first].x);
  edge->step_value = TrRasterFloat(i32(u32(rows[second]) - u32(rows[first])) * reciprocal);
  edge->value = f32(rows[first]);
  edge->next_y = points[second].y;
}

// Execute complete textured wall scan conversion with its real recursive lock and device getters.
// Noise packing occurs before left clipping and keeps one sample stream across scanlines and polygons.
// Noise-free spans retain double accumulation; noisy spans store each scalar step as float.
void TrRasterWall(GxBuffer *device, const ScRect *rectangle, const ScRect *clip, i16 strip, const TrTilePoint *points,
    const i32 *rows, const u16 *palette, const u8 *noise, u32 seed) {
  TrLockScope lock;
  u8 *data = static_cast<u8 *>(device->GetColorSurfaceBits());
  i32 pitch = i32(device->GetColorSurfaceStride()) / 2, origin_x = rectangle->left, origin_y = rectangle->top;
  i32 left = clip ? i32(u32(clip->left) - u32(origin_x)) : 0, top = clip ? i32(u32(clip->top) - u32(origin_y)) : 0,
      right = i32(u32(clip ? clip->right : rectangle->right) - u32(origin_x)),
      bottom = i32(u32(clip ? clip->bottom : rectangle->bottom) - u32(origin_y));
  const u8 *row = TrTileTopology[strip];
  u32 sample = seed;
  for (u32 polygon = 0; polygon < row[0]; ++polygon) {
    const u8 *chain = row + 1 + 6 * polygon;
    u32 left_count = row[13 + polygon], right_count = row[15 + polygon];
    i32 y = points[chain[0]].y, end_y = points[(chain + left_count)[-1]].y;
    if (bottom < end_y)
      end_y = bottom;
    if (y >= end_y)
      continue;
    TrWallEdge first = {chain, left_count, 0, y}, second = {chain + left_count, right_count, 0, y};
    for (; y < end_y; ++y) {
      TrRasterWallStart(&first, y, points, rows);
      TrRasterWallStart(&second, y, points, rows);
      if (y >= top) {
        f64 width = f64(second.x) - first.x + 1.0;
        if (width > 0.0) {
          i32 x = TrRasterRound(first.x), endpoint = TrRasterRound(second.x);
          i32 end_x = endpoint < right ? endpoint : right;
          f32 step = TrRasterFloat((f64(second.value) - first.value) * (1.0 / width));
          if (noise) {
            f32 value = first.value;
            for (; x < end_x; ++x) {
              u8 shade = noise[sample] ? 8 : 0;
              u16 packed;
              TrPackColor(&packed, shade, shade, shade);
              sample = (sample + 1) & 1023;
              if (x >= left)
                *TrRasterPixel(data, pitch, origin_x, origin_y, x, y) =
                    u16(packed + palette[u16(TrRasterRound(value))]);
              value = TrRasterFloat(f64(value) + step);
            }
          } else {
            f64 value = first.value;
            for (; x < end_x; ++x) {
              if (x >= left)
                *TrRasterPixel(data, pitch, origin_x, origin_y, x, y) = palette[u16(TrRasterRound(value))];
              value += step;
            }
          }
        }
      }
      first.x = TrRasterFloat(f64(first.x) + first.step_x);
      second.x = TrRasterFloat(f64(second.x) + second.step_x);
      first.value = TrRasterFloat(f64(first.value) + first.step_value);
      second.value = TrRasterFloat(f64(second.value) + second.step_value);
    }
  }
}
