#include "common/framework.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/text_edit.h"
#include "window/window.h"
#include "window/wn_resource_provider.h"

// Resolve the catalog button before fallback construction, then configure the live interface in original order.
WnButton *WnResourceProvider::CreateFixedButton(
    u32 identifier, const ScStringView &caption, GxBuffer *value, const ScRect *rectangle, u32 key_equivalent) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, &slot)) {
    slot = static_cast<WnButton *>(new WnImageButton);
    static_cast<WnButton *>(slot)->AddRef();
  }
  WnWindow *window = static_cast<WnButton *>(slot)->GetWindow();
  FtFont *font = GetFont(kWnFontButton);
  if (!value)
    value = GetImage(kWnThemeMessageBoxButtonImageId);
  ScRect bounds;
  if (rectangle)
    bounds = *rectangle;
  else if (value)
    value->GetBufferArea(&bounds);
  else if (font)
    bounds = {0, 0, 128, (i32)(font->Height(0) + 4)};
  else
    bounds = {0, 0, 128, 32};
  static_cast<WnButton *>(slot)->SetID(identifier);
  static_cast<WnButton *>(slot)->SetFont(font);
  static_cast<WnButton *>(slot)->SetCaption(caption);
  window->SetFillColor(GetColor(11));
  static_cast<WnButton *>(slot)->SetStyle(kWnButtonPushFixed, value, &bounds);
  window->SetKeyEquivalent(key_equivalent);
  return static_cast<WnButton *>(slot);
}

// Create the mode-two button using primary ID/caption setters, then background, font, image and key in source order.
WnButton *WnResourceProvider::CreateButton(
    u32 identifier, const ScStringView &caption, GxBuffer *value, const ScRect *rectangle, u32 key_equivalent) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, &slot)) {
    slot = static_cast<WnButton *>(new WnImageButton);
    static_cast<WnButton *>(slot)->AddRef();
  }
  WnWindow *window = static_cast<WnButton *>(slot)->GetWindow();
  FtFont *font = GetFont(kWnFontButton);
  if (!value)
    value = GetImage(kWnThemeMessageBoxButtonImageId);
  ScRect bounds;
  if (rectangle)
    bounds = *rectangle;
  else if (value)
    value->GetBufferArea(&bounds);
  else if (font)
    bounds = {0, 0, 128, (i32)(font->Height(0) + 4)};
  else
    bounds = {0, 0, 128, 32};
  window->SetID(identifier);
  window->SetCaption(caption);
  window->SetFillColor(GetColor(11));
  static_cast<WnButton *>(slot)->SetFont(font);
  static_cast<WnButton *>(slot)->SetStyle(kWnButtonPushStretch, value, &bounds);
  window->SetKeyEquivalent(key_equivalent);
  return static_cast<WnButton *>(slot);
}

// Create the mode-four toggle button with secondary ID/caption/font setters before the primary background setter.
WnButton *WnResourceProvider::CreateToggleButton(
    u32 identifier, const ScStringView &caption, GxBuffer *value, const ScRect *rectangle, u32 key_equivalent) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, &slot)) {
    slot = static_cast<WnButton *>(new WnImageButton);
    static_cast<WnButton *>(slot)->AddRef();
  }
  WnWindow *window = static_cast<WnButton *>(slot)->GetWindow();
  FtFont *font = GetFont(kWnFontButton);
  if (!value)
    value = GetImage(kWnThemeMessageBoxButtonImageId);
  ScRect bounds;
  if (rectangle)
    bounds = *rectangle;
  else if (value)
    value->GetBufferArea(&bounds);
  else if (font)
    bounds = {0, 0, 128, (i32)(font->Height(0) + 4)};
  else
    bounds = {0, 0, 128, 32};
  static_cast<WnButton *>(slot)->SetID(identifier);
  static_cast<WnButton *>(slot)->SetCaption(caption);
  static_cast<WnButton *>(slot)->SetFont(font);
  window->SetFillColor(GetColor(11));
  static_cast<WnButton *>(slot)->SetStyle(kWnButtonToggleStretch, value, &bounds);
  window->SetKeyEquivalent(key_equivalent);
  return static_cast<WnButton *>(slot);
}

// Create a catalog label or the actual fallback, then apply captured window/font inputs through live control methods.
WnLabelControl *WnResourceProvider::CreateLabel(u32 identifier, const ScStringView &caption) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, &slot)) {
    slot = static_cast<WnLabelControl *>(new WnLabel);
    static_cast<WnLabelControl *>(slot)->AddRef();
  }
  WnWindow *window = static_cast<WnLabelControl *>(slot)->GetWindow();
  FtFont *font = GetFont(kWnFontDefault);
  u32 color = font ? font->Color() : 0;
  window->SetID(identifier);
  window->SetCaption(caption);
  static_cast<WnLabelControl *>(slot)->SetFont(font);
  static_cast<WnLabelControl *>(slot)->SetTextColor(color);
  static_cast<WnLabelControl *>(slot)->SetAlignment(kWnLabelLeftMiddle);
  static_cast<WnLabelControl *>(slot)->SetBackground(false);
  static_cast<WnLabelControl *>(slot)->SetLabelFlag(kWnLabelWrapText, false);
  static_cast<WnLabelControl *>(slot)->Fit();
  return static_cast<WnLabelControl *>(slot);
}

// Acquire the actual option control or its concrete fallback, then configure the borrowed window and live outline colors.
// Preserve the source's rectangle argument order and its automatic-fill height after all callbacks.
WnOptionControl *WnResourceProvider::CreateOptGrp(u32 requested, const ScStringView &caption, const ScRect *rectangle) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnOptionGroupClassId, IID_WnOptionControl, &slot)) {
    slot = static_cast<WnOptionControl *>(new WnOptionGroup);
    static_cast<WnOptionControl *>(slot)->AddRef();
  }
  WnOptionControl *control = static_cast<WnOptionControl *>(slot);
  WnWindow *window = control->GetWindow();
  window->SetID(requested);
  window->SetCaption(caption);
  if (rectangle)
    window->SetArea(rectangle->left, rectangle->right, rectangle->top, rectangle->bottom);
  control->SetOptionFlag(1, 1);
  control->SetOptionFlag(4, 0);
  control->SetOptionFlag(8, 1);
  u32 bottom = GetColor(19), right = GetColor(20), top = GetColor(19), left = GetColor(19);
  control->SetOutlineColors(left, top, right, bottom);
  i32 height = rectangle ? (i32)((u32)rectangle->bottom - (u32)rectangle->top) : 128;
  control->SetAutoFillType(0, height);
  return control;
}

// Obtain a registered slider or its actual concrete fallback, then apply ID, raw orientation, atlas and range in source order.
WnSliderControl *WnResourceProvider::CreateSlider(
    u32 identifier, i32 orientation, i32 minimum, i32 maximum, i32 value) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnSliderClassId, IID_WnSliderControl, &slot)) {
    slot = static_cast<WnSliderControl *>(new WnSlider);
    static_cast<WnSliderControl *>(slot)->AddRef();
  }
  WnSliderControl *slider = static_cast<WnSliderControl *>(slot);
  WnWindow *window = slider->GetWindow();
  window->SetID(identifier);
  slider->SetDirection(static_cast<WnSliderDirection>(orientation));
  GxBuffer *image = GetImage(orientation == 1 ? 2 : 3);
  slider->SetImage(image, nullptr);
  slider->SetMinimum(minimum);
  slider->SetMaximum(maximum);
  slider->SetValue(value);
  return slider;
}

// Obtain a registered scrollbar or construct the concrete fallback, then configure its two borrowed views in source order.
WnScrollbarControl *WnResourceProvider::CreateScrollbar(
    u32 identifier, i32 orientation, i32 minimum, i32 maximum, i32 value) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnScrollbarClassId, IID_WnScrollbarControl, &slot)) {
    slot = static_cast<WnScrollbarControl *>(new WnScrollbar);
    static_cast<WnScrollbarControl *>(slot)->AddRef();
  }
  WnScrollbarControl *scrollbar = static_cast<WnScrollbarControl *>(slot);
  WnWindow *window = scrollbar->GetWindow();
  WnSliderControl *slider = scrollbar->GetSlider();
  GxBuffer *image = GetImage(kWnThemeScrollbarImageId);
  window->SetID(identifier);
  slider->SetDirection(static_cast<WnSliderDirection>(orientation));
  slider->SetImage(image, nullptr);
  slider->SetMinimum(minimum);
  slider->SetMaximum(maximum);
  scrollbar->SetValue(value);
  return scrollbar;
}

// Acquire a registered spinner or retain its concrete fallback, then apply atlas, numeric state, colors and ID in order.
// Setter results are ignored; the returned control keeps the acquired reference for its caller.
WnSpinnerControl *WnResourceProvider::CreateSpinner(
    u32 identifier, i32 type, i32 minimum, i32 maximum, i32 step, i32 value) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnSpinnerClassId, IID_WnSpinnerControl, &slot)) {
    slot = static_cast<WnSpinnerControl *>(new WnSpinner);
    static_cast<WnSpinnerControl *>(slot)->AddRef();
  }
  WnSpinnerControl *spinner = static_cast<WnSpinnerControl *>(slot);
  WnWindow *window = spinner->GetWindow();
  GxBuffer *image = GetImage(kWnThemeSpinnerImageId);
  u32 first = GetColor(19), second = GetColor(20);
  spinner->SetImage(image, nullptr);
  spinner->SetMinMax(minimum, maximum);
  spinner->SetValue(value);
  spinner->SetStepSize(step);
  spinner->SetSpinnerType(type);
  spinner->SetOutlineColors(first, first, second, second);
  u32 color = GetColor(11);
  spinner->SetFontColor(2, color);
  window->SetID(identifier);
  return spinner;
}

// Acquire the text interface or retain a concrete fallback, then apply editable state, border colors and window ID in order.
WnTextControl *WnResourceProvider::CreateTextEdit(u32 identifier, u8 editable) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, &slot)) {
    slot = static_cast<WnTextControl *>(new WnTextEdit(1));
    static_cast<WnTextControl *>(slot)->AddRef();
  }
  WnTextControl *control = static_cast<WnTextControl *>(slot);
  WnWindow *window = control->AsIGZWin();
  control->SetEditable(editable);
  control->SetOutlineType(WnInputOutline::ThreeD);
  u32 color = GetColor(19);
  control->SetOutlineColor(WnInputEdge::LeftTop, color);
  color = GetColor(20);
  control->SetOutlineColor(WnInputEdge::RightBottom, color);
  window->SetID(identifier);
  return control;
}

// Acquire the fixed toggle's real button identity before selecting its default image and bounds.
// Preserve the secondary setters followed by background, mode-five style and the primary key-equivalent setter.
WnButton *WnResourceProvider::CreateFixedToggleButton(
    u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, &slot)) {
    slot = static_cast<WnButton *>(new WnImageButton);
    static_cast<WnButton *>(slot)->AddRef();
  }
  WnButton *button = static_cast<WnButton *>(slot);
  WnWindow *window = button->GetWindow();
  FtFont *font = GetFont(kWnFontButton);
  if (!image)
    image = GetImage(kWnThemeMessageBoxButtonImageId);
  ScRect bounds;
  if (rectangle)
    bounds = *rectangle;
  else if (image)
    image->GetBufferArea(&bounds);
  else if (font)
    bounds = {0, 0, 128, (i32)(font->Height(0) + 4)};
  else
    bounds = {0, 0, 128, 32};
  button->SetID(identifier);
  button->SetCaption(caption);
  button->SetFont(font);
  window->SetFillColor(GetColor(11));
  button->SetStyle(kWnButtonToggleFixed, image, &bounds);
  window->SetKeyEquivalent(key_equivalent);
  return button;
}

// Build the checkbox with default image15, retaining the source's no-image bounds despite a supplied rectangle.
// Apply ID, font, caption and key before the two-byte gutters and mode-six style.
WnButton *WnResourceProvider::CreateCheckBox(
    u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, &slot)) {
    slot = static_cast<WnButton *>(new WnImageButton);
    static_cast<WnButton *>(slot)->AddRef();
  }
  WnButton *button = static_cast<WnButton *>(slot);
  WnWindow *window = button->GetWindow();
  FtFont *font = GetFont(kWnFontButton);
  if (!image)
    image = GetImage(kWnThemeCheckBoxImageId);
  ScRect bounds;
  if (image)
    if (rectangle)
      bounds = *rectangle;
    else
      image->GetBufferArea(&bounds);
  else if (font)
    bounds = {0, 0, 128, font->Height(0)};
  else
    bounds = {0, 0, 128, 32};
  button->SetID(identifier);
  button->SetFont(font);
  button->SetCaption(caption);
  window->SetKeyEquivalent(key_equivalent);
  button->SetGutters(2, 2);
  button->SetStyle(kWnButtonToggleEightState, image, &bounds);
  return button;
}

// Create the radio button with default image16 and two independent font-height calls for no-image square bounds.
// A supplied rectangle is consumed only when an image exists; configuration retains the original callback order.
WnButton *WnResourceProvider::CreateRadioButton(
    u32 identifier, const ScStringView &caption, GxBuffer *image, const ScRect *rectangle, u32 key_equivalent) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, &slot)) {
    slot = static_cast<WnButton *>(new WnImageButton);
    static_cast<WnButton *>(slot)->AddRef();
  }
  WnButton *button = static_cast<WnButton *>(slot);
  WnWindow *window = button->GetWindow();
  FtFont *font = GetFont(kWnFontButton);
  if (!image)
    image = GetImage(16);
  ScRect bounds;
  if (image) {
    if (rectangle)
      bounds = *rectangle;
    else
      image->GetBufferArea(&bounds);
  } else if (font) {
    i32 height = font->Height(0), width = font->Height(0);
    bounds = {0, 0, width, height};
  } else
    bounds = {0, 0, 16, 16};
  button->SetID(identifier);
  button->SetFont(font);
  button->SetCaption(caption);
  window->SetKeyEquivalent(key_equivalent);
  button->SetGutters(2, 2);
  button->SetStyle(kWnButtonToggleEightStateAlternate, image, &bounds);
  return button;
}

// Acquire the actual bitmap control, set its primary ID and retain the requested image through its original setter.
// Optional fitting reads height before width, then resizes the borrowed window without an extra reference transfer.
WnBitmapControl *WnResourceProvider::CreateWinBMP(u32 identifier, GxBuffer *image, bool fit) {
  void *slot = nullptr;
  if (!WnGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, &slot)) {
    slot = static_cast<WnBitmapControl *>(new WnBitmap);
    static_cast<WnBitmapControl *>(slot)->AddRef();
  }
  WnBitmapControl *control = static_cast<WnBitmapControl *>(slot);
  WnWindow *window = control->GetWindow();
  window->SetID(identifier);
  control->SetImage(image);
  if (fit && image) {
    i32 height = image->Height(), width = image->Width();
    window->SetSize(width, height);
  }
  return control;
}

// Publish the logical RGB bytes through three fresh inserting lookups, preserving growth and aliased output effects.
// This overload does not acquire graphics or convert the stored color into a native pixel.
void WnResourceProvider::GetColor(i32 identifier, u8 *red, u8 *green, u8 *blue) {
  *red = (u8)(colors.At((u32)identifier)->value >> 16);
  *green = (u8)(colors.At((u32)identifier)->value >> 8);
  *blue = (u8)colors.At((u32)identifier)->value;
}

// Capture two insert-on-miss nodes before graphics acquisition, then re-look up blue and consume only packed low16 bits.
u32 WnResourceProvider::GetColor(i32 identifier) {
  auto *first = colors.At((u32)identifier);
  auto *second = colors.At((u32)identifier);
  GxGraphics *graphics = WnGetGraphics();
  auto *third = colors.At((u32)identifier);
  u8 blue = (u8)third->value, green = (u8)(first->value >> 8), red = (u8)(second->value >> 16);
  return graphics->ConvertRGBValueToNative16Bit(red, green, blue);
}

// Erase the old scalar color before inserting the source's packed RGB value under the same unsigned identifier.
void WnResourceProvider::SetColor(i32 identifier, u8 red, u8 green, u8 blue) {
  if (auto *node = colors.Find((u32)identifier))
    colors.Erase(node);
  colors.Insert((u32)identifier, ((u32)red << 16) | ((u32)green << 8) | blue);
}

// Unpack the native low-word pixel and dispatch the RGB setter through the live resource-provider virtual method.
void WnResourceProvider::SetColor(i32 identifier, u32 color) {
  u8 red, green, blue;
  WnGetGraphics()->Convert16BitNativeValueToRGB((u16)color, &red, &green, &blue);
  SetColor(identifier, red, green, blue);
}

// Return the borrowed font, inserting a null placeholder and performing source growth even for a hit.
FtFont *WnResourceProvider::GetFont(i32 identifier) {
  return fonts.At((u32)identifier)->value;
}

// Let the live font manager release the old entry before erasing its captured node; retain a new font before insertion.
void WnResourceProvider::SetFont(i32 identifier, FtFont *font) {
  if (auto *node = fonts.Find((u32)identifier)) {
    font_manager->FontRelease(node->value);
    fonts.Erase(node);
  }
  if (font) {
    font->AddRef();
    fonts.Insert((u32)identifier, font);
  }
}

// Return the borrowed system palette, with the original null insertion and hit-growth effects.
GxPaletteView *WnResourceProvider::SystemPalette(i32 identifier) {
  return palettes.At((u32)identifier)->value;
}

// Release an existing palette unconditionally before node erasure. A null placeholder is outside defined setter execution.
void WnResourceProvider::SetSystemPalette(i32 identifier, GxPaletteView *palette) {
  if (auto *node = palettes.Find((u32)identifier)) {
    node->value->Release();
    palettes.Erase(node);
  }
  if (palette) {
    palette->AddRef();
    palettes.Insert((u32)identifier, palette);
  }
}

// Return the borrowed image, preserving source null-placeholder insertion and growth on hits.
GxBuffer *WnResourceProvider::GetImage(i32 identifier) {
  return images.At((u32)identifier)->value;
}

// A null old image skips Release; otherwise release precedes erasure, and new image retention precedes unique insertion.
void WnResourceProvider::SetImage(i32 identifier, GxBuffer *image) {
  if (auto *node = images.Find((u32)identifier)) {
    if (node->value)
      node->value->Release();
    images.Erase(node);
  }
  if (image) {
    image->AddRef();
    images.Insert((u32)identifier, image);
  }
}

// A sound-ID miss does not insert or grow; a hit still performs the original subscript's pre-lookup growth.
u32 WnResourceProvider::GetSoundId(i32 identifier) {
  if (!sound_ids.Find((u32)identifier))
    return 0;
  return sound_ids.At((u32)identifier)->value;
}

// Replace an existing scalar sound ID by erase followed by unique insertion, including an explicitly stored zero.
void WnResourceProvider::SetSoundId(i32 identifier, u32 sound) {
  if (auto *node = sound_ids.Find((u32)identifier))
    sound_ids.Erase(node);
  sound_ids.Insert((u32)identifier, sound);
}

i32 WnResourceProvider::GetVolume() {
  return volume;
}

void WnResourceProvider::SetVolume(i32 value) {
  volume = value;
}

// Text lookup is noninserting and returns a borrowed original String interface.
const ScStringView *WnResourceProvider::GetText(i32 identifier) {
  auto *node = texts.Find((u32)identifier);
  return node ? node->value : nullptr;
}

// Retain the actual String or resource-String interface, never copy its bytes or compensate a reentrant insertion.
void WnResourceProvider::SetText(i32 identifier, const ScStringView *text) {
  if (auto *node = texts.Find((u32)identifier)) {
    const_cast<ScStringView *>(node->value)->Release();
    texts.Erase(node);
  }
  if (text) {
    const_cast<ScStringView *>(text)->AddRef();
    texts.Insert((u32)identifier, text);
  }
}

// Ask live registered clients first; otherwise create the selected real control, query its requested interface and release it.
bool WnResourceProvider::CreateObject(u32 kind, u32 iid, void **out) {
  for (WnResourceNode *node = objects.head->next; node != objects.head; node = node->next)
    if (node->client->CreateAndSetup(kind, iid, out))
      return true;
  switch (kind) {
  case 1: {
    ScString caption;
    WnButton *button = CreateButton(0, caption, nullptr, nullptr, 0);
    bool result = button->QueryInterface(iid, out);
    button->Release();
    return result;
  }
  case 2: {
    ScString caption;
    WnButton *button = CreateFixedButton(0, caption, nullptr, nullptr, 0);
    bool result = button->QueryInterface(iid, out);
    button->Release();
    return result;
  }
  case 3: {
    ScString caption;
    WnButton *button = CreateToggleButton(0, caption, nullptr, nullptr, 0);
    bool result = button->QueryInterface(iid, out);
    button->Release();
    return result;
  }
  case 5: {
    WnTextControl *control = CreateTextEdit(0, 1);
    bool result = control->QueryInterface(iid, out);
    control->Release();
    return result;
  }
  case 6:
  case 7: {
    WnScrollbarControl *control = CreateScrollbar(0, kind == 6 ? 1 : 2, 0, 200, 100);
    bool result = control->QueryInterface(iid, out);
    control->Release();
    return result;
  }
  case 8: {
    ScString caption;
    ScUnknown *control = CreateLabel(0, caption);
    bool result = control->QueryInterface(iid, out);
    control->Release();
    return result;
  }
  default:
    return false;
  }
}

WnResourceProvider::WnResourceList::WnResourceList() : head(new WnResourceNode) {
  head->next = head;
  head->previous = head;
}

// Discard client-list storage only. Source shutdown performs its separate unregister/release phase first.
WnResourceProvider::WnResourceList::~WnResourceList() {
  WnResourceNode *node = head->next;
  while (node != head) {
    WnResourceNode *next = node->next;
    delete node;
    node = next;
  }
  delete head;
}

WnResourceProvider::WnResourceProvider()
    : WnSystemService(kWnResourcesServiceId, 0), initialized(false), volume(1024), font_manager(nullptr),
      sound_device(nullptr) {
}

WnResourceProvider::~WnResourceProvider() {
  Shutdown();
}

// The resource IID alone has the source null-output check; inherited basic/service queries retain their own contracts.
bool WnResourceProvider::QueryInterface(u32 iid, void **out) {
  if (iid != IID_WnResources)
    return WnSystemService::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnResources *>(this);
  AddRef();
  return true;
}

u32 WnResourceProvider::AddRef() {
  return WnObject::AddRef();
}

u32 WnResourceProvider::Release() {
  return WnObject::Release();
}

// Mark initialized before acquiring services; reentrant initialization therefore observes the already-set source flag.
bool WnResourceProvider::Init() {
  if (!initialized) {
    initialized = true;
    font_manager = WnGetFontManager();
    sound_device = WnGetSoundDevice();
    if (WnGetGraphics()) {
      static const struct {
        u32 identifier;
        u8 red, green, blue;
      } defaults[] = {{kWnColorBlack, 0, 0, 0}, {kWnColorDarkRed, 128, 0, 0}, {kWnColorDarkGreen, 0, 128, 0},
          {kWnColorOlive, 128, 128, 64}, {kWnColorNavy, 0, 0, 128}, {kWnColorPurple, 128, 0, 128},
          {kWnColorTeal, 0, 128, 128}, {kWnColorGray, 128, 128, 128}, {kWnColorSilver, 192, 192, 192},
          {kWnColorRed, 255, 0, 0}, {kWnColorLime, 0, 255, 0}, {kWnColorYellow, 255, 255, 0}, {kWnColorBlue, 0, 0, 255},
          {kWnColorCyan, 0, 255, 255}, {kWnColorWhite, 255, 255, 255}, {kWnColorCharcoal, 64, 64, 64}, {19, 64, 64, 64},
          {20, 192, 192, 192}, {21, 255, 255, 255}, {22, 192, 192, 192}, {24, 160, 160, 160}, {25, 255, 255, 255}};

      for (const auto &entry : defaults)
        SetColor(entry.identifier, entry.red, entry.green, entry.blue);
    }
  }
  return true;
}

// Perform the original live release traversals and clears in order, including sound IDs and registered client objects.
bool WnResourceProvider::Shutdown() {
  colors.Clear();
  for (auto *node = fonts.First(); node; node = fonts.Next(node))
    font_manager->FontRelease(node->value);
  fonts.Clear();
  for (auto *node = palettes.First(); node; node = palettes.Next(node))
    if (node->value)
      node->value->Release();
  palettes.Clear();
  for (auto *node = images.First(); node; node = images.Next(node))
    if (node->value)
      node->value->Release();
  images.Clear();
  for (auto *node = sound_ids.First(); node; node = sound_ids.Next(node))
    if (sound_device)
      sound_device->SndUnregister((i32)node->value, 1);
  sound_ids.Clear();
  while (objects.head->next != objects.head)
    RemoveObject(objects.head->next->client);
  for (auto *node = texts.First(); node; node = texts.Next(node))
    if (node->value)
      const_cast<ScStringView *>(node->value)->Release();
  texts.Clear();
  initialized = false;
  return true;
}

// Query validation acquires the original hold; remove any old entry, register the original receiver and prepend it.
bool WnResourceProvider::RegisterObject(WnResourceClient *client) {
  if (!client)
    return false;
  void *slot = nullptr;
  if (!client->QueryInterface(IID_WnResourceClient, &slot))
    return false;
  RemoveObject(client);
  if (!client->OnRegistered())
    return false;
  WnResourceNode *node = new WnResourceNode{objects.head->next, objects.head, client};
  node->next->previous = node;
  objects.head->next = node;
  return true;
}

// Unregister before reloading/releasing the captured entry, then erase that same node from the live list.
bool WnResourceProvider::RemoveObject(WnResourceClient *client) {
  WnResourceNode *node = objects.head->next;
  while (node != objects.head && node->client != client)
    node = node->next;
  if (node == objects.head)
    return false;
  node->client->OnRemoved();
  node->client->Release();
  node->previous->next = node->next;
  node->next->previous = node->previous;
  delete node;
  return true;
}

// Return the resource owner's primary system-service identity with its constructor's zero reference count.
ScUnknown *WnConstructResources() {
  return static_cast<ScSystemService *>(new WnResourceProvider);
}
