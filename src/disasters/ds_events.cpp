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

// ScMessageServer routes this subscription to DsLocustInstance::DoMessage.
// It retains the generic active-phase payload acknowledgement; the notification purpose is unresolved.
enum DsUnresolvedLocustMessage : u32 {
  kDsMsg_UnresolvedLocustNotification = 0xe5351201,
};

// Tuning keys retain the roles of the named fields read from the variable manager.
enum DsEventPropertyId : u32 {
  kDsEventMaximumActivePropertyId = 0x24860901,
  kDsEventMinimumMonthsPropertyId = 0x24860902,
  kDsEventMinimumStrengthPropertyId = 0x24860904,
  kDsEventMaximumStrengthPropertyId = 0x24860905,
  kDsEventChancePropertyId = 0x24860906,
  kDsEventDemolitionChances0PropertyId = 0x24860907,
  kDsEventDemolitionChances1PropertyId = 0x24860908,
  kDsEventDemolitionChances2PropertyId = 0x24860909,
  kDsEventCellRadiusLowPropertyId = 0x2486090a,
  kDsEventCellRadiusMediumPropertyId = 0x2486090b,
  kDsEventCellRadiusHighPropertyId = 0x2486090c,
  kDsEventPreparednessPercentPropertyId = 0x2486090d,
  kDsEventFirstDelayPropertyId = 0x2486090e,
  kDsEventSecondDelayPropertyId = 0x2486090f,
  kDsEventThirdDelayPropertyId = 0x24860910,
  kDsEventAttackDelayPropertyId = 0x24860911,
  kDsFireMinimumIntervalPropertyId = 0x47df002,
  kDsFireMaximumIntervalPropertyId = 0x647df00f,
  kDsFireMinimumDurationPropertyId = 0xc47df01a,
  kDsFireMaximumDurationPropertyId = 0xa47df024,
  kDsFireMinimumHazardPropertyId = 0x647df02b,
  kDsFireOuterHazardPropertyId = 0x647df032,
  kDsFireMinimumSpread0PropertyId = 0x247df038,
  kDsFireMaximumSpread0PropertyId = 0xa47df03e,
  kDsFireMinimumSpread1PropertyId = 0x47df044,
  kDsFireMaximumSpread1PropertyId = 0xe47df04a,
  kDsFireMinimumSpread2PropertyId = 0x47df051,
  kDsFireMaximumSpread2PropertyId = 0xa47df05a,
  kDsFireFloraChancePropertyId = 0x47df061,
};

enum DsEventArchiveKey : u32 {
  kDsEventHistoryRecordGroup = 0xe4889ef6,
  kDsEventInstancesRecordGroup = 0xe4889ef7,
};

DsAgent::DsAgent() : agent_type(nullptr), references(0) {
}

DsAgent::~DsAgent() {
  ScRelease(&agent_type);
}

// Clear every query output first, then expose IUnknown or the city-agent identity and acquire one reference.
// Unlike the instance queries, unsupported IDs deliberately leave a null output.
bool DsAgent::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtAgent);
  HANDLE_QUERY_INTERFACE(IID_CtAgent, CtAgent);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Acquire the agent before holding the module director, then return the count visible after that callback.
u32 DsAgent::AddRef() {
  ++references;
  DsGetDirector()->AddRef();
  return references;
}

// Drop a held agent/module pair, reading the count again after the director callback. Delete through the real
// virtual destructor when no agent references remain, including an already-zero receiver.
u32 DsAgent::Release() {
  if (references) {
    --references;
    DsGetDirector()->RemoveRef();
  }
  u32 count = references;
  if (!count) {
    delete this;
    return 0;
  }
  return count;
}

bool DsAgent::Simulate(i32 elapsed) {
  return agent_type->Simulate(this, elapsed);
}

i32 DsAgent::MinimumExecutionInterval() {
  return agent_type ? agent_type->MinimumExecutionInterval() : 6;
}

bool DsAgent::IsAgentLocking() {
  return true;
}

// Preserve the type-ID query's original side effect while retaining its unconditional zero result.
u32 DsAgent::TypeId() const {
  if (agent_type)
    agent_type->Id();
  return 0;
}

// Return the borrowed bound type without acquiring another reference.
CtAgentType *DsAgent::AgentType() {
  return agent_type;
}

// Release the old type before publishing and retaining the replacement. The caller supplies a live replacement;
// the original has no null guard or self-assignment shortcut.
bool DsAgent::SetAgentType(CtAgentType *replacement) {
  if (agent_type)
    agent_type->Release();
  agent_type = ScAddRefNotNull(replacement);
  return true;
}

bool DsAgent::DebugInstanceTag(ScStringView *) {
  return false;
}

bool DsAgent::DebugClassTag(ScStringView *) {
  return false;
}

bool DsAgent::DebugTypeTag(ScStringView *) {
  return false;
}

// Average the current view's two camera hints into the disaster rectangle, leaving output untouched without a view.
// Signed shifts preserve the original rounding; unsigned addition preserves the machine sum without C++ overflow.
bool DsController::GetVisibleRect(GeCellRect *bounds) {
  SpViewInterface *view = DsGetCityView();
  if (!view)
    return false;
  ScRect first, second;
  view->GetCameraHint(&first, &second);
  bounds->left = u32(i32(u32(first.left) + u32(second.left)) >> 1);
  bounds->right = u32(i32(u32(first.right) + u32(second.right)) >> 1);
  bounds->top = u32(i32(u32(first.top) + u32(second.top)) >> 1);
  bounds->bottom = u32(i32(u32(first.bottom) + u32(second.bottom)) >> 1);
  return true;
}

// The caller must have a city view; the original otherwise consumes unwritten camera bounds.
i32 DsController::IsInVisibleRect(u32 x, u32 z) {
  GeCellRect bounds;
  [[maybe_unused]] bool visible = GetVisibleRect(&bounds);
  assert(visible && "Disaster containment requires a city view");
  return x >= bounds.left && x <= bounds.right && z >= bounds.top && z <= bounds.bottom;
}

// Test unsigned inclusive overlap with the current camera, discarding the original unused intersection output.
// The caller must have a city view before the original camera bounds can be consumed.
u8 DsController::IntersectsVisibleRect(const GeCellRect &bounds) {
  GeCellRect camera;
  [[maybe_unused]] bool visible = GetVisibleRect(&camera);
  assert(visible && "Disaster intersection requires a city view");
  return !(bounds.right < camera.left || bounds.left > camera.right || bounds.bottom < camera.top ||
      bounds.top > camera.bottom);
}

DsInstances::DsInstances() : anchor{&anchor, &anchor, nullptr}, count(0) {
}

DsInstances::~DsInstances() {
  Clear();
}

// Destroy collection links and restore the empty stable anchor without acquiring or releasing any game instance.
// The controller retains responsibility for stopping and releasing its objects before this storage is cleared.
void DsInstances::Clear() {
  DsInstanceLink *node = anchor.next;
  while (node != &anchor) {
    DsInstanceLink *next = node->next;
    delete node;
    node = next;
  }
  anchor.next = &anchor;
  anchor.previous = &anchor;
  count = 0;
}

// Insert one borrowed instance in original unsigned pointer order, retaining stable entries and ignoring duplicates.
void DsInstances::Insert(DsInstance *instance) {
  uptr key = reinterpret_cast<uptr>(instance);
  DsInstanceLink *next = anchor.next;
  while (next != &anchor && reinterpret_cast<uptr>(next->instance) < key)
    next = next->next;
  if (next != &anchor && next->instance == instance)
    return;
  DsInstanceLink *node = new DsInstanceLink{next, next->previous, instance};
  next->previous->next = node;
  next->previous = node;
  ++count;
}

// Locate an existing entry by the same unsigned pointer key used for insertion. Missing entries have no link.
DsInstanceLink *DsInstances::Find(DsInstance *instance) {
  uptr key = reinterpret_cast<uptr>(instance);
  DsInstanceLink *node = anchor.next;
  while (node != &anchor && reinterpret_cast<uptr>(node->instance) < key)
    node = node->next;
  return node != &anchor && node->instance == instance ? node : nullptr;
}

// Unlink and destroy one found entry without changing instance ownership. Publish the reduced original set count.
void DsInstances::Remove(DsInstanceLink *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  --count;
  delete node;
}

DsChangeQueue::DsChangeQueue() : anchor{&anchor, &anchor, nullptr, 0} {
}

// Destroy links without synthesizing instance Release calls. Reference handling stays explicit in controller methods.
DsChangeQueue::~DsChangeQueue() {
  DsChangeLink *node = anchor.next;
  while (node != &anchor) {
    DsChangeLink *next = node->next;
    delete node;
    node = next;
  }
}

// Append one change without touching its instance reference; the caller acquires that reference before insertion.
void DsChangeQueue::Append(DsInstance *instance, i32 change) {
  DsChangeLink *node = new DsChangeLink{&anchor, anchor.previous, instance, change};
  anchor.previous->next = node;
  anchor.previous = node;
}

// Remove one existing link before its queued action runs, without releasing the instance retained by that action.
void DsChangeQueue::Remove(DsChangeLink *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

DsActiveIterator::DsActiveIterator(DsController *owner) : layer(owner) {
  Rewind();
}

void DsActiveIterator::Rewind() {
  current = layer->active_instances.anchor.next;
  end = &layer->active_instances.anchor;
}

// Advance before returning the borrowed instance. Reaching the captured active end repeatedly returns null
// without acquiring or dropping references on either the instance or controller.
DsInstance *DsActiveIterator::Next() {
  if (current == end)
    return nullptr;
  DsInstanceLink *node = current;
  current = current->next;
  return node->instance;
}

DsAllIterator::DsAllIterator(DsController *owner) : layer(owner) {
  Rewind();
}

// Restart at the active collection and rearm pending traversal even after that collection was previously consumed.
void DsAllIterator::Rewind() {
  pending = true;
  current = layer->active_instances.anchor.next;
  end = &layer->active_instances.anchor;
}

// Exhaust active instances first, then capture the pending collection's first entry and end anchor once.
// Instance references remain borrowed, including when the same pointer occurs in both collections.
DsInstance *DsAllIterator::Next() {
  if (current == end) {
    if (!pending)
      return nullptr;
    pending = false;
    current = layer->pending_instances.anchor.next;
    end = &layer->pending_instances.anchor;
    if (current == end)
      return nullptr;
  }
  DsInstanceLink *node = current;
  current = current->next;
  return node->instance;
}

// Allocate the original active-only iterator over this controller's actual instance storage.
DsListIterator *DsController::CreateActiveDisasterIterator() {
  return new DsActiveIterator(this);
}

// Allocate the original active-then-pending iterator over this controller's two ordered collections.
DsListIterator *DsController::CreateDisasterListIterator() {
  return new DsAllIterator(this);
}

void DsController::DeleteActiveDisasterIterator(DsListIterator *iterator) {
  delete static_cast<DsActiveIterator *>(iterator);
}

void DsController::DeleteDisasterListIterator(DsListIterator *iterator) {
  delete static_cast<DsAllIterator *>(iterator);
}

DsManipulator::DsManipulator() : references(0), layer(nullptr) {
}

DsManipulator::~DsManipulator() {
}

// Expose the same pointer for Unknown and the SDK manipulator identity, retaining only supported queries.
// Unsupported queries overwrite the caller's output with null.
bool DsManipulator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, CtLayerManipulator);
  HANDLE_QUERY_INTERFACE(IID_CtLayerManipulator, CtLayerManipulator);
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsManipulator::AddRef() {
  return ++references;
}

u32 DsManipulator::Release() {
  return ScReleaseImpl(this, &references);
}

bool DsManipulator::Apply(CtAgent *) {
  layer->AdvanceCounter();
  return true;
}

bool DsManipulator::IsLockingCity() {
  return false;
}

bool DsManipulator::DebugInstanceTag(ScStringView *) {
  return false;
}

bool DsManipulator::DebugClassTag(ScStringView *) {
  return false;
}

bool DsManipulator::DebugTypeTag(ScStringView *) {
  return false;
}

//; candidate private nominal return spelling remains unresolved.
// Replace the borrowed controller without acquiring or releasing it, preserving the observed input-pointer result.
DsController *DsManipulator::SetLayer(DsController *owner) {
  layer = owner;
  return owner;
}

// Query the supplied city agent for its acquired disaster-manager identity, update it and release afterward.
// Query refusal skips both calls; the callback reports success independently of the manager's update result.
bool DsManagerManipulator::Apply(CtAgent *agent) {
  void *queried;
  if (agent->QueryInterface(IID_DsManager, &queried)) {
    auto *manager = static_cast<DsManager *>(queried);
    manager->Tick();
    manager->Release();
  }
  return true;
}

// Skip active-instance progression while changes are already deferred; otherwise defer mutation during the pass.
// Advance the saved cursor before each virtual call, drain queued changes, then clear deferral without relocking.
bool DsActiveManipulator::Apply(CtAgent *) {
  layer->change_lock.Lock();
  if (layer->defer_changes) {
    layer->change_lock.Unlock();
    return true;
  }
  layer->defer_changes = true;
  layer->change_lock.Unlock();
  DsInstanceLink *anchor = &layer->active_instances.anchor, *node = anchor->next;
  while (node != anchor) {
    DsInstance *instance = node->instance;
    node = node->next;
    instance->Tick(DsMinuteTick);
  }
  layer->ProcessChanges();
  layer->defer_changes = false;
  return true;
}

// Defer mutations while progressing pending instances even when an outer caller already set the flag.
// Drain queued changes, then acquire the original recursive lock again before clearing deferral.
bool DsPendingManipulator::Apply(CtAgent *) {
  layer->change_lock.Lock();
  layer->defer_changes = true;
  layer->change_lock.Unlock();
  DsInstanceLink *anchor = &layer->pending_instances.anchor, *node = anchor->next;
  while (node != anchor) {
    DsInstance *instance = node->instance;
    node = node->next;
    instance->Tick(DsDayTick);
  }
  layer->ProcessChanges();
  layer->change_lock.Lock();
  layer->defer_changes = false;
  layer->change_lock.Unlock();
  return true;
}

ScArray<DsManagerEntry> DsManagers;

// Construct pending and active storage before the real random stream, listeners, queued changes and final lock.
// Initialize original written state, leaving city bounds, manipulator pointers and the guarded counter unwritten.
DsController::DsController()
    : references(0), agent_types(nullptr), initialized(false), enabled(false), city(nullptr),
      random(kScRandomSeedFromClock), flag_88(false), occupants(nullptr), public_safety(nullptr), fire_service(nullptr),
      police_service(nullptr), budget(nullptr), building_layer(nullptr), demolition(nullptr), service_132(nullptr),
      goto_count(0), fire_dispatch_count(0), police_dispatch_count(0), plane_dispatch_count(0), flag_164(false),
      counter_requests(0), box(nullptr), cost_total(0), simulation_agent(nullptr), agent_registered(false),
      change_agent(nullptr), change_registered(false), defer_changes(false), word_208(UINT32_MAX),
      counter_on_calendar(0), counter_start_value(0), simulation_running(false) {
}

// Construct a zero-reference controller and return its original city-layer factory identity without acquiring it.
ScUnknown *DsCreateLayer() {
  return static_cast<CtLayer *>(new DsController);
}

// Query the primary disaster or secondary city-layer view, clearing the output before testing the identity.
// Every successful view acquires the same controller reference through the primary virtual AddRef operation.
bool DsController::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, DsLayer);
  HANDLE_QUERY_INTERFACE(IID_DsLayer, DsLayer);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_CtLayer, CtLayer);
  HANDLE_QUERY_INTERFACE(IID_ScQueryTarget, CtLayer);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 DsController::AddRef() {
  return ++references;
}

bool DsController::DisastersEnabled() {
  return enabled;
}

u32 DsController::GetActiveDisasterCount() {
  return active_instances.count;
}

bool DsController::IsSimulationRunning() {
  return simulation_running;
}

// Expand a rectangle against the city's inclusive maxima. Preserve the original write/read order so an
// in-place rectangle follows the same behavior; unsigned additions wrap before the upper clipping checks.
void DsController::ComputeSurroundRect(const GeCellRect &input, GeCellRect *output, u32 distance) {
  output->right = input.right + distance;
  if (output->right > maximum_x)
    output->right = maximum_x;
  output->bottom = input.bottom + distance;
  if (output->bottom > maximum_z)
    output->bottom = maximum_z;
  output->left = input.left <= distance ? 0 : input.left - distance;
  output->top = input.top <= distance ? 0 : input.top - distance;
}

DsListeners::DsListeners() : anchor{&anchor, &anchor, nullptr, nullptr} {
}

DsListeners::~DsListeners() {
  Clear();
}

// Erase registrations while preserving the stable listener anchor and caller-owned callback contexts.
// Shutdown and automatic destruction share this same node-storage cleanup.
void DsListeners::Clear() {
  DsChangeListener *listener = anchor.next;
  while (listener != &anchor) {
    DsChangeListener *next = listener->next;
    delete listener;
    listener = next;
  }
  anchor.next = &anchor;
  anchor.previous = &anchor;
}

// Append a callback/context pair, allowing duplicates. The source rejects a null callback before allocation.
bool DsController::NotifyOnChange(ChangeCallback callback, void *context) {
  if (!callback)
    return false;
  DsChangeListener *anchor = &listeners.anchor;
  DsChangeListener *listener = new DsChangeListener{anchor, anchor->previous, callback, context};
  anchor->previous->next = listener;
  anchor->previous = listener;
  return true;
}

// Remove the first registration matching both callback and context. Later duplicates remain in insertion order.
bool DsController::StopNotifyingOnChange(ChangeCallback callback, void *context) {
  DsChangeListener *anchor = &listeners.anchor;
  for (DsChangeListener *listener = anchor->next; listener != anchor; listener = listener->next) {
    if (listener->callback == callback && listener->context == context) {
      listener->previous->next = listener->next;
      listener->next->previous = listener->previous;
      delete listener;
      return true;
    }
  }
  return false;
}

// Return the manager registered under the full disaster ID. This is a borrowed interface; the lookup does not AddRef.
DsManager *DsController::GetDisasterManager(u32 type) {
  for (u32 i = 0; i < DsManagers.size; ++i)
    if (DsManagers.data[i].type == type)
      return DsManagers.data[i].manager;
  return nullptr;
}

// Forward a controller trigger to the registered disaster manager. An absent ID fails without invoking any manager.
// This path reads the registry directly rather than dispatching through the overridable public lookup method.
bool DsController::AttemptDisasterStart(u32 type) {
  for (u32 i = 0; i < DsManagers.size; ++i)
    if (DsManagers.data[i].type == type)
      return DsManagers.data[i].manager->AttemptDisasterStart();
  return false;
}

// Find the next unique type in unsigned registry order without compiler tree machinery.
// The caller copies its key and receiver before invoking callbacks that may grow native storage.
static DsManagerEntry *DsNextManager(u32 previous, bool have_previous) {
  DsManagerEntry *selected = nullptr;
  for (u32 i = 0; i < DsManagers.size; ++i) {
    DsManagerEntry *entry = DsManagers.data + i;
    if ((!have_previous || entry->type > previous) && (!selected || entry->type < selected->type))
      selected = entry;
  }
  return selected;
}

// Change scheduling only when the enabled state changes. Query each manager's agent in unsigned type order;
// enabling assigns the original agent type before registration, and both paths release the queried reference.
// Preserve the original successful byte result even when no scheduling transition is required.
bool DsController::EnableDisasters(bool requested) {
  if (enabled == requested)
    return true;
  CtSimulatorInterface *simulator = city->CitySimulator();
  u32 previous = 0;
  bool have_previous = false;
  while (DsManagerEntry *entry = DsNextManager(previous, have_previous)) {
    previous = entry->type;
    have_previous = true;
    DsManager *manager = entry->manager;
    void *value;
    manager->QueryInterface(IID_CtAgent, &value);
    CtAgent *agent = static_cast<CtAgent *>(value);
    if (requested) {
      agent->SetAgentType(agent_types->GetNode(kCtAgentType_DisasterManager));
      simulator->AddAgent(agent);
    } else {
      simulator->RemoveAgent(agent);
    }
    agent->Release();
  }
  enabled = requested;
  return true;
}

// Retire every registered manager in unsigned key order, reloading its stored interface after StaticShutdown callbacks.
// Clear each pointer only after Release returns, then remove the registry entries without retaining game interfaces.
bool DsController::StaticShutdown() {
  u32 previous = 0;
  bool have_previous = false;
  while (DsManagerEntry *entry = DsNextManager(previous, have_previous)) {
    previous = entry->type;
    have_previous = true;
    entry->manager->StaticShutdown();
    DsController::GetDisasterManager(previous)->Release();
    for (u32 i = 0; i < DsManagers.size; ++i)
      if (DsManagers.data[i].type == previous) {
        DsManagers.data[i].manager = nullptr;
        break;
      }
  }
  DsManagers.Clear();
  return true;
}

u32 DsLayerCounterLimit = 8;

// Send the original BoxX event before testing the live counter flag, then offer the UI according to the request count.
// Preserve cached message decisions across callbacks and recheck instance counts only when selecting the relief flag.
void DsController::StartCounter(u32 initial) {
  box->Event(kAuEventEarlyWarningSiren, 0, 0, 0, 0);
  if (!flag_164) {
    bool has_instances = pending_instances.count || active_instances.count;
    if (!has_instances)
      ++counter_requests;
    bool offered = counter_requests <= 2;
    ScMessage message(kDsMsg_CounterStartRequested, has_instances, offered, 0);
    DsGetMessageServer()->MessagePost(message, false);
    if (has_instances)
      counter_requests = 0;
    if (offered) {
      flag_164 = true;
      if (pending_instances.count || active_instances.count)
        flag_88 = true;
      counter_184 = initial;
    }
  }
}

void DsController::StartCounterAtZero() {
  StartCounter(0);
}

void DsController::AdvanceCounter() {
  if (flag_164 && ++counter_184 > DsLayerCounterLimit)
    EndCounter();
}

// Clear the flag before posting the original zero-payload notification at normal priority. Reset the counter only after
// the message callback returns, preserving callback-visible ordering.
void DsController::EndCounter() {
  if (flag_164) {
    flag_164 = false;
    ScMessage message = {kDsMsg_CounterEnded, 0, 0, 0};
    DsGetMessageServer()->MessagePost(message, false);
    counter_184 = 0;
  }
}

// Account for ended-instance cost before notifying listeners, then choose deferred or immediate application under
// the original recursive lock. Retain before queuing; immediate application runs only after unlocking.
void DsController::DisasterInstanceChange(DsInstance *instance, i32 change) {
  if (!initialized)
    return;
  if (change == 4)
    cost_total += instance->GetCost();
  NotifyListeners(instance, change);
  change_lock.Lock();
  if (defer_changes) {
    instance->AddRef();
    changes.Append(instance, change);
    change_lock.Unlock();
  } else {
    change_lock.Unlock();
    ApplyChange(instance, change);
  }
}

// Dispatch registrations in list order, re-reading the current link after every callback. Later insertion/removal
// therefore affects the same pass; erasing the current registration remains outside the original defined contract.
void DsController::NotifyListeners(DsInstance *instance, i32 change) {
  DsChangeListener *anchor = &listeners.anchor;
  for (DsChangeListener *listener = anchor->next; listener != anchor; listener = listener->next)
    listener->callback(instance, change, listener->context);
}

// Remove each queued record before applying it, retaining its saved instance reference through the application call.
// Release afterward and re-read the head, so changes enqueued by application or release drain in the same pass.
void DsController::ProcessChanges() {
  DsChangeLink *anchor = &changes.anchor;
  while (anchor->next != anchor) {
    DsChangeLink *node = anchor->next;
    DsInstance *instance = node->instance;
    i32 change = node->change;
    changes.Remove(node);
    ApplyChange(instance, change);
    instance->Release();
  }
}

// Route the original registration, activation and removal change words. Other words have no application effects.
void DsController::ApplyChange(DsInstance *instance, i32 change) {
  if (change == 0)
    RegisterPending(instance);
  else if (change == 1)
    ActivateInstance(instance);
  else if (change == 4)
    RemoveInstance(instance);
}

void DsController::RegisterPending(DsInstance *instance) {
  instance->AddRef();
  pending_instances.Insert(instance);
}

// Post the original command enable message through a borrowed server when available. Normalize the byte flag only
// in the message payload; a missing server produces no message and does not alter the caller's counter progression.
static void DsPostMenuEnabled(u32 command, u8 enabled) {
  ScMessageServer *server = DsGetMessageServer();
  if (server) {
    ScMessage message = {kWnMsg_GameMenuSetItemEnabled, command, enabled != 0, 0};
    server->MessagePost(message, false);
  }
}

// Enable Goto before acquiring its first counted use. Release a positive signed count and disable whenever zero,
// preserving callback mutations before acquisition and the original repeated disable at an already-zero count.
void DsController::UpdateGotoCount(u8 add) {
  if (add) {
    if (!goto_count)
      DsPostMenuEnabled(kUiMenuGoToDisasterItemId, 1);
    ++goto_count;
  } else {
    if (i32(goto_count) > 0)
      --goto_count;
    if (!goto_count)
      DsPostMenuEnabled(kUiMenuGoToDisasterItemId, 0);
  }
}

// Enable Fire Dispatch before its first counted use, retaining the counter read after any posted-message callback.
// A release decrements only a positive signed count and posts disable when the resulting count is zero.
void DsController::UpdateFireDispatchCount(u8 add) {
  if (add) {
    if (!fire_dispatch_count)
      DsPostMenuEnabled(kUiMenuDispatchFirefightersItemId, 1);
    ++fire_dispatch_count;
  } else {
    if (i32(fire_dispatch_count) > 0)
      --fire_dispatch_count;
    if (!fire_dispatch_count)
      DsPostMenuEnabled(kUiMenuDispatchFirefightersItemId, 0);
  }
}

// Maintain Police Dispatch's independent counted enablement with the original enable-before-increment ordering.
// Signed-positive release and zero notification remain distinct from an unsigned decrement or an idempotent setter.
void DsController::UpdatePoliceDispatchCount(u8 add) {
  if (add) {
    if (!police_dispatch_count)
      DsPostMenuEnabled(kUiMenuDispatchPoliceItemId, 1);
    ++police_dispatch_count;
  } else {
    if (i32(police_dispatch_count) > 0)
      --police_dispatch_count;
    if (!police_dispatch_count)
      DsPostMenuEnabled(kUiMenuDispatchPoliceItemId, 0);
  }
}

// Maintain Plane Dispatch's independent counted enablement and permit callbacks to change its count before increment.
// Release posts disable at zero, including a repeated release while the count was already zero.
void DsController::UpdatePlaneDispatchCount(u8 add) {
  if (add) {
    if (!plane_dispatch_count)
      DsPostMenuEnabled(kUiMenuDispatchCropDustersItemId, 1);
    ++plane_dispatch_count;
  } else {
    if (i32(plane_dispatch_count) > 0)
      --plane_dispatch_count;
    if (!plane_dispatch_count)
      DsPostMenuEnabled(kUiMenuDispatchCropDustersItemId, 0);
  }
}

// Move a pending instance into the active set without changing its held reference. Enable the original controls
// before announcing the first active disaster and registering the shared agent; insert only after those callbacks.
void DsController::ActivateInstance(DsInstance *instance) {
  DsInstanceLink *pending = pending_instances.Find(instance);
  if (!pending)
    return;
  pending_instances.Remove(pending);
  UpdateGotoCount(1);
  if (instance->TypeId() != kDsParadeTypeId) {
    UpdateFireDispatchCount(1);
    UpdatePoliceDispatchCount(1);
  }
  if (instance->TypeId() == kDsLocustTypeId)
    UpdatePlaneDispatchCount(1);
  if (!active_instances.count) {
    ScMessage message = {kCtMsg_DisasterStarted, 0, 0, 0};
    DsGetMessageServer()->MessagePost(message, false);
    CtSimulatorInterface *simulator = city->CitySimulator();
    agent_registered = simulator->AddAgent(simulation_agent);
  }
  active_instances.Insert(instance);
}

// Remove active or pending membership before releasing its held reference, updating controls only for active instances.
// Revoke crews and unregister the agent after the last active removal; finish counter and relief when both sets empty.
bool DsController::RemoveInstance(DsInstance *instance) {
  if (!initialized)
    return true;
  DsInstanceLink *node = active_instances.Find(instance);
  if (node) {
    active_instances.Remove(node);
    UpdateGotoCount(0);
    if (instance->TypeId() != kDsParadeTypeId) {
      UpdateFireDispatchCount(0);
      UpdatePoliceDispatchCount(0);
    }
    if (instance->TypeId() == kDsLocustTypeId)
      UpdatePlaneDispatchCount(0);
    if (!active_instances.count) {
      ScMessageServer *server = DsGetMessageServer();
      static_cast<CtFireLayer *>(fire_service)->RevokeAllDispatches();
      static_cast<CtPoliceLayer *>(police_service)->RevokeAllDispatches();
      if (agent_registered) {
        city->CitySimulator()->RemoveAgent(simulation_agent);
        agent_registered = false;
      }
      ScMessage message(kCtMsg_DisasterEnded, 0, 0, 0);
      server->MessagePost(message, false);
    }
  } else {
    node = pending_instances.Find(instance);
    if (!node)
      return false;
    pending_instances.Remove(node);
  }
  instance->Release();
  if (!active_instances.count && !pending_instances.count) {
    if (flag_164)
      EndCounter();
    GrantRelief();
    flag_88 = false;
  }
  return true;
}

u32 DsReliefMinimumCost = 0;
u8 DsReliefPercentWithEw = 0, DsReliefPercentWithoutEw = 0;

// While simulation is running, deposit relief only above the unsigned minimum, selecting the original EW percentage.
// Preserve DWORD product wrapping and clear cost after the budget callback, including its mutations.
void DsController::GrantRelief() {
  if (simulation_running) {
    u32 cost = cost_total;
    if (cost > DsReliefMinimumCost) {
      u8 percent = flag_88 ? DsReliefPercentWithEw : DsReliefPercentWithoutEw;
      budget->DepositDisasterReliefFunds((cost * percent) / 100u);
    }
    cost_total = 0;
  }
}

// Price demolition from development cost twelve and the occupant's flat type cost, then sample its Z/X footprint.
// Reread the retained budget after TypeId because each original virtual call may alter the controller's services.
u32 DsController::GetDemolitionCost(ScOccupant *occupant) {
  i32 rate = budget->GetCost(kEcCostDemolishRubble);
  u32 type = occupant->TypeId();
  i32 flat = budget->GetCost(type);
  u32 z = occupant->SizeInCellsZ(), x = occupant->SizeInCellsX();
  return u32(flat) + u32(rate) * x * z;
}

// Retain public safety, synchronize manager scheduling with the city option and construct the change-processing agent.
// Preserve publication before callbacks, the actual AddAgent result and the final simulation-running store.
bool DsController::SimulationBegin() {
  public_safety = ScAddRefNotNull(city->GetPublicSafetyAdvisor());
  bool requested = city->AreDisastersEnabled();
  DsController::EnableDisasters(requested);
  change_agent = ScAddRefNotNull(new DsAgent);
  auto *type = agent_types->GetNode(kCtAgentType_DisasterInstanceDayTick);
  change_agent->SetAgentType(type);
  change_registered = city->CitySimulator()->AddAgent(change_agent);
  simulation_running = true;
  return true;
}

// Clear the running flag before stopping managers, then unregister change and per-instance simulation agents in order.
// Each registration flag remains set throughout its RemoveAgent callback and clears even when that call returns false.
bool DsController::SimulationEnd() {
  if (simulation_running) {
    simulation_running = false;
    DsController::EnableDisasters(false);
    if (change_registered) {
      city->CitySimulator()->RemoveAgent(change_agent);
      change_registered = false;
    }
    if (agent_registered) {
      city->CitySimulator()->RemoveAgent(simulation_agent);
      agent_registered = false;
    }
  }
  return true;
}

// Return the controller's borrowed manipulator and original flag word selected by the agent type's full ID.
bool DsController::GetManipulator(const CtAgentType &type, CtLayerManipulator **out, i32 *flags) {
  *out = nullptr;
  switch (type.Id()) {
  case kCtAgentType_DisasterManager:
    *out = manipulator_96;
    *flags = 4;
    break;
  case kCtAgentType_DisasterInstanceDayTick:
    *out = manipulator_152;
    *flags = 2;
    break;
  case kCtAgentType_AreaAgent:
    *out = manipulator_92;
    *flags = 2;
    break;
  case kCtAgentType_DisasterInstanceMinuteTick:
    *out = manipulator_156;
    *flags = 0;
    break;
  }
  return true;
}

u32 DsController::GetLayerType() {
  return kDsControllerLayerType;
}

bool DsController::DoQueryInfo(ScMessage *, ScUnknown *) {
  return false;
}

bool DsController::Init(CtCity *, ScImporter *) {
  return true;
}

bool DsController::DebugClassTag(ScStringView *) {
  return false;
}

bool DsController::DebugTypeTag(ScStringView *) {
  return false;
}

// Acknowledge messages while uninitialized, then route only the original seven IDs with their distinct running gates.
// Narrow scalar payloads at their original word boundary and discard private handler results as the binary does.
bool DsController::DoMessage(ScMessage *message) {
  if (initialized) {
    switch (message->type) {
    case kWnMsg_CityViewReady:
      if (counter_on_calendar)
        StartCounter(counter_start_value);
      break;
    case kDsMsg_RequestDisaster:
      if (simulation_running)
        AttemptDisasterStart(u32(message->data1));
      break;
    case kDsMsg_DispatchPolice:
      if (simulation_running)
        DispatchPolice(u32(message->data1), u32(message->data2));
      break;
    case kDsMsg_DispatchFire:
      if (simulation_running)
        DispatchFire(u32(message->data1), u32(message->data2));
      break;
    case kDsMsg_DispatchPlane:
      if (simulation_running)
        CanDispatchPlane(u32(message->data1), u32(message->data2));
      break;
    case kDsMsg_StartDisasterCounter:
      if (simulation_running)
        StartCounterAtZero();
      break;
    case kDsMsg_SetDisastersEnabled:
      if (simulation_running)
        DsController::EnableDisasters(u32(message->data1) != 0);
      break;
    default:
      return false;
    }
  }
  return true;
}

bool DsController::CanDispatchPlane(u32, u32) {
  return active_instances.count != 0;
}

DsController::~DsController() {
  if (initialized)
    DsController::Shutdown();
}

u32 DsController::Release() {
  return ScReleaseImpl(this, &references);
}

// Clear readiness before unsubscribing, stop and release active then pending instances, then shut managers down.
// Clear listeners and retire retained services in their original order, keeping each pointer published through Release.
bool DsController::Shutdown() {
  if (!initialized)
    return true;
  initialized = false;
  ScMessageServer *server = DsGetMessageServer();
  u32 messages[] = {kWnMsg_CityViewReady, kDsMsg_RequestDisaster, kDsMsg_DispatchPolice, kDsMsg_DispatchFire,
      kDsMsg_DispatchPlane, kDsMsg_StartDisasterCounter, kDsMsg_SetDisastersEnabled};
  for (u32 message : messages)
    server->RemoveNotification(static_cast<CtLayer *>(this), message);
  DsInstanceLink *node = active_instances.anchor.next;
  while (node != &active_instances.anchor) {
    DsInstance *instance = node->instance;
    node = node->next;
    instance->GetManager()->StopInstance(instance);
    instance->Release();
  }
  active_instances.Clear();
  node = pending_instances.anchor.next;
  while (node != &pending_instances.anchor) {
    DsInstance *instance = node->instance;
    node = node->next;
    instance->GetManager()->StopInstance(instance);
    instance->Release();
  }
  pending_instances.Clear();
  u32 previous = 0;
  bool have_previous = false;
  while (DsManagerEntry *entry = DsNextManager(previous, have_previous)) {
    previous = entry->type;
    have_previous = true;
    entry->manager->Shutdown();
  }
  listeners.Clear();
  ScRelease(&change_agent);
  ScRelease(&simulation_agent);
  ScRelease(&public_safety);
  if (service_132) {
    static_cast<ScUnknown *>(service_132)->Release();
    service_132 = nullptr;
  }
  ScRelease(&demolition);
  ScRelease(&budget);
  ScRelease(&building_layer);
  if (fire_service) {
    static_cast<CtFireLayer *>(fire_service)->Release();
    fire_service = nullptr;
  }
  if (police_service) {
    static_cast<CtPoliceLayer *>(police_service)->Release();
    police_service = nullptr;
  }
  ScRelease(&box);
  ScRelease(&occupants);
  ScRelease(&city);
  ScRelease(&agent_types);
  ScRelease(&manipulator_96);
  ScRelease(&manipulator_156);
  ScRelease(&manipulator_152);
  ScRelease(&manipulator_92);
  return true;
}

bool DsController::DispatchFire(u32 x, u32 z) {
  return active_instances.count ? static_cast<CtFireLayer *>(fire_service)->DispatchCrew(x, z) : false;
}

bool DsController::DispatchPolice(u32 x, u32 z) {
  return active_instances.count ? static_cast<CtPoliceLayer *>(police_service)->DispatchCrew(x, z) : false;
}

// Save every manager in unsigned registry order before writing the controller's version505 record.
// Ignore manager refusals, stop controller payload writes at the first refusal and retain real archive cleanup.
bool DsController::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  u32 previous = 0;
  bool have_previous = false;
  while (DsManagerEntry *entry = DsNextManager(previous, have_previous)) {
    previous = entry->type;
    have_previous = true;
    entry->manager->Save(input_city, segment);
  }
  DsArchive archive(input_city, segment, {kRsCityLayerRecordType, kDsControllerLayerType, 0}, 505, 0);
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (!valid || !stream)
    return false;
  u8 result = stream->WriteI32(i32(cost_total));
  result = result && stream->WriteU32(counter_requests) && stream->WriteU32(flag_164 != 0) &&
      stream->WriteU32(counter_184) && stream->WriteU32(flag_88 != 0);
  return result != 0;
}

// Load every manager before the controller record, preserving partial field writes and unconditional success.
// Use returned flag outputs even on refusal; zero local backing excludes only originally undefined unwritten reads.
bool DsController::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  u32 previous = 0;
  bool have_previous = false;
  while (DsManagerEntry *entry = DsNextManager(previous, have_previous)) {
    previous = entry->type;
    have_previous = true;
    entry->manager->Init(input_city, segment);
  }
  DsArchive archive(input_city, segment, {kRsCityLayerRecordType, kDsControllerLayerType, 0});
  u8 valid = archive.valid;
  RsStream *stream = archive.Stream();
  if (valid && stream->ReadU32(&cost_total) && stream->ReadU32(&counter_requests)) {
    u32 value = 0;
    u8 result = stream->ReadU32(&value);
    counter_on_calendar = value != 0;
    if (result && stream->ReadU32(&counter_start_value)) {
      stream->ReadU32(&value);
      flag_88 = value != 0;
    }
  }
  return true;
}

// Bind live city services and manipulators before initializing managers and publishing the original UI subscriptions.
// Preserve direct query-output publication and partial acquisitions on failure; set readiness after binding the agent.
bool DsController::Init(CtCity *owner) {
  if (initialized)
    Shutdown();
  do {
    if (!owner)
      break;
    city = ScAddRefNotNull(owner);
    maximum_x = city->CellCountX() - 1;
    maximum_z = city->CellCountZ() - 1;
    cost_total = 0;
    manipulator_92 = ScAddRefNotNull(new DsManipulator);
    static_cast<DsManipulator *>(manipulator_92)->SetLayer(this);
    manipulator_96 = ScAddRefNotNull(new DsManagerManipulator);
    static_cast<DsManipulator *>(manipulator_96)->SetLayer(this);
    manipulator_156 = ScAddRefNotNull(new DsActiveManipulator);
    static_cast<DsManipulator *>(manipulator_156)->SetLayer(this);
    manipulator_152 = ScAddRefNotNull(new DsPendingManipulator);
    static_cast<DsManipulator *>(manipulator_152)->SetLayer(this);
    box = DsGetBoxX();
    if (!box)
      break;
    box->AddRef();
    CtLayer *specific = city->GetSpecificLayer(kCtFireLayerType);
    if (!specific || !specific->QueryInterface(IID_CtFireLayer, &fire_service))
      break;
    specific = city->GetSpecificLayer(kCtPoliceLayerType);
    if (!specific || !specific->QueryInterface(IID_CtPoliceLayer, &police_service))
      break;
    budget = city->BudgetLayer();
    if (!budget)
      break;
    budget->AddRef();
    building_layer = city->BuildingLayer();
    if (!building_layer)
      break;
    building_layer->AddRef();
    demolition = city->DemolitionLayer();
    if (demolition)
      demolition->AddRef();
    specific = city->GetSpecificLayer(kCtFlammabilityLayerType);
    if (specific)
      specific->QueryInterface(IID_EnHazardMap, &service_132);
    agent_types = DsGetAgentTree();
    if (!agent_types)
      break;
    agent_types->AddRef();
    occupants = city->SurfaceOccupantManager();
    if (!occupants)
      break;
    occupants->AddRef();
    u32 previous = 0;
    bool have_previous = false;
    while (DsManagerEntry *entry = DsNextManager(previous, have_previous)) {
      previous = entry->type;
      have_previous = true;
      entry->manager->Init(static_cast<DsLayer *>(this), city);
    }
    ScMessageServer *server = DsGetMessageServer();
    u32 commands[] = {kUiMenuGoToDisasterItemId, kUiMenuDispatchPoliceItemId, kUiMenuDispatchFirefightersItemId,
        kUiMenuDispatchCropDustersItemId};
    for (u32 command : commands) {
      ScMessage message(kWnMsg_GameMenuSetItemEnabled, command, 0, 0);
      server->MessagePost(message, false);
    }
    u32 messages[] = {kWnMsg_CityViewReady, kDsMsg_RequestDisaster, kDsMsg_DispatchPolice, kDsMsg_DispatchFire,
        kDsMsg_DispatchPlane, kDsMsg_StartDisasterCounter, kDsMsg_SetDisastersEnabled};
    for (u32 message : messages)
      server->AddNotification(static_cast<CtLayer *>(this), message);
    simulation_agent = ScAddRefNotNull(new DsAgent);
    auto *type = agent_types->GetNode(kCtAgentType_DisasterInstanceMinuteTick);
    simulation_agent->SetAgentType(type);
    defer_changes = false;
    initialized = true;
    return true;
  } while (false);
  Shutdown();
  return false;
}

// Parse a description row and acquire its manager before invoking static initialization, whose refusal is ignored.
// Replace an existing manager only after static shutdown and a reloaded release; publish the new pair afterward.
static void DsRegisterManager(const ScString &key, const ScString &value, void *) {
  u32 type = DsParseNumber(key.ToChar()), class_id = DsParseNumber(value.ToChar());
  void *created;
  if (!DsGetCom()->GetClassObject(class_id, IID_DsManager, &created))
    return;
  auto *manager = static_cast<DsManager *>(created);
  manager->StaticInit();
  u32 index = 0;
  while (index < DsManagers.size && DsManagers.data[index].type != type)
    ++index;
  if (index == DsManagers.size) {
    *DsManagers.Append() = {type, class_id, manager};
  } else {
    if (DsManagers.data[index].manager) {
      DsManagers.data[index].manager->StaticShutdown();
      DsManagers.data[index].manager->Release();
    }
    DsManagers.data[index].manager = manager;
    DsManagers.data[index].class_id = class_id;
  }
}

// Select the original loose and packaged disaster configuration paths before registering description-row managers.
// Apply found tunables in order, retaining prior values on misses and the original byte truncation for relief percentages.
bool DsController::StaticInit(CtDefinition *) {
  ScString path;
  DsIni reader;
  ScString value;
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3DisasterLayer.INI");
  reader.SetPath(path);
  DsGetApplication()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SYS.PAK");
  reader.SetCompressedPath(path);
  {
    ScString section("DisasterDescriptions");
    reader.EnumEntries(section, DsRegisterManager, nullptr);
  }
  const char *keys[] = {"MaxEmergencyDuration", "MinDisasterCostToGetRelief", "ReliefAsPercentageOfCostWithoutEW",
      "ReliefAsPercentageOfCostWithEW"};
  for (u32 index = 0; index < 4; ++index) {
    u8 found;
    {
      ScString key(keys[index]), section("MiscDisasterTunables");
      found = reader.ReadEntry(section, key, &value);
    }
    if (found) {
      u32 parsed = DsParseNumber(value.ToChar());
      switch (index) {
      case 0:
        DsLayerCounterLimit = parsed;
        break;
      case 1:
        DsReliefMinimumCost = parsed;
        break;
      case 2:
        DsReliefPercentWithoutEw = u8(parsed);
        break;
      case 3:
        DsReliefPercentWithEw = u8(parsed);
        break;
      }
    }
  }
  return true;
}

// Create a replacement from the original building attributes, publish occupancy state and copy location/rotation.
// Insert after the optional occupant query and release the acquired building last, retaining original refusal behavior.
u8 DsController::ReplaceBuilding(u32 x, u32 z, u32 rotation, GeAttrib *attributes, u32 state) {
  void *created;
  auto *buildings = static_cast<GeBuildingLayer *>(building_layer);
  if (!buildings->CreateBuilding(attributes, IID_BdBuilding, &created))
    return 0;
  static_cast<BdBuilding *>(created)->SetOccupancyState(state);
  void *queried = nullptr;
  if (static_cast<BdBuilding *>(created)->QueryInterface(IID_ScOccupant, &queried)) {
    static_cast<ScOccupant *>(queried)->SetLocationInCellCoords(x, z);
    static_cast<ScOccupant *>(queried)->GetSpriteInst()->SetRotationOffset(u8(rotation));
    static_cast<ScOccupant *>(queried)->Release();
    queried = nullptr;
  }
  u8 result = static_cast<GeBuildingLayer *>(building_layer)->InsertBuilding(static_cast<BdBuilding *>(created));
  static_cast<BdBuilding *>(created)->Release();
  return result;
}

// Remove an eligible occupant through the real demolition tool, then create its requested building or flora replacement.
// Preserve state rejection, cost publication, sprite rotation, caller retention and the final radioactive-area update.
bool DsController::DestroyOccupant(ScOccupant *occupant, i32 cause, u32 *cost) {
  GeBounds bounds = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  GeBounds affected = bounds;
  *cost = 0;
  if (!simulation_running || !occupant->CanSimRemove())
    return false;
  u32 type = occupant->TypeId();
  if (agent_types->IsAncestor(kCtAgentLandfill, type))
    return false;
  u32 x, z;
  occupant->GetLocationInCellCoords(&x, &z);
  occupant->GetBounds(&bounds);
  bool radioactive = agent_types->IsAncestor(kCtAgentNuclearPlant, type);
  bool flora = agent_types->IsAncestor(kCtAgentFlora, type);
  void *queried;
  bool is_building = occupant->QueryInterface(IID_BdBuilding, &queried);
  // The sprite produces one byte; forward that value without reading the original caller's unwritten padding.
  u8 rotation;
  occupant->GetSpriteInst()->GetRotationOffset(&rotation);
  GeAttrib *attributes = nullptr;
  u32 state = 0, kind = 4;
  if (is_building) {
    attributes = static_cast<BdBuilding *>(queried)->GetBaseAttribData();
    state = static_cast<BdBuilding *>(queried)->GetOccupancyState();
    static_cast<BdBuilding *>(queried)->Release();
    queried = nullptr;
    if (state == u32(cause) || state == kDsDamageBurn ||
        (state == kDsDamageRubble && (cause == kDsDamageRubble || cause == kDsDamageAbandon)))
      return false;
  } else if (flora) {
    if (occupant->QueryInterface(IID_GePlantIdentity, &queried)) {
      kind = static_cast<GePlantIdentity *>(queried)->GetFloraType();
      static_cast<GePlantIdentity *>(queried)->Release();
    }
  }
  if (!is_building || (state != kDsDamageBurn && state != kDsDamageRubble))
    *cost = GetDemolitionCost(occupant);
  u32 ignored_cost;
  if (!static_cast<GeDemolitionView *>(demolition)
          ->DemolishOccupants(occupants, &bounds, nullptr, 0, true, &ignored_cost, &affected))
    occupants->RemoveOccupant(occupant);
  if (cause) {
    if (is_building)
      ReplaceBuilding(x, z, rotation, attributes, u32(cause));
    else if (flora && (cause == kDsDamageBurn || cause == kDsDamageLocust))
      ReplacePlant(x, z, kind, u32(cause));
  }
  if (radioactive) {
    u32 dx = u32(bounds.maximum.x >> kCtMicroCoordinateShift) - u32(bounds.minimum.x >> kCtMicroCoordinateShift);
    u32 dz = u32(bounds.maximum.z >> kCtMicroCoordinateShift) - u32(bounds.minimum.z >> kCtMicroCoordinateShift);
    MarkRadioactivity(x + (dx >> 1), z + (dz >> 1));
  }
  return true;
}

// Mark a clipped radius-twenty region, always accepting the inner radius and randomly thinning the outer band.
void DsController::MarkRadioactivity(u32 x, u32 z) {
  DsRandom area_random(kScRandomSeedFromClock);
  u32 left = x > 20 ? x - 20 : 0, top = z > 20 ? z - 20 : 0;
  u32 columns = city->CellCountX(), rows = city->CellCountZ();
  u32 right = x + 20, bottom = z + 20;
  if (right >= columns)
    right = columns - 1;
  if (bottom >= rows)
    bottom = rows - 1;
  EcLayer *pollution = city->PollutionLayer();
  if (!pollution)
    return;
  for (u32 row = top; row <= bottom; ++row) {
    u32 dz = row - z, squared_z = dz * dz;
    for (u32 column = left; column <= right; ++column) {
      u32 dx = column - x;
      f64 biased = sqrt(f64(i32(squared_z + dx * dx))) + kScDoubleIntegerBias;
      u64 bits;
      memcpy(&bits, &biased, sizeof(bits));
      u32 distance = u32(bits) + kScDoubleIntegerLowWordBias;
      if (distance <= 20 && (distance <= 10 || area_random.Next(10) != 0))
        pollution->SetIsRadioactive(column, row, true);
    }
  }
}

// Create the requested flora kind at density eight or nine, place it at terrain altitude and insert it on the surface.
// Preserve the original high-byte altitude backing and reload the escaped factory output through placement and release.
u8 DsController::ReplacePlant(u32 x, u32 z, u32 kind, u32 state) {
  CtCoord position = {INT32_MAX, INT32_MAX, INT32_MAX};
  u32 density = state == kDsDamageBurn ? 8 : 9;
  GeFlora *flora = city->FloraLayer();
  if (!flora)
    return 0;
  void *created;
  if (!flora->Create(kind, density, IID_ScOccupant, &created))
    return 0;
  position.x = i32(x << kCtMicroCoordinateShift);
  position.z = i32(z << kCtMicroCoordinateShift);
  u8 altitude = u8(state >> 24);
  city->DirtBag()->GetVertexAltitude(x, z, &altitude);
  position.altitude = i32(u32(altitude) << kCtMicroCoordinateShift);
  static_cast<ScOccupant *>(created)->SetLocation(position);
  u8 result = occupants->InsertOccupant(static_cast<ScOccupant *>(created));
  static_cast<ScOccupant *>(created)->Release();
  return result;
}

u32 DsController::GetInfoStringCount(u32) {
  return 0;
}

u8 DsController::GetInfoStrings(u32, u32, u32, u32, u32 *, ScStringView *, ScStringView *) {
  return 0;
}

bool DsController::CreateMapSprite(u32, u32, void **) {
  return false;
}

u32 DsController::GetMapSpriteCount() {
  return 0;
}

bool DsController::GetSpriteIdFromIndex(u32, u32 *) {
  return false;
}

// Allocate the original unsigned32 variant initialized to zero and acquire its returned interface once.
// The sixteen-byte rectangle is an original by-value operand, although this installed body does not inspect it.
ScVariant *DsController::GetVariantValue(ScRect) {
  ScVariant *value = new ScValue(0u, 3u);
  value->AddRef();
  return value;
}

// Original module-wide bit lock3A964 serializes capacity selection across event managers.
static ScSpinLock DsEventStartLock;

i32 DsEventMaximumActive = 1;
u32 DsEventMinimumMonths = 16, DsEventChance = 83;
u32 DsEventMinimumStrength = 25, DsEventMaximumStrength = 85;
// Original max-month, cell-radius and preparedness tunables retain their defaults despite having no live consumers.
static u32 DsEventMaximumMonths = 96, DsEventCellRadiusLow = 2, DsEventCellRadiusMedium = 4;
static u32 DsEventCellRadiusHigh = 6, DsEventPreparednessPercent = 50;

// History and pending replay own distinct32-byte record vectors; original padding and word76 remain absent.
DsEventManager::DsEventManager()
    : initialized(false), automatic_start(0), layer(nullptr), demolition(nullptr), terrain(nullptr), zones(nullptr),
      simulation(nullptr), agent_types(nullptr), city(nullptr), occupants(nullptr), underground_one(nullptr),
      underground_two(nullptr), box(nullptr), active(false), flag_73(0), random(kScRandomSeedFromClock),
      active_count(0), flags_108(0) {
}

DsEventManager::~DsEventManager() {
  DsEventManager::Shutdown();
}

// Publish the actual common or private manager view and retain through the root agent identity.
// All other queries use the original agent fallback, including its unsupported-output behavior.
bool DsEventManager::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_DsManager, DsManager);
  HANDLE_QUERY_INTERFACE(IID_DsEventControl, DsEventControl);
  return DsAgent::QueryInterface(iid, out);
}

u32 DsEventManager::AddRef() {
  return DsAgent::AddRef();
}

u32 DsEventManager::Release() {
  return DsAgent::Release();
}

// Remove the load notification and release retained services in the original non-field order.
// Clear pending before history, publishing the uninitialized state only after every release callback completes.
bool DsEventManager::Shutdown() {
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
u32 DsEventManager::GetRecords(DsEventRecord *records, u32 *count) const {
  if (records) {
    if (*count > history.size)
      *count = history.size;
    for (u32 index = 0; index < *count; ++index)
      memcpy(&records[index], &history.data[index], sizeof(DsEventRecord));
  } else
    *count = history.size;
  return *count;
}

// Report whether history is nonempty and optionally copy its first complete32-byte record.
u8 DsEventManager::GetFirstRecord(DsEventRecord *record) const {
  if (!history.size)
    return 0;
  if (record)
    memcpy(record, history.data, sizeof(DsEventRecord));
  return 1;
}

// The original folded city initializer only shuts down an already-bound manager; fresh binding retains each service.
// Reset activity and mark initialized before registering the load notification so its callback sees the ready manager.
bool DsEventManager::Init(DsLayer *retained_layer, CtCity *retained_city) {
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

bool DsEventManager::StaticShutdown() {
  return true;
}

bool DsEventManager::AttemptDisasterStart() {
  f32 strength = f32(random.InverseSqrtRange(0.0, 1.0));
  return Start(strength, automatic_start);
}

void DsEventManager::SeedHistory() {
  CtDate date(0u, 0u, 0u);
  city->GetSimulationClock()->GetCurrentClockDate(&date);
  DsEventRecord record = {date.serial, 0, 0, 0, 0, 0, 0, 0};
  *history.Append() = record;
}

// Gate automatic attempts by signed capacity, the clamped last history date and the strict thirty-day month delay.
// Capture today's date before seeding empty history, and publish the temporary automatic byte around the virtual trigger.
bool DsEventManager::Tick() {
  if (i32(active_count) < DsEventMaximumActive) {
    CtDate current(0u, 0u, 0u);
    city->GetSimulationClock()->GetCurrentClockDate(&current);
    u32 today = current.serial;
    if (!history.size)
      SeedHistory();
    u32 previous = history.data[history.size - 1].serial;
    if (today < previous)
      previous = today;
    if (today - previous > 30u * DsEventMinimumMonths && random.Next(10000) < DsEventChance) {
      automatic_start = 1;
      AttemptDisasterStart();
      automatic_start = 0;
    }
  }
  return true;
}

// Replace history from its capped archive, then append pending records from the independent live-state archive.
// Close both archives before seeding missing history; partial read failures stop their own loop and do not change success.
bool DsEventManager::Init(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsEventHistoryRecordGroup, 0});
    history.Clear();
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result) {
        if (count > 50000)
          count = 50000;
        for (u32 index = 0; index < count && result; ++index) {
          DsEventRecord record;
          result = stream->ReadBlock(&record, 32) != 0;
          if (result)
            *history.Append() = record;
        }
      }
    }
  }
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsEventInstancesRecordGroup, 0});
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = 0;
      bool result = stream->ReadU32(&count) != 0;
      if (result)
        for (u32 index = 0; index < count && result; ++index) {
          DsEventRecord record;
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
bool DsEventManager::Save(CtCity *input_city, RsDatabaseSegment *segment) {
  {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsEventHistoryRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      u32 count = history.size < 50000 ? history.size : 50000;
      if (stream->WriteU32(count) && count)
        for (u32 index = 0; index < count; ++index) {
          DsEventRecord record = history.data[index];
          stream->WriteBlock(&record, 32);
        }
    }
  }
  if (active_count) {
    DsArchive archive(input_city, segment, {kRsDisasterLayerRecordType, kDsEventInstancesRecordGroup, 0}, 1, 0);
    if (archive.valid) {
      RsStream *stream = archive.Stream();
      if (i32(active_count) > 50000)
        active_count = 50000;
      if (stream->WriteU32(active_count)) {
        DsListIterator *iterator = layer->CreateDisasterListIterator();
        while (DsInstance *instance = iterator->Next())
          if (instance->TypeId() == kDsEventTypeId) {
            DsEventRecord record;
            static_cast<DsEventState *>(static_cast<DsEventInstance *>(instance))->GetRecord(&record);
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
bool DsEventManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CityViewReady) {
    u32 count = pending.size;
    for (u32 index = 0; index < count; ++index) {
      DsEventInstance *instance = new DsEventInstance;
      instance->Init(this, &pending.data[index]);
      ++active_count;
    }
    pending.Clear();
  }
  return true;
}

void DsEventManager::StopInstance(DsInstance *instance) {
  static_cast<DsEventInstance *>(instance)->Shutdown();
}

// Preserve the original zero-to-one fallback before decrementing and clear activity only for the final instance.
void DsEventManager::InstanceEnded() {
  if (!active_count)
    active_count = 1;
  if (active_count-- == 1)
    active = false;
}

void DsEventManager::RecordInstance(DsEventInstance *instance) {
  *history.Append() = instance->state;
}

// Record history before the actual teardown, then publish removal through the current layer without releasing the instance.
void DsEventManager::FinishInstance(DsEventInstance *instance) {
  RecordInstance(instance);
  instance->Shutdown();
  layer->DisasterInstanceChange(instance, 4);
}

u8 DsEventManager::SelectStart(ScVec3I *) {
  return 1;
}

// Read the event tuning words in their original order, including repeated limit and month queries.
// Missing service returns false; missing keys retain existing words and the same live variant serves every query.
bool DsEventManager::StaticInit() {
  ScVariableManager *provider = DsGetPropertyProvider();
  if (!provider)
    return false;
  ScValue value;
  if (provider->GetVariable(kDsEventMaximumActivePropertyId, &value))
    DsEventMaximumActive = i32(value.value.word);
  if (provider->GetVariable(kDsEventMinimumMonthsPropertyId, &value))
    DsEventMinimumMonths = value.value.word;
  if (provider->GetVariable(kDsEventMaximumActivePropertyId, &value))
    DsEventMaximumActive = i32(value.value.word);
  if (provider->GetVariable(kDsEventMinimumMonthsPropertyId, &value))
    DsEventMinimumMonths = value.value.word;
  if (provider->GetVariable(kDsEventMaximumMonthsPropertyId, &value))
    DsEventMaximumMonths = value.value.word;
  if (provider->GetVariable(kDsEventMinimumStrengthPropertyId, &value))
    DsEventMinimumStrength = value.value.word;
  if (provider->GetVariable(kDsEventMaximumStrengthPropertyId, &value))
    DsEventMaximumStrength = value.value.word;
  if (provider->GetVariable(kDsEventChancePropertyId, &value))
    DsEventChance = value.value.word;
  if (provider->GetVariable(kDsEventDemolitionChances0PropertyId, &value))
    DsEventDemolitionChances[0] = value.value.word;
  if (provider->GetVariable(kDsEventDemolitionChances1PropertyId, &value))
    DsEventDemolitionChances[1] = value.value.word;
  if (provider->GetVariable(kDsEventDemolitionChances2PropertyId, &value))
    DsEventDemolitionChances[2] = value.value.word;
  if (provider->GetVariable(kDsEventCellRadiusLowPropertyId, &value))
    DsEventCellRadiusLow = value.value.word;
  if (provider->GetVariable(kDsEventCellRadiusMediumPropertyId, &value))
    DsEventCellRadiusMedium = value.value.word;
  if (provider->GetVariable(kDsEventCellRadiusHighPropertyId, &value))
    DsEventCellRadiusHigh = value.value.word;
  if (provider->GetVariable(kDsEventPreparednessPercentPropertyId, &value))
    DsEventPreparednessPercent = value.value.word;
  if (provider->GetVariable(kDsEventFirstDelayPropertyId, &value))
    DsEventFirstDelay = value.value.word;
  if (provider->GetVariable(kDsEventSecondDelayPropertyId, &value))
    DsEventSecondDelay = value.value.word;
  if (provider->GetVariable(kDsEventThirdDelayPropertyId, &value))
    DsEventThirdDelay = value.value.word;
  if (provider->GetVariable(kDsEventAttackDelayPropertyId, &value))
    DsEventAttackDelay = value.value.word;
  return true;
}

// Recover the signed capacity refusal at F2FF while excluding its original unwritten-position execution.
// SelectStart returns one without stores; the accepted path has no defined coordinates for instance initialization.
u8 DsEventManager::Start(f32, u8) {
  ScLockSpin(&DsEventStartLock);
  assert(i32(active_count) >= DsEventMaximumActive && "Smite selector leaves its required position unwritten");
  ScUnlockSpin(&DsEventStartLock);
  return 0;
}

// Construct the original event-manager product and expose its common manager identity without retaining it.
ScUnknown *DsCreateClass52FE8D53() {
  return static_cast<DsManager *>(new DsEventManager);
}

u32 DsEventFirstDelay = 0, DsEventSecondDelay = 60, DsEventThirdDelay = 120, DsEventAttackDelay = 180;
u32 DsEventDemolitionChances[3] = {1000, 2500, 4000};
static const i8 DsEventPanicLevels[6] = {100, 80, 60, 30, 10, 0};
