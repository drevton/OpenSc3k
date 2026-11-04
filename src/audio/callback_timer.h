#ifndef SC3_AUDIO_CALLBACK_TIMER_H
#define SC3_AUDIO_CALLBACK_TIMER_H

#include <atomic>
#include <list>

#include "audio/api.h"
#include "audio/device.h"
#include "common/thread.h"

class AuCallbackTimer;
using AuTimerDispatch = u32 (*)(AuCallbackTimer *timer);
using AuTimerCallback = void (*)(uptr argument);

// Original scheduler task contents, stored in its actual ordered callback list.
struct AuTimerTask {
  u32 identifier;
  AuTimerDispatch callback;
  AuCallbackTimer *timer;
  u8 once;
  u32 period, due;
  u8 fixedPeriod;
};

// Original GZSOUNDD scheduler owns its task list, elapsed timer, worker and lock.
class AuTimerDispatcher : public ScThreadTarget {
public:
  AuTimerDispatcher();
  ~AuTimerDispatcher();
  u32 Schedule(AuTimerDispatch callback, AuCallbackTimer *timer, u32 period, u8 once, u8 option);
  u8 Cancel(u32 identifier);
  u32 StartFunction(void *argument) override;

private:
  std::list<AuTimerTask> tasks;
  ScStopwatch clock;
  ScThread worker;
  std::atomic<u32> nextId;
  ScLock lock;
  std::atomic<u8> stopping, active;
};

extern AuTimerDispatcher AuDispatcher;

// Retail cRZCallbackTimer 0x11e1 uses the bool return type confirmed by its decorated method symbols.
class AuCallbackTimer {
public:
  AuCallbackTimer();
  virtual ~AuCallbackTimer();
  virtual bool SetResolution(u32 resolution);
  virtual bool GetResolution(u32 *out);
  virtual bool SetPeriod(u32 period);
  virtual bool GetPeriod(u32 *out);
  virtual bool StartPeriodic(u32 period, ScUnknown *owner);
  virtual bool StartOneShot(u32 period, ScUnknown *owner);
  virtual bool Stop();
  virtual bool IsRunning();
  virtual bool SetCallbackFunction(AuTimerCallback callback, uptr argument);
  static u32 InternalCallback(AuCallbackTimer *timer);

private:
  ScUnknown *owner;
  AuTimerCallback callback;
  uptr argument;
  u32 resolution, period;
  u8 running, oneShot;
  u32 event;
};

#endif
