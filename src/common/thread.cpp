#define WIN32_LEAN_AND_MEAN
#include "common/thread.h"

#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#elif !defined(_WIN32)
#include <sched.h>
#endif
#ifdef _WIN32
#include <intrin.h>
// clang-format off: Windows multimedia declarations require the base Windows types first.
#include <windows.h>
#include <mmsystem.h>
// clang-format on
#undef PlaySound
#endif

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

#include "common/sc3_error.h"
#include "common/sc3_thread_signal.h"

// Suspend and resume report failure when no live managed worker record is available.
enum ScThreadSuspendResult : u32 {
  kScThreadSuspendResumeFailed = 0xffffffff,
};

// Workers retain this cached sentinel until an explicit priority is selected; the default maps to normal priority.
enum ScThreadPrioritySentinel : i32 {
  kScThreadDefaultPriority = 0x12345678,
};

// Create a nonrecursive entry gate without changing the semantics of the recursive ScLock class.
// The gate owns its backend synchronization object; failure to allocate it is unrecoverable.
ScGate::ScGate() {
  handle = SDL_CreateSemaphore(1);
  if (!handle)
    ScFatal(SC_ERROR_NOMEM, "Could not create a game entry gate");
}

// Retire the owned gate after its callers have finished. Windows SRW locks contain no separately owned allocation.
ScGate::~ScGate() {
  SDL_DestroySemaphore(handle);
}

// Wait for sole entry into the protected game operation.
// Recursive acquisition remains blocked like the original spinword.
void ScGate::Lock() {
  SDL_WaitSemaphore(handle);
}

// Release one completed entry so another waiter can proceed. Callers retain the original balanced lock boundaries.
void ScGate::Unlock() {
  SDL_SignalSemaphore(handle);
}

// Keep the low millisecond word of the platform counter without overflowing its nanosecond representation.
// Default random seeds need a running system clock rather than SDL's newly initialized process-relative tick origin.
u32 ScSystemMilliseconds() {
  u64 frequency = SDL_GetPerformanceFrequency(), counter = SDL_GetPerformanceCounter();
  return static_cast<u32>(counter / frequency) * 1000u +
      static_cast<u32>((counter % frequency) * 1000u / frequency);
}

// Sample the original Windows multimedia timer. Other hosts use the portable system counter's low millisecond word.
u32 ScMultimediaMilliseconds() {
#ifdef _WIN32
  return timeGetTime();
#else
  return ScSystemMilliseconds();
#endif
}

// Fetch the old scheduler counter and publish its wrapped increment as one atomic operation.
// Keep the existing SDL or Windows storage contract at this boundary.
u32 ScFetchAdd(ScAtomicU32 *value, u32 addend) {
  return SDL_AddAtomicU32(value, addend);
}

// Read the signed 32-bit clock used by the original CRT. Wider host clocks retain its failure result outside that range.
i32 ScTime32() {
#ifdef _WIN32
  return _time32(nullptr);
#else
  time_t now = time(nullptr);
  return now < 0 || now > INT32_MAX ? -1 : static_cast<i32>(now);
#endif
}

// Normalize local calendar fields through the host CRT, retaining the original 32-bit epoch range and failure value.
i32 ScMakeLocalTime32(tm *local) {
#ifdef _WIN32
  return _mktime32(local);
#else
  time_t value = mktime(local);
  return value < 0 || value > INT32_MAX ? -1 : static_cast<i32>(value);
#endif
}

// Read the signed seconds west of UTC used by the original CRT. Calendar callers retain their own wrapping arithmetic.
i32 ScTimezoneSeconds() {
#ifdef _WIN32
  long value;
  _get_timezone(&value);
#else
  tzset();
  long value = timezone;
#endif
  return static_cast<i32>(value);
}

// Require a defined local-date result before a game constructor consumes its fields.
// The original null localtime result leads to an undefined dereference at this native boundary.
void ScReadLocalDate(tm *out) {
#ifdef _WIN32
  __time32_t now = _time32(nullptr);
  if (_localtime32_s(out, &now))
#else
  time_t now = time(nullptr);
  if (now < 0 || now > INT32_MAX || !localtime_r(&now, out))
#endif
    ScFatal(SC_ERROR_IO, "Local date is unavailable");
}

// Read local calendar fields for the timestamp constructor. Windows retains the original shared CRT buffer side effect.
void ScReadLocalTime(tm *out) {
#ifdef _WIN32
  __time32_t now;
  _time32(&now);
#pragma warning(suppress : 4996) // Match the original shared CRT local-time result.
  tm *local = _localtime32(&now);
  assert(local);
  *out = *local;
#else
  ScReadLocalDate(out);
#endif
}

// Query whether the current timezone observes daylight saving. Game-specific transition rules remain with the calendar.
int ScDaylightEnabled() {
#ifdef _WIN32
  int enabled;
  _get_daylight(&enabled);
  return enabled;
#else
  tzset();
  return daylight;
#endif
}

static void ScPlatformLockInit(ScLock *lock) {
  lock->section = SDL_CreateMutex();
  if (!lock->section)
    ScFatal(SC_ERROR_NOMEM, SDL_GetError());
}

static void ScPlatformLockDestroy(ScLock *lock) {
  SDL_DestroyMutex(lock->section);
}

static void ScPlatformLockEnter(ScLock *lock) {
  SDL_LockMutex(lock->section);
}

static void ScPlatformLockLeave(ScLock *lock) {
  SDL_UnlockMutex(lock->section);
}

static bool ScPlatformLockTry(ScLock *lock) {
  return SDL_TryLockMutex(lock->section);
}

// Initialize the recursive platform critical section. The source tracks initialization and starts with zero entries.
ScLock::ScLock() : initialized(0), depth(0) {
  memset(&section, 0, sizeof(section));
  ScPlatformLockInit(this);
  initialized = 1;
}

u32 ScLock::Release() {
  delete this;
  return 0;
}

ScLock::~ScLock() {
  ScPlatformLockDestroy(this);
}

// Enter the recursive section before incrementing the live nesting count. The original ordinary return is zero.
int ScLock::Lock() {
  ScPlatformLockEnter(this);
  ++depth;
  return 0;
}

// Decrement the nesting count before leaving the critical section. Preserve the original return value of one.
int ScLock::Unlock() {
  --depth;
  ScPlatformLockLeave(this);
  return 1;
}

// Attempt the native recursive lock without waiting. Only a successful entry increments the live nesting count.
int ScLock::TryLock() {
  if (!ScPlatformLockTry(this))
    return 0;
  ++depth;
  return 1;
}

int ScLock::IsValid() {
  return initialized;
}

int ScLock::IsLocked() {
  return depth;
}

int ScLock::GetCriticalSectionHandle(void *out) {
  memcpy(out, &section, sizeof(section));
  return 1;
}

#if !defined(_WIN32)
struct ScSdlMutex {
  SDL_Mutex *mutex;
  SDL_SpinLock ownershipLock;
  SDL_ThreadID owner;
  u32 depth;
  int file;
};

// Allocate the recursive process mutex and optionally associate it with a stable per-user lock file.
static ScSdlMutex *ScCreateSdlMutex(const char *name) {
  ScSdlMutex *mutex = new ScSdlMutex{SDL_CreateMutex(), 0, 0, 0, -1};
  if (!mutex->mutex) {
    delete mutex;
    return nullptr;
  }
  if (name) {
    u64 hash = 14695981039346656037ull;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
      hash = (hash ^ *p) * 1099511628211ull;
    char path[96];
    snprintf(path, sizeof(path), "/tmp/sc3-mutex-%lu-%016llx", (unsigned long)getuid(), (unsigned long long)hash);
    mutex->file = open(path, O_CREAT | O_RDWR, 0600);
    if (mutex->file < 0) {
      SDL_DestroyMutex(mutex->mutex);
      delete mutex;
      return nullptr;
    }
  }
  return mutex;
}

// Apply the original mutex lock operation using the SDL/POSIX platform record.
static i32 ScPlatformMutexLock(void *handle) {
  ScSdlMutex *mutex = static_cast<ScSdlMutex *>(handle);
  if (!mutex)
    return 0;
  SDL_LockMutex(mutex->mutex);
  SDL_LockSpinlock(&mutex->ownershipLock);
  bool first = mutex->depth == 0;
  SDL_UnlockSpinlock(&mutex->ownershipLock);
  if (first && mutex->file >= 0 && flock(mutex->file, LOCK_EX)) {
    SDL_UnlockMutex(mutex->mutex);
    return 0;
  }
  SDL_LockSpinlock(&mutex->ownershipLock);
  mutex->owner = SDL_GetCurrentThreadID();
  ++mutex->depth;
  SDL_UnlockSpinlock(&mutex->ownershipLock);
  return 1;
}

// Apply the original mutex unlock operation using the SDL/POSIX platform record.
static i32 ScPlatformMutexUnlock(void *handle) {
  ScSdlMutex *mutex = static_cast<ScSdlMutex *>(handle);
  if (!mutex)
    return 0;
  SDL_LockSpinlock(&mutex->ownershipLock);
  if (mutex->owner != SDL_GetCurrentThreadID() || !mutex->depth) {
    SDL_UnlockSpinlock(&mutex->ownershipLock);
    return 0;
  }
  if (--mutex->depth == 0) {
    mutex->owner = 0;
    if (mutex->file >= 0 && flock(mutex->file, LOCK_UN)) {
      ++mutex->depth;
      mutex->owner = SDL_GetCurrentThreadID();
      SDL_UnlockSpinlock(&mutex->ownershipLock);
      return 0;
    }
  }
  SDL_UnlockSpinlock(&mutex->ownershipLock);
  SDL_UnlockMutex(mutex->mutex);
  return 1;
}

// Apply the original mutex trylock operation using the SDL/POSIX platform record.
static i32 ScPlatformMutexTryLock(void *handle) {
  ScSdlMutex *mutex = static_cast<ScSdlMutex *>(handle);
  if (!mutex || !SDL_TryLockMutex(mutex->mutex))
    return 0;
  SDL_LockSpinlock(&mutex->ownershipLock);
  bool first = mutex->depth == 0;
  SDL_UnlockSpinlock(&mutex->ownershipLock);
  if (first && mutex->file >= 0 && flock(mutex->file, LOCK_EX | LOCK_NB)) {
    SDL_UnlockMutex(mutex->mutex);
    return 0;
  }
  SDL_LockSpinlock(&mutex->ownershipLock);
  mutex->owner = SDL_GetCurrentThreadID();
  ++mutex->depth;
  SDL_UnlockSpinlock(&mutex->ownershipLock);
  return 1;
}

// Destroy the POSIX mutex record and its optional interprocess lock file.
static void ScPlatformMutexDestroy(void *handle) {
  ScSdlMutex *mutex = static_cast<ScSdlMutex *>(handle);
  if (mutex) {
    if (mutex->file >= 0)
      close(mutex->file);
    SDL_DestroyMutex(mutex->mutex);
    delete mutex;
  }
}

// Create a POSIX-backed mutex, optionally shared by name across processes.
static void *ScPlatformMutexCreate(const char *name) {
  return ScCreateSdlMutex(name);
}
#else
// Create a Windows kernel mutex with the optional original name.
static void *ScPlatformMutexCreate(const char *name) {
  return CreateMutexA(nullptr, false, name);
}

static i32 ScPlatformMutexLock(void *handle) {
  return WaitForSingleObject(handle, INFINITE) != WAIT_FAILED;
}

static i32 ScPlatformMutexUnlock(void *handle) {
  return ReleaseMutex(handle);
}

static i32 ScPlatformMutexTryLock(void *handle) {
  DWORD result = WaitForSingleObject(handle, 0);
  return result != WAIT_FAILED && result != WAIT_TIMEOUT;
}

static void ScPlatformMutexDestroy(void *handle) {
  CloseHandle(handle);
}
#endif

// Construct the empty owned name before creating an initially unowned, unnamed kernel mutex.
ScMutex::ScMutex() {
  valid = 0;
  handle = ScPlatformMutexCreate(nullptr);
  if (handle)
    valid = 1;
}

// Create the named kernel mutex and copy its name only when creation returns a handle.
ScMutex::ScMutex(const ScString &source) {
  valid = 0;
  handle = ScPlatformMutexCreate(source.begin);
  if (handle) {
    name = source;
    valid = 1;
  }
}

u32 ScMutex::Release() {
  delete this;
  return 0;
}

i32 ScMutex::Lock() {
  return ScPlatformMutexLock(handle);
}

i32 ScMutex::Unlock() {
  return ScPlatformMutexUnlock(handle);
}

i32 ScMutex::TryLock() {
  return ScPlatformMutexTryLock(handle);
}

i32 ScMutex::IsValid() {
  return valid;
}

ScMutex::~ScMutex() {
  ScPlatformMutexDestroy(handle);
}

i32 ScMutex::GetMutexHandle(void **out) {
  *out = handle;
  return 1;
}



struct ScSdlSignal {
  SDL_Mutex *mutex;
  SDL_Condition *condition;
  u64 pulse;
  bool signaled;
};

// Change an event under its own lock. A pulse wakes existing waiters without becoming pending for future waits.
static i32 ScSdlSetSignal(ScSdlSignal *event, bool persistent) {
  if (!event)
    return 0;
  SDL_LockMutex(event->mutex);
  if (persistent)
    event->signaled = true;
  else {
    event->signaled = false;
    ++event->pulse;
  }
  SDL_BroadcastCondition(event->condition);
  SDL_UnlockMutex(event->mutex);
  return 1;
}

// Allocate the mutex and condition backing the original signal state.
static void *ScPlatformSignalCreate(i32) {
  ScSdlSignal *event = new ScSdlSignal{SDL_CreateMutex(), SDL_CreateCondition(), 0, false};
  if (event->mutex && event->condition)
    return event;
  SDL_DestroyCondition(event->condition);
  SDL_DestroyMutex(event->mutex);
  delete event;
  return nullptr;
}

// Retire both synchronization primitives in the owned signal record.
static void ScPlatformSignalDestroy(void *handle) {
  if (ScSdlSignal *event = static_cast<ScSdlSignal *>(handle)) {
    SDL_DestroyCondition(event->condition);
    SDL_DestroyMutex(event->mutex);
    delete event;
  }
}

static i32 ScPlatformSignalSet(void *handle, bool persistent) {
  return ScSdlSetSignal(static_cast<ScSdlSignal *>(handle), persistent);
}

// Wait on the SDL signal with the original microsecond-to-millisecond conversion.
static i32 ScPlatformSignalWait(void *handle, u32 microseconds) {
  ScSdlSignal *event = static_cast<ScSdlSignal *>(handle);
  if (!event)
    return 0;
  SDL_LockMutex(event->mutex);
  u64 pulse = event->pulse, deadline = SDL_GetTicks() + microseconds / 1000;
  i32 value = 1;
  while (!event->signaled && event->pulse == pulse) {
    if (!microseconds)
      SDL_WaitCondition(event->condition, event->mutex);
    else {
      u64 now = SDL_GetTicks();
      if (now >= deadline) {
        value = 2;
        break;
      }
      SDL_WaitConditionTimeout(event->condition, event->mutex, static_cast<i32>(deadline - now));
    }
  }
  SDL_UnlockMutex(event->mutex);
  return value;
}

// Clear the persistent signal state while retaining its waiter record.
static i32 ScPlatformSignalReset(void *handle) {
  ScSdlSignal *event = static_cast<ScSdlSignal *>(handle);
  if (!event)
    return 0;
  SDL_LockMutex(event->mutex);
  event->signaled = false;
  SDL_UnlockMutex(event->mutex);
  return 1;
}

// SDL workers use a condition to coordinate alertable waits and explicit cooperative suspension.
struct ScSdlThread {
  SDL_Mutex *mutex;
  SDL_Condition *condition;
  SDL_Thread *thread;
  ScThread *owner; // Borrowed while published; cleared under the publication lock before owner destruction.
  ScThreadTarget *target;
  ScThreadFunction function;
  void *argument;
  i32 priority;
  SDL_ThreadID id;
  u32 suspendCount, references, ownerPins;
  bool running, wake;
};

struct ScSdlThreadExit {
  u32 code;
};

static SDL_SpinLock ScSdlThreadPublicationLock;
static thread_local ScSdlThread *ScCurrentSdlThread;
static thread_local i32 ScCurrentSdlPriority = 3;

// Retain the published worker while preventing concurrent completion from destroying its synchronization objects.
static ScSdlThread *ScRetainSdlThread(ScThread *owner) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  ScSdlThread *worker = static_cast<ScSdlThread *>(owner->handle);
  if (worker)
    ++worker->references;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  return worker;
}

// Release a platform record only after its owner, executing worker and all borrowed operations have released it.
static void ScReleaseSdlThread(ScSdlThread *worker) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  bool destroy = --worker->references == 0;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  if (destroy) {
    SDL_DestroyCondition(worker->condition);
    SDL_DestroyMutex(worker->mutex);
    delete worker;
  }
}

// Apply the nearest SDL scheduling class to the current worker and retain its game priority on success.
static i32 ScApplySdlPriority(i32 value) {
  SDL_ThreadPriority platform = value < 3 ? SDL_THREAD_PRIORITY_LOW : value == 3 ? SDL_THREAD_PRIORITY_NORMAL :
      value < 6 ? SDL_THREAD_PRIORITY_HIGH : SDL_THREAD_PRIORITY_TIME_CRITICAL;
  if (!SDL_SetCurrentThreadPriority(platform))
    return 0;
  ScCurrentSdlPriority = value;
  return 1;
}

// Enter the original target or function, translating an explicit worker exit into an ordinary SDL thread return.
// The executing record survives receiver destruction and publishes completion before releasing its last worker hold.
static int ScRunSdlThread(void *argument) {
  ScSdlThread *worker = static_cast<ScSdlThread *>(argument);
  SDL_LockMutex(worker->mutex);
  worker->id = SDL_GetCurrentThreadID();
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  i32 priority = worker->priority;
  if (worker->owner) {
    worker->owner->threadId = (u32)worker->id;
    priority = worker->owner->priority;
  }
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  SDL_UnlockMutex(worker->mutex);
  ScCurrentSdlThread = worker;
  if (priority != kScThreadDefaultPriority)
    ScApplySdlPriority(priority);
  u32 code;
  try {
    code = worker->target ? worker->target->StartFunction(nullptr) : worker->function(worker->argument);
  } catch (const ScSdlThreadExit &exit) {
    code = exit.code;
  }
  SDL_LockMutex(worker->mutex);
  worker->running = false;
  SDL_BroadcastCondition(worker->condition);
  SDL_UnlockMutex(worker->mutex);
  ScCurrentSdlThread = nullptr;
  ScReleaseSdlThread(worker);
  return (int)code;
}

// Allocate a worker record and publish it before allowing the new thread to enter its game callback.
// Retain the requested priority in that record so startup need not read a destroyed game receiver.
static uptr ScStartSdlThread(ScThread *owner, ScThreadTarget *target, ScThreadFunction function, void *argument) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  i32 priority = owner->priority;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  ScSdlThread *worker = new ScSdlThread{SDL_CreateMutex(), SDL_CreateCondition(), nullptr, owner, target, function,
      argument, priority, 0, 0, 2, 0, true, false};
  if (!worker->mutex || !worker->condition) {
    if (worker->condition)
      SDL_DestroyCondition(worker->condition);
    if (worker->mutex)
      SDL_DestroyMutex(worker->mutex);
    delete worker;
    return 0;
  }
  SDL_LockMutex(worker->mutex);
  worker->thread = SDL_CreateThread(ScRunSdlThread, "SC3 worker", worker);
  if (!worker->thread) {
    SDL_UnlockMutex(worker->mutex);
    SDL_DestroyCondition(worker->condition);
    SDL_DestroyMutex(worker->mutex);
    delete worker;
    return 0;
  }
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  ScSdlThread *old = static_cast<ScSdlThread *>(owner->handle);
  owner->handle = worker;
  owner->threadId = (u32)SDL_GetThreadID(worker->thread);
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  if (old)
    ScReleaseSdlThread(old);
  SDL_DetachThread(worker->thread);
  SDL_UnlockMutex(worker->mutex);
  return reinterpret_cast<uptr>(worker);
}

ScThread::ScThread() : state(0), exitCode(0), handle(nullptr), threadId(0), priority(kScThreadDefaultPriority) {
}

u32 ScThread::Release() {
  delete this;
  return 0;
}

uptr ScThread::Start(ScThreadTarget *target) {
  return IsRunning() ? 0 : ScStartSdlThread(this, target, nullptr, nullptr);
}

uptr ScThread::Start(ScThreadFunction function, void *argument) {
  return IsRunning() ? 0 : ScStartSdlThread(this, nullptr, function, argument);
}

// Clear the actual receiver's handle, ID and exit word before ending the calling worker.
// The receiver may differ from the calling worker's owner, as in the original Windows implementation.
void ScThread::Exit(u32 code) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  ScSdlThread *worker = static_cast<ScSdlThread *>(handle);
  handle = nullptr;
  threadId = 0;
  exitCode = code;
  if (worker)
    worker->owner = nullptr;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  if (worker)
    ScReleaseSdlThread(worker);
  throw ScSdlThreadExit{code};
}

// Record a suspension request; a worker suspending itself waits immediately.
u32 ScThread::Suspend() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return kScThreadSuspendResumeFailed;
  SDL_LockMutex(worker->mutex);
  u32 previous = worker->suspendCount++;
  if (ScCurrentSdlThread == worker)
    while (worker->suspendCount)
      SDL_WaitCondition(worker->condition, worker->mutex);
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
  return previous;
}

// Decrement the suspension count and wake a worker once all requests are released.
u32 ScThread::Resume() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return kScThreadSuspendResumeFailed;
  SDL_LockMutex(worker->mutex);
  u32 previous = worker->suspendCount;
  if (previous && !--worker->suspendCount)
    SDL_BroadcastCondition(worker->condition);
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
  return previous;
}

void ScThread::WaitForWake() {
  WaitCurrentForWake();
}

// Wait using the executing worker record, without accessing its potentially retired game receiver.
void ScThread::WaitCurrentForWake() {
  ScSdlThread *worker = ScCurrentSdlThread;
  if (!worker)
    return;
  SDL_LockMutex(worker->mutex);
  while (!worker->wake)
    SDL_WaitCondition(worker->condition, worker->mutex);
  worker->wake = false;
  SDL_UnlockMutex(worker->mutex);
}

// Pin dispatcher-member use while its borrowed owner is live; callbacks and waits must run outside the pin.
void ScThread::PinCallbackOwner() {
  ScSdlThread *worker = ScCurrentSdlThread;
  assert(worker);
  SDL_LockMutex(worker->mutex);
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  bool live = worker->owner != nullptr;
  if (live)
    ++worker->ownerPins;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  SDL_UnlockMutex(worker->mutex);
  assert(live && "Worker callback returned after its dispatcher was destroyed");
  if (!live)
    std::unreachable();
}

// Release a short owner-use pin and notify a destructor waiting to retire its dispatcher members.
void ScThread::UnpinCallbackOwner() {
  ScSdlThread *worker = ScCurrentSdlThread;
  assert(worker);
  SDL_LockMutex(worker->mutex);
  assert(worker->ownerPins);
  if (!--worker->ownerPins)
    SDL_BroadcastCondition(worker->condition);
  SDL_UnlockMutex(worker->mutex);
}

// Close the borrowed owner link and wait only for member accesses already in progress to leave their pins.
void ScThread::RetireCallbackOwner() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return;
  SDL_LockMutex(worker->mutex);
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  if (worker->owner == this)
    worker->owner = nullptr;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  while (worker->ownerPins)
    SDL_WaitCondition(worker->condition, worker->mutex);
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
}

// Wait outside the drawing lock until pending external suspension requests are released.
void ScThread::PauseCheckpoint() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return;
  SDL_LockMutex(worker->mutex);
  while (worker->suspendCount)
    SDL_WaitCondition(worker->condition, worker->mutex);
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
}

// Check for a suspension request after acquiring the caller's protected drawing lock.
bool ScThread::PausePending() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return false;
  SDL_LockMutex(worker->mutex);
  bool pending = worker->suspendCount != 0;
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
  return pending;
}

// Sleep alertably on the executing platform record without borrowing its ScThread receiver.
// A queued wake can end the wait, including after the creator has retired its receiver.
static u32 ScSleepSdlThread(u32 microseconds) {
  ScSdlThread *worker = ScCurrentSdlThread;
  if (!worker) {
    SDL_Delay(microseconds / 1000);
    return 0;
  }
  SDL_LockMutex(worker->mutex);
  u64 deadline = SDL_GetTicks() + microseconds / 1000;
  while (!worker->wake) {
    u64 now = SDL_GetTicks();
    if (now >= deadline)
      break;
    SDL_WaitConditionTimeout(worker->condition, worker->mutex, (i32)(deadline - now));
  }
  u32 result = worker->wake ? 0xc0u : 0;
  worker->wake = false;
  SDL_UnlockMutex(worker->mutex);
  return result;
}

u32 ScThread::Sleep(u32 microseconds) {
  return ScSleepSdlThread(microseconds);
}

// Queue an alert for the worker's next alertable sleep.
u32 ScThread::Wake() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return 0;
  SDL_LockMutex(worker->mutex);
  worker->wake = true;
  SDL_BroadcastCondition(worker->condition);
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
  return 1;
}

// Wait for a different worker to finish its callback.
i32 ScThread::Wait() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return 0;
  if (worker == ScCurrentSdlThread) {
    ScReleaseSdlThread(worker);
    return 0;
  }
  SDL_LockMutex(worker->mutex);
  while (worker->running)
    SDL_WaitCondition(worker->condition, worker->mutex);
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
  return 1;
}

// Return the cached game priority or the original normal default.
i32 ScThread::GetPriority() {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  i32 value = priority == kScThreadDefaultPriority ? 3 : priority;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  return value;
}

i32 ScThread::SetPriority(i32 value) {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (worker) {
    SDL_LockMutex(worker->mutex);
    bool running = worker->running;
    SDL_UnlockMutex(worker->mutex);
    bool current = worker == ScCurrentSdlThread;
    ScReleaseSdlThread(worker);
    if (running && !current)
      return 0;
    if (current && !ScApplySdlPriority(value))
      return 0;
  }
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  priority = value;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  return 1;
}

// Read the worker's completion flag while holding its condition mutex.
i32 ScThread::IsRunning() {
  ScSdlThread *worker = ScRetainSdlThread(this);
  if (!worker)
    return 0;
  SDL_LockMutex(worker->mutex);
  i32 running = worker->running;
  SDL_UnlockMutex(worker->mutex);
  ScReleaseSdlThread(worker);
  return running;
}

// Preserve the original destructor's non-waiting behavior.
// Retire the borrowed receiver link while the executing worker keeps its own platform-record reference.
ScThread::~ScThread() {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  ScSdlThread *worker = static_cast<ScSdlThread *>(handle);
  if (worker)
    worker->owner = nullptr;
  handle = nullptr;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  if (worker)
    ScReleaseSdlThread(worker);
}

// Publish the retained explicit exit word and current liveness.
i32 ScThread::GetExitCode(u32 *out) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  *out = exitCode;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  return IsRunning();
}

i32 ScThread::GetThreadId(u32 *out) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  *out = threadId;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  return IsRunning();
}

// Publish the borrowed platform worker record and current liveness.
i32 ScThread::GetHandle(void **out) {
  SDL_LockSpinlock(&ScSdlThreadPublicationLock);
  *out = handle;
  SDL_UnlockSpinlock(&ScSdlThreadPublicationLock);
  return IsRunning();
}

void ScThread::SleepCurrentThread(u32 microseconds) {
  ScSleepSdlThread(microseconds);
}

// Suspend the calling managed worker until another thread resumes it.
void ScThread::SuspendCurrentThread() {
  ScSdlThread *worker = ScCurrentSdlThread;
  if (!worker)
    return;
  SDL_LockMutex(worker->mutex);
  ++worker->suspendCount;
  while (worker->suspendCount)
    SDL_WaitCondition(worker->condition, worker->mutex);
  SDL_UnlockMutex(worker->mutex);
}

i32 ScThread::SetCurrentPriority(i32 value) {
  return ScApplySdlPriority(value);
}

i32 ScThread::CurrentPriority() {
  return ScCurrentSdlPriority;
}

void ScThread::WakeForWork() {
  Wake();
}

// Read the SDL worker's stop flag under the same owner lock used to publish shutdown.
bool ScThread::IsWorkRunning(const u8 *running, ScLock *lock) {
  lock->Lock();
  bool active = *running != 0;
  lock->Unlock();
  return active;
}

// Publish the stopped flag under its owner lock, then wake and join the idle SDL worker.
void ScThread::StopWork(u8 *running, ScLock *lock) {
  lock->Lock();
  *running = 0;
  lock->Unlock();
  Wake();
  Wait();
}

// Yield the calling thread without processing an alertable APC.
void ScYieldThread() {
  SDL_Delay(0);
}

// Acquire the existing module-wide bit lock, preserving the backend's contention and memory ordering.
// Contention never processes an alertable APC.
void ScLockSpin(ScSpinLock *lock) {
  SDL_LockSpinlock(lock);
}

// Publish the protected state before releasing the same zero-initialized module bit lock.
// The Windows reference backend retains its original volatile clear.
void ScUnlockSpin(ScSpinLock *lock) {
  SDL_UnlockSpinlock(lock);
}

ScThreadSignal::ScThreadSignal(i32 type) : handle(ScPlatformSignalCreate(type)), manual_reset(type), valid(handle != nullptr) {
}

ScThreadSignal::~ScThreadSignal() {
  ScPlatformSignalDestroy(handle);
}

u32 ScThreadSignal::Release() {
  delete this;
  return 0;
}

i32 ScThreadSignal::Signal() {
  return ScPlatformSignalSet(handle, manual_reset != 0);
}

i32 ScThreadSignal::Broadcast() {
  return ScPlatformSignalSet(handle, false);
}

// Serialize waits through the actual thread-safe base. Zero waits indefinitely; finite microseconds truncate to milliseconds.
i32 ScThreadSignal::TimedWait(u32 microseconds) {
  lock.Lock();
  i32 value = ScPlatformSignalWait(handle, microseconds);
  lock.Unlock();
  return value;
}

i32 ScThreadSignal::IsValid() {
  return valid;
}

i32 ScThreadSignal::Unsignal() {
  return ScPlatformSignalReset(handle);
}

i32 ScThreadSignal::GetEventHandle(void **out) {
  *out = handle;
  return 1;
}

static u32 ScTimerMeasureOverhead();

// Cache the same performance-counter conversion factors initialized by the executable's timer module.
struct ScTimerConversions {
  // Read the frequency once and retain the original conversion multipliers.
  ScTimerConversions() : factors{}, inverse{}, overhead(0) {
    u64 frequency = SDL_GetPerformanceFrequency();
    if (frequency) {
      f64 rate = static_cast<f64>(frequency);
      factors[0] = 1000000000.0 / rate;
      factors[1] = 1000000.0 / rate;
      factors[2] = 1000.0 / rate;
      factors[3] = 1.0 / rate;
      factors[4] = 0.01666666666666667 / rate;
      inverse[0] = rate * 0.000000001;
      inverse[1] = rate * 0.000001;
      inverse[2] = rate * 0.001;
      inverse[3] = rate;
      inverse[4] = rate * 60.0;
      overhead = ScTimerMeasureOverhead();
    }
  }

  f64 factors[5], inverse[5];
  u32 overhead;
};

static ScTimerConversions ScTimerConversionValues;

// Share the executable's counter-frequency conversion values across timer instances.
static const ScTimerConversions &ScTimerFactors() {
  return ScTimerConversionValues;
}

static i64 ScTimerInteger(f64 value) {
  assert(std::isfinite(value) && value >= -9223372036854775808.0 && value < 9223372036854775808.0);
  return static_cast<i64>(value);
}

// Subtract two original counter values with the executable's 64-bit wraparound.
static i64 ScTimerSubtract(i64 first, i64 second) {
  u64 difference = static_cast<u64>(first) - static_cast<u64>(second);
  i64 result;
  std::memcpy(&result, &difference, sizeof(result));
  return result;
}

ScTimer::ScTimer(Resolution value, u32 size)
    : resolution(value), start_time(0), elapsed_time(0), sample_count(0), sample_index(0), sample_buffer_size(0) {
  SetSampleBufferSize(size);
}

ScTimer::~ScTimer() {
}

// Set the sample capacity and clear each entry in the requested buffer.
void ScTimer::SetSampleBufferSize(u32 size) {
  sample_index = 0;
  sample_count = 0;
  sample_buffer_size = size;
  if (samples.size() < size)
    samples.resize(size);
  for (u32 index = 0; index < size; ++index)
    samples[index] = {0, 0};
}

bool ScTimer::IsTimerRunning() const {
  return start_time != 0;
}

u32 ScTimer::GetSampleCount() const {
  return sample_count;
}

void ScTimer::SetResolution(Resolution value) {
  if (resolution != value)
    resolution = value;
}

ScTimer::Resolution ScTimer::GetResolution() const {
  return resolution;
}

// Read the performance counter and write zero when the platform query fails.
void ScTimer::GetCurrentInternal(i64 *out) const {
  *out = static_cast<i64>(SDL_GetPerformanceCounter());
}

// Subtract the saved starting counter with the executable's 64-bit wraparound.
void ScTimer::GetElapsedInternal(i64 *out) const {
  i64 current;
  GetCurrentInternal(&current);
  u64 difference = static_cast<u64>(current) - static_cast<u64>(start_time);
  std::memcpy(out, &difference, sizeof(difference));
}

// Start only a stopped timer, clearing accumulated time before reading the counter.
void ScTimer::Start() {
  if (!IsTimerRunning()) {
    elapsed_time = 0;
    GetCurrentInternal(&start_time);
  }
}

void ScTimer::Stop() {
  if (IsTimerRunning()) {
    i64 delta;
    GetElapsedInternal(&delta);
    u64 total = static_cast<u64>(elapsed_time) + static_cast<u64>(delta);
    std::memcpy(&elapsed_time, &total, sizeof(total));
  }
}

// Measure the original counter-reading overhead with four start, stop and elapsed-counter sequences.
static u32 ScTimerMeasureOverhead() {
  ScTimer timer(static_cast<ScTimer::Resolution>(0), 0);
  i64 previous, current;
  ScYieldThread();
  timer.Start();
  timer.Stop();
  timer.GetElapsedInternal(&previous);
  for (u32 index = 0; index < 3; ++index) {
    timer.Restart();
    timer.Stop();
    timer.GetElapsedInternal(&current);
    if (index < 2)
      previous = current;
  }
  if (current < previous)
    previous = current;
  return static_cast<u32>(previous);
}

// Store the current counter delta in the ring and advance the sample count and starting counter.
// A smaller configured ring retains initialized backing entries, including the original next-entry read at its boundary.
void ScTimer::Sample() {
  i64 delta;
  GetElapsedInternal(&delta);
  assert(sample_buffer_size && "Timer sampling requires a sample buffer");
  if (sample_index < sample_buffer_size) {
    samples[sample_index].first = delta;
    ++sample_index;
    assert(sample_index < samples.size() && "The original next-sample read requires initialized backing storage");
    samples[sample_index].second = ScTimerSubtract(samples[sample_index].first, samples[sample_index - 1].first);
  } else {
    sample_index = 0;
    samples[0].first = delta;
    samples[0].second = ScTimerSubtract(samples[0].first, samples[sample_buffer_size - 1].first);
  }
  if (sample_count < sample_buffer_size)
    ++sample_count;
  u64 next = static_cast<u64>(start_time) + static_cast<u64>(delta);
  std::memcpy(&start_time, &next, sizeof(next));
}

// Average the recorded sample deltas, leaving the output untouched when no sample exists.
void ScTimer::GetAverageCycles(f32 *out) const {
  if (!sample_count)
    return;
  *out = 0;
  for (u32 index = 0; index < sample_count; ++index)
    *out = static_cast<f32>(static_cast<f64>(*out) + static_cast<f64>(samples[index].second));
  *out = static_cast<f32>((1.0 / sample_count) * *out);
}

// Search the original ring for a sample more than one second old and calculate its recorded frequency.
// The current entry may be beyond the configured ring after shrinking, provided its retained storage is initialized.
void ScTimer::GetFrequency(f32 *out) const {
  if (!sample_count)
    return;
  assert(sample_index < samples.size() && "Timer frequency requires an existing backing entry");
  i64 latest = samples[sample_index].first;
  f32 seconds = 0;
  u32 checked = 0, index = sample_buffer_size - 1, limit = sample_index - 1;
  bool found = false;
  while (index > limit && !found) {
    ConvertInternalToSeconds(ScTimerSubtract(latest, samples[index].first), &seconds);
    found = seconds > 1.0f;
    --index;
    ++checked;
  }
  if (found) {
    i64 second;
    ConvertSecondsToInternal(1.0f, &second);
    *out = static_cast<f32>((1.0 / seconds) * static_cast<f64>(second) * checked);
  } else {
    *out = -1.0f;
  }
}

f32 ScTimer::GetOverheadMicroseconds() const {
  return static_cast<f32>(static_cast<f64>(ScTimerFactors().overhead) * ScTimerFactors().factors[1]);
}

// Clear the timer, sample counters and every configured sample entry.
void ScTimer::Reset() {
  elapsed_time = 0;
  start_time = 0;
  sample_index = 0;
  sample_count = 0;
  for (u32 index = 0; index < sample_buffer_size; ++index)
    samples[index] = {0, 0};
}

void ScTimer::Restart() {
  Reset();
  GetCurrentInternal(&start_time);
}

// Convert counter ticks to the configured unit using the original cached multiplier.
void ScTimer::ConvertInternalToUnitResolution(i64 value, i64 *out) const {
  i32 unit = static_cast<i32>(resolution);
  if (unit < 0 || unit > 3)
    unit = 4;
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().factors[unit]);
}

// Convert counter ticks to the configured unit as a floating-point value.
void ScTimer::ConvertInternalToUnitResolution(i64 value, f32 *out) const {
  i32 unit = static_cast<i32>(resolution);
  if (unit < 0 || unit > 3)
    unit = 4;
  *out = static_cast<f32>(static_cast<f64>(value) * ScTimerFactors().factors[unit]);
}

void ScTimer::ConvertUnitResolutionToInternal(f32 value, i64 *out) const {
  i32 unit = static_cast<i32>(resolution);
  if (unit < 0 || unit > 3)
    unit = 4;
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().inverse[unit]);
}

u32 ScTimer::GetElapsedTime() const {
  if (!IsTimerRunning())
    return 0;
  i64 internal, converted;
  GetElapsedInternal(&internal);
  ConvertInternalToUnitResolution(internal, &converted);
  return static_cast<u32>(converted);
}

void ScTimer::GetElapsedTime(f32 *out) const {
  if (IsTimerRunning()) {
    i64 internal;
    GetElapsedInternal(&internal);
    ConvertInternalToUnitResolution(internal, out);
  }
}

void ScTimer::ConvertInternalToCycles(i64 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().factors[0]);
}

void ScTimer::ConvertInternalToCycles(i64 value, f32 *out) const {
  *out = static_cast<f32>(static_cast<f64>(value) * ScTimerFactors().factors[0]);
}

void ScTimer::ConvertCyclesToInternal(f32 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().inverse[0]);
}

void ScTimer::ConvertInternalToMicroseconds(i64 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().factors[1]);
}

void ScTimer::ConvertInternalToMicroseconds(i64 value, f32 *out) const {
  *out = static_cast<f32>(static_cast<f64>(value) * ScTimerFactors().factors[1]);
}

void ScTimer::ConvertMicrosecondsToInternal(f32 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().inverse[1]);
}

void ScTimer::ConvertInternalToMilliseconds(i64 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().factors[2]);
}

void ScTimer::ConvertInternalToMilliseconds(i64 value, f32 *out) const {
  *out = static_cast<f32>(static_cast<f64>(value) * ScTimerFactors().factors[2]);
}

void ScTimer::ConvertMillisecondsToInternal(f32 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().inverse[2]);
}

void ScTimer::ConvertInternalToSeconds(i64 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().factors[3]);
}

void ScTimer::ConvertInternalToSeconds(i64 value, f32 *out) const {
  *out = static_cast<f32>(static_cast<f64>(value) * ScTimerFactors().factors[3]);
}

void ScTimer::ConvertSecondsToInternal(f32 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().inverse[3]);
}

void ScTimer::ConvertInternalToMinutes(i64 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().factors[4]);
}

void ScTimer::ConvertInternalToMinutes(i64 value, f32 *out) const {
  *out = static_cast<f32>(static_cast<f64>(value) * ScTimerFactors().factors[4]);
}

void ScTimer::ConvertMinutesToInternal(f32 value, i64 *out) const {
  *out = ScTimerInteger(static_cast<f64>(value) * ScTimerFactors().inverse[4]);
}

// Wait with the original zero-duration scheduler yields using a separate timer of the same resolution.
void ScTimer::Delay(u32 duration) {
  ScTimer timer(resolution, 0);
  timer.Start();
  while (timer.GetElapsedTime() < duration)
    ScYieldThread();
}
