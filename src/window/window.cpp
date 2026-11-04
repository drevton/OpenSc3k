#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "window/window.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>

#include "common/sc3_unknown.h"
#include "graphics/sdl_platform.h"
#include "window/sdl_input.h"

#include "audio/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_array.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/value.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/message_box.h"

// The window manager registers at this service ordering priority.
enum WnWindowManagerPriority : u32 {
  kWnWindowManagerServicePriority = 0x1312d0,
};

WnBitmap::WnBitmap() : image(nullptr), alpha_enabled(0), opacity(0.5f), source_area{}, bitmap_flags(2) {
}

WnBitmap::~WnBitmap() {
  WnBitmap::Shutdown();
}

bool WnBitmap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnBitmapControl, WnBitmapControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnBitmap::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnBitmap::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window from the bitmap's secondary interface without another retain.
WnWindow *WnBitmap::GetWindow() {
  return this;
}

bool WnBitmap::GetBitmapFlag(i32 mask) const {
  return ((u32)mask & bitmap_flags) != 0;
}

// Preserve an already initialized receiver; otherwise compute the source extent before entering root initialization.
bool WnBitmap::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  i32 height = GetH(), width = GetW();
  ScRect rectangle{0, 0, width, height};
  SetSourceBufferAreaFromWindowArea(rectangle);
  return WnRootWidget::Init();
}

bool WnBitmap::Shutdown() {
  ScRelease(&image);
  return WnRootWidget::Shutdown();
}

// Apply root geometry first, then derive the source extent from the live area and successive live image dimensions.
void WnBitmap::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  const ScRect *rectangle = GetArea();
  SetSourceBufferAreaFromWindowArea(*rectangle);
}

bool WnBitmap::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  return !GetBitmapFlag(4) && WnRootWidget::IsPointInWindowScreenCoordinates(x, y);
}

// Dispatch keyboard messages locally; flagged non-keyboard messages translate mouse coordinates and forward to the live
// parent.
bool WnBitmap::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp)
    return WnRootWidget::DoMessage(message);
  if (!GetBitmapFlag(1) || !parent)
    return false;
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel) {
    i32 left = GetL();
    message->data1 = (u32)message->data1 + (u32)left;
    i32 top = GetT();
    message->data2 = (u32)message->data2 + (u32)top;
  }
  return parent->DoMessage(message);
}

bool WnBitmap::GZPaint() {
  if (!(bitmap_flags & 2) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    draw_buffer->Fill(pixel, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (image && draw_buffer->Lock(GxLockFlags::Blt)) {
    if (alpha_enabled)
      draw_buffer->AlphaBlt(image, source_area, draw_origin, opacity, nullptr);
    else {
      ScRect destination{draw_area.left, draw_area.top, (i32)((u32)draw_area.left + (u32)source_area.right),
          (i32)((u32)draw_area.top + (u32)source_area.bottom)};
      draw_buffer->Blt(image, source_area, destination, nullptr);
    }
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

// Set a zero-origin source extent from window dimensions, clamping through each live image-size query in source order.
void WnBitmap::SetSourceBufferAreaFromWindowArea(const ScRect &rectangle) {
  i32 height = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
  i32 width = (i32)((u32)rectangle.right - (u32)rectangle.left);
  source_area = {0, 0, width, height};
  if (image) {
    i32 image_width = (i32)image->Width();
    if (source_area.right > image_width)
      source_area.right = image->Width();
    i32 image_height = image->Height();
    if (source_area.bottom > image_height)
      source_area.bottom = image->Height();
  }
}

// Retain incoming image before releasing the previous one, publish it, recompute the live extent and invalidate.
void WnBitmap::SetImage(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (image)
    image->Release();
  image = incoming;
  const ScRect *rectangle = GetArea();
  SetSourceBufferAreaFromWindowArea(*rectangle);
  InvalidateSelfAndParents();
}

// Store the Boolean enable and unchanged float amount, then invalidate through the live primary method.
void WnBitmap::SetAlpha(bool enabled, f32 amount) {
  alpha_enabled = enabled;
  opacity = amount;
  InvalidateSelfAndParents();
}

// Construct the real bitmap and return its primary window at reference count zero without an extra query or retain.
ScUnknown *WnConstructBitmap() {
  return static_cast<WnWindow *>(new WnBitmap);
}

// Change only the requested bitmap bits; the original setter neither invalidates nor reports a result.
void WnBitmap::SetBitmapFlag(i32 mask, bool enabled) {
  u32 bits = (u32)mask;
  if (enabled)
    bitmap_flags |= bits;
  else
    bitmap_flags &= ~bits;
}

bool WnBitmap::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnBitmap::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

WnFlatRect::WnFlatRect() : layout(0), border_flags(0), border_colors{} {
}

WnFlatRect::~WnFlatRect() {
  WnRootWidget::Shutdown();
}

// Publish the flat secondary interface only for its own IID; preserve root identities and the source's own-output null check.
bool WnFlatRect::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnFlatControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnFlatControl *>(this);
  AddRef();
  return true;
}

u32 WnFlatRect::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnFlatRect::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window without acquiring another hold.
WnWindow *WnFlatRect::GetWindow() {
  return this;
}

// Translate only the three forwarded mouse message kinds into parent coordinates before delivering through the live parent.
bool WnFlatRect::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_MouseDownL || message->type == kWnMsg_MouseUpL || message->type == kWnMsg_MouseMove) {
    i32 left = GetL();
    message->data1 = (u32)message->data1 + (u32)left;
    i32 top = GetT();
    message->data2 = (u32)message->data2 + (u32)top;
  }
  WnWindow *target = parent;
  return target ? target->DoMessage(message) : false;
}

bool WnFlatRect::OnKeyDown(u32 key, u32 modifiers) {
  ScMessage message{kWnMsg_KeyDown, key, modifiers, 0};
  return notification_target->DoMessage(&message);
}

bool WnFlatRect::OnKeyUp(u32 key, u32 modifiers) {
  ScMessage message{kWnMsg_KeyUp, key, modifiers, 0};
  return notification_target->DoMessage(&message);
}

// Store the raw flags before resolving optional bevel defaults, then force all edge bits after the live color setter returns.
u8 WnFlatRect::SetOutlineFlags(u8 requested) {
  border_flags = requested;
  if (requested & 0x30) {
    WnResources *resources = WnGetUiResources();
    u32 first = 0, second = 0xFFFF;
    if (resources) {
      first = resources->GetColor(19);
      second = resources->GetColor(20);
    }
    if (border_flags & 0x10)
      SetOutlineColors(second, second, first, first);
    else if (border_flags & 0x20)
      SetOutlineColors(first, first, second, second);
    border_flags |= 0x0F;
  }
  return 1;
}

u8 WnFlatRect::GetOutlineFlags() {
  return border_flags;
}

u8 WnFlatRect::GetOutlineFlag(i32 mask) {
  return (mask & border_flags) != 0;
}

void WnFlatRect::SetFillColor(u8 red, u8 green, u8 blue) {
  WnRootWidget::SetFillColor(red, green, blue);
}

void WnFlatRect::SetFillColor(u32 pixel) {
  WnRootWidget::SetFillColor(pixel);
}

void WnFlatRect::GetFillColor(u8 *red, u8 *green, u8 *blue) const {
  WnRootWidget::GetFillColor(red, green, blue);
}

u32 WnFlatRect::GetFillColor() const {
  return WnRootWidget::GetFillColor();
}

void WnFlatRect::SetOutlineColor(u8 index, u8 red, u8 green, u8 blue) {
  if (index <= 3)
    border_colors[index] = ((u32)red << 16) | ((u32)green << 8) | blue;
}

// Store one logical RGB value into every edge without adding invalidation or graphics conversion.
void WnFlatRect::SetOutlineColor(u8 red, u8 green, u8 blue) {
  u32 color = ((u32)red << 16) | ((u32)green << 8) | blue;
  for (u32 i = 0; i < 4; ++i)
    border_colors[i] = color;
}

// Unpack the native pixel into genuine byte outputs before dispatching the live all-edge RGB setter.
void WnFlatRect::SetOutlineColor(u32 pixel) {
  u8 red, green, blue;
  WnGetGraphics()->Convert16BitNativeValueToRGB((u16)pixel, &red, &green, &blue);
  SetOutlineColor(red, green, blue);
}

// Convert and store the four independent native pixels in source order, acquiring the live graphics interface for each edge.
void WnFlatRect::SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) {
  u32 pixels[4] = {left, top, right, bottom};
  for (u32 i = 0; i < 4; ++i) {
    u8 red, green, blue;
    WnGetGraphics()->Convert16BitNativeValueToRGB((u16)pixels[i], &red, &green, &blue);
    border_colors[i] = ((u32)red << 16) | ((u32)green << 8) | blue;
  }
}

// Publish each current edge pixel only after its own graphics conversion; earlier output writes remain visible to later calls.
void WnFlatRect::GetOutlineColors(u32 *left, u32 *top, u32 *right, u32 *bottom) {
  u32 *outputs[4] = {left, top, right, bottom};
  for (u32 i = 0; i < 4; ++i) {
    GxGraphics *graphics = WnGetGraphics();
    u32 color = border_colors[i];
    *outputs[i] = graphics->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
  }
}

// Copy the selected RGB bytes in source order; an unsupported byte index leaves every output untouched.
void WnFlatRect::GetOutlineColor(u8 index, u8 *red, u8 *green, u8 *blue) const {
  if (index <= 3) {
    *red = (u8)(border_colors[index] >> 16);
    *green = (u8)(border_colors[index] >> 8);
    *blue = (u8)border_colors[index];
  }
}

u8 WnFlatRect::SetSizeMode(u8 mode) {
  layout = mode;
  return 1;
}

u8 WnFlatRect::GetSizeMode() {
  return layout;
}

// Apply the source's selective horizontal or bottom-edge layout, clamping reversed dimensions before the live area setter.
u8 WnFlatRect::AutoSize(const ScRect &input) {
  if (!layout)
    return 1;
  ScRect rectangle;
  GetArea(&rectangle);
  switch (layout) {
  case 1:
  case 2:
  case 6:
    break;
  case 3:
    rectangle.right = input.right;
    break;
  case 4:
    rectangle.bottom = input.bottom;
    break;
  case 5:
    rectangle.left = input.left;
    rectangle.right = input.right;
    break;
  default:
    return 0;
  }
  if (rectangle.bottom < rectangle.top)
    rectangle.bottom = rectangle.top;
  if (rectangle.right < rectangle.left)
    rectangle.right = rectangle.left;
  SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  return 1;
}

// Fill unless suppressed, convert all four border colors before its lock, then draw each enabled edge in source order.
bool WnFlatRect::GZPaint() {
  if (!(border_flags & 0x40) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (border_flags & 0xBF) {
    u16 pixels[4];
    for (u32 i = 0; i < 4; ++i) {
      GxGraphics *graphics = WnGetGraphics();
      u32 color = border_colors[i];
      pixels[i] = graphics->ConvertRGBValueToNative16Bit((u8)(color >> 16), (u8)(color >> 8), (u8)color);
    }
    if (!draw_buffer->Lock(GxLockFlags::Line))
      return false;
    i32 left = draw_area.left, top = draw_area.top;
    i32 right = (i32)((u32)draw_area.right - 1), bottom = (i32)((u32)draw_area.bottom - 1);
    if (border_flags & 1)
      draw_buffer->DrawLine(left, draw_area.top, left, bottom, static_cast<i32>(pixels[0]), 1);
    if (border_flags & 2)
      draw_buffer->DrawLine(left, top, right, top, static_cast<i32>(pixels[1]), 1);
    if (border_flags & 4)
      draw_buffer->DrawLine(right, top, right, bottom, static_cast<i32>(pixels[2]), 1);
    if (border_flags & 8)
      draw_buffer->DrawLine(left, bottom, right, bottom, static_cast<i32>(pixels[3]), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

// Return the flat rectangle's primary window identity at zero references for a later retained class query.
ScUnknown *WnConstructFlatRect() {
  return static_cast<WnWindow *>(new WnFlatRect);
}

WnObject::WnObject() : references(0) {
}

WnObject::~WnObject() {
}

bool WnObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 WnObject::AddRef() {
  WnGetDirector()->AddRef();
  return ++references;
}

// Drop the module hold before deletion or ordinary unsigned decrement; zero-count release retains source wraparound.
u32 WnObject::Release() {
  if (references == 1) {
    WnGetDirector()->RemoveRef();
    delete this;
    return 0;
  }
  WnGetDirector()->RemoveRef();
  return --references;
}

WnSystemService::WnSystemService(u32 identifier, i32 priority)
    : service_id(identifier), service_priority(priority), running(false) {
}

WnSystemService::~WnSystemService() {
}

bool WnSystemService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  return WnObject::QueryInterface(iid, out);
}

u32 WnSystemService::AddRef() {
  return WnObject::AddRef();
}

u32 WnSystemService::Release() {
  return WnObject::Release();
}

u32 WnSystemService::GetServiceID() const {
  return service_id;
}

void WnSystemService::SetServiceID(u32 identifier) {
  service_id = identifier;
}

i32 WnSystemService::GetServicePriority() const {
  return service_priority;
}

bool WnSystemService::IsServiceRunning() const {
  return running;
}

void WnSystemService::SetServiceRunning(bool value) {
  running = value;
}

bool WnSystemService::Init() {
  return true;
}

bool WnSystemService::Shutdown() {
  return true;
}

bool WnSystemService::OnTick(u32) {
  return true;
}

bool WnSystemService::OnIdle(u32) {
  return true;
}

bool WnSystemService::OnInstall() {
  return true;
}

#define WN_ROOT_CLASS WnRootWidget
#define WN_ROOT_WINDOW_MANAGER_CONSTRUCTOR
#define WN_ROOT_MANAGER WnGetWindowManager
#define WN_ROOT_GRAPHICS WnGetGraphics
#define WN_ROOT_RESOURCES WnGetUiResources
#include "window/wn_root_widget_methods.inc"
#undef WN_ROOT_RESOURCES
#undef WN_ROOT_GRAPHICS
#undef WN_ROOT_MANAGER
#undef WN_ROOT_WINDOW_MANAGER_CONSTRUCTOR
#undef WN_ROOT_CLASS

WnTimerList::WnTimerList() : head(new WnTimerNode) {
  head->next = head;
  head->previous = head;
}

WnTimerList::~WnTimerList() {
  Clear();
  delete head;
}

// Copy actual job fields into a new tail node, preserving insertion order without copying source padding.
void WnTimerList::Append(const WnTimerJob &job) {
  auto *node = new WnTimerNode;
  node->job = {job.identifier, job.callback, job.context, job.once, job.delay, job.deadline, job.fixed};
  node->next = head;
  node->previous = head->previous;
  node->previous->next = node;
  head->previous = node;
}

// Unlink one real job before releasing its storage.
void WnTimerList::Erase(WnTimerNode *node) {
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
}

void WnTimerList::Clear() {
  while (head->next != head)
    Erase(head->next);
}

// Construct list, elapsed timer, worker and lock in source order, then initialize flags around the original priority change.
WnTimerQueue::WnTimerQueue() : elapsed(ScStopwatch::Milliseconds, 0) {
  u32 id;
  worker.GetThreadId(&id);
  stop = 0;
  next_id = 1;
  elapsed.SetResolution(ScStopwatch::Milliseconds);
  ScThread::SetCurrentPriority(2);
  running = 0;
}

// Request termination before checking worker liveness, then resume/wake and retain the source's one-second active-flag wait.
WnTimerQueue::~WnTimerQueue() {
  u32 id;
  worker.GetThreadId(&id);
  stop = 1;
  if (worker.IsRunning()) {
    ScStopwatch waiting(ScStopwatch::Milliseconds, 0);
    worker.Resume();
    worker.Wake();
    waiting.Restart();
    while (waiting.GetElapsedTime() < 1000 && running)
      ;
  }
}

// Register a callback on the actual global scheduler, consuming its ID before taking the list lock.
// Empty queues restart the clock and start/resume the worker; populated queues receive a wake request after append.
i32 WnTimerQueue::AddEvent(WnTimerCallback callback, WnEditorTimer *context, u32 delay, bool once, bool fixed) {
  u32 identifier = next_id++;
  lock.Lock();
  bool empty = list.head->next == list.head;
  if (empty)
    elapsed.Restart();
  u32 deadline = delay + elapsed.GetElapsedTime();
  WnTimerJob job{identifier, callback, context, once, delay, deadline, fixed};
  list.Append(job);
  if (empty) {
    if (!running) {
      running = 1;
      worker.Start(this);
    }
    worker.WakeForWork();
  } else
    worker.Wake();
  lock.Unlock();
  return (i32)identifier;
}

// Remove the first matching integer registration while holding the real queue lock, preserving the found/not-found result.
// High-bit IDs retain their original bit-pattern matching without a signed range check.
bool WnTimerQueue::RemoveEvent(i32 identifier) {
  lock.Lock();
  bool found = false;
  for (WnTimerNode *node = list.head->next; node != list.head; node = node->next)
    if (node->job.identifier == (u32)identifier) {
      list.Erase(node);
      found = true;
      break;
    }
  lock.Unlock();
  return found;
}

// Capture due jobs under the list lock, update repeating deadlines, then invoke the copied callbacks after unlocking.
// Preserve unsigned deadline saturation, elapsed sleep deduction, empty-queue suspension and the source worker status queries.
u32 WnTimerQueue::StartFunction(void *) {
  u32 id;
  worker.GetThreadId(&id);
  WnTimerList batch;
  running = 1;
  while (!stop) {
    if (list.head->next != list.head) {
      if (stop)
        break;
      lock.Lock();
      WnTimerNode *node = list.head->next;
      u32 minimum = UINT32_MAX, now = elapsed.GetElapsedTime();
      batch.Clear();
      while (node != list.head) {
        WnTimerNode *next = node->next;
        if (node->job.deadline <= now) {
          batch.Append(node->job);
          if (node->job.once) {
            list.Erase(node);
            node = next;
            continue;
          }
          node->job.deadline = node->job.delay + (node->job.fixed ? node->job.deadline : now);
          if (node->job.deadline < now)
            node->job.deadline = UINT32_MAX;
        }
        u32 delta = node->job.deadline - now;
        if (delta < minimum)
          minimum = delta;
        node = next;
      }
      lock.Unlock();
      for (node = batch.head->next; node != batch.head; node = node->next)
        node->job.callback(node->job.context);
      if (list.head->next != list.head) {
        if (stop)
          break;
        u32 spent = elapsed.GetElapsedTime() - now, wait = spent < minimum ? minimum - spent : 10;
        ScThread::SleepCurrentThread(wait * 1000u);
        worker.GetThreadId(&id);
        continue;
      }
    }
    if (stop)
      break;
    worker.GetThreadId(&id);
    worker.WaitForWake();
    worker.GetThreadId(&id);
  }
  worker.GetThreadId(&id);
  running = 0;
  return 0;
}

WnTimerQueue WnTimers;

// Query a temporary graphics service, retain its canvas, bind this manager and create the actual root window.
// Keep the fresh root's known rectangle setter but reload the live receiver after the canvas returns its area.
u8 WnWindowService::GSInit(GxGraphics *, GxBuffer *) {
  void *slot = nullptr;
  WnGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, &slot);
  GxGraphics *graphics = static_cast<GxGraphics *>(slot);
  canvas = ScAddRefNotNull(graphics->GetCanvas());
  canvas->SetWindowManager(this);
  main_window = ScAddRefNotNull(new WnRootWidget(this, &keyboard));
  ScRect area = canvas->GetArea();
  static_cast<WnRootWidget *>(main_window)->WnRootWidget::SetArea(area);
  if (graphics)
    graphics->Release();
  return true;
}

void WnWindowService::GSShutdown(GxGraphics *) {
}

void WnWindowService::ForceFullSystemRedraw() {
  if (canvas)
    canvas->ForceFullSystemRedraw();
}

// Notify the live canvas that its native window moved and retain the source's literal successful byte.
u8 WnWindowService::ProcessWindowMove() {
  if (canvas)
    canvas->ProcessWindowMove();
  return true;
}

// Complete the canvas's switch to fullscreen without replacing the source's unconditional success with its result.
u8 WnWindowService::CompleteSwitchToFullScreenMode() {
  if (canvas)
    canvas->CompleteSwitchToFullScreenMode();
  return true;
}

u8 WnWindowService::CompleteSwitchToWindowedMode() {
  if (canvas)
    canvas->CompleteSwitchToWindowedMode();
  return true;
}

void WnWindowService::PauseDrawing() {
  canvas->PauseDrawing();
}

void WnWindowService::ResumeDrawing() {
  canvas->ResumeDrawing();
}

// Retain the replacement first, clear live focus/capture, release the previous root and initialize/focus the new one.
bool WnWindowService::SetMainWin(WnWindow *window) {
  if (window)
    window->AddRef();
  if (main_window) {
    SetFocus(nullptr);
    ReleaseCapture(capture);
    main_window->Release();
  }
  main_window = window;
  if (window) {
    if (canvas) {
      ScRect area = canvas->GetArea();
      window->SetArea(area);
    }
    window->Init();
    SetFocus(window);
  }
  return true;
}

// Find the unsigned identifier's insertion slot without copying or retaining any registered class records.
WnWindowService::WnRegisteredWindow **WnWindowService::FindRegisteredWindow(u32 identifier) {
  WnRegisteredWindow **slot = &classes;
  while (*slot && (*slot)->identifier < identifier)
    slot = &(*slot)->next;
  return slot;
}

// A forced setting always wins; ordinary settings replace only a strictly lower unsigned priority.
bool WnWindowService::SetGZMsgBoxCMDID(u32 identifier, u32 priority, bool forced) {
  if (!forced && priority <= message_priority)
    return false;
  message_id = identifier;
  message_priority = priority;
  message_forced = forced;
  return true;
}

// Write all three stored default-command outputs in source order.
bool WnWindowService::GetGZMsgBoxCMDID(u32 *identifier, u32 *priority, bool *forced) const {
  *identifier = message_id;
  *priority = message_priority;
  *forced = message_forced;
  return true;
}

// Preserve unique insertion: passing the priority/force gate still cannot replace an already registered identifier.
bool WnWindowService::AddRegWin(u32 identifier, u32 first_class, u32 second_class, u32 priority, bool forced) {
  WnRegisteredWindow **slot = FindRegisteredWindow(identifier);
  if (*slot && (*slot)->identifier == identifier) {
    if ((*slot)->priority > priority && !forced)
      return false;
    return false;
  }
  *slot = new WnRegisteredWindow{*slot, identifier, first_class, second_class, priority, forced};
  return true;
}

// Remove one matching unsigned registration key without invoking any widget or catalog callbacks.
bool WnWindowService::RemoveRegWin(u32 identifier) {
  WnRegisteredWindow **slot = FindRegisteredWindow(identifier);
  if (!*slot || (*slot)->identifier != identifier)
    return false;
  WnRegisteredWindow *node = *slot;
  *slot = node->next;
  delete node;
  return true;
}

// Re-read the lowest live key after each virtual removal, so overrides determine subsequent progress.
bool WnWindowService::RemoveAllRegWins() {
  while (classes)
    RemoveRegWin(classes->identifier);
  return true;
}

// Preserve every output on a miss; on a hit publish the two classes, priority and force flag in order.
bool WnWindowService::GetRegWinInfo(
    u32 identifier, u32 *first_class, u32 *second_class, u32 *priority, bool *forced) const {
  WnRegisteredWindow *node = classes;
  while (node && node->identifier < identifier)
    node = node->next;
  if (!node || node->identifier != identifier)
    return false;
  *first_class = node->first_class;
  *second_class = node->second_class;
  *priority = node->priority;
  *forced = node->forced;
  return true;
}

// Copy the complete ordered registrations before callbacks. The local snapshot remains independent of owner mutation.
bool WnWindowService::EnumRegWins(void (*callback)(u32, u32, u32, u32, bool)) {
  struct WnRegistration {
    u32 identifier, first_class, second_class, priority;
    bool forced;
  };

  ScArray<WnRegistration> snapshot;
  for (WnRegisteredWindow *node = classes; node; node = node->next)
    *snapshot.Append() = {node->identifier, node->first_class, node->second_class, node->priority, node->forced};
  for (u32 i = 0; i < snapshot.size; ++i) {
    const WnRegistration &entry = snapshot.data[i];
    callback(entry.identifier, entry.first_class, entry.second_class, entry.priority, entry.forced);
  }
  return true;
}

// Construct registered windows in unsigned-key order, bind paired callbacks and release each acquired interface after attach.
bool WnWindowService::CreateRegWins() {
  void *callback = nullptr, *generic = nullptr, *plain = nullptr;
  for (WnRegisteredWindow *node = classes; node; node = node->next) {
    if (node->first_class) {
      if (WnGetCom()->GetClassObject(node->first_class, IID_UiWindowCallback, &callback)) {
        if (WnGetCom()->GetClassObject(node->second_class, IID_UiGenericInterface, &generic)) {
          UiGenericInterface *view = static_cast<UiGenericInterface *>(generic);
          view->SetCallback(static_cast<UiWindowCallback *>(callback));
          WnWindow *child = view->GetWindow();
          main_window->ChildAdd(child);
          view->Release();
          generic = nullptr;
        }
        static_cast<UiWindowCallback *>(callback)->Release();
        callback = nullptr;
      }
    } else if (WnGetCom()->GetClassObject(node->second_class, IID_WnWindow, &plain)) {
      main_window->ChildAdd(static_cast<WnWindow *>(plain));
      static_cast<WnWindow *>(plain)->Release();
      plain = nullptr;
    }
  }
  return true;
}

static ScRegistry *WnRegistry;

// Publish the acquired registry before releasing the temporary reference. Null acquisitions are retried later.
ScRegistry *WnGetRegistry() {
  if (!WnRegistry) {
    void *slot = nullptr;
    WnGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, &slot);
    ScRegistry *server = static_cast<ScRegistry *>(slot);
    WnRegistry = server;
    if (server)
      server->Release();
  }
  return WnRegistry;
}

// Parse ordinary identifiers as decimal, selecting hexadecimal for an explicit prefix or any hexadecimal letter.
static u32 WnParseIdentifier(const char *text) {
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    return (u32)strtoul(text, nullptr, 16);
  i32 base = 10;
  for (const char *at = text; *at; ++at)
    if ((*at >= 'a' && *at <= 'f') || (*at >= 'A' && *at <= 'F')) {
      base = 16;
      break;
    }
  return (u32)strtoul(text, nullptr, base);
}

// The registry callback only installs a nonzero identifier with nonzero priority; force remains false.
static void WnMessageCommand(const ScStringView &first, const ScStringView &second, void *context) {
  u32 identifier = WnParseIdentifier(first.ToChar());
  u32 priority = WnParseIdentifier(second.ToChar());
  if (identifier && priority)
    static_cast<WnWindowService *>(context)->SetGZMsgBoxCMDID(identifier, priority, false);
}

// Preserve the source's unusual two-conversion predicate even though its format also offers a third priority field.
static void WnRegisterCommand(const ScStringView &first, const ScStringView &second, void *context) {
  u32 first_class = 0, second_class = 0, priority = 0;
  u32 identifier = WnParseIdentifier(first.ToChar());
  if (!identifier)
    return;
  i32 count = sscanf(second.ToChar(), "0x%08X,0x%08X,0x%08X", &first_class, &second_class, &priority);
  if (count == 2)
    static_cast<WnWindowService *>(context)->AddRegWin(identifier, first_class, second_class, priority, false);
}

void WnWindowService::EnumRegistryEffects(const char *category, ScRegistry::ScRegistryCallback callback) {
  ScRegistry *registry = WnGetRegistry();
  if (registry)
    registry->EnumCategory(category, callback, this);
}

// Capture the framework before cursor construction, then enumerate each category through the live cached registry.
bool WnWindowService::Init() {
  if (!initialized) {
    framework = WnGetFramework();
    cursors = ScAddRefNotNull(new WnCursors);
    cursors->Init();
    EnumRegistryEffects("GZMsgBoxCMDID", WnMessageCommand);
    EnumRegistryEffects("GZRegWin", WnRegisterCommand);
    initialized = true;
  }
  return true;
}

static u32 WnMessageBudget, WnRemovalRecursion;

// Notify the old capture before replacing it, then notify the new capture with its own pointer.
bool WnWindowService::SetCapture(WnWindow *window) {
  if (capture)
    SendMessageToTarget(capture, kWnMsg_CaptureChanged, reinterpret_cast<uptr>(window), 0, 0);
  capture = window;
  if (window)
    SendMessageToTarget(window, kWnMsg_CaptureChanged, reinterpret_cast<uptr>(window), 0, 0);
  return true;
}

// Release matching capture, then post a native mouse move with the most recently observed coordinates and modifiers.
bool WnWindowService::ReleaseCapture(WnWindow *window) {
  if (capture != window)
    return false;
  SetCapture(nullptr);
  GZWinMsgPost_Mouse(11, (i32)mouse_x, (i32)mouse_y, mouse_modifiers);
  return true;
}

// Send leave-focus before storing the new pointer, then send enter-focus and return the callback-mutated live state.
WnWindow *WnWindowService::SetFocus(WnWindow *window) {
  WnWindow *old = focus;
  SendMessageToTarget(old, kWnMsg_KillFocus, reinterpret_cast<uptr>(window), 0, 0);
  focus = window;
  if (!window)
    return nullptr;
  SendMessageToTarget(window, kWnMsg_SetFocus, reinterpret_cast<uptr>(old), 0, 0);
  return focus;
}

// Capture both validity results before notifications. Re-read the old hover after callbacks before the enter message.
bool WnWindowService::SetCurrentMouseWin(WnWindow *window) {
  if (hover == window)
    return false;
  bool old_valid = IsWindowValid(hover), new_valid = IsWindowValid(window);
  ScMessage message{};
  if (old_valid) {
    message.type = kWnMsg_MouseExit;
    message.data1 = new_valid ? reinterpret_cast<uptr>(window) : 0;
    hover->DoMessage(&message);
  }
  if (new_valid) {
    message.type = kWnMsg_MouseEnter;
    message.data1 = old_valid ? reinterpret_cast<uptr>(hover) : 0;
    window->DoMessage(&message);
  } else
    window = nullptr;
  hover = window;
  return true;
}

// Repair capture, focus and hover references affected by removing a window or one of its descendants.
void WnWindowService::CleanUpWindowReferences(WnWindow *window) {
  if (IsWindowValid(capture) && (capture == window || capture->IsWinInParentChain(window)))
    ReleaseCapture(capture);
  if (!IsWindowValid(focus))
    SetFocus(main_window);
  else if (focus == window || focus->IsWinInParentChain(window)) {
    if (window->GetNotificationTarget())
      SetFocus(window->GetNotificationTarget());
    else
      SetFocus(main_window);
  }
  if (!IsWindowValid(hover) || hover == window || hover->IsWinInParentChain(window))
    hover = nullptr;
}

// Clean references even on an invalid target. A valid parent owns child deletion; a parentless target is held across close.
bool WnWindowService::DestroyWindowNow(WnWindow *window) {
  bool valid = IsWindowValid(window);
  WnWindow *parent = nullptr;
  CleanUpWindowReferences(window);
  if (valid)
    parent = window->GetParentWin();
  if (IsWindowValid(parent))
    return parent->ChildDeleteAbsolute(window);
  if (!valid)
    return false;
  window->AddRef();
  window->Shutdown();
  window->Release();
  return true;
}

// A pending ancestor suppresses a duplicate removal request, including a request for that ancestor itself.
bool WnWindowService::IsWindowPendingRemoval(WnWindow *window) {
  for (WnWindowRemoval *node = removals.head->next; node != removals.head; node = node->next)
    for (WnWindow *parent = window; parent; parent = parent->GetParentWin())
      if (parent == node->window)
        return true;
  return false;
}

// Destroy immediately outside a removal lock. Otherwise append a borrowed request only when no ancestor is pending.
bool WnWindowService::DestroyWindow(WnWindow *window) {
  if ((i32)removal_depth <= 0)
    return DestroyWindowNow(window);
  if (!IsWindowPendingRemoval(window)) {
    WnWindowRemoval *node = new WnWindowRemoval{removals.head, removals.head->previous, window};
    node->previous->next = node;
    removals.head->previous = node;
  }
  return true;
}

// Share the source recursion counter across managers and traverse live requests before discarding queue storage.
bool WnWindowService::ProcessWindowRemovals() {
  if ((i32)WnRemovalRecursion > 0)
    return false;
  ++WnRemovalRecursion;
  if ((i32)removal_depth <= 0) {
    for (WnWindowRemoval *node = removals.head->next; node != removals.head; node = node->next)
      DestroyWindowNow(node->window);
    WnWindowRemoval *node = removals.head->next;
    while (node != removals.head) {
      WnWindowRemoval *next = node->next;
      delete node;
      node = next;
    }
    removals.head->next = removals.head;
    removals.head->previous = removals.head;
  }
  --WnRemovalRecursion;
  return true;
}

u32 WnWindowService::LockWindowRemovals() {
  return ++removal_depth;
}

// Flush pending removals when the final positive lock is released. Nonpositive depths reset to zero.
u32 WnWindowService::UnlockWindowRemovals() {
  if ((i32)removal_depth > 0) {
    if (--removal_depth == 0)
      ProcessWindowRemovals();
    return removal_depth;
  }
  removal_depth = 0;
  return 0;
}

// Validate before building the scalar/pointer message and synchronously invoking the target's live handler.
bool WnWindowService::SendMessageToTarget(WnWindow *window, u32 type, uptr first, uptr second, uptr third) {
  if (!IsWindowValid(window))
    return false;
  ScMessage message{type, first, second, third};
  return window->DoMessage(&message);
}

// Reject null before permission lookup, then pass the genuine mutable message directly to the receiver.
bool WnWindowService::SendMessageToTarget(WnWindow *window, ScMessage *message) {
  if (!window || !IsWindowValid(window))
    return false;
  return window->DoMessage(message);
}

// Queue a value copy of the receiver and message, without validation or reference-count effects.
void WnWindowService::PostMessageToTarget(WnWindow *window, const ScMessage &message) {
  WnPostedMessage *node = new WnPostedMessage{messages, messages->previous, window, message};
  node->previous->next = node;
  messages->previous = node;
}

void WnWindowService::PostMessageToTarget(WnWindow *window, u32 type, uptr first, uptr second, uptr third) {
  ScMessage message{type, first, second, third};
  PostMessageToTarget(window, message);
}

// Pop before dispatch and share the source budget with nested pumps. Null invalid receivers broadcast in live hash order.
void WnWindowService::ProcessPostedMessages() {
  u32 remaining = WnMessageBudget;
  if ((i32)remaining <= 0) {
    remaining = 0;
    for (WnPostedMessage *node = messages->next; node != messages; node = node->next)
      ++remaining;
  }
  WnMessageBudget = remaining - 1;
  if ((i32)remaining <= 0)
    return;
  do {
    WnPostedMessage *node = messages->next;
    assert(node != messages);
    WnWindow *window = node->window;
    ScMessage message = node->message;
    node->next->previous = node->previous;
    node->previous->next = node->next;
    delete node;
    if (IsWindowValid(window))
      window->DoMessage(&message);
    else if (!window) {
      LockWindowRemovals();
      for (WnValidWindow *receiver = NextValidWindow(nullptr); receiver; receiver = NextValidWindow(receiver))
        receiver->window->DoMessage(&message);
      UnlockWindowRemovals();
    }
    remaining = WnMessageBudget--;
  } while ((i32)remaining > 0);
}

void WnWindowService::Plot() {
  ProcessPostedMessages();
  main_window->Plot();
}

u8 WnWindowService::Draw() {
  Plot();
  return true;
}

// 64-bit message fields still require temporary coordinate outputs.
// Store mouse state first, then route through capture, the modal window, or the main window in source order.
bool WnWindowService::ProcessMouseMessage(ScMessage *message) {
  ScMsgCode type = message->type;
  mouse_x = (u32)message->data1;
  mouse_y = (u32)message->data2;
  mouse_modifiers = (u32)message->data3;
  WnWindow *window = capture;
  if (window) {
    if (type == kWnMsg_MouseMove || window->IsEnabled()) {
      i32 x = (i32)(u32)message->data1, y = (i32)(u32)message->data2;
      window->ScreenToWindowCoordinates(&x, &y);
      message->data1 = (u32)x;
      message->data2 = (u32)y;
      if (type == kWnMsg_MouseDownL || type == kWnMsg_MouseDownR) {
        window->PullToFront();
        SetFocus(window);
      }
      SetCurrentMouseWin(window);
      return window->DoMessage(message);
    }
  } else if (modal) {
    bool inside = modal->IsPointInWindowScreenCoordinates((i32)(u32)message->data1, (i32)(u32)message->data2);
    if (inside && type != kWnMsg_MouseMove)
      inside = modal->IsEnabled();
    if (inside && modal->IsVisible())
      return modal->ProcessCursorMessage(message);
  } else
    return main_window->ProcessCursorMessage(message);
  SetCurrentMouseWin(main_window->GetChildWindowFromPoint((i32)(u32)message->data1, (i32)(u32)message->data2));
  return false;
}

bool WnWindowService::ProcessKeyboardMessage(ScMessage *message) {
  if (focus && focus->IsEnabled())
    return focus->DoMessage(message);
  return false;
}

// Play the optional default/explicit alert before trying the configured command-dialog path, then build a real fallback.
// Preserve modal result width and ordered root shutdown/control release; failed Configure retains the source's hold.
u32 WnWindowService::GZMsgBox(
    const ScStringView &message, const ScStringView &title, u32 options, bool sound, u32 sound_id) {
  i32 result = -1;
  void *slot = nullptr;
  if (sound) {
    if (!sound_id)
      sound_id = WnGetUiResources()->GetSoundId(0);
    if (WnGetSoundDevice())
      WnGetSoundDevice()->SndPlay((i32)sound_id, 1024, 0);
  }
  if (message_id && TryMessageBox(&result, message, title, options))
    return (u32)result;
  result = -1;
  if (!WnGetCom()->GetClassObject(kWnMessageBoxClassId, IID_WnMessageBoxControl, &slot)) {
    slot = static_cast<WnMessageBoxControl *>(new WnMessageBox);
    static_cast<WnMessageBoxControl *>(slot)->AddRef();
  }
  if (!slot || !static_cast<WnMessageBoxControl *>(slot)->Configure(message, title, options))
    return (u32)result;
  WnWindow *window = static_cast<WnMessageBoxControl *>(slot)->GetWindow();
  result = (i32)DoModalWin(window);
  window->Shutdown();
  static_cast<WnMessageBoxControl *>(slot)->Release();
  return (u32)result;
}

// Feed the configured command three retained values, then consume a transferred scalar result after releasing the command.
// Preserve failure outputs and reload the live result after its Release callback before deciding whether it is accepted.
bool WnWindowService::TryMessageBox(i32 *result, const ScStringView &message, const ScStringView &title, u32 options) {
  void *provider_slot = nullptr;
  WnGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, &provider_slot);
  bool accepted = false;
  void *command_slot = nullptr;
  if (provider_slot &&
      static_cast<ScCommandFactory *>(provider_slot)->GetCommand(message_id, IID_UiCommandInterface, &command_slot)) {
    ScVariant *value = new ScValue(message);
    value->AddRef();
    static_cast<UiCommandInterface *>(command_slot)->PushBackInput(value);
    value->Release();
    value = ScAddRefNotNull(new ScValue(title));
    static_cast<UiCommandInterface *>(command_slot)->PushBackInput(value);
    value->Release();
    value = ScAddRefNotNull(new ScValue(options, kScValueUint32));
    static_cast<UiCommandInterface *>(command_slot)->PushBackInput(value);
    value->Release();
    if (!static_cast<UiCommandInterface *>(command_slot)->Execute())
      static_cast<UiCommandInterface *>(command_slot)->Release();
    else {
      value = nullptr;
      if (!static_cast<UiCommandInterface *>(command_slot)->PopFrontOutput(&value))
        static_cast<UiCommandInterface *>(command_slot)->Release();
      else {
        static_cast<UiCommandInterface *>(command_slot)->Release();
        command_slot = nullptr;
        if (value->GetType() != kScValueUint32)
          value->Release();
        else {
          *result = (i32)*value->AsUint32();
          value->Release();
          u32 returned = (u32)*result;
          value = nullptr;
          accepted = returned >= kWnAbortButtonId && (returned <= kWnYesButtonId || returned == kWnModalQuit);
        }
      }
    }
  }
  if (provider_slot)
    static_cast<ScCommandFactory *>(provider_slot)->Release();
  return accepted;
}

u32 WnWindowService::DoModalWin(WnWindow *, WnWindow *window) {
  return DoModalWin(window);
}

// Attach the modal window and pump native messages, preserving the original failed-attach effects and nested state.
// Repost quit events so an enclosing modal pump can observe the same shutdown request.
u32 WnWindowService::DoModalWin(WnWindow *window) {
  if (!IsWindowValid(window))
    return kWnModalFailed;
  WnWindow *root = GetMainWindow();
  if (!IsWindowValid(root))
    return kWnModalFailed;
  ScMessageServer *receiver = WnGetMessageServer();
  u32 result = kWnModalFailed;
  ScMessage notification{};
  u32 idle = 0;
  WnWindow *old_focus = GetFocus(), *old_modal = modal;
  if (receiver) {
    notification.type = kWnMsg_ModalBegin;
    notification.data1 = window->GetID();
    receiver->MessageSend(notification);
  }
  window->SetFlag(kWnFlagModal, true);
  SetFocus(window);
  SetCapture(nullptr);
  ++modal_depth;
  if (root->ChildAdd(window)) {
    modal = window;
    u32 count = 0;
    SDL_Event event;
    for (;;) {
      GxSdlDrainFrame();
      if (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
          SDL_PushEvent(&event);
          framework->Quit(0);
          result = kWnModalQuit;
          break;
        }
        u32 type;
        uptr first, second;
        if (WnSdlPostedMessage(&event, &type, &first, &second) && type == kWnMsg_EndModal &&
            first == reinterpret_cast<uptr>(window)) {
          result = static_cast<u32>(second);
          break;
        }
        WnDispatchSdlEvent(&event);
        if (++count <= 500)
          continue;
      }
      count = 0;
      framework->OnIdle();
      if (!(idle++ & 1))
        SDL_Delay(0);
    }
    window->SetFlag(kWnFlagModal, false);
    root->ChildRemove(window);
    modal = old_modal;
    SetFocus(IsWindowValid(old_focus) ? old_focus : modal);
    framework->OnIdle();
  }
  --modal_depth;
  if (receiver) {
    notification.type = kWnMsg_ModalEnd;
    notification.data1 = window->GetID();
    notification.data2 = modal_depth;
    receiver->MessageSend(notification);
  }
  return result;
}

// Remove queued input while preserving leading key/button releases and one pending key press, then repost survivors.
u32 WnWindowService::FlushInputMessageQueue() {
  return WnSdlFlushInput();
}


// Acquire the canvas's actual Windows interface, post full native payload words, then release the queried interface.
bool WnWindowService::GZWinMsgPost(u32 type, uptr first, uptr second) {
  return WnSdlPostMessage(type, first, second);
}

bool WnWindowService::GZWinMsgPost_Mouse(u32 type, i32 x, i32 y, u32 modifiers) {
  return GZWinMsgPost(type, modifiers, (u16)x | ((u32)y << 16));
}

bool WnWindowService::GZWinMsgPost_Keyboard(u32 type, u32 key, u32 modifiers) {
  return GZWinMsgPost(type, key, modifiers);
}

// Sample the native cursor before querying the canvas's Windows view. Query failure leaves both caller outputs untouched.
// Ignore both native BOOLs, convert to client coordinates, publish x then y, and release the view after both writes.
void WnWindowService::GetCursorScreenPosition(i32 *x, i32 *y) {
  float window_x, window_y, game_x, game_y;
  SDL_GetMouseState(&window_x, &window_y);
  if (GxSdlWindowToGame(window_x, window_y, &game_x, &game_y)) {
    *x = static_cast<i32>(game_x);
    *y = static_cast<i32>(game_y);
  }
}

void WnWindowService::GetCursorRelativePosition(WnWindow *window, i32 *x, i32 *y) {
  GetCursorScreenPosition(x, y);
  window->ScreenToWindowCoordinates(x, y);
}

// Forward visibility to the graphics cursor operation and return its original signed counter.
// The Windows path also updates native visibility; SDL graphics owns both cursor forms.
i32 WnWindowService::GZShowCursor(bool visible) {
  return WnGetGraphics()->ShowCursor((u8)visible);
}

WnWindowService::WnValidWindows::WnValidWindows() : buckets(new WnValidWindow *[193]{}), capacity(193), count(0) {
}

// Native window-validity storage replaces generic container operations near.
// Discard validity nodes without invoking window methods or releasing their borrowed pointers.
WnWindowService::WnValidWindows::~WnValidWindows() {
  for (u32 i = 0; i < capacity; ++i) {
    WnValidWindow *node = buckets[i];
    while (node) {
      WnValidWindow *next = node->next;
      delete node;
      node = next;
    }
  }
  delete[] buckets;
}

WnWindowService::WnWindowRemovals::WnWindowRemovals() : head(new WnWindowRemoval) {
  head->next = head;
  head->previous = head;
}

// Retire removal storage without sending notifications or acquiring window references.
WnWindowService::WnWindowRemovals::~WnWindowRemovals() {
  WnWindowRemoval *node = head->next;
  while (node != head) {
    WnWindowRemoval *next = node->next;
    delete node;
    node = next;
  }
  delete head;
}

WnWindowService::WnWindowService()
    : WnSystemService(kWnWindowManagerServiceId, kWnWindowManagerServicePriority), initialized(false), capture(nullptr),
      focus(nullptr), modal(nullptr), hover(nullptr), main_window(nullptr), canvas(nullptr), removal_depth(0),
      messages(new WnPostedMessage), cursors(nullptr), mouse_x(0), mouse_y(0), mouse_modifiers(0), message_id(0),
      message_priority(0), message_forced(false), classes(nullptr), modal_depth(0) {
  messages->next = messages;
  messages->previous = messages;
}

// Shut down retained interfaces before raw container destruction. Automatic keyboard/removal/registry retirement follows.
WnWindowService::~WnWindowService() {
  Shutdown();
  if (cursors) {
    cursors->Shutdown();
    cursors->Release();
    cursors = nullptr;
  }
  ScRelease(&main_window);
  ScRelease(&canvas);
  while (classes) {
    WnRegisteredWindow *next = classes->next;
    delete classes;
    classes = next;
  }
  WnPostedMessage *node = messages->next;
  while (node != messages) {
    WnPostedMessage *next = node->next;
    delete node;
    node = next;
  }
  delete messages;
}

// Publish the selected real base interface before invoking the live AddRef method.
bool WnWindowService::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnWindowManager, WnWindowManager);
  HANDLE_QUERY_INTERFACE(IID_GxFrameListener, GxFrameListener);
  HANDLE_QUERY_INTERFACE(IID_ScSystemService, ScSystemService);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnObject);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 WnWindowService::AddRef() {
  return WnObject::AddRef();
}

u32 WnWindowService::Release() {
  return WnObject::Release();
}

// Release the reloaded cursor manager, main window and canvas in source order. Repeated shutdown is permitted.
bool WnWindowService::Shutdown() {
  if (initialized) {
    if (cursors) {
      cursors->Shutdown();
      cursors->Release();
      cursors = nullptr;
    }
    initialized = false;
  }
  ScRelease(&main_window);
  ScRelease(&canvas);
  return true;
}

// Return the current borrowed main window.
WnWindow *WnWindowService::GetMainWindow() {
  return main_window;
}

bool WnWindowService::GetMainWinAs(u32 iid, void **out) const {
  return main_window ? main_window->QueryInterface(iid, out) : false;
}

// Return the currently retained canvas as a borrowed interface.
GxCanvas *WnWindowService::GetCanvas() {
  return canvas;
}

// Find one pointer in the live validity hash. No retained identity or comparison token is used.
WnWindowService::WnValidWindow *WnWindowService::FindValidWindow(WnWindow *window) const {
  u32 bucket = (u32)((reinterpret_cast<uptr>(window) >> 2) % valid_windows.capacity);
  for (WnValidWindow *node = valid_windows.buckets[bucket]; node; node = node->next)
    if (node->window == window)
      return node;
  return nullptr;
}

// Move to the next live hash node using the current bucket layout, including any rehash performed by callbacks.
WnWindowService::WnValidWindow *WnWindowService::NextValidWindow(WnValidWindow *window) const {
  if (window && window->next)
    return window->next;
  u32 first = window ? (u32)((reinterpret_cast<uptr>(window->window) >> 2) % valid_windows.capacity) + 1 : 0;
  for (u32 bucket = first; bucket < valid_windows.capacity; ++bucket)
    if (valid_windows.buckets[bucket])
      return valid_windows.buckets[bucket];
  return nullptr;
}

void WnWindowService::GrowValidWindows(u32 count) {
  static const u32 kHashPrimes[] = {53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613, 393241,
      786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457,
      1610612741, 3221225473, 4294967291};
  if (count <= valid_windows.capacity)
    return;
  u32 capacity = valid_windows.capacity;
  for (u32 candidate : kHashPrimes)
    if (candidate >= count) {
      capacity = candidate;
      break;
    }
  if (capacity <= valid_windows.capacity)
    return;
  WnValidWindow **buckets = new WnValidWindow *[capacity] {};
  for (u32 i = 0; i < valid_windows.capacity; ++i)
    while (WnValidWindow *node = valid_windows.buckets[i]) {
      u32 bucket = (u32)((reinterpret_cast<uptr>(node->window) >> 2) % capacity);
      valid_windows.buckets[i] = node->next;
      node->next = buckets[bucket];
      buckets[bucket] = node;
    }
  delete[] valid_windows.buckets;
  valid_windows.buckets = buckets;
  valid_windows.capacity = capacity;
}

// Insert a previously absent borrowed window pointer. Registration does not change its reference count.
bool WnWindowService::AddWindowToValidList(WnWindow *window) {
  if (FindValidWindow(window))
    return false;
  GrowValidWindows(valid_windows.count + 1);
  u32 bucket = (u32)((reinterpret_cast<uptr>(window) >> 2) % valid_windows.capacity);
  valid_windows.buckets[bucket] = new WnValidWindow{valid_windows.buckets[bucket], window};
  ++valid_windows.count;
  return true;
}

// Erase the matching borrowed pointer without releasing the window.
bool WnWindowService::RemoveWindowFromValidList(WnWindow *window) {
  u32 bucket = (u32)((reinterpret_cast<uptr>(window) >> 2) % valid_windows.capacity);
  WnValidWindow **slot = &valid_windows.buckets[bucket];
  while (*slot && (*slot)->window != window)
    slot = &(*slot)->next;
  if (!*slot)
    return false;
  WnValidWindow *node = *slot;
  *slot = node->next;
  delete node;
  --valid_windows.count;
  return true;
}

bool WnWindowService::IsWindowValid(WnWindow *window) {
  return window && FindValidWindow(window);
}

// Delegate hit selection to the current main window, leaving an absent main window as an ordinary miss.
WnWindow *WnWindowService::GetWindowFromPoint(i32 x, i32 y) {
  return main_window ? main_window->GetChildWindowFromPoint(x, y) : nullptr;
}

void WnWindowService::InvalidateAllWindows() {
  for (WnValidWindow *node = NextValidWindow(nullptr); node; node = NextValidWindow(node))
    node->window->InvalidateSelf();
}

// Return the actual embedded keyboard, without treating it as the validity registry or a refcounted interface.
WnKeyboard *WnWindowService::GetKeyboard() {
  return &keyboard;
}

// Return the current borrowed cursor manager.
WnCursorManager *WnWindowService::GetCursorManager() {
  return cursors;
}

// Return the capture pointer without validating or retaining it.
WnWindow *WnWindowService::GetCapture() {
  return capture;
}

// Return the focus pointer without validating or retaining it.
WnWindow *WnWindowService::GetFocus() {
  return focus;
}

bool WnWindowService::IsModal() const {
  return modal_depth != 0;
}

u32 WnWindowService::GetModalNestCount() const {
  return modal_depth;
}

// Construct the concrete manager and return its primary basic-object identity at zero references for the registry.
ScUnknown *WnConstructWindowService() {
  return static_cast<WnObject *>(new WnWindowService);
}

bool WnWindowService::DebugIsFocusGadgetVisible() const {
  return false;
}

void WnWindowService::DebugSetFocusGadgetVisible(bool) {
}

bool WnWindowService::DebugIsCaptureGadgetVisible() const {
  return false;
}

void WnWindowService::DebugSetCaptureGadgetVisible(bool) {
}

bool WnWindowService::DebugIsInvalidateGadgetVisible() const {
  return false;
}

void WnWindowService::DebugSetInvalidateGadgetVisible(bool) {
}

void WnWindowService::DebugDumpWindowList() {
}
