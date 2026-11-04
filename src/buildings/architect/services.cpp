#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "buildings/architect/api.h"
#include "buildings/architect/bd_director.h"
#include "buildings/architect/codec.h"
#include "buildings/architect/image.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "window/api.h"

ScUnknown *BdManagerPrimaryCurrent;

BdDirectorClasses::BdDirectorClasses() : head{&head, &head, 0, 0, nullptr} {
}

// Dispose registration nodes only. Stored factories and borrowed child directors retain their own lifetimes.
BdDirectorClasses::~BdDirectorClasses() {
  while (head.next != &head) {
    BdClassRegistration *entry = head.next;
    head.next = entry->next;
    delete entry;
  }
}

// Insert by unsigned class/reserved pair, keeping the first constructor for a duplicate key.
// Stable links preserve live callback traversal when a later class is inserted during enumeration.
void BdDirectorClasses::Insert(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  BdClassRegistration *before = head.next;
  while (before != &head && (before->clsid < clsid || (before->clsid == clsid && before->reserved < reserved)))
    before = before->next;
  if (before != &head && before->clsid == clsid && before->reserved == reserved)
    return;
  BdClassRegistration *entry = new BdClassRegistration{before, before->previous, clsid, reserved, create};
  before->previous->next = entry;
  before->previous = entry;
}

BdDllDirector::BdDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

BdDllDirector::~BdDllDirector() {
}

// Publish the actual primary director or secondary hook view, then retain through the shared primary virtual.
bool BdDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 BdDllDirector::AddRef() {
  return ++references;
}

u32 BdDllDirector::Release() {
  return RemoveRef();
}

// Decrement a nonzero module count while preserving zero and the static object's lifetime.
u32 BdDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 BdDllDirector::RefCount() {
  return references;
}

// Borrow the framework captured during COM initialization, including the original initial null value.
ScFramework *BdDllDirector::FrameWork() {
  return framework;
}

// Borrow the stored COM root without adding or removing ownership.
ScCom *BdDllDirector::GZCOM() {
  return com;
}

// Store COM and its framework, copy the input interface string, then initialize every live child in source order.
// A null COM input returns false before any fields or child directors are changed.
bool BdDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (u32 i = 0; i < children.size; ++i)
    children.data[i]->InitializeCOM(input, path);
  return true;
}

void BdDllDirector::AddDirector(BdDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 i = 0; i < director->children.size; ++i)
    AddDirector(director->children.data[i]);
  *children.Append() = director;
}

// Enumerate live child catalogs first, then this director's stable unsigned class/reserved order.
void BdDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 i = 0; i < children.size; ++i)
    children.data[i]->EnumClassObjects(callback, context);
  for (BdClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next)
    callback(entry->clsid, entry->reserved, context);
}

// Search children before local factories, retaining first matching class behavior across reserved-key entries.
// Failed QI is followed by AddRef/Release so the newly created zero-reference object disposes itself normally.
bool BdDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 i = 0; i < children.size; ++i)
    if (children.data[i]->GetClassObject(clsid, iid, out))
      return true;
  for (BdClassRegistration *entry = classes.head.next; entry != &classes.head; entry = entry->next) {
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

// Let each live child veto unloading before testing this director's own reference word.
bool BdDllDirector::CanUnloadNow() {
  for (u32 i = 0; i < children.size; ++i)
    if (!children.data[i]->CanUnloadNow())
      return false;
  return references == 0;
}

bool BdDllDirector::OnUnload() {
  return true;
}

bool BdDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Continue registration through all live children while retaining any refusal in the final Boolean result.
bool BdDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 i = 0; i < children.size; ++i)
    if (!children.data[i]->AppRegister(registry))
      result = false;
  return result;
}

// Capture the actual registry service, run the registration virtual and release the captured service afterward.
bool BdDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  BdFrameWork()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  ScRegistry *registry = static_cast<ScRegistry *>(value);
  bool result = AppRegister(registry);
  if (registry)
    registry->Release();
  return result;
}

void BdDllDirector::RegisterClass(u32 clsid, ScUnknown *(*create)(), u32 reserved) {
  classes.Insert(clsid, create, reserved);
}

bool BdDllDirector::PreFrameWorkInit() {
  return true;
}

bool BdDllDirector::PreAppInit() {
  return true;
}

bool BdDllDirector::PostAppInit() {
  return true;
}

bool BdDllDirector::PreAppShutdown() {
  return true;
}

bool BdDllDirector::PostAppShutdown() {
  return true;
}

bool BdDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool BdDllDirector::AbortiveQuit() {
  return true;
}

bool BdDllDirector::OnInstall() {
  return true;
}

// Construct the actual child director before registering the architect's two root factories.
BdDirector::BdDirector() {
  AddDirector(BdGetChildDirector());
  RegisterClass(kBdManagerClassId, BdCreateManager, 0);
  RegisterClass(kBdDocumentClassId, BdCreateClass455969DF, 0);
}

BdDirector::~BdDirector() {
  ScRelease(&BdManagerPrimaryCurrent);
}

u32 BdDirector::GetDirectorID() const {
  return kBdDirectorId;
}

// Ask COM for the architect manager's real system-service view, add it to the actual framework and release the query hold.
bool BdDirector::OnStart(ScCom *input) {
  void *value = nullptr;
  input->GetClassObject(kBdManagerClassId, IID_ScSystemService, &value);
  if (value) {
    ScSystemService *service = static_cast<ScSystemService *>(value);
    BdFrameWork()->AddSystemService(service);
    service->Release();
  }
  return true;
}

// Register the four actual child-library factories. Construction leaves all created-object lifetimes independent.
BdChildDirector::BdChildDirector() {
  RegisterClass(kGxGimexImageClassId, BdCreateClassA2FFB5D3, 0);
  RegisterClass(kGxGimexCodecClassId, BdCreateClassC3023BE2, 0);
  RegisterClass(kBdImageLoaderClassId, BdCreateImageLoader, 0);
  RegisterClass(kBdCodecClassId, BdCreateCodec, 0);
}

BdChildDirector::~BdChildDirector() {
}

u32 BdChildDirector::GetDirectorID() const {
  return kBdChildDirectorId;
}

// Return the real static root, preserving child-before-root destruction order through ordinary static initialization.
ScDirector *BdGetDirector() {
  static BdDirector director;
  return &director;
}

// Construct the actual static child once; it remains borrowed by the root's child pointer storage.
BdChildDirector *BdGetChildDirector() {
  static BdChildDirector director;
  return &director;
}

// Borrow the framework from the actual static director instead of inventing a module service provider.
ScFramework *BdFrameWork() {
  return BdGetDirector()->FrameWork();
}

// Create the real zero-reference image loader registered by the child directory.
ScUnknown *BdCreateImageLoader() {
  return new BdImageLoader;
}

// Create the real zero-reference codec wrapper registered by the child directory.
ScUnknown *BdCreateCodec() {
  return new BdCodec;
}

// Transform a terminated registration value with the repeating key, writing its terminator after the transformed bytes.
u8 BdTransformRegistryBytes(const char *source, char *out, const char *key) {
  if (!key)
    key = "ahv5jdkcosjfkeotaf";
  const char *cursor = key;
  while (*source) {
    *out++ = static_cast<char>((static_cast<u8>(*cursor) + 100) ^ static_cast<u8>(*source++));
    if (!*++cursor)
      cursor = key;
  }
  *out = 0;
  return 1;
}

#if defined(_WIN32)
static bool BdRegistryRootEquals(const ScString *root, const char *name) {
  usize length = strlen(name);
  return root->Strlen() == length && !memcmp(root->Data(), name, length);
}

// Read the named registry value from a complete root/subkey/value path into the supplied game string.
// The source accepts DWORD and string values, retaining the byte count returned for a string, including its terminator.
u8 BdReadRegistryValue(const ScString *path, ScString *out) {
  u32 length = path->Strlen();
  if (!length)
    return 0;
  const char *text = path->Data();
  u32 first = 0, last = length;
  while (first < length && text[first] != '\\')
    ++first;
  while (last > first + 1 && text[last - 1] != '\\')
    --last;
  if (first == length || last <= first + 1 || !first)
    return 0;
  --last;
  ScString root(text, first);
  ScString subkey(text + first + 1, last - first - 1);
  HKEY hive;
  if (BdRegistryRootEquals(&root, "HKEY_CLASSES_ROOT"))
    hive = HKEY_CLASSES_ROOT;
  else if (BdRegistryRootEquals(&root, "HKEY_CURRENT_USER"))
    hive = HKEY_CURRENT_USER;
  else if (BdRegistryRootEquals(&root, "HKEY_LOCAL_MACHINE"))
    hive = HKEY_LOCAL_MACHINE;
  else if (BdRegistryRootEquals(&root, "HKEY_USERS"))
    hive = HKEY_USERS;
  else if (BdRegistryRootEquals(&root, "HKEY_CURRENT_CONFIG"))
    hive = HKEY_CURRENT_CONFIG;
  else
    return 0;
  HKEY opened;
  if (RegOpenKeyExA(hive, subkey.Data(), 0, KEY_QUERY_VALUE, &opened) != ERROR_SUCCESS)
    return 0;
  DWORD type = 0;
  u8 result = 0;
  if (RegQueryValueExA(opened, text + last + 1, nullptr, &type, nullptr, nullptr) == ERROR_SUCCESS) {
    if (type == REG_DWORD) {
      DWORD value = 0, size = sizeof(value);
      if (RegQueryValueExA(opened, text + last + 1, nullptr, &type, reinterpret_cast<BYTE *>(&value), &size) ==
          ERROR_SUCCESS) {
        char text_value[16];
        snprintf(text_value, sizeof(text_value), "%u", static_cast<u32>(value));
        out->FromChar(text_value);
        result = 1;
      }
    } else if (type == REG_SZ) {
      char buffer[1024];
      DWORD size = sizeof(buffer);
      if (RegQueryValueExA(opened, text + last + 1, nullptr, &type, reinterpret_cast<BYTE *>(buffer), &size) ==
          ERROR_SUCCESS) {
        out->FromChar(buffer, size);
        result = 1;
      }
    }
  }
  RegCloseKey(opened);
  return result;
}
#else
u8 BdReadRegistryValue(const ScString *, ScString *) {
  return 0;
}
#endif

static GxGraphics *BdGraphicsCurrent;
static RsFileSystem *BdFileSystemCurrent;
static RsManager *BdResourceManagerCurrent;
static ScMessageServer *BdMessageServerCurrent;
static ScLanguageManager *BdLanguageManagerCurrent;
static ScCommandFactory *BdCommandFactoryCurrent;
static WnWindowManager *BdWindowManagerCurrent;
static WnResources *BdWindowResourcesCurrent;

// Cache the architect DLL's borrowed graphics service before releasing the temporary queried reference.
// Keep this cache independent of other DLLs so their first lookup and ownership ordering remain separate.
GxGraphics *BdGetGraphics() {
  if (!BdGraphicsCurrent) {
    void *value = nullptr;
    BdFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &value);
    BdGraphicsCurrent = static_cast<GxGraphics *>(value);
    if (value)
      static_cast<GxGraphics *>(value)->Release();
  }
  return BdGraphicsCurrent;
}

// Borrow the COM root captured by the real module director without retaining or replacing it.
ScCom *BdGetCom() {
  return BdGetDirector()->GZCOM();
}

// Publish the queried filesystem cache before releasing its temporary reference, then return the live borrowed cache.
RsFileSystem *BdGetFileSystem() {
  if (!BdFileSystemCurrent) {
    void *value = nullptr;
    BdFrameWork()->GetSystemService(kRsFileSystemServiceId, IID_RsFileSystem, &value);
    BdFileSystemCurrent = static_cast<RsFileSystem *>(value);
    if (value)
      static_cast<RsFileSystem *>(value)->Release();
  }
  return BdFileSystemCurrent;
}

// Cache the actual message-server service before releasing the queried hold, matching the DLL's borrowed getters.
// The SDK service alias and Unlimited caller agree on service64063387 and IIDC1085722.
ScMessageServer *BdGetMessageServer() {
  if (!BdMessageServerCurrent) {
    void *value = nullptr;
    BdFrameWork()->GetSystemService(kScMessageServerServiceId, IID_ScMessageServer, &value);
    BdMessageServerCurrent = static_cast<ScMessageServer *>(value);
    if (value)
      BdMessageServerCurrent->Release();
  }
  return BdMessageServerCurrent;
}

// Cache the architect's borrowed language service before releasing the temporary framework reference.
// A failed lookup remains uncached and is retried on the next call.
ScLanguageManager *BdGetLanguageManager() {
  if (!BdLanguageManagerCurrent) {
    void *value = nullptr;
    BdFrameWork()->GetSystemService(kScLanguageManagerServiceId, IID_ScLanguageManager, &value);
    BdLanguageManagerCurrent = static_cast<ScLanguageManager *>(value);
    if (BdLanguageManagerCurrent)
      BdLanguageManagerCurrent->Release();
  }
  return BdLanguageManagerCurrent;
}

// Cache the architect's borrowed command factory before releasing the temporary framework reference.
// A failed lookup remains uncached and is retried on the next call.
ScCommandFactory *BdGetCommandFactory() {
  if (!BdCommandFactoryCurrent) {
    void *value = nullptr;
    BdFrameWork()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &value);
    BdCommandFactoryCurrent = static_cast<ScCommandFactory *>(value);
    if (BdCommandFactoryCurrent)
      BdCommandFactoryCurrent->Release();
  }
  return BdCommandFactoryCurrent;
}

// Publish the architect module's borrowed resource-manager cache before releasing its temporary queried reference.
// Retry a null result on the next call and ignore the framework's separate Boolean result.
RsManager *BdGetResourceManager() {
  if (!BdResourceManagerCurrent) {
    void *object = nullptr;
    BdFrameWork()->GetSystemService(kRsManagerServiceId, IID_RsManager, &object);
    BdResourceManagerCurrent = static_cast<RsManager *>(object);
    if (BdResourceManagerCurrent)
      BdResourceManagerCurrent->Release();
  }
  return BdResourceManagerCurrent;
}

// Query the real window-manager service and publish the module's borrowed cache before releasing the temporary reference.
// A null service remains uncached and is retried on the next lookup.
WnWindowManager *BdGetWindowManager() {
  if (!BdWindowManagerCurrent) {
    void *object = nullptr;
    BdFrameWork()->GetSystemService(kWnWindowManagerServiceId, IID_WnWindowManager, &object);
    BdWindowManagerCurrent = static_cast<WnWindowManager *>(object);
    if (BdWindowManagerCurrent)
      BdWindowManagerCurrent->Release();
  }
  return BdWindowManagerCurrent;
}

// Cache the architect DLL's borrowed window-resource provider before releasing the temporary service reference.
// A null lookup is left uncached and is retried by the next call.
WnResources *BdGetWindowResources() {
  if (!BdWindowResourcesCurrent) {
    void *value = nullptr;
    BdFrameWork()->GetSystemService(kWnResourcesServiceId, IID_WnResources, &value);
    BdWindowResourcesCurrent = static_cast<WnResources *>(value);
    if (BdWindowResourcesCurrent)
      BdWindowResourcesCurrent->Release();
  }
  return BdWindowResourcesCurrent;
}

WnArchitectRootWidget::WnArchitectRootWidget()
    : references(0), identifier(0), absolute_area{}, draw_area{}, children_locked(false), parent(nullptr),
      notification_target(nullptr), caption(), private_buffer(nullptr), draw_buffer(nullptr), dirty(true),
      cursor(nullptr), accelerator(nullptr), fill_color(0), child_focus(nullptr), key_equivalent(0), mouse_down_x(0),
      mouse_down_y(0), area{}, buffer_area{}, flags(kWnDefaultFlags) {
  owner = BdGetWindowManager();
  if (owner)
    keyboard = BdGetWindowManager()->GetKeyboard();
  if (owner)
    owner->AddWindowToValidList(this);
}

// Select this window's private surface or the nearest ancestor's surface.
// If neither exists, use the architect module's own cached window manager and update descendant drawing areas.
void WnArchitectRootWidget::SetBufferToDrawTo() {
  draw_buffer = GetPrivateBuffer();
  if (!draw_buffer) {
    WnWindow *ancestor = GetParentWin();
    while (ancestor) {
      GxBuffer *buffer = ancestor->GetPrivateBuffer();
      if (buffer) {
        draw_buffer = buffer;
        break;
      }
      ancestor = ancestor->GetParentWin();
    }
    if (!draw_buffer) {
      GxCanvas *canvas = BdGetWindowManager()->GetCanvas();
      if (canvas)
        draw_buffer = canvas->GetMainBufferSurface();
    }
  }
  SetAreaToDrawToRecursive();
}
#define WN_ROOT_CLASS WnArchitectRootWidget
#define WN_ROOT_EXTERNAL_CONSTRUCTOR
#define WN_ROOT_CUSTOM_BUFFER
#define WN_ROOT_MANAGER BdGetWindowManager
#define WN_ROOT_GRAPHICS BdGetGraphics
#define WN_ROOT_RESOURCES BdGetWindowResources
#include "window/wn_root_widget_methods.inc"
#undef WN_ROOT_RESOURCES
#undef WN_ROOT_GRAPHICS
#undef WN_ROOT_MANAGER
#undef WN_ROOT_CUSTOM_BUFFER
#undef WN_ROOT_EXTERNAL_CONSTRUCTOR
#undef WN_ROOT_CLASS
