#include "ui/neighbor_window.h"

#include <assert.h>

#include "advisors/ad_petitions.h"
#include "application/api.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/city.h"
#include "city/ct_layer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3_director.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "economy/api.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "terrain/tr_thumbnail.h"
#include "ui/api.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// Opening the neighbor-deal petition passes the reserved all-ones petitioner identity to the modal request.
enum UiNeighborPetitionerRequest : u32 {
  kUiNeighborDealPetitionerRequestId = 0xffffffff,
};

enum UiNeighborControlId : u32 {
  kUiNeighborNameTopLeftId = 0x43068569,
  kUiNeighborNameTopRightId = 0x4306856a,
  kUiNeighborNameBottomLeftId = 0x4306856b,
  kUiNeighborNameBottomRightId = 0x4306856c,
  kUiNeighborCurrentCityNameId = 0x43068571,
  kUiNeighborPopulationTopLeftId = 0x4306856d,
  kUiNeighborPopulationTopRightId = 0x4306856e,
  kUiNeighborPopulationBottomLeftId = 0x4306856f,
  kUiNeighborPopulationBottomRightId = 0x43068570,
  kUiNeighborCurrentCityPopulationId = 0x43068572,
  kUiNeighborBorderTopLeftIdBase = 0x43068583,
  kUiNeighborBorderTopRightIdBase = 0x43068585,
  kUiNeighborBorderBottomRightIdBase = 0x43068589,
  kUiNeighborBorderBottomLeftIdBase = 0x43068587,
  kUiFirstNeighborSelectionIdBase = 0x4306858b,
  kUiSecondNeighborSelectionIdBase = 0x4306858e,
  kUiThirdNeighborSelectionIdBase = 0x43068594,
  kUiFourthNeighborSelectionIdBase = 0x43068591,
  kUiNeighborSelectionLastId = 0x43068596,
  kUiNeighborTitleSegmentIdBase = 0xc3499f0d,
  kUiNeighborMapControlId = 0xa302fc74,
  kUiNeighborNameEditId = 0x43068564,
  kUiMeetNeighborButtonId = 0x43068566,
  kUiNeighborPortraitIdBase = 0x4306857f,
  kUiNeighborDirectionBitmapIdBase = 0x43068573,
};

// Return the constructed neighbor owner's actual primary query interface without another factory retain.
ScUnknown *UiCreateClass4302B412() {
  return static_cast<UiNeighborWindow *>(new UiNeighborWindowObject);
}

// Pause the captured city while successive neighbor dialogs retain their selected neighbor between modal runs.
// Keep the original city window and manager throughout the loop, then resume that same city on completion.
bool UiNeighborCommand::Execute() {
  CtCity *city = UiGetCity();
  WnWindowManager *manager = UiGetWindowManager();
  SpCityWindowView *city_window = UiGetCityWindow();
  if (!city || !manager || !city_window)
    return false;
  city->SimHiddenPause();
  u32 selected = 0;
  bool again = true;
  do {
    UiNeighborWindowObject *dialog = new UiNeighborWindowObject;
    dialog->AddRef();
    dialog->SetSelectedNeighborId(selected);
    dialog->SetupWindow();
    WnWindow *window = dialog->GetAssociatedIGZWin();
    if (window) {
      window->AddRef();
      again = manager->DoModalWin(city_window->AsIGZWin(), window) != 0;
      selected = dialog->GetSelectedNeighborId();
      manager->DestroyWindow(window);
      window->Release();
    }
    dialog->Release();
  } while (again);
  city->SimHiddenUnpause();
  return true;
}

// These module-wide coordinates are changed by the original locale-dependent setup path.
// Their initial values come from installed SIMUI data; SetupWindow preserves the original locale writes.
static i32 neighbor_left = 113, neighbor_middle = 173, neighbor_right = 213;
static i32 neighbor_top = 82, neighbor_center = 132, neighbor_bottom = 182, neighbor_text_width = 100;
static i32 neighbor_icon_top_left_y = 153, neighbor_icon_top_right_y = 153;
static i32 neighbor_icon_bottom_left_y = 253, neighbor_icon_bottom_right_y = 253;
static i32 neighbor_icon_left_x = 130, neighbor_icon_right_x = 230;

// Create the neighbor compass and its four rotated, localized direction labels.
u8 UiNeighborWindowObject::SetupCompassWindow() {
  u32 transparent = UiGetWindowResources()->GetColor(kWnColorUiTransparency);
  i32 zoom = 0, rotation = 0;
  view->GetZoomAndRotation(&zoom, &rotation);
  {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image_ref(13, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *image = image_ref.GetBuffer();
    if (image) {
      ScRect area = *image->GetBufferArea();
      i32 dx = (i32)(20u - (u32)area.left), dy = (i32)(50u - (u32)area.top);
      area.left = 20;
      area.top = 50;
      area.right = (i32)((u32)area.right + (u32)dx);
      area.bottom = (i32)((u32)area.bottom + (u32)dy);
      image->SetTransparency(transparent);
      if (UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&held[1]))) {
        WnBitmapControl *bitmap = static_cast<WnBitmapControl *>(held[1]);
        bitmap->SetImage(image);
        bitmap->GetWindow()->SetArea(area.left, area.top, area.right, area.bottom);
        bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
        WnWindow *parent = window->GetWindow();
        parent->ChildAdd(bitmap->GetWindow());
        area.left = (i32)((u32)area.left + 3);
        area.right = (i32)((u32)area.right - 3);
        static const u32 alignments[4] = {3, 1, 5, 7};
        for (u32 index = 0; index < 4; ++index) {
          {
            ScString empty;
            held[index + 2] = UiGetWindowResources()->CreateLabel(16896 + index, empty);
          }
          WnLabelControl *label = static_cast<WnLabelControl *>(held[index + 2]);
          if (!label)
            continue;
          label->GetWindow()->SetFlag(kWnFlagReorderable, false);
          label->GetWindow()->SetArea(area);
          label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiTitle));
          u32 first = label->GetWindow()->MakeFillColor(214, 218, 255);
          label->SetTextColor(0, first);
          u32 second = label->GetWindow()->MakeFillColor(124, 144, 255);
          label->SetTextColor(1, second);
          label->SetAlignment(alignments[index]);
          label->GetWindow()->SetFlag(kWnFlagReorderable, false);
          WnWindow *view_window = window->GetWindow();
          view_window->ChildAdd(label->GetWindow());
        }
      }
    }
  }
  if (held[2] && held[3] && held[4] && held[5]) {
    static const u32 text_ids[4] = {0x11d, 0x11e, 0x120, 0x11f};
    ScString text;
    for (u32 index = 0; index < 4; ++index) {
      {
        UiTextRef reference(text_ids[index], kRsGameTextGroup);
        text = reference.GetText();
      }
      u32 slot = ((u32)rotation + index) % 4;
      static_cast<WnLabelControl *>(held[slot + 2])->SetCaption(text);
    }
  }
  return 1;
}

// Create coast arrows for unavailable neighbors in the current camera rotation.
u8 UiNeighborWindowObject::SetupCoastNeighborArrows() {
  static const u8 positions[16] = {1, 0, 2, 3, 0, 2, 3, 1, 2, 3, 1, 0, 3, 1, 0, 2};
  static const u32 images[4] = {40, 43, 41, 35};
  static const i32 left[4] = {110, 238, 110, 238}, top[4] = {125, 125, 245, 245};
  for (u32 i = 0; i < neighbors.size; ++i) {
    UiNeighborRecord *record = neighbors.data[i].record;
    if (!record || record->available)
      continue;
    i32 zoom = 0, rotation = 0;
    view->GetZoomAndRotation(&zoom, &rotation);
    u32 index = (u32)rotation * 4 + record->id;
    assert(index < 16);
    u32 position = positions[index];
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image_ref(images[position], kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    WnBitmapControl *bitmap = nullptr;
    if (!UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)))
      continue;
    GxBuffer *image = image_ref.GetBuffer();
    assert(image);
    image->AddRef();
    u32 width = image->Width(), height = image->Height();
    bitmap->GetWindow()->SetID(kUiNeighborPortraitIdBase + position);
    bitmap->GetWindow()->SetArea(0, 0, (i32)width, (i32)height);
    bitmap->SetBitmapFlag(1, true);
    bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
    u32 color = bitmap->GetWindow()->MakeFillColor(255, 0, 255);
    image->SetTransparency(color);
    bitmap->SetImage(image);
    bitmap->GetWindow()->GZWinMoveTo(left[position], top[position]);
    WnWindow *parent = window->GetWindow();
    if (!parent->ChildAdd(bitmap->GetWindow())) {
      UiGetWindowManager()->DestroyWindow(bitmap->GetWindow());
      bitmap->Release();
      return 0;
    }
    bitmap->Release();
    image->Release();
  }
  return 1;
}

UiNeighborDeal::UiNeighborDeal(
    u8 type, u32 category, const ScStringView &description, const ScStringView &rate, const ScStringView &contribution)
    : type(type), category(category), description(description), rate(rate), contribution(contribution) {
}

UiNeighborRecord::UiNeighborRecord(
    UiNeighborWindowObject *owner, u32 id, const ScStringView &name, u32 population, u8 available)
    : owner(owner), id(id), name(name), population(population), deal_window(nullptr), available(available) {
}

UiNeighborRecord::~UiNeighborRecord() {
  ScRelease(&deal_window);
  for (u32 i = 0; i < deals.size; ++i)
    delete deals.data[i];
}

// Replace the neighbor's deal list, pad each description and rate to the source font's two measured columns,
// then configure and attach the resulting list while retaining the original release and callback order.
u8 UiNeighborRecord::CreateDealWindow() {
  if (deal_window) {
    WnWindow *parent = owner->window->GetWindow();
    parent->ChildRemove(deal_window->GetWindow());
    deal_window->Release();
    deal_window = nullptr;
  }
  if (!UiGetCom()->GetClassObject(kWnListClassId, IID_WnListControl, reinterpret_cast<void **>(&deal_window)) ||
      !deal_window)
    return 0;
  UiNeighborDeal **current = deals.data, **end = deals.size ? deals.data + deals.size : deals.data;
  while (current != end) {
    UiNeighborDeal *deal = *current;
    ScString text;
    FtFont *font = UiGetWindowResources()->GetFont(kWnFontUiSmall);
    text = deal->description;
    u32 length = text.Strlen();
    while (font->Fit(text.Data(), length, 145) == length) {
      text.Append(" ");
      length = text.Strlen();
    }
    text.Append(deal->rate.Data(), deal->rate.Strlen());
    length = text.Strlen();
    while (font->Fit(text.Data(), length, 232) == length) {
      text.Append(" ");
      length = text.Strlen();
    }
    text.Append(deal->contribution.Data(), deal->contribution.Strlen());
    deal_window->InsertString(text, -2);
    ++current;
  }
  WnWindow *list_window = deal_window->GetWindow();
  u32 blue = list_window->MakeFillColor(0, 0, 255);
  deal_window->SetHighlightColor(blue);
  deal_window->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
  deal_window->GetWindow()->SetArea(ScRect{30, 380, 341, 440});
  deal_window->SetOutlineDrawing(true);
  deal_window->EnableScrollBar(false, false);
  WnResources *resources = UiGetWindowResources();
  list_window = deal_window->GetWindow();
  list_window->SetFillColor(resources->GetColor(kWnThemeBackgroundColorId));
  deal_window->SetSelection(0, false);
  WnWindow *parent = owner->window->GetWindow();
  bool attached = parent->ChildAdd(deal_window->GetWindow());
  if (attached)
    deal_window->GetWindow()->HideWindow();
  return attached;
}

UiNeighborWindowObject::UiNeighborWindowObject()
    : references(0), city(nullptr), view(nullptr), window(nullptr), held{}, selected(0) {
}

UiNeighborWindowObject::~UiNeighborWindowObject() {
}

// Create the neighbor dialog, retain its city and view, then build controls in the original short-circuit order.
// An existing window returns zero; any failed setup byte enters the original shutdown path.
u8 UiNeighborWindowObject::SetupWindow() {
  if (window)
    return 0;
  u32 language = UiGetLanguageManager()->GetCurrentLanguage();
  if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai) {
    neighbor_left = 106;
    neighbor_middle = 160;
    neighbor_right = 214;
    neighbor_top = 82;
    neighbor_center = 132;
    neighbor_bottom = 182;
    neighbor_text_width = 100;
    neighbor_icon_top_left_y = 146;
    neighbor_icon_top_right_y = 146;
    neighbor_icon_bottom_left_y = 254;
    neighbor_icon_bottom_right_y = 254;
    neighbor_icon_left_x = 130;
    neighbor_icon_right_x = 230;
  }
  if (!UiGetCity())
    return 0;
  city = UiGetCity();
  SpCityWindowView *city_window = UiGetCityWindow();
  if (!city_window)
    return 0;
  view = city_window->GetCurrentView();
  if (!view)
    return 0;
  view->AddRef();
  city->AddRef();
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result && window) {
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericBackButtonState, 1);
    window->SetStyle(kUiGenericSecondButtonState, 1);
    window->GetWindow()->PrivateBuffer(true);
    result = window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  assert(window);
  window->GetWindow()->SetArea(ScRect{50, 50, 422, 495});
  ScString caption;
  {
    UiTextRef reference(kRsGameNeighborsMapText, kRsGameTextGroup);
    caption = reference.GetText();
  }
  window->GetWindow()->SetCaption(caption);
  EnumCityItems(city, 1);
  if (result)
    result = SetupSolidBackground();
  if (result)
    result = SetupCompassWindow();
  if (result)
    result = SetupDealWindows();
  if (result)
    result = SetupMapControlWindow();
  if (result)
    result = SetupCoastNeighborArrows();
  if (result)
    result = SetupMapCityAndNeighborTextWindows();
  if (result)
    result = SetupMeetNeighborWindow();
  if (result)
    result = SetupNationalPopulationWindow();
  if (result)
    result = SetupNameEditWindow();
  if (result)
    result = SetupNamesAndPops();
  if (result)
    result = SetupDealIconsAndIconWindows();
  if (result)
    result = UpdateAllDealIcons();
  if (!result)
    OnShutdown(nullptr);
  return result;
}

// Publish the requested primary, window-callback or city-enumerator identity and retain the common owner. Unsupported
// identifiers clear the output under the project's explicit QueryInterface failure contract.
bool UiNeighborWindowObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_UiNeighborWindow, UiNeighborWindow);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_CtCityEnumerator, CtCityEnumerator);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the owner count before retaining the module director, then return the live count after that callback.
u32 UiNeighborWindowObject::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Release the director only for a held owner reference, then destroy the owner when its live count reaches zero.
u32 UiNeighborWindowObject::Release() {
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

// Borrow the primary window from the retained generic-window identity without acquiring another reference.
WnWindow *UiNeighborWindowObject::GetAssociatedIGZWin() const {
  return window->GetWindow();
}

u32 UiNeighborWindowObject::GetNeighborCount() const {
  return neighbors.size;
}

u8 UiNeighborWindowObject::SetSelectedNeighborId(u32 neighbor) {
  selected = neighbor;
  return 1;
}

u32 UiNeighborWindowObject::GetSelectedNeighborId() {
  return selected;
}

// Replace or insert the icon for its unsigned key, releasing a replaced image before retaining the new one.
// The collection keeps source key order while image references remain real game-owned objects.
u8 UiNeighborWindowObject::SetDealIcon(u32 type, GxBuffer *icon) {
  u32 index = 0;
  while (index < icons.size && icons.data[index].id < type)
    ++index;
  if (index < icons.size && icons.data[index].id == type) {
    icons.data[index].icon->Release();
    index = 0;
    while (index < icons.size && icons.data[index].id < type)
      ++index;
    icons.data[index].icon = icon;
  } else {
    icons.Append();
    for (u32 i = icons.size - 1; i > index; --i)
      icons.data[i] = icons.data[i - 1];
    icons.data[index] = {type, icon};
  }
  icon->AddRef();
  return 1;
}

// Borrow the icon associated with the full unsigned key, or return null when the key is absent.
GxBuffer *UiNeighborWindowObject::GetDealIcon(u32 type) {
  for (u32 i = 0; i < icons.size && icons.data[i].id <= type; ++i)
    if (icons.data[i].id == type)
      return icons.data[i].icon;
  return nullptr;
}

// Copy a found neighbor's name into the supplied String and then publish its live population through the genuine output.
// The final byte argument is retained even though this installed body does not consume it.
u8 UiNeighborWindowObject::GetNeighbor(u32 neighbor, ScStringView *name, u32 *population, u8) {
  for (u32 i = 0; i < neighbors.size && neighbors.data[i].id <= neighbor; ++i) {
    if (neighbors.data[i].id == neighbor) {
      UiNeighborRecord *record = neighbors.data[i].record;
      if (!record)
        return 0;
      name->Copy(record->name);
      *population = record->population;
      return 1;
    }
  }
  return 0;
}

// New records are constructed before their ordered collection entry is published.
u8 UiNeighborWindowObject::SetNeighbor(u32 neighbor, const ScStringView &name, u32 population, u8 available) {
  u32 index = 0;
  while (index < neighbors.size && neighbors.data[index].id < neighbor)
    ++index;
  if (index < neighbors.size && neighbors.data[index].id == neighbor) {
    {
      ScString copy(name);
      index = 0;
      while (index < neighbors.size && neighbors.data[index].id < neighbor)
        ++index;
      neighbors.data[index].record->name = copy;
    }
    index = 0;
    while (index < neighbors.size && neighbors.data[index].id < neighbor)
      ++index;
    UiNeighborRecord *record = neighbors.data[index].record;
    record->population = population;
    record->available = available;
  } else {
    UiNeighborRecord *record = new UiNeighborRecord(this, neighbor, name, population, available);
    index = 0;
    while (index < neighbors.size && neighbors.data[index].id < neighbor)
      ++index;
    if (index == neighbors.size || neighbors.data[index].id != neighbor) {
      neighbors.Append();
      for (u32 i = neighbors.size - 1; i > index; --i)
        neighbors.data[i] = neighbors.data[i - 1];
      neighbors.data[index] = {neighbor, record};
    }
  }
  return 1;
}

u8 UiNeighborWindowObject::AddNeighborDeal(
    u32 neighbor, u8 type, ScStringView *description, ScStringView *rate, ScStringView *contribution) {
  return AddNeighborDeal(neighbor, type, 26, description, rate, contribution);
}

// Add a copied deal only for an existing neighbor with fewer than three deals and no matching type.
// Preserve the three String inputs and their construction order without retaining caller-owned views.
u8 UiNeighborWindowObject::AddNeighborDeal(
    u32 neighbor, u8 type, u32 category, ScStringView *description, ScStringView *rate, ScStringView *contribution) {
  for (u32 i = 0; i < neighbors.size && neighbors.data[i].id <= neighbor; ++i) {
    if (neighbors.data[i].id == neighbor) {
      UiNeighborRecord *record = neighbors.data[i].record;
      if (record->deals.size >= 3)
        return 0;
      for (u32 j = 0; j < record->deals.size; ++j)
        if (record->deals.data[j]->type == type)
          return 0;
      UiNeighborDeal *deal = new UiNeighborDeal(type, category, *description, *rate, *contribution);
      *record->deals.Append() = deal;
      return 1;
    }
  }
  return 0;
}

// Remove the first deal with the requested type from the matching neighbor while keeping the other deals in order.
u8 UiNeighborWindowObject::RemoveNeighborDeal(u32 neighbor, u8 type) {
  for (u32 i = 0; i < neighbors.size && neighbors.data[i].id <= neighbor; ++i) {
    if (neighbors.data[i].id == neighbor) {
      UiNeighborRecord *record = neighbors.data[i].record;
      for (u32 j = 0; j < record->deals.size; ++j) {
        if (record->deals.data[j]->type == type) {
          for (u32 k = j + 1; k < record->deals.size; ++k)
            *record->deals.data[k - 1] = *record->deals.data[k];
          --record->deals.size;
          delete record->deals.data[record->deals.size];
          return 1;
        }
      }
      return 0;
    }
  }
  return 0;
}

// Forward the mutable enumeration message to a present layer using the owner's primary query identity.
// A missing layer is ignored and the original callback always reports success.
bool UiNeighborWindowObject::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<UiNeighborWindow *>(this));
  return true;
}

// Send the opening or closing query to the retained city and then enumerate its layers through the separate identity.
// The explicit city argument only gates delivery; the original uses the live retained city for both calls.
u8 UiNeighborWindowObject::EnumCityItems(CtCity *target, u8 opening) {
  ScMessage message(opening ? kWnMsg_QueryOpened : kWnMsg_QueryClosed, kUiQueryTagNeighbors, 0, 0);
  if (target) {
    city->DoQueryInfo(&message, static_cast<UiNeighborWindow *>(this));
    city->DoEnumLayers(&message, static_cast<CtCityEnumerator *>(this));
  }
  return 1;
}

u8 UiNeighborWindowObject::OnInit(WnWindow *recipient) {
  DoNeighborSelect(recipient, selected);
  return 1;
}

// Send the closing city notification, then retire neighbor records and icons in unsigned key order.
// Release the remaining held identities, city, view and generic window in their original sequence.
u8 UiNeighborWindowObject::OnShutdown(WnWindow *) {
  EnumCityItems(city, 0);
  u32 index = 0;
  while (index < neighbors.size) {
    u32 key = neighbors.data[index].id;
    UiNeighborRecord *record = neighbors.data[index].record;
    if (record) {
      delete record;
      index = 0;
      while (index < neighbors.size && neighbors.data[index].id < key)
        ++index;
      neighbors.data[index].record = nullptr;
    }
    index = 0;
    while (index < neighbors.size && neighbors.data[index].id <= key)
      ++index;
  }
  neighbors.Clear();
  index = 0;
  while (index < icons.size) {
    u32 key = icons.data[index].id;
    GxBuffer *icon = icons.data[index].icon;
    if (icon) {
      icon->Release();
      index = 0;
      while (index < icons.size && icons.data[index].id < key)
        ++index;
      icons.data[index].icon = nullptr;
    }
    index = 0;
    while (index < icons.size && icons.data[index].id <= key)
      ++index;
  }
  icons.Clear();
  for (u32 i = 0; i < 6; ++i) {
    if (held[i]) {
      held[i]->Release();
      held[i] = nullptr;
    }
  }
  ScRelease(&city);
  ScRelease(&view);
  ScRelease(&window);
  return 1;
}

// Show the three border windows for the selected side and hide the other side indicators.
// Invalid side IDs leave the recipient unchanged; a valid selection invalidates its parent chain afterward.
u8 UiNeighborWindowObject::SelectNeighbor(WnWindow *recipient, u32 neighbor) {
  u32 first;
  switch (neighbor) {
  case 0:
    first = kUiFirstNeighborSelectionIdBase;
    break;
  case 1:
    first = kUiSecondNeighborSelectionIdBase;
    break;
  case 2:
    first = kUiThirdNeighborSelectionIdBase;
    break;
  case 3:
    first = kUiFourthNeighborSelectionIdBase;
    break;
  default:
    return 0;
  }
  for (u32 id = kUiFirstNeighborSelectionIdBase; id <= kUiNeighborSelectionLastId; ++id) {
    WnWindow *child = recipient->GetChildWindowFromID(id);
    if (child) {
      if (id == first || id == first + 1 || id == first + 2)
        child->ShowWindow();
      else
        child->HideWindow();
    }
  }
  recipient->InvalidateSelfAndParents();
  return 1;
}

// Create the solid outlined background and attach its twelve-point polygon to the neighbor window.
// The source retains the factory result when no outline is published and ignores child-attachment failure.
u8 UiNeighborWindowObject::SetupSolidBackground() {
  WnOutlineControl *outline = nullptr;
  bool created =
      UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline));
  if (created && outline) {
    outline->SetFlags(kWnOutlinePlain | kWnOutlinePolygon | kWnOutlineFill);
    outline->SetFillColor(UiGetWindowResources()->GetColor(kWnThemePanelFillColorId));
    outline->SetDarkOutlineColor(UiGetWindowResources()->GetColor(kWnThemeBorderColorId));
    outline->GetWindow()->SetArea(ScRect{0, 0, 360, 135});
    ScPoint points[] = {{0, 25}, {0, 135}, {352, 135}, {352, 0}, {222, 0}, {222, 50}, {342, 50}, {342, 95}, {10, 95},
        {10, 50}, {110, 50}, {110, 25}};
    outline->SetShapePoly(12, points);
    outline->GetWindow()->GZWinMoveTo(10, 300);
    outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
    window->GetWindow()->ChildAdd(outline->GetWindow());
    outline->Release();
    return 1;
  }
  return created;
}

// Create the neighbor-name editor and its localized caption using the original attachment and release order.
// The editor attachment controls whether the caption is attempted; the method itself returns one.
u8 UiNeighborWindowObject::SetupNameEditWindow() {
  WnTextControl *editor = nullptr;
  bool attached = UiGetCom()->GetClassObject(kWnTextEditClassId, IID_WnTextControl, reinterpret_cast<void **>(&editor));
  if (attached && editor) {
    editor->AsIGZWin()->SetID(kUiNeighborNameEditId);
    editor->SetCharacterLimit(25);
    editor->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
    u32 black = editor->AsIGZWin()->MakeFillColor(0, 0, 0);
    editor->SetOutlineColor(black);
    editor->SetEditable(1);
    editor->AsIGZWin()->SetArea(2, 2, 100, 19);
    editor->AsIGZWin()->GZWinMoveTo(18, 329);
    WnWindow *parent = window->GetWindow();
    attached = parent->ChildAdd(editor->AsIGZWin());
    editor->Release();
  }
  if (attached) {
    WnLabelControl *label = nullptr;
    if (UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(&label)) && label) {
      ScString caption;
      UiTextRef text(kRsGameCityText, kRsGameTextGroup);
      caption = text.GetText();
      label->SetCaption(caption);
      label->SetBackground(false);
      label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
      label->SetTextColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(121, 125, 181));
      label->SetAlignment(kWnLabelLeftMiddle);
      label->SetLabelFlag(kWnLabelWrapText, true);
      label->GetWindow()->SetArea(15, 305, 110, 325);
      WnWindow *parent = window->GetWindow();
      parent->ChildAdd(label->GetWindow());
      label->Release();
    }
  }
  return 1;
}

// Create the meet-neighbor button and its localized caption, returning failure when either cannot be attached.
// Retire the button before creating the label and preserve the factory's partial-output release on failure.
u8 UiNeighborWindowObject::SetupMeetNeighborWindow() {
  ScString caption(*window->GetWindow()->GetCaption());
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image_ref(0x26, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *image = image_ref.GetBuffer();
  WnButton *button = nullptr;
  bool attached = false;
  if (image) {
    WnResources *resources = UiGetWindowResources();
    const ScRect *area = image->GetBufferArea();
    button = resources->CreateFixedButton(kUiMeetNeighborButtonId, caption, image, area, 0);
    if (button) {
      button->SetButtonFlag(kWnButtonShowText, false);
      button->GetWindow()->GZWinMoveTo(322, 310);
      WnWindow *parent = window->GetWindow();
      attached = parent->ChildAdd(button->GetWindow());
    }
  }
  if (button)
    button->Release();
  if (!attached)
    return 0;
  WnLabelControl *label = nullptr;
  bool created = UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(&label));
  if (created && label) {
    ScString text;
    UiTextRef reference(kRsGameDiscussDealText, kRsGameTextGroup);
    text = reference.GetText();
    label->SetCaption(text);
    label->SetBackground(false);
    label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
    label->SetAlignment(kWnLabelLeftMiddle);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->GetWindow()->SetArea(237, 302, 325, 348);
    WnWindow *parent = window->GetWindow();
    parent->ChildAdd(label->GetWindow());
    label->Release();
    return 1;
  }
  if (!created && label)
    label->Release();
  return 0;
}

// Create the name and population labels at each of the four neighbors and the current city.
// Resource and font lookups occur separately for every label, including a fresh font height for its rectangle.
u8 UiNeighborWindowObject::SetupMapCityAndNeighborTextWindows() {
  const u32 name_ids[] = {kUiNeighborNameTopLeftId, kUiNeighborNameTopRightId, kUiNeighborNameBottomLeftId,
      kUiNeighborNameBottomRightId, kUiNeighborCurrentCityNameId};
  const u32 population_ids[] = {kUiNeighborPopulationTopLeftId, kUiNeighborPopulationTopRightId,
      kUiNeighborPopulationBottomLeftId, kUiNeighborPopulationBottomRightId, kUiNeighborCurrentCityPopulationId};
  for (u32 i = 0; i < 5; ++i) {
    i32 x = i < 2 ? neighbor_left : i < 4 ? neighbor_right : neighbor_middle;
    i32 y = i == 0 || i == 2 ? neighbor_top : i == 1 || i == 3 ? neighbor_bottom : neighbor_center;
    for (u32 j = 0; j < 2; ++j) {
      WnLabelControl *label = nullptr;
      if (UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(&label)) && label) {
        label->GetWindow()->SetID(j ? population_ids[i] : name_ids[i]);
        label->SetBackground(false);
        label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
        label->SetTextColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(255, 255, 255));
        label->SetAlignment(kWnLabelCenterMiddle);
        label->SetLabelFlag(kWnLabelWrapText, true);
        label->SetLabelFlag(kWnLabelForwardMessages, true);
        label->GetWindow()->SetFlag(kWnFlagReorderable, false);
        i32 height = UiGetWindowResources()->GetFont(kWnFontUiSmall)->Height(0);
        i32 top = (i32)((u32)x + (u32)height * (j ? 2u : 0u));
        i32 bottom = (i32)((u32)x + (u32)height * (j ? 3u : 2u));
        label->GetWindow()->SetArea(y, top, (i32)((u32)y + (u32)neighbor_text_width), bottom);
        WnWindow *parent = window->GetWindow();
        parent->ChildAdd(label->GetWindow());
        label->Release();
      }
    }
  }
  return 1;
}

// Load the three paired deal icons, capture their common dimensions, and create twelve bitmap children.
// A refused attachment destroys and releases that bitmap before returning failure.
u8 UiNeighborWindowObject::SetupDealIconsAndIconWindows() {
  const u32 instances[] = {39, 34, 36}, first_types[] = {0, 2, 5}, second_types[] = {1, 3, 4};
  for (u32 i = 0; i < 3; ++i) {
    GxColorType format(GxColorType::Color::RGB16);
    UiImageRef image(instances[i], kRsMapImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = image.GetBuffer();
    SetDealIcon(first_types[i], buffer);
    SetDealIcon(second_types[i], buffer);
    if (!i) {
      word84 = buffer->Width();
      word88 = buffer->Height();
    }
  }
  const u32 types[] = {0, 2, 5};
  for (u32 i = 0; i < 12; ++i) {
    WnBitmapControl *bitmap = nullptr;
    if (!UiGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap)))
      continue;
    bitmap->GetWindow()->SetID(kUiNeighborDirectionBitmapIdBase + i);
    bitmap->GetWindow()->SetArea(0, 0, (i32)word84, (i32)word88);
    bitmap->SetBitmapFlag(1, true);
    bitmap->SetImage(GetDealIcon(types[i % 3]));
    WnWindow *parent = window->GetWindow();
    if (parent->ChildAdd(bitmap->GetWindow())) {
      bitmap->Release();
    } else {
      WnWindowManager *manager = UiGetWindowManager();
      manager->DestroyWindow(bitmap->GetWindow());
      bitmap->Release();
      return 0;
    }
  }
  return 1;
}

// Refresh deal icons for each neighbor until a refresh reports failure.
u8 UiNeighborWindowObject::UpdateAllDealIcons() {
  u8 result = 1;
  for (u32 i = 0; i < 4; ++i) {
    result = UpdateDealIcons(i);
    if (!result)
      break;
  }
  return result;
}

// Hide the three old icons for the rotated neighbor, then position and show icons for its current deals.
// The initial horizontal position uses the source's wrapped unsigned product and logical half-width shift.
u8 UiNeighborWindowObject::UpdateDealIcons(u32 neighbor) {
  UiNeighborRecord *record = nullptr;
  for (u32 i = 0; i < neighbors.size; ++i) {
    if (neighbors.data[i].id == neighbor) {
      record = neighbors.data[i].record;
      break;
    }
  }
  if (!record)
    return 1;
  u32 count = record->deals.size;
  UiNeighborDeal **current = record->deals.data;
  i32 zoom = 0, rotation = 0;
  view->GetZoomAndRotation(&zoom, &rotation);
  u32 position = ((u8)record->id + (u8)rotation) & 3u;
  u32 first_id = kUiNeighborDirectionBitmapIdBase + position * 3;
  i32 center_x = position == 0 || position == 3 ? neighbor_icon_right_x : neighbor_icon_left_x;
  i32 y = position == 0 ? neighbor_icon_top_left_y
      : position == 1   ? neighbor_icon_top_right_y
      : position == 2   ? neighbor_icon_bottom_left_y
                        : neighbor_icon_bottom_right_y;
  for (u32 i = 0; i < 3; ++i) {
    WnWindow *child = window->GetWindow()->GetChildWindowFromID(first_id + i);
    if (child)
      child->HideWindow();
  }
  u32 left = (u32)center_x - ((count * word84) >> 1), index = 0;
  while (current != (record->deals.size ? record->deals.data + record->deals.size : record->deals.data)) {
    u8 type = (*current)->type;
    u32 slot = type <= 1 || type > 5 ? 0 : type <= 3 ? 1 : 2;
    WnWindow *child = window->GetWindow()->GetChildWindowFromID(first_id + slot);
    if (child) {
      child->GZWinMoveTo((i32)(left + index * word84), y);
      child->ShowWindow();
    }
    ++current;
    ++index;
  }
  return 1;
}

// Create the two blue outline layers surrounding the selected neighbor position.
// Failed factory requests skip only their own layer; refused attachments destroy the new window before release.
u8 UiNeighborWindowObject::CreateNeighborBorder(u32 neighbor) {
  if (neighbor > 3)
    return 0;
  const i32 left[] = {86, 184, 184, 86}, top[] = {96, 96, 194, 194};
  const u32 identifiers[] = {kUiNeighborBorderTopLeftIdBase, kUiNeighborBorderTopRightIdBase,
      kUiNeighborBorderBottomRightIdBase, kUiNeighborBorderBottomLeftIdBase};
  for (i32 layer = 0; layer < 2; ++layer) {
    WnOutlineControl *outline = nullptr;
    if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
      outline->SetFlags(kWnOutlinePlain | kWnOutlinePolygon);
      outline->GetWindow()->SetArea(left[neighbor], top[neighbor], left[neighbor] + 94, top[neighbor] + 94);
      outline->GetWindow()->SetID(identifiers[neighbor] + 1 - (u32)layer);
      outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
      outline->GetWindow()->SetFlag(kWnFlagEnabled, false);
      outline->SetDarkOutlineColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(104, 146, 247));
      i32 first = 45 + layer, second = 48 - layer;
      ScPoint points[] = {
          {0, first}, {0, second}, {first, 93}, {second, 93}, {93, second}, {93, first}, {second, 0}, {first, 0}};
      outline->SetShapePoly(8, points);
      WnWindow *parent = window->GetWindow();
      if (!parent->ChildAdd(outline->GetWindow())) {
        WnWindowManager *manager = UiGetWindowManager();
        manager->DestroyWindow(outline->GetWindow());
      }
      outline->Release();
    }
  }
  return 1;
}

// Create three initially hidden red outline layers for the selected neighbor and current map rotation.
// A failed child attachment retires that window while the remaining selection layers are still attempted.
u8 UiNeighborWindowObject::CreateNeighborSelection(u32 neighbor) {
  i32 zoom = 0, rotation = 0;
  if (view)
    view->GetZoomAndRotation(&zoom, &rotation);
  const u32 identifiers[] = {kUiFirstNeighborSelectionIdBase, kUiFourthNeighborSelectionIdBase,
      kUiThirdNeighborSelectionIdBase, kUiSecondNeighborSelectionIdBase, kUiFourthNeighborSelectionIdBase,
      kUiThirdNeighborSelectionIdBase, kUiSecondNeighborSelectionIdBase, kUiFirstNeighborSelectionIdBase,
      kUiThirdNeighborSelectionIdBase, kUiSecondNeighborSelectionIdBase, kUiFirstNeighborSelectionIdBase,
      kUiFourthNeighborSelectionIdBase, kUiSecondNeighborSelectionIdBase, kUiFirstNeighborSelectionIdBase,
      kUiFourthNeighborSelectionIdBase, kUiThirdNeighborSelectionIdBase};
  u32 index = (u32)rotation * 4 + neighbor;
  assert(index < 16);
  u32 identifier = identifiers[index];
  if (neighbor > 3)
    return 0;
  i32 left = neighbor == 0 || neighbor == 3 ? 87 : 185;
  i32 top = neighbor < 2 ? 97 : 195;
  i32 right = left + 92, offset = top - left;
  for (i32 size = 92; size > 86; size -= 2, ++left, --right, ++identifier) {
    WnOutlineControl *outline = nullptr;
    if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
      outline->SetFlags(kWnOutlinePlain | kWnOutlinePolygon);
      outline->GetWindow()->SetArea(left, left + offset, right, right + offset);
      outline->GetWindow()->SetID(identifier);
      outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
      outline->GetWindow()->SetFlag(kWnFlagEnabled, false);
      outline->SetDarkOutlineColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(238, 56, 0));
      i32 half = size / 2;
      ScPoint points[] = {{0, half - 1}, {0, half}, {half - 1, size - 1}, {half, size - 1}, {size - 1, half},
          {size - 1, half - 1}, {half, 0}, {half - 1, 0}};
      outline->SetShapePoly(8, points);
      outline->GetWindow()->HideWindow();
      WnWindow *parent = window->GetWindow();
      if (!parent->ChildAdd(outline->GetWindow())) {
        WnWindowManager *manager = UiGetWindowManager();
        manager->DestroyWindow(outline->GetWindow());
      }
      outline->Release();
    }
  }
  return 1;
}

// Build map outlines, configure the terrain image and attach the map control.
// Retire the renderer and buffer in source order before building the neighbor borders and selections.
u8 UiNeighborWindowObject::SetupMapControlWindow() {
  ScRect rectangle{30, 39, 336, 345};
  for (u32 i = 0; i < 2; ++i) {
    WnOutlineControl *outline = nullptr;
    if (UiGetCom()->GetClassObject(kWnOutlineClassId, IID_WnOutlineControl, reinterpret_cast<void **>(&outline))) {
      outline->SetFlags(kWnOutlineInset | kWnOutlinePolygon);
      outline->GetWindow()->SetArea(rectangle);
      outline->GetWindow()->SetID(kUiNeighborTitleSegmentIdBase + i);
      outline->GetWindow()->SetFlag(kWnFlagReorderable, false);
      i32 width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
      ScPoint points[] = {{0, height / 2 - 1}, {0, height / 2}, {width / 2 - 1, height - 1}, {width / 2, height - 1},
          {width - 1, height / 2}, {width - 1, height / 2 - 1}, {width / 2, 0}, {width / 2 - 1, 0}};
      outline->SetShapePoly(8, points);
      WnWindow *parent = window->GetWindow();
      if (!parent->ChildAdd(outline->GetWindow())) {
        WnWindowManager *manager = UiGetWindowManager();
        manager->DestroyWindow(outline->GetWindow());
      }
      outline->Release();
    }
    ++rectangle.left;
    --rectangle.right;
    ++rectangle.top;
    --rectangle.bottom;
  }
  i32 zoom = 0, rotation = 0;
  view->GetZoomAndRotation(&zoom, &rotation);
  i32 degrees = rotation == 0 ? 315 : rotation == 1 ? 45 : rotation == 2 ? 135 : rotation == 3 ? 225 : 0;
  TrNeighborMapInterface *renderer = nullptr;
  u8 result =
      UiGetCom()->GetClassObject(kTrNeighborMapClassId, IID_TrNeighborMap, reinterpret_cast<void **>(&renderer));
  if (result && renderer)
    result = renderer->Init(city->DirtBag());
  GxBuffer *buffer = nullptr;
  if (result) {
    result = UiGetGraphics()->CreateBuffer(&buffer);
    if (result) {
      GxColorType color(GxColorType::Color::RGB16);
      GxDepthType depth(GxDepthType::Depth::None);
      result = buffer->Init(256, 256, color, nullptr, depth, GxVramRequest::SystemMemory, false);
      if (result) {
        assert(renderer);
        result = renderer->DrawMap(buffer);
        if (result) {
          result =
              UiGetCom()->GetClassObject(kUiMapControlClassId, IID_UiMapControl, reinterpret_cast<void **>(&held[0]));
          if (result) {
            if (held[0]) {
              result = static_cast<UiMapControl *>(held[0])->CreateControl(kUiNeighborMapControlId);
              static_cast<UiMapControl *>(held[0])->SetDrawWindowOutline(false);
              static_cast<UiMapControl *>(held[0])->SetDrawImageOutline(true);
              static_cast<UiMapControl *>(held[0])->AsIGZWin()->SetArea(ScRect{31, 40, 333, 342});
              static_cast<UiMapControl *>(held[0])->AsIGZWin()->SetFlag(kWnFlagReorderable, false);
              static_cast<UiMapControl *>(held[0])->SetRotation(degrees);
              static_cast<UiMapControl *>(held[0])->SetActualMapSizeInCells(3, 3);
              static_cast<UiMapControl *>(held[0])->SetCoordinateForwarding(1, true, nullptr);
            }
            if (result) {
              UiMapControl *map = static_cast<UiMapControl *>(held[0]);
              result = map->AddFilter(10003, buffer);
              if (result)
                result = window->GetWindow()->ChildAdd(map->AsIGZWin());
            }
          }
        }
      }
    }
  }
  if (renderer) {
    renderer->Shutdown();
    renderer->Release();
  }
  if (buffer)
    buffer->Release();
  if (!result && held[0]) {
    held[0]->Release();
    held[0] = nullptr;
  }
  CreateNeighborBorder(0);
  CreateNeighborBorder(1);
  CreateNeighborBorder(3);
  CreateNeighborBorder(2);
  CreateNeighborSelection(0);
  CreateNeighborSelection(1);
  CreateNeighborSelection(3);
  CreateNeighborSelection(2);
  return result;
}

u32 UiNeighborWindowObject::GetNationalPopulation() {
  CtWorldLayer *world = UiGetCity()->WorldLayer();
  return world ? world->GetWorldPopulation() : 0;
}

// Create the localized national-population label and attach it to the neighbor window when available.
// A missing label does not turn this setup step into a failure.
u8 UiNeighborWindowObject::SetupNationalPopulationWindow() {
  ScString caption;
  UiTextRef reference(kRsGameNatLPopText, kRsGameTextGroup);
  caption = reference.GetText();
  ScLanguageUtility *language = UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  ScString population;
  language->MakeNumberString((i64)GetNationalPopulation(), &population);
  caption.Append(" ");
  caption.Append(population.Data(), population.Strlen());
  WnLabelControl *label = nullptr;
  if (UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(&label)) && label) {
    label->SetCaption(caption);
    label->SetBackground(false);
    label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
    label->SetTextColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(121, 125, 181));
    label->SetAlignment(kWnLabelLeftTop);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->GetWindow()->SetArea(240, 50, 360, 110);
    WnWindow *parent = window->GetWindow();
    parent->ChildAdd(label->GetWindow());
    label->Release();
  }
  return 1;
}

// Refresh each stored neighbor name and population, then update the current city's label.
// Traverse the live key order again after callbacks so insertions retain the original ordered-map behavior.
u8 UiNeighborWindowObject::SetupNamesAndPops() {
  u32 index = 0;
  while (index < neighbors.size) {
    u32 key = neighbors.data[index].id;
    UiNeighborRecord *record = neighbors.data[index].record;
    if (record)
      UpdateCityOrNeighborText(record->id, &record->name, (i32)record->population);
    index = 0;
    while (index < neighbors.size && neighbors.data[index].id <= key)
      ++index;
  }
  i32 population = 0;
  ScString name;
  if (city) {
    CtResidentialLayer *residential = city->ResidentialLayer();
    if (residential)
      population = residential->GetPopulation();
    ScString current(*city->GetCityName());
    name = current;
  }
  UpdateCityOrNeighborText(4, &name, population);
  return 1;
}

// Create the three deal headings in their source order, then rebuild each neighbor's deal list.
// A failed earlier heading attachment suppresses subsequent headings but not the later neighbor-list pass.
u8 UiNeighborWindowObject::SetupDealWindows() {
  ScString caption;
  bool proceed = true;
  const u32 titles[] = {0x13d, 0x29b, 0x13e};
  const ScRect areas[] = {{30, 344, 150, 385}, {150, 344, 250, 385}, {250, 344, 341, 385}};
  for (u32 i = 0; i < 3 && proceed; ++i) {
    WnLabelControl *label = nullptr;
    bool created = UiGetCom()->GetClassObject(kWnLabelClassId, IID_WnLabelControl, reinterpret_cast<void **>(&label));
    if (created && label) {
      UiTextRef reference(titles[i], kRsGameTextGroup);
      caption = reference.GetText();
      label->SetCaption(caption);
      label->SetBackground(false);
      label->SetFont(UiGetWindowResources()->GetFont(kWnFontUiSmall));
      label->SetTextColor(UiGetGraphics()->ConvertRGBValueToNative16Bit(121, 125, 181));
      label->SetAlignment(kWnLabelCenterMiddle);
      label->SetLabelFlag(kWnLabelWrapText, true);
      label->GetWindow()->SetArea(areas[i].left, areas[i].top, areas[i].right, areas[i].bottom);
      WnWindow *parent = window->GetWindow();
      bool attached = parent->ChildAdd(label->GetWindow());
      label->Release();
      if (i < 2)
        proceed = attached;
    } else if (i < 2)
      proceed = created;
  }
  u32 index = 0;
  while (index < neighbors.size) {
    u32 key = neighbors.data[index].id;
    neighbors.data[index].record->CreateDealWindow();
    index = 0;
    while (index < neighbors.size && neighbors.data[index].id <= key)
      ++index;
  }
  return 1;
}

// Find the selected deal for the current neighbor and request its petitioner before closing the dialog.
// A selected deal returns one even when no matching petitioner is available.
u8 UiNeighborWindowObject::DoMeetNeighborSelect(WnWindow *recipient) {
  u32 key = ((u8)selected + 1u) & 3u;
  UiNeighborRecord *neighbor = nullptr;
  for (u32 i = 0; i < neighbors.size; ++i)
    if (neighbors.data[i].id == key) {
      neighbor = neighbors.data[i].record;
      break;
    }
  if (!neighbor || !neighbor->deal_window)
    return 0;
  u32 selected_deal = (u32)neighbor->deal_window->GetSelection();
  if (selected_deal >= neighbor->deals.size)
    return 0;
  u8 type = neighbor->deals.data[selected_deal]->type;
  AdPetitionerRecord record{};
  CtCity *current = UiGetCity();
  if (current->GetPetitionerManager()->GetNeighborDealPetitioner(type, &record)) {
    ScMessage message(kWnMsg_ModalPetitioner, kUiNeighborDealPetitionerRequestId, type, 0);
    UiGetMessageServer()->MessageSend(message);
    UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, (uptr)recipient, 1);
  }
  return 1;
}

// Update the rotated neighbor or city name and population labels, truncating long names with localized text.
// Missing label interfaces are skipped independently; population formatting still occurs after the name lookup.
u8 UiNeighborWindowObject::UpdateCityOrNeighborText(u32 neighbor, ScStringView *name, i32 population) {
  u32 position = neighbor, name_id = 0, population_id = 0;
  if (neighbor < 4) {
    i32 zoom = 0, rotation = 0;
    view->GetZoomAndRotation(&zoom, &rotation);
    position = ((u8)neighbor + (u8)rotation) & 3;
  }
  const u32 name_ids[] = {kUiNeighborNameTopRightId, kUiNeighborNameTopLeftId, kUiNeighborNameBottomLeftId,
      kUiNeighborNameBottomRightId, kUiNeighborCurrentCityNameId};
  const u32 population_ids[] = {kUiNeighborPopulationTopRightId, kUiNeighborPopulationTopLeftId,
      kUiNeighborPopulationBottomLeftId, kUiNeighborPopulationBottomRightId, kUiNeighborCurrentCityPopulationId};
  if (position < 5) {
    name_id = name_ids[position];
    population_id = population_ids[position];
  }
  WnLabelControl *label = nullptr;
  if (window->GetWindow()->GetChildAs(name_id, IID_WnLabelControl, reinterpret_cast<void **>(&label)) && label) {
    ScString display(*name);
    if (name->Strlen() > 30) {
      UiTextRef suffix(0x646, kRsCommonTextGroup);
      const ScStringView *resource = suffix.GetResource();
      assert(resource);
      ScString captured(*resource);
      display = display.Left(30);
      display.Trim();
      ScString text = suffix.GetText();
      display.Append(text.Data(), text.Strlen());
    }
    label->SetCaption(display);
    label->GetWindow()->PullToFront();
    label->Release();
    label = nullptr;
  }
  ScString formatted;
  UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent)->MakeNumberString((i64)population, &formatted);
  if (window->GetWindow()->GetChildAs(population_id, IID_WnLabelControl, reinterpret_cast<void **>(&label)) && label) {
    label->SetCaption(formatted);
    label->GetWindow()->PullToFront();
    label->Release();
  }
  return 1;
}

// Route neighbor selection, editing, lifecycle and close events through the actual window callback.
bool UiNeighborWindowObject::DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr) {
  window->GetWindow();
  if (message == kWnMsg_MouseDownL || message == kWnMsg_MapMouseDownL)
    return DoMapControlEvent(recipient, message, first, second) != 0;
  if (message == kWnMsg_Init)
    return OnInit(recipient) != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown(recipient) != 0;
  if (message == kWnMsg_KeyDown && first == 27) {
    UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, (uptr)recipient, 0);
    return true;
  }
  if (message != kWnMsg_Command)
    return false;
  if (first == kWnCommandButtonActivated) {
    if (second == 6) {
      UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
      return false;
    }
    if (second == kUiFrameCloseButtonId) {
      UiGetApplication()->GetBoxX()->Event(31, 0, 0, 0, 0);
      UiGetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, (uptr)recipient, 0);
      return true;
    }
    if (second == kUiMeetNeighborButtonId) {
      UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
      DoMeetNeighborSelect(recipient);
      return true;
    }
    return false;
  }
  if (first != 8 || second != kUiNeighborNameEditId)
    return false;
  WnTextControl *text = nullptr;
  if (!recipient->GetChildAs(kUiNeighborNameEditId, IID_WnTextControl, reinterpret_cast<void **>(&text)) || !text)
    return false;
  u32 neighbor = ((u8)selected + 1u) & 3u, population = 0;
  ScString name;
  u8 result = GetNeighbor(neighbor, &name, &population, 1);
  if (result) {
    text->GetText(&name);
    result = SetNeighbor(neighbor, name, population, 1);
    CtCity *current = UiGetCity();
    if (result) {
      result = current != nullptr;
      if (current) {
        CtNeighborsLayer *layer = current->NeighborsLayer();
        result = layer != nullptr;
        if (layer) {
          layer->SetNeighborName((u8)neighbor, name);
          result = UpdateCityOrNeighborText(neighbor, &name, (i32)population);
        }
      }
    }
  }
  text->Release();
  return result != 0;
}

// Forward pointer movement into the map control and select the corresponding neighbor cell when requested.
u8 UiNeighborWindowObject::DoMapControlEvent(WnWindow *recipient, u32 message, uptr first, uptr second) {
  u32 neighbor = GetSelectedNeighborId();
  if (message == kWnMsg_MouseDownL) {
    i32 x = (i32)first, y = (i32)second;
    UiMapControl *map = static_cast<UiMapControl *>(held[0]);
    recipient->WindowToWindowCoordinates(map->AsIGZWin(), &x, &y);
    recipient->GetWindowManager()->SendMessageToTarget(map->AsIGZWin(), kWnMsg_MouseDownL, (uptr)x, (uptr)y, 0);
  } else if (message == kWnMsg_MapMouseDownL)
    neighbor = (u32)TranslateMapControlCoord((i16)first, (i16)(first >> 16));
  else
    return 0;
  return DoNeighborSelect(recipient, neighbor);
}

// Map the center and four neighboring map cells to their selection IDs. Other coordinates have no selection.
int UiNeighborWindowObject::TranslateMapControlCoord(i16 x, i16 y) {
  if (y == 1) {
    if (x == 0)
      return 0;
    if (x == 1)
      return 4;
    if (x == 2)
      return 2;
  } else if (x == 1) {
    if (y == 0)
      return 1;
    if (y == 2)
      return 3;
  }
  return 10;
}

// Select the next mapped neighbor, switching its retained deal window and updating the recipient's controls.
// Two source IDs are accepted without effects; absent neighbor records leave the prior selection unchanged.
u8 UiNeighborWindowObject::DoNeighborSelect(WnWindow *recipient, u32 neighbor) {
  if (neighbor == 10 || neighbor == 4)
    return 1;
  UiGetApplication()->GetBoxX()->Event(kAuEventPlayEffect, kAuEffectUiCheckboxClick, 0, 0, 0);
  u32 key = ((u8)neighbor + 1u) & 3u;
  UiNeighborRecord *next = nullptr, *previous = nullptr;
  for (u32 i = 0; i < neighbors.size; ++i) {
    if (neighbors.data[i].id == key)
      next = neighbors.data[i].record;
    if (neighbors.data[i].id == selected)
      previous = neighbors.data[i].record;
  }
  if (!next)
    return 0;
  if (previous)
    previous->deal_window->GetWindow()->HideWindow();
  next->deal_window->GetWindow()->PullToFront();
  next->deal_window->GetWindow()->ShowWindow();
  selected = neighbor;
  SelectNeighbor(recipient, neighbor);
  UpdateNameEditControl(recipient, next);
  WnWindow *deal_button = recipient->GetChildWindowFromID(kUiMeetNeighborButtonId);
  deal_button->SetFlag(kWnFlagEnabled, next->deals.size != 0);
  SetSelectedNeighborId(neighbor);
  return 1;
}

// Find the recipient's name editor recursively and copy the selected neighbor's name through its text interface.
// A missing child returns zero; an unpublished queried pointer is outside the source's defined execution.
u8 UiNeighborWindowObject::UpdateNameEditControl(WnWindow *recipient, UiNeighborRecord *record) {
  window->GetWindow();
  WnWindow *child = recipient->GetChildWindowFromIDRecursive(kUiNeighborNameEditId);
  if (!child)
    return 0;
  WnTextControl *text = nullptr;
  bool found = child->QueryInterface(IID_WnTextControl, reinterpret_cast<void **>(&text));
  assert(text);
  if (found)
    text->SetText(&record->name);
  text->Release();
  return found;
}
