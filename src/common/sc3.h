#ifndef SC3_COMMON_SC3_H
#define SC3_COMMON_SC3_H

#if defined(_WIN32) && !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if !defined(__cplusplus) && !defined(bool)
typedef _Bool bool;
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;
typedef size_t usize;
typedef ptrdiff_t isize;
typedef uintptr_t uptr;
typedef intptr_t iptr;

#ifdef __cplusplus

inline constexpr f64 kScDoubleIntegerBias = 4503601774854144.0;
inline constexpr u32 kScDoubleIntegerLowWordBias = 0x80000000u;
inline constexpr f64 kScDoubleNearestIntegerBias = 6755399441055744.0;

// Monotonic timespec samples convert nanoseconds to whole milliseconds.
inline constexpr u32 kScNanosecondsPerMillisecond = 1000000;

// Game random streams use this seed to request one uptime-clock sample instead of a supplied seed.
inline constexpr u32 kScRandomSeedFromClock = UINT32_MAX;

// The imported Windows CRT random sequence has its own multiplier and increment.
inline constexpr u32 kScCrtRandomMultiplier = 214013u;
inline constexpr u32 kScCrtRandomIncrement = 2531011u;

// Stride-table permutations cap their supported population before selecting a relatively-prime stride.
inline constexpr i32 kScPermutationMaximumCount = 130444;

// The reference GetVersion result marks non-NT Windows releases with this family bit.
inline constexpr u32 kScLegacyWindowsVersionFlag = 0x80000000u;

// Adding this unsigned step moves one cell backward while retaining the original coordinate wraparound.
inline constexpr u32 kScBackwardGridStep = UINT32_MAX;

// Packed bit-grid fills set every bit in an allocated word, including unused trailing cells.
inline constexpr u32 kScFilledBitGridWord = UINT32_MAX;

// Signed-word tests use the top bit without changing the original unsigned arithmetic.
inline constexpr u32 kScIntegerSignBit = 0x80000000u;

// Wide random output combines one generated high half with a later generated low half.
inline constexpr u32 kScRandomRetainedHighWordMask = 0xffff0000u;

// Shared game generators use the same linear recurrence and threshold for bounded remainder selection.
inline constexpr u32 kScRandomMultiplier = 1103515245u;
inline constexpr u32 kScRandomIncrement = 12345u;
inline constexpr u32 kScRandomModuloThreshold = 0x00ffffffu;
inline constexpr u32 kScRandomFallbackRealSeed = 0x12345678u;
inline constexpr u32 kScRandomRealMultiplier = 663608941u;
// The original binary64 scale is seven ULPs above 2^-32; retain those low bits across both copied generators.
inline constexpr f64 kScRandomRealScale = 0x1.0000000000007p-32;

#ifndef _WIN32
inline thread_local u32 ScCrtRandomSeed = 1;
#endif

// Preserve the Windows CRT's per-thread random stream, including its 15-bit result and 32-bit seed wraparound.
// The native port follows rather than the host library's different generator.
inline int ScRand() {
#ifdef _WIN32
  return ::rand();
#else
  ScCrtRandomSeed = ScCrtRandomSeed * kScCrtRandomMultiplier + kScCrtRandomIncrement;
  return int((ScCrtRandomSeed >> 16) & 0x7fffu);
#endif
}

// Replace only the calling thread's CRT seed. Windows keeps sharing its native state with existing ABI clients.
inline void ScSrand(u32 seed) {
#ifdef _WIN32
  ::srand(seed);
#else
  ScCrtRandomSeed = seed;
#endif
}
#endif

// Packed RGB payload only; this does not establish ownership or the full declaration of a foreign color class.
typedef struct ScRgb8 {
  u8 red, green, blue;
} ScRgb8;

#if defined(_MSC_VER)
#define SC3_CDECL __cdecl
#define SC3_STDCALL __stdcall
#elif defined(__i386__)
#define SC3_CDECL __attribute__((cdecl))
#define SC3_STDCALL __attribute__((stdcall))
#else
#define SC3_CDECL
#define SC3_STDCALL
#endif

#ifdef _WIN32
#define SC3_PRId64 "I64d"
#else
#define SC3_PRId64 PRId64
#endif

#ifdef __cplusplus
#define SC3_C_LINKAGE extern "C"
#else
#define SC3_C_LINKAGE
#endif

#if defined(_WIN32) && defined(SC3_BUILD)
#define SC3_API SC3_C_LINKAGE __declspec(dllexport)
#elif defined(_WIN32) && !defined(SC3_STATIC)
#define SC3_API SC3_C_LINKAGE __declspec(dllimport)
#else
#define SC3_API SC3_C_LINKAGE
#endif

#endif
