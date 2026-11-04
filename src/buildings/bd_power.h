#ifndef SC3_BUILDINGS_BD_POWER_H
#define SC3_BUILDINGS_BD_POWER_H

#include <list>

#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "economy/ec_pollution.h"
#include "graphics/api.h"
#include "occupants/animation_manager.h"
#include "resources/resource.h"
#include "sprites/api.h"

// The installed Boolean grid owns one DWORD array per X column, with Z packed into 32-bit words.
class CtPowerBitGrid {
public:
  CtPowerBitGrid(u32 width, u32 height, const bool *initial);
  virtual ~CtPowerBitGrid();
  void Fill(const bool *value);
  void Set(u32 x, u32 z, const bool *value);
  // Original low-byte success carriers; the private Boolean/BYTE source declarations remain unresolved.
  u8 Read(RsStream *record);
  u8 Write(RsStream *record) const;
  u32 width, height, wordsPerColumn;
  u32 **words;
};

// SIMUTIL's cSC3CityCellMap<bool> primary, before the power layer's secondary interfaces.
// The class owns the original separate initialized flag and grid pointer, not a foreign memory overlay.
class CtPowerCellMap : public ScCellMap<bool> {
public:
  CtPowerCellMap();
  virtual ~CtPowerCellMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *out) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const bool *initial) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, bool *out) const override;
  void SetValue(u32 x, u32 z, const bool *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const bool *value) override;
  void SetAllCells(const bool *value) override;

protected:
  u32 references;
  bool initialized;
  CtPowerBitGrid *grid;
};

class RsStream;
class UiQuery;
class ScString;

ScString ScUtilityQueryText(u32 instance);

// Module-local localized-text receiver shared by utility building and layer queries.
class RsUtilityText {
public:
  RsUtilityText(u32 instance, u32 group);
  virtual ~RsUtilityText();
  ScString GetText();

private:
  ScStringView *text;
  RsKey key;
};

// Module-local SIMUTIL occupant base at building+8; complete installed vtable parity remains unverified.
class OcUtilityOccupant : public ScOccupant {
public:
  OcUtilityOccupant();
  virtual ~OcUtilityOccupant();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *sender) override;
  bool Init(RsKey *key) override;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  void Shutdown() override;
  bool IsInCity() const override;
  bool TestIsInCityFlag(u32 mask) const override;
  u32 GetIsInCityFlag() const override;
  void SetIsInCityFlag(u32 mask) override;
  void ClearIsInCityFlag(u32 mask) override;
  bool IsGeomFlag(u32 mask) override;
  u8 SetGeomFlag(u32 mask) override;
  u8 ClearGeomFlag(u32 mask) override;
  u32 GetGeomFlag() override;
  u32 GetOccLayerFlag() override;
  void SetOccLayerFlag(u32 mask) override;
  u8 MajorType() const override;
  u8 SubTypeInfo() const override;
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
  bool GetSubSystemData(u32 id, void **out) override;
  u32 TypeId() const override;
  CtAgentType *CityAgentType() override;
  bool CreateQueryDialog(UiQueryWindow **out) override;
  u32 GetSoundObjectId(i32 index) override;
  SpDefinition *GetSpriteAttrib() override;
  SpDefinition *GetSpriteAttrib(u32 layer) override;
  SpSprite *GetSpriteInst() override;
  SpSprite *GetSpriteInst(u32 layer) override;
  u32 GetDefaultLayerFlag() const override;
  u8 SetDefaultLayerFlag(u32 mask) override;
  void GetOrientation(u16 *rotation, u16 *pitch, u16 *roll) const override;
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
  u32 SizeInAnimCellsX() const override;
  u32 SizeInAnimCellsZ() const override;
  u32 SizeInAnimCellsY() const override;
  void SetLocation(const CtCoord &value) override;
  void SetLocationInCellCoords(u32 x, u32 z) override;
  void SetAlt(u8 altitude) override;
  bool GetMarkBit() const override;
  void SetMarkBit(bool value) override;
  i32 Tick(u32 elapsed) override;
  void SetSubTypeInfo(u8 value) override;
  void SetOrientation(u16 rotation, u16 pitch, u16 roll) override;
  u8 SetRotationFlag(u32 rotation) override;
  u32 GetMemUse() const override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  virtual void SetAltAnimCells(u16 value);

protected:
  virtual u8 QueryBasic(ScMessage *message, UiQueryWindow *query);
  virtual u8 QueryTile(ScMessage *message, UiQuery *query);
  virtual u8 QueryDetail(ScMessage *message, UiQuery *query);
  virtual u8 QueryResponse(ScMessage *message, UiQueryWindow *query);
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  // Private nominal Boolean/BYTE returns remain unresolved; these expose the observed predicates.
  virtual bool ReadState(CtCity *city, RsStream *record);
  virtual bool WriteState(CtCity *city, RsStream *record);
  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

// SIMUTIL building primary, city-agent and occupant views are at offsets0, 4 and8 on x86.
// Query, persistence and sprite-refresh methods are reconstructed; complete installed vtable parity remains unverified.
class OcUtilityBuilding : public BdBuilding, public CtAgent, public OcUtilityOccupant {
public:
  OcUtilityBuilding();
  ~OcUtilityBuilding() override;
  using OcUtilityOccupant::Init;
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
  bool GetOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
  void ClearOccupancyStateFlag(GeBuildingLayer::GeOccupancyState mask) override;
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
  // Installed private slot100; the original ignored nominal return remains unresolved.
  virtual void OnOccupancyStateChanged();
  bool ReadState(CtCity *city, RsStream *record) override;
  bool WriteState(CtCity *city, RsStream *record) override;
  u32 occupancyFlags;
  CtAgentType *agentType;
  GeAttrib *cachedAttributes;
  SpDefinition *cachedSprite;
};

// SIMUTIL's linked cSC3BuildingStaticAnim adds behavior but no fields to the utility building.
class GeUtilityAnimatedBuilding : public OcUtilityBuilding {
public:
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
};

class WnResources;
class ScLanguageManager;
class ApGameInterface;
class ScMessageServer;

// SIMUTIL's module director owns the original class registrations; product factories remain direct dependencies.
class ScSimutilDirector : public ScDllDirector {
public:
  ScSimutilDirector();
  ~ScSimutilDirector() override;
  u32 GetDirectorID() const override;
  bool OnStart(ScCom *com) override;
  bool AppRegister(ScRegistry *registry) override;
};

ScDirector *ScGetSimutilDirector();
ScFramework *ScGetSimutilFramework();
ApGameInterface *ScGetSimutilApplication();
CtCity *ScGetSimutilCity();
OcAttribCache *ScGetSimutilAttribCache();
RsManager *ScGetSimutilResourceManager();
WnResources *ScGetSimutilWindowResources();
ScLanguageManager *ScGetSimutilLanguageManager();
ScMessageServer *ScGetSimutilMessageServer();

class ScString;
class ScGraphInfoView;
class CtPowerSimulation;
class CtZoneLayer;
class CtNeighborsLayer;
class CtAgentTree;
class GeBuildingLayer;
class EcBudgetLayer;
class ScOccupantManager;
class BdPowerPlant;
class ScMessageServer;
class UiQuery;
struct GeCellRect;

// Original power-plant list payload; the list owns the retained plant interface.
struct CtPowerPlantNode {
  u32 region;
  BdPowerPlant *plant;
};

// Original neighbor-link payload; the last word is unwritten on insertion in the installed binary.
struct CtPowerNeighborLink {
  u32 x, z;
  i32 neighbor;
  u32 unknown;
};

// Two installed DWORDs hold each neighbor deal quantity; only the low word is changed by the deal setters.
struct CtPowerDealValue {
  u32 low, high;
};

// SIMUTIL's linked map-sprite base, including its cell rendering and annotation dispatch.
class SpUtilityMapSprite : public GxRasterSource {
public:
  SpUtilityMapSprite();
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
  u8 SetPostFilter(u8 enabled) override;
  void SetDestinationCoords(i32 x0, i32 z0, i32 x1, i32 z1, i32 x3, i32 z3) override;
  u8 IsPostFilter() const override;
  bool Draw(GxBuffer *target, const ScRect &rectangle, ScRect *clip) override;
  u8 DebugInstanceTag(ScStringView *out) override;
  u8 DebugClassTag(ScStringView *out) override;
  u8 DebugTypeTag(ScStringView *out) override;
  virtual bool GetColor(u32 x, u32 z, u32 *out);
  virtual bool DrawCells(GxBuffer *target, const ScRect &rectangle, ScRect *clip);
  virtual bool DrawAnnotations(GxBuffer *target, const ScRect &rectangle, ScRect *clip);
  virtual u8 SetDebugInstanceTag(const ScStringView &value);
  virtual u8 SetDebugClassTag(const ScStringView &value);
  virtual ~SpUtilityMapSprite();

protected:
  u32 references, kind;
  GxPaletteView *palette;
  i32 width, height;
  u8 postFilter;
  i32 cornerX[4], cornerZ[4];
};

// Power raster retains the city, layer and zone independently in the original order.
class CtPowerMapSprite : public SpUtilityMapSprite {
public:
  CtPowerMapSprite(CtCity *city, CtPowerSimulation *layer, CtZoneLayer *zones);
  ~CtPowerMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  friend class CtPowerSimulation;
  CtCity *city;
  CtPowerSimulation *layer;
  CtZoneLayer *zones;
};

// Original daily power manipulator retains only its own count and borrows the owning layer.
class CtPowerManipulator : public CtLayerManipulator {
public:
  explicit CtPowerManipulator(CtPowerSimulation *layer);
  virtual ~CtPowerManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

private:
  u32 references;
  CtPowerSimulation *layer;
};

// SIMUTIL power-layer receiver: original map primary and layer, power, sprite-factory and change views.
// Some state ranges and full foreign-vtable parity remain unresolved.
class CtPowerSimulation : public CtPowerCellMap,
                          public CtLayer,
                          public CtPowerLayer,
                          public SpMapSpriteFactory,
                          public CtChangeReceiver {
public:
  CtPowerSimulation();
  ~CtPowerSimulation() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *out) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  using CtPowerCellMap::Init;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, const bool *initial) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool StaticInit(CtDefinition *definition) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, bool *out) const override;
  void SetValue(u32 x, u32 z, const bool *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const bool *value) override;
  void SetAllCells(const bool *value) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  u32 GetLayerType() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u32 GetTotalAnnualConsumption() override;
  u32 GetAnnualConsumptionOfPowerType(u32 type) override;
  u32 GetTotalCityPowerUsed() override;
  u32 GetTotalCityPowerProduced() override;
  u32 GetTotalPowerNeededByTheCity() override;
  u32 GetNumberOfUnpoweredConsumers() override;
  u32 GetNumberOfPoweredConsumers() override;
  u32 GetNumberOfAgingPowerPlants() override;
  u32 GetNumberOfAgingNuclearPlants() override;
  void UpdateLayer() override;
  u8 GetPlantCapacity(u32 type, u32 *capacity) override;
  u32 GetMapSpriteCount() override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  static void RegisterPlantCapacity(const ScString &key, const ScString &value, void *context);
  bool StaticShutdown() override;
  // The original result is discarded by Apply; its source return type is unresolved.
  void UpdateUsageHistory();
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  // Linux identifies this count method; its nominal integer return declaration remains unresolved.
  u32 GetInfoStringCount();
  u8 GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 index);
  u8 MakeBuyingDealWithNeighbor(i32 neighbor, u32 amount);
  u8 MakeSellingDealWithNeighbor(i32 neighbor, u32 amount);
  // Original private returns are ignored; their nominal source declarations remain unresolved.
  void InitInfoAboutNeighborDeals();
  void ExamineNeighborDeals();
  void UpdatePowerGrid(u32 x, u32 z);
  void MarkPowerPlantsAsVisited(u32 used, u32 capacity);
  // Private effect projections: their nominal source return declarations are unresolved.
  void ResetWireBit(u32 x, u32 z);
  void SetWireBit(u32 x, u32 z);
  u8 IsWired(u32 x, u32 z) const;
  void SetMarkBit(u32 x, u32 z);
  void ResetMarkBit(u32 x, u32 z);
  u8 IsMarked(u32 x, u32 z) const;
  void ResetMarkBits(const GeCellRect &bounds);
  void ResetWireBits(const GeCellRect &bounds);
  void SetWireBits(const GeCellRect &bounds);
  void ResetAllMarkBits(u32 first, u32 end);
  u8 SetsWireBits(ScOccupant *occupant);
  void UpdateWireBit(u32 x, u32 z);
  void UpdateWireBits(const GeCellRect &bounds);
  void ExpandRectToIncludeNeighborhood(GeCellRect *bounds) const;
  void GetNeighborhoodOfPoint(u32 x, u32 z, GeCellRect *bounds) const;
  // Linux identifies the two-coordinate edge predicate; Boolean/BYTE source spelling remains unresolved.
  u8 IsOnTheEdge(u32 x, u32 z);
  u8 DetermineNeighbor(u32 x, u32 z) const;

protected:
  // The accounting slots receive the adjusted power view at complete+20; unknown ranges stay explicit.
  std::list<CtPowerPlantNode> plants;
  u8 initialized;
  u32 width, wordsPerRow;
  u32 lastX, lastZ;
  CtCity *city;
  GeBuildingLayer *buildings;
  CtZoneLayer *zones;
  CtNeighborsLayer *neighbors;
  EcBudgetLayer *budget;
  ScOccupantManager *occupants;
  u32 unrecovered80;
  CtLayerManipulator *manipulator;
  u32 *markBits;
  u32 *wireBits;
  u32 zoneSender, occupantSender, bitWordCount;
  u8 unrecovered108[4];
  u32 powerProduced, powerUsed;
  u32 currentUsage;
  u8 conservationOn, stairwellOn, unrecovered126Power[2];
  ScMessageServer *messageServer;
  CtAgentTree *agentTree;
  std::list<CtPowerNeighborLink> neighborLinks;
  CtPowerDealValue buyingDeals[4], sellingDeals[4];
  i32 consumptionHistory[12][10], historyCycle, annualConsumption[10];
  ScGraphInfoView *usageHistory;
  u8 *consumerDemand;
  u32 consumerCount;
  u32 poweredConsumers, unpoweredConsumers, powerNeeded, agingPlants, agingNuclearPlants;
  u8 suppressCellLock, unrecovered761[3];
};

// SIMUTIL's power-plant primary receiver. The Linux reference identifies the three pure owner-view slots;
// concrete building subobjects, later private slots and the complete installed ABI remain unverified.
class BdPowerPlant : public ScUnknown {
public:
  BdPowerPlant();
  // The installed functions return a success byte; their nominal Boolean/BYTE spelling is unresolved.
  static u8 StaticInit();
  static u8 CitySpecificStaticInit(CtCity *city);
  static u8 CitySpecificStaticShutdown();
  bool QueryInterface(u32 iid, void **out) override;
  virtual void SetMaximumAge(u32 value);
  virtual u32 GetMaximumAge();
  virtual void SetDeclineAge(u32 value);
  virtual u32 GetDeclineAge();
  virtual u32 GetAge();
  virtual bool IsAging();
  virtual void Retire();
  virtual u8 GetRetired();
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual BdBuilding *AsISC3Building() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
  virtual u32 MaxCapacity();
  virtual u32 ActualCapacity();
  // The observed stores are known; the original private nominal return is not established.
  virtual void SetUsage(u32 value);
  virtual u32 Usage();
  // The original Update's machine return varies by branch; its nominal declaration remains unresolved.
  virtual void Update();
  virtual u32 PowerSourceType() = 0;
  virtual ~BdPowerPlant();
  virtual bool HasBirthdate() const;
  virtual bool GetBirthdate(CtDateInterface *out) const;
  virtual bool SetBirthdate(const CtDateInterface &value);
  // The derived overrides also implement the SDK occupant's bool persistence methods.
  virtual bool Init(CtCity *city, OcPersistInfo *info);
  virtual bool Save(CtCity *city, OcPersistInfo *info);
  // Private returns are effect-only or observed low-byte carriers pending original declarations.
  virtual u8 DecideWhetherToExplode();
  virtual void Explode();
  virtual void BecomeDefunctDueToAge();
  virtual void DetermineActualCapacity();
  virtual u32 CalculateCapacityBasedOnAge(u32 value);
  virtual void DecideLifeSpanAndDeclineAge(u32 maximum, u32 decline, f32 variation);

protected:
  BdBuilding *GetCorrespondingBuildingRubble();
  // Linux identifies this count method; its nominal integer return declaration remains unresolved.
  u32 GetInfoStringCount();
  // Installed; private source name and nominal return remain unresolved.
  u8 GetPowerQueryRow(ScStringView *label, ScStringView *value, u32 index, u8 *warning);
  u32 usage, output, capacity;
  u16 overwork;
  CtDate constructionDate;
  u32 maximumAge, declineAge;
  u8 retired;
};

// The installed static building contains the actual building receiver at+40; complete foreign vtable parity remains open.
class BdPowerBuilding : public BdPowerPlant, public OcUtilityBuilding {
public:
  BdPowerBuilding();
  ~BdPowerBuilding() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(GeAttrib *attributes) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  u32 GetGeomFlag() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
};

// The animated family has the same verified primary and adjusted building views but distinct installed vtables.
class BdPowerBuildingAnimated : public BdPowerPlant, public GeUtilityAnimatedBuilding {
public:
  BdPowerBuildingAnimated();
  ~BdPowerBuildingAnimated() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(GeAttrib *attributes) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  u32 GetGeomFlag() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;
  bool HasBirthdate() const override;
  bool GetBirthdate(CtDateInterface *out) const override;
  bool SetBirthdate(const CtDateInterface &value) override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
};

// Source-type returns expose the original 32-bit carrier; the private nominal type remains unproved.
class BdCoalPlant : public BdPowerBuildingAnimated {
public:
  BdCoalPlant();
  ~BdCoalPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
};

class BdFusionPlant : public BdPowerBuildingAnimated {
public:
  BdFusionPlant();
  ~BdFusionPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
};

class BdGasPlant : public BdPowerBuildingAnimated {
public:
  BdGasPlant();
  ~BdGasPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
};

class BdMicrowavePlant : public BdPowerBuilding {
public:
  BdMicrowavePlant();
  ~BdMicrowavePlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
};

class BdNuclearPlant : public BdPowerBuildingAnimated {
public:
  BdNuclearPlant();
  ~BdNuclearPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
  void Explode() override;
};

class BdOilPlant : public BdPowerBuildingAnimated {
public:
  BdOilPlant();
  ~BdOilPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
};

class BdSolarPlant : public BdPowerBuilding {
public:
  BdSolarPlant();
  ~BdSolarPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
};

class BdWastePowerPlant : public BdPowerBuildingAnimated, public EcCombustion {
public:
  BdWastePowerPlant();
  ~BdWastePowerPlant() override;
  static void LoadTunableParameters();
  using BdPowerBuildingAnimated::Init;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void SetMaximumAge(u32 value) override;
  u32 GetMaximumAge() override;
  void SetDeclineAge(u32 value) override;
  u32 GetDeclineAge() override;
  u32 GetAge() override;
  bool IsAging() override;
  void Retire() override;
  u8 GetRetired() override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *GetOccupant() override;
  u32 GetProduction() override;
  u32 GetCapacity() override;
  u32 Consume(u32 amount) override;
  bool Simulate(i32 phase) override;
  i32 MinimumExecutionInterval() override;
  u32 PowerSourceType() override;

protected:
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;

private:
  // Weekly capacity calculation uses the traffic-layer trip request exposed by the shared interface.
  void DoWeeklySim();
  void DoMonthlySim();
  u32 dailyGarbageCapacity, garbageConsumed, previousMonthGarbageConsumed;
};

class BdWindPlant : public BdPowerBuildingAnimated {
public:
  BdWindPlant();
  ~BdWindPlant() override;
  static void LoadTunableParameters();
  u32 PowerSourceType() override;
  void DetermineActualCapacity() override;
};

#endif
