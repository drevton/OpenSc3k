#include <cmath>
#include <cstring>
#include <new>
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "disasters/ds_tornado.h"
#include "economy/api.h"
#include "geometry/ge_types.h"
#include "network/api.h"
#include "network/nw_street_producer.h"
#include "network/nw_street_vehicle.h"
#include "network/street.h"
#include "network/street_traffic.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/sp_effect.h"
#include "sprites/sprite.h"
#include "terrain/api.h"

void NwBoatVehicle::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

void NwShipVehicle::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

// Place the boat at the center of a clear water cell and start its routine and wake.
u8 NwBoatVehicle::PutInCell(u32 cellX, u32 cellZ) {
  if (waterTerrain->IsCellBlocked(cellX, cellZ) || waterTerrain->CellWaterVertCount(cellX, cellZ) != 4)
    return 0;
  u32 start = cellZ + (cellX << 16);
  startZ = static_cast<u16>(start);
  startX = static_cast<u16>(start >> 16);
  x = static_cast<i32>((cellX << kCtMicroCoordinateShift) + 128);
  z = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + 128);
  u8 waterAltitude;
  waterTerrain->GetVertexAltitudeWater(cellX, cellZ, &waterAltitude);
  altitude = i32(waterAltitude) << kCtMicroCoordinateShift;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  if (BoatRoutine() == 1)
    return 0;
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 128, &sortId);
  if (wake) {
    CtCoord position{x, z, altitude};
    wake->SetLocation(position);
    wake->SetOrientation(orientation);
    animationManager->InsertOccupant(static_cast<ScOccupant *>(wake), 1, 0, true);
  }
  return 1;
}

u8 NwBoatVehicle::SubTypeInfo() const {
  return 7;
}

u8 NwShipVehicle::SubTypeInfo() const {
  return 6;
}

u8 NwBoatVehicle::StaticInit() {
  updateFunction = &NwBoatVehicle::BoatRoutineUpdateFn;
  return 1;
}

u8 NwBoatVehicle::StaticShutdown() {
  updateFunction = nullptr;
  return 1;
}

// Dispatch base vehicle actions and the boat-specific routine callback.
i32 NwBoatVehicle::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  u32 action = actionStack[actionStackTop];
  if (action <= 16)
    return NwStreetVehicle::ExecuteCurrentUpdateFn(elapsed, status, cancel);
  return (this->*updateFunction)(elapsed, status, cancel);
}

// Project a point behind the boat and require both that cell and the next one to be clear water.
bool NwBoatVehicle::FindStraightWaterPoint(f32 heading, i32 distance, i32 *outX, i32 *outZ) {
  i32 deltaX = static_cast<i32>(static_cast<i64>(std::cos(f64(heading)) * distance));
  i32 deltaZ = static_cast<i32>(static_cast<i64>(std::sin(f64(heading)) * distance));
  *outX = static_cast<i32>(u32(x) - u32(deltaX));
  *outZ = static_cast<i32>(u32(z) - u32(deltaZ));
  i32 nextX = *outX, nextZ = *outZ;
  if (nextX < 0 || nextX >= cityXMicroUnits || nextZ < 0 || nextZ >= cityZMicroUnits)
    return false;
  u32 cellX = u32(nextX >> kCtMicroCoordinateShift), cellZ = u32(nextZ >> kCtMicroCoordinateShift);
  if (waterTerrain->IsCellBlocked(cellX, cellZ) || waterTerrain->CellWaterVertCount(cellX, cellZ) != 4)
    return false;
  nextX = static_cast<i32>(u32(*outX) - u32(deltaX));
  nextZ = static_cast<i32>(u32(*outZ) - u32(deltaZ));
  if (nextX < 0 || nextX >= cityXMicroUnits || nextZ < 0 || nextZ >= cityZMicroUnits)
    return false;
  cellX = u32(nextX >> kCtMicroCoordinateShift);
  cellZ = u32(nextZ >> kCtMicroCoordinateShift);
  return !waterTerrain->IsCellBlocked(cellX, cellZ) && waterTerrain->CellWaterVertCount(cellX, cellZ) == 4;
}

// Try all 32 directions from a random starting heading until a straight water route is found.
u8 NwBoatVehicle::FindAnyNeighboringWaterPoint(i32 distance, u8 *outOrientation, i32 *outX, i32 *outZ) {
  u8 first = static_cast<u8>(NwVehicleRandom.Bounded(32));
  *outOrientation = first;
  do {
    f32 heading = static_cast<f32>(f64(*outOrientation) * f64(0.19634955f));
    if (FindStraightWaterPoint(heading, distance, outX, outZ))
      return 1;
    if (++*outOrientation == 32)
      *outOrientation = 0;
  } while (*outOrientation != first);
  return 0;
}

// Search nearby headings in the original randomized order for a straight water route.
// Floating-point behavior near direction boundaries remains to be compared.
u8 NwBoatVehicle::FindAWaterPoint(f32 heading, i32 distance, i32 *outX, i32 *outZ, f32 *outHeading) {
  i32 first = NwVehicleRandom.Range(-4, 4), index = first;
  *outHeading = static_cast<f32>(f64(first) * f64(0.19634955f) + heading);
  while (index <= 4) {
    if (FindStraightWaterPoint(*outHeading, distance, outX, outZ)) {
      if (*outHeading < 0.0f)
        *outHeading = static_cast<f32>(f64(*outHeading) + f64(6.2831855f));
      return 1;
    }
    ++index;
    *outHeading = static_cast<f32>(f64(*outHeading) + f64(0.19634955f));
  }
  *outHeading = static_cast<f32>(f64(heading) - f64(0.19634955f) * 4.0);
  for (index = -4; index < first; ++index) {
    if (FindStraightWaterPoint(*outHeading, distance, outX, outZ)) {
      if (*outHeading < 0.0f)
        *outHeading = static_cast<f32>(f64(*outHeading) + f64(6.2831855f));
      return 1;
    }
    *outHeading = static_cast<f32>(f64(*outHeading) + f64(0.19634955f));
  }
  return 0;
}

// Start a boat routine by choosing a neighboring water point and scheduling movement to it.
i32 NwBoatVehicle::BoatRoutine() {
  i32 offset = stateStackTop;
  stateStackTop += 16;
  auto *state = new (stateStack + offset) NwBoatRoutineState;
  u8 facing;
  i32 pointX, pointZ;
  if (!FindAnyNeighboringWaterPoint(256, &facing, &pointX, &pointZ)) {
    stateStackTop -= 16;
    return 1;
  }
  SetOrientation(facing);
  state->heading = angle;
  state->distance = 256;
  state->targetDistance = NwVehicleRandom.Range(256, 8192);
  state->speed = static_cast<f32>(NwVehicleRandom.RandomDoubleRangeUniform(minimumSpeed, maximumSpeed));
  PushToActionStack(17);
  OnWaterMoveToPoint(pointX, pointZ, state->speed, true);
  return 2;
}

// Continue the boat's straight route, select another heading, or report that it hit land.
i32 NwBoatVehicle::BoatRoutineUpdateFn(u32, i32, bool cancel) {
  auto *state = reinterpret_cast<NwBoatRoutineState *>(stateStack + stateStackTop - 16);
  if (cancel) {
    stateStackTop -= 16;
    return 0;
  }
  i32 pointX, pointZ;
  if (state->distance < state->targetDistance &&
      FindStraightWaterPoint(state->heading, 256, &pointX, &pointZ)) {
    state->distance = static_cast<i32>(u32(state->distance) + 256);
    OnWaterMoveToPoint(pointX, pointZ, state->speed, true);
    return 2;
  }
  f32 nextHeading;
  if (FindAWaterPoint(angle, 256, &pointX, &pointZ, &nextHeading)) {
    state->heading = nextHeading;
    state->distance = 256;
    state->targetDistance = NwVehicleRandom.Range(256, 8192);
    state->speed = static_cast<f32>(NwVehicleRandom.RandomDoubleRangeUniform(minimumSpeed, maximumSpeed));
    OnWaterMoveToPoint(pointX, pointZ, state->speed, true);
    return 2;
  }
  stateStackTop -= 16;
  static_cast<NwBoatProducer *>(producer)->BoatHitLand(startX, startZ);
  return 1;
}

// Copy an inbound water route, initialize the ship's heading and insert it and its wake into the city.
u8 NwShipVehicle::MakeArrivingShip(ScPointXZ *points, u32 count) {
  route = new ScPointXZ[count];
  routeCount = count;
  for (u32 i = 0; i < count; ++i) {
    route[i].x = points[i].x << kCtMicroCoordinateShift;
    route[i].z = points[i].z << kCtMicroCoordinateShift;
  }
  routeCount = count;
  splineStep = 0;
  routeIndex = count - 1;
  controlPoints[0] = controlPoints[1] = controlPoints[2] = routeIndex;
  controlPoints[3] = routeIndex - 1;
  GetNextPointIn(&x, &z);
  u8 waterAltitude;
  waterTerrain->GetVertexAltitudeWater(
      u32(x >> kCtMicroCoordinateShift), u32(z >> kCtMicroCoordinateShift), &waterAltitude);
  altitude = i32(waterAltitude) << kCtMicroCoordinateShift;
  i32 deltaX = static_cast<i32>(route[routeIndex - 1].x - u32(x));
  i32 deltaZ = static_cast<i32>(route[routeIndex - 1].z - u32(z));
  f64 direction = (std::atan2(f64(deltaZ), f64(deltaX)) + f64(3.1415927f)) / f64(0.19634955f) + 0.5;
  u8 facing = static_cast<u8>(static_cast<i64>(direction));
  if (facing >= 32)
    facing -= 32;
  SetOrientation(facing);
  shipAction = 0;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  routeBoundary = u32((attributes->length >> kCtMicroCoordinateShift) + 6);
  ShipRoutine();
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 128, &sortId);
  if (wake) {
    CtCoord position{x, z, altitude};
    wake->SetLocation(position);
    wake->SetOrientation(orientation);
    animationManager->InsertOccupant(static_cast<ScOccupant *>(wake), 1, 0, true);
  }
  byte400 = 1;
  return 1;
}

// Copy the ship route, position it for departure and insert the ship and any wake into the city.
u8 NwShipVehicle::MakeParkedShip(ScPointXZ *points, u32 count) {
  route = new ScPointXZ[count];
  routeCount = count;
  for (u32 i = 0; i < count; ++i) {
    route[i].x = points[i].x << kCtMicroCoordinateShift;
    route[i].z = points[i].z << kCtMicroCoordinateShift;
  }
  routeCount = count;
  GetSetForOutBoundJourney();
  shipAction = 2;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  ShipRoutine();
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 128, &sortId);
  if (wake) {
    CtCoord position{x, z, altitude};
    wake->SetLocation(position);
    wake->SetOrientation(orientation);
    animationManager->InsertOccupant(static_cast<ScOccupant *>(wake), 1, 0, true);
  }
  byte400 = 0;
  return 1;
}

// Initialize the three original ship action callbacks.
u8 NwShipVehicle::StaticInit() {
  updateFunctions[0] = &NwShipVehicle::ShipRoutineUpdateFn;
  updateFunctions[1] = &NwShipVehicle::ApproachSeaportUpdateFn;
  updateFunctions[2] = &NwShipVehicle::MoveToNeighborUpdateFn;
  return 1;
}

// Clear the ship-specific action callback table.
u8 NwShipVehicle::StaticShutdown() {
  for (auto &function : updateFunctions)
    function = nullptr;
  return 1;
}

// Dispatch base vehicle actions and the three ship-specific action callbacks.
i32 NwShipVehicle::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  u32 action = actionStack[actionStackTop];
  if (action <= 16)
    return NwStreetVehicle::ExecuteCurrentUpdateFn(elapsed, status, cancel);
  return (this->*updateFunctions[action - 17])(elapsed, status, cancel);
}

i32 NwShipVehicle::ApproachSeaport() {
  PushToActionStack(18);
  return 2;
}

// Schedule travel toward the next neighbor and enable its wake state.
i32 NwShipVehicle::MoveToNeighbor() {
  PushToActionStack(19);
  byte400 = 1;
  return 2;
}

i32 NwShipVehicle::WaitAtPier() {
  IdleForDuration(static_cast<u32>(NwVehicleRandom.Range(kNwShipIdleMinimum, kNwShipIdleMaximum)));
  return 2;
}

i32 NwShipVehicle::WaitForTurnAround() {
  IdleForDuration(static_cast<u32>(NwVehicleRandom.Range(kNwShipIdleMinimum, kNwShipIdleMaximum)));
  return 2;
}

i32 NwShipVehicle::ShipRoutine() {
  PushToActionStack(17);
  if (shipAction == 0)
    ApproachSeaport();
  else if (shipAction == 2)
    WaitAtPier();
  return 2;
}

// Advance the ship between approach, turnaround, pier wait and outbound travel.
i32 NwShipVehicle::ShipRoutineUpdateFn(u32, i32 status, bool cancel) {
  if (cancel)
    return 0;
  if (status == 1)
    return 1;
  if (shipAction == 0) {
    byte400 = 0;
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
    shipAction = 1;
    WaitForTurnAround();
  } else if (shipAction == 1) {
    GetSetForOutBoundJourney();
    CtCoord position{x, z, altitude};
    ChangePosition(&position);
    shipAction = 2;
    WaitAtPier();
  } else if (shipAction == 2) {
    shipAction = 3;
    MoveToNeighbor();
  } else if (shipAction == 3) {
    return 0;
  }
  return 2;
}

// Follow the inbound water route, asking the ship producer to recompute it when a cell is blocked.
i32 NwShipVehicle::ApproachSeaportUpdateFn(u32, i32, bool cancel) {
  i32 pointX, pointZ;
  if (cancel || !GetNextPointIn(&pointX, &pointZ))
    return 0;
  if (waterTerrain->CellWaterVertCount(
          u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift)) == 4) {
    OnWaterMoveToPoint(pointX, pointZ, 0.00030000001f, true);
    return 2;
  }
  static_cast<NwShipProducer *>(producer)->ReEvaluateSeaportMapAndPath();
  return 1;
}

// Follow the outbound water route, asking the ship producer to recompute it when a cell is blocked.
i32 NwShipVehicle::MoveToNeighborUpdateFn(u32, i32, bool cancel) {
  i32 pointX, pointZ;
  if (cancel || !GetNextPointOut(&pointX, &pointZ))
    return 0;
  if (waterTerrain->CellWaterVertCount(
          u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift)) == 4) {
    OnWaterMoveToPoint(pointX, pointZ, 0.00030000001f, true);
    return 2;
  }
  static_cast<NwShipProducer *>(producer)->ReEvaluateSeaportMapAndPath();
  return 1;
}

// Evaluate the ship's current cubic route position using the four installed coefficient tables.
void NwShipVehicle::EvaluateBSplinePoint(i32 *outX, i32 *outZ) {
  f64 xValue = f64(static_cast<i32>(route[controlPoints[2]].x)) * NwShipSpline[2][splineStep] +
      f64(static_cast<i32>(route[controlPoints[0]].x)) * NwShipSpline[0][splineStep] +
      f64(static_cast<i32>(route[controlPoints[3]].x)) * NwShipSpline[3][splineStep] +
      f64(static_cast<i32>(route[controlPoints[1]].x)) * NwShipSpline[1][splineStep];
  *outX = static_cast<i32>(static_cast<i64>(xValue));
  f64 zValue = f64(static_cast<i32>(route[controlPoints[2]].z)) * NwShipSpline[2][splineStep] +
      f64(static_cast<i32>(route[controlPoints[0]].z)) * NwShipSpline[0][splineStep] +
      f64(static_cast<i32>(route[controlPoints[3]].z)) * NwShipSpline[3][splineStep] +
      f64(static_cast<i32>(route[controlPoints[1]].z)) * NwShipSpline[1][splineStep];
  *outZ = static_cast<i32>(static_cast<i64>(zValue));
}

// Advance the ship's inbound spline, stopping at its route boundary.
u8 NwShipVehicle::GetNextPointIn(i32 *outX, i32 *outZ) {
  if (splineStep >= 6) {
    splineStep = 0;
    if (routeIndex <= routeBoundary) {
      *outX = 0;
      *outZ = 0;
      return 0;
    }
    --routeIndex;
    for (i32 i = 0; i < 3; ++i)
      controlPoints[i] = controlPoints[i + 1];
    --controlPoints[3];
    if (controlPoints[3] < routeBoundary)
      controlPoints[3] = routeBoundary;
  }
  EvaluateBSplinePoint(outX, outZ);
  ++splineStep;
  return 1;
}

// Advance the ship's outbound spline, stopping after the final route point.
u8 NwShipVehicle::GetNextPointOut(i32 *outX, i32 *outZ) {
  if (splineStep >= 6) {
    splineStep = 0;
    if (++routeIndex >= routeCount) {
      *outX = 0;
      *outZ = 0;
      return 0;
    }
    for (i32 i = 0; i < 3; ++i)
      controlPoints[i] = controlPoints[i + 1];
    ++controlPoints[3];
    if (controlPoints[3] >= routeCount)
      controlPoints[3] = routeCount - 1;
  }
  EvaluateBSplinePoint(outX, outZ);
  ++splineStep;
  return 1;
}

// Initialize the outbound spline, water altitude and ship heading at its departure point.
void NwShipVehicle::GetSetForOutBoundJourney() {
  splineStep = 0;
  routeIndex = 6;
  controlPoints[0] = controlPoints[1] = controlPoints[2] = 6;
  controlPoints[3] = 7;
  GetNextPointOut(&x, &z);
  u8 waterAltitude;
  waterTerrain->GetVertexAltitudeWater(
      u32(x >> kCtMicroCoordinateShift), u32(z >> kCtMicroCoordinateShift), &waterAltitude);
  altitude = i32(waterAltitude) << kCtMicroCoordinateShift;
  i32 deltaX = static_cast<i32>(route[routeIndex + 1].x - u32(x));
  i32 deltaZ = static_cast<i32>(route[routeIndex + 1].z - u32(z));
  f64 direction = (std::atan2(f64(deltaZ), f64(deltaX)) + f64(3.1415927f)) / f64(0.19634955f) + 0.5;
  u8 facing = static_cast<u8>(static_cast<i64>(direction));
  if (facing >= 32)
    facing -= 32;
  SetOrientation(facing);
}

// Start a circular motion around a city point after leaving the vehicle's paths.
i32 NwStreetVehicle::CircularMotionInCity(i32 centerX, i32 centerZ, f32 targetAngle, f32 moveSpeed, f32 rise) {
  i32 offset = stateStackTop;
  stateStackTop += 36;
  auto *state = new (stateStack + offset) NwVehicleCityCircleState;
  LeaveAllPaths();
  state->centerX = centerX;
  state->centerZ = centerZ;
  i32 deltaX = static_cast<i32>(u32(x) - u32(centerX)), deltaZ = static_cast<i32>(u32(z) - u32(centerZ));
  state->radius = static_cast<f32>(std::sqrt(f64(deltaX) * deltaX + f64(deltaZ) * deltaZ));
  f64 startAngle = std::atan2(f64(deltaZ), f64(deltaX));
  state->angle = static_cast<f32>(startAngle);
  state->targetAngle = targetAngle;
  state->progress = 0.0f;
  state->verticalGradient = static_cast<f32>(f64(rise) / f64(6.2831855f));
  state->altitude = static_cast<f32>(altitude);
  f64 heading = startAngle + f64(4.712389f);
  while (heading > f64(6.2831855f))
    heading -= f64(6.2831855f);
  SetOrientation(static_cast<u8>(static_cast<i64>(heading / f64(0.19634955f) + 0.5) & 31));
  state->angularSpeed = moveSpeed / state->radius;
  pitch = 2;
  PushToActionStack(14);
  return 2;
}

f64 NwStreetVehicle::QueryAltitudeAndComputeYGradientPerUnitAngle(
    i32 centerX, i32 centerZ, f32 radius, f32 heading, i32 height, i32 margin) {
  f64 nextAngle = f64(heading) + 1.0;
  i32 pointX = static_cast<i32>(u32(centerX) + u32(static_cast<i64>(std::cos(nextAngle) * radius)));
  i32 pointZ = static_cast<i32>(u32(centerZ) + u32(static_cast<i64>(std::sin(nextAngle) * radius)));
  if (pointX < 0 || pointX >= cityXMicroUnits || pointZ < 0 || pointZ >= cityZMicroUnits)
    return 0.0;
  return static_cast<f64>(i32(u32(ComputeYForTerrainFollowing(pointX, pointZ, height, margin)) - u32(altitude)));
}

// Schedule circular flight and sample the terrain clearance one radian ahead.
i32 NwStreetVehicle::CircularMotion(i32 centerX, i32 centerZ, f32 targetAngle, f32 moveSpeed, i32 height, i32 margin) {
  i32 offset = stateStackTop;
  stateStackTop += 48;
  auto *state = new (stateStack + offset) NwVehicleCircleState;
  LeaveAllPaths();
  state->centerX = centerX;
  state->centerZ = centerZ;
  state->height = height;
  state->margin = margin;
  i32 dx = i32(u32(x) - u32(centerX)), dz = i32(u32(z) - u32(centerZ));
  state->radius = static_cast<f32>(std::sqrt(f64(dx) * dx + f64(dz) * dz));
  f64 heading = std::atan2(f64(dz), f64(dx));
  state->angle = static_cast<f32>(heading);
  state->targetAngle = targetAngle;
  state->progress = 0.0f;
  state->altitude = static_cast<f32>(altitude);
  heading += f64(4.712389f);
  while (heading > f64(6.2831855f))
    heading -= f64(6.2831855f);
  SetOrientation(static_cast<u8>(static_cast<i64>(heading / f64(0.19634955f) + 0.5) & 31));
  state->angularSpeed = moveSpeed / state->radius;
  pitch = 2;
  state->verticalGradient = static_cast<f32>(QueryAltitudeAndComputeYGradientPerUnitAngle(
      state->centerX, state->centerZ, state->radius, state->angle, state->height, state->margin));
  state->angleSinceQuery = 0.0f;
  PushToActionStack(13);
  return 2;
}

// Start the vehicle's spiral movement around a moving origin using its selected animation action.
i32 NwStreetVehicle::SpiralMotion(const ScVec3F &origin, const ScVec3F &velocity) {
  vehicleState |= 1;
  WipeActionStack();
  i32 offset = stateStackTop;
  stateStackTop += 60;
  auto *state = new (stateStack + offset) NwVehicleSpiralState;
  state->origin = origin;
  state->velocity = velocity;
  f64 dx = f64(x) - origin.x, dz = f64(z) - origin.z;
  state->radius = static_cast<f32>(std::sqrt(dx * dx + dz * dz));
  state->angle = static_cast<f32>(std::atan2(dz, dx));
  state->phase = 0;
  state->angularSpeed = 0.001256f / state->radius;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetARandomActionDescriptor();
  frame = static_cast<u8>(NwVehicleRandom.Bounded(actionDescriptor->frameCount));
  state->orientationElapsed = 0;
  PushToActionStack(12);
  return 2;
}

// Advance the spiral, then follow its ballistic descent until reaching terrain or leaving the city.
i32 NwStreetVehicle::SpiralMotionUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleSpiralState *>(stateStack + stateStackTop - 60);
  if (cancel) {
    stateStackTop -= 60;
    return 0;
  }
  state->orientationElapsed += elapsed;
  if (state->orientationElapsed > 20000) {
    SetOrientation(static_cast<u8>(NwVehicleRandom.Bounded(32)));
    pitch = static_cast<i8>(NwVehicleRandom.Bounded(5));
    state->orientationElapsed = 0;
  }
  CtCoord position;
  if (state->phase == 0) {
    f64 radius = f64(elapsed) * f64(0.000015f) + state->radius;
    f64 angularSpeed = f64(state->radius) / radius * state->angularSpeed;
    state->radius = static_cast<f32>(radius);
    state->angularSpeed = static_cast<f32>(angularSpeed);
    state->angle = static_cast<f32>(angularSpeed * f32(elapsed) + state->angle);
    state->origin.x = static_cast<f32>(f64(f32(elapsed)) * state->velocity.x + state->origin.x);
    state->origin.z = static_cast<f32>(f64(f32(elapsed)) * state->velocity.z + state->origin.z);
    f64 nextAltitude = f64(f32(elapsed)) * f64(0.001f) + state->origin.y;
    state->origin.y = static_cast<f32>(nextAltitude);
    position.altitude = static_cast<i32>(static_cast<i64>(nextAltitude));
    position.x = static_cast<i32>(static_cast<i64>(std::cos(f64(state->angle)) * state->radius + state->origin.x));
    position.z = static_cast<i32>(static_cast<i64>(std::sin(f64(state->angle)) * state->radius + state->origin.z));
    if (!InCityBoundsMicro(position.x, position.z)) {
      --vehicleState;
      stateStackTop -= 60;
      return 0;
    }
    if (state->radius > 250.0f) {
      state->phase = 1;
      state->position = {static_cast<f32>(x), static_cast<f32>(altitude), static_cast<f32>(z)};
      f64 tangent = f64(state->angularSpeed) * state->radius;
      state->velocity.x = static_cast<f32>(std::cos(f64(state->angle)) * tangent);
      state->velocity.z = static_cast<f32>(std::sin(f64(state->angle)) * tangent);
      state->velocity.y = 0.001f;
    }
  } else if (state->phase == 1) {
    f64 nextVerticalSpeed = f64(state->velocity.y) - f64(elapsed) * f64(7.68e-10f);
    state->velocity.y = static_cast<f32>(nextVerticalSpeed);
    f64 nextX = f64(elapsed) * state->velocity.x + state->position.x;
    state->position.x = static_cast<f32>(nextX);
    state->position.y = static_cast<f32>(f64(elapsed) * state->velocity.y + state->position.y);
    state->position.z = static_cast<f32>(f64(elapsed) * state->velocity.z + state->position.z);
    position.x = static_cast<i32>(static_cast<i64>(nextX));
    position.altitude = static_cast<i32>(static_cast<i64>(state->position.y));
    position.z = static_cast<i32>(static_cast<i64>(state->position.z));
    if (!InCityBoundsMicro(position.x, position.z)) {
      --vehicleState;
      stateStackTop -= 60;
      return 0;
    }
    ScOccupant *occupant;
    if (occupantManagers[occupantManagerIndex]->GetOccupantAtCell(
            u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &occupant)) {
      GeBounds bounds;
      occupant->GetBounds(&bounds);
      bool inside = position.altitude >= bounds.minimum.altitude && position.altitude <= bounds.maximum.altitude;
      occupant->Release();
      if (inside) {
        state->phase = 2;
        u8 ground = 0;
        dirtBag->GetVertexAltitude(
            u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &ground);
        state->ground = i32(u32(ground) << kCtMicroCoordinateShift);
      }
    }
  } else if (state->phase == 2) {
    f64 nextVerticalSpeed = f64(state->velocity.y) - f64(elapsed) * f64(7.68e-10f);
    state->velocity.y = static_cast<f32>(nextVerticalSpeed);
    f64 nextAltitude = f64(elapsed) * nextVerticalSpeed + state->position.y;
    state->position.y = static_cast<f32>(nextAltitude);
    position = {x, z, static_cast<i32>(static_cast<i64>(nextAltitude))};
    if (position.altitude <= state->ground) {
      --vehicleState;
      stateStackTop -= 60;
      return 0;
    }
  } else {
    return 2;
  }
  ChangePosition(&position);
  return 2;
}

// Advance circular flight, refreshing visibility, position and the terrain-clearance gradient.
i32 NwStreetVehicle::CircularMotionUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleCircleState *>(stateStack + stateStackTop - 48);
  if (cancel) {
    stateStackTop -= 48;
    return 0;
  }
  f64 rawStep = f64(elapsed) * state->angularSpeed;
  f32 step = static_cast<f32>(rawStep);
  f64 progress = rawStep + state->progress;
  state->progress = static_cast<f32>(progress);
  if (progress >= state->targetAngle) {
    stateStackTop -= 48;
    return 0;
  }
  f64 nextAngle = f64(step) + state->angle;
  state->angle = static_cast<f32>(nextAngle);
  if (nextAngle >= f64(6.2831855f))
    state->angle = static_cast<f32>(nextAngle - f64(6.2831855f));
  f64 nextAltitude = f64(step) * state->verticalGradient + state->altitude;
  state->altitude = static_cast<f32>(nextAltitude);
  CtCoord position;
  position.altitude = static_cast<i32>(static_cast<i64>(nextAltitude));
  position.x = i32(u32(state->centerX) + u32(static_cast<i64>(std::cos(f64(state->angle)) * state->radius)));
  position.z = i32(u32(state->centerZ) + u32(static_cast<i64>(std::sin(f64(state->angle)) * state->radius)));
  SetVisibility(position.x >= 0 && position.x < cityXMicroUnits && position.z >= 0 && position.z < cityZMicroUnits);
  i64 heading = static_cast<i64>((f64(state->angle) + f64(4.712389f)) / f64(0.19634955f) + 0.5);
  SetOrientation(static_cast<u8>(heading % 32));
  if (visible)
    ChangePosition(&position);
  f64 sinceQuery = f64(step) + state->angleSinceQuery;
  state->angleSinceQuery = static_cast<f32>(sinceQuery);
  if (sinceQuery >= 1.0) {
    state->angleSinceQuery = 0.0f;
    state->verticalGradient = static_cast<f32>(QueryAltitudeAndComputeYGradientPerUnitAngle(
        state->centerX, state->centerZ, state->radius, state->angle, state->height, state->margin));
  }
  return 2;
}

// Advance city circular motion, updating visibility and position while the angle remains below its target.
i32 NwStreetVehicle::CircularMotionInCityUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleCityCircleState *>(stateStack + stateStackTop - 36);
  if (cancel) {
    stateStackTop -= 36;
    return 0;
  }
  f64 step = f64(elapsed) * state->angularSpeed, progress = step + state->progress;
  state->progress = static_cast<f32>(progress);
  if (progress >= state->targetAngle) {
    stateStackTop -= 36;
    return 0;
  }
  f64 nextAngle = step + state->angle;
  state->angle = static_cast<f32>(nextAngle);
  if (nextAngle >= f64(6.2831855f))
    state->angle = static_cast<f32>(nextAngle - f64(6.2831855f));
  f64 nextAltitude = step * state->verticalGradient + state->altitude;
  state->altitude = static_cast<f32>(nextAltitude);
  CtCoord position;
  position.altitude = static_cast<i32>(static_cast<i64>(nextAltitude));
  position.x = static_cast<i32>(u32(state->centerX) +
      u32(static_cast<i64>(std::cos(f64(state->angle)) * state->radius)));
  position.z = static_cast<i32>(u32(state->centerZ) +
      u32(static_cast<i64>(std::sin(f64(state->angle)) * state->radius)));
  SetVisibility(position.x >= 0 && position.x < cityXMicroUnits && position.z >= 0 && position.z < cityZMicroUnits);
  i64 heading = static_cast<i64>((f64(state->angle) + f64(4.712389f)) / f64(0.19634955f) + 0.5);
  SetOrientation(static_cast<u8>(heading % 32));
  if (visible)
    ChangePosition(&position);
  return 2;
}

// Sample terrain ahead and derive horizontal and vertical speed components for the climb.
void NwStreetVehicle::QueryAltitudeAndComputeSpeedComponents(f32 directionX, f32 directionZ, i32 height, i32 margin,
    f32 *horizontalSpeed, f32 *verticalSpeed) {
  i32 pointX = static_cast<i32>(u32(x) + u32(static_cast<i64>(f64(directionX) * 256.0)));
  i32 pointZ = static_cast<i32>(u32(z) + u32(static_cast<i64>(f64(directionZ) * 256.0)));
  if (pointX < 0 || pointX >= cityXMicroUnits || pointZ < 0 || pointZ >= cityZMicroUnits) {
    *horizontalSpeed = 1.0f;
    *verticalSpeed = 0.0f;
    return;
  }
  i32 target = ComputeYForTerrainFollowing(pointX, pointZ, height, margin);
  i32 delta = static_cast<i32>(u32(target) - u32(altitude));
  f64 scaled = f64(delta) * f64(0.204125f);
  f64 length = std::sqrt(65536.0 + scaled * scaled);
  *horizontalSpeed = static_cast<f32>(256.0 / length);
  *verticalSpeed = static_cast<f32>(scaled / length * f64(4.8989592f));
}

// Schedule flight toward a point while sampling clearance over the terrain and occupants.
i32 NwStreetVehicle::FollowTerrainMoveToPoint(i32 pointX, i32 pointZ, i32 height, i32 margin, f32 moveSpeed) {
  speed = moveSpeed;
  i32 offset = stateStackTop;
  stateStackTop += 56;
  auto *state = new (stateStack + offset) NwVehicleTerrainMoveState;
  state->height = height;
  state->margin = margin;
  state->endX = pointX;
  state->endZ = pointZ;
  if (pointX < 0 || pointX >= cityXMicroUnits || pointZ < 0 || pointZ >= cityZMicroUnits) {
    if (state->endX < 0)
      state->endX = 0;
    if (state->endX >= cityXMicroUnits)
      state->endX = cityXMicroUnits - 1;
    if (state->endZ < 0)
      state->endZ = 0;
    if (state->endZ > cityZMicroUnits)
      state->endZ = cityXMicroUnits - 1;
  }
  state->startX = x;
  state->startZ = z;
  state->altitude = static_cast<f32>(altitude);
  i32 deltaX = static_cast<i32>(u32(pointX) - u32(x)), deltaZ = static_cast<i32>(u32(pointZ) - u32(z));
  f32 length = static_cast<f32>(std::sqrt(f64(deltaX) * deltaX + f64(deltaZ) * deltaZ));
  state->length = length;
  state->progress = 0.0f;
  f64 directionX = f64(deltaX) / length, directionZ = f64(deltaZ) / length;
  state->directionX = static_cast<f32>(directionX);
  state->directionZ = static_cast<f32>(directionZ);
  f64 orientationValue = (std::atan2(directionZ, directionX) + f64(3.1415927f)) / f64(0.19634955f) + 0.5;
  u8 nextOrientation = 0;
  if (std::isfinite(orientationValue) && orientationValue < 0x1p63 && orientationValue >= -0x1p63)
    nextOrientation = static_cast<u8>(static_cast<i64>(orientationValue));
  SetOrientation(nextOrientation);
  QueryAltitudeAndComputeSpeedComponents(state->directionX, state->directionZ, height, margin,
      &state->horizontalSpeed, &state->verticalSpeed);
  state->horizontalSpeed *= speed;
  state->distanceSinceQuery = 0.0f;
  state->verticalSpeed *= speed;
  PushToActionStack(7);
  return 2;
}

// Advance a terrain-following segment and resample its slope after each 256 units.
i32 NwStreetVehicle::FollowTerrainMoveToPointUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleTerrainMoveState *>(stateStack + stateStackTop - 56);
  if (cancel) {
    stateStackTop -= 56;
    return 0;
  }
  f32 moved = static_cast<f32>(f64(elapsed) * state->horizontalSpeed), oldProgress = state->progress;
  f64 verticalMove = f64(elapsed) * state->verticalSpeed;
  f64 progress = f64(oldProgress) + moved;
  state->progress = static_cast<f32>(progress);
  bool finished = progress >= state->length;
  CtCoord position;
  if (!finished) {
    position.x = static_cast<i32>(u32(state->startX) +
        u32(static_cast<i64>(f64(state->progress) * state->directionX)));
    position.z = static_cast<i32>(u32(state->startZ) +
        u32(static_cast<i64>(f64(state->progress) * state->directionZ)));
  } else {
    position.x = state->endX;
    position.z = state->endZ;
    verticalMove *= (f64(state->length) - oldProgress) / moved;
  }
  f64 nextAltitude = f64(state->altitude) + verticalMove;
  state->altitude = static_cast<f32>(nextAltitude);
  position.altitude = static_cast<i32>(static_cast<i64>(nextAltitude));
  ChangePosition(&position);
  if (finished) {
    stateStackTop -= 56;
    return 0;
  }
  f64 distance = f64(state->distanceSinceQuery) + moved;
  state->distanceSinceQuery = static_cast<f32>(distance);
  if (distance >= 256.0) {
    QueryAltitudeAndComputeSpeedComponents(state->directionX, state->directionZ, state->height, state->margin,
        &state->horizontalSpeed, &state->verticalSpeed);
    state->horizontalSpeed *= speed;
    state->verticalSpeed *= speed;
    state->distanceSinceQuery = 0.0f;
  }
  return 2;
}

// Schedule a water-level movement segment and record its target heading.
i32 NwStreetVehicle::OnWaterMoveToPoint(i32 pointX, i32 pointZ, f32 moveSpeed, bool reverse) {
  speed = moveSpeed;
  i32 offset = stateStackTop;
  stateStackTop += 52;
  auto *state = new (stateStack + offset) NwVehicleWaterMoveState;
  state->endX = pointX;
  state->endZ = pointZ;
  u8 waterAltitude;
  dirtBag->GetVertexAltitude(static_cast<u32>(pointX >> kCtMicroCoordinateShift),
      static_cast<u32>(pointZ >> kCtMicroCoordinateShift), &waterAltitude);
  state->endAltitude = i32(waterAltitude) << kCtMicroCoordinateShift;
  state->startX = x;
  state->startAltitude = altitude;
  state->startZ = z;
  i32 deltaX = static_cast<i32>(u32(pointX) - u32(x));
  i32 deltaZ = static_cast<i32>(u32(pointZ) - u32(z));
  f32 deltaAltitude = static_cast<f32>(static_cast<i32>(u32(state->endAltitude) - u32(altitude)));
  f32 length = static_cast<f32>(std::sqrt(f64(deltaX) * deltaX + f64(deltaZ) * deltaZ));
  state->length = length;
  if (length == 0.0f) {
    stateStackTop -= 52;
    return 0;
  }
  state->progress = 0.0f;
  f64 directionX = f64(deltaX) / length, directionZ = f64(deltaZ) / length;
  state->directionX = static_cast<f32>(directionX);
  state->directionAltitude = static_cast<f32>(f64(deltaAltitude) / length);
  state->directionZ = static_cast<f32>(directionZ);
  f64 targetAngle = std::atan2(directionZ, directionX);
  state->angle = static_cast<f32>(reverse ? targetAngle + f64(3.1415927f) : targetAngle);
  PushToActionStack(8);
  return 2;
}

// Advance a water movement segment, turning the vehicle toward its target heading.
i32 NwStreetVehicle::OnWaterMoveToPointUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleWaterMoveState *>(stateStack + stateStackTop - 52);
  if (cancel) {
    stateStackTop -= 52;
    return 0;
  }
  f32 moved = static_cast<f32>(f64(elapsed) * speed), oldProgress = state->progress;
  f64 progress = f64(moved) + oldProgress;
  state->progress = static_cast<f32>(progress);
  bool finished = progress >= state->length;
  CtCoord position;
  if (!finished) {
    f32 storedProgress = state->progress;
    position.x = static_cast<i32>(u32(state->startX) + u32(static_cast<i64>(f64(storedProgress) * state->directionX)));
    position.z = static_cast<i32>(u32(state->startZ) + u32(static_cast<i64>(f64(storedProgress) * state->directionZ)));
    position.altitude = static_cast<i32>(u32(state->startAltitude) +
        u32(static_cast<i64>(f64(storedProgress) * state->directionAltitude)));
  } else {
    position = {state->endX, state->endZ, state->endAltitude};
    moved = static_cast<f32>(f64(state->length) - oldProgress);
  }
  ChangePosition(&position);
  f64 turn = f64(state->angle) - angle;
  if (turn > f64(3.1415927f))
    turn -= f64(6.2831855f);
  else if (turn < -f64(3.1415927f))
    turn += f64(6.2831855f);
  f64 newAngle = turn * f64(0.89999998f) * moved / attributes->length + angle;
  angle = static_cast<f32>(newAngle);
  f64 orientationValue = newAngle / f64(0.19634955f) + 0.5;
  u8 newOrientation = 0;
  if (std::isfinite(orientationValue) && orientationValue < 0x1p63 && orientationValue >= -0x1p63)
    newOrientation = static_cast<u8>(static_cast<i64>(orientationValue) & 31);
  if (newOrientation != orientation)
    SetOrientation(newOrientation);
  if (finished) {
    stateStackTop -= 52;
    return 0;
  }
  return 2;
}

// Schedule movement toward a point, consuming any distance carried from the previous segment.
i32 NwStreetVehicle::MoveToPoint(i32 pointX, i32 pointAltitude, i32 pointZ, i32 distance) {
  i32 offset = stateStackTop;
  stateStackTop += 52;
  auto *state = new (stateStack + offset) NwVehicleMoveState;
  state->pathDistance = distance;
  pathLength = distance;
  state->endAltitude = pointAltitude;
  state->endX = pointX;
  state->endZ = pointZ;
  state->startX = x;
  state->startAltitude = altitude;
  state->startZ = z;
  i32 deltaX = static_cast<i32>(u32(pointX) - u32(x));
  i32 deltaAltitude = static_cast<i32>(u32(pointAltitude) - u32(altitude));
  i32 deltaZ = static_cast<i32>(u32(pointZ) - u32(z));
  i32 scaledAltitude = static_cast<i32>(f64(deltaAltitude) * f64(0.204125f));
  u32 squared = u32(deltaX) * u32(deltaX) + u32(scaledAltitude) * u32(scaledAltitude) + u32(deltaZ) * u32(deltaZ);
  i32 signedSquared = static_cast<i32>(squared);
  // Negative wrapped sums reach an invalid square root in the original.
  state->length = signedSquared < 0 ? 0 : static_cast<i32>(std::sqrt(f64(signedSquared)));
  f64 length = state->length;
  f32 directionX = static_cast<f32>(f64(deltaX) / length);
  state->directionX = directionX;
  state->directionAltitude = static_cast<f32>(f64(scaledAltitude) / length);
  f64 directionZ = f64(deltaZ) / length;
  state->directionZ = static_cast<f32>(directionZ);
  state->progress = 0.0f;
  state->angle = static_cast<f32>(std::atan2(directionZ, directionX) + f64(3.1415927f));
  f32 carry = movementCarry;
  if (carry > 0.0f && MoveToPointMoveDist(state, carry) == 0) {
    stateStackTop -= 52;
    return 0;
  }
  PushToActionStack(2);
  return 2;
}

// Schedule follower movement toward a point, consuming carried distance when present.
i32 NwStreetVehicle::FollowerMoveToPoint(i32 pointX, i32 pointAltitude, i32 pointZ, i32 distance) {
  i32 offset = stateStackTop;
  stateStackTop += 52;
  auto *state = new (stateStack + offset) NwVehicleFollowerMoveState;
  state->pathDistance = distance;
  pathLength = distance;
  state->endAltitude = pointAltitude;
  state->endX = pointX;
  state->endZ = pointZ;
  state->startX = x;
  state->startAltitude = altitude;
  state->startZ = z;
  i32 deltaX = static_cast<i32>(u32(pointX) - u32(x));
  i32 deltaAltitude = static_cast<i32>(u32(pointAltitude) - u32(altitude));
  i32 deltaZ = static_cast<i32>(u32(pointZ) - u32(z));
  f32 scaledAltitude = static_cast<f32>(f64(deltaAltitude) * f64(0.204125f));
  f32 storedZ = static_cast<f32>(deltaZ);
  f64 squared = f64(deltaZ) * storedZ + f64(scaledAltitude) * scaledAltitude + f64(deltaX) * deltaX;
  state->length = static_cast<i32>(u32(static_cast<i64>(std::sqrt(squared))));
  f32 length = static_cast<f32>(state->length);
  f64 directionX = f64(deltaX) / length;
  f64 directionZ = f64(storedZ) / length;
  state->directionX = static_cast<f32>(directionX);
  state->directionAltitude = static_cast<f32>(f64(scaledAltitude) / length);
  state->directionZ = static_cast<f32>(directionZ);
  state->progress = 0.0f;
  state->angle = static_cast<f32>(std::atan2(directionZ, directionX) + f64(3.1415927f));
  f32 carry = movementCarry;
  if (carry > 0.0f && FollowerMoveToPointMoveDist(state, carry) == 0) {
    stateStackTop -= 52;
    return 0;
  }
  PushToActionStack(6);
  return 2;
}

// Advance ordinary point movement for the elapsed time or discard its state on cancellation.
i32 NwStreetVehicle::MoveToPointUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleMoveState *>(stateStack + stateStackTop - 52);
  if (cancel) {
    stateStackTop -= 52;
    return 0;
  }
  movementDistance = 0.0f;
  f32 distance = static_cast<f32>(f64(elapsed) * speed);
  i32 status = MoveToPointMoveDist(state, distance);
  if (status != 2)
    stateStackTop -= 52;
  return status;
}

i32 NwStreetVehicle::FollowerMoveToPointUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwVehicleFollowerMoveState *>(stateStack + stateStackTop - 52);
  if (cancel) {
    stateStackTop -= 52;
    return 0;
  }
  movementDistance = leader ? leader->LastMovementDist() : static_cast<f32>(f64(elapsed) * speed);
  i32 status = FollowerMoveToPointMoveDist(state, movementDistance);
  if (status == 0)
    stateStackTop -= 52;
  return status;
}

// Start traversing the current path and schedule movement to its next distinct point.
i32 NwStreetVehicle::MoveAlongPath() {
  speed = paths[0]->Speed();
  PushToActionStack(1);
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  paths[0]->GetNextPoint(&position.x, &position.altitude, &position.z);
  static_cast<NwVehicle *>(this)->ChangePosition(&position);
  pathLength = paths[0]->Length();
  for (;;) {
    i32 distance = 0, pointX, pointAltitude, pointZ;
    paths[0]->GetDistToEndFromCurrentPoint(&distance);
    if (!paths[0]->GetNextPoint(&pointX, &pointAltitude, &pointZ)) {
      --actionStackTop;
      return 0;
    }
    if (MoveToPoint(pointX, pointAltitude, pointZ, distance) != 0)
      return 2;
  }
}

i32 NwStreetVehicle::FollowerMoveAlongPath() {
  speed = paths[0]->Speed();
  PushToActionStack(5);
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  paths[0]->GetNextPoint(&position.x, &position.altitude, &position.z);
  static_cast<NwVehicle *>(this)->ChangePosition(&position);
  pathLength = paths[0]->Length();
  for (;;) {
    i32 distance = 0, pointX, pointAltitude, pointZ;
    paths[0]->GetDistToEndFromCurrentPoint(&distance);
    if (!paths[0]->GetNextPoint(&pointX, &pointAltitude, &pointZ))
      return 0;
    if (FollowerMoveToPoint(pointX, pointAltitude, pointZ, distance) != 0)
      return 2;
  }
}

i32 NwStreetVehicle::ComputeYForTerrainFollowing(i32 pointX, i32 pointZ, i32 height, i32 margin) {
  i32 cellX = pointX >> kCtMicroCoordinateShift, cellZ = pointZ >> kCtMicroCoordinateShift;
  u8 ground = 0;
  dirtBag->GetVertexAltitude(static_cast<u32>(cellX), static_cast<u32>(cellZ), &ground);
  i32 groundHeight = static_cast<i32>(u32(ground) << kCtMicroCoordinateShift);
  ScOccupant *occupant;
  if (!occupantManagers[0]->GetOccupantAtCell(static_cast<u32>(cellX), static_cast<u32>(cellZ), &occupant))
    return static_cast<i32>(u32(height) + u32(groundHeight));
  i32 clearance = static_cast<i32>(u32(margin) + occupant->SizeInAnimCellsY());
  i32 result = static_cast<i32>(u32(clearance > height ? clearance : height) + u32(groundHeight));
  occupant->Release();
  return result;
}

NwHelicopter::NwHelicopter() {
  byte44 = 0;
  normalRoutine = 1;
  geometryFlags = kNwAirVehicleGeometry;
}

u8 NwHelicopter::StaticInit() {
  updateFunctions[0] = &NwHelicopter::NormalHelicopterRoutineUpdateFn;
  updateFunctions[1] = &NwHelicopter::DisasterFightingRoutineUpdateFn;
  return 1;
}

u8 NwHelicopter::StaticShutdown() {
  updateFunctions[0] = nullptr;
  updateFunctions[1] = nullptr;
  return 1;
}

// Dispatch helicopter actions after the base vehicle's seventeen actions.
i32 NwHelicopter::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  u32 action = actionStack[actionStackTop];
  if (action <= 16)
    return NwStreetVehicle::ExecuteCurrentUpdateFn(elapsed, status, cancel);
  return (this->*updateFunctions[action - 17])(elapsed, status, cancel);
}

u8 NwHelicopter::MakeNormalRoutineHelicopter() {
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  NormalHelicopterRoutine();
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

u8 NwHelicopter::MakeDisasterFightingHelicopter(i32 startX, i32 startZ, i32 targetX, i32 targetZ) {
  normalRoutine = 0;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  DisasterFightingRoutine(u32(startX), u32(startZ), u32(targetX), u32(targetZ));
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

u8 NwHelicopter::IsNormalRoutineHelicopter() {
  return normalRoutine;
}

void NwHelicopter::EndDispatchMission() {
  dispatchEnded = 1;
}

i32 NwHelicopter::NormalHelicopterRoutine() {
  stateStack[stateStackTop++] = 0;
  PushToActionStack(17);
  u8 side = static_cast<u8>(NwVehicleRandom.Bounded(4));
  GetAPointOnTheEdgeOfTheCity(side, &x, &z);
  altitude = ComputeYForTerrainFollowing(x, z, 5120, 1024);
  i32 targetX = NwVehicleRandom.Range(2560, cityXMicroUnits - 2560);
  i32 targetZ = NwVehicleRandom.Range(2560, cityZMicroUnits - 2560);
  FollowTerrainMoveToPoint(targetX, targetZ, 5120, 1024, 0.00053349999f);
  return 2;
}

// Continue the normal flight, selecting local movement or an exit from the city.
i32 NwHelicopter::NormalHelicopterRoutineUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 1;
  if (cancel || stateStack[offset]) {
    --stateStackTop;
    return 0;
  }
  if (NwVehicleRandom.Bounded(5) == 0) {
    MoveToNeighbor();
    stateStack[offset] = 1;
    return 2;
  }
  if (x >= 0 && x < cityXMicroUnits && z >= 0 && z < cityZMicroUnits) {
    if (NwVehicleRandom.Bounded(2))
      MoveCircular();
    else
      MoveStraight();
    return 2;
  }
  --stateStackTop;
  return 1;
}

// Initialize a disaster mission and fly toward an offset near the target cell.
i32 NwHelicopter::DisasterFightingRoutine(u32 startX, u32 startZ, u32 targetX, u32 targetZ) {
  i32 offset = stateStackTop;
  stateStackTop += 20;
  i32 stage = 0;
  i32 startMicroX = static_cast<i32>(startX << kCtMicroCoordinateShift),
      startMicroZ = static_cast<i32>(startZ << kCtMicroCoordinateShift);
  i32 targetMicroX = static_cast<i32>(targetX << kCtMicroCoordinateShift),
      targetMicroZ = static_cast<i32>(targetZ << kCtMicroCoordinateShift);
  std::memcpy(stateStack + offset + 16, &stage, sizeof(stage));
  std::memcpy(stateStack + offset, &startMicroX, sizeof(startMicroX));
  std::memcpy(stateStack + offset + 4, &startMicroZ, sizeof(startMicroZ));
  std::memcpy(stateStack + offset + 8, &targetMicroX, sizeof(targetMicroX));
  std::memcpy(stateStack + offset + 12, &targetMicroZ, sizeof(targetMicroZ));
  dispatchEnded = 0;
  PushToActionStack(18);
  x = startMicroX;
  z = startMicroZ;
  altitude = ComputeYForTerrainFollowing(x, z, 5120, 1024);
  i32 radius = NwVehicleRandom.Range(768, 1280);
  std::memcpy(&startMicroX, stateStack + offset, sizeof(startMicroX));
  std::memcpy(&startMicroZ, stateStack + offset + 4, sizeof(startMicroZ));
  std::memcpy(&targetMicroX, stateStack + offset + 8, sizeof(targetMicroX));
  std::memcpy(&targetMicroZ, stateStack + offset + 12, sizeof(targetMicroZ));
  u32 deltaX = u32(targetMicroX) - u32(startMicroX), deltaZ = u32(targetMicroZ) - u32(startMicroZ);
  f64 length = std::sqrt(f64(deltaX) * deltaX + f64(deltaZ) * deltaZ);
  f64 scale = length ? f64(radius) / length : 0.0;
  i32 offsetX = static_cast<i32>(scale * f32(deltaX)), offsetZ = static_cast<i32>(scale * f32(deltaZ));
  i32 destinationX = static_cast<i32>(u32(targetMicroX) - u32(offsetX));
  i32 destinationZ = static_cast<i32>(u32(targetMicroZ) - u32(offsetZ));
  if (destinationX < 0 || destinationX >= cityXMicroUnits || destinationZ < 0 || destinationZ >= cityZMicroUnits) {
    offsetX = static_cast<i32>(-scale * f32(deltaX));
    offsetZ = static_cast<i32>(-scale * f32(deltaZ));
    destinationX = static_cast<i32>(u32(targetMicroX) - u32(offsetX));
    destinationZ = static_cast<i32>(u32(targetMicroZ) - u32(offsetZ));
  }
  FollowTerrainMoveToPoint(destinationX, destinationZ, 5120, 1024, 0.00053349999f);
  return 2;
}

// Continue circling at the mission target or return to the entry point when dispatched home.
// Cancellation retires the saved frame without reading its payload.
i32 NwHelicopter::DisasterFightingRoutineUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 20, stage = 0;
  if (!cancel)
    std::memcpy(&stage, stateStack + offset + 16, sizeof(stage));
  if (cancel || stage == 2) {
    stateStackTop -= 20;
    return 0;
  }
  if (dispatchEnded) {
    if (x < 0 || x >= cityXMicroUnits || z < 0 || z >= cityZMicroUnits) {
      stateStackTop -= 20;
      return 0;
    }
    stage = 2;
    std::memcpy(stateStack + offset + 16, &stage, sizeof(stage));
    i32 targetX, targetZ;
    std::memcpy(&targetX, stateStack + offset, sizeof(targetX));
    std::memcpy(&targetZ, stateStack + offset + 4, sizeof(targetZ));
    FollowTerrainMoveToPoint(targetX, targetZ, 5120, 1024, 0.00053349999f);
  } else {
    stage = 1;
    std::memcpy(stateStack + offset + 16, &stage, sizeof(stage));
    i32 targetX, targetZ;
    std::memcpy(&targetX, stateStack + offset + 8, sizeof(targetX));
    std::memcpy(&targetZ, stateStack + offset + 12, sizeof(targetZ));
    CircularMotion(targetX, targetZ, 6.2831855f, 0.00053349999f, 5120, 1024);
  }
  return 2;
}

// Select a nearby center toward the city interior and begin a terrain-following circle.
i32 NwHelicopter::MoveCircular() {
  LeaveAllPaths();
  i32 lowerX = x <= static_cast<i32>(cityMidpointX) ? x + 512 : x - 2048;
  i32 upperX = x <= static_cast<i32>(cityMidpointX) ? x + 2048 : x - 512;
  i32 targetX = NwVehicleRandom.Range(lowerX, upperX);
  i32 lowerZ = z <= static_cast<i32>(cityMidpointZ) ? z + 512 : z - 2048;
  i32 upperZ = z <= static_cast<i32>(cityMidpointZ) ? z + 2048 : z - 512;
  i32 targetZ = NwVehicleRandom.Range(lowerZ, upperZ);
  f32 turnAngle = static_cast<f32>(NwVehicleRandom.RandomDoubleRangeUniform(3.1415927f, 18.849556f));
  CircularMotion(targetX, targetZ, turnAngle, 0.00053349999f, 5120, 1024);
  return 2;
}

i32 NwHelicopter::MoveStraight() {
  i32 targetX = NwVehicleRandom.Range(1024, cityXMicroUnits - 1024);
  i32 targetZ = NwVehicleRandom.Range(1024, cityZMicroUnits - 1024);
  FollowTerrainMoveToPoint(targetX, targetZ, 5120, 1024, 0.00053349999f);
  return 2;
}

i32 NwHelicopter::MoveToNeighbor() {
  u8 side = static_cast<u8>((orientation + 4) / 8);
  if (side >= 4)
    side = 0;
  i32 targetX, targetZ;
  GetAPointOnTheEdgeOfTheCity(side, &targetX, &targetZ);
  FollowTerrainMoveToPoint(targetX, targetZ, 5120, 1024, 0.00053349999f);
  return 2;
}

// Cache the city midpoint and retain its terrain service for helicopter routes.
u8 NwHelicopter::CitySpecificStaticInit(CtCity *city) {
  cityMidpointX = city->AnimCellCountX() >> 1;
  cityMidpointZ = city->AnimCellCountZ() >> 1;
  dirtBag = ScAddRefNotNull(city->DirtBag());
  return 1;
}

u8 NwHelicopter::CitySpecificStaticShutdown() {
  ScRelease(&dirtBag);
  return 1;
}

void NwHelicopter::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

// Select a random point along the requested edge of the city.
void NwHelicopter::GetAPointOnTheEdgeOfTheCity(u8 side, i32 *pointX, i32 *pointZ) {
  switch (side) {
    case 0:
      *pointX = 5;
      *pointZ = NwVehicleRandom.Range(5, cityZMicroUnits - 5);
      break;
    case 1:
      *pointX = NwVehicleRandom.Range(5, cityXMicroUnits - 5);
      *pointZ = 5;
      break;
    case 2:
      *pointX = cityXMicroUnits - 5;
      *pointZ = NwVehicleRandom.Range(5, cityZMicroUnits - 5);
      break;
    case 3:
      *pointX = NwVehicleRandom.Range(5, cityXMicroUnits - 5);
      *pointZ = cityZMicroUnits - 5;
      break;
  }
}

u8 NwHelicopter::IsAirVehicle() {
  return 1;
}

// Initialize the rail vehicle's layer and rail-specific geometry flags.
// Linux identifies the original constructor and its base-field writes.
NwRailVehicle::NwRailVehicle() {
  occupantManagerIndex = 1;
  byte44 = 0;
  geometryFlags = kNwRailVehicleGeometry;
}

// Initialize the subway vehicle's layer and subway-specific geometry flags.
// Linux identifies the original constructor and its base-field writes.
NwSubwayVehicle::NwSubwayVehicle() {
  occupantManagerIndex = 2;
  byte44 = 0;
  geometryFlags = kNwSubwayVehicleGeometry;
}

// Place a rail vehicle on an oriented internal transit edge and schedule its appearance.
u8 NwRailVehicle::PlaceOnTransitTile(ScOccupant *occupant, u8 direction) {
  NwConnection *connection;
  if (!occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection)))
    return 0;
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  NwTrafficAttrib *blockAttributes = block->GetStrtSimAttrib();
  NwVehicleGraphNode *node = nullptr;
  NwVehicleGraphEdge *edge = nullptr;
  blockAttributes->vehicleGraph->SelectANodeAndInternalEdgeOfOrientation(&node, &edge, occupantManagerIndex, direction);
  if (!node || !edge) {
    connection->Release();
    return 0;
  }
  u8 cellAltitude = GetAltitude(occupant, blockAttributes);
  NwVehiclePath *path = BuildPath(block, cellX, cellAltitude, cellZ, edge);
  connection->Release();
  path->GetStartPoint(&x, &altitude, &z);
  SetOrientation(edge->startOrientation);
  speed = edge->timeScale;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  if (attributes->value0) {
    boxX->Event(kAuEventPlayEffect, static_cast<u32>(attributes->value0), reinterpret_cast<uptr>(this), u32(x), u32(z));
    i32 siren = DecideSirenState();
    boxX->Event(kAuEventEffectSlot56, static_cast<u32>(attributes->value0), reinterpret_cast<uptr>(this),
        u32(siren), 0);
  }
  remainingDistance = static_cast<f32>(i32(u32(attributes->length) + u32(path->Length())));
  visible = 0;
  if (leader) {
    path->Release();
    path = nullptr;
  }
  AppearOnPath(path);
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

// Place a subway vehicle on an oriented internal transit edge and schedule its appearance.
// The Windows compiler shares this implementation with rail placement.
u8 NwSubwayVehicle::PlaceOnTransitTile(ScOccupant *occupant, u8 direction) {
  NwConnection *connection;
  if (!occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection)))
    return 0;
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  NwTrafficAttrib *blockAttributes = block->GetStrtSimAttrib();
  NwVehicleGraphNode *node = nullptr;
  NwVehicleGraphEdge *edge = nullptr;
  blockAttributes->vehicleGraph->SelectANodeAndInternalEdgeOfOrientation(&node, &edge, occupantManagerIndex, direction);
  if (!node || !edge) {
    connection->Release();
    return 0;
  }
  u8 cellAltitude = GetAltitude(occupant, blockAttributes);
  NwVehiclePath *path = BuildPath(block, cellX, cellAltitude, cellZ, edge);
  connection->Release();
  path->GetStartPoint(&x, &altitude, &z);
  SetOrientation(edge->startOrientation);
  speed = edge->timeScale;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  if (attributes->value0) {
    boxX->Event(kAuEventPlayEffect, static_cast<u32>(attributes->value0), reinterpret_cast<uptr>(this), u32(x), u32(z));
    i32 siren = DecideSirenState();
    boxX->Event(kAuEventEffectSlot56, static_cast<u32>(attributes->value0), reinterpret_cast<uptr>(this),
        u32(siren), 0);
  }
  remainingDistance = static_cast<f32>(i32(u32(attributes->length) + u32(path->Length())));
  visible = 0;
  if (leader) {
    path->Release();
    path = nullptr;
  }
  AppearOnPath(path);
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

u8 NwRailVehicle::SubTypeInfo() const {
  return 2;
}

u8 NwSubwayVehicle::SubTypeInfo() const {
  return 4;
}

u16 NwRailVehicle::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction]);
}

u16 NwSubwayVehicle::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction]);
}

// Draw the rail vehicle at a point when the surface view and zoom show it.
// Linux identifies the original overload and its visibility conditions.
u8 NwRailVehicle::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

// Draw the rail vehicle in a rectangle when the surface view and zoom show it.
// Linux identifies the original overload and its visibility conditions.
u8 NwRailVehicle::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

// Draw the subway vehicle at a point when the underground view and zoom show it.
// Linux identifies the original overload and its visibility conditions.
u8 NwSubwayVehicle::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType != 1)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

// Draw the subway vehicle in a rectangle when the underground view and zoom show it.
// Linux identifies the original overload and its visibility conditions.
u8 NwSubwayVehicle::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType != 1)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

void NwRailVehicle::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

void NwSubwayVehicle::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

void NwRailVehicle::SetBirthZoomLevel(i32 value) {
  birthZoomLevel = value;
}

i32 NwRailVehicle::BirthZoomLevel() {
  return birthZoomLevel;
}

void NwSubwayVehicle::SetBirthZoomLevel(i32 value) {
  birthZoomLevel = value;
}

i32 NwSubwayVehicle::BirthZoomLevel() {
  return birthZoomLevel;
}
i16 NwStreetVehicle::zoomOffsets[5];
i32 NwStreetVehicle::cityXMicroUnits, NwStreetVehicle::cityZMicroUnits;
u32 NwStreetVehicle::maxCellX, NwStreetVehicle::maxCellZ;
CtAgentTree *NwStreetVehicle::agentTypeTree;
ScOccupantManager *NwStreetVehicle::occupantManagers[3];
OcAnimManager *NwStreetVehicle::animationManager;
CtStartSimLayer *NwStreetVehicle::streetLayer;
NwTrafficLayer *NwStreetVehicle::trafficLayer;
ScDirtBag *NwStreetVehicle::dirtBag;

// Initialize the vehicle's script and action dispatch tables and retain its application command service.
u8 NwStreetVehicle::StaticInit() {
  if (staticInitialized)
    return 0;
  for (auto &function : scriptFunctions)
    function = nullptr;
  scriptFunctions[0] = &NwStreetVehicle::MoveNStagesScriptFn;
  scriptFunctions[1] = &NwStreetVehicle::PlayAnimationScriptFn;
  scriptFunctions[2] = &NwStreetVehicle::ExecuteScriptScriptFn;
  scriptFunctions[3] = &NwStreetVehicle::FollowerMoveNStagesScriptFn;
  for (auto &function : updateFunctions)
    function = nullptr;
  updateFunctions[0] = &NwStreetVehicle::MoveNStagesUpdateFn;
  updateFunctions[1] = &NwStreetVehicle::MoveAlongPathUpdateFn;
  updateFunctions[2] = &NwStreetVehicle::MoveToPointUpdateFn;
  updateFunctions[3] = &NwStreetVehicle::PlayAnimationUpdateFn;
  updateFunctions[4] = &NwStreetVehicle::FollowerMoveNStagesUpdateFn;
  updateFunctions[5] = &NwStreetVehicle::FollowerMoveAlongPathUpdateFn;
  updateFunctions[6] = &NwStreetVehicle::FollowerMoveToPointUpdateFn;
  updateFunctions[7] = &NwStreetVehicle::FollowTerrainMoveToPointUpdateFn;
  updateFunctions[8] = &NwStreetVehicle::OnWaterMoveToPointUpdateFn;
  updateFunctions[9] = &NwStreetVehicle::WaitForPathClearanceUpdateFn;
  updateFunctions[10] = &NwStreetVehicle::ExecuteScriptUpdateFn;
  updateFunctions[11] = &NwStreetVehicle::AppearOnPathUpdateFn;
  updateFunctions[12] = &NwStreetVehicle::SpiralMotionUpdateFn;
  updateFunctions[13] = &NwStreetVehicle::CircularMotionUpdateFn;
  updateFunctions[14] = &NwStreetVehicle::CircularMotionInCityUpdateFn;
  updateFunctions[15] = &NwStreetVehicle::IdleForDurationUpdateFn;
  updateFunctions[16] = &NwStreetVehicle::DoNothingUpdateFn;
  pathTileFilters[0] = &NwStreetVehicle::IncludeRoadsAndAvenues;
  pathTileFilters[1] = &NwStreetVehicle::IncludeRail;
  pathTileFilters[2] = &NwStreetVehicle::IncludeSubway;
  ApGameInterface *application;
  NwGetStreetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application));
  resourceManager = application->GetResourceManager();
  boxX = ScAddRefNotNull(application->GetBoxX());
  application->Release();
  staticInitialized = 1;
  return 1;
}

i32 NwStreetVehicle::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  return (this->*updateFunctions[actionStack[actionStackTop]])(elapsed, status, cancel);
}

i32 NwStreetVehicle::CallScriptFunction(NwScriptInstruction *instruction) {
  i16 opcode = static_cast<i16>(instruction->opcode);
  return (this->*scriptFunctions[opcode])(instruction);
}

NwStreetVehicle::NwStreetVehicle()
    : byte32(0), vehicleState(0), visible(1), geometryFlags(kNwStreetVehicleGeometry), effect(0), byte44(1),
      occupantManagerIndex(0), animationElapsed(0), animationInterval(15000), orientation(0), angle(0), pitch(2),
      frame(0), producer(nullptr), stateStack{}, stateStackTop(0), actionStackTop(-1), attributes(nullptr),
      spriteRepository(nullptr), actionDescriptor(nullptr), vehicleAhead(nullptr), followingVehicle(nullptr), speed(0),
      pathLength(0), paths{}, leaderPaths{}, pathTop(-1), leaderPathTop(-1), follower(nullptr), leader(nullptr),
      movementDistance(0), movementCarry(0), sortId(1), movementCounter(11) {
}

NwStreetVehicle::~NwStreetVehicle() {
  if (byte32)
    Shutdown();
}

// Load and retain vehicle attributes, select their default animation and emit configured appearance events.
u8 NwStreetVehicle::Init(const RsKey &key) {
  if (byte32)
    return 0;
  RsResource *resource;
  u8 result = NwStreetLayer::GetVehicleAttrib(key, &resource);
  if (!result)
    return result;
  attributes = static_cast<NwVehicleAttrib *>(resource);
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  spriteRepository = animation->repository;
  actionDescriptor = animation->GetDefaultActionDescriptor();
  if (!actionDescriptor)
    return 0;
  i32 event = attributes->value0;
  if (event) {
    CtCoord position;
    NwStreetOccupant::GetLocation(&position);
    boxX->Event(kAuEventPlayEffect, static_cast<u32>(event), reinterpret_cast<uptr>(this), static_cast<u32>(position.x),
        static_cast<u32>(position.z));
  }
  event = attributes->value2;
  if (event) {
    CtCoord position;
    NwStreetOccupant::GetLocation(&position);
    boxX->Event(kAuEventPlayEffect, static_cast<u32>(event), reinterpret_cast<uptr>(this), static_cast<u32>(position.x),
        static_cast<u32>(position.z));
  }
  byte32 = 1;
  return 1;
}

// Remove an initialized vehicle, cancel its actions and release its retained paths and producer.
void NwStreetVehicle::Shutdown() {
  if (!byte32)
    return;
  byte32 = 0;
  if (NwStreetOccupant::IsInCity())
    animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
  if (leader) {
    leader->FollowerDisappeared(static_cast<NwVehicle *>(this));
    leader = nullptr;
  }
  if (follower) {
    follower->LeaderDisappeared(static_cast<NwVehicle *>(this));
    follower = nullptr;
  }
  i32 event = attributes->value0;
  if (event) {
    CtCoord position;
    NwStreetOccupant::GetLocation(&position);
    boxX->Event(kAuEventEffectSlot52, static_cast<u32>(event), reinterpret_cast<uptr>(this),
        static_cast<u32>(position.x), static_cast<u32>(position.z));
  }
  event = attributes->value2;
  if (event) {
    CtCoord position;
    NwStreetOccupant::GetLocation(&position);
    boxX->Event(kAuEventEffectSlot84, static_cast<u32>(event), reinterpret_cast<uptr>(this),
        static_cast<u32>(position.x), static_cast<u32>(position.z));
  }
  WipeActionStack();
  LeaveAllPaths();
  while (leaderPathTop >= 0) {
    leaderPaths[leaderPathTop]->Release();
    leaderPaths[leaderPathTop] = nullptr;
    --leaderPathTop;
  }
  ScRelease(&attributes);
  if (producer) {
    producer->VehicleDisappears(static_cast<NwVehicle *>(this));
    producer->Release();
    producer = nullptr;
  }
}

u8 NwStreetVehicle::StaticShutdown() {
  if (staticInitialized) {
    boxX->Release();
    boxX = nullptr;
    resourceManager = nullptr;
    staticInitialized = 0;
  }
  return 1;
}

// Initialize zoom offsets and retain the city services used by vehicle simulation and rendering.
u8 NwStreetVehicle::CitySpecificStaticInit(CtCity *city) {
  zoomOffsets[0] = 0;
  zoomOffsets[1] = 0;
  zoomOffsets[2] = 0;
  zoomOffsets[3] = 48;
  zoomOffsets[4] = 96;
  agentTypeTree = ScAddRefNotNull(NwGetStreetAgentTypeTree());
  cityXMicroUnits = i32(city->AnimCellCountX());
  cityZMicroUnits = i32(city->AnimCellCountZ());
  maxCellX = city->CellCountX() - 1;
  maxCellZ = city->CellCountZ() - 1;
  ScOccupantManager *surface = city->SurfaceOccupantManager();
  ScOccupantManager *underground = city->UndergroundOccupantManagerLevelTwo();
  occupantManagers[0] = surface;
  occupantManagers[1] = surface;
  occupantManagers[2] = underground;
  for (u32 i = 0; i < 3; ++i)
    occupantManagers[i]->AddRef();
  animationManager = ScAddRefNotNull(city->AnimOccupantManager());
  streetLayer = ScAddRefNotNull(city->StrtSimLayer());
  trafficLayer = ScAddRefNotNull(city->TrafficLayer());
  dirtBag = ScAddRefNotNull(city->DirtBag());
  return 1;
}

// Release the retained vehicle services and each independently held occupant manager in original order.
u8 NwStreetVehicle::CitySpecificStaticShutdown() {
  ScRelease(&streetLayer);
  ScRelease(&trafficLayer);
  ScRelease(&dirtBag);
  ScRelease(&animationManager);
  ScRelease(&agentTypeTree);
  for (u32 i = 0; i < 3; ++i) {
    occupantManagers[i]->Release();
    occupantManagers[i] = nullptr;
  }
  return 1;
}

void NwStreetVehicle::SetBirthZoomLevel(i32 value) {
  birthZoomLevel = value;
}

i32 NwStreetVehicle::BirthZoomLevel() {
  return birthZoomLevel;
}

bool NwStreetVehicle::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_SpSprite, SpSprite);
  HANDLE_QUERY_INTERFACE(IID_NwVehicle, NwVehicle);
  return NwStreetOccupant::QueryInterface(iid, out);
}

u32 NwStreetVehicle::AddRef() {
  return NwStreetOccupant::AddRef();
}

u32 NwStreetVehicle::Release() {
  return NwStreetOccupant::Release();
}

bool NwStreetVehicle::IsGeomFlag(u32 mask) {
  return (geometryFlags & mask) == mask;
}

u8 NwStreetVehicle::SetGeomFlag(u32 mask) {
  geometryFlags |= mask;
  return 1;
}

u8 NwStreetVehicle::ClearGeomFlag(u32 mask) {
  geometryFlags &= ~mask;
  return 1;
}

u32 NwStreetVehicle::GetGeomFlag() {
  return geometryFlags;
}

u8 NwStreetVehicle::SubTypeInfo() const {
  return 1;
}

u8 NwStreetVehicle::SetSpriteAttrib(SpDefinition *) {
  return 0;
}

u32 NwStreetVehicle::SetProducer(NwVehicleProducerInterface *value) {
  producer = value;
  return producer->AddRef();
}

bool NwStreetVehicle::InCityBoundsMicro(i32 pointX, i32 pointZ) {
  return pointX >= 0 && pointX < cityXMicroUnits && pointZ >= 0 && pointZ < cityZMicroUnits;
}

bool NwStreetVehicle::InCityBoundsCell(u32 cellX, u32 cellZ) {
  return cellX <= maxCellX && cellZ <= maxCellZ;
}

void NwStreetVehicle::SetVisibility(bool value) {
  if (static_cast<u8>(value) != visible) {
    visible = value;
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
  }
}

bool NwStreetVehicle::IsInState(i32 mask) {
  return (vehicleState & static_cast<u8>(mask)) != 0;
}

void NwStreetVehicle::SetState(i32 mask) {
  vehicleState |= static_cast<u8>(mask);
}

u8 NwStreetVehicle::ResetState(i32 mask) {
  vehicleState = static_cast<u8>(vehicleState - static_cast<u8>(mask));
  return vehicleState;
}

void NwStreetVehicle::PopActionStack() {
  --actionStackTop;
}

void NwStreetVehicle::PushToActionStack(u32 action) {
  i32 index = actionStackTop;
  actionStackTop = index + 1;
  actionStack[index + 1] = action;
}

void NwStreetVehicle::PopStateStack(i32 size) {
  stateStackTop -= size;
}

void NwStreetVehicle::PushStateStack(i32 size) {
  stateStackTop += size;
}

// Schedule a period of inactivity using the original duration and elapsed-state words.
i32 NwStreetVehicle::IdleForDuration(u32 duration) {
  i32 offset = stateStackTop;
  stateStackTop += 8;
  u32 elapsed = 0;
  std::memcpy(stateStack + offset, &duration, sizeof(duration));
  std::memcpy(stateStack + offset + 4, &elapsed, sizeof(elapsed));
  PushToActionStack(15);
  return 2;
}

// Advance an idle period, popping its state when cancelled or its duration expires.
i32 NwStreetVehicle::IdleForDurationUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 offset = stateStackTop - 8;
  if (!cancel) {
    u32 duration, accumulated;
    std::memcpy(&duration, stateStack + offset, sizeof(duration));
    std::memcpy(&accumulated, stateStack + offset + 4, sizeof(accumulated));
    accumulated += elapsed;
    std::memcpy(stateStack + offset + 4, &accumulated, sizeof(accumulated));
    if (accumulated < duration)
      return 2;
  }
  stateStackTop -= 8;
  return 0;
}

i32 NwStreetVehicle::DoNothing() {
  PushToActionStack(16);
  return 2;
}

i32 NwStreetVehicle::DoNothingUpdateFn(u32, i32, bool cancel) {
  return cancel ? 0 : 2;
}

// Select an animation action and schedule its elapsed-time state.
i32 NwStreetVehicle::PlayAnimation(u16 action, i32 duration, i32 variation) {
  i32 offset = stateStackTop;
  stateStackTop += 8;
  i32 count = duration;
  if (variation)
    count = NwVehicleRandom.Range(static_cast<i32>(u32(duration) - u32(variation)),
        static_cast<i32>(u32(duration) + u32(variation)));
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  actionDescriptor = animation->GetActionDescriptor(action);
  frame = 0;
  u32 total = 20000u * static_cast<u32>(count) * actionDescriptor->frameCount, elapsed = 0;
  std::memcpy(stateStack + offset, &elapsed, sizeof(elapsed));
  std::memcpy(stateStack + offset + 4, &total, sizeof(total));
  PushToActionStack(3);
  return 2;
}

// Advance animation time and pop its state after cancellation or the final elapsed interval.
i32 NwStreetVehicle::PlayAnimationUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 offset = stateStackTop - 8;
  if (!cancel) {
    u32 accumulated, total;
    std::memcpy(&accumulated, stateStack + offset, sizeof(accumulated));
    std::memcpy(&total, stateStack + offset + 4, sizeof(total));
    accumulated += elapsed;
    std::memcpy(stateStack + offset, &accumulated, sizeof(accumulated));
    if (accumulated <= total)
      return 2;
  }
  stateStackTop -= 8;
  return 0;
}

i32 NwStreetVehicle::AppearOnPath(NwVehiclePath *path) {
  i32 offset = stateStackTop;
  stateStackTop += static_cast<i32>(sizeof(path));
  std::memcpy(stateStack + offset, &path, sizeof(path));
  PushToActionStack(11);
  return 2;
}

// Continue moving along the current path until another movement action is scheduled or the path ends.
i32 NwStreetVehicle::MoveAlongPathUpdateFn(u32, i32 status, bool cancel) {
  if (cancel)
    return 0;
  while (status == 0) {
    i32 distance, pointX, pointY, pointZ;
    paths[0]->GetDistToEndFromCurrentPoint(&distance);
    if (!paths[0]->GetNextPoint(&pointX, &pointY, &pointZ))
      return 0;
    status = MoveToPoint(pointX, pointY, pointZ, distance);
  }
  return 2;
}

// Retain a script and schedule its instruction sequence with the requested repetition count.
i32 NwStreetVehicle::ExecuteScript(const RsKey &key, i32 count, i32 variation) {
  i32 offset = stateStackTop, stateSize = static_cast<i32>(sizeof(NwVehicleScriptState));
  stateStackTop += stateSize;
  RsResource *resource;
  if (!NwStreetLayer::GetScript(key, &resource)) {
    stateStackTop -= stateSize;
    return 1;
  }
  auto *script = static_cast<NwStreetScript *>(resource);
  std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, script), &script, sizeof(script));
  i32 repeats = count;
  if (variation)
    repeats = NwVehicleRandom.Range(static_cast<i32>(u32(count) - u32(variation)),
        static_cast<i32>(u32(count) + u32(variation)));
  i32 completed = 0;
  i16 index = -1;
  std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, repeatCount), &repeats, sizeof(repeats));
  std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, completedCount), &completed, sizeof(completed));
  std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, instructionIndex), &index, sizeof(index));
  PushToActionStack(10);
  return 2;
}

i32 NwStreetVehicle::ExecuteScriptScriptFn(NwScriptInstruction *instruction) {
  u32 *arguments = instruction->arguments;
  RsKey key(arguments[0], arguments[1], arguments[2]);
  i32 count = static_cast<i32>(arguments[3]), variation = static_cast<i32>(arguments[4]);
  return ExecuteScript(key, count, variation);
}

// Dispatch script instructions until an action waits, fails, finishes its repetitions or is cancelled.
i32 NwStreetVehicle::ExecuteScriptUpdateFn(u32, i32 status, bool cancel) {
  i32 stateSize = static_cast<i32>(sizeof(NwVehicleScriptState)), offset = stateStackTop - stateSize;
  if (!cancel) {
    while (status != 2 && status != 1) {
      i16 index;
      NwStreetScript *script;
      std::memcpy(&index, stateStack + offset + offsetof(NwVehicleScriptState, instructionIndex), sizeof(index));
      index = static_cast<i16>(u16(index) + 1);
      std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, instructionIndex), &index, sizeof(index));
      std::memcpy(&script, stateStack + offset + offsetof(NwVehicleScriptState, script), sizeof(script));
      if (index == script->instructionCount) {
        i32 completed, repeats;
        std::memcpy(&completed, stateStack + offset + offsetof(NwVehicleScriptState, completedCount), sizeof(completed));
        std::memcpy(&repeats, stateStack + offset + offsetof(NwVehicleScriptState, repeatCount), sizeof(repeats));
        completed = static_cast<i32>(u32(completed) + 1);
        std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, completedCount), &completed, sizeof(completed));
        if (completed >= repeats)
          break;
        index = 0;
        std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, instructionIndex), &index, sizeof(index));
      }
      status = CallScriptFunction(&script->instructions[index]);
    }
    if (status == 2)
      return 2;
  }
  NwStreetScript *script;
  std::memcpy(&script, stateStack + offset + offsetof(NwVehicleScriptState, script), sizeof(script));
  if (script) {
    script->Release();
    script = nullptr;
    std::memcpy(stateStack + offset + offsetof(NwVehicleScriptState, script), &script, sizeof(script));
  }
  stateStackTop -= stateSize;
  return !cancel && status == 1 ? 1 : 0;
}

// Schedule movement through a number of path stages using the requested animation action.
i32 NwStreetVehicle::MoveNStages(i32 count, i32 variation, u16 action) {
  i32 offset = stateStackTop;
  stateStackTop += static_cast<i32>(sizeof(NwVehicleStagesState));
  i32 stages = count;
  if (variation)
    stages = NwVehicleRandom.Range(static_cast<i32>(u32(count) - u32(variation)),
        static_cast<i32>(u32(count) + u32(variation)));
  i32 zero = 0, one = 1;
  i16 zeroShort = 0;
  std::memcpy(stateStack + offset, &stages, sizeof(stages));
  std::memcpy(stateStack + offset + 4, &zero, sizeof(zero));
  std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, disappearanceCounter), &zeroShort, sizeof(zeroShort));
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  NwSpriteAction *selected = animation->GetActionDescriptor(action);
  if (selected != actionDescriptor) {
    actionDescriptor = selected;
    frame = 0;
  }
  PushToActionStack(0);
  NwVehiclePath *emptyPath = nullptr;
  std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, nextPath), &emptyPath, sizeof(emptyPath));
  std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, moving), &one, sizeof(one));
  MoveAlongPath();
  return 2;
}

// Advance a vehicle through its requested path stages, respecting removal, clearance and sound callbacks.
i32 NwStreetVehicle::MoveNStagesUpdateFn(u32, i32 status, bool cancel) {
  const i32 stateSize = static_cast<i32>(sizeof(NwVehicleStagesState)), offset = stateStackTop - stateSize;
  i32 moving;
  NwVehiclePath *nextPath;
  std::memcpy(&moving, stateStack + offset + offsetof(NwVehicleStagesState, moving), sizeof(moving));
  if (cancel) {
    if (!moving) {
      std::memcpy(&nextPath, stateStack + offset + offsetof(NwVehicleStagesState, nextPath), sizeof(nextPath));
      if (nextPath) {
        nextPath->Release();
        nextPath = nullptr;
        std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, nextPath), &nextPath, sizeof(nextPath));
      }
    }
    stateStackTop -= stateSize;
    return 0;
  }
  if (status == 2)
    return 2;
  for (;;) {
    if (moving) {
      i32 count, index;
      std::memcpy(&count, stateStack + offset + offsetof(NwVehicleStagesState, stageCount), sizeof(count));
      std::memcpy(&index, stateStack + offset + offsetof(NwVehicleStagesState, stageIndex), sizeof(index));
      index = static_cast<i32>(u32(index) + 1);
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, stageIndex), &index, sizeof(index));
      if (index >= count) {
        stateStackTop -= stateSize;
        return 0;
      }
      u16 disappearanceCounter;
      std::memcpy(&disappearanceCounter, stateStack + offset + offsetof(NwVehicleStagesState, disappearanceCounter),
          sizeof(disappearanceCounter));
      bool disappear = producer->NeedVehicleDisappear(static_cast<NwVehicle *>(this), &disappearanceCounter);
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, disappearanceCounter), &disappearanceCounter,
          sizeof(disappearanceCounter));
      if (disappear) {
        stateStackTop -= stateSize;
        return 1;
      }
      moving = 0;
      nextPath = nullptr;
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, moving), &moving, sizeof(moving));
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, nextPath), &nextPath, sizeof(nextPath));
      SelectNextPath(paths[0], &nextPath);
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, nextPath), &nextPath, sizeof(nextPath));
      if (!nextPath) {
        stateStackTop -= stateSize;
        return 1;
      }
      if (attributes->value2 && index % 4 == 0 && NwVehicleRandom.Bounded(2) == 0) {
        u32 sound = 0;
        if (NwVehicleRandom.Bounded(100) >= 80)
          sound = NwVehicleRandom.Bounded(2) == 0 ? 1 : 2;
        boxX->Event(kAuEventEffectSlot56, static_cast<u32>(attributes->value2), reinterpret_cast<uptr>(this), sound, 0);
      }
      status = WaitForPathClearance(nextPath);
      if (status == 2)
        return 2;
    } else if (status == 0) {
      std::memcpy(&nextPath, stateStack + offset + offsetof(NwVehicleStagesState, nextPath), sizeof(nextPath));
      MakeCurrentPath(nextPath);
      nextPath = nullptr;
      moving = 1;
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, nextPath), &nextPath, sizeof(nextPath));
      std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, moving), &moving, sizeof(moving));
      status = MoveAlongPath();
      if (status == 2)
        return 2;
    } else {
      std::memcpy(&nextPath, stateStack + offset + offsetof(NwVehicleStagesState, nextPath), sizeof(nextPath));
      if (nextPath) {
        nextPath->Release();
        nextPath = nullptr;
        std::memcpy(stateStack + offset + offsetof(NwVehicleStagesState, nextPath), &nextPath, sizeof(nextPath));
      }
      stateStackTop -= stateSize;
      return 1;
    }
  }
}

// Decode and schedule a scripted multi-stage movement instruction.
i32 NwStreetVehicle::MoveNStagesScriptFn(NwScriptInstruction *instruction) {
  u32 *arguments = instruction->arguments;
  i32 count = static_cast<i32>(arguments[0]), variation = static_cast<i32>(arguments[1]);
  u16 action = static_cast<u16>(arguments[2]);
  return MoveNStages(count, variation, action);
}

i32 NwStreetVehicle::PlayAnimationScriptFn(NwScriptInstruction *instruction) {
  u32 *arguments = instruction->arguments;
  u16 action = static_cast<u16>(arguments[0]);
  i32 duration = static_cast<i32>(arguments[1]), variation = static_cast<i32>(arguments[2]);
  return PlayAnimation(action, duration, variation);
}

// Schedule a follower's multi-stage movement with the requested animation action.
i32 NwStreetVehicle::FollowerMoveNStages(u16 action) {
  ++stateStackTop;
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  NwSpriteAction *selected = animation->GetActionDescriptor(action);
  if (selected != actionDescriptor) {
    actionDescriptor = selected;
    frame = 0;
  }
  PushToActionStack(4);
  FollowerMoveAlongPath();
  return 2;
}

i32 NwStreetVehicle::FollowerMoveNStagesScriptFn(NwScriptInstruction *instruction) {
  u16 action = static_cast<u16>(instruction->arguments[0]);
  return FollowerMoveNStages(action);
}

// Continue a follower along its current path until movement waits or the path ends.
i32 NwStreetVehicle::FollowerMoveAlongPathUpdateFn(u32, i32 status, bool cancel) {
  if (cancel)
    return 0;
  while (status == 0) {
    i32 distance, pointX, pointY, pointZ;
    paths[0]->GetDistToEndFromCurrentPoint(&distance);
    if (!paths[0]->GetNextPoint(&pointX, &pointY, &pointZ))
      return 0;
    status = FollowerMoveToPoint(pointX, pointY, pointZ, distance);
  }
  return 2;
}

// Take the next leader path for a follower or wait until the leader supplies one.
i32 NwStreetVehicle::FollowerMoveNStagesUpdateFn(u32, i32, bool cancel) {
  if (cancel) {
    --stateStackTop;
    return 0;
  }
  if (vehicleAhead)
    return 2;
  NwVehiclePath *path = GetNextPath();
  if (!path) {
    if (leader)
      return 2;
    --stateStackTop;
    return 0;
  }
  if (path->ForceEnterVehicle(static_cast<NwVehicle *>(this))) {
    MakeCurrentPath(path);
    FollowerMoveAlongPath();
  } else {
    path->Release();
  }
  return 2;
}

// Try to enter a path immediately or schedule a clearance wait when it is occupied.
i32 NwStreetVehicle::WaitForPathClearance(NwVehiclePath *path) {
  if (path->EnterVehicle(static_cast<NwVehicle *>(this), 28))
    return 0;
  i32 offset = stateStackTop;
  stateStackTop += static_cast<i32>(sizeof(NwVehicleWaitState));
  u32 zero = 0;
  std::memcpy(stateStack + offset + offsetof(NwVehicleWaitState, path), &path, sizeof(path));
  std::memcpy(stateStack + offset + offsetof(NwVehicleWaitState, elapsed), &zero, sizeof(zero));
  std::memcpy(stateStack + offset + offsetof(NwVehicleWaitState, soundElapsed), &zero, sizeof(zero));
  PushToActionStack(9);
  return 2;
}

// Wait for path entry, timing out or emitting the configured periodic sound while blocked.
i32 NwStreetVehicle::WaitForPathClearanceUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 stateSize = static_cast<i32>(sizeof(NwVehicleWaitState)), offset = stateStackTop - stateSize;
  if (cancel) {
    stateStackTop -= stateSize;
    return 0;
  }
  NwVehiclePath *path;
  std::memcpy(&path, stateStack + offset + offsetof(NwVehicleWaitState, path), sizeof(path));
  if (path->EnterVehicle(static_cast<NwVehicle *>(this), 28)) {
    stateStackTop -= stateSize;
    return 0;
  }
  u32 accumulated;
  std::memcpy(&accumulated, stateStack + offset + offsetof(NwVehicleWaitState, elapsed), sizeof(accumulated));
  accumulated += elapsed;
  std::memcpy(stateStack + offset + offsetof(NwVehicleWaitState, elapsed), &accumulated, sizeof(accumulated));
  if (accumulated > kNwVehiclePathClearanceTimeout) {
    stateStackTop -= stateSize;
    return 1;
  }
  if (attributes->value1) {
    u32 soundElapsed;
    std::memcpy(&soundElapsed, stateStack + offset + offsetof(NwVehicleWaitState, soundElapsed), sizeof(soundElapsed));
    soundElapsed += elapsed;
    std::memcpy(stateStack + offset + offsetof(NwVehicleWaitState, soundElapsed), &soundElapsed, sizeof(soundElapsed));
    if (soundElapsed > kNwBlockedVehicleSoundDelay) {
      if (NwVehicleRandom.Bounded(3) == 0) {
        CtCoord position;
        NwStreetOccupant::GetLocation(&position);
        boxX->Event(kAuEventEffectSlot60, static_cast<u32>(attributes->value1), reinterpret_cast<uptr>(this),
            static_cast<u32>(position.x), static_cast<u32>(position.z));
      }
      soundElapsed = 0;
      std::memcpy(stateStack + offset + offsetof(NwVehicleWaitState, soundElapsed), &soundElapsed, sizeof(soundElapsed));
    }
  }
  return 2;
}

// Enter the selected path, make the vehicle visible and schedule its initial script.
i32 NwStreetVehicle::AppearOnPathUpdateFn(u32, i32, bool cancel) {
  i32 stateSize = static_cast<i32>(sizeof(NwVehiclePath *)), offset = stateStackTop - stateSize;
  if (cancel) {
    NwVehiclePath *path;
    std::memcpy(&path, stateStack + offset, sizeof(path));
    if (path)
      path->Release();
    stateStackTop -= stateSize;
    return 0;
  }
  if (leader) {
    if (leaderPathTop < 0 || !leaderPaths[0]->EnterVehicle(static_cast<NwVehicle *>(this), 1))
      return 2;
    MakeCurrentPath(GetNextPath());
  } else {
    NwVehiclePath *path;
    std::memcpy(&path, stateStack + offset, sizeof(path));
    if (!path) {
      stateStackTop -= stateSize;
      return 1;
    }
    if (!path->EnterVehicle(static_cast<NwVehicle *>(this), 28))
      return 2;
    MakeCurrentPath(path);
  }
  SetVisibility(true);
  stateStackTop -= stateSize;
  --actionStackTop;
  return ExecuteScript(attributes->scriptKey, 1, 0);
}

// Remove and return the oldest path received from the leader without changing its reference count.
// Linux identifies the member; the distinct Windows entry remains unresolved.
NwVehiclePath *NwStreetVehicle::GetNextPath() {
  if (leaderPathTop < 0)
    return nullptr;
  NwVehiclePath *path = leaderPaths[0];
  --leaderPathTop;
  for (i32 index = 1; index <= leaderPathTop + 1; ++index)
    leaderPaths[index - 1] = leaderPaths[index];
  leaderPaths[leaderPathTop + 1] = nullptr;
  return path;
}

bool NwStreetVehicle::IncludeRail(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRail) != 0;
}

bool NwStreetVehicle::IncludeRoadsAndAvenues(ScOccupant *occupant) {
  u32 flags = occupant->GetGeomFlag();
  return (flags & kOcGeomRoad) != 0 || (flags & kOcGeomHighway) != 0;
}

bool NwStreetVehicle::IncludeSubway(ScOccupant *occupant) {
  return agentTypeTree->IsAncestor(kCtAgentMovableSubway, occupant->TypeId());
}

void NwStreetVehicle::GetLocationInCellCoords(u32 *outX, u32 *outZ) const {
  NwStreetOccupant::GetLocationInCellCoords(outX, outZ);
}

u16 NwStreetVehicle::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction] + actionDescriptor->frames[frame]);
}

i32 NwStreetVehicle::GetW(i32 zoom, i32 rotation) const {
  return spriteRepository->GetWidth(GetFrameNumber(zoom, rotation));
}

i32 NwStreetVehicle::GetH(i32 zoom, i32 rotation) const {
  return spriteRepository->GetHeight(GetFrameNumber(zoom, rotation));
}

u8 NwStreetVehicle::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  return spriteRepository->GetRegistration(GetFrameNumber(zoom, rotation), out);
}

u8 NwStreetVehicle::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 pointX, i32 pointY) const {
  return spriteRepository->GetDrawRect(GetFrameNumber(zoom, rotation), out, pointX, pointY);
}

bool NwStreetVehicle::IsPixelOpaque(i32 pointX, i32 pointY, i32 zoom, i32 rotation) const {
  return spriteRepository->HitTest(GetFrameNumber(zoom, rotation), pointX, pointY);
}

u8 NwStreetVehicle::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwStreetVehicle::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

// Advance animation and queued vehicle actions, then notify its follower or remove a finished occupant.
i32 NwStreetVehicle::Tick(u32 elapsed) {
  if (!byte32 ||
      (!IsAirVehicle() &&
          static_cast<NwStreetLayer *>(streetLayer)
              ->ToxicCloudPresent(u32(x >> kCtMicroCoordinateShift), u32(z >> kCtMicroCoordinateShift))))
    return 0;
  movementStatus = 0;
  animationElapsed += elapsed;
  if (animationElapsed > animationInterval) {
    u8 oldFrame = frame;
    i32 nextFrame = i32(oldFrame) + 1;
    frame = static_cast<u8>(nextFrame);
    if (nextFrame == actionDescriptor->frameCount)
      frame = 0;
    if (frame != oldFrame)
      movementStatus = 1;
    animationElapsed = 0;
  }
  if (!leader && actionStackTop >= 0) {
    i32 status = 0;
    while ((status = ExecuteCurrentUpdateFn(elapsed, status, false)) != 2) {
      --actionStackTop;
      if (actionStackTop < 0) {
        animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
        return 0;
      }
    }
    if (movementStatus == 1)
      animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 0, nullptr);
    if (follower)
      follower->TickFromLeader();
  } else if (movementStatus == 1) {
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 0, nullptr);
  }
  return 0;
}

u16 NwBoatVehicle::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>((zoom - birthZoom) * spriteRepository->GetZoomSetCount() +
      NwVehicleFrameOffsets[pitch][direction]);
}

// Return the vehicle's embedded sprite interface without acquiring another reference.
SpSprite *NwStreetVehicle::GetSpriteInst() {
  return static_cast<SpSprite *>(this);
}

u8 NwStreetVehicle::IsVisible() {
  return visible;
}

i32 NwStreetVehicle::Width() const {
  return 128;
}

u8 NwStreetVehicle::IsAirVehicle() {
  return 0;
}

i32 NwStreetVehicle::Length() const {
  return attributes->length;
}

u8 NwStreetVehicle::IsEmergencyVehicle() {
  return attributes->flag;
}

// Retain the following vehicle unless a different one is already recorded.
u8 NwStreetVehicle::SetFollowingVehicle(NwVehicle *vehicle) {
  if (followingVehicle && vehicle != followingVehicle)
    return 0;
  followingVehicle = vehicle;
  if (vehicle)
    vehicle->AddRef();
  return 1;
}

iptr NwStreetVehicle::SetVehicleAhead(NwVehicle *vehicle) {
  vehicleAhead = vehicle;
  return reinterpret_cast<iptr>(vehicle);
}

void NwStreetVehicle::FollowingVehicleDisappeared(NwVehicle *) {
  followingVehicle = nullptr;
}

void NwStreetVehicle::VehicleAheadLeftPath(NwVehicle *) {
  vehicleAhead = nullptr;
}

iptr NwStreetVehicle::SetFollower(NwVehicle *vehicle) {
  follower = vehicle;
  return reinterpret_cast<iptr>(vehicle);
}

iptr NwStreetVehicle::SetLeader(NwVehicle *vehicle) {
  leader = vehicle;
  return reinterpret_cast<iptr>(vehicle);
}

void NwStreetVehicle::FollowerDisappeared(NwVehicle *) {
  follower = nullptr;
}

void NwStreetVehicle::LeaderDisappeared(NwVehicle *) {
  leader = nullptr;
}

f32 NwStreetVehicle::LastMovementDist() {
  f32 result = movementDistance;
  movementDistance = 0;
  return result;
}

// Retain a copy of the leader's new path when the queue has room, returning the observed register value.
iptr NwStreetVehicle::LeaderEnteredPath(NwVehiclePath *path) {
  ++leaderPathTop;
  if (leaderPathTop > 3) {
    --leaderPathTop;
    return leaderPathTop;
  } else {
    leaderPaths[leaderPathTop] = path->GetCopy();
    return leaderPaths[leaderPathTop]->AddRef();
  }
}

void NwStreetVehicle::LeaveLastPath() {
  paths[pathTop]->VehicleLeftPath(static_cast<NwVehicle *>(this));
  if (!follower)
    paths[pathTop]->UnblockPath();
  paths[pathTop]->Release();
  paths[pathTop] = nullptr;
  if (followingVehicle) {
    followingVehicle->VehicleAheadLeftPath(static_cast<NwVehicle *>(this));
    followingVehicle->Release();
    followingVehicle = nullptr;
  }
  --pathTop;
}

void NwStreetVehicle::LeaveAllPaths() {
  i32 remaining = pathTop;
  while (remaining-- >= 0)
    LeaveLastPath();
  if (vehicleAhead)
    vehicleAhead->FollowingVehicleDisappeared(static_cast<NwVehicle *>(this));
}

// Cancel every pending action through the original update dispatcher.
void NwStreetVehicle::WipeActionStack() {
  i32 status = 0;
  while (actionStackTop >= 0) {
    status = ExecuteCurrentUpdateFn(0, status, true);
    --actionStackTop;
  }
}

// Advance actions after a leader tick, removing the occupant when its action stack is exhausted.
// A surviving vehicle also advances its follower; this notification has no return value.
void NwStreetVehicle::TickFromLeader() {
  if (actionStackTop < 0)
    return;
  i32 status = 0;
  for (;;) {
    status = ExecuteCurrentUpdateFn(0, status, false);
    if (status == 2)
      break;
    --actionStackTop;
    if (actionStackTop < 0) {
      animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
      return;
    }
  }
  if (follower)
    follower->TickFromLeader();
}

// Insert a new current path, update its sort position and notify the follower.
void NwStreetVehicle::MakeCurrentPath(NwVehiclePath *path) {
  if (!leader && follower)
    path->BlockPath();
  ++pathTop;
  i32 newSortId;
  if (path->SortId(&newSortId) && newSortId != sortId && NwStreetOccupant::IsInCity()) {
    sortId = newSortId;
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 128, &sortId);
  }
  if (pathTop == 0) {
    i32 pathDistance = path->Length();
    i32 length = static_cast<i32>(u32(attributes->length) + u32(pathDistance));
    remainingDistance = static_cast<f32>(length);
  }
  for (i32 index = pathTop; index >= 1; --index)
    paths[index] = paths[index - 1];
  paths[0] = path;
  if (follower)
    follower->LeaderEnteredPath(path);
}

void NwStreetVehicle::ChangeToSortIdOfPath(NwVehiclePath *path) {
  i32 newSortId;
  if (path->SortId(&newSortId)) {
    sortId = newSortId;
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 128, &sortId);
  }
}

// Store the orientation byte and its original single-precision angle, returning the observed register value.
u32 NwStreetVehicle::SetOrientation(u8 value) {
  orientation = value;
  angle = f32(value) * 0x1.921fb6p-3f;
  return value;
}

void NwStreetVehicle::GetRotationOffset(u8 *) const {
}

u32 NwStreetVehicle::GetLayerFlags() const {
  return 0;
}

void NwStreetVehicle::SetRotationOffset(u8) {
}

u8 NwStreetVehicle::SetEffect(u32 value) {
  effect = value;
  return 1;
}

void NwStreetVehicle::SetLayer(u8) {
}

u8 NwStreetVehicle::DemandArt(i32, i32, bool) {
  return 1;
}

i32 NwStreetVehicle::ReleaseArt(i32, i32) {
  return 1;
}

bool NwStreetVehicle::SetDefinition(SpDefinition *) {
  return true;
}

u8 NwStreetVehicle::HasGeomFlag(u32 mask) {
  return IsGeomFlag(mask);
}

void NwStreetVehicle::SetNext(SpSprite *) {
}

// Return the empty next-sprite link used by vehicle sprites.
SpSprite *NwStreetVehicle::GetNext() {
  return nullptr;
}
