#include "ui/petitions_deals.h"

#include <assert.h>
#include <stdlib.h>

#include "advisors/ad_advice.h"
#include "advisors/ad_petitions.h"
#include "advisors/api.h"
#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/thread.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "resources/resource.h"
#include "ui/advisor_dialogs.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/window.h"

// Meet-advisors uses this portrait key when no selected advisor supplies its own artwork.
enum UiAdvisorFallbackArtwork : u32 {
  kUiMeetAdvisorsFallbackPortraitId = 0x1010303,
};

// The all-ones petitioner requests the neighbor-deal petition; ordinary IDs request an existing petition.
enum UiPetitionerIdentity : u32 {
  kUiNeighborDealPetitionerId = 0xffffffff,
};

// Petitioner links open a modeless advisor dialog at its first topic.
enum UiPetitionerAdvisorTopic : u32 {
  kUiPetitionerModelessAdvisorTopic = 0x80000001,
};

// Advisor lists separate advisor and petitioner sections; business-deal buttons open the financial briefing or close.
enum UiAdvisorListControlId : u32 {
  kUiMeetAdvisorsHeadingId = 0x62be5dd3,
  kUiMeetPetitionerIssuesHeadingId = 0x62be5dd4,
  kUiMeetAdvisorsDividerId = 0x62be5dd5,
  kUiMeetPetitionerListId = 0x62be5dd6,
  kUiBusinessFinanceBriefingButtonId = 0x22fa718e,
  kUiBusinessDealsCloseButtonId = 0x62fa59f1,
};

UiBusinessDeals::UiBusinessDeals() : window(nullptr) {
}

UiBusinessDeals::~UiBusinessDeals() {
}

bool UiBusinessDeals::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiDialog62FA574B, UiDialog62FA574B);
  return UiDataRows::QueryInterface(iid, out);
}

u32 UiBusinessDeals::AddRef() {
  return UiDataRows::AddRef();
}

u32 UiBusinessDeals::Release() {
  return UiDataRows::Release();
}

// Create and configure the generic dialog once, retaining its real callback interface.
u8 UiBusinessDeals::Init() {
  if (window)
    return 1;
  window = nullptr;
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result && window) {
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericCloseButton, 0);
    window->GetWindow()->PrivateBuffer(true);
    return window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  return result;
}

u8 UiBusinessDeals::EnumCityItems(CtCity *city, bool enabled) {
  ScMessage message(enabled ? kWnMsg_QueryOpened : kWnMsg_QueryClosed, kAdBusinessDealQuerySelector, 0, 0);
  if (city) {
    city->DoQueryInfo(&message, static_cast<UiDialog62FA574B *>(this));
    city->DoEnumLayers(&message, static_cast<UiDialog62FA574B *>(this));
  }
  return 1;
}

// Borrow the generic dialog's underlying window through its real interface.
WnWindow *UiBusinessDeals::GetWindow() const {
  return window->GetWindow();
}

// Forward the city query to each non-null layer with this dialog's enumerator identity.
bool UiBusinessDeals::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<UiDialog62FA574B *>(this));
  return true;
}

// Dispatch dialog lifecycle and button messages, preserving the original sound and finance actions.
bool UiBusinessDeals::DoWinMsg(WnWindow *receiver, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command && (u32)first == kWnCommandButtonActivated) {
    if ((u32)second == kUiBusinessFinanceBriefingButtonId) {
      UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
      DoFinanceBriefing();
      return true;
    }
    if ((u32)second == kUiFrameCloseButtonId || (u32)second == kUiBusinessDealsCloseButtonId) {
      UiGetApplication()->GetBoxX()->Event(35, 0, 0, 0, 0);
      return EndDialog(receiver);
    }
  } else if (message == kWnMsg_KeyDown && (u32)first == 27) {
    return EndDialog(receiver);
  } else if (message == kWnMsg_KeyUp) {
    UiGetApplication()->GetBoxX()->Event(37, 0, 0, 0, 0);
  } else if (message == kWnMsg_Init) {
    return OnInit(receiver) != 0;
  } else if (message == kWnMsg_Shutdown) {
    return OnShutdown(receiver) != 0;
  }
  return false;
}

// Unregister the city's business-deal queries before releasing the generic dialog.
u8 UiBusinessDeals::OnShutdown(WnWindow *) {
  if (window) {
    EnumCityItems(UiGetCity(), false);
    window->Release();
    window = nullptr;
  }
  return 1;
}

bool UiBusinessDeals::EndDialog(WnWindow *receiver) {
  return UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(receiver), 0);
}

bool UiBusinessDeals::DoFinanceBriefing() {
  UiGetApplication()->GetBoxX()->Event(36, 0, 0, 0, 0);
  ScMessage message(kWnMsg_ModalAdvisorBriefing, kAdBudgetAdvisorId, 3005, 0);
  return UiGetMessageServer()->MessageSend(message);
}

// Create a dialog label and attach its primary window after applying its font and geometry.
u8 UiBusinessDeals::AddText(u32 x, u32 y, u32 width, u32 height, const ScString &text, u32 alignment) {
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(x + 1000 * y, text);
  if (!label)
    return 0;
  label->GetWindow()->SetFlag(kWnFlagReorderable, false);
  label->SetLabelFlag(kWnLabelWrapText, true);
  label->SetAlignment(alignment);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  label->GetWindow()->SetArea((i32)x, (i32)y, (i32)(x + width), (i32)(y + height));
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(label->GetWindow());
  label->Release();
  return 1;
}

// Create the flat button surround first, then center the image button inside it and attach both windows.
u8 UiBusinessDeals::AddButton(u32 identifier, u32 x, u32 y, u32 image_id) {
  WnFlatControl *panel = nullptr;
  if (!UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel)))
    return 0;
  u32 fill = UiGetWindowResources()->GetColor(kWnThemePanelFillColorId);
  u32 outline = UiGetWindowResources()->GetColor(kWnThemeBorderColorId);
  panel->GetWindow()->SetFlag(kWnFlagReorderable, false);
  panel->GetWindow()->SetArea((i32)x, (i32)y, (i32)(x + 44), (i32)(y + 34));
  panel->GetWindow()->SetFillColor(fill);
  panel->SetOutlineColor((u16)outline);
  panel->SetOutlineFlags(15);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(panel->GetWindow());
  panel->Release();
  GxBuffer *image;
  {
    const GxColorType format(GxColorType::Color::RGB16);
    UiImageRef reference(image_id, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = reference.GetBuffer();
  }
  WnButton *button;
  {
    ScString caption;
    WnResources *resources = UiGetWindowResources();
    button = resources->CreateButton(identifier, caption, image, image->GetBufferArea(), 32);
  }
  if (!button)
    return 0;
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  button->SetButtonFlag(kWnButtonShowText, false);
  ScRect area;
  button->GetWindow()->GetArea(&area);
  i32 offset_x = (i32)((u32)area.left - area.right + 42) / 2;
  i32 offset_y = (i32)((u32)area.top - area.bottom + 32) / 2;
  button->GetWindow()->GZWinMoveTo((i32)((u32)offset_x + x + 1), (i32)((u32)offset_y + y + 1));
  parent = window->GetWindow();
  parent->ChildAdd(button->GetWindow());
  button->Release();
  return 1;
}

void UiBusinessDeals::CenterDialogEffects() {
  ScRect parent, area;
  window->GetWindow()->GetMainWindow()->GetArea(&parent);
  window->GetWindow()->GetArea(&area);
  window->GetWindow()->GZWinMoveTo((i32)((u32)area.left + parent.right - area.right - parent.left) / 2,
      (i32)((u32)parent.bottom + area.top - parent.top - area.bottom) / 2);
}

// Populate the original two-column list from the city rows and add its separately formatted total label.
u8 UiBusinessDeals::FillList() {
  WnTextListControl *list = nullptr;
  u8 result = UiGetCom()->GetClassObject(kWnTextListClassId, IID_WnTextListControl, reinterpret_cast<void **>(&list));
  if (!result)
    return result;
  list->GetWindow()->SetFlag(kWnFlagReorderable, false);
  list->SetDrawSunkenRect(true);
  list->SetOpaque(true);
  WnResources *first_resources = UiGetWindowResources(), *second_resources = UiGetWindowResources();
  FtFont *first_font = first_resources->GetFont(kWnFontUiSmall);
  list->SetFonts(second_resources->GetFont(kWnFontUiSmall), first_font);
  ScRect area = {7, 59, 321, 127};
  list->SetJustification(true);
  list->SetRightColumnTextStart(260);
  list->GetWindow()->SetArea(area);
  u32 total = 0;
  ScString first, second;
  UiQueryIterator *iterator = nullptr;
  if (CreateIterator(&iterator)) {
    iterator->AddRef();
    UiQueryRow *row;
    while ((row = iterator->GetNext()) != nullptr) {
      ScStringView *name = row->GetText(kAdBusinessDealNameColumn);
      if (!name)
        continue;
      ScString padded("   ");
      padded.Append(name->Data(), name->Strlen());
      first = padded;
      i32 amount = (i32)row->GetU32(kAdBusinessDealIncomeColumn);
      UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(amount, &second);
      ScString value;
      if (amount > 0)
        value.Append("+");
      value.Append(second.Data(), second.Strlen());
      value.Append("   ");
      second = value;
      list->AddRow(first, second, -1);
      total += (u32)amount;
    }
    iterator->Release();
  }
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(list->GetWindow());
  list->Release();
  ScString total_caption;
  {
    UiTextRef reference(kRsGameTotalMonthlyContributionText, kRsGameTextGroup);
    total_caption = reference.GetText();
  }
  i32 amount = (i32)total;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(amount, &second);
  ScString label(total_caption);
  label.Append("   ");
  if (amount > 0)
    label.Append("+");
  label.Append(second.Data(), second.Strlen());
  label.Append("   ");
  AddText(7, 127, 314, 28, label, 6);
  return result;
}

// Set up the business-deals dialog, then register city queries before building the populated list.
u8 UiBusinessDeals::OnInit(WnWindow *receiver) {
  if (!window)
    return 0;
  ScString title;
  {
    UiTextRef reference(kRsGameBusinessDealsText, kRsGameTextGroup);
    title = reference.GetText();
  }
  receiver->SetCaption(title);
  ScRect area = {40, 40, 368, 236};
  receiver->SetArea(area);
  CenterDialogEffects();
  ScString first("   "), second, third;
  {
    UiTextRef reference(kRsGameCurrentBusinessDealsText, kRsGameTextGroup);
    ScString text = reference.GetText();
    first.Append(text.Data(), text.Strlen());
  }
  {
    UiTextRef reference(kRsGameMonthlyContributionText, kRsGameTextGroup);
    second = reference.GetText();
    second.Append("   ");
  }
  {
    UiTextRef reference(kRsGameFinanceBriefingText, kRsGameTextGroup);
    third = reference.GetText();
  }
  AddText(7, 41, 314, 18, first, 0);
  AddText(7, 41, 314, 18, second, 6);
  AddText(22, 155, 64, 34, third, 2);
  AddButton(kUiBusinessFinanceBriefingButtonId, 86, 155, kRsHelpButtonImageId);
  AddButton(kUiBusinessDealsCloseButtonId, 271, 155, kRsStandardButtonImageId);
  EnumCityItems(UiGetCity(), true);
  FillList();
  return 1;
}

// Construct the registered dialog and publish its original secondary business-deals interface view.
ScUnknown *UiCreateClass62FA5742() {
  return static_cast<UiDialog62FA574B *>(new UiBusinessDeals);
}

UiMeetMenu::UiMeetMenu()
    : references(0), window_exists(0), advisor_count(0), advisor_width(0), advisor_height(0), petitioner_count(0),
      petitioners(nullptr) {
}

UiMeetMenu::~UiMeetMenu() {
}

// Publish the original callback identity or Unknown, preserving the callback IID's explicit null-output rejection.
bool UiMeetMenu::QueryInterface(u32 iid, void **out) {
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

u32 UiMeetMenu::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Destroy the callback owner when its original signed reference count is no longer positive.
u32 UiMeetMenu::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

u8 UiMeetMenu::WinExists() const {
  return window_exists;
}

// Create and configure the generic window once, then publish its borrowed primary view and mark it present.
u8 UiMeetMenu::CreateWin(WnWindow **out) {
  *out = nullptr;
  if (WinExists())
    return 0;
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return 0;
  window->SetStyle(kUiGenericDraggable, 1);
  window->SetStyle(kUiGenericResizable, 0);
  window->SetStyle(kUiGenericScroller, 1);
  window->SetStyle(kUiGenericSecondButton, 0);
  window->GetWindow()->PrivateBuffer(true);
  window->SetCallback(this);
  *out = window->GetWindow();
  (*out)->SetID(kUiAdvisorMeetingWindowId);
  window_exists = 1;
  return 1;
}

// Route window lifecycle, petitioner selection and button notifications to their original handlers.
bool UiMeetMenu::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return (u32)first == kWnCommandButtonActivated && OnButtonClicked(window, (u32)second) != 0;
  if (message == kWnMsg_ListViewDoubleClick) {
    if ((u32)first == kUiMeetPetitionerListId && (u32)second != 0) {
      UiGetApplication()->GetBoxX()->Event(12, 0, 0, 0, 0);
      if (window->GetFlag(kWnFlagModal)) {
        ScMessage selected(kWnMsg_ModalPetitioner, (u32)second, 0, 0);
        UiGetMessageServer()->MessageSend(selected);
      } else {
        ScMessage selected(kWnMsg_PetitionerDialog, (u32)second, kUiAdvisorModeless, 0);
        UiGetMessageServer()->MessagePost(selected, false);
      }
    }
    return true;
  }
  if (message == kAdMsg_PetitionerListUpdated)
    return UpdatePetList() != 0;
  if (message == kWnMsg_Init)
    return window && OnInit(window) != 0;
  if (message == kWnMsg_Shutdown)
    return window && OnShutdown(window) != 0;
  return false;
}

// Save the meeting position, unregister its petitioner notification and retire the stored headers.
u8 UiMeetMenu::OnShutdown(WnWindow *window) {
  ApPreferences *preferences = UiGetPreferences();
  u32 left = (u32)window->GetL();
  preferences->AdvMgrPrefs()->meeting_x = left;
  preferences = UiGetPreferences();
  u32 top = (u32)window->GetT();
  preferences->AdvMgrPrefs()->meeting_y = top;
  UiGetPreferences()->AdvMgrPrefs()->ready = 1;
  if (UiGetMessageServer())
    UiGetMessageServer()->RemoveNotification(window, kAdMsg_PetitionerListUpdated);
  KillPetHeaders();
  window_exists = 0;
  return 1;
}

// Play the original button cue and send or post the selected advisor request through the message server.
u8 UiMeetMenu::OnButtonClicked(WnWindow *window, u32 identifier) {
  if (identifier == kUiFrameCloseButtonId) {
    UiGetApplication()->GetBoxX()->Event(11, 0, 0, 0, 0);
    return 0;
  }
  if (!window->GetChildWindowFromID(identifier))
    return 0;
  UiGetApplication()->GetBoxX()->Event(12, 0, 0, 0, 0);
  if (window->GetFlag(kWnFlagModal)) {
    ScMessage selected(kWnMsg_ModalAdvisorTopicList, identifier, 1, 0);
    UiGetMessageServer()->MessageSend(selected);
  } else {
    ScMessage selected(kWnMsg_AdvisorDialog, identifier, kUiPetitionerModelessAdvisorTopic, 0);
    UiGetMessageServer()->MessagePost(selected, false);
  }
  return 1;
}

// Release each retained header string and delete the original individually allocated header records and array.
u8 UiMeetMenu::KillPetHeaders() {
  if (!petitioners)
    return 1;
  for (i32 i = 0; i < petitioner_count; ++i) {
    if (petitioners[i]->text)
      petitioners[i]->text->Release();
    delete petitioners[i];
  }
  delete[] petitioners;
  petitioners = nullptr;
  petitioner_count = 0;
  return 1;
}

// Build the meeting window's advisor portraits, headings and live petitioner list under the city lock.
// Resource and String scopes retire before the lock is released, matching the original initialization lifetime.
u8 UiMeetMenu::OnInit(WnWindow *window) {
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  ScString caption;
  CtCity *city = UiGetCity();
  ScLock *lock = UiGetCity()->GetCriticalSection();
  lock->Lock();
  if (UiGetMessageServer())
    UiGetMessageServer()->AddNotification(window, kAdMsg_PetitionerListUpdated);
  advisor_count = (i32)city->GetAdvisorListLength();
  i32 width = 52 * advisor_count + 19;
  window->SetSize(width, 354);
  if (UiGetPreferences()->AdvMgrPrefs()->ready) {
    ApPreferences *top_preferences = UiGetPreferences(), *left_preferences = UiGetPreferences();
    u32 top = top_preferences->AdvMgrPrefs()->meeting_y;
    u32 left = left_preferences->AdvMgrPrefs()->meeting_x;
    window->GZWinMoveTo((i32)left, (i32)top);
  } else
    window->GZWinMoveTo(8, 8);
  {
    UiTextRef window_title(kRsGameMeetText, kRsGameTextGroup);
    if (window_title.IsResolved())
      window->SetCaption(*window_title.GetResource());
    UiTextRef advisor_heading(kRsGameAdvisorsText, kRsGameTextGroup);
    if (advisor_heading.IsResolved())
      advisor_heading.CopyTo(&caption);
    else
      caption.FromChar("", 0);
    WnLabelControl *label = UiGetWindowResources()->CreateLabel(kUiMeetAdvisorsHeadingId, caption);
    if (label) {
      label->SetFont(font);
      label->GetWindow()->SetSize(128, 8);
      label->SetBackground(false);
      label->SetLabelFlag(kWnLabelWrapText, false);
      label->Fit();
      if (window->ChildAdd(label->GetWindow()))
        label->GetWindow()->GZWinMoveTo(19, 38);
      else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(label->GetWindow());
      }
      label->Release();
    }
    UiTextRef petitioner_heading(kRsGamePetitionerIssuesText, kRsGameTextGroup);
    if (petitioner_heading.IsResolved())
      petitioner_heading.CopyTo(&caption);
    else
      caption.FromChar("", 0);
    label = UiGetWindowResources()->CreateLabel(kUiMeetPetitionerIssuesHeadingId, caption);
    if (label) {
      label->SetFont(font);
      label->GetWindow()->SetSize(128, 8);
      label->SetBackground(false);
      label->SetLabelFlag(kWnLabelWrapText, false);
      label->Fit();
      if (window->ChildAdd(label->GetWindow()))
        label->GetWindow()->GZWinMoveTo(19, 144);
      else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(label->GetWindow());
      }
      label->Release();
    }
    UiGenericInterface *generic = nullptr;
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      generic->AddDivider(kUiMeetAdvisorsDividerId, 130);
      generic->Release();
    }
    ScString advisor_name;
    RsKey fallback{kRsImageResourceType, kRsAdvisorPortraitGroup, kUiMeetAdvisorsFallbackPortraitId};
    RsKey *key = &fallback;
    i32 frames = 1;
    if (city) {
      advisor_count = (i32)city->GetAdvisorListLength();
      if (advisor_count) {
        AdAdvisor **advisors = new AdAdvisor *[advisor_count];
        if (city->GetAdvisorList((u32)advisor_count, advisors)) {
          for (i32 i = 0; i < advisor_count; ++i) {
            if (!advisors[i])
              continue;
            u32 mood = advisors[i]->GetAdvisorAverageMood();
            u32 identifier = advisors[i]->GetAdvisorID();
            if (!UiFindAdvisorPortrait(identifier, mood, true, key, &frames))
              key = advisors[i]->GetSmallAdvisorPicture();
            if (!key)
              key = &fallback;
            identifier = advisors[i]->GetAdvisorID();
            UiAdvisorButtonInterface *picture = nullptr;
            if (UiCreateAdvisorPicture(identifier, 1, frames, *key, &picture)) {
              WnWindow *picture_window = picture->AsIGZWin();
              advisor_width = picture_window->GetW();
              advisor_height = picture_window->GetH();
              advisors[i]->GetName(&advisor_name);
              picture_window->SetCaption(advisor_name);
              window->ChildAdd(picture_window);
              picture_window->GZWinMoveTo(52 * (advisor_count - i) - 33, 60);
              picture_window->Release();
            }
          }
          for (i32 i = 0; i < advisor_count; ++i) {
            if (advisors[i]) {
              advisors[i]->Release();
              advisors[i] = nullptr;
            }
          }
        }
        delete[] advisors;
      }
      GetPetHeaders();
      ApListViewControl *list = nullptr;
      if (UiGetCom()->GetClassObject(
              kApListViewControlClassId, IID_ApListViewControl, reinterpret_cast<void **>(&list))) {
        WnWindow *list_window = list->AsIGZWin();
        list_window->SetID(kUiMeetPetitionerListId);
        WnResources *resources = UiGetWindowResources();
        u32 color = resources->GetColor(kWnColorUiBackground);
        list_window->SetFillColor(color);
        u32 language = UiGetLanguageManager()->GetCurrentLanguage();
        if (language == kScLanguageJapanese ||
            (language >= kScLanguageSimplifiedChinese && language <= kScLanguageKorean)) {
          i32 first_height = font->Height(0);
          i32 rows = 184 / first_height;
          i32 height = font->Height(0) * rows;
          list_window->SetArea(19, 164, width - 5, height + 166);
        } else
          list_window->SetArea(19, 164, width - 5, 348);
        list->SetFont(font);
        list->SetListViewFlag(4, true);
        list->SetListViewFlag(8, true);
        list->SetListViewFlag(32, true);
        list->AddColumn(0, 16, nullptr);
        if (window->ChildAdd(list_window))
          FillPetListView(list);
        else {
          WnWindowManager *manager = window->GetWindowManager();
          manager->DestroyWindow(list->AsIGZWin());
        }
        list->Release();
      }
    }
    window_exists = 1;
  }
  lock->Unlock();
  return 1;
}

// Close the modal meeting window or notify its modeless parent, then clear the existence byte.
u8 UiMeetMenu::KillWin(WnWindow *window) {
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

u8 UiMeetMenu::GetWinArea(ScRect *out) const {
  *out = area;
  return 1;
}

// Replace petitioner headers while holding the city's critical section and retain every published title.
// A missing queue or failed header extraction returns false after the original cleanup.
u8 UiMeetMenu::GetPetHeaders() {
  ScLock *lock = UiGetCity()->GetCriticalSection();
  lock->Lock();
  if (petitioners)
    KillPetHeaders();
  u8 result = 0;
  CtCity *city = UiGetCity();
  if (city) {
    AdPetitionerManager *manager = city->GetPetitionerManager();
    if (manager) {
      petitioner_count = (i32)manager->GetPetitionerQueueSize();
      if (petitioner_count) {
        petitioners = new AdPetitionerHeader *[petitioner_count];
        for (i32 i = 0; i < petitioner_count; ++i)
          petitioners[i] = new AdPetitionerHeader{};
        if (manager->GetPetitionerHeaders(petitioners)) {
          for (i32 i = 0; i < petitioner_count; ++i) {
            if (petitioners[i]->text)
              petitioners[i]->text->AddRef();
            if (!petitioners[i]->id)
              petitioners[i]->id = (u32)ScRand() + 1;
          }
          result = 1;
        } else
          KillPetHeaders();
      }
    }
  }
  lock->Unlock();
  return result;
}

// Append each petitioner header that has a title, preserving its full identifier as the list item key.
u8 UiMeetMenu::FillPetListView(ApListViewControl *list) {
  for (i32 i = 0; i < petitioner_count; ++i) {
    AdPetitionerHeader *header = petitioners[i];
    if (header->text)
      list->AddItem((i32)header->id, 0, header->text);
  }
  return 1;
}

// Refresh the visible meeting list by updating surviving IDs, appending new titles and deleting stale entries.
u8 UiMeetMenu::UpdatePetList() {
  if (!UiGetWindowManager())
    return 0;
  WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromID(kUiAdvisorMeetingWindowId);
  if (!window)
    return 1;
  ApListViewControl *list = nullptr;
  if (!window->GetChildAs(kUiMeetPetitionerListId, IID_ApListViewControl, reinterpret_cast<void **>(&list)))
    return 1;
  WnWindow *list_window = list->AsIGZWin();
  list_window->HideWindow();
  i32 old_count = petitioners ? petitioner_count : 0;
  u32 *old_ids = new u32[old_count];
  for (i32 i = 0; i < old_count; ++i)
    old_ids[i] = petitioners[i]->id;
  KillPetHeaders();
  GetPetHeaders();
  for (i32 i = 0; i < petitioner_count; ++i) {
    AdPetitionerHeader *header = petitioners[i];
    if (!header->text)
      continue;
    i32 found = 0;
    while (found < old_count && (!old_ids[found] || old_ids[found] != header->id))
      ++found;
    if (found == old_count)
      list->AddItem((i32)header->id, 0, header->text);
    else {
      list->SetItemText((i32)old_ids[found], 0, header->text);
      old_ids[found] = 0;
    }
  }
  for (i32 i = 0; i < old_count; ++i)
    if (old_ids[i])
      list->RemoveItem((i32)old_ids[i]);
  delete[] old_ids;
  list_window->ShowWindow();
  list->Release();
  return 1;
}

UiPetitionDialog::UiPetitionDialog()
    : references(0), window_exists(0), current_petitioner(0), behavior_flags(0), petition{}, response_active(0) {
}

// Release the petition's seven text views, extra text and advisor name in their original ownership order.
UiPetitionDialog::~UiPetitionDialog() {
  for (u32 i = 0; i < 7; ++i) {
    if (petition.text[i])
      petition.text[i]->Release();
    petition.text[i] = nullptr;
  }
  if (petition.extra_text)
    petition.extra_text->Release();
  petition.extra_text = nullptr;
  if (petition.advisor_name)
    petition.advisor_name->Release();
  petition.advisor_name = nullptr;
}

// Publish the primary callback identity for either original IID when an output pointer is provided.
bool UiPetitionDialog::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiWindowCallback))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 UiPetitionDialog::AddRef() {
  references = (i32)((u32)references + 1);
  return (u32)references;
}

// Destroy the callback owner when its signed reference count is no longer positive.
u32 UiPetitionDialog::Release() {
  references = (i32)((u32)references - 1);
  if (references > 0)
    return (u32)references;
  delete this;
  return 0;
}

u8 UiPetitionDialog::WinExists() const {
  return window_exists;
}

// Create the generic petition window and configure its modal-dependent style before publishing its primary view.
u8 UiPetitionDialog::CreateWin(WnWindow **out, bool modal) {
  *out = nullptr;
  if (WinExists())
    return 0;
  UiGenericInterface *window = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window)))
    return 0;
  window->SetStyle(kUiGenericDraggable, 1);
  window->SetStyle(kUiGenericResizable, 0);
  window->SetStyle(kUiGenericScroller, 1);
  window->SetStyle(kUiGenericBackButton, !modal);
  window->SetStyle(kUiGenericSecondButton, 0);
  window->GetWindow()->PrivateBuffer(true);
  window->SetCallback(this);
  *out = window->GetWindow();
  (*out)->SetID(kUiAdvisorPetitionerWindowId);
  window_exists = 1;
  return 1;
}

// Close a modal petitioner window or notify its modeless parent, then clear the existence byte.
u8 UiPetitionDialog::KillWin(WnWindow *window) {
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

u8 UiPetitionDialog::GetWinArea(ScRect *out) const {
  *out = area;
  return 1;
}

// Select the next petitioner and preserve the original separate behavior-flags argument.
u8 UiPetitionDialog::SetCurrentPetID(u32 identifier, u32 flags) {
  current_petitioner = identifier;
  behavior_flags = flags;
  return 1;
}

// Dispatch button and lifecycle messages while preserving the original lifecycle null-window checks.
bool UiPetitionDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return (u32)first == kWnCommandButtonActivated && OnButtonClicked(window, (u32)second) != 0;
  if (message == kWnMsg_Init)
    return window && OnInit(window) != 0;
  if (message == kWnMsg_Shutdown)
    return window && OnShutdown(window) != 0;
  return false;
}

// Save the petition window position and clear its active record identifiers before releasing all retained text.
u8 UiPetitionDialog::OnShutdown(WnWindow *window) {
  ApPreferences *preferences = UiGetPreferences();
  u32 left = (u32)window->GetL();
  preferences->AdvMgrPrefs()->petition_x = left;
  preferences = UiGetPreferences();
  u32 top = (u32)window->GetT();
  preferences->AdvMgrPrefs()->petition_y = top;
  UiGetPreferences()->AdvMgrPrefs()->ready = 1;
  window_exists = 0;
  petition.id = 0;
  petition.advisor_id = 0;
  for (u32 i = 0; i < 7; ++i) {
    if (petition.text[i])
      petition.text[i]->Release();
    petition.text[i] = nullptr;
  }
  if (petition.extra_text)
    petition.extra_text->Release();
  petition.extra_text = nullptr;
  if (petition.advisor_name)
    petition.advisor_name->Release();
  petition.advisor_name = nullptr;
  return 1;
}

// Route petitioner responses through the real petition manager and synchronous advisor message box.
// A response already in progress suppresses all further buttons; modeless navigation retains its send/post ordering.
u8 UiPetitionDialog::OnButtonClicked(WnWindow *window, u32 identifier) {
  if (response_active)
    return 1;
  if (identifier == kUiFrameCloseButtonId) {
    UiGetApplication()->GetBoxX()->Event(11, 0, 0, 0, 0);
    return 0;
  }
  if (identifier == kUiFrameBackButtonId) {
    UiGetApplication()->GetBoxX()->Event(16, 0, 0, 0, 0);
    if (window->GetFlag(kWnFlagModal))
      KillWin(window);
    else {
      ScMessage selected(kWnMsg_MeetAdvisors, 0, 0, 0);
      UiGetMessageServer()->MessageSend(selected);
    }
    return 1;
  }
  if (identifier == kUiPetitionFirstChoiceId || identifier == kUiPetitionSecondChoiceId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    u32 button = identifier == kUiPetitionFirstChoiceId ? 1 : 2;
    CtCity *city = UiGetCity();
    if (city) {
      AdPetitionerManager *manager = city->GetPetitionerManager();
      if (manager)
        manager->PetitionerButtonPressed(current_petitioner, button);
    }
    UiAdvisorMessageBox *message = new UiAdvisorMessageBox;
    response_active = 1;
    message->AddRef();
    ScRect anchor;
    window->GetAreaAbsolute(&anchor);
    message->DoAdvMsgBox(petition.text[button + 4], petition.text[0], petition.portrait, 1, true, &anchor);
    message->Release();
    if (window->GetFlag(kWnFlagModal))
      KillWin(window);
    else {
      ScMessage selected(kWnMsg_MeetAdvisors, 0, 0, 0);
      UiGetMessageServer()->MessagePost(selected, false);
    }
    return 1;
  }
  if (identifier == kUiPetitionHelpId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    UiAdvisorMessageBox *message = new UiAdvisorMessageBox;
    message->AddRef();
    ScRect anchor;
    window->GetAreaAbsolute(&anchor);
    message->DoAdvMsgBox(petition.extra_text, petition.advisor_name, petition.advisor_portrait, 5, false, &anchor);
    message->Release();
    return 1;
  }
  if (identifier == kUiPetitionDismissId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    CtCity *city = UiGetCity();
    if (city) {
      AdPetitionerManager *manager = city->GetPetitionerManager();
      if (manager)
        manager->PetitionerButtonPressed(current_petitioner, 0);
    }
    if (!window->GetFlag(kWnFlagModal)) {
      ScMessage selected(kWnMsg_MeetAdvisors, 0, 0, 0);
      UiGetMessageServer()->MessagePost(selected, false);
    }
    return 1;
  }
  return 0;
}

// Acquire and retain the selected petition, then build its title, portrait, read-only message and response controls.
// Petitions with response text expose two choices and advisor help; other petitions receive the original dismiss action.
u8 UiPetitionDialog::OnInit(WnWindow *window) {
  if (!window || !UiGetWindowResources())
    return 0;
  ScString text;
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  window->SetSize(384, 354);
  if (UiGetPreferences()->AdvMgrPrefs()->ready) {
    ApPreferences *top_preferences = UiGetPreferences(), *left_preferences = UiGetPreferences();
    u32 top = top_preferences->AdvMgrPrefs()->petition_y;
    u32 left = left_preferences->AdvMgrPrefs()->petition_x;
    window->GZWinMoveTo((i32)left, (i32)top);
  } else
    window->GZWinMoveTo(8, 8);
  CtCity *city = UiGetCity();
  if (city) {
    AdPetitionerManager *manager = city->GetPetitionerManager();
    if (manager) {
      if (current_petitioner == kUiNeighborDealPetitionerId) {
        manager->GetNeighborDealPetitioner((u8)behavior_flags, &petition);
        current_petitioner = petition.id;
      } else
        manager->GetPetitionerStructureFromID(current_petitioner, &petition);
      for (u32 i = 0; i < 7; ++i)
        if (petition.text[i])
          petition.text[i]->AddRef();
      if (petition.extra_text)
        petition.extra_text->AddRef();
      if (petition.advisor_name)
        petition.advisor_name->AddRef();
    }
  }
  u32 ordinal = (petition.advisor_portrait.instance >> 8) & 255;
  if (ordinal >= 1 && ordinal <= 7) {
    const u32 identifiers[] = {kAdUtilitiesAdvisorId, kAdBudgetAdvisorId, kAdCityPlanningAdvisorId,
        kAdEnvironmentAdvisorId, kAdPublicSafetyAdvisorId, kAdTransportationAdvisorId, kAdDemographicsAdvisorId};
    i32 frames;
    UiFindAdvisorPortrait(identifiers[ordinal - 1], 3, false, &petition.advisor_portrait, &frames);
  }
  bool has_choices = petition.text[3] != nullptr;
  if (petition.text[1])
    window->SetCaption(*petition.text[1]);
  else {
    ScString caption;
    caption.Sprintf("Pet Hdr 0x%08lX %ld", (unsigned long)current_petitioner, (long)(i32)current_petitioner);
    window->SetCaption(caption);
  }
  if (petition.text[0])
    text = ScString(*petition.text[0]);
  else
    text.FromChar("", 0);
  UiTextRef::FormatForEllipsis(&text, 340, font);
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(kUiPetitionNameLabelId, text);
  if (label) {
    label->SetFont(font);
    label->GetWindow()->SetSize(128, 8);
    label->SetBackground(false);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->Fit();
    if (window->ChildAdd(label->GetWindow()))
      label->GetWindow()->GZWinMoveTo(19, 39);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(label->GetWindow());
    }
    label->Release();
  }
  WnFlatControl *panel = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel))) {
    panel->GetWindow()->SetID(kUiPetitionBodyPanelId);
    WnResources *resources = UiGetWindowResources();
    WnWindow *panel_window = panel->GetWindow();
    u32 color = resources->GetColor(kWnColorUiBackground);
    panel_window->SetFillColor(color);
    panel->SetOutlineFlags(32);
    panel->GetWindow()->SetArea(121, 58, 379, has_choices ? 260 : 349);
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
  if (petition.text[2])
    text = ScString(*petition.text[2]);
  else
    text.FromChar("", 0);
  WnEditorControl *editor = nullptr;
  UiGetCom()->GetClassObject(kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor));
  if (editor) {
    editor->AsIGZWin()->SetID(kUiPetitionBodyEditorId);
    editor->SetFont(font);
    editor->SetTextGutterX(12);
    editor->SetTextGutterY(4);
    editor->AsIGZWin()->SetSize(258, has_choices ? 202 : 291);
    editor->SetFlag(2);
    editor->ClearFlag(1);
    editor->ClearFlag(16);
    editor->ClearFlag(32);
    editor->SetText(text);
    if (window->ChildAdd(editor->AsIGZWin()))
      editor->AsIGZWin()->GZWinMoveTo(121, 58);
    else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->DestroyWindow(editor->AsIGZWin());
    }
    editor->Release();
  }
  UiAdvisorButtonInterface *portrait = nullptr;
  if (UiCreateAdvisorPicture(kUiPetitionPortraitId, 2, 1, petition.portrait, &portrait)) {
    WnWindow *picture_window = portrait->AsIGZWin();
    if (window->ChildAdd(picture_window))
      picture_window->GZWinMoveTo(19, 58);
    else
      window->GetWindowManager()->DestroyWindow(picture_window);
    picture_window->Release();
  }
  UiGenericInterface *generic = nullptr;
  if (has_choices) {
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      ScRect rectangle{11, 262, 121, 288};
      generic->AddPanel(kUiPetitionUpperBorderId, 0, 14, &rectangle);
      rectangle = {11, 287, 51, 350};
      generic->AddPanel(kUiPetitionLowerBorderId, 0, 12, &rectangle);
      generic->Release();
    }
    UiTextRef heading(kRsGameTakeActionText, kRsGameTextGroup);
    if (heading.IsResolved())
      heading.CopyTo(&text);
    else
      text.FromChar("", 0);
    label = UiGetWindowResources()->CreateLabel(kUiPetitionChoiceHeadingId, text);
    if (label) {
      label->SetFont(font);
      label->GetWindow()->SetSize(128, 8);
      label->SetBackground(false);
      label->SetLabelFlag(kWnLabelWrapText, false);
      label->Fit();
      if (window->ChildAdd(label->GetWindow()))
        label->GetWindow()->GZWinMoveTo(19, 266);
      else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(label->GetWindow());
      }
      label->Release();
    }
    ScRect button_area{0, 0, 26, 25};
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef arrow(2, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *arrow_buffer = arrow.GetBuffer();
    if (arrow_buffer)
      arrow_buffer->GetBufferArea(&button_area);
    for (u32 row = 0; row < 2; ++row) {
      text.FromChar("", 0);
      WnButton *button = UiGetWindowResources()->CreateFixedButton(
          kUiPetitionFirstChoiceId + row, text, arrow_buffer, &button_area, 0);
      if (button) {
        WnWindow *button_window = button->GetWindow();
        if (!arrow_buffer) {
          u32 color = UiGetWindowResources()->GetColor(kWnColorSilver);
          button_window->SetFillColor(color);
          button_window->SetSize(26, 25);
          button->SetButtonFlag(kWnButtonAutoFit, false);
        }
        button->SetButtonFlag(kWnButtonShowText, false);
        if (window->ChildAdd(button_window))
          button_window->GZWinMoveTo(19, row ? 318 : 290);
        else
          window->GetWindowManager()->DestroyWindow(button_window);
        button->Release();
      }
      if (petition.text[row + 3])
        text = ScString(*petition.text[row + 3]);
      else
        text.FromChar("", 0);
      label = UiGetWindowResources()->CreateLabel(kUiPetitionPrimaryActionLabelId + row, text);
      if (label) {
        label->SetFont(font);
        label->GetWindow()->SetSize(128, 8);
        label->SetBackground(false);
        label->SetLabelFlag(kWnLabelWrapText, false);
        label->Fit();
        if (window->ChildAdd(label->GetWindow()))
          label->GetWindow()->GZWinMoveTo(56, row ? 318 : 290);
        else {
          WnWindowManager *manager = window->GetWindowManager();
          manager->DestroyWindow(label->GetWindow());
        }
        label->Release();
      }
    }
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      ScRect rectangle{272, 293, 379, 349};
      generic->AddPanel(kUiPetitionActionPanelId, 0, 15, &rectangle);
      generic->Release();
    }
    UiImageRef help(kRsHelpButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *help_buffer = help.GetBuffer();
    if (help_buffer)
      help_buffer->GetBufferArea(&button_area);
    else
      button_area = {0, 0, 38, 33};
    text.FromChar("", 0);
    WnButton *button = UiGetWindowResources()->CreateFixedButton(kUiPetitionHelpId, text, help_buffer, &button_area, 0);
    if (button) {
      WnWindow *button_window = button->GetWindow();
      if (!help_buffer) {
        u32 color = UiGetWindowResources()->GetColor(kWnColorSilver);
        button_window->SetFillColor(color);
        button_window->SetSize(38, 33);
        button->SetButtonFlag(kWnButtonAutoFit, false);
      }
      button->SetButtonFlag(kWnButtonShowText, false);
      if (window->ChildAdd(button_window))
        button_window->GZWinMoveTo(337, 317);
      else
        window->GetWindowManager()->DestroyWindow(button_window);
      button->Release();
    }
    UiTextRef help_heading(kRsGameImpactAnalysisText, kRsGameTextGroup);
    if (help_heading.IsResolved())
      help_heading.CopyTo(&text);
    else
      text.FromChar("", 0);
    label = UiGetWindowResources()->CreateLabel(kUiPetitionImpactAnalysisLabelId, text);
    if (label) {
      label->SetLabelFlag(kWnLabelWrapText, true);
      label->SetFont(font);
      label->GetWindow()->SetSize(128, 8);
      label->SetBackground(false);
      label->Fit();
      if (window->ChildAdd(label->GetWindow()))
        label->GetWindow()->SetArea(276, 297, 337, 345);
      else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(label->GetWindow());
      }
      label->Release();
    }
  } else {
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      ScRect rectangle{11, 310, 51, 350};
      generic->AddPanel(kUiPetitionLowerBorderId, 0, 14, &rectangle);
      generic->Release();
    }
    ScRect button_area{0, 0, 26, 25};
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef arrow(2, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = arrow.GetBuffer();
    if (buffer)
      buffer->GetBufferArea(&button_area);
    text.FromChar("", 0);
    WnButton *button = UiGetWindowResources()->CreateFixedButton(kUiPetitionDismissId, text, buffer, &button_area, 0);
    if (button) {
      WnWindow *button_window = button->GetWindow();
      if (!buffer) {
        u32 color = UiGetWindowResources()->GetColor(kWnColorSilver);
        button_window->SetFillColor(color);
        button_window->SetSize(26, 25);
        button->SetButtonFlag(kWnButtonAutoFit, false);
      }
      button->SetButtonFlag(kWnButtonShowText, false);
      if (window->ChildAdd(button_window))
        button_window->GZWinMoveTo(19, 318);
      else
        window->GetWindowManager()->DestroyWindow(button_window);
      button->Release();
    }
    {
      UiTextRef dismiss(kRsGameDismissPetitionerText, kRsGameTextGroup);
      text = dismiss.GetText();
    }
    label = UiGetWindowResources()->CreateLabel(kUiPetitionPrimaryActionLabelId, text);
    if (label) {
      label->SetFont(font);
      label->GetWindow()->SetSize(62, 28);
      label->SetBackground(false);
      label->SetLabelFlag(kWnLabelWrapText, true);
      if (window->ChildAdd(label->GetWindow()))
        label->GetWindow()->GZWinMoveTo(56, 318);
      else {
        WnWindowManager *manager = window->GetWindowManager();
        manager->DestroyWindow(label->GetWindow());
      }
      label->Release();
    }
  }
  window_exists = 1;
  response_active = 0;
  return 1;
}
