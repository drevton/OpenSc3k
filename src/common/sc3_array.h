#ifndef SC3_COMMON_SC3_ARRAY_H
#define SC3_COMMON_SC3_ARRAY_H

#include <stdlib.h>

#include "common/sc3.h"

void *ScArrayGrow(void *data, u32 *capacity, u32 size, usize element_size);
struct CtCoord;
// Coordinate-specific original sort/unique effects; unlike an erase operation, this preserves the array's live size.
void ScSortCellPoints(CtCoord *begin, CtCoord *end);

// Geometric storage for trivial game records. The pointed-to type supplies the element size and append result type.
template<class T> struct ScArray {
  static_assert(__is_trivially_copyable(T));
  T *data = nullptr;
  u32 size = 0, capacity = 0;

  // Start empty without allocating. Field initializers establish the shared size/capacity invariant.
  ScArray() = default;

  // Each array owns its allocation and cannot be copied as a second independent owner.
  ScArray(const ScArray &) = delete;

  // Replacing an owner through assignment is intentionally unavailable.
  ScArray &operator=(const ScArray &) = delete;

  // Release trivial record storage without attempting game-object lifetime operations.
  ~ScArray() {
    free(data);
  }

  // Return a typed slot for one new record, growing capacity geometrically only when needed.
  T *Append() {
    data = static_cast<T *>(ScArrayGrow(data, &capacity, size, sizeof(T)));
    return &data[size++];
  }

  // Reserve space without changing the live range. Existing records retain their values and order.
  void Reserve(u32 count) {
    if (count > capacity)
      data = static_cast<T *>(ScArrayGrow(data, &capacity, count - 1, sizeof(T)));
  }

  // Clear the live range while retaining allocation, matching the source's trivial vector erasure.
  void Clear() {
    size = 0;
  }
};

#endif
