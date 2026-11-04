#ifndef SC3_UI_UI_MAIN_WINDOW_H
#define SC3_UI_UI_MAIN_WINDOW_H

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_unknown.h"

// Copyright (C) 2025 Nicholas Hayes
// Declarations adapted from sc3k-gzcom-dll cISC3WinMain.h.
// Copyright Nicholas Hayes; LGPL-2.1-or-later. See THIRD_PARTY_NOTICES.txt for the upstream source.

// SDK declaration only; concrete behavior remains with the assigned DLL implementation.
class UiMainWindow : public ScUnknown {

public:
  virtual WnWindow *AsIGZWin() = 0;

  virtual bool PreInit(ApGameInterface *) = 0;
  virtual bool DoDancingBear() = 0;
  virtual bool DoCopyRightScreen(bool) = 0;
  virtual bool DoTitleBackgroundScreen() = 0;
  virtual void ReleaseTitleBackgroundBuffer() = 0;

  virtual bool DoMainMenu() = 0;
  virtual bool ActivateMainMenu() = 0;
  virtual bool DoLoadCity(ScStringView *) = 0;
  virtual bool DoShowWinCityView() = 0;

  virtual bool DbgDoDebugWindow(ScMessage *) = 0; // No-op that returns false
  virtual bool DbgDoDebugWindow(u32, u32, u32, u32) = 0;
};

// Original stateless cSCToolBar entry points coordinate the real main-UI manager and the city's hidden pause.
class UiToolbar {
public:
  static bool Init(WnWindow *window);
  static bool Shutdown();
};

#endif
