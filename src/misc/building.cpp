#include "geometry/building.h"

#include <stdio.h>

#include "advisors/ad_advisors.h"
#include "city/api.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/api.h"
#include "misc/api.h"
#include "misc/building.h"
#include "misc/module.h"
#include "misc/oc_building.h"
#include "misc/oc_occupant.h"
#include "misc/oc_static_anim_building.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "window/api.h"

// Copy the localized value before retiring its module-local resource hold, preserving each caller's actual group.
static ScString OcBuildingQueryText(u32 instance, u32 group) {
  CtMiscTextResource resource(instance, group);
  ScString value = resource.GetText(), copy(value);
  return copy;
}

OcBuilding::OcBuilding() : occupancy_flags(0), agent_type(nullptr), cached_attributes(nullptr), cached_sprite(nullptr) {
}

OcBuilding::~OcBuilding() {
  ScRelease(&agent_type);
}

// Clear output before selecting building or city-agent identity. Delegate other requests to the adjusted occupant base.
bool OcBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  return OcOccupant::QueryInterface(iid, out);
}

u32 OcBuilding::AddRef() {
  return OcOccupant::AddRef();
}

u32 OcBuilding::Release() {
  return OcOccupant::Release();
}

// Borrow the adjusted occupant interface without acquiring another reference.
ScOccupant *OcBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the city-agent subobject without a query or reference acquisition.
CtAgent *OcBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

bool OcBuilding::Simulate(i32 elapsed) {
  return agent_type->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 OcBuilding::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool OcBuilding::IsAgentLocking() {
  return true;
}

// Borrow the retained agent type without changing its reference count.
CtAgentType *OcBuilding::AgentType() {
  return agent_type;
}

bool OcBuilding::SetAgentType(CtAgentType *) {
  return false;
}

u32 OcBuilding::TypeId() const {
  return OcOccupant::TypeId();
}

u32 OcBuilding::GetOccupancyState() {
  return u8(occupancy_flags);
}

bool OcBuilding::GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  return (u32(mask) & occupancy_flags) != 0;
}

u32 OcBuilding::GetGeomFlag() {
  return 260;
}

u32 OcBuilding::GetMemUse() const {
  return 44;
}

// Borrow the original generic attribute pointer directly, bypassing the city-specific default-data selection.
GeAttrib *OcBuilding::GetBaseAttribData() {
  return OcOccupant::GetAttribData();
}

// Borrow cached attributes when present. Otherwise select the city's building default for inactive occupancy states,
// falling back to generic attributes on absent defaults and publishing that borrowed result after callbacks.
GeAttrib *OcBuilding::GetAttribData() {
  if (cached_attributes)
    return cached_attributes;
  if (occupancy_flags != 1 && !(u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    GeBuildingLayer *layer = ScGetMiscCity()->BuildingLayer();
    if (layer) {
      cached_attributes = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (cached_attributes)
        return cached_attributes;
    }
  }
  cached_attributes = OcOccupant::GetAttribData();
  return cached_attributes;
}

// Select the virtual default layer before invoking this building's qualified indexed getter.
SpDefinition *OcBuilding::GetSpriteAttrib() {
  u32 layer = GetDefaultLayerFlag();
  return OcBuilding::GetSpriteAttrib(layer);
}

// Reuse the borrowed sprite cache for every layer. Active states use the generic indexed definition; other states
// ask the city's building defaults and fall back to the generic default definition when no result is available.
SpDefinition *OcBuilding::GetSpriteAttrib(u32 layer) {
  if (cached_sprite)
    return cached_sprite;
  if (occupancy_flags == 1 || (u8(occupancy_flags) == 1 && (occupancy_flags & 0xff00) == 0x200)) {
    cached_sprite = OcOccupant::GetSpriteAttrib(layer);
    return cached_sprite;
  }
  GeBuildingLayer *buildings = ScGetMiscCity()->BuildingLayer();
  if (buildings) {
    cached_sprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
    if (cached_sprite)
      return cached_sprite;
  }
  cached_sprite = OcOccupant::GetSpriteAttrib();
  return cached_sprite;
}

// Query the specialized building attribute view from the virtual generic getter. Preserve its raw power byte
// before releasing the acquired interface; absent attributes or a failed query produce zero.
u8 OcBuilding::PowerRequired() {
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

bool OcBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool OcBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

bool OcBuilding::Init(RsKey *key) {
  return OcOccupant::Init(key);
}

// Bind the actual occupant attributes before clearing borrowed building caches. Capture and retain the default agent
// type only when none is stored; failed occupant initialization performs the original second virtual shutdown call.
bool OcBuilding::Init(GeAttrib *value) {
  if (!OcOccupant::Init(value)) {
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

bool OcBuilding::SetName(const ScStringView &) {
  return false;
}

bool OcBuilding::IsNameChangeable() {
  return false;
}

bool OcBuilding::HasBirthdate() const {
  return false;
}

bool OcBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool OcBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

// The original3549B stack guard has a destructor interface and one actual critical-section pointer.
class OcGraphicsLockScope {
public:
  explicit OcGraphicsLockScope(ScLock *lock);
  virtual ~OcGraphicsLockScope();

private:
  ScLock *lock;
};

OcGraphicsLockScope::OcGraphicsLockScope(ScLock *lock) : lock(lock) {
  lock->Lock();
}

OcGraphicsLockScope::~OcGraphicsLockScope() {
  lock->Unlock();
}

// Invalidate the attribute cache, use the selected definition's instance methods and keep failure outputs live.
void OcBuilding::RefreshSprite() {
  OcGraphicsLockScope scope(ScGetMiscCity()->GetGraphicsCriticalSection());
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
void OcBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancy_flags != state) {
    occupancy_flags = state;
    RefreshSprite();
  }
}

// Add the requested occupancy bits. Refresh only when the word changes and the input includes a low-byte bit.
void OcBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags | mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Remove the requested occupancy bits. High-byte-only changes intentionally leave the original sprite untouched.
void OcBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancy_flags & ~mask;
  if (value != occupancy_flags) {
    occupancy_flags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Restore generic occupant data before requiring the descriptor's raw state word. Ignore optional name/date failures;
// a successful raw state load uses the original OR setter, and birthdate outputs are year first, then ordinal day.
bool OcBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !OcOccupant::Init(city, info) || !info->HasLayerData1())
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
bool OcBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!OcOccupant::Save(city, info))
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
void OcBuilding::Shutdown() {
  ScRelease(&agent_type);
  cached_attributes = nullptr;
  cached_sprite = nullptr;
  OcOccupant::Shutdown();
}

bool OcBuilding::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
  return OcOccupant::DoQueryInfo(message, sender);
}

// Select the first matching occupancy status and publish it before the option row. The checked state is captured before
// the font lookup. Hex-Rays inferred its pending third argument as an extra resource-getter argument.
u8 OcBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
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
      CtMiscTextResource resource(instance, kRsCityServiceTextGroup);
      ScString value = resource.GetText(), copy(value);
      status = copy;
    }
    query->SetStatusText(status);
  }
  ScString caption = OcBuildingQueryText(kRsCityServiceMakeHistoricalText, kRsCityServiceTextGroup);
  WnResources *resources = ScGetMiscWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  query->AddOption(caption, font, checked);
  return OcOccupant::QueryBasic(message, query);
}

// For the two inactive occupancy flags, add the signed wrapped footprint cost using the actual money utility. Missing
// language utility returns immediately; absent budget skips the cost row but still permits the base architect row.
u8 OcBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    EcBudgetLayer *budget = ScGetMiscCity()->BudgetLayer();
    if (budget) {
      u32 amount = u32(budget->GetCost(kEcCostDemolish));
      amount *= SizeInCellsZ();
      amount *= SizeInCellsX();
      ScString label = OcBuildingQueryText(kRsCityServiceCleanUpCostText, kRsCityServiceTextGroup);
      ScString formatted = OcBuildingQueryText(kRsCityServiceCleanUpCostFormatText, kRsCityServiceTextGroup);
      ScString number;
      utility->MakeMoneyString(i32(amount), &number);
      formatted.Sprintf(formatted.Data(), number.Data());
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return OcOccupant::QueryTile(message, query);
}

u8 OcBuilding::QueryDetail(ScMessage *message, UiQuery *query) {
  return OcOccupant::QueryDetail(message, query);
}

// Apply the option's checked byte through the occupancy mutators, then invoke the empty base response.
u8 OcBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (query->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return OcOccupant::QueryResponse(message, query);
}

// Restore generic occupant state, then read the raw building word and apply the original zero-to-one conversion.
bool OcBuilding::ReadState(CtCity *city, RsStream *record) {
  if (!OcOccupant::ReadState(city, record))
    return false;
  i32 state = 0;
  bool valid = record->ReadI32(&state);
  if (!state)
    state = 1;
  SetOccupancyStateFlag(u32(state));
  return valid;
}

// Write generic occupant state before forwarding the live raw occupancy word to the original signed stream operation.
bool OcBuilding::WriteState(CtCity *city, RsStream *record) {
  if (!OcOccupant::WriteState(city, record))
    return false;
  return record->WriteI32(i32(occupancy_flags));
}

static ScLock OcOccupantLock; // Original module-wide recursive reference lock4A1C8.

OcOccupant::OcOccupant() : attributes(nullptr), sprite(nullptr), flags(kOcDefaultRemovalFlags), position(0) {
}

// Retire attributes and the base sprite chain only when attributes are present. The final flag mask preserves
// references, removal permissions and the mark bit, matching the qualified base cleanup used during destruction.
OcOccupant::~OcOccupant() {
  if (attributes) {
    attributes->Release();
    attributes = nullptr;
    OcOccupant::DestroySprites(&sprite);
    flags &= kOcFlagsWithoutSubtypeOrMembership;
  }
}

// Expose the four identities sharing this occupant receiver. Successful queries acquire a virtual reference.
bool OcOccupant::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  HANDLE_QUERY_INTERFACE(IID_ScOccupant, ScOccupant);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, ScQueryTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

bool OcOccupant::IsInCity() const {
  return (flags & kOcMembershipMask) != 0;
}

bool OcOccupant::TestIsInCityFlag(u32 mask) const {
  return (mask & (flags >> kOcMembershipShift) & kOcMembershipValueMask) != 0;
}

u32 OcOccupant::GetIsInCityFlag() const {
  return (flags >> kOcMembershipShift) & kOcMembershipValueMask;
}

void OcOccupant::SetIsInCityFlag(u32 mask) {
  flags |= (mask & 31) << kOcMembershipShift;
}

void OcOccupant::ClearIsInCityFlag(u32 mask) {
  flags &= (~mask << kOcMembershipShift) | kOcFlagsWithoutMembership;
}

u8 OcOccupant::SubTypeInfo() const {
  return u8(flags >> kOcSubtypeShift);
}

bool OcOccupant::CanSimRemove() const {
  return (flags & kOcFlagSimRemove) != 0;
}

void OcOccupant::SetSimRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutSimRemove) | (u32(enabled) << kOcSimRemoveShift);
}

bool OcOccupant::CanUserRemove() const {
  return (flags & kOcFlagUserRemove) != 0;
}

void OcOccupant::SetUserRemove(bool enabled) {
  flags = (flags & kOcFlagsWithoutUserRemove) | (u32(enabled) << kOcUserRemoveShift);
}

void OcOccupant::GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const {
  *roll = 0;
  *pitch = 0;
  u32 index = position >> kOcPositionRotationShift;
  *rotation = index ? u16((index << kOcQuarterTurnAngleShift) - 1) : 0;
}

bool OcOccupant::GetRotationFlag(u32 *rotation) const {
  *rotation = position >> kOcPositionRotationShift;
  return true;
}

// Expand the packed cell coordinates and altitude to micro coordinates. Exclude orientation bits from altitude.
void OcOccupant::GetLocation(CtCoord *out) const {
  u32 value = position;
  out->x = i32((value & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->z = i32(((value >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift);
  out->altitude = i32(((value >> kOcPositionAltitudeShift) & kOcPositionAltitudeValueMask) << kCtMicroCoordinateShift);
}

void OcOccupant::GetLocationInCellCoords(u32 *x, u32 *z) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
}

u8 OcOccupant::GetAlt() const {
  return u8(position >> kOcPositionAltitudeShift);
}

// Replace cell coordinates and altitude from micro coordinates in source store order. Preserve orientation.
void OcOccupant::SetLocation(const CtCoord &value) {
  position = (position & kOcPositionWithoutX) | ((u32(value.x) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask);
  position = (position & kOcPositionWithoutZ) |
      (((u32(value.z) >> kCtMicroCoordinateShift) & kOcPositionCellValueMask) << kOcPositionZShift);
  position = (position & kOcPositionWithoutAltitude) |
      (((u32(value.altitude) >> kCtMicroCoordinateShift) & kOcPositionAltitudeValueMask) << kOcPositionAltitudeShift);
}

void OcOccupant::SetLocationInCellCoords(u32 x, u32 z) {
  position = (position & kOcPositionWithoutCellCoords) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << 11);
}

void OcOccupant::SetAlt(u8 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude) << kOcPositionAltitudeShift);
}

bool OcOccupant::GetMarkBit() const {
  return (flags >> kOcMarkShift) != 0;
}

void OcOccupant::SetMarkBit(bool marked) {
  flags = (flags & kOcFlagsWithoutMark) | (u32(marked) << kOcMarkShift);
}

void OcOccupant::SetSubTypeInfo(u8 subtype) {
  flags = (flags & kOcFlagsWithoutSubtype) | (u32(subtype) << kOcSubtypeShift);
}

void OcOccupant::SetOrientation(u16 rotation, u16, u16) {
  u32 index = rotation < 0x1fff ? 0 : rotation < 0x5fff ? 1 : rotation < 0x9fff ? 2 : rotation < 0xdfff ? 3 : 0;
  SetRotationFlag(index);
}

// Follow each borrowed sprite link before releasing its owner. Clear the published head after the final release.
void OcOccupant::DestroySprites(SpSprite **out) {
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
u32 OcOccupant::AddRef() {
  ScGetMiscDirector()->AddRef();
  OcOccupantLock.Lock();
  flags = (flags & kOcFlagsWithoutReferences) | u16(flags + 1);
  u32 count = u16(flags);
  OcOccupantLock.Unlock();
  return count;
}

// Release the module while locked and reload its callback changes before decrementing the live count.
// At zero, unlock before shutdown and destruction. An initially zero count does not release another module hold.
u32 OcOccupant::Release() {
  OcOccupantLock.ScLock::Lock();
  if (u16(flags)) {
    ScGetMiscDirector()->RemoveRef();
    flags = (flags & kOcFlagsWithoutReferences) | u16(flags - 1);
    if (u16(flags)) {
      OcOccupantLock.ScLock::Unlock();
      return u16(flags);
    }
  }
  OcOccupantLock.ScLock::Unlock();
  Shutdown();
  delete this;
  return 0;
}

// Borrow the generic attribute pointer without acquiring a reference or consulting the city.
GeAttrib *OcOccupant::GetAttribData() {
  return attributes;
}

u32 OcOccupant::TypeId() const {
  return attributes ? attributes->TypeId() : 0;
}

bool OcOccupant::DoMessage(ScMessage *) {
  return false;
}

bool OcOccupant::IsGeomFlag(u32 mask) {
  return mask == (mask & GetGeomFlag());
}

u8 OcOccupant::SetGeomFlag(u32 mask) {
  return attributes ? attributes->SetGeomFlag(mask) : 0;
}

u8 OcOccupant::ClearGeomFlag(u32 mask) {
  return attributes ? attributes->ClearGeomFlag(mask) : 0;
}

u32 OcOccupant::GetGeomFlag() {
  return attributes ? attributes->GetGeomFlag() : 0;
}

u32 OcOccupant::GetOccLayerFlag() {
  return attributes ? attributes->GetOccLayerFlag() : 1;
}

void OcOccupant::SetOccLayerFlag(u32 mask) {
  if (attributes)
    attributes->SetOccLayerFlag(u8(mask));
}

u8 OcOccupant::MajorType() const {
  return attributes ? attributes->MajorType() : 0;
}

u32 OcOccupant::GetSubTypeData(u32) {
  return 0;
}

u8 OcOccupant::AnimType() const {
  return attributes ? attributes->AnimType() : 0;
}

void OcOccupant::AnimStart() {
}

void OcOccupant::AnimStop() {
}

i32 OcOccupant::Tick(u32) {
  return 0;
}

bool OcOccupant::GetName(ScStringView *out) {
  return attributes ? attributes->GetName(out) : false;
}

// Select the actual primary sprite-definition or agent-type subsystem through virtual getters.
// Acquire a reference before publishing the selected identity.
bool OcOccupant::GetSubSystemData(u32 id, void **out) {
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
SpDefinition *OcOccupant::GetSpriteAttrib() {
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Borrow the requested layer definition from generic attributes. Missing attributes produce a null pointer.
SpDefinition *OcOccupant::GetSpriteAttrib(u32 layer) {
  return attributes ? attributes->GetSpriteAttrib(layer) : nullptr;
}

// Borrow the stored sprite instance without changing ownership.
SpSprite *OcOccupant::GetSpriteInst() {
  return sprite;
}

// Compare against the virtual default layer before fetching the virtual instance. Other layers have no instance.
SpSprite *OcOccupant::GetSpriteInst(u32 layer) {
  return layer == GetDefaultLayerFlag() ? GetSpriteInst() : nullptr;
}

u32 OcOccupant::GetDefaultLayerFlag() const {
  return attributes ? attributes->GetDefaultLayerFlag() : 1;
}

u8 OcOccupant::SizeInCellsX() const {
  return attributes ? u8(attributes->SizeInCellsX()) : 0;
}

u8 OcOccupant::SizeInCellsZ() const {
  return attributes ? u8(attributes->SizeInCellsZ()) : 0;
}

u8 OcOccupant::SizeInCellsY() const {
  return attributes ? u8(attributes->SizeInCellsY()) : 0;
}

u32 OcOccupant::SizeInAnimCellsX() const {
  return u32(SizeInCellsX()) << 8;
}

u32 OcOccupant::SizeInAnimCellsZ() const {
  return u32(SizeInCellsZ()) << 8;
}

u32 OcOccupant::SizeInAnimCellsY() const {
  return u32(SizeInCellsY()) << 8;
}

// Publish both cell coordinates through the virtual getter, then scale the live outputs in original store order.
void OcOccupant::GetLocationInAnimCellCoords(u32 *x, u32 *z) const {
  GetLocationInCellCoords(x, z);
  *x <<= 8;
  *z <<= 8;
}

u32 OcOccupant::GetMemUse() const {
  return 20;
}

bool OcOccupant::DebugInstanceTag(ScStringView *) {
  return false;
}

bool OcOccupant::DebugClassTag(ScStringView *) {
  return false;
}

bool OcOccupant::DebugTypeTag(ScStringView *) {
  return false;
}

u32 OcOccupant::GetSoundObjectId(i32 index) {
  return GetAttribData()->GetSoundObjectId(&index);
}

// Leave output untouched without attributes. Otherwise capture the minimum before callbacks and compute Y, Z then X
// extents, reloading the packed position after each virtual size getter before publishing maximum coordinates.
void OcOccupant::GetBounds(GeBounds *out) const {
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
void OcOccupant::GetBoundingRectInCellCoords(GeCellRect *out) const {
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
bool OcOccupant::CreateSprite(u32 layer, SpSprite **out) {
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
u8 OcOccupant::SetRotationFlag(u32 rotation) {
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
bool OcOccupant::Init(RsKey *key) {
  if (attributes)
    return true;
  GeAttrib *value = nullptr;
  if (!ScGetMiscAttribCache()->GetOccupantAttrib(*key, &value)) {
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
bool OcOccupant::Init(GeAttrib *value) {
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
bool OcOccupant::Init(CtCity *, OcPersistInfo *info) {
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
CtAgentType *OcOccupant::CityAgentType() {
  return attributes ? attributes->CityAgentType() : nullptr;
}

// Clear the real query-window output before testing attributes. Resolve the configured class through this module's
// actual COM service, passing the caller's live output directly across that external interface boundary.
bool OcOccupant::CreateQueryDialog(UiQueryWindow **out) {
  *out = nullptr;
  if (!attributes)
    return false;
  u32 class_id = attributes->GetQueryDialogClass();
  return ScGetMiscDirector()->GZCOM()->GetClassObject(class_id, IID_UiQueryWindow, reinterpret_cast<void **>(out));
}

u8 OcOccupant::SetDefaultLayerFlag(u32) {
  return 0;
}

// Write generic occupant properties to the actual descriptor in source order. The original invokes the X-size
// getter twice: first for the second size argument, then for the first; preserve these distinct virtual calls.
bool OcOccupant::Save(CtCity *, OcPersistInfo *info) {
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
void OcOccupant::Shutdown() {
  ScRelease(&attributes);
  DestroySprites(&sprite);
  flags &= kOcFlagsWithoutSubtypeOrMembership;
}

u16 OcOccupant::GetAltAnimCells() const {
  return u16((position >> 14) & 0xff00);
}

// Route the four query notifications through their actual interfaces and private virtual handlers. Always return
// false after releasing the live QI output, including when a handler successfully populates the query.
bool OcOccupant::DoQueryInfo(ScMessage *message, ScUnknown *sender) {
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

u8 OcOccupant::QueryBasic(ScMessage *, UiQueryWindow *) {
  return 0;
}

u8 OcOccupant::QueryDetail(ScMessage *, UiQuery *) {
  return 0;
}

u8 OcOccupant::QueryResponse(ScMessage *, UiQueryWindow *) {
  return 0;
}

// Publish the nonempty architect from the borrowed override attribute, ignoring its Boolean getter result. Retire the
// localized label resource before adding the row, and preserve the original all-category mask and no-warning byte.
u8 OcOccupant::QueryTile(ScMessage *, UiQuery *query) {
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
  ScString label;
  {
    CtMiscTextResource resource(kRsSupplementalGameArchitectText, kRsSupplementalGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    label = copy;
  }
  query->AddText(&label, &architect, kUiAllQueryCategories, 0);
  return 1;
}

void OcOccupant::SetAltAnimCells(u16 altitude) {
  position = (position & kOcPositionWithoutAltitude) | (u32(altitude & 0xff00) << kOcAltitudeFromMicroShift);
}

// Restore the original key, subtype, membership, removal permissions, position and orientation from the stream.
bool OcOccupant::ReadState(CtCity *, RsStream *record) {
  RsKey key;
  record->ReadU32(&key.type);
  record->ReadU32(&key.group);
  record->ReadU32(&key.instance);
  Init(&key);
  u32 unused_type = 0;
  record->ReadU32(&unused_type);
  u8 unused_bytes[5]{};
  for (u32 index = 0; index < 5; ++index)
    record->ReadU8(&unused_bytes[index]);
  u8 subtype = 0, marked = 0, memberships = 0, simulation = 0, user = 0;
  record->ReadU8(&subtype);
  SetSubTypeInfo(subtype);
  record->ReadU8(&marked);
  SetMarkBit(marked != 0);
  record->ReadU8(&memberships);
  SetIsInCityFlag(memberships);
  record->ReadU8(&simulation);
  SetSimRemove(simulation != 0);
  record->ReadU8(&user);
  SetUserRemove(user != 0);
  CtCoord coordinate = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  u32 x = 0, z = 0, altitude = 0;
  record->ReadU32(&x);
  record->ReadU32(&z);
  record->ReadU32(&altitude);
  coordinate.x = i32(x << 8);
  coordinate.z = i32(z << 8);
  coordinate.altitude = i32(altitude << 8);
  SetLocation(coordinate);
  u16 rotation = 0, pitch = 0, roll = 0;
  record->ReadU16(&rotation);
  record->ReadU16(&pitch);
  record->ReadU16(&roll);
  SetOrientation(rotation, pitch, roll);
  return true;
}

// Query the retained attributes' resource identity, then write the fields through separate virtual getters.
bool OcOccupant::WriteState(CtCity *, RsStream *record) {
  GeAttrib *attrib = OcOccupant::GetAttribData();
  RsResource *resource = nullptr;
  if (!attrib || !attrib->QueryInterface(IID_RsResource, reinterpret_cast<void **>(&resource)))
    return false;
  record->WriteU32(*static_cast<const RsResource *>(resource)->Type());
  record->WriteU32(*static_cast<const RsResource *>(resource)->Group());
  record->WriteU32(*static_cast<const RsResource *>(resource)->Instance());
  resource->Release();
  record->WriteU32(TypeId());
  record->WriteU8(MajorType());
  record->WriteU8(AnimType());
  record->WriteU8(SizeInCellsX());
  record->WriteU8(SizeInCellsZ());
  record->WriteU8(SizeInCellsY());
  record->WriteU8(SubTypeInfo());
  record->WriteU8(GetMarkBit());
  record->WriteU8(u8(GetIsInCityFlag()));
  record->WriteU8(CanSimRemove());
  record->WriteU8(CanUserRemove());
  CtCoord coordinate = {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
  GetLocation(&coordinate);
  record->WriteU32(u32(coordinate.x >> 8));
  record->WriteU32(u32(coordinate.z >> 8));
  record->WriteU32(u32(coordinate.altitude >> 8));
  u16 rotation = 0, pitch = 0, roll = 0;
  GetOrientation(&rotation, &pitch, &roll);
  record->WriteU16(rotation);
  record->WriteU16(pitch);
  record->WriteU16(roll);
  return true;
}

OcMiscBuilding::OcMiscBuilding() : custom_name(nullptr) {
}

OcMiscBuilding::~OcMiscBuilding() {
  ScDelete(&custom_name);
}

bool OcMiscBuilding::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_OcMiscOccupant, OcMiscOccupant);
  return OcBuilding::QueryInterface(iid, out);
}

u32 OcMiscBuilding::AddRef() {
  return OcBuilding::AddRef();
}

u32 OcMiscBuilding::Release() {
  return OcBuilding::Release();
}

// Borrow the real occupant subobject from the extra conversion view without querying or retaining it.
ScOccupant *OcMiscBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the controlling building primary from the actual extra conversion view.
BdBuilding *OcMiscBuilding::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the actual city-agent subobject without adding another reference or consulting the city.
CtAgent *OcMiscBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Bind the building first, then allocate a custom name only for a nameable receiver and copy the input's default name.
// Keep the original attribute input for that callback, and ignore the attribute name getter's result.
bool OcMiscBuilding::Init(GeAttrib *value) {
  if (!OcBuilding::Init(value))
    return false;
  if (IsNameChangeable() && value) {
    if (!custom_name)
      custom_name = new ScString;
    value->GetName(custom_name);
  }
  return true;
}

// Copy an allocated custom name only while the virtual predicate permits it. Otherwise use the qualified occupant name.
bool OcMiscBuilding::GetName(ScStringView *out) {
  if (!IsNameChangeable() || !custom_name)
    return OcOccupant::GetName(out);
  out->Copy(*custom_name);
  return true;
}

// Refuse nonnameable receivers and empty input. Copy through a temporary String before replacing the owned name.
bool OcMiscBuilding::SetName(const ScStringView &value) {
  if (!IsNameChangeable() || !value.Strlen())
    return false;
  ScString copy(value);
  *custom_name = copy;
  return true;
}

u32 OcMiscBuilding::GetGeomFlag() {
  return 260;
}

void OcMiscBuilding::SetOccupancyState(u32 value) {
  OcBuilding::SetOccupancyState(value);
}

u8 OcMiscBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  query->SetCaptionEnabled(IsNameChangeable());
  return OcBuilding::QueryBasic(message, query);
}

// Add the optional localized construction-date row, then delegate to the building's ordinary tile information.
u8 OcMiscBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (HasBirthdate() && query) {
    ScString label, value;
    EcDate date;
    GetBirthdate(&date);
    ScString working, date_text;
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    {
      CtMiscTextResource resource(kRsCityServiceDateBuiltText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceConstructionDateFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    u32 year = date.Year(), day = date.DayOfMonth(), month = date.Month();
    utility->MakeDateString(i32(month), i32(day), year, &date_text, ScLanguageUtility::DateFormat::NumericMonthDayYear);
    working.Sprintf(working.Data(), date_text.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQueryConstructionDateCategory, 0);
  }
  return OcBuilding::QueryTile(message, query);
}

// Apply the query's borrowed edited caption through the virtual name setter before processing its occupancy option.
u8 OcMiscBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (IsNameChangeable())
    SetName(*query->GetCaption());
  return OcBuilding::QueryResponse(message, query);
}

OcMiscAnimBuilding::OcMiscAnimBuilding() : custom_name(nullptr) {
}

OcMiscAnimBuilding::~OcMiscAnimBuilding() {
  ScDelete(&custom_name);
}

bool OcMiscAnimBuilding::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_OcMiscOccupant, OcMiscOccupant);
  return OcStaticAnimBuilding::QueryInterface(iid, out);
}

u32 OcMiscAnimBuilding::AddRef() {
  return OcStaticAnimBuilding::AddRef();
}

u32 OcMiscAnimBuilding::Release() {
  return OcStaticAnimBuilding::Release();
}

// Borrow the real occupant subobject from the extra conversion view without querying or retaining it.
ScOccupant *OcMiscAnimBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the controlling building primary from the actual extra conversion view.
BdBuilding *OcMiscAnimBuilding::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the actual city-agent subobject without adding another reference or consulting the city.
CtAgent *OcMiscAnimBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Bind the building first, then allocate a custom name only for a nameable receiver and copy the input's default name.
// Keep the original attribute input for that callback, and ignore the attribute name getter's result.
bool OcMiscAnimBuilding::Init(GeAttrib *value) {
  if (!OcStaticAnimBuilding::Init(value))
    return false;
  if (IsNameChangeable() && value) {
    if (!custom_name)
      custom_name = new ScString;
    value->GetName(custom_name);
  }
  return true;
}

// Copy an allocated custom name only while the virtual predicate permits it. Otherwise use the qualified occupant name.
bool OcMiscAnimBuilding::GetName(ScStringView *out) {
  if (!IsNameChangeable() || !custom_name)
    return OcOccupant::GetName(out);
  out->Copy(*custom_name);
  return true;
}

// Refuse nonnameable receivers and empty input. Copy through a temporary String before replacing the owned name.
bool OcMiscAnimBuilding::SetName(const ScStringView &value) {
  if (!IsNameChangeable() || !value.Strlen())
    return false;
  ScString copy(value);
  *custom_name = copy;
  return true;
}

u32 OcMiscAnimBuilding::GetGeomFlag() {
  return 260;
}

void OcMiscAnimBuilding::SetOccupancyState(u32 value) {
  OcStaticAnimBuilding::SetOccupancyState(value);
}

u8 OcMiscAnimBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  query->SetCaptionEnabled(IsNameChangeable());
  return OcStaticAnimBuilding::QueryBasic(message, query);
}

// Add the optional localized construction-date row, then delegate to the building's ordinary tile information.
u8 OcMiscAnimBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (HasBirthdate() && query) {
    ScString label, value;
    EcDate date;
    GetBirthdate(&date);
    ScString working, date_text;
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    {
      CtMiscTextResource resource(kRsCityServiceDateBuiltText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceConstructionDateFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    u32 year = date.Year(), day = date.DayOfMonth(), month = date.Month();
    utility->MakeDateString(i32(month), i32(day), year, &date_text, ScLanguageUtility::DateFormat::NumericMonthDayYear);
    working.Sprintf(working.Data(), date_text.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQueryConstructionDateCategory, 0);
  }
  return OcStaticAnimBuilding::QueryTile(message, query);
}

// Apply the query's borrowed edited caption through the virtual name setter before processing its occupancy option.
u8 OcMiscAnimBuilding::QueryResponse(ScMessage *message, UiQueryWindow *query) {
  if (IsNameChangeable())
    SetName(*query->GetCaption());
  return OcStaticAnimBuilding::QueryResponse(message, query);
}

u8 OcStaticAnimBuilding::AnimType() const {
  return 1;
}

// Acquire the actual secondary animation view and start it at the captured packed location and live width.
// The original retains only eight altitude bits before shifting them into animation units.
void OcStaticAnimBuilding::AnimStart() {
  SpAnimation *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, reinterpret_cast<void **>(&animation))) {
    u32 location = position;
    u8 width = SizeInCellsX();
    animation->Start(i32((location & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(((location >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(u32(u8(location >> kOcPositionAltitudeShift)) << kCtMicroCoordinateShift), width);
    animation->Release();
  }
}

// Stop the current sprite through its acquired secondary animation identity, then retire that identity.
void OcStaticAnimBuilding::AnimStop() {
  SpAnimation *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, reinterpret_cast<void **>(&animation))) {
    animation->Stop();
    animation->Release();
  }
}

i32 OcStaticAnimBuilding::Tick(u32 delta) {
  SpSprite *current = GetSpriteInst();
  return current ? current->Tick(delta) : 1;
}

OcDealBuilding::OcDealBuilding() {
}

OcDealBuilding::~OcDealBuilding() = default;

bool OcDealBuilding::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdDealView, BdDealView);
  return OcBuilding::QueryInterface(iid, out);
}

u32 OcDealBuilding::AddRef() {
  return OcBuilding::AddRef();
}

u32 OcDealBuilding::Release() {
  return OcBuilding::Release();
}

// Borrow the actual occupant subobject without acquiring a separate reference.
ScOccupant *OcDealBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the real controlling building primary from the extra business-deal view.
BdBuilding *OcDealBuilding::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the actual city-agent subobject without another query or module hold.
CtAgent *OcDealBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Require the original water, power, occupancy and routed-trip results after performing every service callback.
bool OcDealBuilding::IsOperational() {
  CtPowerLayer *power = ScGetMiscCity()->PowerLayer();
  CtPlumbingLayer *plumbing = ScGetMiscCity()->PlumbingLayer();
  NwTrafficLayer *traffic = ScGetMiscCity()->TrafficLayer();
  if (!power || !traffic)
    return false;
  bool powered = false;
  power->GetValue(
      position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask, &powered);
  u8 watered = plumbing->IsWatered(
      position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask);
  u8 routed = traffic->RouteOccupantTrip(AsISC3Occupant(), 3, 255, 0, 1, 1, 1);
  bool occupied = GetOccupancyState() == 1;
  return watered != 0 && powered && occupied && routed != 0;
}

bool OcDealBuilding::HasBirthdate() const {
  return true;
}

bool OcDealBuilding::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface into its actual value representation, then copy only the day word.
bool OcDealBuilding::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

u32 OcDealBuilding::GetGeomFlag() {
  return 260;
}

// Add the existing trip-status text only for an exact byte-one trip result, then ordinary building query behavior.
u8 OcDealBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  NwTrafficLayer *traffic = ScGetMiscCity()->TrafficLayer();
  if (!traffic)
    return 0;
  u8 trip = traffic->IsTripAvailable(
      position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask);
  ScString status;
  if (trip == 1) {
    {
      CtMiscTextResource resource(kRsCityServiceNoAccessToCityText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      status = copy;
    }
    query->SetStatusText(status);
  }
  return OcBuilding::QueryBasic(message, query);
}

u8 OcDealBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (HasBirthdate() && query) {
    ScString label, value;
    EcDate date;
    GetBirthdate(&date);
    ScString working, date_text;
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    {
      CtMiscTextResource resource(kRsCityServiceDateBuiltText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceConstructionDateFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    u32 year = date.Year(), day = date.DayOfMonth(), month = date.Month();
    utility->MakeDateString(i32(month), i32(day), year, &date_text, ScLanguageUtility::DateFormat::NumericMonthDayYear);
    working.Sprintf(working.Data(), date_text.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQueryConstructionDateCategory, 0);
    AdBudget *advisor = ScGetMiscCity()->GetBudgetAdvisor();
    i32 income = 0;
    if (IsOperational())
      income = i32(advisor->GetBusinessDealIncomeForBuildingType(static_cast<CtAgent *>(this)->TypeId()));
    ScString money;
    {
      CtMiscTextResource resource(kRsCityServiceMonthlyIncomeText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceIncomeFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeMoneyString(income, &money);
    working.Sprintf(working.Data(), money.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, 0);
  }
  return OcBuilding::QueryTile(message, query);
}

OcDealAnimBuilding::OcDealAnimBuilding() {
}

OcDealAnimBuilding::~OcDealAnimBuilding() = default;

bool OcDealAnimBuilding::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdDealView, BdDealView);
  return OcStaticAnimBuilding::QueryInterface(iid, out);
}

u32 OcDealAnimBuilding::AddRef() {
  return OcStaticAnimBuilding::AddRef();
}

u32 OcDealAnimBuilding::Release() {
  return OcStaticAnimBuilding::Release();
}

// Borrow the actual occupant subobject without acquiring a separate reference.
ScOccupant *OcDealAnimBuilding::AsISC3Occupant() {
  return static_cast<OcOccupant *>(this);
}

// Borrow the real controlling building primary from the extra business-deal view.
BdBuilding *OcDealAnimBuilding::AsISC3Building() {
  return static_cast<BdBuilding *>(this);
}

// Borrow the actual city-agent subobject without another query or module hold.
CtAgent *OcDealAnimBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Require the original water, power, occupancy and routed-trip results after performing every service callback.
bool OcDealAnimBuilding::IsOperational() {
  CtPowerLayer *power = ScGetMiscCity()->PowerLayer();
  CtPlumbingLayer *plumbing = ScGetMiscCity()->PlumbingLayer();
  NwTrafficLayer *traffic = ScGetMiscCity()->TrafficLayer();
  if (!power || !traffic)
    return false;
  bool powered = false;
  power->GetValue(
      position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask, &powered);
  u8 watered = plumbing->IsWatered(
      position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask);
  u8 routed = traffic->RouteOccupantTrip(AsISC3Occupant(), 3, 255, 0, 1, 1, 1);
  bool occupied = GetOccupancyState() == 1;
  return watered != 0 && powered && occupied && routed != 0;
}

bool OcDealAnimBuilding::HasBirthdate() const {
  return true;
}

bool OcDealAnimBuilding::GetBirthdate(CtDateInterface *out) const {
  out->Set(birthdate.serial);
  return true;
}

// Convert the source date interface into its actual value representation, then copy only the day word.
bool OcDealAnimBuilding::SetBirthdate(const CtDateInterface &value) {
  CtDate copy(value);
  birthdate.serial = copy.serial;
  return true;
}

u32 OcDealAnimBuilding::GetGeomFlag() {
  return 260;
}

// Add the existing trip-status text only for an exact byte-one trip result, then ordinary building query behavior.
u8 OcDealAnimBuilding::QueryBasic(ScMessage *message, UiQueryWindow *query) {
  NwTrafficLayer *traffic = ScGetMiscCity()->TrafficLayer();
  if (!traffic)
    return 0;
  u8 trip = traffic->IsTripAvailable(
      position & kOcPositionCellValueMask, (position >> kOcPositionZShift) & kOcPositionCellValueMask);
  ScString status;
  if (trip == 1) {
    {
      CtMiscTextResource resource(kRsCityServiceNoAccessToCityText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      status = copy;
    }
    query->SetStatusText(status);
  }
  return OcStaticAnimBuilding::QueryBasic(message, query);
}

u8 OcDealAnimBuilding::QueryTile(ScMessage *message, UiQuery *query) {
  if (HasBirthdate() && query) {
    ScString label, value;
    EcDate date;
    GetBirthdate(&date);
    ScString working, date_text;
    ScLanguageUtility *utility = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    {
      CtMiscTextResource resource(kRsCityServiceDateBuiltText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceConstructionDateFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    u32 year = date.Year(), day = date.DayOfMonth(), month = date.Month();
    utility->MakeDateString(i32(month), i32(day), year, &date_text, ScLanguageUtility::DateFormat::NumericMonthDayYear);
    working.Sprintf(working.Data(), date_text.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQueryConstructionDateCategory, 0);
    AdBudget *advisor = ScGetMiscCity()->GetBudgetAdvisor();
    i32 income = 0;
    if (IsOperational())
      income = i32(advisor->GetBusinessDealIncomeForBuildingType(static_cast<CtAgent *>(this)->TypeId()));
    ScString money;
    {
      CtMiscTextResource resource(kRsCityServiceMonthlyIncomeText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    label.Copy(working);
    {
      CtMiscTextResource resource(kRsCityServiceIncomeFormatText, kRsCityServiceTextGroup);
      ScString text = resource.GetText(), copy(text);
      working = copy;
    }
    utility->MakeMoneyString(income, &money);
    working.Sprintf(working.Data(), money.Data());
    value.Copy(working);
    query->AddText(&label, &value, kUiQueryPrimaryDetailsCategory, 0);
  }
  return OcStaticAnimBuilding::QueryTile(message, query);
}
