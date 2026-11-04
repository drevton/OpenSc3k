#include "geometry/module.h"

#include <stdlib.h>

#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/value.h"
#include "geometry/api.h"
#include "resources/api.h"
#include "sprites/api.h"
#include "window/api.h"

GeClassFactory::GeClassFactory() : references(0), begin(nullptr), end(nullptr), storageEnd(nullptr) {
}

// Publish the shared Unknown/class-factory identity before live virtual retention; misses clear the output.
bool GeClassFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining the module, then observe the live count after the director callback.
u32 GeClassFactory::AddRef() {
  ++references;
  GeGetDirector()->AddRef();
  return references;
}

// Decrement even an initial zero before removing the module hold; delete only when the live callback-adjusted count is zero.
u32 GeClassFactory::Release() {
  --references;
  GeGetDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Ask delegated factories in stored order before invoking this factory's concrete class creator.
// Each refusal preserves any output publication made by that child; the creator receives the same genuine output pointer.
bool GeClassFactory::GetCommand(u32 classId, u32 iid, void **out) {
  for (ScCommandFactory **entry = begin; entry != end; ++entry)
    if ((*entry)->GetCommand(classId, iid, out))
      return true;
  return CreateCommand(classId, iid, out);
}

// Reject the original null input, otherwise retain the child before appending it to the live delegation range.
// Trivial pointer storage grows geometrically without adding references to already retained children.
bool GeClassFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  if (end == storageEnd) {
    u32 count = begin ? u32(end - begin) : 0;
    if (count > UINT32_MAX / (2 * sizeof(ScCommandFactory *)))
      ScFatal(SC_ERROR_NOMEM, "Geometry factory delegation allocation is too large.");
    u32 capacity = count ? count * 2 : 1;
    ScCommandFactory **storage = new ScCommandFactory *[capacity];
    for (u32 i = 0; i < count; ++i)
      storage[i] = begin[i];
    delete[] begin;
    begin = storage;
    end = storage + count;
    storageEnd = storage + capacity;
  }
  *end++ = factory;
  return true;
}

// Entries appended during release are not revisited before the actual storage allocation is retired.
GeClassFactory::~GeClassFactory() {
  ScCommandFactory **entry = begin, **last = end;
  while (entry != last && *entry) {
    (*entry)->Release();
    *entry++ = nullptr;
  }
  delete[] begin;
}

GeCommandOwner::GeCommandOwner() : references(0) {
}

GeCommandOwner::~GeCommandOwner() {
}

// Remove a reference through the live secondary virtual method and delete the complete owner only at zero.
u32 GeCommandOwner::Release() {
  u32 result = RemoveRef();
  if (!result)
    delete this;
  return result;
}

u32 GeCommandOwner::AddRef() {
  GeGetDirector()->AddRef();
  return ++references;
}

// Leave zero unchanged; otherwise remove the module reference before decrementing the live owner count.
u32 GeCommandOwner::RemoveRef() {
  if (references) {
    GeGetDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 GeCommandOwner::RefCount() {
  return references;
}

GeCommand::GeCommand() {
}

// Transfer pending inputs out before releasing each, then drain outputs before retiring the four sequence owners.
GeCommand::~GeCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

bool GeCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 GeCommand::AddRef() {
  return GeCommandOwner::AddRef();
}

u32 GeCommand::Release() {
  return GeCommandOwner::Release();
}

// Retain an input argument before publishing it at the tail of the pending sequence.
bool GeCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Publish the oldest retained input and then remove its queue entry; an empty queue leaves the output untouched.
bool GeCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 GeCommand::InputQueueSize() {
  return inputs.size;
}

// Visit input descriptors in insertion order, observing descriptors appended by callbacks before testing the live end.
bool GeCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    UiCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Retain an output argument before publishing it at the tail of the pending result sequence.
bool GeCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

// Transfer the oldest output reference to the caller; an empty queue leaves its output pointer untouched.
bool GeCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 GeCommand::OutputQueueSize() {
  return outputs.size;
}

// Visit output descriptors in insertion order, preserving callback growth of the live descriptor sequence.
bool GeCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    UiCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool GeCommand::AddInputParameter(i32 type, u32 flags) {
  *input_parameters.Append() = {type, flags};
  return true;
}

bool GeCommand::AddOutputParameter(i32 type, u32 flags) {
  *output_parameters.Append() = {type, flags};
  return true;
}

// Original one-word scoped registry receiver used only during this module's startup notification.
class GeRegistryRef {
public:
  GeRegistryRef();
  ~GeRegistryRef();
  void *value; // Genuine external COM output storage; typed only when the acquired interface is used.
};

GeRegistryRef::GeRegistryRef() : value(nullptr) {
  GeGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
}

GeRegistryRef::~GeRegistryRef() {
  if (value)
    static_cast<ScRegistry *>(value)->Release();
}

// Register all original creation callbacks in source order without constructing their game objects.
// Unreconstructed callbacks keep their genuine separate declarations rather than fabricated factory results.
GeDirector::GeDirector() {
  AddClass(kScOccupantManagerClassId, GeCreateLayer, 0);
  AddClass(kGeBuildingAttribClassId, GeCreateBuildingAttrib, 0);
  AddClass(kGeOverrideClassId, GeCreateOverride, 0);
  AddClass(kOcOverrideContainerClassId, GeCreateOverrideList, 0);
  AddClass(kGeFloraControllerClassId, GeCreateFloraController, 0);
  AddClass(kGeFloraAttribClassId, GeCreateFloraAttrib, 0);
  AddClass(kGePortLayerClassId, GeCreatePortLayer, 0);
  AddClass(kGePortDataClassId, GeCreatePortData, 0);
  AddClass(kGeBuildingControllerClassId, GeCreateBuildingController, 0);
  AddClass(kGeBuildingDataClassId, GeCreateBuildingData, 0);
  AddClass(kGeBuildingClassId, GeCreateBuilding, 0);
  AddClass(kGeAnimatedBuildingClassId, GeCreateAnimatedBuilding, 0);
  AddClass(kGeBuildingGroupFactoryClassId, GeCreateBuildingGroupFactory, 0);
  AddClass(kGeDemolitionClassId, GeCreateDemolition, 0);
}

GeDirector::~GeDirector() {
}

u32 GeDirector::GetDirectorID() const {
  return kGeDirectorId;
}

bool GeDirector::OnStart(ScCom *) {
  GeRegistryRef registry;
  return AppRegister(static_cast<ScRegistry *>(registry.value));
}

bool GeDirector::AppRegister(ScRegistry *) {
  return true;
}

alignas(GeDirector) static u8 GeDirectorBytes[sizeof(GeDirector)];
static u8 GeDirectorInitialized;

static void GeDestroyDirector() {
  reinterpret_cast<GeDirector *>(GeDirectorBytes)->~GeDirector();
}

// Publish the lazy initialization bit before placement construction and register the original exit destructor once.
ScDirector *GeGetDirector() {
  if (!(GeDirectorInitialized & 1)) {
    GeDirectorInitialized |= 1;
    new (static_cast<void *>(GeDirectorBytes)) GeDirector;
    atexit(GeDestroyDirector);
  }
  return static_cast<ScDirector *>(reinterpret_cast<GeDirector *>(GeDirectorBytes));
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Return the borrowed module singleton through the DLL loader's genuine C entry point.
SC3_API ScDirector *GZDllGetGZCOMDirector() {
  return GeGetDirector();
}
#endif

static ScMessageServer *GeMessageServer;
static ScLanguageManager *GeLanguageManager;
static RsManager *GeResourceManager;
static RsFileSystem *GeFileSystem;
static WnWindowManager *GeWindowManager;
static WnResources *GeWindowResources;
static ScCommandFactory *GeClassFactory;
static u8 GeApplicationInitFlags;
static void *GeApplicationCache;

// Obtain the framework through the actual director's framework slot. The result is not a COM-object factory.
ScFramework *GeGetFramework() {
  return GeGetDirector()->FrameWork();
}

// Cache the acquired framework message service before releasing its temporary reference.
// Reentrant release callbacks see the published cache; failed null lookups retry on the next call.
ScMessageServer *GeGetMessageServer() {
  if (!GeMessageServer) {
    void *object = nullptr;
    GeGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *server = static_cast<ScMessageServer *>(object);
    GeMessageServer = server;
    if (server)
      server->Release();
  }
  return GeMessageServer;
}

// Cache the framework resource service before releasing its temporary reference. Null acquisition retries on the
// next call; the framework Boolean does not determine whether the returned service is usable.
RsManager *GeGetResourceManager() {
  if (!GeResourceManager) {
    void *object = nullptr;
    GeGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *manager = static_cast<RsManager *>(object);
    GeResourceManager = manager;
    if (manager)
      manager->Release();
  }
  return GeResourceManager;
}

static void GeApplicationExit() {
}

// Set the one-time guard before querying the framework application into its persistent output slot. Ignore the query
// result and preserve publication during reentrant calls; a failed null lookup is not retried.
ApGameInterface *GeGetApplication() {
  if (!(GeApplicationInitFlags & 1)) {
    GeApplicationInitFlags |= 1;
    GeGetFramework()->Application()->QueryInterface(IID_ApGameInterface, &GeApplicationCache);
    atexit(GeApplicationExit);
  }
  return static_cast<ApGameInterface *>(GeApplicationCache);
}

// Borrow the application's current city on each lookup. Missing application state returns null without a city call.
CtCity *GeGetCity() {
  ApGameInterface *application = GeGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the application's current occupant-attribute cache on each call. A missing application returns null without
// requesting a cache; the key-initialization caller retains its original startup invariant.
OcAttribCache *GeGetOccupantAttribCache() {
  ApGameInterface *application = GeGetApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Borrow the current application agent-type tree on each request. Missing application state returns null without
// invoking the tree getter, and no separate tree cache or retained reference is introduced.
CtAgentTree *GeGetAgentTypeTree() {
  ApGameInterface *application = GeGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}

// Borrow the director's COM root directly. This accessor performs no query or reference acquisition.
ScCom *GeGetCom() {
  return GeGetDirector()->GZCOM();
}

// Cache the queried class-factory service before releasing its temporary acquisition. Ignore the lookup Boolean;
// a null publication retries later, and release callbacks observe the already published service.
ScCommandFactory *GeGetClassFactory() {
  if (!GeClassFactory) {
    void *object = nullptr;
    GeGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &object);
    ScCommandFactory *factory = static_cast<ScCommandFactory *>(object);
    GeClassFactory = factory;
    if (factory)
      factory->Release();
  }
  return GeClassFactory;
}

// Borrow the current application preference interface on every request. A missing application returns null without
// creating a separate preferences cache or calling its getter.
ApPreferences *GeGetPreferences() {
  ApGameInterface *application = GeGetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Publish the framework file-system service before releasing its temporary reference. A null lookup retries later;
// the framework Boolean does not override the returned service pointer.
RsFileSystem *GeGetFileSystem() {
  if (!GeFileSystem) {
    void *object = nullptr;
    GeGetFramework()->GetSystemService(kRsFileSystemServiceId, IID_RsFileSystem, &object);
    RsFileSystem *service = static_cast<RsFileSystem *>(object);
    GeFileSystem = service;
    if (service)
      service->Release();
  }
  return GeFileSystem;
}

iptr GeGetSystemPerformanceInfo() {
  ApGameInterface *application = GeGetApplication();
  return application ? reinterpret_cast<iptr>(application->GetSystemPerformanceInfo()) : 0;
}

// Borrow the application's current city window on every call. A missing application returns null without a window call.
SpCityWindowView *GeGetCityWindow() {
  ApGameInterface *application = GeGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Follow the live city window to its current view without retaining either interface or caching their identities.
SpViewInterface *GeGetView() {
  SpCityWindowView *window = GeGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the current view's animation tick manager. A missing view prevents the subordinate getter call.
SpTickManager *GeGetTickManager() {
  SpViewInterface *view = GeGetView();
  return view ? view->GetAnimTickManager() : nullptr;
}

// Publish the borrowed window-manager cache before releasing the temporary framework acquisition.
// Ignore the lookup Boolean; failed null publications retry and release callbacks see the published cache.
WnWindowManager *GeGetWindowManager() {
  if (!GeWindowManager) {
    void *object = nullptr;
    GeGetFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    WnWindowManager *service = static_cast<WnWindowManager *>(object);
    GeWindowManager = service;
    if (service)
      service->Release();
  }
  return GeWindowManager;
}

// Acquire the original framework language service into a temporary, publishing the cache before its reference release.
// Null acquisition retries on the next request; the framework Boolean does not override the published pointer.
ScLanguageManager *GeGetLanguageManager() {
  if (!GeLanguageManager) {
    void *publication = nullptr;
    GeGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &publication);
    auto *manager = static_cast<ScLanguageManager *>(publication);
    GeLanguageManager = manager;
    if (manager)
      manager->Release();
  }
  return GeLanguageManager;
}

// Cache the original window-resource provider before releasing its temporary framework acquisition.
// A null publication retries later, and release callbacks observe the already published cache.
WnResources *GeGetWindowResources() {
  if (!GeWindowResources) {
    void *publication = nullptr;
    GeGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &publication);
    auto *resources = static_cast<WnResources *>(publication);
    GeWindowResources = resources;
    if (resources)
      resources->Release();
  }
  return GeWindowResources;
}
