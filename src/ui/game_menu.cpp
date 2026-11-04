#define _CRT_SECURE_NO_WARNINGS
#include "ui/game_menu.h"

#include <stdio.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "ui/commands.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"

// The menu descriptor rejects item counts above the original four-byte-array allocation limit.
enum UiMenuDescriptorLimit : u32 {
  kUiMenuDescriptorMaximumItemCount = 0x3fffffff,
};

// Registry menu-set parsing rejects the reserved all-ones identity as well as zero.
enum UiMenuSetIdentity : u32 {
  kUiInvalidMenuSetId = 0xffffffff,
};

// Deeper game-menu buttons use checkmark art; top-level artwork instances are derived from a separate menu-ID base.
enum UiGameMenuArtwork : u32 {
  kUiGameMenuLevelCheckmarkImageId = 0x5320e694,
  kUiGameMenuArtworkInstanceBase = 0x10000001,
};

// Panel levels select their button subclass; button levels select the next child panel.
enum UiGameMenuLevel : u32 {
  kUiGameMenuMainButtonLevel = 0x4287c741,
  kUiGameMenuSecondButtonLevel = 0x4287c742,
  kUiGameMenuMainPanelLevel = 0x287c760,
  kUiGameMenuSecondPanelLevel = 0x287c761,
  kUiGameMenuFurtherPanelLevel = 0x287c762,
};

UiGameMenuButton::UiGameMenuButton()
    : menu_flags(0), level(kUiGameMenuSecondButtonLevel), definition(nullptr), current_item(nullptr),
      root_menu(nullptr), parent_menu(nullptr), child_menu(nullptr), hint_token(0) {
}

UiGameMenuButton::~UiGameMenuButton() {
  UiGameMenuButton::Shutdown();
}

// Load the button definition and child items, then bind its title and hint before subscribing to item changes.
bool UiGameMenuButton::Init() {
  if (GetInitFlag())
    return true;
  UiGameMenuManager *manager = UiGameMenuPanelMain::GetMenuMgr();
  if (!manager)
    return false;
  ScMessageServer *server = UiGetMessageServer();
  if (!root_menu) {
    GetID();
    GetID();
    Shutdown();
    return false;
  }
  u32 button_id = GetID();
  if (manager->GetMenuBtnDef(button_id, &definition)) {
    u32 description_id = definition->GetMenuDescHn();
    CreateChildItemsEffects(description_id);
    SetCurrentMenuItem(definition->GetDefaultMenuItemID());
    ScStringView *text = nullptr;
    if (child_menu) {
      UiGameMenuDescriptor *description = nullptr;
      manager->GetMenuDesc(definition->GetMenuDescHn(), &description);
      if (description)
        description->GetMenuDescName(&hint);
      if (current_item) {
        current_item->GetMenuItemName(&text);
        if (text)
          caption.Copy(*text);
      }
    } else if (current_item) {
      current_item->GetMenuItemName(&text);
      if (text)
        caption.Copy(*text);
      text = nullptr;
      current_item->GetMenuItemTip(&text);
      if (text)
        hint.Copy(*text);
    }
  } else {
    GetID();
    GetID();
  }
  SetFlag(kWnFlagReorderable, false);
  if (server)
    server->AddNotification(this, kWnMsg_GameMenuItemEnabledChanged);
  return UiBaseWindow::Init();
}

// Forget borrowed panel pointers, hide the button and remove its item-change notification before base shutdown.
bool UiGameMenuButton::Shutdown() {
  root_menu = nullptr;
  parent_menu = nullptr;
  child_menu = nullptr;
  HideWindow();
  ScMessageServer *server = UiGetMessageServer();
  if (server && GetInitFlag())
    server->RemoveNotification(this, kWnMsg_GameMenuItemEnabledChanged);
  ShutdownButton();
  return UiBaseWindow::Shutdown();
}

bool UiGameMenuButton::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_GameMenuItemEnabledChanged)
    return UiBaseWindow::DoMessage(message);
  if (current_item && (u32)message->data1 == current_item->GetMenuItemID()) {
    SetFlag(kWnFlagEnabled, message->data2 != 0);
    InvalidateSelfAndParents();
  }
  return true;
}

// Move focus to the parent before hiding the currently focused button, then apply the original flag change.
u32 UiGameMenuButton::SetFlag(u32 flag, bool enabled) {
  if (flag == kWnFlagVisible && !enabled && GetFocus() == this)
    SetFocusTo(GetParentWin());
  return UiBaseWindow::SetFlag(flag, enabled);
}

bool UiGameMenuButton::OnKeyDown(u32 key, u32 modifiers) {
  ScMessage message(kWnMsg_KeyDown, key, modifiers, 0);
  return notification_target->DoMessage(&message);
}

bool UiGameMenuButton::OnKeyUp(u32 key, u32 modifiers) {
  ScMessage message(kWnMsg_KeyUp, key, modifiers, 0);
  return notification_target->DoMessage(&message);
}

// Clear press and hover flags and dismiss the hint when capture passes to another window.
bool UiGameMenuButton::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    SetMenuBtnFlag(16, false);
    SetMenuBtnFlag(4, false);
    ToggleTipEffects(false, false);
  }
  return true;
}

u8 UiGameMenuButton::SetMenuBtnFlag(i32 mask, bool value) {
  if (value)
    menu_flags |= (u32)mask;
  else
    menu_flags &= ~(u32)mask;
  InvalidateSelfAndParents();
  return 1;
}

i32 UiGameMenuButton::GetMenuBtnFlag(i32 mask) {
  return (menu_flags & (u32)mask) != 0;
}

// Mark the button open, select it in its parent panel and drop its child panel when one exists.
u8 UiGameMenuButton::ShowSubMenu() {
  SetMenuBtnFlag(1, true);
  parent_menu->SetCurrentSubMenuBtnID(GetID());
  if (child_menu)
    child_menu->OnDrop(absolute_area);
  return 1;
}

// Clear submenu flags, reset valid related windows and release capture with its hint when this button owns it.
u8 UiGameMenuButton::HideSubMenu() {
  SetMenuBtnFlag(1, false);
  SetMenuBtnFlag(2, false);
  if (IsWindowValid(parent_menu))
    parent_menu->SetCurrentSubMenuBtnID(0);
  if (IsWindowValid(child_menu))
    child_menu->HideWindow();
  if (owner->GetCapture() == this) {
    GZReleaseCapture();
    ToggleTipEffects(false, false);
    SetMenuBtnFlag(4, false);
  }
  return 1;
}

u8 UiGameMenuButton::MenuBtnAddSubItem(u32 item_id) {
  if (child_menu)
    return child_menu->AddMenuItem(item_id);
  if (CreateChildPanel())
    return child_menu->AddMenuItem(item_id);
  return 0;
}

u8 UiGameMenuButton::SetRootMenu(UiGameMenuPanel *panel) {
  root_menu = panel;
  return 1;
}

u8 UiGameMenuButton::SetParentMenu(UiGameMenuPanel *panel) {
  parent_menu = panel;
  return 1;
}

// Borrow the immediate parent panel associated with this button.
UiGameMenuPanel *UiGameMenuButton::GetParentMenu() const {
  return parent_menu;
}

// Borrow the child submenu panel, which may not yet have been created.
UiGameMenuPanel *UiGameMenuButton::GetChildMenu() const {
  return child_menu;
}

// Select a menu item through the real manager and update the existing child panel's checks on success.
u8 UiGameMenuButton::SetCurrentMenuItem(u32 item_id) {
  current_item = nullptr;
  if (!item_id)
    return 0;
  UiGameMenuManager *manager = UiGameMenuPanelMain::GetMenuMgr();
  if (!manager)
    return 0;
  if (!manager->GetMenuItemInfo(item_id, &current_item)) {
    GetID();
    GetID();
    return 0;
  }
  if (child_menu)
    child_menu->UpdateMenuItemCheckBoxes(item_id);
  return 1;
}

u32 UiGameMenuButton::GetCurrentMenuItemID() {
  return current_item ? current_item->GetMenuItemID() : 0;
}

u8 UiGameMenuButton::Activate() {
  if (!current_item)
    return 1;
  ScMessageServer *server = UiGetMessageServer();
  if (!server)
    return 0;
  u32 item_id = current_item->GetMenuItemID();
  ScMessage message(kWnMsg_GameMenuActivateItem, item_id, 0, 0);
  server->MessagePost(message, false);
  if (parent_menu) {
    UiGameMenuButton *button = parent_menu->GetParentMenuBtn();
    if (button && button->GetMenuBtnFlag(128))
      button->SetCurrentMenuItem(item_id);
  }
  return 1;
}

u8 UiGameMenuButton::ShutdownButton() {
  return 1;
}

// Create and attach the appropriate child panel, carrying its root, parent button and descriptor colors.
u8 UiGameMenuButton::CreateChildPanel() {
  if (child_menu)
    return 0;
  WnWindow *host;
  if (level == kUiGameMenuMainButtonLevel) {
    child_menu = new UiGameMenuPanelLevel2;
    host = GetParentWin();
  } else if (level == kUiGameMenuSecondButtonLevel) {
    child_menu = new UiGameMenuPanelLevelX;
    host = GetMainWindow();
  } else {
    return 0;
  }
  child_menu->AddRef();
  child_menu->HideWindow();
  child_menu->SetArea(*parent_menu->GetArea());
  child_menu->SetRootMenu(root_menu);
  child_menu->SetParentMenuBtn(this);
  u32 background = MakeFillColor(128, 128, 128), highlight = MakeFillColor(192, 192, 192);
  if (definition) {
    u32 descriptor_id = definition->GetMenuDescHn();
    UiGameMenuManager *manager = UiGameMenuPanelMain::GetMenuMgr();
    UiGameMenuDescriptor *description = nullptr;
    if (manager && manager->GetMenuDesc(descriptor_id, &description)) {
      description->GetMenuDescColors(&background, &highlight);
      background = MakeFillColor((u8)(background >> 16), (u8)(background >> 8), (u8)background);
      highlight = MakeFillColor((u8)(highlight >> 16), (u8)(highlight >> 8), (u8)highlight);
    }
  }
  child_menu->SetFillColor(background);
  child_menu->SetHiliteColor(highlight);
  if (!host->ChildAdd(child_menu)) {
    child_menu->Shutdown();
    child_menu->Release();
    child_menu = nullptr;
    return 0;
  }
  child_menu->Release();
  return 1;
}

// Ask the manager for every child ID and attempt each addition without stopping after a failure.
u8 UiGameMenuButton::CreateChildItemsEffects(u32 description_id) {
  UiGameMenuManager *manager = UiGameMenuPanelMain::GetMenuMgr();
  if (!manager)
    return 0;
  i32 count;
  const u32 *items;
  manager->GetMenuDescItemIDs(description_id, &count, &items);
  bool failed = false;
  for (i32 i = 0; i < count; ++i)
    failed |= MenuBtnAddSubItem(items[i]) == 0;
  return !failed;
}

u8 UiGameMenuButton::ToggleTipEffects(bool show, bool notify) {
  ScMessageServer *server = UiGetMessageServer();
  if (!server)
    return 0;
  if (show) {
    if (GetMenuBtnFlag(32)) {
      WnHint payload;
      payload.text = &hint;
      payload.rectangle = absolute_area;
      payload.flags |= 0x40003;
      ScMessage message(kWnMsg_StartHint, (uptr)&payload, 0, 0);
      server->MessageSend(message);
      hint_token = payload.token;
    }
  } else {
    ScMessage cancel(kWnMsg_StopHint, hint_token, 0, 0);
    server->MessageSend(cancel);
    if (notify) {
      ScMessage dismissed(kWnMsg_HintHidden, hint_token, 0, 0);
      server->MessagePost(dismissed, false);
    }
    hint_token = 0;
  }
  return 1;
}

static void UiPlayMenuSoundEffects(u32 sound) {
  if (UiGetApplication()) {
    ApBoxX *audio = UiGetBoxX();
    if (audio)
      audio->Event(kAuEventPlayEffect, sound, 0, 0, 0);
  }
}

// Toggle the main submenu, clearing the city view's active handler when a child submenu is involved.
bool UiGameMenuButtonMain::OnMouseDownL(i32, i32, u32) {
  UiPlayMenuSoundEffects(12);
  ToggleTipEffects(false, true);
  if (!IsWindowValid(parent_menu))
    return false;
  if (child_menu) {
    if (GetMenuBtnFlag(1))
      HideSubMenu();
    else {
      parent_menu->HideCurrentSubMenu();
      ShowSubMenu();
    }
    ApGameInterface *application = UiGetApplication();
    SpCityWindowView *view = application ? application->GetISC3WinCityView() : nullptr;
    if (view)
      view->SetActiveHandler(nullptr);
  } else {
    parent_menu->HideCurrentSubMenu();
  }
  return true;
}

// Activate a captured main button released inside its bounds, or release capture otherwise.
bool UiGameMenuButtonMain::OnMouseUpL(i32 x, i32 y, u32) {
  if (GZAmCapturing() && IsPointInWindow(x, y))
    Activate();
  else
    GZReleaseCapture();
  SetMenuBtnFlag(2, false);
  return true;
}

// Track main-button hover, capture and hint state while no press is active.
bool UiGameMenuButtonMain::OnMouseMove(i32 x, i32 y, u32) {
  if (menu_flags & 2)
    return true;
  if (IsPointInWindow(x, y)) {
    GZSetCapture();
    SetMenuBtnFlag(16, true);
    if (!(menu_flags & 4)) {
      UiPlayMenuSoundEffects(10);
      SetMenuBtnFlag(4, true);
      ToggleTipEffects(true, false);
    }
  } else {
    SetMenuBtnFlag(16, false);
    if (GetMenuBtnFlag(4)) {
      UiPlayMenuSoundEffects(10);
      SetMenuBtnFlag(4, false);
      ToggleTipEffects(false, false);
    }
    WindowToScreenCoordinates(&x, &y);
    GZReleaseCapture();
  }
  return true;
}

// Start a second-level press and toggle the child submenu through the parent panel.
bool UiGameMenuButtonLevel2::OnMouseDownL(i32, i32, u32) {
  UiPlayMenuSoundEffects(12);
  ToggleTipEffects(false, true);
  SetMenuBtnFlag(2, true);
  if (!parent_menu)
    return false;
  if (child_menu) {
    if (GetMenuBtnFlag(1))
      HideSubMenu();
    else {
      parent_menu->HideCurrentSubMenu();
      ShowSubMenu();
    }
  } else {
    parent_menu->HideCurrentSubMenu();
  }
  return true;
}

// Close open descendants and activate a released second-level item when its pointer and flags permit it.
bool UiGameMenuButtonLevel2::OnMouseUpL(i32 x, i32 y, u32) {
  if (root_menu)
    root_menu->HideSubMenus(false);
  if (!child_menu) {
    if (IsPointInWindow(x, y))
      Activate();
    else
      GZReleaseCapture();
  } else {
    if (IsPointInWindow(x, y) && GetMenuBtnFlag(128)) {
      HideSubMenu();
      Activate();
    }
    SetFocusTo(this);
  }
  SetMenuBtnFlag(2, false);
  return true;
}

// Maintain second-level hover and transfer capture when a held press moves into its child submenu.
bool UiGameMenuButtonLevel2::OnMouseMove(i32 x, i32 y, u32) {
  if (menu_flags & 2) {
    if (child_menu) {
      WindowToScreenCoordinates(&x, &y);
      WnWindow *window = GetWindowFromPoint(x, y);
      if (window != this && child_menu->IsDescendent(window))
        GZReleaseCapture();
    }
  } else if (IsPointInWindow(x, y)) {
    GZSetCapture();
    SetMenuBtnFlag(16, true);
    if (!GetMenuBtnFlag(4)) {
      UiPlayMenuSoundEffects(10);
      SetMenuBtnFlag(4, true);
      ToggleTipEffects(true, false);
    }
  } else {
    SetMenuBtnFlag(16, false);
    if (GetMenuBtnFlag(4)) {
      UiPlayMenuSoundEffects(10);
      SetMenuBtnFlag(4, false);
      ToggleTipEffects(false, false);
    }
    GZReleaseCapture();
  }
  return true;
}

// Close open descendants and activate a released deeper-menu item when its pointer and flags permit it.
bool UiGameMenuButtonLevelX::OnMouseUpL(i32 x, i32 y, u32) {
  if (root_menu)
    root_menu->HideSubMenus(false);
  if (!child_menu) {
    if (IsPointInWindow(x, y))
      Activate();
    else
      GZReleaseCapture();
  } else if (IsPointInWindow(x, y) && GetMenuBtnFlag(128)) {
    HideSubMenu();
    Activate();
  }
  SetMenuBtnFlag(2, false);
  return true;
}

// Maintain deeper-button hover and release capture when the pointer enters its parent panel's descendants.
bool UiGameMenuButtonLevelX::OnMouseMove(i32 x, i32 y, u32) {
  if (IsPointInWindow(x, y)) {
    GZSetCapture();
    SetMenuBtnFlag(16, true);
    if (!(menu_flags & 4)) {
      UiPlayMenuSoundEffects(10);
      SetMenuBtnFlag(4, true);
      ToggleTipEffects(true, false);
    }
  } else {
    SetMenuBtnFlag(16, false);
    if (GetMenuBtnFlag(4)) {
      UiPlayMenuSoundEffects(10);
      SetMenuBtnFlag(4, false);
      ToggleTipEffects(false, false);
    }
    if (parent_menu) {
      WindowToScreenCoordinates(&x, &y);
      WnWindow *window = GetWindowFromPoint(x, y);
      if (parent_menu->IsDescendent(window))
        GZReleaseCapture();
    }
  }
  return true;
}

// Original menu IDs and their vertical positions at 480, 600 and larger screen heights.
struct UiMenuArtPosition {
  u16 id, y480, y600, ylarge;
};

static const UiMenuArtPosition UiMenuArtPositions[] = {
    {0x2001, 21, 21, 118},
    {0x2002, 57, 57, 154},
    {0x2003, 93, 93, 190},
    {0x2004, 129, 129, 226},
    {0x2005, 165, 165, 262},
    {0x2006, 201, 201, 298},
    {0x3001, 21, 21, 118},
    {0x3002, 57, 57, 154},
    {0x3003, 93, 93, 190},
    {0x3004, 129, 129, 226},
    {0x3005, 165, 165, 262},
    {0x3006, 201, 201, 298},
    {0x3007, 237, 237, 334},
    {0x3008, 273, 273, 370},
    {0x4001, 21, 21, 118},
    {0x4002, 57, 57, 154},
    {0x4004, 93, 93, 190},
    {0x4005, 129, 129, 226},
    {0x4007, 165, 165, 262},
    {0x4009, 201, 201, 298},
    {0x400a, 237, 237, 334},
    {0x5001, 21, 21, 118},
    {0x5002, 57, 57, 154},
    {0x5003, 93, 93, 190},
    {0x5009, 129, 129, 226},
    {0x500a, 165, 165, 262},
    {0x500b, 201, 201, 298},
    {0x6002, 21, 21, 118},
    {0x6003, 57, 57, 154},
    {0x6004, 93, 93, 190},
    {0x6006, 129, 129, 226},
    {0x6008, 165, 165, 262},
    {0x6009, 201, 201, 298},
    {0x600a, 237, 237, 334},
    {0x8001, 129, 201, 298},
    {0x8002, 201, 273, 370},
    {0x8003, 237, 309, 406},
    {0x8004, 273, 345, 442},
    {0x8005, 165, 237, 334},
    {0x8010, 57, 129, 226},
    {0x8011, 93, 165, 262},
    {0x9001, 57, 128, 225},
    {0x9002, 129, 201, 298},
    {0x9003, 201, 273, 370},
    {0x9004, 237, 309, 406},
    {0x9005, 273, 345, 442},
    {0x9006, 165, 237, 334},
    {0x900a, 93, 164, 261},
    {0xa001, 93, 129, 226},
    {0xa002, 129, 165, 262},
    {0xa003, 165, 201, 298},
    {0xa004, 201, 237, 334},
    {0xa005, 273, 309, 406},
    {0xa006, 237, 273, 370},
};

// Select the installed resource group and button position for the current window height and menu ID.
u8 UiGameMenuButtonLevel2::GetMenuBtnLevel2Info(UiGameMenuButtonLevel2Info *out) {
  i32 screen_height = GetMainWindow()->GetH();
  out->width = 36;
  out->height = 36;
  out->flags = 32;
  out->level = kUiGameMenuSecondButtonLevel;
  out->instance = GetID();
  out->transparent = true;
  u32 menu_id = GetID();
  for (const UiMenuArtPosition &entry : UiMenuArtPositions) {
    if (entry.id != menu_id)
      continue;
    if (screen_height == 480) {
      out->group = kRsCompactUiImageGroup;
      out->y = entry.y480;
    } else if (screen_height == 600) {
      out->group = kRsMainUiImageGroup;
      out->y = entry.y600;
    } else if (screen_height > 600) {
      out->group = kRsMainUiImageGroup;
      out->y = entry.ylarge;
    } else {
      return 0;
    }
    out->x = 0;
    return 1;
  }
  return 0;
}

bool UiGameMenuButtonLevel2::Init() {
  if (GetInitFlag())
    return true;
  UiGameMenuButtonLevel2Info info{};
  if (GetMenuBtnLevel2Info(&info)) {
    menu_flags |= info.flags;
    level = info.level;
    RsKey key(0, info.group, info.instance);
    if (UiLoadGameMenuImage(key, &art, info.transparent)) {
      SetArea(info.x, info.y, info.x + info.width, info.y + info.height);
      return UiGameMenuButton::Init();
    }
  }
  Shutdown();
  return false;
}

UiGameMenuButtonMain::UiGameMenuButtonMain() : art{}, art_state{}, main_panel(nullptr) {
}

UiGameMenuButtonMain::~UiGameMenuButtonMain() {
  UiGameMenuButton::Shutdown();
}

u8 UiGameMenuButtonMain::ShowSubMenu() {
  if (main_panel)
    main_panel->SetActiveMenu((i32)GetID(), art[4], (i32)art_state[0], (i32)art_state[1]);
  UiGameMenuButton::ShowSubMenu();
  return 1;
}

u8 UiGameMenuButtonMain::HideSubMenu() {
  if (main_panel)
    main_panel->SetActiveMenu(0, nullptr, 0, 0);
  return UiGameMenuButton::HideSubMenu();
}

u8 UiGameMenuButtonMain::AutoSize() {
  return 1;
}

u8 UiGameMenuButtonMain::ShutdownButton() {
  for (i32 i = 0; i < 6; ++i) {
    if (art[i]) {
      art[i]->Release();
      art[i] = nullptr;
    }
  }
  return 1;
}

u8 UiGameMenuButtonMain::SetMainPanel(UiGameMenuPanelMain *panel) {
  main_panel = panel;
  return 1;
}

UiGameMenuButtonLevel2::UiGameMenuButtonLevel2() : art(nullptr) {
  SetMenuBtnFlag(128, true);
}

UiGameMenuButtonLevel2::~UiGameMenuButtonLevel2() {
  UiGameMenuButton::Shutdown();
}

u8 UiGameMenuButtonLevel2::AutoSize() {
  return 1;
}

u8 UiGameMenuButtonLevel2::ShutdownButton() {
  ScRelease(&art);
  return 1;
}

UiGameMenuButtonLevelX::UiGameMenuButtonLevelX()
    : art(nullptr), image_left(0), image_top(0), image_right(0), image_bottom(0), text_x(0), text_y(0), image_x(0),
      image_y(0), text_resource(nullptr), text_color(0) {
}

UiGameMenuButtonLevelX::~UiGameMenuButtonLevelX() {
  UiGameMenuButton::Shutdown();
}

// Retain the menu font and optional checkmark art before initializing the common button.
bool UiGameMenuButtonLevelX::Init() {
  if (GetInitFlag())
    return true;
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return false;
  text_resource = resources->GetFont(kWnFontUiSystem);
  if (!text_resource)
    return false;
  text_resource->AddRef();
  text_color = MakeFillColor(0, 0, 0);
  RsKey key(0, kRsMainUiImageGroup, kUiGameMenuLevelCheckmarkImageId);
  if (UiLoadGameMenuImage(key, &art, true)) {
    ScRect image_area;
    art->GetBufferArea(&image_area);
    image_left = (image_area.right - image_area.left) / 4;
    image_top = image_area.top;
    image_right = 2 * image_left;
    image_bottom = image_area.bottom;
  } else {
    art = nullptr;
    image_left = image_top = image_right = image_bottom = 0;
  }
  return UiGameMenuButton::Init();
}

bool UiGameMenuButtonLevelX::OnMouseDownL(i32, i32, u32) {
  return true;
}

// Measure the caption and art cell, then position both inside the deeper button's resulting bounds.
u8 UiGameMenuButtonLevelX::AutoSize() {
  if (!text_resource)
    return 0;
  const ScStringView *length_caption = GetCaption();
  const ScStringView *data_caption = GetCaption();
  ScRect text_bounds;
  u32 length = length_caption->Strlen();
  const char *data = data_caption->Data();
  text_resource->TextArea(data, length, &text_bounds);
  i32 text_height = text_bounds.bottom - text_bounds.top;
  i32 image_height = image_bottom - image_top;
  i32 height = (text_height > image_height ? text_height : image_height) + 2;
  SetSize(image_right - image_left + text_bounds.right - text_bounds.left + 12, height);
  image_x = 4;
  text_x = image_right - image_left + 8;
  text_y = (height + text_bounds.top - text_bounds.bottom) / 2;
  image_y = (height + image_top - image_bottom) / 2;
  return 1;
}

// Update the original hover colors and horizontal art coordinates after changing the requested flag.
u8 UiGameMenuButtonLevelX::SetMenuBtnFlag(i32 mask, bool enabled) {
  u8 result = UiGameMenuButton::SetMenuBtnFlag(mask, enabled);
  if (mask == 4) {
    u32 width = art ? art->Width() >> 2 : 0;
    if (enabled) {
      text_color = MakeFillColor(255, 255, 255);
      image_left = 3 * width;
      image_right = 4 * width;
    } else {
      text_color = MakeFillColor(0, 0, 0);
      image_left = width;
      image_right = 2 * width;
    }
  }
  return result;
}

u8 UiGameMenuButtonLevelX::ShutdownButton() {
  ScRelease(&text_resource);
  ScRelease(&art);
  return 1;
}

struct UiMainMenuPosition {
  i32 y480, y600, active480, active600, active_large;
};

// Main-button positions and active-art offsets for the installed screen-height variants.
static const UiMainMenuPosition UiMainMenuPositions[] = {
    {3, 3, 0, 0, 97},
    {39, 39, 11, 11, 108},
    {75, 75, 11, 11, 108},
    {111, 111, 11, 11, 108},
    {147, 147, 11, 11, 108},
    {219, 291, 0, 0, 97},
    {255, 327, 45, 118, 215},
    {291, 363, 0, 118, 215},
    {183, 219, 83, 119, 216},
};

// Select main-button art IDs and placement for the original nine button IDs and screen-height variants.
u8 UiGameMenuButtonMain::GetMenuBtnInfoMain(UiGameMenuButtonMainInfo *out) {
  i32 screen_height = GetMainWindow()->GetH();
  out->width = 56;
  out->height = 32;
  out->default_group = kRsMainUiImageGroup;
  out->flags = 32;
  out->level = kUiGameMenuMainButtonLevel;
  u32 menu_id = GetID();
  if (menu_id < 0x1001 || menu_id > 0x1009)
    return 0;
  const UiMainMenuPosition &position = UiMainMenuPositions[menu_id - 0x1001];
  if (screen_height == 480) {
    out->y = position.y480;
    out->active_y = position.active480;
  } else if (screen_height == 600) {
    out->y = position.y600;
    out->active_y = position.active600;
  } else if (screen_height > 600) {
    out->y = position.y600 + 97;
    out->active_y = position.active_large;
  } else {
    return 0;
  }
  u32 art_id = kUiGameMenuArtworkInstanceBase + ((menu_id - 0x1000) << 16);
  out->art[1].group = 0;
  out->art[1].instance = art_id;
  out->art[1].transparent = true;
  if (menu_id != 0x1006) {
    out->art[4].group = screen_height == 480 ? kRsCompactUiImageGroup : 0;
    out->art[4].instance = art_id + 2;
    out->art[4].transparent = false;
    out->art[5].group = 0;
    out->art[5].instance = art_id + 4;
    out->art[5].transparent = true;
  }
  out->x = 33;
  out->active_x = 0;
  return 1;
}

// Load the main button's available art entries and placement before initializing its common menu behavior.
bool UiGameMenuButtonMain::Init() {
  if (GetInitFlag())
    return true;
  UiGameMenuButtonMainInfo info{};
  if (GetMenuBtnInfoMain(&info)) {
    menu_flags |= info.flags;
    level = info.level;
    for (i32 i = 0; i < 6; ++i) {
      const UiGameMenuArtEntry &entry = info.art[i];
      u32 group = entry.group ? entry.group : info.default_group;
      if (entry.instance) {
        RsKey key(0, group, entry.instance);
        if (!UiLoadGameMenuImage(key, &art[i], entry.transparent)) {
          Shutdown();
          return false;
        }
      }
    }
    SetArea(info.x, info.y, info.x + info.width, info.y + info.height);
    art_state[0] = (u32)info.active_x;
    art_state[1] = (u32)info.active_y;
    return UiGameMenuButton::Init();
  }
  Shutdown();
  return false;
}

// Select the original main-button art for the pressed, hover and open states and draw it when available.
bool UiGameMenuButtonMain::GZPaint() {
  GxBuffer *image = nullptr;
  bool move_to_origin = false;
  if (menu_flags & 2) {
    image = art[2];
    move_to_origin = true;
  } else if (menu_flags & 4) {
    image = (menu_flags & 1) ? art[5] : art[1];
  } else {
    image = art[0];
  }
  if (!image)
    return true;
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  if (move_to_origin) {
    ScRect destination;
    image->GetBufferArea(&destination);
    i32 dx = draw_area.left - destination.left, dy = draw_area.top - destination.top;
    destination.right += dx;
    destination.bottom += dy;
    destination.left = draw_area.left;
    destination.top = draw_area.top;
    draw_buffer->Blt(image, *image->GetBufferArea(), destination, nullptr);
  } else {
    draw_buffer->Blt(image, *image->GetBufferArea(), draw_area, nullptr);
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Draw the second-level button's state-specific horizontal art cell into its current drawing area.
bool UiGameMenuButtonLevel2::GZPaint() {
  i32 height = GetH(), width = GetW();
  ScRect source = {0, 0, width, height};
  i32 frame = 0;
  if (GetFlag(kWnFlagEnabled)) {
    if (menu_flags & 2)
      frame = 2;
    else if (menu_flags & 4)
      frame = 3;
    else
      return true;
  }
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  if (frame) {
    i32 offset = frame * GetW();
    source.left += offset;
    source.right += offset;
  }
  draw_buffer->Blt(art, source, draw_area, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Fill the hover background, blit the checked art cell and draw the caption with the stored font color.
bool UiGameMenuButtonLevelX::GZPaint() {
  if (menu_flags & 4) {
    if (!draw_buffer->Lock(GxLockFlags::Fill))
      return false;
    if (parent_menu)
      draw_buffer->Fill(parent_menu->GetHiliteColor(), draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (GetMenuBtnFlag(64) && art) {
    if (!draw_buffer->Lock(GxLockFlags::Blt))
      return false;
    ScRect source = {image_left, image_top, image_right, image_bottom};
    ScRect destination = {draw_area.left + image_x, draw_area.top + image_y,
        draw_area.left + image_x + image_right - image_left, draw_area.top + image_y + image_bottom - image_top};
    draw_buffer->Blt(art, source, destination, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  text_resource->DrawTextA(draw_buffer, draw_area.left + text_x, draw_area.top + text_y, caption.begin,
      (u32)(caption.end - caption.begin), text_color);
  return true;
}

UiGameMenuDefinition::UiGameMenuDefinition() : identifier(0), references(0), default_item(0), description(0) {
}

UiGameMenuDefinition::~UiGameMenuDefinition() {
}

bool UiGameMenuDefinition::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiGameMenuDefinition::AddRef() {
  return ++references;
}

// Release the button definition and destroy it when its signed reference count reaches zero or below.
u32 UiGameMenuDefinition::Release() {
  references -= 1;
  if ((i32)references <= 0) {
    delete this;
    return 0;
  }
  return references;
}

u32 UiGameMenuDefinition::GetMenuBtnDefID() {
  return identifier;
}

u32 UiGameMenuDefinition::GetDefaultMenuItemID() {
  return default_item;
}

u32 UiGameMenuDefinition::GetMenuDescHn() {
  return description;
}

// Parse the definition identifier and two comma-delimited values, retaining the source's failure callback ordering.
u8 UiGameMenuDefinition::InitFromRegistry(const ScStringView &key, const ScStringView &value) {
  if (key.Strlen() && value.Strlen()) {
    identifier = ScConvertToUint32(key.ToChar());
    if (identifier) {
      ScString token, remaining(value);
      token = remaining.SplitToken(",");
      default_item = ScConvertToUint32(token.begin);
      if (default_item) {
        token = remaining.SplitToken(",");
        description = ScConvertToUint32(token.begin);
        return 1;
      }
    }
  }
  key.ToChar();
  value.ToChar();
  return 0;
}

UiGameMenuDescriptor::UiGameMenuDescriptor() : flags(0), references(0), identifier(0), count(0), items(nullptr) {
}

UiGameMenuDescriptor::~UiGameMenuDescriptor() {
  if (GetMenuDescFlag(1))
    UiGameMenuDescriptor::Shutdown();
}

bool UiGameMenuDescriptor::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiGameMenuDescriptor::AddRef() {
  return ++references;
}

// Release the descriptor when its signed reference count reaches zero or below.
u32 UiGameMenuDescriptor::Release() {
  references -= 1;
  if ((i32)references <= 0) {
    delete this;
    return 0;
  }
  return references;
}

u32 UiGameMenuDescriptor::GetMenuDescID() {
  return identifier;
}

// Publish the count and borrowed item array; an empty descriptor clears both outputs.
u8 UiGameMenuDescriptor::GetMenuDescItemIDs(i32 *out_count, const u32 **out_items) {
  *out_count = 0;
  *out_items = nullptr;
  if (count) {
    *out_count = count;
    *out_items = items;
  }
  return 1;
}

// Set or clear the requested descriptor bits without changing other flags.
u8 UiGameMenuDescriptor::SetMenuDescFlag(i32 mask, bool value) {
  if (value)
    flags |= (u32)mask;
  else
    flags &= ~(u32)mask;
  return 1;
}

i32 UiGameMenuDescriptor::GetMenuDescFlag(i32 mask) {
  return (flags & (u32)mask) != 0;
}

// Parse the descriptor's item IDs, colors and optional localized name before setting its initialized flag.
u8 UiGameMenuDescriptor::InitFromRegistry(const ScStringView &key, const ScStringView &value) {
  if (GetMenuDescFlag(1))
    return 1;
  ScString remaining(value), token;
  unsigned long parsed_id;
  if (sscanf(key.Data(), "0x%08lX", &parsed_id) != 1) {
    key.ToChar();
    value.ToChar();
    return 0;
  }
  identifier = (u32)parsed_id;
  token = remaining.SplitToken(",");
  u32 group = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  u32 instance = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  background_color = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  highlight_color = ScConvertToUint32(token.begin);
  token = remaining.SplitToken(",");
  count = (i32)ScConvertToUint32(token.begin);
  if (!count) {
    key.ToChar();
    value.ToChar();
    return 0;
  }
  if (count < 0 || (u32)count > kUiMenuDescriptorMaximumItemCount)
    ScFatal(SC_ERROR_INVALID, "SIMUI menu descriptor item count exceeds its defined allocation");
  items = new u32[(u32)count];
  for (i32 i = 0; i < count; ++i) {
    token = remaining.SplitToken(",");
    items[i] = ScConvertToUint32(token.begin);
  }
  if (group && instance) {
    UiTextRef text(instance, group);
    if (text.IsResolved()) {
      ScString localized(*text.GetResource());
      name = localized;
    }
  }
  SetMenuDescFlag(1, true);
  return 1;
}

// Release the stored item-ID array, clear its count and reset the initialized flag through the live method.
u8 UiGameMenuDescriptor::Shutdown() {
  if (items) {
    delete[] items;
    items = nullptr;
  }
  count = 0;
  SetMenuDescFlag(1, false);
  return 1;
}

u8 UiGameMenuDescriptor::GetMenuDescName(ScStringView *out) {
  out->Copy(name);
  return 1;
}

// Return the two stored colors in their original output order.
u8 UiGameMenuDescriptor::GetMenuDescColors(u32 *background, u32 *highlight) {
  *background = background_color;
  *highlight = highlight_color;
  return 1;
}

// Retain an image resource and optionally set the original magenta transparency color.
u8 UiLoadGameMenuImage(const RsKey &key, GxBuffer **out, bool transparent) {
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(key.instance, key.group, GxVramRequest::SystemMemory, format);
  *out = image.GetBuffer();
  if (!*out)
    return 0;
  (*out)->AddRef();
  if (transparent) {
    u16 color = (*out)->ConvertRGBValueToNative16Bit(255, 0, 255);
    (*out)->SetTransparency(color);
  }
  return 1;
}

UiGameMenuItem::UiGameMenuItem()
    : identifier(0), references(0), words12{}, version(0), command_type(0), command(), enabled(true) {
}

UiGameMenuItem::~UiGameMenuItem() {
}

bool UiGameMenuItem::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiGameMenuItem::AddRef() {
  return ++references;
}

// Release the item and destroy it once its signed reference count reaches zero or below.
u32 UiGameMenuItem::Release() {
  references -= 1;
  if ((i32)references <= 0) {
    delete this;
    return 0;
  }
  return references;
}

// Execute a command with its three original arguments, or send/post the configured message through the server.
u8 UiGameMenuItem::Activate() {
  if (!command.type)
    return 1;
  if (command_type == 1) {
    if (!UiGetFramework()->Application())
      return 0;
    ScCommandFactory *factory = nullptr;
    UiGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
    if (!factory)
      return 0;
    UiCommandInterface *action = nullptr;
    if (!factory->GetCommand(u32(command.type), IID_UiCommandInterface, reinterpret_cast<void **>(&action))) {
      factory->Release();
      return 0;
    }
    action->PushBackInput(new ScValue((u32)command.data1, kScValueUint32));
    action->PushBackInput(new ScValue((u32)command.data2, kScValueUint32));
    action->PushBackInput(new ScValue((u32)command.data3, kScValueUint32));
    action->Execute();
    action->Release();
    factory->Release();
    return 1;
  }
  if (command_type == 2) {
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      ScMessage message(command.type, command.data1, command.data2, 0);
      return server->MessagePost(message, false);
    }
  } else if (command_type == 3) {
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      ScMessage message(command.type, command.data1, command.data2, 0);
      return server->MessageSend(message);
    }
  }
  return 0;
}

// Dispatch the parsed registry version and retain the original byte-sized version field.
u8 UiGameMenuItem::InitFromRegistry(const ScStringView &key, const ScStringView &value) {
  long parsed_version = -1;
  if (sscanf(value.ToChar(), "%ld", &parsed_version) == 1) {
    version = (u8)parsed_version;
    if (version == 1)
      return InitVersion1(key, value);
    if (version == 2)
      return InitVersion2(key, value);
  }
  key.ToChar();
  value.ToChar();
  return 0;
}

u8 UiGameMenuItem::InitVersion2(const ScStringView &, const ScStringView &) {
  return 0;
}

// Parse version-one item fields and resolve its name and hint with the original fallback text resource.
u8 UiGameMenuItem::InitVersion1(const ScStringView &key, const ScStringView &value) {
  ScString remaining(value), token;
  identifier = ScConvertToUint32(key.ToChar());
  token = remaining.SplitToken(",");
  for (i32 i = 0; i < 4; ++i) {
    token = remaining.SplitToken(",");
    words12[i] = ScConvertToUint32(token.begin);
  }
  token = remaining.SplitToken(",");
  if (token.Strlen() == 4 && memcmp(token.Data(), "COMM", 4) == 0)
    command_type = 1;
  else if (token.Strlen() == 4 && memcmp(token.Data(), "PMSG", 4) == 0)
    command_type = 2;
  else if (token.Strlen() == 4 && memcmp(token.Data(), "SMSG", 4) == 0)
    command_type = 3;
  else {
    if (token.Strlen() != 4 || memcmp(token.Data(), "NONE", 4) != 0) {
      key.ToChar();
      value.ToChar();
    }
    command_type = 0;
  }
  if (command_type) {
    token = remaining.SplitToken(",");
    command.type = ScConvertToUint32(token.begin);
    token = remaining.SplitToken(",");
    command.data1 = ScConvertToUint32(token.begin);
    token = remaining.SplitToken(",");
    command.data2 = ScConvertToUint32(token.begin);
    if (command_type == 1) {
      token = remaining.SplitToken(",");
      command.data3 = ScConvertToUint32(token.begin);
    }
  } else {
    command.type = 0;
    command.data1 = command.data2 = command.data3 = 0;
  }
  UiTextRef name_text(words12[1], words12[0]);
  UiTextRef tip_text(words12[3], words12[2]);
  UiTextRef fallback(kRsCommonSimCity3000Text, kRsCommonTextGroup);
  if (name_text.IsResolved())
    name.Copy(*name_text.GetResource());
  else if (fallback.IsResolved())
    name.Copy(*fallback.GetResource());
  if (tip_text.IsResolved())
    tip.Copy(*tip_text.GetResource());
  else if (fallback.IsResolved())
    tip.Copy(*fallback.GetResource());
  return 1;
}

u32 UiGameMenuItem::GetMenuItemID() {
  return identifier;
}

u8 UiGameMenuItem::GetMenuItemName(ScStringView **out) {
  *out = &name;
  return 1;
}

u8 UiGameMenuItem::GetMenuItemTip(ScStringView **out) {
  *out = &tip;
  return 1;
}

u8 UiGameMenuItem::GetMenuItemVersion() {
  return version;
}

u8 UiGameMenuItem::GetMenuItemCommandType() {
  return command_type;
}

u8 UiGameMenuItem::GetMenuItemCommandData(ScMessage *out) {
  *out = command;
  return 1;
}

// Store enabled state and post the item-change notification through the source's two server lookups.
u8 UiGameMenuItem::SetEnabled(bool value) {
  enabled = value;
  if (UiGetMessageServer()) {
    ScMessage message(kWnMsg_GameMenuItemEnabledChanged, identifier, value, 0);
    UiGetMessageServer()->MessagePost(message, false);
  }
  return 1;
}

// Locate an ID in a sorted menu array without changing the retained object.
template <class T> static T *UiFindGameMenuEntry(const ScArray<UiGameMenuEntry<T>> &entries, u32 identifier) {
  for (u32 i = 0; i < entries.size; ++i) {
    if (entries.data[i].identifier == identifier)
      return entries.data[i].object;
    if (entries.data[i].identifier > identifier)
      break;
  }
  return nullptr;
}

// Insert a unique ID while retaining the original ordered traversal and borrowed input pointer.
template <class T> static u8 UiInsertGameMenuEntry(ScArray<UiGameMenuEntry<T>> *entries, u32 identifier, T *object) {
  u32 position = 0;
  while (position < entries->size && entries->data[position].identifier < identifier)
    ++position;
  if (position < entries->size && entries->data[position].identifier == identifier)
    return 0;
  entries->Append();
  for (u32 i = entries->size - 1; i > position; --i)
    entries->data[i] = entries->data[i - 1];
  entries->data[position] = {identifier, object};
  return 1;
}

// Remove the first ordered entry after its original shutdown and release callbacks have returned.
template <class T> static void UiRemoveFirstGameMenuEntry(ScArray<UiGameMenuEntry<T>> *entries) {
  --entries->size;
  for (u32 i = 0; i < entries->size; ++i)
    entries->data[i] = entries->data[i + 1];
}

UiGameMenuManagerImpl::UiGameMenuManagerImpl()
    : initialized(false), references(0), active_set(nullptr), cached_item_ids(nullptr) {
}

UiGameMenuManagerImpl::~UiGameMenuManagerImpl() {
  UiGameMenuManagerImpl::Shutdown();
}

// Publish the manager identity for its two original interface IDs and Unknown.
bool UiGameMenuManagerImpl::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_UiGameMenuManager && iid != IID_ScMessageTarget && iid != IID_ScUnknown))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiGameMenuManager *>(this);
  AddRef();
  return true;
}

u32 UiGameMenuManagerImpl::AddRef() {
  return ++references;
}

// Destroy the manager when its signed reference count reaches zero or below.
u32 UiGameMenuManagerImpl::Release() {
  --references;
  if ((i32)references <= 0) {
    delete this;
    return 0;
  }
  return references;
}

// Dispatch item activation, individual enable changes and the all-items enable notification.
bool UiGameMenuManagerImpl::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_GameMenuActivateItem || message->type == kWnMsg_GameMenuSetItemEnabled) {
    UiGameMenuItem *item = UiFindGameMenuEntry(item_map, (u32)message->data1);
    if (!item)
      return false;
    if (message->type == kWnMsg_GameMenuActivateItem)
      return item->Activate() != 0;
    item->SetEnabled(message->data2 != 0);
    return true;
  }
  if (message->type == kWnMsg_GameMenuSetAllEnabled)
    return EnableAllItems(message->data1 != 0) != 0;
  return false;
}

// Enumerate the four original registry categories, select set 4096 and subscribe to item-change messages.
u8 UiGameMenuManagerImpl::Init() {
  if (initialized)
    return 1;
  initialized = true;
  ScRegistry *registry = nullptr;
  UiGetFramework()->GetSystemService(kScRegistryServiceId, IID_ScRegistry, reinterpret_cast<void **>(&registry));
  bool success = registry && registry->EnumCategory("SC3MII", AddItemFromRegistry, this) &&
      registry->EnumCategory("SC3MDESC", AddDescriptorFromRegistry, this) &&
      registry->EnumCategory("SC3MSET", AddSetFromRegistry, this) &&
      registry->EnumCategory("SC3MBTNDEFS", AddDefinitionFromRegistry, this) && SetActiveMenuSet(4096);
  if (success) {
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      server->AddNotification(this, kWnMsg_GameMenuActivateItem);
      server->AddNotification(this, kWnMsg_GameMenuSetItemEnabled);
      server->AddNotification(this, kWnMsg_GameMenuSetAllEnabled);
    }
  }
  if (registry)
    registry->Release();
  return success;
}

// Remove notifications and release item, set, descriptor and definition collections in their original order.
u8 UiGameMenuManagerImpl::Shutdown() {
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->RemoveNotification(this, kWnMsg_GameMenuActivateItem);
    server->RemoveNotification(this, kWnMsg_GameMenuSetItemEnabled);
    server->RemoveNotification(this, kWnMsg_GameMenuSetAllEnabled);
  }
  while (item_map.size) {
    if (item_map.data[0].object)
      item_map.data[0].object->Release();
    UiRemoveFirstGameMenuEntry(&item_map);
  }
  while (set_map.size) {
    UiGameMenuSet *set = set_map.data[0].object;
    if (set) {
      set->Shutdown();
      set->Release();
    }
    UiRemoveFirstGameMenuEntry(&set_map);
  }
  while (description_map.size) {
    UiGameMenuDescriptor *description = description_map.data[0].object;
    if (description) {
      description->Shutdown();
      description->Release();
    }
    UiRemoveFirstGameMenuEntry(&description_map);
  }
  while (definition_map.size) {
    if (definition_map.data[0].object)
      definition_map.data[0].object->Release();
    UiRemoveFirstGameMenuEntry(&definition_map);
  }
  delete[] cached_item_ids;
  cached_item_ids = nullptr;
  initialized = false;
  return 1;
}

u8 UiGameMenuManagerImpl::Requery() {
  return 0;
}

u32 UiGameMenuManagerImpl::GetMenuItemCount() {
  return item_map.size;
}

// Publish a lazily cached array of item IDs in the original unsigned-key order.
u8 UiGameMenuManagerImpl::GetAllMenuItemIDs(i32 *count, const u32 **items) {
  *count = 0;
  *items = nullptr;
  if (!item_map.size)
    return 0;
  if (!cached_item_ids) {
    cached_item_ids = new u32[item_map.size];
    for (u32 i = 0; i < item_map.size; ++i)
      cached_item_ids[i] = item_map.data[i].identifier;
  }
  *count = (i32)item_map.size;
  *items = cached_item_ids;
  return 1;
}

u8 UiGameMenuManagerImpl::GetMenuItemInfo(u32 identifier, UiGameMenuItem **out) {
  *out = UiFindGameMenuEntry(item_map, identifier);
  return *out != nullptr;
}

u8 UiGameMenuManagerImpl::AddMenuItemInfo(UiGameMenuItem *item) {
  return item ? UiInsertGameMenuEntry(&item_map, item->GetMenuItemID(), item) : 0;
}

// Forward the enabled state to every registered menu item in identifier order.
u8 UiGameMenuManagerImpl::EnableAllItems(bool enabled) {
  for (u32 i = 0; i < item_map.size; ++i)
    item_map.data[i].object->SetEnabled(enabled);
  return 1;
}

u8 UiGameMenuManagerImpl::AddMenuSet(UiGameMenuSet *set) {
  return set ? UiInsertGameMenuEntry(&set_map, set->GetMenuSetID(), set) : 0;
}

u8 UiGameMenuManagerImpl::GetActiveMenuSet(UiGameMenuSet **out) {
  *out = active_set;
  return active_set != nullptr;
}

u8 UiGameMenuManagerImpl::GetSpecificMenuSet(u32 identifier, UiGameMenuSet **out) {
  *out = UiFindGameMenuEntry(set_map, identifier);
  return *out != nullptr;
}

// Select an existing set while leaving the previous selection intact if its identifier is absent.
u8 UiGameMenuManagerImpl::SetActiveMenuSet(u32 identifier) {
  UiGameMenuSet *set = UiFindGameMenuEntry(set_map, identifier);
  if (!set)
    return 0;
  active_set = set;
  return 1;
}

u8 UiGameMenuManagerImpl::AddMenuDesc(UiGameMenuDescriptor *description) {
  return description ? UiInsertGameMenuEntry(&description_map, description->GetMenuDescID(), description) : 0;
}

// Resolve a descriptor through the active set and publish its borrowed item-ID array.
u8 UiGameMenuManagerImpl::GetMenuDescItemIDs(u32 identifier, i32 *count, const u32 **items) {
  *count = 0;
  *items = nullptr;
  UiGameMenuDescriptor *description = nullptr;
  if (active_set && active_set->GetMenuDesc(identifier, &description))
    return description->GetMenuDescItemIDs(count, items);
  return 0;
}

u8 UiGameMenuManagerImpl::GetMenuDesc(u32 identifier, UiGameMenuDescriptor **out) {
  *out = nullptr;
  return active_set ? active_set->GetMenuDesc(identifier, out) : 0;
}

u8 UiGameMenuManagerImpl::GetSpecificMenuDesc(u32 identifier, UiGameMenuDescriptor **out) {
  *out = UiFindGameMenuEntry(description_map, identifier);
  return *out != nullptr;
}

u8 UiGameMenuManagerImpl::AddMenuBtnDef(UiGameMenuDefinition *definition) {
  return definition ? UiInsertGameMenuEntry(&definition_map, definition->GetMenuBtnDefID(), definition) : 0;
}

u8 UiGameMenuManagerImpl::GetMenuBtnDef(u32 identifier, UiGameMenuDefinition **out) {
  *out = UiFindGameMenuEntry(definition_map, identifier);
  return *out != nullptr;
}

// Construct and register one item from the registry, releasing it if its original parser fails.
void UiGameMenuManagerImpl::AddItemFromRegistry(const ScStringView &key, const ScStringView &value, void *context) {
  if (!context)
    return;
  UiGameMenuItem *item = new UiGameMenuItem;
  item->AddRef();
  if (item->InitFromRegistry(key, value))
    static_cast<UiGameMenuManagerImpl *>(context)->AddMenuItemInfo(item);
  else
    item->Release();
}

// Construct and register one descriptor, shutting it down first when registry initialization fails.
void UiGameMenuManagerImpl::AddDescriptorFromRegistry(
    const ScStringView &key, const ScStringView &value, void *context) {
  if (!context)
    return;
  UiGameMenuDescriptor *description = new UiGameMenuDescriptor;
  description->AddRef();
  if (description->InitFromRegistry(key, value))
    static_cast<UiGameMenuManagerImpl *>(context)->AddMenuDesc(description);
  else {
    description->Shutdown();
    description->Release();
  }
}

// Construct a menu set, connect its manager and register it only when its configuration loads successfully.
void UiGameMenuManagerImpl::AddSetFromRegistry(const ScStringView &key, const ScStringView &value, void *context) {
  if (!context)
    return;
  UiGameMenuManagerImpl *owner = static_cast<UiGameMenuManagerImpl *>(context);
  UiGameMenuSetImpl *set = new UiGameMenuSetImpl;
  set->SetMenuMgr(owner);
  set->AddRef();
  if (set->InitFromRegistry(key, value))
    owner->AddMenuSet(set);
  else
    set->Release();
}

// Construct and register one button definition, releasing it if its registry data is invalid.
void UiGameMenuManagerImpl::AddDefinitionFromRegistry(
    const ScStringView &key, const ScStringView &value, void *context) {
  if (!context)
    return;
  UiGameMenuDefinition *definition = new UiGameMenuDefinition;
  definition->AddRef();
  if (definition->InitFromRegistry(key, value))
    static_cast<UiGameMenuManagerImpl *>(context)->AddMenuBtnDef(definition);
  else
    definition->Release();
}

// Create the original zero-reference menu manager for the director's registered class factory.
ScUnknown *UiCreateClass82F622C1() {
  return new UiGameMenuManagerImpl;
}

UiGameMenuManager *UiGameMenuPanelMain::manager = nullptr;

// Borrow the manager retained by the main game-menu panel.
UiGameMenuManager *UiGameMenuPanelMain::GetMenuMgr() {
  return manager;
}

// Initialize the panel's empty circular button list and set the original default menu flag.
UiGameMenuPanel::UiGameMenuPanel()
    : menu_flags(8), current_button(0), level(0), buttons(new UiMenuNode), root_menu(nullptr), parent_button(nullptr) {
  buttons->next = buttons;
  buttons->previous = buttons;
}

UiGameMenuPanel::~UiGameMenuPanel() {
  UiGameMenuPanel::Shutdown();
  delete buttons;
}

// Publish the original secondary menu-panel identity and delegate other interface requests to the window base.
bool UiGameMenuPanel::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiGameMenuPanelInterface)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiGameMenuPanelInterface *>(this);
  AddRef();
  return true;
}

u32 UiGameMenuPanel::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiGameMenuPanel::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window corresponding to this panel's secondary menu interface.
WnWindow *UiGameMenuPanel::AsIGZWin() {
  return this;
}

u32 UiGameMenuPanel::GetCurrentSubMenuBtnID() {
  return current_button;
}

// Ask the menu manager for the descriptor's item IDs and add every entry, even after a preceding addition fails.
u8 UiGameMenuPanel::AddMenuItems(u32 menu_id) {
  if (!menu_id) {
    GetID();
    caption.ToChar();
    return 0;
  }
  UiGameMenuManager *menu_manager = UiGameMenuPanelMain::GetMenuMgr();
  if (!menu_manager)
    return 0;
  i32 count;
  const u32 *items;
  if (!menu_manager->GetMenuDescItemIDs(menu_id, &count, &items))
    return 0;
  bool failed = false;
  for (i32 i = 0; i < count; ++i)
    failed |= AddMenuItem(items[i]) == 0;
  return !failed;
}

// Find the requested button in the live root panel, hide existing submenus and open the selected button's submenu.
u8 UiGameMenuPanel::ShowSubMenuOfBtn(u32 button_id) {
  if (!IsWindowValid(root_menu))
    return 0;
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    if (button->GetID() == button_id) {
      root_menu->HideSubMenus(true);
      button->ShowSubMenu();
      return 1;
    }
  }
  return 0;
}

// Register hint notifications and enable the panel's original window flag before base initialization.
bool UiGameMenuPanel::Init() {
  if (GetInitFlag())
    return true;
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->AddNotification(this, kWnMsg_HintShown);
    server->AddNotification(this, kWnMsg_HintHidden);
  }
  SetFlag(kWnFlagPrivateBuffer, true);
  return UiBaseWindow::Init();
}

// Discard the borrowed button list, hide the panel and remove the two notifications before base shutdown.
bool UiGameMenuPanel::Shutdown() {
  UiMenuNode *node = buttons->next;
  while (node != buttons) {
    UiMenuNode *next = node->next;
    delete node;
    node = next;
  }
  buttons->next = buttons;
  buttons->previous = buttons;
  HideWindow();
  if (GetInitFlag()) {
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      server->RemoveNotification(this, kWnMsg_HintShown);
      server->RemoveNotification(this, kWnMsg_HintHidden);
    }
  }
  ShutdownMenu();
  return UiBaseWindow::Shutdown();
}

// Enable or clear the hint state on its two notifications, delegating all other messages to the window base.
bool UiGameMenuPanel::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_HintShown) {
    SetMenuPanelFlag(4, true);
    return true;
  }
  if (message->type == kWnMsg_HintHidden) {
    SetMenuPanelFlag(4, false);
    return true;
  }
  return UiBaseWindow::DoMessage(message);
}

u32 UiGameMenuPanel::SetFlag(u32 flag, bool enabled) {
  if (flag == kWnFlagVisible && !enabled) {
    HideSubMenus(true);
    if (GetFocus() == this)
      SetFocusTo(GetParentWin());
  }
  return UiBaseWindow::SetFlag(flag, enabled);
}

bool UiGameMenuPanel::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiGameMenuPanel::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

u8 UiGameMenuPanel::OnDrop(const ScRect &) {
  SetFlag(kWnFlagReorderable, true);
  PullToFront();
  SetFlag(kWnFlagReorderable, false);
  ShowWindow();
  return 1;
}

// Create the appropriate button subclass, attach it as a child and retain its borrowed list entry on success.
u8 UiGameMenuPanel::AddMenuItem(u32 item_id) {
  if (ChildExists(item_id)) {
    GetID();
    caption.ToChar();
    return 0;
  }
  UiGameMenuButton *button = nullptr;
  if (level == kUiGameMenuMainPanelLevel)
    button = new UiGameMenuButtonMain;
  else if (level == kUiGameMenuSecondPanelLevel)
    button = new UiGameMenuButtonLevel2;
  else if (level == kUiGameMenuFurtherPanelLevel)
    button = new UiGameMenuButtonLevelX;
  else {
    GetID();
    caption.ToChar();
    return 0;
  }
  button->AddRef();
  button->SetID(item_id);
  button->SetRootMenu(root_menu);
  button->SetParentMenu(this);
  bool added = ChildAdd(button);
  button->Release();
  if (added) {
    UiMenuNode *node = new UiMenuNode;
    node->button = button;
    node->next = buttons;
    node->previous = buttons->previous;
    buttons->previous->next = node;
    buttons->previous = node;
  }
  return added;
}

// Find the named menu button and pass the subitem identifier to its original child-item operation.
u8 UiGameMenuPanel::AddSubItem(u32 button_id, u32 child_id) {
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    if (button && button->GetID() == button_id)
      return button->MenuBtnAddSubItem(child_id);
  }
  return 0;
}

// Set or clear exactly the requested menu-specific bits, leaving unrelated panel flags untouched.
u8 UiGameMenuPanel::SetMenuPanelFlag(i32 mask, bool enabled) {
  if (enabled)
    menu_flags |= (u32)mask;
  else
    menu_flags &= ~(u32)mask;
  return 1;
}

i32 UiGameMenuPanel::GetMenuPanelFlag(i32 mask) {
  return ((u32)mask & menu_flags) != 0;
}

u32 UiGameMenuPanel::GetHiliteColor() {
  return 0;
}

u8 UiGameMenuPanel::SetHiliteColor(u32) {
  return 0;
}

u8 UiGameMenuPanel::SetRootMenu(UiGameMenuPanel *panel) {
  root_menu = panel;
  return 1;
}

u8 UiGameMenuPanel::SetParentMenuBtn(UiGameMenuButton *button) {
  parent_button = button;
  return 1;
}

// Return the borrowed parent button without changing its reference count.
UiGameMenuButton *UiGameMenuPanel::GetParentMenuBtn() {
  return parent_button;
}

// Recursively hide child menus and the requested button menus, then restore panel focus and release owned capture.
u8 UiGameMenuPanel::HideSubMenus(bool all) {
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    UiGameMenuPanel *child = button->GetChildMenu();
    if (child)
      child->HideSubMenus(false);
    if (all || (menu_flags & 8))
      button->HideSubMenu();
  }
  if (owner->GetCapture() == this)
    GZReleaseCapture();
  SetFocusTo(this);
  return 1;
}

// Find the active button, close its descendant submenu first and then hide the button's own submenu.
u8 UiGameMenuPanel::HideCurrentSubMenu() {
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    if (button->GetID() == current_button) {
      UiGameMenuPanel *child = button->GetChildMenu();
      if (child)
        child->HideCurrentSubMenu();
      button->HideSubMenu();
      return 1;
    }
  }
  return 0;
}

u8 UiGameMenuPanel::SetCurrentSubMenuBtnID(u32 button_id) {
  current_button = button_id;
  return 1;
}

// Test visible panels, their buttons and nested menus for the supplied window identity.
u8 UiGameMenuPanel::IsDescendent(WnWindow *window) {
  if (!GetFlag(kWnFlagVisible))
    return 0;
  if (window == this)
    return 1;
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    if (button == window || button->GetParentWin() == window)
      return 1;
    UiGameMenuPanel *child = button->GetChildMenu();
    if (child && child->IsDescendent(window))
      return 1;
  }
  return 0;
}

u8 UiGameMenuPanel::UpdateMenuItemCheckBoxes(u32 item_id) {
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    button->SetMenuBtnFlag(64, button->GetCurrentMenuItemID() == item_id);
  }
  return 1;
}

u8 UiGameMenuPanel::ShutdownMenu() {
  return 1;
}

// Select the panel kind that constructs second-level buttons, then enable its two original window flags.
UiGameMenuPanelLevel2::UiGameMenuPanelLevel2() {
  level = kUiGameMenuSecondPanelLevel;
  SetFlag(kWnFlagTransparentBuffer, true);
  SetFlag(kWnFlagClearTransparentBuffer, true);
}

UiGameMenuPanelLevel2::~UiGameMenuPanelLevel2() {
  UiGameMenuPanel::Shutdown();
}

// Size the panel to its first valid button and the root menu's absolute vertical area.
// The category button's short local rectangle would exclude the lower buttons from cursor routing.
u8 UiGameMenuPanelLevel2::AutoSize() {
  if (buttons->next != buttons) {
    const ScRect *root_area = root_menu->GetAreaAbsolute();
    i32 left = root_area->left, top = root_area->top, bottom = root_area->bottom;
    UiGameMenuButton *button = buttons->next->button;
    if (IsWindowValid(button)) {
      SetArea(left, top, left + button->GetW(), bottom);
      return 1;
    }
  }
  SetArea(0, 0, 0, 0);
  return 1;
}

// Resize, bring forward and show the second-level panel at the original origin.
u8 UiGameMenuPanelLevel2::OnDrop(const ScRect &) {
  AutoSize();
  SetFlag(kWnFlagReorderable, true);
  PullToFront();
  SetFlag(kWnFlagReorderable, false);
  GZWinMoveTo(0, 0);
  ShowWindow();
  return 1;
}

UiGameMenuPanelLevelX::UiGameMenuPanelLevelX() : highlight(0) {
  level = kUiGameMenuFurtherPanelLevel;
}

UiGameMenuPanelLevelX::~UiGameMenuPanelLevelX() {
  UiGameMenuPanel::Shutdown();
}

bool UiGameMenuPanelLevelX::Init() {
  HideWindow();
  return UiGameMenuPanel::Init();
}

bool UiGameMenuPanelLevelX::GZPaint() {
  u32 color = GetFillColor();
  draw_buffer->DrawRaisedRect(draw_area, color, true, true);
  return true;
}

// Close the root panel's open descendants and release capture after a left-button release.
bool UiGameMenuPanelLevelX::OnMouseUpL(i32, i32, u32) {
  root_menu->HideSubMenus(false);
  GZReleaseCapture();
  return true;
}

// Transfer capture according to the window under the pointer and this panel's descendant tree.
bool UiGameMenuPanelLevelX::OnMouseMove(i32 x, i32 y, u32) {
  WindowToScreenCoordinates(&x, &y);
  WnWindow *window = GetWindowFromPoint(x, y);
  if (window == this)
    GZSetCapture();
  else if (IsDescendent(window))
    GZReleaseCapture();
  return true;
}

// Size and arrange visible buttons vertically, or use the original empty-panel dimensions.
u8 UiGameMenuPanelLevelX::AutoSize() {
  if (buttons->next == buttons) {
    SetArea(0, 0, 64, 64);
    return 0;
  }
  i32 width = 64, height = 2, y = 2;
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    if (button->GetFlag(kWnFlagVisible)) {
      button->AutoSize();
      if (width < button->GetW())
        width = button->GetW();
      height += button->GetH();
    }
  }
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next) {
    UiGameMenuButton *button = node->button;
    if (button->GetFlag(kWnFlagVisible)) {
      button->SetArea(2, y, width + 2, y + button->GetH());
      y += button->GetH();
    }
  }
  SetSize(width + 4, height + 2);
  return 1;
}

// Place the resized panel to the left of the anchor and invoke the shared drop behavior.
u8 UiGameMenuPanelLevelX::OnDrop(const ScRect &anchor) {
  AutoSize();
  GZWinMoveTo(anchor.left - GetW() + 1, anchor.top - 1);
  return UiGameMenuPanel::OnDrop(anchor);
}

u32 UiGameMenuPanelLevelX::GetHiliteColor() {
  return highlight;
}

u8 UiGameMenuPanelLevelX::SetHiliteColor(u32 color) {
  highlight = color;
  return 1;
}

static void UiMoveMenuRect(ScRect *rectangle, i32 x, i32 y) {
  i32 dx = x - rectangle->left, dy = y - rectangle->top;
  rectangle->right += dx;
  rectangle->bottom += dy;
  rectangle->left = x;
  rectangle->top = y;
}

UiGameMenuPanelMain::UiGameMenuPanelMain()
    : art{}, art_rects{}, art_flags{}, active_art(nullptr), active_rect{}, active_x(0), active_y(0) {
  root_menu = this;
  menu_flags = (menu_flags & ~8u) | 1;
  level = kUiGameMenuMainPanelLevel;
}

UiGameMenuPanelMain::~UiGameMenuPanelMain() {
  UiGameMenuPanel::Shutdown();
}

// Create the menu manager and load the main panel art for the current screen height.
bool UiGameMenuPanelMain::Init() {
  if (GetInitFlag())
    return true;
  UiGetCom()->GetClassObject(kUiGameMenuManagerClassId, IID_UiGameMenuManager, reinterpret_cast<void **>(&manager));
  if (!manager->Init())
    return false;
  RsKey base, extra;
  i32 screen_height = GetMainWindow()->GetH();
  if (screen_height == 480) {
    base.group = kRsCompactUiImageGroup;
    art_flags[0] = 0;
    art_flags[1] = 0;
    art_flags[2] = 0;
  } else if (screen_height == 600) {
    base.group = kRsMainUiImageGroup;
    extra.group = kRsMainUiImageGroup;
    art_flags[0] = 0;
    art_flags[1] = 1;
    art_flags[2] = 0;
  } else if (screen_height > 600) {
    base.group = kRsMainUiImageGroup;
    extra.group = kRsLargeUiImageGroup;
    art_flags[0] = 1;
    art_flags[1] = 1;
    art_flags[2] = screen_height > 768;
  } else {
    SetInitFlag(true);
    Shutdown();
    return false;
  }
  base.instance = 0xffff;
  bool loaded = UiLoadGameMenuImage(base, &art[0], false) != 0;
  if (loaded && art_flags[0]) {
    extra.instance = 0xfffd;
    loaded = UiLoadGameMenuImage(extra, &art[1], false) != 0;
  }
  if (loaded && art_flags[1]) {
    extra.instance = 0xfffe;
    loaded = UiLoadGameMenuImage(extra, &art[2], false) != 0;
  }
  if (loaded && art_flags[2]) {
    extra.instance = 0xfffc;
    loaded = UiLoadGameMenuImage(extra, &art[3], false) != 0;
  }
  if (!loaded) {
    SetInitFlag(true);
    Shutdown();
    return false;
  }
  if (!GetW())
    SetW((i32)art[0]->Width());
  if (!GetH())
    SetH((i32)art[0]->Height());
  return UiGameMenuPanel::Init();
}

u8 UiGameMenuPanelMain::AddMenuItems(u32 menu_id) {
  if (!UiGameMenuPanel::AddMenuItems(menu_id))
    return 0;
  for (UiMenuNode *node = buttons->next; node != buttons; node = node->next)
    static_cast<UiGameMenuButtonMain *>(node->button)->SetMainPanel(this);
  return 1;
}

// Fit the main panel to its art and place the retained background and active-menu rectangles.
void UiGameMenuPanelMain::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = right - left, height = bottom - top, minimum_height = 0;
  if (art[0]) {
    width = (i32)art[0]->Width();
    minimum_height = (i32)art[0]->Height();
  }
  if (art[1])
    minimum_height += (i32)art[1]->Height();
  if (art[2])
    minimum_height += (i32)art[2]->Height();
  if (height < minimum_height)
    height = minimum_height;
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;
  WnRootWidget::SetArea(left, top, left + width, top + height);
  i32 upper_height = 0, middle_bottom = 0, lower_top = 0;
  if (art[1]) {
    art[1]->GetBufferArea(&art_rects[1]);
    UiMoveMenuRect(&art_rects[1], draw_area.left, draw_area.top);
    upper_height = art_rects[1].bottom - art_rects[1].top;
  }
  if (art[0]) {
    art[0]->GetBufferArea(&art_rects[0]);
    UiMoveMenuRect(&art_rects[0], draw_area.left, draw_area.top + upper_height);
    middle_bottom = art_rects[0].bottom;
  }
  if (art[2]) {
    art[2]->GetBufferArea(&art_rects[2]);
    i32 lower_height = (i32)art[2]->Height();
    UiMoveMenuRect(&art_rects[2], draw_area.left, draw_area.bottom - lower_height);
    lower_top = art_rects[2].top;
  }
  if (art[3]) {
    art[3]->GetBufferArea(&art_rects[3]);
    art_rects[3].left += draw_area.left;
    art_rects[3].right += draw_area.left;
    art_rects[3].top = middle_bottom;
    art_rects[3].bottom = lower_top;
  }
  if (active_art) {
    active_art->GetBufferArea(&active_rect);
    UiMoveMenuRect(&active_rect, draw_area.left + active_x, draw_area.top + active_y);
  }
}

// Draw the main panel's top, body, optional tiled fill, bottom and active menu art in original order.
bool UiGameMenuPanelMain::GZPaint() {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  if (art[1])
    draw_buffer->Blt(art[1], *art[1]->GetBufferArea(), art_rects[1], nullptr);
  draw_buffer->Blt(art[0], *art[0]->GetBufferArea(), art_rects[0], nullptr);
  if (art[3]) {
    i32 tile_height = (i32)art[3]->Height();
    ScRect &destination = art_rects[3];
    if (tile_height >= destination.bottom - destination.top) {
      ScRect source = {0, 0, destination.right - destination.left, destination.bottom - destination.top};
      draw_buffer->Blt(art[3], source, destination, nullptr);
    } else {
      ScRect tile = {destination.left, destination.top, destination.right, destination.top};
      do {
        tile.top = tile.bottom;
        tile.bottom += tile_height;
        draw_buffer->Blt(art[3], *art[3]->GetBufferArea(), tile, nullptr);
      } while (tile.top < destination.bottom);
    }
  }
  if (art[2])
    draw_buffer->Blt(art[2], *art[2]->GetBufferArea(), art_rects[2], nullptr);
  if (active_art)
    draw_buffer->Blt(active_art, *active_art->GetBufferArea(), active_rect, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Size the main panel to its first art buffer, or to the original fallback when no buffer is present.
u8 UiGameMenuPanelMain::AutoSize() {
  if (art[0]) {
    i32 height = (i32)art[0]->Height();
    i32 width = (i32)art[0]->Width();
    SetSize(width, height);
    return 1;
  }
  SetSize(64, 64);
  return 0;
}

// Shut down the shared menu manager and release the panel's four retained art buffers.
u8 UiGameMenuPanelMain::ShutdownMenu() {
  if (manager) {
    manager->Shutdown();
    manager->Release();
    manager = nullptr;
  }
  for (i32 i = 0; i < 4; ++i) {
    if (art[i]) {
      art[i]->Release();
      art[i] = nullptr;
    }
  }
  active_art = nullptr;
  return 1;
}

// Store the active menu art and recalculate layout using the panel's existing bounds.
u8 UiGameMenuPanelMain::SetActiveMenu(i32 menu_id, GxBuffer *buffer, i32 x, i32 y) {
  current_button = (u32)menu_id;
  active_art = buffer;
  active_x = x;
  active_y = y;
  i32 bottom = GetB(), right = GetR(), top = GetT(), left = GetL();
  SetArea(left, top, right, bottom);
  return 1;
}

// Create the main menu panel with its original primary window identity and initial reference count.
ScUnknown *UiCreateClass02F61B00() {
  return static_cast<WnWindow *>(new UiGameMenuPanelMain);
}

UiGameMenuSetImpl::UiGameMenuSetImpl()
    : references(0), manager(nullptr), initialized(false), identifier(0), source_ids(nullptr), descriptor_ids(nullptr) {
}

UiGameMenuSetImpl::~UiGameMenuSetImpl() {
  UiGameMenuSetImpl::Shutdown();
}

bool UiGameMenuSetImpl::QueryInterface(u32, void **out) {
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiGameMenuSetImpl::AddRef() {
  return ++references;
}

// Destroy the set when its signed reference count reaches zero or below.
u32 UiGameMenuSetImpl::Release() {
  --references;
  if ((i32)references <= 0) {
    delete this;
    return 0;
  }
  return references;
}

// Parse the set identity and optional name, then load its mappings from the original loose or packaged INI file.
u8 UiGameMenuSetImpl::InitFromRegistry(const ScStringView &key, const ScStringView &value) {
  unsigned long parsed_id = 0, group = 0, instance = 0;
  if (sscanf(key.Data(), "SC3MSET_0x%08lX", &parsed_id) != 1) {
    key.ToChar();
    value.ToChar();
    return 0;
  }
  identifier = (u32)parsed_id;
  if (!identifier || identifier == kUiInvalidMenuSetId) {
    key.ToChar();
    value.ToChar();
    return 0;
  }
  if (sscanf(value.Data(), "0x%08lX,0x%08lX", &group, &instance) != 2) {
    key.ToChar();
    value.ToChar();
  }
  if (group && instance) {
    UiTextRef text((u32)instance, (u32)group);
    if (text.IsResolved())
      name.Copy(*text.GetResource());
  }
  ScString filename(UiGetFileSystem()->DataDirectory());
  ScString rest(value);
  rest.SplitToken(",");
  rest.SplitToken(",");
  filename.Append(rest.begin);
  ApCompressedIni file;
  file.SetPath(filename);
  ScString package(UiGetFileSystem()->DataDirectory());
  package.Append("\\Sys\\SYS.PAK");
  file.SetCompressedPath(package);
  ScString section(key);
  if (!file.EnumEntries(section, AddMappingFromRegistry, reinterpret_cast<ApIniContext *>(this))) {
    key.ToChar();
    value.ToChar();
    Shutdown();
    return 0;
  }
  CreateMappingArrays();
  initialized = true;
  return 1;
}

// Release both cached arrays, clear the mappings and reset the initialization flag.
u8 UiGameMenuSetImpl::Shutdown() {
  delete[] source_ids;
  source_ids = nullptr;
  delete[] descriptor_ids;
  descriptor_ids = nullptr;
  mappings.Clear();
  initialized = false;
  return 1;
}

u32 UiGameMenuSetImpl::GetMenuSetID() {
  return identifier;
}

// Resolve a mapped descriptor through the borrowed manager, clearing the output when no mapping exists.
u8 UiGameMenuSetImpl::GetMenuDesc(u32 menu_id, UiGameMenuDescriptor **out) {
  *out = nullptr;
  if (!manager)
    return 0;
  for (u32 i = 0; i < mappings.size; ++i) {
    if (mappings.data[i].source == menu_id)
      return manager->GetSpecificMenuDesc(mappings.data[i].descriptor, out);
    if (mappings.data[i].source > menu_id)
      break;
  }
  return 0;
}

u8 UiGameMenuSetImpl::SetMenuMgr(UiGameMenuManager *value) {
  manager = value;
  return value != nullptr;
}

// Parse and insert one unique hexadecimal menu-to-descriptor mapping in unsigned-key order.
u8 UiGameMenuSetImpl::AddMapping(const ScStringView &key, const ScStringView &value) {
  unsigned long source, descriptor;
  if (sscanf(key.Data(), "0x%08lX", &source) != 1 || sscanf(value.Data(), "0x%08lX", &descriptor) != 1) {
    key.ToChar();
    value.ToChar();
    return 0;
  }
  u32 position = 0;
  while (position < mappings.size && mappings.data[position].source < (u32)source)
    ++position;
  if (position < mappings.size && mappings.data[position].source == (u32)source) {
    key.ToChar();
    value.ToChar();
    return 0;
  }
  mappings.Append();
  for (u32 i = mappings.size - 1; i > position; --i)
    mappings.data[i] = mappings.data[i - 1];
  mappings.data[position] = {(u32)source, (u32)descriptor};
  return 1;
}

// Rebuild the two cached ID arrays in the same ascending order as the set's mapping entries.
u8 UiGameMenuSetImpl::CreateMappingArrays() {
  delete[] source_ids;
  delete[] descriptor_ids;
  source_ids = nullptr;
  descriptor_ids = nullptr;
  if (!mappings.size)
    return 1;
  source_ids = new u32[mappings.size];
  descriptor_ids = new u32[mappings.size];
  for (u32 i = 0; i < mappings.size; ++i) {
    source_ids[i] = mappings.data[i].source;
    descriptor_ids[i] = mappings.data[i].descriptor;
  }
  return 1;
}

// Forward a nonempty registry pair to the owning menu set through its borrowed callback context.
void UiGameMenuSetImpl::AddMappingFromRegistry(const ScString &key, const ScString &value, ApIniContext *context) {
  if (!context)
    return;
  if (key.Strlen() && value.Strlen())
    reinterpret_cast<UiGameMenuSetImpl *>(context)->AddMapping(key, value);
  else {
    key.ToChar();
    value.ToChar();
  }
}
