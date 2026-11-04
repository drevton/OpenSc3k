#ifndef SC3_TERRAIN_TR_THUMBNAIL_H
#define SC3_TERRAIN_TR_THUMBNAIL_H

#include "common/sc3_unknown.h"
#include "terrain/tr_generator.h"

class ScDirtBag;
class GxBuffer;

// Actual independent thumbnail random receiver at10025BD8, seeded from each thumbnail's stored seed before drawing.
extern TrRandom TrThumbnailRandom;

// Genuine cISC3NeighborMapGen prefix: six virtual methods, without a virtual interface destructor.
// Private nominal bool/BYTE and cleanup return declarations remain unresolved; native effect carriers retain the ABI.
class TrNeighborMapInterface : public ScUnknown {
public:
  virtual u8 Init(ScDirtBag *terrain) = 0;
  virtual void Shutdown() = 0;
  virtual u8 DrawMap(GxBuffer *target) = 0;
};

// Original cSC3NeighborMapGen receiver owns retained terrain, a color resource, salt-edge bytes and an independent seed.
class TrNeighborMap : public TrNeighborMapInterface {
public:
  TrNeighborMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Original slots12/16/20; Init/DrawMap expose AL; their bool/BYTE spelling and cleanup return remain unresolved.
  u8 Init(ScDirtBag *terrain) override;
  void Shutdown() override;
  u8 DrawMap(GxBuffer *target) override;
  virtual ~TrNeighborMap();

protected:
  // Source verifies the separate inputs and nonconst receivers; private nominal returns remain unresolved.
  void CreateLand(GxBuffer *target);
  u32 ConvertAltToColor(GxBuffer *target, i8 height);
  ScDirtBag *terrain;
  GxBuffer *colors;
  u8 west, east, north, south;
  u32 seed, references;
};

#endif
