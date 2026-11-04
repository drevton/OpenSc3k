#include "window/message_box.h"

#include "common/sc3_unknown.h"
#include "graphics/image_codecs.h"
#include "window/api.h"
#include "window/controls.h"

// Construct the modal base and empty body before clearing child controls and converting the initial title-strip color.
// The background is borrowed; edge colors remain unwritten until successful initialization.
WnMessageBox::WnMessageBox()
    : options(0), title_color(0), default_button(nullptr), cancel_button(nullptr), title_label(nullptr),
      body_label(nullptr), background(nullptr) {
  title_color = WnRootWidget::MakeFillColor(124, 144, 255);
}

WnMessageBox::~WnMessageBox() {
  WnMessageBox::Close();
}

bool WnMessageBox::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnMessageBoxControl, WnMessageBoxControl);
  return WnModal::QueryInterface(iid, out);
}

u32 WnMessageBox::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnMessageBox::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed root view from the message-box control without acquiring another reference.
WnWindow *WnMessageBox::GetWindow() {
  return static_cast<WnWindow *>(this);
}

// Copy through the original temporary String before replacing body text, preserving input aliasing and read order.
// This overload also clears the title color; existing child pointers are cleared without extra releases.
bool WnMessageBox::Configure(const ScStringView &incoming, u32 value) {
  {
    ScString temporary(incoming);
    message = temporary;
  }
  options = value;
  default_button = nullptr;
  cancel_button = nullptr;
  title_label = nullptr;
  body_label = nullptr;
  title_color = 0;
  background = nullptr;
  return true;
}

// Replace the copied body before dispatching the live title setter, then publish options and clear child/background fields.
// Unlike the two-argument overload, this path preserves the stored title color.
bool WnMessageBox::Configure(const ScStringView &incoming, const ScStringView &title, u32 value) {
  {
    ScString temporary(incoming);
    message = temporary;
  }
  SetCaption(title);
  options = value;
  default_button = nullptr;
  cancel_button = nullptr;
  title_label = nullptr;
  body_label = nullptr;
  background = nullptr;
  return true;
}

bool WnMessageBox::Close() {
  return Shutdown();
}

// Release each independent child-control hold in source order, clearing its field only after Release returns.
// Clear flag1 and the private buffer through live virtuals before the qualified root shutdown.
bool WnMessageBox::Shutdown() {
  ScRelease(&default_button);
  ScRelease(&cancel_button);
  ScRelease(&title_label);
  ScRelease(&body_label);
  SetFlag(kWnFlagVisible, false);
  PrivateBuffer(false);
  return WnRootWidget::Shutdown();
}

// Return the new message box's controlling root at zero references for the class registry's retained interface query.
ScUnknown *WnConstructMessageBox() {
  return static_cast<WnWindow *>(new WnMessageBox);
}

// Create a retained button through resources or the genuine image-button fallback, then apply its localized caption.
// Keep the source color groups, fitted height, fixed width and destruction/release ordering on unsupported IDs or attach failure.
WnButton *WnMessageBox::CreateButton(u32 button_id) {
  u32 red = MakeFillColor(192, 0, 0), green = MakeFillColor(0, 192, 0), yellow = MakeFillColor(192, 192, 0);
  WnResources *resources = WnGetUiResources();
  const ScStringView *localized = resources ? resources->GetText(button_id) : nullptr;
  ScString empty;
  WnButton *button = resources ? resources->CreateButton(button_id, empty, nullptr, nullptr, 0) : nullptr;
  if (!button) {
    button = ScAddRefNotNull(new WnImageButton);
    button->SetStyle(kWnButtonPushStretch, nullptr, nullptr);
    button->GetWindow()->SetID(button_id);
  }
  WnWindow *window = button->GetWindow();
  const char *fallback_caption;
  u32 color;
  switch (button_id) {
  case kWnAbortButtonId:
    fallback_caption = "Abort";
    color = red;
    break;
  case kWnCancelButtonId:
    fallback_caption = "Cancel";
    color = red;
    break;
  case kWnIgnoreButtonId:
    fallback_caption = "Ignore";
    color = yellow;
    break;
  case kWnNoButtonId:
    fallback_caption = "No";
    color = red;
    break;
  case kWnOkButtonId:
    fallback_caption = "OK";
    color = green;
    break;
  case kWnRetryButtonId:
    fallback_caption = "Retry";
    color = green;
    break;
  case kWnYesButtonId:
    fallback_caption = "Yes";
    color = green;
    break;
  default:
    GetWindowManager()->DestroyWindow(window);
    button->Release();
    return nullptr;
  }
  if (localized)
    window->SetCaption(*localized);
  else {
    ScString text(fallback_caption);
    window->SetCaption(text);
  }
  window->SetFillColor(color);
  button->Fit();
  button->SetButtonFlag(kWnButtonAutoFit, false);
  window->SetW(100);
  if (ChildAdd(window))
    return button;
  GetWindowManager()->DestroyWindow(window);
  button->Release();
  return nullptr;
}

static i32 WnMessageBoxAdd(i32 first, i32 second) {
  return (i32)((u32)first + (u32)second);
}

// Build retained title/body labels and the selected button group, sizing text before placing each control.
// Retain the live geometry/query order, independent default/cancel holds and source early failures before root Init.
bool WnMessageBox::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  i32 title_width = 0, button_width = 200, width;
  u32 group = (u16)options;
  WnWindow *title_window = nullptr;
  WnResources *resources = WnGetUiResources();
  if (!group)
    button_width = 100;
  else if (group <= 3)
    button_width = 208;
  else if (group <= 5)
    button_width = 316;
  width = button_width < 200 ? 200 : button_width;
  if (GetCaption()->Strlen()) {
    ScString title(*GetCaption());
    if (resources)
      title_label = resources->CreateLabel(0, title);
    if (!title_label) {
      title_label = new WnLabel;
      title_label->SetCaption(title);
      title_label->AddRef();
    }
    title_window = title_label->GetWindow();
    title_label->SetBackground(true);
    title_window->SetFillColor(title_color);
    title_label->SetLabelFlag(kWnLabelForwardMessages, true);
    if (!ChildAdd(title_window)) {
      GetWindowManager()->DestroyWindow(title_window);
      return false;
    }
    title_label->Fit();
    title_width = WnMessageBoxAdd(title_window->GetW(), 16);
  }
  if (width < title_width)
    width = title_width;
  if (resources)
    body_label = resources->CreateLabel(0, message);
  if (!body_label) {
    body_label = new WnLabel;
    body_label->SetCaption(message);
    body_label->AddRef();
  }
  WnWindow *body_window = body_label->GetWindow();
  body_label->SetAlignment(kWnLabelCenterMiddle);
  body_label->SetLabelFlag(kWnLabelWrapText, true);
  if (!ChildAdd(body_window)) {
    GetWindowManager()->DestroyWindow(body_window);
    return false;
  }
  for (;;) {
    body_window->SetW(width);
    body_label->Fit();
    i32 height = body_window->GetH();
    if ((i32)((u32)height * 2) < width)
      break;
    width = WnMessageBoxAdd(width, 16);
  }
  i32 top;
  if (!GetCaption()->Strlen())
    top = WnMessageBoxAdd(body_window->GetH(), 24);
  else {
    i32 body_height = body_window->GetH();
    top = WnMessageBoxAdd(WnMessageBoxAdd(body_height, title_window->GetH()), 24);
  }
  i32 left = WnMessageBoxAdd((i32)((u32)width - (u32)button_width) / 2, 8), bottom;
  if (!group) {
    WnButton *button = CreateButton(kWnOkButtonId);
    if (!button)
      return false;
    WnWindow *window = button->GetWindow();
    window->GZWinMoveTo(left, top);
    default_button = cancel_button = button;
    button->AddRef();
    bottom = WnMessageBoxAdd(window->GetH(), top);
    i32 extent = WnMessageBoxAdd(left, window->GetW());
    if (width < extent)
      width = WnMessageBoxAdd(left, window->GetW());
  } else {
    u32 first, middle = 0, last;
    switch (group) {
    case 1:
      first = kWnOkButtonId;
      last = kWnCancelButtonId;
      break;
    case 2:
      first = kWnRetryButtonId;
      last = kWnCancelButtonId;
      break;
    case 3:
      first = kWnYesButtonId;
      last = kWnNoButtonId;
      break;
    case 4:
      first = kWnYesButtonId;
      middle = kWnNoButtonId;
      last = kWnCancelButtonId;
      break;
    case 5:
      first = kWnAbortButtonId;
      middle = kWnRetryButtonId;
      last = kWnIgnoreButtonId;
      break;
    default:
      return false;
    }
    WnButton *button = CreateButton(first);
    if (!button)
      return false;
    WnWindow *window = button->GetWindow();
    window->GZWinMoveTo(left, top);
    default_button = button;
    i32 position = WnMessageBoxAdd(WnMessageBoxAdd(left, window->GetW()), 8);
    if (middle) {
      button = CreateButton(middle);
      if (!button)
        return false;
      window = button->GetWindow();
      window->GZWinMoveTo(position, top);
      position = WnMessageBoxAdd(position, WnMessageBoxAdd(window->GetW(), 8));
    }
    button = CreateButton(last);
    if (!button)
      return false;
    window = button->GetWindow();
    window->GZWinMoveTo(position, top);
    cancel_button = button;
    bottom = WnMessageBoxAdd(window->GetH(), top);
    i32 extent = WnMessageBoxAdd(position, window->GetW());
    if (width < extent)
      width = WnMessageBoxAdd(position, window->GetW());
  }
  SetArea(0, 0, WnMessageBoxAdd(width, 8), WnMessageBoxAdd(bottom, 8));
  if (GetCaption()->Strlen()) {
    title_window->SetW((i32)((u32)GetW() - 16));
    title_window->GZWinMoveTo(8, 8);
  }
  body_window->SetW((i32)((u32)GetW() - 16));
  if (GetCaption()->Strlen())
    body_window->GZWinMoveTo(8, WnMessageBoxAdd(title_window->GetB(), 8));
  else
    body_window->GZWinMoveTo(8, 8);
  PrivateBuffer(true);
  WnWindow *main_window = GetMainWindow();
  if (!main_window)
    return false;
  i32 main_height = main_window->GetH(), height = GetH();
  i32 y = (i32)((u32)main_height - (u32)height) / 2;
  i32 own_width = GetW(), main_width = main_window->GetW();
  GZWinMoveTo((i32)((u32)main_width - (u32)own_width) / 2, y);
  u32 pixel = MakeFillColor(124, 144, 255);
  SetFillColor(pixel);
  first_edge = MakeFillColor(255, 255, 255);
  second_edge = MakeFillColor(128, 128, 128);
  return WnRootWidget::Init();
}

bool WnMessageBox::OnKeyDown(u32 key, u32 modifiers) {
  if (key == kWnKeyReturn)
    default_button->Activate(0);
  else if (key == kWnKeyEscape || (key == kWnKeyF4 && (modifiers & kWnModifierAlt)))
    cancel_button->Activate(0);
  return true;
}

bool WnMessageBox::OnKeyUp(u32, u32) {
  return true;
}

// Save the drag anchor before enabling the source dragging flag and asking the actual owner to capture this root.
// Both virtual results are ignored; the original mouse press always succeeds.
bool WnMessageBox::OnMouseDownL(i32 x, i32 y, u32) {
  SetMouseDownOffset(x, y);
  SetFlag(kWnFlagDragging, true);
  GZSetCapture();
  return true;
}

bool WnMessageBox::OnMouseUpL(i32, i32, u32) {
  if (!GetFlag(kWnFlagDragging))
    return false;
  owner->ReleaseCapture(this);
  SetFlag(kWnFlagDragging, false);
  return true;
}

// Shift from the saved anchor only during an active drag, preserving wrapping delta arithmetic.
bool WnMessageBox::OnMouseMove(i32 x, i32 y, u32) {
  if (GetFlag(kWnFlagDragging))
    GZWinOffset((i32)((u32)x - (u32)mouse_down_x), (i32)((u32)y - (u32)mouse_down_y));
  return true;
}

// Post a modal completion for button command-kind1 and preserve the literal success even if native posting fails.
bool WnMessageBox::OnCommand(u32 kind, u32 command) {
  if (kind != kWnCommandButtonActivated)
    return false;
  GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), command);
  return true;
}

// Tile a borrowed background or fill the draw rectangle, then independently attempt the four two-color border lines.
// Failed lock and raster results skip only their own phase; the installed painter always returns true.
bool WnMessageBox::GZPaint() {
  if (background) {
    if (draw_buffer->Lock(GxLockFlags::Blt)) {
      const ScRect *source = background->GetBufferArea();
      draw_buffer->BltTiled(background, *(source), draw_area, 0, 0, nullptr);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
  } else if (draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    draw_buffer->Fill(pixel, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (draw_buffer->Lock(GxLockFlags::Line)) {
    draw_buffer->DrawLine(draw_area.left, draw_area.top, (i32)((u32)draw_area.right - 1), draw_area.top,
        static_cast<i32>(first_edge), 1);
    draw_buffer->DrawLine(draw_area.left, draw_area.top, draw_area.left, (i32)((u32)draw_area.bottom - 1),
        static_cast<i32>(first_edge), 1);
    draw_buffer->DrawLine((i32)((u32)draw_area.right - 1), draw_area.top, (i32)((u32)draw_area.right - 1),
        (i32)((u32)draw_area.bottom - 1), static_cast<i32>(second_edge), 1);
    draw_buffer->DrawLine(draw_area.left, (i32)((u32)draw_area.bottom - 1), (i32)((u32)draw_area.right - 1),
        (i32)((u32)draw_area.bottom - 1), static_cast<i32>(second_edge), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

WnModal::WnModal() {
}

WnModal::~WnModal() {
  WnRootWidget::Shutdown();
}

bool WnModal::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnModalControl, WnModalControl);
  return WnRootWidget::QueryInterface(iid, out);
}

u32 WnModal::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnModal::Release() {
  return WnRootWidget::Release();
}

// Resolve the live manager and post completion with the root view and the caller's full result word.
// Return the genuine posting result; the modal interface itself neither shuts down nor releases the window.
bool WnModal::EndModal(i32 result) {
  WnWindowManager *manager = GetWindowManager();
  return manager->GZWinMsgPost(
      kWnMsg_EndModal, reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), (uptr)(u32)result);
}

bool WnModal::GZOnCharacter(char) {
  return true;
}

// Find the live matching child, focus it through the manager, then forward the original key and modifier words.
// A missing match returns false without focus changes or message delivery.
bool WnModal::OnKeyDown(u32 key, u32 modifiers) {
  WnWindow *window = IsChildKeyEquivalent(key, modifiers);
  if (!window)
    return false;
  GetWindowManager()->SetFocus(window);
  return SendMsg(window, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnModal::OnKeyUp(u32, u32) {
  return true;
}
