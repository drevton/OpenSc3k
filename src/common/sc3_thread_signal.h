#ifndef SC3_COMMON_SC3_THREAD_SIGNAL_H
#define SC3_COMMON_SC3_THREAD_SIGNAL_H

#include "common/com.h"

// The original signal interface is independent of ScUnknown and has its own release operation.
class ScSignal {
public:
  virtual u32 Release() = 0;
  virtual i32 Signal() = 0;
  virtual i32 Broadcast() = 0;
  virtual i32 TimedWait(u32 microseconds) = 0;
  virtual i32 IsValid() = 0;
};

// Windows thread signal with the real secondary thread-safe base and owned event name.
class ScThreadSignal : public ScSignal, public ScThreadSafe {
public:
  explicit ScThreadSignal(i32 type);
  ~ScThreadSignal() override;
  u32 Release() override;
  i32 Signal() override;
  i32 Broadcast() override;
  i32 TimedWait(u32 microseconds) override;
  i32 IsValid() override;
  virtual i32 Unsignal();
  virtual i32 GetEventHandle(void **out);

private:
  void *handle;
  i32 manual_reset, valid;
  ScString name;
};

#endif
