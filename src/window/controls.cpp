#include "window/controls.h"

#include <assert.h>
#include <stdio.h>

#include <utility>

#include "audio/api.h"
#include "common/api.h"
#include "common/sc3_array.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "window/api.h"
#include "window/wn_resource_provider.h"

// Hints start with the original pale-yellow color before caller-specific presentation changes.
enum WnHintPresentation : u32 {
  kWnHintDefaultColor = 0xffffe0,
};

// This button flag invalidates painting without recalculating layout; option-group bits have separate color roles.
static constexpr i32 kWnButtonInvalidateFlag = 0x01000000;
static constexpr u32 kWnOptionNormalButtonColorFlag = 0x01000000;
static constexpr u32 kWnOptionHoverButtonColorFlag = 0x02000000;
static constexpr u32 kWnOptionDisabledButtonColorFlag = 0x04000000;
static constexpr u32 kWnOptionEnabledCaptionColorFlag = 0x00100000;
static constexpr u32 kWnOptionDisabledCaptionColorFlag = 0x00400000;
static constexpr u32 kWnUnsetOptionColor = UINT32_MAX;

WnImageButton::WnImageButton()
    : button_flags(0x6A4), control_text(), text_x(0), text_y(0), text_width(0), text_height(0), alignment(0), gutters{},
      font(nullptr), image_mode(0), image_layout(0), image_area{}, selected_image_x(0), image_x(0), image_y(0),
      image(nullptr), hint_text(), hint_token(0), hint_flags(0), hint_x(0), hint_y(0) {
  WnImageButton::SetFlag(kWnFlagReorderable, false);
  u32 pixel = WnRootWidget::MakeFillColor(192, 192, 192);
  WnRootWidget::SetFillColor(pixel);
  for (u32 &color : text_colors)
    color = kWnDefaultColor;
  sound_ids[0] = 0;
}

WnImageButton::~WnImageButton() {
  WnImageButton::Shutdown();
}

// Publish the secondary button interface before live reference acquisition; other IIDs use the root's query.
bool WnImageButton::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnButton)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnButton *>(this);
  AddRef();
  return true;
}

u32 WnImageButton::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnImageButton::Release() {
  return WnRootWidget::Release();
}

void WnImageButton::SetID(u32 value) {
  WnRootWidget::SetID(value);
}

// Return the original primary interface without reading state or acquiring another reference.
WnWindow *WnImageButton::GetWindow() {
  return static_cast<WnWindow *>(this);
}

bool WnImageButton::GetButtonFlag(i32 mask) const {
  return (button_flags & (u32)mask) != 0;
}

bool WnImageButton::LayoutCurrent() {
  i32 height = GetH(), width = GetW();
  return Layout(width, height);
}

void WnImageButton::SetCaption(const ScStringView &text) {
  WnRootWidget::SetCaption(text);
  MeasureText();
  LayoutCurrent();
}

void WnImageButton::SetKeyEquivalent(u32 key) {
  WnRootWidget::SetKeyEquivalent(key);
  SetCaption(*GetCaption());
}

// Retain the supplied font before releasing the previous one, then resolve only unset color pairs.
bool WnImageButton::SetFont(FtFont *value) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  if (value) {
    u32 first = value->Color(), second = font->EndColor();
    for (u32 i = 0; i != 6; ++i)
      if (text_colors[i] == kWnDefaultColor)
        text_colors[i] = i & 1 ? second : first;
  }
  LayoutCurrent();
  return true;
}

// Lazily acquire the provider font and preserve the WORD-height store before wrapped-width measurement.
void WnImageButton::MeasureText() {
  if (!font) {
    WnResources *resources = WnGetUiResources();
    if (resources) {
      font = resources->GetFont(kWnFontButton);
      if (!font)
        font = resources->GetFont(kWnFontDefault);
    }
    if (font)
      font->AddRef();
  }
  if (!font || !(button_flags & kWnButtonShowText)) {
    text_width = text_height = 0;
    return;
  }
  ScRect rectangle;
  if (button_flags & kWnButtonWrapText) {
    i32 width = GetW();
    width = (i32)((u32)width - gutters[2] - gutters[0]);
    u32 length = caption.Strlen();
    const char *bytes = caption.Data();
    u32 lines = font->WrapLines(bytes, length, (u32)width);
    text_height = (i16)(lines * font->Height(0));
    length = caption.Strlen();
    bytes = caption.Data();
    if (lines == 1) {
      font->TextArea(bytes, length, &rectangle);
      text_width = (i16)((u32)rectangle.right - (u32)rectangle.left);
    } else
      text_width = (i16)font->WrapWidth(bytes, length, (u32)(i32)text_height);
  } else {
    u32 length = caption.Strlen();
    const char *bytes = caption.Data();
    font->TextArea(bytes, length, &rectangle);
    text_width = (i16)((u32)rectangle.right - (u32)rectangle.left);
    text_height = (i16)((u32)rectangle.bottom - (u32)rectangle.top);
  }
}

// Select four/eight atlas states, preserving mode stores even when subsequent validation fails.
bool WnImageButton::SetStyle(i32 mode, GxBuffer *value, const ScRect *rectangle) {
  if (!mode)
    return false;
  image_mode = mode;
  ScRect piece{};
  if (mode == kWnButtonTextOnly)
    image_layout = 0;
  else if (mode >= kWnButtonPushStretch && mode <= kWnButtonToggleEightStateAlternate && rectangle) {
    image_layout = mode <= kWnButtonToggleFixed ? 1 : 3;
    piece = *rectangle;
    i32 width = (i32)((u32)piece.right - (u32)piece.left);
    piece.right = (i32)((u32)piece.left + (u32)(width / (mode <= kWnButtonToggleFixed ? 4 : 8)));
  } else
    return false;
  if (!value) {
    if (rectangle)
      piece = *rectangle;
    else
      piece = {0, 0, 128, 32};
  }
  return InstallImage(value, piece, image_layout, false);
}

// Install and retain the atlas before validating its layout; failed layout selection does not undo ownership.
bool WnImageButton::InstallImage(GxBuffer *value, const ScRect &rectangle, i32 layout, u8) {
  if ((flags & 0x4000) || image)
    return false;
  image = value;
  image_area = rectangle;
  image_layout = layout;
  if (image) {
    image->AddRef();
    if (image->GetTransparency())
      SetFlag(kWnFlagTransparentBuffer, true);
  }
  switch (image_layout) {
  case 0:
    SetFlag(kWnFlagTransparentBuffer, true);
    break;
  case 1:
  case 3:
    break;
  case 4:
    image_mode = 3;
    break;
  case 5:
    image_mode = 6;
    alignment = 2;
    break;
  default:
    return false;
  }
  if (button_flags & kWnButtonAutoFit)
    Fit();
  return LayoutCurrent();
}

// Clamp changed dimensions to the live button's limits before the base area update and final layout.
void WnImageButton::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width < 0)
    width = 0;
  if (height < 0)
    height = 0;
  if (width != GetW() || height != GetH()) {
    ScRect limits;
    if (GetSizeLimits(&limits)) {
      if (width < limits.left)
        width = limits.left;
      if (width > limits.right)
        width = limits.right;
      if (height < limits.top)
        height = limits.top;
      if (height > limits.bottom)
        height = limits.bottom;
    }
  }
  WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  Layout(width, height);
}

// Resize through the live root size method after obtaining minimum dimensions from the secondary interface.
bool WnImageButton::Fit() {
  ScRect limits;
  if (!GetSizeLimits(&limits))
    return false;
  SetSize(limits.left, limits.top);
  return true;
}

// Keep the base flag result while refreshing the button layout only for enable-state changes.
u32 WnImageButton::SetFlag(u32 mask, bool enabled) {
  u32 result = WnRootWidget::SetFlag(mask, enabled);
  if (mask == kWnFlagEnabled)
    LayoutCurrent();
  if (mask == kWnFlagVisible || mask == kWnFlagEnabled)
    InvalidateSelfAndParents();
  return result;
}

// Position signed WORD text extents inside the supplied area; unsupported alignment values reset both positions.
void WnImageButton::PlaceText(const ScRect &rectangle) {
  if (alignment < 0 || alignment > 2) {
    text_x = text_y = 0;
    return;
  }
  if (alignment == 0)
    text_x = (i16)((u32)rectangle.left +
        (u32)((i32)((u32)rectangle.right - (u32)text_width - (u32)rectangle.left) / 2));
  else if (alignment == 1)
    text_x = (i16)rectangle.left;
  else
    text_x = (i16)((u32)rectangle.right - (u32)text_width);
  text_y = (i16)((u32)rectangle.top +
      (u32)((i32)((u32)rectangle.bottom - (u32)text_height - (u32)rectangle.top) / 2));
}

// Select the original atlas state and text area for each layout, preserving live dimension/enable virtual calls.
bool WnImageButton::Layout(i32 width, i32 height) {
  InvalidateSelfAndParents();
  MeasureText();
  i32 layout = image_layout, piece_width = (i32)((u32)image_area.right - (u32)image_area.left);
  i32 piece_height = (i32)((u32)image_area.bottom - (u32)image_area.top);
  if (layout == 0) {
    if (button_flags & kWnButtonShowText) {
      i32 bottom = GetH();
      bottom = (i32)((u32)bottom - gutters[3]);
      i32 right = GetW();
      right = (i32)((u32)right - gutters[2]);
      PlaceText({gutters[0], gutters[1], right, bottom});
    }
    return true;
  }
  if (layout < 0)
    return false;
  if (layout == 1 || layout == 2) {
    selected_image_x = image_area.left;
    u32 mode = image_mode;
    if (GetFlag(kWnFlagEnabled)) {
      u32 state = button_flags;
      if ((state & kWnButtonPressed) || ((mode != 2 && mode != 3) && (state & kWnButtonChecked)))
        selected_image_x = (i32)((u32)selected_image_x + 2u * (u32)piece_width);
      else if (state & kWnButtonHovered)
        selected_image_x = (i32)((u32)selected_image_x + 3u * (u32)piece_width);
      else
        selected_image_x = (i32)((u32)selected_image_x + (u32)piece_width);
    }
    image_x = image_y = 0;
    if (button_flags & kWnButtonShowText)
      PlaceText({gutters[0], gutters[1], (i32)((u32)width - gutters[2]), (i32)((u32)height - gutters[3])});
    return true;
  }
  if (layout == 3) {
    selected_image_x = image_area.left;
    bool enabled = GetFlag(kWnFlagEnabled);
    u32 state = button_flags, selected;
    if (enabled) {
      if (state & kWnButtonPressed)
        selected = state & kWnButtonChecked ? 4 : 5;
      else if (state & kWnButtonHovered)
        selected = state & kWnButtonChecked ? 2 : 3;
      else
        selected = state & kWnButtonChecked ? 0 : 1;
    } else
      selected = state & kWnButtonChecked ? 6 : 7;
    selected_image_x = (i32)((u32)selected_image_x + selected * (u32)piece_width);
    if (alignment < 0 || alignment > 2)
      return false;
    image_x = alignment == 1 ? (i32)((u32)width - (u32)piece_width) : 0;
    image_y = (i32)((u32)height - (u32)piece_height) / 2;
    if (state & kWnButtonShowText) {
      i32 left = (i32)((u32)gutters[0] + (alignment == 1 ? 0u : (u32)piece_width));
      PlaceText({left, gutters[1], (i32)((u32)left + (u32)text_width), gutters[1] + text_height});
    }
    return true;
  }
  if (layout == 4) {
    selected_image_x = image_area.left;
    if (GetFlag(kWnFlagEnabled))
      selected_image_x = (i32)((u32)selected_image_x + (button_flags & kWnButtonPressed ? 2u : 1u) * (u32)piece_width);
    image_x = 0;
    image_y = (i32)((u32)height - (u32)piece_height) / 2;
    if (button_flags & kWnButtonShowText) {
      i32 bottom = GetH();
      bottom = (i32)((u32)bottom - gutters[3]);
      i32 right = GetW();
      right = (i32)((u32)right - gutters[2]);
      PlaceText({gutters[0], gutters[1], right, bottom});
    }
    return true;
  }
  if (layout != 5)
    return false;
  selected_image_x = image_area.left;
  if (GetFlag(kWnFlagEnabled)) {
    if (button_flags & kWnButtonPressed)
      selected_image_x = (i32)((u32)selected_image_x + 2u * (u32)piece_width);
    else
      selected_image_x = (i32)((u32)selected_image_x + (button_flags & kWnButtonChecked ? 3u : 1u) * (u32)piece_width);
  }
  if (alignment < 0 || alignment > 2)
    return false;
  image_x = alignment == 1 ? (i32)((u32)width - (u32)piece_width) : 0;
  image_y = (i32)((u32)height - (u32)piece_height) / 2;
  if (button_flags & kWnButtonShowText) {
    i32 left = (i32)((u32)gutters[0] + (alignment == 1 ? 0u : (u32)piece_width));
    PlaceText({left, gutters[1], (i32)((u32)left + (u32)text_width), gutters[1] + text_height});
  }
  return true;
}

// Report mode-specific minimum/maximum dimensions from the current text extents, gutters and atlas piece.
bool WnImageButton::GetSizeLimits(ScRect *limits) {
  i32 width = 0, height = 0;
  if (image_mode == 1) {
    *limits = {text_width + gutters[0] + gutters[2], text_height + gutters[1] + gutters[3], 65535, 65535};
  } else if (image_mode == 2 || image_mode == 4) {
    if (button_flags & kWnButtonShowText)
      limits->left = text_width + gutters[0] + gutters[2];
    else
      limits->left = (i32)((u32)image_area.right - (u32)image_area.left);
    if (image) {
      width = (i32)((u32)image_area.right - (u32)image_area.left);
      height = (i32)((u32)image_area.bottom - (u32)image_area.top);
      if (limits->left < width)
        limits->left = width;
      limits->top = limits->bottom = height;
    } else {
      limits->top = text_height + gutters[1] + gutters[3];
      limits->bottom = 65535;
    }
    limits->right = 65535;
  } else if (image_mode == 3 || image_mode == 5) {
    width = (i32)((u32)image_area.right - (u32)image_area.left);
    height = (i32)((u32)image_area.bottom - (u32)image_area.top);
    *limits = {width, height, width, height};
  } else if (image_mode == 6 || image_mode == 7) {
    width = (i32)((u32)image_area.right - (u32)image_area.left);
    height = (i32)((u32)image_area.bottom - (u32)image_area.top);
    if (button_flags & kWnButtonShowText) {
      limits->left = (i32)((u32)width + (u32)text_width + gutters[0] + gutters[2]);
      limits->top = text_height + gutters[1] + gutters[3];
      if (limits->top < height)
        limits->top = height;
      limits->right = 65535;
      limits->bottom = limits->top;
    } else
      *limits = {width, height, width, height};
  } else {
    if (image) {
      width = (i32)((u32)image_area.right - (u32)image_area.left);
      height = (i32)((u32)image_area.bottom - (u32)image_area.top);
      if (button_flags & kWnButtonShowText) {
        if (width < text_width)
          width = text_width;
        if (height < text_height)
          height = text_height;
      }
    } else if (button_flags & kWnButtonShowText) {
      width = text_width;
      height = text_height;
    } else
      width = height = 16;
    *limits = {width, height, width, height};
  }
  return true;
}

void WnImageButton::Notify(u32 code, uptr second, uptr third) {
  PostMsg(notification_target, kWnMsg_Command, code, second, third);
}

// Mutate button state before notifications and layout; only the listed masks trigger those original effects.
bool WnImageButton::SetButtonFlag(i32 mask, bool enabled) {
  button_flags = enabled ? button_flags | (u32)mask : button_flags & ~(u32)mask;
  switch (mask) {
  case 1:
    Notify(kWnCommandButtonOnChanged, GetID(), 0);
    break;
  case 0x1000:
    Notify(kWnCommandButtonPressedChanged, GetID(), enabled);
    break;
  case 4:
  case 0x20:
  case 0x40:
  case 0x2000:
    break;
  case kWnButtonInvalidateFlag:
    InvalidateSelfAndParents();
    return true;
  default:
    return true;
  }
  LayoutCurrent();
  return true;
}

// Queue the object activation command first, then reload the live identifier for its scalar command.
bool WnImageButton::Activate(u32) {
  Notify(kWnCommandButtonActivatedByWindow, reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), 0);
  Notify(kWnCommandButtonActivated, GetID(), 0);
  return true;
}

// Resolve missing font, sound and image defaults only before initialization, then initialize the real root.
bool WnImageButton::Init() {
  if (!(flags & 0x4000)) {
    WnResources *resources = WnGetUiResources();
    if ((button_flags & kWnButtonShowText) && !font && resources)
      SetFont(resources->GetFont(kWnFontDefault));
    if (!sound_ids[0] && resources)
      sound_ids[0] = resources->GetSoundId(2);
    if (!image_mode) {
      GxBuffer *value = resources ? resources->GetImage(kWnThemeMessageBoxButtonImageId) : nullptr;
      if (value)
        SetStyle(2, value, value->GetBufferArea());
      else
        SetStyle(2, nullptr, nullptr);
    }
    if (button_flags & kWnButtonAutoFit)
      Fit();
    WnRootWidget::Init();
  }
  return true;
}

bool WnImageButton::Shutdown() {
  CancelHint();
  HideWindow();
  ScRelease(&image);
  ScRelease(&font);
  WnRootWidget::Shutdown();
  return true;
}

// Draw the selected atlas state, tiling the middle third for stretchable modes with both original offsets zero.
bool WnImageButton::PaintImage() {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  i32 width = (i32)((u32)image_area.right - (u32)image_area.left);
  i32 height = (i32)((u32)image_area.bottom - (u32)image_area.top);
  ScRect source{selected_image_x, image_area.top, (i32)((u32)selected_image_x + (u32)width),
      (i32)((u32)image_area.top + (u32)height)};
  ScRect destination;
  if (image_mode == kWnButtonPushStretch || image_mode == kWnButtonToggleStretch) {
    i32 third = width / 3;
    source.right = (i32)((u32)source.left + (u32)third);
    destination = {draw_area.left, draw_area.top, (i32)((u32)draw_area.left + (u32)third),
        (i32)((u32)draw_area.top + (u32)height)};
    draw_buffer->Blt(image, source, destination, nullptr);
    source.left = (i32)((u32)source.left + (u32)third);
    source.right = (i32)((u32)source.right + (u32)third);
    destination.left = destination.right;
    destination.right = (i32)((u32)draw_area.right - (u32)third);
    draw_buffer->BltTiled(image, source, destination, 0, 0, nullptr);
    source.left = (i32)((u32)source.left + (u32)third);
    source.right = (i32)((u32)source.right + (u32)third);
    destination.left = destination.right;
    destination.right = draw_area.right;
    draw_buffer->Blt(image, source, destination, nullptr);
  } else {
    destination = {(i32)((u32)draw_area.left + (u32)image_x), (i32)((u32)draw_area.top + (u32)image_y),
        (i32)((u32)draw_area.left + (u32)image_x + (u32)width),
        (i32)((u32)draw_area.top + (u32)image_y + (u32)height)};
    draw_buffer->Blt(image, source, destination, nullptr);
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Draw the original raised/pressed bevel and optional inner outline when no atlas is installed.
bool WnImageButton::PaintFallback() {
  if (button_flags & kWnButtonNoFallbackPainting)
    return false;
  bool raised = true;
  if (image_mode >= kWnButtonPushStretch && image_mode <= kWnButtonPushFixed)
    raised = !(button_flags & kWnButtonPressed);
  else if (image_mode >= kWnButtonToggleStretch && image_mode <= kWnButtonToggleEightStateAlternate)
    raised = !(button_flags & kWnButtonPressedOrChecked);
  u32 color = GetFillColor();
  draw_buffer->DrawRaisedRect(draw_area, color, raised, true);
  if (button_flags & kWnButtonHovered) {
    if (!draw_buffer->Lock(GxLockFlags::Line))
      return false;
    ScRect rectangle{(i32)((u32)draw_area.left + 1), (i32)((u32)draw_area.top + 1),
        (i32)((u32)draw_area.right - 1), (i32)((u32)draw_area.bottom - 1)};
    draw_buffer->DrawRectangleOutline(rectangle, 65535, 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

// Choose enabled/hover/disabled color pairs and draw point or wrapped text through the actual bitmap font.
void WnImageButton::PaintText() {
  if (!(button_flags & kWnButtonShowText) || !font)
    return;
  u32 index = GetFlag(kWnFlagEnabled) ? button_flags & kWnButtonHovered ? 2 : 0 : 4;
  u32 first = text_colors[index], second = text_colors[index + 1];
  bool defaults = first == kWnDefaultColor || second == kWnDefaultColor;
  i32 offset = (button_flags & kWnButtonOffsetPressedText) && (button_flags & kWnButtonPressed);
  i32 x = (i32)((u32)draw_area.left + (u32)text_x + (u32)offset);
  i32 y = (i32)((u32)draw_area.top + (u32)text_y + (u32)offset);
  if (button_flags & kWnButtonWrapText) {
    ScRect rectangle{x, y, (i32)((u32)x + (u32)text_width), (i32)((u32)y + (u32)text_height)};
    if (defaults) {
      FtOutputStyle style = FtOutputStyle(text_alignment);
      u32 length = caption.Strlen();
      const char *bytes = caption.Data();
      font->DrawTextPara(draw_buffer, rectangle, bytes, length, style);
    } else
      font->DrawTextPara(draw_buffer, rectangle, caption.begin, caption.ScString::Strlen(), first, second,
          FtOutputStyle(text_alignment));
  } else if (defaults)
    font->DrawTextA(draw_buffer, x, y, caption.begin, caption.ScString::Strlen());
  else
    font->DrawTextA(draw_buffer, x, y, caption.begin, caption.ScString::Strlen(), first, second);
}

bool WnImageButton::GZPaint() {
  if (image)
    PaintImage();
  else
    PaintFallback();
  PaintText();
  return true;
}

bool WnImageButton::SetFontColor(i32 index, u32 color) {
  bool changed = text_colors[index] != color;
  text_colors[index] = color;
  if (changed)
    InvalidateSelfAndParents();
  return true;
}

bool WnImageButton::ClearFontColor(i32 index) {
  SetFontColor(index, kWnDefaultColor);
  return true;
}

// Store the source caption style, mapping its three known font alignments before live height/width layout calls.
bool WnImageButton::SetCaptionStyle(i32 style) {
  alignment = style;
  if (style == 0)
    text_alignment = 1;
  else if (style == 1)
    text_alignment = 0;
  else if (style == 2)
    text_alignment = 2;
  i32 height = GetH(), width = GetW();
  Layout(width, height);
  return true;
}

// Store the four actual gutter bytes, then query live height before width and ignore the layout result.
bool WnImageButton::SetGutters(u8 first, u8 second, u8 third, u8 fourth) {
  gutters[0] = first;
  gutters[1] = second;
  gutters[2] = third;
  gutters[3] = fourth;
  i32 height = GetH(), width = GetW();
  Layout(width, height);
  return true;
}

bool WnImageButton::SetGutters(u8 first, u8 second) {
  return SetGutters(first, second, first, second);
}

bool WnImageButton::ToggleOn() {
  return WnImageButton::SetButtonFlag(kWnButtonChecked, true);
}

bool WnImageButton::ToggleOff() {
  return WnImageButton::SetButtonFlag(kWnButtonChecked, false);
}

bool WnImageButton::IsOn() const {
  return (button_flags & kWnButtonChecked) != 0;
}

bool WnImageButton::SetChecked(bool enabled) {
  return WnImageButton::SetButtonFlag(kWnButtonChecked, enabled);
}

bool WnImageButton::IsChecked() const {
  return (button_flags & kWnButtonChecked) != 0;
}

// Widen the four stored unsigned gutter bytes into genuine signed 32-bit outputs, in the original write order.
bool WnImageButton::GetGutters(i32 *first, i32 *second, i32 *third, i32 *fourth) const {
  *first = gutters[0];
  *second = gutters[1];
  *fourth = gutters[3];
  *third = gutters[2];
  return true;
}

// Widen the first two unsigned gutter bytes without changing either stored value or the remaining gutters.
bool WnImageButton::GetGutters(i32 *first, i32 *second) const {
  *first = gutters[0];
  *second = gutters[1];
  return true;
}

i32 WnImageButton::GetStyle() {
  return (i32)image_mode;
}

i32 WnImageButton::GetImageW() const {
  return (i32)((u32)image_area.right - (u32)image_area.left);
}

i32 WnImageButton::GetImageH() const {
  return (i32)((u32)image_area.bottom - (u32)image_area.top);
}

bool WnImageButton::GetImageArea(ScRect *rectangle) const {
  *rectangle = image_area;
  return true;
}

// Retain the incoming image before releasing the live old field, then publish it and invalidate the button hierarchy.
bool WnImageButton::SetBuffer(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (image)
    image->Release();
  image = incoming;
  InvalidateSelfAndParents();
  return true;
}

bool WnImageButton::GetBuffer(GxBuffer **buffer) const {
  *buffer = image;
  return true;
}

bool WnImageButton::GetCaptionStyle(i32 *style) const {
  *style = alignment;
  return true;
}

u32 WnImageButton::GetID() const {
  return WnRootWidget::GetID();
}

// Return the borrowed caption without exposing a mutable string through the button interface.
const ScStringView *WnImageButton::GetCaption() const {
  return WnRootWidget::GetCaption();
}

bool WnImageButton::SetImage(i32, GxBuffer *, const ScRect &) {
  return false;
}

bool WnImageButton::GetImage(i32, GxBuffer **, ScRect *) const {
  return false;
}

bool WnImageButton::SetSoundID(i32 index, u32 value) {
  sound_ids[index] = value;
  return true;
}

u32 WnImageButton::GetSoundID(i32 index) const {
  return sound_ids[index];
}

// Return the image button's primary window identity at zero references for the class registry's retained query.
ScUnknown *WnConstructImageButton() {
  return static_cast<WnWindow *>(new WnImageButton);
}

WnHint::WnHint()
    : kind(1), flags(0), text(nullptr), rectangle{}, word28(0), color(kWnHintDefaultColor), word36(0), token(0) {
}

// Enter activates immediately; Space synthesizes the original local press, while other keys go to the target.
bool WnImageButton::OnKeyDown(u32 key, u32 modifiers) {
  GetCaption()->ToChar();
  if (key == kWnKeyReturn)
    Activate(modifiers);
  else if (key != kWnKeySpace)
    return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
  else if (!(button_flags & kWnButtonKeyPressed)) {
    WnImageButton::SetButtonFlag(kWnButtonKeyPressed, true);
    SendMsg(this, kWnMsg_MouseDownL, 1, 1, modifiers);
  }
  return true;
}

// Space clears its keyboard-held state before synthesizing release; other keys are forwarded unchanged.
bool WnImageButton::OnKeyUp(u32 key, u32 modifiers) {
  if (key != kWnKeySpace)
    return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
  WnImageButton::SetButtonFlag(kWnButtonKeyPressed, false);
  return SendMsg(this, kWnMsg_MouseUpL, 1, 1, modifiers);
}

// Begin a press once, toggling immediate buttons or taking capture before entering their pressed state.
bool WnImageButton::OnMouseDownL(i32, i32, u32 modifiers) {
  if (!(button_flags & kWnButtonPressed)) {
    CancelHint();
    if (button_flags & kWnButtonToggleOnPress) {
      WnImageButton::SetButtonFlag(kWnButtonChecked, !(button_flags & kWnButtonChecked));
      Activate(modifiers);
    } else {
      owner->SetCapture(this);
      WnImageButton::SetButtonFlag(kWnButtonPressed, true);
    }
    InvalidateSelfAndParents();
    PlaySound(0);
  }
  return true;
}

// Activate a captured press released inside; an outside release drops capture and plays its release sound.
bool WnImageButton::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  GetCaption()->ToChar();
  if (button_flags & kWnButtonToggleOnPress)
    return true;
  if (owner->GetCapture() == this) {
    if (IsPointInWindow(x, y)) {
      if (button_flags & kWnButtonPressed) {
        WnImageButton::SetButtonFlag(kWnButtonChecked, !(button_flags & kWnButtonChecked));
        Activate(modifiers);
      }
    } else {
      owner->ReleaseCapture(this);
      PlaySound(0);
    }
  }
  WnImageButton::SetButtonFlag(kWnButtonPressed, false);
  InvalidateSelfAndParents();
  return true;
}

// Hover captures the button and starts its hint; movement outside clears hover before dropping capture.
bool WnImageButton::OnMouseMove(i32 x, i32 y, u32) {
  GetCaption()->ToChar();
  if (!(button_flags & kWnButtonPressed)) {
    if (owner->GetCapture() != this) {
      owner->SetCapture(this);
      WnImageButton::SetButtonFlag(kWnButtonHovered, true);
      StartHint();
    } else if (IsPointInWindow(x, y)) {
      if (!(button_flags & kWnButtonHovered)) {
        WnImageButton::SetButtonFlag(kWnButtonHovered, true);
        StartHint();
      }
    } else {
      WnImageButton::SetButtonFlag(kWnButtonHovered, false);
      owner->ReleaseCapture(this);
    }
  }
  return true;
}

// Losing capture clears hover and press independently, then sends the original hint cancellation.
bool WnImageButton::GZOnCaptureChanged(WnWindow *window) {
  if (window != static_cast<WnWindow *>(this)) {
    WnImageButton::SetButtonFlag(kWnButtonHovered, false);
    WnImageButton::SetButtonFlag(kWnButtonPressed, false);
    CancelHint();
  }
  return true;
}

void WnImageButton::PlaySound([[maybe_unused]] u32 index) {
  assert(index == 0);
  if (sound_ids[0]) {
    AuSoundDevice *device = WnGetSoundDevice();
    if (device) {
      WnResources *resources = WnGetUiResources();
      if (resources) {
        i32 volume = resources->GetVolume();
        device->SndPlay((i32)sound_ids[0], volume, 0);
      }
    }
  }
}

// Copy nonempty tip text through the owned string interface. Empty input dispatches the live clear method.
bool WnImageButton::SetTipText(const ScStringView &text) {
  if (text.Strlen())
    hint_text.Copy(text);
  else
    ClearTipText();
  return true;
}

bool WnImageButton::GetTipText(ScStringView *text) const {
  text->Copy(hint_text);
  return true;
}

// Send the original hint cancellation before erasing all owned text.
bool WnImageButton::ClearTipText() {
  WnImageButton::CancelHint();
  hint_text.Erase(0, (u32)(hint_text.end - hint_text.begin));
  return true;
}

bool WnImageButton::SetTipPlacementType(i32 type) {
  hint_flags = (u32)type;
  return true;
}

bool WnImageButton::GetTipPlacementType(i32 *type) const {
  *type = (i32)hint_flags;
  return true;
}

// Retain the low word of each separate placement coordinate, as in the original two word stores.
bool WnImageButton::SetTipPlacementOffsets(i32 x, i32 y) {
  hint_x = (i16)x;
  hint_y = (i16)y;
  return true;
}

// Sign-extend the two stored coordinate words into distinct 32-bit outputs in x then y order.
bool WnImageButton::GetTipPlacementOffsets(i32 *x, i32 *y) const {
  *x = hint_x;
  *y = hint_y;
  return true;
}

bool WnImageButton::StartHint() {
  if (!(button_flags & kWnButtonShowHint) || !hint_text.Strlen())
    return false;
  WnHint hint;
  hint.text = &hint_text;
  if (button_flags & kWnButtonHintFlagOne)
    hint.flags |= 1;
  if (button_flags & kWnButtonHintFlagTwo)
    hint.flags |= 2;
  hint.flags |= hint_flags;
  if (hint.flags & 0x10000) {
    i32 x = hint_x, y = hint_y;
    WindowToScreenCoordinates(&x, &y);
    hint.rectangle = {x, y, x, y};
  } else
    hint.rectangle = absolute_area;
  ScMessage message{kWnMsg_StartHint, reinterpret_cast<uptr>(&hint), 0, 0};
  ScMessageServer *server = WnGetMessageServer();
  if (server)
    server->MessageSend(message);
  hint_token = hint.token;
  return true;
}

bool WnImageButton::CancelHint() {
  ScMessage message{kWnMsg_CancelHint, hint_token, 0, 0};
  ScMessageServer *server = WnGetMessageServer();
  if (server)
    server->MessageSend(message);
  return true;
}

WnLabel::WnLabel()
    : font(nullptr), padding{2, 2, 0, 0}, text_area{0, 0, 8, 8}, label_flags(6), alignment(0),
      text_colors{kWnDefaultColor, kWnDefaultColor}, text_alignment(0) {
}

WnLabel::~WnLabel() {
  Shutdown();
}

// Publish the label secondary identity before live AddRef; its own IID has the original null-output check.
bool WnLabel::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnLabelControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnLabelControl *>(this);
  AddRef();
  return true;
}

u32 WnLabel::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnLabel::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed canonical window view of this label.
WnWindow *WnLabel::GetWindow() {
  return static_cast<WnWindow *>(this);
}

// Before base initialization, acquire the default font only when the label has no font and a UI provider is present.
bool WnLabel::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  if (!font)
    if (WnResources *resources = WnGetUiResources())
      SetFont(resources->GetFont(kWnFontDefault));
  return WnRootWidget::Init();
}

// Hide the live window before releasing its font and delegating the base close result.
bool WnLabel::Shutdown() {
  HideWindow();
  ScRelease(&font);
  return WnRootWidget::Shutdown();
}

bool WnLabel::GetLabelFlag(i32 mask) {
  return (label_flags & (u32)mask) != 0;
}

// Update the label flag, recalculating wrapped layout or mirroring background transparency through real window flags.
bool WnLabel::SetLabelFlag(i32 mask, bool enabled) {
  if (enabled)
    label_flags |= (u32)mask;
  else
    label_flags &= ~(u32)mask;
  if (mask == kWnLabelWrapText) {
    Layout();
    InvalidateSelfAndParents();
  } else if (mask == kWnLabelFillBackground) {
    SetFlag(kWnFlagTransparentBuffer, !enabled);
    SetFlag(kWnFlagClearTransparentBuffer, GetFlag(kWnFlagTransparentBuffer));
    InvalidateSelfAndParents();
  }
  return true;
}

// Store one of two explicit colors, recompute whether they match the live font's default pair, then dirty the label.
bool WnLabel::SetTextColor(i32 index, u32 color) {
  if ((u32)index > 1)
    return false;
  text_colors[index] = color;
  bool matches = false;
  if (font) {
    u32 foreground = font->Color();
    if (text_colors[0] == foreground) {
      u32 background = font->EndColor();
      matches = text_colors[1] == background;
    }
  }
  SetLabelFlag(kWnLabelUseFontColors, matches);
  InvalidateSelfAndParents();
  return true;
}

bool WnLabel::SetTextColor(u32 color) {
  return SetTextColor(0, color);
}

bool WnLabel::SetBackground(bool enabled) {
  return SetLabelFlag(kWnLabelFillBackground, enabled);
}

// Retain the incoming font before releasing the live old one; fill sentinel colors, re-evaluate both colors and lay out.
bool WnLabel::SetFont(FtFont *value) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  if (value) {
    if (text_colors[0] == kWnDefaultColor)
      SetTextColor(0, value->Color());
    if (text_colors[1] == kWnDefaultColor)
      SetTextColor(1, font->EndColor());
  }
  SetTextColor(0, text_colors[0]);
  SetTextColor(1, text_colors[1]);
  Layout();
  InvalidateSelfAndParents();
  return true;
}

// Select one of nine original placements and its three-way text alignment; unchanged values skip layout and dirtiness.
bool WnLabel::SetAlignment(u32 value) {
  if (value == alignment)
    return true;
  if (value > 8)
    return false;
  text_alignment = value / 3;
  alignment = value;
  Layout();
  InvalidateSelfAndParents();
  return true;
}

// Store the two signed byte gutters and recompute text placement. The source returns true without dirtying the label.
bool WnLabel::SetGutters(i8 horizontal, i8 vertical) {
  padding[0] = horizontal;
  padding[1] = vertical;
  Layout();
  return true;
}

// Copy through the actual string interface before recomputing text placement and invalidating the label.
void WnLabel::SetCaption(const ScStringView &text) {
  WnRootWidget::SetCaption(text);
  Layout();
  InvalidateSelfAndParents();
}

// Preserve base area changes and relayout only when the clamped requested dimensions differ from the old dimensions.
void WnLabel::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width < 0)
    width = 0;
  if (height < 0)
    height = 0;
  bool width_changed = width != GetW(), height_changed = height != GetH();
  WnRootWidget::SetArea(left, top, right, bottom);
  if (width_changed || height_changed)
    Layout();
}

// Calculate wrapped text bounds or obtain the actual unwrapped font rectangle through its genuine output.
void WnLabel::GetTextExtent(ScRect *out) {
  if (!font) {
    *out = {0, 0, 1, 1};
    return;
  }
  if (GetLabelFlag(kWnLabelWrapText)) {
    i32 window_width = GetW();
    i32 width = (i32)((u32)window_width - (u32)(2 * padding[0]));
    if (width < 1)
      width = 1;
    FtFont *selected = font;
    u32 lines = selected->WrapLines(caption.begin, caption.ScString::Strlen(), (u32)width);
    *out = {0, 0, width, (i32)(selected->Height(0) * lines)};
  } else
    font->TextArea(caption.begin, caption.ScString::Strlen(), out);
}

// Recompute local text bounds from signed BYTE margins and the original top/center/bottom placement groups.
void WnLabel::Layout() {
  if (caption.begin == caption.end)
    return;
  u32 current_alignment = alignment;
  switch (current_alignment) {
  case 0:
  case 3:
  case 6: {
    GetArea(&text_area);
    i32 width = (i32)((u32)text_area.right - (u32)text_area.left);
    i32 height = (i32)((u32)text_area.bottom - (u32)text_area.top);
    text_area = {padding[0], padding[1], (i32)((u32)width - (u32)(i32)padding[0]),
        (i32)((u32)height - (u32)(i32)padding[1])};
    break;
  }
  case 1:
  case 4:
  case 7:
  case 2:
  case 5:
  case 8: {
    ScRect extent;
    GetTextExtent(&extent);
    i32 height = (i32)((u32)extent.bottom - (u32)extent.top);
    i32 window_height = GetH();
    i32 top = current_alignment % 3 == 1 ? (i32)((u32)window_height - (u32)height) / 2 :
        (i32)((u32)window_height - (u32)(i32)padding[1] - (u32)height);
    i32 gutter = padding[0];
    i32 bottom = (i32)((u32)top + (u32)extent.bottom - (u32)extent.top);
    i32 right = (i32)((u32)GetW() - (u32)gutter);
    text_area = {gutter, top, right, bottom};
    break;
  }
  }
}

// Fit the actual window to its current text extent plus both signed margins through the live SetSize virtual method.
bool WnLabel::Fit() {
  ScRect extent;
  GetTextExtent(&extent);
  SetSize((i32)((u32)extent.right + (u32)(2 * padding[0]) - (u32)extent.left),
      (i32)((u32)extent.bottom + (u32)(2 * padding[1]) - (u32)extent.top));
  return true;
}

// Fill the selected background, then render caption text with the original single- or dual-color branch.
bool WnLabel::GZPaint() {
  if ((label_flags & kWnLabelFillBackground) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  FtFont *selected = font;
  if (selected && caption.begin != caption.end) {
    bool defaults = (label_flags & kWnLabelUseFontColors) != 0;
    FtOutputStyle style = FtOutputStyle(text_alignment);
    u32 first = 0, second = 0, length;
    const char *bytes;
    if (defaults) {
      length = caption.Strlen();
      bytes = caption.Data();
    } else {
      first = text_colors[0];
      second = text_colors[1];
      bytes = caption.begin;
      length = caption.ScString::Strlen();
    }
    ScRect rectangle = text_area;
    rectangle.left = (i32)((u32)rectangle.left + (u32)draw_area.left);
    rectangle.right = (i32)((u32)rectangle.right + (u32)draw_area.left);
    rectangle.top = (i32)((u32)rectangle.top + (u32)draw_area.top);
    rectangle.bottom = (i32)((u32)rectangle.bottom + (u32)draw_area.top);
    if (defaults)
      font->DrawTextPara(draw_buffer, rectangle, bytes, length, style);
    else
      selected->DrawTextPara(draw_buffer, rectangle, bytes, length, first, second, style);
  }
  return true;
}

// Publish a valid indexed text pixel; invalid indices preserve the caller's output.
bool WnLabel::GetTextColor(i32 index, u32 *color) const {
  if ((u32)index > 1)
    return false;
  *color = text_colors[index];
  return true;
}

bool WnLabel::GetTextColor(u32 *color) {
  return GetTextColor(0, color);
}

// Restore a valid text color from the current font through the live indexed setter.
bool WnLabel::ClearTextColor(i32 index) {
  if (!index)
    return SetTextColor(0, font->Color());
  if (index == 1)
    return SetTextColor(1, font->EndColor());
  return false;
}

bool WnLabel::ClearTextColor() {
  return ClearTextColor(0);
}

bool WnLabel::IsBackgroundOpaque() {
  return GetLabelFlag(kWnLabelFillBackground);
}

// Publish signed gutter bytes in order, retaining alias-visible output stores.
bool WnLabel::GetGutters(i8 *horizontal, i8 *vertical) {
  *horizontal = padding[0];
  *vertical = padding[1];
  return true;
}

// Store both signed text offsets without layout or invalidation side effects.
bool WnLabel::SetTextOffsets(i8 horizontal, i8 vertical) {
  padding[2] = horizontal;
  padding[3] = vertical;
  return true;
}

// Publish the stored signed text offsets through their original byte outputs.
bool WnLabel::GetTextOffsets(i8 *horizontal, i8 *vertical) {
  *horizontal = padding[2];
  *vertical = padding[3];
  return true;
}

void WnLabel::SetFillColor(u32 color) {
  WnRootWidget::SetFillColor(color);
  InvalidateSelfAndParents();
}

// Dispatch keys through the root; permitted pointer messages translate into the current parent's coordinates before delivery.
bool WnLabel::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp)
    return WnRootWidget::DoMessage(message);
  if (!GetLabelFlag(kWnLabelForwardMessages) || !parent)
    return false;
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel) {
    i32 left = GetL();
    message->data1 = (u32)message->data1 + (u32)left;
    i32 top = GetT();
    message->data2 = (u32)message->data2 + (u32)top;
  }
  return parent->DoMessage(message);
}

bool WnLabel::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool WnLabel::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// The registered constructor returns the actual primary Unknown view at reference count zero, without a query or retain.
ScUnknown *WnConstructLabel() {
  return static_cast<WnWindow *>(new WnLabel);
}

WnOptionGroup::WnOptionGroup()
    : option_flags(11), button_class(0), autofill_mode(0), autofill_limit(128), horizontal_gutter(4),
      vertical_gutter(4), outline_colors{}, value(0), background(nullptr), label_font(nullptr), button_font(nullptr),
      font_colors{
          kWnUnsetOptionColor, kWnUnsetOptionColor, kWnUnsetOptionColor, kWnUnsetOptionColor, kWnUnsetOptionColor} {
}

WnOptionGroup::~WnOptionGroup() {
  WnOptionGroup::Shutdown();
}

// Publish the option secondary interface for its own IID before retaining through the live primary receiver.
bool WnOptionGroup::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnOptionControl)
    return WnRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnOptionControl *>(this);
  AddRef();
  return true;
}

u32 WnOptionGroup::AddRef() {
  return WnRootWidget::AddRef();
}

u32 WnOptionGroup::Release() {
  return WnRootWidget::Release();
}

// Return the borrowed primary window without acquiring a second reference.
WnWindow *WnOptionGroup::GetWindow() {
  return this;
}

int WnOptionGroup::GetOptionFlag(u32 mask) const {
  return (option_flags & mask) != 0;
}

// Change the requested bits first; exact caption changes manage the child label, while fill/outline changes invalidate.
bool WnOptionGroup::SetOptionFlag(u32 mask, u8 enabled) {
  if (enabled)
    option_flags |= mask;
  else
    option_flags &= ~mask;
  if (mask == 1) {
    if (enabled)
      EnsureLabel();
    else {
      WnWindow *label = GetChildWindowFromID(kWnOptionCaptionId);
      ChildDelete(label);
    }
  } else if (mask == 4 || mask == 8)
    InvalidateSelfAndParents();
  return true;
}

// Store both raw gutter bytes before requesting a live automatic resize, ignoring that resize's Boolean result.
bool WnOptionGroup::SetGutters(u8 horizontal, u8 vertical) {
  horizontal_gutter = horizontal;
  vertical_gutter = vertical;
  AutoSize();
  return true;
}

// Widen the two stored gutter bytes into the original separate signed outputs in horizontal/vertical order.
bool WnOptionGroup::GetGutters(i32 *horizontal, i32 *vertical) const {
  *horizontal = horizontal_gutter;
  *vertical = vertical_gutter;
  return true;
}

u32 WnOptionGroup::GetValue() const {
  return value;
}

// Clear the previous button before resolving the new one; publish a successful selection before flagging and posting
// its notice.
bool WnOptionGroup::SetValue(u32 requested) {
  void *slot = nullptr;
  if (GetChildAs(value, IID_WnButton, &slot)) {
    WnButton *button = static_cast<WnButton *>(slot);
    button->SetButtonFlag(kWnButtonChecked, false);
    button->Release();
  }
  slot = nullptr;
  if (!GetChildAs(requested, IID_WnButton, &slot))
    return false;
  WnButton *button = static_cast<WnButton *>(slot);
  value = requested;
  button->SetButtonFlag(kWnButtonChecked, true);
  button->Release();
  u32 group_id = GetID();
  PostMsg(notification_target, kWnMsg_Command, kWnCommandOptionSelectionChanged, group_id, 0);
  return true;
}

// Validate the resolved child through the live manager, delete it, clear matching selection and invalidate ancestors.
bool WnOptionGroup::DeleteButton(u32 requested) {
  WnWindow *button = GetChildWindowFromID(requested);
  if (!owner->IsWindowValid(button))
    return false;
  ChildDelete(button);
  if (value == requested)
    value = 0;
  for (WnWindow *window = GetParentWin(); window; window = window->GetParentWin())
    window->InvalidateSelf();
  return true;
}

// Resolve and validate the child before moving it through its actual primary virtual method.
bool WnOptionGroup::MoveButton(u32 requested, i32 x, i32 y) {
  WnWindow *button = GetChildWindowFromID(requested);
  if (!owner->IsWindowValid(button))
    return false;
  button->GZWinMoveTo(x, y);
  return true;
}

// Preserve the child lookup and manager validity check before changing the live child's dimensions.
bool WnOptionGroup::SizeButton(u32 requested, i32 width, i32 height) {
  WnWindow *button = GetChildWindowFromID(requested);
  if (!owner->IsWindowValid(button))
    return false;
  button->SetSize(width, height);
  return true;
}

bool WnOptionGroup::GetButton(u32 requested, WnButton **out) {
  return GetChildAs(requested, IID_WnButton, reinterpret_cast<void **>(out));
}

// Retain the incoming backdrop before releasing the old one, then publish it without forcing an invalidation.
bool WnOptionGroup::SetBackground(GxBuffer *image) {
  if (image)
    image->AddRef();
  if (background)
    background->Release();
  background = image;
  return true;
}

bool WnOptionGroup::SetButtonClass(u32 requested) {
  button_class = requested;
  return true;
}

// Resize from the first two minimum-size outputs only when the live limits method succeeds, then report success either
// way.
bool WnOptionGroup::AutoSize() {
  ScRect limits;
  if (GetMinMaxInfo(&limits))
    SetSize(limits.left, limits.top);
  return true;
}

// Only button command kind one selects an option; its selection result does not replace the command handler's success.
bool WnOptionGroup::OnCommand(u32 kind, u32 requested) {
  if (kind != kWnCommandButtonActivated)
    return false;
  SetValue(requested);
  return true;
}

// Update the current label first, then copy the root caption even when no label interface is available.
void WnOptionGroup::SetCaption(const ScStringView &text) {
  void *slot = nullptr;
  if (GetChildAs(kWnOptionCaptionId, IID_WnLabelControl, &slot)) {
    WnLabelControl *label = static_cast<WnLabelControl *>(slot);
    label->SetCaption(text);
    label->Release();
  }
  WnRootWidget::SetCaption(text);
}

bool WnOptionGroup::EnumFuncBtnFont(WnWindow *, u32, void *view, void *context) {
  return static_cast<WnButton *>(view)->SetFont(static_cast<FtFont *>(context));
}

// Apply the live group colors to all three button-state pairs, then release the acquired group view.
bool WnOptionGroup::EnumFuncBtnColors(WnWindow *parent, u32, void *view, void *context) {
  void *slot = nullptr;
  if (!parent->QueryInterface(IID_WnOptionControl, &slot))
    return false;
  WnOptionControl *group = static_cast<WnOptionControl *>(slot);
  WnButton *button = static_cast<WnButton *>(view);
  u32 *colors = static_cast<u32 *>(context);
  for (i32 pair = 0; pair < 3; ++pair) {
    if (group->GetOptionFlag(kWnOptionNormalButtonColorFlag << pair)) {
      button->SetFontColor(pair * 2, colors[pair + 2]);
      button->SetFontColor(pair * 2 + 1, colors[pair + 2]);
    } else {
      button->ClearFontColor(pair * 2);
      button->ClearFontColor(pair * 2 + 1);
    }
  }
  group->Release();
  return true;
}

// Inline the used effects of private color-flag and label-color phases without asserting their uncovered source return
// types.
void WnOptionGroup::RefreshLabelColors() {
  if ((option_flags & 1) && label_font) {
    u32 color = label_font->Color();
    WnOptionGroup::SetOptionFlag(kWnOptionEnabledCaptionColorFlag, font_colors[0] != color);
    WnOptionGroup::SetOptionFlag(kWnOptionDisabledCaptionColorFlag, font_colors[1] != color);
  }
  if (!(option_flags & 1))
    return;
  void *slot = nullptr;
  if (!GetChildAs(kWnOptionCaptionId, IID_WnLabelControl, &slot))
    return;
  WnLabelControl *label = static_cast<WnLabelControl *>(slot);
  if (GetFlag(kWnFlagEnabled)) {
    if (option_flags & kWnOptionEnabledCaptionColorFlag)
      label->SetTextColor(font_colors[0]);
  } else if (option_flags & kWnOptionDisabledCaptionColorFlag)
    label->SetTextColor(font_colors[1]);
  label->Release();
  InvalidateSelfAndParents();
}

// Inline the private button color-flag and enumeration phases, preserving invalidation before live child enumeration.
void WnOptionGroup::RefreshButtonColors() {
  if (button_font) {
    u32 color = button_font->Color();
    WnOptionGroup::SetOptionFlag(kWnOptionNormalButtonColorFlag, font_colors[2] != color);
    WnOptionGroup::SetOptionFlag(kWnOptionHoverButtonColorFlag, font_colors[3] != color);
    WnOptionGroup::SetOptionFlag(kWnOptionDisabledButtonColorFlag, font_colors[4] != color);
  }
  ApplyButtonColors();
}

void WnOptionGroup::ApplyButtonColors() {
  InvalidateSelfAndParents();
  EnumChildren(IID_WnButton, EnumFuncBtnColors, font_colors);
}

// Retain and publish the label font before updating the existing label, then recompute and apply its live enabled-state
// color.
bool WnOptionGroup::SetLabelFont(FtFont *font) {
  if (font)
    font->AddRef();
  if (label_font)
    label_font->Release();
  label_font = font;
  if (option_flags & 1) {
    void *slot = nullptr;
    if (GetChildAs(kWnOptionCaptionId, IID_WnLabelControl, &slot)) {
      WnLabelControl *label = static_cast<WnLabelControl *>(slot);
      label->SetFont(label_font);
      label->Fit();
      label->Release();
    }
  }
  RefreshLabelColors();
  return true;
}

// Retain the incoming font before releasing the previous font, enumerate that exact input, then refresh state color
// overrides.
bool WnOptionGroup::SetButtonFont(FtFont *font) {
  if (font)
    font->AddRef();
  if (button_font)
    button_font->Release();
  button_font = font;
  EnumChildren(IID_WnButton, EnumFuncBtnFont, font);
  RefreshButtonColors();
  return true;
}

bool WnOptionGroup::SetFontColor(i32 index, u32 color) {
  font_colors[index] = color;
  if (index >= 0 && index <= 1) {
    RefreshLabelColors();
    return true;
  }
  if (index >= 2 && index <= 4) {
    RefreshButtonColors();
    return true;
  }
  return false;
}

// Replace a supported color with its currently assigned font's default, preserving the live virtual setter call.
bool WnOptionGroup::ClearFontColor(i32 index) {
  if (index < 0 || index > 4)
    return false;
  FtFont *font = index <= 1 ? label_font : button_font;
  if (!font)
    return false;
  u32 color = font->Color();
  SetFontColor(index, color);
  return true;
}

// Acquire the borrowed resource font only when absent, initialize unset button colors, then retain through the live
// setter.
bool WnOptionGroup::EnsureButtonFont() {
  if (button_font)
    return true;
  WnResources *resources = WnGetUiResources();
  if (!resources)
    return false;
  FtFont *font = resources->GetFont(kWnFontEditor);
  if (!font)
    return false;
  u32 color = font->Color();
  for (u32 i = 2; i < 5; ++i)
    if (font_colors[i] == kWnUnsetOptionColor)
      font_colors[i] = color;
  SetButtonFont(font);
  return button_font != nullptr;
}

bool WnOptionGroup::EnsureLabelFont() {
  if (label_font)
    return true;
  WnResources *resources = WnGetUiResources();
  if (!resources)
    return false;
  FtFont *font = resources->GetFont(kWnFontEditor);
  if (!font)
    return false;
  u32 color = font->Color();
  for (u32 i = 0; i < 2; ++i)
    if (font_colors[i] == kWnUnsetOptionColor)
      font_colors[i] = color;
  SetLabelFont(font);
  return label_font != nullptr;
}

// Create the reserved caption label only when missing, configure it before attachment and refresh colors after
// releasing its view.
bool WnOptionGroup::EnsureLabel() {
  if (ChildExists(kWnOptionCaptionId))
    return true;
  if (!EnsureLabelFont())
    return false;
  WnResources *resources = WnGetUiResources();
  if (!resources)
    return false;
  const ScStringView *text = GetCaption();
  WnLabelControl *label = resources->CreateLabel(kWnOptionCaptionId, *text);
  if (!label)
    return false;
  label->SetFont(label_font);
  label->SetLabelFlag(kWnLabelWrapText, false);
  label->SetBackground(false);
  label->Fit();
  WnWindow *window = label->GetWindow();
  bool result = ChildAdd(window);
  label->Release();
  RefreshLabelColors();
  return result;
}

// Hide before releasing the backdrop, clear both fonts through live setters, then run root shutdown and report success.
bool WnOptionGroup::Shutdown() {
  HideWindow();
  ScRelease(&background);
  SetLabelFont(nullptr);
  SetButtonFont(nullptr);
  WnRootWidget::Shutdown();
  return true;
}

bool WnOptionGroup::EnumFuncEnable(WnWindow *, u32, void *view, void *context) {
  WnWindow *window = static_cast<WnButton *>(view)->GetWindow();
  bool enabled = *static_cast<bool *>(context);
  window->SetFlag(kWnFlagEnabled, enabled);
  return true;
}

u32 WnOptionGroup::SetFlag(u32 mask, bool enabled) {
  u32 result = WnRootWidget::SetFlag(mask, enabled);
  if (mask == kWnFlagEnabled) {
    EnumChildren(IID_WnButton, EnumFuncEnable, &enabled);
    RefreshLabelColors();
  }
  return result;
}

// Resolve both fonts and any caption label before initial layout, then run ordinary root initialization.
bool WnOptionGroup::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  if (!EnsureButtonFont() || !EnsureLabelFont() || ((option_flags & 1) && !EnsureLabel()))
    return false;
  if (autofill_mode) {
    i32 height = GetH(), width = GetW();
    LayoutEffects(width, height);
  } else
    AutoSize();
  return WnRootWidget::Init();
}

// Clamp changed dimensions against live size limits before the root geometry update, then lay out with those captured
// dimensions.
void WnOptionGroup::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 height = (i32)((u32)bottom - (u32)top), width = (i32)((u32)right - (u32)left);
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;
  if (width != GetW() || height != GetH()) {
    if (!autofill_mode)
      autofill_limit = height;
    ScRect limits;
    if (GetMinMaxInfo(&limits)) {
      if (width < limits.left)
        width = limits.left;
      if (width > limits.right)
        width = limits.right;
      if (height < limits.top)
        height = limits.top;
      if (height > limits.bottom)
        height = limits.bottom;
    }
  }
  WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  LayoutEffects(width, height);
}

void WnOptionGroup::SetAutoFillType(i32 type, i32 limit) {
  autofill_mode = type;
  autofill_limit = limit > 0 ? limit : 1;
}

// Store each outline pixel's original low word, then invalidate this group and its ancestors before reporting success.
bool WnOptionGroup::SetOutlineColors(u32 left, u32 top, u32 right, u32 bottom) {
  outline_colors[0] = (u16)left;
  outline_colors[1] = (u16)top;
  outline_colors[2] = (u16)right;
  outline_colors[3] = (u16)bottom;
  InvalidateSelfAndParents();
  return true;
}

bool WnOptionGroup::LayoutW(i32) {
  return false;
}

bool WnOptionGroup::LayoutH(i32) {
  return false;
}

// Create a unique radio button after acquiring its font, then configure and attach it before applying colors and
// layout.
bool WnOptionGroup::AddButton(
    u32 requested, const ScStringView &text, u32 key, GxBuffer *image, const ScRect *rectangle) {
  if (!requested || GetChildWindowFromID(requested) || !EnsureButtonFont())
    return false;
  WnResources *resources = WnGetUiResources();
  WnButton *button = image ? resources->CreateRadioButton(requested, text, image, rectangle, key)
                           : resources->CreateRadioButton(requested, text, nullptr, nullptr, 0);
  if (!button)
    return false;
  button->SetFont(button_font);
  button->SetButtonFlag(kWnButtonShowText, (option_flags & 2) != 0);
  WnWindow *window = button->GetWindow();
  bool result = ChildAdd(window);
  button->Release();
  ApplyButtonColors();
  i32 height = GetH(), width = GetW();
  LayoutEffects(width, height);
  InvalidateSelfAndParents();
  return result;
}

// Return the actual secondary option interface at reference count zero, preserving the registered constructor contract.
ScUnknown *WnConstructOptionGroup() {
  return static_cast<WnOptionControl *>(new WnOptionGroup);
}
