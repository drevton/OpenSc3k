#ifndef SC3_GRAPHICS_MODULE_H
#define SC3_GRAPHICS_MODULE_H

#include "common/api.h"
#include "common/sc3.h"
#include "common/sc3_array.h"
#include "common/sc3_director.h"

class GxDllDirector;

// Stable ordered class registrations replace the original library tree without changing callback traversal order.
struct GxClassRegistration {
  GxClassRegistration *next, *previous;
  u32 clsid, reserved;
  ScUnknown *(*create)();
};

struct GxDirectorClasses {
  GxDirectorClasses();
  ~GxDirectorClasses();
  void Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved);
  GxClassRegistration head;
};

// Actual director base shared by this DLL's three static directors; children and factory functions are borrowed.
class GxDllDirector : public ScDirector, public ScFrameworkHooks {
public:
  GxDllDirector();
  virtual ~GxDllDirector();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 RemoveRef() override;
  u32 RefCount() override;
  bool InitializeCOM(ScCom *com, ScStringView *path) override;
  bool OnStart(ScCom *com) override;
  void EnumClassObjects(void (*callback)(u32, u32, void *), void *context) override;
  bool GetClassObject(u32 clsid, u32 iid, void **out) override;
  bool CanUnloadNow() override;
  bool OnUnload() override;
  ScFramework *FrameWork() override;
  ScCom *GZCOM() override;
  bool ModulePath(ScStringView *out) override;
  virtual bool AppRegister(ScRegistry *registry);
  virtual void AddDirector(GxDllDirector *director);
  virtual u32 GetDirectorID() const override = 0;
  bool PreFrameWorkInit() override;
  bool PreAppInit() override;
  bool PostAppInit() override;
  bool PreAppShutdown() override;
  bool PostAppShutdown() override;
  bool PostSystemServiceShutdown() override;
  bool AbortiveQuit() override;
  bool OnInstall() override;
  ScArray<GxDllDirector *> children;
  GxDirectorClasses classes;
  u32 references;
  ScCom *com;
  ScString library_path;
  ScFramework *framework;
};

class GxDirector : public GxDllDirector {
public:
  GxDirector();
  bool OnStart(ScCom *com) override;
  u32 GetDirectorID() const override;
};

class GxClassDirector : public GxDllDirector {
public:
  GxClassDirector();
  u32 GetDirectorID() const override;
};

class GxDisplayDirector : public GxDllDirector {
public:
  GxDisplayDirector();
  u32 GetDirectorID() const override;
};

GxDirector *GxGetDirector();
GxClassDirector *GxGetClassDirector();
GxDisplayDirector *GxGetDisplayDirector();
ScUnknown *GxCreateDisplayObject();
ScUnknown *GxCreateBufferObject();
ScUnknown *GxCreateImageObject();
ScUnknown *GxCreateEffectsObject();
ScUnknown *GxCreateColorService();

// Original secondary reference-count base, retaining the actual graphics module before changing its local count.
class GxModuleObject : public ScUnknown {
public:
  GxModuleObject();
  // Final Release destroys the complete object through this virtual destructor after removing the module reference.
  virtual ~GxModuleObject() = default;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual u32 RemoveRef();
  virtual u32 RefCount();

protected:
  u32 references;
};

// Concrete field/property portion of the original24-byte system-service base; no framework callback descriptors are stored.
class GxSystemServiceBase : public ScSystemService, public GxModuleObject {
public:
  GxSystemServiceBase(u32 identifier, i32 priority);
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 GetServiceID() const override;
  void SetServiceID(u32 identifier) override;
  i32 GetServicePriority() const override;
  bool IsServiceRunning() const override;
  void SetServiceRunning(bool running) override;
  bool Init() override;
  bool Shutdown() override;
  bool OnTick(u32 argument) override;
  bool OnIdle(u32 argument) override;
  bool OnInstall() override;

protected:
  u32 identifier;
  i32 priority;
  bool running;
};

// Original cRZTimer::TimerResolution uses a signed DWORD and five explicit unit choices.
enum class GxTimeUnit : i32 {
  Cycles = 0,
  Microseconds = 1,
  Milliseconds = 2,
  Seconds = 3,
  Minutes = 4
};

// Original sixteen-byte tTimerSample is a pair of signed 64-bit values.
struct GxTimerSample {
  i64 first, second;
};

// Recovered embedded timer state, performance-counter reads and sample-window resets.
// Retail cRZTimer declarations establish void timing operations and mutable signed 64-bit timestamp outputs.
class GxTimer {
public:
  GxTimer(GxTimeUnit unit, u32 sample_limit);
  virtual ~GxTimer();
  void SetResolution(GxTimeUnit value);
  bool IsTimerRunning() const;
  void GetCurrentInternal(i64 *out) const;
  void GetElapsedInternal(i64 *out) const;
  void Reset();
  void Start();
  void Restart();
  void Stop();
  void SetSampleBufferSize(u32 sample_limit);

protected:
  GxTimeUnit unit;
  i64 first_tick = 0, second_tick = 0;
  // Original sample vector stores begin, logical end, and allocation end as three pointers.
  GxTimerSample *samples = nullptr, *samples_end = nullptr, *samples_capacity = nullptr;
  u32 sample_count, sample_index, buffer_size;
};
u32 GxTimerComputeBase();
extern u32 GxClockOverhead;
extern f64 GxClockUnits[10];
// The original setup object initializes shared timer conversion factors during module startup.
// The Linux object is one byte; no Windows constructor or consumer accesses an instance field.
class GxTimerSetup {
public:
  GxTimerSetup();
};

#endif
