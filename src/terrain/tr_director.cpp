#include "terrain/tr_director.h"

#include "application/api.h"
#include "application/application_core.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/thread.h"
#include "common/value.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "terrain/api.h"
#include "terrain/tr_commands.h"
#include "terrain/tr_dirt_bag.h"
#include "terrain/tr_generator.h"
#include "terrain/tr_thumbnail.h"
#include "window/api.h"

ScLock TrTerrainLock;

// This is the distinct reference-counter lock, not the recursive terrain operation lock.
static ScLock TrReferenceLock;

// Serialize only the original DWORD counter adjustment, preserving wrap and releasing before any external callbacks.
void TrAdjustReference(u32 *counter, i32 delta) {
  TrReferenceLock.Lock();
  *counter += (u32)delta;
  TrReferenceLock.Unlock();
}

static ScMessageServer *TrMessageServer;
static GxGraphics *TrGraphics;
static RsFileSystem *TrFileSystem;
static RsManager *TrResourceManager;
static ScLanguageManager *TrLanguageManager;
static WnWindowManager *TrWindowManager;
static bool TrApplicationInitialized;
// Keep the actual QueryInterface output slot directly writable during reentrant acquisition.
static void *TrApplication;

// Obtain the real module director's framework pointer. This does not acquire another interface reference.
ScFramework *TrGetFramework() {
  return TrGetDirector()->FrameWork();
}

// Resolve the message service only while its cache is null, then publish the cache before dropping the acquired reference.
// A reentrant Release observes the cached object; unsuccessful acquisition leaves the next call free to retry.
ScMessageServer *TrGetMessageServer() {
  if (!TrMessageServer) {
    void *object = nullptr;
    TrGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *server = static_cast<ScMessageServer *>(object);
    TrMessageServer = server;
    if (server)
      server->Release();
  }
  return TrMessageServer;
}

// Cache the actual graphics interface before releasing its framework acquisition reference.
// A null acquisition retries on the next call, while reentrant releases observe the published cache.
GxGraphics *TrGetGraphics() {
  if (!TrGraphics) {
    void *object = nullptr;
    TrGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &object);
    GxGraphics *graphics = static_cast<GxGraphics *>(object);
    TrGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return TrGraphics;
}

// Acquire the game application once, setting the initialization guard before entering external code.
// QueryInterface publishes directly into the cache; recursive calls can observe it before acquisition returns.
ApGameInterface *TrGetApplication() {
  if (!TrApplicationInitialized) {
    TrApplicationInitialized = true;
    TrGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &TrApplication);
  }
  return static_cast<ApGameInterface *>(TrApplication);
}

// Ask the cached application for its current city on every call. Failed application acquisition leaves no city.
CtCity *TrGetCity() {
  ApGameInterface *application = TrGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Cache the real filesystem service before releasing its acquisition reference, ignoring the lookup Boolean.
// Missing output retries on the next call; reentrant Release sees the already published borrowed interface.
RsFileSystem *TrGetFileSystem() {
  if (!TrFileSystem) {
    void *object = nullptr;
    TrGetFramework()->GetSystemService(kRsFileSystemServiceId, IID_RsFileSystem, &object);
    TrFileSystem = static_cast<RsFileSystem *>(object);
    if (object)
      static_cast<RsFileSystem *>(object)->Release();
  }
  return TrFileSystem;
}

// Publish the borrowed resource manager before releasing its acquired service reference; a null result retries next time.
RsManager *TrGetResourceManager() {
  if (!TrResourceManager) {
    void *object = nullptr;
    TrGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    TrResourceManager = static_cast<RsManager *>(object);
    if (object)
      static_cast<RsManager *>(object)->Release();
  }
  return TrResourceManager;
}

// Cache the language manager before releasing its temporary framework acquisition reference.
// Missing output retries next time, while recursive release callbacks observe the already published manager.
ScLanguageManager *TrGetLanguageManager() {
  if (!TrLanguageManager) {
    void *object = nullptr;
    TrGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    TrLanguageManager = static_cast<ScLanguageManager *>(object);
    if (object)
      static_cast<ScLanguageManager *>(object)->Release();
  }
  return TrLanguageManager;
}

// Borrow the module director's COM registry directly, without an additional lookup cache or interface reference.
ScCom *TrGetCom() {
  return TrGetDirector()->GZCOM();
}

// Ask the acquired game application for its current city-window interface on each call.
// A missing application leaves no window; the returned interface remains borrowed.
SpCityWindowView *TrGetCityWindow() {
  ApGameInterface *application = TrGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Publish the borrowed window-manager cache before releasing its temporary service acquisition reference.
// A null acquisition retries; recursive releases observe the published manager without another framework lookup.
WnWindowManager *TrGetWindowManager() {
  if (!TrWindowManager) {
    void *object = nullptr;
    TrGetFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    WnWindowManager *manager = static_cast<WnWindowManager *>(object);
    TrWindowManager = manager;
    if (manager)
      manager->Release();
  }
  return TrWindowManager;
}

// Register the four actual terrain classes without constructing any game receiver or acquiring its ownership.
TrDirector::TrDirector() {
  classes.Insert(kTrDirtBagClassId, CreateDirtBag, 0);
  classes.Insert(kTrToolFactoryClassId, CreateToolFactory, 0);
  classes.Insert(kTrGeneratorClassId, CreateGenerator, 0);
  classes.Insert(kTrNeighborMapClassId, CreateNeighborMap, 0);
}

TrDirector::~TrDirector() {
}

u32 TrDirector::GetDirectorID() const {
  return kTrDirectorId;
}

// Convert the terrain-tool class and zero argument into a command name, registering only a successful conversion.
// Preserve the original empty destination and key/value construction order, including a registry's false result.
bool TrDirector::AppRegister(ScRegistry *registry) {
  ScString name, key("0x822a1e4b"), value("0");
  if (!registry->MakeRegistryName("cogamecmd", key, value, &name))
    return false;
  return registry->Register(name);
}

// Construct the real terrain receiver for its module class registration, without querying or retaining it.
// Publish its primary Unknown identity; the director performs the subsequent interface query.
ScUnknown *TrDirector::CreateDirtBag() {
  return static_cast<ScDirtBag *>(new TrDirtBag);
}

// Construct the actual tool factory with the retained-child base's zero reference count and empty child sequence.
// Construction acquires no module reference; the director then queries and retains the requested factory identity.
ScUnknown *TrDirector::CreateToolFactory() {
  return new TrToolFactory;
}

// Construct the actual generator with zero references, null planes and its original unset dimensions and threshold.
// The director queries the requested interface afterward; construction itself does not retain the module.
ScUnknown *TrDirector::CreateGenerator() {
  return new TrGenerator;
}

// Construct the neighbor-map generator through its original static director callback without another query or reference.
ScUnknown *TrDirector::CreateNeighborMap() {
  return new TrNeighborMap;
}

// Construct the actual process-lifetime director on its first lookup and return the same primary identity thereafter.
// Native static storage retires registrations and path storage at exit, matching the original atexit callback.
ScDirector *TrGetDirector() {
  static TrDirector director;
  return &director;
}

#if !defined(SC3_MONOLITHIC)
// Expose the original DLL loader entry through its real C ABI and the shared primary director interface.
// The exported jump and the internal getter share the same static receiver and reference state.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return TrGetDirector();
}
#endif

TrDirectorClasses::TrDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
TrDirectorClasses::~TrDirectorClasses() {
  while (head.next != &head) {
    TrClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void TrDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  TrClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  TrClassRegistration *entry = new TrClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

TrDllDirector::TrDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

TrDllDirector::~TrDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool TrDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 TrDllDirector::AddRef() {
  return ++references;
}

u32 TrDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 TrDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 TrDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *TrDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *TrDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool TrDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->InitializeCOM(input, path);
  return true;
}

// Initialize a borrowed child, recursively append its own children, then append the child without a retain.
void TrDllDirector::AddDirector(TrDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < director->children.size; ++index)
    AddDirector(director->children.data[index]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void TrDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->EnumClassObjects(callback, context);
  for (TrClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool TrDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < children.size; ++index)
    if (children.data[index]->GetClassObject(clsid, iid, out))
      return true;
  for (TrClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
    if (entry->clsid != clsid)
      continue;
    ScUnknown *object = entry->create();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// Let live children veto unloading before checking this director's own reference word.
// The installed PC body differs from the available SDK's reference-count shortcut.
bool TrDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool TrDllDirector::OnUnload() {
  return true;
}

bool TrDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool TrDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool TrDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  TrGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool TrDllDirector::PreFrameWorkInit() {
  return true;
}

bool TrDllDirector::PreAppInit() {
  return true;
}

bool TrDllDirector::PostAppInit() {
  return true;
}

bool TrDllDirector::PreAppShutdown() {
  return true;
}

bool TrDllDirector::PostAppShutdown() {
  return true;
}

bool TrDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool TrDllDirector::AbortiveQuit() {
  return true;
}

bool TrDllDirector::OnInstall() {
  return true;
}

TrFactory::TrFactory() : references(0) {
}

// Publish the primary factory identity before virtual retention; unsupported IDs clear the output.
bool TrFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment this receiver before retaining its module, then return the live count after the external callback.
u32 TrFactory::AddRef() {
  ++references;
  TrGetDirector()->AddRef();
  return references;
}

// Decrement before retiring the module reference and reload the count after that callback.
// Zero destroys the real factory and its retained children; unsigned underflow is preserved rather than guarded.
u32 TrFactory::Release() {
  --references;
  TrGetDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Ask retained children in order using the original three arguments and stop on their first successful publication.
// Observe the live end while retaining the current cursor, then reach the actual derived creation method on failure.
bool TrFactory::GetCommand(u32 class_id, u32 iid, void **out) {
  ScCommandFactory **cursor = children.data;
  for (u32 index = 0; index < children.size; ++index, ++cursor)
    if ((*cursor)->GetCommand(class_id, iid, out))
      return true;
  return CreateCommand(class_id, iid, out);
}

// Reject the original null child case; otherwise retain the supplied interface before appending it to owned storage.
bool TrFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

// The original null child terminates this walk; native array destruction then frees the retained pointer storage.
TrFactory::~TrFactory() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}
