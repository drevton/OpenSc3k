#include "misc/budget.h"
#include <stdio.h>
#include <stdlib.h>
#include "advisors/ad_advisors.h"
#include "advisors/api.h"
#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/ids.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "economy/ec_module.h"
#include "economy/ec_residential.h"
#include "fonts/sc3_bitmap_font.h"
#include "misc/api.h"
#include "misc/module.h"
#include "misc/sc_graph_info.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "resources/stream.h"
#include "ui/api.h"
#include "ui/commands.h"
#include "ui/game_menu.h"
#include "ui/generic_dialogs.h"
#include "ui/neighbor_window.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"

CtBudgetWindow::CtBudgetWindow()
    : city(nullptr), budget(nullptr), language(nullptr), totals{}, year_balance(0), estimated_balance(0),
      estimated_funds(0), column_left(0), column_width(0), income_button(nullptr), expense_button(nullptr),
      income_window(nullptr), income_view(nullptr), expense_window(nullptr), expense_view(nullptr) {
  language = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  city = ScGetMiscApplication()->GetCity();
  budget = city->BudgetLayer();
}

CtBudgetWindow::~CtBudgetWindow() {
}

// Clear the output before testing unknown or the real window-callback identity, then retain the published primary view.
bool CtBudgetWindow::QueryInterface(u32 iid, void **out) {
  *out = nullptr;
  if (iid == IID_ScUnknown || iid == IID_UiWindowCallback)
    *out = static_cast<UiWindowCallback *>(this);
  if (!*out)
    RETURN_QUERY_INTERFACE_FAILURE();
  AddRef();
  return true;
}

u32 CtBudgetWindow::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 CtBudgetWindow::Release() {
  return ScMiscRefCount::Release();
}

// Dispatch initialization, shutdown and button notifications with their original separate payloads.
// Unrelated control events return the original zero byte without reading their remaining arguments.
bool CtBudgetWindow::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  switch (message) {
  case kWnMsg_Command:
    if (u32(first) != 1)
      return 0;
    return OnButtonActivated(window, u32(second));
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  default:
    return 0;
  }
}

// Acquire a real generic window, install this callback and configure the original500-by480 budget client.
// Reuse a remembered visible location; otherwise ask the message server for the packed screen extent and center it.
bool CtBudgetWindow::CreateBudgetWindow(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 1);
  generic->SetStyle(kUiGenericCloseButtonState, 1);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericCloseButton, 0);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtBudgetWindowId);
  (*out)->SetFlag(kWnFlagPrivateBuffer, true);
  (*out)->SetArea(0, 0, 500, 480);
  ScRect area = *(*out)->GetMainWindow()->GetArea();
  if (CtBudgetLocationSet &&
      (CtBudgetLastX > area.right || CtBudgetLastY > area.bottom || i32(u32(CtBudgetLastX) + 500) < area.left ||
          i32(u32(CtBudgetLastY) + 480) < area.top))
    CtBudgetLocationSet = false;
  if (!CtBudgetLocationSet) {
    ScMessage message(kWnMsg_QueryUiClientArea);
    ScGetMiscMessageServer()->MessageSend(message);
    CtBudgetLastX = (i32(u16(message.data2 >> 16)) - i32(u16(message.data1 >> 16)) - 500) / 2;
    CtBudgetLastY = (i32(u16(message.data2)) - i32(u16(message.data1)) - 480) / 2;
    if (CtBudgetLastX < 0)
      CtBudgetLastX = 0;
    if (CtBudgetLastY < 0)
      CtBudgetLastY = 0;
    CtBudgetLocationSet = true;
  }
  (*out)->GZWinMoveTo(CtBudgetLastX, CtBudgetLastY);
  return true;
}

// Clear borrowed city services before retiring control holds, releasing each pointer before clearing its field.
// Capture the final window location only after all release callbacks, so the next budget window can reuse it.
bool CtBudgetWindow::OnShutdown(WnWindow *window) {
  city = nullptr;
  budget = nullptr;
  language = nullptr;
  for (u32 i = 0; i < 8; ++i)
    if (totals[i]) {
      totals[i]->Release();
      totals[i] = nullptr;
    }
  ScRelease(&income_button);
  ScRelease(&expense_button);
  ScRelease(&income_window);
  ScRelease(&income_view);
  ScRelease(&expense_window);
  ScRelease(&expense_view);
  CtBudgetLastX = window->GetL();
  CtBudgetLastY = window->GetT();
  CtBudgetLocationSet = true;
  return true;
}

// Report every button activation to BoxX before dispatching its actual command, including unsupported identifiers.
// Close through the window-manager queue; tab changes update live panes and toggle controls in their original order.
bool CtBudgetWindow::OnButtonActivated(WnWindow *window, u32 identifier) {
  ApBoxX *box = ScGetMiscApplication()->GetBoxX();
  if (box) {
    if (identifier == 32)
      box->Event(21, 0, 0, 0, 0);
    else
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  }
  switch (identifier) {
  case 32:
    return ScGetMiscWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  case 33:
    CtBudgetLastView = 0;
    income_window->SetFlag(kWnFlagVisible, false);
    expense_window->SetFlag(kWnFlagVisible, true);
    expense_button->ToggleOn();
    income_button->ToggleOff();
    break;
  case 34:
    CtBudgetLastView = 1;
    income_window->SetFlag(kWnFlagVisible, true);
    expense_window->SetFlag(kWnFlagVisible, false);
    expense_button->ToggleOff();
    income_button->ToggleOn();
    break;
  case 35: {
    ScMessage message(kWnMsg_ModalAdvisorTopicList, kAdBudgetAdvisorId, 0, 0);
    ScGetMiscMessageServer()->MessageSend(message);
    break;
  }
  default:
    return false;
  }
  return true;
}

extern u8 CtBudgetLastView;

// Construct the original totals, tab controls and both concrete budget panes, preserving acquisition and callback
// order.
bool CtBudgetWindow::OnInit(WnWindow *window) {
  ScString text, format, year_text;
  u32 muted = window->MakeFillColor(127, 120, 176), blue = window->MakeFillColor(31, 32, 111);
  u32 black = window->MakeFillColor(0, 0, 0), red = window->MakeFillColor(255, 0, 0);
  FtFont *font = ScGetMiscWindowResources()->GetFont(kWnFontUiSystem);
  UiGenericInterface *generic;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  ScRect client;
  generic->GetClientArea(&client);
  i32 width = i32(u32(client.right) - u32(client.left) + 1) / 3;
  column_width = width;
  column_left = i32(u32(client.right) - u32(width) - 64);
  {
    CtMiscTextResource resource(kRsGameBudgetText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  window->SetCaption(text);
  i32 language_id = ScGetMiscLanguageManager()->GetCurrentLanguage();
  if (language_id != kScLanguageJapanese && language_id != kScLanguageSimplifiedChinese &&
      language_id != kScLanguageTraditionalChinese && language_id != kScLanguageKorean &&
      language_id != kScLanguageThai) {
    i32 x = i32(u32(client.left) + u32(width));
    for (u32 i = 0; i < 2; ++i) {
      {
        CtMiscTextResource resource(134 + i, kRsGameTextGroup);
        ScString value = resource.GetText(), copy(value);
        text = copy;
      }
      WnButton **button = i ? &income_button : &expense_button;
      ConfigureResizableButton(button, 1, 33 + i, true);
      WnWindow *child = (*button)->GetWindow();
      child->SetCaption(text);
      window->ChildAdd(child);
      (*button)->Fit();
      child->GZWinMoveTo(x, 4);
      i32 child_width = child->GetW();
      if (!i)
        x = i32(u32(x) + u32(child_width) + 8);
    }
  }
  WnButton *button;
  ConfigureButton(&button, kRsStandardButtonImageId, kRsUiImageGroup, 32);
  WnWindow *child = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  window->ChildAdd(child);
  i32 x = i32(u32(client.right) - u32(child->GetW()) - 4);
  i32 y = i32(u32(client.bottom) - u32(child->GetH()) - 2);
  child->GZWinMoveTo(x, y);
  ScRect rectangle = {i32(u32(x) - 16), i32(u32(y) - 1), i32(u32(x) + 43), i32(u32(y) + 33)};
  generic->AddPanel(kCtBudgetCloseButtonPanelId, 0, 15, &rectangle);
  child->SetFlag(kWnFlagReorderable, true);
  window->ChildToFront(child);
  child->SetFlag(kWnFlagReorderable, false);
  button->Release();
  ConfigureButton(&button, kRsHelpButtonImageId, kRsUiImageGroup, 35);
  child = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  window->ChildAdd(child);
  x = i32(u32(client.left) + 15);
  y = i32(u32(client.bottom) - u32(child->GetH()) - 2);
  child->GZWinMoveTo(x, y);
  rectangle = {11, i32(u32(y) - 4), 68, i32(u32(y) + 26)};
  generic->AddPanel(kCtBudgetHelpButtonPanelId, 0, 14, &rectangle);
  child->SetFlag(kWnFlagReorderable, true);
  window->ChildToFront(child);
  child->SetFlag(kWnFlagReorderable, false);
  button->Release();
  {
    CtMiscTextResource resource(kRsGameMeetFinancialAdvisorText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  WnLabelControl *label;
  ConfigureWrappedLabel(&label, text, 100, muted, ScGetMiscWindowResources()->GetFont(kWnFontUiSmall));
  window->ChildAdd(label->GetWindow());
  label->SetAlignment(kWnLabelLeftTop);
  y = i32(u32(y) - 10 - u32(label->GetWindow()->GetH()));
  label->GetWindow()->GZWinMoveTo(i32(u32(x) - 16), y);
  label->Release();
  ScRect pane = client;
  y = i32(u32(client.bottom) - 4);
  const u32 box_ids[8] = {kCtBudgetYearEndFundsBoxId, kCtBudgetCurrentFundsBoxId, kCtBudgetEstimatedCashFlowBoxId, 0,
      kCtBudgetEstimatedExpenseBoxId, 0, kCtBudgetEstimatedIncomeBoxId, 0};
  for (u32 i = 0; i < 8; ++i) {
    {
      CtMiscTextResource resource(160 - i, kRsGameTextGroup);
      ScString value = resource.GetText(), copy(value);
      text = copy;
    }
    u32 caption_color = i < 4 ? blue : i < 6 ? red : black;
    ConfigureLabel(&label, text, caption_color, font);
    bool box_before_caption = i == 2 || i == 4 || i == 6;
    WnWindow *box = nullptr;
    if (box_before_caption) {
      i32 height = label->GetWindow()->GetH();
      rectangle = {i32(u32(column_left) + 3), i32(u32(y) + 2 * (3 - u32(height))),
          i32(u32(column_left) + u32(column_width) - 3), y};
      ConfigureDigitBox(&box, i32(box_ids[i]), rectangle, true);
      window->ChildAdd(box);
    }
    window->ChildAdd(label->GetWindow());
    i32 left = i32(u32(column_left) - u32(label->GetWindow()->GetW()) - 2);
    i32 height = label->GetWindow()->GetH();
    y = i32(u32(y) - u32(height) + (i == 0 || i == 3 || i == 5 || i == 7 ? 4 : 0));
    label->GetWindow()->GZWinMoveTo(left, y);
    label->Release();
    if (box_before_caption)
      box->Release();
    language->MakeMoneyString(0, &text);
    ConfigureLabel(&totals[i], text, black, font);
    if (i < 2) {
      height = totals[i]->GetWindow()->GetH();
      rectangle = {i32(u32(column_left) + 3), i32(u32(y) + 1), i32(u32(column_left) + u32(column_width) - 3),
          i32(u32(height) + u32(y) - 1)};
      ConfigureDigitBox(&box, i32(box_ids[i]), rectangle, true);
      window->ChildAdd(box);
    }
    window->ChildAdd(totals[i]->GetWindow());
    i32 value_width = i == 3 || i == 5 || i == 7 ? column_width >> 1 : column_width;
    left = i32(u32(column_left) + u32(value_width) - u32(totals[i]->GetWindow()->GetW()) - 2);
    totals[i]->GetWindow()->GZWinMoveTo(left, y);
    if (i < 2)
      box->Release();
  }
  i32 pane_bottom = y;
  pane.bottom = y;
  language_id = ScGetMiscLanguageManager()->GetCurrentLanguage();
  if (language_id == kScLanguageJapanese || language_id == kScLanguageSimplifiedChinese ||
      language_id == kScLanguageTraditionalChinese || language_id == kScLanguageKorean ||
      language_id == kScLanguageThai) {
    x = i32(u32(pane.left) - 4);
    y = i32(u32(pane.bottom) - 1);
    for (u32 i = 0; i < 2; ++i) {
      {
        CtMiscTextResource resource(134 + i, kRsGameTextGroup);
        ScString value = resource.GetText(), copy(value);
        text = copy;
      }
      WnButton **tab = i ? &income_button : &expense_button;
      ConfigureResizableButton(tab, 1, 33 + i, true);
      child = (*tab)->GetWindow();
      child->SetCaption(text);
      window->ChildAdd(child);
      (*tab)->Fit();
      child->GZWinMoveTo(x, y);
      if (!i)
        x = i32(u32(x) + u32(child->GetW()));
    }
    pane_bottom = y;
  }
  i32 right_divider = i32(u32(pane.right) - 64);
  i32 middle_divider = i32(u32(pane.left) + ((u32(column_left) - u32(pane.left)) >> 1));
  rectangle = {11, 75, i32(u32(window->GetW()) - 7), 83};
  generic->AddPanel(kCtBudgetTopDividerPanelId, 0, 14, &rectangle);
  rectangle = {11, i32(u32(pane_bottom) - 8), i32(u32(window->GetW()) - 7), pane_bottom};
  generic->AddPanel(kCtBudgetBottomDividerPanelId, 0, 14, &rectangle);
  rectangle = {middle_divider, 82, column_left, i32(u32(pane_bottom) - 7)};
  generic->AddPanel(kCtBudgetMiddleDividerPanelId, 0, 5, &rectangle);
  rectangle = {right_divider, 82, i32(u32(right_divider) + 48), i32(u32(pane_bottom) - 7)};
  generic->AddPanel(kCtBudgetRightDividerPanelId, 0, 5, &rectangle);
  rectangle = {pane.left, pane.top, i32(u32(middle_divider) - 1), 74};
  WnWindow *box;
  ConfigureDigitBox(&box, kCtBudgetHeaderFrameId, rectangle, false);
  window->ChildAdd(box);
  box->Release();
  u32 month = 0, day = 0, year = 0;
  city->GetDate(&month, &day, &year);
  {
    CtMiscTextResource resource(kRsGameBudgetHeadingFormatText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    format = copy;
  }
  year_text.Sprintf("%u", year);
  text.Sprintf(format.begin, year_text.begin);
  ConfigureLabel(&label, text, muted, nullptr);
  WnWindow *measured = label->GetWindow(), *sized = label->GetWindow();
  i32 date_right = i32(u32(middle_divider) - 3), height = measured->GetH();
  sized->SetArea(i32(u32(pane.left) + 2), 0, date_right, height);
  window->ChildAdd(label->GetWindow());
  label->GetWindow()->GZWinMoveTo(i32(u32(pane.left) + 2), i32(u32(pane.top) + 2));
  label->Release();
  language->MakeDateString(i32(month), 0, year, &text, ScLanguageUtility::DateFormat::AlphaMonthYear);
  ConfigureLabel(&label, text, muted, nullptr);
  measured = label->GetWindow();
  sized = label->GetWindow();
  height = measured->GetH();
  sized->SetArea(i32(u32(pane.left) + 2), 0, date_right, height);
  window->ChildAdd(label->GetWindow());
  measured = label->GetWindow();
  WnWindow *moving = label->GetWindow();
  height = measured->GetH();
  moving->GZWinMoveTo(i32(u32(pane.left) + 2), i32(u32(height) + u32(pane.top) - 2));
  label->Release();
  i32 row_height = (y - 91) / 11;
  i32 last_top = i32(u32(y) - u32(row_height)), stripe_top = i32(u32(row_height) + 83);
  u32 identifier = 225;
  while (stripe_top <= last_top) {
    WnFlatControl *control = nullptr;
    if (ScGetMiscDirector()->GZCOM()->GetClassObject(
            kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&control))) {
      u32 fill = control->GetWindow()->MakeFillColor(255, 255, 255);
      u16 border = u16(control->GetWindow()->MakeFillColor(255, 255, 255));
      WnWindow *stripe = control->GetWindow();
      stripe->SetID(identifier);
      stripe->SetArea(12, stripe_top, i32(u32(middle_divider) - 1), i32(u32(row_height) + u32(stripe_top) - 1));
      stripe->SetFillColor(fill);
      stripe->SetFlag(kWnFlagReorderable, false);
      control->SetOutlineColor(border);
      control->SetSizeMode(0);
      control->SetOutlineFlags(15);
      window->ChildAdd(stripe);
      control->Release();
      ++identifier;
      stripe_top = i32(u32(stripe_top) + 2 * u32(row_height));
    }
  }
  generic->Release();
  expense_view = new CtBudgetExpenseView(static_cast<EcBudget *>(budget), this, u32(column_left) - u32(pane.left),
      u32(column_width), pane, u32(row_height));
  expense_view->AddRef();
  if (!static_cast<CtBudgetExpenseView *>(expense_view)->CreateExpenseView(&expense_window))
    return false;
  window->ChildAdd(expense_window);
  expense_window->GZWinMoveTo(pane.left, pane.top);
  expense_window->SetFlag(kWnFlagVisible, CtBudgetLastView == 0);
  income_view = new CtBudgetIncomeView(static_cast<EcBudget *>(budget), this, u32(column_left) - u32(pane.left),
      u32(column_width), pane, u32(row_height));
  income_view->AddRef();
  if (!static_cast<CtBudgetIncomeView *>(income_view)->CreateIncomeView(&income_window))
    return false;
  window->ChildAdd(income_window);
  income_window->GZWinMoveTo(pane.left, pane.top);
  income_window->SetFlag(kWnFlagVisible, CtBudgetLastView == 1);
  if (CtBudgetLastView) {
    expense_button->ToggleOff();
    income_button->ToggleOn();
  } else {
    expense_button->ToggleOn();
    income_button->ToggleOff();
  }
  UpdateTotals();
  return true;
}

CtShowBudgetCommand::CtShowBudgetCommand() {
}

// Retire the derived command through the original common command destructor and its retained argument queues.
CtShowBudgetCommand::~CtShowBudgetCommand() = default;

// Pause the captured city while the real budget window runs modally, then destroy its window and release its builder.
// Preserve the missing-city refusal and resume the same city after either window-construction result.
bool CtShowBudgetCommand::Execute() {
  CtCity *city = ScGetMiscApplication()->GetCity();
  if (!city)
    return false;
  CtBudgetWindow *builder = new CtBudgetWindow;
  builder->AddRef();
  WnWindow *window = nullptr;
  city->SimHiddenPause();
  bool valid = builder->CreateBudgetWindow(&window);
  if (valid) {
    WnWindowManager *root_manager = ScGetMiscWindowManager(), *modal_manager = ScGetMiscWindowManager();
    modal_manager->DoModalWin(root_manager->GetMainWindow(), window);
    ScGetMiscWindowManager()->DestroyWindow(window);
    window->Release();
    window = nullptr;
  }
  builder->Release();
  city->SimHiddenUnpause();
  return valid;
}

static_assert(sizeof(CtShowBudgetCommand) == sizeof(ScGameCommand));

// Apply the static label builder's complete effects without asserting the original return from its final flag setter.
// Configure the unwrapped flags, font and color before assigning the caption, fitting and setting the window flag.
void CtBudgetWindow::ConfigureLabel(WnLabelControl **out, const ScString &text, u32 color, FtFont *font) {
  *out = nullptr;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(out)))
    return;
  (*out)->SetLabelFlag(kWnLabelWrapText, false);
  (*out)->SetLabelFlag(kWnLabelFillBackground, false);
  if (font)
    (*out)->SetFont(font);
  else
    (*out)->SetFont(ScGetMiscWindowResources()->GetFont(kWnFontDefault));
  (*out)->SetTextColor(color);
  (*out)->GetWindow()->SetCaption(text);
  (*out)->Fit();
  (*out)->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
}

// Apply the wrapped label builder's effects, keeping its caption-before-color order distinct from the plain label.
void CtBudgetWindow::ConfigureWrappedLabel(
    WnLabelControl **out, const ScString &text, i32 width, u32 color, FtFont *font) {
  *out = nullptr;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(out)))
    return;
  (*out)->SetLabelFlag(kWnLabelWrapText, true);
  (*out)->SetLabelFlag(kWnLabelFillBackground, false);
  if (font)
    (*out)->SetFont(font);
  else
    (*out)->SetFont(ScGetMiscWindowResources()->GetFont(kWnFontDefault));
  (*out)->GetWindow()->SetCaption(text);
  (*out)->SetTextColor(color);
  (*out)->GetWindow()->SetW(width);
  (*out)->Fit();
  (*out)->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
}

// Preserve three window acquisitions, the low-WORD blue border and the acquisition hold transferred to the output view.
void CtBudgetWindow::ConfigureDigitBox(WnWindow **out, i32 identifier, const ScRect &area, bool inset) {
  *out = nullptr;
  WnFlatControl *control = nullptr;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&control)))
    return;
  u32 fill = control->GetWindow()->MakeFillColor(255, 255, 255);
  u16 border = u16(control->GetWindow()->MakeFillColor(124, 144, 255));
  *out = control->GetWindow();
  (*out)->SetID(u32(identifier));
  (*out)->SetArea(area.left, area.top, area.right, area.bottom);
  (*out)->SetFillColor(fill);
  (*out)->SetFlag(kWnFlagReorderable, false);
  control->SetOutlineColor(border);
  control->SetSizeMode(0);
  control->SetOutlineFlags(inset ? 32 : 96);
}

// Create the original image button from a scoped RGB16 resource, retaining its borrowed rectangle through creation.
void CtBudgetWindow::ConfigureButton(WnButton **out, u32 instance, u32 group, u32 identifier) {
  const GxColorType format(GxColorType::Color::RGB16);
  ScBuffer image(instance, group, GxVramRequest::SystemMemory, format);
  ScString caption;
  GxBuffer *buffer = image.GetBuffer();
  WnResources *resources = ScGetMiscWindowResources();
  if (buffer) {
    const ScRect *rectangle = buffer->GetBufferArea();
    *out = resources->CreateFixedButton(identifier, caption, buffer, rectangle, 0);
  } else {
    *out = resources->CreateFixedButton(identifier, caption, nullptr, nullptr, 0);
    (*out)->GetWindow()->SetSize(26, 16);
  }
}

// Create the requested push/radio image control, then install font12 and enforce the original minimum text gutters.
void CtBudgetWindow::ConfigureResizableButton(WnButton **out, u32 instance, u32 identifier, bool radio) {
  const GxColorType format(GxColorType::Color::RGB16);
  ScBuffer image(instance, kRsGonzoControlImageGroup, GxVramRequest::SystemMemory, format);
  ScString caption;
  GxBuffer *buffer = image.GetBuffer();
  if (buffer) {
    WnResources *resources = ScGetMiscWindowResources();
    const ScRect *rectangle = buffer->GetBufferArea();
    if (radio)
      *out = resources->CreateRadioButton(identifier, caption, buffer, rectangle, 0);
    else
      *out = resources->CreateButton(identifier, caption, buffer, rectangle, 0);
  } else {
    *out = ScGetMiscWindowResources()->CreateFixedButton(identifier, caption, nullptr, nullptr, 0);
  }
  (*out)->SetFont(ScGetMiscWindowResources()->GetFont(kWnFontUiTitle));
  i32 horizontal = 0, vertical = 0;
  (*out)->GetGutters(&horizontal, &vertical);
  if (horizontal < 15)
    horizontal = 15;
  (*out)->SetGutters(u8(horizontal), u8(vertical));
}

// Route button, scrolling and funding notifications through their separate original payloads.
// Preserve the pointer width of the scroll control and the Boolean result of the callback interface.
bool CtBudgetExpenseView::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  switch (message) {
  case kWnMsg_Command:
    if (u32(first) == 1)
      return OnButtonActivated(window, u32(second));
    if (u32(first) == 14)
      return OnSliderChanged(window, reinterpret_cast<WnWindow *>(second));
    return 0;
  case kWnMsg_BudgetSliderChanged:
    return OnBudgetSliderChanged(u32(first), i32(u32(second)));
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  default:
    return 0;
  }
}

// Store a funding notification's byte percentage through the actual department interface, then refresh estimates.
bool CtBudgetExpenseView::OnBudgetSliderChanged(u32 identifier, i32 value) {
  if (identifier < 200 || identifier >= department_count + 200)
    return false;
  departments[identifier - 200].department->SetFundingPercentage(u8(value));
  UpdateTotals();
  return true;
}

// Dispatch ordinance, command, bonds and department-help buttons after the original optional BoxX event.
bool CtBudgetExpenseView::OnButtonActivated(WnWindow *, u32 identifier) {
  ApBoxX *box = ScGetMiscApplication()->GetBoxX();
  if (box)
    box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  switch (identifier) {
  case 50: {
    ScMessage message(kWnMsg_ModalOrdinanceDialog, 0, 0, 0);
    ScGetMiscMessageServer()->MessageSend(message);
    UpdateTotals();
    return true;
  }
  case 51: {
    ScCommandFactory *factory = ScGetMiscCommandFactory();
    UiCommandInterface *command = nullptr;
    if (factory &&
        factory->GetCommand(kUiNeighborCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
      command->Execute();
      command->Release();
    }
    UpdateTotals();
    return true;
  }
  case 52: {
    CtBondsDialog *dialog = new CtBondsDialog(budget);
    dialog->AddRef();
    WnWindow *window = nullptr;
    if (dialog->CreateBondsDialog(&window)) {
      ScGetMiscWindowManager()->DoModalWin(window);
      window->Shutdown();
      window->Release();
    }
    dialog->Release();
    UpdateTotals();
    return true;
  }
  case 53:
  case 54: {
    ScMessage message(kWnMsg_ModalAdvisorBriefing, kAdBudgetAdvisorId, identifier == 53 ? 3004 : 3006, 0);
    ScGetMiscMessageServer()->MessageSend(message);
    return true;
  }
  default:
    if (identifier >= 100 && identifier < department_count + 100) {
      u32 index = identifier - 100;
      if (i32(index) >= i32(department_count))
        return true;
      ScMessage message(kWnMsg_ModalAdvisorTopic, 0, 0, 0);
      message.data1 = departments[index].department->GetAdvisorID();
      message.data2 = departments[index].department->GetAdvisorGroupID();
      ScGetMiscMessageServer()->MessageSend(message);
      return true;
    }
    return false;
  }
}

// Borrow one of the original row's seven windows through its actual control interface or direct window field.
// This native layout helper preserves getter calls; it owns no additional control or comparison state.
WnWindow *CtExpenseRowWindow(CtBudgetExpenseRow *row, u32 column) {
  switch (column) {
  case 0:
    return row->name->GetWindow();
  case 1:
    return row->funding->AsIGZWin();
  case 2:
    return row->button;
  case 3:
    return row->year_total->GetWindow();
  case 4:
    return row->estimated_total->GetWindow();
  case 5:
    return row->year_box;
  default:
    return row->estimated_box;
  }
}

// Show or hide all seven controls in each row before moving them, preserving the original separate traversal phases.
void CtBudgetExpenseView::UpdateDepartmentView() {
  if (!scrollbar)
    return;
  i32 viewport_height = scrollbar->GetWindow()->GetH();
  scrollbar->GetWindow()->GetW();
  i32 value = scrollbar->GetValue();
  // The source reads a nonexistent first row when count is zero; exclude that unavailable native memory access.
  if (!department_count)
    return;
  u32 offset = u32(value) * u32(departments[0].height), consumed = 0, top = department_top;
  bool clipped = false;
  for (u32 i = 0; i < department_count; ++i) {
    bool overflow = top + u32(departments[i].height) > u32(viewport_height) + department_top;
    bool hidden = clipped || consumed < offset || overflow;
    if (hidden)
      clipped = overflow;
    for (u32 column = 0; column < 7; ++column) {
      WnWindow *window = CtExpenseRowWindow(&departments[i], column);
      if (hidden)
        window->HideWindow();
      else
        window->ShowWindow();
    }
    for (u32 column = 0; column < 7; ++column) {
      WnWindow *vertical = CtExpenseRowWindow(&departments[i], column);
      if (hidden) {
        WnWindow *moving = CtExpenseRowWindow(&departments[i], column);
        i32 left = vertical->GetL();
        moving->GZWinMoveTo(left, 0);
      } else {
        WnWindow *horizontal = CtExpenseRowWindow(&departments[i], column);
        WnWindow *moving = CtExpenseRowWindow(&departments[i], column);
        i32 height = vertical->GetH(), left = horizontal->GetL();
        i32 y = i32(top + u32((i32(row_height) - height) / 2));
        moving->GZWinMoveTo(left, y);
      }
    }
    if (!hidden)
      top += u32(departments[i].height);
    consumed += u32(departments[i].height);
  }
}

// Accept only the original department scrollbar window ID, then rearrange the row controls through the native phase.
bool CtBudgetExpenseView::OnSliderChanged(WnWindow *, WnWindow *control) {
  if (control->GetID() != 59)
    return false;
  UpdateDepartmentView();
  return true;
}

CtBudgetExpenseView::CtBudgetExpenseView(
    EcBudget *budget, CtBudgetWindow *owner, u32 columnLeft, u32 columnWidth, const ScRect &area, u32 rowHeight)
    : owner(owner), budget(budget), column_left(columnLeft), column_width(columnWidth), row_height(rowHeight), totals{},
      departments(nullptr), scrollbar(nullptr) {
  city = ScGetMiscApplication()->GetCity();
  ordinances = ScAddRefNotNull(city->OrdinanceLayer());
  language = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  this->area = area;
}

CtBudgetExpenseView::~CtBudgetExpenseView() {
  ScRelease(&ordinances);
}

bool CtBudgetExpenseView::QueryInterface(u32, void **out) {
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtBudgetExpenseView::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 CtBudgetExpenseView::Release() {
  return ScMiscRefCount::Release();
}

// Acquire the generic child, apply the original eight styles and callback, then transfer its acquisition hold directly.
// Preserve the expense-pane identifier and wrapping rectangle differences without an extra retain or release.
bool CtBudgetExpenseView::CreateExpenseView(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, 0);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericCloseButtonState, 1);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericNoFrame, 1);
  generic->SetStyle(kUiGenericNoPainting, 1);
  generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtBudgetExpenseViewId);
  (*out)->SetArea(0, 0, i32(u32(area.right) - u32(area.left)), i32(u32(area.bottom) - u32(area.top)));
  return true;
}

// Build expense summaries, retained department controls and their original viewport/scrollbar, then refresh both views.
bool CtBudgetExpenseView::OnInit(WnWindow *window) {
  ScString text, unused;
  u32 muted = window->MakeFillColor(127, 120, 176), blue = window->MakeFillColor(31, 32, 111);
  u32 black = window->MakeFillColor(0, 0, 0), red = window->MakeFillColor(255, 0, 0);
  UiGenericInterface *generic;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  u32 month = 0, day = 0, year = 0;
  city->GetDate(&month, &day, &year);
  remaining_months = 13 - month;
  i32 help_left = i32(u32(window->GetW()) - 64);
  i32 midpoint = i32(column_left >> 1), selection_width = i32(column_left - (column_left >> 1)), top = 0;
  const u32 header_ids[4] = {139, 140, 141, 142};
  for (u32 index = 0; index < 4; ++index) {
    {
      CtMiscTextResource resource(header_ids[index], kRsGameTextGroup);
      ScString value = resource.GetText(), copy(value);
      text = copy;
    }
    i32 x, width;
    if (!index) {
      x = midpoint;
      width = selection_width;
    } else if (index < 3) {
      x = i32(column_left + (index == 2 ? column_width >> 1 : 0));
      width = i32(column_width >> 1);
    } else {
      x = help_left;
      width = 48;
    }
    WnLabelControl *label;
    CtBudgetWindow::ConfigureWrappedLabel(&label, text, width, muted, nullptr);
    window->ChildAdd(label->GetWindow());
    label->SetAlignment(kWnLabelCenterBottom);
    label->GetWindow()->GZWinMoveTo(x, 2);
    if (top < label->GetWindow()->GetH())
      top = label->GetWindow()->GetH();
    label->Release();
  }
  i32 header_height = top;
  top = i32(u32(top) + 14);
  i32 language_id = ScGetMiscLanguageManager()->GetCurrentLanguage();
  if (language_id == kScLanguageJapanese || language_id == kScLanguageSimplifiedChinese ||
      language_id == kScLanguageTraditionalChinese || language_id == kScLanguageKorean ||
      language_id == kScLanguageThai)
    top = i32(66 - ScGetMiscWindowResources()->GetFont(kWnFontUiHeading)->Height(0));
  const u32 category_ids[3] = {143, 144, 566}, button_instances[3] = {9, 8, 19};
  const u32 past_boxes[3] = {kCtBudgetFirstAccruedBoxId, kCtBudgetSecondAccruedBoxId, kCtBudgetThirdAccruedBoxId};
  const u32 future_boxes[3] = {
      kCtBudgetFirstProjectedBoxId, kCtBudgetSecondProjectedBoxId, kCtBudgetThirdProjectedBoxId};
  for (u32 index = 0; index < 3; ++index) {
    {
      CtMiscTextResource resource(category_ids[index], kRsGameTextGroup);
      ScString value = resource.GetText(), copy(value);
      text = copy;
    }
    WnLabelControl *label;
    CtBudgetWindow::ConfigureWrappedLabel(&label, text, midpoint, blue, nullptr);
    window->ChildAdd(label->GetWindow());
    WnWindow *vertical = label->GetWindow(), *moving = label->GetWindow();
    i32 height = vertical->GetH();
    moving->GZWinMoveTo(0, i32(u32(top) + u32((i32(row_height) - height) / 2)));
    label->Release();
    WnButton *button;
    CtBudgetWindow::ConfigureButton(&button, button_instances[index], kRsGonzoControlImageGroup, 50 + index);
    WnWindow *child = button->GetWindow();
    window->ChildAdd(child);
    i32 y = i32(u32(top) + u32((i32(row_height) - child->GetH()) / 2));
    i32 x = i32(u32(midpoint) + u32((selection_width - child->GetW()) / 2));
    child->GZWinMoveTo(x, y);
    if (index == 2 && child->GetH() > header_height)
      child->GetH();
    child->Release();
    i64 accrued;
    if (!index)
      accrued = i64(u64(0) - u64(budget->ordinance_expenses.yearTotal));
    else if (index == 1)
      accrued = i64(u64(0) - u64(budget->neighbor_expenses.yearTotal));
    else
      accrued = i64(u64(0) - u64(budget->yearly_bond_expenses));
    for (u32 side = 0; side < 2; ++side) {
      language->MakeMoneyString(side ? 0 : accrued, &text);
      if (side) {
        CtBudgetWindow::ConfigureLabel(&totals[index], text, black, nullptr);
        label = totals[index];
      } else {
        CtBudgetWindow::ConfigureLabel(&label, text, accrued < 0 ? red : black, nullptr);
      }
      bool special = true;
      if ((index == 0 && side) || index == 1) {
        language_id = ScGetMiscLanguageManager()->GetCurrentLanguage();
        special = language_id == kScLanguageJapanese || language_id == kScLanguageSimplifiedChinese ||
            language_id == kScLanguageTraditionalChinese || language_id == kScLanguageKorean ||
            language_id == kScLanguageThai;
      }
      ScRect rectangle;
      rectangle.left = i32(column_left + (side ? column_width >> 1 : 0) + (side ? 1 : 3));
      rectangle.right = i32(column_left + (side ? column_width : column_width >> 1) - (side ? 3 : 1));
      if (index == 1 && !special) {
        height = label->GetWindow()->GetH();
        rectangle.top = i32(u32(top) + 4);
        rectangle.bottom = i32(u32(top) + u32(height) + 3);
      } else if (index == 1) {
        WnWindow *height_a = label->GetWindow(), *height_b = label->GetWindow();
        i32 offset = (i32(row_height) - height_b->GetH()) / 2;
        rectangle.bottom = i32(u32(top) + u32(offset) + u32(height_a->GetH()));
        rectangle.top = i32(u32(top) + u32((i32(row_height) - label->GetWindow()->GetH()) / 2));
      } else if (!index && side) {
        WnWindow *height_a = label->GetWindow(), *height_b = label->GetWindow();
        i32 offset = (i32(row_height) - height_b->GetH()) / 2;
        rectangle.bottom = i32(u32(top) + u32(offset) + u32(height_a->GetH()));
        if (!special)
          rectangle.right = i32(column_left + column_width - 1);
        rectangle.top = i32(u32(top) + u32((i32(row_height) - label->GetWindow()->GetH()) / 2));
      } else {
        WnWindow *height_a = label->GetWindow(), *height_b = label->GetWindow(), *height_c = label->GetWindow();
        rectangle.top = i32(u32(top) + u32((i32(row_height) - height_c->GetH()) / 2));
        i32 offset = (i32(row_height) - height_b->GetH()) / 2;
        rectangle.bottom = i32(u32(top) + u32(offset) + u32(height_a->GetH()));
      }
      WnWindow *box;
      CtBudgetWindow::ConfigureDigitBox(&box, i32(side ? future_boxes[index] : past_boxes[index]), rectangle, true);
      window->ChildAdd(box);
      window->ChildAdd(label->GetWindow());
      x = i32(column_left + (side ? column_width : column_width >> 1) - u32(label->GetWindow()->GetW()) - 2);
      vertical = label->GetWindow();
      moving = label->GetWindow();
      height = vertical->GetH();
      moving->GZWinMoveTo(x, i32(u32(top) + u32((i32(row_height) - height) / 2)));
      if (!side)
        label->Release();
      box->Release();
    }
    if (index) {
      CtBudgetWindow::ConfigureButton(&button, kRsHelpButtonImageId, kRsUiImageGroup, 52 + index);
      child = button->GetWindow();
      window->ChildAdd(child);
      y = i32(u32(top) + u32((i32(row_height) - child->GetH()) / 2));
      x = i32(u32(help_left) + u32((48 - child->GetW()) / 2));
      child->GZWinMoveTo(x, y);
      child->Release();
    }
    top = i32(u32(top) + row_height);
  }
  u32 count = u32(budget->departments.size());
  department_top = u32(top);
  department_height = u32(window->GetH()) - 8 - u32(top);
  department_count = count;
  // Zero native rows exclude the source's unwritten control pointers during interrupted construction cleanup.
  departments = new CtBudgetExpenseRow[count ? count : 1]{};
  auto entry = budget->departments.begin();
  for (u32 index = 0; index < count; ++index) {
    ScString name;
    departments[index].identifier = entry->second.id;
    EcDepartmentBudget *department = entry->second.department;
    departments[index].department = department;
    departments[index].department->AddRef();
    {
      ScString copy(*department->GetDepartmentName());
      name = copy;
    }
    i32 extent = 0;
    WnLabelControl *label;
    CtBudgetWindow::ConfigureWrappedLabel(&label, name, midpoint, blue, nullptr);
    window->ChildAdd(label->GetWindow());
    label->GetWindow()->GZWinMoveTo(0, top);
    if (label->GetWindow()->GetH() > 0)
      extent = label->GetWindow()->GetH();
    departments[index].name = label;
    UiBudgetSlider *slider = nullptr;
    ScGetMiscDirector()->GZCOM()->GetClassObject(
        kUiBudgetSliderClassId, IID_UiBudgetSlider, reinterpret_cast<void **>(&slider));
    WnWindow *child = slider->AsIGZWin();
    child->SetID(200 + index);
    child->SetW(i32(u32(selection_width) - 12));
    slider->SetMinMax(0, 120);
    slider->SetValue(department->GetFundingPercentage());
    slider->SetMarkerValue(100);
    window->ChildAdd(child);
    child->GZWinMoveTo(i32(u32(midpoint) + 6), top);
    departments[index].funding = slider;
    if (child->GetH() > extent)
      extent = child->GetH();
    i64 accrued = i64(u64(0) - u64(entry->second.yearTotal));
    language->MakeMoneyString(accrued, &text);
    CtBudgetWindow::ConfigureLabel(&label, text, accrued < 0 ? red : black, nullptr);
    WnWindow *vertical = label->GetWindow();
    i32 left = i32(column_left + 3), right = i32((column_width >> 1) + column_left - 1);
    i32 height = vertical->GetH();
    ScRect box_area = {left, i32(u32(top) + 4), right, i32(u32(height) + u32(top) + 3)};
    u32 identifier = kCtBudgetYearBoxIdBase + index;
    WnWindow *box;
    CtBudgetWindow::ConfigureDigitBox(&box, i32(identifier), box_area, true);
    window->ChildAdd(box);
    window->ChildAdd(label->GetWindow());
    i32 x = i32(column_left + (column_width >> 1) - u32(label->GetWindow()->GetW()) - 2);
    label->GetWindow()->GZWinMoveTo(x, i32(u32(top) + 4));
    if (label->GetWindow()->GetH() > extent)
      extent = label->GetWindow()->GetH();
    departments[index].year_total = label;
    departments[index].year_box = box;
    language->MakeMoneyString(0, &text);
    CtBudgetWindow::ConfigureLabel(&label, text, black, nullptr);
    vertical = label->GetWindow();
    right = i32(column_width + column_left - 3);
    left = i32((column_width >> 1) + column_left + 1);
    height = vertical->GetH();
    box_area = {left, i32(u32(top) + 4), right, i32(u32(height) + u32(top) + 3)};
    CtBudgetWindow::ConfigureDigitBox(&box, i32(identifier + 496), box_area, true);
    window->ChildAdd(box);
    window->ChildAdd(label->GetWindow());
    x = i32(column_left - u32(label->GetWindow()->GetW()) + column_width - 2);
    label->GetWindow()->GZWinMoveTo(x, i32(u32(top) + 4));
    if (label->GetWindow()->GetH() > extent)
      extent = label->GetWindow()->GetH();
    departments[index].estimated_total = label;
    departments[index].estimated_box = box;
    WnButton *button;
    CtBudgetWindow::ConfigureButton(&button, kRsHelpButtonImageId, kRsUiImageGroup, 100 + index);
    child = button->GetWindow();
    window->ChildAdd(child);
    i32 width = child->GetW();
    child->GZWinMoveTo(i32(u32(help_left) + u32((48 - width) / 2)), top);
    departments[index].button = child;
    if (child->GetH() > extent)
      extent = child->GetH();
    departments[index].height = extent;
    ++entry;
  }
  u32 full_height = row_height * count, end = department_top + full_height;
  scrollbar = nullptr;
  if (end <= department_height + department_top) {
    department_top += row_height * (department_height / row_height - count);
    department_height = count * row_height;
  } else {
    u32 remainder = department_height % row_height;
    department_top += remainder;
    department_height -= remainder;
    u32 visible = department_height / row_height;
    scrollbar = ScGetMiscWindowResources()->CreateScrollbar(59, 2, 0, i32(count - visible), 0);
    scrollbar->SetDivisors(0, 0);
    window->ChildAdd(scrollbar->GetWindow());
    i32 width = scrollbar->GetWindow()->GetW(), left = i32(u32(help_left) + 48);
    scrollbar->GetWindow()->SetArea(
        left, i32(department_top), i32(u32(left) + u32(width)), i32(department_height + department_top));
    department_height = end - department_top + 1;
  }
  top = i32(department_top);
  for (u32 index = 0; index < count; ++index) {
    for (u32 column = 0; column < 7; ++column) {
      if (column == 2 || column >= 5) {
        WnWindow *captured = CtExpenseRowWindow(&departments[index], column);
        i32 height = captured->GetH(), left = captured->GetL();
        CtExpenseRowWindow(&departments[index], column)
            ->GZWinMoveTo(left, i32(u32(top) + u32((i32(row_height) - height) / 2)));
      } else {
        WnWindow *vertical = CtExpenseRowWindow(&departments[index], column);
        WnWindow *horizontal = CtExpenseRowWindow(&departments[index], column);
        WnWindow *moving = CtExpenseRowWindow(&departments[index], column);
        i32 height = vertical->GetH(), left = horizontal->GetL();
        moving->GZWinMoveTo(left, i32(u32(top) + u32((i32(row_height) - height) / 2)));
      }
    }
    departments[index].height = i32(row_height);
    top = i32(u32(top) + u32(departments[index].height));
  }
  UpdateTotals();
  UpdateDepartmentView();
  generic->Release();
  generic = nullptr;
  return true;
}

// Clear borrowed services before releasing the ordinance layer, totals and scrollbar in original order.
// Clear each row identifier and release its eight holds before deleting the array; the live row count is reread each time.
bool CtBudgetExpenseView::OnShutdown(WnWindow *) {
  city = nullptr;
  owner = nullptr;
  budget = nullptr;
  language = nullptr;
  ScRelease(&ordinances);
  for (u32 index = 0; index < 3; ++index) {
    if (totals[index]) {
      totals[index]->Release();
      totals[index] = nullptr;
    }
  }
  ScRelease(&scrollbar);
  if (departments) {
    for (u32 index = 0; index < department_count; ++index) {
      CtBudgetExpenseRow *row = &departments[index];
      row->identifier = 0;
      if (row->department) {
        row->department->Release();
        row->department = nullptr;
      }
      if (row->name) {
        row->name->Release();
        row->name = nullptr;
      }
      if (row->funding) {
        row->funding->Release();
        row->funding = nullptr;
      }
      if (row->button) {
        row->button->Release();
        row->button = nullptr;
      }
      if (row->year_total) {
        row->year_total->Release();
        row->year_total = nullptr;
      }
      if (row->estimated_total) {
        row->estimated_total->Release();
        row->estimated_total = nullptr;
      }
      if (row->year_box) {
        row->year_box->Release();
        row->year_box = nullptr;
      }
      if (row->estimated_box) {
        row->estimated_box->Release();
        row->estimated_box = nullptr;
      }
    }
    delete[] departments;
    departments = nullptr;
  }
  return true;
}

// Dispatch spinner and button notifications through separate payloads, retaining the inherited Boolean result.
// Unrelated messages return zero; initialization and shutdown retain their original window receiver.
bool CtBudgetIncomeView::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  switch (message) {
  case kWnMsg_None:
    return OnSpinnerChanged(window, u32(first), i32(u32(second)));
  case kWnMsg_Command:
    if (u32(first) != 1)
      return 0;
    return OnButtonActivated(window, u32(second));
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  default:
    return 0;
  }
}

// Process the real ordinance, neighbor, business and tax-help buttons after their optional BoxX event.
bool CtBudgetIncomeView::OnButtonActivated(WnWindow *, u32 identifier) {
  ApBoxX *box = ScGetMiscApplication()->GetBoxX();
  if (box)
    box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  switch (identifier) {
  case 50: {
    ScMessage message(kWnMsg_ModalOrdinanceDialog, 0, 0, 0);
    ScGetMiscMessageServer()->MessageSend(message);
    UpdateTotals();
    return true;
  }
  case 51: {
    UiNeighborWindow *dialog = nullptr;
    if (ScGetMiscDirector()->GZCOM()->GetClassObject(
            kUiNeighborWindowClassId, IID_UiNeighborWindow, reinterpret_cast<void **>(&dialog))) {
      dialog->SetupWindow();
      WnWindow *window = dialog->GetAssociatedIGZWin();
      if (window) {
        window->AddRef();
        ScGetMiscWindowManager()->DoModalWin(window);
        window->Shutdown();
        window->Release();
      }
      if (dialog)
        dialog->Release();
    }
    UpdateTotals();
    return true;
  }
  case 53:
  case 56: {
    ScMessage message(kWnMsg_ModalAdvisorBriefing, kAdBudgetAdvisorId, identifier == 53 ? 3004 : 3005, 0);
    ScGetMiscMessageServer()->MessageSend(message);
    return true;
  }
  case 55: {
    // The source reused its identifier as an unwritten failure output; initialize this native boundary.
    UiDialog62FA574B *dialog = nullptr;
    ScGetMiscDirector()->GZCOM()->GetClassObject(
        kUiBusinessDealsClassId, IID_UiDialog62FA574B, reinterpret_cast<void **>(&dialog));
    if (dialog) {
      dialog->Init();
      WnWindow *window = dialog->GetWindow();
      if (window) {
        window->AddRef();
        ScGetMiscWindowManager()->DoModalWin(window);
        window->Shutdown();
        window->Release();
      }
      dialog->Release();
    }
    UpdateTotals();
    return true;
  }
  default:
    if (identifier >= 175 && identifier < 178) {
      const u32 topics[3] = {
          kAdReplacementResidentialTaxes, kAdReplacementCommercialTaxes, kAdReplacementIndustrialTaxes};
      ScMessage message(kWnMsg_ModalAdvisorTopic, kAdBudgetAdvisorId, topics[identifier - 175], 0);
      ScGetMiscMessageServer()->MessageSend(message);
      return true;
    }
    return false;
  }
}

// Construct the four headers, five receipt rows and three actual tax spinners before refreshing live estimates.
bool CtBudgetIncomeView::OnInit(WnWindow *window) {
  ScString text, unused;
  CtDate current_date;
  u32 muted = window->MakeFillColor(127, 120, 176), blue = window->MakeFillColor(31, 32, 111);
  u32 black = window->MakeFillColor(0, 0, 0);
  UiGenericInterface *generic;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  u32 month = 0, day = 0, year = 0;
  city->GetDate(&month, &day, &year);
  remaining_months = 13 - month;
  i32 help_left = i32(u32(window->GetW()) - 64);
  i32 midpoint = i32(column_left >> 1), selection_width = i32(column_left - (column_left >> 1)), top = 0;
  const u32 header_ids[4] = {139, 153, 141, 142};
  for (u32 i = 0; i < 4; ++i) {
    {
      CtMiscTextResource resource(header_ids[i], kRsGameTextGroup);
      ScString value = resource.GetText(), copy(value);
      text = copy;
    }
    i32 x, width;
    if (!i) {
      x = midpoint;
      width = selection_width;
    } else if (i < 3) {
      x = i32(column_left + (i == 2 ? column_width >> 1 : 0));
      width = i32(column_width >> 1);
    } else {
      x = help_left;
      width = 48;
    }
    WnLabelControl *label;
    CtBudgetWindow::ConfigureWrappedLabel(&label, text, width, muted, nullptr);
    window->ChildAdd(label->GetWindow());
    label->SetAlignment(kWnLabelCenterBottom);
    label->GetWindow()->GZWinMoveTo(x, 2);
    if (top < label->GetWindow()->GetH())
      top = label->GetWindow()->GetH();
    label->Release();
  }
  top = i32(u32(top) + 14);
  i32 language_id = ScGetMiscLanguageManager()->GetCurrentLanguage();
  if (language_id == kScLanguageJapanese || language_id == kScLanguageSimplifiedChinese ||
      language_id == kScLanguageTraditionalChinese || language_id == kScLanguageKorean ||
      language_id == kScLanguageThai)
    top = i32(66 - ScGetMiscWindowResources()->GetFont(kWnFontUiHeading)->Height(0));
  const u32 category_ids[5] = {143, 144, 162, 648, 431};
  const u32 slots[5] = {1, 3, 4, 2, 0};
  const u32 button_instances[3] = {9, 8, 20}, button_ids[3] = {50, 51, 55};
  const u32 past_boxes[5] = {kCtBudgetFirstAccruedBoxId, kCtBudgetSecondAccruedBoxId, kCtBudgetThirdAccruedBoxId,
      kCtBudgetAdditionalAccruedBoxId, kCtBudgetAdditionalAccruedBoxId};
  const u32 future_boxes[5] = {kCtBudgetFirstProjectedBoxId, kCtBudgetSecondProjectedBoxId,
      kCtBudgetThirdProjectedBoxId, kCtBudgetAdditionalProjectedBoxId, kCtBudgetAdditionalProjectedBoxId};
  for (u32 i = 0; i < 5; ++i) {
    {
      CtMiscTextResource resource(category_ids[i], kRsGameTextGroup);
      ScString value = resource.GetText(), copy(value);
      text = copy;
    }
    WnLabelControl *label;
    CtBudgetWindow::ConfigureWrappedLabel(&label, text, midpoint, blue, nullptr);
    window->ChildAdd(label->GetWindow());
    WnWindow *vertical = label->GetWindow(), *moving = label->GetWindow();
    i32 height = vertical->GetH();
    moving->GZWinMoveTo(0, i32(u32(top) + u32((i32(row_height) - height) / 2)));
    label->Release();
    if (i < 3) {
      WnButton *button;
      CtBudgetWindow::ConfigureButton(&button, button_instances[i], kRsGonzoControlImageGroup, button_ids[i]);
      WnWindow *child = button->GetWindow();
      window->ChildAdd(child);
      i32 y = i32(u32(top) + u32((i32(row_height) - child->GetH()) / 2));
      i32 x = i32(u32(midpoint) + u32((selection_width - child->GetW()) / 2));
      child->GZWinMoveTo(x, y);
      child->Release();
    }
    i64 accrued;
    switch (i) {
    case 0:
      accrued = budget->ordinance_income.yearTotal;
      break;
    case 1:
      accrued = budget->neighbor_income.yearTotal;
      break;
    case 2:
      accrued = budget->business_income.yearTotal;
      break;
    case 3:
      accrued = budget->fare_income.yearTotal;
      break;
    default:
      accrued = budget->disaster_relief.yearTotal;
      break;
    }
    for (u32 side = 0; side < 2; ++side) {
      language->MakeMoneyString(side ? 0 : accrued, &text);
      if (side) {
        CtBudgetWindow::ConfigureLabel(&totals[slots[i]], text, black, nullptr);
        label = totals[slots[i]];
      } else {
        CtBudgetWindow::ConfigureLabel(&label, text, black, nullptr);
      }
      WnWindow *height_a = label->GetWindow(), *height_b = label->GetWindow(), *height_c = label->GetWindow();
      i32 y = i32(u32(top) + u32((i32(row_height) - height_c->GetH()) / 2));
      i32 offset = (i32(row_height) - height_b->GetH()) / 2;
      i32 bottom = i32(u32(offset) + u32(top) + u32(height_a->GetH()));
      i32 left = i32(column_left + (side ? column_width >> 1 : 0) + (side ? 1 : 3));
      i32 right = i32(column_left + (side ? column_width : column_width >> 1) - (side ? 3 : 1));
      ScRect rectangle = {left, y, right, bottom};
      WnWindow *box;
      CtBudgetWindow::ConfigureDigitBox(&box, i32(side ? future_boxes[i] : past_boxes[i]), rectangle, true);
      window->ChildAdd(box);
      window->ChildAdd(label->GetWindow());
      left = i32(column_left + (side ? column_width : column_width >> 1) - u32(label->GetWindow()->GetW()) - 2);
      vertical = label->GetWindow();
      moving = label->GetWindow();
      height = vertical->GetH();
      moving->GZWinMoveTo(left, i32(u32(top) + u32((i32(row_height) - height) / 2)));
      if (!side)
        label->Release();
      box->Release();
    }
    if (i == 1 || i == 2) {
      WnButton *button;
      CtBudgetWindow::ConfigureButton(&button, kRsHelpButtonImageId, kRsUiImageGroup, i == 1 ? 53 : 56);
      WnWindow *child = button->GetWindow();
      window->ChildAdd(child);
      i32 y = i32(u32(top) + u32((i32(row_height) - child->GetH()) / 2));
      i32 x = i32(u32(help_left) + u32((48 - child->GetW()) / 2));
      child->GZWinMoveTo(x, y);
      child->Release();
    }
    top = i32(u32(top) + row_height);
  }
  i32 spacing = ((window->GetH() - top) / i32(row_height) - 3) / 2;
  top = i32(u32(top) + row_height * u32(spacing));
  // Source cleanup after partial construction reads unwritten slots; zero native slots exclude those undefined reads.
  tax_totals = new WnLabelControl *[3]{};
  i32 spinner_left = i32(u32(midpoint) + u32((selection_width - 36) / 2));
  for (u32 i = 0; i < 3; ++i) {
    WnLabelControl *label;
    CtBudgetWindow::ConfigureLabel(&label, budget->taxes[i].name, blue, nullptr);
    window->ChildAdd(label->GetWindow());
    WnWindow *vertical = label->GetWindow(), *moving = label->GetWindow();
    i32 height = vertical->GetH();
    moving->GZWinMoveTo(0, i32(u32(top) + u32((i32(row_height) - height) / 2)));
    label->Release();
    WnButton *button;
    CtBudgetWindow::ConfigureButton(&button, kRsHelpButtonImageId, kRsUiImageGroup, 175 + i);
    WnWindow *child = button->GetWindow();
    window->ChildAdd(child);
    i32 y = i32(u32(top) + u32((i32(row_height) - child->GetH()) / 2));
    i32 x = i32(u32(help_left) + u32((48 - child->GetW()) / 2));
    child->GZWinMoveTo(x, y);
    child->Release();
    WnSpinnerControl *spinner = ScGetMiscWindowResources()->CreateSpinner(
        150 + i, 2, 0, budget->taxes[i].maximumRate, 1, budget->taxes[i].rate);
    spinner->GetWindow()->SetArea(spinner_left, top, i32(u32(spinner_left) + 36), i32(u32(top) + row_height));
    spinner->SetSpinnerFlag(1, true);
    spinner->SetSpinnerFlag(2, true);
    WnWindow *color_window = spinner->GetWindow(), *fill_window = spinner->GetWindow();
    u32 fill = color_window->MakeFillColor(255, 255, 255);
    fill_window->SetFillColor(fill);
    window->ChildAdd(spinner->GetWindow());
    spinner->Release();
    for (u32 side = 0; side < 2; ++side) {
      language->MakeMoneyString(side ? 0 : budget->taxes[i].yearTotal, &text);
      if (side) {
        CtBudgetWindow::ConfigureLabel(&tax_totals[i], text, black, nullptr);
        label = tax_totals[i];
      } else {
        CtBudgetWindow::ConfigureLabel(&label, text, black, nullptr);
      }
      WnWindow *height_a = label->GetWindow(), *height_b = label->GetWindow(), *height_c = label->GetWindow();
      y = i32(u32(top) + u32((i32(row_height) - height_c->GetH()) / 2));
      i32 offset = (i32(row_height) - height_b->GetH()) / 2;
      i32 bottom = i32(u32(offset) + u32(top) + u32(height_a->GetH()));
      i32 left = i32(column_left + (side ? column_width >> 1 : 0) + (side ? 1 : 3));
      i32 right = i32(column_left + (side ? column_width : column_width >> 1) - (side ? 3 : 1));
      ScRect rectangle = {left, y, right, bottom};
      WnWindow *box;
      CtBudgetWindow::ConfigureDigitBox(
          &box, side ? i32(kCtBudgetAdditionalProjectedBoxId) : i32(kCtBudgetAdditionalAccruedBoxId), rectangle, true);
      window->ChildAdd(box);
      window->ChildAdd(label->GetWindow());
      left = i32(column_left + (side ? column_width : column_width >> 1) - u32(label->GetWindow()->GetW()) - 2);
      vertical = label->GetWindow();
      moving = label->GetWindow();
      height = vertical->GetH();
      moving->GZWinMoveTo(left, i32(u32(top) + u32((i32(row_height) - height) / 2)));
      if (!side)
        label->Release();
      box->Release();
    }
    top = i32(u32(top) + row_height);
  }
  generic->Release();
  UpdateTotals();
  return true;
}

// Borrow the budget and parent callback, then query and retain the current city's ordinance layer separately.
// Copy the supplied pane rectangle only after borrowing the language utility, preserving service callback order.
CtBudgetIncomeView::CtBudgetIncomeView(
    EcBudget *budget, CtBudgetWindow *owner, u32 columnLeft, u32 columnWidth, const ScRect &area, u32 rowHeight)
    : owner(owner), budget(budget), ordinances(nullptr), column_left(columnLeft), column_width(columnWidth),
      row_height(rowHeight), totals{}, tax_totals(nullptr) {
  city = ScGetMiscApplication()->GetCity();
  city->GetSpecificLayer(kCtOrdinancesLayerType)
      ->QueryInterface(IID_CtOrdinanceLayer, reinterpret_cast<void **>(&ordinances));
  language = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  this->area = area;
}

CtBudgetIncomeView::~CtBudgetIncomeView() {
  ScRelease(&ordinances);
}

bool CtBudgetIncomeView::QueryInterface(u32, void **out) {
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtBudgetIncomeView::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 CtBudgetIncomeView::Release() {
  return ScMiscRefCount::Release();
}

// Acquire a generic child window with the original eight styles and this callback, then transfer its acquisition hold.
// Use the supplied rectangle's wrapping edge differences for the child area; no extra retain or release is performed.
bool CtBudgetIncomeView::CreateIncomeView(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, 0);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericCloseButtonState, 1);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericNoFrame, 1);
  generic->SetStyle(kUiGenericNoPainting, 1);
  generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtBudgetIncomeViewId);
  (*out)->SetArea(0, 0, i32(u32(area.right) - u32(area.left)), i32(u32(area.bottom) - u32(area.top)));
  return true;
}

// Clear borrowed services before releasing the ordinance layer and five summary labels in their original order.
// Release each of the three tax labels before deleting its array, clearing live fields only after their release calls.
bool CtBudgetIncomeView::OnShutdown(WnWindow *) {
  city = nullptr;
  owner = nullptr;
  budget = nullptr;
  language = nullptr;
  ScRelease(&ordinances);
  for (u32 i = 0; i < 5; ++i)
    if (totals[i]) {
      totals[i]->Release();
      totals[i] = nullptr;
    }
  if (tax_totals) {
    for (u32 i = 0; i < 3; ++i)
      if (tax_totals[i]) {
        tax_totals[i]->Release();
        tax_totals[i] = nullptr;
      }
    delete[] tax_totals;
    tax_totals = nullptr;
  }
  return true;
}

static i32 CtBondsWidth = 360, CtBondsHeight = 400;
static bool CtBondsLocationSet;
static i32 CtBondsLastX, CtBondsLastY;

// Borrow the concrete budget and language utility without retaining either service; construction owns no control holds.
// Native null initialization also covers source-unwritten controls, excluding undefined early-shutdown reads.
CtBondsDialog::CtBondsDialog(EcBudget *budget)
    : budget(budget), language(nullptr), rows(nullptr), borrowed_total(nullptr), warning(nullptr), issue(nullptr),
      spinner(nullptr) {
  language = ScGetMiscLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
}

CtBondsDialog::~CtBondsDialog() {
}

bool CtBondsDialog::QueryInterface(u32, void **out) {
  *out = nullptr;
  RETURN_QUERY_INTERFACE_FAILURE();
}

u32 CtBondsDialog::AddRef() {
  return ScMiscRefCount::AddRef();
}

u32 CtBondsDialog::Release() {
  return ScMiscRefCount::Release();
}

// Route spinner words, button activation and lifecycle messages to the original dialog handlers.
// Other window messages remain unhandled; the partial initialization handler stops at its explicit missing dependency.
bool CtBondsDialog::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  switch (message) {
  case kWnMsg_None:
    return OnSpinnerChanged(window, i32(u32(first)), i32(u32(second)));
  case kWnMsg_Command:
    return u32(first) == 1 ? OnButtonActivated(window, u32(second)) : 0;
  case kWnMsg_Init:
    return OnInit(window);
  case kWnMsg_Shutdown:
    return OnShutdown(window);
  default:
    return 0;
  }
}

// Acquire a generic dialog and install its original styles, callback, identifier and language-dependent client height.
// Reuse its remembered screen location when visible, otherwise center using the message server's packed screen extents.
bool CtBondsDialog::CreateBondsDialog(WnWindow **out) {
  *out = nullptr;
  UiGenericInterface *generic;
  if (!ScGetMiscDirector()->GZCOM()->GetClassObject(
          kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->SetStyle(kUiGenericDraggable, 1);
  generic->SetStyle(kUiGenericResizable, 0);
  generic->SetStyle(kUiGenericScroller, 0);
  generic->SetStyle(kUiGenericCloseButtonState, 1);
  generic->SetStyle(kUiGenericSecondButton, 0);
  generic->SetStyle(kUiGenericCloseButton, 0);
  generic->SetCallback(this);
  *out = generic->GetWindow();
  (*out)->SetID(kCtBondsDialogId);
  (*out)->SetFlag(kWnFlagPrivateBuffer, true);
  i32 languageId = ScGetMiscLanguageManager()->GetCurrentLanguage();
  if (languageId == kScLanguageJapanese || languageId == kScLanguageSimplifiedChinese ||
      languageId == kScLanguageTraditionalChinese || languageId == kScLanguageKorean || languageId == kScLanguageThai)
    CtBondsHeight = 460;
  (*out)->SetArea(0, 0, CtBondsWidth, CtBondsHeight);
  ScRect bounds = *(*out)->GetMainWindow()->GetArea();
  if (CtBondsLocationSet &&
      (CtBondsLastX > bounds.right || CtBondsLastY > bounds.bottom ||
          i32(u32(CtBondsLastX) + u32(CtBondsWidth)) < bounds.left ||
          i32(u32(CtBondsLastY) + u32(CtBondsHeight)) < bounds.top))
    CtBondsLocationSet = false;
  if (!CtBondsLocationSet) {
    ScMessage message(kWnMsg_QueryUiClientArea);
    ScGetMiscMessageServer()->MessageSend(message);
    CtBondsLastX = i32(u32(u16(message.data2 >> 16)) - u32(CtBondsWidth) - u32(u16(message.data1 >> 16))) / 2;
    CtBondsLastY = i32(u32(u16(message.data2)) - u32(CtBondsHeight) - u32(u16(message.data1))) / 2;
    if (CtBondsLastX < 0)
      CtBondsLastX = 0;
    if (CtBondsLastY < 0)
      CtBondsLastY = 0;
    CtBondsLocationSet = true;
  }
  (*out)->GZWinMoveTo(CtBondsLastX, CtBondsLastY);
  return true;
}

// Release each bond row's four labels using the captured signed configured maximum, then retire the current row array.
// Release summary labels, issue button and spinner before capturing the final location for the next dialog.
bool CtBondsDialog::OnShutdown(WnWindow *window) {
  if (rows) {
    i32 count = i32(EcBudgetMaximumBonds);
    for (i32 i = 0; i < count; ++i) {
      CtBondRow *row = rows + i;
      for (u32 j = 0; j < 4; ++j)
        if (row->labels[j]) {
          row->labels[j]->Release();
          row->labels[j] = nullptr;
        }
    }
    delete[] rows;
    rows = nullptr;
  }
  ScRelease(&borrowed_total);
  ScRelease(&warning);
  ScRelease(&issue);
  ScRelease(&spinner);
  CtBondsLastX = window->GetL();
  CtBondsLastY = window->GetT();
  CtBondsLocationSet = true;
  return true;
}

// Report the button to BoxX before closing, issuing the selected bond or sending the original bond-help message.
// Issuing ignores the budget's predicate and refreshes the controls afterward; unsupported identifiers return false.
bool CtBondsDialog::OnButtonActivated(WnWindow *window, u32 identifier) {
  ApBoxX *box = ScGetMiscApplication()->GetBoxX();
  if (box) {
    if (identifier == 32)
      box->Event(21, 0, 0, 0, 0);
    else
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
  }
  switch (identifier) {
  case 32:
    return ScGetMiscWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
  case 58:
    budget->IssueBond(u32(amount));
    RefreshTotals();
    return true;
  case 60: {
    ScMessage message(kWnMsg_ModalAdvisorBriefing, kAdBudgetAdvisorId, 3006, 0);
    ScGetMiscMessageServer()->MessageSend(message);
    return true;
  }
  default:
    return false;
  }
}

// Accept only the bond-amount spinner, recording changed values after the optional BoxX event and before refreshing.
// An unchanged amount is still handled but performs no event or refresh.
bool CtBondsDialog::OnSpinnerChanged(WnWindow *, i32 identifier, i32 value) {
  if (identifier != 57)
    return false;
  if (amount != value) {
    ApBoxX *box = ScGetMiscApplication()->GetBoxX();
    if (box)
      box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    amount = value;
    RefreshTotals();
  }
  return true;
}

// Format live bond rows before centering their columns. Then update debt color and borrowing eligibility.
void CtBondsDialog::RefreshTotals() {
  ScString text;
  i32 maximumWidth[4] = {};
  for (i32 i = 0; i < i32(budget->bond_count); ++i)
    for (u32 j = 0; j < 4; ++j) {
      if (!j)
        language->MakeDateString(budget->bonds[i].month, 0, budget->bonds[i].maturity_year, &text,
            ScLanguageUtility::DateFormat::NumericMonthYear);
      else {
        i32 value;
        if (j == 1)
          value = i32(budget->bonds[i].total_payments);
        else if (j == 2)
          value = i32(budget->bonds[i].remaining_payments);
        else
          value = i32(budget->bonds[i].paid);
        language->MakeMoneyString(value, &text);
      }
      rows[i].labels[j]->SetCaption(text);
      rows[i].labels[j]->Fit();
      if (rows[i].labels[j]->GetWindow()->GetW() > maximumWidth[j])
        maximumWidth[j] = rows[i].labels[j]->GetWindow()->GetW();
    }
  i32 left = area.left, width = i32(u32(area.right) - u32(left) + 1) / 4;
  i32 offsets[4];
  for (u32 j = 0; j < 4; ++j)
    offsets[j] = i32(u32(width) - u32(maximumWidth[j])) / 2;
  for (i32 i = 0; i < i32(budget->bond_count); ++i) {
    for (u32 j = 0; j < 4; ++j) {
      WnWindow *vertical = rows[i].labels[j]->GetWindow(), *moving = rows[i].labels[j]->GetWindow();
      i32 top = vertical->GetT();
      moving->GZWinMoveTo(i32(u32(left) + j * u32(width) + u32(offsets[j])), top);
    }
    left = area.left;
  }
  i32 right = i32(u32(left) + 3 * u32(width));
  i64 borrowed = budget->GetTotalBorrowed(), limit = budget->GetCurrentBorrowingLimit();
  language->MakeMoneyString(borrowed, &text);
  WnWindow *colorWindow = borrowed_total->GetWindow();
  u32 color = borrowed > 0 ? colorWindow->MakeFillColor(255, 0, 0) : colorWindow->MakeFillColor(0, 0, 0);
  borrowed_total->SetTextColor(color);
  borrowed_total->SetCaption(text);
  borrowed_total->Fit();
  WnWindow *vertical = borrowed_total->GetWindow(), *measured = borrowed_total->GetWindow();
  WnWindow *moving = borrowed_total->GetWindow();
  i32 top = vertical->GetT(), measuredWidth = measured->GetW();
  moving->GZWinMoveTo(i32(u32(right) - u32(measuredWidth)), top);
  bool eligible = borrowed < limit && budget->bond_count < EcBudgetMaximumBonds;
  issue->GetWindow()->SetFlag(kWnFlagEnabled, eligible);
  spinner->GetWindow()->SetFlag(kWnFlagVisible, eligible);
  warning->GetWindow()->SetFlag(kWnFlagVisible, !eligible);
  colorWindow = issue->GetWindow();
  issue->SetFontColor(5, colorWindow->MakeFillColor(128, 128, 128));
  colorWindow = issue->GetWindow();
  issue->SetFontColor(4, colorWindow->MakeFillColor(64, 64, 64));
}

// Build the original bond rows, framed image buttons, totals, issue controls and warning before refreshing live values.
bool CtBondsDialog::OnInit(WnWindow *window) {
  ScString text;
  u32 blue = window->MakeFillColor(31, 32, 111), black = window->MakeFillColor(0, 0, 0);
  u32 red = window->MakeFillColor(255, 0, 0);
  FtFont *font = ScGetMiscWindowResources()->GetFont(kWnFontUiSmall);
  UiGenericInterface *generic;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return false;
  generic->GetClientArea(&area);
  i32 width = i32(u32(area.right) - u32(area.left) + 1) / 4;
  {
    CtMiscTextResource resource(kRsGameLoansText, kRsGameTextGroup);
    ScString value = resource.GetText();
    ScString copy(value);
    text = copy;
  }
  window->SetCaption(text);
  i32 x = area.left, y = area.top, headerHeight = 0;
  for (u32 i = 0; i < 4; ++i) {
    {
      CtMiscTextResource resource(435 + i, kRsGameTextGroup);
      ScString value = resource.GetText();
      ScString copy(value);
      text = copy;
    }
    WnLabelControl *label;
    CtBudgetWindow::ConfigureWrappedLabel(&label, text, width, blue, font);
    window->ChildAdd(label->GetWindow());
    label->SetAlignment(kWnLabelCenterBottom);
    label->GetWindow()->GZWinMoveTo(x, y);
    if (!i)
      headerHeight = label->GetWindow()->GetH();
    else if (headerHeight < label->GetWindow()->GetH())
      headerHeight = label->GetWindow()->GetH();
    label->Release();
    x = i32(u32(x) + u32(width));
  }
  x = area.left;
  // Zero the pointer array to exclude the source's undefined cleanup reads after interrupted row construction.
  rows = new CtBondRow[EcBudgetMaximumBonds]{};
  y = i32(u32(y) + u32(headerHeight) + 3);
  i32 rowTop = y, rowHeight = headerHeight;
  for (i32 i = 0; i < i32(EcBudgetMaximumBonds); ++i) {
    for (u32 j = 0; j < 4; ++j) {
      if (u32(i) >= budget->bond_count) {
        ScString empty("");
        text = empty;
      } else if (!j)
        language->MakeDateString(budget->bonds[i].month, 0, budget->bonds[i].maturity_year, &text,
            ScLanguageUtility::DateFormat::NumericMonthYear);
      else {
        i32 value;
        if (j == 1)
          value = i32(budget->bonds[i].total_payments);
        else if (j == 2)
          value = i32(budget->bonds[i].remaining_payments);
        else
          value = i32(budget->bonds[i].paid);
        language->MakeMoneyString(value, &text);
      }
      WnLabelControl *label;
      CtBudgetWindow::ConfigureLabel(&label, text, j == 2 ? red : j == 3 ? black : blue, font);
      window->ChildAdd(label->GetWindow());
      label->GetWindow()->GZWinMoveTo(x, y);
      rows[i].labels[j] = label;
      if (j < 3)
        x = i32(u32(x) + u32(width));
      else {
        rowHeight = label->GetWindow()->GetH();
        y = i32(u32(y) + u32(rowHeight));
      }
    }
    x = area.left;
  }
  i32 bottom = i32(u32(y) + 1);
  ScRect border = {i32(u32(area.left) + 2), i32(u32(rowTop) - 1), i32(u32(area.right) - 2), y};
  WnWindow *frame;
  CtBudgetWindow::ConfigureDigitBox(&frame, kCtBondRowsFrameId, border, false);
  window->ChildAdd(frame);
  frame->Release();
  u32 stripeHeight = (u32(bottom) - u32(rowTop)) / (EcBudgetMaximumBonds >> 1), identifier = 225;
  i32 lastTop = i32(u32(bottom) - stripeHeight), stripeTop = rowTop;
  while (stripeTop <= lastTop) {
    WnFlatControl *control = nullptr;
    if (ScGetMiscDirector()->GZCOM()->GetClassObject(
            kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&control))) {
      u32 fill = control->GetWindow()->MakeFillColor(255, 255, 255);
      u16 edge = u16(control->GetWindow()->MakeFillColor(255, 255, 255));
      WnWindow *stripe = control->GetWindow();
      stripe->SetID(identifier);
      stripe->SetArea(
          i32(u32(area.left) + 3), stripeTop, i32(u32(area.right) - 3), i32(u32(stripeTop) + stripeHeight - 1));
      stripe->SetFillColor(fill);
      control->SetOutlineColor(edge);
      control->SetSizeMode(0);
      control->SetOutlineFlags(15);
      window->ChildAdd(stripe);
      stripe->SetFlag(kWnFlagReorderable, true);
      window->ChildToBack(stripe);
      stripe->SetFlag(kWnFlagReorderable, false);
      stripe->Release();
      ++identifier;
      stripeTop = i32(u32(stripeTop) + 2 * stripeHeight);
    }
  }
  y = i32(u32(bottom) + u32(rowHeight) + 2);
  x = area.left;
  WnButton *button;
  CtBudgetWindow::ConfigureButton(&button, kRsHelpButtonImageId, kRsUiImageGroup, 60);
  WnWindow *buttonWindow = button->GetWindow();
  window->ChildAdd(buttonWindow);
  x = i32(u32(x) + u32((width - buttonWindow->GetW()) / 2));
  buttonWindow->GZWinMoveTo(x, y);
  i32 panelBottom = i32(u32(buttonWindow->GetH()) + u32(y) + 2);
  border = {i32(u32(x) - 2), i32(u32(y) - 2), i32(u32(buttonWindow->GetW()) + u32(x) + 2), panelBottom};
  generic->AddPanel(kCtBondsAdvisorButtonPanelId, 0, 15, &border);
  buttonWindow->SetFlag(kWnFlagReorderable, true);
  window->ChildToFront(buttonWindow);
  buttonWindow->SetFlag(kWnFlagReorderable, false);
  y = i32(u32(buttonWindow->GetH()) + u32(y));
  {
    CtMiscTextResource resource(kRsGameConsultFinancialAdvisorText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  WnLabelControl *label;
  CtBudgetWindow::ConfigureWrappedLabel(&label, text, width, blue, font);
  window->ChildAdd(label->GetWindow());
  label->SetAlignment(kWnLabelCenterTop);
  i32 halfWidth = label->GetWindow()->GetW() / 2;
  x = i32(u32(x) + u32(buttonWindow->GetW() / 2 - halfWidth));
  label->GetWindow()->GZWinMoveTo(x, y);
  label->Release();
  buttonWindow->Release();
  y = i32(u32(bottom) + 2);
  x = i32(u32(area.left) + 2 * u32(width));
  {
    CtMiscTextResource resource(kRsGameBorrowedText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  CtBudgetWindow::ConfigureLabel(&label, text, blue, font);
  window->ChildAdd(label->GetWindow());
  WnWindow *sizeWindow = label->GetWindow(), *moveWindow = label->GetWindow();
  moveWindow->GZWinMoveTo(i32(u32(x) - u32(sizeWindow->GetW())), y);
  label->Release();
  language->MakeMoneyString(budget->GetTotalBorrowed(), &text);
  CtBudgetWindow::ConfigureLabel(&borrowed_total, text, blue, font);
  window->ChildAdd(borrowed_total->GetWindow());
  sizeWindow = borrowed_total->GetWindow();
  moveWindow = borrowed_total->GetWindow();
  moveWindow->GZWinMoveTo(i32(u32(width) + u32(x) - u32(sizeWindow->GetW())), y);
  y = i32(u32(y) + u32(borrowed_total->GetWindow()->GetH()));
  x = i32(u32(area.left) + 2 * u32(width));
  {
    CtMiscTextResource resource(kRsGameBorrowingLimitText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  CtBudgetWindow::ConfigureLabel(&label, text, blue, font);
  window->ChildAdd(label->GetWindow());
  sizeWindow = label->GetWindow();
  moveWindow = label->GetWindow();
  moveWindow->GZWinMoveTo(i32(u32(x) - u32(sizeWindow->GetW())), y);
  label->Release();
  language->MakeMoneyString(i32(budget->GetCurrentBorrowingLimit()), &text);
  CtBudgetWindow::ConfigureLabel(&label, text, blue, font);
  window->ChildAdd(label->GetWindow());
  sizeWindow = label->GetWindow();
  moveWindow = label->GetWindow();
  moveWindow->GZWinMoveTo(i32(u32(width) + u32(x) - u32(sizeWindow->GetW())), y);
  label->GetWindow()->GetH();
  label->Release();
  CtBudgetWindow::ConfigureButton(&button, kRsStandardButtonImageId, kRsUiImageGroup, 32);
  buttonWindow = button->GetWindow();
  button->SetButtonFlag(kWnButtonShowText, false);
  window->ChildAdd(buttonWindow);
  x = i32(u32(area.right) - u32(buttonWindow->GetW()) - 2);
  y = i32(u32(area.bottom) - u32(buttonWindow->GetH()) - 4);
  buttonWindow->GZWinMoveTo(x, y);
  panelBottom = i32(u32(buttonWindow->GetH()) + u32(y) + 2);
  border = {i32(u32(x) - 2), i32(u32(y) - 2), i32(u32(buttonWindow->GetW()) + u32(x) + 2), panelBottom};
  generic->AddPanel(kCtBondsCloseButtonPanelId, 0, 15, &border);
  buttonWindow->SetFlag(kWnFlagReorderable, true);
  window->ChildToFront(buttonWindow);
  buttonWindow->SetFlag(kWnFlagReorderable, false);
  buttonWindow->Release();
  x = area.left;
  {
    CtMiscTextResource resource(kRsGameAcceptLoanText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  CtBudgetWindow::ConfigureResizableButton(&button, 21, 58, false);
  buttonWindow = button->GetWindow();
  buttonWindow->SetCaption(text);
  window->ChildAdd(buttonWindow);
  button->Fit();
  buttonWindow->GZWinMoveTo(i32(u32(area.left) + 2 * u32(width) - u32(buttonWindow->GetW() / 2)), y);
  i32 issueWidth = buttonWindow->GetW();
  y = i32(u32(y) - 26);
  amount = 5000;
  x = i32(u32(x) + u32(issueWidth) + 8);
  issue = button;
  spinner = ScGetMiscWindowResources()->CreateSpinner(57, 2, 5000, i32(EcBudgetBondValue), 5000, amount);
  if (spinner) {
    WnWindow *spinnerWindow = spinner->GetWindow();
    spinner->SetFont(font);
    spinner->SetSpinnerFlag(1, true);
    spinner->SetSpinnerFlag(2, true);
    u32 fill = spinnerWindow->MakeFillColor(255, 255, 255);
    spinnerWindow->SetFillColor(fill);
    x = i32(u32(area.left) + u32(3 * width / 2));
    spinnerWindow->SetArea(x, y, i32(u32(x) + u32(width)), i32(u32(y) + 24));
    window->ChildAdd(spinnerWindow);
  }
  {
    CtMiscTextResource resource(kRsGame10LoanLimitText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  CtBudgetWindow::ConfigureLabel(&label, text, black, font);
  window->ChildAdd(label->GetWindow());
  sizeWindow = label->GetWindow();
  moveWindow = label->GetWindow();
  moveWindow->GZWinMoveTo(i32(u32(x) + u32((width - sizeWindow->GetW()) / 2)), y);
  warning = label;
  {
    CtMiscTextResource resource(kRsGameNewLoanText, kRsGameTextGroup);
    ScString value = resource.GetText(), copy(value);
    text = copy;
  }
  CtBudgetWindow::ConfigureLabel(&label, text, blue, font);
  window->ChildAdd(label->GetWindow());
  y = i32(u32(y) - 2 - u32(label->GetWindow()->GetH()));
  sizeWindow = label->GetWindow();
  moveWindow = label->GetWindow();
  moveWindow->GZWinMoveTo(i32(u32(area.left) + 2 * u32(width) - u32(sizeWindow->GetW() / 2)), y);
  label->Release();
  x = i32(u32(width) + u32(area.left));
  border = {x, i32(u32(y) - 2), i32(u32(x) + 2 * u32(width)), i32(u32(area.bottom) - 2)};
  generic->AddPanel(kCtBondSummaryPanelId, 0, 15, &border);
  WnWindow *panel = window->GetChildWindowFromID(kCtBondSummaryPanelId);
  panel->SetFlag(kWnFlagReorderable, true);
  window->ChildToBack(panel);
  panel->SetFlag(kWnFlagReorderable, false);
  generic->Release();
  RefreshTotals();
  return true;
}
