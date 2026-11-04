#include "application/animation_player.h"
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <dsound.h>
#undef GetObject
#undef PlaySound
#include <process.h>
#include <unknwn.h>
#endif
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include "graphics/sdl_platform.h"
#include "application/application_core.h"
#include "audio/device.h"
#include "audio/sample.h"
#include "common/api.h"
#include "common/path.h"
#include "common/rect.h"
#include "common/sc3_error.h"
#include "common/sc3_qfs.h"
#include "graphics/api.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_graphics.h"
#include "graphics/sc3_general_buffer.h"
#include "resources/file.h"

static i32 ApVideoTqiRound(f64 value) {
  f64 biased = value + kScDoubleNearestIntegerBias;
  u64 bits;
  memcpy(&bits, &biased, sizeof(bits));
  return static_cast<i32>(bits);
}

// Apply one eight-value transform pass using the original fixed and floating-point coefficients.
static void ApVideoTqiTransformPass(const i32 *input, i32 *output, i32 step, bool final_pass) {
  auto add = [](i32 a, i32 b) { return static_cast<i32>(static_cast<u32>(a) + static_cast<u32>(b)); };
  auto subtract = [](i32 a, i32 b) { return static_cast<i32>(static_cast<u32>(a) - static_cast<u32>(b)); };
  auto twice_high = [](i32 value) {
    i64 product = static_cast<i64>(value) * kApVideoInverseSqrtTwoQ31;
    return static_cast<i32>(static_cast<u32>(product >> 32) << 1);
  };
  i32 odd_sum = add(input[3], input[5]), odd_pair = add(input[1], input[7]);
  i32 odd_total = add(odd_sum, odd_pair), fixed_odd = twice_high(subtract(odd_pair, odd_sum));
  i32 left = subtract(input[1], input[7]), right = subtract(input[5], input[3]);
  f64 combined = (static_cast<f64>(left) + static_cast<f64>(right)) * static_cast<f32>(0.3826834261417389);
  i32 rounded_left = ApVideoTqiRound(static_cast<f64>(left) * static_cast<f32>(1.3065630197525024) - combined);
  i32 rounded_right = ApVideoTqiRound(static_cast<f64>(right) * static_cast<f32>(0.5411961078643799) + combined);
  i32 odd0 = add(odd_total, rounded_left), odd1 = add(fixed_odd, rounded_right);
  i32 odd2 = add(fixed_odd, rounded_left), even_sum = add(input[2], input[6]);
  i32 fixed_even = twice_high(subtract(input[2], input[6]));
  i32 even_total = add(input[0], input[4]), even_difference = subtract(input[0], input[4]);
  i32 even0 = add(even_total, add(even_sum, fixed_even));
  i32 even3 = subtract(even_total, add(even_sum, fixed_even));
  i32 even1 = add(even_difference, fixed_even), even2 = subtract(even_difference, fixed_even);
  i32 values[8] = {add(even0, odd0), add(even1, odd2), add(even2, odd1), add(even3, rounded_right),
      subtract(even3, rounded_right), subtract(even2, odd1), subtract(even1, odd2), subtract(even0, odd0)};
  for (i32 i = 0; i < 8; ++i)
    output[i * step] = final_pass ? values[i] >> 17 : values[i];
}

// Invert a quantized TQI block in the same two transform passes used by the installed decoder.
// Floating-point precision and the assembly-only return conventions require original-code comparison.
[[maybe_unused]] static void ApVideoTqiInverseTransform(const i32 *coefficients, i32 *output, i32 row_stride) {
  i32 intermediate[72];
  for (i32 row = 0; row < 8; ++row)
    ApVideoTqiTransformPass(coefficients + row * 8, intermediate + row, 9, false);
  for (i32 row = 0; row < 8; ++row)
    ApVideoTqiTransformPass(intermediate + row * 9, output + row * row_stride, 1, true);
}

// Combine one older-format luma and chroma macroblock into the original packed YUV output rows.
[[maybe_unused]] static i32 ApVideoPackOldMacroblock(const i32 *luma, const i32 *chroma, u8 *output, i32 pitch) {
  const u8 *clip = reinterpret_cast<const u8 *>(ApVideoClipTable);
  const u8 *y = reinterpret_cast<const u8 *>(luma), *uv = reinterpret_cast<const u8 *>(chroma);
  u32 packed = 0;
  for (i32 row = 0; row < 16; ++row) {
    const u8 *colors = uv + (row >> 1) * 32;
    for (i32 pair = 0; pair < 8; ++pair) {
      u8 first = clip[y[2 + pair * 8]], second = clip[y[6 + pair * 8]];
      u8 blue = clip[colors[2 + pair * 4]], red = clip[colors[258 + pair * 4]];
      packed = (static_cast<u32>(first) << 24) | (static_cast<u32>(second) << 16) |
          (static_cast<u32>(blue) << 8) | red;
      memcpy(output + 4 * pair, &packed, sizeof(packed));
    }
    y += 64;
    output += pitch;
  }
  return static_cast<i32>(packed);
}

// Invert the older format's current coefficient block into the caller's strided transform buffer.
[[maybe_unused]] static i32 ApVideoInverseOldBlock(i32 *output, i32 stride) {
  i32 intermediate[72];
  for (i32 row = 0; row < 8; ++row)
    ApVideoTqiTransformPass(ApVideoOldCoefficients + row * 8, intermediate + row, 9, false);
  for (i32 row = 0; row < 8; ++row)
    ApVideoTqiTransformPass(intermediate + row * 9, output + row * stride, 1, false);
  return output[7 * stride + 6];
}

// Decode one older-format macroblock using its reference pixels or six transform-coded blocks.
[[maybe_unused]] static i32 ApVideoDecodeOldMacroblock(u8 *reference, u8 *output, i32 width) {
  i32 half_width = width >> 1, phase = 0;
  u8 flags = 0;
  if (ApVideoDifferentialFrame && (ApVideoBitValue & kApVideoOldFrameModeMask)) {
    if (ApVideoBitValue & kApVideoStreamTopBit) {
      flags = 0xff;
      ApVideoConsumeBits(1);
    } else {
      flags = static_cast<u8>(ApVideoBitValue >> 24);
      ApVideoConsumeBits(8);
    }
    i32 x = ApVideoReadShortValue(), y = ApVideoReadShortValue();
    phase = x & 1;
    if (flags & 0x3f) {
      assert(reference);
      reference += 4 * ((x >> 1) + half_width * y);
    }
  } else if (ApVideoDifferentialFrame) {
    ApVideoConsumeBits(2);
  }
  for (i32 block = 0; block < 4; ++block) {
    i32 *destination = ApVideoOldLuma + (block & 1 ? 8 : 0) + (block & 2 ? 128 : 0);
    if (flags & (1 << block)) {
      i32 offset = (block & 1 ? 16 : 0) + (block & 2 ? 32 * half_width : 0);
      ApVideoCopyLumaBlock(reference + offset, phase, half_width, reinterpret_cast<u8 *>(destination),
          static_cast<i8>(ApVideoReadShortValue() - 64));
    } else if (ApVideoReadOldBlock() == 1) {
      ApVideoFillDcBlock(reinterpret_cast<u32 *>(destination), 16);
    } else {
      ApVideoInverseOldBlock(destination, 16);
    }
  }
  for (i32 block = 0; block < 2; ++block) {
    i32 *destination = ApVideoOldChroma + block * 64;
    if (flags & (0x10 << block)) {
      ApVideoCopyChromaBlock(reference, block ? 0 : 1, half_width, reinterpret_cast<u8 *>(destination),
          static_cast<i8>(ApVideoReadShortValue() - 64));
    } else if (ApVideoReadOldBlock() == 1) {
      ApVideoFillDcBlock(reinterpret_cast<u32 *>(destination), 8);
    } else {
      ApVideoInverseOldBlock(destination, 8);
    }
  }
  return ApVideoPackOldMacroblock(ApVideoOldLuma, ApVideoOldChroma, output, 4 * half_width);
}

// Decode an older-format movie frame into its circular reference buffers and sixteen-bit destination.
// The frame, retained reference buffers, and destination must cover every byte the decoder actually accesses.
[[maybe_unused]] static i32 ApVideoDecodeOldFrame(ApVideoState *state, const u8 *frame, u32 *output, i32 pitch,
    i32 kind) {
  u32 width = state->options->video_width, height = state->options->video_height;
  assert(height);
  if (!ApVideoOldFramesInitialized) {
    ApVideoOldFramesInitialized = true;
    u32 bytes = 4u * width * height;
    for (i32 i = 0; i < 3; ++i)
      state->old_frames[i] = static_cast<u8 *>(calloc(1, bytes));
  }
  ApVideoBeginOldFrame(reinterpret_cast<const u16 *>(frame + 16), kind != 5, static_cast<i8>(frame[13]));
  u32 *working = reinterpret_cast<u32 *>(state->old_frames[kind == 7 ? 2 : 1]);
  if (width)
    assert(working);
  for (u32 y = 0; y < height; y += 16) {
    for (u32 x = 0; x < width; x += 16) {
      u32 offset = (x + width * y) >> 1;
      u8 *reference = state->old_frames[0];
      if (reference)
        reference += 4 * offset;
      ApVideoDecodeOldMacroblock(reference, reinterpret_cast<u8 *>(working + offset), width);
    }
  }
  u32 *source = working;
  for (u32 y = 0; y < height - 1; ++y) {
    ApVideoConvertYuv16(source, output, width >> 1);
    output = reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(output) + pitch);
    source += width >> 1;
  }
  if (kind != 7) {
    u8 *previous = state->old_frames[0];
    state->old_frames[0] = state->old_frames[1];
    state->old_frames[1] = previous;
  }
  return 0;
}

// Decode the four luma and two chroma blocks that form one TQI macroblock.
// The original uses a register-only internal ABI; this phase remains unverified against the installed decoder.
[[maybe_unused]] static void ApVideoTqiReadMacroblock(ApVideoState *state, i32 *luma, i32 *chroma) {
  i32 coefficients[64];
  for (i32 block = 0; block < 6; ++block) {
    i32 channel = block < 4 ? 0 : block - 3;
    i32 dc = ApVideoTqiReadDc(state, channel);
    ApVideoTqiReadCoefficients(state, dc, coefficients);
    i32 *destination = block < 4 ? luma + (block & 1 ? 8 : 0) + (block & 2 ? 128 : 0) :
        chroma + (block - 4) * 64;
    i32 stride = block < 4 ? 16 : 8;
    bool dc_only = true;
    for (i32 i = 1; i < 64; ++i) {
      if (coefficients[i]) {
        dc_only = false;
        break;
      }
    }
    if (dc_only) {
      i32 value = coefficients[0] >> 17;
      for (i32 y = 0; y < 8; ++y)
        for (i32 x = 0; x < 8; ++x)
          destination[y * stride + x] = value;
    } else {
      ApVideoTqiInverseTransform(coefficients, destination, stride);
    }
  }
}

// Reset the three transform predictors, then retain the input pointer and its initial bit position.
[[maybe_unused]] static void ApVideoBeginBitstream(const u8 *data, ApVideoState *state) {
  state->dc_predictors[2] = 0;
  state->dc_predictors[1] = 0;
  state->dc_predictors[0] = 0;
  state->bitstream = data;
  state->bit_position = 0;
}

// Map the TQI transform's signed luma and chroma values through the original display conversion tables.
static u32 ApVideoTqiPixel(i32 luma, i32 blue, i32 red) {
  assert(luma >= -256 && luma <= 255 && blue >= -256 && blue <= 255 && red >= -256 && red <= 255);
  u32 value = ApVideoYuvLuma[luma + 256] + ApVideoYuvBlue[blue + 256] + ApVideoYuvRed[red + 256];
  u32 r = value >> 23, g = (value >> 11) & 0x1ff, b = value & 0x1ff;
  assert(r < 306 && g < 262 && b < 354);
  return ApVideoRedConversion[r] | ApVideoGreenConversion[g] | ApVideoBlueConversion[b];
}

// Decode and draw one TQI macroblock through the original full-color lookup tables.
// The assembly implementation's returned pixel and floating-point rounding remain unverified.
static i32 ApVideoWriteTqi32Block(u8 *output, i32 pitch, i32 *quantization) {
  ApVideoState *state = reinterpret_cast<ApVideoState *>(reinterpret_cast<u8 *>(quantization) -
      offsetof(ApVideoState, quantization));
  i32 luma[256], chroma[128];
  ApVideoTqiReadMacroblock(state, luma, chroma);
  u32 previous = 0;
  for (i32 line = 0; line < 16; ++line) {
    for (i32 pixel = 0; pixel < 16; ++pixel) {
      i32 c = (line >> 1) * 8 + (pixel >> 1);
      u32 color = ApVideoTqiPixel(luma[line * 16 + pixel], chroma[c], chroma[64 + c]);
      memcpy(output + line * pitch + pixel * 4, &color, sizeof(color));
      if (line == 15 && pixel == 14)
        previous = color;
    }
  }
  return static_cast<i32>(previous);
}

// Decode and draw one TQI macroblock as pairs of sixteen-bit display pixels.
// The assembly implementation's returned pixel and floating-point rounding remain unverified.
static i32 ApVideoWriteTqi16Block(u8 *output, i32 pitch, i32 *quantization) {
  ApVideoState *state = reinterpret_cast<ApVideoState *>(reinterpret_cast<u8 *>(quantization) -
      offsetof(ApVideoState, quantization));
  i32 luma[256], chroma[128];
  ApVideoTqiReadMacroblock(state, luma, chroma);
  u32 previous = 0;
  for (i32 line = 0; line < 16; ++line) {
    for (i32 pixel = 0; pixel < 16; pixel += 2) {
      i32 c = (line >> 1) * 8 + (pixel >> 1);
      u32 first = ApVideoTqiPixel(luma[line * 16 + pixel], chroma[c], chroma[64 + c]);
      u32 second = ApVideoTqiPixel(luma[line * 16 + pixel + 1], chroma[c], chroma[64 + c]);
      u32 pair = (first & 0xffff) | (second & kApVideoUpperHalfwordMask);
      memcpy(output + line * pitch + pixel * 2, &pair, sizeof(pair));
      if (line == 15 && pixel == 14)
        previous = first & 0xffff;
    }
  }
  return static_cast<i32>(previous);
}

// Decode one TQI macroblock using the original fast sixteen-bit channel tables.
// The register-level transform and returned pixel remain unverified.
static i32 ApVideoWriteTqi16FastBlock(u8 *output, i32 pitch, i32 *quantization) {
  ApVideoState *state = reinterpret_cast<ApVideoState *>(reinterpret_cast<u8 *>(quantization) -
      offsetof(ApVideoState, quantization));
  i32 luma[256], chroma[128];
  ApVideoTqiReadMacroblock(state, luma, chroma);
  u32 previous = 0;
  for (i32 line = 0; line < 16; ++line) {
    for (i32 pixel = 0; pixel < 16; pixel += 2) {
      i32 c = (line >> 1) * 8 + (pixel >> 1);
      u32 cb = (static_cast<u32>(chroma[c]) >> 17) & 127;
      u32 cr = (static_cast<u32>(chroma[64 + c]) >> 17) & 127;
      u32 base = ApVideoChromaBlueTable[cb] + ApVideoChromaRedTable[cr];
      u32 first = base + ApVideoLumaTable[(static_cast<u32>(luma[line * 16 + pixel]) >> 17) & 127];
      u32 second = base + ApVideoLumaTable[(static_cast<u32>(luma[line * 16 + pixel + 1]) >> 17) & 127];
      u16 pair[2] = {static_cast<u16>(first), static_cast<u16>(second)};
      memcpy(output + line * pitch + pixel * 2, pair, sizeof(pair));
      if (line == 15 && pixel == 14)
        previous = first;
    }
  }
  return static_cast<i32>(previous);
}

// Decode and pack one TQI macroblock into the original two-pixel YUV surface format.
static i32 ApVideoWriteTqiYuvBlock(u8 *output, i32 pitch, i32 *quantization) {
  ApVideoState *state = reinterpret_cast<ApVideoState *>(reinterpret_cast<u8 *>(quantization) -
      offsetof(ApVideoState, quantization));
  i32 luma[256], chroma[128];
  ApVideoTqiReadMacroblock(state, luma, chroma);
  u32 pair = 0;
  for (i32 line = 0; line < 16; ++line) {
    for (i32 pixel = 0; pixel < 16; pixel += 2) {
      i32 c = (line >> 1) * 8 + (pixel >> 1);
      i32 y0 = luma[line * 16 + pixel], y1 = luma[line * 16 + pixel + 1];
      i32 cb = chroma[c], cr = chroma[64 + c];
      assert(y0 >= -256 && y0 <= 255 && y1 >= -256 && y1 <= 255 && cb >= -256 && cb <= 255 && cr >= -256 &&
          cr <= 255);
      pair = ApVideoClipTable[512 + 2 * y0] | (ApVideoClipTable[512 + 2 * cb] << 8) |
          (ApVideoClipTable[512 + 2 * y1] << 16) | (ApVideoClipTable[512 + 2 * cr] << 24);
      memcpy(output + line * pitch + pixel * 2, &pair, sizeof(pair));
    }
  }
  return static_cast<i32>(pair);
}

// Render the TQI macroblocks through the original packed YUV path and return the active options record.
[[maybe_unused]] static ApVideoOptions *ApVideoRenderTqiYuv(ApVideoState *state, const u8 *input, u8 *output, u8 *,
    u32 pitch) {
  ApVideoBeginBitstream(input, state);
  u8 *row = output;
  for (u32 y = 0; y < static_cast<u32>(state->options->video_height); y += 16) {
    u8 *destination = row;
    for (u32 x = 0; x < static_cast<u32>(state->options->video_width); x += 16) {
      ApVideoWriteTqiYuvBlock(destination, pitch, state->quantization);
      destination += 32;
    }
    row += 32 * (pitch >> 1);
  }
  return state->options;
}

// Render TQI macroblocks through the selected paired sixteen-bit output path.
[[maybe_unused]] static ApVideoOptions *ApVideoRenderTqi16(ApVideoState *state, const u8 *input, u8 *output, u8 *,
    u32 pitch) {
  ApVideoBeginBitstream(input, state);
  u8 *row = output;
  for (u32 y = 0; y < static_cast<u32>(state->options->video_height); y += 16) {
    u8 *destination = row;
    for (u32 x = 0; x < static_cast<u32>(state->options->video_width); x += 16) {
      if (state->video_flag_a)
        ApVideoWriteTqi16Block(destination, pitch, state->quantization);
      else
        ApVideoWriteTqi16FastBlock(destination, pitch, state->quantization);
      destination += 32;
    }
    row += 32 * (pitch >> 1);
  }
  return state->options;
}

// Render TQI macroblocks through the original full-color output tables.
[[maybe_unused]] static ApVideoOptions *ApVideoRenderTqi32(ApVideoState *state, const u8 *input, u8 *output, u8 *,
    u32 pitch) {
  ApVideoBeginBitstream(input, state);
  u8 *row = output;
  for (u32 y = 0; y < static_cast<u32>(state->options->video_height); y += 16) {
    u8 *destination = row;
    for (u32 x = 0; x < static_cast<u32>(state->options->video_width); x += 16) {
      assert(state->video_flag_a);
      if (!state->video_flag_a)
        std::unreachable();
      ApVideoWriteTqi32Block(destination, pitch, state->quantization);
      destination += 64;
    }
    row += 64 * (pitch >> 2);
  }
  return state->options;
}

// Initialize the selected renderer, channel conversions, and clipping table for a movie with video frames.
// Initialize palette storage before passing it to DirectDraw; the original passed uninitialized bytes.
[[maybe_unused]] static i32 ApVideoInitializeRendering(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  if (options->audio_only) {
    return 1;
  }
  GxBuffer *target = options->sdl_target;
  if (!target) {
    GxGraphics *graphics = ApGetGraphics();
    GxPrimary *screen = graphics ? graphics->ScreenBuffer() : nullptr;
    target = screen ? screen->AsIGZBuffer() : nullptr;
  }
  if (!target || target->GetBitsPerPixel() != 16 || target->GetColorType().color != GxColorType::Color::RGB565)
    return 0;
  u32 width = target->Width(), height = target->Height();
  u64 row_bytes = static_cast<u64>(width) * 2, padded_pitch = (row_bytes + 3) & ~u64{3};
  if (!width || !height || padded_pitch > INT32_MAX || height > INT32_MAX ||
      padded_pitch * height > SIZE_MAX)
    return 0;
  state->screen_width = static_cast<i32>(width);
  state->screen_height = static_cast<i32>(height);
  state->sdl_pitch = static_cast<u32>(padded_pitch);
  state->sdl_frame = new u8[static_cast<usize>(state->sdl_pitch) * height]{};
  if (!target->Lock(GxLockFlags::Bits)) {
    delete[] state->sdl_frame;
    state->sdl_frame = nullptr;
    return 0;
  }
  const u8 *source = static_cast<const u8 *>(target->GetColorSurfaceBits());
  u32 pitch = target->GetColorSurfaceStride();
  if (source && pitch >= row_bytes)
    for (u32 row = 0; row < height; ++row)
      memcpy(state->sdl_frame + row * state->sdl_pitch, source + row * pitch, static_cast<usize>(row_bytes));
  target->Unlock(GxLockFlags::Bits);
  if (!source || pitch < row_bytes) {
    delete[] state->sdl_frame;
    state->sdl_frame = nullptr;
    return 0;
  }
  options->pixel_format = {5, 6, 5, 11, 5, 0, 0xffff, 16};
  state->video_mode = 1;
  if (!(options->flags & kApVideoUseSuppliedGamma))
    options->gamma = 100;
  ApVideoSetColorTables(state);
  ApVideoBuildPixelConversion(&options->pixel_format, state);
  for (i32 value = -512; value < 512; ++value) {
    i32 clipped = value < -128 ? -128 : value > 127 ? 127 : value;
    ApVideoClipTable[static_cast<u32>(value - 384) & 0x3ff] = clipped + 128;
  }
  state->display_ready = 1;
  return 512;
}

// Calculate the movie's scaled dimensions and optional centered or explicitly offset destination rectangle.
[[maybe_unused]] static ApVideoState *ApVideoCalculateRenderBounds(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  state->render_x = 0;
  state->render_y = 0;
  state->render_width = options->video_width;
  state->render_height = options->video_height;
  if (options->flags & kApVideoDoubleWidth)
    state->render_width *= 2;
  if (options->flags & kApVideoDoubleHeightMask)
    state->render_height *= 2;
  if (options->flags & kApVideoCenterHorizontal)
    state->render_x = (state->screen_width - state->render_width) >> 1;
  if (options->flags & kApVideoCenterVertical)
    state->render_y = (state->screen_height - state->render_height) >> 1;
  if (options->flags & kApVideoDestinationX)
    state->render_x += options->destination_x;
  if (options->flags & kApVideoDestinationY)
    state->render_y += options->destination_y;
  if (options->flags & kApVideoDestinationWidth)
    state->render_width = options->destination_width;
  if (options->flags & kApVideoDestinationHeight)
    state->render_height = options->destination_height;
  return state;
}

// Read indexed-frame dimensions and build the source palette using the selected percentage brightness.
[[maybe_unused]] static i32 ApVideoReadFrameHeader1(ApVideoState *state, u16 *header) {
  ApVideoOptions *options = state->options;
  u16 fields[4];
  memcpy(fields, header, sizeof(fields));
  options->video_width = fields[0];
  options->video_height = fields[1];
  state->first_color = fields[2];
  state->color_count = fields[3];
  assert(state->color_count <= 256);
  u8 *source = reinterpret_cast<u8 *>(header) + 12;
  for (i32 i = 0; i < state->color_count; ++i) {
    for (i32 channel = 0; channel < 3; ++channel) {
      u32 value = static_cast<u32>(options->gamma) * *source++ / 100;
      options->palette[i * 3 + channel] = static_cast<u8>(value > 255 ? 255 : value);
    }
  }
  return state->color_count;
}

// Read the width, height, and color depth from an early video frame header.
[[maybe_unused]] static i16 *ApVideoReadFrameHeader3(ApVideoState *state, i16 *header) {
  i16 dimensions[2];
  memcpy(dimensions, header, sizeof(dimensions));
  state->options->video_width = dimensions[0];
  state->options->video_height = dimensions[1];
  state->video_depth = reinterpret_cast<u8 *>(header)[4];
  return header;
}

// Read a later video frame header including its two low-bit decoding flags.
[[maybe_unused]] static i32 ApVideoReadFrameHeader4(ApVideoState *state, i16 *header) {
  i16 dimensions[2];
  memcpy(dimensions, header, sizeof(dimensions));
  state->options->video_width = dimensions[0];
  state->options->video_height = dimensions[1];
  state->video_depth = reinterpret_cast<u8 *>(header)[4];
  u8 flags = reinterpret_cast<u8 *>(header)[7];
  state->video_flag_a = flags & 2;
  state->video_flag_b = flags & 1;
  return state->video_flag_b;
}

// Read the signed dimensions used by the three newer frame-header variants.
[[maybe_unused]] static i32 ApVideoReadFrameHeader5(ApVideoState *state, i16 *header) {
  i16 dimensions[2];
  memcpy(dimensions, reinterpret_cast<const u8 *>(header) + 8, sizeof(dimensions));
  state->options->video_width = dimensions[0];
  state->options->video_height = dimensions[1];
  return state->options->video_height;
}

// Apply the header parser selected by the queued frame's original format tag.
static void ApVideoApplyFrameHeader(ApVideoState *state, u8 *header) {
  switch (state->frame_kind) {
  case 1:
    ApVideoReadFrameHeader1(state, reinterpret_cast<u16 *>(header));
    break;
  case 3:
    ApVideoReadFrameHeader3(state, reinterpret_cast<i16 *>(header));
    break;
  case 4:
    ApVideoReadFrameHeader4(state, reinterpret_cast<i16 *>(header));
    break;
  case 5:
  case 6:
  case 7:
    ApVideoReadFrameHeader5(state, reinterpret_cast<i16 *>(header));
    break;
  }
}

// Populate initial frame dimensions and palette once the queue contains a video frame.
// The original helper's unused return type has not yet been established.
[[maybe_unused]] static void ApVideoPrepareFirstFrame(ApVideoState *state) {
  if (!state->options->audio_only) {
    state->frame_kind = state->video_chunks[0].kind;
    if (state->video_chunks[0].data)
      ApVideoApplyFrameHeader(state, state->video_chunks[0].data);
  }
}

// Consume one tagged big-endian audio-header number and sign-extend one-, two-, and three-byte values.
static i32 ApVideoReadNumber(const u8 **cursor) {
  const u8 *data = *cursor;
  u32 length = data[1], value = 0;
  data += 2;
  for (u32 i = 0; i < length; ++i)
    value = (value << 8) | *data++;
  *cursor = data;
  if (length == 1 && value > 0x7f)
    return static_cast<i32>(value) - 0x100;
  if (length == 2 && value > 0x7fff)
    return static_cast<i32>(value) - 0x10000;
  if (length == 3 && value > kApVideoThreeBytePositiveMax)
    return static_cast<i32>(value) - kApVideoThreeByteModulus;
  return static_cast<i32>(value);
}

using ApVideoFile = SDL_IOStream *;

// Open a movie path with the selected file backend, retaining its native failure result.
static ApVideoFile ApVideoOpenFile(char *filename) {
  ScString native = ScNativeFilePath(filename);
  return SDL_IOFromFile(native.Data(), "rb");
}

// Accept a supplied movie file and configure the original Windows multimedia buffer where required.
static bool ApVideoPrepareFile(ApVideoFile file) {
  return file != nullptr;
}

// Close a movie file through the backend that opened or supplied its handle.
static void ApVideoCloseFile(ApVideoFile file) {
  SDL_CloseIO(file);
}

// Read one original chunk from the active platform file while preserving EOF and file-error distinctions.
static i32 ApVideoReadFile(ApVideoFile file, void *data, i32 size) {
  size_t received = SDL_ReadIO(file, data, static_cast<usize>(size));
  return !received && SDL_GetIOStatus(file) == SDL_IO_STATUS_ERROR ? -1 : static_cast<i32>(received);
}

// Seek within the original signed movie-file position domain, retaining the platform failure result.
static i32 ApVideoSeekFile(ApVideoFile file, i32 offset, int origin) {
  SDL_IOWhence whence = origin == 0 ? SDL_IO_SEEK_SET : origin == 1 ? SDL_IO_SEEK_CUR : SDL_IO_SEEK_END;
  i64 result = SDL_SeekIO(file, offset, whence);
  return result < 0 || result > INT32_MAX ? -1 : static_cast<i32>(result);
}

// Read an audio header chunk and apply its tagged channel, sample rate, width, and compression fields.
// The original assumes each byte actually consumed by the parser was initialized by the file read.
[[maybe_unused]] static i32 ApVideoReadSoundHeader(ApVideoState *state, i32 size) {
  u8 data[49152];
  i32 received = ApVideoReadFile(reinterpret_cast<ApVideoFile>(state->options->file_handle), data, size);
  if (!received)
    return -1;
  if (received == -1)
    return -2;
  state->sample_bits = 16;
  state->channels = 1;
  state->sample_rate = 22050;
  state->compression = 0;
  const u8 *cursor = data + 4;
  // A custom MMIO callback may return a negative value after writing the bytes consumed below.
  // Such callers must initialize those bytes; the return value does not describe their extent.
  [[maybe_unused]] const u8 *end = data + (received > 0 && received < static_cast<i32>(sizeof(data)) ?
      static_cast<usize>(received) : sizeof(data));
  for (;;) {
    assert(cursor < end);
    if (*cursor == 0xfd) {
      ++cursor;
      break;
    }
    assert(end - cursor >= 2 && end - cursor - 2 >= cursor[1]);
    ApVideoReadNumber(&cursor);
  }
  for (;;) {
    assert(cursor < end);
    u8 tag = *cursor;
    if (tag == 0x8a)
      return 0;
    assert(end - cursor >= 2 && end - cursor - 2 >= cursor[1]);
    i32 value = ApVideoReadNumber(&cursor);
    switch (tag) {
    case 0x81:
      state->sample_bits = value;
      break;
    case 0x82:
      state->channels = value;
      break;
    case 0x83:
      state->compression = value;
      break;
    case 0x84:
      state->sample_rate = value;
      break;
    }
  }
}

// Consume an older audio header and select its fixed stereo, sample-rate, and compression parameters.
[[maybe_unused]] static i32 ApVideoReadLegacySoundHeader(ApVideoState *state, i32 size) {
  char data[49152];
  i32 received = ApVideoReadFile(reinterpret_cast<ApVideoFile>(state->options->file_handle), data, size);
  if (!received) {
    state->end_of_file = 1;
    return -1;
  }
  if (received == -1)
    return -2;
  state->sample_bits = 16;
  state->channels = 2;
  state->sample_rate = 22050;
  state->compression = 10;
  return 0;
}

// Close an internally opened movie file and discard its circular audio and video storage.
[[maybe_unused]] static i32 ApVideoCloseInput(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  if (!(options->flags & kApVideoSuppliedFileHandle) && options->file_handle)
    ApVideoCloseFile(reinterpret_cast<ApVideoFile>(options->file_handle));
  free(state->video_storage);
  free(state->audio_storage);
  for (i32 i = 0; i < 24; ++i)
    state->video_chunks[i].data = nullptr;
  if (options->has_audio)
    for (i32 i = 0; i < 24; ++i)
      state->audio_chunks[i].data = nullptr;
  return 0;
}

// Read a video chunk into the circular frame buffer and publish it in the original 24-slot queue.
[[maybe_unused]] static i32 ApVideoReadFrameChunk(ApVideoState *state, i32 size, i32 kind) {
  uptr write_address = reinterpret_cast<uptr>(state->video_write);
  uptr end_address = reinterpret_cast<uptr>(state->video_storage) + kApVideoFrameStorageBytes;
  if (write_address + static_cast<iptr>(size) >= end_address)
    state->video_write = state->video_storage;
  i32 received = ApVideoReadFile(reinterpret_cast<ApVideoFile>(state->options->file_handle), state->video_write, size);
  if (!received) {
    state->end_of_file = 1;
    return -1;
  }
  if (received == -1)
    return -2;
  ApVideoChunk *chunk = &state->video_chunks[state->video_queue_write % 24];
  chunk->data = state->video_write;
  chunk->kind = kind;
  chunk->sequence = state->video_sequence;
  state->video_write = reinterpret_cast<u8 *>(reinterpret_cast<uptr>(state->video_write) + static_cast<iptr>(size));
  ApVideoIncrement((&state->video_queue_write));
  ApVideoIncrement((&state->video_queue_count));
  ApVideoIncrement((&state->video_sequence));
  return 1;
}

// Count consecutive initial audio-track headers and restore the movie file position for the normal chunk reader.
[[maybe_unused]] static i32 ApVideoScanAudioTracks(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  ApVideoFile file = reinterpret_cast<ApVideoFile>(options->file_handle);
  u8 header[8];
  u32 rewind = kApVideoChunkHeaderRewind, type, size;
  options->audio_tracks = 0;
  options->has_audio = 0;
  for (;;) {
    if (ApVideoReadFile(file, header, 8) == 8) {
      type = static_cast<u32>(header[0]) | (static_cast<u32>(header[1]) << 8) |
          (static_cast<u32>(header[2]) << 16) | (static_cast<u32>(header[3]) << 24);
      size = (static_cast<u32>(header[4]) | (static_cast<u32>(header[5]) << 8) |
          (static_cast<u32>(header[6]) << 16) | (static_cast<u32>(header[7]) << 24)) - 8;
    } else {
      type = 0;
      size = 1;
    }
    if (type != kApAudioHeaderSead && type != kApAudioHeaderSchl && type != kApAudioHeader1Snh)
      break;
    ApVideoSeekFile(file, static_cast<i32>(size), 1);
    rewind -= 8 + size;
    options->has_audio = 1;
    ++options->audio_tracks;
  }
  ApVideoSeekFile(file, static_cast<i32>(rewind), 1);
  return options->has_audio;
}

// Read PCM, predictor-compressed, and differential audio chunks into the original sound queue.
static i32 ApVideoReadAudioChunk(ApVideoState *state, i32 size) {
  ApVideoFile file = reinterpret_cast<ApVideoFile>(state->options->file_handle);
  u8 data[49152];
  if (!state->audio_packets && (state->options->flags & kApVideoLoopPlayback))
    state->loop_offset = ApVideoSeekFile(file, 0, 1) - 8;
  i32 received = ApVideoReadFile(file, data, size);
  if (!received) {
    state->end_of_file = 1;
    return -1;
  }
  if (received == -1)
    return -2;
  // A negative custom-MMIO result may still accompany bytes written by its callback.
  // The caller must initialize every byte consumed by the selected decoder.
  i32 bytes;
  if (state->compression == 7) {
    assert(received < -1 || received >= 12);
    i16 samples[4];
    memcpy(samples, data + 4, sizeof(samples));
    state->left_sample = samples[0];
    state->previous_left_sample = samples[1];
    state->right_sample = samples[2];
    state->previous_right_sample = samples[3];
    i32 block_size = state->channels == 2 ? 30 : 15;
    assert(size < 12 || (size - 12) / block_size <= (static_cast<i32>(sizeof(data)) - 12) / block_size);
    assert(received < -1 || received >= 12 + ((size - 12) / block_size) * block_size);
    bytes = 112 * ((size - 12) / block_size);
    assert(bytes <= kApVideoAudioStorageBytes);
    if (state->audio_write - state->audio_storage + bytes >= kApVideoAudioStorageBytes)
      state->audio_write = state->audio_storage;
    i16 *output = reinterpret_cast<i16 *>(state->audio_write);
    i32 position = 0;
    if (state->channels == 2)
      ApVideoDecodeStereo(bytes / 4, &position, &state->left_sample, &state->previous_left_sample,
          &state->right_sample, &state->previous_right_sample, data + 12, &output);
    else
      ApVideoDecodeMono(bytes / 4, &position, &state->left_sample, &state->previous_left_sample, data + 12, &output);
  } else if (state->compression == 10) {
    assert(size <= static_cast<i32>(sizeof(data)));
    assert(received < -1 || received >= size);
    bytes = 4 * size;
    if (state->audio_write - state->audio_storage + bytes >= kApVideoAudioStorageBytes)
      state->audio_write = state->audio_storage;
    ApVideoDecodeDifferential(data, reinterpret_cast<i16 *>(state->audio_write), size,
        &state->adpcm_samples, &state->adpcm_steps);
  } else if (!state->compression) {
    assert(received < -1 || received >= 4);
    u32 count;
    memcpy(&count, data, sizeof(count));
    bytes = static_cast<i32>(count * 4u);
    u32 frames = bytes > 0 ? static_cast<u32>(bytes) / 4 : 0;
    [[maybe_unused]] u32 source_bytes = frames * (state->channels == 2 ? 4 : 2);
    assert(source_bytes <= sizeof(data) - 4);
    assert(received < -1 || source_bytes <= static_cast<u32>(received - 4));
    assert(bytes <= kApVideoAudioStorageBytes);
    if (reinterpret_cast<uptr>(state->audio_write) + static_cast<iptr>(bytes) >=
        reinterpret_cast<uptr>(state->audio_storage) + kApVideoAudioStorageBytes)
      state->audio_write = state->audio_storage;
    if (state->channels == 2 && bytes > 0) {
      memcpy(state->audio_write, data + 4, bytes);
    } else {
      for (u32 i = 0; i < frames; ++i) {
        memcpy(state->audio_write + i * 4, data + 4 + i * 2, 2);
        memcpy(state->audio_write + i * 4 + 2, data + 4 + i * 2, 2);
      }
    }
  } else {
    assert(false && "UltraVideo compression has no defined decoded length");
    std::unreachable();
  }
  if (state->options->audio_only)
    ApVideoIncrement((&state->video_sequence));
  ApVideoChunk *chunk = &state->audio_chunks[state->audio_write_slot % 24];
  chunk->data = state->audio_write;
  chunk->size = bytes;
  chunk->sequence = state->video_sequence;
  chunk->kind = state->audio_packets;
  state->audio_write = reinterpret_cast<u8 *>(reinterpret_cast<uptr>(state->audio_write) + static_cast<iptr>(bytes));
  state->audio_bytes = static_cast<i32>(static_cast<u32>(state->audio_bytes) + static_cast<u32>(bytes));
  ApVideoIncrement(reinterpret_cast<volatile i32 *>(&state->audio_write_slot));
  ApVideoIncrement((&state->audio_queue_count));
  ApVideoIncrement(reinterpret_cast<volatile i32 *>(&state->audio_packets));
  return 2;
}

// Dispatch the installed movie chunk types to their audio-header, audio-sample, or video-frame readers.
// Compression values without a defined decoded length are excluded in the audio chunk reader.
[[maybe_unused]] static i32 ApVideoReadChunk(ApVideoState *state) {
  ApVideoFile file = reinterpret_cast<ApVideoFile>(state->options->file_handle);
  u8 header[8];
  i32 received = ApVideoReadFile(file, header, 8);
  if (!received) {
    state->end_of_file = 1;
    return -1;
  }
  if (received != 8)
    return -2;
  u32 type = static_cast<u32>(header[0]) | (static_cast<u32>(header[1]) << 8) |
      (static_cast<u32>(header[2]) << 16) | (static_cast<u32>(header[3]) << 24);
  i32 size = static_cast<i32>((static_cast<u32>(header[4]) | (static_cast<u32>(header[5]) << 8) |
      (static_cast<u32>(header[6]) << 16) | (static_cast<u32>(header[7]) << 24)) - 8);
  switch (type) {
  case kApAudioDataSndc:
  case kApAudioDataSndcReversed:
  case kApAudioDataScdlReversed:
  case kApAudioData1Snd:
  case kApAudioDataScdl:
    if (!state->options->has_audio)
      return -2;
    ++state->current_audio_stream;
    if (state->current_audio_stream == state->options->selected_audio_track)
      return ApVideoReadAudioChunk(state, size);
    ApVideoSeekFile(file, size, 1);
    return 2;
  default:
    break;
  }
  state->current_audio_stream = 0;
  switch (type) {
  case kApAudioHeaderSead:
    return ApVideoReadLegacySoundHeader(state, size);
  case kApAudioHeaderSchlReversed:
  case kApAudioHeader1Snh:
  case kApAudioHeaderSchl:
    return ApVideoReadSoundHeader(state, size);
  case kApAudioEndSend:
  case kApAudioEnd1Sne:
  case kApAudioEndScel:
    return -1;
  case kApAudioAuxiliaryScclReversed:
  case kApAudioAuxiliaryScllReversed:
  case kApAudioAuxiliarySccl:
  case kApAudioAuxiliaryScll:
    ApVideoSeekFile(file, size, 1);
    return 4;
  case kApVideoTgvKeyFrame:
    return ApVideoReadFrameChunk(state, size, 1);
  case kApVideoTgvFrameReversed:
  case kApVideoTgvFrame:
    return ApVideoReadFrameChunk(state, size, 2);
  case kApVideoTgqFrameReversed:
  case kApVideoMuvFrame:
    return ApVideoReadFrameChunk(state, size, 3);
  case kApVideoTqiFrameReversed:
  case kApVideoUv2Frame:
    return ApVideoReadFrameChunk(state, size, 4);
  case kApVideoMadKeyFrameReversed:
  case kApVideoMadKeyFrame:
    return ApVideoReadFrameChunk(state, size, 5);
  case kApVideoMadMFrameReversed:
  case kApVideoMadMFrame:
    return ApVideoReadFrameChunk(state, size, 6);
  case kApVideoMadEFrameReversed:
  case kApVideoMadEFrame:
    return ApVideoReadFrameChunk(state, size, 7);
  default:
    return -2;
  }
}

// Seek to the recorded audio loop point and refill the first chunk when looping is enabled.
static i32 ApVideoRestartInput(ApVideoState *state) {
  if (!(state->options->flags & kApVideoLoopPlayback)) {
    state->chunk_result = -1;
    ApVideoExchange(&state->sdl_input_finished, 1);
    return static_cast<i32>(reinterpret_cast<uptr>(state->options));
  }
  ApVideoDelay(0);
  ApVideoSeekFile(reinterpret_cast<ApVideoFile>(state->options->file_handle), state->loop_offset, 0);
  state->chunk_result = ApVideoReadChunk(state);
  return state->chunk_result;
}

// Refill the movie queues below their original thresholds and handle end-of-stream looping.
static i32 ApVideoReadAhead(ApVideoState *state) {
  if (state->chunk_result == -1) {
    ApVideoRestartInput(state);
    if (state->chunk_result == -1)
      return -1;
  }
  if (state->audio_queue_count >= 22 || state->video_queue_count >= 22)
    return state->audio_queue_count;
  i32 result = ApVideoReadChunk(state);
  state->last_chunk_result = result;
  if (result == -1) {
    return ApVideoRestartInput(state);
  } else if (result == 1 && state->options->has_audio) {
    result = ApVideoReadChunk(state);
    state->chunk_result = result;
    if (result == 1) {
      state->options->has_audio = 0;
    } else {
      while (result == 2) {
        result = ApVideoReadChunk(state);
        state->chunk_result = result;
      }
    }
    return result;
  } else if (result == 2 && (!state->options->audio_only || state->audio_queue_count < 22)) {
    do {
      result = ApVideoReadChunk(state);
      state->chunk_result = result;
    } while (result == 2);
    return result;
  } else if (result == 1) {
    return state->options->has_audio;
  } else if (result == 2) {
    return state->options->audio_only;
  }
  return result + 2;
}

// Open and prebuffer the movie's original audio and video chunk queues, retaining recoverable allocation failures.
[[maybe_unused]] static i32 ApVideoOpenInput(char *filename, ApVideoState *state) {
  ApVideoOptions *options = state->options;
  state->audio_threshold = 5;
  state->end_of_file = 0;
  if (!(options->flags & kApVideoSuppliedFileHandle))
    options->file_handle = ApVideoOpenFile(filename);
  ApVideoFile file = reinterpret_cast<ApVideoFile>(options->file_handle);
  if (!ApVideoPrepareFile(file))
    return 1;
  ApVideoScanAudioTracks(state);
  if (!(options->flags & kApVideoSelectAudioTrack) || options->selected_audio_track < 1 ||
      options->selected_audio_track > options->audio_tracks)
    options->selected_audio_track = 1;
  for (i32 i = 0; i < 24; ++i) {
    state->audio_chunks[i].data = nullptr;
    state->video_chunks[i].data = nullptr;
  }
  state->video_storage = static_cast<u8 *>(calloc(1, kApVideoFrameStorageBytes));
  state->video_write = state->video_storage;
  if (!state->video_storage)
    return 2;
  state->audio_storage = static_cast<u8 *>(calloc(1, kApVideoAudioStorageBytes));
  state->audio_write = state->audio_storage;
  if (!state->audio_storage)
    return 2;
  state->audio_bytes = 0;
  state->video_sequence = 0;
  state->audio_packets = 0;
  state->video_read_slot = 0;
  state->video_queue_write = 0;
  state->video_queue_count = 0;
  state->audio_queue_count = 0;
  state->chunk_result = 4;
  if (options->has_audio) {
    while (state->audio_queue_count < 21)
      while (ApVideoReadChunk(state) == 2) {}
    assert(state->audio_packets);
    i32 average = state->audio_bytes / state->audio_packets;
    assert(average);
    state->audio_threshold = 32768 / average;
  } else {
    while (state->video_queue_count < 20 && ApVideoReadChunk(state) != -1) {}
  }
  options->audio_only = state->video_sequence == 0;
  return 0;
}

// Initialize the selected rendering backend and retain its existing open-time failure handling.
static bool ApVideoOpenRendering(ApVideoState *state) {
  return ApVideoInitializeRendering(state) != 0;
}

// Open and prebuffer a movie, initialize its sound and renderer, and publish the playback handle.
extern "C" ApVideoInstance *SC3_CDECL uV_Open(char *filename, void *window, ApVideoOptions *options) {
  ApVideoState *state = static_cast<ApVideoState *>(calloc(1, sizeof(ApVideoState)));
  if (!state)
    return nullptr;
  state->options = options;
  options->window = window;
  if (ApVideoOpenInput(filename, state)) {
    free(state);
    return nullptr;
  }
  ApVideoPrepareFirstFrame(state);
  options->state = state;
  if (ApVideoInitializeSound(state)) {
    ApVideoCloseInput(state);
    options->state = nullptr;
    free(state);
    return nullptr;
  }
  if (!ApVideoOpenRendering(state)) {
    ApVideoCloseSound(state);
    ApVideoCloseInput(state);
    options->state = nullptr;
    free(state);
    return nullptr;
  }
  ApVideoExchange((&options->playback_status), kApVideoReady);
  return reinterpret_cast<ApVideoInstance *>(options);
}

// Check the original playback state, including the alternate mode's distinct completion state.
extern "C" i32 SC3_CDECL uV_is_Playing(ApVideoInstance *instance) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  if (!options->state)
    return 0;
  if (options->state->alternate_playback)
    return options->playback_status != kApVideoClosed;
  return options->playback_status == kApVideoPlaying || options->playback_status == kApVideoPaused;
}

// Decode a movie frame into the selected direct, offscreen, or overlay destination.
// Unrecognized frame kinds skip decoding and still follow the normal surface-unlock path.
// The older transform and alternate full-color TQI writers remain unverified or incomplete.
static void ApVideoDecodeFrame(ApVideoState *state, ApVideoChunk *frame) {
  ApVideoOptions *options = state->options;
  state->frame_kind = frame->kind;
  ApVideoApplyFrameHeader(state, frame->data);
  ApVideoCalculateRenderBounds(state);
  u8 *bits = nullptr, *base = nullptr;
  u32 pitch = 0;
  bool transformed = frame->kind == 3 || frame->kind == 4;
  i64 width = options->video_width, height = options->video_height;
  if (transformed && width > 0 && height > 0) {
    width = (width + 15) & ~i64{15};
    height = (height + 15) & ~i64{15};
  }
  width *= frame->kind <= 4 && (options->flags & kApVideoDoubleWidth) ? 2 : 1;
  if (transformed && (options->flags & kApVideoDoubleHeightMask) && height > 0)
    height = height * 2 - 1;
  i64 left = transformed ? static_cast<i64>(state->render_x) * (options->flags & kApVideoDoubleWidth ? 2 : 1) : 0;
  i64 top = transformed ? state->render_y : 0;
  if (!state->sdl_frame || state->render_x < 0 || state->render_y < 0 || width <= 0 || height <= 0 ||
      left + width > state->screen_width || top + height > state->screen_height) {
    ApVideoExchange((&options->playback_status), kApVideoStopRequested);
    return;
  }
  bits = base = state->sdl_frame;
  pitch = state->sdl_pitch;
  if (frame->kind == 1) {
    ApVideoInitializeIndexedFrame(state, frame->data);
    if (options->pixel_format.bits_per_pixel != 8)
      ApVideoPackPalette(state);
    ApVideoCopyIndexedFrame(state, bits, base, pitch);
  } else if (frame->kind == 2) {
    ApVideoDecodeIndexedFrame(state, frame->data);
    ApVideoCopyIndexedFrame(state, bits, base, pitch);
  } else if (frame->kind == 3) {
    ApVideoSetTransformQuality(state->video_depth);
    u32 bytes_per_pixel = options->flags & kApVideoDoubleWidth ? 4 : 2;
    u8 *destination = bits + pitch * state->render_y + bytes_per_pixel * state->render_x;
    u32 output_pitch = options->flags & kApVideoDoubleHeightMask ? 2 * pitch : pitch;
    if (options->flags & kApVideoDoubleWidth)
      ApVideoRenderBlocks32(state, frame->data + 8, destination, base, output_pitch);
    else
      ApVideoRenderBlocks16(state, frame->data + 8, destination, base, output_pitch);
  } else if (frame->kind == 4) {
    ApVideoBuildQuantization(state->quantization, state->video_depth);
    if (state->video_mode != 1) {
      ApVideoRenderTqiYuv(state, frame->data + 8, bits, base, pitch);
    } else if ((options->flags & kApVideoDoubleWidth) || options->pixel_format.bits_per_pixel > 16) {
      u8 *destination = bits + pitch * state->render_y + 4 * state->render_x;
      u32 output_pitch = options->flags & kApVideoDoubleHeightMask ? 2 * pitch : pitch;
      ApVideoRenderTqi32(state, frame->data + 8, destination, base, output_pitch);
    } else {
      u8 *destination = bits;
      u32 output_pitch = pitch;
      if (options->flags & kApVideoDirectSurfaceDecode) {
        destination += pitch * state->render_y + 2 * state->render_x;
        if (options->flags & kApVideoDoubleHeightMask)
          output_pitch *= 2;
      }
      ApVideoRenderTqi16(state, frame->data + 8, destination, base, output_pitch);
    }
  } else if (frame->kind >= 5 && frame->kind <= 7) {
    ApVideoDecodeOldFrame(state, frame->data, reinterpret_cast<u32 *>(bits), static_cast<i32>(pitch), frame->kind);
  }
}

// Present the decoded movie surface, including the original overlay, scanline, and page-flipping modes.
static void ApVideoPresentFrame(ApVideoState *state) {
  ApVideoOptions *options = state->options;
  if (options->sdl_target) {
    GxBuffer *target = options->sdl_target;
    if (!target->Lock(GxLockFlags::Bits)) {
      ApVideoExchange((&options->playback_status), kApVideoStopRequested);
      return;
    }
    u8 *destination = static_cast<u8 *>(target->GetColorSurfaceBits());
    u32 pitch = target->GetColorSurfaceStride();
    u32 row_bytes = static_cast<u32>(state->screen_width) * 2;
    if (!destination || pitch < row_bytes) {
      target->Unlock(GxLockFlags::Bits);
      ApVideoExchange((&options->playback_status), kApVideoStopRequested);
      return;
    }
    for (i32 row = 0; row < state->screen_height; ++row)
      memcpy(destination + row * pitch, state->sdl_frame + row * state->sdl_pitch, row_bytes);
    target->Unlock(GxLockFlags::Bits);
  } else if (!GxSdlSubmitFrame(state->sdl_frame, state->sdl_pitch, state->screen_width, state->screen_height)) {
    ApVideoExchange((&options->playback_status), kApVideoStopRequested);
  }
  return;
}

// Apply the movie's requested priority using the active worker-thread backend.
static void ApVideoSetPlaybackPriority(i32 priority) {
  SDL_SetCurrentThreadPriority(priority >= 15 ? SDL_THREAD_PRIORITY_TIME_CRITICAL :
      priority > 0 ? SDL_THREAD_PRIORITY_HIGH :
      priority < 0 ? SDL_THREAD_PRIORITY_LOW : SDL_THREAD_PRIORITY_NORMAL);
}

using ApVideoSoundStatus = u32;

// Check the active sound backend for queued playback and preserve the Windows buffer-lost restart.
static bool ApVideoHasPendingSound(ApVideoState *state, [[maybe_unused]] ApVideoSoundStatus *sound_status) {
  return state->sdl_audio && SDL_GetAudioStreamQueued(state->sdl_audio) > 0;
}

// Run the original queue, audio, pause, and timing portions of the movie playback loop.
static i32 ApVideoPlaybackLoop(ApVideoInstance *instance) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  ApVideoState *state = options->state;
  ApVideoSoundStatus sound_status;
  if ((options->flags & kApVideoThreadPriorityMask) == kApVideoThreadPriorityMask)
    ApVideoSetPlaybackPriority(options->thread_priority);
  if (!ApVideoStartAudioThread(state)) {
    options->frame_delay = 0;
    volatile i32 *status = (&options->playback_status);
    ApVideoExchange(status, kApVideoStarting);
    state->start_tick = ApVideoTicks();
    for (;;) {
      i32 current = ApVideoExchange(status, kApVideoPlaying);
      ApVideoChunk *frame = &state->video_chunks[state->video_read_slot % 24];
      if (current == kApVideoPaused) {
        ApVideoExchange(status, kApVideoPaused);
        do {
          ApVideoDelay(20);
          current = ApVideoExchange(status, kApVideoPaused);
        } while (current == kApVideoPaused);
        ApVideoExchange(status, current);
      }
      if (current == kApVideoStopRequested)
        break;
      if (current != kApVideoPlaying)
        continue;
      ApVideoDelay(0);
      ApVideoReadAhead(state);
      if (!state->audio_running && options->has_audio) {
        if (ApVideoStartSound(state))
          break;
        state->audio_frame_counter = 0;
      }
      if (frame->data) {
        if (options->frame_delay > 2) {
          if (frame->kind == 1 || frame->kind == 2 || frame->kind == 5 || frame->kind == 6)
            ApVideoDecodeFrame(state, frame);
        } else {
          ApVideoDecodeFrame(state, frame);
          ApVideoPresentFrame(state);
        }
        if ((options->flags & kApVideoFrameCallback) && options->frame_callback && options->frame_callback())
          break;
        frame->data = nullptr;
        frame->size = 0;
        ApVideoIncrement(reinterpret_cast<volatile i32 *>(&state->video_read_slot));
        ApVideoDecrement((&state->video_queue_count));
        ApVideoSwapBuffers(state);
      }
      ApVideoIncrement(reinterpret_cast<volatile i32 *>(&options->frames_displayed));
      if (!state->video_queue_count) {
        if (!options->has_audio || !(options->flags & kApVideoEnableAudio))
          break;
        if (!state->audio_queue_count && !ApVideoHasPendingSound(state, &sound_status))
          break;
      }
      if (options->has_audio) {
        i32 delay = state->audio_frame_counter - state->audio_threshold;
        i32 displayed = options->frames_displayed;
        for (i32 waits = 0; displayed - delay >= 1 && waits <= 150; ++waits) {
          ApVideoDelay(0);
          ApVideoDelay(2);
          delay = state->audio_frame_counter - state->audio_threshold;
        }
        options->frame_delay = delay - displayed;
        if (options->frame_delay < 0)
          options->frame_delay = 0;
      }
    }
    state->end_tick = ApVideoTicks();
    ApVideoStopAudioThread(state);
  }
  ApVideoReleasePlaybackAudio(state);
  return ApVideoExchange((&options->playback_status), kApVideoFinished);
}

// Run the movie presentation loop and close self-managed playback before ending a worker thread.
static void SC3_CDECL ApVideoPlaybackThread(void *context) {
  ApVideoInstance *instance = static_cast<ApVideoInstance *>(context);
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  ApVideoState *state = options->state;
  bool threaded = (options->flags & kApVideoThreadedPlayback) != 0;
  ApVideoPlaybackLoop(instance);
  if (state->alternate_playback)
    uV_Close(instance);
  if (threaded)
    ApVideoFinishWorker();
}

// Close the movie's input, sound, and graphics resources, then retire its private playback state.
extern "C" i32 SC3_CDECL uV_Close(ApVideoInstance *instance) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  ApVideoState *state = options->state;
  options->playback_elapsed = static_cast<i32>(state->end_tick - state->start_tick);
  ApVideoCloseInput(state);
  ApVideoCloseSound(state);
  ApVideoCloseRendering(state);
  ApVideoExchange((&options->playback_status), kApVideoClosed);
  ApVideoExchangePointer(reinterpret_cast<void *volatile *>(&options->state), nullptr);
  free(state);
  return 0;
}

// Request the playback thread to stop and wait for its mode-specific completed state.
extern "C" i32 SC3_CDECL uV_Stop(ApVideoInstance *instance) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  i32 status = options->playback_status;
  if (status == kApVideoInactive || status == kApVideoFinished || status == kApVideoClosed || !options->state)
    return -1;
  ApVideoExchange((&options->playback_status), kApVideoStopRequested);
  i32 finished = options->state->alternate_playback ? kApVideoClosed : kApVideoFinished;
  while (options->playback_status != finished)
    ApVideoDelay(10);
  return 0;
}

// Pause or resume the active movie sound using the selected audio backend.
static void ApVideoPauseSound(ApVideoOptions *options, bool paused) {
  if (options->state && options->state->sdl_audio) {
    if (paused)
      SDL_PauseAudioStreamDevice(options->state->sdl_audio);
    else
      SDL_ResumeAudioStreamDevice(options->state->sdl_audio);
  }
}

// Apply a movie volume through its backend, including the existing SDL decibel conversion and range check.
static void ApVideoSetSoundVolume(ApVideoOptions *options, i32 volume) {
  if (options->state && options->state->sdl_audio && volume >= -10000 && volume <= 0)
    SDL_SetAudioStreamGain(options->state->sdl_audio,
        volume == -10000 ? 0.0f : static_cast<f32>(pow(10.0, volume / 2000.0)));
}

// Apply a movie sampling frequency through its backend, retaining the current SDL ratio conversion.
static void ApVideoSetSoundFrequency(ApVideoOptions *options, i32 frequency) {
  if (options->state && options->state->sdl_audio && options->state->sample_rate &&
      (frequency == 0 || frequency >= 100))
    SDL_SetAudioStreamFrequencyRatio(options->state->sdl_audio, frequency ?
        static_cast<f32>(frequency) / static_cast<f32>(options->state->sample_rate) : 1.0f);
}

// Toggle the playback state and stop or resume the associated DirectSound buffer when present.
extern "C" i32 SC3_CDECL uV_Pause(ApVideoInstance *instance) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  volatile i32 *status = (&options->playback_status);
  if (ApVideoExchange(status, kApVideoPaused) == kApVideoPlaying) {
    ApVideoExchange(status, kApVideoPaused);
    ApVideoPauseSound(options, true);
    return 1;
  }
  ApVideoExchange(status, kApVideoPlaying);
  ApVideoPauseSound(options, false);
  return 0;
}

// Forward an in-range volume to the active movie audio buffer and retain the original zero return.
extern "C" i32 SC3_CDECL uV_SetAudioVolume(ApVideoInstance *instance, i32 volume) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  ApVideoSetSoundVolume(options, volume);
  return 0;
}

// Forward the requested sampling frequency to the active movie audio buffer and retain the original zero return.
extern "C" i32 SC3_CDECL uV_SetAudioFrequency(ApVideoInstance *instance, i32 frequency) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  ApVideoSetSoundFrequency(options, frequency);
  return 0;
}

// Select a numbered audio track only when it falls within the movie's discovered track count.
extern "C" i32 SC3_CDECL uV_SelectAudioTrack(ApVideoInstance *instance, i32 track) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  if (track < 1 || track > options->audio_tracks)
    return 1;
  options->selected_audio_track = track;
  return 0;
}

static i32 ApVideoRenderTypes([[maybe_unused]] IDirectDraw2 *device) {
  return 1;
}

// Report the software renderer and the two DirectDraw rendering modes supported by the device caps.
extern "C" i32 SC3_CDECL uV_RenderTypes(IDirectDraw2 *device) {
  return ApVideoRenderTypes(device);
}

// Store the requested gamma and rebuild the movie's current color conversion tables.
extern "C" i32 SC3_CDECL uV_SetGamma(ApVideoInstance *instance, i32 gamma) {
  ApVideoOptions *options = reinterpret_cast<ApVideoOptions *>(instance);
  options->gamma = gamma;
  ApVideoSetColorTables(options->state);
  return 0;
}

// Open a movie, select the self-managed playback mode, and start its playback thread.
extern "C" ApVideoInstance *SC3_CDECL uV_Play(char *filename, void *window, ApVideoOptions *options) {
  ApVideoInstance *instance = uV_Open(filename, window, options);
  if (!instance)
    return nullptr;
  reinterpret_cast<ApVideoOptions *>(instance)->state->alternate_playback = 1;
  if (uV_Play_FromHandle(instance))
    return nullptr;
  return instance;
}

// Wait until the movie's playback state reports that it has finished.
extern "C" i32 SC3_CDECL uV_Wait(ApVideoInstance *instance) {
  while (uV_is_Playing(instance))
    ApVideoDelay(10);
  return 0;
}

// Return the installed UltraVideo version string.
extern "C" char *SC3_CDECL uV_Version() {
  static char version[] = "1.60";
  return version;
}

// Initialize the unopened player and allocate its zeroed UltraVideo options after the path String is constructed.
ApAnimationPlayer::ApAnimationPlayer() {
  status = 0;
  opened = false;
  pause_count = 0;
  looping = false;
  blank_at_end = false;
  video_width = 0;
  video_height = 0;
  destination_x = 0;
  destination_y = 0;
  destination_width = 0;
  destination_height = 0;
  window = nullptr;
  buffer_blit_flags = 0;
  destination_type = Destination::PrimaryScreen;
  sound_volume = 1024;
  front_buffer = nullptr;
  back_buffer = nullptr;
  video = nullptr;
  options = new ApVideoOptions{};
}

// Initialize the unopened player with the supplied destination dimensions and optional copied movie path.
ApAnimationPlayer::ApAnimationPlayer(const char *filename, u32 width, u32 height) {
  status = 0;
  opened = false;
  pause_count = 0;
  looping = false;
  blank_at_end = false;
  video_width = 0;
  video_height = 0;
  destination_x = 0;
  destination_y = 0;
  destination_width = width;
  destination_height = height;
  window = nullptr;
  destination_type = Destination::PrimaryScreen;
  sound_volume = 1024;
  front_buffer = nullptr;
  back_buffer = nullptr;
  video = nullptr;
  options = new ApVideoOptions{};
  if (filename) {
    ScString name(filename);
    path = name;
  }
}

// Release only the surfaces retained by the selected movie backend.
static void ApVideoReleaseTargets(ApVideoOptions *options) {
  if (options->sdl_target)
    options->sdl_target->Release();
}

// Test the configured movie destination using the active backend's existing requirements.
static bool ApVideoTargetReady(ApVideoOptions *options, [[maybe_unused]] bool primary) {
  return primary ? GxSdlWindow() != nullptr : options->sdl_target != nullptr;
}

// Close the original player, release both retained vendor surfaces and retire its options before the path String.
ApAnimationPlayer::~ApAnimationPlayer() {
  ApAnimationPlayer::Close();
  if (options) {
    ApVideoReleaseTargets(options);
    delete options;
  }
}

// Select the original movie path, initialize the vendor options and open a supported screen or buffer destination.
// Preserve the source's 100-pixel fallback and leave an already opened player unchanged.
bool ApAnimationPlayer::Open(const char *filename) {
  if (opened)
    return false;
  if (filename) {
    ScString name(filename);
    path = name;
  }
  if (!RsFile::FileExists(path) || !options)
    return false;
  if (destination_type != Destination::PrimaryScreen && !CanSetOptions()) {
    if (options->destination_width || !video_width) {
      options->destination_width = 100;
      video_width = 100;
    }
    if (options->destination_height || !video_height) {
      options->destination_height = 100;
      video_height = 100;
    }
    return true;
  }
  SetOptions();
  if (!ApVideoTargetReady(options, destination_type == Destination::PrimaryScreen))
    return false;
  video = uV_Open(path.begin, window, options);
  if (!video)
    return false;
  if (options->destination_width || !video_width) {
    options->destination_width = 100;
    video_width = 100;
  }
  if (options->destination_height || !video_height) {
    options->destination_height = 100;
    video_height = 100;
  }
  opened = true;
  return true;
}

// Stop an opened movie through virtual dispatch, then close its UltraVideo handle and clear the open state.
void ApAnimationPlayer::Close() {
  if (opened) {
    Stop();
    if (video)
      uV_Close(video);
  }
  video = nullptr;
  opened = false;
}

void ApAnimationPlayer::SetDestinationType(Destination value) {
  destination_type = value;
}

// Start or poll the real vendor movie, and optionally clear the destination after playback has ended.
// The original ignores the supplied coordinates and relies on the previously configured video options.
bool ApAnimationPlayer::ComposeToGZBuffer(GxBuffer *buffer, u32, u32) {
  if (status == 0) {
    if (!video) {
      if (!front_buffer)
        front_buffer = buffer;
      if (!Open(nullptr))
        return false;
    }
    if (video && uV_Play_FromHandle(video) == 0) {
      status = 1;
      return true;
    }
    status = 2;
    return false;
  }
  if (status == 2) {
    if (blank_at_end && buffer->Lock(static_cast<GxLockFlags>(64))) {
      buffer->Fill(fill_color, static_cast<ScRect *>(nullptr));
      buffer->Unlock(static_cast<GxLockFlags>(64));
    }
    return false;
  }
  if (!uV_is_Playing(video)) {
    status = 2;
    return false;
  }
  return true;
}

bool ApAnimationPlayer::ComposeToRawBuffer(u8 *, u32, u32, u32, u32) {
  return false;
}

// Store the loop preference and update the corresponding flag in an existing UltraVideo options record.
void ApAnimationPlayer::SetLoopingPlayback(bool value) {
  looping = value;
  if (options) {
    if (value)
      options->flags |= kApVideoLoopPlayback;
    else
      options->flags &= ~kApVideoLoopPlayback;
  }
}

// Scale the intrinsic video dimensions and publish both destination sizes into the existing video options.
bool ApAnimationPlayer::SetZoomFactor(u32 factor) {
  if (!video_width || !video_height)
    return false;
  destination_height = factor * video_height;
  destination_width = factor * video_width;
  if (options) {
    options->flags |= kApVideoDestinationSizeMask;
    options->destination_width = destination_width;
    options->destination_height = destination_height;
  }
  return true;
}

// Store both destination coordinates and mark their corresponding UltraVideo options present.
void ApAnimationPlayer::SetPosition(i32 x, i32 y) {
  destination_x = x;
  destination_y = y;
  if (options) {
    options->flags |= kApVideoDestinationPositionMask;
    options->destination_x = destination_x;
    options->destination_y = destination_y;
  }
}

// Store both destination dimensions and mark their corresponding UltraVideo options present.
void ApAnimationPlayer::SetSize(u32 width, u32 height) {
  destination_width = width;
  destination_height = height;
  if (options) {
    options->flags |= kApVideoDestinationSizeMask;
    options->destination_width = destination_width;
    options->destination_height = destination_height;
  }
}

bool ApAnimationPlayer::SetPalette() {
  return false;
}

bool ApAnimationPlayer::Goto(u32) {
  return false;
}

// Resume outstanding pauses before stopping an opened vendor instance, then reset the player's status.
// The platform-owned playback status is reloaded while the original completion condition is pending.
bool ApAnimationPlayer::Stop() {
  if (opened) {
    if (video && options->playback_status != kApVideoStopRequested) {
      while (pause_count)
        Resume();
      if (options->playback_status != kApVideoStopRequested) {
        uV_Stop(video);
        if (options->playback_status != kApVideoFinished && options->playback_status != kApVideoInactive)
          while (options->playback_status != kApVideoStopRequested) {
          }
      }
    }
    status = 0;
  }
  return true;
}

// Increment the nested pause count and toggle the vendor pause only for the first active pause.
bool ApAnimationPlayer::Pause() {
  if (!opened || !video)
    return false;
  if (++pause_count == 1)
    uV_Pause(video);
  return true;
}

// Remove one nested pause and toggle the vendor pause only when the final pause is released.
bool ApAnimationPlayer::Resume() {
  if (!opened || !video || !pause_count)
    return false;
  if (--pause_count == 0)
    uV_Pause(video);
  return true;
}

// Store the requested volume and forward its original signed fixed-point scaling to an opened vendor instance.
bool ApAnimationPlayer::SetVolume(i32 volume) {
  sound_volume = volume;
  if (opened && video) {
    u32 bits = static_cast<u32>(volume) << 15;
    i32 shifted;
    memcpy(&shifted, &bits, sizeof(shifted));
    uV_SetAudioVolume(video, shifted / 1024);
  }
  return true;
}

// Read the client rectangle with the platform's existing failure handling.
static bool ApVideoWindowRect(void *window, ScRect *rectangle) {
  i32 width, height;
  if (!SDL_GetWindowSize(static_cast<SDL_Window *>(window), &width, &height))
    return false;
  *rectangle = {0, 0, width, height};
  return true;
}

// Select the platform window destination and center the movie inside its current client rectangle.
void ApAnimationPlayer::SetWindow(void *value) {
  window = value;
  destination_type = Destination::Window;
  ScRect rectangle;
  if (!ApVideoWindowRect(value, &rectangle))
    return;
  CenterBufferInRect(&rectangle);
}

// Stop an opened playing or ended movie through the actual virtual stop method, then reset its status.
void ApAnimationPlayer::Reset() {
  if (opened && (status == 1 || status == 2)) {
    Stop();
    status = 0;
  }
}

void ApAnimationPlayer::GetSummary(ApAnimationPlaySummary *out) {
  out->frames_per_second = 0;
}

// Center the selected destination or intrinsic movie size within the supplied rectangle.
// Store both offsets before dispatching the original virtual position update.
void ApAnimationPlayer::CenterBufferInRect(ScRect *rectangle) {
  i32 width = static_cast<i32>(destination_width ? destination_width : video_width);
  i32 height = static_cast<i32>(destination_height ? destination_height : video_height);
  i64 horizontal = static_cast<i64>(rectangle->right) - rectangle->left;
  i64 vertical = static_cast<i64>(rectangle->bottom) - rectangle->top;
  destination_x = static_cast<i32>(horizontal / 2 - width / 2);
  destination_y = static_cast<i32>(vertical / 2 - height / 2);
  SetPosition(destination_x, destination_y);
}

bool ApAnimationPlayer::SetFrameRate(u32) {
  return false;
}

void ApAnimationPlayer::SetDestinationBufferColorType(GxBuffer *buffer) {
  GxColorType color(GxColorType::Color::RGB16);
  buffer->GetColorType(&color);
  SetDestinationBufferColorType(color);
}

void ApAnimationPlayer::SetDestinationBufferColorType(const GxColorType &) {
}

u32 ApAnimationPlayer::GetVideoWidth() {
  return video_width;
}

u32 ApAnimationPlayer::GetVideoHeight() {
  return video_height;
}

u32 ApAnimationPlayer::GetDestinationWidth() {
  return destination_width;
}

u32 ApAnimationPlayer::GetDestinationHeight() {
  return destination_height;
}

bool ApAnimationPlayer::GetInfo(ApAnimationFileInfo *out) {
  *out = {};
  return false;
}

bool ApAnimationPlayer::GetInfo(const ScString &filename, ApAnimationFileInfo *out) {
  return GetInfo(filename.begin, out);
}

bool ApAnimationPlayer::GetInfo(const char *, ApAnimationFileInfo *out) {
  *out = {};
  return false;
}

// Copy the executable player's fixed version string to the caller's character buffer.
char *ApAnimationPlayer::GetPlayerVersion(char *out) {
  memcpy(out, "1.60", 5);
  return out;
}

void ApAnimationPlayer::GetPlayerVersion(ScString *out) {
  ScString version("1.60");
  *out = version;
}

bool ApAnimationPlayer::CanSetOptions() {
  return destination_type == Destination::PrimaryScreen || front_buffer != nullptr;
}


// Retain the SDL destination before the caller clears its movie options.
static GxBuffer *ApVideoRetainOptionsTarget([[maybe_unused]] ApVideoOptions *options,
    [[maybe_unused]] const ApAnimationPlayer::Destination *destination, [[maybe_unused]] GxBuffer *const *front) {
  GxBuffer *target = *destination == ApAnimationPlayer::Destination::PrimaryScreen ? nullptr : *front;
  if (target)
    target->AddRef();
  if (options->sdl_target)
    options->sdl_target->Release();
  return target;
}

// Populate the selected backend's movie surfaces and window after the options have been cleared.
static void ApVideoConfigureOptionsTarget(ApVideoOptions *options,
    [[maybe_unused]] const ApAnimationPlayer::Destination *destination, [[maybe_unused]] GxBuffer *const *front,
    [[maybe_unused]] GxBuffer *const *back, void **window,
    [[maybe_unused]] GxBuffer *target) {
  options->sdl_target = target;
  *window = GxSdlWindow();
}

// Retain the original page-flipping back-surface overwrite in Windows reference builds.
static void ApVideoRefreshOptionsBackSurface([[maybe_unused]] ApVideoOptions *options) {
}

// Populate the backend-specific audio fields and playback flag using the original service lookup order.
void ApAnimationPlayer::ConfigureOptionsSound() {
  AuDevice *sound = static_cast<AuDevice *>(ApServices::SS());
  if (sound && sound->GetInitFlag() && sound->sdlDevice)
    options->flags |= kApVideoEnableAudio;
}

// Initialize the vendor's screen, surface, position and audio options from the live graphics and sound systems.
// Preserve separate service lookups, retained DirectDraw surface queries and the original page-flipping overwrite.
void ApAnimationPlayer::SetOptions() {
  GxBuffer *target = ApVideoRetainOptionsTarget(options, &destination_type, &front_buffer);
  memset(options, 0, sizeof(*options));
  options->display_mode = 1;
  options->flags |= kApVideoDefaultPlaybackFlags;
  options->flags |= kApVideoDefaultTargetFlags;
  ApVideoConfigureOptionsTarget(options, &destination_type, &front_buffer, &back_buffer, &window, target);
  if (looping)
    options->flags |= kApVideoLoopPlayback;
  ApVideoRefreshOptionsBackSurface(options);
  if (destination_x) {
    options->flags |= kApVideoDestinationX;
    options->destination_x = destination_x;
  }
  if (destination_y) {
    options->flags |= kApVideoDestinationY;
    options->destination_y = destination_y;
  }
  if (destination_width) {
    options->flags |= kApVideoDestinationWidth;
    options->destination_width = destination_width;
  }
  if (destination_height) {
    options->flags |= kApVideoDestinationHeight;
    options->destination_height = destination_height;
  }
  if (sound_volume > 128)
    ConfigureOptionsSound();
}
