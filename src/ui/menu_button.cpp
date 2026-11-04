#include "ui/ui_menu_button.h"
#define WIN32_LEAN_AND_MEAN
#include "ui/ui_ticks.h"

// Menu buttons animate entry, departure and press transitions, retaining idle and held endpoints.
enum UiMenuAnimationPhase : i32 {
  kUiMenuAnimationIdle = 0x65dcc508,
  kUiMenuAnimationEntering = 0x65dcc509,
  kUiMenuAnimationLeaving = 0x65dcc50a,
  kUiMenuAnimationPressing = 0x65dcc50b,
  kUiMenuAnimationHeld = 0x65dcc50c,
};

static constexpr u32 kUiMenuButtonSuppressCommandFlag = 0x10000000;

UiMenuImages::UiMenuImages()
    : normal(nullptr), alternate(nullptr), frame(0), source{}, offset_x(0), offset_y(0), word200(0), word204(0) {
}

// Release normal then alternate images and clear all remaining animation geometry.
UiMenuImages::~UiMenuImages() {
  if (normal)
    normal->Release();
  normal = nullptr;
  if (alternate)
    alternate->Release();
  alternate = nullptr;
  source = {};
  offset_x = offset_y = 0;
  word200 = word204 = 0;
}

// Retain before releasing the previous normal image; preserve the atlas's unusual absolute left/top coordinates.
bool UiMenuImages::SetNormal(GxBuffer *image) {
  if (image)
    image->AddRef();
  if (normal)
    normal->Release();
  normal = image;
  if (image) {
    const ScRect *area = image->GetBufferArea();
    i32 left = area->left, top = area->top, bottom = area->bottom;
    i32 width = (i32)((u32)area->right - (u32)left);
    source.left = left;
    source.top = top;
    source.right = width / 4;
    source.bottom = (i32)((u32)bottom - (u32)top) / 2;
  } else
    source = {};
  return true;
}

// Transfer the alternate buffer ownership without changing the normal atlas geometry.
bool UiMenuImages::SetAlternate(GxBuffer *image) {
  if (image)
    image->AddRef();
  if (alternate)
    alternate->Release();
  alternate = image;
  return true;
}

// Wrap any frame beyond seven to zero and place the source rectangle in the atlas's two rows.
bool UiMenuImages::SetFrame(u32 value) {
  frame = value < 8 ? value : 0;
  u32 height = (u32)source.bottom - (u32)source.top, width = (u32)source.right - (u32)source.left;
  if (frame >= 4) {
    source.top = (i32)height;
    source.left = (i32)(width * (frame - 4));
    source.right = (i32)(width + (u32)source.left);
    source.bottom = (i32)(2 * height);
  } else {
    source.top = 0;
    source.right = (i32)(width * (frame + 1));
    source.left = (i32)(width * frame);
    source.bottom = (i32)height;
  }
  return true;
}

bool UiMenuImages::NextFrame() {
  return SetFrame(frame + 1);
}

// Outside or absent images are transparent; successful locks query the live normal buffer and unlock it afterward.
bool UiMenuImages::IsPixelTransparent(i32 x, i32 y) const {
  if (!normal || x < offset_x || y < offset_y)
    return true;
  i32 width = normal->Width();
  if (x > (i32)((u32)offset_x + (u32)width))
    return true;
  i32 captured_y = offset_y;
  if (y > (i32)((u32)captured_y + normal->Height()))
    return true;
  x = (i32)((u32)x - (u32)offset_x);
  y = (i32)((u32)y - (u32)captured_y);
  if (x < 0 || y < 0)
    return true;
  bool result = false;
  if (normal->Lock(GxLockFlags::GetSetPixel)) {
    result = normal->IsPixelTransparent((u32)x, (u32)y);
    normal->Unlock(GxLockFlags::GetSetPixel);
  }
  return result;
}

UiMenuButton::UiMenuButton() : button_flags(20), last_tick(0), phase(kUiMenuAnimationIdle) {
}

UiMenuButton::~UiMenuButton() {
}

// Resize through the live window method after the normal atlas has been replaced successfully.
bool UiMenuButton::SetImage(GxBuffer *image) {
  bool result = images.SetNormal(image);
  if (result)
    SetSize((i32)((u32)images.source.right - (u32)images.source.left),
        (i32)((u32)images.source.bottom - (u32)images.source.top));
  return result;
}

// Replace the alternate buffer, then update the live button flag even when that assignment returns false.
bool UiMenuButton::SetAlternate(GxBuffer *image) {
  bool result = images.SetAlternate(image);
  SetButtonFlags(kUiMenuButtonHasAlternateArt, images.alternate != nullptr);
  return result;
}

bool UiMenuButton::Fit() {
  if (!images.normal)
    return false;
  GxBuffer *image = images.normal;
  i32 height = image->Height(), width = image->Width();
  SetSize(width, height);
  return true;
}

// Apply the raw incoming byte as a predicate; mask eight additionally selects frame seven minus current capture.
bool UiMenuButton::SetButtonFlags(u32 mask, u8 enabled) {
  button_flags = enabled ? button_flags | mask : button_flags & ~mask;
  if (mask == kUiMenuButtonUseFinalIdleFrame)
    images.SetFrame(7 - (owner->GetCapture() == this));
  return true;
}

bool UiMenuButton::Flag(u32 mask) const {
  return (button_flags & mask) != 0;
}

// Find the paired immediate label by identifier and synchronously send its selection command.
bool UiMenuButton::NotifyLabel(u8 enabled) {
  WnWindow *container = GetParentWin();
  if (!container)
    return false;
  WnWindow *label = container->GetChildWindowFromID(GetID() + 0x10000u);
  SendMsg(label, kWnMsg_Command, kUiMenuLabelSelectionCommand, enabled, 0);
  return true;
}

// Restore idle state and choose the initial frame from the live alternate-state flag.
bool UiMenuButton::ResetAnimation() {
  phase = kUiMenuAnimationIdle;
  images.SetFrame(Flag(kUiMenuButtonUseFinalIdleFrame) ? 7 : 0);
  return true;
}

// Consume the matching reset command; otherwise use ordinary input and bubble unhandled keyboard messages.
bool UiMenuButton::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_Command) {
    if ((u32)message->data1 == kUiMenuResetAnimationCommand && (u32)message->data2 == GetID()) {
      ResetAnimation();
      NotifyLabel(0);
      return true;
    }
    return false;
  }
  bool result = WnRootWidget::DoMessage(message);
  if ((message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp) && !result) {
    if (GetParentWin())
      return GetParentWin()->DoMessage(message);
    return false;
  }
  return result;
}

// Translate window coordinates to the absolute origin before dispatching the screen-coordinate hit test.
bool UiMenuButton::IsPointInWindow(i32 x, i32 y) const {
  i32 screen_x = x + absolute_area.left;
  i32 screen_y = y + absolute_area.top;
  return IsPointInWindowScreenCoordinates(screen_x, screen_y);
}

// Start press animation when alternate art exists; synchronous command notification respects the inherited suppress flag.
bool UiMenuButton::OnMouseDownL(i32, i32, u32) {
  if (Flag(kUiMenuButtonHasAlternateArt)) {
    if (phase != kUiMenuAnimationPressing) {
      phase = kUiMenuAnimationPressing;
      images.SetFrame(0);
      if (!GetFlag(kUiMenuButtonSuppressCommandFlag)) {
        u32 id = GetID();
        SendMsg(GetParentWin(), kWnMsg_Command, kWnCommandButtonPressedChanged, id, 0);
      }
    }
  } else if (!GetFlag(kUiMenuButtonSuppressCommandFlag)) {
    u32 id = GetID();
    SendMsg(GetParentWin(), kWnMsg_Command, kWnCommandButtonActivated, id, 0);
  }
  return true;
}

bool UiMenuButton::OnMouseUpL(i32, i32, u32) {
  return true;
}

// Acquire capture on first movement; outside movement releases it unless the press animation is active.
bool UiMenuButton::OnMouseMove(i32 x, i32 y, u32) {
  if (owner->GetCapture() == this) {
    if (phase != kUiMenuAnimationPressing && !IsPointInWindowScreenCoordinates(x, y))
      owner->ReleaseCapture(this);
  } else
    owner->SetCapture(this);
  return true;
}

// Enter/leave animation follows the actual capture notification and leaves press/held phases untouched on departure.
bool UiMenuButton::GZOnCaptureChanged(WnWindow *window) {
  if (window == this) {
    NotifyLabel(1);
    phase = kUiMenuAnimationEntering;
  } else if ((i32)phase < kUiMenuAnimationPressing || (i32)phase > kUiMenuAnimationHeld) {
    NotifyLabel(0);
    if (Flag(kUiMenuButtonImmediateLeave)) {
      phase = kUiMenuAnimationIdle;
      if (Flag(kUiMenuButtonUseFinalIdleFrame))
        images.SetFrame(7);
    } else
      phase = kUiMenuAnimationLeaving;
  }
  InvalidateSelf();
  return true;
}

// Use the inclusive absolute rectangle and optionally exclude normal-image color-key pixels.
bool UiMenuButton::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  if (x < absolute_area.left || y < absolute_area.top || x > absolute_area.right || y > absolute_area.bottom)
    return false;
  if (Flag(kUiMenuButtonPixelHitTest))
    return !images.IsPixelTransparent((i32)((u32)x - (u32)absolute_area.left), (i32)((u32)y - (u32)absolute_area.top));
  return true;
}

// Advance the live eight-frame animation at 100ms intervals and blit the selected atlas frame under buffer locks.
bool UiMenuButton::GZPaint() {
  if (!images.normal)
    return true;
  if (phase != kUiMenuAnimationIdle && phase != kUiMenuAnimationHeld) {
    u32 tick = UiTicks();
    if (tick - last_tick > 100) {
      images.NextFrame();
      if (phase == kUiMenuAnimationLeaving && !Flag(kUiMenuButtonUseFinalIdleFrame)) {
        if (!images.frame)
          phase = kUiMenuAnimationIdle;
      } else if (phase == kUiMenuAnimationPressing) {
        if (images.frame == 7) {
          phase = kUiMenuAnimationHeld;
          if (!GetFlag(kUiMenuButtonSuppressCommandFlag)) {
            u32 id = GetID();
            PostMsg(GetParentWin(), kWnMsg_Command, kWnCommandButtonActivated, id, 0);
          }
        }
      } else if ((phase == kUiMenuAnimationLeaving || phase == kUiMenuAnimationEntering) &&
          Flag(kUiMenuButtonUseFinalIdleFrame) && images.frame == 7) {
        phase = kUiMenuAnimationIdle;
        images.SetFrame(7 - (owner->GetCapture() == this));
      }
      last_tick = tick;
    }
  }
  if (draw_buffer->Lock(GxLockFlags::Blt)) {
    ScRect destination;
    destination.left = (i32)((u32)draw_area.left + (u32)images.offset_x);
    destination.top = (i32)((u32)draw_area.top + (u32)images.offset_y);
    destination.right = (i32)((u32)destination.left + (u32)images.source.right - (u32)images.source.left);
    destination.bottom = (i32)((u32)destination.top + (u32)images.source.bottom - (u32)images.source.top);
    GxBuffer *image =
        phase == kUiMenuAnimationHeld || phase == kUiMenuAnimationPressing ? images.alternate : images.normal;
    if (image)
      draw_buffer->Blt(image, images.source, destination, &draw_area);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

// Retain a constructed menu button, assign identity and position, clear flag800, then publish its actual pointer.
bool UiCreateMenuButton(u32 identifier, i32 x, i32 y, UiMenuButton **out) {
  UiMenuButton *button = new UiMenuButton();
  button->AddRef();
  button->SetID(identifier);
  button->GZWinMoveTo(x, y);
  button->SetFlag(kWnFlagReorderable, false);
  *out = button;
  return true;
}

bool UiMenuButton::GetButtonFlags(u32 mask) const {
  return Flag(mask);
}
