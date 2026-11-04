#include "disasters/ds_tornado.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_array.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "disasters/api.h"
#include "disasters/ds_events.h"
#include "disasters/ds_module.h"
#include "disasters/ds_toxic.h"
#include "economy/api.h"
#include "geometry/api.h"
#include "geometry/ge_types.h"
#include "misc/sc_graph_info.h"
#include "network/api.h"
#include "network/street.h"
#include "occupants/animation_manager.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "sprites/sp_effect.h"
#include "terrain/api.h"

DsRiotInstance::DsRiotInstance()
    : initialized(false), in_camera(false), references(0), manager(nullptr), effect(nullptr), elapsed(0), x(0), z(0),
      phase(0), effects{kApInvalidNotificationId, kApInvalidNotificationId, kApInvalidNotificationId}, automatic(1),
      running(0), retarget(false) {
}

DsRiotInstance::~DsRiotInstance() {
  Shutdown();
}

// Publish primary Unknown/disaster or private movement identity and retain exactly once on success. Unsupported
// identities clear the output.
bool DsRiotInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsRiotState, DsRiotState);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsRiotInstance::AddRef() {
  return ++references;
}

// Drop a held reference without underflowing zero and delete the complete instance when the resulting count is zero.
u32 DsRiotInstance::Release() {
  if (references)
    --references;
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

// Bound the initialized riot's twenty-cell reach against current city dimensions, preserving unsigned additions.
// An inactive receiver writes the zero rectangle in bottom/top/right/left order without touching its manager.
bool DsRiotInstance::GetBounds(GeCellRect *output) {
  if (initialized) {
    u32 maximum_x = manager->city->CellCountX() - 1, maximum_z = manager->city->CellCountZ() - 1;
    output->left = x <= 20 ? 0 : x - 20;
    output->top = z <= 20 ? 0 : z - 20;
    output->right = x + 20;
    if (output->right > maximum_x)
      output->right = maximum_x;
    output->bottom = z + 20;
    if (output->bottom > maximum_z)
      output->bottom = maximum_z;
    return true;
  }
  output->bottom = 0;
  output->top = 0;
  output->right = 0;
  output->left = 0;
  return false;
}

// Copy saved X then Z without a readiness gate. Aliased outputs therefore retain Z.
bool DsRiotInstance::GetLocation(u32 *output_x, u32 *output_z) {
  *output_x = x;
  *output_z = z;
  return true;
}

// Return the shared null occupant result; a riot owns protected cells rather than one retained occupant.
ScOccupant *DsRiotInstance::GetOccupant() {
  return nullptr;
}

// Borrow the manager's real common identity while preserving a null manager before initialization.
DsManager *DsRiotInstance::GetManager() {
  return manager;
}

u32 DsRiotInstance::TypeId() {
  return kDsRiotTypeId;
}

u32 DsRiotInstance::GetDestroyedCount() {
  return destroyed;
}

u32 DsRiotInstance::GetCost() {
  return cost;
}

bool DsRiotInstance::Tick(DsTickType tick) {
  return Advance(tick, 1);
}

i32 DsRiotInstance::GetRecords(DsRiotHistory *records, u32 *count) const {
  return manager->GetRecords(records, count);
}

void DsRiotInstance::RequestRetarget() {
  retarget = true;
}

// Publish location, owner and phase timing before counting/registering the new instance and running its first step.
// Keep the normal-update gate clear across registration and that direct initialization step, then enable it last.
u8 DsRiotInstance::Init(
    DsRiotManager *owner, u32 cell_x, u32 cell_z, u32 initial_cost, u32 initial_destroyed, u8 automatic_start) {
  x = cell_x;
  z = cell_z;
  manager = owner;
  automatic = automatic_start;
  if (automatic_start) {
    phase = 0;
    elapsed = 0;
  } else {
    phase = 1;
    elapsed = DsRiotInitialDelay;
  }
  initialized = true;
  cost = initial_cost;
  destroyed = initial_destroyed;
  manager->InstanceStarted();
  running = 0;
  manager->layer->DisasterInstanceChange(this, 0);
  Advance(0, 0);
  running = 1;
  return 1;
}

// Publish the current cell, then acquire protection for its first occupant or increment the existing cell's use count.
// Re-find after synchronous occupant callbacks before assigning count1, preserving the source map's live insertion.
void DsRiotInstance::EnterCell(u32 cell_x, u32 cell_z) {
  if (initialized) {
    x = cell_x;
    z = cell_z;
    u32 key = cell_x + (cell_z << 16), index = 0;
    while (index < cells.size && cells.data[index].key < key)
      ++index;
    if (index < cells.size && cells.data[index].key == key) {
      cells.data[index].count = i32(u32(cells.data[index].count) + 1);
    } else {
      ScOccupant *occupant;
      if (manager->occupants->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
        occupant->SetSimRemove(false);
        occupant->SetUserRemove(false);
        occupant->Release();
        index = 0;
        while (index < cells.size && cells.data[index].key < key)
          ++index;
        if (index == cells.size || cells.data[index].key != key) {
          cells.Append();
          for (u32 move = cells.size - 1; move > index; --move)
            cells.data[move] = cells.data[move - 1];
        }
        cells.data[index] = {key, 1};
      }
    }
  }
}

// Restore both removal permissions for the packed cell's current occupant, releasing its lookup reference last.
// The original second stack operand points to the count but is never read or forwarded.
void DsRiotInstance::ReleaseCell(u32 key, i32 *) {
  ScOccupant *occupant;
  if (manager->occupants->GetOccupantAtCell(u16(key), key >> 16, &occupant)) {
    occupant->SetSimRemove(true);
    occupant->SetUserRemove(true);
    occupant->Release();
  }
}

// Decrement a protected cell and restore permissions only when its signed count reaches zero or less.
// The installed method erases the entry even for a still-positive count; preserve that defined behavior.
void DsRiotInstance::LeaveCell(u32 cell_x, u32 cell_z) {
  if (initialized) {
    u32 key = cell_x + (cell_z << 16), index = 0;
    while (index < cells.size && cells.data[index].key < key)
      ++index;
    if (index < cells.size && cells.data[index].key == key) {
      cells.data[index].count = i32(u32(cells.data[index].count) - 1);
      if (cells.data[index].count <= 0)
        ReleaseCell(key, &cells.data[index].count);
      index = 0;
      while (index < cells.size && cells.data[index].key != key)
        ++index;
      assert(index < cells.size);
      for (u32 move = index + 1; move < cells.size; ++move)
        cells.data[move - 1] = cells.data[move];
      --cells.size;
    }
  }
}

i32 DsRiotInstance::ContainsCell(u32 cell_x, u32 cell_z) {
  return cell_x < manager->city->CellCountX() && cell_z < manager->city->CellCountZ();
}

// Attempt ignition only for an initialized, in-bounds retained occupant through the manager's actual fire dependency.
// Count a nonzero ignition result before releasing the occupant, preserving the independent destruction total.
void DsRiotInstance::IgniteCell(u32 cell_x, u32 cell_z) {
  ScOccupant *occupant;
  if (initialized && ContainsCell(cell_x, cell_z) && manager->occupants->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
    if (manager->fire->IgniteOccupant(occupant, 0))
      ++destroyed;
    occupant->Release();
  }
}

// Expand the selected phase1 or phase3 resource and send its mutable synchronous notice with an actual String hold.
// Only phase1 stores the returned effect, after releasing text and before the resource handle retires.
void DsRiotInstance::PostPhase(u32 state) {
  u32 resource;
  if (state == 1)
    resource = manager->random.Next(6) + 614;
  else if (state == 3)
    resource = manager->random.Next(3) + 623;
  else
    return;
  auto *text = new ScString;
  DsTextResource content(resource, kRsNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  if (state == 1)
    effects[0] = notice.effect;
}

// Remove the three transient effects in order, retaining each published handle until its synchronous message returns.
void DsRiotInstance::ClearEffects() {
  for (u32 index = 0; index < 3; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Refresh actual location and camera membership, changing manager counters before publishing the new camera flag.
void DsRiotInstance::RefreshVisibility() {
  u32 cell_x, cell_z;
  if (GetLocation(&cell_x, &cell_z)) {
    bool visible = DsController::IsInVisibleRect(cell_x, cell_z) != 0;
    if (visible != in_camera) {
      if (visible)
        manager->EnterCamera();
      else
        manager->LeaveCamera();
      in_camera = visible;
    }
  }
}

// Retain the current cell's occupant and apply the actual riot predicate before attempting swarm attachment.
// Preserve the attachment's raw byte result and release the retained occupant after that callback.
u8 DsRiotInstance::TryStartCell(u32 cell_x, u32 cell_z) {
  ScOccupant *occupant;
  u8 result = 0;
  if (manager->occupants->GetOccupantAtCell(cell_x, cell_z, &occupant)) {
    result = u8(DsRiotAcceptsOccupant(occupant));
    if (result)
      result = AttachToOccupant(occupant);
    occupant->Release();
  }
  return result;
}

// Publish terminal phase3 and its notice before removing through the manager read after synchronous callbacks.
// Retain the final reference-count transport rather than claiming an inferred Boolean success result.
u32 DsRiotInstance::Finish() {
  phase = 3;
  PostPhase(3);
  return manager->FinishInstance(this);
}

// Release the prior swarm before considering three retained random occupants while simulation is running.
// A different origin counts as a retarget attempt even when attachment refuses; finish only when none is found.
void DsRiotInstance::Retarget() {
  if (initialized) {
    ScRelease(&effect);
    if (manager->layer->IsSimulationRunning()) {
      bool found = false;
      for (u32 attempt = 0; attempt < 3 && !found; ++attempt) {
        ScOccupant *occupant;
        if (!manager->FindRandomOccupant(&occupant))
          break;
        u32 cell_x, cell_z;
        occupant->GetLocationInCellCoords(&cell_x, &cell_z);
        if (cell_x != x || cell_z != z) {
          AttachToOccupant(occupant);
          found = true;
        }
        occupant->Release();
      }
      if (!found)
        Finish();
    }
  }
}

// Advance premonition, preparation and active-riot phases under the actual running gate and signed timing tests.
// Preserve police-coverage sampling every eighth active tick, per-tick cost, retarget ordering and camera cadence.
u8 DsRiotInstance::Advance(i32, u8 normal_update) {
  if (!initialized)
    return 0;
  if (normal_update && !running)
    return 1;
  i32 current_phase = phase;
  elapsed = i32(u32(elapsed) + 1);
  if (current_phase == 0) {
    if (elapsed >= DsRiotPremonitionTicks) {
      phase = 1;
      PostPhase(1);
      elapsed = 0;
    }
  } else if (current_phase == 1) {
    if (elapsed >= DsRiotInitialDelay) {
      if ((!automatic || manager->GetUnemployment() >= DsRiotMinimumUnemployment) && TryStartCell(x, z)) {
        phase = 2;
        ClearEffects();
        PostPhase(u32(phase));
        manager->EffectsStarted();
        manager->layer->DisasterInstanceChange(this, 1);
        elapsed = 0;
        camera_ticks = 5;
      } else {
        manager->FinishInstance(this);
      }
    }
  } else if (current_phase == 2) {
    if (retarget) {
      retarget = false;
      Retarget();
    }
    cost += DsRiotCostPerTick;
    bool expired = false;
    if (!(u32(elapsed) & 7)) {
      // Both original callers push literal 0/1 DWORDs; the output backing's high byte therefore starts at zero.
      u8 value = 0;
      manager->police->GetValue(x, z, &value);
      i32 duration = i32(i64(f64(DsRiotMaximumDuration) - f64(value) * f64(DsRiotDurationScale)));
      expired = elapsed >= duration;
    }
    if (expired) {
      Finish();
    } else if (++camera_ticks > 4) {
      camera_ticks = 0;
      RefreshVisibility();
    }
  }
  return 1;
}

i8 DsRiotInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return levels[distance];
}

u32 DsRiotInstance::PanicRadius() {
  return 5;
}

bool DsRiotInstance::IsActive() {
  return effect != nullptr;
}

// Capture the tile origin and publish the retained street-simulation crowd before binding its disaster callbacks.
// Configure the signed fireball interval on the live crowd, then return the tile-placement operation's raw byte.
u8 DsRiotInstance::AttachToOccupant(ScOccupant *occupant) {
  occupant->GetLocationInCellCoords(&x, &z);
  effect = manager->simulation->CreateRiotCrowd();
  if (!effect)
    return 0;
  effect->SetAssociatedRiotInstance(static_cast<DsRiotState *>(this));
  effect->SetMinAndMaxTimeBetweenFireballThrows(DsRiotMinimumFireballTime, DsRiotMaximumFireballTime);
  return effect->PlaceOnTransitTile(occupant);
}

// Clear readiness before ending camera/effect/activity counts and removing transient notices in original order.
// Disburse and release the live crowd, then restore each protected occupant before clearing the cell records.
u8 DsRiotInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    if (in_camera)
      manager->LeaveCamera();
    if (phase >= 2)
      manager->EffectsEnded();
    manager->InstanceEnded();
    ClearEffects();
    if (effect) {
      effect->Disburse();
      effect->Release();
      effect = nullptr;
    }
    for (u32 index = 0; index < cells.size; ++index)
      ReleaseCell(cells.data[index].key, &cells.data[index].count);
    cells.Clear();
    initialized = false;
  }
  return 1;
}

i32 DsTornadoMinimumInterval = 12, DsTornadoMaximumInterval = 36;
u32 DsTornadoMaximumInstances = 0;
f32 DsTornadoMinimumSpeed = 0.11111000180244446f, DsTornadoMaximumSpeed = 0.3333300054073334f;
i32 DsTornadoMinimumDuration = 10, DsTornadoMaximumDuration = 400;
i32 DsTornadoFormationTicks = 80;
i32 DsTornadoUiId = 0;
u32 DsTornadoDamageChance[3] = {20, 50, 70};
// Original3AA30/39AE8/39AEC/39AF0 phase thresholds; advancement compares these unsigned words.
u32 DsTornadoFirstDelay = 0, DsTornadoSecondDelay = 20, DsTornadoThirdDelay = 40, DsTornadoAttackDelay = 60;

DsTornadoManager::DsTornadoManager()
    : initialized(false), layer(nullptr), city(nullptr), occupants(nullptr), random(kScRandomSeedFromClock),
      active_count(0), effect_count(0), camera_count(0), box(nullptr), flags_96(0), elapsed(0), interval(0) {
}

DsTornadoManager::~DsTornadoManager() {
  if (initialized)
    DsTornadoManager::Shutdown();
}

// Advance the tornado cadence and select its next interval on expiry. Automatic spawning calls the original
// random-start method directly, after storing the reset counter and new interval, and ignores its result.
bool DsTornadoManager::Tick() {
  elapsed = i32(u32(elapsed) + 1);
  if (elapsed >= interval) {
    elapsed = 0;
    interval = DsTornadoMinimumInterval;
    if (DsTornadoMinimumInterval < DsTornadoMaximumInterval)
      interval = random.Range(DsTornadoMinimumInterval, DsTornadoMaximumInterval);
    DsTornadoManager::StartRandom(true);
  }
  return true;
}

// Construct the instance between the real and final integer draws; initialization's result does not affect success.
bool DsTornadoManager::StartRandom(bool automatic) {
  if (active_count > DsTornadoMaximumInstances)
    return false;
  u32 x = random.Next(maximum_x), z = random.Next(maximum_z);
  f64 component = random.RealRange(-1.0, 1.0);
  ScVec3F direction = {f32(component), 0.0f, f32(sqrt(1.0 - component * component))};
  f32 speed = f32(random.RealRange(DsTornadoMinimumSpeed, DsTornadoMaximumSpeed));
  DsTornadoInstance *instance = new DsTornadoInstance;
  i8 severity = i8(random.Next(3));
  i32 duration = random.Range(DsTornadoMinimumDuration, DsTornadoMaximumDuration);
  instance->Init(this, x, z, &direction, severity, speed, duration, 0, 0, automatic);
  return true;
}

bool DsTornadoManager::StaticShutdown() {
  return true;
}

// Remove the load notification before releasing retained layer, agent tree, occupants, BoxX, terrain and city.
// Clear each pointer after its release and leave the initialization flag set until all six releases finish.
bool DsTornadoManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&city);
    initialized = false;
  }
  return true;
}

// Replace any initialized city binding, acquiring the original city, terrain, agent tree, BoxX, occupants and layer
// in order. Reset activity, register the load notification, then choose the first interval before marking ready.
bool DsTornadoManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized)
    Shutdown();
  city = ScAddRefNotNull(retained_city);
  terrain = ScAddRefNotNull(city->DirtBag());
  agent_types = ScAddRefNotNull(DsGetAgentTree());
  maximum_x = city->CellCountX() - 1;
  maximum_z = city->CellCountZ() - 1;
  box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
  occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
  layer = ScAddRefNotNull(retained_layer);
  active_count = 0;
  effect_count = 0;
  camera_count = 0;
  flags_96 = 0;
  DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  elapsed = 0;
  interval = DsTornadoMinimumInterval;
  if (DsTornadoMinimumInterval < DsTornadoMaximumInterval)
    interval = random.Range(DsTornadoMinimumInterval, DsTornadoMaximumInterval);
  initialized = true;
  return true;
}

// Start one tornado at the supplied position when the active count does not exceed its cap. Return its borrowed
// private motion identity after initialization, preserving the original ignored initialization result.
DsTornadoMotion *DsTornadoManager::Start(
    u32 x, u32 z, const ScVec3F *direction, i8 severity, f32 speed, i32 duration, bool automatic) {
  if (active_count > DsTornadoMaximumInstances)
    return nullptr;
  DsTornadoInstance *instance = new DsTornadoInstance;
  instance->Init(this, x, z, direction, severity, speed, duration, 0, 0, automatic);
  return instance;
}

// Restore the saved position, direction, severity and damage totals with a freshly drawn lifetime. Draw before
// allocating the instance and report its actual initialization result; restored instances are not automatic.
bool DsTornadoManager::Restore(const DsTornadoRecord *record) {
  i32 duration = random.Range(DsTornadoMinimumDuration, DsTornadoMaximumDuration);
  DsTornadoInstance *instance = new DsTornadoInstance;
  return instance->Init(this, record->x, record->z, &record->direction, record->severity, record->speed, duration,
      record->cost, record->destroyed, false);
}

// Replay pending tornado records in saved order when city loading finishes, ignoring individual Init results.
// Consume the pending range afterward while retaining its storage; unrelated messages are not handled.
bool DsTornadoManager::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_CityViewReady)
    return false;
  DsTornadoRecord *records = pending.data;
  u32 count = pending.size;
  for (u32 i = 0; i < count; ++i)
    DsTornadoManager::Restore(records + i);
  pending.Clear();
  return true;
}

// Expose the common or tornado-specific manager identity, retaining through the root agent operation. Delegate
// other queries to the shared agent so unsupported outputs are cleared and its two root IDs remain available.
bool DsTornadoManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsTornadoControl, DsTornadoControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsTornadoManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsTornadoManager::Release() {
  return DsAgent::Release();
}

// Attempt a manual random tornado and post the original UI event only when spawning is accepted. Preserve
// the spawn result independently of the UI event result and keep automatic spawning on its separate path.
bool DsTornadoManager::AttemptDisasterStart() {
  bool result = DsTornadoManager::StartRandom(false);
  if (result)
    box->Event(kAuEventStartDisaster, kAuEffectDisasterTornado, 0, 0, 0);
  return result;
}

// Drop a nonzero live-effect count and notify the UI when the last effect disappears. Publish zero before the callback.
void DsTornadoManager::ReleaseEffect() {
  if (effect_count) {
    --effect_count;
    if (!effect_count)
      box->Event(kAuEventEffectSlot52, DsTornadoUiId, 0, 0, 0);
  }
}

void DsTornadoManager::ReleaseInstance() {
  if (active_count)
    --active_count;
}

// Count a tornado entering the camera and announce the first visible instance when an effect is active.
void DsTornadoManager::EnterCamera() {
  ++camera_count;
  if (camera_count == 1 && effect_count)
    box->Event(kAuEventEffectSlot56, DsTornadoUiId, 0, 1, 0);
}

// Drop a nonzero camera count and announce the last visible instance leaving while an effect remains active.
void DsTornadoManager::LeaveCamera() {
  if (camera_count) {
    --camera_count;
    if (!camera_count && effect_count)
      box->Event(kAuEventEffectSlot56, DsTornadoUiId, 0, 0, 0);
  }
}

// Establish the three interface identities and the original initialized state. Direction, velocity, severity,
// speed and step count remain unwritten until Init; the six effect handles start at the invalid all-ones value.
DsTornadoInstance::DsTornadoInstance()
    : initialized(false), manager(nullptr), effect(nullptr), x(0), z(0), fixed_x(0), fixed_y(0), fixed_z(0),
      in_camera(false), phase(0), remaining(0), duration(0), references(0), cost(0), destroyed(0), flag_132(false),
      flag_133(false), notifications_registered(false) {
  for (u32 i = 0; i < 6; ++i)
    effects[i] = kApInvalidNotificationId;
}

// Expose the original primary and specialized interfaces, acquiring one reference on success. Unsupported IDs clear the
// output; the message-target subobject is used directly and is not advertised by this query.
bool DsTornadoInstance::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsInstance, DsInstance);
  HANDLE_QUERY_INTERFACE(IID_DsTornadoMotion, DsTornadoMotion);
  RETURN_QUERY_INTERFACE_FAILURE();
}

DsTornadoInstance::~DsTornadoInstance() {
  if (initialized)
    Shutdown();
}

// Drop a held reference when present, deleting the instance once the count reaches zero. The original also
// deletes a zero-count receiver without underflowing its counter.
u32 DsTornadoInstance::Release() {
  if (references)
    --references;
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

// Publish the notification flag before obtaining the server, then register both messages through that borrowed server.
// A receiver already registered performs no service lookup or additional registration.
void DsTornadoInstance::RegisterNotifications() {
  if (!notifications_registered) {
    notifications_registered = true;
    ScMessageServer *server = DsGetMessageServer();
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_TornadoPositionChanged);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kSpMsg_TornadoLeftMap);
  }
}

// Clear the subscription flag before obtaining one borrowed server and removing the two messages in registration order.
// An already unsubscribed receiver makes no service lookup.
void DsTornadoInstance::RemoveNotifications() {
  if (notifications_registered) {
    notifications_registered = false;
    ScMessageServer *server = DsGetMessageServer();
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_TornadoPositionChanged);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kSpMsg_TornadoLeftMap);
  }
}

// Remove the six live transient handles in order, preserving each handle until its synchronous message returns.
void DsTornadoInstance::ClearEffects() {
  for (u32 index = 0; index < 6; ++index) {
    if (effects[index] != kApInvalidNotificationId) {
      ScMessage message = {kApMsgDismissNotification, effects[index], 0, 0};
      DsGetMessageServer()->MessageSend(message);
      effects[index] = kApInvalidNotificationId;
    }
  }
}

// Mark the instance inactive before removing notifications and its six transient effects. Update the manager
// and UI counters in original order, releasing the owned motion effect before dropping the active count.
bool DsTornadoInstance::Shutdown() {
  if (initialized) {
    initialized = false;
    RemoveNotifications();
    ClearEffects();
    if (in_camera) {
      --manager->camera_count;
      if (!manager->camera_count)
        manager->box->Event(kAuEventEffectSlot56, DsTornadoUiId, 0, 0, 0);
    }
    if (effect) {
      manager->ReleaseEffect();
      static_cast<SpTornadoSimulator *>(effect)->Release();
      effect = nullptr;
    }
    manager->ReleaseInstance();
  }
  return true;
}

// Bind an inactive tornado to its manager, terrain position and motion, then register it with the original
// notifications and disaster controller. Initial phase advancement runs before normal updates are enabled.
bool DsTornadoInstance::Init(DsTornadoManager *owner, u32 start_x, u32 start_z, const ScVec3F *motion, i8 level,
    f32 input_speed, i32 lifetime, u32 initial_cost, u32 initial_destroyed, bool automatic) {
  if (!initialized) {
    fixed_x = i32(start_x << kCtMicroCoordinateShift);
    manager = owner;
    x = start_x;
    z = start_z;
    fixed_z = i32(start_z << kCtMicroCoordinateShift);
    u8 altitude;
    manager->terrain->GetVertexAltitude(x, z, &altitude);
    fixed_y = i32(u32(altitude) << kCtMicroCoordinateShift);
    direction.x = motion->x;
    direction.y = motion->y;
    direction.z = motion->z;
    severity = level;
    if (severity >= 3)
      severity = 2;
    if (severity < 0)
      severity = 0;
    f64 factor = f64(input_speed) * f64(0.001f);
    speed = input_speed;
    velocity.x = f32(factor * direction.x);
    velocity.y = f32(factor * direction.y);
    velocity.z = f32(factor * direction.z);
    if (automatic) {
      phase = 0;
      remaining = 0;
    } else {
      phase = 4;
      remaining = DsTornadoFormationTicks;
    }
    duration = lifetime;
    cost = initial_cost;
    steps = 0;
    destroyed = initial_destroyed;
    initialized = true;
    ++manager->active_count;
    RegisterNotifications();
    flag_133 = false;
    manager->layer->DisasterInstanceChange(this, 0);
    Advance(0, false);
    flag_133 = true;
  }
  return true;
}

u32 DsTornadoInstance::AddRef() {
  return ++references;
}

// Report the initialized tornado's six-cell surrounding rectangle, clipped against its manager's city bounds.
// An uninitialized instance writes a zero rectangle and returns false without reaching the manager.
bool DsTornadoInstance::GetBounds(GeCellRect *output) {
  if (!initialized) {
    output->bottom = 0;
    output->top = 0;
    output->right = 0;
    output->left = 0;
    return false;
  }
  output->left = x > 6 ? x - 6 : 0;
  output->right = x + 6;
  if (output->right > manager->maximum_x)
    output->right = manager->maximum_x;
  output->top = z > 6 ? z - 6 : 0;
  output->bottom = z + 6;
  if (output->bottom > manager->maximum_z)
    output->bottom = manager->maximum_z;
  return true;
}

// Copy the stored cell coordinates into the two genuine outputs. Initialization does not gate this query.
bool DsTornadoInstance::GetLocation(u32 *output_x, u32 *output_z) {
  *output_x = x;
  *output_z = z;
  return true;
}

// Tornado instances have no associated building occupant. The returned pointer is always null.
ScOccupant *DsTornadoInstance::GetOccupant() {
  return nullptr;
}

// Return the owning manager's interface without retaining it. Native C++ performs the original base adjustment.
DsManager *DsTornadoInstance::GetManager() {
  return manager;
}

u32 DsTornadoInstance::TypeId() {
  return kDsTornadoTypeId;
}

u32 DsTornadoInstance::GetDestroyedCount() {
  return destroyed;
}

u32 DsTornadoInstance::GetCost() {
  return cost;
}

// Copy the signed fixed-point position through the three original outputs in X/Y/Z order. Aliased outputs
// retain the last coordinate written; initialization does not gate this motion query.
bool DsTornadoInstance::GetPosition(i32 *output_x, i32 *output_y, i32 *output_z) const {
  *output_x = fixed_x;
  *output_y = fixed_y;
  *output_z = fixed_z;
  return true;
}

u8 DsTornadoInstance::GetSeverity() {
  return u8(severity);
}

// Copy the three stored velocity components through the original output, preserving component order and raw values.
ScVec3F *DsTornadoInstance::GetVelocity(ScVec3F *output) const {
  output->x = velocity.x;
  output->y = velocity.y;
  output->z = velocity.z;
  return output;
}

// Copy the three stored direction components into the genuine output in order and retain the original pointer value.
ScVec3F *DsTornadoInstance::GetDirection(ScVec3F *output) const {
  output->x = direction.x;
  output->y = direction.y;
  output->z = direction.z;
  return output;
}

f32 DsTornadoInstance::GetSpeed() {
  return speed;
}

// Hold the instance during its effect notifications and update fixed coordinates before map checks. Crossing a cell
// brackets altitude/effect application with controller changes, then updates camera counters before releasing the hold.
bool DsTornadoInstance::DoMessage(ScMessage *message) {
  AddRef();
  if (initialized && message->data1 == reinterpret_cast<uptr>(effect)) {
    if (message->type == kSpMsg_TornadoLeftMap)
      flag_132 = true;
    else if (message->type == kSpMsg_TornadoPositionChanged) {
      i32 next_x = i32(u32(message->data2)), next_z = i32(u32(message->data3));
      if (next_x != fixed_x || next_z != fixed_z) {
        fixed_x = next_x;
        fixed_z = next_z;
        CheckWaterEffects();
        u32 cell_x = u32(fixed_x >> kCtMicroCoordinateShift), cell_z = u32(fixed_z >> kCtMicroCoordinateShift);
        if (cell_x != x || cell_z != z) {
          steps = i32(u32(steps) + 1);
          manager->layer->DisasterInstanceChange(this, 2);
          x = cell_x;
          z = cell_z;
          u8 altitude;
          manager->terrain->GetVertexAltitude(x, cell_z, &altitude);
          fixed_y = i32(u32(altitude) << kCtMicroCoordinateShift);
          ApplyCellEffects();
          manager->layer->DisasterInstanceChange(this, 3);
          bool visible = DsController::IsInVisibleRect(cell_x, cell_z) != 0;
          if (visible != in_camera) {
            if (visible)
              manager->EnterCamera();
            else
              manager->LeaveCamera();
            in_camera = visible;
          }
        }
      }
    }
  }
  Release();
  return true;
}

// Count matching tornadoes through the combined list, restart it and serialize fields through original scalar writers.
// Always sample direction after coordinate writes, advance once after refusal and close the iterator first.
bool DsTornadoManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsTornadoRecordGroup, 0}, 1, 0);
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  DsListIterator *iterator = layer->CreateDisasterListIterator();
  if (!iterator)
    return false;
  u32 count = 0;
  while (DsInstance *instance = iterator->Next())
    if (instance->TypeId() == kDsTornadoTypeId)
      ++count;
  bool result = stream->WriteU32(count) != 0;
  iterator->Rewind();
  while (DsInstance *instance = iterator->Next()) {
    if (!result)
      break;
    if (instance->TypeId() == kDsTornadoTypeId) {
      u32 x, z;
      instance->GetLocation(&x, &z);
      result = stream->WriteU32(x) != 0;
      if (result)
        result = stream->WriteU32(z) != 0;
      DsTornadoMotion *motion = static_cast<DsTornadoInstance *>(instance);
      ScVec3F direction;
      motion->GetDirection(&direction);
      result = result && stream->WriteF32(direction.x) != 0 && stream->WriteF32(direction.y) != 0 &&
          stream->WriteF32(direction.z) != 0 && stream->WriteI8(i8(motion->GetSeverity())) != 0 &&
          stream->WriteF32(motion->GetSpeed()) != 0 && stream->WriteU32(instance->GetCost()) != 0 &&
          stream->WriteU32(instance->GetDestroyedCount()) != 0;
    }
  }
  layer->DeleteDisasterListIterator(iterator);
  return result;
}

// Replace the pending tornado records with complete entries read in original field order, stopping at the first
// refused scalar read. Keep earlier complete entries and close the archive on every normal result.
bool DsTornadoManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  pending.Clear();
  DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsTornadoRecordGroup, 0});
  bool result = archive.valid;
  RsStream *stream = archive.Stream();
  if (!result || !stream)
    return false;
  i32 count;
  result = stream->ReadI32(&count) != 0;
  // Testing the read result first avoids the original failed-read path's unused, possibly unwritten count read.
  for (i32 index = 0; result && index < count; ++index) {
    DsTornadoRecord entry;
    result = stream->ReadU32(&entry.x) && stream->ReadU32(&entry.z) && stream->ReadF32(&entry.direction.x) &&
        stream->ReadF32(&entry.direction.y) && stream->ReadF32(&entry.direction.z) && stream->ReadI8(&entry.severity) &&
        stream->ReadF32(&entry.speed) && stream->ReadU32(&entry.cost) && stream->ReadU32(&entry.destroyed);
    if (result)
      *pending.Append() = entry;
  }
  return result;
}

bool DsTornadoInstance::Tick(DsTickType tick) {
  return Advance(tick, true);
}

// Select the phase resource with its original random draw, expand its text and send the mutable synchronous payload.
// Store the returned effect after releasing the message text, then release the current loaded resource identity.
void DsTornadoInstance::PostPhase(u32 state) {
  i32 slot = -1;
  u32 resource;
  if (state == 1) {
    resource = manager->random.Next(4) + 590;
    slot = 0;
  } else if (state == 2) {
    resource = manager->random.Next(4) + 594;
    slot = 1;
  } else if (state == 3) {
    resource = manager->random.Next(4) + 598;
    slot = 2;
  } else if (state == 4) {
    resource = manager->random.Next(3) + 602;
    slot = 3;
  } else if (state == 6)
    resource = manager->random.Next(3) + 608;
  else
    return;
  auto *text = new ScString;
  DsTextResource content(resource, kRsNewsTextGroup);
  content.CopyTo(text);
  DsNoticePayload notice = {0, kRsNewsTextGroup, resource, text, {0, 0, 0, 0}};
  text->AddRef();
  ScMessage message = {kApMsgCreateNotification, reinterpret_cast<uptr>(&notice), 0, 0};
  DsGetMessageServer()->MessageSend(message);
  text->Release();
  if (slot >= 0)
    effects[slot] = notice.effect;
}

// Sample the severity's inclusive damage chance and retain the occupant at the current cell. Emit geometry particles
// and fire effects independently, then destroy with tornado cause8 and accumulate only successful demolition costs.
void DsTornadoInstance::ApplyCellEffects() {
  ScOccupant *occupant;
  if (manager->random.Next(100) <= DsTornadoDamageChance[severity] &&
      manager->occupants->GetOccupantAtCell(x, z, &occupant)) {
    u16 altitude = occupant->GetAltAnimCells();
    if (occupant->GetGeomFlag() & kOcGeomPower) {
      void *created;
      if (DsGetCom()->GetClassObject(kSpParticleEffectClassId, IID_SpParticleEffect, &created)) {
        auto *particle = static_cast<SpParticleEffect *>(created);
        particle->Init();
        particle->DoParticleGeneration(fixed_x, fixed_z, altitude, 1, 10);
        particle->Release();
      }
    }
    void *created;
    if (DsGetCom()->GetClassObject(kSpExplosionViewClassId, IID_SpExplosionView, &created)) {
      auto *fire = static_cast<SpExplosionView *>(created);
      fire->Init(fixed_x, fixed_z, fixed_y, 3, 2, 0);
      manager->box->Event(kAuEventPlayBuildingSound, fixed_x / 256, fixed_z / 256, 0, 0);
      fire->Release();
    }
    u32 demolition_cost;
    if (manager->layer->DestroyOccupant(occupant, kDsDamageRubble, &demolition_cost)) {
      cost += demolition_cost;
      ++destroyed;
    }
    occupant->Release();
  }
}

void DsTornadoManager::NotifyRemoval(DsTornadoInstance *instance) {
  layer->DisasterInstanceChange(instance, 4);
}

// Remove motion notifications before publishing terminal phase6, then notify the current manager of removal.
// Synchronous callbacks may change the manager; the final call reads it after the phase notice returns.
void DsTornadoInstance::Finish() {
  RemoveNotifications();
  phase = 6;
  PostPhase(6);
  manager->NotifyRemoval(this);
}

void DsTornadoManager::StopInstance(DsInstance *instance) {
  NotifyRemoval(static_cast<DsTornadoInstance *>(instance));
}

i8 DsTornadoInstance::PanicEffect(u32 distance) {
  static const i8 levels[] = {100, 80, 60, 30, 10, 0};
  return phase == 5 && distance <= 5 ? levels[distance] : 0;
}

u32 DsTornadoInstance::PanicRadius() {
  return phase == 5 ? 5 : 0;
}

bool DsTornadoInstance::IsActive() {
  return initialized && effect != nullptr;
}

// Parse the first comma-separated field as the instance limit, then retire that prefix before publishing the UI ID.
// A missing comma leaves an empty first field and parses the unchanged value as the UI ID; the key is ignored.
void DsTornadoReadEntry(const ScString &key, const ScString &value, void *) {
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
    DsTornadoMaximumInstances = DsParseNumber(first.ToChar());
  }
  DsTornadoUiId = i32(DsParseNumber(remaining.ToChar()));
}

// Convert each speed through the original truncated low unsigned word before scaling, then enforce duration order.
void DsUpdateTornadoScales() {
  DsTornadoMinimumSpeed = f32(f64(u32(i64(DsTornadoMinimumSpeed))) * 256.0 * 1.6666666851961054e-5);
  DsTornadoMaximumSpeed = f32(f64(u32(i64(DsTornadoMaximumSpeed))) * 256.0 * 1.6666666851961054e-5);
  u32 minimum = u32(DsTornadoMinimumDuration);
  if (!minimum) {
    minimum = 1;
    DsTornadoMinimumDuration = 1;
  }
  if (u32(DsTornadoMaximumDuration) <= minimum)
    DsTornadoMaximumDuration = i32(minimum + 1);
}

// Load tornado descriptions before the ordered interval, signed-byte speed, damage and phase properties.
// Missing keys retain previous values; an absent service returns false without recomputing speed or duration scales.
bool DsTornadoManager::StaticInit() {
  DsIni reader;
  ScString path, unused;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  {
    ScString section("TornadoDisaster");
    reader.EnumEntries(section, DsTornadoReadEntry, nullptr);
  }
  ScVariableManager *provider = DsGetPropertyProvider();
  if (!provider)
    return false;
  ScValue value;
  if (provider->GetVariable(kDsTornadoMinimumIntervalPropertyId, &value))
    DsTornadoMinimumInterval = i32(value.value.word);
  if (provider->GetVariable(kDsTornadoMaximumIntervalPropertyId, &value))
    DsTornadoMaximumInterval = i32(value.value.word);
  if (provider->GetVariable(kDsTornadoMinimumSpeedPropertyId, &value))
    DsTornadoMinimumSpeed = f32(i8(value.value.word));
  if (provider->GetVariable(kDsTornadoMaximumSpeedPropertyId, &value))
    DsTornadoMaximumSpeed = f32(i8(value.value.word));
  if (provider->GetVariable(kDsTornadoDamageChance0PropertyId, &value))
    DsTornadoDamageChance[0] = value.value.word;
  if (provider->GetVariable(kDsTornadoDamageChance1PropertyId, &value))
    DsTornadoDamageChance[1] = value.value.word;
  if (provider->GetVariable(kDsTornadoDamageChance2PropertyId, &value))
    DsTornadoDamageChance[2] = value.value.word;
  if (provider->GetVariable(kDsTornadoMinimumDurationPropertyId, &value))
    DsTornadoMinimumDuration = i32(value.value.word);
  if (provider->GetVariable(kDsTornadoMaximumDurationPropertyId, &value))
    DsTornadoMaximumDuration = i32(value.value.word);
  if (provider->GetVariable(kDsTornadoFirstDelayPropertyId, &value))
    DsTornadoFirstDelay = value.value.word;
  if (provider->GetVariable(kDsTornadoSecondDelayPropertyId, &value))
    DsTornadoSecondDelay = value.value.word;
  if (provider->GetVariable(kDsTornadoThirdDelayPropertyId, &value))
    DsTornadoThirdDelay = value.value.word;
  if (provider->GetVariable(kDsTornadoAttackDelayPropertyId, &value))
    DsTornadoAttackDelay = value.value.word;
  if (provider->GetVariable(kDsTornadoFormationTicksPropertyId, &value))
    DsTornadoFormationTicks = i32(value.value.word);
  DsUpdateTornadoScales();
  return true;
}

// Construct the actual tornado manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateTornadoManager() {
  return static_cast<DsManager *>(new DsTornadoManager);
}

void DsTornadoManager::RegisterEffect() {
  if (++effect_count == 1)
    box->Event(kAuEventPlayEffect, DsTornadoUiId, i32(reinterpret_cast<uptr>(this)), 0, 0);
}

// Advance the original premonition and formation thresholds, then create and register the real tornado simulator.
// Expiry requests fading before immediate retirement; the active phase otherwise waits for its completion flag.
bool DsTornadoInstance::Advance(i32, bool running) {
  if (!initialized || (running && !flag_133))
    return true;
  if (steps >= duration) {
    if (effect)
      static_cast<SpTornadoSimulator *>(effect)->DoFade(0, 1);
    Finish();
    return true;
  }
  u32 ticks = u32(remaining) + 1;
  remaining = i32(ticks);
  switch (phase) {
  case 0:
    if (ticks < DsTornadoFirstDelay)
      break;
    phase = 1;
    PostPhase(1);
    remaining = 0;
    break;
  case 1:
    if (ticks < DsTornadoSecondDelay)
      break;
    phase = 2;
    PostPhase(2);
    remaining = 0;
    break;
  case 2:
    if (ticks < DsTornadoThirdDelay)
      break;
    phase = 3;
    PostPhase(3);
    remaining = 0;
    break;
  case 3:
    if (ticks < DsTornadoAttackDelay)
      break;
    phase = 4;
    PostPhase(4);
    remaining = 0;
    break;
  case 4:
    if (ticks > u32(DsTornadoFormationTicks)) {
      phase = 5;
      ClearEffects();
      PostPhase(u32(phase));
      remaining = 0;
      DsGetCom()->GetClassObject(kSpTornadoSimulatorClassId, IID_SpTornadoSimulator, &effect);
      static_cast<SpTornadoSimulator *>(effect)->Init(fixed_x, fixed_z, fixed_y, direction, i8(u8(severity) + 1u));
      static_cast<SpTornadoSimulator *>(effect)->SetSpeed(speed);
      manager->layer->DisasterInstanceChange(this, 1);
      manager->RegisterEffect();
    }
    break;
  case 5:
    if (flag_132)
      Finish();
    break;
  }
  return true;
}

// Query water at the old cell before drawing, then create debris below the severity-specific percentage.
// The actual terrain callback can change the fixed position before movement resumes in the caller.
void DsTornadoInstance::CheckWaterEffects() {
  static const u8 chance[] = {40, 70, 100};
  if (!manager->terrain->IsWater(x, z) && manager->random.Next(100) < chance[severity])
    static_cast<SpTornadoSimulator *>(effect)->CreateTornadoDebris();
}

// Actual module-wide bit lock at3AB04 serializes UFO start attempts without recursive-lock semantics.
static ScSpinLock DsUfoStartLock;

u32 DsUfoMaximumInstances = 1, DsUfoMinimumMonths = 16, DsUfoMaximumMonths = 96, DsUfoAttackChance = 42;
u32 DsUfoExitExplosionChance = 1000, DsUfoFirstPremonitionTicks = 0, DsUfoSecondPremonitionTicks = 60;
u32 DsUfoThirdPremonitionTicks = 120, DsUfoAttackTicks = 180, DsUfoEndTicks = 240, DsUfoMaximumShips = 20;
u32 DsUfoMaximumCheatShips = 50, DsUfoMaximumAbductions = 3, DsUfoMaximumCropCircles = 3;
u32 DsUfoMaximumLandmarks = 3, DsUfoMaximumSpaceports = 3, DsUfoMaximumLowTech = 3, DsUfoMaximumHighTech = 3;
u32 DsUfoMaximumPower = 2, DsUfoMaximumDestruction = 32, DsUfoMinimumYear = 0, DsUfoAbductionYear = 1950;
u32 DsUfoCropCircleYear = 1975, DsUfoAttackYear = 2000, DsUfoDestructionChance = 10, DsUfoFireChance = 10;

DsUfoManager::DsUfoManager()
    : initialized(false), automatic(0), kind(1), layer(nullptr), demolition(nullptr), flag_52(false), city(nullptr),
      occupants(nullptr), random(kScRandomSeedFromClock), active_count(0), box(nullptr), flags_104(0) {
}

DsUfoManager::~DsUfoManager() {
  DsUfoManager::Shutdown();
}

// Publish the actual message, common-manager or private-record identity and retain the shared agent once on success.
// Other queries retain the original agent fallback and its output-clearing behavior.
bool DsUfoManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsUfoControl, DsUfoControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsUfoManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsUfoManager::Release() {
  return DsAgent::Release();
}

bool DsUfoManager::StaticShutdown() {
  return true;
}

// Copy at most the caller's live record capacity, rereading the mutable count after each complete record copy.
// A null record pointer reports the full current history size through both the genuine output and return value.
u32 DsUfoManager::GetRecords(DsUfoRecord *records, u32 *count) const {
  if (!records) {
    *count = history.size;
    return history.size;
  }
  u32 copied = *count < history.size ? *count : history.size;
  *count = copied;
  for (u32 index = 0; index < *count; ++index)
    memcpy(records + index, history.data + index, sizeof(DsUfoRecord));
  return *count;
}

// Read the current simulation-clock date into the original zero-date receiver and append one real cleared history row.
// Only its leading serial word is populated; the remaining eleven words begin zero.
void DsUfoManager::SeedHistory() {
  CtDate current(0u, 0, 0);
  city->GetSimulationClock()->GetCurrentClockDate(&current);
  DsUfoRecord record = {};
  record.serial = current.serial;
  *history.Append() = record;
}

// Gate automatic attacks by unsigned capacity, elapsed serial days and the original strict monthly chance draw.
// Seed empty history after sampling the current date, clamp a future last record and clear the temporary flag after dispatch.
bool DsUfoManager::Tick() {
  if (active_count < DsUfoMaximumInstances) {
    CtDate current(0u, 0, 0);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    u32 today = current.serial;
    if (!history.size)
      SeedHistory();
    u32 previous = history.data[history.size - 1].serial;
    if (today < previous)
      previous = today;
    if (today - previous > 30u * DsUfoMinimumMonths && random.Next(10000) < DsUfoAttackChance) {
      automatic = 1;
      AttemptDisasterStart();
      automatic = 0;
    }
  }
  return true;
}

// Load the original INI/package sources and 26 ordered UFO scalar keys, retaining defaults when individual keys are absent.
// Destroy each temporary section/key before parsing and publishing the returned numeric word, preserving callback ordering.
bool DsUfoManager::StaticInit() {
  DsIni reader;
  ScString path, value;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  const char *keys[] = {"MaxUFOInstances", "MinMonthsBetweenUFOAttacks", "MaxMonthsBetweenUFOAttacks",
      "PercentageChanceOfUFOAttackEachMonthTimes100", "PercentageChanceOfUFOExplosionOnExitTimes100",
      "TimeBeforeFirstPremonitionMessage", "TimeBeforeSecondPremonitionMessage", "TimeBeforeThirdPremonitionMessage",
      "TimeBeforeUFOAttack", "TimeBeforeUFOAttackEnd", "MaxUFOsPerInstance", "MaxUFOsPerInstanceCheatSwarm",
      "MaxAbductionsMadePerAttack", "MaxCropCirclesMadePerAttack", "MaxLandmarkBuildingsDestroyedPerAttack",
      "MaxSpaceportBuildingsDestroyedPerAttack", "MaxLowTechBuildingsDestroyedPerAttack",
      "MaxHighTechBuildingsDestroyedPerAttack", "MaxPowerBuildingsDestroyedPerAttack", "MaxTotalBuildingsToDestroy",
      "YearOfFirstPossibleVisitationAppearance", "YearOfFirstPossibleAbductionAppearance",
      "YearOfFirstPossibleCropCircleAppearance", "YearOfFirstPossibleAttackAppearance",
      "PercentageOfBuildingsDestroyedByUFOBombs", "PercentageOfBuildingsCatchFireByUFOBombs"};
  u32 *destinations[] = {&DsUfoMaximumInstances, &DsUfoMinimumMonths, &DsUfoMaximumMonths, &DsUfoAttackChance,
      &DsUfoExitExplosionChance, &DsUfoFirstPremonitionTicks, &DsUfoSecondPremonitionTicks, &DsUfoThirdPremonitionTicks,
      &DsUfoAttackTicks, &DsUfoEndTicks, &DsUfoMaximumShips, &DsUfoMaximumCheatShips, &DsUfoMaximumAbductions,
      &DsUfoMaximumCropCircles, &DsUfoMaximumLandmarks, &DsUfoMaximumSpaceports, &DsUfoMaximumLowTech,
      &DsUfoMaximumHighTech, &DsUfoMaximumPower, &DsUfoMaximumDestruction, &DsUfoMinimumYear, &DsUfoAbductionYear,
      &DsUfoCropCircleYear, &DsUfoAttackYear, &DsUfoDestructionChance, &DsUfoFireChance};
  for (u32 index = 0; index < 26; ++index) {
    u8 found;
    {
      ScString key(keys[index]), section("UFODisasterTunables");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found)
      *destinations[index] = DsParseNumber(value.ToChar());
  }
  return true;
}

u32 DsUfoManager::SetKind(u32 value) {
  kind = value;
  return value;
}

// Replace history from its own archive, then append complete pending-instance rows from the separate archive.
// Close both streams before seeding empty history, preserving earlier complete rows and the original unconditional success.
bool DsUfoManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsUfoHistoryRecordGroup, 0});
    history.Clear();
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      u8 result = stream->ReadU32(&count);
      if (result) {
        for (u32 index = 0; index < count && result; ++index) {
          DsUfoRecord record;
          result = stream->ReadBlock(&record, 48);
          if (result)
            *history.Append() = record;
        }
      }
    }
  }
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsUfoInstancesRecordGroup, 0});
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      u8 result = stream->ReadU32(&count);
      if (result) {
        for (u32 index = 0; index < count && result; ++index) {
          DsUfoRecord record;
          result = stream->ReadBlock(&record, 48);
          if (result)
            *pending.Append() = record;
        }
      }
    }
  }
  if (!history.size)
    SeedHistory();
  return true;
}

// Retain the actual city services, register the UFO Swarm cheat and subscribe to load completion before publishing readiness.
// An already initialized entry only shuts down and returns, deliberately skipping immediate rebinding.
bool DsUfoManager::Init(DsLayer *retained_layer, CtCity *owner) {
  if (initialized) {
    Shutdown();
  } else {
    city = ScAddRefNotNull(owner);
    terrain = ScAddRefNotNull(city->DirtBag());
    demolition = ScAddRefNotNull(city->DemolitionLayer());
    simulation = ScAddRefNotNull(city->StrtSimLayer());
    agent_types = ScAddRefNotNull(DsGetAgentTree());
    box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
    occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
    layer = ScAddRefNotNull(retained_layer);
    cheats = DsGetApplication()->GetCheatCodeManager();
    {
      ScString name("UFO Swarm");
      cheats->RegisterCheatCode(kDsUfoSwarmCheatId, name);
    }
    cheats->AddNotification(static_cast<ScMessageTarget *>(this), 0);
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    active_count = 0;
    flags_104 = 0;
    flag_52 = false;
    initialized = true;
  }
  return true;
}

// Remove load notification before releasing the original service sequence, clearing each field after its release callback.
// Retire the cheat subscription and both record ranges last, retaining allocated capacity and publishing inactive state last.
bool DsUfoManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&demolition);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&simulation);
    ScRelease(&city);
    if (cheats) {
      cheats->RemoveNotification(static_cast<ScMessageTarget *>(this), 0);
      cheats->Release();
      cheats = nullptr;
    }
    history.Clear();
    pending.Clear();
    initialized = false;
  }
  return true;
}

// Write capped history first, then serialize every matching live instance when the active-count gate is nonzero.
// Ignore individual block-write failures as installed, close each archive independently and preserve unconditional success.
bool DsUfoManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsUfoHistoryRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = history.size < 50000 ? history.size : 50000;
      if (stream->WriteU32(count) && count) {
        for (u32 index = 0; index < count; ++index) {
          DsUfoRecord record = history.data[index];
          stream->WriteBlock(&record, 48);
        }
      }
    }
  }
  if (active_count) {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsUfoInstancesRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      if (stream->WriteU32(active_count)) {
        DsListIterator *iterator = layer->CreateDisasterListIterator();
        while (DsInstance *instance = iterator->Next()) {
          if (instance->TypeId() == kDsUfoTypeId) {
            DsUfoRecord record;
            static_cast<DsUfoInstance *>(instance)->CopyRecord(&record);
            stream->WriteBlock(&record, 48);
          }
        }
        layer->DeleteDisasterListIterator(iterator);
      }
    }
  }
  return true;
}

// Toggle the registered UFO Swarm cheat or restore the captured number of pending instances on load completion.
// Replay reads the current pending base for each row, increments the count after initialization and clears the live range last.
bool DsUfoManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CheatCommand) {
    if (u32(message->data1) == kDsUfoSwarmCheatId) {
      bool enabled = cheats->GetCheatHasBeenSet(kDsUfoSwarmCheatId);
      cheats->SetCheatHasBeenSet(kDsUfoSwarmCheatId, !enabled);
    }
  } else if (message->type == kWnMsg_CityViewReady) {
    u32 count = pending.size;
    for (u32 index = 0; index < count; ++index) {
      auto *instance = new DsUfoInstance;
      instance->Init(this, pending.data + index);
      ++active_count;
    }
    pending.Clear();
  }
  return true;
}

// Hold the original module-wide spin lock across capacity/year gates, random kind selection and actual instance creation.
// Preserve descending year tests and draw thresholds, then dispatch the disaster-start audio event before publishing the incremented active count.
bool DsUfoManager::AttemptDisasterStart() {
  ScLockSpin(&DsUfoStartLock);
  bool result = false;
  if (active_count < DsUfoMaximumInstances) {
    CtDate current(0u, 0, 0);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    if (current.Year() >= DsUfoMinimumYear) {
      u32 selected;
      if (current.Year() < DsUfoAttackYear || random.Next(100) < 10) {
        if (current.Year() < DsUfoCropCircleYear || random.Next(100) < 30)
          if (current.Year() < DsUfoAbductionYear || random.Next(100) < 5)
            selected = kDsUfoFlyby;
          else
            selected = kDsUfoAbduction;
        else
          selected = kDsUfoCropCircles;
      } else {
        selected = kDsUfoAttack;
      }
      SetKind(selected);
      auto *instance = new DsUfoInstance;
      instance->Init(this, kind, 0, automatic);
      box->Event(kAuEventStartDisaster, kAuEffectDisasterUfo, 0, 0, 0);
      ++active_count;
      result = true;
    }
  }
  ScUnlockSpin(&DsUfoStartLock);
  return result;
}

// Replace the raw automatic byte only across the common start attempt and restore the prior value after it returns.
u8 DsUfoManager::Start(u8 value) {
  u8 previous = automatic;
  automatic = value;
  u8 result = u8(AttemptDisasterStart());
  automatic = previous;
  return result;
}

void DsUfoManager::StopInstance(DsInstance *instance) {
  static_cast<DsUfoInstance *>(instance)->Shutdown();
}

// Retire one live instance, treating an already-zero counter as one before the original post-decrement test.
void DsUfoManager::InstanceEnded() {
  if (!active_count)
    active_count = 1;
  if (active_count-- == 1)
    flag_52 = false;
}

// Construct the actual manager with zero initial agent references and return its common-manager Unknown identity.
// Ordinary native allocation replaces only the original allocation boundary, not the reconstructed class or its factory contract.
ScUnknown *DsCreateClass22FE8D3E() {
  return static_cast<DsManager *>(new DsUfoManager);
}

void DsUfoManager::RecordInstance(DsUfoInstance *instance) {
  *history.Append() = instance->state;
}

// Preserve history before shutting down the instance and publishing its completion to the current layer.
// The original completion helper does not release the instance.
void DsUfoManager::FinishInstance(DsUfoInstance *instance) {
  RecordInstance(instance);
  instance->Shutdown();
  layer->DisasterInstanceChange(instance, 4);
}

// Actual module-wide bit lock3AB68 serializes this family across all manager instances.
static ScSpinLock DsWaterStartLock;

u32 DsWaterMinimumStrength = 25, DsWaterMaximumStrength = 85;
// The remaining unused tuning word has no proven consumer; keep its neutral address-derived name.
static u32 DsWaterWordA228 = 96;
i32 DsWaterDamageRadius = 7;
u32 DsWaterDebrisLifetime = 30000;
u32 DsWaterSpeedTimes1000 = 60, DsWaterFrameMicroseconds = kDsWaterDefaultFrameMicroseconds;
u32 DsWaterFastestSpeedTimes100000000 = 1000, DsWaterOuterGravityDistance = 2400;

// History and pending replay own distinct32-byte record vectors; original padding and word76 remain absent.
DsWaterManager::DsWaterManager()
    : initialized(false), automatic_start(0), layer(nullptr), demolition(nullptr), terrain(nullptr), zones(nullptr),
      simulation(nullptr), agent_types(nullptr), city(nullptr), occupants(nullptr), underground_one(nullptr),
      underground_two(nullptr), box(nullptr), active(false), flag_73(0), random(kScRandomSeedFromClock),
      active_count(0), flags_108(0) {
}

DsWaterManager::~DsWaterManager() {
  DsWaterManager::Shutdown();
}

// Publish the actual common or private manager view and retain through the root agent identity.
// All other queries use the original agent fallback, including its unsupported-output behavior.
bool DsWaterManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsWaterControl, DsWaterControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsWaterManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsWaterManager::Release() {
  return DsAgent::Release();
}

// Remove the load notification and release retained services in the original non-field order.
// Clear pending before history, publishing the uninitialized state only after every release callback completes.
bool DsWaterManager::Shutdown() {
  if (initialized) {
    DsGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
    ScRelease(&layer);
    ScRelease(&demolition);
    ScRelease(&zones);
    ScRelease(&agent_types);
    ScRelease(&occupants);
    ScRelease(&underground_one);
    ScRelease(&underground_two);
    ScRelease(&box);
    ScRelease(&terrain);
    ScRelease(&simulation);
    ScRelease(&city);
    pending.Clear();
    history.Clear();
    initialized = false;
  }
  return true;
}

// Copy at most the caller's capacity from the live history, rereading its output count between record copies.
// A null record output reports the full available count through both original result channels.
u32 DsWaterManager::GetRecords(DsWaterRecord *records, u32 *count) const {
  if (records) {
    if (*count > history.size)
      *count = history.size;
    for (u32 index = 0; index < *count; ++index)
      memcpy(&records[index], &history.data[index], sizeof(DsWaterRecord));
  } else
    *count = history.size;
  return *count;
}

// Report whether history is nonempty and optionally copy its first complete32-byte record.
u8 DsWaterManager::GetFirstRecord(DsWaterRecord *record) const {
  if (!history.size)
    return 0;
  if (record)
    memcpy(record, history.data, sizeof(DsWaterRecord));
  return 1;
}

// Match either preferred ancestor ID while traversing borrowed city-agent types in their original order.
// Return immediately on the first seaport or marina ancestor, without retaining the borrowed type nodes.
u8 DsWaterPrefersOccupant(ScOccupant *occupant) {
  for (CtAgentType *type = occupant->CityAgentType(); type; type = type->Parent()) {
    u32 id = type->Id();
    if (id == kCtAgentSeaport || id == kCtAgentMarina)
      return 1;
  }
  return 0;
}

// Count preferred retained occupants in the exclusive rectangle, with Z outside X and live bounds reads.
// The original optional surface manager gate skips the entire scan when that service is absent.
u32 DsWaterManager::CountPreferred(const GeCellRect *bounds) {
  u32 count = 0;
  if (occupants)
    for (u32 z = bounds->top; z < bounds->bottom; ++z)
      for (u32 x = bounds->left; x < bounds->right; ++x) {
        ScOccupant *occupant;
        if (occupants->GetOccupantAtCell(x, z, &occupant)) {
          if (DsWaterPrefersOccupant(occupant))
            ++count;
          occupant->Release();
        }
      }
  return count;
}

// Draw independent fixed X/Z positions inside the original five-cell inset and sample their vertex altitude.
// Narrow insets use the original right/bottom-minus-six repair; the third word is an unused original operand.
u8 DsWaterManager::SampleRectangle(const GeCellRect *bounds, ScVec3I *position, i32) {
  u32 right = bounds->right, bottom = bounds->bottom;
  u32 left = bounds->left + 5, top = bounds->top + 5;
  if (i32(left) >= i32(right - 5))
    left = right - 6;
  if (i32(top) >= i32(bottom - 5))
    top = bottom - 6;
  position->x = random.Range(i32(left << kCtMicroCoordinateShift), i32((right - 5) << kCtMicroCoordinateShift));
  position->z = random.Range(i32(top << kCtMicroCoordinateShift), i32((bottom - 5) << kCtMicroCoordinateShift));
  u8 altitude;
  terrain->GetVertexAltitude(u32(position->x / 256), u32(position->z / 256), &altitude);
  position->y = (u32(altitude) + 6) << kCtMicroCoordinateShift;
  return 1;
}

u8 DsWaterManager::IsValidPosition(const ScVec3I *position) {
  if (terrain && city) {
    u32 x = u32(position->x / 256), z = u32(position->z / 256);
    if (x < city->CellCountX() && z < city->CellCountZ())
      return terrain->IsRealWater(x, z);
  }
  return 0;
}

// Find real-water rectangles of at least eight cells per side, preferring those containing the recognized occupants.
// Retry the original random rectangle/position selection at most sixteen times; leave failed sample outputs visible.
u8 DsWaterManager::SelectStart(ScVec3I *position) {
  ScArray<GeCellRect> rectangles;
  if (!DsFindWaterRectangles(terrain, &rectangles, 8, 8, nullptr) || !rectangles.size)
    return 0;
  u32 count = rectangles.size;
  ScArray<GeCellRect> preferred;
  for (u32 index = 0; index < count; ++index)
    if (CountPreferred(&rectangles.data[index]))
      *preferred.Append() = rectangles.data[index];
  u32 preferred_count = preferred.size;
  for (u32 attempt = 1; attempt <= 16; ++attempt) {
    if (preferred_count) {
      u32 index = random.Next(preferred_count);
      SampleRectangle(&preferred.data[index], position, 1);
    } else {
      u32 index = random.Next(count);
      SampleRectangle(&rectangles.data[index], position, 0);
    }
    if (IsValidPosition(position))
      return 1;
  }
  return 0;
}

// The original folded city initializer only shuts down an already-bound manager; fresh binding retains each service.
// Reset activity and mark initialized before registering the load notification so its callback sees the ready manager.
bool DsWaterManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
  if (initialized) {
    Shutdown();
  } else {
    city = ScAddRefNotNull(retained_city);
    terrain = ScAddRefNotNull(city->DirtBag());
    // The SDK getter remains pointer-sized; only the established Unknown retain/release contract is used here.
    demolition = ScAddRefNotNull(static_cast<ScUnknown *>(city->DemolitionLayer()));
    zones = ScAddRefNotNull(city->ZoneLayer());
    simulation = ScAddRefNotNull(city->StrtSimLayer());
    agent_types = ScAddRefNotNull(DsGetAgentTree());
    box = ScAddRefNotNull(DsGetApplication()->GetBoxX());
    occupants = ScAddRefNotNull(city->SurfaceOccupantManager());
    underground_one = ScAddRefNotNull(city->UndergroundOccupantManagerLevelOne());
    underground_two = ScAddRefNotNull(city->UndergroundOccupantManagerLevelTwo());
    layer = ScAddRefNotNull(retained_layer);
    active_count = 0;
    flags_108 = 0;
    active = false;
    initialized = true;
    DsGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CityViewReady);
  }
  return true;
}

bool DsWaterManager::StaticShutdown() {
  return true;
}

bool DsWaterManager::AttemptDisasterStart() {
  f32 strength = f32(random.InverseSqrtRange(0.0, 1.0));
  return Start(strength, automatic_start);
}

u32 DsWaterMaximumActive = 1, DsWaterMinimumMonths = 16, DsWaterChance = 83;

void DsWaterManager::SeedHistory() {
  CtDate date(0u, 0, 0);
  city->GetSimulationClock()->GetCurrentClockDate(&date);
  DsWaterRecord record = {date.serial, 0, 0, 0, 0, 0, 0, 0};
  *history.Append() = record;
}

// Gate automatic attempts by unsigned capacity, the clamped last history date and the strict thirty-day month delay.
// Capture today's date before seeding empty history, and publish the temporary automatic byte around the virtual trigger.
bool DsWaterManager::Tick() {
  if (active_count < DsWaterMaximumActive) {
    CtDate current(0u, 0, 0);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    u32 today = current.serial;
    if (!history.size)
      SeedHistory();
    u32 previous = history.data[history.size - 1].serial;
    if (today < previous)
      previous = today;
    if (today - previous > 30u * DsWaterMinimumMonths && random.Next(10000) < DsWaterChance) {
      automatic_start = 1;
      AttemptDisasterStart();
      automatic_start = 0;
    }
  }
  return true;
}

// Replace history from its capped archive, then append pending records from the independent live-state archive.
// Close both archives before seeding missing history; partial read failures stop their own loop and do not change success.
bool DsWaterManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsWaterHistoryRecordGroup, 0});
    history.Clear();
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result) {
        if (count > 50000)
          count = 50000;
        for (u32 index = 0; index < count && result; ++index) {
          DsWaterRecord record;
          result = stream->ReadBlock(&record, 32) != 0;
          if (result)
            *history.Append() = record;
        }
      }
    }
  }
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsWaterInstancesRecordGroup, 0});
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result)
        for (u32 index = 0; index < count && result; ++index) {
          DsWaterRecord record;
          result = stream->ReadBlock(&record, 32) != 0;
          if (result)
            *pending.Append() = record;
        }
    }
  }
  if (!history.size)
    SeedHistory();
  return true;
}

// Save capped history rows before enumerating every matching live instance through its actual private record view.
// The signed live-count clamp mutates the manager after opening its archive; individual block-write failures are ignored.
bool DsWaterManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsWaterHistoryRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = history.size < 50000 ? history.size : 50000;
      if (stream->WriteU32(count) && count)
        for (u32 index = 0; index < count; ++index) {
          DsWaterRecord record = history.data[index];
          stream->WriteBlock(&record, 32);
        }
    }
  }
  if (active_count) {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsWaterInstancesRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      if (i32(active_count) > 50000)
        active_count = 50000;
      if (stream->WriteU32(active_count)) {
        DsListIterator *iterator = layer->CreateDisasterListIterator();
        while (DsInstance *instance = iterator->Next())
          if (instance->TypeId() == kDsWaterTypeId) {
            DsWaterRecord record;
            static_cast<DsWaterState *>(static_cast<DsWaterInstance *>(instance))->GetRecord(&record);
            stream->WriteBlock(&record, 32);
          }
        layer->DeleteDisasterListIterator(iterator);
      }
    }
  }
  return true;
}

// Restore the captured pending record count after city loading, rereading the vector base between initializer callbacks.
// Count each attempted restore after initialization and clear pending only after all original rows have been processed.
bool DsWaterManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CityViewReady) {
    u32 count = pending.size;
    for (u32 index = 0; index < count; ++index) {
      DsWaterInstance *instance = new DsWaterInstance;
      instance->Init(this, &pending.data[index]);
      ++active_count;
    }
    pending.Clear();
  }
  return true;
}

void DsWaterManager::StopInstance(DsInstance *instance) {
  static_cast<DsWaterInstance *>(instance)->Shutdown();
}

// Preserve the original zero-to-one fallback before decrementing and clear activity only for the final instance.
void DsWaterManager::InstanceEnded() {
  if (!active_count)
    active_count = 1;
  if (active_count-- == 1)
    active = false;
}

// Hold the original bit lock across capacity, water-site selection, strength mapping and real instance initialization.
// Only a successful initializer increments activity; failed creation keeps its original filter-owning instance lifetime.
u8 DsWaterManager::Start(f32 strength, u8 automatic) {
  ScLockSpin(&DsWaterStartLock);
  u8 result = 0;
  ScVec3I start;
  if (active_count < DsWaterMaximumActive && SelectStart(&start)) {
    u32 maximum = DsWaterMaximumStrength, minimum = DsWaterMinimumStrength;
    f32 mapped = f32((f64(maximum - minimum) * strength + f64(i32(minimum))) * f64(0.1f));
    auto *instance = new DsWaterInstance;
    result = u8(instance->Init(this, mapped, &start, automatic));
    if (result)
      ++active_count;
  }
  ScUnlockSpin(&DsWaterStartLock);
  return result;
}

void DsWaterManager::RecordInstance(DsWaterInstance *instance) {
  *history.Append() = instance->state;
}

// Record history before the actual teardown, then publish removal through the current layer without releasing the instance.
void DsWaterManager::FinishInstance(DsWaterInstance *instance) {
  RecordInstance(instance);
  instance->Shutdown();
  layer->DisasterInstanceChange(instance, 4);
}

// Load the water-family tuning words with the original repeated limit/month queries and one mutable variant.
// A missing service or key preserves existing tuning; this manager reports success even without the service.
bool DsWaterManager::StaticInit() {
  ScVariableManager *provider = DsGetPropertyProvider();
  if (provider) {
    ScValue value;
    if (provider->GetVariable(kDsWaterMaximumActivePropertyId, &value))
      DsWaterMaximumActive = value.value.word;
    if (provider->GetVariable(kDsWaterMinimumMonthsPropertyId, &value))
      DsWaterMinimumMonths = value.value.word;
    if (provider->GetVariable(kDsWaterMaximumActivePropertyId, &value))
      DsWaterMaximumActive = value.value.word;
    if (provider->GetVariable(kDsWaterMinimumMonthsPropertyId, &value))
      DsWaterMinimumMonths = value.value.word;
    if (provider->GetVariable(kDsEventMaximumMonthsPropertyId, &value))
      DsWaterWordA228 = value.value.word;
    if (provider->GetVariable(kDsWaterMinimumStrengthPropertyId, &value))
      DsWaterMinimumStrength = value.value.word;
    if (provider->GetVariable(kDsWaterMaximumStrengthPropertyId, &value))
      DsWaterMaximumStrength = value.value.word;
    if (provider->GetVariable(kDsWaterChancePropertyId, &value))
      DsWaterChance = value.value.word;
    if (provider->GetVariable(kDsWaterDamageRadiusPropertyId, &value))
      DsWaterDamageRadius = i32(value.value.word);
    if (provider->GetVariable(kDsWaterFirstDelayPropertyId, &value))
      DsWaterFirstDelay = value.value.word;
    if (provider->GetVariable(kDsWaterSecondDelayPropertyId, &value))
      DsWaterSecondDelay = value.value.word;
    if (provider->GetVariable(kDsWaterThirdDelayPropertyId, &value))
      DsWaterThirdDelay = value.value.word;
    if (provider->GetVariable(kDsWaterAttackDelayPropertyId, &value))
      DsWaterAttackDelay = value.value.word;
    if (provider->GetVariable(kDsWaterDurationPropertyId, &value))
      DsWaterDuration = value.value.word;
    if (provider->GetVariable(kDsWaterDebrisLifetimePropertyId, &value))
      DsWaterDebrisLifetime = value.value.word;
    if (provider->GetVariable(kDsWaterSpeedTimes1000PropertyId, &value))
      DsWaterSpeedTimes1000 = value.value.word;
    if (provider->GetVariable(kDsWaterFrameMicrosecondsPropertyId, &value))
      DsWaterFrameMicroseconds = value.value.word;
    if (provider->GetVariable(kDsWaterFastestSpeedTimes100000000PropertyId, &value))
      DsWaterFastestSpeedTimes100000000 = value.value.word;
    if (provider->GetVariable(kDsWaterOuterGravityDistancePropertyId, &value))
      DsWaterOuterGravityDistance = value.value.word;
  }
  return true;
}

// Construct the actual water manager at zero agent references and return its common-manager Unknown identity.
ScUnknown *DsCreateClass52FE8D50() {
  return static_cast<DsManager *>(new DsWaterManager);
}
