#include "window/tools.h"

#include <assert.h>

#include <cmath>
#include <cstdlib>
#include <new>

#include "common/api.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/api.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"

// The debug display registers at this service ordering priority.
enum ScDebugDisplayPriority : u32 {
  kScDebugDisplayServicePriority = 0x3d0900,
};

static constexpr u32 kWnMsg_ToolsShow = 0x628ff806;
static constexpr u32 kWnMsg_ToolsHide = 0xe28ff811;
static constexpr u32 kWnMsg_ToolsItems = 0x428e9072;

ScToolsFactoryBase::ScToolsFactoryBase() : references(0) {
}

// Release retained children in their captured original range until the first null entry.
ScToolsFactoryBase::~ScToolsFactoryBase() {
  ScCommandFactory **current = children.data(), **end = current;
  if (!children.empty())
    end += children.size();
  while (current != end && *current) {
    (*current)->Release();
    *current++ = nullptr;
  }
}

// Publish the original basic and command-factory identities and retain the module-aware receiver.
bool ScToolsFactoryBase::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScCommandFactory);
  HANDLE_QUERY_INTERFACE(IID_ScCommandFactory, ScCommandFactory);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the local count before retaining the module director, then return the live local count.
u32 ScToolsFactoryBase::AddRef() {
  ++references;
  ScGetToolsDirector()->AddRef();
  return references;
}

// Drop the local and module references in order, then delete the complete factory if the live count is zero.
u32 ScToolsFactoryBase::Release() {
  --references;
  ScGetToolsDirector()->RemoveRef();
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

// Query children in insertion order before dispatching the original virtual local command factory.
bool ScToolsFactoryBase::GetCommand(u32 class_id, u32 iid, void **out) {
  for (usize index = 0; index < children.size(); ++index)
    if (children[index]->GetCommand(class_id, iid, out))
      return true;
  return CreateCommand(class_id, iid, out);
}

// Retain a nonnull child before appending it to the ordered command-factory vector.
bool ScToolsFactoryBase::AddCommandFactory(ScCommandFactory *factory) {
  if (!factory)
    return false;
  factory->AddRef();
  children.push_back(factory);
  return true;
}

// Construct class122 and query the requested command identity; other class IDs clear the output.
bool ScToolsCommandFactory::CreateCommand(u32 class_id, u32 iid, void **out) {
  if (class_id != 122) {
    *out = nullptr;
    return false;
  }
  ScToolsCommand *command = new ScToolsCommand;
  if (command->QueryInterface(iid, out))
    return true;
  delete command;
  return false;
}

ScToolsCommandOwner::ScToolsCommandOwner() : references(0) {
}

ScToolsCommandOwner::~ScToolsCommandOwner() {
}

// Remove one reference through the live lifetime method, deleting the complete command only at zero.
u32 ScToolsCommandOwner::Release() {
  u32 result = RemoveRef();
  if (!result)
    delete this;
  return result;
}

u32 ScToolsCommandOwner::AddRef() {
  ScGetToolsDirector()->AddRef();
  return ++references;
}

// Drop a positive command reference and its module retain without deleting the receiver.
u32 ScToolsCommandOwner::RemoveRef() {
  if (references) {
    ScGetToolsDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScToolsCommandOwner::RefCount() {
  return references;
}

ScToolsCommand::ScToolsCommand() {
}

// Drain and release retained input arguments first, then outputs, before retiring the sequence owners.
ScToolsCommand::~ScToolsCommand() {
  ScVariant *argument;
  while (PopFrontInput(&argument))
    argument->Release();
  while (PopFrontOutput(&argument))
    argument->Release();
}

// Publish the original command or basic identity and retain the complete command.
bool ScToolsCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiCommandInterface);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScToolsCommand::AddRef() {
  return ScToolsCommandOwner::AddRef();
}

u32 ScToolsCommand::Release() {
  return ScToolsCommandOwner::Release();
}

bool ScToolsCommand::PushBackInput(ScVariant *argument) {
  argument->AddRef();
  *inputs.Append() = argument;
  return true;
}

// Transfer the oldest input reference to the caller, leaving its output untouched when empty.
bool ScToolsCommand::PopFrontInput(ScVariant **out) {
  if (!inputs.size)
    return false;
  *out = inputs.data[0];
  --inputs.size;
  for (u32 i = 0; i < inputs.size; ++i)
    inputs.data[i] = inputs.data[i + 1];
  return true;
}

u32 ScToolsCommand::InputQueueSize() {
  return inputs.size;
}

// Visit input descriptors in order, checking the live sequence end after every callback.
bool ScToolsCommand::EnumInputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!input_parameters.size)
    return false;
  for (u32 i = 0; i < input_parameters.size; ++i) {
    UiCommandParameter parameter = input_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

bool ScToolsCommand::PushBackOutput(ScVariant *argument) {
  argument->AddRef();
  *outputs.Append() = argument;
  return true;
}

// Transfer the oldest output reference to the caller, leaving its output untouched when empty.
bool ScToolsCommand::PopFrontOutput(ScVariant **out) {
  if (!outputs.size)
    return false;
  *out = outputs.data[0];
  --outputs.size;
  for (u32 i = 0; i < outputs.size; ++i)
    outputs.data[i] = outputs.data[i + 1];
  return true;
}

u32 ScToolsCommand::OutputQueueSize() {
  return outputs.size;
}

// Visit output descriptors in order, checking the live sequence end after every callback.
bool ScToolsCommand::EnumOutputParameters(void (*callback)(i32, u32, void *), void *context) {
  if (!output_parameters.size)
    return false;
  for (u32 i = 0; i < output_parameters.size; ++i) {
    UiCommandParameter parameter = output_parameters.data[i];
    callback(parameter.type, parameter.flags, context);
  }
  return true;
}

// Obtain the current root window, create the original window class, add it and bring it to the front.
bool ScToolsCommand::Execute() {
  WnWindowManager *manager = nullptr;
  ScGetToolsFramework()->GetSystemService(
      kWnWindowManagerServiceId, IID_WnWindowManager, reinterpret_cast<void **>(&manager));
  assert(manager);
  WnWindow *root = manager->GetMainWindow(), *window = nullptr;
  if (ScGetToolsCom()->GetClassObject(kWnToolsCommandWindowClassId, IID_WnWindow, reinterpret_cast<void **>(&window))) {
    assert(root);
    root->ChildAdd(window);
    assert(window);
    window->PullToFront();
  }
  manager->Release();
  return true;
}

// Cache the borrowed debug-display facet before releasing the temporary framework reference.
ScDebugDisplay *ScGetToolsDisplay() {
  static ScDebugDisplay *display;
  if (!display) {
    ScDebugDisplay *value = nullptr;
    ScGetToolsFramework()->GetSystemService(
        kScDebugDisplayServiceId, IID_ScDebugDisplay, reinterpret_cast<void **>(&value));
    display = value;
    if (value)
      value->Release();
  }
  return display;
}

// Cache the borrowed graphics service and balance the framework's temporary interface reference.
GxGraphics *ScGetToolsGraphics() {
  static GxGraphics *graphics;
  if (!graphics) {
    GxGraphics *value = nullptr;
    ScGetToolsFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&value));
    graphics = value;
    if (value)
      value->Release();
  }
  return graphics;
}

// Cache the borrowed window-resource service and release its temporary framework reference.
WnResources *ScGetToolsResources() {
  static WnResources *resources;
  if (!resources) {
    WnResources *value = nullptr;
    ScGetToolsFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&value));
    resources = value;
    if (value)
      value->Release();
  }
  return resources;
}

// Cache the borrowed window manager before balancing the framework's temporary reference.
WnWindowManager *ScGetToolsWindowManager() {
  static WnWindowManager *manager;
  if (!manager) {
    WnWindowManager *value = nullptr;
    ScGetToolsFramework()->GetSystemService(
        kWnWindowManagerServiceId, IID_WnWindowManager, reinterpret_cast<void **>(&value));
    manager = value;
    if (value)
      value->Release();
  }
  return manager;
}

// Try the preferred font system, then its original fallback, and cache the borrowed successful identity.
FtFontSystem *ScGetToolsFontSystem() {
  static FtFontSystem *fonts;
  if (!fonts) {
    FtFontSystem *preferred = nullptr;
    ScGetToolsFramework()->GetSystemService(
        kFtAppFontServiceId, IID_FtFontSystem, reinterpret_cast<void **>(&preferred));
    if (preferred)
      fonts = preferred;
    else {
      FtFontSystem *fallback = nullptr;
      ScGetToolsFramework()->GetSystemService(
          kFtFontSystemServiceId, IID_FtFontSystem, reinterpret_cast<void **>(&fallback));
      fonts = fallback;
      if (fallback)
        fallback->Release();
    }
    if (preferred)
      preferred->Release();
  }
  return fonts;
}

// Construct the display service and return its leading service identity to the module factory.
static ScUnknown *ScCreateToolsDisplay() {
  return static_cast<ScSystemService *>(new ScToolsDisplay);
}

// Construct the original debug window and return its leading window identity.
static ScUnknown *ScCreateToolsWindow() {
  return static_cast<WnWindow *>(new WnToolsWindow);
}

ScToolsDirector::ScToolsDirector() {
  AddClass(kScToolsDisplayClassId, ScCreateToolsDisplay, 0);
  AddClass(kWnToolsWindowClassId, ScCreateToolsWindow, 0);
}

u32 ScToolsDirector::GetDirectorID() const {
  return kScToolsDirectorId;
}

bool ScToolsDirector::OnStart(ScCom *) {
  ScGetToolsFramework()->AddHook(static_cast<ScFrameworkHooks *>(this));
  ScSystemService *service = nullptr;
  GZCOM()->GetClassObject(kScToolsDisplayClassId, IID_ScSystemService, reinterpret_cast<void **>(&service));
  if (service) {
    ScGetToolsFramework()->AddSystemService(service);
    service->Release();
  }
  return true;
}

bool ScToolsDirector::OnUnload() {
  return true;
}

bool ScToolsDirector::PreFrameWorkInit() {
  ScCommandFactory *service = nullptr;
  ScGetToolsFramework()->GetSystemService(
      kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&service));
  if (service) {
    ScToolsCommandFactory *factory = new ScToolsCommandFactory;
    factory->AddRef();
    service->AddCommandFactory(factory);
    factory->Release();
    service->Release();
  }
  return true;
}

alignas(ScToolsDirector) static u8 ScToolsDirectorStorage[sizeof(ScToolsDirector)];
static bool ScToolsDirectorInitialized;

static void ScDestroyToolsDirector() {
  reinterpret_cast<ScToolsDirector *>(ScToolsDirectorStorage)->~ScToolsDirector();
}

// Publish the initialization flag before constructing the original singleton and registering its exit cleanup.
ScToolsDirector *ScGetToolsDirector() {
  if (!ScToolsDirectorInitialized) {
    ScToolsDirectorInitialized = true;
    new (ScToolsDirectorStorage) ScToolsDirector();
    atexit(ScDestroyToolsDirector);
  }
  return reinterpret_cast<ScToolsDirector *>(ScToolsDirectorStorage);
}

// Borrow the framework through the original module singleton's live director interface.
ScFramework *ScGetToolsFramework() {
  return ScGetToolsDirector()->FrameWork();
}

// Borrow the COM root through the original module singleton's director interface.
ScCom *ScGetToolsCom() {
  return ScGetToolsDirector()->GZCOM();
}

// Expose the original module singleton through a uniquely named export alias.
SC3_C_LINKAGE ScDirector *ScToolsDllGetGZCOMDirector() {
  return ScGetToolsDirector();
}

ScToolsItem::ScToolsItem(ScStringView *value) : kind(0), references(0), line(value) {
}

u8 ScToolsItem::AddRef() {
  return ++references;
}

// Release the private item and destroy it on the one-to-zero transition.
u8 ScToolsItem::Release() {
  u8 result = --references;
  if (!result)
    delete this;
  return result;
}

ScToolsItem::~ScToolsItem() {
}

// Replace the text snapshot only when it differs from the currently borrowed String.
bool ScToolsItem::Update() {
  if (snapshot.IsEqual(line))
    return false;
  snapshot.FromChar(line->ToChar());
  return true;
}

ScToolsMeterItem::ScToolsMeterItem(f32 *input, u32 length)
    : ScToolsItem(nullptr), values(input), count(length), labels(nullptr), labelCount(0), previousBegin(nullptr),
      previousEnd(nullptr), previousCapacity(nullptr) {
}

// Retain each supplied label and store a separately retained copy of its current text.
ScToolsMeterItem::ScToolsMeterItem(f32 *input, u32 length, ScStringView **names, u32 namesCount)
    : ScToolsItem(nullptr), values(input), count(length), labels(names), labelCount(namesCount), previousBegin(nullptr),
      previousEnd(nullptr), previousCapacity(nullptr) {
  for (u32 i = 0; i < labelCount; ++i) {
    labels[i]->AddRef();
    ScString *copy = new ScString(labels[i]->ToChar());
    copy->AddRef();
    copiedLabels.push_back(copy);
  }
}

// Release copied label objects before retiring the original value and label vectors.
ScToolsMeterItem::~ScToolsMeterItem() {
  for (ScString *copy : copiedLabels)
    copy->Release();
  delete[] previousBegin;
}

// Refresh the value snapshot when its reserved capacity differs or any value differs by more than one.
bool ScToolsMeterItem::UpdateValues() {
  u32 capacity = previousBegin ? static_cast<u32>(previousCapacity - previousBegin) : 0;
  bool changed = capacity != count;
  if (capacity < count) {
    u32 length = previousBegin ? static_cast<u32>(previousEnd - previousBegin) : 0;
    f32 *replacement = new f32[count];
    for (u32 i = 0; i < length; ++i)
      replacement[i] = previousBegin[i];
    delete[] previousBegin;
    previousBegin = replacement;
    previousEnd = replacement + length;
    previousCapacity = replacement + count;
  }
  for (u32 i = 0; i < count && !changed; ++i) {
    f64 difference = (f64)values[i] - previousBegin[i];
    if (difference > 1.0 || difference < -1.0)
      changed = true;
  }
  if (changed)
    for (u32 i = 0; i < count; ++i)
      previousBegin[i] = values[i];
  return changed;
}

// Construct the display service, its secondary interface and its empty circular item list.
ScToolsDisplay::ScToolsDisplay()
    : ScToolsService(kScDebugDisplayServiceId, kScDebugDisplayServicePriority), visible(false), server(nullptr),
      head(new ScToolsItemNode), unknown(0) {
  head->next = head->previous = head;
}

// Invoke the live shutdown hook, then discard the remaining list nodes and sentinel without releasing their payloads.
ScToolsDisplay::~ScToolsDisplay() {
  Shutdown();
  ScToolsItemNode *node = head->next;
  while (node != head) {
    ScToolsItemNode *next = node->next;
    delete node;
    node = next;
  }
  delete head;
}

bool ScToolsDisplay::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScDebugDisplay, ScDebugDisplay);
  return ScToolsService::QueryInterface(iid, out);
}

u32 ScToolsDisplay::AddRef() {
  return ScToolsService::AddRef();
}

u32 ScToolsDisplay::Release() {
  return ScToolsService::Release();
}

// Acquire the real message-server service and preserve the original temporary and cached reference ordering.
// The original requires the acquired service when installing its retained cache.
ScMessageServer *ScToolsDisplay::AcquireMessageServer() {
  if (!server) {
    ScMessageServer *value = nullptr;
    ScGetToolsFramework()->GetSystemService(
        kScMessageServerServiceId, IID_ScMessageServer, reinterpret_cast<void **>(&value));
    server = value;
    assert(value);
    value->AddRef();
    value->Release();
  }
  return server;
}

bool ScToolsDisplay::Init() {
  AcquireMessageServer();
  return true;
}

// Release the cached server, then each recognized private item and all list nodes.
bool ScToolsDisplay::Shutdown() {
  ScRelease(&server);
  ScToolsItemNode *node = head->next;
  while (node != head) {
    ScToolsItemNode *next = node->next;
    if (node->item->kind <= 2)
      node->item->Release();
    delete node;
    node = next;
  }
  head->next = head->previous = head;
  return true;
}

void ScToolsDisplay::Show() {
  visible = true;
  ScMessage message(kWnMsg_ToolsShow);
  AcquireMessageServer()->MessagePost(message, false);
}

// Mark the display hidden before posting its original hide message.
void ScToolsDisplay::Hide() {
  ScMessage message(kWnMsg_ToolsHide);
  visible = false;
  AcquireMessageServer()->MessagePost(message, false);
}

bool ScToolsDisplay::IsVisible() {
  return visible;
}

// Append one retained item to the real circular list before sending its display-update notification.
void ScToolsDisplay::AppendItem(ScToolsItem *item) {
  item->AddRef();
  ScToolsItemNode *node = new ScToolsItemNode{head, head->previous, item};
  head->previous->next = node;
  head->previous = node;
}

// Post addition notifications and send removal notifications through the original message server.
void ScToolsDisplay::NotifyItems(bool post) {
  ScMessage message(kWnMsg_ToolsItems);
  if (post)
    AcquireMessageServer()->MessagePost(message, false);
  else
    AcquireMessageServer()->MessageSend(message);
}

bool ScToolsDisplay::RemoveItem(u8 kind, const void *identity) {
  ScToolsItemNode *node = head->next;
  while (node != head) {
    ScToolsItem *item = node->item;
    const void *source = nullptr;
    if (item->kind == kind)
      source = kind ? static_cast<const void *>(static_cast<ScToolsMeterItem *>(item)->values) : item->line;
    if (item->kind == kind && source == identity) {
      node->previous->next = node->next;
      node->next->previous = node->previous;
      delete node;
      item->Release();
      NotifyItems(false);
      return true;
    }
    node = node->next;
  }
  return false;
}

// Retain the caller's line as a private item and post the original display-update message.
bool ScToolsDisplay::AddLine(ScStringView *line) {
  AppendItem(new ScToolsItem(line));
  line->ToChar();
  NotifyItems(true);
  return true;
}

bool ScToolsDisplay::RemoveLine(ScStringView *line) {
  line->ToChar();
  return RemoveItem(0, line);
}

// Add an unlabeled bar graph over the caller's live float array.
bool ScToolsDisplay::AddBarGraph(f32 *values, u32 count) {
  ScToolsMeterItem *item = new ScToolsMeterItem(values, count);
  item->kind = 1;
  AppendItem(item);
  NotifyItems(true);
  return true;
}

// Add a bar graph with retained source labels and independent text snapshots.
bool ScToolsDisplay::AddBarGraph(f32 *values, u32 count, ScStringView **labels, u32 labelCount) {
  ScToolsMeterItem *item = new ScToolsMeterItem(values, count, labels, labelCount);
  item->kind = 1;
  AppendItem(item);
  NotifyItems(true);
  return true;
}

bool ScToolsDisplay::RemoveBarGraph(f32 *values) {
  return RemoveItem(1, values);
}

// Add an unlabeled strip meter over the caller's live float array.
bool ScToolsDisplay::AddStripMeter(f32 *values, u32 count) {
  ScToolsMeterItem *item = new ScToolsMeterItem(values, count);
  item->kind = 2;
  AppendItem(item);
  NotifyItems(true);
  return true;
}

// Add a labeled meter with copied text; the installed entry leaves its constructor's zero kind unchanged.
bool ScToolsDisplay::AddStripMeter(f32 *values, u32 count, ScStringView **labels, u32 labelCount) {
  ScToolsMeterItem *item = new ScToolsMeterItem(values, count, labels, labelCount);
  AppendItem(item);
  NotifyItems(true);
  return true;
}

bool ScToolsDisplay::RemoveStripMeter(f32 *values) {
  return RemoveItem(2, values);
}

WnToolsRootWidget::WnToolsRootWidget()
    : owner(nullptr), keyboard(nullptr), references(0), identifier(0), absolute_area{}, draw_area{},
      children_locked(false), parent(nullptr), notification_target(nullptr), caption(), private_buffer(nullptr),
      draw_buffer(nullptr), dirty(true), cursor(nullptr), accelerator(nullptr), fill_color(0), child_focus(nullptr),
      key_equivalent(0), mouse_down_x(0), mouse_down_y(0), area{}, buffer_area{}, flags(kWnDefaultFlags) {
  owner = ScGetToolsWindowManager();
  if (owner)
    keyboard = ScGetToolsWindowManager()->GetKeyboard();
  if (owner)
    owner->AddWindowToValidList(this);
}

// Select the nearest private drawing buffer or the tool module's canvas, then update descendant draw areas.
void WnToolsRootWidget::SetBufferToDrawTo() {
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
      GxCanvas *canvas = ScGetToolsWindowManager()->GetCanvas();
      if (canvas)
        draw_buffer = canvas->GetMainBufferSurface();
    }
  }
  SetAreaToDrawToRecursive();
}

#define WN_ROOT_CLASS WnToolsRootWidget
#define WN_ROOT_EXTERNAL_CONSTRUCTOR
#define WN_ROOT_CUSTOM_BUFFER
#define WN_ROOT_MANAGER ScGetToolsWindowManager
#define WN_ROOT_GRAPHICS ScGetToolsGraphics
#define WN_ROOT_RESOURCES ScGetToolsResources
#include "window/wn_root_widget_methods.inc"
#undef WN_ROOT_RESOURCES
#undef WN_ROOT_GRAPHICS
#undef WN_ROOT_MANAGER
#undef WN_ROOT_CUSTOM_BUFFER
#undef WN_ROOT_EXTERNAL_CONSTRUCTOR
#undef WN_ROOT_CLASS

ScToolsObject::ScToolsObject() : references(0) {
}

ScToolsObject::~ScToolsObject() {
}

bool ScToolsObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScToolsObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 ScToolsObject::AddRef() {
  ScGetToolsDirector()->AddRef();
  return ++references;
}

// Drop the module reference before deleting the final receiver or decrementing its local count.
u32 ScToolsObject::Release() {
  if (references == 1) {
    ScGetToolsDirector()->RemoveRef();
    delete this;
    return 0;
  }
  ScGetToolsDirector()->RemoveRef();
  return --references;
}

// Remove a module and local reference only when the local count is positive, without deleting the receiver.
u32 ScToolsObject::RemoveRef() {
  if (references) {
    ScGetToolsDirector()->RemoveRef();
    --references;
  }
  return references;
}

u32 ScToolsObject::RefCount() {
  return references;
}

ScToolsService::ScToolsService(u32 value, i32 order) : identifier(value), priority(order), running(false) {
}

ScToolsService::~ScToolsService() {
}

bool ScToolsService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  return ScToolsObject::QueryInterface(iid, out);
}

u32 ScToolsService::AddRef() {
  return ScToolsObject::AddRef();
}

u32 ScToolsService::Release() {
  return ScToolsObject::Release();
}

u32 ScToolsService::GetServiceID() const {
  return identifier;
}

void ScToolsService::SetServiceID(u32 value) {
  identifier = value;
}

i32 ScToolsService::GetServicePriority() const {
  return priority;
}

bool ScToolsService::IsServiceRunning() const {
  return running;
}

void ScToolsService::SetServiceRunning(bool value) {
  running = value;
}

bool ScToolsService::Init() {
  return true;
}

bool ScToolsService::Shutdown() {
  return true;
}

bool ScToolsService::OnTick(u32) {
  return true;
}

bool ScToolsService::OnIdle(u32) {
  return true;
}

bool ScToolsService::OnInstall() {
  return true;
}

static bool WnToolsDisplayActive, WnToolsDrawBorders;
static FtFont *WnToolsFont;
static u32 WnToolsWhite = UINT32_MAX, WnToolsBlack, WnToolsLineWidth = 384, WnToolsLineHeight = 15;
static u32 WnToolsBarWidth = 384, WnToolsBarHeight = 4, WnToolsBarColors[16];

WnToolsLineWindow::WnToolsLineWindow(ScToolsItem *value) : item(value), surface(nullptr) {
  item->AddRef();
}

WnToolsLineWindow::~WnToolsLineWindow() {
  ScRelease(&item);
  ScRelease(&surface);
  if (WnToolsFont) {
    ScGetToolsFontSystem()->FontRelease(WnToolsFont);
    WnToolsFont = nullptr;
  }
}

WnToolsBarWindow::WnToolsBarWindow(ScToolsMeterItem *value) : WnToolsLineWindow(value), meter(value) {
  meter->AddRef();
  WnToolsBarColors[0] = 0;
}

WnToolsBarWindow::~WnToolsBarWindow() {
  ScRelease(&meter);
}

// Refresh a changed line in its cached surface, then draw that surface and the optional debug border.
bool WnToolsLineWindow::GZPaint() {
  if (!WnToolsDisplayActive)
    return true;
  if (!WnToolsFont) {
    WnToolsFont = ScGetToolsFontSystem()->FontAcquire(FtType::FixedSerif, 14, 0, WnToolsWhite);
    GxGraphics *graphics = ScGetToolsGraphics();
    WnToolsBlack = graphics->ConvertRGBValueToNative16Bit(0, 0, 0);
    WnToolsWhite = graphics->ConvertRGBValueToNative16Bit(255, 255, 255);
    GxMode metrics = ScGetToolsGraphics()->GetGameMetrics();
    WnToolsLineWidth = static_cast<u16>(metrics.width - 260);
    WnToolsLineHeight = static_cast<u16>(WnToolsFont->Height(0));
    WnToolsBarWidth = WnToolsLineWidth;
    if (!WnToolsFont)
      return true;
  }
  if (!surface) {
    ScGetToolsGraphics()->CreateBuffer(&surface);
    surface->Init(WnToolsLineWidth, WnToolsLineHeight, GxColorType(GxColorType::Color::RGB16), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::VideoMemoryLowPriority, false);
    i32 bottom = GetT() + WnToolsLineHeight;
    i32 right = GetL() + WnToolsLineWidth;
    i32 top = GetT(), left = GetL();
    SetArea(left, top, right, bottom);
    if (surface->Lock(GxLockFlags::Fill)) {
      surface->Fill(WnToolsBlack, nullptr);
      surface->Unlock(GxLockFlags::Fill);
    }
  }
  if (item->Update()) {
    if (surface->Lock(GxLockFlags::Fill)) {
      surface->Fill(WnToolsBlack, nullptr);
      surface->Unlock(GxLockFlags::Fill);
    }
    if (item->line) {
      u32 length = item->line->Strlen();
      const char *text = item->line->Data();
      WnToolsFont->DrawTextA(surface, 0, 0, text, length);
    }
  }
  surface->Width();
  GetW();
  surface->Height();
  GetH();
  u32 surfaceHeight = surface->Height();
  i32 bottom = GetT() + surfaceHeight;
  u32 surfaceWidth = surface->Width();
  i32 right = GetL() + surfaceWidth;
  i32 top = GetT(), left = GetL();
  ScRect target = {left, top, right, bottom};
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    draw_buffer->Blt(surface, *surface->GetBufferArea(), target, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if (WnToolsDrawBorders && draw_buffer->Lock(GxLockFlags::Rect)) {
    draw_buffer->DrawRectangleOutline(*GetArea(), UINT32_MAX, 1);
    draw_buffer->Unlock(GxLockFlags::Rect);
  }
  InvalidateSelf();
  return true;
}

// Paint the meter into its cached surface, excluding out-of-range values and palette accesses.
bool WnToolsBarWindow::GZPaint() {
  if (!WnToolsDisplayActive)
    return true;
  if (!WnToolsBarColors[0]) {
    static const u8 kDefaultChartColors[16][3] = {{77, 164, 23}, {226, 218, 52}, {198, 32, 28}, {0, 0, 127},
        {63, 0, 127}, {0, 63, 127}, {127, 0, 63}, {127, 63, 0}, {0, 63, 127}, {0, 127, 63}, {255, 0, 255},
        {255, 255, 0}, {0, 255, 255}, {0, 255, 255}, {0, 255, 255}, {0, 255, 255}};
    GxGraphics *graphics = ScGetToolsGraphics();
    for (u32 i = 0; i < 16; ++i)
      WnToolsBarColors[i] = graphics->ConvertRGBValueToNative16Bit(
          kDefaultChartColors[i][0], kDefaultChartColors[i][1], kDefaultChartColors[i][2]);
  }
  if (!surface) {
    ScGetToolsGraphics()->CreateBuffer(&surface);
    surface->Init(WnToolsBarWidth, WnToolsBarHeight, GxColorType(GxColorType::Color::RGB16), nullptr,
        GxDepthType(GxDepthType::Depth::None), GxVramRequest::VideoMemoryLowPriority, false);
    i32 bottom = GetT() + WnToolsBarHeight;
    i32 right = GetL() + WnToolsBarWidth;
    i32 top = GetT(), left = GetL();
    SetArea(left, top, right, bottom);
    if (surface->Lock(GxLockFlags::Fill)) {
      surface->Fill(WnToolsBlack, nullptr);
      surface->Unlock(GxLockFlags::Fill);
    }
  }
  if (meter->UpdateValues()) {
    if (surface->Lock(GxLockFlags::Fill)) {
      surface->Fill(WnToolsBlack, nullptr);
      ScRect bar = {0, 0, 0, GetH()};
      i32 position = 0;
      for (u32 i = 0; i < meter->count; ++i) {
        assert(i < 16);
        bar.left = position;
        f64 value = meter->values[i];
        assert(std::isfinite(value) && value >= -9223372036854775808.0 && value < 9223372036854775808.0);
        position = static_cast<i32>(static_cast<u32>(position) + static_cast<u32>(static_cast<i64>(value)));
        if (position >= GetR())
          position = GetR();
        bar.right = position;
        surface->Fill(WnToolsBarColors[i], bar, nullptr);
      }
      surface->Unlock(GxLockFlags::Fill);
    }
    if (meter->line) {
      u32 length = meter->line->Strlen();
      const char *text = meter->line->Data();
      WnToolsFont->DrawTextA(surface, 0, 0, text, length);
    }
  }
  surface->Width();
  GetW();
  surface->Height();
  GetH();
  u32 surfaceHeight = surface->Height();
  i32 bottom = GetT() + surfaceHeight;
  u32 surfaceWidth = surface->Width();
  i32 right = GetL() + surfaceWidth;
  i32 top = GetT(), left = GetL();
  ScRect target = {left, top, right, bottom};
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    draw_buffer->Blt(surface, *surface->GetBufferArea(), target, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if (WnToolsDrawBorders && draw_buffer->Lock(GxLockFlags::Rect)) {
    draw_buffer->DrawRectangleOutline(*GetArea(), UINT32_MAX, 1);
    draw_buffer->Unlock(GxLockFlags::Rect);
  }
  InvalidateSelf();
  return true;
}

// Acquire the original temporary message-server reference for window notification registration.
static ScMessageServer *WnToolsAcquireMessageServer() {
  ScMessageServer *server = nullptr;
  ScGetToolsFramework()->GetSystemService(
      kScMessageServerServiceId, IID_ScMessageServer, reinterpret_cast<void **>(&server));
  return server;
}

// Construct the root window and empty item-view list, then retain the concrete display behind its borrowed facet.
WnToolsWindow::WnToolsWindow() : active(false), display(nullptr), head(new WnToolsWindowNode) {
  head->next = head->previous = head;
  ScDebugDisplay *view = ScGetToolsDisplay();
  if (view)
    display = ScAddRefNotNull(static_cast<ScToolsDisplay *>(view));
}

// Release the display and discard item-view list nodes before retiring the name and root-window bases.
WnToolsWindow::~WnToolsWindow() {
  if (display)
    display->Release();
  WnToolsWindowNode *node = head->next;
  while (node != head) {
    WnToolsWindowNode *next = node->next;
    delete node;
    node = next;
  }
  delete head;
}

// Register the three display notifications, set the original debug name and initialize the root window.
bool WnToolsWindow::Init() {
  ScMessageServer *server = WnToolsAcquireMessageServer();
  assert(server);
  server->AddNotification(this, kWnMsg_ToolsItems);
  server->AddNotification(this, kWnMsg_ToolsShow);
  server->AddNotification(this, kWnMsg_ToolsHide);
  {
    ScString name("GZWinDebugDisplayMarkII");
    debug_name = name;
  }
  bool result = WnToolsRootWidget::Init();
  server->Release();
  return result;
}

// Unregister notifications and release each retained item view before shutting down the root window.
bool WnToolsWindow::Shutdown() {
  ScMessageServer *server = WnToolsAcquireMessageServer();
  assert(server);
  server->RemoveNotification(this, kWnMsg_ToolsItems);
  server->RemoveNotification(this, kWnMsg_ToolsShow);
  server->RemoveNotification(this, kWnMsg_ToolsHide);
  for (WnToolsWindowNode *node = head->next; node != head; node = node->next) {
    if (node->window) {
      node->window->Release();
      node->window = nullptr;
    }
  }
  bool result = WnToolsRootWidget::Shutdown();
  server->Release();
  return result;
}

// Reconcile item changes and dispatch the original show and hide notifications.
bool WnToolsWindow::DoMessage(ScMessage *message) {
  bool show = message->type == kWnMsg_ToolsShow, hide = message->type == kWnMsg_ToolsHide;
  if (message->type == kWnMsg_ToolsItems) {
    ReconcileItems();
    show = static_cast<ScDebugDisplay *>(display)->IsVisible();
    hide = !show;
  }
  if (show) {
    active = true;
    WnToolsDisplayActive = true;
    ShowWindow();
    PullToFront();
    return true;
  }
  if (hide) {
    active = false;
    WnToolsDisplayActive = false;
    HideWindow();
    return true;
  }
  return parent ? parent->DoMessage(message) : false;
}

void WnToolsWindow::ReconcileItems() {
  u32 height = 0;
  for (ScToolsItemNode *entry = display->head->next; entry != display->head; entry = entry->next) {
    WnToolsWindowNode *found = head->next;
    while (found != head && found->window->item != entry->item)
      found = found->next;
    WnToolsLineWindow *child = nullptr;
    if (found != head) {
      child = found->window;
      u32 nextHeight = height + static_cast<u32>(child->GetH());
      if (nextHeight > static_cast<u32>(GetH())) {
        u32 delta = nextHeight - static_cast<u32>(GetH());
        i32 bottom = GetB() + delta;
        i32 right = GetR(), top = GetT(), left = GetL();
        SetArea(left, top, right, bottom);
        top = GetT() - delta;
        left = GetL();
        GZWinMoveTo(left, top);
      }
      i32 top = GetT() + height, left = GetL();
      child->GZWinMoveTo(left, top);
      height = nextHeight;
      continue;
    }
    u32 width = 0, childHeight = 0;
    if (entry->item->kind == 0) {
      child = new WnToolsLineWindow(entry->item);
      width = WnToolsLineWidth;
      childHeight = WnToolsLineHeight;
    } else if (entry->item->kind == 1) {
      child = new WnToolsBarWindow(static_cast<ScToolsMeterItem *>(entry->item));
      width = WnToolsBarWidth;
      childHeight = WnToolsBarHeight;
    } else
      assert(false);
    child->AddRef();
    i32 bottom = GetT() + height + childHeight;
    i32 right = GetL() + width;
    i32 top = GetT() + height, left = GetL();
    child->SetArea(left, top, right, bottom);
    u32 nextHeight = height + childHeight;
    if (nextHeight > static_cast<u32>(GetH())) {
      u32 delta = nextHeight - static_cast<u32>(GetH());
      bottom = GetB() + delta;
      right = GetR();
      top = GetT();
      left = GetL();
      SetArea(left, top, right, bottom);
      top = GetT() - delta;
      left = GetL();
      GZWinMoveTo(left, top);
      top = GetT() + height;
      left = GetL();
      child->GZWinMoveTo(left, top);
    }
    ChildAdd(child);
    WnToolsWindowNode *node = new WnToolsWindowNode{head, head->previous, child};
    head->previous->next = node;
    head->previous = node;
    height = nextHeight;
  }
  for (;;) {
    WnToolsWindowNode *stale = head->next;
    while (stale != head) {
      ScToolsItemNode *entry = display->head->next;
      while (entry != display->head && entry->item != stale->window->item)
        entry = entry->next;
      if (entry == display->head)
        break;
      stale = stale->next;
    }
    if (stale == head)
      break;
    WnToolsLineWindow *child = stale->window;
    stale->previous->next = stale->next;
    stale->next->previous = stale->previous;
    delete stale;
    ChildRemove(child);
    child->Release();
  }
  if (height < static_cast<u32>(GetH())) {
    u32 delta = static_cast<u32>(GetH()) - height;
    i32 bottom = GetB() - delta;
    i32 right = GetR(), top = GetT(), left = GetL();
    SetArea(left, top, right, bottom);
    top = GetT() + delta;
    left = GetL();
    GZWinMoveTo(left, top);
  }
}

// Reconcile the display, bring it forward and show its children before setting its own visible flag.
void WnToolsWindow::ShowWindow() {
  ReconcileItems();
  PullToFront();
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    node->window->ShowWindow();
  WnToolsRootWidget::ShowWindow();
}

// Hide each child in forward order before applying the base window's own hidden flag.
void WnToolsWindow::HideWindow() {
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    node->window->HideWindow();
  WnToolsRootWidget::HideWindow();
}

// Mark an active display dirty for the next frame and report successful painting.
bool WnToolsWindow::GZPaint() {
  if (active)
    InvalidateSelf();
  return true;
}

// Hide on Escape and toggle the module's border visualization on R; other keys are not handled.
bool WnToolsWindow::OnKeyDown(u32 key, u32) {
  if (key == kWnKeyEscape)
    HideWindow();
  else if (key == kWnKeyR)
    WnToolsDrawBorders = !WnToolsDrawBorders;
  else
    return false;
  return true;
}
