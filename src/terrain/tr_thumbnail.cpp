#include "terrain/tr_thumbnail.h"

#include "city/api.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "graphics/image_codecs.h"
#include "resources/resource.h"
#include "terrain/api.h"
#include "terrain/tr_director.h"
#include "terrain/tr_dirt_bag.h"

// Terrain thumbnails reset to a fixed preview seed and paint their border with the original light color.
enum TrThumbnailDefault : u32 {
  kTrThumbnailDefaultSeed = 0x83010c56,
  kTrThumbnailBorderColor = 0xffffffec,
};

TrRandom TrThumbnailRandom(kScRandomSeedFromClock);

TrNeighborMap::TrNeighborMap()
    : terrain(nullptr), colors(nullptr), west(0), east(0), north(0), south(0), seed(kTrThumbnailDefaultSeed),
      references(0) {
}

TrNeighborMap::~TrNeighborMap() {
  Shutdown();
}

// Publish the original primary Unknown or thumbnail identity before acquiring its virtual reference. Unsupported
// interface IDs clear the output.
bool TrNeighborMap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, TrNeighborMap);
  HANDLE_QUERY_INTERFACE(IID_TrNeighborMap, TrNeighborMap);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment this receiver before acquiring its module reference, then reload the count after that callback.
u32 TrNeighborMap::AddRef() {
  ++references;
  TrGetDirector()->AddRef();
  return references;
}

// Retire a module reference before decrementing a nonzero count; a callback can alter the subsequent decrement.
// Zero destroys this actual thumbnail through its virtual destructor, including both retained interface releases.
u32 TrNeighborMap::Release() {
  if (references) {
    TrGetDirector()->RemoveRef();
    --references;
  }
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

// Acquire the original color resource, retire the temporary handle, then retain its still-live image and the terrain.
// Preserve original reinitialization overwrites and sample north/south/west/east salt edges through the live city.
u8 TrNeighborMap::Init(ScDirtBag *world) {
  if (!world)
    return 0;
  const GxColorType format(GxColorType::Color::RGB16);
  {
    TrImageHandle image(0x21, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    colors = image.Value();
  }
  if (!colors)
    return 0;
  colors->AddRef();
  terrain = world;
  world->AddRef();
  u32 width = terrain->VertexCountX(), height = terrain->VertexCountZ();
  EcLayer *pollution = TrGetCity()->PollutionLayer();
  if (pollution) {
    north = pollution->IsSaltWater(width >> 1, 0);
    south = pollution->IsSaltWater(width >> 1, height - 2);
    west = pollution->IsSaltWater(0, height >> 1);
    east = pollution->IsSaltWater(width - 2, height >> 1);
  } else {
    east = 0;
    west = 0;
    south = 0;
    north = 0;
  }
  seed = kTrThumbnailDefaultSeed;
  return 1;
}

void TrNeighborMap::Shutdown() {
  ScRelease(&terrain);
  ScRelease(&colors);
}

// Map the original signed height byte to the color-resource row, retaining its low-height and capped upper branches.
u32 TrNeighborMap::ConvertAltToColor(GxBuffer *, i8 height) {
  if (height <= 0)
    return colors->GetPixel(0, 3);
  i32 column = height >= 10 ? 10 * i32(height) / 256 + 5 : i32(height) / 2 + 1;
  if (column > 11)
    column = 11;
  return colors->GetPixel(column, 3);
}

static i8 TrThumbnailCornerHeight() {
  return i8(TrThumbnailRandom.RandomSint32RangeGaussianFast(-2, 10));
}

// A negative random height is compressed toward zero through the original signed-byte conversion and division.
static i8 TrThumbnailHeight(i8 first, i8 second, i32 radius) {
  i32 average = (i32(first) + second) / 2, low = average - radius, high = average + radius;
  if (low < -127)
    low = -127;
  if (high > 127)
    high = 127;
  if (low >= high - 2)
    return i8((low + high) / 2);
  i32 value = TrThumbnailRandom.RandomSint32RangeGaussianFast(low, high);
  if (u32(value) & 0x80)
    return i8(2 * i32(i8(value)) / 3);
  return i8(value);
}

// Fill only original FFFF midpoint holes, preserving corner-read order, random tie choices and quadrant traversal.
// Promote signed heights to DWORD pixels while retaining the original final-quadrant iteration order.
static void TrFillThumbnailMidpoint(GxBuffer *target, i32 left, i32 top, i32 right, i32 bottom) {
  while (left + 1 < right || top + 1 < bottom) {
    i32 middle_x = (right + left) / 2, middle_z = (top + bottom) / 2;
    if (((right + left) & 1) && TrThumbnailRandom.RandomUint32Uniform(2))
      ++middle_x;
    if (((top + bottom) & 1) && TrThumbnailRandom.RandomUint32Uniform(2))
      ++middle_z;
    i32 width = right - left, height = bottom - top, radius = (height >= width ? width : height) / 2;
    if (target->GetPixel(left, middle_z) == 0xffff) {
      i8 last = i8(target->GetPixel(left, bottom)), first = i8(target->GetPixel(left, top));
      target->SetPixel(left, middle_z, u32(TrThumbnailHeight(first, last, radius)));
    }
    if (target->GetPixel(right, middle_z) == 0xffff) {
      i8 last = i8(target->GetPixel(right, bottom)), first = i8(target->GetPixel(right, top));
      target->SetPixel(right, middle_z, u32(TrThumbnailHeight(first, last, radius)));
    }
    if (target->GetPixel(middle_x, top) == 0xffff) {
      i8 last = i8(target->GetPixel(right, top)), first = i8(target->GetPixel(left, top));
      target->SetPixel(middle_x, top, u32(TrThumbnailHeight(first, last, radius)));
    }
    if (target->GetPixel(middle_x, bottom) == 0xffff) {
      i8 last = i8(target->GetPixel(right, bottom)), first = i8(target->GetPixel(left, bottom));
      target->SetPixel(middle_x, bottom, u32(TrThumbnailHeight(first, last, radius)));
    }
    if (target->GetPixel(middle_x, middle_z) == 0xffff) {
      i8 first, last;
      if (TrThumbnailRandom.RandomUint32Uniform(2)) {
        last = i8(target->GetPixel(right, bottom));
        first = i8(target->GetPixel(left, top));
      } else {
        last = i8(target->GetPixel(right, top));
        first = i8(target->GetPixel(left, bottom));
      }
      target->SetPixel(middle_x, middle_z, u32(TrThumbnailHeight(first, last, radius)));
    }
    TrFillThumbnailMidpoint(target, left, top, middle_x, middle_z);
    TrFillThumbnailMidpoint(target, middle_x, top, right, middle_z);
    TrFillThumbnailMidpoint(target, left, middle_z, middle_x, bottom);
    left = middle_x;
    top = middle_z;
  }
}

// Restore the original member's four ordered corner seeds before filling the complete256-square map through pixel methods.
// Values other than the exact FFFF sentinel survive, including signed heights stored in their original low WORD form.
void TrNeighborMap::CreateLand(GxBuffer *target) {
  if (target->GetPixel(0, 0) == 0xffff)
    target->SetPixel(0, 0, u32(TrThumbnailCornerHeight()));
  if (target->GetPixel(0, 255) == 0xffff)
    target->SetPixel(0, 255, u32(TrThumbnailCornerHeight()));
  if (target->GetPixel(255, 0) == 0xffff)
    target->SetPixel(255, 0, u32(TrThumbnailCornerHeight()));
  if (target->GetPixel(255, 255) == 0xffff)
    target->SetPixel(255, 255, u32(TrThumbnailCornerHeight()));
  TrFillThumbnailMidpoint(target, 0, 0, 255, 255);
}

// Restore the complete256-square neighbor-map effects through the original buffer operations.
// Preserve the transposed86-square sample, salt wedges, midpoint fill and final signed-byte palette mapping.
u8 TrNeighborMap::DrawMap(GxBuffer *target) {
  if (!target)
    return 0;
  if (target->Width() < 256)
    return 0;
  if (target->Height() < 256)
    return 0;
  if (!target->Lock(GxLockFlags::Fill))
    return 0;
  target->Fill(0xffff, 0, 0, 256, 256, nullptr);
  target->Unlock(GxLockFlags::Fill);
  if (!target->Lock(GxLockFlags::GetSetPixel))
    return 0;
  u32 width = terrain->VertexCountX(), height = terrain->VertexCountZ();
  for (u32 row = 0; row < 86; ++row) {
    u32 x = (row * width) / 86;
    for (u32 column = 0; column < 86; ++column) {
      u32 z = (column * height) / 86;
      u8 dry, wet;
      terrain->GetVertexAltitudeDirt(x, z, &dry);
      terrain->GetVertexAltitudeWater(x, z, &wet);
      i32 level = i32(dry) - wet;
      if (level > 255)
        level = 255;
      if (level < 0)
        level = 0;
      target->SetPixel(i32(column) + 85, i32(row) + 85, u32(level));
    }
  }
  TrThumbnailRandom.Seed(seed);
  if (west) {
    for (i32 inset = 0; inset < 86; ++inset)
      for (i32 x = inset; x < 256 - inset; ++x)
        target->SetPixel(x, inset, kTrThumbnailBorderColor);
  } else {
    for (i32 x = 86; x < 172; ++x) {
      u32 pixel = target->GetPixel(x, 86);
      target->SetPixel(x, 0, pixel);
    }
  }
  if (east) {
    for (i32 inset = 0; inset < 86; ++inset)
      for (i32 x = inset; x < 256 - inset; ++x)
        target->SetPixel(x, 255 - inset, kTrThumbnailBorderColor);
  } else {
    for (i32 x = 86; x < 172; ++x) {
      u32 pixel = target->GetPixel(x, 169);
      target->SetPixel(x, 255, pixel);
    }
  }
  if (north) {
    for (i32 inset = 0; inset < 86; ++inset)
      for (i32 z = inset; z < 256 - inset; ++z)
        target->SetPixel(inset, z, kTrThumbnailBorderColor);
  } else {
    for (i32 z = 86; z < 172; ++z) {
      u32 pixel = target->GetPixel(86, z);
      target->SetPixel(0, z, pixel);
    }
  }
  if (south) {
    for (i32 inset = 0; inset < 86; ++inset)
      for (i32 z = inset; z < 256 - inset; ++z)
        target->SetPixel(255 - inset, z, kTrThumbnailBorderColor);
  } else {
    for (i32 z = 86; z < 172; ++z) {
      u32 pixel = target->GetPixel(169, z);
      target->SetPixel(255, z, pixel);
    }
  }
  CreateLand(target);
  colors->Lock(GxLockFlags::GetSetPixel);
  for (i32 x = 0; x < 256; ++x) {
    for (i32 z = 0; z < 256; ++z) {
      i8 level = i8(target->GetPixel(x, z));
      target->SetPixel(x, z, ConvertAltToColor(target, level));
    }
  }
  colors->Unlock(GxLockFlags::GetSetPixel);
  target->Unlock(GxLockFlags::GetSetPixel);
  return 1;
}
