#ifndef SC3_DISASTERS_DS_EVENTS_H
#define SC3_DISASTERS_DS_EVENTS_H

#include "city/api.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/rect.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "disasters/api.h"
#include "disasters/ds_module.h"
#include "disasters/oc_disaster_building.h"
#include "geometry/ge_types.h"
#include "sprites/api.h"

// Actual shared agent base constructed by the fire, track and tornado managers. Its virtual destructor follows
// the inherited city-agent operations in the installed vtable; this native class is not a foreign memory overlay.
class DsAgent : public CtAgent {
public:
  DsAgent();
  virtual ~DsAgent();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Simulate(i32 elapsed) override;
  i32 MinimumExecutionInterval() override;
  bool IsAgentLocking() override;
  u32 TypeId() const override;
  CtAgentType *AgentType() override;
  bool SetAgentType(CtAgentType *type) override;
  bool DebugInstanceTag(ScStringView *output) override;
  bool DebugClassTag(ScStringView *output) override;
  bool DebugTypeTag(ScStringView *output) override;

protected:
  CtAgentType *agent_type; // Original +4.
  u32 references;          // Original +8; acquired references also hold the module director.
};

class DsInstance;

struct DsChangeLink {
  DsChangeLink *next, *previous;
  DsInstance *instance;
  i32 change;
};

// Native disaster-change storage replaces the original list while preserving stable entries and FIFO order.
// The controller owns queued instance references separately; this owner destroys links only.
class DsChangeQueue {
public:
  DsChangeQueue();
  ~DsChangeQueue();
  // Stable sentinel links cannot be transferred to a copied owner.
  DsChangeQueue(const DsChangeQueue &) = delete;
  // Do not create a second owner for existing link allocations.
  DsChangeQueue &operator=(const DsChangeQueue &) = delete;
  void Append(DsInstance *instance, i32 change);
  void Remove(DsChangeLink *node);
  DsChangeLink anchor;
};

class DsInstance;
class DsController;
struct DsInstanceLink;

// Retail CodeView type8122 supplies the original void Rewind and borrowed-instance Next virtuals.
class DsListIterator {
public:
  virtual void Rewind() = 0;
  virtual DsInstance *Next() = 0;
};

class DsActiveIterator final : public DsListIterator {
public:
  explicit DsActiveIterator(DsController *layer);
  void Rewind() override;
  DsInstance *Next() override;

private:
  DsController *layer;
  DsInstanceLink *current, *end;
};

class DsAllIterator final : public DsListIterator {
public:
  explicit DsAllIterator(DsController *layer);
  void Rewind() override;
  DsInstance *Next() override;

private:
  bool pending;
  DsController *layer;
  DsInstanceLink *current, *end;
};

class DsInstance;

struct DsInstanceLink {
  DsInstanceLink *next, *previous;
  DsInstance *instance;
};

// Disaster-instance storage preserves the original set's pointer order, uniqueness and stable cursor entries.
// The controller owns instance references separately; this storage owns only its links.
class DsInstances {
public:
  DsInstances();
  ~DsInstances();
  // A copied owner would invalidate the original stable anchor identity.
  DsInstances(const DsInstances &) = delete;
  // Do not duplicate link ownership through assignment.
  DsInstances &operator=(const DsInstances &) = delete;
  void Clear();
  void Insert(DsInstance *instance);
  DsInstanceLink *Find(DsInstance *instance);
  void Remove(DsInstanceLink *node);
  DsInstanceLink anchor;
  u32 count; // Original set owner size word, independent of its stable sentinel.
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class CtCity;
class RsDatabaseSegment;
class DsLayer;
class DsInstance;

// Installed Unlimited manager order differs from the public SDK: saved-city Init precedes layer Init and Shutdown.
// Retail CodeView8101 proves void StopInstance(+36); this native subset is not a foreign vtable overlay.
class DsManager : public ScUnknown {
public:
  // MSVC reverses this overload group: layer Init occupies +16 and saved-city Init occupies +12.
  virtual bool Init(DsLayer *layer, CtCity *city) = 0;
  virtual bool Init(CtCity *city, RsDatabaseSegment *segment) = 0;
  virtual bool Shutdown() = 0;                                     // Original +20.
  virtual bool Save(CtCity *city, RsDatabaseSegment *segment) = 0;
  virtual bool StaticInit() = 0;
  virtual bool StaticShutdown() = 0;
  // Authentic retail declaration supersedes the SDK Boolean transcription; original callers discard residual registers.
  virtual void StopInstance(DsInstance *) = 0;
  virtual bool AttemptDisasterStart() = 0;
  virtual bool Tick() = 0; // Original +44; retail CodeView preserves this original public method name.
};

class DsManager;
class ScOccupant;
struct GeCellRect;

// Original cISC3DisasterLayer::tTickType, recovered from retail CodeView80F0/80EF.
enum DsTickType : i32 { DsDayTick = 0, DsMinuteTick = 1 };

// Common disaster-instance interface IID22223BA4, with panic slots identified by authentic retail CodeView80F9.
// Native declarations retain the existing descriptive names for the other recovered operations.
class DsInstance : public ScUnknown {
public:
  virtual bool GetBounds(GeCellRect *output) = 0;
  virtual bool GetLocation(u32 *x, u32 *z) = 0;
  virtual ScOccupant *GetOccupant() = 0;
  virtual DsManager *GetManager() = 0;
  // Primary slot28 receives the original signed32-bit day/minute selector.
  virtual bool Tick(DsTickType tick) = 0;
  virtual u32 TypeId() = 0;
  virtual i8 PanicEffect(u32 distance) = 0;
  virtual u32 PanicRadius() = 0;
  virtual bool IsActive() = 0;
  virtual u32 GetDestroyedCount() = 0;
  virtual u32 GetCost() = 0;
};

class DsController;

// Shared twelve-byte original manipulator base: references+4 and borrowed controller+8. Derived destruction
// restores its32728 vtable, establishing this base independently of the compiler-folded interface methods.
class DsManipulator : public CtLayerManipulator {
public:
  DsManipulator();
  virtual ~DsManipulator();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Apply(CtAgent *agent) override;
  bool IsLockingCity() override;
  bool DebugInstanceTag(ScStringView *out) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  // Original private slot36 returns its input pointer in EAX; nominal source return spelling remains unresolved.
  virtual DsController *SetLayer(DsController *owner);

protected:
  u32 references;
  DsController *layer;
};

// Distinct32700 derived vtable uses the same base lifetime and dispatches a supplied agent's manager update.
class DsManagerManipulator final : public DsManipulator {
public:
  bool Apply(CtAgent *agent) override;
};

// Distinct326D8 and326B0 tables share the recovered base destructor and retain separate deferral semantics.
class DsActiveManipulator final : public DsManipulator {
public:
  bool Apply(CtAgent *agent) override;
};

class DsPendingManipulator final : public DsManipulator {
public:
  bool Apply(CtAgent *agent) override;
};

class DsManager;

// The original registry maps a disaster ID to a factory class ID and a manager interface. Entries are unique by type.
struct DsManagerEntry {
  u32 type, class_id;
  DsManager *manager;
};

// Native storage for the original global registry. Manager creation/registration is still unresolved;
// this allocation owns only entries, not manager interfaces, whose shutdown/release belongs to the original lifecycle.
extern ScArray<DsManagerEntry> DsManagers;

// Original cSC3BuildingStaticAnim adds no fields to the forty-four-byte building receiver.
// It uses the disaster DLL's concrete building copy and its own module reference/cache bindings.
class GeStaticAnimBuilding : public DsDisaster::OcBuilding {
public:
  GeStaticAnimBuilding();
  u8 AnimType() const override;
  void AnimStart() override;
  void AnimStop() override;
  i32 Tick(u32 elapsed) override;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class DsInstance;
class DsManipulator;
class DsAgent;
class GeAttrib;
class ScVariant;

// Original unsigned controller-counter limit, initialized to8.
extern u32 DsLayerCounterLimit;
extern u32 DsReliefMinimumCost;
extern u8 DsReliefPercentWithEw, DsReliefPercentWithoutEw;
class CtCity;
class CtAgentTree;
class DsManager;
class ScOccupant;
class EcBudgetLayer;

struct DsChangeListener {
  DsChangeListener *next, *previous;
  DsChangeCallback callback;
  void *context;
};

// Disaster-specific listener ownership replaces the original std::list, preserving stable nodes and insertion order.
class DsListeners {
public:
  DsListeners();
  ~DsListeners();
  void Clear();
  DsListeners(const DsListeners &) = delete;
  DsListeners &operator=(const DsListeners &) = delete;
  DsChangeListener anchor;
};

// Recovered controller with the proven public disaster and city-layer identities and original constructed state.
// Query strings and map-sprite factory retain their original third/fourth views without expanding QueryInterface.
class DsController : public DsLayer, public CtLayer, public ScQueryInfoString, public SpMapSpriteFactory {
  friend class DsActiveIterator;
  friend class DsAllIterator;
  friend class DsManipulator;
  friend class DsActiveManipulator;
  friend class DsPendingManipulator;

public:
  DsController();
  virtual ~DsController();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;

  bool DoMessage(ScMessage *message) override;
  bool Shutdown() override;
  bool StaticShutdown() override;
  bool StaticInit(CtDefinition *definition) override;
  bool SimulationBegin() override;
  bool SimulationEnd() override;
  bool GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) override;
  u32 GetLayerType() override;
  bool DoQueryInfo(ScMessage *message, ScUnknown *source) override;
  bool Init(CtCity *city, ScImporter *importer) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Init(CtCity *city) override;
  bool DebugClassTag(ScStringView *out) override;
  bool DebugTypeTag(ScStringView *out) override;
  u32 GetInfoStringCount(u32 kind) override;
  u8 GetInfoStrings(u32 first, u32 second, u32 third, u32 fourth, u32 *value, ScStringView *firstText,
      ScStringView *secondText) override;
  bool CreateMapSprite(u32 kind, u32 iid, void **out) override;
  u32 GetMapSpriteCount() override;
  bool GetSpriteIdFromIndex(u32 index, u32 *out) override;
  static bool GetVisibleRect(GeCellRect *bounds);
  static i32 IsInVisibleRect(u32 x, u32 z);
  // Original08AC7 consumes a const unsigned rectangle; nominal byte return spelling remains qualified.
  static u8 IntersectsVisibleRect(const GeCellRect &bounds);

  void DisasterInstanceChange(DsInstance *instance, i32 change) override;
  DsListIterator *CreateActiveDisasterIterator() override;
  void DeleteActiveDisasterIterator(DsListIterator *iterator) override;
  DsListIterator *CreateDisasterListIterator() override;
  void DeleteDisasterListIterator(DsListIterator *iterator) override;
  bool EnableDisasters(bool enabled) override;
  bool DisastersEnabled() override;
  u32 GetActiveDisasterCount() override;
  bool NotifyOnChange(ChangeCallback callback, void *context) override;
  bool StopNotifyingOnChange(ChangeCallback callback, void *context) override;
  DsManager *GetDisasterManager(u32 type) override;
  bool AttemptDisasterStart(u32 type);
  u32 GetDemolitionCost(ScOccupant *occupant) override;
  bool DestroyOccupant(ScOccupant *occupant, i32 cause, u32 *cost) override;
  bool IsSimulationRunning() override;
  void ComputeSurroundRect(const GeCellRect &input, GeCellRect *output, u32 distance) override;

protected:
  // Original private virtual76 updates nearby radioactive cells; its ignored nominal return remains unresolved.
  virtual void MarkRadioactivity(u32 x, u32 z);
  // Original private virtual80 takes the genuine rectangle by value and returns an acquired variant.
  virtual ScVariant *GetVariantValue(ScRect bounds);
  // Original private replacement helpers retain their separate arguments and byte-result carriers.
  u8 ReplaceBuilding(u32 x, u32 z, u32 rotation, GeAttrib *attributes, u32 state);
  u8 ReplacePlant(u32 x, u32 z, u32 kind, u32 state);

  // Guarded dispatches use the canonical service interfaces verified from their installed producers.
  bool DispatchFire(u32 x, u32 z);
  bool DispatchPolice(u32 x, u32 z);
  // Remaining private candidates preserve defined results/effects; historical nominal spellings stay unresolved.
  bool CanDispatchPlane(u32 x, u32 z);
  void StartCounter(u32 initial);
  void StartCounterAtZero();
  void AdvanceCounter();
  void EndCounter();
  // Complete relief side effects; the ignored private nominal return remains unresolved.
  void GrantRelief();
  void NotifyListeners(DsInstance *instance, i32 change);
  // Candidate consumer: its ignored private source return spelling remains unresolved.
  void ProcessChanges();
  // Private dispatch/registration returns are ignored by original callers; nominal declarations remain unresolved.
  void ApplyChange(DsInstance *instance, i32 change);
  void RegisterPending(DsInstance *instance);
  // Activation nominal return remains unresolved; removal preserves its defined true/false result.
  void ActivateInstance(DsInstance *instance);
  bool RemoveInstance(DsInstance *instance);
  // Byte inputs gate reference-counted menu enablement; private nominal bool/BYTE spellings remain unresolved.
  void UpdateGotoCount(u8 add);
  void UpdateFireDispatchCount(u8 add);
  void UpdatePoliceDispatchCount(u8 add);
  void UpdatePlaneDispatchCount(u8 add);

  u32 references; // Original complete+16, shared by every interface view.
  // Original set owners+20/+32 precede the random receiver and remaining allocating members.
  DsInstances pending_instances, active_instances;
  CtAgentTree *agent_types;  // Original complete+44.
  bool initialized, enabled; // Original complete+48/+49.
  CtCity *city;              // Original complete+52.
  u32 maximum_x, maximum_z;  // Original+56/+60, unwritten until city binding.
  DsRandom random;           // Original+64, constructed before listener and queued-change storage.
  bool flag_88;              // Relief selector; INI labels distinguish WithEW/WithoutEW, expansion unresolved.
  CtLayerManipulator *manipulator_92, *manipulator_96; // Unwritten until manipulator binding.
  ScOccupantManager *occupants;                        // Original complete+100, retained surface manager.
  AdPublicSafety *public_safety;                       // Original complete+108.
  // Genuine QueryInterface destinations publish directly during callbacks through the external COM ABI.
  void *fire_service, *police_service;
  EcBudgetLayer *budget; // Original complete+120, retained city BudgetLayer348.
  // These acquired services expose only their proven Unknown lifetime prefix in recovered methods.
  ScUnknown *building_layer, *demolition;
  void *service_132;
  u32 goto_count, fire_dispatch_count, police_dispatch_count, plane_dispatch_count; // Original136..148.
  CtLayerManipulator *manipulator_152, *manipulator_156; // Unwritten until manipulator binding.
  DsListeners listeners; // Original list head+160; native nodes preserve stable callback/context identity.
  bool flag_164;
  u32 counter_requests;                  // Original+168, incremented only for an empty controller.
  ApBoxX *box;                           // Original+176, borrowed through application GetBoxX52.
  u32 cost_total;                        // Original+180.
  u32 counter_184;                       // Original constructor leaves this guarded counter unwritten.
  DsAgent *simulation_agent;             // Original+188, actual shared active-instance agent.
  bool agent_registered;                 // Original+192, simulator AddAgent result.
  DsAgent *change_agent;                 // Original+196, distinct change-processing agent.
  bool change_registered, defer_changes; // Original+200/+201; defer flag is guarded by the change lock.
  DsChangeQueue changes;                 // Original list head+204; actual instance retention occurs before insertion.
  u32 word_208;                          // Original constructor writes all ones; later purpose remains unresolved.
  u8 counter_on_calendar;                // Original+212, raw byte gate.
  u32 counter_start_value;               // Original+216, forwarded to StartCounter by calendar messages.
  bool simulation_running;               // Original+220.
  ScLock change_lock;                    // Original recursive critical-section receiver+224, constructed last.
};

class CtCity;
class CtAgentTree;
class CtStartSimLayer;
class CtZoneLayer;
class ScOccupantManager;
class ApBoxX;
class DsEventInstance;

extern i32 DsEventMaximumActive;
extern u32 DsEventMinimumMonths, DsEventChance;
extern u32 DsEventMinimumStrength, DsEventMaximumStrength;
extern u32 DsEventFirstDelay, DsEventSecondDelay, DsEventThirdDelay, DsEventAttackDelay;
// Original demolition thresholds are hundredths of a percent, not cell radii.
extern u32 DsEventDemolitionChances[3];

// Actual32-byte saved state of instance13873257, distinct from similarly laid-out sibling records.
struct DsEventRecord {
  u32 serial, magnitude, destroyed, cost, preparedness, packed_location, phase, phase_ticks;
};

static_assert(sizeof(DsEventRecord) == 32);

// Actual04889EE9 manager-private identity; nominal private byte declarations remain unresolved.
class DsEventControl : public ScUnknown {
public:
  virtual u32 GetRecords(DsEventRecord *records, u32 *count) const = 0;
  virtual u8 GetFirstRecord(DsEventRecord *record) const = 0;
  virtual u8 Start(f32 strength, u8 automatic) = 0;
};

// Original Smite manager: factory52FE8D53/private04889EE9 match the exact Linux cSC3SmiteDisasterManager query.
// The trigger excludes its original unwritten-position path at an explicit nonreturning native boundary.
class DsEventManager : public DsAgent, public DsManager, public DsEventControl, public ScMessageTarget {
  friend class DsEventInstance;

public:
  DsEventManager();
  ~DsEventManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Shutdown() override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool DoMessage(ScMessage *message) override;
  void StopInstance(DsInstance *instance) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool AttemptDisasterStart() override;
  bool Tick() override;
  u32 GetRecords(DsEventRecord *records, u32 *count) const override;
  u8 GetFirstRecord(DsEventRecord *record) const override;
  // Defined capacity refusal only; successful selection leaves original coordinates unwritten.
  u8 Start(f32 strength, u8 automatic) override;

protected:
  // Complete effects; ignored original private nominal return declarations remain unresolved.
  void SeedHistory();
  void InstanceEnded();
  void RecordInstance(DsEventInstance *instance);
  void FinishInstance(DsEventInstance *instance);
  // Original leaf returns one without writing this genuine output; do not fabricate a start position.
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
  ScArray<DsEventRecord> history, pending;
};

class ScString;

class ScOccupant;
class ApBoxX;
class CtCity;
class CtAgentTree;
class CtFireLayer;
class GeBuildingLayer;
class ScOccupantManager;
class OcIteratorTest;
class DsLayer;
class DsFireInstance;
class EnHazardMap;

// Fire-disaster timing tunables at original/. The installed data supplies these defaults;
// StaticInit loads their FireDisaster properties through the original variable manager.
extern i32 DsFireMinimumInterval, DsFireMaximumInterval;
// Original FireDisaster description words3A728/3A72C.
extern u32 DsFireMaximumInstances;
extern i32 DsFireUiId;

// Original saved ignition command is twelve bytes, with a signed final mode word.
struct DsFireRecord {
  u32 x, z;
  i32 mode;
};

// Proven private manager identity634FD3BC; both operations preserve the original separate cell-coordinate arguments.
class DsFireControl : public ScUnknown {
public:
  virtual u8 IsCellBurning(u32 x, u32 z) = 0;
  virtual u8 IgniteCell(u32 x, u32 z) = 0;
};

// Original fire predicate borrows the manager's currently bound hazard interface through this module alias.
extern EnHazardMap *DsFireFlammability;
// Original396AC flora-preference percentage, statically initialized to ten.
extern u32 DsFireFloraChance;
// Original duration/spread tuning words39684..396A8 and lower spread bounds3A798..3A7A0.
extern i32 DsFireMinimumDuration, DsFireMaximumDuration;
extern f32 DsFireDurationScale;
extern u8 DsFireMinimumHazard, DsFireOuterHazard;
extern i32 DsFireMinimumSpread[3], DsFireMaximumSpread[3];
extern f32 DsFireSpreadScale[3];
// Private coefficient-update return declarations are unresolved; original callers consume only these effects.
void DsUpdateFireDurationScale();
void SC3_STDCALL DsUpdateFireSpreadScale(u32 index);

// Fire manager with recovered lifecycle, static tuning, persistence and ignition. Specialized sprite production
// remains with SIMSPR; these native identities are not a foreign layout.
class DsFireManager : public DsAgent, public DsManager, public DsFireControl, public ScMessageTarget {
  friend class DsFireInstance;

public:
  DsFireManager();
  ~DsFireManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool DoMessage(ScMessage *message) override;
  bool AttemptDisasterStart() override;
  u8 IsCellBurning(u32 x, u32 z) override;
  u8 IgniteCell(u32 x, u32 z) override;
  bool Tick() override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  // Original5E59 occupant/mode operands and byte result; private nominal source spellings remain unresolved.
  u8 IgniteOccupant(ScOccupant *occupant, i32 mode);

protected:
  // Private callers discard these returns; original nominal return declarations remain unresolved.
  void InstanceStarted();
  void InstanceEnded();
  // Release-count transport is observed; the private nominal return and destruction-byte spelling remain unresolved.
  u32 FinishInstance(DsFireInstance *instance, u8 destroy);

  // Original private byte results and separate mode argument; nominal bool/BYTE spelling remains unresolved.
  u8 IgniteCell(u32 x, u32 z, i32 mode);
  u8 IgniteRandom();
  u8 IgniteKind(u32 ancestor);
  u8 SpreadFrom(ScOccupant *occupant, u32 chance);
  bool initialized;              // Original24.
  DsLayer *layer;                // Original28.
  CtCity *city;                  // Original32.
  CtAgentTree *agent_types;      // Original36, unwritten until city binding.
  GeBuildingLayer *buildings;    // Original40, unwritten until city binding.
  ScUnknown *demolition;         // Original44, optional retained Unknown prefix.
  ApBoxX *box;                   // Original48.
  ScOccupantManager *occupants;  // Original52.
  DsRandom random;               // Original56, constructed before native collection storage.
  OcIteratorTest *filter;        // Original80, owned fire-specific predicate.
  u32 instance_count;            // Original84; original decrement uses a signed positive test.
  u32 camera_count;              // Original88, unwritten until city binding.
  u32 maximum_x, maximum_z;      // Original92/96, unwritten until city binding.
  CtFireLayer *fire_service;     // Original100, unwritten until city binding.
  EnHazardMap *flammability;     // Original104, retained view2144802B.
  ScArray<ScOccupant *> burning; // Original108 pointer-key set; keys are borrowed, not retained.
  ScArray<DsFireRecord> pending; // Original120, separate saved ignition commands.
  i32 elapsed, interval;         // Original132/136.
};

// Original description-row callback; the numeric key is consumed but does not select a row.
void DsFireReadEntry(const ScString &key, const ScString &value, void *context);

class DsFireManager;

// The proven fire-instance primary identity has no additional base subobjects. Native fields describe game state;
// original offsets refer to the installed 64-byte receiver rather than this C++ object's layout.
class DsFireInstance : public DsInstance {
  friend class DsFireManager;

public:
  DsFireInstance();
  ~DsFireInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  // Original progression preserves the ignored day/minute selector.
  bool Tick(DsTickType tick) override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  // Complete private teardown; AL1 is proven but nominal bool/BYTE spelling remains unresolved.
  bool Shutdown();
  // Original65E4 has three separate operands and raw AL1; the specialized producer keeps its own module identity.
  u8 Init(DsFireManager *manager, ScOccupant *occupant, i32 mode);

protected:
  // Complete camera effects; the ignored private nominal return remains unresolved.
  void RefreshVisibility();
  bool initialized;       // Original +4.
  DsFireManager *manager; // Original +8, borrowed from initialization.
  ScOccupant *occupant;   // Original +12, retained by initialization.
  u32 x, z;               // Original +16/+20.
  void *effect;           // Original +24; actual COM output destination, consumed as SpFireSimulator primary0223BE6C.
  i32 mode;               // Original +28; private word carrier, nominal declaration unresolved.
  u8 cycles;              // Original+32, a wrapping cycle counter tested against ten.
  i32 elapsed, duration;  // Original +36/+40; word carriers pending progression signedness proof.
  u32 references;         // Original +44.
  bool in_camera;         // Original +48.
  u32 camera_ticks;       // Original+52, starts at3 and refreshes visibility every third tick.
  bool destroyed;         // Original +56, common count reports0/1.
  u32 cost;               // Original +60.
};

// Actual04889EE8 private identity; nominal floating/word/byte source declarations remain unresolved.
class DsEventState : public ScUnknown {
public:
  virtual f64 GetMagnitude() const = 0;
  virtual u8 GetSeverity() const = 0;
  virtual i32 GetPosition(i32 *x, i32 *y, i32 *z) const = 0;
  virtual void GetRecord(DsEventRecord *record) = 0;
};

// Actual132-byte instance with primary/private/message0/4/8, independent of the track and water class layouts.
// Word96 is a tested scalar with no proven pointer ownership; retain its original behavior without inventing an effect.
class DsEventInstance final : public DsInstance, public DsEventState, public ScMessageTarget {
  friend class DsEventManager;

public:
  DsEventInstance();
  ~DsEventInstance();
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
  void GetRecord(DsEventRecord *record) override;
  // Actual four separate operands or saved-record pair; private nominal bool/BYTE declarations remain candidates.
  u8 Init(DsEventManager *manager, f32 strength, const ScVec3I *position, u8 automatic);
  u8 Init(DsEventManager *manager, const DsEventRecord *record);
  u8 Shutdown();

protected:
  u32 ConfigureMagnitude(f32 strength);
  void ApplyPreparedness(u8 enabled);
  void ClearEffects();
  void PostPhase(u32 phase);
  bool initialized;
  u32 timing[4], references;
  bool announcement;
  u32 effects[5];
  // The second level byte is reduced by preparedness; its spatial interpretation is not proved.
  u8 severity, adjusted_level;
  u32 demolition_chance;
  ScVec3I position;
  u32 word_80, columns, rows;
  DsEventManager *manager;
  u32 word_96;
  DsEventRecord state;
};

#endif
