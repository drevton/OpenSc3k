#include "common/utf8.h"

// Decode a complete Unicode scalar while rejecting overlong forms, surrogate values and out-of-range sequences.
// Failure does not publish a scalar, so external callers can reject invalid input without inventing replacement text.
u32 ScUtf8Decode(const char *bytes, u32 length, u32 *scalar) {
  if (!length)
    return 0;
  u8 first = static_cast<u8>(bytes[0]);
  u32 count, value;
  if (first < 0x80) {
    count = 1;
    value = first;
  } else if (first >= 0xc2 && first <= 0xdf) {
    count = 2;
    value = first & 0x1f;
  } else if (first >= 0xe0 && first <= 0xef) {
    count = 3;
    value = first & 0x0f;
  } else if (first >= 0xf0 && first <= 0xf4) {
    count = 4;
    value = first & 7;
  } else
    return 0;
  if (count > length)
    return 0;
  for (u32 i = 1; i < count; ++i) {
    u8 next = static_cast<u8>(bytes[i]);
    if ((next & 0xc0) != 0x80)
      return 0;
    value = (value << 6) | (next & 0x3f);
  }
  if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) || (count == 4 && value < 0x10000) ||
      (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
    return 0;
  if (scalar)
    *scalar = value;
  return count;
}

// Encode one scalar directly into UTF-8, leaving the output untouched for invalid Unicode values.
u32 ScUtf8Encode(u32 scalar, char *out) {
  if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
    return 0;
  if (scalar < 0x80) {
    out[0] = static_cast<char>(scalar);
    return 1;
  }
  u32 count = scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
  for (u32 i = count - 1; i; --i) {
    out[i] = static_cast<char>(0x80 | (scalar & 0x3f));
    scalar >>= 6;
  }
  out[0] = static_cast<char>((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | scalar);
  return count;
}

bool ScUtf8Validate(const char *bytes, u32 length) {
  u32 offset = 0;
  while (offset < length) {
    u32 count = ScUtf8Decode(bytes + offset, length - offset, nullptr);
    if (!count)
      return false;
    offset += count;
  }
  return true;
}

// Find a preceding scalar without scanning before the supplied prefix or accepting an incomplete trailing sequence.
u32 ScUtf8PreviousSize(const char *bytes, u32 length) {
  if (!length)
    return 0;
  u32 start = length - 1;
  while (start && length - start < 4 && (static_cast<u8>(bytes[start]) & 0xc0) == 0x80)
    --start;
  u32 count = ScUtf8Decode(bytes + start, length - start, nullptr);
  return count == length - start ? count : 0;
}

// Stop truncation before the first scalar that would exceed the byte capacity or contain malformed input.
u32 ScUtf8Prefix(const char *bytes, u32 length, u32 capacity) {
  u32 offset = 0;
  while (offset < length && offset < capacity) {
    u32 count = ScUtf8Decode(bytes + offset, length - offset, nullptr);
    if (!count || count > capacity - offset)
      break;
    offset += count;
  }
  return offset;
}

// Keep an offset inside the byte span and move continuation-byte positions back to their scalar's leading byte.
u32 ScUtf8Floor(const char *bytes, u32 length, u32 offset) {
  if (offset >= length)
    return length;
  while (offset && (static_cast<u8>(bytes[offset]) & 0xc0) == 0x80)
    --offset;
  return offset;
}

// Equal-delta ranges use a stride of one or two to retain the alternating Latin/Cyrillic scalar mappings compactly.
struct ScUnicodeCaseRange {
  u32 first, last, step;
  i32 delta;
};

#include "common/unicode_case.inc"

// Locate the first range whose end reaches this scalar, retaining unmapped gaps and alternating-stride exclusions.
static u32 ScUnicodeCase(u32 scalar, const ScUnicodeCaseRange *ranges, u32 count) {
  u32 first = 0, last = count;
  while (first < last) {
    u32 middle = first + (last - first) / 2;
    if (ranges[middle].last < scalar)
      first = middle + 1;
    else
      last = middle;
  }
  if (first == count || scalar < ranges[first].first || (scalar - ranges[first].first) % ranges[first].step)
    return scalar;
  return static_cast<u32>(static_cast<i32>(scalar) + ranges[first].delta);
}

u32 ScUnicodeLower(u32 scalar) {
  return ScUnicodeCase(scalar, kScUnicodeLower, sizeof(kScUnicodeLower) / sizeof(*kScUnicodeLower));
}

u32 ScUnicodeUpper(u32 scalar) {
  return ScUnicodeCase(scalar, kScUnicodeUpper, sizeof(kScUnicodeUpper) / sizeof(*kScUnicodeUpper));
}
