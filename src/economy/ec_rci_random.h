#ifndef SC3_ECONOMY_EC_RCI_RANDOM_H
#define SC3_ECONOMY_EC_RCI_RANDOM_H

#include "common/sc3.h"

namespace EcRci {
// SIMRCI's copied common random receiver keeps independent integer and floating streams.
class ScRandom {
public:
  explicit ScRandom(u32 seed);
  void SetSeed(u32 seed);
  u32 Bounded(u32 bound);
  f64 RandomDoubleUniform();

private:
  u32 integer_seed, real_seed;
  i32 normal_ready;
  f64 normal_value;
};
} // namespace EcRci

#endif
