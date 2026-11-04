#include "ui/ui_module.h"

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/value.h"
#include "fonts/sc3_font_manager.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "sprites/api.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/wn_resource_provider.h"

UiDirectorClasses::UiDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Release registration storage only; constructors and borrowed child directors have independent lifetimes.
UiDirectorClasses::~UiDirectorClasses() {
  while (head.next != &head) {
    UiClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved key while retaining the first constructor for duplicate keys.
// Stable nodes preserve the original ordered tree's behavior when enumeration callbacks insert another class.
void UiDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  UiClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  UiClassRegistration *entry = new UiClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

UiDllDirector::UiDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

UiDllDirector::~UiDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before the shared virtual retain.
bool UiDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiDllDirector::AddRef() {
  return ++references;
}

u32 UiDllDirector::Release() {
  return RemoveRef();
}

// Decrement a positive module count while preserving zero and the static director's lifetime.
u32 UiDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 UiDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including its initial null state.
ScFramework *UiDllDirector::FrameWork() {
  return framework;
}

// Borrow the current COM root without changing its ownership.
ScCom *UiDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and its framework, copy the path, then initialize the live children in source order.
bool UiDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
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
void UiDllDirector::AddDirector(UiDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < director->children.size; ++index)
    AddDirector(director->children.data[index]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, followed by this director's stable unsigned registration order.
void UiDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < children.size; ++index)
    children.data[index]->EnumClassObjects(callback, context);
  for (UiClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories; failed QI disposes the zero-reference result through AddRef then Release.
bool UiDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < children.size; ++index)
    if (children.data[index]->GetClassObject(clsid, iid, out))
      return true;
  for (UiClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
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
bool UiDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool UiDllDirector::OnUnload() {
  return true;
}

bool UiDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child, retaining a refusal while continuing the rest of the registration pass.
bool UiDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < children.size; ++index)
    if (!children.data[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the actual registry service, run registration and release the captured registry even after refusal.
bool UiDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  UiGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool UiDllDirector::PreFrameWorkInit() {
  return true;
}

bool UiDllDirector::PreAppInit() {
  return true;
}

bool UiDllDirector::PostAppInit() {
  return true;
}

bool UiDllDirector::PreAppShutdown() {
  return true;
}

bool UiDllDirector::PostAppShutdown() {
  return true;
}

bool UiDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool UiDllDirector::AbortiveQuit() {
  return true;
}

bool UiDllDirector::OnInstall() {
  return true;
}

UiFactory::UiFactory() : references(0) {
}

// Publish the actual unknown/factory interface and retain it through the live virtual AddRef. Unsupported interface IDs
// clear the output.
bool UiFactory::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the receiver first, retain the director next, then return the live receiver count after that callback.
u32 UiFactory::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Remove the receiver reference before the module reference, then decide deletion from the live field.
// Final release destroys this factory and its retained children before returning zero.
u32 UiFactory::Release() {
  --references;
  UiGetDirector()->RemoveRef();
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Give retained children the original three arguments in order, stopping at their first successful publication.
// Keep the current cursor while observing the live end; reach the real CreateCommand override after all children fail.
bool UiFactory::GetCommand(u32 classId, u32 iid, void **out) {
  ScCommandFactory **cursor = children.data;
  for (u32 i = 0; i < children.size; ++i, ++cursor)
    if ((*cursor)->GetCommand(classId, iid, out))
      return true;
  return CreateCommand(classId, iid, out);
}

// Reject a null child as the original does; otherwise retain it before appending the actual interface pointer.
bool UiFactory::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  *children.Append() = factory;
  return true;
}

UiFactory::~UiFactory() {
  ScCommandFactory **cursor = children.data, **end = cursor;
  if (children.size)
    end += children.size;
  while (cursor != end && *cursor) {
    (*cursor)->Release();
    *cursor++ = nullptr;
  }
}

// Publish this module's existing director through the original ordinal-one entry without acquiring a reference.
SC3_C_LINKAGE ScDirector *UiDllGetGZCOMDirector() {
  return UiGetDirector();
}

// Adapt the genuine secondary factory pointer to the catalog callback type without querying or retaining it.
static ScUnknown *UiCreateGenericClass() {
  return UiCreateGenericWindow();
}

// Preserve the control factory's original return type while publishing the same unknown view to the catalog.
static ScUnknown *UiCreateMinMaxButtonClass() {
  return UiMinMaxButton::CreateNewObject();
}

// Register the supported local classes in their original relative order without constructing game objects.
// The user excludes network communication and chat, so their optional dialog factories are omitted.
UiDirector::UiDirector() {
  classes.Insert(kUiCommandFactoryClassId, UiCreateClass000005A6, 0);
  classes.Insert(kUiMessageBoxClassId, UiCreateClass3370D1A5, 0);
  classes.Insert(kUiQueryWindowClassId, UiCreateClass0000057A, 0);
  classes.Insert(kUiQuerySectionClassId, UiCreateClass00000584, 0);
  classes.Insert(kUiRasterQueryClassId, UiCreateClass000005A3, 0);
  classes.Insert(kUiMapControlClassId, UiCreateClass000005A9, 0);
  classes.Insert(kUiBuildingPickerClassId, UiCreateClassE3091435, 0);
  classes.Insert(kUiBusinessDealsClassId, UiCreateClass62FA5742, 0);
  classes.Insert(kUiAdvisorButtonClassId, UiCreateClass62BE6EE2, 0);
  classes.Insert(kUiGenericClassId, UiCreateGenericClass, 0);
  classes.Insert(kUiTerrainEditorClassId, UiCreateClass72E85997, 0);
  classes.Insert(kUiTerrainConfigClassId, UiCreateClass82E968EE, 0);
  classes.Insert(kUiLayerDialogClassId, UiCreateClassC2F14704, 0);
  classes.Insert(kUiGameMenuManagerClassId, UiCreateClass82F622C1, 0);
  classes.Insert(kUiTipDialogClassId, UiCreateClassC2FA5860, 0);
  classes.Insert(kUiBudgetSliderClassId, UiCreateClass230391B9, 0);
  classes.Insert(kUiResourceClientClassId, UiCreateClass030477A4, 0);
  classes.Insert(kUiAdvisorManagerClassId, UiCreateClass22BE563E, 0);
  classes.Insert(kUiNeighborWindowClassId, UiCreateClass4302B412, 0);
  classes.Insert(kUiUserInputClassId, UiCreateClass454423FF, 0);
  classes.Insert(kUiMainManagerObjectClassId, UiCreateClass43164FB1, 0);
  classes.Insert(kUiGameMenuPanelClassId, UiCreateClass02F61B00, 0);
  classes.Insert(kUiNavigationClassId, UiCreateClassE2F95AF5, 0);
  classes.Insert(kUiDateCashWindowClassId, UiCreateClass02F95D57, 0);
  classes.Insert(kUiRciWindowClassId, UiCreateClass12FAF305, 0);
  classes.Insert(kUiMinMaxButtonClassId, UiCreateMinMaxButtonClass, 0);
  classes.Insert(kUiReplacementProcedureClassId, UiCreateClass756B5B54, 0);
}

UiDirector::~UiDirector() {
}

u32 UiDirector::GetDirectorID() const {
  return kUiDirectorId;
}

// Build both source command names and register only successful conversions.
void UiDirector::ConfigureRegistryEffects(ScRegistry *registry) {
  if (!registry)
    return;
  ScString name, key, value;
  key.FromChar("0x000005A6");
  value.FromChar("0");
  if (registry->MakeRegistryName("cogamecmd", key, value, &name))
    registry->Register(name);
  key.FromChar("0x03728C87");
  value.FromChar("0x19981201");
  name.Erase(0, name.Strlen());
  if (registry->MakeRegistryName("GZMsgBoxCMDID", key, value, &name))
    registry->Register(name);
}

// Configure command registration before publishing the secondary framework hook, then release the captured registry.
bool UiDirector::OnStart(ScCom *) {
  void *value = nullptr;
  UiGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  ScRegistry *registry = static_cast<ScRegistry *>(value);
  ConfigureRegistryEffects(registry);
  if (framework)
    framework->AddHook(static_cast<ScFrameworkHooks *>(this));
  if (registry)
    registry->Release();
  return true;
}

// Acquire and register the real resource client, preserving class-query success as the gate for both temporary releases.
bool UiDirector::PreAppInit() {
  if (com) {
    void *value = nullptr;
    if (com->GetClassObject(kUiResourceClientClassId, IID_WnResourceClient, &value)) {
      WnResourceClient *client = static_cast<WnResourceClient *>(value);
      void *provider = nullptr;
      UiGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &provider);
      WnResources *resources = static_cast<WnResources *>(provider);
      if (resources)
        resources->RegisterObject(client);
      client->Release();
      if (resources)
        resources->Release();
    }
  }
  return true;
}

// Construct the module's one static director and register its ordinary C++ teardown once.
ScDirector *UiGetDirector() {
  static UiDirector director;
  return &director;
}

static WnWindowManager *UiWindows;
static GxGraphics *UiGraphics;
static ScMessageServer *UiMessages;
static RsManager *UiResources;
static RsFileSystem *UiFiles;
static WnResources *UiWindowResources;
static ScLanguageManager *UiLanguage;
static FtFontSystem *UiFonts;
static bool UiApplicationInitialized;
static ApGameInterface *UiApplication;
static ScCommandFactory *UiCommands;
static AuSoundDevice *UiSoundDevice;

// Publish SIMUI's borrowed sound device before releasing its temporary service reference.
// Null acquisitions retry, and reentry through Release observes the published cache.
AuSoundDevice *UiGetSoundDevice() {
  if (!UiSoundDevice) {
    AuSoundDevice *device = nullptr;
    UiGetFramework()->GetSystemService(kAuSoundDeviceServiceId, IID_AuSoundDevice, reinterpret_cast<void **>(&device));
    UiSoundDevice = device;
    if (device)
      device->Release();
  }
  return UiSoundDevice;
}

// Borrow the application's performance record when its real application service is available.
// A missing application returns null without acquiring or manufacturing a replacement record.
ApPerformanceInfo *UiGetSystemPerformanceInfo() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetSystemPerformanceInfo() : nullptr;
}

// Borrow the framework captured by the concrete SIMUI director during COM initialization.
ScFramework *UiGetFramework() {
  return UiGetDirector()->FrameWork();
}

// Publish the command service before releasing its temporary acquisition, retaining the framework's borrowed identity.
// Missing services retry on later calls; a pointer published with a false acquisition result still follows this lifetime.
ScCommandFactory *UiGetCommandFactory() {
  if (!UiCommands) {
    ScCommandFactory *command_factory = nullptr;
    UiGetFramework()->GetSystemService(
        kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&command_factory));
    UiCommands = command_factory;
    if (command_factory)
      command_factory->Release();
  }
  return UiCommands;
}

// Acquire the sprite image-cache identity anew and release the temporary hold before returning its borrowed pointer.
SpImageCache *UiGetImageCache() {
  SpImageCache *images = nullptr;
  UiGetFramework()->GetSystemService(kSpImageCacheServiceId, IID_SpImageCache, reinterpret_cast<void **>(&images));
  SpImageCache *result = images;
  if (images)
    images->Release();
  return result;
}

// Borrow the module director's live class catalog without acquiring a synthetic reference.
ScCom *UiGetCom() {
  return UiGetDirector()->GZCOM();
}

// Mark the one-shot application query before entering the framework.
// Retain its published game interface unchanged.
// A null or failed query never retries, and reentry observes the current cache without another application lookup.
ApGameInterface *UiGetApplication() {
  if (!UiApplicationInitialized) {
    UiApplicationInitialized = true;
    UiGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&UiApplication));
  }
  return UiApplication;
}

// Borrow the application's live preference interface without retaining or caching it separately.
// The nullable application lookup precedes the original slot96 call on every invocation.
ApPreferences *UiGetPreferences() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetAppPreferences() : nullptr;
}

// Borrow the application's live occupant-attribute cache; an absent application supplies the source's null result.
OcAttribCache *UiGetAttribCache() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetAppOccupantAttribCache() : nullptr;
}

// Read the application's current city afresh on each call; no city reference is acquired by this borrowed getter.
CtCity *UiGetCity() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetCity() : nullptr;
}

// Borrow the current city-window identity from the captured application interface.
SpCityWindowView *UiGetCityWindow() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetISC3WinCityView() : nullptr;
}

// Borrow the current city view through the freshly fetched city window; absent city-window state returns null.
SpViewInterface *UiGetCurrentView() {
  SpCityWindowView *window = UiGetCityWindow();
  return window ? window->GetCurrentView() : nullptr;
}

// Borrow the actual BoxX event interface through original application slot52; an absent application returns null.
ApBoxX *UiGetBoxX() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetBoxX() : nullptr;
}

// Publish the borrowed manager before releasing its acquisition reference; null results retry on the next call.
WnWindowManager *UiGetWindowManager() {
  if (!UiWindows) {
    void *object = nullptr;
    UiGetFramework()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    WnWindowManager *windows = static_cast<WnWindowManager *>(object);
    UiWindows = windows;
    if (windows)
      windows->Release();
  }
  return UiWindows;
}

// Cache the borrowed graphics interface before dropping its temporary reference, preserving reentrant visibility.
GxGraphics *UiGetGraphics() {
  if (!UiGraphics) {
    void *object = nullptr;
    UiGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &object);
    GxGraphics *graphics = static_cast<GxGraphics *>(object);
    UiGraphics = graphics;
    if (graphics)
      graphics->Release();
  }
  return UiGraphics;
}

// Cache the message server before releasing its temporary acquisition; failed lookup leaves the cache null.
ScMessageServer *UiGetMessageServer() {
  if (!UiMessages) {
    void *object = nullptr;
    UiGetFramework()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &object);
    ScMessageServer *messages = static_cast<ScMessageServer *>(object);
    UiMessages = messages;
    if (messages)
      messages->Release();
  }
  return UiMessages;
}

// Cache the resource manager's borrowed interface. Release observes the newly installed cache during reentry.
RsManager *UiGetResourceManager() {
  if (!UiResources) {
    void *object = nullptr;
    UiGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    RsManager *resources = static_cast<RsManager *>(object);
    UiResources = resources;
    if (resources)
      resources->Release();
  }
  return UiResources;
}

// Publish this DLL's borrowed window-resource provider before releasing its temporary acquisition reference.
// A null lookup retries on the next call, independently of the GZWIND module's own cache.
WnResources *UiGetWindowResources() {
  if (!UiWindowResources) {
    void *object = nullptr;
    UiGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &object);
    WnResources *resources = static_cast<WnResources *>(object);
    UiWindowResources = resources;
    if (resources)
      resources->Release();
  }
  return UiWindowResources;
}

// Publish the borrowed language service before releasing its temporary reference; a null acquisition retries later.
ScLanguageManager *UiGetLanguageManager() {
  if (!UiLanguage) {
    void *value = nullptr;
    UiGetFramework()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &value);
    ScLanguageManager *language_manager = static_cast<ScLanguageManager *>(value);
    UiLanguage = language_manager;
    if (language_manager)
      language_manager->Release();
  }
  return UiLanguage;
}

// Try the preferred font service before the fallback, publishing either borrowed view before its temporary release.
FtFontSystem *UiGetFontManager() {
  if (!UiFonts) {
    void *first = nullptr;
    UiGetFramework()->GetSystemService(kFtAppFontServiceId, IID_FtFontSystem, &first);
    if (first)
      UiFonts = static_cast<FtFontSystem *>(first);
    else {
      void *second = nullptr;
      UiGetFramework()->GetSystemService(kFtFontSystemServiceId, IID_FtFontSystem, &second);
      UiFonts = static_cast<FtFontSystem *>(second);
      if (second)
        static_cast<FtFontSystem *>(second)->Release();
    }
    if (first)
      static_cast<FtFontSystem *>(first)->Release();
  }
  return UiFonts;
}

// Query the blend service afresh on every call and release its temporary reference before returning the borrowed view.
GxBlendService *UiGetBlendService() {
  void *object = nullptr;
  UiGetFramework()->GetSystemService(kGxBlendServiceId, IID_GxBlendService, &object);
  GxBlendService *blend = static_cast<GxBlendService *>(object);
  if (blend)
    blend->Release();
  return blend;
}

// Publish this DLL's borrowed filesystem before retiring the lookup reference so release-time reentry sees the cache.
// Ignore the lookup Boolean and retry only when its published pointer remains null.
RsFileSystem *UiGetFileSystem() {
  if (!UiFiles) {
    void *value = nullptr;
    UiGetFramework()->GetSystemService(RsFileSystem::ServiceId, RsFileSystem::Iid, &value);
    RsFileSystem *filesystem = static_cast<RsFileSystem *>(value);
    UiFiles = filesystem;
    if (filesystem)
      filesystem->Release();
  }
  return UiFiles;
}

// Borrow the application type tree when the cached application exists, without retaining either interface.
// A missing application returns null without another service query.
CtAgentTree *UiGetAgentTypeTree() {
  ApGameInterface *application = UiGetApplication();
  return application ? application->GetAppAgentTypeTree() : nullptr;
}
