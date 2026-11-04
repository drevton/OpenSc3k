#ifndef SC3_UI_UI_WINDOW_H
#define SC3_UI_UI_WINDOW_H

#include "window/api.h"

// The application's SC3UI base shares the recovered root fields and obtains its owner from the application.
class UiWindow : public WnRootWidget {
public:
  UiWindow();
  void CenterWindowInRect(const ScRect &rectangle) override;
  using WnRootWidget::CenterWindowInRect;
};

WnWindowManager *ApGetDialogManager();
WnResources *ApGetUiResources();

#endif
