#include "common/com.h"

#include <cstring>

#include "application/application_core.h"
#include "city/sc_random.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/sc3_director.h"
#include "common/thread.h"

// The COM service registers this identity with its service base.
enum ScComServiceIdentity : u32 {
  kScComServiceId = 0xa3cd8db3,
};

// The service constructor supplies this original startup ordering priority.
enum ScComServiceOrder : i32 {
  kScComServicePriority = 0x989680,
};

ScExeObject::ScExeObject() : references(0) {
}

ScExeObject::~ScExeObject() {
}

bool ScExeObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScUnknown);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScExeObject::AddRef() {
  ApGetDirector()->AddRef();
  return ++references;
}

// Choose final deletion from the old count, then remove the module reference before object destruction.
// Other counts decrement after the same module call, including the original DWORD wrap from zero.
u32 ScExeObject::Release() {
  if (references == 1) {
    ApGetDirector()->RemoveRef();
    delete this;
    return 0;
  }
  ApGetDirector()->RemoveRef();
  return --references;
}

// Remove an existing reference without deleting the receiver. An already empty count does not touch the module director.
u32 ScExeObject::RemoveRef() {
  if (references) {
    ApGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScExeObject::RefCount() {
  return references;
}

ScServiceBase::ScServiceBase(u32 id, i32 order) : identifier(id), priority(order), service_running(false) {
}

ScServiceBase::~ScServiceBase() {
}

bool ScServiceBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  return ScExeObject::QueryInterface(iid, out);
}

u32 ScServiceBase::AddRef() {
  return ScExeObject::AddRef();
}

u32 ScServiceBase::Release() {
  return ScExeObject::Release();
}

u32 ScServiceBase::GetServiceID() const {
  return identifier;
}

void ScServiceBase::SetServiceID(u32 value) {
  identifier = value;
}

i32 ScServiceBase::GetServicePriority() const {
  return priority;
}

bool ScServiceBase::IsServiceRunning() const {
  return service_running;
}

void ScServiceBase::SetServiceRunning(bool value) {
  service_running = value;
}

bool ScServiceBase::Init() {
  return true;
}

bool ScServiceBase::Shutdown() {
  return true;
}

bool ScServiceBase::OnTick(u32) {
  return true;
}

bool ScServiceBase::OnIdle(u32) {
  return true;
}

bool ScServiceBase::OnInstall() {
  return true;
}

ScComLibrary::ScComLibrary(const ScString &name) : loaded(false), director(nullptr), path(name), linked_director(nullptr) {
}

ScComLibrary::ScComLibrary(ScDirector *linked, const ScString &name)
    : loaded(false), director(nullptr), path(name), linked_director(linked) {
}

ScComLibrary::ScComLibrary(const ScComLibrary &other)
    : loaded(other.loaded), director(other.director), path(other.path), linked_director(other.linked_director) {
}

// Compare complete String ranges with unsigned-byte ordering, then compare their lengths when prefixes are equal.
// Case and embedded zero bytes remain significant for library-set identity.
bool ScComLibrary::operator<(const ScComLibrary &other) const {
  u32 first = path.Strlen(), second = other.path.Strlen(), common = first < second ? first : second;
  int order = memcmp(path.begin, other.path.begin, common);
  return order < 0 || (!order && first < second);
}

// Initialize a linked module once through its borrowed director and the real COM/name arguments.
bool ScComLibrary::Load() {
  if (loaded)
    return true;
  ScDirector *object = linked_director;
  if (!object)
    return false;
  director = object;
  loaded = true;
  if (object->InitializeCOM(ApGetDirector()->GZCOM(), &path))
    return true;
  Free();
  return false;
}

// Clear this library record without unloading code from the executable.
// Preserve the original lack of an extra COM Release or OnUnload callback in this phase.
bool ScComLibrary::Free() {
  if (loaded) {
    director = nullptr;
    loaded = false;
  }
  return true;
}

bool ScComLibrary::IsLoaded() const {
  return loaded;
}

bool ScComCore::CanUnloadLibrary(ScComLibrary *library) {
  return !library->IsLoaded() || library->GetDirector()->CanUnloadNow();
}

// Borrow the linked module's director without adding a COM reference. Executable code remains resident independently.
ScDirector *ScComLibrary::GetDirector() const {
  return director;
}

ScComCore::ScComCore()
    : ScServiceBase(kScComServiceId, kScComServicePriority), initialized(false), current_library(nullptr) {
}

ScComCore::~ScComCore() {
}

bool ScComCore::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScCom, ScCom);
  return ScServiceBase::QueryInterface(iid, out);
}

u32 ScComCore::AddRef() {
  return ScServiceBase::AddRef();
}

u32 ScComCore::Release() {
  return ScExeObject::RemoveRef();
}

// Capture a class's library while locked, then load and call its live director after unlocking.
// A miss remains a miss across unlock: class1000 queries the current framework, all others use the executable director.
bool ScComCore::GetClassObject(u32 class_id, u32 iid, void **out) {
  lock.Lock();
  auto found = classes.find(class_id);
  bool registered = found != classes.end();
  ScComLibrary *library = registered ? found->second.library : nullptr;
  lock.Unlock();
  if (!registered) {
    if (class_id == 1000)
      return ScFrameworkCore::FrameWork()->QueryInterface(iid, out);
    return ApGetDirector()->GetClassObject(class_id, iid, out);
  }
  return GetLibObject(library, class_id, iid, out);
}

// Delegate class creation to its live director.
bool ScComCore::GetLibObject(ScComLibrary *library, u32 class_id, u32 iid, void **out) {
  if (!library->IsLoaded() && !library->Load())
    return false;
  return library->GetDirector()->GetClassObject(class_id, iid, out);
}

// Borrow the process's current framework, independently of which framework originally constructed this COM object.
ScFramework *ScComCore::FrameWork() {
  return ScFrameworkCore::FrameWork();
}

bool ScComCore::AddLibrary(const ScStringView &) {
  return false;
}

// Register a linked director once under its logical identity, retaining failed first insertions.
// Enumerate its classes before OnStart, whose result does not replace successful registration.
bool ScComCore::RegisterDirector(ScDirector *director, const ScString &name) {
  ScComLibrary candidate(director, name);
  lock.Lock();
  if (libraries.find(candidate) != libraries.end()) {
    lock.Unlock();
    return true;
  }
  auto inserted = libraries.insert(candidate);
  // Only load state changes; the set's ordering name is never modified.
  ScComLibrary *library = const_cast<ScComLibrary *>(&*inserted.first);
  lock.Unlock();
  if (!UpdateClassRegistry(library))
    return false;
  library->GetDirector()->OnStart(this);
  return true;
}

// Load before entering the region, then publish the current enumeration library without restoring older nested state.
// Capture the director before locking and ignore its enumeration result, matching the actual void callback interface.
bool ScComCore::UpdateClassRegistry(ScComLibrary *library) {
  if (!library->Load())
    return false;
  ScDirector *object = library->GetDirector();
  lock.Lock();
  current_library = library;
  object->EnumClassObjects(AddEntryCallback, this);
  lock.Unlock();
  return true;
}

// The actual external director callback keeps the highest unsigned version for each class identifier.
// Read the live current-library field after any old entry is erased; nested enumeration is deliberately not restored.
void ScComCore::AddEntryCallback(u32 class_id, u32 version, void *context) {
  ScComCore *owner = static_cast<ScComCore *>(context);
  auto found = owner->classes.find(class_id);
  if (found != owner->classes.end() && found->second.version < version)
    owner->classes.erase(found);
  owner->classes.insert({class_id, {version, owner->current_library}});
}

// Visit live libraries in name order and unload those whose directors report no remaining owners.
// Hold the original recursive region throughout callbacks and preserve the explicit unlock point.
void ScComCore::FreeUnusedLibraries() {
  lock.Lock();
  auto end = libraries.end();
  for (auto entry = libraries.begin(); entry != end; ++entry) {
    ScComLibrary *library = const_cast<ScComLibrary *>(&*entry);
    if (library->IsLoaded() && CanUnloadLibrary(library))
      library->Free();
  }
  lock.Unlock();
}

// Preserve the separate current-time/date reads, wrapped DWORD arithmetic and successful null-output no-op.
bool ScComCore::CreateGuid(u32 *out) {
  if (out) {
    CtTime now;
    CtDate current(now), epoch(2, 2, 1997), today;
    CtTime anchor(today, 7, 5, 0);
    u32 elapsed = 86400u * current.serial - 86400u * epoch.serial;
    ScRandom random(ScMultimediaMilliseconds());
    *out = (random.Bounded(8) << 29) | (elapsed - anchor.timestamp + now.timestamp);
  }
  return true;
}

// Initialize the executable director with the real TheApp.exe name before setting the COM initialization flag.
// Ordinary director refusal returns false; a completed prior initialization succeeds without another callback.
bool ScComCore::RealInit() {
  if (!initialized) {
    ScString name("TheApp.exe");
    if (!ApGetDirector()->InitializeCOM(this, &name))
      return false;
    initialized = true;
  }
  return true;
}

// Clear initialized before shutdown work. The original closes copies of the library records, retaining stored record flags.
// No additional director release or class-map clearing is synthesized by this phase.
void ScComCore::RealShutdown() {
  if (!initialized)
    return;
  initialized = false;
  FreeAllLibraries();
}

// Close copies of the ordered library records while holding the recursive region. Stored flags and handles remain unchanged.
void ScComCore::FreeAllLibraries() {
  lock.Lock();
  auto end = libraries.end();
  for (auto entry = libraries.begin(); entry != end; ++entry) {
    ScComLibrary copy(*entry);
    if (copy.IsLoaded())
      copy.Free();
  }
  lock.Unlock();
}

void ScComCore::SetServiceRunning(bool value) {
  ScServiceBase::SetServiceRunning(value);
}
