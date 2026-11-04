#ifndef SC3_TERRAIN_TR_CONTOUR_H
#define SC3_TERRAIN_TR_CONTOUR_H

#include "common/sc3.h"

class ScDirtBag;
class SpSelection;

struct TrContourPoint {
  i16 x, z;
  u8 altitude, original;
};

// The original contour owns two point buffers followed by their independent live counts.
// Construction leaves counts unset until Start allocates missing buffers and resets both counts.
class TrContour {
public:
  TrContour();
  ~TrContour();
  // Complete private startup/flip effects; their nominal return declarations remain undocumented.
  u8 Start();
  void Flip();
  // Complete private reflection effects; their discarded nominal returns remain undocumented.
  void Reflect(ScDirtBag *terrain);
  void ReflectPrimary(ScDirtBag *terrain);
  void ReflectPrimaryHighway(ScDirtBag *terrain);
  TrContourPoint *current, *next;
  u32 current_count, next_count;

  // Full private7DC0 effects; the original nominal bool/BYTE declaration remains unresolved.
  u8 CreateRoadOutline(const ScDirtBag *terrain, SpSelection *selection, u32 *raised, u32 *lowered);
  u8 CreateHighwayOutline(const ScDirtBag *terrain, SpSelection *selection, u32 *raised, u32 *lowered);
  bool AddPri(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude);
  bool AddVertex(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude);
  // Windows stores a new raw provenance byte; Linux Boolean spelling does not justify normalizing this domain.
  bool AddSec(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude, u8 original);
  bool NeedsAdjustment(u32 index, TrContourPoint *point, u32 *raised, u32 *lowered);
  bool GrowAndFix(const ScDirtBag *terrain, u32 *raised, u32 *lowered);
  // Verified effects of private10007C90; its discarded nominal return declaration remains unresolved.
  void Bounds(u32 *left, u32 *top, u32 *right, u32 *bottom);

private:
  // Source-verified bounds helper inputs; nominal Boolean/BYTE return spelling remains unresolved.
  u8 OutOfBounds(const ScDirtBag *terrain, const TrContourPoint &point);
  // Native phases of the complete road-selection seed body.
  void AppendRoadPoint(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude);
  void SeedRoadCell(const ScDirtBag *terrain, i32 x, i32 z);
  // Effect-only decomposition of 10009AC0; callers ignore its undocumented private return declaration.
  void AddConnection(const ScDirtBag *terrain, u32 index, const TrContourPoint &point);
};

#endif
