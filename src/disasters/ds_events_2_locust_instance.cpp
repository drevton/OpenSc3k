#include "disasters/ds_events.h"
#include <math.h>
#include <string.h>
#include <cassert>
#include "advisors/ad_domains.h"
#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_locust_instance.h"
#include "disasters/ds_module.h"
#include "economy/api.h"
#include "environment/api.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "geometry/grid.h"
#include "geometry/selection.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/api.h"
#include "sprites/sp_effect.h"
#include "terrain/api.h"
#include "ui/game_menu.h"

DsEventInstance::DsEventInstance()
    : initialized(false), timing{0, 0, 0, 0}, references(0), announcement(false),
      effects{kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId,
          kApInvalidNotificationId},
      severity(1), demolition_chance(0), position{0, 0, 0}, word_80(0), manager(nullptr), word_96(0), state{} {
}

DsEventInstance::~DsEventInstance() {
  Shutdown();
}

// Publish the actual common or private view and retain through the primary identity; rejected outputs are cleared.
bool DsEventInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsEventState, DsEventState);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsEventInstance::AddRef() {
  return ++references;
}

// Delete at zero or one references without changing the final count seen by teardown; larger counts decrement first.
u32 DsEventInstance::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Query the live city's full rectangle whenever initialized, preserving separate X/Z callbacks and unsigned subtraction.
// An inactive query clears bottom/top/right/left without reading owner or dimensions.
bool DsEventInstance::GetBounds(GeCellRect *output) {
  if (initialized) {
    output->left = 0;
    output->top = 0;
    output->right = manager->city->CellCountX() - 1;
    output->bottom = manager->city->CellCountZ() - 1;
    return true;
  }
  output->bottom = 0;
  output->top = 0;
  output->right = 0;
  output->left = 0;
  return false;
}

bool DsEventInstance::GetLocation(u32 *x, u32 *z) {
  *x = u32(position.x) >> kCtMicroCoordinateShift;
  *z = u32(position.z) >> kCtMicroCoordinateShift;
  return true;
}

// Return the shared null occupant pointer; the instance does not retain a single building.
ScOccupant *DsEventInstance::GetOccupant() {
  return nullptr;
}

// Borrow the actual common manager view, preserving null before its native base conversion.
DsManager *DsEventInstance::GetManager() {
  return manager;
}

u32 DsEventInstance::TypeId() {
  return kDsEventTypeId;
}

u32 DsEventInstance::GetDestroyedCount() {
  return state.destroyed;
}

u32 DsEventInstance::GetCost() {
  return state.cost;
}

bool DsEventInstance::DoMessage(ScMessage *) {
  return true;
}

f64 DsEventInstance::GetMagnitude() const {
  return f64(state.magnitude) * f64(0.1f);
}

u8 DsEventInstance::GetSeverity() const {
  return severity;
}

// Copy the actual signed fixed triple in X/Y/Z order and return the captured final Z word.
i32 DsEventInstance::GetPosition(i32 *x, i32 *y, i32 *z) const {
  *x = position.x;
  *y = position.y;
  i32 value = position.z;
  *z = value;
  return value;
}

void DsEventInstance::GetRecord(DsEventRecord *record) {
  *record = state;
}

// Return the distance-indexed panic byte only from phase five onward. The panic radius supplies the five-cell extent.
i8 DsEventInstance::PanicEffect(u32 distance) {
  if (state.phase < 5)
    return 0;
  return DsEventPanicLevels[distance];
}

u32 DsEventInstance::PanicRadius() {
  return state.phase < 5 ? 0 : 5;
}

bool DsEventInstance::IsActive() {
  return state.phase >= 5;
}

// Bind genuine position/timing inputs and publish ready state before city callbacks and the initial phase notification.
// Manual starts skip premonitions and tick immediately; already-ready instances return without rebinding.
u8 DsEventInstance::Init(DsEventManager *owner, f32 strength, const ScVec3I *start, u8 automatic) {
  if (!initialized) {
    initialized = true;
    manager = owner;
    columns = owner->city->CellCountX();
    rows = manager->city->CellCountZ();
    position = *start;
    timing[0] = DsEventFirstDelay;
    timing[1] = DsEventSecondDelay;
    timing[2] = DsEventThirdDelay;
    timing[3] = DsEventAttackDelay;
    state.serial = 0;
    f64 rounded = f64(strength) * 10.0 + kScDoubleIntegerBias;
    u64 bits;
    memcpy(&bits, &rounded, sizeof(bits));
    state.magnitude = u32(bits) + kScDoubleIntegerLowWordBias;
    state.preparedness = manager->flag_73;
    state.destroyed = 0;
    state.packed_location = u32(u16(position.z)) | (u32(u16(position.x)) << 16);
    state.cost = 0;
    state.phase = 1;
    state.phase_ticks = 0;
    if (!automatic) {
      state.phase = 4;
      timing[0] = 0;
      timing[1] = 0;
      timing[2] = 0;
      timing[3] = 0;
    }
    ConfigureMagnitude(f32(f64(state.magnitude) * f64(0.1f)));
    ApplyPreparedness(state.preparedness != 0);
    manager->layer->DisasterInstanceChange(this, 0);
    if (!automatic)
      Tick(DsDayTick);
  }
  return 1;
}

// Temporarily repair saved phase/ticks for initialization, then reload all eight raw words in the original order.
// Preserve the original packed reconstruction into X/Y with zero Z and the phase-five restart before final radius setup.
u8 DsEventInstance::Init(DsEventManager *owner, const DsEventRecord *record) {
  state = *record;
  u32 magnitude = state.magnitude;
  if (magnitude < 10 || state.phase > 7)
    state.phase = 1;
  if (state.phase > 7)
    state.phase = 1;
  if (state.phase_ticks > 10000)
    state.phase_ticks = 0;
  ScVec3I start = {i32(state.packed_location >> 16), i32(u16(state.packed_location)), 0};
  Init(owner, f32(f64(magnitude) * f64(0.1f)), &start, 1);
  state.serial = record->serial;
  state.magnitude = record->magnitude;
  state.preparedness = record->preparedness;
  state.packed_location = record->packed_location;
  state.destroyed = record->destroyed;
  state.cost = record->cost;
  state.phase = record->phase;
  state.phase_ticks = record->phase_ticks;
  if (record->phase == 5) {
    state.phase_ticks = timing[3];
    state.phase = 4;
    Tick(DsDayTick);
  }
  ConfigureMagnitude(f32(f64(state.magnitude) * f64(0.1f)));
  ApplyPreparedness(state.preparedness != 0);
  return 1;
}

// Decrement owner activity before clearing the announcement flag, owner and ready state.
// The original does not release word96, emit UI events or remove notices during this guarded teardown.
u8 DsEventInstance::Shutdown() {
  if (initialized) {
    manager->InstanceEnded();
    if (announcement)
      announcement = false;
    manager = nullptr;
    initialized = false;
  }
  return 1;
}

// Publish severity and the original hardcoded adjustable level before selecting the demolition threshold.
u32 DsEventInstance::ConfigureMagnitude(f32 strength) {
  if (strength > 4.0f) {
    if (strength > 6.0f) {
      severity = 3;
      adjusted_level = 3;
      demolition_chance = DsEventDemolitionChances[2];
    } else {
      severity = 2;
      adjusted_level = 2;
      demolition_chance = DsEventDemolitionChances[1];
    }
  } else {
    severity = 1;
    adjusted_level = 1;
    demolition_chance = DsEventDemolitionChances[0];
  }
  return demolition_chance;
}

void DsEventInstance::ApplyPreparedness(u8 enabled) {
  if (enabled)
    --adjusted_level;
}

// Remove each of the five live notices in order and invalidate its handle after the synchronous callback returns.
void DsEventInstance::ClearEffects() {
  for (u32 index = 0; index < 5; ++index)
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
}

// Select the same four-resource notice range independently for phases two, three and four.
// Retain the actual String across the message and publish its returned handle after release but before resource destruction.
void DsEventInstance::PostPhase(u32 phase) {
  if (phase < 2 || phase > 4)
    return;
  u32 *handle = &effects[phase - 2], resource = manager->random.Next(4) + 1234;
  auto *text = new ScString;
  DsTextResource content(resource, kRsNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  *handle = notice.effect;
}

// Advance cumulative phase timing, sample the actual simulation date at activation and retire phase six through the
// owner.
bool DsEventInstance::Tick(DsTickType) {
  u32 ticks = ++state.phase_ticks;
  switch (state.phase) {
  case 1:
    if (ticks >= timing[0]) {
      state.phase = 2;
      PostPhase(2);
    }
    break;
  case 2:
    if (ticks >= timing[1]) {
      state.phase = 3;
      PostPhase(3);
    }
    break;
  case 3:
    if (ticks >= timing[2]) {
      state.phase = 4;
      PostPhase(4);
    }
    break;
  case 4:
    if (ticks >= timing[3]) {
      state.phase = 5;
      if (i32(manager->active_count) <= 1)
        announcement = true;
      PostPhase(5);
      CtDate current;
      manager->city->GetSimulationClock()->GetCurrentClockDate(&current);
      state.serial = current.serial;
      if (!word_96)
        state.phase = 6;
      manager->layer->DisasterInstanceChange(this, 1);
    }
    break;
  case 6:
    if (manager->active_count == 1)
      announcement = false;
    ClearEffects();
    manager->FinishInstance(this);
    break;
  }
  return true;
}

i32 DsFireMinimumInterval = 5, DsFireMaximumInterval = 30;
u32 DsFireMaximumInstances = 0;
i32 DsFireUiId = 0;
EnHazardMap *DsFireFlammability;
u32 DsFireFloraChance = 10;
i32 DsFireMinimumDuration = 10, DsFireMaximumDuration = 40;
f32 DsFireDurationScale = 0.6f;
u8 DsFireMinimumHazard = 50, DsFireOuterHazard = 150;
i32 DsFireMinimumSpread[3] = {0, 0, 0}, DsFireMaximumSpread[3] = {20, 50, 95};
f32 DsFireSpreadScale[3] = {0.1f, 0.2f, 0.3f};

// Advance the fire-attempt cadence. On expiry, reset the counter and choose the next interval before calling
// the manager's original trigger; its result does not change this update's successful Boolean return.
bool DsFireManager::Tick() {
  elapsed = i32(u32(elapsed) + 1);
  if (elapsed >= interval) {
    elapsed = 0;
    interval = DsFireMinimumInterval;
    if (DsFireMinimumInterval < DsFireMaximumInterval)
      interval = random.Range(DsFireMinimumInterval, DsFireMaximumInterval);
    AttemptDisasterStart();
  }
  return true;
}

bool DsFireManager::StaticShutdown() {
  return true;
}

static u32 DsFireProgress(u32 count) {
  assert(DsFireMaximumInstances != 0);
  return (100u * count) / DsFireMaximumInstances;
}

void DsFireManager::InstanceStarted() {
  ++instance_count;
  if (instance_count == 1)
    box->Event(kAuEventPlayEffect, DsFireUiId, 0, 0, 0);
  i32 progress = i32(DsFireProgress(instance_count));
  if (progress > 100)
    progress = 100;
  box->Event(kAuEventEffectSlot60, DsFireUiId, 0, progress, 0);
}

// Decrement a positive signed fire count and announce zero even when it was already zero. Always calculate and post the
// remaining progress afterward, preserving the original absence of a clamp on this path.
void DsFireManager::InstanceEnded() {
  if (i32(instance_count) > 0)
    --instance_count;
  if (!instance_count)
    box->Event(kAuEventEffectSlot52, DsFireUiId, 0, 0, 0);
  box->Event(kAuEventEffectSlot60, DsFireUiId, 0, i32(DsFireProgress(instance_count)), 0);
}

// Eight-byte fire-specific iterator predicate; the public iterator-test identity owns only its actual reference count.
class DsFireTest : public OcIteratorTest {
public:
  DsFireTest();
  virtual ~DsFireTest();
  bool QueryInterface(u32 iid, void **output) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Test(ScOccupant *occupant) override;
  u32 references;
};

DsFireTest::DsFireTest() : references(0) {
}

DsFireTest::~DsFireTest() {
}

// Expose Unknown or the real iterator-test identity, publishing it before virtual acquisition and clearing the output
// on misses.
bool DsFireTest::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIteratorTest);
  HANDLE_QUERY_INTERFACE(IID_OcIteratorTest, OcIteratorTest);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsFireTest::AddRef() {
  return ++references;
}

// Decrement a positive count and delete at zero, including an already-zero release, without a module-director hold.
u32 DsFireTest::Release() {
  if (references)
    --references;
  u32 result = references;
  if (!result) {
    delete this;
    return 0;
  }
  return result;
}

// Read hazard at the occupant origin and apply the original strict threshold, unlike manual ignition's inclusive gate.
// The output backing starts with the original pointer parameter's high DWORD byte before the real getter writes it.
bool DsFireTest::Test(ScOccupant *occupant) {
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  u8 value = u8(reinterpret_cast<uptr>(occupant) >> 24);
  DsFireFlammability->GetValue(x, z, &value);
  return DsFireMinimumHazard < value;
}

// Bind the original city services and fire predicate in acquisition order, publishing genuine queried outputs directly.
// Reset counters and borrowed keys before subscribing, then choose the first interval and mark readiness last.
bool DsFireManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized)
    Shutdown();
  filter = ScAddRefNotNull(new DsFireTest);
  city = ScAddRefNotNull(retained_city);
  agent_types = ScAddRefNotNull(DsGetAgentTree());
  maximum_x = city->CellCountX() - 1;
  maximum_z = city->CellCountZ() - 1;
  box = ScAddRefNotNull(DsGetBoxX());
  buildings = ScAddRefNotNull(city->BuildingLayer());
  demolition = city->DemolitionLayer();
  if (demolition)
    demolition->AddRef();
  occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  layer = ScAddRefNotNull(retained_layer);
  CtLayer *specific = city->GetSpecificLayer(kCtFireLayerType);
  specific->QueryInterface(IID_CtFireLayer, reinterpret_cast<void **>(&fire_service));
  specific = city->GetSpecificLayer(kCtFlammabilityLayerType);
  specific->QueryInterface(IID_EnHazardMap, reinterpret_cast<void **>(&flammability));
  DsFireFlammability = flammability;
  instance_count = 0;
  burning.size = 0;
  camera_count = 0;
  DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  elapsed = 0;
  if (DsFireMinimumInterval >= DsFireMaximumInterval)
    interval = DsFireMinimumInterval;
  interval = random.Range(DsFireMinimumInterval, DsFireMaximumInterval);
  initialized = true;
  return true;
}

// Establish the four identities and original inactive state, seeding the random stream before collection construction.
// Leave city-bound types, buildings, bounds and fire service unwritten until live initialization binds them.
DsFireManager::DsFireManager()
    : initialized(false), layer(nullptr), city(nullptr), demolition(nullptr), box(nullptr), occupants(nullptr),
      random(kScRandomSeedFromClock), filter(nullptr), instance_count(0), flammability(nullptr), elapsed(0),
      interval(0) {
}

DsFireManager::~DsFireManager() {
  if (initialized)
    DsFireManager::Shutdown();
}

// Publish common or fire-private identity before acquiring the shared root count; delegate other IDs to the real agent.
bool DsFireManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsFireControl, DsFireControl);
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsFireManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsFireManager::Release() {
  return DsAgent::Release();
}

// Clear borrowed burning keys before notification removal, then release retained city services in original order.
// Keep identities and readiness published through callbacks; clear readiness after the final predicate release.
bool DsFireManager::Shutdown() {
  if (initialized) {
    burning.size = 0;
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&agent_types);
    if (flammability) {
      flammability->Release();
      flammability = nullptr;
      DsFireFlammability = nullptr;
    }
    ScRelease(&fire_service);
    ScRelease(&box);
    ScRelease(&demolition);
    ScRelease(&buildings);
    ScRelease(&occupants);
    ScRelease(&city);
    ScRelease(&filter);
    initialized = false;
  }
  return true;
}

// Signed negative counts remain an empty iteration; unavailable failed-read count bytes are never inspected.
bool DsFireManager::Init(CtCity *owner, RsDatabaseSegment *segment) {
  pending.size = 0;
  DsArchive archive(owner, segment, {kRsDisasterLayerRecordType, kDsFireRecordGroup, 0});
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  i32 count;
  u8 result = stream->ReadI32(&count);
  for (i32 index = 0; result && index < count; ++index) {
    DsFireRecord record;
    result = stream->ReadU32(&record.x);
    result = result && stream->ReadU32(&record.z) && stream->ReadI32(&record.mode);
    if (result)
      *pending.Append() = record;
  }
  return result;
}

// Count matching active/listed fire instances, reset the same iterator and write each location and stored mode.
// Advance before checking writer failure, then delete through the current controller before archive cleanup.
bool DsFireManager::Save(CtCity *owner, RsDatabaseSegment *segment) {
  DsArchive archive(owner, segment, {kRsDisasterLayerRecordType, kDsFireRecordGroup, 0}, 1, 0);
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  DsListIterator *iterator = layer->CreateDisasterListIterator();
  if (!iterator)
    return false;
  u32 count = 0;
  for (DsInstance *instance = iterator->Next(); instance; instance = iterator->Next())
    if (instance->TypeId() == kDsFireTypeId)
      ++count;
  u8 result = stream->WriteU32(count);
  iterator->Rewind();
  for (DsInstance *instance = iterator->Next(); instance && result; instance = iterator->Next()) {
    if (instance->TypeId() == kDsFireTypeId) {
      u32 x, z;
      instance->GetLocation(&x, &z);
      result = stream->WriteU32(x);
      result = result && stream->WriteU32(z) && stream->WriteI32(static_cast<DsFireInstance *>(instance)->mode);
    }
  }
  layer->DeleteDisasterListIterator(iterator);
  return result;
}

// Replay the captured pending range only on load completion, preserving coordinates and signed mode for each command.
// Clear the current pending range after callbacks, including commands appended during replay without reallocating it.
bool DsFireManager::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityViewReady)
    return false;
  DsFireRecord *records = pending.data;
  u32 count = pending.size;
  for (u32 index = 0; index < count; ++index)
    IgniteCell(records[index].x, records[index].z, records[index].mode);
  pending.size = 0;
  return true;
}

// Retain the surface occupant at this cell, snapshot membership in the burning-key collection and release it afterward.
u8 DsFireManager::IsCellBurning(u32 x, u32 z) {
  ScOccupant *occupant;
  if (!occupants->GetOccupantAtCell(x, z, &occupant))
    return 0;
  u8 found = 0;
  for (u32 index = 0; index < burning.size; ++index) {
    if (burning.data[index] == occupant) {
      found = 1;
      break;
    }
  }
  occupant->Release();
  return found;
}

u8 DsFireManager::IgniteCell(u32 x, u32 z) {
  return IgniteCell(x, z, 0);
}

// Hold the located occupant through ignition and return its result only after the lookup reference is released.
u8 DsFireManager::IgniteCell(u32 x, u32 z, i32 mode) {
  ScOccupant *occupant;
  if (!occupants->GetOccupantAtCell(x, z, &occupant))
    return 0;
  u8 result = IgniteOccupant(occupant, mode);
  occupant->Release();
  return result;
}

// Draw a fresh starting cell and spiral over inclusive city bounds, accepting the first successful ignition.
u8 DsFireManager::IgniteRandom() {
  DsTileScan scan;
  i32 x = random.Range(0, i32(maximum_x)), z = random.Range(0, i32(maximum_z));
  scan.left = 0;
  scan.top = 0;
  scan.right = i32(maximum_x);
  scan.bottom = i32(maximum_z);
  scan.Reset(x, z);
  while (scan.Next(&x, &z)) {
    ScOccupant *occupant;
    if (occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      u8 ignited = IgniteOccupant(occupant, 0);
      occupant->Release();
      if (ignited)
        return 1;
    }
  }
  return 0;
}

// Spiral from independent coordinate draws, attempting ignition only for occupants of the requested ancestor type.
u8 DsFireManager::IgniteKind(u32 ancestor) {
  DsTileScan scan;
  i32 x = random.Range(0, i32(maximum_x)), z = random.Range(0, i32(maximum_z));
  scan.left = 0;
  scan.top = 0;
  scan.right = i32(maximum_x);
  scan.bottom = i32(maximum_z);
  scan.Reset(x, z);
  while (scan.Next(&x, &z)) {
    ScOccupant *occupant;
    if (occupants->GetOccupantAtCell(u32(x), u32(z), &occupant)) {
      u32 type = occupant->TypeId();
      u8 ignited = 0;
      if (agent_types->IsAncestor(ancestor, type))
        ignited = IgniteOccupant(occupant, 0);
      occupant->Release();
      if (ignited)
        return 1;
    }
  }
  return 0;
}

// Reject counts strictly above the configured maximum, prefer flora or buildings, then fall back unfiltered.
// Post the original fire-success event only after a scan has actually ignited an occupant.
bool DsFireManager::AttemptDisasterStart() {
  if (instance_count > DsFireMaximumInstances)
    return false;
  u8 result = IgniteKind(random.Next(100) < DsFireFloraChance ? 300u : 100u);
  if (!result)
    result = IgniteRandom();
  if (result)
    box->Event(kAuEventStartDisaster, kAuEffectDisasterFire, 0, 0, 0);
  return result;
}

// Hold the fire through removal, restore both occupant removal permissions and erase its borrowed burning key.
// Shut down and release the instance before optional burned replacement through the current controller.
u32 DsFireManager::FinishInstance(DsFireInstance *instance, u8 destroy) {
  instance->AddRef();
  ScOccupant *occupant = instance->GetOccupant();
  occupant->SetUserRemove(true);
  occupant->SetSimRemove(true);
  layer->DisasterInstanceChange(instance, 4);
  for (u32 index = 0; index < burning.size; ++index) {
    if (burning.data[index] == occupant) {
      for (u32 next = index + 1; next < burning.size; ++next)
        burning.data[next - 1] = burning.data[next];
      --burning.size;
      break;
    }
  }
  instance->Shutdown();
  u32 result = instance->Release();
  if (destroy) {
    u32 ignored_cost;
    return layer->DestroyOccupant(occupant, kDsDamageBurn, &ignored_cost);
  }
  return result;
}

// Recompute the duration slope from the signed wrapped interval and remaining byte hazard range.
// Threshold255 clears maximum duration instead of the prior slope, preserving the original defined edge behavior.
void DsUpdateFireDurationScale() {
  f64 range = 255.0 - f64(DsFireMinimumHazard);
  if (range <= 0.0)
    DsFireMaximumDuration = 0;
  else
    DsFireDurationScale = f32(f64(i32(u32(DsFireMaximumDuration) - u32(DsFireMinimumDuration))) / range);
}

void SC3_STDCALL DsUpdateFireSpreadScale(u32 index) {
  i32 difference = i32(u32(DsFireMaximumSpread[index]) - u32(DsFireMinimumSpread[index]));
  DsFireSpreadScale[index] = f32(f64(difference) * f64(0.0039215689f));
}

// Split the expanded cell perimeter into four strips, omitting clipped edges and duplicate corners.
// Preserve unsigned expansion, write ordering and the original count-pointer return.
static u32 *SC3_STDCALL DsFirePerimeter(
    const GeCellRect *bounds, u32 distance, u32 maximum_x, u32 maximum_z, GeCellRect *strips, u32 *count) {
  *count = 0;
  u32 right = distance + bounds->right, bottom = distance + bounds->bottom;
  bool has_right = right <= maximum_x, has_bottom = bottom <= maximum_z;
  if (!has_right)
    right = maximum_x;
  if (!has_bottom)
    bottom = maximum_z;
  u32 left = 0, top = 0;
  bool has_left = bounds->left >= distance, has_top = bounds->top >= distance;
  if (has_left)
    left = bounds->left - distance;
  if (has_top)
    top = bounds->top - distance;
  if (has_right) {
    strips[0].left = right;
    strips[*count].right = right;
    strips[*count].top = top;
    strips[*count].bottom = has_bottom ? bottom - 1 : bottom;
    ++*count;
  }
  if (has_left) {
    strips[*count].right = left;
    strips[*count].left = left;
    strips[*count].top = has_top ? top + 1 : top;
    strips[(*count)++].bottom = bottom;
  }
  if (has_bottom) {
    strips[*count].top = bottom;
    strips[*count].bottom = bottom;
    strips[*count].left = has_left ? left + 1 : left;
    strips[(*count)++].right = right;
  }
  if (has_top) {
    strips[*count].bottom = top;
    strips[*count].top = top;
    strips[*count].left = left;
    strips[(*count)++].right = has_right ? right - 1 : right;
  }
  return count;
}

// Select occupants whose origin exceeds the outer-ring hazard threshold. The original output backing
// starts with the high byte of the occupant pointer and retains that byte if the provider leaves it untouched.
static bool DsFireOuterTest(ScOccupant *occupant) {
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  u8 value = u8(reinterpret_cast<uptr>(occupant) >> 24);
  DsFireFlammability->GetValue(x, z, &value);
  return DsFireOuterHazard < value;
}

// Attempt ignition among filtered neighbors, then query the expanded perimeter with its higher hazard threshold.
// Iterator occupants remain borrowed; each retained perimeter result is released after its ignition attempt.
u8 DsFireManager::SpreadFrom(ScOccupant *occupant, u32 chance) {
  if (!occupant->IsInCity() || i32(instance_count) > i32(DsFireMaximumInstances))
    return 0;
  OcIterator *iterator = nullptr;
  occupants->GetNeighbors(&iterator, occupant, filter);
  for (ScOccupant *neighbor = iterator->Reset(); neighbor; neighbor = iterator->Next())
    if (random.Next(100) < chance)
      IgniteOccupant(neighbor, 0);
  iterator->Shutdown();
  iterator->Release();
  GeCellRect bounds, strips[4];
  u32 count;
  occupant->GetBoundingRectInCellCoords(&bounds);
  DsFirePerimeter(&bounds, 2, maximum_x, maximum_z, strips, &count);
  for (u32 index = 0; index < count; ++index) {
    GeBounds volume = {{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate},
        {kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}};
    volume.minimum.altitude = 0;
    volume.minimum.x = i32(strips[index].left << kCtMicroCoordinateShift);
    volume.minimum.z = i32(strips[index].top << kCtMicroCoordinateShift);
    volume.maximum.altitude = 0;
    volume.maximum.x = i32(strips[index].right << kCtMicroCoordinateShift);
    volume.maximum.z = i32(strips[index].bottom << kCtMicroCoordinateShift);
    ScOccupant *found[6];
    u32 actual;
    occupants->GetOccupantsInCell(volume, 6, found, &actual, DsFireOuterTest);
    for (u32 item = 0; item < actual; ++item) {
      if (random.Next(100) < chance)
        IgniteOccupant(found[item], 0);
      found[item]->Release();
    }
  }
  return 1;
}

// Hold the new fire across initialization, release that temporary reference, then perform the original unique-key
// insertion.
u8 DsFireManager::IgniteOccupant(ScOccupant *occupant, i32 mode) {
  u32 x, z;
  u8 hazard;
  occupant->GetLocationInCellCoords(&x, &z);
  flammability->GetValue(x, z, &hazard);
  if (hazard < DsFireMinimumHazard)
    return 0;
  for (u32 index = 0; index < burning.size; ++index)
    if (burning.data[index] == occupant)
      return 0;
  occupant->SetUserRemove(false);
  occupant->SetSimRemove(false);
  auto *instance = new DsFireInstance;
  instance->AddRef();
  instance->Init(this, occupant, mode);
  instance->Release();
  uptr key = reinterpret_cast<uptr>(occupant);
  u32 index = 0;
  while (index < burning.size && reinterpret_cast<uptr>(burning.data[index]) < key)
    ++index;
  if (index == burning.size || burning.data[index] != occupant) {
    burning.Append();
    for (u32 next = burning.size - 1; next > index; --next)
      burning.data[next] = burning.data[next - 1];
    burning.data[index] = occupant;
  }
  return 1;
}

void DsFireManager::StopInstance(DsInstance *instance) {
  FinishInstance(static_cast<DsFireInstance *>(instance), 0);
}

// Parse the first comma-separated field as the instance limit, then retire that prefix before publishing the UI ID.
// A missing comma leaves an empty first field and parses the unchanged value as the UI ID; the key is ignored.
void DsFireReadEntry(const ScString &key, const ScString &value, void *) {
  DsParseNumber(key.ToChar());
  ScString remaining(value);
  {
    ScString first;
    u32 length = remaining.Strlen(), split = 0;
    while (split < length && remaining.begin[split] != ',')
      ++split;
    if (split < length) {
      first.FromChar(remaining.begin, split);
      remaining.Erase(0, split + 1);
    }
    DsFireMaximumInstances = DsParseNumber(first.ToChar());
  }
  DsFireUiId = i32(DsParseNumber(remaining.ToChar()));
}

// Read fire descriptions before querying the variable manager, then load the original ordered timing and spread words.
// Missing properties retain prior values; recompute coefficients only when the service exists and preserve its false miss.
bool DsFireManager::StaticInit() {
  DsIni reader;
  ScString path, unused;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  {
    ScString section("FireDisaster");
    reader.EnumEntries(section, DsFireReadEntry, nullptr);
  }
  ScVariableManager *provider = DsGetPropertyProvider();
  if (!provider)
    return false;
  ScValue value;
  if (provider->GetVariable(kDsFireMinimumIntervalPropertyId, &value))
    DsFireMinimumInterval = i32(value.value.word);
  if (provider->GetVariable(kDsFireMaximumIntervalPropertyId, &value))
    DsFireMaximumInterval = i32(value.value.word);
  if (provider->GetVariable(kDsFireMinimumDurationPropertyId, &value))
    DsFireMinimumDuration = i32(value.value.word);
  if (provider->GetVariable(kDsFireMaximumDurationPropertyId, &value))
    DsFireMaximumDuration = i32(value.value.word);
  if (provider->GetVariable(kDsFireMinimumHazardPropertyId, &value))
    DsFireMinimumHazard = u8(value.value.word);
  if (provider->GetVariable(kDsFireOuterHazardPropertyId, &value))
    DsFireOuterHazard = u8(value.value.word);
  DsUpdateFireDurationScale();
  if (provider->GetVariable(kDsFireMinimumSpread0PropertyId, &value))
    DsFireMinimumSpread[0] = i32(value.value.word);
  if (provider->GetVariable(kDsFireMaximumSpread0PropertyId, &value))
    DsFireMaximumSpread[0] = i32(value.value.word);
  if (provider->GetVariable(kDsFireMinimumSpread1PropertyId, &value))
    DsFireMinimumSpread[1] = i32(value.value.word);
  if (provider->GetVariable(kDsFireMaximumSpread1PropertyId, &value))
    DsFireMaximumSpread[1] = i32(value.value.word);
  if (provider->GetVariable(kDsFireMinimumSpread2PropertyId, &value))
    DsFireMinimumSpread[2] = i32(value.value.word);
  if (provider->GetVariable(kDsFireMaximumSpread2PropertyId, &value))
    DsFireMaximumSpread[2] = i32(value.value.word);
  for (u32 index = 0; index < 3; ++index)
    DsUpdateFireSpreadScale(index);
  if (provider->GetVariable(kDsFireFloraChancePropertyId, &value))
    DsFireFloraChance = value.value.word;
  return true;
}

// Construct the actual fire manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateFireManager() {
  return static_cast<DsManager *>(new DsFireManager);
}

DsFireInstance::DsFireInstance()
    : initialized(false), manager(nullptr), occupant(nullptr), x(0), z(0), effect(nullptr), mode(0), cycles(0),
      elapsed(0), duration(0), references(0), in_camera(false), camera_ticks(3), destroyed(false), cost(0) {
}

DsFireInstance::~DsFireInstance() {
  if (initialized)
    Shutdown();
}

// Expose the primary Unknown and disaster-instance identities, retaining once on success. Unsupported IDs clear the
// output.
bool DsFireInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsFireInstance::AddRef() {
  return ++references;
}

// Drop a held reference without underflowing zero, deleting the complete fire when the resulting count is zero.
u32 DsFireInstance::Release() {
  if (references)
    --references;
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

// Forward the caller's actual rectangle output directly to the retained occupant. Without an occupant, write the
// zero rectangle in bottom/right/top/left order and report failure independently of initialization state.
bool DsFireInstance::GetBounds(GeCellRect *output) {
  if (occupant) {
    occupant->GetBoundingRectInCellCoords(output);
    return true;
  }
  output->bottom = 0;
  output->right = 0;
  output->top = 0;
  output->left = 0;
  return false;
}

// Copy the saved cell location in X/Z order. Aliased outputs retain Z and initialization does not gate the query.
bool DsFireInstance::GetLocation(u32 *output_x, u32 *output_z) {
  *output_x = x;
  *output_z = z;
  return true;
}

// Return the stored occupant pointer without acquiring a reference or testing initialization.
ScOccupant *DsFireInstance::GetOccupant() {
  return occupant;
}

// Return the borrowed common manager identity, preserving null instead of applying the original base adjustment.
DsManager *DsFireInstance::GetManager() {
  return manager;
}

u32 DsFireInstance::TypeId() {
  return kDsFireTypeId;
}

u32 DsFireInstance::GetDestroyedCount() {
  return destroyed != 0;
}

u32 DsFireInstance::GetCost() {
  return cost;
}

// Refresh camera membership through the real location query, updating the manager's UI count only on a transition.
// Publish the new camera flag after synchronous events so callbacks observe the original prior membership.
void DsFireInstance::RefreshVisibility() {
  u32 cell_x, cell_z;
  if (GetLocation(&cell_x, &cell_z)) {
    bool visible = DsController::IsInVisibleRect(cell_x, cell_z) != 0;
    if (visible != in_camera) {
      if (visible) {
        if (++manager->camera_count == 1)
          manager->box->Event(kAuEventEffectSlot56, DsFireUiId, 0, 1, 0);
      } else {
        if (--manager->camera_count == 0)
          manager->box->Event(kAuEventEffectSlot56, DsFireUiId, 0, 0, 0);
      }
      in_camera = visible;
    }
  }
}

i8 DsFireInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return distance <= 4 ? levels[distance] : 0;
}

u32 DsFireInstance::PanicRadius() {
  return 4;
}

bool DsFireInstance::IsActive() {
  return initialized;
}

// Retain the occupant and publish ownership before sampling its origin and choosing the original fire duration.
// Attach the specialized sprite before announcing started/active changes; temporary caller references remain separate.
u8 DsFireInstance::Init(DsFireManager *owner, ScOccupant *object, i32 initial_mode) {
  initialized = true;
  manager = owner;
  manager->InstanceStarted();
  occupant = ScAddRefNotNull(object);
  occupant->GetLocationInCellCoords(&x, &z);
  elapsed = 0;
  duration = manager->random.Range(DsFireMinimumDuration, DsFireMaximumDuration);
  mode = initial_mode;
  DsGetCom()->GetClassObject(kSpFireSimulatorClassId, IID_SpFireSimulator, &effect);
  switch (mode) {
  case 0:
    static_cast<SpFireSimulator *>(effect)->Init(object, 2);
    break;
  case 1:
    static_cast<SpFireSimulator *>(effect)->Init(object, 3);
    break;
  case 2:
    static_cast<SpFireSimulator *>(effect)->Init(object, 4);
    break;
  }
  manager->layer->DisasterInstanceChange(this, 0);
  manager->layer->DisasterInstanceChange(this, 1);
  return 1;
}

// Clear readiness before camera/owner callbacks, then release the occupant and request specialized sprite removal.
// The sprite removal operation is separate from Unknown::Release, and each pointer remains live through its callback.
bool DsFireInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    if (in_camera) {
      --manager->camera_count;
      if (!manager->camera_count)
        manager->box->Event(kAuEventEffectSlot56, DsFireUiId, 0, 0, 0);
    }
    manager->InstanceEnded();
    ScRelease(&occupant);
    if (effect) {
      static_cast<SpFireSimulator *>(effect)->ReleaseAsynchronous();
      effect = nullptr;
    }
  }
  return true;
}

// Check occupant identity, refresh camera visibility every third step and advance expired fire cycles.
// Preserve coverage/RNG ordering, spread before mode transitions and the final smoke, cost and demolition sequence.
bool DsFireInstance::Tick(DsTickType) {
  if (!initialized)
    return true;
  ScOccupant *current;
  if (!manager->occupants->GetOccupantAtCell(x, z, &current)) {
    manager->StopInstance(this);
    return true;
  }
  if (current != occupant) {
    current->Release();
    manager->StopInstance(this);
    return true;
  }
  current->Release();
  if (++camera_ticks > 2) {
    camera_ticks = 0;
    RefreshVisibility();
  }
  i32 previous = elapsed;
  elapsed = i32(u32(elapsed) + 1);
  if (previous < duration)
    return true;
  ++cycles;
  elapsed = 0;
  u32 cell_x, cell_z;
  occupant->GetLocationInCellCoords(&cell_x, &cell_z);
  u8 coverage, hazard;
  manager->fire_service->GetValue(cell_x, cell_z, &coverage);
  bool advance = cycles >= 10 || manager->random.Next(255) > coverage;
  manager->flammability->GetValue(cell_x, cell_z, &hazard);
  f64 next_duration = advance ? f64(DsFireMaximumDuration) - f64(hazard) * DsFireDurationScale
                              : f64(hazard) * DsFireDurationScale + f64(DsFireMinimumDuration);
  duration = i32(i64(next_duration));
  i32 spread = i32(i64(f64(DsFireMaximumSpread[mode]) - f64(coverage) * DsFireSpreadScale[mode]));
  if (spread > 0)
    manager->SpreadFrom(occupant, u32(spread));
  switch (mode) {
  case 0:
    if (advance) {
      mode = 1;
      static_cast<SpFireSimulator *>(effect)->SetLevel(3, 1);
    } else {
      static_cast<SpFireSimulator *>(effect)->SetLevel(0, 1);
      manager->FinishInstance(this, 0);
    }
    break;
  case 1:
    if (advance) {
      static_cast<SpFireSimulator *>(effect)->SetLevel(4, 1);
      mode = 2;
    } else {
      static_cast<SpFireSimulator *>(effect)->SetLevel(2, 1);
      mode = 0;
    }
    break;
  case 2:
    if (advance) {
      void *created;
      DsGetCom()->GetClassObject(kSpExplosionViewClassId, IID_SpExplosionView, &created);
      manager->box->Event(kAuEventPlayBuildingSound, i32(cell_x), i32(cell_z), 0, 0);
      if (created) {
        i32 fixed_x, fixed_z, fixed_y;
        static_cast<SpFireSimulator *>(effect)->GetPositionForExplosion(&fixed_x, &fixed_z, &fixed_y);
        auto *smoke = static_cast<SpExplosionView *>(created);
        smoke->Init(fixed_x, fixed_z, fixed_y, 3, 0, 0);
        smoke->Release();
      }
      static_cast<SpFireSimulator *>(effect)->SetLevel(0, 1);
      mode = 3;
      destroyed = true;
      cost = manager->layer->GetDemolitionCost(occupant);
      manager->FinishInstance(this, 1);
    } else {
      static_cast<SpFireSimulator *>(effect)->SetLevel(3, 1);
      mode = 1;
    }
    break;
  }
  return true;
}

DsLocustCells::DsLocustCells() : anchor(new DsLocustLink) {
  anchor->next = anchor;
  anchor->previous = anchor;
}

// Retire each node in forward order, then release the anchor after all record storage has been discarded.
DsLocustCells::~DsLocustCells() {
  DsLocustLink *node = anchor->next;
  while (node != anchor) {
    DsLocustLink *retired = node;
    node = node->next;
    delete retired;
  }
  delete anchor;
}

// Establish the three identities and original written fields, with cell/vector ownership preceding the recursive lock.
// Leave padding and the unused list-anchor payload unwritten, then initialize saved state and invalid effect handles.
DsLocustInstance::DsLocustInstance()
    : initialized(false), manager(nullptr), swarm(nullptr), automatic_start(0), references(0), flag_32(0), flag_33(0),
      flag_34(0), flag_35(0), location_cursor(0), flag_40(0), word_44(0), word_48(0), word_52(0), destroyed(0), cost(0),
      flag_124(0) {
  record = {};
  for (u32 index = 0; index < 5; ++index)
    effects[index] = kApInvalidNotificationId;
}

DsLocustInstance::~DsLocustInstance() {
  if (initialized)
    Shutdown();
}

// Publish the common or locust-private identity before its virtual acquisition, clearing the output.
bool DsLocustInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsLocustState, DsLocustState);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsLocustInstance::AddRef() {
  return ++references;
}

// Decrement only counts greater than one; zero and one delete the complete instance without first changing its count.
// Preserve the original count observed by active destruction and return zero after the final release.
u32 DsLocustInstance::Release() {
  if (references > 1)
    return --references;
  delete this;
  return 0;
}

// Publish the whole-city rectangle only after initialization and phase three, querying the X dimension twice.
// Preserve the original defined square-bounds behavior and reverse output-clear order on inactive paths.
bool DsLocustInstance::GetBounds(GeCellRect *output) {
  if (initialized && record.phase >= 3) {
    output->left = 0;
    output->top = 0;
    output->right = manager->city->CellCountX() - 1;
    output->bottom = manager->city->CellCountX() - 1;
    return true;
  }
  output->bottom = 0;
  output->right = 0;
  output->top = 0;
  output->left = 0;
  return false;
}

// Return the shared null occupant identity; locust swarms are not represented by a single city occupant.
ScOccupant *DsLocustInstance::GetOccupant() {
  return nullptr;
}

// Return the borrowed manager through its actual common interface, preserving a missing owner as null.
DsManager *DsLocustInstance::GetManager() {
  return manager;
}

u32 DsLocustInstance::TypeId() {
  return kDsLocustTypeId;
}

u32 DsLocustInstance::GetDestroyedCount() {
  return destroyed;
}

u32 DsLocustInstance::GetCost() {
  return cost;
}

void DsLocustInstance::GetRecord(DsLocustRecord *output) {
  *output = record;
}

// Publish active ownership and notice timing before registering six notifications through one borrowed server.
// Optional immediate progression runs after the controller receives the pending instance and before enablement is stored.
u8 DsLocustInstance::Init(DsLocustManager *owner, u8 automatic, u8 premonitions, u8 enabled) {
  if (!initialized) {
    initialized = true;
    manager = owner;
    automatic_start = automatic;
    record.phase = 0;
    record.phase_ticks = 0;
    word_44 = DsLocustFirstNoticeTime;
    word_48 = DsLocustSecondNoticeTime;
    word_52 = DsLocustAttackTime;
    if (!premonitions) {
      record.phase = 2;
      word_44 = 0;
      word_48 = 0;
      word_52 = 0;
    }
    ScMessageServer *server = DsGetMessageServer();
    const u32 messages[] = {kDsMsg_UnresolvedLocustNotification, kSpMsg_LocustSprayCheck, kSpMsg_LocustOccupantContact,
        kDsMsg_DispatchPlane, kSpMsg_SprayTrailStarted, kSpMsg_SprayTrailContinued};
    for (u32 index = 0; index < 6; ++index)
      server->AddNotification(static_cast<ScMessageTarget *>(this), messages[index]);
    manager->layer->DisasterInstanceChange(this, 0);
    if (!premonitions)
      Advance(0, 0);
    flag_40 = enabled;
  }
  return 1;
}

// Copy and provisionally constrain saved state before live binding, then reread the first five caller words afterward.
// Resume phases three and above through the real virtual tick after publishing phase two and the attack-time counter.
u8 DsLocustInstance::Init(DsLocustManager *owner, const DsLocustRecord *saved) {
  record = *saved;
  if (record.swarm_count > DsLocustCheatSwarmMaximum)
    record.swarm_count = DsLocustCheatSwarmMaximum;
  if (record.phase > 4)
    record.phase = 0;
  if (record.phase_ticks > 10000)
    record.phase_ticks = 0;
  Init(owner, 0, 1, 0);
  record.date = saved->date;
  record.swarm_count = saved->swarm_count;
  record.word_8 = saved->word_8;
  u32 phase = saved->phase;
  record.phase = phase;
  record.phase_ticks = saved->phase_ticks;
  if (phase >= 3) {
    record.phase_ticks = word_52;
    record.phase = 2;
    Tick(DsDayTick);
  }
  flag_40 = 1;
  return 1;
}

bool DsLocustInstance::Tick(DsTickType tick) {
  return Advance(tick, 1);
}

// Select the phase resource with its original random draw, expand its text and send the mutable synchronous payload.
// Store the returned effect after releasing the message text, then release the current loaded resource identity.
void DsLocustInstance::PostPhase(u32 state) {
  i32 slot;
  u32 resource;
  if (state == 1) {
    resource = manager->random.Next(2) + 120;
    slot = 0;
  } else if (state == 2) {
    resource = manager->random.Next(2) + 123;
    slot = 1;
  } else if (state == 4) {
    if (flag_124 == 1) {
      resource = manager->random.Next(3) + 129;
      slot = 3;
    } else {
      if (!destroyed)
        return;
      resource = manager->random.Next(3) + 133;
      slot = 4;
    }
  } else
    return;
  auto *text = new ScString;
  DsTextResource content(resource, kRsDisasterNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsDisasterNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  if (slot >= 0)
    effects[slot] = notice.effect;
}

void DsLocustInstance::ClearEffects() {
  for (u32 index = 0; index < 5; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

i8 DsLocustInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return record.phase >= 3 ? levels[distance] : 0;
}

u32 DsLocustInstance::PanicRadius() {
  return record.phase >= 3 ? 5 : 0;
}

u32 DsLocustInstance::GetRecords(DsLocustRecord *records, u32 *count) const {
  return manager->GetRecords(records, count);
}

u32 DsLocustInstance::GetCount() {
  return swarm ? static_cast<SpLocustGroupSimulator *>(swarm)->GetCount() : 0;
}

bool DsLocustInstance::IsActive() {
  return swarm && static_cast<SpLocustGroupSimulator *>(swarm)->GetLocusts(0) != nullptr;
}

// Cycle through owned swarms or the global fifty-entry fallback, preserving preincremented cursor and borrowed lifetime.
// The fallback deliberately reports the child's altitude as Z; both paths logically shift X then Z and preserve aliases.
bool DsLocustInstance::GetLocation(u32 *x, u32 *z) {
  if (swarm) {
    u32 count = static_cast<SpLocustGroupSimulator *>(swarm)->GetCount();
    if (count) {
      ++location_cursor;
      if (location_cursor >= count)
        location_cursor = 0;
      SpLocustSimulator *child = static_cast<SpLocustGroupSimulator *>(swarm)->GetLocusts(location_cursor);
      if (child) {
        u32 altitude;
        child->GetPosition(x, &altitude, z);
        *x >>= 8;
        *z >>= 8;
        return true;
      }
    } else {
      SpLocustSimulator *children[50];
      count = 50;
      static_cast<SpLocustGroupSimulator *>(swarm)->GetAllLocustsAnywhere(children, &count);
      if (count) {
        ++location_cursor;
        if (location_cursor >= count)
          location_cursor = 0;
        u32 unused_z;
        children[location_cursor]->GetPosition(x, z, &unused_z);
        *x >>= 8;
        *z >>= 8;
        return true;
      }
    }
  }
  *z = kDsInvalidCellCoordinate;
  *x = kDsInvalidCellCoordinate;
  return false;
}

// Capture the owned swarm count and queue deferred disappearance from the last child back to the first.
// Re-read the current controller for each call, retaining the original lack of random draws.
u8 DsLocustInstance::QueueDeparture() {
  if (swarm) {
    u32 count = static_cast<SpLocustGroupSimulator *>(swarm)->GetCount();
    while (count) {
      --count;
      static_cast<SpLocustGroupSimulator *>(swarm)->AddInstructionDisappear(i32(count), 1);
    }
  }
  return 1;
}

// Advance premonitions and the live attack while preserving the original raw running gate and unsigned phase deadlines.
// Publish visibility and eating flags before their manager callbacks, then retire through real notices and history.
u8 DsLocustInstance::Advance(i32, u8 running) {
  if (running && !flag_40)
    return 1;
  u32 phase = record.phase, elapsed = ++record.phase_ticks;
  switch (phase) {
  case 0:
    if (elapsed >= word_44) {
      record.phase = 1;
      PostPhase(1);
    }
    break;
  case 1:
    if (elapsed >= word_48) {
      record.phase = 2;
      PostPhase(2);
    }
    break;
  case 2:
    if (elapsed >= word_52) {
      record.phase = 3;
      PostPhase(3);
      if (ExecuteAttack()) {
        flag_35 = 1;
        manager->BeginNotice233();
      } else
        record.phase = 3;
      manager->layer->DisasterInstanceChange(this, 1);
    }
    break;
  case 3:
    if (!GetCount()) {
      record.phase = 4;
      QueueDeparture();
    }
    if (swarm) {
      u8 visible = u8(static_cast<SpLocustGroupSimulator *>(swarm)->IsAnyLocustsCurrentlyVisible());
      if (flag_32 != visible) {
        flag_32 = visible;
        manager->ChangeNotice233Mode(visible);
      }
      u32 eating, near_camera;
      static_cast<SpLocustGroupSimulator *>(swarm)->GetEatingSwarmsCount(&eating, &near_camera);
      u8 has_eating = eating != 0;
      if (flag_33 != has_eating) {
        flag_33 = has_eating;
        manager->ChangeNotice235(has_eating);
      }
      u8 has_near_camera = near_camera != 0;
      if (flag_34 != has_near_camera) {
        flag_34 = has_near_camera;
        manager->ChangeNotice235Mode(has_near_camera);
      }
    }
    break;
  case 4: {
    auto *controller = static_cast<SpLocustGroupSimulator *>(swarm);
    if (!controller) {
      manager->FinishInstance(this);
      return 1;
    }
    if (!controller->DoesAnyLocustsExistAnywhere()) {
      PostPhase(record.phase);
      ClearEffects();
      manager->FinishInstance(this);
      return 1;
    }
    if (record.phase_ticks < word_52 + 600u) {
      u8 visible = u8(static_cast<SpLocustGroupSimulator *>(swarm)->IsAnyLocustsCurrentlyVisible());
      if (flag_32 != visible) {
        flag_32 = visible;
        manager->ChangeNotice233Mode(visible);
      }
    } else {
      PostPhase(record.phase);
      ClearEffects();
      manager->FinishInstance(this);
      if (swarm)
        static_cast<SpLocustGroupSimulator *>(swarm)->DestroyAllLocusts(1);
    }
    break;
  }
  }
  return 1;
}

// Capture the original linked-cell count, then request removal at the current tail's two stored coordinates each time.
// Failed removals retain the original bounded retry behavior and do not become a new game result.
u8 DsLocustInstance::ClearCells() {
  u32 count = 0;
  for (DsLocustLink *node = cells.anchor->next; node != cells.anchor; node = node->next)
    ++count;
  while (count) {
    RemoveCell(cells.anchor->previous->x, cells.anchor->previous->z);
    --count;
  }
  return 1;
}

// Clear readiness before callbacks, remove live cells and decrement only a signed-positive manager activity count.
// Request asynchronous swarm retirement before clearing its pointer, then remove notices and all six subscriptions.
u8 DsLocustInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    if (flag_32)
      manager->ChangeNotice233Mode(0);
    ClearCells();
    if (i32(manager->active_count) > 0)
      --manager->active_count;
    if (swarm) {
      static_cast<SpLocustGroupSimulator *>(swarm)->ReleaseAsynchronous();
      swarm = nullptr;
    }
    if (flag_35) {
      manager->EndNotice233();
      flag_35 = 0;
    }
    ScMessageServer *server = DsGetMessageServer();
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kDsMsg_UnresolvedLocustNotification);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_LocustSprayCheck);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_LocustOccupantContact);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kDsMsg_DispatchPlane);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_SprayTrailStarted);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_SprayTrailContinued);
    manager = nullptr;
  }
  return 1;
}

// Remove the matching cell under the original lock, revoking its airplane before detaching the placed animation occupant.
// Keep each retained pointer live through its service callback, release before unlinking, then unlock on both outcomes.
u8 DsLocustInstance::RemoveCell(u32 x, u32 z) {
  lock.Lock();
  DsLocustLink *node = cells.anchor->next;
  while (node != cells.anchor && (node->x != x || node->z != z))
    node = node->next;
  if (node == cells.anchor) {
    lock.Unlock();
    return 0;
  }
  if (node->airplane) {
    DsGetCity()->StrtSimLayer()->RevokeDispatchedAirplane(node->airplane);
    node->airplane->Release();
    node->airplane = nullptr;
  }
  void *queried;
  if (node->object->QueryInterface(IID_ScOccupant, &queried)) {
    auto *occupant = static_cast<ScOccupant *>(queried);
    DsGetCity()->AnimOccupantManager()->RemoveOccupant(occupant);
    occupant->Release();
  }
  node->object->Release();
  node->object = nullptr;
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
  lock.Unlock();
  return 1;
}

void DsLocustInstance::AddSprayArea(u32 x, u32 z) {
  *targets.Append() = {i32(x - 3), i32(z - 3), i32(x + 3), i32(z + 3)};
}

// Expand the queried cell by three and report the first inclusive intersection with a retained spray rectangle.
u8 DsLocustInstance::IsSprayArea(i32 x, i32 z) {
  ScRect bounds{i32(u32(x) - 3), i32(u32(z) - 3), i32(u32(x) + 3), i32(u32(z) + 3)}, intersection;
  for (u32 index = 0; index < targets.size; ++index)
    if (intersection.IntersectInclusive(&bounds, &targets.data[index]))
      return 1;
  return 0;
}

// Dispatch active-phase damage, airplane, poisoning and spray-trail messages through the original game interfaces.
// Clear the sender word only after accepted-phase callbacks, retaining captured coordinates where the original saves them.
bool DsLocustInstance::DoMessage(ScMessage *message) {
  u32 phase = record.phase;
  if (phase == 3 || phase == 4) {
    switch (message->type) {
    case kDsMsg_DispatchPlane:
      DispatchSprayAircraft(u16(message->data1), u16(message->data2));
      break;
    case kSpMsg_LocustOccupantContact: {
      ScOccupant *occupant;
      u32 packed = u32(message->data2);
      if (message->data1 && manager->occupants->GetOccupantAtCell(packed >> 16, u16(packed), &occupant)) {
        u32 charge;
        if (manager->layer->DestroyOccupant(occupant, kDsDamageLocust, &charge)) {
          cost += charge;
          ++destroyed;
        }
        occupant->Release();
      }
      break;
    }
    case kSpMsg_LocustSprayCheck: {
      auto *child = reinterpret_cast<SpLocustSimulator *>(message->data1);
      if (child) {
        i32 x = u16(u32(message->data2) >> 16), z = u16(message->data2);
        if (IsSprayArea(x, z) == 1) {
          static_cast<SpLocustGroupSimulator *>(swarm)->AddInstructionPoisonSwarm(x, z, child, 0);
          flag_124 = 1;
        }
      }
      break;
    }
    case kSpMsg_SprayTrailStarted:
    case kSpMsg_SprayTrailContinued: {
      if (message->type == kSpMsg_SprayTrailStarted)
        ClearCells();
      u32 x = u32(message->data1), z = u32(message->data2);
      i32 altitude = i32(u32(message->data3));
      static_cast<SpLocustGroupSimulator *>(swarm)->DropDDTTrail(i32(x), i32(z), altitude);
      AddSprayArea(x >> kCtMicroCoordinateShift, z >> kCtMicroCoordinateShift);
      break;
    }
    }
    message->data1 = 0;
  }
  return true;
}

// Create the real swarm controller and distribute the scanned unsigned crop targets after setting swarm count.
// Apply the original no-crop random choice or crop-ratio limits, then the cheat override, and publish the date last.
u8 DsLocustInstance::ExecuteAttack() {
  ScVec3U crop_cells[512];
  if (!DsGetCom()->GetClassObject(kSpLocustGroupSimulatorClassId, IID_SpLocustGroupSimulator, &swarm))
    return 0;
  u32 count = 0;
  manager->FindCropCells(crop_cells, &count, DsLocustMaximumCrops);
  u32 swarms;
  if (count) {
    swarms = (count - 1) / DsLocustSwarmCropRatio + 1;
    if (swarms < DsLocustMinimumSwarms)
      swarms = DsLocustMinimumSwarms;
    if (swarms > DsLocustMaximumSwarms)
      swarms = DsLocustMaximumSwarms;
  } else
    swarms = (manager->random.Next(2) != 0) + 1;
  if (manager->cheats->GetCheatHasBeenSet(kDsLocustSwarmCheatId))
    swarms = DsLocustCheatSwarmMaximum;
  static_cast<SpLocustGroupSimulator *>(swarm)->SetCount(swarms);
  static_cast<SpLocustGroupSimulator *>(swarm)->DistributeTargetListAmongSwarms(crop_cells, count);
  CtDate date;
  manager->city->GetSimulationClock()->GetCurrentClockDate(&date);
  record.swarm_count = swarms;
  record.date = date.serial;
  return 1;
}

// Construct the actual SIMDSTR building base, retaining its local director and query bindings.
// The original dispatch inlines this constructor before installing the static-animation virtual tables.
GeStaticAnimBuilding::GeStaticAnimBuilding() = default;

u8 GeStaticAnimBuilding::AnimType() const {
  return 1;
}

// Start the queried animation interface at the captured packed position, then release its current output identity.
// Capture position before the virtual cell-width query so callbacks cannot replace the coordinates used here.
void GeStaticAnimBuilding::AnimStart() {
  SpAnimation *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, reinterpret_cast<void **>(&animation))) {
    u32 origin = position;
    u32 width = SizeInCellsX();
    animation->Start(i32((origin & 0x7ffu) << kCtMicroCoordinateShift),
        i32(((origin >> 11) & 0x7ffu) << kCtMicroCoordinateShift),
        i32(u32(u8(origin >> 22)) << kCtMicroCoordinateShift), width);
    animation->Release();
  }
}

// Stop the sprite's acquired animation view when supported and release the live interface output afterward.
// Missing sprites or rejected queries leave the building and its retained sprite untouched.
void GeStaticAnimBuilding::AnimStop() {
  SpAnimation *animation = nullptr;
  SpSprite *current = GetSpriteInst();
  if (current && current->QueryInterface(IID_SpAnimation, reinterpret_cast<void **>(&animation))) {
    animation->Stop();
    animation->Release();
  }
}

i32 GeStaticAnimBuilding::Tick(u32 elapsed) {
  SpSprite *current = GetSpriteInst();
  return current ? current->Tick(elapsed) : 1;
}

// Reuse the oldest retained aircraft at the three-dispatch cap, replacing its placed static-animation building.
// Append the new payload under the instance lock after placement, preserving the original callback and notice order.
u8 DsLocustInstance::DispatchSprayAircraft(u32 x, u32 z) {
  u32 count = 0;
  for (DsLocustLink *node = cells.anchor->next; node != cells.anchor; node = node->next)
    ++count;
  ScOccupant *airplane;
  u32 target_x;
  if (count >= 3) {
    DsLocustLink *node = cells.anchor->next;
    airplane = node->airplane;
    ScUnknown *object = node->object;
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete node;
    target_x = x;
    if (x < DsGetCity()->CellCountX() - 1)
      target_x = x + 1;
    CtStartSimLayer *simulation = DsGetCity()->StrtSimLayer();
    if (simulation) {
      ScOccupant *placed;
      if (object->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&placed))) {
        DsGetCity()->AnimOccupantManager()->RemoveOccupant(placed);
        placed->Release();
      }
      object->Release();
      object = nullptr;
      simulation->RedirectAirplane(airplane, target_x, z);
    }
  } else {
    target_x = x;
    if (x < DsGetCity()->CellCountX() - 1)
      target_x = x + 1;
    CtStartSimLayer *simulation = DsGetCity()->StrtSimLayer();
    assert(simulation && "Dispatching an airplane requires the street simulation");
    airplane = reinterpret_cast<ScOccupant *>(simulation->DispatchAirplane(target_x, z));
    if (airplane)
      airplane->AddRef();
  }
  auto *building = new GeStaticAnimBuilding;
  building->AddRef();
  ScUnknown *object = static_cast<BdBuilding *>(building);
  RsKey key{kRsBuildingDataResourceType, kRsBuildingDataGroup, 16303};
  building->Init(&key);
  building->SetLocationInCellCoords(target_x, z);
  u8 altitude;
  DsGetCity()->DirtBag()->GetVertexAltitude(target_x, z, &altitude);
  building->SetAlt(altitude);
  ScOccupant *placed;
  if (building->QueryInterface(IID_ScOccupant, reinterpret_cast<void **>(&placed))) {
    DsGetCity()->AnimOccupantManager()->InsertOccupant(placed, 1, 0, true);
    placed->Release();
  }
  lock.Lock();
  DsLocustLink *anchor = cells.anchor;
  auto *node = new DsLocustLink{anchor, anchor->previous, airplane, target_x, z, object};
  anchor->previous->next = node;
  anchor->previous = node;
  lock.Unlock();
  ApBoxX *box = DsGetBoxX();
  if (box)
    box->Event(kAuEventPlayEffect, kAuEffectUiCropDispatch, 0, 0, 0);
  return 1;
}
