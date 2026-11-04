#include "application/main_dialogs.h"

#include <float.h>

#include "audio/api.h"
#if defined(_WIN32)
#include <windows.h>
#endif

#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "application/animation_player.h"
#include "application/api.h"
#include "application/application_core.h"
#include "application/window_controls.h"
#include "city/api.h"
#include "city/timing.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/primary_buffer.h"
#include "graphics/sc3_effects.h"
#include "graphics/sc3_graphics.h"
#include "network/api.h"
#include "occupants/animation_manager.h"
#include "resources/api.h"
#include "resources/file.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/generic_dialogs.h"
#include "ui/graph_window.h"
#include "ui/ui_main_window.h"
#include "ui/ui_menu_config.h"
#include "ui/ui_menu_dialog.h"
#include "ui/ui_resource_handles.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/input.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// Splash, title, credits and cheat-input screens retain their distinct installed artwork keys.
enum ApMainScreenArtwork : u32 {
  kApDefaultCopyrightSplashImageId = 0x22721000,
  kApDefaultTitleBackgroundImageId = 0x22729921,
  kApCreditsLogoImageId = 0x22729940,
  kApCheatDialogBackgroundImageId = 0xc2bf4643,
};

// The main controller and dancing-bear intro assign separate identities to their own root windows.
enum ApRootWindowIdentity : u32 {
  kApMainWindowId = 0x6104489a,
  kApDancingBearWindowId = 0xa37c5bfd,
};

// Credits retain the original delay after their last line scrolls beyond the window.
enum ApCreditsDelay : i32 {
  kApCreditsExitDelay = 0xf4240,
};

// Main-window command payloads distinguish a dialog request from quitting; the main-menu request selects its dialog.
enum ApMainDialogCommand : u32 {
  kApMainMenuDialogRequest = 0x6104489d,
  kApProcessDialogCommand = 0x10001,
  kApQuitCommand = 0x10002,
};

enum UiToolTipWindowIdentity : u32 {
  kUiToolTipWindowId = 0x82f0121d,
};

// Logo wait selectors and modal replies are separate from the window message protocol.
static constexpr i32 kUiLogoWaitForPaints = 635915169;
static constexpr i32 kUiLogoWaitForInput = 635915170;
static constexpr u32 kUiLogoAutomaticResult = 1978207503u;
static constexpr u32 kUiLogoInputResult = 1978207504u;
static constexpr u32 kUiLogoPrimaryLabelId = 1978088384u;
static constexpr u32 kUiLogoSecondaryLabelId = 100254596u;

static constexpr u32 kUiAnyTipId = 0xdeadbeef;

static u32 ApMainPaintCount;
static UiMenuDialog *ApMainMenu;
static UiMainManager *UiMainManagerInstance;

ScAutoCursor::ScAutoCursor(u32 identifier) : pushed(false) {
  WnWindowManager *manager = ApGetDialogManager();
  if (manager) {
    WnCursorManager *cursor = manager->GetCursorManager();
    if (cursor)
      pushed = cursor->PushAutoCursor(identifier);
  }
}

ScAutoCursor::~ScAutoCursor() {
  if (pushed) {
    WnWindowManager *manager = ApGetDialogManager();
    if (manager) {
      WnCursorManager *cursor = manager->GetCursorManager();
      if (cursor)
        cursor->PopAutoCursor();
    }
  }
}

// Initialize the real main-UI manager while the current city is hidden-paused, preferring the city's window identity.
// An existing manager or failed class lookup retains the original successful result; only failed Init returns false.
bool UiToolbar::Init(WnWindow *window) {
  if (!ApGetDialogManager() || !ApGetDialogManager()->IsWindowValid(window))
    return false;
  CtCity *city = ApGetCity();
  if (city)
    city->SimHiddenPause();
  bool result = true;
  if (!UiMainManagerInstance &&
      ApGetCom()->GetClassObject(
          kUiMainManagerObjectClassId, IID_UiMainManagerObject, (void **)&UiMainManagerInstance)) {
    SpCityWindowView *view = ApGetCityWindow();
    WnWindow *target = nullptr;
    if (view && view->QueryInterface(IID_WnWindow, (void **)&target))
      result = UiMainManagerInstance->Init(target);
    else
      result = UiMainManagerInstance->Init(window);
  }
  if (city)
    city->SimHiddenUnpause();
  return result;
}

// Shut down the cached main-UI manager before releasing it, then clear the global after both callbacks.
// An empty cache also reports successful shutdown.
bool UiToolbar::Shutdown() {
  if (UiMainManagerInstance) {
    UiMainManagerInstance->Shutdown();
    UiMainManagerInstance->Release();
    UiMainManagerInstance = nullptr;
  }
  return true;
}

// Construct both window identities and mark the four unused fill rectangles by their left edges alone.
// The source/destination rectangles and super-spy pointer stay unwritten, as in the original constructor.
ApMainWindow::ApMainWindow()
    : application(nullptr), paint_mode(PaintMode::Black), menu_shown(false), city_loaded(false), title_buffer(nullptr),
      city_view(nullptr) {
  for (ScRect &rectangle : fill_rectangles)
    rectangle.left = -1;
}

ApMainWindow::~ApMainWindow() {
  Shutdown();
}

bool ApMainWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiMainWindow, UiMainWindow);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 ApMainWindow::AddRef() {
  return WnRootWidget::AddRef();
}

u32 ApMainWindow::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed root-window identity from the original secondary main-window receiver.
WnWindow *ApMainWindow::AsIGZWin() {
  return static_cast<WnRootWidget *>(this);
}

bool ApMainWindow::PreInit(ApGameInterface *value) {
  application = value;
  return true;
}

// Release the retained title image before clearing the field, preserving its visibility during release callbacks.
void ApMainWindow::ReleaseTitleBackgroundBuffer() {
  if (title_buffer) {
    title_buffer->Release();
    title_buffer = nullptr;
  }
}

// Draw black or the retained title image with the original separate fill and blit lock scopes.
// Every eighth paint services the local album and snapshot owner.
bool ApMainWindow::GZPaint() {
  if (paint_mode == PaintMode::Background && !city_loaded) {
    if (!title_buffer)
      DoTitleBackgroundScreen();
    if (fill_rectangles[0].left != -1 && draw_buffer->Lock(GxLockFlags::Fill)) {
      for (ScRect &rectangle : fill_rectangles)
        if (rectangle.left != -1)
          draw_buffer->Fill(0, rectangle, nullptr);
      draw_buffer->Unlock(GxLockFlags::Fill);
    }
    if (title_buffer && draw_buffer->Lock(GxLockFlags::Blt)) {
      draw_buffer->Blt(title_buffer, title_source, title_destination, nullptr);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  } else if (paint_mode == PaintMode::Black && draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(0, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (!(++ApMainPaintCount & 7)) {
    NwInternet *internet = application->GetSC3Internet();
    if (internet)
      internet->DoIdleProcessing();
  }
  return true;
}

bool ApMainWindow::DoMainMenu() {
  DoTitleBackgroundScreen();
  return PostMsg(this, kWnMsg_Command, kApProcessDialogCommand, kApMainMenuDialogRequest, 0);
}

bool ApMainWindow::ProcessDialogRequest(u32 command) {
  return command == kApMainMenuDialogRequest ? ActivateMainMenu() : false;
}

// Route dialog commands and the quit command through their original receivers and Boolean replies.
bool ApMainWindow::OnCommand(u32 kind, u32 command) {
  if (kind == kApProcessDialogCommand)
    return ProcessDialogRequest(command);
  if (kind == kApQuitCommand)
    return application->Quit(true, true);
  return false;
}

// Queue the main menu on an unmodified left press only while no city is loaded; always consume the press.
bool ApMainWindow::OnMouseDownL(i32, i32, u32 modifiers) {
  if (!(modifiers & kWnModifierShift) && !city_loaded)
    PostMsg(this, kWnMsg_Command, kApProcessDialogCommand, kApMainMenuDialogRequest, 0);
  return true;
}

// Offer the key to the notification target first, then handle the original space and system-info shortcuts.
// Unhandled keys retain the release build's false debug-handler result.
bool ApMainWindow::OnKeyDown(u32 key, u32 modifiers) {
  if (!SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0)) {
    if (key == kWnKeySpace) {
      if (!city_loaded)
        PostMsg(this, kWnMsg_Command, kApProcessDialogCommand, kApMainMenuDialogRequest, 0);
    } else {
      if (key != kWnKeyV || !(modifiers & kWnModifierControl) || (modifiers & kWnModifierShift))
        return DebugKeyDown(key, modifiers);
      DoSystemInfoWindow();
    }
  }
  return true;
}

bool ApMainWindow::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

bool ApMainWindow::DbgDoDebugWindow(ScMessage *) {
  return false;
}

bool ApMainWindow::DbgDoDebugWindow(u32, u32, u32, u32) {
  return false;
}

bool ApMainWindow::DebugKeyDown(u32, u32) {
  return false;
}

// Initialize the root, size it from two live main-window queries, and parent the local transfer owner.
// Subscribe the controller to city teardown, city ready and native-message notifications.
bool ApMainWindow::Init() {
  if (GetInitFlag())
    return true;
  WnRootWidget::Init();
  SetID(kApMainWindowId);
  SetFlag(kWnFlagReorderable, false);
  WnWindow *height_window = GetMainWindow(), *width_window = GetMainWindow();
  i32 height = height_window->GetH(), width = width_window->GetW();
  SetArea(0, 0, width, height);
  NwInternet *internet = application->GetSC3Internet();
  if (internet) {
    internet->SetParentWindow(this);
    NwTransferSystem *transfer = internet->GetTransferSystem();
    if (transfer)
      transfer->SetParentWindow(this);
  }
  ScMessageServer *messages = ApGetMessageServer();
  if (messages) {
    messages->AddNotification(this, kApMsgPreCityChange);
    messages->AddNotification(this, kApMsgCityReady);
    messages->AddNotification(this, kWnMsg_ProcHook);
  }
  return true;
}

// Disconnect the application's borrowed city view, hide the root and retire the toolbar before releasing its backdrop.
// Remove the three controller subscriptions before the qualified root shutdown; an uninitialized root needs no work.
bool ApMainWindow::Shutdown() {
  if (!GetInitFlag())
    return true;
  if (application)
    application->SetWinCityView(nullptr);
  HideWindow();
  SetNotificationTarget(nullptr);
  InitToolbar(false);
  if (title_buffer) {
    title_buffer->Release();
    title_buffer = nullptr;
  }
  ScMessageServer *messages = ApGetMessageServer();
  if (messages) {
    messages->RemoveNotification(this, kApMsgPreCityChange);
    messages->RemoveNotification(this, kApMsgCityReady);
    messages->RemoveNotification(this, kWnMsg_ProcHook);
  }
  return WnRootWidget::Shutdown();
}

// Select the original toolbar startup or teardown entry and ignore its result, as the controller does.
bool ApMainWindow::InitToolbar(bool enable) {
  if (enable)
    UiToolbar::Init(this);
  else
    UiToolbar::Shutdown();
  return true;
}

// Dispatch a native procedure hook only on the backend that exposes its original window messages.
static bool ApHandleMainWindowProcHook([[maybe_unused]] ApMainWindow *window, [[maybe_unused]] ScMessage *message) {
  return false;
}

// Connect city-ready notifications to the real toolbar/view and clear the backdrop before flushing queued input.
// Route the native close button through the existing quit command.
bool ApMainWindow::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kApMsgPreCityChange:
    SetNotificationTarget(nullptr);
    city_loaded = false;
    return true;
  case kWnMsg_ActivateApp:
    ApGetMessageServer()->MessagePost(*message, false);
    return true;
  case kApMsgCityReady: {
    InitToolbar(true);
    DoShowWinCityView();
    if (ApGetCityWindow())
      SetNotificationTarget(ApGetCityWindow()->AsIGZWin());
    city_loaded = true;
    paint_mode = PaintMode::Nothing;
    if (title_buffer) {
      title_buffer->Release();
      title_buffer = nullptr;
    }
    if (owner)
      owner->FlushInputMessageQueue();
    RsManager *resources = ApGetResourceManager();
    if (resources)
      resources->ForceGarbageCollection();
    return true;
  }
  case kWnMsg_ProcHook:
    if (ApHandleMainWindowProcHook(this, message))
      return false;
    return WnRootWidget::DoMessage(message);
  default:
    return WnRootWidget::DoMessage(message);
  }
}

// Run the opening window modally with its presentation cursor and suspend the main window's background painting.
// Notify BoxX before entering the modal loop, then restore painting before shutting down and releasing the window.
bool ApMainWindow::DoDancingBear() {
  WnWindow *presentation = nullptr;
  if (!ApGetCom()->GetClassObject(kApDancingBearClassId, IID_WnWindow, (void **)&presentation))
    return false;
  WnCursorManager *cursors = GetCursorManager();
  WnCursor *wait_cursor = cursors ? cursors->GetIGZCursor(kSpBlankCursorId) : nullptr;
  if (wait_cursor)
    presentation->SetCursor(wait_cursor, true);
  PaintMode previous = paint_mode;
  paint_mode = PaintMode::Nothing;
  ApGameInterface *game = nullptr;
  if (ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, (void **)&game)) {
    ApBoxX *box = game->GetBoxX();
    game->Release();
    box->Event(1036, 0, 0, 0, 0);
  }
  owner->DoModalWin(presentation);
  paint_mode = previous;
  presentation->Shutdown();
  presentation->Release();
  return true;
}

// Present the configured splash image and copyright strings modally, selecting the original automatic wait mode.
bool ApMainWindow::DoCopyRightScreen(bool delay) {
  RsKey key{};
  if (!UiReadSplashKey("SPLASHBMP", &key)) {
    key.group = kRsMainImageGroup;
    key.instance = kApDefaultCopyrightSplashImageId;
  }
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageHandle image(key.instance, key.group, GxVramRequest::SystemMemory, format);
  UiTextHandle first(kRsSupplementalGameCopyrightText, kRsSupplementalGameTextGroup),
      second(kRsArchitectADivisionOfElectronicArtsTMText, kRsArchitectTextGroup);
  UiLogoWindow *logo = new UiLogoWindow;
  logo->AddRef();
  logo->SetWaitType(delay ? kUiLogoWaitForPaints : kUiLogoWaitForInput);
  logo->SetBuffer(image.GetBuffer(), true);
  logo->SetLabelText(0, first.GetText());
  logo->SetLabelText(1, second.GetText());
  paint_mode = PaintMode::Nothing;
  GetWindowManager()->DoModalWin(logo);
  logo->Shutdown();
  logo->Release();
  return false;
}

// Load the original SKU-selected title image once, then center it or crop it to the current draw area.
// Cache the four black-fill rectangles only for the original narrower-image case before enabling background paint.
bool ApMainWindow::DoTitleBackgroundScreen() {
  if (!title_buffer) {
    RsKey key{};
    if (!UiReadSplashKey("WINSC3BMP", &key)) {
      key.group = kRsMainImageGroup;
      key.instance = kApDefaultTitleBackgroundImageId;
    }
    const GxColorType format(GxColorType::Color::RGB16);
    UiImageHandle image(key.instance, key.group, GxVramRequest::SystemMemory, format);
    title_buffer = image.GetBuffer();
    if (!title_buffer)
      return false;
    title_buffer->AddRef();
    title_buffer->GetBufferArea(&title_source);
    if (i32(u32(title_source.right) - u32(title_source.left)) > GetW()) {
      title_destination = draw_area;
      i32 width = i32(u32(title_destination.right) - u32(title_destination.left));
      i32 height = i32(u32(title_destination.bottom) - u32(title_destination.top));
      i32 y = i32(u32(title_source.bottom) + u32(title_destination.top) - u32(title_destination.bottom) -
          u32(title_source.top)) / 2;
      i32 x = i32(u32(title_destination.left) + u32(title_source.right) - u32(title_destination.right) -
          u32(title_source.left)) / 2;
      title_source = {x, y, i32(u32(x) + u32(width)), i32(u32(y) + u32(height))};
    } else {
      i32 width = i32(u32(title_source.right) - u32(title_source.left));
      i32 height = i32(u32(title_source.bottom) - u32(title_source.top));
      title_destination = {0, 0, width, height};
      i32 image_height = i32(u32(title_source.bottom) - u32(title_source.top));
      i32 image_width = i32(u32(title_source.right) - u32(title_source.left));
      i32 y = i32(u32(GetH()) - u32(image_height)) / 2;
      i32 x = i32(u32(GetW()) - u32(image_width)) / 2;
      title_destination.left = i32(u32(title_destination.left) + u32(x));
      title_destination.right = i32(u32(title_destination.right) + u32(x));
      title_destination.top = i32(u32(title_destination.top) + u32(y));
      title_destination.bottom = i32(u32(title_destination.bottom) + u32(y));
      if (i32(u32(title_source.right) - u32(title_source.left)) < GetW()) {
        fill_rectangles[0] = {draw_area.left, draw_area.top, draw_area.right, title_destination.top};
        fill_rectangles[1] = {draw_area.left, title_destination.top, title_destination.left, title_destination.bottom};
        fill_rectangles[2] = {
            title_destination.right, title_destination.top, draw_area.right, title_destination.bottom};
        fill_rectangles[3] = {draw_area.left, title_destination.bottom, draw_area.right, draw_area.bottom};
      }
    }
  }
  paint_mode = PaintMode::Background;
  return true;
}

// Run the real menu window until resume, confirmed new/load, close or accepted quit ends its modal command loop.
// Preserve the one-time BoxX event, shared output pointer, command lifetimes and original window-flag transitions.
bool ApMainWindow::ActivateMainMenu() {
  if (ApMainMenu)
    return false;
  ApMainMenu = new UiMenuDialog;
  ApMainMenu->AddRef();
  if (!menu_shown) {
    ApGameInterface *game = nullptr;
    if (ApGetFramework()->Application()->QueryInterface(IID_ApGameInterface, reinterpret_cast<void **>(&game))) {
      ApBoxX *box = game->GetBoxX();
      game->Release();
      box->Event(kAuEventMainMenu, 0, 0, 0, 0);
    }
  }
  menu_shown = true;
  ScVariant *value = nullptr;
  bool keep_menu = true;
  while (keep_menu) {
    u32 selection = owner->DoModalWin(ApMainMenu);
    switch (selection) {
    case kUiMainMenuResumeCityId:
      if (city_loaded)
        DoShowWinCityView();
      keep_menu = false;
      break;
    case kUiMainMenuNewCityId: {
      ScAutoCommand create(kCtNewCityCommandId);
      if (create.IsValid()) {
        create->Execute();
        if (create->PopFrontOutput(&value)) {
          u8 accepted = *value->AsBool();
          value->Release();
          value = nullptr;
          if (accepted == 1)
            keep_menu = false;
        }
      }
      break;
    }
    case kUiMainMenuLoadCityId:
    case kUiMainMenuLoadTerrainId:
    case kUiMainMenuLoadStarterTownId:
    case kUiMainMenuLoadScenarioId: {
      ScAutoCommand load(kCtLoadCityCommandId);
      if (!load.IsValid())
        break;
      SetFlag(kWnFlagEnabled, false);
      UiCommandInterface *command = load;
      u32 resource;
      switch (selection) {
      case kUiMainMenuLoadCityId:
        resource = kCtFileOpenCity;
        break;
      case kUiMainMenuLoadTerrainId:
        resource = kCtFileOpenTerrain;
        break;
      case kUiMainMenuLoadStarterTownId:
        resource = kCtFileOpenStarterTown;
        break;
      default:
        resource = kCtFileOpenScenario;
        break;
      }
      ScValue *input = new ScValue;
      input->AddRef();
      value = input;
      input->SetValUint32(resource);
      command->PushBackInput(value);
      value->Release();
      value = nullptr;
      input = new ScValue;
      input->AddRef();
      value = input;
      input->SetValBool(true);
      command->PushBackInput(value);
      value->Release();
      value = nullptr;
      if (command->Execute() && command->OutputQueueSize()) {
        command->PopFrontOutput(&value);
        u8 accepted = *value->AsBool();
        value->Release();
        value = nullptr;
        keep_menu = accepted == 0;
      }
      SetFlag(kWnFlagEnabled, true);
      break;
    }
    case kUiMainMenuQuitId:
      if (application->Quit(true, true))
        keep_menu = false;
      break;
    case kWnModalQuit:
      keep_menu = false;
      break;
    }
  }
  ApMainMenu->Shutdown();
  ApMainMenu->Release();
  ApMainMenu = nullptr;
  return true;
}

// Construct each input before borrowing the command, preserving filename callbacks that can replace its held interface.
// Queue the original selector, false query flag and filename; only a successful Execute marks the city loaded.
bool ApMainWindow::DoLoadCity(ScStringView *filename) {
  ScAutoCommand load(kCtLoadCityCommandId);
  if (!load.IsValid())
    return false;
  ScVariant *parameter = new ScValue(kCtFileOpenCity, kScValueUint32);
  load->PushBackInput(parameter);
  parameter = new ScValue(false);
  load->PushBackInput(parameter);
  parameter = new ScValue(*filename);
  load->PushBackInput(parameter);
  bool loaded = load->Execute();
  if (loaded)
    city_loaded = true;
  return loaded;
}

// Acquire and attach the city-view window once, installing its resource-backed keyboard accelerator when available.
// Show the window behind its siblings with the original temporary 2048 flag, including for an existing view.
bool ApMainWindow::DoShowWinCityView() {
  WnWindow *window;
  if (city_view) {
    window = city_view->AsIGZWin();
  } else {
    ApGetCom()->GetClassObject(kSpCityWindowViewClassId, IID_SpCityWindowView, (void **)&city_view);
    if (!city_view)
      return true;
    window = city_view->AsIGZWin();
    window->SetID(kUiMainCityViewportId);
    ChildAdd(window);
    application->SetWinCityView(city_view);
    RsKey key(kRsAcceleratorResourceType, kRsArchitectAcceleratorGroup, 2);
    WnAcceleratorResources *resources = nullptr;
    ApGetResourceManager()->GetResource(key, IID_WnAcceleratorResources, (void **)&resources);
    if (resources) {
      WnAccelerator *loaded_accelerator = nullptr;
      if (ApGetCom()->GetClassObject(kWnKeyAcceleratorClassId, IID_WnAccelerator, (void **)&loaded_accelerator)) {
        if (loaded_accelerator->Init(resources))
          window->SetKeyboardAccelerator(loaded_accelerator);
        loaded_accelerator->Release();
      }
      resources->Release();
    }
  }
  if (window) {
    window->ShowWindow();
    window->SetFlag(kWnFlagReorderable, true);
    ChildToBack(window);
    window->SetFlag(kWnFlagReorderable, false);
  }
  return true;
}

// Construct the original system-information procedure and run its created window through the live manager.
// Shut down and release the window before releasing the procedure, preserving failure ownership and the Boolean result.
bool ApMainWindow::DoSystemInfoWindow() {
  bool result = false;
  ApSysInfoWindow *procedure = new ApSysInfoWindow;
  procedure->AddRef();
  WnWindow *window = nullptr;
  if (procedure->CreateSysInfoWindow(&window)) {
    owner->DoModalWin(window);
    window->Shutdown();
    window->Release();
    result = true;
  }
  procedure->Release();
  return result;
}

ApRuntime::ScOccupantAreaSelectionHandler::ScOccupantAreaSelectionHandler()
    : dragging(0), previous{INT32_MAX, INT32_MAX, INT32_MAX}, occupants(nullptr) {
}

ApRuntime::ScOccupantAreaSelectionHandler::~ScOccupantAreaSelectionHandler() {
}

bool ApRuntime::ScOccupantAreaSelectionHandler::DoMessage(ScMessage *) {
  return false;
}

// Pick an occupant or cell, initialize both endpoints and dispatch the initial bounds.
bool ApRuntime::ScOccupantAreaSelectionHandler::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (view->PerformPick(x, y, &point, true)) {
    dragging = 1;
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
    if (bounds.CoordInBoundsXZ(point)) {
      ScOccupant *occupant = nullptr;
      if (occupants->GetOccupantAtCell(u32(point.x >> 8), u32(point.z >> 8), &occupant)) {
        occupant->GetBounds(&first);
        occupant->GetBounds(&second);
        occupant->Release();
      } else {
        first = GeBounds(point);
        second = GeBounds(point);
      }
      combined.UnionXZ(first, second);
      return ExecuteOnMouseDown(first);
    }
  }
  return false;
}

// Complete a valid occupant-area selection and clear its cursor and cost display.
bool ApRuntime::ScOccupantAreaSelectionHandler::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage;
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, true)) {
      dragging = 0;
      if (bounds.CoordInBoundsXZ(point)) {
        ScOccupant *occupant = nullptr;
        if (occupants->GetOccupantAtCell(u32(point.x >> 8), u32(point.z >> 8), &occupant)) {
          occupant->GetBounds(&second);
          occupant->Release();
        } else {
          second = GeBounds(point);
          if (first.minimum.x == INT32_MAX)
            first = GeBounds(point);
        }
      }
    }
    if (bounds.CoordInBoundsXZ(first.minimum) && bounds.CoordInBoundsXZ(first.maximum) &&
        bounds.CoordInBoundsXZ(second.minimum) && bounds.CoordInBoundsXZ(second.maximum)) {
      combined.UnionXZ(first, second);
      ClearStatus();
      ExecuteOnMouseUp(combined);
      if (edge_scroll)
        ExecuteRectDraw(combined, &damage);
      UpdateStatusDisplay();
    }
  }
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  dragging = 0;
  Dettach(ApGetCityWindow());
  return true;
}

// Update the occupant-area preview and dispatch drag or hover callbacks for valid city coordinates.
bool ApRuntime::ScOccupantAreaSelectionHandler::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage;
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if ((dragging && panning) || (!dragging && cursor_visible)) {
    cursor_visible = 0;
    window->SetCellCursorVisible(false);
  } else if (dragging && !cursor_visible) {
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
  }
  if (panning || cursor_visible)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, true) && bounds.CoordInBoundsXZ(point)) {
    ScOccupant *occupant = nullptr;
    if (dragging) {
      if (occupants->GetOccupantAtCell(u32(point.x >> 8), u32(point.z >> 8), &occupant)) {
        occupant->GetBounds(&second);
        occupant->Release();
      } else {
        second = GeBounds(point);
        if (first.minimum.x == INT32_MAX)
          first = GeBounds(point);
      }
      combined.UnionXZ(first, second);
    } else {
      if (occupants->GetOccupantAtCell(u32(point.x >> 8), u32(point.z >> 8), &occupant)) {
        occupant->GetBounds(&first);
        occupant->GetBounds(&second);
        occupant->Release();
      } else {
        first = GeBounds(point);
        second = GeBounds(point);
      }
      combined = first;
    }
    if (!panning) {
      if (dragging) {
        if (bounds.CoordInBoundsXZ(combined.minimum) && bounds.CoordInBoundsXZ(combined.maximum)) {
          ClearStatus();
          ExecuteOnMouseDrag(combined);
          UpdateStatusDisplay();
          if (edge_scroll)
            ExecuteRectDraw(combined, &damage);
          manager->FlushInputMessageQueue();
        }
      } else if (bounds.CoordInBoundsXZ(first.minimum) && bounds.CoordInBoundsXZ(first.maximum)) {
        ClearStatus();
        ExecuteOnMouseMove(first);
        UpdateStatusDisplay();
        if (edge_scroll)
          ExecuteRectDraw(first, &damage);
      }
    }
  }
  return true;
}

// Cancel panning and cost display, invalidate horizontal endpoints and reset the combined preview.
void ApRuntime::ScOccupantAreaSelectionHandler::OnResetState() {
  if (panning)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
  first.minimum.x = INT32_MAX;
  first.minimum.z = INT32_MAX;
  first.maximum.x = INT32_MAX;
  first.maximum.z = INT32_MAX;
  second = first;
  combined = first;
  cost = 0;
}

bool ApRuntime::ScOccupantAreaSelectionHandler::IsValidHandlerByView(i32, u32) const {
  return true;
}

bool ApRuntime::ScOccupantAreaSelectionHandler::ExecuteOnMouseDown(const GeBounds &) {
  return true;
}

bool ApRuntime::ScOccupantAreaSelectionHandler::ExecuteOnMouseUp(const GeBounds &) {
  return true;
}

bool ApRuntime::ScOccupantAreaSelectionHandler::ExecuteOnMouseDrag(const GeBounds &) {
  return true;
}

bool ApRuntime::ScOccupantAreaSelectionHandler::ExecuteOnMouseMove(const GeBounds &) {
  return true;
}

// Draw the area with palette value 35 when cursor updates are enabled.
bool ApRuntime::ScOccupantAreaSelectionHandler::ExecuteRectDraw(const GeBounds &selection, GeBounds *) {
  if (edge_scroll)
    cursor->SetRect(selection, 35);
  return true;
}

void ApRuntime::ScOccupantAreaSelectionHandler::ExecuteOnResetState() {
}

bool ApRuntime::ScOccupantAreaSelectionHandler::Dettach(SpCityWindowView *input) {
  return ScSelectionHandler::Dettach(input);
}

ApRuntime::ScOccupantSelectionHandler::ScOccupantSelectionHandler()
    : dragging(0), current{INT32_MAX, INT32_MAX, INT32_MAX}, previous{INT32_MAX, INT32_MAX, INT32_MAX} {
}

ApRuntime::ScOccupantSelectionHandler::~ScOccupantSelectionHandler() {
}

bool ApRuntime::ScOccupantSelectionHandler::DoMessage(ScMessage *) {
  return false;
}

// Pick an occupant coordinate and dispatch the press when it is within the city.
bool ApRuntime::ScOccupantSelectionHandler::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (view->PerformPick(x, y, &point, true)) {
    current = point;
    dragging = 1;
    if (bounds.CoordInBoundsXZ(point))
      return ExecuteOnMouseDown(point);
  } else {
    dragging = 0;
    cursor->Clear(true);
  }
  return false;
}

// Complete a picked occupant, clear the cursor and notify the original message server.
bool ApRuntime::ScOccupantSelectionHandler::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage;
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, true))
      dragging = 0;
    if (bounds.CoordInBoundsXZ(point))
      current = point;
    if (bounds.CoordInBoundsXZ(current)) {
      if (edge_scroll)
        ExecuteOccDraw(current, &damage);
      ExecuteOnMouseUp(current);
    }
  }
  dragging = 0;
  cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  Dettach(ApGetCityWindow());
  ScMessage message(kWnMsg_ProcessDeferredCityHandlerReset);
  ApGetMessageServer()->MessageSend(message);
  return true;
}

// Pick the hovered occupant, update panning and redraw its cursor when enabled.
bool ApRuntime::ScOccupantSelectionHandler::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage;
  bool result = false;
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if (!dragging || panning) {
    if (dragging && panning) {
      cursor_visible = 0;
      window->SetCellCursorVisible(false);
    }
    if (panning)
      window->UpdateScroll(i16(x), i16(y));
  }
  if (view->PerformPick(x, y, &point, true) && bounds.CoordInBoundsXZ(point)) {
    current = point;
    dragging = 1;
    if (!panning) {
      result = ExecuteOnMouseMove(point);
      if (edge_scroll)
        ExecuteOccDraw(current, &damage);
    }
  }
  return result;
}

// Stop active panning and hide the cost display when their flags are set.
void ApRuntime::ScOccupantSelectionHandler::OnResetState() {
  if (panning)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
}

bool ApRuntime::ScOccupantSelectionHandler::IsValidHandlerByView(i32, u32) const {
  return true;
}

bool ApRuntime::ScOccupantSelectionHandler::ExecuteOnMouseDown(const CtCoord &) {
  return true;
}

bool ApRuntime::ScOccupantSelectionHandler::ExecuteOnMouseUp(const CtCoord &) {
  return true;
}

bool ApRuntime::ScOccupantSelectionHandler::ExecuteOnMouseMove(const CtCoord &) {
  return true;
}

// Draw the selected occupant coordinate as a single-cell rectangle when enabled.
bool ApRuntime::ScOccupantSelectionHandler::ExecuteOccDraw(const CtCoord &point, GeBounds *) {
  if (edge_scroll) {
    GeBounds rectangle{point, point};
    cursor->SetRect(rectangle, 35);
  }
  return true;
}

void ApRuntime::ScOccupantSelectionHandler::ExecuteOnResetState() {
}

bool ApRuntime::ScOccupantSelectionHandler::Dettach(SpCityWindowView *input) {
  return ScSelectionHandler::Dettach(input);
}

ApRuntime::ScLineSelectionHandler::ScLineSelectionHandler()
    : dragging(0), first{INT32_MAX, INT32_MAX, INT32_MAX}, second{INT32_MAX, INT32_MAX, INT32_MAX},
      previous{INT32_MAX, INT32_MAX, INT32_MAX} {
}

ApRuntime::ScLineSelectionHandler::~ScLineSelectionHandler() {
}

bool ApRuntime::ScLineSelectionHandler::DoMessage(ScMessage *) {
  return false;
}

// Pick the initial endpoint and notify the tool when it lies within the city.
bool ApRuntime::ScLineSelectionHandler::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (view->PerformPick(x, y, &point, false)) {
    dragging = 1;
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
    if (bounds.CoordInBoundsXZ(point)) {
      first = point;
      ClearStatus();
      ExecuteOnMouseDown(first);
      UpdateStatusDisplay();
    }
  }
  return true;
}

// Finish the line selection, release its enumerator and clear the cursor display.
bool ApRuntime::ScLineSelectionHandler::OnMouseUpL(i32 x, i32 y, u32 flags) {
  SpSelection *selection = nullptr;
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, false)) {
      dragging = 0;
      if (bounds.CoordInBoundsXZ(first) && bounds.CoordInBoundsXZ(point))
        second = point;
    }
    if (edge_scroll && bounds.CoordInBoundsXZ(first) && bounds.CoordInBoundsXZ(second)) {
      ClearStatus();
      ExecuteLineDraw(first, second, &selection);
      ExecuteOnMouseUp(selection);
      UpdateStatusDisplay();
      if (selection)
        selection->Release();
    }
  }
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  dragging = 0;
  Dettach(ApGetCityWindow());
  return true;
}

// Update scrolling and dispatch a line preview for a dragged or hovered city cell.
bool ApRuntime::ScLineSelectionHandler::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if (!dragging || panning) {
    if (cursor_visible) {
      cursor_visible = 0;
      window->SetCellCursorVisible(false);
    }
  } else if (!cursor_visible) {
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
  }
  if (panning || cursor_visible)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, false)) {
    SpSelection *selection = nullptr;
    if (dragging) {
      if (bounds.CoordInBoundsXZ(first) && bounds.CoordInBoundsXZ(point)) {
        second = point;
        if (edge_scroll && !panning) {
          ClearStatus();
          ExecuteLineDraw(first, second, &selection);
          ExecuteOnMouseDrag(selection);
          manager->FlushInputMessageQueue();
          UpdateStatusDisplay();
        }
      }
    } else if (bounds.CoordInBoundsXZ(point)) {
      first = point;
      if (edge_scroll && !panning) {
        ClearStatus();
        ExecuteLineDraw(first, first, &selection);
        ExecuteOnMouseMove(selection);
        UpdateStatusDisplay();
      }
    }
    if (selection)
      selection->Release();
  }
  return true;
}

// Stop active scrolling and cost display, then invalidate both horizontal endpoints.
void ApRuntime::ScLineSelectionHandler::OnResetState() {
  if (panning)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
  first.x = INT32_MAX;
  first.z = INT32_MAX;
  second.x = INT32_MAX;
  second.z = INT32_MAX;
}

bool ApRuntime::ScLineSelectionHandler::IsValidHandlerByView(i32, u32) const {
  return true;
}

bool ApRuntime::ScLineSelectionHandler::ExecuteOnMouseDown(const CtCoord &) {
  return true;
}

bool ApRuntime::ScLineSelectionHandler::ExecuteOnMouseUp(SpSelection *) {
  return true;
}

bool ApRuntime::ScLineSelectionHandler::ExecuteOnMouseDrag(SpSelection *) {
  return true;
}

bool ApRuntime::ScLineSelectionHandler::ExecuteOnMouseMove(SpSelection *) {
  return true;
}

bool ApRuntime::ScLineSelectionHandler::ExecuteLineDraw(const CtCoord &start, const CtCoord &end,
    SpSelection **selection) {
  cursor->SetLine(start, end, 35);
  cursor->GetEnumerator(selection);
  return true;
}

void ApRuntime::ScLineSelectionHandler::ExecuteOnResetState() {
}

ApRuntime::ScRectSelectionHandler::ScRectSelectionHandler() : dragging(0) {
}

ApRuntime::ScRectSelectionHandler::~ScRectSelectionHandler() {
}

bool ApRuntime::ScRectSelectionHandler::DoMessage(ScMessage *) {
  return false;
}

// Pick the pressed coordinate and begin a rectangle selection when it lies inside the city bounds.
bool ApRuntime::ScRectSelectionHandler::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  if (view->PerformPick(x, y, &point, false)) {
    dragging = 1;
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
    if (bounds.CoordInBoundsXZ(point)) {
      rectangle.minimum = point;
      ClearStatus();
      ExecuteOnMouseDown(point);
      UpdateStatusDisplay();
    }
  }
  return true;
}

// Complete a valid rectangle, clear the cursor and cost display, and process deferred handler retirement.
bool ApRuntime::ScRectSelectionHandler::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage;
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, false)) {
      dragging = 0;
      if (bounds.CoordInBoundsXZ(rectangle.minimum) && bounds.CoordInBoundsXZ(point))
        rectangle.maximum = point;
    }
    if (!panning && bounds.CoordInBoundsXZ(rectangle.minimum) && bounds.CoordInBoundsXZ(rectangle.maximum)) {
      if (edge_scroll)
        ExecuteRectDraw(rectangle, &damage);
      ClearStatus();
      ExecuteOnMouseUp(rectangle);
      UpdateStatusDisplay();
    }
  }
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  rectangle.maximum.x = -1;
  rectangle.maximum.z = -1;
  dragging = 0;
  Dettach(ApGetCityWindow());
  return true;
}

// Update scrolling and the rectangle preview, dispatching drag or hover callbacks for valid picked cells.
bool ApRuntime::ScRectSelectionHandler::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point{INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage;
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if (!dragging || panning) {
    if (cursor_visible) {
      cursor_visible = 0;
      window->SetCellCursorVisible(false);
    }
  } else if (!cursor_visible) {
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
  }
  if (panning || cursor_visible)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, false) && bounds.CoordInBoundsXZ(point)) {
    if (dragging) {
      if (cursor->InBounds(rectangle.minimum) && cursor->InBounds(point) && !panning) {
        rectangle.maximum = point;
        ClearStatus();
        ExecuteOnMouseDrag(rectangle);
        UpdateStatusDisplay();
        if (edge_scroll)
          ExecuteRectDraw(rectangle, &damage);
        manager->FlushInputMessageQueue();
      }
    } else if (cursor->InBounds(point) && !panning) {
      rectangle.maximum = point;
      rectangle.minimum = point;
      ClearStatus();
      ExecuteOnMouseMove(point);
      UpdateStatusDisplay();
      if (edge_scroll)
        ExecuteRectDraw(rectangle, &damage);
    }
  }
  return true;
}

// Reset panning and cost display, then invalidate the two horizontal endpoints of the active rectangle.
void ApRuntime::ScRectSelectionHandler::OnResetState() {
  if (panning)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
  rectangle.minimum.x = INT32_MAX;
  rectangle.minimum.z = INT32_MAX;
  rectangle.maximum.x = INT32_MAX;
  rectangle.maximum.z = INT32_MAX;
}

bool ApRuntime::ScRectSelectionHandler::IsValidHandlerByView(i32, u32) const {
  return true;
}

bool ApRuntime::ScRectSelectionHandler::ExecuteOnMouseDown(const CtCoord &) {
  return true;
}

bool ApRuntime::ScRectSelectionHandler::ExecuteOnMouseUp(const GeBounds &) {
  return true;
}

bool ApRuntime::ScRectSelectionHandler::ExecuteOnMouseDrag(const GeBounds &) {
  return true;
}

bool ApRuntime::ScRectSelectionHandler::ExecuteOnMouseMove(const CtCoord &) {
  return true;
}

// Draw the selection with palette value 35 when cursor updates are enabled.
bool ApRuntime::ScRectSelectionHandler::ExecuteRectDraw(const GeBounds &selection, GeBounds *) {
  if (edge_scroll)
    cursor->SetRect(selection, 35);
  return true;
}

void ApRuntime::ScRectSelectionHandler::ExecuteOnResetState() {
}

ApRuntime::ScSelectionHandler::ScSelectionHandler()
    : manager(nullptr), window(nullptr), view(nullptr), cursor(nullptr), budget(nullptr),
      bounds{{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}}, panning(0), cursor_visible(0),
      edge_scroll(0), cost_visible(0), cost(0), invalid(0), unaffordable(0), conflict(0), retiring(0) {
}

ApRuntime::ScSelectionHandler::~ScSelectionHandler() {
  if (budget) {
    budget->Release();
    budget = nullptr;
  }
  if (cursor) {
    cursor->Release();
    cursor = nullptr;
  }
  if (view) {
    view->Release();
    view = nullptr;
  }
  if (window) {
    window->Release();
    window = nullptr;
  }
}

bool ApRuntime::ScSelectionHandler::DoMessage(ScMessage *) {
  return false;
}

bool ApRuntime::ScSelectionHandler::Init() {
  return 1;
}

bool ApRuntime::ScSelectionHandler::Shutdown() {
  window->SetEdgeScroll(false);
  window->SetCostDisplay(false, 0, 0);
  edge_scroll = 0;
  return 1;
}

bool ApRuntime::ScSelectionHandler::OnKeyDown(u32 key, u32 flags) {
  return window->DefOnKeyDown(key, flags);
}

bool ApRuntime::ScSelectionHandler::OnKeyUp(u32 key, u32 flags) {
  return window->DefOnKeyUp(key, flags);
}

// Forward the left press before the deferred handler-retirement phase.
bool ApRuntime::ScSelectionHandler::OnMouseDownL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseDownL(x, y, flags);
  Dettach(ApGetCityWindow());
  return result;
}

bool ApRuntime::ScSelectionHandler::OnMouseDownR(i32 x, i32 y, u32) {
  panning = 1;
  window->SetMouseScroll(i16(x), i16(y), true);
  edge_scroll = 0;
  window->SetEdgeScroll(false);
  cost_visible = 0;
  window->SetCostDisplay(false, 0, 0);
  return 1;
}

// Forward the left release before testing deferred retirement.
bool ApRuntime::ScSelectionHandler::OnMouseUpL(i32 x, i32 y, u32 flags) {
  bool result = window->DefOnMouseUpL(x, y, flags);
  Dettach(ApGetCityWindow());
  return result;
}

// End panning, enable edge scrolling and then process deferred retirement.
bool ApRuntime::ScSelectionHandler::OnMouseUpR(i32 x, i32 y, u32) {
  panning = 0;
  window->SetMouseScroll(i16(x), i16(y), false);
  edge_scroll = 1;
  window->SetEdgeScroll(true);
  cost_visible = 1;
  Dettach(ApGetCityWindow());
  return 1;
}

bool ApRuntime::ScSelectionHandler::OnMouseMove(i32 x, i32 y, u32 flags) {
  if (!edge_scroll) {
    window->SetCellCursorSize(1, 1);
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  bool result = window->DefOnMouseMove(x, y, flags);
  Dettach(ApGetCityWindow());
  return result;
}

bool ApRuntime::ScSelectionHandler::GetCursor(u32 *out) {
  *out = kSpCenterCursorId;
  return 1;
}

// Cancel active pan/cost display effects and reset the stored cost.
void ApRuntime::ScSelectionHandler::OnResetState() {
  if (panning)
    window->SetMouseScroll(0, 0, false);
  if (cost_visible)
    window->SetCostDisplay(false, 0, 0);
  cost = 0;
}

bool ApRuntime::ScSelectionHandler::IsValidHandlerByView(i32, u32) const {
  return 1;
}

// Retire an active handler only when its deferred-retirement byte is set.
bool ApRuntime::ScSelectionHandler::Dettach(SpCityWindowView *) {
  if (!retiring)
    return false;
  if (panning) {
    panning = 0;
    window->SetMouseScroll(0, 0, false);
  }
  if (cursor_visible) {
    cursor_visible = 0;
    window->SetCellCursorVisible(false);
  }
  return window->SetActiveHandler(nullptr);
}

// Clear the three original status bytes while retaining the displayed cost.
void ApRuntime::ScSelectionHandler::ClearStatus() {
  invalid = 0;
  unaffordable = 0;
  conflict = 0;
}

// Update only the status byte selected by the original integer argument.
void ApRuntime::ScSelectionHandler::SetStatus(i32 status, bool enabled) {
  if (status == 0)
    invalid = enabled;
  else if (status == 1)
    unaffordable = enabled;
  else if (status == 2)
    conflict = enabled;
}

void ApRuntime::ScSelectionHandler::UpdateStatusDisplay() {
  i32 amount = cost;
  unaffordable = amount != 0 && i64(amount) > budget->GetTotalFunds();
  if (!panning)
    window->SetCostDisplay(true, amount, unaffordable != 0);
}

// Compare live cost against the budget after retrieving available funds.
void ApRuntime::ScSelectionHandler::CheckBudget() {
  i64 funds = budget->GetTotalFunds();
  i32 amount = cost;
  unaffordable = amount != 0 && i64(amount) > funds;
}

// Acquire the real city-view interfaces, bounds and budget before installing the handler.
bool ApRuntime::ScSelectionHandler::Attach(SpCityWindowView *input) {
  manager = ApGetDialogManager();
  window = input;
  input->AddRef();
  view = window->GetCurrentView();
  view->AddRef();
  cursor = window->GetCellCursorMap();
  cursor->AddRef();
  CtCity *city = ApGetCity();
  bounds.minimum = {0, 0, 0};
  u32 last_z = city->CellCountZ() - 1, width = city->CellCountX();
  bounds.maximum.x = i32((width << 8) - 256);
  bounds.maximum.z = i32(last_z << 8);
  bounds.maximum.altitude = 0;
  budget = city->BudgetLayer();
  budget->AddRef();
  bool accepted = false;
  void *object;
  if (QueryInterface(IID_SpViewHandler, &object)) {
    SpViewHandler *handler = static_cast<SpViewHandler *>(object);
    accepted = window->SetActiveHandler(handler);
    handler->Release();
    object = nullptr;
  }
  cursor->SetDefaultColor(0, 35);
  u32 cursor_id;
  GetCursor(&cursor_id);
  window->SetActiveCursor(cursor_id);
  return accepted;
}

ApRuntime::ScPointSelectionHandler::ScPointSelectionHandler()
    : dragging(0), current{INT32_MAX, INT32_MAX, INT32_MAX}, previous{INT32_MAX, INT32_MAX, INT32_MAX} {
}

// Pick the pressed point, start dragging and issue the original preview callbacks.
bool ApRuntime::ScPointSelectionHandler::OnMouseDownL(i32 x, i32 y, u32) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  bool result = false;
  if (view->PerformPick(x, y, &point, false)) {
    dragging = 1;
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
    if (bounds.CoordInBoundsXZ(point)) {
      current = point;
      ClearStatus();
      result = ExecuteOnMouseDown(current);
      UpdateStatusDisplay();
      if (edge_scroll)
        ExecuteCellDraw(current, &damage);
    }
  }
  return result;
}

// Finish a valid drag before hiding its cursor and processing deferred retirement.
bool ApRuntime::ScPointSelectionHandler::OnMouseUpL(i32 x, i32 y, u32 flags) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
  if (!(flags & 1)) {
    if (view->PerformPick(x, y, &point, false)) {
      dragging = 0;
      if (bounds.CoordInBoundsXZ(point))
        current = point;
    }
    if (bounds.CoordInBoundsXZ(current)) {
      ClearStatus();
      ExecuteOnMouseUp(current);
      UpdateStatusDisplay();
      if (edge_scroll)
        ExecuteCellDraw(current, &damage);
    }
  }
  cursor_visible = 0;
  window->SetCellCursorVisible(false);
  if (edge_scroll)
    cursor->Clear(true);
  window->SetCostDisplay(false, 0, 0);
  dragging = 0;
  Dettach(ApGetCityWindow());
  return 1;
}

// Update panning and the live point before issuing drag or hover callbacks.
bool ApRuntime::ScPointSelectionHandler::OnMouseMove(i32 x, i32 y, u32) {
  CtCoord point = {INT32_MAX, INT32_MAX, INT32_MAX};
  if (!edge_scroll) {
    window->SetEdgeScroll(true);
    edge_scroll = 1;
  }
  if (!dragging || panning) {
    if (cursor_visible) {
      cursor_visible = 0;
      window->SetCellCursorVisible(false);
    }
  } else if (!cursor_visible) {
    cursor_visible = 1;
    window->SetCellCursorVisible(true);
  }
  if (panning || cursor_visible)
    window->UpdateScroll(i16(x), i16(y));
  if (view->PerformPick(x, y, &point, false) && bounds.CoordInBoundsXZ(point)) {
    GeBounds damage = {{INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX}};
    if (bounds.CoordInBoundsXZ(point)) {
      current = point;
      if (!panning) {
        ClearStatus();
        if (dragging) {
          ExecuteOnMouseDrag(current);
          manager->FlushInputMessageQueue();
        } else
          ExecuteOnMouseMove(current);
        UpdateStatusDisplay();
        ExecuteCellDraw(current, &damage);
      }
    }
  }
  return 1;
}

bool ApRuntime::ScPointSelectionHandler::ExecuteOnMouseDown(const CtCoord &) {
  return 1;
}

bool ApRuntime::ScPointSelectionHandler::ExecuteOnMouseUp(const CtCoord &) {
  return 1;
}

bool ApRuntime::ScPointSelectionHandler::ExecuteOnMouseDrag(const CtCoord &) {
  return 1;
}

bool ApRuntime::ScPointSelectionHandler::ExecuteOnMouseMove(const CtCoord &) {
  return 1;
}

// Draw the original single-cell preview when edge scrolling is active.
bool ApRuntime::ScPointSelectionHandler::ExecuteCellDraw(const CtCoord &point, GeBounds *) {
  if (edge_scroll)
    cursor->SetCell(point, 35);
  return 1;
}

void ApRuntime::ScPointSelectionHandler::ExecuteOnResetState() {
}

ApRuntime::ScPointSelectionHandler::~ScPointSelectionHandler() {
}

bool ApRuntime::ScPointSelectionHandler::DoMessage(ScMessage *) {
  return false;
}

bool ApRuntime::ScPointSelectionHandler::Init() {
  return ScSelectionHandler::Init();
}

bool ApRuntime::ScPointSelectionHandler::Shutdown() {
  return ScSelectionHandler::Shutdown();
}

void ApRuntime::ScPointSelectionHandler::OnResetState() {
  ScSelectionHandler::OnResetState();
}

bool ApRuntime::ScPointSelectionHandler::IsValidHandlerByView(i32, u32) const {
  return true;
}

static u32 ApCreditsShadowColor;
static bool ApCreditsEndNotified;

// Preserve the active x87 precision on 32-bit Windows and the native rounding direction on all platforms.
static u16 ApCreditsFloatingControl() {
  u16 result = ScCounterControl();
#if defined(_M_IX86)
  unsigned int control;
  _controlfp_s(&control, 0, 0);
  result = (result & ~0x0c00) | ((control & _MCW_RC) << 2);
  switch (control & _MCW_PC) {
  case _PC_24: result &= ~0x0300; break;
  case _PC_64: result = (result & ~0x0300) | 0x0300; break;
  default: break;
  }
#endif
  return result;
}

// Construct the credits dialog with its original timing, scroll limits and empty string list.
ApCreditsWindow::ApCreditsWindow()
    : WnGeneralDialog(WnDialogStyle::Edge), fade_buffer(nullptr), text_buffer(nullptr), logo_buffer(nullptr),
      title_font(nullptr), names_font(nullptr), timer(ScStopwatch::Milliseconds, 0), start_time(0), next_line_space(20),
      scroll_rate(0.025f), minimum_scroll(0), maximum_scroll(4), fade_in{}, fade_out{}, title_color(0), names_color(0) {
  style = WnDialogStyle::None;
}

ApCreditsWindow::~ApCreditsWindow() {
  ApCreditsWindow::Shutdown();
}

// Construct the scrolling credits resources, load their text and tunables, and capture input for the dialog.
bool ApCreditsWindow::Init() {
  if (GetInitFlag())
    return true;
  ScRect bounds = *ApGetDialogManager()->GetMainWindow()->GetArea();
  ScString directory;
  ApApp *application = ApGetFramework()->Application();
  static_cast<ApApplication *>(application)->GetAppDataDirectory(&directory);
  directory.Append("Res\\UI\\shared\\Credits\\");
  GxColorType format(GxColorType::Color::RGB16);
  {
    UiImageHandle logo(kApCreditsLogoImageId, kRsMainImageGroup, GxVramRequest::SystemMemory, format);
    logo_buffer = logo.GetBuffer();
  }
  logo_buffer->AddRef();
  u32 transparent_color = MakeFillColor(255, 0, 255);
  logo_buffer->SetTransparency(transparent_color);
  i32 logo_height = static_cast<i32>(logo_buffer->Height());
  i32 top = i32(u32(logo_height) + 40u), height = i32(u32(bounds.bottom) - u32(logo_height) - 60u);
  if (bounds.bottom >= 600) {
    i32 inset = (bounds.bottom - 600) / 2;
    if (bounds.bottom - 20 > inset + 600)
      height = i32(u32(inset) - u32(top) + 580u);
  }
  SetSize(550, height);
  GZWinMoveTo(i32(u32(bounds.right) - 550u) / 2, top);
  ApGetGraphics()->CreateBuffer(&text_buffer);
  i32 width = i32(u32(area.right) - u32(area.left));
  i32 window_height = i32(u32(area.bottom) - u32(area.top));
  text_buffer->Init(static_cast<u32>(width), u32(window_height) + 40u, format, nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (text_buffer->Lock(static_cast<GxLockFlags>(1))) {
    text_buffer->Fill(0, static_cast<ScRect *>(nullptr));
    text_buffer->Unlock(static_cast<GxLockFlags>(1));
  }
  text_buffer->SetTransparency(0);
  width = i32(u32(area.right) - u32(area.left));
  window_height = i32(u32(area.bottom) - u32(area.top));
  fade_in = {0, i32(u32(window_height) - 72u), width, window_height};
  fade_out = {0, 0, width, 72};
  ApGetGraphics()->CreateBuffer(&fade_buffer);
  width = i32(u32(area.right) - u32(area.left));
  window_height = i32(u32(area.bottom) - u32(area.top));
  fade_buffer->Init(static_cast<u32>(width), static_cast<u32>(window_height), GxColorType(GxColorType::Color::Pal8),
      nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (fade_buffer->Lock(static_cast<GxLockFlags>(1))) {
    fade_buffer->Fill(32, static_cast<ScRect *>(nullptr));
    fade_buffer->Unlock(static_cast<GxLockFlags>(1));
  }
  if (fade_buffer->Lock(static_cast<GxLockFlags>(128))) {
    for (i32 y = 0, value = 0; value < 18432; ++y, value += 256) {
      for (i32 x = 0; x < i32(u32(area.right) - u32(area.left)); ++x) {
        fade_buffer->SetPixel(x, y, value / 576);
        i32 bottom = i32(u32(area.bottom) - u32(area.top) - u32(y) - 1u);
        fade_buffer->SetPixel(x, bottom, value / 576);
      }
    }
    fade_buffer->Unlock(static_cast<GxLockFlags>(128));
  }
  SetFlag(kWnFlagReorderable, false);
  SetFlag(kWnFlagDraggable, false);
  title_font = ApGetUiResources()->GetFont(kWnFontUiSerif);
  names_font = ApGetUiResources()->GetFont(kWnFontUiSerif);
  title_color = MakeFillColor(255, 222, 144);
  names_color = MakeFillColor(255, 222, 144);
  ApCreditsShadowColor = MakeFillColor(88, 73, 68);
  owns_font = false;
  if (!WnGeneralDialog::Init())
    return false;
  for (u32 index = 2; index < 189; ++index) {
    ScString title;
    {
      UiTextHandle text(index, kRsCreditsTextGroup);
      title = static_cast<ScString>(text);
    }
    if (title.Strlen() && title.begin[0] == '5')
      continue;
    if (!title.Strlen() || title.begin[0] < '1' || title.begin[0] > '5')
      title.Replace(0, 0, "4", 1);
    lines.push_back(title);
    ScString body;
    {
      UiTextHandle text(index + 199, kRsCreditsTextGroup);
      body = static_cast<ScString>(text);
    }
    if (!body.Strlen() || body.begin[body.Strlen() - 1] != '\n')
      body.Append("\n", 1);
    while (body.Strlen()) {
      u32 length = 0;
      while (length < body.Strlen() && body.begin[length] != '\n')
        ++length;
      ScString line(body.begin, length);
      if (!line.Strlen() || line.begin[0] < '1' || line.begin[0] > '5')
        line.Replace(0, 0, "3", 1);
      lines.push_back(line);
      body.Erase(0, length + 1);
    }
  }
  current = lines.begin();
  timer.Restart();
  start_time = 0;
  ApCompressedIni settings;
  ScString path, value;
  ApGetGameApp()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\SC3Tune.ini");
  settings.SetPath(path);
  ApGetGameApp()->GetAppDataDirectory(&path);
  path.Append("\\Sys\\Sys.pak");
  settings.SetCompressedPath(path);
  ScString section("CreditsTunables");
  if (settings.ReadEntry(section, ScString("ScrollRateInPixelsPerMinute"), &value)) {
    u32 integer;
    bool less;
    ScMultiplyUint32Float(ScConvertToUint32(value.begin), 0.000016666667f, 0.0001f, ApCreditsFloatingControl(),
        &integer, &scroll_rate, &less);
    if (less)
      scroll_rate = 0.0001f;
  }
  if (settings.ReadEntry(section, ScString("MinPixelsToScrollPerFrame"), &value)) {
    minimum_scroll = ScConvertToUint32(value.begin);
    if (minimum_scroll > 20)
      minimum_scroll = 20;
  }
  if (settings.ReadEntry(section, ScString("MaxPixelsToScrollPerFrame"), &value)) {
    maximum_scroll = ScConvertToUint32(value.begin);
    if (!maximum_scroll)
      maximum_scroll = 1;
  }
  GZSetCapture();
  ApGetGameApp()->GetBoxX()->Event(1046, 0, 0, 0, 0);
  return true;
}

static void ApCreditsScrollProduct(u32 elapsed, f32 rate, u32 *distance, f32 *rounded) {
  bool less;
  ScMultiplyUint32Float(elapsed, rate, 0.0001f, ApCreditsFloatingControl(), distance, rounded, &less);
}

// Scroll the credits text, draw the next resource line and compose the logo and fade masks onto the window.
bool ApCreditsWindow::GZPaint() {
  const ScRect *screen = ApGetDialogManager()->GetMainWindow()->GetArea();
  i32 screen_left = screen->left, screen_right = screen->right;
  u32 now = timer.GetElapsedTime(), elapsed = now - start_time;
  u32 distance;
  f32 rounded;
  ApCreditsScrollProduct(elapsed, scroll_rate, &distance, &rounded);
  bool preserve_fraction = true;
  if (distance < minimum_scroll) {
    preserve_fraction = false;
    distance = minimum_scroll;
  }
  if (distance > maximum_scroll) {
    preserve_fraction = false;
    distance = maximum_scroll;
  }
  i32 width, height;
  if (distance) {
    text_buffer->RemoveTransparency();
    if (text_buffer->Lock(static_cast<GxLockFlags>(16))) {
      width = i32(u32(area.right) - u32(area.left));
      height = i32(u32(area.bottom) - u32(area.top));
      ScRect source{0, static_cast<i32>(distance), width, height + 40};
      ScRect target{0, 0, width, static_cast<i32>(height - distance + 40)};
      text_buffer->Blt(text_buffer, source, target, nullptr);
      text_buffer->Unlock(static_cast<GxLockFlags>(16));
    }
    text_buffer->SetTransparency(0);
    if (text_buffer->Lock(static_cast<GxLockFlags>(1))) {
      width = i32(u32(area.right) - u32(area.left));
      height = i32(u32(area.bottom) - u32(area.top));
      ScRect cleared{0, static_cast<i32>(height - distance + 40), width, height + 40};
      text_buffer->Fill(0, cleared, nullptr);
      text_buffer->Unlock(static_cast<GxLockFlags>(1));
    }
    next_line_space = static_cast<i32>(static_cast<u32>(next_line_space) + distance);
    if (next_line_space > i32(u32(area.bottom) - u32(area.top) - 50u) && !ApCreditsEndNotified) {
      ApCreditsEndNotified = true;
      ApGetGameApp()->GetBoxX()->Event(1047, 0, 0, 0, 0);
    }
    if (next_line_space > i32(u32(area.bottom) - u32(area.top) + 50u)) {
      start_time = now + kApCreditsExitDelay;
      return ApGetDialogManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(this), 0);
    }
    bool has_line = current != lines.end();
    i32 required_height = has_line ? title_font->Height(0) : 0;
    if (has_line && next_line_space >= required_height) {
      auto line = current;
      if (text_buffer->Lock(static_cast<GxLockFlags>(16))) {
        ScString text = line->Mid(1, kScStringToEnd);
        u32 length = static_cast<u32>(text.end - text.begin), shadow = MakeFillColor(20, 20, 20);
        char kind = line->begin[0];
        if (kind == '1') {
          width = i32(u32(area.right) - u32(area.left));
          height = i32(u32(area.bottom) - u32(area.top));
          ScRect shifted{2, i32(u32(height) - u32(next_line_space) + 42u), i32(u32(width) + 2u),
              i32(u32(height) + 42u)};
          u32 paragraph_shadow = MakeFillColor(20, 20, 20);
          title_font->DrawTextPara(text_buffer, shifted, text.begin, length, paragraph_shadow, ApCreditsShadowColor,
              FtOutputStyle::Center);
          width = i32(u32(area.right) - u32(area.left));
          height = i32(u32(area.bottom) - u32(area.top));
          ScRect normal{0, i32(u32(height) - u32(next_line_space) + 40u), width, i32(u32(height) + 40u)};
          title_font->DrawTextPara(text_buffer, normal, text.begin, length, title_color, ApCreditsShadowColor,
              FtOutputStyle::Center);
          i32 consumed = title_font->Height(0);
          next_line_space = i32(u32(next_line_space) - u32(consumed));
        } else if (kind == '3') {
          i32 text_width = names_font->Measure(text.begin, length);
          width = i32(u32(area.right) - u32(area.left));
          height = i32(u32(area.bottom) - u32(area.top));
          names_font->DrawTextA(text_buffer, i32(u32(width) - u32(text_width) + 2u),
              i32(u32(height) - u32(next_line_space) + 42u), text.begin, length, shadow, ApCreditsShadowColor);
          width = i32(u32(area.right) - u32(area.left));
          height = i32(u32(area.bottom) - u32(area.top));
          names_font->DrawTextA(text_buffer, i32(u32(width) - u32(text_width)),
              i32(u32(height) - u32(next_line_space) + 40u), text.begin, length, names_color, ApCreditsShadowColor);
          i32 consumed = names_font->Height(0);
          next_line_space = i32(u32(next_line_space) - u32(consumed));
        } else {
          height = i32(u32(area.bottom) - u32(area.top));
          title_font->DrawTextA(text_buffer, 2, i32(u32(height) - u32(next_line_space) + 42u), text.begin, length,
              shadow, ApCreditsShadowColor);
          height = i32(u32(area.bottom) - u32(area.top));
          title_font->DrawTextA(text_buffer, 0, i32(u32(height) - u32(next_line_space) + 40u), text.begin, length,
              title_color, ApCreditsShadowColor);
          if (kind == '4') {
            ++current;
            if (current != lines.end()) {
              text = current->Mid(1, kScStringToEnd);
              length = static_cast<u32>(text.end - text.begin);
              i32 text_width = names_font->Measure(text.begin, length);
              width = i32(u32(area.right) - u32(area.left));
              height = i32(u32(area.bottom) - u32(area.top));
              names_font->DrawTextA(text_buffer, i32(u32(width) - u32(text_width) + 2u),
                  i32(u32(height) - u32(next_line_space) + 42u), text.begin, length, shadow, ApCreditsShadowColor);
              width = i32(u32(area.right) - u32(area.left));
              height = i32(u32(area.bottom) - u32(area.top));
              names_font->DrawTextA(text_buffer, i32(u32(width) - u32(text_width)),
                  i32(u32(height) - u32(next_line_space) + 40u), text.begin, length, names_color, ApCreditsShadowColor);
            }
            i32 names_height = names_font->Height(0), title_height = title_font->Height(0);
            i32 consumed = title_height < names_height ? names_height : title_height;
            next_line_space = static_cast<i32>(static_cast<u32>(next_line_space) - static_cast<u32>(consumed));
          } else {
            i32 consumed = title_font->Height(0);
            next_line_space = i32(u32(next_line_space) - u32(consumed));
          }
        }
        text_buffer->Unlock(static_cast<GxLockFlags>(16));
      }
      if (current == lines.end())
        current = lines.begin();
      else
        ++current;
    }
    start_time = now;
    if (preserve_fraction) {
      i64 remainder = ScFloatDifferenceQuotient(rounded, distance, scroll_rate, ApCreditsFloatingControl());
      start_time = now - static_cast<u32>(remainder);
    }
  }
  if (draw_buffer->Lock(static_cast<GxLockFlags>(16))) {
    width = i32(u32(area.right) - u32(area.left));
    height = i32(u32(area.bottom) - u32(area.top));
    ScRect source{0, 72, width, i32(u32(height) - 72u)};
    ScRect destination{area.left, i32(u32(area.top) + 72u), area.right, i32(u32(area.bottom) - 72u)};
    u32 screen_width = static_cast<u32>(screen_right) - static_cast<u32>(screen_left);
    u32 left = (screen_width - logo_buffer->Width()) >> 1;
    GxBuffer *measured_logo = logo_buffer;
    u32 measured_width = measured_logo->Width();
    u32 right = (screen_width - measured_width) >> 1;
    right += measured_logo->Width();
    ScRect logo_area{static_cast<i32>(left), 20, static_cast<i32>(right),
        static_cast<i32>(logo_buffer->Height() + 20)};
    const ScRect *logo_source = logo_buffer->GetBufferArea();
    draw_buffer->Blt(logo_buffer, *logo_source, logo_area, nullptr);
    draw_buffer->Blt(text_buffer, source, destination, nullptr);
    draw_buffer->Unlock(static_cast<GxLockFlags>(16));
  }
  width = i32(u32(area.right) - u32(area.left));
  height = i32(u32(area.bottom) - u32(area.top));
  ScRect bottom{0, i32(u32(height) - 72u), width, height};
  ScPoint bottom_at{area.left, i32(u32(area.bottom) - 72u)};
  ApServices::AB()->AlphaBlt(text_buffer, draw_buffer, fade_buffer, bottom, bottom_at, nullptr);
  width = i32(u32(area.right) - u32(area.left));
  ScRect top{0, 0, width, 72};
  GxBlendService *alpha = ApServices::AB();
  ScPoint top_at{area.left, area.top};
  alpha->AlphaBlt(text_buffer, draw_buffer, fade_buffer, top, top_at, nullptr);
  return true;
}

// Release the three retained buffers and capture before shutting down an initialized credits dialog.
bool ApCreditsWindow::Shutdown() {
  if (!GetInitFlag())
    return true;
  fade_buffer->Release();
  text_buffer->Release();
  logo_buffer->Release();
  fade_buffer = nullptr;
  text_buffer = nullptr;
  logo_buffer = nullptr;
  title_font = nullptr;
  names_font = nullptr;
  GZReleaseCapture();
  return WnGeneralDialog::Shutdown();
}

bool ApCreditsWindow::OnMouseDownL(i32, i32, u32) {
  ApGetGameApp()->GetBoxX()->Event(1048, 0, 0, 0, 0);
  return ApGetDialogManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(this), 0);
}

bool ApCreditsWindow::OnMouseUpL(i32, i32, u32) {
  return true;
}

WnGeneralDialog::WnGeneralDialog(WnDialogStyle value)
    : font(nullptr), title_font(nullptr), owns_font(false), background(nullptr), owns_background(false),
      border_widths{}, command_callback(nullptr), callback_context(nullptr), style(value) {
}

WnGeneralDialog::~WnGeneralDialog() {
  WnGeneralDialog::Shutdown();
}

// Return the original dialog identity for its two accepted IDs and delegate all other requests to the root window.
bool WnGeneralDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnGeneralDialogView, WnGeneralDialogView);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, WnGeneralDialogView);
  return UiWindow::QueryInterface(iid, out);
}

u32 WnGeneralDialog::AddRef() {
  return UiWindow::AddRef();
}

u32 WnGeneralDialog::Release() {
  return UiWindow::Release();
}

ScRect WnGeneralDialog::GetBorderWidths() {
  return border_widths;
}

WnDialogStyle WnGeneralDialog::GetStyle() {
  return style;
}

void WnGeneralDialog::SetCommandCallback(WnDialogCallback callback, WnDialogCallbackContext *context) {
  command_callback = callback;
  callback_context = context;
}

// Disable the dialog and its private buffer, release its retained resources, and shut down the root window.
bool WnGeneralDialog::Shutdown() {
  SetFlag(kWnFlagVisible, false);
  if (style == WnDialogStyle::Edge)
    PrivateBuffer(false);
  if (background) {
    background->Release();
    background = nullptr;
  }
  if (font) {
    font->Release();
    font = nullptr;
  }
  if (title_font) {
    title_font->Release();
    title_font = nullptr;
  }
  return UiWindow::Shutdown();
}

// Paint the selected background style, then draw a centered caption in the edge-style title strip when present.
bool WnGeneralDialog::GZPaint() {
  if (style == WnDialogStyle::Edge) {
    if (draw_buffer->Lock(GxLockFlags::Blt)) {
      ScRect slices{0, 0, 24, 24};
      draw_buffer->BltEdge(background, slices, draw_area, 1);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  } else if (style == WnDialogStyle::Plain) {
    if (draw_buffer->Lock(GxLockFlags::Blt)) {
      draw_buffer->Blt(background, *background->GetBufferArea(), draw_area, static_cast<const ScRect *>(nullptr));
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  } else if (style == WnDialogStyle::None && draw_buffer->Lock(GxLockFlags::Fill)) {
    draw_buffer->Fill(GetFillColor(), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (style != WnDialogStyle::Edge || !GetCaption()->Strlen())
    return true;
  i32 width = GetW(), height = title_font->Height(0);
  ScRect title{0, (23 - height) / 2, width, 23};
  const ScStringView *length_source = GetCaption();
  const ScStringView *text_source = GetCaption();
  u32 length = length_source->Strlen();
  return title_font->DrawTextPara(draw_buffer, title, text_source->Data(), length, FtOutputStyle::Center) != 0;
}

// Record the press position and begin capturing a draggable dialog's mouse input.
bool WnGeneralDialog::OnMouseDownL(i32 x, i32 y, u32) {
  SetMouseDownOffset(x, y);
  if (GetFlag(kWnFlagDraggable))
    SetFlag(kWnFlagDragging, true);
  GZSetCapture();
  return true;
}

// Clear the active drag flag and release mouse capture when the left button is released.
bool WnGeneralDialog::OnMouseUpL(i32, i32, u32) {
  if (GetFlag(kWnFlagDragging))
    SetFlag(kWnFlagDragging, false);
  GZReleaseCapture();
  return true;
}

bool WnGeneralDialog::SetBackground(GxBuffer *buffer) {
  if (background) {
    background->Release();
    background = nullptr;
  }
  background = buffer;
  buffer->AddRef();
  return true;
}

// Construct the executable list, route its notifications to itself, then attach it with the dialog font and fill color.
WnWindow *WnGeneralDialog::AddListBox(const ScRect &rectangle) {
  WnAppList *list = new WnAppList(false);
  list->SetNotificationTarget(list);
  list->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  list->SetFont(font);
  list->SetFillColor(ApGetUiResources()->GetColor(kWnColorUiBackground));
  ChildAdd(list);
  return list;
}

// Construct an editable or noneditable combo and attach it with the dialog font and standard fill color.
WnWindow *WnGeneralDialog::AddCombo(const ScRect &rectangle, bool editable) {
  WnAppCombo *combo = new WnAppCombo(editable);
  combo->SetNotificationTarget(combo);
  combo->SetFont(font);
  combo->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  combo->SetFillColor(ApGetUiResources()->GetColor(kWnColorUiBackground));
  ChildAdd(combo);
  return combo;
}

// Create a bitmap control, assign its interface as its window ID, and attach it to the dialog.
WnWindow *WnGeneralDialog::AddBMP(const ScRect &rectangle, GxBuffer *buffer) {
  WnBitmapControl *control = ApGetUiResources()->CreateWinBMP(0, buffer, false);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetID((u32)(uptr)control);
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Create and attach a captioned button, destroying its window if attachment fails.
WnWindow *WnGeneralDialog::AddBtn(const ScRect &rectangle, const ScStringView &label) {
  WnButton *control = ApGetUiResources()->CreateButton(0, label, nullptr, nullptr, 0);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetID((u32)(uptr)control);
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Create an editable line input using the dialog font and the standard input background color.
WnWindow *WnGeneralDialog::AddLineInput(const ScRect &rectangle) {
  WnTextControl *control = ApGetUiResources()->CreateTextEdit(0, 1);
  if (!control)
    return nullptr;
  WnWindow *window = control->AsIGZWin();
  control->SetFont(font);
  window->SetFillColor(ApGetUiResources()->GetColor(kWnColorUiBackground));
  window->SetID((u32)(uptr)control);
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Create the multiline editor with the dialog's font and input color, then attach its initial caption and text.
WnWindow *WnGeneralDialog::AddTextEdit(const ScRect &rectangle, const ScStringView &text) {
  WnAppEditor *editor = new WnAppEditor(511);
  ScString copy(text);
  editor->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  editor->SetNotificationTarget(editor);
  editor->SetCaption(copy);
  editor->SetFillColor(ApGetUiResources()->GetColor(kWnColorUiBackground));
  editor->SetFont(font);
  if (copy.begin != copy.end)
    editor->SetText(copy.begin, static_cast<u32>(copy.end - copy.begin));
  ChildAdd(editor);
  return editor;
}

// Create a slider with the requested range and orientation, using its window as the child ID.
WnWindow *WnGeneralDialog::AddSlider(const ScRect &rectangle, i32 first, i32 second, i32 third, i32 fourth) {
  WnSliderControl *control = ApGetUiResources()->CreateSlider(0, fourth, first, second, third);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  window->SetID((u32)(uptr)window);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Create a scrollbar with the requested range and orientation and attach its window.
WnWindow *WnGeneralDialog::Scrollbar(const ScRect &rectangle, i32 first, i32 second, i32 third, i32 fourth) {
  WnScrollbarControl *control = ApGetUiResources()->CreateScrollbar(0, fourth, first, second, third);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  window->SetID((u32)(uptr)control);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Create a numeric spinner with unit steps and add its window to the dialog.
WnWindow *WnGeneralDialog::AddSpinner(const ScRect &rectangle, i32 first, i32 second, i32 third) {
  WnSpinnerControl *control = ApGetUiResources()->CreateSpinner(0, 2, first, second, 1, third);
  WnWindow *window = control->GetWindow();
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  window->SetID((u32)(uptr)control);
  ChildAdd(window);
  return window;
}

// Create a formatted label, set its drawing attributes, and bring the new child to the front.
WnWindow *WnGeneralDialog::AddText(const ScRect &rectangle, const ScStringView &text, u32 alignment, bool flag) {
  WnLabelControl *control = ApGetUiResources()->CreateLabel(0, text);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  control->SetFont(font);
  control->SetAlignment(alignment);
  control->SetLabelFlag(kWnLabelForwardMessages, true);
  control->SetBackground(!flag);
  window->SetFillColor(ApGetUiResources()->GetColor(kWnColorUiBackground));
  window->SetID((u32)(uptr)control);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  window->SetFlag(kWnFlagReorderable, true);
  ChildToFront(window);
  window->SetFlag(kWnFlagReorderable, false);
  return window;
}

// Create and attach a checkbox with the supplied label.
WnWindow *WnGeneralDialog::AddCheck(const ScRect &rectangle, const ScStringView &text) {
  WnButton *control = ApGetUiResources()->CreateCheckBox(0, text, nullptr, nullptr, 0);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetID((u32)(uptr)control);
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Create an option group for the requested rectangle and attach its window to the dialog.
WnWindow *WnGeneralDialog::AddOptionGroup(const ScRect &rectangle, const ScStringView &text) {
  WnOptionControl *control = ApGetUiResources()->CreateOptGrp(0, text, &rectangle);
  if (!control)
    return nullptr;
  WnWindow *window = control->GetWindow();
  window->SetID((u32)(uptr)control);
  window->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  if (!ChildAdd(window)) {
    GetWindowManager()->DestroyWindow(window);
    return nullptr;
  }
  return window;
}

// Load the edge background and fonts once, then initialize the root window with the original border widths.
bool WnGeneralDialog::Init() {
  if (GetInitFlag())
    return true;
  if (!ApGetUiResources())
    return false;
  if (style == WnDialogStyle::Edge) {
    if (!background) {
      background = ApGetUiResources()->GetImage(8);
      if (background)
        background->AddRef();
      PrivateBuffer(true);
    }
    border_widths = {6, 23, 6, 8};
  }
  if (!background && style != WnDialogStyle::None)
    return false;
  if (!font) {
    font = ApGetUiResources()->GetFont(kWnFontDefault);
    assert(font);
    font->AddRef();
  }
  title_font = ApGetUiResources()->GetFont(kWnFontCaption);
  if (!title_font)
    return false;
  title_font->AddRef();
  return UiWindow::Init();
}

bool WnGeneralDialog::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  if (GetFlag(kWnFlagDraggable)) {
    if (GetFlag(kWnFlagDragging))
      GZWinMoveTo(x - mouse_down_x, y - mouse_down_y);
    return true;
  }
  WindowToWindowCoordinates(GetParentWin(), &x, &y);
  return SendMsg(GetParentWin(), kWnMsg_MouseMove, x, y, modifiers);
}

// Dispatch child commands to the installed callback or post a close message for a child without an ID.
bool WnGeneralDialog::OnCommand(u32 kind, u32 command) {
  ScString text;
  if (kind == kWnCommandButtonActivatedByWindow) {
    WnWindow *child = reinterpret_cast<WnWindow *>((uptr)command);
    if (ChildExists(child)) {
      if (child->GetID())
        command_callback(16, command, callback_context);
      else
        GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, (uptr)this, 0);
    }
  } else if (command_callback) {
    command_callback(kind, command, callback_context);
  }
  return false;
}

// Construct a line graph, route its notifications to itself and add it as a dialog child.
WnWindow *WnGeneralDialog::AddLineGraph(const ScRect &rectangle) {
  WnLineGraph *graph = new WnLineGraph;
  graph->SetNotificationTarget(graph);
  graph->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  ChildAdd(graph);
  return graph;
}

// Construct a bar graph, route its notifications to itself and add it as a dialog child.
WnWindow *WnGeneralDialog::AddBarGraph(const ScRect &rectangle) {
  WnBarGraph *graph = new WnBarGraph;
  graph->SetNotificationTarget(graph);
  graph->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  ChildAdd(graph);
  return graph;
}

// Construct a scatter graph, route its notifications to itself and add it as a dialog child.
WnWindow *WnGeneralDialog::AddScatterGraph(const ScRect &rectangle) {
  WnScatterGraph *graph = new WnScatterGraph;
  graph->SetNotificationTarget(graph);
  graph->SetArea(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
  ChildAdd(graph);
  return graph;
}

// its masked invalid result has a zero low word for nonfinite or out-of-range inputs.
static i32 WnGraphTruncate(f64 value) {
  if (!std::isfinite(value) || value < -9223372036854775808.0 || value >= 9223372036854775808.0)
    return 0;
  return static_cast<i32>(static_cast<i64>(value));
}

WnGraph::WnGraph()
    : unknown_168(1), graph_area{INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}, unknown_188(false),
      title_position{INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}, title_font(nullptr),
      title_color(kUiGraphUseFontColor), unknown_236(32), unknown_240(1), background_color(kUiGraphDefaultFillColor),
      background_outline(true), background_outline_color(0), graph_fill(false),
      graph_fill_color(kUiGraphInitialFillColor), graph_outline(true), graph_outline_color(0),
      background_pattern(nullptr), graph_background_pattern(nullptr), draw_axes(false), major_tick_usage{true, true},
      minor_tick_usage{false, false}, major_tick_grid{true, true}, minor_tick_grid{false, false},
      tick_text_drawing{false, true}, axis_line_color(0), axis_min{FLT_MIN, FLT_MIN}, axis_max{FLT_MIN, FLT_MIN},
      major_tick_spacing{FLT_MIN, FLT_MIN}, minor_tick_spacing{FLT_MIN, FLT_MIN}, major_tick_decimals{-127, -127},
      major_tick_length{4, 4}, minor_tick_length{2, 2}, tick_color(0), unknown_352{1, 2},
      axis_zero_position{INT32_MAX, INT32_MAX},
      axis_label_positions{{INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX}},
      unknown_440(1), unknown_444(0), axis_label_fonts{nullptr, nullptr},
      axis_label_colors{kUiGraphUseFontColor, kUiGraphUseFontColor}, axis_tick_fonts{nullptr, nullptr},
      axis_tick_colors{kUiGraphUseFontColor, kUiGraphUseFontColor},
      axis_label_rotations{static_cast<WnGraphRotation>(0), static_cast<WnGraphRotation>(0)}, transparent_color(0),
      hit_data(nullptr), hit_data_size(0) {
  transparent_color = ApGetGraphics()->ConvertRGBValueToNative16Bit(255, 0, 255);
}

WnGraph::~WnGraph() {
  WnGraph::Shutdown();
}

bool WnGraph::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnGraphView, WnGraphView);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnGraph::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnGraph::Release() {
  return WnRootWidget::Release();
}

// Reuse an initialized root or enable the graph window flag before initializing it.
bool WnGraph::Init() {
  if (GetInitFlag())
    return true;
  SetFlag(kWnFlagPrivateBuffer, true);
  return WnRootWidget::Init();
}

// Delete dynamic objects and release graph-owned fonts, patterns, and hit data before root shutdown.
bool WnGraph::Shutdown() {
  for (WnGraphObject *object : objects)
    delete object;
  objects.clear();
  if (title_font) {
    title_font->Release();
    title_font = nullptr;
  }
  if (background_pattern) {
    background_pattern->Release();
    background_pattern = nullptr;
  }
  if (graph_background_pattern) {
    graph_background_pattern->Release();
    graph_background_pattern = nullptr;
  }
  for (u32 axis = 0; axis < 2; ++axis) {
    if (axis_label_fonts[axis]) {
      axis_label_fonts[axis]->Release();
      axis_label_fonts[axis] = nullptr;
    }
    if (axis_tick_fonts[axis]) {
      axis_tick_fonts[axis]->Release();
      axis_tick_fonts[axis] = nullptr;
    }
  }
  delete[] hit_data;
  hit_data = nullptr;
  unknown_188 = false;
  return WnRootWidget::Shutdown();
}

// Acquire missing title, axis-label, and tick fonts through the original virtual setters.
void WnGraph::LoadDefaultFonts() {
  if (!title_font)
    SetTitleFont(32, 0, 1, kUiGraphUseFontColor);
  for (i32 axis = 0; axis < 2; ++axis) {
    WnGraphAxis selected = static_cast<WnGraphAxis>(axis);
    if (!axis_label_fonts[axis])
      SetAxisLabelFont(selected, 1, 16, 0, kUiGraphUseFontColor);
    if (!axis_tick_fonts[axis])
      SetAxisTickFont(selected, 1, 14, 0, kUiGraphUseFontColor);
  }
  unknown_188 = true;
}

bool WnGraph::GZPaint() {
  CompleteRedraw(nullptr);
  return true;
}

bool WnGraph::OnMouseDownL(i32, i32, u32) {
  GZSetCapture();
  return true;
}

bool WnGraph::OnMouseUpL(i32, i32, u32) {
  if (GZAmCapturing())
    GZReleaseCapture();
  return true;
}

// Check the current mouse position while captured and consume the movement message.
bool WnGraph::OnMouseMove(i32 x, i32 y, u32) {
  if (GZAmCapturing())
    IsPointInWindow(x, y);
  return true;
}

void WnGraph::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  WnRootWidget::SetArea(left, top, right, bottom);
  CompleteRedraw(draw_buffer);
}

// Resolve the drawing target, initialize defaults, and invoke graph drawing phases in their original order.
void WnGraph::CompleteRedraw(GxBuffer *target) {
  if (!target)
    target = draw_buffer;
  if (!target)
    return;
  if (!unknown_188)
    LoadDefaultFonts();
  CommitDefaultValues();
  for (i32 axis = 0; axis < 2; ++axis)
    CalculateAxisZeroPosition(static_cast<WnGraphAxis>(axis));
  DrawBackground(target);
  DrawGraphAreaBackground(target);
  DrawAxes(target);
  DrawTicks(target, static_cast<WnGraphAxis>(7));
  DrawTickText(target, static_cast<WnGraphAxis>(7));
  DrawGraphData(target);
  DrawDynamicObjects(target);
  DrawTitles(target);
}

void WnGraph::SetBackgroundOutline(bool enabled) {
  background_outline = enabled;
}

void WnGraph::SetBackgroundFillColor(u32 color) {
  background_color = color;
}

void WnGraph::SetBackgroundOutlineColor(u32 color) {
  background_outline_color = color;
}

// Retain the new background pattern before releasing the previously retained pattern.
void WnGraph::SetBackgroundPattern(GxBuffer *pattern) {
  if (pattern)
    pattern->AddRef();
  if (background_pattern)
    background_pattern->Release();
  background_pattern = pattern;
}

void WnGraph::SetGraphAreaOutlineAndFill(bool outline, bool fill) {
  graph_outline = outline;
  graph_fill = fill;
}

void WnGraph::SetGraphAreaOutlineColor(u32 color) {
  graph_outline_color = color;
}

void WnGraph::SetGraphAreaFillColor(u32 color) {
  graph_fill_color = color;
}

// Retain the new graph-area pattern before releasing the previously retained pattern.
void WnGraph::SetGraphBackgroundPattern(GxBuffer *pattern) {
  if (pattern)
    pattern->AddRef();
  if (graph_background_pattern)
    graph_background_pattern->Release();
  graph_background_pattern = pattern;
}

void WnGraph::SetGraphArea(const ScRect &rectangle) {
  graph_area = rectangle;
}

void WnGraph::GetGraphArea(ScRect *rectangle) {
  *rectangle = graph_area;
}

void WnGraph::SetAxisMinValue(WnGraphAxis axis, f32 value) {
  axis_min[static_cast<i32>(axis)] = value;
}

void WnGraph::SetAxisMaxValue(WnGraphAxis axis, f32 value) {
  axis_max[static_cast<i32>(axis)] = value;
}

void WnGraph::SetAxisZeroPosition(WnGraphAxis axis, i32 position) {
  axis_zero_position[static_cast<i32>(axis)] = position;
}

void WnGraph::SetAxisLineColor(u32 color) {
  axis_line_color = color;
}

void WnGraph::SetMajorTickUsage(WnGraphAxis axis, WnGraphUsage usage) {
  major_tick_usage[static_cast<i32>(axis)] = static_cast<i32>(usage) != 0;
}

void WnGraph::SetMajorTickSpacing(WnGraphAxis axis, f32 spacing) {
  major_tick_spacing[static_cast<i32>(axis)] = spacing;
}

// Derive major spacing from the axis range, optionally imposing the original four-pixel minimum.
void WnGraph::SetMajorTickDivisions(WnGraphAxis axis, u32 divisions, bool automatic) {
  i32 index = static_cast<i32>(axis);
  f64 spacing = (static_cast<f64>(axis_max[index]) - axis_min[index]) / static_cast<i32>(divisions);
  if (automatic) {
    i32 pixels = ValueAmountToPixelAmount(axis, static_cast<f32>(spacing));
    if (pixels < 4)
      pixels = 4;
    spacing = PixelAmountToValueAmount(axis, pixels);
  }
  major_tick_spacing[index] = static_cast<f32>(spacing);
}

void WnGraph::SetMajorTickGridUsage(WnGraphAxis axis, WnGraphUsage usage) {
  major_tick_grid[static_cast<i32>(axis)] = static_cast<i32>(usage) != 0;
}

// Clamp the selected axis's major tick decimal count to the original range from zero through nine.
void WnGraph::SetMajorTickDecimalPlaces(WnGraphAxis axis, i32 places) {
  if (places < 0)
    places = 0;
  else if (places > 9)
    places = 9;
  major_tick_decimals[static_cast<i32>(axis)] = static_cast<i8>(places);
}

void WnGraph::SetMajorTickLength(WnGraphAxis axis, i32 length) {
  major_tick_length[static_cast<i32>(axis)] = length;
}

void WnGraph::SetAxisTickTextDrawing(WnGraphAxis axis, WnGraphUsage usage) {
  tick_text_drawing[static_cast<i32>(axis)] = static_cast<i32>(usage) != 0;
}

void WnGraph::SetTickColor(u32 color) {
  tick_color = color;
}

void WnGraph::SetMinorTickUsage(WnGraphAxis axis, WnGraphUsage usage) {
  minor_tick_usage[static_cast<i32>(axis)] = static_cast<i32>(usage) == 1;
}

void WnGraph::SetMinorTickSpacing(WnGraphAxis axis, f32 spacing) {
  minor_tick_spacing[static_cast<i32>(axis)] = spacing;
}

// Derive minor spacing from the major interval and apply the executable's optional two-pixel limit.
void WnGraph::SetMinorTickDivisions(WnGraphAxis axis, u32 divisions, bool automatic) {
  i32 index = static_cast<i32>(axis);
  f32 spacing = static_cast<f32>(static_cast<f64>(major_tick_spacing[index]) / static_cast<f64>(divisions));
  ValueAmountToPixelAmount(axis, minor_tick_spacing[index]);
  if (automatic && ValueAmountToPixelAmount(axis, spacing) < 2) {
    spacing = PixelAmountToValueAmount(axis, 2);
    if (static_cast<f64>(major_tick_spacing[index]) * 0.5 < spacing)
      minor_tick_usage[index] = false;
  }
  minor_tick_spacing[index] = spacing;
}

void WnGraph::SetMinorTickGridUsage(WnGraphAxis axis, WnGraphUsage usage) {
  minor_tick_grid[static_cast<i32>(axis)] = static_cast<i32>(usage) == 1;
}

void WnGraph::SetMinorTickLength(WnGraphAxis axis, i32 length) {
  minor_tick_length[static_cast<i32>(axis)] = length;
}

// Copy an axis label and optionally request automatic positioning by resetting its left edge.
void WnGraph::SetAxisLabel(WnGraphAxis axis, const ScString &label, bool automatic) {
  i32 index = static_cast<i32>(axis);
  axis_labels[index] = label;
  if (automatic)
    axis_label_positions[index].left = INT32_MAX;
}

void WnGraph::SetAxisLabelPosition(WnGraphAxis axis, ScRect *rectangle) {
  axis_label_positions[static_cast<i32>(axis)] = *rectangle;
}

void WnGraph::SetAxisLabelFont(WnGraphAxis axis, FtFont *font, u32 color) {
  i32 index = static_cast<i32>(axis);
  if (font)
    font->AddRef();
  if (axis_label_fonts[index])
    axis_label_fonts[index]->Release();
  axis_label_fonts[index] = font;
  axis_label_colors[index] = color;
}

void WnGraph::SetAxisLabelRotation(WnGraphAxis axis, WnGraphRotation rotation) {
  axis_label_rotations[static_cast<i32>(axis)] = rotation;
}

// Retain an axis tick-label font before releasing the old one and record its color.
void WnGraph::SetAxisTickFont(WnGraphAxis axis, FtFont *font, u32 color) {
  i32 index = static_cast<i32>(axis);
  if (font)
    font->AddRef();
  if (axis_tick_fonts[index])
    axis_tick_fonts[index]->Release();
  axis_tick_fonts[index] = font;
  axis_tick_colors[index] = color;
}

void WnGraph::SetTitle(const ScString &value, bool automatic) {
  title = value;
  if (automatic)
    title_position.left = INT32_MAX;
}

void WnGraph::SetTitlePosition(ScRect *rectangle) {
  title_position = *rectangle;
}

// Retain the replacement title font before releasing the current font and recording its color.
void WnGraph::SetTitleFont(FtFont *font, u32 color) {
  if (font)
    font->AddRef();
  if (title_font)
    title_font->Release();
  title_font = font;
  title_color = color;
}

bool WnGraph::DoHitTest(ScPoint *, u32 *) {
  return false;
}

// Replace the hit-test allocation and update its requested size using the executable's original release condition.
bool WnGraph::AllocateHitTestData(u32 size) {
  if (size > hit_data_size)
    delete[] hit_data;
  hit_data_size = size;
  hit_data = new u8[size];
  return true;
}

// Borrow the current hit-test allocation without transferring ownership.
void *WnGraph::GetHitTestResultData() {
  return hit_data;
}

void WnGraph::AddDynamicGraphObject(WnGraphObject *object) {
  objects.push_back(object);
}

void WnGraph::RemoveDynamicGraphObject(WnGraphObject *object) {
  objects.remove(object);
}

// Construct a dynamic line, register it with the graph, and return the graph-owned object.
WnGraphLine *WnGraph::AddMiscGraphLine() {
  WnGraphLine *line = new WnGraphLine;
  AddDynamicGraphObject(line);
  return line;
}

// Construct a dynamic rectangle, register it with the graph, and return the graph-owned object.
WnGraphRectangle *WnGraph::AddMiscGraphRect() {
  WnGraphRectangle *rectangle = new WnGraphRectangle;
  AddDynamicGraphObject(rectangle);
  return rectangle;
}

// Construct a dynamic arrow, register it with the graph, and return the graph-owned object.
WnGraphArrow *WnGraph::AddMiscGraphArrow() {
  WnGraphArrow *arrow = new WnGraphArrow;
  AddDynamicGraphObject(arrow);
  return arrow;
}

// Construct a picture and register it through the graph's dynamic-object interface before returning it.
WnGraphPicture *WnGraph::AddMiscGraphPicture() {
  WnGraphPicture *picture = new WnGraphPicture;
  AddDynamicGraphObject(picture);
  return picture;
}

// Convert an axis value relative to its zero position and optionally clamp it to the graph rectangle.
// The axis must select one coordinate rather than a tick-axis bit mask.
i32 WnGraph::ValueToPixel(WnGraphAxis axis, f32 value, bool clamp) {
  i32 index = static_cast<i32>(axis);
  assert(index == 0 || index == 1);
  i32 low = index == 0 ? graph_area.left : graph_area.top;
  i32 high = index == 0 ? graph_area.right : graph_area.bottom;
  i32 extent = static_cast<i32>(static_cast<u32>(high) - static_cast<u32>(low));
  f64 scaled = value / ((static_cast<f64>(axis_max[index]) - axis_min[index]) / extent);
  u32 amount = static_cast<u32>(WnGraphTruncate(scaled));
  u32 zero = static_cast<u32>(axis_zero_position[index]);
  i32 pixel = static_cast<i32>(index == 0 ? zero + amount : zero - amount);
  if (clamp) {
    if (pixel < low)
      pixel = low;
    if (pixel > high)
      pixel = high;
  }
  return pixel;
}

// Convert a pixel from the chosen axis to a graph value, applying the original optional range clamp.
// The original conversion requires one of the two coordinate axes.
f32 WnGraph::PixelToValue(WnGraphAxis axis, i32 pixel, bool clamp) {
  i32 index = static_cast<i32>(axis);
  assert(index == 0 || index == 1);
  i32 low = index == 0 ? graph_area.left : graph_area.top;
  i32 high = index == 0 ? graph_area.right : graph_area.bottom;
  i32 extent = static_cast<i32>(static_cast<u32>(high) - static_cast<u32>(low));
  i32 distance = index == 0 ? pixel - axis_zero_position[index] : axis_zero_position[index] - pixel;
  f64 value = (static_cast<f64>(axis_max[index]) - axis_min[index]) / extent * distance;
  if (clamp) {
    if (value < axis_min[index])
      value = axis_min[index];
    if (value > axis_max[index])
      value = axis_max[index];
  }
  return static_cast<f32>(value);
}

// Convert a value interval into a pixel interval using the current graph extent and axis range.
i32 WnGraph::ValueAmountToPixelAmount(WnGraphAxis axis, f32 amount) {
  i32 index = static_cast<i32>(axis);
  f64 scale;
  if (index == 0 || index == 1) {
    i32 low = index == 0 ? graph_area.left : graph_area.top;
    i32 high = index == 0 ? graph_area.right : graph_area.bottom;
    i32 extent = static_cast<i32>(static_cast<u32>(high) - static_cast<u32>(low));
    scale = (static_cast<f64>(axis_max[index]) - axis_min[index]) / extent;
  } else {
    f32 bits;
    std::memcpy(&bits, &index, sizeof(bits));
    scale = bits;
  }
  f64 value = amount / scale;
  return WnGraphTruncate(value);
}

// Convert a pixel interval back to an axis-value interval with the executable's signed pixel arithmetic.
f32 WnGraph::PixelAmountToValueAmount(WnGraphAxis axis, i32 amount) {
  i32 index = static_cast<i32>(axis);
  f64 scale;
  if (index == 0 || index == 1) {
    i32 low = index == 0 ? graph_area.left : graph_area.top;
    i32 high = index == 0 ? graph_area.right : graph_area.bottom;
    i32 extent = static_cast<i32>(static_cast<u32>(high) - static_cast<u32>(low));
    scale = (static_cast<f64>(axis_max[index]) - axis_min[index]) / extent;
  } else {
    f32 bits;
    std::memcpy(&bits, &index, sizeof(bits));
    scale = bits;
  }
  return static_cast<f32>(scale * amount);
}

// Format and draw enabled major tick labels for each requested axis using its font, alignment and color.
void WnGraph::DrawTickText(GxBuffer *target, WnGraphAxis axes) {
  ScString text;
  char format[] = "%._f";
  i32 mask = static_cast<i32>(axes);
  if ((mask & 1) && tick_text_drawing[0] && axis_tick_fonts[0]) {
    FtFont *font = axis_tick_fonts[0];
    i32 height = font->Height(0);
    ScRect box{};
    box.top = graph_area.bottom + major_tick_length[0] + 2;
    box.bottom = box.top + height + 2;
    for (f32 value = axis_min[0]; static_cast<f64>(major_tick_spacing[0]) * 0.25 + axis_max[0] >= value;
        value += major_tick_spacing[0]) {
      format[2] = static_cast<char>('0' + major_tick_decimals[0]);
      text.Sprintf(format, static_cast<f64>(value));
      i32 width = font->Measure(text.Data(), text.Strlen());
      i32 pixel = ValueToPixel(static_cast<WnGraphAxis>(0), value, false), alignment = unknown_352[0];
      if (alignment == 0) {
        box.left = pixel;
        box.right = pixel + width + 2;
      } else if (alignment == 1) {
        box.left = pixel - (width / 2) - 1;
        box.right = pixel + (width / 2) + 1;
      } else {
        box.left = pixel - width - 2;
        box.right = pixel;
      }
      if (axis_tick_colors[0] == kUiGraphUseFontColor)
        font->DrawTextPara(target, box, text.Data(), text.Strlen(), static_cast<FtOutputStyle>(alignment));
      else
        font->DrawTextPara(
            target, box, text.Data(), text.Strlen(), axis_tick_colors[0], static_cast<FtOutputStyle>(alignment));
    }
  }
  if ((mask & 2) && tick_text_drawing[1] && axis_tick_fonts[1]) {
    FtFont *font = axis_tick_fonts[1];
    i32 half_height = font->Height(0) / 2;
    ScRect box{};
    box.right = graph_area.left - major_tick_length[1] - 2;
    for (f32 value = axis_min[1]; static_cast<f64>(major_tick_spacing[1]) * 0.25 + axis_max[1] >= value;
        value += major_tick_spacing[1]) {
      format[2] = static_cast<char>('0' + major_tick_decimals[1]);
      text.Sprintf(format, static_cast<f64>(value));
      i32 width = font->Measure(text.Data(), text.Strlen());
      box.left = box.right - width;
      i32 pixel = ValueToPixel(static_cast<WnGraphAxis>(1), value, false);
      box.top = pixel - half_height;
      box.bottom = pixel + half_height + 2;
      auto alignment = static_cast<FtOutputStyle>(unknown_352[1]);
      if (axis_tick_colors[1] == kUiGraphUseFontColor)
        font->DrawTextPara(target, box, text.Data(), text.Strlen(), alignment);
      else
        font->DrawTextPara(target, box, text.Data(), text.Strlen(), axis_tick_colors[1], alignment);
    }
  }
}

// Draw axis labels and title, using a temporary keyed buffer for a rotated second-axis label.
void WnGraph::DrawTitles(GxBuffer *target) {
  if (axis_labels[0].begin != axis_labels[0].end && axis_label_fonts[0]) {
    FtFont *font = axis_label_fonts[0];
    u32 length = static_cast<u32>(axis_labels[0].end - axis_labels[0].begin);
    auto alignment = static_cast<FtOutputStyle>(unknown_440);
    if (axis_label_colors[0] == kUiGraphUseFontColor)
      font->DrawTextPara(target, axis_label_positions[0], axis_labels[0].begin, length, alignment);
    else
      font->DrawTextPara(
          target, axis_label_positions[0], axis_labels[0].begin, length, axis_label_colors[0], alignment);
  }
  if (axis_labels[1].begin != axis_labels[1].end && axis_label_fonts[1]) {
    FtFont *font = axis_label_fonts[1];
    i32 rotation = static_cast<i32>(axis_label_rotations[1]);
    auto alignment = static_cast<FtOutputStyle>(unknown_444);
    if (rotation == 0) {
      u32 length = static_cast<u32>(axis_labels[1].end - axis_labels[1].begin);
      if (axis_label_colors[1] == kUiGraphUseFontColor)
        font->DrawTextPara(target, axis_label_positions[1], axis_labels[1].begin, length, alignment);
      else
        font->DrawTextPara(
            target, axis_label_positions[1], axis_labels[1].begin, length, axis_label_colors[1], alignment);
    } else {
      GxBuffer *temporary = nullptr;
      ApGetGraphics()->CreateBuffer(&temporary);
      ScRect rectangle{};
      rotation = static_cast<i32>(axis_label_rotations[1]);
      u32 width = static_cast<u32>(axis_label_positions[1].right) - static_cast<u32>(axis_label_positions[1].left);
      u32 height = static_cast<u32>(axis_label_positions[1].bottom) - static_cast<u32>(axis_label_positions[1].top);
      rectangle.right = static_cast<i32>((rotation == 2 ? width : height) + 4);
      rectangle.bottom = static_cast<i32>((rotation == 2 ? height : width) + 4);
      temporary->Init(static_cast<u32>(rectangle.right), static_cast<u32>(rectangle.bottom),
          GxColorType(GxColorType::Color::RGB16), nullptr, GxDepthType(GxDepthType::Depth::None),
          GxVramRequest::VideoMemoryLowPriority, false);
      if (temporary->Lock(GxLockFlags::Fill)) {
        temporary->FillNoClip(transparent_color, rectangle);
        temporary->Unlock(GxLockFlags::Fill);
      }
      if (axis_label_fonts[1]) {
        font = axis_label_fonts[1];
        alignment = static_cast<FtOutputStyle>(unknown_444);
        u32 length = static_cast<u32>(axis_labels[1].end - axis_labels[1].begin);
        if (axis_label_colors[1] == kUiGraphUseFontColor)
          font->DrawTextPara(temporary, rectangle, axis_labels[1].begin, length, alignment);
        else
          font->DrawTextPara(temporary, rectangle, axis_labels[1].begin, length, axis_label_colors[1], alignment);
      }
      temporary->SetTransparency(transparent_color);
      if (target->Lock(GxLockFlags::BltRotate)) {
        rotation = static_cast<i32>(axis_label_rotations[1]);
        u32 angle = rotation == 1 ? 90 : rotation == 2 ? 180 : 270;
        ScPoint destination{axis_label_positions[1].left, axis_label_positions[1].top};
        target->RotateBltSimple(temporary, rectangle, destination, angle, nullptr);
        target->Unlock(GxLockFlags::BltRotate);
      }
      temporary->Release();
    }
  }
  if (title.begin != title.end && title_font) {
    u32 length = static_cast<u32>(title.end - title.begin);
    auto alignment = static_cast<FtOutputStyle>(unknown_240);
    if (title_color == kUiGraphUseFontColor)
      title_font->DrawTextPara(target, title_position, title.begin, length, alignment);
    else
      title_font->DrawTextPara(target, title_position, title.begin, length, title_color, alignment);
  }
}

WnGraphObject::~WnGraphObject() {
}

void WnGraphObject::DrawSelf(WnGraphView *, GxBuffer *) {
}

WnGraphLine::WnGraphLine() : x1(0), y1(0), x2(0), y2(0), color(0), thickness(1) {
}

WnGraphLine::~WnGraphLine() {
}

void WnGraphLine::DrawSelf(WnGraphView *, GxBuffer *buffer) {
  if (buffer->Lock(GxLockFlags::Line)) {
    buffer->DrawLine(x1, y1, x2, y2, color, thickness);
    buffer->Unlock(GxLockFlags::Line);
  }
}

WnGraphRectangle::WnGraphRectangle()
    : rectangle{}, outline(true), thickness(1), outline_color(0), fill(false), fill_color(kUiGraphDefaultFillColor) {
}

WnGraphRectangle::~WnGraphRectangle() {
}

// Fill and outline the rectangle under their respective buffer locks when each operation is enabled.
void WnGraphRectangle::DrawSelf(WnGraphView *, GxBuffer *buffer) {
  if (fill && buffer->Lock(GxLockFlags::Fill)) {
    buffer->Fill(fill_color, &rectangle);
    buffer->Unlock(GxLockFlags::Fill);
  }
  if (outline && buffer->Lock(GxLockFlags::Rect)) {
    buffer->DrawRectangleOutline(rectangle, outline_color, thickness);
    buffer->Unlock(GxLockFlags::Rect);
  }
}
