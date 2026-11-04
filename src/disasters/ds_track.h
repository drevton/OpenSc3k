#ifndef SC3_DISASTERS_DS_TRACK_H
#define SC3_DISASTERS_DS_TRACK_H

#include "common/api.h"
#include "common/rect.h"
#include "common/sc3_array.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"

class ApBoxX;
class DsImpactInstance;
class ScDirtBag;
class CtZoneLayer;
class CtAgentTree;
class CtStartSimLayer;
class ScOccupantManager;

// Original automatic-start and strength tuning at39A88/39A8C/39A94/39A98/39A9C/39AC4.
extern i32 DsImpactMaximumActive, DsImpactMinimumMonths;
extern u32 DsImpactMinimumStrength, DsImpactMaximumStrength, DsImpactChance, DsImpactMinimumYear;
// Original3A9C4/39AA0..39AB0 absolute phase deadlines and average debris interval.
extern u32 DsImpactFirstDelay, DsImpactSecondDelay, DsImpactThirdDelay;
extern u32 DsImpactAttackDelay, DsImpactEndDelay, DsImpactAverageDebrisTime;
// Original39AB4/39AB8 particle-only and damage thresholds before magnitude adjustments.
extern u32 DsImpactParticleChance, DsImpactDamageChance;
// Original39ABC lifetime passed to the real space-debris impact display.
extern u32 DsImpactDisplayLifetime;

// Proven private manager identity at original complete+16, IID24889F6F. Additional slots remain unported.
class DsImpactControl : public ScUnknown {
public:
  virtual u32 GetRecords(DsImpactRecord *records, u32 *count) const = 0;
  virtual bool GetFirstRecord(DsImpactRecord *record) const = 0;
  virtual u8 Start(f32 strength, u8 automatic) = 0;
  // Original private24=107A9 returns x87 zero/one; f64 is a lossless carrier, not proved float/double spelling.
  virtual f64 ActiveCountIsPositive() = 0;
};

// Manager for instance type13873256/factory52FE8D52 with recovered lifecycle, tuning and persistence.
// Impact remains descriptive: the human disaster name and specialized instance progression are unresolved.
class DsImpactManager : public DsAgent, public DsManager, public DsImpactControl, public ScMessageTarget {
  friend class DsImpactInstance;

public:
  DsImpactManager();
  ~DsImpactManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool DoMessage(ScMessage *message) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool AttemptDisasterStart() override;
  bool Tick() override;
  // Full start body is recovered; private nominal byte/flag declarations remain unresolved candidates.
  u8 Start(f32 strength, u8 automatic) override;
  f64 ActiveCountIsPositive() override;
  u32 GetRecords(DsImpactRecord *records, u32 *count) const override;
  bool GetFirstRecord(DsImpactRecord *record) const override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;

protected:
  // Folded with DsTrackManager::InstanceEnded; the private nominal return remains unresolved.
  void InstanceEnded();
  // Both callers ignore this private return; its original nominal declaration remains unresolved.
  void SeedHistory();
  // Direct record capture and final removal preserve ignored private return candidates and callback ordering.
  void RecordInstance(DsImpactInstance *instance);
  void FinishInstance(DsImpactInstance *instance);
  bool initialized, automatic_start; // Original complete+24/+25.
  DsLayer *layer;                    // Original complete+28.
  ScUnknown *demolition;             // Original complete+32; specialized demolition contract remains unresolved.
  ScDirtBag *terrain;                // Original complete+36.
  CtZoneLayer *zones;                // Original complete+40.
  CtStartSimLayer *simulation;       // Original complete+44; retained start-simulation interface.
  CtAgentTree *agent_types;          // Original complete+48; retained application agent tree.
  CtCity *city;                      // Original complete+52.
  ScOccupantManager *occupants, *underground_one, *underground_two; // Original complete+56/+60/+64.
  ApBoxX *box;                                                      // Original complete+68.
  bool active;      // Original complete+72; following padding remains unwritten.
  DsRandom random;  // Original complete+80.
  u32 active_count; // Original complete+104.
  u16 flags_108;
  ScArray<DsImpactRecord> history, pending; // Original complete+112/+124, actual24-byte record vectors.
};

class DsImpactManager;

// Proven private state identity at original complete+4, IID24889F6E. The folded magnitude getter occupies slot12;
// its nominal floating return spelling remains unresolved and this native interface is not a foreign layout.
class DsImpactState : public ScUnknown {
public:
  virtual f64 GetMagnitude() const = 0;
  virtual u8 GetSeverity() const = 0;
  virtual void GetRecord(DsImpactRecord *output) = 0;
};

// Proven128-byte instance with common, private-state and message-target identities. The descriptive name does not
// assert its still-unproved human disaster name; initialization and phases call the real foreign debris display.
class DsImpactInstance : public DsInstance, public DsImpactState, public ScMessageTarget {
  friend class DsImpactManager;

public:
  DsImpactInstance();
  ~DsImpactInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  f64 GetMagnitude() const override;
  u8 GetSeverity() const override;
  void GetRecord(DsImpactRecord *output) override;
  // AL1 and complete teardown body are proven; private nominal bool/BYTE spelling remains unresolved.
  u8 Shutdown();
  // Restore through the real live initializer and virtual phase progression, preserving caller-record rereads.
  bool Init(DsImpactManager *owner, const DsImpactRecord *saved);
  // Three live-init arguments are proven; private nominal bool/BYTE spellings remain unresolved.
  bool Init(DsImpactManager *owner, f32 strength, u8 automatic);
  // Original primary28 receives the original day/minute enum; this implementation ignores that selector.
  bool Tick(DsTickType tick) override;

protected:
  // One float input and no consumed result; private nominal return remains unresolved.
  void ConfigureMagnitude(f32 strength);
  // The five-handle cleanup loop is recovered; its ignored private nominal return remains unresolved.
  void ClearEffects();
  // Synchronous phase notice effects are recovered; the ignored private nominal return remains unresolved.
  void PostPhase(u32 state);
  // Complete pattern body; private nominal coordinate/byte spellings remain unresolved.
  u8 ApplyPattern(i32 x, i32 z, u32 message);
  // Whole1224A uses the real impact-display factory; private nominal bool/BYTE spelling remains unresolved.
  u8 EmitGroundEffect(i32 x, i32 z, f32 size);
  // Three stack words are proven; the unused third argument and private nominal byte return remain candidates.
  u8 EmitTileParticles(i32 x, i32 z, f32 size);
  // Two cell-coordinate words and the low-byte result are proven; nominal private spellings remain candidates.
  u8 IgniteAtTile(i32 x, i32 z);
  // Three original inputs and all height changes are recovered; private nominal byte return remains a candidate.
  u8 DeformTerrain(i32 x, i32 z, f32 size);
  // Inclusive clipped scan and six original tally arguments; private nominal byte spellings remain candidates.
  u8 ClearPatternArea(const ScRect *bounds);
  // Complete three-layer destruction and genuine occupancy output; private nominal byte spellings remain candidates.
  u8 TallyTile(i32 x, i32 z, u8 *occupied, u8 destroy, u8 clear_zone, u8 special);
  // Original five stack arguments and outputs; nominal rectangle/clipped flag and return spellings remain unresolved.
  u8 ComputePatternBounds(i32 x, i32 z, f32 size, ScRect *bounds, u8 *clipped);
  bool initialized;                                         // Original complete+12.
  u32 word_16, word_20, word_24, word_28, word_32, word_36; // Constructor-written carriers, meanings unresolved.
  u32 references;                                           // Original complete+40.
  bool announcement;                                        // Original complete+44.
  u32 location_x, location_z; // Original complete+48/+52, initially invalid all-ones values.
  u32 effects[5];             // Original complete+56, contiguous five-slot handle array.
  u8 severity, word_77;       // Original complete+76/+77; only severity is constructor-written.
  u32 word_80, word_84;       // Original constructor clears both; remaining meanings unresolved.
  u32 columns, rows;          // Original complete+88/+92, unwritten until initialization.
  DsImpactManager *manager;   // Original complete+96, borrowed owner.
  void *effect;               // Original complete+100, actual external COM output destination.
  DsImpactRecord record;      // Original complete+104.
};

class CtCity;
class ApBoxX;
class DsTrackInstance;
class DsLayer;
class ScDirtBag;
class CtZoneLayer;
class CtAgentTree;
class CtStartSimLayer;
class ScOccupantManager;

// Original scalar tuning groups and; their game units remain unresolved.
extern i32 DsTrackTuning64[3], DsTrackTuning68[3];
// Automatic-start properties at original/24/34; installed defaults are1,16,83.
extern i32 DsTrackMaximumActive, DsTrackMinimumMonths, DsTrackChance;
// Original property words3952C/39530: interpolate their unsigned difference, then add the signed minimum.
extern u32 DsTrackMinimumStrength, DsTrackMaximumStrength;
// Original phase properties at3A6B0/3958C/39590/39594, installed defaults0/60/120/180.
extern u32 DsTrackPhaseDurations[4];
// Original path-effect Start arguments: phase three uses39578/3A6AC/39584/39588; phase five uses39560/3956C.
extern i32 DsTrackPhaseThreeDuration[3], DsTrackPhaseThreeArgument[3];
extern i32 DsTrackPhaseFiveDuration[3], DsTrackPhaseFiveArgument[3];
// Original border radii39550/554/558 for widths1/2/other-positive, installed values2/4/6.
extern i32 DsTrackBorderRadii[3];
// Original terrain band deltas395A4/395B0; severity selects1/2/3 slots after the signed early gate.
extern i32 DsTrackOuterDeltas[3], DsTrackInnerDeltas[3];

// Original nine-word saved/history record, copied at instance+10388 and stored as 36-byte manager records.
struct DsTrackRecord {
  u32 date, magnitude, destroyed, cost, automatic, packed_endpoints, packed_location, phase, phase_ticks;
};

// Track-specific private state identity, IID. The tornado's same numeric IID has a different vtable;
// original methods keep their separate outputs; nominal floating and coordinate-return spellings remain qualified.
class DsTrackState : public ScUnknown {
public:
  virtual f64 GetMagnitude() const = 0;
  virtual u8 GetSeverity() const = 0;
  virtual i32 GetPosition(i32 *x, i32 *y, i32 *z) const = 0;
  virtual i32 GetEndpoints(i32 *first_x, i32 *first_y, i32 *first_z, i32 *last_x, i32 *last_y, i32 *last_z) const = 0;
  virtual void GetRecord(DsTrackRecord *output) = 0;
};

// Original specialized manager identity at complete+16, IID. Its history queries and trigger retain
// their original separate arguments; native vtables are not installed-binary overlays.
class DsTrackControl : public ScUnknown {
public:
  virtual u32 GetRecords(DsTrackRecord *records, u32 *count) const = 0;
  virtual bool GetFirstRecord(DsTrackRecord *record) const = 0;
  virtual bool Start(f32 strength, bool automatic) = 0;
};

// Moving-swath manager with original root, common, specialized and message identities. Static tuning and its real
// factory are recovered; specialized foreign effect production remains explicit.
class DsTrackManager : public DsAgent, public DsManager, public DsTrackControl, public ScMessageTarget {
  friend class DsTrackInstance;

public:
  DsTrackManager();
  ~DsTrackManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Shutdown() override;
  void StopInstance(DsInstance *instance) override;
  bool DoMessage(ScMessage *message) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool ChooseEndpoints(ScVec3I *start, ScVec3I *end, ScVec3I *current);
  void InstanceEnded();
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool AttemptDisasterStart() override;
  bool Tick() override;
  bool Start(f32 strength, bool automatic) override;
  u32 GetRecords(DsTrackRecord *records, u32 *count) const override;
  bool GetFirstRecord(DsTrackRecord *record) const override;

protected:
  // SeedHistory is a candidate with no consumed return; its private source return spelling remains unresolved.
  void SeedHistory();
  // Ignored private returns remain unresolved; preserve the observed record/lifetime callback order.
  void RecordInstance(DsTrackInstance *instance);
  void FinishInstance(DsTrackInstance *instance);
  i32 RepairZones(); // Complete count body; private nominal signedness remains unresolved.

  bool initialized, automatic_start; // Original complete+24/+25.
  DsLayer *layer;                    // Complete+28.
  ScUnknown *demolition;             // Complete+32; only its proven Unknown lifetime prefix is represented.
  ScDirtBag *terrain;                // Complete+36.
  CtZoneLayer *zones;                // Complete+40.
  CtStartSimLayer *simulation;       // Original complete+44; retained start-simulation interface.
  CtAgentTree *agent_types;          // Original complete+48; retained application agent tree.
  CtCity *city;                      // Complete+52.
  ScOccupantManager *occupants, *underground_one, *underground_two; // Complete+56/+60/+64.
  ApBoxX *box;                                                      // Complete+68.
  bool active, flag_73; // Complete+72/+73; the unobserved word at+76 remains absent from the native representation.
  DsRandom random;      // Complete+80, constructed after retained pointers and activity flags.
  u32 active_count;     // Complete+104.
  u16 flags_108;
  ScArray<DsTrackRecord> history, pending; // Complete+112/+124, separately owned record vectors.
};

// Original primary, motion and message-target identities are proven by constructor/QI.
// Initialization, lifetime, phase advancement and reached terrain/occupant updates are recovered.
class DsTrackInstance : public DsInstance, public DsTrackState, public ScMessageTarget {
  friend class DsTrackManager;

public:
  DsTrackInstance();
  ~DsTrackInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Shutdown();
  bool BuildPath();
  // Original virtual28 ignores one four-byte stack operand; its nominal source type remains unresolved.
  bool Tick(DsTickType tick) override;
  // Both inputs and AL1 are proven; the restored body is a candidate with unresolved nominal bool/BYTE return.
  bool Init(DsTrackManager *manager, const DsTrackRecord *record);
  // Six live-start stack arguments and body are recovered; nominal private bool/BYTE return remains unresolved.
  bool Init(DsTrackManager *manager, f32 strength, const ScVec3I *start, const ScVec3I *end, const ScVec3I *current,
      bool automatic);
  void GetRecord(DsTrackRecord *output) override;
  f64 GetMagnitude() const override;
  u8 GetSeverity() const override;
  i32 GetPosition(i32 *x, i32 *y, i32 *z) const override;
  i32 GetEndpoints(
      i32 *first_x, i32 *first_y, i32 *first_z, i32 *last_x, i32 *last_y, i32 *last_z) const override;
  bool DoMessage(ScMessage *message) override;
  bool GetBounds(GeCellRect *output) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;

protected:
  // Private return spelling is unresolved; preserve the observed EAX word rather than inventing a status result.
  i32 ConfigureMagnitude(f32 strength);
  void AdjustWidth(bool is_automatic);
  // Both original callers ignore the return; its private nominal spelling remains unresolved.
  void ClearEffects();
  // Synchronous phase notice effects are recovered; the ignored private nominal return remains unresolved.
  void PostPhase(u32 state);
  // Candidate byte-returning private operations; nominal bool/BYTE spellings remain unresolved.
  u8 ScatterTiles();
  u8 ApplyTile(i32 x, i32 z, u8 destroy);
  // Two original stack words are unused; private nominal input and bool/BYTE result spellings remain unresolved.
  u8 RenderSwath(i32 first, i32 second);
  bool HasAdjacentOccupant(i32 x, i32 z);
  // Complete neighbor effects; private nominal bool/BYTE return spellings remain unresolved.
  u8 ClearAdjacentZones(i32 x, i32 z);
  u8 TallyAdjacentTiles(i32 x, i32 z);
  // Complete3E56 prior-occupancy result and destruction effects; private word/flag source spellings remain unresolved.
  u8 TallyTile(i32 x, i32 z, i32 first, i32 second, u8 special);

  bool initialized;                   // Original +12.
  u32 phase_durations[4], references; // Original +16/+32.
  bool announcement;                  // Original +36.
  u32 effects[5];                     // Original +40, invalid all-ones handles.
  u8 severity;                        // Original +60; width+61 remains unwritten by construction.
  i8 width;
  i32 tuning_64, tuning_68;                          // Original severity-dependent tuning words, units unresolved.
  ScVec3I start, end, current;                       // Original72/84/96: actual triples passed to the path effect.
  bool horizontal;                                   // Original +108.
  u32 path_cursor;                                   // Original +112.
  ScPoint path[1281];                                // Original +116; unused points remain uninitialized.
  u32 base_count, total_count, maximum_x, maximum_z; // Original +10364..+10376.
  DsTrackManager *manager;                           // Original +10380.
  void *effect; // Original +10384; external COM output publishes its canonical primary interface directly.
  u32 date, magnitude, destroyed, cost;             // Original +10388..+10400.
  u32 automatic, packed_endpoints, packed_location; // Original +10404..+10412.
  u32 phase, phase_ticks;                           // Original +10416/+10420.
};

#endif
