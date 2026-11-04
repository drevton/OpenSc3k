#include "economy/ec_sector.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "application/api.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_neighbor_gate.h"
#include "economy/ec_pollution.h"
#include "economy/ec_rci_module.h"
#include "economy/ec_rci_random.h"
#include "geometry/api.h"
#include "geometry/building.h"
#include "geometry/ge_types.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "window/api.h"

EcOccupant::EcOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

// Retire attributes and the base sprite chain only when attributes are present. The final flag mask preserves
// references, removal permissions and the mark bit, matching the qualified base cleanup used during destruction.
EcOccupant::~EcOccupant() {
  if (attributes) {
    attributes->Release();
    attributes = nullptr;
    EcOccupant::DestroySprites(&sprite);
    flags &= kOcFlagsWithoutSubtypeOrMembership;
  }
}

// Expose the four identities sharing this occupant receiver. Successful queries acquire a virtual reference.
bool EcOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScQueryTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

bool EcOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool EcOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 EcOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void EcOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void EcOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

u8 EcOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool EcOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void EcOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool EcOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void EcOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

void EcOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool EcOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude to micro coordinates. Exclude orientation bits from altitude.
void EcOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void EcOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 EcOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Replace cell coordinates and altitude from micro coordinates in source store order. Preserve orientation.
void EcOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void EcOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void EcOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool EcOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void EcOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void EcOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void EcOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Follow each borrowed sprite link before releasing its owner. Clear the published head after the final release.
void EcOccupant::DestroySprites(SpSprite **out) {
  SpSprite *current = *out;
  if (current) {
    do {
      SpSprite *next = current->GetNext();
      current->Release();
      current = next;
    } while (current);
    *out = nullptr;
  }
}

// Retain the module before locking and increment only the packed reference word. Capture the count before unlocking.
u32 EcOccupant::AddRef() {
  EcGetDirector()->AddRef();
  EcOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  EcOccupantLock.Unlock();
  return count;
}

// Release the module while locked and reload its callback changes before decrementing the live count.
// At zero, unlock before virtual shutdown and destruction; an initially zero count does not release another module hold.
u32 EcOccupant::Release() {
  EcOccupantLock.ScLock::Lock();
  if (u16(flags)) {
    EcGetDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    if (u16(flags)) {
      EcOccupantLock.ScLock::Unlock();
      return u16(flags);
    }
  }
  EcOccupantLock.ScLock::Unlock();
  Shutdown();
  delete this;
  return 0;
}

// Borrow the generic attribute pointer without acquiring a reference or consulting the city.
GeAttrib *EcOccupant::GetAttribData() {
  return attributes;
}

u32 EcOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

bool EcOccupant::DoMessage(ScMessage *) {
  return false;
}

bool EcOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 EcOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 EcOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 EcOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 EcOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void EcOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 EcOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u32 EcOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 EcOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

void EcOccupant::AnimStart() {
}

void EcOccupant::AnimStop() {
}

i32 EcOccupant::Tick(u32) {
  return 0;
}

bool EcOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Select the actual primary sprite-definition or agent-type subsystem through virtual getters.
// Acquire a reference before publishing the selected identity.
bool EcOccupant::GetSubSystemData(u32 id, void **out) {
  if (!attributes)
    return false;
  ScUnknown *selected;
  if (id == IID_SpDefView)
    selected = static_cast<SpDefView *>(GetSpriteAttrib());
  else if (id == IID_CtAgentType)
    selected = CityAgentType();
  else
    return false;
  if (!selected)
    return false;
  selected->AddRef();
  *out = selected;
  return true;
}

// Borrow the default sprite definition from generic attributes without acquiring it.
SpDefinition *EcOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the requested layer definition from generic attributes. Missing attributes produce a null pointer.
SpDefinition *EcOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Borrow the stored sprite instance without changing ownership.
SpSprite *EcOccupant::GetSpriteInst() {
  return sprite;
}

// Compare against the virtual default layer before fetching the virtual instance. Other layers have no instance.
SpSprite *EcOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u32 EcOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

u8 EcOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 EcOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 EcOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 EcOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << 8;
}

u32 EcOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << 8;
}

u32 EcOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << 8;
}

// Publish both cell coordinates through the virtual getter, then scale the live outputs in original store order.
void EcOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u32 EcOccupant::GetMemUse() const {
  return 20;
}

bool EcOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool EcOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

u32 EcOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Leave output untouched without attributes. Otherwise capture the minimum before callbacks and compute Y, Z then X
// extents, reloading the packed position after each virtual size getter before publishing maximum coordinates.
void EcOccupant::GetBounds(GeBounds *out) const {
  if (!attributes)
    return;
  u32 value = position;
  out->minimum.x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->minimum.z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->minimum.altitude =
      i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
  u32 ymax = SizeInCellsY();
  ymax += ((position >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) - 1;
  u32 zmax = SizeInCellsZ();
  zmax += ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
  u32 xmax = SizeInCellsX();
  xmax += (position & kOcPositionCellValueMask) - 1;
  out->maximum.x = i32(xmax << kCtMicroCoordinateShift);
  out->maximum.z = i32(zmax << kCtMicroCoordinateShift);
  out->maximum.altitude = i32(ymax << kCtMicroCoordinateShift);
}

// Publish the live cell rectangle only when attributes exist. Size callbacks run X then Z and may change position
// before each corresponding maximum is computed and immediately stored.
void EcOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (!attributes)
    return;
  out->left = position & kOcPositionCellValueMask;
  out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  u32 width = SizeInCellsX();
  out->right = width + (position & kOcPositionCellValueMask) - 1;
  u32 depth = SizeInCellsZ();
  out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
}

// Create an instance through the selected generic attribute definition, then set rotation before geometry flags.
// Failure still performs the original borrowed-key getter call; it does not fabricate a sprite or clear output.
bool EcOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (!definition || !definition->CreateInstance(out)) {
    attributes->Key();
    return false;
  }
  (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
  u32 geometry = GetGeomFlag();
  (*out)->SetGeomFlag(geometry);
  return true;
}

// Replace the packed orientation only when its full input differs.
u8 EcOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  u32 layer = GetDefaultLayerFlag();
  CreateSprite(layer, &sprite);
  return 1;
}

// Keep an existing attribute binding unchanged. Otherwise acquire through the application cache, invoke the virtual
// attribute initializer and release its live temporary output; failed cache lookup clears the instance pointer.
bool EcOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!EcGetAttribCache()->GetOccupantAttrib(*key, &value)) {
    attributes = nullptr;
    return false;
  }
  bool result = Init(value);
  if (value)
    value->Release();
  return result;
}

// Retire old attributes before the sprite chain, retain the new pointer and create its actual default-layer sprite.
// Failed creation invokes virtual shutdown; successful binding adopts the default subtype only when its byte is zero.
bool EcOccupant::Init(GeAttrib *value) {
  if (!value)
    return false;
  ScRelease(&attributes);
  if (sprite)
    DestroySprites(&sprite);
  attributes = value;
  value->AddRef();
  u32 layer = GetDefaultLayerFlag();
  if (!CreateSprite(layer, &sprite)) {
    Shutdown();
    return false;
  }
  if (!u8(flags >> kOcSubtypeShift)) {
    u8 subtype = attributes->SubTypeInfo();
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
  }
  return true;
}

// Bind a descriptor key only when attributes are absent, then apply partial position and rotation outputs even on
// failure. Successful records supply removal flags; failed records enable both permissions without status wrappers.
bool EcOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool result = true;
  if (!attributes) {
    RsKey key{};
    info->GetAttribKey(&key);
    result = Init(&key);
    u32 type = 0;
    if (result)
      result = info->GetTypeId(&type) != 0;
  }
  u32 x = 0, z = 0, altitude = 0, rotation = 0;
  if (result)
    result = info->GetLocationInCells(&x, &z, &altitude) != 0;
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
  if (result)
    result = info->GetRotationFlag(&rotation) != 0;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (result) {
    bool simulation = info->GetCanSimRemoveFlag();
    flags = (flags & kOcFlagsWithoutSimRemove) | (u32(simulation) << kOcSimRemoveShift);
    bool user = info->GetCanUserRemoveFlag();
    flags = (flags & kOcFlagsWithoutUserRemove) | (u32(user) << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return result;
}

// Borrow the generic attribute agent type without acquiring a reference. Unbound occupants have no type.
CtAgentType *EcOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

// Clear the real query-window output before testing attributes. Resolve the configured class through this module's
// actual COM service, passing the caller's live output directly across that external interface boundary.
bool EcOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 class_id = attributes->GetQueryDialogClass();
  return EcGetCom()->GetClassObject(class_id, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

u8 EcOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

// Write generic occupant properties to the actual descriptor in source order. The original invokes the X-size
// getter twice: first for the second size argument, then for the first; preserve these distinct virtual calls.
bool EcOccupant::Save(CtCity *, OcPersistInfo *info) {
  if (!attributes)
    return false;
  info->SetAttribKey(*attributes->Key());
  u32 type = TypeId();
  info->SetTypeId(type);
  info->SetLocationInCells(position & kOcPositionCellValueMask,
      (position >> kOcPositionZShift) & kOcPositionCellValueMask, u8(position >> kOcPositionAltitudeShift));
  u32 second = SizeInCellsX();
  u32 first = SizeInCellsX();
  info->SetSizeInCells(first, second);
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  info->SetRemoveFlags(u8((flags >> kOcSimRemoveShift) & 1), u8((flags >> kOcUserRemoveShift) & 1));
  return true;
}

// Release and clear the live attributes before virtual sprite cleanup. Apply the original flag mask after callbacks;
// the shared void contract exposes the cleanup effects without inventing a success result.
void EcOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

u16 EcOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

EcBuilding::EcBuilding() : occupancy_flags(0), agent_type(nullptr), cached_attributes(nullptr), cached_sprite(nullptr) {
}

EcBuilding::~EcBuilding() {
  ScRelease(&agent_type);
}

// Clear output before selecting building or city-agent identity. Delegate other requests to the adjusted occupant base.
bool EcBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  return EcOccupant::QueryInterface(iid, out);
}

u32 EcBuilding::AddRef() {
  return EcOccupant::AddRef();
}

u32 EcBuilding::Release() {
  return EcOccupant::Release();
}

// Borrow the adjusted occupant interface without acquiring another reference.
ScOccupant *EcBuilding::AsISC3Occupant() {
  return static_cast<EcOccupant *>(this);
}

// Borrow the city-agent subobject without a query or reference acquisition.
CtAgent *EcBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool EcBuilding::Simulate(i32 elapsed) {
  return agent_type->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 EcBuilding::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool EcBuilding::IsAgentLocking() {
  return true;
}

// Borrow the retained agent type without changing its reference count.
CtAgentType *EcBuilding::AgentType() {
  return agent_type;
}

bool EcBuilding::SetAgentType(CtAgentType *) {
  return false;
}

u32 EcBuilding::TypeId() const {
  return EcOccupant::TypeId();
}

u32 EcBuilding::GetOccupancyState() {
  return u8(occupancy_flags);
}

bool EcBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (u32(mask) & occupancy_flags) != 0;
}

u32 EcBuilding::GetGeomFlag() {
  return 260;
}

u32 EcBuilding::GetMemUse() const {
  return 44;
}

// Borrow the original generic attribute pointer directly, bypassing the city-specific default-data selection.
GeAttrib *EcBuilding::GetBaseAttribData() {
  return EcOccupant::GetAttribData();
}

// Borrow cached attributes when present. Otherwise select the city's building default for inactive occupancy states,
// falling back to generic attributes on absent defaults and publishing that borrowed result after callbacks.
GeAttrib *EcBuilding::GetAttribData() {
  if (cached_attributes)
    return cached_attributes;
  if (occupancy_flags != 1 && !(u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = EcGetCity()->BuildingLayer();
    if (layer) {
      cached_attributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cached_attributes)
        return cached_attributes;
    }
  }
  cached_attributes = EcOccupant::GetAttribData();
  return cached_attributes;
}

// Select the virtual default layer before invoking this building's qualified indexed getter.
SpDefinition *EcBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return EcBuilding::GetSpriteAttrib(layer);
}

// Reuse the borrowed sprite cache for every layer. Active states use the generic indexed definition; other states
// ask the city's building defaults and fall back to the generic default definition when no result is available.
SpDefinition *EcBuilding::GetSpriteAttrib(u32 layer) {
  if (cached_sprite)
    return cached_sprite;
  if (occupancy_flags == 1 || (u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    cached_sprite = EcOccupant::GetSpriteAttrib(layer);
    return cached_sprite;
  }
  GeBuildingLayer *buildings = EcGetCity()->BuildingLayer();
  if (buildings) {
    cached_sprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cached_sprite)
      return cached_sprite;
  }
  cached_sprite = EcOccupant::GetSpriteAttrib();
  return cached_sprite;
}

// Query the specialized building attribute view from the virtual generic getter. Preserve its raw power byte
// before releasing the acquired interface; absent attributes or a failed query produce zero.
u8 EcBuilding::PowerRequired() {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  void *building;
  if (!value->QueryInterface(IID_GeBuildingType, &building))
    return 0;
  u8 power = static_cast<GeBuildingType *>(building)->PowerRequired();
  static_cast<GeBuildingType *>(building)->Release();
  return power;
}

bool EcBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool EcBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

bool EcBuilding::Init(RsKey *key) {
  return EcOccupant::Init(key);
}

// Bind the actual occupant attributes before clearing borrowed building caches. Capture and retain the default agent
// type only when none is stored; failed occupant initialization performs the original second virtual shutdown call.
bool EcBuilding::Init(GeAttrib *value) {
  if (!EcOccupant::Init(value)) {
    Shutdown();
    return false;
  }
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  if (!agent_type) {
    agent_type = GetBaseAttribData()->CityAgentType();
    if (agent_type)
      agent_type->AddRef();
  }
  return true;
}

bool EcBuilding::SetName(const ScStringView &) {
  return false;
}

bool EcBuilding::IsNameChangeable() {
  return false;
}

bool EcBuilding::HasBirthdate() const {
  return false;
}

bool EcBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool EcBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

// The original1461E stack guard has a destructor interface and one actual critical-section pointer.
class EcGraphicsLockScope {
public:
  explicit EcGraphicsLockScope(ScLock *lock);
  virtual ~EcGraphicsLockScope();

private:
  ScLock *lock;
};

EcGraphicsLockScope::EcGraphicsLockScope(ScLock *lock) : lock(lock) {
  lock->Lock();
}

EcGraphicsLockScope::~EcGraphicsLockScope() {
  lock->Unlock();
}

// Invalidate only the attribute cache, use the selected definition's actual instance methods and keep failure outputs
// live.
void EcBuilding::RefreshSprite() {
  EcGraphicsLockScope scope(EcGetCity()->GetGraphicsCriticalSection());
  cached_attributes = nullptr;
  SpDefinition *definition = GetSpriteAttrib();
  if (!definition)
    return;
  if (sprite) {
    definition->DestroyInstance(&sprite);
    sprite = nullptr;
  }
  if (definition->CreateInstance(&sprite))
    sprite->SetRotationOffset(u8(position >> kOcPositionRotationShift));
}

// Replace the complete occupancy word with the input's low byte. Changed values always invoke virtual sprite refresh.
void EcBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancy_flags != state) {
    occupancy_flags = state;
    RefreshSprite();
  }
}

// Add the requested occupancy bits. Refresh only when the word changes and the input includes a low-byte bit.
void EcBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags | mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Remove the requested occupancy bits. High-byte-only changes intentionally leave the original sprite untouched.
void EcBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags & ~mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Restore generic occupant data before requiring the descriptor's raw state word. Optional name/date failures are ignored;
// a successful raw state load uses the original OR setter, and birthdate outputs are year first, then ordinal day.
bool EcBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !EcOccupant::Init(city, info) || !info->HasLayerData1())
    return false;
  u32 state;
  if (!info->GetLayerData1(&state))
    return false;
  SetOccupancyStateFlag(state);
  if (info->HasNameField()) {
    ScString name;
    if (info->GetNameField(&name))
      SetName(name);
  }
  if (info->HasSimCreateDate()) {
    i32 year = 0, day = 0;
    if (info->GetSimCreateDate(&year, &day)) {
      EcDate date(day, year);
      SetBirthdate(date);
    }
  }
  return true;
}

// Save generic occupant fields before optional custom names and construction dates. Publish a name only when its
// actual generic default exists and differs, then store the live raw occupancy word after all callbacks.
bool EcBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!EcOccupant::Save(city, info))
    return false;
  if (IsNameChangeable()) {
    ScString name;
    if (GetName(&name)) {
      ScString default_name;
      if (attributes && attributes->GetName(&default_name) && !name.IsEqual(&default_name))
        info->SetNameField(&name);
    }
  }
  if (HasBirthdate()) {
    EcDate date;
    if (GetBirthdate(&date)) {
      u32 day = date.DayOfYear(), year = date.Year();
      info->SetSimCreateDate(year, day);
    }
  }
  info->SetLayerData1(occupancy_flags);
  return true;
}

// Retire the agent type before clearing both borrowed caches. Delegate directly to occupant cleanup after those stores.
void EcBuilding::Shutdown() {
  ScRelease(&agent_type);
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  EcOccupant::Shutdown();
}

static ScString EcOccupantQueryText(u32 instance, u32 group) {
  EcTextRef resource(instance, group);
  return resource.CopyString();
}

// Route the four reached query notifications through actual queried interfaces and private virtual handlers. Always return
// false after releasing the live QI output, including when a handler successfully populates the query.
bool EcOccupant::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  ScMsgCode type = message->type;
  if (type != kWnMsg_QueryOpened && type != kWnMsg_QueryClosed)
    return false;
  u32 kind = u32(message->data1);
  if (kind == kUiQueryTagDialog) {
    UiQueryWindow *query = nullptr;
    if (sender && sender->QueryInterface(IID_UiQueryWindow, reinterpret_cast<void **>(&query))) {
      if (type == kWnMsg_QueryOpened)
        QueryBasic(message, query);
      else
        QueryResponse(message, query);
      query->Release();
    }
  } else if (type == kWnMsg_QueryOpened && (kind == kUiQueryTagDebug || kind == kUiQueryTagStandard)) {
    UiQuery *query = nullptr;
    if (sender && sender->QueryInterface(IID_UiQuerySection, reinterpret_cast<void **>(&query))) {
      if (kind == kUiQueryTagDebug)
        QueryDetail(message, query);
      else
        QueryTile(message, query);
      query->Release();
    }
  }
  return false;
}

u8 EcOccupant::QueryBasic(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 EcOccupant::QueryDetail(ScMessage *, UiQuery *) {
  return 0;
}

u8 EcOccupant::QueryResponse(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Publish the nonempty architect from the borrowed override attribute, ignoring its Boolean getter result. Retire the
// localized label resource before adding the row, and preserve the original all-category mask and no-warning byte.
u8 EcOccupant::QueryTile(ScMessage *, UiQuery *query) {
  GeAttrib *attrib = GetAttribData();
  if (!attrib)
    return 0;
  OcOverride *override_attrib = attrib->GetOverRideAttrib();
  if (!override_attrib)
    return 0;
  ScString architect;
  override_attrib->GetArchitectName(&architect);
  if (!architect.Strlen())
    return 0;
  ScString label = EcOccupantQueryText(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

bool EcBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return EcOccupant::DoQueryInfo(message, sender);
}

// Select the first matching occupancy status and publish it before the option row. The checked state is captured before
// the font lookup; its pending third argument was incorrectly inferred as an extra resource-getter argument by Hex-Rays.
u8 EcBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  ScString status;
  u32 instance = 0;
  if (GetOccupancyStateFlag(kGeBuildingCoveredInGarbage))
    instance = 198;
  else if (GetOccupancyStateFlag(kGeBuildingAbandoned))
    instance = 197;
  else if (GetOccupancyStateFlag(kGeBuildingUnderConstruction))
    instance = 196;
  else if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16))
    instance = 199;
  if (instance) {
    {
      EcTextRef resource(instance, kRsCityServiceTextGroup);
      status = resource.CopyString();
    }
    query->SetStatusText(status);
  }
  ScString caption = EcOccupantQueryText(kRsCityServiceMakeHistoricalText, kRsCityServiceTextGroup);
  WnResources *resources = EcGetWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  query->AddOption(caption, font, checked);
  return EcOccupant::QueryBasic(message, query);
}

// For the two inactive occupancy flags, add the signed wrapped footprint cost using the actual money utility. Missing
// language utility returns immediately; absent budget skips the cost row but still permits the base architect row.
u8 EcBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *utility = EcGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    EcBudgetLayer *budget = EcGetCity()->BudgetLayer();
    if (budget) {
      u32 amount = u32(budget->GetCost(kEcCostDemolish));
      amount *= SizeInCellsZ();
      amount *= SizeInCellsX();
      ScString label = EcOccupantQueryText(kRsCityServiceCleanUpCostText, kRsCityServiceTextGroup);
      ScString formatted = EcOccupantQueryText(kRsCityServiceCleanUpCostFormatText, kRsCityServiceTextGroup);
      ScString number;
      utility->MakeMoneyString(i32(amount), &number);
      EcFormatText(&formatted, formatted.Data(), number.Data());
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return EcOccupant::QueryTile(message, query);
}

u8 EcBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return EcOccupant::QueryDetail(message, query);
}

// Apply the query option's current checked byte through the real occupancy mutators, then invoke the empty base response.
u8 EcBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (query->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return EcOccupant::QueryResponse(message, query);
}

static i32 EcCommercialInitialConnections = kEcCommercialDefaultInitialConnections;
static i32 EcIndustrialInitialConnections = 85000;
static i32 EcIndustrialWaterEffect = 10, EcIndustrialPowerEffect = 10;

EcCommercial::EcCommercial() : city(nullptr), demand(nullptr), commercial(nullptr), connections(nullptr) {
}

EcCommercial::~EcCommercial() {
  Shutdown();
}

// Return the primary sector or secondary city-layer identity and retain the complete receiver.
bool EcCommercial::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcCommercialLayer);
  HANDLE_QUERY_INTERFACE(IID_EcCommercialLayer, EcCommercialLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcCommercial::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcCommercial::Release() {
  return EcCommandOwner::Release();
}

u8 EcCommercial::GetDevelopmentCap() {
  return commercial->GetDemandCap();
}

bool EcCommercial::DoMessage(ScMessage *) {
  return false;
}

bool EcCommercial::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool EcCommercial::StaticShutdown() {
  return true;
}

bool EcCommercial::SimulationBegin() {
  return true;
}

bool EcCommercial::SimulationEnd() {
  return true;
}

// Release the city, demand layer and sector dependencies in their original field order.
bool EcCommercial::Shutdown() {
  ScRelease(&city);
  ScRelease(&demand);
  ScRelease(&commercial);
  ScRelease(&connections);
  return true;
}

u32 EcCommercial::GetLayerType() {
  return kEcCommercialLayerType;
}

bool EcCommercial::DebugClassTag(ScStringView *) {
  return false;
}

bool EcCommercial::DebugTypeTag(ScStringView *) {
  return false;
}

EcIndustrial::EcIndustrial()
    : city(nullptr), demand(nullptr), ordinances(nullptr), industrial(nullptr), connections(nullptr) {
}

EcIndustrial::~EcIndustrial() {
  Shutdown();
}

// Return the primary sector or secondary city-layer identity and retain the complete receiver.
bool EcIndustrial::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, EcIndustrialLayer);
  HANDLE_QUERY_INTERFACE(IID_EcIndustrialLayer, EcIndustrialLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcIndustrial::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcIndustrial::Release() {
  return EcCommandOwner::Release();
}

u8 EcIndustrial::GetDevelopmentCap() {
  return industrial->GetDemandCap();
}

bool EcIndustrial::DoMessage(ScMessage *) {
  return false;
}

bool EcIndustrial::DoQueryInfo(ScMessage *, ScUnknown *) {
  return true;
}

bool EcIndustrial::StaticShutdown() {
  return true;
}

bool EcIndustrial::SimulationBegin() {
  return true;
}

bool EcIndustrial::SimulationEnd() {
  return true;
}

// Release the city, demand layer and sector dependencies in their original field order.
bool EcIndustrial::Shutdown() {
  ScRelease(&city);
  ScRelease(&demand);
  ScRelease(&ordinances);
  ScRelease(&industrial);
  ScRelease(&connections);
  return true;
}

u32 EcIndustrial::GetLayerType() {
  return kEcIndustrialLayerType;
}

bool EcIndustrial::DebugClassTag(ScStringView *) {
  return false;
}

bool EcIndustrial::DebugTypeTag(ScStringView *) {
  return false;
}

bool EcCommercial::StaticInit(CtDefinition *) {
  return LoadTuning() != 0;
}

// Load the commercial layer's initial connection supply from the installed loose or packaged tuning file.
u8 EcCommercial::LoadTuning() {
  ScString path, value;
  EcIni ini;
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SC3ComLayer.ini");
  ini.SelectFile(&path);
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(path);
  ScString section("TuningParameters"), key("InitialConnectionSupply");
  if (ini.GetValue(&section, &key, &value)) {
    EcCommercialInitialConnections = i32(EcParseTuningNumber(value.Data()));
    if (EcCommercialInitialConnections < 0)
      EcCommercialInitialConnections = 0;
  }
  return 1;
}

bool EcIndustrial::StaticInit(CtDefinition *) {
  return LoadTuning() != 0;
}

// Load initial industrial connections and both original conservation ordinance coefficients.
u8 EcIndustrial::LoadTuning() {
  ScString path, value;
  EcIni ini;
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("Sys\\SC3IndLayer.ini");
  ini.SelectFile(&path);
  EcGetRciApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  ini.SelectPackage(path);
  ScString section("TuningParameters"), key("InitialConnectionSupply");
  if (ini.GetValue(&section, &key, &value)) {
    EcIndustrialInitialConnections = i32(EcParseTuningNumber(value.Data()));
    if (EcIndustrialInitialConnections < 0)
      EcIndustrialInitialConnections = 0;
  }
  key.FromChar("WaterConservationOrdEffect");
  if (ini.GetValue(&section, &key, &value))
    EcIndustrialWaterEffect = i32(EcParseTuningNumber(value.Data()));
  key.FromChar("PowerConservationOrdEffect");
  if (ini.GetValue(&section, &key, &value))
    EcIndustrialPowerEffect = i32(EcParseTuningNumber(value.Data()));
  return 1;
}

// Retain the city and demand layer, then create and initialize the commercial and external-connection valves.
// Any failed acquisition runs the original complete shutdown path before returning false.
bool EcCommercial::Init(CtCity *owner) {
  city = ScAddRefNotNull(owner);
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  if (!layer || !layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand))) {
    demand = nullptr;
    Shutdown();
    return false;
  }
  if (!demand->CreateNewValve(kEcCommercialValveId, IID_EcValve, reinterpret_cast<void **>(&commercial))) {
    Shutdown();
    return false;
  }
  commercial->SetValveMax(2000);
  commercial->SetValveMin(-2000);
  if (!demand->CreateNewValve(kEcCommercialConnectionsValveId, IID_EcValve, reinterpret_cast<void **>(&connections))) {
    Shutdown();
    return false;
  }
  connections->SetValveMax(INT32_MAX);
  connections->SetValveMin(i32(kScIntegerSignBit));
  connections->SetInitialSupplyValue(EcCommercialInitialConnections);
  return true;
}

// Retain the city, ordinance service and demand layer before creating industrial and connection valves.
// Early missing-layer failures preserve the original partial ownership; failed valve creation invokes Shutdown.
bool EcIndustrial::Init(CtCity *owner) {
  city = ScAddRefNotNull(owner);
  CtLayer *layer = city->GetSpecificLayer(kEcValveLayerType);
  if (!layer) {
    demand = nullptr;
    return false;
  }
  ordinances = city->OrdinanceLayer();
  if (!ordinances)
    return false;
  ordinances->AddRef();
  if (!layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&demand))) {
    demand = nullptr;
    return false;
  }
  if (!demand->CreateNewValve(kEcIndustrialValveId, IID_EcValve, reinterpret_cast<void **>(&industrial))) {
    Shutdown();
    return false;
  }
  industrial->SetValveMax(2000);
  industrial->SetValveMin(-2000);
  if (!demand->CreateNewValve(kEcIndustrialConnectionsValveId, IID_EcValve, reinterpret_cast<void **>(&connections))) {
    Shutdown();
    return false;
  }
  connections->SetValveMax(INT32_MAX);
  connections->SetValveMin(i32(kScIntegerSignBit));
  connections->SetInitialSupplyValue(EcIndustrialInitialConnections);
  return true;
}

bool EcCommercial::Init(CtCity *, ScImporter *importer) {
  u32 value = 0;
  if (importer->MapIsPresent(kCtLegacyMiscPlane)) {
    if (!importer->GetMiscValue(455, reinterpret_cast<i32 *>(&value)))
      value = 0;
    if (i32(value) < 0) {
      i32 product = i32(value * u32(commercial->QueryValveMin()));
      commercial->SetSupplyValue(product / 20000);
      commercial->SetDemandValue(0);
      return true;
    }
  }
  i32 product = i32(value * u32(commercial->QueryValveMax()));
  commercial->SetSupplyValue(0);
  commercial->SetDemandValue(product / 20000);
  return true;
}

// Import the legacy industrial supply or demand using its corresponding property and live valve bounds.
bool EcIndustrial::Init(CtCity *, ScImporter *importer) {
  u32 value = 0;
  if (importer->MapIsPresent(kCtLegacyMiscPlane)) {
    if (!importer->GetMiscValue(456, reinterpret_cast<i32 *>(&value)))
      value = 0;
    if (i32(value) < 0) {
      i32 product = i32(value * u32(industrial->QueryValveMin()));
      industrial->SetSupplyValue(product / 20000);
      industrial->SetDemandValue(0);
      return true;
    }
  }
  i32 product = i32(value * u32(industrial->QueryValveMax()));
  industrial->SetSupplyValue(0);
  industrial->SetDemandValue(product / 20000);
  return true;
}

void EcCommercial::UpdateDemandCap() {
  i32 supply = connections->QuerySupplyValue(), demand_value = connections->QueryDemandValue();
  u8 cap = demand_value ? (supply >= demand_value ? u8(i32(100u * u32(demand_value)) / supply) : 100) : 0;
  commercial->SetDemandCap(cap);
}

// Update the industrial cap, then apply the power and water conservation effects to positive live demand.
void EcIndustrial::UpdateDemandCap() {
  i32 supply = connections->QuerySupplyValue(), demand_value = connections->QueryDemandValue();
  u8 cap = demand_value ? (supply >= demand_value ? u8(i32(100u * u32(demand_value)) / supply) : 100) : 0;
  industrial->SetDemandCap(cap);
  i32 value = industrial->QueryDemandValue();
  if (value > 0) {
    if (ordinances->QueryOrdinanceOn(kCtOrdinance_PowerConservation))
      industrial->AddToDemandValue(i32(u32(value) * u32(EcIndustrialPowerEffect)) / -100);
    if (ordinances->QueryOrdinanceOn(kCtOrdinance_WaterConservation))
      industrial->AddToDemandValue(i32(u32(value) * u32(EcIndustrialWaterEffect)) / -100);
  }
}

EcCommercialManipulator::EcCommercialManipulator() : owner(nullptr) {
}

EcCommercialManipulator::~EcCommercialManipulator() {
  Shutdown();
}

// Publish the same primary identity for Unknown and the actual city-layer manipulator IID.
bool EcCommercialManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcCommercialManipulator::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcCommercialManipulator::Release() {
  return EcCommandOwner::Release();
}

bool EcCommercialManipulator::Apply(CtAgent *) {
  owner->UpdateDemandCap();
  return true;
}

bool EcCommercialManipulator::IsLockingCity() {
  return false;
}

bool EcCommercialManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcCommercialManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcCommercialManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

u8 EcCommercialManipulator::Init(EcCommercial *value) {
  owner = ScAddRefNotNull(value);
  return 1;
}

u8 EcCommercialManipulator::Shutdown() {
  ScRelease(&owner);
  return 1;
}

// Return flag four and construct the original sector manipulator only for the matching simulation agent type.
bool EcCommercial::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    EcCommercialManipulator *manipulator = new EcCommercialManipulator;
    manipulator->Init(this);
    *out = manipulator;
  }
  return true;
}

EcIndustrialManipulator::EcIndustrialManipulator() : owner(nullptr) {
}

EcIndustrialManipulator::~EcIndustrialManipulator() {
  Shutdown();
}

// Publish the same primary identity for Unknown and the actual city-layer manipulator IID.
bool EcIndustrialManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 EcIndustrialManipulator::AddRef() {
  return EcCommandOwner::AddRef();
}

u32 EcIndustrialManipulator::Release() {
  return EcCommandOwner::Release();
}

bool EcIndustrialManipulator::Apply(CtAgent *) {
  owner->UpdateDemandCap();
  return true;
}

bool EcIndustrialManipulator::IsLockingCity() {
  return false;
}

bool EcIndustrialManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool EcIndustrialManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool EcIndustrialManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

u8 EcIndustrialManipulator::Init(EcIndustrial *value) {
  owner = ScAddRefNotNull(value);
  return 1;
}

u8 EcIndustrialManipulator::Shutdown() {
  ScRelease(&owner);
  return 1;
}

// Return flag four and construct the original sector manipulator only for the matching simulation agent type.
bool EcIndustrial::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *flags = 4;
  *out = nullptr;
  if (type.Id() == kCtAgentType_AreaAgent) {
    EcIndustrialManipulator *manipulator = new EcIndustrialManipulator;
    manipulator->Init(this);
    *out = manipulator;
  }
  return true;
}

// Write the two actual sector valves and their identifiers into the original version-two framed record.
// Abort the record at the first failed stream or valve operation.
bool EcCommercial::Save(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcCommercialClassId, 0}, 2, 0);
  u8 result = frame.good;
  RsStream *stream = frame.GetStream();
  result = result && stream->WriteU32(2) && stream->WriteU32(commercial->GetId()) &&
      commercial->SerialWrite(owner, stream) && stream->WriteU32(connections->GetId());
  if (result)
    result = connections->SerialWrite(owner, stream);
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Retry a failed commercial frame once, then restore the present valve records in original order.
bool EcCommercial::Init(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame *frame =
      new EcRci::EcArchiveFrame(owner, segment, RsKey{kRsCityLayerRecordType, kEcCommercialClassId, 0});
  if (!frame->good) {
    delete frame;
    frame = new EcRci::EcArchiveFrame(owner, segment, RsKey{kRsCityLayerRecordType, kEcCommercialClassId, 0});
  }
  u8 result = frame->good;
  RsStream *stream = frame->GetStream();
  if (!frame->version)
    frame->text_flag = 0;
  if (result && frame->text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  u32 count = 0, identifier = 0;
  if (result)
    result = stream->ReadU32(&count);
  if (result && count) {
    result = stream->ReadU32(&identifier);
    if (result)
      result = commercial->SerialRead(owner, stream);
  }
  if (result && count > 1) {
    result = stream->ReadU32(&identifier);
    if (result)
      result = connections->SerialRead(owner, stream);
  }
  if (result && frame->version > 1 && frame->text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  if (!result)
    frame->MarkFailed();
  delete frame;
  return result != 0;
}

// Write the two actual sector valves and their identifiers into the original version-two framed record.
// Abort the record at the first failed stream or valve operation.
bool EcIndustrial::Save(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcIndustrialClassId, 0}, 2, 0);
  u8 result = frame.good;
  RsStream *stream = frame.GetStream();
  result = result && stream->WriteU32(2) && stream->WriteU32(industrial->GetId()) &&
      industrial->SerialWrite(owner, stream) && stream->WriteU32(connections->GetId());
  if (result)
    result = connections->SerialWrite(owner, stream);
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Restore the present industrial and connection valves from the original versioned framed record.
bool EcIndustrial::Init(CtCity *owner, RsDatabaseSegment *segment) {
  EcRci::EcArchiveFrame frame(owner, segment, RsKey{kRsCityLayerRecordType, kEcIndustrialClassId, 0});
  u8 result = frame.good;
  RsStream *stream = frame.GetStream();
  if (result && frame.text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  u32 count = 0, identifier = 0;
  if (result)
    result = stream->ReadU32(&count);
  if (result && count) {
    result = stream->ReadU32(&identifier);
    if (result)
      result = industrial->SerialRead(owner, stream);
  }
  if (result && count > 1) {
    result = stream->ReadU32(&identifier);
    if (result)
      result = connections->SerialRead(owner, stream);
  }
  if (result && frame.version > 1 && frame.text_flag) {
    ScString ignored;
    result = stream->ReadString(&ignored);
  }
  if (!result)
    frame.MarkFailed();
  return result != 0;
}

// Installed zoning palette stores source RGB bytes and a shared converted native color for each zone.
struct GxRciZoneColor {
  u8 red, green, blue;
  u32 native;
};

static GxRciZoneColor GxRciZoneColors[23] = {{255, 255, 255, 0}, {0, 250, 0, 0}, {0, 200, 0, 0}, {0, 150, 0, 0},
    {0, 100, 0, 0}, {0, 0, 250, 0}, {0, 0, 200, 0}, {0, 0, 150, 0}, {0, 0, 100, 0}, {250, 250, 0, 0}, {200, 200, 0, 0},
    {150, 150, 0, 0}, {100, 100, 0, 0}, {255, 255, 255, 0}, {51, 204, 255, 0}, {51, 255, 153, 0}, {255, 255, 255, 0},
    {153, 102, 51, 0}, {255, 255, 255, 0}, {255, 255, 255, 0}, {255, 255, 255, 0}, {255, 255, 255, 0}, {239, 95, 0, 0}};

// Retain the source map, initialize raster dimensions and convert every shared RGB palette entry in source order.
GxRciZoneMap::GxRciZoneMap(EcByteMap *source) : map(source) {
  map->AddRef();
  u32 map_height = map->CellCountZ(), map_width = map->CellCountX();
  SetId(kUiZoningMapRasterId);
  SetBaseSize(map_width, map_height);
  for (u32 index = 0; index < 23; ++index) {
    GxRciZoneColor *color = &GxRciZoneColors[index];
    color->native = EcGetRciGraphics()->ConvertRGBValueToNative16Bit(color->red, color->green, color->blue);
  }
}

GxRciZoneMap::~GxRciZoneMap() {
  ScRelease(&map);
}

// Publish the converted color for a nonzero zone below 23, leaving the output untouched otherwise.
bool GxRciZoneMap::GetColor(u32 x, u32 z, u32 *out) {
  u8 zone;
  map->GetValue(x, z, &zone);
  if (!zone || zone >= 23)
    return false;
  *out = GxRciZoneColors[zone].native;
  return true;
}

GxRciMapSprite::GxRciMapSprite() : references(0), kind(0), palette(nullptr), width(0), height(0), enabled(0) {
  GxRciMapSprite::SetDestinationCoords(0, 0, 0, 1, 1, 0);
}

GxRciMapSprite::~GxRciMapSprite() {
  ScRelease(&palette);
}

// Publish the actual unknown/raster identity before the virtual
// retain, clearing the output.
bool GxRciMapSprite::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, GxRciMapSprite);
  HANDLE_QUERY_INTERFACE(IID_GxRasterSource, GxRciMapSprite);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GxRciMapSprite::AddRef() {
  return ++references;
}

u32 GxRciMapSprite::Release() {
  return ScReleaseImpl(this, &references);
}

// Set the raster kind before its dimensions through the actual virtual entries, ignoring both setter replies.
bool GxRciMapSprite::Init(u32 value, u32 x, u32 z) {
  SetId(value);
  SetBaseSize(x, z);
  return true;
}

u32 GxRciMapSprite::GetId() const {
  return kind;
}

bool GxRciMapSprite::SetId(u32 value) {
  kind = value;
  return true;
}

// Retain the incoming palette before inspecting and releasing the current palette, then publish the new owner.
bool GxRciMapSprite::SetPalette(GxPaletteView *source) {
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
GxPaletteView *GxRciMapSprite::GetPalette() {
  return palette;
}

i32 GxRciMapSprite::GetBaseWidth() const {
  return width;
}

i32 GxRciMapSprite::GetBaseHeight() const {
  return width;
}

// Publish both original unsigned dimension arguments as their stored DWORD bits and report success.
bool GxRciMapSprite::SetBaseSize(u32 x, u32 z) {
  width = i32(x);
  height = i32(z);
  return true;
}

u8 GxRciMapSprite::SetPostFilter(u8 value) {
  enabled = value;
  return value;
}

// Store three defining corners and derive the fourth in the source's nonsequential write order.
void GxRciMapSprite::SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) {
  corner_x[1] = x1;
  corner_x[0] = x0;
  corner_z[1] = z1;
  corner_x[2] = i32(u32(x3) + u32(x1) - u32(x0));
  corner_z[0] = z0;
  corner_x[3] = x3;
  corner_z[2] = i32(u32(z3) + u32(z1) - u32(z0));
  corner_z[3] = z3;
}

u8 GxRciMapSprite::IsPostFilter() const {
  return enabled;
}

// Require a destination, run the virtual pixel pass, and invoke completion only when that pass reports success.
bool GxRciMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
  if (!destination)
    return false;
  if (!DrawCells(destination, rectangle, clip))
    return false;
  return DrawAnnotations(destination, rectangle, clip);
}

u8 GxRciMapSprite::DebugInstanceTag(ScStringView *) {
  return 0;
}

u8 GxRciMapSprite::DebugClassTag(ScStringView *) {
  return 0;
}

u8 GxRciMapSprite::DebugTypeTag(ScStringView *) {
  return 0;
}

bool GxRciMapSprite::GetColor(u32, u32, u32 *) {
  return false;
}

// Scale columns before rows with a vertical source flip and destination clipping.
// Wrap DWORD coordinate arithmetic before signed scaling, matching the installed SUB/IMUL/IDIV sequence.
bool GxRciMapSprite::DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) {
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

bool GxRciMapSprite::DrawAnnotations(GxBuffer *, const ScRect &, ScRect *) {
  return true;
}

u8 GxRciMapSprite::SetDebugInstanceTag(const ScStringView &) {
  return 0;
}

u8 GxRciMapSprite::SetDebugClassTag(const ScStringView &) {
  return 0;
}

GxRciDemandMap::GxRciDemandMap(CtCity *owner, EcDemand *source) : ready(false), layer(nullptr), city(owner) {
  city->AddRef();
  layer = ScAddRefNotNull(source);
  u32 cells_z = city->CellCountZ(), cells_x = city->CellCountX();
  SetId(kUiPopulationMapRasterId);
  SetBaseSize(cells_x, cells_z);
  if (layer && city)
    ready = true;
}

GxRciDemandMap::~GxRciDemandMap() {
  ScRelease(&layer);
  ScRelease(&city);
}

// Convert the working demand density to the original inverse palette index for this map cell.
// A missing palette or an unready map declines the color request without writing the result.
bool GxRciDemandMap::GetColor(u32 x, u32 z, u32 *out) {
  if (!ready)
    return false;
  u8 density = 0;
  layer->GetDensity(x, z, &density);
  if (density > 160)
    density = 160;
  GxPaletteView *colors = GetPalette();
  if (!colors)
    return false;
  i32 count = colors->GetNumberEntries();
  i32 index = i32(f64(count) * (f64(160 - density) * 0.00625));
  return colors->GetColor(index, out);
}

// Construct the density map for its original kind and publish only a requested supported identity.
u8 EcDemand::CreateRasterSource(u32 kind, u32 iid, void **out) {
  if (kind != kUiPopulationMapRasterId)
    return 0;
  GxRciDemandMap *source = new GxRciDemandMap(city, this);
  if (source->QueryInterface(iid, out))
    return 1;
  delete source;
  return 0;
}
