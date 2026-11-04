#include "ui/terrain_editor.h"

#include <assert.h>

#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"

#include "common/ids.h"

#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "graphics/image_codecs.h"
#include "resources/resource.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

// Terrain editing saves the original budget and supplies this temporary balance while editing.
enum UiTerrainEditorBudget : i32 {
  kUiTerrainEditorTemporaryFunds = 0x3b9aca00,
};

// Construct the terrain configuration window and return its primary window identity without acquiring a reference.
// Candidate: resource initialization and paint behavior require installed-code comparison.
ScUnknown *UiCreateClass82E968EE() {
  return static_cast<WnWindow *>(new UiTerrainConfig);
}

UiTerrainConfig::UiTerrainConfig()
    : center_type(kUiTerrainCenterNone), config_flags(0), rotation(0), hit(0), hovering(0), overlay{}, highlight{},
      background(nullptr), centers{}, mask(nullptr) {
  colors[0] = UiBaseWindow::MakeFillColor(255, 0, 255);
  colors[1] = UiBaseWindow::MakeFillColor(255, 0, 0);
  colors[2] = UiBaseWindow::MakeFillColor(255, 255, 0);
  colors[3] = UiBaseWindow::MakeFillColor(0, 255, 255);
  colors[4] = UiBaseWindow::MakeFillColor(0, 255, 0);
}

UiTerrainConfig::~UiTerrainConfig() {
}

// Publish the terrain IID and preserve the base identity fallback.
bool UiTerrainConfig::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiTerrainConfigInterface)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiTerrainConfigInterface *>(this);
  AddRef();
  return true;
}

u32 UiTerrainConfig::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiTerrainConfig::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window without adding another reference.
WnWindow *UiTerrainConfig::AsIGZWin() {
  return static_cast<WnWindow *>(this);
}

// Set or clear the selected low 16 flag bits and report the original success byte.
u8 UiTerrainConfig::SetWinTerrConfigFlag(i32 mask_value, bool enabled) {
  if (enabled)
    config_flags |= (u16)mask_value;
  else
    config_flags &= (u16)~(u32)mask_value;
  return 1;
}

i32 UiTerrainConfig::GetWinTerrConfigFlag(i32 mask_value) const {
  return (config_flags & (u32)mask_value) != 0;
}

u8 UiTerrainConfig::SetCenterType(u32 value) {
  center_type = value;
  return 1;
}

u8 UiTerrainConfig::GetCenterType(u32 *out) const {
  *out = center_type;
  return 1;
}

void UiTerrainConfig::SetRotation(i32) {
}

// Keep the requested origin while deriving the size from the live mask image.
void UiTerrainConfig::SetArea(i32 left, i32 top, i32, i32) {
  i32 width = 124, height = 62;
  if (mask) {
    width = (i32)mask->Width();
    height = (i32)mask->Height();
  }
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
}

// Publish and retain the image before setting its transparent native color.
u8 UiTerrainConfig::GetBuffer(u32 instance, GxBuffer **out) {
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef resource(instance, kRsTerrainEditorImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = resource.GetBuffer();
  *out = image;
  if (!image)
    return 0;
  image->AddRef();
  (*out)->SetTransparency(colors[0]);
  return 1;
}

// Attempt all source image loads, size the window and initialize the SIMUI base.
// Each load executes even when an earlier image failed; only the final combined byte reports that failure.
bool UiTerrainConfig::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  bool failed = !GetBuffer(18, &mask);
  failed = !GetBuffer(8, &background) || failed;
  failed = !GetBuffer(11, &highlight[0]) || failed;
  failed = !GetBuffer(12, &highlight[1]) || failed;
  failed = !GetBuffer(9, &highlight[2]) || failed;
  failed = !GetBuffer(10, &highlight[3]) || failed;
  failed = !GetBuffer(15, &overlay[0]) || failed;
  failed = !GetBuffer(16, &overlay[1]) || failed;
  failed = !GetBuffer(13, &overlay[2]) || failed;
  failed = !GetBuffer(14, &overlay[3]) || failed;
  failed = !GetBuffer(4, &centers[1]) || failed;
  failed = !GetBuffer(6, &centers[2]) || failed;
  failed = !GetBuffer(7, &centers[3]) || failed;
  failed = !GetBuffer(17, &centers[4]) || failed;
  SetSize(0, 0);
  UiBaseWindow::Init();
  return !failed;
}

// Hide first, then release mask, background, paired overlays and center images.
bool UiTerrainConfig::Shutdown() {
  HideWindow();
  ScRelease(&mask);
  ScRelease(&background);
  for (u32 i = 0; i < 4; ++i) {
    if (highlight[i]) {
      highlight[i]->Release();
      highlight[i] = nullptr;
    }
    if (overlay[i]) {
      overlay[i]->Release();
      overlay[i] = nullptr;
    }
  }
  for (u32 i = 0; i < 5; ++i) {
    if (centers[i]) {
      centers[i]->Release();
      centers[i] = nullptr;
    }
  }
  return UiBaseWindow::Shutdown();
}

bool UiTerrainConfig::OnMouseUpL(i32, i32, u32) {
  return true;
}

// Clear the hover state when capture moves to a different window.
bool UiTerrainConfig::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    hit = 0;
    hovering = 0;
  }
  return true;
}

bool UiTerrainConfig::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiTerrainConfig::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Update the colored region under the cursor and preserve capture ordering.
bool UiTerrainConfig::OnMouseMove(i32 x, i32 y, u32) {
  if (!IsPointInWindow(x, y)) {
    if (hovering) {
      hovering = 0;
      GZReleaseCapture();
      hit = 0;
    }
  } else {
    if (!hovering) {
      GZSetCapture();
      hovering = 1;
    }
    if (mask->Lock(GxLockFlags::GetSetPixel)) {
      u32 pixel = mask->GetPixel((u32)x, (u32)y);
      mask->Unlock(GxLockFlags::GetSetPixel);
      if (pixel == colors[0])
        hit = 0;
      else if (pixel == colors[1])
        hit = kUiTerrainCoastUpperLeft;
      else if (pixel == colors[2])
        hit = kUiTerrainCoastUpperRight;
      else if (pixel == colors[3])
        hit = kUiTerrainCoastLowerLeft;
      else if (pixel == colors[4])
        hit = kUiTerrainCoastLowerRight;
      else
        hit = kUiTerrainCenterHit;
    }
  }
  PullToFront();
  return true;
}

bool UiTerrainConfig::OnMouseDownL(i32, i32, u32) {
  UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  if (hit == kUiTerrainCoastUpperLeft || hit == kUiTerrainCoastUpperRight || hit == kUiTerrainCoastLowerLeft ||
      hit == kUiTerrainCoastLowerRight) {
    i32 selected = hit;
    SetWinTerrConfigFlag(selected, !GetWinTerrConfigFlag(selected));
    u16 current_flags = config_flags;
    u32 id = GetID();
    PostMsg(notification_target, kWnMsg_TerrainCoastsChanged, id, current_flags, 0);
    return true;
  }
  if (hit != kUiTerrainCenterHit)
    return false;
  ++center_type;
  if ((i32)center_type >= kUiTerrainCenterCount)
    center_type = kUiTerrainCenterNone;
  u32 current_center = center_type, id = GetID();
  PostMsg(notification_target, kWnMsg_TerrainCenterChanged, id, current_center, 0);
  return true;
}

// Draw the selected center, enabled coast overlays and current highlighted region.
// The destination stays locked across all source blits and the source reads the live image members between calls.
bool UiTerrainConfig::GZPaint() {
  assert(draw_buffer);
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  if (center_type >= kUiTerrainCenterLake && center_type <= kUiTerrainCenterPeak) {
    u32 index = center_type;
    GxBuffer *source = centers[index];
    assert(source);
    const ScRect *source_area = source->GetBufferArea();
    draw_buffer->Blt(centers[index], *source_area, draw_area, nullptr);
  }
  for (u32 i = 0; i < 4; ++i) {
    if (config_flags & (1u << i)) {
      i32 x = (i32)((u32)draw_area.left + ((i & 1u) ? 62u : 0u));
      i32 y = (i32)((u32)draw_area.top + ((i & 2u) ? 31u : 0u));
      ScRect destination{x, y, (i32)((u32)x + 62), (i32)((u32)y + ((i & 2u) ? 33u : 31u))};
      GxBuffer *source = overlay[i];
      assert(source);
      const ScRect *source_area = source->GetBufferArea();
      draw_buffer->Blt(overlay[i], *source_area, destination, nullptr);
    }
  }
  u32 selected = hit;
  if (selected == kUiTerrainCoastUpperLeft || selected == kUiTerrainCoastUpperRight ||
      selected == kUiTerrainCoastLowerLeft || selected == kUiTerrainCoastLowerRight ||
      selected == kUiTerrainCenterHit) {
    u32 index = selected == kUiTerrainCoastUpperLeft ? 0
        : selected == kUiTerrainCoastUpperRight      ? 1
        : selected == kUiTerrainCoastLowerLeft       ? 2
                                                     : 3;
    i32 x, y, width, height;
    GxBuffer *source;
    if (selected == kUiTerrainCenterHit) {
      x = (i32)((u32)draw_area.left + 18);
      y = (i32)((u32)draw_area.top + 9);
      width = 88;
      height = 44;
      source = background;
    } else {
      x = (i32)((u32)draw_area.left + ((index & 1u) ? 62u : 0u));
      y = (i32)((u32)draw_area.top + ((index & 2u) ? 31u : 0u));
      width = 62;
      height = (index & 2u) ? 33 : 31;
      source = highlight[index];
    }
    ScRect destination{x, y, (i32)((u32)x + (u32)width), (i32)((u32)y + (u32)height)};
    assert(source);
    const ScRect *source_area = source->GetBufferArea();
    GxBuffer *current = selected == kUiTerrainCenterHit ? background : highlight[index];
    draw_buffer->Blt(current, *source_area, destination, nullptr);
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Construct both original identities and initialize the source's reference count and stored result words.
// The two intervening unknown words are intentionally left uninitialized because the source does not write them here.
UiTerrainEditor::UiTerrainEditor()
    : references(0), center_type(kUiTerrainCenterNone), coast_flags(0), hills(0), water(0), trees(0), smoothing(0),
      saved_funds(0) {
  ResetToDefault();
}

UiTerrainEditor::~UiTerrainEditor() {
}

// Publish the primary procedure identity or its separate message-target view and retain the common owner. Unsupported
// interface IDs clear the output.
bool UiTerrainEditor::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiTerrainEditorInterface);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiTerrainEditorInterface);
  HANDLE_QUERY_INTERFACE(IID_UiTerrainEditorInterface, UiTerrainEditorInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 UiTerrainEditor::AddRef() {
  return ++references;
}

// Retire the owner through its virtual destructor when the decremented signed count is not positive.
u32 UiTerrainEditor::Release() {
  --references;
  if ((i32)references <= 0) {
    delete this;
    return 0;
  }
  return references;
}

u32 UiTerrainEditor::GetCenterType() {
  return center_type;
}

u16 UiTerrainEditor::GetCoastFlags() {
  return coast_flags;
}

u8 UiTerrainEditor::GetHills() {
  return hills;
}

u8 UiTerrainEditor::GetWater() {
  return water;
}

u8 UiTerrainEditor::GetTrees() {
  return trees;
}

u8 UiTerrainEditor::GetSmoothing() {
  return smoothing;
}

// Restore the default terrain center, coast and three intensity settings.
// The source leaves smoothing and the other owner fields unchanged.
void UiTerrainEditor::ResetToDefault() {
  hills = 0x80;
  water = 0x80;
  trees = 0x80;
  center_type = kUiTerrainCenterRiverZ;
  coast_flags = kUiTerrainCoastLowerRight;
}

// Return the new terrain editor's primary callback identity without adding a factory reference.
ScUnknown *UiCreateClass72E85997() {
  return static_cast<UiWindowCallback *>(new UiTerrainEditor);
}

// Preserve raw slider bytes and release the acquired control even when the identifier is not one of the three settings.
bool UiTerrainEditor::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (!window)
    return false;
  switch (message) {
  case kWnMsg_Command:
    if (first == kWnCommandButtonActivated)
      return OnButtonClicked(window, (u32)second) != 0;
    if (first == kWnCommandSliderValueChanged) {
      WnSliderControl *slider = nullptr;
      if (window->GetChildAs((u32)second, IID_WnSliderControl, reinterpret_cast<void **>(&slider))) {
        switch (second) {
        case kUiTerrainHillsSliderId:
          hills = (u8)slider->GetValue();
          break;
        case kUiTerrainWaterSliderId:
          water = (u8)slider->GetValue();
          break;
        case kUiTerrainTreesSliderId:
          trees = (u8)slider->GetValue();
          break;
        }
        slider->Release();
      }
    }
    break;
  case kWnMsg_KeyDown:
    return OnKeyDown(window, (u32)first, (u32)second) != 0;
  case kWnMsg_Init:
    return OnInit(window) != 0;
  case kWnMsg_Shutdown:
    return OnShutdown(window) != 0;
  case kWnMsg_TerrainCoastsChanged:
    coast_flags = (u16)(second & kUiTerrainAllCoasts);
    return true;
  case kWnMsg_TerrainCenterChanged:
    center_type = (u32)second;
    return true;
  }
  return false;
}

// Enter invokes the same action as the acceptance button and reports the key as handled.
u8 UiTerrainEditor::OnKeyDown(WnWindow *window, u32 key, u32) {
  if (key != kWnKeyReturn)
    return 0;
  OnButtonClicked(window, kUiTerrainAcceptButtonId);
  return 1;
}

// Construct the generic terrain window, configure its image and styles, and position it by the root.
u8 UiTerrainEditor::CreateTerrEditWin(WnWindow **out) {
  UiGenericInterface *generic = nullptr;
  if (!UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  *out = generic->GetWindow();
  (*out)->SetID(kUiTerrainEditorWindowId);
  const GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(2, kRsTerrainEditorImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  if (image) {
    generic->SetImage(1, image);
    generic->SetData(nullptr);
    generic->SetStyle(kUiGenericNoBevel, 1);
    i32 height = (i32)image->Height(), width = (i32)image->Width();
    (*out)->SetSize(width, height);
  } else {
    generic->SetStyle(kUiGenericNoBevel, 0);
    (*out)->SetSize(172, 480);
  }
  generic->SetStyle(kUiGenericNoFrame, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericCloseButton, 0);
  generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
  generic->SetCallback(this);
  WnWindow *main_window = UiGetWindowManager()->GetMainWindow();
  i32 x = 16, y = 16;
  if (main_window) {
    x = main_window->GetR() - 172;
    i32 height = (*out)->GetH();
    i32 root_height = main_window->GetH();
    i32 top = main_window->GetT();
    y = top + (root_height - height) / 2;
  }
  (*out)->GZWinMoveTo(x, y);
  return 1;
}

// Create the editor's vertical slider with the shared slider image.
static WnSliderControl *UiTerrainCreateSlider(u32 identifier) {
  WnResources *resources = UiGetWindowResources();
  WnSliderControl *slider = resources->CreateSlider(identifier, 2, 0, 255, 0);
  if (slider)
    slider->SetImage(resources->GetImage(kWnThemeSliderImageId), nullptr);
  return slider;
}

// Initialize and attach one terrain-value slider, destroying it if attachment fails.
static void UiTerrainAttachSlider(WnWindow *recipient, u32 identifier, u8 value, i32 left) {
  WnSliderControl *slider = UiTerrainCreateSlider(identifier);
  if (slider) {
    slider->SetMinimum(0);
    slider->SetMaximum(255);
    slider->SetValue(value);
    slider->GetWindow()->SetArea(left, 180, left + 24, 262);
    if (!recipient->ChildAdd(slider->GetWindow())) {
      WnWindowManager *manager = recipient->GetWindowManager();
      manager->DestroyWindow(slider->GetWindow());
    }
    slider->Release();
  }
}

// Create an image button and attach its window; the returned view is borrowed after the local release.
static WnButton *UiTerrainCreateImageButton(WnWindow *parent, i32 x, i32 y, u32 identifier, u32 group, u32 instance) {
  WnResources *resources = UiGetWindowResources();
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(instance, group, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  if (!image)
    return nullptr;
  const ScRect *area = image->GetBufferArea();
  WnButton *button;
  {
    ScString caption;
    button = resources->CreateFixedButton(identifier, caption, image, area, 0);
  }
  if (button) {
    image->SetTransparency(resources->GetColor(kWnColorUiTransparency));
    button->GetWindow()->GZWinMoveTo(x, y);
    i32 width = (i32)((u32)area->right - (u32)area->left);
    i32 height = (i32)((u32)area->bottom - (u32)area->top);
    button->GetWindow()->SetSize(width / 4, height);
    parent->ChildAdd(button->GetWindow());
    button->Release();
  }
  return button;
}

// Resolve the tooltip into its final string after releasing the temporary text resource and copies.
static ScString UiTerrainGetButtonTip(u32 instance, u32 group) {
  UiTextRef reference(instance, group);
  ScString text = reference.GetText();
  ScString copy(text);
  return ScString(copy);
}

// Install nonempty tooltip text and enable its original placement mode.
static void UiTerrainSetButtonTip(WnButton *button, u32 instance, u32 group) {
  if (button) {
    ScString text = UiTerrainGetButtonTip(instance, group);
    if (text.Strlen()) {
      button->SetTipText(text);
      button->SetButtonFlag(kWnButtonShowHint, true);
      button->SetTipPlacementType(kWnTipAboveCenter);
    }
  }
}

// Populate the terrain editor with its controls, subscribe to messages and prepare the city for editing.
u8 UiTerrainEditor::OnInit(WnWindow *recipient) {
  WnResources *resources = UiGetWindowResources();
  ScString caption;
  UiGenericInterface *generic = nullptr;
  if (!recipient->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  recipient->Release();
  UiTextRef title(kRsGameTerrainEditText, kRsGameTextGroup);
  if (title.IsResolved())
    title.CopyTo(&caption);
  else
    caption.Erase(0, caption.Strlen());
  recipient->SetCaption(caption);
  UiTextRef button_text(kRsGameAcceptThisTerrainText, kRsGameTextGroup);
  if (button_text.IsResolved())
    button_text.CopyTo(&caption);
  else
    caption.Erase(0, caption.Strlen());
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(5, kRsTerrainEditorImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  const ScRect *area = image ? image->GetBufferArea() : nullptr;
  WnButton *button = resources->CreateFixedButton(kUiTerrainAcceptButtonId, caption, image, area, 0);
  if (button) {
    if (!image)
      button->GetWindow()->SetSize(154, 55);
    button->SetFont(resources->GetFont(kWnFontUiTitle));
    static const u8 indices[6] = {1, 0, 3, 2, 5, 4};
    static const u8 colors[6][3] = {
        {92, 122, 64}, {255, 255, 255}, {92, 122, 64}, {255, 255, 255}, {128, 128, 128}, {64, 64, 64}};
    for (u32 index = 0; index < 6; ++index) {
      u32 color = button->GetWindow()->MakeFillColor(colors[index][0], colors[index][1], colors[index][2]);
      button->SetFontColor(indices[index], color);
    }
    button->SetGutters(20, 6, 45, 14);
    button->SetButtonFlag(kWnButtonWrapText, true);
    button->SetCaptionStyle(1);
    button->GetWindow()->GZWinMoveTo(16, 422);
    if (!recipient->ChildAdd(button->GetWindow())) {
      WnWindowManager *manager = recipient->GetWindowManager();
      manager->DestroyWindow(button->GetWindow());
    }
    button->Release();
  }
  UiTextRef second_text(kRsGameReGenerateTerrainText, kRsGameTextGroup);
  if (second_text.IsResolved())
    second_text.CopyTo(&caption);
  else
    caption.Erase(0, caption.Strlen());
  UiImageRef second_ref(3, kRsTerrainEditorImageGroup, GxVramRequest::SystemMemory, format);
  image = second_ref.GetBuffer();
  area = image ? image->GetBufferArea() : nullptr;
  button = resources->CreateFixedButton(kUiTerrainRegenerateButtonId, caption, image, area, 0);
  if (button) {
    if (!image)
      button->GetWindow()->SetSize(154, 55);
    button->SetFont(resources->GetFont(kWnFontUiTitle));
    static const u8 indices[6] = {1, 0, 3, 2, 5, 4};
    static const u8 colors[6][3] = {
        {92, 122, 64}, {255, 255, 255}, {92, 122, 64}, {255, 255, 255}, {128, 128, 128}, {64, 64, 64}};
    for (u32 index = 0; index < 6; ++index) {
      u32 color = button->GetWindow()->MakeFillColor(colors[index][0], colors[index][1], colors[index][2]);
      button->SetFontColor(indices[index], color);
    }
    button->SetGutters(20, 6, 45, 14);
    button->SetButtonFlag(kWnButtonWrapText, true);
    button->SetCaptionStyle(1);
    button->GetWindow()->GZWinMoveTo(16, 274);
    if (!recipient->ChildAdd(button->GetWindow())) {
      WnWindowManager *manager = recipient->GetWindowManager();
      manager->DestroyWindow(button->GetWindow());
    }
    button->Release();
  }
  UiTerrainAttachSlider(recipient, kUiTerrainHillsSliderId, hills, 46);
  UiTerrainAttachSlider(recipient, kUiTerrainWaterSliderId, water, 79);
  UiTerrainAttachSlider(recipient, kUiTerrainTreesSliderId, trees, 112);
  configuration = nullptr;
  if (UiGetCom()->GetClassObject(
          kUiTerrainConfigClassId, IID_UiTerrainConfigInterface, reinterpret_cast<void **>(&configuration))) {
    configuration->SetWinTerrConfigFlag(kUiTerrainCoastUpperLeft, (coast_flags & kUiTerrainCoastUpperLeft) != 0);
    configuration->SetWinTerrConfigFlag(kUiTerrainCoastUpperRight, (coast_flags & kUiTerrainCoastUpperRight) != 0);
    configuration->SetWinTerrConfigFlag(kUiTerrainCoastLowerLeft, (coast_flags & kUiTerrainCoastLowerLeft) != 0);
    configuration->SetWinTerrConfigFlag(kUiTerrainCoastLowerRight, (coast_flags & kUiTerrainCoastLowerRight) != 0);
    configuration->SetCenterType(center_type);
    configuration->AsIGZWin()->SetID(kUiTerrainConfigurationId);
    if (!recipient->ChildAdd(configuration->AsIGZWin())) {
      WnWindowManager *manager = recipient->GetWindowManager();
      manager->DestroyWindow(configuration->AsIGZWin());
    }
    configuration->AsIGZWin()->GZWinMoveTo(29, 54);
  }

  struct ButtonSpec {
    i32 x, y;
    u32 identifier, image, tip;
  };

  static const ButtonSpec buttons[] = {{22, 96, kUiTerrainZoomInButtonId, 0x13, 0x0a},
      {143, 96, kUiTerrainZoomOutButtonId, 0x14, 0x0b}, {41, 107, kUiTerrainRotateClockwiseButtonId, 0x15, 0x92},
      {124, 107, kUiTerrainRotateCounterclockwiseButtonId, 0x16, 0x93},
      {28, 342, kUiTerrainLowerToolButtonId, 0x17, 0x18}, {71, 342, kUiTerrainRaiseToolButtonId, 0x18, 0x19},
      {114, 342, kUiTerrainLevelToolButtonId, 0x19, 0x1a}, {71, 382, kUiTerrainWaterToolButtonId, 0x1a, 0x17},
      {28, 382, kUiTerrainTreeToolButtonId, 0x1b, 0x16}, {114, 382, kUiTerrainDemolishToolButtonId, 0x1c, 0x1b}};
  for (const ButtonSpec &spec : buttons) {
    WnButton *image_button =
        UiTerrainCreateImageButton(recipient, spec.x, spec.y, spec.identifier, kRsTerrainEditorImageGroup, spec.image);
    UiTerrainSetButtonTip(image_button, spec.tip, kRsGuiTextGroup);
  }
  UiBitmap *bitmap = new UiBitmap;
  preview = ScAddRefNotNull(static_cast<WnBitmapControl *>(bitmap));
  UiImageRef preview_ref(0x1d, kRsTerrainEditorImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *preview_image = preview_ref.GetBuffer();
  preview->SetImage(preview_image);
  preview->GetWindow()->GZWinMoveTo(20, 48);
  WnWindow *preview_window = preview->GetWindow();
  assert(preview_image);
  i32 height = (i32)preview_image->Height(), width = (i32)preview_image->Width();
  preview_window->SetSize(width, height);
  recipient->ChildAdd(preview->GetWindow());
  UiTextRef first_label_text(kRsGamePickFeaturesText, kRsGameTextGroup);
  if (first_label_text.IsResolved())
    first_label_text.CopyTo(&caption);
  else
    caption.Erase(0, caption.Strlen());
  WnLabelControl *label = resources->CreateLabel(kUiTerrainFeaturesLabelId, caption);
  if (label) {
    label->SetFont(resources->GetFont(kWnFontUiSmall));
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->SetAlignment(kWnLabelCenterTop);
    label->SetBackground(false);
    if (recipient->ChildAdd(label->GetWindow()))
      label->GetWindow()->SetArea(21, 35, 161, 49);
    else
      UiGetWindowManager()->DestroyWindow(label->GetWindow());
    label->Release();
  }
  UiTextRef second_label_text(kRsGameAdjustParametersText, kRsGameTextGroup);
  if (second_label_text.IsResolved())
    second_label_text.CopyTo(&caption);
  else
    caption.Erase(0, caption.Strlen());
  label = resources->CreateLabel(kUiTerrainParametersLabelId, caption);
  if (label) {
    label->SetFont(resources->GetFont(kWnFontUiSmall));
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->SetAlignment(kWnLabelCenterTop);
    label->SetBackground(false);
    if (recipient->ChildAdd(label->GetWindow()))
      label->GetWindow()->SetArea(21, 134, 161, 148);
    else
      UiGetWindowManager()->DestroyWindow(label->GetWindow());
    label->Release();
  }
  UiBorderPanel *panel = new UiBorderPanel;
  panel->AddRef();
  panel->SetFillColor(panel->MakeFillColor(123, 146, 255));
  panel->SetArea(5, 32, 11, 36);
  recipient->ChildAdd(panel);
  panel->Release();
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CameraChanged);
    server->AddNotification(static_cast<ScMessageTarget *>(this), kTrMsg_TerrainRegenerated);
  }
  EcBudgetLayer *budget = UiGetCity() ? UiGetCity()->BudgetLayer() : nullptr;
  if (budget) {
    saved_funds = budget->GetTotalFunds();
    budget->SetTotalFunds(kUiTerrainEditorTemporaryFunds);
  }
  SpCityWindowView *city_window = UiGetCityWindow();
  if (city_window)
    city_window->EnableCostDisplay(false);
  return 1;
}

// Remove both notifications before releasing the configuration and preview views.
// Restore saved city funds and re-enable the live city window's cost display after those releases.
u8 UiTerrainEditor::OnShutdown(WnWindow *) {
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_CameraChanged);
    server->RemoveNotification(static_cast<ScMessageTarget *>(this), kTrMsg_TerrainRegenerated);
  }
  ScRelease(&configuration);
  ScRelease(&preview);
  EcBudgetLayer *budget = UiGetCity() ? UiGetCity()->BudgetLayer() : nullptr;
  if (budget)
    budget->SetTotalFunds(saved_funds);
  SpCityWindowView *city_window = UiGetCityWindow();
  if (city_window)
    city_window->EnableCostDisplay(true);
  return 1;
}

// Forward the rotation and update its preview image, or flush pending input.
// Out-of-range centers pass the original message address as the 32-bit image instance identifier.
bool UiTerrainEditor::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_CameraChanged) {
    u32 center = (u32)message->data2;
    if (configuration)
      configuration->SetRotation((i32)center);
    if (preview) {
      u32 instance;
      switch (center) {
      case 0:
        instance = 29;
        break;
      case 1:
        instance = 32;
        break;
      case 2:
        instance = 30;
        break;
      case 3:
        instance = 31;
        break;
      default:
        instance = static_cast<u32>(reinterpret_cast<uptr>(message));
        break;
      }
      const GxColorType format(GxColorType::Color::RGB16);
      UiImageRef image_ref(instance, kRsTerrainEditorImageGroup, GxVramRequest::SystemMemory, format);
      GxBuffer *image = image_ref.GetBuffer();
      preview->SetImage(image);
      WnWindow *window = preview->GetWindow();
      assert(image);
      i32 height = (i32)image->Height(), width = (i32)image->Width();
      window->SetSize(width, height);
    }
  } else if (message->type == kTrMsg_TerrainRegenerated) {
    UiGetWindowManager()->FlushInputMessageQueue();
  }
  return true;
}

// Route terrain-editor buttons to the original audio, camera and command services.
u8 UiTerrainEditor::OnButtonClicked(WnWindow *, u32 identifier) {
  SpCityWindowView *city_window = UiGetCityWindow();
  switch (identifier) {
  case kUiFrameCloseButtonId:
    return 1;
  case kUiTerrainAcceptButtonId: {
    UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    ScMessage message(kWnMsg_EnterCity);
    UiGetMessageServer()->MessagePost(message, false);
    return 1;
  }
  case kUiTerrainRegenerateButtonId: {
    UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    ScMessage message;
    message.type = kTrMsg_RegenerateTerrain;
    u32 packed = ((u32)GetTrees() & 0xfeu) << 8;
    packed = (((u32)GetWater() & 0xfeu) | packed) << 7;
    message.data1 = ((u32)GetHills() >> 1) | packed;
    u32 flags = (GetCoastFlags() & kUiTerrainCoastUpperLeft) != 0;
    if (GetCoastFlags() & kUiTerrainCoastLowerLeft)
      flags |= 2;
    if (GetCoastFlags() & kUiTerrainCoastLowerRight)
      flags |= 4;
    if (GetCoastFlags() & kUiTerrainCoastUpperRight)
      flags |= 8;
    if (GetCenterType() == kUiTerrainCenterRiverX)
      flags |= 0x10;
    if (GetCenterType() == kUiTerrainCenterRiverZ)
      flags |= 0x20;
    if (GetCenterType() == kUiTerrainCenterLake)
      flags |= 0x40;
    if (GetCenterType() == kUiTerrainCenterPeak)
      flags |= 0x80;
    message.data2 = flags;
    UiGetMessageServer()->MessagePost(message, false);
    return 1;
  }
  case kUiTerrainZoomInButtonId:
    if (city_window)
      city_window->ZoomIn(true);
    return 1;
  case kUiTerrainZoomOutButtonId:
    if (city_window)
      city_window->ZoomOut(true);
    return 1;
  case kUiTerrainRotateClockwiseButtonId:
    if (city_window)
      city_window->RotateCameraCW(true);
    return 1;
  case kUiTerrainRotateCounterclockwiseButtonId:
    if (city_window)
      city_window->RotateCameraCCW(true);
    return 1;
  default:
    break;
  }
  u32 class_id;
  switch (identifier) {
  case kUiTerrainLowerToolButtonId:
    class_id = kTrLowerToolAliasClassId;
    break;
  case kUiTerrainRaiseToolButtonId:
    class_id = kTrRaiseToolAliasClassId;
    break;
  case kUiTerrainLevelToolButtonId:
    class_id = kTrLevelToolAliasClassId;
    break;
  case kUiTerrainTreeToolButtonId:
    class_id = kGeFloraToolClassId;
    break;
  case kUiTerrainWaterToolButtonId:
    class_id = kTrWaterToolClassId;
    break;
  case kUiTerrainDemolishToolButtonId:
    class_id = kGeDemolitionToolClassId;
    break;
  default:
    return 0;
  }
  ScCommandFactory *factory = nullptr;
  UiGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  if (factory) {
    UiCommandInterface *command = reinterpret_cast<UiCommandInterface *>((uptr)identifier);
    if (factory->GetCommand(class_id, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
      command->Execute();
      command->Release();
    }
    factory->Release();
  }
  return 1;
}
