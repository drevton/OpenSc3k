#ifndef SC3_NETWORK_NW_TRANSIT_PRODUCTS_H
#define SC3_NETWORK_NW_TRANSIT_PRODUCTS_H

#include <deque>

#include "city/api.h"
#include "city/simulation.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "common/thread.h"
#include "economy/api.h"
#include "geometry/ge_types.h"
#include "misc/api.h"
#include "occupants/animation_manager.h"
#include "terrain/tr_dirt_bag.h"

// SDK member declarations: Copyright (C) 2026 Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the canonical occupant, building and city-layer contracts.

class UiQuery;
class RsStream;

// SimTransit contains its own generic occupant/building implementation and module lifetime.
// Public SDK overrides and connected generic bodies are recovered here; private nominal contracts stay qualified.
class OcTransitOccupant : public ScOccupant {
public:
  OcTransitOccupant();
  virtual ~OcTransitOccupant();
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

  // Native effects of original288; the public SDK omits this original WORD setter.
  virtual void SetAltAnimCells(u16 altitude);

protected:
  // Actual private query slots292..304, with byte returns and separate original message/query arguments.
  virtual u8 QueryBasic(ScMessage *message, UiQueryWindow *query);
  virtual u8 QueryTile(ScMessage *message, UiQuery *query);
  virtual u8 QueryDetail(ScMessage *message, UiQuery *query);
  virtual u8 QueryResponse(ScMessage *message, UiQueryWindow *query);
  // Original private slot308 returns a Boolean byte; SDK supplies no nominal private declaration.
  virtual bool CreateSprite(u32 layer, SpSprite **out);
  virtual void DestroySprites(SpSprite **out);
  // Full native effects of private slots316/320; original nominal byte/Boolean returns remain unresolved.
  virtual bool ReadState(CtCity *city, RsStream *record);
  virtual bool WriteState(CtCity *city, RsStream *record);
  GeAttrib *attributes;
  SpSprite *sprite;
  u32 flags, position;
};

// Constructor14BE5 establishes building0, city-agent4 and occupant8, followed by four actual building fields.
class OcTransitBuilding : public BdBuilding, public CtAgent, public OcTransitOccupant {
public:
  OcTransitBuilding();
  ~OcTransitBuilding() override;
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
  bool ReadState(CtCity *city, RsStream *record) override;
  bool WriteState(CtCity *city, RsStream *record) override;
  u32 occupancy_flags;
  CtAgentType *agent_type;
  GeAttrib *cached_attributes;
  SpDefinition *cached_sprite;
};

// Actual station interface at44 supplies three borrowed owner views; its historical private name is unproved.
class NwStationViews : public ScUnknown {
public:
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual BdBuilding *AsISC3Building() = 0;
  virtual CtAgent *AsISC3CityAgent() = 0;
};

// Actual upkeep interface at48 owns optimal/current monthly costs and the byte funding percentage.
// The private setter return is unused; its effect-only declaration remains explicitly unresolved.
class NwUpkeep : public ScUnknown {
public:
  NwUpkeep();
  virtual ScOccupant *AsISC3Occupant() = 0;
  // This32-bit signed carrier follows the field's signed arithmetic; the private getter typedef remains unproved.
  virtual i32 GetOptimalMonthlyUpkeep();
  virtual void SetFundingPercentage(u8 percentage);

protected:
  i32 optimalMonthlyUpkeep, monthlyUpkeep;
  u8 fundingPercentage;
};

class NwStation : public OcTransitBuilding, public NwStationViews, public NwUpkeep {
public:
  NwStation();
  ~NwStation() override;
  using OcTransitBuilding::Init;
  bool Init(GeAttrib *value) override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  CtAgentType *AgentType() override;
  GeAttrib *GetBaseAttribData() override;
  u32 GetGeomFlag() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  ScOccupant *AsISC3Occupant() override;
  BdBuilding *AsISC3Building() override;
  CtAgent *AsISC3CityAgent() override;

protected:
  // Original primary104/108; private names and selector signedness remain qualified in evidence.
  virtual u32 QueryValueCount();
  virtual u8 QueryValue(ScStringView *label, ScStringView *out, u32 index, u8 *warning);
  // Installed primary slots112/116 are still undeclared: their private argument and return types are unproved.
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
};

class NwRailStation : public NwStation {
public:
  NwRailStation();
  ~NwRailStation() override;
  u32 GetGeomFlag() override;
};

class NwSubwayRailStation : public NwStation {
public:
  NwSubwayRailStation();
  ~NwSubwayRailStation() override;
  using NwStation::GetSpriteInst;
  using NwStation::Init;
  bool Init(GeAttrib *value) override;
  void Shutdown() override;
  u32 GetGeomFlag() override;
  SpSprite *GetSpriteInst(u32 layer) override;

protected:
  SpSprite *subwaySprite;
};

class NwSubwayStation : public NwStation {
public:
  NwSubwayStation();
  ~NwSubwayStation() override;
  using NwStation::GetSpriteInst;
  using NwStation::Init;
  bool Init(GeAttrib *value) override;
  void Shutdown() override;
  u32 GetGeomFlag() override;
  SpSprite *GetSpriteInst(u32 layer) override;

protected:
  SpSprite *subwaySprite;
};

class NwBusStop : public NwStation {
public:
  NwBusStop();
  ~NwBusStop() override;
  u32 GetGeomFlag() override;
};

// Actual secondary factory interface at layer4; unrecovered nominal names/types remain explicit effect carriers.
class NwTransitAgentFactory : public ScUnknown {
public:
  virtual bool CreateAgent(u32 type, u32 iid, void **out) = 0;
  virtual u32 FactoryValue() = 0;
  virtual u8 FactoryOperation(u32 first, u32 second) = 0;
};

// Embedded56-byte rectangular scan state. The original packed-coordinate vector occupies32..43.
struct NwRouteOptions {
  NwRouteOptions();
  void SetBounds(const GeCellRect *bounds);
  void Prepare(u8 shuffle);
  u8 Next(u32 *x, u32 *z);
  u8 state;
  u32 mode;
  u8 dirty;
  u32 x0, z0, x1, z1, count;
  ScArray<u16> entries;
  u32 index, position, stride;
};

struct NwUpkeepLink {
  NwUpkeepLink *next, *previous;
  NwUpkeep *value;
};

// Original one-word upkeep list owns its sentinel; the independent counters belong to the layer.
struct NwUpkeepList {
  NwUpkeepList();
  ~NwUpkeepList();
  void Clear();
  NwUpkeepLink *head;
};

// Original x-major grids own separately allocated columns; only the actual destructor is virtual.
class NwByteGrid {
public:
  NwByteGrid(u32 width, u32 height, const u8 *initial);
  virtual ~NwByteGrid();
  void Fill(const u8 *value);
  u8 Load(RsStream *stream);
  u8 Save(RsStream *stream);
  u32 width, height;
  u8 **cells;
};

class NwWordGrid {
public:
  NwWordGrid(u32 width, u32 height, const u16 *initial);
  virtual ~NwWordGrid();
  u32 width, height;
  u16 **cells;
};

class NwBitGrid {
public:
  NwBitGrid(u32 width, u32 height, const u8 *initial);
  virtual ~NwBitGrid();
  // Used effects of private operations; their ignored nominal returns remain unresolved.
  void Fill(const u8 *value);
  void Set(u32 x, u32 z, const u8 *value);
  u8 Load(RsStream *stream);
  u8 Save(RsStream *stream);
  u32 width, height, wordsPerColumn;
  u32 **words;
};

// Original32-byte route aggregate passed to the layer's actual trip operation, not a native argument bundle.
struct NwTripData {
  ScOccupant *occupant;
  u32 x0, z0, x1, z1;
  u8 mode, value21;
  u32 value24;
  u8 value28, value29;
};

class EcValveLayer;
class EcWorkforceView;
class NwManager;
class CtNeighborsLayer;
class CtOrdinanceLayer;
class CtZoneLayer;
class EcBudgetLayer;
class EcDepartmentBudget;

// Module-local four-slot reference base. RemoveRef is distinct from the deleting Release operation.
class NwTransitRefCount {
public:
  NwTransitRefCount();
  virtual ~NwTransitRefCount();
  virtual u32 Release();
  virtual u32 AddRef();
  virtual u32 RemoveRef();

protected:
  u32 references;
};

// Original traffic graph producer: four Strings, scalar metadata and three proven DWORD deques of12/10/10 elements.
// Modern deque storage is native and does not overlay the original240-byte object.
class NwTrafficHistory : public NwTransitRefCount, public ScGraphInfoView {
public:
  explicit NwTrafficHistory(u32 limit);
  ~NwTrafficHistory() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  // Observed effects of slot104; its private nominal return remains unresolved.
  void SetAutoMaxFactors(u32 minimum, f32 factor) override;
  const ScStringView *GetSelectionLabel() const override;
  const ScStringView *GetCurrentValueLabel(ScGraphDataSet set) const override;
  const u32 *GetSelectionLabelID() const override;
  const u32 *GetValueLabelID() const override;
  u32 GetLineColor() const override;
  u32 GetMaxValue() const override;
  u8 GetMonthData(u32 *out) override;
  u8 GetMonthDataSet(u32 *out) const override;
  u32 GetMonthAverage() const override;
  u8 GetYearData(u32 *out) override;
  u8 GetYearDataSet(u32 *out) const override;
  u32 GetYearAverage() const override;
  u8 GetDecadeData(u32 *out) override;
  u8 GetDecadeDataSet(u32 *out) const override;
  u32 GetDecadeAverage() const override;
  u8 VerifyData() override;
  u8 SetSelectionLabel(const ScStringView &value) override;
  u8 SetCurrentValueLabel(ScGraphDataSet set, const ScStringView &value) override;
  u8 SetSelectionLabelID(const u32 &value) override;
  u8 SetValueLabelID(const u32 &value) override;
  u8 SetColor(u32 value) override;
  u8 SetMaxValue(u32 value) override;
  u8 SetAutoMax(u8 enabled) override;
  u8 SetMonthData(u32 value) override;
  u8 SetMonthDataSet(u32 *values) override;
  u8 SetYearData(u32 value) override;
  u8 SetYearDataSet(u32 *values) override;
  u8 SetDecadeData(u32 value) override;
  u8 SetDecadeDataSet(u32 *values) override;
  u8 SerialRead(CtCity *city, RsStream *record) override;
  u8 SerialWrite(CtCity *city, RsStream *record) const override;

protected:
  u32 RecomputeLimit();
  ScString name, label0, label1, label2;
  u32 key, group, value, limit;
  u8 autoScale;
  u32 minimum;
  f32 factor;
  std::deque<u32> series0, series1, series2;
};

class NwTransitLayer;

// Actual eight-byte statistics manipulator; its final Release does not delete the module-owned object.
class NwTrafficStatsManipulator : public CtLayerManipulator {
public:
  NwTrafficStatsManipulator();
  virtual ~NwTrafficStatsManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  u32 references;
};

// Actual twenty-byte weekly trip manipulator. Its four raw option bytes and scheduling phase are separate fields.
class NwTripManipulator : public CtLayerManipulator {
public:
  NwTripManipulator(u32 kind, u32 phase);
  virtual ~NwTripManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;

protected:
  u32 references, kind;
  u8 flag12, flag13, flag14, flag15;
  u32 phase;
};

// The installed constructor calls the same module-independent60-byte raster base used by the terrain source.
class NwTrafficRaster : public TrRasterSource {
  friend class NwTransitLayer;

public:
  NwTrafficRaster(CtCity *city, NwTransitLayer *layer);
  ~NwTrafficRaster() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

protected:
  u8 ready;
  NwTransitLayer *layer;
  CtCity *city;
};

// Actual shared module tuning operation; the private bool/BYTE nominal spelling remains unresolved.
u8 NwLoadTransitTunables();

// Actual traffic/city-layer primary and separate factory at4. Pending methods remain undefined link dependencies.
class NwTransitLayer : public NwTrafficLayer, public NwTransitAgentFactory {
public:
  NwTransitLayer();
  virtual ~NwTransitLayer();
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
  void GetVolume(u32 x, u32 z, u8 *volume, u8 mask) override;
  u8 IsTripAvailable(u32 x, u32 z) override;
  u8 RouteOccupantTrip(ScOccupant *occupant, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) override;
  i64 GetFareIncome() override;
  f64 GetTrafficRatio() override;
  u32 GetRailTotal() override;
  u32 GetRailUsage() override;
  void SetRoadFundingPercentage(u8 value) override;
  void SetRailFundingPercentage(u8 value) override;
  u8 Flag108IsOne() override;
  void BeginFlag108Phase() override;
  i32 GetRailFundingDeficit() override;
  // Original C68C refuses unsupported identities without releasing the constructor-retained owners.
  bool CreateAgent(u32 type, u32 iid, void **out) override;
  u32 FactoryValue() override;
  u8 FactoryOperation(u32 first, u32 second) override;
  // Recovered private tail operations keep descriptive names and qualified source declarations.
  u8 RouteCellTrip(u32 x, u32 z, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last) override;
  u32 GetMaxDimension() override;
  u8 GetRoadDistance() override;
  u8 HasBlockedTrip(u32 x0, u32 z0, u32 x1, u32 z1) override;
  void SetTripAvailability(u32 x, u32 z, u8 state) override;
  void FillTripAvailability(u32 x0, u32 z0, u32 x1, u32 z1, u8 state) override;
  void GetModeVolume(u32 x, u32 z, u32 *out, u8 mode) override;
  void SumVolume(u32 x0, u32 z0, u32 x1, u32 z1, u32 *total, u32 *count, u8 mask) override;
  u32 AverageVolume(u32 x0, u32 z0, u32 x1, u32 z1, u8 *out, u8 mask) override;
  u8 SetFlag17(u8 value) override;
  u8 GetFlag17() override;
  void ClearDensity() override;
  u32 GetCount220() override;
  u32 GetCount224() override;
  i64 GetValue256() override;
  f64 GetRailMultiplier() override;
  f64 GetRoadMultiplier() override;
  u8 EnumerateUpkeepOccupants(ScOccupant **out, u32 capacity, u32 *count) override;
  bool HasNetworkMode(u32 x, u32 z, u8 mode);
  u8 HasNetworkWithin(const u32 *point, i32 radius) override;

protected:
  friend class NwTrafficStatsManipulator;
  friend class NwTripManipulator;
  // The workforce ratio interface exposes observed return carriers; its nominal return remains unresolved.
  u8 SelectTripKind(u32 kind, u32 *out);
  u8 StampNetwork(u32 kind, ScOccupant *occupant);
  u8 ClearNetwork(u32 kind, ScOccupant *occupant);
  void RebuildCoverage(u32 x0, u32 z0, u32 x1, u32 z1);
  void StampCoverage(ScOccupant *occupant);
  void RemoveStationCoverage(ScOccupant *occupant);
  u8 QueryTrafficTile(ScMessage *message, UiQuery *query);
  u32 QueryTrafficValueCount();
  u8 QueryTrafficValue(u32 x, u32 z, ScStringView *label, ScStringView *out, u32 index, u8 *warning);
  u8 ProbeRouteCell(u8 requested, u32 x, u32 z, u8 *volume, u8 *mode, u16 *flags);
  u8 HandleQueryMessage(ScMessage *message, ScUnknown *source);
  void RecomputeStats();
  void UpdateCongestionState();
  void UpdateTransportBudgets();
  void UpdateRoadUpkeep();
  void UpdateRailUpkeep();
  void ApplyTunables();
  bool IsReachable(NwTripData *trip, u32 kind);
  // Original0755E candidate retains all route-search effects; the open batch still awaits verification.
  u8 SelectNextCell(NwTripData *trip, NwTripData *next, u32 cost, u8 first, u8 second, u8 last);
  void AddCongestion(u32 x, u32 z, i8 amount, u32 mode);
  u8 RouteTrip(NwTripData *trip, u32 kind, i32 amount, u8 flag, u8 first, u8 second, u8 last);
  ScLock *lock;
  u32 references;
  u8 initialized, flag17;
  u32 width, height;
  NwRouteOptions options;
  u8 drivingSide;
  u32 maxDimension;
  NwWordGrid *grid92;
  NwByteGrid *grid96, *grid100, *grid104, *grid108, *grid112, *grid116, *grid120;
  NwBitGrid *grid124, *grid128, *grid132, *grid136, *grid140;
  CtCity *city;
  ScOccupantManager *surface, *underground;
  EcValveLayer *valves;
  EcWorkforceView *trafficValve;
  CtOrdinanceLayer *ordinances;
  NwManager *network;
  ScGraphInfoView *history;
  u32 roadTotal, roadUsage;
  f32 trafficRatio;
  u32 railTotal, railUsage;
  u8 tripThresholdPercent, roadDistanceRes, roadDistanceCom, roadDistanceInd, roadDistance200, massTransitChance;
  f32 coefficient204, coefficient208, coefficient212;
  NwUpkeepList upkeep;
  u32 count220, count224;
  u8 roadFunding, railFunding;
  EcDepartmentBudget *roadBudget, *railBudget;
  EcBudgetLayer *budget;
  CtNeighborsLayer *neighbors;
  f32 railMultiplier, roadMultiplier, value256, fareIncome;
  u8 phase;
  u32 value268;
};

#endif
