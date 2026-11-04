#include "ui/ui_resources.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "application/application_core.h"
#include "city/api.h"
#include "city/sc_random.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/api.h"
#include "ui/query_dialogs.h"
#include "ui/text_controls.h"
#include "ui/ui_module.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// The standard stretch-toggle resource loads this button image before applying its font and gutters.
enum UiToggleArtwork : u32 {
  kUiStandardToggleButtonImageId = 0xe2b99e02,
};

UiImageRef::UiImageRef(u32 instance, u32 group, GxVramRequest mode, const GxColorType &format) : buffer(nullptr) {
  RsManager *manager = UiGetResourceManager();
  RsFactoryInterface *factory;
  if (manager->FindObjectFactory(kRsImageResourceType, &factory)) {
    RsImageOptions *options;
    if (factory->QueryInterface(IID_RsImageOptions, reinterpret_cast<void **>(&options))) {
      options->SetMode(mode);
      options->SetFormat(format);
      Load(manager, group, instance);
      options->Release();
    }
    factory->Release();
  }
}

// Release the image retained by the scoped resource lookup before retiring the handle.
UiImageRef::~UiImageRef() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
}

// Borrow the resolved buffer without acquiring another reference.
GxBuffer *UiImageRef::GetBuffer() {
  return buffer;
}

bool UiImageRef::Load(RsManager *manager, u32 group, u32 instance) {
  RsKey key = {kRsImageResourceType, group, instance};
  return manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer));
}

UiTextRef::UiTextRef(u32 instance, u32 group) : text(nullptr), key{kRsTextResourceType, group, instance} {
  LoadResource();
}

UiTextRef::~UiTextRef() {
  ScRelease(&text);
}

bool UiTextRef::IsResolved() {
  return text != nullptr;
}

// Borrow the resolved String through the field getter folded with the image handle's GetBuffer implementation.
const ScStringView *UiTextRef::GetResource() const {
  return text;
}

bool UiTextRef::CopyTo(ScStringView *sink) {
  if (!text)
    return false;
  ScString copy(*text);
  sink->FromChar(copy.Data(), copy.Strlen());
  return true;
}

// Copy the resolved resource into an independent string, or return an empty string when resolution supplied no text.
ScString UiTextRef::GetText() {
  if (text)
    return ScString(*text);
  return ScString();
}

// Resolve the text key through the real manager; a false lookup explicitly clears a partially published pointer.
void UiTextRef::LoadResource() {
  bool result = UiGetResourceManager()->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(&text));
  if (!result)
    text = nullptr;
}

// Install all fourteen source kind selectors. A null initial provider excludes the original unwritten cleanup read.
UiResourceClient::UiResourceClient() : kinds(nullptr), references(0), provider(nullptr) {
  AddKindEffects(kUiResourceList, UiCreateResource75807646);
  AddKindEffects(kUiResourceListAlternate, UiCreateResource75807650);
  AddKindEffects(kUiResourceStandardButton, UiCreateResource7580764B);
  AddKindEffects(kUiResourceFlatPanel, UiCreateResource75807647);
  AddKindEffects(kUiResourceCaptionButton, UiCreateResource75807649);
  AddKindEffects(1, UiCreateResource75807649);
  AddKindEffects(kUiResourceColumns, UiCreateResource75807651);
  AddKindEffects(kUiResourceBuildingPreview, UiCreateResource75807652);
  AddKindEffects(kUiResourceCombo, UiCreateResource7580764E);
  AddKindEffects(kUiResourceReadOnlyCombo, UiCreateResource7580764F);
  AddKindEffects(kUiResourceLabel, UiCreateResource7580764D);
  AddKindEffects(kUiResourceEightStateButton, UiCreateResource75807648);
  AddKindEffects(kUiResourceToggleButton, UiCreateResource7580764A);
  AddKindEffects(kUiResourceOptionGroup, UiCreateResource7580764C);
}

// Insert a new unsigned selector while retaining the original handler for a duplicate key.
void UiResourceClient::AddKindEffects(u32 kind, u8 (*create)(WnResources *, u32, void **)) {
  UiResourceKind **before = &kinds;
  while (*before && (*before)->kind < kind)
    before = &(*before)->next;
  if (*before && (*before)->kind == kind)
    return;
  *before = new UiResourceKind{*before, kind, create};
}

// Release the live provider before retiring dispatch storage; callbacks during release still see the original provider.
UiResourceClient::~UiResourceClient() {
  if (provider)
    provider->Release();
  provider = nullptr;
  while (kinds) {
    UiResourceKind *node = kinds;
    kinds = node->next;
    delete node;
  }
}

// Both supported identities publish the same secondary client view before invoking the shared virtual retain.
bool UiResourceClient::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_WnResourceClient))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<WnResourceClient *>(this);
  AddRef();
  return true;
}

u32 UiResourceClient::AddRef() {
  UiGetDirector()->AddRef();
  return ++references;
}

// Drop the module hold before decrementing the object and dispatching its real virtual destructor at zero.
u32 UiResourceClient::Release() {
  UiGetDirector()->RemoveRef();
  u32 result = --references;
  if (!result)
    delete this;
  return result;
}

u32 UiResourceClient::GetCLSID() {
  return kUiResourceClientClassId;
}

u8 UiResourceClient::OnRegistered() {
  UiGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&provider));
  return provider != nullptr;
}

// Release the current provider before clearing the live field, including a replacement published during that release.
u8 UiResourceClient::OnRemoved() {
  if (provider)
    provider->Release();
  provider = nullptr;
  return 1;
}

// Dispatch an exact DWORD kind using the current provider and preserve the handler's raw byte and genuine output.
u8 UiResourceClient::CreateAndSetup(u32 kind, u32 iid, void **out) {
  for (UiResourceKind *node = kinds; node; node = node->next)
    if (node->kind == kind)
      return node->create(provider, iid, out);
  return 0;
}

// Construct the genuine zero-reference resource client and return the same secondary unknown identity as the source.
ScUnknown *UiCreateClass030477A4() {
  return static_cast<WnResourceClient *>(new UiResourceClient);
}

// Query the new column owner before resolving theme values, then configure both fonts and colors before dropping the temporary.
u8 UiCreateResource75807651(WnResources *provider, u32 iid, void **out) {
  UiColumnsControl *columns = new UiColumns;
  columns->AddRef();
  bool result = columns->QueryInterface(iid, out);
  if (result) {
    u32 background = provider->GetColor(kWnThemeBackgroundColorId);
    u32 first_color = provider->GetColor(kWnThemeMutedTextColorId);
    u32 second_color = provider->GetColor(kWnThemeTextColorId);
    FtFont *font = provider->GetFont(kWnFontUiSmall);
    u32 outline[4];
    UiGetResourceOutline(provider, outline);
    columns->GetWindow()->SetFillColor(background);
    columns->SetBorderStyle(2);
    columns->SetOutlineColors(outline[0], outline[2], outline[1], outline[3]);
    columns->SetFont(0, font);
    columns->SetFontColor(0, first_color);
    columns->SetFont(1, font);
    columns->SetFontColor(1, second_color);
  }
  columns->Release();
  return result;
}

// Resolve all four outline pixels independently; the final theme ID is deliberately queried twice.
u8 UiGetResourceOutline(WnResources *provider, u32 *pixels) {
  pixels[0] = provider->GetColor(kWnThemeOutlineMediumColorId);
  pixels[1] = provider->GetColor(kWnThemeOutlineDarkColorId);
  pixels[2] = provider->GetColor(kWnThemeOutlineLightColorId);
  pixels[3] = provider->GetColor(kWnThemeOutlineLightColorId);
  return 1;
}

// Acquire the preferred or fallback font before the list, then configure only a successful requested interface.
u8 UiCreateResource75807646(WnResources *provider, u32 iid, void **out) {
  FtFont *font = provider->GetFont(kWnFontUiMain);
  if (!font)
    font = provider->GetFont(kWnFontDefault);
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnListClassId, IID_WnListControl, &value))
    return 0;
  WnListControl *list = static_cast<WnListControl *>(value);
  bool result = list->QueryInterface(iid, out);
  if (result) {
    WnWindow *window = list->GetWindow();
    list->SetDrawSunkenRect(1);
    u32 second = provider->GetColor(20), first = provider->GetColor(19);
    list->SetOutlineColors(first, second);
    window->SetFillColor(provider->GetColor(kWnColorUiBackground));
    list->SetHighlightColor(provider->GetColor(kWnThemePanelFillColorId));
    list->SetHighlightFillAlpha(1.0f);
    list->SetFont(font);
    list->SetFontColor(0, provider->GetColor(kWnThemeTextColorId));
  }
  list->Release();
  return result;
}

// Reuse the configured list before querying the caller's interface, then apply font17 and a white window fill.
u8 UiCreateResource75807650(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiCreateResource75807646(provider, IID_WnListControl, &value))
    return 0;
  WnListControl *list = static_cast<WnListControl *>(value);
  bool result = list->QueryInterface(iid, out);
  if (result) {
    FtFont *font = provider->GetFont(kWnFontUiFileDialog);
    WnWindow *window = list->GetWindow();
    list->SetFont(font);
    window->SetFillColor(window->MakeFillColor(255, 255, 255));
  }
  list->Release();
  return result;
}

// Configure the queried combo and its borrowed line input, preserving repeated font-color and outline lookups.
u8 UiCreateResource7580764E(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnComboClassId, IID_WnComboControl, &value))
    return 0;
  WnComboControl *combo = static_cast<WnComboControl *>(value);
  bool result = combo->QueryInterface(iid, out);
  if (result) {
    provider->GetFont(kWnFontUiFileDialog);
    WnWindow *window = combo->GetWindow();
    WnTextControl *input = combo->GetLineInput();
    u32 color = provider->GetColor(kWnThemeTextColorId), outline[4];
    UiGetResourceOutline(provider, outline);
    combo->SetFont(provider->GetFont(kWnFontUiMain));
    combo->SetFontColor(0, color);
    combo->SetBtnGutter(1);
    ScRect limits;
    combo->GetMinMaxInfo(&limits);
    window->SetH(limits.top);
    window->SetFillColor(UiMakeResourceColor(window, 255, 255, 255));
    combo->SetHighlightFillAlpha(1.0f);
    combo->SetHighlightColor(UiMakeResourceColor(window, 0, 0, 255));
    combo->SetFontColor(0, color);
    if (input) {
      input->SetOutlineType(WnInputOutline::ThreeDEx);
      input->SetOutlineColor(WnInputEdge::LeftTop, outline[0]);
      input->SetOutlineColor(WnInputEdge::RightBottom, outline[2]);
      input->SetOutlineColor(WnInputEdge::LeftTopInside, outline[1]);
      input->SetOutlineColor(WnInputEdge::RightBottomInside, outline[0]);
    }
  }
  combo->Release();
  return result;
}

// Acquire the configured combo before the caller's query, then disable editing and increase its current height by one.
u8 UiCreateResource7580764F(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiCreateResource7580764E(provider, IID_WnComboControl, &value))
    return 0;
  WnComboControl *combo = static_cast<WnComboControl *>(value);
  bool result = combo->QueryInterface(iid, out);
  if (result) {
    FtFont *font = provider->GetFont(kWnFontUiFileDialog);
    WnWindow *window = combo->GetWindow();
    combo->SetEditable(0);
    combo->SetFont(font);
    window->SetH((i32)((u32)window->GetH() + 1));
  }
  combo->Release();
  return result;
}

// Configure the flat window before its requested interface query, preserving the WORD border color and final release.
u8 UiCreateResource75807647(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnFlatRectClassId, IID_WnFlatControl, &value))
    return 0;
  WnFlatControl *panel = static_cast<WnFlatControl *>(value);
  WnWindow *window = panel->GetWindow();
  u32 fill = provider->GetColor(kWnThemePanelFillColorId);
  u16 border = (u16)provider->GetColor(kWnThemeBorderColorId);
  window->SetFlag(kWnFlagReorderable, false);
  window->SetFillColor(fill);
  panel->SetOutlineColor(border);
  panel->SetOutlineFlags(15);
  bool result = panel->QueryInterface(iid, out);
  panel->Release();
  return result;
}

// Install the resource image before querying the requested button interface, releasing image before the temporary button.
u8 UiCreateResource7580764B(WnResources *, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnImageButtonClassId, IID_WnButton, &value))
    return 0;
  WnButton *button = static_cast<WnButton *>(value);
  const RsKey key = {kRsImageResourceType, kRsUiImageGroup, kRsStandardButtonImageId};
  GxBuffer *image = nullptr;
  bool result = false;
  if (UiLoadResourceBuffer(key, &image, 0)) {
    button->SetStyle(kWnButtonPushFixed, image, image->GetBufferArea());
    result = button->QueryInterface(iid, out);
    image->Release();
    image = nullptr;
  }
  button->Release();
  return result;
}

// Resolve the caption font before construction, then install the image and disabled text colors before the requested query.
u8 UiCreateResource75807649(WnResources *provider, u32 iid, void **out) {
  FtFont *font = provider->GetFont(kWnFontUiTitle);
  if (font)
    font->SetColor(provider->GetColor(kWnThemeOutlineDarkColorId));
  else
    font = provider->GetFont(kWnFontDefault);
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnImageButtonClassId, IID_WnButton, &value))
    return 0;
  WnButton *button = static_cast<WnButton *>(value);
  const RsKey key = {kRsImageResourceType, kRsUiImageGroup, kRsMessageBoxButtonImageId};
  GxBuffer *image = nullptr;
  bool result = false;
  if (UiLoadResourceBuffer(key, &image, 0)) {
    button->SetStyle(kWnButtonPushStretch, image, image->GetBufferArea());
    button->SetFont(font);
    button->SetFontColor(kWnButtonTextDisabledEnd, UiMakeResourceColor(button, 128, 128, 128));
    button->SetFontColor(kWnButtonTextDisabled, UiMakeResourceColor(button, 64, 64, 64));
    button->SetGutters(10, 0, 16, 4);
    button->Fit();
    result = button->QueryInterface(iid, out);
    image->Release();
    image = nullptr;
  }
  button->Release();
  return result;
}

// Configure caption colors and gutters only after the requested query, borrowing the provider's optional image.
u8 UiCreateResource75807648(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnImageButtonClassId, IID_WnButton, &value))
    return 0;
  WnButton *button = static_cast<WnButton *>(value);
  bool result = button->QueryInterface(iid, out);
  if (result) {
    GxBuffer *image = provider->GetImage(kWnThemeCheckBoxImageId);
    button->SetCaptionStyle(2);
    button->SetFont(provider->GetFont(kWnFontUiSystem));
    button->SetFontColor(kWnButtonTextEnabled, UiMakeResourceColor(button, 49, 52, 123));
    button->SetFontColor(kWnButtonTextHovered, UiMakeResourceColor(button, 49, 52, 123));
    button->SetFontColor(kWnButtonTextDisabled, UiMakeResourceColor(button, 125, 129, 181));
    button->SetGutters(2, 2);
    if (image)
      button->SetStyle(kWnButtonToggleEightState, image, image->GetBufferArea());
  }
  button->Release();
  return result;
}

// Keep requested-query success even when the image lookup fails; clear that image before configuring flags, font and gutters.
u8 UiCreateResource7580764A(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnImageButtonClassId, IID_WnButton, &value))
    return 0;
  WnButton *button = static_cast<WnButton *>(value);
  bool result = button->QueryInterface(iid, out);
  if (result) {
    const RsKey key = {kRsImageResourceType, kRsUiImageGroup, kUiStandardToggleButtonImageId};
    GxBuffer *image = nullptr;
    if (UiLoadResourceBuffer(key, &image, 0)) {
      button->SetStyle(kWnButtonToggleStretch, image, image->GetBufferArea());
      image->Release();
      image = nullptr;
    } else
      button->SetStyle(kWnButtonToggleStretch, nullptr, nullptr);
    button->SetButtonFlag(kWnButtonOffsetPressedText, false);
    button->SetFont(provider->GetFont(kWnFontUiSmall));
    button->SetGutters(8, 1, 11, 3);
  }
  button->Release();
  return result;
}

// Configure the option group's flags, manual fill mode and caption colors only after the requested query succeeds.
u8 UiCreateResource7580764C(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnOptionGroupClassId, IID_WnOptionControl, &value))
    return 0;
  WnOptionControl *options = static_cast<WnOptionControl *>(value);
  bool result = options->QueryInterface(iid, out);
  if (result) {
    options->SetOptionFlag(1, 0);
    options->SetOptionFlag(4, 0);
    options->SetOptionFlag(8, 0);
    options->SetAutoFillType(1, 0);
    options->SetGutters(0, 0);
    options->SetButtonFont(provider->GetFont(kWnFontUiSystem));
    options->SetFontColor(2, UiMakeResourceColor(options, 49, 52, 123));
    options->SetFontColor(3, UiMakeResourceColor(options, 49, 52, 123));
    options->SetFontColor(4, UiMakeResourceColor(options, 125, 129, 181));
  }
  options->Release();
  return result;
}

// Configure the queried label's flags, font and width only after its requested interface is available, then fit it.
u8 UiCreateResource7580764D(WnResources *provider, u32 iid, void **out) {
  void *value = nullptr;
  if (!UiQueryResourceClass(kWnLabelClassId, IID_WnLabelControl, &value))
    return 0;
  WnLabelControl *label = static_cast<WnLabelControl *>(value);
  bool result = label->QueryInterface(iid, out);
  if (result) {
    label->SetLabelFlag(kWnLabelFillBackground, false);
    label->SetLabelFlag(kWnLabelForwardMessages, true);
    label->SetLabelFlag(kWnLabelWrapText, false);
    label->SetFont(provider->GetFont(kWnFontUiMain));
    label->GetWindow()->SetW(4);
    label->Fit();
  }
  label->Release();
  return result;
}

UiResourceManagerRef::UiResourceManagerRef() : manager(nullptr) {
  UiGetFramework()->GetSystemService(kRsManagerServiceId, IID_RsManager, reinterpret_cast<void **>(&manager));
}

UiResourceManagerRef::~UiResourceManagerRef() {
  if (manager)
    manager->Release();
}

UiWindowResourcesRef::UiWindowResourcesRef() : provider(nullptr) {
  UiGetFramework()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&provider));
}

UiWindowResourcesRef::~UiWindowResourcesRef() {
  if (provider)
    provider->Release();
}

// Resolve the image key and optionally apply provider color zero, then publish a retained buffer before cleanup.
u8 UiLoadResourceBuffer(const RsKey &key, GxBuffer **out, u8 keyed) {
  if (!out)
    return 0;
  UiResourceManagerRef resources;
  if (!resources.manager)
    return 0;
  GxBuffer *buffer = nullptr;
  if (!resources.manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&buffer)))
    return 0;
  if (keyed) {
    UiWindowResourcesRef colors;
    if (colors.provider) {
      u32 color = colors.provider->GetColor(kWnColorUiTransparency);
      buffer->SetTransparency(color);
    }
  }
  buffer->AddRef();
  *out = buffer;
  buffer->Release();
  buffer = nullptr;
  return 1;
}

// Convert through the queried window's live color format and release that view only after a successful query.
u32 UiMakeResourceColor(ScUnknown *control, u32 red, u32 green, u32 blue) {
  void *value = nullptr;
  if (!control->QueryInterface(IID_WnWindow, &value))
    return 0;
  WnWindow *window = static_cast<WnWindow *>(value);
  u32 pixel = window->MakeFillColor((u8)red, (u8)green, (u8)blue);
  window->Release();
  return pixel;
}

// Configure the real preview's key and both fills before querying the caller's interface, then release the temporary view.
u8 UiCreateResource75807652(WnResources *provider, u32 iid, void **out) {
  UiPreviewControl *preview = new UiPreview;
  preview->AddRef();
  u32 background = provider->GetColor(kWnColorUiTransparency);
  u32 original = UiMakeResourceColor(preview, 192, 192, 224);
  u32 replacement = UiMakeResourceColor(preview, 255, 255, 208);
  preview->SetBackgroundColor(background);
  preview->SetOriginalColor(original);
  preview->SetReplacementColor(replacement);
  bool result = preview->QueryInterface(iid, out);
  preview->Release();
  return result;
}

static bool UiResourceCatalogInitialized;
static ScCom *UiResourceCatalog;

// Cache the borrowed catalog once, setting the flag before acquisition so reentry observes the current cache.
bool UiQueryResourceClass(u32 clsid, u32 iid, void **out) {
  if (!UiResourceCatalogInitialized) {
    UiResourceCatalogInitialized = true;
    UiResourceCatalog = UiGetCom();
  }
  return UiResourceCatalog ? UiResourceCatalog->GetClassObject(clsid, iid, out) : false;
}

UiTokenData::UiTokenData() : kind(0), name("%TOKEN%"), instance(0), group(0), number(0) {
}

// Copy the supplied interface through an independent string before storing its numeric replacement value.
// Resource identity starts empty even when the supplied kind later selects resource substitution.
UiTokenData::UiTokenData(u32 kind, const ScStringView &name, i32 number) {
  this->kind = kind;
  {
    ScString copy(name);
    this->name = copy;
  }
  instance = 0;
  group = 0;
  this->number = number;
}

UiTokenData::~UiTokenData() {
}

// Copy the kind before the owned name, then the resource identity and signed value; self-assignment changes nothing.
UiTokenData &UiTokenData::operator=(const UiTokenData &source) {
  if (this != &source) {
    kind = source.kind;
    name = source.name;
    instance = source.instance;
    group = source.group;
    number = source.number;
  }
  return *this;
}

bool UiTokenData::SerialRead(u32, RsStream *record) {
  return record->ReadU32(&kind) && record->ReadString(&name) && record->ReadU32(&instance) &&
      record->ReadU32(&group) && record->ReadI32(&number);
}

bool UiTokenData::SerialWrite(RsStream *record) const {
  return record->WriteU32(kind) && record->WriteString(&name) && record->WriteU32(instance) &&
      record->WriteU32(group) && record->WriteI32(number);
}

UiTokenData::UiTokenData(u32 value, const ScStringView &source, u32 resource_instance, u32 resource_group)
    : kind(value) {
  ScString copy(source);
  name = copy;
  number = 0;
  instance = resource_instance;
  group = resource_group;
}

UiTokenData::UiTokenData(u32 value, const ScStringView &source, u32 resource_instance) : kind(value) {
  ScString copy(source);
  name = copy;
  group = 0;
  number = 0;
  instance = resource_instance;
}

// Try the eight built-in keywords in order, retaining live city lookups and the original replacement scan position.
bool UiTextRef::SubstituteBuiltin(ScString *value, i32 *scan_position) {
  ScString &result = *value;
  u32 position = (u32)*scan_position;
  static const char *const keywords[] = {
      "%MAYOR%", "%YOURNAME%", "%CITYNAME%", "%YOURCITY%", "%POPULATION%", "%YEAR%", "%ANYNEIGHBOR%", "%PARADENAME%"};
  for (u32 kind = 0; kind < 8; ++kind) {
    u32 length = (u32)strlen(keywords[kind]);
    if (length > result.Strlen() - position || memcmp(result.Data() + position, keywords[kind], length))
      continue;
    if (kind < 4) {
      CtCity *city = UiGetCity();
      if (city) {
        ScStringView *first = kind < 2 ? city->GetMayorName() : city->GetCityName();
        ScStringView *second = kind < 2 ? city->GetMayorName() : city->GetCityName();
        u32 count = first->Strlen();
        const char *bytes = second->Data();
        result.Replace(position, length, bytes, count);
        ScStringView *third = kind < 2 ? city->GetMayorName() : city->GetCityName();
        position += third->Strlen() - 1;
      }
    } else if (kind == 4) {
      ScString replacement("0");
      CtCity *city = UiGetCity();
      if (city && city->ResidentialLayer()) {
        i32 population = city->ResidentialLayer()->GetPopulation();
        char number[16];
        int count = snprintf(number, sizeof(number), "%d", population);
        replacement.FromChar(number, (u32)count);
      }
      result.Replace(position, length, replacement.Data(), replacement.Strlen());
      position += replacement.Strlen() - 1;
    } else if (kind == 5) {
      CtCity *city = UiGetCity();
      if (city) {
        u32 month, day, year;
        city->GetDate(&month, &day, &year);
        ScString replacement;
        char number[16];
        int count = snprintf(number, sizeof(number), "%d", (i32)year);
        replacement.FromChar(number, (u32)count);
        result.Replace(position, length, replacement.Data(), replacement.Strlen());
        position += replacement.Strlen() - 1;
      } else {
        ScString replacement("1900");
        result.Replace(position, length, replacement.Data(), replacement.Strlen());
        position += replacement.Strlen() - 1;
      }
    } else if (kind == 6) {
      CtCity *city = UiGetCity();
      CtNeighborsLayer *neighbors = city ? city->NeighborsLayer() : nullptr;
      if (neighbors) {
        ScRandom random(kScRandomSeedFromClock);
        ScString replacement;
        neighbors->GetNeighborName((u8)random.Bounded(4), &replacement);
        result.Replace(position, length, replacement.Data(), replacement.Strlen());
        position += replacement.Strlen() - 1;
      } else
        return false;
    } else {
      ScString replacement;
      i32 country = UiGetPreferences()->GetCountry();
      u32 instance = 0;
      switch (country) {
      case 6:
        instance = 0x258;
        break;
      case 17:
        instance = 0x259;
        break;
      case 19:
        instance = 0x25a;
        break;
      case 31:
        instance = 0x25b;
        break;
      case 32:
        instance = 0x25c;
        break;
      case 45:
        instance = 0x25d;
        break;
      case 46:
        instance = 0x25e;
        break;
      case 49:
        instance = 0x25f;
        break;
      case 54:
        instance = 0x260;
        break;
      }
      if (instance) {
        UiTextRef resource(instance, kRsSupplementalGameTextGroup);
        replacement = resource.GetText();
      }
      result.Replace(position, length, replacement.Data(), replacement.Strlen());
      position += replacement.Strlen() - 1;
    }
    *scan_position = (i32)position;
    return true;
  }
  return false;
}

// Copy resolved text before expanding built-in keywords and skip all inserted replacement bytes during the scan.
ScString UiTextRef::GetExpandedText() {
  if (!text)
    return ScString();
  ScString result(*text);
  for (i32 position = 0; (u32)position < result.Strlen(); position = (i32)((u32)position + 1))
    if (result.Data()[(u32)position] == '%')
      SubstituteBuiltin(&result, &position);
  return result;
}

// Missing services preserve the text and scan position; formatter Boolean results are deliberately ignored.
bool UiTextRef::SubstituteCustom(ScString *value, UiTokenData *data, i32 *position) {
  ScString name(data->name), part = value->Mid((u32)*position, name.Strlen());
  u32 length = name.Strlen();
  if (part.Strlen() != length || memcmp(part.Data(), name.Data(), length))
    return false;
  ScString replacement;
  u32 kind = data->kind;
  switch (kind) {
  case 1: {
    UiTextRef resource(data->instance, data->group);
    replacement = resource.GetText();
    break;
  }
  case 2:
  case 3: {
    ScLanguageUtility *utility = UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
    if (!utility)
      return false;
    if (kind == 2)
      utility->MakeNumberString(data->number, &replacement);
    else
      utility->MakeMoneyString(data->number, &replacement);
    break;
  }
  case 4: {
    CtCity *city = UiGetCity();
    if (!city)
      return false;
    CtNeighborsLayer *neighbors = city->NeighborsLayer();
    if (!neighbors)
      return false;
    neighbors->GetNeighborName((u8)data->instance, &replacement);
    break;
  }
  case 5:
    replacement.Sprintf("%d", data->number);
    break;
  default:
    return false;
  }
  value->Replace((u32)*position, length, replacement.Data(), replacement.Strlen());
  *position = (i32)((u32)*position + replacement.Strlen() - 1);
  return true;
}

UiBitmap::UiBitmap() : image(nullptr), alpha_enabled(0), opacity(0.5f), source_area{}, bitmap_flags(2) {
}

UiBitmap::~UiBitmap() {
  UiBitmap::Shutdown();
}

bool UiBitmap::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_WnBitmapControl, WnBitmapControl);
  return UiBaseWindow::QueryInterface(iid, out);
}

u32 UiBitmap::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiBitmap::Release() {
  return UiBaseWindow::Release();
}

// Return the borrowed primary from the bitmap-control view without another retain; the original adjustor is shared.
WnWindow *UiBitmap::GetWindow() {
  return this;
}

// Keep an initialized receiver intact; otherwise capture height then width and clamp source bounds before UI startup.
bool UiBitmap::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  i32 height = GetH(), width = GetW();
  ScRect rectangle = {0, 0, width, height};
  SetSourceBufferAreaFromWindowArea(rectangle);
  return UiBaseWindow::Init();
}

bool UiBitmap::Shutdown() {
  ScRelease(&image);
  return UiBaseWindow::Shutdown();
}

void UiBitmap::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiBaseWindow::SetArea(left, top, right, bottom);
  SetSourceBufferAreaFromWindowArea(*GetArea());
}

// Retain the incoming image before retiring the old one, publish it, recalculate bounds and invalidate the live window.
void UiBitmap::SetImage(GxBuffer *incoming) {
  if (incoming)
    incoming->AddRef();
  if (image)
    image->Release();
  image = incoming;
  SetSourceBufferAreaFromWindowArea(*GetArea());
  InvalidateSelfAndParents();
}

// Build zero-origin bounds from the supplied dimensions, then clamp through each live image-size query in source order.
void UiBitmap::SetSourceBufferAreaFromWindowArea(const ScRect &rectangle) {
  i32 height = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
  i32 width = (i32)((u32)rectangle.right - (u32)rectangle.left);
  source_area = {0, 0, width, height};
  if (image) {
    i32 image_width = image->Width();
    if (source_area.right > image_width)
      source_area.right = image->Width();
    i32 image_height = image->Height();
    if (source_area.bottom > image_height)
      source_area.bottom = image->Height();
  }
}

// Preserve the Boolean enable and unchanged float amount before invalidating the live primary window.
void UiBitmap::SetAlpha(bool enabled, f32 amount) {
  alpha_enabled = enabled;
  opacity = amount;
  InvalidateSelfAndParents();
}

bool UiBitmap::GetBitmapFlag(i32 mask) const {
  return (bitmap_flags & (u32)mask) != 0;
}

// Change only the selected bitmap bits; this genuine void setter does not invalidate or report prior state.
void UiBitmap::SetBitmapFlag(i32 mask, bool enabled) {
  if (enabled)
    bitmap_flags |= (u32)mask;
  else
    bitmap_flags &= ~(u32)mask;
}

bool UiBitmap::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  return !GetBitmapFlag(4) && UiBaseWindow::IsPointInWindowScreenCoordinates(x, y);
}

// Dispatch keyboard messages through the UI root; flagged mouse messages translate coordinates before parent delivery.
bool UiBitmap::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp)
    return UiBaseWindow::DoMessage(message);
  if (!GetBitmapFlag(1) || !parent)
    return false;
  if (message->type >= kWnMsg_MouseDownL && message->type <= kWnMsg_MouseWheel) {
    message->data1 = (u32)message->data1 + (u32)GetL();
    message->data2 = (u32)message->data2 + (u32)GetT();
  }
  return parent->DoMessage(message);
}

// Fill when enabled, then alpha-blit or copy the source rectangle into the live draw area under separate buffer locks.
// Failed locks leave their corresponding work untouched while preserving the source's successful paint result.
bool UiBitmap::GZPaint() {
  if (!(bitmap_flags & 2) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 pixel = GetFillColor();
    draw_buffer->Fill(pixel, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if (image && draw_buffer->Lock(GxLockFlags::Blt)) {
    if (alpha_enabled)
      draw_buffer->AlphaBlt(image, source_area, ScPoint{(draw_area).left, (draw_area).top}, opacity, nullptr);
    else {
      ScRect destination = {draw_area.left, draw_area.top, (i32)((u32)draw_area.left + (u32)source_area.right),
          (i32)((u32)draw_area.top + (u32)source_area.bottom)};
      draw_buffer->Blt(image, source_area, destination, nullptr);
    }
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

bool UiBitmap::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiBitmap::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Fit encoded text and append the window provider's ellipsis when the complete caption exceeds the available width.
// Trim only ASCII spaces and exclude the source trim's undefined all-space underread.
void UiTextRef::FormatForEllipsis(ScString *text, i32 width, FtFont *font) {
  if (!font)
    return;
  u32 length = text->ScString::Strlen();
  if (!length)
    return;
  u32 fitted = font->Fit(text->ScString::Data(), length, (u32)width);
  if (!fitted) {
    text->Erase(0, text->ScString::Strlen());
    return;
  }
  if (fitted == text->ScString::Strlen())
    return;
  WnResources *provider = UiGetWindowResources();
  if (!provider)
    return;
  const ScStringView *tail = provider->GetText(kWnEllipsisTextId);
  ScString ellipsis;
  if (tail) {
    u32 count = tail->Strlen();
    const char *bytes = tail->Data();
    ellipsis.FromChar(bytes, count);
  }
  ScRect bounds = {};
  if (ellipsis.ScString::Strlen())
    font->TextArea(ellipsis.ScString::Data(), ellipsis.ScString::Strlen(), &bounds);
  i32 ellipsis_width = (i32)((u32)bounds.right - (u32)bounds.left);
  if (ellipsis_width < width) {
    fitted =
        font->Fit(text->ScString::Data(), text->ScString::Strlen(), (u32)width + (u32)bounds.left - (u32)bounds.right);
    text->Erase(fitted, text->ScString::Strlen() - fitted);
    u32 retained = text->ScString::Strlen();
    while (retained && text->ScString::Data()[retained - 1] == ' ')
      --retained;
    assert(retained || !text->ScString::Strlen());
    if (retained != text->ScString::Strlen())
      text->Erase(retained, text->ScString::Strlen() - retained);
    text->Append(ellipsis.ScString::Data(), ellipsis.ScString::Strlen());
  } else
    text->Erase(0, text->ScString::Strlen());
}
