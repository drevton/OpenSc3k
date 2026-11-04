#include "audio/audio_cache.h"

#include <assert.h>
#include <string.h>

#include "resources/file.h"

AuCacheEntry::AuCacheEntry() : operation(0) {
}

AuCacheEntry::~AuCacheEntry() {
}

AuCacheWorker::AuCacheWorker(ScThread *workerThread, AuSoundCache *owner) : thread(workerThread), cache(owner) {
  running = 1;
}

u32 AuCacheWorker::StartFunction(void *) {
  while (thread->IsWorkRunning(&running, &cache->lock))
    if (cache->Process())
      thread->WaitForWake();
  thread->Exit(0);
  return 0;
}

AuSoundCache::AuSoundCache() : initialized(0), size(0), thread(nullptr), worker(nullptr) {
}

AuSoundCache::~AuSoundCache() {
  Shutdown();
}

// Create the cache thread and its worker once, then start the worker through the original thread interface.
u8 AuSoundCache::Init() {
  if (!initialized) {
    thread = new ScThread;
    worker = new AuCacheWorker(thread, this);
    lock.Lock();
    thread->Start(static_cast<ScThreadTarget *>(worker));
    initialized = 1;
    lock.Unlock();
  }
  return 1;
}

// Stop the worker, clear cached files and destroy the pending entries before marking the cache inactive.
u8 AuSoundCache::Shutdown() {
  if (!initialized)
    return 1;
  if (thread) {
    thread->StopWork(&worker->running, &lock);
    thread = nullptr;
  }
  Clear();
  for (AuCacheEntry *entry : pending)
    delete entry;
  initialized = 0;
  return 1;
}

// Process queued operations and unfinished copies under the cache lock, then report whether the queue is empty.
u8 AuSoundCache::Process() {
  lock.Lock();
  u8 result = 0;
  if (initialized && worker->running) {
    ProcessPending();
    for (AuCacheEntry *entry : entries) {
      if (entry->size < 0)
        CopyEntry(entry);
    }
    if (pending.empty())
      result = 1;
  }
  lock.Unlock();
  return result;
}

// Apply queued additions and removals to the cached entries' reference counts, then empty the pending list.
u8 AuSoundCache::ProcessPending() {
  lock.Lock();
  if (pending.empty()) {
    lock.Unlock();
    return 0;
  }
  for (AuCacheEntry *request : pending) {
    AuCacheEntry *match = nullptr;
    for (AuCacheEntry *entry : entries) {
      u32 length = entry->name.ScString::Strlen();
      if (length == request->name.ScString::Strlen() &&
          !memcmp(entry->name.ScString::Data(), request->name.ScString::Data(), length)) {
        match = entry;
        break;
      }
    }
    if (request->operation == 1) {
      if (match) {
        ++match->operation;
      } else {
        match = new AuCacheEntry;
        match->name = request->name;
        match->operation = 1;
        match->size = -1;
        entries.push_back(match);
      }
    } else if (request->operation == -1) {
      if (match)
        --match->operation;
    } else {
      lock.Unlock();
      return 0;
    }
    delete request;
  }
  pending.clear();
  lock.Unlock();
  return 1;
}

u8 AuSoundCache::Clear() {
  lock.Lock();
  for (AuCacheEntry *entry : entries) {
    ScString cached = GetCachedPath(&entry->name);
    RsFile::Delete(cached);
    delete entry;
  }
  entries.clear();
  lock.Unlock();
  return 1;
}

// Remove unreferenced cache files and entries, restarting at the second entry after each removal.
u8 AuSoundCache::Evict() {
  auto current = entries.begin();
  while (current != entries.end()) {
    AuCacheEntry *entry = *current;
    if (entry->operation < 1) {
      ScString cached = GetCachedPath(&entry->name);
      RsFile::Delete(cached);
      lock.Lock();
      delete entry;
      entries.erase(current);
      current = entries.begin();
      lock.Unlock();
      if (current == entries.end())
        break;
    }
    ++current;
  }
  return 1;
}

u8 AuSoundCache::CopyEntry(AuCacheEntry *entry) {
  ScString cached = GetCachedPath(&entry->name);
  RsFile source(entry->name);
  if (!source.Open(1, 2, 1)) {
    entry->size = 1;
    return 0;
  }
  i32 fileSize = static_cast<i32>(source.Size());
  i64 required = static_cast<i64>(fileSize) + GetUsedSize();
  assert(required <= INT32_MAX && required >= INT32_MIN);
  if (required >= size) {
    Evict();
    required = static_cast<i64>(fileSize) + GetUsedSize();
    assert(required <= INT32_MAX && required >= INT32_MIN);
    if (required >= size) {
      source.Close();
      return 0;
    }
  }
  if (RsFile::Copy(entry->name, cached, false)) {
    RsFile::SetAttribute(cached, 1, false);
    entry->size = fileSize;
  } else {
    entry->size = 0;
    source.Close();
  }
  return 1;
}

// Replace the stored cache directory and return its concrete String identity.
ScString *AuSoundCache::SetPath(const ScString *value) {
  path = *value;
  return &path;
}

// Borrow the stored cache directory without copying or retaining it.
ScString *AuSoundCache::GetPath() {
  return &path;
}

i32 AuSoundCache::SetSize(i32 value) {
  size = value;
  return value;
}

i32 AuSoundCache::GetSize() {
  return size;
}

// Add the positive recorded sizes of cached entries in list order.
i32 AuSoundCache::GetUsedSize() {
  i32 total = 0;
  for (AuCacheEntry *entry : entries) {
    if (entry->size > 0) {
      i64 next = static_cast<i64>(total) + entry->size;
      assert(next <= INT32_MAX);
      total = static_cast<i32>(next);
    }
  }
  return total;
}

// Join the cache directory to the input path after its first three bytes, retaining short paths whole.
ScString AuSoundCache::GetCachedPath(const ScString *value) {
  u32 length = value->ScString::Strlen();
  ScString suffix = value->Right(length - 3u);
  ScString result(path);
  result.Append(suffix.ScString::Data(), suffix.ScString::Strlen());
  return result;
}

// Queue a cache-add entry and resume the cache worker while holding the entry-list lock.
u8 AuSoundCache::AddFile(const ScString *value) {
  if (!initialized || !worker->running)
    return 0;
  AuCacheEntry *entry = new AuCacheEntry;
  entry->name = *value;
  entry->operation = 1;
  lock.Lock();
  pending.push_back(entry);
  thread->WakeForWork();
  lock.Unlock();
  return 1;
}

// Queue a cache-remove entry and resume the cache worker while holding the entry-list lock.
u8 AuSoundCache::RemoveFile(const ScString *value) {
  if (!initialized || !worker->running)
    return 0;
  AuCacheEntry *entry = new AuCacheEntry;
  entry->name = *value;
  entry->operation = -1;
  lock.Lock();
  pending.push_back(entry);
  thread->WakeForWork();
  lock.Unlock();
  return 1;
}

// Find a matching byte-exact cache name with a nonnegative recorded size.
u8 AuSoundCache::HasFile(const ScString *value) {
  for (AuCacheEntry *entry : entries) {
    u32 length = entry->name.ScString::Strlen();
    if (length == value->ScString::Strlen() &&
        !memcmp(entry->name.ScString::Data(), value->ScString::Data(), length) && entry->size >= 0)
      return 1;
  }
  return 0;
}
