#ifndef SC3_UI_UI_MENU_DIALOG_H
#define SC3_UI_UI_MENU_DIALOG_H

#include "ui/ui_window.h"

// Main-menu results select the exact lifecycle action handled by the application.
enum UiMainMenuControlId : u32 {
  kUiMainMenuResumeCityId = 0x712bf5bd,
  kUiMainMenuNewCityId = 0x712bf5be,
  kUiMainMenuLoadCityId = 0x712bf5bf,
  kUiMainMenuLoadTerrainId = 0x712bf5c0,
  kUiMainMenuLoadStarterTownId = 0x712bf5c1,
  kUiMainMenuPreferencesId = 0x712bf5c2,
  kUiMainMenuCreditsId = 0x712bf5c3,
  kUiMainMenuQuitId = 0x712bf5c4,
  kUiMainMenuLoadScenarioId = 0x712bf5c5,
};

// The main menu dialog adds behavior to the application window without adding owner fields.
class UiMenuDialog : public UiWindow {
public:
  UiMenuDialog();
  ~UiMenuDialog() override;
  bool Init() override;
  bool Shutdown() override;

protected:
  bool OnCommand(u32 kind, u32 command) override;
  bool ForwardCommand(u32 command);
  static bool SC3_STDCALL ShowPreferences(WnWindow *window);
  bool ShowCredits(WnWindow *window);
};

extern u32 UiMenuPositions[30];

#endif
