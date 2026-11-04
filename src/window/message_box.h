#ifndef SC3_WINDOW_MESSAGE_BOX_H
#define SC3_WINDOW_MESSAGE_BOX_H

#include "window/api.h"

// The actual secondary C12E57E2 identity adds only completion of the modal loop to Unknown.
class WnModalControl : public ScUnknown {
public:
  virtual bool EndModal(i32 result) = 0;
};

// Recovered 168-byte source base used by the built-in message box. Native inheritance performs its real adjustments.
// The secondary identity shares the root count rather than introducing independent modal ownership.
class WnModal : public WnRootWidget, public WnModalControl {
public:
  WnModal();
  ~WnModal() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool EndModal(i32 result) override;

protected:
  bool GZOnCharacter(char character) override;
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
};

class WnButton;
class WnLabelControl;

// Actual57C control operations reached by the manager and message-box destructor.
// The remaining SetTitleFillColor overloads take a u32 pixel or three u8 RGB values. Their nominal returns remain unresolved.
class WnMessageBoxControl : public ScUnknown {
public:
  virtual WnWindow *GetWindow() = 0;
  // MSVC emits this overload group in reverse declaration order: original control slots16 and20.
  virtual bool Configure(const ScStringView &message, const ScStringView &title, u32 options) = 0;
  virtual bool Configure(const ScStringView &message, u32 options) = 0;
  virtual bool Close() = 0;
};

// Built-in class593 extends the actual modal base with its own control, body text and four retained child controls.
// Its native declaration is a reached interface subset, not an original x86 vtable overlay.
class WnMessageBox : public WnModal, public WnMessageBoxControl {
public:
  WnMessageBox();
  ~WnMessageBox() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  WnWindow *GetWindow() override;
  bool Configure(const ScStringView &message, const ScStringView &title, u32 options) override;
  bool Configure(const ScStringView &message, u32 options) override;
  bool Close() override;
  bool Init() override;
  bool Shutdown() override;
  bool GZPaint() override;

protected:
  ScString message;
  u32 options, title_color;
  WnButton *default_button, *cancel_button;
  WnLabelControl *title_label, *body_label;
  GxBuffer *background;
  u32 first_edge, second_edge;
  WnButton *CreateButton(u32 button_id);
  bool OnKeyDown(u32 key, u32 modifiers) override;
  bool OnKeyUp(u32 key, u32 modifiers) override;
  bool OnMouseDownL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseUpL(i32 x, i32 y, u32 modifiers) override;
  bool OnMouseMove(i32 x, i32 y, u32 modifiers) override;
  bool OnCommand(u32 kind, u32 command) override;
};

#endif
