#include "geometry/api.h"
#include "common/ids.h"
#include "geometry/ge_building_data.h"
#include "resources/api.h"

GeBuildingData::GeBuildingData() : classId(kGeBuildingClassId) {
}

GeBuildingData::~GeBuildingData() {
}

bool GeBuildingData::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GeBuildingType, GeBuildingType);
  return GeBuildingAttrib::QueryInterface(iid, out);
}

u32 GeBuildingData::AddRef() {
  return GeBuildingAttrib::AddRef();
}

u32 GeBuildingData::Release() {
  return GeBuildingAttrib::Release();
}

// Borrow the actual resource key through the generic attribute base. Both generic and specific views share this key.
RsKey *GeBuildingData::Key() const {
  return GeBuildingAttrib::Key();
}

u8 GeBuildingData::MajorType() const {
  return 1;
}

u8 GeBuildingData::PowerRequired() {
  return powerRequired;
}

bool GeBuildingData::GetOccupantGZCLSID(u32 *out) const {
  *out = classId;
  return true;
}

bool GeBuildingData::SetOccupantGZCLSID(u32 value) {
  classId = value;
  return true;
}

bool GeBuildingData::SetPowerRequired(u8 value) {
  powerRequired = value;
  return true;
}

u32 GeBuildingData::Flags() {
  return flags;
}

// Return the borrowed generic occupant-attribute interface from the specialized building view. No identity query,
// reference operation or resource-key lookup belongs to this conversion.
GeAttrib *GeBuildingData::AsOccupantAttrib() {
  return static_cast<GeAttrib *>(this);
}

bool GeBuildingData::GetName(ScStringView *out) {
  return GeBuildingAttrib::GetName(out);
}

static const RsProperty GeFactoryProperty{0x73, 3, 1}, GePowerProperty{0xc9, 1, 1}, GePlacementProperty{0xca, 3, 1},
    GePlacementTextProperty{0xcb, 7, 1};

// Continue loading the raw power byte, placement flags and occupant factory class even when the base load fails.
// Optional failures use their individual defaults; virtual setters retain their original ordering and ignored results.
bool GeBuildingData::LoadProperties(RsPropertySet *properties) {
  bool loaded = GeBuildingAttrib::LoadProperties(properties);
  u8 power = 0;
  if (!properties->GetByte(GePowerProperty, &power, 0))
    power = 0;
  SetPowerRequired(power);
  if (!properties->GetWord(GePlacementProperty, &flags, 0))
    flags = 0;
  u32 clsid = 0;
  if (!properties->GetWord(GeFactoryProperty, &clsid, 0))
    clsid = kGeBuildingClassId;
  SetOccupantGZCLSID(clsid);
  return loaded;
}

// Sample the virtual power byte even when the base save fails, then short-circuit the additional property writes.
// Optional placement text follows the original flag order; its callback may change the final live factory class.
bool GeBuildingData::SaveProperties(RsPropertySet *properties) {
  bool saved = GeBuildingAttrib::SaveProperties(properties);
  u8 power = PowerRequired();
  if (!saved)
    return false;
  u8 result = properties->SetByte(GePowerProperty, power, 0);
  if (!result)
    return false;
  result = properties->SetWord(GePlacementProperty, flags, 0);
  if (!result)
    return false;
  if (GetPropertyFlag()) {
    ScString text("Debug Placement Info Only: ");
    if (flags & 1)
      text.Append("CN ");
    if (flags & 2)
      text.Append("CS ");
    if (flags & 4)
      text.Append("CE ");
    if (flags & 8)
      text.Append("CW ");
    if (flags & 0x20)
      text.Append("ENE ");
    if (flags & 0x10)
      text.Append("ENW ");
    if (flags & 0x80)
      text.Append("ESE ");
    if (flags & 0x40)
      text.Append("ESW ");
    if (flags & 0x100)
      text.Append("FI ");
    if (flags & 0x200)
      text.Append("FL ");
    result = properties->SetText(GePlacementTextProperty, text, 0);
  }
  if (result && classId != kGeBuildingClassId && classId)
    result = properties->SetWord(GeFactoryProperty, classId, 0);
  return result != 0;
}

// Construct the specialized building descriptor and return its distinct building-attribute interface to the director.
// Its shared resource reference count remains zero until the factory caller queries or retains the result.
ScUnknown *GeCreateBuildingData() {
  return static_cast<GeBuildingType *>(new GeBuildingData);
}

GePortData::GePortData() {
}

GePortData::~GePortData() {
}

bool GePortData::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_GePortType, GePortType);
  return GeBuildingData::QueryInterface(iid, out);
}

u32 GePortData::AddRef() {
  return GeBuildingData::AddRef();
}

u32 GePortData::Release() {
  return GeBuildingData::Release();
}

// Return the building base's borrowed resource key; the port view does not own a second key.
RsKey *GePortData::Key() const {
  return GeBuildingData::Key();
}

bool GePortData::GetName(ScStringView *out) {
  return GeBuildingData::GetName(out);
}

u8 GePortData::PowerRequired() {
  return GeBuildingData::PowerRequired();
}

bool GePortData::GetOccupantGZCLSID(u32 *out) const {
  return GeBuildingData::GetOccupantGZCLSID(out);
}

bool GePortData::SetOccupantGZCLSID(u32 value) {
  return GeBuildingData::SetOccupantGZCLSID(value);
}

// Convert the extended port view to the same borrowed generic attribute identity used by the building base.
GeAttrib *GePortData::AsOccupantAttrib() {
  return GeBuildingData::AsOccupantAttrib();
}

u32 GePortData::Flags() {
  return GeBuildingData::Flags();
}

// Borrow the original building interface at complete+316 rather than the distinct extended port identity at+332.
GeBuildingType *GePortData::AsBuildingAttrib() {
  return static_cast<GeBuildingType *>(static_cast<GeBuildingData *>(this));
}

u32 GePortData::GetPortType() {
  return portType;
}

static const RsProperty GePortProperty{0x1f4, 3, 1}, GePortTextProperty{0x1f5, 7, 1};

// Load the building base before requiring property500. Either failure sets the port type to zero and reports failure.
bool GePortData::LoadProperties(RsPropertySet *properties) {
  u32 value = 0;
  bool loaded = GeBuildingData::LoadProperties(properties) && properties->GetWord(GePortProperty, &value, 0);
  portType = loaded ? value : 0;
  return loaded;
}

// Save the building base, then snapshot the port word for its scalar write. Optional debug text samples the live type
// after the property-flag callback and uses the original exact labels without changing the saved scalar snapshot.
bool GePortData::SaveProperties(RsPropertySet *properties) {
  if (!GeBuildingData::SaveProperties(properties))
    return false;
  u32 value = portType;
  bool saved = properties->SetWord(GePortProperty, value, 0) != 0;
  if (saved && GetPropertyFlag()) {
    ScString text("PortType Debug Info Only: ");
    const char *label;
    switch (portType) {
    case 1:
      label = "RUNWAY";
      break;
    case 2:
      label = "TAXIWAY";
      break;
    case 3:
      label = "RTCONNECTOR";
      break;
    case 7:
      label = "SEAWALL";
      break;
    case 8:
      label = "SPCONNECTOR";
      break;
    case 9:
      label = "PIER";
      break;
    default:
      label = "NONE";
      break;
    }
    text.Append(label);
    saved = properties->SetText(GePortTextProperty, text, 0) != 0;
  }
  return saved;
}

// Construct the actual port resource and return its extended interface without acquiring a reference.
ScUnknown *GeCreatePortData() {
  return static_cast<GePortType *>(new GePortData);
}

// Query a building's original base attributes for the port extension and capture its full type before releasing it.
u32 SC3_STDCALL GeGetPortType(ScOccupant *occupant) {
  u32 type = 0;
  void *buildingView;
  if (occupant->QueryInterface(IID_BdBuilding, &buildingView)) {
    GeAttrib *attributes = static_cast<BdBuilding *>(buildingView)->GetBaseAttribData();
    void *portView;
    if (attributes && attributes->QueryInterface(IID_GePortType, &portView)) {
      type = static_cast<GePortType *>(portView)->GetPortType();
      static_cast<GePortType *>(portView)->Release();
    }
    static_cast<BdBuilding *>(buildingView)->Release();
  }
  return type;
}

// These forwarding overrides expose the original complete building and port attribute view prefixes.
// The existing generic resource implementation continues to own their fields, references and game behavior.

bool GeBuildingData::IsGeomFlag(u32 mask) {
  return GeBuildingAttrib::IsGeomFlag(mask);
}

u8 GeBuildingData::SetGeomFlag(u32 mask) {
  return GeBuildingAttrib::SetGeomFlag(mask);
}

u8 GeBuildingData::ClearGeomFlag(u32 mask) {
  return GeBuildingAttrib::ClearGeomFlag(mask);
}

u32 GeBuildingData::GetGeomFlag() {
  return GeBuildingAttrib::GetGeomFlag();
}

u8 GeBuildingData::GetOccLayerFlag() {
  return GeBuildingAttrib::GetOccLayerFlag();
}

void GeBuildingData::SetOccLayerFlag(u8 mask) {
  GeBuildingAttrib::SetOccLayerFlag(mask);
}

u8 GeBuildingData::SubTypeInfo() const {
  return GeBuildingAttrib::SubTypeInfo();
}

u8 GeBuildingData::AnimType() const {
  return GeBuildingAttrib::AnimType();
}

u32 GeBuildingData::SizeInCellsX() {
  return GeBuildingAttrib::SizeInCellsX();
}

u32 GeBuildingData::SizeInCellsZ() {
  return GeBuildingAttrib::SizeInCellsZ();
}

u32 GeBuildingData::SizeInCellsY() {
  return GeBuildingAttrib::SizeInCellsY();
}

// Borrow the city-agent type resolved during property loading. This getter does not acquire a reference.
CtAgentType *GeBuildingData::CityAgentType() {
  return GeBuildingAttrib::CityAgentType();
}

u32 GeBuildingData::TypeId() const {
  return GeBuildingAttrib::TypeId();
}

// Let the override select indexed art, otherwise choose the first supported mask bit. Cache misses retain the
// resource-manager output directly; returned definitions are borrowed.
SpDefinition *GeBuildingData::GetSpriteAttrib(u32 layer) {
  return GeBuildingAttrib::GetSpriteAttrib(layer);
}

// Borrow the override's unindexed art when one is installed. Otherwise dispatch the original-sprite getter virtually,
// preserving derived providers rather than directly selecting this class's primary cache.
SpDefinition *GeBuildingData::GetSpriteAttrib() {
  return GeBuildingAttrib::GetSpriteAttrib();
}

// Borrow the primary sprite definition independently of any override. Resolve an empty cache through the same
// resource-manager acquisition used by indexed art.
SpDefinition *GeBuildingData::GetOriginalSpriteAttrib() {
  return GeBuildingAttrib::GetOriginalSpriteAttrib();
}

u32 GeBuildingData::GetDefaultLayerFlag() const {
  return GeBuildingAttrib::GetDefaultLayerFlag();
}

bool GeBuildingData::SetDefaultLayerFlag(u32 layer) {
  return GeBuildingAttrib::SetDefaultLayerFlag(layer);
}

iptr GeBuildingData::GetPeopleProducerAttrib() {
  return GeBuildingAttrib::GetPeopleProducerAttrib();
}

iptr GeBuildingData::GetVehicleProducerAttrib() {
  return GeBuildingAttrib::GetVehicleProducerAttrib();
}

u8 GeBuildingData::Flammability() {
  return GeBuildingAttrib::Flammability();
}

u32 GeBuildingData::GetSoundObjectId(i32 *index) {
  return GeBuildingAttrib::GetSoundObjectId(index);
}

void GeBuildingData::GetSoundKeyA(RsKey *out) {
  GeBuildingAttrib::GetSoundKeyA(out);
}

void GeBuildingData::GetSoundKeyB(RsKey *out) {
  GeBuildingAttrib::GetSoundKeyB(out);
}

void GeBuildingData::GetPlopCursorKey(RsKey *out) {
  GeBuildingAttrib::GetPlopCursorKey(out);
}

void GeBuildingData::GetQueryDescKey(RsKey *out) {
  GeBuildingAttrib::GetQueryDescKey(out);
}

void GeBuildingData::GetQueryFormatFlags(u32 *out) {
  GeBuildingAttrib::GetQueryFormatFlags(out);
}

u32 GeBuildingData::GetQueryDialogClass() {
  return GeBuildingAttrib::GetQueryDialogClass();
}

bool GeBuildingData::SetQueryDialogClass(u32 clsid) {
  return GeBuildingAttrib::SetQueryDialogClass(clsid);
}

bool GeBuildingData::DebugInstanceTag(ScStringView *out) {
  return GeBuildingAttrib::DebugInstanceTag(out);
}

bool GeBuildingData::DebugClassTag(ScStringView *out) {
  return GeBuildingAttrib::DebugClassTag(out);
}

bool GeBuildingData::DebugTypeTag(ScStringView *out) {
  return GeBuildingAttrib::DebugTypeTag(out);
}

bool GeBuildingData::GetNameKey(RsKey *out) {
  return GeBuildingAttrib::GetNameKey(out);
}

void GeBuildingData::SetOverRideAttrib(OcOverride *value) {
  GeBuildingAttrib::SetOverRideAttrib(value);
}

// Borrow the retained override interface. This getter does not acquire a reference.
OcOverride *GeBuildingData::GetOverRideAttrib() {
  return GeBuildingAttrib::GetOverRideAttrib();
}

bool GeBuildingData::GetArchitectName(ScStringView *out) {
  return GeBuildingAttrib::GetArchitectName(out);
}

bool GePortData::IsGeomFlag(u32 mask) {
  return GeBuildingData::IsGeomFlag(mask);
}

u8 GePortData::SetGeomFlag(u32 mask) {
  return GeBuildingData::SetGeomFlag(mask);
}

u8 GePortData::ClearGeomFlag(u32 mask) {
  return GeBuildingData::ClearGeomFlag(mask);
}

u32 GePortData::GetGeomFlag() {
  return GeBuildingData::GetGeomFlag();
}

u8 GePortData::GetOccLayerFlag() {
  return GeBuildingData::GetOccLayerFlag();
}

void GePortData::SetOccLayerFlag(u8 mask) {
  GeBuildingData::SetOccLayerFlag(mask);
}

u8 GePortData::MajorType() const {
  return GeBuildingData::MajorType();
}

u8 GePortData::SubTypeInfo() const {
  return GeBuildingData::SubTypeInfo();
}

u8 GePortData::AnimType() const {
  return GeBuildingData::AnimType();
}

u32 GePortData::SizeInCellsX() {
  return GeBuildingData::SizeInCellsX();
}

u32 GePortData::SizeInCellsZ() {
  return GeBuildingData::SizeInCellsZ();
}

u32 GePortData::SizeInCellsY() {
  return GeBuildingData::SizeInCellsY();
}

// Borrow the city-agent type resolved during property loading. This getter does not acquire a reference.
CtAgentType *GePortData::CityAgentType() {
  return GeBuildingData::CityAgentType();
}

u32 GePortData::TypeId() const {
  return GeBuildingData::TypeId();
}

// Let the override select indexed art, otherwise choose the first supported mask bit. Cache misses retain the
// resource-manager output directly; returned definitions are borrowed.
SpDefinition *GePortData::GetSpriteAttrib(u32 layer) {
  return GeBuildingData::GetSpriteAttrib(layer);
}

// Borrow the override's unindexed art when one is installed. Otherwise dispatch the original-sprite getter virtually,
// preserving derived providers rather than directly selecting this class's primary cache.
SpDefinition *GePortData::GetSpriteAttrib() {
  return GeBuildingData::GetSpriteAttrib();
}

// Borrow the primary sprite definition independently of any override. Resolve an empty cache through the same
// resource-manager acquisition used by indexed art.
SpDefinition *GePortData::GetOriginalSpriteAttrib() {
  return GeBuildingData::GetOriginalSpriteAttrib();
}

u32 GePortData::GetDefaultLayerFlag() const {
  return GeBuildingData::GetDefaultLayerFlag();
}

bool GePortData::SetDefaultLayerFlag(u32 layer) {
  return GeBuildingData::SetDefaultLayerFlag(layer);
}

iptr GePortData::GetPeopleProducerAttrib() {
  return GeBuildingData::GetPeopleProducerAttrib();
}

iptr GePortData::GetVehicleProducerAttrib() {
  return GeBuildingData::GetVehicleProducerAttrib();
}

u8 GePortData::Flammability() {
  return GeBuildingData::Flammability();
}

u32 GePortData::GetSoundObjectId(i32 *index) {
  return GeBuildingData::GetSoundObjectId(index);
}

void GePortData::GetSoundKeyA(RsKey *out) {
  GeBuildingData::GetSoundKeyA(out);
}

void GePortData::GetSoundKeyB(RsKey *out) {
  GeBuildingData::GetSoundKeyB(out);
}

void GePortData::GetPlopCursorKey(RsKey *out) {
  GeBuildingData::GetPlopCursorKey(out);
}

void GePortData::GetQueryDescKey(RsKey *out) {
  GeBuildingData::GetQueryDescKey(out);
}

void GePortData::GetQueryFormatFlags(u32 *out) {
  GeBuildingData::GetQueryFormatFlags(out);
}

u32 GePortData::GetQueryDialogClass() {
  return GeBuildingData::GetQueryDialogClass();
}

bool GePortData::SetQueryDialogClass(u32 clsid) {
  return GeBuildingData::SetQueryDialogClass(clsid);
}

bool GePortData::DebugInstanceTag(ScStringView *out) {
  return GeBuildingData::DebugInstanceTag(out);
}

bool GePortData::DebugClassTag(ScStringView *out) {
  return GeBuildingData::DebugClassTag(out);
}

bool GePortData::DebugTypeTag(ScStringView *out) {
  return GeBuildingData::DebugTypeTag(out);
}

bool GePortData::GetNameKey(RsKey *out) {
  return GeBuildingData::GetNameKey(out);
}

void GePortData::SetOverRideAttrib(OcOverride *value) {
  GeBuildingData::SetOverRideAttrib(value);
}

// Borrow the retained override interface. This getter does not acquire a reference.
OcOverride *GePortData::GetOverRideAttrib() {
  return GeBuildingData::GetOverRideAttrib();
}

bool GePortData::GetArchitectName(ScStringView *out) {
  return GeBuildingData::GetArchitectName(out);
}

bool GePortData::SetPowerRequired(u8 value) {
  return GeBuildingData::SetPowerRequired(value);
}
