#include "ui/chart_views.h"

#include <assert.h>
#include <string.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_error.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/sc3_graphics.h"
#include "misc/api.h"
#include "resources/resource.h"
#include "ui/controls.h"
#include "ui/graph_series.h"
#include "ui/graph_window.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"

// The history-label position removes the double-integer low-word bias while retaining its ten-pixel offset.
enum UiHistoryLabelBias : u32 {
  kUiHistoryLabelConversionBias = 0x8000000a,
};

// Each legend outline is addressed by adding this offset to its matching label ID.
enum UiGraphLegendId : u32 {
  kUiGraphLegendOutlineIdOffset = 0xbaabaa,
};

// Return the newly constructed chart view's primary maps-view identity without querying it or adding a reference.
UiMapsViewInterface *UiChartsView::CreateChartsView() {
  return new UiChartsView;
}

UiChartsView::UiChartsView() = default;

// Retire the chart-view identities before the shared maps-view state is destroyed.
UiChartsView::~UiChartsView() = default;

// Configure the chart view's area, identifier, name and icon before creating its shared view.
// Failed setup leaves earlier changes in place and returns zero.
u8 UiChartsView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  u32 identifier = 0x604;
  if ((u8)IsReady() || !SetViewArea(area) || !SetViewId(identifier) || !SetNameResource(kRsGameTextGroup, 0x183) ||
      !SetIconButton(0x5e5, 3) || !SetIconButtonLocation(location))
    return 0;
  return UiMapsView::CreateView(view_owner, area, location);
}

// Remove the chart notification and persist the selected chart before shutting down the shared view.
u8 UiChartsView::OnShutdown(WnWindow *receiver) {
  UiGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kCtMsg_MonthChanged);
  WnWindow *child = receiver->GetChildWindowFromID(5);
  WnOptionControl *options = nullptr;
  if (child && child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&options))) {
    ApPreferences *preferences = UiGetPreferences();
    u32 selected = options->GetValue();
    preferences->ChartsPrefs()->selection = selected;
    options->Release();
    UiGetPreferences()->ChartsPrefs()->ready = 1;
  }
  return UiMapsView::OnShutdown(receiver);
}

// Update the chart for its subscribed city notification when the shared window exists.
bool UiChartsView::DoMessage(ScMessage *message) {
  if (message->type != kCtMsg_MonthChanged)
    return false;
  if (window)
    UpdateViewEffects(window->GetWindow());
  return true;
}

u8 UiChartsView::OnButtonActivated(WnWindow *receiver, u32 identifier) {
  if (identifier != kUiFrameCloseButtonId)
    return 0;
  EndDialog(receiver, 1);
  return 1;
}

// Route lifecycle messages and chart controls to their original handlers, playing the selection sound on changes.
bool UiChartsView::DoWinMsg(WnWindow *receiver, u32 message, uptr first, uptr second, uptr third) {
  if (message == kWnMsg_Init)
    return OnInit(receiver) != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown(receiver) != 0;
  if (message == kWnMsg_Command && (u32)first == kWnCommandButtonActivated) {
    if (OnButtonActivated(receiver, (u32)second))
      return true;
  } else if (message == kWnMsg_Command && (u32)first == kWnCommandOptionSelectionChanged) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    UpdateViewEffects(receiver);
    return true;
  }
  return UiMapsView::DoWinMsg(receiver, message, first, second, third);
}

// Query the chart option group, draw the selected chart and release its interface.
void UiChartsView::UpdateViewEffects(WnWindow *receiver) {
  WnWindow *child = receiver->GetChildWindowFromID(5);
  WnOptionControl *options = nullptr;
  if (!child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&options)))
    return;
  switch (options->GetValue()) {
  case 6:
    DrawEducationChartEffects(receiver);
    break;
  case 7:
    DrawElectricityChartEffects(receiver);
    break;
  case 8:
    DrawGarbageChartEffects(receiver);
    break;
  case 9:
    DrawHealthChartEffects(receiver);
    break;
  case 10:
    DrawPopulationChartEffects(receiver);
    break;
  }
  options->Release();
}

void SC3_STDCALL UiChartsSetTextEffects(WnWindow *receiver, u32 identifier, const ScStringView &text) {
  WnLabelControl *label = nullptr;
  if (receiver->GetChildAs(identifier, IID_WnLabelControl, reinterpret_cast<void **>(&label))) {
    label->SetCaption(text);
    label->Release();
  }
}

bool UiChartsView::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiChartsViewIdentity, UiChartsViewIdentity);
  return UiMapsView::QueryInterface(iid, out);
}

u32 UiChartsView::AddRef() {
  return UiMapsView::AddRef();
}

u32 UiChartsView::Release() {
  return UiMapsView::Release();
}

u8 UiChartsView::PrepareHideView() {
  return 1;
}

u8 UiChartsView::PrepareShowView() {
  return 1;
}

u8 UiChartsView::EnumCityItems(CtCity *, bool) {
  return 1;
}

u8 UiChartsView::RefreshData(CtCity *) {
  return 1;
}

bool UiChartsView::Enumerate(ScMessage *, CtLayer *) {
  return true;
}

// Construct the utility chart's bar graph, set its single series colors and attach it to the view window.
u8 UiChartsView::CreateBarGraph(WnWindow *parent, u32 identifier, const ScString &) {
  UiBarGraphWindow *graph_window = new UiBarGraphWindow;
  graph_window->AddRef();
  graph_window->SetID(identifier);
  graph_window->SetSize(356, 189);
  UiBarGraph *graph = static_cast<UiBarGraph *>(graph_window);
  graph->SetBarGraphType(static_cast<UiBarGraphType>(1));
  graph->SetGraphOrientation(static_cast<UiBarOrientation>(0));
  graph->InsertSeries(kUiAppendGraphSeries);
  graph->SetBarColorUse(static_cast<UiGraphUsage>(1), static_cast<UiGraphUsage>(1), kUiAllGraphSeries);
  graph->SetBarFillColor(chart_colors[15], 0);
  graph->SetBarOutlineColor(chart_colors[16], 0);
  graph_window->GZWinMoveTo(14, 10);
  u8 result = parent->ChildAdd(graph_window);
  if (!result)
    UiGetWindowManager()->DestroyWindow(graph_window);
  graph_window->Release();
  return result;
}

// Create the five chart-type options and restore a valid saved selection after attaching the option group.
u8 SC3_STDCALL UiChartsCreateOptions(WnWindow *parent) {
  ScString empty("");
  ScRect area{150, 260, 270, 410};
  WnOptionControl *options = UiGetWindowResources()->CreateOptGrp(5, empty, &area);
  assert(options != nullptr);
  static const u32 text_ids[5] = {0x1dd, 0x1e0, 0x1e1, 0x1de, 0x1df};
  static const u32 button_ids[5] = {6, 9, 10, 7, 8};
  for (u32 index = 0; index < 5; ++index) {
    UiTextRef reference(text_ids[index], kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    ScString caption(copy);
    options->AddButton(button_ids[index], caption, 0, nullptr, nullptr);
  }
  options->SetOptionFlag(1, 0);
  options->SetOptionFlag(8, 0);
  if (!parent->ChildAdd(options->GetWindow())) {
    WnWindowManager *manager = UiGetWindowManager();
    manager->DestroyWindow(options->GetWindow());
    options->Release();
    return 0;
  }
  options->GetWindow()->GZWinMoveTo(150, 260);
  options->GetWindow()->SetH(150);
  options->GetWindow()->SetW(120);
  u32 selected = 6;
  if (UiGetPreferences()->ChartsPrefs()->ready == 1) {
    u32 saved = UiGetPreferences()->ChartsPrefs()->selection;
    if (saved >= 6 && saved <= 10)
      selected = saved;
  }
  options->SetValue(selected);
  options->Release();
  return 1;
}

// Create a hidden chart label, choosing white or black text when its background color is enabled.
u8 SC3_STDCALL UiChartsCreateLabel(WnWindow *parent, u32 identifier, i32 x, i32 y, i32 width, i32 height,
    const ScStringView &caption, u32 background, u32 color) {
  WnLabelControl *label = UiGetWindowResources()->CreateLabel(identifier, caption);
  assert(label != nullptr);
  label->GetWindow()->SetID(identifier);
  if ((u8)background == 1) {
    label->GetWindow()->SetFillColor(color);
    label->SetBackground(true);
    u8 red = (u8)(background >> 24), green = (u8)(identifier >> 24), blue = (u8)((uptr)&caption >> 24);
    UiGetGraphics()->Convert16BitNativeValueToRGB((u16)color, &red, &green, &blue);
    GxGraphics *graphics = UiGetGraphics();
    u8 value = (u32)red + green + blue <= 384 ? 255 : 0;
    label->SetTextColor(graphics->ConvertRGBValueToNative16Bit(value, value, value));
  }
  label->GetWindow()->SetSize(width, height);
  label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  label->GetWindow()->GZWinMoveTo(x, y);
  label->GetWindow()->HideWindow();
  u8 result = parent->ChildAdd(label->GetWindow());
  if (!result) {
    WnWindowManager *manager = UiGetWindowManager();
    manager->DestroyWindow(label->GetWindow());
  }
  label->Release();
  return result;
}

u8 SC3_STDCALL UiChartsCreateGraphOutline(WnWindow *parent) {
  WnOutlineControl *outline = nullptr;
  u8 result = UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline));
  if (result && outline) {
    outline->SetFlags(kWnOutlineInset | kWnOutlineFill);
    WnResources *resources = UiGetWindowResources();
    u32 color = resources->GetColor(kWnThemeBackgroundColorId);
    outline->SetFillColor(color);
    outline->GetWindow()->SetArea(ScRect{0, 0, 356, 189});
    outline->GetWindow()->SetID(4);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    outline->GetWindow()->GZWinMoveTo(14, 10);
    parent->ChildAdd(outline->GetWindow());
    outline->Release();
    return 1;
  }
  return result;
}

static bool UiChartsCompactLanguage(u32 language) {
  return language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
}

static void UiChartsAppendSample(UiGraphFloatVector *vector, f32 value) {
  u32 size = vector->begin ? (u32)(vector->end - vector->begin) : 0;
  u32 capacity = vector->begin ? (u32)(vector->storage_end - vector->begin) : 0;
  if (size == capacity) {
    u32 next = capacity ? capacity * 2 : 1;
    if (next < capacity)
      ScFatal(SC_ERROR_INVALID, "Chart samples exceed their storage range");
    f32 *storage = new f32[next];
    for (u32 index = 0; index < size; ++index)
      storage[index] = vector->begin[index];
    delete[] vector->begin;
    vector->begin = storage;
    vector->end = storage + size;
    vector->storage_end = storage + next;
  }
  *vector->end++ = value;
}

// Populate the education bar chart from the residential layer's age cohorts and localized axis labels.
void UiChartsView::DrawEducationChartEffects(WnWindow *recipient) {
  CtResidentialLayer *residential = UiGetCity()->ResidentialLayer();
  SetChartVisibilityEffects(recipient, 6);
  ScString format, display, number;
  i32 population = residential->GetPopulation();
  {
    UiTextRef reference(kRsGamePopulationEducationQuotientFormatText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  i32 rating = population ? residential->GetAgeCohortEQ(0, 89) : 0;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(rating, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 11, display);
  {
    UiTextRef reference(kRsGameWorkforceEducationQuotientFormatText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  rating = population ? residential->GetWorkforceEQ() : 0;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(rating, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 12, display);
  WnWindow *child = recipient->GetChildWindowFromID(2);
  assert(child != nullptr);
  UiBarGraph *bar = nullptr;
  if (!child->QueryInterface(IID_WnBarGraphView, reinterpret_cast<void **>(&bar)))
    return;
  UiGraph *graph = nullptr;
  if (child->QueryInterface(IID_WnGraphView, reinterpret_cast<void **>(&graph))) {
    graph->SetGraphAreaOutlineAndFill(false, false);
    GxBuffer *pattern;
    {
      GxColorType color_type(GxColorType::Color::RGB16);
      UiImageRef image(50, kRsMapImageGroup, GxVramRequest::SystemMemory, color_type);
      pattern = image.GetBuffer();
    }
    if (pattern) {
      UiChartsPaintPattern(pattern, 51);
      graph->SetBackgroundPattern(pattern);
    }
    graph->SetGraphArea(ScRect{93, 1, 355, 155});
    UiGraphAxis horizontal = static_cast<UiGraphAxis>(0), vertical = static_cast<UiGraphAxis>(1);
    graph->SetAxisMinValue(vertical, 0.0f);
    graph->SetAxisMaxValue(vertical, 150.0f);
    graph->SetMajorTickSpacing(vertical, 20.0f);
    graph->SetAxisMinValue(horizontal, 0.0f);
    graph->SetAxisMaxValue(horizontal, 100.0f);
    graph->SetMinorTickSpacing(horizontal, 10.0f);
    graph->SetMajorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMajorTickUsage(horizontal, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(horizontal, static_cast<UiGraphUsage>(1));
    graph->SetMajorTickDecimalPlaces(horizontal, 0);
    graph->SetMajorTickDecimalPlaces(vertical, 0);
    FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
    u32 text_color = recipient->MakeFillColor(53, 58, 128);
    graph->SetAxisTickFont(horizontal, font, text_color);
    graph->SetAxisTickFont(vertical, font, text_color);
    graph->SetAxisLabelFont(horizontal, font, text_color);
    graph->SetAxisLabelFont(vertical, font, text_color);
    if (!UiChartsCompactLanguage(UiGetLanguageManager()->GetCurrentLanguage()))
      graph->SetAxisLabelRotation(vertical, static_cast<UiGraphRotation>(1));
    graph->SetTickColor(recipient->MakeFillColor(173, 177, 220));
    static const char *const ages[9] = {"10", "20", "30", "40", "50", "60", "70", "80", "90"};
    for (u32 index = 0; index < 9; ++index) {
      ScString text(ages[index]);
      bar->InsertCategoryName(text, 2 * index + 1);
    }
    ScString horizontal_label;
    {
      UiTextRef reference(kRsGameResidentAgeText, kRsGameTextGroup);
      horizontal_label = reference.GetText();
    }
    ScString vertical_label;
    {
      UiTextRef reference(kRsGameEducationQuotientText, kRsGameTextGroup);
      vertical_label = reference.GetText();
    }
    graph->SetAxisLabel(horizontal, horizontal_label, true);
    graph->SetAxisLabel(vertical, vertical_label, true);
    ScRect label_area = UiChartsCompactLanguage(UiGetLanguageManager()->GetCurrentLanguage())
        ? ScRect{30, 45, 48, 145}
        : ScRect{15, 45, 115, 145};
    graph->SetAxisLabelPosition(vertical, &label_area);
    graph->Release();
  }
  bar->SetBarFillColor(recipient->MakeFillColor(0, 210, 143), 0);
  bar->SetBarOutlineColor(recipient->MakeFillColor(173, 177, 220), 0);
  UiGraphFloatVector samples{};
  for (u32 age = 0; age < 100; age += 5) {
    f32 value = 0.0f;
    if (population && residential->GetAgeCohortPopPct((u8)age, (u8)(age + 4)) > 0.0f)
      value = (f32)residential->GetAgeCohortEQ((u8)age, (u8)(age + 4));
    UiChartsAppendSample(&samples, value);
  }
  bar->SetData(&samples, 0);
  bar->CompleteRedraw(nullptr);
  bar->Release();
  delete[] samples.begin;
}

// Populate the health bar chart from the residential layer's age cohorts and localized axis labels.
void UiChartsView::DrawHealthChartEffects(WnWindow *recipient) {
  CtResidentialLayer *residential = UiGetCity()->ResidentialLayer();
  SetChartVisibilityEffects(recipient, 9);
  ScString format, display, number;
  i32 population = residential->GetPopulation();
  {
    UiTextRef reference(kRsGamePopulationLifeExpectancyYearsText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  i32 rating = population ? residential->GetAgeCohortLE(0, 89) : 0;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(rating, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 11, display);
  {
    UiTextRef reference(kRsGameWorkforceLifeExpectancyYearsText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  rating = population ? residential->GetWorkforceLE() : 0;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(rating, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 12, display);
  WnWindow *child = recipient->GetChildWindowFromID(2);
  assert(child != nullptr);
  UiBarGraph *bar = nullptr;
  if (!child->QueryInterface(IID_WnBarGraphView, reinterpret_cast<void **>(&bar)))
    return;
  UiGraph *graph = nullptr;
  if (child->QueryInterface(IID_WnGraphView, reinterpret_cast<void **>(&graph))) {
    graph->SetGraphAreaOutlineAndFill(false, false);
    GxBuffer *pattern;
    {
      GxColorType color_type(GxColorType::Color::RGB16);
      UiImageRef image(48, kRsMapImageGroup, GxVramRequest::SystemMemory, color_type);
      pattern = image.GetBuffer();
    }
    if (pattern) {
      UiChartsPaintPattern(pattern, 49);
      graph->SetBackgroundPattern(pattern);
    }
    graph->SetGraphArea(ScRect{93, 1, 355, 155});
    UiGraphAxis horizontal = static_cast<UiGraphAxis>(0), vertical = static_cast<UiGraphAxis>(1);
    graph->SetAxisMinValue(vertical, 0.0f);
    graph->SetAxisMaxValue(vertical, 100.0f);
    graph->SetMajorTickSpacing(vertical, 10.0f);
    graph->SetAxisMinValue(horizontal, 0.0f);
    graph->SetAxisMaxValue(horizontal, 100.0f);
    graph->SetMinorTickSpacing(horizontal, 10.0f);
    graph->SetMajorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMajorTickUsage(horizontal, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(horizontal, static_cast<UiGraphUsage>(1));
    graph->SetMajorTickDecimalPlaces(horizontal, 0);
    graph->SetMajorTickDecimalPlaces(vertical, 0);
    FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
    u32 text_color = recipient->MakeFillColor(53, 58, 128);
    graph->SetAxisTickFont(horizontal, font, text_color);
    graph->SetAxisTickFont(vertical, font, text_color);
    graph->SetAxisLabelFont(horizontal, font, text_color);
    graph->SetAxisLabelFont(vertical, font, text_color);
    if (!UiChartsCompactLanguage(UiGetLanguageManager()->GetCurrentLanguage()))
      graph->SetAxisLabelRotation(vertical, static_cast<UiGraphRotation>(1));
    graph->SetTickColor(recipient->MakeFillColor(173, 177, 220));
    static const char *const ages[9] = {"10", "20", "30", "40", "50", "60", "70", "80", "90"};
    for (u32 index = 0; index < 9; ++index) {
      ScString text(ages[index]);
      bar->InsertCategoryName(text, 2 * index + 1);
    }
    ScString horizontal_label;
    {
      UiTextRef reference(kRsGameResidentAgeText, kRsGameTextGroup);
      horizontal_label = reference.GetText();
    }
    ScString vertical_label;
    {
      UiTextRef reference(0x1e0, kRsGameTextGroup);
      vertical_label = reference.GetText();
    }
    graph->SetAxisLabel(horizontal, horizontal_label, true);
    graph->SetAxisLabel(vertical, vertical_label, true);
    ScRect label_area = UiChartsCompactLanguage(UiGetLanguageManager()->GetCurrentLanguage())
        ? ScRect{30, 45, 48, 145}
        : ScRect{15, 45, 115, 145};
    graph->SetAxisLabelPosition(vertical, &label_area);
    graph->Release();
  }
  bar->SetBarFillColor(recipient->MakeFillColor(0, 210, 143), 0);
  bar->SetBarOutlineColor(recipient->MakeFillColor(173, 177, 220), 0);
  UiGraphFloatVector samples{};
  for (u32 age = 0; age < 100; age += 5) {
    f32 value = 0.0f;
    if (population && residential->GetAgeCohortPopPct((u8)age, (u8)(age + 4)) > 0.0f)
      value = (f32)residential->GetAgeCohortLE((u8)age, (u8)(age + 4));
    UiChartsAppendSample(&samples, value);
  }
  bar->SetData(&samples, 0);
  bar->CompleteRedraw(nullptr);
  bar->Release();
  delete[] samples.begin;
}

// Preserve the source's double-bias conversion to a signed 32-bit percentage.
static i64 UiChartsRoundPercent(double value) {
  double biased = value + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &biased, sizeof(bits));
  u32 low = (u32)bits + kScDoubleIntegerLowWordBias;
  i32 rounded;
  memcpy(&rounded, &low, sizeof(rounded));
  return rounded;
}

// Populate the population bar chart from the residential layer's age cohorts and localized axis labels.
void UiChartsView::DrawPopulationChartEffects(WnWindow *recipient) {
  CtResidentialLayer *residential = UiGetCity()->ResidentialLayer();
  SetChartVisibilityEffects(recipient, 10);
  ScString format, display, number;
  i32 population;
  {
    UiTextRef reference(kRsGameTotalPopulationFormatText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  population = residential->GetPopulation();
  i64 rating = (u32)population;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(rating, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 11, display);
  {
    UiTextRef reference(kRsGameWorkforceOfPopulationText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  rating = population ? UiChartsRoundPercent((double)residential->GetWorkforcePopPct() * 100.0) : 0;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(rating, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 12, display);
  WnWindow *child = recipient->GetChildWindowFromID(2);
  assert(child != nullptr);
  UiBarGraph *bar = nullptr;
  if (!child->QueryInterface(IID_WnBarGraphView, reinterpret_cast<void **>(&bar)))
    return;
  UiGraph *graph = nullptr;
  if (child->QueryInterface(IID_WnGraphView, reinterpret_cast<void **>(&graph))) {
    graph->SetGraphAreaOutlineAndFill(false, false);
    GxBuffer *pattern;
    {
      GxColorType color_type(GxColorType::Color::RGB16);
      UiImageRef image(52, kRsMapImageGroup, GxVramRequest::SystemMemory, color_type);
      pattern = image.GetBuffer();
    }
    if (pattern) {
      UiChartsPaintPattern(pattern, 53);
      graph->SetBackgroundPattern(pattern);
    }
    graph->SetGraphArea(ScRect{93, 1, 355, 155});
    UiGraphAxis horizontal = static_cast<UiGraphAxis>(0), vertical = static_cast<UiGraphAxis>(1);
    graph->SetAxisMinValue(vertical, 0.0f);
    graph->SetAxisMaxValue(vertical, 15.0f);
    graph->SetMajorTickSpacing(vertical, 5.0f);
    graph->SetAxisMinValue(horizontal, 0.0f);
    graph->SetAxisMaxValue(horizontal, 100.0f);
    graph->SetMinorTickSpacing(horizontal, 10.0f);
    graph->SetMajorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMajorTickUsage(horizontal, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(horizontal, static_cast<UiGraphUsage>(1));
    graph->SetMajorTickDecimalPlaces(horizontal, 0);
    graph->SetMajorTickDecimalPlaces(vertical, 0);
    FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
    u32 text_color = recipient->MakeFillColor(53, 58, 128);
    graph->SetAxisTickFont(horizontal, font, text_color);
    graph->SetAxisTickFont(vertical, font, text_color);
    graph->SetAxisLabelFont(horizontal, font, text_color);
    graph->SetAxisLabelFont(vertical, font, text_color);
    if (!UiChartsCompactLanguage(UiGetLanguageManager()->GetCurrentLanguage()))
      graph->SetAxisLabelRotation(vertical, static_cast<UiGraphRotation>(1));
    graph->SetTickColor(recipient->MakeFillColor(173, 177, 220));
    static const char *const ages[9] = {"10", "20", "30", "40", "50", "60", "70", "80", "90"};
    for (u32 index = 0; index < 9; ++index) {
      ScString text(ages[index]);
      bar->InsertCategoryName(text, 2 * index + 1);
    }
    ScString horizontal_label;
    {
      UiTextRef reference(kRsGameResidentAgeText, kRsGameTextGroup);
      horizontal_label = reference.GetText();
    }
    ScString vertical_label;
    {
      UiTextRef reference(kRsGamePopulationText, kRsGameTextGroup);
      vertical_label = reference.GetText();
    }
    graph->SetAxisLabel(horizontal, horizontal_label, true);
    graph->SetAxisLabel(vertical, vertical_label, true);
    ScRect label_area = UiChartsCompactLanguage(UiGetLanguageManager()->GetCurrentLanguage())
        ? ScRect{30, 45, 48, 145}
        : ScRect{15, 45, 115, 145};
    graph->SetAxisLabelPosition(vertical, &label_area);
    graph->Release();
  }
  bar->SetBarFillColor(recipient->MakeFillColor(0, 210, 143), 0);
  bar->SetBarOutlineColor(recipient->MakeFillColor(173, 177, 220), 0);
  UiGraphFloatVector samples{};
  for (u32 age = 0; age < 100; age += 5) {
    double percentage = (double)residential->GetAgeCohortPopPct((u8)age, (u8)(age + 4)) * 100.0;
    f32 value = (f32)percentage;
    if (percentage > 0.0 && percentage < 0.19480519f)
      value = 0.19480519f;
    UiChartsAppendSample(&samples, population ? value : 0.0f);
  }
  bar->SetData(&samples, 0);
  bar->CompleteRedraw(nullptr);
  bar->Release();
  delete[] samples.begin;
}

// Populate the power chart from the city's annual consumption and its nine plant categories.
void UiChartsView::DrawElectricityChartEffects(WnWindow *recipient) {
  ScString format, display, number;
  CtPowerLayer *power = UiGetCity()->PowerLayer();
  f32 total = (f32)power->GetTotalAnnualConsumption();
  for (u32 index = 0; index < 10; ++index)
    legend_visible[index] = 0;
  SetChartVisibilityEffects(recipient, 7);
  {
    UiTextRef reference(kRsGameAnnuallyAveragedElectricityUsageByTypeText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    ScString caption(copy);
    UiChartsSetTextEffects(recipient, 13, caption);
  }
  {
    UiTextRef reference(kRsGameTotalElectricityUsageAnnuallyMWHText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString((i64)total, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 11, display);
  WnWindow *child = recipient->GetChildWindowFromID(3);
  UiPieGraph *pie = nullptr;
  if (!child || !child->QueryInterface(IID_UiPieGraph, reinterpret_cast<void **>(&pie)))
    return;
  pie->RemoveAllSeries();
  u32 series = 0;
  if (total > 0.0f) {
    static const u32 types[9] = {1, 6, 3, 9, 7, 5, 4, 2, 8};
    f32 sum = 0.0f;
    for (u32 index = 0; index < 9; ++index) {
      double proportion = (double)power->GetAnnualConsumptionOfPowerType(types[index]) / total;
      if (proportion > 0.0) {
        sum = (f32)(sum + proportion);
        UiChartsSetPieSliceEffects(recipient, pie, (f32)proportion, 14 + index, chart_colors[index], &series);
        legend_visible[index] = 1;
      }
    }
    double remaining = 1.0 - sum;
    if (remaining > 0.000099999997) {
      UiChartsSetPieSliceEffects(recipient, pie, (f32)remaining, 23, chart_colors[9], &series);
      legend_visible[9] = 1;
    }
  }
  SetChartVisibilityEffects(recipient, 7);
  pie->CompleteRedraw(nullptr);
  pie->Release();
}

// Add two source garbage counters with the installed 64-bit wraparound before converting their combined value.
static i64 UiChartsAddGarbage(i64 first, i64 second) {
  u64 bits = (u64)first + (u64)second;
  i64 result;
  memcpy(&result, &bits, sizeof(result));
  return result;
}

// Preserve the source's double-bias conversion to a signed 32-bit display count.
static i64 UiChartsRoundGarbage(double value) {
  double biased = value + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &biased, sizeof(bits));
  u32 low = (u32)bits + kScDoubleIntegerLowWordBias;
  i32 rounded;
  memcpy(&rounded, &low, sizeof(rounded));
  return rounded;
}

// Populate the garbage chart from the original production, disposal and uncollected city counters.
void UiChartsView::DrawGarbageChartEffects(WnWindow *recipient) {
  EcLayer *garbage = UiGetCity()->PollutionLayer();
  ScString format, display, number;
  for (u32 index = 10; index < 15; ++index)
    legend_visible[index] = 0;
  SetChartVisibilityEffects(recipient, 8);
  {
    UiTextRef reference(kRsGameGarbageGoesToText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    ScString caption(copy);
    UiChartsSetTextEffects(recipient, 13, caption);
  }
  {
    UiTextRef reference(kRsGameTotalGarbageProducedAnnuallyTonsText, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    format = copy;
  }
  i64 produced = garbage->GetTotalGarbageProduced();
  f32 scale = garbage->GetGarbageScalingFactor();
  i64 amount = UiChartsRoundGarbage((double)scale * (double)produced);
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString(amount, &number);
  display.Sprintf(format.Data(), number.Data());
  UiChartsSetTextEffects(recipient, 11, display);
  WnWindow *child = recipient->GetChildWindowFromID(3);
  UiPieGraph *pie = nullptr;
  if (!child || !child->QueryInterface(IID_UiPieGraph, reinterpret_cast<void **>(&pie)))
    return;
  f32 total = (f32)garbage->GetTotalGarbageLandfilled();
  total = (f32)((double)garbage->GetTotalGarbageExported() + total);
  i64 converted = garbage->GetTotalGarbageConvertedToEnergy();
  i64 incinerated = garbage->GetTotalGarbageIncinerated();
  total = (f32)((double)UiChartsAddGarbage(converted, incinerated) + total);
  total = (f32)((double)garbage->GetTotalGarbageRecycled() + total);
  total = (f32)((double)(u32)garbage->GetTotalGarbageUncollected() + total);
  pie->RemoveAllSeries();
  u32 series = 0;
  if (total > 0.0f) {
    double proportion = (double)garbage->GetTotalGarbageLandfilled() / total;
    if (proportion > 0.0) {
      UiChartsSetPieSliceEffects(recipient, pie, (f32)proportion, 24, chart_colors[10], &series);
      legend_visible[10] = 1;
    }
    proportion = (double)garbage->GetTotalGarbageExported() / total;
    if (proportion > 0.0) {
      UiChartsSetPieSliceEffects(recipient, pie, (f32)proportion, 25, chart_colors[11], &series);
      legend_visible[11] = 1;
    }
    converted = garbage->GetTotalGarbageConvertedToEnergy();
    incinerated = garbage->GetTotalGarbageIncinerated();
    proportion = (double)UiChartsAddGarbage(converted, incinerated) / total;
    if (proportion > 0.0) {
      UiChartsSetPieSliceEffects(recipient, pie, (f32)proportion, 26, chart_colors[12], &series);
      legend_visible[12] = 1;
    }
    proportion = (double)garbage->GetTotalGarbageRecycled() / total;
    if (proportion > 0.0) {
      UiChartsSetPieSliceEffects(recipient, pie, (f32)proportion, 27, chart_colors[13], &series);
      legend_visible[13] = 1;
    }
    proportion = (double)(u32)garbage->GetTotalGarbageUncollected() / total;
    if (proportion > 0.0) {
      UiChartsSetPieSliceEffects(recipient, pie, (f32)proportion, 28, chart_colors[14], &series);
      legend_visible[14] = 1;
    }
  }
  SetChartVisibilityEffects(recipient, 8);
  pie->CompleteRedraw(nullptr);
  pie->Release();
}

// Build the chart controls and localized legends, then subscribe the chart view to city-change notifications.
u8 UiChartsView::OnInit(WnWindow *recipient) {
  if (!UiMapsView::OnInit(recipient))
    return 0;
  static const u8 colors[17][3] = {{128, 64, 64}, {255, 126, 126}, {0, 128, 255}, {255, 128, 0}, {0, 255, 0},
      {255, 255, 0}, {255, 0, 0}, {0, 255, 255}, {0, 0, 255}, {240, 58, 230}, {240, 58, 230}, {58, 240, 176},
      {240, 58, 58}, {58, 71, 240}, {240, 217, 58}, {0, 210, 143}, {53, 58, 128}};
  for (u32 index = 0; index < 17; ++index)
    chart_colors[index] =
        UiGetGraphics()->ConvertRGBValueToNative16Bit(colors[index][0], colors[index][1], colors[index][2]);
  ScString empty;
  CreateBarGraph(recipient, 2, empty);
  UiChartsCreateGraphOutline(recipient);
  UiChartsCreatePieGraph(recipient, 3, empty);
  UiChartsCreateLabel(recipient, 11, 10, 200, 356, 20, empty, 0, 0);
  UiChartsCreateLabel(recipient, 12, 10, 216, 356, 20, empty, 0, 0);
  WnLabelControl *label = nullptr;
  recipient->GetChildAs(11, IID_WnLabelControl, reinterpret_cast<void **>(&label));
  if (label) {
    label->SetAlignment(kWnLabelCenterTop);
    u32 color = label->GetWindow()->MakeFillColor(53, 58, 128);
    label->SetTextColor(color);
    label->Release();
    label = nullptr;
  }
  recipient->GetChildAs(12, IID_WnLabelControl, reinterpret_cast<void **>(&label));
  if (label) {
    label->SetAlignment(kWnLabelCenterTop);
    u32 color = label->GetWindow()->MakeFillColor(240, 141, 95);
    label->SetTextColor(color);
    label->Release();
  }
  UiChartsCreateLabel(recipient, 13, 19, 13, 180, 18, empty, 0, 0);
  static const u32 text_ids[15] = {
      0x1e8, 0x1e9, 0x1ea, 0x1eb, 0x1ec, 0x1ed, 0x1ee, 0x1ef, 0x1f0, 0x1f1, 0x1f4, 0x1f1, 0x1f5, 0x1f6, 0x1f7};
  for (u32 index = 0; index < 15; ++index) {
    UiTextRef reference(text_ids[index], kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    ScString caption(copy);
    UiChartsCreateLabel(recipient, 14 + index, 20, 30, 167, index < 10 ? 14 : 23, caption, 1, chart_colors[index]);
  }
  UiChartsCreateOptions(recipient);
  UiGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kCtMsg_MonthChanged);
  return 1;
}

// Tile the requested chart pattern and overlay the localized caption within the graph's right-hand panel.
void SC3_STDCALL UiChartsPaintPattern(GxBuffer *destination, u32 instance) {
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(instance, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  if (!image)
    return;
  ScRect panel{145, 1, 263, 154}, source;
  image->AddRef();
  image->GetBufferArea(&source);
  destination->BltTiled(image, source, panel, 0, 0, nullptr);
  image->Release();
  UiTextRef reference(kRsGameWorkforceText, kRsGameTextGroup);
  ScString text(reference.GetText());
  ScString copy(text);
  ScString caption(copy);
  FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
  u32 color = destination->ConvertRGBValueToNative16Bit(210, 141, 95);
  font->DrawTextPara(destination, panel, caption.Data(), caption.Strlen(), color, 0, static_cast<FtOutputStyle>(1));
}

// Construct the chart's tilted pie, retaining the real alpha and shading masks before attachment.
u8 SC3_STDCALL UiChartsCreatePieGraph(WnWindow *parent, u32 identifier, const ScStringView &caption) {
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  UiPieGraphWindow *window = new UiPieGraphWindow;
  window->AddRef();
  u32 transparent = resources->GetColor(kWnColorUiTransparency);
  window->SetID(identifier);
  UiGraph *graph = static_cast<UiGraph *>(window);
  {
    ScString title(caption);
    graph->SetTitle(title, false);
  }
  graph->SetBackgroundOutline(false);
  graph->SetBackgroundFillColor(transparent);
  window->SetSize(180, 130);
  ScRect area = *window->GetArea();
  graph->SetGraphArea(area);
  graph->SetGraphAreaOutlineAndFill(false, false);
  UiPieGraph *pie = static_cast<UiPieGraph *>(window);
  pie->SetViewType(static_cast<UiPieGraphView>(1));
  GxColorType alpha_format(GxColorType::Color::Pal8);
  UiImageRef alpha_ref(kRsPieGraphAlphaImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, alpha_format);
  GxBuffer *alpha = alpha_ref.GetBuffer();
  if (!alpha)
    return 0;
  GxColorType shade_format(GxColorType::Color::RGB16);
  UiImageRef shade_ref(kRsPieGraphShadeImageId, kRsDialogImageGroup, GxVramRequest::SystemMemory, shade_format);
  GxBuffer *shade = shade_ref.GetBuffer();
  if (!shade)
    return 0;
  shade->SetTransparency(transparent);
  pie->Set3DViewMaskBuffers(alpha, shade);
  window->SetSize(185, 135);
  FtFont *font = resources->GetFont(kWnFontUiSmall);
  pie->SetPercentageTextFont(font, kUiGraphUseFontColor);
  window->GZWinMoveTo(193, 50);
  window->SetFlag(kWnFlagTransparentBuffer, true);
  u8 result = parent->ChildAdd(window);
  if (!result)
    window->Shutdown();
  window->Release();
  return result;
}

// Insert a nonzero pie slice, set its two colors and position its visible legend; zero values hide the legend.
void SC3_STDCALL UiChartsSetPieSliceEffects(
    WnWindow *parent, UiPieGraph *pie, f32 value, u32 identifier, u32 color, u32 *index) {
  WnWindow *label = parent->GetChildWindowFromID(identifier);
  if (value == 0.0f) {
    label->HideWindow();
    return;
  }
  u8 red = (u8)((uptr)parent >> 24), green = (u8)(identifier >> 24), blue = 0;
  UiGetGraphics()->Convert16BitNativeValueToRGB((u16)color, &red, &green, &blue);
  u8 contrast = (u32)red + green + blue <= 384 ? 255 : 0;
  u32 text_color = UiGetGraphics()->ConvertRGBValueToNative16Bit(contrast, contrast, contrast);
  pie->InsertSeries(kUiAppendGraphSeries);
  pie->SetData(value, *index);
  pie->SetSeriesColor(color, *index, text_color);
  label->ShowWindow();
  i32 height = label->GetH();
  label->GZWinMoveTo(20, (i32)(*index * ((u32)height + 2u) + 30u));
  ++*index;
}

// Set the chart's title, legend and graph visibility in the source's child lookup order.
// The fifteen legend flags are read only for the chart that initialized the corresponding group.
void UiChartsView::SetChartVisibilityEffects(WnWindow *recipient, u32 chart) {
  bool pie_visible = chart == 7 || chart == 8;
  WnWindow *child = recipient->GetChildWindowFromID(13);
  if (child)
    child->SetFlag(kWnFlagVisible, pie_visible);
  for (u32 index = 0; index < 15; ++index) {
    child = recipient->GetChildWindowFromID(14 + index);
    if (child) {
      bool visible = index < 10 ? chart == 7 && legend_visible[index] != 0 : chart == 8 && legend_visible[index] != 0;
      child->SetFlag(kWnFlagVisible, visible);
    }
  }
  pie_visible = chart == 7 || chart == 8;
  child = recipient->GetChildWindowFromID(3);
  if (child)
    child->SetFlag(kWnFlagVisible, pie_visible);
  child = recipient->GetChildWindowFromID(4);
  if (child)
    child->SetFlag(kWnFlagVisible, pie_visible);
  bool bar_visible = chart == 6 || chart == 9 || chart == 10;
  child = recipient->GetChildWindowFromID(2);
  if (child)
    child->SetFlag(kWnFlagVisible, bar_visible);
  child = recipient->GetChildWindowFromID(11);
  if (child)
    child->SetFlag(kWnFlagVisible, true);
  bar_visible = chart == 6 || chart == 9 || chart == 10;
  child = recipient->GetChildWindowFromID(12);
  if (child)
    child->SetFlag(kWnFlagVisible, bar_visible);
}

// Return the new history view through its original primary maps-view identity without an extra factory retain.
UiMapsViewInterface *UiGraphsView::CreateGraphsView() {
  return new UiGraphsView;
}

UiGraphsView::UiGraphsView() {
}

// Destroy the graph-line storage before destroying the shared maps-view state.
// The original releases retained graph interfaces during window shutdown, not here.
UiGraphsView::~UiGraphsView() = default;

bool UiGraphsView::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiGraphsViewIdentity, UiGraphsViewIdentity);
  return UiMapsView::QueryInterface(iid, out);
}

u32 UiGraphsView::AddRef() {
  return UiMapsView::AddRef();
}

u32 UiGraphsView::Release() {
  return UiMapsView::Release();
}

// Retain a graph and append a disabled line; null graph inputs are rejected.
u8 UiGraphsView::AddGraphInfo(ScGraphInfoView *graph) {
  if (!graph)
    return 0;
  graph->AddRef();
  UiGraphLine *line = lines.Append();
  line->enabled = 0;
  line->graph = graph;
  return 1;
}

// Configure the graph view's identity and resources before creating the shared view.
// The original ignores SetViewArea's result and preserves the base call's raw return byte.
u8 UiGraphsView::CreateView(UiMapsWindow *view_owner, ScRect *area, ScPoint *location) {
  if ((u8)IsReady())
    return 0;
  SetViewArea(area);
  u32 identifier = 0x605;
  if (!SetViewId(identifier) || !SetNameResource(kRsGameTextGroup, 0x184) || !SetIconButton(0x5e5, 11) ||
      !SetIconButtonLocation(location))
    return 0;
  return UiMapsView::CreateView(view_owner, area, location);
}

u8 UiGraphsView::PrepareHideView() {
  return 1;
}

u8 UiGraphsView::PrepareShowView() {
  return 1;
}

u8 UiGraphsView::RefreshData(CtCity *) {
  return 1;
}

// Query the city and enumerate its layers with the graph-view request and real interface identities.
u8 UiGraphsView::EnumCityItems(CtCity *city, bool) {
  ScMessage message(kWnMsg_QueryOpened, kUiQueryTagGraphs, 0, 0);
  if (city) {
    city->DoQueryInfo(&message, static_cast<UiGraphsViewIdentity *>(this));
    city->DoEnumLayers(&message, static_cast<CtCityEnumerator *>(this));
  }
  return 1;
}

// Forward each non-null city layer the original message and the graph-view identity.
bool UiGraphsView::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<UiGraphsViewIdentity *>(this));
  return true;
}

// Redraw the graph when its subscribed city notification arrives and the shared window exists.
bool UiGraphsView::DoMessage(ScMessage *message) {
  if (message->type != kCtMsg_MonthChanged)
    return false;
  if (window)
    DrawLineGraphEffects(window->GetWindow());
  return true;
}

// Apply a period selection or graph-checkbox activation and redraw when the checkbox changes.
u8 UiGraphsView::OnButtonActivated(WnWindow *receiver, u32 identifier) {
  if (identifier == 2 || identifier == kUiFrameCloseButtonId)
    return 1;
  if (identifier >= 3 && identifier <= 5) {
    UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
    WnWindow *child = receiver->GetChildWindowFromID(2);
    WnOptionControl *options = nullptr;
    if (child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&options))) {
      options->SetValue(identifier);
      options->Release();
    }
    return 1;
  }
  u32 index = 0, count = lines.size;
  while (index < count && *lines.data[index].graph->GetSelectionLabelID() != identifier)
    ++index;
  if (index == count)
    return 0;
  WnWindow *child = receiver->GetChildWindowFromID(identifier);
  WnButton *button = nullptr;
  if (!child->QueryInterface(IID_WnButton, reinterpret_cast<void **>(&button)))
    return 0;
  lines.data[index].enabled = button->IsChecked();
  DrawLineGraphEffects(receiver);
  button->Release();
  return 1;
}

// Dispatch graph button messages, play their selection sound and redraw selected periods.
bool UiGraphsView::DoWinMsg(WnWindow *receiver, u32 message, uptr first, uptr second, uptr third) {
  if (message != kWnMsg_Command || ((u32)first != 1 && (u32)first != 3))
    return UiMapsView::DoWinMsg(receiver, message, first, second, third);
  u8 handled = (u32)first == kWnCommandButtonActivated ? OnButtonActivated(receiver, (u32)second) : 0;
  UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  WnWindow *child = receiver->GetChildWindowFromID((u32)second);
  WnOptionControl *options = nullptr;
  if (child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&options))) {
    u32 selected = options->GetValue();
    if (selected >= 3 && selected <= 5)
      DrawLineGraphEffects(receiver);
    options->Release();
    return true;
  }
  if (handled)
    return true;
  return UiMapsView::DoWinMsg(receiver, message, first, second, third);
}

// Persist the selected graph period and line states, then release each retained graph in order.
u8 UiGraphsView::OnShutdown(WnWindow *receiver) {
  UiGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kCtMsg_MonthChanged);
  WnWindow *child = receiver->GetChildWindowFromID(2);
  WnOptionControl *options = nullptr;
  bool ready = child != nullptr;
  if (child) {
    ready = child->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&options));
    if (ready) {
      ApGraphsPrefs *preferences = UiGetPreferences()->GraphsPrefs();
      u32 selected = options->GetValue();
      preferences->SetRadioSelection(selected);
      options->Release();
    }
  }
  for (u32 i = 0; i < lines.size; ++i) {
    UiGraphLine &line = lines.data[i];
    u32 identifier = *line.graph->GetSelectionLabelID();
    ready = UiGetPreferences()->GraphsPrefs()->AddCheckbox(identifier, line.enabled != 0);
    if (!ready)
      break;
  }
  if (ready)
    UiGetPreferences()->GraphsPrefs()->SetReady(true);
  for (u32 i = 0; i < lines.size; ++i)
    lines.data[i].graph->Release();
  lines.Clear();
  return UiMapsView::OnShutdown(receiver);
}

void UiGraphsView::StartPlacingLabelsEffects() {
  for (u32 i = 0; i < 16; ++i)
    label_slots[i] = 0;
}

void UiGraphsView::EndPlacingLabels() {
}

u8 UiGraphsView::PlaceLabel(i32 *position) {
  i32 value = *position;
  if (value < 10 || value > 167)
    return 0;
  i32 slot = (value - 10) / 10;
  if (label_slots[slot]) {
    i32 distance = 0;
    for (;;) {
      i32 lower = slot - distance, upper = slot + distance;
      if (lower >= 0 && !label_slots[lower]) {
        slot = lower;
        break;
      }
      if (upper < 16 && !label_slots[upper]) {
        slot = upper;
        break;
      }
      if (++distance >= 6)
        return 1;
    }
  }
  label_slots[slot] = 1;
  *position = 10 * slot + 10;
  return 1;
}

// Create the graph-period option group, restore its saved selection and attach its three localized buttons.
u8 SC3_STDCALL UiGraphsCreatePeriodOptions(WnWindow *parent) {
  ScString empty("");
  ScRect area{275, 335, 375, 435};
  WnOptionControl *options = UiGetWindowResources()->CreateOptGrp(2, empty, &area);
  assert(options != nullptr);
  options->SetButtonFont(UiGetWindowResources()->GetFont(kWnFontUiSystem));
  for (i32 index = 2; index <= 4; ++index) {
    u32 color = options->GetWindow()->MakeFillColor(49, 52, 123);
    options->SetFontColor(index, color);
  }
  for (u32 index = 0; index < 3; ++index) {
    UiTextRef reference(0x170 + index, kRsGameTextGroup);
    ScString text(reference.GetText());
    ScString copy(text);
    ScString caption(copy);
    options->AddButton(3 + index, caption, 0, nullptr, nullptr);
  }
  u32 selected = 3;
  if (UiGetPreferences()->GraphsPrefs()->IsReady()) {
    u32 saved = UiGetPreferences()->GraphsPrefs()->GetRadioSelection();
    if (saved >= 3 && saved <= 5)
      selected = saved;
  }
  options->SetValue(selected);
  options->SetOptionFlag(1, 0);
  options->SetOptionFlag(8, 0);
  if (parent->ChildAdd(options->GetWindow())) {
    options->GetWindow()->GZWinMoveTo(275, 335);
    options->GetWindow()->SetH(100);
    options->GetWindow()->SetW(100);
    options->Release();
    return 1;
  }
  WnWindowManager *manager = UiGetWindowManager();
  manager->DestroyWindow(options->GetWindow());
  options->Release();
  return 0;
}

// Create and position a graph checkbox, restoring its saved value or applying the three original default selections.
u8 SC3_STDCALL UiGraphsCreateCheckbox(
    WnWindow *parent, u32 identifier, i32 row, i32 column, const ScStringView &caption, u32) {
  WnResources *resources = UiGetWindowResources();
  WnButton *button = resources->CreateCheckBox(identifier, caption, nullptr, nullptr, 0);
  if (button) {
    button->GetWindow()->SetFlag(kWnFlagReorderable, false);
    button->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSystem));
    bool selected;
    if (UiGetPreferences()->GraphsPrefs()->IsReady() && UiGetPreferences()->GraphsPrefs()->FindCheckbox(identifier))
      selected = UiGetPreferences()->GraphsPrefs()->GetCheckboxState(identifier);
    else
      selected = identifier == kEcFundsGraphSelectionId || identifier == kCtCitySizeGraphSelectionId ||
          identifier == kCtPowerGraphSelectionId;
    button->SetChecked(selected);
    button->GetWindow()->GZWinMoveTo((i32)(120u * (u32)column + 20u), (i32)(24u * (u32)row + 210u));
    if (!parent->ChildAdd(button->GetWindow())) {
      WnWindowManager *manager = UiGetWindowManager();
      manager->DestroyWindow(button->GetWindow());
    }
    button->Release();
  }
  return 1;
}

// Create a graph's colored label and its initially hidden marker outline, retaining the source callback order.
u8 UiGraphsView::CreateGraphLegend(WnWindow *parent, u32 identifier, const ScStringView &caption, u32 color) {
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return 0;
  WnLabelControl *label = resources->CreateLabel(identifier, caption);
  if (!label)
    return 0;
  label->GetWindow()->SetID(identifier);
  label->SetTextColor(color);
  label->SetLabelFlag(kWnLabelWrapText, true);
  label->GetWindow()->SetW(96);
  label->Fit();
  label->GetWindow()->HideWindow();
  if (!parent->ChildAdd(label->GetWindow()))
    label->GetWindow()->Shutdown();
  label->Release();
  WnOutlineControl *outline = nullptr;
  u8 result = UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline));
  if (result) {
    outline->SetFlags(kWnOutlinePlain | kWnOutlinePolygon);
    outline->SetFillColor(color);
    outline->SetDarkOutlineColor(color);
    outline->SetLightOutlineColor(color);
    outline->GetWindow()->SetArea(ScRect{262, 10, 266, 168});
    outline->GetWindow()->SetID(identifier + kUiGraphLegendOutlineIdOffset);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    ScPoint points[] = {{0, 0}, {1, 1}};
    outline->SetShapePoly(2, points);
    outline->GetWindow()->HideWindow();
    WnWindow *view_window = window->GetWindow();
    if (!view_window->ChildAdd(outline->GetWindow())) {
      WnWindowManager *manager = UiGetWindowManager();
      manager->DestroyWindow(outline->GetWindow());
    }
    outline->Release();
  }
  return result;
}

// Compute the history label's initial vertical position with the source's double-bias conversion.
// Retain the binary32 normalized input before the double multiplication and bit extraction.
static i32 UiGraphsLabelPosition(f32 normalized) {
  double biased = (double)normalized * 157.0 + kScDoubleIntegerBias;
  u64 bits;
  memcpy(&bits, &biased, sizeof(bits));
  u32 low = (u32)bits + kUiHistoryLabelConversionBias;
  i32 position;
  memcpy(&position, &low, sizeof(position));
  return position;
}

// Rebuild enabled history series, their date labels and the aligned current-value markers.
void UiGraphsView::DrawLineGraphEffects(WnWindow *recipient) {
  ScString empty;
  UiLineGraphSeriesInfo settings;
  settings.thickness = 2;
  WnWindow *graph_window = recipient->GetChildWindowFromID(6);
  u32 month, day, year;
  UiGetCity()->GetDate(&month, &day, &year);
  StartPlacingLabelsEffects();
  assert(graph_window != nullptr);
  UiLineGraph *line = nullptr;
  if (graph_window->QueryInterface(IID_WnLineGraphView, reinterpret_cast<void **>(&line))) {
    UiGraph *graph = nullptr;
    line->QueryInterface(IID_WnGraphView, reinterpret_cast<void **>(&graph));
    line->RemoveAllSeries();
    WnWindow *period_window = recipient->GetChildWindowFromID(2);
    WnOptionControl *options = nullptr;
    u32 period = 3;
    assert(period_window != nullptr);
    if (period_window->QueryInterface(IID_WnOptionControl, reinterpret_cast<void **>(&options))) {
      period = options->GetValue();
      options->Release();
    }
    assert(graph != nullptr);
    UiGraphAxis horizontal = static_cast<UiGraphAxis>(0), vertical = static_cast<UiGraphAxis>(1);
    graph->SetAxisTickTextDrawing(horizontal, static_cast<UiGraphUsage>(1));
    graph->SetAxisTickTextDrawing(vertical, static_cast<UiGraphUsage>(1));
    graph->SetMajorTickUsage(horizontal, static_cast<UiGraphUsage>(0));
    graph->SetMajorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    graph->SetMinorTickUsage(horizontal, static_cast<UiGraphUsage>(1));
    graph->SetMinorTickLength(horizontal, 4);
    graph->SetMinorTickUsage(vertical, static_cast<UiGraphUsage>(0));
    u32 series = 0;
    bool categories_written = false;
    for (u32 index = 0; index < lines.size; ++index) {
      ScGraphInfoView *info = lines.data[index].graph;
      WnButton *button = nullptr;
      if (recipient->GetChildAs(*info->GetSelectionLabelID(), IID_WnButton, reinterpret_cast<void **>(&button))) {
        lines.data[index].enabled = button->IsChecked();
        button->Release();
      }
      if (!lines.data[index].enabled) {
        u32 identifier = *info->GetValueLabelID();
        recipient->GetChildWindowFromID(identifier)->HideWindow();
        identifier = *info->GetValueLabelID();
        recipient->GetChildWindowFromID(identifier + kUiGraphLegendOutlineIdOffset)->HideWindow();
        continue;
      }
      line->InsertSeries(kUiAppendGraphSeries);
      if (period >= 3 && period <= 5) {
        u32 count = period == 3 ? 12 : 10;
        if (period == 3) {
          --year;
          graph->SetAxisMinValue(horizontal, (f32)(month + 12u * year - 1u));
          graph->SetAxisMaxValue(horizontal, (f32)(month + 12u * year + 10u));
          graph->SetMinorTickSpacing(horizontal, 1.0f);
        } else {
          year -= period == 4 ? 10u : 100u;
          graph->SetAxisMinValue(horizontal, (f32)year);
          graph->SetAxisMaxValue(horizontal, (f32)(year + (period == 4 ? 9u : 99u)));
          graph->SetMinorTickSpacing(horizontal, period == 4 ? 1.0f : 10.0f);
        }
        u32 values[12];
        if (period == 3)
          info->GetMonthDataSet(values);
        else if (period == 4)
          info->GetYearDataSet(values);
        else
          info->GetDecadeDataSet(values);
        f32 maximum = (f32)info->GetMaxValue();
        if (maximum > 0.0f) {
          for (u32 sample = 0; sample < count; ++sample) {
            f32 value = (f32)((double)values[count - sample - 1] * 100.0 / maximum);
            line->SetData(value, kUiAppendGraphValue, series);
            if (!categories_written) {
              bool show_year = period == 3 ? month == 12 : period == 4 ? year % 5 == 4 : year % 20 == 10;
              if (show_year) {
                ScString text;
                text.Sprintf("%u", period == 5 ? year + 10 : year + 1);
                line->InsertCategoryName(text, sample);
              } else {
                line->InsertCategoryName(empty, sample);
              }
            }
            if (period == 3) {
              if (++month == 13) {
                ++year;
                month = 1;
              }
            } else {
              year += period == 4 ? 1u : 10u;
            }
          }
          categories_written = true;
        }
        u32 identifier = *info->GetValueLabelID();
        WnWindow *label = recipient->GetChildWindowFromID(identifier);
        ScGraphDataSet set = period == 3 ? ScGraphDataSet::Month
            : period == 4                ? ScGraphDataSet::Year
                                         : ScGraphDataSet::Decade;
        label->SetCaption(*info->GetCurrentValueLabel(set));
        f32 normalized = (f32)(1.0 - (double)values[0] / maximum);
        i32 initial = UiGraphsLabelPosition(normalized), adjusted = initial;
        PlaceLabel(&adjusted);
        identifier = *info->GetValueLabelID();
        UiGraphsSetMarkerEffects(recipient, identifier, initial, adjusted);
        i32 height = label->GetH();
        label->GZWinMoveTo(266, (i32)((u32)adjusted - (u32)(height / 2) + 10u));
        label->ShowWindow();
      }
      line->SetSeriesInfo(&settings, series);
      u32 rgb = lines.data[index].graph->GetLineColor();
      u32 color = UiGetGraphics()->ConvertRGBValueToNative16Bit((u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb);
      line->SetSeriesColor(color, series);
      line->SetSymbolUse(static_cast<UiGraphUsage>(0), static_cast<UiGraphUsage>(1), series);
      ++series;
    }
    graph->Release();
    line->Release();
  }
  EndPlacingLabels();
  graph_window->GZPaint();
}

// Initialize the history graph and period controls, enumerate city graph lines, then subscribe to city changes.
u8 UiGraphsView::OnInit(WnWindow *recipient) {
  u8 result = UiMapsView::OnInit(recipient);
  if (result) {
    {
      ScString title("");
      UiGraphsCreateLineGraph(recipient, 6, title);
    }
    UiGraphsCreatePeriodOptions(recipient);
    EnumCityItems(UiGetCity(), true);
    u32 row = 0, column = 0, index = 0;
    while (index < lines.size) {
      if (index == lines.size - 1 && index % 3 == 0 && column == 0)
        column = 1;
      ScGraphInfoView *info = lines.data[index].graph;
      u32 rgb = info->GetLineColor();
      GxGraphics *graphics = UiGetGraphics();
      u32 color = graphics->ConvertRGBValueToNative16Bit((u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb);
      const ScStringView *selection = info->GetSelectionLabel();
      u32 selection_id = *info->GetSelectionLabelID();
      UiGraphsCreateCheckbox(recipient, selection_id, (i32)row, (i32)column, *selection, color);
      const ScStringView *value = info->GetCurrentValueLabel(ScGraphDataSet::Month);
      u32 value_id = *info->GetValueLabelID();
      CreateGraphLegend(recipient, value_id, *value, color);
      if (++column > 2) {
        column = 0;
        ++row;
      }
      ++index;
    }
    DrawLineGraphEffects(recipient);
  }
  UiGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kCtMsg_MonthChanged);
  return result;
}

// Construct the graph-view line graph, configure its axes and attach it before installing the tick font.
u8 SC3_STDCALL UiGraphsCreateLineGraph(WnWindow *parent, u32 identifier, const ScStringView &caption) {
  UiLineGraphWindow *line = new UiLineGraphWindow;
  line->AddRef();
  line->SetID(identifier);
  UiGraph *graph = static_cast<UiGraph *>(line);
  {
    ScString title(caption);
    graph->SetTitle(title, false);
  }
  line->SetSize(356, 189);
  UiGraphAxis horizontal = static_cast<UiGraphAxis>(0), vertical = static_cast<UiGraphAxis>(1);
  graph->SetAxisMinValue(vertical, 0.0f);
  graph->SetAxisMaxValue(vertical, 100.0f);
  graph->SetMinorTickUsage(vertical, static_cast<UiGraphUsage>(0));
  graph->SetMajorTickUsage(horizontal, static_cast<UiGraphUsage>(1));
  graph->SetTickColor(parent->MakeFillColor(173, 177, 220));
  GxBuffer *pattern;
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(47, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    pattern = image.GetBuffer();
  }
  if (pattern)
    graph->SetBackgroundPattern(pattern);
  graph->SetGraphAreaOutlineAndFill(false, false);
  graph->SetGraphArea(ScRect{1, 10, 256, 167});
  line->GZWinMoveTo(6, 10);
  u8 result = parent->ChildAdd(line);
  if (result) {
    WnResources *resources = UiGetWindowResources();
    u32 color = parent->MakeFillColor(173, 177, 220);
    FtFont *font = resources->GetFont(kWnFontUiSystem);
    graph->SetAxisTickFont(horizontal, font, color);
    line->PullToFront();
    line->SetFlag(kWnFlagReorderable, false);
  } else {
    UiGetWindowManager()->DestroyWindow(line);
  }
  line->Release();
  return result;
}

void SC3_STDCALL UiGraphsSetMarkerEffects(WnWindow *window, u32 identifier, i32 first, i32 second) {
  WnOutlineControl *outline = nullptr;
  if (window->GetChildAs(
          identifier + kUiGraphLegendOutlineIdOffset, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
    ScPoint points[] = {{0, first}, {0, (i32)((u32)first + 1)}, {4, (i32)((u32)second + 1)}, {4, second}};
    outline->SetShapePoly(4, points);
    outline->GetWindow()->ShowWindow();
    outline->Release();
  }
}
