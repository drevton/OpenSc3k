#ifndef SC3_COMMON_THREAD_H
#define SC3_COMMON_THREAD_H

#include <atomic>
#include <utility>
#include <vector>

#include <SDL3/SDL_atomic.h>

#include "common/sc3.h"
#include "common/sc3_string.h"

struct tm;

// Preserve the scheduler's backend storage and publication semantics behind portable names.
struct SDL_Semaphore;
using ScAtomicU32 = SDL_AtomicU32;
using ScSpinLock = SDL_SpinLock;
using ScThreadFlag = std::atomic<u8>;
using ScGateHandle = SDL_Semaphore *;

// Native nonrecursive exclusion for recovered spinword entries; it is separate from recursive game critical sections.
class ScGate {
public:
  ScGate();
  ~ScGate();
  void Lock();
  void Unlock();
  ScGate(const ScGate &) = delete;
  ScGate &operator=(const ScGate &) = delete;

private:
  ScGateHandle handle;
};

u32 ScFetchAdd(ScAtomicU32 *value, u32 addend);

// Original cIGZMutex528A is an independent synchronization interface, with no Unknown base or reference-count methods.
class ScMutexInterface {
public:
  virtual u32 Release() = 0;
  virtual i32 Lock() = 0;
  virtual i32 Unlock() = 0;
  virtual i32 TryLock() = 0;
  virtual i32 IsValid() = 0;
};

// Original cRZMutex3633 owns one platform HANDLE and an optional name; Release deletes rather than decrementing a count.
class ScMutex : public ScMutexInterface {
public:
  ScMutex();
  explicit ScMutex(const ScString &name);
  u32 Release() override;
  i32 Lock() override;
  i32 Unlock() override;
  i32 TryLock() override;
  i32 IsValid() override;
  virtual ~ScMutex();
  virtual i32 GetMutexHandle(void **out);

private:
  void *handle;
  i32 valid;
  ScString name;
};

struct SDL_Mutex;

class ScLock {
public:
  ScLock();
  virtual u32 Release();
  virtual int Lock();
  virtual int Unlock();
  virtual int TryLock();
  virtual int IsValid();
  virtual int IsLocked();
  virtual ~ScLock();
  virtual int GetCriticalSectionHandle(void *out);
  // Keep the platform record footprint so embedded game fields retain their established offsets.
  union {
    SDL_Mutex *section;
    u8 platform_section[sizeof(uptr) == 4 ? 24 : 40];
  };
  u32 initialized, depth;
};

// Native monotonic clock for default random seeds; unlike process-relative ticks, it does not restart at each launch.
u32 ScSystemMilliseconds();

// Multimedia-clock seed source; retain timeGetTime on Windows and portable system uptime on other hosts.
u32 ScMultimediaMilliseconds();

// Calendar boundaries retain the original signed 32-bit CRT time range on every host.
i32 ScTime32();
i32 ScMakeLocalTime32(tm *local);
i32 ScTimezoneSeconds();
void ScReadLocalDate(tm *out);
void ScReadLocalTime(tm *out);
int ScDaylightEnabled();

// Yield without making the current thread's wait alertable.
void ScYieldThread();

// Zero-initialized module-wide bit locks retain their platform acquisition and publication semantics.
void ScLockSpin(ScSpinLock *lock);
void ScUnlockSpin(ScSpinLock *lock);

#ifdef _WIN32
#define SC_THREAD_CALL __stdcall
#else
#define SC_THREAD_CALL
#endif

typedef u32(SC_THREAD_CALL *ScThreadFunction)(void *argument);

// Actual cIGZThreadStartable has one virtual, returning a DWORD from its platform pointer argument.
// The worker supplies nullptr; the interface has no virtual destructor.
class ScThreadTarget {
public:
  virtual u32 StartFunction(void *argument) = 0;
};

// Recovered cRZThread operations and fields shared by SIMCITY and SIMSPR.
class ScThread {
public:
  ScThread();
  virtual u32 Release();
  virtual uptr Start(ScThreadTarget *target);
  virtual uptr Start(ScThreadFunction function, void *argument);
  virtual void Exit(u32 code);
  virtual u32 Suspend();
  virtual u32 Resume();
  virtual u32 Sleep(u32 microseconds);
  virtual u32 Wake();
  virtual i32 Wait();
  virtual i32 GetPriority();
  virtual i32 SetPriority(i32 priority);
  virtual i32 IsRunning();
  virtual ~ScThread();
  virtual i32 GetExitCode(u32 *out);
  virtual i32 GetThreadId(u32 *out);
  virtual i32 GetHandle(void **out);
  static void SleepCurrentThread(u32 microseconds);
  static void SuspendCurrentThread();
  // Platform coordination for idle workers and externally paused drawing work.
  void WakeForWork();
  bool IsWorkRunning(const u8 *running, ScLock *lock);
  void StopWork(u8 *running, ScLock *lock);
  void WaitForWake();
  static void WaitCurrentForWake();
  static void PinCallbackOwner();
  static void UnpinCallbackOwner();
  void RetireCallbackOwner();
  void PauseCheckpoint();
  bool PausePending();
  static i32 SetCurrentPriority(i32 priority);
  static i32 CurrentPriority();
  u8 state;
  u32 exitCode;
  void *handle;
  u32 threadId;
  i32 priority;
};

// Retail cRZTimer type 2CD5 records the original vector of pairs of 64-bit samples.
class ScTimer {
public:
  enum class Resolution : i32;
  ScTimer(Resolution resolution, u32 sample_buffer_size);
  virtual ~ScTimer();
  void Start();
  void Stop();
  void Sample();
  void Reset();
  void Restart();
  bool IsTimerRunning() const;
  u32 GetSampleCount() const;
  void GetAverageCycles(f32 *out) const;
  void GetFrequency(f32 *out) const;
  f32 GetOverheadMicroseconds() const;
  void SetSampleBufferSize(u32 size);
  void SetResolution(Resolution value);
  Resolution GetResolution() const;
  void GetCurrentInternal(i64 *out) const;
  void GetElapsedInternal(i64 *out) const;
  void ConvertInternalToUnitResolution(i64 value, i64 *out) const;
  void ConvertInternalToUnitResolution(i64 value, f32 *out) const;
  void ConvertUnitResolutionToInternal(f32 value, i64 *out) const;
  void ConvertInternalToCycles(i64 value, i64 *out) const;
  void ConvertInternalToMicroseconds(i64 value, i64 *out) const;
  void ConvertInternalToMilliseconds(i64 value, i64 *out) const;
  void ConvertInternalToSeconds(i64 value, i64 *out) const;
  void ConvertInternalToMinutes(i64 value, i64 *out) const;
  void ConvertInternalToCycles(i64 value, f32 *out) const;
  void ConvertInternalToMicroseconds(i64 value, f32 *out) const;
  void ConvertInternalToMilliseconds(i64 value, f32 *out) const;
  void ConvertInternalToSeconds(i64 value, f32 *out) const;
  void ConvertInternalToMinutes(i64 value, f32 *out) const;
  void ConvertCyclesToInternal(f32 value, i64 *out) const;
  void ConvertMicrosecondsToInternal(f32 value, i64 *out) const;
  void ConvertMillisecondsToInternal(f32 value, i64 *out) const;
  void ConvertSecondsToInternal(f32 value, i64 *out) const;
  void ConvertMinutesToInternal(f32 value, i64 *out) const;
  u32 GetElapsedTime() const;
  void GetElapsedTime(f32 *out) const;
  void Delay(u32 duration);

private:
  u32 field4;
  Resolution resolution;
  i64 start_time, elapsed_time;
  std::vector<std::pair<i64, i64>> samples;
  u32 sample_count, sample_index, sample_buffer_size;
};

#endif
