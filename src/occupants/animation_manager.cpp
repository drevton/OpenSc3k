#include "occupants/animation_manager.h"

#include <cassert>
#include <cmath>

#include "application/application_core.h"
#include "city/api.h"
#include "common/api.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"

// Animation ticks clamp positive elapsed intervals while retaining negative wrapped words.
enum OcAnimationTickLimit : i32 {
  kOcMaximumAnimationTickElapsed = 0x3d090,
};

// Select the executable's next occupancy bucket count from its original ascending prime table.
static u32 OcAnimationBucketCount(u32 minimum) {
  static const u32 kOcAnimationBucketPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
      196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189,
      805306457, 1610612741, 3221225473u, 4294967291u};
  for (u32 prime : kOcAnimationBucketPrimes)
    if (minimum <= prime)
      return prime;
  return kOcAnimationBucketPrimes[sizeof(kOcAnimationBucketPrimes) / sizeof(kOcAnimationBucketPrimes[0]) - 1];
}

OcAnimationTable::OcAnimationTable(u32 minimum_buckets)
    : buckets(OcAnimationBucketCount(minimum_buckets), nullptr), count(0) {
}

OcAnimationTable::~OcAnimationTable() {
  Clear();
}

// Locate the node for an occupant identity in the original shifted-pointer bucket.
OcAnimationNode *OcAnimationTable::Find(ScOccupant *occupant) const {
  u32 hash = static_cast<u32>(reinterpret_cast<uptr>(occupant)) >> 2;
  for (OcAnimationNode *node = buckets[hash % buckets.size()]; node; node = node->next)
    if (node->info.occupant == occupant)
      return node;
  return nullptr;
}

// Rehash each old bucket from head to tail, inserting each node at the new bucket's head as the executable does.
void OcAnimationTable::Grow(u32 minimum_buckets) {
  if (minimum_buckets <= buckets.size())
    return;
  u32 size = OcAnimationBucketCount(minimum_buckets);
  if (size <= buckets.size())
    return;
  std::vector<OcAnimationNode *> next(size, nullptr);
  for (OcAnimationNode *&head : buckets) {
    while (head) {
      OcAnimationNode *node = head;
      head = node->next;
      u32 hash = static_cast<u32>(reinterpret_cast<uptr>(node->info.occupant)) >> 2;
      u32 bucket = hash % size;
      node->next = next[bucket];
      next[bucket] = node;
    }
  }
  buckets.swap(next);
}

// Grow before checking for an existing identity, then prepend a new occupant to its selected bucket.
OcAnimationNode *OcAnimationTable::Insert(const OcAnimationInfo &entry) {
  Grow(count + 1);
  OcAnimationNode *found = Find(entry.occupant);
  if (found)
    return found;
  u32 hash = static_cast<u32>(reinterpret_cast<uptr>(entry.occupant)) >> 2;
  u32 bucket = hash % static_cast<u32>(buckets.size());
  OcAnimationNode *node = new OcAnimationNode{buckets[bucket], entry};
  buckets[bucket] = node;
  ++count;
  return node;
}

// Remove the specified existing node from its original bucket and retire its node storage.
void OcAnimationTable::Erase(OcAnimationNode *node) {
  u32 hash = static_cast<u32>(reinterpret_cast<uptr>(node->info.occupant)) >> 2;
  OcAnimationNode **link = &buckets[hash % buckets.size()];
  while (*link != node)
    link = &(*link)->next;
  *link = node->next;
  delete node;
  --count;
}

// Remove a matching occupant identity if it is still present in the occupancy table.
void OcAnimationTable::Erase(ScOccupant *occupant) {
  OcAnimationNode *node = Find(occupant);
  if (node)
    Erase(node);
}

// Retire every bucket chain while retaining the selected bucket count for future insertions.
void OcAnimationTable::Clear() {
  for (OcAnimationNode *&head : buckets) {
    while (head) {
      OcAnimationNode *node = head;
      head = node->next;
      delete node;
    }
  }
  count = 0;
}

// Return the first occupied bucket's head in ascending bucket order.
OcAnimationNode *OcAnimationTable::First() const {
  for (OcAnimationNode *head : buckets)
    if (head)
      return head;
  return nullptr;
}

// Continue along the current chain, then find the next occupied bucket in ascending order.
OcAnimationNode *OcAnimationTable::Next(const OcAnimationNode *node) const {
  if (node->next)
    return node->next;
  u32 hash = static_cast<u32>(reinterpret_cast<uptr>(node->info.occupant)) >> 2;
  for (u32 bucket = hash % static_cast<u32>(buckets.size()) + 1; bucket < buckets.size(); ++bucket)
    if (buckets[bucket])
      return buckets[bucket];
  return nullptr;
}

// Construct the original animation-manager interfaces, occupancy containers, tick lists and scheduling state.
// Occupancy storage preserves the executable's pointer hash and ascending-bucket traversal.
OcAnimationManager::OcAnimationManager()
    : initialized(0), references(0), cell_size(0), max_x(0), max_z(0), max_y(0x8000), visibility_test(nullptr),
      default_visibility_test(nullptr), occupants(193), iterating(0), sprite_manager(nullptr), sprite_cell_map(nullptr),
      sender_id(0), last_high(0), last_most_first(0), last_most_second(0), last_few(0), frame_count(0), pause_count(0),
      fps(10.0f), tick_timer(ScStopwatch::Microseconds, 0), step_next_frame(0), current_load(100),
      execution_regular{5, 3, 3, 3, 6, 3, 3, 3, 5, 3, 3, 3, 6, 3, 3, 3},
      execution_critical{1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2, 0}, execution_table(execution_regular),
      addition_count(0), critical_execution_count(0) {
  tick_timer.Restart();
}

OcAnimationManager::~OcAnimationManager() {
}

// Publish the requested original manager, tickable, message-target or tick-manager identity while holding its lock.
bool OcAnimationManager::QueryInterface(u32 iid, void **out) {
  lock.Lock();
  switch (iid) {
  case IID_ScUnknown:
  case IID_OcAnimManager:
    *out = static_cast<OcAnimManager *>(this);
    break;
  case IID_SpTickable:
    *out = static_cast<SpTickable *>(this);
    break;
  case IID_ScMessageTarget:
    *out = static_cast<ScMessageTarget *>(this);
    break;
  case IID_SpTickManager:
    *out = static_cast<SpTickManager *>(this);
    break;
  default:
    lock.Unlock();
    RETURN_QUERY_INTERFACE_FAILURE();
  }
  AddRef();
  lock.Unlock();
  return true;
}

// Increment the shared reference count under the original recursive lock.
u32 OcAnimationManager::AddRef() {
  lock.Lock();
  u32 result = ++references;
  lock.Unlock();
  return result;
}

// Release the lock before deleting the last reference or decrementing the remaining count, as in the executable.
u32 OcAnimationManager::Release() {
  lock.Lock();
  if (references <= 1) {
    lock.Unlock();
    delete this;
    return 0;
  }
  lock.Unlock();
  return --references;
}

// Capture the city's dimensions and sender ID once, then register the animation-manager initialization message.
bool OcAnimationManager::Init(CtCity *city, u32 id) {
  if (!initialized) {
    cell_size = static_cast<u32>(city->GetTileSize());
    max_x = city->CellCountX() - 1;
    max_z = city->CellCountZ() - 1;
    u32 count_y = city->CellCountY();
    initialized = 1;
    max_y = count_y - 1;
    sender_id = id;
    pause_count = 0;
    step_next_frame = 0;
    ApGetMessageServer()->AddNotification(this, kWnMsg_AnimationTickManagerReady);
  }
  return true;
}

// Unregister notifications, remove occupants and receivers, and release retained sprite services once initialized.
bool OcAnimationManager::Shutdown() {
  if (initialized) {
    ApGetMessageServer()->RemoveNotification(this, kWnMsg_AnimationTickManagerReady);
    RemoveAllOccupants();
    UnregisterAllChangeReceivers();
    ScRelease(&sprite_cell_map);
    ScRelease(&sprite_manager);
    pause_count = 0;
    step_next_frame = 0;
    initialized = 0;
  }
  return true;
}

// Attach the manager to the active animation ticker and retain the current sprite services when the city view appears.
bool OcAnimationManager::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_AnimationTickManagerReady) {
    ApGetAnimationTickManager()->AddTickableObject(static_cast<SpTickable *>(this));
    if (!sprite_manager) {
      sprite_manager = ApGetCitySpriteManager();
      if (sprite_manager)
        sprite_manager->AddRef();
    }
    if (!sprite_cell_map) {
      sprite_cell_map = ApGetCitySpriteCellMap();
      if (sprite_cell_map)
        sprite_cell_map->AddRef();
    }
  }
  return true;
}

// Register a new change receiver once, then retain it and optionally post current occupants after releasing the lock.
bool OcAnimationManager::RegisterChangeReceiver(CtChangeReceiver *receiver, u32 *sender, bool post) {
  lock.Lock();
  *sender = sender_id;
  for (CtChangeReceiver *entry : receivers) {
    if (entry == receiver) {
      lock.Unlock();
      return true;
    }
  }
  receivers.push_back(receiver);
  lock.Unlock();
  receiver->AddRef();
  if (post)
    PostOccupantAll(receiver);
  if (receiver->QueryInterface(IID_SpManagerView, reinterpret_cast<void **>(&sprite_manager)) && !sprite_cell_map) {
    sprite_cell_map = ApGetCitySpriteCellMap();
    if (sprite_cell_map)
      sprite_cell_map->AddRef();
  }
  return true;
}

// Remove one registered receiver and release associated sprite services when it exposes the sprite-manager identity.
bool OcAnimationManager::UnregisterChangeReceiver(CtChangeReceiver *receiver) {
  lock.Lock();
  for (auto entry = receivers.begin(); entry != receivers.end(); ++entry) {
    if (*entry != receiver)
      continue;
    receiver->Release();
    receivers.erase(entry);
    SpManager *queried = nullptr;
    if (receiver->QueryInterface(IID_SpManagerView, reinterpret_cast<void **>(&queried))) {
      queried->Release();
      ScRelease(&sprite_manager);
      ScRelease(&sprite_cell_map);
    }
    lock.Unlock();
    return true;
  }
  lock.Unlock();
  return false;
}

// Release each registered receiver while its node remains linked, then clear the complete receiver list.
bool OcAnimationManager::UnregisterAllChangeReceivers() {
  lock.Lock();
  for (CtChangeReceiver *receiver : receivers)
    receiver->Release();
  receivers.clear();
  lock.Unlock();
  return true;
}

// Notify the receiver about each visible occupant in the executable's bucket traversal order while holding the lock.
bool OcAnimationManager::PostOccupantAll(CtChangeReceiver *receiver) {
  lock.Lock();
  for (OcAnimationNode *node = occupants.First(); node; node = occupants.Next(node))
    if ((node->info.visibility & 3) == 3)
      receiver->OccupantInserted(sender_id, node->info.occupant);
  lock.Unlock();
  return true;
}

bool OcAnimationManager::PostOccupantAll(ScMessageTarget *) const {
  return false;
}

OcAnimationIterator::OcAnimationIterator(OcAnimationManager *owner)
    : references(0), manager(owner), test(nullptr), bounds{{-1, -1, -1}, {-1, -1, -1}}, current(nullptr),
      current_table(nullptr) {
  manager->AddRef();
}

// Release the optional test followed by the manager retained by the constructor.
OcAnimationIterator::~OcAnimationIterator() {
  if (test)
    test->Release();
  if (manager)
    manager->Release();
}

bool OcAnimationIterator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, OcIterator);
  HANDLE_QUERY_INTERFACE(IID_OcIterator, OcIterator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 OcAnimationIterator::AddRef() {
  return ++references;
}

// Release an iterator reference and destroy the object when its count reaches zero.
u32 OcAnimationIterator::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

// Record restricted bounds and the supplied test, then reset the original iterator.
bool OcAnimationIterator::Init(const GeBounds *value, OcIteratorTest *filter) {
  if (value &&
      (value->minimum.x > 0 || value->minimum.z > 0 ||
          value->maximum.x < static_cast<i32>(manager->GetAnimCellsInX() - 1) ||
          value->maximum.z < static_cast<i32>(manager->GetAnimCellsInZ() - 1)))
    bounds = *value;
  else
    bounds.minimum.x = -1;
  test = filter;
  Reset();
  return true;
}

bool OcAnimationIterator::Shutdown() {
  ScRelease(&test);
  return true;
}

// Reset to the first occupied bucket and return that occupant without applying the later advance filters.
ScOccupant *OcAnimationIterator::Reset() {
  current_table = &manager->occupants;
  current = current_table->First();
  return current ? current->info.occupant : nullptr;
}

// Return the occupant at the current position without acquiring an additional reference.
ScOccupant *OcAnimationIterator::Current() {
  return current ? current->info.occupant : nullptr;
}

// Advance through the installed bucket order, applying the optional test before the restricted cell bounds.
// The current node must be live: the original reads its successor before testing exhaustion.
ScOccupant *OcAnimationIterator::Next() {
  assert(current);
  for (current = current_table->Next(current); current; current = current_table->Next(current)) {
    ScOccupant *occupant = current->info.occupant;
    if (test && !test->Test(occupant))
      continue;
    if (bounds.minimum.x != -1) {
      u32 x, z;
      occupant->GetLocationInCellCoords(&x, &z);
      if (x < static_cast<u32>(bounds.minimum.x) || x > static_cast<u32>(bounds.maximum.x) ||
          z < static_cast<u32>(bounds.minimum.z) || z > static_cast<u32>(bounds.maximum.z))
        continue;
    }
    return occupant;
  }
  return nullptr;
}

// Construct and retain an animation iterator without initializing its bounds or filter.
bool OcAnimationManager::GetIterator(OcIterator **out) {
  *out = new OcAnimationIterator(this);
  (*out)->AddRef();
  return true;
}

// Construct and retain an iterator, then initialize it with the supplied bounds and filter.
bool OcAnimationManager::GetOccupants(OcIterator **out, const GeBounds *bounds, OcIteratorTest *test) {
  *out = new OcAnimationIterator(this);
  (*out)->AddRef();
  return (*out)->Init(bounds, test);
}

u32 OcAnimationManager::GetCellsInX() {
  return max_x + 1;
}

u32 OcAnimationManager::GetAnimCellsInX() {
  return (max_x + 1) << 8;
}

f32 OcAnimationManager::GetWorldUnitsInX() {
  return static_cast<f32>(cell_size * (max_x + 1));
}

u32 OcAnimationManager::GetCellsInZ() {
  return max_z + 1;
}

u32 OcAnimationManager::GetAnimCellsInZ() {
  return (max_z + 1) << 8;
}

f32 OcAnimationManager::GetWorldUnitsInZ() {
  return static_cast<f32>(cell_size * (max_z + 1));
}

u32 OcAnimationManager::GetCellsInY() {
  return max_y + 1;
}

u32 OcAnimationManager::GetAnimCellsInY() {
  return (max_y + 1) << 8;
}

f32 OcAnimationManager::GetWorldUnitsInY() {
  return static_cast<f32>(max_y + 1) * 3.266f;
}

u32 OcAnimationManager::GetSenderId() const {
  return sender_id;
}

// Read an occupant's current location and forward the original insertion arguments to the coordinate overload.
bool OcAnimationManager::InsertOccupant(ScOccupant *occupant, i32 layer, u32 flags, bool visible) {
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  return InsertOccupant(occupant, position.x, position.z, position.altitude, layer, flags, visible);
}

// Retain a new occupant, establish its visibility, and insert it now or after the current traversal.
bool OcAnimationManager::InsertOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y, i32 layer, u32 flags, bool visible) {
  OcAnimationInfo entry{occupant, 0, 0, 1};
  lock.Lock();
  if (occupants.Find(entry.occupant) != nullptr) {
    lock.Unlock();
    return false;
  }
  occupant->AddRef();
  occupant->SetIsInCityFlag(1);
  entry.visibility = static_cast<u8>(4 | (visible ? 2 : 0));
  if (!visibility_test || visibility_test->Test(occupant)) {
    if (sprite_manager)
      sprite_manager->InsertUnalignedSprite(occupant, x, z, y, layer, flags);
    entry.visibility |= 1;
  }
  entry.flags = static_cast<u8>(flags);
  entry.sort_id = static_cast<u16>(layer);
  if (iterating)
    deferred_additions.push_back(entry);
  else
    occupants.Insert(entry);
  lock.Unlock();
  return true;
}

// Forward removal with the occupant's current coordinates only when that occupant belongs to the set.
void OcAnimationManager::RemoveOccupant(ScOccupant *occupant) {
  OcAnimationInfo key{occupant, 0, 0, 0};
  if (occupants.Find(key.occupant) != nullptr) {
    CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
    occupant->GetLocation(&position);
    RemoveOccupant(occupant, position.x, position.z, position.altitude);
  }
}

// Remove the visible sprite and occupant, deferring the set removal while it is being iterated.
void OcAnimationManager::RemoveOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  if (found != nullptr) {
    if ((found->info.visibility & 3) == 3 && sprite_manager)
      sprite_manager->RemoveUnalignedSprite(occupant, x, z, y);
    occupant->ClearIsInCityFlag(1);
    found->info.visibility &= static_cast<u8>(~4u);
    if (!iterating) {
      occupant->Release();
      occupants.Erase(found);
    } else {
      deferred_removals.push_back(found->info);
    }
  }
  lock.Unlock();
}

// Remove matching occupants in bucket order, retaining the original bounds, filter and deferred-removal behavior.
bool OcAnimationManager::RemoveOccupants(const GeBounds *bounds, OcIteratorTest *test) {
  lock.Lock();
  bool restricted = bounds &&
      (bounds->minimum.x > 0 || bounds->minimum.z > 0 || bounds->maximum.x < static_cast<i32>(GetAnimCellsInX() - 1) ||
          bounds->maximum.z < static_cast<i32>(GetAnimCellsInZ() - 1));
  OcAnimationNode *node = occupants.First();
  while (node) {
    ScOccupant *occupant = node->info.occupant;
    bool matches = true;
    if (restricted) {
      u32 x, z;
      occupant->GetLocationInCellCoords(&x, &z);
      matches = x >= static_cast<u32>(bounds->minimum.x) && x <= static_cast<u32>(bounds->maximum.x) &&
          z >= static_cast<u32>(bounds->minimum.z) && z <= static_cast<u32>(bounds->maximum.z);
    }
    if (!matches || (test && !test->Test(occupant))) {
      node = occupants.Next(node);
      continue;
    }
    if ((node->info.visibility & 3) == 3 && sprite_manager)
      sprite_manager->RemoveUnalignedSprite(occupant);
    occupant->ClearIsInCityFlag(1);
    if (iterating) {
      deferred_removals.push_back(node->info);
      continue;
    }
    occupant->Release();
    OcAnimationNode *next = occupants.Next(node);
    occupants.Erase(node);
    node = next;
  }
  lock.Unlock();
  return true;
}

// Remove every occupant in bucket order, clear deferred changes and retain the allocated bucket array.
void OcAnimationManager::RemoveAllOccupants() {
  lock.Lock();
  iterating = static_cast<i8>(static_cast<u8>(iterating) + 1);
  for (OcAnimationNode *node = occupants.First(); node; node = occupants.Next(node)) {
    ScOccupant *occupant = node->info.occupant;
    if ((node->info.visibility & 3) == 3 && sprite_manager)
      sprite_manager->RemoveUnalignedSprite(occupant);
    occupant->ClearIsInCityFlag(1);
    occupant->Release();
  }
  deferred_additions.clear();
  deferred_removals.clear();
  occupants.Clear();
  iterating = static_cast<i8>(static_cast<u8>(iterating) - 1);
  lock.Unlock();
}

// Reevaluate each occupant in bucket order and update visible sprites when its filter result changes.
void OcAnimationManager::ShowOrHideOccupants(OcIteratorTest *test) {
  lock.Lock();
  if (!test)
    test = visibility_test;
  iterating = static_cast<i8>(static_cast<u8>(iterating) + 1);
  for (OcAnimationNode *node = occupants.First(); node; node = occupants.Next(node)) {
    OcAnimationInfo &entry = node->info;
    bool was_visible = (entry.visibility & 3) == 3;
    if (!test || test->Test(entry.occupant))
      entry.visibility |= 1;
    else
      entry.visibility &= static_cast<u8>(~1u);
    bool is_visible = (entry.visibility & 3) == 3;
    if (was_visible != is_visible && sprite_manager) {
      if (is_visible)
        sprite_manager->InsertUnalignedSprite(entry.occupant, entry.flags, entry.sort_id);
      else
        sprite_manager->RemoveUnalignedSprite(entry.occupant);
    }
  }
  iterating = static_cast<i8>(static_cast<u8>(iterating) - 1);
  DoDeferredAdditionsAndRemovals();
  lock.Unlock();
}

bool OcAnimationManager::MoveOccupant(ScOccupant *occupant, CtCoord *position) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  if (found == nullptr) {
    lock.Unlock();
    return false;
  }
  CtCoord previous{INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&previous);
  occupant->SetLocation(*position);
  bool result = true;
  if ((found->info.visibility & 3) == 3 && sprite_manager)
    result = sprite_manager->MoveUnalignedSprite(
        occupant, previous.x, previous.z, previous.altitude, position->x, position->z, position->altitude);
  lock.Unlock();
  return result;
}

// Notify the sprite manager of a registered visible occupant's supplied old and new coordinates.
bool OcAnimationManager::MoveOccupant(ScOccupant *occupant, i32 old_x, i32 old_z, i32 old_y, i32 x, i32 z, i32 y) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  bool result = found != nullptr;
  if (result && (found->info.visibility & 3) == 3 && sprite_manager)
    result = sprite_manager->MoveUnalignedSprite(occupant, old_x, old_z, old_y, x, z, y);
  lock.Unlock();
  return result;
}

// Refresh a visible occupant at its current location, retaining the sort word when requested.
bool OcAnimationManager::RefreshOccupant(ScOccupant *occupant, i32 flags, void *payload) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  bool result = found != nullptr;
  if (result && (found->info.visibility & 3) == 3 && sprite_manager) {
    CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
    occupant->GetLocation(&position);
    if (flags & 0x80)
      found->info.sort_id = *static_cast<u16 *>(payload);
    result = sprite_manager->ChangeUnalignedSprite(occupant, position.x, position.z, flags, payload);
  }
  lock.Unlock();
  return result;
}

// Refresh a visible occupant at the supplied coordinates, retaining the requested sort word.
bool OcAnimationManager::RefreshOccupant(ScOccupant *occupant, i32 x, i32 z, i32, i32 flags, void *payload) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  bool result = found != nullptr;
  if (result && (found->info.visibility & 3) == 3 && sprite_manager) {
    if (flags & 0x80)
      found->info.sort_id = *static_cast<u16 *>(payload);
    result = sprite_manager->ChangeUnalignedSprite(occupant, x, z, flags, payload);
  }
  lock.Unlock();
  return result;
}

bool OcAnimationManager::ShowOccupant(ScOccupant *occupant, i32 x, i32 z, i32 y, bool visible) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  if (found == nullptr) {
    lock.Unlock();
    return false;
  }
  if (visible) {
    if (found->info.visibility != 3 && (found->info.visibility & 1) && sprite_manager)
      sprite_manager->InsertUnalignedSprite(occupant, x, z, y, 1, 0);
    found->info.visibility |= 2;
  } else {
    if ((found->info.visibility & 3) == 3 && sprite_manager)
      sprite_manager->RemoveUnalignedSprite(occupant, x, z, y);
    found->info.visibility &= static_cast<u8>(~2u);
  }
  lock.Unlock();
  return true;
}

// Read the live occupant position and forward the visibility request to the coordinate overload.
bool OcAnimationManager::ShowOccupant(ScOccupant *occupant, bool visible) {
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  return ShowOccupant(occupant, position.x, position.z, position.altitude, visible);
}

// Replace the retained visibility test under the lock, then optionally update occupants after unlocking.
bool OcAnimationManager::SetTest(OcIteratorTest *test, bool update) {
  lock.Lock();
  if (test)
    test->AddRef();
  if (visibility_test)
    visibility_test->Release();
  visibility_test = test;
  lock.Unlock();
  if (update)
    ShowOrHideOccupants(visibility_test);
  return true;
}

// Check both original visibility bits for a registered occupant under the recursive lock.
bool OcAnimationManager::IsOccupantVisible(ScOccupant *occupant) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  bool result = found != nullptr && (found->info.visibility & 3) == 3;
  lock.Unlock();
  return result;
}

bool OcAnimationManager::IsOccupantVisible(ScOccupant *occupant, bool check_occlusion) {
  CtCoord position{INT32_MAX, INT32_MAX, INT32_MAX};
  occupant->GetLocation(&position);
  if (!sprite_cell_map)
    return false;
  SpSprite *sprite = occupant->GetSpriteInst();
  return sprite_cell_map->IsDynamicSpriteVisible(sprite, position.x, position.z, check_occlusion);
}

// Set or clear the tick bit on an existing occupant while retaining its identity and other flags.
void OcAnimationManager::SetShouldTick(ScOccupant *occupant, bool enabled) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  if (found != nullptr) {
    if (enabled)
      found->info.visibility |= 4;
    else
      found->info.visibility &= static_cast<u8>(~4u);
  }
  lock.Unlock();
}

// Read the original per-occupant tick bit, returning false when the occupant is absent.
bool OcAnimationManager::GetShouldTick(ScOccupant *occupant) {
  lock.Lock();
  OcAnimationInfo key{occupant, 0, 0, 0};
  auto found = occupants.Find(key.occupant);
  bool result = found != nullptr && (found->info.visibility & 4) != 0;
  lock.Unlock();
  return result;
}

// Apply queued additions before queued removals after the outermost occupant traversal completes.
void OcAnimationManager::DoDeferredAdditionsAndRemovals() {
  if (iterating > 0)
    return;
  for (const OcAnimationInfo &entry : deferred_additions)
    occupants.Insert(entry);
  deferred_additions.clear();
  for (const OcAnimationInfo &entry : deferred_removals) {
    entry.occupant->Release();
    occupants.Erase(entry.occupant);
  }
  deferred_removals.clear();
}

bool OcAnimationManager::IsInBoundsCell(u32 x, u32 z) {
  return (x << 8) <= max_x && (z << 8) <= max_z;
}

bool OcAnimationManager::IsInBoundsAnimCell(u32 x, u32 z) {
  return x <= max_x && z <= max_z;
}

// Truncate each world coordinate as the original x87 conversion does, then compare its low word to the inclusive maxima.
bool OcAnimationManager::IsInBoundsWorld(f32 x, f32 z) {
  u32 first = 0, second = 0;
  if (std::isfinite(x) && static_cast<f64>(x) >= -9223372036854775808.0 && static_cast<f64>(x) < 9223372036854775808.0)
    first = static_cast<u32>(static_cast<i64>(x));
  if (std::isfinite(z) && static_cast<f64>(z) >= -9223372036854775808.0 && static_cast<f64>(z) < 9223372036854775808.0)
    second = static_cast<u32>(static_cast<i64>(z));
  return first <= max_x && second <= max_z;
}

bool OcAnimationManager::IsInBounds(i32 x, i32 z, i32 y) {
  return static_cast<u32>(x) <= max_x && static_cast<u32>(z) <= max_z && static_cast<u32>(y) <= max_y;
}

bool OcAnimationManager::IsInBounds(const CtCoord &position) {
  return static_cast<u32>(position.x >> 8) <= max_x && static_cast<u32>(position.z >> 8) <= max_z &&
      static_cast<u32>(position.altitude >> 8) <= max_y;
}

bool OcAnimationManager::GetOccupantsInVolume(const GeBounds &bounds, ScOccupant **out, u32 *count) {
  return ApGetCurrentCityView()->GetOccupantsInVolume(bounds, out, count, false, true);
}

// Obtain a tickable's live priority, append it to the corresponding original queue, and update the addition counter.
bool OcAnimationManager::AddTickableObject(SpTickable *object) {
  lock.Lock();
  u8 priority = 0;
  i32 interval = 0;
  object->GetAnimationTickPriority(&priority, &interval);
  OcAnimationTickEntry entry{object, priority, static_cast<u8>(interval)};
  if (priority == 255) {
    critical_execution_count = static_cast<i32>(static_cast<u32>(critical_execution_count) + 1);
    execution_table = execution_critical;
  }
  if (priority >= 250)
    tick_high.push_back(entry);
  else if (priority >= 64)
    if (addition_count & 1)
      tick_most_first.push_back(entry);
    else
      tick_most_second.push_back(entry);
  else
    tick_few.push_back(entry);
  ++addition_count;
  lock.Unlock();
  return true;
}

// Remove the first matching tickable in the executable's queue-search order, preserving its critical-priority update.
bool OcAnimationManager::RemoveTickableObject(SpTickable *object) {
  lock.Lock();
  std::list<OcAnimationTickEntry> *queues[] = {
      &tick_most_first, &tick_most_second, &tick_few, &tick_high, &tick_disabled};
  for (auto *queue : queues) {
    for (auto entry = queue->begin(); entry != queue->end(); ++entry) {
      if (entry->object != object)
        continue;
      u8 priority = entry->priority;
      queue->erase(entry);
      if (priority == 255) {
        critical_execution_count = static_cast<i32>(static_cast<u32>(critical_execution_count) - 1);
        if (critical_execution_count <= 0)
          critical_execution_count = 0;
        execution_table = execution_regular;
      }
      lock.Unlock();
      return true;
    }
  }
  lock.Unlock();
  return true;
}

void OcAnimationManager::SetNewPriority(SpTickable *object, u8, i32) {
  RemoveTickableObject(object);
  AddTickableObject(object);
}

u8 OcAnimationManager::GetActualAnimationSystemLoad() {
  f32 capped = fps > 40.0f ? 40.0f : fps;
  return static_cast<u8>(static_cast<i64>(255.0f - capped * 6.375f));
}

// Run one original tick list while capturing the next node before each callback.
static void OcTickEntries(std::list<OcAnimationTickEntry> *entries, u32 elapsed) {
  for (auto position = entries->begin(); position != entries->end();) {
    SpTickable *object = position->object;
    ++position;
    object->DoAnimationTick(elapsed);
  }
}

static u32 OcTickElapsed(u32 now, u32 before) {
  i32 elapsed = static_cast<i32>(now - before);
  return static_cast<u32>(elapsed < kOcMaximumAnimationTickElapsed ? elapsed : kOcMaximumAnimationTickElapsed);
}

// Tick enabled occupants in the executable's bucket order, then apply changes deferred during their callbacks.
void OcAnimationManager::DoAnimationTick(u32 elapsed) {
  lock.Lock();
  iterating = static_cast<i8>(static_cast<u8>(iterating) + 1);
  for (OcAnimationNode *node = occupants.First(); node; node = occupants.Next(node))
    if (node->info.visibility & 4)
      node->info.occupant->Tick(elapsed);
  iterating = static_cast<i8>(static_cast<u8>(iterating) - 1);
  DoDeferredAdditionsAndRemovals();
  lock.Unlock();
}

// Update the animation clock and smoothed load, then tick the four priority queues selected for this frame.
// Paused calls still advance timing; a pending step permits exactly one scheduled frame.
void OcAnimationManager::DoAnimationTick() {
  u32 previous = last_high, frame = frame_count & 15;
  lock.Lock();
  last_high = tick_timer.GetElapsedTime();
  u32 old_frame = frame_count++;
  if (!(old_frame & 3)) {
    u32 duration = last_high - previous + 3;
    fps = static_cast<f32>((1000000.0 / static_cast<f64>(duration) - fps) * 0.125 + fps);
    if (!frame)
      current_load = GetActualAnimationSystemLoad();
  }
  if (!pause_count || step_next_frame) {
    step_next_frame = 0;
    OcTickEntries(&tick_high, OcTickElapsed(last_high, previous));
    if (execution_table[frame] & 1) {
      OcTickEntries(&tick_most_first, OcTickElapsed(last_high, last_most_first));
      last_most_first = last_high;
    }
    if (execution_table[frame] & 2) {
      OcTickEntries(&tick_most_second, OcTickElapsed(last_high, last_most_second));
      last_most_second = last_high;
    }
    if (execution_table[frame] & 4) {
      OcTickEntries(&tick_few, OcTickElapsed(last_high, last_few));
      last_few = last_high;
    }
  }
  lock.Unlock();
}

u8 OcAnimationManager::GetAnimationSystemLoad() const {
  return current_load;
}

// Increment or decrement the wrapping signed pause counter and return its complete word.
u32 OcAnimationManager::PauseAnimationSystem(bool pause) {
  u32 count = static_cast<u32>(pause_count);
  count = pause ? count + 1 : count - 1;
  pause_count = static_cast<i32>(count);
  return count;
}

bool OcAnimationManager::IsAnimationSystemPaused() const {
  return pause_count > 0;
}

void OcAnimationManager::StepAnimationSystem() {
  step_next_frame = 1;
}

u32 OcAnimationManager::GetCurrentTickTime() const {
  return last_high;
}

void OcAnimationManager::SetCriticalExecutionPriority(bool critical) {
  u32 count = static_cast<u32>(critical_execution_count);
  critical_execution_count = static_cast<i32>(critical ? count + 1 : count - 1);
}

void OcAnimationManager::GetAnimationTickPriority(u8 *priority, i32 *interval) {
  *priority = 250;
  *interval = 4;
}
