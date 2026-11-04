#ifndef SC3_NETWORK_STREET_H
#define SC3_NETWORK_STREET_H

#include "common/sc3.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "geometry/ge_types.h"
#include "network/api.h"
#include "occupants/animation_manager.h"
#include "sprites/api.h"

class NwPeoplePathAttrib;
class NwTrafficBlockObject;
class NwPeopleInterface;
struct NwPeopleGraphEdge;

// Original internal people-path virtual order. Discarded returns and private bool/BYTE spellings remain unresolved.
class NwPeoplePath {
public:
  virtual ~NwPeoplePath();
  virtual void Rewind() = 0;
  virtual void GetStartPoint(i32 *x, i32 *altitude, i32 *z) = 0;
  virtual u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) = 0;
  virtual u8 GetEndOrientation() = 0;
  virtual void GetEndCell(u32 *x, u32 *z) = 0;
  virtual u8 EnterPerson(NwPeopleInterface *person) = 0;
  virtual void PersonLeftPath(NwPeopleInterface *person) = 0;
  virtual NwPeopleGraphEdge *GetEdge() = 0;
};

// The forward path retains its traffic block and borrows the edge and people-path resource.
class NwPeopleForwardPath : public NwPeoplePath {
public:
  NwPeopleForwardPath(i32 x, i32 altitude, i32 z, NwTrafficBlockObject *block, NwPeopleGraphEdge *edge);
  ~NwPeopleForwardPath() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  void GetEndCell(u32 *x, u32 *z) override;
  u8 EnterPerson(NwPeopleInterface *person) override;
  void PersonLeftPath(NwPeopleInterface *person) override;
  NwPeopleGraphEdge *GetEdge() override;

private:
  i32 pointIndex, x, altitude, z;
  NwPeoplePathAttrib *attributes;
  NwTrafficBlockObject *block;
  NwPeopleGraphEdge *edge;
};

// The backward path retains the same inputs and traverses the resource in reverse.
class NwPeopleBackwardPath : public NwPeoplePath {
public:
  NwPeopleBackwardPath(i32 x, i32 altitude, i32 z, NwTrafficBlockObject *block, NwPeopleGraphEdge *edge);
  ~NwPeopleBackwardPath() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  void GetEndCell(u32 *x, u32 *z) override;
  u8 EnterPerson(NwPeopleInterface *person) override;
  void PersonLeftPath(NwPeopleInterface *person) override;
  NwPeopleGraphEdge *GetEdge() override;

private:
  i32 pointIndex, x, altitude, z;
  NwPeoplePathAttrib *attributes;
  NwTrafficBlockObject *block;
  NwPeopleGraphEdge *edge;
};

// The original straight people path stores two endpoints and does not retain a traffic block.
class NwPeopleStraightPath : public NwPeoplePath {
public:
  // The six-coordinate constructor's original x87 orientation rounding remains unresolved.
  NwPeopleStraightPath(i32 startX, i32 startAltitude, i32 startZ, i32 endX, i32 endAltitude, i32 endZ);
  NwPeopleStraightPath(i32 startX, i32 startAltitude, i32 startZ, i32 endX, i32 endAltitude, i32 endZ, u8 orientation);
  ~NwPeopleStraightPath() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  void GetEndCell(u32 *x, u32 *z) override;
  u8 EnterPerson(NwPeopleInterface *person) override;
  void PersonLeftPath(NwPeopleInterface *person) override;
  NwPeopleGraphEdge *GetEdge() override;

private:
  i8 pointIndex;
  i32 startX, startAltitude, startZ, endX, endAltitude, endZ;
  u8 orientation;
};

// Original automata iterator filter; each retained filter also retains its module director.
class NwAutomataFilter : public OcIteratorTest {
public:
  NwAutomataFilter();
  virtual ~NwAutomataFilter();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;

private:
  u32 references;
};

class NwPeopleRectFilter : public NwAutomataFilter {
public:
  NwPeopleRectFilter();
  bool Test(ScOccupant *occupant) override;
  void SetRectangles(GeCellRect *rectangles, i32 *count); // Discarded nominal return remains unresolved.

private:
  GeCellRect rectangles[8];
  i32 count;
};

class NwRoadRectFilter : public NwAutomataFilter {
public:
  NwRoadRectFilter();
  bool Test(ScOccupant *occupant) override;
  void SetRectangles(GeCellRect *rectangles, i32 *count); // Discarded nominal return remains unresolved.

private:
  GeCellRect rectangles[8];
  i32 count;
};

class NwPeopleFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRoadVehicleFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwNonEmergencyRoadFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwPeopleRoadFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwRailVehicleFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwSubwayVehicleFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class NwBoatFilter : public NwAutomataFilter {
public:
  bool Test(ScOccupant *occupant) override;
};

class ApGameInterface;
class ApBoxX;
class ApPreferences;
struct ApPerformanceInfo;
class CtCity;
class CtAgentTree;
class ScLanguageManager;
class ScMessageServer;
class RsManager;
class SpTickManager;
class SpCityWindowView;
class SpViewInterface;

// The street-simulation director registers the thirteen original class factories in their source order.
class NwStreetDirector : public ScDllDirector {
public:
  NwStreetDirector();
  ~NwStreetDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
};

ScDirector *NwGetStreetDirector();
ScFramework *NwGetStreetFramework();
ApGameInterface *NwGetStreetApplication();
CtCity *NwGetStreetCity();
ApBoxX *NwGetStreetBoxX();
SpCityWindowView *NwGetStreetCityWindow();
SpViewInterface *NwGetStreetCurrentView();
ApPreferences *NwGetStreetPreferences();
ScLanguageManager *NwGetStreetLanguageManager();
CtAgentTree *NwGetStreetAgentTypeTree();
RsManager *NwGetStreetResourceManager();
SpTickManager *NwGetStreetTickManager();
ApPerformanceInfo *NwGetStreetPerformanceInfo();
ScMessageServer *NwGetStreetMessageServer();
u32 NwAddStreetModuleRef();
u32 NwRemoveStreetModuleRef();

// The street-layer implementation still has unresolved initialization and shutdown dependencies.
ScUnknown *NwCreateStreetLayer();
ScUnknown *NwCreateStreetClass21FD6EEF();
ScUnknown *NwCreateStreetClass025C6D8B();
ScUnknown *NwCreatePeoplePathAttribFactory();
ScUnknown *NwCreateVehiclePathAttribFactory();
ScUnknown *NwCreateStreetScriptFactory();
ScUnknown *NwCreateAnimSpriteAttribFactory();
ScUnknown *NwCreatePersonAttribFactory();
ScUnknown *NwCreateVehicleAttribFactory();
ScUnknown *NwCreateTrafficFactory();
ScUnknown *NwCreateTrafficAttribFactory();
ScUnknown *NwCreateTrainProducerAttribFactory();
ScUnknown *NwCreateAirTrafficAttribFactory();

class NwPeopleProducerInterface;
class RsManager;
class CtStartSimLayer;
class OcAnimManager;
class ScDirtBag;
class ScOccupantManager;
class RsStream;
class NwPersonAttrib;
class NwPeoplePath;
struct NwPeopleGraphEdge;
struct NwPeopleGraphNode;
class NwPeopleGraph;
class SpDefView;
struct NwSpriteAction;
struct NwScriptInstruction;
struct ScVec3F;

// The original abstract street-simulation occupant owns its city flag, reference count and three coordinates.
// Its remaining pure virtual operations are supplied by derived people and vehicle classes.
class NwStreetOccupant : public ScOccupant {
public:
  NwStreetOccupant();
  virtual ~NwStreetOccupant();
  virtual f32 GetAltWorldUnits() const; // Original float/double spelling remains unresolved.
  virtual void GetBoundingRectInAnimCellCoords(GeCellRect *out) const;
  virtual void SetAltAnimCells(u16 altitude); // Discarded nominal return remains unresolved.
  virtual void SetLocationInAnimCoords(u32 x, u32 z); // Discarded nominal return remains unresolved.
  virtual u8 SerialRead(CtCity *city, RsStream *stream); // Private bool/BYTE spelling remains unresolved.
  virtual u8 SerialWrite(CtCity *city, RsStream *stream); // Private bool/BYTE spelling remains unresolved.
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *context) override;
  bool Init(GeAttrib *attributes) override;
  bool Init(RsKey *key) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  u32 GetOccLayerFlag() override;
  void SetOccLayerFlag(u32 flags) override;
  u8 MajorType() const override;
  u32 GetSubTypeData(u32 mask) override;
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  bool CanSimRemove() const override;
  void SetSimRemove(bool enabled) override;
  bool CanUserRemove() const override;
  void SetUserRemove(bool enabled) override;
  bool GetName(ScStringView *out) override;
  GeAttrib *GetAttribData() override;
  bool GetSubSystemData(u32 iid, void **out) override;
  u32 TypeId() const override;
  CtAgentType *CityAgentType() override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  u32 GetSoundObjectId(i32 index) override;
  using ScOccupant::GetSpriteAttrib;
  using ScOccupant::GetSpriteInst;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpSprite *GetSpriteInst(u32 layer) override;
  u32 GetDefaultLayerFlag() const override;
  u8 SetDefaultLayerFlag(u32 layer) override;
  void GetOrientation(u16 *x, u16 *y, u16 *z) const override;
  bool GetRotationFlag(u32 *rotation) const override;
  void GetLocation(CtCoord *out) const override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  u8 GetAlt() const override;
  void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  u16 GetAltAnimCells() const override;
  void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
  u8 SizeInCellsX() const override;
  u8 SizeInCellsZ() const override;
  u8 SizeInCellsY() const override;
  u32 SizeInAnimCellsZ() const override;
  u32 SizeInAnimCellsX() const override;
  u32 SizeInAnimCellsY() const override;
  void SetLocation(const CtCoord &position) override;
  void SetLocationInCellCoords(u32 x, u32 z) override;
  void SetAlt(u8 altitude) override;
  bool GetMarkBit() const override;
  void SetMarkBit(bool value) override;
  void SetSubTypeInfo(u8 type) override;
  void SetOrientation(u16 x, u16 y, u16 z) override;
  u8 SetRotationFlag(u32 flag) override;
  u32 GetMemUse() const override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

private:
  u8 cityFlags;
  u32 references;
protected:
  i32 x, altitude, z;
};

// The installed people interface has one placement slot after the Unknown prefix.
class NwPeopleInterface : public ScUnknown {
public:
  virtual u8 PlaceOnTransitTile(ScOccupant *occupant) = 0; // Nominal bool/BYTE spelling remains unresolved.
};

// The original person has occupant, people and sprite views at x86 offsets 0, 24 and 28.
// Only the verified initial field prefix is present; the remaining layout and lifecycle are unresolved.
class NwPerson : public NwStreetOccupant, public NwPeopleInterface, public SpSprite {
  friend class NwStreetLayer;
public:
  NwPerson();
  ~NwPerson() override;
  void Shutdown() override;
  i32 Tick(u32 elapsed) override;
  i32 PanicBehavior();
  i32 SpiralMotion(const ScVec3F &origin, const ScVec3F &velocity);
  u8 Init(const RsKey &key); // Private bool/BYTE source spelling remains unresolved.
  using UpdateFn = i32 (NwPerson::*)(u32 elapsed, i32 status, bool cancel);
  void PushStateStack(i32 size); // Discarded nominal return remains unresolved.
  void PopStateStack(i32 size); // Discarded nominal return remains unresolved.
  void PushToActionStack(UpdateFn update); // Discarded nominal return remains unresolved.
  void PopActionStack(); // Discarded nominal return remains unresolved.
  void WipeActionStack(); // Discarded nominal return remains unresolved.
  i32 MoveNStages(i32 count, i32 variation, u16 action, i32 duration, i32 durationVariation);
  i32 PlayAnimation(u16 action, i32 duration, i32 variation);
  i32 ExecuteScript(const RsKey &key, i32 count, i32 variation);
  i32 WaitForPathClearance(NwPeoplePath *path);
  i32 MoveAlongPath(u16 action, f32 speed, NwPeoplePath *path);
  i32 MoveToPoint(u16 action, f32 speed, i32 x, i32 altitude, i32 z);
  NwPeopleGraphEdge *SelectAnEdge(u32 *indices, u32 count, NwPeopleGraphNode *node, bool allowReverse, u8 orientation);
  bool GetPathGraphAndNodeForMyPosition(NwPeopleGraph **graph, NwPeopleGraphNode **node);
  NwPeoplePath *BuildPath(u32 cellX, u32 cellZ, NwPeopleGraphEdge *edge);
  NwPeoplePath *ChooseNextPathGraph(u8 orientation, NwPeopleGraph **graph, NwPeopleGraphNode **node);
  NwPeopleGraphEdge *SelectNextEdge(NwPeopleGraphNode *node, u8 orientation);
  void GetPathGraphOnRoadOcc(ScOccupant *occupant, i32 x, i32 z, NwPeopleGraph **graph,
      NwPeopleGraphNode **node) const; // Discarded nominal return remains unresolved.
  static u8 StaticInit(); // Nominal bool/BYTE spelling remains unresolved.
  static u8 StaticShutdown(); // Nominal bool/BYTE spelling remains unresolved.
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  static void SetBirthZoomLevel(i32 value); // The unused nominal return remains unresolved.
  static i32 BirthZoomLevel();
  u8 PlaceOnTransitTile(ScOccupant *occupant) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 SubTypeInfo() const override;
  SpDefinition *GetSpriteAttrib() override; // Shared occupant declaration still names the concrete producer type.
  u8 SetSpriteAttrib(SpDefinition *value); // Private bool/BYTE source spelling remains unresolved.
  NwPeopleProducerInterface *GetProducer() const;
  u32 SetProducer(NwPeopleProducerInterface *value); // Observed return register; nominal source return remains unresolved.
  bool InCityBoundsMicro(i32 x, i32 z);
  bool InsideStaticOccupant(i32 x, i32 altitude, i32 z);
  static bool IncludeRoads(ScOccupant *occupant);
  bool IsInState(i32 state); // Original enum spelling remains unresolved.
  void SetState(i32 state); // Original enum spelling and discarded return remain unresolved.
  u8 ResetState(i32 state); // Original enum spelling and nominal return remain unresolved.
  u16 GetFrameNumber(i32 zoom, i32 rotation) const; // Nominal source return spelling remains unresolved.
  i32 GetW(i32 zoom, i32 rotation) const override;
  i32 GetH(i32 zoom, i32 rotation) const override;
  u8 GetRegistration(i32 zoom, i32 rotation, ScRect *out) const override;
  u8 GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 x, i32 y) const override;
  u8 Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const override;
  u8 Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const override;
  bool IsPixelOpaque(i32 x, i32 y, i32 zoom, i32 rotation) const override;
  using NwStreetOccupant::GetSpriteInst;
  SpSprite *GetSpriteInst() override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;
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

private:
  u8 byte32, personState, byte34, byte35;
  u32 effect;
  NwPeopleProducerInterface *producer;
  // Native pointer-bearing action records widen on 64-bit hosts; the original x86 stack remains 204 bytes.
  u8 stateStack[sizeof(void *) == 8 ? 408 : 204];
  UpdateFn actionStack[10];
  u8 unresolvedTail[16]; // Original fields at x86 offsets 328 through 343 remain unresolved.
  i32 stateStackTop, actionStackTop;
  NwPersonAttrib *attributes;
  SpDefView *spriteRepository;
  NwSpriteAction *actionDescriptor;
  using ScriptFn = i32 (NwPerson::*)(NwScriptInstruction *);
  i32 WaitForPathClearanceUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveAlongPathUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveNStagesUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveToPointUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 PlayAnimationUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 PanicBehaviorUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 SpiralMotionUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 ExecuteScriptUpdateFn(u32 elapsed, i32 status, bool cancel);
  i32 MoveNStagesScriptFn(NwScriptInstruction *instruction);
  i32 PlayAnimationScriptFn(NwScriptInstruction *instruction);
  i32 ExecuteScriptScriptFn(NwScriptInstruction *instruction);
  static ScriptFn scriptFunctions[3];
  static RsManager *resourceManager;
  static u8 staticInitialized;
  static i32 birthZoomLevel;
  static i16 zoomOffsets[5];
  static i32 cityXMicroUnits, cityZMicroUnits;
  static ScOccupantManager *occupantManager;
  static OcAnimManager *animationManager;
  static CtStartSimLayer *streetLayer;
  static ScDirtBag *dirtBag;
};

class NwVehiclePathAttrib;

// The original forward vehicle path owns a block reference and borrows its edge and path resource.
class NwVehicleForwardPath final : public NwVehiclePath {
public:
  NwVehicleForwardPath(NwTrafficBlock *block, i32 x, i32 altitude, i32 z, NwVehicleGraphEdge *edge);
  explicit NwVehicleForwardPath(NwVehicleForwardPath *source);
  ~NwVehicleForwardPath();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  NwVehiclePath *GetCopy() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  void GetEndPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  u8 GetPlacingOrientation() override;
  f32 GetPitch() override;
  u8 GetDistToEndFromCurrentPoint(i32 *distance) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  i32 Length() override;
  void GetEndCell(u32 *x, u32 *z) override;
  void SetSpeed(f32 speed) override;
  f32 Speed() override;
  NwVehicleGraphEdge *GetEdge() override;
  NwVehicle *LastVehicle() override;
  u8 EnterVehicle(NwVehicle *vehicle, i32 distance) override;
  void VehicleLeftPath(NwVehicle *vehicle) override;
  NwTrafficBlock *GetStrtSimTrBlock() override;
  u8 ForceEnterVehicle(NwVehicle *vehicle) override;
  void BlockPath() override;
  void UnblockPath() override;
  i32 GetReferenceMicroY() override;
  u8 SortId(i32 *id) override;

private:
  u32 references;
  i32 pointIndex, x, altitude, z;
  NwVehiclePathAttrib *attributes;
  NwTrafficBlock *block;
  NwVehicleGraphEdge *edge;
  f32 speed;
};

// The original backward vehicle path retains its traffic block and traverses the borrowed resource in reverse.
class NwVehicleBackwardPath final : public NwVehiclePath {
public:
  NwVehicleBackwardPath(NwTrafficBlock *block, i32 x, i32 altitude, i32 z, NwVehicleGraphEdge *edge);
  explicit NwVehicleBackwardPath(NwVehicleBackwardPath *source);
  ~NwVehicleBackwardPath();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  NwVehiclePath *GetCopy() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  void GetEndPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  u8 GetPlacingOrientation() override;
  f32 GetPitch() override;
  u8 GetDistToEndFromCurrentPoint(i32 *distance) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  i32 Length() override;
  void GetEndCell(u32 *x, u32 *z) override;
  void SetSpeed(f32 speed) override;
  f32 Speed() override;
  NwVehicleGraphEdge *GetEdge() override;
  NwVehicle *LastVehicle() override;
  u8 EnterVehicle(NwVehicle *vehicle, i32 distance) override;
  void VehicleLeftPath(NwVehicle *vehicle) override;
  NwTrafficBlock *GetStrtSimTrBlock() override;
  u8 ForceEnterVehicle(NwVehicle *vehicle) override;
  void BlockPath() override;
  void UnblockPath() override;
  i32 GetReferenceMicroY() override;
  u8 SortId(i32 *id) override;

private:
  u32 references;
  i32 pointIndex, x, altitude, z;
  NwVehiclePathAttrib *attributes;
  NwTrafficBlock *block;
  NwVehicleGraphEdge *edge;
  f32 speed;
};

// The original attribute-only path borrows a resource and has no traffic-block or graph-edge ownership.
class NwVehicleAttribPath final : public NwVehiclePath {
public:
  NwVehicleAttribPath(i32 x, i32 altitude, i32 z, NwVehiclePathAttrib *attributes);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  NwVehiclePath *GetCopy() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  void GetEndPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  u8 GetPlacingOrientation() override;
  f32 GetPitch() override;
  u8 GetDistToEndFromCurrentPoint(i32 *distance) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  i32 Length() override;
  void GetEndCell(u32 *x, u32 *z) override;
  void SetSpeed(f32 speed) override;
  f32 Speed() override;
  NwVehicleGraphEdge *GetEdge() override;
  NwVehicle *LastVehicle() override;
  u8 EnterVehicle(NwVehicle *vehicle, i32 distance) override;
  void VehicleLeftPath(NwVehicle *vehicle) override;
  NwTrafficBlock *GetStrtSimTrBlock() override;
  u8 ForceEnterVehicle(NwVehicle *vehicle) override;
  void BlockPath() override;
  void UnblockPath() override;
  i32 GetReferenceMicroY() override;
  u8 SortId(i32 *id) override;

private:
  u32 references;
  i32 pointIndex, x, altitude, z;
  NwVehiclePathAttrib *attributes;
  f32 speed;
};

// A straight path stores two points and their derived geometry without a block or resource.
class NwVehicleStraightPath final : public NwVehiclePath {
public:
  // Original x87 intermediate rounding near geometry boundaries remains unresolved.
  NwVehicleStraightPath(i32 startX, i32 startAltitude, i32 startZ, i32 endX, i32 endAltitude, i32 endZ);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  NwVehiclePath *GetCopy() override;
  void Rewind() override;
  void GetStartPoint(i32 *x, i32 *altitude, i32 *z) override;
  void GetEndPoint(i32 *x, i32 *altitude, i32 *z) override;
  u8 GetEndOrientation() override;
  u8 GetPlacingOrientation() override;
  f32 GetPitch() override;
  u8 GetDistToEndFromCurrentPoint(i32 *distance) override;
  u8 GetNextPoint(i32 *x, i32 *altitude, i32 *z) override;
  i32 Length() override;
  void GetEndCell(u32 *x, u32 *z) override;
  void SetSpeed(f32 speed) override;
  f32 Speed() override;
  NwVehicleGraphEdge *GetEdge() override;
  NwVehicle *LastVehicle() override;
  u8 EnterVehicle(NwVehicle *vehicle, i32 distance) override;
  void VehicleLeftPath(NwVehicle *vehicle) override;
  NwTrafficBlock *GetStrtSimTrBlock() override;
  u8 ForceEnterVehicle(NwVehicle *vehicle) override;
  void BlockPath() override;
  void UnblockPath() override;
  i32 GetReferenceMicroY() override;
  u8 SortId(i32 *id) override;

private:
  i8 pointIndex;
  i32 startX, startAltitude, startZ, endX, endAltitude, endZ;
  u8 orientation;
  f32 pitch;
  i32 length;
  u32 references;
  f32 speed;
};

struct RsKey;
class ScOccupant;
class DsRiotState;
class DsParadeMotion;
class NwVehicle;
class NwVehiclePath;

// Original STRTSIM interface 425F27F8, returned by its traffic-block factory.
// Discarded private return declarations remain qualified where register residue differs.
class NwTrafficBlock : public ScUnknown {
public:
  virtual u8 Init(const RsKey &key) = 0;
  virtual u8 Shutdown() = 0;
  virtual NwVehicle *LastVehicle(i8 path) = 0;
  virtual u8 EnterVehicle(NwVehiclePath *path, NwVehicle *vehicle, i32 mode, bool force) = 0;
  virtual void VehicleLeftPath(i8 path, NwVehicle *vehicle) = 0;
  virtual void BlockPath(NwVehiclePath *path) = 0;
  virtual void UnblockPath(NwVehiclePath *path) = 0;
  virtual void DisallowVehicleEntrance() = 0;
  virtual void AllowVehicleEntrance() = 0;
  virtual u8 ContainsVehicles() = 0;
};

// Foreign STRTSIM class225C563D, interface025C566C;12 creates a block from the original key and genuine output.
// Private nominal bool/BYTE and reference spelling remain unresolved; preserve the observed byte and pointer ABI.
class NwTrafficBlockFactory : public ScUnknown {
public:
  virtual u8 CreateStrtSimTrBlock(const RsKey &key, void **out) = 0;
};

// Actual cISSRiotCrowd identity231F75BE, returned at cSSRiotCrowd complete+348 by STRTSIM12310.
// The six real methods cover slots12..32; these declarations provide no foreign implementation.
class NwRiotCrowd : public ScUnknown {
public:
  // PC12=ECEE/E782 consumes the retained tile and returns raw AL0/1; nominal bool/BYTE remains qualified.
  virtual u8 PlaceOnTransitTile(ScOccupant *occupant) = 0;
  // PC16=82B6 retains the actual private disaster interface; ignored AddRef residue does not prove a return type.
  virtual void SetAssociatedRiotInstance(DsRiotState *instance) = 0;
  // PC20=7DB6 writes unsigned cell X/Z after signed shifts; PC24=7DD7 writes signed micro X/altitude/Z.
  virtual void GetLocation(u32 *x, u32 *z) = 0;
  virtual void GetLocationMicro(i32 *x, i32 *altitude, i32 *z) = 0;
  // PC28=E53A sets the disburse flag; PC32=E9DB consumes signed interval bounds, matching exact Linux symbols.
  // Their discarded nominal source results remain qualified.
  virtual void Disburse() = 0;
  virtual void SetMinAndMaxTimeBetweenFireballThrows(i32 minimum, i32 maximum) = 0;
};

// Actual cISSParadeBlock identity64CCEC24, returned at cSSParadeBlock complete+348 by STRTSIM123A6.
// All eight original methods12..40 are established; concrete production remains with street simulation.
class NwParadeBlock : public ScUnknown {
public:
  // MSVC reverses the overload group: default placement is16=8573, explicit unsigned-byte direction is12=8568.
  virtual u8 PlaceOnTransitTile(ScOccupant *occupant) = 0;
  virtual u8 PlaceOnTransitTile(ScOccupant *occupant, u8 direction) = 0;
  // Original20=82B6 retains the real private callback; ignored AddRef residue does not prove a source result.
  virtual void SetAssociatedParadeInstance(DsParadeMotion *instance) = 0;
  // Original24=7DB6 and28=7DD7 share the riot coordinate getters and genuine output order.
  virtual void GetLocation(u32 *x, u32 *z) = 0;
  virtual void GetLocationMicro(i32 *x, i32 *altitude, i32 *z) = 0;
  // Original32=7C2A sets the disburse byte; the discarded nominal source result remains qualified.
  virtual void Disburse() = 0;
  // Original36=7BAD and40=7BB1 return their raw stored bytes without widening or normalization.
  virtual u8 IsBored() = 0;
  virtual u8 GetOrientation() = 0;
};

#endif
