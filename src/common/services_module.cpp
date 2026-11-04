#include "common/services_module.h"

#include <stdlib.h>

#include <new>

#include "common/ids.h"
#include "common/locale.h"
#include "common/message_queue.h"
#include "common/sc3_director.h"
#include "fonts/sc3_font_manager.h"

ScCommandFactoryBase::ScCommandFactoryBase() : references(0) {
}

ScCommandFactoryBase::~ScCommandFactoryBase() {
  ScCommandFactory **cursor = children.data(), **end = cursor;
  if (!children.empty())
    end += children.size();
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor = nullptr;
    ++cursor;
  }
}

// Publish the factory for its own identity or Unknown, then retain it through the live virtual entry.
bool ScCommandFactoryBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the owning module, then return the live count.
u32 ScCommandFactoryBase::AddRef() {
  ++references;
  ScGetServicesDirector()->AddRef();
  return references;
}

// Decrement locally, release the module reference and delete the complete factory if its live count is zero.
u32 ScCommandFactoryBase::Release() {
  --references;
  ScGetServicesDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Try child factories in insertion order before dispatching the genuine virtual local command creator.
bool ScCommandFactoryBase::GetCommand(u32 class_id, u32 iid, void **out) {
  for (usize index = 0; index < children.size(); ++index)
    if (children[index]->GetCommand(class_id, iid, out))
      return true;
  return CreateCommand(class_id, iid, out);
}

// Retain and append a non-null child factory. Null input leaves the child vector untouched.
bool ScCommandFactoryBase::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  children.push_back(factory);
  return true;
}

ScCommandServiceRoot::ScCommandServiceRoot() : ScServicesBase(kScCommandServiceId, 0) {
}

ScCommandServiceRoot::~ScCommandServiceRoot() {
}

bool ScCommandServiceRoot::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScCommandService, ScCommandService);
  if (ScServicesBase::QueryInterface(iid, out))
    return true;
  return ScCommandFactoryBase::QueryInterface(iid, out);
}

u32 ScCommandServiceRoot::AddRef() {
  return ScServicesBase::AddRef();
}

u32 ScCommandServiceRoot::Release() {
  return ScServicesBase::Release();
}

bool ScCommandServiceRoot::CreateCommand(u32, u32, void **) {
  return false;
}

// Borrow the embedded command-factory identity without adding a reference.
ScCommandFactory *ScCommandServiceRoot::GetClassFactory() {
  return static_cast<ScCommandFactoryBase *>(this);
}

// Construct the real message server and publish its leading system-service identity.
// The director performs the subsequent requested-interface query and retain.
static ScUnknown *ScCreateMessageService() {
  return static_cast<ScSystemService *>(new ScMessageService);
}

// Construct the actual queue-manager service and return its leading system-service identity.
static ScUnknown *ScCreateService6073210C() {
  return static_cast<ScSystemService *>(new ScQueueService);
}

// Construct the real font system and return its secondary system-service identity.
// Its font-system primary identity remains available through the original QueryInterface.
static ScUnknown *ScCreateFontService() {
  return static_cast<ScSystemService *>(new FtSystem);
}

// Construct the actual manager and return its system-service facet with the original zero initial reference count.
static ScUnknown *ScCreateServiceA3024279() {
  return static_cast<ScSystemService *>(new ScLanguageService);
}

// Construct the original command service and return its leading system-service identity.
static ScUnknown *ScCreateService24E80DAB() {
  return static_cast<ScSystemService *>(new ScCommandServiceRoot);
}

// Register the four original local constructors, then attach the real independently initialized child director.
ScServicesDirector::ScServicesDirector() {
  AddClass(kScMessageServiceClassId, ScCreateMessageService, 0);
  AddClass(kScQueueServiceClassId, ScCreateService6073210C, 0);
  AddClass(kFtSystemClassId, ScCreateFontService, 0);
  AddClass(kScLanguageServiceClassId, ScCreateServiceA3024279, 0);
  AddDirector(ScGetServicesChildDirector());
}

u32 ScServicesDirector::GetDirectorID() const {
  return kScServicesDirectorId;
}

// Start the child first, then request and install the four main services in their original order.
// Reload COM/framework around callbacks, preserving a published pointer even when GetClassObject returns false.
bool ScServicesDirector::OnStart(ScCom *input) {
  ScGetServicesChildDirector()->OnStart(input);
  const u32 classIds[4] = {
      kScMessageServiceClassId, kScQueueServiceClassId, kFtFontSystemServiceId, kScLanguageServiceClassId};
  ScSystemService *service = nullptr;
  for (u32 i = 0; i < 4; ++i) {
    GZCOM()->GetClassObject(classIds[i], IID_ScSystemService, reinterpret_cast<void **>(&service));
    if (service) {
      ScServicesFramework()->AddSystemService(service);
      service->Release();
      if (i != 3)
        service = nullptr;
    }
  }
  return true;
}

ScServicesChildDirector::ScServicesChildDirector() {
  AddClass(kScCommandServiceClassId, ScCreateService24E80DAB, 0);
}

u32 ScServicesChildDirector::GetDirectorID() const {
  return kScServicesChildDirectorId;
}

// Request the child service through the caller's COM root, install any published view, then release temporary ownership.
bool ScServicesChildDirector::OnStart(ScCom *input) {
  ScSystemService *service = nullptr;
  input->GetClassObject(kScCommandServiceClassId, IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    ScServicesFramework()->AddSystemService(service);
    service->Release();
  }
  return true;
}

alignas(ScServicesDirector) static u8 ScServicesDirectorStorage[sizeof(ScServicesDirector)];
alignas(ScServicesChildDirector) static u8 ScServicesChildDirectorStorage[sizeof(ScServicesChildDirector)];
static bool ScServicesDirectorInitialized, ScServicesChildDirectorInitialized;

static void ScDestroyServicesDirector() {
  reinterpret_cast<ScServicesDirector *>(ScServicesDirectorStorage)->~ScServicesDirector();
}

static void ScDestroyServicesChildDirector() {
  reinterpret_cast<ScServicesChildDirector *>(ScServicesChildDirectorStorage)->~ScServicesChildDirector();
}

// Publish the lazy-construction flag before building the actual main director, then register its independent exit cleanup.
ScDirector *ScGetServicesDirector() {
  if (!ScServicesDirectorInitialized) {
    ScServicesDirectorInitialized = true;
    new (ScServicesDirectorStorage) ScServicesDirector();
    atexit(ScDestroyServicesDirector);
  }
  return reinterpret_cast<ScServicesDirector *>(ScServicesDirectorStorage);
}

// Construct the original child singleton once with its own flag and exit-registration order.
ScServicesChildDirector *ScGetServicesChildDirector() {
  if (!ScServicesChildDirectorInitialized) {
    ScServicesChildDirectorInitialized = true;
    new (ScServicesChildDirectorStorage) ScServicesChildDirector();
    atexit(ScDestroyServicesChildDirector);
  }
  return reinterpret_cast<ScServicesChildDirector *>(ScServicesChildDirectorStorage);
}

// Borrow the framework from the real main director instead of a separate injected service provider.
ScFramework *ScServicesFramework() {
  return ScGetServicesDirector()->FrameWork();
}

// Export the original nonretaining module lookup through a unique native symbol for the DLL export alias.
SC3_C_LINKAGE ScDirector *ScServicesDllGetGZCOMDirector() {
  return ScGetServicesDirector();
}

ScServicesObject::ScServicesObject() : references(0) {
}

ScServicesObject::~ScServicesObject() {
}

bool ScServicesObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScServicesObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScServicesObject::AddRef() {
  ScGetServicesDirector()->AddRef();
  return ++references;
}

// Capture final-reference selection before removing the module reference, then delete or decrement the live object.
u32 ScServicesObject::Release() {
  if (references == 1) {
    ScGetServicesDirector()->RemoveRef();
    delete this;
    return 0;
  }
  ScGetServicesDirector()->RemoveRef();
  return --references;
}

// Remove the module reference before decrementing a positive local count, preserving the live object even at zero.
// A zero local count skips both decrements, unlike Release's wrapping zero-count path.
u32 ScServicesObject::RemoveRef() {
  if (references) {
    ScGetServicesDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScServicesObject::RefCount() {
  return references;
}

ScServicesBase::ScServicesBase(u32 value, i32 order) : identifier(value), priority(order), running(false) {
}

ScServicesBase::~ScServicesBase() {
}

// Keep system-service and basic identities distinct while sharing the same dynamic retain implementation.
bool ScServicesBase::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScSystemService)
    return ScServicesObject::QueryInterface(iid, out);
  *out = static_cast<ScSystemService *>(this);
  static_cast<ScSystemService *>(this)->AddRef();
  return true;
}

u32 ScServicesBase::AddRef() {
  return ScServicesObject::AddRef();
}

u32 ScServicesBase::Release() {
  return ScServicesObject::Release();
}

u32 ScServicesBase::GetServiceID() const {
  return identifier;
}

void ScServicesBase::SetServiceID(u32 value) {
  identifier = value;
}

i32 ScServicesBase::GetServicePriority() const {
  return priority;
}

bool ScServicesBase::IsServiceRunning() const {
  return running;
}

void ScServicesBase::SetServiceRunning(bool value) {
  running = value;
}

bool ScServicesBase::Init() {
  return true;
}

bool ScServicesBase::Shutdown() {
  return true;
}

bool ScServicesBase::OnTick(u32) {
  return true;
}

bool ScServicesBase::OnIdle(u32) {
  return true;
}

bool ScServicesBase::OnInstall() {
  return true;
}
