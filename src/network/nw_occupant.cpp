#include "network/nw_occupant.h"

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "geometry/api.h"
#include "network/api.h"
#include "network/nw_module.h"
#include "network/nw_records.h"
#include "network/street_traffic.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "ui/api.h"

// Connection setters retain unrelated bits; absent name and query keys use the original missing-text instance.
enum NwPieceAttributeConstant : u32 {
  kNwConnectionWithoutFlagZero = 0xfffffffe,
  kNwConnectionWithoutFlagOne = 0xfffffffd,
  kNwMissingTextResourceInstance = 0xffffffff,
};

// Concrete network-piece variants return these exact default geometry words, including crossing memberships.
enum NwPieceGeometry : u32 {
  kNwRoadPieceGeometry = 0xc804,
  kNwHighwayPieceGeometry = 0x14804,
  kNwRailPieceGeometry = 0x24804,
  kNwSubwayPieceGeometry = 0x44804,
  kNwRoadRailPieceGeometry = 0x2c804,
  kNwRoadHighwayPieceGeometry = 0x1c804,
  kNwRailHighwayPieceGeometry = 0x34804,
  kNwRailSubwayPieceGeometry = 0x64804,
  kNwPipePieceGeometry = 0x800804,
  kNwPowerPieceGeometry = 0x400804,
  kNwRoadPowerPieceGeometry = 0x40c804,
  kNwRailPowerPieceGeometry = 0x424804,
  kNwHighwayPowerPieceGeometry = 0x414804,
  kNwRoadHighwayOnRampPieceGeometry = 0x21c804,
  kNwHighwayOnRampPieceGeometry = 0x214804,
  kNwWireBridgePieceGeometry = 0x480804,
  kNwRoadTunnelPieceGeometry = 0x10c804,
  kNwRailTunnelPieceGeometry = 0x124804,
  kNwHighwayTunnelPieceGeometry = 0x114804,
};

// Network piece data packs its tile identity, damage state and optional teleport direction and length.
enum NwPiecePackedField : u32 {
  kNwPieceWithoutDamage = 0xfffeffff,
  kNwPieceWithoutTileId = 0xffff0000,
  kNwPieceWithoutTeleportData = 0xf003ffff,
  kNwPieceWithoutTeleportFlag = 0xfffdffff,
  kNwPieceWithoutTeleportLength = 0xf00fffff,
  kNwPieceWithoutTeleportDirection = 0xfff3ffff,
  kNwPieceDamageFlag = 0x10000,
  kNwPieceTeleportFlag = 0x20000,
  kNwPieceDamageShift = 16,
  kNwPieceTeleportDirectionShift = 18,
  kNwPieceTeleportLengthShift = 20,
  kNwPieceTileIdMask = 0xffff,
};

bool NwAttrib::GetOccupantGZCLSID(u32 *) const {
  return false;
}

bool NwAttrib::SetOccupantGZCLSID(u32) {
  return false;
}

bool NwAttrib::SetDefaultLayerFlag(u32) {
  return false;
}

bool NwAttrib::DebugInstanceTag(ScStringView *) {
  return false;
}

bool NwAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool NwAttrib::DebugTypeTag(ScStringView *) {
  return false;
}

NwAttrib::NwAttrib()
    : occupantClass(0), geometryFlags(0), sizeX(0), sizeZ(0), sizeY(0), agentType(nullptr), primary{},
      peopleAttrib(nullptr), vehicleAttrib(nullptr), peopleKey{}, vehicleKey{}, soundA{}, soundB{}, cachedLayers(0),
      variants{}, nameKey{}, plopKey{}, queryKey{}, propertyFlag(0), overrideAttrib(nullptr) {
}

// Release the override and cached resources in original order. Clear each retained pointer after its Release before
// destroying the name and resource base.
NwAttrib::~NwAttrib() {
  ScRelease(&overrideAttrib);
  if (primary.resource) {
    static_cast<ScUnknown *>(primary.resource)->Release();
    primary.resource = nullptr;
  }
  if (peopleAttrib) {
    static_cast<ScUnknown *>(peopleAttrib)->Release();
    peopleAttrib = nullptr;
  }
  if (vehicleAttrib) {
    static_cast<ScUnknown *>(vehicleAttrib)->Release();
    vehicleAttrib = nullptr;
  }
  for (u32 i = 0; i < 7; ++i)
    if (variants[i].resource) {
      static_cast<ScUnknown *>(variants[i].resource)->Release();
      variants[i].resource = nullptr;
    }
}

// Expose the actual adjusted attribute and property subobjects. Other identities use the retained resource base query.
bool NwAttrib::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GePropertyObject, GePropertyObject);
  HANDLE_QUERY_INTERFACE(IID_GeAttrib, GeAttrib);
  return RsResource::QueryInterface(iid, out);
}

u32 NwAttrib::AddRef() {
  return RsResource::AddRef();
}

u32 NwAttrib::Release() {
  return RsResource::Release();
}

bool NwAttrib::IsGeomFlag(u32 mask) {
  return mask == (mask & geometryFlags);
}

u8 NwAttrib::SetGeomFlag(u32 mask) {
  geometryFlags |= mask;
  return true;
}

u8 NwAttrib::ClearGeomFlag(u32 mask) {
  geometryFlags &= ~mask;
  return true;
}

u32 NwAttrib::GetGeomFlag() {
  return geometryFlags;
}

// Cache the five-layer mask from the corresponding resource-key type words. A zero result is recomputed on the next
// call.
u8 NwAttrib::GetOccLayerFlag() {
  if (!cachedLayers) {
    cachedLayers = primary.key.type != 0;
    for (u32 i = 0; i < 4; ++i)
      if (variants[i].key.type)
        cachedLayers |= u8(2u << i);
  }
  return cachedLayers;
}

void NwAttrib::SetOccLayerFlag(u8) {
}

u8 NwAttrib::MajorType() const {
  return 0;
}

u8 NwAttrib::SubTypeInfo() const {
  return 0;
}

u32 NwAttrib::SizeInCellsX() {
  return sizeX;
}

u32 NwAttrib::SizeInCellsZ() {
  return sizeZ;
}

u32 NwAttrib::SizeInCellsY() {
  return sizeY;
}

u32 NwAttrib::TypeId() const {
  return occupantClass;
}

u32 NwAttrib::GetDefaultLayerFlag() const {
  return 1;
}

u8 NwAttrib::Flammability() {
  return flammability;
}

// Let a retained override supply the display name when present. Otherwise copy the stored name and report success.
bool NwAttrib::GetName(ScStringView *out) {
  if (overrideAttrib)
    return overrideAttrib->GetName(out);
  out->Copy(name);
  return true;
}

bool NwAttrib::GetArchitectName(ScStringView *out) {
  if (overrideAttrib)
    return overrideAttrib->GetArchitectName(out);
  ScString empty;
  out->Copy(empty);
  return false;
}

bool NwAttrib::GetNameKey(RsKey *out) {
  *out = nameKey;
  return true;
}

u32 NwAttrib::GetSoundObjectId(i32 *index) {
  return *index == 0 ? soundA.instance : *index == 1 ? soundB.instance : 0;
}

void NwAttrib::GetSoundKeyA(RsKey *out) {
  *out = soundA;
}

void NwAttrib::GetSoundKeyB(RsKey *out) {
  *out = soundB;
}

void NwAttrib::GetPlopCursorKey(RsKey *out) {
  *out = plopKey;
}

void NwAttrib::GetQueryDescKey(RsKey *out) {
  *out = queryKey;
}

void NwAttrib::GetQueryFormatFlags(u32 *out) {
  *out = queryFlags;
}

u32 NwAttrib::GetQueryDialogClass() {
  return queryDialogClass;
}

bool NwAttrib::SetQueryDialogClass(u32 clsid) {
  queryDialogClass = clsid;
  return true;
}

// Release the old override before storing and retaining its replacement. Preserve the original ordering even when the
// pointers are equal.
void NwAttrib::SetOverRideAttrib(OcOverride *value) {
  ScRelease(&overrideAttrib);
  overrideAttrib = value;
  if (value)
    value->AddRef();
}

// Borrow the retained override interface. This getter does not acquire a reference.
OcOverride *NwAttrib::GetOverRideAttrib() {
  return overrideAttrib;
}

u8 NwAttrib::SetPropertyFlag(u8 value) {
  propertyFlag = value;
  return value;
}

u8 NwAttrib::GetPropertyFlag() {
  return propertyFlag;
}

u8 NwAttrib::AnimType() const {
  return 0;
}

// Borrow the city-agent type resolved during property loading. This getter does not acquire a reference.
CtAgentType *NwAttrib::CityAgentType() {
  return agentType;
}

// Borrow the actual resource-base key. The original const interface exposes a mutable pointer without retaining it.
RsKey *NwAttrib::Key() const {
  return const_cast<RsKey *>(&key);
}

// Failed requests clear even a published pointer; successful requests still require a nonnull result.
static bool SC3_STDCALL NwResolveSprite(const RsKey *key, void **out) {
  if (!NwGetResourceManager()->GetResource(*key, IID_SpDefView, out))
    *out = nullptr;
  return *out != nullptr;
}

// Let an override supply indexed art, otherwise select the first supported layer bit and cache its definition.
// The retained resource slot is the actual publication output, preserving callback changes to that slot.
SpDefinition *NwAttrib::GetSpriteAttrib(u32 layer) {
  if (overrideAttrib)
    return overrideAttrib->GetSpriteAttrib(layer);
  NwCachedArt *art;
  if (layer & 1)
    art = &primary;
  else if (layer & 2)
    art = &variants[0];
  else if (layer & 4)
    art = &variants[1];
  else
    return nullptr;
  if (art->resource || NwResolveSprite(&art->key, &art->resource))
    return static_cast<SpDefinition *>(art->resource);
  return nullptr;
}

// Borrow the primary art independently of an override, acquiring it only while its retained cache is empty.
SpDefinition *NwAttrib::GetOriginalSpriteAttrib() {
  if (primary.resource || NwResolveSprite(&primary.key, &primary.resource))
    return static_cast<SpDefinition *>(primary.resource);
  return nullptr;
}

// Borrow an override's unindexed art when present; otherwise dispatch the original-art getter virtually.
SpDefinition *NwAttrib::GetSpriteAttrib() {
  return overrideAttrib ? overrideAttrib->GetSpriteAttrib() : GetOriginalSpriteAttrib();
}

// Borrow the cached people producer, loading its live key on the first missing lookup.
iptr NwAttrib::GetPeopleProducerAttrib() {
  if (!peopleAttrib && !LoadPeople(peopleKey))
    return 0;
  return reinterpret_cast<iptr>(peopleAttrib);
}

// Borrow the cached vehicle producer after resolving a missing entry through the current application.
// A failed acquisition leaves the cache empty so subsequent calls retry.
iptr NwAttrib::GetVehicleProducerAttrib() {
  if (!vehicleAttrib && !LoadVehicle(vehicleKey))
    return 0;
  return reinterpret_cast<iptr>(vehicleAttrib);
}

// Resolve the people key through a freshly queried application and release that temporary interface before loading.
u8 NwAttrib::LoadPeople(const RsKey &resourceKey) {
  if (resourceKey.instance) {
    void *application = nullptr;
    if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, &application)) {
      RsManager *resources = static_cast<ApGameInterface *>(application)->GetResourceManager();
      static_cast<ApGameInterface *>(application)->Release();
      if (!resources->GetResource(resourceKey, IID_NwPeopleProducerAttribInterface, &peopleAttrib))
        peopleAttrib = nullptr;
    } else
      peopleAttrib = nullptr;
  } else
    peopleAttrib = nullptr;
  return 1;
}

u8 NwAttrib::LoadVehicle(const RsKey &resourceKey) {
  if (resourceKey.instance) {
    void *application = nullptr;
    if (NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, &application)) {
      RsManager *resources = static_cast<ApGameInterface *>(application)->GetResourceManager();
      static_cast<ApGameInterface *>(application)->Release();
      if (!resources->GetResource(resourceKey, IID_NwVehicleProducerAttrib, &vehicleAttrib))
        vehicleAttrib = nullptr;
    } else
      vehicleAttrib = nullptr;
  } else
    vehicleAttrib = nullptr;
  return 1;
}

static const RsProperty NwClassProperty{0x65, 3, 1}, NwSizeProperty{0x66, 3, 3}, NwPrimaryProperty{0x67, 8, 1},
    NwPeopleProperty{0x6b, 8, 1}, NwVehicleProperty{0x6c, 8, 1}, NwNameProperty{0x6d, 8, 1},
    NwFlammabilityProperty{0x6e, 1, 1}, NwSoundAProperty{0x6f, 8, 1}, NwSoundBProperty{0x70, 8, 1},
    NwDialogProperty{0x71, 3, 1}, NwQueryFlagsProperty{0x72, 3, 2}, NwPlopProperty{0x7b, 8, 1},
    NwQueryProperty{0x7c, 8, 1};
static const RsProperty NwVariantProperties[7] = {
    {0x74, 8, 1}, {0x75, 8, 1}, {0x76, 8, 1}, {0x77, 8, 1}, {0x78, 8, 1}, {0x79, 8, 1}, {0x7a, 8, 1}};

// Load required class and X/height/Z words before optional art, sound and query records, retaining partial outputs.
// Optional failures preserve the source's asymmetric field defaults and scoped localized-name resource ordering.
bool NwAttrib::LoadProperties(RsPropertySet *properties) {
  u8 classRead = properties->GetWord(NwClassProperty, &occupantClass, 0);
  bool failed = !classRead;
  CtAgentTree *tree = NwGetAgentTypeTree();
  if (tree) {
    agentType = tree->GetNode(occupantClass);
    if (!classRead)
      failed = true;
  } else
    failed = true;
  if (!properties->GetWord(NwSizeProperty, &sizeX, 0))
    failed = true;
  if (!properties->GetWord(NwSizeProperty, &sizeY, 1))
    failed = true;
  if (!properties->GetWord(NwSizeProperty, &sizeZ, 2))
    failed = true;
  properties->GetKey(NwPrimaryProperty, &primary.key, 0);
  if (!properties->GetKey(NwPeopleProperty, &peopleKey, 0)) {
    peopleKey.instance = 0;
    peopleKey.group = 0;
    peopleKey.type = 0;
  }
  properties->GetKey(NwVehicleProperty, &vehicleKey, 0);
  if (!properties->GetByte(NwFlammabilityProperty, &flammability, 0))
    flammability = 0;
  if (!properties->GetKey(NwSoundAProperty, &soundA, 0))
    soundA.instance = 0;
  if (!properties->GetKey(NwSoundBProperty, &soundB, 0))
    soundB.instance = 0;
  void *dialog;
  if (properties->GetWord(NwDialogProperty, &queryDialogClass, 0) &&
      NwGetCom()->GetClassObject(queryDialogClass, IID_UiQueryWindow, &dialog))
    static_cast<ScUnknown *>(dialog)->Release();
  else
    queryDialogClass = 1402;
  for (u32 i = 0; i < 7; ++i)
    if (!properties->GetKey(NwVariantProperties[i], &variants[i].key, 0))
      variants[i].resource = nullptr;
  if (!properties->GetKey(NwPlopProperty, &plopKey, 0))
    plopKey.instance = 0;
  if (!properties->GetKey(NwNameProperty, &nameKey, 0)) {
    nameKey.instance = kNwMissingTextResourceInstance;
    nameKey.group = kRsBuildingTextGroup;
    queryKey.type = kRsTextResourceType;
  }
  {
    NwTextRef text(nameKey.instance, nameKey.group);
    ScString raw = text.CopyString();
    ScString copied(raw);
    name = copied;
  }
  if (!properties->GetKey(NwQueryProperty, &queryKey, 0)) {
    queryKey.instance = kNwMissingTextResourceInstance;
    queryKey.group = kRsBuildingCatalogTextGroup;
    queryKey.type = kRsTextResourceType;
  }
  if (!properties->GetWord(NwQueryFlagsProperty, &queryFlags, 0))
    queryFlags = 0x5bee0;
  return !failed;
}

// Write every required tuple word and optional record in source order; only required failures affect the result.
// Conditional records inspect live fields after preceding property callbacks, without clearing omitted properties.
bool NwAttrib::SaveProperties(RsPropertySet *properties) {
  bool failed = !properties->SetWord(NwClassProperty, occupantClass, 0);
  if (!properties->SetWord(NwSizeProperty, sizeX, 0))
    failed = true;
  if (!properties->SetWord(NwSizeProperty, sizeY, 1))
    failed = true;
  if (!properties->SetWord(NwSizeProperty, sizeZ, 2))
    failed = true;
  properties->SetKey(NwPrimaryProperty, primary.key, 0);
  if (peopleKey.instance || peopleKey.group || peopleKey.type)
    properties->SetKey(NwPeopleProperty, peopleKey, 0);
  properties->SetKey(NwVehicleProperty, vehicleKey, 0);
  properties->SetKey(NwSoundAProperty, soundA, 0);
  properties->SetKey(NwSoundBProperty, soundB, 0);
  if (queryDialogClass != 1402)
    properties->SetWord(NwDialogProperty, queryDialogClass, 0);
  if (flammability)
    properties->SetByte(NwFlammabilityProperty, flammability, 0);
  for (u32 i = 0; i < 7; ++i)
    properties->SetKey(NwVariantProperties[i], variants[i].key, 0);
  properties->SetKey(NwPlopProperty, plopKey, 0);
  if (nameKey.instance || nameKey.group || nameKey.type)
    properties->SetKey(NwNameProperty, nameKey, 0);
  if (queryKey.instance || queryKey.group || queryKey.type)
    properties->SetKey(NwQueryProperty, queryKey, 0);
  if (queryFlags)
    properties->SetWord(NwQueryFlagsProperty, queryFlags, 0);
  return !failed;
}

bool NwPieceAttrib::GetOccupantGZCLSID(u32 *out) const {
  return NwAttrib::GetOccupantGZCLSID(out);
}

bool NwPieceAttrib::SetOccupantGZCLSID(u32 classId) {
  return NwAttrib::SetOccupantGZCLSID(classId);
}

// Borrow the resource key from the actual retained generic attribute base.
RsKey *NwPieceAttrib::Key() const {
  return NwAttrib::Key();
}

bool NwPieceAttrib::IsGeomFlag(u32 mask) {
  return NwAttrib::IsGeomFlag(mask);
}

u8 NwPieceAttrib::SetGeomFlag(u32 mask) {
  return NwAttrib::SetGeomFlag(mask);
}

u8 NwPieceAttrib::ClearGeomFlag(u32 mask) {
  return NwAttrib::ClearGeomFlag(mask);
}

u32 NwPieceAttrib::GetGeomFlag() {
  return NwAttrib::GetGeomFlag();
}

u8 NwPieceAttrib::GetOccLayerFlag() {
  return NwAttrib::GetOccLayerFlag();
}

void NwPieceAttrib::SetOccLayerFlag(u8 mask) {
  NwAttrib::SetOccLayerFlag(mask);
}

u8 NwPieceAttrib::SubTypeInfo() const {
  return NwAttrib::SubTypeInfo();
}

u8 NwPieceAttrib::AnimType() const {
  return NwAttrib::AnimType();
}

bool NwPieceAttrib::GetName(ScStringView *out) {
  return NwAttrib::GetName(out);
}

u32 NwPieceAttrib::SizeInCellsX() {
  return NwAttrib::SizeInCellsX();
}

u32 NwPieceAttrib::SizeInCellsZ() {
  return NwAttrib::SizeInCellsZ();
}

u32 NwPieceAttrib::SizeInCellsY() {
  return NwAttrib::SizeInCellsY();
}

// Borrow the generic attribute's retained city-agent type.
CtAgentType *NwPieceAttrib::CityAgentType() {
  return NwAttrib::CityAgentType();
}

u32 NwPieceAttrib::TypeId() const {
  return NwAttrib::TypeId();
}

// Resolve the requested sprite layer through the live generic cache.
SpDefinition *NwPieceAttrib::GetSpriteAttrib(u32 layer) {
  return NwAttrib::GetSpriteAttrib(layer);
}

// Resolve the primary sprite through the generic attribute cache.
SpDefinition *NwPieceAttrib::GetSpriteAttrib() {
  return NwAttrib::GetSpriteAttrib();
}

// Borrow the original cached sprite definition without requesting a resource.
SpDefinition *NwPieceAttrib::GetOriginalSpriteAttrib() {
  return NwAttrib::GetOriginalSpriteAttrib();
}

u32 NwPieceAttrib::GetDefaultLayerFlag() const {
  return NwAttrib::GetDefaultLayerFlag();
}

bool NwPieceAttrib::SetDefaultLayerFlag(u32 mask) {
  return NwAttrib::SetDefaultLayerFlag(mask);
}

u8 NwPieceAttrib::Flammability() {
  return NwAttrib::Flammability();
}

u32 NwPieceAttrib::GetSoundObjectId(i32 *index) {
  return NwAttrib::GetSoundObjectId(index);
}

void NwPieceAttrib::GetSoundKeyA(RsKey *out) {
  NwAttrib::GetSoundKeyA(out);
}

void NwPieceAttrib::GetSoundKeyB(RsKey *out) {
  NwAttrib::GetSoundKeyB(out);
}

void NwPieceAttrib::GetPlopCursorKey(RsKey *out) {
  NwAttrib::GetPlopCursorKey(out);
}

void NwPieceAttrib::GetQueryDescKey(RsKey *out) {
  NwAttrib::GetQueryDescKey(out);
}

void NwPieceAttrib::GetQueryFormatFlags(u32 *out) {
  NwAttrib::GetQueryFormatFlags(out);
}

u32 NwPieceAttrib::GetQueryDialogClass() {
  return NwAttrib::GetQueryDialogClass();
}

bool NwPieceAttrib::SetQueryDialogClass(u32 clsid) {
  return NwAttrib::SetQueryDialogClass(clsid);
}

bool NwPieceAttrib::DebugInstanceTag(ScStringView *out) {
  return NwAttrib::DebugInstanceTag(out);
}

bool NwPieceAttrib::DebugClassTag(ScStringView *out) {
  return NwAttrib::DebugClassTag(out);
}

bool NwPieceAttrib::DebugTypeTag(ScStringView *out) {
  return NwAttrib::DebugTypeTag(out);
}

bool NwPieceAttrib::GetNameKey(RsKey *out) {
  return NwAttrib::GetNameKey(out);
}

void NwPieceAttrib::SetOverRideAttrib(OcOverride *value) {
  NwAttrib::SetOverRideAttrib(value);
}

// Borrow the current generic occupant override pointer.
OcOverride *NwPieceAttrib::GetOverRideAttrib() {
  return NwAttrib::GetOverRideAttrib();
}

bool NwPieceAttrib::GetArchitectName(ScStringView *out) {
  return NwAttrib::GetArchitectName(out);
}

NwPieceAttrib::NwPieceAttrib() : connectionMasks{}, connectionFlags(0) {
}

bool NwPieceAttrib::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_NwConnectionData, NwConnectionData);
  return NwAttrib::QueryInterface(iid, out);
}

u32 NwPieceAttrib::AddRef() {
  return NwAttrib::AddRef();
}

u32 NwPieceAttrib::Release() {
  return NwAttrib::Release();
}

u8 NwPieceAttrib::MajorType() const {
  return 2;
}

// Select one captured connection mask and rotate its four samples through the installed lookup table.
void NwPieceAttrib::GetConnectionSamples(
    i32 kind, u8 plane, i32 side, i32 rotation, u8 *first, u8 *second, u8 *third, u8 *fourth) const {
  static const u8 rotated[64] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 12, 13, 14, 3, 0, 1, 2, 7, 4,
      5, 6, 11, 8, 9, 10, 10, 11, 8, 9, 14, 15, 12, 13, 2, 3, 0, 1, 6, 7, 4, 5, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15,
      12, 1, 2, 3, 0};
  u32 mask = u32(kind) < 3 ? connectionMasks[u32(kind)] : 0;
  u32 index = 4 * (u32(side) + 4 * u32(rotation)), shift = plane ? 0 : 16;
  *first = (mask & (1u << ((shift + rotated[index]) & 31))) != 0;
  *second = (mask & (1u << ((shift + rotated[index + 1]) & 31))) != 0;
  *third = (mask & (1u << ((shift + rotated[index + 2]) & 31))) != 0;
  *fourth = (mask & (1u << ((shift + rotated[index + 3]) & 31))) != 0;
}

void NwPieceAttrib::SetPrimaryMask(u32 value) {
  connectionMasks[0] = value;
}

u8 NwPieceAttrib::ConnectionFlag0() const {
  return (connectionFlags & 1) == 1;
}

void NwPieceAttrib::SetConnectionFlag0(u8 value) {
  connectionFlags = (connectionFlags & kNwConnectionWithoutFlagZero) | (value & 1);
}

u8 NwPieceAttrib::ConnectionFlag1() const {
  return (connectionFlags & 2) == 2;
}

void NwPieceAttrib::SetConnectionFlag1(u8 value) {
  connectionFlags = (connectionFlags & kNwConnectionWithoutFlagOne) | (u32(value & 1) << 1);
}

iptr NwPieceAttrib::GetPeopleProducerAttrib() {
  return NwAttrib::GetPeopleProducerAttrib();
}

iptr NwPieceAttrib::GetVehicleProducerAttrib() {
  return NwAttrib::GetVehicleProducerAttrib();
}

static const RsProperty NwConnectionFlagsProperty{0x12d, 3, 1};
static const RsProperty NwConnectionMaskProperties[3] = {{0x136, 3, 1}, {0x137, 3, 1}, {0x138, 3, 1}};

// Run the generic loader before reading flags and three masks, even when the generic loader refused the resource.
// Missing connection properties clear their individual words without changing the captured generic result.
bool NwPieceAttrib::LoadProperties(RsPropertySet *properties) {
  bool loaded = NwAttrib::LoadProperties(properties);
  if (!properties->GetWord(NwConnectionFlagsProperty, &connectionFlags, 0))
    connectionFlags = 0;
  for (u32 i = 0; i < 3; ++i)
    if (!properties->GetWord(NwConnectionMaskProperties[i], &connectionMasks[i], 0))
      connectionMasks[i] = 0;
  return loaded;
}

// Stop after the first refused generic or connection write, retaining the original flags-before-masks order.
bool NwPieceAttrib::SaveProperties(RsPropertySet *properties) {
  return NwAttrib::SaveProperties(properties) && properties->SetWord(NwConnectionFlagsProperty, connectionFlags, 0) &&
      properties->SetWord(NwConnectionMaskProperties[0], connectionMasks[0], 0) &&
      properties->SetWord(NwConnectionMaskProperties[1], connectionMasks[1], 0) &&
      properties->SetWord(NwConnectionMaskProperties[2], connectionMasks[2], 0);
}

// Construct the actual unretained connection attribute and return its primary Unknown-compatible interface.
ScUnknown *NwCreateAttributes() {
  return static_cast<NwConnectionData *>(new NwPieceAttrib());
}

NwOccupant::NwOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

NwOccupant::~NwOccupant() {
  if (attributes)
    NwOccupant::Shutdown();
}

// Expose the four interfaces sharing the original receiver. A successful query acquires one reference.
bool NwOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, NwOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, NwOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, NwOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, NwOccupant);
  RETURN_QUERY_INTERFACE_FAILURE();
}

bool NwOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool NwOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 NwOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void NwOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void NwOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

bool NwOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 NwOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool NwOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void NwOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool NwOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void NwOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

// Expand the two-bit orientation into the original angle values. Pitch and roll are always zero.
void NwOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool NwOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand tile and altitude fields to micro coordinates.
void NwOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void NwOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 NwOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Obtain cell coordinates through the virtual getter before scaling. Preserve its behavior when output pointers alias.
void NwOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u16 NwOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Write inclusive micro-coordinate bounds when attribute data exists. Dimensions are narrowed to the bytes consumed by the binary.
void NwOccupant::GetBounds(GeBounds *out) const {
  if (attributes) {
    u32 value = position;
    out->minimum.x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
    out->minimum.z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
    out->minimum.altitude =
        i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
    u32 height = SizeInCellsY(), upperAltitude = height + u32(u8(position >> kOcPositionAltitudeShift)) - 1;
    u32 depth = SizeInCellsZ(), upperZ = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
    u32 width = SizeInCellsX();
    out->maximum.x =
        i32((((position & kOcPositionCellValueMask) + width) << kCtMicroCoordinateShift) - kCtMicroUnitsPerCell);
    out->maximum.z = i32(upperZ << kCtMicroCoordinateShift);
    out->maximum.altitude = i32(upperAltitude << kCtMicroCoordinateShift);
  }
}

// Write the inclusive footprint when attribute data exists. Missing attributes leave the output unchanged.
void NwOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
  if (attributes) {
    out->left = position & kOcPositionCellValueMask;
    out->top = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
    u32 width = SizeInCellsX();
    out->right = width + (position & kOcPositionCellValueMask) - 1;
    u32 depth = SizeInCellsZ();
    out->bottom = depth + ((position >> kOcPositionZShift) & kOcPositionCellValueMask) - 1;
  }
}

u32 NwOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << 8;
}

u32 NwOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << 8;
}

u32 NwOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << 8;
}

// Replace the position fields from micro coordinates in source order. Preserve the two orientation bits.
void NwOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (u32(u8(u32(value.altitude) >> kCtMicroCoordinateShift)) << kOcPositionAltitudeShift);
}

void NwOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void NwOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool NwOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void NwOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void NwOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void NwOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

u32 NwOccupant::GetSubTypeData(u32) {
  return 0;
}

void NwOccupant::AnimStop() {
}

i32 NwOccupant::Tick(u32) {
  return 0;
}

bool NwOccupant::DoMessage(ScMessage *) {
  return false;
}

void NwOccupant::AnimStart() {
}

u8 NwOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

bool NwOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool NwOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool NwOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

bool NwOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  GeAttrib *value = attributes;
  if (!value)
    return false;
  u32 classId = value->GetQueryDialogClass();
  return NwGetCom()->GetClassObject(classId, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

u8 NwOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 NwOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 NwOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 NwOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void NwOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 NwOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u8 NwOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

u32 NwOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

// Borrow the retained attribute interface directly. This getter does not acquire a reference.
GeAttrib *NwOccupant::GetAttribData() {
  return attributes;
}

u32 NwOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

u8 NwOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 NwOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 NwOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

bool NwOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Borrow the agent type supplied by the retained attribute object. Missing attributes return a null pointer.
CtAgentType *NwOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

u32 NwOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Borrow the retained sprite pointer. This shared machine-code getter also serves unrelated fields in other original
// classes.
SpSprite *NwOccupant::GetSpriteInst() {
  return sprite;
}

// Return the borrowed sprite only for the virtual default layer. A mismatch does not call the sprite getter.
SpSprite *NwOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u32 NwOccupant::GetMemUse() const {
  return 20;
}

// Borrow the default sprite definition from retained attribute data. An unbound occupant has no definition.
SpDefinition *NwOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Forward the complete layer mask to the attribute definition lookup without retaining its borrowed result.
SpDefinition *NwOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Acquire the requested default sprite definition or agent type through the live virtual getter.
// Publish only after retaining the selected interface; rejected keys and absent values leave the output untouched.
bool NwOccupant::GetSubSystemData(u32 id, void **out) {
  if (!attributes)
    return false;
  ScUnknown *object;
  if (id == IID_SpDefView)
    object = static_cast<SpDefView *>(GetSpriteAttrib());
  else if (id == IID_CtAgentType)
    object = CityAgentType();
  else
    return false;
  if (!object)
    return false;
  object->AddRef();
  *out = object;
  return true;
}

// Replace retained attribute data before creating the default-layer sprite. Failure delegates cleanup to Shutdown; a
// zero instance subtype byte inherits the attribute subtype classification.
bool NwOccupant::Init(GeAttrib *value) {
  if (!value)
    return false;
  ScRelease(&attributes);
  if (sprite)
    DestroySprites(&sprite);
  attributes = value;
  value->AddRef();
  if (!CreateSprite(GetDefaultLayerFlag(), &sprite)) {
    Shutdown();
    return false;
  }
  if (!u8(flags >> kOcSubtypeShift)) {
    u8 subtype = attributes->SubTypeInfo();
    flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
  }
  return true;
}

// Replace the packed rotation and recreate its sprite only when the requested index differs.
u8 NwOccupant::SetRotationFlag(u32 rotation) {
  if ((position >> kOcPositionRotationShift) == rotation)
    return 1;
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  if (sprite)
    DestroySprites(&sprite);
  CreateSprite(GetDefaultLayerFlag(), &sprite);
  return 1;
}

// Follow the actual sprite links before releasing each instance, clearing the published head after the final release.
void NwOccupant::DestroySprites(SpSprite **out) {
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

// Release retained attributes before retiring the published sprite chain through its virtual teardown phase.
// Clear subtype and city-membership bits after callbacks, preserving references and removal permissions.
void NwOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

// Keep an existing attribute binding; otherwise query the actual cache and dispatch through virtual attribute Init.
bool NwOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  bool result = NwGetOccupantAttribCache()->GetOccupantAttrib(*key, &value);
  if (result) {
    result = Init(value);
    if (value)
      value->Release();
  } else
    attributes = nullptr;
  return result;
}

ScLock NwOccupantLock;

// Retain the module before entering the reference lock, then increment only the packed low word.
// Capture the returned count before unlocking while preserving every non-reference flag bit.
u32 NwOccupant::AddRef() {
  NwGetDirector()->AddRef();
  NwOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 references = u16(flags);
  NwOccupantLock.Unlock();
  return references;
}

// Release the module while locked, reloading callback changes before decrementing the live packed count.
// At zero, unlock before virtual shutdown and destruction; a zero initial count does not release the module again.
u32 NwOccupant::Release() {
  NwOccupantLock.ScLock::Lock();
  if (u16(flags)) {
    NwGetDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    if (u16(flags)) {
      NwOccupantLock.ScLock::Unlock();
      return u16(flags);
    }
  }
  NwOccupantLock.ScLock::Unlock();
  Shutdown();
  delete this;
  return 0;
}

// Dispatch recognized query messages through the acquired UI interface and the actual occupant virtual hook.
// Release the live interface output after the hook, preserving callback changes, and always report false.
bool NwOccupant::DoQueryInfo(ScMessage *message, ScUnknown *source) {
  u32 route, iid;
  if (message->type == kWnMsg_QueryOpened) {
    switch (u32(message->data1)) {
    case kUiQueryTagDialog:
      route = 0;
      iid = 1401;
      break;
    case kUiQueryTagStandard:
      route = 1;
      iid = 1411;
      break;
    case kUiQueryTagDebug:
      route = 2;
      iid = 1411;
      break;
    default:
      return false;
    }
  } else if (message->type == kWnMsg_QueryClosed && u32(message->data1) == kUiQueryTagDialog) {
    route = 3;
    iid = 1401;
  } else
    return false;
  void *selected = nullptr;
  if (!source || !source->QueryInterface(iid, &selected))
    return false;
  switch (route) {
  case 0:
    QueryWindow(message, static_cast<UiQueryWindow *>(selected));
    break;
  case 1:
    QueryTile(message, static_cast<UiQuery *>(selected));
    break;
  case 2:
    QueryText(message, static_cast<UiQuery *>(selected));
    break;
  case 3:
    QueryWindowEnd(message, static_cast<UiQueryWindow *>(selected));
    break;
  }
  static_cast<ScUnknown *>(selected)->Release();
  return false;
}

u8 NwOccupant::QueryWindow(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Submit a nonempty override architect name using the original localized label and row flags.
u8 NwOccupant::QueryTile(ScMessage *, UiQuery *query) {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  OcOverride *overrideAttrib = value->GetOverRideAttrib();
  if (!overrideAttrib)
    return 0;
  ScString architect;
  overrideAttrib->GetArchitectName(&architect);
  if (!architect.Strlen())
    return 0;
  // Build the independent row label before retiring its temporary resource and intermediate copies.
  ScString label = []() {
    NwTextRef text(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    ScString copy = text.CopyString();
    ScString intermediate(copy);
    return ScString(intermediate);
  }();
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

u8 NwOccupant::QueryText(ScMessage *, UiQuery *) {
  return 0;
}

u8 NwOccupant::QueryWindowEnd(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Ask the actual layer definition to publish its instance, then apply rotation and the live owner geometry flags.
bool NwOccupant::CreateSprite(u32 layer, SpSprite **out) {
  SpDefinition *definition = attributes->GetSpriteAttrib(layer);
  if (definition && definition->CreateInstance(out)) {
    (*out)->SetRotationOffset(u8(position >> kOcPositionRotationShift));
    u32 geometryFlags = GetGeomFlag();
    (*out)->SetGeomFlag(geometryFlags);
    return true;
  }
  attributes->Key();
  return false;
}

// Bind an unbound occupant from the descriptor key before its type-ID check, then restore location and rotation.
// Failed stages still clear the corresponding packed coordinates and enable both removal flags as the original does.
bool NwOccupant::Init(CtCity *, OcPersistInfo *info) {
  if (!info)
    return false;
  bool result = true;
  if (!attributes) {
    RsKey key{};
    info->GetAttribKey(&key);
    result = Init(&key);
    u32 typeId = 0;
    if (result)
      result = info->GetTypeId(&typeId) != 0;
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
    bool removable = info->GetCanSimRemoveFlag();
    flags = (flags & kOcFlagsWithoutSimRemove) | (u32(removable & 1) << kOcSimRemoveShift);
    removable = info->GetCanUserRemoveFlag();
    flags = (flags & kOcFlagsWithoutUserRemove) | (u32(removable & 1) << kOcUserRemoveShift);
  } else
    flags |= kOcDefaultRemovalFlags;
  return result;
}

// Save the live attribute key, virtual type and packed geometry only when an attribute is bound.
// Evaluate the two original X-size calls separately in their observed order, and preserve descriptor setter ordering.
bool NwOccupant::Save(CtCity *, OcPersistInfo *info) {
  if (!attributes)
    return false;
  info->SetAttribKey(*attributes->Key());
  info->SetTypeId(TypeId());
  u32 value = position;
  info->SetLocationInCells(value & kOcPositionCellValueMask, (value >> kOcPositionZShift) & kOcPositionCellValueMask,
      u8(value >> kOcPositionAltitudeShift));
  u8 second = SizeInCellsX(), first = SizeInCellsX();
  info->SetSizeInCells(first, second);
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  value = flags;
  info->SetRemoveFlags((value >> kOcSimRemoveShift) & 1, (value & kOcFlagUserRemove) != 0);
  return true;
}

NwPiece::NwPiece() : pieceData(0) {
}

bool NwPiece::Init(RsKey *key) {
  return NwOccupant::Init(key);
}

// Capture the virtual resource key before changing the packed rotation and low piece word, then bind through the base.
// Existing attributes preserve the base key initializer's early success without a redundant cache lookup.
bool NwPiece::Init(u32 piece, u32 rotation) {
  RsKey key = AsResourceKey(piece);
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
  pieceData ^= u16(piece ^ pieceData);
  return NwOccupant::Init(&key);
}

RsKey NwPiece::AsResourceKey(u32 piece) const {
  return {kNwTileResourceType, kNwTileResourceGroup, piece};
}

bool NwPiece::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_NwConnection, NwConnection);
  return NwOccupant::QueryInterface(iid, out);
}

u32 NwPiece::AddRef() {
  return NwOccupant::AddRef();
}

u32 NwPiece::Release() {
  return NwOccupant::Release();
}

u32 NwPiece::GetSubTypeData(u32 mask) {
  return pieceData & mask;
}

void NwPiece::Repair() {
  pieceData |= kNwPieceDamageFlag;
}

void NwPiece::SetIsDiagonal(u8) {
}

void NwPiece::SetCollapse(u8) {
}

void NwPiece::SetDamage(u8 value) {
  pieceData = (pieceData & kNwPieceWithoutDamage) | ((value & 1) << kNwPieceDamageShift);
}

void NwPiece::SetTileId(u32 piece) {
  pieceData = (pieceData & kNwPieceWithoutTileId) | (piece & kNwPieceTileIdMask);
}

void NwPiece::SetRotationFlags(u32 rotation) {
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
}

u32 NwPiece::NeedsRepair() const {
  return (pieceData >> kNwPieceDamageShift) & 1;
}

u32 NwPiece::GetRotationFlags() {
  return position >> kOcPositionRotationShift;
}

int NwPiece::IsTeleport() const {
  return (pieceData & kNwPieceTeleportFlag) != 0;
}

// Publish direction and length in order even when the teleport-present bit is clear, then test the live packed word.
// Separate reads preserve the original behavior when either DWORD output aliases the piece data.
int NwPiece::GetTeleport(i32 *direction, i32 *length) {
  *direction = i32((pieceData >> kNwPieceTeleportDirectionShift) & 3);
  *length = i32((pieceData >> kNwPieceTeleportLengthShift) & 0xff);
  return (pieceData & kNwPieceTeleportFlag) != 0;
}

void NwPiece::SetTeleport(i32 direction, i32 length) {
  pieceData = (pieceData & kNwPieceWithoutTeleportData) |
      ((u32(direction & 3) | ((u32(length) & 0xff) << 2)) << kNwPieceTeleportDirectionShift) | kNwPieceTeleportFlag;
}

// Capture the first network attribute flag before releasing its queried interface.
u8 NwPiece::ConnectionFlag0() const {
  u8 result = 0;
  void *view = nullptr;
  if (attributes->QueryInterface(IID_NwConnectionData, &view)) {
    result = static_cast<NwConnectionData *>(view)->ConnectionFlag0();
    static_cast<NwConnectionData *>(view)->Release();
  }
  return result;
}

// Capture the second network attribute flag before releasing the live queried interface.
// A failed query returns zero without releasing a value spuriously published by that failed query.
u8 NwPiece::ConnectionFlag1() const {
  u8 result = 0;
  void *view = nullptr;
  if (attributes->QueryInterface(IID_NwConnectionData, &view)) {
    result = static_cast<NwConnectionData *>(view)->ConnectionFlag1();
    static_cast<NwConnectionData *>(view)->Release();
  }
  return result;
}

// Clear the four genuine byte outputs before querying attributes, then sample with the live post-query rotation.
// The live query output is released after sampling. Its reference-count residue is not a Boolean method result.
void NwPiece::GetConnectionSamples(i32 kind, u8 plane, i32 side, u8 *first, u8 *second, u8 *third, u8 *fourth) const {
  *first = 0;
  *second = 0;
  *third = 0;
  void *view = nullptr;
  *fourth = 0;
  if (attributes->QueryInterface(IID_NwConnectionData, &view)) {
    static_cast<NwConnectionData *>(view)->GetConnectionSamples(
        kind, plane, side, i32(position >> kOcPositionRotationShift), first, second, third, fourth);
    static_cast<NwConnectionData *>(view)->Release();
  }
}

u32 NwPiece::TileId() const {
  return pieceData & kNwPieceTileIdMask;
}

u8 NwPiece::GetTrafficKey(RsKey *) {
  return 0;
}

// Return the original null traffic-block view for the abstract piece base.
NwTrafficBlock *NwPiece::GetTrafficBlock() {
  return nullptr;
}

u8 NwPiece::ClearTrafficBlock() {
  return 0;
}

void NwPiece::Shutdown() {
  ScRelease(&attributes);
  NwOccupant::Shutdown();
}

// Borrow the actual secondary occupant interface from the piece's primary network interface.
// Returning this view does not change the shared reference count.
ScOccupant *NwPiece::AsOccupant() {
  return static_cast<NwOccupant *>(this);
}

// Read the descriptor's identifier and rotation before virtual piece binding, retaining its ignored extension query.
// Successful binding then restores coordinates, removal flags and independently accepted packed properties in order.
bool NwPiece::Init(CtCity *, OcPersistInfo *info) {
  u32 identifier = 0;
  info->GetIntData(kNwPieceTileIdKey, &identifier);
  u32 rotation = 0;
  info->GetRotationFlag(&rotation);
  u32 extension = 0;
  info->GetIntData(kNwPieceExtensionKey, &extension);
  bool result = Init(identifier, rotation);
  if (result) {
    u32 x = 0, z = 0, altitude = 0;
    info->GetLocationInCells(&x, &z, &altitude);
    position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
        ((z & kOcPositionCellValueMask) << 11) | (u32(u8(altitude)) << 22);
    u8 removable = info->GetCanUserRemoveFlag();
    flags = (flags & kOcFlagsWithoutUserRemove) | (u32(removable & 1) << 25);
    removable = info->GetCanSimRemoveFlag();
    flags = (flags & kOcFlagsWithoutSimRemove) | (u32(removable & 1) << 24);
    u32 value;
    if (info->GetIntData(kNwPieceDamageKey, &value))
      pieceData = (pieceData & kNwPieceWithoutDamage) | (u32(value != 0) << kNwPieceDamageShift);
    if (info->GetIntData(kNwPieceTeleportPresentKey, &value)) {
      pieceData = (pieceData & kNwPieceWithoutTeleportFlag) | (u32(value != 0) << 17);
      if (pieceData & kNwPieceTeleportFlag) {
        if (info->GetIntData(kNwPieceTeleportLengthKey, &value))
          pieceData = (pieceData & kNwPieceWithoutTeleportLength) | (u32(u8(value)) << kNwPieceTeleportLengthShift);
        if (info->GetIntData(kNwPieceTeleportDirectionKey, &value))
          pieceData = (pieceData & kNwPieceWithoutTeleportDirection) | ((value & 3) << kNwPieceTeleportDirectionShift);
      }
    }
  }
  return result;
}

// Write the piece identifier and present extension fields without clearing omitted descriptor properties.
// Reload live owner fields after each setter callback and retain the original removal-flag argument order.
bool NwPiece::Save(CtCity *, OcPersistInfo *info) {
  info->SetIntData(kNwPieceTileIdKey, u16(pieceData));
  if (pieceData & kNwPieceDamageFlag)
    info->SetIntData(kNwPieceDamageKey, 1);
  if (pieceData & kNwPieceTeleportFlag) {
    info->SetIntData(kNwPieceTeleportPresentKey, 1);
    info->SetIntData(kNwPieceTeleportLengthKey, u8(pieceData >> kNwPieceTeleportLengthShift));
    info->SetIntData(kNwPieceTeleportDirectionKey, (pieceData >> kNwPieceTeleportDirectionShift) & 3);
  }
  u32 value = position;
  info->SetLocationInCells(value & kOcPositionCellValueMask, (value >> kOcPositionZShift) & kOcPositionCellValueMask,
      u8(value >> kOcPositionAltitudeShift));
  info->SetRotationFlag(position >> kOcPositionRotationShift);
  value = flags;
  info->SetRemoveFlags((value & kOcFlagUserRemove) != 0, (value >> 24) & 1);
  return true;
}

// Add the matching network connection's neighboring city before the generic architect row.
// Missing neighbors skip the generic query; otherwise its final byte is retained even when an edge row was posted.
u8 NwPiece::QueryTile(ScMessage *message, UiQuery *query) {
  CtNeighborsLayer *neighbors = NwGetCity()->NeighborsLayer();
  if (!neighbors)
    return 0;
  static const u32 masks[] = {kOcGeomRoad, kOcGeomRail, kOcGeomPower, kOcGeomHighway, kOcGeomPipe, kOcGeomSubway,
      kOcGeomTunnel, kOcGeomHighwayOnRamp};
  u32 category = 0;
  for (u32 i = 0; i < 8; ++i)
    if (IsGeomFlag(masks[i])) {
      category = i + 1;
      break;
    }
  if (category &&
      neighbors->HasConnection(i32(category), i32(position & kOcPositionCellValueMask),
          i32((position >> kOcPositionZShift) & kOcPositionCellValueMask))) {
    u8 edge;
    if (!(position & kOcPositionCellValueMask))
      edge = 0;
    else {
      u32 depth = NwGetCity()->CellCountZ();
      if (((position >> kOcPositionZShift) & kOcPositionCellValueMask) == depth - 1)
        edge = 3;
      else {
        u32 width = NwGetCity()->CellCountX();
        edge = u8(((position & kOcPositionCellValueMask) == width - 1) + 1);
      }
    }
    ScString label, neighborName;
    {
      NwTextRef text(kRsCityServiceConnectedToText, kRsCityServiceTextGroup);
      ScString copy = text.CopyString(), intermediate(copy);
      label = intermediate;
    }
    neighbors->GetNeighborName(edge, &neighborName);
    query->AddText(&label, &neighborName, kUiQueryPrimaryDetailsCategory, 0);
  }
  return NwOccupant::QueryTile(message, query);
}

u8 NwPiece::QueryText(ScMessage *message, UiQuery *query) {
  return NwOccupant::QueryText(message, query);
}

NwTrafficPiece::NwTrafficPiece() : trafficBlock(nullptr) {
}

NwTrafficPiece::~NwTrafficPiece() {
  ScRelease(&trafficBlock);
}

// Build the traffic key from the attribute's full instance only for zero rotation.
u8 NwTrafficPiece::GetTrafficKey(RsKey *out) {
  if (position >> kOcPositionRotationShift) {
    out->type = kNwTrafficBlockResourceType;
    out->group = kNwTrafficBlockResourceGroup;
    out->instance = 11008;
    return 0;
  }
  RsKey *key = attributes->Key();
  out->type = key->type;
  out->group = key->group;
  u32 instance = key->instance;
  out->instance = instance;
  out->type = kNwTrafficBlockResourceType;
  out->instance = instance << 8;
  out->group = kNwTrafficBlockResourceGroup;
  return 1;
}

// Test the live occupant before reusing or creating its retained traffic block.
// Preserve real COM publication, the post-piece-ID rotation read and the factory's independent output effects.
NwTrafficBlock *NwTrafficPiece::GetTrafficBlock() {
  if (!static_cast<OcIteratorTest *>(NwTrafficPredicate)->Test(this))
    return nullptr;
  if (!trafficBlock) {
    if (!NwTrafficFactoryCache) {
      if (!NwGetCom()->GetClassObject(
              kNwTrafficBlockFactoryClassId, IID_NwTrafficBlockFactory, &NwTrafficFactoryCache)) {
        NwTrafficFactoryCache = nullptr;
        return nullptr;
      }
    }
    RsKey key{kNwTrafficBlockResourceType, kNwTrafficBlockResourceGroup, 0};
    u32 piece = TileId();
    key.instance = (piece << kNwPieceImageInstanceShift) + (position >> kOcPositionRotationShift);
    static_cast<NwTrafficBlockFactory *>(NwTrafficFactoryCache)->CreateStrtSimTrBlock(
        key, reinterpret_cast<void **>(&trafficBlock));
  }
  return trafficBlock;
}

u8 NwTrafficPiece::ClearTrafficBlock() {
  ScRelease(&trafficBlock);
  return 1;
}

u32 NwRoadPiece::GetGeomFlag() {
  return kNwRoadPieceGeometry;
}

u32 NwRoadPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRoadPiece::MajorType() const {
  return 2;
}

u32 NwRoadPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRoadPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwHighwayPiece::GetGeomFlag() {
  return kNwHighwayPieceGeometry;
}

u32 NwHighwayPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwHighwayPiece::MajorType() const {
  return 2;
}

u32 NwHighwayPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwHighwayPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRailPiece::GetGeomFlag() {
  return kNwRailPieceGeometry;
}

u32 NwRailPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRailPiece::MajorType() const {
  return 2;
}

u32 NwRailPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRailPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwSubwayPiece::GetGeomFlag() {
  return kNwSubwayPieceGeometry;
}

u32 NwSubwayPiece::GetOccLayerFlag() {
  return 4;
}

u8 NwSubwayPiece::MajorType() const {
  return 2;
}

u32 NwSubwayPiece::GetDefaultLayerFlag() const {
  return 4;
}

bool NwSubwayPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRoadRailPiece::GetGeomFlag() {
  return kNwRoadRailPieceGeometry;
}

u32 NwRoadRailPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRoadRailPiece::MajorType() const {
  return 2;
}

u32 NwRoadRailPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRoadRailPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRoadHighwayPiece::GetGeomFlag() {
  return kNwRoadHighwayPieceGeometry;
}

u32 NwRoadHighwayPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRoadHighwayPiece::MajorType() const {
  return 2;
}

u32 NwRoadHighwayPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRoadHighwayPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRailHighwayPiece::GetGeomFlag() {
  return kNwRailHighwayPieceGeometry;
}

u32 NwRailHighwayPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRailHighwayPiece::MajorType() const {
  return 2;
}

u32 NwRailHighwayPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRailHighwayPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRailSubwayPiece::GetGeomFlag() {
  return kNwRailSubwayPieceGeometry;
}

u32 NwRailSubwayPiece::GetOccLayerFlag() {
  return 5;
}

u8 NwRailSubwayPiece::MajorType() const {
  return 2;
}

u32 NwRailSubwayPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRailSubwayPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwPipePiece::GetGeomFlag() {
  return kNwPipePieceGeometry;
}

u32 NwPipePiece::GetOccLayerFlag() {
  return 2;
}

u8 NwPipePiece::MajorType() const {
  return 2;
}

u32 NwPipePiece::GetDefaultLayerFlag() const {
  return 2;
}

bool NwPipePiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwPowerPiece::GetGeomFlag() {
  return kNwPowerPieceGeometry;
}

u32 NwPowerPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwPowerPiece::MajorType() const {
  return 2;
}

u32 NwPowerPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwPowerPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRoadPowerPiece::GetGeomFlag() {
  return kNwRoadPowerPieceGeometry;
}

u32 NwRoadPowerPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRoadPowerPiece::MajorType() const {
  return 2;
}

u32 NwRoadPowerPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRoadPowerPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwRailPowerPiece::GetGeomFlag() {
  return kNwRailPowerPieceGeometry;
}

u32 NwRailPowerPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwRailPowerPiece::MajorType() const {
  return 2;
}

u32 NwRailPowerPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwRailPowerPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwHighwayPowerPiece::GetGeomFlag() {
  return kNwHighwayPowerPieceGeometry;
}

u32 NwHighwayPowerPiece::GetOccLayerFlag() {
  return 1;
}

u8 NwHighwayPowerPiece::MajorType() const {
  return 2;
}

u32 NwHighwayPowerPiece::GetDefaultLayerFlag() const {
  return 1;
}

bool NwHighwayPowerPiece::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask21C804::GetGeomFlag() {
  return kNwRoadHighwayOnRampPieceGeometry;
}

u32 NwMask21C804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask21C804::MajorType() const {
  return 2;
}

u32 NwMask21C804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask21C804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask214804::GetGeomFlag() {
  return kNwHighwayOnRampPieceGeometry;
}

u32 NwMask214804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask214804::MajorType() const {
  return 2;
}

u32 NwMask214804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask214804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask8C804::GetGeomFlag() {
  return 0x8c804;
}

u32 NwMask8C804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask8C804::MajorType() const {
  return 2;
}

u32 NwMask8C804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask8C804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask94804::GetGeomFlag() {
  return 0x94804;
}

u32 NwMask94804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask94804::MajorType() const {
  return 2;
}

u32 NwMask94804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask94804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMaskA4804::GetGeomFlag() {
  return 0xa4804;
}

u32 NwMaskA4804::GetOccLayerFlag() {
  return 1;
}

u8 NwMaskA4804::MajorType() const {
  return 2;
}

u32 NwMaskA4804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMaskA4804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask480804::GetGeomFlag() {
  return kNwWireBridgePieceGeometry;
}

u32 NwMask480804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask480804::MajorType() const {
  return 2;
}

u32 NwMask480804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask480804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask10C804::GetGeomFlag() {
  return kNwRoadTunnelPieceGeometry;
}

u32 NwMask10C804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask10C804::MajorType() const {
  return 2;
}

u32 NwMask10C804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask10C804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask124804::GetGeomFlag() {
  return kNwRailTunnelPieceGeometry;
}

u32 NwMask124804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask124804::MajorType() const {
  return 2;
}

u32 NwMask124804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask124804::DebugClassTag(ScStringView *) {
  return false;
}

u32 NwMask114804::GetGeomFlag() {
  return kNwHighwayTunnelPieceGeometry;
}

u32 NwMask114804::GetOccLayerFlag() {
  return 1;
}

u8 NwMask114804::MajorType() const {
  return 2;
}

u32 NwMask114804::GetDefaultLayerFlag() const {
  return 1;
}

bool NwMask114804::DebugClassTag(ScStringView *) {
  return false;
}

// Construct the original low-byte kind's actual class, then query the caller's requested interface.
u8 NwMakePiece(u32 kind, u32 iid, void **out) {
  NwConnection *piece;
  switch (u8(kind)) {
  case 1:
    piece = new NwRoadPiece;
    break;
  case 2:
    piece = new NwHighwayPiece;
    break;
  case 3:
    piece = new NwRailPiece;
    break;
  case 4:
    piece = new NwSubwayPiece;
    break;
  case 5:
    piece = new NwRoadRailPiece;
    break;
  case 6:
    piece = new NwRoadHighwayPiece;
    break;
  case 7:
    piece = new NwRailHighwayPiece;
    break;
  case 8:
    piece = new NwRailSubwayPiece;
    break;
  case 9:
    piece = new NwPipePiece;
    break;
  case 10:
    piece = new NwPowerPiece;
    break;
  case 11:
    piece = new NwRoadPowerPiece;
    break;
  case 12:
    piece = new NwRailPowerPiece;
    break;
  case 13:
    piece = new NwHighwayPowerPiece;
    break;
  case 14:
    piece = new NwMask21C804;
    break;
  case 15:
    piece = new NwMask214804;
    break;
  case 16:
    piece = new NwMask8C804;
    break;
  case 17:
    piece = new NwMask94804;
    break;
  case 18:
    piece = new NwMaskA4804;
    break;
  case 19:
    piece = new NwMask480804;
    break;
  case 20:
    piece = new NwMask10C804;
    break;
  case 21:
    piece = new NwMask124804;
    break;
  case 22:
    piece = new NwMask114804;
    break;
  default:
    return 0;
  }
  if (!piece->QueryInterface(iid, out)) {
    piece->Release();
    return 0;
  }
  piece->AsOccupant()->SetSubTypeInfo(u8(kind));
  return 1;
}

NwTextRef::NwTextRef(u32 instance, u32 group) : stringInterface(nullptr), key{kRsTextResourceType, group, instance} {
  Resolve();
}

NwTextRef::~NwTextRef() {
  if (stringInterface) {
    static_cast<ScStringView *>(stringInterface)->Release();
    stringInterface = nullptr;
  }
}

ScString NwTextRef::CopyString() const {
  if (stringInterface)
    return ScString(*static_cast<ScStringView *>(stringInterface));
  ScString empty;
  return ScString(empty);
}

void NwTextRef::Resolve() {
  if (!NwGetResourceManager()->GetResource(key, IID_ScStringView, &stringInterface))
    stringInterface = nullptr;
}

// Require user-removable geometry, then accept the source flag shortcuts or the exact allowed piece IDs.
bool NwRoadOverlap::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if (flags & kOcGeomFlora)
    return true;
  if (!(flags & kOcGeomNetwork))
    return false;
  if (flags & kOcGeomRoad)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 92 || piece == 73 || piece == 15051 || piece == 44 || piece == 45 || piece == 10001 ||
      piece == 10036 || piece == 10037 || piece == 15 || piece == 14;
}

// Require user-removable geometry, then accept the source flag shortcuts or the exact allowed piece IDs.
bool NwPowerOverlap::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if (flags & kOcGeomFlora)
    return true;
  if (!(flags & kOcGeomNetwork))
    return false;
  if (flags & kOcGeomPower)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 29 || piece == 11204 || piece == 44 || piece == 73 || piece == 15051 || piece == 30 || piece == 45;
}

// Require user-removable geometry, then accept the source flag shortcuts or the exact allowed piece IDs.
bool NwRailOverlap::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if (flags & kOcGeomFlora)
    return true;
  if (!(flags & kOcGeomNetwork))
    return false;
  if (flags & kOcGeomRail)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 29 || piece == 11204 || piece == 73 || piece == 15051 || piece == 92 || piece == 30 ||
      piece == 10001 || piece == 10036 || piece == 10037 || piece == 15 || piece == 14;
}

// Require user-removable geometry, then accept the source flag shortcuts or the exact allowed piece IDs.
bool NwHighwayOverlap::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if (flags & kOcGeomFlora)
    return true;
  if (!(flags & kOcGeomNetwork))
    return false;
  if (flags & kOcGeomHighway)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 29 || piece == 11204 || piece == 44 || piece == 92 || piece == 30 || piece == 45 || piece == 10001 ||
      piece == 10036 || piece == 10037 || piece == 15 || piece == 14;
}

bool NwSubwayOverlap::Test(ScOccupant *occupant) {
  return occupant->CanUserRemove();
}

bool NwPipeOverlap::Test(ScOccupant *occupant) {
  return occupant->CanUserRemove();
}

// Accept removable matching geometry and the two source joint pieces, preserving subtype-query ordering.
bool NwRoadJoin::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if ((flags & kOcGeomFlora) || (flags & kOcGeomRoadNetwork) == kOcGeomRoadNetwork)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 15 || piece == 14;
}

// Accept removable flora or geometry with both power-network bits present.
bool NwPowerJoin::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  return (flags & kOcGeomFlora) || (flags & kOcGeomPowerNetwork) == kOcGeomPowerNetwork;
}

// Accept removable matching geometry and the two source joint pieces, preserving subtype-query ordering.
bool NwRailJoin::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if ((flags & kOcGeomFlora) || (flags & kOcGeomRailNetwork) == kOcGeomRailNetwork)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 15 || piece == 14;
}

// Accept removable matching geometry and the two source joint pieces, preserving subtype-query ordering.
bool NwHighwayJoin::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove())
    return false;
  u32 flags = occupant->GetGeomFlag();
  if ((flags & kOcGeomFlora) || (flags & kOcGeomHighwayNetwork) == kOcGeomHighwayNetwork)
    return true;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 15 || piece == 14;
}

bool NwSubwayJoin::Test(ScOccupant *occupant) {
  return occupant->CanUserRemove();
}

bool NwPipeJoin::Test(ScOccupant *occupant) {
  return occupant->CanUserRemove();
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationA::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 73 || piece == 11258 || piece == 67 || piece == 69 || piece == 92 || piece == 15;
  case 1:
    return piece == 44 || piece == 92;
  case 2:
    return piece == 73 || piece == 11258;
  default:
    return false;
  }
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationB::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 71 || piece == 73 || piece == 16006 || piece == 92 || piece == 15;
  case 1:
    return piece == 29 || piece == 11204 || piece == 69 || piece == 92;
  case 2:
    return piece == 73 || piece == 16006;
  case 3:
    return piece == 11204;
  default:
    return false;
  }
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationC::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 16007 || piece == 15037 || piece == 15098 || piece == 92 || piece == 14;
  case 1:
    return piece == 44 || piece == 92 || piece == 29 || piece == 11204;
  case 2:
    return piece == 16007 || piece == 15037 || piece == 15098 || piece == 92 || piece == 14;
  case 3:
    return piece == 11204;
  default:
    return false;
  }
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationD::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 73 || piece == 15036;
  case 1:
    return piece == 29 || piece == 11204 || piece == 67 || piece == 44 || piece == 71;
  case 2:
    return piece == 73 || piece == 15036;
  case 3:
    return piece == 11204;
  default:
    return false;
  }
}

bool NwRotationE::Test(ScOccupant *occupant) {
  occupant->CanUserRemove();
  return false;
}

bool NwRotationF::Test(ScOccupant *occupant) {
  occupant->CanUserRemove();
  return false;
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationG::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 44 || piece == 92 || piece == 15051 || piece == 15098 || piece == 14;
  case 1:
    return piece == 69 || piece == 67;
  case 2:
    return piece == 15051 || piece == 15098;
  default:
    return false;
  }
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationH::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 29 || piece == 11204 || piece == 69 || piece == 92 || piece == 15051 || piece == 14;
  case 1:
    return piece == 71;
  case 2:
    return piece == 15051 || piece == 11204 || piece == 16007;
  default:
    return false;
  }
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationI::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 44 || piece == 92 || piece == 15036 || piece == 29 || piece == 11204 || piece == 11258 ||
        piece == 15;
  case 2:
    return piece == 16006 || piece == 15036 || piece == 11204 || piece == 11258 || piece == 15;
  default:
    return false;
  }
}

// Match the exact per-rotation piece lists after the original removal, network and subtype queries.
bool NwRotationJ::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  // The source reuses its nonnull aligned occupant argument as rotation storage; an untouched value rejects.
  u32 rotation = UINT32_MAX;
  occupant->GetRotationFlag(&rotation);
  switch (rotation) {
  case 0:
    return piece == 29 || piece == 67 || piece == 11204 || piece == 44 || piece == 71 || piece == 15051 ||
        piece == 15037;
  case 2:
    return piece == 15051 || piece == 11204 || piece == 15037;
  default:
    return false;
  }
}

bool NwRotationK::Test(ScOccupant *occupant) {
  occupant->CanUserRemove();
  return false;
}

bool NwRotationL::Test(ScOccupant *occupant) {
  occupant->CanUserRemove();
  return false;
}

// Require removable road geometry before accepting the three undecorated source piece IDs.
bool NwDecorEligible::Test(ScOccupant *occupant) {
  if (!occupant->CanUserRemove() || !occupant->IsGeomFlag(kOcGeomRoadNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 29 || piece == 11204 || piece == 35;
}

// Test road geometry before matching the four decorated source piece IDs, without querying removal eligibility.
bool NwDecorated::Test(ScOccupant *occupant) {
  if (!occupant->IsGeomFlag(kOcGeomRoadNetwork))
    return false;
  u32 piece = occupant->GetSubTypeData(0xffff);
  return piece == 15040 || piece == 15041 || piece == 15042 || piece == 9051;
}

// Allocate the empty queue's sentinel and link it to itself. Sentinel coordinates remain unused.
NwPositionList::NwPositionList() {
  head = new NwPositionNode;
  head->next = head;
  head->previous = head;
}

// Allocate an independent sentinel and copy each coordinate in the original order, including duplicates and altitude.
// This supplies the owned by-value argument used by the outer tiling operation without sharing source nodes.
NwPositionList::NwPositionList(const NwPositionList &source) : NwPositionList() {
  NwPositionNode *sourceHead = source.head;
  for (NwPositionNode *node = sourceHead->next; node != sourceHead; node = node->next)
    Append(node->position);
}

NwPositionList::~NwPositionList() {
  Clear();
  delete head;
}

// Copy all three coordinate words before linking a new tail. Existing node addresses remain stable.
void NwPositionList::Append(const CtCoord &position) {
  NwPositionNode *node = new NwPositionNode;
  node->position = position;
  node->next = head;
  node->previous = head->previous;
  head->previous->next = node;
  head->previous = node;
}

// Capture each successor before freeing its node, then restore the empty cyclic sentinel.
void NwPositionList::Clear() {
  NwPositionNode *node = head->next;
  while (node != head) {
    NwPositionNode *next = node->next;
    delete node;
    node = next;
  }
  head->next = head;
  head->previous = head;
}

// Remove every node with the supplied fixed-point X/Z, preserving the order and identity of all other nodes.
// Capture each successor before unlinking so duplicates can be removed in one traversal.
void NwPositionList::RemoveXZ(const CtCoord &position) {
  i32 x = position.x, z = position.z;
  NwPositionNode *node = head->next;
  while (node != head) {
    NwPositionNode *next = node->next;
    if (node->position.x == x && node->position.z == z) {
      node->previous->next = next;
      next->previous = node->previous;
      delete node;
    }
    node = next;
  }
}

// Retire the first coordinate after a successful ramp consumed that endpoint.
// The caller supplies the original nonempty-list invariant; no game status is introduced.
void NwPositionList::PopFront() {
  NwPositionNode *node = head->next;
  head->next = node->next;
  node->next->previous = head;
  delete node;
}
