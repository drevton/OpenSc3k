#include "ui/override_editor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "buildings/architect/document.h"
#include "buildings/architect/manager.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/path.h"
#include "common/sc3_error.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "geometry/api.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "occupants/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "sprites/sprite.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/replacement_manager.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/window.h"

// Override confirmations preserve their complete yes/no and OK/cancel styles and the accepted-result threshold.
enum UiOverrideDialogPolicy : u32 {
  kUiOverrideYesNoConfirmationStyle = 0x2010003,
  kUiOverrideOkCancelConfirmationStyle = 0x2010001,
  kUiOverrideAcceptedResultLowerBound = 0x80000002,
};

// The override editor loads this image for its add-building button.
enum UiOverrideArtwork : u32 {
  kUiOverrideAddBuildingImageId = 0xfa3a81ca,
};

// The catalog picker accepts the selected row through its action button and separates the detail area with a divider.
enum UiCatalogPickerControlId : u32 {
  kUiCatalogSelectButtonId = 0xe2e80764,
  kUiCatalogDetailsDividerId = 0x42e6cc90,
};

// Original module-global positions retain a prior language expansion when later attachments use another language.
static i32 catalog_details_top = 260, catalog_first_top = 260, catalog_second_top = 260;
static i32 catalog_note_left = 60, catalog_note_top = 388, catalog_note_width = 250, catalog_note_height = 14;

#if defined(_M_IX86)
static_assert(sizeof(UiCatalogPicker) == 104);
#endif

UiCatalogPicker::UiCatalogPicker()
    : category(kUiQueryBuildings), has_selection(0), can_select(0), selected_index(0), next_label_id(1000),
      window(nullptr), preview(nullptr), list(nullptr), labels{}, frame(nullptr), action_button(nullptr) {
}

UiCatalogPicker::~UiCatalogPicker() {
}

bool UiCatalogPicker::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiBuildingPicker, UiBuildingPicker);
  return UiDataRows::QueryInterface(iid, out);
}

u32 UiCatalogPicker::AddRef() {
  return UiDataRows::AddRef();
}

u32 UiCatalogPicker::Release() {
  return UiDataRows::Release();
}

void UiCatalogPicker::SetCategoryEffects(u32 value) {
  category = value;
}

// Acquire and configure the generic window only once, retaining the factory result even when it publishes on refusal.
// A successful acquisition enables the private buffer before installing the actual callback view.
u8 UiCatalogPicker::Init() {
  if (window)
    return 1;
  window = nullptr;
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result && window) {
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericScroller, 1);
    window->GetWindow()->PrivateBuffer(true);
    return window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  return result;
}

// Borrow the primary window through the stored generic interface without adding an acquisition or null fallback.
WnWindow *UiCatalogPicker::GetWindow() const {
  return window->GetWindow();
}

bool UiCatalogPicker::HasSelection() const {
  return has_selection != 0 && can_select != 0;
}

// Borrow the attribute cache before acquiring the row iterator, then resolve the selected row's actual three-word key.
// Preserve the source's zero reply even when the cache publishes an attribute through the caller's genuine output.
u8 UiCatalogPicker::GetSelection(GeAttrib **out) {
  UiQueryIterator *iterator = nullptr;
  OcAttribCache *cache = UiGetAttribCache();
  if (cache && CreateIterator(&iterator)) {
    iterator->AddRef();
    UiQueryRow *row = iterator->GetAt(selected_index);
    if (row) {
      RsKey key = {0, 0, 0};
      key.type = row->GetU32(kUiCatalogResourceTypeField);
      key.group = row->GetU32(kUiCatalogResourceGroupField);
      key.instance = row->GetU32(kUiCatalogResourceInstanceField);
      cache->GetOccupantAttrib(key, out);
    }
    iterator->Release();
  }
  return 0;
}

// Deliver opening or closing catalog messages to the optional city before enumerating layers through this separate view.
u8 UiCatalogPicker::NotifyTarget(CtCity *city, u8 opening) {
  ScMessage message(opening ? kWnMsg_QueryOpened : kWnMsg_QueryClosed, category, 0, 0);
  if (city) {
    city->DoQueryInfo(&message, static_cast<UiBuildingPicker *>(this));
    city->DoEnumLayers(&message, static_cast<CtCityEnumerator *>(this));
  }
  return 1;
}

bool UiCatalogPicker::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<UiBuildingPicker *>(this));
  return true;
}

u8 UiCatalogPicker::EndModal(WnWindow *recipient, u32 result) {
  NotifyTarget(UiGetCity(), 0);
  return UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(recipient), result);
}

// Route window lifecycle, cancellation keys, list selection and accept/cancel actions through the real picker owner.
// Explicit button actions play their source audio notification before updating modal state or closing.
bool UiCatalogPicker::DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Init)
    return Attach(recipient);
  if (message == kWnMsg_Shutdown)
    return Detach(recipient);
  if (message == kWnMsg_KeyDown) {
    if (first == 27 || (first == 115 && (second & 4)))
      return EndModal(recipient, 0);
    return 0;
  }
  if (message != kWnMsg_Command)
    return 0;
  if (first == kWnCommandListSelectionChanged) {
    i32 index = list->GetSelection();
    SelectEntry((u32)index);
    return 1;
  }
  if (first == 15 && can_select) {
    has_selection = GetCount() != 0;
    return EndModal(recipient, 1);
  }
  if (first != 1)
    return 0;
  if (second == kUiFrameCloseButtonId) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectCloseWindow, 0, 0, 0);
    return EndModal(recipient, 0);
  }
  if (second != kUiCatalogSelectButtonId)
    return 0;
  UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  has_selection = GetCount() != 0;
  return EndModal(recipient, 1);
}

// Save position and the matching category's live list selection, then retire controls in their original callback order.
// Reacquire preferences for each write and clear each retained field only after its release callback returns.
u8 UiCatalogPicker::Detach(WnWindow *recipient) {
  ApPreferences *preferences = UiGetPreferences();
  i32 x = recipient->GetL();
  preferences->SelectABuildingPrefs()->x = x;
  preferences = UiGetPreferences();
  i32 y = recipient->GetT();
  preferences->SelectABuildingPrefs()->y = y;
  if (list) {
    i32 slot = -1;
    switch (category) {
    case kUiQueryLandmarks:
      slot = 3;
      break;
    case kUiQueryRewards:
      slot = 4;
      break;
    case kUiQueryPowerBuildings:
      slot = 0;
      break;
    case kUiQueryWasteBuildings:
      slot = 2;
      break;
    case kUiQueryWaterBuildings:
      slot = 1;
      break;
    default:
      break;
    }
    if (slot >= 0) {
      preferences = UiGetPreferences();
      i32 index = list->GetSelection();
      preferences->SelectABuildingPrefs()->selections[slot] = index;
    }
  }
  UiGetPreferences()->SelectABuildingPrefs()->enabled = 1;
  ScRelease(&preview);
  ScRelease(&list);
  for (u32 index = 0; index < 8; ++index)
    if (labels[index]) {
      labels[index]->Release();
      labels[index] = nullptr;
    }
  ScRelease(&frame);
  ScRelease(&action_button);
  ScRelease(&window);
  return 1;
}

// Create the next identified empty label, retire its caption and configure font, flags, alignment and wrapping geometry.
// Attach the borrowed label window to the live generic primary without adding another reference or checking attachment.
WnLabelControl *UiCatalogPicker::CreateRegion(i32 left, i32 top, i32 width, i32 height) {
  WnLabelControl *label;
  {
    ScString empty("");
    u32 identifier = next_label_id++;
    label = UiGetWindowResources()->CreateLabel(identifier, empty);
  }
  if (label) {
    ScRect rectangle = {left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height)};
    label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
    label->SetLabelFlag(kWnLabelFillBackground, false);
    label->SetAlignment(kWnLabelLeftTop);
    label->GetWindow()->SetArea(rectangle);
    WnWindow *primary = window->GetWindow();
    primary->ChildAdd(label->GetWindow());
  }
  return label;
}

static ScString UiCopyCatalogText(UiTextRef *resource) {
  ScString loaded(*resource->GetResource());
  return ScString(loaded);
}

// Populate filtered catalog rows, then measure scrollbar gutters using temporary empty rows before fitting names.
// Preserve borrowed iterator entries, scoped resource copies, marked captions and the final selection callback.
u8 UiCatalogPicker::BuildCatalog() {
  OcAttribCache *cache = UiGetAttribCache();
  if (!cache)
    return 0;
  if (category == kUiQueryBuildings) {
    CtAgentTree *tree = UiGetApplication()->GetAppAgentTypeTree();
    if (!tree)
      return 0;
    OcCacheIterator *iterator = UiGetApplication()->GetAppOccupantAttribCache()->CreateNodeIterator();
    if (!iterator)
      return 0;
    for (GeAttrib *attribute = iterator->GetNext(); attribute; attribute = iterator->GetNext()) {
      CtAgentType *type = attribute->CityAgentType();
      u32 identifier = type ? type->Id() : 0;
      if (tree->IsAncestor(100, identifier) && attribute->MajorType() == 1) {
        RsKey key = *attribute->Key();
        UiQueryRow *row = CreateRow();
        row->SetU32(kUiCatalogResourceTypeField, key.type);
        row->SetU32(kUiCatalogResourceGroupField, key.group);
        row->SetU32(kUiCatalogResourceInstanceField, key.instance);
        AppendRow(row);
      }
    }
    iterator->Release();
  }
  if (GetCount() >= 12) {
    ScRect rectangle = {173, 56, 362, 220};
    list->GetWindow()->SetArea(rectangle);
  }
  for (u32 index = 0; index < GetCount(); ++index) {
    ScString empty;
    list->InsertString(empty, -2);
  }
  ScRect text_area;
  list->GetTextArea(&text_area);
  list->RemoveAllStrings();
  i32 width = (i32)((u32)text_area.right - (u32)text_area.left);
  UiQueryIterator *iterator = nullptr;
  if (CreateIterator(&iterator)) {
    iterator->AddRef();
    for (UiQueryRow *row = iterator->GetNext(); row; row = iterator->GetNext()) {
      ScString name;
      RsKey key;
      key.type = row->GetU32(kUiCatalogResourceTypeField);
      key.group = row->GetU32(kUiCatalogResourceGroupField);
      key.instance = row->GetU32(kUiCatalogResourceInstanceField);
      GeAttrib *attribute;
      if (cache->GetOccupantAttrib(key, &attribute)) {
        attribute->GetName(&name);
        attribute->Release();
      }
      FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
      if (font) {
        u32 fit = font->Fit(name.begin, name.Strlen(), (u32)width);
        if (!fit)
          name.Erase(0, name.Strlen());
        else if (fit < name.Strlen()) {
          UiTextRef resource(0x646, kRsCommonTextGroup);
          ScString ellipsis = UiCopyCatalogText(&resource);
          ScRect extent;
          font->TextArea(ellipsis.begin, ellipsis.Strlen(), &extent);
          i32 ellipsis_width = (i32)((u32)extent.right - (u32)extent.left);
          if (ellipsis_width >= width)
            name.Erase(0, name.Strlen());
          else {
            const char *bytes = name.begin;
            FtFont *current_font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
            u32 count = current_font->Fit(bytes, name.Strlen(), (u32)width - (u32)ellipsis_width);
            {
              ScString prefix(name.begin, count <= name.Strlen() ? count : name.Strlen());
              name = prefix;
            }
            u32 length = name.Strlen();
            while (length && name.begin[length - 1] == ' ')
              --length;
            name.Erase(length, name.Strlen() - length);
            ScString joined(name);
            joined.Append(ellipsis.begin, ellipsis.Strlen());
            name = joined;
          }
        }
      }
      if (category == kUiQueryLandmarks && row->GetU32(kUiCatalogAlreadyPlacedField)) {
        ScString marked("#7D81B5");
        marked.Append(name.begin);
        list->InsertString(marked, -2);
      } else
        list->InsertString(name, -2);
    }
    iterator->Release();
  }
  if (!GetCount()) {
    ScString empty_message;
    u32 instance = 0;
    bool known = true;
    switch (category) {
    case kUiQueryLandmarks:
      instance = 0x77;
      break;
    case kUiQueryRewards:
      instance = 0x6e;
      break;
    case kUiQueryPowerBuildings:
      instance = 0x7a;
      break;
    case kUiQueryBuildings:
      instance = UINT32_MAX;
      break;
    case kUiQueryWasteBuildings:
      instance = 0x79;
      break;
    case kUiQueryWaterBuildings:
      instance = 0x7b;
      break;
    default:
      known = false;
      break;
    }
    if (known) {
      UiTextRef resource(instance, kRsBuildingCatalogTextGroup);
      ScString loaded = resource.GetText(), copy(loaded);
      empty_message = copy;
    }
    labels[6]->GetWindow()->SetCaption(empty_message);
  }
  list->SetSelection(0, 1);
  return 1;
}

void UiCatalogPicker::CenterEffects() {
  ScRect parent_area, own_area;
  window->GetWindow()->GetParentWin()->GetArea(&parent_area);
  window->GetWindow()->GetArea(&own_area);
  i32 horizontal = (i32)((u32)own_area.left + (u32)parent_area.right - (u32)own_area.right - (u32)parent_area.left);
  i32 vertical = (i32)((u32)parent_area.bottom + (u32)own_area.top - (u32)parent_area.top - (u32)own_area.bottom);
  window->GetWindow()->GZWinMoveTo(horizontal / 2, vertical / 2);
}

static ScString UiCopyCatalogHeading() {
  UiTextRef resource(kRsBuildingCatalogBuildingsAvailableText, kRsBuildingCatalogTextGroup);
  ScString loaded = resource.GetText(), intermediate(loaded);
  return ScString(intermediate);
}

// Restore the title, saved position, controls and preview before enumerating the city catalog.
// Reapply the category's saved list selection after catalog population and its synchronous selection notification.
u8 UiCatalogPicker::Attach(WnWindow *recipient) {
  if (!window)
    return 0;
  i32 language = UiGetLanguageManager()->GetCurrentLanguage();
  if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai) {
    catalog_note_left = 26;
    catalog_details_top = 264;
    catalog_first_top = 264;
    catalog_second_top = 264;
    catalog_note_top = 369;
    catalog_note_width = 284;
    catalog_note_height = 40;
  }
  ScString title;
  u32 instance = 0;
  bool known = true;
  switch (category) {
  case kUiQueryLandmarks:
    instance = 0x44;
    break;
  case kUiQueryRewards:
    instance = 0x2a;
    break;
  case kUiQueryPowerBuildings:
    instance = 1;
    break;
  case kUiQueryBuildings:
    instance = 0x7c;
    break;
  case kUiQueryWasteBuildings:
    instance = 0x75;
    break;
  case kUiQueryWaterBuildings:
    instance = 0x1d;
    break;
  default:
    known = false;
    break;
  }
  if (known) {
    UiTextRef resource(instance, kRsBuildingCatalogTextGroup);
    ScString loaded = resource.GetText(), copy(loaded);
    title = copy;
  }
  recipient->SetCaption(title);
  recipient->SetSize(370, 412);
  bool positioned = false;
  if (UiGetPreferences()->SelectABuildingPrefs()->enabled) {
    ApPreferences *vertical = UiGetPreferences(), *horizontal = UiGetPreferences(), *visibility = UiGetPreferences();
    i32 top = vertical->SelectABuildingPrefs()->y;
    i32 left = horizontal->SelectABuildingPrefs()->x;
    if (visibility->IsWindowVisible(left, top, 370, 412)) {
      vertical = UiGetPreferences();
      horizontal = UiGetPreferences();
      top = vertical->SelectABuildingPrefs()->y;
      left = horizontal->SelectABuildingPrefs()->x;
      recipient->GZWinMoveTo(left, top);
      positioned = true;
    }
  }
  if (!positioned)
    CenterEffects();
  window->AddDivider(kUiCatalogDetailsDividerId, 225);
  labels[0] = CreateRegion(173, 38, 160, 14);
  labels[1] = CreateRegion(26, catalog_first_top, 133, 14);
  labels[2] = CreateRegion(178, catalog_second_top, 181, 14);
  labels[3] = CreateRegion(26, 246, 260, 14);
  labels[4] = CreateRegion(26, catalog_details_top, 200, 14);
  labels[5] = CreateRegion(50, 288, 260, 108);
  labels[6] = CreateRegion(60, 288, 250, 52);
  labels[7] = CreateRegion(catalog_note_left, catalog_note_top, catalog_note_width, catalog_note_height);
  for (u32 index = 0; index < 8; ++index)
    if (!labels[index])
      return 0;
  labels[6]->SetAlignment(kWnLabelCenterTop);
  language = UiGetLanguageManager()->GetCurrentLanguage();
  labels[7]->SetAlignment(language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
              language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai
          ? 0
          : 3);
  ScString heading = UiCopyCatalogHeading();
  labels[0]->GetWindow()->SetCaption(heading);
  labels[0]->SetTextColor(recipient->MakeFillColor(96, 96, 159));
  ScString description;
  switch (category) {
  case kUiQueryLandmarks: {
    ScString empty;
    description = empty;
    break;
  }
  case kUiQueryRewards:
  case kUiQueryPowerBuildings:
  case kUiQueryBuildings:
  case kUiQueryWasteBuildings:
  case kUiQueryWaterBuildings: {
    ScString empty("");
    description = empty;
    break;
  }
  }
  labels[7]->GetWindow()->SetCaption(description);
  labels[7]->SetTextColor(recipient->MakeFillColor(96, 96, 159));
  if (!UiGetCom()->GetClassObject(kWnListClassId, IID_WnListControl, reinterpret_cast<void **>(&list)))
    return 0;
  list->SetDrawSunkenRect(true);
  WnResources *resources = UiGetWindowResources();
  WnWindow *primary = list->GetWindow();
  primary->SetFillColor(resources->GetColor(kWnColorUiBackground));
  u32 color = list->GetWindow()->MakeFillColor(0, 0, 255);
  list->SetHighlightColor(color);
  ScRect rectangle = {173, 56, 339, 220};
  list->GetWindow()->SetArea(rectangle);
  list->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  recipient->ChildAdd(list->GetWindow());
  if (!UiGetCom()->GetClassObject(
          kSpCitySpritePreviewClassId, IID_SpCitySpritePreview, reinterpret_cast<void **>(&preview)))
    return 0;
  preview->AsIGZWin()->SetArea(ScRect{26, 38, 163, 220});
  preview->SetDrawSunkenRect(true);
  preview->SetKeepWindowSize(true);
  recipient->ChildAdd(preview->AsIGZWin());
  if (!UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&frame)))
    return 0;
  u32 fill = UiGetWindowResources()->GetColor(kWnThemePanelFillColorId);
  u32 outline = UiGetWindowResources()->GetColor(kWnThemeBorderColorId);
  frame->GetWindow()->SetFlag(kWnFlagReorderable, false);
  frame->GetWindow()->SetArea(317, 369, 361, 403);
  frame->GetWindow()->SetFillColor(fill);
  frame->SetOutlineColor(static_cast<u16>(outline));
  frame->SetOutlineFlags(15);
  recipient->ChildAdd(frame->GetWindow());
  GxBuffer *image;
  {
    UiImageRef resource(
        kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, GxColorType(GxColorType::Color::RGB16));
    image = resource.GetBuffer();
  }
  {
    ScString caption;
    WnResources *button_resources = UiGetWindowResources();
    const ScRect *image_area = image->GetBufferArea();
    action_button = button_resources->CreateFixedButton(kUiCatalogSelectButtonId, caption, image, image_area, 32);
  }
  action_button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  action_button->SetButtonFlag(kWnButtonShowText, false);
  action_button->GetWindow()->GZWinMoveTo(318, 370);
  recipient->ChildAdd(action_button->GetWindow());
  NotifyTarget(UiGetCity(), 1);
  BuildCatalog();
  if (UiGetPreferences()->SelectABuildingPrefs()->enabled) {
    u32 selection = 0;
    switch (category) {
    case kUiQueryLandmarks:
      selection = UiGetPreferences()->SelectABuildingPrefs()->selections[3];
      break;
    case kUiQueryRewards:
      selection = UiGetPreferences()->SelectABuildingPrefs()->selections[4];
      break;
    case kUiQueryPowerBuildings:
      selection = UiGetPreferences()->SelectABuildingPrefs()->selections[0];
      break;
    case kUiQueryWasteBuildings:
      selection = UiGetPreferences()->SelectABuildingPrefs()->selections[2];
      break;
    case kUiQueryWaterBuildings:
      selection = UiGetPreferences()->SelectABuildingPrefs()->selections[1];
      break;
    }
    if (list && static_cast<u32>(list->GetSelection()) != selection) {
      if (static_cast<i32>(selection) >= list->GetItemCount())
        list->SetSelection(static_cast<i32>(static_cast<u32>(list->GetItemCount()) - 1), true);
      else
        list->SetSelection(static_cast<i32>(selection), true);
    }
  }
  return 1;
}

// An unavailable landmark disables acceptance and applies the original alternate caption colors.
u8 UiCatalogPicker::SelectEntry(u32 index) {
  OcAttribCache *cache = UiGetAttribCache();
  if (!cache)
    return 0;
  if (index >= GetCount())
    return 1;
  selected_index = index;
  UiQueryIterator *iterator = nullptr;
  if (CreateIterator(&iterator)) {
    iterator->AddRef();
    UiQueryRow *row = iterator->GetAt(selected_index);
    RsKey key;
    GeAttrib *attribute = nullptr;
    ScString name;
    key.type = row->GetU32(kUiCatalogResourceTypeField);
    key.group = row->GetU32(kUiCatalogResourceGroupField);
    key.instance = row->GetU32(kUiCatalogResourceInstanceField);
    if (cache->GetOccupantAttrib(key, &attribute)) {
      preview->SetCitySprite(attribute);
      preview->SetSpriteZoomToFitInWindowArea();
      attribute->GetName(&name);
      labels[3]->SetCaption(name);
      attribute->Release();
    } else {
      preview->SetCitySprite(static_cast<GeAttrib *>(nullptr));
    }
    const u32 keys[] = {
        kUiCatalogDescriptionField, kUiCatalogSecondaryTextField, kUiCatalogDetailHeadingField, kUiCatalogNameField};
    bool fallback = false;
    for (u32 caption = 0; caption < 4; ++caption) {
      if (caption == 3 && fallback) {
        ScString empty;
        labels[5]->SetCaption(empty);
        continue;
      }
      ScStringView *value = row->GetText(keys[caption]);
      if (!value && caption == 2 && category == kUiQueryLandmarks) {
        value = row->GetText(kUiCatalogNameField);
        fallback = value != nullptr;
      }
      u32 label = caption < 2 ? caption + 1 : caption + 2;
      if (value) {
        value->AddRef();
        labels[label]->SetCaption(*value);
        value->Release();
      } else {
        ScString empty;
        labels[label]->SetCaption(empty);
      }
    }
    can_select = 1;
    if (category == kUiQueryLandmarks) {
      labels[6]->SetTextColor(window->GetWindow()->MakeFillColor(96, 96, 159));
      ScString unavailable;
      if (static_cast<i32>(row->GetU32(kUiCatalogAlreadyPlacedField)) > 0) {
        UiTextRef resource(kRsBuildingCatalogAlreadyPlacedText, kRsBuildingCatalogTextGroup);
        ScString loaded = resource.GetText(), copy(loaded);
        unavailable = copy;
      }
      labels[6]->SetCaption(unavailable);
      if (!unavailable.Strlen()) {
        action_button->GetWindow()->SetFlag(kWnFlagEnabled, true);
        u32 color = 0;
        FtFont *font = UiGetWindowResources()->GetFont(kWnFontDefault);
        if (font)
          color = font->Color();
        labels[1]->SetTextColor(color);
        labels[4]->SetTextColor(color);
        labels[3]->SetTextColor(color);
      } else {
        {
          ScString empty;
          labels[5]->SetCaption(empty);
        }
        action_button->GetWindow()->SetFlag(kWnFlagEnabled, false);
        u32 color = window->GetWindow()->MakeFillColor(96, 96, 159);
        labels[1]->SetTextColor(color);
        labels[4]->SetTextColor(color);
        labels[3]->SetTextColor(color);
        can_select = 0;
      }
    }
    iterator->Release();
  }
  return 1;
}

// Construct the real104-byte catalog picker and return its secondary picker view without an extra retain.
ScUnknown *UiCreateClassE3091435() {
  return static_cast<UiBuildingPicker *>(new UiCatalogPicker());
}

UiEditBuilding::UiEditBuilding() : key(0, 0, 0) {
}

UiEditBuilding::~UiEditBuilding() {
}

UiEditOverridesCommand::UiEditOverridesCommand() {
}

UiEditOverridesCommand::~UiEditOverridesCommand() {
}

// Only the own-identity branch checks output storage before retaining the complete owner.
bool UiEditOverridesCommand::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiEditOverridesIdentity)
    return UiCommand::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiEditOverridesIdentity *>(this);
  AddRef();
  return true;
}

u32 UiEditOverridesCommand::AddRef() {
  return UiCommand::AddRef();
}

u32 UiEditOverridesCommand::Release() {
  return UiCommand::Release();
}

// Attach a retained stack editor to a generic modal, using fresh primary getters for each original operation.
bool UiEditOverridesCommand::Execute() {
  UiOverrideEditor procedure;
  bool result = false;
  procedure.AddRef();
  UiGenericBase *generic = nullptr;
  if (UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericInterface, reinterpret_cast<void **>(&generic))) {
    generic->SetNotify(static_cast<UiWindowCallback *>(&procedure));
    generic->GetWindow()->SetArea(8, 8, 523, 353);
    WnWindowManager *manager = generic->GetWindow()->GetWindowManager();
    i32 modal_result = manager->DoModalWin(generic->GetWindow());
    generic->GetWindow()->Shutdown();
    generic->Release();
    generic = nullptr;
    result = modal_result >= (i32)kUiOverrideAcceptedResultLowerBound;
  }
  return result;
}

UiEditorPaths::UiEditorPaths() : head(new UiEditorPathLink) {
  head->next = head;
  head->previous = head;
}

UiEditorPaths::~UiEditorPaths() {
  Clear();
  delete head;
}

// Delete real directory names in forward order, retaining the original sentinel identity for future publications.
void UiEditorPaths::Clear() {
  UiEditorPathLink *node = head->next;
  while (node != head) {
    UiEditorPathLink *next = node->next;
    delete static_cast<UiEditorPath *>(node);
    node = next;
  }
  head->next = head;
  head->previous = head;
}

// Append an independent full caption before its display copy is shortened by the font-fitting callback.
void UiEditorPaths::Append(const ScString &value) {
  UiEditorPath *node = new UiEditorPath{{head, head->previous}, value};
  head->previous->next = node;
  head->previous = node;
}

// Copy the live saved readiness byte and directory only after all five colors have been captured.
UiOverrideEditor::UiOverrideEditor() : preferences{0, {}} {
  preview = nullptr;
  document = nullptr;
  active_selected = 1;
  file_selected = 0;
  selection_busy = 0;
  added_count = 0;
  disabled_color = UiGetWindowResources()->GetColor(kWnColorGray);
  highlight_color = UiGetWindowResources()->GetColor(kWnColorWhite);
  first_color = UiGetGraphics()->ConvertRGBValueToNative16Bit(127, 144, 255);
  second_color = UiGetGraphics()->ConvertRGBValueToNative16Bit(144, 168, 255);
  third_color = UiGetGraphics()->ConvertRGBValueToNative16Bit(175, 168, 175);
  ApEditBldgPrefs *saved = UiGetPreferences()->EditBATBldgListPrefs();
  preferences.ready = saved->ready;
  preferences.directory_path = saved->directory_path;
}

// Delete every owned building record before releasing the live preview buffer. Member retirement then frees the
// removed-key vector, pointer vector, directory list and strings; the document pointer adds no release here.
UiOverrideEditor::~UiOverrideEditor() {
  for (u32 i = 0; i < buildings.size; ++i)
    delete buildings.data[i];
  if (preview)
    preview->Release();
}

// Publish the editor's own primary identity before retaining; other accepted identities retain the callback subobject.
bool UiOverrideEditor::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiEditorIdentity)
    return UiWindowProcedure::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiEditorIdentity *>(this);
  AddRef();
  return true;
}

u32 UiOverrideEditor::AddRef() {
  return UiWindowProcedure::AddRef();
}

u32 UiOverrideEditor::Release() {
  return UiWindowProcedure::Release();
}

// Compare two live name snapshots per row, confirming duplicates before inserting the pending file in sorted order.
// Notify selection before incrementing the added count, keeping the display String alive through both list releases.
u8 UiOverrideEditor::AddBuilding(WnWindow *window) {
  WnListControl *active = nullptr, *files = nullptr;
  if (!window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&active)) ||
      !window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&files)))
    return 1;
  u32 index = 0;
  UiEditBuilding *duplicate = nullptr;
  while (index < buildings.size) {
    UiEditBuilding *building = buildings.data[index];
    bool equal;
    {
      ScString candidate(*selected_name);
      u32 length = building->name.ScString::Strlen();
      equal = length == candidate.ScString::Strlen() &&
          !memcmp(building->name.ScString::Data(), candidate.ScString::Data(), length);
    }
    if (equal) {
      duplicate = building;
      break;
    }
    bool less;
    {
      ScString candidate(*selected_name);
      u32 first = candidate.ScString::Strlen(), second = building->name.ScString::Strlen();
      int order = memcmp(candidate.ScString::Data(), building->name.ScString::Data(), first < second ? first : second);
      less = order < 0 || (!order && first < second);
    }
    if (less)
      break;
    ++index;
  }
  if (duplicate) {
    if (UiShowResourceMessage(kRsArchitectSupplementalTextGroup, kRsArchitectSupplementalDuplicateBuildingNameText,
            kRsArchitectSupplementalTextGroup, 0x0d, kUiOverrideYesNoConfirmationStyle) != kWnYesButtonId)
      return 1;
    if (duplicate->pending)
      --added_count;
    else
      *removed.Append() = duplicate->key;
    for (u32 i = index + 1; i < buildings.size; ++i)
      buildings.data[i - 1] = buildings.data[i];
    --buildings.size;
    active->RemoveString((i32)index);
  }
  UiEditBuilding *building = new UiEditBuilding;
  building->pending = 1;
  {
    ScString name(*selected_name);
    building->name = name;
  }
  building->lot_size = lot_size;
  building->file = file;
  buildings.Append();
  for (u32 i = buildings.size - 1; i > index; --i)
    buildings.data[i] = buildings.data[i - 1];
  buildings.data[index] = building;
  ScString caption("+ ");
  caption.Append(building->name.ScString::Data(), building->name.ScString::Strlen());
  i32 width = (i32)((u32)active->GetWindow()->GetW() - 26);
  FtFont *font = nullptr;
  GetFont(14, &font);
  UiTextRef::FormatForEllipsis(&caption, width, font);
  active->InsertString(caption, (i32)index);
  active->SetSelection((i32)index, true);
  ++added_count;
  active->Release();
  files->Release();
  return 1;
}

// Route selection, navigation and action notifications through the editor's real methods using one captured audio view.
// On close, save the live directory before a fresh preferences readiness update and the window completion post.
u8 UiOverrideEditor::OnCommand(WnWindow *window, u32 kind, uptr first, uptr) {
  ApBoxX *audio = UiGetBoxX();
  if (kind != 1) {
    if (kind == 6) {
      if (first == kUiOverrideEditorFileListId) {
        if (active_selected)
          DeselectActiveList(window);
        HandleFileListSelected(window);
      }
      if (first == kUiOverrideEditorActiveListId) {
        if (file_selected)
          DeselectFileList(window);
        HandleActiveListSelected(window);
      }
    } else if (kind == 11) {
      if (first == kUiOverrideEditorDriveComboId)
        DriveChange(window);
    } else if (kind == 15 && file_selected)
      HandleFileListDoubleClick(window);
    return 0;
  }
  if (first != kUiOverrideEditorAcceptId) {
    if (first == kUiOverrideEditorAddBuildingId) {
      if (audio)
        audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      AddBuilding(window);
    } else if (first == kUiOverrideEditorRemoveBuildingId) {
      if (audio)
        audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
      RemoveBuilding(window);
    }
    return 0;
  }
  if (audio)
    audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
  if (RenderBuildings(window) != 1)
    return 0;
  ApEditBldgPrefs *saved = UiGetPreferences()->EditBATBldgListPrefs();
  saved->directory_path = directory;
  UiGetPreferences()->EditBATBldgListPrefs()->ready = 1;
  return UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
}

// Clear the active-list state before entering its synchronous selection callback. Keep the guard set through the
// queried view's release, then clear it even if lookup or selection was refused.
u8 UiOverrideEditor::DeselectActiveList(WnWindow *window) {
  if (selection_busy)
    return 0;
  active_selected = 0;
  selection_busy = 1;
  WnListControl *list = nullptr;
  if (window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    list->SetSelection(-1, true);
    list->Release();
  }
  selection_busy = 0;
  return 1;
}

// Select the first active row only when the queried list reports a positive signed count. Retire the queried view
// before returning the captured byte; the selection method's own Boolean reply is ignored.
u8 SC3_STDCALL UiOverrideEditor::SelectActiveList(WnWindow *window) {
  u8 result = 0;
  WnListControl *list = nullptr;
  if (window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    if (list->GetItemCount() > 0) {
      list->SetSelection(0, true);
      result = 1;
    }
    list->Release();
  }
  return result;
}

// Clear the file-list state under the same owner guard used by active selection.
u8 UiOverrideEditor::DeselectFileList(WnWindow *window) {
  if (selection_busy)
    return 0;
  file_selected = 0;
  selection_busy = 1;
  WnListControl *list = nullptr;
  if (window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    list->SetSelection(-1, true);
    list->Release();
  }
  selection_busy = 0;
  return 1;
}

// Select file row zero with notification when at least one row exists, then release the acquired list view.
u8 SC3_STDCALL UiOverrideEditor::SelectFileList(WnWindow *window) {
  u8 result = 0;
  WnListControl *list = nullptr;
  if (window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    if (list->GetItemCount() > 0) {
      list->SetSelection(0, true);
      result = 1;
    }
    list->Release();
  }
  return result;
}

// Update the add button's enabled bit when the primary child exists; absence alone returns byte zero.
u8 SC3_STDCALL UiOverrideEditor::UpdateAddButton(WnWindow *window, bool enabled) {
  WnWindow *button = window->GetChildWindowFromID(kUiOverrideEditorAddBuildingId);
  if (!button)
    return 0;
  button->SetFlag(kWnFlagEnabled, enabled);
  return 1;
}

// Update the remove button's enabled bit through its primary window, ignoring the flag setter's previous-state word.
u8 SC3_STDCALL UiOverrideEditor::UpdateRemoveButton(WnWindow *window, bool enabled) {
  WnWindow *button = window->GetChildWindowFromID(kUiOverrideEditorRemoveBuildingId);
  if (!button)
    return 0;
  button->SetFlag(kWnFlagEnabled, enabled);
  return 1;
}

// Show the lot-size child before querying its label view. Copy the resource through Strlen then ToChar, append the
// byte-sized square dimensions, and release the label before the resource and three local strings retire.
u8 SC3_STDCALL UiOverrideEditor::UpdateLotSizeLabel(WnWindow *window, u8 size) {
  ScString format, dimensions, caption;
  WnWindow *control = window->GetChildWindowFromID(kUiOverrideEditorPreviewCaptionId);
  if (!control)
    return 0;
  control->ShowWindow();
  WnLabelControl *label = nullptr;
  if (!control->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label)))
    return 0;
  {
    UiTextRef resource(kRsSupplementalGameSizeText, kRsSupplementalGameTextGroup);
    caption.Copy(*resource.GetResource());
    format.FromChar("%dx%d");
    char buffer[32];
    int count = snprintf(buffer, sizeof(buffer), format.Data(), size, size);
    dimensions.FromChar(buffer, (u32)count);
    caption.Append(dimensions.Data(), dimensions.Strlen());
    label->SetCaption(caption);
    label->Release();
    label = nullptr;
  }
  return 1;
}

// Hide the lot-size primary window if present, without acquiring another interface or changing its stored caption.
u8 SC3_STDCALL UiOverrideEditor::HideLotSizeLabel(WnWindow *window) {
  WnWindow *control = window->GetChildWindowFromID(kUiOverrideEditorPreviewCaptionId);
  if (control)
    control->HideWindow();
  return 1;
}

#if defined(_WIN32)
static constexpr char UiEditorSeparator = '\\';
static constexpr const char *UiEditorSeparatorString = "\\";
#else
static constexpr char UiEditorSeparator = '/';
static constexpr const char *UiEditorSeparatorString = "/";
#endif

// Rebuild drive ancestry and file rows under the original reentry guard, preserving each full caption before fitting.
// Keep the captured search directory independent from live owner changes made by combo/list callbacks.
u8 UiOverrideEditor::ReadDirectory(WnComboControl *combo, WnListControl *files) {
  if (reading_directory)
    return 0;
  reading_directory = 1;
  ScString caption;
  ScDirectoryNames directories, names;
  FtFont *font = nullptr;
  GetFont(14, &font);
  ScDirectory *search = new ScDirectory(directory, true, false, false);
  ScString row;
  i32 index = 0;
  while (combo->GetStringFromIndex(index, &row) == 1)
    if (row.ScString::Strlen() && row.ScString::Data()[0] == ' ')
      combo->RemoveString(index);
    else
      ++index;
  index = 0;
  while (combo->GetStringFromIndex(index, &row) == 1) {
    u32 length = row.ScString::Strlen(), start = length ? 1 : 0;
    while (start < length && row.ScString::Data()[start] == ' ')
      ++start;
    char first = row.ScString::Data()[start], second = directory.ScString::Data()[0];
    int order = ScCompareNoCase(&first, &second, 1);
    if (!order) {
#if defined(_WIN32)
      ScPath path(directory);
      ScString remainder(path.Directory()), component;
#else
      ScString remainder(directory), component;
#endif
      if (remainder.ScString::Strlen() && remainder.ScString::Data()[0] == UiEditorSeparator)
        remainder.Erase(0, 1);
      u32 depth = 0;
      while (remainder.ScString::Strlen()) {
        u32 separator = 0, size = remainder.ScString::Strlen();
        while (separator < size && remainder.ScString::Data()[separator] != UiEditorSeparator)
          ++separator;
        if (separator < size) {
          component.FromChar(remainder.ScString::Data(), separator);
          remainder.Erase(0, separator + 1);
        } else {
#if defined(_WIN32)
          component.Erase(0, component.Strlen());
#else
          component = remainder;
          remainder.Erase(0, remainder.Strlen());
#endif
        }
        ++depth;
        ScString ancestry;
        for (u32 i = 0; i < depth; ++i)
          ancestry.Append("     ");
        char icon = 6;
        ancestry.Append(&icon, 1);
        ancestry.Append(" ");
        ancestry.Append(component.ScString::Data(), component.ScString::Strlen());
        combo->InsertString(ancestry, ++index);
      }
      combo->SetSelection(index, true);
      break;
    }
    ++index;
  }
  files->RemoveAllStrings();
  paths.Clear();
  i32 width = (i32)((u32)files->GetWindow()->GetW() - 3);
  {
    ScString wildcard("*.*");
    search->SetNewDirectoryEntryFilter(wildcard, true);
  }
  search->ReadNames(&directories, 4);
  directories.Sort();
  i32 next = 0, first_file = 0;
#if defined(_WIN32)
  ScPath current(directory);
  bool has_parent = current.Directory().ScString::Strlen() > 1;
#else
  bool has_parent = directory.ScString::Strlen() > 1;
#endif
  if (has_parent) {
    char icon = 12;
    caption.FromChar(&icon, 1);
    caption.Append(" ");
    paths.Append(caption);
    UiTextRef::FormatForEllipsis(&caption, width, font);
    files->InsertString(caption, 0);
    next = 1;
  }
  for (u32 i = 0; i < directories.entries.size; ++i) {
    char icon = 6;
    caption.FromChar(&icon, 1);
    caption.Append(" ");
    caption.Append(directories.entries.data[i]->ScString::Data());
    paths.Append(caption);
    UiTextRef::FormatForEllipsis(&caption, width, font);
    files->InsertString(caption, next++);
  }
  {
    ScString wildcard("*.BLD");
    search->SetNewDirectoryEntryFilter(wildcard, true);
  }
  if (search->GetDirectoryEntryCount())
    search->ReadNames(&names, 8);
  names.Sort();
  for (u32 i = 0; i < names.entries.size; ++i) {
    ScPath path(*names.entries.data[i]);
    caption.Erase(0, caption.Strlen());
    char icon = 18;
    caption.FromChar(&icon, 1);
    caption.Append(" ");
    ScString &name = path.FileName();
    caption.Append(name.ScString::Data(), name.ScString::Strlen());
    paths.Append(caption);
    UiTextRef::FormatForEllipsis(&caption, width, font);
    files->InsertString(caption, next);
    if (!first_file)
      first_file = next;
    ++next;
  }
  files->SetSelection(first_file, true);
  delete search;
  reading_directory = 0;
  return 1;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif


#ifdef GetObject
#undef GetObject
#endif

struct UiEditorDrive {
  ScString root;
  u8 icon;
};

// Keep the logical-drive snapshot alive across combo callbacks without recreating the original generic list machinery.
struct UiEditorDrives {
  ScArray<UiEditorDrive *> entries;
  // Retire every captured root only after all decorated labels have been sent to the combo.
  ~UiEditorDrives() {
    for (u32 i = 0; i < entries.size; ++i)
      delete entries.data[i];
  }
};

// Snapshot lowercase logical roots and their drive-type icons before inserting decorated rows in enumeration order.
// Ignore platform enumeration failure as the source caller does, leaving the combo's existing rows intact.
u8 SC3_STDCALL UiOverrideEditor::InitDriveList(WnComboControl *combo) {
#ifdef _WIN32
  UiEditorDrives drives;
  DWORD length = GetLogicalDriveStringsA(0, nullptr);
  if (!length)
    return 1;
  char *buffer = new char[(usize)length + 2];
  DWORD copied = GetLogicalDriveStringsA(length + 1, buffer);
  if (copied && copied <= length + 1) {
    u32 offset = 0, limit = copied < length - 1 ? copied : length - 1;
    while (offset < limit) {
      char *root = buffer + offset;
      char *end = static_cast<char *>(memchr(root, 0, limit - offset));
      if (!end)
        break;
      if (*root < 'a')
        *root += 32;
      UiEditorDrive *drive = new UiEditorDrive;
      drive->root.FromChar(root);
      *drives.entries.Append() = drive;
      switch (GetDriveTypeA(root)) {
      case DRIVE_REMOVABLE:
        drive->icon = 15;
        break;
      case DRIVE_CDROM:
        drive->icon = 17;
        break;
      default:
        drive->icon = 14;
        break;
      }
      offset = (u32)(end - buffer) + 1;
    }
  }
  delete[] buffer;
  for (u32 i = 0; i < drives.entries.size; ++i) {
    UiEditorDrive *drive = drives.entries.data[i];
    ScString label(reinterpret_cast<const char *>(&drive->icon), 1);
    label.Append(" ");
    u32 root_length = drive->root.ScString::Strlen();
    {
      ScString prefix(drive->root.ScString::Data(), root_length < 2 ? root_length : 2);
      label.Append(prefix.ScString::Data());
    }
    combo->InsertString(label, (i32)i);
  }
  return 1;
#else
  char icon = 14;
  ScString root(&icon, 1);
  root.Append(" /");
  combo->InsertString(root, 0);
  return 1;
#endif
}

// Create the editor's real controls and preview bitmap, configure the generic divider, then populate their contents.
// Apply city-dependent style and center using two separately captured parent views before releasing the generic hold.
u8 UiOverrideEditor::OnInit(WnWindow *window) {
  UiGenericBase *generic = nullptr;
  if (!window->QueryInterface(IID_UiGenericInterface, reinterpret_cast<void **>(&generic)))
    return 0;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  AddControl(window, kUiResourceList, kUiOverrideEditorActiveListId);
  AddControl(window, kUiResourceLabel, kUiOverrideEditorActiveListLabelId);
  AddControl(window, kUiResourceCaptionButton, kUiOverrideEditorRemoveBuildingId);
  AddControl(window, kUiResourceLabel, kUiOverrideEditorDirectoryLabelId);
  AddControl(window, kUiResourceReadOnlyCombo, kUiOverrideEditorDriveComboId);
  AddControl(window, kUiResourceListAlternate, kUiOverrideEditorFileListId);
  AddControl(window, kUiResourceStandardButton, kUiOverrideEditorAcceptId);
  AddControl(window, kUiResourceLabel, kUiOverrideEditorPreviewCaptionId);
  GxBuffer *image;
  const GxColorType format(GxColorType::Color::RGB16);
  {
    UiImageRef handle(kUiOverrideAddBuildingImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = handle.GetBuffer();
  }
  WnButton *add;
  {
    ScString caption;
    WnResources *provider = UiGetWindowResources();
    const ScRect *area = image->GetBufferArea();
    add = provider->CreateButton(kUiOverrideEditorAddBuildingId, caption, image, area, 32);
  }
  if (add) {
    add->SetCaptionStyle(0);
    FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiHeading);
    if (font)
      add->SetFont(font);
    add->SetFontColor(4, disabled_color);
    add->SetFontColor(5, third_color);
    add->SetFontColor(0, highlight_color);
    add->SetFontColor(1, first_color);
    add->SetFontColor(2, highlight_color);
    add->SetFontColor(3, second_color);
    window->ChildAdd(add->GetWindow());
    add->Release();
  }
  WnOutlineControl *outline = nullptr;
  if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
    outline->GetWindow()->SetID(kUiOverrideEditorPreviewOutlineId);
    outline->SetFlags(kWnOutlineInset | kWnOutlineFill);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    window->ChildAdd(outline->GetWindow());
    outline->Release();
  }
  UiBitmap *bitmap = new UiBitmap;
  bitmap->AddRef();
  bitmap->SetID(kUiOverrideEditorPreviewBitmapId);
  WnBitmapControl *bitmap_view = bitmap;
  if (!window->ChildAdd(bitmap_view->GetWindow())) {
    UiGetWindowManager()->RemoveWindowFromValidList(bitmap_view->GetWindow());
    bitmap->Release();
    bitmap = nullptr;
  }
  assert(bitmap);
  bitmap->Release();
  UiGenericInterface *layout = nullptr;
  if (generic->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&layout))) {
    WnWindow *close = window->GetChildWindowFromID(kUiOverrideEditorAcceptId);
    i32 extent = close ? (i32)((u32)close->GetH() + 2) : 16;
    generic->SetStyle(kUiGenericScroller, 1);
    layout->AddDivider(kUiOverrideEditorBottomDividerId, (i32)((u32)window->GetB() - (u32)extent));
    WnWindow *divider = window->GetChildWindowFromID(kUiOverrideEditorBottomDividerId);
    if (divider) {
      divider->SetH(extent);
      divider->GZWinMoveTo(11, (i32)(341u - (u32)extent));
    }
    layout->Release();
    layout = nullptr;
  }
  InitControls(window);
  generic->SetStyle(kUiGenericDraggable, UiGetCity() != nullptr);
  WnWindow *first_parent = window->GetParentWin(), *second_parent = window->GetParentWin();
  i32 parent_height = first_parent->GetH();
  i32 y = (i32)((u32)parent_height - (u32)window->GetH()) / 2;
  i32 parent_width = second_parent->GetW();
  i32 x = (i32)((u32)parent_width - (u32)window->GetW()) / 2;
  window->GZWinMoveTo(x, y);
  generic->Release();
  return 1;
}

// Apply captions and fixed child geometry before populating the active and file lists under initialized busy flags.
// Retain the resource-manager hold through the final selection callback and preserve partial-query retention.
u8 UiOverrideEditor::InitControls(WnWindow *window) {
  FtFont *font = nullptr;
  WnListControl *list = nullptr;
  WnComboControl *combo = nullptr;
  RsKey key;
  UiResourceManagerRef resources;
  directory_busy = 0;
  reading_directory = 0;
  key.type = kRsTextResourceType;
  key.group = kRsSupplementalGameTextGroup;
  key.instance = 0x13b;
  UiReplacementControls::SetCaptionFromResource(window, resources.manager, key);
  WnWindow *child = window->GetChildWindowFromID(kUiOverrideEditorAcceptId);
  if (child) {
    child->GZWinMoveTo(465, 308);
    child->SetFlag(kWnFlagReorderable, true);
    child->PullToFront();
    child->SetFlag(kWnFlagReorderable, false);
  }
  key.instance = 0x13c;
  child = window->GetChildWindowFromID(kUiOverrideEditorActiveListLabelId);
  if (child) {
    UiReplacementControls::SetCaptionFromResource(child, resources.manager, key);
    child->SetArea(20, 40, 200, 55);
  }
  child = window->GetChildWindowFromID(kUiOverrideEditorActiveListId);
  if (child)
    child->SetArea(20, 60, 200, 255);
  child = window->GetChildWindowFromID(kUiOverrideEditorPreviewBitmapId);
  if (child) {
    child->SetSize(100, 100);
    child->GZWinMoveTo(210, 61);
  }
  child = window->GetChildWindowFromID(kUiOverrideEditorPreviewOutlineId);
  if (child) {
    child->SetSize(102, 102);
    child->GZWinMoveTo(209, 60);
  }
  child = window->GetChildWindowFromID(kUiOverrideEditorPreviewCaptionId);
  if (child) {
    child->SetSize(100, 10);
    child->GZWinMoveTo(210, 168);
    WnLabelControl *label = nullptr;
    if (child->QueryInterface(IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
      label->SetAlignment(kWnLabelCenterMiddle);
      FtFont *label_font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
      if (label_font)
        label->SetFont(label_font);
      label->Release();
    }
  }
  key.instance = 0x13f;
  child = window->GetChildWindowFromID(kUiOverrideEditorAddBuildingId);
  if (child) {
    UiReplacementControls::SetCaptionFromResource(child, resources.manager, key);
    child->GZWinMoveTo(218, 193);
  }
  key.instance = 0x13e;
  child = window->GetChildWindowFromID(kUiOverrideEditorRemoveBuildingId);
  if (child) {
    UiReplacementControls::SetCaptionFromResource(child, resources.manager, key);
    i32 height = child->GetH();
    child->SetSize(120, height);
    i32 offset = (i32)(180u - (u32)child->GetW());
    child->GZWinMoveTo(offset / 2 + 20, 264);
  }
  key.instance = 0x13d;
  child = window->GetChildWindowFromID(kUiOverrideEditorDirectoryLabelId);
  if (child) {
    UiReplacementControls::SetCaptionFromResource(child, resources.manager, key);
    UiReplacementControls::AutoSizeLabelEffects(child);
    child->SetArea(320, 40, 500, 55);
  }
  child = window->GetChildWindowFromID(kUiOverrideEditorDriveComboId);
  if (child) {
    i32 bottom = (i32)((u32)child->GetH() + 60);
    child->SetArea(320, 60, 500, bottom);
  }
  child = window->GetChildWindowFromID(kUiOverrideEditorFileListId);
  if (child)
    child->SetArea(320, 90, 500, 297);
  if (window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    if (GetFont(14, &font)) {
      list->SetFont(font);
      font->Release();
      font = nullptr;
    }
    InitActiveList(list);
    list->Release();
    list = nullptr;
  }
  if (window->GetChildAs(kUiOverrideEditorDriveComboId, IID_WnComboControl, reinterpret_cast<void **>(&combo)) &&
      window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&list))) {
    InitDriveCombo(combo);
    ReadDirectory(combo, list);
    combo->Release();
    combo = nullptr;
    list->Release();
    list = nullptr;
  }
  if (!SelectActiveList(window))
    SelectFileList(window);
  return 1;
}

// Populate the active list from borrowed BAT overrides, retaining complete names and sprite pointers in sorted records.
// Fit only the displayed caption and reread the source container size after every entry and selection-list callback.
u8 UiOverrideEditor::InitActiveList(WnListControl *active) {
  ScString name;
  i32 width = (i32)((u32)active->GetWindow()->GetW() - 26);
  FtFont *font = nullptr;
  GetFont(14, &font);
  CtSchemeManager *manager = UiGetApplication()->CitySchemeManager();
  OcOverrideContainer *source = reinterpret_cast<OcOverrideContainer *>(manager->GetBATBuildings());
  u32 next = 0;
  if (source->Size()) {
    do {
      OcOverride *attributes;
      if (source->GetObject(next, &attributes) && attributes->GetName(&name)) {
        UiEditBuilding *building = new UiEditBuilding;
        building->pending = 0;
        building->name = name;
        building->lot_size = attributes->GetLotSize();
        building->sprite = attributes->GetSpriteAttrib();
        building->key = *attributes->Key();
        building->zoom = FindBestFrame(building->sprite, 100, 100);
        u32 index = 0;
        while (index < buildings.size) {
          ScString *other = &buildings.data[index]->name;
          u32 first = building->name.ScString::Strlen(), second = other->ScString::Strlen();
          int order = memcmp(building->name.ScString::Data(), other->ScString::Data(), first < second ? first : second);
          if (order < 0 || (!order && first < second))
            break;
          ++index;
        }
        buildings.Append();
        for (u32 i = buildings.size - 1; i > index; --i)
          buildings.data[i] = buildings.data[i - 1];
        buildings.data[index] = building;
        UiTextRef::FormatForEllipsis(&name, width, font);
        active->InsertString(name, (i32)index);
      }
      ++next;
    } while (next < source->Size());
  }
  return 1;
}

// Reconstruct the selected drive and ancestry rows backwards, then refresh the lists while the navigation guard is set.
// Failed interface acquisition preserves the source's sticky guard and any partial acquisition.
u8 UiOverrideEditor::DriveChange(WnWindow *window) {
  if (directory_busy)
    return 0;
  directory_busy = 1;
  WnComboControl *combo = nullptr;
  WnListControl *files = nullptr;
  if (!window->GetChildAs(kUiOverrideEditorDriveComboId, IID_WnComboControl, reinterpret_cast<void **>(&combo)) ||
      !window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&files)))
    return 0;
  ScString row, first, second, path;
  i32 index = combo->GetSelection();
  char marker;
  do {
    combo->GetStringFromIndex(index, &row);
    u32 length = row.ScString::Strlen(), icon = 0;
    while (icon < length && row.ScString::Data()[icon] == ' ')
      ++icon;
    marker = icon < length ? row.ScString::Data()[icon] : 0;
    u32 start = icon < length ? icon + 1 : length;
    ScString component(row.ScString::Data() + start, length - start);
    u32 spaces = 0;
    while (spaces < component.ScString::Strlen() && component.ScString::Data()[spaces] == ' ')
      ++spaces;
    if (spaces)
      component.Erase(0, spaces);
    if (!path.ScString::Strlen())
      path.Append(component.ScString::Data(), component.ScString::Strlen());
    else {
#if defined(_WIN32)
      component.Append("\\");
#else
      if (component.Strlen() != 1 || component.Data()[0] != '/')
        component.Append("/");
#endif
      component.Append(path.ScString::Data(), path.ScString::Strlen());
      path = component;
    }
    --index;
  } while (marker == 6);
  u32 length = path.ScString::Strlen();
  if (length && path.ScString::Data()[length - 1] != UiEditorSeparator)
    path.Append(UiEditorSeparatorString);
  directory = path;
  ReadDirectory(combo, files);
  directory_busy = 0;
  combo->Release();
  files->Release();
  return 1;
}

// Follow the selected stable full caption to enter a folder or remove the final parent-path component.
// Keep the navigation guard through list refresh and view retirement, clearing it even after failed queries.
u8 UiOverrideEditor::HandleFileListDoubleClick(WnWindow *window) {
  if (directory_busy)
    return 0;
  directory_busy = 1;
  ScString selected;
  WnComboControl *combo = nullptr;
  WnListControl *files = nullptr;
  if (window->GetChildAs(kUiOverrideEditorDriveComboId, IID_WnComboControl, reinterpret_cast<void **>(&combo)) &&
      window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&files))) {
    i32 index = 0;
    path_cursor = paths.head->next;
    if (files->GetSelection() >= 0) {
      do {
        UiEditorPath *entry = static_cast<UiEditorPath *>(path_cursor);
        path_cursor = entry->next;
        selected = entry->value;
        ++index;
      } while (index <= files->GetSelection());
    }
    if (selected.ScString::Strlen()) {
      char marker = selected.ScString::Data()[0];
      if (marker == 6) {
        selected.Erase(0, 1);
        u32 spaces = 0;
        while (spaces < selected.ScString::Strlen() && selected.ScString::Data()[spaces] == ' ')
          ++spaces;
        if (spaces)
          selected.Erase(0, spaces);
        selected.Append(UiEditorSeparatorString);
        drive.Append(UiEditorSeparatorString);
        drive.Append(selected.ScString::Data());
        directory.Append(selected.ScString::Data());
        ReadDirectory(combo, files);
      } else if (marker == 12) {
        u32 length = directory.ScString::Strlen();
        if (length > 1) {
          directory.Erase(length - 1, directory.ScString::Strlen() - length + 1);
          while (directory.ScString::Strlen() &&
              directory.ScString::Data()[directory.ScString::Strlen() - 1] != UiEditorSeparator)
            directory.Erase(directory.ScString::Strlen() - 1, 1);
        }
        ReadDirectory(combo, files);
      }
    }
    combo->Release();
    files->Release();
  }
  directory_busy = 0;
  return 1;
}

// Match the directory drive against decorated combo rows, then retry with the original resource-path fallback.
// Notify selection before rebuilding the live directory and retain the source fallback spelling without normalization.
u8 UiOverrideEditor::SetupPath(WnComboControl *combo) {
#if !defined(_WIN32)
  if (!directory.Strlen() || directory.Data()[0] != '/') {
    RsFileSystem *filesystem = UiGetFileSystem();
    const char *path = filesystem ? filesystem->AppDirectory() : nullptr;
    if (path && path[0] == '/')
      directory.FromChar(path);
    else
      directory.FromChar("/");
  }
  if (directory.Strlen() && directory.Data()[directory.Strlen() - 1] != '/')
    directory.Append("/");
  drive = directory;
  combo->SetSelection(0, true);
  return 1;
#else
  u32 length = directory.ScString::Strlen();
  ScString prefix(directory.ScString::Data(), length ? 1 : 0);
  {
    u32 start = length > 2 ? 2 : length;
    ScString remainder(directory.ScString::Data() + start, length - start);
    drive = remainder;
  }
  bool found = false;
  ScString row, letter, candidate;
  for (u32 attempt = 0; attempt < 2; ++attempt) {
    if (!attempt)
      candidate = prefix;
    else {
      RsFileSystem *filesystem = UiGetFileSystem();
      if (filesystem) {
        const char *path = filesystem->DataDirectory();
        if (path) {
          ScString data(path);
          candidate = data;
        }
      }
      candidate.Append("Res\\");
    }
    i32 index = 0;
    while (combo->GetStringFromIndex(index, &row) == 1) {
      u32 count = row.ScString::Strlen(), start = count ? 1 : 0;
      while (start < count && row.ScString::Data()[start] == ' ')
        ++start;
      letter.FromChar(row.ScString::Data() + start, start < count ? 1 : 0);
      int order = ScCompareNoCase(candidate.ScString::Data(), letter.ScString::Data(), 1);
      if (!order) {
        combo->SetSelection(index, true);
        directory = candidate;
        directory.Append(":");
        directory.Append(drive.ScString::Data(), drive.ScString::Strlen());
        found = true;
        break;
      }
      ++index;
    }
    if (found)
      break;
  }
  if (!found) {
    combo->SetSelection(0, true);
    ScString first;
    combo->GetStringFromIndex(0, &first);
    if (first.ScString::Strlen())
      first.Erase(0, 1);
    u32 spaces = 0;
    while (spaces < first.ScString::Strlen() && first.ScString::Data()[spaces] == ' ')
      ++spaces;
    if (spaces)
      first.Erase(0, spaces);
    directory = first;
  }
  return 1;
#endif
}

// Restore the saved directory or derive the original Buildings path before probing its captured path object.
// Set the WORD-converted blue highlight first, then populate drive rows and select the matching path before retirement.
u8 UiOverrideEditor::InitDriveCombo(WnComboControl *combo) {
  u32 color = UiGetGraphics()->ConvertRGBValueToNative16Bit(0, 0, 255);
  combo->SetHighlightColor(color);
  if (preferences.ready)
    directory = preferences.directory_path;
  else {
    ScString base(UiGetFileSystem()->AppDirectory());
    u32 previous = 0, last = 0;
    for (u32 i = 0; i < base.ScString::Strlen(); ++i) {
      if (base.ScString::Data()[i] == UiEditorSeparator) {
        previous = last;
        last = i;
      }
    }
    if (previous)
      base.Erase(previous + 1, base.Strlen() - previous - 1);
#if !defined(_WIN32)
    if (base.Strlen() && base.Data()[base.Strlen() - 1] != '/')
      base.Append("/");
#endif
    base.Append("Buildings");
    base.Append(UiEditorSeparatorString);
    directory = base;
  }
  ScPath path(directory);
  if (!path.Exists()) {
    ScString fallback(UiGetFileSystem()->AppDirectory());
    directory = fallback;
  }
  InitDriveList(combo);
  SetupPath(combo);
  return 1;
}

// Choose the last rotation-one frame whose registration sums fit the supplied unsigned bounds. Requery the actual
// virtual WORD frame count after each accepted frame; missing definitions or refused instances return signed minus one.
i32 SC3_STDCALL UiOverrideEditor::FindBestFrame(SpDefinition *definition, u32 width, u32 height) {
  if (!definition)
    return -1;
  SpSprite *sprite = nullptr;
  if (!definition->CreateInstance(&sprite))
    return -1;
  i32 selected = 0;
  ScRect registration = {};
  for (i32 frame = 0; frame < definition->SprAttGetTotalFrameCount(); ++frame) {
    if (!sprite->GetRegistration(frame, 1, &registration))
      break;
    if ((u32)registration.left + (u32)registration.right > width)
      break;
    if ((u32)registration.top + (u32)registration.bottom > height)
      break;
    selected = frame;
  }
  sprite->Release();
  return selected;
}

// Allocate and prepare a rotation-one sprite frame, retaining the raw draw byte and publishing only after sprite
// retirement. Source failure paths leave the caller's output untouched and do not retire the temporary buffer.
u8 SC3_STDCALL UiOverrideEditor::GetFrame(GxBuffer **out, SpDefinition *definition, i32 frame) {
  if (!definition || frame < 0)
    return 0;
  GxBuffer *buffer = nullptr;
  u8 result = 0;
  if (UiGetGraphics()->CreateBuffer(&buffer)) {
    SpSprite *sprite = nullptr;
    if (definition->CreateInstance(&sprite)) {
      ScRect registration = {};
      sprite->GetRegistration(frame, 1, &registration);
      u32 width = (u32)registration.left + (u32)registration.right;
      u32 height = (u32)registration.top + (u32)registration.bottom;
      buffer->Shutdown();
      if (buffer->Init(width, height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
              GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false) &&
          buffer->Lock(GxLockFlags::Fill)) {
        buffer->Unlock(GxLockFlags::Fill);
        if (sprite->DemandArt(frame, 1, 0)) {
          if (buffer->Lock(GxLockFlags::Bits)) {
            result = sprite->Draw(buffer, *buffer->GetBufferArea(), frame, 1, nullptr);
            buffer->Unlock(GxLockFlags::Bits);
          }
          sprite->ReleaseArt(frame, 1);
        }
      }
      sprite->Release();
      if (result)
        *out = buffer;
    }
  }
  return result;
}

// Fit the captured source against a fresh window-colored background, using a one-pixel border when scaling.
// Ignore preparation and copy replies, then retire the live incoming buffer before publishing its replacement.
u8 SC3_STDCALL UiOverrideEditor::FitBuffer(WnWindow *window, GxBuffer **buffer, u32 width, u32 height) {
  GxBuffer *source = *buffer, *target = nullptr;
  ScRect area;
  source->GetBufferArea(&area);
  ScRect clip = {0, 0, (i32)width, (i32)height};
  if (!UiGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&target)))
    return 0;
  target->Init(width, height, GxColorType(static_cast<GxColorType::Color>(4)), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  target->Fill(window->MakeFillColor(255, 255, 208), nullptr);
  if (!source->GetTransparency())
    source->SetTransparency(0);
  if (source->Width() > width || source->Height() > height) {
    ScRect destination = {1, 1, (i32)(width - 1), (i32)(height - 1)};
    if (target->Lock(GxLockFlags::Blt)) {
      source->BltStretchSrc(target, area, destination, &clip);
      target->Unlock(GxLockFlags::Blt);
    }
  } else {
    i32 left = (i32)((u32)area.left + width - (u32)area.right) / 2;
    i32 top = (i32)((u32)area.top + height - (u32)area.bottom) / 2;
    ScRect destination = {left, top, (i32)((u32)left + (u32)area.right - (u32)area.left),
        (i32)((u32)top + (u32)area.bottom - (u32)area.top)};
    if (target->Lock(GxLockFlags::Blt)) {
      source->BltSrc(target, area, destination, &clip);
      target->Unlock(GxLockFlags::Blt);
    }
  }
  (*buffer)->Release();
  *buffer = target;
  return 1;
}

// Resolve body and caption independently, borrowing both after resolution and constructing one shared empty fallback.
// Retain the complete modal result across empty-string retirement, then caption-resource and body-resource release.
i32 UiShowResourceMessage(u32 body_group, u32 body_instance, u32 caption_group, u32 caption_instance, u32 style) {
  UiTextRef body(body_instance, body_group), caption(caption_instance, caption_group);
  const ScStringView *body_text = body.GetResource(), *caption_text = caption.GetResource();
  ScString empty;
  if (!body_text)
    body_text = &empty;
  if (!caption_text)
    caption_text = &empty;
  i32 result = UiGetWindowManager()->GZMsgBox(*body_text, *caption_text, style, true, 0);
  return result;
}

// Confirm pending imports and removals, then send owned file-name and key snapshots to the actual scheme manager.
// Free the key snapshot before retiring file references, observing the live added count during the source cleanup loop.
u8 UiOverrideEditor::RenderBuildings(WnWindow *) {
  if (!added_count && !removed.size)
    return 1;
  if (UiShowResourceMessage(kRsArchitectSupplementalTextGroup, kRsArchitectSupplementalConfirmUpdateBuildingListText,
          kRsArchitectSupplementalTextGroup, 0x0f, kUiOverrideOkCancelConfirmationStyle) != kWnOkButtonId)
    return 0;
  ScStringView **files = nullptr;
  RsKey *keys = nullptr;
  if (added_count) {
    files = new ScStringView *[added_count];
    u32 next = 0;
    for (u32 i = 0; i < buildings.size; ++i) {
      UiEditBuilding *building = buildings.data[i];
      if (building->pending == 1) {
        ScString *name = new ScString(building->file);
        name->AddRef();
        files[next++] = name;
      }
    }
  }
  u32 removed_count = removed.size;
  if (removed_count) {
    keys = new RsKey[removed_count];
    for (u32 i = 0; i < removed.size; ++i)
      keys[i] = removed.data[i];
  }
  CtSchemeManager *manager = UiGetApplication()->CitySchemeManager();
  manager->UpdateBATSegment(added_count, files, removed_count, keys);
  delete[] keys;
  for (u32 i = 0; i < added_count; ++i)
    files[i]->Release();
  delete[] files;
  return 1;
}

// Confirm the captured active row before reading its live building record. Update pending/removal state before the
// list callback, erase only the pointer entry, then notify using the list's freshly reported selection.
u8 UiOverrideEditor::RemoveBuilding(WnWindow *window) {
  WnListControl *active = nullptr, *files = nullptr;
  if (window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&active)) &&
      window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&files))) {
    i32 index = active->GetSelection();
    if (UiShowResourceMessage(kRsArchitectSupplementalTextGroup, kRsArchitectSupplementalConfirmRemoveBuildingText,
            kRsArchitectSupplementalTextGroup, 0x22, kUiOverrideYesNoConfirmationStyle) == kWnYesButtonId) {
      UiEditBuilding *building = buildings.data[index];
      if (building->pending)
        --added_count;
      else
        *removed.Append() = building->key;
      active->RemoveString(index);
      for (u32 i = (u32)index + 1; i < buildings.size; ++i)
        buildings.data[i - 1] = buildings.data[i];
      --buildings.size;
      active->SetSelection(active->GetSelection(), true);
    }
    active->Release();
    files->Release();
  }
  return 1;
}

// Keep the editor's scoped architect lookup separate from its explicit extra reference during each preview load.
// This native owner replaces the original local service-pointer machinery without changing its two-release ordering.
class UiEditorPreviewService {
public:
  // Initialize the output before the actual service lookup; the lookup reply does not replace its published pointer.
  UiEditorPreviewService() : value(nullptr) {
    UiGetFramework()->GetSystemService(kBdManagerServiceId, IID_BdManagerView, reinterpret_cast<void **>(&value));
  }
  // Retire the lookup's reference after the explicit preview reference and any following bitmap/list work.
  ~UiEditorPreviewService() {
    if (value)
      value->Release();
  }
  BdManagerView *value;
};

// Select an active record, enabling removal and obtaining a file preview or the stored sprite frame as appropriate.
// Publish the fitted image before releasing the live preview, then retire both queried list views in source order.
u8 UiOverrideEditor::HandleActiveListSelected(WnWindow *window) {
  active_selected = 1;
  WnListControl *active = nullptr, *files = nullptr;
  UpdateRemoveButton(window, false);
  HideLotSizeLabel(window);
  if (window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&active)) &&
      window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&files))) {
    if (active->GetSelection() >= 0) {
      UpdateRemoveButton(window, true);
      UiEditBuilding *building = buildings.data[active->GetSelection()];
      UpdateLotSizeLabel(window, building->lot_size);
      if (building->pending) {
        UiEditorPreviewService service;
        BdManagerView *manager = service.value;
        if (manager) {
          manager->AddRef();
          if (manager->LoadPreviewDocument(&building->file, &preview, &document) && document)
            document->Release();
          manager->Release();
        }
      } else if (building->sprite)
        GetFrame(&preview, building->sprite, building->zoom);
    }
    if (preview)
      FitBuffer(window, &preview, 100, 100);
    WnBitmapControl *bitmap = nullptr;
    if (window->GetChildAs(kUiOverrideEditorPreviewBitmapId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)) &&
        bitmap) {
      bitmap->SetImage(preview);
      bitmap->Release();
    }
    ScRelease(&preview);
    active->Release();
    files->Release();
  }
  return 1;
}

// Follow the live file-list selection through stable directory entries and load metadata only for the file marker12.
// Keep the scoped architect reference through preview publication and preserve the source's retained active-list view.
u8 UiOverrideEditor::HandleFileListSelected(WnWindow *window) {
  ScString stem, selected;
  file_selected = 1;
  UiEditorPreviewService service;
  BdManagerView *manager = service.value;
  UpdateAddButton(window, false);
  HideLotSizeLabel(window);
  WnListControl *files = nullptr, *active = nullptr;
  if (window->GetChildAs(kUiOverrideEditorFileListId, IID_WnListControl, reinterpret_cast<void **>(&files)) &&
      window->GetChildAs(kUiOverrideEditorActiveListId, IID_WnListControl, reinterpret_cast<void **>(&active))) {
    if (files->GetSelection() >= 0) {
      i32 index = 0;
      path_cursor = paths.head->next;
      if (files->GetSelection() >= 0) {
        do {
          UiEditorPath *entry = static_cast<UiEditorPath *>(path_cursor);
          path_cursor = entry->next;
          selected = entry->value;
          ++index;
        } while (index <= files->GetSelection());
      }
      if (selected.ScString::Strlen() && selected.ScString::Data()[0] == 0x12) {
        stem.FromChar(selected.ScString::Data() + 1, selected.ScString::Strlen() - 1);
        u32 spaces = 0;
        while (spaces < stem.ScString::Strlen() && stem.ScString::Data()[spaces] == ' ')
          ++spaces;
        if (spaces)
          stem.Erase(0, spaces);
        {
          ScString path(directory);
          path.Append(stem.ScString::Data(), stem.ScString::Strlen());
          path.Append(".BLD");
          file = path;
        }
        if (manager) {
          manager->AddRef();
          if (manager->LoadPreviewDocument(&file, &preview, &document) && document) {
            UpdateAddButton(window, true);
            document->GetText08(&selected_name);
            document->GetText1C(&selected_architect);
            lot_size = (u8)document->GetByte04();
            document->Release();
            UpdateLotSizeLabel(window, lot_size);
          }
          manager->Release();
        }
      }
    }
    if (preview)
      FitBuffer(window, &preview, 100, 100);
    WnBitmapControl *bitmap = nullptr;
    if (window->GetChildAs(kUiOverrideEditorPreviewBitmapId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)) &&
        bitmap) {
      bitmap->SetImage(preview);
      bitmap->Release();
    }
    ScRelease(&preview);
    files->Release();
  }
  return 1;
}
