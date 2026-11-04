#include "ui/ui_menu_label.h"
#include "ui/ui_menu_button.h"

// The menu-label selection flag switches between its normal and selected text color pairs.
enum UiMenuLabelFlag : u32 {
  kUiMenuLabelSelected = 1,
};

static constexpr u32 kUiMenuLabelHitTestFlag = 0x10000000;

UiMenuLabel::UiMenuLabel() : UiWindow(), font(nullptr), label_flags(0) {
}

// Release the owned font before the ordinary window destructor cleans up children and owner references.
UiMenuLabel::~UiMenuLabel() {
  if (font)
    font->Release();
  font = nullptr;
}

void UiMenuLabel::SetCaption(const ScStringView &text) {
  WnRootWidget::SetCaption(text);
  InvalidateSelfAndParents();
}

// Compare the requested nonnegative extents against the live virtual dimensions after base placement.
void UiMenuLabel::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width < 0)
    width = 0;
  if (height < 0)
    height = 0;
  WnRootWidget::SetArea(left, top, right, bottom);
  if (width != GetW() || height != GetH())
    InvalidateSelfAndParents();
}

// Consume the label's selection command; unhandled keyboard messages bubble to the live parent after base input.
bool UiMenuLabel::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_Command) {
    if ((u32)message->data1 != kUiMenuLabelSelectionCommand)
      return 0;
    SetLabelFlags(kUiMenuLabelSelected, (u32)message->data2 != 0);
    return 1;
  }
  bool result = WnRootWidget::DoMessage(message);
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp) {
    if (result)
      return 1;
    if (!GetParentWin())
      return 0;
    return GetParentWin()->DoMessage(message);
  }
  return result;
}

// Initialize once and enable the three inherited text-control flags.
bool UiMenuLabel::Init() {
  if (!(flags & 0x4000)) {
    WnRootWidget::Init();
    SetFlag(kWnFlagPrivateBuffer, true);
    SetFlag(kWnFlagTransparentBuffer, true);
    SetFlag(kWnFlagClearTransparentBuffer, true);
  }
  return 1;
}

// Draw a one-pixel displaced shadow, then the live normal or selected color pair on the actual drawing buffer.
bool UiMenuLabel::GZPaint() {
  if (!font)
    return 1;
  ScRect shadow = {(i32)((u32)draw_area.left + 1), (i32)((u32)draw_area.top + 1), (i32)((u32)draw_area.right + 1),
      (i32)((u32)draw_area.bottom + 1)};
  font->DrawTextPara(
      draw_buffer, shadow, caption.Data(), caption.Strlen(), colors[2], colors[3], FtOutputStyle::Center);
  u32 index = GetLabelFlags(kUiMenuLabelSelected) ? 4 : 0;
  font->DrawTextPara(draw_buffer, draw_area, caption.Data(), caption.Strlen(), colors[index], colors[index + 1],
      FtOutputStyle::Center);
  return 1;
}

// Retain the incoming font before releasing its predecessor, then convert and store each caller color in order.
bool UiMenuLabel::Style(FtFont *replacement, const u32 *input) {
  if (replacement)
    replacement->AddRef();
  if (font)
    font->Release();
  font = replacement;
  for (u32 index = 0; index < 6; ++index) {
    u32 rgb = input[index];
    colors[index] = MakeFillColor((u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb);
  }
  InvalidateSelfAndParents();
  return 1;
}

bool UiMenuLabel::Fit() {
  if (font) {
    FtFont *current = font;
    const char *text = caption.Data();
    u32 length = caption.Strlen(), height = current->Height(0);
    u32 width = current->Measure(text, length);
    SetSize((i32)width, (i32)height);
  }
  return 0;
}

// Compare the normalized previous bit against the raw incoming byte before changing flags and invalidating.
bool UiMenuLabel::SetLabelFlags(u32 mask, u8 enabled) {
  u32 previous = GetLabelFlags(mask);
  bool changed = previous != enabled;
  label_flags = enabled ? label_flags | mask : label_flags & ~mask;
  if (changed && (i32)mask > 0 && (i32)mask <= 2)
    InvalidateSelfAndParents();
  return 1;
}

bool UiMenuLabel::GetLabelFlags(u32 mask) const {
  return (label_flags & mask) != 0;
}

bool UiMenuLabel::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  return GetFlag(kUiMenuLabelHitTestFlag) ? WnRootWidget::IsPointInWindowScreenCoordinates(x, y) : false;
}

bool UiMenuLabel::IsPointInWindow(i32 x, i32 y) const {
  return GetFlag(kUiMenuLabelHitTestFlag) ? WnRootWidget::IsPointInWindow(x, y) : false;
}
