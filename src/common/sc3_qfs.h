#ifndef SC3_COMMON_SC3_QFS_H
#define SC3_COMMON_SC3_QFS_H

#include "common/sc3.h"

u32 ScQfsGetSize(const u8 *source);
u32 ScQfsDecompress(const u8 *source, u32 *consumed, u8 *destination);
#ifndef NDEBUG
void ScQfsAssertBounds(const u8 *source, u32 source_size, u32 capacity);
#endif

// Reached methods of the codec subobject embedded at archive+254. No codec state is read by these two bodies;
// construction and the remaining private fields remain unported rather than inferred from the unused receiver.
class ScQfs {
public:
  bool Decompress(const u8 *source, u32 payload_size, u8 *destination, u32 *written);
  u32 GetSize(const u8 *source);
};

#endif
