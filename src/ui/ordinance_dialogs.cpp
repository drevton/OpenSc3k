#include "ui/ordinance_dialogs.h"

#include <assert.h>

#include "advisors/ad_advice.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/thread.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "resources/resource.h"
#include "ui/advisor_dialogs.h"
#include "ui/api.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// The initial footer coordinate stands in for a footer that has not yet been positioned.
enum UiOrdinanceLayoutSentinel : i32 {
  kUiOrdinanceFooterUnsetTop = 0xf4240,
};

// Ordinance lists create the vertical scrollbar directly and select orientation from the requested scrollbar identity.
enum UiOrdinanceScrollbarId : u32 {
  kUiOrdinanceVerticalScrollbarId = 0x62cbafda,
  kUiOrdinanceHorizontalScrollbarId = 0x62cbafdb,
};

enum UiOrdinanceHeadingControlId : u32 {
  kUiOrdinanceHeadingId = 0x12345678,
  kUiOrdinanceHeadingPanelId = 0x12345679,
  kUiOrdinanceHelpButtonId = 0x1234567a,
};

// Start an empty ordinance list with its own sentinel and no associated advisor or row height.
UiAdvisorOrdinance::UiAdvisorOrdinance() : associated_advisor(0), font(nullptr), line_height(0) {
  lines = new UiOrdinanceNode;
  lines->next = lines;
  lines->previous = lines;
}

// Retire list nodes and its sentinel before destroying the window base. Ordinary shutdown owns the row objects.
UiAdvisorOrdinance::~UiAdvisorOrdinance() {
  UiOrdinanceNode *node = lines->next;
  while (node != lines) {
    UiOrdinanceNode *next = node->next;
    delete node;
    node = next;
  }
  delete lines;
}

// Publish the two secondary identities, sharing the root's reference count for every view.
bool UiAdvisorOrdinance::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnListItem && iid != IID_UiAdvisorOrdinanceControl)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  if (iid == IID_WnListItem)
    *out = static_cast<WnListItem *>(this);
  else
    *out = static_cast<UiAdvisorOrdinanceControl *>(this);
  AddRef();
  return true;
}

u32 UiAdvisorOrdinance::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiAdvisorOrdinance::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window belonging to the ordinance-specific view.
WnWindow *UiAdvisorOrdinance::AsIGZWin() {
  return this;
}

u8 UiAdvisorOrdinance::SetAssociatedAdvisorID(u32 advisor_id) {
  associated_advisor = advisor_id;
  return advisor_id != 0;
}

// Sum the costs of enabled ordinance checkboxes using the original 32-bit wraparound arithmetic.
i32 UiAdvisorOrdinance::GetValue() const {
  u32 total = 0;
  for (UiOrdinanceNode *node = lines->next; node != lines; node = node->next) {
    UiOrdinanceLine *line = node->line;
    if (line->checkbox && line->checkbox->GetButtonFlag(kWnButtonChecked))
      total += (u32)line->cost;
  }
  return (i32)total;
}

// Unsubscribe before deleting row controls, then run the original common-window shutdown.
bool UiAdvisorOrdinance::Shutdown() {
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->RemoveNotification(this, kCtMsg_OrdinanceAvailable);
    server->RemoveNotification(this, kCtMsg_OrdinanceCostsUpdated);
    server->RemoveNotification(this, kCtMsg_OrdinanceChanged);
  }
  RemoveAllOrdinanceLines();
  return UiBaseWindow::Shutdown();
}

bool UiAdvisorOrdinance::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiAdvisorOrdinance::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

bool UiAdvisorOrdinance::LayoutH(i32) {
  return false;
}

// Find the first checkbox whose primary window carries the requested identifier without retaining it.
WnButton *UiAdvisorOrdinance::FindOrdinanceCheckbox(u32 button_id) {
  for (UiOrdinanceNode *node = lines->next; node != lines; node = node->next) {
    WnButton *checkbox = node->line->checkbox;
    if (checkbox && checkbox->GetWindow()->GetID() == button_id)
      return checkbox;
  }
  return nullptr;
}

// Delete each row's label and checkbox windows before releasing the retained control views and list nodes.
u8 UiAdvisorOrdinance::RemoveAllOrdinanceLines() {
  while (lines->next != lines) {
    UiOrdinanceNode *node = lines->next;
    UiOrdinanceLine *line = node->line;
    if (line->label) {
      WnWindow *window;
      if (line->label->QueryInterface(IID_WnWindow, reinterpret_cast<void **>(&window))) {
        ChildDelete(window);
        window->Release();
      }
      line->label->Release();
      line->label = nullptr;
    }
    if (line->checkbox) {
      WnWindow *window = line->checkbox->GetWindow();
      ChildDelete(window);
      line->checkbox->Release();
      line->checkbox = nullptr;
    }
    node->previous->next = node->next;
    node->next->previous = node->previous;
    delete node;
    delete line;
  }
  return 1;
}

// Subscribe and build the advisor heading, help button and background before acquiring the live ordinance rows.
// The borrowed font remains in its original field; scoped strings and image references retire after common initialization.
bool UiAdvisorOrdinance::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  if (!associated_advisor)
    return false;
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->AddNotification(this, kCtMsg_OrdinanceAvailable);
    server->AddNotification(this, kCtMsg_OrdinanceCostsUpdated);
    server->AddNotification(this, kCtMsg_OrdinanceChanged);
  }
  ScString text;
  AdAdvisor *advisor = nullptr;
  bool initialized = false;
  if (UiFindAdvisor(associated_advisor, &advisor)) {
    advisor->GetName(&text);
    SetCaption(text);
    font = UiGetWindowResources()->GetFont(kWnFontDefault);
    if (font) {
      WnLabelControl *heading = UiGetWindowResources()->CreateLabel(kUiOrdinanceHeadingId, text);
      if (heading) {
        heading->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
        heading->SetLabelFlag(kWnLabelWrapText, false);
        heading->SetBackground(false);
        heading->Fit();
        WnWindow *window = heading->GetWindow();
        if (!ChildAdd(window)) {
          WnWindowManager *manager = GetWindowManager();
          manager->DestroyWindow(heading->GetWindow());
        }
        heading->Release();
      }
      ScString empty;
      GxColorType format(GxColorType::Color::RGB16);
      UiImageRef image(kRsHelpButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
      GxBuffer *buffer = image.GetBuffer();
      WnResources *resources = UiGetWindowResources();
      WnButton *help;
      if (buffer) {
        const ScRect *image_area = buffer->GetBufferArea();
        help = resources->CreateFixedButton(kUiOrdinanceHelpButtonId, empty, buffer, image_area, 0);
      } else {
        help = resources->CreateFixedButton(kUiOrdinanceHelpButtonId, empty, nullptr, nullptr, 0);
        assert(help != nullptr);
        help->GetWindow()->SetSize(26, 25);
      }
      assert(help != nullptr);
      WnWindow *help_window = help->GetWindow();
      help->SetButtonFlag(kWnButtonShowText, false);
      if (!ChildAdd(help_window)) {
        GetWindowManager()->DestroyWindow(help_window);
        help_window->Release();
        help_window = nullptr;
      }
      u32 fill = UiGetWindowResources()->GetColor(kWnThemePanelFillColorId);
      u32 outline = UiGetWindowResources()->GetColor(kWnThemeBorderColorId);
      i32 height = font->Height(0) + 8;
      if (help_window && help_window->GetH() + 2 > height)
        height = help_window->GetH() + 2;
      WnFlatControl *panel = nullptr;
      if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel))) {
        panel->GetWindow()->SetID(kUiOrdinanceHeadingPanelId);
        panel->GetWindow()->SetFillColor(fill);
        panel->SetOutlineColor(outline);
        panel->SetSizeMode(5);
        panel->SetOutlineFlags(15);
        panel->GetWindow()->SetArea(0, 0, 4, height);
        WnWindow *window = panel->GetWindow();
        if (ChildAdd(window)) {
          panel->GetWindow()->SendToBack();
          panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
        } else {
          WnWindowManager *manager = GetWindowManager();
          manager->DestroyWindow(panel->GetWindow());
        }
        panel->Release();
      }
      help->Release();
      RequeryOrdinanceLines();
      RequeryOrdinanceCosts();
      initialized = UiBaseWindow::Init();
    }
  }
  return initialized;
}

// Refresh displayed costs before measuring the title, header controls and widest ordinance row.
// Preserve the original label-view release after a successful primary-window query.
bool UiAdvisorOrdinance::GetMinMaxInfo(ScRect *limits) {
  *limits = {0, 0, 640, 480};
  WnWindow *window = GetChildWindowFromID(kUiOrdinanceHeadingPanelId);
  if (window)
    limits->top += window->GetH();
  window = GetChildWindowFromID(kUiOrdinanceHeadingId);
  if (window)
    limits->left += window->GetW() + 2;
  window = GetChildWindowFromID(kUiOrdinanceHelpButtonId);
  if (window)
    limits->left += window->GetW() + 2;
  RequeryOrdinanceCosts();
  for (UiOrdinanceNode *node = lines->next; node != lines; node = node->next) {
    UiOrdinanceLine *line = node->line;
    i32 width = 2;
    if (line->checkbox)
      width = line->checkbox->GetWindow()->GetW() + 2;
    if (line->label && line->label->QueryInterface(IID_WnWindow, reinterpret_cast<void **>(&window))) {
      width += window->GetW();
      line->label->Release();
    }
    if (limits->left < width)
      limits->left = width;
  }
  u32 count = 0;
  for (UiOrdinanceNode *node = lines->next; node != lines; node = node->next)
    ++count;
  limits->top = (i32)((u32)limits->top + (u32)line_height * count);
  return true;
}

// Lay out the header and vertically centered row controls, then size this window to the resulting list height.
bool UiAdvisorOrdinance::LayoutW(i32 width) {
  i32 header_height = 0, top = 0;
  WnWindow *window = GetChildWindowFromID(kUiOrdinanceHeadingPanelId);
  if (window) {
    header_height = window->GetH();
    top = window->GetH();
    i32 height = window->GetH();
    window->SetArea(0, 0, width, height);
  }
  window = GetChildWindowFromID(kUiOrdinanceHeadingId);
  if (window)
    window->GZWinMoveTo(2, 4);
  window = GetChildWindowFromID(kUiOrdinanceHelpButtonId);
  if (window) {
    i32 child_width = window->GetW();
    window->GZWinMoveTo(width - child_width - 1, 1);
  }
  u32 count = 0;
  for (UiOrdinanceNode *node = lines->next; node != lines; node = node->next)
    ++count;
  i32 height = (i32)(count * (u32)line_height + (u32)header_height);
  UiOrdinanceNode *node = lines->next;
  while (count) {
    UiOrdinanceLine *line = node->line;
    if (line->checkbox) {
      window = line->checkbox->GetWindow();
      i32 child_height = window->GetH();
      window->GZWinMoveTo(0, top + (line_height - child_height) / 2);
    }
    if (line->label) {
      window = line->label->GetWindow();
      i32 child_height = window->GetH();
      i32 child_top = top + (line_height - child_height) / 2;
      i32 child_width = window->GetW();
      window->GZWinMoveTo(width - child_width, child_top);
    }
    top += line_height;
    node = node->next;
    --count;
  }
  SetSize(width, height);
  return true;
}

// Queue layer notifications to this window and route its help and ordinance toggles through the original services.
// Other window messages retain the root handler's behavior and return value.
bool UiAdvisorOrdinance::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_Command) {
    switch (message->type) {
    case kWnMsg_RefreshOrdinances: {
      RequeryOrdinanceLines();
      ScMessage changed{kWnMsg_OrdinanceTotalChanged, 0, 0, 0};
      UiGetMessageServer()->MessagePost(changed, false);
      return true;
    }
    case kWnMsg_RefreshOrdinanceCosts:
      RequeryOrdinanceCosts();
      LayoutW(GetW());
      return true;
    case kWnMsg_UpdateOrdinanceCheckbox: {
      WnButton *checkbox = FindOrdinanceCheckbox((u32)message->data1);
      if (checkbox) {
        checkbox->SetButtonFlag(kWnButtonChecked, (u32)message->data2 != 0);
        ScMessage changed{kWnMsg_OrdinanceTotalChanged, 0, 0, 0};
        UiGetMessageServer()->MessagePost(changed, false);
      }
      return true;
    }
    case kCtMsg_OrdinanceAvailable:
      PostMsg(this, kWnMsg_RefreshOrdinances, 0, 0, 0);
      return true;
    case kCtMsg_OrdinanceCostsUpdated:
      PostMsg(this, kWnMsg_RefreshOrdinanceCosts, 0, 0, 0);
      return true;
    case kCtMsg_OrdinanceChanged:
      PostMsg(this, kWnMsg_UpdateOrdinanceCheckbox, (u32)message->data1, (u32)message->data2, 0);
      return true;
    default:
      return UiBaseWindow::DoMessage(message);
    }
  }
  if (UiGetMessageServer() && (u32)message->data1 == 1) {
    u32 button_id = (u32)message->data2;
    if (button_id == kUiOrdinanceHelpButtonId) {
      UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
      if (GetParentWin() && GetParentWin()->GetParentWin() && GetParentWin()->GetParentWin()->GetFlag(kWnFlagModal)) {
        ScMessage help{kWnMsg_ModalAdvisorOrdinanceList, associated_advisor, 3, 0};
        UiGetMessageServer()->MessageSend(help);
      } else {
        ScMessage help{kWnMsg_AdvisorDialog, associated_advisor, 3, 0};
        UiGetMessageServer()->MessagePost(help, false);
      }
      return true;
    }
    WnButton *checkbox = FindOrdinanceCheckbox(button_id);
    if (checkbox) {
      if (checkbox->IsOn())
        UiGetApplication()->GetBoxX()->Event(42, 0, 0, 0, 0);
      else
        UiGetApplication()->GetBoxX()->Event(43, 0, 0, 0, 0);
      CtCity *city = UiGetCity();
      if (city && city->OrdinanceLayer()) {
        CtOrdinanceLayer *layer = city->OrdinanceLayer();
        bool on = checkbox->GetButtonFlag(kWnButtonChecked);
        layer->SetOrdinance((u32)message->data2, on);
      }
      ScMessage changed{kWnMsg_OrdinanceTotalChanged, 0, 0, 0};
      UiGetMessageServer()->MessagePost(changed, false);
      RequeryOrdinanceCosts();
      return true;
    }
  }
  return false;
}

// Refresh the associated advisor's ordinance rows under the city's critical section and notify the parent afterward.
u8 UiAdvisorOrdinance::RequeryOrdinanceLines() {
  ScLock *lock = UiGetCity()->GetCriticalSection();
  lock->Lock();
  HideWindow();
  RemoveAllOrdinanceLines();
  ShowWindow();
  u8 result = 0;
  CtCity *city = UiGetCity();
  if (city) {
    CtOrdinanceLayer *layer = city->OrdinanceLayer();
    if (layer) {
      u32 count = layer->GetOridnanceListSize((i32)associated_advisor);
      if (count) {
        CtOrdinanceLayer::CtOrdinance **ordinances = new CtOrdinanceLayer::CtOrdinance *[count];
        for (u32 i = 0; i != count; ++i)
          ordinances[i] = new CtOrdinanceLayer::CtOrdinance{};
        if (layer->GetOrdinanceList(ordinances, associated_advisor) && (i32)count > 0)
          for (u32 i = 0; i != count; ++i)
            if (ordinances[i])
              AddOrdinance(ordinances[i]);
        for (u32 i = 0; i != count; ++i)
          delete ordinances[i];
        delete[] ordinances;
        u32 id = GetID();
        PostMsg(parent, kWnMsg_LayoutRequested, id, 0, 0);
      }
      result = 1;
    }
  }
  lock->Unlock();
  return result;
}

// Refresh each visible cost label from the ordinance layer and color enabled entries by signed cost.
// Keep the original repeated locale and message-server acquisitions and post the total-change notification last.
u8 UiAdvisorOrdinance::RequeryOrdinanceCosts() {
  if (!UiGetLanguageManager())
    return 0;
  if (!UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent))
    return 0;
  CtCity *city = UiGetCity();
  if (!city)
    return 0;
  CtOrdinanceLayer *layer = city->OrdinanceLayer();
  if (!layer)
    return 0;
  UiOrdinanceNode *node = lines->next;
  ScString amount;
  while (node != lines) {
    UiOrdinanceLine *line = node->line;
    if (line->label) {
      line->cost = (i32)layer->GetOrdinanceCost(line->identifier);
      UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString((i64)line->cost, &amount);
      assert(line->checkbox != nullptr);
      if (line->checkbox->IsOn()) {
        if (line->cost >= 0) {
          WnWindow *window = line->label->GetWindow();
          u32 color = window->MakeFillColor(0, 0, 0);
          line->label->SetTextColor(color);
        } else {
          WnWindow *window = line->label->GetWindow();
          u32 color = window->MakeFillColor(255, 0, 0);
          line->label->SetTextColor(color);
        }
      } else {
        WnWindow *window = line->label->GetWindow();
        u32 color = window->MakeFillColor(128, 128, 128);
        line->label->SetTextColor(color);
      }
      line->label->GetWindow()->SetCaption(amount);
      line->label->Fit();
    }
    node = node->next;
  }
  if (UiGetMessageServer()) {
    ScMessage message{kWnMsg_OrdinanceTotalChanged, 0, 0, 0};
    UiGetMessageServer()->MessagePost(message, false);
  }
  return 1;
}

// Create one retained checkbox/cost-label pair and append it in the ordinance layer's supplied order.
u8 UiAdvisorOrdinance::AddOrdinance(CtOrdinanceLayer::CtOrdinance *ordinance) {
  bool complete = true;
  ScString text;
  UiOrdinanceLine *line = new UiOrdinanceLine{};
  line->identifier = ordinance->id;
  line->cost = (i32)ordinance->monthly_cost;
  u8 on = ordinance->on;
  if (ordinance->name) {
    ScString copy(*ordinance->name);
    text = copy;
  } else
    text.Sprintf("Ordinance %ld\n", (long)(i32)ordinance->id);
  line->checkbox = UiGetWindowResources()->CreateCheckBox(line->identifier, text, nullptr, nullptr, 0);
  if (line->checkbox) {
    line->checkbox->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSystem));
    WnWindow *window = line->checkbox->GetWindow();
    window->SetID(line->identifier);
    window->SetCaption(text);
    line->checkbox->SetChecked(on != 0);
    if (!ChildAdd(window)) {
      GetWindowManager()->DestroyWindow(window);
      line->checkbox->Release();
      line->checkbox = nullptr;
      return 0;
    }
    line->checkbox->Fit();
    if (window->GetH() > line_height)
      line_height = window->GetH();
  } else
    complete = false;
  if (!UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString((i64)line->cost, &text))
    text.Erase(0, text.Strlen());
  line->label = UiGetWindowResources()->CreateLabel(0, text);
  if (line->label) {
    WnWindow *window = line->label->GetWindow();
    window->SetFlag(kWnFlagEnabled, false);
    line->label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
    assert(line->checkbox != nullptr);
    if (line->checkbox->IsOn()) {
      u32 color = line->cost >= 0 ? line->label->GetWindow()->MakeFillColor(0, 0, 0)
                                  : line->label->GetWindow()->MakeFillColor(255, 0, 0);
      line->label->SetTextColor(color);
    } else {
      u32 color = line->label->GetWindow()->MakeFillColor(128, 128, 128);
      line->label->SetTextColor(color);
    }
    line->label->SetLabelFlag(kWnLabelWrapText, false);
    line->label->SetBackground(false);
    line->label->Fit();
    if (window->GetH() > line_height)
      line_height = window->GetH();
    if (!ChildAdd(window)) {
      GetWindowManager()->DestroyWindow(window);
      line->label->Release();
      line->label = nullptr;
    }
  } else
    complete = false;
  UiOrdinanceNode *node = new UiOrdinanceNode{lines, lines->previous, line};
  lines->previous->next = node;
  lines->previous = node;
  return complete;
}

UiOrdinanceDialog::UiOrdinanceDialog() : references(0), window_exists(0), area{8, 8, 392, 362} {
}

UiOrdinanceDialog::~UiOrdinanceDialog() {
}

// Publish the callback identity and preserve the original callback-IID null-output rejection.
bool UiOrdinanceDialog::QueryInterface(u32 iid, void **out) {
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

u32 UiOrdinanceDialog::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Destroy the callback owner when its signed reference count is no longer positive.
u32 UiOrdinanceDialog::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

u8 UiOrdinanceDialog::WinExists() const {
  return window_exists;
}

u8 UiOrdinanceDialog::GetWinArea(ScRect *out) const {
  *out = area;
  return 1;
}

// Create the generic ordinance window with its fixed presentation styles and callback identity.
u8 UiOrdinanceDialog::CreateWin(WnWindow **out) {
  *out = nullptr;
  if (WinExists())
    return 0;
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return 0;
  window->SetStyle(kUiGenericDraggable, 1);
  window->SetStyle(kUiGenericResizable, 0);
  window->SetStyle(kUiGenericScroller, 0);
  window->SetStyle(kUiGenericBackButton, 0);
  window->SetStyle(kUiGenericSecondButton, 0);
  window->SetStyle(kUiGenericCloseButton, 0);
  window->GetWindow()->PrivateBuffer(true);
  window->SetCallback(this);
  *out = window->GetWindow();
  (*out)->SetID(kUiAdvisorOrdinanceWindowId);
  window_exists = 1;
  return 1;
}

// Close the modal ordinance window or notify its modeless parent, then clear the existence byte.
u8 UiOrdinanceDialog::KillWin(WnWindow *window) {
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

// Dispatch ordinance buttons, refresh, lifecycle and geometry notifications with the original pointer checks.
bool UiOrdinanceDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return (u32)first == kWnCommandButtonActivated && OnButtonClicked(window, (u32)second) != 0;
  if (message == kWnMsg_OrdinanceTotalChanged) {
    if (window)
      OnUpdateOrdinanceTotal(window);
    return true;
  }
  if (message == kWnMsg_Init)
    return window && OnInit(window) != 0;
  if (message == kWnMsg_Shutdown)
    return window && OnShutdown(window) != 0;
  if (message == kWnMsg_ClientConstraints) {
    if (window && first && second) {
      OnGetMinMaxInfo(window, reinterpret_cast<ScRect *>(first), reinterpret_cast<const ScRect *>(second));
      return true;
    }
  } else if (message == kWnMsg_ClientLayout && first && window) {
    OnUserAreaChanged(window, reinterpret_cast<const ScRect *>(first));
    return true;
  }
  return false;
}

// Save the ordinance position and full rectangle and unregister the window's total-change notification.
u8 UiOrdinanceDialog::OnShutdown(WnWindow *window) {
  ApPreferences *preferences = UiGetPreferences();
  u32 left = (u32)window->GetL();
  preferences->AdvMgrPrefs()->ordinance_x = left;
  preferences = UiGetPreferences();
  u32 top = (u32)window->GetT();
  preferences->AdvMgrPrefs()->ordinance_y = top;
  UiGetPreferences()->AdvMgrPrefs()->ready = 1;
  if (UiGetMessageServer())
    UiGetMessageServer()->RemoveNotification(window, kWnMsg_OrdinanceTotalChanged);
  window->GetArea(&area);
  window_exists = 0;
  return 1;
}

// Play the ordinance-close cue and close the window only for its original button identifier.
u8 UiOrdinanceDialog::OnButtonClicked(WnWindow *window, u32 identifier) {
  if (identifier != kUiAdvisorOrdinanceLeftBorderId)
    return 0;
  UiGetApplication()->GetBoxX()->Event(41, 0, 0, 0, 0);
  return KillWin(window);
}

struct UiOrdinanceTotal {
  i64 amount;
};

bool UiOrdinanceDialog::EnumOrdinanceControls(WnWindow *, u32, void *view, void *context) {
  UiAdvisorOrdinanceControl *control = static_cast<UiAdvisorOrdinanceControl *>(view);
  UiOrdinanceTotal *total = static_cast<UiOrdinanceTotal *>(context);
  total->amount = (i64)((u64)total->amount + (u64)(i64)control->GetValue());
  return true;
}

// Position ordinance headings, footer values, list area and close button from the supplied client rectangle.
// Repeated size getters remain separate because the original reads live child geometry between calls.
u8 UiOrdinanceDialog::OnUserAreaChanged(WnWindow *window, const ScRect *rectangle) {
  i32 value_left = 0, heading_bottom = 0, footer_top = kUiOrdinanceFooterUnsetTop;
  WnWindow *child = window->GetChildWindowFromID(kUiAdvisorOrdinanceCaptionId);
  if (child) {
    child->GZWinMoveTo(rectangle->left, rectangle->top);
    heading_bottom = child->GetB() + 1;
  }
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceBodyId);
  if (child) {
    i32 width = child->GetW();
    child->GZWinMoveTo(rectangle->right - width - 28, rectangle->top);
    if (heading_bottom <= child->GetB() + 1)
      heading_bottom = child->GetB() + 1;
  }
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceFooterId);
  if (child) {
    i32 height = child->GetH(), top = rectangle->bottom - height - 46;
    i32 width = child->GetW();
    child->GZWinMoveTo(rectangle->right - width - 28, top);
    value_left = child->GetL() - 4;
    if (child->GetT() < kUiOrdinanceFooterUnsetTop)
      footer_top = child->GetT();
  }
  child = window->GetChildWindowFromID(kUiAdvisorOrdinancePortraitId);
  if (child) {
    i32 height = child->GetH(), top = rectangle->bottom - height - 46;
    i32 width = child->GetW();
    child->GZWinMoveTo(value_left - width, top);
    if (child->GetT() < footer_top)
      footer_top = child->GetT();
  }
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceBodyPanelId);
  if (child)
    child->SetArea(rectangle->left + 1, heading_bottom + 1, rectangle->right - 1, footer_top - 1);
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceAcceptId);
  if (child)
    child->SetArea(rectangle->left, heading_bottom, rectangle->right, footer_top);
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceTopBorderId);
  if (child)
    child->GZWinMoveTo(rectangle->right - 48, rectangle->bottom - 40);
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceLeftBorderId);
  if (child) {
    ScRect button;
    child->GetArea(&button);
    child->GZWinMoveTo((button.left - button.right + 42) / 2 + rectangle->right - 47,
        (button.top - button.bottom + 32) / 2 + rectangle->bottom - 39);
  }
  return 1;
}

// Sum the ordinance groups, format their signed annual total and recolor the total label before requesting relayout.
u8 UiOrdinanceDialog::OnUpdateOrdinanceTotal(WnWindow *window) {
  UiOrdinanceTotal total{};
  ScString text;
  u8 result = 0;
  WnWindow *list = window->GetChildWindowFromID(kUiAdvisorOrdinanceBodyPanelId);
  if (list) {
    list->EnumChildren(IID_UiAdvisorOrdinanceControl, EnumOrdinanceControls, &total);
    if (UiGetLanguageManager())
      UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(total.amount, &text);
    WnLabelControl *label = nullptr;
    if (window->GetChildAs(kUiAdvisorOrdinanceFooterId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
      WnWindow *label_window = label->GetWindow();
      u32 color = label_window->MakeFillColor(total.amount < 0 ? 255 : 0, 0, 0);
      label->SetTextColor(color);
      label->SetCaption(text);
      label->Fit();
      label->Release();
      window->GetWindowManager()->PostMessageToTarget(window, kWnMsg_Relayout, 0, 0, 0);
      result = 1;
    }
  }
  return result;
}

// Build the ordinance headings, real scrolling list, total labels and close control before restoring saved placement.
// Preserve the original text lifetimes and cached-image borrowing; undefined missing-image/list paths stay explicit.
u8 UiOrdinanceDialog::OnInit(WnWindow *window) {
  if (!window)
    return 0;
  ScString text;
  if (UiGetMessageServer())
    UiGetMessageServer()->AddNotification(window, kWnMsg_OrdinanceTotalChanged);
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  UiTextRef title(kRsGameOrdinancesText, kRsGameTextGroup);
  if (title.IsResolved())
    window->SetCaption(*title.GetResource());
  UiTextRef ordinance_title(kRsGameOrdinanceText, kRsGameTextGroup);
  if (ordinance_title.IsResolved())
    ordinance_title.CopyTo(&text);
  else
    text.Erase(0, text.Strlen());
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(kUiAdvisorOrdinanceCaptionId, text);
  if (label) {
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->Fit();
    WnWindow *child = label->GetWindow();
    if (!window->ChildAdd(child)) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  UiTextRef cost_title(kRsGameCurrentMonthlyCostText, kRsGameTextGroup);
  if (cost_title.IsResolved())
    cost_title.CopyTo(&text);
  else
    text.Erase(0, text.Strlen());
  label = UiGetWindowResources()->CreateLabel(kUiAdvisorOrdinanceBodyId, text);
  if (label) {
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->Fit();
    WnWindow *child = label->GetWindow();
    if (!window->ChildAdd(child)) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  WnFlatControl *separator = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&separator))) {
    separator->GetWindow()->SetID(kUiAdvisorOrdinanceAcceptId);
    WnResources *resources = UiGetWindowResources();
    WnWindow *surface = separator->GetWindow();
    u32 color = resources->GetColor(kWnColorUiBackground);
    surface->SetFillColor(color);
    separator->SetOutlineFlags(32);
    separator->GetWindow()->SetArea(0, 0, 1, 1);
    WnWindow *child = separator->GetWindow();
    if (window->ChildAdd(child)) {
      separator->GetWindow()->SendToBack();
      separator->GetWindow()->SetFlag(kWnFlagReorderable, false);
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(separator->GetWindow());
    }
    separator->Release();
  }
  UiOrdinanceList *list = new UiOrdinanceList;
  list->AddRef();
  list->SetID(kUiAdvisorOrdinanceBodyPanelId);
  assert(font);
  i32 line_size = font->Height(0);
  list->SetLineSizeV((u16)line_size);
  if (!window->ChildAdd(list)) {
    window->GetWindowManager()->DestroyWindow(list);
    list->Release();
    list = nullptr;
  }
  UiTextRef total_title(kRsGameMonthlyTotalText, kRsGameTextGroup);
  if (total_title.IsResolved())
    total_title.CopyTo(&text);
  else
    text.Erase(0, text.Strlen());
  label = UiGetWindowResources()->CreateLabel(kUiAdvisorOrdinancePortraitId, text);
  if (label) {
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->Fit();
    WnWindow *child = label->GetWindow();
    if (!window->ChildAdd(child)) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  text.Erase(0, text.Strlen());
  label = UiGetWindowResources()->CreateLabel(kUiAdvisorOrdinanceFooterId, text);
  if (label) {
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->Fit();
    WnWindow *child = label->GetWindow();
    if (!window->ChildAdd(child)) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  WnFlatControl *footer = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&footer))) {
    u32 fill = UiGetWindowResources()->GetColor(kWnThemePanelFillColorId);
    u16 outline = (u16)UiGetWindowResources()->GetColor(kWnThemeBorderColorId);
    footer->GetWindow()->SetID(kUiAdvisorOrdinanceTopBorderId);
    footer->GetWindow()->SetFlag(kWnFlagReorderable, false);
    footer->GetWindow()->SetArea(0, 0, 44, 34);
    footer->GetWindow()->SetFillColor(fill);
    footer->SetOutlineColor((u32)outline);
    footer->SetOutlineFlags(15);
    WnWindow *child = footer->GetWindow();
    if (!window->ChildAdd(child)) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(footer->GetWindow());
    }
    footer->Release();
  }
  GxBuffer *close_image;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    close_image = image.GetBuffer();
  }
  WnButton *close;
  {
    ScString empty;
    WnResources *resources = UiGetWindowResources();
    assert(close_image);
    const ScRect *source = close_image->GetBufferArea();
    close = resources->CreateToggleButton(kUiAdvisorOrdinanceLeftBorderId, empty, close_image, source, 32);
  }
  if (close) {
    close->GetWindow()->SetFlag(kWnFlagReorderable, false);
    close->SetButtonFlag(kWnButtonShowText, false);
    ScRect bounds;
    close->GetWindow()->GetArea(&bounds);
    close->GetWindow()->GZWinMoveTo(
        (bounds.left - bounds.right + 42) / 2 + 1, (bounds.top - bounds.bottom + 32) / 2 + 1);
    WnWindow *child = close->GetWindow();
    if (!window->ChildAdd(child)) {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(close->GetWindow());
    }
    close->Release();
  }
  FillOrdinanceList(list);
  assert(list);
  list->Release();
  OnUpdateOrdinanceTotal(window);
  window->SetSize(384, 354);
  if (UiGetPreferences()->AdvMgrPrefs()->ready) {
    ApPreferences *top_preferences = UiGetPreferences(), *left_preferences = UiGetPreferences();
    u32 top = top_preferences->AdvMgrPrefs()->ordinance_y;
    u32 left = left_preferences->AdvMgrPrefs()->ordinance_x;
    window->GZWinMoveTo((i32)left, (i32)top);
  } else
    window->GZWinMoveTo(8, 8);
  window_exists = 1;
  return 1;
}

// Combine header widths, the live list minimum and footer widths with the original fixed lower margin.
u8 UiOrdinanceDialog::OnGetMinMaxInfo(WnWindow *window, ScRect *out, const ScRect *) {
  *out = {0, 0, 640, 480};
  i32 header_height = 0;
  WnWindow *child = window->GetChildWindowFromID(kUiAdvisorOrdinanceCaptionId);
  if (child) {
    if (child->GetH() >= 0)
      header_height = child->GetH();
    out->left += child->GetW();
  }
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceBodyId);
  if (child) {
    i32 height = child->GetH();
    if (header_height <= height)
      header_height = child->GetH();
    out->left += child->GetW();
  }
  out->top += header_height;
  WnControlList *list = nullptr;
  if (window->GetChildAs(kUiAdvisorOrdinanceBodyPanelId, IID_WnControlList, reinterpret_cast<void **>(&list))) {
    ScRect limits;
    list->GetMinMaxInfo(&limits);
    if (out->left < limits.left)
      out->left = limits.left;
    out->top += limits.top;
    list->Release();
  }
  i32 value_width = 0, footer_height = 0;
  child = window->GetChildWindowFromID(kUiAdvisorOrdinanceFooterId);
  if (child) {
    value_width = child->GetW();
    if (child->GetH() >= 0)
      footer_height = child->GetH();
  }
  i32 footer_width;
  child = window->GetChildWindowFromID(kUiAdvisorOrdinancePortraitId);
  if (child) {
    footer_width = child->GetW() + value_width;
    i32 height = child->GetH();
    if (footer_height <= height)
      footer_height = child->GetH();
  } else
    footer_width = value_width;
  if (out->left < footer_width)
    out->left = footer_width;
  out->top += footer_height + 46;
  return 1;
}

// Clear the existing ordinance groups and enumerate the city's advisors in their supplied order.
// Release successful enumeration entries after inserting each advisor's group.
u8 UiOrdinanceDialog::FillOrdinanceList(WnWindow *list) {
  if (!list)
    return 0;
  list->ChildDeleteAll();
  CtCity *city = UiGetCity();
  if (!city)
    return 0;
  u32 count = city->GetAdvisorListLength();
  if (count) {
    AdAdvisor **advisors = new AdAdvisor *[count];
    if (city->GetAdvisorList(count, advisors) && (i32)count > 0) {
      for (u32 i = 0; i != count; ++i) {
        AddAdvisorOrdinances(list, advisors[i]);
        if (advisors[i]) {
          advisors[i]->Release();
          advisors[i] = nullptr;
        }
      }
    }
    delete[] advisors;
  }
  return 1;
}

// Create the advisor's real ordinance-group window, preserving its retained secondary view through child insertion.
u8 UiOrdinanceDialog::AddAdvisorOrdinances(WnWindow *list, AdAdvisor *advisor) {
  if (!advisor)
    return 0;
  u32 advisor_id = advisor->GetAdvisorID();
  if (!advisor_id)
    return 0;
  UiAdvisorOrdinanceControl *group = new UiAdvisorOrdinance;
  group->AddRef();
  group->SetAssociatedAdvisorID(advisor_id);
  WnWindow *window = group->AsIGZWin();
  u8 added = list->ChildAdd(window);
  if (!added) {
    WnWindowManager *manager = list->GetWindowManager();
    manager->DestroyWindow(group->AsIGZWin());
  }
  group->Release();
  return added;
}

UiOrdinanceList::UiOrdinanceList()
    : list_flags(0), virtual_width(0), virtual_height(0), scroll_x(0), scroll_y(0), maximum_y(0), page_v(0), page_h(0),
      line_v(1), line_h(1), gutter_x(3), gutter_y(4), vertical_scroll(nullptr), horizontal_scroll(nullptr) {
}

UiOrdinanceList::~UiOrdinanceList() {
}

// Publish the control-list identity or delegate to the common primary-window query.
bool UiOrdinanceList::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnControlList)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnControlList *>(this);
  AddRef();
  return true;
}

u32 UiOrdinanceList::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiOrdinanceList::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window from the list's secondary interface without retaining it.
WnWindow *UiOrdinanceList::AsIGZWin() {
  return this;
}

// Enable the original clipping/background flags and fill color before common initialization.
bool UiOrdinanceList::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  SetFlag(kWnFlagTransparentBuffer, true);
  SetFlag(kWnFlagPrivateBuffer, true);
  u32 color = UiGetWindowResources()->GetColor(kWnColorUiTransparency);
  SetFillColor(color);
  return UiBaseWindow::Init();
}

bool UiOrdinanceList::Shutdown() {
  ScRelease(&vertical_scroll);
  ScRelease(&horizontal_scroll);
  HideWindow();
  PrivateBuffer(false);
  return UiBaseWindow::Shutdown();
}

// Add the child through the common root, bring it forward and clear its independent background-drawing flag.
bool UiOrdinanceList::ChildAdd(WnWindow *child) {
  bool added = UiBaseWindow::ChildAdd(child);
  if (added) {
    ChildToFront(child);
    child->SetFlag(kWnFlagReorderable, false);
  }
  return added;
}

bool UiOrdinanceList::GZPaint() {
  if (!draw_buffer->Lock(GxLockFlags::Fill))
    return false;
  u32 color = GetFillColor();
  draw_buffer->Fill(color, draw_area, nullptr);
  draw_buffer->Unlock(GxLockFlags::Fill);
  return true;
}

bool UiOrdinanceList::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiOrdinanceList::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Store the two gutter bytes without relayout or normalization.
u8 UiOrdinanceList::SetGutters(u8 horizontal, u8 vertical) {
  gutter_x = horizontal;
  gutter_y = vertical;
  return 1;
}

// Widen the current gutter bytes into the caller's separate integer outputs.
u8 UiOrdinanceList::GetGutters(i32 *horizontal, i32 *vertical) {
  *horizontal = gutter_x;
  *vertical = gutter_y;
  return 1;
}

u8 UiOrdinanceList::SetPageSizeV(u16 size) {
  page_v = size;
  return 1;
}

u8 UiOrdinanceList::SetLineSizeV(u16 size) {
  line_v = size;
  return 1;
}

u8 UiOrdinanceList::SetPageSizeH(u16 size) {
  page_h = size;
  return 1;
}

u8 UiOrdinanceList::SetLineSizeH(u16 size) {
  line_h = size;
  return 1;
}

// Retain the horizontal offset and clamp the vertical offset before updating its scrollbar and child placement.
u8 UiOrdinanceList::ScrollTo(i32 x, i32 y) {
  scroll_x = x;
  scroll_y = y;
  if (scroll_y < 0)
    scroll_y = 0;
  if (scroll_y > maximum_y)
    scroll_y = maximum_y;
  if (vertical_scroll)
    vertical_scroll->SetValue(scroll_y);
  i32 height = GetH(), width = GetW();
  PlaceControls(width, height);
  return 1;
}

// Enforce gutter minima and negotiate content width when the requested size changes, then set the root rectangle.
void UiOrdinanceList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = right - left, height = bottom - top;
  if (width < 2 * gutter_x)
    width = 2 * gutter_x;
  if (height < 2 * gutter_y)
    height = 2 * gutter_y;
  if (width == GetW() && height == GetH()) {
    UiBaseWindow::SetArea(left, top, right, bottom);
    return;
  }
  OnLayout(&width, &height);
  UiBaseWindow::SetArea(left, top, left + width, top + height);
}

bool UiOrdinanceList::EnumMinVirtualSize(WnWindow *, u32, void *view, void *context) {
  WnListItem *item = static_cast<WnListItem *>(view);
  UiListExtent *extent = static_cast<UiListExtent *>(context);
  ScRect limits{};
  if (item->GetMinMaxInfo(&limits)) {
    if (limits.left > extent->width)
      extent->width = limits.left;
    extent->height = (i32)((u32)extent->height + (u32)limits.top);
  }
  return true;
}

// Measure only children that expose the original list-item interface and publish the two independent outputs.
u8 UiOrdinanceList::CalcMinVirtualSize(i32 *width, i32 *height) {
  *height = 0;
  UiListExtent extent{};
  *width = 0;
  EnumChildren(IID_WnListItem, EnumMinVirtualSize, &extent);
  *width = extent.width;
  *height = extent.height;
  return 1;
}

// Report the measured content width and gutter minima, adding an existing vertical scrollbar's extent.
// The original minimum height deliberately does not include the children's full virtual height.
bool UiOrdinanceList::GetMinMaxInfo(ScRect *limits) {
  i32 width, height;
  CalcMinVirtualSize(&width, &height);
  limits->right = 0xffff;
  limits->bottom = 0xffff;
  limits->left = width + 2 * gutter_x;
  limits->top = 2 * gutter_y;
  if (vertical_scroll) {
    ScRect scrollbar;
    vertical_scroll->GetExtent(&scrollbar);
    limits->left += scrollbar.left + gutter_x;
    limits->top += scrollbar.top;
  }
  return true;
}

// Recompute the vertical range and clamp an offset only when the nonnegative new range becomes smaller.
u8 UiOrdinanceList::OnVirtualSizeChanged(i32, i32 height) {
  maximum_y = virtual_height + 2 * gutter_y - height;
  if (maximum_y >= 0) {
    if (scroll_y > maximum_y)
      ScrollTo(scroll_x, maximum_y);
  } else
    maximum_y = 0;
  if (vertical_scroll)
    vertical_scroll->SetRange(0, maximum_y);
  return 1;
}

// Lay out list-item children in their existing order and hide rows entirely beyond the vertical viewport.
// Query retention is balanced through each original primary-window view after its placement and height read.
u8 UiOrdinanceList::PlaceControls(i32, i32 height) {
  u32 count = 0;
  for (WnChild *node = children.head->next; node != children.head; node = node->next)
    ++count;
  if (!count)
    return 1;
  i32 left = gutter_x - scroll_x, bottom = height - gutter_y, top = gutter_y - scroll_y;
  WnChild *node = children.head->next;
  do {
    WnWindow *window = node->window;
    WnListItem *item;
    if (window->QueryInterface(IID_WnListItem, reinterpret_cast<void **>(&item))) {
      item->LayoutW(virtual_width);
      window->GZWinMoveTo(left, top);
      if (window->GetB() <= gutter_y || window->GetT() >= bottom)
        window->HideWindow();
      else
        window->ShowWindow();
      top += window->GetH();
      window->Release();
    }
    --count;
    node = node->next;
  } while (count);
  return 1;
}

// Measure content, update scrollbar visibility and grow the requested width only when its required minimum increases.
// Configure the existing vertical scrollbar before updating ranges and placing the live rows.
bool UiOrdinanceList::OnLayout(i32 *width, i32 *height) {
  bool grew = false;
  CalcMinVirtualSize(&virtual_width, &virtual_height);
  UpdateScrollBarVisibility(*width, *height);
  if (list_flags & 0x40000) {
    if (vertical_scroll) {
      WnWindow *window = vertical_scroll->GetWindow();
      ScRect scrollbar;
      vertical_scroll->GetExtent(&scrollbar);
      if (*width < virtual_width + window->GetW() + 2 * gutter_x) {
        *width = virtual_width + window->GetW() + 2 * gutter_x;
        if (*width < scrollbar.left)
          *width = scrollbar.left;
        grew = true;
      } else
        virtual_width = *width - 3 * gutter_x - window->GetW();
      i32 bottom = *height - gutter_y, right = *width - gutter_x, top = gutter_y;
      i32 scrollbar_width = window->GetW();
      window->SetArea(*width - gutter_x - scrollbar_width, top, right, bottom);
      if (page_v)
        vertical_scroll->SetPageCount(page_v);
      else {
        i32 page = window->GetH();
        vertical_scroll->SetPageCount(page);
      }
      vertical_scroll->SetLineCount(line_v);
    }
  } else if (*width >= virtual_width)
    virtual_width = *width - 2 * gutter_x;
  else {
    *width = virtual_width + 2 * gutter_x;
    grew = true;
  }
  OnVirtualSizeChanged(*width, *height);
  PlaceControls(*width, *height);
  return grew;
}

// Toggle the vertical scrollbar only when content crosses the viewport threshold, preserving its retained instance.
u8 UiOrdinanceList::UpdateScrollBarVisibility(i32, i32 height) {
  if (virtual_height <= height - 2 * gutter_y) {
    if (!(list_flags & 0x40000))
      return 0;
    if (vertical_scroll)
      vertical_scroll->GetWindow()->HideWindow();
    list_flags &= ~0x40000u;
    ScrollTo(0, 0);
  } else {
    if (list_flags & 0x40000)
      return 0;
    if (!vertical_scroll)
      CreateScrollBar(kUiOrdinanceVerticalScrollbarId, &vertical_scroll);
    if (vertical_scroll)
      vertical_scroll->GetWindow()->ShowWindow();
    list_flags |= 0x40000;
  }
  return 1;
}

// Create and initialize one of the two original scrollbar identifiers, then attach its window and orientation.
// Failed setup or attachment destroys the primary window and clears the retained interface output.
u8 UiOrdinanceList::CreateScrollBar(u32 id, WnScrollbarControl **out) {
  *out = nullptr;
  if (id != kUiOrdinanceVerticalScrollbarId && id != kUiOrdinanceHorizontalScrollbarId)
    return 0;
  if (!UiGetCom()->GetClassObject(kWnAppScrollbarClassId, IID_WnScrollbarControl, reinterpret_cast<void **>(out)) ||
      !*out)
    return 0;
  WnWindow *window = (*out)->GetWindow();
  window->SetID(id);
  ScRect image_area{0, 0, 16, 16};
  if (!(*out)->ImageSetup(image_area, nullptr, 0) || !ChildAdd(window)) {
    if (GetWindowManager())
      GetWindowManager()->DestroyWindow(window);
    (*out)->Release();
    *out = nullptr;
    return 0;
  }
  (*out)->SetOrientation(id == kUiOrdinanceVerticalScrollbarId ? 2 : 1);
  return 1;
}

u8 UiOrdinanceList::OnScrollH(const ScMessage &) {
  return 0;
}

// Accept only this scrollbar's line/page/thumb notifications and scroll to its live value.
u8 UiOrdinanceList::OnScrollV(const ScMessage &message) {
  if (!vertical_scroll)
    return 0;
  if (vertical_scroll->GetWindow()->GetID() != (u32)message.data1)
    return 0;
  u32 kind = (u32)message.data2;
  if (kind != 2 && kind != 3 && kind != 6 && kind != 7 && kind != 11)
    return 0;
  i32 value = vertical_scroll->GetValue();
  ScrollTo(scroll_x, value);
  return 1;
}

// Handle arrow-key scrolling, content-size changes and the two scrollbar notification identities.
bool UiOrdinanceList::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown) {
    if ((u32)message->data1 == 38) {
      ScrollTo(0, (i32)((u32)scroll_y - line_v));
      return true;
    }
    if ((u32)message->data1 == 40) {
      ScrollTo(0, (i32)((u32)scroll_y + line_v));
      return true;
    }
    return UiBaseWindow::DoMessage(message);
  }
  if (message->type == kWnMsg_KeyUp) {
    if ((u32)message->data1 == 38 || (u32)message->data1 == 40)
      return true;
    return UiBaseWindow::DoMessage(message);
  }
  if (message->type == kWnMsg_LayoutRequested) {
    i32 width = GetW(), height = GetH();
    OnLayout(&width, &height);
    return true;
  }
  if (message->type == kWnMsg_ScrollHorizontal)
    return OnScrollH(*message) != 0;
  if (message->type == kWnMsg_ScrollVertical)
    return OnScrollV(*message) != 0;
  return UiBaseWindow::DoMessage(message);
}
