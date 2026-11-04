#ifndef SC3_CITY_CT_POLICE_H
#define SC3_CITY_CT_POLICE_H

#include "city/api.h"
#include "city/ct_graph_info.h"
#include "city/ct_layer.h"
#include "city/maps.h"
#include "city/sc_random.h"
#include "city/simulation.h"
#include "city/sp_service_map_sprite.h"
#include "common/api.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "sprites/api.h"

class CtCity;
class CtZoneLayer;
class CtCrimeLayer;
class CtFire;
class CtPolice;

// Actual primary manipulator and separate SIMSERV reference base; simulation Apply remains an explicit dependency.
class CtDailyFire : public CtLayerManipulator, public CtService::ScRefCount {
  friend class CtFire;

public:
  CtDailyFire();
  ~CtDailyFire() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Private Init/Shutdown have observed AL0/1; nominal Boolean/BYTE spelling remains unresolved.
  u8 Init(CtCity *city);
  u8 Shutdown();

private:
  ScRandom random;
  CtCity *city;                       // Original12, retained during Init.
  CtFire *layer;                      // Original16, primary view retained during Init.
  CtZoneLayer *zones;                 // Original20, retained during Init.
  u32 cells_per_tick, scan_x, scan_z; // Original24/32/36.
  u8 scan_state;                      // Original28.
  u16 update_period;                  // Original64.
};

// Police owns the same actual interface pair and retains an additional crime layer during simulation.
class CtDailyPolice : public CtLayerManipulator, public CtService::ScRefCount {
  friend class CtPolice;

public:
  CtDailyPolice();
  ~CtDailyPolice() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Private Init/Shutdown retain the observed AL0/1 while their nominal source return remains unresolved.
  u8 Init(CtCity *city);
  u8 Shutdown();

private:
  // Original FCEF/FD0B mutation effects; both private ignored nominal results remain unresolved.
  void IncarcerateCriminals(i32 count);
  void RehabilitateCriminals();
  ScRandom random;
  CtCity *city;                                             // Original12.
  CtPolice *layer;                                          // Original16.
  CtCrimeLayer *crime;                                      // Original20.
  CtZoneLayer *zones;                                       // Original24.
  u32 cells_per_tick, scan_x, scan_z;                       // Original56/64/68.
  u8 scan_state;                                            // Original60.
  u16 update_period;                                        // Original72.
  f32 daily_release_rate, conviction_rate, no_jail_penalty; // Original76/80/84.
};

class BdBuilding;
class ScOccupant;
class ScStringView;

// Actual fire-station service identity; the first two operations borrow its building and occupant subobjects.
class BdFireStationView : public ScUnknown {
public:
  virtual BdBuilding *AsISC3Building() = 0;
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual u8 GetCoverageRadius() = 0;
  virtual u8 GetMaxCoverage() = 0;
  virtual u8 GetMinCoverage() = 0;
};

// Actual police-station service identity has the same reached prefix but remains a separate queried interface.
class BdPoliceStationView : public ScUnknown {
public:
  virtual BdBuilding *AsISC3Building() = 0;
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual u8 GetCoverageRadius() = 0;
  virtual u8 GetMaxCoverage() = 0;
  virtual u8 GetMinCoverage() = 0;
  virtual u32 GetInfoStringCount() = 0;
  // Private source Boolean/byte spelling remains unresolved; mutable outputs use explicit pointers.
  virtual u8 GetInfoStrings(ScStringView *label, ScStringView *value, u32 index, bool *warning) = 0;
  virtual u32 GetDebugInfoStringCount() = 0;
  virtual u8 GetDebugInfoStrings(ScStringView *label, ScStringView *value, u32 index) = 0;
};

// IID A2EAB10D identifies the real upkeep view shared by service buildings.
class BdUpkeepView : public ScUnknown {
public:
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual i32 GetMonthlyUpkeep() = 0;
  virtual void SetFundingPercentage(u8 percentage) = 0;
};

// Actual jail service identity, including the borrowed owner views and inmate state operations.
class BdJailView : public ScUnknown {
public:
  virtual BdBuilding *AsISC3Building() = 0;
  virtual ScOccupant *AsISC3Occupant() = 0;
  virtual u32 GetInmateCapacity() = 0;
  virtual u32 GetInmateCount() = 0;
  // The original consumed stores are proven; the source nominal setter result remains unresolved.
  virtual u32 SetInmateCount(u32 count) = 0;
  virtual u32 GetDebugInfoStringCount() = 0;
  virtual u8 GetDebugInfoStrings(ScStringView *label, ScStringView *value, u32 index) = 0;
};

struct CtJailLink {
  CtJailLink *next;
  BdJailView *jail;
};

// Actual funded-building list payload; node allocation is native and identity ownership remains with the view.
struct CtServiceFundingLink {
  CtServiceFundingLink *next;
  BdUpkeepView *upkeep;
};

// Stable native nodes replace only the original list allocation; station identities retain their own actual lifetime.
struct CtFireStationLink {
  CtFireStationLink *next;
  BdFireStationView *station;
};

struct CtPoliceStationLink {
  CtPoliceStationLink *next;
  BdPoliceStationView *station;
};

// The recovered dispatch coordinates participate in original list traversal; other dispatch state remains unresolved.
struct CtServiceDispatchLink {
  CtServiceDispatchLink *previous, *next;
  ScOccupant *vehicle;
  u32 x, z;
  BdBuilding *marker;
};

// These owners replace only the original list nodes. Payload references are released by the service Shutdown methods.
struct CtFireStationList {
  CtFireStationLink *first = nullptr;
  ~CtFireStationList();
  void Clear();
};

struct CtPoliceStationList {
  CtPoliceStationLink *first = nullptr;
  ~CtPoliceStationList();
  void Clear();
};

struct CtServiceFundingList {
  CtServiceFundingLink *first = nullptr;
  ~CtServiceFundingList();
  void Clear();
};

struct CtServiceDispatchList {
  CtServiceDispatchLink *first = nullptr, *last = nullptr;
  ~CtServiceDispatchList();
  void Clear();
};

struct CtJailList {
  CtJailLink *first = nullptr;
  ~CtJailList();
  void Clear();
};

class CtFire;
class CtZoneLayer;

// Actual service raster retains its layer and optional zone view while drawing fire-station annotations.
class CtFireMapSprite : public CtService::SpMapSprite {
  friend class CtFire;

public:
  CtFireMapSprite(CtFire *source, CtZoneLayer *zones);
  ~CtFireMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;

private:
  CtFire *layer;      // Original60, retained after the optional zone view.
  CtZoneLayer *zones; // Original64.
};

class CtPolice;
class CtZoneLayer;

// Actual service raster retains its layer and optional zone view while drawing police-station annotations.
class CtPoliceMapSprite : public CtService::SpMapSprite {
  friend class CtPolice;

public:
  CtPoliceMapSprite(CtPolice *source, CtZoneLayer *zones);
  ~CtPoliceMapSprite() override;
  bool GetColor(u32 x, u32 z, u32 *out) override;
  bool Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *clip) override;

private:
  CtPolice *layer;    // Original60, retained after the optional zone view.
  CtZoneLayer *zones; // Original64.
};

// Installed 9B53 and exact Linux45C24 establish these map, fire, city, change, string and factory bases.
// The service factory constructs this receiver; private return spellings and complete runtime validation remain open.
class CtFlammabilityLayer;
class EcBudgetLayer;
class EcDepartmentBudget;
class CtDailyFire;
class CtAgentTree;
class CtStartSimLayer;
class OcAnimManager;
struct GeCellRect;
class UiQuery;
class UiQueryWindow;

class CtFire : public CtSizedByteMap,
               public CtFireLayer,
               public CtLayer,
               public CtChangeReceiver,
               public ScQueryInfoString,
               public SpMapSpriteFactory {
  // The serialized fire-layer group remains distinct from its COM class identity.
  static constexpr u32 kCtFireRecordGroup = 0xa0f42214;

  friend class CtDailyFire;
  friend class CtFireMapSprite;

public:
  CtFire();
  ~CtFire() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetLayerType() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *first_text,
      ScStringView *second_text) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  u32 GetTotalCityFireUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  bool IsOnStrike() override;
  u32 ChanceOfStrike() override;
  bool GetStrikingBuildings(ScOccupant **out, u32 capacity, u32 *count) override;
  bool DispatchCrew(u32 x, u32 z) override;
  bool RevokeDispatch(u32 x, u32 z) override;
  bool RevokeAllDispatches() override;
  u32 GetAverageCoverage() override;
  u32 GetPercentCoverage() override;
  void EndStrike() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u8 *initial) override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;
  void SetAllCells(const u8 *value) override;

protected:
  static u16 update_period;
  static u8 dispatch_radius, dispatch_effect, min_funding, max_flammability, strike_efficiency;
  static u32 max_strike_duration;
  // Complete map-update effects; the private nominal return remains unresolved.
  void UpdateMaps(const GeCellRect &rect, u8 radius, i16 maximum, i16 minimum);
  // The ignored private nominal return is unresolved; preserve the real separate coordinate outputs.
  void SelectDispatchOrigin(u32 *x, u32 *z);
  // The insertion/removal handler preserves the observed byte carrier; its private nominal return is unresolved.
  u8 HandleOccupantChange(i32 change, ScOccupant *occupant);
  // Original month-end carrier is observed as AL1; its private bool/BYTE spelling remains unresolved.
  u8 EndOfMonth();
  // PC folds these actual private hooks; their nominal bool/BYTE result remains unresolved.
  u8 DoQueryViews(ScMessage *message, UiQueryWindow *query);
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  CtFireStationList stations;                            // Original list at48.
  CtServiceFundingList funded_buildings;                 // Original list at52.
  CtSizedIntMap working;                                 // Original64, resolution shift2.
  CtServiceDispatchList dispatches;                      // Original list at100.
  ScRandom random;                                       // Original time-seeded service random state.
  ScLock station_lock;                                   // Original144.
  void *hazard_identity;                                 // Original116, external QueryInterface publication slot.
  u8 funding_percentage;                                 // Original184.
  u32 coverage_samples, coverage_sum, uncovered_samples; // Original204/208/212.
  bool bound;
  CtAgentTree *agent_tree;
  CtStartSimLayer *start_sim;
  OcAnimManager *animations;
  CtCity *city;                   // Original92.
  CtDailyFire *manipulator;       // Original60, retained after creation.
  ScOccupantManager *occupants;   // Original108, retained surface manager.
  EcBudgetLayer *budget;          // Original192, retained city budget layer.
  EcDepartmentBudget *department; // Original188, acquired during simulation begin.
  // Recovered service state; this native layout is not an overlay of the installed receiver.
  u32 upkeep;                             // Original180.
  bool on_strike;                         // Original196.
  u32 strike_event;                       // Original200, posted when a strike ends.
  u32 average_coverage, percent_coverage; // Original216/220.
};

// Installed D7B8 and exact Linux4FE14 establish these map, police, city, change, string and factory bases.
// The service factory constructs this receiver; private return spellings and complete runtime validation remain open.
class CtCrimeLayer;
class EcBudgetLayer;
class EcDepartmentBudget;
class CtDailyPolice;
class CtAgentTree;
class CtStartSimLayer;
class OcAnimManager;
struct GeCellRect;
class UiQuery;
class UiQueryWindow;

class CtPolice : public CtSizedByteMap,
                 public CtPoliceLayer,
                 public CtLayer,
                 public CtChangeReceiver,
                 public ScQueryInfoString,
                 public SpMapSpriteFactory {
  friend class CtDailyPolice;
  friend class CtPoliceMapSprite;

public:
  CtPolice();
  ~CtPolice() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetLayerType() override;
  bool DoMessage(ScMessage *message) override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  bool StaticInit(CtDefinition *definition) override;
  bool StaticShutdown() override;
  bool Init(CtCity *city) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool OccupantInserted(u32 sender, ScOccupant *occupant) override;
  bool OccupantRemoved(u32 sender, ScOccupant *occupant) override;
  bool OccupantMoved(u32 sender, ScOccupant *occupant, const CtCoord &position) override;
  bool OccupantChanged(u32 sender, ScOccupant *occupant, i32 change, void *data) override;
  bool CellChangedAll(u32 sender) override;
  bool CellChanged(u32 sender, u32 x, u32 z) override;
  bool CellChanged(u32 sender, u32 x0, u32 z0, u32 x1, u32 z1) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *first_text,
      ScStringView *second_text) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  u32 GetTotalCityPoliceUpkeep() override;
  void SetFundingPercentage(u8 percentage) override;
  bool IsOnStrike() override;
  u32 ChanceOfStrike() override;
  bool GetStrikingBuildings(ScOccupant **out, u32 capacity, u32 *count) override;
  bool DispatchCrew(u32 x, u32 z) override;
  bool RevokeDispatch(u32 x, u32 z) override;
  bool RevokeAllDispatches() override;
  void EndStrike() override;
  u32 GetAverageCoverage() override;
  u32 GetPercentCoverage() override;
  u32 GetTotalJailCapacity() override;
  u32 GetInmatePopulation() override;
  u32 GetCriminalsAtLarge() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u8 *initial) override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;
  void SetAllCells(const u8 *value) override;

protected:
  static u16 update_period;
  static u8 dispatch_radius, dispatch_effect, min_funding, max_crime, strike_efficiency;
  static u32 max_strike_duration;
  static f32 max_jail_overcrowding, no_jail_penalty, conviction_rate, daily_jail_release_rate;
  // Complete map-update effects; the private nominal return remains unresolved.
  void UpdateMaps(const GeCellRect &rect, u8 radius, i16 maximum, i16 minimum);
  // The ignored private nominal return is unresolved; preserve the real separate coordinate outputs.
  void SelectDispatchOrigin(u32 *x, u32 *z);
  // The insertion/removal handler preserves the observed byte carrier; its private nominal return is unresolved.
  u8 HandleOccupantChange(i32 change, ScOccupant *occupant);
  // Original month-end carrier is observed as AL1; its private bool/BYTE spelling remains unresolved.
  u8 EndOfMonth();
  // Complete F3BD redistribution effects; the ignored private nominal return remains unresolved.
  void RedistributeInmates();
  // PC folds these actual private hooks; their nominal bool/BYTE result remains unresolved.
  u8 DoQueryViews(ScMessage *message, UiQueryWindow *query);
  u8 DoStandardQueryViewInfo(ScMessage *message, UiQuery *query);
  u8 DoDebugQueryViewInfo(ScMessage *message, UiQuery *query);
  CtPoliceStationList stations;                          // Original list at48.
  CtServiceFundingList funded_buildings;                 // Original list at52.
  CtSizedIntMap working;                                 // Original72, resolution shift2.
  CtServiceDispatchList dispatches;                      // Original list at108.
  ScRandom random;                                       // Original time-seeded service random state.
  ScLock station_lock;                                   // Original152.
  CtJailList jails;                                      // Original list at188.
  CtCrimeLayer *hazard;                                  // Original124.
  u8 funding_percentage;                                 // Original208.
  u32 coverage_samples, coverage_sum, uncovered_samples; // Original220/224/228.
  bool bound;
  CtAgentTree *agent_tree;
  CtStartSimLayer *start_sim;
  OcAnimManager *animations;
  CtCity *city;                   // Original100.
  CtDailyPolice *manipulator;     // Original68, retained after creation.
  ScOccupantManager *occupants;   // Original116, retained surface manager.
  EcBudgetLayer *budget;          // Original216, retained city budget layer.
  EcDepartmentBudget *department; // Original212, acquired during simulation begin.
  // Recovered service state; this native layout is not an overlay of the installed receiver.
  bool on_strike;                         // Original56.
  u32 strike_event;                       // Original60.
  u32 jail_capacity, inmate_count;        // Original192/196.
  u32 criminals_at_large, upkeep;         // Original200/204.
  u32 average_coverage, percent_coverage; // Original232/236.
};

#endif
