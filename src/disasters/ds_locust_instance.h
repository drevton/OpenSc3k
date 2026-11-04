#ifndef SC3_DISASTERS_DS_LOCUST_INSTANCE_H
#define SC3_DISASTERS_DS_LOCUST_INSTANCE_H

#include "common/rect.h"
#include "common/thread.h"
#include "disasters/ds_events.h"
#include "disasters/ds_locust.h"

struct GeCellRect;

// Cell nodes retain a dispatched airplane and placed object beside their two cell coordinates.
// The anchor owns only links; its unused payload is never initialized or read.
struct DsLocustLink {
  DsLocustLink *next, *previous;
  ScOccupant *airplane;
  u32 x, z;
  ScUnknown *object;
};

// Keep the cell list owner before the auxiliary vector and lock so native destruction retains their original order.
class DsLocustCells {
public:
  DsLocustCells();
  ~DsLocustCells();
  DsLocustCells(const DsLocustCells &) = delete;
  DsLocustCells &operator=(const DsLocustCells &) = delete;
  DsLocustLink *anchor;
};

// The proven private identity is complete+4, with history/count/record operations at original12/16/20.
class DsLocustState : public ScUnknown {
public:
  virtual u32 GetRecords(DsLocustRecord *records, u32 *count) const = 0;
  virtual u32 GetCount() = 0;
  virtual void GetRecord(DsLocustRecord *output) = 0;
};

// ConstructorB8C4 is called directly by the locust manager's AttemptA781. Its list/vector and lock are real
// instance owners; live queries and phases call the actual swarm, with target distribution and cell cleanup still explicit.
class DsLocustInstance : public DsInstance, public DsLocustState, public ScMessageTarget {
  friend class DsLocustManager;

public:
  DsLocustInstance();
  ~DsLocustInstance();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool DoMessage(ScMessage *message) override;
  bool GetBounds(GeCellRect *output) override;
  // Original primary28 forwards the day/minute selector and a forced running byte to private progression.
  bool Tick(DsTickType tick) override;
  bool GetLocation(u32 *x, u32 *z) override;
  ScOccupant *GetOccupant() override;
  DsManager *GetManager() override;
  u32 TypeId() override;
  i8 PanicEffect(u32 distance) override;
  u32 PanicRadius() override;
  // Activity tests only the first owned swarm, independently of globally existing swarms.
  bool IsActive() override;
  u32 GetDestroyedCount() override;
  u32 GetCost() override;
  void GetRecord(DsLocustRecord *output) override;
  u32 GetRecords(DsLocustRecord *records, u32 *count) const override;
  u32 GetCount() override;
  // Active teardown retains its real cell-removal dependency; private nominal byte spelling is unresolved.
  u8 Shutdown();
  // Original live and restored init arguments are proven; private nominal byte declarations remain candidates.
  u8 Init(DsLocustManager *owner, u8 automatic, u8 premonitions, u8 enabled);
  u8 Init(DsLocustManager *owner, const DsLocustRecord *saved);

protected:
  // Private progression preserves both original operands and the raw update gate.
  u8 Advance(i32 elapsed, u8 running);
  // Receiver-only attack creates the actual swarm group and distributes canonical unsigned crop triples.
  u8 ExecuteAttack();
  u8 QueueDeparture();
  u8 ClearCells();
  // Original coordinate words and raw bool/BYTE result; nominal private coordinate signedness remains qualified.
  u8 RemoveCell(u32 x, u32 z);
  // Capped dispatch retains the explicit module-bound static-animation building constructor dependency.
  u8 DispatchSprayAircraft(u32 x, u32 z);
  u8 IsSprayArea(i32 x, i32 z);
  // Complete area-append effects; original private nominal return remains unresolved.
  void AddSprayArea(u32 x, u32 z);
  // Synchronous phase notice effects are recovered; the ignored private nominal return remains unresolved.
  void PostPhase(u32 state);
  // Original five-slot cleanup effects are known; the ignored private nominal return remains unresolved.
  void ClearEffects();
  bool initialized;         // Original complete+12.
  DsLocustManager *manager; // Original complete+16, borrowed owner.
  void *swarm;              // Original complete+20, actual external COM output destination.
  u8 automatic_start;       // Original complete+24, raw live-init input.
  u32 references;           // Original complete+28.
  u8 flag_32, flag_33, flag_34, flag_35;
  u32 location_cursor; // Original complete+36, incremented by GetLocation.
  u8 flag_40;
  u32 word_44, word_48, word_52;
  u32 effects[5];          // Original complete+56, initialized to invalid handles.
  DsLocustRecord record;   // Original complete+76; constructor initializes all six words.
  DsLocustCells cells;     // Original complete+100, owns a circular24-byte-node list.
  ScArray<ScRect> targets; // Original complete+104, sixteen-byte spray-area rectangles.
  u32 destroyed, cost;     // Original complete+116/+120; independent of saved record words.
  u8 flag_124;
  ScLock lock; // Original complete+128; recursive platform lock.
};

#endif
