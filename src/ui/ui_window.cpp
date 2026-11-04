#include "ui/ui_window.h"

// Install the application owner after ordinary root initialization, then obtain keyboard and register this base.
UiWindow::UiWindow() : WnRootWidget(nullptr, nullptr) {
  owner = ApGetDialogManager();
  if (owner)
    keyboard = ApGetDialogManager()->GetKeyboard();
  if (owner)
    owner->AddWindowToValidList(this);
}

// Center the current extents within the supplied rectangle using signed truncation and the live placement method.
void UiWindow::CenterWindowInRect(const ScRect &rectangle) {
  i32 x = (i32)((u32)area.left + (u32)rectangle.right - (u32)area.right - (u32)rectangle.left) / 2;
  i32 y = (i32)((u32)rectangle.bottom + (u32)area.top - (u32)area.bottom - (u32)rectangle.top) / 2;
  x = (i32)((u32)rectangle.left + (u32)x);
  y = (i32)((u32)rectangle.top + (u32)y);
  SetArea(x, y, (i32)((u32)x + (u32)area.right - (u32)area.left), (i32)((u32)y + (u32)area.bottom - (u32)area.top));
}
