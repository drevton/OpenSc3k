#ifndef SC3_CITY_PUBLIC_SERVICES_H
#define SC3_CITY_PUBLIC_SERVICES_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/ct_police.h"
#include "city/maps.h"
#include "city/oc_service_building.h"
#include "city/sp_service_map_sprite.h"
#include "common/api.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "sprites/api.h"

// SIMSERV's real station identities precede its independently linked 44-byte building base.
// Their remaining private stream and query operations stay explicit until their contracts are recovered.
class BdFireStation : public BdFireStationView, public BdUpkeepView, public CtService::OcBuilding {
public:
  BdFireStation();
  ~BdFireStation() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  u8 GetCoverageRadius() override;
  u8 GetMaxCoverage() override;
  u8 GetMinCoverage() override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  // Original private query helpers; byte/Boolean and count signedness remain qualified.
  u32 GetInfoStringCount();
  u8 GetInfoStrings(ScStringView *label, ScStringView *value, u32 index, bool *warning);

protected:
  // The stock station overrides these UI hooks; their nominal return spellings remain unresolved.
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  // Private stream carrier spelling is unresolved; these overrides retain the observed effects.
  bool ReadState(CtCity *city, RsStream *record) override;
  bool WriteState(CtCity *city, RsStream *record) override;

private:
  u8 coverage_radius, maximum_coverage, minimum_coverage;
  u32 optimal_upkeep, monthly_upkeep;
};

class BdPoliceStation : public BdPoliceStationView, public BdUpkeepView, public CtService::OcBuilding {
public:
  BdPoliceStation();
  ~BdPoliceStation() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  u8 GetCoverageRadius() override;
  u8 GetMaxCoverage() override;
  u8 GetMinCoverage() override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetInfoStringCount() override;
  u8 GetInfoStrings(ScStringView *label, ScStringView *value, u32 index, bool *warning) override;
  u32 GetDebugInfoStringCount() override;
  u8 GetDebugInfoStrings(ScStringView *label, ScStringView *value, u32 index) override;

protected:
  // The stock station overrides these UI hooks; their nominal return spellings remain unresolved.
  u8 QueryBasic(ScMessage *message, UiQueryWindow *query) override;
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  // Private stream carrier spelling is unresolved; these overrides retain the observed effects.
  bool ReadState(CtCity *city, RsStream *record) override;
  bool WriteState(CtCity *city, RsStream *record) override;

private:
  u8 coverage_radius, maximum_coverage, minimum_coverage;
  u32 optimal_upkeep, monthly_upkeep;
};

class BdJail : public BdJailView, public BdUpkeepView, public CtService::OcBuilding {
public:
  BdJail();
  ~BdJail() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(CtCity *city, OcPersistInfo *info) override;
  bool Save(CtCity *city, OcPersistInfo *info) override;
  BdBuilding *AsISC3Building() override;
  ScOccupant *AsISC3Occupant() override;
  u32 GetInmateCapacity() override;
  u32 GetInmateCount() override;
  u32 SetInmateCount(u32 count) override;
  i32 GetMonthlyUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  u32 GetDebugInfoStringCount() override;
  u8 GetDebugInfoStrings(ScStringView *label, ScStringView *value, u32 index) override;

protected:
  // The jail has its own tile query; its nominal return remains unresolved.
  u8 QueryTile(ScMessage *message, UiQuery *query) override;
  u8 QueryDetail(ScMessage *message, UiQuery *query) override;
  // Private stream carrier spelling is unresolved; these overrides retain the observed effects.
  bool ReadState(CtCity *city, RsStream *record) override;
  bool WriteState(CtCity *city, RsStream *record) override;

private:
  static u32 inmate_capacity, optimal_upkeep;
  u32 inmate_count, monthly_upkeep;
};

class CtZoneLayer;
class CtPoliceLayer;
class CtOrdinanceLayer;
class CtAgentTree;
class ScGraphInfoView;
class UiQuery;
class UiQueryWindow;
struct ApIniContext;

// Stable native entry for the original agent-type modifier maps. Amount is signed and radius is unsigned; the link
// supplies native traversal while retaining each cached value at a stable address.
struct CtCrimeMod {
  CtCrimeMod *next;
  u32 type;
  i8 amount;
  u8 radius;
};

// Own crime agent-type modifier entries. Entries retain stable addresses throughout their lifetime.
class CtCrimeMods {
public:
  CtCrimeMods();
  ~CtCrimeMods();
  // Keep entry ownership unique; callers copy actual modifier values rather than duplicating this owner.
  CtCrimeMods(const CtCrimeMods &) = delete;
  // Prevent two owners from destroying the same cached entries.
  CtCrimeMods &operator=(const CtCrimeMods &) = delete;
  void Clear();
  CtCrimeMod *Find(u32 type) const;
  CtCrimeMod *Get(u32 type);
  u32 Count() const;
  void Remove(u32 type);
  CtCrimeMod *first;
};

// Installed constructor5F9A and exact Linux cSC3CrimeLayer establish these actual base subobjects in this order.
// Construction and shutdown preserve real member lifetimes. Remaining simulation/initialization overrides keep the
// receiver abstract; recovered native storage is not an original binary-layout overlay.
class CtCrime : public CtSizedByteMap,
                public CtCrimeLayer,
                public CtLayer,
                public CtAgent,
                public CtChangeReceiver,
                public ScQueryInfoString,
                public SpMapSpriteFactory {
public:
  CtCrime();
  ~CtCrime() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetLayerType() override;
  bool Shutdown() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *source_city, const u8 *initial) override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool Simulate(i32 elapsed) override;
  bool Init(CtCity *source_city) override;
  bool Init(CtCity *source_city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *source_city, RsDatabaseSegment *segment) override;
  bool Init(CtCity *source_city, ScImporter *importer) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  u8 GetAverageValue() override;
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *first_text,
      ScStringView *second_text) override;

protected:
  // Actual7B4E byte-return body; its private nominal bool/BYTE declaration remains unresolved.
  u8 DoDailySim();
  // Actual7D35 change handler; private enum declaration and nominal bool/BYTE spelling remain unresolved.
  u8 OccupantChanged(u32 change, ScOccupant *occupant);
  // Mutation phases preserve ignored original calls; their private nominal returns remain unresolved.
  void UpdateHistoryObjects();
  void EndOfMonth();
  void EndOfCycle();
  static void RegisterAgentModifier(const ScString &key, const ScString &value, ApIniContext *owner);
  // Private query byte carriers retain unresolved nominal bool/BYTE spelling.
  u8 OnWindowCreate(ScMessage *message, ScUnknown *source);
  u8 DoQueryViews(ScMessage *message, UiQueryWindow *query);
  u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  u32 GetInfoStringCount();
  u8 GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 category, bool *warning);
  static u16 update_period;
  static u8 police_factor;
  static i16 curfew_effect, gambling_effect, sports_effect, watch_effect, corps_effect;
  static CtCrimeMods agent_modifiers; // Original global24968: definitions and inherited-type cache.
  bool bound;                         // Original52, independent of the sized-map initialization byte8.
  CtCity *city;                       // Original56, retained while bound.
  CtZoneLayer *zones;                 // Original60, retained city zone map.
  CtPoliceLayer *police;              // Original64, retained coverage map from city372.
  CtOrdinanceLayer *ordinances;       // Original68, retained city ordinance layer.
  u32 cells_per_tick, scan_x, scan_z; // Original72/76/80, initialized only after all member construction.
  CtSavedIterator iterator;           // Original84, with its plain state at88 and two serialization methods.
  CtAgentTree *agent_types;           // Original144, retained tree used for modifier ancestry lookup.
  CtSizedShortMap working;            // Original148, constructed with resolution shift1.
  CtCrimeMods modifiers;              // Original176 saved modifier snapshot compared before simulation.
  u32 sum, sample_count;              // Original188/192, accumulated crime and contributing cell counts.
  u8 average;                         // Original196, genuine byte result.
  ScGraphInfoView *graph;             // Original200, retained graph view from the actual module-local receiver.
  ScString number_format;             // Original204, real format string destroyed before the modifier owner.
};

class CtCrime;
class CtZoneLayer;

// Actual crime navigation-map source. The common map-sprite base retains its established owning Sp prefix.
class CtCrimeMapSprite : public CtService::SpMapSprite {
  friend class CtCrime;

public:
  CtCrimeMapSprite(CtCrime *crime, CtZoneLayer *zones);
  ~CtCrimeMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  CtCrime *crime;     // Original60, retained after the optional zone view.
  CtZoneLayer *zones; // Original64, optional zoning mask.
};

class CtOrdinanceLayer;
class CtPlumbingLayer;
class ScOccupantManager;
class UiQuery;
class UiQueryWindow;

// Exact Linux flammability view has three operations after ScUnknown; it is separate from the full map interface.
class CtFlammabilityLayer : public ScUnknown {
public:
  // Actual C5B2 returns byte1 after the read/lazy update; nominal bool/BYTE spelling remains unresolved.
  virtual u8 GetValue(u32 x, u32 z, u8 *value) = 0;
  virtual void SetValue(u32 x, u32 z, const u8 *value) = 0;
  virtual u8 GetAverageValue() = 0;
};

// Constructor C3E6 and exact Linux4D544 prove the plain map plus these five actual interface subobjects.
// Unrecovered overrides remain abstract; no guessed receiver behavior enables the factory prematurely.
class CtFlammability : public CtByteMap,
                       public CtFlammabilityLayer,
                       public CtLayer,
                       public CtAgent,
                       public SpMapSpriteFactory,
                       public ScQueryInfoString {
public:
  CtFlammability();
  ~CtFlammability() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Shutdown() override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  bool Simulate(i32 elapsed) override;
  u8 GetValue(u32 x, u32 z, u8 *value) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  u32 GetLayerType() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  u8 GetAverageValue() override;
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *first_text,
      ScStringView *second_text) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;

protected:
  // Private query helpers preserve the observed byte carrier while nominal bool/BYTE spelling remains unresolved.
  u8 DoQueryViews(ScMessage *message, UiQueryWindow *query);
  u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  u32 GetInfoStringCount();
  u8 GetInfoStrings(u32 x, u32 z, ScStringView *label, ScStringView *value, u32 category, bool *warning);
  // The private nominal byte/Boolean return remains unresolved; C608 supplies a raw byte value.
  u8 CalculateValue(u32 x, u32 z);
  // The daily scan returns AL0/1; its private source return spelling remains unresolved.
  u8 DoDailySim();
  static u8 leaf_effect, detector_effect, water_effect;
  CtCity *city;                   // Original36.
  CtOrdinanceLayer *ordinances;   // Original40; city344 getter.
  CtPlumbingLayer *plumbing;      // Original44; city340 primary plumbing identity.
  ScOccupantManager *occupants;   // Original48; city284 getter.
  bool bound;                     // Original52.
  u32 references, cells_per_tick; // Original56/60, separate derived counter.
  u32 sum, sample_count;          // Original72/76.
  u8 average;                     // Original80.
  u32 scan_x, scan_z;             // Original64/68, written last by the constructor.
};

class CtFlammability;
class CtZoneLayer;

// Actual flammability raster retains its layer and optional zone view after the shared map-sprite base.
class CtFlammabilityMapSprite : public CtService::SpMapSprite {
  friend class CtFlammability;

public:
  CtFlammabilityMapSprite(CtFlammability *source, CtZoneLayer *zones);
  ~CtFlammabilityMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;

private:
  CtFlammability *flammability; // Original64; the native common base may have different layout.
  CtZoneLayer *zones;           // Original68, retained even though GetColor does not read it.
};

// Actual SIMSERV director derives from the common DLL director and registers the seven original service products.
class CtServiceDirector : public ScDllDirector {
public:
  CtServiceDirector();
  ~CtServiceDirector() override;
  u32 GetDirectorID() const override;
  bool AppRegister(ScRegistry *registry) override;
  // Native spelling of the module-specific common OnStart phase; the original method belongs to ScDllDirector.
  bool OnStart(ScCom *com) override;

private:
  static ScUnknown *CreateCrimeLayer();
  static ScUnknown *CreateFlammabilityLayer();
  static ScUnknown *CreatePoliceLayer();
  static ScUnknown *CreateFireLayer();
  static ScUnknown *CreateBuildingFireStation();
  static ScUnknown *CreateBuildingPoliceStation();
  static ScUnknown *CreateBuildingJail();
};

#endif
