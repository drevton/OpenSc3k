#include <set>

#include "audio/callback_timer.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"

static ScLock AuTimerLock;
static std::set<AuCallbackTimer *> AuTimers;

// Hold the original global timer lock while changing or inspecting its live-object set.
class AuTimerGuard {
public:
  // Enter the shared recursive lock before inspecting a timer.
  AuTimerGuard() {
    AuTimerLock.Lock();
  }

  // Leave the lock when the protected scope ends.
  ~AuTimerGuard() {
    AuTimerLock.Unlock();
  }
};

// Initialize a stopped callback timer and register its real address in the process-wide timer set.
AuCallbackTimer::AuCallbackTimer()
    : owner(nullptr), callback(nullptr), resolution(100), period(1000), running(0), oneShot(0), event(0) {
  AuTimerGuard guard;
  AuTimers.insert(this);
}

// Remove the timer from the live set before stopping it and releasing its retained owner.
AuCallbackTimer::~AuCallbackTimer() {
  {
    AuTimerGuard guard;
    AuTimers.erase(this);
  }
  if (running)
    Stop();
  ScRelease(&owner);
  callback = nullptr;
  argument = 0;
}

// Store the resolution and restart a running timer while preserving its owner reference ordering.
bool AuCallbackTimer::SetResolution(u32 value) {
  ScUnknown *saved = owner;
  if (saved)
    saved->AddRef();
  bool wasRunning = running != 0;
  resolution = value;
  if (wasRunning) {
    Stop();
    if (oneShot)
      StartOneShot(0, nullptr);
    else
      StartPeriodic(0, nullptr);
  }
  if (saved) {
    if (owner)
      saved->Release();
    owner = saved;
  }
  return 1;
}

bool AuCallbackTimer::GetResolution(u32 *out) {
  *out = resolution;
  return 1;
}

// Store the period and restart a running timer while preserving its owner reference ordering.
bool AuCallbackTimer::SetPeriod(u32 value) {
  ScUnknown *saved = owner;
  if (saved)
    saved->AddRef();
  bool wasRunning = running != 0;
  period = value;
  if (wasRunning) {
    Stop();
    if (oneShot)
      StartOneShot(0, nullptr);
    else
      StartPeriodic(0, nullptr);
  }
  if (saved) {
    if (owner)
      saved->Release();
    owner = saved;
  }
  return 1;
}

bool AuCallbackTimer::GetPeriod(u32 *out) {
  *out = period;
  return 1;
}

// Retain an optional owner and schedule a repeating callback, restoring the stopped state on failure.
bool AuCallbackTimer::StartPeriodic(u32 value, ScUnknown *newOwner) {
  if (running || !callback)
    return 0;
  if (newOwner) {
    newOwner->AddRef();
    owner = newOwner;
  }
  if (value)
    period = value;
  running = 1;
  event = AuDispatcher.Schedule(InternalCallback, this, period, 0, 0);
  if (!event) {
    running = 0;
    if (newOwner)
      newOwner->Release();
    owner = nullptr;
    return 0;
  }
  return 1;
}

// Schedule one callback using the supplied delay or the existing period when the delay is zero.
bool AuCallbackTimer::StartOneShot(u32 value, ScUnknown *newOwner) {
  if (running || !callback)
    return 0;
  if (!value)
    value = period;
  if (newOwner) {
    newOwner->AddRef();
    owner = newOwner;
  }
  running = 1;
  oneShot = 1;
  event = AuDispatcher.Schedule(InternalCallback, this, value, 1, 0);
  if (!event) {
    running = 0;
    oneShot = 0;
    if (newOwner)
      newOwner->Release();
    owner = nullptr;
    return 0;
  }
  return 1;
}

bool AuCallbackTimer::Stop() {
  if (event) {
    AuDispatcher.Cancel(event);
    ScRelease(&owner);
    event = 0;
    oneShot = 0;
    running = 0;
  }
  return 1;
}

bool AuCallbackTimer::IsRunning() {
  return running != 0;
}

// Store the callback and its native-width argument without retaining a separate context object.
bool AuCallbackTimer::SetCallbackFunction(AuTimerCallback value, uptr parameter) {
  callback = value;
  argument = parameter;
  return 1;
}

// Check that the timer is still registered, retain its owner, and invoke the saved callback outside the lock.
u32 AuCallbackTimer::InternalCallback(AuCallbackTimer *timer) {
  AuTimerLock.Lock();
  if (AuTimers.find(timer) == AuTimers.end()) {
    AuTimerLock.Unlock();
    return 0;
  }
  ScUnknown *saved = timer->owner;
  AuTimerCallback function = timer->callback;
  uptr parameter = timer->argument;
  u8 once = timer->oneShot;
  if (saved)
    saved->AddRef();
  if (once) {
    if (timer->owner) {
      timer->owner->Release();
      timer->owner = nullptr;
    }
    timer->running = 0;
    timer->oneShot = 0;
    timer->event = 0;
  }
  AuTimerLock.Unlock();
  if (function) {
    function(parameter);
    if (saved)
      saved->Release();
  }
  return 0;
}

// The original global dispatcher exists for the lifetime of the sound DLL.
AuTimerDispatcher AuDispatcher;

// Hold the dispatcher's actual recursive lock through task-list changes and exception unwinding.
class AuDispatchGuard {
public:
  // Enter the dispatcher's lock.
  explicit AuDispatchGuard(ScLock *value) : lock(value) {
    lock->Lock();
  }

  // Leave the dispatcher lock at scope exit.
  ~AuDispatchGuard() {
    lock->Unlock();
  }

private:
  ScLock *lock;
};

// Protect only short dispatcher-member accesses against native worker retirement.
class AuDispatchOwnerPin {
public:
  // Pin the current worker's still-live dispatcher receiver.
  AuDispatchOwnerPin() {
    ScThread::PinCallbackOwner();
  }

  // Release the pin before any foreign callback, sleep or wait.
  ~AuDispatchOwnerPin() {
    ScThread::UnpinCallbackOwner();
  }
};

// Initialize the task list, elapsed timer, worker and lock, then set the original caller-thread priority.
AuTimerDispatcher::AuTimerDispatcher() : clock(ScStopwatch::Milliseconds, 0) {
  u32 threadId;
  worker.GetThreadId(&threadId);
  stopping.store(0, std::memory_order_relaxed);
  nextId.store(1, std::memory_order_relaxed);
  clock.SetResolution(ScStopwatch::Milliseconds);
  ScThread::SetCurrentPriority(2);
  active.store(0, std::memory_order_relaxed);
}

// Wake the worker and preserve the original one-second shutdown wait before destroying dispatcher members.
AuTimerDispatcher::~AuTimerDispatcher() {
  u32 threadId;
  worker.GetThreadId(&threadId);
  stopping.store(1, std::memory_order_relaxed);
  if (worker.IsRunning()) {
    ScStopwatch timeout(ScStopwatch::Milliseconds, 0);
    worker.Resume();
    worker.Wake();
    timeout.Restart();
    while (timeout.GetElapsedTime() < 1000 && active.load(std::memory_order_relaxed)) {
    }
  }
  worker.RetireCallbackOwner();
}

// Add an original callback task and start or wake the worker according to the list's previous state.
u32 AuTimerDispatcher::Schedule(AuTimerDispatch callback, AuCallbackTimer *timer, u32 period, u8 once, u8 option) {
  AuTimerTask task{nextId.fetch_add(1, std::memory_order_relaxed), callback, timer, once, period, 0, option};
  AuDispatchGuard guard(&lock);
  if (tasks.empty()) {
    clock.Restart();
    task.due = period + clock.GetElapsedTime();
    tasks.push_back(task);
    if (!active.load(std::memory_order_relaxed)) {
      active.store(1, std::memory_order_relaxed);
      worker.Start(static_cast<ScThreadTarget *>(this));
    }
    worker.WakeForWork();
  } else {
    task.due = period + clock.GetElapsedTime();
    tasks.push_back(task);
    worker.Wake();
  }
  return task.identifier;
}

// Remove the first task with the requested identifier while holding the original dispatcher lock.
u8 AuTimerDispatcher::Cancel(u32 identifier) {
  AuDispatchGuard guard(&lock);
  for (auto it = tasks.begin(); it != tasks.end(); ++it) {
    if (it->identifier == identifier) {
      tasks.erase(it);
      return 1;
    }
  }
  return 0;
}

// Collect due tasks under the lock, invoke their callbacks outside it and wait or suspend until the next work.
u32 AuTimerDispatcher::StartFunction(void *) {
  u32 threadId;
  std::list<AuTimerTask> ready;
  {
    AuDispatchOwnerPin pin;
    worker.GetThreadId(&threadId);
    active.store(1, std::memory_order_relaxed);
  }
  for (;;) {
    bool hasTasks, dispatch = false;
    u32 now = 0, next = UINT32_MAX;
    {
      AuDispatchOwnerPin pin;
      if (stopping.load(std::memory_order_relaxed)) {
        worker.GetThreadId(&threadId);
        active.store(0, std::memory_order_relaxed);
        return 0;
      }
      {
        AuDispatchGuard guard(&lock);
        hasTasks = !tasks.empty();
      }
      if (hasTasks && !stopping.load(std::memory_order_relaxed)) {
        dispatch = true;
        AuDispatchGuard guard(&lock);
        now = clock.GetElapsedTime();
        ready.clear();
        for (auto it = tasks.begin(); it != tasks.end();) {
          if (it->due <= now) {
            ready.push_back(*it);
            if (it->once) {
              it = tasks.erase(it);
              continue;
            }
            it->due = it->fixedPeriod ? it->due + it->period : now + it->period;
            if (it->due < now)
              it->due = UINT32_MAX;
          }
          u32 delay = it->due - now;
          if (delay < next)
            next = delay;
          ++it;
        }
      }
    }
    if (dispatch) {
      for (const AuTimerTask &task : ready)
        task.callback(task.timer);
      bool sleep = false;
      u32 delay = 0;
      {
        AuDispatchOwnerPin pin;
        {
          AuDispatchGuard guard(&lock);
          hasTasks = !tasks.empty();
        }
        if (hasTasks && !stopping.load(std::memory_order_relaxed)) {
          u32 elapsed = clock.GetElapsedTime() - now;
          delay = elapsed < next ? next - elapsed : 10;
          sleep = true;
        }
      }
      if (sleep) {
        ScThread::SleepCurrentThread(1000 * delay);
        AuDispatchOwnerPin pin;
        worker.GetThreadId(&threadId);
        continue;
      }
    }
    bool wait;
    {
      AuDispatchOwnerPin pin;
      wait = !stopping.load(std::memory_order_relaxed);
      if (wait)
        worker.GetThreadId(&threadId);
    }
    if (wait) {
      ScThread::WaitCurrentForWake();
      AuDispatchOwnerPin pin;
      worker.GetThreadId(&threadId);
    }
  }
}
