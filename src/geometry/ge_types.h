#ifndef SC3_GEOMETRY_GE_TYPES_H
#define SC3_GEOMETRY_GE_TYPES_H

#include "city/api.h"

struct GeCellRect {
  u32 left, top, right, bottom;
};

struct GeBounds {
  // Initialize both endpoints to the original unknown-coordinate sentinel.
  GeBounds()
      : minimum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        maximum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate} {
  }

  GeBounds &operator=(const GeBounds &) = default;

  // Initialize both endpoints before copying the original six coordinate words in field order.
  GeBounds(const GeBounds &source)
      : minimum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        maximum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate} {
    minimum = source.minimum;
    maximum = source.maximum;
  }

  // Initialize both endpoints, then copy the supplied by-value coordinate into each endpoint.
  GeBounds(CtCoord point)
      : minimum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        maximum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate} {
    minimum = point;
    maximum = point;
  }

  // Initialize both endpoints, then copy the two by-value coordinates without sorting or normalization.
  GeBounds(CtCoord first, CtCoord second)
      : minimum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        maximum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate} {
    minimum = first;
    maximum = second;
  }

  // Convert both unsigned cell corners to fixed-point coordinates after initializing the original sentinels.
  // Each endpoint receives altitude zero; the other words preserve 32-bit shift wraparound.
  GeBounds(const GeCellRect &rectangle)
      : minimum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        maximum{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate} {
    minimum.altitude = 0;
    minimum.x = i32(rectangle.left << 8);
    minimum.z = i32(rectangle.top << 8);
    maximum.altitude = 0;
    maximum.x = i32(rectangle.right << 8);
    maximum.z = i32(rectangle.bottom << 8);
  }

  CtCoord minimum, maximum;
  bool CoordInBoundsXZ(CtCoord point);
  void NormalizeXZ();
  void GrowXZCell(u32 cells);
  void IntersectXZ(const GeBounds &first, const GeBounds &second);
  void UnionXZ(const GeBounds &first, const GeBounds &second);
  void UnionXZ(const GeBounds &first, CtCoord point);
};

#endif
