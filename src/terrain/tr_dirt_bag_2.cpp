#include "terrain/tr_dirt_bag.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "economy/api.h"
#include "geometry/selection.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "terrain/api.h"
#include "terrain/tr_contour.h"
#include "terrain/tr_director.h"
#include "terrain/tr_tile.h"
#include "ui/api.h"

// Trace a checked level perimeter, price each propagated volume, publish affected bounds, then mark and price its interior.
// Missing budget and rejected seeds preserve the original partial output/change effects; final policy is a virtual filter call.
bool TrDirtBag::CanLevelTerrain(const GeBounds &bounds, u8 altitude, GeBounds *affected, u32 *cost) const {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  *cost = 0;
  ResetChanges();
  TrPlacementContour.Start();
  for (u32 x = TrPlacementTile(bounds.minimum.x); x < TrPlacementTile(bounds.maximum.x); ++x)
    if (!TrPlacementContour.AddVertex(this, x, TrPlacementTile(bounds.minimum.z), altitude))
      return false;
  for (u32 z = TrPlacementTile(bounds.minimum.z); z < TrPlacementTile(bounds.maximum.z); ++z)
    if (!TrPlacementContour.AddVertex(this, TrPlacementTile(bounds.maximum.x), z, altitude))
      return false;
  for (u32 x = TrPlacementTile(bounds.maximum.x); x > TrPlacementTile(bounds.minimum.x); --x)
    if (!TrPlacementContour.AddVertex(this, x, TrPlacementTile(bounds.maximum.z), altitude))
      return false;
  for (u32 z = TrPlacementTile(bounds.maximum.z); z > TrPlacementTile(bounds.minimum.z); --z)
    if (!TrPlacementContour.AddVertex(this, TrPlacementTile(bounds.minimum.x), z, altitude))
      return false;
  u32 raised, lowered;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    u32 volume = raised * u32(budget->GetCost(kEcCostRaiseTerrain));
    u32 price = u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + lowered * price) >> 2;
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  raised = 0;
  for (i32 x = i32(bounds.minimum.x) >> kCtMicroCoordinateShift;
      x <= (i32(bounds.maximum.x) >> kCtMicroCoordinateShift); ++x) {
    for (i32 z = i32(bounds.minimum.z) >> kCtMicroCoordinateShift;
        z <= (i32(bounds.maximum.z) >> kCtMicroCoordinateShift); ++z) {
      u8 height;
      GetVertexAltitudeDirt(u32(x), u32(z), &height);
      if (height != altitude) {
        MarkVertexChanged(u32(x), u32(z));
        raised += u32(height > altitude ? height - altitude : altitude - height);
      }
    }
  }
  *cost += (raised * u32(budget->GetCost(kEcCostLevelTerrain))) >> 2;
  return FunctionFilterTestPassed(*affected, *cost, 4);
}

// Average the four ordered perimeter walks with half-up rounding and publish altitude before virtual leveling validation.
// Missing budget or an empty perimeter leaves every caller output untouched.
bool TrDirtBag::GetOptimalLevel(const GeBounds &bounds, u8 *altitude, GeBounds *affected, u32 *cost) const {
  if (!city->BudgetLayer())
    return false;
  u32 sum = 0, count = 0;
  u8 height;
  for (u32 x = TrPlacementTile(bounds.minimum.x); x < TrPlacementTile(bounds.maximum.x); ++x) {
    GetVertexAltitudeDirt(x, TrPlacementTile(bounds.minimum.z), &height);
    ++count;
    sum += height;
  }
  for (u32 z = TrPlacementTile(bounds.minimum.z); z < TrPlacementTile(bounds.maximum.z); ++z) {
    GetVertexAltitudeDirt(TrPlacementTile(bounds.maximum.x), z, &height);
    ++count;
    sum += height;
  }
  for (u32 x = TrPlacementTile(bounds.maximum.x); x > TrPlacementTile(bounds.minimum.x); --x) {
    GetVertexAltitudeDirt(x, TrPlacementTile(bounds.maximum.z), &height);
    ++count;
    sum += height;
  }
  for (u32 z = TrPlacementTile(bounds.maximum.z); z > TrPlacementTile(bounds.minimum.z); --z) {
    GetVertexAltitudeDirt(TrPlacementTile(bounds.minimum.x), z, &height);
    ++count;
    sum += height;
  }
  if (!count)
    return false;
  u32 level = (sum + (count >> 1)) / count;
  *altitude = u8(level);
  return CanLevelTerrain(bounds, u8(level), affected, cost);
}

// Read a live perimeter height and seed its one-step saturating target through the original checked contour path.
static bool TrPlacementSeedShift(const TrDirtBag *terrain, u32 x, u32 z, bool raising) {
  u8 height;
  terrain->GetVertexAltitudeDirt(x, z, &height);
  if (raising) {
    if (height != 255)
      ++height;
  } else if (height)
    --height;
  return TrPlacementContour.AddVertex(terrain, x, z, height);
}

// Share only the matching raise/lower validation phases while retaining their separate original public contracts.
// Contour propagation uses live prices; interior pricing counts changed vertices rather than leveling volume.
bool TrDirtBag::CanShiftTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool raising) const {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  *cost = 0;
  ResetChanges();
  TrPlacementContour.Start();
  for (u32 x = TrPlacementTile(bounds.minimum.x); x < TrPlacementTile(bounds.maximum.x); ++x)
    if (!TrPlacementSeedShift(this, x, TrPlacementTile(bounds.minimum.z), raising))
      return false;
  for (u32 z = TrPlacementTile(bounds.minimum.z); z < TrPlacementTile(bounds.maximum.z); ++z)
    if (!TrPlacementSeedShift(this, TrPlacementTile(bounds.maximum.x), z, raising))
      return false;
  for (u32 x = TrPlacementTile(bounds.maximum.x); x > TrPlacementTile(bounds.minimum.x); --x)
    if (!TrPlacementSeedShift(this, x, TrPlacementTile(bounds.maximum.z), raising))
      return false;
  for (u32 z = TrPlacementTile(bounds.maximum.z); z > TrPlacementTile(bounds.minimum.z); --z)
    if (!TrPlacementSeedShift(this, TrPlacementTile(bounds.minimum.x), z, raising))
      return false;
  u32 raised, lowered;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    u32 volume = raised * u32(budget->GetCost(kEcCostRaiseTerrain));
    u32 price = u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + lowered * price) >> 2;
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  u32 count = 0;
  for (i32 x = i32(bounds.minimum.x) >> kCtMicroCoordinateShift;
      x <= (i32(bounds.maximum.x) >> kCtMicroCoordinateShift); ++x) {
    for (i32 z = i32(bounds.minimum.z) >> kCtMicroCoordinateShift;
        z <= (i32(bounds.maximum.z) >> kCtMicroCoordinateShift); ++z) {
      u8 height;
      GetVertexAltitudeDirt(u32(x), u32(z), &height);
      if (height != (raising ? 255 : 0)) {
        MarkVertexChanged(u32(x), u32(z));
        ++count;
      }
    }
  }
  *cost += (count * u32(budget->GetCost(raising ? 9 : 10))) >> 2;
  return FunctionFilterTestPassed(*affected, *cost, raising ? 1 : 2);
}

bool TrDirtBag::CanRaiseTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost) const {
  return CanShiftTerrain(bounds, affected, cost, true);
}

bool TrDirtBag::CanLowerTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost) const {
  return CanShiftTerrain(bounds, affected, cost, false);
}

// Direction zero levels to altitude; positive and negative directions preserve the distinct saturating brush behavior.
bool TrDirtBag::ApplyTerrain(
    const GeBounds &bounds, u8 altitude, GeBounds *affected, u32 *cost, bool free_of_charge, i32 direction) {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  *cost = 0;
  ResetChanges();
  SetEnabled(0);
  TrPlacementContour.Start();
  for (u32 x = TrPlacementTile(bounds.minimum.x); x < TrPlacementTile(bounds.maximum.x); ++x)
    SeedAppliedTerrain(x, TrPlacementTile(bounds.minimum.z), altitude, direction);
  for (u32 z = TrPlacementTile(bounds.minimum.z); z < TrPlacementTile(bounds.maximum.z); ++z)
    SeedAppliedTerrain(TrPlacementTile(bounds.maximum.x), z, altitude, direction);
  for (u32 x = TrPlacementTile(bounds.maximum.x); x > TrPlacementTile(bounds.minimum.x); --x)
    SeedAppliedTerrain(x, TrPlacementTile(bounds.maximum.z), altitude, direction);
  for (u32 z = TrPlacementTile(bounds.maximum.z); z > TrPlacementTile(bounds.minimum.z); --z)
    SeedAppliedTerrain(TrPlacementTile(bounds.minimum.x), z, altitude, direction);
  u32 raised, lowered;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    u32 volume = raised * u32(budget->GetCost(kEcCostRaiseTerrain));
    u32 price = u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + lowered * price) >> 2;
    TrPlacementContour.Reflect(this);
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  u32 volume = 0;
  for (i32 x = i32(bounds.minimum.x) >> kCtMicroCoordinateShift;
      x <= (i32(bounds.maximum.x) >> kCtMicroCoordinateShift); ++x) {
    for (i32 z = i32(bounds.minimum.z) >> kCtMicroCoordinateShift;
        z <= (i32(bounds.maximum.z) >> kCtMicroCoordinateShift); ++z) {
      u8 height;
      GetVertexAltitudeDirt(u32(x), u32(z), &height);
      if (direction ? height != (direction > 0 ? 255 : 0) : height != altitude) {
        u8 target = direction ? u8(i32(height) + direction) : altitude;
        SetVertexAltitude(u32(x), u32(z), target);
        volume += direction ? 1 : u32(height > altitude ? height - altitude : altitude - height);
      }
    }
  }
  *cost += (volume * u32(budget->GetCost(direction ? (direction > 0 ? 9 : 10) : 8))) >> 2;
  left = TrPlacementTile(affected->minimum.x);
  top = TrPlacementTile(affected->minimum.z);
  right = TrPlacementTile(affected->maximum.x);
  bottom = TrPlacementTile(affected->maximum.z);
  u8 sea_level = GetGlobalSeaLevel();
  SetWaterTable(left + 1, top + 1, right, bottom, sea_level);
  left = TrPlacementTile(affected->minimum.x);
  top = TrPlacementTile(affected->minimum.z);
  right = TrPlacementTile(affected->maximum.x) + 3;
  bottom = TrPlacementTile(affected->maximum.z) + 3;
  left = left > 3 ? left - 3 : 0;
  top = top > 3 ? top - 3 : 0;
  if (right >= VertexCountX() - 1)
    right = VertexCountX() - 1;
  if (bottom >= VertexCountZ() - 1)
    bottom = VertexCountZ() - 1;
  CalculateAndSetVertexLight(left, top, right, bottom);
  SetEnabled(1);
  PublishChanges(left, top, right - 1, bottom - 1, 1);
  if (!free_of_charge)
    budget->WithdrawFunds(*cost);
  return true;
}

// Seed the actual contour without checked-neighbor validation, reading a live saturating height for brush operations.
void TrDirtBag::SeedAppliedTerrain(u32 x, u32 z, u8 altitude, i32 direction) {
  if (direction) {
    GetVertexAltitudeDirt(x, z, &altitude);
    if (altitude != (direction > 0 ? 255 : 0))
      altitude = u8(i32(altitude) + direction);
  }
  TrPlacementContour.AddPri(this, x, z, altitude);
}

bool TrDirtBag::RaiseTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool free_of_charge) {
  return ApplyTerrain(bounds, 0, affected, cost, free_of_charge, 1);
}

bool TrDirtBag::LowerTerrain(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool free_of_charge) {
  return ApplyTerrain(bounds, 0, affected, cost, free_of_charge, -1);
}

bool TrDirtBag::LevelTerrain(const GeBounds &bounds, u8 altitude, GeBounds *affected, u32 *cost, bool free_of_charge) {
  return ApplyTerrain(bounds, altitude, affected, cost, free_of_charge, 0);
}

// Mark every selected dry vertex, copy the complete bounds, then price water coverage and ask the actual policy filter.
// Existing change bits are retained and a missing budget leaves both genuine outputs untouched.
bool TrDirtBag::CanPlaceWater(const GeBounds &bounds, GeBounds *affected, u32 *cost) const {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  u32 left = TrPlacementTile(bounds.minimum.x), top = TrPlacementTile(bounds.minimum.z),
      right = TrPlacementTile(bounds.maximum.x), bottom = TrPlacementTile(bounds.maximum.z), count = 0;
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      u8 dry, wet;
      GetVertexAltitudeDirt(x, z, &dry);
      GetVertexAltitudeWater(x, z, &wet);
      if (dry > wet) {
        MarkVertexChanged(x, z);
        ++count;
      }
    }
  }
  *affected = bounds;
  *cost = (count * u32(budget->GetCost(kEcCostDecorativeWater))) >> 2;
  return FunctionFilterTestPassed(*affected, *cost, 32);
}

// Raise each selected dry water vertex to the live ground height, preserving existing change bits and output ordering.
// Recompute the expanded lighting margin before enabling notifications and performing the optional final charge.
bool TrDirtBag::PlaceWater(const GeBounds &bounds, GeBounds *affected, u32 *cost, bool free_of_charge) {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  u32 left = TrPlacementTile(bounds.minimum.x), top = TrPlacementTile(bounds.minimum.z),
      right = TrPlacementTile(bounds.maximum.x), bottom = TrPlacementTile(bounds.maximum.z), count = 0;
  SetEnabled(0);
  for (u32 x = left; x <= right; ++x) {
    for (u32 z = top; z <= bottom; ++z) {
      u8 dry, wet;
      GetVertexAltitudeDirt(x, z, &dry);
      GetVertexAltitudeWater(x, z, &wet);
      if (dry > wet) {
        ++count;
        SetWaterTable(x, z, x, z, dry);
      }
    }
  }
  *affected = bounds;
  *cost = (count * u32(budget->GetCost(kEcCostDecorativeWater))) >> 2;
  left = left > 3 ? left - 3 : 0;
  top = top > 3 ? top - 3 : 0;
  right += 3;
  bottom += 3;
  if (right >= VertexCountX() - 1)
    right = VertexCountX() - 1;
  if (bottom >= VertexCountZ() - 1)
    bottom = VertexCountZ() - 1;
  CalculateAndSetVertexLight(left, top, right, bottom);
  SetEnabled(1);
  PublishChanges(left, top, right - 1, bottom - 1, 1);
  if (!free_of_charge)
    budget->WithdrawFunds(*cost);
  return true;
}

// Intersect one surrounding-height constraint with the current smoothing interval, saturating at byte limits.
// This native arithmetic phase keeps the eight original samples in their caller's observable order.
static void TrConstrainZoneHeight(u8 height, u8 *minimum, u8 *maximum) {
  u8 low = height < 4 ? 0 : u8(height - 4), high = height > 251 ? 255 : u8(height + 4);
  if (low > *minimum)
    *minimum = low;
  if (high < *maximum)
    *maximum = high;
}

// Validation marks changes without simulating writes; application reads its actual preceding mutations on later visits.
void TrDirtBag::ShapeZone(
    u32 left, u32 top, u32 right, u32 bottom, bool smooth, bool apply, u32 *raised, u32 *lowered) const {
  u8 minimum = 255;
  for (u32 x = left + 1; x <= right - 1; ++x) {
    for (u32 z = top + 1; z <= bottom - 1; ++z) {
      u8 height;
      GetVertexAltitudeDirt(x, z, &height);
      if (height < minimum)
        minimum = height;
    }
  }
  TrDirtBag *mutable_terrain = const_cast<TrDirtBag *>(this);
  for (u32 x = left + 1; x <= right - 1; ++x) {
    for (u32 z = top + 1; z <= bottom - 1; ++z) {
      u8 height, adjacent;
      GetVertexAltitudeDirt(x, z, &height);
      GetVertexAltitudeWater(x, z, &adjacent);
      if (adjacent >= height)
        continue;
      GetVertexAltitudeDirt(x, z + 1, &adjacent);
      if (adjacent == height)
        continue;
      GetVertexAltitudeDirt(x + 1, z, &adjacent);
      if (adjacent == height)
        continue;
      GetVertexAltitudeDirt(x, z - 1, &adjacent);
      if (adjacent == height)
        continue;
      GetVertexAltitudeDirt(x - 1, z, &adjacent);
      if (adjacent == height)
        continue;
      u32 difference = (height - minimum) & 0xfe;
      *lowered += difference;
      if (difference) {
        if (apply)
          mutable_terrain->SetVertexAltitude(x, z, u8(minimum + difference));
        else
          MarkVertexChanged(x, z);
      }
    }
  }
  if (!smooth || right - left <= 3 || bottom - top <= 3)
    return;
  for (u32 x = left + 1; x < right - 1; ++x) {
    for (u32 z = top + 1; z < bottom - 1; ++z) {
      u8 heights[4];
      GetVertexAltitudeDirt(x, z, &heights[0]);
      GetVertexAltitudeDirt(x, z + 1, &heights[1]);
      GetVertexAltitudeDirt(x + 1, z, &heights[2]);
      GetVertexAltitudeDirt(x + 1, z + 1, &heights[3]);
      u8 first = heights[0], second = heights[1], third = heights[2], fourth = heights[3], target;
      if ((first == third && second == fourth) || (first == second && third == fourth))
        continue;
      if (first == third || first == second || first == fourth)
        target = first;
      else if (third == second || third == fourth)
        target = third;
      else if (second == fourth)
        target = second;
      else
        target = u8((u32(first) + second + third + fourth) / 4);
      u8 low = 0, high = 255, sample;
      const u32 surrounding_x[8] = {x, x + 1, x - 1, x + 2, x - 1, x + 2, x, x + 1};
      const u32 surrounding_z[8] = {z - 1, z - 1, z, z, z + 1, z + 1, z + 2, z + 2};
      for (u32 index = 0; index < 8; ++index) {
        GetVertexAltitudeDirt(surrounding_x[index], surrounding_z[index], &sample);
        TrConstrainZoneHeight(sample, &low, &high);
      }
      const u32 corner_x[4] = {x, x, x + 1, x + 1}, corner_z[4] = {z, z + 1, z + 1, z};
      for (u32 index = 0; index < 4; ++index) {
        GetVertexAltitudeWater(corner_x[index], corner_z[index], &sample);
        u8 above_water = u8(sample + 1);
        if (above_water > low)
          low = above_water;
      }
      if (low > high || target < low || target > high)
        continue;
      if (apply)
        for (u32 index = 0; index < 4; ++index)
          mutable_terrain->SetVertexAltitude(corner_x[index], corner_z[index], target);
      else
        MarkCellChanged(x, z);
      for (u32 index = 0; index < 4; ++index) {
        if (heights[index] < target)
          *raised += target - heights[index];
        if (heights[index] > target)
          *lowered += heights[index] - target;
      }
    }
  }
}

// Price the isolated-height and optional four-vertex smoothing phases while marking their affected terrain.
bool TrDirtBag::CanSetupZone(const GeBounds &bounds, u32 *cost, bool smooth) const {
  *cost = 0;
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  ResetChanges();
  u32 left = TrPlacementTile(bounds.minimum.x), top = TrPlacementTile(bounds.minimum.z),
      right = TrPlacementTile(bounds.maximum.x), bottom = TrPlacementTile(bounds.maximum.z);
  if (right < left) {
    u32 swap = left;
    left = right;
    right = swap;
  }
  if (bottom < top) {
    u32 swap = top;
    top = bottom;
    bottom = swap;
  }
  if (bottom - top < 2 || right - left < 2)
    return true;
  u32 raised = 0, lowered = 0;
  ShapeZone(left, top, right, bottom, smooth, false, &raised, &lowered);
  u32 price = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
  *cost = (price + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  return FunctionFilterTestPassed(bounds, *cost, 64);
}

// Apply the same zone interior phases to live heights with notifications disabled, then shade the expanded footprint.
// Publish changed cells before calculating the final budget charge; free operations retain their computed cost output.
bool TrDirtBag::SetupZone(const GeBounds &bounds, u32 *cost, bool free_of_charge, bool smooth) {
  *cost = 0;
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  ResetChanges();
  u32 left = TrPlacementTile(bounds.minimum.x), top = TrPlacementTile(bounds.minimum.z),
      right = TrPlacementTile(bounds.maximum.x), bottom = TrPlacementTile(bounds.maximum.z);
  if (right < left) {
    u32 swap = left;
    left = right;
    right = swap;
  }
  if (bottom < top) {
    u32 swap = top;
    top = bottom;
    bottom = swap;
  }
  if (bottom - top < 2 || right - left < 2)
    return true;
  SetEnabled(0);
  u32 raised = 0, lowered = 0;
  ShapeZone(left, top, right, bottom, smooth, true, &raised, &lowered);
  left = left <= 2 ? 0 : left - 2;
  top = top <= 2 ? 0 : top - 2;
  right += 2;
  if (right >= VertexCountX() - 1)
    right = VertexCountX() - 1;
  bottom += 2;
  if (bottom >= VertexCountZ() - 1)
    bottom = VertexCountZ() - 1;
  CalculateAndSetVertexLight(left, top, right, bottom);
  SetEnabled(1);
  PublishChanges(left, top, right - 1, bottom - 1, 1);
  u32 price = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
  *cost = (price + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  if (!free_of_charge)
    budget->WithdrawFunds(*cost);
  return true;
}

// Seed the complete selected road edge and price its initial and propagated height volumes in source callback order.
// A failed seed preserves the reset change plane and leaves cost and affected bounds untouched.
bool TrDirtBag::CanPrepareForRoad(SpSelection *selection, GeBounds *affected, u32 *cost) const {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  ResetChanges();
  u32 raised = 0, lowered = 0;
  if (!TrPlacementContour.CreateRoadOutline(this, selection, &raised, &lowered))
    return false;
  u32 volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
  *cost = (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  return FunctionFilterTestPassed(*affected, *cost, 8);
}

// Apply the selected road seed before tracing nonoriginal contour points and accumulating their terrain prices.
// Fill the interior water rectangle, shade the expanded affected area, publish changes and optionally debit the budget.
bool TrDirtBag::PrepareForRoad(SpSelection *selection, GeBounds *affected, u32 *cost, bool free_of_charge) {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  ResetChanges();
  SetEnabled(0);
  u32 raised = 0, lowered = 0;
  if (!TrPlacementContour.CreateRoadOutline(this, selection, &raised, &lowered))
    return false;
  TrPlacementContour.ReflectPrimary(this);
  u32 volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
  *cost = (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    TrPlacementContour.Reflect(this);
    volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  left = u32(affected->minimum.x >> kCtMicroCoordinateShift);
  top = u32(affected->minimum.z >> kCtMicroCoordinateShift);
  right = u32(affected->maximum.x >> kCtMicroCoordinateShift);
  bottom = u32(affected->maximum.z >> kCtMicroCoordinateShift);
  u8 sea_level = GetGlobalSeaLevel();
  SetWaterTable(left + 1, top + 1, right, bottom, sea_level);
  left = u32(affected->minimum.x >> kCtMicroCoordinateShift);
  top = u32(affected->minimum.z >> kCtMicroCoordinateShift);
  right = u32(affected->maximum.x >> kCtMicroCoordinateShift);
  bottom = u32(affected->maximum.z >> kCtMicroCoordinateShift);
  left = left <= 3 ? 0 : left - 3;
  top = top <= 3 ? 0 : top - 3;
  right += 3;
  if (right >= VertexCountX() - 1)
    right = VertexCountX() - 1;
  bottom += 3;
  if (bottom >= VertexCountZ() - 1)
    bottom = VertexCountZ() - 1;
  CalculateAndSetVertexLight(left, top, right, bottom);
  SetEnabled(1);
  PublishChanges(left, top, right - 1, bottom - 1, 1);
  if (!free_of_charge)
    budget->WithdrawFunds(*cost);
  return true;
}

// Seed the complete paired highway edge and price its initial and propagated height volumes in source callback order.
// A failed seed preserves the reset change plane and leaves cost and affected bounds untouched.
bool TrDirtBag::CanPrepareForHighway(SpSelection *selection, GeBounds *affected, u32 *cost) const {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  ResetChanges();
  u32 raised = 0, lowered = 0;
  if (!TrPlacementContour.CreateHighwayOutline(this, selection, &raised, &lowered))
    return false;
  u32 volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
  *cost = (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  return FunctionFilterTestPassed(*affected, *cost, 16);
}

// Apply the paired highway perimeter and its center line before propagating nonoriginal contour heights.
// Fill the interior water rectangle, shade the expanded affected area, publish changes and optionally debit the budget.
bool TrDirtBag::PrepareForHighway(SpSelection *selection, GeBounds *affected, u32 *cost, bool free_of_charge) {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  ResetChanges();
  SetEnabled(0);
  u32 raised = 0, lowered = 0;
  if (!TrPlacementContour.CreateHighwayOutline(this, selection, &raised, &lowered))
    return false;
  TrPlacementContour.ReflectPrimaryHighway(this);
  u32 volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
  *cost = (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  while (TrPlacementContour.GrowAndFix(this, &raised, &lowered)) {
    if (!(raised + lowered))
      break;
    TrPlacementContour.Reflect(this);
    volume = lowered * u32(budget->GetCost(kEcCostLowerTerrain));
    *cost += (volume + raised * u32(budget->GetCost(kEcCostRaiseTerrain))) >> 2;
  }
  u32 left, top, right, bottom;
  TrPlacementContour.Bounds(&left, &top, &right, &bottom);
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  left = u32(affected->minimum.x >> kCtMicroCoordinateShift);
  top = u32(affected->minimum.z >> kCtMicroCoordinateShift);
  right = u32(affected->maximum.x >> kCtMicroCoordinateShift);
  bottom = u32(affected->maximum.z >> kCtMicroCoordinateShift);
  u8 sea_level = GetGlobalSeaLevel();
  SetWaterTable(left + 1, top + 1, right, bottom, sea_level);
  left = u32(affected->minimum.x >> kCtMicroCoordinateShift);
  top = u32(affected->minimum.z >> kCtMicroCoordinateShift);
  right = u32(affected->maximum.x >> kCtMicroCoordinateShift);
  bottom = u32(affected->maximum.z >> kCtMicroCoordinateShift);
  left = left <= 3 ? 0 : left - 3;
  top = top <= 3 ? 0 : top - 3;
  right += 3;
  if (right >= VertexCountX() - 1)
    right = VertexCountX() - 1;
  bottom += 3;
  if (bottom >= VertexCountZ() - 1)
    bottom = VertexCountZ() - 1;
  CalculateAndSetVertexLight(left, top, right, bottom);
  SetEnabled(1);
  PublishChanges(left, top, right - 1, bottom - 1, 1);
  if (!free_of_charge)
    budget->WithdrawFunds(*cost);
  return true;
}

// Scan the source's64-coordinate batches through the real retained enumerator and preserve repeated vertex visits.
// This native shared phase uses bounded scoped scratch; validation marks dry vertices, while application fills their water.
u32 TrDirtBag::ScanSelectedWater(
    SpSelection *selection, u32 *left, u32 *top, u32 *right, u32 *bottom, bool apply) const {
  CtCoord points[64];
  for (u32 index = 0; index < 64; ++index)
    points[index] = {INT32_MAX, INT32_MAX, INT32_MAX};
  selection->AddRef();
  selection->Reset();
  u32 changed = 0, count, minimum_x = 255, minimum_z = 255, maximum_x = 0, maximum_z = 0;
  while (selection->Next(64, points, &count)) {
    if (!count)
      break;
    for (u32 index = 0; index < count; ++index) {
      u32 cell_x = u32(points[index].x >> kCtMicroCoordinateShift),
          cell_z = u32(points[index].z >> kCtMicroCoordinateShift);
      if (!(cell_x < cell_x + 1))
        continue;
      for (u32 x = cell_x; x <= cell_x + 1; ++x) {
        if (!(cell_z < cell_z + 1))
          continue;
        for (u32 z = cell_z; z <= cell_z + 1; ++z) {
          u8 dry, wet;
          GetVertexAltitudeDirt(x, z, &dry);
          GetVertexAltitudeWater(x, z, &wet);
          if (dry > wet) {
            ++changed;
            if (apply)
              const_cast<TrDirtBag *>(this)->SetWaterTable(x, z, x, z, dry);
            else
              MarkVertexChanged(x, z);
            if (x < minimum_x)
              minimum_x = x;
            if (x > maximum_x)
              maximum_x = x;
            if (z < minimum_z)
              minimum_z = z;
            if (z > maximum_z)
              maximum_z = z;
          }
        }
      }
    }
  }
  selection->Release();
  if (maximum_x < minimum_x)
    minimum_x = maximum_x;
  if (maximum_z < minimum_z)
    minimum_z = maximum_z;
  *left = minimum_x;
  *top = minimum_z;
  *right = maximum_x;
  *bottom = maximum_z;
  return changed;
}

// Mark all selected dry corner visits and price their count without resetting prior change bits.
// Publish the changed bounds before pricing and apply the genuine water-policy filter to that completed result.
bool TrDirtBag::CanPlaceWater(SpSelection *selection, GeBounds *affected, u32 *cost) const {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  u32 left, top, right, bottom;
  u32 changed = ScanSelectedWater(selection, &left, &top, &right, &bottom, false);
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  *cost = (changed * u32(budget->GetCost(kEcCostDecorativeWater))) >> 2;
  return FunctionFilterTestPassed(*affected, *cost, 32);
}

// Fill selected dry vertices before disabling updates, retaining the real rectangle setter's immediate effects.
// Then price, shade the expanded footprint, publish changed cells and optionally withdraw the computed cost.
bool TrDirtBag::PlaceWater(SpSelection *selection, GeBounds *affected, u32 *cost, bool free_of_charge) {
  EcBudgetLayer *budget = city->BudgetLayer();
  if (!budget)
    return false;
  u32 left, top, right, bottom;
  u32 changed = ScanSelectedWater(selection, &left, &top, &right, &bottom, true);
  affected->minimum.x = left << kCtMicroCoordinateShift;
  affected->minimum.z = top << kCtMicroCoordinateShift;
  affected->minimum.altitude = 0;
  affected->maximum.x = right << kCtMicroCoordinateShift;
  affected->maximum.z = bottom << kCtMicroCoordinateShift;
  affected->maximum.altitude = 0;
  SetEnabled(0);
  *cost = (changed * u32(budget->GetCost(kEcCostDecorativeWater))) >> 2;
  left = left <= 3 ? 0 : left - 3;
  top = top <= 3 ? 0 : top - 3;
  right += 3;
  if (right >= VertexCountX() - 1)
    right = VertexCountX() - 1;
  bottom += 3;
  if (bottom >= VertexCountZ() - 1)
    bottom = VertexCountZ() - 1;
  CalculateAndSetVertexLight(left, top, right, bottom);
  SetEnabled(1);
  PublishChanges(left, top, right - 1, bottom - 1, 1);
  if (!free_of_charge)
    budget->WithdrawFunds(*cost);
  return true;
}

// Read the linear distance WORD with the original wrapped byte offset. The all-ones sentinel means unreachable.
i32 TrDistanceToEdge(u16 *distance, u32 width, u32 x, u32 z) {
  u16 value;
  memcpy(&value, (const u8 *)distance + 2u * (x + z * width), sizeof(value));
  return value == UINT16_MAX ? -1 : value;
}

// Emit the descending water route with strict east/west/south/north tie ordering and its final cell.
// The original limit argument is unused; the caller provides sufficient temporary storage for the distance.
i32 TrSeaportFindPath(u16 *distance, u32 width, u32 *path, i32, u32 x, u32 z) {
  u32 selected = (u16)TrDistanceToEdge(distance, width, x, z), written = 0;
  if (selected == UINT16_MAX)
    return -1;
  if (selected > 1) {
    for (;;) {
      u32 direction = 0;
      u32 coordinate = (x << 16) | z;
      memcpy((u8 *)path + 4u * written, &coordinate, sizeof(coordinate));
      u32 east = (u16)TrDistanceToEdge(distance, width, x + 1, z),
          west = (u16)TrDistanceToEdge(distance, width, x - 1, z),
          south = (u16)TrDistanceToEdge(distance, width, x, z + 1),
          north = (u16)TrDistanceToEdge(distance, width, x, z - 1);
      if (east < selected) {
        selected = east;
        direction = 4;
      }
      if (west < selected) {
        selected = west;
        direction = 6;
      }
      if (south < selected) {
        selected = south;
        direction = 1;
      }
      if (north < selected) {
        selected = north;
        direction = 9;
      }
      x += 1u - (direction & 3);
      z += 1u - ((direction >> 2) & 3);
      ++written;
      if (selected <= 1)
        break;
    }
  }
  u32 coordinate = (x << 16) | z;
  memcpy((u8 *)path + 4u * written, &coordinate, sizeof(coordinate));
  return (i32)(written + 1);
}

// Publish a sweep's distance to exactly the changed bits in one row word. No unset bit's distance is read or changed.
static void TrStoreWaterDistances(u16 *distance, u32 width, u32 bits, u32 x, u32 z, u16 value) {
  u32 index = x + z * width;
  do {
    if (bits & 1)
      distance[index] = value;
    bits >>= 1;
    ++index;
  } while (bits);
}

u8 TrBit1SelectionGrowWriteHoriz(u16 *distance, u32 *reach, u32 *allowed, u32 width, u32 value, i32 *first, i32 *end) {
  i32 begin = *first, stop = *end;
  u32 words = width >> 5;
  u8 changed = 0;
  *first = (i32)width;
  *end = 0;
  for (i32 z = begin; z < stop; ++z) {
    u32 row = (u32)z * words, previous = 0, current = reach[row], next = words > 1 ? reach[row + 1] : 0;
    for (u32 word = 0; word < words; ++word) {
      u32 neighbors = (current << 1) | (current >> 1);
      if (word)
        neighbors |= previous >> 31;
      if (word + 1 < words)
        neighbors |= next << 31;
      u32 bits = current ^ (allowed[row + word] & (current | neighbors));
      if (bits) {
        if (z < *first)
          *first = z;
        if (z >= *end)
          *end = z + 1;
        changed = 1;
        reach[row + word] = current | bits;
        TrStoreWaterDistances(distance, width, bits, word << 5, (u32)z, (u16)value);
      }
      previous = current;
      current = next;
      if (word + 2 < words)
        next = reach[row + word + 2];
    }
  }
  return changed;
}

// Advance the primary vertical wave with saved neighboring rows, replacing its changed-row interval.
u8 TrBit1SelectionGrowWriteVert(u16 *distance, u32 *reach, u32 *allowed, u32 width, u32 value, i32 *first, i32 *end) {
  i32 begin = *first, last = *end - 1;
  u32 words = width >> 5;
  u8 changed = 0;
  *first = (i32)width;
  *end = 0;
  for (u32 word = 0; begin <= last && word < words; ++word) {
    u32 index = (u32)begin * words + word, current = reach[index], next = reach[index + words], previous = 0;
    for (i32 z = begin; z <= last; ++z) {
      u32 neighbors = z == begin ? next
          : z == last            ? previous
                                 : previous | next,
          bits = current ^ (allowed[index] & (current | neighbors));
      if (bits) {
        if (z < *first)
          *first = z;
        if (z >= *end)
          *end = z + 1;
        changed = 1;
        reach[index] = current | bits;
        TrStoreWaterDistances(distance, width, bits, word << 5, (u32)z, (u16)value);
      }
      previous = current;
      current = next;
      index += words;
      if (z + 1 < last)
        next = reach[index + words];
    }
  }
  return changed;
}

// Extend the deep horizontal wave without changing the caller's row interval. Only newly reached distances are written.
void TrBit1GrowDeepWriteHoriz(u16 *distance, u32 *reach, u32 *allowed, u32 width, u32 value, i32 *first, i32 *end) {
  i32 begin = *first, stop = *end;
  u32 words = width >> 5;
  for (i32 z = begin; z < stop; ++z) {
    u32 row = (u32)z * words, previous = 0, current = reach[row], next = words > 1 ? reach[row + 1] : 0;
    for (u32 word = 0; word < words; ++word) {
      u32 neighbors = (current << 1) | (current >> 1);
      if (word)
        neighbors |= previous >> 31;
      if (word + 1 < words)
        neighbors |= next << 31;
      u32 bits = current ^ (current | (allowed[row + word] & neighbors));
      if (bits) {
        reach[row + word] = current | bits;
        TrStoreWaterDistances(distance, width, bits, word << 5, (u32)z, (u16)value);
      }
      previous = current;
      current = next;
      if (word + 2 < words)
        next = reach[row + word + 2];
    }
  }
}

// Extend the deep vertical wave and replace the caller's changed-row interval, retaining inward-only boundary
// neighbors.
void TrBit1GrowDeepWriteVert(u16 *distance, u32 *reach, u32 *allowed, u32 width, u32 value, i32 *first, i32 *end) {
  i32 begin = *first, last = *end - 1;
  u32 words = width >> 5;
  *first = (i32)width;
  *end = 0;
  for (u32 word = 0; begin <= last && word < words; ++word) {
    u32 index = (u32)begin * words + word, current = reach[index], next = reach[index + words], previous = 0;
    for (i32 z = begin; z <= last; ++z) {
      u32 neighbors = z == begin ? next
          : z == last            ? previous
                                 : previous | next,
          bits = current ^ (current | (allowed[index] & neighbors));
      if (bits) {
        if (z < *first)
          *first = z;
        if (z >= *end)
          *end = z + 1;
        reach[index] = current | bits;
        TrStoreWaterDistances(distance, width, bits, word << 5, (u32)z, (u16)value);
      }
      previous = current;
      current = next;
      index += words;
      if (z + 1 < last)
        next = reach[index + words];
    }
  }
}

// Build the installed distance-field effects using the original four alternating sweeps and boundary seed order.
void TrSeaportRecompute(u16 *distance, u32 *reach, u32 *open, u32 *deep, u32 width) {
  u32 cells = width * width, words = cells >> 5, row_words = width >> 5;
  for (u32 i = words; i;)
    reach[--i] = 0;
  if (cells)
    memset(distance, 0xff, (usize)cells * sizeof(u16));
  for (u32 word = 0, cell = 0; word < words; word += row_words, cell += width) {
    reach[word] = open[word] & 1;
    if (reach[word])
      distance[cell] = 1;
    u32 last = word + row_words - 1;
    reach[last] = open[last] & kTrReachabilityLastColumnBit;
    if (reach[last])
      distance[cell + width - 1] = 1;
  }
  for (u32 row = 0; row < 2; ++row) {
    u32 word = row ? words - row_words : 0, cell = row ? width * (width - 1) : 0;
    for (u32 i = 0; i < row_words; ++i) {
      u32 bits = open[word + i];
      reach[word + i] = bits;
      for (u32 bit = 0; bit < 32; ++bit) {
        if (bits & 1)
          distance[cell] = 1;
        ++cell;
        bits >>= 1;
      }
    }
  }
  i32 row_first = 0, column_first = 0, row_end = (i32)width, column_end = (i32)width;
  u32 value = 2;
  for (;;) {
    bool horizontal = TrBit1SelectionGrowWriteHoriz(distance, reach, open, width, value++, &row_first, &row_end),
         vertical = TrBit1SelectionGrowWriteVert(distance, reach, open, width, value++, &column_first, &column_end);
    if (row_first >= column_first)
      row_first = column_first;
    column_first = row_first;
    if (row_end <= column_end)
      row_end = column_end;
    column_end = row_end;
    if (column_first > 0)
      --column_first;
    if (column_end < (i32)width)
      ++column_end;
    if (!horizontal && !vertical)
      break;
    TrBit1GrowDeepWriteHoriz(distance, reach, deep, width, value++, &row_first, &row_end);
    i32 next_first = column_first, next_end = column_end;
    TrBit1GrowDeepWriteVert(distance, reach, deep, width, value++, &next_first, &next_end);
    if (next_first == column_first) {
      row_first = column_first;
      if (column_first > 0)
        --column_first;
    }
    if (next_end == column_end) {
      row_first = column_first;
      if (column_end < (i32)width)
        ++column_end;
    }
  }
}

// Classify four corners of every square-city cell, then compute water routes from the original boundary seeds.
// Apply blocked bits and publish deep-water presence only at completed 32-cell words, matching the installed loop.
void TrDirtBag::RecomputeSeaportMap() {
  u32 width = city->CellCountX(), word = 0, bit = 1;
  if (!distances)
    distances = new u16[2u * width * width / sizeof(u16)];
  deep_water = false;
  for (u32 z = 0; z < width; ++z) {
    for (u32 x = 0; x < width; ++x) {
      water_open[word] |= bit;
      water_deep[word] |= bit;
      const u32 corner_x[4] = {x, x, x + 1, x + 1}, corner_z[4] = {z, z + 1, z + 1, z};
      for (u32 corner = 0; corner < 4; ++corner) {
        u8 dry, wet;
        GetVertexAltitudeDirt(corner_x[corner], corner_z[corner], &dry);
        GetVertexAltitudeWater(corner_x[corner], corner_z[corner], &wet);
        if (wet < 8 || dry > wet - 8)
          water_deep[word] &= ~bit;
        if (dry > wet || (dry == wet && wet != sea))
          water_open[word] &= ~bit;
      }
      bit <<= 1;
      if (!bit) {
        water_open[word] &= ~blocked_cells[word];
        water_deep[word] &= ~blocked_cells[word];
        if (water_deep[word])
          deep_water = true;
        ++word;
        bit = 1;
      }
    }
  }
  TrSeaportRecompute(distances, water_reach, water_open, water_deep, width);
}

// Mark adjacent cells in original order, re-reading the live city dimensions at each original call.
// Edge vertices omit neighboring cells outside the city without replacing city getters with terrain extents.
void TrDirtBag::MarkVertexChanged(u32 x, u32 z) const {
  if (x) {
    if (z)
      MarkCellChanged(x - 1, z - 1);
    if (z < city->CellCountZ())
      MarkCellChanged(x - 1, z);
  }
  if (x < city->CellCountX() && z)
    MarkCellChanged(x, z - 1);
  if (x < city->CellCountX() && z < city->CellCountZ())
    MarkCellChanged(x, z);
}

// Lazily request the real seaport-map producer, then copy a reachable distance to the genuine output pointer.
// An unreachable vertex returns false without writing the caller's existing output.
bool TrDirtBag::Distance(u32 x, u32 z, u32 *distance) const {
  if (!distances)
    const_cast<TrDirtBag *>(this)->RecomputeSeaportMap();
  i32 value = TrDistanceToEdge(distances, city->CellCountX(), x, z);
  if (value < 0)
    return false;
  *distance = (u32)value;
  return true;
}

// Trace into the original 512-word scratch area or its distance-sized heap replacement, then truncate the published path.
// Preserve the second city-width lookup, packed coordinate conversion, and untouched outputs on unreachable input.
bool TrDirtBag::GetWaterPath(u32 x, u32 z, u32 capacity, ScPointXZ *path, u32 *count) const {
  if (!distances)
    const_cast<TrDirtBag *>(this)->RecomputeSeaportMap();
  i32 distance = TrDistanceToEdge(distances, city->CellCountX(), x, z);
  if (distance < 0)
    return false;
  u32 scratch[512], *allocated = distance > 512 ? new u32[distance] : nullptr,
                    *packed = allocated ? allocated : scratch;
  *count = (u32)TrSeaportFindPath(distances, city->CellCountX(), packed, distance, x, z);
  if (*count > capacity)
    *count = capacity;
  for (u32 i = 0; i < *count; ++i) {
    path[i].x = packed[i] >> 16;
    path[i].z = packed[i] & 0xffff;
  }
  delete[] allocated;
  return true;
}

u32 TrDirtBag::GetPathGranularity() const {
  return 1;
}

// Set or clear the row-major blocked-cell bit using the live city width. Other bits retain their previous values.
void TrDirtBag::BlockCell(u32 x, u32 z, bool blocked) {
  u32 index = x + z * city->CellCountX(), bit = 1u << (index & 31);
  if (blocked)
    blocked_cells[index >> 5] |= bit;
  else
    blocked_cells[index >> 5] &= ~bit;
}

bool TrDirtBag::IsCellBlocked(u32 x, u32 z) const {
  u32 index = x + z * city->CellCountX();
  return (blocked_cells[index >> 5] & (1u << (index & 31))) != 0;
}

bool TrDirtBag::IsDeepWaterPresent() {
  if (!distances)
    RecomputeSeaportMap();
  return deep_water;
}

// Probe the four edge midpoints through the current city's salt-water layer while holding the terrain lock.
// A direction counts as a neighbor when its midpoint is not salt water; a missing layer has no neighbors.
bool TrDirtBag::IsNeighborPresent(u8 directions) const {
  u8 neighbors = 0;
  {
    TrLockScope lock;
    EcLayer *pollution = TrGetCity()->PollutionLayer();
    if (pollution) {
      u32 width = VertexCountX(), height = VertexCountZ();
      if (!pollution->IsSaltWater(width >> 1, 0))
        neighbors |= 1;
      if (!pollution->IsSaltWater(width - 2, height >> 1))
        neighbors |= 2;
      if (!pollution->IsSaltWater(width >> 1, height - 2))
        neighbors |= 4;
      if (!pollution->IsSaltWater(0, height >> 1))
        neighbors |= 8;
    }
  }
  return (directions & neighbors) != 0;
}

// Install a callback once by function identity, preserving the first context and insertion order on repeated installs.
// Removal ignores context and unlinks the matching node; every defined path returns true.
bool TrDirtBag::InstallFunctionFilter(TrFunctionFilter filter, void *context, bool install) {
  TrFilterLink *node = filters->next;
  while (node != filters && node->filter != filter)
    node = node->next;
  if (install) {
    if (node == filters) {
      node = new TrFilterLink;
      node->filter = filter;
      node->context = context;
      node->next = filters;
      node->previous = filters->previous;
      filters->previous->next = node;
      filters->previous = node;
    }
  } else if (node != filters) {
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete node;
  }
  return true;
}

// Invoke the actual ordered placement callbacks with the original bounds, cost, kind and caller-owned context.
// Stop at the first false; advance through the live chain after each callback so valid registration changes take effect.
bool TrDirtBag::FunctionFilterTestPassed(const GeBounds &bounds, u32 cost, i32 kind) const {
  TrFilterLink *node = filters->next;
  while (node != filters) {
    if (!node->filter(bounds, cost, kind, node->context))
      return false;
    node = node->next;
  }
  return true;
}

TrRenderer *TrTerrainRenderer;

// Bind the original terrain interface and read its concrete terrain planes and sea under the recursive module lock.
// Publish readiness only after the real independent noise stream has filled both global tables.
void TrRenderer::SetDirtBag(ScDirtBag *source) {
  TrLockScope lock;
  terrain = source;
  TrDirtBag *bag = static_cast<TrDirtBag *>(source);
  ground = bag->ground;
  shade = bag->shade;
  water = bag->water;
  sea = bag->sea;
  TrTile::GenerateBumpMaps();
  ready = true;
}

u8 TrRenderer::SetGrid(u8 enabled) {
  grid = enabled;
  return enabled;
}

u8 TrRenderer::GetGrid() const {
  return grid;
}

bool TrDirtBag::GetGrid() const {
  return TrTerrainRenderer->GetGrid();
}

// Change the shared renderer's grid flag only when its value differs, keeping the complete update under the module lock.
void TrDirtBag::SetGrid(bool enabled) {
  TrLockScope lock;
  if (TrTerrainRenderer->GetGrid() != u8(enabled))
    TrTerrainRenderer->SetGrid(enabled);
}

// Create the land tile and publish its identity before acquiring its first live reference.
u8 TrRenderer::GetDirtClodLand(u32 cell_x, u32 cell_z, u8 palette_kind, u8 overlay, SpSprite **out) const {
  SpSprite *tile = new TrDryTile(u16(cell_x), u16(cell_z), palette_kind, overlay);
  *out = tile;
  tile->AddRef();
  return 1;
}

// Create the shore tile and publish its identity before acquiring its first live reference.
u8 TrRenderer::GetDirtClodShore(u32 cell_x, u32 cell_z, u8 palette_kind, u8 overlay, SpSprite **out) const {
  SpSprite *tile = new TrReliefTile(u16(cell_x), u16(cell_z), palette_kind, overlay);
  *out = tile;
  tile->AddRef();
  return 1;
}

// Create the water tile and publish its identity before acquiring its first live reference.
u8 TrRenderer::GetDirtClodWater(u32 cell_x, u32 cell_z, u8 palette_kind, u8 overlay, SpSprite **out) const {
  SpSprite *tile = new TrWaterTile(u16(cell_x), u16(cell_z), palette_kind, overlay);
  *out = tile;
  tile->AddRef();
  return 1;
}

// Create the cursor tile and publish its identity before acquiring its first live reference.
u8 TrRenderer::GetDirtClodCursor(
    u32 cell_x, u32 cell_z, u8 palette_kind, u16 row, bool dry_only, SpSprite **out) const {
  SpSprite *tile = new TrSingleTile(u16(cell_x), u16(cell_z), palette_kind, row, dry_only);
  *out = tile;
  tile->AddRef();
  return 1;
}

// Create the edge tile and publish its identity before acquiring its first live reference.
u8 TrRenderer::GetDirtClodEdge(u32 cell_x, u32 cell_z, bool reverse_side, bool dry_only, SpSprite **out) const {
  SpSprite *tile = new TrEdgeTile(u16(cell_x), u16(cell_z), reverse_side, dry_only);
  *out = tile;
  tile->AddRef();
  return 1;
}

u8 TrRenderer::GetLightIndex(f32 cosine) const {
  return u8(acos(f64(cosine)) / GetLightStep());
}

u32 TrTileWidths[5], TrTileHalfWidths[5], TrTileHeights[5], TrTileScales[5];
TrRenderer *TrRendererSingleton;

// Initialize original lifetime flags before locking, then publish scale tables, load all nine palettes and allocate cache.
// Terrain planes and sea remain unset until SetDirtBag supplies them.
TrRenderer::TrRenderer() : references(0), ready(false), grid(false), palettes(nullptr), field16(0), selection(0) {
  TrLockScope lock;
  for (u32 i = 0; i < 5; ++i) {
    TrTileWidths[i] = 8u << i;
    TrTileHalfWidths[i] = (8u << i) >> 1;
    TrTileHeights[i] = 4u << i;
    TrTileScales[i] = 1u << i;
  }
  BuildColorLightTables();
  cache = new u16[257 * 257];
}

// Unsubscribe only a ready receiver before locking, then retire its actual palettes and uninitialized pixel cache.
TrRenderer::~TrRenderer() {
  if (ready)
    Shutdown();
  TrLockScope lock;
  ReleaseColorLightTables();
  delete[] cache;
  cache = nullptr;
}

void TrRenderer::InvalidateColorTable() {
  if (cache)
    memset(cache, 0, 257 * 257 * sizeof(u16));
}

// Publish the actual unknown/message-target receiver before virtual AddRef, clearing the output.
bool TrRenderer::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, TrRenderer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, TrRenderer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 TrRenderer::AddRef() {
  TrAdjustReference(&references, 1);
  return references;
}

// Decrement only a nonzero count, then destroy this concrete renderer when the current count is zero.
u32 TrRenderer::Release() {
  if (references)
    TrAdjustReference(&references, -1);
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

u8 TrRenderer::Init() {
  ScMessageServer *server = TrGetMessageServer();
  if (server)
    server->AddNotification(this, kCtMsg_BeforeLandscapeChange);
  return 1;
}

u8 TrRenderer::Shutdown() {
  if (ready) {
    ready = false;
    ScMessageServer *server = TrGetMessageServer();
    if (server)
      server->RemoveNotification(this, kCtMsg_BeforeLandscapeChange);
  }
  return 1;
}

// Publish the singleton only after its full constructor returns; constructor reentry sees the original empty cache.
// The original cache is non-owning and is not reset by the renderer destructor.
TrRenderer *TrGetRenderer() {
  if (!TrRendererSingleton)
    TrRendererSingleton = new TrRenderer;
  return TrRendererSingleton;
}

void TrRenderer::RegenPalette(u8 index) {
  TrLockScope lock;
  if (index >= 9)
    return;
  palettes[index].Shutdown();
  if (index == 4) {
    CtSchemeManager *scheme = TrGetApplication()->CitySchemeManager();
    u32 group, instance;
    if (scheme && scheme->GetLandScapePalette(selection, &group, &instance)) {
      const GxColorType format(GxColorType::Color::RGB16);
      GxBuffer *image;
      {
        TrImageHandle handle(instance, group, GxVramRequest::SystemMemory, format);
        image = handle.Value();
      }
      if (image) {
        image->AddRef();
        palettes[index].InitFromBuffer(image);
        image->Release();
      }
    }
    return;
  }
  static const char *const names[9] = {"palBasic.bmp", "data1Pal.bmp", "data2Pal.bmp", "data3Pal.bmp", nullptr,
      "palUnder.bmp", "palWater.bmp", "palEdgeL.bmp", "palEdgeD.bmp"};
  ScString name(names[index], (u32)strlen(names[index]));
  if (!index)
    palettes[index].InitFromPaletteFile(name);
  else
    palettes[index].InitFromFile(name);
}

u8 TrRenderer::BuildColorLightTables() {
  palettes = new TrPalette[9];
  for (u8 i = 0; i < 9; ++i)
    RegenPalette(i);
  return 1;
}

void TrRenderer::ReleaseColorLightTables() {
  if (palettes) {
    for (u32 i = 0; i < 9; ++i)
      palettes[i].Shutdown();
    delete[] palettes;
    palettes = nullptr;
  }
}

void TrRenderer::SetAltitudePalette(u32 value) {
  if (value == selection)
    return;
  CtCity *world = TrGetCity();
  ScMessageServer *server = TrGetMessageServer();
  if (server) {
    if (world) {
      world->SimHiddenPause();
      world->Lock();
    }
    selection = value;
    RegenPalette(4);
    ScMessage message = {kTrMsg_AltitudePaletteChanged, 0, 0, 0};
    server->MessagePost(message, false);
    if (world) {
      world->SimHiddenUnpause();
      world->Unlock();
    }
  }
}

// Process only the original palette-selection notification, narrowing its scalar payload to the original DWORD.
bool TrRenderer::DoMessage(ScMessage *message) {
  if (message->type == kCtMsg_BeforeLandscapeChange)
    SetAltitudePalette((u32)message->data1);
  return true;
}

void TrDirtBag::RegenPalette(u8 index) {
  TrTerrainRenderer->RegenPalette(index);
}

#ifdef RGB
#undef RGB
#endif

TrPalette::TrPalette() : initialized(false), maximum_row(0), rows(nullptr) {
}

TrPalette::~TrPalette() {
  Shutdown();
}

// Retire initialized row allocations in ascending order, clearing each pointer before retiring the row-pointer array.
// The WORD maximum is zero-extended before adding one, so every allocated row through that endpoint is visited.
void TrPalette::Shutdown() {
  if (initialized) {
    if (rows) {
      for (u32 row = 0; row <= maximum_row; ++row) {
        if (rows[row]) {
          delete[] rows[row];
          rows[row] = nullptr;
        }
      }
      delete[] rows;
      rows = nullptr;
    }
    maximum_row = 0;
    initialized = false;
  }
}

// Build 32 angular shades for each captured RGB row under the module lock, publishing initialized only after all pixels.
// Resolve graphics separately for every conversion and reload the live row pointer after its callback before storing.
u8 TrPalette::InitFromPalette(i32 count, const ScRgb8 *rgb) {
  TrTerrainLock.Lock();
  if (initialized)
    Shutdown();
  maximum_row = (u16)((u32)count - 1);
  rows = new u16 *[count];
  for (i32 row = 0; row < count; ++row) {
    rows[row] = new u16[u32(LightIndexMax) + 1];
    ScRgb8 color = rgb[row];
    for (u32 column = 0; column <= LightIndexMax; ++column) {
      volatile f32 stored_weight = (f32)((f64)0.35f * cos((u32(LightIndexMax) - column) * GetLightStep()));
      f64 weight = stored_weight > 0.0f ? stored_weight : 0.0;
      u8 r = (u8)((f64)color.red * (1.0 - weight)), g = (u8)((f64)color.green * (1.0 - weight)),
         b = (u8)((f64)color.blue * (1.0 - weight));
      u16 pixel = TrGetGraphics()->ConvertRGBValueToNative16Bit(r, g, b);
      rows[row][column] = pixel;
    }
  }
  initialized = true;
  TrTerrainLock.Unlock();
  return 1;
}

f64 TrPalette::GetLightStep() {
  return 0x1.9f1879ce739cep-5;
}

// Copy locked image pixels into new palette rows, reloading the destination row after each virtual pixel read.
// A null image returns zero; a false lock preserves existing storage and still publishes initialized with byte result one.
u8 TrPalette::InitFromBuffer(GxBuffer *image) {
  if (!image)
    return 0;
  if (image->Lock(GxLockFlags::GetSetPixel)) {
    u32 width = image->Width(), height = image->Height();
    rows = new u16 *[height];
    maximum_row = (u16)(height - 1);
    for (u32 row = 0; row < height; ++row) {
      rows[row] = new u16[width];
      for (u32 column = 0; column < width; ++column)
        rows[row][column] = (u16)image->GetPixel((i32)column, (i32)row);
    }
    image->Unlock(GxLockFlags::GetSetPixel);
  }
  initialized = true;
  return 1;
}

u8 TrPalette::InitFromFile(const ScString &name) {
  TrLockScope lock;
  if (initialized)
    Shutdown();
  ScString path;
  RsFileSystem *system = TrGetFileSystem();
  if (system) {
    const char *directory = system->DataDirectory();
    if (directory)
      path.FromChar(directory);
  }
  path.Append("Res\\Dirt\\palette\\");
  path.Append(name.begin, (u32)(name.end - name.begin));
  GxBuffer *image = nullptr;
  if (TrGetApplication() && TrGetApplication()->GetBufferResourceFactory()) {
    const GxColorType format(GxColorType::Color::RGB16);
    TrGetApplication()->GetBufferResourceFactory()->LoadFile(&path, &image, 0, &format, 0);
  }
  u8 result = 0;
  if (image) {
    result = InitFromBuffer(image);
    image->Release();
  }
  return result;
}

// State the required image and allocation-size invariants before entering the original pixel and shade loops.
static i32 TrBasicImageSize(GxBuffer *image, i32 *width) {
  assert(image);
  *width = image->Width();
  i32 height = image->Height();
  assert(*width > 0 && height > 0 && *width <= INT_MAX / 3 / height);
  return *width * height;
}

u8 TrPalette::InitFromPaletteFile(const ScString &name) {
  TrLockScope lock;
  if (initialized)
    Shutdown();
  ScString path;
  RsFileSystem *system = TrGetFileSystem();
  if (system) {
    const char *directory = system->DataDirectory();
    if (directory)
      path.FromChar(directory);
  }
  path.Append("Res\\Dirt\\Palette\\");
  path.Append(name.begin, (u32)(name.end - name.begin));
  GxBuffer *image = nullptr;
  if (TrGetApplication() && TrGetApplication()->GetBufferResourceFactory()) {
    const GxColorType format(GxColorType::Color::RGB16);
    TrGetApplication()->GetBufferResourceFactory()->LoadFile(&path, &image, 0, &format, 0);
  }
  i32 width, count = TrBasicImageSize(image, &width);
  ScRgb8 *rgb = new ScRgb8[count];
  bool locked = image->Lock(GxLockFlags::GetSetPixel);
  if (locked) {
    u8 red = 0, green = 0, blue = 0;
    for (i32 i = 0; i < count; ++i) {
      u16 pixel = (u16)image->GetPixel(i % width, i / width);
      TrGetGraphics()->Convert16BitNativeValueToRGB(pixel, &red, &green, &blue);
      rgb[i] = {red, green, blue};
    }
    image->Unlock(GxLockFlags::GetSetPixel);
  }
  image->Release();
  assert(locked); // Stock would consume unwritten RGB rows after the same release if locking failed.
  InitFromPalette(count, rgb);
  delete[] rgb;
  initialized = true;
  return 1;
}

TrImageHandle::TrImageHandle() : buffer(nullptr) {
}

TrImageHandle::TrImageHandle(const TrImageHandle &other) : buffer(other.buffer) {
  if (buffer)
    static_cast<GxBuffer *>(buffer)->AddRef();
}

// Retain the source before releasing the current image, then reread the source identity after those callbacks.
// Self-assignment keeps the original owner without making either reference call.
TrImageHandle &TrImageHandle::operator=(const TrImageHandle &other) {
  if (&other != this) {
    if (other.buffer)
      static_cast<GxBuffer *>(other.buffer)->AddRef();
    if (buffer)
      static_cast<GxBuffer *>(buffer)->Release();
    buffer = other.buffer;
  }
  return *this;
}

// Set live image factory options before loading the requested buffer; release options before the factory reference.
TrImageHandle::TrImageHandle(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format) : buffer(nullptr) {
  RsManager *manager = TrGetResourceManager();
  RsFactoryInterface *factory;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    void *identity;
    if (factory->QueryInterface(IID_RsImageOptions, &identity)) {
      RsImageOptions *options = static_cast<RsImageOptions *>(identity);
      options->SetMode(mode);
      options->SetFormat(format);
      LoadResource(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

TrImageHandle::~TrImageHandle() {
  if (buffer)
    static_cast<GxBuffer *>(buffer)->Release();
  buffer = nullptr;
}

// Borrow the current image without acquiring another ownership reference.
GxBuffer *TrImageHandle::Value() const {
  return static_cast<GxBuffer *>(buffer);
}

bool TrImageHandle::IsBufferPresent() const {
  return buffer != nullptr;
}

void TrImageHandle::LoadResource(RsManager *manager, u32 group, u32 instance) {
  RsKey key = {kRsImageResourceType, group, instance};
  manager->GetResource(key, IID_GxBuffer, &buffer);
}

TrTextResource::TrTextResource() : text_identity(nullptr), key{} {
}

TrTextResource::TrTextResource(const TrTextResource &other) : text_identity(nullptr), key(other.key) {
  LoadResource();
}

// Release the current text before rebuilding the source key with the fixed text-resource type and resolving it again.
// Self-assignment performs no release or lookup; source key reads follow the old resource callback.
TrTextResource &TrTextResource::operator=(const TrTextResource &other) {
  if (&other != this) {
    UnloadResource();
    other.GetResKey(&key);
    LoadResource();
  }
  return *this;
}

TrTextResource::TrTextResource(u32 instance, u32 group)
    : text_identity(nullptr), key{kRsTextResourceType, group, instance} {
  LoadResource();
}

TrTextResource::~TrTextResource() {
  UnloadResource();
}

// Release the current text before clearing the published identity. An empty handle performs neither operation.
void TrTextResource::UnloadResource() {
  if (text_identity) {
    static_cast<ScStringView *>(text_identity)->Release();
    text_identity = nullptr;
  }
}

void TrTextResource::LoadResource() {
  if (!TrGetResourceManager()->GetResource(key, IID_ScStringView, &text_identity))
    text_identity = nullptr;
}

// Borrow the retained text through the original const Value accessor without acquiring another reference.
// Its retail declaration is verified; a distinct SIMDIRT entry for this field getter has not been established.
const ScStringView *TrTextResource::Value() const {
  return static_cast<const ScStringView *>(text_identity);
}

// Convert the retained interface to an owned String copy, or produce an empty value after failed lookup.
// The compiler supplies the original aggregate return storage; there is no additional game output parameter.
TrTextResource::operator ScString() {
  const ScStringView *value = Value();
  if (value)
    return ScString(*value);
  return ScString();
}

// Publish the original text-resource type with the stored group and instance, even when the stored type is zero.
// The source writes the type before reading either remaining word.
void TrTextResource::GetResKey(RsKey *out) const {
  out->type = kRsTextResourceType;
  out->group = key.group;
  out->instance = key.instance;
}

bool TrTextResource::IsStringPresent() {
  return text_identity != nullptr;
}

// Copy the original row-to-palette phase using the existing actual graphics palette implementation.
static bool TrCopyPaletteRow(GxBuffer *image, u32 row, u32 count, GxPaletteView **out) {
  if (!out || !image || image->Width() < count || row > image->Height())
    return false;
  GxPaletteView *result = new GxPalette;
  result->AddRef();
  bool success = false;
  if (result->MakeGenericPalette((i32)count, 32) && image->Lock(GxLockFlags::GetSetPixel)) {
    for (u32 i = 0; i < count; ++i) {
      u32 value = image->GetPixel((i32)i, (i32)row);
      result->SetColor((i32)i, value);
    }
    image->Unlock(GxLockFlags::GetSetPixel);
    void *identity;
    success = result->QueryInterface(IID_GxPaletteView, &identity);
    if (success)
      *out = static_cast<GxPaletteView *>(identity);
  }
  result->Release();
  return success;
}

// Retain the terrain, capture its dimensions in Z-then-X order, then optionally build the twelve-color landscape palette.
TrTerrainMapSprite::TrTerrainMapSprite(TrDirtBag *source) : terrain(source) {
  CtSchemeManager *scheme = TrGetApplication()->CitySchemeManager();
  terrain->AddRef();
  u32 z = source->CellCountZ(), x = source->CellCountX();
  SetId(kUiTerrainMapRasterId);
  SetBaseSize(x, z);
  if (scheme) {
    u32 row;
    scheme->GetNavViewTerrainPaletteForCurrentLandScape(&row);
    const GxColorType format(GxColorType::Color::RGB16);
    TrImageHandle image(33, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = image.Value();
    GxPaletteView *colors = nullptr;
    if (buffer && TrCopyPaletteRow(buffer, row, 12, &colors)) {
      SetPalette(colors);
      colors->Release();
    }
  }
}

TrTerrainMapSprite::~TrTerrainMapSprite() {
  ScRelease(&terrain);
}

// Read a map color through the live palette or the original graphics RGB fallback, preserving byte subtraction and bins.
bool TrTerrainMapSprite::GetColor(u32 x, u32 z, u32 *out) {
  GxPaletteView *colors = GetPalette();
  if (colors) {
    if (colors->GetNumberEntries() != 12)
      return false;
    u8 dirt = 0, water = 0;
    if (terrain->IsWater(x, z))
      return colors->GetColor(0, out);
    terrain->GetVertexAltitudeDirt(x, z, &dirt);
    terrain->GetVertexAltitudeWater(x, z, &water);
    u8 difference = (u8)(dirt - water);
    u32 index;
    if (difference < 10)
      index = (difference >> 1) + 1;
    else {
      index = 10u * difference / 256 + 5;
      if (index > 11)
        index = 11;
    }
    return colors->GetColor((i32)index, out);
  }
  u8 altitude = 0;
  u16 pixel;
  if (terrain->IsWater(x, z))
    pixel = TrGetGraphics()->ConvertRGBValueToNative16Bit(0, 0, 160);
  else {
    terrain->GetVertexAltitude(x, z, &altitude);
    pixel = TrGetGraphics()->ConvertRGBValueToNative16Bit(0, (u8)((altitude >> 1) + 127), 0);
  }
  *out = pixel;
  return true;
}

TrRasterSource::TrRasterSource() : references(0), kind(0), palette(nullptr), width(0), height(0), enabled(0) {
  TrRasterSource::SetDestinationCoords(0, 0, 0, 1, 1, 0);
}

TrRasterSource::~TrRasterSource() {
  ScRelease(&palette);
}

// Publish the actual unknown/raster identity before the virtual retain, clearing the output.
bool TrRasterSource::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, TrRasterSource);
  HANDLE_QUERY_INTERFACE(IID_GxRasterSource, TrRasterSource);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 TrRasterSource::AddRef() {
  return ++references;
}

u32 TrRasterSource::Release() {
  return ScReleaseImpl(this, &references);
}

// Set the raster kind before its dimensions through the actual virtual entries, ignoring both setter replies.
bool TrRasterSource::Init(u32 value, u32 x, u32 z) {
  SetId(value);
  SetBaseSize(x, z);
  return true;
}

u32 TrRasterSource::GetId() const {
  return kind;
}

bool TrRasterSource::SetId(u32 value) {
  kind = value;
  return true;
}

// Retain the incoming palette before inspecting and releasing the current palette, then publish the new owner.
bool TrRasterSource::SetPalette(GxPaletteView *source) {
  if (source)
    source->AddRef();
  GxPaletteView *previous = palette;
  if (previous) {
    previous->Release();
    palette = nullptr;
  }
  palette = source;
  return true;
}

// Borrow the current palette without another retain; the original reads the field at+12.
GxPaletteView *TrRasterSource::GetPalette() {
  return palette;
}

i32 TrRasterSource::GetBaseWidth() const {
  return width;
}

i32 TrRasterSource::GetBaseHeight() const {
  return width;
}

// Publish both original unsigned dimension arguments as their stored DWORD bits and report success.
bool TrRasterSource::SetBaseSize(u32 x, u32 z) {
  width = i32(x);
  height = i32(z);
  return true;
}

u8 TrRasterSource::SetPostFilter(u8 value) {
  enabled = value;
  return value;
}

// Store three defining corners and derive the fourth in the source's nonsequential write order.
void TrRasterSource::SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) {
  corner_x[1] = x1;
  corner_x[0] = x0;
  corner_z[1] = z1;
  corner_x[2] = i32(u32(x3) + u32(x1) - u32(x0));
  corner_z[0] = z0;
  corner_x[3] = x3;
  corner_z[2] = i32(u32(z3) + u32(z1) - u32(z0));
  corner_z[3] = z3;
}

u8 TrRasterSource::IsPostFilter() const {
  return enabled;
}

// Require a destination, run the virtual pixel pass, and invoke completion only when that pass reports success.
bool TrRasterSource::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  if (!destination)
    return false;
  if (!DrawCells(destination, rectangle, clip))
    return false;
  return DrawAnnotations(destination, rectangle, clip);
}

u8 TrRasterSource::DebugInstanceTag(ScStringView *) {
  return 0;
}

u8 TrRasterSource::DebugClassTag(ScStringView *) {
  return 0;
}

u8 TrRasterSource::DebugTypeTag(ScStringView *) {
  return 0;
}

bool TrRasterSource::GetColor(u32, u32, u32 *) {
  return false;
}

// Scale columns before rows with a vertical source flip and destination clipping.
// Wrap DWORD coordinate arithmetic before signed scaling, matching the installed SUB/IMUL/IDIV sequence.
bool TrRasterSource::DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  i32 span_x = i32(u32(rectangle.right) - u32(rectangle.left)),
      span_z = i32(u32(rectangle.bottom) - u32(rectangle.top));
  i32 left = 0, top = 0, right = span_x, bottom = span_z;
  if (clip) {
    i32 x0 = i32(u32(clip->left) - u32(rectangle.left)), z0 = i32(u32(clip->top) - u32(rectangle.top));
    i32 x1 = i32(u32(clip->right) - u32(rectangle.left)), z1 = i32(u32(clip->bottom) - u32(rectangle.top));
    if (x0 > left)
      left = x0;
    if (z0 > top)
      top = z0;
    if (x1 < right)
      right = x1;
    if (z1 < bottom)
      bottom = z1;
  }
  u32 pixel = 0;
  i32 last_z = i32(u32(height) - 1);
  for (i32 x = left; x < right; ++x) {
    i32 source_x = i32(u32(x) * u32(width)) / span_x;
    for (i32 z = top; z < bottom; ++z) {
      i32 source_z = i32(u32(z) * u32(height)) / span_z;
      if (GetColor(u32(source_x), u32(last_z) - u32(source_z), &pixel))
        destination->SetPixel(u32(x) + u32(rectangle.left), u32(z) + u32(rectangle.top), pixel);
    }
  }
  return true;
}

bool TrRasterSource::DrawAnnotations(GxBuffer *, const ScRect &, ScRect *) {
  return true;
}

u8 TrRasterSource::SetDebugInstanceTag(const ScStringView &) {
  return 0;
}

u8 TrRasterSource::SetDebugClassTag(const ScStringView &) {
  return 0;
}
