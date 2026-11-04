#ifndef SC3_NETWORK_NW_STREET_VEHICLE_H
#define SC3_NETWORK_NW_STREET_VEHICLE_H

#include <list>

#include "network/nw_street_producer.h"
#include "network/street.h"
#include "network/street_traffic.h"
#include "sprites/api.h"

// Vehicle constructors and fixed occupant views publish these exact geometry words for their concrete families.
enum NwVehicleGeometry : u32 {
  kNwAirVehicleGeometry = 0x4001204,
  kNwWaterVehicleGeometry = 0x2001204,
  kNwRailVehicleGeometry = 0x1021204,
  kNwSubwayVehicleGeometry = 0x1041204,
  kNwStreetVehicleGeometry = 0x1009204,
  kNwCrowdGeometry = 0x1001204,
  kNwPersonGeometry = 0x100a204,
  kNwParadeBlockGeometry = 0x1204,
};

class NwVehicleProducerInterface;
class ApBoxX;
class RsManager;
class CtAgentTree;
class CtStartSimLayer;
class NwTrafficLayer;
class NwTrafficBlock;
class OcAnimManager;
class ScDirtBag;
class ScOccupantManager;
class NwAirTrafficManager;
class NwVehicleAttrib;
class NwVehiclePathAttrib;
class NwTrafficAttrib;
class SpDefView;
class SpViewInterface;
struct NwSpriteAction;
struct NwScriptInstruction;
struct NwVehicleMoveState;
struct NwVehicleFollowerMoveState;
struct NwVehicleGraphNode;
struct NwVehicleGraphEdge;
struct ScPointXZ;
struct ScVec3F;

// The original vehicle places its occupant, vehicle and sprite interfaces at x86 offsets 0, 24 and 28.
// Only the verified initial field prefix is present; the remaining layout and lifecycle are unresolved.
class NwStreetVehicle : public NwStreetOccupant, public NwVehicle, public SpSprite {
  friend class NwStreetLayer;
public:
  NwStreetVehicle();
  ~NwStreetVehicle() override;
  virtual u8 Init(const RsKey &key); // Private bool/BYTE spelling remains unresolved.
  virtual u32 SetProducer(NwVehicleProducerInterface *value); // Observed register; nominal return remains unresolved.
  virtual void SetVisibility(bool visible); // The discarded nominal return remains unresolved.
  virtual u8 SetSpriteAttrib(SpDefinition *value); // Private bool/BYTE spelling remains unresolved.
  virtual i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel);
  virtual i32 CallScriptFunction(NwScriptInstruction *instruction);
  virtual NwVehiclePath *SelectPath(ScOccupant *occupant);
  virtual NwVehicleGraphEdge *SelectAnEdge(NwVehicleGraphNode *node, i32 x, i32 z);
  virtual void LeaveLastPath(); // Discarded source return remains unresolved.
  virtual void MakeCurrentPath(NwVehiclePath *path); // Discarded source return remains unresolved.
  static u8 StaticInit(); // Nominal bool/BYTE spelling remains unresolved.
  static u8 StaticShutdown(); // Nominal bool/BYTE spelling remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static void SetBirthZoomLevel(i32 value); // The unused nominal return remains unresolved.
  static i32 BirthZoomLevel();
  bool QueryInterface(u32 iid, void **out) override;
  void Shutdown() override;
  u32 AddRef() override;
  u32 Release() override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 SubTypeInfo() const override;
  bool InCityBoundsMicro(i32 x, i32 z);
  bool InCityBoundsCell(u32 x, u32 z);
  bool IsInState(i32 mask); // Original enum spelling remains unresolved.
  void SetState(i32 mask); // Original enum spelling and discarded return remain unresolved.
  u8 ResetState(i32 mask); // Original enum spelling and nominal return remain unresolved.
  void PopActionStack(); // Discarded source return remains unresolved.
  void PushToActionStack(u32 action); // Discarded source return remains unresolved.
  void PopStateStack(i32 size); // Discarded source return remains unresolved.
  void PushStateStack(i32 size); // Discarded source return remains unresolved.
  i32 IdleForDuration(u32 duration); // Original execution-status enum spelling remains unresolved.
  i32 IdleForDurationUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 DoNothing(); // Original execution-status enum spelling remains unresolved.
  i32 DoNothingUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 PlayAnimation(u16 action, i32 duration, i32 variation);
  i32 PlayAnimationUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 AppearOnPath(NwVehiclePath *path);
  i32 MoveAlongPathUpdateFn(u32 elapsed, i32 status, bool cancel); // Execution-status enum spelling remains unresolved.
  i32 ExecuteScript(const RsKey &key, i32 count, i32 variation);
  i32 ExecuteScriptScriptFn(NwScriptInstruction *instruction);
  i32 ExecuteScriptUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveNStages(i32 count, i32 variation, u16 action);
  i32 MoveNStagesScriptFn(NwScriptInstruction *instruction);
  i32 PlayAnimationScriptFn(NwScriptInstruction *instruction);
  i32 FollowerMoveNStagesScriptFn(NwScriptInstruction *instruction);
  i32 FollowerMoveNStages(u16 action);
  i32 WaitForPathClearance(NwVehiclePath *path);
  NwVehiclePath *GetNextPath();
  NwVehiclePath *SelectPath(i32 x, i32 altitude, i32 z);
  NwVehiclePath *SelectPathFromConnectingNodeOnNextCell(i32 cellX, i32 cellZ, NwVehicleGraphEdge *edge,
      i32 pointX, i32 pointZ);
  void SelectNextPath(NwVehiclePath *path, NwVehiclePath **out); // Discarded nominal return remains unresolved.
  u8 GetAltitude(ScOccupant *occupant, NwTrafficAttrib *attributes);
  static bool IncludeRail(ScOccupant *occupant);
  static bool IncludeRoadsAndAvenues(ScOccupant *occupant);
  static bool IncludeSubway(ScOccupant *occupant);
  void GetBoundingRectInAnimCellCoords(GeCellRect *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
  u16 GetFrameNumber(i32 zoom, i32 rotation) const;
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  void GetTrueBoundingRect(ScPointXZSigned *points) const override;
  void GetCenterOfMass(ScPointXZSigned *point) const override;
  using NwStreetOccupant::GetSpriteInst;
  SpSprite *GetSpriteInst() override;
  u8 IsVisible() override;
  i32 Width() const override;
  u8 IsAirVehicle() override;
  SpDefinition *GetSpriteAttrib() override; // Shared occupant declaration still names the concrete type.
  u8 IsEmergencyVehicle() override;
  u8 PlaceOnTransitTile(ScOccupant *occupant) override;
  u8 PlaceOnTransitTile(ScOccupant *occupant, u8 direction) override;
  i32 Length() const override;
  u8 SetFollowingVehicle(NwVehicle *vehicle) override;
  iptr SetVehicleAhead(NwVehicle *vehicle) override;
  void FollowingVehicleDisappeared(NwVehicle *vehicle) override;
  void VehicleAheadLeftPath(NwVehicle *vehicle) override;
  iptr SetFollower(NwVehicle *vehicle) override;
  iptr SetLeader(NwVehicle *vehicle) override;
  void FollowerDisappeared(NwVehicle *vehicle) override;
  void LeaderDisappeared(NwVehicle *vehicle) override;
  f32 LastMovementDist() override;
  i32 DistanceToBack(i32 length, NwVehiclePath *path) override;
  iptr LeaderEnteredPath(NwVehiclePath *path) override;
  void ChangeToSortIdOfPath(NwVehiclePath *path); // Discarded source return remains unresolved.
  void LeaveAllPaths(); // Discarded source return remains unresolved.
  void WipeActionStack(); // Discarded source return remains unresolved.
  void TickFromLeader() override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  u32 SetOrientation(u8 orientation) override;
  u8 ChangePosition(CtCoord *position) override;
  void GetRotationOffset(u8 *out) const override;
  u32 GetLayerFlags() const override;
  void SetRotationOffset(u8 rotation) override;
  u8 SetEffect(u32 effect) override;
  void SetLayer(u8 layer) override;
  u8 DemandArt(i32 zoom, i32 rotation, bool argument) override;
  i32 ReleaseArt(i32 zoom, i32 rotation) override;
  bool SetDefinition(SpDefinition *value) override;
  u8 HasGeomFlag(u32 mask) override;
  void SetNext(SpSprite *next) override;
  SpSprite *GetNext() override;

protected:
  NwVehiclePath *BuildPath(NwTrafficBlock *block, u32 x, u8 altitude, u32 z, NwVehicleGraphEdge *edge);
  i32 MoveToPoint(i32 x, i32 altitude, i32 z, i32 distance);
  i32 MoveToPointMoveDist(NwVehicleMoveState *state, f32 distance);
  i32 FollowerMoveToPointMoveDist(NwVehicleFollowerMoveState *state, f32 distance);
  u8 ComputePitch(); // Original nominal source return spelling remains unresolved.
  i32 MoveAlongPath();
  i32 FollowerMoveAlongPath();
  i32 FollowerMoveToPoint(i32 x, i32 altitude, i32 z, i32 distance);
  i32 CircularMotionInCity(i32 x, i32 z, f32 angle, f32 speed, f32 radius);
  i32 CircularMotion(i32 x, i32 z, f32 angle, f32 speed, i32 height, i32 margin);
  i32 SpiralMotion(const ScVec3F &origin, const ScVec3F &velocity);
  f64 QueryAltitudeAndComputeYGradientPerUnitAngle(i32 x, i32 z, f32 radius, f32 angle, i32 height, i32 margin);
  i32 DecideSirenState(); // Original enum spelling remains unresolved.
  static f64 CalculateAngle(i32 x0, i32 z0, i32 x1, i32 z1); // x87 result carrier; nominal source return unresolved.
  void ChangeVehicleAngle(f32 delta, f32 distance); // The discarded nominal return remains unresolved.
  bool InsideStaticOccupant(i32 x, i32 altitude, i32 z);
  i32 FollowTerrainMoveToPoint(i32 x, i32 z, i32 height, i32 margin, f32 speed);
  void QueryAltitudeAndComputeSpeedComponents(f32 directionX, f32 directionZ, i32 height, i32 margin,
      f32 *horizontalSpeed, f32 *verticalSpeed); // Discarded nominal source return remains unresolved.
  i32 OnWaterMoveToPoint(i32 x, i32 z, f32 speed, bool reverse);
  i32 ComputeYForTerrainFollowing(i32 x, i32 z, i32 height, i32 margin);
private:
  i32 MoveNStagesUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveToPointUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 FollowerMoveNStagesUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 FollowerMoveAlongPathUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 FollowerMoveToPointUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 FollowTerrainMoveToPointUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 OnWaterMoveToPointUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 WaitForPathClearanceUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 AppearOnPathUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 SpiralMotionUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 CircularMotionUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 CircularMotionInCityUpdateFn(u32 elapsed, i32 status, bool cancel);
  using UpdateFn = i32 (NwStreetVehicle::*)(u32, i32, bool);
  using ScriptFn = i32 (NwStreetVehicle::*)(NwScriptInstruction *);
  using TileFilter = bool (*)(ScOccupant *);
  static UpdateFn updateFunctions[17];
  static ScriptFn scriptFunctions[4];
  static TileFilter pathTileFilters[3];
protected:
  // Byte 32's role remains unresolved. The named fields retain their original x86 positions.
  u8 byte32, vehicleState, visible;
  u32 geometryFlags, effect;
  u8 byte44, occupantManagerIndex;
  u32 animationElapsed, animationInterval;
  u8 orientation;
  f32 angle;
  i8 pitch;
  u8 frame;
protected:
  NwVehicleProducerInterface *producer;
  // Native pointer-bearing action records widen on 64-bit hosts; the original x86 stack remains 120 bytes.
  u8 stateStack[sizeof(void *) == 8 ? 240 : 120];
  u32 actionStack[12];
  i32 stateStackTop, actionStackTop;
  NwVehicleAttrib *attributes;
  SpDefView *spriteRepository;
  NwSpriteAction *actionDescriptor;
  NwVehicle *vehicleAhead, *followingVehicle;
  f32 speed;
  i32 pathLength;
  f32 remainingDistance;
  NwVehiclePath *paths[4], *leaderPaths[4];
  i32 pathTop, leaderPathTop;
  NwVehicle *follower, *leader;
  f32 movementDistance, movementCarry;
  i32 sortId;
  u8 movementCounter;
  i32 movementStatus;
  static ApBoxX *boxX;
  static RsManager *resourceManager;
  static u8 staticInitialized;
  static i32 birthZoomLevel;
  static i16 zoomOffsets[5];
protected:
  static i32 cityXMicroUnits, cityZMicroUnits;
  static u32 maxCellX, maxCellZ;
  static CtAgentTree *agentTypeTree;
  static ScOccupantManager *occupantManagers[3];
  static OcAnimManager *animationManager;
  static CtStartSimLayer *streetLayer;
  static NwTrafficLayer *trafficLayer;
  static ScDirtBag *dirtBag;
};

// The original rail and subway vehicles each derive directly from the street vehicle.
// Their remaining fields and lifecycle are still unresolved.
// The dependent vehicle is the original wake occupant; its inherited sprite implementation remains incomplete.
class NwDependentVehicle : public NwStreetVehicle {
public:
  NwDependentVehicle();
  u8 SubTypeInfo() const override;
};

// The original riot crowd adds its retained disaster interface at complete-object offset 348.
class NwRiotCrowdObject : public NwStreetVehicle, public NwRiotCrowd {
public:
  NwRiotCrowdObject();
  ~NwRiotCrowdObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  void Shutdown() override;
  i32 Tick(u32 elapsed) override;
  u32 GetGeomFlag() override;
  u8 SubTypeInfo() const override;
  using NwStreetVehicle::PlaceOnTransitTile;
  using NwStreetVehicle::GetLocation;
  u8 PlaceOnTransitTile(ScOccupant *occupant) override;
  void SetAssociatedRiotInstance(DsRiotState *instance) override;
  void GetLocation(u32 *x, u32 *z) override;
  void GetLocationMicro(i32 *x, i32 *altitude, i32 *z) override;
  void Disburse() override;
  void SetMinAndMaxTimeBetweenFireballThrows(i32 minimum, i32 maximum) override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) override;
  i32 CallScriptFunction(NwScriptInstruction *instruction) override;
  void LeaveLastPath() override;
  void MakeCurrentPath(NwVehiclePath *path) override;
  static u8 StaticInit();
  static u8 StaticShutdown();
  static void SetCurrentViewType(i32 value); // Discarded nominal return remains unresolved.

private:
  u16 GetFrameNumber(i32 zoom, i32 rotation) const;
  void ThrowFireBall(); // The original discarded return remains unresolved.
  i32 RiotCrowdMovement();
  i32 RiotCrowdMovementUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 RiotCrowdMovementScriptFn(NwScriptInstruction *instruction);
  i32 EnterPathInVehicleLessTile(NwVehiclePath *path);
  i32 EnterPathInVehicleLessTileUpdateFn(u32 elapsed, i32 status, bool cancel);
  using UpdateFn = i32 (NwRiotCrowdObject::*)(u32, i32, bool);
  using ScriptFn = i32 (NwRiotCrowdObject::*)(NwScriptInstruction *);
  DsRiotState *riot;
  ScOccupant *transitTile;
  u32 fireballElapsed, nextFireball;
  i32 fireballMinimum, fireballMaximum;
  u8 disbursing;
  static i32 currentViewType;
  static UpdateFn updateFunctions[2];
  static ScriptFn scriptFunctions[1];
};

// The original parade vehicle adds its parade identity at complete-object offset 348.
class NwParadeBlockObject : public NwStreetVehicle, public NwParadeBlock {
public:
  NwParadeBlockObject();
  ~NwParadeBlockObject() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  void Shutdown() override;
  i32 Tick(u32 elapsed) override;
  u32 GetGeomFlag() override;
  u8 SubTypeInfo() const override;
  using NwStreetVehicle::GetLocation;
  using NwStreetVehicle::GetOrientation;
  u8 PlaceOnTransitTile(ScOccupant *occupant) override;
  u8 PlaceOnTransitTile(ScOccupant *occupant, u8 direction) override;
  void SetAssociatedParadeInstance(DsParadeMotion *instance) override;
  void GetLocation(u32 *x, u32 *z) override;
  void GetLocationMicro(i32 *x, i32 *altitude, i32 *z) override;
  void Disburse() override;
  u8 IsBored() override;
  u8 GetOrientation() override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) override;
  i32 CallScriptFunction(NwScriptInstruction *instruction) override;
  NwVehiclePath *SelectPath(ScOccupant *occupant) override;
  NwVehicleGraphEdge *SelectAnEdge(NwVehicleGraphNode *node, i32 x, i32 z) override;
  u32 SetOrientation(u8 orientation) override;
  u8 ChangePosition(CtCoord *position) override;
  void LeaveLastPath() override;
  void MakeCurrentPath(NwVehiclePath *path) override;
  static u8 StaticInit();
  static u8 StaticShutdown();
  static void SetCurrentViewType(i32 value); // Discarded nominal return remains unresolved.

private:
  u16 GetFrameNumber(i32 zoom, i32 rotation) const;
  i32 ParadeBlockMovement();
  i32 ParadeBlockMovementUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 ParadeBlockMovementScriptFn(NwScriptInstruction *instruction);
  i32 EnterPathInVehicleLessTile(NwVehiclePath *path);
  i32 EnterPathInVehicleLessTileUpdateFn(u32 elapsed, i32 status, bool cancel);
  using UpdateFn = i32 (NwParadeBlockObject::*)(u32, i32, bool);
  using ScriptFn = i32 (NwParadeBlockObject::*)(NwScriptInstruction *);
  DsParadeMotion *parade;
  ScOccupant *transitTile;
  u8 disbursing, bored;
  static i32 currentViewType;
  static UpdateFn updateFunctions[2];
  static ScriptFn scriptFunctions[1];
};

// The original strike crowd adds one retained transit occupant after the street-vehicle base.
class NwStrikeCrowd : public NwStreetVehicle {
public:
  NwStrikeCrowd();
  ~NwStrikeCrowd() override;
  void Shutdown() override;
  u32 GetGeomFlag() override;
  using NwStreetVehicle::PlaceOnTransitTile;
  u8 PlaceOnTransitTile(ScOccupant *occupant) override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  void LeaveLastPath() override;

private:
  u16 GetFrameNumber(i32 zoom, i32 rotation) const;
  i32 StrikeCrowdRoutine();
  i32 EnterPathInTile(NwVehiclePath *path);
  ScOccupant *transitTile;
};

// The original boat adds producer and wake interfaces at x86 offsets 348 and 352.
class NwBoatVehicle : public NwStreetVehicle, public NwVehicleProducerInterface, public NwBoat {
public:
  NwBoatVehicle();
  u8 Init(const RsKey &key) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 EnableWake(bool enabled) override;
  u32 SetOrientation(u8 orientation) override;
  u8 ChangePosition(CtCoord *position) override;
  u8 SubTypeInfo() const override;
  u8 PutInCell(u32 x, u32 z);
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) override;
  static u8 StaticInit();
  static u8 StaticShutdown();
  void GetStartingPoint(u32 *x, u32 *z); // Discarded nominal return remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static void SetCurrentViewType(i32 view); // Discarded nominal return remains unresolved.
private:
  u16 GetFrameNumber(i32 zoom, i32 rotation) const;
  bool FindStraightWaterPoint(f32 angle, i32 distance, i32 *x, i32 *z);
  u8 FindAnyNeighboringWaterPoint(i32 distance, u8 *orientation, i32 *x, i32 *z);
  u8 FindAWaterPoint(f32 angle, i32 distance, i32 *x, i32 *z, f32 *resultAngle);
  i32 BoatRoutine();
  i32 BoatRoutineUpdateFn(u32 elapsed, i32 status, bool cancel);
  using UpdateFn = i32 (NwBoatVehicle::*)(u32, i32, bool);
  static UpdateFn updateFunction;
  u16 startZ, startX;
  i8 birthZoom;
  NwDependentVehicle *wake;
  f32 minimumSpeed, maximumSpeed;
  SpViewInterface *viewer;
  static ScDirtBag *waterTerrain;
  static i32 currentViewType;
};

// The original ship adds the same two secondary identities and retains its spline route and wake.
class NwShipVehicle : public NwStreetVehicle, public NwVehicleProducerInterface, public NwShip {
public:
  NwShipVehicle();
  u8 Init(const RsKey &key) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void Shutdown() override;
  void VehicleDisappears(NwVehicle *vehicle) override;
  bool NeedVehicleDisappear(NwVehicle *vehicle, u16 *counter) override;
  u8 EnableWake(bool enabled) override;
  u32 SetOrientation(u8 orientation) override;
  u8 ChangePosition(CtCoord *position) override;
  u8 SubTypeInfo() const override;
  u8 MakeParkedShip(ScPointXZ *points, u32 count);
  u8 MakeArrivingShip(ScPointXZ *points, u32 count);
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  i32 Tick(u32 elapsed) override;
  i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) override;
  static u8 StaticInit();
  static u8 StaticShutdown();
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static void SetCurrentViewType(i32 view); // Discarded nominal return remains unresolved.
private:
  u16 GetFrameNumber(i32 zoom, i32 rotation) const;
  void EvaluateBSplinePoint(i32 *x, i32 *z); // Discarded nominal return remains unresolved.
  u8 GetNextPointIn(i32 *x, i32 *z); // Private bool/BYTE spelling remains unresolved.
  u8 GetNextPointOut(i32 *x, i32 *z); // Private bool/BYTE spelling remains unresolved.
  void GetSetForOutBoundJourney(); // Discarded nominal return remains unresolved.
  i32 ShipRoutine();
  i32 ApproachSeaport();
  i32 MoveToNeighbor();
  i32 WaitAtPier();
  i32 WaitForTurnAround();
  i32 ShipRoutineUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 ApproachSeaportUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveToNeighborUpdateFn(u32 elapsed, i32 status, bool cancel);
  using UpdateFn = i32 (NwShipVehicle::*)(u32, i32, bool);
  static UpdateFn updateFunctions[3];
  i32 shipAction;
  ScPointXZ *route;
  u32 routeCount, routeIndex;
  i32 splineStep;
  i8 birthZoom;
  u32 controlPoints[4];
  NwDependentVehicle *wake;
  u8 byte400;
  u32 routeBoundary;
  SpViewInterface *viewer;
  static ScDirtBag *waterTerrain;
  static i32 currentViewType;
};

class NwRailVehicle : public NwStreetVehicle {
public:
  NwRailVehicle();
  using NwStreetVehicle::PlaceOnTransitTile;
  u8 PlaceOnTransitTile(ScOccupant *occupant, u8 direction) override;
  u8 SubTypeInfo() const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u16 GetFrameNumber(i32 zoom, i32 rotation) const; // Nominal source return spelling remains unresolved.
  static void SetCurrentViewType(i32 value); // The unused nominal return remains unresolved.
  static void SetBirthZoomLevel(i32 value); // The unused nominal return remains unresolved.
  static i32 BirthZoomLevel();
private:
  static i32 birthZoomLevel, currentViewType;
};

class NwSubwayVehicle : public NwStreetVehicle {
public:
  NwSubwayVehicle();
  using NwStreetVehicle::PlaceOnTransitTile;
  u8 PlaceOnTransitTile(ScOccupant *occupant, u8 direction) override;
  u8 SubTypeInfo() const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u16 GetFrameNumber(i32 zoom, i32 rotation) const; // Nominal source return spelling remains unresolved.
  static void SetCurrentViewType(i32 value); // The unused nominal return remains unresolved.
  static void SetBirthZoomLevel(i32 value); // The unused nominal return remains unresolved.
  static i32 BirthZoomLevel();
private:
  static i32 birthZoomLevel, currentViewType;
};

// The helicopter derives directly from the street vehicle; its instance tail is not yet recovered.
class NwHelicopter : public NwStreetVehicle {
public:
  NwHelicopter();
  static u8 StaticInit();
  static u8 StaticShutdown();
  i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) override;
  u8 MakeDisasterFightingHelicopter(i32 startX, i32 startZ, i32 targetX, i32 targetZ);
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static void SetCurrentViewType(i32 value); // The unused nominal return remains unresolved.
  void GetAPointOnTheEdgeOfTheCity(u8 side, i32 *pointX, i32 *pointZ); // Discarded source return remains unresolved.
  u8 MakeNormalRoutineHelicopter(); // The private bool/BYTE spelling remains unresolved.
  u8 IsNormalRoutineHelicopter(); // The private bool/BYTE spelling remains unresolved.
  void EndDispatchMission(); // The discarded nominal return remains unresolved.
  u8 IsAirVehicle() override;
  u8 SubTypeInfo() const override;
  u16 GetFrameNumber(i32 zoom, i32 rotation) const; // Nominal source return spelling remains unresolved.
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
private:
  i32 NormalHelicopterRoutine();
  i32 DisasterFightingRoutine(u32 startX, u32 startZ, u32 targetX, u32 targetZ);
  i32 NormalHelicopterRoutineUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 DisasterFightingRoutineUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveCircular();
  i32 MoveStraight();
  i32 MoveToNeighbor();
  using UpdateFn = i32 (NwHelicopter::*)(u32, i32, bool);
  static UpdateFn updateFunctions[2];
  u8 normalRoutine, dispatchEnded;
  static u32 cityMidpointX, cityMidpointZ;
  static ScDirtBag *dirtBag;
  static i32 currentViewType;
};

// Linux Redirect and list symbols identify two animation-coordinate words per target.
struct NwAirTarget {
  u32 x, z;
};

// The plane derives directly from the street vehicle; the parent instance tail remains incomplete.
class NwAeroplane : public NwStreetVehicle {
public:
  NwAeroplane();
  static u8 StaticInit(); // Private bool/BYTE spelling remains unresolved.
  static u8 StaticShutdown(); // Private bool/BYTE spelling remains unresolved.
  i32 ExecuteCurrentUpdateFn(u32 elapsed, i32 status, bool cancel) override;
  u8 MakeArrivingPlane(i32 x, u32 z); // The private bool/BYTE source spelling remains unresolved.
  u8 MakeDepartingPlane(u32 x, u32 z, u8 orientation); // The private bool/BYTE spelling remains unresolved.
  u8 MakeDisasterFightingPlane(i32 startX, i32 startZ, i32 targetX, i32 targetZ);
  i32 CurrentAction(); // Original enum spelling remains unresolved.
  u32 SetProducer(NwVehicleProducerInterface *value) override; // Observed x86 register; source return remains unresolved.
  void SetPlaneAction(i32 action); // Original enum spelling and discarded return remain unresolved.
  u8 IsNormalRoutinePlane(); // Private bool/BYTE spelling remains unresolved.
  void EndDispatchMission(); // The discarded nominal return remains unresolved.
  u8 Redirect(u32 cellX, u32 cellZ); // Private bool/BYTE spelling remains unresolved.
  static void SetCurrentViewType(i32 value); // The discarded nominal return remains unresolved.
  u8 IsAirVehicle() override;
  u8 SubTypeInfo() const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  u16 GetFrameNumber(i32 zoom, i32 rotation) const; // Nominal source return spelling remains unresolved.
  i32 ApproachAirport();
  i32 Descend();
  i32 DoPlaneRoutine();
  i32 GetPermissionForLanding();
  i32 GetPermissionToTakeOff();
  i32 TakeOff();
private:
  i32 DisasterFightingRoutine(u32 startX, u32 startZ, u32 targetX, u32 targetZ);
  i32 TouchDown();
  i32 SlowDownOnRunway();
  i32 TurnIntoTaxiway();
  i32 MoveAlongTaxiway();
  i32 TurnIntoRunway();
  i32 SpeedUpOnRunway();
  i32 DoCircularMotionOnTakeOff();
  i32 MoveToNeighbor();
  i32 DoPlaneRoutineUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 DescendUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 GetPermissionForLandingUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 TouchDownUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 SlowDownOnRunwayUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 TurnIntoTaxiwayUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveAlongTaxiwayUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 GetPermissionToTakeOffUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 TurnIntoRunwayUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 SpeedUpOnRunwayUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 TakeOffUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 DisasterFightingRoutineUpdateFn(u32 elapsed, i32 status, bool cancel);
  using UpdateFn = i32 (NwAeroplane::*)(u32, i32, bool);
  static UpdateFn updateFunctions[12];
  static NwVehiclePathAttrib *straightPathAttributes[4], *turnPathAttributes[8];
  static RsManager *resourceManager;
  u8 normalRoutine, dispatchEnded;
  std::list<NwAirTarget> targets;
  i32 planeAction;
  NwAirTrafficManager *manager;
  static i32 currentViewType;
};

#endif
