#ifndef SC3_DISASTERS_DS_TORNADO_H
#define SC3_DISASTERS_DS_TORNADO_H

#include "city/ct_date.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"

class CtCity;
class CtStartSimLayer;
class CtAuraLayer;
class CtAgentTree;
class ScOccupantManager;
class ScOccupant;
class ApBoxX;
class DsParadeInstance;

// PrivateA4C68296 identity. Observed word predicates/state return are retained; nominal spellings are unresolved.
class DsParadeControl : public ScUnknown {
public:
  virtual i32 AcceptsKind(u32 kind) = 0;
  virtual i32 IsReady() = 0;
  virtual i32 IsEnding() = 0;
  virtual u32 RequestEnd() = 0;
  virtual u8 AdvanceSchedule() = 0;
};

// Original144-byte parade manager has agent/common/private/message views and versioned scalar saved state.
// Route protection and specialized instance progression remain explicit dependencies.
class DsParadeManager : public DsAgent, public DsManager, public DsParadeControl, public ScMessageTarget {
  friend class DsParadeInstance;

public:
  DsParadeManager();
  ~DsParadeManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool DoMessage(ScMessage *message) override;
  bool Tick() override;
  bool AttemptDisasterStart() override;
  // Original20E7E uses the actual private schedule receiver with no extra register arguments.
  u8 AdvanceSchedule() override;
  i32 AcceptsKind(u32 kind) override;
  i32 IsReady() override;
  i32 IsEnding() override;
  u32 RequestEnd() override;

protected:
  // Complete replay candidate; private nominal byte spelling remains unresolved.
  u8 Resume();
  // Original occupant start retains the actual separate receiver, occupant and raw direction operands.
  u8 StartOccupant(ScOccupant *occupant, u8 mode);
  // Original203DA tries a centered random origin, then the city center; private byte nominal spelling is unresolved.
  u8 FindStart(ScOccupant **occupant, u8 *mode);
  u8 FindRoute(ScOccupant **occupant, u8 *mode, u32 origin_x, u32 origin_z);
  void PostNoRoute();
  u8 WalkAxis1(u32 x, u32 z);
  u8 WalkAxis2(u32 x, u32 z);
  void ProtectOccupants();
  void RestoreOccupants();
  void InstanceStarted();
  u32 InstanceStopped();
  void EffectsStarted();
  void EffectsStopped();
  u32 FinishInstance(DsParadeInstance *instance);
  u8 StartNext(u32 x, u32 z, u8 direction, u32 remaining);
  bool initialized;                 // Original24.
  DsLayer *layer;                   // Original28.
  CtStartSimLayer *simulation;      // Original32, constructor-unwritten; actual city356.
  CtAuraLayer *aura;                // Original36, optional retained identity4259C018.
  CtAgentTree *agent_types;         // Original40, constructor-unwritten.
  CtCity *city;                     // Original44.
  u32 columns, rows;                // Original48/52, constructor-unwritten.
  ScOccupantManager *occupants;     // Original56.
  u32 word_60;                      // Original60, constructor-unwritten carrier.
  DsRandom random;                  // Original64, before counter initialization.
  u32 active_count;                 // Original88.
  u32 effects_count, visible_count; // Original92/96, active-effect and camera-visible counts; constructor-unwritten.
  ApBoxX *box;                      // Original100.
  u32 event_index;                  // Original104, selected configuration row; constructor0 and persisted.
  ScRect bounds;                    // Original108..120, used by actual occupant protection scans.
  u32 state;                        // Original124, constructor/live binding1.
  u32 start_x, start_z;             // Original128/132, constructor-unwritten; reset by live binding.
  u8 mode, resume;                  // Original136/137, raw saved bytes; constructor-unwritten.
  u32 last_event_month;             // Original140, last event year*12+month; constructor0, persisted from version2.
};

class DsParadeManager;
class NwParadeBlock;

// Private24C6828C identity belongs to the actual parade movement callback, separate from riot motion.
// Private query transport and the ignored animation return retain explicitly qualified nominal declarations.
class DsParadeMotion : public ScUnknown {
public:
  virtual void EnterCell(u32 x, u32 z) = 0;
  virtual void LeaveCell(u32 x, u32 z) = 0;
  virtual void RequestRetarget() = 0;
  // Complete animation effects; the original ignored nominal return remains unresolved.
  virtual void AdvanceTime(u32 elapsed) = 0;
  virtual i32 AcceptsKind(u32 kind) = 0;
};

// Original ordered map stores packed cell coordinates and signed-tested wrapping use counts.
struct DsParadeCell {
  u32 key;
  i32 count;
};

// Actual 100-byte instance has primary/private identities, visited-cell storage and the embedded local date.
// Actual block attachment, completion polling and teardown use the retained street-simulation interface.
class DsParadeInstance final : public DsInstance, public DsParadeMotion {
  friend class DsParadeManager;

public:
  DsParadeInstance();
  ~DsParadeInstance();
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
  void EnterCell(u32 x, u32 z) override;
  void LeaveCell(u32 x, u32 z) override;
  void RequestRetarget() override;
  void AdvanceTime(u32 elapsed) override;
  i32 AcceptsKind(u32 kind) override;
  u8 Init(DsParadeManager *manager, u32 x, u32 z, u8 direction);
  u8 Shutdown();

protected:
  u8 Advance(i32 elapsed, u8 running);
  u8 TryStartCell(u32 x, u32 z);
  u8 AttachToOccupant(ScOccupant *occupant);
  u32 Finish();
  void PostPhase(u32 phase);
  void ClearEffects();
  void RefreshVisibility();
  bool initialized;
  u8 in_camera;                 // Original8/9.
  u32 references;               // Original12.
  DsParadeManager *manager;     // Original16, borrowed.
  NwParadeBlock *effect;        // Original20, actual retained cISSParadeBlock secondary interface.
  ScArray<DsParadeCell> cells;  // Original24..35, actual ordered cell/use-count storage.
  u32 elapsed, camera_ticks;    // Original36/40, latter constructor-unwritten.
  CtDate date;                  // Original44/48.
  u32 x, z;                     // Original52/56.
  i32 phase;                    // Original60.
  u32 effects[2];               // Original64/68, invalid-handle sentinels.
  u8 running;                   // Original72, raw callback gate.
  bool retarget, flag_74;       // Original73/74; role of the latter remains unresolved.
  u32 start_x, start_z;         // Original76/80, constructor-unwritten.
  u8 direction;                 // Original84, constructorFF sentinel.
  u32 remaining, sprite, delay; // Original88/92/96.
};

class ScString;
class DsRiotState;
struct DsRiotHistory;
class DsRiotInstance;
class DsFireManager;
class CtAgentTree;
class CtPoliceLayer;
class CtWorldLayer;
class CtStartSimLayer;
class CtAuraLayer;
class ScOccupantManager;
class ApBoxX;

// Original399FC instance limit defaults to one; description and static property loading are recovered.
extern i32 DsRiotMaximumInstances;
extern i32 DsRiotInitialDelay, DsRiotPremonitionTicks, DsRiotUiId;
extern u32 DsRiotMinimumUnemployment, DsRiotMaximumDuration, DsRiotCostPerTick;
extern f32 DsRiotDurationScale;
extern i32 DsRiotMinimumFireballTime, DsRiotMaximumFireballTime;
extern i32 DsRiotMinimumInterval, DsRiotMinimumDuration;
extern u32 DsRiotMaximumInterval;
extern f32 DsRiotIntervalScale;
extern u32 DsRiotSearchRadius;
extern i8 DsRiotMaximumAura;
// Actual coefficient effects; the ignored private nominal return remains unresolved.
void DsUpdateRiotScales();

// Shared original geometry predicate, preserving observed full-word zero-or-one transport.
i32 DsRiotAcceptsOccupant(ScOccupant *occupant);

// Original twenty-byte pending record; phase is read as signed while coordinates and accounting remain DWORDs.
struct DsRiotRecord {
  u32 x, z;
  i32 phase;
  u32 cost, destroyed;
};

// Original private0303B198 identity. Three operands and the raw final byte are proven; nominal spelling is pending.
class DsRiotControl : public ScUnknown {
public:
  virtual DsRiotState *Start(u32 x, u32 z, u8 automatic) = 0;
};

// Riot manager registered asE2FE8D38, with actual agent/common/private/message identities and owned pending records.
// Static tuning and unemployment sampling are recovered; specialized swarm production remains explicit.
class DsRiotManager : public DsAgent, public DsManager, public DsRiotControl, public ScMessageTarget {
  friend class DsRiotInstance;

public:
  DsRiotManager();
  ~DsRiotManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool DoMessage(ScMessage *message) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool AttemptDisasterStart() override;
  bool Tick() override;
  DsRiotState *Start(u32 x, u32 z, u8 automatic) override;
  // Original unused history query returns a full zero word without writing either output.
  i32 GetRecords(DsRiotHistory *records, u32 *count) const;

protected:
  // Complete private candidate transports; nominal byte spellings remain unresolved.
  u8 Restore(const DsRiotRecord *record);
  // Complete signed count calculation; the private nominal source return spelling remains unresolved.
  i32 GetStartCount();
  void InstanceStarted();
  // Complete private lifecycle effects; ignored nominal source returns remain unresolved.
  void InstanceEnded();
  void EffectsStarted();
  void EffectsEnded();
  void EnterCamera();
  void LeaveCamera();
  u32 FinishInstance(DsRiotInstance *instance);
  // OriginalD3BF unsigned newest monthly unemployment sample; private nominal word spelling remains unresolved.
  u32 GetUnemployment();
  u8 FindOccupant(ScOccupant **output, i32 origin_x, i32 origin_z);
  u8 FindRandomOccupant(ScOccupant **output);
  u8 StartRandom();
  DsRiotInstance *StartOccupant(ScOccupant *occupant, u8 automatic);
  bool initialized;               // Original24.
  DsLayer *layer;                 // Original28.
  CtStartSimLayer *simulation;    // Original32, unwritten until live binding; city356.
  CtAuraLayer *aura;              // Original36, optional queried identity4259C018.
  CtWorldLayer *world;            // Original40, retained city service352.
  CtPoliceLayer *police;          // Original44, retained city service372.
  CtAgentTree *agent_types;       // Original48, unwritten until live binding.
  CtCity *city;                   // Original52.
  u32 columns, rows;              // Original56/60, unwritten until live binding.
  ScOccupantManager *occupants;   // Original64.
  u32 word_68;                    // Original68, constructor-unwritten carrier; meaning remains unresolved.
  DsRandom random;                // Original72, constructed before counters and record storage.
  u32 active_count;               // Original96.
  u32 effect_count, camera_count; // Original100/104, unwritten until live binding.
  ApBoxX *box;                    // Original108.
  DsFireManager *fire;            // Original112, retained complete fire manager.
  i32 elapsed, interval;          // Original116/120, signed cadence words initialized to0/10.
  ScArray<DsRiotRecord> pending;  // Original124/128/132.
};

// Original RiotDisaster row callback; its key and context operands are unused.
void DsRiotReadEntry(const ScString &key, const ScString &value, void *context);

class DsRiotManager;
class NwRiotCrowd;
struct DsRiotHistory;

// Actual private2303B189 identity combines a const history query with the movement callbacks.
// History record layout and the full-word query's nominal return spelling remain unresolved.
class DsRiotState : public ScUnknown {
public:
  virtual i32 GetRecords(DsRiotHistory *records, u32 *count) const = 0;
  virtual void EnterCell(u32 x, u32 z) = 0;
  virtual void LeaveCell(u32 x, u32 z) = 0;
  virtual void RequestRetarget() = 0;
  virtual void IgniteCell(u32 x, u32 z) = 0;
};

// Original ordered map stores the packed cell coordinate and a signed-tested wrapping use count.
struct DsRiotCell {
  u32 key;
  i32 count;
};

// Original92-byte receiver has primary and private identities, actual protected-cell ownership and an embedded date.
// Actual crowd creation and teardown use the separate street-simulation interface and retained protected cells.
class DsRiotInstance final : public DsInstance, public DsRiotState {
  friend class DsRiotManager;

public:
  DsRiotInstance();
  ~DsRiotInstance();
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
  i32 GetRecords(DsRiotHistory *records, u32 *count) const override;
  void EnterCell(u32 x, u32 z) override;
  void LeaveCell(u32 x, u32 z) override;
  void RequestRetarget() override;
  void IgniteCell(u32 x, u32 z) override;
  // Original six separate stack arguments and raw final byte; private nominal byte spelling remains unresolved.
  u8 Init(DsRiotManager *manager, u32 x, u32 z, u32 cost, u32 destroyed, u8 automatic);
  u8 Shutdown();

protected:
  u8 Advance(i32 elapsed, u8 running);
  // Complete private effects; original ignored nominal return declarations remain unresolved.
  void PostPhase(u32 phase);
  void ClearEffects();
  void RefreshVisibility();
  void Retarget();
  u32 Finish();
  u8 TryStartCell(u32 x, u32 z);
  // OriginalE175 publishes the actual crowd and binds this instance before configuring and placing it.
  u8 AttachToOccupant(ScOccupant *occupant);
  // Complete occupant-flag restoration effects; unused count pointer and private nominal return remain unresolved.
  void ReleaseCell(u32 key, i32 *count);
  // Original full-word containment predicate; private nominal BOOL spelling remains unresolved.
  i32 ContainsCell(u32 x, u32 z);
  bool initialized, in_camera; // Original8/9.
  u32 references;              // Original12.
  DsRiotManager *manager;      // Original16, borrowed.
  NwRiotCrowd *effect;         // Original20, actual retained cISSRiotCrowd secondary interface.
  ScArray<DsRiotCell> cells;   // Original24..35, ordered packed-cell/use-count map.
  i32 elapsed;                 // Original36, signed timing comparisons.
  u32 camera_ticks;            // Original40, constructor-unwritten.
  CtDate date;                 // Original44/48, actual local-date constructor.
  u32 x, z;                    // Original52/56.
  i32 phase;                   // Original60, signed saved-record phase.
  u32 effects[3];              // Original64/68/72, invalid-handle sentinels.
  u8 automatic, running;       // Original76/77, raw source flag and callback gate.
  u32 cost, destroyed;         // Original80/84, constructor-unwritten until live initialization.
  bool retarget;               // Original88.
};

class ScString;

class DsLayer;
class CtCity;
class ScDirtBag;
class ScOccupantManager;
class ApBoxX;
class CtAgentTree;
class DsTornadoMotion;
class DsTornadoInstance;
struct ScVec3F;

// Proven private manager identity at original complete+16, IID. Remaining slots stay unresolved.
class DsTornadoControl : public ScUnknown {
public:
  virtual DsTornadoMotion *Start(
      u32 x, u32 z, const ScVec3F *direction, i8 severity, f32 speed, i32 duration, bool automatic) = 0;
  virtual bool StartRandom(bool automatic) = 0;
};

// Original interval globals/, initialized to 12/36 in the installed DLL.
extern i32 DsTornadoMinimumInterval, DsTornadoMaximumInterval;
extern u32 DsTornadoMaximumInstances;
extern f32 DsTornadoMinimumSpeed, DsTornadoMaximumSpeed;
extern i32 DsTornadoMinimumDuration, DsTornadoMaximumDuration;
extern i32 DsTornadoFormationTicks;
extern i32 DsTornadoUiId;
extern u32 DsTornadoFirstDelay, DsTornadoSecondDelay, DsTornadoThirdDelay, DsTornadoAttackDelay;
// Native coefficient effects; the ignored private nominal source return remains unresolved.
void DsUpdateTornadoScales();
// Original39AF8..39B00 inclusive damage percentages, indexed by the three valid severities.
extern u32 DsTornadoDamageChance[3];

// Tornado manager with recovered construction, shutdown, saved-city methods and pending-record replay. Private interface
// coverage, archive read-header construction and static initialization remain incomplete; city services keep their types.
class DsTornadoManager : public DsAgent, public DsManager, public DsTornadoControl, public ScMessageTarget {
  friend class DsTornadoInstance;

public:
  DsTornadoManager();
  ~DsTornadoManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Tick() override;
  bool AttemptDisasterStart() override;
  bool Restore(const DsTornadoRecord *record);
  bool DoMessage(ScMessage *message) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  DsTornadoMotion *Start(
      u32 x, u32 z, const ScVec3F *direction, i8 severity, f32 speed, i32 duration, bool automatic) override;
  bool StartRandom(bool automatic) override;

protected:
  // Private helpers are used for effects only; their original nominal return declarations remain unresolved.
  void RegisterEffect();
  void ReleaseEffect();
  void ReleaseInstance();
  void EnterCamera();
  void LeaveCamera();
  void NotifyRemoval(DsTornadoInstance *instance);

  // Original fields complete+24..+44; the constructor deliberately leaves terrain and agent_types unwritten.
  bool initialized;
  DsLayer *layer;
  ScDirtBag *terrain;
  CtAgentTree *agent_types;
  CtCity *city;
  ScOccupantManager *occupants;
  DsRandom random; // Original complete+48, seeded between the retained pointer and counter initialization.
  u32 active_count, effect_count, camera_count; // Original complete+72/+76/+80.
  u32 maximum_x, maximum_z;                     // Original complete+84/+88, left unwritten until Init.
  ApBoxX *box;                                  // Original complete+92.
  u16 flags_96;
  ScArray<DsTornadoRecord> pending; // Original complete+100/+104/+108 vector, consumed after city loading.
  i32 elapsed, interval;            // Original complete+112/+116.
};

// Original description-row callback; the numeric key is consumed but does not select a row.
void DsTornadoReadEntry(const ScString &key, const ScString &value, void *context);

class DsTornadoManager;
class DsManager;
class ScOccupant;
struct GeCellRect;

// Tornado-specific private motion interface, IID. Track instances reuse that IID for an incompatible
// private state interface; their virtual contracts must remain distinct. Private nominal returns remain qualified.
class DsTornadoMotion : public ScUnknown {
public:
  virtual bool GetPosition(i32 *x, i32 *y, i32 *z) const = 0;
  // Candidate private getters retain observed output-pointer return and stored-f32 value; nominal spelling is unresolved.
  virtual ScVec3F *GetVelocity(ScVec3F *output) const = 0;
  virtual ScVec3F *GetDirection(ScVec3F *output) const = 0;
  virtual u8 GetSeverity() = 0;
  virtual f32 GetSpeed() = 0;
};

// Recovered instance with primary, specialized and message-target identities, real lifecycle and phase advancement.
// Movement and debris use the real terrain/simulator operations; this native class is not a foreign layout.
class DsTornadoInstance : public DsInstance, public DsTornadoMotion, public ScMessageTarget {
public:
  DsTornadoInstance();
  ~DsTornadoInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool Shutdown();
  // Whole142EC retains two operands; private first-operand spelling and nominal bool/BYTE return remain qualified.
  bool Advance(i32 elapsed, bool running);
  bool Tick(DsTickType tick) override;
  bool GetPosition(i32 *x, i32 *y, i32 *z) const override;
  ScVec3F *GetVelocity(ScVec3F *output) const override;
  ScVec3F *GetDirection(ScVec3F *output) const override;
  u8 GetSeverity() override;
  f32 GetSpeed() override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *output_x, u32 *output_z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  bool Init(DsTornadoManager *manager, u32 x, u32 z, const ScVec3F *direction, i8 severity, f32 speed, i32 duration,
      u32 cost, u32 destroyed, bool automatic);

protected:
  // Original callers use these effects only; their private nominal return declarations remain unresolved.
  void RegisterNotifications();
  void RemoveNotifications();
  void ClearEffects();
  // Synchronous phase notice effects are recovered; the ignored private nominal return remains unresolved.
  void PostPhase(u32 state);
  // Complete retirement effects; the ignored private nominal return remains unresolved.
  void Finish();
  // Whole debris/damage effects are recovered; ignored private nominal return declarations remain unresolved.
  void CheckWaterEffects();
  void ApplyCellEffects();

  // Original offsets +12/+16/+20/+24/+28; native C++ supplies the three actual interface subobjects.
  bool initialized;
  DsTornadoManager *manager;
  void *effect; // Actual external COM output destination; reached operations use SpTornadoSimulator.
  u32 x, z;
  // Original fixed-point position +32; direction +44; velocity +56; signed severity +68; speed +72.
  i32 fixed_x, fixed_y, fixed_z;
  ScVec3F direction, velocity;
  i8 severity;
  f32 speed;
  bool in_camera;
  i32 phase, remaining, duration, steps;
  // Original six-word array +96 is initialized to all ones. Reference count is +120; final flag meanings remain unresolved.
  u32 effects[6], references, cost, destroyed;
  bool flag_132, flag_133, notifications_registered;
};

class CtCity;
class CtAgentTree;
class CtStartSimLayer;
class ScDirtBag;
class ScOccupantManager;
class ApBoxX;
class ScCheatCodeManager;
class DsUfoInstance;

// Persisted UFO kinds selected by the manager's year and probability gates. Other stored values remain raw words.
enum DsUfoKind : u32 {
  kDsUfoFlyby = 2,
  kDsUfoAbduction = 3,
  kDsUfoCropCircles = 4,
  kDsUfoAttack = 5,
};

// Persisted instance phases also index the original timing array. Manual starts enter the attack-pending phase.
enum DsUfoPhase : u32 {
  kDsUfoInitialDelay = 0,
  kDsUfoFirstPremonition = 1,
  kDsUfoSecondPremonition = 2,
  kDsUfoAttackPending = 3,
  kDsUfoAttacking = 4,
  kDsUfoDeparting = 5,
};

// Original48-byte history/state payload records dates, UFO activity and category accounting.
struct DsUfoRecord {
  u32 serial, ships, kind, abductions, crop_circles;
  u32 landmarks, spaceports, low_tech, advanced;
  u32 phase, elapsed, cost;
};

static_assert(sizeof(DsUfoRecord) == 48);

// Actual43024DED private manager identity; additional start/state methods remain unreconstructed.
class DsUfoControl : public ScUnknown {
public:
  virtual u32 GetRecords(DsUfoRecord *records, u32 *count) const = 0;
  // Original shared trigger forwards the common Boolean result through AL; private nominal bool/BYTE unresolved.
  virtual u8 Start(u8 automatic) = 0;
};

// Factory22FE8D3E constructs the136-byte manager identified by its UFODisasterTunables configuration.
// The public manager and instance phases call the specialized fleet, whose sprite production remains unported.
class DsUfoManager : public DsAgent, public DsManager, public DsUfoControl, public ScMessageTarget {
  friend class DsUfoInstance;

public:
  DsUfoManager();
  ~DsUfoManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool Shutdown() override;
  bool Tick() override;
  bool AttemptDisasterStart() override;
  void StopInstance(DsInstance *instance) override;
  u8 Start(u8 automatic) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool DoMessage(ScMessage *message) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Init(DsLayer *layer, CtCity *city) override;
  u32 GetRecords(DsUfoRecord *records, u32 *count) const override;

protected:
  // Complete seed-record effects; the ignored original vector result is not a game return contract.
  void SeedHistory();
  // Original helper returns the selected kind word after publishing it.
  u32 SetKind(u32 kind);
  // Complete counter effects; the ignored private nominal return remains unresolved.
  void InstanceEnded();
  // Complete history/retirement effects; ignored private nominal returns remain unresolved.
  void RecordInstance(DsUfoInstance *instance);
  void FinishInstance(DsUfoInstance *instance);
  bool initialized;             // Original24.
  u8 automatic;                 // Original25, raw temporary automatic-start gate.
  u32 kind;                     // Original28, constructor1; selected attack kind.
  DsLayer *layer;               // Original32.
  ScUnknown *demolition;        // Original36, retained city404 Unknown lifetime prefix.
  ScDirtBag *terrain;           // Original40, constructor-unwritten.
  CtStartSimLayer *simulation;  // Original44, constructor-unwritten; actual city356.
  ScCheatCodeManager *cheats;   // Original48, application cheat-code manager.
  bool flag_52;                 // Original52.
  CtAgentTree *agent_types;     // Original56, constructor-unwritten.
  CtCity *city;                 // Original60.
  ScOccupantManager *occupants; // Original64.
  u32 word_68;                  // Original68, constructor-unwritten.
  DsRandom random;              // Original72.
  u32 active_count;             // Original96.
  ApBoxX *box;                  // Original100; constructor null.
  u16 flags_104;
  ScArray<DsUfoRecord> history, pending; // Original108..119/120..131.
  u32 word_132;                          // Original132, constructor-unwritten.
};

class CtCity;
class CtAgentTree;
class CtStartSimLayer;
class CtZoneLayer;
class ScOccupantManager;
class ScOccupant;
class ApBoxX;
class DsWaterInstance;

// Original property-backed words3A220/3A224/3A234, updated through the actual variable manager.
extern u32 DsWaterMaximumActive, DsWaterMinimumMonths, DsWaterChance;
extern u32 DsWaterMinimumStrength, DsWaterMaximumStrength;
extern u32 DsWaterFirstDelay, DsWaterSecondDelay, DsWaterThirdDelay, DsWaterAttackDelay, DsWaterDuration;
// Original3A238 signed damage-square radius and3A24C debris lifetime.
extern i32 DsWaterDamageRadius;
extern u32 DsWaterDebrisLifetime;
extern u32 DsWaterSpeedTimes1000, DsWaterFrameMicroseconds;
extern u32 DsWaterFastestSpeedTimes100000000, DsWaterOuterGravityDistance;
u8 DsWaterPrefersOccupant(ScOccupant *occupant);

// Actual32-byte history payload stores accounting, raw width adjustment, packed fixed position and saved phase.
struct DsWaterRecord {
  u32 serial, magnitude, destroyed, cost, narrow_width, packed_location, phase, phase_ticks;
};

static_assert(sizeof(DsWaterRecord) == 32);

// Actual E3135E82 private identity at complete+16; nominal private byte declarations remain unresolved.
class DsWaterControl : public ScUnknown {
public:
  virtual u32 GetRecords(DsWaterRecord *records, u32 *count) const = 0;
  virtual u8 GetFirstRecord(DsWaterRecord *record) const = 0;
  virtual u8 Start(f32 strength, u8 automatic) = 0;
};

// Factory52FE8D50 constructs this136-byte manager. Water describes its proven real-water placement scan,
// without asserting a human disaster name or an inheritance relationship with structurally similar managers.
// Manager tuning is recovered; specialized sprite progression is still incomplete.
class DsWaterManager : public DsAgent, public DsManager, public DsWaterControl, public ScMessageTarget {
  friend class DsWaterInstance;

public:
  DsWaterManager();
  ~DsWaterManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Shutdown() override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool DoMessage(ScMessage *message) override;
  void StopInstance(DsInstance *instance) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool AttemptDisasterStart() override;
  u8 Start(f32 strength, u8 automatic) override;
  bool Tick() override;
  u32 GetRecords(DsWaterRecord *records, u32 *count) const override;
  u8 GetFirstRecord(DsWaterRecord *record) const override;

protected:
  // Complete seed effects; the ignored original vector return has no established nominal private declaration.
  void SeedHistory();
  // Complete folded counter effects; ignored private nominal return remains unresolved.
  void InstanceEnded();
  // Direct history/retirement effects; ignored private nominal returns remain unresolved.
  void RecordInstance(DsWaterInstance *instance);
  void FinishInstance(DsWaterInstance *instance);
  // Whole placement operations retain original separate operands; private nominal declarations remain candidates.
  u32 CountPreferred(const GeCellRect *bounds);
  u8 SampleRectangle(const GeCellRect *bounds, ScVec3I *position, i32 preferred);
  u8 IsValidPosition(const ScVec3I *position);
  u8 SelectStart(ScVec3I *position);
  bool initialized;
  u8 automatic_start;
  DsLayer *layer;
  ScUnknown *demolition;
  ScDirtBag *terrain;
  CtZoneLayer *zones;
  CtStartSimLayer *simulation;
  CtAgentTree *agent_types;
  CtCity *city;
  ScOccupantManager *occupants, *underground_one, *underground_two;
  ApBoxX *box;
  bool active;
  u8 flag_73;
  DsRandom random;
  u32 active_count;
  u16 flags_108;
  ScArray<DsWaterRecord> history, pending;
};

#endif
