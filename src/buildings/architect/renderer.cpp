#include "buildings/architect/renderer.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_general_buffer.h"

#ifdef _MSC_VER
#define BD_UNAVAILABLE_NOINLINE __declspec(noinline)
#else
#define BD_UNAVAILABLE_NOINLINE __attribute__((noinline))
#endif
// Stop explicitly when an Architect operation needs its unreconstructed portable 3D device.
BD_UNAVAILABLE_NOINLINE static i32 BdMissingPortableRenderer() {
  void (*volatile missing)(const char *) = ScNotImpl;
  missing("Architect SDL 3D renderer is not reconstructed");
#ifdef _MSC_VER
  __assume(0);
#else
  __builtin_unreachable();
#endif
}
// Keep the missing-code boundary indirect so whole-unit optimization preserves the unrelated caller paths.
static i32 (*volatile BdPortableRendererMissing)() = BdMissingPortableRenderer;

// The installed software renderer indexes this alpha-stipple matrix as y modulo four plus four times x modulo four.
static constexpr u8 kBdAlphaStipple[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};

// The driver reserves signed headroom for reciprocal depth and carries normalized texture coordinates in 20 bits.
static constexpr f32 kBdTextureCoordScale = 1048576.0f;
static constexpr f32 kBdTextureRhwScale = 2147483520.0f;
static constexpr f32 kBdTextureReciprocalScale = 140737488355328.0f;
static constexpr f32 kBdTextureNormalizedRhw = 0.95f;
static constexpr f32 kBdTextureZeroWReciprocal = 2097152.0f;
static constexpr f32 kBdTextureIntegerLimit = 9223372036854775808.0f;

enum BdTextureRasterFlag : u32 {
  kBdTextureDiffuseAlpha = 8,
  kBdTexturePrimary = 0x10,
  kBdTextureSecondary = 0x20,
  kBdTextureSourceAlpha = 0x40,
  kBdTextureSecondaryOpacity = 0x80,
  kBdTexturePrimaryOpacity = 0x100,
  kBdTextureModulate = 0x200,
  kBdTextureColorKey = 0x400,
  kBdTextureSignedAdd = 0x800,
};

struct BdRasterEdge {
  i32 x, whole, carry;
  i64 fraction, increment;
};

static i32 BdRasterCeil(f32 value) {
  return value > 0 ? static_cast<i32>(value + 0.99999988f) : static_cast<i32>(value);
}

// Reproduce an x87 division stored as a float under the original truncating rounding mode.
static f32 BdRasterDivide(f32 numerator, f32 denominator) {
  f32 result = numerator / denominator;
  f64 exact = static_cast<f64>(numerator) / denominator;
  return fabs(static_cast<f64>(result)) > fabs(exact) ? nextafterf(result, 0.0f) : result;
}

// Initialize the signed edge step and 31-bit accumulator; the long edge uses the original x87 truncating division.
static BdRasterEdge BdMakeRasterEdge(f32 x0, f32 y0, f32 x1, f32 y1, i32 row, bool long_edge) {
  f32 dx = x1 - x0, dy = y1 - y0;
  f32 slope = long_edge ? BdRasterDivide(dx, dy) : dx / dy;
  f32 start = (static_cast<f32>(row) - y0) * slope + x0;
  i32 whole = static_cast<i32>(slope);
  i32 floor = start < 0 ? static_cast<i32>(start - 0.99999988f) : static_cast<i32>(start);
  i64 fraction = static_cast<i64>((start - static_cast<f32>(floor)) * 2147483648.0f);
  i64 increment = static_cast<i64>((slope - static_cast<f32>(whole)) * 2147483648.0f);
  return {BdRasterCeil(start), whole, whole + (slope >= 0 ? 1 : -1), (fraction - 1) & 0x7fffffff, increment};
}

// Advance one edge exactly once, carrying its signed fractional step into the integer x coordinate.
static void BdAdvanceRasterEdge(BdRasterEdge *edge) {
  i64 next = edge->fraction + edge->increment;
  bool carry = next < 0 || next >= 0x80000000ll;
  edge->fraction = next & 0x7fffffff;
  edge->x += carry ? edge->carry : edge->whole;
}

// Blend one RGB565 destination with the software driver's eight-bit source-alpha factors and channel truncation.
static u16 BdBlendRgb565(u16 destination, u32 source) {
  u32 alpha = source >> 24, inverse = 255 - alpha;
  u32 red = (((source >> 16) & 255) * alpha + ((destination >> 11) << 3) * inverse) >> 8;
  u32 green = (((source >> 8) & 255) * alpha + (((destination >> 5) & 63) << 2) * inverse) >> 8;
  u32 blue = ((source & 255) * alpha + ((destination & 31) << 3) * inverse) >> 8;
  return static_cast<u16>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

// Apply the same source-alpha arithmetic to an RGB555 target, whose three destination channels are five bits each.
static u16 BdBlendRgb555(u16 destination, u32 source) {
  u32 alpha = source >> 24, inverse = 255 - alpha;
  u32 red = (((source >> 16) & 255) * alpha + (((destination >> 10) & 31) << 3) * inverse) >> 8;
  u32 green = (((source >> 8) & 255) * alpha + (((destination >> 5) & 31) << 3) * inverse) >> 8;
  u32 blue = ((source & 255) * alpha + ((destination & 31) << 3) * inverse) >> 8;
  return static_cast<u16>(((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3));
}

static u32 BdBlendArgb32(u32 destination, u32 source) {
  u32 alpha = source >> 24, inverse = 255 - alpha, result = 0;
  for (u32 shift = 0; shift < 32; shift += 8) {
    u32 channel = (((source >> shift) & 255) * alpha + ((destination >> shift) & 255) * inverse) >> 8;
    result |= channel << shift;
  }
  return result;
}

// Retain the original command arena across draws; flushes recycle its allocation cursor without clearing bytes.
// Packed numerical fields can become later primitive fields when differently sized headers and spans reuse storage.
// Target pointers remain native SDL state; no foreign addresses or pointer tokens are placed in this numerical arena.
class BdRasterCommands {
public:
  static constexpr u32 kCapacity = 4096, kPrimitiveSize = 96, kSpanSize = 64, kPartialThreshold = 512;

  // Allocate the original zero-initialized arena once for the lifetime of this native raster device.
  BdRasterCommands() : bytes(new u8[kCapacity]{}) {
  }

  // Release the arena only when its owning device is destroyed, preserving all intervening draw history.
  ~BdRasterCommands() {
    delete[] bytes;
  }

  BdRasterCommands(const BdRasterCommands &) = delete;
  BdRasterCommands &operator=(const BdRasterCommands &) = delete;

  // Start a command stream at the arena base without changing retained packet contents.
  void Begin() {
    cursor = primitive = reserved = next_span = 0;
  }

  // Reserve a triangle header after the same minimum-header-and-span capacity check as the source producer.
  void BeginTriangle() {
    if (cursor + kPrimitiveSize + kSpanSize > kCapacity)
      Begin();
    primitive = cursor;
    cursor += kPrimitiveSize;
    u16 count = 0;
    u32 next = 0;
    memcpy(bytes + primitive + 4, &count, sizeof(count));
    memcpy(bytes + primitive + 84, &next, sizeof(next));
  }

  // End a trapezoid by returning its unused reserved spans while retaining their old numerical contents.
  void EndTrap() {
    cursor -= reserved * kSpanSize;
    reserved = 0;
  }

  // Reserve the next span through the source's fit, truncate and partial-flush allocation rules.
  // The caller supplies the remaining trapezoid rows and the clipped walker's eight-span reservation preference.
  u32 AllocateSpan(u32 rows, bool clipped) {
    if (!reserved) {
      u32 count = clipped && rows > 8 ? 8 : rows;
      while (cursor + count * kSpanSize > kCapacity) {
        if (cursor + kPartialThreshold <= kCapacity) {
          count = (kCapacity - cursor) / kSpanSize;
          break;
        }
        u8 header[kPrimitiveSize];
        memcpy(header, bytes + primitive, sizeof(header));
        Begin();
        memcpy(bytes, header, sizeof(header));
        cursor = kPrimitiveSize;
        u16 spans = 0;
        u32 next = 0;
        memcpy(bytes + 4, &spans, sizeof(spans));
        memcpy(bytes + 84, &next, sizeof(next));
      }
      next_span = cursor;
      cursor += count * kSpanSize;
      reserved = count;
    }
    u32 span = next_span;
    next_span += kSpanSize;
    --reserved;
    u16 count;
    memcpy(&count, bytes + primitive + 4, sizeof(count));
    ++count;
    memcpy(bytes + primitive + 4, &count, sizeof(count));
    return span;
  }

  // Access the current packed numerical header for the recovered setup chain.
  u8 *Header() {
    return bytes + primitive;
  }

  // Access an allocated span's retained numerical payload without creating a foreign pointer transport.
  u8 *Span(u32 offset) {
    return bytes + offset;
  }

private:
  u8 *bytes;
  u32 cursor = 0, primitive = 0, reserved = 0, next_span = 0;
};

// Preserve the low 32 bits of the source's truncating qword conversion within its defined finite domain.
static i32 BdRasterInteger(f32 value) {
  if (!isfinite(value) || value < -kBdTextureIntegerLimit || value >= kBdTextureIntegerLimit)
    return BdPortableRendererMissing();
  return static_cast<i32>(static_cast<i64>(value));
}

// Load a retained packed integer through its actual bytes, without alignment or object-aliasing assumptions.
static i32 BdRasterRead(const u8 *record, u32 offset) {
  i32 value;
  memcpy(&value, record + offset, sizeof(value));
  return value;
}

static void BdRasterWrite(u8 *record, u32 offset, i32 value) {
  memcpy(record + offset, &value, sizeof(value));
}

// Retain the general loop's final reciprocal after a single floating span has consumed its pixel.
// Each such span restarts the predictor and takes the original bounded slow refinement branch.
static i32 BdRasterRefine(i32 q, i32 reciprocal) {
  i32 updated = reciprocal < 0 ? reciprocal >> 1 : reciprocal;
  for (u32 iteration = 0; iteration < 7; ++iteration) {
    i32 previous = updated, high = static_cast<i32>((static_cast<i64>(updated) * q) >> 32);
    i32 error = 65536 - high;
    while (error < 0)
      error = (error + 32768) >> 1;
    i32 correction = static_cast<i32>(static_cast<u32>(error) << 15);
    updated = static_cast<i32>(static_cast<u32>((static_cast<i64>(updated) * correction) >> 32) * 4);
    i64 change = static_cast<i64>(updated) - previous;
    if (change >= -32 && change <= 32)
      break;
  }
  return updated;
}

// The software renderer borrows the actual game buffer only while its scene lock is held.
class BdCpuRenderDevice {
public:
  // Borrow the renderer's already retained target; each scene owns its own SDL views and buffer lock.
  explicit BdCpuRenderDevice(GxBuffer *target) : target(target) {
  }

  // Release an unfinished scene and both retained texture bindings before the renderer drops its target reference.
  ~BdCpuRenderDevice() {
    Close();
    for (BdCpuTexture *texture : textures)
      if (texture)
        texture->Release();
  }

  // Wrap the live game pixels with an SDL software renderer and retain the original buffer lock for the scene.
  bool Open() {
    if (renderer || locked || !target->Lock(GxLockFlags::Bits))
      return false;
    locked = true;
    GxColorType format = target->GetColorType();
    SDL_PixelFormat pixel_format = SDL_PIXELFORMAT_UNKNOWN;
    if (format.color == GxColorType::Color::RGB555 && format.depth == 16)
      pixel_format = SDL_PIXELFORMAT_XRGB1555;
    else if (format.color == GxColorType::Color::RGB565 && format.depth == 16)
      pixel_format = SDL_PIXELFORMAT_RGB565;
    else if (format.color == GxColorType::Color::ARGB32 && format.depth == 32)
      pixel_format = SDL_PIXELFORMAT_ARGB8888;
    u32 width = target->Width(), height = target->Height(), pitch = target->GetColorSurfaceStride();
    if (pixel_format != SDL_PIXELFORMAT_UNKNOWN && width <= INT32_MAX && height <= INT32_MAX && pitch <= INT32_MAX)
      surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height), pixel_format,
          target->GetColorSurfaceBits(), static_cast<int>(pitch));
    if (surface)
      renderer = SDL_CreateSoftwareRenderer(surface);
    if (renderer && !SDL_SetRenderDrawBlendMode(renderer, blend)) {
      SDL_DestroyRenderer(renderer);
      renderer = nullptr;
    }
    if (!renderer) {
      Close();
      return false;
    }
    return true;
  }

  // Flush queued drawing and release both SDL views before unlocking the game buffer.
  bool Close() {
    bool result = true;
    if (renderer) {
      result = SDL_RenderPresent(renderer);
      SDL_DestroyRenderer(renderer);
      renderer = nullptr;
    }
    if (surface) {
      SDL_DestroySurface(surface);
      surface = nullptr;
    }
    if (locked) {
      bool unlocked = target->Unlock(GxLockFlags::Bits) != 0;
      if (unlocked)
        locked = false;
      result = unlocked && result;
    }
    return result;
  }

  // Apply the effective source-over or copy mode after either recovered alpha-enable or factor state changes.
  bool SetBlend(bool enabled, bool source_alpha) {
    blend = enabled && source_alpha ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    return !renderer || SDL_SetRenderDrawBlendMode(renderer, blend);
  }

  // Capture the world translation and viewport selected when the source creates its device.
  void SetView(u32 width, u32 height, const BdPoint &offset) {
    view_width = width;
    view_height = height;
    world_x = static_cast<f32>(-0.375 - static_cast<f64>(offset.x));
    world_y = static_cast<f32>(-0.375 - static_cast<f64>(offset.y));
    f64 angle = 3.1415927f;
    SetTextureView(static_cast<f32>(sin(angle)), static_cast<f32>(cos(angle)));
  }

  // Multiply independent platform matrices in the software driver's stored single-precision order.
  // Camera setup keeps the output separate from both input matrices.
  static void MultiplyTextureMatrices(f32 *out, const f32 *first, const f32 *second) {
    for (u32 row = 0; row < 4; ++row)
      for (u32 column = 0; column < 4; ++column)
        out[row * 4 + column] = ((first[row * 4] * second[column] + first[row * 4 + 1] * second[column + 4]) +
            first[row * 4 + 2] * second[column + 8]) + first[row * 4 + 3] * second[column + 12];
  }

  // Retain the actual axis rotation, normalized projection and captured world translation for texture drawing.
  // The platform view state remains independent of the original game's later mutable offset field.
  void SetTextureView(f32 sine, f32 cosine) {
    f32 view[16] = {}, projection[16] = {}, world[16] = {};
    view[0] = view[15] = 1;
    view[5] = view[10] = cosine;
    view[6] = sine;
    view[9] = -sine;
    view[14] = static_cast<f32>(view_width);
    f32 clip_height = static_cast<f32>((static_cast<f64>(view_height) / view_width) * 2);
    projection[0] = projection[10] = projection[11] = projection[15] = 1;
    projection[5] = 2.0f / clip_height;
    projection[8] = projection[9] = 0.5f;
    projection[14] = -1;
    world[0] = world[5] = world[10] = world[15] = 1;
    world[12] = world_x;
    world[13] = world_y;
    MultiplyTextureMatrices(texture_view, view, projection);
    MultiplyTextureMatrices(texture_clip, world, texture_view);
  }

  // Evaluate homogeneous depth from the retained combined matrix before reciprocal division.
  // The source frontend substitutes a finite reciprocal when transformed W is exactly zero.
  f32 TextureReciprocal(f32 x, f32 y, f32 z) const {
    const f32 *matrix = identity_world ? texture_view : texture_clip;
    f32 w = ((x * matrix[3] + y * matrix[7]) + z * matrix[11]) + matrix[15];
    return w != 0 ? 1.0f / w : kBdTextureZeroWReciprocal;
  }

  // Project z=1 vertices through the captured world and viewport rather than the later mutable offset field.
  SDL_FPoint Project(const BdColorVertex &vertex) const {
    f32 x = identity_world ? 0.0f : world_x, y = identity_world ? 0.0f : world_y;
    return {vertex.x + x + static_cast<f32>(view_width) * 0.5f + 0.5f,
        vertex.y + y + static_cast<f32>(view_height) * 0.5f + 0.5f};
  }

  // Project texture vertices through the retained source camera and the original normalized viewport.
  // Keep the combined-matrix stores and vertex-product order without changing ordinary color primitives.
  SDL_FPoint ProjectTexture(const BdColorVertex &vertex) const {
    const f32 *matrix = identity_world ? texture_view : texture_clip;
    f32 reciprocal = TextureReciprocal(vertex.x, vertex.y, vertex.z);
    f32 clip_x = ((vertex.y * matrix[4] + vertex.x * matrix[0]) + vertex.z * matrix[8]) + matrix[12];
    f32 clip_y = ((vertex.x * matrix[1] + vertex.y * matrix[5]) + vertex.z * matrix[9]) + matrix[13];
    f32 height = static_cast<f32>(view_height);
    return {(reciprocal * clip_x) * static_cast<f32>(view_width), (reciprocal * clip_y) * -height + height};
  }

  // Identify the integer line cases whose Direct3D coverage is recovered, including the bounded diagonal path.
  bool SupportsLine(const BdColorVertex *vertices) const {
    f32 dx = vertices[1].x - vertices[0].x, dy = vertices[1].y - vertices[0].y;
    if (dx != 0 && dy != 0 && fabsf(dx) != fabsf(dy))
      return false;
    for (u32 index = 0; index < 2; ++index) {
      const BdColorVertex &vertex = vertices[index];
      if (vertex.z != 1.0f || !isfinite(vertex.x) || !isfinite(vertex.y) || vertex.x != floorf(vertex.x) ||
          vertex.y != floorf(vertex.y))
        return false;
      SDL_FPoint point = Project(vertex);
      if (point.x < -32768 || point.x > 32768 || point.y < -32768 || point.y > 32768)
        return false;
      if (dx != 0 && dy != 0 && (point.x < 1 || point.x >= static_cast<f32>(view_width) || point.y < 0 ||
                                    point.y >= static_cast<f32>(view_height)))
        return false;
    }
    return true;
  }

  // Retain the original fan command stream, then submit its packed colors through the published SDL pixel path.
  bool DrawPolygon(const BdColorVertex *vertices, u32 count) {
    commands.Begin();
    BdDualVertex packet[4] = {};
    for (u32 index = 0; index < count; ++index)
      packet[index] = {vertices[index].x, vertices[index].y, vertices[index].z, vertices[index].color, 0, 0, 0, 0};
    BdDualVertex first[3] = {packet[count == 4 ? 1 : 0], packet[count == 4 ? 2 : 1], packet[count == 4 ? 0 : 2]};
    if (!DrawTextureTriangle(first, true))
      return false;
    if (count == 4) {
      BdDualVertex second[3] = {packet[2], packet[3], packet[0]};
      if (!DrawTextureTriangle(second, true))
        return false;
    }
    SDL_Vertex output[4] = {};
    for (u32 index = 0; index < count; ++index) {
      output[index].position = Project(vertices[index]);
      u32 color = vertices[index].color;
      output[index].color = {static_cast<f32>((color >> 16) & 255) / 255.0f,
          static_cast<f32>((color >> 8) & 255) / 255.0f, static_cast<f32>(color & 255) / 255.0f,
          static_cast<f32>((color >> 24) & 255) / 255.0f};
    }
    const int fan[] = {0, 1, 2, 0, 2, 3};
    return SDL_RenderGeometry(renderer, nullptr, output, static_cast<int>(count), fan, count == 3 ? 3 : 6);
  }

  // Retain the numerical line packets before the published projected flat-color SDL submission.
  bool DrawLine(const BdColorVertex *vertices) {
    if (!line_list)
      commands.Begin();
    BdDualVertex packet[2] = {{vertices[0].x, vertices[0].y, vertices[0].z, vertices[0].color, 0, 0, 0, 0},
        {vertices[1].x, vertices[1].y, vertices[1].z, vertices[1].color, 0, 0, 0, 0}};
    if (!DrawTextureLine(packet, true))
      return false;
    u32 color = vertices[0].color;
    SDL_FPoint first = Project(vertices[0]), second = Project(vertices[1]);
    first.x -= 1.0f;
    second.x -= 1.0f;
    return SDL_SetRenderDrawColor(renderer, static_cast<u8>(color >> 16), static_cast<u8>(color >> 8),
               static_cast<u8>(color), static_cast<u8>(color >> 24)) &&
        SDL_RenderLine(renderer, first.x, first.y, second.x, second.y);
  }

  // Retain the numerical line packets alongside the published 8.8 color, blend and stipple pixel stepping.
  bool DrawCpuLine(const BdColorVertex *vertices, bool stipple) {
    if (!line_list)
      commands.Begin();
    BdDualVertex packet[2] = {{vertices[0].x, vertices[0].y, vertices[0].z, vertices[0].color, 0, 0, 0, 0},
        {vertices[1].x, vertices[1].y, vertices[1].z, vertices[1].color, 0, 0, 0, 0}};
    if (!DrawTextureLine(packet, true))
      return false;
    SDL_FPoint first = Project(vertices[0]), second = Project(vertices[1]);
    i32 x0 = static_cast<i32>(floorf(first.x - 1)), y0 = static_cast<i32>(floorf(first.y));
    i32 x1 = static_cast<i32>(floorf(second.x - 1)), y1 = static_cast<i32>(floorf(second.y));
    bool gradient = vertices[0].color != vertices[1].color;
    i32 original_x = x0, original_y = y0;
    f32 attributes[4] = {}, increments[4] = {};
    if (gradient) {
      i32 fx0 = static_cast<i32>((first.x - 1) * 16 + 0.5f), fy0 = static_cast<i32>((first.y - 0.25f) * 16 + 0.5f);
      i32 fx1 = static_cast<i32>((second.x - 1) * 16 + 0.5f), fy1 = static_cast<i32>((second.y - 0.25f) * 16 + 0.5f);
      bool horizontal_major = abs(fx1 - fx0) >= abs(fy1 - fy0);
      i32 initial = horizontal_major ? fx0 : fy0, final = horizontal_major ? fx1 : fy1;
      i32 first_pixel = horizontal_major ? x0 : y0;
      f32 inverse = 16.0f / static_cast<f32>(abs(final - initial));
      f32 offset = static_cast<f32>(final >= initial ? 16 * first_pixel - initial : initial - 16 * first_pixel) * 0.0625f;
      if (offset < 0)
        offset = 0;
      for (u32 channel = 0; channel < 4; ++channel) {
        u32 shift = channel * 8;
        i32 start = static_cast<i32>((vertices[0].color >> shift) & 255);
        i32 end = static_cast<i32>((vertices[1].color >> shift) & 255);
        increments[channel] = static_cast<f32>((end - start) * 256) * inverse;
        attributes[channel] = increments[channel] * offset + static_cast<f32>(start * 256);
      }
    }
    if (y0 == y1) {
      if (y0 < 0 || y0 >= static_cast<i32>(view_height) || (x0 < 0 && x1 < 0) ||
          (x0 >= static_cast<i32>(view_width) && x1 >= static_cast<i32>(view_width)))
        return true;
      if (x0 < 0)
        x0 = 0;
      if (x1 < 0)
        x1 = 0;
      if (x0 >= static_cast<i32>(view_width))
        x0 = static_cast<i32>(view_width) - 1;
      if (x1 >= static_cast<i32>(view_width))
        x1 = static_cast<i32>(view_width) - 1;
    } else if (x0 == x1) {
      if (x0 < 0 || x0 >= static_cast<i32>(view_width) || (y0 < 0 && y1 < 0) ||
          (y0 >= static_cast<i32>(view_height) && y1 >= static_cast<i32>(view_height)))
        return true;
      if (y0 < 0)
        y0 = 0;
      if (y1 < 0)
        y1 = 0;
      if (y0 >= static_cast<i32>(view_height))
        y0 = static_cast<i32>(view_height) - 1;
      if (y1 >= static_cast<i32>(view_height))
        y1 = static_cast<i32>(view_height) - 1;
    }
    if (gradient) {
      i32 skipped = abs(x0 - original_x) + abs(y0 - original_y);
      for (i32 index = 0; index < skipped; ++index)
        for (u32 channel = 0; channel < 4; ++channel)
          attributes[channel] += increments[channel];
    }
    i32 dx = x1 == x0 ? 0 : x1 > x0 ? 1 : -1, dy = y1 == y0 ? 0 : y1 > y0 ? 1 : -1;
    i32 steps = x1 == x0 ? abs(y1 - y0) : abs(x1 - x0);
    if (!SDL_FlushRenderer(renderer))
      return false;
    u32 color = vertices[0].color;
    u32 pixel = SDL_MapSurfaceRGBA(surface, static_cast<u8>(color >> 16), static_cast<u8>(color >> 8),
        static_cast<u8>(color), static_cast<u8>(color >> 24));
    u32 bytes = surface->format == SDL_PIXELFORMAT_ARGB8888 ? 4 : 2;
    for (i32 index = 0; index <= steps; ++index) {
      i32 x = x0 + dx * index, y = y0 + dy * index;
      u32 current = color;
      if (gradient) {
        current = 0;
        for (u32 channel = 0; channel < 4; ++channel) {
          // The original line span floors negative channels at zero but wraps positive values to 16 bits.
          i32 fixed = static_cast<i32>(attributes[channel]);
          u16 value = static_cast<u16>(fixed < 0 ? 0 : fixed);
          current |= static_cast<u32>(value >> 8) << (8 * channel);
          attributes[channel] += increments[channel];
        }
      }
      if (x < 0 || y < 0 || x >= static_cast<i32>(view_width) || y >= static_cast<i32>(view_height) ||
          (stipple && (current >> 24) <= kBdAlphaStipple[(y & 3) | ((x & 3) << 2)]))
        continue;
      u32 current_pixel = gradient ? SDL_MapSurfaceRGBA(surface, static_cast<u8>(current >> 16),
          static_cast<u8>(current >> 8), static_cast<u8>(current), static_cast<u8>(current >> 24)) : pixel;
      u8 *address = static_cast<u8 *>(surface->pixels) + static_cast<usize>(y) * surface->pitch + bytes * x;
      if (bytes == 4) {
        u32 value = current_pixel;
        if (blend == SDL_BLENDMODE_BLEND) {
          memcpy(&value, address, sizeof(value));
          value = BdBlendArgb32(value, current);
        }
        memcpy(address, &value, sizeof(value));
      } else {
        u16 value = static_cast<u16>(current_pixel);
        if (blend == SDL_BLENDMODE_BLEND) {
          memcpy(&value, address, sizeof(value));
          value = surface->format == SDL_PIXELFORMAT_RGB565 ? BdBlendRgb565(value, current)
                                                           : BdBlendRgb555(value, current);
        }
        memcpy(address, &value, sizeof(value));
      }
    }
    return true;
  }

  // Retain the newly selected real asset storage before releasing the prior device binding.
  // The device keeps its texture references between scenes, independently of the asset's image lifetime.
  void BindTexture(u32 stage, BdCpuTexture *texture) {
    if (texture)
      texture->AddRef();
    if (textures[stage])
      textures[stage]->Release();
    textures[stage] = texture;
  }

  // Expand wrapped RGB channels by shifting and ARGB4444 opacity by repeating its four-bit alpha.
  u32 ReadTexturePixel(u32 stage, i32 x, i32 y) const {
    const BdCpuTexture *texture = textures[stage];
    x %= static_cast<i32>(texture->width);
    y %= static_cast<i32>(texture->height);
    if (x < 0)
      x += texture->width;
    if (y < 0)
      y += texture->height;
    u32 pixel = texture->pixels[static_cast<usize>(y) * texture->width + x];
    u32 opacity = texture_keyed && texture->color_key && pixel == (texture->rgb565 ? 0xf81fu : 0x7c1fu) ? 0 : 255;
    if (texture->alpha)
      return (opacity ? (pixel >> 12) * 17u << 24 : 0) | ((pixel & 0xf00) << 12) |
          ((pixel & 0xf0) << 8) | ((pixel & 15) << 4);
    u32 green_bits = texture->rgb565 ? 6u : 5u;
    return (opacity << 24) | (((pixel >> (green_bits + 5)) & 31) << 19) |
        (((pixel >> 5) & ((1u << green_bits) - 1)) << (16 - green_bits)) | ((pixel & 31) << 3);
  }

  // Apply the driver's half-texel linear-filter origin and two signed 16-bit interpolation steps per channel.
  // Point sampling retains its unshifted origin; interpolation happens before the stage color operation.
  u32 SampleTexture(u32 stage, i32 u, i32 v) const {
    const BdCpuTexture *texture = textures[stage];
    bool linear = linear_filter[stage];
    i64 scaled_u = static_cast<i64>(u) * texture->width - (linear ? 32768 : 0);
    i64 scaled_v = static_cast<i64>(v) * texture->height - (linear ? 32768 : 0);
    i32 x = static_cast<i32>(scaled_u >> 16), y = static_cast<i32>(scaled_v >> 16);
    u32 first = ReadTexturePixel(stage, x, y);
    if (!linear)
      return first;
    u32 second = ReadTexturePixel(stage, x + 1, y), third = ReadTexturePixel(stage, x, y + 1);
    u32 fourth = ReadTexturePixel(stage, x + 1, y + 1), result = 0;
    i32 fraction_u = static_cast<u32>(scaled_u) & 65535, fraction_v = static_cast<u32>(scaled_v) & 65535;
    for (u32 shift = 0; shift < 32; shift += 8) {
      i32 top = (first >> shift) & 255, bottom = (third >> shift) & 255;
      top += (fraction_u * (static_cast<i32>((second >> shift) & 255) - top)) >> 16;
      bottom += (fraction_u * (static_cast<i32>((fourth >> shift) & 255) - bottom)) >> 16;
      result |= static_cast<u32>(top + ((fraction_v * (bottom - top)) >> 16)) << shift;
    }
    return result;
  }

  // Apply the recovered stage operators, key stipple and blend factors to one actual target word.
  // Each rasterizer supplies its original diffuse color and the alpha position used by the stipple test.
  void ShadeTexturePixel(i32 x, i32 y, u8 *address, u32 diffuse, u32 stipple_alpha, const i32 *fixed) {
    const BdCpuTexture *texture = textures[0];
    bool rgb565 = surface->format == SDL_PIXELFORMAT_RGB565;
    // The installed driver selects C_TexBlend_Tex1_None for this diffuse-alpha stage operation.
    bool diffuse_only = texture_diffuse_only;
    u32 sample = diffuse_only ? diffuse : SampleTexture(0, fixed[0], fixed[1]);
    // The software driver initializes this color once and retains it when the second sampler disappears.
    if (!diffuse_only && texture_two_stage)
      secondary_sample = SampleTexture(1, fixed[2], fixed[3]);
    if (diffuse_only && texture_keyed &&
        stipple_alpha <= kBdAlphaStipple[(y & 3) | ((x & 3) << 2)])
      return;
    if (!diffuse_only && texture_keyed && texture->color_key &&
        (sample >> 24) <= kBdAlphaStipple[(y & 3) | ((x & 3) << 2)])
      return;
    // D3DTOP_ADDSIGNED adds each full diffuse byte before half-range subtraction and saturation.
    if (!diffuse_only && texture_signed_add) {
      u32 color = diffuse, shaded = sample & 0xff000000;
      for (u32 shift = 0; shift < 24; shift += 8) {
        i32 channel = static_cast<i32>((sample >> shift) & 255) +
            static_cast<i32>((color >> shift) & 255) - 128;
        channel = channel < 0 ? 0 : channel > 255 ? 255 : channel;
        shaded |= static_cast<u32>(channel) << shift;
      }
      sample = shaded;
    } else if (!diffuse_only && (texture_flags & kBdTextureModulate)) {
      u32 shaded = sample & 0xff000000;
      for (u32 shift = 0; shift < 24; shift += 8)
        shaded |= ((((sample >> shift) & 255) * ((diffuse >> shift) & 255)) >> 8) << shift;
      sample = shaded;
    }
    if (!diffuse_only && texture_dual_combiner) {
      u32 secondary = secondary_sample;
      u32 combined = secondary & 0xff000000;
      i32 alpha = secondary >> 24;
      for (u32 shift = 0; shift < 24; shift += 8) {
        i32 background = (sample >> shift) & 255, foreground = (secondary >> shift) & 255;
        combined |= static_cast<u32>(background + ((alpha * (foreground - background)) >> 8)) << shift;
      }
      sample = combined;
    }
    if (texture_blend) {
      // RGB formats retain diffuse alpha even when the stage selects texture alpha.
      bool stored_alpha = texture_dual_combiner || (texture && texture->alpha && texture_alpha_from_texture);
      u32 alpha = !diffuse_only && stored_alpha ? sample >> 24 : diffuse >> 24;
      u16 destination;
      memcpy(&destination, address, sizeof(destination));
      if (texture_blend == 1) {
        sample = (sample & 0xffffff) | (alpha << 24);
        u16 blended = rgb565 ? BdBlendRgb565(destination, sample) : BdBlendRgb555(destination, sample);
        memcpy(address, &blended, sizeof(blended));
        return;
      }
      u32 green_bits = rgb565 ? 6u : 5u;
      u32 background = (((destination >> (green_bits + 5)) & 31) << 19) |
          (((destination >> 5) & ((1u << green_bits) - 1)) << (16 - green_bits)) | ((destination & 31) << 3);
      u32 blended = 0;
      for (u32 shift = 0; shift < 24; shift += 8) {
        u32 channel = ((((sample >> shift) & 255) + ((background >> shift) & 255)) * alpha) >> 8;
        blended |= (channel > 255 ? 255 : channel) << shift;
      }
      sample = blended;
    }
    u16 pixel = static_cast<u16>(((sample & 0xf80000) >> (rgb565 ? 8 : 9)) |
        ((sample & (rgb565 ? 0xfc00 : 0xf800)) >> (rgb565 ? 5 : 6)) | ((sample >> 3) & 31));
    memcpy(address, &pixel, sizeof(pixel));
  }

  // Apply the original four-bit endpoint diamond test, including the equal-slope tie quadrants.
  static bool InLineDiamond(i32 x, i32 y, bool diagonal, bool positive) {
    i32 distance = abs(x) + abs(y);
    if (distance < 8)
      return true;
    if ((x == (positive ? -8 : 8) && !y) || (y == 8 && !x))
      return true;
    if (!diagonal || distance != 8)
      return false;
    return positive ? x < 0 && y > 0 : x > 0 && y > 0;
  }

  // Walk textured line pixels using the driver's four-bit endpoints and signed 31-bit minor-coordinate accumulator.
  // Advance reciprocal depth and perspective UVs as floats; convert each visible point through its actual reciprocal.
  bool DrawTextureLine(const BdDualVertex *vertices, bool retain_only = false) {
    SDL_FPoint points[2];
    for (u32 index = 0; index < 2; ++index) {
      const BdDualVertex &vertex = vertices[index];
      BdColorVertex position{vertex.x, vertex.y, vertex.z, vertex.color};
      points[index] = ProjectTexture(position);
    }
    i32 fx[2], fy[2];
    for (u32 index = 0; index < 2; ++index) {
      fx[index] = static_cast<i32>(points[index].x * 16 + 0.5f);
      fy[index] = static_cast<i32>(points[index].y * 16 + 0.5f);
    }
    i32 dx = fx[1] - fx[0], dy = fy[1] - fy[0];
    if (!dx && !dy)
      return true;
    bool horizontal = abs(dx) >= abs(dy), diagonal = abs(dx) == abs(dy);
    bool positive = diagonal && ((dx ^ dy) >= 0);
    i32 px[2], py[2];
    for (u32 index = 0; index < 2; ++index) {
      px[index] = (fx[index] + 7 + positive) >> 4;
      py[index] = (fy[index] + 7) >> 4;
    }
    i32 initial = horizontal ? fx[0] : fy[0], final = horizontal ? fx[1] : fy[1];
    i32 first = horizontal ? px[0] : py[0], last = horizontal ? px[1] : py[1];
    i32 direction = final >= initial ? 1 : -1, minor_direction = (horizontal ? dy : dx) >= 0 ? 1 : -1;
    if (!InLineDiamond(fx[0] - 16 * px[0], fy[0] - 16 * py[0], diagonal, positive) &&
        direction * (initial - 16 * first) >= 0)
      first += direction;
    if (!InLineDiamond(fx[1] - 16 * px[1], fy[1] - 16 * py[1], diagonal, positive) &&
        direction * (16 * last - final) >= 0)
      last -= direction;
    i32 count = direction * (last - first) + 1;
    if (count <= 0)
      return true;
    f32 inverse = 16.0f / static_cast<f32>(abs(final - initial));
    f32 raw_offset = static_cast<f32>(direction * (16 * first - initial)) * 0.0625f;
    f32 slope = (static_cast<f32>(horizontal ? dy : dx) * inverse) * 0.0625f;
    f32 minor_at = raw_offset * slope + static_cast<f32>(horizontal ? fy[0] + 7 : fx[0] + 7 + positive) * 0.0625f;
    i32 minor = static_cast<i32>(minor_at < 0 ? minor_at - 0.99999988f : minor_at);
    u32 fraction = static_cast<u32>(static_cast<i64>((minor_at - static_cast<f32>(minor)) * 2147483648.0f));
    u32 fraction_step = static_cast<u32>(static_cast<i64>(slope * 2147483648.0f));
    i32 x = horizontal ? first : minor, y = horizontal ? minor : first;
    f32 offset = raw_offset < 0 ? 0 : raw_offset;
    const f32 *matrix = identity_world ? texture_view : texture_clip;
    f32 depths[2];
    for (u32 index = 0; index < 2; ++index) {
      const BdDualVertex &vertex = vertices[index];
      f32 clip_z = ((vertex.x * matrix[2] + vertex.y * matrix[6]) + vertex.z * matrix[10]) + matrix[14];
      depths[index] = clip_z * TextureReciprocal(vertex.x, vertex.y, vertex.z);
    }
    f32 z_step = ((depths[1] - depths[0]) * kBdTextureRhwScale) * inverse;
    f32 z = z_step * offset + depths[0] * kBdTextureRhwScale;
    f32 rhw = TextureReciprocal(vertices[0].x, vertices[0].y, vertices[0].z);
    f32 end_rhw = TextureReciprocal(vertices[1].x, vertices[1].y, vertices[1].z);
    f32 largest = rhw > end_rhw ? rhw : end_rhw;
    if (largest > 1 || rhw < 0 || end_rhw < 0) {
      f32 normalization = kBdTextureNormalizedRhw / largest;
      rhw *= normalization;
      end_rhw *= normalization;
    }
    f32 q_step = ((end_rhw - rhw) * kBdTextureRhwScale) * inverse;
    f32 q = q_step * offset + rhw * kBdTextureRhwScale;
    f32 coordinates[4][2] = {{vertices[0].u, vertices[1].u}, {vertices[0].v, vertices[1].v},
        {vertices[0].second_u, vertices[1].second_u}, {vertices[0].second_v, vertices[1].second_v}};
    f32 uv[4], uv_step[4], colors[4], color_step[4];
    u32 coordinate_count = retain_only || texture_diffuse_only ? 0 : texture_two_stage ? 4 : 2;
    for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
      f32 start = texture_diffuse_only ? 0 : coordinates[coordinate][0];
      f32 end = texture_diffuse_only ? 0 : coordinates[coordinate][1];
      f32 initial_uv = start * rhw, final_uv = ((end - start) + start) * end_rhw;
      uv_step[coordinate] = ((final_uv - initial_uv) * kBdTextureCoordScale) * inverse;
      // The original second coordinate pair omits the initial 20-bit scaling, while its increment is scaled.
      uv[coordinate] = uv_step[coordinate] * offset + (coordinate < 2 ? initial_uv * kBdTextureCoordScale : initial_uv);
    }
    for (u32 channel = 0; channel < 4; ++channel) {
      u32 shift = channel * 8;
      i32 start = (vertices[0].color >> shift) & 255, end = (vertices[1].color >> shift) & 255;
      color_step[channel] = static_cast<f32>((end - start) * 256) * inverse;
      colors[channel] = color_step[channel] * offset + static_cast<f32>(start * 256);
    }
    if (!SDL_FlushRenderer(renderer))
      return false;
    if (!line_list || !line_header) {
      commands.BeginTriangle();
      memset(commands.Header(), 0, BdRasterCommands::kPrimitiveSize);
      line_header = true;
    }
    bool first_emitted = true;
    for (i32 index = 0; index < count; ++index) {
      if (x >= 0 && y >= 0 && x < static_cast<i32>(view_width) && y < static_cast<i32>(view_height)) {
        u8 *packet = commands.Span(commands.AllocateSpan(static_cast<u32>(count - index), false));
        i32 stored_z = (first_emitted || index + 1 == count) && z < 0 ? 0 : BdRasterInteger(z);
        first_emitted = false;
        BdRasterWrite(packet, 12, stored_z);
        BdRasterWrite(packet, 60, 0);
        if (coordinate_count) {
          BdRasterWrite(packet, 8, 0);
          BdRasterWrite(packet, 20, BdRasterInteger(q));
          f32 original_reciprocal = (kBdTextureRhwScale / q) * 65536.0f;
          BdRasterWrite(packet, 16, BdRasterRefine(BdRasterInteger(q), BdRasterInteger(original_reciprocal)));
        }
        for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
          u32 field = coordinate < 2 ? 24 + coordinate * 4 : 48 + (coordinate - 2) * 4;
          BdRasterWrite(packet, field, BdRasterInteger(uv[coordinate]));
        }
        i32 reciprocal = 0;
        if (coordinate_count) {
          f32 value = kBdTextureReciprocalScale / q;
          if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return BdPortableRendererMissing() != 0;
          reciprocal = static_cast<i32>(value);
        }
        i32 fixed[4];
        for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
          f32 value = uv[coordinate];
          if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return BdPortableRendererMissing() != 0;
          fixed[coordinate] = static_cast<i32>((static_cast<i64>(static_cast<i32>(value)) * reciprocal) >> 20);
        }
        u32 diffuse = 0;
        for (u32 channel = 0; channel < 4; ++channel) {
          f32 value = colors[channel];
          if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return BdPortableRendererMissing() != 0;
          i32 integer = static_cast<i32>(value);
          u16 color = static_cast<u16>(integer < 0 ? 0 : integer);
          memcpy(packet + 32 + channel * 2, &color, sizeof(color));
          diffuse |= static_cast<u32>(color >> 8) << (channel * 8);
        }
        u8 *address = static_cast<u8 *>(surface->pixels) + static_cast<usize>(y) * surface->pitch + 2 * x;
        if (!retain_only)
          ShadeTexturePixel(x, y, address, diffuse, diffuse >> 24, fixed);
      }
      u32 next = fraction + fraction_step;
      bool carry = (next & 0x80000000u) != 0;
      fraction = carry ? next & 0x7fffffffu : next;
      x += horizontal ? direction : carry ? minor_direction : 0;
      y += horizontal ? (carry ? minor_direction : 0) : direction;
      z += z_step;
      q += q_step;
      for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate)
        uv[coordinate] += uv_step[coordinate];
      for (u32 channel = 0; channel < 4; ++channel)
        colors[channel] += color_step[channel];
    }
    commands.EndTrap();
    return true;
  }

  // Dispatch one- or two-coordinate line, triangle and fan packets with their original attribute ordering.
  // Decode source and target layouts independently; near-plane and out-of-range raster modes stay explicit.
  bool DrawTexturePrimitive(const BdDualVertex *input, u32 primitive) {
    BdCpuTexture *texture = textures[0];
    if (!surface || (surface->format != SDL_PIXELFORMAT_RGB565 && surface->format != SDL_PIXELFORMAT_XRGB1555))
      return BdPortableRendererMissing() != 0;
    u32 count = primitive == 1 ? 2 : primitive == 2 ? 3 : 4;
    BdDualVertex vertices[4];
    for (u32 index = 0; index < count; ++index) {
      const BdDualVertex &source = input[index];
      vertices[index] = {source.x, source.y, source.z, source.color, texture_diffuse_only ? 0 : source.u,
          texture_diffuse_only ? 0 : source.v, texture_diffuse_only || !texture_two_stage ? 0 : source.second_u,
          texture_diffuse_only || !texture_two_stage ? 0 : source.second_v};
      const BdDualVertex &vertex = vertices[index];
      if (!isfinite(vertex.x) || !isfinite(vertex.y) || !isfinite(vertex.z))
        return BdPortableRendererMissing() != 0;
      f32 reciprocal = TextureReciprocal(vertex.x, vertex.y, vertex.z);
      if (!isfinite(reciprocal) || reciprocal == 0)
        return BdPortableRendererMissing() != 0;
      BdColorVertex position{vertex.x, vertex.y, vertex.z, vertex.color};
      SDL_FPoint point = ProjectTexture(position);
      if (!isfinite(point.x) || !isfinite(point.y) || point.x < -4095 || point.y < -4095 ||
          point.x > static_cast<f32>(view_width) + 4096 || point.y > static_cast<f32>(view_height) + 4096)
        return BdPortableRendererMissing() != 0;
      if (!texture_diffuse_only) {
        if (!isfinite(vertex.u) || !isfinite(vertex.v) || fabsf(vertex.u * static_cast<f32>(texture->width)) > 32768 ||
            fabsf(vertex.v * static_cast<f32>(texture->height)) > 32768)
          return BdPortableRendererMissing() != 0;
        if (texture_two_stage &&
            (!isfinite(vertex.second_u) || !isfinite(vertex.second_v) ||
                fabsf(vertex.second_u * static_cast<f32>(textures[1]->width)) > 32768 ||
                fabsf(vertex.second_v * static_cast<f32>(textures[1]->height)) > 32768))
          return BdPortableRendererMissing() != 0;
      }
    }
    commands.Begin();
    if (primitive == 1)
      return DrawTextureLine(vertices);
    BdDualVertex first[3] = {
        vertices[primitive == 3 ? 1 : 0], vertices[primitive == 3 ? 2 : 1], vertices[primitive == 3 ? 0 : 2]};
    bool result = renderer && DrawTextureTriangle(first);
    if (primitive == 3 && result) {
      BdDualVertex second[3] = {vertices[2], vertices[3], vertices[0]};
      result = DrawTextureTriangle(second);
    }
    return result;
  }

  // Rasterize wrapped textures with the original integer spans or successive floating-attribute spans.
  // Retain Z/q/UV packets through original arena reuse; native target pointers stay outside the numerical storage.
  bool DrawTextureTriangle(const BdDualVertex *input, bool retain_only = false) {
    SDL_FPoint points[3];
    f32 coordinates[4][3];
    u32 coordinate_count = retain_only || texture_diffuse_only ? 0 : texture_two_stage ? 4 : 2;
    for (u32 index = 0; index < 3; ++index) {
      const BdDualVertex &vertex = input[index];
      BdColorVertex position{vertex.x, vertex.y, vertex.z, vertex.color};
      points[index] = ProjectTexture(position);
      coordinates[0][index] = texture_diffuse_only ? 0 : vertex.u;
      coordinates[1][index] = texture_diffuse_only ? 0 : vertex.v;
      coordinates[2][index] = texture_diffuse_only ? 0 : vertex.second_u;
      coordinates[3][index] = texture_diffuse_only ? 0 : vertex.second_v;
    }
    f32 dx1 = points[1].x - points[0].x, dy1 = points[1].y - points[0].y;
    f32 dx2 = points[2].x - points[0].x, dy2 = points[2].y - points[0].y;
    f32 area = dx1 * dy2 - dy1 * dx2;
    if (area <= 0)
      return true;
    u32 order[3] = {0, 1, 2};
    if (points[1].y < points[0].y) {
      if (points[2].y <= points[1].y) {
        order[0] = 2;
        order[1] = 1;
        order[2] = 0;
      } else if (points[2].y < points[0].y) {
        order[0] = 1;
        order[1] = 2;
        order[2] = 0;
      } else {
        order[0] = 1;
        order[1] = 0;
      }
    } else if (points[2].y < points[1].y) {
      if (points[2].y >= points[0].y) {
        order[1] = 2;
        order[2] = 1;
      } else {
        order[0] = 2;
        order[1] = 0;
        order[2] = 1;
      }
    }
    i32 min_x = BdRasterCeil(points[0].x), max_x = min_x;
    for (u32 index = 1; index < 3; ++index) {
      i32 x = BdRasterCeil(points[index].x);
      if (x < min_x)
        min_x = x;
      if (x > max_x)
        max_x = x;
    }
    if (min_x >= static_cast<i32>(view_width) || max_x <= 0 || max_x <= min_x)
      return true;
    SDL_FPoint sorted[3] = {points[order[0]], points[order[1]], points[order[2]]};
    i32 first = BdRasterCeil(sorted[0].y), middle = BdRasterCeil(sorted[1].y), last = BdRasterCeil(sorted[2].y);
    if (first < 0)
      first = 0;
    if (last > static_cast<i32>(view_height))
      last = static_cast<i32>(view_height);
    if (first >= last)
      return true;
    if (!SDL_FlushRenderer(renderer))
      return false;
    commands.BeginTriangle();
    BdRasterEdge long_edge = BdMakeRasterEdge(sorted[0].x, sorted[0].y, sorted[2].x, sorted[2].y, first, true);
    BdRasterEdge short_edge{};
    f32 attributes[4], row_whole[4], row_carry[4], row_y[4], step_x[4];
    f32 dx_short = sorted[1].x - sorted[0].x, dy_short = sorted[1].y - sorted[0].y;
    f32 dx_long = sorted[2].x - sorted[0].x, dy_long = sorted[2].y - sorted[0].y;
    f32 determinant = dy_short * dx_long - dy_long * dx_short;
    bool long_right = determinant > 0;
    f32 inverse = BdRasterDivide(1.0f, determinant), sx = dx_short * inverse, sy = dy_short * inverse;
    f32 lx = dx_long * inverse, ly = dy_long * inverse;
    f32 slope = BdRasterDivide(dx_long, dy_long), offset_y = static_cast<f32>(first) - sorted[0].y;
    f32 advance = offset_y * slope, at = advance + sorted[0].x;
    i32 origin = long_edge.x - (long_right ? 1 : 0);
    if (!long_right && origin < 0)
      origin = 0;
    if (long_right && origin >= static_cast<i32>(view_width))
      origin = static_cast<i32>(view_width) - 1;
    i32 attribute_x = origin;
    f32 offset_x = (static_cast<f32>(origin) - at) + advance;
    f32 rhw[3], largest = 0;
    for (u32 index = 0; index < 3; ++index) {
      rhw[index] = TextureReciprocal(input[index].x, input[index].y, input[index].z);
      if (index == 0 || rhw[index] > largest)
        largest = rhw[index];
    }
    f32 normalization = kBdTextureNormalizedRhw / largest;
    for (u32 index = 0; index < 3; ++index)
      rhw[index] *= normalization;
    f32 depth[3];
    const f32 *matrix = identity_world ? texture_view : texture_clip;
    for (u32 index = 0; index < 3; ++index) {
      const BdDualVertex &vertex = input[index];
      f32 clip_z = ((vertex.x * matrix[2] + vertex.y * matrix[6]) + vertex.z * matrix[10]) + matrix[14];
      depth[index] = clip_z * TextureReciprocal(vertex.x, vertex.y, vertex.z);
    }
    f32 depth_mid = (depth[order[1]] - depth[order[0]]) * kBdTextureRhwScale;
    f32 depth_end = (depth[order[2]] - depth[order[0]]) * kBdTextureRhwScale;
    f32 depth_x = sy * depth_end - ly * depth_mid, depth_y = lx * depth_mid - sx * depth_end;
    f32 depth_attribute = (offset_x * depth_x + depth[order[0]] * kBdTextureRhwScale) + offset_y * depth_y;
    f32 depth_whole = static_cast<f32>(long_edge.whole) * depth_x + depth_y;
    f32 depth_carry = depth_whole + (slope >= 0 ? depth_x : -depth_x);
    f32 depth_step = long_right ? -depth_x : depth_x;
    bool floating_gradient = fabsf(depth_x) >= 2147483648.0f;
    if (!floating_gradient)
      BdRasterWrite(commands.Header(), 8, BdRasterInteger(depth_step));
    f32 q_attribute = 0, q_whole = 0, q_carry = 0, q_y = 0, q_step = 0;
    if (coordinate_count) {
      f32 mid = (rhw[order[1]] - rhw[order[0]]) * kBdTextureRhwScale;
      f32 end = (rhw[order[2]] - rhw[order[0]]) * kBdTextureRhwScale;
      f32 x_gradient = sy * end - ly * mid, y_gradient = lx * mid - sx * end;
      if (!isfinite(x_gradient) || !isfinite(y_gradient))
        return BdPortableRendererMissing() != 0;
      floating_gradient |= fabsf(x_gradient) >= 2147483648.0f || fabsf(y_gradient) >= 2147483648.0f;
      q_attribute = (offset_x * x_gradient + rhw[order[0]] * kBdTextureRhwScale) + offset_y * y_gradient;
      q_whole = static_cast<f32>(long_edge.whole) * x_gradient + y_gradient;
      q_carry = q_whole + (slope >= 0 ? x_gradient : -x_gradient);
      q_step = long_right ? -x_gradient : x_gradient;
      q_y = y_gradient;
      if (!floating_gradient) {
        BdRasterWrite(commands.Header(), 12, BdRasterInteger(q_step));
        BdRasterWrite(commands.Header(), 80, BdRasterInteger(q_y));
      }
    }
    for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
      const f32 *values = coordinates[coordinate];
      f32 initial = values[order[0]] * rhw[order[0]];
      // TextureDiff subtracts the anchor before setup adds it back; retain both float-rounding steps.
      f32 mid_value = (values[order[1]] - values[order[0]]) + values[order[0]];
      f32 end_value = (values[order[2]] - values[order[0]]) + values[order[0]];
      f32 mid = (mid_value * rhw[order[1]] - initial) * kBdTextureCoordScale;
      f32 end = (end_value * rhw[order[2]] - initial) * kBdTextureCoordScale;
      f32 x_gradient = sy * end - ly * mid, y_gradient = lx * mid - sx * end;
      if (!isfinite(x_gradient) || !isfinite(y_gradient))
        return BdPortableRendererMissing() != 0;
      floating_gradient |= fabsf(x_gradient) >= 2147483648.0f || fabsf(y_gradient) >= 2147483648.0f;
      attributes[coordinate] = (offset_x * x_gradient + initial * kBdTextureCoordScale) + offset_y * y_gradient;
      row_whole[coordinate] = static_cast<f32>(long_edge.whole) * x_gradient + y_gradient;
      row_carry[coordinate] = row_whole[coordinate] + (slope >= 0 ? x_gradient : -x_gradient);
      step_x[coordinate] = long_right ? -x_gradient : x_gradient;
      row_y[coordinate] = y_gradient;
      if ((coordinate & 1) && !floating_gradient) {
        u32 x_field = coordinate == 1 ? 16 : 56, y_field = coordinate == 1 ? 64 : 72;
        BdRasterWrite(commands.Header(), x_field, BdRasterInteger(step_x[coordinate - 1]));
        BdRasterWrite(commands.Header(), x_field + 4, BdRasterInteger(step_x[coordinate]));
        BdRasterWrite(commands.Header(), y_field, BdRasterInteger(row_y[coordinate - 1]));
        BdRasterWrite(commands.Header(), y_field + 4, BdRasterInteger(row_y[coordinate]));
      }
    }
    bool gradient = input[0].color != input[1].color || input[0].color != input[2].color;
    f32 color_attributes[4], color_whole[4], color_carry[4], color_y[4], color_step[4];
    if (gradient) {
      for (u32 channel = 0; channel < 4; ++channel) {
        u32 shift = channel * 8;
        i32 initial = static_cast<i32>((input[order[0]].color >> shift) & 255);
        f32 mid = static_cast<f32>((static_cast<i32>((input[order[1]].color >> shift) & 255) - initial) * 256);
        f32 end = static_cast<f32>((static_cast<i32>((input[order[2]].color >> shift) & 255) - initial) * 256);
        f32 x_gradient = sy * end - ly * mid, y_gradient = lx * mid - sx * end;
        if (!isfinite(x_gradient) || !isfinite(y_gradient))
          return BdPortableRendererMissing() != 0;
        floating_gradient |= fabsf(x_gradient) >= 32768.0f;
        color_attributes[channel] = (offset_x * x_gradient + static_cast<f32>(initial * 256)) + offset_y * y_gradient;
        color_whole[channel] = static_cast<f32>(long_edge.whole) * x_gradient + y_gradient;
        color_carry[channel] = color_whole[channel] + (slope >= 0 ? x_gradient : -x_gradient);
        color_step[channel] = long_right ? -x_gradient : x_gradient;
        color_y[channel] = y_gradient;
      }
    }
    if (!floating_gradient) {
      for (u32 channel = 0; channel < 4; ++channel) {
        u16 increment = gradient ? static_cast<u16>(BdRasterInteger(color_step[channel])) : 0;
        memcpy(commands.Header() + 24 + channel * 2, &increment, sizeof(increment));
      }
    }
    bool clip_walk = min_x < 0 || max_x > static_cast<i32>(view_width) || max_x - min_x > (floating_gradient ? 1 : 256);
    for (i32 row = first; row < last; ++row) {
      if (row == first || row == middle) {
        commands.EndTrap();
        u32 start = row < middle ? 0 : 1;
        short_edge =
            BdMakeRasterEdge(sorted[start].x, sorted[start].y, sorted[start + 1].x, sorted[start + 1].y, row, false);
      }
      i32 left = long_edge.x < short_edge.x ? long_edge.x : short_edge.x;
      i32 right = long_edge.x > short_edge.x ? long_edge.x : short_edge.x;
      if (left < 0)
        left = 0;
      if (right > static_cast<i32>(view_width))
        right = static_cast<i32>(view_width);
      if (left < right) {
        u8 *output = static_cast<u8 *>(surface->pixels) + static_cast<usize>(row) * surface->pitch;
        i32 attr[4], increment[4];
        f32 span_attributes[4], span_colors[4];
        i32 row_origin = attribute_x;
        i32 distance = long_right ? row_origin - (right - 1) : left - row_origin;
        for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
          f32 value = attributes[coordinate] + static_cast<f32>(distance) * step_x[coordinate];
          if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return BdPortableRendererMissing() != 0;
          span_attributes[coordinate] = value;
          attr[coordinate] = static_cast<i32>(value);
          increment[coordinate] = floating_gradient ? 0 : static_cast<i32>(step_x[coordinate]);
        }
        u16 color_attr[4] = {};
        i16 color_increment[4] = {};
        for (u32 channel = 0; channel < 4; ++channel)
          color_attr[channel] = static_cast<u16>(((input[0].color >> (channel * 8)) & 255) << 8);
        if (gradient) {
          for (u32 channel = 0; channel < 4; ++channel) {
            f32 value = color_attributes[channel] + static_cast<f32>(distance) * color_step[channel];
            if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
              return BdPortableRendererMissing() != 0;
            span_colors[channel] = value;
            color_attr[channel] = static_cast<u16>(static_cast<i32>(value));
            color_increment[channel] = floating_gradient ? 0 : static_cast<i16>(color_step[channel]);
          }
        }
        f32 z_float = depth_attribute + static_cast<f32>(distance) * depth_step;
        f32 q_float = q_attribute + static_cast<f32>(distance) * q_step;
        i32 q = 0, q_increment = 0, reciprocal = 0, reciprocal_delta = 0;
        if (coordinate_count) {
          // The general two-stage converter divides the reserved-headroom scale before its 16-bit rescaling.
          f32 value =
              texture_two_stage ? (kBdTextureRhwScale / q_float) * 65536.0f : kBdTextureReciprocalScale / q_float;
          if (!isfinite(q_float) || q_float == 0 || q_float < -kBdTextureIntegerLimit ||
              q_float >= kBdTextureIntegerLimit || !isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return BdPortableRendererMissing() != 0;
          // The source stores the low 32 bits of its truncating 64-bit conversion.
          q = static_cast<i32>(static_cast<i64>(q_float));
          if (!floating_gradient)
            q_increment = static_cast<i32>(q_step);
          reciprocal = static_cast<i32>(value);
        }
        // Increasing q refines the first three points; decreasing q refines the final four.
        u16 refinement = 0;
        i32 span_left = 0, span_length = 0;
        u8 *packet = nullptr;
        for (i32 column = left; column < right; ++column) {
          i32 selected = long_right ? right - 1 - (column - left) : column;
          if (!span_left) {
            if (column != left && !floating_gradient) {
              z_float += static_cast<f32>(span_length) * depth_step;
              q_float += static_cast<f32>(span_length) * q_step;
              for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
                span_attributes[coordinate] += static_cast<f32>(span_length) * step_x[coordinate];
                attr[coordinate] = BdRasterInteger(span_attributes[coordinate]);
              }
              if (gradient)
                for (u32 channel = 0; channel < 4; ++channel) {
                  span_colors[channel] += static_cast<f32>(span_length) * color_step[channel];
                  color_attr[channel] = static_cast<u16>(BdRasterInteger(span_colors[channel]));
                }
              if (coordinate_count) {
                q = BdRasterInteger(q_float);
                f32 value =
                    texture_two_stage ? (kBdTextureRhwScale / q_float) * 65536.0f : kBdTextureReciprocalScale / q_float;
                reciprocal = BdRasterInteger(value);
              }
              reciprocal_delta = 0;
            }
            span_length = floating_gradient ? 1 : right - column < 256 ? right - column : 256;
            span_left = span_length;
            i32 trap_end = row < middle && middle < last ? middle : last;
            packet = commands.Span(commands.AllocateSpan(static_cast<u32>(trap_end - row), clip_walk));
            refinement = static_cast<u16>(q_increment <= 0 ? -32764 - span_length : -3);
            BdRasterWrite(packet, 12,
                static_cast<i32>(static_cast<u32>(BdRasterInteger(z_float)) +
                    static_cast<u32>(BdRasterRead(commands.Header(), 8)) * span_length));
            BdRasterWrite(packet, 60, 0);
          }
          if (floating_gradient) {
            for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
              f32 value = span_attributes[coordinate];
              if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                return BdPortableRendererMissing() != 0;
              attr[coordinate] = static_cast<i32>(value);
            }
            if (gradient) {
              for (u32 channel = 0; channel < 4; ++channel) {
                f32 value = span_colors[channel];
                if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                  return BdPortableRendererMissing() != 0;
                color_attr[channel] = static_cast<u16>(static_cast<i32>(value));
              }
            }
            if (coordinate_count) {
              f32 value =
                  texture_two_stage ? (kBdTextureRhwScale / q_float) * 65536.0f : kBdTextureReciprocalScale / q_float;
              if (!isfinite(q_float) || q_float == 0 || q_float < -kBdTextureIntegerLimit ||
                  q_float >= kBdTextureIntegerLimit || !isfinite(value) || value < -2147483648.0f ||
                  value >= 2147483648.0f)
                return BdPortableRendererMissing() != 0;
              reciprocal = static_cast<i32>(value);
            }
          }
          if (span_left == span_length) {
            if (coordinate_count) {
              BdRasterWrite(packet, 8, 0);
              BdRasterWrite(packet, 20,
                  static_cast<i32>(static_cast<u32>(BdRasterInteger(q_float)) +
                      static_cast<u32>(BdRasterRead(commands.Header(), 12)) * span_length));
            }
            for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
              u32 field = coordinate < 2 ? 24 + coordinate * 4 : 48 + (coordinate - 2) * 4;
              u32 step = coordinate < 2 ? 16 + coordinate * 4 : 56 + (coordinate - 2) * 4;
              BdRasterWrite(packet, field,
                  static_cast<i32>(static_cast<u32>(attr[coordinate]) +
                      static_cast<u32>(BdRasterRead(commands.Header(), step)) * span_length));
            }
            for (u32 channel = 0; channel < 4; ++channel) {
              u16 step;
              memcpy(&step, commands.Header() + 24 + channel * 2, sizeof(step));
              u16 final = static_cast<u16>(color_attr[channel] + step * span_length);
              memcpy(packet + 32 + channel * 2, &final, sizeof(final));
            }
          }
          i32 fixed[4];
          for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate)
            fixed[coordinate] = static_cast<i32>((static_cast<i64>(attr[coordinate]) * reciprocal) >> 20);
          if (!floating_gradient) {
            q = static_cast<i32>(static_cast<u32>(q) + q_increment);
            i32 prediction = static_cast<i32>(static_cast<u32>(reciprocal) + reciprocal_delta), updated = prediction;
            if (refinement & 0x8000) {
              if (updated < 0)
                updated = reciprocal >> 1;
              for (u32 iteration = 0; iteration < 7; ++iteration) {
                i32 previous = updated, high = static_cast<i32>((static_cast<i64>(updated) * q) >> 32);
                i32 error = 65536 - high;
                while (error < 0)
                  error = (error + 32768) >> 1;
                i32 correction = static_cast<i32>(static_cast<u32>(error) << 15);
                updated = static_cast<i32>(static_cast<u32>((static_cast<i64>(updated) * correction) >> 32) * 4);
                i64 change = static_cast<i64>(updated) - previous;
                if (change >= -32 && change <= 32)
                  break;
              }
            } else {
              i32 high = static_cast<i32>((static_cast<i64>(prediction) * q) >> 32);
              i32 correction = static_cast<i32>(static_cast<u32>(-65536 - high) << 15);
              updated = static_cast<i32>(static_cast<u32>((static_cast<i64>(prediction) * correction) >> 32) * 4);
            }
            ++refinement;
            reciprocal_delta = static_cast<i32>(static_cast<u32>(updated) - reciprocal);
            reciprocal = updated;
            for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate)
              attr[coordinate] = static_cast<i32>(static_cast<u32>(attr[coordinate]) + increment[coordinate]);
          }
          u32 diffuse = input[0].color;
          if (gradient) {
            diffuse = 0;
            for (u32 channel = 0; channel < 4; ++channel) {
              diffuse |= static_cast<u32>(color_attr[channel] >> 8) << (channel * 8);
              color_attr[channel] = static_cast<u16>(color_attr[channel] + color_increment[channel]);
            }
          }
          u32 stipple_alpha = gradient ? color_attr[3] >> 8 : diffuse >> 24;
          if (floating_gradient) {
            u16 step;
            memcpy(&step, commands.Header() + 30, sizeof(step));
            stipple_alpha = static_cast<u16>(color_attr[3] + step) >> 8;
          }
          if (!retain_only)
            ShadeTexturePixel(
                selected, row, output + static_cast<usize>(selected) * sizeof(u16), diffuse, stipple_alpha, fixed);
          --span_left;
          if (!span_left && coordinate_count)
            BdRasterWrite(
                packet, 16, floating_gradient ? BdRasterRefine(BdRasterRead(packet, 20), reciprocal) : reciprocal);
          if (floating_gradient && column + 1 < right) {
            z_float += depth_step;
            q_float += q_step;
            for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate)
              span_attributes[coordinate] += step_x[coordinate];
            if (gradient)
              for (u32 channel = 0; channel < 4; ++channel)
                span_colors[channel] += color_step[channel];
          }
        }
      }
      i64 next_fraction = long_edge.fraction + long_edge.increment;
      bool carry = next_fraction < 0 || next_fraction >= 0x80000000ll;
      i32 raw_anchor = long_edge.x - (long_right ? 1 : 0);
      bool clipped = attribute_x != raw_anchor;
      BdAdvanceRasterEdge(&long_edge);
      i32 next_anchor = long_edge.x - (long_right ? 1 : 0), next_raw = next_anchor;
      if (!long_right && next_anchor < 0)
        next_anchor = 0;
      if (long_right && next_anchor >= static_cast<i32>(view_width))
        next_anchor = static_cast<i32>(view_width) - 1;
      i32 horizontal = next_anchor - (clipped ? attribute_x : next_raw);
      f32 adjustment = static_cast<f32>(long_right ? -horizontal : horizontal);
      // A clipped long edge advances vertically first, then resumes its own horizontal motion on reentry.
      depth_attribute += clipped ? depth_y : carry ? depth_carry : depth_whole;
      if (horizontal)
        depth_attribute += adjustment * depth_step;
      q_attribute += clipped ? q_y : carry ? q_carry : q_whole;
      if (horizontal)
        q_attribute += adjustment * q_step;
      for (u32 coordinate = 0; coordinate < coordinate_count; ++coordinate) {
        f32 row_increment = clipped ? row_y[coordinate] : carry ? row_carry[coordinate] : row_whole[coordinate];
        attributes[coordinate] += row_increment;
        if (horizontal)
          attributes[coordinate] += adjustment * step_x[coordinate];
      }
      if (gradient) {
        for (u32 channel = 0; channel < 4; ++channel) {
          f32 row_increment = clipped ? color_y[channel] : carry ? color_carry[channel] : color_whole[channel];
          color_attributes[channel] += row_increment;
          if (horizontal)
            color_attributes[channel] += adjustment * color_step[channel];
        }
      }
      attribute_x = next_anchor;
      BdAdvanceRasterEdge(&short_edge);
    }
    commands.EndTrap();
    return true;
  }

  BdRasterCommands commands;
  GxBuffer *target;
  BdCpuTexture *textures[2] = {};
  bool texture_signed_add = false, texture_keyed = false, texture_two_stage = false;
  bool texture_dual_combiner = false;
  u32 secondary_sample = 0;
  bool texture_diffuse_only = true, texture_alpha_from_texture = false;
  u32 texture_flags = 0, texture_blend = 0;
  SDL_Surface *surface = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_BlendMode blend = SDL_BLENDMODE_NONE;
  u32 view_width = 0, view_height = 0;
  f32 world_x = 0, world_y = 0;
  f32 texture_view[16] = {}, texture_clip[16] = {};
  bool linear_filter[2] = {};
  bool alpha_enabled = false, identity_world = false;
  bool locked = false, color_fan = false, line_list = false, line_header = false;
};

static constexpr i32 kBdRenderFailure = static_cast<i32>(0x80004005u);

// Architect faces use distinct default, ground and empty-object colors when no explicit face color is supplied.
enum BdRenderColor : u32 {
  kBdDefaultFaceColor = 0xffa0a0a0,
  kBdGroundFaceColor = 0xff709750,
  kBdEmptyObjectFaceColor = 0xff208020,
};


const BdPointF BdShapePoints[12] = {{0.0f, 1.0f}, {1.0f, 0.5f}, {0.0f, 0.0f}, {-1.0f, 0.5f}, {0.0f, 0.0f},
    {1.0f, -0.5f}, {0.0f, -1.0f}, {-1.0f, -0.5f}, {0.0f, 0.5f}, {1.0f, 0.0f}, {0.0f, -0.5f}, {-1.0f, 0.0f}};

static const BdPointF BdLightAngles[16] = {{0.0f, 0.7853981852531433f}, {1.5707963705062866f, 0.7853981852531433f},
    {0.0f, 2.356194496154785f}, {1.5707963705062866f, 2.356194496154785f}, {0.0f, 1.5707963705062866f}, {0.0f, 0.0f},
    {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.7853981852531433f}, {1.5707963705062866f, 0.7853981852531433f},
    {0.0f, 2.356194496154785f}, {1.5707963705062866f, 2.356194496154785f}, {0.0f, 1.5707963705062866f}, {0.0f, 0.0f},
    {0.0f, 0.0f}, {0.0f, 0.0f}};

// Clear the four point arrays and color table, initialize known graphics/flag state, then select the original scale two.
// Clip, offset, dimensions and other initialized-later fields deliberately retain their source uninitialized lifetime.
BdRenderer::BdRenderer() {
  for (u32 i = 0; i < 12; ++i)
    points[i] = {0.0f, 0.0f};
  for (u32 i = 0; i < 12; ++i)
    scaled[i] = {0.0f, 0.0f};
  memset(colors, 0, sizeof(colors));
  for (u32 i = 0; i < 12; ++i)
    projected[i] = {0.0f, 0.0f};
  for (u32 i = 0; i < 12; ++i)
    object_uv[i] = {0.0f, 0.0f};
  catalog = nullptr;
  buffer = nullptr;
  materials = nullptr;
  draw_edges = draw_empty_objects = 0;
  direct_3d = nullptr;
  device = nullptr;
  viewport = nullptr;
  scene_active = 0;
  draw_calls = triangles = lines = texture_binds = state_changes = 0;
  use_sdk = 0;
  sdk_renderer = nullptr;
  device_ready = stage0_enabled = stage1_enabled = blend_enabled = keying_enabled = field_311 = stage0_linear =
      stage1_linear = 0;
  linear_filter = 0;
  SetScale(2.0f);
}

BdRenderer::~BdRenderer() {
}

// Scale all twelve original shape points, preserving the source's separate x/y multiplication and stored scale.
void BdRenderer::SetScale(f32 value) {
  scale = value;
  for (u32 i = 0; i < 12; ++i) {
    scaled[i].x = value * BdShapePoints[i].x;
    scaled[i].y = value * BdShapePoints[i].y;
  }
}

// Rotate only the first sixteen directional lights from their original angle bases, leaving preview lights fixed.
void BD_FASTCALL BdRenderer::RotateLights(i32 rotation) {
  f64 angle = static_cast<f64>(rotation) * static_cast<f64>(3.1415927f) * 0.00390625;
  for (u32 i = 0; i < 16; ++i)
    BdLights[i].azimuth = static_cast<f32>(angle + BdLightAngles[i].x);
}

// Accumulate positive directional-light contributions, clamp channels and publish the opaque packed color. Callers
// dereference the returned output pointer; red/green and prior blue sums retain the original float spills.
u32 *BD_FASTCALL BdRenderer::Shade(u32 *out, f32 x, f32 y, f32 z, i32 mode) {
  static const u32 starts[5] = {0, 8, 8, 8, 16};
  f32 red = 0.0f, green = 0.0f, blue = 0.0f;
  f64 final_blue = 0.0;
  for (u32 i = 0; i < 6; ++i) {
    const BdLight *light = BdLights + starts[mode] + i;
    f64 elevation_cos = cos(light->elevation);
    f32 dx = static_cast<f32>(cos(light->azimuth) * elevation_cos);
    f64 azimuth_sin = sin(light->azimuth);
    f32 dy = static_cast<f32>(cos(light->elevation) * azimuth_sin);
    f64 dot = sin(light->elevation) * z + static_cast<f64>(dx) * x + static_cast<f64>(dy) * y;
    if (dot > 0.0) {
      red = static_cast<f32>(dot * light->red + red);
      green = static_cast<f32>(dot * light->green + green);
      final_blue = dot * light->blue + blue;
      blue = static_cast<f32>(final_blue);
    } else
      final_blue = blue;
  }
  if (red < 0.0f)
    red = 0.0f;
  if (green < 0.0f)
    green = 0.0f;
  if (final_blue < 0.0)
    final_blue = 0.0;
  if (red > 1.0f)
    red = 1.0f;
  if (green > 1.0f)
    green = 1.0f;
  if (final_blue > 1.0)
    final_blue = 1.0;
  *out = kGxArgbAlphaMask | static_cast<u32>(static_cast<f64>(red) * 255.0) << 16 |
      static_cast<u32>(static_cast<f64>(green) * 255.0) << 8 | static_cast<u32>(final_blue * 255.0);
  return out;
}

// Populate the exact source face-color groups from integer mode/rotation, retaining the untouched eight-entry group.
// The first two rings preserve the original rounded intermediate y product before their forty-five-degree rotation.
u32 BD_FASTCALL BdRenderer::SetLighting(i32 mode, i32 rotation) {
  f64 angle = static_cast<f64>(rotation + 64) * static_cast<f64>(3.1415927f) * 0.00390625;
  f32 cosine = static_cast<f32>(cos(angle)), sine = static_cast<f32>(sin(angle));
  f32 x = cosine, y = sine;
  for (u32 i = 0; i < 8; ++i) {
    Shade(colors + i, x, y, 0.0f, mode);
    f64 first = static_cast<f64>(x) * 0.70710677f, second = static_cast<f64>(y) * 0.70710677f;
    y = static_cast<f32>(static_cast<f64>(static_cast<f32>(second)) + first);
    x = static_cast<f32>(first - second);
  }
  x = cosine * 0.70710677f;
  y = sine * 0.70710677f;
  for (u32 i = 0; i < 8; ++i) {
    Shade(colors + 8 + i, x, y, 0.70710677f, mode);
    f64 first = static_cast<f64>(x) * 0.70710677f, second = static_cast<f64>(y) * 0.70710677f;
    y = static_cast<f32>(static_cast<f64>(static_cast<f32>(second)) + first);
    x = static_cast<f32>(first - second);
  }
  Shade(colors + 16, 0.0f, 0.0f, 1.0f, mode);
  Shade(colors + 56, 0.0f, 0.0f, 1.0f, mode);
  for (u32 i = 1; i < 8; ++i) {
    colors[16 + i] = colors[16];
    colors[56 + i] = colors[56];
  }
  Shade(colors + 40, 0.0f, 0.0f, -1.0f, mode);
  for (u32 i = 1; i < 8; ++i)
    colors[40 + i] = colors[40];
  x = 0.5f * cosine;
  y = 0.5f * sine;
  for (u32 i = 0; i < 8; ++i) {
    Shade(colors + 24 + i, x, y, 0.86602539f, mode);
    f64 first = static_cast<f64>(x) * 0.86602539f, second = static_cast<f64>(y) * 0.5f;
    y = static_cast<f32>(static_cast<f64>(x) * 0.5f + static_cast<f64>(y) * 0.86602539f);
    x = static_cast<f32>(first - second);
  }
  x = 0.86602539f * cosine;
  y = 0.86602539f * sine;
  for (u32 i = 0; i < 8; ++i) {
    Shade(colors + 32 + i, x, y, 0.5f, mode);
    f64 first = static_cast<f64>(x) * 0.5f, second = static_cast<f64>(y) * 0.86602539f;
    y = static_cast<f32>(static_cast<f64>(x) * 0.86602539f + static_cast<f64>(y) * 0.5f);
    x = static_cast<f32>(first - second);
  }
  return colors[39];
}

// Borrow the attached real graphics buffer without retaining it or initiating device setup.
GxBuffer *BdRenderer::GetBuffer() {
  return buffer;
}

// Copy the two original projection-offset coordinates and preserve the observed second-coordinate result.
i32 BdRenderer::SetOffset(const BdPoint *value) {
  offset.x = value->x;
  offset.y = value->y;
  return value->y;
}

// Store the borrowed catalog for renderer lookups and preserve the observed pointer result.
// This assignment does not acquire or release catalog ownership.
BdCatalog *BdRenderer::SetCatalog(BdCatalog *value) {
  catalog = value;
  return value;
}

void BdRenderer::SetMaterials(BdMaterials *value) {
  materials = value;
}

// Translate all twelve scaled shape points through the original orientation permutation into projected coordinates.
// Convert screen integers to double before adding float offsets and storing each float result.
void BD_FASTCALL BdRenderer::ProjectCell(const BdPoint *screen, u8 rotation) {
  for (u32 i = 0; i < 12; ++i) {
    const BdPointF *point = scaled + BdCellRotations[rotation][i];
    projected[i].x = static_cast<f32>(static_cast<f64>(screen->x) + point->x);
    projected[i].y = static_cast<f32>(static_cast<f64>(screen->y) + point->y);
  }
}

// Draw visible block faces in the original orientation order, dispatching object cells before ordinary block handling.
void BD_FASTCALL BdRenderer::DrawCell(const BdPoint *screen, const BdCell *cell, u8 use_color, u32 color) {
  u32 kind_bits = cell->link_height_kind & kBdCellKindMask;
  if (kind_bits == kBdCellObjectKind) {
    DrawObjectCell(screen, cell, use_color, color);
    return;
  }
  u32 mask = cell->face_mask;
  if (!static_cast<u8>(mask))
    return;
  u8 rotation = static_cast<u8>(cell->link_height_kind >> kBdCellRotationShift);
  ProjectCell(screen, rotation);
  u32 fill_color = use_color ? color : kBdDefaultFaceColor;
  if (kind_bits == kBdCellGroundKind)
    fill_color = kBdGroundFaceColor;
  BdColor edge = {fill_color};
  edge.Darken(32);
  u32 kind = (cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask;
  for (const i8 *entry = BdCellFaceOrder[kind][rotation]; *entry != -1; ++entry) {
    u32 face_index = static_cast<u8>(*entry);
    if (mask & (1u << face_index)) {
      const BdCellPart *part = cell->parts + face_index;
      BdFaceTextures textures = {materials->Peek(part->first), materials->Peek(part->second),
          static_cast<u8>(part->offset_x), static_cast<u8>(part->offset_y), nullptr};
      const BdFace *face = &BdCellFaces[kind][face_index];
      if (textures.primary || textures.secondary)
        DrawTexturedFace(face, &textures, rotation,
            static_cast<i16>((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask), use_color,
            color);
      else {
        DrawFace(face, fill_color, rotation);
        if (draw_edges)
          DrawEdges(face, edge.value, 1);
      }
    }
  }
}

// Preserve the block projection/material work while submitting only face zero when it appears in the visible-face order.
// Object cells still dispatch through their independent full object-face path.
void BD_FASTCALL BdRenderer::DrawTopCell(const BdPoint *screen, const BdCell *cell, u8 use_color, u32 color) {
  u32 kind_bits = cell->link_height_kind & kBdCellKindMask;
  if (kind_bits == kBdCellObjectKind) {
    DrawObjectCell(screen, cell, use_color, color);
    return;
  }
  u32 mask = cell->face_mask;
  if (!static_cast<u8>(mask))
    return;
  u8 rotation = static_cast<u8>(cell->link_height_kind >> kBdCellRotationShift);
  ProjectCell(screen, rotation);
  u32 fill_color = use_color ? color : kBdDefaultFaceColor;
  if (kind_bits == kBdCellGroundKind)
    fill_color = kBdGroundFaceColor;
  BdColor edge = {fill_color};
  edge.Darken(32);
  u32 kind = (cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask;
  for (const i8 *entry = BdCellFaceOrder[kind][rotation]; *entry != -1; ++entry) {
    if (*entry == 0 && (mask & 1)) {
      const BdCellPart *part = cell->parts;
      BdFaceTextures textures = {materials->Peek(part->first), materials->Peek(part->second),
          static_cast<u8>(part->offset_x), static_cast<u8>(part->offset_y), nullptr};
      const BdFace *face = &BdCellFaces[kind][0];
      if (textures.primary || textures.secondary)
        DrawTexturedFace(face, &textures, rotation,
            static_cast<i16>((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask), use_color,
            color);
      else {
        DrawFace(face, fill_color, rotation);
        if (draw_edges)
          DrawEdges(face, edge.value, 1);
      }
    }
  }
}

void BD_FASTCALL BdRenderer::DrawObjectCell(const BdPoint *screen, const BdCell *cell, u8 use_color, u32 color) {
  u32 mask = cell->face_mask;
  if (!static_cast<u8>(mask))
    return;
  u8 rotation = static_cast<u8>(cell->link_height_kind >> kBdCellRotationShift);
  ProjectCell(screen, rotation);
  for (u32 i = 0; i < 12; ++i)
    object_uv[i] = BdShapePoints[BdCellRotations[rotation][i]];
  BdColor edge = {kBdEmptyObjectFaceColor};
  edge.Darken(32);
  const BdCellPart *part = cell->parts + rotation + 1;
  BdFaceTextures textures = {materials->Peek(part->first), materials->Peek(part->second),
      static_cast<u8>(part->offset_x), static_cast<u8>(part->offset_y), nullptr};
  u32 kind = (cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask;
  for (const i8 *entry = BdCellFaceOrder[kind][rotation]; *entry != -1; ++entry) {
    u32 face_index = static_cast<u8>(*entry);
    if (mask & (1u << face_index)) {
      const BdFace *face = &BdCellFaces[kind][face_index];
      if (textures.primary)
        DrawObjectFace(face, &textures, rotation,
            static_cast<i32>((cell->link_height_kind >> kBdCellHeightShift) & kBdCellHeightValueMask), use_color,
            color);
      else if (draw_empty_objects) {
        DrawFace(face, kBdEmptyObjectFaceColor, rotation);
        if (draw_edges)
          DrawEdges(face, edge.value, 1);
      }
    }
  }
}

// Draw every orientation-visible selection face regardless of the cell mask; ordinary blocks force opaque alpha.
// Keep the original fill tint separate from its darkened outline copy, and project even an empty-mask cell.
void BD_FASTCALL BdRenderer::DrawHighlight(const BdPoint *screen, const BdCell *cell, u32 color) {
  u32 kind = (cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask;
  if (kind != 15)
    color |= kGxArgbAlphaMask;
  u8 rotation = static_cast<u8>(cell->link_height_kind >> kBdCellRotationShift);
  ProjectCell(screen, rotation);
  BdColor edge = {color};
  edge.Darken(32);
  for (const i8 *entry = BdCellFaceOrder[kind][rotation]; *entry != -1; ++entry) {
    const BdFace *face = &BdCellFaces[kind][static_cast<u8>(*entry)];
    DrawFace(face, color, rotation);
    if (draw_edges)
      DrawEdges(face, edge.value, 1);
  }
}

// Release the selected SDK renderer or the actual viewport/device/Direct3D chain in source order, then clear readiness.
i32 BdRenderer::ReleaseDevice() {
  if (use_sdk) {
    sdk_renderer->Release();
    sdk_renderer = nullptr;
  } else {
    delete device;
    device = nullptr;
  }
  device_ready = 0;
  return 1;
}

// Begin the actual Direct3D scene; collapse a failed HRESULT to E_FAIL without touching cached scene state.
// A successful begin marks the scene active and clears the five source cache words.
// Lock the target and open its SDL software scene before publishing the original scene and counter state.
i32 BdRenderer::BeginScene() {
  if (use_sdk)
    return BdPortableRendererMissing();
  if (!device || !device->Open())
    return kBdRenderFailure;
  scene_active = 1;
  draw_calls = triangles = lines = texture_binds = state_changes = 0;
  return 0;
}

// Attach a different or unready buffer through the real ownership and device-initialization operations.
// Reusing the same ready buffer does nothing; detaching preserves its old dimensions and clip fields.
void BdRenderer::SetBuffer(GxBuffer *target) {
  if (!device_ready || !buffer || !target || buffer != target) {
    if (buffer) {
      ReleaseDevice();
      buffer->Release();
      buffer = nullptr;
    }
    if (target && !device_ready) {
      target->AddRef();
      buffer = target;
      const ScRect *area = buffer->GetBufferArea();
      clip = {area->left, area->top, area->right, area->bottom};
      width = static_cast<f32>(buffer->Width());
      height = static_cast<f32>(buffer->Height());
      InitializeDevice();
    }
  }
}

// Initialize the selected renderer from the actual SDK interfaces, retaining the alternate object when supplied.
// The legacy RGB path preserves the original HRESULTs and device-creation call order.
i32 BdRenderer::InitializeDevice() {
  GxGraphics *graphics = BdGetGraphics();
  if (!graphics)
    return 0;
  GxRendererSource *alternate = nullptr;
  if (graphics->QueryInterface(IID_GxRendererSource, reinterpret_cast<void **>(&alternate))) {
    sdk_renderer = alternate->GetRenderer();
    if (sdk_renderer) {
      sdk_renderer->AddRef();
      use_sdk = 1;
    }
    alternate->Release();
    device_ready = 1;
    return 1;
  }
  BdCpuRenderDevice *candidate = new BdCpuRenderDevice(buffer);
  if (!candidate->Open()) {
    delete candidate;
    return kBdRenderFailure;
  }
  if (!candidate->Close()) {
    delete candidate;
    return kBdRenderFailure;
  }
  candidate->SetView(buffer->Width(), buffer->Height(), offset);
  device = candidate;
  device_ready = 1;
  return 1;
}

// Finish the actual COM scene and clear the original active byte even when the SDK reports failure.
// The unavailable alternate SDK operation is rejected explicitly instead of being replaced by the COM backend.
// Finish the SDL software scene and clear the original active byte even when flushing or unlocking fails.
i32 BdRenderer::EndScene() {
  if (use_sdk)
    return BdPortableRendererMissing();
  bool result = device && device->renderer && device->Close();
  scene_active = 0;
  return result ? 0 : kBdRenderFailure;
}

// Apply color-key enable and the original point/linear filter transitions, counting every actual SDK state call.
// The linear preference deliberately submits all four filter calls even when their cached flags already match.
// Update the original key and filtering caches used by the CPU texture sampler.
void BD_FASTCALL BdRenderer::SetColorKey(u8 enabled) {
  if (enabled) {
    if (!keying_enabled) {
      keying_enabled = 1;
      ++state_changes;
    }
    if (stage0_linear || stage1_linear) {
      device->linear_filter[0] = device->linear_filter[1] = false;
      state_changes += 4;
      stage0_linear = stage1_linear = 0;
    }
    keying_enabled = 1;
  } else {
    if (linear_filter) {
      device->linear_filter[0] = device->linear_filter[1] = true;
      state_changes += 4;
      stage0_linear = stage1_linear = 1;
    }
    if (keying_enabled) {
      keying_enabled = 0;
      ++state_changes;
    }
  }
}

// Preserve the source blend-cache behavior and disable both previously active texture stages for an untextured primitive.
// In the color-key case the cache becomes set while alpha blending is disabled, exactly as in the original.
// Apply the original untextured blend transitions to the SDL scene while retaining the source cache counters.
void BD_FASTCALL BdRenderer::SetColorState(const BdColorState *state) {
  if (state->flags & 0x48) {
    SetColorKey(0);
    if (!blend_enabled) {
      blend_enabled = 1;
      device->alpha_enabled = true;
      ++state_changes;
    }
    device->SetBlend(device->alpha_enabled, true);
    state_changes += 2;
  } else if (state->flags & 0x400) {
    SetColorKey(1);
    if (!blend_enabled) {
      blend_enabled = 1;
      device->alpha_enabled = false;
      ++state_changes;
    }
    device->SetBlend(device->alpha_enabled, false);
    state_changes += 2;
  } else if (blend_enabled) {
    SetColorKey(0);
    blend_enabled = 0;
    device->alpha_enabled = false;
    device->SetBlend(false, false);
    state_changes += 3;
  }
  if (stage0_enabled) {
    stage0_enabled = 0;
    state_changes += 2;
  } else if (!stage1_enabled)
    return;
  if (stage1_enabled) {
    stage1_enabled = 0;
    state_changes += 2;
  }
}

// Set all four texture filters, retaining the raw preference byte and the source's asymmetric enabled-cache update.
// Retain the two actual filter selections for subsequent texture binding and update the original cache counters.
i32 BD_FASTCALL BdRenderer::SetFiltering(u8 enabled) {
  linear_filter = enabled;
  device->linear_filter[0] = device->linear_filter[1] = enabled != 0;
  stage0_linear = enabled ? 1 : 0;
  if (!enabled)
    stage1_linear = 0;
  state_changes += 4;
  return 0;
}

// Submit the two XYZ/diffuse line vertices with their following state and count the request even on SDK failure.
i32 BD_FASTCALL BdRenderer::DrawLine(BdColorLine *line) {
  bool gradient = line->vertices[0].color != line->vertices[1].color;
  if (!device->SupportsLine(line->vertices))
    return BdPortableRendererMissing();
  SetColorState(&line->state);
  bool degenerate = line->vertices[0].x == line->vertices[1].x && line->vertices[0].y == line->vertices[1].y;
  bool cpu = gradient || keying_enabled || device->blend == SDL_BLENDMODE_BLEND ||
      (device->surface && device->surface->format == SDL_PIXELFORMAT_ARGB8888 &&
          (line->vertices[0].color >> 24) != 255);
  bool result = device->renderer && (degenerate || (cpu ? device->DrawCpuLine(line->vertices, keying_enabled)
                                                        : device->DrawLine(line->vertices)));
  ++draw_calls;
  ++lines;
  return result ? 0 : kBdRenderFailure;
}

// Submit the first three vertices of the original four-slot color packet as a triangle list.
// Retain the source command producer alongside the published pixel body; extreme color steps stay explicit.
i32 BD_FASTCALL BdRenderer::DrawTriangle(BdColorQuad *triangle) {
  if (!device->color_fan) {
    device->commands.Begin();
    BdDualVertex packet[3];
    for (u32 index = 0; index < 3; ++index) {
      const BdColorVertex &vertex = triangle->vertices[index];
      packet[index] = {vertex.x, vertex.y, vertex.z, vertex.color, 0, 0, 0, 0};
    }
    if (!device->DrawTextureTriangle(packet, true))
      return kBdRenderFailure;
  }
  bool gradient = triangle->vertices[1].color != triangle->vertices[0].color ||
      triangle->vertices[2].color != triangle->vertices[0].color;
  for (u32 index = 0; index < 3; ++index) {
    const BdColorVertex &vertex = triangle->vertices[index];
    if (vertex.z != 1.0f || !isfinite(vertex.x) || !isfinite(vertex.y))
      return BdPortableRendererMissing();
  }
  const BdColorVertex *v = triangle->vertices;
  f64 cross = (static_cast<f64>(v[1].x) - v[0].x) * (static_cast<f64>(v[2].y) - v[0].y) -
      (static_cast<f64>(v[1].y) - v[0].y) * (static_cast<f64>(v[2].x) - v[0].x);
  if (cross > 0) {
    if (!device->renderer || !device->surface ||
        (device->surface->format != SDL_PIXELFORMAT_RGB565 && device->surface->format != SDL_PIXELFORMAT_XRGB1555 &&
            device->surface->format != SDL_PIXELFORMAT_ARGB8888) ||
        device->view_width > 4096 || device->view_height > 4096)
      return BdPortableRendererMissing();
    SDL_FPoint vertices[3];
    u32 vertex_colors[3];
    for (u32 index = 0; index < 3; ++index) {
      vertices[index] = device->Project(v[index]);
      vertex_colors[index] = v[index].color;
      vertices[index].x -= 1.0f;
      vertices[index].y -= 0.25f;
      if (!isfinite(vertices[index].x) || !isfinite(vertices[index].y) || vertices[index].x < -4096 ||
          vertices[index].y < -4096 ||
          vertices[index].x > static_cast<f32>(device->view_width) + 4096 ||
          vertices[index].y > static_cast<f32>(device->view_height) + 4096)
        return BdPortableRendererMissing();
    }
    u32 order[3] = {0, 1, 2};
    if (vertices[1].y < vertices[0].y) {
      if (vertices[2].y <= vertices[1].y) {
        order[0] = 2;
        order[1] = 1;
        order[2] = 0;
      } else if (vertices[2].y < vertices[0].y) {
        order[0] = 1;
        order[1] = 2;
        order[2] = 0;
      } else {
        order[0] = 1;
        order[1] = 0;
      }
    } else if (vertices[2].y < vertices[1].y) {
      if (vertices[2].y >= vertices[0].y) {
        order[1] = 2;
        order[2] = 1;
      } else {
        order[0] = 2;
        order[1] = 0;
        order[2] = 1;
      }
    }
    SDL_FPoint sorted[3] = {vertices[order[0]], vertices[order[1]], vertices[order[2]]};
    u32 sorted_colors[3] = {vertex_colors[order[0]], vertex_colors[order[1]], vertex_colors[order[2]]};
    memcpy(vertices, sorted, sizeof(vertices));
    memcpy(vertex_colors, sorted_colors, sizeof(vertex_colors));
    i32 first = BdRasterCeil(vertices[0].y), middle = BdRasterCeil(vertices[1].y), last = BdRasterCeil(vertices[2].y);
    if (first < 0)
      first = 0;
    if (last > static_cast<i32>(device->view_height))
      last = static_cast<i32>(device->view_height);
    SetColorState(&triangle->state);
    if (!SDL_FlushRenderer(device->renderer)) {
      ++draw_calls;
      ++triangles;
      return kBdRenderFailure;
    }
    u32 color = v[0].color;
    u32 pixel = SDL_MapSurfaceRGBA(device->surface, static_cast<u8>(color >> 16), static_cast<u8>(color >> 8),
        static_cast<u8>(color), static_cast<u8>(color >> 24));
    bool argb32 = device->surface->format == SDL_PIXELFORMAT_ARGB8888;
    if (first < last) {
      BdRasterEdge long_edge = BdMakeRasterEdge(vertices[0].x, vertices[0].y, vertices[2].x, vertices[2].y, first, true);
      BdRasterEdge short_edge = {};
      f32 attributes[4] = {}, row_whole[4] = {}, row_carry[4] = {}, step_x[4] = {};
      bool long_right = false;
      i32 origin_adjust = 0;
      if (gradient) {
        f32 dx_short = vertices[1].x - vertices[0].x, dy_short = vertices[1].y - vertices[0].y;
        f32 dx_long = vertices[2].x - vertices[0].x, dy_long = vertices[2].y - vertices[0].y;
        f32 area = dy_short * dx_long - dy_long * dx_short;
        u32 inversions = (order[0] > order[1]) + (order[0] > order[2]) + (order[1] > order[2]);
        if (area == 0 || ((inversions & 1) != 0) != (area > 0)) {
          ++draw_calls;
          ++triangles;
          return 0;
        }
        long_right = area > 0;
        f32 inverse = BdRasterDivide(1.0f, area), sx = dx_short * inverse, sy = dy_short * inverse,
            lx = dx_long * inverse, ly = dy_long * inverse;
        if (!isfinite(inverse))
          return BdPortableRendererMissing();
        f32 slope = BdRasterDivide(dx_long, dy_long), offset_y = static_cast<f32>(first) - vertices[0].y;
        f32 advance = offset_y * slope, at = advance + vertices[0].x;
        i32 origin = long_edge.x - (long_right ? 1 : 0);
        i32 raw_origin = origin;
        if (origin < 0)
          origin = 0;
        if (origin >= static_cast<i32>(device->view_width))
          origin = static_cast<i32>(device->view_width) - 1;
        origin_adjust = origin - raw_origin;
        f32 offset_x = (static_cast<f32>(origin) - at) + advance;
        for (u32 channel = 0; channel < 4; ++channel) {
          u32 shift = channel * 8;
          i32 initial = static_cast<i32>((vertex_colors[0] >> shift) & 255);
          f32 mid = static_cast<f32>((static_cast<i32>((vertex_colors[1] >> shift) & 255) - initial) * 256);
          f32 end = static_cast<f32>((static_cast<i32>((vertex_colors[2] >> shift) & 255) - initial) * 256);
          f32 x_gradient = sy * end - ly * mid, y_gradient = lx * mid - sx * end;
          if (!isfinite(x_gradient) || !isfinite(y_gradient) || fabsf(x_gradient) >= 32768.0f)
            return BdPortableRendererMissing();
          attributes[channel] = (offset_x * x_gradient + static_cast<f32>(initial * 256)) + offset_y * y_gradient;
          row_whole[channel] = static_cast<f32>(long_edge.whole) * x_gradient + y_gradient;
          row_carry[channel] = row_whole[channel] + (slope >= 0 ? x_gradient : -x_gradient);
          step_x[channel] = long_right ? -x_gradient : x_gradient;
        }
      }
      for (i32 row = first; row < last; ++row) {
        if (row == first || row == middle) {
          u32 start = row < middle ? 0 : 1;
          short_edge = BdMakeRasterEdge(vertices[start].x, vertices[start].y, vertices[start + 1].x,
              vertices[start + 1].y, row, false);
        }
        i32 left = long_edge.x < short_edge.x ? long_edge.x : short_edge.x;
        i32 right = long_edge.x > short_edge.x ? long_edge.x : short_edge.x;
        if (left < 0)
          left = 0;
        if (right > static_cast<i32>(device->view_width))
          right = static_cast<i32>(device->view_width);
        if (row >= 0 && row < static_cast<i32>(device->view_height)) {
          u8 *output = static_cast<u8 *>(device->surface->pixels) + static_cast<usize>(row) * device->surface->pitch;
          u16 attr[4] = {};
          i16 increment[4] = {};
          if (gradient) {
            i32 origin = long_edge.x - (long_right ? 1 : 0) + origin_adjust;
            i32 distance = long_right ? origin - (right - 1) : left - origin;
            for (u32 channel = 0; channel < 4; ++channel) {
              f32 value = attributes[channel] + static_cast<f32>(distance) * step_x[channel];
              if (!isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
                return BdPortableRendererMissing();
              attr[channel] = static_cast<u16>(static_cast<i32>(value));
              increment[channel] = static_cast<i16>(step_x[channel]);
            }
          }
          for (i32 column = left; column < right; ++column) {
            i32 selected = long_right && gradient ? right - 1 - (column - left) : column;
            u32 current = color;
            if (gradient) {
              current = 0;
              for (u32 channel = 0; channel < 4; ++channel) {
                current |= static_cast<u32>(attr[channel] >> 8) << (channel * 8);
                attr[channel] = static_cast<u16>(attr[channel] + increment[channel]);
              }
            }
            // The driver's stipple test reads the advanced alpha while color output still uses this pixel's channels.
            if (keying_enabled && (gradient ? attr[3] >> 8 : current >> 24) <=
                    kBdAlphaStipple[(row & 3) | ((selected & 3) << 2)])
              continue;
            if (argb32) {
              u8 *address = output + 4 * selected;
              u32 value = gradient ? current : pixel;
              if (device->blend == SDL_BLENDMODE_BLEND) {
                memcpy(&value, address, sizeof(value));
                value = BdBlendArgb32(value, current);
              }
              memcpy(address, &value, sizeof(value));
            } else {
              u8 *address = output + 2 * selected;
              u16 value = gradient ? static_cast<u16>(SDL_MapSurfaceRGBA(device->surface, static_cast<u8>(current >> 16),
                  static_cast<u8>(current >> 8), static_cast<u8>(current), static_cast<u8>(current >> 24)))
                                   : static_cast<u16>(pixel);
              if (device->blend == SDL_BLENDMODE_BLEND) {
                memcpy(&value, address, sizeof(value));
                value = device->surface->format == SDL_PIXELFORMAT_RGB565 ? BdBlendRgb565(value, current)
                                                                         : BdBlendRgb555(value, current);
              }
              memcpy(address, &value, sizeof(value));
            }
          }
        }
        i32 previous_x = long_edge.x;
        BdAdvanceRasterEdge(&long_edge);
        if (gradient)
          for (u32 channel = 0; channel < 4; ++channel)
            attributes[channel] += long_edge.x - previous_x == long_edge.whole ? row_whole[channel] : row_carry[channel];
        BdAdvanceRasterEdge(&short_edge);
      }
    }
    ++draw_calls;
    ++triangles;
    return 0;
  }
  SetColorState(&triangle->state);
  ++draw_calls;
  ++triangles;
  return device->renderer ? 0 : kBdRenderFailure;
}

// Submit the actual four XYZ/diffuse vertices as a triangle fan and count the request even if the SDK rejects it.
// Retain one original fan command stream while the published pixel bodies keep their source state counts.
i32 BD_FASTCALL BdRenderer::DrawQuad(BdColorQuad *quad) {
  BdColorVertex *v = quad->vertices;
  bool gradient = v[1].color != v[0].color || v[2].color != v[0].color || v[3].color != v[0].color;
  for (u32 index = 0; index < 4; ++index)
    if (v[index].z != 1.0f || !isfinite(v[index].x) || !isfinite(v[index].y))
      return BdPortableRendererMissing();
  bool rectangle = v[0].y == v[1].y && v[1].x == v[2].x && v[2].y == v[3].y && v[3].x == v[0].x &&
      v[0].x <= v[1].x && v[0].y <= v[3].y;
  if (device->surface && device->surface->format == SDL_PIXELFORMAT_ARGB8888)
    rectangle = false;
  if ((v[0].color >> 24) != 255 || (quad->state.flags & 0x400))
    rectangle = false;
  if (gradient)
    rectangle = false;
  if (rectangle && !device->identity_world)
    for (u32 index = 0; index < 4; ++index)
      if (v[index].x != floorf(v[index].x) || v[index].y != floorf(v[index].y))
        rectangle = false;
  if (!rectangle || (quad->state.flags & 0x48)) {
    device->commands.Begin();
    BdDualVertex packet[4];
    for (u32 index = 0; index < 4; ++index)
      packet[index] = {v[index].x, v[index].y, v[index].z, v[index].color, 0, 0, 0, 0};
    BdDualVertex first_packet[3] = {packet[1], packet[2], packet[0]};
    BdDualVertex second_packet[3] = {packet[2], packet[3], packet[0]};
    if (!device->DrawTextureTriangle(first_packet, true) || !device->DrawTextureTriangle(second_packet, true))
      return kBdRenderFailure;
    device->color_fan = true;
    BdColorQuad triangle = {};
    triangle.state = quad->state;
    triangle.vertices[0] = v[gradient ? 1 : 0];
    triangle.vertices[1] = v[gradient ? 2 : 1];
    triangle.vertices[2] = v[gradient ? 0 : 2];
    i32 first = DrawTriangle(&triangle);
    if (first) {
      device->color_fan = false;
      ++triangles;
      return first;
    }
    u32 changes = state_changes;
    triangle.vertices[0] = v[gradient ? 2 : 0];
    triangle.vertices[1] = v[gradient ? 3 : 2];
    triangle.vertices[2] = v[gradient ? 0 : 3];
    i32 second = DrawTriangle(&triangle);
    device->color_fan = false;
    state_changes = changes;
    --draw_calls;
    return second;
  }
  SetColorState(&quad->state);
  bool result = device->renderer && device->DrawPolygon(quad->vertices, 4);
  ++draw_calls;
  triangles += 2;
  return result ? 0 : kBdRenderFailure;
}

// Draw the original oversized flat quad in its own scene with an identity world transform, then restore the saved matrix.
// Draw the source's oversized background quad with an identity world transform, then restore the stored transform.
void BdRenderer::DrawBackground(u32 color) {
  if (!device)
    return;
  BeginScene();
  bool saved = device->identity_world;
  device->identity_world = true;
  f32 x = static_cast<f32>(static_cast<f64>(buffer->Width()) * 0.60000002f);
  f32 y = static_cast<f32>(static_cast<f64>(buffer->Height()) * 0.60000002f);
  BdColorQuad quad;
  quad.state = {3, 0, 0};
  quad.vertices[0] = {-x, -y, 1.0f, color};
  quad.vertices[1] = {x, -y, 1.0f, color};
  quad.vertices[2] = {x, y, 1.0f, color};
  quad.vertices[3] = {-x, y, 1.0f, color};
  DrawQuad(&quad);
  device->identity_world = saved;
  EndScene();
}

// Add the tint's centered RGB offsets to the supplied color, clamp each channel and replace alpha from the tint.
u32 BdTintColor(u32 *color, u32 tint) {
  i32 red = static_cast<i32>((*color >> 16) & 255) + static_cast<i32>((tint >> 16) & 255) - 127,
      green = static_cast<i32>((*color >> 8) & 255) + static_cast<i32>((tint >> 8) & 255) - 127,
      blue = static_cast<i32>(*color & 255) + static_cast<i32>(tint & 255) - 127;
  if (red > 255)
    red = 255;
  if (blue > 255)
    blue = 255;
  if (green > 255)
    green = 255;
  if (red < 0)
    red = 0;
  if (green < 0)
    green = 0;
  if (blue < 0)
    blue = 0;
  *color = static_cast<u32>(blue) | (tint & kGxArgbAlphaMask) | (static_cast<u32>(green) << 8) |
      (static_cast<u32>(red) << 16);
  return *color;
}

// Select the face's rotated lighting entry, apply its tint and submit the actual three- or four-vertex color packet.
// A nonopaque tint sets the source alpha flag; the fourth vertex of a triangle remains unused by the SDK.
i32 BD_FASTCALL BdRenderer::DrawFace(const BdFace *face, u32 tint, u8 rotation) {
  u32 flags = static_cast<u8>(face->flags);
  u32 color = colors[(flags & 0xF8) | ((flags + 2 * rotation) & 7)];
  BdTintColor(&color, tint);
  bool triangle = face->vertex[3] == -1;
  u32 count = triangle ? 3 : 4;
  BdColorQuad quad;
  quad.state = {0x800u | (triangle ? 2u : 3u) | ((tint >> 24) != 255 ? 0x40u : 0u), 0, 0};
  for (u32 i = 0; i < count; ++i) {
    const BdPointF *point = projected + face->vertex[i];
    quad.vertices[i] = {point->x, point->y, 1.0f, color};
  }
  return triangle ? DrawTriangle(&quad) : DrawQuad(&quad);
}

// Publish the face's attribute order and submit its projected object polygon when a primary asset is present.
void BD_FASTCALL BdRenderer::DrawObjectFace(
    const BdFace *face, BdFaceTextures *textures, u8 rotation, i32, u8 use_color, u32 color) {
  textures->order = face->attributes;
  if (!use_color)
    color = colors[0x38 | ((2 * rotation - 4) & 7)];
  u32 selected = textures->primary ? 1 : 0;
  if (textures->secondary)
    selected |= 2;
  if (selected & 1) {
    u32 count = face->vertex[3] == -1 ? 3 : 4;
    BdTextureQuad quad;
    quad.state = {0xE00u | (count == 3 ? 2u : 3u), nullptr, nullptr};
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *point = projected + face->vertex[i];
      quad.vertices[i] = {point->x, point->y, 1.0f, color, 0.0f, 0.0f};
    }
    if (selected == 1)
      BuildObjectUV(&quad, face, textures);
    if (!use_sdk)
      DrawTextured(&quad);
  }
}

// Publish face mapping, select or shade its color by height and construct the actual single- or dual-texture packet.
// Coordinate construction still runs before the original optional-SDK guard suppresses legacy submission.
void BD_FASTCALL BdRenderer::DrawTexturedFace(
    const BdFace *face, BdFaceTextures *textures, u8 rotation, i16 elevation, u8 use_color, u32 color) {
  textures->order = face->attributes;
  if (!use_color) {
    u32 flags = static_cast<u8>(face->flags);
    color = colors[(flags & 0xF8) | ((flags + 2 * rotation) & 7)];
  }
  BdColor shaded = {color};
  if (elevation < 8) {
    f32 factor = static_cast<f32>(1.0 - static_cast<f64>(8 - elevation) * 0.025f);
    shaded.Scale(factor);
  }
  if (!textures->primary && !textures->secondary)
    return;
  u32 count = face->vertex[3] == -1 ? 3 : 4;
  u32 flags = 0x800u | (count == 3 ? 2u : 3u) | ((shaded.value >> 24) != 255 ? 8u : 0u);
  if (textures->primary && textures->secondary) {
    BdDualQuad quad;
    quad.state = {count == 3 ? 2u : 3u, nullptr, nullptr};
    BuildDualUV(&quad, textures);
    quad.state.flags |= flags;
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *point = projected + face->vertex[i];
      quad.vertices[i].x = point->x;
      quad.vertices[i].y = point->y;
      quad.vertices[i].z = 1.0f;
      quad.vertices[i].color = shaded.value;
    }
    if (!use_sdk)
      DrawDual(&quad);
  } else if (textures->primary || textures->secondary) {
    BdTextureQuad quad;
    quad.state = {count == 3 ? 2u : 3u, nullptr, nullptr};
    BuildSingleUV(&quad, textures);
    quad.state.flags |= flags;
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *point = projected + face->vertex[i];
      quad.vertices[i].x = point->x;
      quad.vertices[i].y = point->y;
      quad.vertices[i].z = 1.0f;
      quad.vertices[i].color = shaded.value;
    }
    if (!use_sdk)
      DrawTextured(&quad);
  }
}

// Draw the face's three or four perimeter segments after the source blend/stage disabling sequence.
// The original FVF includes unused UV fields; initialize those explicitly while preserving all defined vertex data.
// Retain one source line-list header while the published perimeter segments disable blending and texture stages.
i32 BD_FASTCALL BdRenderer::DrawEdges(const BdFace *face, u32 color, i32) {
  u32 count = face->vertex[3] == -1 ? 3 : 4;
  BdColorVertex segments[8];
  for (u32 index = 0; index < count; ++index) {
    const BdPointF *first = projected + face->vertex[index], *second = projected + face->vertex[(index + 1) % count];
    segments[2 * index] = {first->x, first->y, 1.0f, color};
    segments[2 * index + 1] = {second->x, second->y, 1.0f, color};
    if (!device->SupportsLine(segments + 2 * index))
      return BdPortableRendererMissing();
  }
  if (blend_enabled) {
    blend_enabled = 0;
    device->alpha_enabled = false;
    device->SetBlend(false, false);
    state_changes += 3;
  }
  if (stage0_enabled || stage1_enabled) {
    stage0_enabled = stage1_enabled = 0;
    state_changes += 2;
  }
  bool result = device->renderer != nullptr;
  bool cpu = keying_enabled || (device->surface && device->surface->format == SDL_PIXELFORMAT_ARGB8888 &&
                                   (color >> 24) != 255);
  device->commands.Begin();
  device->line_list = true;
  device->line_header = false;
  for (u32 index = 0; index < count && result; ++index) {
    BdColorVertex *line = segments + 2 * index;
    bool degenerate = line[0].x == line[1].x && line[0].y == line[1].y;
    if (!degenerate)
      result = cpu ? device->DrawCpuLine(line, keying_enabled) : device->DrawLine(line);
  }
  device->line_list = false;
  ++draw_calls;
  ++lines;
  return result ? 0 : kBdRenderFailure;
}

static bool BdFaceEdge(const BdPointF *point, const BdPointF *first, const BdPointF *second) {
  return (static_cast<f64>(point->y) - first->y) * (static_cast<f64>(second->x) - first->x) >=
      (static_cast<f64>(second->y) - first->y) * (static_cast<f64>(point->x) - first->x);
}

static bool BdHitTriangle(
    const BdPointF *point, const BdPointF *first, const BdPointF *second, const BdPointF *third) {
  return BdFaceEdge(point, first, second) && BdFaceEdge(point, second, third) && BdFaceEdge(point, third, first);
}

// Test a point against the four ordered edges of a projected quadrilateral.
static bool BdHitQuad(
    const BdPointF *point, const BdPointF *first, const BdPointF *second,
    const BdPointF *third, const BdPointF *fourth) {
  return BdFaceEdge(point, first, second) && BdFaceEdge(point, second, third) &&
      BdFaceEdge(point, third, fourth) && BdFaceEdge(point, fourth, first);
}

// Test a face against the renderer's real scaled vertices after applying the stored cell rotation.
// The original signed fourth-vertex sentinel selects its triangle or quadrilateral predicate.
bool BD_FASTCALL BdRenderer::HitFace(const BdFace *face, const BdPointF *point, u8 rotation) {
  const u8 *mapping = BdCellRotations[rotation];
  const BdPointF *first = &scaled[mapping[face->vertex[0]]], *second = &scaled[mapping[face->vertex[1]]],
      *third = &scaled[mapping[face->vertex[2]]];
  if (face->vertex[3] == -1)
    return BdHitTriangle(point, first, second, third);
  return BdHitQuad(point, first, second, third, &scaled[mapping[face->vertex[3]]]);
}

// Negate the supplied local point and pick the first containing face in the cell's original draw order.
// Return the actual signed face byte, including minus one when no listed face contains the point.
i8 BD_FASTCALL BdRenderer::PickCellFace(BdPointF *point, const BdCell *cell) {
  u32 packed = cell->link_height_kind, kind = (packed >> kBdCellKindShift) & kBdCellKindValueMask,
      rotation = packed >> kBdCellRotationShift;
  point->x = -point->x;
  point->y = -point->y;
  const i8 *order = BdCellFaceOrder[kind][rotation];
  while (*order != -1) {
    i8 index = *order;
    if (HitFace(&BdCellFaces[kind][index], point, static_cast<u8>(rotation)))
      return index;
    ++order;
  }
  return -1;
}

// Decode only the original explicit face directions into horizontal or vertical plane bits.
// Other flag bytes return zero; the receiver is part of the actual fastcall contract but is not read.
u8 BD_FASTCALL BdRenderer::ClassifyPlane(u8 direction) {
  switch (direction) {
  case 1:
  case 3:
    return 1;
  case 5:
  case 7:
    return 2;
  case 16:
  case 56:
    return 4;
  default:
    return 0;
  }
}

// Draw visible cell faces plus requested plane classes using the original fallback colors and optional edges.
// The classification cursor advances consecutively from the first ordered face while drawing still uses each order index.
void BD_FASTCALL BdRenderer::DrawMaskedCell(const BdPoint *screen, const BdCell *cell, u8 planes) {
  u8 rotation = static_cast<u8>(cell->link_height_kind >> kBdCellRotationShift);
  ProjectCell(screen, rotation);
  u32 kind = (cell->link_height_kind >> kBdCellKindShift) & kBdCellKindValueMask, color = kBdDefaultFaceColor;
  if (kind == 8)
    color = kBdGroundFaceColor;
  else if (kind == 15) {
    if (!draw_empty_objects)
      return;
    color = kBdEmptyObjectFaceColor;
  }
  u32 mask = cell->face_mask;
  const i8 *order = BdCellFaceOrder[kind][rotation];
  i32 classification = *order;
  BdColor edge = {color};
  edge.Darken(32);
  for (; *order != -1; ++order, ++classification) {
    u32 index = static_cast<u8>(*order);
    if ((mask & (1u << index)) || (ClassifyPlane(static_cast<u8>(BdCellFaces[kind][classification].flags)) & planes)) {
      const BdFace *face = &BdCellFaces[kind][index];
      DrawFace(face, color, rotation);
      if (draw_edges)
        DrawEdges(face, edge.value, 1);
    }
  }
}

// Build a four-corner color plane from the original two integer vectors, scale and separate color argument.
// Use wrapping arithmetic before float conversion and preserve the quad-specific alpha flag.
i32 BD_FASTCALL BdRenderer::DrawPlane(
    const BdPoint *origin, i32 first_x, i32 first_y, i32 second_x, i32 second_y, i32 step, u32 color) {
  u32 x = static_cast<u32>(origin->x), y = static_cast<u32>(origin->y), multiplier = static_cast<u32>(step);
  u32 ax = static_cast<u32>(first_x), ay = static_cast<u32>(first_y);
  u32 bx = static_cast<u32>(second_x), by = static_cast<u32>(second_y);
  BdColorQuad quad;
  quad.state = {(color >> 24) == 255 ? 3u : 0x43u, 0, 0};
  quad.vertices[0] = {static_cast<f32>(origin->x), static_cast<f32>(origin->y), 1.0f, color};
  quad.vertices[1] = {static_cast<f32>(static_cast<i32>(x + multiplier * ax)),
      static_cast<f32>(static_cast<i32>(y + multiplier * ay)), 1.0f, color};
  quad.vertices[2] = {static_cast<f32>(static_cast<i32>(x + multiplier * (ax + bx))),
      static_cast<f32>(static_cast<i32>(y + multiplier * (ay + by))), 1.0f, color};
  quad.vertices[3] = {static_cast<f32>(static_cast<i32>(x + multiplier * bx)),
      static_cast<f32>(static_cast<i32>(y + multiplier * by)), 1.0f, color};
  return DrawQuad(&quad);
}

// Submit the original two-point color line without lighting or projection-table lookups.
// Preserve its separate nonopaque-line flag, which differs from the color-plane flag.
i32 BD_FASTCALL BdRenderer::DrawSegment(const BdPoint *first, const BdPoint *second, u32 color) {
  BdColorLine line;
  line.state = {(color >> 24) == 255 ? 1u : 9u, 0, 0};
  line.vertices[0] = {static_cast<f32>(first->x), static_cast<f32>(first->y), 1.0f, color};
  line.vertices[1] = {static_cast<f32>(second->x), static_cast<f32>(second->y), 1.0f, color};
  return DrawLine(&line);
}

// Submit the four separate integer screen points as an unlit color quad in source order.
// Unlike the volume-plane primitive, nonopaque screen quads use flag11 without the extra plane state bit.
i32 BD_FASTCALL BdRenderer::DrawScreenQuad(
    const BdPoint *first, const BdPoint *second, const BdPoint *third, const BdPoint *fourth, u32 color) {
  BdColorQuad quad;
  quad.state = {(color >> 24) == 255 ? 3u : 11u, 0, 0};
  quad.vertices[0] = {static_cast<f32>(first->x), static_cast<f32>(first->y), 1.0f, color};
  quad.vertices[1] = {static_cast<f32>(second->x), static_cast<f32>(second->y), 1.0f, color};
  quad.vertices[2] = {static_cast<f32>(third->x), static_cast<f32>(third->y), 1.0f, color};
  quad.vertices[3] = {static_cast<f32>(fourth->x), static_cast<f32>(fourth->y), 1.0f, color};
  return DrawQuad(&quad);
}

// Build the original three-corner tile packet from the renderer's scaled point table.
// The final y coordinate uses the source's stored-float base, independently of the earlier double additions.
i32 BD_FASTCALL BdRenderer::DrawTileTriangle(const BdPoint *screen, i32 first, i32 second, i32 third, u32 color) {
  BdColorQuad quad;
  quad.state = {(color >> 24) == 255 ? 2u : 10u, 0, 0};
  f64 x = screen->x, y = screen->y;
  f32 last_y = static_cast<f32>(y);
  quad.vertices[0] = {static_cast<f32>(x + scaled[first].x), static_cast<f32>(y + scaled[first].y), 1.0f, color};
  quad.vertices[1] = {static_cast<f32>(x + scaled[second].x), static_cast<f32>(y + scaled[second].y), 1.0f, color};
  quad.vertices[2] = {
      static_cast<f32>(x + scaled[third].x), static_cast<f32>(static_cast<f64>(last_y) + scaled[third].y), 1.0f, color};
  return DrawTriangle(&quad);
}

// Build a four-corner tile packet with the original opaque or blended color state.
// Preserve the distinct stored-float y base for the fourth vertex and return the actual device submission result.
i32 BD_FASTCALL BdRenderer::DrawTileQuad(
    const BdPoint *screen, i32 first, i32 second, i32 third, i32 fourth, u32 color) {
  BdColorQuad quad;
  quad.state = {(color >> 24) == 255 ? 3u : 11u, 0, 0};
  f64 x = screen->x, y = screen->y;
  f32 last_y = static_cast<f32>(y);
  quad.vertices[0] = {static_cast<f32>(x + scaled[first].x), static_cast<f32>(y + scaled[first].y), 1.0f, color};
  quad.vertices[1] = {static_cast<f32>(x + scaled[second].x), static_cast<f32>(y + scaled[second].y), 1.0f, color};
  quad.vertices[2] = {static_cast<f32>(x + scaled[third].x), static_cast<f32>(y + scaled[third].y), 1.0f, color};
  quad.vertices[3] = {static_cast<f32>(x + scaled[fourth].x),
      static_cast<f32>(static_cast<f64>(last_y) + scaled[fourth].y), 1.0f, color};
  return DrawQuad(&quad);
}

// Fill the bottom shadow's selected triangular regions, or its entire quadrilateral for the raw FF mask.
// Keep triangle ordering and repeated coverage exactly as the original byte-mask dispatch.
void BD_FASTCALL BdRenderer::DrawBottomShadow(const BdPoint *screen, u8 mask, u32 color) {
  if (mask == 255) {
    DrawTileQuad(screen, 4, 7, 6, 5, color);
    return;
  }
  if (mask & 0x10)
    DrawTileTriangle(screen, 5, 4, 7, color);
  if (mask & 0x20)
    DrawTileTriangle(screen, 4, 6, 5, color);
  if (mask & 0x40)
    DrawTileTriangle(screen, 5, 7, 6, color);
  if (mask & 0x80)
    DrawTileTriangle(screen, 4, 7, 6, color);
}

// Fill the left shadow using the original four triangular coverage bits and full-mask shortcut.
// The source submits the two high-bit triangles before either low-bit triangle.
void BD_FASTCALL BdRenderer::DrawLeftShadow(const BdPoint *screen, u8 mask, u32 color) {
  if (mask == 255) {
    DrawTileQuad(screen, 0, 4, 5, 1, color);
    return;
  }
  if (mask & 0x20)
    DrawTileTriangle(screen, 4, 5, 1, color);
  if (mask & 0x10)
    DrawTileTriangle(screen, 0, 4, 5, color);
  if (mask & 1)
    DrawTileTriangle(screen, 1, 0, 4, color);
  if (mask & 2)
    DrawTileTriangle(screen, 5, 1, 0, color);
}

// Fill the right shadow in high-bit, middle-bit, and low-bit source order.
// A raw FF byte selects the complete quad instead of four overlapping triangles.
void BD_FASTCALL BdRenderer::DrawRightShadow(const BdPoint *screen, u8 mask, u32 color) {
  if (mask == 255) {
    DrawTileQuad(screen, 0, 3, 7, 4, color);
    return;
  }
  if (mask & 0x80)
    DrawTileTriangle(screen, 3, 7, 4, color);
  if (mask & 0x10)
    DrawTileTriangle(screen, 0, 7, 4, color);
  if (mask & 1)
    DrawTileTriangle(screen, 0, 3, 4, color);
  if (mask & 8)
    DrawTileTriangle(screen, 0, 3, 7, color);
}

// Apply the recovered blend/stage sequence and bind retained asset textures through their real interface.
// Preserve the dual-stage counter's original undercount and clear descriptor flags when an asset pointer is absent.
// Apply blend, key and one- or two-stage color operations using real retained CPU textures.
// Missing primary assets still clear their descriptor bit after the original stage-state sequence.
void BD_FASTCALL BdRenderer::SetTextureState(BdTextureState *state) {
  device->texture_flags = state->flags;
  device->texture_blend = 0;
  if (state->flags & kBdTextureColorKey) {
    if (!blend_enabled) {
      blend_enabled = 1;
      device->alpha_enabled = false;
      ++state_changes;
    }
    SetColorKey(1);
    device->SetBlend(device->alpha_enabled, false);
    state_changes += 2;
  } else if (state->flags & (kBdTextureDiffuseAlpha | kBdTextureSourceAlpha |
      kBdTexturePrimaryOpacity | kBdTextureSecondaryOpacity)) {
    if (!blend_enabled) {
      blend_enabled = 1;
      device->alpha_enabled = true;
      ++state_changes;
    }
    SetColorKey(0);
    device->SetBlend(device->alpha_enabled, true);
    if (device->alpha_enabled)
      device->texture_blend = state->flags & kBdTextureSourceAlpha ? 2 : 1;
    state_changes += 2;
  } else if (blend_enabled) {
    SetColorKey(0);
    blend_enabled = 0;
    device->alpha_enabled = false;
    device->SetBlend(false, false);
    state_changes += 3;
  }
  device->texture_keyed = keying_enabled != 0;
  u32 stages = ((state->flags & kBdTexturePrimary) != 0) | (2 * ((state->flags & kBdTextureSecondary) != 0));
  device->texture_signed_add = stages == 3 || (state->flags & kBdTextureSignedAdd) != 0;
  if (stages == 3) {
    state_changes += 2 + (state->flags & kBdTextureDiffuseAlpha ? 3 : 1) + 2;
  } else if (stages) {
    state_changes += state->flags & (kBdTextureSignedAdd | kBdTextureModulate) ? 3 : 2;
    state_changes += state->flags & kBdTextureDiffuseAlpha ? 3 : state->flags & kBdTextureModulate ? 2 : 1;
    if (stage1_enabled)
      state_changes += 2;
  } else if (stage0_enabled || stage1_enabled) {
    state_changes += 2;
  }
  stage0_enabled = (state->flags & kBdTexturePrimary) != 0 && state->primary;
  stage1_enabled = (state->flags & kBdTextureSecondary) != 0 && state->secondary;
  BdCpuTexture *texture = nullptr;
  if (stage0_enabled) {
    state->primary->GetTexture(&texture);
    device->BindTexture(0, texture);
    ++texture_binds;
    if (texture)
      texture->Release();
  } else {
    device->BindTexture(0, nullptr);
    state->flags &= ~kBdTexturePrimary;
  }
  if (stage1_enabled) {
    texture = nullptr;
    state->secondary->GetTexture(&texture);
    device->BindTexture(1, texture);
    ++texture_binds;
    if (texture)
      texture->Release();
  } else {
    device->BindTexture(1, nullptr);
    state->flags &= ~kBdTextureSecondary;
  }
  device->texture_two_stage = device->textures[0] && device->textures[1];
  device->texture_dual_combiner = stages == 3;
  device->texture_diffuse_only = !device->textures[0] || (state->flags & kBdTextureDiffuseAlpha);
  device->texture_alpha_from_texture = stages != 3 && (state->flags & kBdTextureModulate);
}

// Submit the original four-slot XYZ/diffuse/UV packet using its low-three-bit primitive selector after state mutation.
// Preserve SDK HRESULTs and the original arithmetic result for an unrecognized selector.
// The private source return declaration remains unresolved.
// Submit single-coordinate textured primitives with the selected filter, key and blend factors.
// Empty assets and storage retain the original diffuse fallback without reapplying color state.
i32 BD_FASTCALL BdRenderer::DrawTextured(BdTextureQuad *quad) {
  SetTextureState(&quad->state);
  u32 primitive = quad->state.flags & 7;
  if (primitive < 1 || primitive > 3)
    return static_cast<i32>(primitive) - 3;
  if (device->texture_two_stage && !device->texture_diffuse_only)
    return BdPortableRendererMissing();
  BdDualVertex vertices[4];
  u32 count = primitive == 1 ? 2 : primitive == 2 ? 3 : 4;
  for (u32 index = 0; index < count; ++index) {
    const BdTextureVertex &vertex = quad->vertices[index];
    vertices[index] = {vertex.x, vertex.y, vertex.z, vertex.color,
        device->texture_diffuse_only ? 0 : vertex.u, device->texture_diffuse_only ? 0 : vertex.v, 0, 0};
  }
  bool result = device->DrawTexturePrimitive(vertices, primitive);
  ++draw_calls;
  if (primitive == 1)
    ++lines;
  else
    triangles += primitive == 3 ? 2 : 1;
  return result ? 0 : kBdRenderFailure;
}

// Submit the original XYZ/diffuse/two-UV packet, retaining the same primitive selection and counter effects.
// Both coordinate sets remain in the original32-byte vertex stride, including when the state selects only one texture.
// Missing storage selects the original effective stage count without changing the descriptor publication order.
i32 BD_FASTCALL BdRenderer::DrawDual(BdDualQuad *quad) {
  SetTextureState(&quad->state);
  u32 primitive = quad->state.flags & 7;
  if (primitive < 1 || primitive > 3)
    return static_cast<i32>(primitive) - 3;
  bool result = device->DrawTexturePrimitive(quad->vertices, primitive);
  ++draw_calls;
  if (primitive == 1)
    ++lines;
  else
    triangles += primitive == 3 ? 2 : 1;
  return result ? 0 : kBdRenderFailure;
}

BdPointF BdTextureCorners[9];

// Publish the original nine atlas corner pairs from the packed mapping byte and actual inverse image dimensions.
void BD_FASTCALL BdBuildTextureCorners(u8 mapping, f32 inverse_width, f32 inverse_height) {
  u32 column = mapping & 0xF0, row = 16 * (mapping & 15);
  f32 left = static_cast<f32>(static_cast<f64>(column) * inverse_width),
      right = static_cast<f32>(static_cast<f64>(column + 16) * inverse_width),
      top = static_cast<f32>(static_cast<f64>(row) * inverse_height),
      bottom = static_cast<f32>(static_cast<f64>(row + 16) * inverse_height);
  BdTextureCorners[0] = {left, top};
  BdTextureCorners[1] = {right, top};
  BdTextureCorners[2] = {right, bottom};
  BdTextureCorners[3] = {left, bottom};
  BdTextureCorners[4] = {left, 0.0f};
  BdTextureCorners[5] = {0.0f, top};
  BdTextureCorners[6] = {right, 0.0f};
  BdTextureCorners[7] = {0.0f, bottom};
  BdTextureCorners[8] = {0.0f, 0.0f};
}

// Select the primary asset when present, otherwise use the secondary asset in the first texture stage.
void BD_FASTCALL BdRenderer::BuildSingleUV(BdTextureQuad *quad, const BdFaceTextures *textures) {
  u32 count = (quad->state.flags & 7) + 1;
  if (textures->primary) {
    BdAsset *asset = textures->primary;
    quad->state.flags |= 0x10;
    quad->state.flags = (quad->state.flags & ~0x400u) | (asset->flags & 2 ? 0x400u : 0u);
    quad->state.primary = asset;
    quad->state.flags = (quad->state.flags & ~0x100u) | (asset->flags & 1 ? 0u : 0x100u);
    BdBuildTextureCorners(textures->primary_map, asset->inverse_width, asset->inverse_height);
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *corner = BdTextureCorners + textures->order[i];
      quad->vertices[i].u = corner->x;
      quad->vertices[i].v = corner->y;
    }
  } else if (textures->secondary) {
    BdAsset *asset = textures->secondary;
    quad->state.flags |= 0x10;
    quad->state.flags = (quad->state.flags & ~0x400u) | (asset->flags & 2 ? 0x400u : 0u);
    quad->state.primary = asset;
    quad->state.flags = (quad->state.flags & ~0x80u) | (asset->flags & 1 ? 0u : 0x80u);
    BdBuildTextureCorners(textures->secondary_map, asset->inverse_width, asset->inverse_height);
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *corner = BdTextureCorners + textures->order[i];
      quad->vertices[i].u = corner->x;
      quad->vertices[i].v = corner->y;
    }
  }
}

// The second asset deliberately replaces the shared key flag rather than combining the two assets' key properties.
void BD_FASTCALL BdRenderer::BuildDualUV(BdDualQuad *quad, const BdFaceTextures *textures) {
  u32 count = (quad->state.flags & 7) + 1;
  if (textures->primary) {
    BdAsset *asset = textures->primary;
    quad->state.flags |= 0x10;
    quad->state.primary = asset;
    quad->state.flags = (quad->state.flags & ~0x400u) | (asset->flags & 2 ? 0x400u : 0u);
    BdBuildTextureCorners(textures->primary_map, asset->inverse_width, asset->inverse_height);
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *corner = BdTextureCorners + textures->order[i];
      quad->vertices[i].u = corner->x;
      quad->vertices[i].v = corner->y;
    }
  }
  if (textures->secondary) {
    BdAsset *asset = textures->secondary;
    quad->state.flags |= 0x20;
    quad->state.secondary = asset;
    quad->state.flags = (quad->state.flags & ~0x400u) | (asset->flags & 2 ? 0x400u : 0u);
    BdBuildTextureCorners(textures->secondary_map, asset->inverse_width, asset->inverse_height);
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *corner = BdTextureCorners + textures->order[i];
      quad->vertices[i].second_u = corner->x;
      quad->vertices[i].second_v = corner->y;
    }
  }
}

// Keep the source's separate float spill for the horizontal origin and its later half-unit addition on the vertical
// path.
void BD_FASTCALL BdRenderer::BuildObjectUV(BdTextureQuad *quad, const BdFace *face, const BdFaceTextures *textures) {
  u32 count = (quad->state.flags & 7) + 1;
  if (textures->primary) {
    BdAsset *asset = textures->primary;
    f64 inverse_width = asset->inverse_width, inverse_height = asset->inverse_height;
    f64 left = static_cast<f64>(16 * textures->primary_map) * inverse_width;
    u32 row = 8 * textures->secondary_map;
    quad->state.flags |= 0x10;
    quad->state.flags = (quad->state.flags & ~0x400u) | (asset->flags & 2 ? 0x400u : 0u);
    quad->state.primary = asset;
    for (u32 i = 0; i < count; ++i) {
      const BdPointF *point = object_uv + face->vertex[i];
      f32 origin = static_cast<f32>(left + 0.5);
      quad->vertices[i].u = static_cast<f32>(inverse_width * point->x * 16.0 + origin);
      quad->vertices[i].v = static_cast<f32>(inverse_height * point->y * 16.0 + row * inverse_height + 0.5);
    }
  }
}

#undef BD_UNAVAILABLE_NOINLINE
