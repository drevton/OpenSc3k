#include "city/api.h"
#include "common/sc3_array.h"
#include "common/sc3_error.h"

// Ensure room for one additional trivial record with geometric growth. Allocation and impossible lengths fail globally.
void *ScArrayGrow(void *data, u32 *capacity, u32 size, usize element_size) {
  if (!element_size || size == UINT32_MAX || (usize)size >= SIZE_MAX / element_size)
    ScFatal(SC_ERROR_INVALID, "Array length exceeds the addressable range.");
  if (size < *capacity)
    return data;
  usize limit = SIZE_MAX / element_size;
  if (limit > UINT32_MAX)
    limit = UINT32_MAX;
  u32 next = *capacity ? *capacity : 8;
  if (next > limit)
    next = (u32)limit;
  while (next <= size)
    next = next > limit / 2 ? (u32)limit : next * 2;
  void *grown = realloc(data, (usize)next * element_size);
  if (!grown)
    ScFatal(SC_ERROR_NOMEM, "Out of memory while growing an array.");
  *capacity = next;
  return grown;
}

static bool ScCellPointLess(const CtCoord &first, const CtCoord &second) {
  return first.x < second.x || (first.x == second.x && first.z < second.z);
}

// Select the original median-of-three pivot, retaining its tie decisions without moving any coordinate.
static CtCoord ScCellPointMedian(const CtCoord &first, const CtCoord &middle, const CtCoord &last) {
  if (ScCellPointLess(first, middle)) {
    if (ScCellPointLess(middle, last))
      return middle;
    return ScCellPointLess(first, last) ? last : first;
  }
  if (ScCellPointLess(first, last))
    return first;
  return ScCellPointLess(middle, last) ? last : middle;
}

// Restore a coordinate heap by descending through the larger child, choosing the right child on equal keys.
static void ScCellPointAdjustHeap(CtCoord *first, i32 hole, i32 count, const CtCoord &value) {
  i32 top = hole, child = hole * 2 + 2;
  while (child < count) {
    if (ScCellPointLess(first[child], first[child - 1]))
      --child;
    first[hole] = first[child];
    hole = child;
    child = child * 2 + 2;
  }
  if (child == count) {
    first[hole] = first[child - 1];
    hole = child - 1;
  }
  i32 parent = (hole - 1) / 2;
  while (hole > top && ScCellPointLess(first[parent], value)) {
    first[hole] = first[parent];
    hole = parent;
    parent = (parent - 1) / 2;
  }
  first[hole] = value;
}

// Execute the original coordinate-specific heap fallback when the partition depth limit is exhausted.
// Every exchange copies the full altitude payload while ordering only the signed X/Z keys.
static void ScCellPointHeapSort(CtCoord *first, CtCoord *last) {
  i32 count = i32(last - first);
  if (count >= 2)
    for (i32 parent = (count - 2) / 2;; --parent) {
      CtCoord value = first[parent];
      ScCellPointAdjustHeap(first, parent, count, value);
      if (!parent)
        break;
    }
  while (last - first > 1) {
    --last;
    CtCoord value = *last;
    *last = *first;
    ScCellPointAdjustHeap(first, 0, i32(last - first), value);
  }
}

// Reproduce the original partition exchanges and right-first recursion down to sixteen-coordinate ranges.
// The partition pivot is a copied coordinate; equal keys exchange their payloads rather than being stably sorted.
static void ScCellPointPartitionSort(CtCoord *first, CtCoord *last, u32 depth) {
  while (last - first > 16) {
    if (!depth) {
      ScCellPointHeapSort(first, last);
      return;
    }
    --depth;
    CtCoord pivot = ScCellPointMedian(*first, first[(last - first) / 2], last[-1]);
    CtCoord *left = first, *right = last;
    for (;;) {
      while (ScCellPointLess(*left, pivot))
        ++left;
      do
        --right;
      while (ScCellPointLess(pivot, *right));
      if (left >= right)
        break;
      CtCoord value = *left;
      *left++ = *right;
      *right = value;
    }
    ScCellPointPartitionSort(left, last, depth);
    last = left;
  }
}

// Insert a saved coordinate behind the established minimum sentinel, shifting only strictly greater keys.
static void ScCellPointInsertBack(CtCoord *position, const CtCoord &value) {
  CtCoord *previous = position - 1;
  while (ScCellPointLess(value, *previous)) {
    *position = *previous;
    position = previous--;
  }
  *position = value;
}

// Preserve the original vector end and equal-key permutations across the identical module instantiations.
void ScSortCellPoints(CtCoord *begin, CtCoord *end) {
  if (begin != end) {
    u32 count = u32(end - begin), depth = 0;
    for (u32 remaining = count; remaining != 1; remaining >>= 1)
      ++depth;
    ScCellPointPartitionSort(begin, end, depth * 2);
    CtCoord *prefix = count > 16 ? begin + 16 : end;
    for (CtCoord *position = begin + 1; position != prefix; ++position) {
      CtCoord value = *position;
      if (ScCellPointLess(value, *begin)) {
        for (CtCoord *destination = position; destination != begin; --destination)
          *destination = destination[-1];
        *begin = value;
      } else
        ScCellPointInsertBack(position, value);
    }
    for (CtCoord *position = prefix; position != end; ++position) {
      CtCoord value = *position;
      ScCellPointInsertBack(position, value);
    }
    CtCoord *last = begin;
    for (CtCoord *position = begin + 1; position != end; ++position)
      if (position->x != last->x || position->z != last->z) {
        ++last;
        if (last != position)
          *last = *position;
      }
  }
}
