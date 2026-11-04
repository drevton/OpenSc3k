#ifndef SC3_TERRAIN_TR_GENERATOR_H
#define SC3_TERRAIN_TR_GENERATOR_H

#include "common/api.h"
#include "common/sc3.h"
#include "terrain/tr_dirt_bag.h"

class ScString;
class ScStringView;
class ScDirtBag;
struct TrByteGrid;
struct TrChangeGrid;

// Original generator receiver, distinct from city terrain storage and the terrain renderer.
// Public operations retain the verified Unlimited contracts; private nominal declaration gaps are recorded below.
class TrGenerator : public TrGeneratorInterface {
public:
  TrGenerator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 cells_x, u32 cells_z) override;
  // Unlimited cleanup is void; the SDK Boolean declaration consumes an undefined result.
  void Shutdown() override;
  void SetDifficulty(i32 difficulty) override;
  bool IsReady() const override;
  bool GenerateRandom(i32 seed, u8 control3, u8 control4, u8 control5, u8 flags) override;
  bool GenerateAltFromBMP8(const ScStringView *name, u8 threshold) override;
  bool GenerateAltFromBMP24(const ScStringView *name) override;
  bool ReflectMap(ScDirtBag *target) override;
  virtual ~TrGenerator();

protected:
  // Concrete Linux symbols verify the private names and inputs; unused nominal returns remain unresolved.
  // The following methods carry the already recovered effects rather than asserting those missing return declarations.
  void AltitudeSubdivision(u32 left, u32 top, u32 right, u32 bottom, u32 amplitude, u32 depth);
  void FixShores(u8 flags);
  bool ChangeTerrainProfile(u8 first, u8 second, u8 third, u8 fourth);
  // Native effect phases of181D6/18427; no undocumented original private result is exposed.
  void FindSaltWater(u8 flags, u8 control4);
  void CreateFlora(u8 density);
  // Verified river effects retain the original signed-byte arm depth; unconsumed nominal returns remain unresolved.
  void CreateRivers(u8 flags, u8 control4);
  void TraceDepth(i32 x, i32 z, i32 dx, i32 dz, i8 depth);
  // Original fields: dimensions+4/+8, bitmap name+12, threshold+16, four grids+20/+24/+28/+32.
  u32 width, height;
  ScString *bitmap_name;
  u8 threshold;
  TrByteGrid *ground, *water, *flora;
  TrChangeGrid *shore;
  // Sea+36 and difficulty+40 are initialized by the constructor; reference count+44 drives module lifetime.
  u8 sea;
  i32 difficulty;
  u32 references;
};

// Original module-local 24-byte random receiver. The Gaussian cache remains present beside both uniform stream seeds.
struct TrRandom {
  explicit TrRandom(u32 seed);
  u32 state, real_state;
  i32 normal_ready;
  f64 cached_normal;

  // The original seed method resets both streams while preserving the independent Gaussian-cache state.
  void Seed(u32 seed);
  u32 RandomUint32Uniform();
  u32 RandomUint32Uniform(u32 bound);
  i32 RandomSint32RangeUniform(i32 lower, i32 upper);
  i32 RandomSint32RangeGaussian(i32 lower, i32 upper);
  i32 RandomSint32RangeGaussianRight(i32 lower, i32 upper);
  i32 RandomSint32RangeGaussianFast(i32 lower, i32 upper);
  i32 RandomSint32RangeGaussianRightFast(i32 lower, i32 upper);
  f64 RandomDoubleUniform();
  f64 RandomDoubleRangeUniform(f64 lower, f64 upper);
};

static_assert(sizeof(TrRandom) == 24);
static_assert(offsetof(TrRandom, cached_normal) == 16);

// Original independent terrain random object at10025BC0, initialized with the clock sentinel at module startup.
extern TrRandom TrTerrainRandom;

// Original Bezier2D4Controls<cRZPoint> has a void result and four nonconst point-reference inputs.
// Spell those references as typed pointers and retain live input/output aliasing and the original count argument.
void TrSampleRiver(ScPoint *first, ScPoint *second, ScPoint *third, ScPoint *last, ScPoint *output, i32 count);

#endif
