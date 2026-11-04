#include "common/sc3_qfs.h"

#include <cassert>

// Read the requested number of big-endian bytes into the original DWORD accumulator. Header callers use two or three.
static u32 ScQfsReadBe(const u8 *source, u32 count) {
  u32 value = 0;
  for (; count; --count)
    value = (value << 8) + *source++;
  return value;
}

#ifndef NDEBUG
// Check QFS packet input ranges, back-references and destination capacity in debug builds.
// This validation does not decode data or add release-build failure behavior to the original codec.
void ScQfsAssertBounds(const u8 *source, u32 source_size, u32 capacity) {
  assert(source_size >= 1);
  u32 input = source[0] & 1 ? 8 : 5, output = 0;
  assert(input <= source_size);
  u32 declared = ScQfsReadBe(source + input - 3, 3);
  for (;;) {
    assert(input < source_size);
    u8 control = source[input++];
    u32 literals, length = 0, distance = 0;
    bool terminal = false;
    if (control < 0x80) {
      assert(source_size - input >= 1);
      u8 next = source[input++];
      literals = control & 3;
      length = ((control >> 2) & 7) + 3;
      distance = ((control & 0x60u) << 3) + next + 1;
    } else if (control < 0xc0) {
      assert(source_size - input >= 2);
      u8 first = source[input++], second = source[input++];
      literals = first >> 6;
      length = (control & 0x3f) + 4;
      distance = ((first & 0x3fu) << 8) + second + 1;
    } else if (control < 0xe0) {
      assert(source_size - input >= 3);
      u8 first = source[input++], second = source[input++], third = source[input++];
      literals = control & 3;
      length = ((control & 0x0cu) << 6) + third + 5;
      distance = ((control & 0x10u) << 12) + (static_cast<u32>(first) << 8) + second + 1;
    } else if (control < 0xfc)
      literals = (control & 0x1f) * 4 + 4;
    else {
      literals = control & 3;
      terminal = true;
    }
    assert(source_size - input >= literals && literals <= capacity - output);
    input += literals;
    output += literals;
    assert(!length || (distance <= output && length <= capacity - output));
    output += length;
    if (terminal)
      break;
  }
  assert(declared <= output);
}
#endif

u32 ScQfsGetSize(const u8 *source) {
  return ScQfsReadBe(source + (ScQfsReadBe(source, 2) == 0x10fb ? 2 : 5), 3);
}

// Expand literal and overlapping back-reference packets until the terminal control byte. Return the header's
// declared output length and optionally publish actual input consumption, without imposing new length checks.
u32 ScQfsDecompress(const u8 *source, u32 *consumed, u8 *destination) {
  if (!source) {
    if (consumed)
      *consumed = 0;
    return 0;
  }
  const u8 *input = source + (source[0] & 1 ? 5 : 2);
  u32 size = ScQfsReadBe(input, 3);
  input += 3;
  for (;;) {
    u8 control = *input++;
    u32 literals, length = 0, distance = 0;
    bool terminal = false;
    if (control < 0x80) {
      u8 next = *input++;
      literals = control & 3;
      length = ((control >> 2) & 7) + 3;
      distance = ((u32)(control & 0x60) << 3) + next + 1;
    } else if (control < 0xc0) {
      u8 first = *input++, second = *input++;
      literals = first >> 6;
      length = (control & 0x3f) + 4;
      distance = ((u32)(first & 0x3f) << 8) + second + 1;
    } else if (control < 0xe0) {
      u8 first = *input++, second = *input++, third = *input++;
      literals = control & 3;
      length = ((u32)(control & 0x0c) << 6) + third + 5;
      distance = ((u32)(control & 0x10) << 12) + ((u32)first << 8) + second + 1;
    } else if (control < 0xfc) {
      literals = (control & 0x1f) * 4 + 4;
    } else {
      literals = control & 3;
      terminal = true;
    }
    for (; literals; --literals)
      *destination++ = *input++;
    if (terminal)
      break;
    if (length) {
      const u8 *match = destination - distance;
      for (; length; --length)
        *destination++ = *match++;
    }
  }
  if (consumed)
    *consumed = (u32)(input - source);
  return size;
}

// Skip the outer length DWORD and decode the actual QFS stream. The codec ignores the supplied input size
// and returns whether the declared output length is nonzero, publishing that length through its output.
bool ScQfs::Decompress(const u8 *source, u32 payload_size, u8 *destination, u32 *written) {
  u32 result = ScQfsDecompress(source + 4, &payload_size, destination);
  *written = result;
  return result != 0;
}

u32 ScQfs::GetSize(const u8 *source) {
  return ScQfsGetSize(source + 4);
}
