#include "common/message_queue.h"

#include "common/services_module.h"

// Blocking message retrieval waits at the original one-second polling interval.
enum ScQueueWaitTiming : u32 {
  kScQueueBlockingWaitInterval = 0xf4240,
};

// The service constructor supplies this original startup ordering priority.
enum ScMessageServiceOrder : i32 {
  kScMessageServicePriority = 0x4c4b40,
  kScQueueServicePriority = 0x4c4b40,
};

static u32 ScQueueCount;

ScQueueList::ScQueueList() : head(new ScQueueNode) {
  head->next = head->previous = head;
}

// Destroy every remaining message node and then the owned sentinel.
ScQueueList::~ScQueueList() {
  while (head->next != head)
    Erase(head->next);
  delete head;
}

// Count the current circular list without taking the queue's lock.
u32 ScQueueList::Count() const {
  u32 count = 0;
  for (ScQueueNode *node = head->next; node != head; node = node->next)
    ++count;
  return count;
}

void ScQueueList::InsertBefore(ScQueueNode *position, const ScMessage &message) {
  ScQueueNode *node = new ScQueueNode{position, position->previous, message};
  node->previous->next = node;
  position->previous = node;
}

// Unlink and destroy one owned node while leaving neighboring messages in place.
void ScQueueList::Erase(ScQueueNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

static bool ScQueueEqual(const ScMessage &first, const ScMessage &second) {
  return first.type == second.type && first.data1 == second.data1 && first.data2 == second.data2 &&
      first.data3 == second.data3;
}

ScQueue::ScQueue() : signal(0), references(0) {
  ++ScQueueCount;
}

ScQueue::~ScQueue() {
  --ScQueueCount;
}

// Publish the queue or base identity and retain it; unsupported queries clear the output.
bool ScQueue::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScMessageQueue);
  HANDLE_QUERY_INTERFACE(IID_ScMessageQueue, ScMessageQueue);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScQueue::AddRef() {
  return ++references;
}

// Release one reference and destroy the queue when the decremented count is zero.
u32 ScQueue::Release() {
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

// Append a copied message under the queue lock and signal only the empty-to-one transition.
void ScQueue::PushBack(const ScMessage &message) {
  lock.Lock();
  entries.InsertBefore(entries.head, message);
  if (entries.Count() == 1)
    signal.Signal();
  lock.Unlock();
}

// Prepend a copied message under the queue lock and signal only the empty-to-one transition.
void ScQueue::PushFront(const ScMessage &message) {
  lock.Lock();
  entries.InsertBefore(entries.head->next, message);
  if (entries.Count() == 1)
    signal.Signal();
  lock.Unlock();
}

bool ScQueue::RemoveFirstFound(const ScMessage &message) {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  while (node != entries.head && !ScQueueEqual(node->message, message))
    node = node->next;
  bool found = node != entries.head;
  if (found)
    entries.Erase(node);
  lock.Unlock();
  return found;
}

// Remove the first message whose type matches the complete requested word.
bool ScQueue::RemoveFirstFound(ScMsgCode type) {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  while (node != entries.head && node->message.type != type)
    node = node->next;
  bool found = node != entries.head;
  if (found)
    entries.Erase(node);
  lock.Unlock();
  return found;
}

bool ScQueue::RemoveLastFound(const ScMessage &message) {
  lock.Lock();
  ScQueueNode *node = entries.head->previous;
  while (node != entries.head && !ScQueueEqual(node->message, message))
    node = node->previous;
  bool found = node != entries.head;
  if (found)
    entries.Erase(node);
  lock.Unlock();
  return found;
}

// Remove the last message whose type matches the complete requested word.
bool ScQueue::RemoveLastFound(ScMsgCode type) {
  lock.Lock();
  ScQueueNode *node = entries.head->previous;
  while (node != entries.head && node->message.type != type)
    node = node->previous;
  bool found = node != entries.head;
  if (found)
    entries.Erase(node);
  lock.Unlock();
  return found;
}

// Remove every complete message match and return the number removed.
u32 ScQueue::RemoveAllFound(const ScMessage &message) {
  lock.Lock();
  u32 count = 0;
  for (ScQueueNode *node = entries.head->next; node != entries.head;) {
    ScQueueNode *next = node->next;
    if (ScQueueEqual(node->message, message)) {
      ++count;
      entries.Erase(node);
    }
    node = next;
  }
  lock.Unlock();
  return count;
}

// Remove every message of the requested type and return the number removed.
u32 ScQueue::RemoveAllFound(ScMsgCode type) {
  lock.Lock();
  u32 count = 0;
  for (ScQueueNode *node = entries.head->next; node != entries.head;) {
    ScQueueNode *next = node->next;
    if (node->message.type == type) {
      ++count;
      entries.Erase(node);
    }
    node = next;
  }
  lock.Unlock();
  return count;
}

u32 ScQueue::RemoveAll() {
  lock.Lock();
  u32 count = entries.Count();
  while (entries.head->next != entries.head)
    entries.Erase(entries.head->next);
  lock.Unlock();
  return count;
}

ScMsgCode ScQueue::PeekFrontType() {
  lock.Lock();
  ScMsgCode result = entries.head->next == entries.head ? 0 : entries.head->next->message.type;
  lock.Unlock();
  return result;
}

ScMsgCode ScQueue::PeekBackType() {
  lock.Lock();
  ScMsgCode result = entries.head->next == entries.head ? 0 : entries.head->previous->message.type;
  lock.Unlock();
  return result;
}

// Search the queue for the requested type without modifying its contents.
bool ScQueue::PeekAnywhereType(ScMsgCode type) {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  while (node != entries.head && node->message.type != type)
    node = node->next;
  bool found = node != entries.head;
  lock.Unlock();
  return found;
}

// Return the borrowed front message pointer after releasing the queue lock, or null when empty.
ScMessage *ScQueue::PeekFrontMessage() {
  lock.Lock();
  ScMessage *result = entries.head->next == entries.head ? nullptr : &entries.head->next->message;
  lock.Unlock();
  return result;
}

// Return the borrowed back message pointer after releasing the queue lock, or null when empty.
ScMessage *ScQueue::PeekBackMessage() {
  lock.Lock();
  ScMessage *result = entries.head->next == entries.head ? nullptr : &entries.head->previous->message;
  lock.Unlock();
  return result;
}

// Search for a complete message match without modifying the queue.
bool ScQueue::PeekAnywhereMessage(const ScMessage &message) {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  while (node != entries.head && !ScQueueEqual(node->message, message))
    node = node->next;
  bool found = node != entries.head;
  lock.Unlock();
  return found;
}

ScMsgCode ScQueue::GetFrontType() {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  ScMsgCode result = 0;
  if (node != entries.head) {
    result = node->message.type;
    entries.Erase(node);
  }
  lock.Unlock();
  return result;
}

ScMsgCode ScQueue::GetBackType() {
  lock.Lock();
  ScQueueNode *node = entries.head->previous;
  ScMsgCode result = 0;
  if (node != entries.head) {
    result = node->message.type;
    entries.Erase(node);
  }
  lock.Unlock();
  return result;
}

bool ScQueue::GetAnywhereType(ScMsgCode type) {
  return RemoveFirstFound(type);
}

// Copy and remove the front message; an empty queue leaves the output untouched.
bool ScQueue::GetFrontMessage(ScMessage *out) {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  bool found = node != entries.head;
  if (found) {
    *out = node->message;
    entries.Erase(node);
  }
  lock.Unlock();
  return found;
}

// Copy and remove the back message; an empty queue leaves the output untouched.
bool ScQueue::GetBackMessage(ScMessage *out) {
  lock.Lock();
  ScQueueNode *node = entries.head->previous;
  bool found = node != entries.head;
  if (found) {
    *out = node->message;
    entries.Erase(node);
  }
  lock.Unlock();
  return found;
}

// Remove the first complete message match and report whether one existed.
bool ScQueue::GetAnywhereMessage(const ScMessage &message) {
  lock.Lock();
  ScQueueNode *node = entries.head->next;
  while (node != entries.head && !ScQueueEqual(node->message, message))
    node = node->next;
  bool found = node != entries.head;
  if (found)
    entries.Erase(node);
  lock.Unlock();
  return found;
}

// Repeatedly remove front types until a nonzero type is obtained, waiting one second between attempts.
ScMsgCode ScQueue::GetFrontTypeBlock() {
  ScMsgCode result = GetFrontType();
  while (!result) {
    signal.TimedWait(kScQueueBlockingWaitInterval);
    result = GetFrontType();
  }
  return result;
}

// Repeatedly remove back types until a nonzero type is obtained, waiting one second between attempts.
ScMsgCode ScQueue::GetBackTypeBlock() {
  ScMsgCode result = GetBackType();
  while (!result) {
    signal.TimedWait(kScQueueBlockingWaitInterval);
    result = GetBackType();
  }
  return result;
}

// Wait between live front-message attempts until one writes the caller's output.
bool ScQueue::GetFrontMessageBlock(ScMessage *out) {
  while (!GetFrontMessage(out))
    signal.TimedWait(kScQueueBlockingWaitInterval);
  return true;
}

// Wait between live back-message attempts until one writes the caller's output.
bool ScQueue::GetBackMessageBlock(ScMessage *out) {
  while (!GetBackMessage(out))
    signal.TimedWait(kScQueueBlockingWaitInterval);
  return true;
}

u32 ScQueue::GetMessageCount() {
  return entries.Count();
}

// The installed replay guard is shared by all message-server instances in this module.
static bool ScReplayingNotifications;

// Construct the queue and targeted-post list after the three independent critical sections.
// Notification storage begins empty and the pending replay depth starts at zero.
ScMessageService::ScMessageService()
    : ScServicesBase(kScMessageServerServiceId, kScMessageServicePriority), notification_depth(0), types(nullptr),
      pending_first(nullptr), pending_last(nullptr), posts_remaining(0) {
  posts.next = posts.previous = &posts;
}

// Delete the owned targeted-post nodes without retaining or releasing their borrowed target pointers.
ScMessageService::~ScMessageService() {
  notifications_lock.Lock();
  notification_depth = 0;
  while (types) {
    ScNotificationType *type = types;
    types = type->next;
    while (type->first) {
      ScNotificationTarget *target = type->first;
      type->first = target->next;
      delete target;
    }
    delete type;
  }
  notifications_lock.Unlock();
  while (pending_first) {
    ScPendingNotification *pending = pending_first;
    pending_first = pending->next;
    delete pending;
  }
  while (posts.next != &posts) {
    ScTargetPost *post = posts.next;
    posts.next = post->next;
    delete post;
  }
}

bool ScMessageService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageServer, ScMessageServer);
  return ScServicesBase::QueryInterface(iid, out);
}

u32 ScMessageService::AddRef() {
  return ScServicesBase::AddRef();
}

u32 ScMessageService::Release() {
  return ScServicesBase::Release();
}

bool ScMessageService::Init() {
  return ScServicesFramework()->AddToTick(this);
}

bool ScMessageService::Shutdown() {
  return ScServicesFramework()->RemoveFromTick(this);
}

// Find the registration list associated with a message type; its targets retain their insertion order.
ScNotificationType *ScMessageService::FindType(ScMsgCode type) {
  for (ScNotificationType *entry = types; entry; entry = entry->next)
    if (entry->type == type)
      return entry;
  return nullptr;
}

// Insert a target only once for its type. Deferred additions already own their pending retain.
void ScMessageService::AddDirect(ScMessageTarget *target, ScMsgCode type, bool retain) {
  ScNotificationType *entry = FindType(type);
  if (!entry) {
    entry = new ScNotificationType{types, type, nullptr, nullptr};
    types = entry;
  }
  for (ScNotificationTarget *item = entry->first; item; item = item->next)
    if (item->target == target)
      return;
  ScNotificationTarget *item = new ScNotificationTarget{nullptr, target};
  if (entry->last)
    entry->last->next = item;
  else
    entry->first = item;
  entry->last = item;
  if (retain)
    target->AddRef();
}

// Erase a matching target before releasing it, then remove an empty type list.
void ScMessageService::RemoveDirect(ScMessageTarget *target, ScMsgCode type) {
  ScNotificationType **position = &types;
  while (*position && (*position)->type != type)
    position = &(*position)->next;
  ScNotificationType *entry = *position;
  if (!entry)
    return;
  ScNotificationTarget **item = &entry->first, *previous = nullptr;
  while (*item && (*item)->target != target) {
    previous = *item;
    item = &(*item)->next;
  }
  if (!*item)
    return;
  ScNotificationTarget *removed = *item;
  *item = removed->next;
  if (entry->last == removed)
    entry->last = previous;
  delete removed;
  target->Release();
  position = &types;
  while (*position && *position != entry)
    position = &(*position)->next;
  if (*position && !entry->first) {
    *position = entry->next;
    delete entry;
  }
}

// Append a reentrant registration change and retain targets only for deferred additions.
void ScMessageService::Defer(ScMessageTarget *target, ScMsgCode type, bool add) {
  ScPendingNotification *pending = new ScPendingNotification{nullptr, target, type, add};
  if (pending_last)
    pending_last->next = pending;
  else
    pending_first = pending;
  pending_last = pending;
  if (add)
    target->AddRef();
}

// Apply pending changes directly before a send; an addition transfers its already-acquired reference.
void ScMessageService::ApplyPending() {
  for (ScPendingNotification *pending = pending_first; pending; pending = pending->next) {
    if (pending->add)
      AddDirect(pending->target, pending->type, false);
    else
      RemoveDirect(pending->target, pending->type);
  }
  while (pending_first) {
    ScPendingNotification *pending = pending_first;
    pending_first = pending->next;
    delete pending;
  }
  pending_last = nullptr;
}

// Replay deferred changes through the live virtual interface and balance the temporary addition retain.
void ScMessageService::ReplayPending() {
  notifications_lock.Lock();
  if (!ScReplayingNotifications) {
    ScReplayingNotifications = true;
    if (!notification_depth) {
      while (pending_first) {
        ScPendingNotification *pending = pending_first;
        if (pending->add) {
          AddNotification(pending->target, pending->type);
          pending->target->Release();
        } else
          RemoveNotification(pending->target, pending->type);
        pending_first = pending->next;
        if (!pending_first)
          pending_last = nullptr;
        delete pending;
      }
    }
    ScReplayingNotifications = false;
  }
  notifications_lock.Unlock();
}

// Retain the registered target snapshot under the notification lock, then dispatch and release it outside the lock.
bool ScMessageService::MessageSend(const ScMessage &message) {
  ScMessageTarget *targets[514], **next = targets;
  notifications_lock.Lock();
  ApplyPending();
  ++notification_depth;
  ScNotificationType *entry = message.type ? FindType(message.type) : nullptr;
  if (entry) {
    for (ScNotificationTarget *item = entry->first; item; item = item->next) {
      *next++ = item->target;
      item->target->AddRef();
    }
  }
  *next = nullptr;
  --notification_depth;
  notifications_lock.Unlock();
  for (ScMessageTarget **item = targets; *item; ++item) {
    (*item)->DoMessage(const_cast<ScMessage *>(&message));
    (*item)->Release();
  }
  return true;
}

// Register a target once for the type, deferring reentrant changes until the current operation completes.
bool ScMessageService::AddNotification(ScMessageTarget *target, ScMsgCode type) {
  notifications_lock.Lock();
  if (notification_depth)
    Defer(target, type, true);
  else {
    ++notification_depth;
    AddDirect(target, type, true);
    --notification_depth;
    ReplayPending();
  }
  notifications_lock.Unlock();
  return true;
}

// Remove a registered target for the type, or defer the change while a registration operation is active.
bool ScMessageService::RemoveNotification(ScMessageTarget *target, ScMsgCode type) {
  notifications_lock.Lock();
  if (notification_depth)
    Defer(target, type, false);
  else {
    ++notification_depth;
    RemoveDirect(target, type);
    --notification_depth;
    ReplayPending();
  }
  notifications_lock.Unlock();
  return true;
}

bool ScMessageService::MessagePost(const ScMessage &message, bool high_priority) {
  queue_lock.Lock();
  if (high_priority)
    queue.PushFront(message);
  else
    queue.PushBack(message);
  queue_lock.Unlock();
  return true;
}

u32 ScMessageService::GetReferenceCountToMessageTarget(ScMessageTarget *) {
  return 0;
}

u32 ScMessageService::GetReferenceCountToMessageType(ScMsgCode) {
  return 0;
}

u32 ScMessageService::GetTotalReferenceCount() {
  return 0;
}

// Append a copy of the message and its borrowed target to the targeted-post list under its own lock.
void ScMessageService::GeneralMessagePostToTarget(const ScMessage &message, ScMessageTarget *target) {
  posts_lock.Lock();
  ScTargetPost *post = new ScTargetPost;
  post->target = target;
  post->message = message;
  post->next = &posts;
  post->previous = posts.previous;
  posts.previous->next = post;
  posts.previous = post;
  posts_lock.Unlock();
}

// Remove every pending targeted post for the supplied target without releasing the borrowed pointer.
void ScMessageService::CancelGeneralMessagePostsToTarget(ScMessageTarget *target) {
  posts_lock.Lock();
  for (ScTargetPost *post = posts.next; post != &posts;) {
    ScTargetPost *next = post->next;
    if (post->target == target) {
      post->previous->next = post->next;
      post->next->previous = post->previous;
      delete post;
    }
    post = next;
  }
  posts_lock.Unlock();
}

// Drain only the captured number of targeted posts, unlocking the nested list lock around each live callback.
void ScMessageService::DispatchPosts() {
  posts_lock.Lock();
  if (posts_remaining <= 0) {
    posts_remaining = 0;
    for (ScTargetPost *post = posts.next; post != &posts; post = post->next)
      ++posts_remaining;
  }
  while (posts_remaining-- > 0) {
    if (posts.next != &posts) {
      ScTargetPost *post = posts.next;
      ScMessageTarget *target = post->target;
      ScMessage message = post->message;
      post->previous->next = post->next;
      post->next->previous = post->previous;
      delete post;
      posts_lock.Unlock();
      target->DoMessage(&message);
      posts_lock.Lock();
    }
  }
  posts_lock.Unlock();
}

// Drain broadcast messages before dispatching the current batch of targeted posts.
// The primary service tick and adjusted message-server tick both reach this method.
bool ScMessageService::OnTick(u32) {
  ScMessage message;
  for (;;) {
    queue_lock.Lock();
    bool found = queue.GetFrontMessage(&message);
    queue_lock.Unlock();
    if (!found)
      break;
    MessageSend(message);
  }
  posts_lock.Lock();
  if (posts.next != &posts)
    DispatchPosts();
  posts_lock.Unlock();
  return true;
}

// Count queued broadcast messages while holding the separate service queue lock.
u32 ScMessageService::GetMessageQueueSize() {
  queue_lock.Lock();
  u32 count = queue.GetMessageCount();
  queue_lock.Unlock();
  return count;
}

ScQueueService::ScQueueService() : ScServicesBase(kScQueueManagerServiceId, kScQueueServicePriority) {
}

ScQueueService::~ScQueueService() {
}

bool ScQueueService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScQueueManager, ScQueueManager);
  return ScServicesBase::QueryInterface(iid, out);
}

u32 ScQueueService::AddRef() {
  return ScServicesBase::AddRef();
}

u32 ScQueueService::Release() {
  return ScServicesBase::Release();
}

// Look up the borrowed queue through the original inserting subscript and publish null on a miss.
bool ScQueueService::GetQueue(ScQueueKey key, ScMessageQueue **out) {
  lock.Lock();
  *out = queues[key];
  bool found = *out != nullptr;
  lock.Unlock();
  return found;
}

// Create and retain a new queue only when the key is absent from the ordered map.
bool ScQueueService::AddQueue(ScQueueKey key) {
  lock.Lock();
  bool added = false;
  if (queues.find(key) == queues.end()) {
    ScQueue *queue = new ScQueue;
    queue->AddRef();
    queues.insert({key, queue});
    added = true;
  }
  lock.Unlock();
  return added;
}

// Insert the supplied queue pointer only for a missing key, without retaining it.
bool ScQueueService::AddQueue(ScQueueKey key, ScMessageQueue *queue) {
  lock.Lock();
  bool added = queues.find(key) == queues.end();
  if (added)
    queues.insert({key, queue});
  lock.Unlock();
  return added;
}

// Release the selected queue before erasing its captured key when requested.
bool ScQueueService::RemoveQueue(ScQueueKey key, bool release) {
  lock.Lock();
  auto item = queues.find(key);
  bool found = item != queues.end();
  if (found) {
    if (release)
      item->second->Release();
    queues.erase(item);
  }
  lock.Unlock();
  return found;
}

// Erase the first matching queue pointer in key order, then optionally release that pointer.
bool ScQueueService::RemoveQueue(ScMessageQueue *queue, bool release) {
  lock.Lock();
  auto item = queues.begin();
  while (item != queues.end() && item->second != queue)
    ++item;
  bool found = item != queues.end();
  if (found) {
    queues.erase(item);
    if (release)
      queue->Release();
  }
  lock.Unlock();
  return found;
}

// Erase queues in key order and optionally release each captured pointer after its node is gone.
bool ScQueueService::RemoveAllQueues(bool release) {
  lock.Lock();
  auto item = queues.begin();
  while (item != queues.end()) {
    auto current = item++;
    ScMessageQueue *queue = current->second;
    queues.erase(current);
    if (release)
      queue->Release();
  }
  lock.Unlock();
  return true;
}

u32 ScQueueService::GetQueueCount() {
  return (u32)queues.size();
}
