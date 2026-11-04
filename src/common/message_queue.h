#ifndef SC3_COMMON_MESSAGE_QUEUE_H
#define SC3_COMMON_MESSAGE_QUEUE_H

#include <map>

#include "common/api.h"
#include "common/sc3_thread_signal.h"
#include "common/services_module.h"
#include "common/value.h"

struct ScQueueNode {
  ScQueueNode *next, *previous;
  ScMessage message;
};

// The original message list owns its sentinel and nodes; destroying it does not dispatch to message targets.
struct ScQueueList {
  ScQueueList();
  ~ScQueueList();
  u32 Count() const;
  void InsertBefore(ScQueueNode *position, const ScMessage &message);
  void Erase(ScQueueNode *node);
  ScQueueNode *head;
};

// GZServiceD message queue: an owned sentinel list, recursive lock, embedded signal and direct reference count.
class ScQueue : public ScMessageQueue {
public:
  ScQueue();
  virtual ~ScQueue();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  void PushBack(const ScMessage &message) override;
  void PushFront(const ScMessage &message) override;
  bool RemoveFirstFound(const ScMessage &message) override;
  bool RemoveFirstFound(ScMsgCode type) override;
  bool RemoveLastFound(const ScMessage &message) override;
  bool RemoveLastFound(ScMsgCode type) override;
  u32 RemoveAllFound(const ScMessage &message) override;
  u32 RemoveAllFound(ScMsgCode type) override;
  u32 RemoveAll() override;
  ScMsgCode PeekFrontType() override;
  ScMsgCode PeekBackType() override;
  bool PeekAnywhereType(ScMsgCode type) override;
  ScMessage *PeekFrontMessage() override;
  ScMessage *PeekBackMessage() override;
  bool PeekAnywhereMessage(const ScMessage &message) override;
  ScMsgCode GetFrontType() override;
  ScMsgCode GetBackType() override;
  bool GetAnywhereType(ScMsgCode type) override;
  bool GetFrontMessage(ScMessage *out) override;
  bool GetBackMessage(ScMessage *out) override;
  bool GetAnywhereMessage(const ScMessage &message) override;
  ScMsgCode GetFrontTypeBlock() override;
  ScMsgCode GetBackTypeBlock() override;
  bool GetFrontMessageBlock(ScMessage *out) override;
  bool GetBackMessageBlock(ScMessage *out) override;
  u32 GetMessageCount() override;

private:
  ScQueueList entries;
  ScLock lock;
  ScThreadSignal signal;
  u32 references;
};

// The original manager owns an ordered map of queue pointers and its own recursive thread-safe base.
// Linux cGZMessageQueueManager symbols establish the original std::map facility and unsigned key type.
class ScQueueService : public ScServicesBase, public ScQueueManager, public ScThreadSafe {
public:
  ScQueueService();
  ~ScQueueService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool GetQueue(ScQueueKey key, ScMessageQueue **out) override;
  bool AddQueue(ScQueueKey key) override;
  bool AddQueue(ScQueueKey key, ScMessageQueue *queue) override;
  bool RemoveQueue(ScQueueKey key, bool release) override;
  bool RemoveQueue(ScMessageQueue *queue, bool release) override;
  bool RemoveAllQueues(bool release) override;
  u32 GetQueueCount() override;

private:
  std::map<ScQueueKey, ScMessageQueue *> queues;
};

struct ScTargetPost {
  ScTargetPost *next, *previous;
  ScMessageTarget *target;
  ScMessage message;
};

struct ScNotificationTarget {
  ScNotificationTarget *next;
  ScMessageTarget *target;
};

struct ScNotificationType {
  ScNotificationType *next;
  ScMsgCode type;
  ScNotificationTarget *first, *last;
};

struct ScPendingNotification {
  ScPendingNotification *next;
  ScMessageTarget *target;
  ScMsgCode type;
  bool add;
};

// The original message service has a separate message-server facet, three locks and an embedded queue.
// Both service and message-server tick identities dispatch the same original message-draining method.
class ScMessageService : public ScServicesBase, public ScMessageServer {
public:
  ScMessageService();
  ~ScMessageService() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool MessageSend(const ScMessage &message) override;
  bool MessagePost(const ScMessage &message, bool high_priority) override;
  bool AddNotification(ScMessageTarget *target, ScMsgCode type) override;
  bool RemoveNotification(ScMessageTarget *target, ScMsgCode type) override;
  u32 GetReferenceCountToMessageTarget(ScMessageTarget *target) override;
  u32 GetReferenceCountToMessageType(ScMsgCode type) override;
  u32 GetTotalReferenceCount() override;
  void GeneralMessagePostToTarget(const ScMessage &message, ScMessageTarget *target) override;
  void CancelGeneralMessagePostsToTarget(ScMessageTarget *target) override;
  u32 GetMessageQueueSize() override;

private:
  ScNotificationType *FindType(ScMsgCode type);
  void AddDirect(ScMessageTarget *target, ScMsgCode type, bool retain);
  void RemoveDirect(ScMessageTarget *target, ScMsgCode type);
  void Defer(ScMessageTarget *target, ScMsgCode type, bool add);
  void ApplyPending();
  void ReplayPending();
  void DispatchPosts();
  ScLock notifications_lock, queue_lock, posts_lock;
  ScQueue queue;
  u32 notification_depth;
  ScNotificationType *types;
  ScPendingNotification *pending_first, *pending_last;
  i32 posts_remaining;
  ScTargetPost posts;
};

#endif
