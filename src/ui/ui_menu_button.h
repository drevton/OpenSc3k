#ifndef SC3_UI_UI_MENU_BUTTON_H
#define SC3_UI_UI_MENU_BUTTON_H

#include "graphics/image_codecs.h"
#include "ui/ui_window.h"

// The embedded animation record owns two buffer references and an eight-frame atlas rectangle.
struct UiMenuImages {
  UiMenuImages();
  ~UiMenuImages();
  bool SetNormal(GxBuffer *image);
  bool SetAlternate(GxBuffer *image);
  bool SetFrame(u32 value);
  bool NextFrame();
  bool IsPixelTransparent(i32 x, i32 y) const;
  GxBuffer *normal, *alternate;
  u32 frame;
  ScRect source;
  i32 offset_x, offset_y;
  u32 word200, word204;
};

// Menu buttons notify their paired label of selection and accept a separate animation-reset command.
enum UiMenuCommand : u32 {
  kUiMenuLabelSelectionCommand = 0x35de5228,
  kUiMenuResetAnimationCommand = 0x15de1735,
};

// Button-specific flags select alternate art, transparent-pixel hit testing and frame/departure behavior.
enum UiMenuButtonFlag : u32 {
  kUiMenuButtonHasAlternateArt = 1,
  kUiMenuButtonPixelHitTest = 4,
  kUiMenuButtonUseFinalIdleFrame = 8,
  kUiMenuButtonImmediateLeave = 16,
};

class UiMenuButton : public UiWindow {
public:
  UiMenuButton();
  ~UiMenuButton() override;
  bool DoMessage(ScMessage *message) override;
  bool GZPaint() override;
  virtual bool SetImage(GxBuffer *image);
  virtual bool SetAlternate(GxBuffer *image);
  virtual bool Fit();
  virtual bool SetButtonFlags(u32 mask, u8 enabled);
  virtual bool GetButtonFlags(u32 mask) const;
  bool IsPointInWindowScreenCoordinates(i32 x, i32 y) const override;

protected:
  bool IsPointInWindow(i32 x, i32 y) const override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool GZOnCaptureChanged(WnWindow *window) override;
  bool NotifyLabel(u8 enabled);
  bool ResetAnimation();
  bool Flag(u32 mask) const;
  UiMenuImages images;
  u32 button_flags, last_tick, phase;
};

bool UiCreateMenuButton(u32 identifier, i32 x, i32 y, UiMenuButton **out);

#endif
