#include "ui/query_dialogs.h"

#include <assert.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/sc3_font_manager.h"
#include "geometry/api.h"
#include "occupants/animation_manager.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/city_window.h"
#include "sprites/sprite.h"
#include "sprites/view_camera.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"

#ifdef CreateDialog
#undef CreateDialog
#endif
#ifdef GetObject
#undef GetObject
#endif

// Empty query rows sort by category: the later separators bracket construction-date and monthly-cost rows.
enum UiQuerySeparatorCategory : u32 {
  kUiQueryPrimarySectionSeparator = 0x10000,
  kUiQueryBeforeConstructionDateSeparator = 0x80000,
  kUiQueryBeforeMonthlyCostSeparator = 0x400000,
  kUiQueryAfterMonthlyCostSeparator = 0x1000000,
};

// The preview replacement button is positioned above the lower edge by its own height and this signed five-pixel gap.
enum UiPreviewButtonLayout : u32 {
  kUiPreviewReplaceButtonNegativeGap = 0xfffffffb,
};

// Untargeted queries select terrain, water or landfill formats; the option bit gates checkbox creation and layout.
enum UiQueryFormat : i32 {
  kUiTerrainQueryFormat = 0x5bee0,
  kUiWaterQueryFormat = 0x5a020,
  kUiLandfillQueryFormat = 0x30020,
  kUiQueryShowOptionFlag = 0x2000000,
};

// Preview layout compares its dimensions using the exact rational representation of the original float 1.2.
enum UiPreviewAspect : i32 {
  kUiPreviewAspectDenominator = 0x400000,
  kUiPreviewAspectNumerator = 0x4ccccd,
};

// Availability bits track which original and replacement previews can currently be selected.
enum UiPreviewAvailability : u32 {
  kUiPreviewReplacementAvailable = 1,
  kUiPreviewOriginalAvailable = 2,
};

// Preview selection identifies the original or replacement image independently of its availability flags.
enum UiPreviewSelection : u32 {
  kUiPreviewOriginalChoice = 1,
  kUiPreviewReplacementChoice = 2,
};

UiLayerDialog::UiLayerDialog() : references(0), window(nullptr), changed(0), control(nullptr) {
}

UiLayerDialog::~UiLayerDialog() {
}

// Construct the real layer dialog and return its primary interface without adding a factory reference.
ScUnknown *UiCreateClassC2F14704() {
  return static_cast<UiLayerDialogInterface *>(new UiLayerDialog);
}

// Publish the primary layer-dialog or secondary callback view, then retain the shared owner.
bool UiLayerDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiLayerDialogInterface);
  HANDLE_QUERY_INTERFACE(IID_UiLayerDialogInterface, UiLayerDialogInterface);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Hold the module for each acquired reference and return the live count after the callback.
u32 UiLayerDialog::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Drop the matching module hold and destroy the owner once its count reaches zero.
u32 UiLayerDialog::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Acquire the generic view once, configure its original styles and attach the callback identity.
u8 UiLayerDialog::CreateDialog() {
  if (window)
    return 1;
  window = nullptr;
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result && window) {
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericScroller, 0);
    window->GetWindow()->PrivateBuffer(true);
    return window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  return result;
}

// Borrow the primary window from the generic view after dialog creation.
WnWindow *UiLayerDialog::GetAssociatedIGZWin() const {
  return window->GetWindow();
}

// Read the checked state only for the six city layer identifiers.
u8 UiLayerDialog::GetViewItem(u32 identifier) {
  if (identifier < kUiLayerTransportCheckId || identifier > kUiLayerZonesCheckId)
    return 0;
  return GetCheck(identifier);
}

// Change a checked state only for the six city layer identifiers.
u8 UiLayerDialog::SetViewItem(u32 identifier, bool enabled) {
  if (identifier < kUiLayerTransportCheckId || identifier > kUiLayerZonesCheckId)
    return 0;
  return SetCheck(identifier, enabled);
}

// Select the first checked alternate view, falling back to the ordinary layer view.
u32 UiLayerDialog::GetView() {
  if (GetCheck(kUiUndergroundLayerSectionId))
    return kUiUndergroundLayerSectionId;
  if (GetCheck(kUiDataLayerSectionId))
    return kUiDataLayerSectionId;
  return kUiAboveGroundLayerSectionId;
}

u8 UiLayerDialog::SetView(u32 identifier) {
  return GroupSwitch(identifier);
}

u32 UiLayerDialog::GetViewOption(u32 identifier) {
  return GetValue(identifier);
}

u8 UiLayerDialog::SetViewOption(u32 identifier, u32 value) {
  return SetValue(identifier, value);
}

u8 UiLayerDialog::OnShutdown(WnWindow *recipient) {
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->RemoveNotification(recipient, kWnMsg_ViewInfoUpdated);
  ScRelease(&control);
  ScRelease(&window);
  return 1;
}

u8 UiLayerDialog::ItemClick(u32) {
  changed = 1;
  return 1;
}

// Apply accepted settings, notify the dialog owner and post the original modal completion message.
u8 UiLayerDialog::EndDialog(WnWindow *recipient, u32 result) {
  if (!result)
    Apply();
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    ScMessage message(
        kWnMsg_CityLayersDialogClosed, reinterpret_cast<uptr>(static_cast<UiLayerDialogInterface *>(this)), 0, 0);
    server->MessageSend(message);
  }
  WnWindowManager *manager = nullptr;
  UiGetFramework()->GetSystemService(
      kWnWindowManagerServiceId, IID_WnWindowManager, reinterpret_cast<void **>(&manager));
  if (manager) {
    manager->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(recipient), result);
    manager->Release();
  }
  return 1;
}

u8 UiLayerDialog::Apply() {
  ScMessage message(
      kWnMsg_UpdateCityViewLayers, reinterpret_cast<uptr>(static_cast<UiLayerDialogInterface *>(this)), 0, 0);
  UiGetMessageServer()->MessageSend(message);
  changed = 0;
  return 1;
}

// Restore the ordinary view and enable all six layer checks in source order.
u8 UiLayerDialog::ResetToDefault() {
  GroupSwitch(kUiAboveGroundLayerSectionId);
  for (u32 identifier = kUiLayerTransportCheckId; identifier <= kUiLayerZonesCheckId; ++identifier)
    SetCheck(identifier, true);
  return 1;
}

// Clear all group checks and disable their controls before enabling the selected group.
u8 UiLayerDialog::GroupSwitch(u32 identifier) {
  changed = 1;
  SetCheck(kUiAboveGroundLayerSectionId, false);
  SetCheck(kUiUndergroundLayerSectionId, false);
  SetCheck(kUiDataLayerSectionId, false);
  for (u32 item = kUiLayerTransportCheckId; item <= kUiLayerZonesCheckId; ++item)
    Enable(item, false);
  Enable(kUiUndergroundLayerOptionGroupId, false);
  Enable(kUiDataLayerOptionGroupId, false);
  switch (identifier) {
  case kUiAboveGroundLayerSectionId:
    for (u32 item = kUiLayerTransportCheckId; item <= kUiLayerZonesCheckId; ++item)
      Enable(item, true);
    SetCheck(kUiAboveGroundLayerSectionId, true);
    break;
  case kUiUndergroundLayerSectionId:
    Enable(kUiUndergroundLayerOptionGroupId, true);
    SetCheck(kUiUndergroundLayerSectionId, true);
    break;
  case kUiDataLayerSectionId:
    Enable(kUiDataLayerOptionGroupId, true);
    SetCheck(kUiDataLayerSectionId, true);
    break;
  }
  return 1;
}

// Query the named button, request its on/off state and release the acquired interface.
u8 UiLayerDialog::SetCheck(u32 identifier, bool enabled) {
  WnButton *button = nullptr;
  if (!window->GetWindow()->GetChildAs(identifier, IID_WnButton, reinterpret_cast<void **>(&button)) || !button)
    return 0;
  if (enabled)
    button->ToggleOn();
  else
    button->ToggleOff();
  button->Release();
  return 1;
}

u8 UiLayerDialog::SetPress(u32 identifier, bool enabled) {
  return SetCheck(identifier, enabled);
}

// Read the named button's checked state and release its acquired interface.
u8 UiLayerDialog::GetCheck(u32 identifier) {
  WnButton *button = nullptr;
  if (!window->GetWindow()->GetChildAs(identifier, IID_WnButton, reinterpret_cast<void **>(&button)) || !button)
    return 0;
  u8 result = button->IsChecked();
  button->Release();
  return result;
}

// Apply the enabled window flag when the child query succeeds; the original helper always returns zero.
u8 UiLayerDialog::Enable(u32 identifier, bool enabled) {
  WnWindow *child = nullptr;
  if (window->GetWindow()->GetChildAs(identifier, IID_WnWindow, reinterpret_cast<void **>(&child))) {
    child->SetFlag(kWnFlagEnabled, enabled);
    child->Release();
  }
  return 0;
}

// Resolve an alternate view's option group, set its value and release the acquired interface.
u8 UiLayerDialog::SetValue(u32 identifier, u32 value) {
  u32 child_id = identifier == kUiUndergroundLayerSectionId ? kUiUndergroundLayerOptionGroupId
      : identifier == kUiDataLayerSectionId                 ? kUiDataLayerOptionGroupId
                                                            : 0;
  WnOptionControl *options = nullptr;
  if (!window->GetWindow()->GetChildAs(child_id, IID_WnOptionControl, reinterpret_cast<void **>(&options)) || !options)
    return 0;
  options->SetValue(value);
  options->Release();
  return 1;
}

// Resolve an alternate view's option group, read its value and release the acquired interface.
u32 UiLayerDialog::GetValue(u32 identifier) {
  u32 child_id = identifier == kUiUndergroundLayerSectionId ? kUiUndergroundLayerOptionGroupId
      : identifier == kUiDataLayerSectionId                 ? kUiDataLayerOptionGroupId
                                                            : 0;
  WnOptionControl *options = nullptr;
  if (!window->GetWindow()->GetChildAs(child_id, IID_WnOptionControl, reinterpret_cast<void **>(&options)) || !options)
    return 0;
  u32 value = options->GetValue();
  options->Release();
  return value;
}

// Create a checkbox from the real resource provider, attach it, then fit, position and check it.
u8 UiLayerDialog::AddCheck(u32 identifier, u32 x, u32 y, u32 text_instance) {
  UiWindowResourcesRef resources;
  if (!resources.provider)
    return 0;
  u8 result = 0;
  UiTextRef text(text_instance, kRsGameTextGroup);
  WnButton *button = nullptr;
  if (resources.provider->CreateObject(kUiResourceEightStateButton, IID_WnButton, reinterpret_cast<void **>(&button))) {
    button->GetWindow()->SetID(identifier);
    if (text.GetResource()) {
      button->GetWindow()->SetCaption(*text.GetResource());
    } else {
      ScString value(text.GetText());
      ScString first(value);
      ScString second(first);
      button->GetWindow()->SetCaption(second);
    }
    WnWindow *parent = window->GetWindow();
    result = parent->ChildAdd(button->GetWindow());
    button->Fit();
    button->GetWindow()->GZWinMoveTo((i32)x, (i32)y);
    button->SetChecked(true);
    button->Release();
  }
  return result;
}

u8 UiLayerDialog::BeginOptionGroup(u32 identifier) {
  ScRelease(&control);
  UiWindowResourcesRef resources;
  if (!resources.provider ||
      !resources.provider->CreateObject(
          kUiResourceOptionGroup, IID_WnOptionControl, reinterpret_cast<void **>(&control)))
    return 0;
  control->GetWindow()->SetID(identifier);
  return 1;
}

// Resolve one option caption, add its button and place it within the current group.
u8 UiLayerDialog::AddOption(u32 identifier, u32 x, u32 y, u32 text_instance) {
  if (!control)
    return 0;
  ScString caption;
  {
    UiTextRef resource(text_instance, kRsGameTextGroup);
    ScString value(resource.GetText());
    ScString first(value);
    ScString second(first);
    caption = second;
  }
  control->AddButton(identifier, caption, 0, nullptr, nullptr);
  control->MoveButton(identifier, (i32)x, (i32)y);
  return 1;
}

// Attach the option group, set its initial area and size it before dropping the temporary owner.
u8 UiLayerDialog::EndOptionGroup() {
  if (!control)
    return 0;
  control->GetWindow()->SetFlag(kWnFlagReorderable, false);
  WnWindow *parent = window->GetWindow();
  parent->ChildAdd(control->GetWindow());
  control->GetWindow()->SetArea(0, 0, 1, 1);
  control->AutoSize();
  control->Release();
  control = nullptr;
  return 1;
}

// Create the resource-backed rectangle, assign its ID and attach it to the supplied parent.
u8 UiLayerDialog::AddRect(WnWindow *parent, u32 identifier) {
  UiWindowResourcesRef resources;
  if (!resources.provider)
    return 0;
  WnWindow *child = nullptr;
  if (!resources.provider->CreateObject(kUiResourceFlatPanel, IID_WnWindow, reinterpret_cast<void **>(&child)))
    return 0;
  child->SetID(identifier);
  u8 result = parent->ChildAdd(child);
  child->Release();
  return result;
}

// Create the layer dialog's requested action, view-selection or city-flag control.
u8 UiLayerDialog::AddButton(WnWindow *parent, u32 identifier, u32 group, u32 instance, bool selected) {
  UiWindowResourcesRef resources;
  if (!resources.provider)
    return 0;
  u8 result = 0;
  if (identifier == kUiLayerAcceptButtonId) {
    WnWindow *child = nullptr;
    if (resources.provider->CreateObject(kUiResourceStandardButton, IID_WnWindow, reinterpret_cast<void **>(&child))) {
      child->SetID(kUiLayerAcceptButtonId);
      result = parent->ChildAdd(child);
      child->Release();
    }
  } else if (identifier == kUiLayerTerrainGridButtonId) {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(kRsLayerDialogButtonImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = image.GetBuffer();
    if (buffer) {
      u16 pixel = buffer->ConvertRGBValueToNative16Bit(255, 0, 255);
      buffer->SetTransparency(pixel);
      WnButton *button = nullptr;
      if (UiGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button))) {
        UiTextRef text(instance, group);
        SpViewInterface *view = UiGetCurrentView();
        bool checked = view ? view->GetCityViewFlag(kSpCityViewTerrainGrid) : true;
        button->SetID(kUiLayerTerrainGridButtonId);
        if (text.GetResource())
          button->SetTipText(*text.GetResource());
        button->SetTipPlacementType(kWnTipLeftMiddle);
        const ScRect *image_area = buffer->GetBufferArea();
        button->SetStyle(kWnButtonToggleFixed, buffer, image_area);
        button->SetButtonFlag(kWnButtonShowText, false);
        button->SetButtonFlag(kWnButtonChecked, checked);
        button->SetButtonFlag(kWnButtonShowHint, true);
        result = parent->ChildAdd(button->GetWindow());
        button->Release();
      }
    }
  } else {
    ScString caption;
    UiTextRef text(instance, group);
    if (text.GetResource())
      caption = *text.GetResource();
    WnWindow *child = nullptr;
    u32 kind = selected ? kUiResourceToggleButton : kUiResourceCaptionButton;
    if (resources.provider->CreateObject(kind, IID_WnWindow, reinterpret_cast<void **>(&child))) {
      child->SetID(identifier);
      child->SetCaption(caption);
      result = parent->ChildAdd(child);
      child->Release();
    }
  }
  return result;
}

// Build the layer checks, option groups, section frames and action controls before subscribing to view changes.
u8 UiLayerDialog::OnInit(WnWindow *recipient) {
  if (!window)
    return 0;
  UiWindowResourcesRef resources;
  if (!resources.provider)
    return 0;
  ScString caption;
  {
    UiTextRef resource(kRsGameViewCityLayersText, kRsGameTextGroup);
    ScString value(resource.GetText());
    ScString first(value);
    ScString second(first);
    caption = second;
  }
  recipient->SetCaption(caption);
  ScRect area{32, 32, 328, 368};
  recipient->SetArea(area);
  AddCheck(kUiLayerTransportCheckId, 15, 72, 0x26e);
  AddCheck(kUiLayerPowerLinesCheckId, 15, 88, 0x26f);
  AddCheck(kUiLayerFloraCheckId, 15, 104, 0x270);
  AddCheck(kUiLayerZonedBuildingsCheckId, 15, 120, 0x271);
  AddCheck(kUiLayerOtherBuildingsCheckId, 15, 136, 0x272);
  AddCheck(kUiLayerZonesCheckId, 15, 152, 0x273);
  BeginOptionGroup(kUiUndergroundLayerOptionGroupId);
  AddOption(kUiLayerSubwayOptionId, 0, 0, 0x274);
  AddOption(kUiLayerWaterPipesOptionId, 0, 16, 0x275);
  EndOptionGroup();
  BeginOptionGroup(kUiDataLayerOptionGroupId);
  AddOption(kUiLayerAuraOptionId, 0, 0, 0x276);
  AddOption(kUiLayerCrimeOptionId, 0, 16, 0x277);
  AddOption(kUiLayerDensityOptionId, 0, 32, 0x278);
  AddOption(kUiLayerPowerOptionId, 0, 48, 0x279);
  AddOption(kUiLayerFlammabilityOptionId, 0, 64, 0x27a);
  AddOption(kUiLayerLandValueOptionId, 0, 80, 0x27b);
  AddOption(kUiLayerPollutionOptionId, 0, 96, 0x27c);
  AddOption(kUiLayerTrafficOptionId, 0, 112, 0x27d);
  EndOptionGroup();
  AddRect(recipient, kUiAboveGroundLayerOutlineId);
  AddRect(recipient, kUiUndergroundLayerOutlineId);
  AddRect(recipient, kUiDataLayerOutlineId);
  AddRect(recipient, kUiLayerFooterOutlineId);
  AddButton(recipient, kUiAboveGroundLayerSectionId, kRsGameTextGroup, 0x26a, true);
  AddButton(recipient, kUiUndergroundLayerSectionId, kRsGameTextGroup, 0x280, true);
  AddButton(recipient, kUiDataLayerSectionId, kRsGameTextGroup, 0x26b, true);
  AddButton(recipient, kUiLayerDefaultButtonId, kRsGameTextGroup, 0x26c, false);
  AddButton(recipient, kUiLayerApplyButtonId, kRsGameTextGroup, 0x26d, false);
  AddButton(recipient, kUiLayerTerrainGridButtonId, kRsGuiTextGroup, 0xa5, true);
  AddButton(recipient, kUiLayerAcceptButtonId, 0, 0, false);
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->AddNotification(recipient, kWnMsg_ViewInfoUpdated);
    ScMessage message(
        kWnMsg_InitCityViewLayers, reinterpret_cast<uptr>(static_cast<UiLayerDialogInterface *>(this)), 0, 0);
    server->MessageSend(message);
  }
  changed = 0;
  return 1;
}

// Fit the three layer sections and combine their measured extents into the original window limits.
u8 UiLayerDialog::OnGetMinMaxInfo(WnWindow *recipient, ScRect *limits) const {
  const u32 items[] = {kUiLayerTransportCheckId, kUiLayerPowerLinesCheckId, kUiLayerFloraCheckId,
      kUiLayerZonedBuildingsCheckId, kUiLayerOtherBuildingsCheckId, kUiLayerZonesCheckId};
  UiLayerSection section{kUiAboveGroundLayerSectionId, kUiAboveGroundLayerOutlineId, 0, items, 6};
  ScRect measured;
  *limits = {0, 0, 640, 480};
  UiLayerSectionMeasure(recipient, section, &measured);
  i32 width = measured.right - measured.left + 4;
  if (limits->left < width)
    limits->left = width;
  limits->top += measured.bottom - measured.top + 4;
  section = {kUiUndergroundLayerSectionId, kUiUndergroundLayerOutlineId, kUiUndergroundLayerOptionGroupId, nullptr, 0};
  UiLayerSectionMeasure(recipient, section, &measured);
  width = measured.right - measured.left + 4;
  if (limits->left < width)
    limits->left = width;
  limits->top += measured.bottom - measured.top;
  section = {kUiDataLayerSectionId, kUiDataLayerOutlineId, kUiDataLayerOptionGroupId, nullptr, 0};
  UiLayerSectionMeasure(recipient, section, &measured);
  width = limits->left + measured.right - measured.left;
  if (limits->left < width)
    limits->left = width;
  i32 height = measured.bottom - measured.top;
  if (limits->top < height)
    limits->top = height;
  limits->top += 76;
  return 1;
}

// Arrange the three sections and the bottom action controls within the supplied dialog area.
u8 UiLayerDialog::OnAreaChanged(WnWindow *recipient, const ScRect &area) {
  const u32 items[] = {kUiLayerTransportCheckId, kUiLayerPowerLinesCheckId, kUiLayerFloraCheckId,
      kUiLayerZonedBuildingsCheckId, kUiLayerOtherBuildingsCheckId, kUiLayerZonesCheckId};
  UiLayerSection section{kUiAboveGroundLayerSectionId, kUiAboveGroundLayerOutlineId, 0, items, 6};
  ScRect first, second, third;
  i32 half_width = (area.right - area.left - 4) / 2;
  UiLayerSectionMeasure(recipient, section, &first);
  if (first.right - first.left < half_width)
    first.right = first.left + half_width;
  first.right += area.left - first.left;
  first.left = area.left;
  first.bottom += area.top - first.top;
  first.top = area.top;
  UiLayerSectionPlace(recipient, section, first);
  section = {kUiUndergroundLayerSectionId, kUiUndergroundLayerOutlineId, kUiUndergroundLayerOptionGroupId, nullptr, 0};
  UiLayerSectionMeasure(recipient, section, &second);
  if (second.right - second.left < half_width)
    second.right = second.left + half_width;
  second.right += area.left - second.left;
  second.left = area.left;
  second.bottom += first.bottom + 4 - second.top;
  second.top = first.bottom + 4;
  UiLayerSectionPlace(recipient, section, second);
  section = {kUiDataLayerSectionId, kUiDataLayerOutlineId, kUiDataLayerOptionGroupId, nullptr, 0};
  UiLayerSectionMeasure(recipient, section, &third);
  if (third.right - third.left < half_width)
    third.right = third.left + half_width;
  third.bottom += area.top - third.top;
  third.top = area.top;
  third.right += area.left + half_width + 4 - third.left;
  third.left = area.left + half_width + 4;
  UiLayerSectionPlace(recipient, section, third);

  i32 height = area.bottom - area.top, bottom_top = height - 76, width = area.right - area.left;
  WnWindow *outline = recipient->GetChildWindowFromID(kUiLayerFooterOutlineId);
  if (outline)
    outline->SetArea(area.left, area.top + bottom_top, area.left + width, area.top + height);
  i32 right_width = 0;
  WnWindow *child = recipient->GetChildWindowFromID(kUiLayerTerrainGridButtonId);
  i32 candidate = child ? child->GetW() : 0;
  if (candidate > right_width)
    right_width = candidate;
  child = recipient->GetChildWindowFromID(kUiLayerAcceptButtonId);
  candidate = child ? child->GetW() : 0;
  if (candidate > right_width)
    right_width = candidate;
  i32 y = 4;
  child = recipient->GetChildWindowFromID(kUiLayerTerrainGridButtonId);
  if (child) {
    i32 x = area.left + (right_width - child->GetW()) / 2 - right_width + width - 4;
    child->GZWinMoveTo(x, area.top + bottom_top + 4);
    y = child->GetH() + 4;
  }
  child = recipient->GetChildWindowFromID(kUiLayerAcceptButtonId);
  if (child) {
    i32 x = area.left + (right_width - child->GetW()) / 2 - right_width + width - 4;
    child->GZWinMoveTo(x, area.top + bottom_top + y);
  }
  i32 left_width = 0;
  child = recipient->GetChildWindowFromID(kUiLayerApplyButtonId);
  candidate = child ? child->GetW() : 0;
  if (candidate > left_width)
    left_width = candidate;
  child = recipient->GetChildWindowFromID(kUiLayerDefaultButtonId);
  candidate = child ? child->GetW() : 0;
  if (candidate > left_width)
    left_width = candidate;
  y = 4;
  child = recipient->GetChildWindowFromID(kUiLayerApplyButtonId);
  if (child) {
    i32 bottom = area.top + bottom_top + 4 + child->GetH();
    child->SetArea(area.left + 4, area.top + bottom_top + 4, area.left + 4 + left_width, bottom);
    y = child->GetH() + 4;
  }
  child = recipient->GetChildWindowFromID(kUiLayerDefaultButtonId);
  if (child) {
    i32 top = area.top + bottom_top + y, bottom = top + child->GetH();
    child->SetArea(area.left + 4, top, area.left + 4 + left_width, bottom);
    child->GetH();
  }
  return 1;
}

static u8 UiLayerInteraction(u32 category, u32 event) {
  ApGameInterface *application = UiGetApplication();
  if (!application)
    return 0;
  ApBoxX *box = application->GetBoxX();
  if (!box)
    return 0;
  box->Event(category, event, 0, 0, 0);
  return 1;
}

// Borrow the current city view through the application's live city-window interface.
static SpViewInterface *UiLayerCurrentView() {
  ApGameInterface *application = UiGetApplication();
  if (!application)
    return nullptr;
  SpCityWindowView *city_window = application->GetISC3WinCityView();
  return city_window ? city_window->GetCurrentView() : nullptr;
}

// Dispatch layer-dialog commands, window lifecycle and city-view flag notifications.
bool UiLayerDialog::DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Init)
    return OnInit(recipient) != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown(recipient) != 0;
  if (message == kWnMsg_ClientConstraints)
    return first && OnGetMinMaxInfo(recipient, reinterpret_cast<ScRect *>(first));
  if (message == kWnMsg_ClientLayout)
    return first && OnAreaChanged(recipient, *reinterpret_cast<const ScRect *>(first));
  if (message == kWnMsg_KeyDown)
    return first == 27 && EndDialog(recipient, 1);
  if (message == kWnMsg_ViewInfoUpdated) {
    if (first == kSpViewUpdate_Snapshot) {
      u32 flags = 0;
      reinterpret_cast<SpViewInfo *>(second)->GetFlags(&flags);
      bool enabled = (flags & kSpCityViewTerrainGrid) != 0;
      WnButton *button = nullptr;
      if (recipient->GetChildAs(kUiLayerTerrainGridButtonId, IID_WnButton, reinterpret_cast<void **>(&button))) {
        if (button->GetButtonFlag(kWnButtonChecked) != enabled)
          button->SetButtonFlag(kWnButtonChecked, enabled);
        button->Release();
      }
    }
    return true;
  }
  if (message != kWnMsg_Command)
    return false;
  if (first == kWnCommandOptionSelectionChanged) {
    UiLayerInteraction(1, 13);
    return true;
  }
  if (first != 1)
    return false;
  u32 identifier = (u32)second;
  if (identifier >= kUiAboveGroundLayerSectionId && identifier <= kUiDataLayerSectionId) {
    UiLayerInteraction(1, 12);
    return GroupSwitch(identifier) != 0;
  }
  switch (identifier) {
  case kUiLayerDefaultButtonId:
    UiLayerInteraction(53, 0);
    return ResetToDefault() != 0;
  case kUiLayerApplyButtonId:
    UiLayerInteraction(53, 0);
    return Apply() != 0;
  case kUiLayerAcceptButtonId:
    UiLayerInteraction(52, 0);
    return EndDialog(recipient, 0) != 0;
  case kUiLayerTerrainGridButtonId: {
    UiLayerInteraction(1, 12);
    SpViewInterface *view = UiLayerCurrentView();
    if (view) {
      WnButton *button = nullptr;
      if (recipient->GetChildAs(kUiLayerTerrainGridButtonId, IID_WnButton, reinterpret_cast<void **>(&button))) {
        bool enabled = button->GetButtonFlag(kWnButtonChecked);
        view->SetCityViewFlag(kSpCityViewTerrainGrid, enabled, true);
        button->Release();
      }
    }
    return true;
  }
  case kUiFrameCloseButtonId:
    UiLayerInteraction(52, 0);
    return EndDialog(recipient, 1) != 0;
  default:
    UiLayerInteraction(1, 13);
    return ItemClick(identifier) != 0;
  }
}

// Fit the named button when its interface is available, then release the acquired view.
bool UiLayerFitButton(WnWindow *parent, u32 identifier) {
  if (!parent)
    return false;
  WnButton *button = nullptr;
  if (!parent->GetChildAs(identifier, IID_WnButton, reinterpret_cast<void **>(&button)))
    return false;
  button->Fit();
  button->Release();
  return true;
}

// Measure a section from its fitted title and either its option group or its individual items.
bool UiLayerSectionMeasure(WnWindow *parent, const UiLayerSection &section, ScRect *out) {
  if (!parent)
    return false;
  i32 width = 0, height = 0;
  UiLayerFitButton(parent, section.title);
  WnWindow *child = parent->GetChildWindowFromID(section.title);
  if (child) {
    i32 candidate = child->GetW() + 4;
    if (candidate > width)
      width = candidate;
  }
  if (section.option) {
    child = parent->GetChildWindowFromID(section.option);
    if (child) {
      i32 candidate = child->GetW() + 8;
      if (candidate > width)
        width = candidate;
    }
  } else {
    for (u32 i = 0; i < section.item_count; ++i) {
      child = parent->GetChildWindowFromID(section.items[i]);
      i32 candidate = child ? child->GetW() + 8 : 0;
      if (candidate > width)
        width = candidate;
    }
  }
  child = parent->GetChildWindowFromID(section.title);
  if (child)
    height = child->GetH() + 4;
  if (section.option) {
    child = parent->GetChildWindowFromID(section.option);
    if (child)
      height += child->GetH();
  } else {
    for (u32 i = 0; i < section.item_count; ++i) {
      child = parent->GetChildWindowFromID(section.items[i]);
      if (child)
        height += child->GetH();
    }
  }
  *out = {0, 0, width, height};
  return width != 0 && height != 0;
}

// Place the title, outline and either the option group or each item inside the measured section.
u8 UiLayerSectionPlace(WnWindow *parent, const UiLayerSection &section, const ScRect &area) {
  if (!parent)
    return 0;
  i32 top = 0;
  WnWindow *title = parent->GetChildWindowFromID(section.title);
  if (title) {
    i32 bottom = title->GetH() + area.top + 2;
    title->SetArea(area.left + 2, area.top + 2, area.right - 2, bottom);
    top = title->GetH() + 4;
  }
  WnWindow *outline = parent->GetChildWindowFromID(section.outline);
  if (outline)
    outline->SetArea(area.left, area.top, area.right, area.top + top);
  if (section.option) {
    WnWindow *option = parent->GetChildWindowFromID(section.option);
    if (option)
      option->GZWinMoveTo(area.left + 8, area.top + top);
  } else {
    i32 y = area.top + top;
    for (u32 i = 0; i < section.item_count; ++i) {
      WnWindow *child = parent->GetChildWindowFromID(section.items[i]);
      if (child) {
        child->GZWinMoveTo(area.left + 8, y);
        y += child->GetH();
      }
    }
  }
  return 1;
}

class UiPreviewGraphicsRef {
public:
  UiPreviewGraphicsRef();
  ~UiPreviewGraphicsRef();
  GxGraphics *graphics;
};

UiPreviewGraphicsRef::UiPreviewGraphicsRef() : graphics(nullptr) {
  UiGetFramework()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&graphics));
}

UiPreviewGraphicsRef::~UiPreviewGraphicsRef() {
  if (graphics)
    graphics->Release();
}

// Start with empty prepared previews, resolve the badge, then retain it after its scoped image handle is destroyed.
UiPreview::UiPreview()
    : enabled(1), key(0), badge(nullptr), original(nullptr), original_color(0), replacement(nullptr),
      replacement_color(0), selected(0), available(0), attributes(nullptr), override_attributes(nullptr),
      original_refreshed(false), replacement_refreshed(false) {
  const GxColorType format(GxColorType::Color::RGB16);
  {
    UiImageRef image(kRsReplacementPreviewBadgeImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    badge = image.GetBuffer();
  }
  if (badge)
    badge->AddRef();
}

UiPreview::~UiPreview() {
  ScRelease(&badge);
  ScRelease(&original);
  ScRelease(&replacement);
  ScRelease(&override_attributes);
  ScRelease(&attributes);
}

// Publish the secondary preview identity before its live retain; other identities use the copied window base.
bool UiPreview::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiPreviewControl)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiPreviewControl *>(this);
  AddRef();
  return true;
}

u32 UiPreview::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiPreview::Release() {
  return UiBaseWindow::Release();
}

// Return the borrowed primary window view of the same complete preview object.
WnWindow *UiPreview::GetWindow() {
  return this;
}

// Acquire only missing prepared buffers, keeping partial outputs and releasing graphics before base initialization.
bool UiPreview::Init() {
  bool ready = false;
  {
    UiPreviewGraphicsRef service;
    if (service.graphics) {
      bool failed = false;
      if (!replacement)
        failed = !service.graphics->CreateBuffer(&replacement);
      if (!original)
        failed |= !service.graphics->CreateBuffer(&original);
      ready = !failed;
    }
  }
  return ready ? UiBaseWindow::Init() : false;
}

// Give the base first refusal for keyboard messages, then synchronously forward an unhandled one to the live parent.
bool UiPreview::DoMessage(ScMessage *message) {
  if (message->type < kWnMsg_Character || message->type > kWnMsg_KeyUp)
    return UiBaseWindow::DoMessage(message);
  if (UiBaseWindow::DoMessage(message))
    return true;
  return parent ? parent->DoMessage(message) : false;
}

// Store the preview key pixel before invalidating this window and its ancestors.
u8 UiPreview::SetBackgroundColor(u32 pixel) {
  key = pixel;
  InvalidateSelfAndParents();
  return 1;
}

// Store the original preview's fill pixel and request a new paint even when the value is unchanged.
u8 UiPreview::SetOriginalColor(u32 pixel) {
  original_color = pixel;
  InvalidateSelfAndParents();
  return 1;
}

// Store the replacement preview's fill pixel before the common invalidation sequence.
u8 UiPreview::SetReplacementColor(u32 pixel) {
  replacement_color = pixel;
  InvalidateSelfAndParents();
  return 1;
}

// Select either known preview mode; all other DWORD values clear selection and return the source's zero byte.
u8 UiPreview::Select(u32 value) {
  if (value == 1 || value == 2) {
    selected = value;
    return 1;
  }
  selected = 0;
  return 0;
}

// Prefer a prepared replacement to the original image, clearing selection when neither is available.
u8 UiPreview::SelectAvailable() {
  if (available & kUiPreviewReplacementAvailable)
    return Select(2);
  if (available & kUiPreviewOriginalAvailable)
    return Select(1);
  Select(0);
  return 0;
}

u8 UiPreview::RefreshOriginal() {
  SpDefinition *definition = nullptr;
  u8 rendered = 1;
  if (attributes) {
    definition = attributes->GetOriginalSpriteAttrib();
    rendered = RenderPreview(original, definition);
  }
  original_refreshed = true;
  if (rendered && definition)
    available |= kUiPreviewOriginalAvailable;
  else
    available &= ~kUiPreviewOriginalAvailable;
  return SelectAvailable();
}

u8 UiPreview::RefreshReplacement() {
  SpDefinition *definition = nullptr;
  u8 rendered = 1;
  if (override_attributes) {
    definition = override_attributes->GetSpriteAttrib();
    rendered = RenderPreview(replacement, definition);
  }
  replacement_refreshed = true;
  if (rendered && definition)
    available |= kUiPreviewReplacementAvailable;
  else
    available &= ~kUiPreviewReplacementAvailable;
  return SelectAvailable();
}

// Release the previous attribute view before storing and retaining the new one, then refresh only while enabled.
u8 UiPreview::SetAttributes(GeAttrib *value) {
  if (attributes)
    attributes->Release();
  attributes = value;
  if (value)
    value->AddRef();
  if (enabled) {
    RefreshOriginal();
    InvalidateSelfAndParents();
  } else
    original_refreshed = false;
  return 1;
}

// Replace the retained override in source order and defer its refresh when preview rendering is disabled.
u8 UiPreview::SetOverride(OcOverride *value) {
  if (override_attributes)
    override_attributes->Release();
  override_attributes = value;
  if (value)
    value->AddRef();
  if (enabled) {
    RefreshReplacement();
    InvalidateSelfAndParents();
  } else
    replacement_refreshed = false;
  return 1;
}

void UiPreview::SetEnabledEffects(u8 value) {
  if (enabled == value)
    return;
  enabled = value;
  if (value) {
    if (!original_refreshed)
      RefreshOriginal();
    if (!replacement_refreshed)
      RefreshReplacement();
  }
  InvalidateSelfAndParents();
}

// Capture and temporarily select the original image, notifying the live parent only when the selection changes.
bool UiPreview::OnMouseDownL(i32, i32, u32) {
  if (available & kUiPreviewOriginalAvailable) {
    owner->SetCapture(this);
    u32 previous = selected;
    Select(1);
    if (selected != previous) {
      InvalidateSelfAndParents();
      u32 id = GetID();
      SendMsg(GetParentWin(), kWnMsg_Command, kUiPreviewOriginalSelected, id, 0);
    }
  }
  return true;
}

bool UiPreview::OnMouseUpL(i32, i32, u32) {
  owner->ReleaseCapture(this);
  return true;
}

// Restore preferred selection when another window takes capture and send the corresponding original/replacement event.
bool UiPreview::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    u32 previous = selected;
    SelectAvailable();
    if (selected != previous) {
      InvalidateSelfAndParents();
      u32 event = selected == kUiPreviewOriginalChoice ? kUiPreviewOriginalSelected
          : selected == kUiPreviewReplacementChoice    ? kUiPreviewReplacementSelected
                                                       : 0;
      u32 id = GetID();
      SendMsg(GetParentWin(), kWnMsg_Command, event, id, 0);
    }
  }
  return true;
}

// Center image bounds with source DWORD coordinate wrap and signed, truncating half offsets.
static ScRect UiCenterPreview(const ScRect &area, const ScRect &image) {
  i32 width = (i32)((u32)image.right - (u32)image.left), height = (i32)((u32)image.bottom - (u32)image.top);
  i32 x = (i32)((u32)area.right - (u32)area.left - (u32)width) / 2;
  i32 y = (i32)((u32)area.bottom - (u32)area.top - (u32)height) / 2;
  x = (i32)((u32)area.left + (u32)x);
  y = (i32)((u32)area.top + (u32)y);
  return {x, y, (i32)((u32)x + (u32)width), (i32)((u32)y + (u32)height)};
}

// Paint the selected prepared image centered over its fill; replacement mode may also display the original badge.
bool UiPreview::GZPaint() {
  if (enabled && draw_buffer->Lock(GxLockFlags::Blt)) {
    if (selected == kUiPreviewReplacementChoice) {
      draw_buffer->Fill(replacement_color, draw_area, nullptr);
      if (replacement) {
        ScRect source;
        replacement->GetBufferArea(&source);
        ScRect destination = UiCenterPreview(draw_area, source);
        draw_buffer->Blt(replacement, source, destination, &draw_area);
      }
      if ((available & kUiPreviewOriginalAvailable) && badge) {
        i32 right = draw_area.right, width = badge->Width(), top = draw_area.top;
        i32 height = badge->Height();
        ScRect destination = {(i32)((u32)right - (u32)width), top, right, (i32)((u32)draw_area.top + (u32)height)};
        const ScRect *source = badge->GetBufferArea();
        draw_buffer->Blt(badge, *(source), destination, &draw_area);
      }
    } else if (selected == kUiPreviewOriginalChoice) {
      draw_buffer->Fill(original_color, draw_area, nullptr);
      if (original) {
        ScRect source;
        original->GetBufferArea(&source);
        ScRect destination = UiCenterPreview(draw_area, source);
        draw_buffer->Blt(original, source, destination, &draw_area);
      }
    } else
      draw_buffer->Fill(original_color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

// Prepare the last fitting zoom and render its demanded sprite art into the supplied buffer, retaining source cleanup.
u8 UiPreview::RenderPreview(GxBuffer *buffer, SpDefinition *definition) {
  if (!definition)
    return 1;
  i32 width = GetW(), height = GetH();
  SpSprite *sprite = nullptr;
  if (!definition->CreateInstance(&sprite))
    return 0;
  ScRect registration = {};
  u32 selected_zoom = 0;
  i32 image_width = 0, image_height = 0;
  bool found = false;
  for (u32 zoom = 0; zoom < 5; ++zoom) {
    if (!sprite->GetRegistration((i32)zoom, 0, &registration))
      break;
    i32 next_width = (i32)((u32)registration.left + (u32)registration.right);
    i32 next_height = (i32)((u32)registration.top + (u32)registration.bottom);
    if (next_width > width || next_height > height)
      break;
    selected_zoom = zoom;
    image_width = next_width;
    image_height = next_height;
    found = true;
  }
  if (!found) {
    sprite->GetRegistration(0, 0, &registration);
    image_width = (i32)((u32)registration.left + (u32)registration.right);
    image_height = (i32)((u32)registration.top + (u32)registration.bottom);
    selected_zoom = 0;
  }
  u8 result = 0;
  if (buffer) {
    bool ready;
    if (buffer->Width() == (u32)image_width && buffer->Height() == (u32)image_height)
      ready = buffer->IsReady();
    else {
      buffer->Shutdown();
      ready = buffer->Init((u32)image_width, (u32)image_height, GxColorType(static_cast<GxColorType::Color>(4)),
          nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
      if (ready) {
        buffer->SetTransparency(key);
        ready = buffer->Lock(GxLockFlags::Fill) != 0;
        if (ready) {
          buffer->Fill(key, nullptr);
          buffer->Unlock(GxLockFlags::Fill);
        }
      }
    }
    if (ready && sprite->DemandArt((i32)selected_zoom, 0, 0)) {
      if (buffer->Lock(GxLockFlags::Bits)) {
        const ScRect *destination = buffer->GetBufferArea();
        sprite->Draw(buffer, *destination, (i32)selected_zoom, 0, nullptr);
        buffer->Unlock(GxLockFlags::Bits);
        result = 1;
      }
      sprite->ReleaseArt((i32)selected_zoom, 0);
    }
  }
  sprite->Release();
  return result;
}

// Construct the original280-byte query-dialog owner and return its primary query interface without an extra retain.
ScUnknown *UiCreateClass0000057A() {
  return static_cast<UiQueryWindow *>(new UiQueryDialog());
}


// Keep the two concrete copies alive until the final result is constructed, then retire the copies and scoped resource.
// Returning a fresh concrete prvalue preserves the initializer's final copy without adding an assignment or interface read.
static ScString UiCopyQueryHeading() {
  UiTextRef resource(kRsCityServiceQueryText, kRsCityServiceTextGroup);
  ScString loaded = resource.GetText(), intermediate(loaded);
  return ScString(intermediate);
}

// Create the default category section only when the live field is empty and append its four empty category separators.
// Publish the local result even when a section already existed or a failed factory query wrote the local output.
u8 UiQueryDialog::EnsureSection() {
  UiQuery *created = nullptr;
  if (!section &&
      UiGetCom()->GetClassObject(kUiQuerySectionClassId, IID_UiQuerySection, reinterpret_cast<void **>(&created))) {
    ScString empty;
    created->SetCategoriesEffects(attributes);
    empty.Erase(0, empty.Strlen());
    created->AddText(&empty, &empty, kUiQueryPrimarySectionSeparator, 0);
    created->AddText(&empty, &empty, kUiQueryBeforeConstructionDateSeparator, 0);
    created->AddText(&empty, &empty, kUiQueryBeforeMonthlyCostSeparator, 0);
    created->AddText(&empty, &empty, kUiQueryAfterMonthlyCostSeparator, 0);
  }
  section = created;
  return 1;
}

// Initialize the generic query window, its default section and borrowed resource-backed controls in source callback order.
// Preview construction calls the separate class580 sprite-window helper when the occupant needs an image.
u8 UiQueryDialog::Initialize() {
  UiWindowResourcesRef resources;
  if (!resources.provider)
    return 0;
  preview_enabled = target && UiIsReplaceableOccupant(target);
  UpdateQueryFormat();
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result && window) {
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericScroller, 1);
    window->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
    result = window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  UpdateLayoutEffects();
  ScString heading_copy = UiCopyQueryHeading();
  SetHeading(&heading_copy);
  EnsureSection();
  if (result) {
    if (target && layout != 1)
      result = BuildPreview();
    if (result) {
      ScString title_copy = UiCopyQueryHeading();
      const u32 tag = kUiQueryTagStandard;
      section->SetTag(&tag);
      section->SetTitle(title_copy);
      result = section->BindTo(static_cast<UiQueryWindow *>(this));
      section->SetArea(&content_area);
      // The following source helper62CB7 returns one without effects; its result is ignored.
    }
  }
  NotifyTarget(UiGetCity(), 1);
  i32 count = (i32)sections.size;
  for (i32 index = 0; index < count; ++index) {
    sections.data[index]->BindTo(static_cast<UiQueryWindow *>(this));
    sections.data[index]->SetArea(&content_area);
  }
  WnWindow *primary = window->GetWindow(), *replace_button = nullptr;
  if (preview_enabled &&
      resources.provider->CreateObject(
          kUiResourceCaptionButton, IID_WnWindow, reinterpret_cast<void **>(&replace_button))) {
    UiTextRef replace_caption(kRsSupplementalGameReplaceText, kRsSupplementalGameTextGroup);
    if (replace_caption.GetResource())
      replace_button->SetCaption(*replace_caption.GetResource());
    replace_button->SetID(4);
    window->GetWindow()->ChildAdd(replace_button);
    i32 offset = (i32)(kUiPreviewReplaceButtonNegativeGap - (u32)replace_button->GetH());
    i32 y = (i32)((u32)primary->GetH() + (u32)offset);
    replace_button->GZWinMoveTo(16, y);
    replace_button->Release();
    replace_button = nullptr;
  }
  const GxColorType format(GxColorType::Color::RGB16);
  GxBuffer *image;
  {
    UiImageRef resource(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    image = resource.GetBuffer();
  }
  if (!image)
    return 0;
  {
    ScString empty;
    action_button = resources.provider->CreateButton(0, empty, image, image->GetBufferArea(), 0);
  }
  if (!action_button)
    return 0;
  WnWindow *button_window = action_button->GetWindow();
  action_button->SetButtonFlag(kWnButtonShowText, false);
  i32 y = (i32)((u32)primary->GetH() - 37), x = (i32)((u32)primary->GetW() - 50);
  button_window->GZWinMoveTo(x, y);
  if (!window->GetWindow()->ChildAdd(button_window)) {
    window->GetWindow()->GetWindowManager()->DestroyWindow(button_window);
    return 0;
  }
  if (attributes & kUiQueryShowOptionFlag)
    LayoutOption();
  return result;
}

// The preview must exist because the original dereferences it before checking acquisition.
u8 UiQueryDialog::BuildPreview() {
  if (!target)
    return 0;
  if (active)
    return 1;
  u8 result = 0;
  if (target->MajorType() != 2) {
    SpCitySpritePreview *preview = nullptr;
    result = UiGetCom()->GetClassObject(
        kSpCitySpritePreviewClassId, IID_SpCitySpritePreview, reinterpret_cast<void **>(&preview));
    assert(preview != nullptr);
    preview->SetDrawSunkenRect(true);
    if (result) {
      result = preview->SetCitySprite(target);
      if (!layout) {
        f32 ratio = 0;
        result = preview->GetSpriteHeightToWidthRatio(&ratio);
        layout = ratio <= 1.2f ? 2 : 3;
      }
      if (result) {
        UpdateLayoutEffects();
        preview->SetArea(picture_area);
        preview->SetSpriteZoomToFitInWindowArea();
      }
      WnWindow *sprite_window = preview->AsIGZWin();
      if (result)
        result = SetPreviewWindow(sprite_window);
      preview->Release();
      if (result)
        active = 1;
    }
  }
  ScString text;
  if (target->GetName(&text))
    result = SetCaption(text);
  return result;
}

#if defined(_M_IX86)
static_assert(sizeof(UiDataRows) == 24);
static_assert(sizeof(UiDataRow) == 20);
static_assert(sizeof(UiDataIterator) == 16);
static_assert(sizeof(UiDataProperty) == 12);
#endif

UiDataRows::UiDataRows() : references(0), sort_property(0) {
}

UiDataRows::~UiDataRows() {
  UiDataRows::Clear();
}

// Publish the primary collection identity for Unknown or its actual IID before invoking the live retain method.
bool UiDataRows::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiQueryRows);
  HANDLE_QUERY_INTERFACE(IID_UiQueryRows, UiQueryRows);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the owner count before retaining the module, then return the count after that callback.
u32 UiDataRows::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Remove the module hold before decrementing a nonzero count; delete the complete owner whenever it reaches zero.
u32 UiDataRows::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

u8 UiDataRows::CreateIterator(UiQueryIterator **out) {
  *out = new UiDataIterator(this);
  return 1;
}

u32 UiDataRows::GetCount() const {
  return rows.size;
}

void UiDataRows::SetSortPropertyEffects(u32 key) {
  sort_property = key;
}

// Compare matching DWORD properties as signed integers, or compare copied text bytes lexicographically.
// Independent virtual reads use the live sort key; both text results are retained before either is copied.
bool UiDataRows::IsBefore(UiQueryRow *first, UiQueryRow *second) {
  if (!sort_property)
    return false;
  u32 type = first->GetType(sort_property);
  if (type != second->GetType(sort_property))
    return false;
  if (type == 7) {
    i32 left = (i32)first->GetU32(sort_property);
    return left < (i32)second->GetU32(sort_property);
  }
  if (type != 0x800c)
    return false;
  ScStringView *left = first->GetText(sort_property), *right = second->GetText(sort_property);
  left->AddRef();
  right->AddRef();
  ScString left_copy(*left), right_copy(*right);
  u32 left_length = left_copy.Strlen(), right_length = right_copy.Strlen();
  u32 length = left_length < right_length ? left_length : right_length;
  i32 order = length ? memcmp(left_copy.Data(), right_copy.Data(), length) : 0;
  bool before = order < 0 || (!order && left_length < right_length);
  left->Release();
  right->Release();
  return before;
}

// Insert before the first greater sortable row, preserving insertion order for ties and all unsortable rows.
// Publish the complete new sequence before retaining the incoming row.
u8 UiDataRows::AppendRow(UiQueryRow *row) {
  u32 position = rows.size;
  if (sort_property) {
    position = 0;
    while (position < rows.size && !IsBefore(row, rows.data[position]))
      ++position;
  }
  rows.Append();
  memmove(rows.data + position + 1, rows.data + position, (rows.size - position - 1) * sizeof(*rows.data));
  rows.data[position] = row;
  row->AddRef();
  return 1;
}

// Construct the real empty row and return its interface without adding a reference.
UiQueryRow *UiDataRows::CreateRow() const {
  return new UiDataRow();
}

// Release the live row sequence in order before clearing its range; retain the vector allocation for reuse.
u8 UiDataRows::Clear() {
  for (u32 index = 0; index < rows.size; ++index)
    rows.data[index]->Release();
  rows.Clear();
  return 1;
}

UiDataRow::UiDataRow() : references(0) {
}

// Release holders in unsigned key order, finding each live successor after its callback so inserted earlier keys stay skipped.
UiDataRow::~UiDataRow() {
  u32 index = 0;
  while (index < properties.size) {
    u32 key = properties.data[index].key;
    properties.data[index].value->Release();
    index = 0;
    while (index < properties.size && properties.data[index].key <= key)
      ++index;
  }
}

// Publish the actual row identity before retaining it; a rejected IID clears the output.
bool UiDataRow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiQueryRow);
  HANDLE_QUERY_INTERFACE(IID_UiQueryRow, UiQueryRow);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Retain the row before the shared module hold and return the live count after that callback.
u32 UiDataRow::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Remove the module hold before decrementing a nonzero row count, then delete through the actual virtual destructor.
u32 UiDataRow::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Borrow the holder for an exact unsigned key match, leaving the output untouched when the key is absent.
u8 UiDataRow::GetProperty(u32 key, UiQueryProperty **out) {
  for (u32 index = 0; index < properties.size; ++index)
    if (properties.data[index].key == key) {
      *out = properties.data[index].value;
      return 1;
    }
  return 0;
}

// Insert a previously absent key in unsigned order; duplicate keys retain the original stored holder.
// The source nevertheless retains the incoming holder after either outcome and always returns one.
u8 UiDataRow::SetProperty(u32 key, UiQueryProperty *value) {
  u32 position = 0;
  while (position < properties.size && properties.data[position].key < key)
    ++position;
  if (position == properties.size || properties.data[position].key != key) {
    properties.Append();
    memmove(properties.data + position + 1, properties.data + position,
        (properties.size - position - 1) * sizeof(*properties.data));
    properties.data[position] = {key, value};
  }
  value->AddRef();
  return 1;
}

// Construct a zero-reference empty holder for the subsequent typed variant setter and property insertion.
UiQueryProperty *UiDataRow::CreateProperty() const {
  return new UiDataProperty();
}

// Build the holder first, copy the supplied text into a real byte-array variant, then insert and retain the holder.
u8 UiDataRow::SetText(u32 key, ScStringView *value) {
  UiQueryProperty *property = CreateProperty();
  property->SetValue(new ScValue(*value));
  return SetProperty(key, property);
}

// Refuse a null String before allocating; otherwise preserve the separate holder, variant and insertion calls.
u8 UiDataRow::SetOptionalText(u32 key, ScStringView *value) {
  if (!value)
    return 0;
  UiQueryProperty *property = CreateProperty();
  property->SetValue(new ScValue(*value));
  return SetProperty(key, property);
}

// Refuse a null object before allocating; otherwise retain it through the real object-bearing variant and holder.
u8 UiDataRow::SetObject(u32 key, ScUnknown *value) {
  if (!value)
    return 0;
  UiQueryProperty *property = CreateProperty();
  property->SetValue(new ScValue(value));
  return SetProperty(key, property);
}

// Wrap the complete DWORD in variant type7 before publishing its separately allocated holder under the supplied key.
u8 UiDataRow::SetU32(u32 key, u32 value) {
  UiQueryProperty *property = CreateProperty();
  property->SetValue(new ScValue(value, 7));
  return SetProperty(key, property);
}

// Return null for an absent holder/value; otherwise copy the exact byte count into a new zero-reference String.
ScStringView *UiDataRow::GetText(u32 key) {
  UiQueryProperty *property = nullptr;
  if (!GetProperty(key, &property))
    return nullptr;
  ScVariant *value = nullptr;
  property->GetValue(&value);
  if (!value)
    return nullptr;
  ScString *result = new ScString();
  u32 count = value->GetCount();
  result->FromChar(value->RefRZChar(), count);
  return result;
}

// Borrow the stored object without retaining either its variant or its holder; missing values return null.
ScUnknown *UiDataRow::GetObject(u32 key) {
  UiQueryProperty *property = nullptr;
  if (!GetProperty(key, &property))
    return nullptr;
  ScVariant *value = nullptr;
  property->GetValue(&value);
  return value ? value->RefUnknown() : nullptr;
}

// Read the stored DWORD through the actual borrowed variant storage; an absent holder/value returns zero.
u32 UiDataRow::GetU32(u32 key) {
  UiQueryProperty *property = nullptr;
  if (!GetProperty(key, &property))
    return 0;
  ScVariant *value = nullptr;
  property->GetValue(&value);
  return value ? *value->AsUint32() : 0;
}

// Return the actual variant type or zero when no stored holder/value is available.
u32 UiDataRow::GetType(u32 key) {
  UiQueryProperty *property = nullptr;
  if (!GetProperty(key, &property))
    return 0;
  ScVariant *value = nullptr;
  property->GetValue(&value);
  return value ? value->GetType() : 0;
}

UiDataIterator::UiDataIterator(UiDataRows *value) : references(0), owner(value), index(0) {
  value->AddRef();
}

UiDataIterator::~UiDataIterator() {
  owner->Release();
}

bool UiDataIterator::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiQueryIterator);
  HANDLE_QUERY_INTERFACE(IID_UiQueryIterator, UiQueryIterator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the iterator count before retaining the module and return the live count after its callback.
u32 UiDataIterator::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Drop a nonzero module hold before decrementing the iterator count, deleting the iterator whenever it reaches zero.
u32 UiDataIterator::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Reset the stored position before the virtual count query and borrow the live first row when the collection is nonempty.
UiQueryRow *UiDataIterator::GetFirst() {
  index = 0;
  return owner->GetCount() ? owner->rows.data[0] : nullptr;
}

// Capture the current index before the virtual count query, then borrow that row from the live owner storage.
UiQueryRow *UiDataIterator::GetCurrent() const {
  u32 current = index;
  return current < owner->GetCount() ? owner->rows.data[current] : nullptr;
}

// Advance only a captured valid position, publishing the next index before borrowing the current row from live storage.
UiQueryRow *UiDataIterator::GetNext() {
  u32 current = index;
  if (current >= owner->GetCount())
    return nullptr;
  index = current + 1;
  return owner->rows.data[current];
}

// Publish the supplied position before querying the live count; invalid positions remain stored and return null.
UiQueryRow *UiDataIterator::GetAt(u32 value) {
  index = value;
  return value < owner->GetCount() ? owner->rows.data[value] : nullptr;
}

UiDataProperty::UiDataProperty() : references(0), value(nullptr) {
}

UiDataProperty::~UiDataProperty() {
  UiDataProperty::SetValue(nullptr);
}

// Publish the actual value-holder identity before retaining it; unsupported IIDs clear the output.
bool UiDataProperty::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiQueryProperty);
  HANDLE_QUERY_INTERFACE(IID_UiQueryProperty, UiQueryProperty);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the holder's count before retaining the module, then return the count after that callback.
u32 UiDataProperty::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Drop the module hold before decrementing a nonzero property count and delete the complete holder at zero.
u32 UiDataProperty::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Release the previous variant before clearing the live field, then publish and retain a nonnull replacement.
u8 UiDataProperty::SetValue(ScVariant *replacement) {
  ScRelease(&value);
  if (replacement) {
    value = replacement;
    replacement->AddRef();
  }
  return 1;
}

u32 UiDataProperty::GetValue(ScVariant **out) {
  *out = value;
  return value != nullptr;
}

#if defined(_M_IX86)
static_assert(sizeof(UiQuerySection) == 312);
#endif

UiQuerySection::UiQuerySection()
    : query_owner(nullptr), cached_fonts{}, title("*No View Name*"), tag(0), categories(kUiAllQueryCategories),
      state308(0) {
}

UiQuerySection::~UiQuerySection() {
}

// Publish all three source query identities through the actual secondary view, then retain the shared window owner.
bool UiQuerySection::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiQuery, UiQuery);
  HANDLE_QUERY_INTERFACE(IID_UiQuerySection, UiQuery);
  HANDLE_QUERY_INTERFACE(IID_CtCityEnumerator, UiQuery);
  return UiTextList::QueryInterface(iid, out);
}

u32 UiQuerySection::AddRef() {
  return UiTextList::AddRef();
}

u32 UiQuerySection::Release() {
  return UiTextList::Release();
}

// Pass the original mutable query and this query identity to a nonnull layer, ignoring its reply.
bool UiQuerySection::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<UiQuery *>(this));
  return true;
}

// Borrow missing font15 entries separately and install their retained base views.
// Publish and retain the dialog before notifying query recipients.
u8 UiQuerySection::BindTo(UiQueryWindow *value) {
  if (!cached_fonts[0])
    cached_fonts[0] = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  if (!cached_fonts[1])
    cached_fonts[1] = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  SetFonts(cached_fonts[0], cached_fonts[1]);
  query_owner = value;
  value->AddRef();
  NotifyTarget(UiGetCity(), 1);
  return 1;
}

// Notify the live occupant before the optional city and layer enumeration, using this section's tag and query view.
u8 UiQuerySection::NotifyTarget(CtCity *city, u8 opening) {
  ScMessage message(opening ? kWnMsg_QueryOpened : kWnMsg_QueryClosed, tag, 0, 0);
  ScOccupant *target = query_owner->GetTarget();
  if (target)
    target->DoQueryInfo(&message, static_cast<UiQuery *>(this));
  if (!city)
    return 0;
  city->DoQueryInfo(&message, static_cast<UiQuery *>(this));
  city->DoEnumLayers(&message, static_cast<UiQuery *>(this));
  return 1;
}

// Install fonts and the owner's current content area, disable alternate alignment/fill and refresh before base startup.
bool UiQuerySection::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  SetFonts(cached_fonts[0], cached_fonts[1]);
  ScRect rectangle;
  query_owner->GetContentArea(&rectangle);
  UiBaseWindow::SetArea(rectangle);
  SetJustification(0);
  SetOpaque(0);
  CalculateAllMetrics();
  SetScrollbarSizes();
  SetScrollbarValue();
  return UiTextList::Init();
}

// Closing notification precedes release of the live owner field; clear it before the base list's font cleanup.
bool UiQuerySection::Shutdown() {
  if (!GetFlag(kWnFlagInitialized))
    return true;
  NotifyTarget(UiGetCity(), 0);
  ScRelease(&query_owner);
  return UiTextList::Shutdown();
}

u8 UiQuerySection::SetTag(const u32 *value) {
  tag = *value;
  return 1;
}

u8 UiQuerySection::SetTitle(const ScStringView &value) {
  ScString copy(value);
  title = copy;
  return 1;
}

// Borrow the stored tag without retaining the owner or adding an output argument.
u32 *UiQuerySection::GetTag() {
  return &tag;
}

// Borrow the embedded title interface without creating a temporary or consuming a selection index.
ScStringView *UiQuerySection::GetTitle() {
  return &title;
}

void UiQuerySection::SetArea(const ScRect *rectangle) {
  UiBaseWindow::SetArea(*rectangle);
}

void UiQuerySection::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiTextList::SetArea(left, top, right, bottom);
}

void UiQuerySection::GetArea(ScRect *out) {
  UiBaseWindow::GetArea(out);
}

// Borrow the real copied window through either the primary list or secondary query view.
WnWindow *UiQuerySection::GetWindow() {
  return this;
}

// Borrow the dialog field; source query56 shares this offset4 getter with unrelated pointer-returning owners.
UiQueryWindow *UiQuerySection::GetOwner() {
  return query_owner;
}

// Read the live dialog's occupant only when an owner exists; the returned occupant is borrowed.
ScOccupant *UiQuerySection::GetTarget() {
  return query_owner ? query_owner->GetTarget() : nullptr;
}

u8 UiQuerySection::GetLocation(CtCoord *out) {
  return query_owner ? query_owner->GetLocation(out) : 0;
}

void UiQuerySection::SetCategoriesEffects(u32 value) {
  categories = value;
}

// Replace only nonnull cached entries with nonnull inputs, removing each old font through a freshly fetched manager.
u8 UiQuerySection::SetQueryFonts(FtFont *first, FtFont *second) {
  if (first && cached_fonts[0]) {
    UiGetFontManager()->FontRelease(cached_fonts[0]);
    cached_fonts[0] = first;
  }
  if (second && cached_fonts[1]) {
    UiGetFontManager()->FontRelease(cached_fonts[1]);
    cached_fonts[1] = second;
  }
  return 1;
}

// Copy each interface input in its own scope, insert before the first larger category and invalidate after publication.
u8 UiQuerySection::InsertSortedRow(
    const ScStringView *first, const ScStringView *second, u32 first_color, u32 second_color, u32 row_categories) {
  ScString first_copy, second_copy;
  {
    ScString copy(*first);
    first_copy = copy;
  }
  {
    ScString copy(*second);
    second_copy = copy;
  }
  UiTextRow *position = rows->next;
  while (position != rows && row_categories >= position->tag)
    position = position->next;
  UiTextRow *row = new UiTextRow;
  row->first = first_copy;
  row->second = second_copy;
  row->colors[0] = first_color;
  row->colors[1] = second_color;
  row->tag = row_categories;
  row->next = position;
  row->previous = position->previous;
  position->previous->next = row;
  position->previous = row;
  InvalidateSelfAndParents();
  return 1;
}

// Reject masked categories before input reads; preserve both initial copies and the later sorted helper's rereads.
u8 UiQuerySection::AddText(const ScStringView *label, const ScStringView *value, u32 row_categories, u8 warning) {
  if (!(row_categories & categories))
    return 0;
  ScString first_copy(*label), second_copy(*value);
  u32 second_color = warning ? MakeFillColor(255, 0, 0) : MakeFillColor(48, 48, 127);
  u32 first_color = MakeFillColor(48, 48, 127);
  InsertSortedRow(label, value, first_color, second_color, row_categories);
  return 1;
}

// Construct the actual section and publish its original secondary query identity without an extra retain.
ScUnknown *UiCreateClass00000584() {
  return static_cast<UiQuery *>(new UiQuerySection);
}

#if defined(_M_IX86)
static_assert(sizeof(UiQueryDialog) == 280);
#endif

UiQueryDialog::UiQueryDialog()
    : references(0), active(0), layout(1), extent(280), area{40, 40, 348, 320}, attributes(kUiTerrainQueryFormat),
      caption(""), caption_enabled(0), alternate_status(""), status_enabled(0), target(nullptr),
      location{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate}, window(nullptr),
      caption_control(nullptr), status_control(nullptr), alternate_control(nullptr), selector(nullptr),
      preview_window(nullptr), section(nullptr), active_section(nullptr), action_button(nullptr) {
  UpdateLayoutEffects();
  background = UiGetWindowResources()->GetColor(kWnColorUiBackground);
}

void UiQueryDialog::SetLabelFlags(u32 flags) {
  attributes = flags;
}

// Read occupant query flags directly when a target exists; otherwise classify the current signed micro-coordinate as water,
// landfill or ordinary terrain, reacquiring the city only when the water path does not select its format.
u8 UiQueryDialog::UpdateQueryFormat() {
  if (target) {
    target->GetAttribData()->GetQueryFormatFlags(&attributes);
  } else {
    ScDirtBag *dirt = UiGetCity()->DirtBag();
    if (dirt && dirt->IsWater((u32)(location.x >> 8), (u32)(location.z >> 8))) {
      attributes = kUiWaterQueryFormat;
    } else {
      CtZoneLayer *zones = UiGetCity()->ZoneLayer();
      u8 zone;
      if (zones) {
        zones->GetValue((u32)(location.x >> 8), (u32)(location.z >> 8), &zone);
        attributes = zone == 17 ? kUiLandfillQueryFormat : kUiTerrainQueryFormat;
      } else {
        attributes = kUiTerrainQueryFormat;
      }
    }
  }
  return 1;
}

// Ask the application's borrowed scheme manager whether the occupant's live attribute is replaceable.
// An absent manager refuses the operation before the occupant attribute is queried.
bool UiIsReplaceableOccupant(ScOccupant *occupant) {
  CtSchemeManager *schemes = UiGetApplication()->CitySchemeManager();
  if (!schemes)
    return false;
  return schemes->IsReplaceableBuilding(occupant->GetAttribData());
}

UiQueryDialog::~UiQueryDialog() {
}

void UiQueryDialog::UpdateLayoutEffects() {
  i32 x = 40, y = 40;
  if (UiGetPreferences()->QueryDialogPrefs()->enabled) {
    ApPreferences *y_owner = UiGetPreferences(), *x_owner = UiGetPreferences(), *check_owner = UiGetPreferences();
    i32 saved_y = y_owner->QueryDialogPrefs()->y;
    i32 saved_x = x_owner->QueryDialogPrefs()->x;
    if (check_owner->IsWindowVisible(saved_x, saved_y, 308, 280)) {
      x = UiGetPreferences()->QueryDialogPrefs()->x;
      y = UiGetPreferences()->QueryDialogPrefs()->y;
    }
  }
  switch (layout) {
  case 1:
    area = {x, y, (i32)((u32)x + 308), (i32)((u32)y + 280)};
    break;
  case 2:
    area = {x, y, (i32)((u32)x + 308), (i32)((u32)y + 395)};
    break;
  case 3:
    area = {x, y, (i32)((u32)x + 423), (i32)((u32)y + 320)};
    break;
  default:
    break;
  }
  LayoutPictureEffects((i32)layout);
  LayoutCaptionEffects((i32)layout);
  LayoutStatusEffects((i32)layout);
  LayoutContentEffects((i32)layout);
  LayoutButtonsEffects((i32)layout);
  if (window)
    window->GetWindow()->SetArea(area);
}

// Unsupported modes retain the existing rectangle.
void UiQueryDialog::LayoutPictureEffects(i32 mode) {
  u32 width = (u32)area.right - (u32)area.left, height = (u32)area.bottom - (u32)area.top;
  switch (mode) {
  case 1:
    picture_area = {};
    break;
  case 2:
    picture_area = {20, 40, (i32)(width - 8), 155};
    break;
  case 3:
    picture_area = {20, 40, 135, (i32)(height - 40)};
    break;
  default:
    break;
  }
}

// Preserve each mode's vertical and horizontal gaps.
void UiQueryDialog::LayoutCaptionEffects(i32 mode) {
  u32 width = (u32)area.right - (u32)area.left;
  switch (mode) {
  case 1:
    caption_area = {20, 40, (i32)(width - 8), 60};
    break;
  case 2: {
    u32 picture_height = (u32)picture_area.bottom - (u32)picture_area.top;
    caption_area = {20, (i32)(picture_height + 43), (i32)(width - 8), (i32)(picture_height + 63)};
    break;
  }
  case 3:
    caption_area = {(i32)((u32)picture_area.right - (u32)picture_area.left + 23), 40, (i32)(width - 8), 60};
    break;
  default:
    break;
  }
}

// Disabled status clears it even when the layout mode is unsupported.
void UiQueryDialog::LayoutStatusEffects(i32 mode) {
  u32 width = (u32)area.right - (u32)area.left;
  if (!status_enabled)
    status_area = {};
  else if (mode == 1 || mode == 2)
    status_area = {20, caption_area.bottom, (i32)(width - 8), (i32)((u32)caption_area.bottom + 20)};
  else if (mode == 3)
    status_area = {(i32)((u32)picture_area.right - (u32)picture_area.left + 23), caption_area.bottom, (i32)(width - 8),
        (i32)((u32)caption_area.bottom + 20)};
}

// Preserve the original bottom margin and horizontal spacing.
void UiQueryDialog::LayoutContentEffects(i32 mode) {
  if (mode >= 1 && mode <= 3) {
    u32 width = (u32)area.right - (u32)area.left, height = (u32)area.bottom - (u32)area.top;
    i32 left = mode == 3 ? (i32)((u32)picture_area.right - (u32)picture_area.left + 23) : 20;
    i32 top = status_enabled ? status_area.bottom : caption_area.bottom;
    content_area = {left, (i32)((u32)top + 10), (i32)(width - 8), (i32)(height - 40)};
  }
}

// Its horizontal endpoints use the source's signed division by three.
void UiQueryDialog::LayoutButtonsEffects(i32 mode) {
  if (mode >= 1 && mode <= 3) {
    i32 width = (i32)((u32)area.right - (u32)area.left), third = width / 3;
    button_area = {third, 6, 2 * third, 26};
  }
}

// Publish the primary query identity before retaining it; rejected identifiers clear the output.
bool UiQueryDialog::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiQueryWindow);
  HANDLE_QUERY_INTERFACE(IID_UiQueryWindow, UiQueryWindow);
  HANDLE_QUERY_INTERFACE(IID_CtCityEnumerator, UiQueryWindow);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the shared owner count before retaining the module, then return the live count after that callback.
u32 UiQueryDialog::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Remove the module hold before decrementing a nonzero live count, and delete whenever the resulting count is zero.
u32 UiQueryDialog::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Refuse an already published generic view, otherwise clear the old target and copy the complete supplied position.
// Layout flags and all three coordinates are visible to the direct initializer before its byte reply is returned.
u8 UiQueryDialog::InitAtLocation(const CtCoord *position, u32 flags) {
  if (window)
    return 0;
  target = nullptr;
  layout = flags;
  location = *position;
  return Initialize();
}

// Publish a nonnull occupant before retaining it and asking it to write the stored location.
// A null occupant preserves the previous target and position; no synthesized release or reset is added.
u8 UiQueryDialog::InitForOccupant(ScOccupant *occupant, u32 flags) {
  if (window)
    return 0;
  layout = flags;
  if (occupant) {
    target = occupant;
    occupant->AddRef();
    target->GetLocation(&location);
  }
  return Initialize();
}

// Copy the arbitrary String interface into an independent temporary before assigning the stored heading.
u8 UiQueryDialog::SetHeading(ScStringView *input) {
  ScString copy(*input);
  heading = copy;
  return 1;
}

// Refuse caption replacement after its control exists, otherwise assign an independently copied String into the caption.
u8 UiQueryDialog::SetCaption(const ScStringView &input) {
  if (caption_control)
    return 0;
  ScString copy(input);
  caption = copy;
  return 1;
}

// Copy the status before publishing the single-status flags and recomputing layout; an existing status control refuses it.
u8 UiQueryDialog::SetStatusText(const ScStringView &input) {
  if (status_control)
    return 0;
  {
    ScString copy(input);
    status = copy;
  }
  status_pair = 0;
  status_enabled = 1;
  UpdateLayoutEffects();
  return 1;
}

// Copy and retire each temporary in sequence before enabling the paired status layout.
// Either already published status control refuses both assignments without inspecting the input strings.
u8 UiQueryDialog::SetStatusPair(ScStringView *first, ScStringView *second) {
  if (status_control || alternate_control)
    return 0;
  {
    ScString copy(*first);
    status = copy;
  }
  {
    ScString copy(*second);
    alternate_status = copy;
  }
  status_enabled = 1;
  status_pair = 1;
  UpdateLayoutEffects();
  return 1;
}

u8 UiQueryDialog::GetArea(ScRect *out) const {
  *out = area;
  return 1;
}

// Acquire the current primary before changing the stored rectangle, then forward that live rectangle to the window.
u8 UiQueryDialog::SetArea(ScRect *rectangle) {
  if (!window)
    return 0;
  WnWindow *primary = window->GetWindow();
  area = *rectangle;
  primary->SetArea(area);
  return 1;
}

// Borrow the generic owner's primary without adding a hold, or return null before a generic view has been published.
WnWindow *UiQueryDialog::GetWindow() const {
  return window ? window->GetWindow() : nullptr;
}

u8 UiQueryDialog::GetLocation(CtCoord *out) const {
  *out = location;
  return 1;
}

// Recompute layout only when the full supplied mode differs from the live stored value.
// Same-mode calls preserve every stored rectangle and skip preference reads.
u8 UiQueryDialog::SetLayout(u32 mode) {
  if (mode != layout) {
    layout = mode;
    UpdateLayoutEffects();
  }
  return 1;
}

// Save each window coordinate through its separately captured preference owner before enabling saved placement.
// Retire the section, captured section range, controls, target and generic view in source order, clearing after each release.
u8 UiQueryDialog::Detach(WnWindow *recipient) {
  window->GetWindow();
  ApPreferences *x_owner = UiGetPreferences();
  i32 x = recipient->GetL();
  x_owner->QueryDialogPrefs()->x = x;
  ApPreferences *y_owner = UiGetPreferences();
  i32 y = recipient->GetT();
  y_owner->QueryDialogPrefs()->y = y;
  UiGetPreferences()->QueryDialogPrefs()->enabled = 1;
  ScRelease(&section);
  u32 count = sections.size;
  for (u32 i = 0; i < count; ++i) {
    if (sections.data[i]) {
      sections.data[i]->Release();
      sections.data[i] = nullptr;
    }
  }
  ScRelease(&preview_window);
  ScRelease(&caption_control);
  ScRelease(&status_control);
  ScRelease(&alternate_control);
  ScRelease(&selector);
  ScRelease(&action_button);
  ScRelease(&target);
  ScRelease(&window);
  return 1;
}

// Build the caption and status controls, then attach every section while preserving live field reloads and captured counts.
// Publish the main pane before optional selector construction, and run option layout even after a control refusal.
u8 UiQueryDialog::Attach(WnWindow *recipient) {
  window->GetWindow();
  u8 result = 1;
  if (recipient) {
    recipient->SetCaption(heading);
    if (preview_window) {
      preview_window->SetArea(picture_area);
      recipient->ChildAdd(preview_window);
    }
    if (caption_enabled) {
      void *view = nullptr;
      result = UiGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, &view);
      if (result) {
        result = static_cast<WnTextControl *>(view)->QueryInterface(
            IID_WnWindow, reinterpret_cast<void **>(&caption_control));
        if (result) {
          FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
          static_cast<WnTextControl *>(view)->SetFont(font);
          static_cast<WnTextControl *>(view)->SetText(&caption);
          static_cast<WnTextControl *>(view)->SetCharacterLimit(50);
          static_cast<WnTextControl *>(view)->SetOutlineType(WnInputOutline::ThreeD);
          u32 color = UiGetWindowResources()->GetColor(19);
          static_cast<WnTextControl *>(view)->SetFontColor(WnInputColor::Normal, color);
          color = UiGetWindowResources()->GetColor(20);
          static_cast<WnTextControl *>(view)->SetFontColor(WnInputColor::Disabled, color);
          caption_control->SetArea(caption_area);
          caption_control->SetID(2);
          color = UiGetWindowResources()->GetColor(kWnColorWhite);
          caption_control->SetFillColor(color);
          recipient->ChildAdd(caption_control);
          static_cast<WnTextControl *>(view)->Release();
        }
      }
    } else {
      void *view = nullptr;
      result = UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, &view);
      if (result) {
        result = static_cast<WnLabelControl *>(view)->QueryInterface(
            IID_WnWindow, reinterpret_cast<void **>(&caption_control));
        if (result) {
          FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiMain);
          static_cast<WnLabelControl *>(view)->SetFont(font);
          u32 color = caption_control->MakeFillColor(48, 48, 127);
          static_cast<WnLabelControl *>(view)->SetTextColor(color);
          static_cast<WnLabelControl *>(view)->SetLabelFlag(kWnLabelFillBackground, false);
          caption_control->SetCaption(caption);
          caption_control->SetArea(caption_area);
          recipient->ChildAdd(caption_control);
          static_cast<WnLabelControl *>(view)->SetBackground(true);
          static_cast<WnLabelControl *>(view)->Release();
        }
      }
    }
    if (status_enabled && result) {
      ScRect status_bounds{};
      void *view = nullptr;
      result = UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, &view);
      if (result) {
        result = static_cast<WnLabelControl *>(view)->QueryInterface(
            IID_WnWindow, reinterpret_cast<void **>(&status_control));
        if (result) {
          FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiMain);
          static_cast<WnLabelControl *>(view)->SetFont(font);
          static_cast<WnLabelControl *>(view)->SetLabelFlag(kWnLabelFillBackground, false);
          static_cast<WnLabelControl *>(view)->SetLabelFlag(kWnLabelWrapText, false);
          u32 color = status_control->MakeFillColor(48, 48, 127);
          static_cast<WnLabelControl *>(view)->SetTextColor(color);
          status_control->SetCaption(status);
          status_control->SetArea(status_area);
          static_cast<WnLabelControl *>(view)->Fit();
          status_bounds = *status_control->GetArea();
          recipient->ChildAdd(status_control);
          static_cast<WnLabelControl *>(view)->SetBackground(true);
          static_cast<WnLabelControl *>(view)->Release();
          view = nullptr;
        }
      }
      if (status_pair) {
        void *alternate = nullptr;
        result = UiGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, &alternate);
        if (result) {
          result = static_cast<WnTextControl *>(alternate)->QueryInterface(
              IID_WnWindow, reinterpret_cast<void **>(&alternate_control));
          if (result) {
            FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
            static_cast<WnTextControl *>(alternate)->SetFont(font);
            static_cast<WnTextControl *>(alternate)->SetText(&alternate_status);
            static_cast<WnTextControl *>(alternate)->SetCharacterLimit(50);
            static_cast<WnTextControl *>(alternate)->SetOutlineType(WnInputOutline::ThreeD);
            u32 color = UiGetWindowResources()->GetColor(19);
            static_cast<WnTextControl *>(alternate)->SetFontColor(WnInputColor::Normal, color);
            color = UiGetWindowResources()->GetColor(20);
            static_cast<WnTextControl *>(alternate)->SetFontColor(WnInputColor::Disabled, color);
            alternate_control->SetArea(
                (i32)((u32)status_bounds.right + 2), status_bounds.top, status_area.right, status_area.bottom);
            alternate_control->SetID(3);
            color = UiGetWindowResources()->GetColor(kWnColorWhite);
            alternate_control->SetFillColor(color);
            recipient->ChildAdd(alternate_control);
            static_cast<WnTextControl *>(alternate)->Release();
          }
        }
      }
    }
    u32 count = sections.size;
    for (u32 i = 0; i < count; ++i) {
      sections.data[i]->SetArea(&content_area);
      recipient->ChildAdd(sections.data[i]->GetWindow());
      sections.data[i]->GetWindow()->HideWindow();
    }
    section->SetArea(&content_area);
    recipient->ChildAdd(section->GetWindow());
    section->GetWindow()->PullToFront();
    section->GetWindow()->ShowWindow();
    active_section = section;
    if (result && GetSectionCount() > 1) {
      result = UiGetCom()->GetClassObject(kWnComboClassId, IID_WnComboControl, reinterpret_cast<void **>(&selector));
      if (result) {
        selector->SetEditable(0);
        selector->GetWindow()->SetArea(button_area);
        ScStringView *title = section->GetTitle();
        selector->InsertString(*title, 0);
        ScString temporary;
        for (u32 i = 0; i < count; ++i) {
          if (sections.data[i]) {
            title = sections.data[i]->GetTitle();
            selector->InsertString(*title, (i32)(i + 1));
          }
        }
        recipient->ChildAdd(selector->GetWindow());
        selector->SetSelection(0, false);
        SelectSection();
      }
    }
  }
  LayoutOption();
  return result;
}

// Select a nonnull indexed section or fall back to the main section, keeping an unchanged selection untouched.
// Hide the previous pane before publishing the new borrowed selection; show uses the live selection after fronting callbacks.
u8 UiQueryDialog::SelectSection() {
  UiQuery *selected = nullptr;
  if (selector) {
    i32 index = selector->GetSelection();
    u32 slot = (u32)index - 1;
    if (index && slot < sections.size)
      selected = sections.data[slot];
  }
  if (!selected)
    selected = section;
  if (selected != active_section) {
    active_section->GetWindow()->HideWindow();
    active_section = selected;
    selected->GetWindow()->PullToFront();
    active_section->GetWindow()->ShowWindow();
  }
  return 1;
}

// Infer a layout only while its mode is unset, refusing nonpositive wrapped widths before publishing the preview.
// Clear the active flag and publish the retained window before its AddRef callback; no prior preview is released.
u8 UiQueryDialog::SetPreviewWindow(WnWindow *preview) {
  if (!layout) {
    ScRect rectangle;
    preview->GetArea(&rectangle);
    i32 width = (i32)((u32)rectangle.right - (u32)rectangle.left);
    if (width <= 0)
      return 0;
    i32 height = (i32)((u32)rectangle.bottom - (u32)rectangle.top);
    // The original threshold is float1.2 (5033165/4194304), rather than the double literal1.2.
    layout = (i64)height * kUiPreviewAspectDenominator <= (i64)width * kUiPreviewAspectNumerator ? 2 : 3;
  }
  active = 0;
  preview_window = preview;
  preview->AddRef();
  return 1;
}

u8 UiQueryDialog::SetExtent(i32 value) {
  extent = value;
  return 1;
}

// Refuse a changed name-edit Boolean after its control exists, while allowing the same value again.
// Linux SetNameEditFlag...b proves the input; retain the existing qualified byte return carrier.
u8 UiQueryDialog::SetCaptionEnabled(bool value) {
  if (u8(value) != caption_enabled && caption_control)
    return 0;
  caption_enabled = value;
  return 1;
}

u8 UiQueryDialog::SetBackgroundColor(u32 pixel) {
  background = pixel;
  return 1;
}

u8 UiQueryDialog::GetLayout(u32 *out) const {
  *out = layout;
  return 1;
}

u32 UiQueryDialog::GetExtent() const {
  return extent;
}

// Borrow the embedded heading interface without creating a copy or adding a hold.
ScStringView *UiQueryDialog::GetHeading() {
  return &heading;
}

// Borrow the separate embedded caption interface, rather than the heading or status storage.
ScStringView *UiQueryDialog::GetCaption() {
  return &caption;
}

// Borrow the second status string used by paired status presentation.
ScStringView *UiQueryDialog::GetAlternateStatusText() {
  return &alternate_status;
}

u8 UiQueryDialog::GetCaptionEnabled() {
  return caption_enabled;
}

u8 UiQueryDialog::GetBackgroundColor(u32 *out) {
  *out = background;
  return 1;
}

u8 UiQueryDialog::QueryPreviewInterface(u32 iid, void **out) const {
  return preview_window ? preview_window->QueryInterface(iid, out) : 0;
}

u8 UiQueryDialog::GetPictureArea(ScRect *out) const {
  *out = picture_area;
  return 1;
}

u8 UiQueryDialog::QuerySectionInterface(u32 iid, void **out) const {
  return section ? section->QueryInterface(iid, out) : 0;
}

u8 UiQueryDialog::GetContentArea(ScRect *out) const {
  *out = content_area;
  return 1;
}

// Accept the first main section only, publishing its actual interface before the retain callback.
u8 UiQueryDialog::SetSection(UiQuery *value) {
  if (section)
    return 0;
  section = value;
  value->AddRef();
  return 1;
}

// Append the original interface pointer before retaining it, so callbacks observe the grown live section range.
u8 UiQueryDialog::AddSection(UiQuery *value) {
  *sections.Append() = value;
  value->AddRef();
  return 1;
}

// Test the main section first, then search a captured section count while reloading the live array after callbacks.
u8 UiQueryDialog::FindSectionInterface(u32 *tag, u32 iid, void **out) {
  u32 *stored_tag = section->GetTag();
  UiQuery *selected;
  if (*tag == *stored_tag) {
    selected = section;
  } else {
    u32 index = 0, count = sections.size;
    for (; index < count; ++index) {
      stored_tag = sections.data[index]->GetTag();
      if (*tag == *stored_tag)
        break;
    }
    if (index == count)
      return 0;
    selected = sections.data[index];
  }
  return selected ? selected->QueryInterface(iid, out) : 0;
}

// Borrow the current occupant pointer without querying it or adding a reference.
ScOccupant *UiQueryDialog::GetTarget() const {
  return target;
}

u32 UiQueryDialog::GetSectionCount() {
  return sections.size + 1;
}

// Place the checkbox below the current content and, when present, its companion above it.
// Resize each live section and then the captured active section to the companion's current top edge.
u8 UiQueryDialog::LayoutOption() {
  if (!(attributes & kUiQueryShowOptionFlag))
    return 0;
  WnWindow *checkbox = window->GetWindow()->GetChildWindowFromID(1);
  if (!checkbox)
    return 0;
  i32 x = layout == 3 ? (i32)((u32)picture_area.right - (u32)picture_area.left + 23) : 20;
  i32 height = checkbox->GetH();
  i32 y = (i32)((u32)area.bottom - (u32)area.top - (u32)height - 8);
  checkbox->GZWinMoveTo(x, y);
  WnWindow *companion = window->GetWindow()->GetChildWindowFromID(4);
  if (companion) {
    height = companion->GetH();
    companion->GZWinMoveTo(x, (i32)((u32)y - (u32)height));
    UiQuery **entries = sections.data;
    for (u32 i = 0; i < sections.size; ++i) {
      UiQuery *current = entries[i];
      if (current) {
        ScRect rectangle;
        current->GetWindow()->GetArea(&rectangle);
        WnWindow *pane = current->GetWindow();
        i32 bottom = companion->GetT();
        pane->SetArea(rectangle.left, rectangle.top, rectangle.right, bottom);
      }
    }
    UiQuery *current = active_section;
    if (current) {
      ScRect rectangle;
      current->GetWindow()->GetArea(&rectangle);
      WnWindow *pane = current->GetWindow();
      i32 bottom = companion->GetT();
      pane->SetArea(rectangle.left, rectangle.top, rectangle.right, bottom);
    }
  }
  return 1;
}

// Create the retained checkbox through the live resource provider, then configure its window, font, gutters and check state.
// Capture child attachment before fitting and releasing the temporary view, preserving the original refusal behavior.
u8 UiQueryDialog::AddOption(const ScStringView &option_caption, FtFont *font, bool checked) {
  if (!UiGetWindowResources() || !(attributes & kUiQueryShowOptionFlag))
    return 0;
  void *view = nullptr;
  u8 attached = 0;
  if (UiGetWindowResources()->CreateObject(kUiResourceEightStateButton, IID_WnButton, &view)) {
    static_cast<WnButton *>(view)->GetWindow()->SetID(1);
    static_cast<WnButton *>(view)->GetWindow()->SetCaption(option_caption);
    if (font)
      static_cast<WnButton *>(view)->SetFont(font);
    static_cast<WnButton *>(view)->SetGutters(2, 1, 2, 2);
    static_cast<WnButton *>(view)->SetChecked(checked);
    WnWindow *parent = window->GetWindow();
    attached = parent->ChildAdd(static_cast<WnButton *>(view)->GetWindow());
    static_cast<WnButton *>(view)->Fit();
    static_cast<WnButton *>(view)->Release();
  }
  return attached;
}

// Query the retained checkbox view only after a generic window exists, preserving refusal without compensating release.
// Capture its checked result before retiring the live published view and returning the byte.
u8 UiQueryDialog::IsOptionChecked() {
  if (!window)
    return 0;
  void *view = nullptr;
  if (!window->GetWindow()->GetChildAs(1, IID_WnButton, &view))
    return 0;
  u8 checked = static_cast<WnButton *>(view)->IsChecked();
  static_cast<WnButton *>(view)->Release();
  return checked;
}

// Forward the caller's mutable message to each nonnull layer without consuming its reply.
// A missing layer still permits enumeration to continue through the constant true result.
bool UiQueryDialog::Enumerate(ScMessage *query_message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(query_message, static_cast<UiQueryWindow *>(this));
  return true;
}

// Deliver the same mutable notification first to the occupant, then the city and its layer enumerator.
// Preserve the installed decimal-derived identifiers and ignore every recipient's Boolean reply.
u8 UiQueryDialog::NotifyTarget(CtCity *city, u8 opening) {
  ScMessage query_message(opening ? kWnMsg_QueryOpened : kWnMsg_QueryClosed, kUiQueryTagDialog, 0, 0);
  ScUnknown *sender = static_cast<UiQueryWindow *>(this);
  if (target)
    target->DoQueryInfo(&query_message, sender);
  if (city) {
    city->DoQueryInfo(&query_message, sender);
    city->DoEnumLayers(&query_message, static_cast<CtCityEnumerator *>(this));
  }
  return 1;
}

void UiQueryDialog::EndModalEffects(WnWindow *recipient, u32 result) {
  if (result == 0 || result == 2)
    NotifyTarget(UiGetCity(), 0);
  UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(recipient), result);
}

// Borrow the primary before every callback, then route attachment, teardown, keys, commands and text updates.
// Text notifications acquire and retire their actual text-control view even for identifiers whose text is not copied.
bool UiQueryDialog::DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr) {
  window->GetWindow();
  if (message == kWnMsg_Init)
    return Attach(recipient);
  if (message == kWnMsg_Shutdown)
    return Detach(recipient);
  if (message == kWnMsg_KeyDown) {
    if ((u32)first != 27 && ((u32)first != 115 || !((u32)second & 4)))
      return 0;
    EndModalEffects(recipient, 1);
    return 1;
  }
  if (message != kWnMsg_Command)
    return 0;
  switch ((u32)first) {
  case 1:
    if (!(u32)second)
      EndModalEffects(recipient, 0);
    else if ((u32)second == 4)
      EndModalEffects(recipient, 2);
    else if ((u32)second == kUiFrameCloseButtonId)
      EndModalEffects(recipient, 1);
    else
      return 0;
    return 1;
  case 8: {
    if (!recipient)
      return 0;
    WnWindow *child = recipient->GetChildWindowFromID((u32)second);
    void *view = nullptr;
    if (!child->QueryInterface(IID_WnTextControl, &view))
      return 0;
    if ((u32)second == 2)
      static_cast<WnTextControl *>(view)->GetText(&caption);
    else if ((u32)second == 3)
      static_cast<WnTextControl *>(view)->GetText(&alternate_status);
    static_cast<WnTextControl *>(view)->Release();
    return 1;
  }
  case 11:
    SelectSection();
    return 1;
  default:
    return 0;
  }
}

UiRasterVisibilityCommand::UiRasterVisibilityCommand() {
}

// Consume a type3 operation first; only operationzero consumes a second argument and changes window visibility.
bool UiRasterVisibilityCommand::Execute() {
  ScVariant *argument;
  if (!InputQueueSize() || !PopFrontInput(&argument))
    return false;
  if (argument->GetType() != kScValueUint32) {
    argument->Release();
    return false;
  }
  i32 operation = (i32)*argument->AsUint32();
  argument->Release();
  if (operation)
    return true;
  if (!InputQueueSize() || !PopFrontInput(&argument))
    return false;
  bool visible = *argument->AsUint32() != 0;
  argument->Release();
  return SetVisible(visible);
}

// Find the existing query window recursively, or create its real provider only when a city view exists.
// Retain the window across attachment and show/hide operations, releasing the provider before changing visibility.
bool UiRasterVisibilityCommand::SetVisible(bool visible) {
  WnWindow *window = UiGetWindowManager()->GetMainWindow()->GetChildWindowFromIDRecursive(kUiMapsWindowId);
  if (window) {
    window->AddRef();
  } else {
    if (!UiGetCityWindow())
      return false;
    WnWindow *parent = UiGetWindowManager()->GetMainWindow();
    UiRasterQuery *query = nullptr;
    if (UiGetCom()->GetClassObject(kUiRasterQueryClassId, IID_UiRasterQuery, reinterpret_cast<void **>(&query)) &&
        query->Init()) {
      window = query->GetWindow();
      if (window) {
        window->AddRef();
        parent->ChildAdd(window);
      }
    }
    if (query)
      query->Release();
    if (!window)
      return false;
  }
  if (visible) {
    window->PullToFront();
    window->ShowWindow();
  } else {
    window->HideWindow();
  }
  window->Release();
  return true;
}
