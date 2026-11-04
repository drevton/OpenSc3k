#ifndef SC3_DISASTERS_API_H
#define SC3_DISASTERS_API_H

#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"
#include "geometry/ge_types.h"

// The event and water loaders query this same event-tuning selector; retain their shared original key.
enum DsSharedTuningProperty : u32 {
  kDsEventMaximumMonthsPropertyId = 0x24860903,
};

// Disaster identities returned by each concrete instance and used to select its manager.
enum DsTypeId : u32 {
  kDsEventTypeId = 0x13873257,
  kDsFireTypeId = 0x621cda33,
  kDsLocustTypeId = 0x13873255,
  kDsParadeTypeId = 0xc4c90997,
  kDsRiotTypeId = 0x4296380e,
  kDsTornadoTypeId = 0x02963821,
  kDsToxicTypeId = 0x22963800,
  kDsUfoTypeId = 0xe2963828,
  kDsWaterTypeId = 0x13873254,
  kDsImpactTypeId = 0x13873256,
  kDsTrackTypeId = 0x42963812,
};

// Serialized group keys have a distinct role from the matching live disaster identities.
enum DsRecordGroup : u32 {
  kDsFireRecordGroup = 0x621cda33,
  kDsParadeRecordGroup = 0xc4c90997,
  kDsRiotRecordGroup = 0x4296380e,
  kDsTornadoRecordGroup = 0x02963821,
  kDsToxicRecordGroup = 0x22963800,
};

// Registered disaster cheats retain their original command spellings and identities.
enum DsCheatId : u32 {
  kDsUfoSwarmCheatId = 0x6303efa6,
  kDsLocustSwarmCheatId = 0xc5326321,
};

// Disaster-controller model requests retain their scalar type or tile-coordinate payloads.
enum DsMsg : u32 {
  kDsMsg_CounterStartRequested = 0x3417e94,
  kDsMsg_CounterEnded = 0x23417ec0,
  kDsMsg_RequestDisaster = 0x6373d4e2,
  kDsMsg_DispatchPolice = 0x373d622,
  kDsMsg_DispatchFire = 0x4373d6a0,
  kDsMsg_DispatchPlane = 0x45356db9,
  kDsMsg_StartDisasterCounter = 0x8373d754,
  kDsMsg_SetDisastersEnabled = 0x373d8ce,
};

// DestroyOccupant forwards nonzero causes as replacement occupancy states, rather than combining damage flags.
// Fire and toxic rain share the burned replacement; both locust damage paths use the same separate state.
enum DsDamageCause : i32 {
  kDsDamageRemove = 0,
  kDsDamageAbandon = 2,
  kDsDamageRubble = 8,
  kDsDamageBurn = 16,
  kDsDamageLocust = 32,
};

// Actual six-word saved record at original instance complete+104 and manager vector elements.
struct DsImpactRecord {
  u32 date, magnitude, destroyed, cost, phase, phase_ticks;
};

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

// Disaster instances publish this cell-coordinate value when they have no current effect location.
enum DsCellLocationSentinel : u32 {
  kDsInvalidCellCoordinate = 0xffffffff,
};

class DsInstance;
class DsListIterator;
class DsManager;
class ScOccupant;

// The callback context belongs to the external plugin ABI and remains opaque.
using DsChangeCallback = void (*)(DsInstance *instance, i32 change, void *context);

// Public primary disaster interface, queried as E1F6ABE2. Controller storage belongs to DsController.
class DsLayer : public ScUnknown {
public:
  using ChangeCallback = DsChangeCallback;
  using DisasterChangeCallback = DsChangeCallback;

  virtual void DisasterInstanceChange(DsInstance *instance, i32 change) = 0;
  virtual DsListIterator *CreateActiveDisasterIterator() = 0;
  virtual void DeleteActiveDisasterIterator(DsListIterator *iterator) = 0;
  virtual DsListIterator *CreateDisasterListIterator() = 0;
  virtual void DeleteDisasterListIterator(DsListIterator *iterator) = 0;
  virtual bool EnableDisasters(bool enabled) = 0;
  virtual bool DisastersEnabled() = 0;
  virtual u32 GetActiveDisasterCount() = 0;
  virtual bool NotifyOnChange(ChangeCallback callback, void *context) = 0;
  virtual bool StopNotifyingOnChange(ChangeCallback callback, void *context) = 0;
  virtual DsManager *GetDisasterManager(u32 type) = 0;
  virtual u32 GetDemolitionCost(ScOccupant *occupant) = 0;
  virtual bool DestroyOccupant(ScOccupant *occupant, i32 cause, u32 *cost) = 0;
  virtual bool IsSimulationRunning() = 0;
  virtual void ComputeSurroundRect(const GeCellRect &input, GeCellRect *output, u32 distance) = 0;
};

class ScStringView;

// Synchronous82684130 payload; the receiver writes effect while the remaining words identify resource text.
struct DsNoticePayload {
  u32 effect, group, resource;
  ScStringView *text;
  u32 reserved[4];
};

u32 DsRasterPath(i32 start_x, i32 start_z, i32 end_x, i32 end_z, ScPoint *points, u32 capacity);

class CtCity;
class ScDirtBag;
struct ScRect;

// Original unsigned corner-difference threshold, statically initialized to10 before tuning loads.
extern u32 DsImpactMaximumCornerDifference;

i32 DsFixedAltitude(CtCity *city, i32 x, i32 z);
// Candidate preserves seven stack arguments and AL transport; private nominal coordinates/bool return remain unresolved.
u8 SC3_STDCALL DsAdjustDirtAltitude(
    u32 x, u32 z, i32 delta, i32 minimum, i32 maximum, i32 sea_level, ScDirtBag *terrain);
// Private helper spellings remain candidates; both original input pointers and AL transport are preserved.
u8 DsContainsWater(ScDirtBag *terrain, const ScRect *bounds);
bool DsHasLevelCorners(ScDirtBag *terrain, const ScRect *bounds);
// Original five separate operands and byte predicate result; private nominal bool/BYTE spelling remains unresolved.
u8 DsFindWaterRectangles(
    ScDirtBag *terrain, ScArray<GeCellRect> *rectangles, u32 first, u32 second, const GeCellRect *bounds);

// Original 36-byte pending tornado record; only the severity byte at+20 is meaningful in its four-byte slot.
struct DsTornadoRecord {
  u32 x, z;
  ScVec3F direction;
  i8 severity;
  f32 speed;
  u32 cost, destroyed;
};

#endif
