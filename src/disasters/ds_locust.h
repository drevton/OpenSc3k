#ifndef SC3_DISASTERS_DS_LOCUST_H
#define SC3_DISASTERS_DS_LOCUST_H

#include "common/api.h"
#include "common/sc3_array.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"

class ScDirtBag;
class CtAgentTree;
class CtStartSimLayer;
class ScCheatCodeManager;
class ScOccupantManager;
class ApBoxX;
class DsLocustInstance;

// Original automatic-start tuning at397D0/397D4/397D8/397DC. Counter cadence remains unresolved.
extern i32 DsLocustMaximumActive;
extern u32 DsLocustMinimumDelay, DsLocustMaximumDelay, DsLocustChance;
// Remaining INI words retain their original raw DWORDs until each progression consumer is reconstructed.
extern u32 DsLocustSwarmSpeed, DsLocustFirstNoticeTime, DsLocustSecondNoticeTime, DsLocustThirdNoticeTime;
extern u32 DsLocustAttackTime, DsLocustMinimumSwarms, DsLocustMaximumSwarms, DsLocustSwarmCropRatio;
extern u32 DsLocustCheatSwarmMaximum, DsLocustFirstVisitationYear, DsLocustMaximumCrops;

// Original history/pending records contain the attack date/count and persisted phase state.
// Words8 and20 have no recovered meaning and remain explicit unsigned storage.
struct DsLocustRecord {
  u32 date, swarm_count, word_8, phase, phase_ticks, word_20;
};

static_assert(sizeof(DsLocustRecord) == 24);

// Proven private identityC5326356 at complete+16. Recovered record, trigger and crop operations are represented here;
// the remaining private methods stay unported and this native subset is not an installed vtable overlay.
class DsLocustControl : public ScUnknown {
public:
  virtual u32 GetRecords(DsLocustRecord *records, u32 *count) const = 0;
  virtual u8 Start(u8 automatic) = 0;          // Private +16; nominal byte declaration remains a candidate.
  virtual bool IsCropAtCell(u32 x, u32 z) = 0; // Private +20; retained occupant and specialized building flags.
};

// Class52FE8D51 is bound directly by constructorA325's common vtable to the LocustDisasterTunables loaderAA59.
// Swarm progression and active instance teardown remain explicit unimplemented dependencies.
class DsLocustManager : public DsAgent, public DsManager, public DsLocustControl, public ScMessageTarget {
  friend class DsLocustInstance;

public:
  DsLocustManager();
  ~DsLocustManager() override;
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init(DsLayer *layer, CtCity *city) override;
  bool Shutdown() override;
  bool Save(CtCity *city, RsDatabaseSegment *segment) override;
  bool Init(CtCity *city, RsDatabaseSegment *segment) override;
  bool StaticInit() override;
  bool StaticShutdown() override;
  bool Tick() override;
  bool AttemptDisasterStart() override;
  bool DoMessage(ScMessage *message) override;
  void StopInstance(DsInstance *instance) override;
  u8 Start(u8 automatic) override;
  bool IsCropAtCell(u32 x, u32 z) override;
  void FindCropCells(ScVec3U *cells, u32 *count, u32 capacity);
  // Count and copied24-byte elements are proven; the private nominal declaration remains a candidate.
  u32 GetRecords(DsLocustRecord *records, u32 *count) const override;

protected:
  // Both original finish helpers discard their results; private nominal return declarations remain unresolved.
  void RecordInstance(DsLocustInstance *instance);
  void FinishInstance(DsLocustInstance *instance);
  // Original notice counters have no consumed result; private nominal void returns remain candidates.
  void ChangeNotice235(u8 enabled);
  void ChangeNotice235Mode(u8 enabled);
  void ChangeNotice233Mode(u8 enabled);
  void EndNotice233();
  void BeginNotice233();
  bool initialized;             // Original complete+24.
  u8 automatic_start;           // Original complete+25; private trigger temporarily replaces the raw byte.
  i32 elapsed_updates;          // Original complete+28; incremented only while eligible and idle.
  DsLayer *layer;               // Original complete+32.
  ScUnknown *demolition;        // Original complete+36; specialized interface remains unresolved.
  ScDirtBag *terrain;           // Original complete+40, unwritten until city binding.
  CtStartSimLayer *simulation;  // Original complete+44, unwritten until city binding.
  ScCheatCodeManager *cheats;   // Original complete+48, unwritten until city binding.
  CtAgentTree *agent_types;     // Original complete+52, unwritten until city binding.
  CtCity *city;                 // Original complete+56.
  ScOccupantManager *occupants; // Original complete+60.
  DsRandom random;              // Original complete+64.
  u32 active_count;             // Original complete+88.
  u32 notice_233_count, notice_233_mode_count, notice_235_count,
      notice_235_mode_count; // Original complete+92/+96/+100/+104.
  ApBoxX *box;               // Original complete+108.
  u16 flags_112;
  ScArray<DsLocustRecord> history, pending; // Original complete+116/+128; word140 remains unwritten/unmodeled.
};

#endif
