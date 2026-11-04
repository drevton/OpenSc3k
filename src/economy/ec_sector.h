#ifndef SC3_ECONOMY_EC_SECTOR_H
#define SC3_ECONOMY_EC_SECTOR_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/rect.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "geometry/ge_types.h"
#include "graphics/api.h"
#include "occupants/animation_manager.h"
#include "terrain/tr_dirt_bag.h"

class GeAttrib;
class SpSprite;
class UiQuery;
class UiQueryWindow;

// SIMECO's module-local occupant base, established by its constructor and building interface adjustors.
// Private stream-state adapters remain unreconstructed; public operations use the shared producer contracts.
class EcOccupant : public ScOccupant {
public:
  EcOccupant();
  virtual ~EcOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(RsKey *key) override;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  CtAgentType *CityAgentType() override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  u8 SetDefaultLayerFlag(u32 mask) override;
  bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  u8 SubTypeInfo() const override;
  u32 TypeId() const override;
  GeAttrib *GetAttribData() override;
  bool CanSimRemove() const override;
  void SetSimRemove(bool enabled) override;
  bool CanUserRemove() const override;
  void SetUserRemove(bool enabled) override;
  void GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const override;
  bool GetRotationFlag(u32 *rotation) const override;
  void GetLocation(CtCoord *out) const override;
  void GetLocationInCellCoords(u32 *x, u32 *z) const override;
  u8 GetAlt() const override;
  u16 GetAltAnimCells() const override;
  void SetLocation(const CtCoord &value) override;
  void SetLocationInCellCoords(u32 x, u32 z) override;
  void SetAlt(u8 altitude) override;
  bool GetMarkBit() const override;
  void SetMarkBit(bool marked) override;
  void SetSubTypeInfo(u8 subtype) override;
  void SetOrientation(u16 rotation, u16 pitch, u16 roll) override;
  u8 SetRotationFlag(u32 rotation) override;
  u32 GetSoundObjectId(i32 index) override;
  void GetBounds(GeBounds *out) const override;
  void GetBoundingRectInCellCoords(GeCellRect *out) const override;

  bool DoMessage(ScMessage *) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  void SetOccLayerFlag(u32 mask) override;
  u8 MajorType() const override;
  u32 GetSubTypeData(u32) override;
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32) override;
  bool GetName(ScStringView *out) override;
  bool GetSubSystemData(u32 id, void **out) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpSprite *GetSpriteInst() override;
  SpSprite *GetSpriteInst(u32 layer) override;
  u32 GetDefaultLayerFlag() const override;
  u8 SizeInCellsX() const override;
  u8 SizeInCellsZ() const override;
  u8 SizeInCellsY() const override;
  u32 SizeInAnimCellsX() const override;
  u32 SizeInAnimCellsZ() const override;
  u32 SizeInAnimCellsY() const override;
  void GetLocationInAnimCellCoords(u32 *x, u32 *z) const override;
  u32 GetMemUse() const override;
  bool DebugInstanceTag(ScStringView *) override;
  bool DebugClassTag(ScStringView *) override;
  bool DebugTypeTag(ScStringView *) override;

protected:
  // Actual private query slots292..304, with byte returns and separate original message/query arguments.
  virtual u8 QueryBasic(ScMessage *message, UiQueryWindow *query);
  virtual u8 QueryTile(ScMessage *message, UiQuery *query);
  virtual u8 QueryDetail(ScMessage *message, UiQuery *query);
  virtual u8 QueryResponse(ScMessage *message, UiQueryWindow *query);
  // Original private slot308 returns a Boolean byte; SDK supplies no nominal private declaration.
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

class CtNeighborsLayer;
class CtOrdinanceLayer;
class CtAgentTree;
class EcByteMap;
struct EcDemandValveEntry;
struct EcDemandAgentEntry;
struct EcDemandBinding;
class EcDemandAgentManipulator;

// Original demand-wide simulation manipulator has its own count and a retained primary valve-layer view.
// Shutdown releases the layer explicitly; destruction itself does not repair an omitted shutdown.
class EcDemandManipulator : public CtLayerManipulator {
public:
  EcDemandManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Complete private32/36 AL effects; original nominal bool/BYTE declarations remain unresolved.
  virtual u8 Init(EcValveLayer *layer);
  virtual u8 Shutdown();
  virtual ~EcDemandManipulator();

protected:
  u32 references;
  EcValveLayer *layer;
};

// Native grouping of the actual primary valve-layer and secondary city-layer identities.
// Typed valve/agent lists replace only the original ordered library containers; this is not a foreign object overlay.
class EcDemand : public EcValveLayer, public CtLayer {
public:
  EcDemand();
  virtual ~EcDemand();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool AddValveToLayer(EcValve *valve) override;
  bool EndOfMonth() override;
  bool CreateNewValve(u32 identifier, u32 iid, void **out) override;
  bool GetValvePointer(u32 identifier, u32 iid, void **out) override;
  i16 GetAgentSupplyEffect(u32 valve, u32 agent) override;
  i16 GetAgentDemandEffect(u32 valve, u32 agent) override;
  void GetDensity(u32 x, u32 z, u8 *out) override;
  u32 GetTaxableResidentialDensity() override;
  u32 GetTaxableCommercialDensity() override;
  u32 GetTaxableIndustrialDensity() override;
  void DebugSetValve(ScStringView *name, ScStringView *supply, ScStringView *demand) override;

protected:
  friend class EcDemandAgentManipulator;
  // Original private query effects and demand-raster construction are implemented in SIMRCI-owned source.
  u8 HandleQuery(ScMessage *message, ScUnknown *source);
  u8 CreateRasterSource(u32 kind, u32 iid, void **out);
  EcValve **ValveSlot(u32 identifier);
  EcDemandAgentManipulator **AgentSlot(u32 identifier);
  // Original complete receiver count8,city12,neighbors16,ordinances20,monthly manipulator36 and agent tree52.
  u32 references;
  CtCity *city;
  CtNeighborsLayer *neighbors;
  CtOrdinanceLayer *ordinances;
  EcDemandValveEntry *valves;
  EcDemandManipulator *monthly;
  EcDemandAgentEntry *agents;
  CtAgentTree *agent_tree;
  // Original previous/current density56/60, then three published and three accumulating taxable totals64..84.
  EcByteMap *density, *current_density;
  u32 taxable[3], next_taxable[3];
};

// Distinct agent-specific receiver copies configured signed coefficients while retaining the matching valves.
// It shares the public manipulator contract, not an invented inheritance relation with the monthly manipulator.
class EcDemandAgentManipulator : public CtLayerManipulator {
public:
  EcDemandAgentManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  virtual ~EcDemandAgentManipulator();
  // Complete private AL effects; nominal bool/BYTE declarations remain unresolved.
  virtual u8 Init(EcDemand *layer);
  virtual u8 Shutdown();
  virtual u8 SetAgentType(u32 identifier);

protected:
  void ClearBindings();
  u32 references;
  EcDemand *layer;
  u32 identifier;
  EcDemandBinding *bindings;
};

// Original X-major DWORD allocation. Dimensions describe the coarse allocation rather than city coordinates.
class EcPackedGrid {
public:
  EcPackedGrid(u32 width, u32 height, const u32 *fill);
  virtual ~EcPackedGrid();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  u32 width, height;
  u32 **columns;
};

class CtCity;

// Recovered DWORD city-map base of the pollution owner. Interface discovery remains abstract;
// this native class is not an x86 layout or a replacement for a complete game object.
class EcDwordMap : public ScDwordMap {
public:
  EcDwordMap();
  u32 AddRef() override;
  u32 Release() override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  virtual bool Init(CtCity *city, const u32 *fill) override;
  virtual bool Shutdown() override;
  virtual u32 CellCountX() const override;
  virtual u32 CellCountZ() const override;
  virtual void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  virtual bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  virtual bool InBounds(u32 x, u32 z) const override;
  virtual bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  virtual void GetValue(u32 x, u32 z, u32 *value) const override;
  virtual void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u32 *value) override;
  virtual void SetValue(u32 x, u32 z, const u32 *value) override;
  virtual void SetAllCells(const u32 *value) override;
  virtual ~EcDwordMap();

protected:
  u32 references;   // Original primary receiver +0x04.
  bool initialized; // +0x08
  // Original primary-map receiver offsets +0x0C, +0x10, +0x14 and +0x18.
  u32 width, height, coordinate_shift;
  EcPackedGrid *published;
};

// Original signed-DWORD coarse allocation; full-cell stores preserve signed bits and whole-map fills repeat bytes.
class EcIntGrid {
public:
  EcIntGrid(u32 width, u32 height, const i32 *fill);
  virtual ~EcIntGrid();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  u32 width, height;
  i32 **columns;
};

// Signed accumulation map embedded twice at whole pollution owner +0x58 and +0x74.
// Director acquisition remains an explicit original dependency.
class EcIntMap : public ScCellMapBase {
public:
  EcIntMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  virtual bool Init(CtCity *city, const i32 *fill);
  virtual bool Shutdown();
  virtual void GetValue(u32 x, u32 z, i32 *value) const;
  virtual void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i32 *value);
  virtual void SetValue(u32 x, u32 z, const i32 *value);
  virtual void SetAllCells(const i32 *value);
  virtual ~EcIntMap();

protected:
  u32 references;
  bool initialized;
  u32 width, height, coordinate_shift;
  EcIntGrid *grid;
};

class EcPollution;

// Original64-byte SIMECO raster owner, constructorE2DD, with the common IID5DD raster fields and retained parent.
// Reuse the verified raster implementation; a common copied library base does not acquire a second economy prefix.
class EcMapSource : public TrRasterSource {
  friend class EcPollution;

public:
  EcMapSource(u32 kind, EcPollution *owner, u32 reserved);
  ~EcMapSource() override;
  bool GetColor(u32 x, u32 z, u32 *out) override = 0; // SIMECO table1001C0C0 keeps this entry pure.

protected:
  EcPollution *owner; // Original+60.
};

class EcAirSource final : public EcMapSource {
public:
  explicit EcAirSource(EcPollution *owner);
  bool GetColor(u32 x, u32 z, u32 *out) override;
};

class EcWaterSource final : public EcMapSource {
public:
  explicit EcWaterSource(EcPollution *owner);
  bool GetColor(u32 x, u32 z, u32 *out) override;
};

class EcGarbageSource final : public EcMapSource {
public:
  explicit EcGarbageSource(EcPollution *owner);
  bool GetColor(u32 x, u32 z, u32 *out) override;
};

class EcRadiationSource final : public EcMapSource {
public:
  explicit EcRadiationSource(EcPollution *owner);
  bool GetColor(u32 x, u32 z, u32 *out) override;
};

class EcValve;
class EcValveLayer;
class CtOrdinanceLayer;
class EcCommercial;
class EcIndustrial;

// SIMRCI commercial layer owns its city, demand layer and two separately retained valves.
class EcCommercial : public EcCommercialLayer, public EcCommandOwner, public CtLayer {
public:
  EcCommercial();
  virtual ~EcCommercial();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetDevelopmentCap() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Original monthly calculation's unused private return remains unresolved.
  void UpdateDemandCap();

private:
  // Original 1000ECCD returns raw AL; its private Boolean/BYTE spelling remains unresolved.
  u8 LoadTuning();
  CtCity *city;
  EcValveLayer *demand;
  EcValve *commercial, *connections;
};

// Industrial layer additionally retains the city ordinance service and its conservation effects.
class EcIndustrial : public EcIndustrialLayer, public EcCommandOwner, public CtLayer {
public:
  EcIndustrial();
  virtual ~EcIndustrial();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 GetDevelopmentCap() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Shutdown() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Original monthly calculation's unused private return remains unresolved.
  void UpdateDemandCap();

private:
  // Original 10015DC0 returns raw AL; its private Boolean/BYTE spelling remains unresolved.
  u8 LoadTuning();
  CtCity *city;
  EcValveLayer *demand;
  CtOrdinanceLayer *ordinances;
  EcValve *industrial, *connections;
};

// Each installed sector manipulator retains its actual owner; private Init/Shutdown return spellings remain unresolved.
class EcCommercialManipulator : public CtLayerManipulator, public EcCommandOwner {
public:
  EcCommercialManipulator();
  virtual ~EcCommercialManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  virtual u8 Init(EcCommercial *owner);
  virtual u8 Shutdown();

private:
  EcCommercial *owner;
};

class EcIndustrialManipulator : public CtLayerManipulator, public EcCommandOwner {
public:
  EcIndustrialManipulator();
  virtual ~EcIndustrialManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  virtual u8 Init(EcIndustrial *owner);
  virtual u8 Shutdown();

private:
  EcIndustrial *owner;
};

// Original signed-WORD coarse allocation; full-cell stores preserve signed bits and whole-map fills repeat bytes.
class EcShortGrid {
public:
  EcShortGrid(u32 width, u32 height, const i16 *fill);
  virtual ~EcShortGrid();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  u32 width, height;
  i16 **columns;
};

// Signed garbage-production map embedded at whole pollution owner +0x128.
// Director acquisition remains an explicit original dependency.
class EcShortMap : public ScCellMapBase {
public:
  EcShortMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  virtual bool Init(CtCity *city, const i16 *fill);
  virtual bool Shutdown();
  virtual void GetValue(u32 x, u32 z, i16 *value) const;
  virtual void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i16 *value);
  virtual void SetValue(u32 x, u32 z, const i16 *value);
  virtual void SetAllCells(const i16 *value);
  virtual ~EcShortMap();

protected:
  u32 references;
  bool initialized;
  u32 width, height, coordinate_shift;
  EcShortGrid *grid;
};

class CtCity;
class RsStream;

// Original unsigned-WORD map allocation. Native pointers replace x86 pointers without changing column ownership.
class EcWordGrid {
public:
  EcWordGrid(u32 width, u32 height, const u16 *fill);
  virtual ~EcWordGrid();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  u32 width, height;
  u16 **columns;
};

// Concrete unsigned-WORD city map embedded in the pollution receiver. DLL director acquisition
// remains an explicit dependency; the public map methods follow the original SDK contracts.
class EcWordMap : public ScCellMapBase {
public:
  EcWordMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 CellCountX() const override;
  virtual u32 CellCountZ() const override;
  virtual void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  virtual bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  virtual bool InBounds(u32 x, u32 z) const override;
  virtual bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  virtual bool SerialRead(RsStream *record) override;
  virtual bool SerialWrite(RsStream *record) const override;
  virtual bool Init(CtCity *city, const u16 *fill);
  virtual bool Shutdown();
  virtual void GetValue(u32 x, u32 z, u16 *value) const;
  virtual void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u16 *value);
  virtual void SetValue(u32 x, u32 z, const u16 *value);
  virtual void SetAllCells(const u16 *value);
  virtual ~EcWordMap();

protected:
  u32 references;
  bool initialized;
  EcWordGrid *grid;
};

class GxBuffer;
class GxPaletteView;
class CtCity;
class EcDemand;
class ScStringView;
class EcByteMap;

// SIMRCI copy of the original map-sprite base, retaining the shared IID5DD raster contract.
// The shared IID5DD prefix follows the installed slot order; private nominal declarations remain qualified.
class GxRciMapSprite : public GxRasterSource {
public:
  GxRciMapSprite();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(u32 id, u32 width, u32 height) override;
  u32 GetId() const override;
  bool SetId(u32 id) override;
  bool SetPalette(GxPaletteView *source) override;
  GxPaletteView *GetPalette() override;
  i32 GetBaseWidth() const override;
  i32 GetBaseHeight() const override;
  bool SetBaseSize(u32 width, u32 height) override;
  // Windows stores raw bytes; Linux bool spelling does not justify normalizing this state.
  u8 SetPostFilter(u8 enabled) override;
  // Native effects of private1001DAF2; the discarded nominal result declaration is undocumented.
  void SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) override;
  u8 IsPostFilter() const override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;
  // Folded debug entries return byte zero; original nominal Boolean/BYTE declarations remain unresolved.
  u8 DebugInstanceTag(ScStringView *out) override;
  u8 DebugClassTag(ScStringView *out) override;
  u8 DebugTypeTag(ScStringView *out) override;
  virtual bool GetColor(u32 x, u32 z, u32 *out);
  virtual bool DrawCells(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual bool DrawAnnotations(GxBuffer *destination, const ScRect &rectangle, ScRect *clip);
  virtual u8 SetDebugInstanceTag(const ScStringView &value);
  virtual u8 SetDebugClassTag(const ScStringView &value);
  virtual ~GxRciMapSprite();

protected:
  u32 references, kind;
  GxPaletteView *palette;
  i32 width, height;
  u8 enabled;
  // Actual four corner coordinates at original+28..56; constructor's three defining points produce the unit square.
  i32 corner_x[4], corner_z[4];
};

// The demand map keeps the real city and valve-layer identities until destruction.
class GxRciDemandMap : public GxRciMapSprite {
public:
  GxRciDemandMap(CtCity *city, EcDemand *layer);
  ~GxRciDemandMap() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  bool ready;
  EcDemand *layer;
  CtCity *city;
};

// SIMRCI zone-map raster retains the actual byte map and reads its shared converted zoning palette.
class GxRciZoneMap : public GxRciMapSprite {
public:
  explicit GxRciZoneMap(EcByteMap *map);
  ~GxRciZoneMap() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  EcByteMap *map;
};

class SpDefinition;

// SIMECO's building base: building, city-agent and occupant interfaces precede four recovered fields.
// Cleanup retains the actual direct occupant dependency and its private stream-state reconstruction gap.
class EcBuilding : public BdBuilding, public CtAgent, public EcOccupant {
public:
  EcBuilding();
  ~EcBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(RsKey *key) override;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  ScOccupant *AsISC3Occupant() override;
  CtAgent *AsISC3CityAgent() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  u32 TypeId() const override;
  u32 GetOccupancyState() override;
  void SetOccupancyState(u32 value) override;
  void SetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  void ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  bool GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  u32 GetGeomFlag() override;
  u32 GetMemUse() const override;
  GeAttrib *GetAttribData() override;
  GeAttrib *GetBaseAttribData() override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  u8 PowerRequired() override;
  bool SetName(const ScStringView &value) override;
  bool IsNameChangeable() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  u8 QueryResponse(ScMessage *message, UiQueryWindow *query) override;
  // Native effect projection of original virtual100; its unused private return remains unresolved.
  virtual void RefreshSprite();
  u32 occupancy_flags;
  CtAgentType *agent_type;
  GeAttrib *cached_attributes;
  SpDefinition *cached_sprite;
};

#endif
