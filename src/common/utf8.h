#ifndef SC3_COMMON_UTF8_H
#define SC3_COMMON_UTF8_H

#include "common/sc3.h"

// Native UTF-8 boundaries retain byte lengths/offsets; they do not reinterpret them as character indices.
// Decode one scalar, returning its byte width or zero for empty, malformed or incomplete input.
// A nullable scalar output is written only on success; embedded U+0000 remains a valid one-byte scalar.
u32 ScUtf8Decode(const char *bytes, u32 length, u32 *scalar);
// Write one scalar to a caller-owned four-byte buffer, without a terminator; invalid scalars return zero unchanged.
u32 ScUtf8Encode(u32 scalar, char *out);
// Validate the entire byte span without replacing or dropping malformed input.
bool ScUtf8Validate(const char *bytes, u32 length);
// Return the complete scalar ending at this prefix length, or zero for an empty/invalid tail.
u32 ScUtf8PreviousSize(const char *bytes, u32 length);
// Return the longest complete valid prefix within a byte capacity; malformed input ends that prefix.
u32 ScUtf8Prefix(const char *bytes, u32 length, u32 capacity);
// Clamp a byte offset backward to a scalar boundary in trusted, valid UTF-8.
u32 ScUtf8Floor(const char *bytes, u32 length, u32 offset);

// Apply Unicode 17.0.0 simple scalar casing, returning unmapped values unchanged without string expansions.
u32 ScUnicodeLower(u32 scalar);
u32 ScUnicodeUpper(u32 scalar);

#endif
