#include "network/street.h"

#include <stdlib.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "network/api.h"
#include "network/nw_street_producer.h"
#include "network/nw_street_vehicle.h"
#include "network/street_traffic.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "terrain/api.h"

// Person animation advances at its original interval; blocked paths expire at a separate timeout.
enum NwPersonTiming : i32 {
  kNwPersonFrameInterval = 0x493e0,
  kNwPersonPathClearanceTimeout = 0x2625a00,
};

NwAutomataFilter::NwAutomataFilter() : references(0) {
}

NwAutomataFilter::~NwAutomataFilter() {
}

// Publish and retain the filter for the original unknown or occupant-iterator-test identity.
bool NwAutomataFilter::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, OcIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain both the filter and its module director, returning the live filter count after the director callback.
u32 NwAutomataFilter::AddRef() {
  ++references;
  NwGetStreetDirector()->AddRef();
  return references;
}

// Release the director hold before decrementing a live filter reference and destroy the filter at zero.
u32 NwAutomataFilter::Release() {
  if (references) {
    NwGetStreetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

bool NwAutomataFilter::Test(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomAutomata) == kOcGeomAutomata;
}

NwPeopleRectFilter::NwPeopleRectFilter() : count(0) {
}

// Copy at most eight inclusive rectangles in the original component order.
void NwPeopleRectFilter::SetRectangles(GeCellRect *input, i32 *inputCount) {
  count = *inputCount;
  if (count > 8)
    count = 8;
  for (i32 i = 0; i < count; ++i) {
    rectangles[i].left = input[i].left;
    rectangles[i].top = input[i].top;
    rectangles[i].right = input[i].right;
    rectangles[i].bottom = input[i].bottom;
  }
}

// Select a people occupant whose cell coordinates fall in at least one stored inclusive rectangle.
bool NwPeopleRectFilter::Test(ScOccupant *occupant) {
  if ((occupant->GetGeomFlag() & kOcGeomPerson) != kOcGeomPerson)
    return false;
  u32 x = 0, z = u32(uptr(occupant));
  occupant->GetLocationInCellCoords(&x, &z);
  i32 limit = count;
  for (i32 i = 0; i < limit; ++i)
    if (x >= rectangles[i].left && x <= rectangles[i].right && z >= rectangles[i].top && z <= rectangles[i].bottom)
      return true;
  return false;
}

NwRoadRectFilter::NwRoadRectFilter() : count(0) {
}

// Copy at most eight inclusive rectangles in the original component order.
void NwRoadRectFilter::SetRectangles(GeCellRect *input, i32 *inputCount) {
  count = *inputCount;
  if (count > 8)
    count = 8;
  for (i32 i = 0; i < count; ++i) {
    rectangles[i].left = input[i].left;
    rectangles[i].top = input[i].top;
    rectangles[i].right = input[i].right;
    rectangles[i].bottom = input[i].bottom;
  }
}

// Select a people-tagged road vehicle whose cell coordinates fall in a stored inclusive rectangle.
bool NwRoadRectFilter::Test(ScOccupant *occupant) {
  u32 flags = occupant->GetGeomFlag();
  if ((flags & (kOcGeomPerson | kOcGeomRoadVehicle)) != (kOcGeomPerson | kOcGeomRoadVehicle))
    return false;
  u32 x = 0, z = u32(uptr(occupant));
  occupant->GetLocationInCellCoords(&x, &z);
  i32 limit = count;
  for (i32 i = 0; i < limit; ++i)
    if (x >= rectangles[i].left && x <= rectangles[i].right && z >= rectangles[i].top && z <= rectangles[i].bottom)
      return true;
  return false;
}

bool NwPeopleFilter::Test(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomPerson) == kOcGeomPerson;
}

bool NwRoadVehicleFilter::Test(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRoadVehicle) == kOcGeomRoadVehicle;
}

bool NwNonEmergencyRoadFilter::Test(ScOccupant *occupant) {
  if ((occupant->GetGeomFlag() & kOcGeomRoadVehicle) != kOcGeomRoadVehicle)
    return false;
  NwStreetOccupant *streetOccupant = static_cast<NwStreetOccupant *>(occupant);
  NwStreetVehicle *vehicle = static_cast<NwStreetVehicle *>(streetOccupant);
  return vehicle->IsEmergencyVehicle() == 0;
}

bool NwPeopleRoadFilter::Test(ScOccupant *occupant) {
  u32 flags = occupant->GetGeomFlag();
  return (flags & kOcGeomPerson) == kOcGeomPerson || (flags & kOcGeomRoadVehicle) == kOcGeomRoadVehicle;
}

bool NwRailVehicleFilter::Test(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRailVehicle) == kOcGeomRailVehicle;
}

bool NwSubwayVehicleFilter::Test(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomSubwayVehicle) == kOcGeomSubwayVehicle;
}

bool NwBoatFilter::Test(ScOccupant *occupant) {
  return occupant->MajorType() == 6 && occupant->SubTypeInfo() == 7;
}

static u8 NwStreetApplicationInitFlags;
static void *NwStreetApplicationCache;
static ScLanguageManager *NwStreetLanguageCache;
static RsManager *NwStreetResourceCache;
static ScMessageServer *NwStreetMessageCache;

static void NwStreetApplicationExit() {
}

// Query the framework application once into the original module-local cache.
ApGameInterface *NwGetStreetApplication() {
  if (!(NwStreetApplicationInitFlags & 1)) {
    NwStreetApplicationInitFlags |= 1;
    NwGetStreetFramework()->Application()->QueryInterface(IID_ApGameInterface, &NwStreetApplicationCache);
    atexit(NwStreetApplicationExit);
  }
  return static_cast<ApGameInterface *>(NwStreetApplicationCache);
}

// Borrow the application's current city, or return null when the application was unavailable.
CtCity *NwGetStreetCity() {
  ApGameInterface *application = NwGetStreetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the application's city-window view, or return null when the application was unavailable.
SpCityWindowView *NwGetStreetCityWindow() {
  ApGameInterface *application = NwGetStreetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Borrow the active city view from the current city-window view when available.
SpViewInterface *NwGetStreetCurrentView() {
  SpCityWindowView *window = NwGetStreetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the current view's animation tick manager when a city view exists.
SpTickManager *NwGetStreetTickManager() {
  SpViewInterface *view = NwGetStreetCurrentView();
  return view ? view->GetAnimTickManager() : nullptr;
}

// Cache the framework resource manager before releasing the temporary service reference.
RsManager *NwGetStreetResourceManager() {
  if (!NwStreetResourceCache) {
    void *object = nullptr;
    NwGetStreetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    NwStreetResourceCache = static_cast<RsManager *>(object);
    if (object)
      static_cast<RsManager *>(object)->Release();
  }
  return NwStreetResourceCache;
}

// Borrow the application's preferences, or return null when the application was unavailable.
ApPreferences *NwGetStreetPreferences() {
  ApGameInterface *application = NwGetStreetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Borrow the application's system performance information when the application is available.
ApPerformanceInfo *NwGetStreetPerformanceInfo() {
  ApGameInterface *application = NwGetStreetApplication();
  return application ? application->GetSystemPerformanceInfo() : nullptr;
}

// Cache the framework language manager before releasing the temporary service reference.
ScLanguageManager *NwGetStreetLanguageManager() {
  if (!NwStreetLanguageCache) {
    void *object = nullptr;
    NwGetStreetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    NwStreetLanguageCache = static_cast<ScLanguageManager *>(object);
    if (object)
      static_cast<ScLanguageManager *>(object)->Release();
  }
  return NwStreetLanguageCache;
}

// Cache the framework message server before releasing the temporary service reference.
ScMessageServer *NwGetStreetMessageServer() {
  if (!NwStreetMessageCache) {
    void *object = nullptr;
    NwGetStreetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    NwStreetMessageCache = static_cast<ScMessageServer *>(object);
    if (object)
      static_cast<ScMessageServer *>(object)->Release();
  }
  return NwStreetMessageCache;
}

// Borrow the application's BoxX service, or return null when the application was unavailable.
ApBoxX *NwGetStreetBoxX() {
  ApGameInterface *application = NwGetStreetApplication();
  return application ? application->GetBoxX() : nullptr;
}

// Borrow the application's agent-type tree, or return null when the application was unavailable.
CtAgentTree *NwGetStreetAgentTypeTree() {
  ApGameInterface *application = NwGetStreetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Register each original class and its factory after the common director has constructed its own collections.
NwStreetDirector::NwStreetDirector() {
  AddClass(kNwStreetLayerClassId, NwCreateStreetLayer, 0);
  AddClass(kNwPeopleProducerAttribClassId, NwCreateStreetClass21FD6EEF, 0);
  AddClass(kNwVehicleProducerAttribFactoryClassId, NwCreateStreetClass025C6D8B, 0);
  AddClass(kNwPeoplePathAttribFactoryClassId, NwCreatePeoplePathAttribFactory, 0);
  AddClass(kNwVehiclePathAttribFactoryClassId, NwCreateVehiclePathAttribFactory, 0);
  AddClass(kNwStreetScriptFactoryClassId, NwCreateStreetScriptFactory, 0);
  AddClass(kNwAnimSpriteAttribFactoryClassId, NwCreateAnimSpriteAttribFactory, 0);
  AddClass(kNwPersonAttribFactoryClassId, NwCreatePersonAttribFactory, 0);
  AddClass(kNwVehicleAttribFactoryClassId, NwCreateVehicleAttribFactory, 0);
  AddClass(kNwTrafficBlockFactoryClassId, NwCreateTrafficFactory, 0);
  AddClass(kNwTrafficAttribFactoryClassId, NwCreateTrafficAttribFactory, 0);
  AddClass(kNwTrainProducerAttribFactoryClassId, NwCreateTrainProducerAttribFactory, 0);
  AddClass(kNwAirTrafficAttribFactoryClassId, NwCreateAirTrafficAttribFactory, 0);
}

// Allocate the street-simulation layer with its original primary interface.
ScUnknown *NwCreateStreetLayer() {
  return static_cast<CtStartSimLayer *>(new NwStreetLayer);
}

// Allocate the people-producer attributes and return their original secondary interface view.
ScUnknown *NwCreateStreetClass21FD6EEF() {
  return static_cast<NwPeopleProducerAttribInterface *>(new NwPeopleProducerAttrib);
}

// Allocate the vehicle-producer attribute factory registered under the street module's class ID.
ScUnknown *NwCreateStreetClass025C6D8B() {
  return static_cast<RsFactoryInterface *>(new NwVehicleProducerAttribFactory);
}

// Allocate the people-path resource factory registered under its original class ID.
ScUnknown *NwCreatePeoplePathAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwPeoplePathAttribFactory);
}

// Allocate the vehicle-path resource factory registered under its original class ID.
ScUnknown *NwCreateVehiclePathAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwVehiclePathAttribFactory);
}

// Allocate the traffic-block attribute factory registered under its original class ID.
ScUnknown *NwCreateTrafficAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwTrafficAttribFactory);
}

// Allocate the person-attribute factory registered under its original class ID.
ScUnknown *NwCreatePersonAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwPersonAttribFactory);
}

// Allocate the vehicle-attribute factory registered under its original class ID.
ScUnknown *NwCreateVehicleAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwVehicleAttribFactory);
}

// Allocate the street-script factory registered under its original class ID.
ScUnknown *NwCreateStreetScriptFactory() {
  return static_cast<RsFactoryInterface *>(new NwStreetScriptFactory);
}

// Allocate the animation-attribute factory registered under its original class ID.
ScUnknown *NwCreateAnimSpriteAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwAnimSpriteAttribFactory);
}

// Allocate the train-producer attribute factory registered under its original class ID.
ScUnknown *NwCreateTrainProducerAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwTrainProducerAttribFactory);
}

// Allocate the air-traffic attribute factory registered under its original class ID.
ScUnknown *NwCreateAirTrafficAttribFactory() {
  return static_cast<RsFactoryInterface *>(new NwAirTrafficAttribFactory);
}

NwStreetDirector::~NwStreetDirector() {
}

u32 NwStreetDirector::GetDirectorID() const {
  return kNwStreetDirectorId;
}

class NwStreetRegistryRef {
public:
  // Acquire the original registry service through its genuine escaped output slot.
  NwStreetRegistryRef() : object(nullptr) {
    NwGetStreetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
  }

  // Release the acquired service after the director's registration callback returns.
  ~NwStreetRegistryRef() {
    if (object)
      static_cast<ScRegistry *>(object)->Release();
  }

  void *object;
};

bool NwStreetDirector::OnStart(ScCom *) {
  NwStreetRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.object));
}

alignas(NwStreetDirector) static u8 NwStreetDirectorStorage[sizeof(NwStreetDirector)];
static u8 NwStreetDirectorInitialized;

static void NwDestroyStreetDirector() {
  reinterpret_cast<NwStreetDirector *>(NwStreetDirectorStorage)->~NwStreetDirector();
}

// Publish the initialization bit before constructing the director and registering its exit-time destructor.
ScDirector *NwGetStreetDirector() {
  if (!(NwStreetDirectorInitialized & 1)) {
    NwStreetDirectorInitialized |= 1;
    new (static_cast<void *>(NwStreetDirectorStorage)) NwStreetDirector;
    atexit(NwDestroyStreetDirector);
  }
  return static_cast<ScDirector *>(reinterpret_cast<NwStreetDirector *>(NwStreetDirectorStorage));
}

#if !defined(SC3_MONOLITHIC)
// Borrow the single street-simulation director through the DLL's original export.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return NwGetStreetDirector();
}
#endif

// Borrow the framework published through the module director's live virtual operation.
ScFramework *NwGetStreetFramework() {
  return NwGetStreetDirector()->FrameWork();
}

u32 NwAddStreetModuleRef() {
  return NwGetStreetDirector()->AddRef();
}

u32 NwRemoveStreetModuleRef() {
  return NwGetStreetDirector()->RemoveRef();
}

extern ScRandom NwPeopleGraphRandom;
extern ScRandom NwPeopleNodeRandom;

static ScRandom NwPersonRandom(kScRandomSeedFromClock);
static constexpr u32 NwPersonCornerX[4] = {0, 1, 1, 0};
static constexpr u32 NwPersonCornerZ[4] = {0, 0, 1, 1};
static constexpr i8 NwPersonStepX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
static constexpr i8 NwPersonStepZ[8] = {0, -1, -1, -1, 0, 1, 1, 1};
static constexpr u16 NwPersonOrientationTable[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 2, 3, 4, 5, 6, 7, 0, 1,
    4, 5, 6, 7, 0, 1, 2, 3, 6, 7, 0, 1, 2, 3, 4, 5};

// Original waiting state contains a borrowed path and its elapsed time.
struct NwPersonScriptState {
  NwStreetScript *script;
  i16 instructionIndex;
  i32 repeatCount, completedCount;
};

struct NwPersonPointState {
  f32 speed;
  u16 action;
  u32 animationElapsed, movementElapsed;
  i32 startX, startAltitude, startZ, endX, endAltitude, endZ;
  f32 directionX, directionAltitude, directionZ;
  i32 distance;
};

struct NwPersonPathState {
  NwPeoplePath *path;
  f32 speed;
  u16 action;
};

struct NwPersonWaitState {
  NwPeoplePath *path;
  u32 elapsed;
};

#pragma pack(push, 1)
struct NwPersonSpiralState {
  ScVec3F origin, velocity, position;
  f32 angularSpeed, angle, radius;
  i32 phase, ground;
  u32 animationElapsed;
};
#pragma pack(pop)
static_assert(sizeof(NwPersonSpiralState) == 60);

#pragma pack(push, 4)
struct NwPersonStageState {
  u16 action, unused;
  f32 speed;
  i32 count, completed, phase;
  NwPeopleGraph *graph;
  NwPeopleGraphNode *node;
  NwPeoplePath *path;
  u8 orientation;
};
#pragma pack(pop)

NwPerson::ScriptFn NwPerson::scriptFunctions[3];
RsManager *NwPerson::resourceManager;
u8 NwPerson::staticInitialized;
i32 NwPerson::birthZoomLevel = 3;
i16 NwPerson::zoomOffsets[5];
i32 NwPerson::cityXMicroUnits, NwPerson::cityZMicroUnits;
ScOccupantManager *NwPerson::occupantManager;
OcAnimManager *NwPerson::animationManager;
CtStartSimLayer *NwPerson::streetLayer;
ScDirtBag *NwPerson::dirtBag;
#include "geometry/ge_types.h"

NwStreetOccupant::NwStreetOccupant() : cityFlags(0), references(0), x(0), altitude(0), z(0) {
}

NwStreetOccupant::~NwStreetOccupant() {
}

f32 NwStreetOccupant::GetAltWorldUnits() const {
  return 0;
}

void NwStreetOccupant::SetAltAnimCells(u16 value) {
  altitude = value;
}

void NwStreetOccupant::SetLocationInAnimCoords(u32 pointX, u32 pointZ) {
  x = static_cast<i32>(pointX);
  z = static_cast<i32>(pointZ);
}

u8 NwStreetOccupant::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 NwStreetOccupant::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

bool NwStreetOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwStreetOccupant::AddRef() {
  return ++references;
}

u32 NwStreetOccupant::Release() {
  return ScReleaseImpl(this, &references);
}

bool NwStreetOccupant::DoMessage(ScMessage *) {
  return false;
}

bool NwStreetOccupant::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool NwStreetOccupant::Init(GeAttrib *) {
  return false;
}

bool NwStreetOccupant::Init(RsKey *) {
  return false;
}

bool NwStreetOccupant::Init(CtCity *, OcPersistInfo *) {
  return false;
}

bool NwStreetOccupant::Save(CtCity *, OcPersistInfo *) {
  return false;
}

bool NwStreetOccupant::IsInCity() const {
  return cityFlags != 0;
}

bool NwStreetOccupant::TestIsInCityFlag(u32 mask) const {
  return (cityFlags & u8(mask)) != 0;
}

u32 NwStreetOccupant::GetIsInCityFlag() const {
  return cityFlags;
}

void NwStreetOccupant::SetIsInCityFlag(u32 mask) {
  cityFlags = u8(mask);
}

// Clear requested city flags and shut down the occupant when the last set bit is removed.
void NwStreetOccupant::ClearIsInCityFlag(u32 mask) {
  if (cityFlags) {
    cityFlags = u8(cityFlags & ~u8(mask));
    if (!cityFlags)
      Shutdown();
  }
}

u32 NwStreetOccupant::GetOccLayerFlag() {
  return 1;
}

void NwStreetOccupant::SetOccLayerFlag(u32) {
}

u8 NwStreetOccupant::MajorType() const {
  return 6;
}

u32 NwStreetOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 NwStreetOccupant::AnimType() const {
  return 2;
}

void NwStreetOccupant::AnimStart() {
}

void NwStreetOccupant::AnimStop() {
}

bool NwStreetOccupant::CanSimRemove() const {
  return true;
}

void NwStreetOccupant::SetSimRemove(bool) {
}

bool NwStreetOccupant::CanUserRemove() const {
  return true;
}

void NwStreetOccupant::SetUserRemove(bool) {
}

bool NwStreetOccupant::GetName(ScStringView *) {
  return false;
}

// Return no occupant attribute object from the abstract base.
GeAttrib *NwStreetOccupant::GetAttribData() {
  return nullptr;
}

bool NwStreetOccupant::GetSubSystemData(u32, void **) {
  return false;
}

u32 NwStreetOccupant::TypeId() const {
  return 0;
}

// Return no city-agent type from the abstract base.
CtAgentType *NwStreetOccupant::CityAgentType() {
  return nullptr;
}

bool NwStreetOccupant::CreateQueryDialog(UiQueryWindow **) {
  return false;
}

u32 NwStreetOccupant::GetSoundObjectId(i32) {
  return 0;
}

// Borrow the default sprite definition only when the requested layer is the occupant's default layer.
SpDefinition *NwStreetOccupant::GetSpriteAttrib(u32 layer) {
  if (layer == GetDefaultLayerFlag())
    return GetSpriteAttrib();
  return nullptr;
}

// Borrow the default sprite instance only when the requested layer is the occupant's default layer.
SpSprite *NwStreetOccupant::GetSpriteInst(u32 layer) {
  if (layer == GetDefaultLayerFlag())
    return GetSpriteInst();
  return nullptr;
}

u32 NwStreetOccupant::GetDefaultLayerFlag() const {
  return 1;
}

u8 NwStreetOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

void NwStreetOccupant::GetOrientation(u16 *, u16 *, u16 *) const {
}

bool NwStreetOccupant::GetRotationFlag(u32 *) const {
  return false;
}

// Copy the stored coordinates to the city-coordinate output in the original component order.
void NwStreetOccupant::GetLocation(CtCoord *out) const {
  i32 oldAltitude = altitude, oldX = x;
  out->z = z;
  out->x = oldX;
  out->altitude = oldAltitude;
}

void NwStreetOccupant::GetLocationInCellCoords(u32 *outX, u32 *outZ) const {
  *outX = u32(x >> kCtMicroCoordinateShift);
  *outZ = u32(z >> kCtMicroCoordinateShift);
}

u8 NwStreetOccupant::GetAlt() const {
  return u8(altitude >> kCtMicroCoordinateShift);
}

void NwStreetOccupant::GetLocationInAnimCellCoords(u32 *outX, u32 *outZ) const {
  *outX = u32(x);
  *outZ = u32(z);
}

u16 NwStreetOccupant::GetAltAnimCells() const {
  return u16(altitude);
}

// Copy the minimum coordinates and form the inclusive maximum from the three virtual animation dimensions.
void NwStreetOccupant::GetBounds(GeBounds *out) const {
  i32 oldAltitude = altitude, oldX = x;
  out->minimum.z = z;
  out->minimum.x = oldX;
  out->minimum.altitude = oldAltitude;
  u32 sizeY = SizeInAnimCellsY();
  u32 maxAltitude = sizeY + u32(altitude) - 1;
  u32 sizeZ = SizeInAnimCellsZ();
  u32 maxZ = sizeZ + u32(z) - 1;
  u32 sizeX = SizeInAnimCellsX(), currentX = u32(x);
  out->maximum.z = i32(maxZ);
  out->maximum.altitude = i32(maxAltitude);
  out->maximum.x = i32(sizeX + currentX - 1);
}

void NwStreetOccupant::GetBoundingRectInCellCoords(GeCellRect *) const {
}

u8 NwStreetOccupant::SizeInCellsX() const {
  return 0;
}

u8 NwStreetOccupant::SizeInCellsZ() const {
  return 0;
}

u8 NwStreetOccupant::SizeInCellsY() const {
  return 0;
}

u32 NwStreetOccupant::SizeInAnimCellsZ() const {
  return 1;
}

u32 NwStreetOccupant::SizeInAnimCellsX() const {
  return 1;
}

u32 NwStreetOccupant::SizeInAnimCellsY() const {
  return 1;
}

// Replace the three stored coordinates in their original x, z and altitude write order.
void NwStreetOccupant::SetLocation(const CtCoord &position) {
  x = position.x;
  z = position.z;
  altitude = position.altitude;
}

void NwStreetOccupant::SetLocationInCellCoords(u32 cellX, u32 cellZ) {
  x = i32(cellX << kCtMicroCoordinateShift);
  z = i32(cellZ << kCtMicroCoordinateShift);
}

void NwStreetOccupant::SetAlt(u8 value) {
  altitude = i32(u32(value) << kCtMicroCoordinateShift);
}

bool NwStreetOccupant::GetMarkBit() const {
  return false;
}

void NwStreetOccupant::SetMarkBit(bool) {
}

void NwStreetOccupant::SetSubTypeInfo(u8) {
}

void NwStreetOccupant::SetOrientation(u16, u16, u16) {
}

u8 NwStreetOccupant::SetRotationFlag(u32) {
  return 0;
}

u32 NwStreetOccupant::GetMemUse() const {
  return 24;
}

bool NwStreetOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool NwStreetOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool NwStreetOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

void NwStreetOccupant::GetBoundingRectInAnimCellCoords(GeCellRect *) const {
}

NwPerson::NwPerson()
    : byte32(0), personState(0), byte34(0), byte35(0), effect(0), producer(nullptr), stateStack{}, stateStackTop(0),
      actionStackTop(-1), attributes(nullptr), spriteRepository(nullptr), actionDescriptor(nullptr) {
  x = 0;
  altitude = 0;
  z = 0;
}

NwPerson::~NwPerson() {
  if (byte32)
    Shutdown();
}

void NwPerson::PushStateStack(i32 size) {
  stateStackTop = static_cast<i32>(u32(stateStackTop) + u32(size));
}

void NwPerson::PopStateStack(i32 size) {
  stateStackTop = static_cast<i32>(u32(stateStackTop) - u32(size));
}

void NwPerson::PushToActionStack(UpdateFn update) {
  actionStack[++actionStackTop] = update;
}

void NwPerson::PopActionStack() {
  --actionStackTop;
}

// Cancel all stacked person actions, passing each result to the next cancellation.
void NwPerson::WipeActionStack() {
  i32 status = 0;
  while (actionStackTop >= 0) {
    UpdateFn update = actionStack[actionStackTop--];
    status = (this->*update)(0, status, true);
  }
}

// Start a person's spiral motion around a moving origin and select a random sprite action.
i32 NwPerson::SpiralMotion(const ScVec3F &origin, const ScVec3F &velocity) {
  personState |= 3;
  WipeActionStack();
  i32 offset = stateStackTop;
  stateStackTop += 60;
  auto *state = new (stateStack + offset) NwPersonSpiralState;
  state->origin = origin;
  state->velocity = velocity;
  f64 dx = f64(x) - origin.x, dz = f64(z) - origin.z;
  f32 angleX = static_cast<f32>(dx), angleZ = static_cast<f32>(dz);
  state->radius = static_cast<f32>(std::sqrt(dx * dx + dz * dz));
  state->angle = static_cast<f32>(std::atan2(f64(angleZ), f64(angleX)));
  state->phase = 0;
  state->angularSpeed = static_cast<f32>(f64(0.001256f) / state->radius);
  actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetARandomActionDescriptor();
  byte35 = static_cast<u8>(NwPersonRandom.Bounded(actionDescriptor->frameCount));
  state->animationElapsed = 0;
  PushToActionStack(&NwPerson::SpiralMotionUpdateFn);
  return 2;
}

// Advance the person's spiral and ballistic descent, moving the animated occupant until it reaches terrain.
i32 NwPerson::SpiralMotionUpdateFn(u32 elapsed, i32, bool cancel) {
  auto *state = reinterpret_cast<NwPersonSpiralState *>(stateStack + stateStackTop - 60);
  if (cancel) {
    stateStackTop -= 60;
    return 0;
  }
  state->animationElapsed += elapsed;
  if (state->animationElapsed > kNwPersonFrameInterval) {
    actionDescriptor = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->GetARandomActionDescriptor();
    byte35 = static_cast<u8>(NwPersonRandom.Bounded(actionDescriptor->frameCount));
    state->animationElapsed = 0;
  }
  CtCoord position;
  if (state->phase == 0) {
    f64 radius = f64(elapsed) * f64(0.000015f) + state->radius;
    f64 angularSpeed = f64(state->radius) / radius * state->angularSpeed;
    state->radius = static_cast<f32>(radius);
    state->angularSpeed = static_cast<f32>(angularSpeed);
    state->angle = static_cast<f32>(angularSpeed * elapsed + state->angle);
    state->origin.x = static_cast<f32>(f64(elapsed) * state->velocity.x + state->origin.x);
    state->origin.z = static_cast<f32>(f64(elapsed) * state->velocity.z + state->origin.z);
    f64 nextAltitude = f64(elapsed) * f64(0.001f) + state->origin.y;
    state->origin.y = static_cast<f32>(nextAltitude);
    position.altitude = static_cast<i32>(static_cast<i64>(nextAltitude));
    position.x = static_cast<i32>(static_cast<i64>(std::cos(f64(state->angle)) * state->radius + state->origin.x));
    position.z = static_cast<i32>(static_cast<i64>(std::sin(f64(state->angle)) * state->radius + state->origin.z));
    if (!InCityBoundsMicro(position.x, position.z)) {
      personState = static_cast<u8>(personState - 3);
      stateStackTop -= 60;
      return 0;
    }
    if (state->radius > 250.0f) {
      state->phase = 1;
      state->position = {static_cast<f32>(x), static_cast<f32>(altitude), static_cast<f32>(z)};
      f64 tangent = f64(state->radius) * state->angularSpeed;
      state->velocity.x = static_cast<f32>(std::cos(f64(state->angle)) * tangent);
      state->velocity.z = static_cast<f32>(std::sin(f64(state->angle)) * tangent);
      state->velocity.y = 0.001f;
    }
  } else if (state->phase == 1) {
    state->velocity.y = static_cast<f32>(f64(state->velocity.y) - f64(elapsed) * f64(7.68e-10f));
    f32 nextX = static_cast<f32>(f64(elapsed) * state->velocity.x + state->position.x);
    state->position.x = nextX;
    state->position.y = static_cast<f32>(f64(elapsed) * state->velocity.y + state->position.y);
    state->position.z = static_cast<f32>(f64(elapsed) * state->velocity.z + state->position.z);
    position.x = static_cast<i32>(static_cast<i64>(nextX));
    position.altitude = static_cast<i32>(static_cast<i64>(state->position.y));
    position.z = static_cast<i32>(static_cast<i64>(state->position.z));
    if (!InCityBoundsMicro(position.x, position.z)) {
      personState = static_cast<u8>(personState - 3);
      stateStackTop -= 60;
      return 0;
    }
    if (InsideStaticOccupant(position.x, position.altitude, position.z)) {
      state->phase = 2;
      u8 ground = 0;
      dirtBag->GetVertexAltitude(
          u32(position.x >> kCtMicroCoordinateShift), u32(position.z >> kCtMicroCoordinateShift), &ground);
      state->ground = i32(u32(ground) << kCtMicroCoordinateShift);
    }
  } else if (state->phase == 2) {
    f64 nextVerticalSpeed = f64(state->velocity.y) - f64(elapsed) * f64(7.68e-10f);
    state->velocity.y = static_cast<f32>(nextVerticalSpeed);
    f64 nextAltitude = f64(elapsed) * nextVerticalSpeed + state->position.y;
    state->position.y = static_cast<f32>(nextAltitude);
    position = {x, z, static_cast<i32>(static_cast<i64>(nextAltitude))};
    if (position.altitude <= state->ground) {
      personState = static_cast<u8>(personState - 3);
      stateStackTop -= 60;
      return 0;
    }
  } else {
    return 2;
  }
  animationManager->MoveOccupant(static_cast<ScOccupant *>(this), &position);
  return 2;
}

// Decode the five movement arguments and start the person stage sequence.
i32 NwPerson::MoveNStagesScriptFn(NwScriptInstruction *instruction) {
  u32 *args = instruction->arguments;
  return MoveNStages(static_cast<i32>(args[0]), static_cast<i32>(args[1]), static_cast<u16>(args[2]),
      static_cast<i32>(args[3]), static_cast<i32>(args[4]));
}

i32 NwPerson::PlayAnimationScriptFn(NwScriptInstruction *instruction) {
  u32 *args = instruction->arguments;
  return PlayAnimation(static_cast<u16>(args[0]), static_cast<i32>(args[1]), static_cast<i32>(args[2]));
}

// Decode a resource key and repeat parameters before executing the nested script.
i32 NwPerson::ExecuteScriptScriptFn(NwScriptInstruction *instruction) {
  u32 *args = instruction->arguments;
  RsKey key(args[0], args[1], args[2]);
  return ExecuteScript(key, static_cast<i32>(args[3]), static_cast<i32>(args[4]));
}

// Initialize the person script dispatch table and borrow the application's resource manager.
u8 NwPerson::StaticInit() {
  if (staticInitialized)
    return 0;
  scriptFunctions[0] = &NwPerson::MoveNStagesScriptFn;
  scriptFunctions[1] = &NwPerson::PlayAnimationScriptFn;
  scriptFunctions[2] = &NwPerson::ExecuteScriptScriptFn;
  ApGameInterface *application;
  NwGetStreetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&application));
  resourceManager = application->GetResourceManager();
  application->Release();
  staticInitialized = 1;
  return 1;
}

// Return the person's borrowed sprite-attribute interface without acquiring a reference.
// The shared occupant declaration currently spells this interface as its concrete producer class.
SpDefinition *NwPerson::GetSpriteAttrib() {
  return reinterpret_cast<SpDefinition *>(spriteRepository);
}

// Retain the person attributes and borrow their sprite repository.
u8 NwPerson::Init(const RsKey &key) {
  if (byte32)
    return 0;
  RsResource *resource;
  if (!NwStreetLayer::GetPersonAttrib(key, &resource))
    return 0;
  attributes = static_cast<NwPersonAttrib *>(resource);
  SpDefView *repository = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes)->repository;
  byte32 = 1;
  spriteRepository = repository;
  return 1;
}

void NwPerson::Shutdown() {
  if (!byte32)
    return;
  byte32 = 0;
  WipeActionStack();
  attributes->Release();
  attributes = nullptr;
  producer->PersonDisappears(static_cast<NwPeopleInterface *>(this));
  producer->Release();
  producer = nullptr;
}

i32 NwPerson::WaitForPathClearance(NwPeoplePath *path) {
  NwPersonWaitState state{path, 0};
  i32 offset = stateStackTop;
  stateStackTop += static_cast<i32>(sizeof(state));
  std::memcpy(stateStack + offset, &state, sizeof(state));
  PushToActionStack(&NwPerson::WaitForPathClearanceUpdateFn);
  return 2;
}

// Retry admission until the path accepts the person, the wait expires, or the action is cancelled.
i32 NwPerson::WaitForPathClearanceUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 offset = stateStackTop - static_cast<i32>(sizeof(NwPersonWaitState));
  if (!cancel) {
    NwPeoplePath *path;
    std::memcpy(&path, stateStack + offset, sizeof(path));
    if (!path->EnterPerson(static_cast<NwPeopleInterface *>(this))) {
      u32 accumulated;
      std::memcpy(&accumulated, stateStack + offset + offsetof(NwPersonWaitState, elapsed), sizeof(accumulated));
      accumulated += elapsed;
      std::memcpy(stateStack + offset + offsetof(NwPersonWaitState, elapsed), &accumulated, sizeof(accumulated));
      if (accumulated <= kNwPersonPathClearanceTimeout)
        return 2;
      stateStackTop -= static_cast<i32>(sizeof(NwPersonWaitState));
      return 1;
    }
  }
  stateStackTop -= static_cast<i32>(sizeof(NwPersonWaitState));
  return 0;
}

// Select an animation, refresh the person, and schedule the requested repetitions.
i32 NwPerson::PlayAnimation(u16 action, i32 duration, i32 variation) {
  i32 offset = stateStackTop;
  stateStackTop += 16;
  i32 repeats = duration;
  if (variation)
    repeats = NwPersonRandom.Range(static_cast<i32>(u32(duration) - u32(variation)),
        static_cast<i32>(u32(duration) + u32(variation)));
  i32 completed = 0;
  std::memcpy(stateStack + offset + 8, &completed, sizeof(completed));
  std::memcpy(stateStack + offset + 4, &repeats, sizeof(repeats));
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  actionDescriptor = animation->GetActionDescriptor(action);
  byte35 = 0;
  animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
  u32 animationElapsed = 0;
  std::memcpy(stateStack + offset + 12, &animationElapsed, sizeof(animationElapsed));
  PushToActionStack(&NwPerson::PlayAnimationUpdateFn);
  return 2;
}

// Advance a frame after the original interval and finish after the requested repetitions.
i32 NwPerson::PlayAnimationUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 offset = stateStackTop - 16;
  if (cancel) {
    stateStackTop -= 16;
    return 0;
  }
  u32 accumulated;
  std::memcpy(&accumulated, stateStack + offset + 12, sizeof(accumulated));
  accumulated += elapsed;
  std::memcpy(stateStack + offset + 12, &accumulated, sizeof(accumulated));
  if (accumulated > kNwPersonFrameInterval) {
    ++byte35;
    if (byte35 == actionDescriptor->frameCount) {
      byte35 = 0;
      i32 completed, repeats;
      std::memcpy(&completed, stateStack + offset + 8, sizeof(completed));
      completed = static_cast<i32>(u32(completed) + 1);
      std::memcpy(stateStack + offset + 8, &completed, sizeof(completed));
      std::memcpy(&repeats, stateStack + offset + 4, sizeof(repeats));
      if (completed == repeats) {
        stateStackTop -= 16;
        return 0;
      }
      accumulated = 0;
      std::memcpy(stateStack + offset + 12, &accumulated, sizeof(accumulated));
    }
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
  }
  return 2;
}

// Retain a script and schedule its instructions for the requested number of repetitions.
i32 NwPerson::ExecuteScript(const RsKey &key, i32 count, i32 variation) {
  i32 offset = stateStackTop, size = static_cast<i32>(sizeof(NwPersonScriptState));
  stateStackTop += size;
  RsResource *resource;
  if (!NwStreetLayer::GetScript(key, &resource)) {
    stateStackTop -= size;
    return 1;
  }
  auto *script = static_cast<NwStreetScript *>(resource);
  std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, script), &script, sizeof(script));
  i32 repeats = count;
  if (variation)
    repeats = NwPersonRandom.Range(static_cast<i32>(u32(count) - u32(variation)),
        static_cast<i32>(u32(count) + u32(variation)));
  i32 completed = 0;
  i16 index = -1;
  std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, completedCount), &completed, sizeof(completed));
  std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, instructionIndex), &index, sizeof(index));
  std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, repeatCount), &repeats, sizeof(repeats));
  PushToActionStack(&NwPerson::ExecuteScriptUpdateFn);
  return 2;
}

// Run script instructions until an action waits, fails, finishes its repetitions, or is cancelled.
i32 NwPerson::ExecuteScriptUpdateFn(u32, i32 status, bool cancel) {
  i32 size = static_cast<i32>(sizeof(NwPersonScriptState)), offset = stateStackTop - size;
  if (!cancel) {
    while (status != 2 && status != 1) {
      i16 index;
      NwStreetScript *script;
      std::memcpy(&index, stateStack + offset + offsetof(NwPersonScriptState, instructionIndex), sizeof(index));
      index = static_cast<i16>(u16(index) + 1);
      std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, instructionIndex), &index, sizeof(index));
      std::memcpy(&script, stateStack + offset + offsetof(NwPersonScriptState, script), sizeof(script));
      if (index == script->instructionCount) {
        i32 completed, repeats;
        std::memcpy(&completed, stateStack + offset + offsetof(NwPersonScriptState, completedCount), sizeof(completed));
        completed = static_cast<i32>(u32(completed) + 1);
        std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, completedCount), &completed, sizeof(completed));
        std::memcpy(&repeats, stateStack + offset + offsetof(NwPersonScriptState, repeatCount), sizeof(repeats));
        if (completed >= repeats)
          break;
        index = 0;
        std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, instructionIndex), &index, sizeof(index));
      }
      NwScriptInstruction *instruction = &script->instructions[index];
      status = (this->*scriptFunctions[static_cast<i16>(instruction->opcode)])(instruction);
    }
    if (status == 2)
      return 2;
  }
  NwStreetScript *script;
  std::memcpy(&script, stateStack + offset + offsetof(NwPersonScriptState, script), sizeof(script));
  if (script) {
    script->Release();
    script = nullptr;
    std::memcpy(stateStack + offset + offsetof(NwPersonScriptState, script), &script, sizeof(script));
  }
  stateStackTop -= size;
  return !cancel && status == 1 ? 1 : 0;
}

// Start a sequence of walking stages and acquire a path to the current graph node if necessary.
i32 NwPerson::MoveNStages(i32 count, i32 variation, u16 action, i32 duration, i32 durationVariation) {
  i32 offset = stateStackTop, size = static_cast<i32>(sizeof(NwPersonStageState));
  stateStackTop += size;
  NwPersonStageState state{};
  state.count = variation ? NwPersonRandom.Range(static_cast<i32>(u32(count) - u32(variation)),
      static_cast<i32>(u32(count) + u32(variation))) : count;
  state.completed = 0;
  state.action = action;
  i32 randomizedDuration = durationVariation ? NwPersonRandom.Range(
      static_cast<i32>(u32(duration) - u32(durationVariation)),
      static_cast<i32>(u32(duration) + u32(durationVariation))) : duration;
  state.speed = static_cast<f32>(f64(randomizedDuration) * f64(0.00000000222222f));
  NwPeopleGraph *graph;
  NwPeopleGraphNode *node;
  if (!GetPathGraphAndNodeForMyPosition(&graph, &node)) {
    stateStackTop -= size;
    return 1;
  }
  state.graph = graph;
  state.node = node;
  PushToActionStack(&NwPerson::MoveNStagesUpdateFn);
  i32 targetX = static_cast<i32>((u32(x) & kCtMicroCellMask) + u32(state.node->coordinate0));
  i32 targetZ = static_cast<i32>((u32(z) & kCtMicroCellMask) + u32(state.node->coordinate2));
  if (x == targetX && z == targetZ) {
    state.path = nullptr;
    state.phase = 1;
    state.orientation = 5;
    std::memcpy(stateStack + offset, &state, sizeof(state));
  } else {
    state.path = new NwPeopleStraightPath(x, altitude, z, targetX, altitude, targetZ);
    state.phase = 0;
    state.orientation = state.path->GetEndOrientation();
    std::memcpy(stateStack + offset, &state, sizeof(state));
    WaitForPathClearance(state.path);
  }
  return 2;
}

// Advance or cancel a walking-stage sequence, managing its current path and graph transition.
i32 NwPerson::MoveNStagesUpdateFn(u32, i32 status, bool cancel) {
  i32 size = static_cast<i32>(sizeof(NwPersonStageState)), offset = stateStackTop - size;
  NwPersonStageState state;
  std::memcpy(&state, stateStack + offset, sizeof(state));
  if (cancel) {
    if (state.path) {
      if (state.phase == 1)
        state.path->PersonLeftPath(static_cast<NwPeopleInterface *>(this));
      delete state.path;
    }
    stateStackTop -= size;
    return 0;
  }
  if (state.phase != 1) {
    if (status == 0) {
      state.phase = 1;
      std::memcpy(stateStack + offset, &state, sizeof(state));
      MoveAlongPath(state.action, state.speed, state.path);
      return 2;
    }
    delete state.path;
    stateStackTop -= size;
    return 1;
  }
  state.phase = 0;
  std::memcpy(stateStack + offset, &state, sizeof(state));
  if (state.path) {
    state.path->PersonLeftPath(static_cast<NwPeopleInterface *>(this));
    delete state.path;
    if (++state.completed >= state.count) {
      stateStackTop -= size;
      return 0;
    }
  }
  NwPeopleGraphEdge *edge = SelectNextEdge(state.node, state.orientation);
  if (!edge) {
    stateStackTop -= size;
    return 1;
  }
  state.orientation = edge->endOrientation;
  if (!edge->target) {
    NwPeopleGraph *oldGraph = state.graph;
    NwPeopleGraphNode *oldNode = state.node;
    NwPeopleGraph *graph = state.graph;
    NwPeopleGraphNode *node = state.node;
    state.path = ChooseNextPathGraph(state.orientation, &graph, &node);
    state.graph = graph;
    state.node = node;
    if (state.path) {
      std::memcpy(stateStack + offset, &state, sizeof(state));
      WaitForPathClearance(state.path);
      return 2;
    }
    if (static_cast<NwStreetLayer *>(streetLayer)
            ->CellIsInBorderOfPeopleSimRect(u32(x >> kCtMicroCoordinateShift), u32(z >> kCtMicroCoordinateShift))) {
      stateStackTop -= size;
      return 1;
    }
    state.graph = oldGraph;
    state.node = oldNode;
    edge = state.node->SelectAnInternalEdge();
    if (!edge) {
      stateStackTop -= size;
      return 1;
    }
  }
  state.path = BuildPath(u32(x >> kCtMicroCoordinateShift), u32(z >> kCtMicroCoordinateShift), edge);
  if (!state.path) {
    stateStackTop -= size;
    return 1;
  }
  state.node = edge->target;
  std::memcpy(stateStack + offset, &state, sizeof(state));
  WaitForPathClearance(state.path);
  return 2;
}

// Rewind the people path, place the person at its first point, and schedule movement to the next point.
i32 NwPerson::MoveAlongPath(u16 action, f32 speed, NwPeoplePath *path) {
  i32 offset = stateStackTop, size = static_cast<i32>(sizeof(NwPersonPathState));
  stateStackTop += size;
  std::memcpy(stateStack + offset + offsetof(NwPersonPathState, path), &path, sizeof(path));
  std::memcpy(stateStack + offset + offsetof(NwPersonPathState, action), &action, sizeof(action));
  path->Rewind();
  std::memcpy(stateStack + offset + offsetof(NwPersonPathState, speed), &speed, sizeof(speed));
  std::memcpy(&path, stateStack + offset + offsetof(NwPersonPathState, path), sizeof(path));
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  path->GetNextPoint(&position.x, &position.altitude, &position.z);
  animationManager->MoveOccupant(static_cast<ScOccupant *>(this), &position);
  std::memcpy(&path, stateStack + offset + offsetof(NwPersonPathState, path), sizeof(path));
  i32 nextX = 0, nextAltitude = 0, nextZ = 0;
  path->GetNextPoint(&nextX, &nextAltitude, &nextZ);
  PushToActionStack(&NwPerson::MoveAlongPathUpdateFn);
  std::memcpy(&action, stateStack + offset + offsetof(NwPersonPathState, action), sizeof(action));
  std::memcpy(&speed, stateStack + offset + offsetof(NwPersonPathState, speed), sizeof(speed));
  MoveToPoint(action, speed, nextX, nextAltitude, nextZ);
  return 2;
}

// Continue along the people path until it has no more points or the action is cancelled.
i32 NwPerson::MoveAlongPathUpdateFn(u32, i32, bool cancel) {
  i32 size = static_cast<i32>(sizeof(NwPersonPathState)), offset = stateStackTop - size;
  if (!cancel) {
    NwPeoplePath *path;
    std::memcpy(&path, stateStack + offset + offsetof(NwPersonPathState, path), sizeof(path));
    i32 pointX, pointAltitude, pointZ;
    if (path->GetNextPoint(&pointX, &pointAltitude, &pointZ)) {
      u16 action;
      f32 speed;
      std::memcpy(&action, stateStack + offset + offsetof(NwPersonPathState, action), sizeof(action));
      std::memcpy(&speed, stateStack + offset + offsetof(NwPersonPathState, speed), sizeof(speed));
      MoveToPoint(action, speed, pointX, pointAltitude, pointZ);
      return 2;
    }
  }
  stateStackTop -= size;
  return 0;
}

// Select the person's walking animation and schedule movement to the supplied coordinates.
i32 NwPerson::MoveToPoint(u16 action, f32 speed, i32 pointX, i32 pointAltitude, i32 pointZ) {
  i32 offset = stateStackTop;
  stateStackTop += static_cast<i32>(sizeof(NwPersonPointState));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, speed), &speed, sizeof(speed));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, action), &action, sizeof(action));
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  NwSpriteAction *selected = animation->GetActionDescriptor(action);
  if (selected != actionDescriptor) {
    actionDescriptor = selected;
    byte35 = 0;
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
  }
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, startX), &x, sizeof(x));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, startAltitude), &altitude, sizeof(altitude));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, startZ), &z, sizeof(z));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, endX), &pointX, sizeof(pointX));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, endAltitude), &pointAltitude, sizeof(pointAltitude));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, endZ), &pointZ, sizeof(pointZ));
  i32 dx = static_cast<i32>(u32(pointX) - u32(x));
  i32 dy = static_cast<i32>(u32(pointAltitude) - u32(altitude));
  i32 dz = static_cast<i32>(u32(pointZ) - u32(z));
  f64 length = std::sqrt(f64(dx) * dx + f64(dy) * dy + f64(dz) * dz);
  i32 distance = static_cast<i32>(u32(static_cast<i64>(length)));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, distance), &distance, sizeof(distance));
  f32 directionX = distance ? f32(f32(dx) / f64(distance)) : 0.0f;
  f32 directionAltitude = distance ? f32(f32(dy) / f64(distance)) : 0.0f;
  f64 fullDirectionZ = distance ? f64(f32(dz)) / f64(distance) : 0.0;
  f32 directionZ = f32(fullDirectionZ);
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, directionX), &directionX, sizeof(directionX));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, directionAltitude), &directionAltitude,
      sizeof(directionAltitude));
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, directionZ), &directionZ, sizeof(directionZ));
  byte34 = distance ? static_cast<u8>(static_cast<i32>(
      (std::atan2(fullDirectionZ, directionX) + 3.141592654) / 0.7853981635 + 0.5)) : 0;
  if (byte34 == 8)
    byte34 = 0;
  u32 movementElapsed = 0;
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, movementElapsed), &movementElapsed,
      sizeof(movementElapsed));
  byte35 = 0;
  animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
  PushToActionStack(&NwPerson::MoveToPointUpdateFn);
  return 2;
}

// Advance walking animation and interpolate the person along the scheduled segment.
i32 NwPerson::MoveToPointUpdateFn(u32 elapsed, i32, bool cancel) {
  i32 size = static_cast<i32>(sizeof(NwPersonPointState)), offset = stateStackTop - size;
  if (cancel) {
    stateStackTop -= size;
    return 0;
  }
  u32 animationElapsed;
  std::memcpy(&animationElapsed, stateStack + offset + offsetof(NwPersonPointState, animationElapsed),
      sizeof(animationElapsed));
  animationElapsed += elapsed;
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, animationElapsed), &animationElapsed,
      sizeof(animationElapsed));
  if (animationElapsed > kNwPersonFrameInterval) {
    animationElapsed = 0;
    std::memcpy(stateStack + offset + offsetof(NwPersonPointState, animationElapsed), &animationElapsed,
        sizeof(animationElapsed));
    ++byte35;
    if (byte35 == actionDescriptor->frameCount)
      byte35 = 0;
    animationManager->RefreshOccupant(static_cast<ScOccupant *>(this), 2, nullptr);
  }
  u32 movementElapsed;
  f32 speed;
  i32 distance;
  std::memcpy(&movementElapsed, stateStack + offset + offsetof(NwPersonPointState, movementElapsed),
      sizeof(movementElapsed));
  movementElapsed += elapsed;
  std::memcpy(stateStack + offset + offsetof(NwPersonPointState, movementElapsed), &movementElapsed,
      sizeof(movementElapsed));
  std::memcpy(&speed, stateStack + offset + offsetof(NwPersonPointState, speed), sizeof(speed));
  std::memcpy(&distance, stateStack + offset + offsetof(NwPersonPointState, distance), sizeof(distance));
  i32 traveled = static_cast<i32>(u32(static_cast<i64>(f64(movementElapsed) * speed)));
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  if (traveled >= distance) {
    std::memcpy(&position.x, stateStack + offset + offsetof(NwPersonPointState, endX), sizeof(position.x));
    std::memcpy(&position.altitude, stateStack + offset + offsetof(NwPersonPointState, endAltitude),
        sizeof(position.altitude));
    std::memcpy(&position.z, stateStack + offset + offsetof(NwPersonPointState, endZ), sizeof(position.z));
    animationManager->MoveOccupant(static_cast<ScOccupant *>(this), &position);
    stateStackTop -= size;
    return 0;
  }
  i32 startX, startAltitude, startZ;
  f32 dx, dy, dz;
  std::memcpy(&startX, stateStack + offset + offsetof(NwPersonPointState, startX), sizeof(startX));
  std::memcpy(&startAltitude, stateStack + offset + offsetof(NwPersonPointState, startAltitude), sizeof(startAltitude));
  std::memcpy(&startZ, stateStack + offset + offsetof(NwPersonPointState, startZ), sizeof(startZ));
  std::memcpy(&dx, stateStack + offset + offsetof(NwPersonPointState, directionX), sizeof(dx));
  std::memcpy(&dy, stateStack + offset + offsetof(NwPersonPointState, directionAltitude), sizeof(dy));
  std::memcpy(&dz, stateStack + offset + offsetof(NwPersonPointState, directionZ), sizeof(dz));
  position.x = static_cast<i32>(u32(startX) + u32(static_cast<i32>(f64(traveled) * dx)));
  position.altitude = static_cast<i32>(u32(startAltitude) + u32(static_cast<i32>(f64(traveled) * dy)));
  position.z = static_cast<i32>(u32(startZ) + u32(static_cast<i32>(f64(traveled) * dz)));
  animationManager->MoveOccupant(static_cast<ScOccupant *>(this), &position);
  return 2;
}

// Choose a candidate people edge, excluding the supplied orientation unless reversing is allowed.
// Linux identifies the member and its original five arguments; Windows inlines this selection in route planning.
NwPeopleGraphEdge *NwPerson::SelectAnEdge(
    u32 *indices, u32 count, NwPeopleGraphNode *node, bool allowReverse, u8 orientation) {
  if (allowReverse)
    return &node->edges[indices[NwPersonRandom.Bounded(count)]];
  if (count == 1) {
    NwPeopleGraphEdge *edge = &node->edges[indices[0]];
    return edge->startOrientation == orientation ? nullptr : edge;
  }
  u32 selected = NwPersonRandom.Bounded(count - 1), current = 0;
  for (u32 i = 0; i < count; ++i) {
    NwPeopleGraphEdge *edge = &node->edges[indices[i]];
    if (edge->startOrientation != orientation) {
      if (current == selected)
        return edge;
      ++current;
    }
  }
  return nullptr;
}

// Find the people graph and matching node on a road occupant at the supplied micro-coordinates.
void NwPerson::GetPathGraphOnRoadOcc(ScOccupant *occupant, i32 pointX, i32 pointZ, NwPeopleGraph **graph,
    NwPeopleGraphNode **node) const {
  *graph = nullptr;
  *node = nullptr;
  NwConnection *connection;
  if (occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection))) {
    auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
    *graph = block->GetStrtSimAttrib()->peopleGraph;
    connection->Release();
    u32 originX, originZ;
    occupant->GetLocationInAnimCellCoords(&originX, &originZ);
    *node = (*graph)->GetNode(static_cast<i32>(u32(pointX) - originX), static_cast<i32>(u32(pointZ) - originZ));
  }
}

// Find the current road's people graph and nearest node, retaining no occupant reference on return.
// Linux identifies the member; the original Boolean contract is observed in the caller.
bool NwPerson::GetPathGraphAndNodeForMyPosition(NwPeopleGraph **graph, NwPeopleGraphNode **node) {
  ScOccupant *occupant;
  if (!occupantManager->GetOccupantAtCell(
          static_cast<u32>(x >> kCtMicroCoordinateShift), static_cast<u32>(z >> kCtMicroCoordinateShift), &occupant))
    return false;
  if (!(occupant->GetGeomFlag() & kOcGeomRoad)) {
    occupant->Release();
    return false;
  }
  GetPathGraphOnRoadOcc(occupant, x, z, graph, node);
  if (!*node && *graph) {
    u32 originX, originZ;
    occupant->GetLocationInAnimCellCoords(&originX, &originZ);
    u16 originAltitude = occupant->GetAltAnimCells();
    *node = (*graph)->GetNearestNodeAtGivenAltitude(static_cast<i32>(u32(x) - originX),
        static_cast<i32>(u32(altitude) - originAltitude), static_cast<i32>(u32(z) - originZ));
  }
  occupant->Release();
  return *node && *graph;
}

// Build a forward or reverse people path on the road occupant at the supplied cell.
NwPeoplePath *NwPerson::BuildPath(u32 cellX, u32 cellZ, NwPeopleGraphEdge *edge) {
  ScOccupant *occupant;
  if (!occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant))
    return nullptr;
  if (!(occupant->GetGeomFlag() & kOcGeomRoad)) {
    occupant->Release();
    return nullptr;
  }
  SpSprite *sprite = occupant->GetSpriteInst();
  u8 rotation;
  sprite->GetRotationOffset(&rotation);
  i32 originX = static_cast<i32>(cellX << kCtMicroCoordinateShift),
      originZ = static_cast<i32>(cellZ << kCtMicroCoordinateShift);
  u8 terrainAltitude = 0;
  dirtBag->GetVertexAltitude(cellX + NwPersonCornerX[rotation], cellZ + NwPersonCornerZ[rotation], &terrainAltitude);
  i32 originAltitude = static_cast<i32>(u32(terrainAltitude) << kCtMicroCoordinateShift);
  NwConnection *connection;
  occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection));
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  NwPeoplePath *path;
  if (edge->reverse)
    path = new NwPeopleBackwardPath(originX, originAltitude, originZ, block, edge);
  else
    path = new NwPeopleForwardPath(originX, originAltitude, originZ, block, edge);
  connection->Release();
  occupant->Release();
  return path;
}

// Find the neighboring road graph and create a straight path to its connected node.
// Linux identifies the member; the concrete street-layer rectangle operation remains unresolved.
NwPeoplePath *NwPerson::ChooseNextPathGraph(u8 orientation, NwPeopleGraph **graph, NwPeopleGraphNode **node) {
  u32 cellX = u32(x >> kCtMicroCoordinateShift) + u32(NwPersonStepX[orientation]);
  u32 cellZ = u32(z >> kCtMicroCoordinateShift) + u32(NwPersonStepZ[orientation]);
  auto *layer = static_cast<NwStreetLayer *>(streetLayer);
  ScOccupant *occupant;
  if (!layer->CellIsInPeopleSimRect(cellX, cellZ) || !occupantManager->GetOccupantAtCell(cellX, cellZ, &occupant))
    return nullptr;
  if (!(occupant->GetGeomFlag() & kOcGeomRoad)) {
    occupant->Release();
    return nullptr;
  }
  GetPathGraphOnRoadOcc(occupant, x, z, graph, node);
  if (!*node) {
    occupant->Release();
    return nullptr;
  }
  SpSprite *sprite = occupant->GetSpriteInst();
  u8 rotation;
  sprite->GetRotationOffset(&rotation);
  u8 terrainAltitude = 0;
  dirtBag->GetVertexAltitude(cellX + NwPersonCornerX[rotation], cellZ + NwPersonCornerZ[rotation], &terrainAltitude);
  i32 targetX = static_cast<i32>(u32((*node)->coordinate0) + (cellX << kCtMicroCoordinateShift));
  i32 targetAltitude = static_cast<i32>(u32((*node)->coordinate1) + (u32(terrainAltitude) << kCtMicroCoordinateShift));
  i32 targetZ = static_cast<i32>(u32((*node)->coordinate2) + (cellZ << kCtMicroCoordinateShift));
  NwPeoplePath *path = new NwPeopleStraightPath(x, altitude, z, targetX, targetAltitude, targetZ, orientation);
  occupant->Release();
  return path;
}

// Place a person at a random node of a transit tile and schedule its default script.
u8 NwPerson::PlaceOnTransitTile(ScOccupant *occupant) {
  NwConnection *connection;
  occupant->QueryInterface(IID_NwConnection, reinterpret_cast<void **>(&connection));
  auto *block = static_cast<NwTrafficBlockObject *>(connection->GetTrafficBlock());
  NwPeopleGraph *graph = block->GetStrtSimAttrib()->peopleGraph;
  connection->Release();
  u32 cellX, cellZ;
  occupant->GetLocationInCellCoords(&cellX, &cellZ);
  SpSprite *sprite = occupant->GetSpriteInst();
  u8 rotation;
  sprite->GetRotationOffset(&rotation);
  u8 terrainAltitude = 0;
  dirtBag->GetVertexAltitude(cellX + NwPersonCornerX[rotation], cellZ + NwPersonCornerZ[rotation], &terrainAltitude);
  if (graph->nodes.empty())
    return 0;
  u32 nodeIndex = NwPeopleGraphRandom.Bounded(static_cast<u32>(graph->nodes.size()));
  NwPeopleGraphNode *node = &graph->nodes[nodeIndex];
  x = static_cast<i32>(u32(node->coordinate0) + (cellX << kCtMicroCoordinateShift));
  z = static_cast<i32>(u32(node->coordinate2) + (cellZ << kCtMicroCoordinateShift));
  altitude = static_cast<i32>(u32(terrainAltitude) << kCtMicroCoordinateShift);
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  actionDescriptor = animation->GetDefaultActionDescriptor();
  u8 inserted = animationManager->InsertOccupant(static_cast<ScOccupant *>(this), 1, 0, true);
  if (!inserted)
    return inserted;
  if (ExecuteScript(attributes->scriptKey, 1, 0) == 2)
    return inserted;
  animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
  return 0;
}

// Advance stacked person actions, or start panic behavior when the current cell becomes unsafe.
i32 NwPerson::Tick(u32 elapsed) {
  if (actionStackTop < 0)
    return 0;
  auto *layer = static_cast<NwStreetLayer *>(streetLayer);
  if (!layer->IsPeopleSimRectCellUnsafe(
          static_cast<u32>(x >> kCtMicroCoordinateShift), static_cast<u32>(z >> kCtMicroCoordinateShift)) ||
      (personState & 1)) {
    i32 status = 0;
    while (true) {
      UpdateFn update = actionStack[actionStackTop];
      status = (this->*update)(elapsed, status, false);
      if (status == 2)
        break;
      if (--actionStackTop < 0) {
        animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
        return 0;
      }
    }
  } else {
    PanicBehavior();
  }
  return 0;
}

// Cancel current activity and select a panic animation and escape movement in an active simulation cell.
i32 NwPerson::PanicBehavior() {
  personState |= 1;
  auto *layer = static_cast<NwStreetLayer *>(streetLayer);
  if (!layer->CellIsInPeopleSimRect(
          static_cast<u32>(x >> kCtMicroCoordinateShift), static_cast<u32>(z >> kCtMicroCoordinateShift))) {
    animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
    return 0;
  }
  WipeActionStack();
  i32 offset = stateStackTop;
  stateStackTop += 2;
  auto *animation = static_cast<NwAnimSpriteAttrib *>(attributes->animationAttributes);
  u16 action;
  if (animation->GetActionDescriptor(11))
    action = 11;
  else if (animation->GetActionDescriptor(2))
    action = 2;
  else
    action = static_cast<u16>(animation->GetDefaultActionDescriptor()->action);
  std::memcpy(stateStack + offset, &action, sizeof(action));
  PushToActionStack(&NwPerson::PanicBehaviorUpdateFn);
  std::memcpy(&action, stateStack + offset, sizeof(action));
  i32 status = MoveNStages(8, 0, action, 80000, 8000);
  if (status == 2)
    return 2;
  animationManager->RemoveOccupant(static_cast<ScOccupant *>(this));
  return status;
}

// Continue escaping until a safe cell is reached, then restart the person's normal script.
i32 NwPerson::PanicBehaviorUpdateFn(u32, i32 status, bool cancel) {
  i32 offset = stateStackTop - 2;
  if (cancel || status != 0) {
    stateStackTop -= 2;
    return cancel ? 0 : 1;
  }
  auto *layer = static_cast<NwStreetLayer *>(streetLayer);
  if (layer->IsPeopleSimRectCellClearlySafe(
          static_cast<u32>(x >> kCtMicroCoordinateShift), static_cast<u32>(z >> kCtMicroCoordinateShift))) {
    --actionStackTop;
    --personState;
    stateStackTop -= 2;
    return ExecuteScript(attributes->scriptKey, 1, 0);
  }
  u16 action;
  std::memcpy(&action, stateStack + offset, sizeof(action));
  MoveNStages(8, 0, action, 80000, 8000);
  return 2;
}

u16 NwPerson::GetFrameNumber(i32 zoom, i32 rotation) const {
  return static_cast<u16>(NwPersonOrientationTable[8 * rotation + byte34] + actionDescriptor->frames[byte35] +
      zoomOffsets[zoom]);
}

i32 NwPerson::GetW(i32 zoom, i32 rotation) const {
  return spriteRepository->GetWidth(GetFrameNumber(zoom, rotation));
}

i32 NwPerson::GetH(i32 zoom, i32 rotation) const {
  return spriteRepository->GetHeight(GetFrameNumber(zoom, rotation));
}

u8 NwPerson::GetRegistration(i32 zoom, i32 rotation, ScRect *out) const {
  return spriteRepository->GetRegistration(GetFrameNumber(zoom, rotation), out);
}

u8 NwPerson::GetAreaFromPt(i32 zoom, i32 rotation, ScRect *out, i32 pointX, i32 pointY) const {
  return spriteRepository->GetDrawRect(GetFrameNumber(zoom, rotation), out, pointX, pointY);
}

u8 NwPerson::Draw(GxBuffer *target, i32 pointX, i32 pointY, i32 zoom, i32 rotation, ScRect *clip) const {
  return spriteRepository->DrawAt(target, pointX, pointY, GetFrameNumber(zoom, rotation), effect, clip);
}

u8 NwPerson::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  return spriteRepository->DrawFrame(target, &rect, GetFrameNumber(zoom, rotation), effect, clip);
}

bool NwPerson::IncludeRoads(ScOccupant *occupant) {
  return (occupant->GetGeomFlag() & kOcGeomRoad) != 0;
}

bool NwPerson::IsPixelOpaque(i32 pointX, i32 pointY, i32 zoom, i32 rotation) const {
  return spriteRepository->HitTest(GetFrameNumber(zoom, rotation), pointX, pointY);
}

// Select a route edge, preferring cells with intermediate safety when disasters are present.
// Linux identifies the member; the direct street-layer safety and rectangle operations remain unresolved.
NwPeopleGraphEdge *NwPerson::SelectNextEdge(NwPeopleGraphNode *node, u8 orientation) {
  bool allowReverse = NwPersonRandom.Bounded(100) <= 5;
  u8 reverse = 0;
  if (!allowReverse) {
    reverse = static_cast<u8>(orientation + 4);
    if (reverse >= 8)
      reverse = static_cast<u8>(reverse - 8);
  }
  auto *layer = static_cast<NwStreetLayer *>(streetLayer);
  if (!layer->DisastersPresent()) {
    if (!allowReverse)
      return node->SelectAnEdge(reverse);
    if (node->edges.empty())
      return nullptr;
    return &node->edges[NwPeopleNodeRandom.Bounded(static_cast<u32>(node->edges.size()))];
  }
  GeCellRect cell{static_cast<u32>(x >> kCtMicroCoordinateShift), static_cast<u32>(z >> kCtMicroCoordinateShift),
      static_cast<u32>(x >> kCtMicroCoordinateShift), static_cast<u32>(z >> kCtMicroCoordinateShift)};
  layer->GetSimRectIndices(&cell);
  i32 currentSafety = layer->SafetyValueInPeopleSimRect(static_cast<i32>(cell.left), static_cast<i32>(cell.top));
  u32 high[6], medium[6], low[6], highCount = 0, mediumCount = 0, lowCount = 0;
  u32 count = static_cast<u32>(node->edges.size());
  for (u32 i = 0; i < count; ++i) {
    NwPeopleGraphEdge *edge = &node->edges[i];
    i32 safety = currentSafety;
    if (!edge->target) {
      u32 nextX = cell.left + u32(NwPersonStepX[edge->endOrientation]);
      u32 nextZ = cell.top + u32(NwPersonStepZ[edge->endOrientation]);
      safety = layer->SafetyValueInPeopleSimRect(static_cast<i32>(nextX), static_cast<i32>(nextZ));
    }
    // The original fixed arrays have six entries; larger same-class sets overrun its stack.
    if (safety <= 0) {
      if (lowCount == 6)
        return nullptr;
      low[lowCount++] = i;
    } else if (safety <= 20) {
      if (mediumCount == 6)
        return nullptr;
      medium[mediumCount++] = i;
    } else {
      if (highCount == 6)
        return nullptr;
      high[highCount++] = i;
    }
  }
  NwPeopleGraphEdge *edge;
  if (mediumCount && (edge = SelectAnEdge(medium, mediumCount, node, allowReverse, reverse)))
    return edge;
  if (lowCount && (edge = SelectAnEdge(low, lowCount, node, allowReverse, reverse)))
    return edge;
  if (!allowReverse) {
    if (mediumCount && (edge = SelectAnEdge(medium, mediumCount, node, true, reverse)))
      return edge;
    if (lowCount && (edge = SelectAnEdge(low, lowCount, node, true, reverse)))
      return edge;
  }
  return highCount ? SelectAnEdge(high, highCount, node, allowReverse, reverse) : nullptr;
}

bool NwPerson::IsInState(i32 state) {
  return (personState & static_cast<u8>(state)) != 0;
}

void NwPerson::SetState(i32 state) {
  personState |= static_cast<u8>(state);
}

u8 NwPerson::ResetState(i32 state) {
  personState = static_cast<u8>(personState - static_cast<u8>(state));
  return personState;
}

// Test whether the altitude intersects the static occupant at the supplied point.
// Linux establishes the member and its manager/bounds call order.
bool NwPerson::InsideStaticOccupant(i32 pointX, i32 pointAltitude, i32 pointZ) {
  GeBounds bounds;
  ScOccupant *occupant;
  if (!occupantManager->GetOccupantAtCell(static_cast<u32>(pointX >> kCtMicroCoordinateShift),
          static_cast<u32>(pointZ >> kCtMicroCoordinateShift), &occupant))
    return false;
  occupant->GetBounds(&bounds);
  bool result = pointAltitude >= bounds.minimum.altitude && pointAltitude <= bounds.maximum.altitude;
  occupant->Release();
  return result;
}

u8 NwPerson::StaticShutdown() {
  if (staticInitialized) {
    resourceManager = nullptr;
    staticInitialized = 0;
  }
  return 1;
}

// Initialize zoom offsets and retain the city services used by person animation and placement.
u8 NwPerson::CitySpecificStaticInit(CtCity *city) {
  zoomOffsets[0] = 0;
  zoomOffsets[1] = 0;
  zoomOffsets[2] = 0;
  zoomOffsets[3] = 0;
  zoomOffsets[4] = 8;
  cityXMicroUnits = i32(city->AnimCellCountX());
  cityZMicroUnits = i32(city->AnimCellCountZ());
  occupantManager = ScAddRefNotNull(city->SurfaceOccupantManager());
  animationManager = ScAddRefNotNull(city->AnimOccupantManager());
  streetLayer = ScAddRefNotNull(city->StrtSimLayer());
  dirtBag = ScAddRefNotNull(city->DirtBag());
  return 1;
}

u8 NwPerson::CitySpecificStaticShutdown() {
  ScRelease(&streetLayer);
  ScRelease(&dirtBag);
  ScRelease(&animationManager);
  ScRelease(&occupantManager);
  return 1;
}

void NwPerson::SetBirthZoomLevel(i32 value) {
  birthZoomLevel = value;
}

i32 NwPerson::BirthZoomLevel() {
  return birthZoomLevel;
}

bool NwPerson::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_SpSprite, SpSprite);
  HANDLE_QUERY_INTERFACE(IID_NwPeopleInterface, NwPeopleInterface);
  return NwStreetOccupant::QueryInterface(iid, out);
}

u32 NwPerson::AddRef() {
  return NwStreetOccupant::AddRef();
}

u32 NwPerson::Release() {
  return NwStreetOccupant::Release();
}

bool NwPerson::IsGeomFlag(u32 mask) {
  return (GetGeomFlag() & mask) == mask;
}

u8 NwPerson::SetGeomFlag(u32) {
  return 0;
}

u8 NwPerson::ClearGeomFlag(u32) {
  return 0;
}

u32 NwPerson::GetGeomFlag() {
  return kNwPersonGeometry;
}

u8 NwPerson::SubTypeInfo() const {
  return 0;
}

u8 NwPerson::SetSpriteAttrib(SpDefinition *) {
  return 0;
}

// Borrow the retained producer interface without incrementing its reference count.
// Linux identifies the method; its distinct Windows entry remains unresolved.
NwPeopleProducerInterface *NwPerson::GetProducer() const {
  return producer;
}

u32 NwPerson::SetProducer(NwPeopleProducerInterface *value) {
  producer = value;
  return producer->AddRef();
}

bool NwPerson::InCityBoundsMicro(i32 pointX, i32 pointZ) {
  return pointX >= 0 && pointX < cityXMicroUnits && pointZ >= 0 && pointZ < cityZMicroUnits;
}

// Return the person's embedded sprite interface without acquiring another reference.
SpSprite *NwPerson::GetSpriteInst() {
  return static_cast<SpSprite *>(this);
}

void NwPerson::GetBoundingRectInCellCoords(GeCellRect *) const {
}

void NwPerson::GetRotationOffset(u8 *) const {
}

u32 NwPerson::GetLayerFlags() const {
  return 0;
}

void NwPerson::SetRotationOffset(u8) {
}

u8 NwPerson::SetEffect(u32 value) {
  effect = value;
  return 1;
}

void NwPerson::SetLayer(u8) {
}

u8 NwPerson::DemandArt(i32, i32, bool) {
  return 1;
}

i32 NwPerson::ReleaseArt(i32, i32) {
  return 1;
}

bool NwPerson::SetDefinition(SpDefinition *) {
  return true;
}

u8 NwPerson::HasGeomFlag(u32 mask) {
  return IsGeomFlag(mask);
}

void NwPerson::SetNext(SpSprite *) {
}

// Return the empty next-sprite link used by person sprites.
SpSprite *NwPerson::GetNext() {
  return nullptr;
}

// Borrow the edge and its attributes, retain the traffic block, then initialize the coordinates and forward cursor.
NwVehicleForwardPath::NwVehicleForwardPath(
    NwTrafficBlock *value, i32 xValue, i32 altitudeValue, i32 zValue, NwVehicleGraphEdge *edgeValue)
    : references(0) {
  edge = edgeValue;
  attributes = edge->pathAttributes;
  block = ScAddRefNotNull(value);
  x = xValue;
  altitude = altitudeValue;
  z = zValue;
  speed = edge->timeScale;
  Rewind();
}

// Copy a forward path's borrowed objects and coordinates while acquiring its own block reference and resetting its cursor.
NwVehicleForwardPath::NwVehicleForwardPath(NwVehicleForwardPath *source) : references(0) {
  edge = source->edge;
  attributes = source->attributes;
  block = ScAddRefNotNull(source->block);
  x = source->x;
  altitude = source->altitude;
  z = source->z;
  speed = source->speed;
  Rewind();
}

NwVehicleForwardPath::~NwVehicleForwardPath() {
  if (block)
    block->Release();
}

// Store the endpoints and derive their orientation, pitch and altitude-scaled segment length.
NwVehicleStraightPath::NwVehicleStraightPath(
    i32 x0, i32 y0, i32 z0, i32 x1, i32 y1, i32 z1)
    : startX(x0), startAltitude(y0), startZ(z0), endX(x1), endAltitude(y1), endZ(z1), references(0), speed(0) {
  f32 dx = static_cast<f32>(i32(u32(x1) - u32(x0)));
  f32 dy = static_cast<f32>(i32(u32(y1) - u32(y0)));
  f32 dz = static_cast<f32>(i32(u32(z1) - u32(z0)));
  i64 heading = static_cast<i64>((std::atan2(f64(dz), f64(dx)) + 3.141592654) * 5.092958178275649 + 0.5);
  orientation = static_cast<u8>(heading);
  if (orientation >= 32)
    orientation = 0;
  f64 horizontal = f64(dz) * dz + f64(dx) * dx;
  f32 storedHorizontal = static_cast<f32>(horizontal);
  f64 horizontalLength = std::sqrt(horizontal);
  if (horizontalLength == 0.0)
    pitch = dy == 0.0f ? std::nanf("") : std::copysign(HUGE_VALF, dy);
  else
    pitch = static_cast<f32>(f64(dy) / horizontalLength);
  f64 vertical = f64(dy) * f64(0.204125f);
  length = i32(u32(static_cast<i64>(std::sqrt(vertical * vertical + storedHorizontal))));
  Rewind();
}

// Publish either original path identity and retain it, or clear the output for an unsupported identity.
bool NwVehicleForwardPath::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleForwardPath);
  HANDLE_QUERY_INTERFACE(IID_NwVehiclePath, NwVehicleForwardPath);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwVehicleForwardPath::AddRef() {
  return ++references;
}

u32 NwVehicleForwardPath::Release() {
  return ScReleaseImpl(this, &references);
}

// Allocate a new unretained forward path with the same edge, block, coordinates and speed.
NwVehiclePath *NwVehicleForwardPath::GetCopy() {
  return new NwVehicleForwardPath(this);
}

void NwVehicleForwardPath::Rewind() {
  pointIndex = 0;
}

void NwVehicleForwardPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = i32(u32(x) + u32(attributes->x[0]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[0]));
  *outZ = i32(u32(z) + u32(attributes->z[0]));
}

void NwVehicleForwardPath::GetEndPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 last = attributes->pointCount - 1;
  *outX = i32(u32(x) + u32(attributes->x[last]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[last]));
  *outZ = i32(u32(z) + u32(attributes->z[last]));
}

u8 NwVehicleForwardPath::GetEndOrientation() {
  return attributes->endOrientation;
}

u8 NwVehicleForwardPath::GetPlacingOrientation() {
  return attributes->placingOrientation;
}

f32 NwVehicleForwardPath::GetPitch() {
  return attributes->pitch;
}

// Write the distance from the previous visited point to the destination when a point has been visited.
u8 NwVehicleForwardPath::GetDistToEndFromCurrentPoint(i32 *distance) {
  if (pointIndex <= 0)
    return 0;
  *distance = attributes->distances[pointIndex - 1];
  return 1;
}

// Write and advance the next translated resource point when one remains.
u8 NwVehicleForwardPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 index = pointIndex;
  if (index >= attributes->pointCount)
    return 0;
  *outX = i32(u32(x) + u32(attributes->x[index]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[pointIndex]));
  *outZ = i32(u32(z) + u32(attributes->z[pointIndex]));
  pointIndex = i32(u32(pointIndex) + 1);
  return 1;
}

i32 NwVehicleForwardPath::Length() {
  return attributes->distances[0];
}

void NwVehicleForwardPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(i32(u32(x) + u32(attributes->x[attributes->pointCount - 1])) >> kCtMicroCoordinateShift);
  *outZ = u32(i32(u32(z) + u32(attributes->z[attributes->pointCount - 1])) >> kCtMicroCoordinateShift);
}

void NwVehicleForwardPath::SetSpeed(f32 value) {
  speed = value;
}

f32 NwVehicleForwardPath::Speed() {
  return speed;
}

// Borrow the graph edge used to construct this path.
NwVehicleGraphEdge *NwVehicleForwardPath::GetEdge() {
  return edge;
}

// Borrow the last vehicle recorded for this edge's signed path index.
NwVehicle *NwVehicleForwardPath::LastVehicle() {
  return block->LastVehicle(edge->pathIndex);
}

u8 NwVehicleForwardPath::EnterVehicle(NwVehicle *vehicle, i32 distance) {
  return block->EnterVehicle(this, vehicle, distance, false);
}

void NwVehicleForwardPath::VehicleLeftPath(NwVehicle *vehicle) {
  block->VehicleLeftPath(edge->pathIndex, vehicle);
}

// Borrow the traffic block that this path retains.
NwTrafficBlock *NwVehicleForwardPath::GetStrtSimTrBlock() {
  return block;
}

u8 NwVehicleForwardPath::ForceEnterVehicle(NwVehicle *vehicle) {
  return block->EnterVehicle(this, vehicle, 0, true);
}

void NwVehicleForwardPath::BlockPath() {
  block->BlockPath(this);
}

void NwVehicleForwardPath::UnblockPath() {
  block->UnblockPath(this);
}

i32 NwVehicleForwardPath::GetReferenceMicroY() {
  return altitude;
}

u8 NwVehicleForwardPath::SortId(i32 *id) {
  *id = attributes->duration;
  return 1;
}

// Borrow the edge and attributes, retain the traffic block, then initialize the coordinates and reverse cursor.
NwVehicleBackwardPath::NwVehicleBackwardPath(
    NwTrafficBlock *value, i32 xValue, i32 altitudeValue, i32 zValue, NwVehicleGraphEdge *edgeValue)
    : references(0) {
  edge = edgeValue;
  attributes = edge->pathAttributes;
  block = ScAddRefNotNull(value);
  x = xValue;
  altitude = altitudeValue;
  z = zValue;
  speed = edge->timeScale;
  Rewind();
}

// Copy a backward path while acquiring its own block reference and resetting its reverse cursor.
NwVehicleBackwardPath::NwVehicleBackwardPath(NwVehicleBackwardPath *source) : references(0) {
  edge = source->edge;
  attributes = source->attributes;
  block = ScAddRefNotNull(source->block);
  x = source->x;
  altitude = source->altitude;
  z = source->z;
  speed = source->speed;
  Rewind();
}

NwVehicleBackwardPath::~NwVehicleBackwardPath() {
  if (block)
    block->Release();
}

// Publish either original path identity and retain it, or clear the output for an unsupported identity.
bool NwVehicleBackwardPath::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleBackwardPath);
  HANDLE_QUERY_INTERFACE(IID_NwVehiclePath, NwVehicleBackwardPath);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwVehicleBackwardPath::AddRef() {
  return ++references;
}

u32 NwVehicleBackwardPath::Release() {
  return ScReleaseImpl(this, &references);
}

// Allocate an unretained copy of the reverse path and restart it at its last point.
NwVehiclePath *NwVehicleBackwardPath::GetCopy() {
  return new NwVehicleBackwardPath(this);
}

void NwVehicleBackwardPath::Rewind() {
  pointIndex = i32(u32(attributes->pointCount) - 1);
}

void NwVehicleBackwardPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = i32(u32(x) + u32(attributes->x[attributes->pointCount - 1]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[attributes->pointCount - 1]));
  *outZ = i32(u32(z) + u32(attributes->z[attributes->pointCount - 1]));
}

void NwVehicleBackwardPath::GetEndPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = i32(u32(x) + u32(attributes->x[0]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[0]));
  *outZ = i32(u32(z) + u32(attributes->z[0]));
}

u8 NwVehicleBackwardPath::GetEndOrientation() {
  return attributes->beginOrientation;
}

// Turn the stored placing orientation by half a rotation using the original single subtraction.
u8 NwVehicleBackwardPath::GetPlacingOrientation() {
  u32 result = u32(attributes->placingOrientation) + 16;
  if (result >= 32)
    result -= 32;
  return u8(result);
}

f32 NwVehicleBackwardPath::GetPitch() {
  return attributes->pitch;
}

// Write the remaining reverse distance when the next higher point exists.
u8 NwVehicleBackwardPath::GetDistToEndFromCurrentPoint(i32 *distance) {
  i32 index = i32(u32(pointIndex) + 1);
  if (index >= attributes->pointCount)
    return 0;
  i32 length = Length();
  *distance = i32(u32(length) - u32(attributes->distances[index]));
  return 1;
}

// Write the next reverse resource point and move the cursor toward the first point.
u8 NwVehicleBackwardPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 index = pointIndex;
  if (index < 0)
    return 0;
  *outX = i32(u32(x) + u32(attributes->x[index]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[pointIndex]));
  *outZ = i32(u32(z) + u32(attributes->z[pointIndex]));
  pointIndex = i32(u32(pointIndex) - 1);
  return 1;
}

i32 NwVehicleBackwardPath::Length() {
  return attributes->distances[0];
}

void NwVehicleBackwardPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(i32(u32(x) + u32(attributes->x[0])) >> kCtMicroCoordinateShift);
  *outZ = u32(i32(u32(z) + u32(attributes->z[0])) >> kCtMicroCoordinateShift);
}

void NwVehicleBackwardPath::SetSpeed(f32 value) {
  speed = value;
}

f32 NwVehicleBackwardPath::Speed() {
  return speed;
}

// Borrow the graph edge from which this reverse path was constructed.
NwVehicleGraphEdge *NwVehicleBackwardPath::GetEdge() {
  return edge;
}

// Borrow the traffic block's last vehicle for the edge's signed path index.
NwVehicle *NwVehicleBackwardPath::LastVehicle() {
  return block->LastVehicle(edge->pathIndex);
}

u8 NwVehicleBackwardPath::EnterVehicle(NwVehicle *vehicle, i32 distance) {
  return block->EnterVehicle(this, vehicle, distance, false);
}

void NwVehicleBackwardPath::VehicleLeftPath(NwVehicle *vehicle) {
  block->VehicleLeftPath(edge->pathIndex, vehicle);
}

// Borrow the traffic block retained by this reverse path.
NwTrafficBlock *NwVehicleBackwardPath::GetStrtSimTrBlock() {
  return block;
}

u8 NwVehicleBackwardPath::ForceEnterVehicle(NwVehicle *vehicle) {
  return block->EnterVehicle(this, vehicle, 0, true);
}

void NwVehicleBackwardPath::BlockPath() {
  block->BlockPath(this);
}

void NwVehicleBackwardPath::UnblockPath() {
  block->UnblockPath(this);
}

i32 NwVehicleBackwardPath::GetReferenceMicroY() {
  return altitude;
}

u8 NwVehicleBackwardPath::SortId(i32 *id) {
  *id = attributes->duration;
  return 1;
}

NwVehicleAttribPath::NwVehicleAttribPath(i32 xValue, i32 altitudeValue, i32 zValue, NwVehiclePathAttrib *value)
    : references(0), x(xValue), altitude(altitudeValue), z(zValue), attributes(value), speed(0.0f) {
  Rewind();
}

// Publish either path identity and retain it, or clear the output when the identity is unsupported.
bool NwVehicleAttribPath::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleAttribPath);
  HANDLE_QUERY_INTERFACE(IID_NwVehiclePath, NwVehicleAttribPath);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwVehicleAttribPath::AddRef() {
  return ++references;
}

u32 NwVehicleAttribPath::Release() {
  return ScReleaseImpl(this, &references);
}

// The original attribute-only path does not offer a copy.
NwVehiclePath *NwVehicleAttribPath::GetCopy() {
  return nullptr;
}

void NwVehicleAttribPath::Rewind() {
  pointIndex = 0;
}

// Write the translated first resource point.
void NwVehicleAttribPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = i32(u32(x) + u32(attributes->x[0]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[0]));
  *outZ = i32(u32(z) + u32(attributes->z[0]));
}

// Write the translated final resource point.
void NwVehicleAttribPath::GetEndPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 last = attributes->pointCount - 1;
  *outX = i32(u32(x) + u32(attributes->x[last]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[last]));
  *outZ = i32(u32(z) + u32(attributes->z[last]));
}

u8 NwVehicleAttribPath::GetEndOrientation() {
  return attributes->endOrientation;
}

u8 NwVehicleAttribPath::GetPlacingOrientation() {
  return attributes->placingOrientation;
}

f32 NwVehicleAttribPath::GetPitch() {
  return attributes->pitch;
}

// Write the remaining distance when the cursor has passed at least one point.
u8 NwVehicleAttribPath::GetDistToEndFromCurrentPoint(i32 *distance) {
  if (pointIndex <= 0)
    return 0;
  *distance = attributes->distances[pointIndex - 1];
  return 1;
}

// Write and advance the next translated point when one remains.
u8 NwVehicleAttribPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 index = pointIndex;
  if (index >= attributes->pointCount)
    return 0;
  *outX = i32(u32(x) + u32(attributes->x[index]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[pointIndex]));
  *outZ = i32(u32(z) + u32(attributes->z[pointIndex]));
  pointIndex = i32(u32(pointIndex) + 1);
  return 1;
}

i32 NwVehicleAttribPath::Length() {
  return attributes->distances[0];
}

void NwVehicleAttribPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(i32(u32(x) + u32(attributes->x[attributes->pointCount - 1])) >> kCtMicroCoordinateShift);
  *outZ = u32(i32(u32(z) + u32(attributes->z[attributes->pointCount - 1])) >> kCtMicroCoordinateShift);
}

void NwVehicleAttribPath::SetSpeed(f32 value) {
  speed = value;
}

f32 NwVehicleAttribPath::Speed() {
  return speed;
}

// The original attribute-only path has no graph edge.
NwVehicleGraphEdge *NwVehicleAttribPath::GetEdge() {
  return nullptr;
}

// The original attribute-only path has no traffic-block vehicle queue.
NwVehicle *NwVehicleAttribPath::LastVehicle() {
  return nullptr;
}

u8 NwVehicleAttribPath::EnterVehicle(NwVehicle *, i32) {
  return 1;
}

void NwVehicleAttribPath::VehicleLeftPath(NwVehicle *) {
}

// The original attribute-only path does not retain a traffic block.
NwTrafficBlock *NwVehicleAttribPath::GetStrtSimTrBlock() {
  return nullptr;
}

u8 NwVehicleAttribPath::ForceEnterVehicle(NwVehicle *) {
  return 1;
}

void NwVehicleAttribPath::BlockPath() {
}

void NwVehicleAttribPath::UnblockPath() {
}

i32 NwVehicleAttribPath::GetReferenceMicroY() {
  return 0;
}

u8 NwVehicleAttribPath::SortId(i32 *id) {
  *id = attributes->duration;
  return 1;
}

// Publish either original path identity and retain it, or clear the unsupported output.
bool NwVehicleStraightPath::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwVehicleStraightPath);
  HANDLE_QUERY_INTERFACE(IID_NwVehiclePath, NwVehicleStraightPath);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwVehicleStraightPath::AddRef() {
  return ++references;
}

u32 NwVehicleStraightPath::Release() {
  return ScReleaseImpl(this, &references);
}

// The original straight path does not supply a copy.
NwVehiclePath *NwVehicleStraightPath::GetCopy() {
  return nullptr;
}

void NwVehicleStraightPath::Rewind() {
  pointIndex = -1;
}

void NwVehicleStraightPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = startX;
  *outAltitude = startAltitude;
  *outZ = startZ;
}

void NwVehicleStraightPath::GetEndPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = endX;
  *outAltitude = endAltitude;
  *outZ = endZ;
}

u8 NwVehicleStraightPath::GetEndOrientation() {
  return orientation;
}

u8 NwVehicleStraightPath::GetPlacingOrientation() {
  return orientation;
}

f32 NwVehicleStraightPath::GetPitch() {
  return pitch;
}

// Write the original distance for the current cursor and preserve its success decision.
u8 NwVehicleStraightPath::GetDistToEndFromCurrentPoint(i32 *distance) {
  if (pointIndex) {
    u8 result = pointIndex == 1;
    *distance = 0;
    return result;
  }
  *distance = length;
  return 1;
}

// Advance through the two stored endpoints, leaving outputs untouched after traversal finishes.
u8 NwVehicleStraightPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  if (pointIndex >= 1)
    return 0;
  pointIndex = i8(u8(pointIndex) + 1);
  if (!pointIndex) {
    *outX = startX;
    *outAltitude = startAltitude;
    *outZ = startZ;
  } else if (pointIndex == 1) {
    *outX = endX;
    *outAltitude = endAltitude;
    *outZ = endZ;
  } else {
    return 0;
  }
  return 1;
}

i32 NwVehicleStraightPath::Length() {
  return length;
}

void NwVehicleStraightPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(endX >> kCtMicroCoordinateShift);
  *outZ = u32(endZ >> kCtMicroCoordinateShift);
}

void NwVehicleStraightPath::SetSpeed(f32 value) {
  speed = value;
}

f32 NwVehicleStraightPath::Speed() {
  return speed;
}

// A straight path has no graph edge.
NwVehicleGraphEdge *NwVehicleStraightPath::GetEdge() {
  return nullptr;
}

// A straight path has no traffic-block vehicle queue.
NwVehicle *NwVehicleStraightPath::LastVehicle() {
  return nullptr;
}

u8 NwVehicleStraightPath::EnterVehicle(NwVehicle *, i32) {
  return 1;
}

void NwVehicleStraightPath::VehicleLeftPath(NwVehicle *) {
}

// A straight path does not retain a traffic block.
NwTrafficBlock *NwVehicleStraightPath::GetStrtSimTrBlock() {
  return nullptr;
}

u8 NwVehicleStraightPath::ForceEnterVehicle(NwVehicle *) {
  return 1;
}

void NwVehicleStraightPath::BlockPath() {
}

void NwVehicleStraightPath::UnblockPath() {
}

i32 NwVehicleStraightPath::GetReferenceMicroY() {
  return 0;
}

u8 NwVehicleStraightPath::SortId(i32 *) {
  return 0;
}

NwPeoplePath::~NwPeoplePath() {
}

// Borrow the edge and path resource, retain the block, and initialize the forward origin and cursor.
NwPeopleForwardPath::NwPeopleForwardPath(
    i32 xValue, i32 altitudeValue, i32 zValue, NwTrafficBlockObject *value, NwPeopleGraphEdge *edgeValue) {
  edge = edgeValue;
  attributes = edge->pathAttributes;
  block = ScAddRefNotNull(value);
  pointIndex = 0;
  x = xValue;
  altitude = altitudeValue;
  z = zValue;
}

NwPeopleForwardPath::~NwPeopleForwardPath() {
  if (block)
    block->Release();
}

void NwPeopleForwardPath::Rewind() {
  pointIndex = 0;
}

// Write the first point translated by the stored origin.
void NwPeopleForwardPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = i32(u32(x) + u32(attributes->x[0]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[0]));
  *outZ = i32(u32(z) + u32(attributes->z[0]));
}

// Write the next translated point and advance the cursor when a point remains.
u8 NwPeopleForwardPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 index = pointIndex;
  if (index >= attributes->pointCount)
    return 0;
  *outX = i32(u32(x) + u32(attributes->x[index]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[pointIndex]));
  *outZ = i32(u32(z) + u32(attributes->z[pointIndex]));
  pointIndex = i32(u32(pointIndex) + 1);
  return 1;
}

u8 NwPeopleForwardPath::GetEndOrientation() {
  return attributes->endOrientation;
}

void NwPeopleForwardPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(i32(u32(x) + u32(attributes->x[attributes->pointCount - 1])) >> kCtMicroCoordinateShift);
  *outZ = u32(i32(u32(z) + u32(attributes->z[attributes->pointCount - 1])) >> kCtMicroCoordinateShift);
}

// Admit unindexed paths immediately or ask the block to check crossing vehicles and count the person.
u8 NwPeopleForwardPath::EnterPerson(NwPeopleInterface *person) {
  if (edge->pathIndex < 0)
    return 1;
  return block->EnterPerson(this, person);
}

void NwPeopleForwardPath::PersonLeftPath(NwPeopleInterface *person) {
  i8 index = edge->pathIndex;
  if (index >= 0)
    block->PersonLeftPath(index, person);
}

// Borrow the graph edge used to create the people path.
NwPeopleGraphEdge *NwPeopleForwardPath::GetEdge() {
  return edge;
}

// Borrow the edge and path resource, retain the block, and start traversal at the final resource point.
NwPeopleBackwardPath::NwPeopleBackwardPath(
    i32 xValue, i32 altitudeValue, i32 zValue, NwTrafficBlockObject *value, NwPeopleGraphEdge *edgeValue) {
  edge = edgeValue;
  attributes = edge->pathAttributes;
  block = ScAddRefNotNull(value);
  x = xValue;
  altitude = altitudeValue;
  z = zValue;
  pointIndex = i32(u32(attributes->pointCount) - 1);
}

NwPeopleBackwardPath::~NwPeopleBackwardPath() {
  if (block)
    block->Release();
}

void NwPeopleBackwardPath::Rewind() {
  pointIndex = i32(u32(attributes->pointCount) - 1);
}

// Write the final resource point translated by the stored origin.
void NwPeopleBackwardPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 last = attributes->pointCount - 1;
  *outX = i32(u32(x) + u32(attributes->x[last]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[last]));
  *outZ = i32(u32(z) + u32(attributes->z[last]));
}

// Write the next reverse point and decrement the cursor when a point remains.
u8 NwPeopleBackwardPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  i32 index = pointIndex;
  if (index < 0)
    return 0;
  *outX = i32(u32(x) + u32(attributes->x[index]));
  *outAltitude = i32(u32(altitude) + u32(attributes->altitude[pointIndex]));
  *outZ = i32(u32(z) + u32(attributes->z[pointIndex]));
  pointIndex = i32(u32(pointIndex) - 1);
  return 1;
}

u8 NwPeopleBackwardPath::GetEndOrientation() {
  return attributes->beginOrientation;
}

void NwPeopleBackwardPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(i32(u32(x) + u32(attributes->x[0])) >> kCtMicroCoordinateShift);
  *outZ = u32(i32(u32(z) + u32(attributes->z[0])) >> kCtMicroCoordinateShift);
}

// Admit unindexed paths immediately or ask the block to check crossing vehicles and count the person.
u8 NwPeopleBackwardPath::EnterPerson(NwPeopleInterface *person) {
  if (edge->pathIndex < 0)
    return 1;
  return block->EnterPerson(this, person);
}

void NwPeopleBackwardPath::PersonLeftPath(NwPeopleInterface *person) {
  i8 index = edge->pathIndex;
  if (index >= 0)
    block->PersonLeftPath(index, person);
}

// Borrow the graph edge used to create the reverse people path.
NwPeopleGraphEdge *NwPeopleBackwardPath::GetEdge() {
  return edge;
}

// Initialize the two endpoints and compute their eight-way orientation.
NwPeopleStraightPath::NwPeopleStraightPath(
    i32 firstX, i32 firstAltitude, i32 firstZ, i32 lastX, i32 lastAltitude, i32 lastZ)
    : pointIndex(0), startX(firstX), startAltitude(firstAltitude), startZ(firstZ), endX(lastX),
      endAltitude(lastAltitude), endZ(lastZ) {
  i32 deltaX = static_cast<i32>(u32(lastX) - u32(firstX)), deltaZ = static_cast<i32>(u32(lastZ) - u32(firstZ));
  f64 value = (std::atan2(f64(deltaZ), f64(deltaX)) + 3.141592654) * 1.273239544568912 + 0.5;
  orientation = static_cast<u8>(static_cast<i64>(value));
  if (orientation == 8)
    orientation = 0;
}

// Initialize a straight people path with the route's already selected orientation.
// The Linux ChooseNextPathGraph body shows this constructor inlined with all seven inputs.
NwPeopleStraightPath::NwPeopleStraightPath(
    i32 firstX, i32 firstAltitude, i32 firstZ, i32 lastX, i32 lastAltitude, i32 lastZ, u8 direction)
    : pointIndex(0), startX(firstX), startAltitude(firstAltitude), startZ(firstZ), endX(lastX),
      endAltitude(lastAltitude), endZ(lastZ), orientation(direction) {
}

NwPeopleStraightPath::~NwPeopleStraightPath() {
}

void NwPeopleStraightPath::Rewind() {
  pointIndex = 0;
}

void NwPeopleStraightPath::GetStartPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  *outX = startX;
  *outAltitude = startAltitude;
  *outZ = startZ;
}

// Write the current endpoint and advance only after all three coordinates have been written.
u8 NwPeopleStraightPath::GetNextPoint(i32 *outX, i32 *outAltitude, i32 *outZ) {
  if (pointIndex == 0) {
    *outX = startX;
    *outAltitude = startAltitude;
    *outZ = startZ;
    pointIndex = 1;
  } else if (pointIndex == 1) {
    *outX = endX;
    *outAltitude = endAltitude;
    *outZ = endZ;
    pointIndex = 2;
  } else {
    return 0;
  }
  return 1;
}

u8 NwPeopleStraightPath::GetEndOrientation() {
  return orientation;
}

void NwPeopleStraightPath::GetEndCell(u32 *outX, u32 *outZ) {
  *outX = u32(endX >> kCtMicroCoordinateShift);
  *outZ = u32(endZ >> kCtMicroCoordinateShift);
}

u8 NwPeopleStraightPath::EnterPerson(NwPeopleInterface *) {
  return 1;
}

void NwPeopleStraightPath::PersonLeftPath(NwPeopleInterface *) {
}

// A straight people path has no graph edge.
NwPeopleGraphEdge *NwPeopleStraightPath::GetEdge() {
  return nullptr;
}
