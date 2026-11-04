#include "ui/status_windows.h"

#include <assert.h>

#include <cmath>

#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_unknown.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "resources/resource.h"
#include "ui/commands.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"

// Time-control hints retain the original complete flag combination sent with the hint request.
enum UiTimeHintPolicy : u32 {
  kUiTimeControlHintFlags = 0x1000003,
};

// The date and cash panel uses this image for its query button.
enum UiDateCashButtonArtwork : u32 {
  kUiDateCashQueryButtonImageId = 0x52fb06ea,
};

// The date/cash bar draws a fixed panel and tiles a separate middle strip across the remaining width.
enum UiDateCashArtwork : u32 {
  kUiDateCashPanelImageId = 0x82790741,
  kUiDateCashMiddleTileImageId = 0x82790742,
};

// Time controls use separate selected, hover and disabled pause/play images and a contiguous speed-selector atlas.
enum UiTimeArtwork : u32 {
  kUiPauseSelectedImageId = 0x52fb06e0,
  kUiPauseHoveredImageId = 0x52fb06e1,
  kUiPauseDisabledImageId = 0x52fb06eb,
  kUiPlaySelectedImageId = 0x52fb06e2,
  kUiPlayHoveredImageId = 0x52fb06e3,
  kUiPlayDisabledImageId = 0x52fb06ec,
  kUiSpeedSelectorFirstImageId = 0x52fb06e4,
  kUiDemandGraphAtlasImageId = 0x42fb77f6,
};

// Pause/play controls track selection, pointer hover and the enabled/visible halves of their blink state.
enum UiTimeButtonState : u32 {
  kUiTimeButtonSelected = 1,
  kUiTimeButtonHovered = 2,
  kUiTimeButtonBlinkEnabled = 4,
  kUiTimeButtonBlinkVisible = 8,
};

enum UiDateCashControlId : u32 {
  kUiDateCashQueryButtonId = 0x52fb5feb,
  kUiDateCashPauseButtonId = 0x52fb5fec,
  kUiDateCashPlayButtonId = 0x52fb5fed,
  kUiDateCashSpeedControlId = 0x52fb5fee,
  kUiDateCashTickerId = 0x52fb5fef,
};

// Allocate the title-bar window and return its primary Unknown view to the module class registry.
ScUnknown *UiCreateClass02F95D57() {
  return static_cast<UiBaseWindow *>(new UiDateCashWindow);
}

UiDateCashWindow::UiDateCashWindow()
    : images{}, image_areas{}, display_areas{}, words384{}, words408{}, font(nullptr), font_color(0), year(3000),
      day_of_year(1) {
}

UiDateCashWindow::~UiDateCashWindow() {
}

// Publish the date/cash/title identity when requested and delegate other identities to the window base.
bool UiDateCashWindow::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiDateCashWindowInterface)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiDateCashWindowInterface *>(this);
  AddRef();
  return true;
}

u32 UiDateCashWindow::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiDateCashWindow::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window without changing the owner reference count.
WnWindow *UiDateCashWindow::AsIGZWin() {
  return this;
}

// Execute the title-bar city command after acquiring its factory, and play the source click sound on success.
bool UiDateCashWindow::OnCommand(u32 kind, u32 command_id) {
  if (kind != kWnCommandButtonActivated || command_id != kUiDateCashQueryButtonId || !UiGetCommandFactory())
    return false;
  ScCommandFactory *factory = nullptr;
  UiGetFramework()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  assert(factory);
  UiCommandInterface *command = nullptr;
  factory->GetCommand(kGeLocationToolClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command));
  if (command) {
    if (UiGetApplication()) {
      ApBoxX *box = UiGetBoxX();
      if (box)
        box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    }
    command->Execute();
    command->Release();
    factory->Release();
    return true;
  }
  factory->Release();
  return false;
}

// Acquire and retain an RGB16 title-bar image through the module's original resource reference.
static bool UiLoadDateCashImage(u32 group, u32 instance, GxBuffer **out) {
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(instance, group, GxVramRequest::SystemMemory, format);
  *out = image.GetBuffer();
  if (!*out)
    return false;
  (*out)->AddRef();
  return true;
}

// Build the original display images, time controls and newscast before subscribing to city changes.
// Initialization failure follows the original shutdown path after service acquisition succeeds.
bool UiDateCashWindow::Init() {
  if (GetInitFlag())
    return true;
  UiBaseWindow::Init();
  GxGraphics *graphics = UiGetGraphics();
  ScMessageServer *server = UiGetMessageServer();
  WnResources *resources = UiGetWindowResources();
  UiImageRef button_image(kUiDateCashQueryButtonImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory,
      GxColorType(GxColorType::Color::RGB16));
  UiTextRef prefix(kRsGuiPopText, kRsGuiTextGroup);
  ScMessage paused_message(kCtMsg_Paused);
  if (!graphics || !server || !resources)
    return false;
  if (prefix.IsResolved())
    prefix.CopyTo(&title);
  title.Append(" ", 1);
  UiDateCashLayout layout;
  if (!GetLayoutInfoEffects(&layout)) {
    Shutdown();
    return false;
  }
  if (!UiLoadDateCashImage(layout.image_group, kUiDateCashPanelImageId, &images[0])) {
    Shutdown();
    return false;
  }
  images[0]->GetBufferArea(&image_areas[0]);
  display_areas[0] = image_areas[0];
  SetSize(image_areas[0].right - image_areas[0].left, image_areas[0].bottom - image_areas[0].top);
  if (!UiLoadDateCashImage(kRsMainUiImageGroup, kUiDateCashMiddleTileImageId, &images[1])) {
    Shutdown();
    return false;
  }
  const ScRect fields[] = {layout.title, layout.population, layout.date, layout.cash};
  for (u32 i = 0; i < 4; ++i) {
    u32 index = i + 2;
    display_areas[index] = fields[i];
    image_areas[index] = {0, 0, fields[i].right - fields[i].left, fields[i].bottom - fields[i].top};
    words384[index] = fields[i].left;
    words408[index] = fields[i].top;
  }
  images[1]->GetBufferArea(&image_areas[1]);
  graphics->CreateBuffer(&images[2]);
  graphics->CreateBuffer(&images[3]);
  graphics->CreateBuffer(&images[5]);
  graphics->CreateBuffer(&images[4]);
  if (!images[4] || !images[5] || !images[2] || !images[3]) {
    Shutdown();
    return false;
  }
  const u32 order[] = {2, 3, 5, 4};
  for (u32 index : order) {
    ScRect &field = image_areas[index];
    if (!images[index]->Init((u32)(field.right - field.left), (u32)(field.bottom - field.top),
            GxColorType(GxColorType::Color::RGB16), nullptr, GxDepthType(GxDepthType::Depth::None),
            GxVramRequest::SystemMemory, false)) {
      Shutdown();
      return false;
    }
  }
  if (images[2]->Lock(GxLockFlags::Fill)) {
    images[2]->Fill(0, nullptr);
    images[2]->Unlock(GxLockFlags::Fill);
  }
  for (u32 index : order)
    images[index]->SetSurfaceRestoreFunction(RestoreField, this, reinterpret_cast<void *>((uptr)index));

  UiPauseTogglePause *pause = new UiPauseTogglePause;
  pause->AddRef();
  pause->SetID(kUiDateCashPauseButtonId);
  pause->GZWinMoveTo(layout.pause.x, layout.pause.y);
  ChildAdd(pause);
  pause->DoMessage(&paused_message);
  pause->Release();
  UiPauseTogglePlay *play = new UiPauseTogglePlay;
  play->AddRef();
  play->SetID(kUiDateCashPlayButtonId);
  play->GZWinMoveTo(layout.play.x, layout.play.y);
  ChildAdd(play);
  play->DoMessage(&paused_message);
  play->Release();
  UiSpeedWindow *speed = new UiSpeedWindow;
  speed->AddRef();
  speed->SetID(kUiDateCashSpeedControlId);
  speed->GZWinMoveTo(layout.speed.x, layout.speed.y);
  ChildAdd(speed);
  speed->DoMessage(&paused_message);
  speed->Release();

  font = resources->GetFont(kWnFontUiSmallAlias);
  if (!font)
    font = resources->GetFont(kWnFontDefault);
  if (!font) {
    Shutdown();
    return false;
  }
  font->AddRef();
  font_color = MakeFillColor(31, 41, 136);
  GxBuffer *button_buffer = button_image.GetBuffer();
  if (button_buffer) {
    UiTextRef hint(kRsGuiQueryText, kRsGuiTextGroup);
    ScString tooltip, empty;
    WnButton *button =
        resources->CreateFixedButton(kUiDateCashQueryButtonId, empty, button_buffer, button_buffer->GetBufferArea(), 0);
    if (!button) {
      Shutdown();
      return false;
    }
    WnWindow *button_window = button->GetWindow();
    button->SetButtonFlag(kWnButtonShowText, false);
    button->SetButtonFlag(kWnButtonShowHint, true);
    button->SetTipPlacementType(kWnTipAboveCenter);
    if (hint.IsResolved())
      tooltip = hint.GetText();
    button->SetTipText(tooltip);
    button_window->GZWinMoveTo(layout.button.x, layout.button.y);
    ChildAdd(button_window);
    button->Release();
  }
  ApNewscastControl *newscast = nullptr;
  if (UiGetCom()->GetClassObject(
          kApNewscastControlClassId, IID_ApNewscastControl, reinterpret_cast<void **>(&newscast))) {
    WnWindow *ticker = newscast->AsIGZWin();
    ticker->SetID(kUiDateCashTickerId);
    ticker->SetFlag(kWnFlagReorderable, false);
    ticker->SetFillColor(0);
    ticker->SetW(layout.center.right - layout.center.left);
    ChildAdd(ticker);
    i32 height = ticker->GetH();
    ticker->GZWinMoveTo(
        layout.center.left, layout.center.top + (layout.center.bottom - layout.center.top - height) / 2);
    newscast->Release();
  }
  const u32 notifications[] = {kApMsgPostCityChange, kCtMsg_DayChanged, kCtMsg_MonthChanged, kEcMsg_PopulationChanged,
      kEcMsg_FundsChanged, kCtMsg_CityNameChanged};
  for (u32 message : notifications)
    server->AddNotification(this, message);
  if (!OnMonthChangedEffects()) {
    Shutdown();
    return false;
  }
  return true;
}

// Select the original title-bar resource group and field geometry from the main window width.
u8 UiDateCashWindow::GetLayoutInfoEffects(UiDateCashLayout *layout) {
  WnWindowManager *manager = GetWindowManager();
  if (!manager)
    return 0;
  WnWindow *window = manager->GetMainWindow();
  if (!window)
    return 0;
  i32 width = window->GetW();
  if (width == 640) {
    *layout = {kRsCompactUiImageGroup, {8, 46, 340, 64}, {8, 25, 116, 43}, {120, 25, 270, 43}, {264, 25, 350, 43},
        {8, 3, 340, 23}, {371, 14}, {348, 14}, {399, 14}, {349, 34}};
    return 1;
  }
  if (width == 800) {
    *layout = {kRsMediumUiImageGroup, {8, 26, 144, 44}, {146, 26, 258, 44}, {260, 26, 408, 44}, {396, 26, 500, 44},
        {8, 5, 481, 22}, {511, 9}, {488, 9}, {547, 11}, {489, 29}};
    return 1;
  }
  if (width > 800) {
    *layout = {kRsLargeUiImageGroup, {8, 26, 366, 44}, {372, 26, 480, 44}, {486, 26, 630, 44}, {630, 26, 712, 44},
        {8, 6, 704, 23}, {735, 10}, {712, 10}, {773, 9}, {713, 29}};
    return 1;
  }
  return 0;
}

// Restore one lost display image by redrawing its matching title-bar field.
bool UiDateCashWindow::RestoreField(void *owner, void *field) {
  if (!owner)
    return false;
  UiDateCashWindow *window = static_cast<UiDateCashWindow *>(owner);
  switch ((uptr)field) {
  case 2:
    return window->UpdateTitleEffects(nullptr) != 0;
  case 3:
    return window->UpdatePopulationEffects(nullptr) != 0;
  case 4:
    return window->UpdateDateEffects() != 0;
  case 5:
    return window->UpdateCashEffects(nullptr) != 0;
  default:
    return false;
  }
}

// Move a rectangle to a new origin while retaining its original wrapped dimensions.
static void UiDateCashMoveRect(ScRect *rectangle, i32 x, i32 y) {
  u32 dx = (u32)x - (u32)rectangle->left, dy = (u32)y - (u32)rectangle->top;
  rectangle->right = (i32)((u32)rectangle->right + dx);
  rectangle->bottom = (i32)((u32)rectangle->bottom + dy);
  rectangle->left = x;
  rectangle->top = y;
}

// Hide an initialized title bar, unsubscribe its six messages and release its font and six images.
bool UiDateCashWindow::Shutdown() {
  if (!GetFlag(kWnFlagInitialized))
    return true;
  HideWindow();
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->RemoveNotification(this, kApMsgPostCityChange);
    server->RemoveNotification(this, kCtMsg_DayChanged);
    server->RemoveNotification(this, kCtMsg_MonthChanged);
    server->RemoveNotification(this, kEcMsg_PopulationChanged);
    server->RemoveNotification(this, kEcMsg_FundsChanged);
    server->RemoveNotification(this, kCtMsg_CityNameChanged);
  }
  if (font)
    font->Release();
  font = nullptr;
  for (u32 index = 0; index < 6; ++index) {
    if (images[index])
      images[index]->Release();
    images[index] = nullptr;
  }
  return UiBaseWindow::Shutdown();
}

// Apply root geometry and reposition the six display areas relative to the new draw origin.
void UiDateCashWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  UiBaseWindow::SetArea(left, top, right, bottom);
  UiDateCashMoveRect(&display_areas[0], draw_area.left, draw_area.top);
  UiDateCashMoveRect(
      &display_areas[2], (i32)((u32)draw_area.left + words384[2]), (i32)((u32)draw_area.top + words408[2]));
  UiDateCashMoveRect(
      &display_areas[3], (i32)((u32)draw_area.left + words384[3]), (i32)((u32)draw_area.top + words408[3]));
  UiDateCashMoveRect(
      &display_areas[5], (i32)((u32)draw_area.left + words384[5]), (i32)((u32)draw_area.top + words408[5]));
  UiDateCashMoveRect(
      &display_areas[4], (i32)((u32)draw_area.left + words384[4]), (i32)((u32)draw_area.top + words408[4]));
  display_areas[1] = {display_areas[0].right, draw_area.top, draw_area.right, draw_area.bottom};
}

// Route title, date, population and funds notifications to their respective field updates.
bool UiDateCashWindow::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kEcMsg_FundsChanged:
    return UpdateCashEffects(nullptr) != 0;
  case kEcMsg_PopulationChanged:
    return UpdatePopulationEffects(nullptr) != 0;
  case kCtMsg_DayChanged:
    year = (u32)message->data1;
    day_of_year = (u32)message->data2;
    return UpdateDateEffects() != 0;
  case kApMsgPostCityChange:
    return OnCityChangedEffects() != 0;
  case kCtMsg_CityNameChanged:
    return UpdateTitleEffects(nullptr) != 0;
  case kCtMsg_MonthChanged:
    year = (u32)message->data1;
    day_of_year = (u32)message->data2;
    return OnMonthChangedEffects() != 0;
  default:
    return UiBaseWindow::DoMessage(message);
  }
}

// Tile the bar's middle image from right to left, then draw the fixed title, date and cash images.
bool UiDateCashWindow::GZPaint() {
  assert(draw_buffer);
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  if (display_areas[1].left < draw_area.right) {
    i32 width = (i32)((u32)image_areas[1].right - (u32)image_areas[1].left);
    assert(width > 0);
    ScRect destination{(i32)((u32)draw_area.right - (u32)width), draw_area.top, draw_area.right, draw_area.bottom};
    do {
      draw_buffer->Blt(images[1], image_areas[1], destination, nullptr);
      destination.left = (i32)((u32)destination.left - (u32)width);
      destination.right = (i32)((u32)destination.right - (u32)width);
    } while (destination.right > display_areas[1].left);
  }
  draw_buffer->Blt(images[0], image_areas[0], display_areas[0], nullptr);
  draw_buffer->Blt(images[2], image_areas[2], display_areas[2], nullptr);
  draw_buffer->Blt(images[3], image_areas[3], display_areas[3], nullptr);
  draw_buffer->Blt(images[5], image_areas[5], display_areas[5], nullptr);
  draw_buffer->Blt(images[4], image_areas[4], display_areas[4], nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

static void UiDateCashTruncateEffects(ScString *text, i32 width, FtFont *font) {
  u32 length = text->ScString::Strlen();
  u32 fitted = font->Fit(text->ScString::Data(), length, (u32)width);
  if (!fitted || fitted == length)
    return;
  WnResources *resources = UiGetWindowResources();
  ScString fallback("...");
  const ScStringView *suffix = resources ? resources->GetText(kWnEllipsisTextId) : nullptr;
  if (!suffix)
    suffix = &fallback;
  u32 suffix_length = suffix->Strlen();
  const char *suffix_bytes = suffix->Data();
  i32 suffix_width = font->Measure(suffix_bytes, suffix_length);
  if (suffix_width >= width) {
    text->Erase(0, text->ScString::Strlen());
    return;
  }
  fitted = font->Fit(text->ScString::Data(), text->ScString::Strlen(), (u32)width - (u32)suffix_width);
  text->Erase(fitted - 1, text->ScString::Strlen() - fitted + 1);
  u32 retained = text->ScString::Strlen();
  while (retained && text->ScString::Data()[retained - 1] == ' ')
    --retained;
  assert(retained || !text->ScString::Strlen());
  if (retained != text->ScString::Strlen())
    text->Erase(retained, text->ScString::Strlen() - retained);
  suffix_length = suffix->Strlen();
  suffix_bytes = suffix->Data();
  text->Append(suffix_bytes, suffix_length);
}

// Restore a field's background and draw its centered, truncated text in the requested color.
u8 UiDateCashWindow::UpdateTextEffects(i32 index, FtFont *text_font, const ScStringView *text, u32 color) {
  if (!text_font)
    return 0;
  assert((u32)index < 6);
  if (!images[index] || !images[index]->Lock(GxLockFlags::Blt))
    return 0;
  ScRect source = display_areas[index];
  source.left = (i32)((u32)source.left - (u32)draw_area.left);
  source.right = (i32)((u32)source.right - (u32)draw_area.left);
  source.top = (i32)((u32)source.top - (u32)draw_area.top);
  source.bottom = (i32)((u32)source.bottom - (u32)draw_area.top);
  images[index]->Blt(images[0], source, image_areas[index], nullptr);
  images[index]->Unlock(GxLockFlags::Blt);
  assert(text);
  ScString rendered(*text);
  i32 available_width = (i32)images[index]->Width();
  UiDateCashTruncateEffects(&rendered, available_width, text_font);
  ScRect extent;
  text_font->TextArea(rendered.begin, rendered.ScString::Strlen(), &extent);
  i32 text_height = (i32)((u32)extent.bottom - (u32)extent.top);
  i32 text_width = (i32)((u32)extent.right - (u32)extent.left);
  GxBuffer *image = images[index];
  i32 height = (i32)image->Height();
  i32 width = (i32)image->Width();
  i32 x = (i32)((u32)width - (u32)text_width) / 2;
  i32 y = (i32)((u32)height - (u32)text_height) / 2;
  text_font->DrawTextA(images[index], x, y, rendered.begin, rendered.ScString::Strlen(), color);
  return 1;
}

// Draw the current city's borrowed name, or an empty title when there is no city.
u8 UiDateCashWindow::UpdateTitleEffects(CtCity *city) {
  if (!city)
    city = UiGetCity();
  if (city) {
    u32 color = font_color;
    ScStringView *name = city->GetCityName();
    return UpdateTextEffects(2, font, name, color);
  }
  ScString empty;
  return UpdateTextEffects(2, font, &empty, font_color);
}

// Format the current city funds and use the original red color for a negative balance.
// Failed city or language acquisition leaves the source's empty field text in place.
u8 UiDateCashWindow::UpdateCashEffects(CtCity *city) {
  ScString text;
  if (!city)
    city = UiGetCity();
  if (city) {
    ScLanguageManager *language = UiGetLanguageManager();
    ScLanguageUtility *utility = language ? language->GetLanguageUtility(kScLanguageCurrent) : nullptr;
    if (utility) {
      EcBudgetLayer *budget = city->BudgetLayer();
      u32 color = font_color;
      if (budget) {
        i64 funds = budget->GetTotalFunds();
        utility->MakeMoneyString(funds, &text);
        if (funds < 0)
          color = MakeFillColor(255, 0, 0);
      } else
        text.Erase(0, text.Strlen());
      return UpdateTextEffects(5, font, &text, color);
    }
  }
  return UpdateTextEffects(5, font, &text, font_color);
}

// Decode the cached ordinal date and ask the language utility for its numeric month/day/year spelling.
// A missing language manager or formatter returns the source's observed zero byte.
u8 UiDateCashWindow::UpdateDateEffects() {
  ScLanguageManager *language = UiGetLanguageManager();
  if (!language)
    return 0;
  ScLanguageUtility *utility = language->GetLanguageUtility(kScLanguageCurrent);
  if (!utility)
    return 0;
  ScString text;
  CtDate date(day_of_year, year);
  u32 calendar_year = date.Year();
  u32 calendar_day = date.DayOfMonth();
  u32 calendar_month = date.Month();
  utility->MakeDateString(
      (i32)calendar_month, (i32)calendar_day, calendar_year, &text, ScLanguageUtility::DateFormat::NumericMonthDayYear);
  return UpdateTextEffects(4, font, &text, font_color);
}

// Format the residential supply and add the title-bar prefix for noncompact languages.
// Release the queried valve and the original layer view in the source's order.
u8 UiDateCashWindow::UpdatePopulationEffects(CtCity *city) {
  ScString text;
  i32 population = 0;
  if (!city)
    city = UiGetCity();
  if (city) {
    CtLayer *layer = city->GetSpecificLayer(kEcDemandLayerType);
    EcValveLayer *valve_layer = nullptr;
    if (layer && layer->QueryInterface(IID_EcValveLayer, reinterpret_cast<void **>(&valve_layer))) {
      EcValve *valve = nullptr;
      if (valve_layer->GetValvePointer(3000, IID_EcValve, reinterpret_cast<void **>(&valve))) {
        population = valve->QuerySupplyValue();
        valve->Release();
      }
      layer->Release();
    }
  }
  ScLanguageManager *language = UiGetLanguageManager();
  assert(language);
  ScLanguageUtility *utility = language->GetLanguageUtility(kScLanguageCurrent);
  u32 language_id = language->GetCurrentLanguage();
  if (utility)
    utility->MakeNumberString(population, &text);
  if (language_id != kScLanguageJapanese && language_id != kScLanguageSimplifiedChinese &&
      language_id != kScLanguageTraditionalChinese && language_id != kScLanguageKorean &&
      language_id != kScLanguageThai)
    text.Replace(0, 0, title.begin, title.ScString::Strlen());
  return UpdateTextEffects(3, font, &text, font_color);
}

// Refresh the date, population and funds in source order even if an earlier field reports failure.
i32 UiDateCashWindow::OnMonthChangedEffects() {
  CtCity *city = UiGetCity();
  u8 date_ok = UpdateDateEffects();
  u8 population_ok = UpdatePopulationEffects(city);
  u8 cash_ok = UpdateCashEffects(city);
  return date_ok != 0 && population_ok != 0 && cash_ok != 0;
}

// Refresh the calendar from the current city, then update title and monthly fields without short-circuiting.
i32 UiDateCashWindow::OnCityChangedEffects() {
  CtCity *city = UiGetCity();
  if (city)
    city->GetDate(&day_of_year, &year);
  else {
    day_of_year = 1;
    year = 3000;
  }
  u8 title_ok = UpdateTitleEffects(city);
  i32 month_ok = OnMonthChangedEffects();
  return title_ok != 0 && month_ok != 0;
}

UiRciWindow::UiRciWindow() : atlas(nullptr), bars{}, colors{}, scale(0.0f), lower_origin(0), upper_origin(0) {
}

// Allocate the demand meter and return its primary window identity without retaining it.
ScUnknown *UiCreateClass12FAF305() {
  return static_cast<WnWindow *>(new UiRciWindow);
}

UiRciWindow::~UiRciWindow() {
}

// Publish the demand-meter identity and retain it; all other interface requests belong to the window base.
bool UiRciWindow::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiRciWindowInterface)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiRciWindowInterface *>(this);
  AddRef();
  return true;
}

u32 UiRciWindow::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiRciWindow::Release() {
  return UiBaseWindow::Release();
}

// Borrow the primary window without acquiring another reference.
WnWindow *UiRciWindow::AsIGZWin() {
  return this;
}

bool UiRciWindow::Shutdown() {
  HideWindow();
  ScMessageServer *server = UiGetMessageServer();
  if (server && GetFlag(kWnFlagInitialized))
    server->RemoveNotification(this, kEcMsg_DemandChanged);
  ScRelease(&atlas);
  return true;
}

// Keep the requested origin and size the meter to its atlas, or to four pixels when none is present.
void UiRciWindow::SetArea(i32 left, i32 top, i32, i32) {
  GxBuffer *image = atlas;
  i32 width = 4, height = 4;
  if (image) {
    height = (i32)image->Height();
    width = (i32)image->Width();
  }
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
}

// Measure and draw one resolved localized bar label with the font's explicit color overload.
// Reload its borrowed bytes and length after measurement before issuing the draw.
static void UiRciDrawLabel(
    FtFont *font, GxBuffer **atlas, UiTextRef *reference, const ScRect *bar, i32 baseline, i32 offset, u32 color) {
  const ScStringView *text = reference->GetResource();
  if (text) {
    u32 length = text->Strlen();
    const char *bytes = text->Data();
    i32 measured = font->Measure(bytes, length);
    i32 x = bar->left + (bar->right - bar->left - measured) / 2 + offset;
    length = text->Strlen();
    bytes = text->Data();
    font->DrawTextA(*atlas, x, baseline, bytes, length, color);
  }
}

// Load the demand-meter atlas, label its three bars and subscribe to local demand updates.
bool UiRciWindow::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return false;
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef atlas_ref(kUiDemandGraphAtlasImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  atlas = atlas_ref.GetBuffer();
  if (!atlas)
    return false;
  atlas->AddRef();
  atlas->SetTransparency(resources->GetColor(kWnColorUiTransparency));
  GxBuffer *image = atlas;
  i32 height = (i32)image->Height(), width = (i32)image->Width();
  SetSize(width, height);
  bars[0] = {5, 0, 10, 0};
  bars[1] = {14, 0, 19, 0};
  bars[2] = {23, 0, 28, 0};
  scale = 16.0f;
  lower_origin = 51;
  upper_origin = 62;
  colors[0] = MakeFillColor(0, 192, 0);
  colors[1] = MakeFillColor(0, 0, 192);
  colors[2] = MakeFillColor(192, 192, 0);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  if (!font)
    font = resources->GetFont(kWnFontDefault);
  if (font) {
    i32 baseline = lower_origin + (upper_origin - lower_origin) / 2 - font->Height(0) / 2;
    u32 color = MakeFillColor(31, 41, 136);
    u32 language = UiGetLanguageManager()->GetCurrentLanguage();
    if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
        language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai) {
      u32 previous = font->Color();
      if (font->SetColor(color)) {
        i32 measured = font->Measure("R", 1);
        font->DrawTextA(atlas, bars[0].left + (bars[0].right - measured - bars[0].left) / 2, baseline, "R", 1);
        measured = font->Measure("C", 1);
        font->DrawTextA(atlas, bars[1].left + (bars[1].right - bars[1].left - measured) / 2, baseline, "C", 1);
        measured = font->Measure("I", 1);
        font->DrawTextA(atlas, bars[2].left + (bars[2].right - bars[2].left - measured) / 2 + 1, baseline, "I", 1);
        font->SetColor(previous);
      }
    } else {
      UiTextRef r(kRsGuiRText, kRsGuiTextGroup);
      UiRciDrawLabel(font, &atlas, &r, &bars[0], baseline, 0, color);
      UiTextRef c(kRsGuiCText, kRsGuiTextGroup);
      UiRciDrawLabel(font, &atlas, &c, &bars[1], baseline, 0, color);
      UiTextRef i(kRsGuiIText, kRsGuiTextGroup);
      UiRciDrawLabel(font, &atlas, &i, &bars[2], baseline, 1, color);
    }
  }
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->AddNotification(this, kEcMsg_DemandChanged);
  SetFlag(kWnFlagPrivateBuffer, true);
  SetFlag(kWnFlagTransparentBuffer, true);
  return UiBaseWindow::Init();
}

// Update the appropriate demand bar when a local RCI notification arrives.
// Pass other messages to the inherited window handler.
bool UiRciWindow::DoMessage(ScMessage *message) {
  if (message->type == kEcMsg_DemandChanged) {
    u32 index;
    switch ((u32)message->data1) {
    case 3000:
      index = 0;
      break;
    case 5000:
      index = 1;
      break;
    case 8000:
      index = 2;
      break;
    default:
      return true;
    }
    f32 value = (f32)((f64)(i32)(u32)message->data2 * (f64)0.001f);
    UiRciUpdateRectangleEffects(&bars[index], value, scale, lower_origin, upper_origin);
    InvalidateSelfAndParents();
    return true;
  }
  return UiBaseWindow::DoMessage(message);
}

// Clear the keyed private buffer, draw the meter atlas and fill each nonempty demand bar.
// The destination and atlas must exist on the original dereference paths.
bool UiRciWindow::GZPaint() {
  FillPrivateBufferWithTransparentColor();
  assert(draw_buffer);
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return true;
  assert(atlas);
  const ScRect *source = atlas->GetBufferArea();
  draw_buffer->Blt(atlas, *source, draw_area, nullptr);
  for (u32 index = 0; index < 3; ++index) {
    const ScRect &bar = bars[index];
    if (bar.bottom != bar.top) {
      ScRect destination{(i32)((u32)bar.left + (u32)draw_area.left), (i32)((u32)bar.top + (u32)draw_area.top),
          (i32)((u32)bar.right + (u32)draw_area.left), (i32)((u32)bar.bottom + (u32)draw_area.top)};
      draw_buffer->Fill(colors[index], destination, nullptr);
    }
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Apply the defined effects of the source's private bar-rectangle calculation.
void UiRciUpdateRectangleEffects(ScRect *bar, f32 value, f32 scale, i32 lower, i32 upper) {
  if (value == 0.0f || std::isnan(value)) {
    bar->top = 0;
    bar->bottom = 0;
    return;
  }
  long double product = (long double)value * scale;
  assert(std::isfinite(product) && product >= -9223372036854775808.0L && product < 9223372036854775808.0L);
  u32 delta = (u32)(i64)product;
  if (value > 0.0f) {
    bar->bottom = lower;
    bar->top = (i32)((u32)lower - delta);
    if (bar->top == bar->bottom)
      bar->top = (i32)((u32)bar->top - 1);
  } else {
    bar->top = upper;
    bar->bottom = (i32)((u32)upper - delta);
    if (bar->bottom == bar->top)
      bar->bottom = (i32)((u32)bar->bottom + 1);
  }
}

i32 UiPlayControlSound(i32 sound);

// Retain one title-control image after its scoped RGB16 resource reference goes out of scope.
static bool UiLoadTimeImage(u32 instance, GxBuffer **out) {
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef resource(instance, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  *out = resource.GetBuffer();
  if (!*out)
    return false;
  (*out)->AddRef();
  return true;
}

static void UiCancelTimeHint(u32 token) {
  ScMessage message(kWnMsg_StopHint, token, 0, 0);
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->MessageSend(message);
}

static void UiShowTimeHint(u32 *token, const ScStringView *text, const ScRect &rectangle) {
  WnHint hint;
  hint.text = text;
  hint.rectangle = rectangle;
  hint.flags |= kUiTimeControlHintFlags;
  ScMessage message(kWnMsg_StartHint, (uptr)&hint, 0, 0);
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->MessageSend(message);
  *token = hint.token;
}

UiPauseToggle::UiPauseToggle()
    : images{nullptr, nullptr, nullptr}, state(0), hint_token(0), stopwatch(ScStopwatch::Milliseconds, 0),
      blink_time(0) {
}

UiPauseToggle::~UiPauseToggle() {
}

// Subscribe to pause and modal notifications before restarting the blink clock and initializing the window.
bool UiPauseToggle::Init() {
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    const u32 messages[] = {kCtMsg_Paused, kCtMsg_Unpaused, kCtMsg_DisasterStarted, kCtMsg_DisasterEnded,
        kApMsgPostCityChange, kCtMsg_SetSimulationControlsEnabled};
    for (u32 message : messages)
      server->AddNotification(this, message);
  }
  stopwatch.Restart();
  return UiBaseWindow::Init();
}

// Hide the control, remove its live subscriptions, release its three images and clear its state.
bool UiPauseToggle::Shutdown() {
  HideWindow();
  ScMessageServer *server = UiGetMessageServer();
  if (server && GetInitFlag()) {
    const u32 messages[] = {kCtMsg_Paused, kCtMsg_Unpaused, kCtMsg_DisasterStarted, kCtMsg_DisasterEnded,
        kApMsgPostCityChange, kCtMsg_SetSimulationControlsEnabled};
    for (u32 message : messages)
      server->RemoveNotification(this, message);
  }
  for (GxBuffer *&image : images) {
    ScRelease(&image);
  }
  state = 0;
  return UiBaseWindow::Shutdown();
}

// Draw the disabled, hovered or selected image, advancing the selected image's 500 ms blink when requested.
// Hidden blink phases skip the blit while preserving the same buffer lock and unlock sequence.
bool UiPauseToggle::GZPaint() {
  bool enabled = GetFlag(kWnFlagEnabled);
  if (enabled && !state)
    return true;
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  GxBuffer *image = nullptr;
  bool draw = true;
  if (!enabled)
    image = images[2];
  else if (state & kUiTimeButtonHovered)
    image = images[1];
  else if (state & kUiTimeButtonSelected) {
    if (state & kUiTimeButtonBlinkEnabled) {
      u32 elapsed = stopwatch.GetElapsedTime();
      if (elapsed > blink_time) {
        blink_time = elapsed + 500;
        state ^= kUiTimeButtonBlinkVisible;
      }
      if (state & kUiTimeButtonBlinkVisible)
        image = images[0];
      else
        draw = false;
    } else
      image = images[0];
  } else
    draw = false;
  if (draw)
    draw_buffer->Blt(image, *image->GetBufferArea(), draw_area, nullptr);
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

bool UiPauseToggle::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiPauseToggle::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Start the hover hint and capture once on entry; on exit clear the hover state and release capture.
bool UiPauseToggle::OnMouseMove(i32 x, i32 y, u32) {
  if (IsPointInWindow(x, y)) {
    if (!(state & kUiTimeButtonHovered)) {
      state |= kUiTimeButtonHovered;
      UiShowTimeHint(&hint_token, &caption, absolute_area);
      owner->SetCapture(this);
    }
  } else if (state & kUiTimeButtonHovered) {
    state &= ~kUiTimeButtonHovered;
    GZReleaseCapture();
  }
  return true;
}

bool UiPauseToggle::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    state &= ~kUiTimeButtonHovered;
    UiCancelTimeHint(hint_token);
  }
  return true;
}

// Load the pause tooltip and three images before sizing the window and subscribing to notifications.
bool UiPauseTogglePause::Init() {
  if (GetInitFlag())
    return true;
  state |= kUiTimeButtonBlinkEnabled;
  UiTextRef text(kRsGuiPauseSimulationText, kRsGuiTextGroup);
  if (text.IsResolved())
    text.CopyTo(&caption);
  if (UiLoadTimeImage(kUiPauseSelectedImageId, &images[0]) && UiLoadTimeImage(kUiPauseHoveredImageId, &images[1]) &&
      UiLoadTimeImage(kUiPauseDisabledImageId, &images[2])) {
    u32 height = images[0]->Height(), width = images[0]->Width();
    SetSize(width, height);
    modal_disabled = false;
    return UiPauseToggle::Init();
  }
  Shutdown();
  return false;
}

// Track pause and modal notifications, forwarding messages outside this control's subscriptions to its base.
bool UiPauseTogglePause::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kCtMsg_Paused:
    state |= kUiTimeButtonSelected;
    return true;
  case kCtMsg_Unpaused:
    state &= ~kUiTimeButtonSelected;
    return true;
  case kCtMsg_DisasterStarted:
    SetFlag(kWnFlagEnabled, false);
    return true;
  case kCtMsg_SetSimulationControlsEnabled:
    modal_disabled = message->data1 == 0;
    SetFlag(kWnFlagEnabled, !modal_disabled);
    return true;
  case kCtMsg_DisasterEnded:
  case kApMsgPostCityChange:
    if (!modal_disabled)
      SetFlag(kWnFlagEnabled, true);
    return true;
  default:
    return UiBaseWindow::DoMessage(message);
  }
}

// Cancel the hint and request a pause when the city or the control is not already paused.
bool UiPauseTogglePause::OnMouseDownL(i32, i32, u32) {
  UiCancelTimeHint(hint_token);
  ApGameInterface *application = UiGetApplication();
  CtCity *city = application ? application->GetCity() : nullptr;
  if (city) {
    if (city->IsPaused()) {
      if (!(state & kUiTimeButtonSelected)) {
        UiPlayControlSound(24);
        state |= kUiTimeButtonSelected;
      }
    } else {
      UiPlayControlSound(24);
      state |= kUiTimeButtonSelected;
      ScMessageServer *server = UiGetMessageServer();
      if (server) {
        ScMessage message(kCtMsg_RequestPause);
        server->MessagePost(message, false);
      }
    }
  }
  return true;
}

// Load the play tooltip and three images before sizing the window and subscribing to notifications.
bool UiPauseTogglePlay::Init() {
  if (GetInitFlag())
    return true;
  UiTextRef text(kRsGuiRunSimulationText, kRsGuiTextGroup);
  if (text.IsResolved())
    text.CopyTo(&caption);
  if (UiLoadTimeImage(kUiPlaySelectedImageId, &images[0]) && UiLoadTimeImage(kUiPlayHoveredImageId, &images[1]) &&
      UiLoadTimeImage(kUiPlayDisabledImageId, &images[2])) {
    u32 height = images[0]->Height(), width = images[0]->Width();
    SetSize(width, height);
    return UiPauseToggle::Init();
  }
  Shutdown();
  return false;
}

// Invert the selected play state on pause notifications and delegate all other messages.
bool UiPauseTogglePlay::DoMessage(ScMessage *message) {
  if (message->type == kCtMsg_Paused) {
    state &= ~kUiTimeButtonSelected;
    return true;
  }
  if (message->type == kCtMsg_Unpaused) {
    state |= kUiTimeButtonSelected;
    return true;
  }
  return UiBaseWindow::DoMessage(message);
}

// Cancel the hint and request play for a paused city, retaining the original separate server lookups.
bool UiPauseTogglePlay::OnMouseDownL(i32, i32, u32) {
  UiCancelTimeHint(hint_token);
  ApGameInterface *application = UiGetApplication();
  CtCity *city = application ? application->GetCity() : nullptr;
  if (city) {
    if (city->IsPaused()) {
      if (!(state & kUiTimeButtonSelected)) {
        UiPlayControlSound(23);
        state |= kUiTimeButtonSelected;
      }
      if (UiGetMessageServer()) {
        ScMessage message(kCtMsg_RequestUnpause);
        UiGetMessageServer()->MessagePost(message, false);
      }
    } else {
      UiPlayControlSound(23);
      state |= kUiTimeButtonSelected;
    }
  }
  return true;
}

UiSpeedWindow::UiSpeedWindow()
    : paused(0), disabled(0), selection(0), hovered(-1), hit_areas{}, images{}, hint_token(0), hints{} {
}

// Load the selector images and four localized hints, lay out its hit areas and subscribe to city notifications.
bool UiSpeedWindow::Init() {
  if (GetInitFlag())
    return true;
  for (u32 i = 0; i < 6; ++i)
    if (!UiLoadTimeImage(kUiSpeedSelectorFirstImageId + i, &images[i]))
      return false;
  u32 height = images[0]->Height(), width = images[0]->Width();
  SetSize(width, height);
  UiTextRef first(kRsGuiTurtleText, kRsGuiTextGroup), second(kRsGuiLlamaText, kRsGuiTextGroup),
      third(kRsGuiCheetahText, kRsGuiTextGroup), fourth(kRsGuiAfricanSwallowText, kRsGuiTextGroup);
  UiTextRef *texts[] = {&first, &second, &third, &fourth};
  for (u32 i = 0; i < 4; ++i) {
    ScString *hint = new ScString;
    hints[i] = hint;
    hint->AddRef();
    if (texts[i]->IsResolved())
      texts[i]->CopyTo(hint);
  }
  i32 image_width = (i32)images[4]->Width(), image_height = (i32)images[4]->Height() + 5;
  i32 left = 4;
  for (ScRect &hit : hit_areas) {
    hit = {left, 5, left + image_width, image_height};
    left += image_width;
  }
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    const u32 messages[] = {kCtMsg_GameSpeedChanged, kCtMsg_Paused, kCtMsg_Unpaused, kCtMsg_DisasterStarted,
        kCtMsg_DisasterEnded, kApMsgPostCityChange, kCtMsg_SetSimulationControlsEnabled};
    for (u32 message : messages)
      server->AddNotification(this, message);
  }
  modal_disabled = false;
  return UiBaseWindow::Init();
}

// Hide the selector, release images and strings, and remove subscriptions only if it was initialized.
bool UiSpeedWindow::Shutdown() {
  HideWindow();
  for (GxBuffer *&image : images) {
    if (image)
      image->Release();
    image = nullptr;
  }
  for (ScStringView *&hint : hints) {
    ScRelease(&hint);
  }
  ScMessageServer *server = UiGetMessageServer();
  if (server && GetInitFlag()) {
    const u32 messages[] = {kCtMsg_GameSpeedChanged, kCtMsg_Paused, kCtMsg_Unpaused, kCtMsg_DisasterStarted,
        kCtMsg_DisasterEnded, kApMsgPostCityChange, kCtMsg_SetSimulationControlsEnabled};
    for (u32 message : messages)
      server->RemoveNotification(this, message);
  }
  return UiBaseWindow::Shutdown();
}

// Apply speed, pause and modal notifications in their original order, delegating other messages.
bool UiSpeedWindow::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kCtMsg_GameSpeedChanged:
    if ((u32)message->data1 >= 1 && (u32)message->data1 <= 4)
      selection = (i8)((u32)message->data1 - 1);
    return true;
  case kCtMsg_Paused:
    paused = 1;
    return true;
  case kCtMsg_Unpaused:
    paused = 0;
    return true;
  case kCtMsg_DisasterStarted:
    disabled = 1;
    SetFlag(kWnFlagEnabled, false);
    return true;
  case kCtMsg_SetSimulationControlsEnabled:
    SetFlag(kWnFlagEnabled, message->data1 != 0);
    modal_disabled = message->data1 == 0;
    return true;
  case kCtMsg_DisasterEnded:
  case kApMsgPostCityChange:
    disabled = 0;
    if (!modal_disabled)
      SetFlag(kWnFlagEnabled, true);
    return true;
  default:
    return UiBaseWindow::DoMessage(message);
  }
}

// Paint the speed image or disabled segments, then overlay every hovered segment in the original order.
bool UiSpeedWindow::GZPaint() {
  if (!GetFlag(kWnFlagEnabled) && !(modal_disabled && !disabled))
    return true;
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  if (paused || disabled || modal_disabled) {
    ScRect destination = hit_areas[0];
    destination.left += draw_origin.x;
    destination.right += draw_origin.x;
    destination.top += draw_origin.y;
    destination.bottom += draw_origin.y;
    i32 width = destination.right - destination.left;
    const ScRect *source = images[5]->GetBufferArea();
    for (i32 i = 0; i <= selection; ++i) {
      draw_buffer->Blt(images[5], *source, destination, nullptr);
      destination.left += width;
      destination.right += width;
    }
  } else {
    i32 index = selection >= 0 && selection <= 3 ? selection : 0;
    draw_buffer->Blt(images[index], *images[index]->GetBufferArea(), draw_area, nullptr);
  }
  if (hovered != -1) {
    ScRect destination = hit_areas[0];
    i32 width = destination.right - destination.left;
    const ScRect *source = images[4]->GetBufferArea();
    destination.left += draw_origin.x;
    destination.right += draw_origin.x;
    destination.top += draw_origin.y;
    destination.bottom += draw_origin.y;
    for (i32 i = 0; i <= hovered; ++i) {
      draw_buffer->Blt(images[4], *source, destination, nullptr);
      destination.left += width;
      destination.right += width;
    }
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

bool UiSpeedWindow::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiSpeedWindow::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

// Select the clicked speed, post its one-based value and play the corresponding control sound when it changes.
bool UiSpeedWindow::OnMouseDownL(i32 x, i32 y, u32) {
  UiCancelTimeHint(hint_token);
  i8 previous = selection;
  for (i32 i = 0; i < 4; ++i) {
    const ScRect &hit = hit_areas[i];
    if (x >= hit.left && y >= hit.top && x <= hit.right && y <= hit.bottom) {
      selection = (i8)i;
      break;
    }
  }
  if (selection != previous) {
    u32 speed = selection >= 1 && selection <= 3 ? selection + 1 : 1;
    ScMessageServer *server = UiGetMessageServer();
    if (server) {
      ScMessage message(kCtMsg_SetGameSpeed, speed, 0, 0);
      server->MessagePost(message, false);
    }
    UiPlayControlSound((i32)speed + 24);
  }
  return true;
}

// Track the hovered speed and request its hint, preserving the modal-disabled capture behavior.
bool UiSpeedWindow::OnMouseMove(i32 x, i32 y, u32) {
  if (!modal_disabled)
    hovered = -1;
  if (IsPointInWindow(x, y)) {
    for (i32 i = 0; i < 4; ++i) {
      const ScRect &hit = hit_areas[i];
      if (x >= hit.left && y >= hit.top && x <= hit.right && y <= hit.bottom) {
        if (!modal_disabled)
          hovered = (i8)i;
        UiShowTimeHint(&hint_token, hints[i], absolute_area);
        break;
      }
    }
    if (!modal_disabled)
      owner->SetCapture(this);
  } else {
    UiCancelTimeHint(hint_token);
    if (!modal_disabled)
      GZReleaseCapture();
  }
  return true;
}

// Reset the highlighted section and cancel its hint after capture changes to another window.
bool UiSpeedWindow::GZOnCaptureChanged(WnWindow *window) {
  if (window != this) {
    hovered = -1;
    UiCancelTimeHint(hint_token);
  }
  return true;
}
