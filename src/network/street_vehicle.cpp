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

// Cell bit twenty-three becomes the signed animation-coordinate bit after the micro-coordinate shift.
enum NwAircraftCoordinateLimit : u32 {
  kNwAircraftCellSignBit = 0x800000,
};

// Crowd animation, blocked vehicle paths and takeoff permission each retain their original timing threshold.
enum NwVehicleTiming : i32 {
  kNwStrikeCrowdFrameInterval = 1000000,
  kNwCrowdFrameInterval = 0x493e0,
  kNwVehiclePathClearanceTimeout = 0x2625a00,
  kNwTakeoffPermissionWaitLimit = 0x4c4b3f,
};

// Blocked street vehicles wait this interval between sound requests.
enum NwStreetSoundDelay : i32 {
  kNwBlockedVehicleSoundDelay = 0x4c4b40,
};

// Ships wait within the original random interval at piers and before turning around.
enum NwShipIdleDelay : i32 {
  kNwShipIdleMinimum = 0x5b8d80,
  kNwShipIdleMaximum = 0xb71b00,
};

// The plane routine selects independent random idle ranges for its two wait instructions.
enum NwPlaneIdleDelay : i32 {
  kNwPlaneFirstIdleMinimum = 0xe4e1c0,
  kNwPlaneFirstIdleMaximum = 0x1c9c380,
  kNwPlaneSecondIdleMinimum = 0x7a1200,
  kNwPlaneSecondIdleMaximum = 0xb71b00,
};

extern ScRandom NwVehicleRandom;
ScDirtBag *NwBoatVehicle::waterTerrain;
ScDirtBag *NwShipVehicle::waterTerrain;
i32 NwRiotCrowdObject::currentViewType;
NwRiotCrowdObject::UpdateFn NwRiotCrowdObject::updateFunctions[2];
NwRiotCrowdObject::ScriptFn NwRiotCrowdObject::scriptFunctions[1];
i32 NwParadeBlockObject::currentViewType;
NwParadeBlockObject::UpdateFn NwParadeBlockObject::updateFunctions[2];
NwParadeBlockObject::ScriptFn NwParadeBlockObject::scriptFunctions[1];
i32 NwBoatVehicle::currentViewType, NwShipVehicle::currentViewType;
NwShipVehicle::UpdateFn NwShipVehicle::updateFunctions[3];
NwBoatVehicle::UpdateFn NwBoatVehicle::updateFunction;
static constexpr i8 NwVehicleStepX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
static constexpr i8 NwVehicleStepZ[8] = {0, -1, -1, -1, 0, 1, 1, 1};
static constexpr i32 NwPlaneStartX[4] = {255, 127, 0, 127};
static constexpr i32 NwPlaneStartZ[4] = {127, 255, 127, 0};
static constexpr i32 NwPlanePlaceX[4] = {0, 127, 255, 127};
static constexpr i32 NwPlanePlaceZ[4] = {127, 0, 127, 255};
static constexpr i32 NwPlaneDirectionX[4] = {-1, 0, 1, 0};
static constexpr i32 NwPlaneDirectionZ[4] = {0, -1, 0, 1};
static constexpr u8 NwVehicleRotationOffsets[4] = {0, 8, 16, 24};
static constexpr u8 NwVehicleFrameOffsets[5][32] = {
    {36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38, 38,
     38, 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 39, 36, 36, 36, 36},
    {44, 44, 44, 44, 45, 45, 45, 45, 45, 45, 45, 45, 46, 46, 46, 46,
     46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 47, 47, 44, 44, 44, 44},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
    {40, 40, 40, 40, 41, 41, 41, 41, 41, 41, 41, 41, 42, 42, 42, 42,
     42, 42, 42, 42, 43, 43, 43, 43, 43, 43, 43, 43, 40, 40, 40, 40},
    {32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34,
     34, 34, 34, 34, 35, 35, 35, 35, 35, 35, 35, 35, 32, 32, 32, 32}};
// Installed STRTSIM's four six-step cubic B-spline coefficient tables, retained as exact float values.
static constexpr f32 NwShipSpline[4][6] = {
    {0x1.555582p-3f, 0x1.5d8674p-4f, 0x1.26e978p-5f, 0x1.5d86c4p-7f, 0x1.5d8642p-10f, 0.0f},
    {0x1.555560p-1f, 0x1.42e6c8p-1f, 0x1.13cc2ap-1f, 0x1.a89e78p-2f, 0x1.217376p-2f, 0x1.555582p-3f},
    {0x1.555582p-3f, 0x1.217376p-2f, 0x1.a89e78p-2f, 0x1.13cc2ap-1f, 0x1.42e6c8p-1f, 0x1.555560p-1f},
    {0.0f, 0x1.5d8642p-10f, 0x1.5d86c4p-7f, 0x1.26e978p-5f, 0x1.5d8674p-4f, 0x1.555582p-3f}};

// Original per-action script state; native pointer width controls the record size outside x86.
// Both original point-motion records have thirteen 32-bit fields and may occupy an unaligned state-stack offset.
#pragma pack(push, 1)
struct NwVehicleMoveState {
  f32 progress;
  i32 pathDistance, startX, startAltitude, startZ, endX, endAltitude, endZ;
  f32 directionX, directionAltitude, directionZ;
  i32 length;
  f32 angle;
};
struct NwVehicleFollowerMoveState {
  f32 progress;
  i32 pathDistance, startX, startAltitude, startZ, endX, endAltitude, endZ;
  f32 directionX, directionAltitude, directionZ;
  i32 length;
  f32 angle;
};
struct NwVehicleWaterMoveState {
  f32 progress, length;
  i32 startX, startAltitude, startZ, endX, endAltitude, endZ;
  f32 directionX, directionAltitude, directionZ;
  i32 unused;
  f32 angle;
};
struct NwVehicleTerrainMoveState {
  f32 progress, length;
  i32 height, margin, startX, startZ, endX, endZ;
  f32 directionX, directionZ, altitude, distanceSinceQuery, horizontalSpeed, verticalSpeed;
};
struct NwVehicleCityCircleState {
  i32 centerX, centerZ;
  f32 altitude, radius, angularSpeed, targetAngle, progress, angle, verticalGradient;
};
struct NwVehicleCircleState {
  i32 centerX, centerZ;
  f32 altitude, radius, angularSpeed, targetAngle, progress, angle, verticalGradient;
  i32 height, margin;
  f32 angleSinceQuery;
};
struct NwVehicleSpiralState {
  ScVec3F origin, velocity, position;
  f32 angularSpeed, angle, radius;
  i32 phase, ground;
  u32 orientationElapsed;
};
struct NwBoatRoutineState {
  f32 speed, heading;
  i32 distance, targetDistance;
};
struct NwVehicleStagesState {
  i32 stageCount, stageIndex, unused0, unused1, moving;
  NwVehiclePath *nextPath;
  u16 disappearanceCounter, padding;
};
#pragma pack(pop)
static_assert(sizeof(NwVehicleMoveState) == 52 && sizeof(NwVehicleFollowerMoveState) == 52);
static_assert(sizeof(NwVehicleWaterMoveState) == 52);
static_assert(sizeof(NwVehicleTerrainMoveState) == 56);
static_assert(sizeof(NwVehicleCityCircleState) == 36);
static_assert(sizeof(NwVehicleCircleState) == 48);
static_assert(sizeof(NwVehicleSpiralState) == 60);
static_assert(sizeof(NwBoatRoutineState) == 16);

struct NwVehicleScriptState {
  NwStreetScript *script;
  i16 instructionIndex;
  i32 repeatCount, completedCount;
};

// Original path-clearance wait state; native pointer width controls the record size outside x86.
struct NwVehicleWaitState {
  NwVehiclePath *path;
  u32 elapsed, soundElapsed;
};
ApBoxX *NwStreetVehicle::boxX;
RsManager *NwStreetVehicle::resourceManager;
u8 NwStreetVehicle::staticInitialized;
NwStreetVehicle::UpdateFn NwStreetVehicle::updateFunctions[17];
NwStreetVehicle::ScriptFn NwStreetVehicle::scriptFunctions[4];
NwStreetVehicle::TileFilter NwStreetVehicle::pathTileFilters[3];
i32 NwStreetVehicle::birthZoomLevel = 2;
i32 NwRailVehicle::birthZoomLevel = 2;
i32 NwSubwayVehicle::birthZoomLevel = 2;
i32 NwRailVehicle::currentViewType;
i32 NwSubwayVehicle::currentViewType;
u32 NwHelicopter::cityMidpointX, NwHelicopter::cityMidpointZ;
ScDirtBag *NwHelicopter::dirtBag;
i32 NwHelicopter::currentViewType;
NwHelicopter::UpdateFn NwHelicopter::updateFunctions[2];
i32 NwAeroplane::currentViewType;
NwAeroplane::UpdateFn NwAeroplane::updateFunctions[12];
NwVehiclePathAttrib *NwAeroplane::straightPathAttributes[4], *NwAeroplane::turnPathAttributes[8];
RsManager *NwAeroplane::resourceManager;

u16 NwAeroplane::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction]);
}

u8 NwAeroplane::SubTypeInfo() const {
  return 3;
}

// Initialize the riot crowd's retained interfaces and original animation timing.
NwRiotCrowdObject::NwRiotCrowdObject() : riot(nullptr), transitTile(nullptr), disbursing(0) {
  byte44 = 0;
  animationInterval = kNwCrowdFrameInterval;
  visible = 0;
}

NwRiotCrowdObject::~NwRiotCrowdObject() {
  if (byte32)
    Shutdown();
}

bool NwRiotCrowdObject::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_NwRiotCrowd, NwRiotCrowd);
  return NwStreetVehicle::QueryInterface(iid, out);
}

u32 NwRiotCrowdObject::AddRef() {
  return NwStreetVehicle::AddRef();
}

u32 NwRiotCrowdObject::Release() {
  return NwStreetVehicle::Release();
}

// Forward a fireball impact addressed to this crowd to the associated riot.
bool NwRiotCrowdObject::DoMessage(ScMessage *message) {
  if (message->type != kSpMsg_BuildingImpact)
    return false;
  if (message->data3 == reinterpret_cast<uptr>(this)) {
    u32 coordinates = u32(message->data2);
    riot->IgniteCell(coordinates >> 16, coordinates & 0xffff);
  }
  return true;
}

void NwRiotCrowdObject::Shutdown() {
  if (!byte32)
    return;
  NwGetStreetMessageServer()->RemoveNotification(static_cast<ScOccupant *>(this), kSpMsg_BuildingImpact);
  ScRelease(&transitTile);
  NwStreetVehicle::Shutdown();
  if (riot) {
    riot->RequestRetarget();
    riot->Release();
    riot = nullptr;
  }
}

u32 NwRiotCrowdObject::GetGeomFlag() {
  return kNwCrowdGeometry;
}

u8 NwRiotCrowdObject::SubTypeInfo() const {
  return 8;
}

// Retain the starting transit tile, place the crowd and subscribe to its fireball impact messages.
u8 NwRiotCrowdObject::PlaceOnTransitTile(ScOccupant *occupant) {
  if (!byte32)
    return 0;
  visible = 0;
  transitTile = ScAddRefNotNull(occupant);
  u32 cellX, cellZ;
  transitTile->GetLocationInCellCoords(&cellX, &cellZ);
  x = i32(cellX << kCtMicroCoordinateShift);
  z = i32(cellZ << kCtMicroCoordinateShift);
  u8 terrainAltitude = 0;
  dirtBag->GetVertexAltitude(cellX, cellZ, &terrainAltitude);
  altitude = i32(terrainAltitude) << kCtMicroCoordinateShift;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  ExecuteScript(attributes->scriptKey, 1, 0);
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  NwGetStreetMessageServer()->AddNotification(static_cast<ScOccupant *>(this), kSpMsg_BuildingImpact);
  return 1;
}

void NwRiotCrowdObject::SetAssociatedRiotInstance(DsRiotState *instance) {
  riot = ScAddRefNotNull(instance);
}

void NwRiotCrowdObject::GetLocation(u32 *cellX, u32 *cellZ) {
  *cellX = u32(x >> kCtMicroCoordinateShift);
  *cellZ = u32(z >> kCtMicroCoordinateShift);
}

void NwRiotCrowdObject::GetLocationMicro(i32 *pointX, i32 *pointAltitude, i32 *pointZ) {
  *pointX = x;
  *pointAltitude = altitude;
  *pointZ = z;
}

void NwRiotCrowdObject::Disburse() {
  disbursing = 1;
}

// Configure and resample fireball timing for an initialized crowd.
void NwRiotCrowdObject::SetMinAndMaxTimeBetweenFireballThrows(i32 minimum, i32 maximum) {
  if (byte32) {
    fireballMinimum = minimum;
    fireballMaximum = maximum;
    fireballElapsed = 0;
    nextFireball = u32(NwVehicleRandom.Range(minimum, maximum));
  }
}

u8 NwRiotCrowdObject::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwRiotCrowdObject::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

u16 NwRiotCrowdObject::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = u8((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return u16(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction] + actionDescriptor->frames[frame]);
}

// Remove disbursing crowds or advance their fireball timer before the base vehicle tick.
i32 NwRiotCrowdObject::Tick(u32 elapsed) {
  if (disbursing && byte32) {
    animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
    return 0;
  }
  fireballElapsed += elapsed;
  if (fireballElapsed >= nextFireball) {
    fireballElapsed = 0;
    ThrowFireBall();
    nextFireball = u32(NwVehicleRandom.Range(fireballMinimum, fireballMaximum));
  }
  return NwStreetVehicle::Tick(elapsed);
}

// Launch a trajectory from a random in-bounds point along the crowd's current heading.
void NwRiotCrowdObject::ThrowFireBall() {
  SpTrajectoryView *trajectory = nullptr;
  if (!NwGetStreetDirector()->GZCOM()->GetClassObject(
          kSpTrajectoryViewClassId, IID_SpTrajectoryView, reinterpret_cast<void **>(&trajectory)) ||
      !trajectory)
    return;
  trajectory->SetSourceSignature(u32(reinterpret_cast<uptr>(this)));
  i32 endX = i32(u32(x) + u32(i64(std::cos(f64(angle)) * f64(attributes->length))));
  i32 endZ = i32(u32(z) + u32(i64(std::sin(f64(angle)) * f64(attributes->length))));
  i32 targetX = x < endX ? NwVehicleRandom.Range(x, endX) : x > endX ? NwVehicleRandom.Range(endX, x) : x;
  i32 targetZ = z < endZ ? NwVehicleRandom.Range(z, endZ) : z > endZ ? NwVehicleRandom.Range(endZ, z) : z;
  if (targetX < 0 || targetX >= cityXMicroUnits || targetZ < 0 || targetZ >= cityZMicroUnits)
    return;
  f64 heading = NwVehicleRandom.RandomDoubleRangeUniform(0.0, f64(6.2831855f));
  ScVec3F velocity{f32(std::cos(heading) * 1.2), 0.2f, f32(std::sin(heading) * 1.2)};
  trajectory->Init(targetX, targetZ, i32(u32(altitude) + 256u), velocity, 1);
  trajectory->SetTrajectoryType(SpTrajectoryView::Motion::Decelerating, nullptr);
  trajectory->Release();
}

// Install the riot script and action handlers in their original table order.
u8 NwRiotCrowdObject::StaticInit() {
  scriptFunctions[0] = &NwRiotCrowdObject::RiotCrowdMovementScriptFn;
  updateFunctions[0] = &NwRiotCrowdObject::RiotCrowdMovementUpdateFn;
  updateFunctions[1] = &NwRiotCrowdObject::EnterPathInVehicleLessTileUpdateFn;
  return 1;
}

// Clear the two riot action handlers at module shutdown.
u8 NwRiotCrowdObject::StaticShutdown() {
  updateFunctions[0] = nullptr;
  updateFunctions[1] = nullptr;
  return 1;
}

void NwRiotCrowdObject::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

// Dispatch base vehicle actions and the two riot-specific actions from their respective tables.
i32 NwRiotCrowdObject::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  u32 action = actionStack[actionStackTop];
  if (action <= 16)
    return NwStreetVehicle::ExecuteCurrentUpdateFn(elapsed, status, cancel);
  return (this->*updateFunctions[action - 17])(elapsed, status, cancel);
}

i32 NwRiotCrowdObject::CallScriptFunction(NwScriptInstruction *instruction) {
  return (this->*scriptFunctions[instruction->opcode - 200])(instruction);
}

void NwRiotCrowdObject::LeaveLastPath() {
  NwVehiclePath *path = paths[pathTop];
  path->GetStrtSimTrBlock()->AllowVehicleEntrance();
  i32 pointX, pointAltitude, pointZ;
  path->GetStartPoint(&pointX, &pointAltitude, &pointZ);
  if (riot)
    riot->LeaveCell(u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift));
  NwStreetVehicle::LeaveLastPath();
}

// Close a traffic block and notify the riot after entering its path.
void NwRiotCrowdObject::MakeCurrentPath(NwVehiclePath *path) {
  NwStreetVehicle::MakeCurrentPath(path);
  path->GetStrtSimTrBlock()->DisallowVehicleEntrance();
  i32 pointX, pointAltitude, pointZ;
  path->GetStartPoint(&pointX, &pointAltitude, &pointZ);
  if (riot)
    riot->EnterCell(u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift));
}

// Schedule entry to a traffic path after its vehicle occupants have cleared.
i32 NwRiotCrowdObject::EnterPathInVehicleLessTile(NwVehiclePath *path) {
  i32 offset = stateStackTop;
  stateStackTop += sizeof(path);
  std::memcpy(stateStack + offset, &path, sizeof(path));
  PushToActionStack(18);
  return 2;
}

// Wait to enter the stored path, releasing it on cancellation or adopting it after successful entry.
i32 NwRiotCrowdObject::EnterPathInVehicleLessTileUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - sizeof(NwVehiclePath *);
  NwVehiclePath *path;
  std::memcpy(&path, stateStack + offset, sizeof(path));
  i32 result = 0;
  if (cancel) {
    path->Release();
    path = nullptr;
    std::memcpy(stateStack + offset, &path, sizeof(path));
  } else if (!path->ForceEnterVehicle(static_cast<NwVehicle *>(this))) {
    result = 1;
  } else {
    visible = 1;
    MakeCurrentPath(path);
  }
  stateStackTop = offset;
  return result;
}

// Choose a starting road path, position the crowd and schedule its path-entry and movement actions.
i32 NwRiotCrowdObject::RiotCrowdMovement() {
  NwVehiclePath *path = SelectPath(transitTile);
  if (!path)
    return 1;
  path->SetSpeed(0.000029999999f);
  CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  path->GetStartPoint(&position.x, &position.altitude, &position.z);
  static_cast<NwVehicle *>(this)->SetOrientation(path->GetPlacingOrientation());
  static_cast<NwVehicle *>(this)->ChangePosition(&position);
  i32 offset = stateStackTop, stage = 0;
  stateStackTop += sizeof(stage);
  std::memcpy(stateStack + offset, &stage, sizeof(stage));
  PushToActionStack(17);
  EnterPathInVehicleLessTile(path);
  return 2;
}

// Continue crowd movement across paths until cancellation or the road path ends.
i32 NwRiotCrowdObject::RiotCrowdMovementUpdateFn(u32, i32 status, bool cancel) {
  i32 offset = stateStackTop - 4, stage;
  std::memcpy(&stage, stateStack + offset, sizeof(stage));
  if (cancel || status == 1) {
    stateStackTop = offset;
    return cancel ? 0 : 1;
  }
  if (!stage) {
    visible = 1;
    stage = 1;
    std::memcpy(stateStack + offset, &stage, sizeof(stage));
    MoveAlongPath();
    return 2;
  }
  NwVehiclePath *next = nullptr;
  SelectNextPath(paths[0], &next);
  if (!next) {
    stateStackTop = offset;
    return 1;
  }
  next->SetSpeed(0.000029999999f);
  next->GetStrtSimTrBlock()->DisallowVehicleEntrance();
  stage = 0;
  std::memcpy(stateStack + offset, &stage, sizeof(stage));
  EnterPathInVehicleLessTile(next);
  return 2;
}

i32 NwRiotCrowdObject::RiotCrowdMovementScriptFn(NwScriptInstruction *) {
  return RiotCrowdMovement();
}

// Initialize the parade block's retained interfaces and original animation timing.
NwParadeBlockObject::NwParadeBlockObject() : parade(nullptr), transitTile(nullptr), disbursing(0), bored(0) {
  byte44 = 0;
  animationInterval = kNwCrowdFrameInterval;
  visible = 0;
}

NwParadeBlockObject::~NwParadeBlockObject() {
  if (byte32)
    Shutdown();
}

bool NwParadeBlockObject::DoMessage(ScMessage *) {
  return false;
}

bool NwParadeBlockObject::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_NwParadeBlock, NwParadeBlock);
  return NwStreetVehicle::QueryInterface(iid, out);
}

u32 NwParadeBlockObject::AddRef() {
  return NwStreetVehicle::AddRef();
}

u32 NwParadeBlockObject::Release() {
  return NwStreetVehicle::Release();
}

// Release the starting tile and parade callback around the base vehicle shutdown.
void NwParadeBlockObject::Shutdown() {
  if (!byte32)
    return;
  ScRelease(&transitTile);
  NwStreetVehicle::Shutdown();
  if (parade) {
    parade->RequestRetarget();
    parade->Release();
    parade = nullptr;
  }
}

i32 NwParadeBlockObject::Tick(u32 elapsed) {
  if (parade)
    parade->AdvanceTime(elapsed);
  if (disbursing && byte32) {
    animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
    return 0;
  }
  return NwStreetVehicle::Tick(elapsed);
}

u32 NwParadeBlockObject::GetGeomFlag() {
  return kNwParadeBlockGeometry;
}

u8 NwParadeBlockObject::SubTypeInfo() const {
  return 11;
}

u8 NwParadeBlockObject::PlaceOnTransitTile(ScOccupant *occupant) {
  if (!byte32)
    return 0;
  visible = 0;
  transitTile = ScAddRefNotNull(occupant);
  u32 cellX, cellZ;
  transitTile->GetLocationInCellCoords(&cellX, &cellZ);
  x = i32(cellX << kCtMicroCoordinateShift);
  z = i32(cellZ << kCtMicroCoordinateShift);
  u8 terrainAltitude = 0;
  dirtBag->GetVertexAltitude(cellX, cellZ, &terrainAltitude);
  altitude = i32(terrainAltitude) << kCtMicroCoordinateShift;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  ExecuteScript(attributes->scriptKey, 1, 0);
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

// Place a parade block on a path matching its requested starting direction.
u8 NwParadeBlockObject::PlaceOnTransitTile(ScOccupant *occupant, u8 direction) {
  if (!byte32)
    return 0;
  visible = 0;
  transitTile = ScAddRefNotNull(occupant);
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  static_cast<NwVehicle *>(this)->SetOrientation(direction);
  NwConnection *connection;
  if (!occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection)))
    return 0;
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
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
  static_cast<NwVehicle *>(this)->SetOrientation(edge->startOrientation);
  speed = edge->timeScale;
  AppearOnPath(path);
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  ExecuteScript(attributes->scriptKey, 1, 0);
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

void NwParadeBlockObject::SetAssociatedParadeInstance(DsParadeMotion *instance) {
  parade = ScAddRefNotNull(instance);
}

void NwParadeBlockObject::GetLocation(u32 *cellX, u32 *cellZ) {
  *cellX = u32(x >> kCtMicroCoordinateShift);
  *cellZ = u32(z >> kCtMicroCoordinateShift);
}

// Return the block's micro coordinates and altitude.
// Windows folds this body with the riot crowd coordinate getter.
void NwParadeBlockObject::GetLocationMicro(i32 *pointX, i32 *pointAltitude, i32 *pointZ) {
  *pointX = x;
  *pointAltitude = altitude;
  *pointZ = z;
}

void NwParadeBlockObject::Disburse() {
  disbursing = 1;
}

u8 NwParadeBlockObject::IsBored() {
  return bored;
}

u8 NwParadeBlockObject::GetOrientation() {
  return orientation;
}

u16 NwParadeBlockObject::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = u8((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return u16(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction] + actionDescriptor->frames[frame]);
}

u8 NwParadeBlockObject::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), 0, clip);
}

u8 NwParadeBlockObject::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), 0, clip);
}

// Dispatch base vehicle actions and the two parade-specific actions from their respective tables.
i32 NwParadeBlockObject::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  u32 action = actionStack[actionStackTop];
  if (action <= 16)
    return NwStreetVehicle::ExecuteCurrentUpdateFn(elapsed, status, cancel);
  return (this->*updateFunctions[action - 17])(elapsed, status, cancel);
}

i32 NwParadeBlockObject::CallScriptFunction(NwScriptInstruction *instruction) {
  return (this->*scriptFunctions[instruction->opcode - 200])(instruction);
}

// Select a path from the starting transit tile using the block's current direction.
NwVehiclePath *NwParadeBlockObject::SelectPath(ScOccupant *occupant) {
  NwConnection *connection;
  occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection));
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  NwTrafficAttrib *blockAttributes = block->GetStrtSimAttrib();
  u8 cellAltitude = GetAltitude(occupant, blockAttributes);
  NwVehicleGraphNode *node = nullptr;
  NwVehicleGraphEdge *edge = nullptr;
  blockAttributes->vehicleGraph->SelectANodeAndInternalEdgeOfOrientation(&node, &edge, occupantManagerIndex, orientation);
  NwVehiclePath *path = nullptr;
  if (node && edge) {
    edge = SelectAnEdge(node, 0, 0);
    path = BuildPath(block, cellX, cellAltitude, cellZ, edge);
  }
  connection->Release();
  return path;
}

// Prefer an outgoing edge that keeps the current orientation, falling back to the first edge.
NwVehicleGraphEdge *NwParadeBlockObject::SelectAnEdge(NwVehicleGraphNode *node, i32, i32) {
  NwVehicleGraphEdge *edges[6];
  u32 count = 6;
  node->GetOutgoingEdges(edges, &count);
  if (!count)
    return nullptr;
  for (u8 i = 0; i < count; ++i)
    if (edges[i]->startOrientation == edges[i]->endOrientation && edges[i]->startOrientation == orientation)
      return edges[i];
  return edges[0];
}

u32 NwParadeBlockObject::SetOrientation(u8 value) {
  return NwStreetVehicle::SetOrientation(u8((value + 4) & 0xf8));
}

// Center the parade block on the axis perpendicular to its cardinal heading.
u8 NwParadeBlockObject::ChangePosition(CtCoord *position) {
  CtCoord adjusted = *position;
  if (orientation == 0 || orientation == 16)
    adjusted.z = i32((u32(adjusted.z) & kCtMicroCellMask) + 128u);
  else if (orientation == 8 || orientation == 24)
    adjusted.x = i32((u32(adjusted.x) & kCtMicroCellMask) + 128u);
  return NwStreetVehicle::ChangePosition(&adjusted);
}

void NwParadeBlockObject::LeaveLastPath() {
  NwVehiclePath *path = paths[pathTop];
  i32 pointX, pointAltitude, pointZ;
  path->GetStartPoint(&pointX, &pointAltitude, &pointZ);
  if (parade)
    parade->LeaveCell(u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift));
  NwStreetVehicle::LeaveLastPath();
}

void NwParadeBlockObject::MakeCurrentPath(NwVehiclePath *path) {
  NwStreetVehicle::MakeCurrentPath(path);
  i32 pointX, pointAltitude, pointZ;
  path->GetStartPoint(&pointX, &pointAltitude, &pointZ);
  ScOccupant *occupant = nullptr;
  if (NwGetStreetCity()->SurfaceOccupantManager()->GetOccupantAtCell(
          u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift), &occupant)) {
    if (occupant->CanUserRemove() || occupant->CanSimRemove())
      bored = 1;
    occupant->Release();
  }
  if (parade)
    parade->EnterCell(u32(pointX >> kCtMicroCoordinateShift), u32(pointZ >> kCtMicroCoordinateShift));
}

// Install the parade script and action handlers in their original table order.
u8 NwParadeBlockObject::StaticInit() {
  scriptFunctions[0] = &NwParadeBlockObject::ParadeBlockMovementScriptFn;
  updateFunctions[0] = &NwParadeBlockObject::ParadeBlockMovementUpdateFn;
  updateFunctions[1] = &NwParadeBlockObject::EnterPathInVehicleLessTileUpdateFn;
  return 1;
}

// Clear the two parade action handlers at module shutdown.
u8 NwParadeBlockObject::StaticShutdown() {
  updateFunctions[0] = nullptr;
  updateFunctions[1] = nullptr;
  return 1;
}

void NwParadeBlockObject::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

// Schedule entry to a traffic path after its vehicle occupants have cleared.
// Windows folds this body with the riot crowd's matching path-entry action.
i32 NwParadeBlockObject::EnterPathInVehicleLessTile(NwVehiclePath *path) {
  i32 offset = stateStackTop;
  stateStackTop += sizeof(path);
  std::memcpy(stateStack + offset, &path, sizeof(path));
  PushToActionStack(18);
  return 2;
}

// Wait to enter the stored path, releasing it on cancellation or adopting it after successful entry.
// Windows folds this body with the riot crowd's matching path-entry action.
i32 NwParadeBlockObject::EnterPathInVehicleLessTileUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - sizeof(NwVehiclePath *);
  NwVehiclePath *path;
  std::memcpy(&path, stateStack + offset, sizeof(path));
  i32 result = 0;
  if (cancel) {
    path->Release();
    path = nullptr;
    std::memcpy(stateStack + offset, &path, sizeof(path));
  } else if (!path->ForceEnterVehicle(static_cast<NwVehicle *>(this))) {
    result = 1;
  } else {
    visible = 1;
    MakeCurrentPath(path);
  }
  stateStackTop = offset;
  return result;
}

// Choose a starting path and schedule movement unless the parade block is already bored.
i32 NwParadeBlockObject::ParadeBlockMovement() {
  if (bored)
    return 1;
  NwVehiclePath *path = SelectPath(transitTile);
  if (!path)
    return 1;
  path->SetSpeed(0.000029999999f);
  CtCoord position{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  path->GetStartPoint(&position.x, &position.altitude, &position.z);
  static_cast<NwVehicle *>(this)->SetOrientation(path->GetPlacingOrientation());
  static_cast<NwVehicle *>(this)->ChangePosition(&position);
  i32 offset = stateStackTop, stage = 0;
  stateStackTop += sizeof(stage);
  std::memcpy(stateStack + offset, &stage, sizeof(stage));
  PushToActionStack(17);
  EnterPathInVehicleLessTile(path);
  return 2;
}

// Continue across straight paths and stop the parade when the next path turns.
i32 NwParadeBlockObject::ParadeBlockMovementUpdateFn(u32, i32 status, bool cancel) {
  i32 offset = stateStackTop - 4, stage;
  std::memcpy(&stage, stateStack + offset, sizeof(stage));
  if (cancel || status == 1) {
    stateStackTop = offset;
    return cancel ? 0 : 1;
  }
  if (!stage) {
    visible = 1;
    stage = 1;
    std::memcpy(stateStack + offset, &stage, sizeof(stage));
    MoveAlongPath();
    return 2;
  }
  NwVehiclePath *next = nullptr;
  SelectNextPath(paths[0], &next);
  if (!next) {
    stateStackTop = offset;
    return 1;
  }
  if (next->GetPlacingOrientation() != next->GetEndOrientation()) {
    next->Release();
    stateStackTop = offset;
    bored = 1;
    return 1;
  }
  next->SetSpeed(0.000029999999f);
  stage = 0;
  std::memcpy(stateStack + offset, &stage, sizeof(stage));
  EnterPathInVehicleLessTile(next);
  return 2;
}

i32 NwParadeBlockObject::ParadeBlockMovementScriptFn(NwScriptInstruction *) {
  return ParadeBlockMovement();
}

NwStrikeCrowd::NwStrikeCrowd() : transitTile(nullptr) {
  byte44 = 0;
  animationInterval = kNwStrikeCrowdFrameInterval;
}

NwStrikeCrowd::~NwStrikeCrowd() {
  if (byte32)
    Shutdown();
}

// Restore the retained tile's removal flags and release it before vehicle shutdown.
void NwStrikeCrowd::Shutdown() {
  if (!byte32)
    return;
  if (transitTile) {
    transitTile->SetSimRemove(true);
    transitTile->SetUserRemove(true);
    transitTile->Release();
    transitTile = nullptr;
  }
  NwStreetVehicle::Shutdown();
}

u32 NwStrikeCrowd::GetGeomFlag() {
  return kNwCrowdGeometry;
}

// Retain the tile and protect it from removal after a strike crowd has entered its path.
u8 NwStrikeCrowd::PlaceOnTransitTile(ScOccupant *occupant) {
  transitTile = ScAddRefNotNull(occupant);
  if (StrikeCrowdRoutine() == 1) {
    transitTile->Release();
    transitTile = nullptr;
    return 0;
  }
  transitTile->SetSimRemove(false);
  transitTile->SetUserRemove(false);
  return animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
}

u8 NwStrikeCrowd::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwStrikeCrowd::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

u16 NwStrikeCrowd::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = u8((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return u16(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction] + actionDescriptor->frames[frame]);
}

i32 NwStrikeCrowd::EnterPathInTile(NwVehiclePath *path) {
  if (!path->ForceEnterVehicle(static_cast<NwVehicle *>(this)))
    return 1;
  MakeCurrentPath(path);
  path->GetStrtSimTrBlock()->DisallowVehicleEntrance();
  return 0;
}

void NwStrikeCrowd::LeaveLastPath() {
  paths[pathTop]->GetStrtSimTrBlock()->AllowVehicleEntrance();
  NwStreetVehicle::LeaveLastPath();
}

// Select and enter a path on the protected tile, then keep the crowd in its stationary action.
i32 NwStrikeCrowd::StrikeCrowdRoutine() {
  NwVehiclePath *path = SelectPath(transitTile);
  if (!path)
    return 1;
  path->GetEndPoint(&x, &altitude, &z);
  static_cast<NwVehicle *>(this)->SetOrientation(path->GetPlacingOrientation());
  if (EnterPathInTile(path) == 1) {
    path->Release();
    return 1;
  }
  DoNothing();
  return 2;
}

u8 NwAeroplane::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwAeroplane::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwHelicopter::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

u16 NwHelicopter::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>(zoomOffsets[zoom] + NwVehicleFrameOffsets[pitch][direction] + actionDescriptor->frames[frame]);
}

u8 NwHelicopter::SubTypeInfo() const {
  return 5;
}

u8 NwHelicopter::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (!visible || zoom < 2 || currentViewType)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

// Initialize the aeroplane's target list, manager pointer and original geometry flags.
NwAeroplane::NwAeroplane() : manager(nullptr) {
  normalRoutine = 1;
  byte44 = 0;
  geometryFlags = kNwAirVehicleGeometry;
}

// Initialize aeroplane action dispatch and retain the original straight and turning path resources.
u8 NwAeroplane::StaticInit() {
  for (auto &function : updateFunctions)
    function = nullptr;
  updateFunctions[0] = &NwAeroplane::DoPlaneRoutineUpdateFn;
  updateFunctions[1] = &NwAeroplane::DescendUpdateFn;
  updateFunctions[2] = &NwAeroplane::GetPermissionForLandingUpdateFn;
  updateFunctions[3] = &NwAeroplane::TouchDownUpdateFn;
  updateFunctions[4] = &NwAeroplane::SlowDownOnRunwayUpdateFn;
  updateFunctions[5] = &NwAeroplane::TurnIntoTaxiwayUpdateFn;
  updateFunctions[6] = &NwAeroplane::MoveAlongTaxiwayUpdateFn;
  updateFunctions[7] = &NwAeroplane::GetPermissionToTakeOffUpdateFn;
  updateFunctions[8] = &NwAeroplane::TurnIntoRunwayUpdateFn;
  updateFunctions[9] = &NwAeroplane::SpeedUpOnRunwayUpdateFn;
  updateFunctions[10] = &NwAeroplane::TakeOffUpdateFn;
  updateFunctions[11] = &NwAeroplane::DisasterFightingRoutineUpdateFn;
  resourceManager = ScAddRefNotNull(NwGetStreetResourceManager());
  const u32 kNwAeroplaneStraightPathIds[4] = {5120001, 5120002, 5120003, 5120000};
  const u32 kNwAeroplaneTurnPathIds[8] = {5120258, 5120259, 5120256, 5120257, 5120514, 5120515, 5120512, 5120513};
  RsKey key(kNwVehiclePathResourceType, kNwVehiclePathResourceGroup, 0);
  RsResource *resource = nullptr;
  for (u32 index = 0; index < 4; ++index) {
    key.instance = kNwAeroplaneStraightPathIds[index];
    NwStreetLayer::GetVclPathAttrib(key, &resource);
    straightPathAttributes[index] = static_cast<NwVehiclePathAttrib *>(resource);
  }
  for (u32 index = 0; index < 8; ++index) {
    key.instance = kNwAeroplaneTurnPathIds[index];
    NwStreetLayer::GetVclPathAttrib(key, &resource);
    turnPathAttributes[index] = static_cast<NwVehiclePathAttrib *>(resource);
  }
  return 1;
}

u8 NwAeroplane::StaticShutdown() {
  for (auto &resource : straightPathAttributes) {
    if (resource)
      resource->Release();
    resource = nullptr;
  }
  for (auto &resource : turnPathAttributes) {
    if (resource)
      resource->Release();
    resource = nullptr;
  }
  ScRelease(&resourceManager);
  return 1;
}

// Dispatch aeroplane-specific actions after the base vehicle's original seventeen actions.
i32 NwAeroplane::ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) {
  u32 action = actionStack[actionStackTop];
  if (action <= 16)
    return NwStreetVehicle::ExecuteCurrentUpdateFn(elapsed, status, cancel);
  return (this->*updateFunctions[action - 17])(elapsed, status, cancel);
}

i32 NwAeroplane::ApproachAirport() {
  MoveAlongPath();
  return 2;
}

i32 NwAeroplane::Descend() {
  PushToActionStack(18);
  return 2;
}

i32 NwAeroplane::DoPlaneRoutine() {
  PushToActionStack(17);
  return 2;
}

// Advance the airport flight sequence, releasing runway and taxiway reservations on cancellation.
i32 NwAeroplane::DoPlaneRoutineUpdateFn(u32, i32 status, bool cancel) {
  if (cancel) {
    manager->RelinquishRunway(this);
    manager->PlaneLeftTaxiway(this);
    return 0;
  }
  if (status == 2)
    return 2;
  while (status != 1) {
    i32 action = planeAction;
    if (action == 15)
      return 0;
    planeAction = action + 1;
    switch (action) {
      case 0: status = ApproachAirport(); break;
      case 1: status = Descend(); break;
      case 2: status = GetPermissionForLanding(); break;
      case 3: status = TouchDown(); break;
      case 4: status = SlowDownOnRunway(); break;
      case 5: status = TurnIntoTaxiway(); break;
      case 6: status = MoveAlongTaxiway(); break;
      case 7:
        status = IdleForDuration(
            static_cast<u32>(NwVehicleRandom.Range(kNwPlaneFirstIdleMinimum, kNwPlaneFirstIdleMaximum)));
        break;
      case 8: status = GetPermissionToTakeOff(); break;
      case 9: status = TurnIntoRunway(); break;
      case 10:
        status = IdleForDuration(
            static_cast<u32>(NwVehicleRandom.Range(kNwPlaneSecondIdleMinimum, kNwPlaneSecondIdleMaximum)));
        break;
      case 11: status = SpeedUpOnRunway(); break;
      case 12: status = TakeOff(); break;
      case 13: status = DoCircularMotionOnTakeOff(); break;
      case 14: status = MoveToNeighbor(); break;
    }
    if (status == 2)
      return 2;
  }
  return 1;
}

// Leave the runway path and circle around the airport after takeoff.
i32 NwAeroplane::DoCircularMotionOnTakeOff() {
  LeaveAllPaths();
  f32 turnAngle = static_cast<f32>(NwVehicleRandom.RandomDoubleRangeUniform(3.1415927f, 9.424778f));
  u32 cellX, cellZ;
  manager->GetRunwayBegin(&cellX, &cellZ);
  CircularMotionInCity(static_cast<i32>(cellX << kCtMicroCoordinateShift),
      static_cast<i32>(cellZ << kCtMicroCoordinateShift), turnAngle, 0.00053349999f, 2000.0f);
  return 2;
}

// Circle toward the airport until the aircraft reaches its approach altitude.
i32 NwAeroplane::DescendUpdateFn(u32, i32, bool cancel) {
  if (cancel)
    return 0;
  i32 runwayAltitude = manager->MicroYAtRunwayBegin();
  i32 difference = static_cast<i32>(u32(runwayAltitude) - u32(altitude) + 13500u);
  i64 magnitude = difference < 0 ? -i64(difference) : i64(difference);
  if (magnitude < 200)
    return 0;
  u32 cellX, cellZ;
  manager->GetRunwayBegin(&cellX, &cellZ);
  CircularMotionInCity(static_cast<i32>(cellX << kCtMicroCoordinateShift),
      static_cast<i32>(cellZ << kCtMicroCoordinateShift), 6.2831855f, 0.00053349999f, static_cast<f32>(difference));
  return 2;
}

i32 NwAeroplane::GetPermissionForLanding() {
  PushToActionStack(19);
  return 2;
}

i32 NwAeroplane::GetPermissionForLandingUpdateFn(u32, i32, bool cancel) {
  if (cancel || manager->ReserveRunway(this))
    return 0;
  u32 cellX, cellZ;
  manager->GetRunwayBegin(&cellX, &cellZ);
  CircularMotionInCity(static_cast<i32>(cellX << kCtMicroCoordinateShift),
      static_cast<i32>(cellZ << kCtMicroCoordinateShift), 6.2831855f, 0.00053349999f, 0.0f);
  return 2;
}

// Schedule a timed runway-reservation request for takeoff.
i32 NwAeroplane::GetPermissionToTakeOff() {
  i32 offset = stateStackTop;
  stateStackTop += 4;
  u32 elapsed = 0;
  std::memcpy(stateStack + offset, &elapsed, sizeof(elapsed));
  PushToActionStack(24);
  return 2;
}

// Wait for the takeoff delay and reserve the runway before completing the action.
i32 NwAeroplane::GetPermissionToTakeOffUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 offset = stateStackTop - 4;
  if (!cancel) {
    u32 accumulated;
    std::memcpy(&accumulated, stateStack + offset, sizeof(accumulated));
    if (accumulated <= kNwTakeoffPermissionWaitLimit) {
      accumulated += elapsed;
      std::memcpy(stateStack + offset, &accumulated, sizeof(accumulated));
      return 2;
    }
    if (!manager->ReserveRunway(this))
      return 2;
  }
  stateStackTop -= 4;
  return 0;
}

i32 NwAeroplane::TakeOff() {
  i32 offset = stateStackTop;
  stateStackTop += 16;
  u8 direction = !manager->IsRunwayInXDirection();
  i32 stage = 0;
  std::memcpy(stateStack + offset + 12, &direction, sizeof(direction));
  std::memcpy(stateStack + offset + 4, &stage, sizeof(stage));
  PushToActionStack(27);
  return 2;
}

// Place an arriving aeroplane and schedule its approach to the runway.
u8 NwAeroplane::MakeArrivingPlane(i32 pointX, u32 pointZ) {
  planeAction = 0;
  x = pointX;
  z = static_cast<i32>(pointZ);
  altitude = static_cast<i32>(u32(manager->MicroYAtRunwayBegin()) + 16000u);
  u32 cellX, cellZ;
  u8 result = manager->GetLandingBeginCell(&cellX, &cellZ, 9);
  if (!result)
    return result;
  i32 endX = static_cast<i32>((cellX << kCtMicroCoordinateShift) + 128u),
      endZ = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + 128u);
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, altitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00053349999f);
  MakeCurrentPath(path);
  static_cast<NwVehicle *>(this)->SetOrientation(path->GetEndOrientation());
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  actionDescriptor = animation->GetDefaultActionDescriptor();
  DoPlaneRoutine();
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

u8 NwAeroplane::MakeDepartingPlane(u32 cellX, u32 cellZ, u8 direction) {
  planeAction = 6;
  u8 terrainAltitude = 0;
  dirtBag->GetVertexAltitude(cellX, cellZ, &terrainAltitude);
  altitude = i32(terrainAltitude) << kCtMicroCoordinateShift;
  u32 index = direction >> 3;
  x = static_cast<i32>((cellX << kCtMicroCoordinateShift) + u32(NwPlanePlaceX[index]));
  z = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + u32(NwPlanePlaceZ[index]));
  static_cast<NwVehicle *>(this)->SetOrientation(direction);
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  actionDescriptor = animation->GetDefaultActionDescriptor();
  DoPlaneRoutine();
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

// Select the aircraft's disaster animation and insert it after scheduling its mission.
u8 NwAeroplane::MakeDisasterFightingPlane(i32 startX, i32 startZ, i32 targetX, i32 targetZ) {
  normalRoutine = 0;
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetDefaultActionDescriptor();
  DisasterFightingRoutine(u32(startX), u32(startZ), u32(targetX), u32(targetZ));
  animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  return 1;
}

// Initialize a disaster flight at terrain height and schedule movement toward an in-bounds target.
i32 NwAeroplane::DisasterFightingRoutine(u32 startX, u32 startZ, u32 targetX, u32 targetZ) {
  i32 offset = stateStackTop;
  stateStackTop += 32;
  dispatchEnded = 0;
  i32 initialX = static_cast<i32>(startX << kCtMicroCoordinateShift),
      initialZ = static_cast<i32>(startZ << kCtMicroCoordinateShift), zero = 0;
  std::memcpy(stateStack + offset, &initialX, sizeof(initialX));
  std::memcpy(stateStack + offset + 4, &initialZ, sizeof(initialZ));
  std::memcpy(stateStack + offset + 8, &zero, sizeof(zero));
  std::memcpy(stateStack + offset + 24, &zero, sizeof(zero));
  x = initialX;
  z = initialZ;
  altitude = ComputeYForTerrainFollowing(x, z, 2560, 512);
  i32 endX = static_cast<i32>(targetX << kCtMicroCoordinateShift),
      endZ = static_cast<i32>(targetZ << kCtMicroCoordinateShift);
  if (endX < 0 || endX >= cityXMicroUnits || endZ < 0 || endZ >= cityZMicroUnits) {
    stateStackTop -= 32;
    return 1;
  }
  f64 heading = CalculateAngle(x, z, endX, endZ);
  std::memcpy(stateStack + offset + 16, &heading, sizeof(heading));
  PushToActionStack(28);
  FollowTerrainMoveToPoint(endX, endZ, 2560, 512, 0.001f);
  return 2;
}

// Advance the disaster flight, post its attack positions, and return to the starting point on completion.
i32 NwAeroplane::DisasterFightingRoutineUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 32, startX, startZ, stage;
  std::memcpy(&startX, stateStack + offset, sizeof(startX));
  std::memcpy(&startZ, stateStack + offset + 4, sizeof(startZ));
  std::memcpy(&stage, stateStack + offset + 24, sizeof(stage));
  if (cancel || stage == 3) {
    stateStackTop -= 32;
    return 0;
  }
  if (dispatchEnded) {
    if (x < 0 || x >= cityXMicroUnits || z < 0 || z >= cityZMicroUnits) {
      stateStackTop -= 32;
      return 0;
    }
    stage = 3;
    std::memcpy(stateStack + offset + 24, &stage, sizeof(stage));
    FollowTerrainMoveToPoint(startX, startZ, 2560, 512, 0.001f);
    return 2;
  }
  if (stage == 0) {
    i32 steps = 0;
    stage = 1;
    std::memcpy(stateStack + offset + 8, &steps, sizeof(steps));
    std::memcpy(stateStack + offset + 24, &stage, sizeof(stage));
  } else if (stage == 1) {
    ScMessage message(kSpMsg_SprayTrailContinued, u32(x), u32(z), u32(altitude));
    NwGetStreetMessageServer()->MessagePost(message, false);
    f64 heading;
    u32 steps;
    std::memcpy(&heading, stateStack + offset + 16, sizeof(heading));
    std::memcpy(&steps, stateStack + offset + 8, sizeof(steps));
    ++steps;
    std::memcpy(stateStack + offset + 8, &steps, sizeof(steps));
    i32 nextX = i32(u32(x) - u32(i32(std::cos(heading) * -512.0)));
    i32 nextZ = i32(u32(z) - u32(i32(std::sin(heading) * -512.0)));
    if (nextX < 0 || nextX >= cityXMicroUnits || nextZ < 0 || nextZ >= cityZMicroUnits || steps >= 5) {
      stage = 2;
      std::memcpy(stateStack + offset + 24, &stage, sizeof(stage));
    } else {
      FollowTerrainMoveToPoint(nextX, nextZ, 2560, 512, 0.001f);
    }
  } else if (stage == 2) {
    if (targets.empty()) {
      if (!IsVisible())
        return 2;
      i32 dx = i32(u32(startX) - u32(x)), dz = i32(u32(startZ) - u32(z));
      i32 squared = i32(u32(dx) * u32(dx) + u32(dz) * u32(dz));
      if (i32(std::sqrt(f64(squared))) >= 100) {
        f64 heading = std::atan2(f64(dz), f64(dx));
        i32 nextX = i32(u32(x) - u32(i32(std::cos(heading) * -100.0)));
        i32 nextZ = i32(u32(z) - u32(i32(std::sin(heading) * -100.0)));
        FollowTerrainMoveToPoint(nextX, nextZ, 2560, 512, 0.001f);
      } else {
        SetVisibility(false);
      }
    } else {
      if (!IsVisible())
        SetVisibility(true);
      NwAirTarget target = targets.front();
      targets.pop_front();
      stage = 0;
      std::memcpy(stateStack + offset + 24, &stage, sizeof(stage));
      f64 heading = CalculateAngle(x, z, i32(target.x), i32(target.z));
      std::memcpy(stateStack + offset + 16, &heading, sizeof(heading));
      FollowTerrainMoveToPoint(i32(target.x), i32(target.z), 2560, 512, 0.001f);
    }
  }
  return 2;
}

// Schedule a faster straight path to the end of the runway for deceleration.
i32 NwAeroplane::SlowDownOnRunway() {
  u32 cellX, cellZ;
  manager->GetRunwayEnd(&cellX, &cellZ);
  u32 direction = !manager->IsRunwayInXDirection();
  i32 endX = static_cast<i32>((cellX << kCtMicroCoordinateShift) + u32(NwPlaneStartX[direction]));
  i32 endZ = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + u32(NwPlaneStartZ[direction]));
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, altitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00106699998f);
  PushToActionStack(21);
  MakeCurrentPath(path);
  MoveAlongPath();
  return 2;
}

// Schedule a faster straight path to the end of the runway for acceleration.
i32 NwAeroplane::SpeedUpOnRunway() {
  u32 cellX, cellZ;
  manager->GetRunwayEnd(&cellX, &cellZ);
  u32 direction = !manager->IsRunwayInXDirection();
  i32 endX = static_cast<i32>((cellX << kCtMicroCoordinateShift) + u32(NwPlaneStartX[direction]));
  i32 endZ = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + u32(NwPlaneStartZ[direction]));
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, altitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00106699998f);
  PushToActionStack(26);
  MakeCurrentPath(path);
  MoveAlongPath();
  return 2;
}

// Set up the first descending segment toward the runway landing point.
i32 NwAeroplane::TouchDown() {
  i32 offset = stateStackTop;
  stateStackTop += 12;
  i32 distance = 8;
  u8 direction = !manager->IsRunwayInXDirection();
  std::memcpy(stateStack + offset + 4, &distance, sizeof(distance));
  std::memcpy(stateStack + offset, &direction, sizeof(direction));
  i32 ground = manager->MicroYAtRunwayBegin();
  std::memcpy(&distance, stateStack + offset + 4, sizeof(distance));
  std::memcpy(stateStack + offset + 8, &ground, sizeof(ground));
  u32 cellX, cellZ;
  if (!manager->GetLandingBeginCell(&cellX, &cellZ, static_cast<u32>(distance))) {
    stateStackTop -= 12;
    return 1;
  }
  std::memcpy(&direction, stateStack + offset, sizeof(direction));
  std::memcpy(&distance, stateStack + offset + 4, sizeof(distance));
  std::memcpy(&ground, stateStack + offset + 8, sizeof(ground));
  i32 endX = static_cast<i32>((cellX << kCtMicroCoordinateShift) + u32(NwPlaneStartX[direction]));
  i32 endZ = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + u32(NwPlaneStartZ[direction]));
  i32 endAltitude = static_cast<i32>(u32(ground) + 1500u * u32(distance));
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, endAltitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00053349999f);
  PushToActionStack(20);
  MakeCurrentPath(path);
  static_cast<NwVehicle *>(this)->SetOrientation(path->GetEndOrientation());
  MoveAlongPath();
  return 2;
}

// Start the turn from the taxiway toward the runway using the original turn-path resource.
i32 NwAeroplane::TurnIntoRunway() {
  i32 offset = stateStackTop;
  stateStackTop += 4;
  i32 stage = 0;
  std::memcpy(stateStack + offset, &stage, sizeof(stage));
  u8 direction = static_cast<u8>(!manager->IsRunwayInXDirection() + 2);
  u32 cellX, cellZ;
  manager->GetTaxiwayEnd(&cellX, &cellZ);
  u8 side = manager->TaxiwaySide();
  auto *path = new NwVehicleAttribPath(static_cast<i32>(cellX << kCtMicroCoordinateShift), altitude,
      static_cast<i32>(cellZ << kCtMicroCoordinateShift), turnPathAttributes[4 * side + direction]);
  path->AddRef();
  path->SetSpeed(0.0001059999995f);
  MakeCurrentPath(path);
  PushToActionStack(25);
  MoveAlongPath();
  return 2;
}

i32 NwAeroplane::TurnIntoTaxiway() {
  i32 offset = stateStackTop;
  stateStackTop += 4;
  i32 stage = 0;
  std::memcpy(stateStack + offset, &stage, sizeof(stage));
  u8 direction = !manager->IsRunwayInXDirection();
  u32 cellX, cellZ;
  manager->GetRunwayEnd(&cellX, &cellZ);
  u8 side = manager->TaxiwaySide();
  auto *path = new NwVehicleAttribPath(static_cast<i32>(cellX << kCtMicroCoordinateShift), altitude,
      static_cast<i32>(cellZ << kCtMicroCoordinateShift), turnPathAttributes[4 * side + direction]);
  path->AddRef();
  path->SetSpeed(0.0001059999995f);
  MakeCurrentPath(path);
  PushToActionStack(22);
  speed = 0.0001059999995f;
  MoveAlongPath();
  return 2;
}

i32 NwAeroplane::MoveAlongTaxiway() {
  speed = 0.0001059999995f;
  manager->PlaneEnteredTaxiway(this);
  PushToActionStack(23);
  return 2;
}

// Select a city-edge destination and schedule a straight departing path.
i32 NwAeroplane::MoveToNeighbor() {
  u32 direction = (u32(orientation) + 4) / 8;
  if (direction >= 4)
    direction = 0;
  i32 endX = 0, endZ = 0;
  switch (direction) {
    case 0:
      endX = 5;
      endZ = NwVehicleRandom.Range(5, cityZMicroUnits - 5);
      break;
    case 1:
      endZ = 5;
      endX = NwVehicleRandom.Range(5, cityXMicroUnits - 5);
      break;
    case 2:
      endX = cityXMicroUnits - 5;
      endZ = NwVehicleRandom.Range(5, cityZMicroUnits - 5);
      break;
    case 3:
      endX = NwVehicleRandom.Range(5, cityXMicroUnits - 5);
      endZ = cityZMicroUnits - 5;
      break;
  }
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, altitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00053349999f);
  MakeCurrentPath(path);
  MoveAlongPath();
  return 2;
}

// Continue descending along the next landing segment until cancelled or the runway is reached.
i32 NwAeroplane::TouchDownUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 12;
  if (cancel) {
    stateStackTop -= 12;
    return 0;
  }
  i32 distance;
  std::memcpy(&distance, stateStack + offset + 4, sizeof(distance));
  if (distance == 0) {
    stateStackTop -= 12;
    return 0;
  }
  --distance;
  std::memcpy(stateStack + offset + 4, &distance, sizeof(distance));
  u32 cellX, cellZ;
  manager->GetLandingBeginCell(&cellX, &cellZ, static_cast<u32>(distance));
  u8 direction;
  i32 ground;
  std::memcpy(&direction, stateStack + offset, sizeof(direction));
  std::memcpy(&distance, stateStack + offset + 4, sizeof(distance));
  std::memcpy(&ground, stateStack + offset + 8, sizeof(ground));
  i32 endX = static_cast<i32>((cellX << kCtMicroCoordinateShift) + u32(NwPlaneStartX[direction]));
  i32 endZ = static_cast<i32>((cellZ << kCtMicroCoordinateShift) + u32(NwPlaneStartZ[direction]));
  i32 endAltitude = static_cast<i32>(u32(ground) + 1500u * u32(distance));
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, endAltitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00053349999f);
  MakeCurrentPath(path);
  static_cast<NwVehicle *>(this)->SetOrientation(path->GetEndOrientation());
  MoveAlongPath();
  return 2;
}

// Continue the takeoff climb one cell at a time and release the runway after its final stage.
i32 NwAeroplane::TakeOffUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 16;
  if (cancel) {
    stateStackTop -= 16;
    return 0;
  }
  i32 stage;
  std::memcpy(&stage, stateStack + offset + 4, sizeof(stage));
  if (stage >= 9) {
    stateStackTop -= 16;
    manager->RelinquishRunway(this);
    return 0;
  }
  u8 direction;
  std::memcpy(&direction, stateStack + offset + 12, sizeof(direction));
  i32 cellX = x >> kCtMicroCoordinateShift, cellZ = z >> kCtMicroCoordinateShift;
  if (direction) {
    if (cellZ == 0) {
      stateStackTop -= 16;
      return 1;
    }
    --cellZ;
  } else {
    if (cellX == 0) {
      stateStackTop -= 16;
      return 1;
    }
    --cellX;
  }
  i32 endX = static_cast<i32>((u32(cellX) << kCtMicroCoordinateShift) + u32(NwPlaneStartX[direction]));
  i32 endZ = static_cast<i32>((u32(cellZ) << kCtMicroCoordinateShift) + u32(NwPlaneStartZ[direction]));
  i32 endAltitude = static_cast<i32>(u32(altitude) + 1500u);
  auto *path = new NwVehicleStraightPath(x, altitude, z, endX, endAltitude, endZ);
  path->AddRef();
  path->SetSpeed(0.00053349999f);
  MakeCurrentPath(path);
  std::memcpy(&stage, stateStack + offset + 4, sizeof(stage));
  stage = static_cast<i32>(u32(stage) + 1);
  std::memcpy(stateStack + offset + 4, &stage, sizeof(stage));
  MoveAlongPath();
  return 2;
}

// Advance the two-stage taxiway-to-runway turn and remove the plane from the taxiway on completion.
i32 NwAeroplane::TurnIntoRunwayUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 4;
  if (cancel) {
    manager->RelinquishRunway(this);
    stateStackTop -= 4;
    return 0;
  }
  i32 stage;
  std::memcpy(&stage, stateStack + offset, sizeof(stage));
  if (stage != 0 && stage != 1) {
    manager->PlaneLeftTaxiway(this);
    stateStackTop -= 4;
    return 0;
  }
  i32 next = stage + 1;
  std::memcpy(stateStack + offset, &next, sizeof(next));
  u32 direction = paths[0]->GetEndOrientation() >> 3;
  i32 pointX =
      static_cast<i32>((u32((x >> kCtMicroCoordinateShift) + NwPlaneDirectionX[direction])) << kCtMicroCoordinateShift);
  i32 pointZ =
      static_cast<i32>((u32((z >> kCtMicroCoordinateShift) + NwPlaneDirectionZ[direction])) << kCtMicroCoordinateShift);
  NwVehiclePathAttrib *pathAttributes = stage == 0 ? straightPathAttributes[direction]
      : turnPathAttributes[4 * manager->TaxiwaySide() + direction];
  auto *path = new NwVehicleAttribPath(pointX, altitude, pointZ, pathAttributes);
  path->AddRef();
  path->SetSpeed(0.0001059999995f);
  MakeCurrentPath(path);
  MoveAlongPath();
  return 2;
}

// Advance the two-stage runway-to-taxiway turn and release the runway when the turn finishes.
i32 NwAeroplane::TurnIntoTaxiwayUpdateFn(u32, i32, bool cancel) {
  i32 offset = stateStackTop - 4;
  if (cancel) {
    manager->RelinquishRunway(this);
    stateStackTop -= 4;
    return 0;
  }
  i32 stage;
  std::memcpy(&stage, stateStack + offset, sizeof(stage));
  if (stage != 0 && stage != 1) {
    manager->RelinquishRunway(this);
    stateStackTop -= 4;
    return 0;
  }
  i32 next = stage + 1;
  std::memcpy(stateStack + offset, &next, sizeof(next));
  u32 direction = paths[0]->GetEndOrientation() >> 3;
  i32 pointX =
      static_cast<i32>((u32((x >> kCtMicroCoordinateShift) + NwPlaneDirectionX[direction])) << kCtMicroCoordinateShift);
  i32 pointZ =
      static_cast<i32>((u32((z >> kCtMicroCoordinateShift) + NwPlaneDirectionZ[direction])) << kCtMicroCoordinateShift);
  NwVehiclePathAttrib *pathAttributes = stage == 0 ? straightPathAttributes[direction]
      : turnPathAttributes[4 * manager->TaxiwaySide() + direction];
  auto *path = new NwVehicleAttribPath(pointX, altitude, pointZ, pathAttributes);
  path->AddRef();
  path->SetSpeed(0.0001059999995f);
  MakeCurrentPath(path);
  MoveAlongPath();
  return 2;
}

// Look ahead along the taxiway and move when the next three cells are clear.
i32 NwAeroplane::MoveAlongTaxiwayUpdateFn(u32, i32, bool cancel) {
  if (cancel)
    return 0;
  u32 endX, endZ;
  manager->GetTaxiwayEnd(&endX, &endZ);
  u32 direction = u32(!manager->IsRunwayInXDirection()) + 2;
  u32 cellX = u32(x >> kCtMicroCoordinateShift) + u32(NwPlaneDirectionX[direction]);
  u32 cellZ = u32(z >> kCtMicroCoordinateShift) + u32(NwPlaneDirectionZ[direction]);
  if (cellX > maxCellX || cellZ > maxCellZ)
    return 1;
  if (cellX == endX && cellZ == endZ)
    return 0;
  u32 checkX = cellX, checkZ = cellZ, count = 0;
  while (manager->TaxiwayCellEmpty(checkX, checkZ)) {
    checkX += u32(NwPlaneDirectionX[direction]);
    checkZ += u32(NwPlaneDirectionZ[direction]);
    if (++count >= 3) {
      auto *path = new NwVehicleAttribPath(static_cast<i32>(cellX << kCtMicroCoordinateShift), altitude,
          static_cast<i32>(cellZ << kCtMicroCoordinateShift), straightPathAttributes[direction]);
      path->AddRef();
      path->SetSpeed(0.0001059999995f);
      MakeCurrentPath(path);
      if (pathTop == 0) {
        i32 pathDistance = path->Length();
        remainingDistance = static_cast<f32>(static_cast<i32>(u32(attributes->length) + u32(pathDistance)));
      }
      MoveAlongPath();
      return 2;
    }
  }
  return 2;
}

// Release the runway if a deceleration action is cancelled.
i32 NwAeroplane::SlowDownOnRunwayUpdateFn(u32, i32, bool cancel) {
  if (cancel)
    manager->RelinquishRunway(this);
  return 0;
}

// Release the runway if an acceleration action is cancelled.
i32 NwAeroplane::SpeedUpOnRunwayUpdateFn(u32, i32, bool cancel) {
  if (cancel)
    manager->RelinquishRunway(this);
  return 0;
}

i32 NwAeroplane::CurrentAction() {
  return planeAction;
}

// Retain the vehicle producer and store its airport-manager identity after the retain callback.
u32 NwAeroplane::SetProducer(NwVehicleProducerInterface *value) {
  producer = ScAddRefNotNull(value);
  manager = static_cast<NwAirTrafficManager *>(producer);
  return static_cast<u32>(reinterpret_cast<uptr>(manager));
}

void NwAeroplane::SetPlaneAction(i32 action) {
  planeAction = action;
}

u8 NwAeroplane::IsNormalRoutinePlane() {
  return normalRoutine;
}

void NwAeroplane::EndDispatchMission() {
  dispatchEnded = 1;
}

// Add an in-bounds cell as a future aircraft target in animation coordinates.
u8 NwAeroplane::Redirect(u32 cellX, u32 cellZ) {
  u32 microX = cellX << kCtMicroCoordinateShift, microZ = cellZ << kCtMicroCoordinateShift;
  if ((cellX & kNwAircraftCellSignBit) || static_cast<i32>(microX) >= cityXMicroUnits ||
      (cellZ & kNwAircraftCellSignBit) || static_cast<i32>(microZ) >= cityZMicroUnits)
    return 0;
  NwAirTarget target{microX, microZ};
  targets.push_back(target);
  return 1;
}

void NwAeroplane::SetCurrentViewType(i32 value) {
  currentViewType = value;
}

u8 NwAeroplane::IsAirVehicle() {
  return 1;
}

// Choose whether an emergency vehicle uses no siren or one of its two siren states.
i32 NwStreetVehicle::DecideSirenState() {
  if (NwVehicleRandom.Bounded(100) < 80)
    return 0;
  u32 selection = NwVehicleRandom.Bounded(2);
  if (selection == 0)
    return 1;
  if (selection == 1)
    return 2;
  return 0;
}

f64 NwStreetVehicle::CalculateAngle(i32 x0, i32 z0, i32 x1, i32 z1) {
  i32 dx = i32(u32(x1) - u32(x0)), dz = i32(u32(z1) - u32(z0));
  return std::atan2(f64(dz), f64(dx));
}

// Turn the vehicle toward a target angle according to distance traveled and vehicle length.
// Linux identifies the member; the Windows compiler incorporates its arithmetic into movement updates.
void NwStreetVehicle::ChangeVehicleAngle(f32 delta, f32 distance) {
  f64 turn = delta;
  if (turn > f64(3.1415927f))
    turn -= f64(6.2831855f);
  else if (turn < -f64(3.1415927f))
    turn += f64(6.2831855f);
  f64 nextAngle = turn * (f64(distance) * f64(0.89999998f)) / attributes->length + angle;
  angle = static_cast<f32>(nextAngle);
  i64 next = static_cast<i64>(nextAngle / f64(0.19634955f) + 0.5);
  u8 heading = static_cast<u8>(u64(next) & 31u);
  if (heading != orientation)
    SetOrientation(heading);
}

i32 NwStreetVehicle::DistanceToBack(i32 pathLengthValue, NwVehiclePath *path) {
  NwVehicleGraphEdge *edge = path->GetEdge();
  if (edge == paths[pathTop]->GetEdge()) {
    NwTrafficBlock *block = path->GetStrtSimTrBlock();
    if (block == paths[pathTop]->GetStrtSimTrBlock())
      return static_cast<i32>(u32(pathLengthValue) - u32(static_cast<i32>(remainingDistance)));
  }
  return -1;
}

// Test whether the supplied altitude intersects the static occupant in the selected cell.
bool NwStreetVehicle::InsideStaticOccupant(i32 pointX, i32 pointAltitude, i32 pointZ) {
  GeBounds bounds;
  ScOccupant *occupant;
  if (!occupantManagers[occupantManagerIndex]->GetOccupantAtCell(static_cast<u32>(pointX >> kCtMicroCoordinateShift),
          static_cast<u32>(pointZ >> kCtMicroCoordinateShift), &occupant))
    return false;
  occupant->GetBounds(&bounds);
  bool result = pointAltitude >= bounds.minimum.altitude && pointAltitude <= bounds.maximum.altitude;
  occupant->Release();
  return result;
}

// Get the transit cell's base altitude after applying the block's rotated corner offset.
// Linux identifies the original member; Windows incorporates it into its callers.
u8 NwStreetVehicle::GetAltitude(ScOccupant *occupant, NwTrafficAttrib *blockAttributes) {
  u8 cellAltitude = occupant->GetAlt(), rotation = 0;
  occupant->GetSpriteInst()->GetRotationOffset(&rotation);
  return static_cast<u8>(cellAltitude - blockAttributes->cornerOffsets[rotation]);
}

// Return the borrowed sprite-attribute interface used by this vehicle.
// The shared occupant declaration currently spells this interface as its concrete producer class.
SpDefinition *NwStreetVehicle::GetSpriteAttrib() {
  return reinterpret_cast<SpDefinition *>(spriteRepository);
}

// Construct and retain the forward or reverse path described by the selected vehicle edge.
// Linux identifies the original member and parameter order; a distinct Windows entry remains unresolved.
NwVehiclePath *NwStreetVehicle::BuildPath(
    NwTrafficBlock *block, u32 cellX, u8 cellAltitude, u32 cellZ, NwVehicleGraphEdge *edge) {
  if (!edge->pathAttributes)
    return nullptr;
  i32 originX = static_cast<i32>(cellX << kCtMicroCoordinateShift),
      originAltitude = static_cast<i32>(u32(cellAltitude) << kCtMicroCoordinateShift);
  i32 originZ = static_cast<i32>(cellZ << kCtMicroCoordinateShift);
  NwVehiclePath *path;
  if (edge->reverse)
    path = new NwVehicleBackwardPath(block, originX, originAltitude, originZ, edge);
  else
    path = new NwVehicleForwardPath(block, originX, originAltitude, originZ, edge);
  path->AddRef();
  return path;
}

// Select a compatible internal vehicle edge on the supplied transit occupant and build its path.
NwVehiclePath *NwStreetVehicle::SelectPath(ScOccupant *occupant) {
  NwConnection *connection;
  occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection));
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  NwTrafficAttrib *blockAttributes = block->GetStrtSimAttrib();
  NwVehicleGraph *graph = blockAttributes->vehicleGraph;
  u8 cellAltitude = GetAltitude(occupant, blockAttributes);
  NwVehicleGraphNode *node = nullptr;
  NwVehicleGraphEdge *edge = nullptr;
  graph->SelectANodeAndInternalEdge(&node, &edge, occupantManagerIndex);
  NwVehiclePath *path = node && edge ? BuildPath(block, cellX, cellAltitude, cellZ, edge) : nullptr;
  connection->Release();
  return path;
}

// Select the next vehicle edge at a specific point on an occupied transit cell.
NwVehiclePath *NwStreetVehicle::SelectPath(i32 pointX, i32, i32 pointZ) {
  i32 cellX = pointX >> kCtMicroCoordinateShift, cellZ = pointZ >> kCtMicroCoordinateShift;
  ScOccupant *occupant;
  if (!occupantManagers[occupantManagerIndex]->GetOccupantAtCell(u32(cellX), u32(cellZ), &occupant))
    return nullptr;
  if (!pathTileFilters[occupantManagerIndex](occupant)) {
    occupant->Release();
    return nullptr;
  }
  NwConnection *connection;
  occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection));
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  NwTrafficAttrib *blockAttributes = block->GetStrtSimAttrib();
  i32 localX = static_cast<i32>(u32(pointX) - (u32(cellX) << kCtMicroCoordinateShift));
  i32 localZ = static_cast<i32>(u32(pointZ) - (u32(cellZ) << kCtMicroCoordinateShift));
  NwVehicleGraphNode *node = blockAttributes->vehicleGraph->GetNode(localX, localZ, occupantManagerIndex);
  NwVehicleGraphEdge *edge = node->GetNextPath();
  u8 cellAltitude = GetAltitude(occupant, blockAttributes);
  NwVehiclePath *path = BuildPath(block, u32(cellX), cellAltitude, u32(cellZ), edge);
  connection->Release();
  occupant->Release();
  return path;
}

// Follow a connecting edge into the neighboring transit cell and build its selected internal path.
// The layer's original vehicle simulation rectangle implementation remains unresolved.
NwVehiclePath *NwStreetVehicle::SelectPathFromConnectingNodeOnNextCell(
    i32 cellX, i32 cellZ, NwVehicleGraphEdge *edge, i32 pointX, i32 pointZ) {
  u8 direction = edge->endOrientation >> 2;
  u32 nextX = u32(cellX) + u32(NwVehicleStepX[direction]);
  u32 nextZ = u32(cellZ) + u32(NwVehicleStepZ[direction]);
  if (nextX > maxCellX || nextZ > maxCellZ ||
      (byte44 && !static_cast<NwStreetLayer *>(streetLayer)->CellIsInVehicleSimRect(nextX, nextZ)))
    return nullptr;
  ScOccupant *occupant;
  if (!occupantManagers[occupantManagerIndex]->GetOccupantAtCell(nextX, nextZ, &occupant))
    return nullptr;
  NwVehiclePath *path = nullptr;
  NwConnection *connection;
  if (pathTileFilters[occupantManagerIndex](occupant) &&
      occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
    auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
    NwTrafficAttrib *blockAttributes = block->GetStrtSimAttrib();
    NwVehicleGraph *graph = blockAttributes->vehicleGraph;
    if (graph) {
      i32 localX = static_cast<i32>(u32(pointX) - (nextX << kCtMicroCoordinateShift));
      i32 localZ = static_cast<i32>(u32(pointZ) - (nextZ << kCtMicroCoordinateShift));
      NwVehicleGraphNode *node = graph->GetNode(localX, localZ, occupantManagerIndex);
      if (node) {
        NwVehicleGraphEdge *nextEdge = SelectAnEdge(node, static_cast<i32>(nextX), static_cast<i32>(nextZ));
        if (nextEdge->target) {
          u8 cellAltitude = GetAltitude(occupant, blockAttributes);
          path = BuildPath(block, nextX, cellAltitude, nextZ, nextEdge);
        }
      }
    }
    connection->Release();
  }
  occupant->Release();
  return path;
}

// Select a continuation from the current edge or its connecting neighboring cell.
void NwStreetVehicle::SelectNextPath(NwVehiclePath *path, NwVehiclePath **out) {
  i32 pointX, pointAltitude, pointZ;
  path->GetEndPoint(&pointX, &pointAltitude, &pointZ);
  i32 cellX = pointX >> kCtMicroCoordinateShift, cellZ = pointZ >> kCtMicroCoordinateShift;
  NwVehicleGraphEdge *edge = SelectAnEdge(path->GetEdge()->target, cellX, cellZ);
  if (!edge) {
    *out = nullptr;
    return;
  }
  if (edge->target) {
    u8 cellAltitude = static_cast<u8>(u32(path->GetReferenceMicroY()) >> kCtMicroCoordinateShift);
    *out = BuildPath(path->GetStrtSimTrBlock(), u32(cellX), cellAltitude, u32(cellZ), edge);
  } else {
    *out = SelectPathFromConnectingNodeOnNextCell(cellX, cellZ, edge, pointX, pointZ);
  }
}

// Choose and enter a transit path, then insert the vehicle and start its script.
u8 NwStreetVehicle::PlaceOnTransitTile(ScOccupant *occupant) {
  for (i32 attempt = 0; attempt < 3; ++attempt) {
    NwVehiclePath *path = SelectPath(occupant);
    if (!path)
      return 0;
    path->GetStartPoint(&x, &altitude, &z);
    SetOrientation(path->GetPlacingOrientation());
    i32 distance = static_cast<i32>(u32(attributes->length) + u32(path->Length()));
    remainingDistance = static_cast<f32>(distance);
    if (path->EnterVehicle(static_cast<NwVehicle *>(this), 28)) {
      speed = path->Speed();
      MakeCurrentPath(path);
      if (!animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true))
        return 0;
      i32 pathSortId;
      if (paths[0] && paths[0]->SortId(&pathSortId)) {
        sortId = pathSortId;
        animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 128, &sortId);
      }
      ExecuteScript(attributes->scriptKey, 1, 0);
      return 1;
    }
    path->Release();
  }
  return 0;
}

u8 NwStreetVehicle::PlaceOnTransitTile(ScOccupant *, u8) {
  return 0;
}

// Choose an outgoing edge, weighting surface routes by their destination traffic volume.
// Linux identifies the member; the shared traffic interface's complete slot order remains unresolved.
NwVehicleGraphEdge *NwStreetVehicle::SelectAnEdge(NwVehicleGraphNode *node, i32 cellX, i32 cellZ) {
  NwVehicleGraphEdge *edges[6];
  u32 count = 6;
  node->GetOutgoingEdges(edges, &count);
  if (!count)
    return nullptr;
  if (count == 1)
    return edges[0];
  if (occupantManagerIndex)
    return edges[NwVehicleRandom.Bounded(count)];
  u32 cumulative[6], total = 0;
  for (u32 i = 0; i < count; ++i) {
    NwVehicleGraphEdge *edge = edges[i];
    u8 volume = 0;
    if (edge->target) {
      trafficLayer->GetVolume(static_cast<u32>(cellX), static_cast<u32>(cellZ), &volume, 3);
    } else {
      u8 direction = static_cast<u8>(edge->endOrientation >> 2);
      u32 nextX = u32(cellX) + u32(NwVehicleStepX[direction]);
      u32 nextZ = u32(cellZ) + u32(NwVehicleStepZ[direction]);
      if (nextX <= maxCellX && nextZ <= maxCellZ)
        trafficLayer->GetVolume(nextX, nextZ, &volume, 3);
    }
    total += volume;
    cumulative[i] = total;
  }
  if (!total)
    return edges[NwVehicleRandom.Bounded(count)];
  u32 draw = NwVehicleRandom.Bounded(total), index = 0;
  while (index < count && draw > cumulative[index])
    ++index;
  return edges[index];
}

// Compute the original four oriented vehicle corners from its length, width and heading.
void NwStreetVehicle::GetTrueBoundingRect(ScPointXZSigned *points) const {
  i32 originX = x, originZ = z;
  const NwVehicle *vehicle = static_cast<const NwVehicle *>(this);
  i32 vehicleLength = vehicle->Length(), vehicleWidth = vehicle->Width();
  if (!vehicleLength || !std::isfinite(angle))
    return; // The original encounters invalid division or conversion on these inputs.
  i32 forwardX = static_cast<i32>(u32(static_cast<i64>(std::cos(f64(angle)) * vehicleLength)));
  i32 forwardZ = static_cast<i32>(u32(static_cast<i64>(std::sin(f64(angle)) * vehicleLength)));
  i32 endX = static_cast<i32>(u32(originX) + u32(forwardX));
  i32 endZ = static_cast<i32>(u32(originZ) + u32(forwardZ));
  f64 cornerAngle = std::atan2(f64(vehicleWidth / 2 / vehicleLength), 1.0);
  f64 cosine = std::cos(cornerAngle), sine = std::sin(cornerAngle);
  f64 reverseCosine = std::cos(-cornerAngle), reverseSine = std::sin(-cornerAngle);
  f64 backX = f64(static_cast<i32>(u32(originX) - u32(endX)));
  f64 backZ = f64(static_cast<i32>(u32(originZ) - u32(endZ)));
  f64 aheadX = f64(static_cast<i32>(u32(endX) - u32(originX)));
  f64 aheadZ = f64(static_cast<i32>(u32(endZ) - u32(originZ)));
  points[0].x = static_cast<i32>(u32(endX) + u32(static_cast<i64>(backX * cosine - backZ * sine)));
  points[0].z = static_cast<i32>(u32(endZ) + u32(static_cast<i64>(backX * sine + backZ * cosine)));
  points[1].x = static_cast<i32>(u32(endX) + u32(static_cast<i64>(backX * reverseCosine - backZ * reverseSine)));
  points[1].z = static_cast<i32>(u32(endX) + u32(static_cast<i64>(backZ * reverseCosine + backX * reverseSine)));
  points[2].x = static_cast<i32>(u32(originX) + u32(static_cast<i64>(aheadX * cosine - aheadZ * sine)));
  points[2].z = static_cast<i32>(u32(originZ) + u32(static_cast<i64>(aheadX * sine + aheadZ * cosine)));
  points[3].x = static_cast<i32>(u32(originX) + u32(static_cast<i64>(aheadX * reverseCosine - aheadZ * reverseSine)));
  points[3].z = static_cast<i32>(u32(originZ) + u32(static_cast<i64>(aheadX * reverseSine + aheadZ * reverseCosine)));
}

// Compute the midpoint between the vehicle's position and its heading-length endpoint.
void NwStreetVehicle::GetCenterOfMass(ScPointXZSigned *point) const {
  i32 originX = x, originZ = z;
  const NwVehicle *vehicle = static_cast<const NwVehicle *>(this);
  i32 vehicleLength = vehicle->Length();
  vehicle->Width();
  if (!std::isfinite(angle))
    return; // The original converts the non-finite trigonometric result to an integer.
  i32 deltaZ = static_cast<i32>(u32(static_cast<i64>(std::sin(f64(angle)) * vehicleLength)));
  i32 deltaX = static_cast<i32>(u32(static_cast<i64>(std::cos(f64(angle)) * vehicleLength)));
  point->x = static_cast<i32>(u32(deltaX) + u32(originX) * 2u) / 2;
  point->z = static_cast<i32>(u32(deltaZ) + u32(originZ) * 2u) / 2;
}

// Compute the axis-aligned bounds around the four corners returned by the vehicle interface.
void NwStreetVehicle::GetBoundingRectInAnimCellCoords(GeCellRect *out) const {
  ScPointXZSigned points[4]{};
  static_cast<const NwVehicle *>(this)->GetTrueBoundingRect(points);
  i32 minimumX = points[0].x, maximumX = points[0].x;
  i32 minimumZ = points[0].z, maximumZ = points[0].z;
  for (u32 i = 1; i < 4; ++i) {
    if (points[i].x < minimumX)
      minimumX = points[i].x;
    if (points[i].x > maximumX)
      maximumX = points[i].x;
    if (points[i].z < minimumZ)
      minimumZ = points[i].z;
    if (points[i].z > maximumZ)
      maximumZ = points[i].z;
  }
  out->left = static_cast<u32>(minimumX);
  out->right = static_cast<u32>(maximumX);
  out->top = static_cast<u32>(minimumZ);
  out->bottom = static_cast<u32>(maximumZ);
}

// Convert the vehicle's animation-coordinate bounds to city cell coordinates.
void NwStreetVehicle::GetBoundingRectInCellCoords(GeCellRect *out) const {
  GetBoundingRectInAnimCellCoords(out);
  out->left >>= 8;
  out->top >>= 8;
  out->right >>= 8;
  out->bottom >>= 8;
}

// Move the vehicle and periodically send its configured movement events.
u8 NwStreetVehicle::ChangePosition(CtCoord *position) {
  animationManager->MoveOccupant(static_cast<ScOccupant *>(this), position);
  movementStatus = 2;
  ++movementCounter;
  if (movementCounter < 10)
    return movementCounter;
  movementCounter = 0;
  if (attributes->value0) {
    if (static_cast<NwVehicle *>(this)->IsAirVehicle())
      boxX->Event(kAuEventEffectSlot60, static_cast<u32>(attributes->value0), reinterpret_cast<uptr>(this),
          static_cast<u32>(altitude), 0);
    boxX->Event(kAuEventEffectSlot48, static_cast<u32>(attributes->value0), reinterpret_cast<uptr>(this),
        static_cast<u32>(x), static_cast<u32>(z));
  }
  i32 event = attributes->value2;
  if (event)
    return static_cast<u8>(boxX->Event(kAuEventEffectSlot48, static_cast<u32>(event), reinterpret_cast<uptr>(this),
        static_cast<u32>(x), static_cast<u32>(z)));
  return 0;
}

// Derive the vehicle pitch from the weighted pitches of its retained paths.
u8 NwStreetVehicle::ComputePitch() {
  f32 weightedPitch = 0.0f;
  for (i32 i = 0; i <= pathTop; ++i) {
    NwVehiclePath *path = paths[i];
    f32 lengthValue = static_cast<f32>(path->Length());
    weightedPitch = static_cast<f32>(f64(path->GetPitch()) * lengthValue + weightedPitch);
  }
  f32 firstPitch = paths[0]->GetPitch();
  NwVehiclePath *lastPath = paths[pathTop];
  f32 adjustedPitch = static_cast<f32>(f64(weightedPitch) - f64(firstPitch) * pathLength);
  f32 unusedLength = static_cast<f32>(f64(lastPath->Length()) - remainingDistance);
  f32 lastPitch = lastPath->GetPitch();
  f64 result = (f64(adjustedPitch) - f64(lastPitch) * unusedLength) / attributes->length + 2.5;
  if (!std::isfinite(result) || result >= 0x1p63 || result < -0x1p63)
    return 0;
  u8 value = static_cast<u8>(static_cast<i64>(result));
  if (value & 0x80)
    return 0;
  return value > 4 ? 4 : value;
}

// Move along the current point segment, updating orientation, retained paths and pitch.
i32 NwStreetVehicle::MoveToPointMoveDist(NwVehicleMoveState *state, f32 distance) {
  if (vehicleAhead) {
    i32 available = static_cast<i32>(u32(vehicleAhead->DistanceToBack(pathLength, paths[0])) - 28u);
    if (available <= 0) {
      movementCarry = 0.0f;
      return 2;
    }
    if (distance > static_cast<f64>(available))
      distance = static_cast<f32>(available);
  }
  f32 oldProgress = state->progress;
  f64 progress = f64(distance) + oldProgress;
  state->progress = static_cast<f32>(progress);
  f32 segmentLength = static_cast<f32>(state->length), moved;
  bool finished = progress >= segmentLength;
  if (!finished) {
    movementCarry = 0.0f;
    moved = distance;
  } else {
    moved = static_cast<f32>(f64(segmentLength) - oldProgress);
    movementCarry = static_cast<f32>(f64(distance) - moved);
  }
  movementDistance = static_cast<f32>(f64(moved) + movementDistance);
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
    newOrientation = static_cast<u8>(u64(static_cast<i64>(orientationValue)) & 31u);
  if (newOrientation != orientation)
    static_cast<NwVehicle *>(this)->SetOrientation(newOrientation);
  f64 remaining = f64(remainingDistance) - moved;
  remainingDistance = static_cast<f32>(remaining);
  while (remaining < 0.0) {
    LeaveLastPath();
    remaining = f64(paths[pathTop]->Length()) + remainingDistance;
    remainingDistance = static_cast<f32>(remaining);
  }
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  if (finished) {
    position.x = state->endX;
    position.altitude = state->endAltitude;
    position.z = state->endZ;
    static_cast<NwVehicle *>(this)->ChangePosition(&position);
    pathLength = static_cast<i32>(u32(state->pathDistance) - u32(state->length));
    pitch = static_cast<i8>(ComputePitch());
    return 0;
  }
  i32 dx = static_cast<i32>(u32(static_cast<i64>(f64(state->directionX) * state->progress)));
  i32 dy = static_cast<i32>(u32(static_cast<i64>(f64(state->directionAltitude) * state->progress * f64(4.8989592f))));
  i32 dz = static_cast<i32>(u32(static_cast<i64>(f64(state->directionZ) * state->progress)));
  position.x = static_cast<i32>(u32(state->startX) + u32(dx));
  position.altitude = static_cast<i32>(u32(state->startAltitude) + u32(dy));
  position.z = static_cast<i32>(u32(state->startZ) + u32(dz));
  static_cast<NwVehicle *>(this)->ChangePosition(&position);
  pathLength = static_cast<i32>(u32(state->pathDistance) - u32(static_cast<i64>(state->progress)));
  pitch = static_cast<i8>(ComputePitch());
  return 2;
}

// Move along the current point segment, updating orientation, retained paths and pitch.
i32 NwStreetVehicle::FollowerMoveToPointMoveDist(NwVehicleFollowerMoveState *state, f32 distance) {
  f32 oldProgress = state->progress;
  f64 progress = f64(distance) + oldProgress;
  state->progress = static_cast<f32>(progress);
  f32 segmentLength = static_cast<f32>(state->length), moved;
  bool finished = progress >= segmentLength;
  if (!finished) {
    movementCarry = 0.0f;
    moved = distance;
  } else {
    moved = static_cast<f32>(f64(segmentLength) - oldProgress);
    movementCarry = static_cast<f32>(f64(distance) - moved);
  }
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
    newOrientation = static_cast<u8>(u64(static_cast<i64>(orientationValue)) & 31u);
  if (newOrientation != orientation)
    static_cast<NwVehicle *>(this)->SetOrientation(newOrientation);
  f64 remaining = f64(remainingDistance) - moved;
  remainingDistance = static_cast<f32>(remaining);
  while (remaining < 0.0) {
    LeaveLastPath();
    remaining = f64(paths[pathTop]->Length()) + remainingDistance;
    remainingDistance = static_cast<f32>(remaining);
  }
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  if (finished) {
    position.x = state->endX;
    position.altitude = state->endAltitude;
    position.z = state->endZ;
    static_cast<NwVehicle *>(this)->ChangePosition(&position);
    pathLength = static_cast<i32>(u32(state->pathDistance) - u32(state->length));
    pitch = static_cast<i8>(ComputePitch());
    return 0;
  }
  i32 dx = static_cast<i32>(u32(static_cast<i64>(f64(state->directionX) * state->progress)));
  i32 dy = static_cast<i32>(u32(static_cast<i64>(f64(state->directionAltitude) * state->progress * f64(4.8989592f))));
  i32 dz = static_cast<i32>(u32(static_cast<i64>(f64(state->directionZ) * state->progress)));
  position.x = static_cast<i32>(u32(state->startX) + u32(dx));
  position.altitude = static_cast<i32>(u32(state->startAltitude) + u32(dy));
  position.z = static_cast<i32>(u32(state->startZ) + u32(dz));
  static_cast<NwVehicle *>(this)->ChangePosition(&position);
  pathLength = static_cast<i32>(u32(state->pathDistance) - u32(static_cast<i64>(state->progress)));
  pitch = static_cast<i8>(ComputePitch());
  return 2;
}

NwDependentVehicle::NwDependentVehicle() {
  geometryFlags = 4;
}

u8 NwDependentVehicle::SubTypeInfo() const {
  return 10;
}

// Initialize the boat's wake, view threshold and default movement speeds.
NwBoatVehicle::NwBoatVehicle() {
  byte44 = 0;
  birthZoom = 5;
  wake = nullptr;
  sortId = 2;
  minimumSpeed = 0.00001f;
  maximumSpeed = 0.0001f;
  viewer = nullptr;
  geometryFlags = kNwWaterVehicleGeometry;
}

NwShipVehicle::NwShipVehicle() {
  shipAction = 2;
  route = nullptr;
  routeCount = 0;
  routeIndex = 0;
  birthZoom = 5;
  wake = nullptr;
  byte400 = 0;
  viewer = nullptr;
  byte44 = 0;
  sortId = 2;
  geometryFlags = kNwWaterVehicleGeometry;
}

// Load the boat attributes and create a dependent wake for sufficiently long vessels.
u8 NwBoatVehicle::Init(const RsKey &key) {
  viewer = NwGetStreetCurrentView();
  u8 result = NwStreetVehicle::Init(key);
  if (!result)
    return result;
  u8 zoomCount = spriteRepository->GetZoomCount();
  if (zoomCount > 5)
    return 0;
  birthZoom = static_cast<i8>(5 - zoomCount);
  maximumSpeed = attributes->maxSpeed;
  minimumSpeed = static_cast<f32>(f64(maximumSpeed) * 0.5);
  if (f64(maximumSpeed) <= f64(maximumSpeed) * 0.5 || minimumSpeed <= 0.0f) {
    minimumSpeed = 0.00001f;
    maximumSpeed = 0.0001f;
  }
  wake = nullptr;
  if (attributes->length >= 180) {
    RsKey wakeKey(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, attributes->length < 680 ? 98 : 97);
    wake = ScAddRefNotNull(new NwDependentVehicle);
    wake->SetProducer(static_cast<NwVehicleProducerInterface *>(this));
    wake->SetVisibility(false);
    result = wake->Init(wakeKey);
    if (!result) {
      wake->Release();
      wake = nullptr;
      return 0;
    }
    wake->DoNothing();
  }
  return result;
}

// Load the ship attributes and create a dependent wake for sufficiently long vessels.
u8 NwShipVehicle::Init(const RsKey &key) {
  viewer = NwGetStreetCurrentView();
  u8 result = NwStreetVehicle::Init(key);
  if (!result)
    return result;
  u8 zoomCount = spriteRepository->GetZoomCount();
  if (zoomCount > 5)
    return 0;
  birthZoom = static_cast<i8>(5 - zoomCount);
  wake = nullptr;
  if (attributes->length >= 180) {
    RsKey wakeKey(kRsVehicleAttributesResourceType, kRsVehicleAttributesGroup, attributes->length < 680 ? 98 : 97);
    wake = ScAddRefNotNull(new NwDependentVehicle);
    wake->SetProducer(static_cast<NwVehicleProducerInterface *>(this));
    wake->SetVisibility(false);
    result = wake->Init(wakeKey);
    if (!result) {
      wake->Release();
      wake = nullptr;
      return 0;
    }
    wake->DoNothing();
  }
  return result;
}

bool NwBoatVehicle::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_NwBoat, NwBoat);
  return NwStreetVehicle::QueryInterface(iid, out);
}

bool NwShipVehicle::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_NwShip, NwShip);
  return NwStreetVehicle::QueryInterface(iid, out);
}

u32 NwBoatVehicle::AddRef() {
  return NwStreetVehicle::AddRef();
}

u32 NwBoatVehicle::Release() {
  return NwStreetVehicle::Release();
}

u32 NwShipVehicle::AddRef() {
  return NwStreetVehicle::AddRef();
}

u32 NwShipVehicle::Release() {
  return NwStreetVehicle::Release();
}

u8 NwBoatVehicle::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (zoom < birthZoom || currentViewType || !visible)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwBoatVehicle::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (zoom < birthZoom || currentViewType || !visible)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

// Advance the boat only while its view and zoom are eligible, updating its wake visibility.
i32 NwBoatVehicle::Tick(u32 elapsed) {
  if (!viewer) {
    viewer = NwGetStreetCurrentView();
    return NwStreetVehicle::Tick(elapsed);
  }
  i32 zoom, rotation;
  viewer->GetZoomAndRotation(&zoom, &rotation);
  if (zoom >= birthZoom && currentViewType == 0) {
    if (wake)
      wake->SetVisibility(true);
    return NwStreetVehicle::Tick(elapsed);
  }
  if (wake)
    wake->SetVisibility(false);
  return 1;
}

u16 NwShipVehicle::GetFrameNumber(i32 zoom, i32 rotation) const {
  u8 direction = static_cast<u8>((orientation + NwVehicleRotationOffsets[rotation]) & 31);
  return static_cast<u16>((zoom - birthZoom) * spriteRepository->GetZoomSetCount() +
      NwVehicleFrameOffsets[pitch][direction]);
}

u8 NwShipVehicle::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  if (zoom < birthZoom || currentViewType || !visible)
    return 1;
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwShipVehicle::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  if (zoom < birthZoom || currentViewType || !visible)
    return 1;
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

// Advance the ship only while its view and zoom are eligible, updating its wake visibility.
i32 NwShipVehicle::Tick(u32 elapsed) {
  if (!viewer) {
    viewer = NwGetStreetCurrentView();
    return NwStreetVehicle::Tick(elapsed);
  }
  i32 zoom, rotation;
  viewer->GetZoomAndRotation(&zoom, &rotation);
  if (zoom >= birthZoom && currentViewType == 0) {
    if (wake)
      wake->SetVisibility(byte400 != 0);
    return NwStreetVehicle::Tick(elapsed);
  }
  if (wake)
    wake->SetVisibility(false);
  return 1;
}

// Show or hide the boat's dependent wake when it exists.
u8 NwBoatVehicle::EnableWake(bool enabled) {
  if (wake)
    wake->SetVisibility(enabled);
  return 1;
}

// Show or hide the ship's dependent wake when it exists.
u8 NwShipVehicle::EnableWake(bool enabled) {
  if (wake)
    wake->SetVisibility(enabled);
  return 1;
}

u32 NwBoatVehicle::SetOrientation(u8 value) {
  NwStreetVehicle::SetOrientation(value);
  return wake ? wake->SetOrientation(value) : 0;
}

u32 NwShipVehicle::SetOrientation(u8 value) {
  NwStreetVehicle::SetOrientation(value);
  return wake ? wake->SetOrientation(value) : 0;
}

u8 NwBoatVehicle::ChangePosition(CtCoord *position) {
  NwStreetVehicle::ChangePosition(position);
  return wake ? wake->ChangePosition(position) : 0;
}

u8 NwShipVehicle::ChangePosition(CtCoord *position) {
  NwStreetVehicle::ChangePosition(position);
  return wake ? wake->ChangePosition(position) : 0;
}

// Shut down the boat and release its dependent wake after removing it from the city.
void NwBoatVehicle::Shutdown() {
  if (!byte32)
    return;
  NwStreetVehicle::Shutdown();
  if (wake) {
    if (wake->IsInCity())
      animationManager->RemoveOccupant(static_cast<ScOccupant *>(wake));
    wake->Release();
    wake = nullptr;
  }
}

// Shut down the ship, release its route and retire its dependent wake.
void NwShipVehicle::Shutdown() {
  if (!byte32)
    return;
  NwStreetVehicle::Shutdown();
  delete[] route;
  routeCount = 0;
  routeIndex = 0;
  if (wake) {
    if (wake->IsInCity())
      animationManager->RemoveOccupant(static_cast<ScOccupant *>(wake));
    wake->Release();
    wake = nullptr;
  }
}

void NwBoatVehicle::VehicleDisappears(NwVehicle *) {
  if (wake && byte32) {
    wake->Release();
    wake = nullptr;
  }
}

void NwShipVehicle::VehicleDisappears(NwVehicle *) {
  if (wake && byte32) {
    wake->Release();
    wake = nullptr;
  }
}

bool NwBoatVehicle::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

bool NwShipVehicle::NeedVehicleDisappear(NwVehicle *, u16 *) {
  return false;
}

void NwBoatVehicle::GetStartingPoint(u32 *outX, u32 *outZ) {
  *outX = startX;
  *outZ = startZ;
}

u8 NwBoatVehicle::CitySpecificStaticInit(CtCity *city) {
  waterTerrain = ScAddRefNotNull(city->DirtBag());
  return 1;
}

u8 NwBoatVehicle::CitySpecificStaticShutdown() {
  ScRelease(&waterTerrain);
  return 1;
}

u8 NwShipVehicle::CitySpecificStaticInit(CtCity *city) {
  waterTerrain = ScAddRefNotNull(city->DirtBag());
  return 1;
}

u8 NwShipVehicle::CitySpecificStaticShutdown() {
  ScRelease(&waterTerrain);
  return 1;
}
