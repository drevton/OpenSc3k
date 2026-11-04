#ifndef SC3_DISASTERS_DS_TOXIC_H
#define SC3_DISASTERS_DS_TOXIC_H

#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"
#include "disasters/ds_tornado.h"
#include "geometry/ge_types.h"

class EcLayer;
class CtOrdinanceLayer;
class GeBuildingLayer;
class CtCity;
class DsLayer;
class ScDirtBag;
class CtAgentTree;
class ScOccupantManager;
class ApBoxX;
class CtZoneLayer;
class DsToxicInstance;
class DsToxicMotion;

// Original32-byte pending state stores cell location, raw severity, velocity and independent damage totals.
struct DsToxicRecord {
  u32 x, z;
  i32 severity;
  ScVec3F velocity;
  u32 cost, destroyed;
};

static_assert(sizeof(DsToxicRecord) == 32);

// Raw tuning words retain their full bits until consumers apply their proven signed or unsigned interpretations.
extern u32 DsToxicMaximumActive;
extern f32 DsToxicMinimumSpeed, DsToxicMaximumSpeed;
extern u32 DsToxicMinimumClouds;
extern u32 DsToxicMaximumClouds;
extern u32 DsToxicFirstNoticeTime;
extern u32 DsToxicSecondNoticeTime;
extern u32 DsToxicThirdNoticeTime;
extern u32 DsToxicVisibleTime;
extern u32 DsToxicMinimumHeight;
extern u32 DsToxicMaximumHeight;
extern u32 DsToxicMinimumDuration;
extern u32 DsToxicMaximumDuration;
extern u32 DsToxicAverageInterval;
extern u32 DsToxicPollutionWeight;
extern u32 DsToxicCleanIndustryWeight;
extern u32 DsToxicPollutantFeeWeight;
extern u32 DsToxicWasteDisposalWeight;
extern u32 DsToxicWastePlantWeight;
extern u32 DsToxicThreshold;
extern u32 DsToxicScorePerLevel;
extern u32 DsToxicMinimumRainStart;
extern u32 DsToxicMaximumRainStart;
extern u32 DsToxicMinimumRainDuration;
extern u32 DsToxicMaximumRainDuration;
extern u32 DsToxicMinimumRainGap;
extern u32 DsToxicAbandonRadius;
extern u32 DsToxicDestructionRadius;
extern u32 DsToxicDestructionChance;
extern u32 DsToxicAbandonChance;

// Proven private identityE475EAD4 at original complete+16. Native methods preserve its separate spawn arguments;
// this reached interface subset is not an installed vtable overlay.
class DsToxicControl : public ScUnknown {
public:
  virtual DsToxicMotion *Start(
      u32 x, u32 z, const ScVec3F *velocity, u32 severity, u32 cost, u32 destroyed, u8 automatic) = 0;
  virtual f64 IsTriggerEligible() = 0;
};

// FactoryC2FE8D43 and constructor14D61 bind the common manager to the literal ToxicCloudDisasterTunables loader.
// Cloud progression uses its actual foreign display; the public void stop contract follows retail CodeView.
class DsToxicManager : public DsAgent, public DsManager, public DsToxicControl, public ScMessageTarget {
  friend class DsToxicInstance;

public:
  DsToxicManager();
  ~DsToxicManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool DoMessage(ScMessage *message) override;
  DsToxicMotion *Start(
      u32 x, u32 z, const ScVec3F *velocity, u32 severity, u32 cost, u32 destroyed, u8 automatic) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool AttemptDisasterStart() override;
  bool Tick() override;
  f64 IsTriggerEligible() override;

protected:
  // Signed DWORD pollution/ordinance/building score; private nominal declaration remains unknown.
  i32 ComputeScore();
  // Original14E93 clears elapsed before drawing; its ignored private nominal return remains unresolved.
  void ResetInterval();
  // Two separate stack words and full normalized EAX are proven; private nominal BOOL/mode spellings remain unresolved.
  i32 Start(u32 severity, i32 automatic);
  // Complete cell selection/weight effects; private nominal byte and ignored-return declarations remain candidates.
  u8 GetSpawnWeight(u32 x, u32 z, u32 *weight);
  void FindSpawnSite(u32 *x, u32 *z);
  // Private lifecycle helpers expose their original effects; ignored nominal returns remain unresolved.
  void InstanceStarted();
  void InstanceEnded();
  void BeginNotice175();
  void BeginNotice231();
  void EndNotice175();
  void EndNotice231();
  void EndNotice231Mode();
  void LeaveCamera();
  void EnterCamera();
  void EnterNotice231Mode();
  DsToxicInstance *Restore(const DsToxicRecord *saved);
  // Finish publishes removal before teardown and returns the final Release word; nominal spelling is unresolved.
  u32 FinishInstance(DsToxicInstance *instance);
  bool initialized;
  DsLayer *layer;
  ScDirtBag *terrain;
  CtAgentTree *agent_types;
  CtCity *city;
  ScOccupantManager *occupants;
  DsRandom random;                                                             // Original complete+48.
  u32 active_count;                                                            // Original complete+72.
  u32 notice_175_count, notice_231_count, notice_231_mode_count, camera_count; // Original76/80/84/88.
  u32 maximum_x, maximum_z; // Original92/96, unwritten until city binding.
  ApBoxX *box;
  ScArray<DsToxicRecord> pending; // Original104, separately owned32-byte records.
  EcLayer *pollution;             // Original complete+120, retained by city binding.
  CtOrdinanceLayer *ordinances;   // Original complete+124, retained by city binding.
  GeBuildingLayer *buildings;     // Original complete+128, retained by city binding.
  CtZoneLayer *zones;
  u32 animation_x, animation_z; // Original144/148, unwritten until city binding.
  u32 elapsed, interval;        // Original complete+136/+140, unsigned scheduling words.
};

struct ScPointXZ;

// Actual4303B1AE instance view. The count getter's private nominal source spelling remains unresolved.
class DsUfoMotion : public ScUnknown {
public:
  virtual u32 GetRecords(DsUfoRecord *records, u32 *count) const = 0;
  virtual u32 GetCount() = 0;
  virtual void CopyRecord(DsUfoRecord *record) = 0;
};

// Actual128-byte receiver has primary/private/message identities and its real48-byte persisted state at80.
// Live phase progression calls the specialized foreign fleet directly; its actual sprite production remains unported.
class DsUfoInstance final : public DsInstance, public DsUfoMotion, public ScMessageTarget {
  friend class DsUfoManager;

public:
  DsUfoInstance();
  ~DsUfoInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  bool Tick(DsTickType tick) override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  // Activity reports only the actual first owned fleet child; global fallback ships do not make it active.
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  u32 GetRecords(DsUfoRecord *records, u32 *count) const override;
  u32 GetCount() override;
  void CopyRecord(DsUfoRecord *record) override;
  bool DoMessage(ScMessage *message) override;
  // Original separate manager, kind and two byte operands; private nominal bool/BYTE remains unresolved.
  u8 Init(DsUfoManager *manager, u32 kind, u8 flag, u8 automatic);
  u8 Init(DsUfoManager *manager, const DsUfoRecord *record);
  u8 Shutdown();

protected:
  // Complete synchronous notice effects; ignored private nominal returns remain unresolved.
  void PostPhase(u32 phase);
  void ClearEffects();
  // Original output array/count operands; ignored private nominal return remains unresolved.
  void SelectOpenSites(ScPointXZ *sites, u32 *count);
  void SelectTargetBuildings(ScPointXZ *sites, u32 *count);
  // Whole attack/exit operations; private nominal bool/BYTE result spellings remain unresolved.
  u8 ExecuteAttack();
  u8 QueueDeparture();
  // Original two cell operands and byte results; private nominal bool/BYTE spelling remains unresolved.
  u8 IgniteCell(u32 x, u32 z);
  u8 DestroyCell(u32 x, u32 z);
  u8 BombCell(u32 x, u32 z);
  u8 PlaceCrop(u32 x, u32 z);
  bool initialized;      // Original12.
  DsUfoManager *manager; // Original16, borrowed.
  void *effect;          // Original20, actual external COM output destination for the specialized fleet.
  u8 flag_24;            // Original24.
  u32 references;        // Original28.
  u8 flag_32, flag_33;   // Original32/33.
  u32 location_index;    // Original36.
  u32 timing[5];         // Original40..56.
  u32 effects[5];        // Original60..76, invalid-handle sentinels.
  DsUfoRecord state;     // Original80..127.
};

// Classify the first recognized ancestor using the actual UFO category-ID tables.
i32 DsUfoClassifyTarget(ScOccupant *occupant);

// Actual A276C763 private identity at complete+4. Nominal floating/word/byte return spellings remain unresolved.
class DsWaterState : public ScUnknown {
public:
  virtual f64 GetMagnitude() const = 0;
  virtual u8 GetSeverity() const = 0;
  virtual i32 GetPosition(i32 *x, i32 *y, i32 *z) const = 0;
  virtual void GetRecord(DsWaterRecord *record) = 0;
};

// Actual136-byte receiver has common/private/message identities at0/4/8 and its32-byte saved state at104.
// Position queries and progression retain explicit dependencies on the specialized foreign sprite controller.
class DsWaterInstance final : public DsInstance, public DsWaterState, public ScMessageTarget {
  friend class DsWaterManager;

public:
  DsWaterInstance();
  ~DsWaterInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  bool Tick(DsTickType tick) override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  bool DoMessage(ScMessage *message) override;
  f64 GetMagnitude() const override;
  u8 GetSeverity() const override;
  i32 GetPosition(i32 *x, i32 *y, i32 *z) const override;
  void GetRecord(DsWaterRecord *record) override;
  // Original four separate operands and defined full EAX0/1 result; nominal private BOOL spelling is unresolved.
  i32 Init(DsWaterManager *manager, f32 strength, const ScVec3I *position, u8 automatic);
  u8 Init(DsWaterManager *manager, const DsWaterRecord *record);
  u8 Shutdown();

protected:
  // Complete private scalar effects; ignored nominal return declarations remain unresolved.
  void ConfigureMagnitude(f32 strength);
  void AdjustWidth(u8 enabled);
  void PostPhase(u32 phase);
  void ClearEffects();
  // Original five separate operands and prior-occupancy result; nominal coordinate/byte spellings remain unresolved.
  u8 TallyTile(i32 x, i32 z, u8 destroy, u8 clear_zone, u8 special);
  bool IsWaterNetwork(ScOccupantManager *occupants, u32 x, u32 z);
  u8 BuildPath(u32 x, u32 z, ScArray<ScPointXZ> *points);
  u8 CollectPathOccupants(const ScRect *bounds, ScArray<ScPointXZ> *points);
  u32 SelectFrame();
  // Whole signed half-open scan; private nominal bool/BYTE spelling remains unresolved.
  u8 ApplyDamage(i32 x, i32 z);
  u8 RenderPath(u32 x, u32 z);
  bool initialized;
  u32 timing[5], references;
  bool announcement;
  u8 flag_41;
  u32 effects[5];
  u8 severity, width;
  u32 radius;
  ScVec3I position;
  u32 word_84, columns, rows;
  DsWaterManager *manager;
  void *effect; // Actual external COM output destination; specialized primary interface remains unresolved.
  DsWaterRecord state;
};

class SpToxicCloudDisplay;

// The original12-byte cloud list stores the actual secondary display identity in each non-anchor node.
struct DsToxicLink {
  DsToxicLink *next, *previous;
  SpToxicCloudDisplay *cloud;
};

// Node storage owns only links; active shutdown releases cloud identities before discarding their nodes.
class DsToxicClouds {
public:
  DsToxicClouds();
  ~DsToxicClouds();
  DsToxicClouds(const DsToxicClouds &) = delete;
  DsToxicClouds &operator=(const DsToxicClouds &) = delete;
  void Clear();
  DsToxicLink *anchor;
};

// Proven privateA475EA6F identity at complete+4, with fixed-position outputs and a borrowed velocity copy.
// Nominal private byte/BOOL spellings remain candidates; the native declaration is not a foreign vtable overlay.
class DsToxicMotion : public ScUnknown {
public:
  virtual u8 GetPosition(i32 *x, i32 *y, i32 *z) const = 0;
  virtual ScVec3F *GetVelocity(ScVec3F *output) const = 0;
  virtual i32 Contains(u32 x, u32 z) const = 0;
};

// Constructor16D12 is reached directly from the toxic manager's private seven-argument start.
// Real cloud creation, rain and fade progression call the specialized foreign display directly.
class DsToxicInstance : public DsInstance, public DsToxicMotion, public ScMessageTarget {
  friend class DsToxicManager;

public:
  DsToxicInstance();
  ~DsToxicInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  bool Tick(DsTickType tick) override;
  bool DoMessage(ScMessage *message) override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  u8 GetPosition(i32 *x, i32 *y, i32 *z) const override;
  ScVec3F *GetVelocity(ScVec3F *output) const override;
  i32 Contains(u32 x, u32 z) const override;
  // Original eight arguments and AL1 are proven; private nominal byte spellings remain candidates.
  u8 Init(DsToxicManager *owner, u32 x, u32 z, const ScVec3F *velocity, u32 severity, u32 cost, u32 destroyed,
      u8 automatic);
  u8 Shutdown();

protected:
  // Whole private progression preserves the separate selector word and raw running gate.
  u8 Advance(i32 elapsed, u8 running);
  // Complete spawn/stop effects; original private nominal return declarations remain unresolved.
  void SpawnClouds();
  void StopClouds();
  void RegisterNotifications();
  void RemoveNotifications();
  void ClearEffects();
  void RefreshVisibility();
  void ApplyRainDamage();
  // Original message/payload effects and final release transport are proven; private nominal returns remain unresolved.
  void PostPhase(u32 state);
  u32 Finish();
  bool initialized;
  u8 raining;
  DsToxicManager *manager;
  // The signed12-byte position storage is proved; its original nominal Toxic member type remains unresolved.
  ScVec3I position;
  ScVec3F velocity; // Original32..40, unwritten until initialization.
  bool in_camera;
  u32 phase, phase_ticks;
  u32 effects[6], references, cost, destroyed;
  u8 enabled;
  bool notifications_registered;
  u32 duration; // Original96, unwritten until visible clouds are created.
  u32 severity;
  DsToxicClouds clouds;
  GeCellRect bounds;
  GeCellRect destruction_bounds; // Original124..136, distinct from the wider abandonment bounds.
  GeCellRect camera_bounds;      // Original140..152; separate from the public cell bounds at108.
  u32 next_rain_tick, rain_ticks, rain_duration, active_clouds; // Original156/160/164/168.
};

#endif
