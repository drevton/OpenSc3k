#ifndef SC3_CITY_SC_RANDOM_H
#define SC3_CITY_SC_RANDOM_H

#include "common/sc3.h"

// Original24-byte random receiver: independent integer/double streams and a lazily initialized Gaussian companion.
class ScRandom {
public:
  explicit ScRandom(u32 seed);
  void SetSeed(u32 seed);
  u32 Next();
  u32 Bounded(u32 bound);
  i32 Range(i32 minimum, i32 maximum);
  i32 RandomSint32RangeGaussian(i32 minimum, i32 maximum);
  f64 RandomDoubleUniform();
  f64 RandomDoubleGaussian();
  f64 RandomDoubleRangeUniform(f64 minimum, f64 maximum);
  f64 RandomDoubleRangeGaussian(f64 minimum, f64 maximum);
  f64 RandomDoubleRangeGaussianFast(f64 minimum, f64 maximum);
  u32 integerSeed, currentDoubleSeed;
  i32 iset;
  f64 gset;
};

static_assert(sizeof(ScRandom) == 24);
static_assert(offsetof(ScRandom, iset) == 8);
static_assert(offsetof(ScRandom, gset) == 16);

u32 CtSeedFromTime();

#endif
