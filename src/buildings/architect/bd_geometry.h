#ifndef SC3_BUILDINGS_ARCHITECT_BD_GEOMETRY_H
#define SC3_BUILDINGS_ARCHITECT_BD_GEOMETRY_H

#include "common/sc3.h"

#if defined(_WIN32)
#define BD_STDCALL __stdcall
#else
#define BD_STDCALL
#endif

struct BdPoint {
  i32 x, y;
};

struct BdPointF {
  f32 x, y;
};

struct BdPoint3 {
  i32 x, y, z;
};

struct BdVolume {
  BdPoint3 origin, size;
};

struct BdRect {
  i32 left, top, right, bottom;

  bool Intersect(const BdRect *first, const BdRect *second);
  bool Unite(const BdRect *first, const BdRect *second);
  i32 Offset(const BdPoint *point);
};

// Preserve the observed output pointer result; the private pointer/reference/void declaration remains unresolved.
f32 *BD_STDCALL BdFaceNormal(u8 direction, f32 *x, f32 *y, f32 *z);

BdPoint3 *BdClampPoint(BdPoint3 *point, const BdVolume *volume);

bool BdContains(const BdPoint3 *point, const BdVolume *volume);
u32 BdShiftVertexMask(const u32 *mask, const BdPoint3 *offset);

// A queued face stores its tile immediately after the face byte, with source-compatible tail alignment.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
#pragma pack(push, 1)
struct alignas(4) BdPaintEntry {
  u32 cell;
  u8 face;
  BdPoint3 tile;
};
#pragma pack(pop)
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static_assert(sizeof(BdPaintEntry) == 20 && offsetof(BdPaintEntry, face) == 4 && offsetof(BdPaintEntry, tile) == 5);

extern BdPaintEntry BdPaintEntries[128];
extern i32 BdPaintQueued, BdPaintProcessed;
// Preserve the observed queue offset/full-distance result; the private nominal return is unresolved.
i32 BdQueuePaint(u32 cell, u8 face, i32 x, i32 y, i32 z);

#endif
