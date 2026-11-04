#include "geometry/building.h"

#include <assert.h>
#include <stdio.h>

#include <utility>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/city.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/tool.h"
#include "geometry/utilities.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "window/api.h"

// Absent building name and query-text keys use the original missing-text instance.
enum GeAttributeTextFallback : u32 {
  kGeMissingBuildingTextInstance = 0xffffffff,
};

// Query the borrowed sprite for its animation view, then snapshot the packed position before the virtual width query.
void GeAnimatedBuilding::AnimStart() {
  void *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, &animation)) {
    u32 packed = position, argument = SizeInCellsX();
    static_cast<SpAnimation *>(animation)->Start(i32((packed & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(((packed >> kOcPositionZShift) & kOcPositionCellValueMask) << kCtMicroCoordinateShift),
        i32(u32(u8(packed >> kOcPositionAltitudeShift)) << kCtMicroCoordinateShift), argument);
    static_cast<SpAnimation *>(animation)->Release();
  }
}

// Stop through the queried animation view without reading location or width. A missing sprite or refused query performs
// no animation call and does not release a pointer published by a failed query.
void GeAnimatedBuilding::AnimStop() {
  void *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, &animation)) {
    static_cast<SpAnimation *>(animation)->Stop();
    static_cast<SpAnimation *>(animation)->Release();
  }
}

u8 GeAnimatedBuilding::AnimType() const {
  return 1;
}

i32 GeAnimatedBuilding::Tick(u32 elapsed) {
  SpSprite *current = GetSpriteInst();
  return current ? current->Tick(elapsed) : 1;
}

// Construct the actual animated building and publish its occupant subobject without acquiring a reference.
ScUnknown *GeCreateAnimatedBuilding() {
  return static_cast<GeOccupant *>(new GeAnimatedBuilding);
}

GeBuilding::GeBuilding() : occupancyFlags(0), agentType(nullptr), cachedAttrib(nullptr), cachedSprite(nullptr) {
}

GeBuilding::~GeBuilding() {
  ScRelease(&agentType);
}

// Clear output before testing the building and agent identities. Other identities delegate to the occupant subobject,
// preserving its adjusted successful output and virtual reference acquisition.
bool GeBuilding::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_BdBuilding, BdBuilding);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  return GeOccupant::QueryInterface(iid, out);
}

u32 GeBuilding::AddRef() {
  return GeOccupant::AddRef();
}

u32 GeBuilding::Release() {
  return GeOccupant::Release();
}

// Return the borrowed occupant subobject without querying another identity or acquiring a reference.
ScOccupant *GeBuilding::AsISC3Occupant() {
  return static_cast<GeOccupant *>(this);
}

// Return the borrowed city-agent subobject. The original folded body also serves other classes with a+4 interface.
CtAgent *GeBuilding::AsISC3CityAgent() {
  return static_cast<CtAgent *>(this);
}

// Borrow the stored agent type without acquiring it. This pointer belongs to the complete building receiver at+32.
CtAgentType *GeBuilding::AgentType() {
  return agentType;
}

bool GeBuilding::SetAgentType(CtAgentType *) {
  return false;
}

u32 GeBuilding::TypeId() const {
  return GeOccupant::TypeId();
}

// Borrow the original attribute pointer directly from the occupant base. This getter does not query specialized data.
GeAttrib *GeBuilding::GetBaseAttribData() {
  return GeOccupant::GetAttribData();
}

u32 GeBuilding::GetOccupancyState() {
  return u8(occupancyFlags);
}

bool GeBuilding::GetOccupancyStateFlag(u32 mask) {
  return (occupancyFlags & mask) != 0;
}

// Query specialized building attributes from the virtual generic attribute getter. Preserve the selected power byte
// before releasing the acquired view; missing attributes or a failed query return zero.
u8 GeBuilding::PowerRequired() {
  GeAttrib *value = GetAttribData();
  if (!value)
    return 0;
  void *building;
  if (!value->QueryInterface(IID_GeBuildingType, &building))
    return 0;
  u8 required = static_cast<GeBuildingType *>(building)->PowerRequired();
  static_cast<GeBuildingType *>(building)->Release();
  return required;
}

bool GeBuilding::IsAgentLocking() {
  return true;
}

bool GeBuilding::DebugInstanceTag(ScStringView *) {
  return false;
}

bool GeBuilding::DebugClassTag(ScStringView *) {
  return false;
}

bool GeBuilding::DebugTypeTag(ScStringView *) {
  return false;
}

// Initialize the occupant base before invalidating caches and retaining a missing agent type from its base view.
// Rejected base initialization dispatches Shutdown; failed sprite creation can therefore dispatch it twice.
bool GeBuilding::Init(GeAttrib *value) {
  if (!GeOccupant::Init(value)) {
    Shutdown();
    return false;
  }
  cachedAttrib = nullptr;
  cachedSprite = nullptr;
  if (!agentType) {
    agentType = GetBaseAttribData()->CityAgentType();
    if (agentType)
      agentType->AddRef();
  }
  return true;
}

bool GeBuilding::Simulate(i32 elapsed) {
  return agentType->Simulate(static_cast<CtAgent *>(this), elapsed);
}

i32 GeBuilding::MinimumExecutionInterval() {
  return agentType ? agentType->MinimumExecutionInterval() : 6;
}

u32 GeBuilding::GetGeomFlag() {
  return 0x104;
}

u32 GeBuilding::GetMemUse() const {
  return 44;
}

bool GeBuilding::Init(RsKey *key) {
  return GeOccupant::Init(key);
}

// Borrow cached building attributes, selecting the original base view for the two special occupancy forms. Other
// states query the live city building layer, then cache its result or the current base pointer without retaining it.
GeAttrib *GeBuilding::GetAttribData() {
  if (!cachedAttrib) {
    if (occupancyFlags == 1 || (u8(occupancyFlags) == 1 && (occupancyFlags & 0xff00) == 0x200))
      cachedAttrib = GeOccupant::GetAttribData();
    else {
      GeBuildingLayer *layer = GeGetCity()->BuildingLayer();
      if (layer)
        cachedAttrib = layer->GetDefaultAttribData(static_cast<BdBuilding *>(this));
      if (!layer || !cachedAttrib)
        cachedAttrib = GeOccupant::GetAttribData();
    }
  }
  return cachedAttrib;
}

// Sample the virtual default layer even when a sprite is already cached. Delegate directly to this class's indexed
// implementation rather than dispatching a derived indexed override.
SpDefinition *GeBuilding::GetSpriteAttrib() {
  return GeBuilding::GetSpriteAttrib(GetDefaultLayerFlag());
}

// Borrow the single cached sprite definition. Special occupancy forms forward the requested layer to base attributes;
// other states query building-layer defaults and use the unindexed base fallback, preserving the original layer choice.
SpDefinition *GeBuilding::GetSpriteAttrib(u32 layer) {
  if (!cachedSprite) {
    if (occupancyFlags == 1 || (u8(occupancyFlags) == 1 && (occupancyFlags & 0xff00) == 0x200))
      cachedSprite = GeOccupant::GetSpriteAttrib(layer);
    else {
      GeBuildingLayer *buildings = GeGetCity()->BuildingLayer();
      if (buildings)
        cachedSprite = buildings->GetDefaultSpriteData(static_cast<BdBuilding *>(this));
      if (!buildings || !cachedSprite)
        cachedSprite = GeOccupant::GetSpriteAttrib();
    }
  }
  return cachedSprite;
}

bool GeBuilding::SetName(const ScStringView &) {
  return false;
}

bool GeBuilding::IsNameChangeable() {
  return false;
}

bool GeBuilding::HasBirthdate() const {
  return false;
}

bool GeBuilding::GetBirthdate(CtDateInterface *) const {
  return false;
}

bool GeBuilding::SetBirthdate(const CtDateInterface &) {
  return false;
}

// Release the retained agent type before clearing both borrowed attribute/sprite caches. Then run the actual qualified
// occupant cleanup; none of these operations changes the building occupancy word or reports synthetic success.
void GeBuilding::Shutdown() {
  ScRelease(&agentType);
  cachedAttrib = nullptr;
  cachedSprite = nullptr;
  GeOccupant::Shutdown();
}

// Original scoped graphics lock used around occupancy-driven sprite replacement.
class GeGraphicsLockScope {
public:
  explicit GeGraphicsLockScope(ScLock *lock);
  ~GeGraphicsLockScope();

private:
  ScLock *lock;
};

GeGraphicsLockScope::GeGraphicsLockScope(ScLock *lock) : lock(lock) {
  lock->Lock();
}

GeGraphicsLockScope::~GeGraphicsLockScope() {
  lock->Unlock();
}

// Invalidate only the attribute cache, use the selected definition for both instance operations and retain any failed
// creation publication.
void GeBuilding::RefreshSprite() {
  GeGraphicsLockScope scope(GeGetCity()->GetGraphicsCriticalSection());
  cachedAttrib = nullptr;
  SpDefinition *definition = GetSpriteAttrib();
  if (!definition)
    return;
  if (sprite) {
    definition->DestroyInstance(&sprite);
    sprite = nullptr;
  }
  if (definition->CreateInstance(&sprite))
    sprite->SetRotationOffset(u8(position >> 30));
}

// Replace the complete occupancy word with the input's low byte. A changed word always dispatches sprite refresh.
void GeBuilding::SetOccupancyState(u32 value) {
  u32 state = u8(value);
  if (occupancyFlags != state) {
    occupancyFlags = state;
    RefreshSprite();
  }
}

// Add the full input mask, refreshing only a changed word whose requested bits include the low occupancy byte.
void GeBuilding::SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancyFlags | mask;
  if (value != occupancyFlags) {
    occupancyFlags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Remove the requested bits. High-byte-only changes preserve the original sprite instance.
void GeBuilding::ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) {
  u32 value = occupancyFlags & ~mask;
  if (value != occupancyFlags) {
    occupancyFlags = value;
    if (u8(mask))
      RefreshSprite();
  }
}

// Construct the actual building and return its occupant subobject without acquiring a reference.
ScUnknown *GeCreateBuilding() {
  return static_cast<GeOccupant *>(new GeBuilding);
}

// Resolve the original building-query resource into an independent String before retiring its retained interface.
static ScString GeQueryString(u32 instance) {
  GeTextResource resource(instance, kRsCityServiceTextGroup);
  ScString raw = resource;
  return ScString(raw);
}

// Expand the query's single String argument through the C runtime before publishing its independent String value.
// Size the temporary buffer first; allocation and unrepresentable output lengths use the native fatal boundary.
static ScString GeFormatQueryValue(const ScString &format, const ScString &value) {
  int length = snprintf(nullptr, 0, format.Data(), value.Data());
  if (length < 0)
    ScFatal(SC_ERROR_INVALID, "Building query text exceeds the formatting range.");
  char *bytes = new char[usize(length) + 1];
  snprintf(bytes, usize(length) + 1, format.Data(), value.Data());
  ScString formatted(bytes, u32(length));
  delete[] bytes;
  return formatted;
}

// Select the first matching occupancy-status message, then append the original protection option with borrowed font15.
// Preserve service and predicate order before delegating to the occupant's qualified query-window hook.
u8 GeBuilding::DoQueryViews(ScMessage *message, UiQueryWindow *window) {
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
    status = GeQueryString(instance);
    window->SetStatusText(status);
  }
  ScString caption = GeQueryString(kRsCityServiceMakeHistoricalText);
  WnResources *resources = GeGetWindowResources();
  bool checked = GetOccupancyStateFlag(kGeBuildingHistorical);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  window->AddOption(caption, font, checked);
  return GeOccupant::DoQueryViews(message, window);
}

// Add the localized demolition-cost row for the selected occupancy states, sampling depth before width at the live price.
// Missing locale utility refuses immediately; other paths finish through the occupant's architect-name query.
u8 GeBuilding::DoStandardQueryViewInfo(ScMessage *message, UiQuery *query) {
  if (GetOccupancyStateFlag(8) || GetOccupancyStateFlag(16)) {
    ScLanguageUtility *utility = GeGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return 0;
    EcBudgetLayer *budget = GeGetCity()->BudgetLayer();
    if (budget) {
      i32 unitPrice = budget->GetCost(kEcCostDemolish);
      u32 price = u32(unitPrice) * SizeInCellsZ();
      price = u32(SizeInCellsX()) * price;
      ScString label = GeQueryString(kRsCityServiceCleanUpCostText),
               format = GeQueryString(kRsCityServiceCleanUpCostFormatText), money;
      utility->MakeMoneyString(i64(i32(price)), &money);
      ScString formatted = GeFormatQueryValue(format, money);
      query->AddText(&label, &formatted, 16, 0);
    }
  }
  return GeOccupant::DoStandardQueryViewInfo(message, query);
}

u8 GeBuilding::DoDebugQueryViewInfo(ScMessage *message, UiQuery *query) {
  return GeOccupant::DoDebugQueryViewInfo(message, query);
}

// Apply the query option to occupancy bit512 through the original virtual setters, even when the bit already agrees.
// The qualified occupant close hook runs after that state change and supplies the method's final byte.
u8 GeBuilding::DoExitQueryWindow(ScMessage *message, UiQueryWindow *window) {
  if (window->IsOptionChecked())
    SetOccupancyStateFlag(kGeBuildingHistorical);
  else
    ClearOccupancyStateFlag(kGeBuildingHistorical);
  return GeOccupant::DoExitQueryWindow(message, window);
}

// Read the base occupant before the signed state word, then apply the original state-flag setter even when the read
// reports failure after publishing. Preserve that read reply across the setter's callbacks; zero state becomes one.
u8 GeBuilding::SerialRead(CtCity *city, RsStream *record) {
  u8 result = GeOccupant::SerialRead(city, record);
  if (result) {
    i32 state = 0;
    result = u8(record->ReadI32(&state));
    if (!state)
      state = 1;
    SetOccupancyStateFlag(u32(state));
  }
  return result;
}

// Write the base occupant, then append the live complete occupancy word as signed transport data.
u8 GeBuilding::SerialWrite(CtCity *city, RsStream *record) {
  u8 result = GeOccupant::SerialWrite(city, record);
  if (result)
    result = u8(record->WriteI32(i32(occupancyFlags)));
  return result;
}

GeBuildingAttrib::GeBuildingAttrib()
    : occupantClass(0), geometryFlags(0), sizeX(0), sizeZ(0), sizeY(0), agentType(nullptr), primary{},
      peopleAttrib(nullptr), vehicleAttrib(nullptr), peopleKey{}, vehicleKey{}, soundA{}, soundB{}, cachedLayers(0),
      variants{}, nameKey{}, plopKey{}, queryKey{}, propertyFlag(0), overrideAttrib(nullptr) {
}

// Release the override and cached resources in original order. Clear each retained pointer after its Release before
// destroying the name and resource base.
GeBuildingAttrib::~GeBuildingAttrib() {
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
bool GeBuildingAttrib::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GePropertyObject, GePropertyObject);
  HANDLE_QUERY_INTERFACE(IID_GeAttrib, GeAttrib);
  return RsResource::QueryInterface(iid, out);
}

u32 GeBuildingAttrib::AddRef() {
  return RsResource::AddRef();
}

u32 GeBuildingAttrib::Release() {
  return RsResource::Release();
}

bool GeBuildingAttrib::IsGeomFlag(u32 mask) {
  return mask == (mask & geometryFlags);
}

u8 GeBuildingAttrib::SetGeomFlag(u32 mask) {
  geometryFlags |= mask;
  return true;
}

u8 GeBuildingAttrib::ClearGeomFlag(u32 mask) {
  geometryFlags &= ~mask;
  return true;
}

u32 GeBuildingAttrib::GetGeomFlag() {
  return geometryFlags;
}

// Cache the five-layer mask from the corresponding resource-key type words. A zero result is recomputed on the next
// call.
u8 GeBuildingAttrib::GetOccLayerFlag() {
  if (!cachedLayers) {
    cachedLayers = primary.key.type != 0;
    for (u32 i = 0; i < 4; ++i)
      if (variants[i].key.type)
        cachedLayers |= u8(2u << i);
  }
  return cachedLayers;
}

void GeBuildingAttrib::SetOccLayerFlag(u8) {
}

u8 GeBuildingAttrib::MajorType() const {
  return 0;
}

u8 GeBuildingAttrib::SubTypeInfo() const {
  return 0;
}

u32 GeBuildingAttrib::SizeInCellsX() {
  return sizeX;
}

u32 GeBuildingAttrib::SizeInCellsZ() {
  return sizeZ;
}

u32 GeBuildingAttrib::SizeInCellsY() {
  return sizeY;
}

u32 GeBuildingAttrib::TypeId() const {
  return occupantClass;
}

u32 GeBuildingAttrib::GetDefaultLayerFlag() const {
  return 1;
}

u8 GeBuildingAttrib::Flammability() {
  return flammability;
}

// Let a retained override supply the display name when present. Otherwise copy the stored name and report success.
bool GeBuildingAttrib::GetName(ScStringView *out) {
  if (overrideAttrib)
    return overrideAttrib->GetName(out);
  out->Copy(name);
  return true;
}

bool GeBuildingAttrib::GetArchitectName(ScStringView *out) {
  if (overrideAttrib)
    return overrideAttrib->GetArchitectName(out);
  ScString empty;
  out->Copy(empty);
  return false;
}

bool GeBuildingAttrib::GetNameKey(RsKey *out) {
  *out = nameKey;
  return true;
}

u32 GeBuildingAttrib::GetSoundObjectId(i32 *index) {
  return *index == 0 ? soundA.instance : *index == 1 ? soundB.instance : 0;
}

void GeBuildingAttrib::GetSoundKeyA(RsKey *out) {
  *out = soundA;
}

void GeBuildingAttrib::GetSoundKeyB(RsKey *out) {
  *out = soundB;
}

void GeBuildingAttrib::GetPlopCursorKey(RsKey *out) {
  *out = plopKey;
}

void GeBuildingAttrib::GetQueryDescKey(RsKey *out) {
  *out = queryKey;
}

void GeBuildingAttrib::GetQueryFormatFlags(u32 *out) {
  *out = queryFlags;
}

u32 GeBuildingAttrib::GetQueryDialogClass() {
  return queryDialogClass;
}

bool GeBuildingAttrib::SetQueryDialogClass(u32 clsid) {
  queryDialogClass = clsid;
  return true;
}

// Release the old override before storing and retaining its replacement. Preserve the original ordering even when the
// pointers are equal.
void GeBuildingAttrib::SetOverRideAttrib(OcOverride *value) {
  ScRelease(&overrideAttrib);
  overrideAttrib = value;
  if (value)
    value->AddRef();
}

// Borrow the retained override interface. This getter does not acquire a reference.
OcOverride *GeBuildingAttrib::GetOverRideAttrib() {
  return overrideAttrib;
}

u8 GeBuildingAttrib::SetPropertyFlag(u8 value) {
  propertyFlag = value;
  return value;
}

u8 GeBuildingAttrib::GetPropertyFlag() {
  return propertyFlag;
}

u8 GeBuildingAttrib::AnimType() const {
  return 0;
}

// Borrow the city-agent type resolved during property loading. This getter does not acquire a reference.
CtAgentType *GeBuildingAttrib::CityAgentType() {
  return agentType;
}

// Borrow the actual resource-base key. The original const interface exposes a mutable pointer without retaining it.
RsKey *GeBuildingAttrib::Key() const {
  return const_cast<RsKey *>(&key);
}

// A failed lookup clears even a nonnull output, while a successful lookup still requires an actual pointer.
static bool SC3_STDCALL GeResolveSprite(const RsKey *key, void **out) {
  if (!GeGetResourceManager()->GetResource(*key, IID_SpDefView, out))
    *out = nullptr;
  return *out != nullptr;
}

// Let the override select indexed art, otherwise choose the first supported mask bit. Cache misses retain the
// resource-manager output directly; returned definitions are borrowed.
SpDefinition *GeBuildingAttrib::GetSpriteAttrib(u32 layer) {
  if (overrideAttrib)
    return overrideAttrib->GetSpriteAttrib(layer);
  GeCachedArt *art;
  if (layer & 1)
    art = &primary;
  else if (layer & 2)
    art = &variants[0];
  else if (layer & 4)
    art = &variants[1];
  else
    return nullptr;
  if (art->resource || GeResolveSprite(&art->key, &art->resource))
    return static_cast<SpDefinition *>(art->resource);
  return nullptr;
}

// Borrow the primary sprite definition independently of any override. Resolve an empty cache through the same
// resource-manager acquisition used by indexed art.
SpDefinition *GeBuildingAttrib::GetOriginalSpriteAttrib() {
  if (primary.resource || GeResolveSprite(&primary.key, &primary.resource))
    return static_cast<SpDefinition *>(primary.resource);
  return nullptr;
}

// Borrow the override's unindexed art when one is installed. Otherwise dispatch the original-sprite getter virtually,
// preserving derived providers rather than directly selecting this class's primary cache.
SpDefinition *GeBuildingAttrib::GetSpriteAttrib() {
  return overrideAttrib ? overrideAttrib->GetSpriteAttrib() : GetOriginalSpriteAttrib();
}

bool GeBuildingAttrib::GetOccupantGZCLSID(u32 *) const {
  return false;
}

bool GeBuildingAttrib::SetOccupantGZCLSID(u32) {
  return false;
}

bool GeBuildingAttrib::SetDefaultLayerFlag(u32) {
  return false;
}

bool GeBuildingAttrib::DebugInstanceTag(ScStringView *) {
  return false;
}

bool GeBuildingAttrib::DebugClassTag(ScStringView *) {
  return false;
}

bool GeBuildingAttrib::DebugTypeTag(ScStringView *) {
  return false;
}

static const RsProperty GeClassProperty{0x65, 3, 1}, GeSizeProperty{0x66, 3, 3}, GePrimaryProperty{0x67, 8, 1},
    GePeopleProperty{0x6b, 8, 1}, GeVehicleProperty{0x6c, 8, 1}, GeNameProperty{0x6d, 8, 1},
    GeFlammabilityProperty{0x6e, 1, 1}, GeSoundAProperty{0x6f, 8, 1}, GeSoundBProperty{0x70, 8, 1},
    GeDialogProperty{0x71, 3, 1}, GeQueryFlagsProperty{0x72, 3, 2}, GePlopProperty{0x7b, 8, 1},
    GeQueryProperty{0x7c, 8, 1};
static const RsProperty GeVariantProperties[7] = {
    {0x74, 8, 1}, {0x75, 8, 1}, {0x76, 8, 1}, {0x77, 8, 1}, {0x78, 8, 1}, {0x79, 8, 1}, {0x7a, 8, 1}};

// Read the original required class and X/height/Z tuple before optional appearance, sound and query properties.
// Preserve partial writes, live factory validation, resource lookup ordering and the original asymmetric defaults.
bool GeBuildingAttrib::LoadProperties(RsPropertySet *properties) {
  u8 classRead = properties->GetWord(GeClassProperty, &occupantClass, 0);
  bool failed = !classRead;
  CtAgentTree *tree = GeGetAgentTypeTree();
  if (tree) {
    agentType = tree->GetNode(occupantClass);
    if (!classRead)
      failed = true;
  } else
    failed = true;
  if (!properties->GetWord(GeSizeProperty, &sizeX, 0))
    failed = true;
  if (!properties->GetWord(GeSizeProperty, &sizeY, 1))
    failed = true;
  if (!properties->GetWord(GeSizeProperty, &sizeZ, 2))
    failed = true;
  properties->GetKey(GePrimaryProperty, &primary.key, 0);
  if (!properties->GetKey(GePeopleProperty, &peopleKey, 0))
    peopleKey = {};
  properties->GetKey(GeVehicleProperty, &vehicleKey, 0);
  if (!properties->GetByte(GeFlammabilityProperty, &flammability, 0))
    flammability = 0;
  if (!properties->GetKey(GeSoundAProperty, &soundA, 0))
    soundA.instance = 0;
  if (!properties->GetKey(GeSoundBProperty, &soundB, 0))
    soundB.instance = 0;
  void *dialog;
  if (properties->GetWord(GeDialogProperty, &queryDialogClass, 0) &&
      GeGetCom()->GetClassObject(queryDialogClass, IID_UiQueryWindow, &dialog))
    static_cast<ScUnknown *>(dialog)->Release();
  else
    queryDialogClass = 1402;
  for (u32 i = 0; i < 7; ++i)
    if (!properties->GetKey(GeVariantProperties[i], &variants[i].key, 0))
      variants[i].resource = nullptr;
  if (!properties->GetKey(GePlopProperty, &plopKey, 0))
    plopKey.instance = 0;
  if (!properties->GetKey(GeNameProperty, &nameKey, 0)) {
    nameKey.instance = kGeMissingBuildingTextInstance;
    nameKey.group = kRsBuildingTextGroup;
    queryKey.type = kRsTextResourceType;
  }
  {
    GeTextResource text(nameKey.instance, nameKey.group);
    ScString raw = text;
    ScString copied(raw);
    name.Copy(copied);
  }
  if (!properties->GetKey(GeQueryProperty, &queryKey, 0)) {
    queryKey.instance = kGeMissingBuildingTextInstance;
    queryKey.group = kRsBuildingCatalogTextGroup;
    queryKey.type = kRsTextResourceType;
  }
  if (!properties->GetWord(GeQueryFlagsProperty, &queryFlags, 0))
    queryFlags = 0x5bee0;
  return !failed;
}

// Save the required tuple and optional records in the original callback order. Only required property failures affect
// the Boolean result; conditional records are tested live after all preceding property callbacks.
bool GeBuildingAttrib::SaveProperties(RsPropertySet *properties) {
  bool failed = !properties->SetWord(GeClassProperty, occupantClass, 0);
  if (!properties->SetWord(GeSizeProperty, sizeX, 0))
    failed = true;
  if (!properties->SetWord(GeSizeProperty, sizeY, 1))
    failed = true;
  if (!properties->SetWord(GeSizeProperty, sizeZ, 2))
    failed = true;
  properties->SetKey(GePrimaryProperty, primary.key, 0);
  if (peopleKey.type || peopleKey.group || peopleKey.instance)
    properties->SetKey(GePeopleProperty, peopleKey, 0);
  properties->SetKey(GeVehicleProperty, vehicleKey, 0);
  properties->SetKey(GeSoundAProperty, soundA, 0);
  properties->SetKey(GeSoundBProperty, soundB, 0);
  if (queryDialogClass != 1402)
    properties->SetWord(GeDialogProperty, queryDialogClass, 0);
  if (flammability)
    properties->SetByte(GeFlammabilityProperty, flammability, 0);
  for (u32 i = 0; i < 7; ++i)
    properties->SetKey(GeVariantProperties[i], variants[i].key, 0);
  properties->SetKey(GePlopProperty, plopKey, 0);
  if (nameKey.type || nameKey.group || nameKey.instance)
    properties->SetKey(GeNameProperty, nameKey, 0);
  if (queryKey.type || queryKey.group || queryKey.instance)
    properties->SetKey(GeQueryProperty, queryKey, 0);
  if (queryFlags)
    properties->SetWord(GeQueryFlagsProperty, queryFlags, 0);
  return !failed;
}

// Resolve the people key through a fresh application query, releasing that temporary interface before resource access.
u8 GeBuildingAttrib::ResolvePeopleProducer(const RsKey *value) {
  void *published = nullptr;
  if (value->instance && GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &published)) {
    auto *application = static_cast<ApGameInterface *>(published);
    RsManager *manager = application->GetResourceManager();
    application->Release();
    if (!manager->GetResource(*value, IID_NwPeopleProducerAttribInterface, &peopleAttrib))
      peopleAttrib = nullptr;
  } else
    peopleAttrib = nullptr;
  return 1;
}

// Resolve the vehicle key through the original separate key pointer and retain direct publication into its cache.
u8 GeBuildingAttrib::ResolveVehicleProducer(const RsKey *value) {
  void *published = nullptr;
  if (value->instance && GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &published)) {
    auto *application = static_cast<ApGameInterface *>(published);
    RsManager *manager = application->GetResourceManager();
    application->Release();
    if (!manager->GetResource(*value, IID_NwVehicleProducerAttrib, &vehicleAttrib))
      vehicleAttrib = nullptr;
  } else
    vehicleAttrib = nullptr;
  return 1;
}

// Borrow the cached people-producer interface or ask the original resolver to populate its actual retained member.
// Resource acquisition stays independent from the sprite cache and the module's cached application accessor.
iptr GeBuildingAttrib::GetPeopleProducerAttrib() {
  if (peopleAttrib || ResolvePeopleProducer(&peopleKey))
    return reinterpret_cast<iptr>(peopleAttrib);
  return 0;
}

// Borrow the cached vehicle-producer interface, resolving a miss through its separate key and native private receiver.
// The returned pointer-sized SDK value remains borrowed from the descriptor's retained resource field.
iptr GeBuildingAttrib::GetVehicleProducerAttrib() {
  if (vehicleAttrib || ResolveVehicleProducer(&vehicleKey))
    return reinterpret_cast<iptr>(vehicleAttrib);
  return 0;
}

// Construct the actual generic descriptor and return its generic attribute identity to the director callback.
// The original factory does not acquire a reference; successful interface discovery supplies the first hold.
ScUnknown *GeCreateBuildingAttrib() {
  return static_cast<GeAttrib *>(new GeBuildingAttrib);
}

GeFloraAttrib::GeFloraAttrib() {
}

GeFloraAttrib::~GeFloraAttrib() {
}

bool GeFloraAttrib::LoadProperties(RsPropertySet *properties) {
  return GeBuildingAttrib::LoadProperties(properties);
}

bool GeFloraAttrib::SaveProperties(RsPropertySet *properties) {
  return GeBuildingAttrib::SaveProperties(properties);
}

u8 GeFloraAttrib::MajorType() const {
  return 5;
}

// Construct the flora resource descriptor and return its generic attribute prefix without acquiring a reference.
ScUnknown *GeCreateFloraAttrib() {
  return static_cast<GeAttrib *>(new GeFloraAttrib);
}

GeDefaultEntry *GeDefaultEntries;
GeDefaultCategory *GeDefaultCategories;
GeRandom GeDefaultRandom(kScRandomSeedFromClock);
GeBuildingDefaults *GeDefaultBuildings;

struct GeDefaultStorage {
  // Retire the original module containers at process teardown. Generic container destruction frees pointer storage
  // without invoking game Release operations, which belong to the defaults receiver's separate Clear method.
  ~GeDefaultStorage() {
    while (GeDefaultEntries) {
      GeDefaultEntry *next = GeDefaultEntries->next;
      delete GeDefaultEntries;
      GeDefaultEntries = next;
    }
    while (GeDefaultCategories) {
      GeDefaultCategory *next = GeDefaultCategories->next;
      delete GeDefaultCategories;
      GeDefaultCategories = next;
    }
  }
};

static GeDefaultStorage GeDefaultStorageLifetime;

// Compare the five actual key fields in the original type/width/state/power/occupancy order. Spare bytes have no role.
static bool GeDefaultLess(const GeDefaultKey &first, const GeDefaultKey &second) {
  if (first.type != second.type)
    return first.type < second.type;
  if (first.width != second.width)
    return first.width < second.width;
  if (first.state != second.state)
    return first.state < second.state;
  if (first.powered != second.powered)
    return !first.powered;
  return !first.flagged && second.flagged;
}

// Find an original key or publish an empty value vector for an actual map-subscript operation. Node stability preserves
// source selections while callbacks cause another key to be inserted.
static GeDefaultEntry *GeFindDefault(const GeDefaultKey &key, bool insert) {
  GeDefaultEntry **slot = &GeDefaultEntries;
  while (*slot && GeDefaultLess((*slot)->key, key))
    slot = &(*slot)->next;
  if (*slot && !GeDefaultLess(key, (*slot)->key))
    return *slot;
  if (!insert)
    return nullptr;
  GeDefaultEntry *entry = new GeDefaultEntry;
  entry->key = key;
  entry->next = *slot;
  *slot = entry;
  return entry;
}

// Select one specialized attribute using the original global generator, then borrow its generic view. Both otherwise
// unused key lookups remain observable virtual calls, and a stored null selection returns null without conversion.
static GeAttrib *GeSelectDefault(GeDefaultEntry *entry) {
  GeBuildingType *value = entry->values.data[GeDefaultRandom.Next(entry->values.size)];
  if (!value)
    return nullptr;
  GeAttrib *attributes = value->AsOccupantAttrib();
  attributes->Key();
  attributes->Key();
  return attributes;
}

// Copy the source pointer vector before acquiring each newly cached reference. The copy does not release any prior
// destination contents, matching the original vector assignment followed by a separate AddRef traversal.
static void GeCacheDefault(const GeDefaultKey &key, GeDefaultEntry *source) {
  GeDefaultEntry *destination = GeFindDefault(key, true);
  if (destination != source) {
    destination->values.Clear();
    for (u32 i = 0; i < source->values.size; ++i)
      *destination->values.Append() = source->values.data[i];
  }
  GeBuildingType **values = destination->values.data;
  u32 count = destination->values.size;
  for (u32 i = 0; i < count; ++i)
    values[i]->AddRef();
}

GeBuildingDefaults::GeBuildingDefaults() : initialized(0), cache(nullptr), tree(nullptr) {
}

GeBuildingDefaults::~GeBuildingDefaults() {
  Clear();
}

// Release every specialized pointer in key/vector order before erasing the shared default map.
void GeBuildingDefaults::Clear() {
  if (!initialized)
    return;
  for (GeDefaultEntry *entry = GeDefaultEntries; entry; entry = entry->next) {
    GeBuildingType **values = entry->values.data;
    u32 count = entry->values.size;
    for (u32 i = 0; i < count; ++i) {
      values[i]->Release();
      values[i] = nullptr;
    }
  }
  while (GeDefaultEntries) {
    GeDefaultEntry *next = GeDefaultEntries->next;
    delete GeDefaultEntries;
    GeDefaultEntries = next;
  }
  tree->Release();
  tree = nullptr;
  cache->Release();
  cache = nullptr;
  initialized = 0;
}

// Select a default by occupancy, footprint width, agent type and the two original flags. A direct hit only borrows its
// selected attribute; nearest-category, parent and root fallbacks additionally cache their whole retained pointer vector.
GeAttrib *GeBuildingDefaults::GetAttrib(BdBuilding *building) {
  void *occupant = nullptr;
  if (!building->QueryInterface(IID_ScOccupant, &occupant))
    return nullptr;
  GeDefaultKey key;
  key.state = building->GetOccupancyState();
  GeCellRect rectangle;
  static_cast<ScOccupant *>(occupant)->GetBoundingRectInCellCoords(&rectangle);
  static_cast<ScOccupant *>(occupant)->Release();
  occupant = nullptr;
  key.width = u8(rectangle.right - rectangle.left + 1);
  u32 type = building->AgentType()->Id();
  key.flagged = building->GetOccupancyStateFlag(kGeBuildingCoveredInGarbage);
  GeAttrib *base = building->GetBaseAttribData();
  void *specific = nullptr;
  if (base && base->QueryInterface(IID_GeBuildingType, &specific)) {
    key.powered = (static_cast<GeBuildingType *>(specific)->Flags() & 0x100) != 0;
    static_cast<GeBuildingType *>(specific)->Release();
  } else
    key.powered = false;
  key.type = type;
  GeDefaultEntry *entry = GeFindDefault(key, false);
  if (entry)
    return GeSelectDefault(entry);
  u32 nearest = 0, best = UINT32_MAX;
  for (GeDefaultCategory *node = GeDefaultCategories; node; node = node->next) {
    u32 distance, category = node->type;
    if (tree->IsAncestor(category, type, &distance) && distance < best) {
      nearest = category;
      best = distance;
    }
  }
  if (!nearest)
    return nullptr;
  key.type = nearest;
  entry = GeFindDefault(key, false);
  if (!entry) {
    for (;;) {
      CtAgentType *node = tree->GetNode(key.type);
      if (!node)
        break;
      CtAgentType *parent = node->Parent();
      if (!parent)
        break;
      key.type = parent->Id();
      if (key.type == 100)
        break;
      entry = GeFindDefault(key, false);
      if (entry)
        break;
    }
    if (!entry) {
      key.type = 100;
      entry = GeFindDefault(key, false);
    }
  }
  if (!entry)
    return nullptr;
  GeAttrib *attributes = GeSelectDefault(entry);
  key.type = type;
  GeCacheDefault(key, entry);
  return attributes;
}

// Resolve through this receiver's direct attribute selector, then borrow the unindexed sprite definition. A missing
// selection returns null and does not consult the source building's original sprite attributes.
SpDefinition *GeBuildingDefaults::GetSprite(BdBuilding *building) {
  GeAttrib *attributes = GetAttrib(building);
  return attributes ? attributes->GetSpriteAttrib() : nullptr;
}

// Take the first delimiter prefix, leaving the suffix in the working String.
static ScString GeTakeDefaultToken(ScString *text, char delimiter) {
  u32 length = text->Strlen(), index = 0;
  while (index < length && text->begin[index] != delimiter)
    ++index;
  if (index == length)
    return ScString();
  ScString token(text->begin, index);
  text->Erase(0, index + 1);
  return token;
}

// Parse a default-data row through the original void callback with const String inputs and unused generic context.
// Retain successful resource-query acquisitions directly in the keyed vector and register a category on the first success.
void GeParseDefaultRow(const ScString &name, const ScString &value, void *) {
  ScString work(name), token = GeTakeDefaultToken(&work, ':');
  GeDefaultKey key;
  key.state = GeParseNumber(token.ToChar());
  token = GeTakeDefaultToken(&work, ':');
  key.width = static_cast<u8>(GeParseNumber(token.ToChar()));
  token = GeTakeDefaultToken(&work, ':');
  key.powered = false;
  key.flagged = false;
  if (!token.Strlen())
    key.type = GeParseNumber(work.ToChar());
  else {
    key.type = GeParseNumber(token.ToChar());
    char flag = work.begin[0];
    key.powered = flag == 'f' || flag == 'F' || flag == 'g' || flag == 'G';
    key.flagged = flag == 'g' || flag == 'G';
  }
  work = value;
  bool category = true, more;
  do {
    more = false;
    token = GeTakeDefaultToken(&work, ':');
    RsKey resource;
    resource.type = GeParseNumber(token.ToChar());
    token = GeTakeDefaultToken(&work, ':');
    resource.group = GeParseNumber(token.ToChar());
    u32 length = work.Strlen(), index = 0;
    while (index < length && work.begin[index] != '+')
      ++index;
    if (index < length) {
      token = GeTakeDefaultToken(&work, '+');
      more = true;
    } else {
      index = 0;
      while (index < length && work.begin[index] != ';')
        ++index;
      if (index < length)
        token = GeTakeDefaultToken(&work, ';');
    }
    resource.instance = GeParseNumber(token.ToChar());
    if (!resource.instance)
      break;
    void *object;
    if (GeGetResourceManager()->GetResource(resource, IID_GeBuildingType, &object)) {
      *GeFindDefault(key, true)->values.Append() = static_cast<GeBuildingType *>(object);
      if (category) {
        GeDefaultCategory **slot = &GeDefaultCategories;
        while (*slot && (*slot)->type < key.type)
          slot = &(*slot)->next;
        if (!*slot || (*slot)->type != key.type) {
          GeDefaultCategory *entry = new GeDefaultCategory{key.type, *slot};
          *slot = entry;
        }
        category = false;
      }
    }
  } while (more);
}

// Retain the definition's tree and cache, then populate shared defaults from the installed package or loose INI.
// Enumeration failure does not reject initialization; reloading first clears the old retained defaults and services.
bool GeBuildingDefaults::Load(CtDefinition *definition) {
  if (initialized)
    Clear();
  tree = ScAddRefNotNull(definition->GetAgentTypeTree());
  cache = ScAddRefNotNull(definition->GetOccupantAttribCache());
  ScString file(GeGetFileSystem()->DataDirectory());
  ScString package(file);
  file.Append("sys\\SC3BuildingLayer.INI", 24);
  package.Append("sys\\SYS.PAK", 11);
  GeIni reader;
  reader.SetPath(file);
  reader.SetCompressedPath(package);
  {
    ScString section("DefaultBuildingData");
    reader.EnumEntries(section, GeParseDefaultRow, nullptr);
  }
  initialized = 1;
  return true;
}

GeBuildingFactory::GeBuildingFactory() {
}

GeBuildingFactory::~GeBuildingFactory() {
}

// Select the actual city command by class identifier, retaining the original independent rotation state and key order.
// Defined unknown classes clear the output; recognized objects publish through their genuine command identities.
bool GeBuildingFactory::CreateCommand(u32 classId, u32 iid, void **out) {
  static GeRandom random(kScRandomSeedFromClock);
  GeCommand *command;
  switch (classId) {
  case kGeCollegeCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 214), false, false);
    break;
  case kGeLibraryCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 245), false, false);
    break;
  case kGePlaygroundCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 9035), false, false);
    break;
  case kGeHospitalCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 209), false, false);
    break;
  case kGeSchoolCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8084), false, false);
    break;
  case kGePoliceStationCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 210), false, false);
    break;
  case kGeMuseumCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 212), false, false);
    break;
  case kGeBaseballFieldCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 215), false, false);
    break;
  case kGeBigParkCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 213), false, false);
    break;
  case kGeTrainStationCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 237), false, false);
    break;
  case kGeZooCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 218), false, false);
    break;
  case kGePondCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 9036), false, false);
    break;
  case kGeFountainCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 9038), false, false);
    break;
  case kGeFireCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 211), false, false);
    break;
  case kGeBusStopCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8086), false, false);
    break;
  case kGeSmallParkCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 13), false, false);
    break;
  case kGeJailCommandClassId:
    command = new GeBuildingTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8001), false, false);
    break;
  case kGeShoreToolClassId:
    command = new GeShoreTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 248), false, false);
    break;
  case kGeSubwayStationCommandClassId:
    command = new GeSubwayTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 233), false, false);
    break;
  case kGeRailToSubwayCommandClassId:
    command = new GeSubwayTool(RsKey(kRsBuildingDataResourceType, kRsBuildingDataGroup, 16010), false, false);
    break;
  case kGeThemeParkCommandClassId: {
    u32 rotation = random.Draw() & 3;
    RsKey first(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + rotation),
        second(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + ((rotation + 1) & 3)),
        third(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + ((rotation + 2) & 3)),
        fourth(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8029 + ((rotation + 3) & 3));
    command = new GeMultiTool(first, second, third, fourth, false, false);
    break;
  }
  case kGeMilitaryBaseCommandClassId: {
    RsKey first(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8078),
        second(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8077),
        third(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8075),
        fourth(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8076);
    command = new GeMultiTool(first, second, third, fourth, false, false);
    break;
  }
  case kGeUniversityCommandClassId: {
    u32 rotation = random.Draw() & 3;
    RsKey first(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + rotation),
        second(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + ((rotation + 1) & 3)),
        third(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + ((rotation + 2) & 3)),
        fourth(kRsBuildingDataResourceType, kRsBuildingDataGroup, 8088 + ((rotation + 3) & 3));
    command = new GeMultiTool(first, second, third, fourth, false, false);
    break;
  }
  case kGeLocationToolClassId:
    command = new GeLocationTool;
    break;
  case kGeDemolitionToolClassId:
    command = new GeDemolitionTool;
    break;
  default:
    *out = nullptr;
    return false;
  }
  if (command->QueryInterface(iid, out))
    return true;
  // Stock frees the interior command interface on rejection, outside the allocator's valid input domain.
  assert(false && "Geometry command interface query must succeed");
  std::unreachable();
}

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

GeBuildingGroup::GeBuildingGroup() : landUse(0), buildingCount(0) {
}

// Release entries using the live stored count and storage after each callback, clearing each pointer after Release.
// Vector destruction frees only its storage; it does not release entries beyond the original count.
GeBuildingGroup::~GeBuildingGroup() {
  for (u32 i = 0; i < buildingCount; ++i) {
    buildings.data[i].attributes->Release();
    buildings.data[i].attributes = nullptr;
  }
}

bool GeBuildingGroup::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GeBuildingGroupView, GeBuildingGroupView);
  return RsResource::QueryInterface(iid, out);
}

u32 GeBuildingGroup::AddRef() {
  return RsResource::AddRef();
}

u32 GeBuildingGroup::Release() {
  return RsResource::Release();
}

u32 GeBuildingGroup::GetLandUse() {
  return landUse;
}

u32 GeBuildingGroup::GetBuildingCount() {
  return buildingCount;
}

// Copy the stored pointer/weight pairs without acquiring references. The source rereads the count on each iteration.
u8 GeBuildingGroup::GetBuildings(GeWeightedBuilding *out) {
  for (u32 i = 0; i < buildingCount; ++i)
    out[i] = buildings.data[i];
  return 1;
}

// Require a header read result of exactly one, then dispatch the two original file markers.
// Unknown markers retain that successful byte rather than rejecting the resource.
u8 GeBuildingGroup::Load(RsStream *stream) {
  u32 marker = 0;
  u8 result = stream->ReadU32(&marker);
  if (result != 1)
    return 0;
  if (marker == kRsTextAttributeTag)
    return LoadText(stream);
  if (marker == kRsBinaryAttributeTag)
    return LoadBinary(stream);
  return result;
}

// Read the declared count separately from retained entries. Rejected/zero-weight resources reduce that live count;
// successful acquisitions append their original reference without another AddRef, even after a failed weight read.
u8 GeBuildingGroup::LoadBinary(RsStream *stream) {
  RsKey resourceKey;
  RsManager *manager = GeGetResourceManager();
  stream->ReadU32(&landUse);
  u8 result = stream->ReadU32(&buildingCount);
  // Initialize only the originally unwritten first-use locals; later failed publications retain their prior values.
  u32 accepted = 0, weight = 0;
  void *published = nullptr;
  while (accepted < buildingCount && result) {
    stream->ReadU32(&resourceKey.type);
    stream->ReadU32(&resourceKey.group);
    stream->ReadU32(&resourceKey.instance);
    result = stream->ReadU32(&weight);
    if (weight && manager->GetResource(resourceKey, IID_GeBuildingType, &published)) {
      ++accepted;
      *buildings.Append() = {static_cast<GeBuildingType *>(published), weight};
    } else
      --buildingCount;
  }
  return result;
}

// Clear the line, skip every LF and finish only at CR, appending the original explicit terminating zero.
// EOF preserves accumulated text and returns zero without supplying a successful line.
static u8 GeReadGroupLine(RsStream *stream, ScString *line) {
  line->Erase(0, kScStringToEnd);
  char value = 0;
  for (;;) {
    if (!stream->ReadBlock(&value, 1))
      return 0;
    if (value == '\n')
      continue;
    if (value == '\r') {
      value = 0;
      line->Append(&value, 1);
      return 1;
    }
    line->Append(&value, 1);
  }
}

// Consume the original header/land-use/count lines, then acquire weighted building descriptors in file order.
// Missing resources reduce the target count; malformed entry lines replace it with the accepted count and fail.
u8 GeBuildingGroup::LoadText(RsStream *stream) {
  ScString line;
  RsKey resourceKey;
  RsManager *manager = GeGetResourceManager();
  i32 number;
  if (!GeReadGroupLine(stream, &line) || !GeReadGroupLine(stream, &line) ||
      sscanf(line.Data(), "LandUse: %d", &number) != 1)
    return 0;
  landUse = u32(number);
  if (!GeReadGroupLine(stream, &line) || sscanf(line.Data(), "BuildingCount: %d", &number) != 1)
    return 0;
  buildingCount = u32(number);
  u32 accepted = 0;
  void *published = nullptr;
  while (accepted < buildingCount) {
    i32 group, type, instance, weight;
    if (!GeReadGroupLine(stream, &line) ||
        sscanf(line.Data(), "Key: %d,%d,%d,%d", &group, &type, &instance, &weight) != 4) {
      buildingCount = accepted;
      return 0;
    }
    resourceKey = RsKey(u32(type), u32(group), u32(instance));
    if (weight && manager->GetResource(resourceKey, IID_GeBuildingType, &published)) {
      ++accepted;
      *buildings.Append() = {static_cast<GeBuildingType *>(published), u32(weight)};
    } else
      --buildingCount;
  }
  return 1;
}

// Keep the original recursive resource region active through construction, reading and interface publication.
class GeGroupResourceScope {
public:
  // Enter the common native resource region used by the reconstructed resource base.
  GeGroupResourceScope() {
    RsEnterResourceRegion();
  }

  // Leave the region on both normal and exceptional method exits.
  ~GeGroupResourceScope() {
    RsLeaveResourceRegion();
  }
};

GeBuildingGroupFactory::GeBuildingGroupFactory() {
}

u32 GeBuildingGroupFactory::AddRef() {
  GeGetDirector()->AddRef();
  return ++references;
}

// Release the SIMGEOM hold after capturing the final-reference branch. Only a surviving object decrements its field.
u32 GeBuildingGroupFactory::Release() {
  bool one = references == 1;
  GeGetDirector()->RemoveRef();
  if (one) {
    delete this;
    return 0;
  }
  return --references;
}

// Construct the record's resource type under the recursive region, copy its freshly queried key and read it.
// Retain the requested-interface result before releasing the temporary resource; failed construction preserves output.
bool GeBuildingGroupFactory::Load(RsRecord *record, u32 iid, void **out) {
  GeGroupResourceScope scope;
  void *published;
  bool result = false;
  if (New(record->GetKey()->type, IID_RsResource, &published)) {
    auto *resource = static_cast<RsResource *>(published);
    RsKey *source = record->GetKey(), *destination = resource->GetKey();
    *destination = *source;
    if (ReadRecord(resource, record))
      result = resource->QueryInterface(iid, out);
    resource->Release();
  }
  return result;
}

// Construct only the original group type and return its requested interface directly. Refused queries deliberately
// do not delete the unreferenced allocation; unrelated types leave output untouched.
bool GeBuildingGroupFactory::New(u32 type, u32 iid, void **out) {
  if (type != kRsBuildingGroupResourceType)
    return false;
  return (new GeBuildingGroup)->QueryInterface(iid, out);
}

// Acquire the record's input stream under the same recursive region, then capture Read before its Release callback.
bool GeBuildingGroupFactory::ReadRecord(RsResource *resource, RsRecord *record) {
  GeGroupResourceScope scope;
  void *published;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, &published)) {
    auto *stream = static_cast<RsStream *>(published);
    result = SerialRead(resource, stream);
    stream->Release();
  }
  return result;
}

// Require the live resource type before reading the complete group's binary/text format through its real receiver.
bool GeBuildingGroupFactory::SerialRead(RsResource *resource, RsStream *stream) {
  if (resource->GetKey()->type != kRsBuildingGroupResourceType)
    return 0;
  return static_cast<GeBuildingGroup *>(resource)->Load(stream);
}

// Query the writable record stream under the original recursive region, saving its Boolean result before release.
// The const public resource enters the existing mutable private writer without changing its type/key dispatch.
bool GeBuildingGroupFactory::WriteRecord(const RsResource *resource, RsRecord *record) {
  GeGroupResourceScope scope;
  void *published;
  bool result = false;
  if (record->QueryInterface(IID_RsStream, &published)) {
    auto *stream = static_cast<RsStream *>(published);
    result = SerialWrite(const_cast<RsResource *>(resource), stream);
    stream->Release();
  }
  return result;
}

// Inspect the live resource type as the original short circuit does. This read-only factory refuses all writes.
bool GeBuildingGroupFactory::SerialWrite(RsResource *resource, RsStream *) {
  if (resource->GetKey()->type != kRsBuildingGroupResourceType)
    return 0;
  return 0;
}

// Construct the actual two-interface factory and return its primary identity without acquiring a reference.
ScUnknown *GeCreateBuildingGroupFactory() {
  return static_cast<RsFactoryInterface *>(new GeBuildingGroupFactory);
}

// Restore the base occupant before requiring a stored building state, applying that state through the real flag setter.
// Optional name and birthdate reads do not reject an otherwise initialized building; failed optional reads are ignored.
bool GeBuilding::Init(CtCity *city, OcPersistInfo *info) {
  if (!info || !GeOccupant::Init(city, info) || !info->HasLayerData1())
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
      GeDate date(day, year);
      SetBirthdate(date);
    }
  }
  return true;
}

// Save the actual occupant state before optional building-specific data. Only a changeable name differing from the live
// base attribute name is stored; birthdate getter failures are ignored and the complete occupancy word is written last.
bool GeBuilding::Save(CtCity *city, OcPersistInfo *info) {
  if (!GeOccupant::Save(city, info))
    return false;
  if (IsNameChangeable()) {
    ScString name;
    if (GetName(&name)) {
      ScString original;
      if (attributes && attributes->GetName(&original) && !name.IsEqual(&original))
        info->SetNameField(&name);
    }
  }
  if (HasBirthdate()) {
    GeDate date;
    if (GetBirthdate(&date)) {
      u32 day = date.GeDate::DayOfYear(), year = date.GeDate::Year();
      info->SetSimCreateDate(year, day);
    }
  }
  info->SetLayerData1(occupancyFlags);
  return true;
}

GeRecord::GeRecord(u8 owned)
    : position(0), flags(0), key{}, type(0), birthYear(0), birthDay(0), state(0), field3C(0), tokensBegin(nullptr),
      tokensEnd(nullptr), tokensStorageEnd(nullptr), references(0), callerOwned(owned) {
  Clear();
}

// Release retained interface entries before retiring string properties and the concrete name owner.
GeRecord::~GeRecord() {
  u32 count = tokensBegin ? u32(tokensEnd - tokensBegin) : 0;
  GeToken *entries = tokensBegin;
  for (u32 i = 0; i < count; ++i) {
    if (entries[i].value) {
      entries[i].value->Release();
      entries[i].value = nullptr;
    }
  }
  delete[] tokensBegin;
  for (u32 i = 0; i < texts.size; ++i)
    delete texts.data[i].value;
}

// Publish the shared descriptor identity before virtual retention. Failed queries clear the output.
bool GeRecord::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcPersistInfo);
  HANDLE_QUERY_INTERFACE(IID_OcPersistInfo, OcPersistInfo);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the descriptor count before acquiring a module hold, then return the live count after that callback.
u32 GeRecord::AddRef() {
  ++references;
  GeGetDirector()->AddRef();
  return references;
}

// Remove a module hold even at zero, leaving the final stored count intact.
// Final release deletes only records whose live caller-owned byte is zero.
u32 GeRecord::Release() {
  if (references > 1) {
    --references;
    GeGetDirector()->RemoveRef();
    return references;
  }
  GeGetDirector()->RemoveRef();
  if (!callerOwned)
    delete this;
  return 0;
}

u8 GeRecord::GetAttribKey(RsKey *out) const {
  *out = key;
  return 1;
}

u8 GeRecord::GetTypeId(u32 *out) const {
  *out = type;
  return 1;
}

// Unpack two eleven-bit cell coordinates and the eight-bit altitude from the position word.
u8 GeRecord::GetLocationInCells(u32 *x, u32 *z, u32 *altitude) const {
  *x = position & kOcPositionCellValueMask;
  *z = (position >> kOcPositionZShift) & kOcPositionCellValueMask;
  *altitude = (position >> kOcPositionAltitudeShift) & 255;
  return 1;
}

// Return the two six-bit footprint dimensions stored in the property flag word.
// Both genuine outputs are always written before the byte-one result.
u8 GeRecord::GetSizeInCells(u32 *first, u32 *second) const {
  *first = flags & kGeRecordDimensionMask;
  *second = (flags >> kGeRecordSecondDimensionShift) & kGeRecordDimensionMask;
  return 1;
}

u8 GeRecord::GetRotationFlag(u32 *out) const {
  *out = position >> kOcPositionRotationShift;
  return 1;
}

u8 GeRecord::GetCanSimRemoveFlag() const {
  return (flags & kGeRecordCanSimRemove) != 0;
}

u8 GeRecord::GetCanUserRemoveFlag() const {
  return (flags & kGeRecordCanUserRemove) != 0;
}

void GeRecord::SetAttribKey(const RsKey &value) {
  key = value;
}

void GeRecord::SetTypeId(u32 value) {
  type = value;
}

void GeRecord::SetLocationInCells(u32 x, u32 z, u32 altitude) {
  position = (position & kOcPositionRotationMask) | (x & kOcPositionCellValueMask) |
      ((z & kOcPositionCellValueMask) << kOcPositionZShift) | ((altitude & 255) << kOcPositionAltitudeShift);
}

void GeRecord::SetSizeInCells(u32 first, u32 second) {
  flags = (flags & kGeRecordWithoutFootprint) | (first & kGeRecordDimensionMask) |
      ((second & kGeRecordDimensionMask) << kGeRecordSecondDimensionShift);
}

void GeRecord::SetRotationFlag(u32 rotation) {
  position = (position & kOcPositionWithoutRotation) | (rotation << kOcPositionRotationShift);
}

void GeRecord::SetRemoveFlags(u8 first, u8 second) {
  flags = (flags & kGeRecordWithoutRemoveFlags) | (u32(first & 1) << kGeRecordSimRemoveShift) |
      (u32(second & 1) << kGeRecordUserRemoveShift);
}

u8 GeRecord::HasNameField() const {
  return (flags & kGeRecordHasName) != 0;
}

// Copy a present name through the caller's genuine string interface; ignore its copy result.
u8 GeRecord::GetNameField(ScStringView *out) const {
  if (!HasNameField())
    return 0;
  out->Copy(name);
  return 1;
}

// Publish name presence before copying the input through an independent temporary string.
u8 GeRecord::SetNameField(ScStringView *value) {
  flags |= kGeRecordHasName;
  ScString copy(*value);
  name = copy;
  return 1;
}

u8 GeRecord::HasSimCreateDate() const {
  return (flags & kGeRecordHasCreateDate) != 0;
}

// Write year and ordinal day only when a birthdate is present. Missing dates preserve both outputs.
u8 GeRecord::GetSimCreateDate(i32 *year, i32 *day) const {
  if (!(flags & kGeRecordHasCreateDate))
    return 0;
  *year = birthYear;
  *day = birthDay;
  return 1;
}

// Publish birthdate presence before storing the supplied year and ordinal day in order.
u8 GeRecord::SetSimCreateDate(i32 year, i32 day) {
  flags |= kGeRecordHasCreateDate;
  birthYear = year;
  birthDay = day;
  return 1;
}

u8 GeRecord::HasLayerData1() const {
  return (flags & kGeRecordHasLayerData1) != 0;
}

// Dispatch the live state-presence virtual before writing the full saved state word.
u8 GeRecord::GetLayerData1(u32 *out) const {
  if (!HasLayerData1())
    return 0;
  *out = state;
  return 1;
}

// Publish state presence and retain the complete supplied state word.
u8 GeRecord::SetLayerData1(u32 value) {
  flags |= kGeRecordHasLayerData1;
  state = value;
  return 1;
}

// Find an integer property by its full unsigned key. Missing keys leave the caller's output unchanged.
u8 GeRecord::GetIntData(u32 property, u32 *out) {
  for (u32 i = 0; i < words.size; ++i) {
    if (words.data[i].key == property) {
      *out = words.data[i].value;
      return 1;
    }
  }
  return 0;
}

// Insert a new integer property in unsigned key order; existing keys reject replacement without changing flags.
u8 GeRecord::SetIntData(u32 property, u32 value) {
  u32 index = 0;
  while (index < words.size && words.data[index].key < property)
    ++index;
  if (index < words.size && words.data[index].key == property)
    return 0;
  flags |= kGeRecordHasIntData;
  words.Append();
  for (u32 i = words.size - 1; i > index; --i)
    words.data[i] = words.data[i - 1];
  words.data[index] = {property, value};
  return 1;
}

// Reset packed state and property collections while retaining the stored resource key and name text. Release interface
// entries while their range is still published, then retire the captured range without resetting references.
u8 GeRecord::Clear() {
  type = position = birthYear = birthDay = state = field3C = 0;
  flags = kGeRecordVersionFlag;
  u32 count = tokensBegin ? u32(tokensEnd - tokensBegin) : 0;
  GeToken *entries = tokensBegin;
  for (u32 i = 0; i < count; ++i) {
    if (entries[i].value) {
      entries[i].value->Release();
      entries[i].value = nullptr;
    }
  }
  if (count)
    tokensEnd -= count;
  words.Clear();
  for (u32 i = 0; i < texts.size; ++i)
    delete texts.data[i].value;
  texts.Clear();
  floats.Clear();
  return 1;
}

u32 GeRecord::GetPersistRecordVersion() const {
  return 1;
}

u8 GeRecord::DidSameKeyWriteResource() {
  return (flags & kGeRecordVersionFlag) != 0;
}

u8 GeRecord::SetSameKeyWroteResource(u8 enabled) {
  flags = (flags & kGeRecordWithoutVersionFlag) | (u32(enabled & 1) << kGeRecordVersionShift);
  return 1;
}

u8 GeRecord::HasLayerData2() const {
  return (flags & kGeRecordHasLayerData2) != 0;
}

// Dispatch the live optional-state test before publishing its stored DWORD. Missing state preserves output.
u8 GeRecord::GetLayerData2(u32 *out) const {
  if (!HasLayerData2())
    return 0;
  *out = field3C;
  return 1;
}

// Publish the second state-word presence before storing its complete DWORD value.
u8 GeRecord::SetLayerData2(u32 value) {
  flags |= kGeRecordHasLayerData2;
  field3C = value;
  return 1;
}

u32 GeRecord::GetIntDataCount() const {
  return words.size;
}

u32 GeRecord::GetStringDataCount() const {
  return texts.size;
}

// Copy a matching string property through the caller's real string interface. Ignore its copy result.
u8 GeRecord::GetStringData(u32 property, ScStringView *out) {
  for (u32 i = 0; i < texts.size; ++i) {
    if (texts.data[i].key == property) {
      out->Copy(*texts.data[i].value);
      return 1;
    }
  }
  return 0;
}

// Reject existing keys before observing the input string. Publish presence before copying; unique insertion retains
// the first value even if an input callback inserts that same key while the independent copy is being constructed.
u8 GeRecord::SetStringData(u32 property, ScStringView *value) {
  u32 index = 0;
  while (index < texts.size && texts.data[index].key < property)
    ++index;
  if (index < texts.size && texts.data[index].key == property)
    return 0;
  flags |= kGeRecordHasStringData;
  ScString copy(*value);
  index = 0;
  while (index < texts.size && texts.data[index].key < property)
    ++index;
  if (index == texts.size || texts.data[index].key != property) {
    ScString *stored = new ScString(copy);
    texts.Append();
    for (u32 i = texts.size - 1; i > index; --i)
      texts.data[i] = texts.data[i - 1];
    texts.data[index] = {property, stored};
  }
  return 1;
}

u32 GeRecord::GetFloatDataCount() const {
  return floats.size;
}

// Publish the stored floating-point value only for a matching property key. Missing keys preserve output.
u8 GeRecord::GetFloatData(u32 property, f32 *out) {
  for (u32 i = 0; i < floats.size; ++i) {
    if (floats.data[i].key == property) {
      *out = floats.data[i].value;
      return 1;
    }
  }
  return 0;
}

// Insert a unique float property in unsigned key order. Duplicate refusal leaves value and presence flags unchanged.
u8 GeRecord::SetFloatData(u32 property, f32 value) {
  u32 index = 0;
  while (index < floats.size && floats.data[index].key < property)
    ++index;
  if (index < floats.size && floats.data[index].key == property)
    return 0;
  flags |= kGeRecordHasFloatData;
  floats.Append();
  for (u32 i = floats.size - 1; i > index; --i)
    floats.data[i] = floats.data[i - 1];
  floats.data[index] = {property, value};
  return 1;
}

u32 GeRecord::GetTokenCount() const {
  return tokensBegin ? u32(tokensEnd - tokensBegin) : 0;
}

// Search retained entries in insertion order, publishing null matching values before continuing. Retain and return
// the first nonnull matching identity; the caller's output is untouched when no entry has the requested key.
u8 GeRecord::GetToken(u32 property, ScVariant **out) {
  for (u32 i = 0; tokensBegin && i < u32(tokensEnd - tokensBegin); ++i) {
    if (tokensBegin[i].key == property) {
      ScVariant *value = tokensBegin[i].value;
      *out = value;
      if (value) {
        value->AddRef();
        return 1;
      }
    }
  }
  return 0;
}

// Growth retains copied entries before releasing the old published range, preserving observable reference callbacks.
void GeRecord::AppendToken(const GeToken &entry) {
  if (tokensEnd != tokensStorageEnd) {
    *tokensEnd = entry;
    if (tokensEnd->value)
      tokensEnd->value->AddRef();
    ++tokensEnd;
    return;
  }
  u32 count = tokensBegin ? u32(tokensEnd - tokensBegin) : 0;
  if (count > UINT32_MAX / (2 * sizeof(GeToken)))
    ScFatal(SC_ERROR_NOMEM, "Occupant record token allocation is too large.");
  u32 capacity = count ? count * 2 : 1, copied = 0;
  GeToken *storage = new GeToken[capacity];
  for (u32 i = 0; i < count; ++i) {
    storage[copied] = tokensBegin[i];
    if (storage[copied].value)
      storage[copied].value->AddRef();
    ++copied;
  }
  storage[copied] = entry;
  if (storage[copied].value)
    storage[copied].value->AddRef();
  ++copied;
  GeToken *first = tokensBegin, *last = tokensEnd;
  for (; first != last; ++first)
    if (first->value) {
      first->value->Release();
      first->value = nullptr;
    }
  delete[] tokensBegin;
  tokensBegin = storage;
  tokensEnd = storage + copied;
  tokensStorageEnd = storage + capacity;
}

// Keep the temporary hold through growth callbacks, then release it after the stored entry owns its reference.
u8 GeRecord::AddToken(u32 property, ScVariant *value) {
  GeToken entry;
  entry.key = property;
  flags |= kGeRecordHasTokens;
  entry.value = value;
  value->AddRef();
  AppendToken(entry);
  if (entry.value) {
    entry.value->Release();
    entry.value = nullptr;
  }
  return 1;
}

// Write the packed descriptor and collections in wire order, encoding displayed names/StringData at the save boundary.
// Flags and interface ranges stay live across stream callbacks; capture the integer-map count separately.
u8 GeRecord::SerialWrite(CtCity *, RsStream *stream) {
  u8 result = stream->WriteU32(position);
  if (!result)
    return result;
  result = stream->WriteU32(flags);
  if (!result)
    return result;
  result = stream->WriteU32(type);
  if (!result)
    return result;
  result = stream->WriteKey(&key);
  if (!result)
    return result;
  if (flags & kGeRecordHasName)
    result = CtWriteSavedText(stream, &name, kScLanguageCurrent);
  if (!result)
    return result;
  if (flags & kGeRecordHasCreateDate) {
    result = stream->WriteI32(birthYear);
    if (!result)
      return result;
    result = stream->WriteI32(birthDay);
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasLayerData1)
    result = stream->WriteU32(state);
  if (!result)
    return result;
  if (flags & kGeRecordHasLayerData2)
    result = stream->WriteU32(field3C);
  if (!result)
    return result;
  if (flags & kGeRecordHasIntData) {
    u32 count = words.size;
    result = stream->WriteU32(count);
    if (!result)
      return result;
    ScArray<u32> values;
    for (u32 i = 0; i < words.size; ++i) {
      *values.Append() = words.data[i].key;
      *values.Append() = words.data[i].value;
    }
    result = stream->WriteU32Array(values.data, count * 2);
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasStringData) {
    result = stream->WriteU32(texts.size);
    u32 index = 0;
    while (result && index < texts.size) {
      u32 property = texts.data[index].key;
      ScString *value = texts.data[index].value;
      result = stream->WriteU32(property);
      if (result)
        result = CtWriteSavedText(stream, value, kScLanguageCurrent);
      index = 0;
      while (index < texts.size && texts.data[index].key <= property)
        ++index;
    }
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasFloatData) {
    result = stream->WriteU32(floats.size);
    u32 index = 0;
    while (result && index < floats.size) {
      u32 property = floats.data[index].key;
      result = stream->WriteU32(property);
      if (result) {
        index = 0;
        while (floats.data[index].key != property)
          ++index;
        result = stream->WriteF32(floats.data[index].value);
      }
      index = 0;
      while (index < floats.size && floats.data[index].key <= property)
        ++index;
    }
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasTokens) {
    result = stream->WriteU32(tokensBegin ? u32(tokensEnd - tokensBegin) : 0);
    for (u32 i = 0; tokensBegin && i < u32(tokensEnd - tokensBegin); ++i) {
      if (!result)
        break;
      result = stream->WriteU32(tokensBegin[i].key);
      if (result)
        result = stream->WriteVariant(tokensBegin[i].value);
    }
  }
  return result;
}

// Decode an actual saved text field using its containing city's last-save language, preserving partial stream publication.
// A missing city context cannot establish that page and reports an unsupported read rather than guessing from a path.
static bool GeReadRecordText(CtCity *city, RsStream *stream, ScStringView *out) {
  i32 language, country;
  if (!city || !city->GetLastLanguageAndCountry(&language, &country))
    return false;
  return CtReadSavedText(stream, out, static_cast<u32>(language));
}

// Read codec one into the descriptor, decoding displayed names/StringData with the containing city's saved language.
// Preserve partial publications, first-wins duplicate keys, and the float loop's deliberate continuation after failure.
u8 GeRecord::SerialRead(CtCity *city, u32 codec, RsStream *stream) {
  if (codec != 1)
    return 0;
  u8 result = stream->ReadU32(&position);
  if (!result)
    return result;
  result = stream->ReadU32(&flags);
  if (!result)
    return result;
  result = stream->ReadU32(&type);
  if (!result)
    return result;
  result = stream->ReadKey(&key);
  if (!result)
    return result;
  if (flags & kGeRecordHasName)
    result = GeReadRecordText(city, stream, &name);
  if (!result)
    return result;
  if (flags & kGeRecordHasCreateDate) {
    result = stream->ReadI32(&birthYear);
    if (!result)
      return result;
    result = stream->ReadI32(&birthDay);
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasLayerData1)
    result = stream->ReadU32(&state);
  if (!result)
    return result;
  if (flags & kGeRecordHasLayerData2)
    result = stream->ReadU32(&field3C);
  if (!result)
    return result;
  if (flags & kGeRecordHasIntData) {
    words.Clear();
    u32 count = 0;
    result = stream->ReadU32(&count);
    u32 slots = count * 2;
    ScArray<u32> values;
    values.Reserve(slots);
    for (u32 i = 0; i < slots; ++i)
      *values.Append() = 0;
    if (result)
      result = stream->ReadU32Array(values.data, slots);
    for (u32 i = 0; result && i < slots; i += 2) {
      u32 index = 0, property = values.data[i];
      while (index < words.size && words.data[index].key < property)
        ++index;
      if (index == words.size || words.data[index].key != property) {
        words.Append();
        for (u32 j = words.size - 1; j > index; --j)
          words.data[j] = words.data[j - 1];
        words.data[index] = {property, values.data[i + 1]};
      }
    }
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasStringData) {
    for (u32 i = 0; i < texts.size; ++i)
      delete texts.data[i].value;
    texts.Clear();
    u32 count = 0, property;
    result = stream->ReadU32(&count);
    ScString value;
    for (u32 i = 0; i < count; ++i) {
      if (!result)
        break;
      result = stream->ReadU32(&property);
      if (result) {
        result = GeReadRecordText(city, stream, &value);
        if (result) {
          ScString copy(value);
          u32 index = 0;
          while (index < texts.size && texts.data[index].key < property)
            ++index;
          if (index == texts.size || texts.data[index].key != property) {
            ScString *stored = new ScString(copy);
            texts.Append();
            for (u32 j = texts.size - 1; j > index; --j)
              texts.data[j] = texts.data[j - 1];
            texts.data[index] = {property, stored};
          }
        }
      }
    }
  }
  if (!result)
    return result;
  if (flags & kGeRecordHasFloatData) {
    floats.Clear();
    u32 count = 0, property;
    f32 value;
    result = stream->ReadU32(&count);
    for (u32 i = 0; i < count; ++i) {
      result = stream->ReadU32(&property);
      if (result) {
        result = stream->ReadF32(&value);
        if (result) {
          u32 index = 0;
          while (index < floats.size && floats.data[index].key < property)
            ++index;
          if (index == floats.size || floats.data[index].key != property) {
            floats.Append();
            for (u32 j = floats.size - 1; j > index; --j)
              floats.data[j] = floats.data[j - 1];
            floats.data[index] = {property, value};
          }
        }
      }
    }
  }
  if (result && (flags & kGeRecordHasTokens)) {
    u32 count = tokensBegin ? u32(tokensEnd - tokensBegin) : 0;
    GeToken *entries = tokensBegin;
    for (u32 i = 0; i < count; ++i)
      if (entries[i].value) {
        entries[i].value->Release();
        entries[i].value = nullptr;
      }
    if (count)
      tokensEnd -= count;
    count = 0;
    result = stream->ReadU32(&count);
    ScVariant *publication = nullptr;
    u32 property = 0;
    for (u32 i = 0; i < count; ++i) {
      if (!result)
        break;
      result = stream->ReadU32(&property);
      if (result) {
        result = stream->ReadVariant(&publication);
        if (result && publication) {
          GeToken entry{property, publication};
          entry.value->AddRef();
          AppendToken(entry);
          ScRelease(&publication);
          entry.value->Release();
        }
      }
    }
  }
  return result;
}
