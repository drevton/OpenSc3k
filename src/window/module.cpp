#include "graphics/sc3_graphics.h"
#include "window/api.h"
#include "window/wn_dll_director.h"

// Register every original control constructor after attaching the real window-service child director.
WnClassDirector::WnClassDirector() {
  AddDirector(WnGetWindowDirector());
  AddCls(kWnResourcesClassId, WnConstructResources, 0);
  AddCls(kWnFlatRectClassId, WnConstructFlatRect, 0);
  AddCls(kWnLabelClassId, WnConstructLabel, 0);
  AddCls(kWnEditorClassId, WnConstructEditor, 0);
  AddCls(kWnMessageBoxClassId, WnConstructMessageBox, 0);
  AddCls(kWnListClassId, WnConstructList, 0);
  AddCls(kWnTextListClassId, WnConstructTextList, 0);
  AddCls(kWnIconListClassId, WnConstructIconList, 0);
  AddCls(kWnComboClassId, WnConstructCombo, 0);
  AddCls(kWnImageButtonClassId, WnConstructImageButton, 0);
  AddCls(kWnBitmapClassId, WnConstructBitmap, 0);
  AddCls(kWnSliderClassId, WnConstructSlider, 0);
  AddCls(kWnScrollbarClassId, WnConstructScrollbar, 0);
  AddCls(kWnTextEditClassId, WnConstructTextEdit, 0);
  AddCls(kWnOptionGroupClassId, WnConstructOptionGroup, 0);
  AddCls(kWnSpinnerClassId, WnConstructSpinner, 0);
  AddCls(kWnOutlineClassId, WnConstructOutline, 0);
  AddCls(kWnTickerClassId, WnConstructTicker, 0);
  AddCls(kWnKeyAcceleratorClassId, WnConstructKeyAccelerator, 0);
}

u32 WnClassDirector::GetDirectorID() const {
  return kWnClassDirectorId;
}

WnWindowDirector::WnWindowDirector() {
  AddCls(kWnWindowServiceClassId, WnConstructWindowService, 0);
}

u32 WnWindowDirector::GetDirectorID() const {
  return kWnWindowDirectorId;
}

// Construct the module's class director once and return its borrowed static identity.
WnClassDirector *WnGetClassDirector() {
  static WnClassDirector director;
  return &director;
}

// Construct the window-service child once and preserve the original static destruction ordering.
WnWindowDirector *WnGetWindowDirector() {
  static WnWindowDirector director;
  return &director;
}

WnDirector::WnDirector() {
  AddDirector(WnGetClassDirector());
}

u32 WnDirector::GetDirectorID() const {
  return kWnDirectorId;
}

// Install the real framework hook, then create and install each source system service before releasing its factory reference.
bool WnDirector::OnStart(ScCom *) {
  WnGetFramework()->AddHook(this);
  void *slot = nullptr;
  GZCOM()->GetClassObject(kWnWindowServiceClassId, IID_ScSystemService, &slot);
  if (slot) {
    ScSystemService *service = static_cast<ScSystemService *>(slot);
    WnGetFramework()->AddSystemService(service);
    service->Release();
    slot = nullptr;
  }
  GZCOM()->GetClassObject(kWnResourcesClassId, IID_ScSystemService, &slot);
  if (slot) {
    ScSystemService *service = static_cast<ScSystemService *>(slot);
    WnGetFramework()->AddSystemService(service);
    service->Release();
  }
  return true;
}

// Acquire both services before checking them, then attach the manager's queried frame listener to graphics.
// A failed listener query still succeeds; an acquired listener is released after the ignored registration result.
bool WnDirector::PreFrameWorkInit() {
  WnWindowManager *windows = WnGetWindowManager();
  GxGraphics *graphics = WnGetGraphics();
  if (!windows || !graphics)
    return false;
  void *slot = nullptr;
  windows->QueryInterface(IID_GxFrameListener, &slot);
  if (slot) {
    GxFrameListener *listener = static_cast<GxFrameListener *>(slot);
    graphics->AddHook(listener);
    listener->Release();
  }
  return true;
}

WnDllDirector::WnDllDirector() : references(0), com(nullptr), framework(nullptr) {
}

WnDllDirector::~WnDllDirector() {
}

// Publish the primary director or secondary framework-hook identity before retaining the live shared director count.
bool WnDllDirector::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScDirector, ScDirector);
  HANDLE_QUERY_INTERFACE(IID_ScFrameworkHooks, ScFrameworkHooks);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 WnDllDirector::AddRef() {
  return ++references;
}

u32 WnDllDirector::Release() {
  return RemoveRef();
}

// Decrement only a positive module count and return its current value without retiring the director.
u32 WnDllDirector::RemoveRef() {
  if (references)
    --references;
  return references;
}

u32 WnDllDirector::RefCount() {
  return references;
}

// The installed framework getter shares a folded field-load body with other receivers.
// Return the borrowed pointer captured during COM initialization, including its initial null value.
ScFramework *WnDllDirector::FrameWork() {
  return framework;
}

// Return the borrowed COM root captured during initialization without adding a reference.
ScCom *WnDllDirector::GZCOM() {
  return com;
}

// Capture the COM root and framework, copy through a temporary path string, then initialize all live children in order.
bool WnDllDirector::InitializeCOM(ScCom *input, ScStringView *path) {
  if (!input)
    return false;
  com = input;
  framework = input->FrameWork();
  {
    ScString temporary(*path);
    library_path = temporary;
  }
  for (u32 index = 0; index < (u32)children.size(); ++index)
    children[index]->InitializeCOM(input, path);
  return true;
}

// Initialize a borrowed child, recursively append its children, then append that child without retaining it.
void WnDllDirector::AddDirector(WnDllDirector *director) {
  director->InitializeCOM(GZCOM(), &library_path);
  for (u32 index = 0; index < (u32)director->children.size(); ++index)
    AddDirector(director->children[index]);
  children.push_back(director);
}

void WnDllDirector::AddCls(u32 clsid, WnClassFactory create, u32 reserved) {
  classes.insert({{clsid, reserved}, create});
}

// Enumerate live child catalogs first, then this director's stable unsigned class/reserved order.
void WnDllDirector::EnumClassObjects(void (*callback)(u32, u32, void *), void *context) {
  for (u32 index = 0; index < (u32)children.size(); ++index)
    children[index]->EnumClassObjects(callback, context);
  for (auto entry = classes.begin(); entry != classes.end(); ++entry)
    callback(entry->first.first, entry->first.second, context);
}

// Search children before local factories. A failed local query disposes its zero-reference object through AddRef then Release.
bool WnDllDirector::GetClassObject(u32 clsid, u32 iid, void **out) {
  for (u32 index = 0; index < (u32)children.size(); ++index)
    if (children[index]->GetClassObject(clsid, iid, out))
      return true;
  for (auto entry = classes.begin(); entry != classes.end(); ++entry) {
    if (entry->first.first != clsid)
      continue;
    ScUnknown *object = entry->second();
    if (object->QueryInterface(iid, out))
      return true;
    object->AddRef();
    object->Release();
    return false;
  }
  return false;
}

// Let each live child veto unloading before checking whether this director has any module references.
bool WnDllDirector::CanUnloadNow() {
  for (u32 index = 0; index < (u32)children.size(); ++index)
    if (!children[index]->CanUnloadNow())
      return false;
  return references == 0;
}

bool WnDllDirector::OnUnload() {
  return true;
}

bool WnDllDirector::ModulePath(ScStringView *out) {
  out->Copy(library_path);
  return true;
}

// Visit every child registration callback even after one refuses, retaining failure for the final result.
bool WnDllDirector::AppRegister(ScRegistry *registry) {
  bool result = true;
  for (u32 index = 0; index < (u32)children.size(); ++index)
    if (!children[index]->AppRegister(registry))
      result = false;
  return result;
}

// Acquire the registry through the real framework, dispatch registration and retire the captured temporary registry reference.
bool WnDllDirector::OnStart(ScCom *) {
  void *value = nullptr;
  WnGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &value);
  bool result = AppRegister(static_cast<ScRegistry *>(value));
  if (value)
    static_cast<ScRegistry *>(value)->Release();
  return result;
}

bool WnDllDirector::PreFrameWorkInit() {
  return true;
}

bool WnDllDirector::PreAppInit() {
  return true;
}

bool WnDllDirector::PostAppInit() {
  return true;
}

bool WnDllDirector::PreAppShutdown() {
  return true;
}

bool WnDllDirector::PostAppShutdown() {
  return true;
}

bool WnDllDirector::PostSystemServiceShutdown() {
  return true;
}

bool WnDllDirector::AbortiveQuit() {
  return true;
}

bool WnDllDirector::OnInstall() {
  return true;
}

// Return the one real static module director after its child catalogs have been constructed.
ScDirector *WnGetDirector() {
  static WnDirector director;
  return &director;
}

// Borrow the framework from the live module director without another retain or service lookup.
ScFramework *WnGetFramework() {
  return WnGetDirector()->FrameWork();
}

// Borrow the COM root from the actual director each time, preserving startup and replacement visibility.
ScCom *WnGetCom() {
  return WnGetDirector()->GZCOM();
}

#if !defined(SC3_STATIC) && !defined(SC3_MONOLITHIC)
// Expose the original loader entry at ordinal one, borrowing the same lazy director without adding a reference.
// Static integration omits this per-DLL C symbol so separately linked modules keep independent directors.
SC3_C_LINKAGE ScDirector *GZDllGetGZCOMDirector() {
  return WnGetDirector();
}
#endif
