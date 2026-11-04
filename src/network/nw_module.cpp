#include "network/nw_module.h"

#include <stdlib.h>

#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/thread.h"
#include "common/value.h"
#include "network/nw_tool.h"
#include "resources/api.h"
#include "window/api.h"

NwDirectorClasses::NwDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
NwDirectorClasses::~NwDirectorClasses() {
  while (head.next != &head) {
    NwClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void NwDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  NwClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  NwClassRegistration *entry = new NwClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

NwDllDirector::NwDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

NwDllDirector::~NwDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool NwDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 NwDllDirector::AddRef() {
  return ++references;
}

u32 NwDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 NwDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 NwDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *NwDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *NwDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool NwDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
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
void NwDllDirector::AddDirector(NwDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < director->children.size; ++index)
    AddDirector(director->children.data[index]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void NwDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->EnumClassObjects(callback, context);
  for (NwClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool NwDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < children.size; ++index)
    if (children.data[index]->GetClassObject(clsid, iid, out))
      return true;
  for (NwClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
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
bool NwDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool NwDllDirector::OnUnload() {
  return true;
}

bool NwDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool NwDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool NwDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  NwGetFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool NwDllDirector::PreFrameWorkInit() {
  return true;
}

bool NwDllDirector::PreAppInit() {
  return true;
}

bool NwDllDirector::PostAppInit() {
  return true;
}

bool NwDllDirector::PreAppShutdown() {
  return true;
}

bool NwDllDirector::PostAppShutdown() {
  return true;
}

bool NwDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool NwDllDirector::AbortiveQuit() {
  return true;
}

bool NwDllDirector::OnInstall() {
  return true;
}

NwDirector::NwDirector() {
  classes.Insert(kNwManagerClassId, NwCreateManager, 0);
  classes.Insert(kNwPieceAttribClassId, NwCreateAttributes, 0);
}

NwDirector::~NwDirector() {
}

u32 NwDirector::GetDirectorID() const {
  return kNwDirectorId;
}

bool NwDirector::AppRegister(ScRegistry *) {
  return true;
}

alignas(NwDirector) static u8 NwDirectorStorage[sizeof(NwDirector)];
static u8 NwDirectorInitFlags;

static void NwDestroyDirector() {
  reinterpret_cast<NwDirector *>(NwDirectorStorage)->~NwDirector();
}

// Set the lazy guard before constructing the actual static module object, then register its ordinary destruction.
ScDirector *NwGetDirector() {
  if (!(NwDirectorInitFlags & 1)) {
    NwDirectorInitFlags |= 1;
    new (NwDirectorStorage) NwDirector;
    atexit(NwDestroyDirector);
  }
  return reinterpret_cast<NwDirector *>(NwDirectorStorage);
}

NwFactory::NwFactory() : references(0) {
}

// Publish the actual unknown/factory interface and retain it through the live virtual AddRef. Unsupported interface IDs
// clear the output.
bool NwFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the receiver first, retain the director next, then return the live receiver count after that callback.
u32 NwFactory::AddRef() {
  ++references;
  NwGetDirector()->AddRef();
  return references;
}

// Remove the receiver reference before the module reference, then decide deletion from the live field.
// Final release destroys this factory and its retained children before returning zero.
u32 NwFactory::Release() {
  --references;
  NwGetDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Give retained children the original three arguments in order, stopping at their first successful publication.
// Reload the live range while scanning, and reach this factory's real CreateCommand override only after all children fail.
bool NwFactory::GetCommand(u32 classId, u32 iid, void **out) {
  for (u32 i = 0; i < children.size; ++i)
    if (children.data[i]->GetCommand(classId, iid, out))
      return true;
  return CreateCommand(classId, iid, out);
}

// Reject a null child as the original does; otherwise retain it before appending the actual interface pointer.
bool NwFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

NwFactory::~NwFactory() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}

NwCommandFactory::~NwCommandFactory() {
}

// Construct only the seven registered network commands and let the actual object publish its requested interface.
// Failed queries destroy the unretained complete tool before clearing the output; unknown classes clear it directly.
bool NwCommandFactory::CreateCommand(u32 classId, u32 iid, void **out) {
  NwTool *tool;
  switch (classId) {
  case kNwDiagonalToolClassId:
    tool = new NwDiagonalTool();
    break;
  case kNwPipeToolClassId:
    tool = new NwPipeTool();
    break;
  case kNwHighwayToolClassId:
    tool = new NwHighwayTool();
    break;
  case kNwRailToolClassId:
    tool = new NwRailTool();
    break;
  case kNwPowerToolClassId:
    tool = new NwPowerTool();
    break;
  case kNwRoadToolClassId:
    tool = new NwRoadTool();
    break;
  case kNwSubwayToolClassId:
    tool = new NwSubwayTool();
    break;
  default:
    *out = nullptr;
    return false;
  }
  if (tool->QueryInterface(iid, out))
    return true;
  delete tool;
  *out = nullptr;
  return false;
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Expose the original DLL loader ABI and borrow the same lazy network director without retaining it.
// SIMNTWRK.def exports the undecorated name at ordinal one; static integration omits this per-DLL symbol.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return NwGetDirector();
}
#endif

NwRandom *NwDecorationRandom = nullptr;

// Publish the seed and its initial copy before clearing the auxiliary field.
NwRandom::NwRandom(u32 value) {
  if (value == kScRandomSeedFromClock)
    value = ScMultimediaMilliseconds();
  seed = value;
  baseSeed = value;
  auxiliary = 0;
}

// Advance the actual seed with the source unsigned product and return all thirty-two shifted result bits.
u32 NwRandom::Next() {
  u64 product = (u64)kScRandomMultiplier * seed + kScRandomIncrement;
  seed = (u32)product;
  return (u32)(product >> 16);
}

// Small bounds use a remainder; large bounds reject whole generated values above the inclusive upper limit.
u32 NwRandom::Bounded(u32 bound) {
  if (!bound)
    return 0;
  u32 result;
  do {
    result = Next();
  } while (bound >= kScRandomModuloThreshold && result > bound);
  return bound < kScRandomModuloThreshold ? result % bound : result;
}

u8 NwApplicationInitFlags = 0;
void *NwApplicationCache = nullptr;
RsFileSystem *NwFileSystemCache = nullptr;
ScMessageServer *NwMessageServerCache = nullptr;
RsManager *NwResourceManagerCache = nullptr;
WnWindowManager *NwWindowManagerCache = nullptr;
ScLanguageManager *NwLanguageManagerCache = nullptr;
WnResources *NwWindowResourcesCache = nullptr;

// Cache the framework's published resource manager before releasing its temporary service reference.
// A null live cache retries on the next request, including when Release replaces the published cache.
RsManager *NwGetResourceManager() {
  if (!NwResourceManagerCache) {
    void *object = nullptr;
    NwGetFrameWork()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    NwResourceManagerCache = static_cast<RsManager *>(object);
    if (object)
      static_cast<RsManager *>(object)->Release();
  }
  return NwResourceManagerCache;
}

// Borrow the framework through the actual lazy network director on every request.
// The director owns this pointer; this call does not retain it.
ScFramework *NwGetFrameWork() {
  return NwGetDirector()->FrameWork();
}

// Borrow the COM service through the actual module director without changing its reference count.
ScCom *NwGetCom() {
  return NwGetDirector()->GZCOM();
}

// Cache the published filesystem interface regardless of the service lookup result, then release the temporary reference.
// Return the live cache after Release, preserving callback-induced changes and retrying future lookups when it is null.
RsFileSystem *NwGetFileSystem() {
  if (!NwFileSystemCache) {
    void *object = nullptr;
    NwGetFrameWork()->GetSystemService(kRsFileSystemServiceId, IID_RsFileSystem, &object);
    NwFileSystemCache = static_cast<RsFileSystem *>(object);
    if (object)
      static_cast<RsFileSystem *>(object)->Release();
  }
  return NwFileSystemCache;
}

// Publish the original message-server cache before releasing the framework's temporary reference.
// Ignore the lookup Boolean and return the live cache after callbacks, retrying a missing service next time.
ScMessageServer *NwGetMessageServer() {
  if (!NwMessageServerCache) {
    void *object = nullptr;
    NwGetFrameWork()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    NwMessageServerCache = static_cast<ScMessageServer *>(object);
    if (object)
      static_cast<ScMessageServer *>(object)->Release();
  }
  return NwMessageServerCache;
}

static void NwApplicationExit() {
}

// Set the one-time guard before querying the framework application into the actual live cache slot.
// Ignore the query result and borrow its published interface, preserving re-entry and failed-query publication.
ApGameInterface *NwGetApplication() {
  if (!(NwApplicationInitFlags & 1)) {
    NwApplicationInitFlags |= 1;
    NwGetFrameWork()->Application()->QueryInterface(IID_ApGameInterface, &NwApplicationCache);
    atexit(NwApplicationExit);
  }
  return static_cast<ApGameInterface *>(NwApplicationCache);
}

// Borrow the application's current attribute-cache view without retaining it or adding a second cache.
// A missing application returns null; the original binding caller retains its own startup invariant.
OcAttribCache *NwGetOccupantAttribCache() {
  ApGameInterface *application = NwGetApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Borrow the application's current city, returning null when the actual application view is unavailable.
CtCity *NwGetCity() {
  ApGameInterface *application = NwGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the application's actual city-window view through SDK GetISC3WinCityView, without another cache or reference.
SpCityWindowView *NwGetCityWindow() {
  ApGameInterface *application = NwGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Borrow the current view from the live city window, preserving the original null window result.
SpViewInterface *NwGetCurrentView() {
  SpCityWindowView *window = NwGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Publish the borrowed window-manager service before releasing its temporary framework lookup reference.
// Ignore the service Boolean and return the live cache after Release, retrying future calls when it is null.
WnWindowManager *NwGetWindowManager() {
  if (!NwWindowManagerCache) {
    void *object = nullptr;
    NwGetFrameWork()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    NwWindowManagerCache = static_cast<WnWindowManager *>(object);
    if (object)
      static_cast<WnWindowManager *>(object)->Release();
  }
  return NwWindowManagerCache;
}

// Publish the borrowed language manager before releasing the temporary service reference, retrying a null live cache.
ScLanguageManager *NwGetLanguageManager() {
  if (!NwLanguageManagerCache) {
    void *object = nullptr;
    NwGetFrameWork()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &object);
    NwLanguageManagerCache = static_cast<ScLanguageManager *>(object);
    if (object)
      static_cast<ScLanguageManager *>(object)->Release();
  }
  return NwLanguageManagerCache;
}

// Cache the actual window-resource service before releasing its temporary reference and return the live borrowed view.
WnResources *NwGetWindowResources() {
  if (!NwWindowResourcesCache) {
    void *object = nullptr;
    NwGetFrameWork()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
    NwWindowResourcesCache = static_cast<WnResources *>(object);
    if (object)
      static_cast<WnResources *>(object)->Release();
  }
  return NwWindowResourcesCache;
}

// Borrow the current application's agent tree, returning null when the cached application view is unavailable.
CtAgentTree *NwGetAgentTypeTree() {
  ApGameInterface *application = NwGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Borrow the current city definition from the application, returning null when its cached view is unavailable.
CtDefinition *NwGetCityDefinition() {
  ApGameInterface *application = NwGetApplication();
  return application ? application->GetAppCityDefinition() : nullptr;
}

static u8 NwTransitApplicationFlags;
static void *NwTransitApplicationCache;

// Register the five original factory entries after constructing the common director's collections and path.
// The common ordered registry preserves ascending class-ID enumeration while retaining actual factory identities.
NwTransitDirector::NwTransitDirector() {
  AddClass(kNwTransitLayerClassId, NwCreateTransitLayer, 0);
  AddClass(kNwRailStationClassId, NwCreateRailStation, 0);
  AddClass(kNwSubwayRailStationClassId, NwCreateSubwayRailStation, 0);
  AddClass(kNwSubwayStationClassId, NwCreateSubwayStation, 0);
  AddClass(kNwBusStopClassId, NwCreateBusStop, 0);
}

NwTransitDirector::~NwTransitDirector() {
}

u32 NwTransitDirector::GetDirectorID() const {
  return kNwTransitDirectorId;
}

bool NwTransitDirector::AppRegister(ScRegistry *) {
  return true;
}

class NwTransitRegistryRef {
public:
  // Acquire the module's original registry service through the escaped output slot, independently of success.
  // This scope is the installed common OnStart temporary, not a transit simulation dependency adapter.
  NwTransitRegistryRef() : object(nullptr) {
    NwGetTransitFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &object);
  }

  // Release the live output after AppRegister, including a replacement published through the acquisition slot.
  ~NwTransitRegistryRef() {
    if (object)
      static_cast<ScRegistry *>(object)->Release();
  }

  void *object;
};

bool NwTransitDirector::OnStart(ScCom *) {
  NwTransitRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.object));
}

// Borrow the original lazily constructed static director, whose destructor runs through the C runtime at exit.
// Registration construction has no external callbacks and does not acquire a director reference.
ScDirector *NwGetTransitDirector() {
  static NwTransitDirector director;
  return &director;
}

// Borrow this module's cached framework through the actual director virtual operation.
ScFramework *NwGetTransitFramework() {
  return NwGetTransitDirector()->FrameWork();
}

// Publish the module's initialization guard before querying the application's actual escaped output slot.
// Failed queries preserve their independent publication; the original exit callback performs no cleanup.
ApGameInterface *NwGetTransitApplication() {
  if (!(NwTransitApplicationFlags & 1)) {
    NwTransitApplicationFlags |= 1;
    NwGetTransitFramework()->Application()->QueryInterface(IID_ApGameInterface, &NwTransitApplicationCache);
  }
  return static_cast<ApGameInterface *>(NwTransitApplicationCache);
}

// Borrow the current city through the actual application interface, preserving the original absent-application result.
CtCity *NwGetTransitCity() {
  ApGameInterface *application = NwGetTransitApplication();
  return application ? application->GetCity() : nullptr;
}

// The original service-holder temporary owns its escaped QI output until the cached borrowed pointer is published.
class NwTransitServiceRef {
public:
  // Query the actual system service into the live output slot; a refused query may still publish an owned interface.
  NwTransitServiceRef(u32 service, u32 iid) : object(nullptr) {
    NwGetTransitFramework()->GetSystemService(service, iid, &object);
  }

  // Release the live temporary output after cache publication, without adding a second permanent module hold.
  ~NwTransitServiceRef() {
    if (object)
      static_cast<ScUnknown *>(object)->Release();
  }

  void *object;
};

static ScLanguageManager *NwTransitLanguageManager;
static RsManager *NwTransitResourceManager;

// Cache the original borrowed language-manager service before releasing its acquisition temporary.
// A null cache retries the original lookup rather than inventing a permanent failure guard.
ScLanguageManager *NwGetTransitLanguageManager() {
  if (!NwTransitLanguageManager) {
    NwTransitServiceRef service(kScLanguageManagerServiceId, IID_ScLanguageManager);
    NwTransitLanguageManager = static_cast<ScLanguageManager *>(service.object);
  }
  return NwTransitLanguageManager;
}

// Borrow this DLL's original resource-manager service, publishing its cache before the temporary Release callback.
RsManager *NwGetTransitResourceManager() {
  if (!NwTransitResourceManager) {
    NwTransitServiceRef service(kRsManagerServiceId, IID_RsManager);
    NwTransitResourceManager = static_cast<RsManager *>(service.object);
  }
  return NwTransitResourceManager;
}

// Borrow the application's actual occupant cache, preserving the original null result when no application is available.
OcAttribCache *NwGetTransitAttribCache() {
  ApGameInterface *application = NwGetTransitApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

static WnResources *NwTransitWindowResources;

// Publish the borrowed window-resource cache before releasing its actual escaped service output.
// A null cache repeats acquisition on the next call, as in the installed body.
WnResources *NwGetTransitWindowResources() {
  if (!NwTransitWindowResources) {
    NwTransitServiceRef service(kWnResourcesClassId, IID_WnResources);
    NwTransitWindowResources = static_cast<WnResources *>(service.object);
  }
  return NwTransitWindowResources;
}

static ScMessageServer *NwTransitMessageServer;

// Borrow the original message service, publishing its cached pointer before the acquisition temporary is released.
ScMessageServer *NwGetTransitMessageServer() {
  if (!NwTransitMessageServer) {
    NwTransitServiceRef service(kScMessageServerServiceId, IID_ScMessageServer);
    NwTransitMessageServer = static_cast<ScMessageServer *>(service.object);
  }
  return NwTransitMessageServer;
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Expose the original loader entry and return the same unretained lazy director used by module services.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return NwGetTransitDirector();
}
#endif
