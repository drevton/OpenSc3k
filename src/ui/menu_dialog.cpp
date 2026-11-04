#include "application/api.h"
#include "application/application_core.h"
#include "application/main_dialogs.h"
#include "audio/api.h"

#include "common/ids.h"

#include "common/sc3_class_factory.h"
#include "resources/resource.h"
#include "ui/commands.h"
#include "ui/ui_menu_button.h"
#include "ui/ui_menu_config.h"
#include "ui/ui_menu_dialog.h"
#include "ui/ui_menu_label.h"
#include "ui/ui_resource_handles.h"
#include "window/api.h"

// Menu image conversion uses magenta as its transparent source color.
enum UiMenuImageColor : u32 {
  kUiMenuImageTransparentColor = 0xff00ff,
};

// Menu rows retain individual shade colors while normal and selected labels use the shared foreground colors.
enum UiMainMenuColor : u32 {
  kUiNewCityMenuShade = 0x40601f,
  kUiQuitMenuShade = 0x50606f,
  kUiLoadCityMenuShade = 0x707f80,
  kUiStarterTownMenuShade = 0x707f60,
  kUiPreferencesMenuShade = 0x7f7f96,
  kUiScenarioMenuShade = 0x707040,
  kUiTerrainMenuShade = 0x807860,
  kUiMenuNormalTextColor = 0xffd778,
  kUiMenuSelectedTextColor = 0xffff20,
};

// These indexes select label coordinates in the original menu-position array, independently of text resource IDs.
enum UiMenuLabelPosition : u32 {
  kUiStarterTownMenuLabelPosition = 6,
  kUiPreferencesMenuLabelPosition = 4,
  kUiQuitMenuLabelPosition = 2,
};

u32 UiMenuPositions[30] = {63, 88, 49, 83, 73, 54, 63, 53, 77, 66, 151, 146, 136, 132, 353, 10, 520, 367, 10, 402, 180,
    313, 194, 73, 319, 331, 333, 156, 31, 137};

UiMenuDialog::UiMenuDialog() {
}

UiMenuDialog::~UiMenuDialog() {
  Shutdown();
}

bool UiMenuDialog::Shutdown() {
  WnRootWidget::Shutdown();
  return true;
}

// Reset the exit control and flush input before the initialized check; then create the source-ordered menu controls.
bool UiMenuDialog::Init() {
  WnWindow *exit = GetChildWindowFromID(kUiMainMenuQuitId);
  SendMsg(exit, kWnMsg_Command, kUiMenuResetAnimationCommand, kUiMainMenuQuitId, 0);
  owner->FlushInputMessageQueue();
  if (flags & 0x4000)
    return true;
  WnRootWidget::Init();
  SetSize(640, 480);
  WnWindow *main = GetMainWindow();
  CenterWindowInRect(*main->GetAreaAbsolute());
  FtFont *font = ApGetUiResources()->GetFont(kWnFontUiSerif);
  UiMenuKeys keys;
  UiLoadMenuKeys(&keys);

  struct Row {
    u32 command, key, position, label_position, label_group, label_instance, shadow;
  };

  static const Row rows[] = {{kUiMainMenuNewCityId, 4, 28, 12, kRsMenuTextGroup, 1, kUiNewCityMenuShade},
      {kUiMainMenuLoadCityId, 5, 26, 10, kRsMenuTextGroup, 2, kUiLoadCityMenuShade},
      {kUiMainMenuLoadStarterTownId, 7, 22, kUiStarterTownMenuLabelPosition, kRsMenuTextGroup, 9,
          kUiStarterTownMenuShade},
      {kUiMainMenuPreferencesId, 8, 20, kUiPreferencesMenuLabelPosition, kRsMenuTextGroup, 4, kUiPreferencesMenuShade},
      {kUiMainMenuLoadScenarioId, 9, 14, 0, kRsSupplementalGameTextGroup, 1, kUiScenarioMenuShade},
      {kUiMainMenuLoadTerrainId, 6, 24, 8, kRsMenuTextGroup, 19, kUiTerrainMenuShade},
      {kUiMainMenuCreditsId, 3, 18, 0, 0, 0, 0},
      {kUiMainMenuQuitId, 1, 16, kUiQuitMenuLabelPosition, kRsMenuTextGroup, 6, kUiQuitMenuShade}};
  UiMenuButton *button = nullptr;
  UiMenuLabel *label = nullptr;
  GxBuffer *image = nullptr;
  for (const Row &row : rows) {
    i32 x = (i32)UiMenuPositions[row.position], y = (i32)UiMenuPositions[row.position + 1];
    RsKey key = keys.keys[row.key - 1];
    if (UiCreateMenuButton(row.command, x, y, &button)) {
      if (UiLoadMenuImage(key.group, key.instance,
              row.command == kUiMainMenuCreditsId ? 0 : kUiMenuImageTransparentColor, &image)) {
        button->SetImage(image);
        if (row.command == kUiMainMenuQuitId) {
          GxBuffer *alternate = nullptr;
          key = keys.keys[1];
          if (UiLoadMenuImage(key.group, key.instance, kUiMenuImageTransparentColor, &alternate)) {
            button->SetAlternate(alternate);
            button->SetButtonFlags(kUiMenuButtonImmediateLeave, 0);
            ChildAdd(button);
            alternate->Release();
            alternate = nullptr;
          }
        } else {
          ChildAdd(button);
          if (row.command == kUiMainMenuCreditsId) {
            button->SetButtonFlags(kUiMenuButtonUseFinalIdleFrame, 1);
            button->SetButtonFlags(kUiMenuButtonPixelHitTest, 0);
          }
        }
        image->Release();
        image = nullptr;
      }
      button->Release();
      button = nullptr;
    }
    if (!row.label_group)
      continue;
    i32 offset_x = (i32)UiMenuPositions[row.label_position], offset_y = (i32)UiMenuPositions[row.label_position + 1];
    u32 colors[6] = {kUiMenuNormalTextColor, row.shadow, 0, row.shadow, kUiMenuSelectedTextColor, row.shadow};
    if (UiCreateMenuLabel(row.command + 0x10000u, row.label_group, row.label_instance, &label)) {
      label->Style(font, colors);
      label->Fit();
      i32 height = label->GetH();
      i32 top = (i32)((u32)y + (u32)offset_y - (u32)(height / 2));
      i32 width = label->GetW();
      i32 left = (i32)((u32)offset_x + (u32)x - (u32)(width / 2));
      label->GZWinMoveTo(left, top);
      ChildAdd(label);
      label->Release();
      label = nullptr;
    }
  }
  SetFlag(kWnFlagReorderable, false);
  SetFlag(kWnFlagDraggable, false);
  return WnRootWidget::Init();
}

bool UiMenuDialog::ForwardCommand(u32 command) {
  return owner->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(this), command);
}

bool UiMenuDialog::OnCommand(u32 kind, u32 command) {
  if (kind != kWnCommandButtonActivated)
    return false;
  if (ApServices::BoxX())
    ApServices::BoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  switch (command) {
  case kUiMainMenuNewCityId:
  case kUiMainMenuLoadCityId:
  case kUiMainMenuLoadTerrainId:
  case kUiMainMenuLoadStarterTownId:
  case kUiMainMenuQuitId:
  case kUiMainMenuLoadScenarioId:
    ForwardCommand(command);
    break;
  case kUiMainMenuPreferencesId:
    ShowPreferences(this);
    break;
  case kUiMainMenuCreditsId:
    ShowCredits(this);
    break;
  default:
    return false;
  }
  return true;
}

bool SC3_STDCALL UiMenuDialog::ShowPreferences(WnWindow *window) {
  ScAutoCommand command(kUiPreferencesCommandClassId);
  if (!command.IsValid())
    return false;
  window->HideWindow();
  command->Execute();
  window->ShowWindow();
  return true;
}

bool UiMenuDialog::ShowCredits(WnWindow *window) {
  ApCreditsWindow *credits = new ApCreditsWindow;
  credits->AddRef();
  window->HideWindow();
  owner->DoModalWin(credits);
  credits->Shutdown();
  credits->Release();
  window->ShowWindow();
  return true;
}
