#ifndef SC3_GEOMETRY_BUILDING_H
#define SC3_GEOMETRY_BUILDING_H

#include "city/api.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/grid.h"
#include "geometry/module.h"
#include "geometry/utilities.h"
#include "occupants/api.h"
#include "resources/factory.h"
#include "resources/resource.h"

// Original building-specific attribute view has the complete generic method prefix and four building operations.
// Preserve the proved virtual surface without inferring an undocumented source-level relationship to GeAttrib.
class GeBuildingType : public ScUnknown {
public:
  virtual bool GetOccupantGZCLSID(u32 *) const = 0;
  virtual bool SetOccupantGZCLSID(u32) = 0;
  virtual RsKey *Key() const = 0;
  virtual bool IsGeomFlag(u32) = 0;
  virtual u8 SetGeomFlag(u32 mask) = 0;
  virtual u8 ClearGeomFlag(u32 mask) = 0;
  virtual u32 GetGeomFlag() = 0;
  virtual u8 GetOccLayerFlag() = 0;
  virtual void SetOccLayerFlag(u8 mask) = 0;
  virtual u8 MajorType() const = 0;
  virtual u8 SubTypeInfo() const = 0;
  virtual u8 AnimType() const = 0;
  virtual bool GetName(ScStringView *out) = 0;
  virtual u32 SizeInCellsX() = 0;
  virtual u32 SizeInCellsZ() = 0;
  virtual u32 SizeInCellsY() = 0;
  virtual CtAgentType *CityAgentType() = 0;
  virtual u32 TypeId() const = 0;
  virtual SpDefinition *GetSpriteAttrib(u32 layer) = 0;
  virtual SpDefinition *GetSpriteAttrib() = 0;
  virtual SpDefinition *GetOriginalSpriteAttrib() = 0;
  virtual u32 GetDefaultLayerFlag() const = 0;
  virtual bool SetDefaultLayerFlag(u32) = 0;
  virtual iptr GetPeopleProducerAttrib() = 0;
  virtual iptr GetVehicleProducerAttrib() = 0;
  virtual u8 Flammability() = 0;
  virtual u32 GetSoundObjectId(i32 *index) = 0;
  virtual void GetSoundKeyA(RsKey *) = 0;
  virtual void GetSoundKeyB(RsKey *) = 0;
  virtual void GetPlopCursorKey(RsKey *) = 0;
  virtual void GetQueryDescKey(RsKey *) = 0;
  virtual void GetQueryFormatFlags(u32 *) = 0;
  virtual u32 GetQueryDialogClass() = 0;
  virtual bool SetQueryDialogClass(u32) = 0;
  virtual bool DebugInstanceTag(ScStringView *) = 0;
  virtual bool DebugClassTag(ScStringView *) = 0;
  virtual bool DebugTypeTag(ScStringView *) = 0;
  virtual bool GetNameKey(RsKey *) = 0;
  virtual void SetOverRideAttrib(OcOverride *) = 0;
  virtual OcOverride *GetOverRideAttrib() = 0;
  virtual bool GetArchitectName(ScStringView *) = 0;
  virtual GeAttrib *AsOccupantAttrib() = 0; // Original specific +176; borrowed complete+20 generic view.
  virtual u8 PowerRequired() = 0;
  virtual bool SetPowerRequired(u8 value) = 0;
  virtual u32 Flags() = 0; // Original specific +188 returns the complete property202 DWORD.
};

// Original 44-byte building: primary building interface, city agent at+4 and occupant at+8. Actual occupancy changes
// refresh its sprite through the city graphics lock and the selected definition.
class GeBuilding : public BdBuilding, public CtAgent, public GeOccupant {
public:
  GeBuilding();
  ~GeBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(GeAttrib *value) override;
  bool Init(RsKey *key) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  u32 GetGeomFlag() override;
  u32 GetMemUse() const override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  virtual ScOccupant *AsISC3Occupant() override;
  virtual CtAgent *AsISC3CityAgent() override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool SetName(const ScStringView &name) override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &date) override;
  u32 TypeId() const override;
  virtual GeAttrib *GetBaseAttribData() override;
  GeAttrib *GetAttribData() override;
  u32 GetOccupancyState() override;
  void SetOccupancyState(u32 value) override;
  void SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  void ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  virtual bool GetOccupancyStateFlag(u32 mask) override;
  u8 PowerRequired() override;
  bool IsAgentLocking() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  u8 DoQueryViews(ScMessage *message, UiQueryWindow *window) override;
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query) override;
  u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query) override;
  u8 DoExitQueryWindow(ScMessage *message, UiQueryWindow *window) override;
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) override;
  // Native effect of original virtual100; its unused private return declaration remains unresolved.
  virtual void RefreshSprite();
  u32 occupancyFlags;
  CtAgentType *agentType;
  GeAttrib *cachedAttrib;
  SpDefinition *cachedSprite;
};

class RsPropertySet;
class CtAgentType;

// The property interface is established by IID and the constructor's third vtable.
// Persistence retains its original Boolean contracts; the byte flag keeps its observed, unnamed semantics.
class GePropertyObject : public ScUnknown {
public:
  virtual bool LoadProperties(RsPropertySet *properties) = 0;
  virtual bool SaveProperties(RsPropertySet *properties) = 0;
  virtual u8 SetPropertyFlag(u8 value) = 0;
  virtual u8 GetPropertyFlag() = 0;
};

struct GeCachedArt {
  void *resource; // Actual COM output slot, written directly during resource acquisition.
  RsKey key;
};

// The three bases and retained fields follow constructor and its interface adjustments.
// Property persistence uses genuine typed property operations; sprite acquisition uses the framework resource manager.
class GeBuildingAttrib : public RsResource, public GeAttrib, public GePropertyObject {
public:
  GeBuildingAttrib();
  ~GeBuildingAttrib() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  RsKey *Key() const override;
  bool GetOccupantGZCLSID(u32 *out) const override;
  bool SetOccupantGZCLSID(u32 value) override;
  bool SetDefaultLayerFlag(u32 layer) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  virtual bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u8 GetOccLayerFlag() override;
  void SetOccLayerFlag(u8 mask) override;
  u8 MajorType() const override;
  u8 SubTypeInfo() const override;
  u8 AnimType() const override;
  CtAgentType *CityAgentType() override;
  iptr GetPeopleProducerAttrib() override;
  iptr GetVehicleProducerAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetOriginalSpriteAttrib() override;
  u32 SizeInCellsX() override;
  u32 SizeInCellsZ() override;
  u32 SizeInCellsY() override;
  u32 TypeId() const override;
  u32 GetDefaultLayerFlag() const override;
  virtual u8 Flammability() override;
  bool GetName(ScStringView *out) override;
  virtual bool GetArchitectName(ScStringView *out) override;
  virtual bool GetNameKey(RsKey *out) override;
  u32 GetSoundObjectId(i32 *index) override;
  virtual void GetSoundKeyA(RsKey *out) override;
  virtual void GetSoundKeyB(RsKey *out) override;
  virtual void GetPlopCursorKey(RsKey *out) override;
  virtual void GetQueryDescKey(RsKey *out) override;
  virtual void GetQueryFormatFlags(u32 *out) override;
  virtual u32 GetQueryDialogClass() override;
  virtual bool SetQueryDialogClass(u32 clsid) override;
  virtual void SetOverRideAttrib(OcOverride *value) override;
  virtual OcOverride *GetOverRideAttrib() override;
  bool LoadProperties(RsPropertySet *properties) override;
  bool SaveProperties(RsPropertySet *properties) override;
  u8 SetPropertyFlag(u8 value) override;
  u8 GetPropertyFlag() override;

protected:
  // Native private resolver contracts retain the defined AL result; Boolean/BYTE source spelling is unresolved.
  u8 ResolvePeopleProducer(const RsKey *key);
  u8 ResolveVehicleProducer(const RsKey *key);
  u32 occupantClass;
  ScString name;
  u32 geometryFlags, sizeX, sizeZ, sizeY;
  CtAgentType *agentType;
  GeCachedArt primary;
  void *peopleAttrib, *vehicleAttrib; // Genuine COM output slots; used through their returned interface prefix.
  RsKey peopleKey, vehicleKey;
  u8 flammability;
  RsKey soundA, soundB;
  u8 cachedLayers;
  GeCachedArt variants[7];
  RsKey nameKey, plopKey, queryKey;
  u32 queryFlags;
  u8 propertyFlag;
  OcOverride *overrideAttrib;
  u32 queryDialogClass;
};

// Actual flora descriptor inherits the generic attribute fields unchanged and supplies major type five.
class GeFloraAttrib : public GeBuildingAttrib {
public:
  GeFloraAttrib();
  ~GeFloraAttrib() override;
  bool LoadProperties(RsPropertySet *properties) override;
  bool SaveProperties(RsPropertySet *properties) override;
  u8 MajorType() const override;
};

class ScString;
class BdBuilding;
class CtDefinition;
class CtAgentTree;
class OcAttribCache;
class GeBuildingType;
class GeAttrib;
class SpDefinition;

// Original default-selection key has defined members at0/4/8/12/13; its padding is never compared.
struct GeDefaultKey {
  u32 state;
  u8 width;
  u32 type;
  bool powered, flagged;
};

// Stable game-specific map entries replace the original STL nodes. Each vector owns one reference per pointer;
// memoized entries own their references independently from the source entry.
struct GeDefaultEntry {
  GeDefaultKey key;
  ScArray<GeBuildingType *> values;
  GeDefaultEntry *next;
};

extern GeDefaultEntry *GeDefaultEntries;

struct GeDefaultCategory {
  u32 type;
  GeDefaultCategory *next;
};

// The original set iterates in unsigned type order and keeps nodes stable through ancestor callbacks.
extern GeDefaultCategory *GeDefaultCategories;
extern GeRandom GeDefaultRandom;

// Original16-byte receiver allocated by controller StaticInit. Its read-side INI loader retains real services and resource acquisitions.
class GeBuildingDefaults {
public:
  GeBuildingDefaults();
  virtual ~GeBuildingDefaults();
  bool Load(CtDefinition *definition);
  void Clear(); // Native teardown phase; original0444E nominal return remains unresolved.
  GeAttrib *GetAttrib(BdBuilding *building);
  SpDefinition *GetSprite(BdBuilding *building);

protected:
  u8 initialized;
  OcAttribCache *cache;
  CtAgentTree *tree;
};

extern GeBuildingDefaults *GeDefaultBuildings;

// Actual generic-library callback ABI; the default row does not use its context pointer.
void GeParseDefaultRow(const ScString &name, const ScString &value, void *context);

// Original20-byte concrete factory shares the base lifetime and delegation state without adding a second counter.
class GeBuildingFactory : public GeClassFactory {
public:
  GeBuildingFactory();
  ~GeBuildingFactory() override;
  bool CreateCommand(u32 classId, u32 iid, void **out) override;
};

class GeBuildingType;

// Original eight-byte entry: an acquired specialized building descriptor and its full resource weight word.
struct GeWeightedBuilding {
  GeBuildingType *attributes;
  u32 weight;
};

// Primary interface A1F592F9, separate from the resource identity at complete+4.
class GeBuildingGroupView : public ScUnknown {
public:
  virtual u32 GetLandUse() = 0;
  virtual u32 GetBuildingCount() = 0;
  virtual u8 GetBuildings(GeWeightedBuilding *out) = 0;
};

// Actual44-byte resource for type81F53D09. The stored count is independent of the vector's live storage length.
class GeBuildingGroup : public GeBuildingGroupView, public RsResource {
public:
  GeBuildingGroup();
  ~GeBuildingGroup() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetLandUse() override;
  u32 GetBuildingCount() override;
  u8 GetBuildings(GeWeightedBuilding *out) override;
  u8 Load(RsStream *stream);

private:
  u8 LoadBinary(RsStream *stream);
  u8 LoadText(RsStream *stream);
  u32 landUse, buildingCount;
  ScArray<GeWeightedBuilding> buildings;
};

// Actual resource factory A1F53F57 uses the standard two-interface factory layout and this DLL's module holds.
class GeBuildingGroupFactory : public RsFactory {
public:
  GeBuildingGroupFactory();
  u32 AddRef() override;
  u32 Release() override;
  bool Load(RsRecord *record, u32 iid, void **out) override;
  bool New(u32 type, u32 iid, void **out) override;
  bool ReadRecord(RsResource *resource, RsRecord *record) override;
  bool SerialRead(RsResource *resource, RsStream *stream) override;
  bool WriteRecord(const RsResource *resource, RsRecord *record) override;
  virtual bool SerialWrite(RsResource *resource, RsStream *stream) override;
};

class CtCity;
class RsStream;
class ScVariant;

struct GeRecordWord {
  u32 key, value;
};

struct GeRecordText {
  u32 key;
  ScString *value;
};

struct GeRecordFloat {
  u32 key;
  f32 value;
};

struct GeToken {
  u32 key;
  ScVariant *value;
};

// Actual occupant persistence descriptor, primary IID431517ED, with packed fields and owned property collections.
// Native arrays replace its three ordered maps; the variant-token range keeps its original pointer fields.
// Windows CodeView4BBB establishes the complete public descriptor order, const getters, signed date arguments
// and mutable String inputs. Byte carriers preserve replies without asserting literal old bool/BYTE spelling.
// Saved-record presence/version/removal flags; these differ from the live occupant flags.
enum GeRecordFlag : u32 {
  kGeRecordCanSimRemove = 0x20000000,
  kGeRecordCanUserRemove = 0x40000000,
  kGeRecordWithoutRemoveFlags = 0x9fffffff,
  kGeRecordVersionFlag = 0x00100000,
  kGeRecordWithoutVersionFlag = 0xffefffff,
  kGeRecordHasName = 0x00200000,
  kGeRecordHasCreateDate = 0x00400000,
  kGeRecordHasIntData = 0x00800000,
  kGeRecordHasStringData = 0x01000000,
  kGeRecordHasFloatData = 0x02000000,
  kGeRecordHasTokens = 0x04000000,
  kGeRecordHasLayerData1 = 0x08000000,
  kGeRecordHasLayerData2 = 0x10000000,
  kGeRecordWithoutFootprint = 0xfffff000,
};

// Six-bit footprint dimensions and the independent persisted-version/removal bit positions.
enum GeRecordField : u32 {
  kGeRecordDimensionMask = 0x0000003f,
  kGeRecordSecondDimensionShift = 0x00000006,
  kGeRecordVersionShift = 0x00000014,
  kGeRecordSimRemoveShift = 0x0000001d,
  kGeRecordUserRemoveShift = 0x0000001e,
};

class GeRecord : public OcPersistInfo {
public:
  explicit GeRecord(u8 callerOwned);
  virtual ~GeRecord();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetAttribKey(RsKey *out) const override;
  u8 GetTypeId(u32 *out) const override;
  u8 GetLocationInCells(u32 *x, u32 *z, u32 *altitude) const override;
  u8 GetSizeInCells(u32 *first, u32 *second) const override;
  u8 GetRotationFlag(u32 *out) const override;
  u8 GetCanSimRemoveFlag() const override;
  u8 GetCanUserRemoveFlag() const override;
  void SetAttribKey(const RsKey &key) override;
  void SetTypeId(u32 type) override;
  void SetLocationInCells(u32 x, u32 z, u32 altitude) override;
  void SetSizeInCells(u32 first, u32 second) override;
  void SetRotationFlag(u32 rotation) override;
  void SetRemoveFlags(u8 first, u8 second) override;
  u32 GetPersistRecordVersion() const override;
  u8 DidSameKeyWriteResource() override;
  u8 SetSameKeyWroteResource(u8 enabled) override;
  u8 HasNameField() const override;
  u8 GetNameField(ScStringView *out) const override;
  u8 SetNameField(ScStringView *value) override;
  u8 HasSimCreateDate() const override;
  u8 GetSimCreateDate(i32 *year, i32 *day) const override;
  u8 SetSimCreateDate(i32 year, i32 day) override;
  u8 HasLayerData1() const override;
  u8 GetLayerData1(u32 *out) const override;
  u8 SetLayerData1(u32 value) override;
  u8 HasLayerData2() const override;
  u8 GetLayerData2(u32 *out) const override;
  u8 SetLayerData2(u32 value) override;
  u32 GetIntDataCount() const override;
  u8 GetIntData(u32 key, u32 *out) override;
  u8 SetIntData(u32 key, u32 value) override;
  u32 GetStringDataCount() const override;
  u8 GetStringData(u32 key, ScStringView *out) override;
  u8 SetStringData(u32 key, ScStringView *value) override;
  u32 GetFloatDataCount() const override;
  u8 GetFloatData(u32 key, f32 *out) override;
  u8 SetFloatData(u32 key, f32 value) override;
  u32 GetTokenCount() const override;
  u8 GetToken(u32 key, ScVariant **out) override;
  u8 AddToken(u32 key, ScVariant *value) override;
  u8 Clear() override;
  u8 SerialRead(CtCity *city, u32 codec, RsStream *stream) override;
  u8 SerialWrite(CtCity *city, RsStream *stream) override;

protected:
  void AppendToken(const GeToken &entry);
  u32 position, flags;
  RsKey key;
  u32 type;
  i32 birthYear, birthDay;
  ScString name;
  u32 state, field3C;
  ScArray<GeRecordWord> words;
  ScArray<GeRecordText> texts;
  ScArray<GeRecordFloat> floats;
  GeToken *tokensBegin, *tokensEnd, *tokensStorageEnd;
  u32 references;
  u8 callerOwned;
};

// Factory12BB5 installs this building's three tables after the same44-byte base construction. Animation adds no fields;
// its class-specific animation classification and tick forwarding accompany the start/stop methods.
class GeAnimatedBuilding : public GeBuilding {
public:
  u8 AnimType() const override;
  i32 Tick(u32 elapsed) override;
  void AnimStart() override;
  void AnimStop() override;
};

#endif
