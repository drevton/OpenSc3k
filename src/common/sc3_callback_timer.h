#ifndef SC3_COMMON_SC3_CALLBACK_TIMER_H
#define SC3_COMMON_SC3_CALLBACK_TIMER_H

#include "common/sc3_unknown.h"

using ScTimerCallback = void (*)(uptr argument);

// Original executable callback timer, with a native-width word for the pointer-bearing callback argument.
// Periods, resolutions and scheduler IDs retain their original integer widths.
class ScCallbackTimer {
public:
  ScCallbackTimer();
  virtual ~ScCallbackTimer();
  virtual bool SetResolution(u32 resolution);
  virtual bool GetResolution(u32 *out);
  virtual bool SetPeriod(u32 period);
  virtual bool GetPeriod(u32 *out);
  virtual bool StartPeriodic(u32 period, ScUnknown *owner);
  virtual bool StartOneShot(u32 period, ScUnknown *owner);
  virtual bool Stop();
  virtual bool IsRunning();
  virtual bool SetCallbackFunction(ScTimerCallback callback, uptr argument);
  static u32 InternalCallbackFunction(ScCallbackTimer *timer);

private:
  ScUnknown *volatile owner;
  ScTimerCallback volatile callback;
  volatile uptr argument;
  volatile u32 resolution, period;
  volatile bool running, once;
  volatile i32 event;
};

#endif
