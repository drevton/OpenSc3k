#include "ui/replacement_manager.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "geometry/api.h"
#include "occupants/api.h"
#include "resources/resource.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/local_album.h"
#include "ui/override_editor.h"
#include "ui/query_dialogs.h"
#include "ui/text_controls.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/window.h"

// Type options follow the ancestor and reward classification; size options select footprints one through five cells wide.
enum UiReplacementFilterArtwork : u32 {
  kUiReplacementResidentialFilterImageId = 0xda096c77,
  kUiReplacementCommercialFilterImageId = 0x5a096bba,
  kUiReplacementIndustrialFilterImageId = 0x9a096c11,
  kUiReplacementRewardFilterImageId = 0x7a096c9f,
  kUiReplacementOtherFilterImageId = 0x1a096c4f,
  kUiReplacementOneCellFilterImageId = 0xfa096af4,
  kUiReplacementTwoCellFilterImageId = 0x9a096b35,
  kUiReplacementThreeCellFilterImageId = 0x7a096b55,
  kUiReplacementFourCellFilterImageId = 0xda096b70,
  kUiReplacementFiveCellFilterImageId = 0x7a096b86,
};

// The replacement dialog assigns separate identities to its footer, lower content and upper-right panels.
enum UiReplacementPanelId : u32 {
  kUiReplacementFooterPanelId = 0x157e0b34,
  kUiReplacementLowerPanelId = 0x157e0b35,
  kUiReplacementUpperRightPanelId = 0x157e0b36,
};

// The replacement manager and individual replacement dialog use separate slide, replace and revert artwork.
enum UiReplacementArtwork : u32 {
  kUiReplacementManagerSlideFrameImageId = 0xda096ce5,
  kUiReplacementManagerReplaceImageId = 0x9a096c66,
  kUiReplacementManagerRevertImageId = 0xba096c8e,
  kUiReplacementSlideFrameImageId = 0x9a096d34,
  kUiReplacementReplaceImageId = 0x5a096d0d,
  kUiReplacementRevertImageId = 0xba096d24,
};

UiReplacementCommand::UiReplacementCommand() {
}

UiReplacementCommand::~UiReplacementCommand() {
}

// Only the own-identity branch checks a null output, matching the source's delegation to the command base.
bool UiReplacementCommand::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiReplacementCommandIdentity)
    return UiCommand::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiReplacementCommandIdentity *>(this);
  AddRef();
  return true;
}

u32 UiReplacementCommand::AddRef() {
  return UiCommand::AddRef();
}

u32 UiReplacementCommand::Release() {
  return UiCommand::Release();
}

// Consume at most one retained attribute argument, then attach the real replacement callback to a generic modal window.
// Commit a changed replacement only for modal result one, after window shutdown and generic-view retirement.
bool UiReplacementCommand::Execute() {
  UiReplacementProcedure *procedure = new UiReplacementProcedure;
  procedure->AddRef();
  GeAttrib *attributes = nullptr;
  if (InputQueueSize()) {
    ScVariant *argument = nullptr;
    PopFrontInput(&argument);
    argument->RefUnknown()->QueryInterface(IID_GeAttrib, reinterpret_cast<void **>(&attributes));
    argument->Release();
    argument = nullptr;
  }
  i32 modal_result = (i32)kWnModalFailed;
  UiGenericBase *generic = nullptr;
  if (UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericInterface, reinterpret_cast<void **>(&generic))) {
    WnWindow *window = generic->GetWindow();
    procedure->SetOriginalInfo(window, attributes);
    OcOverride *previous = attributes->GetOverRideAttrib();
    if (previous)
      procedure->SetOverRideInfo(window, previous);
    generic->SetNotify(static_cast<UiWindowCallback *>(procedure));
    window->SetArea(8, 8, 520, 296);
    modal_result = window->GetWindowManager()->DoModalWin(window);
    window->Shutdown();
    generic->Release();
    generic = nullptr;
  }
  bool result = modal_result != 0;
  if (modal_result == 1) {
    OcOverride *replacement = procedure->GetReplacement();
    if (replacement != attributes->GetOverRideAttrib()) {
      CtSchemeManager *manager = UiGetApplication()->CitySchemeManager();
      manager->MakeBATReplacement(*attributes->Key(), replacement);
    }
  }
  procedure->Release();
  if (attributes)
    attributes->Release();
  return result;
}

// Fit an acquired button interface when the optional child exists, then retire the successful query.
void UiReplacementControls::AutoSizeButton(WnWindow *window) {
  if (!window)
    return;
  WnButton *button = nullptr;
  if (window->QueryInterface(IID_WnButton, reinterpret_cast<void **>(&button))) {
    button->Fit();
    button->Release();
  }
}

void UiReplacementControls::GetLotSizeTextEffects(ScString *text, u32 size) {
  text->Sprintf("%dx%d", (i32)size, (i32)size);
}

// Retire the scoped image handle before creating its button, then retire the empty caption before testing that result.
// Clear the primary's ordinary-window flag and the button's mode flag before attaching its freshly borrowed primary.
u8 UiReplacementControls::AddButton(WnWindow *window, u32 identifier, u32 group, u32 instance) {
  GxBuffer *buffer;
  const GxColorType format(GxColorType::Color::RGB16);
  {
    UiImageRef image(instance, group, GxVramRequest::SystemMemory, format);
    buffer = image.GetBuffer();
  }
  WnButton *button;
  {
    ScString caption;
    WnResources *resources = UiGetWindowResources();
    const ScRect *area = buffer->GetBufferArea();
    button = resources->CreateButton(identifier, caption, buffer, area, 32);
  }
  if (!button)
    return 0;
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  button->SetButtonFlag(kWnButtonShowText, false);
  window->ChildAdd(button->GetWindow());
  button->Release();
  return 1;
}

// Retire the image handle before creating and configuring the bitmap, preserving its borrowed image through attachment.
// Set the image color key and size the primary from fresh height then width getters before publishing the child.
u8 UiReplacementControls::AddSlideFrame(WnWindow *window, u32 identifier, u32 group, u32 instance) {
  const GxColorType format(GxColorType::Color::RGB16);
  GxBuffer *buffer;
  {
    UiImageRef image(instance, group, GxVramRequest::SystemMemory, format);
    buffer = image.GetBuffer();
  }
  WnBitmapControl *bitmap = nullptr;
  if (!UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)))
    return 0;
  WnWindow *primary = bitmap->GetWindow();
  primary->SetID(identifier);
  primary->SetFlag(kWnFlagReorderable, false);
  primary->SetFlag(kWnFlagTransparentBuffer, true);
  primary->SetFlag(kWnFlagHitTestTransparency, true);
  bitmap->SetBitmapFlag(4, true);
  if (buffer) {
    u32 key = UiGetWindowResources()->GetColor(kWnColorUiTransparency);
    buffer->SetTransparency(key);
    i32 height = buffer->Height(), width = buffer->Width();
    primary->SetSize(width, height);
  }
  bitmap->SetImage(buffer);
  window->ChildAdd(primary);
  bitmap->Release();
  return 1;
}

u8 UiReplacementControls::GetStringResource(RsManager *manager, const RsKey &key, ScStringView **out) {
  return manager && out && manager->GetResource(key, IID_ScStringView, reinterpret_cast<void **>(out));
}

// Set the live window caption from an acquired resource string, then retire that string after the setter returns.
// Missing windows and failed lookups preserve the original zero byte without retiring false-published outputs.
u8 UiReplacementControls::SetCaptionFromResource(WnWindow *window, RsManager *manager, const RsKey &key) {
  if (!window)
    return 0;
  ScStringView *text = nullptr;
  if (!GetStringResource(manager, key, &text))
    return 0;
  window->SetCaption(*text);
  text->Release();
  return 1;
}

// Give a zero-width label its source minimum before fitting, using a fresh primary getter for the width change.
void UiReplacementControls::AutoSizeLabelEffects(WnWindow *window) {
  if (!window)
    return;
  WnLabelControl *label = nullptr;
  if (window->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    if (!label->GetWindow()->GetW())
      label->GetWindow()->SetW(4);
    label->Fit();
    label->Release();
  }
}

// Read the selected list ordinal through its acquired interface and release it before returning the captured value.
// Missing windows and failed queries return minus one without retiring a pointer published by a failed query.
i32 UiReplacementControls::GetSelection(WnWindow *window, u32 identifier) {
  i32 selection = -1;
  if (window) {
    WnListControl *list = nullptr;
    if (window->GetChildAs(identifier, IID_WnListControl, reinterpret_cast<void **>(&list))) {
      selection = list->GetSelection();
      list->Release();
    }
  }
  return selection;
}

UiReplacementData::UiReplacementData()
    : original(nullptr), override_attributes(nullptr), replacement(nullptr), container(nullptr) {
}

// Release the chosen replacement before the original, browsed override and container, clearing each live field afterward.
// Later fields are read after the preceding release callback, preserving the original retirement order.
UiReplacementData::~UiReplacementData() {
  ScRelease(&replacement);
  ScRelease(&original);
  ScRelease(&override_attributes);
  ScRelease(&container);
}

// Hold the original scoped manager while formatting the title and localized labels before control-specific setup.
u8 UiReplacementProcedure::InitControls(WnWindow *window) {
  UiResourceManagerRef resources;
  RsKey key(kRsTextResourceType, kRsSupplementalGameTextGroup, 0);
  {
    UiTextRef resource(0x12f, key.group);
    ScString title = resource.GetExpandedText(), dimensions;
    u32 size = data.original->SizeInCellsX();
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%dx%d", (i32)size, (i32)size);
    dimensions.FromChar(buffer);
    title.Append(dimensions.Data(), dimensions.Strlen());
    window->SetCaption(title);
  }
  key.instance = 304;
  WnWindow *control = window->GetChildWindowFromID(kUiReplacementReplaceLabelId);
  if (control) {
    UiReplacementControls::SetCaptionFromResource(control, resources.manager, key);
    UiReplacementControls::AutoSizeLabelEffects(control);
  }
  key.instance = 305;
  control = window->GetChildWindowFromID(kUiReplacementRevertLabelId);
  if (control) {
    UiReplacementControls::SetCaptionFromResource(control, resources.manager, key);
    UiReplacementControls::AutoSizeLabelEffects(control);
  }
  control = window->GetChildWindowFromID(kUiReplacementInstructionsId);
  if (control) {
    WnLabelControl *label = nullptr;
    if (control->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
      WnResources *provider = UiGetWindowResources();
      FtFont *font = provider->GetFont(kWnFontUiHeading);
      UiTextRef resource(301, key.group);
      if (font) {
        label->SetFont(font);
        label->SetTextColor(0, provider->GetColor(kWnThemeBackgroundColorId));
        label->SetTextColor(1, provider->GetColor(kWnThemePanelFillColorId));
      }
      {
        ScString caption = resource.GetText();
        label->SetCaption(caption);
      }
      label->Fit();
      label->Release();
    }
  }
  key.instance = 302;
  control = window->GetChildWindowFromID(kUiReplacementEditOverridesId);
  UiReplacementControls::SetCaptionFromResource(control, resources.manager, key);
  WnButton *button = nullptr;
  if (window->GetChildAs(kUiReplacementEditOverridesId, IID_WnButton, reinterpret_cast<void **>(&button))) {
    button->Fit();
    button->Release();
  }
  WnListControl *list = nullptr;
  if (window->GetChildAs(kUiReplacementOverrideListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    FtFont *font = nullptr;
    if (GetFont(kWnFontUiMain, &font)) {
      list->SetFont(font);
      font->Release();
      font = nullptr;
    }
    list->Release();
    list = nullptr;
  }
  const u32 previews[2] = {kUiReplacementOriginalPreviewId, kUiReplacementReplacementPreviewId};
  for (u32 identifier : previews) {
    control = window->GetChildWindowFromID(identifier);
    if (control) {
      control->SetSize(100, 100);
      UiPreviewControl *preview = nullptr;
      if (control->QueryInterface(IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
        preview->SetEnabledEffects(1);
        preview->Release();
      }
    }
  }
  UiColumnsControl *columns = nullptr;
  if (window->GetChildAs(
          kUiReplacementOriginalAttributesId, IID_UiColumnsControl, reinterpret_cast<void **>(&columns))) {
    UiAttributeText transfer;
    transfer.InitFieldNames(columns);
    columns->SetColumnWidth(0, 76);
    columns->Fit();
    columns->Release();
    columns = nullptr;
  }
  if (window->GetChildAs(
          kUiReplacementReplacementAttributesId, IID_UiColumnsControl, reinterpret_cast<void **>(&columns))) {
    UiAttributeText transfer;
    transfer.InitFieldNames(columns);
    columns->SetColumnWidth(0, 76);
    columns->Fit();
    columns->Release();
    columns = nullptr;
  }
  key.instance = 304;
  control = window->GetChildWindowFromID(kUiReplacementReplaceId);
  UiReplacementControls::SetCaptionFromResource(control, resources.manager, key);
  key.instance = 305;
  control = window->GetChildWindowFromID(kUiReplacementRevertId);
  UiReplacementControls::SetCaptionFromResource(control, resources.manager, key);
  SetReplaceEnabledEffects(window, 0);
  SetRevertEnabledEffects(window, 0);
  control = window->GetChildWindowFromID(kUiReplacementAcceptId);
  if (control)
    control->SetFlag(kWnFlagEnabled, true);
  return 1;
}

// Place controls around the optional bitmap frame, retaining the source's fallback geometry.
// Preserve getter order and the edit button's inherited vertical coordinate when earlier controls are absent.
u8 UiReplacementProcedure::ResizeAndPositionControls(WnWindow *window) {
  WnWindow *control = window->GetChildWindowFromID(kUiReplacementSlideFrameId);
  i32 edge = 238, right = 363;
  if (control) {
    control->GZWinMoveTo(238, 40);
    edge = control->GetL();
    right = control->GetR();
  }
  right = (i32)((u32)right + 4);
  i32 row_y = edge;
  control = window->GetChildWindowFromID(kUiReplacementAcceptId);
  if (control) {
    i32 left = (i32)(572u - (u32)control->GetW());
    row_y = (i32)(782u - (u32)control->GetH()) / 2;
    control->GZWinMoveTo(left, row_y);
  }
  control = window->GetChildWindowFromID(kUiReplacementOverrideListId);
  if (control) {
    control->SetArea(21, 40, (i32)((u32)edge - 4), 167);
    row_y = (i32)((u32)control->GetB() + 4);
  }
  control = window->GetChildWindowFromID(kUiReplacementEditOverridesId);
  if (control) {
    i32 left = (i32)((u32)edge - 4 - (u32)control->GetW() + 21) / 2;
    control->GZWinMoveTo(left, row_y);
  }
  control = window->GetChildWindowFromID(kUiReplacementReplaceId);
  if (control)
    control->GZWinMoveTo((i32)((u32)edge + 16), 213);
  control = window->GetChildWindowFromID(kUiReplacementRevertId);
  if (control)
    control->GZWinMoveTo((i32)((u32)edge + 71), 161);
  control = window->GetChildWindowFromID(kUiReplacementReplaceLabelId);
  if (control) {
    i32 left = (i32)((u32)edge - (u32)control->GetW() + 27);
    control->GZWinMoveTo(left, 194);
  }
  control = window->GetChildWindowFromID(kUiReplacementRevertLabelId);
  if (control)
    control->GZWinMoveTo((i32)((u32)edge + 104), 187);
  control = window->GetChildWindowFromID(kUiReplacementInstructionsId);
  if (control)
    control->GZWinMoveTo(21, 237);
  control = window->GetChildWindowFromID(kUiReplacementOriginalAttributesId);
  if (control)
    control->SetArea(right, 48, 568, 121);
  control = window->GetChildWindowFromID(kUiReplacementOriginalPreviewId);
  if (control)
    control->GZWinMoveTo((i32)((u32)edge + 13), 52);
  control = window->GetChildWindowFromID(kUiReplacementReplacementAttributesId);
  if (control)
    control->SetArea((i32)((u32)right + 4), 244, 568, 312);
  control = window->GetChildWindowFromID(kUiReplacementReplacementPreviewId);
  if (control)
    control->GZWinMoveTo((i32)((u32)edge + 13), 242);
  return 1;
}

// Repopulate the override list from the live container, retaining the last entry matching the saved resource key.
// Keep the captured iteration count, borrowed override outputs and selection notification ordering of the source.
u8 UiReplacementProcedure::RequeryList(WnWindow *window) {
  i32 selected = -1;
  RsKey saved;
  WnListControl *list = nullptr;
  OcOverride *entry = nullptr;
  if (data.override_attributes)
    saved = *data.override_attributes->Key();
  if (window->GetChildAs(kUiReplacementOverrideListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    ScString name;
    list->RemoveAllStrings();
    CtSchemeManager *schemes = UiGetApplication()->CitySchemeManager();
    u32 size = data.original->SizeInCellsX();
    schemes->GetBATBuildings(data.container, size);
    u32 count = data.container->Size();
    for (u32 i = 0; i < count; ++i) {
      if (data.container->GetObject(i, &entry)) {
        entry->GetName(&name);
        list->InsertString(name, -2);
        RsKey *key = entry->Key();
        if (key->instance == saved.instance && key->group == saved.group && key->type == saved.type)
          selected = (i32)i;
      }
    }
    if (selected >= 0)
      list->SetSelection(selected, true);
    else if (count)
      list->SetSelection(0, true);
    else {
      WnWindow *display = window->GetChildWindowFromID(kUiReplacementOriginalAttributesId);
      UiAttributeText transfer;
      transfer.Transfer(static_cast<OcOverride *>(nullptr), display);
      SetOverRideInfo(window, nullptr);
    }
    list->Release();
  }
  return 1;
}

static const ScString UiFilteredCountToken("%x%"), UiTotalCountToken("%y%");

// Start empty ownership and default filters, then capture preferences before acquiring the shared color provider.
// Construct the count text before copying a ready preference snapshot into the filters and selected resource keys.
UiReplacementManager::UiReplacementManager()
    : word12(0), container(nullptr), type_filter(kUiReplacementManagerFirstTypeChoiceId), size_filter(1),
      schemes(nullptr), word56(0), word60(0), count_text(nullptr) {
  ApPreferences *preferences = UiGetPreferences();
  WnResources *resources = UiGetWindowResources();
  disabled_color = resources->GetColor(kWnColorGray);
  enabled_color = resources->GetColor(kWnThemeTextColorId);
  count_text = new UiTextRef(kRsSupplementalGameBuildingSelectionCountFormatText, kRsSupplementalGameTextGroup);
  if (preferences) {
    const ApReplacementPrefs saved = *preferences->BldgReplacementPrefs();
    if (saved.ready) {
      type_filter = saved.type_filter;
      size_filter = saved.size_filter;
      selected_building = saved.selected_building;
      selected_slide = saved.selected_slide;
    }
  }
}

// Persist filters and selected keys before releasing every row, then the live container, scheme manager and count text.
// Filtered index storage retires before row storage and the inherited procedure; no extra module hold is removed here.
UiReplacementManager::~UiReplacementManager() {
  ApPreferences *preferences = UiGetPreferences();
  if (preferences) {
    ApReplacementPrefs *saved = preferences->BldgReplacementPrefs();
    saved->type_filter = type_filter;
    saved->size_filter = size_filter;
    saved->selected_building = selected_building;
    saved->selected_slide = selected_slide;
    saved->ready = 1;
  }
  ClearInfosEffects(&infos);
  ScRelease(&container);
  ScRelease(&schemes);
  ScDelete(&count_text);
}

// Other identities delegate to the procedure callback, preserving its separate receiver and rejection behavior.
bool UiReplacementManager::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiReplacementManagerControl)
    return UiWindowProcedure::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiReplacementManagerControl *>(this);
  AddRef();
  return true;
}

u32 UiReplacementManager::AddRef() {
  return UiWindowProcedure::AddRef();
}

u32 UiReplacementManager::Release() {
  return UiWindowProcedure::Release();
}

void UiReplacementManager::ClearInfosEffects(ScArray<UiReplacementInfo> *infos) {
  UiReplacementInfo *row = infos->data;
  u32 count = infos->size;
  for (u32 i = 0; i < count; ++i, ++row) {
    row->original->Release();
    if (row->replacement)
      row->replacement->Release();
  }
  infos->Clear();
}

// Consume Escape or Windows Alt-F4 and post the original modal cancellation envelope through the current manager.
u8 UiReplacementManager::OnKeyDown(WnWindow *window, u32 key, u32 modifiers) {
  if (key != kWnKeyEscape && (key != kWnKeyF4 || !(modifiers & kWnModifierAlt)))
    return 0;
  UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  return 1;
}

// Classify the captured type through three ordered ancestor tests, returning immediately on the first match.
// Only the fallback rereads the type before asking the live scheme service whether it is a reward building.
i32 UiReplacementManager::DetermineOccAttribType(const GeAttrib *attributes) {
  CtAgentTree *tree = UiGetAgentTypeTree();
  u32 type = attributes->TypeId();
  if (tree->IsAncestor(3000, type))
    return 0;
  if (tree->IsAncestor(5000, type))
    return 1;
  if (tree->IsAncestor(8000, type))
    return 2;
  type = attributes->TypeId();
  return schemes->IsRewardBuilding(type) ? 3 : 4;
}

// Capture the label color before changing the replace button, then recolor the separately queried label.
void UiReplacementManager::SetReplaceEnabledEffects(WnWindow *window, bool enabled) {
  u32 color = enabled ? enabled_color : disabled_color;
  WnWindow *button = window->GetChildWindowFromID(kUiReplacementManagerReplaceId);
  if (button)
    button->SetFlag(kWnFlagEnabled, enabled);
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(kUiReplacementManagerReplaceLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->SetTextColor(color);
    label->Release();
  }
}

// Capture the label color before changing the revert button, then retire the queried label after its color update.
void UiReplacementManager::SetRevertEnabledEffects(WnWindow *window, bool enabled) {
  u32 color = enabled ? enabled_color : disabled_color;
  WnWindow *button = window->GetChildWindowFromID(kUiReplacementManagerRevertId);
  if (button)
    button->SetFlag(kWnFlagEnabled, enabled);
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(kUiReplacementManagerRevertLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->SetTextColor(color);
    label->Release();
  }
}

bool UiReplacementManager::IsToken(const ScString &text, i32 position, const ScString &token) {
  ScString part = text.Mid((u32)position, token.Strlen());
  u32 length = (u32)(part.end - part.begin), token_length = (u32)(token.end - token.begin);
  return length == token_length && !memcmp(part.begin, token.begin, length);
}

bool UiReplacementManager::ReplaceToken(const ScString &text, i32 position, UiTokenData *data) {
  if (IsToken(text, position, UiFilteredCountToken)) {
    UiTokenData replacement(2, UiFilteredCountToken, (i32)filtered.size);
    *data = replacement;
    return true;
  }
  if (IsToken(text, position, UiTotalCountToken)) {
    UiTokenData replacement(2, UiTotalCountToken, (i32)infos.size);
    *data = replacement;
    return true;
  }
  return false;
}

bool UiReplacementManager::ReplaceTokenStatic(
    const ScString &text, i32 position, UiTokenData *data, UiReplacementManager *manager) {
  return manager ? manager->ReplaceToken(text, position, data) : false;
}

void UiReplacementManager::UpdateFilteredBuildingCountTextEffects(WnWindow *window) {
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(
          kUiReplacementManagerCurrentSizeLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    ScString caption;
    count_text->CopyWithTokens(&caption, ReplaceTokenStatic, this);
    label->SetCaption(caption);
    label->Fit();
    label->Release();
  }
}

// Rebuild filtered indices against a captured type selection while reading the size selection after each getter.
void UiReplacementManager::FilterOccAttribsEffects(WnWindow *window) {
  filtered.Clear();
  u32 kind = type_filter - kUiReplacementManagerFirstTypeChoiceId;
  UiReplacementInfo *row = infos.data;
  u32 count = infos.size;
  for (u32 index = 0; index < count; ++index, ++row) {
    u32 size = row->original->SizeInCellsX();
    if (size == size_filter && row->kind == kind)
      *filtered.Append() = index;
  }
  UpdateFilteredBuildingCountTextEffects(window);
}

// Clear retained rows before acquiring the borrowed cache, then retain each replaceable original and its live override.
void UiReplacementManager::InitOccAttribListEffects(WnWindow *window) {
  ClearInfosEffects(&infos);
  filtered.Clear();
  OcAttribCache *cache = UiGetAttribCache();
  if (cache) {
    OcCacheIterator *iterator = cache->CreateNodeIterator();
    if (iterator) {
      GeAttrib *original;
      while ((original = iterator->GetNext()) != nullptr) {
        if (schemes->IsReplaceableBuilding(original)) {
          original->AddRef();
          OcOverride *replacement = original->GetOverRideAttrib();
          if (replacement)
            replacement->AddRef();
          u32 kind = (u32)DetermineOccAttribType(original);
          *infos.Append() = {original, replacement, kind};
        }
      }
      iterator->Release();
      FilterOccAttribsEffects(window);
    }
  }
}

// Compare the selected borrowed replacement's byte lot size with the captured filtered original's full dimension.
void UiReplacementManager::DetermineReplaceabilityEffects(WnWindow *window) {
  i32 selection = UiReplacementControls::GetSelection(window, kUiReplacementManagerOverrideListId);
  OcOverride *replacement = nullptr;
  u32 lot_size = 0, original_size = 0;
  bool enabled = container->GetObject((u32)selection, &replacement);
  if (enabled)
    lot_size = replacement->GetLotSize();
  UiSlidesControl *slides = nullptr;
  if (window->GetChildAs(kUiReplacementManagerSlidesId, IID_UiSlidesControl, reinterpret_cast<void **>(&slides))) {
    selection = slides->GetSelection();
    if (selection < 0 || (u32)selection >= filtered.size)
      enabled = false;
    else {
      u32 index = filtered.data[(u32)selection];
      original_size = infos.data[index].original->SizeInCellsX();
      SetRevertEnabledEffects(window, infos.data[index].replacement != nullptr);
    }
    slides->Release();
    if (enabled)
      enabled = original_size == lot_size;
  } else
    enabled = false;
  SetReplaceEnabledEffects(window, enabled);
}

// Rebuild slides from the captured filtered range, choosing the last ordinal whose live original key matches selection.
// Release the slide view before reevaluating buttons; a null window or failed query preserves the observed zero byte.
u8 UiReplacementManager::RequerySlides(WnWindow *window) {
  if (!window)
    return 0;
  UiSlidesControl *slides = nullptr;
  if (!window->GetChildAs(kUiReplacementManagerSlidesId, IID_UiSlidesControl, reinterpret_cast<void **>(&slides)))
    return 0;
  slides->RemoveAllSlides();
  u32 *index = filtered.data, count = filtered.size;
  i32 selected = -1;
  for (u32 ordinal = 0; ordinal < count; ++ordinal, ++index) {
    UiReplacementInfo *row = infos.data + *index;
    slides->AddOriginal(row->original, row->replacement);
    const RsKey *key = row->original->Key();
    if (key->instance == selected_slide.instance && key->group == selected_slide.group &&
        key->type == selected_slide.type)
      selected = (i32)ordinal;
  }
  if (selected >= 0)
    slides->SetSelection(selected);
  slides->Release();
  DetermineReplaceabilityEffects(window);
  return 1;
}

// Refill the live BAT container before rebuilding list names and choosing the last matching stored resource key.
// Empty containers clear details and the override preview; release the list before refreshing enabled states.
u8 UiReplacementManager::RequeryListbox(WnWindow *window) {
  schemes->GetBATBuildings(container, size_filter);
  if (!window)
    return 0;
  WnListControl *list = nullptr;
  if (!window->GetChildAs(kUiReplacementManagerOverrideListId, IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  OcOverride *current = nullptr;
  ScString name;
  i32 selected = -1;
  list->RemoveAllStrings();
  u32 count = container->Size();
  for (u32 index = 0; index < count; ++index) {
    if (container->GetObject(index, &current)) {
      current->GetName(&name);
      list->InsertString(name, -2);
      const RsKey *key = current->Key();
      if (key->instance == selected_building.instance && key->group == selected_building.group &&
          key->type == selected_building.type)
        selected = (i32)index;
    }
  }
  if (selected >= 0)
    list->SetSelection(selected, true);
  else if (count)
    list->SetSelection(0, true);
  else {
    UiAttributeText transfer;
    transfer.Transfer(
        static_cast<OcOverride *>(nullptr), window->GetChildWindowFromID(kUiReplacementManagerAttributesId));
    UiPreviewControl *preview = nullptr;
    if (window->GetChildAs(kUiReplacementManagerPreviewId, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
      preview->SetOverride(nullptr);
      preview->Release();
    }
  }
  list->Release();
  DetermineReplaceabilityEffects(window);
  return 1;
}

// Compare the raw type ID before clamping and refresh only slides after rebuilding the filtered index list.
void UiReplacementManager::TypeFilterSelectionChangedEffects(WnWindow *window) {
  UiFixedOptionsControl *options = nullptr;
  if (window->GetChildAs(
          kUiReplacementManagerTypeOptionsId, IID_UiFixedOptionsControl, reinterpret_cast<void **>(&options))) {
    u32 selected;
    options->GetSelection(&selected);
    if (selected != type_filter) {
      type_filter = selected;
      if (selected < kUiReplacementManagerFirstTypeChoiceId || selected > kUiReplacementManagerLastTypeChoiceId)
        type_filter = kUiReplacementManagerFirstTypeChoiceId;
      FilterOccAttribsEffects(window);
      RequerySlides(window);
    }
    options->Release();
  }
}

// Normalize the selected size ID before comparing its one-based size, then refresh indices, slides, BAT names and
// label.
void UiReplacementManager::SizeFilterSelectionChangedEffects(WnWindow *window) {
  UiFixedOptionsControl *options = nullptr;
  if (window->GetChildAs(
          kUiReplacementManagerSizeOptionsId, IID_UiFixedOptionsControl, reinterpret_cast<void **>(&options))) {
    u32 selected;
    options->GetSelection(&selected);
    if (selected < kUiReplacementManagerSizeOneId || selected > kUiReplacementManagerSizeFiveId)
      selected = kUiReplacementManagerSizeOneId;
    u32 size = selected - kUiReplacementManagerSizeOptionsId;
    if (size != size_filter) {
      size_filter = size;
      FilterOccAttribsEffects(window);
      RequerySlides(window);
      RequeryListbox(window);
      DisplayCurrentSizeEffects(window, size_filter);
    }
    options->Release();
  }
}

// Copy the required label prefix before appending the captured signed32 square dimensions and fitting its label.
void UiReplacementManager::DisplayCurrentSizeEffects(WnWindow *window, u32 size) {
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(
          kUiReplacementManagerSelectionLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    ScString caption, dimensions;
    UiTextRef prefix(kRsSupplementalGameSizeText, kRsSupplementalGameTextGroup);
    const ScStringView *text = prefix.GetResource();
    assert(text);
    caption.Copy(*text);
    dimensions.Sprintf("%dx%d", (i32)size, (i32)size);
    caption.Append(dimensions.Data(), dimensions.Strlen());
    label->SetCaption(caption);
    label->Fit();
    label->Release();
  }
}

// Refresh the selected BAT preview and details before storing its live key, then recalculate both action states.
void UiReplacementManager::ListBoxSelectionChangedEffects(WnWindow *window) {
  OcOverride *attributes = nullptr;
  i32 selected = UiReplacementControls::GetSelection(window, kUiReplacementManagerOverrideListId);
  if (container->GetObject((u32)selected, &attributes)) {
    UiPreviewControl *preview = nullptr;
    if (window->GetChildAs(kUiReplacementManagerPreviewId, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
      preview->SetOverride(attributes);
      preview->Release();
    }
    UiAttributeText text;
    text.Transfer(attributes, window->GetChildWindowFromID(kUiReplacementManagerAttributesId));
    selected_building = *attributes->Key();
  }
  DetermineReplaceabilityEffects(window);
}

void UiReplacementManager::SliderSelectionChangedEffects(WnWindow *window) {
  UiSlidesControl *slides = nullptr;
  if (window->GetChildAs(kUiReplacementManagerSlidesId, IID_UiSlidesControl, reinterpret_cast<void **>(&slides))) {
    i32 selected = slides->GetSelection();
    if (selected >= 0)
      selected_slide = *infos.data[filtered.data[selected]].original->Key();
    slides->Release();
  }
  DetermineReplaceabilityEffects(window);
}

// Position the manager's original fixed dialog controls, using frame edges and equalized lower action widths.
// Preserve the successive live edge reads and finish by sizing the slide window and its picture view separately.
u8 UiReplacementManager::ResizeAndPositionControls(WnWindow *window) {
  WnWindow *child = window->GetChildWindowFromID(kUiReplacementManagerSlideFrameId);
  i32 right = 416, bottom = 364;
  if (child) {
    child->GZWinMoveTo(291, 40);
    right = child->GetR();
    bottom = child->GetB();
  }
  child = window->GetChildWindowFromID(kUiReplacementManagerAcceptId);
  if (child) {
    i32 x = 631 - child->GetW(), y = (913 - child->GetH()) / 2;
    child->GZWinMoveTo(x, y);
  }
  child = window->GetChildWindowFromID(kUiReplacementManagerOverrideListId);
  if (child)
    child->SetArea(16, 40, 276, 171);
  child = window->GetChildWindowFromID(kUiReplacementManagerEditOverridesId);
  if (child)
    child->GZWinMoveTo((292 - child->GetW()) / 2, 175);
  child = window->GetChildWindowFromID(kUiReplacementManagerPreviewId);
  if (child)
    child->GZWinMoveTo(304, 52);
  child = window->GetChildWindowFromID(kUiReplacementManagerAttributesId);
  if (child)
    child->SetArea(right + 4, 48, 627, 128);
  child = window->GetChildWindowFromID(kUiReplacementManagerReplaceId);
  if (child)
    child->GZWinMoveTo(307, 213);
  child = window->GetChildWindowFromID(kUiReplacementManagerRevertId);
  if (child)
    child->GZWinMoveTo(362, 161);
  child = window->GetChildWindowFromID(kUiReplacementManagerReplaceLabelId);
  if (child)
    child->GZWinMoveTo(318 - child->GetW(), 194);
  child = window->GetChildWindowFromID(kUiReplacementManagerRevertLabelId);
  if (child)
    child->GZWinMoveTo(395, 187);
  child = window->GetChildWindowFromID(kUiReplacementManagerSelectionLabelId);
  if (child)
    child->GZWinMoveTo((right - child->GetW() + 291) / 2, 344);
  WnWindow *first = window->GetChildWindowFromID(kUiReplacementManagerImportSetId);
  WnWindow *second = window->GetChildWindowFromID(kUiReplacementManagerClearSetId);
  i32 first_width = 0, first_height = 0, second_width = 0, second_height = 0;
  if (first) {
    first_width = first->GetW();
    first_height = first->GetH();
  }
  if (second) {
    second_width = second->GetW();
    second_height = second->GetH();
  }
  if (second_width >= first_width) {
    if (second_width > first_width && first)
      first->SetW(second_width);
  } else if (second)
    second->SetW(first_width);
  right = 627;
  i32 gap = (423 - second_height - first_height - bottom) / 2, y = 421 - gap;
  if (second) {
    y -= second_height;
    second->GZWinMoveTo(627 - second->GetW(), y);
  }
  if (first) {
    y -= first_height + gap;
    first->GZWinMoveTo(627 - first->GetW(), y);
  }
  child = window->GetChildWindowFromID(kUiReplacementManagerReplacementSetLabelId);
  if (child) {
    child->GZWinMoveTo(20, bottom - 4);
    y = child->GetB();
    right = child->GetR();
  }
  child = window->GetChildWindowFromID(kUiReplacementManagerCurrentSizeLabelId);
  if (child)
    child->GZWinMoveTo(right, bottom - 4);
  child = window->GetChildWindowFromID(kUiReplacementManagerTypeOptionsId);
  if (child) {
    child->GZWinMoveTo(20, y);
    right = child->GetR() + 4;
  }
  child = window->GetChildWindowFromID(kUiReplacementManagerSizeOptionsId);
  if (child)
    child->GZWinMoveTo(right, y);
  child = window->GetChildWindowFromID(kUiReplacementManagerSlidesId);
  if (child) {
    child->SetSize(615, 0);
    child->GZWinMoveTo(16, 228);
    UiSlidesControl *slides = nullptr;
    if (child->QueryInterface(IID_UiSlidesControl, reinterpret_cast<void **>(&slides))) {
      slides->SetScrollIndent(272);
      slides->Release();
    }
  }
  return 1;
}

u8 UiReplacementManager::OnShutdown(WnWindow *) {
  return 1;
}

// Rebuild BAT names before capturing the original rows, then replace retained overrides by looking up their copied keys.
// Retire each old override before using the live container, and rebuild slides after the entire captured row traversal.
u8 UiReplacementManager::BATBuildingListEdited(WnWindow *window) {
  RsKey key;
  OcOverride *attributes = nullptr;
  RequeryListbox(window);
  UiReplacementInfo *row = infos.data;
  u32 count = infos.size;
  for (u32 i = 0; i < count; ++i, ++row) {
    if (!row->replacement)
      continue;
    key = *row->replacement->Key();
    row->replacement->Release();
    row->replacement = nullptr;
    if (container && container->GetObject(key, &attributes)) {
      row->replacement = attributes;
      attributes->AddRef();
    }
  }
  return RequerySlides(window);
}

void UiReplacementManager::EndDialogEffects(WnWindow *window, u32 result) {
  UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), result);
}

// Confirm clearing through the two localized resources, play the dismissal sound, then clear previews before row holds.
// Only confirmation retires live row replacements and asks the scheme service to remove its complete replacement set.
void UiReplacementManager::ClearReplacementSet(WnWindow *window) {
  WnWindowManager *manager = UiGetWindowManager();
  ApBoxX *box = UiGetBoxX();
  ScString title, message;
  {
    UiTextRef resource(kRsSupplementalGameClearBuildingSetText, kRsSupplementalGameTextGroup);
    ScString text = resource.GetText();
    title.Copy(text);
  }
  {
    UiTextRef resource(kRsSupplementalGameConfirmClearReplacementsText, kRsSupplementalGameTextGroup);
    ScString text = resource.GetText();
    message.Copy(text);
  }
  if (manager) {
    u32 answer = manager->GZMsgBox(message, title, 0x10004, true, 0);
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    if (answer == kWnYesButtonId) {
      UiSlidesControl *slides = nullptr;
      if (window->GetChildAs(kUiReplacementManagerSlidesId, IID_UiSlidesControl, reinterpret_cast<void **>(&slides))) {
        slides->ClearAllOverRides();
        slides->Release();
      }
      UiReplacementInfo *row = infos.data;
      u32 count = infos.size;
      for (u32 i = 0; i < count; ++i, ++row)
        if (row->replacement) {
          row->replacement->Release();
          row->replacement = nullptr;
        }
      schemes->ClearReplacementSet();
    }
  }
}

// Execute the replacement-set picker with its original mode argument, accepting two queued outputs only on success.
// Retire the command before importing the captured filename and refreshing live row overrides from their originals.
u8 UiReplacementManager::ImportReplacementSet(WnWindow *window) {
  u8 result = 0;
  ScString filename;
  UiCommandInterface *command = nullptr;
  if (UiGetCommandFactory()->GetCommand(
          kCtFileDialogCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
    ScVariant *argument = new ScValue(kCtFileImportReplacementSet, kScValueUint32);
    argument->AddRef();
    command->PushBackInput(argument);
    argument->Release();
    bool accepted = command->Execute();
    if (accepted) {
      if (command->OutputQueueSize() < 2)
        accepted = false;
      if (accepted) {
        command->PopFrontOutput(&argument);
        if (!*argument->AsBool())
          accepted = false;
        argument->Release();
        if (accepted) {
          command->PopFrontOutput(&argument);
          u32 count = argument->GetCount();
          char *bytes = argument->RefRZChar();
          {
            ScString text(bytes, count);
            filename = text;
          }
          argument->Release();
        }
      }
    }
    command->Release();
    if (accepted) {
      schemes->ImportReplacementSet(&filename);
      UiReplacementInfo *row = infos.data;
      u32 count = infos.size;
      for (u32 i = 0; i < count; ++i, ++row) {
        OcOverride *replacement = row->original->GetOverRideAttrib();
        if (replacement) {
          if (row->replacement)
            row->replacement->Release();
          row->replacement = replacement;
          replacement->AddRef();
        }
      }
      RequerySlides(window);
      result = 1;
    }
  }
  return result;
}

// Capture BoxX before dispatch, including button commands that independently capture it again in their handler.
// Selection notifications keep their original unconditional handled replies even when the child ID is unrelated.
u8 UiReplacementManager::OnCommand(WnWindow *window, u32 kind, uptr first, uptr) {
  ApBoxX *box = UiGetBoxX();
  switch (kind) {
  case 1:
    return OnButton(window, (u32)first);
  case 3:
    if ((u32)first == kUiReplacementManagerTypeOptionsId) {
      if (box)
        box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      TypeFilterSelectionChangedEffects(window);
      return 1;
    }
    if ((u32)first == kUiReplacementManagerSizeOptionsId) {
      if (box)
        box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      SizeFilterSelectionChangedEffects(window);
      return 1;
    }
    break;
  case 6:
    if ((u32)first == kUiReplacementManagerOverrideListId)
      ListBoxSelectionChangedEffects(window);
    return 1;
  case kUiSlideSelectionChangedCommand:
    if ((u32)first == kUiReplacementManagerSlidesId)
      SliderSelectionChangedEffects(window);
    return 1;
  }
  return 0;
}

// Route the manager's modal, editor, replacement and set actions after one captured BoxX lookup.
// Replacements update the live row storage after callbacks while preserving the captured filtered ordinal.
u8 UiReplacementManager::OnButton(WnWindow *window, u32 button_id) {
  ApBoxX *box = UiGetBoxX();
  switch (button_id) {
  case kUiFrameCloseButtonId:
  case kUiReplacementManagerAcceptId:
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    EndDialogEffects(window, button_id == kUiReplacementManagerAcceptId ? 1 : 0);
    return 1;
  case kUiReplacementManagerEditOverridesId: {
    UiEditOverridesCommand command;
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    window->SetFlag(kWnFlagPrivateBuffer, true);
    window->HideWindow();
    bool edited = command.Execute();
    window->ShowWindow();
    window->SetFlag(kWnFlagPrivateBuffer, false);
    if (edited)
      BATBuildingListEdited(window);
    return 1;
  }
  case kUiReplacementManagerReplaceId: {
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    i32 selected = UiReplacementControls::GetSelection(window, kUiReplacementManagerOverrideListId);
    if (selected < 0)
      return 1;
    OcOverride *replacement = nullptr;
    if (!container->GetObject((u32)selected, &replacement))
      return 1;
    UiSlidesControl *slides = nullptr;
    if (!window->GetChildAs(kUiReplacementManagerSlidesId, IID_UiSlidesControl, reinterpret_cast<void **>(&slides)))
      return 1;
    selected = slides->GetSelection();
    slides->SetOverRide(replacement);
    SetRevertEnabledEffects(window, true);
    if (selected >= 0) {
      u32 index = filtered.data[selected];
      if (infos.data[index].replacement)
        infos.data[index].replacement->Release();
      infos.data[index].replacement = replacement;
      infos.data[index].replacement->AddRef();
    }
    slides->Release();
    return 1;
  }
  case kUiReplacementManagerRevertId: {
    UiSlidesControl *slides = nullptr;
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    if (!window->GetChildAs(kUiReplacementManagerSlidesId, IID_UiSlidesControl, reinterpret_cast<void **>(&slides)))
      return 1;
    i32 selected = slides->GetSelection();
    slides->SetOverRide(nullptr);
    SetRevertEnabledEffects(window, false);
    if (selected >= 0 && (u32)selected < filtered.size) {
      u32 index = filtered.data[selected];
      if (infos.data[index].replacement) {
        infos.data[index].replacement->Release();
        infos.data[index].replacement = nullptr;
      }
    }
    slides->Release();
    return 1;
  }
  case kUiReplacementManagerImportSetId:
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    ImportReplacementSet(window);
    return 1;
  case kUiReplacementManagerClearSetId:
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    ClearReplacementSet(window);
    return 1;
  }
  return 0;
}

// Localize the fixed labels and populate both option groups before configuring preview and detail-display geometry.
// Preserve resource order, live filter reads and final button fitting while the scoped manager remains acquired.
u8 UiReplacementManager::InitControls(WnWindow *window) {
  UiResourceManagerRef resources;
  RsKey key(kRsTextResourceType, kRsSupplementalGameTextGroup, 309);
  UiReplacementControls::SetCaptionFromResource(window, resources.manager, key);
  key.instance = 304;
  WnWindow *child = window->GetChildWindowFromID(kUiReplacementManagerReplaceLabelId);
  if (child) {
    UiReplacementControls::SetCaptionFromResource(child, resources.manager, key);
    UiReplacementControls::AutoSizeLabelEffects(child);
  }
  key.instance = 305;
  child = window->GetChildWindowFromID(kUiReplacementManagerRevertLabelId);
  if (child) {
    UiReplacementControls::SetCaptionFromResource(child, resources.manager, key);
    UiReplacementControls::AutoSizeLabelEffects(child);
  }
  DisplayCurrentSizeEffects(window, size_filter);
  key.instance = 311;
  UiReplacementControls::SetCaptionFromResource(
      window->GetChildWindowFromID(kUiReplacementManagerReplacementSetLabelId), resources.manager, key);
  key.instance = 302;
  UiReplacementControls::SetCaptionFromResource(
      window->GetChildWindowFromID(kUiReplacementManagerEditOverridesId), resources.manager, key);
  WnButton *button = nullptr;
  if (window->GetChildAs(kUiReplacementManagerEditOverridesId, IID_WnButton, reinterpret_cast<void **>(&button))) {
    button->Fit();
    button->Release();
  }
  key.instance = 308;
  UiReplacementControls::SetCaptionFromResource(
      window->GetChildWindowFromID(kUiReplacementManagerImportSetId), resources.manager, key);
  key.instance = 310;
  UiReplacementControls::SetCaptionFromResource(
      window->GetChildWindowFromID(kUiReplacementManagerClearSetId), resources.manager, key);
  UiFixedOptionsControl *options = nullptr;
  key.type = kRsImageResourceType;
  key.group = kRsUiImageGroup;
  if (window->GetChildAs(
          kUiReplacementManagerTypeOptionsId, IID_UiFixedOptionsControl, reinterpret_cast<void **>(&options))) {
    static const u32 kUiReplacementTypeFilterImages[] = {kUiReplacementResidentialFilterImageId, kUiReplacementCommercialFilterImageId, kUiReplacementIndustrialFilterImageId, kUiReplacementRewardFilterImageId, kUiReplacementOtherFilterImageId};
    for (u32 i = 0; i < 5; ++i) {
      key.instance = kUiReplacementTypeFilterImages[i];
      UiFixedOptions::AddOptionResource(options, kUiReplacementManagerFirstTypeChoiceId + i, resources.manager, key);
    }
    options->SetSelection(type_filter);
    options->LayoutLeftToRight();
    options->Release();
    options = nullptr;
  }
  if (window->GetChildAs(
          kUiReplacementManagerSizeOptionsId, IID_UiFixedOptionsControl, reinterpret_cast<void **>(&options))) {
    static const u32 kUiReplacementSizeFilterImages[] = {kUiReplacementOneCellFilterImageId, kUiReplacementTwoCellFilterImageId, kUiReplacementThreeCellFilterImageId, kUiReplacementFourCellFilterImageId, kUiReplacementFiveCellFilterImageId};
    for (u32 i = 0; i < 5; ++i) {
      key.instance = kUiReplacementSizeFilterImages[i];
      UiFixedOptions::AddOptionResource(options, kUiReplacementManagerSizeOneId + i, resources.manager, key);
    }
    options->SetSelection(size_filter + kUiReplacementManagerSizeOptionsId);
    options->LayoutLeftToRight();
    options->Release();
  }
  child = window->GetChildWindowFromID(kUiReplacementManagerPreviewId);
  if (child) {
    child->SetSize(100, 100);
    UiPreviewControl *preview = nullptr;
    if (child->QueryInterface(IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
      preview->SetEnabledEffects(1);
      preview->Release();
    }
  }
  UiColumnsControl *columns = nullptr;
  if (window->GetChildAs(
          kUiReplacementManagerAttributesId, IID_UiColumnsControl, reinterpret_cast<void **>(&columns))) {
    UiAttributeText text;
    text.InitFieldNames(columns);
    columns->SetColumnWidth(0, 76);
    columns->Fit();
    columns->Release();
  }
  UiReplacementControls::AutoSizeLabelEffects(window->GetChildWindowFromID(kUiReplacementManagerReplacementSetLabelId));
  UiReplacementControls::AutoSizeButton(window->GetChildWindowFromID(kUiReplacementManagerClearSetId));
  UiReplacementControls::AutoSizeButton(window->GetChildWindowFromID(kUiReplacementManagerImportSetId));
  return 1;
}

// Acquire the real container and scheme owner before building, formatting and populating the fixed manager controls.
// Center against independently fetched parent views, then apply the original packed work-area reply and style state.
u8 UiReplacementManager::OnInit(WnWindow *window) {
  UiGenericBase *generic = nullptr;
  if (!window->QueryInterface(IID_UiGenericInterface, reinterpret_cast<void **>(&generic)))
    return 0;
  u8 created = UiGetCom()->GetClassObject(
      kOcOverrideContainerClassId, IID_OcOverrideContainer, reinterpret_cast<void **>(&container));
  if (!created)
    return 0;
  schemes = UiGetApplication()->CitySchemeManager();
  if (!schemes)
    return 0;
  schemes->AddRef();
  window->SetSize(635, 478);
  generic->SetStyle(kUiGenericResizable, 0);
  UiGenericInterface *layout = nullptr;
  if (generic->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&layout))) {
    ScRect rectangle = {16, 228, 631, 427};
    layout->AddPanel(kUiReplacementManagerSlidesPanelId, 0, 15, &rectangle);
    rectangle = {317, 44, 631, 166};
    layout->AddPanel(kUiReplacementManagerPreviewPanelId, 0, 15, &rectangle);
    generic->SetStyle(kUiGenericScroller, 1);
    layout->AddDivider(kUiReplacementManagerBottomDividerId, (i32)((u32)window->GetH() - 39));
    WnWindow *divider = window->GetChildWindowFromID(kUiReplacementManagerBottomDividerId);
    if (divider)
      divider->SetH(35);
    layout->Release();
  }
  AddControl(window, kUiResourceStandardButton, kUiReplacementManagerAcceptId);
  UiSlides *slides = new UiSlides;
  static_cast<WnWindow *>(slides)->SetID(kUiReplacementManagerSlidesId);
  static_cast<WnWindow *>(slides)->SetFlag(kWnFlagReorderable, false);
  window->ChildAdd(static_cast<WnWindow *>(slides));
  AddControl(window, kUiResourceList, kUiReplacementManagerOverrideListId);
  AddControl(window, kUiResourceCaptionButton, kUiReplacementManagerEditOverridesId);
  UiReplacementControls::AddSlideFrame(
      window, kUiReplacementManagerSlideFrameId, kRsUiImageGroup, kUiReplacementManagerSlideFrameImageId);
  UiReplacementControls::AddButton(
      window, kUiReplacementManagerReplaceId, kRsUiImageGroup, kUiReplacementManagerReplaceImageId);
  UiReplacementControls::AddButton(
      window, kUiReplacementManagerRevertId, kRsUiImageGroup, kUiReplacementManagerRevertImageId);
  AddControl(window, kUiResourceLabel, kUiReplacementManagerReplaceLabelId);
  AddControl(window, kUiResourceLabel, kUiReplacementManagerRevertLabelId);
  AddControl(window, kUiResourceLabel, kUiReplacementManagerSelectionLabelId);
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(
          kUiReplacementManagerSelectionLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
    if (font)
      label->SetFont(font);
    label->SetTextColor(disabled_color);
    label->Release();
  }
  AddControl(window, kUiResourceColumns, kUiReplacementManagerAttributesId);
  AddControl(window, kUiResourceBuildingPreview, kUiReplacementManagerPreviewId);
  UiFixedOptionsControl *options = new UiFixedOptions;
  options->AsIGZWin()->SetID(kUiReplacementManagerTypeOptionsId);
  window->ChildAdd(options->AsIGZWin());
  options = new UiFixedOptions;
  options->AsIGZWin()->SetID(kUiReplacementManagerSizeOptionsId);
  window->ChildAdd(options->AsIGZWin());
  AddControl(window, kUiResourceLabel, kUiReplacementManagerReplacementSetLabelId);
  AddControl(window, kUiResourceLabel, kUiReplacementManagerCurrentSizeLabelId);
  AddControl(window, kUiResourceCaptionButton, kUiReplacementManagerImportSetId);
  AddControl(window, kUiResourceCaptionButton, kUiReplacementManagerClearSetId);
  InitControls(window);
  ResizeAndPositionControls(window);
  SetRevertEnabledEffects(window, false);
  SetReplaceEnabledEffects(window, false);
  InitOccAttribListEffects(window);
  RequeryListbox(window);
  RequerySlides(window);
  WnWindow *parent = window->GetParentWin();
  i32 width = window->GetW();
  i32 x = (i32)((u32)parent->GetW() - (u32)width) / 2;
  parent = window->GetParentWin();
  i32 parent_height = parent->GetH(), height = window->GetH();
  i32 y = (i32)((u32)parent_height - (u32)height) / 2;
  bool style = false;
  ScMessage message(kWnMsg_QueryUiVisibility);
  UiGetMessageServer()->MessageSend(message);
  if ((u32)message.data1 & 2) {
    style = true;
    ScMessage work_area(kWnMsg_QueryUiClientArea);
    UiGetMessageServer()->MessageSend(work_area);
    i32 available = (i32)(((u32)work_area.data2 >> 16) - ((u32)work_area.data1 >> 16));
    if (available > window->GetW())
      x = (i32)((u32)available - (u32)window->GetW()) / 2;
    available = (i32)(((u32)work_area.data2 & 0xffff) - ((u32)work_area.data1 & 0xffff));
    if (available > window->GetH())
      y = (i32)((u32)available - (u32)window->GetH()) / 2;
  }
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  window->GZWinMoveTo(x, y);
  generic->SetStyle(kUiGenericDraggable, style);
  generic->Release();
  return created;
}

UiReplacementManagerCommand::UiReplacementManagerCommand() {
}

UiReplacementManagerCommand::~UiReplacementManagerCommand() {
}

// Publish the command's distinct primary identity before retaining it, delegating other identities to its command view.
bool UiReplacementManagerCommand::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiReplacementManagerIdentity)
    return UiCommand::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiReplacementManagerIdentity *>(this);
  AddRef();
  return true;
}

u32 UiReplacementManagerCommand::AddRef() {
  return UiCommand::AddRef();
}

u32 UiReplacementManagerCommand::Release() {
  return UiCommand::Release();
}

// Hold the real manager through modal shutdown and generic-view retirement, applying changes only for exact result one.
// Commit requested replacements before releasing the manager and persisting its final preferences during destruction.
bool UiReplacementManagerCommand::Execute() {
  UiReplacementManager *manager = new UiReplacementManager;
  manager->AddRef();
  bool result = false;
  UiGenericBase *generic = nullptr;
  if (UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericInterface, reinterpret_cast<void **>(&generic))) {
    generic->SetNotify(static_cast<UiWindowCallback *>(manager));
    WnWindow *window = generic->GetWindow();
    WnWindowManager *windows = window->GetWindowManager();
    i32 modal = windows->DoModalWin(generic->GetWindow());
    generic->GetWindow()->Shutdown();
    generic->Release();
    if (modal == 1) {
      result = true;
      DoRequestedChangesEffects(manager);
    }
  }
  manager->Release();
  return result;
}

// Capture each requested original/replacement pair before reading its current override and updating the captured
// scheme.
void UiReplacementManagerCommand::DoRequestedChangesEffects(UiReplacementManager *manager) {
  CtSchemeManager *schemes = UiGetApplication()->CitySchemeManager();
  if (!schemes)
    return;
  UiReplacementInfo *row = manager->infos.data;
  u32 count = manager->infos.size;
  for (u32 i = 0; i < count; ++i, ++row) {
    GeAttrib *original = row->original;
    OcOverride *replacement = row->replacement;
    if (replacement != original->GetOverRideAttrib())
      schemes->MakeBATReplacement(*original->Key(), replacement);
  }
}

// Return the replacement dialog's callback view at original offset4, as producer1006603A does without another retain.
ScUnknown *UiCreateClass756B5B54() {
  return static_cast<UiWindowProcedure *>(new UiReplacementProcedure);
}

// Construct the callback and retained instance data before capturing one provider for both original label colors.
UiReplacementProcedure::UiReplacementProcedure() {
  WnResources *resources = UiGetWindowResources();
  disabled_color = resources->GetColor(kWnColorGray);
  enabled_color = resources->GetColor(kWnThemeTextColorId);
}

UiReplacementProcedure::~UiReplacementProcedure() {
}

// Publish the replacement-control view for its own identity; other accepted queries retain the callback identity.
bool UiReplacementProcedure::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiReplacementControl)
    return UiWindowProcedure::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiReplacementControl *>(this);
  AddRef();
  return true;
}

u32 UiReplacementProcedure::AddRef() {
  return UiWindowProcedure::AddRef();
}

u32 UiReplacementProcedure::Release() {
  return UiWindowProcedure::Release();
}

// Borrow the selected replacement without retaining it; this pointer getter shares an offset24 body with other owners.
OcOverride *UiReplacementProcedure::GetReplacement() {
  return data.replacement;
}

// Retain the incoming occupant before releasing the previous one, then update its preview and three display rows.
// The final text transfer uses the captured input even if callbacks have changed the stored occupant pointer.
u8 UiReplacementProcedure::SetOriginalInfo(WnWindow *window, GeAttrib *attributes) {
  u8 result = 0;
  UiPreviewControl *preview = nullptr;
  if (attributes)
    attributes->AddRef();
  if (data.original)
    data.original->Release();
  data.original = attributes;
  if (window->GetChildAs(
          kUiReplacementReplacementPreviewId, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
    result = preview->SetAttributes(data.original);
    preview->Release();
  }
  WnWindow *display = window->GetChildWindowFromID(kUiReplacementReplacementAttributesId);
  UiAttributeText transfer;
  transfer.Transfer(attributes, display);
  return result;
}

// Replace the browsed override with incoming-retain-before-old-release ordering, then refresh its separate preview.
// Transfer the original argument to the override display after retiring the queried preview.
u8 UiReplacementProcedure::SetOverRideInfo(WnWindow *window, OcOverride *attributes) {
  u8 result = 0;
  UiPreviewControl *preview = nullptr;
  if (attributes)
    attributes->AddRef();
  if (data.override_attributes)
    data.override_attributes->Release();
  data.override_attributes = attributes;
  if (window->GetChildAs(kUiReplacementOriginalPreviewId, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
    result = preview->SetOverride(data.override_attributes);
    preview->Release();
  }
  WnWindow *display = window->GetChildWindowFromID(kUiReplacementOriginalAttributesId);
  UiAttributeText transfer;
  transfer.Transfer(attributes, display);
  return result;
}

// Select the live browsed override after releasing the old replacement, then update the original-side preview and text.
// Enable reversion only after a successful preview reply and after releasing that queried view.
u8 UiReplacementProcedure::Replace(WnWindow *window) {
  u8 result = 0;
  UiPreviewControl *preview = nullptr;
  if (data.replacement)
    data.replacement->Release();
  data.replacement = data.override_attributes;
  if (data.replacement)
    data.replacement->AddRef();
  if (window->GetChildAs(
          kUiReplacementReplacementPreviewId, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
    result = preview->SetOverride(data.override_attributes);
    if (result) {
      WnWindow *display = window->GetChildWindowFromID(kUiReplacementReplacementAttributesId);
      UiAttributeText transfer;
      transfer.Transfer(data.override_attributes, display);
    }
    preview->Release();
    if (result)
      SetRevertEnabledEffects(window, 1);
  }
  return result;
}

// Clear the selected replacement before resetting the preview; successful reset restores the live original's text.
// Disable reversion after the preview release, preserving the reply captured before that callback.
u8 UiReplacementProcedure::Revert(WnWindow *window) {
  u8 result = 0;
  UiPreviewControl *preview = nullptr;
  if (data.replacement) {
    data.replacement->Release();
    data.replacement = nullptr;
  }
  if (window->GetChildAs(
          kUiReplacementReplacementPreviewId, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
    result = preview->SetOverride(nullptr);
    if (result) {
      WnWindow *display = window->GetChildWindowFromID(kUiReplacementReplacementAttributesId);
      UiAttributeText transfer;
      transfer.Transfer(data.original, display);
    }
    preview->Release();
    if (result)
      SetRevertEnabledEffects(window, 0);
  }
  return result;
}

void UiReplacementProcedure::EndDialogEffects(WnWindow *window, u32 result) {
  UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), result);
}

// Capture the replacement label's color before changing its button, then update the separately queried label.
void UiReplacementProcedure::SetReplaceEnabledEffects(WnWindow *window, u8 enabled) {
  u32 color = enabled ? enabled_color : disabled_color;
  WnWindow *button = window->GetChildWindowFromID(kUiReplacementReplaceId);
  if (button)
    button->SetFlag(kWnFlagEnabled, enabled != 0);
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(kUiReplacementReplaceLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->SetTextColor(color);
    label->Release();
  }
}

// Capture the reversion label's color before the optional button update, then release its queried label after coloring.
void UiReplacementProcedure::SetRevertEnabledEffects(WnWindow *window, u8 enabled) {
  u32 color = enabled ? enabled_color : disabled_color;
  WnWindow *button = window->GetChildWindowFromID(kUiReplacementRevertId);
  if (button)
    button->SetFlag(kWnFlagEnabled, enabled != 0);
  WnLabelControl *label = nullptr;
  if (window->GetChildAs(kUiReplacementRevertLabelId, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->SetTextColor(color);
    label->Release();
  }
}

// Resolve the selected list index into a borrowed override, leaving outputs untouched when no selection is available.
// Release the temporary list after the container lookup, then inspect the caller's live published output.
u8 UiReplacementProcedure::GetSelectedOverride(WnWindow *window, u32 identifier, OcOverride **out) {
  if (!out)
    return 0;
  WnListControl *list = nullptr;
  if (!window->GetChildAs(identifier, IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  i32 index = list->GetSelection();
  bool result = false;
  if (index != -1)
    result = data.container->GetObject((u32)index, out);
  list->Release();
  return result && *out;
}

// Consume Escape or Windows Alt-F4 by posting a cancellation; all other key/modifier pairs remain unhandled.
u8 UiReplacementProcedure::OnKeyDown(WnWindow *window, u32 key, u32 modifiers) {
  if (key != kWnKeyEscape && (key != kWnKeyF4 || !(modifiers & kWnModifierAlt)))
    return 0;
  EndDialogEffects(window, 0);
  return 1;
}

// Capture audio once and keep the nested command alive through hide, execute, show and refresh.
// Successful accept/cancel closes the modal; replace/revert preserve their actual preview byte replies.
u8 UiReplacementProcedure::OnButton(WnWindow *window, u32 identifier) {
  ApBoxX *audio = UiGetBoxX();
  switch (identifier) {
  case kUiReplacementAcceptId:
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    EndDialogEffects(window, 1);
    return 1;
  case kUiReplacementEditOverridesId: {
    UiEditOverridesCommand command;
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    window->HideWindow();
    bool result = command.Execute();
    window->ShowWindow();
    if (result)
      RefreshAfterEditorEffects(window);
    return result;
  }
  case kUiReplacementRevertId:
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    return Revert(window);
  case kUiReplacementReplaceId:
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    return Replace(window);
  case kUiFrameCloseButtonId:
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    EndDialogEffects(window, 0);
    return 1;
  }
  return 0;
}

// Route button activation, override-list selection and the preview's two display-change notifications.
// Capture the original occupant before reading the selected lot size, as the source does before its nested calls.
u8 UiReplacementProcedure::OnCommand(WnWindow *window, u32 kind, uptr first, uptr) {
  u32 identifier = (u32)first;
  if (kind == 1)
    return OnButton(window, identifier);
  if (kind == 6 && identifier == kUiReplacementOverrideListId) {
    OcOverride *selected = nullptr;
    if (GetSelectedOverride(window, identifier, &selected)) {
      bool enabled = false;
      if (SetOverRideInfo(window, selected)) {
        GeAttrib *original = data.original;
        u8 size = selected->GetLotSize();
        enabled = size == original->SizeInCellsX();
      }
      SetReplaceEnabledEffects(window, enabled);
    }
    return 1;
  }
  if (identifier == kUiReplacementReplacementPreviewId) {
    if (kind == kUiPreviewReplacementSelected) {
      WnWindow *display = window->GetChildWindowFromID(kUiReplacementReplacementAttributesId);
      UiAttributeText transfer;
      transfer.Transfer(data.override_attributes, display);
      return 1;
    }
    if (kind == kUiPreviewOriginalSelected) {
      WnWindow *display = window->GetChildWindowFromID(kUiReplacementReplacementAttributesId);
      UiAttributeText transfer;
      transfer.Transfer(data.original, display);
      return 1;
    }
  }
  return 0;
}

// Build the fixed controls, format them, replay live attributes, repopulate the list and arrange the final window.
u8 UiReplacementProcedure::OnInit(WnWindow *window) {
  UiGenericInterface *generic = nullptr;
  if (!window->QueryInterface(IID_UiGenericInterface, reinterpret_cast<void **>(&generic)))
    return 0;
  u8 created = UiGetCom()->GetClassObject(
      kOcOverrideContainerClassId, IID_OcOverrideContainer, reinterpret_cast<void **>(&data.container));
  if (!created)
    return 0;
  window->SetSize(578, 412);
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  UiGenericInterface *layout = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&layout))) {
    layout->SetStyle(kUiGenericScroller, 1);
    ScRect rectangle = {4, 374, 572, 408};
    layout->AddPanel(kUiReplacementFooterPanelId, 0, 5, &rectangle);
    rectangle = {21, 237, 572, 359};
    layout->AddPanel(kUiReplacementLowerPanelId, 0, 15, &rectangle);
    rectangle = {289, 44, 572, 167};
    layout->AddPanel(kUiReplacementUpperRightPanelId, 0, 15, &rectangle);
    layout->Release();
  }
  AddControl(window, kUiResourceList, kUiReplacementOverrideListId);
  AddControl(window, kUiResourceStandardButton, kUiReplacementAcceptId);
  AddControl(window, kUiResourceCaptionButton, kUiReplacementEditOverridesId);
  UiReplacementControls::AddSlideFrame(
      window, kUiReplacementSlideFrameId, kRsUiImageGroup, kUiReplacementSlideFrameImageId);
  UiReplacementControls::AddButton(window, kUiReplacementReplaceId, kRsUiImageGroup, kUiReplacementReplaceImageId);
  UiReplacementControls::AddButton(window, kUiReplacementRevertId, kRsUiImageGroup, kUiReplacementRevertImageId);
  AddControl(window, kUiResourceLabel, kUiReplacementReplaceLabelId);
  AddControl(window, kUiResourceLabel, kUiReplacementRevertLabelId);
  AddControl(window, kUiResourceLabel, kUiReplacementInstructionsId);
  AddControl(window, kUiResourceColumns, kUiReplacementOriginalAttributesId);
  AddControl(window, kUiResourceBuildingPreview, kUiReplacementOriginalPreviewId);
  AddControl(window, kUiResourceColumns, kUiReplacementReplacementAttributesId);
  AddControl(window, kUiResourceBuildingPreview, kUiReplacementReplacementPreviewId);
  InitControls(window);
  if (data.original) {
    data.original->AddRef();
    SetOriginalInfo(window, data.original);
    data.original->Release();
  }
  if (data.override_attributes) {
    data.override_attributes->AddRef();
    SetOverRideInfo(window, data.override_attributes);
    Replace(window);
    data.override_attributes->Release();
  }
  RequeryList(window);
  ScRect area;
  window->GetArea(&area);
  ResizeAndPositionControls(window);
  generic->Release();
  return created;
}

// Release the queried view after data retirement; failed queries and reads retain the original success reply.
u8 UiReplacementProcedure::OnShutdown(WnWindow *window) {
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericInterface, reinterpret_cast<void **>(&generic))) {
    UiWindowData *payload = nullptr;
    if (generic->GetIndexedData(0, &payload)) {
      delete reinterpret_cast<UiReplacementData *>(payload);
      generic->SetIndexedData(0, nullptr);
    }
    generic->Release();
  }
  return 1;
}

void UiReplacementProcedure::RefreshAfterEditorEffects(WnWindow *window) {
  RequeryList(window);
  if (data.override_attributes && !data.original->GetOverRideAttrib())
    Revert(window);
}
