#ifndef SC3_COMMON_RECT_H
#define SC3_COMMON_RECT_H

#include "common/sc3.h"

// Return the smaller signed integer. Compare directly so extreme values cannot overflow.
static inline i32 ScIntMin(i32 a, i32 b) {
  return a < b ? a : b;
}

// Return the larger signed integer. Compare directly so extreme values cannot overflow.
static inline i32 ScIntMax(i32 a, i32 b) {
  return a > b ? a : b;
}

// Return the smaller unsigned integer. Preserve the full 32-bit range.
static inline u32 UintMin(u32 a, u32 b) {
  return a < b ? a : b;
}

// Return the larger unsigned integer. Preserve the full 32-bit range.
static inline u32 ScUintMax(u32 a, u32 b) {
  return a > b ? a : b;
}

// Return the smaller signed 64-bit integer. Avoid subtraction at the signed limits.
static inline i64 ScInt64Min(i64 a, i64 b) {
  return a < b ? a : b;
}

// Return the larger signed 64-bit integer. Avoid subtraction at the signed limits.
static inline i64 ScInt64Max(i64 a, i64 b) {
  return a > b ? a : b;
}

// Return the smaller unsigned 64-bit integer. Preserve values above INT64_MAX.
static inline u64 ScUint64Min(u64 a, u64 b) {
  return a < b ? a : b;
}

// Return the larger unsigned 64-bit integer. Preserve values above INT64_MAX.
static inline u64 ScUint64Max(u64 a, u64 b) {
  return a > b ? a : b;
}

// Clamp a signed integer to inclusive bounds. Callers supply minimum no greater than maximum.
static inline i32 ScClampInt(i32 value, i32 minimum, i32 maximum) {
  return ScIntMax(minimum, ScIntMin(value, maximum));
}

// Clamp an unsigned integer to inclusive bounds. Do not convert large values through a signed type.
static inline u32 ScClampUint(u32 value, u32 minimum, u32 maximum) {
  return ScUintMax(minimum, UintMin(value, maximum));
}

// Clamp a signed 64-bit integer to inclusive bounds. Callers supply minimum no greater than maximum.
static inline i64 ScClampInt64(i64 value, i64 minimum, i64 maximum) {
  return ScInt64Max(minimum, ScInt64Min(value, maximum));
}

// Clamp an unsigned 64-bit integer to inclusive bounds. Preserve the full unsigned range throughout.
static inline u64 ScClampUint64(u64 value, u64 minimum, u64 maximum) {
  return ScUint64Max(minimum, ScUint64Min(value, maximum));
}

struct ScRect {
  i32 left, top, right, bottom;
#ifdef __cplusplus
  bool Intersect(const ScRect *first, const ScRect *second);
  bool IntersectInclusive(const ScRect *first, const ScRect *second);
  i32 Union(const ScRect *first, const ScRect *second);
#endif
};

#endif
