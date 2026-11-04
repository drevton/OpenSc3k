#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <set>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/ids.h"
#include "common/rect.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "economy/api.h"
#include "environment/api.h"
#include "geometry/ge_types.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "occupants/api.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"
#include "sprites/sp_effect.h"
#include "sprites/sp_tick_manager.h"
#include "sprites/sprite.h"
#include "sprites/view_camera.h"
#include "terrain/api.h"

// Power icons synchronize their surface/pipe/subway geometry; clouds retain the initial step deadline until weather sets it.
enum SpDisplayGeometryPolicy : u32 {
  kSpPowerIconWithoutUndergroundGeometry = 0xff7bffff,
  kSpPowerIconLayerGeometryMask = 0x840100,
  kSpCloudInitialStepTime = 0xffffffff,
};

// Particle kinds select separate installed image families before the zoom offset is added.
enum SpParticleImageFamily : u32 {
  kSpParticleKindsZeroOneThreeImageBase = 0xfffd2440,
  kSpParticleKindTwoImageBase = 0xfffd2430,
  kSpParticleKindsFourFiveImageBase = 0xfffd2420,
  kSpParticleKindSixImageBase = 0xfffd2410,
};

// Terrain changes queue a targeted radiation-icon refresh for each selected cell.
enum SpRadiationNotification : u32 {
  kSpMsg_RefreshRadiationCellIcon = 0x63792e9f,
};

// Multi-frame trajectory kinds share the original animation interval.
enum SpTrajectoryTiming : i32 {
  kSpTrajectoryMultiFrameInterval = 0x493e0,
};

// Hidden lightning and outside trajectories share a parking altitude; lightning visibility has a ten-unit margin.
enum SpEffectParkingPosition : i32 {
  kSpOffWorldAnimationAltitude = 0x400000,
  kSpVisibleAnimationAltitudeLimit = kSpOffWorldAnimationAltitude - 10,
};

// Frame deadlines and power-icon blink intervals retain their separate effect roles.
enum SpBasicEffectTiming : i32 {
  kSpSmokeFrameInterval = 0x1e848,
  kSpTrajectoryInitialFrameInterval = 0x186a0,
  kSpSplashFrameInterval = 0x30d40,
  kSpPowerDefaultBlinkInterval = 0xf4240,
  kSpPowerZoomBlinkBase = 0x1e8480,
  kSpPowerZoomBlinkStep = 200000,
};

// Original effect families retain independent frame, scheduling and lifetime thresholds.
enum SpEffectTiming : i32 {
  kSpPrecipitationMinimumDelay = 30000000,
  kSpWeatherMaximumEventDelay = 60000000,
  kSpLightningMinimumDelay = 5000000,
  kSpBirdFlockMinimumDelay = 100000000,
  kSpBirdFlockMaximumDelay = 200000000,
  kSpLightningRetirementAge = 150000,
  kSpPrecipitationInitialStepTime = 1000000,
  kSpDebrisDefaultFrameInterval = 130000,
  kSpDebrisMinimumFrameInterval = 32500,
  kSpDebrisMaximumFrameInterval = 195000,
  kSpDebrisSettledLifetime = 20000000,
  kSpParticleFrameInterval = 100000,
  kSpWaterDebrisFrameInterval = 1000000,
  kSpNuclearInitialFrameInterval = 200000,
  kSpNuclearLaterFrameInterval = 4000000,
};

// Debris special variants retain separate color/shadow offsets; explosions retain separate color and mask image bases.
enum SpEffectArtworkKey : u32 {
  kSpDebrisVariant1005ColorOffset = 0x2ff80,
  kSpDebrisVariant1006ColorOffset = 0x2ff7b,
  kSpDebrisVariant1005ShadowOffset = 0x2fe80,
  kSpDebrisVariant1006ShadowOffset = 0x2fe7b,
  kSpExplosionVariantEightColorImageBase = 0x3fb70000,
  kSpExplosionVariantEightMaskImageBase = 0x3fb70014,
};

// Neighbor icons choose a distinct image family for odd and even camera rotations before applying orientation and zoom.
enum SpNeighborIconArtwork : u32 {
  kSpNeighborOddCameraImageBase = 0x243a0000,
  kSpNeighborEvenCameraImageBase = 0x243b0000,
};

// Effect artwork computes resource keys from zoom and variant using distinct color, mask, shadow and icon offsets.
enum SpEffectImageOffset : i32 {
  kSpCloudColorKeyOffset = 0x3f000,
  kSpCloudMaskKeyOffset = 0x3ef00,
  kSpCloudShadowMaskKeyOffset = 0x3ee00,
  kSpPrecipitationColorKeyOffset = 0x3e800,
  kSpPrecipitationMaskKeyOffset = 0x3e700,
  kSpLightningImageKeyOffset = 0x1ea80,
  kSpDebrisColorKeyOffset = 0x30000,
  kSpDebrisShadowKeyOffset = 0x2ff00,
  kSpRadioactivityIconKeyOffset = 0x2d000,
  kSpPowerIconKeyOffset = 0x1ea00,
};

// Installed artwork identities retain each effect's color, mask, overlay and shadow roles.
enum SpBasicEffectImageId : u32 {
  kSpSplashColorImageBase = 0xfffe0000,
  kSpSplashMaskImageBase = 0xfffe0100,
  kSpSmokeColorImageBase = 0xfffe1200,
  kSpSmokeMaskImageBase = 0xfffe1280,
  kSpTrajectoryDefaultColorImageBase = 0xfffe1300,
  kSpExplosionColorImageBase = 0xfffd1000,
  kSpExplosionMaskImageBase = 0xfffd1200,
  kSpExplosionOverlayImageBase = 0xfffd1400,
  kSpParticleAgeColorTableImageId = 0xfffd2400,
  kSpNuclearColorImageBase = 0xfffd2000,
  kSpNuclearMaskImageBase = 0xfffd2100,
};

u32 SpPowerIcon::blink_time = kSpPowerDefaultBlinkInterval;

// Reproduce the original biased double-store conversion without aliasing a floating-point object.
// Extracting the low word also avoids undefined float-to-integer casts outside the signed range.
static i32 SpEffectCoordinate(f64 coordinate) {
  f64 value = coordinate + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  return (i32)((u32)bits + kScDoubleIntegerLowWordBias);
}

// Initialize the original effects base with no city membership or references and geometry bit one. Its float position
// starts at zero.
SpEffectOccupant::SpEffectOccupant() {
  in_city = references = 0;
  geom_flags = 1;
  position = {0.0f, 0.0f, 0.0f};
}

SpEffectOccupant::~SpEffectOccupant() {
}

// Expose the original Unknown and occupant identities and acquire their virtual reference. Unsupported requests
// explicitly clear the output.
bool SpEffectOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpEffectOccupant::AddRef() {
  return ++references;
}

// Decrement the byte reference count, deleting through the real virtual destructor when its previous value was one. A
// zero starting count wraps to 255.
u32 SpEffectOccupant::Release() {
  u8 previous = references--;
  if (previous != 1)
    return references;
  delete this;
  return 0;
}

bool SpEffectOccupant::IsInCity() const {
  return in_city != 0;
}

bool SpEffectOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & in_city) != 0;
}

u32 SpEffectOccupant::GetIsInCityFlag() const {
  return in_city;
}

void SpEffectOccupant::SetIsInCityFlag(u32 mask) {
  in_city = (u8)mask;
}

void SpEffectOccupant::ClearIsInCityFlag(u32 mask) {
  in_city &= (u8)~mask;
}

bool SpEffectOccupant::IsGeomFlag(u32 mask) {
  return (geom_flags & mask) == mask;
}

u8 SpEffectOccupant::SetGeomFlag(u32 mask) {
  geom_flags |= mask;
  return 1;
}

u8 SpEffectOccupant::ClearGeomFlag(u32 mask) {
  geom_flags &= ~mask;
  return 1;
}

u32 SpEffectOccupant::GetGeomFlag() {
  return geom_flags;
}

// Resolve artwork only for the effect's current default layer. Both the layer and the unindexed artwork lookup retain
// their original virtual dispatch.
SpDefinition *SpEffectOccupant::GetSpriteAttrib(u32 layer) {
  if (layer == GetDefaultLayerFlag())
    return GetSpriteAttrib();
  return nullptr;
}

// Resolve the derived sprite only when its default layer matches. The unindexed operation remains the real pure virtual
// dependency.
SpSprite *SpEffectOccupant::GetSpriteInst(u32 layer) {
  if (layer == GetDefaultLayerFlag())
    return GetSpriteInst();
  return nullptr;
}

// Round the float position to fixed city coordinates using the original double-store conversion. City coordinates order
// X/Z/altitude rather than the vector's X/Y/Z.
void SpEffectOccupant::GetLocation(CtCoord *out) const {
  out->x = SpEffectCoordinate(position.x);
  out->z = SpEffectCoordinate(position.z);
  out->altitude = SpEffectCoordinate(position.y);
}

void SpEffectOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = (u32)(SpEffectCoordinate(position.x) / kCtMicroUnitsPerCell);
  *z = (u32)(SpEffectCoordinate(position.z) / kCtMicroUnitsPerCell);
}

u8 SpEffectOccupant::GetAlt() const {
  return (u8)(SpEffectCoordinate(position.y) / 256);
}

void SpEffectOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  *x = (u32)SpEffectCoordinate(position.x);
  *z = (u32)SpEffectCoordinate(position.z);
}

u16 SpEffectOccupant::GetAltAnimCells() const {
  return (u16)SpEffectCoordinate(position.y);
}

// Publish the rounded position and its one-unit exclusive maximum in fixed city coordinates. Unsigned addition
// preserves the defined DWORD wrap at the coordinate limit.
void SpEffectOccupant::GetBounds(GeBounds *out) const {
  out->minimum.x = SpEffectCoordinate(position.x);
  out->minimum.z = SpEffectCoordinate(position.z);
  out->minimum.altitude = SpEffectCoordinate(position.y);
  out->maximum.x = (i32)((u32)SpEffectCoordinate(position.x) + 1);
  out->maximum.z = (i32)((u32)SpEffectCoordinate(position.z) + 1);
  out->maximum.altitude = (i32)((u32)SpEffectCoordinate(position.y) + 1);
}

// Preserve the installed three-store rectangle operation: left=X, top=Z and right=Z+1. The original leaves bottom
// untouched; do not read or manufacture that unwritten member.
void SpEffectOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  out->left = (u32)(SpEffectCoordinate(position.x) / kCtMicroUnitsPerCell);
  out->top = (u32)(SpEffectCoordinate(position.z) / kCtMicroUnitsPerCell);
  out->right = out->top + 1;
}

bool SpEffectOccupant::DoMessage(ScMessage *) {
  return false;
}

bool SpEffectOccupant::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool SpEffectOccupant::Init(GeAttrib *) {
  return false;
}

bool SpEffectOccupant::Init(RsKey *) {
  return false;
}

bool SpEffectOccupant::Init(CtCity *, OcPersistInfo *) {
  return false;
}

void SpEffectOccupant::Shutdown() {
}

bool SpEffectOccupant::Save(CtCity *, OcPersistInfo *) {
  return false;
}

u32 SpEffectOccupant::GetOccLayerFlag() {
  return 1;
}

void SpEffectOccupant::SetOccLayerFlag(u32) {
}

u8 SpEffectOccupant::MajorType() const {
  return 0;
}

u8 SpEffectOccupant::SubTypeInfo() const {
  return 0;
}

u32 SpEffectOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 SpEffectOccupant::AnimType() const {
  return 2;
}

void SpEffectOccupant::AnimStart() {
}

void SpEffectOccupant::AnimStop() {
}

bool SpEffectOccupant::CanSimRemove() const {
  return false;
}

void SpEffectOccupant::SetSimRemove(bool) {
}

bool SpEffectOccupant::CanUserRemove() const {
  return false;
}

void SpEffectOccupant::SetUserRemove(bool) {
}

bool SpEffectOccupant::GetName(ScStringView *) {
  return false;
}

// Return the original null attribute pointer. This base owns no occupant definition.
GeAttrib *SpEffectOccupant::GetAttribData() {
  return nullptr;
}

bool SpEffectOccupant::GetSubSystemData(u32, void **) {
  return false;
}

u32 SpEffectOccupant::TypeId() const {
  return 0;
}

// Return the original null city-agent type. The base effect has no simulation agent.
CtAgentType *SpEffectOccupant::CityAgentType() {
  return nullptr;
}

bool SpEffectOccupant::CreateQueryDialog(UiQueryWindow **) {
  return false;
}

u8 SpEffectOccupant::SerialRead(CtCity *, RsStream *) {
  return 1;
}

u8 SpEffectOccupant::SerialWrite(CtCity *, RsStream *) {
  return 1;
}

u32 SpEffectOccupant::GetSoundObjectId(i32) {
  return 0;
}

// Return the original null base artwork definition. Derived effects supply any actual definition.
SpDefinition *SpEffectOccupant::GetSpriteAttrib() {
  return nullptr;
}

u32 SpEffectOccupant::GetDefaultLayerFlag() const {
  return 1;
}

u8 SpEffectOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

void SpEffectOccupant::GetOrientation(u16 *, u16 *, u16 *) const {
}

bool SpEffectOccupant::GetRotationFlag(u32 *) const {
  return false;
}

u8 SpEffectOccupant::SizeInCellsX() const {
  return 0;
}

u8 SpEffectOccupant::SizeInCellsZ() const {
  return 0;
}

u8 SpEffectOccupant::SizeInCellsY() const {
  return 0;
}

u32 SpEffectOccupant::SizeInAnimCellsZ() const {
  return 1;
}

u32 SpEffectOccupant::SizeInAnimCellsX() const {
  return 1;
}

u32 SpEffectOccupant::SizeInAnimCellsY() const {
  return 1;
}

void SpEffectOccupant::SetLocation(const CtCoord &) {
}

void SpEffectOccupant::SetLocationInCellCoords(u32, u32) {
}

void SpEffectOccupant::SetAlt(u8) {
}

bool SpEffectOccupant::GetMarkBit() const {
  return false;
}

void SpEffectOccupant::SetMarkBit(bool) {
}

i32 SpEffectOccupant::Tick(u32) {
  return 0;
}

void SpEffectOccupant::SetSubTypeInfo(u8) {
}

void SpEffectOccupant::SetOrientation(u16, u16, u16) {
}

u8 SpEffectOccupant::SetRotationFlag(u32) {
  return 0;
}

u32 SpEffectOccupant::GetMemUse() const {
  return 24;
}

bool SpEffectOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool SpEffectOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool SpEffectOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

void SpEffectOccupant::GetBoundingRectInAnimCellCoords(GeCellRect *) const {
}

void SpEffectOccupant::SetAltAnimCells(u16) {
}

void SpEffectOccupant::SetLocationInAnimCoords(u32, u32) {
}

bool SpEffects::initialized;
ApPerformanceInfo *SpEffects::performance;
SpCellMap *SpEffects::cell_map;
SpView *SpEffects::view;
SpManagerView *SpEffects::manager;
CtCity *SpEffects::city;
ScDirtBag *SpEffects::dirt;
SpTickManager *SpEffects::tick_manager;
SpImageCache *SpEffects::images;
ScMessageServer *SpEffects::messages;
ScOccupantManager *SpEffects::surface;
OcAnimManager *SpEffects::animated;
ApBoxX *SpEffects::box;
CtNeighborsLayer *SpEffects::neighbors;
u32 SpEffects::transparent_color;
i32 SpEffects::zoom, SpEffects::rotation, SpEffects::view_id = -1;
i32 SpEffects::micro_width, SpEffects::micro_height;
ScRandom SpEffects::random(kScRandomSeedFromClock);
std::atomic<i32> SpEffects::thread_safe_flag{0};
SpViewChange SpEffects::view_change;
SpLandChange SpEffects::land_change;

bool SpViewChange::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpViewChange::AddRef() {
  return 1;
}

u32 SpViewChange::Release() {
  return 1;
}

// Refresh shared zoom, rotation and view type from the supplied snapshot in the original getter order.
// Other message types and view-change subtypes leave the cached values untouched and still return true.
bool SpViewChange::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ViewInfoUpdated && message->data1 == kSpViewUpdate_Snapshot) {
    SpIsoViewInfo *snapshot = reinterpret_cast<SpIsoViewInfo *>(message->data2);
    snapshot->GetZoom(&SpEffects::zoom);
    snapshot->GetCompass(&SpEffects::rotation);
    snapshot->GetType(&SpEffects::view_id);
  }
  return true;
}

bool SpLandChange::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpLandChange::AddRef() {
  return 1;
}

u32 SpLandChange::Release() {
  return 1;
}

// Expand terrain and occupant changes by the original cell margin and notify nearby space-debris impact displays.
// Enumeration failure returns false; successful queries release every returned occupant and queried display.
bool SpLandChange::DoMessage(ScMessage *message) {
  ScRect rectangle = {-1, -1, -1, -1};
  if (message->type == kTrMsg_TerrainChanged) {
    const ScRect *changed = reinterpret_cast<const ScRect *>(message->data1);
    if (!changed)
      return true;
    rectangle = *changed;
  } else if (message->type == kOcMsg_SurfaceInserted || message->type == kOcMsg_SurfaceRemoved) {
    ScOccupant *occupant = reinterpret_cast<ScOccupant *>(message->data1);
    // A partial foreign implementation must not expose unwritten stack words to the reconstructed code.
    GeCellRect changed = {};
    occupant->GetBoundingRectInCellCoords(&changed);
    rectangle = {(i32)changed.left, (i32)changed.top, (i32)changed.right, (i32)changed.bottom};
  } else
    return true;
  if (rectangle.left < 0)
    return true;
  GeBounds bounds;
  bounds.minimum = {(i32)(((u32)rectangle.left - 5u) << kCtMicroCoordinateShift),
      (i32)(((u32)rectangle.top - 5u) << kCtMicroCoordinateShift), 0};
  bounds.maximum = {(i32)(((u32)rectangle.right + 6u) << kCtMicroCoordinateShift),
      (i32)(((u32)rectangle.bottom + 6u) << kCtMicroCoordinateShift), 0x10000};
  bounds.minimum.x = ScIntMax(bounds.minimum.x, 0);
  bounds.minimum.z = ScIntMax(bounds.minimum.z, 0);
  if (bounds.maximum.x >= SpEffects::micro_width)
    bounds.maximum.x = (i32)((u32)SpEffects::micro_width - 1u);
  if (bounds.maximum.z >= SpEffects::micro_height)
    bounds.maximum.z = (i32)((u32)SpEffects::micro_height - 1u);
  ScOccupant *occupants[128];
  u32 count = 128;
  if (!SpEffects::animated->GetOccupantsInVolume(bounds, occupants, &count))
    return false;
  for (u32 index = 0; index < count; ++index) {
    ScOccupant *occupant = occupants[index];
    void *object;
    if (occupant->QueryInterface(IID_SpImpactDisplay, &object)) {
      SpImpactDisplay *display = static_cast<SpImpactDisplay *>(object);
      display->DoTerrainChanged(rectangle);
      display->Release();
    }
    occupant->Release();
  }
  return true;
}

SpEffects::SpEffects() : references(0) {
}

SpEffects::~SpEffects() {
  if (initialized)
    ShutdownEffects();
}

// Publish the Unknown or effects-manager identity and retain it through the actual virtual AddRef operation.
// Unsupported identifiers clear the output.
bool SpEffects::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, SpEffects);
  HANDLE_QUERY_INTERFACE(IID_SpEffects, SpEffects);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 SpEffects::AddRef() {
  return ++references;
}

u32 SpEffects::Release() {
  return ScReleaseImpl(this, &references);
}

// Acquire the original shared drawing and city services in their reached order, then register both static observers.
void SpEffects::InitEffects(SpCellMap *map) {
  if (initialized)
    return;
  ApGameInterface *application = SpGetApplication();
  performance = SpGetPerformanceInfo();
  if (!cell_map)
    cell_map = ScAddRefNotNull(map);
  if (!view)
    view = ScAddRefNotNull(static_cast<SpView *>(application->GetISC3WinCityView()->GetCurrentView()));
  if (!manager)
    manager = ScAddRefNotNull(view->GetViewManager());
  if (!city)
    city = ScAddRefNotNull(SpGetCity());
  if (!dirt)
    dirt = ScAddRefNotNull(city->DirtBag());
  if (!tick_manager)
    tick_manager = ScAddRefNotNull(SpGetTickManager());
  if (!images)
    images = SpGetImageCache();
  if (!messages)
    messages = ScAddRefNotNull(SpGetMessageServer());
  if (!surface)
    surface = ScAddRefNotNull(city->SurfaceOccupantManager());
  if (!animated)
    animated = ScAddRefNotNull(city->AnimOccupantManager());
  if (!box)
    box = ScAddRefNotNull(SpGetApplication()->GetBoxX());
  if (!neighbors)
    neighbors = ScAddRefNotNull(city->NeighborsLayer());
  transparent_color = SpGetGraphics()->ConvertRGBValueToNative16Bit(255, 0, 255);
  zoom = cell_map->GetZoom();
  rotation = cell_map->GetRotate();
  micro_width = (i32)(cell_map->GetActualGridSizeX() << kCtMicroCoordinateShift);
  micro_height = (i32)(cell_map->GetActualGridSizeZ() << kCtMicroCoordinateShift);
  SpGetMessageServer()->AddNotification(&view_change, kWnMsg_ViewInfoUpdated);
  SpGetMessageServer()->AddNotification(&land_change, kTrMsg_TerrainChanged);
  SpGetMessageServer()->AddNotification(&land_change, kOcMsg_SurfaceInserted);
  SpGetMessageServer()->AddNotification(&land_change, kOcMsg_SurfaceRemoved);
  initialized = true;
}

// Disable the shared effects before removing notifications, then release each owned service in the original order.
void SpEffects::ShutdownEffects() {
  if (!initialized)
    return;
  initialized = false;
  SpGetMessageServer()->RemoveNotification(&view_change, kWnMsg_ViewInfoUpdated);
  SpGetMessageServer()->RemoveNotification(&land_change, kTrMsg_TerrainChanged);
  SpGetMessageServer()->RemoveNotification(&land_change, kOcMsg_SurfaceInserted);
  SpGetMessageServer()->RemoveNotification(&land_change, kOcMsg_SurfaceRemoved);
  ScRelease(&cell_map);
  ScRelease(&view);
  ScRelease(&manager);
  ScRelease(&dirt);
  ScRelease(&city);
  ScRelease(&tick_manager);
  ScRelease(&messages);
  ScRelease(&surface);
  ScRelease(&animated);
  ScRelease(&box);
  ScRelease(&neighbors);
}

SpWeatherSim::SpWeatherSim()
    : references(0), registered(0), clock(0), cloud_flags(0), move_clouds(1), flag26(0), level(2), lightning_enabled(0),
      cloud_limit(0), compass(0), altitude(40960), wind{0, 0, 0}, cloud_height(38400.0f), cloud_speed(0),
      precipitation_flags(0), precipitation_type(1), precipitation(nullptr), flag84(0), precipitation_cloud(-1),
      next_precipitation(0), strike_enabled(0), lightning(nullptr), next_lightning(0), last_lightning(0),
      birds_enabled(0), next_bird(UINT32_MAX), word124(0) {
}

// Publish the actual weather, animation or message identity before retaining the primary object.
bool SpWeatherSim::QueryInterface(u32 iid, void **out) {
  switch (iid) {
  case IID_ScUnknown:
  case IID_SpWeatherView:
    *out = static_cast<SpWeatherView *>(this);
    break;
  case IID_SpTickable:
    *out = static_cast<SpTickable *>(this);
    break;
  case IID_ScMessageTarget:
    *out = static_cast<ScMessageTarget *>(this);
    break;
  default:
    RETURN_QUERY_INTERFACE_FAILURE();
  }
  AddRef();
  return true;
}

u32 SpWeatherSim::AddRef() {
  return ++references;
}

// Delete through the weather destructor when the previous count is one; otherwise decrement with DWORD wrap.
// A zero starting count therefore becomes UINT32_MAX, matching the original receiver.
u32 SpWeatherSim::Release() {
  if (references == 1) {
    delete this;
    return 0;
  }
  return --references;
}

void SpWeatherSim::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 20;
  *interval = 6;
}

i32 SpWeatherSim::GetWeatherVisibility() {
  return cloud_flags & 1;
}

u8 SpWeatherSim::SetCurrentPrecipitation(i32 type) {
  precipitation_type = type;
  return 1;
}

// Apply the original three-argument visibility request, preserving its unused third word and low-byte masks.
u8 SpWeatherSim::SetVisibilityFlags(i32 cloud_mask, i32 precipitation_mask, i32) {
  u8 old_clouds = cloud_flags, old_precipitation = precipitation_flags;
  precipitation_flags |= (u8)precipitation_mask;
  cloud_flags |= (u8)cloud_mask;
  if ((cloud_flags == 15) != (old_clouds == 15))
    ShowClouds(cloud_flags == 15);
  if ((precipitation_flags == 15) != (old_precipitation == 15))
    return ShowPrecipitation(precipitation_flags == 15);
  return precipitation_flags;
}

// Remove the original visibility masks before testing either all-bits transition.
u8 SpWeatherSim::ClearVisibilityFlags(i32 cloud_mask, i32 precipitation_mask, i32) {
  u8 old_clouds = cloud_flags, old_precipitation = precipitation_flags;
  precipitation_flags &= (u8)~precipitation_mask;
  cloud_flags &= (u8)~cloud_mask;
  if ((cloud_flags == 15) != (old_clouds == 15))
    ShowClouds(cloud_flags == 15);
  if ((precipitation_flags == 15) != (old_precipitation == 15))
    return ShowPrecipitation(precipitation_flags == 15);
  return precipitation_flags;
}

u8 SpWeatherSim::ShowOrHide(u8 enabled) {
  return enabled ? SetVisibilityFlags(1, 1, 1) : ClearVisibilityFlags(1, 1, 1);
}

// Scale the configured signed weather level by the original micro-map area, then cap the low DWORD count at25.
// Unsigned intermediates preserve the installed multiplication wrap without introducing signed-overflow behavior.
u32 SpWeatherSim::CalculateAppropriateCloudCount() {
  u64 scaled = (u64)(i64(7) + level) * (u64)(i64)SpEffects::micro_height * (u64)(i64)SpEffects::micro_width;
  u32 count = (u32)(scaled >> 30);
  return UintMin(count, 25);
}

u32 SpWeatherSim::SetNewPrecipitationToggleTime(u32 time) {
  next_precipitation = (u32)SpEffects::random.Range(kSpPrecipitationMinimumDelay, kSpWeatherMaximumEventDelay) + time +
      kSpPrecipitationMinimumDelay;
  return next_precipitation;
}

u32 SpWeatherSim::SetNewLightningStrikeTime(u32 time) {
  next_lightning = time + (u32)SpEffects::random.Range(kSpLightningMinimumDelay, kSpWeatherMaximumEventDelay);
  return next_lightning;
}

// Schedule a bird flock with the original area-dependent divisor, preserving the32-bit product before signed division.
u32 SpWeatherSim::SetNewCreateBirdFlockTime(u32 time) {
  i32 delay = SpEffects::random.Range(kSpBirdFlockMinimumDelay, kSpBirdFlockMaximumDelay);
  i32 area = (i32)((u32)(SpEffects::micro_width / 256) * (u32)SpEffects::micro_height);
  i64 divisor = (i64(area / 256) - 4096) / 16 + 4096;
  assert(divisor != 0);
  next_bird = time + (u32)((i64(delay) * 4096) / divisor);
  return next_bird;
}

// Scale only the horizontal vector components to the supplied length, leaving altitude untouched.
void SpEffects::Scale2DVector(ScVec3F *vector, f32 length) {
  f64 scale = (f64)length / std::sqrt((f64)vector->x * vector->x + (f64)vector->z * vector->z);
  vector->x = (f32)(scale * vector->x);
  vector->z = (f32)(scale * vector->z);
}

f64 SpEffects::AngleOf2DVector(const ScVec3F &vector) {
  f64 angle = std::atan2((f64)vector.z, (f64)vector.x);
  return angle < 0.0 ? angle + 6.2831854820251465 : angle;
}

// Convert the wind angle into the original eight-direction compass, including its0.01 bias and out-of-range fallback.
// Finite angles express the source's defined floating-to-integer domain without adding a normal game error result.
i32 SpWeatherSim::VectorToAppropriateEdge(const ScVec3F &direction) {
  f64 value = (4.0 / 3.1415927410125732) * SpEffects::AngleOf2DVector(direction) + 0.01;
  assert(std::isfinite(value));
  i32 edge = (i32)value;
  return (u32)edge > 7 ? 0 : edge;
}

// Store cloud settings, clamp the signed level and flatten wind altitude before normalizing its horizontal speed.
// Near-zero wind selects the original westward fallback; the resulting direction determines the stored compass.
u8 SpWeatherSim::SetCloudProperties(const ScVec3F &direction, u8 move, u8 enable_lightning, i8 value) {
  lightning_enabled = enable_lightning;
  move_clouds = move;
  SetLevel(value);
  wind.x = direction.x;
  wind.y = direction.y;
  wind.z = direction.z;
  wind.y = 0.0f;
  cloud_speed = 0.04f;
  if (SpEffects::LengthOf2DVector(wind) < (f64)0.01f) {
    wind.x = -1.0f;
    wind.z = 0.0f;
  }
  SpEffects::Scale2DVector(&wind, cloud_speed);
  compass = (u16)VectorToAppropriateEdge(wind);
  return 1;
}

SpWeatherSim::~SpWeatherSim() {
  Shutdown();
}

void SpWeatherSim::Shutdown() {
  ScRelease(&lightning);
  RemoveFromAnimationSystem();
  SpGetApplication()->GetCheatCodeManager()->RemoveNotification(this, 0);
}

// Clear the registered byte before removing the original three notifications and animation-manager membership.
void SpWeatherSim::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  SpGetMessageServer()->RemoveNotification(this, kWnMsg_ViewInfoUpdated);
  SpGetMessageServer()->RemoveNotification(this, kEnMsg_WeatherChanged);
  SpGetMessageServer()->RemoveNotification(this, kWnMsg_EnterCity);
  if (SpEffects::tick_manager)
    SpEffects::tick_manager->RemoveTickableObject(this);
  DestroyAllPrecipitation();
  DestroyAllClouds();
}

SpBasicSprite::SpBasicSprite() : mode(0), shifts{}, color{}, mask{}, origins{} {
  for (u32 zoom = 0; zoom < 5; ++zoom)
    registration[zoom] = {0, 0, 1, 1};
}

SpBasicSprite::~SpBasicSprite() {
}

bool SpBasicSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpSprite, SpSprite);
  return SpEffectOccupant::QueryInterface(iid, out);
}

u32 SpBasicSprite::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpBasicSprite::Release() {
  return SpEffectOccupant::Release();
}

i32 SpBasicSprite::GetW(i32 zoom, i32) const {
  return i32(u32(registration[zoom].left) + u32(registration[zoom].right));
}

i32 SpBasicSprite::GetH(i32 zoom, i32) const {
  return i32(u32(registration[zoom].top) + u32(registration[zoom].bottom));
}

// Copy the four zoom-specific anchor distances in their original write order. Rotation does not select another image.
u8 SpBasicSprite::GetRegistration(i32 zoom, i32, ScRect *out) const {
  out->left = registration[zoom].left;
  out->top = registration[zoom].top;
  out->right = registration[zoom].right;
  out->bottom = registration[zoom].bottom;
  return 1;
}

// Expand an anchor into its destination rectangle. Compute the original temporary extents before publishing outputs.
u8 SpBasicSprite::GetAreaFromPt(i32 zoom, i32, ScRect *out, i32 x, i32 y) const {
  const ScRect &rect = registration[zoom];
  i32 bottom = i32(u32(y) + u32(rect.bottom)), right = i32(u32(x) + u32(rect.right));
  i32 left = i32(u32(x) - u32(rect.left));
  out->top = i32(u32(y) - u32(rect.top));
  out->left = left;
  out->right = right;
  out->bottom = bottom;
  return 1;
}

void SpBasicSprite::GetRotationOffset(u8 *) const {
}

u32 SpBasicSprite::GetLayerFlags() const {
  return 0;
}

void SpBasicSprite::SetRotationOffset(u8) {
}

u8 SpBasicSprite::SetEffect(u32) {
  return 0;
}

void SpBasicSprite::SetLayer(u8) {
}

bool SpBasicSprite::IsPixelOpaque(i32 x, i32 y, i32 zoom, i32) const {
  GxBuffer *buffer = color[zoom] ? color[zoom] : mask[zoom];
  if (!buffer || !buffer->Lock(GxLockFlags::GetSetPixel))
    return false;
  u32 shift = shifts[zoom] & 31;
  u32 pixel = buffer->GetPixel(u32(x >> shift) + u32(origins[zoom].x), u32(y >> shift) + u32(origins[zoom].y));
  bool opaque = pixel != SpEffects::transparent_color;
  buffer->Unlock(GxLockFlags::GetSetPixel);
  return opaque;
}

u8 SpBasicSprite::DemandArt(i32, i32, bool) {
  return 1;
}

i32 SpBasicSprite::ReleaseArt(i32, i32) {
  return 1;
}

// Draw into the supplied rectangle using the selected zoom's original blend mode. Its source origin stays unscaled.
u8 SpBasicSprite::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32, ScRect *clip) const {
  const ScRect &reg = registration[zoom];
  u8 scale = shifts[zoom];
  u32 shift = scale & 31;
  i32 width = i32(u32(reg.left) + u32(reg.right)) >> shift;
  i32 height = i32(u32(reg.top) + u32(reg.bottom)) >> shift;
  ScRect source = {origins[zoom].x, 0, i32(u32(origins[zoom].x) + u32(width)), height};
  GxBuffer *image = color[zoom];
  if (!image && !mask[zoom])
    return 1;
  void *object = nullptr;
  switch (mode) {
  case 0:
    if (image) {
      if (shifts[zoom])
        image->BltStretchSrc(target, source, rect, clip);
      else
        image->BltSrc(target, source, rect, clip);
    }
    break;
  case 1:
    if (image->QueryInterface(IID_GxImageBuilder, &object) && !shifts[zoom])
      static_cast<GxImageBuilder *>(object)->SpanBufferAlphaBltSelfAlpha(
          target, source, ScPoint{rect.left, rect.top}, clip);
    break;
  case 2:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      if (shifts[zoom])
        span->SpanBufferAlphaBltStretch(target, mask[zoom], source, rect, clip);
      else
        span->SpanBufferAlphaBlt(target, mask[zoom], source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 3: {
    if (shifts[zoom])
      return 1;
    GxBlendService *blend = SpGetBlendService();
    blend->ShadowBlitConstAlpha(target, image, source, ScPoint{rect.left, rect.top}, clip);
    break;
  }
  case 4: {
    GxBlendService *blend = SpGetBlendService();
    if (scale)
      blend->ShadeBltStretch(target, mask[zoom], source, rect, clip);
    else
      blend->ShadeBlt(target, mask[zoom], source, ScPoint{rect.left, rect.top}, clip);
    break;
  }
  case 5:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      f32 opacity;
      memcpy(&opacity, &effect_value, sizeof(opacity));
      if (shifts[zoom])
        span->SpanBufferAlphaBltConstAlphaStretch(target, opacity, source, rect, clip);
      else
        span->SpanBufferAlphaBltConstAlpha(target, opacity, source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 6:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      f32 opacity;
      memcpy(&opacity, &effect_value, sizeof(opacity));
      if (shifts[zoom])
        span->SpanBufferAlphaBltWithAdditionalAlphaFactorStretch(target, mask[zoom], opacity, source, rect, clip);
      else
        span->SpanBufferAlphaBltWithAdditionalAlphaFactor(
            target, mask[zoom], opacity, source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 7:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      if (shifts[zoom])
        span->SpanBufferHighlightBltStretch(target, effect_value, source, rect, clip);
      else
        span->SpanBufferHighlightBlt(target, effect_value, source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  default:
    return 1;
  }
  if (object)
    static_cast<GxImageBuilder *>(object)->Release();
  return 1;
}

// Build the destination from an anchor and draw the selected image. This overload scales the source origin and adds mode8.
u8 SpBasicSprite::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32, ScRect *clip) const {
  const ScRect &reg = registration[zoom];
  u8 scale = shifts[zoom];
  u32 shift = scale & 31;
  i32 width = i32(u32(reg.left) + u32(reg.right)) >> shift;
  i32 height = i32(u32(reg.top) + u32(reg.bottom)) >> shift;
  i32 origin = origins[zoom].x >> shift;
  ScRect source = {origin, 0, i32(u32(origin) + u32(width)), height};
  ScRect rect = {i32(u32(x) - u32(reg.left)), i32(u32(y) - u32(reg.top)), i32(u32(x) + u32(reg.right)),
      i32(u32(y) + u32(reg.bottom))};
  GxBuffer *image = color[zoom];
  if (!image && !mask[zoom])
    return 1;
  void *object = nullptr;
  switch (mode) {
  case 0:
    if (image) {
      if (shifts[zoom])
        image->BltStretchSrc(target, source, rect, clip);
      else
        image->BltSrc(target, source, rect, clip);
    }
    break;
  case 1:
    if (shifts[zoom])
      return 1;
    if (image->QueryInterface(IID_GxImageBuilder, &object))
      static_cast<GxImageBuilder *>(object)->SpanBufferAlphaBltSelfAlpha(
          target, source, ScPoint{rect.left, rect.top}, clip);
    break;
  case 2:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      if (shifts[zoom])
        span->SpanBufferAlphaBltStretch(target, mask[zoom], source, rect, clip);
      else
        span->SpanBufferAlphaBlt(target, mask[zoom], source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 3: {
    if (shifts[zoom])
      return 1;
    GxBlendService *blend = SpGetBlendService();
    blend->ShadowBlitConstAlpha(target, image, source, ScPoint{rect.left, rect.top}, clip);
    break;
  }
  case 4: {
    GxBlendService *blend = SpGetBlendService();
    if (scale)
      blend->ShadeBltStretch(target, mask[zoom], source, rect, clip);
    else
      blend->ShadeBlt(target, mask[zoom], source, ScPoint{rect.left, rect.top}, clip);
    break;
  }
  case 5:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      f32 opacity;
      memcpy(&opacity, &effect_value, sizeof(opacity));
      if (shifts[zoom])
        span->SpanBufferAlphaBltConstAlphaStretch(target, opacity, source, rect, clip);
      else
        span->SpanBufferAlphaBltConstAlpha(target, opacity, source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 6:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      f32 opacity;
      memcpy(&opacity, &effect_value, sizeof(opacity));
      if (shifts[zoom])
        span->SpanBufferAlphaBltWithAdditionalAlphaFactorStretch(target, mask[zoom], opacity, source, rect, clip);
      else
        span->SpanBufferAlphaBltWithAdditionalAlphaFactor(
            target, mask[zoom], opacity, source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 7:
    if (image->QueryInterface(IID_GxImageBuilder, &object)) {
      GxImageBuilder *span = static_cast<GxImageBuilder *>(object);
      if (shifts[zoom])
        span->SpanBufferHighlightBltStretch(target, effect_value, source, rect, clip);
      else
        span->SpanBufferHighlightBlt(target, effect_value, source, ScPoint{rect.left, rect.top}, clip);
    }
    break;
  case 8: {
    GxBlendService *blend = SpGetBlendService();
    f32 opacity;
    memcpy(&opacity, &effect_value, sizeof(opacity));
    if (scale)
      blend->ShadeBltStretchWithAdditionalAlpha(target, mask[zoom], opacity, source, rect, clip);
    else
      blend->ShadeBltWithAdditionalAlpha(target, mask[zoom], opacity, source, ScPoint{rect.left, rect.top}, clip);
    break;
  }
  default:
    return 1;
  }
  if (object)
    static_cast<GxImageBuilder *>(object)->Release();
  return 1;
}

i32 SpBasicSprite::Tick(u32) {
  return 0;
}

bool SpBasicSprite::SetDefinition(SpDefinition *) {
  return true;
}

u8 SpBasicSprite::HasGeomFlag(u32 bits) {
  return (geom_flags & bits) == bits;
}

u8 SpBasicSprite::SetGeomFlag(u32 bits) {
  geom_flags |= bits;
  return 1;
}

u8 SpBasicSprite::ClearGeomFlag(u32 bits) {
  geom_flags &= ~bits;
  return 1;
}

u32 SpBasicSprite::GetGeomFlag() {
  return geom_flags;
}

void SpBasicSprite::SetNext(SpSprite *) {
}

// Return the original absent chain link. Concrete compound effects manage their sprites separately.
SpSprite *SpBasicSprite::GetNext() {
  return nullptr;
}

// Recover the primary sprite identity from its occupant view. The result is borrowed and does not change references.
SpSprite *SpBasicSprite::GetSpriteInst() {
  return static_cast<SpSprite *>(this);
}

SpCloud::SpCloud(i32 value) : variant(value), elapsed(0), step_time(kSpCloudInitialStepTime), shadow(nullptr) {
  mode = 2;
  SetSpanRects();
}

u32 SpCloud::GetSpriteKey(i32 zoom, i32 variant, i32 base) {
  return u32(base) + u32(zoom) + 5 * u32(variant);
}

// Populate each registration rectangle from existing color metadata. Missing metadata leaves its constructor extents.
void SpCloud::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, variant, -kSpCloudColorKeyOffset), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Acquire low-zoom cloud color and alpha images, then the optional shadow mask. Higher zooms clear both extents.
u8 SpCloud::DemandArt(i32 zoom, i32, bool) {
  if (zoom > 1) {
    registration[zoom] = {};
    if (shadow)
      shadow->registration[zoom] = {};
    return 1;
  }
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, -kSpCloudColorKeyOffset), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, -kSpCloudMaskKeyOffset), 0, &image);
  mask[zoom] = image->buffer;
  if (shadow) {
    SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, -kSpCloudShadowMaskKeyOffset), 0, &image);
    shadow->mask[zoom] = image->buffer;
    shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  return color[zoom] != nullptr;
}

// Release only acquired low-zoom demands through the live cache. Shadow ownership remains with the simulator.
i32 SpCloud::ReleaseArt(i32 zoom, i32) {
  if (zoom <= 1 && SpEffects::images) {
    if (color[zoom]) {
      SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, -kSpCloudColorKeyOffset), 0);
      color[zoom] = nullptr;
    }
    if (mask[zoom]) {
      SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, -kSpCloudMaskKeyOffset), 0);
      mask[zoom] = nullptr;
    }
    if (shadow && shadow->mask[zoom]) {
      SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, -kSpCloudShadowMaskKeyOffset), 0);
      shadow->mask[zoom] = nullptr;
    }
  }
  return 0;
}

u8 SpCloud::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  return zoom <= 1 ? SpBasicSprite::Draw(target, rect, zoom, rotation, clip) : 1;
}

u8 SpCloud::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  return zoom <= 1 ? SpBasicSprite::Draw(target, x, y, zoom, rotation, clip) : 1;
}

SpPrecipitation::SpPrecipitation() : kind(0), phase(0), elapsed(0), step_time(kSpPrecipitationInitialStepTime) {
  mode = 2;
  SetSpanRects();
}

u32 SpPrecipitation::GetSpriteKey(i32 zoom, i32 variant, i32 base) {
  return u32(base) + u32(zoom) + 5 * u32(variant);
}

// Fill per-zoom extents from available precipitation color metadata. Missing entries preserve the existing rectangle.
void SpPrecipitation::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, kind, -kSpPrecipitationColorKeyOffset), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Demand the precipitation's low-zoom color and mask separately. At higher zooms only the registration is cleared.
u8 SpPrecipitation::DemandArt(i32 zoom, i32, bool) {
  if (zoom > 1) {
    registration[zoom] = {};
    return 1;
  }
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, kind, -kSpPrecipitationColorKeyOffset), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, kind, -kSpPrecipitationMaskKeyOffset), 0, &image);
  mask[zoom] = image->buffer;
  return color[zoom] != nullptr;
}

// Retire acquired low-zoom images and clear their borrowed pointers. The original full DWORD result is zero.
i32 SpPrecipitation::ReleaseArt(i32 zoom, i32) {
  if (zoom <= 1 && SpEffects::images) {
    if (color[zoom]) {
      SpEffects::images->UnprepareImage(GetSpriteKey(zoom, kind, -kSpPrecipitationColorKeyOffset), 0);
      color[zoom] = nullptr;
    }
    if (mask[zoom]) {
      SpEffects::images->UnprepareImage(GetSpriteKey(zoom, kind, -kSpPrecipitationMaskKeyOffset), 0);
      mask[zoom] = nullptr;
    }
  }
  return 0;
}

u8 SpPrecipitation::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  return zoom <= 1 ? SpBasicSprite::Draw(target, rect, zoom, rotation, clip) : 1;
}

u8 SpPrecipitation::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  return zoom <= 1 ? SpBasicSprite::Draw(target, x, y, zoom, rotation, clip) : 1;
}

// Remove the cloud and shadow occupants in their original order before dropping references. Clear the live array last.
void SpWeatherSim::DestroyAllClouds() {
  u32 count = clouds.size;
  for (u32 index = 0; index < count; ++index) {
    if (SpEffects::animated) {
      SpCloud *cloud = clouds.data[index];
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(cloud), SpEffectCoordinate(cloud->position.x),
          SpEffectCoordinate(cloud->position.z), SpEffectCoordinate(cloud->position.y));
    }
    if (clouds.data[index]->shadow) {
      if (SpEffects::animated) {
        SpBasicSprite *shadow = clouds.data[index]->shadow;
        SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(shadow),
            SpEffectCoordinate(shadow->position.x), SpEffectCoordinate(shadow->position.z),
            SpEffectCoordinate(shadow->position.y));
      }
      clouds.data[index]->shadow->Release();
      clouds.data[index]->shadow = nullptr;
    }
    clouds.data[index]->Release();
    clouds.data[index] = nullptr;
  }
  clouds.size = 0;
  DestroyAllPrecipitation();
}

// Remove existing precipitation before releasing its sprite. The cloud selector resets only when an object existed.
u32 SpWeatherSim::DestroyAllPrecipitation() {
  if (!precipitation)
    return 0;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(precipitation),
        SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
        SpEffectCoordinate(precipitation->position.y));
  u32 count = precipitation->Release();
  precipitation = nullptr;
  precipitation_cloud = -1;
  return count;
}

// Offset an integer shadow toward the current view's light and clamp it to the map.
static void SpOffsetShadow(i32 *x, i32 *z) {
  if (SpEffects::rotation == 0 || SpEffects::rotation == 1)
    *x = i32(u32(*x) - 5);
  else
    *x = i32(u32(*x) + 5);
  if (SpEffects::rotation == 1 || SpEffects::rotation == 2)
    *z = i32(u32(*z) + 5);
  else
    *z = i32(u32(*z) - 5);
  if (*x < 0)
    *x = 0;
  else if (*x >= SpEffects::micro_width)
    *x = SpEffects::micro_width - 1;
  if (*z < 0)
    *z = 0;
  else if (*z >= SpEffects::micro_height)
    *z = SpEffects::micro_height - 1;
}

// Truncate the original float coordinates before the integer light offset.
static void SpOffsetShadow(f32 *x, f32 *z) {
  i32 px = i32(i64(*x)), pz = i32(i64(*z));
  SpOffsetShadow(&px, &pz);
  *x = f32(px);
  *z = f32(pz);
}

// An empty cloud list leaves outputs alone.
u8 SpWeatherSim::GetPrecipitationPositionForCloud(u32 index, ScVec3F *out) {
  if (!clouds.size)
    return 0;
  if (index >= clouds.size)
    index = 0;
  *out = clouds.data[index]->position;
  out->y += 10240.0f;
  SpOffsetShadow(&out->x, &out->z);
  return 1;
}

SpLightning::SpLightning() : registered(0), word204(0) {
  mode = 4;
  position.y = f32(kSpOffWorldAnimationAltitude);
}

// Insert the hidden lightning occupant once, publishing its registration flag before the animation callback.
void SpLightning::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  position.y = f32(kSpOffWorldAnimationAltitude);
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), kSpOffWorldAnimationAltitude, 1, 0, true);
}

// Clear registration before removing the occupant at its current position. Reset the hidden anchor afterward.
void SpLightning::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
  position.y = f32(kSpOffWorldAnimationAltitude);
  position.x = position.z = 0.0f;
}

// Move lightning from its current altitude to the original hidden sentinel, then publish that height locally.
void SpLightning::HideFromView() {
  i32 x = SpEffectCoordinate(position.x), z = SpEffectCoordinate(position.z), y = SpEffectCoordinate(position.y);
  SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, x, z, kSpOffWorldAnimationAltitude);
  position.y = f32(kSpOffWorldAnimationAltitude);
}

// Move the animation-layer occupant before copying the live input point. Callback changes to aliased input remain
// visible.
void SpLightning::MoveToPosition(const ScVec3F &point) {
  i32 x = SpEffectCoordinate(position.x), z = SpEffectCoordinate(position.z), y = SpEffectCoordinate(position.y);
  i32 next_x = SpEffectCoordinate(point.x), next_z = SpEffectCoordinate(point.z), next_y = SpEffectCoordinate(point.y);
  SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, next_x, next_z, next_y);
  position = point;
}

// Demand the low-zoom lightning mask and publish its registration. Higher zooms retain no visible rectangle.
u8 SpLightning::DemandArt(i32 zoom, i32, bool) {
  if (zoom > 1) {
    registration[zoom] = {};
    return 1;
  }
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(u32(zoom) - kSpLightningImageKeyOffset, 0, &image);
  mask[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  return 1;
}

// Release the original low-zoom cache demand even when the local mask pointer is null. Clear the borrowed pointer afterward.
i32 SpLightning::ReleaseArt(i32 zoom, i32) {
  if (zoom <= 1 && SpEffects::images) {
    SpEffects::images->UnprepareImage(u32(zoom) - kSpLightningImageKeyOffset, 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

u8 SpLightning::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  return zoom <= 1 ? SpBasicSprite::Draw(target, rect, zoom, rotation, clip) : 1;
}

u8 SpLightning::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  return zoom <= 1 ? SpBasicSprite::Draw(target, x, y, zoom, rotation, clip) : 1;
}

// Initialize cloud settings and the real lightning object before registering animation and cheat notifications.
// A missing animation layer is the original ordinary failure; allocation follows the global new-handler policy.
u8 SpWeatherSim::Init() {
  if (!SpEffects::animated)
    return 0;
  ScVec3F direction = {0.0f, 0.0f, 0.0f};
  cloud_limit = u16(CalculateAppropriateCloudCount());
  SetCloudProperties(direction, 1, 0, -1);
  lightning = ScAddRefNotNull(new SpLightning);
  AddToAnimationSystem();
  SetupInitialWeather();
  ScCheatCodeManager *cheats = SpGetApplication()->GetCheatCodeManager();
  {
    ScString name(" ");
    cheats->RegisterCheatCode(kSpCheat_Birds, name);
  }
  cheats->AddNotification(this, 0);
  birds_enabled = cheats->GetCheatHasBeenSet(kSpCheat_Birds);
  return 1;
}

// Register the three original weather messages and reschedule enabled deadlines before adding the tick receiver.
void SpWeatherSim::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  SpGetMessageServer()->AddNotification(this, kWnMsg_ViewInfoUpdated);
  SpGetMessageServer()->AddNotification(this, kEnMsg_WeatherChanged);
  SpGetMessageServer()->AddNotification(this, kWnMsg_EnterCity);
  if (next_precipitation && next_bird != UINT32_MAX)
    SetNewPrecipitationToggleTime(clock);
  if (next_lightning && next_bird != UINT32_MAX)
    SetNewLightningStrikeTime(clock);
  if (next_bird && next_bird != UINT32_MAX)
    SetNewCreateBirdFlockTime(clock);
  SpEffects::tick_manager->AddTickableObject(this);
}

// Query current weather before applying the original visibility masks, then gate effects by the current zoom.
void SpWeatherSim::SetupInitialWeather() {
  EnWeatherLayer::EnWeatherType type;
  i32 detail;
  SpGetCity()->WeatherLayer()->GetWeather(&type, &detail, nullptr);
  if (type)
    SetVisibilityFlags(2, 2, 2);
  else
    ClearVisibilityFlags(2, 2, 2);
  SetVisibilityFlags(8, 8, 0);
  if (SpEffects::zoom > 1)
    ClearVisibilityFlags(4, 4, 4);
  else
    SetVisibilityFlags(4, 4, 4);
}

// Truncate animation-space coordinates to terrain vertices, then widen the returned altitude to animation units.
// Only the genuine output byte is read; no fallback altitude or additional failure result is manufactured.
i32 SpEffects::GetAltitudeOfTerrainInAnimUnits(f32 x, f32 z) {
  u8 altitude;
  dirt->GetVertexAltitude(u32(i64(x) / 256), u32(i64(z) / 256), &altitude);
  return i32(altitude) << kCtMicroCoordinateShift;
}

// Attach or move precipitation to the selected cloud, preserving separate old/new animation coordinates.
// The first type operand is unused by the installed body; its ordinary gate reads the stored precipitation type.
u8 SpWeatherSim::AttachPrecipitationToCloud(i32, u32 index, u32 time) {
  if (!precipitation_type || !clouds.size) {
    if (precipitation) {
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(precipitation),
          SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
          SpEffectCoordinate(precipitation->position.y));
      precipitation->Release();
      precipitation = nullptr;
    }
    return 0;
  }
  if (index >= clouds.size)
    index = 0;
  if (!precipitation) {
    precipitation = ScAddRefNotNull(new SpPrecipitation);
    precipitation_cloud = -1;
  }
  if (precipitation_cloud == -1) {
    GetPrecipitationPositionForCloud(index, &precipitation->position);
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(precipitation),
        SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
        SpEffectCoordinate(precipitation->position.y), 1, 0, true);
  } else {
    i32 x = SpEffectCoordinate(precipitation->position.x), z = SpEffectCoordinate(precipitation->position.z);
    i32 y = SpEffectCoordinate(precipitation->position.y);
    GetPrecipitationPositionForCloud(index, &precipitation->position);
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(precipitation), x, z, y,
        SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
        SpEffectCoordinate(precipitation->position.y));
  }
  precipitation_cloud = i32(index);
  precipitation_since = time;
  return 1;
}

// Select an interior or incoming-edge cloud position using the original random-call order and compass choices.
void SpWeatherSim::PickRandomSeedPositionForCloud(ScVec3F *point, u8 edge) {
  if (!edge) {
    point->x = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, SpEffects::micro_width - 256));
    point->z = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, SpEffects::micro_height - 256));
  } else {
    i32 side = compass;
    if (side == 1)
      side = SpEffects::random.Bounded(2) ? 0 : 2;
    else if (side == 3)
      side = SpEffects::random.Bounded(2) ? 4 : 2;
    else if (side == 5)
      side = SpEffects::random.Bounded(2) ? 4 : 6;
    else if (side == 7)
      side = SpEffects::random.Bounded(2) ? 0 : 6;
    switch (side) {
    case 0:
      point->x = 0.0f;
      point->z = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, SpEffects::micro_height - 256));
      break;
    case 2:
      point->x = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, SpEffects::micro_width - 256));
      point->z = f32(SpEffects::micro_height - 1);
      break;
    case 4:
      point->x = f32(SpEffects::micro_width - 1);
      point->z = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, SpEffects::micro_height - 256));
      break;
    case 6:
      point->x = f32(SpEffects::random.RandomDoubleRangeUniform(256.0, SpEffects::micro_width - 256));
      point->z = 0.0f;
      break;
    }
  }
  point->y = f32(SpEffects::random.RandomDoubleRangeUniform(f64(cloud_height) - 1280.0, f64(cloud_height) + 1280.0));
}

bool SpLightning::IsInVisiblePosition() {
  return position.y < f32(kSpVisibleAnimationAltitudeLimit);
}

u8 SpWeatherSim::SetLevel(i8 value) {
  level = i8(ScClampInt(value, 0, 3));
  return 1;
}

f64 SpEffects::LengthOf2DVector(const ScVec3F &vector) {
  return std::sqrt(f64(vector.x) * vector.x + f64(vector.z) * vector.z);
}

i32 SpEffects::Position3DIsIn2DBounds(const ScVec3F &point) {
  return u32(SpEffectCoordinate(point.x)) < u32(micro_width) && u32(SpEffectCoordinate(point.z)) < u32(micro_height);
}

void SpWeatherSim::WrapPositionToOtherSideOfMap(ScVec3F *point) {
  PickRandomSeedPositionForCloud(point, 1);
}

// Choose a nearby lightning strike while preserving the cloud altitude.
void SpWeatherSim::GetNewLightningStrikePosition(const ScVec3F &origin, ScVec3F *out) {
  *out = origin;
  f32 min_z, max_z;
  if (SpEffects::rotation == 2) {
    min_z = f32(f64(origin.z) + 10.0);
    max_z = f32(f64(origin.z) + 768.0);
  } else {
    min_z = f32(f64(origin.z) - 768.0);
    max_z = f32(f64(origin.z) - 10.0);
  }
  out->x = f32(SpEffects::random.RandomDoubleRangeUniform(f64(origin.x) + 10.0, f64(origin.x) + 768.0));
  if (SpEffectCoordinate(out->x) <= 0)
    out->x = 1.0f;
  if (SpEffectCoordinate(out->x) >= SpEffects::micro_width - 1)
    out->x = f32(SpEffects::micro_width - 2);
  out->z = f32(SpEffects::random.RandomDoubleRangeUniform(min_z, max_z));
  if (SpEffectCoordinate(out->z) <= 0)
    out->z = 1.0f;
  if (SpEffectCoordinate(out->z) >= SpEffects::micro_height - 1)
    out->z = f32(SpEffects::micro_height - 2);
}

static i32 SpWeatherSnapshotZoom = -1, SpWeatherSnapshotRotation = -1;

// Handle view/weather/cheat notifications through the real message identity. View rotation relocates precipitation
// after querying the shared snapshot, retaining the original cloud-index and random-stream ordering.
bool SpWeatherSim::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ViewInfoUpdated) {
    if (u32(message->data1) == kSpViewUpdate_Snapshot) {
      auto snapshot = reinterpret_cast<SpIsoViewInfo *>(message->data2);
      i32 zoom, rotation;
      snapshot->GetZoom(&zoom);
      if (zoom != SpWeatherSnapshotZoom) {
        SpWeatherSnapshotZoom = zoom;
        if (zoom > 1)
          ClearVisibilityFlags(4, 4, 4);
        else
          SetVisibilityFlags(4, 4, 4);
      }
      snapshot->GetCompass(&rotation);
      if (rotation != SpWeatherSnapshotRotation) {
        SpWeatherSnapshotRotation = rotation;
        if (zoom <= 1 && precipitation_cloud != -1) {
          if (precipitation) {
            i32 x = SpEffectCoordinate(precipitation->position.x), z = SpEffectCoordinate(precipitation->position.z);
            i32 y = SpEffectCoordinate(precipitation->position.y);
            GetPrecipitationPositionForCloud(u32(precipitation_cloud), &precipitation->position);
            SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(precipitation), x, z, y,
                SpEffectCoordinate(precipitation->position.x), SpEffectCoordinate(precipitation->position.z),
                SpEffectCoordinate(precipitation->position.y));
          } else {
            precipitation_cloud = -1;
          }
        }
      }
    }
  } else if (message->type == kEnMsg_WeatherChanged) {
    if (u32(message->data2)) {
      if (!SpEffects::random.Bounded(3)) {
        SetVisibilityFlags(2, 2, 2);
        strike_enabled = SpEffects::random.Bounded(3) == 0;
        if (strike_enabled)
          SetNewLightningStrikeTime(clock);
      }
    } else {
      ClearVisibilityFlags(2, 2, 2);
      strike_enabled = 0;
    }
  } else if (message->type == kWnMsg_EnterCity) {
    SetNewCreateBirdFlockTime(clock);
  } else if (message->type == kWnMsg_CheatCommand && u32(message->data1) == kSpCheat_Birds) {
    ScCheatCodeManager *cheats = SpGetApplication()->GetCheatCodeManager();
    birds_enabled = !cheats->GetCheatHasBeenSet(kSpCheat_Birds);
    cheats->SetCheatHasBeenSet(kSpCheat_Birds, birds_enabled != 0);
  }
  return true;
}

// Create the requested initial clouds and optional shadows before inserting their actual occupant identities.
void SpWeatherSim::GenerateInitialClouds(u8 fill_all, u8 insert) {
  if (cloud_flags && cloud_limit) {
    cloud_height = f32(f64(SpEffects::cell_map->GetHighestTerrainAltitude(true)) + altitude);
    u32 count = cloud_limit;
    clouds.Reserve(count);
    while (clouds.size < count)
      *clouds.Append() = nullptr;
    if (clouds.size > count)
      clouds.size = count;
    if (fill_all) {
      for (u32 index = 0; index < cloud_limit; ++index) {
        if (!clouds.data[index]) {
          i32 variant = i32(SpEffects::random.Bounded(8));
          clouds.data[index] = new SpCloud(variant);
          clouds.data[index]->AddRef();
          PickRandomSeedPositionForCloud(&clouds.data[index]->position, 0);
          f32 speed = f32(SpEffects::random.RandomDoubleRangeGaussianFast(
              f64(cloud_speed) * f64(0.9f), f64(cloud_speed) * f64(1.1f)));
          clouds.data[index]->step_time = u32(i64(128000.0 / speed));
          clouds.data[index]->velocity = wind;
          SpEffects::Scale2DVector(&clouds.data[index]->velocity, speed);
          if (move_clouds) {
            clouds.data[index]->shadow = new SpBasicSprite;
            clouds.data[index]->shadow->AddRef();
            clouds.data[index]->shadow->position.x = f32(SpEffectCoordinate(clouds.data[index]->position.x));
            clouds.data[index]->shadow->position.z = f32(SpEffectCoordinate(clouds.data[index]->position.z));
            clouds.data[index]->shadow->position.y = f32(SpEffects::GetAltitudeOfTerrainInAnimUnits(
                clouds.data[index]->position.x, clouds.data[index]->position.z));
            clouds.data[index]->shadow->mode = 4;
          }
        }
      }
    } else if (!clouds.size) {
      i32 variant = i32(SpEffects::random.Bounded(8));
      clouds.data[0] = new SpCloud(variant);
      clouds.data[0]->AddRef();
      PickRandomSeedPositionForCloud(&clouds.data[0]->position, 1);
      f32 speed = f32(
          SpEffects::random.RandomDoubleRangeGaussianFast(f64(cloud_speed) * f64(0.9f), f64(cloud_speed) * f64(1.1f)));
      clouds.data[0]->velocity = wind;
      SpEffects::Scale2DVector(&clouds.data[0]->velocity, speed);
    }
  }
  if (insert) {
    u32 count = clouds.size;
    for (u32 index = 0; index < count; ++index) {
      SpCloud *cloud = clouds.data[index];
      SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(cloud), SpEffectCoordinate(cloud->position.x),
          SpEffectCoordinate(cloud->position.z), SpEffectCoordinate(cloud->position.y), 1, 0, true);
      if (clouds.data[index]->shadow) {
        SpBasicSprite *shadow = clouds.data[index]->shadow;
        SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(shadow),
            SpEffectCoordinate(shadow->position.x), SpEffectCoordinate(shadow->position.z),
            SpEffectCoordinate(shadow->position.y), 1, 0, true);
      }
    }
  }
}

// Publish or destroy clouds before updating lightning membership and the cell map's alpha-blend count.
void SpWeatherSim::ShowClouds(u8 enabled) {
  if (enabled) {
    if (!clouds.size) {
      GenerateInitialClouds(1, 1);
    } else {
      u32 count = clouds.size;
      for (u32 index = 0; index < count; ++index) {
        SpCloud *cloud = clouds.data[index];
        SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(cloud),
            SpEffectCoordinate(cloud->position.x), SpEffectCoordinate(cloud->position.z),
            SpEffectCoordinate(cloud->position.y), 1, 0, true);
      }
    }
    lightning->AddToAnimationSystem();
    if (!flag26) {
      flag26 = 1;
      SpEffects::cell_map->IncrementAlphaBlendCount(1);
    }
  } else {
    DestroyAllClouds();
    lightning->RemoveFromAnimationSystem();
    if (flag26) {
      flag26 = 0;
      SpEffects::cell_map->IncrementAlphaBlendCount(-1);
    }
  }
}

// Initialize the controller, sprite and occupant views with the default splash resource pair and eight frames.
// The original high geometry flag marks the containing animation occupant.
SpSplash::SpSplash()
    : color_base(kSpSplashColorImageBase), mask_base(kSpSplashMaskImageBase), variant(0), registered(0), frame(0),
      frame_count(8), elapsed(0), frame_elapsed(0) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpSplash::~SpSplash() {
  RemoveFromAnimationSystem();
}

bool SpSplash::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpWaterSplash, SpWaterSplash);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpSplash::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpSplash::Release() {
  return SpEffectOccupant::Release();
}

// Bind a splash inside the animation world, applying only nonzero resource/frame overrides.
// Reinitialization resets the resource variant but retains the existing elapsed and horizontal frame state.
u8 SpSplash::Init(i32 x, i32 z, i32 altitude, i32 first, i32 last, i8 frames) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  variant = 0;
  position.x = f32(x);
  mode = 2;
  position.z = f32(z);
  position.y = f32(altitude);
  if (first)
    color_base = u32(first);
  if (last)
    mask_base = u32(last);
  if (frames)
    frame_count = u8(frames);
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

u32 SpSplash::GetSpriteKey(i32 variant, i32 zoom, i32 base) {
  return u32(base) + u32(zoom) + 5 * u32(variant);
}

// Populate each zoom's registration extents from the selected color metadata.
void SpSplash::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(variant, zoom, i32(color_base)), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Acquire the actual color and mask demands and copy color registration before the second acquisition.
// The original calls consume their produced records directly rather than inventing a new error result.
u8 SpSplash::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(variant, zoom, i32(color_base)), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(GetSpriteKey(variant, zoom, i32(mask_base)), 0, &image);
  mask[zoom] = image->buffer;
  return 1;
}

// Release only held splash image demands, clearing each pointer after its cache callback.
// The installed method returns a full zero word even if no cache exists.
i32 SpSplash::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(variant, zoom, i32(color_base)), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(variant, zoom, i32(mask_base)), 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

// Publish splash membership before inserting the actual occupant into animation group two.
void SpSplash::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 2, 0, true);
  frame_elapsed = 0;
}

// Clear splash membership before invoking the original layer removal callback.
// Do not access the object afterward because removal can release its last reference.
void SpSplash::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
}

// Advance at most one horizontal frame after the original strict deadline, then refresh group two or retire.
// The elapsed/deadline arithmetic wraps as DWORDs and the frame counter remains a byte.
i32 SpSplash::Tick(u32 delta) {
  elapsed += delta;
  if (elapsed > frame_elapsed + kSpSplashFrameInterval) {
    frame_elapsed = elapsed;
    ++frame;
    for (i32 zoom = 0; zoom < 5; ++zoom)
      origins[zoom].x = i32(u32(frame) * u32(GetW(zoom, 0)));
    if (frame < frame_count)
      SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
          SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 2, nullptr);
    else
      RemoveFromAnimationSystem();
  }
  return 0;
}

// Initialize trailing smoke's two identities, frame state and original public interval word.
// The resource variant remains unwritten until Init supplies it.
SpSmoke::SpSmoke()
    : elapsed(0), frame_elapsed(0), word216(0), registered(0), frame(0), frame_count(8),
      frame_interval(kSpSmokeFrameInterval) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpSmoke::~SpSmoke() {
  RemoveFromAnimationSystem();
}

bool SpSmoke::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpSmokeView, SpSmokeView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpSmoke::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpSmoke::Release() {
  return SpEffectOccupant::Release();
}

u32 SpSmoke::SetTimeBetweenFrames(u32 duration) {
  frame_interval = duration;
  return duration;
}

// Initialize smoke inside the actual animation world and bind its resource variant.
// Existing frame and elapsed state survive repeated initialization as in the original body.
u8 SpSmoke::Init(i32 x, i32 z, i32 altitude, i32 value) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  mode = 2;
  variant = value;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

u32 SpSmoke::GetSpriteKey(i32 zoom, i32 variant, u32 base) {
  return base + u32(zoom) + 5 * u32(variant);
}

// Load color registration extents for all five zoom levels without demanding their pixels.
// Missing metadata preserves prior extents and does not become a synthetic game failure.
void SpSmoke::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(zoom, variant, kSpSmokeColorImageBase), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Demand the original smoke color and alpha resource pair for the selected zoom and variant.
// Registration comes from the color record before the mask record replaces the temporary pointer.
u8 SpSmoke::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, kSpSmokeColorImageBase), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, variant, kSpSmokeMaskImageBase), 0, &image);
  mask[zoom] = image->buffer;
  return 1;
}

// Release held smoke color and mask demands with callback-before-clear ordering.
// The installed ordinary return is a full zero word.
i32 SpSmoke::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, kSpSmokeColorImageBase), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, variant, kSpSmokeMaskImageBase), 0);
    mask[zoom] = nullptr;
  }
  return 0;
}

// Publish smoke membership before inserting the effects occupant into animation group one.
// First insertion resets the frame deadline, retaining the accumulated elapsed word.
void SpSmoke::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 1, 0, true);
  frame_elapsed = 0;
}

// Clear membership before removing the smoke's actual occupant from the animation layer.
// Removal can destroy the object, so no further member access follows the callback.
void SpSmoke::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
}

// Advance one horizontal frame after the original fixed deadline, refreshing only at zoom two or greater.
// Reaching the byte frame limit retires membership even when drawing refresh is suppressed.
i32 SpSmoke::Tick(u32 delta) {
  elapsed += delta;
  if (elapsed > frame_elapsed + kSpSmokeFrameInterval) {
    ++frame;
    frame_elapsed = elapsed;
    for (i32 zoom = 0; zoom < 5; ++zoom)
      origins[zoom].x = i32(u32(frame) * u32(GetW(zoom, 0)));
    if (frame < frame_count) {
      if (SpEffects::zoom >= 2)
        SpEffects::animated->RefreshOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
            SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 2, nullptr);
    } else {
      RemoveFromAnimationSystem();
    }
  }
  return 0;
}

static ScLock SpTrajectoryLock;
static std::set<SpTrajectory *> SpTrajectoryObjects;

// Initialize the original trajectory fields after the sprite/occupant bases. Target and velocity remain unwritten
// until Init or seeking configuration supplies them; the default distance copies the original FLT_MAX constant.
SpTrajectory::SpTrajectory()
    : byte204(0), kind(0), allow_outside(0), signature(0), sprite_index(0),
      color_base(kSpTrajectoryDefaultColorImageBase), mask_base(0), motion(Motion::Linear), trailing_smoke(0),
      registered(0), impact_kind(1), flag260(1), elapsed(0), frame_elapsed(0), distance296(FLT_MAX), distance300(0.0f),
      frame(0), frame_count(8), frame_step(1), rotation_art(0), frame_interval(kSpTrajectoryInitialFrameInterval),
      shadow(nullptr), shadow_base(0), shadow_index(0) {
}

SpTrajectory::~SpTrajectory() {
  RemoveFromAnimationSystem();
}

bool SpTrajectory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpTrajectoryView, SpTrajectoryView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpTrajectory::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpTrajectory::Release() {
  return SpEffectOccupant::Release();
}

u8 SpTrajectory::EnableTrailingSmoke(u8 enabled) {
  trailing_smoke = enabled;
  return enabled;
}

u8 SpTrajectory::EnableCreationOutsideWorldBoundaries(u8 enabled) {
  allow_outside = enabled;
  return enabled;
}

i32 SpTrajectory::GetType() {
  return kind;
}

u32 SpTrajectory::GetSourceSignature() {
  return signature;
}

u32 SpTrajectory::SetSourceSignature(u32 value) {
  signature = value;
  return value;
}

// Configure shadow artwork only before a shadow object exists. The next initialization creates the actual sprite.
u8 SpTrajectory::AddShadow(u32 base, u32 index) {
  if (!shadow) {
    shadow_base = base;
    shadow_index = index;
  }
  return 1;
}

f64 SpEffects::LengthOf3DVector(const ScVec3F &vector) {
  return std::sqrt(f64(vector.x) * vector.x + f64(vector.y) * vector.y + f64(vector.z) * vector.z);
}

// Original division behavior and component-store order remain visible to the caller.
void SpEffects::Scale3DVector(ScVec3F *vector, f32 length) {
  f64 factor = f64(length) / LengthOf3DVector(*vector);
  vector->x = f32(factor * vector->x);
  vector->y = f32(factor * vector->y);
  vector->z = f32(factor * vector->z);
}

// Maintain the original unique pointer set under its recursive lock. Membership does not retain or release objects;
// the installed lock-release residue is ignored rather than exposed as a new game result.
void SpTrajectory::AddTrajectoryObjectToStaticList(SpTrajectory *object, u8 insert) {
  SpTrajectoryLock.Lock();
  if (insert)
    SpTrajectoryObjects.insert(object);
  else
    SpTrajectoryObjects.erase(object);
  SpTrajectoryLock.Unlock();
}

// Count registered trajectories matching their source signature. The original all-ones selector returns total size.
u32 SpTrajectory::GetCountOfTrajectoryObjectsStatic(u32 value) {
  SpTrajectoryLock.Lock();
  u32 count = 0;
  if (value == UINT32_MAX)
    count = u32(SpTrajectoryObjects.size());
  else
    for (SpTrajectory *object : SpTrajectoryObjects)
      if (object->signature == value)
        ++count;
  SpTrajectoryLock.Unlock();
  return count;
}

u32 SpTrajectory::GetCountOfTrajectoryObjects(u32 value) {
  return GetCountOfTrajectoryObjectsStatic(value);
}

// Select frame/zoom/rotation stride according to the original artwork family. Resource arithmetic wraps by DWORD.
u32 SpTrajectory::GetSpriteKey(i32 zoom, i32 rotation, i32 variant, u32 base) {
  u32 offset;
  if (rotation_art)
    if (kind >= 6 && kind <= 10)
      offset = u32(rotation) + 4 * u32(zoom);
    else
      offset = u32(rotation) + 4 * (u32(zoom) + 5 * u32(variant));
  else
    offset = u32(zoom) + 5 * u32(variant);
  return base + offset;
}

// Populate registration from existing color metadata for every zoom. Missing records leave constructor extents alone.
void SpTrajectory::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image =
        SpEffects::images->GetImageInfo(GetSpriteKey(zoom, SpEffects::rotation, sprite_index, color_base), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
}

// Acquire the selected color and optional alpha images before the configured shadow mask. The image metadata defines
// each registration rectangle; acquisition result bytes are not turned into synthetic failure/status paths.
u8 SpTrajectory::DemandArt(i32 zoom, i32 rotation, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(zoom, rotation, sprite_index, color_base), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  if (mode == 2) {
    SpEffects::images->PrepareImage(GetSpriteKey(zoom, rotation, sprite_index, mask_base), 0, &image);
    mask[zoom] = image->buffer;
  }
  if (shadow) {
    SpEffects::images->PrepareImage(u32(zoom) + shadow_base + 5 * shadow_index, 0, &image);
    shadow->mask[zoom] = image->buffer;
    shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  return 1;
}

// Retire only held color/mask demands, then the live shadow demand.
i32 SpTrajectory::ReleaseArt(i32 zoom, i32 rotation) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, rotation, sprite_index, color_base), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(zoom, rotation, sprite_index, mask_base), 0);
    mask[zoom] = nullptr;
  }
  if (shadow && shadow->mask[zoom]) {
    SpEffects::images->UnprepareImage(u32(zoom) + shadow_base + 5 * shadow_index, 0);
    shadow->mask[zoom] = nullptr;
  }
  return 0;
}

// Initialize actual position, velocity and artwork policy before registration. The original outside-world flag gates
// bounds failure; repeated initialization retains fields that the selected kind does not explicitly change.
u8 SpTrajectory::Init(i32 x, i32 z, i32 altitude, const ScVec3F &direction, i32 value) {
  if (!allow_outside && (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height)))
    return 0;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
  velocity = direction;
  kind = value;
  sprite_index = value;
  if (SpEffects::performance->processor_power >= 3) {
    distance296 = 250.0f;
    distance300 = 300.0f;
  }
  midpoint = velocity;
  SpEffects::Scale3DVector(&midpoint, distance296 * 0.5f);
  midpoint.x += position.x;
  midpoint.y += position.y;
  midpoint.z += position.z;
  if (impact_kind == 1) {
    if (!kind)
      impact_kind = 4;
    else if (kind >= 6 && kind <= 10)
      impact_kind = 6;
    else if (kind >= 3 && kind <= 5)
      impact_kind = 0;
    else
      impact_kind = 2;
  }
  if (kind == 1) {
    frame_interval = 20000;
    frame_count = 19;
    frame_step = 2;
  } else if (kind >= 3 && kind <= 5) {
    flag260 = 0;
    frame_interval = kSpTrajectoryMultiFrameInterval;
    frame_count = 4;
  } else if (kind >= 6 && kind <= 10) {
    sprite_index = kind - 6;
    mode = 2;
    color_base = (u32(sprite_index) + 16306u) << 16;
    rotation_art = 1;
    allow_outside = 1;
    frame_interval = kSpTrajectoryMultiFrameInterval;
    mask_base = color_base + 20;
    frame_count = 6;
  } else {
    frame_count = 8;
  }
  if (shadow_base && !shadow) {
    shadow = ScAddRefNotNull(new SpBasicSprite);
    shadow->position.x = 0.0f;
    shadow->position.z = 0.0f;
    shadow->position.y = 100000.0f;
    shadow->mode = 4;
  }
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Set the original trajectory policy and optional target vector.
void SpTrajectory::SetTrajectoryType(Motion value, void *data) {
  if (value == Motion::Seeking) {
    target = *static_cast<const ScVec3F *>(data);
    if (motion != Motion::Seeking)
      target_speed = f32(SpEffects::LengthOf3DVector(velocity));
  }
  motion = value;
}

// Insert the occupant at its actual position or the original off-world parking point, then register its pointer.
void SpTrajectory::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  i32 x = SpEffectCoordinate(position.x), z = SpEffectCoordinate(position.z);
  bool outside = u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height);
  if (outside && !allow_outside) {
    AddTrajectoryObjectToStaticList(this, 1);
    return;
  }
  if (outside)
    SpEffects::animated->InsertOccupant(
        static_cast<SpEffectOccupant *>(this), 0, 0, kSpOffWorldAnimationAltitude, 1, 0, true);
  else
    SpEffects::animated->InsertOccupant(
        static_cast<SpEffectOccupant *>(this), x, z, SpEffectCoordinate(position.y), 1, 0, true);
  if (shadow) {
    SpBasicSprite *current = shadow;
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(current),
        SpEffectCoordinate(current->position.x), SpEffectCoordinate(current->position.z),
        SpEffectCoordinate(current->position.y), 1, 0, true);
  }
  AddTrajectoryObjectToStaticList(this, 1);
}

// Remove set membership before invoking layer callbacks.
void SpTrajectory::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  AddTrajectoryObjectToStaticList(this, 0);
  u8 refs_before = references;
  SpBasicSprite *current = shadow;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
  if (current) {
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(current),
          SpEffectCoordinate(current->position.x), SpEffectCoordinate(current->position.z),
          SpEffectCoordinate(current->position.y));
    current->Release();
    if (refs_before > 1)
      shadow = nullptr;
  }
}

// Retire an existing shadow independently of trajectory membership. Exact Linux B9F30 proves the byte-one result;
// the PC standalone entry remains unlocated, so no fabricated address association is recorded.
u8 SpTrajectory::RemoveShadow() {
  if (shadow) {
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(shadow),
          SpEffectCoordinate(shadow->position.x), SpEffectCoordinate(shadow->position.z),
          SpEffectCoordinate(shadow->position.y));
    shadow->Release();
    shadow = nullptr;
  }
  return 1;
}

// Query the retained surface occupant and report its animation height only for original major type one.
// Always retire the acquired interface before returning; an absent or different occupant reports minus one.
static i32 SpBuildingHeightAtCell(i32 x, i32 z) {
  i32 height = -1;
  ScOccupant *occupant;
  if (SpEffects::surface->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
    if (occupant->MajorType() == 1)
      height = i32(occupant->SizeInAnimCellsY());
    occupant->Release();
  }
  return height;
}

// Advance the trajectory's artwork and original linear, decelerating or seeking motion, then process world collisions.
// Smoke, splash, messages and shadow moves preserve the original callback order and separate old/new coordinates.
i32 SpTrajectory::Tick(u32 delta) {
  elapsed += delta;
  bool was_inside = u32(SpEffectCoordinate(position.x)) < u32(SpEffects::micro_width) &&
      u32(SpEffectCoordinate(position.z)) < u32(SpEffects::micro_height);
  if (elapsed > frame_elapsed + frame_interval) {
    frame_elapsed = elapsed;
    frame += frame_step;
    if (frame >= frame_count)
      frame = 0;
    for (i32 zoom = 0; zoom < 5; ++zoom)
      origins[zoom].x = i32(u32(frame) * u32(GetW(zoom, 0)));
  }
  ScVec3F previous = position;
  constexpr f32 vertical_scale = 8.94f, inverse_scale = 1.0f / vertical_scale;
  f64 step = f64(delta) * f64(0.001f);
  if (motion != Motion::Linear && motion != Motion::Decelerating) {
    velocity = {target.x - position.x, target.y - position.y, target.z - position.z};
    SpEffects::Scale3DVector(&velocity, target_speed);
    f32 seeking_step = f32(step);
    position.x = f32(f64(seeking_step) * velocity.x + position.x);
    position.z = f32(f64(seeking_step) * velocity.z + position.z);
    position.y = f32(f64(vertical_scale) * velocity.y * seeking_step + position.y);
  } else {
    position.x = f32(step * velocity.x + position.x);
    position.z = f32(step * velocity.z + position.z);
    f64 vertical_speed = velocity.y;
    if (motion == Motion::Decelerating) {
      vertical_speed -= f64(0.0003f) * step;
      velocity.y = f32(vertical_speed);
    }
    position.y = f32(f64(vertical_scale) * vertical_speed * step + position.y);
  }
  i32 x = SpEffectCoordinate(position.x), z = SpEffectCoordinate(position.z), y = SpEffectCoordinate(position.y);
  bool inside = u32(x) < u32(SpEffects::micro_width) && u32(z) < u32(SpEffects::micro_height);
  if (!inside) {
    if (was_inside || !allow_outside) {
      position = previous;
      RemoveFromAnimationSystem();
    }
    return 0;
  }
  if (!was_inside && allow_outside) {
    SpEffects::animated->MoveOccupant(
        static_cast<SpEffectOccupant *>(this), 0, 0, kSpOffWorldAnimationAltitude, x, z, y);
    return 0;
  }
  u32 message;
  if (flag260) {
    i32 building_height = SpBuildingHeightAtCell(x / 256, z / 256);
    if (building_height != -1) {
      u8 altitude;
      SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
      if (i32(u32(building_height) + (u32(altitude) << kCtMicroCoordinateShift)) > y) {
        message = kSpMsg_BuildingImpact;
        goto impact;
      }
    }
  }
  u8 altitude;
  SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &altitude);
  if (y <= (i32(altitude) << kCtMicroCoordinateShift)) {
    message = SpEffects::dirt->IsWater(u32(x / 256), u32(z / 256)) ? kSpMsg_WaterImpact : kSpMsg_GroundImpact;
  impact:
    position = previous;
    if (message == kSpMsg_WaterImpact) {
      SpSplash *splash = new SpSplash;
      splash->AddRef();
      if (kind == 6) {
        static u32 splash_count;
        if (splash_count++ == UINT32_MAX)
          splash->Init(x, z, y, kSpNineFrameSplashColorBase, kSpNineFrameSplashMaskBase, 9);
        else
          splash->Init(x, z, y, kSpTenFrameSplashColorBase, kSpTenFrameSplashMaskBase, 10);
      } else {
        splash->Init(x, z, y, 0, 0, 0);
      }
      splash->Release();
    } else {
      DoImpact();
    }
    if (SpEffects::messages) {
      u32 cell = ((u32(x) & 0xff00u) << 8) | ((u32(z) >> 8) & 0xffu);
      SpEffects::messages->MessagePost(ScMessage(message, reinterpret_cast<uptr>(this), cell, signature), false);
    }
    RemoveFromAnimationSystem();
    return 0;
  }
  if (trailing_smoke) {
    f64 dx = f64(midpoint.x) - position.x, dy = f64(midpoint.y) - position.y;
    f64 dz = f64(midpoint.z) - position.z;
    if (dx < 0.0)
      dx = -dx;
    if (dy < 0.0)
      dy = -dy;
    if (dz < 0.0)
      dz = -dz;
    dy *= inverse_scale;
    if (std::sqrt(dz * dz + dy * dy + dx * dx) > distance296 && SpEffects::zoom >= 2) {
      ScVec3F point = velocity;
      SpSmoke *smoke = new SpSmoke;
      smoke->AddRef();
      SpEffects::Scale3DVector(&point, -distance300);
      point.x += position.x;
      point.y += position.y;
      point.z += position.z;
      smoke->Init(SpEffectCoordinate(point.x), SpEffectCoordinate(point.z), SpEffectCoordinate(point.y), 0);
      smoke->Release();
      midpoint = point;
    }
  }
  i32 old_x = SpEffectCoordinate(previous.x), old_z = SpEffectCoordinate(previous.z);
  SpEffects::animated->MoveOccupant(
      static_cast<SpEffectOccupant *>(this), old_x, old_z, SpEffectCoordinate(previous.y), x, z, y);
  if (shadow) {
    SpBasicSprite *current = shadow;
    i32 old_altitude = SpEffectCoordinate(current->position.y);
    current->position.x = f32(x);
    current->position.z = f32(z);
    current->position.y = f32(u32(altitude) << kCtMicroCoordinateShift);
    SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(current), old_x, old_z, old_altitude, x, z,
        SpEffectCoordinate(current->position.y));
  }
  return 0;
}

SpExplosion::SpExplosion()
    : registered(0), rotation_art(0), variant(0), color_base(kSpExplosionColorImageBase),
      mask_base(kSpExplosionMaskImageBase), stage(0), word224(0), stage_times{100000, 150000, 200000, 600000, 3900000},
      word248(0), frame_times{20480, 100000, 150000, 300000, 600000, 900000, 1200000, 1500000, 1800000, 2100000,
                      2400000, 2700000, 3000000, 3300000, 3600000},
      blast_enabled(0), radii{200, 300, 400}, elapsed(0), word332(0), word336(0), frame(0), frame_count(9), severity(0),
      effect_mode(0), center{0.0f, 0.0f, 0.0f}, overlay(nullptr) {
  geom_flags |= kSpEffectBaseGeometry;
}

SpExplosion::~SpExplosion() {
  RemoveFromAnimationSystem();
}

bool SpExplosion::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpExplosionView, SpExplosionView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpExplosion::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpExplosion::Release() {
  return SpEffectOccupant::Release();
}

// Configure the original center, artwork policy and blast gate before publishing animation membership.
// Repeated initialization retains fields not written by its selected mode, including the rotation-art switch.
u8 SpExplosion::Init(i32 x, i32 z, i32 altitude, u8 strength, i8 value, u8 start) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  blast_enabled = start;
  center.x = f32(x);
  center.z = f32(z);
  effect_mode = value;
  severity = strength;
  center.y = f32(altitude);
  mode = 2;
  variant = value;
  switch (value) {
  case 0:
    frame_count = strength >= 2 ? (strength == 2 ? 11 : 13) : 9;
    break;
  case 2:
    severity = 0;
    frame_count = 12;
    break;
  case 8:
    variant = 0;
    rotation_art = 1;
    color_base = kSpExplosionVariantEightColorImageBase;
    mask_base = kSpExplosionVariantEightMaskImageBase;
    frame_count = 13;
    severity = 2;
    break;
  default:
    frame_count = 9;
    break;
  }
  SetSpanRects();
  if (SpEffects::performance->processor_power <= 3) {
    frame = 1;
    for (i32 zoom = 0; zoom < 5; ++zoom)
      origins[zoom].x = i32(u32(frame) * (u32(registration[zoom].left) + u32(registration[zoom].right)));
  }
  AddToAnimationSystem();
  return 1;
}

// Replace the three original signed blast radii in order and preserve the full final-input register reply.
i32 SpExplosion::SetBlastRadius(i32 first, i32 second, i32 third) {
  radii[0] = first;
  radii[1] = second;
  radii[2] = third;
  return third;
}

// Compute unit outward velocities from the rounded explosion center, with the original upward bias below center.
// Components are written separately before normalization, preserving the source's input/output overlap order.
u8 SpExplosion::GetOccupantTrajectories(ScVec3F *points, ScVec3F *out, u32 count) {
  for (u32 index = 0; index < count; ++index) {
    out[index].x = f32(f64(points[index].x) - SpEffectCoordinate(center.x));
    out[index].z = f32(f64(points[index].z) - SpEffectCoordinate(center.z));
    f64 height = f64(points[index].y) - SpEffectCoordinate(center.y);
    out[index].y = f32(height);
    if (height < 0.0)
      out[index].y = 0.1f;
    SpEffects::Scale3DVector(&out[index], 1.0f);
    out[index].y = f32(f64(8.94f) * out[index].y);
  }
  return 1;
}

// Select the original severity/zoom/rotation resource stride, retaining its special variant aliases.
// Rotation artwork uses a separate layout and mode eight suppresses the ordinary variant stride.
u32 SpExplosion::GetSpriteKey(i32 strength, i32 zoom, i32 rotation, i32 value, u32 base) {
  u32 offset;
  if (rotation_art) {
    if (effect_mode == 8)
      offset = u32(rotation) + 4 * u32(zoom);
    else
      offset = u32(rotation) + 4 * (u32(zoom) + 5 * u32(value));
  } else {
    if (value == 4 || value == 6)
      value = 0;
    else if (value == 5)
      value = 1;
    offset = 5 * u32(strength) + u32(zoom) + 32 * u32(value);
  }
  return base + offset;
}

i32 SpExplosion::GetW(i32 zoom, i32) const {
  const ScRect &rect = overlay ? overlay->registration[zoom] : registration[zoom];
  return i32(u32(rect.left) + u32(rect.right));
}

// Preserve the installed height query's overlay-width behavior; only the no-overlay path adds vertical extents.
// This apparent original quirk is not replaced with a geometrically corrected result.
i32 SpExplosion::GetH(i32 zoom, i32) const {
  if (overlay) {
    const ScRect &rect = overlay->registration[zoom];
    return i32(u32(rect.left) + u32(rect.right));
  }
  return i32(u32(registration[zoom].top) + u32(registration[zoom].bottom));
}

// Draw the actual base image first, then dispatch the optional overlay with the same destination rectangle.
// Both underlying draw results are discarded and the original byte-one result is retained.
u8 SpExplosion::Draw(GxBuffer *target, const ScRect &rect, i32 zoom, i32 rotation, ScRect *clip) const {
  SpBasicSprite::Draw(target, rect, zoom, rotation, clip);
  if (overlay)
    overlay->Draw(target, rect, zoom, rotation, clip);
  return 1;
}

// Draw the explosion and then its optional overlay at the supplied anchor.
// Keep the original two direct drawing operations and ordinary byte-one reply.
u8 SpExplosion::Draw(GxBuffer *target, i32 x, i32 y, i32 zoom, i32 rotation, ScRect *clip) const {
  SpBasicSprite::Draw(target, x, y, zoom, rotation, clip);
  if (overlay)
    overlay->Draw(target, x, y, zoom, rotation, clip);
  return 1;
}

// Populate explosion and optional overlay registration from actual image metadata at all zoom levels.
void SpExplosion::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(
        GetSpriteKey(i8(severity), zoom, SpEffects::rotation, effect_mode, color_base), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
    if (overlay) {
      image = SpEffects::images->GetImageInfo(
          GetSpriteKey(i8(severity), zoom, SpEffects::rotation, effect_mode, kSpExplosionOverlayImageBase), 0);
      if (image)
        overlay->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    }
  }
}

// Demand the real color/mask pair and optional overlay color, recording each image's actual registration extents.
u8 SpExplosion::DemandArt(i32 zoom, i32 rotation, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(i8(severity), zoom, rotation, effect_mode, color_base), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  SpEffects::images->PrepareImage(GetSpriteKey(i8(severity), zoom, rotation, effect_mode, mask_base), 0, &image);
  mask[zoom] = image->buffer;
  if (overlay) {
    SpEffects::images->PrepareImage(
        GetSpriteKey(i8(severity), zoom, rotation, effect_mode, kSpExplosionOverlayImageBase), 0, &image);
    overlay->color[zoom] = image->buffer;
    overlay->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  return 1;
}

// Retire only held explosion color/mask and overlay-color demands, invoking each cache callback before clearing it.
// The original full-word zero return is retained even when no cache exists.
i32 SpExplosion::ReleaseArt(i32 zoom, i32 rotation) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(i8(severity), zoom, rotation, effect_mode, color_base), 0);
    color[zoom] = nullptr;
  }
  if (mask[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(i8(severity), zoom, rotation, effect_mode, mask_base), 0);
    mask[zoom] = nullptr;
  }
  if (overlay && overlay->color[zoom]) {
    SpEffects::images->UnprepareImage(
        GetSpriteKey(i8(severity), zoom, rotation, effect_mode, kSpExplosionOverlayImageBase), 0);
    overlay->color[zoom] = nullptr;
  }
  return 0;
}

// Publish explosion membership before inserting its occupant at the separately stored center in group one.
// Insertion results are discarded as in the original; no alternate membership service is added.
void SpExplosion::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(center.x),
      SpEffectCoordinate(center.z), SpEffectCoordinate(center.y), 1, 0, true);
}

// Clear membership and release the overlay before removing the actual explosion occupant.
// The final layer callback can destroy this object, so it is followed by no further member access.
void SpExplosion::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  ScRelease(&overlay);
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(center.x),
        SpEffectCoordinate(center.z), SpEffectCoordinate(center.y));
}

// Query up to128 retained occupants in the original clamped blast box and synchronously publish their outward vectors.
// The stack payload remains live through MessageSend, after which each acquired occupant is released in order.
void SpExplosion::DoBlastWave(i32 radius) {
  GeBounds bounds;
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (!SpEffects::messages)
    return;
  i32 x = SpEffectCoordinate(center.x), z = SpEffectCoordinate(center.z), y = SpEffectCoordinate(center.y);
  bounds.minimum.x = ScClampInt(i32(u32(x) - u32(radius)), 0, SpEffects::micro_width);
  bounds.minimum.z = ScClampInt(i32(u32(z) - u32(radius)), 0, SpEffects::micro_height);
  bounds.minimum.altitude = i32(u32(y) - u32(radius));
  bounds.maximum.x = ScClampInt(i32(u32(x) + u32(radius)), 0, SpEffects::micro_width);
  bounds.maximum.z = ScClampInt(i32(u32(z) + u32(radius)), 0, SpEffects::micro_height);
  bounds.maximum.altitude = i32(u32(y) + u32(radius));
  ScOccupant *occupants[128];
  SpBlastTarget targets[128];
  u32 count = 128;
  SpEffects::manager->GetUnalignedOccupantsInVolume(bounds, occupants, &count);
  if (!count)
    return;
  for (u32 index = 0; index < count; ++index) {
    occupants[index]->GetLocation(&point);
    targets[index].occupant = occupants[index];
    targets[index].velocity.x = f32(i32(u32(point.x) - u32(SpEffectCoordinate(center.x))));
    targets[index].velocity.z = f32(i32(u32(point.z) - u32(SpEffectCoordinate(center.z))));
    targets[index].velocity.y = f32(i32(u32(point.altitude) - u32(SpEffectCoordinate(center.y))));
    if (targets[index].velocity.y < 0.0f)
      targets[index].velocity.y = 0.1f;
    SpEffects::Scale3DVector(&targets[index].velocity, 1.0f);
    targets[index].velocity.y = f32(f64(8.94f) * targets[index].velocity.y);
  }
  SpEffects::messages->MessageSend(
      ScMessage(kSpMsg_BlastTargets, reinterpret_cast<uptr>(this), count, reinterpret_cast<uptr>(targets)));
  for (u32 index = 0; index < count; ++index)
    targets[index].occupant->Release();
}

// Construct the actual explosion policy for the selected trajectory impact, retaining its original Init/Release order.
// Only impact kinds two and four send their corresponding BoxX events after the explosion call completes.
void SpTrajectory::DoImpact() {
  i8 value;
  i32 event = 0;
  switch (impact_kind) {
  case 2:
    value = 2;
    event = 204;
    break;
  case 3:
    value = 0;
    break;
  case 4:
    value = 1;
    event = 200;
    break;
  case 6:
    value = 8;
    break;
  default:
    return;
  }
  SpExplosion *explosion = new SpExplosion;
  explosion->AddRef();
  explosion->Init(SpEffectCoordinate(position.x), SpEffectCoordinate(position.z), SpEffectCoordinate(position.y),
      byte204, value, 0);
  explosion->Release();
  if (event && SpEffects::box)
    SpEffects::box->Event(event, uptr(SpEffectCoordinate(position.x) / kCtMicroUnitsPerCell),
        uptr(SpEffectCoordinate(position.z) / kCtMicroUnitsPerCell), 0, 0);
}

static u16 SpDebrisCount;

SpDebris::SpDebris()
    : byte204(0), variant(0), velocity{-1.0f, -1.0f, -1.0f}, elapsed(0), frame_elapsed(0), gravity(0.0003f),
      hit_building(0), settled_at(0), registered(0), frame(0), frame_count(8), create_shadow(1), shadow(nullptr),
      frame_interval(kSpDebrisDefaultFrameInterval) {
  geom_flags |= kSpEffectBaseGeometry;
  ++SpDebrisCount;
}

SpDebris::~SpDebris() {
  RemoveFromAnimationSystem();
  --SpDebrisCount;
}

bool SpDebris::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_SpDebrisView, SpDebrisView);
  return SpBasicSprite::QueryInterface(iid, out);
}

u32 SpDebris::AddRef() {
  return SpEffectOccupant::AddRef();
}

u32 SpDebris::Release() {
  return SpEffectOccupant::Release();
}

// Bind debris motion and resource policy, generating only the velocities selected by the original sentinel/variant rules.
// Terrain height, processor power and the module's live count govern its actual shadow allocation and registration.
u8 SpDebris::Init(i32 x, i32 z, i32 altitude, const ScVec3F &direction, i32 value, i8 argument) {
  if (u32(x) >= u32(SpEffects::micro_width) || u32(z) >= u32(SpEffects::micro_height) || !SpEffects::animated)
    return 0;
  position.x = f32(x);
  position.z = f32(z);
  position.y = f32(altitude);
  velocity = direction;
  if (value == 1007) {
    velocity.x = f32(SpEffects::random.RandomDoubleRangeUniform(0.4f, 1.2f));
    velocity.z = f32(SpEffects::random.RandomDoubleRangeUniform(f64(0.04f) * -4.0, f64(0.04f) * 4.0));
    velocity.y = f32(SpEffects::random.RandomDoubleRangeUniform(0.2f, 0.4f));
  }
  if (velocity.x == -1.0f) {
    velocity.x = f32(SpEffects::random.RandomDoubleRangeUniform(-0.4f, 0.4f));
    velocity.z = f32(SpEffects::random.RandomDoubleRangeUniform(-0.4f, 0.4f));
    if (value == 1003 || value == 1005)
      velocity.y = f32(SpEffects::random.RandomDoubleRangeUniform(-0.4f, 0.4f));
    else if (value == 1006)
      velocity.y = 0.0f;
    else
      velocity.y = f32(SpEffects::random.RandomDoubleRangeUniform(0.4f, 0.6f));
  }
  if (value < 1000 || value == 1005 || value == 1006)
    variant = i16(value);
  else if (value == 1001)
    variant = i16(SpEffects::random.Range(4, 8));
  else
    variant = i16(SpEffects::random.Range(0, 4));
  byte204 = u8(argument);
  u8 terrain;
  SpEffects::dirt->GetVertexAltitude(u32(x / 256), u32(z / 256), &terrain);
  f32 terrain_height = f32(u32(terrain) << kCtMicroCoordinateShift);
  if (position.y < terrain_height)
    position.y = terrain_height;
  if (create_shadow) {
    i32 power = SpEffects::performance->processor_power;
    if (power >= 3 && SpDebrisCount < 20 * (power - 3)) {
      shadow = ScAddRefNotNull(new SpBasicSprite);
      shadow->position.x = f32(x);
      shadow->position.z = f32(z);
      shadow->position.y = terrain_height;
      shadow->mode = 1;
    }
  }
  frame_interval = u32(SpEffects::random.Range(kSpDebrisMinimumFrameInterval, kSpDebrisMaximumFrameInterval));
  SetSpanRects();
  AddToAnimationSystem();
  return 1;
}

// Select the actual debris resource key, including the two special nonrandom resource variants.
// The first original operand is unused; retain it rather than changing the method contract.
u32 SpDebris::GetSpriteKey(i32, i32 zoom, i32 value) {
  if (value == 1005)
    return u32(zoom) - kSpDebrisVariant1005ColorOffset;
  if (value == 1006)
    return u32(zoom) - kSpDebrisVariant1006ColorOffset;
  return 5 * u32(value) + u32(zoom) - kSpDebrisColorKeyOffset;
}

// Populate debris and shadow extents from their original metadata lookup formulas.
// The shadow metadata formula deliberately lacks the special cases used by its actual pixel demands.
void SpDebris::SetSpanRects() {
  for (i32 zoom = 0; zoom < 5; ++zoom) {
    SpImage *image = SpEffects::images->GetImageInfo(GetSpriteKey(i8(byte204), zoom, variant), 0);
    if (image)
      registration[zoom] = {image->left, image->top, image->right, image->bottom};
    if (shadow) {
      image = SpEffects::images->GetImageInfo(5 * u32(variant) + u32(zoom) - kSpDebrisShadowKeyOffset, 0);
      if (image)
        shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
    }
  }
}

// Acquire the debris color and optional shadow color using the actual variant-specific resource keys.
// Their registration extents come directly from the demanded metadata records.
u8 SpDebris::DemandArt(i32 zoom, i32, bool) {
  SpImage *image = nullptr;
  SpEffects::images->PrepareImage(GetSpriteKey(i8(byte204), zoom, variant), 0, &image);
  color[zoom] = image->buffer;
  registration[zoom] = {image->left, image->top, image->right, image->bottom};
  if (shadow) {
    u32 key;
    if (variant == 1005)
      key = u32(zoom) - kSpDebrisVariant1005ShadowOffset;
    else if (variant == 1006)
      key = u32(zoom) - kSpDebrisVariant1006ShadowOffset;
    else
      key = 5 * u32(variant) + u32(zoom) - kSpDebrisShadowKeyOffset;
    SpEffects::images->PrepareImage(key, 0, &image);
    shadow->color[zoom] = image->buffer;
    shadow->registration[zoom] = {image->left, image->top, image->right, image->bottom};
  }
  return 1;
}

// Release held debris and shadow color demands without inventing mask ownership.
i32 SpDebris::ReleaseArt(i32 zoom, i32) {
  if (!SpEffects::images)
    return 0;
  if (color[zoom]) {
    SpEffects::images->UnprepareImage(GetSpriteKey(i8(byte204), zoom, variant), 0);
    color[zoom] = nullptr;
  }
  if (shadow && shadow->color[zoom]) {
    u32 key;
    if (variant == 1005)
      key = u32(zoom) - kSpDebrisVariant1005ShadowOffset;
    else if (variant == 1006)
      key = u32(zoom) - kSpDebrisVariant1006ShadowOffset;
    else
      key = 5 * u32(variant) + u32(zoom) - kSpDebrisShadowKeyOffset;
    SpEffects::images->UnprepareImage(key, 0);
    shadow->color[zoom] = nullptr;
  }
  return 0;
}

// Publish membership before inserting debris and then its optional shadow at their actual positions in group one.
// The shadow is a real basic sprite with a separately adjusted occupant identity.
void SpDebris::AddToAnimationSystem() {
  if (registered)
    return;
  registered = 1;
  SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
      SpEffectCoordinate(position.z), SpEffectCoordinate(position.y), 1, 0, true);
  if (shadow) {
    SpBasicSprite *current = shadow;
    SpEffects::animated->InsertOccupant(static_cast<SpEffectOccupant *>(current),
        SpEffectCoordinate(current->position.x), SpEffectCoordinate(current->position.z),
        SpEffectCoordinate(current->position.y), 1, 0, true);
  }
}

// Remove debris before its captured shadow, preserving the original final-reference callback behavior.
// Clear the owner's shadow field only when the captured reference byte proves the owner survives layer removal.
void SpDebris::RemoveFromAnimationSystem() {
  if (!registered)
    return;
  registered = 0;
  u8 refs_before = references;
  SpBasicSprite *current = shadow;
  if (SpEffects::animated)
    SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(this), SpEffectCoordinate(position.x),
        SpEffectCoordinate(position.z), SpEffectCoordinate(position.y));
  if (current) {
    if (SpEffects::animated)
      SpEffects::animated->RemoveOccupant(static_cast<SpEffectOccupant *>(current),
          SpEffectCoordinate(current->position.x), SpEffectCoordinate(current->position.z),
          SpEffectCoordinate(current->position.y));
    current->Release();
    if (refs_before > 1)
      shadow = nullptr;
  }
}

// Advance one explosion stage and one artwork frame per tick, creating the original blast and debris policies.
// Random calls, concrete debris lifetimes and overlay retirement preserve their original order before layer updates.
i32 SpExplosion::Tick(u32 delta) {
  elapsed += delta;
  u32 now = elapsed;
  if (now > stage_times[stage]) {
    ++stage;
    if (stage == 1) {
      if (blast_enabled && i8(severity) > 0) {
        i32 radius = radii[i8(severity) - 1];
        if (radius)
          DoBlastWave(radius);
      }
    } else if (stage == 2) {
      ScRelease(&overlay);
    } else if (stage == 3 && (SpEffects::zoom >= 1 || effect_mode >= 4)) {
      if (effect_mode == 5) {
        SpDebris *debris = new SpDebris;
        debris->AddRef();
        ScVec3F direction{0.0f, 0.0f, 0.0f};
        debris->Init(SpEffectCoordinate(center.x), SpEffectCoordinate(center.z), SpEffectCoordinate(center.y),
            direction, 1006, 0);
        debris->Release();
        i32 count = SpEffects::performance->processor_power / 2 + 2;
        f32 step = f32(f64(6.2831855f) / count), angle = 0.0f;
        for (i32 index = 0; index < count; ++index) {
          direction.x = f32(std::cos(f64(angle)) * f64(0.8f));
          direction.z = f32(std::sin(f64(angle)) * f64(0.8f));
          debris = ScAddRefNotNull(new SpDebris);
          i32 y = SpEffectCoordinate(center.y), z = SpEffectCoordinate(center.z), x = SpEffectCoordinate(center.x);
          z = i32(u32(z) + u32(SpEffects::random.Range(20, 30)));
          x = i32(u32(x) + u32(SpEffects::random.Range(20, 30)));
          debris->Init(x, z, y, direction, 1005, 0);
          debris->Release();
          angle += step;
        }
      } else {
        i32 count = SpEffects::performance->processor_power / 2;
        for (i32 index = 0; index < count; ++index) {
          SpDebris *debris = new SpDebris;
          debris->AddRef();
          i32 value;
          switch (effect_mode) {
          case 4:
            value = 1003;
            break;
          case 6:
            value = 1004;
            break;
          case 8:
            value = 1007;
            break;
          default:
            value = i32(SpEffects::random.Bounded(8));
            break;
          }
          i32 x = i32(u32(SpEffectCoordinate(center.x)) + u32(SpEffects::random.Range(10, 25)));
          i32 z = i32(u32(SpEffectCoordinate(center.z)) + u32(SpEffects::random.Range(10, 25)));
          i32 y = i32(u32(SpEffectCoordinate(center.y)) + 768u);
          ScVec3F direction{-1.0f, -1.0f, -1.0f};
          debris->Init(x, z, y, direction, value, 0);
          debris->Release();
        }
      }
    }
  }
  if (now > frame_times[frame]) {
    ++frame;
    if (frame < frame_count) {
      for (i32 zoom = 0; zoom < 5; ++zoom)
        origins[zoom].x = i32(u32(frame) * (u32(registration[zoom].left) + u32(registration[zoom].right)));
      i32 x = SpEffectCoordinate(center.x), z = SpEffectCoordinate(center.z), y = SpEffectCoordinate(center.y);
      SpEffects::animated->MoveOccupant(static_cast<SpEffectOccupant *>(this), x, z, y, x, z, y);
    } else {
      RemoveFromAnimationSystem();
    }
  }
  return 0;
}
