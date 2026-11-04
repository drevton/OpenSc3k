#ifndef AU_SOUND_CACHE_H
#define AU_SOUND_CACHE_H

#include <list>

#include "common/sc3_string.h"
#include "common/thread.h"

class AuSoundCache;

// One cache entry owns its source name and stores its reference count and copied size.
class AuCacheEntry {
public:
  AuCacheEntry();
  ~AuCacheEntry();
  ScString name;
  i32 operation, size;
};

// The worker borrows its cache and thread while processing pending local files.
class AuCacheWorker : public ScThreadTarget {
public:
  AuCacheWorker(ScThread *thread, AuSoundCache *cache);
  u32 StartFunction(void *argument) override;
private:
  friend class AuSoundCache;
  u8 running;
  ScThread *thread;
  AuSoundCache *cache;
};

// The original cache owns its local entries and worker; paths and files remain local.
// Nominal private byte/integer return spellings and concrete String reference spelling remain provisional.
class AuSoundCache {
public:
  AuSoundCache();
  ~AuSoundCache();
  virtual u8 Init();
  virtual u8 Shutdown();
  virtual ScString *SetPath(const ScString *path);
  virtual ScString *GetPath();
  virtual i32 SetSize(i32 size);
  virtual i32 GetSize();
  virtual i32 GetUsedSize();
  virtual u8 AddFile(const ScString *path);
  virtual ScString GetCachedPath(const ScString *path);
  virtual u8 RemoveFile(const ScString *path);
  virtual u8 HasFile(const ScString *path);
  virtual u8 Process();
  virtual u8 Clear();
  virtual u8 Evict();
  virtual u8 CopyEntry(AuCacheEntry *entry);
  virtual u8 ProcessPending();

private:
  friend class AuCacheWorker;
  u8 initialized;
  ScString path;
  i32 size;
  std::list<AuCacheEntry *> entries, pending;
  ScThread *thread;
  AuCacheWorker *worker;
  ScLock lock;
};
#endif
