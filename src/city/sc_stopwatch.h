#ifndef SC3_CITY_SC_STOPWATCH_H
#define SC3_CITY_SC_STOPWATCH_H

#include "common/sc3.h"

struct ScCounterSample {
  i64 first, second;
};

// Recovered elapsed-counter receiver. Original x86 offsets4/12 are compiler padding, not additional data members.
class ScStopwatch {
public:
  enum TimerResolution : i32 { Cycles, Microseconds, Milliseconds, Seconds, Minutes };

  ScStopwatch(TimerResolution unit, u32 count);
  virtual ~ScStopwatch();
  void Reset();
  void Restart();
  u32 GetElapsedTime() const;
  void GetElapsedTime(f32 *out) const;
  bool IsTimerRunning() const;
  void Start();
  void Delay(u32 duration);
  void Stop();
  void GetElapsedInternal(i64 *out) const;
  void GetCurrentInternal(i64 *out) const;
  void ConvertInternalToUnitResolution(i64 ticks, i64 *out) const;
  void ConvertInternalToUnitResolution(i64 ticks, f32 *out) const;
  void ConvertInternalToCycles(i64 ticks, i64 *out) const;
  void ConvertInternalToCycles(i64 ticks, f32 *out) const;
  void ConvertInternalToMicroseconds(i64 ticks, i64 *out) const;
  void ConvertInternalToMicroseconds(i64 ticks, f32 *out) const;
  void ConvertInternalToMilliseconds(i64 ticks, i64 *out) const;
  void ConvertInternalToMilliseconds(i64 ticks, f32 *out) const;
  void ConvertInternalToSeconds(i64 ticks, i64 *out) const;
  void ConvertInternalToSeconds(i64 ticks, f32 *out) const;
  void ConvertInternalToMinutes(i64 ticks, i64 *out) const;
  void ConvertInternalToMinutes(i64 ticks, f32 *out) const;
  void SetResolution(TimerResolution value);
  void SetSampleBufferSize(u32 count);
  TimerResolution unit;
  i64 baseline, accumulated;
  ScCounterSample *samplesBegin, *samplesEnd, *samplesLimit;
  u32 sampleCount, sampleIndex, sampleBufferSize;
};

#endif
