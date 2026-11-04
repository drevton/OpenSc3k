#include "ui/city_scheme.h"

#include <assert.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_unknown.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/sprite.h"
#include "terrain/tr_dirt_bag.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/text_edit.h"
#include "window/window.h"

// Create and fit a provider label before attaching it, shutting down a rejected child before retiring its label view.
void UiCitySchemeWindow::AddTextLabelEffects(
    WnWindow *parent_window, u32 id, u32 x, u32 y, const ScStringView &text, FtFont *font, bool enabled) {
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(id, text);
  if (!label)
    return;
  WnWindow *window = label->GetWindow();
  label->SetFont(font);
  label->SetBackground(false);
  label->SetLabelFlag(kWnLabelWrapText, enabled);
  label->Fit();
  window->GZWinMoveTo((i32)x, (i32)y);
  if (!parent_window->ChildAdd(window))
    window->Shutdown();
  label->Release();
}

// Attach two inset flat outlines using independently acquired color providers in the original argument-evaluation order.
// A rejected attachment destroys that child and returns zero; unavailable factory objects leave the final result one.
u8 UiCitySchemeWindow::AddOutlineBox(WnWindow *parent_window, u32 x, u32 y, u32 width, u32 height) {
  WnFlatControl *outline = nullptr;
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&outline))) {
    WnWindow *window = outline->GetWindow();
    window->SetArea((i32)x, (i32)y, (i32)(x + width), (i32)(y + height));
    window->SetFlag(kWnFlagReorderable, false);
    outline->SetOutlineFlags(15);
    WnResources *bottom_provider = UiGetWindowResources(), *right_provider = UiGetWindowResources();
    WnResources *top_provider = UiGetWindowResources(), *left_provider = UiGetWindowResources();
    u32 bottom = bottom_provider->GetColor(kWnThemeOutlineLightColorId),
        right = right_provider->GetColor(kWnThemeOutlineLightColorId);
    u32 top = top_provider->GetColor(kWnThemeOutlineMediumColorId),
        left = left_provider->GetColor(kWnThemeOutlineMediumColorId);
    outline->SetOutlineColors(left, top, right, bottom);
    if (!parent_window->ChildAdd(window)) {
      UiGetWindowManager()->DestroyWindow(window);
      outline->Release();
      return 0;
    }
    outline->Release();
    outline = nullptr;
  }
  if (UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&outline))) {
    WnWindow *window = outline->GetWindow();
    window->SetArea((i32)(x + 1), (i32)(y + 1), (i32)(x + width - 1), (i32)(y + height - 1));
    window->SetFlag(kWnFlagReorderable, false);
    outline->SetOutlineFlags(15);
    WnResources *bottom_provider = UiGetWindowResources(), *right_provider = UiGetWindowResources();
    WnResources *top_provider = UiGetWindowResources(), *left_provider = UiGetWindowResources();
    u32 bottom = bottom_provider->GetColor(kWnThemeSecondaryMutedColorId),
        right = right_provider->GetColor(kWnThemeSecondaryMutedColorId);
    u32 top = top_provider->GetColor(kWnThemeOutlineDarkColorId),
        left = left_provider->GetColor(kWnThemeOutlineDarkColorId);
    outline->SetOutlineColors(left, top, right, bottom);
    if (!parent_window->ChildAdd(window)) {
      UiGetWindowManager()->DestroyWindow(window);
      outline->Release();
      return 0;
    }
    outline->Release();
  }
  return 1;
}

// Configure the source single-line input's font, colors, limit and gutters before sizing it from its minimum height.
// Preserve the unused height input and return one when the factory declines creation, as in the installed body.
u8 UiCitySchemeWindow::AddOutlinedTextEdit(
    WnWindow *parent_window, u32 id, u32 x, u32 y, u32 width, u32, const ScStringView &text) {
  WnTextControl *input = nullptr;
  if (UiGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, reinterpret_cast<void **>(&input))) {
    WnWindow *window = input->AsIGZWin();
    WnResources *resources = UiGetWindowResources();
    window->SetID(id);
    window->SetFillColor(resources->GetColor(kWnColorWhite));
    input->SetFont(resources->GetFont(kWnFontUiSmall));
    input->SetText(text);
    input->SetCharacterLimit(50);
    input->SetOutlineType(WnInputOutline::ThreeDEx);
    input->SetOutlineColor(WnInputEdge::LeftTop, resources->GetColor(kWnThemeOutlineMediumColorId));
    input->SetOutlineColor(WnInputEdge::RightBottom, resources->GetColor(kWnThemeOutlineLightColorId));
    input->SetOutlineColor(WnInputEdge::LeftTopInside, resources->GetColor(kWnThemeOutlineDarkColorId));
    input->SetOutlineColor(WnInputEdge::RightBottomInside, resources->GetColor(kWnThemeSecondaryMutedColorId));
    input->SetGutters(4, 1, 2, 1);
    ScRect limits{};
    input->GetMinMaxInfo(&limits);
    window->SetArea((i32)x, (i32)y, (i32)(x + width), (i32)(y + (u32)limits.top));
    if (!parent_window->ChildAdd(window)) {
      UiGetWindowManager()->DestroyWindow(window);
      input->Release();
      return 0;
    }
    input->Release();
  }
  return 1;
}

// Build a radio-style checkbox, set its three matching text colors, fit and position it, then capture attachment success.
// This original member retains its unused receiver and six separate inputs; rejected children are destroyed before release.
u8 UiCitySchemeWindow::AddCheckBox(
    WnWindow *parent_window, u32 id, u32 x, u32 y, bool enabled, const ScStringView &text) {
  WnButton *button = UiGetWindowResources()->CreateRadioButton(id, text, nullptr, nullptr, 0);
  u32 color = UiGetWindowResources()->GetColor(kWnThemeTextColorId);
  WnWindow *window = button->GetWindow();
  button->SetChecked(enabled);
  button->SetGutters(3, 1, 2, 2);
  button->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  button->SetFontColor(kWnButtonTextEnabled, color);
  button->SetFontColor(kWnButtonTextHovered, color);
  button->SetFontColor(kWnButtonTextDisabled, color);
  button->Fit();
  window->GZWinMoveTo((i32)x, (i32)y);
  u8 result = 0;
  if (parent_window->ChildAdd(window))
    result = 1;
  else
    UiGetWindowManager()->DestroyWindow(window);
  button->Release();
  return result;
}

// Create resource-backed option buttons across the captured key range, advancing the horizontal cell only for created views.
// Fit each accepted child against the supplied bottom edge and toggle every created button before retiring its image handle.
void UiCitySchemeWindow::AddOptionButtons(ScArray<RsKey> *keys, u32 id, i32 bottom, i32 stride, u32 selected) {
  i32 x = 248;
  ScString text;
  RsKey *key = keys->data;
  u32 count = keys->size;
  for (u32 i = 0; i < count; ++i, ++key, ++id) {
    const GxColorType format(GxColorType::Color::RGB16);
    UiImageRef record(key->instance, key->group, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = record.GetBuffer();
    ScRect rectangle{};
    if (buffer)
      buffer->GetBufferArea(&rectangle);
    WnButton *button = UiGetWindowResources()->CreateFixedToggleButton(id, text, buffer, &rectangle, 0);
    if (button) {
      WnWindow *window = button->GetWindow();
      if (!buffer) {
        window->SetFillColor(UiGetWindowResources()->GetColor(kWnColorSilver));
        button->SetButtonFlag(kWnButtonAutoFit, true);
      }
      button->SetButtonFlag(kWnButtonShowText, false);
      if (ChildAdd(window)) {
        i32 center = (i32)((u32)x + (u32)(stride / 2));
        i32 y = (i32)((u32)bottom - (u32)window->GetH());
        i32 width = window->GetW();
        window->GZWinMoveTo((i32)((u32)center - (u32)(width / 2)), y);
      } else
        GetWindowManager()->DestroyWindow(window);
      if (id == selected)
        button->ToggleOn();
      else
        button->ToggleOff();
      x = (i32)((u32)x + (u32)stride);
      button->Release();
    }
  }
}

// Build the three scheme selectors from the live manager's identifiers and icon keys, retaining their original ranges.
// Add the localized labels and action button after the option rows, preserving resource and child ownership order.
u8 UiCitySchemeWindow::DrawRightPane() {
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  if (!font)
    return 0;
  RsKey key{};
  {
    UiTextRef label(kRsSupplementalGameLandscapeText, kRsSupplementalGameTextGroup);
    if (label.IsResolved())
      AddTextLabelEffects(this, kUiCitySetupLandscapeLabelId, 232, 44, *label.GetResource(), font, false);
  }
  {
    UiTextRef label(kRsSupplementalGameTreesText, kRsSupplementalGameTextGroup);
    if (label.IsResolved())
      AddTextLabelEffects(this, kUiCitySetupTreesLabelId, 232, 106, *label.GetResource(), font, false);
  }
  {
    UiTextRef label(kRsSupplementalGameBuildingsText, kRsSupplementalGameTextGroup);
    if (label.IsResolved())
      AddTextLabelEffects(this, kUiCitySetupBuildingsLabelId, 232, 188, *label.GetResource(), font, false);
  }
  u32 count = schemes->GetNumLandScapes(), actual = 0;
  landscape_last = kUiCitySetupLandscapeChoiceLowerBoundId + count;
  if (count) {
    ScArray<RsKey> keys;
    landscape_ids = new u32[count];
    schemes->GetLandScapeIds(landscape_ids, count, &actual);
    for (u32 i = 0; i < actual; ++i) {
      schemes->GetLandScapeIconKey(landscape_ids[i], &key);
      *keys.Append() = key;
    }
    AddOptionButtons(&keys, kUiCitySetupFirstLandscapeChoiceId, 92, 43, GetButtonIdForLandScape(landscape));
  }
  count = schemes->GetNumFloraSets();
  flora_first = landscape_last + 1;
  flora_last = flora_first + count - 1;
  if (count) {
    ScArray<RsKey> keys;
    flora_ids = new u32[count];
    actual = 0;
    schemes->GetFloraSetIds(flora_ids, count, &actual);
    for (u32 i = 0; i < actual; ++i) {
      schemes->GetFloraSetIconKey(flora_ids[i], &key);
      *keys.Append() = key;
    }
    AddOptionButtons(&keys, flora_first, 166, 39, GetButtonIdForFloraSet(flora));
  }
  count = schemes->GetNumBuildingSets();
  buildings_first = flora_last + 1;
  buildings_last = buildings_first + count - 1;
  if (count) {
    ScArray<RsKey> keys;
    building_ids = new u32[count];
    actual = 0;
    schemes->GetBuildingSetIds(building_ids, count, &actual);
    for (u32 i = 0; i < actual; ++i) {
      schemes->GetBuildingSetIconKey(building_ids[i], &key);
      *keys.Append() = key;
    }
    AddOptionButtons(&keys, buildings_first, 249, 49, GetButtonIdForBuildingSet(buildings));
  }
  {
    UiTextRef label(kRsSupplementalGameUserMadeBuildingsText, kRsSupplementalGameTextGroup);
    if (label.IsResolved())
      AddTextLabelEffects(this, kUiCitySetupUserBuildingsLabelId, 304, 292, *label.GetResource(), font, false);
  }
  i32 right = 430;
  UiTextRef action_text(kRsSupplementalGameSelectText, kRsSupplementalGameTextGroup);
  ScString action_caption;
  if (action_text.GetResource())
    action_caption.Copy(*action_text.GetResource());
  WnWindow *label = nullptr;
  if (GetChildAs(kUiCitySetupUserBuildingsLabelId, IID_WnWindow, reinterpret_cast<void **>(&label))) {
    right = label->GetR();
    label->Release();
  }
  WnButton *button = nullptr;
  UiGetWindowResources()->CreateObject(kUiResourceCaptionButton, IID_WnButton, reinterpret_cast<void **>(&button));
  if (button) {
    WnWindow *window = button->GetWindow();
    window->SetID(kUiCitySetupSelectUserBuildingsId);
    window->SetCaption(action_caption);
    button->Fit();
    button->SetButtonFlag(kWnButtonAutoFit, false);
    if (window->GetW() < 78)
      window->SetSize(78, window->GetH());
    window->SetFlag(kWnFlagReorderable, false);
    window->GZWinMoveTo((i32)((u32)right + 5), 282);
    ChildAdd(window);
    button->Release();
  }
  return 1;
}

// Set the existing-city editor's color from the borrowed font before laying out the remaining controls.
u8 UiCitySchemeWindow::DrawLeftPane() {
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  ApNewCityPrefs *preferences = UiGetPreferences()->NewCityPrefs();
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  u32 text_color = resources->GetColor(kWnThemeTextColorId);
  ScString group_caption;
  ScArray<u32> button_ids;
  if (!font) {
    font = resources->GetFont(kWnFontDefault);
    if (!font)
      return 0;
  }
  if (mode) {
    {
      UiTextRef name_text(kRsGameNewCityText, kRsGameTextGroup);
      city_name = name_text.GetText();
    }
    {
      UiTextRef mayor_text(kRsGameDefactoText, kRsGameTextGroup);
      mayor_name = mayor_text.GetText();
    }
    if (preferences->ready && preferences->mayor_name[0]) {
      ScString saved_name(preferences->mayor_name);
      mayor_name = saved_name;
    }
    city_description.Erase(0, city_description.Strlen());
  } else {
    CtCity *existing = UiGetCity();
    if (existing) {
      {
        ScString name(*existing->GetCityName());
        city_name = name;
      }
      {
        ScString mayor(*existing->GetMayorName());
        mayor_name = mayor;
      }
      {
        ScString description(*existing->GetCityDescription());
        city_description = description;
      }
    }
  }
  {
    UiTextRef title(kRsGameCityNameText, kRsGameTextGroup);
    ScString label = title.GetText();
    AddTextLabelEffects(this, kUiCitySetupCityNameLabelId, 15, 36, label, font, false);
  }
  WnWindow *child = GetChildWindowFromID(kUiCitySetupCityNameLabelId);
  i32 y = 36 + (child ? child->GetH() : 0);
  AddOutlinedTextEdit(this, kUiCitySetupCityNameEditId, 23, (u32)y, 190, 19, city_name);
  child = GetChildWindowFromID(kUiCitySetupCityNameLabelId);
  y += (child ? child->GetH() : 0) + 2;
  {
    UiTextRef title(kRsGameMayorNameText, kRsGameTextGroup);
    ScString label = title.GetText();
    AddTextLabelEffects(this, kUiCitySetupMayorNameLabelId, 15, (u32)y, label, font, false);
  }
  child = GetChildWindowFromID(kUiCitySetupMayorNameLabelId);
  y += child ? child->GetH() : 0;
  AddOutlinedTextEdit(this, kUiCitySetupMayorNameEditId, 23, (u32)y, 190, 19, mayor_name);
  child = GetChildWindowFromID(kUiCitySetupMayorNameEditId);
  y += (child ? child->GetH() : 0) + 2;
  if (options[1]) {
    ScString amount;
    {
      UiTextRef title(kRsGameDifficultyLevelText, kRsGameTextGroup);
      group_caption = title.GetText();
    }
    ScRect group_area = {0, 0, 32, 32};
    WnOptionControl *group = resources->CreateOptGrp(kUiCitySetupDifficultyOptionsId, group_caption, &group_area);
    button_ids.Clear();
    group->SetButtonFont(font);
    group->SetFontColor(2, text_color);
    group->SetFontColor(3, text_color);
    group->SetFontColor(4, text_color);
    const u32 resource_ids[] = {0x22c, 0x22d, 0x22e};
    const i64 funds_values[] = {50000, 20000, 10000};
    for (u32 i = 0; i < 3; ++i) {
      ScString format;
      {
        UiTextRef text(resource_ids[i], kRsGameTextGroup);
        format = text.GetText();
      }
      UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeMoneyString(funds_values[i], &amount);
      group_caption.Sprintf(format.ToChar(), amount.ToChar());
      u32 id = kUiCitySetupEasyId + i;
      group->AddButton(id, group_caption, 0, nullptr, nullptr);
      *button_ids.Append() = id;
    }
    u32 selected = preferences->ready ? preferences->difficulty : kUiCitySetupEasyId;
    if (selected < kUiCitySetupEasyId || selected > kUiCitySetupHardId)
      selected = kUiCitySetupEasyId;
    group->SetValue(selected);
    if (!ChildAdd(group->GetWindow())) {
      UiGetWindowManager()->DestroyWindow(group->GetWindow());
      group->Release();
      return 0;
    }
    group->SetOptionFlag(1, 1);
    group->SetOptionFlag(8, 0);
    group->SetLabelFont(font);
    group->SetGutters(0, 0);
    group->GetWindow()->SetSize(1000, 1000);
    group->AutoSize();
    group->GetWindow()->GZWinMoveTo(15, y);
    UiOffsetOptionButtons(group, 8, &button_ids);
    y += group->GetWindow()->GetH();
    group->Release();
  }
  i32 option_height = 0;
  if (options[3]) {
    {
      UiTextRef title(kRsGameStartDateText, kRsGameTextGroup);
      group_caption = title.GetText();
    }
    ScRect group_area = {0, 0, 32, 32};
    WnOptionControl *group = resources->CreateOptGrp(kUiCitySetupStartDateOptionsId, group_caption, &group_area);
    button_ids.Clear();
    group->SetButtonFont(font);
    group->SetFontColor(2, text_color);
    group->SetFontColor(3, text_color);
    group->SetFontColor(4, text_color);
    for (u32 i = 0; i < 3; ++i) {
      UiTextRef text(0x230 + i, kRsGameTextGroup);
      ScString label = text.GetText();
      group->AddButton(kUiCitySetupStartYear1900Id + i, label, 0, nullptr, nullptr);
    }
    for (u32 i = 0; i < 3; ++i)
      *button_ids.Append() = kUiCitySetupStartYear1900Id + i;
    u32 selected = preferences->ready ? preferences->start_date : kUiCitySetupStartYear1900Id;
    if (selected < kUiCitySetupStartYear1900Id || selected > kUiCitySetupStartYear2000Id)
      selected = kUiCitySetupStartYear1900Id;
    group->SetValue(selected);
    if (!ChildAdd(group->GetWindow())) {
      UiGetWindowManager()->DestroyWindow(group->GetWindow());
      group->Release();
      return 0;
    }
    group->SetOptionFlag(1, 1);
    group->SetOptionFlag(8, 0);
    group->SetLabelFont(font);
    group->SetGutters(0, 0);
    group->GetWindow()->SetSize(1000, 1000);
    group->AutoSize();
    group->GetWindow()->GZWinMoveTo(15, y);
    UiOffsetOptionButtons(group, 8, &button_ids);
    i32 height = group->GetWindow()->GetH() + 2;
    if (height > option_height)
      option_height = height;
    group->Release();
  }
  if (options[2]) {
    {
      UiTextRef title(kRsGameCitySizeText, kRsGameTextGroup);
      group_caption = title.GetText();
    }
    ScRect group_area = {0, 0, 32, 32};
    WnOptionControl *group = resources->CreateOptGrp(kUiCitySetupCitySizeOptionsId, group_caption, &group_area);
    button_ids.Clear();
    group->SetButtonFont(font);
    group->SetFontColor(2, text_color);
    group->SetFontColor(3, text_color);
    group->SetFontColor(4, text_color);
    const u32 resource_ids[] = {0x2ce, 0x234, 0x2d3, 0x235};
    for (u32 i = 0; i < 4; ++i) {
      UiTextRef text(resource_ids[i], kRsGameTextGroup);
      ScString label = text.GetText();
      group->AddButton(kUiCitySetupCitySize64Id + i, label, 0, nullptr, nullptr);
    }
    for (u32 i = 0; i < 4; ++i)
      *button_ids.Append() = kUiCitySetupCitySize64Id + i;
    u32 selected = kUiCitySetupCitySize128Id;
    if (preferences->ready) {
      u32 saved = preferences->city_size;
      if (saved >= kUiCitySetupCitySize64Id && saved <= kUiCitySetupCitySize256Id)
        selected = saved;
    } else {
      i32 power = UiGetSystemPerformanceInfo()->processor_power;
      i32 memory = UiGetSystemPerformanceInfo()->memory_mb;
      if (power >= 5 && memory >= 60)
        selected = kUiCitySetupCitySize256Id;
      else if (power >= 3 && memory >= 45)
        selected = kUiCitySetupCitySize192Id;
      else if (power < 2 || memory < 30)
        selected = kUiCitySetupCitySize64Id;
    }
    group->SetValue(selected);
    if (!ChildAdd(group->GetWindow())) {
      UiGetWindowManager()->DestroyWindow(group->GetWindow());
      group->Release();
      return 0;
    }
    group->SetOptionFlag(1, 1);
    group->SetOptionFlag(8, 0);
    group->SetLabelFont(font);
    group->SetGutters(0, 0);
    group->GetWindow()->SetSize(1000, 1000);
    group->AutoSize();
    group->GetWindow()->GZWinMoveTo(100, y);
    UiOffsetOptionButtons(group, 8, &button_ids);
    i32 height = group->GetWindow()->GetH() + 2;
    if (height > option_height)
      option_height = height;
    group->Release();
  }
  y += option_height;
  if (mode == 0) {
    {
      UiTextRef title(kRsSupplementalGameDescriptionText, kRsSupplementalGameTextGroup);
      ScString label = title.GetText();
      AddTextLabelEffects(this, kUiCitySetupDescriptionLabelId, 15, (u32)y, label, font, false);
    }
    child = GetChildWindowFromID(kUiCitySetupDescriptionLabelId);
    y += child ? child->GetH() : 0;
    AddOutlineBox(this, 23, (u32)y, 190, 144);
    WnEditorControl *editor = nullptr;
    if (UiGetCom()->GetClassObject(kWnEditorClassId, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
      WnWindow *editor_window = editor->AsIGZWin();
      FtFont *editor_font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
      editor_window->SetID(kUiCitySetupDescriptionEditId);
      if (editor_font) {
        editor->SetFont(editor_font);
        editor->SetFontColor(editor_font->Color());
      }
      editor->SetTextGutterX(2);
      editor->SetTextGutterY(4);
      editor_window->SetArea(25, y + 2, 211, y + 142);
      editor->SetFlag(2);
      editor->SetFlag(1);
      editor->ClearFlag(16);
      editor->SetFlag(32);
      editor->SetText(city_description);
      if (ChildAdd(editor_window)) {
        WnResources *current_resources = UiGetWindowResources();
        u32 fill = current_resources->GetColor(kWnColorWhite);
        editor_window->SetFillColor(fill);
      } else
        GetWindowManager()->DestroyWindow(editor_window);
      editor->Release();
      child = GetChildWindowFromID(kUiCitySetupDescriptionEditId);
      y += child ? child->GetH() + 6 : 0;
    }
  }
  {
    UiTextRef text(kRsGameDisastersText, kRsGameTextGroup);
    ScString label = text.GetText();
    AddCheckBox(this, kUiCitySetupDisastersId, 17, (u32)y, disasters != 0, label);
  }
  child = GetChildWindowFromID(kUiCitySetupDisastersId);
  y += child ? child->GetH() : 0;
  i32 checkbox_height = child ? child->GetH() : 0;
  {
    UiTextRef text(kRsGameAutoBudgetText, kRsGameTextGroup);
    ScString label = text.GetText();
    AddCheckBox(this, kUiCitySetupAutoBudgetId, 17, (u32)y, auto_budget != 0, label);
  }
  child = GetChildWindowFromID(kUiCitySetupAutoBudgetId);
  checkbox_height += child ? child->GetH() + 2 : 0;
  assert(child);
  i32 bottom = child->GetB() + 2;
  if (bottom < 330)
    bottom = 330;
  word428 = (u32)bottom;
  y = bottom - checkbox_height;
  child = GetChildWindowFromID(kUiCitySetupDisastersId);
  if (child) {
    child->GZWinMoveTo(child->GetL(), y);
    y += child->GetH();
  }
  child = GetChildWindowFromID(kUiCitySetupAutoBudgetId);
  if (child)
    child->GZWinMoveTo(child->GetL(), y);
  return 1;
}

// Shift each existing option button and enlarge the group only if at least one requested button was found.
// Keep the live identifier range and three separate window lookups in their original callback order.
u8 UiOffsetOptionButtons(WnOptionControl *group, i32 offset, ScArray<u32> *identifiers) {
  if (!group || !identifiers->size)
    return 0;
  u32 *current = identifiers->data;
  WnButton *button = nullptr;
  u8 changed = 0;
  group->SetAutoFillType(1, 0);
  while (current != identifiers->data + identifiers->size) {
    if (group->GetButton(*current, &button)) {
      ScRect area;
      button->GetWindow()->GetArea(&area);
      area.left = (i32)((u32)area.left + (u32)offset);
      group->MoveButton(*current, area.left, area.top);
      button->Release();
      button = nullptr;
      changed = 1;
    }
    ++current;
  }
  if (changed) {
    WnWindow *height_window = group->GetWindow(), *width_window = group->GetWindow();
    WnWindow *size_window = group->GetWindow();
    i32 height = height_window->GetH();
    i32 width = width_window->GetW();
    size_window->SetSize((i32)((u32)width + (u32)offset), height);
  }
  return changed;
}

// Paint the generic window, then composite the dirty landscape, flora and building previews under one bits lock.
// Keep the original three masked placements and acquired builder holds; this body neither clears dirty nor releases them.
bool UiCitySchemeWindow::GZPaint() {
  GxImageBuilder *building = nullptr, *flora_builder = nullptr;
  UiGenericWindow::GZPaint();
  if (options[0] && draw_buffer->Lock(GxLockFlags::Bits)) {
    ScRect clip = *draw_buffer->GetBufferArea();
    if (landscape_image) {
      ScRect source = *landscape_image->GetBufferArea();
      ScRect target = {(i32)((u32)source.left + 444), (i32)((u32)source.top + 92), (i32)((u32)source.right + 444),
          (i32)((u32)source.bottom + 92)};
      landscape_image->BltSrc(draw_buffer, source, target, &clip);
    }
    if (flora_front && flora_back) {
      ScRect source = *flora_front->GetBufferArea();
      if (flora_front->QueryInterface(IID_GxImageBuilder, reinterpret_cast<void **>(&flora_builder)) && flora_builder) {
        i32 y = (i32)(222u - flora_front->Height());
        u32 width = flora_front->Width();
        ScPoint target = {(i32)((u32)source.left + 589u - (width >> 1)), (i32)((u32)source.top + (u32)y)};
        flora_builder->SpanBufferAlphaBlt(draw_buffer, flora_back, source, target, &clip);
      }
    }
    if (building_front && building_back) {
      ScRect source = *building_front->GetBufferArea();
      if (building_front->QueryInterface(IID_GxImageBuilder, reinterpret_cast<void **>(&building)) && building) {
        i32 y = (i32)(242u - building_front->Height());
        u32 width = building_front->Width();
        ScPoint target = {(i32)((u32)source.left + 600u - width), (i32)((u32)source.top + (u32)y)};
        building->SpanBufferAlphaBlt(draw_buffer, building_back, source, target, &clip);
      }
    }
    if (flora_builder) {
      ScRect source = *flora_front->GetBufferArea();
      i32 y = (i32)(242u - flora_front->Height());
      u32 width = flora_front->Width();
      ScPoint target = {(i32)((u32)source.left + 488u - (width >> 1)), (i32)((u32)source.top + (u32)y)};
      flora_builder->SpanBufferAlphaBlt(draw_buffer, flora_back, source, target, &clip);
    }
    draw_buffer->Unlock(GxLockFlags::Bits);
  }
  return true;
}

// Acquire the scheme and image services, then select the preference or live-city resource sets for the requested mode.
// Preserve the saved original selections independently of later edits; mode zero requires the source's existing city.
UiCitySchemeWindow::UiCitySchemeWindow(u32 requested_mode)
    : callback_references(0), mode(requested_mode), landscape_last(0), flora_first(0), flora_last(0),
      buildings_first(0), buildings_last(0), landscape_ids(nullptr), flora_ids(nullptr), building_ids(nullptr),
      funds(50000), funds_debt(0), difficulty(1), dirt_generator(nullptr), landscape_image(nullptr),
      building_front(nullptr), building_back(nullptr), flora_front(nullptr), flora_back(nullptr), images(nullptr),
      word428(0) {
  options[0] = 1;
  schemes = ScAddRefNotNull(UiGetApplication()->CitySchemeManager());
  images = ScAddRefNotNull(UiGetImageCache());
  options[1] = mode == 1 || mode == 2 || mode == 3;
  options[3] = mode == 1 || mode == 3;
  options[2] = mode == 1;
  if (mode) {
    ApSimSettings *settings = UiGetPreferences()->SimPrefs();
    SetLandScapeSelectionEffects(settings->landscape_id);
    SetFloraSelectionEffects(settings->flora_set_id);
    SetBuildingSelectionEffects(settings->building_set_id);
    auto_budget = settings->auto_budget;
    disasters = settings->enable_disasters;
  } else {
    CtCity *city = UiGetCity();
    assert(city);
    u32 id = 0;
    schemes->GetCurrentLandScape(&id);
    SetLandScapeSelectionEffects(id);
    schemes->GetCurrentFloraSet(&id);
    SetFloraSelectionEffects(id);
    schemes->GetCurrentBuildingSet(&id);
    SetBuildingSelectionEffects(id);
    auto_budget = city->IsAutoBudgetEnabled();
    disasters = city->AreDisastersEnabled();
  }
  saved_auto_budget = auto_budget;
  saved_disasters = disasters;
  saved_landscape = landscape;
  saved_flora = flora;
  saved_buildings = buildings;
}

// Retire the three ID allocations before the terrain generator, five preview buffers and two retained services.
UiCitySchemeWindow::~UiCitySchemeWindow() {
  delete[] landscape_ids;
  landscape_ids = nullptr;
  delete[] flora_ids;
  flora_ids = nullptr;
  delete[] building_ids;
  building_ids = nullptr;
  ScRelease(&dirt_generator);
  ScRelease(&landscape_image);
  ScRelease(&building_front);
  ScRelease(&building_back);
  ScRelease(&flora_front);
  ScRelease(&flora_back);
  ScRelease(&images);
  ScRelease(&schemes);
}

bool UiCitySchemeWindow::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  return UiGenericWindow::QueryInterface(iid, out);
}

u32 UiCitySchemeWindow::AddRef() {
  return UiGenericWindow::AddRef();
}

u32 UiCitySchemeWindow::Release() {
  return UiGenericWindow::Release();
}

u8 UiCitySchemeWindow::OnShutdown() {
  return 1;
}

// Prepare the dialog and its two panes, arrange the action strip, then center it within the active display area. Keep
// the original resource lifetime, message ordering and late window-style selection around the pane builders.
u8 UiCitySchemeWindow::OnInit() {
  PrivateBuffer(true);
  SetSize(639, 365);
  if (mode &&
      !UiGetCom()->GetClassObject(kTrGeneratorClassId, IID_TrGenerator, reinterpret_cast<void **>(&dirt_generator)))
    return 0;
  {
    ScString title_text;
    {
      UiTextRef title(mode ? 45 : kRsSupplementalGameCityOptionsText, kRsSupplementalGameTextGroup);
      ScString text = title.GetText();
      title_text = text;
    }
    SetCaption(title_text);
  }
  DrawRightPane();
  DrawLeftPane();
  i32 top = client.top, bottom = (i32)word428;
  GxBuffer *button_image;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef reference(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
    button_image = reference.GetBuffer();
  }
  i32 strip_height = (i32)(button_image->Height() + 2);
  ScRect panel_area = {(i32)border_x, top, (i32)border_x + 10, bottom - (i32)border_y};
  AddPanel(kUiCitySetupLeftBorderPanelId, 0, 15, &panel_area);
  panel_area = {218, top, 228, bottom - (i32)border_y};
  AddPanel(kUiCitySetupCenterBorderPanelId, 0, 15, &panel_area);
  i32 inset = border_x;
  panel_area = {inset, bottom, GetW() - inset, bottom + strip_height};
  AddPanel(kUiCitySetupAcceptButtonPanelId, 0, 15, &panel_area);
  WnWindow *strip = GetChildWindowFromID(kUiCitySetupAcceptButtonPanelId);
  if (strip) {
    i32 limit = GetH() - (i32)border_y;
    if (strip->GetB() > limit) {
      i32 height = strip->GetB() + (i32)border_y;
      i32 width = GetW();
      SetSize(width, height);
    }
  }
  WnButton *accept;
  {
    ScString empty;
    WnResources *resources = UiGetWindowResources();
    const ScRect *image_area = button_image->GetBufferArea();
    accept = resources->CreateButton(kUiCitySetupAcceptId, empty, button_image, image_area, 32);
  }
  if (accept) {
    WnWindow *window = accept->GetWindow();
    accept->SetButtonFlag(kWnButtonShowText, false);
    window->SetFlag(kWnFlagReorderable, false);
    strip = GetChildWindowFromID(kUiCitySetupAcceptButtonPanelId);
    if (strip) {
      i32 button_height = window->GetH();
      i32 y = (strip->GetH() - button_height) / 2 + strip->GetT();
      i32 width = window->GetW();
      i32 x = strip->GetR() - 4 - width;
      window->GZWinMoveTo(x, y);
    }
    ChildAdd(window);
    accept->Release();
  }
  i32 parent_width = GetParentWin()->GetW(), width = GetW();
  i32 x = (parent_width - width) / 2;
  i32 parent_height = GetParentWin()->GetH(), height = GetH();
  i32 y = (parent_height - height) / 2;
  bool windowed = false;
  ScMessage display_flags(kWnMsg_QueryUiVisibility);
  UiGetMessageServer()->MessageSend(display_flags);
  if (display_flags.data1 & 2) {
    windowed = true;
    ScMessage display(kWnMsg_QueryUiClientArea);
    UiGetMessageServer()->MessageSend(display);
    u32 first = (u32)display.data1, second = (u32)display.data2;
    i32 display_width = (i32)(second >> 16) - (i32)(first >> 16);
    i32 display_height = (i32)(second & 0xffff) - (i32)(first & 0xffff);
    if (display_width > GetW())
      x = (display_width - GetW()) / 2;
    if (display_height > GetH())
      y = (display_height - GetH()) / 2;
  }
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  GZWinMoveTo(x, y);
  SetStyle(kUiGenericDraggable, windowed);
  return 1;
}

// Route window notifications to the original initialization, button and edit handlers through the callback identity.
// Escape and Alt-F4 close the dialog; an event-three notification plays the original sound and remains unhandled.
bool UiCitySchemeWindow::DoWinMsg(WnWindow *, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if (first == kWnCommandButtonActivated)
      return OnBtnClicked((u32)second) != 0;
    if (first == kWnCommandOptionSelectionChanged) {
      UiGetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
      return false;
    }
    if (first == 8)
      return OnLineInputChanged((u32)second) != 0;
    return false;
  }
  if (message == kWnMsg_KeyDown) {
    if (first != 27 && (first != 115 || (second & 4) == 0))
      return false;
    EndDialogEffects(0);
    return true;
  }
  if (message == kWnMsg_Init)
    return OnInit() != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown() != 0;
  return false;
}

// Dispatch the three option ranges and dialog buttons, preserving the captured audio identity and notification sounds.
// The alternate command is acquired through the framework service and runs while the dialog is temporarily disabled.
u8 UiCitySchemeWindow::OnBtnClicked(u32 id) {
  ApBoxX *audio = UiGetBoxX();
  if (id >= kUiCitySetupFirstLandscapeChoiceId && id <= landscape_last) {
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    LandScapeBtnClickedEffects(id);
  } else if (id >= flora_first && id <= flora_last) {
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    FloraBtnClickedEffects(id);
  } else if (id >= buildings_first && id <= buildings_last) {
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    BuildingBtnClickedEffects(id);
  } else if (id == kUiCitySetupAcceptId) {
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    ReadValuesFromWindow();
    EndDialogEffects(1);
  } else if (id == kUiFrameCloseButtonId) {
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    EndDialogEffects(0);
  } else if (id == kUiCitySetupSelectUserBuildingsId) {
    if (audio)
      audio->Event(kAuEventPlayEffect, kAuEffectUiButtonClickSmall, 0, 0, 0);
    UiCommandInterface *command = nullptr;
    if (UiGetCommand(
            kUiReplacementManagerCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
      SetFlag(kWnFlagVisible, false);
      command->Execute();
      SetFlag(kWnFlagVisible, true);
      command->Release();
    }
  } else
    return 0;
  return 1;
}

// Read the enabled option groups into both the dialog state and new-city preferences, then update the terrain
// generator. Copy the mayor name to preferences before optional city-description editor readback.
u8 UiCitySchemeWindow::ReadValuesFromWindow() {
  ApNewCityPrefs *preferences = UiGetPreferences()->NewCityPrefs();
  WnOptionControl *group = nullptr;
  WnWindow *child;
  if (options[1]) {
    child = GetChildWindowFromID(kUiCitySetupDifficultyOptionsId);
    if (!child || !child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&group)))
      return 0;
    u32 selected = group->GetValue();
    preferences->difficulty = selected;
    if (selected == kUiCitySetupMediumId) {
      difficulty = 2;
      funds = 20000;
      funds_debt = 0;
    } else if (selected == kUiCitySetupHardId) {
      difficulty = 3;
      funds = 10000;
      funds_debt = 1;
    } else {
      difficulty = 1;
      funds = 50000;
      funds_debt = 0;
    }
    group->Release();
    group = nullptr;
    if (mode == 1 && dirt_generator)
      dirt_generator->SetDifficulty(difficulty);
  }
  if (options[3]) {
    child = GetChildWindowFromID(kUiCitySetupStartDateOptionsId);
    if (!child || !child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&group)))
      return 0;
    u32 selected = group->GetValue();
    preferences->start_date = selected;
    start_year = selected == kUiCitySetupStartYear1950Id ? 1950 : selected == kUiCitySetupStartYear2000Id ? 2000 : 1900;
    group->Release();
    group = nullptr;
  }
  if (options[2]) {
    child = GetChildWindowFromID(kUiCitySetupCitySizeOptionsId);
    if (!child || !child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&group)))
      return 0;
    u32 selected = group->GetValue();
    preferences->city_size = selected;
    city_size = selected == kUiCitySetupCitySize64Id ? 64
        : selected == kUiCitySetupCitySize128Id      ? 128
        : selected == kUiCitySetupCitySize192Id      ? 192
                                                     : 256;
    group->Release();
  }
  if (mode == 1 && !dirt_generator->IsReady() && dirt_generator->Init(city_size, city_size))
    dirt_generator->GenerateRandom(-1, 64, 64, 64, 36);
  u32 length = mayor_name.Strlen();
  if (length > 127)
    length = 127;
  ScString mayor_copy(mayor_name.Data(), length);
  const char *mayor_text = mayor_copy.ToChar();
  memcpy(preferences->mayor_name, mayor_text, strlen(mayor_text) + 1);
  preferences->ready = 1;
  WnButton *button = nullptr;
  if (GetChildAs(kUiCitySetupAutoBudgetId, IID_WnButton, reinterpret_cast<void **>(&button))) {
    auto_budget = button->IsChecked();
    button->Release();
  }
  if (GetChildAs(kUiCitySetupDisastersId, IID_WnButton, reinterpret_cast<void **>(&button))) {
    disasters = button->IsChecked();
    button->Release();
  }
  WnEditorControl *editor = nullptr;
  if (mode == 0 && GetChildAs(kUiCitySetupDescriptionEditId, IID_WnEditorControl, reinterpret_cast<void **>(&editor))) {
    editor->GetText(&city_description);
    editor->Release();
  }
  ApSimSettings *simulation = UiGetPreferences()->SimPrefs();
  if (saved_auto_budget != auto_budget)
    simulation->auto_budget = auto_budget;
  if (saved_disasters != disasters)
    simulation->enable_disasters = disasters;
  if (saved_landscape != landscape)
    simulation->landscape_id = landscape;
  if (saved_flora != flora)
    simulation->flora_set_id = flora;
  if (saved_buildings != buildings)
    simulation->building_set_id = buildings;
  return 1;
}

// Replace the selected landscape image using the original resource/group order and retained buffer identity.
void UiCitySchemeWindow::SetLandScapeSelectionEffects(u32 id) {
  u32 group = 0, resource = 0;
  SpImage *record = nullptr;
  if (landscape_image) {
    schemes->GetUiImageForLandScape(landscape, &group, &resource);
    images->UnprepareImage(resource, group);
    landscape_image->Release();
    landscape_image = nullptr;
  }
  landscape = id;
  schemes->GetUiImageForLandScape(id, &group, &resource);
  if (images->PrepareImage(resource, group, &record)) {
    landscape_image = record->buffer;
    if (landscape_image)
      landscape_image->AddRef();
  }
}

// Select the two flora preview images, preserving the source's asymmetric retirement of its first cached buffer.
void UiCitySchemeWindow::SetFloraSelectionEffects(u32 id) {
  u32 first_group = 0, first_resource = 0, second_group = 0, second_resource = 0;
  SpImage *record = nullptr;
  if (flora_front) {
    schemes->GetUiImageForFloraSet(flora, &first_group, &first_resource, &second_group, &second_resource);
    images->UnprepareImage(first_resource, first_group);
    flora_front = nullptr;
    if (flora_back) {
      images->UnprepareImage(second_resource, second_group);
      flora_back->Release();
      flora_back = nullptr;
    }
  }
  flora = id;
  if (schemes->GetUiImageForFloraSet(id, &first_group, &first_resource, &second_group, &second_resource) &&
      images->PrepareImage(first_resource, first_group, &record)) {
    flora_front = record->buffer;
    if (flora_front)
      flora_front->AddRef();
    if (images->PrepareImage(second_resource, second_group, &record)) {
      flora_back = record->buffer;
      if (flora_back)
        flora_back->AddRef();
    }
  }
}

// Replace the two building previews while preserving cache retirement before each live buffer release.
void UiCitySchemeWindow::SetBuildingSelectionEffects(u32 id) {
  u32 first_group = 0, first_resource = 0, second_group = 0, second_resource = 0;
  SpImage *record = nullptr;
  if (building_front) {
    schemes->GetUiImageForBuildingSet(buildings, &first_group, &first_resource, &second_group, &second_resource);
    images->UnprepareImage(first_resource, first_group);
    building_front->Release();
    building_front = nullptr;
    if (building_back) {
      images->UnprepareImage(second_resource, second_group);
      building_back->Release();
      building_back = nullptr;
    }
  }
  buildings = id;
  if (schemes->GetUiImageForBuildingSet(id, &first_group, &first_resource, &second_group, &second_resource) &&
      images->PrepareImage(first_resource, first_group, &record)) {
    building_front = record->buffer;
    if (building_front)
      building_front->AddRef();
    if (images->PrepareImage(second_resource, second_group, &record)) {
      building_back = record->buffer;
      if (building_back)
        building_back->AddRef();
    }
  }
}

// Find the landscape's option ordinal within the current inclusive ID range, falling back to its first button ID.
u32 UiCitySchemeWindow::GetButtonIdForLandScape(u32 id) const {
  i32 count = (i32)(landscape_last - kUiCitySetupFirstLandscapeChoiceId + 1);
  for (i32 i = 0; i < count; ++i)
    if (landscape_ids[i] == id)
      return kUiCitySetupFirstLandscapeChoiceId + (u32)i;
  return kUiCitySetupFirstLandscapeChoiceId;
}

// Map the selected flora resource through its live ID array without treating a missing resource as a new option.
u32 UiCitySchemeWindow::GetButtonIdForFloraSet(u32 id) const {
  i32 count = (i32)(flora_last - flora_first + 1);
  for (i32 i = 0; i < count; ++i)
    if (flora_ids[i] == id)
      return flora_first + (u32)i;
  return flora_first;
}

u32 UiCitySchemeWindow::GetButtonIdForBuildingSet(u32 id) const {
  i32 count = (i32)(buildings_last - buildings_first + 1);
  for (i32 i = 0; i < count; ++i)
    if (building_ids[i] == id)
      return buildings_first + (u32)i;
  return buildings_first;
}

void UiCitySchemeWindow::CauseRedrawEffects() {
  options[0] = 1;
  InvalidateSelf();
}

void UiCitySchemeWindow::EndDialogEffects(u32 result) {
  UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(static_cast<WnWindow *>(this)), result);
}

// Toggle only a successfully queried button, then retire that queried view; all advisory replies remain discarded.
void UiCitySchemeWindow::ToggleButtonEffects(u32 id, bool enabled) {
  WnButton *button = nullptr;
  if (GetChildAs(id, IID_WnButton, reinterpret_cast<void **>(&button))) {
    if (enabled)
      button->ToggleOn();
    else
      button->ToggleOff();
    button->Release();
  }
}

// Retoggle an unchanged landscape selection; otherwise turn off the old button before replacing the image and redrawing.
void UiCitySchemeWindow::LandScapeBtnClickedEffects(u32 id) {
  u32 previous = GetButtonIdForLandScape(landscape);
  if (previous == id) {
    ToggleButtonEffects(previous, true);
    return;
  }
  ToggleButtonEffects(previous, false);
  SetLandScapeSelectionEffects(landscape_ids[id - kUiCitySetupFirstLandscapeChoiceId]);
  ToggleButtonEffects(id, true);
  CauseRedrawEffects();
}

// Change the selected flora preview between the two ordered toggle operations, then mark the composite dirty.
void UiCitySchemeWindow::FloraBtnClickedEffects(u32 id) {
  u32 previous = GetButtonIdForFloraSet(flora);
  if (previous == id) {
    ToggleButtonEffects(previous, true);
    return;
  }
  ToggleButtonEffects(previous, false);
  SetFloraSelectionEffects(flora_ids[id - flora_first]);
  ToggleButtonEffects(id, true);
  CauseRedrawEffects();
}

// Replace the selected building preview while preserving the previous button's retirement and the final invalidation.
void UiCitySchemeWindow::BuildingBtnClickedEffects(u32 id) {
  u32 previous = GetButtonIdForBuildingSet(buildings);
  if (previous == id) {
    ToggleButtonEffects(previous, true);
    return;
  }
  ToggleButtonEffects(previous, false);
  SetBuildingSelectionEffects(building_ids[id - buildings_first]);
  ToggleButtonEffects(id, true);
  CauseRedrawEffects();
}

u8 UiCitySchemeWindow::OnLineInputChanged(u32 id) {
  WnWindow *child = GetChildWindowFromID(id);
  if (!child)
    return 0;
  WnTextControl *input = nullptr;
  if (!child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&input)))
    return 0;
  if (id == kUiCitySetupCityNameEditId)
    input->GetText(&city_name);
  else if (id == kUiCitySetupMayorNameEditId)
    input->GetText(&mayor_name);
  input->Release();
  return 1;
}

UiSelectCitySchemeCommand::UiSelectCitySchemeCommand() : dialog(nullptr) {
}

// Shut down the current dialog before releasing the freshly read live owner and retiring the inherited command state.
UiSelectCitySchemeCommand::~UiSelectCitySchemeCommand() {
  if (dialog) {
    dialog->Shutdown();
    dialog->Release();
    dialog = nullptr;
  }
}

// Clear the genuine output before checking the result identity, then retain that view or delegate to the command base.
bool UiSelectCitySchemeCommand::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  HANDLE_QUERY_INTERFACE(IID_UiCitySchemeSelection, UiCitySchemeSelection);
  return UiCommand::QueryInterface(iid, out);
}

u32 UiSelectCitySchemeCommand::AddRef() {
  return UiCommand::AddRef();
}

u32 UiSelectCitySchemeCommand::Release() {
  return UiCommand::Release();
}

// Consume three command inputs, retain the dialog, and run its callback-bound modal window.
// On acceptance publish the selected scheme and, for an existing city, apply the selected names and simulation flags.
bool UiSelectCitySchemeCommand::Execute() {
  CtSchemeManager *schemes = UiGetApplication()->CitySchemeManager();
  if (!schemes || InputQueueSize() != 3)
    return false;
  ScVariant *argument;
  PopFrontInput(&argument);
  u32 mode = *argument->AsUint32();
  argument->Release();
  argument = nullptr;
  PopFrontInput(&argument);
  argument->Release();
  argument = nullptr;
  PopFrontInput(&argument);
  argument->Release();
  dialog = ScAddRefNotNull(new UiCitySchemeWindow(mode));
  WnWindow *window = dialog->GetWindow();
  window->PrivateBuffer(true);
  dialog->SetCallback(static_cast<UiWindowCallback *>(dialog));
  u32 result = UiGetWindowManager()->DoModalWin(window);
  dialog->SetCallback(nullptr);
  if (result != 1)
    return false;
  schemes->SetCityScheme(dialog->landscape, dialog->flora, dialog->buildings, mode == 0);
  if (mode == 0) {
    CtCity *city = UiGetCity();
    if (city) {
      ScString *name = GetCityName();
      if (name)
        city->SetCityName(*name);
      name = GetMayorName();
      if (name)
        city->SetMayorName(*name);
      name = GetCityDescription();
      if (name)
        city->SetCityDescription(*name);
      city->SetAutoBudgetEnabled(IsAutoBudgetEnabled());
      city->SetDisastersEnabled(AreDisastersEnabled());
    }
  }
  return true;
}

u32 UiSelectCitySchemeCommand::GetSelectedLandScape() const {
  return dialog ? dialog->landscape : 0;
}

u32 UiSelectCitySchemeCommand::GetSelectedFloraSet() const {
  return dialog ? dialog->flora : 0;
}

u32 UiSelectCitySchemeCommand::GetSelectedBuildingSet() const {
  return dialog ? dialog->buildings : 0;
}

// Borrow the dialog's embedded city-name String without copying or retaining it; absence returns a null pointer.
ScString *UiSelectCitySchemeCommand::GetCityName() const {
  return dialog ? &dialog->city_name : nullptr;
}

// Borrow the embedded mayor-name String independently of the other city-name fields.
ScString *UiSelectCitySchemeCommand::GetMayorName() const {
  return dialog ? &dialog->mayor_name : nullptr;
}

// Borrow the embedded description String, preserving its live identity for callers of the result interface.
ScString *UiSelectCitySchemeCommand::GetCityDescription() const {
  return dialog ? &dialog->city_description : nullptr;
}

i64 UiSelectCitySchemeCommand::GetFunds() const {
  return dialog ? dialog->funds : 50000;
}

u8 UiSelectCitySchemeCommand::AreFundsDebt() const {
  return dialog ? dialog->funds_debt : 0;
}

i32 UiSelectCitySchemeCommand::GetDifficulty() const {
  return dialog ? dialog->difficulty : 1;
}

u32 UiSelectCitySchemeCommand::GetStartYear() const {
  return dialog ? dialog->start_year : 1900;
}

u32 UiSelectCitySchemeCommand::GetCitySize() const {
  return dialog ? dialog->city_size : 256;
}

u8 UiSelectCitySchemeCommand::IsAutoBudgetEnabled() const {
  return dialog ? dialog->auto_budget : 1;
}

u8 UiSelectCitySchemeCommand::AreDisastersEnabled() const {
  return dialog ? dialog->disasters : 0;
}

// Borrow the retained terrain-generator interface without acquiring another reference; no dialog returns null.
TrGeneratorInterface *UiSelectCitySchemeCommand::GetDirtGenerator() const {
  return dialog ? dialog->dirt_generator : nullptr;
}
