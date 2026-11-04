#include "ui/advisor_dialogs.h"

#include <assert.h>
#include <stdlib.h>

#include "advisors/ad_advice.h"
#include "advisors/ad_text.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "resources/resource.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ordinance_dialogs.h"
#include "ui/petitions_deals.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/window.h"

// ScMessageServer routes these subscriptions to UiAdvisorDialogManager::DoMessage.
// Unresolved32 is acknowledged without action; Unresolved33 follows the default path. Their request purposes are unknown.
enum UiUnresolvedAdvisorMessage : u32 {
  kWnMsg_Unresolved32 = 0x724a82d3,
  kWnMsg_Unresolved33 = 0x724a82d8,
};

// The portrait fallback rejects an instance word filled with the CRT debug heap new-object byte.
enum UiPortraitDebugHeapMarker : u32 {
  kUiDebugUninitializedPortraitInstance = 0xcdcdcdcd,
};

// Advisor menus retain a review-ordinances caption and a contiguous group of ordinary menu captions.
enum UiAdvisorCaptionId : u32 {
  kUiAdvisorReviewOrdinancesLabelId = 0x62be5008,
  kUiAdvisorMenuLabelIdBase = 0x62be5010,
};

// The advisor dialog uses the installed fallback portrait when a selected advisor supplies no picture.
enum UiDefaultAdvisorPortraitId : u32 {
  kUiDefaultAdvisorPortraitId = 0x10303,
};

// Read the current animation word through the platform's existing read operation.
i32 UiAnimationOffset::Read() {
  return SDL_GetAtomicInt(&value);
}

// Publish the next frame through the platform's atomic exchange operation.
void UiAnimationOffset::Write(i32 offset) {
  SDL_SetAtomicInt(&value, offset);
}

// Construct the advisor button with the window resource provider's two original outline colors.
UiAdvisorButton::UiAdvisorButton()
    : unknown168(0), button_flags(0), image(nullptr), frames(0), image_type(0), source{}, animation_offset{0} {
  normal_color = UiGetWindowResources()->GetColor(19);
  pressed_color = UiGetWindowResources()->GetColor(20);
}

UiAdvisorButton::~UiAdvisorButton() {
}

// Publish the advisor-button secondary identity and delegate other queries to the original window base.
bool UiAdvisorButton::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiAdvisorButtonInterface)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiAdvisorButtonInterface *>(this);
  AddRef();
  return true;
}

u32 UiAdvisorButton::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiAdvisorButton::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window corresponding to this advisor-button interface.
WnWindow *UiAdvisorButton::AsIGZWin() {
  return this;
}

// Set the current and stored outline colors in the original argument order.
u8 UiAdvisorButton::SetOutlineColors(u32 first, u32 second) {
  normal_color = outline_first = first;
  pressed_color = outline_second = second;
  return 1;
}

// Require a selected image mode, initialize the outline colors and start animation when multiple frames exist.
bool UiAdvisorButton::Init() {
  if (!image_type)
    return false;
  outline_first = normal_color;
  outline_second = pressed_color;
  if (frames > 1)
    timer.StartPeriodic(0, nullptr);
  return UiBaseWindow::Init();
}

bool UiAdvisorButton::Shutdown() {
  ScRelease(&image);
  timer.Stop();
  return UiBaseWindow::Shutdown();
}

// Pass keyboard messages and all messages for active buttons to the common window handler.
bool UiAdvisorButton::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp || image_type == 1)
    return UiBaseWindow::DoMessage(message);
  return true;
}

bool UiAdvisorButton::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiAdvisorButton::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Set or clear the requested advisor-button state bits without changing unrelated flags.
u8 UiAdvisorButton::SetAdvBtnFlag(i32 mask, bool enabled) {
  if (enabled)
    button_flags |= (u32)mask;
  else
    button_flags &= ~(u32)mask;
  return 1;
}

i32 UiAdvisorButton::GetAdvBtnFlag(i32 mask) {
  return (button_flags & (u32)mask) != 0;
}

u8 UiAdvisorButton::ShowTip() {
  WnHint hint;
  hint.flags |= 3;
  hint.text = &caption;
  hint.rectangle = absolute_area;
  ScMessage message(kWnMsg_StartHint, reinterpret_cast<uptr>(&hint), 0, 0);
  UiGetMessageServer()->MessageSend(message);
  return 1;
}

u8 UiAdvisorButton::HideTip() {
  ScMessage message(kWnMsg_CancelHint, 0, 0, 0);
  UiGetMessageServer()->MessageSend(message);
  return 1;
}

u8 UiAdvisorButton::SetAdvImage(i32 type, GxBuffer *buffer, const ScRect *, i32 count) {
  if (image_type)
    return 0;
  frames = (i16)count;
  if (type <= 0 || type > 2)
    return 0;
  image_type = (i16)type;
  ScRelease(&image);
  image = buffer;
  if (image) {
    image->AddRef();
    if (image_type == 1) {
      image->GetBufferArea(&source);
      i32 height = (i32)image->Height(), width = (i32)image->Width();
      SetSize(width, height);
    } else {
      if (frames) {
        i32 height = (i32)image->Height();
        u32 width = image->Width();
        SetSize((i32)(width / (u32)(i32)frames), height);
      } else {
        SetSize(94, 117);
      }
      i32 height = GetH(), width = GetW();
      source = {0, 0, width, height};
    }
  } else if (image_type == 1) {
    SetSize(48, 58);
  } else {
    SetSize(94, 117);
  }
  if (frames > 1) {
    timer.SetResolution(1000);
    timer.SetPeriod((u32)(ScRand() & 255) + 750);
    uptr receiver = reinterpret_cast<uptr>(this);
    timer.SetCallbackFunction(AnimateCallback, receiver);
  }
  return 1;
}

void UiAdvisorButton::AnimateEffects() {
  i32 offset = 0;
  if (!animation_offset.Read() && (ScRand() & 1))
    offset = source.right + (source.right - source.left) * (ScRand() % (frames - 1)) - source.left;
  animation_offset.Write(offset);
  AsIGZWin()->InvalidateSelfAndParents();
}

void UiAdvisorButton::AnimateCallback(uptr value) {
  reinterpret_cast<UiAdvisorButton *>(value)->AnimateEffects();
}

// Return the original primary window identity from the advisor-button class factory.
ScUnknown *UiCreateClass62BE6EE2() {
  return static_cast<WnWindow *>(new UiAdvisorButton);
}

bool UiAdvisorButton::OnMouseDownL(i32, i32, u32) {
  if (image_type == 1) {
    GZSetCapture();
    SetAdvBtnFlag(512, true);
    HideTip();
    outline_first = pressed_color;
    outline_second = pressed_color;
  }
  return true;
}

// Post the original button command when a captured press ends inside the active button.
bool UiAdvisorButton::OnMouseUpL(i32 x, i32 y, u32) {
  if (image_type != 1)
    return true;
  if (IsPointInWindow(x, y)) {
    outline_first = pressed_color;
    outline_second = pressed_color;
    if (GetAdvBtnFlag(512))
      SendMsg(notification_target, kWnMsg_Command, kWnCommandButtonActivated, GetID(), 0);
  } else {
    outline_first = normal_color;
    outline_second = pressed_color;
    GZReleaseCapture();
  }
  SetAdvBtnFlag(512, false);
  return true;
}

// Track hover and capture, forwarding the original mouse message when the pointer leaves the button.
bool UiAdvisorButton::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  if (image_type != 1 || GetAdvBtnFlag(512))
    return true;
  if (IsPointInWindow(x, y)) {
    if (!GetAdvBtnFlag(256)) {
      SetAdvBtnFlag(256, true);
      GZSetCapture();
      outline_first = pressed_color;
      outline_second = pressed_color;
      ShowTip();
    }
  } else if (GetAdvBtnFlag(256)) {
    SetAdvBtnFlag(256, false);
    outline_first = normal_color;
    outline_second = pressed_color;
    WindowToScreenCoordinates(&x, &y);
    GZReleaseCapture();
    GetWindowManager()->GZWinMsgPost_Mouse(11, x, y, modifiers);
    HideTip();
  }
  AsIGZWin()->InvalidateSelfAndParents();
  return true;
}

// Restore normal outlines, clear hover and press state, and dismiss the hint when capture moves elsewhere.
bool UiAdvisorButton::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    outline_first = normal_color;
    outline_second = pressed_color;
    SetAdvBtnFlag(256, false);
    SetAdvBtnFlag(512, false);
    HideTip();
  }
  return true;
}

// Draw the active art frame and original two-color border, including the outer pressed outline when colors match.
bool UiAdvisorButton::GZPaint() {
  button_flags |= kUiAdvisorButtonPainting;
  ScRect image_source = source;
  if (image_type == 2) {
    image_source.left += animation_offset.Read();
    image_source.right += animation_offset.Read();
  }
  if (image && draw_buffer->Lock(GxLockFlags::Blt)) {
    draw_buffer->Blt(image, image_source, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if (draw_buffer->Lock(GxLockFlags::Line)) {
    i32 left = draw_area.left, top = draw_area.top, right = draw_area.right - 1, bottom = draw_area.bottom - 1;
    draw_buffer->DrawLine(left, top, left, bottom, (i32)outline_first, 1);
    draw_buffer->DrawLine(left, top, right, top, (i32)outline_first, 1);
    draw_buffer->DrawLine(right, top, right, bottom, (i32)outline_second, 1);
    draw_buffer->DrawLine(left, bottom, right, bottom, (i32)outline_second, 1);
    if (outline_first == outline_second) {
      draw_buffer->DrawLine(left - 1, top - 1, left - 1, bottom + 1, (i32)outline_first, 1);
      draw_buffer->DrawLine(left - 1, top - 1, right + 1, top - 1, (i32)outline_first, 1);
      draw_buffer->DrawLine(right + 1, top - 1, right + 1, bottom + 1, (i32)outline_second, 1);
      draw_buffer->DrawLine(left - 1, bottom + 1, right + 1, bottom + 1, (i32)outline_second, 1);
    }
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  button_flags &= ~kUiAdvisorButtonPainting;
  return true;
}

UiAdvisorDialogManager::UiAdvisorDialogManager()
    : references(0), active(false), dialog_state(false), meet(nullptr), advisor_main(nullptr), advisor_topic(nullptr),
      petitioner(nullptr), ordinance(nullptr) {
}

UiAdvisorDialogManager::~UiAdvisorDialogManager() {
}

// Publish the primary advisor-manager identity for Unknown or the original class IID.
bool UiAdvisorDialogManager::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiAdvisorManager))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiAdvisorManager *>(this);
  AddRef();
  return true;
}

u32 UiAdvisorDialogManager::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Decrement the original signed count and destroy the manager when it is no longer positive.
u32 UiAdvisorDialogManager::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

// Allocate the original 32-byte manager and return its primary identity without an extra retained reference.
ScUnknown *UiCreateClass22BE563E() {
  return new UiAdvisorDialogManager;
}

// Route modeless and modal requests and state notifications through the original close-before-open ordering.
bool UiAdvisorDialogManager::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kApMsgPreCityChange:
    RemoveAdvisors();
    RemoveOrdinances();
    break;
  case kWnMsg_MeetAdvisors:
    RemoveAdvisors();
    OnMeetDialog();
    break;
  case kWnMsg_AdvisorDialog:
    RemoveAdvisors();
    OnAdvisorDialog(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_AdvisorTopic:
    RemoveAdvisors();
    OnAdvisorTopic(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_Unresolved32:
    break;
  case kWnMsg_AdvisorBriefing:
    RemoveAdvisors();
    OnAdvisorBriefing(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_AdvisorOrdinance:
    RemoveAdvisors();
    OnAdvisorOrdinance(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_PetitionerDialog:
    RemoveAdvisors();
    OnPetitioner(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_OrdinanceDialog:
    RemoveOrdinances();
    OnOrdinanceDialog();
    break;
  case kWnMsg_AdvisorDialogState:
    dialog_state = message->data1 != 0;
    break;
  case kWnMsg_ModalAdvisorTopicList:
    ModalAdvisorTopicList(static_cast<u32>(message->data1));
    break;
  case kWnMsg_ModalAdvisorBriefingList:
    ModalAdvisorBriefingList(static_cast<u32>(message->data1));
    break;
  case kWnMsg_ModalAdvisorOrdinanceList:
    ModalAdvisorOrdinanceList(static_cast<u32>(message->data1));
    break;
  case kWnMsg_ModalAdvisorTopic:
    ModalAdvisorTopic(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_ModalAdvisorBriefing:
    ModalAdvisorBriefing(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_ModalAdvisorOrdinance:
    ModalAdvisorOrdinance(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_ModalPetitioner:
    ModalPetitioner(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  case kWnMsg_ModalOrdinanceDialog:
    ModalOrdinanceDialog();
    break;
  case kWnMsg_ModalMeetAdvisors:
    ModalMeetDialog();
    break;
  case kWnMsg_ModalAdvisorEndGame:
    ModalAdvisorEndGame(static_cast<u32>(message->data1), static_cast<u32>(message->data2));
    break;
  default:
    return false;
  }
  return true;
}

// Close modeless advisors, select topic category1 and run a separately retained modal callback.
// A refused window creation skips the modal loop; the callback is still released in the original order.
void UiAdvisorDialogManager::ModalAdvisorTopic(u32 advisor, u32 topic) {
  RemoveAdvisors();
  UiAdvisorTopic *dialog = new UiAdvisorTopic;
  dialog->AddRef();
  WnWindow *window = nullptr;
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopicID(topic);
  dialog->SetTopicType(1);
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors, select topic category2 and run a separately retained modal callback.
// A refused window creation skips the modal loop; the callback is still released in the original order.
void UiAdvisorDialogManager::ModalAdvisorBriefing(u32 advisor, u32 topic) {
  RemoveAdvisors();
  UiAdvisorTopic *dialog = new UiAdvisorTopic;
  dialog->AddRef();
  WnWindow *window = nullptr;
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopicID(topic);
  dialog->SetTopicType(2);
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors, select topic category3 and run a separately retained modal callback.
// A refused window creation skips the modal loop; the callback is still released in the original order.
void UiAdvisorDialogManager::ModalAdvisorOrdinance(u32 advisor, u32 topic) {
  RemoveAdvisors();
  UiAdvisorTopic *dialog = new UiAdvisorTopic;
  dialog->AddRef();
  WnWindow *window = nullptr;
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopicID(topic);
  dialog->SetTopicType(3);
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors and present the selected advisor's category1 list through a fresh modal callback.
// Retire a successfully created window before releasing the callback, including the ordinary creation-failure path.
void UiAdvisorDialogManager::ModalAdvisorTopicList(u32 advisor) {
  RemoveAdvisors();
  UiAdvisorMain *dialog = new UiAdvisorMain;
  dialog->AddRef();
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopic(1);
  WnWindow *window = nullptr;
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors and present the selected advisor's category2 list through a fresh modal callback.
// Retire a successfully created window before releasing the callback, including the ordinary creation-failure path.
void UiAdvisorDialogManager::ModalAdvisorBriefingList(u32 advisor) {
  RemoveAdvisors();
  UiAdvisorMain *dialog = new UiAdvisorMain;
  dialog->AddRef();
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopic(2);
  WnWindow *window = nullptr;
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors and present the selected advisor's category3 list through a fresh modal callback.
// Retire a successfully created window before releasing the callback, including the ordinary creation-failure path.
void UiAdvisorDialogManager::ModalAdvisorOrdinanceList(u32 advisor) {
  RemoveAdvisors();
  UiAdvisorMain *dialog = new UiAdvisorMain;
  dialog->AddRef();
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopic(3);
  WnWindow *window = nullptr;
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors and pass the original petitioner identifier and behavior flags to a fresh modal callback.
// Creation failure still releases the callback; a created window is destroyed and released after its modal loop.
void UiAdvisorDialogManager::ModalPetitioner(u32 identifier, u32 flags) {
  RemoveAdvisors();
  UiPetitionDialog *dialog = new UiPetitionDialog;
  dialog->AddRef();
  dialog->SetCurrentPetID(identifier, flags);
  WnWindow *window = nullptr;
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close existing ordinances before constructing a separate modal ordinance callback and its window.
// Release the callback after either refused creation or the successful modal/destroy/release sequence.
void UiAdvisorDialogManager::ModalOrdinanceDialog() {
  RemoveOrdinances();
  UiOrdinanceDialog *dialog = new UiOrdinanceDialog;
  dialog->AddRef();
  WnWindow *window = nullptr;
  if (dialog->CreateWin(&window)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Close modeless advisors and construct a separately retained meeting callback for the modal request.
// Destroy and release its window before dropping the callback's final reference.
void UiAdvisorDialogManager::ModalMeetDialog() {
  RemoveAdvisors();
  UiMeetMenu *dialog = new UiMeetMenu;
  dialog->AddRef();
  WnWindow *window = nullptr;
  if (dialog->CreateWin(&window)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
}

// Hold the simulation while showing the selected end-game topic, then post the action chosen by its modal result.
// Creation failure retains result zero and still unpauses the freshly queried city after releasing the callback.
void UiAdvisorDialogManager::ModalAdvisorEndGame(u32 advisor, u32 topic) {
  UiGetCity()->SimHiddenPause();
  RemoveAdvisors();
  UiAdvisorTopic *dialog = new UiAdvisorTopic;
  dialog->AddRef();
  WnWindow *window = nullptr;
  u32 result = 0;
  dialog->SetCurrentAdvisorID(advisor);
  dialog->SetCurrentTopicID(topic);
  dialog->SetEndGame(true);
  dialog->SetTopicType(1);
  if (dialog->CreateWin(&window, true)) {
    WnWindowManager *root = UiGetWindowManager(), *manager = UiGetWindowManager();
    result = manager->DoModalWin(root->GetMainWindow(), window);
    UiGetWindowManager()->DestroyWindow(window);
    window->Release();
  }
  dialog->Release();
  switch (result) {
  case 1: {
    ScMessage message(kCtMsg_RequestLoadCity);
    UiGetMessageServer()->MessagePost(message, false);
    break;
  }
  case 2: {
    ScMessage message(kCtMsg_RequestNewCity);
    UiGetMessageServer()->MessagePost(message, false);
    break;
  }
  case 3: {
    ScMessage message(kCtMsg_RequestQuitGame);
    UiGetMessageServer()->MessagePost(message, false);
    break;
  }
  }
  UiGetCity()->SimHiddenUnpause();
}

// Subscribe before creating and retaining the five independent dialog owners. Initialize saved positions only once.
bool UiAdvisorDialogManager::Init() {
  if (active)
    return true;
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    const u32 notifications[] = {kWnMsg_MeetAdvisors, kWnMsg_AdvisorDialog, kWnMsg_AdvisorTopic, kWnMsg_Unresolved32,
        kWnMsg_AdvisorBriefing, kWnMsg_AdvisorOrdinance, kWnMsg_PetitionerDialog, kWnMsg_OrdinanceDialog,
        kWnMsg_Unresolved33, kWnMsg_AdvisorDialogState, kApMsgPreCityChange, kWnMsg_ModalAdvisorTopicList,
        kWnMsg_ModalAdvisorBriefingList, kWnMsg_ModalAdvisorOrdinanceList, kWnMsg_ModalAdvisorTopic,
        kWnMsg_ModalAdvisorBriefing, kWnMsg_ModalAdvisorOrdinance, kWnMsg_ModalPetitioner, kWnMsg_ModalOrdinanceDialog,
        kWnMsg_ModalMeetAdvisors, kWnMsg_ModalAdvisorEndGame};
    for (u32 notification : notifications)
      server->AddNotification(this, notification);
  }
  meet = ScAddRefNotNull(new UiMeetMenu);
  advisor_main = ScAddRefNotNull(new UiAdvisorMain);
  advisor_topic = ScAddRefNotNull(new UiAdvisorTopic);
  petitioner = ScAddRefNotNull(new UiPetitionDialog);
  ordinance = ScAddRefNotNull(new UiOrdinanceDialog);
  if (!UiGetPreferences()->AdvMgrPrefs()->ready) {
    UiGetPreferences()->AdvMgrPrefs()->meeting_x = 8;
    UiGetPreferences()->AdvMgrPrefs()->meeting_y = 8;
    UiGetPreferences()->AdvMgrPrefs()->main_x = 8;
    UiGetPreferences()->AdvMgrPrefs()->main_y = 8;
    UiGetPreferences()->AdvMgrPrefs()->main_page = 1;
    UiGetPreferences()->AdvMgrPrefs()->topic_x = 8;
    UiGetPreferences()->AdvMgrPrefs()->topic_y = 8;
    UiGetPreferences()->AdvMgrPrefs()->petition_x = 8;
    UiGetPreferences()->AdvMgrPrefs()->petition_y = 8;
    UiGetPreferences()->AdvMgrPrefs()->ordinance_x = 8;
    UiGetPreferences()->AdvMgrPrefs()->ordinance_y = 8;
    UiGetPreferences()->AdvMgrPrefs()->ready = 1;
  }
  active = true;
  return true;
}

// Attach the retained meeting window to the main window and release its temporary creation reference.
u8 UiAdvisorDialogManager::OnMeetDialog() {
  if (!meet)
    return 0;
  WnWindow *window = nullptr;
  if (!meet->CreateWin(&window))
    return 1;
  u8 added = UiGetWindowManager()->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

// Select the advisor and category before creating its retained dialog. The high flag bit controls modeless styling.
u8 UiAdvisorDialogManager::OnAdvisorDialog(u32 advisor, u32 flags) {
  if (!advisor_main)
    return 0;
  WnWindow *window = nullptr;
  advisor_main->SetCurrentAdvisorID(advisor);
  advisor_main->SetCurrentTopic(flags & kUiAdvisorCategoryMask);
  if (!advisor_main->CreateWin(&window, (flags & kUiAdvisorModeless) == 0))
    return 1;
  u8 added = UiGetWindowManager()->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

// Select category 1 and the requested topic, retaining the independent back-button and modeless flags.
u8 UiAdvisorDialogManager::OnAdvisorTopic(u32 advisor, u32 flags) {
  if (!advisor_topic)
    return 0;
  WnWindow *window = nullptr;
  advisor_topic->SetCurrentAdvisorID(advisor);
  advisor_topic->SetCurrentTopicID(flags & kUiAdvisorTopicMask);
  advisor_topic->SetTopicType(1);
  advisor_topic->SetParentNoBackButton((flags & kUiAdvisorNoBackButton) != 0);
  if (!advisor_topic->CreateWin(&window, (flags & kUiAdvisorModeless) == 0))
    return 1;
  u8 added = UiGetWindowManager()->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

// Select category 2 and the requested topic, retaining the independent back-button and modeless flags.
u8 UiAdvisorDialogManager::OnAdvisorBriefing(u32 advisor, u32 flags) {
  if (!advisor_topic)
    return 0;
  WnWindow *window = nullptr;
  advisor_topic->SetCurrentAdvisorID(advisor);
  advisor_topic->SetCurrentTopicID(flags & kUiAdvisorTopicMask);
  advisor_topic->SetTopicType(2);
  advisor_topic->SetParentNoBackButton((flags & kUiAdvisorNoBackButton) != 0);
  if (!advisor_topic->CreateWin(&window, (flags & kUiAdvisorModeless) == 0))
    return 1;
  u8 added = UiGetWindowManager()->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

// Select category 3 and the requested topic, retaining the independent back-button and modeless flags.
u8 UiAdvisorDialogManager::OnAdvisorOrdinance(u32 advisor, u32 flags) {
  if (!advisor_topic)
    return 0;
  WnWindow *window = nullptr;
  advisor_topic->SetCurrentAdvisorID(advisor);
  advisor_topic->SetCurrentTopicID(flags & kUiAdvisorTopicMask);
  advisor_topic->SetTopicType(3);
  advisor_topic->SetParentNoBackButton((flags & kUiAdvisorNoBackButton) != 0);
  if (!advisor_topic->CreateWin(&window, (flags & kUiAdvisorModeless) == 0))
    return 1;
  u8 added = UiGetWindowManager()->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

// Select a petitioner with cleared behavior flags and attach its retained window using the requested modal style.
u8 UiAdvisorDialogManager::OnPetitioner(u32 identifier, u32 flags) {
  if (!petitioner)
    return 0;
  WnWindow *window = nullptr;
  petitioner->SetCurrentPetID(identifier, 0);
  if (!petitioner->CreateWin(&window, (flags & kUiAdvisorModeless) == 0))
    return 1;
  u8 added = UiGetWindowManager()->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

u8 UiAdvisorDialogManager::OnOrdinanceDialog() {
  if (!ordinance)
    return 0;
  WnWindow *window = nullptr;
  if (!ordinance->CreateWin(&window))
    return 1;
  u8 added = window->GetMainWindow()->ChildAdd(window);
  window->Release();
  return added;
}

// Unsubscribe the initialized manager and release its five dialog owners in their original order.
// The inactive path leaves all fields untouched and reports successful shutdown.
bool UiAdvisorDialogManager::Shutdown() {
  if (active) {
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      const u32 notifications[] = {kWnMsg_MeetAdvisors, kWnMsg_AdvisorDialog, kWnMsg_AdvisorTopic, kWnMsg_Unresolved32,
          kWnMsg_AdvisorBriefing, kWnMsg_AdvisorOrdinance, kWnMsg_PetitionerDialog, kWnMsg_AdvisorDialogState,
          kWnMsg_OrdinanceDialog, kWnMsg_Unresolved33, kApMsgPreCityChange, kWnMsg_ModalAdvisorTopicList,
          kWnMsg_ModalAdvisorBriefingList, kWnMsg_ModalAdvisorOrdinanceList, kWnMsg_ModalAdvisorTopic,
          kWnMsg_ModalAdvisorBriefing, kWnMsg_ModalAdvisorOrdinance, kWnMsg_ModalPetitioner,
          kWnMsg_ModalOrdinanceDialog, kWnMsg_ModalMeetAdvisors, kWnMsg_ModalAdvisorEndGame};
      for (u32 notification : notifications)
        server->RemoveNotification(this, notification);
    }
    meet->Release();
    meet = nullptr;
    advisor_main->Release();
    advisor_main = nullptr;
    advisor_topic->Release();
    advisor_topic = nullptr;
    petitioner->Release();
    petitioner = nullptr;
    ordinance->Release();
    ordinance = nullptr;
    active = false;
  }
  return true;
}

// Retire advisor-family window identifiers before requesting their close, then process the posted close messages.
// The three nested topic checks preserve the original repeated lookups and any intervening callback effects.
u8 UiAdvisorDialogManager::RemoveAdvisors() {
  if (meet) {
    WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorMeetingWindowId);
    if (window) {
      window->SetID(kUiAdvisorRetiredWindowId);
      meet->KillWin(window);
    }
  }
  if (advisor_main) {
    WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorMainWindowId);
    if (window) {
      window->SetID(kUiAdvisorRetiredWindowId);
      advisor_main->KillWin(window);
    }
  }
  if (advisor_topic) {
    WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorTopicWindowId);
    if (window) {
      window->SetID(kUiAdvisorRetiredWindowId);
      advisor_topic->KillWin(window, 0);
    }
    if (advisor_topic) {
      window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorTopicWindowId);
      if (window) {
        window->SetID(kUiAdvisorRetiredWindowId);
        advisor_topic->KillWin(window, 0);
      }
      if (advisor_topic) {
        window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorTopicWindowId);
        if (window) {
          window->SetID(kUiAdvisorRetiredWindowId);
          advisor_topic->KillWin(window, 0);
        }
      }
    }
  }
  if (petitioner) {
    WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorPetitionerWindowId);
    if (window) {
      window->SetID(kUiAdvisorRetiredWindowId);
      petitioner->KillWin(window);
    }
  }
  UiGetWindowManager()->ProcessPostedMessages();
  return 1;
}

// Retire the ordinance window's identifier before posting its close and processing queued messages.
u8 UiAdvisorDialogManager::RemoveOrdinances() {
  if (ordinance) {
    WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorOrdinanceWindowId);
    if (window) {
      window->SetID(kUiAdvisorRetiredWindowId);
      ordinance->KillWin(window);
    }
  }
  UiGetWindowManager()->ProcessPostedMessages();
  return 1;
}

u8 UiAdvisorMain::GetWinArea(ScRect *out) const {
  *out = area;
  return 1;
}

// Save advisor position and page, release its topic headers and unregister the secondary message-target view.
u8 UiAdvisorMain::OnShutdown(WnWindow *window) {
  ApPreferences *preferences = UiGetPreferences();
  u32 left = (u32)window->GetL();
  preferences->AdvMgrPrefs()->main_x = left;
  preferences = UiGetPreferences();
  u32 top = (u32)window->GetT();
  preferences->AdvMgrPrefs()->main_y = top;
  UiGetPreferences()->AdvMgrPrefs()->main_page = current_topic;
  UiGetPreferences()->AdvMgrPrefs()->ready = 1;
  KillTopicHeaders();
  current_advisor = 0;
  window_exists = 0;
  if (UiGetMessageServer())
    UiGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_AdviceChanged);
  return 1;
}

// Replace the three advisor header arrays under the city's critical section, preserving count/output ordering.
// A failed extraction retires all published headers; a null advisor leaves the prior advisor mood untouched.
u8 UiAdvisorMain::GetTopicHeaders(AdAdvisor *advisor) {
  ScLock *lock = UiGetCity()->GetCriticalSection();
  lock->Lock();
  KillTopicHeaders();
  u8 result = 0;
  if (advisor) {
    advisor_mood = 0;
    advice_count = advisor->GetAdviceQueueSize();
    bool extracted = true;
    if (advice_count) {
      advice = new AdHeaderRecord *[advice_count];
      extracted = advisor->GetAdviceHeaders(&advice_count, advice);
    }
    if (extracted) {
      advisor_mood = advisor->GetAdvisorAverageMood();
      ordinance_count = advisor->GetOrdinanceQueueSize();
      if (ordinance_count) {
        ordinances = new AdHeaderRecord *[ordinance_count];
        extracted = advisor->GetOrdinanceHeaders(&ordinance_count, ordinances);
      }
      if (extracted) {
        help_count = advisor->GetHelpQueueSize();
        if (help_count) {
          help = new AdHeaderRecord *[help_count];
          extracted = advisor->GetHelpHeaders(&help_count, help);
        }
        if (extracted)
          result = 1;
      }
    }
    if (!extracted)
      KillTopicHeaders();
  }
  lock->Unlock();
  return result;
}

// Release each retained header and delete its array, clearing counts only for arrays that actually exist.
u8 UiAdvisorMain::KillTopicHeaders() {
  if (advice) {
    for (u32 i = 0; i < advice_count; ++i)
      if (advice[i])
        advice[i]->Release();
    delete[] advice;
    advice = nullptr;
    advice_count = 0;
  }
  if (ordinances) {
    for (u32 i = 0; i < ordinance_count; ++i)
      if (ordinances[i])
        ordinances[i]->Release();
    delete[] ordinances;
    ordinances = nullptr;
    ordinance_count = 0;
  }
  if (help) {
    for (u32 i = 0; i < help_count; ++i)
      if (help[i])
        help[i]->Release();
    delete[] help;
    help = nullptr;
    help_count = 0;
  }
  return 1;
}

// Rebuild the selected topic category using temporary retained text copies from each header.
// Category zero only clears the list; other categories also become the stored current page.
u8 UiAdvisorMain::FillTopicListView(ApListViewControl *list, u32 topic) {
  list->RemoveAllItems();
  if (!topic)
    return 1;
  AdHeaderRecord **headers = nullptr;
  u32 count = 0;
  if (topic == 1) {
    headers = advice;
    count = advice_count;
  } else if (topic == 2) {
    headers = ordinances;
    count = ordinance_count;
  } else if (topic == 3) {
    headers = help;
    count = help_count;
  }
  for (u32 i = 0; i < count; ++i) {
    if (!headers[i])
      continue;
    ScStringView *text = nullptr;
    headers[i]->GetText(&text);
    if (text) {
      u32 id = headers[i]->GetID();
      list->AddItem((i32)id, 0, text);
      text->Release();
    }
  }
  current_topic = topic;
  return 1;
}

UiAdvisorMain::UiAdvisorMain()
    : references(0), behavior_flags(0), current_topic(1), window_exists(0), advice_count(0), advice(nullptr),
      ordinance_count(0), ordinances(nullptr), help_count(0), help(nullptr), advisor_mood(0) {
}

UiAdvisorMain::~UiAdvisorMain() {
}

// Publish the primary callback identity for Unknown or the original callback IID.
bool UiAdvisorMain::QueryInterface(u32 iid, void **out) {
  if (iid == IID_UiWindowCallback) {
    if (!out)
      RETURN_QUERY_INTERFACE_FAILURE();
  } else if (iid != IID_ScUnknown)
    RETURN_QUERY_INTERFACE_FAILURE();
  assert(out);
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiAdvisorMain::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Destroy the owner when its original signed reference count is no longer positive.
u32 UiAdvisorMain::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

u8 UiAdvisorMain::WinExists() const {
  return window_exists;
}

u8 UiAdvisorMain::SetCurrentAdvisorID(u32 identifier) {
  current_advisor = identifier;
  return 1;
}

u8 UiAdvisorMain::SetCurrentTopic(u32 identifier) {
  current_topic = identifier;
  return 1;
}

// Create the generic advisor dialog and configure its modal-dependent style before publishing its primary view.
u8 UiAdvisorMain::CreateWin(WnWindow **out, bool modal) {
  *out = nullptr;
  if (WinExists())
    return 0;
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return 0;
  window->SetStyle(kUiGenericDraggable, 1);
  window->SetStyle(kUiGenericResizable, 0);
  window->SetStyle(kUiGenericScroller, 0);
  window->SetStyle(kUiGenericSecondButton, 0);
  window->SetStyle(kUiGenericBackButton, !modal);
  window->GetWindow()->PrivateBuffer(true);
  window->SetCallback(static_cast<UiWindowCallback *>(this));
  *out = window->GetWindow();
  (*out)->SetID(kUiAdvisorMainWindowId);
  behavior_flags = modal ? kUiAdvisorBehaviorModal : 0;
  return 1;
}

// End a modal advisor dialog or notify the parent of a modeless one, then clear the open-window byte.
u8 UiAdvisorMain::KillWin(WnWindow *window) {
  if (!window)
    return 0;
  if (window->GetFlag(kWnFlagModal))
    UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  else {
    WnWindowManager *manager = window->GetWindowManager();
    WnWindow *parent = window->GetParentWin();
    manager->PostMessageToTarget(parent, kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
  }
  window_exists = 0;
  return 1;
}

bool UiAdvisorMain::DoMessage(ScMessage *message) {
  return message->type == kWnMsg_AdviceChanged && OnAdviceChanged() != 0;
}

// Build the advisor's portrait, personal name, topic list and category selector and subscribe its message-target view.
// The borrowed advisor identity and scoped resource strings retain the original construction and callback order.
u8 UiAdvisorMain::OnInit(WnWindow *window) {
  if (!window)
    return 0;
  if (UiGetMessageServer())
    UiGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_AdviceChanged);
  window->SetSize(384, 354);
  if (UiGetPreferences()->AdvMgrPrefs()->ready) {
    ApPreferences *top_preferences = UiGetPreferences(), *left_preferences = UiGetPreferences();
    u32 top = top_preferences->AdvMgrPrefs()->main_y;
    u32 left = left_preferences->AdvMgrPrefs()->main_x;
    window->GZWinMoveTo((i32)left, (i32)top);
  } else
    window->GZWinMoveTo(8, 8);
  ScString caption, personal_name("Christine McGavran");
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  AdAdvisor *advisor = nullptr;
  if (UiFindAdvisor(current_advisor, &advisor)) {
    advisor->GetName(&caption);
    advisor->GetPersonalName(&personal_name);
  } else
    caption.Sprintf("Advisor 0x%08lX %ld", (unsigned long)current_advisor, (long)(i32)current_advisor);
  window->SetCaption(caption);
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    ScRect rectangle{261, 180, 380, 346};
    generic->AddPanel(kUiAdvisorMainPanelId, 0, 15, &rectangle);
    generic->Release();
  }
  GetTopicHeaders(advisor);
  RsKey fallback{kRsImageResourceType, kRsAdvisorPortraitGroup, kUiDefaultAdvisorPortraitId};
  RsKey *key = &fallback;
  i32 frames = 5, picture_bottom = 0;
  if (advisor) {
    u32 identifier = advisor->GetAdvisorID();
    if (!UiFindAdvisorPortrait(identifier, advisor_mood, false, &fallback, &frames))
      key = advisor->GetLargeAdvisorPicture();
    if (!key || key->instance == kUiDebugUninitializedPortraitInstance)
      key = &fallback;
  }
  UiAdvisorButtonInterface *portrait = nullptr;
  if (UiCreateAdvisorPicture(kUiAdvisorMainPortraitId, 2, frames, *key, &portrait)) {
    WnWindow *picture_window = portrait->AsIGZWin();
    if (window->ChildAdd(picture_window)) {
      u32 white = picture_window->MakeFillColor(255, 255, 255);
      u32 gray = picture_window->MakeFillColor(64, 64, 64);
      portrait->SetOutlineColors(gray, white);
      i32 width = picture_window->GetW();
      picture_window->GZWinMoveTo((119 - width) / 2 + 261, 187);
      picture_bottom = picture_window->GetB();
    } else
      window->GetWindowManager()->DestroyWindow(picture_window);
    picture_window->Release();
  }
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(kUiAdvisorMainNameId, personal_name);
  if (label) {
    label->SetFont(font);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    u32 language = UiGetLanguageManager()->GetCurrentLanguage();
    if (language == kScLanguageJapanese || (language >= kScLanguageSimplifiedChinese && language <= kScLanguageKorean))
      label->GetWindow()->SetSize(106, 40);
    else
      label->Fit();
    if (window->ChildAdd(label->GetWindow())) {
      WnWindow *height_window = label->GetWindow(), *position_window = label->GetWindow();
      i32 height = height_window->GetH();
      position_window->GZWinMoveTo(273, picture_bottom + (346 - picture_bottom - height) / 2);
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  i32 label_right = 0;
  UiTextRef heading(kRsGameDiscussText, kRsGameTextGroup);
  if (heading.IsResolved())
    heading.CopyTo(&caption);
  else
    caption.FromChar("", 0);
  label = UiGetWindowResources()->CreateLabel(kUiAdvisorMainCaptionId, caption);
  if (label) {
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->Fit();
    if (window->ChildAdd(label->GetWindow())) {
      label->GetWindow()->GZWinMoveTo(7, 44);
      label_right = label->GetWindow()->GetR();
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  ApListViewControl *list = nullptr;
  if (UiGetCom()->GetClassObject(kApListViewControlClassId, IID_ApListViewControl, reinterpret_cast<void **>(&list))) {
    WnWindow *list_window = list->AsIGZWin();
    list_window->SetID(kUiAdvisorTopicListId);
    WnResources *resources = UiGetWindowResources();
    u32 color = resources->GetColor(kWnColorUiBackground);
    list_window->SetFillColor(color);
    u32 language = UiGetLanguageManager()->GetCurrentLanguage();
    if (language == kScLanguageJapanese ||
        (language >= kScLanguageSimplifiedChinese && language <= kScLanguageKorean)) {
      i32 first_height = font->Height(0), rows = 279 / first_height;
      i32 height = font->Height(0) * rows;
      list_window->SetArea(7, 67, 255, height + 69);
    } else
      list_window->SetArea(7, 67, 255, 346);
    list->SetFont(font);
    list->SetListViewFlag(4, true);
    list->SetListViewFlag(8, true);
    list->SetListViewFlag(32, true);
    list->AddColumn(0, 16, nullptr);
    if (window->ChildAdd(list_window))
      FillTopicListView(list, current_topic);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(list->AsIGZWin());
    }
    list->Release();
  }
  generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    ScRect rectangle{label_right + 3, 43, 256, 60};
    generic->AddPanel(kUiAdvisorCategoryPanelId, 0, 32, &rectangle);
    generic->Release();
  }
  WnComboControl *combo = nullptr;
  if (UiGetCom()->GetClassObject(kWnComboClassId, IID_WnComboControl, reinterpret_cast<void **>(&combo))) {
    UiTextRef advice_text(kRsGameNewsTopicsText, kRsGameTextGroup),
        briefing_text(kRsGameBriefingsText, kRsGameTextGroup), ordinance_text(kRsGameOrdinancesText, kRsGameTextGroup);
    WnWindow *combo_window = combo->GetWindow();
    combo_window->SetID(kUiAdvisorCategoryComboId);
    combo_window->SetArea(label_right + 4, 44, 255, 59);
    u32 blue = combo->GetWindow()->MakeFillColor(0, 0, 255);
    combo->SetHighlightColor(blue);
    combo->SetFont(font);
    if (advice_text.IsResolved())
      advice_text.CopyTo(&caption);
    else
      caption.FromChar("", 0);
    combo->InsertString(caption, -2);
    if (briefing_text.IsResolved())
      briefing_text.CopyTo(&caption);
    else
      caption.FromChar("", 0);
    combo->InsertString(caption, -2);
    if (ordinance_text.IsResolved())
      ordinance_text.CopyTo(&caption);
    else
      caption.FromChar("", 0);
    combo->InsertString(caption, -2);
    if (window->ChildAdd(combo_window)) {
      if (current_topic >= 1 && current_topic <= 3)
        combo->SetSelection((i32)current_topic - 1, true);
      combo->SetEditable(0);
    } else
      window->GetWindowManager()->DestroyWindow(combo_window);
    combo->Release();
  }
  window_exists = 1;
  return 1;
}

// Route button commands and topic selection to the original modal or modeless advisor messages.
// Selector changes rebuild the list from the retained category headers, independently of the reported command ID.
bool UiAdvisorMain::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if ((u32)first == kWnCommandButtonActivated)
      return OnButtonClicked(window, (u32)second) != 0;
    if ((u32)first != 11)
      return false;
    i32 selected = -1;
    WnWindow *child = window->GetChildWindowFromID(kUiAdvisorCategoryComboId);
    if (child) {
      WnComboControl *combo = nullptr;
      if (child->QueryInterface(IID_WnComboControl, reinterpret_cast<void **>(&combo))) {
        selected = combo->GetSelection();
        combo->Release();
      }
    }
    ApListViewControl *list = nullptr;
    if (window->GetChildAs(kUiAdvisorTopicListId, IID_ApListViewControl, reinterpret_cast<void **>(&list))) {
      if (selected >= 0 && selected <= 2)
        FillTopicListView(list, (u32)selected + 1);
      list->Release();
    }
    return true;
  }
  if (message == kWnMsg_ListViewDoubleClick) {
    if ((u32)first == kUiAdvisorTopicListId) {
      u32 advisor = current_advisor;
      bool modal = window->GetFlag(kWnFlagModal);
      if (current_topic >= 1 && current_topic <= 3) {
        u32 type;
        if (modal)
          type = kWnMsg_ModalAdvisorOrdinanceList + current_topic;
        else
          type = current_topic == 1 ? kWnMsg_AdvisorTopic
              : current_topic == 2  ? kWnMsg_AdvisorBriefing
                                    : kWnMsg_AdvisorOrdinance;
        UiGetApplication()->GetBoxX()->Event(14, 0, 0, 0, 0);
        u32 value = (u32)second;
        if (!modal)
          value |= behavior_flags | kUiAdvisorModeless;
        ScMessage selected(type, advisor, value, 0);
        UiGetMessageServer()->MessageSend(selected);
      }
    }
    return true;
  }
  if (message == kWnMsg_Init)
    return window && OnInit(window) != 0;
  if (message == kWnMsg_Shutdown)
    return window && OnShutdown(window) != 0;
  return false;
}

// Refresh the visible advice headers while preserving the selected list item and the active topic page.
u8 UiAdvisorMain::OnAdviceChanged() {
  if (!UiGetWindowManager())
    return 0;
  WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorMainWindowId);
  if (!window)
    return 1;
  ApListViewControl *list = nullptr;
  if (!window->GetChildAs(kUiAdvisorTopicListId, IID_ApListViewControl, reinterpret_cast<void **>(&list)))
    return 1;
  i32 selected = list->GetSelectedItemID();
  WnWindow *list_window = list->AsIGZWin();
  list_window->HideWindow();
  if (advice)
    for (u32 i = 0; i < advice_count; ++i)
      list->RemoveItem((i32)advice[i]->GetID());
  KillTopicHeaders();
  AdAdvisor *advisor = nullptr;
  if (UiFindAdvisor(current_advisor, &advisor)) {
    GetTopicHeaders(advisor);
    FillTopicListView(list, current_topic ? current_topic : 1);
  }
  list->SetSelectedItem(selected);
  list_window->ShowWindow();
  list->Release();
  return 1;
}

// Handle the original close and back buttons with their distinct cues and modal versus synchronous message routing.
u8 UiAdvisorMain::OnButtonClicked(WnWindow *window, u32 identifier) {
  if (identifier == kUiFrameCloseButtonId) {
    UiGetApplication()->GetBoxX()->Event(11, 0, 0, 0, 0);
    return 0;
  }
  if (identifier != kUiFrameBackButtonId)
    return 0;
  UiGetApplication()->GetBoxX()->Event(16, 0, 0, 0, 0);
  if (window->GetFlag(kWnFlagModal))
    KillWin(window);
  else if (UiGetMessageServer()) {
    ScMessage selected(kWnMsg_MeetAdvisors, 0, 0, 0);
    UiGetMessageServer()->MessageSend(selected);
  }
  return 1;
}

UiAdvisorMessageBox::UiAdvisorMessageBox()
    : references(0), window_exists(0), message_text(nullptr), title_text(nullptr), anchor(nullptr), picture_left(true),
      picture{} {
}

UiAdvisorMessageBox::~UiAdvisorMessageBox() {
}

// Publish Unknown or the original callback identity only when an output pointer is provided.
bool UiAdvisorMessageBox::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiWindowCallback))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiAdvisorMessageBox::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Destroy the callback owner when its signed reference count is no longer positive.
u32 UiAdvisorMessageBox::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

u8 UiAdvisorMessageBox::WinExists() const {
  return window_exists;
}

// Create the generic message window with fixed modal presentation and its original shared window identifier.
u8 UiAdvisorMessageBox::CreateWin(WnWindow **out) {
  *out = nullptr;
  if (WinExists())
    return 0;
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return 0;
  window->SetStyle(kUiGenericDraggable, 0);
  window->SetStyle(kUiGenericResizable, 0);
  window->SetStyle(kUiGenericScroller, 0);
  window->SetStyle(kUiGenericBackButton, 0);
  window->SetStyle(kUiGenericSecondButton, 0);
  window->SetStyle(kUiGenericNoFrame, 1);
  window->GetWindow()->PrivateBuffer(true);
  window->SetCallback(this);
  *out = window->GetWindow();
  (*out)->SetID(kUiAdvisorOrdinanceWindowId);
  window_exists = 1;
  return 1;
}

// Notify the modeless parent and clear the existence byte, as documented by Linux cSC3AdvMsgBox::KillWin.
u8 UiAdvisorMessageBox::KillWin(WnWindow *window) {
  if (!window)
    return 0;
  WnWindowManager *manager = window->GetWindowManager();
  WnWindow *parent = window->GetParentWin();
  manager->PostMessageToTarget(parent, kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
  window_exists = 0;
  return 1;
}

u8 UiAdvisorMessageBox::SetMessageString(ScStringView *text) {
  message_text = text;
  return 1;
}

u8 UiAdvisorMessageBox::SetTitleString(ScStringView *text) {
  title_text = text;
  return 1;
}

// Copy the picture's resource key and preserve its separate frame-count argument.
u8 UiAdvisorMessageBox::SetPicKey(const RsKey &key, i32 frames) {
  picture = key;
  picture_frames = frames;
  return 1;
}

// Borrow the supplied content, run the created window synchronously and retire its window view afterward.
u8 UiAdvisorMessageBox::DoAdvMsgBox(
    ScStringView *message, ScStringView *title, const RsKey &key, i32 frames, bool left, const ScRect *position) {
  WnWindowManager *manager = UiGetWindowManager();
  if (!manager)
    return 0;
  SetMessageString(message);
  SetTitleString(title);
  SetPicKey(key, frames);
  anchor = position;
  picture_left = left;
  WnWindow *window = nullptr;
  if (!CreateWin(&window))
    return 0;
  manager->DoModalWin(nullptr, window);
  manager->DestroyWindow(window);
  window->Release();
  return 1;
}

// Dispatch buttons and lifecycle messages with the original lifecycle null-window checks.
bool UiAdvisorMessageBox::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return (u32)first == kWnCommandButtonActivated && OnButtonClicked(window, (u32)second) != 0;
  if (message == kWnMsg_Init)
    return window && OnInit(window) != 0;
  if (message == kWnMsg_Shutdown)
    return window && OnShutdown(window) != 0;
  return false;
}

// Clear borrowed content and the picture key without retaining or releasing the caller's resources.
u8 UiAdvisorMessageBox::OnShutdown(WnWindow *) {
  picture = {};
  title_text = nullptr;
  message_text = nullptr;
  window_exists = 0;
  return 1;
}

u8 UiAdvisorMessageBox::OnButtonClicked(WnWindow *window, u32 identifier) {
  if (!window)
    return 0;
  UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  if (identifier != kUiFrameCloseButtonId && identifier != kUiAdvisorOrdinanceAcceptId)
    return 0;
  window->GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), identifier);
  return 1;
}

// Build the fixed-size advisor message window, its portrait, centered close button and read-only message editor.
// Title/message strings and the optional centering rectangle remain borrowed from the synchronous caller.
u8 UiAdvisorMessageBox::OnInit(WnWindow *window) {
  if (!window || !UiGetWindowResources())
    return 0;
  window->SetSize(337, 254);
  if (anchor) {
    i32 width = anchor->right - anchor->left, height = window->GetH();
    i32 available_height = anchor->bottom - anchor->top;
    i32 top = anchor->top + (available_height - height) / 2;
    i32 window_width = window->GetW();
    window->GZWinMoveTo(anchor->left + (width - window_width) / 2, top);
  } else {
    WnWindow *height_root = window->GetMainWindow(), *width_root = window->GetMainWindow();
    i32 height = height_root->GetH(), window_height = window->GetH();
    i32 top = (height - window_height) / 2, width = width_root->GetW(), window_width = window->GetW();
    window->GZWinMoveTo((width - window_width) / 2, top);
  }
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  ScString text;
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    ScRect rectangle{1, 1, 12, 253};
    generic->AddPanel(kUiAdvisorOrdinanceRightBorderId, 0, 4, &rectangle);
    rectangle = {325, 1, 336, 253};
    generic->AddPanel(kUiAdvisorOrdinanceBottomBorderId, 0, 1, &rectangle);
    rectangle = {11, 1, 326, 12};
    generic->AddPanel(kUiAdvisorOrdinanceLeftBorderId, 0, 8, &rectangle);
    rectangle = {11, 242, 326, 253};
    generic->AddPanel(kUiAdvisorOrdinanceTopBorderId, 0, 2, &rectangle);
    generic->Release();
  }
  if (title_text)
    text = ScString(*title_text);
  else
    text.FromChar("", 0);
  UiTextRef::FormatForEllipsis(&text, 290, font);
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(kUiAdvisorOrdinanceCaptionId, text);
  if (label) {
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->Fit();
    if (window->ChildAdd(label->GetWindow()))
      label->GetWindow()->GZWinMoveTo(17, 18);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  i32 picture_width = 0;
  UiAdvisorButtonInterface *portrait = nullptr;
  if (UiCreateAdvisorPicture(kUiAdvisorOrdinancePortraitId, 2, picture_frames, picture, &portrait)) {
    WnWindow *picture_window = portrait->AsIGZWin();
    if (window->ChildAdd(picture_window)) {
      picture_width = picture_window->GetW();
      picture_window->GZWinMoveTo(picture_left ? 17 : 319 - picture_width, 37);
    } else
      window->GetWindowManager()->DestroyWindow(picture_window);
    picture_window->Release();
  }
  ScRect footer{};
  [[maybe_unused]] bool footer_ready = false;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    footer = picture_left ? ScRect{17, 187, picture_width + 17, 236} : ScRect{319 - picture_width, 187, 319, 236};
    generic->AddPanel(kUiAdvisorOrdinanceFooterId, 0, 15, &footer);
    generic->Release();
    footer_ready = true;
  }
  ScRect button_area{0, 0, 42, 24};
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *buffer = image.GetBuffer();
  if (buffer)
    buffer->GetBufferArea(&button_area);
  text.FromChar("", 0);
  WnButton *button =
      UiGetWindowResources()->CreateFixedButton(kUiAdvisorOrdinanceAcceptId, text, buffer, &button_area, 0);
  if (button) {
    WnWindow *button_window = button->GetWindow();
    if (!buffer) {
      u32 color = UiGetWindowResources()->GetColor(kWnColorSilver);
      button_window->SetFillColor(color);
      button_window->SetSize(42, 24);
      button->SetButtonFlag(kWnButtonAutoFit, false);
    }
    button->SetButtonFlag(kWnButtonShowText, false);
    if (window->ChildAdd(button_window)) {
      assert(footer_ready);
      i32 width = footer.right - footer.left, height = button_window->GetH();
      i32 top = footer.top + (footer.bottom - footer.top - height) / 2;
      i32 button_width = button_window->GetW();
      button_window->GZWinMoveTo(footer.left + (width - button_width) / 2, top);
    } else
      window->GetWindowManager()->DestroyWindow(button_window);
    button->Release();
  }
  ScRect body = picture_left ? ScRect{picture_width + 23, 37, 319, 236} : ScRect{17, 37, 313 - picture_width, 236};
  WnFlatControl *panel = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel))) {
    panel->GetWindow()->SetID(kUiAdvisorOrdinanceBodyPanelId);
    WnResources *resources = UiGetWindowResources();
    WnWindow *panel_window = panel->GetWindow();
    u32 color = resources->GetColor(kWnColorUiBackground);
    panel_window->SetFillColor(color);
    panel->SetOutlineFlags(32);
    panel->GetWindow()->SetArea(body);
    if (window->ChildAdd(panel->GetWindow())) {
      panel->GetWindow()->PullToFront();
      panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(panel->GetWindow());
    }
    panel->Release();
    panel = nullptr;
  }
  if (message_text)
    text = ScString(*message_text);
  else
    text.FromChar("", 0);
  WnEditorControl *editor = nullptr;
  UiGetCom()->GetClassObject(kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor));
  if (editor) {
    editor->AsIGZWin()->SetID(kUiAdvisorOrdinanceBodyId);
    editor->SetFont(font);
    editor->SetTextGutterX(12);
    editor->SetTextGutterY(6);
    editor->AsIGZWin()->SetSize(body.right - body.left, body.bottom - body.top);
    editor->SetFlag(2);
    editor->ClearFlag(1);
    editor->ClearFlag(16);
    editor->ClearFlag(32);
    editor->SetText(text);
    if (window->ChildAdd(editor->AsIGZWin()))
      editor->AsIGZWin()->SetArea(body);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(editor->AsIGZWin());
    }
    editor->Release();
  }
  window_exists = 1;
  return 1;
}

// Find the first matching advisor in the city's retained enumeration and return its borrowed identity.
// Release every enumerated advisor before deleting the pointer array, including the selected advisor.
u8 UiFindAdvisor(u32 identifier, AdAdvisor **out) {
  *out = nullptr;
  CtCity *city = UiGetCity();
  if (!city)
    return 0;
  u8 found = 0;
  u32 count = city->GetAdvisorListLength();
  if (count) {
    AdAdvisor **advisors = new AdAdvisor *[count];
    if (city->GetAdvisorList(count, advisors)) {
      for (u32 i = 0; i < count; ++i) {
        if (advisors[i]->GetAdvisorID() == identifier) {
          *out = advisors[i];
          found = 1;
          break;
        }
      }
      for (u32 i = 0; i < count; ++i) {
        advisors[i]->Release();
        advisors[i] = nullptr;
      }
    }
    delete[] advisors;
  }
  return found;
}

u32 UiAdvisorPictureFrames(u32 instance) {
  return ((~instance & kAdPortraitSmallFlag) | kAdPortraitModeBias) >> 22;
}

// Select the advisor portrait by language family, mood and size, falling back to default language and mood three.
u8 UiFindAdvisorPortrait(u32 advisor, u32 mood, bool small, RsKey *out, i32 *frames) {
  u32 ordinal;
  switch (advisor) {
  case kAdUtilitiesAdvisorId:
    ordinal = 1;
    break;
  case kAdBudgetAdvisorId:
    ordinal = 2;
    break;
  case kAdCityPlanningAdvisorId:
    ordinal = 3;
    break;
  case kAdEnvironmentAdvisorId:
    ordinal = 4;
    break;
  case kAdPublicSafetyAdvisorId:
    ordinal = 5;
    break;
  case kAdTransportationAdvisorId:
    ordinal = 6;
    break;
  case kAdDemographicsAdvisorId:
    ordinal = 7;
    break;
  default:
    return 0;
  }
  if (!mood)
    mood = 3;
  u32 language = UiGetLanguageManager()->GetCurrentLanguage(), family = 0;
  if ((language >= kScLanguageFrench && language <= kScLanguagePortuguese) || language == kScLanguagePolish)
    family = 1;
  else if (language == kScLanguageJapanese ||
      (language >= kScLanguageSimplifiedChinese && language <= kScLanguageKorean))
    family = 2;
  u32 size = small ? kAdPortraitSmallFlag : 0;
  *out = {kRsImageResourceType, kRsAdvisorPortraitGroup, mood + size + ((ordinal + (family << 8)) << 8)};
  if (!UiGetResourceManager()->TestForKey(out)) {
    u32 base = size + (ordinal << 8);
    out->instance = base + mood;
    if (!UiGetResourceManager()->TestForKey(out)) {
      out->instance = base + 3;
      if (!UiGetResourceManager()->TestForKey(out))
        return 0;
    }
  }
  *frames = (i32)UiAdvisorPictureFrames(out->instance);
  return 1;
}

// Create the real advisor-picture control and give it the scoped image resource or the original null-image fallback.
u8 UiCreateAdvisorPicture(u32 identifier, i32 type, i32 frames, const RsKey &key, UiAdvisorButtonInterface **out) {
  if (!UiGetCom()->GetClassObject(
          kUiAdvisorButtonClassId, IID_UiAdvisorButtonInterface, reinterpret_cast<void **>(out)))
    return 0;
  (*out)->AsIGZWin()->SetID(identifier);
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(key.instance, key.group, GxVramRequest::SystemMemory, format);
  GxBuffer *buffer = image.GetBuffer();
  (*out)->SetAdvImage(type, buffer, nullptr, frames);
  return 1;
}

UiAdvisorTopic::UiAdvisorTopic()
    : references(0), topic_type(0), window_exists(0), current_advisor(0), current_topic(0),
      parent_no_back_button(false), end_game(false) {
}

UiAdvisorTopic::~UiAdvisorTopic() {
}

// Publish Unknown or the window-callback identity only when an output pointer is supplied.
bool UiAdvisorTopic::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiWindowCallback))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiAdvisorTopic::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Destroy the callback owner when its signed reference count is no longer positive.
u32 UiAdvisorTopic::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

u8 UiAdvisorTopic::WinExists() const {
  return window_exists;
}

// Configure the generic topic window and its modal and end-game presentation before publishing it.
u8 UiAdvisorTopic::CreateWin(WnWindow **out, bool modal) {
  *out = nullptr;
  if (WinExists())
    return 0;
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return 0;
  window->SetStyle(kUiGenericDraggable, 1);
  window->SetStyle(kUiGenericResizable, 0);
  window->SetStyle(kUiGenericScroller, 0);
  window->SetStyle(kUiGenericBackButton, !modal);
  window->SetStyle(kUiGenericSecondButton, 0);
  if (end_game)
    window->SetStyle(kUiGenericCloseButton, 0);
  window->GetWindow()->PrivateBuffer(true);
  window->SetCallback(this);
  *out = window->GetWindow();
  (*out)->SetID(kUiAdvisorTopicWindowId);
  window_exists = 1;
  return 1;
}

// Close a modal topic with its selected result or notify the parent of a modeless topic.
u8 UiAdvisorTopic::KillWin(WnWindow *window, u32 result) {
  if (!window)
    return 0;
  if (window->GetFlag(kWnFlagModal))
    UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), result);
  else {
    WnWindowManager *manager = window->GetWindowManager();
    WnWindow *parent = window->GetParentWin();
    manager->PostMessageToTarget(parent, kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
  }
  window_exists = 0;
  return 1;
}

u8 UiAdvisorTopic::GetWinArea(ScRect *out) const {
  *out = area;
  return 1;
}

u8 UiAdvisorTopic::SetCurrentAdvisorID(u32 identifier) {
  current_advisor = identifier;
  return 1;
}

u8 UiAdvisorTopic::SetCurrentTopicID(u32 identifier) {
  current_topic = identifier;
  return 1;
}

u8 UiAdvisorTopic::SetTopicType(u32 type) {
  topic_type = type;
  return 1;
}

u8 UiAdvisorTopic::SetEndGame(bool enabled) {
  end_game = enabled;
  return 1;
}

bool UiAdvisorTopic::SetParentNoBackButton(bool enabled) {
  parent_no_back_button = enabled;
  return enabled;
}

// Route button and lifecycle messages, and answer both end-game presentation predicates.
bool UiAdvisorTopic::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return (u32)first == kWnCommandButtonActivated && OnButtonClicked(window, (u32)second) != 0;
  if (message == kWnMsg_KeyDown || message == kWnMsg_KeyUp)
    return end_game;
  if (message == kWnMsg_Init)
    return OnInit(window) != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown(window) != 0;
  return false;
}

// Save the topic window position and clear its open-window state.
u8 UiAdvisorTopic::OnShutdown(WnWindow *window) {
  ApPreferences *preferences = UiGetPreferences();
  u32 left = (u32)window->GetL();
  preferences->AdvMgrPrefs()->topic_x = left;
  preferences = UiGetPreferences();
  u32 top = (u32)window->GetT();
  preferences->AdvMgrPrefs()->topic_y = top;
  UiGetPreferences()->AdvMgrPrefs()->ready = 1;
  window_exists = 0;
  return 1;
}

// Route topic actions to their original sounds, modal results and advisor-navigation messages.
u8 UiAdvisorTopic::OnButtonClicked(WnWindow *window, u32 identifier) {
  if (identifier == kUiFrameCloseButtonId) {
    UiGetApplication()->GetBoxX()->Event(11, 0, 0, 0, 0);
    return 0;
  }
  if (identifier == kUiFrameBackButtonId) {
    UiGetApplication()->GetBoxX()->Event(17, 0, 0, 0, 0);
    u32 advisor = current_advisor;
    if (window->GetFlag(kWnFlagModal)) {
      KillWin(window, 0);
      return 1;
    }
    if (topic_type >= 1 && topic_type <= 3) {
      ScMessage selected(
          kWnMsg_AdvisorDialog, advisor, topic_type | (parent_no_back_button ? 0 : kUiAdvisorModeless), 0);
      UiGetMessageServer()->MessageSend(selected);
    }
    return 1;
  }
  if (identifier == kUiAdvisorTopicBackId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    if (!window->GetFlag(kWnFlagModal)) {
      ScMessage selected(kWnMsg_OrdinanceDialog, 0, 0, 0);
      UiGetMessageServer()->MessagePost(selected, false);
    }
    return 1;
  }
  if (identifier >= kUiAdvisorTopicFirstChoiceId && identifier <= kUiAdvisorTopicThirdChoiceId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    KillWin(window, identifier - kUiAdvisorTopicChoiceIdBase);
    return 1;
  }
  return 0;
}

// Build the selected advisor topic, portrait, text and optional illustration with the original control lifetimes.
// End-game topics expose three result choices; ordinary kind-three topics expose their original help/back controls.
u8 UiAdvisorTopic::OnInit(WnWindow *window) {
  if (!window || !UiGetWindowResources())
    return 0;
  AdAdvisor *advisor = nullptr;
  ScString title, personal_name;
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  if (UiFindAdvisor(current_advisor, &advisor)) {
    switch (topic_type) {
    case 1:
      advisor->GetNameAdvice(&title);
      break;
    case 2:
      advisor->GetNameBriefings(&title);
      break;
    case 3:
      advisor->GetNameOrdinance(&title);
      break;
    }
    advisor->GetPersonalName(&personal_name);
  }
  window->SetCaption(title);
  window->SetSize(384, 354);
  if (UiGetPreferences()->AdvMgrPrefs()->ready) {
    ApPreferences *top_preferences = UiGetPreferences(), *left_preferences = UiGetPreferences();
    u32 top = top_preferences->AdvMgrPrefs()->topic_y;
    u32 left = left_preferences->AdvMgrPrefs()->topic_x;
    window->GZWinMoveTo((i32)left, (i32)top);
  } else
    window->GZWinMoveTo(8, 8);
  UiGenericInterface *generic = nullptr;
  ScRect illustration_bounds{};
  [[maybe_unused]] bool illustration_bounds_ready = false;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    illustration_bounds = {261, 180, 380, 346};
    illustration_bounds_ready = true;
    generic->AddPanel(kUiAdvisorTopicPicturePanelId, 0, 15, &illustration_bounds);
    if (end_game) {
      illustration_bounds = {7, 237, 45, 346};
      generic->AddPanel(kUiAdvisorTopicAlternatePicturePanelId, 0, 15, &illustration_bounds);
    }
    window->Release();
  }
  AdMessageRecord *advice = nullptr;
  ScString heading, body;
  u8 mood = 0;
  RsKey picture{};
  if (advisor) {
    switch (topic_type) {
    case 1:
      if (advisor->GetAdviceStructureFromID(current_topic, &advice) && advice) {
        advice->CopyTitle(&heading);
        advice->CopyMessage(&body);
        mood = (u8)advice->GetMood();
        picture = *advice->GetPicture();
        advice->Release();
        advice = nullptr;
      }
      break;
    case 2:
      // Keep the installed112/116 slots even though this topic uses the briefing caption.
      advisor->GetOrdinanceTitleFromID(current_topic, &heading);
      advisor->GetOrdinanceMessageFromID(current_topic, &body);
      break;
    case 3:
      advisor->GetHelpTitleFromID(current_topic, &heading);
      advisor->GetHelpMessageFromID(current_topic, &body);
      break;
    }
  }
  WnFlatControl *body_panel = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&body_panel))) {
    body_panel->GetWindow()->SetID(kUiAdvisorTopicBodyPanelId);
    WnResources *resources = UiGetWindowResources();
    WnWindow *surface = body_panel->GetWindow();
    u32 color = resources->GetColor(kWnColorUiBackground);
    surface->SetFillColor(color);
    body_panel->SetOutlineFlags(32);
    body_panel->GetWindow()->SetArea(7, 39, 255, end_game ? 222 : 346);
    WnWindow *child = body_panel->GetWindow();
    if (window->ChildAdd(child)) {
      body_panel->GetWindow()->SendToBack();
      body_panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(body_panel->GetWindow());
    }
    body_panel->Release();
  }
  u32 portrait_bottom = 0;
  UiAdvisorButtonInterface *portrait = nullptr;
  RsKey default_portrait{kRsImageResourceType, kRsAdvisorPortraitGroup, kUiDefaultAdvisorPortraitId};
  const RsKey *portrait_key = &default_portrait;
  i32 frames = 5;
  if (advisor) {
    u32 portrait_mood = topic_type == 1 ? mood : 3;
    u32 advisor_id = advisor->GetAdvisorID();
    if (!UiFindAdvisorPortrait(advisor_id, portrait_mood, false, &default_portrait, &frames))
      portrait_key = advisor->GetLargeAdvisorPicture();
    if (!portrait_key)
      portrait_key = &default_portrait;
  }
  if (UiCreateAdvisorPicture(kUiAdvisorTopicPortraitId, 2, frames, *portrait_key, &portrait)) {
    WnWindow *child = portrait->AsIGZWin();
    if (window->ChildAdd(child)) {
      u32 white = child->MakeFillColor(255, 255, 255);
      u32 gray = child->MakeFillColor(64, 64, 64);
      portrait->SetOutlineColors(gray, white);
      i32 width = child->GetW();
      child->GZWinMoveTo((119 - width) / 2 + 261, 187);
      portrait_bottom = (u32)child->GetB();
    } else
      window->GetWindowManager()->DestroyWindow(child);
    child->Release();
  }
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(kUiAdvisorTopicNameId, personal_name);
  if (label) {
    label->SetFont(font);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    u32 language = UiGetLanguageManager()->GetCurrentLanguage();
    if (language == kScLanguageJapanese || (language >= kScLanguageSimplifiedChinese && language <= kScLanguageKorean))
      label->GetWindow()->SetSize(106, 40);
    WnWindow *child = label->GetWindow();
    if (window->ChildAdd(child)) {
      WnWindow *height_source = label->GetWindow(), *positioned = label->GetWindow();
      i32 height = height_source->GetH();
      i32 top = (i32)(portrait_bottom + (u32)((i32)(346u - portrait_bottom - (u32)height) / 2));
      positioned->GZWinMoveTo(273, top);
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  label = UiGetWindowResources()->CreateLabel(kUiAdvisorTopicHeadingId, heading);
  if (label) {
    label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiMain));
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->GetWindow()->SetSize(128, 8);
    WnWindow *child = label->GetWindow();
    if (window->ChildAdd(child))
      label->GetWindow()->SetArea(11, 43, 251, 74);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  WnEditorControl *editor = nullptr;
  UiGetCom()->GetClassObject(kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor));
  if (editor) {
    editor->AsIGZWin()->SetID(kUiAdvisorTopicBodyId);
    editor->SetFont(font);
    editor->SetTextGutterX(4);
    editor->AsIGZWin()->SetSize(245, end_game ? 143 : 267);
    editor->SetFlag(2);
    editor->ClearFlag(1);
    editor->ClearFlag(16);
    editor->ClearFlag(32);
    editor->SetText(body);
    WnWindow *child = editor->AsIGZWin();
    if (window->ChildAdd(child))
      editor->AsIGZWin()->GZWinMoveTo(8, 78);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(editor->AsIGZWin());
    }
    editor->Release();
  }
  if (topic_type == 1 && (picture.instance || picture.group || picture.type)) {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(picture.instance, picture.group, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = image.GetBuffer();
    if (buffer) {
      buffer->AddRef();
      WnFlatControl *frame = nullptr;
      if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&frame))) {
        frame->GetWindow()->SetID(kUiAdvisorTopicIllustrationFrameId);
        WnResources *resources = UiGetWindowResources();
        WnWindow *surface = frame->GetWindow();
        u32 color = resources->GetColor(kWnColorUiBackground);
        surface->SetFillColor(color);
        frame->SetOutlineFlags(32);
        WnWindow *child = frame->GetWindow();
        if (window->ChildAdd(child)) {
          frame->GetWindow()->SendToBack();
          frame->GetWindow()->SetFlag(kWnFlagReorderable, false);
        } else {
          WnWindowManager *manager = window->GetWindowManager();
          manager->DestroyWindow(frame->GetWindow());
        }
      }
      WnBitmapControl *bitmap = nullptr;
      UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap));
      if (bitmap) {
        bitmap->GetWindow()->SetID(kUiAdvisorTopicIllustrationId);
        bitmap->SetImage(buffer);
        WnWindow *child = bitmap->GetWindow();
        if (window->ChildAdd(child)) {
          bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
          WnWindow *positioned = bitmap->GetWindow();
          u32 height = buffer->Height();
          u32 right = ((buffer->Width() + 119) >> 1) + 262;
          u32 left = ((119 - buffer->Width()) >> 1) + 262;
          positioned->SetArea((i32)left, 40, (i32)right, (i32)(height + 40));
          bitmap->GetWindow()->GetArea(&illustration_bounds);
          illustration_bounds_ready = true;
          --illustration_bounds.left;
          ++illustration_bounds.right;
          --illustration_bounds.top;
          ++illustration_bounds.bottom;
        } else {
          WnWindowManager *manager = window->GetWindowManager();
          manager->DestroyWindow(bitmap->GetWindow());
        }
        bitmap->Release();
      }
      buffer->Release();
      if (frame) {
        WnWindow *surface = frame->GetWindow();
        assert(illustration_bounds_ready);
        surface->SetArea(illustration_bounds);
        frame->Release();
      }
    }
  }
  if (topic_type == 3 && !window->GetFlag(kWnFlagModal)) {
    WnFlatControl *back_panel = nullptr;
    if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&back_panel))) {
      back_panel->GetWindow()->SetID(kUiAdvisorTopicBackPanelId);
      WnResources *resources = UiGetWindowResources();
      WnWindow *surface = back_panel->GetWindow();
      u32 color = resources->GetColor(kWnThemePanelFillColorId);
      surface->SetFillColor(color);
      back_panel->SetOutlineColor(UiGetWindowResources()->GetColor(kWnThemeBorderColorId));
      back_panel->SetOutlineFlags(15);
      WnWindow *child = back_panel->GetWindow();
      if (window->ChildAdd(child)) {
        back_panel->GetWindow()->SetArea(269, 76, 369, 142);
        back_panel->GetWindow()->SendToBack();
        back_panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
      } else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(back_panel->GetWindow());
      }
      back_panel->Release();
    }
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(9, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = image.GetBuffer();
    if (buffer) {
      WnButton *back;
      {
        ScString empty;
        WnResources *resources = UiGetWindowResources();
        const ScRect *source = buffer->GetBufferArea();
        back = resources->CreateFixedButton(kUiAdvisorTopicBackId, empty, buffer, source, 0);
      }
      assert(back != nullptr);
      back->SetButtonFlag(kWnButtonShowText, false);
      back->GetWindow()->SetFlag(kWnFlagReorderable, false);
      WnWindow *child = back->GetWindow();
      if (window->ChildAdd(child)) {
        WnWindow *height_source = back->GetWindow(), *width_source = back->GetWindow();
        WnWindow *positioned = back->GetWindow();
        i32 height = height_source->GetH(), width = width_source->GetW();
        positioned->GZWinMoveTo((100 - width) / 2 + 269, 138 - height);
      } else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(back->GetWindow());
      }
      back->Release();
    }
    UiTextRef back_title(kRsGameReviewOrdinancesText, kRsGameTextGroup);
    ScString text;
    if (back_title.IsResolved())
      back_title.CopyTo(&text);
    label = UiGetWindowResources()->CreateLabel(kUiAdvisorReviewOrdinancesLabelId, text);
    if (label) {
      label->SetFont(font);
      label->GetWindow()->SetSize(100, 66);
      label->SetBackground(false);
      label->SetAlignment(kWnLabelCenterTop);
      label->SetLabelFlag(kWnLabelWrapText, true);
      label->Fit();
      WnWindow *child = label->GetWindow();
      if (window->ChildAdd(child)) {
        WnWindow *width_source = label->GetWindow(), *positioned = label->GetWindow();
        i32 width = width_source->GetW();
        positioned->GZWinMoveTo((100 - width) / 2 + 269, 80);
      } else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(label->GetWindow());
      }
      label->Release();
    }
  }
  if (end_game) {
    ScRect source{0, 0, 26, 25};
    ScString text;
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(2, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = image.GetBuffer();
    if (buffer)
      buffer->GetBufferArea(&source);
    text.Erase(0, text.Strlen());
    for (u32 i = 0; i != 3; ++i) {
      WnButton *button =
          UiGetWindowResources()->CreateFixedButton(kUiAdvisorTopicFirstChoiceId + i, text, buffer, &source, 0);
      if (button) {
        WnWindow *child = button->GetWindow();
        if (!buffer) {
          u32 color = UiGetWindowResources()->GetColor(kWnColorSilver);
          child->SetFillColor(color);
          child->SetSize(26, 25);
          button->SetButtonFlag(kWnButtonAutoFit, false);
        }
        button->SetButtonFlag(kWnButtonShowText, false);
        if (window->ChildAdd(child))
          child->GZWinMoveTo(15, 248 + 30 * (i32)i);
        else
          window->GetWindowManager()->DestroyWindow(child);
        button->Release();
      }
    }
    for (u32 i = 0; i != 3; ++i) {
      {
        u32 instance = i == 0 ? 2 : i == 1 ? 1 : 0x2df;
        u32 group = i == 2 ? kRsGameTextGroup : kRsMenuTextGroup;
        UiTextRef title_text(instance, group);
        {
          ScString value = title_text.GetText();
          ScString first(value), second(first);
          text = second;
        }
      }
      label = UiGetWindowResources()->CreateLabel(kUiAdvisorMenuLabelIdBase + i, text);
      if (label) {
        label->SetFont(font);
        label->GetWindow()->SetSize(248, 30);
        label->SetBackground(false);
        label->SetLabelFlag(kWnLabelWrapText, false);
        label->Fit();
        WnWindow *child = label->GetWindow();
        if (window->ChildAdd(child))
          label->GetWindow()->GZWinMoveTo(50, 251 + 30 * (i32)i);
        else {
          WnWindowManager *manager = window->GetWindowManager();
          manager->DestroyWindow(label->GetWindow());
        }
        label->Release();
      }
    }
  }
  window_exists = 1;
  return 1;
}
