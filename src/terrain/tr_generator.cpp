#include "terrain/tr_generator.h"

#include <math.h>

#include <new>

#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/thread.h"
#include "terrain/api.h"
#include "terrain/tr_director.h"
#include "terrain/tr_dirt_bag.h"

// Allocate a zero-filled X-major terrain byte plane using ordinary C++ storage instead of template allocator machinery.
static TrByteGrid *TrGeneratorByteGrid(u32 width, u32 height) {
  TrByteGrid *grid = new TrByteGrid{width, height, new u8 *[width]};
  for (u32 x = 0; x < width; ++x)
    grid->columns[x] = new u8[height]{};
  return grid;
}

// Allocate the source bitplane's X-major DWORD columns, including zero-filled padding bits in the last word.
static TrChangeGrid *TrGeneratorShoreGrid(u32 width, u32 height) {
  u32 words = (height >> 5) + ((height & 31) != 0);
  TrChangeGrid *grid = new TrChangeGrid{width, height, words, new u32 *[width]};
  for (u32 x = 0; x < width; ++x)
    grid->columns[x] = new u32[words]{};
  return grid;
}

// Retire a byte plane's actual owned columns before its pointer table and grid descriptor.
static void TrGeneratorDeleteGrid(TrByteGrid *grid) {
  for (u32 x = 0; x < grid->width; ++x)
    delete[] grid->columns[x];
  delete[] grid->columns;
  delete grid;
}

TrGenerator::TrGenerator()
    : bitmap_name(nullptr), ground(nullptr), water(nullptr), flora(nullptr), shore(nullptr), sea(0), difficulty(0),
      references(0) {
}

TrGenerator::~TrGenerator() {
  TrGenerator::Shutdown();
}

// Publish this receiver for the unknown or dirt-generator IID before making the original virtual AddRef call.
// Unsupported queries clear the caller's output and return false.
bool TrGenerator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, TrGenerator);
  HANDLE_QUERY_INTERFACE(IID_TrGenerator, TrGenerator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before acquiring the DLL director reference, then return the live count after that call.
u32 TrGenerator::AddRef() {
  ++references;
  TrGetDirector()->AddRef();
  return references;
}

// Remove the director reference before decrementing a nonzero object count.
u32 TrGenerator::Release() {
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

void TrGenerator::SetDifficulty(i32 value) {
  difficulty = value;
}

bool TrGenerator::IsReady() const {
  return ground && water && flora && shore;
}

// Shut down an already ready receiver through its original virtual method, then allocate cells+1 vertices per dimension.
// Publish three zero-filled byte grids followed by the zero-filled shore bitplane and return true.
bool TrGenerator::Init(u32 cells_x, u32 cells_z) {
  if (IsReady())
    Shutdown();
  width = cells_x + 1;
  height = cells_z + 1;
  ground = TrGeneratorByteGrid(width, height);
  water = TrGeneratorByteGrid(width, height);
  flora = TrGeneratorByteGrid(width, height);
  shore = TrGeneratorShoreGrid(width, height);
  return true;
}

// Retire each original plane and clear its field immediately, retaining dimensions, filename and scalar settings.
// Unlimited shares this void cleanup tail with the destructor; the SDK Boolean result is not defined by this body.
void TrGenerator::Shutdown() {
  if (ground) {
    TrGeneratorDeleteGrid(ground);
    ground = nullptr;
  }
  if (water) {
    TrGeneratorDeleteGrid(water);
    water = nullptr;
  }
  if (flora) {
    TrGeneratorDeleteGrid(flora);
    flora = nullptr;
  }
  if (shore) {
    for (u32 x = 0; x < shore->width; ++x)
      delete[] shore->columns[x];
    delete[] shore->columns;
    delete shore;
    shore = nullptr;
  }
}

// Remember a copied bitmap filename and its threshold without loading or altering any terrain planes.
// The source frees only the old string object, leaving its character allocation untouched.
bool TrGenerator::GenerateAltFromBMP8(const ScStringView *name, u8 level) {
  ::operator delete(bitmap_name);
  bitmap_name = new ScString(*name);
  threshold = level;
  return true;
}

// Replace the remembered filename through the same raw object retirement, preserving the previous threshold byte.
bool TrGenerator::GenerateAltFromBMP24(const ScStringView *name) {
  ::operator delete(bitmap_name);
  bitmap_name = new ScString(*name);
  return true;
}

// Publish the generated threshold, then complete height, water/shore and flora passes through the real target interface.
// Reload live dimensions and planes after each target call, ignoring setter results and preserving partial effects.
bool TrGenerator::ReflectMap(ScDirtBag *target) {
  if (!IsReady() || !target)
    return false;
  target->SetGlobalSeaLevel(threshold);
  for (u32 x = 0; x < width; ++x)
    for (u32 z = 0; z < height; ++z)
      target->SetVertexAltitude(x, z, ground->columns[x][z]);
  for (u32 x = 0; x < width; ++x)
    for (u32 z = 0; z < height; ++z) {
      bool marked = (shore->columns[x][z >> 5] & (1u << (z & 31))) != 0;
      target->SetWaterTable(x, z, water->columns[x][z], marked);
    }
  for (u32 x = 0; x < width; ++x)
    for (u32 z = 0; z < height; ++z)
      target->InitFloraLevel(x, z, flora->columns[x][z]);
  return true;
}

static i32 TrSubdivisionSpread(u32 amplitude) {
  return (i32)(amplitude - 1);
}

// Select between two captured endpoint heights using the original signed overlap or triangular random interval.
// Amplitudes below two return the first endpoint unchanged, before clamping or drawing any random value.
static i32 TrSubdivisionChooseTwo(u32 first, u32 second, u32 amplitude, i32 spread) {
  if (amplitude < 2)
    return (i32)first;
  u32 maximum = first >= second ? first : second, minimum = second >= first ? first : second;
  i32 lower = (i32)(maximum - (u32)spread), upper = (i32)(minimum + (u32)spread);
  i32 value = lower >= upper ? (i32)((u32)lower + (u32)upper) / 2
      : TrTerrainRandom.RandomSint32RangeGaussianFast(lower, (i32)((u32)upper + 1));
  if (value < 16)
    value = 16;
  if (value > 240)
    value = 240;
  return value;
}

// Select from the minimum/maximum envelope of the four captured edge midpoints, retaining original argument order.
// Preserve the same unclamped first-sample result for small amplitudes and the same wrapped signed interval arithmetic.
static i32 TrSubdivisionChooseFour(u32 a, u32 b, u32 c, u32 d, u32 amplitude, i32 spread) {
  if (amplitude < 2)
    return (i32)a;
  u32 maximum = a >= b ? a : b, other_maximum = c >= d ? c : d;
  u32 minimum = b >= a ? a : b, other_minimum = d >= c ? c : d;
  if (maximum < other_maximum)
    maximum = other_maximum;
  if (other_minimum < minimum)
    minimum = other_minimum;
  i32 lower = (i32)(maximum - (u32)spread), upper = (i32)(minimum + (u32)spread);
  i32 value = lower >= upper ? (i32)((u32)lower + (u32)upper) / 2
      : TrTerrainRandom.RandomSint32RangeGaussianFast(lower, (i32)((u32)upper + 1));
  if (value < 16)
    value = 16;
  if (value > 240)
    value = 240;
  return value;
}

// Fill zero midpoints from captured corners/edges, then recurse NW, SW, NE, SE with halved amplitude and depth.
void TrGenerator::AltitudeSubdivision(u32 left, u32 top, u32 right, u32 bottom, u32 amplitude, u32 depth) {
  for (;;) {
    u32 middle_x = (left + right) >> 1, middle_z = (top + bottom) >> 1;
    if ((right - left) & 1)
      middle_x += TrTerrainRandom.RandomUint32Uniform(2) != 0;
    if ((top - bottom) & 1)
      middle_z += TrTerrainRandom.RandomUint32Uniform(2) != 0;
    u8 top_left = ground->columns[left][top], bottom_left = ground->columns[left][bottom];
    u8 left_middle = ground->columns[left][middle_z], top_right = ground->columns[right][top];
    u8 bottom_right = ground->columns[right][bottom], right_middle = ground->columns[right][middle_z];
    u8 top_middle = ground->columns[middle_x][top], bottom_middle = ground->columns[middle_x][bottom];
    u8 center = ground->columns[middle_x][middle_z];
    i32 spread = TrSubdivisionSpread(amplitude);
    if (!left_middle) {
      left_middle = (u8)TrSubdivisionChooseTwo(top_left, bottom_left, amplitude, spread);
      ground->columns[left][middle_z] = left_middle;
    }
    if (!right_middle) {
      right_middle = (u8)TrSubdivisionChooseTwo(top_right, bottom_right, amplitude, spread);
      ground->columns[right][middle_z] = right_middle;
    }
    if (!top_middle) {
      top_middle = (u8)TrSubdivisionChooseTwo(top_left, top_right, amplitude, spread);
      ground->columns[middle_x][top] = top_middle;
    }
    if (!bottom_middle) {
      bottom_middle = (u8)TrSubdivisionChooseTwo(bottom_left, bottom_right, amplitude, spread);
      ground->columns[middle_x][bottom] = bottom_middle;
    }
    if (!center) {
      center = (u8)TrSubdivisionChooseFour(left_middle, right_middle, top_middle, bottom_middle, amplitude, spread);
      ground->columns[middle_x][middle_z] = center;
    }
    if (!depth)
      return;
    amplitude >>= 1;
    --depth;
    AltitudeSubdivision(left, top, middle_x, middle_z, amplitude, depth);
    AltitudeSubdivision(left, middle_z, middle_x, bottom, amplitude, depth);
    AltitudeSubdivision(middle_x, top, right, middle_z, amplitude, depth);
    left = middle_x;
    top = middle_z;
  }
}

// Find the first height bin reaching the original cumulative rank, preserving255 when the upper tail is exhausted.
static u8 TrGeneratorRank(const u32 *histogram, u32 begin, u32 target) {
  u32 total = 0;
  for (u32 value = begin; value < 256; ++value) {
    total += histogram[value];
    if (total >= target)
      return (u8)value;
  }
  return 255;
}

// Generate the original anchors, subdivided relief, water ranks, remap, coasts, river, shore and decoration in order.
bool TrGenerator::GenerateRandom(i32 seed, u8 control3, u8 control4, u8 control5, u8 flags) {
  TrTerrainRandom.Seed((u32)seed);
  TrByteGrid *cleared[2] = {ground, water};
  for (TrByteGrid *grid : cleared)
    for (u32 x = 0; x < grid->width; ++x)
      for (u32 z = 0; z < grid->height; ++z)
        grid->columns[x][z] = 0;
  for (u32 x = 0; x < shore->width; ++x)
    for (u32 word = 0; word < shore->words; ++word)
      shore->columns[x][word] = 0;
  for (u32 x = 0; x < flora->width; ++x)
    for (u32 z = 0; z < flora->height; ++z)
      flora->columns[x][z] = 0;
  static const u32 kTrLowAnchorMasks[8] = {0x1f, 0x1084210, 0x1f00000, 0x108421, 0, 0, 0x1000, 0};
  static const u32 kTrXMidpointMasks[8] = {0xf, 0, 0xf00000, 0, 0, 0, 0, 0};
  static const u32 kTrZMidpointMasks[8] = {0, 0x84210, 0, 0x8421, 0, 0, 0, 0};
  u32 low = 0, midpoint_x = 0, midpoint_z = 0;
  for (u32 bit = 0; bit < 8; ++bit) {
    if (flags & (1u << bit)) {
      low |= kTrLowAnchorMasks[bit];
      midpoint_x |= kTrXMidpointMasks[bit];
      midpoint_z |= kTrZMidpointMasks[bit];
    }
  }
  u32 cells_x = width - 1, cells_z = height - 1, quarter_x = cells_x >> 2, quarter_z = cells_z >> 2;
  u32 amplitude = cells_x < cells_z ? cells_x : cells_z, depth = amplitude >> 2;
  for (u32 index = 0; index < 25; ++index) {
    u32 x = (index % 5) * quarter_x, z = (index / 5) * quarter_z;
    u32 value = low & (1u << index) ? 96 - depth : 96;
    ground->columns[x][z] = (u8)TrSubdivisionChooseTwo(value, value, 16, TrSubdivisionSpread(16));
  }
  if (flags & 0x80) {
    u32 value = 2 * depth + 96;
    ground->columns[2 * quarter_x][2 * quarter_z] =
        (u8)TrSubdivisionChooseTwo(value, value, 16, TrSubdivisionSpread(16));
  }
  for (u32 index = 0; index < 25; ++index) {
    u32 x = (index % 5) * quarter_x, z = (index / 5) * quarter_z;
    if (midpoint_x & (1u << index))
      ground->columns[x + (quarter_x >> 1)][z] =
          (u8)(((u32)ground->columns[x][z] + ground->columns[x + quarter_x][z]) / 2);
    if (midpoint_z & (1u << index))
      ground->columns[x][z + (quarter_z >> 1)] =
          (u8)(((u32)ground->columns[x][z] + ground->columns[x][z + quarter_z]) / 2);
  }
  AltitudeSubdivision(0, 0, cells_x, cells_z, amplitude, 8);
  u32 histogram[256] = {};
  threshold = 255;
  for (u32 x = 0; x < width; ++x) {
    for (u32 z = 0; z < height; ++z) {
      u8 value = ground->columns[x][z];
      ++histogram[value];
      if (threshold >= value)
        threshold = (u8)(value - 1);
    }
  }
  u32 weight = 1;
  for (u32 bit = 0; bit < 4; ++bit)
    if (flags & (1u << bit))
      weight += 2;
  if (flags & 0x40)
    weight += 3;
  u32 area = width * height, control = control4 < 4 ? 4 : control4;
  u32 target = (weight * control * area) / 1536;
  if (control4 >= 128)
    target = (9 * area) / 10;
  if (!target)
    target = 1;
  u32 total = 0;
  for (u32 value = 0; value < 256; ++value) {
    total += histogram[value];
    if (total >= target) {
      threshold = (u8)value;
      for (u32 x = 0; x < water->width; ++x)
        for (u32 z = 0; z < water->height; ++z)
          water->columns[x][z] = threshold;
      break;
    }
  }
  f64 factor = 1.0 - sqrt((f64)control3 / ((flags & 0x80) ? 128.0 : 256.0) * 0.5);
  f64 first_rank = (f64)area * factor, third_rank = first_rank * factor;
  third_rank *= factor;
  u8 second = TrGeneratorRank(histogram, (u32)threshold + 1, (u32)(i64)first_rank);
  u8 third = TrGeneratorRank(histogram, (u32)second + 1, (u32)(i64)third_rank);
  u8 fourth = TrGeneratorRank(histogram, (u32)third + 1, (u32)(i64)(third_rank * factor));
  bool quantize = ChangeTerrainProfile((u8)(threshold + 2), second, third, fourth);
  FixShores(flags);
  CreateRivers(flags, control4);
  FindSaltWater(flags, control4);
  CreateFlora(control5);
  if (quantize) {
    for (u32 x = 0; x < width; ++x)
      for (u32 z = 0; z < height; ++z) {
        u8 value = ground->columns[x][z];
        if ((u32)value > (u32)threshold + 2)
          ground->columns[x][z] = (u8)(threshold + (((u32)value - threshold - 2) & 0xfe) + 2);
      }
  }
  return true;
}

// Remap heights through the original difficulty-dependent bands, wrapping each cumulative knot separately to a byte.
// Threshold order is retained as supplied; the result indicates whether the caller must perform even-step quantization.
bool TrGenerator::ChangeTerrainProfile(u8 first, u8 second, u8 third, u8 fourth) {
  static const i32 divisors[3][3] = {{12, 6, 3}, {10, 5, 3}, {6, 3, 2}};
  u32 profile = difficulty == 2 ? 1 : difficulty == 3 ? 2 : 0;
  const i32 *divisor = divisors[profile];
  u8 lower = (u8)(first + ((i32)second - first) / divisor[0]);
  u8 middle = (u8)(lower + ((i32)third - second) / divisor[1]);
  u8 upper = (u8)(middle + ((i32)fourth - third) / divisor[2]);
  for (u32 x = 0; x < width; ++x) {
    for (u32 z = 0; z < height; ++z) {
      u8 value = ground->columns[x][z];
      if (value < first)
        continue;
      if (value < second)
        value = (u8)(first + ((i32)value - first) / divisor[0]);
      else if (value < third)
        value = (u8)(lower + ((i32)value - second) / divisor[1]);
      else if (value < fourth)
        value = (u8)(middle + ((i32)value - third) / divisor[2]);
      else
        value = (u8)(upper - fourth + value);
      ground->columns[x][z] = value;
    }
  }
  return profile == 0;
}

// Lower flagged north/east/south/west strips in order, moving the strip width toward each original triangular draw.
void TrGenerator::FixShores(u8 flags) {
  for (u32 edge = 0; edge < 4; ++edge) {
    if (!(flags & (1u << edge)))
      continue;
    u32 length = edge & 1 ? height : width;
    i32 strip = 3;
    for (u32 position = 0; position < length; ++position) {
      u32 x = edge == 0 || edge == 2 ? position : edge == 1 ? width - 1 : 0;
      u32 z = edge == 1 || edge == 3 ? position : edge == 2 ? height - 1 : 0;
      if (ground->columns[x][z] <= threshold)
        continue;
      i32 target = TrTerrainRandom.RandomSint32RangeGaussian(0, 7);
      if (target > strip)
        ++strip;
      else if (target < strip)
        --strip;
      for (i32 remaining = strip; remaining > 0; --remaining) {
        u8 level = (u8)((i32)threshold - remaining + 3);
        if (ground->columns[x][z] > level)
          ground->columns[x][z] = level;
        if (edge == 0)
          ++z;
        else if (edge == 1)
          --x;
        else if (edge == 2)
          --z;
        else
          ++x;
      }
    }
  }
}

// Clear every shore DWORD before directional scans, marking through the fifth dry vertex and the width-based budget.
void TrGenerator::FindSaltWater(u8 flags, u8 control4) {
  for (u32 x = 0; x < shore->width; ++x)
    for (u32 word = 0; word < shore->words; ++word)
      shore->columns[x][word] = 0;
  for (u32 edge = 0; edge < 4; ++edge) {
    if (!(flags & (1u << edge)))
      continue;
    u32 length = edge & 1 ? height : width;
    for (u32 position = 0; position < length; ++position) {
      u32 distance = (width * control4) >> 8, dry = 5;
      u32 x = edge == 0 || edge == 2 ? position : edge == 1 ? width - 1 : 0;
      u32 z = edge == 1 || edge == 3 ? position : edge == 2 ? height - 1 : 0;
      while (distance && dry) {
        if ((i32)water->columns[x][z] < (i32)ground->columns[x][z] - 1)
          --dry;
        --distance;
        shore->columns[x][z >> 5] |= 1u << (z & 31);
        if (edge == 0)
          ++z;
        else if (edge == 1)
          --x;
        else if (edge == 2)
          --z;
        else
          ++x;
      }
    }
  }
}

// Scatter radial clusters with independent integer and angle draws, retaining existing decoration outside accepted land.
void TrGenerator::CreateFlora(u8 density) {
  u32 clusters = (width * height * density) >> 16;
  if (!clusters)
    return;
  u32 radius_bound = density >> 2;
  if (radius_bound <= 2)
    radius_bound = 2;
  for (u32 cluster = 0; cluster < clusters; ++cluster) {
    u32 center_x = TrTerrainRandom.RandomUint32Uniform(width), center_z = TrTerrainRandom.RandomUint32Uniform(height);
    u32 radius = (u32)TrTerrainRandom.RandomSint32RangeGaussianFast(0, (i32)radius_bound);
    u32 count = radius <= 1 ? 1 : (u32)TrTerrainRandom.RandomSint32RangeGaussianFast(0, (i32)(radius * radius));
    for (u32 sample = 0; sample < count; ++sample) {
      u32 distance = radius <= 1 ? 0 : TrTerrainRandom.RandomUint32Uniform(radius);
      f64 angle = TrTerrainRandom.RandomDoubleRangeUniform(0.0, 0x1.921ff2e48e8a7p+2);
      f64 offset = cos(angle) * (f64)distance;
      i32 x = (i32)(center_x + (u32)(i64)offset);
      offset = sin(angle) * (f64)distance;
      i32 z = (i32)(center_z + (u32)(i64)offset);
      if (x >= 0 && x < (i32)width && z >= 0 && z < (i32)height) {
        if (ground->columns[x][z] > water->columns[x][z]) {
          u32 bit = shore->columns[x][(u32)z >> 5] & (1u << (z & 31));
          flora->columns[x][z] = (u8)TrTerrainRandom.RandomSint32RangeGaussianRight(0, bit ? 128 : 256);
        }
      }
    }
  }
}

// Sample the cubic at1/count through1 in original arithmetic and accumulation order, truncating each coordinate.
// Read the live control points for every sample, including when the typed output overlaps the input points.
void TrSampleRiver(ScPoint *first, ScPoint *second, ScPoint *third, ScPoint *last, ScPoint *output, i32 count) {
  if (count <= 0)
    return;
  f64 step = 1.0 / count, t = step;
  for (i32 index = 0; index < count; ++index) {
    f64 square = t * t, cube = square * t, three_t = t * 3.0;
    f64 a = ((1.0 - three_t) + square * 3.0) - cube;
    f64 b = (three_t - square * 6.0) + cube * 3.0, c = square * 3.0 - cube * 3.0;
    f64 x = (f64)first->x * a;
    x += (f64)second->x * b;
    x += (f64)third->x * c;
    x += (f64)last->x * cube;
    output[index].x = (i32)(u32)(i64)x;
    f64 z = (f64)third->y * c;
    z += (f64)first->y * a;
    z += (f64)last->y * cube;
    z += (f64)second->y * b;
    output[index].y = (i32)(u32)(i64)z;
    t += step;
  }
}

// Lower the first cell before testing continuation, then step until bounds or the252 target limit stops the arm.
void TrGenerator::TraceDepth(i32 x, i32 z, i32 dx, i32 dz, i8 depth) {
  u8 target = (u8)(threshold - depth);
  do {
    u8 *value = &ground->columns[x][z];
    if (*value > target)
      *value = target;
    x = (i32)((u32)x + (u32)dx);
    z = (i32)((u32)z + (u32)dz);
    target = (u8)(target + (target >= threshold ? 1 : 2));
  } while (x >= 0 && z >= 0 && x < (i32)width && z < (i32)height && target < 252);
}

// Carve the original1024-point river after both requested control phases, with four jittered arms per distinct cell.
void TrGenerator::CreateRivers(u8 flags, u8 control4) {
  if (!(flags & 0x30))
    return;
  i32 depth = (i32)((u32)(i64)(sqrt((f64)width) * 0.5) * ((u32)control4 + 64)) / 128;
  if (depth >= threshold)
    depth = (i32)threshold - 1;
  ScPoint *samples = new ScPoint[1024];
  ScPoint first, second, third, last;
  i32 first_depth = depth, last_depth = depth;
  if (flags & 0x20) {
    first.x = TrTerrainRandom.RandomSint32RangeUniform((i32)(width >> 2), (i32)((3 * width) >> 2));
    first.y = 0;
    last.x = TrTerrainRandom.RandomSint32RangeUniform((i32)(width >> 2), (i32)((3 * width) >> 2));
    last.y = (i32)(height - 1);
    second.x = TrTerrainRandom.RandomSint32RangeUniform((i32)(width >> 3), (i32)((7 * width) >> 3));
    second.y = TrTerrainRandom.RandomSint32RangeUniform((i32)(height >> 3), (i32)((7 * height) >> 3));
    third.x = TrTerrainRandom.RandomSint32RangeUniform((i32)(width >> 3), (i32)((7 * width) >> 3));
    third.y = TrTerrainRandom.RandomSint32RangeUniform((i32)(height >> 3), (i32)((7 * height) >> 3));
    if (flags & 1) {
      first_depth = (i32)threshold - ground->columns[first.x][first.y];
      if (first_depth < depth)
        first_depth = depth;
    }
    if (flags & 4) {
      last_depth = (i32)threshold - ground->columns[last.x][last.y];
      if (last_depth < depth)
        last_depth = depth;
    }
  }
  if (flags & 0x10) {
    first.x = 0;
    first.y = TrTerrainRandom.RandomSint32RangeUniform((i32)(height >> 2), (i32)((3 * height) >> 2));
    last.x = (i32)(width - 1);
    last.y = TrTerrainRandom.RandomSint32RangeUniform((i32)(height >> 2), (i32)((3 * height) >> 2));
    second.x = TrTerrainRandom.RandomSint32RangeUniform((i32)(width >> 3), (i32)((7 * width) >> 3));
    second.y = TrTerrainRandom.RandomSint32RangeUniform((i32)(height >> 3), (i32)((7 * height) >> 3));
    third.x = TrTerrainRandom.RandomSint32RangeUniform((i32)(width >> 3), (i32)((7 * width) >> 3));
    third.y = TrTerrainRandom.RandomSint32RangeUniform((i32)(height >> 3), (i32)((7 * height) >> 3));
    if (flags & 8) {
      first_depth = (i32)threshold - ground->columns[first.x][first.y];
      if (first_depth < depth)
        first_depth = depth;
    }
    if (flags & 2) {
      last_depth = (i32)threshold - ground->columns[last.x][last.y];
      if (last_depth < depth)
        last_depth = depth;
    }
  }
  TrSampleRiver(&first, &second, &third, &last, samples, 1024);
  for (u32 index = 0; index < 1024; ++index) {
    if (index && samples[index].x == samples[index - 1].x && samples[index].y == samples[index - 1].y)
      continue;
    u32 interpolation = index * ((u32)last_depth - (u32)first_depth);
    u8 base = (u8)((u32)first_depth + (interpolation >> 10));
    TraceDepth(samples[index].x, samples[index].y, -1, 0, (i8)(base + TrTerrainRandom.RandomSint32RangeUniform(0, 3)));
    TraceDepth(samples[index].x, samples[index].y, 1, 0, (i8)(base + TrTerrainRandom.RandomSint32RangeUniform(0, 3)));
    TraceDepth(samples[index].x, samples[index].y, 0, -1, (i8)(base + TrTerrainRandom.RandomSint32RangeUniform(0, 3)));
    TraceDepth(samples[index].x, samples[index].y, 0, 1, (i8)(base + TrTerrainRandom.RandomSint32RangeUniform(0, 3)));
  }
  delete[] samples;
}

TrRandom TrTerrainRandom(kScRandomSeedFromClock);

TrRandom::TrRandom(u32 seed) {
  Seed(seed);
  normal_ready = 0;
}

// Store the same seed in both original streams, resolving the all-ones sentinel through one real clock query.
// Preserve the Gaussian-cache flag and cached double, matching the original void seed method.
void TrRandom::Seed(u32 seed) {
  if (seed == kScRandomSeedFromClock)
    seed = ScMultimediaMilliseconds();
  state = seed;
  real_state = seed;
}

// Advance the integer stream once and return the original high-word-derived DWORD without a range reduction.
u32 TrRandom::RandomUint32Uniform() {
  u64 product = (u64)state * kScRandomMultiplier + kScRandomIncrement;
  state = (u32)product;
  return (u32)(product >> 16);
}

// Advance the original multiply/add sequence and mix its high words. A zero bound returns without reading the seed.
u32 TrRandom::RandomUint32Uniform(u32 bound) {
  if (!bound)
    return 0;
  u32 next = state, value;
  do {
    u64 product = (u64)next * kScRandomMultiplier;
    u32 low = (u32)product, high = (u32)(product >> 32) << 16;
    next = low + kScRandomIncrement;
    high += 65535u + (next < low);
    value = (high & kScRandomRetainedHighWordMask) | (next >> 16);
    if (bound < kScRandomModuloThreshold) {
      value %= bound;
      break;
    }
    // The BSR-derived mask includes every set bit of value, leaving it unchanged even when value is zero.
  } while (value > bound);
  state = next;
  return value;
}

i32 TrRandom::RandomSint32RangeGaussian(i32 lower, i32 upper) {
  return RandomSint32RangeGaussianFast(lower, upper);
}

i32 TrRandom::RandomSint32RangeGaussianRight(i32 lower, i32 upper) {
  return RandomSint32RangeGaussianRightFast(lower, upper);
}

// Signed division truncates toward zero; endpoint addition and negation retain the original DWORD bit pattern.
i32 TrRandom::RandomSint32RangeGaussianFast(i32 lower, i32 upper) {
  i32 half = (i32)((u32)upper - (u32)lower) / 2;
  i32 first = (i32)RandomUint32Uniform((u32)half), selected = (i32)RandomUint32Uniform((u32)half);
  if (first < selected)
    selected = first;
  if (RandomUint32Uniform(2))
    selected = (i32)(0u - (u32)selected);
  return (i32)((u32)lower + (u32)selected + (u32)half);
}

// Only the integer seed advances; the independent real stream and Gaussian cache remain unchanged.
i32 TrRandom::RandomSint32RangeGaussianRightFast(i32 lower, i32 upper) {
  u32 width = u32(upper) - u32(lower);
  i32 first = i32(RandomUint32Uniform(width)), second = i32(RandomUint32Uniform(width));
  return i32(u32(lower) + u32(first < second ? first : second));
}

// Advance only the independent real seed, replacing zero before multiplication and using the exact source scale.
f64 TrRandom::RandomDoubleUniform() {
  if (!real_state)
    real_state = kScRandomFallbackRealSeed;
  real_state *= kScRandomRealMultiplier;
  return (f64)real_state * 0x1.0000000000007p-32;
}

f64 TrRandom::RandomDoubleRangeUniform(f64 lower, f64 upper) {
  return RandomDoubleUniform() * (upper - lower) + lower;
}

i32 TrRandom::RandomSint32RangeUniform(i32 lower, i32 upper) {
  return (i32)((u32)lower + RandomUint32Uniform((u32)upper - (u32)lower));
}
