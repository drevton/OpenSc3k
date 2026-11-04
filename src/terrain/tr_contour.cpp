#include "terrain/tr_contour.h"

#include <assert.h>

#include "city/api.h"
#include "common/rect.h"
#include "sprites/city_window.h"
#include "terrain/api.h"

TrContour::TrContour() : current(nullptr), next(nullptr) {
}

TrContour::~TrContour() {
  delete[] current;
  delete[] next;
}

u8 TrContour::Start() {
  if (!current)
    current = new TrContourPoint[1024];
  if (!next)
    next = new TrContourPoint[1024];
  current_count = 0;
  next_count = 0;
  return 1;
}

// Exchange the two real contour buffers and their corresponding counts after a completed propagation.
void TrContour::Flip() {
  TrContourPoint *points = current;
  current = next;
  next = points;
  u32 count = current_count;
  current_count = next_count;
  next_count = count;
}

static u32 TrContourDifference(i32 first, i32 second) {
  i32 difference = first - second;
  return (u32)(difference < 0 ? -difference : difference);
}

static u32 TrContourDistance(const TrContourPoint *first, const TrContourPoint *second) {
  return TrContourDifference(first->x, second->x) + TrContourDifference(first->z, second->z);
}

static bool TrContourSame(const TrContourPoint *first, const TrContourPoint *second) {
  return first->x == second->x && first->z == second->z;
}

// Encode the compass relation of a neighbor to the current vertex in the original eight-direction order.
static u32 TrContourDirection(const TrContourPoint *point, const TrContourPoint *center) {
  if (point->z < center->z)
    return point->x < center->x ? 0 : point->x > center->x ? 2 : 1;
  if (point->z > center->z)
    return point->x < center->x ? 6 : 5;
  return point->x >= center->x ? 3 : 7;
}

static TrContourPoint TrContourOffset(const TrContourPoint *center, u32 direction) {
  static const i32 dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1}, dz[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
  return {(i16)(center->x + dx[direction & 7]), (i16)(center->z + dz[direction & 7]), 0, 0};
}

// Read and mark the perimeter vertex before checking capacity, retaining those effects if the buffer is full.
// Coordinates are stored as packed WORDs and the original-perimeter flag is published before the count advances.
bool TrContour::AddPri(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude) {
  u8 old;
  terrain->GetVertexAltitudeDirt(x, z, &old);
  if (old != altitude)
    terrain->MarkVertexChanged(x, z);
  if (current_count >= 1024)
    return false;
  current[current_count].original = 1;
  current[current_count].x = (i16)x;
  current[current_count].z = (i16)z;
  current[current_count++].altitude = altitude;
  return true;
}

// Seed before rejecting a nonadjacent pair or an altitude jump above four. A rejected pair remains in the buffer.
bool TrContour::AddVertex(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude) {
  bool result = AddPri(terrain, x, z, altitude);
  if (current_count <= 1)
    return result;
  const TrContourPoint *previous = &current[current_count - 2], *point = &current[current_count - 1];
  if (TrContourDistance(point, previous) != 1 || TrContourDifference(point->altitude, previous->altitude) > 4)
    return false;
  return result;
}

bool TrContour::AddSec(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude, u8 original) {
  u8 old;
  terrain->GetVertexAltitudeDirt(x, z, &old);
  if (old != altitude)
    terrain->MarkVertexChanged(x, z);
  if (next_count >= 1024)
    return false;
  if (next_count && next[next_count - 1].x == (i16)x && next[next_count - 1].z == (i16)z) {
    next[next_count - 1].original = next[next_count - 1].original || original;
  } else {
    next[next_count].original = original;
    next[next_count].x = (i16)x;
    next[next_count].z = (i16)z;
    next[next_count++].altitude = altitude;
  }
  return true;
}

// The six-neighbor scan wraps its negative starting index as an unsigned DWORD before taking the remainder.
bool TrContour::NeedsAdjustment(u32 index, TrContourPoint *point, u32 *raised, u32 *lowered) {
  u8 maximum = current[index].altitude, minimum = maximum;
  if (next_count) {
    u8 altitude = next[next_count - 1].altitude;
    if (altitude > maximum)
      maximum = altitude;
    if (altitude < minimum)
      minimum = altitude;
  }
  for (u32 offset = 0; offset < 6; ++offset) {
    const TrContourPoint *neighbor = &current[(index - 3 + offset) % current_count];
    if (TrContourDifference(point->x, neighbor->x) <= 1 && TrContourDifference(point->z, neighbor->z) <= 1) {
      if (neighbor->altitude > maximum)
        maximum = neighbor->altitude;
      if (neighbor->altitude < minimum)
        minimum = neighbor->altitude;
    }
  }
  u8 lower = maximum > 4 ? maximum - 4 : 0, upper = minimum >= 251 ? 255 : minimum + 4;
  if (point->altitude < lower) {
    *raised += lower - point->altitude;
    point->altitude = lower;
    return true;
  }
  if (point->altitude > upper) {
    *lowered += point->altitude - upper;
    point->altitude = upper;
    return true;
  }
  return false;
}

// Insert a current-contour vertex when a proposed append would otherwise leave a gap in the next contour.
void TrContour::AddConnection(const ScDirtBag *terrain, u32 index, const TrContourPoint &point) {
  if (!next_count)
    return;
  const TrContourPoint *previous = &next[next_count - 1];
  if (TrContourDistance(&point, previous) == 1 || TrContourSame(&point, previous))
    return;
  const TrContourPoint *selected = &current[index];
  if (TrContourDistance(selected, previous) != 1)
    selected = &current[index ? index - 1 : current_count - 1];
  AddSec(terrain, selected->x, selected->z, selected->altitude, selected->original);
}

u8 TrContour::OutOfBounds(const ScDirtBag *terrain, const TrContourPoint &point) {
  return point.x < 0 || point.x >= i16(terrain->VertexCountX()) || point.z < 0 ||
      point.z >= i16(terrain->VertexCountZ());
}

// Propagate the perimeter through eight-direction neighbor steps, preserving all earlier marks on capacity failure.
// Remove a duplicate closing vertex and swap the actual buffers/counts only after the complete contour succeeds.
bool TrContour::GrowAndFix(const ScDirtBag *terrain, u32 *raised, u32 *lowered) {
  next_count = 0;
  *raised = 0;
  *lowered = 0;
  for (u32 index = 0; index < current_count; ++index) {
    const TrContourPoint *center = &current[index];
    u32 previous = TrContourDirection(&current[index ? index - 1 : current_count - 1], center);
    u32 following = TrContourDirection(&current[index == current_count - 1 ? 0 : index + 1], center);
    u32 turn = (following - previous) & 7, steps = turn == 0 ? 6 : turn == 4 ? 2 : turn == 6 ? 4 : 0;
    bool emitted_corner = false;
    if (current_count == 1)
      steps = 8;
    if (turn == 2) {
      AddConnection(terrain, index, *center);
      center = &current[index];
      AddSec(terrain, center->x, center->z, center->altitude, center->original);
    } else {
      for (u32 step = 2; step <= steps; step += turn == 6 ? 1 : 2) {
        TrContourPoint candidate = TrContourOffset(&current[index], step + previous);
        if (next_count && TrContourSame(&candidate, &next[next_count - 1]))
          continue;
        if (!OutOfBounds(terrain, candidate)) {
          terrain->GetVertexAltitudeDirt((u32)candidate.x, (u32)candidate.z, &candidate.altitude);
          if (NeedsAdjustment(index, &candidate, raised, lowered) || emitted_corner) {
            if (step == 3) {
              TrContourPoint bridge = TrContourOffset(&current[index], previous + 2);
              terrain->GetVertexAltitudeDirt((u32)bridge.x, (u32)bridge.z, &bridge.altitude);
              AddConnection(terrain, index, bridge);
              AddSec(terrain, bridge.x, bridge.z, bridge.altitude, bridge.original);
              AddSec(terrain, candidate.x, candidate.z, candidate.altitude, candidate.original);
              emitted_corner = true;
            } else {
              AddConnection(terrain, index, candidate);
              AddSec(terrain, candidate.x, candidate.z, candidate.altitude, candidate.original);
            }
            continue;
          }
        }
        AddConnection(terrain, index, current[index]);
        center = &current[index];
        AddSec(terrain, center->x, center->z, center->altitude, center->original);
      }
    }
    if (next_count > 1020)
      return false;
  }
  AddConnection(terrain, 0, *next);
  if (next_count > 1 && TrContourSame(next, &next[next_count - 1]))
    --next_count;
  Flip();
  return true;
}

// Compute affected tile bounds from the live packed contour, preserving output aliasing and store order.
void TrContour::Bounds(u32 *left, u32 *top, u32 *right, u32 *bottom) {
  if (!current_count) {
    *bottom = 0;
    *top = 0;
    *right = 0;
    *left = 0;
    return;
  }
  *left = 256;
  *right = 0;
  *top = 256;
  *bottom = 0;
  for (u32 i = 0; i < current_count; ++i) {
    if (u32(i32(current[i].x)) < *left)
      *left = u32(i32(current[i].x));
    if (u32(i32(current[i].x)) > *right)
      *right = u32(i32(current[i].x));
    if (u32(i32(current[i].z)) < *top)
      *top = u32(i32(current[i].z));
    if (u32(i32(current[i].z)) > *bottom)
      *bottom = u32(i32(current[i].z));
  }
  if (*left)
    --*left;
  if (*top)
    --*top;
  *right = UintMin(*right, TrGetCity()->CellCountX() - 1);
  *bottom = UintMin(*bottom, TrGetCity()->CellCountZ() - 1);
}


// Sum one cell's four original heights in source NW,SW,SE,NE call order.
// Road and highway endpoint averaging retain their distinct four- and eight-sample rounding.
static u32 TrRoadSum(const ScDirtBag *terrain, i32 x, i32 z) {
  u8 height;
  terrain->GetVertexAltitudeDirt(x, z, &height);
  u32 sum = height;
  terrain->GetVertexAltitudeDirt(u32(x), u32(z + 1), &height);
  sum += height;
  terrain->GetVertexAltitudeDirt(u32(x + 1), u32(z + 1), &height);
  sum += height;
  terrain->GetVertexAltitudeDirt(u32(x + 1), u32(z), &height);
  return sum + height;
}

static u8 TrRoadAverage(const ScDirtBag *terrain, i32 x, i32 z) {
  return u8(TrRoadSum(terrain, x, z) / 4);
}

// Reduce a two-unit road grade by splitting excess distance between its two averaged edges.
// The first edge takes the rounded-down half; the second absorbs the remaining unit when the excess is odd.
static void TrBalanceRoadEdges(u8 *first, u8 *second) {
  i32 distance = i32(*first) - *second;
  if (distance < 0)
    distance = -distance;
  if (distance <= 2)
    return;
  i32 first_step = (distance - 2) / 2, second_step = distance - first_step - 2;
  if (*first >= *second) {
    *first = u8(*first - first_step);
    *second = u8(*second + second_step);
  } else {
    *first = u8(*first + first_step);
    *second = u8(*second - second_step);
  }
}

// Reread the planned vertex before marking a difference, then append its original contour point if capacity remains.
// Source selection seeding bypasses the adjacency checks of the separate AddSec method and retains duplicate points.
void TrContour::AppendRoadPoint(const ScDirtBag *terrain, u32 x, u32 z, u8 altitude) {
  u8 height;
  terrain->GetVertexAltitudeDirt(x, z, &height);
  if (height != altitude)
    terrain->MarkVertexChanged(x, z);
  if (current_count < 1024) {
    TrContourPoint *point = &current[current_count];
    point->original = 1;
    point->x = i16(x);
    point->z = i16(z);
    point->altitude = altitude;
    ++current_count;
  }
}

// Seed the original single-cell road mode without adding its four original points to the propagated-volume outputs.
// Retain a supported edge grade, or flatten to an adjacent repeated height before falling back to the four-way average.
void TrContour::SeedRoadCell(const ScDirtBag *terrain, i32 x, i32 z) {
  u8 north_west, north_east, south_east, south_west;
  terrain->GetVertexAltitudeDirt(u32(x), u32(z), &north_west);
  terrain->GetVertexAltitudeDirt(u32(x + 1), u32(z), &north_east);
  terrain->GetVertexAltitudeDirt(u32(x + 1), u32(z + 1), &south_east);
  terrain->GetVertexAltitudeDirt(u32(x), u32(z + 1), &south_west);
  if ((north_west == north_east || south_east == south_west) && north_east != south_east && south_west != north_west) {
    u8 north = u8((u32(north_west) + north_east) / 2), south = u8((u32(south_east) + south_west) / 2);
    TrBalanceRoadEdges(&north, &south);
    AppendRoadPoint(terrain, x, z, north);
    AppendRoadPoint(terrain, x + 1, z, north);
    AppendRoadPoint(terrain, x + 1, z + 1, south);
    AppendRoadPoint(terrain, x, z + 1, south);
  } else if ((north_east == south_east || south_west == north_west) && north_west != north_east &&
      south_east != south_west) {
    u8 east = u8((u32(north_east) + south_east) / 2), west = u8((u32(north_west) + south_west) / 2);
    TrBalanceRoadEdges(&east, &west);
    AppendRoadPoint(terrain, x + 1, z, east);
    AppendRoadPoint(terrain, x + 1, z + 1, east);
    AppendRoadPoint(terrain, x, z + 1, west);
    AppendRoadPoint(terrain, x, z, west);
  } else {
    u8 altitude;
    if (north_west == north_east || north_west == south_west)
      altitude = north_west;
    else if (south_east == north_east || south_east == south_west)
      altitude = south_east;
    else
      altitude = u8((u32(north_west) + north_east + south_east + south_west) / 4);
    AppendRoadPoint(terrain, x, z, altitude);
    AppendRoadPoint(terrain, x + 1, z, altitude);
    AppendRoadPoint(terrain, x + 1, z + 1, altitude);
    AppendRoadPoint(terrain, x, z + 1, altitude);
  }
}

u8 TrContour::CreateRoadOutline(const ScDirtBag *terrain, SpSelection *selection, u32 *raised, u32 *lowered) {
  Start();
  CtCoord *points = new CtCoord[511];
  for (u32 index = 0; index < 511; ++index)
    points[index] = {INT32_MAX, INT32_MAX, INT32_MAX};
  selection->AddRef();
  u32 count = 0;
  u8 result = selection->Reset();
  if (result)
    result = selection->Next(511, points, &count);
  selection->Release();
  *raised = 0;
  *lowered = 0;
  if (result && count == 1)
    SeedRoadCell(terrain, points[0].x >> kCtMicroCoordinateShift, points[0].z >> kCtMicroCoordinateShift);
  else if (result && count > 1 && count < 511) {
    i32 first = 0, last = i32(count) - 1;
    i32 start_x = points[0].x >> kCtMicroCoordinateShift, start_z = points[0].z >> kCtMicroCoordinateShift,
        end_x = points[last].x >> kCtMicroCoordinateShift, end_z = points[last].z >> kCtMicroCoordinateShift;
    u8 start_height = u8(u32(points[0].altitude) >> kCtMicroCoordinateShift),
       end_height = u8(u32(points[last].altitude) >> kCtMicroCoordinateShift);
    if (start_height) {
      first = 1;
      start_x = points[1].x >> kCtMicroCoordinateShift;
      start_z = points[1].z >> kCtMicroCoordinateShift;
    } else
      start_height = TrRoadAverage(terrain, start_x, start_z);
    if (end_height) {
      --last;
      end_x = points[last].x >> kCtMicroCoordinateShift;
      end_z = points[last].z >> kCtMicroCoordinateShift;
    } else if (first == last)
      end_height = start_height;
    else
      end_height = TrRoadAverage(terrain, end_x, end_z);
    i32 x, z, step_x, step_z, neighbor_x, neighbor_z;
    bool reverse;
    if ((u32(points[0].z) ^ u32(points[count - 1].z)) & kCtMicroCellMask) {
      if ((u32(points[0].x) ^ u32(points[count - 1].x)) & kCtMicroCellMask) {
        delete[] points;
        return 0;
      }
      step_x = 0;
      step_z = 1;
      neighbor_x = -1;
      neighbor_z = 0;
      reverse = u32(points[count - 1].z >> kCtMicroCoordinateShift) < u32(points[0].z >> kCtMicroCoordinateShift);
      x = start_x + 1;
      z = reverse ? end_z : start_z;
    } else {
      step_x = 1;
      step_z = 0;
      neighbor_x = 0;
      neighbor_z = 1;
      reverse = u32(points[count - 1].x >> kCtMicroCoordinateShift) < u32(points[0].x >> kCtMicroCoordinateShift);
      x = reverse ? end_x : start_x;
      z = start_z;
    }
    if (reverse) {
      u8 swap = start_height;
      start_height = end_height;
      end_height = swap;
    }
    u8 previous = start_height;
    for (i32 index = first; index <= last + 1; ++index) {
      i32 remaining = last + 1 - index, low = i32(end_height) - 2 * remaining, high = i32(end_height) + 2 * remaining;
      if (low < 0)
        low = 0;
      if (high > 255)
        high = 255;
      u8 previous_low = previous <= 2 ? 0 : u8(previous - 2), previous_high = previous >= 253 ? 255 : u8(previous + 2);
      if (index == first)
        previous_low = previous_high = start_height;
      if (previous_low > low)
        low = previous_low;
      if (previous_high < high)
        high = previous_high;
      if (low > high) {
        delete[] points;
        return 0;
      }
      u8 height, altitude;
      terrain->GetVertexAltitudeDirt(x, z, &height);
      altitude = height < low ? u8(low) : height > high ? u8(high) : height;
      if (height < altitude)
        *raised += altitude - height;
      if (height > altitude)
        *lowered += height - altitude;
      AppendRoadPoint(terrain, x, z, altitude);
      previous = altitude;
      x += step_x;
      z += step_z;
    }
    for (i32 index = i32(current_count) - 1; index >= 0; --index) {
      i32 adjacent_x = i32(current[index].x) + neighbor_x, adjacent_z = i32(current[index].z) + neighbor_z;
      u8 height;
      terrain->GetVertexAltitudeDirt(u32(adjacent_x), u32(adjacent_z), &height);
      u8 altitude = current[index].altitude;
      if (height < altitude)
        *raised += altitude - height;
      if (height > altitude)
        *lowered += height - altitude;
      AppendRoadPoint(terrain, adjacent_x, adjacent_z, altitude);
    }
  }
  delete[] points;
  return result;
}

u8 TrContour::CreateHighwayOutline(const ScDirtBag *terrain, SpSelection *selection, u32 *raised, u32 *lowered) {
  Start();
  CtCoord *points = new CtCoord[1024];
  for (u32 index = 0; index < 1024; ++index)
    points[index] = {INT32_MAX, INT32_MAX, INT32_MAX};
  selection->AddRef();
  u32 count = 0;
  u8 result = selection->Reset();
  if (result)
    result = selection->Next(1024, points, &count);
  selection->Release();
  *raised = 0;
  *lowered = 0;
  if (!result || count >= 1024) {
    delete[] points;
    return result;
  }
  assert(count >= 2);
  u32 first = 0, last = count - 1;
  i32 start_x = points[0].x >> kCtMicroCoordinateShift, start_z = points[0].z >> kCtMicroCoordinateShift;
  i32 end_x = points[last].x >> kCtMicroCoordinateShift, end_z = points[last].z >> kCtMicroCoordinateShift;
  u8 start_height = u8(u32(points[0].altitude) >> kCtMicroCoordinateShift),
     end_height = u8(u32(points[last].altitude) >> kCtMicroCoordinateShift);
  if (count > 2 && start_height) {
    first = 2;
    start_x = points[2].x >> kCtMicroCoordinateShift;
    start_z = points[2].z >> kCtMicroCoordinateShift;
  } else if (!start_height) {
    u32 sum = TrRoadSum(terrain, start_x, start_z);
    sum += TrRoadSum(terrain, points[1].x >> kCtMicroCoordinateShift, points[1].z >> kCtMicroCoordinateShift);
    start_height = u8(sum / 8);
  }
  if (count > 2 || !u8(u32(points[0].altitude) >> kCtMicroCoordinateShift)) {
    if (end_height) {
      last -= 2;
      end_x = points[last].x >> kCtMicroCoordinateShift;
      end_z = points[last].z >> kCtMicroCoordinateShift;
    } else if (first == last)
      end_height = start_height;
    else {
      u32 sum = TrRoadSum(terrain, end_x, end_z);
      sum += TrRoadSum(
          terrain, points[count - 2].x >> kCtMicroCoordinateShift, points[count - 2].z >> kCtMicroCoordinateShift);
      end_height = u8(sum / 8);
    }
  }
  i32 x, z, step_x, step_z, side_x, side_z;
  bool reverse;
  i64 difference_z = i64(points[0].z >> kCtMicroCoordinateShift) - (points[count - 1].z >> kCtMicroCoordinateShift);
  i64 difference_x = i64(points[0].x >> kCtMicroCoordinateShift) - (points[count - 1].x >> kCtMicroCoordinateShift);
  if (difference_z == 1 || difference_z == -1) {
    z = start_z < end_z ? start_z : end_z;
    step_x = 1;
    step_z = 0;
    side_x = 0;
    side_z = 2;
    reverse = u32(points[count - 1].x >> kCtMicroCoordinateShift) < u32(points[0].x >> kCtMicroCoordinateShift);
    x = reverse ? end_x : start_x;
  } else if (difference_x == 1 || difference_x == -1) {
    x = (u32(start_x) <= u32(end_x) ? end_x : start_x) + 1;
    step_x = 0;
    step_z = 1;
    side_x = -2;
    side_z = 0;
    reverse = u32(points[count - 1].z >> kCtMicroCoordinateShift) < u32(points[0].z >> kCtMicroCoordinateShift);
    z = reverse ? end_z : start_z;
  } else {
    delete[] points;
    return result;
  }
  if (reverse) {
    u8 swap = start_height;
    start_height = end_height;
    end_height = swap;
  }
  i32 first_edge = i32(first >> 1), last_edge = i32(last >> 1) + 1;
  u8 previous = 0, previous_dry = 0, cached = 0;
  for (i32 index = first_edge; index <= last_edge; ++index) {
    i32 low = i32(end_height) - 2 * (last_edge - index), high = i32(end_height) + 2 * (last_edge - index);
    if (low < 0)
      low = 0;
    if (high >= 255)
      high = 255;
    u8 previous_low = previous <= 2 ? 0 : u8(previous - 2), previous_high = previous >= 253 ? 255 : u8(previous + 2);
    if (index == first_edge)
      previous_low = previous_high = start_height;
    if (previous_low > low)
      low = previous_low;
    if (previous_high < high)
      high = previous_high;
    if (low > high) {
      delete[] points;
      return result;
    }
    u8 dry;
    terrain->GetVertexAltitudeDirt(u32(x), u32(z), &dry);
    if (dry != previous_dry || cached < low || cached > high)
      cached = 0;
    previous_dry = dry;
    u8 altitude = dry;
    if (cached && cached != dry) {
      if (cached < dry)
        *lowered += dry - cached;
      else
        *raised += cached - dry;
      altitude = cached;
    }
    if (altitude < low) {
      *raised += u32(low - altitude);
      altitude = u8(low);
    } else if (altitude > high) {
      *lowered += u32(altitude - high);
      altitude = u8(high);
    }
    u8 middle;
    terrain->GetVertexAltitudeDirt(u32(x + side_x / 2), u32(z + side_z / 2), &middle);
    if (middle < altitude)
      *raised += altitude - middle;
    else if (middle > altitude)
      *lowered += middle - altitude;
    AppendRoadPoint(terrain, x, z, altitude);
    cached = altitude;
    previous = altitude;
    x += step_x;
    z += step_z;
  }
  i32 half_x = side_x / 2, half_z = side_z / 2;
  const TrContourPoint end = current[current_count - 1];
  AppendRoadPoint(terrain, i32(end.x) + half_x, i32(end.z) + half_z, end_height);
  for (i32 index = i32(current_count) - 2; index >= 0; --index) {
    i32 opposite_x = side_x + current[index].x, opposite_z = side_z + current[index].z;
    u8 height;
    terrain->GetVertexAltitudeDirt(u32(opposite_x), u32(opposite_z), &height);
    u8 altitude = current[index].altitude;
    if (height < altitude)
      *raised += altitude - height;
    else if (height > altitude)
      *lowered += height - altitude;
    AppendRoadPoint(terrain, opposite_x, opposite_z, altitude);
    terrain->GetVertexAltitudeDirt(u32(half_x + current[index].x), u32(half_z + current[index].z), &height);
    if (height != current[index].altitude)
      terrain->MarkVertexChanged(u32(half_x + current[index].x), u32(half_z + current[index].z));
  }
  AppendRoadPoint(terrain, half_x + current[0].x, half_z + current[0].z, start_height);
  delete[] points;
  return result;
}

void TrContour::ReflectPrimary(ScDirtBag *terrain) {
  for (u32 index = 0; index < current_count; ++index) {
    const TrContourPoint point = current[index];
    terrain->SetVertexAltitude(u32(i32(point.x)), u32(i32(point.z)), point.altitude);
  }
}

void TrContour::Reflect(ScDirtBag *terrain) {
  for (u32 index = 0; index < current_count; ++index) {
    const TrContourPoint point = current[index];
    if (!point.original)
      terrain->SetVertexAltitude(u32(i32(point.x)), u32(i32(point.z)), point.altitude);
  }
}

void TrContour::ReflectPrimaryHighway(ScDirtBag *terrain) {
  ReflectPrimary(terrain);
  for (u32 index = 0; index < (current_count >> 1) - 1; ++index) {
    const TrContourPoint first = current[index], opposite = current[current_count - index - 2];
    terrain->SetVertexAltitude(u32((i32(first.x) + opposite.x) / 2),
        u32((i32(first.z) + opposite.z) / 2), first.altitude);
  }
}
