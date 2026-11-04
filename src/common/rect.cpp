#include "common/rect.h"

// Store the intersection of two pixel rectangles, or clear all four edges when they do not overlap.
bool ScRect::Intersect(const ScRect *first, const ScRect *second) {
  if (first->right <= second->left || first->left >= second->right || first->bottom <= second->top ||
      first->top >= second->bottom) {
    bottom = top = right = left = 0;
    return false;
  }
  left = ScIntMax(first->left, second->left);
  top = ScIntMax(first->top, second->top);
  right = ScIntMin(first->right, second->right);
  bottom = ScIntMin(first->bottom, second->bottom);
  return true;
}

// Intersect coordinate bounds including their boundary edges, so an edge or corner contact succeeds.
// Disjoint bounds clear the destination in the original store order; input/output aliasing retains live edge reads.
bool ScRect::IntersectInclusive(const ScRect *first, const ScRect *second) {
  if (first->right < second->left || first->left > second->right || first->bottom < second->top ||
      first->top > second->bottom) {
    bottom = 0;
    top = 0;
    right = 0;
    left = 0;
    return false;
  }
  left = ScIntMax(first->left, second->left);
  top = ScIntMax(first->top, second->top);
  right = ScIntMin(first->right, second->right);
  bottom = ScIntMin(first->bottom, second->bottom);
  return true;
}

// Return the original full DWORD Boolean indicating that neither resulting dimension is zero.
i32 ScRect::Union(const ScRect *first, const ScRect *second) {
  left = ScIntMin(first->left, second->left);
  top = ScIntMin(first->top, second->top);
  right = ScIntMax(first->right, second->right);
  bottom = ScIntMax(first->bottom, second->bottom);
  return left != right && top != bottom;
}
