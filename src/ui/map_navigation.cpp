#include "ui/map_navigation.h"

#include <assert.h>

#include "application/api.h"
#include "application/application_core.h"
#include "audio/api.h"
#include "city/api.h"
#include "city/ct_date.h"
#include "city/ct_layer.h"
#include "city/simulation.h"
#include "common/api.h"
#include "common/framework.h"

#include "common/ids.h"

#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "fonts/sc3_bitmap_font.h"
#include "graphics/api.h"
#include "graphics/image.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/resource.h"
#include "sprites/api.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"
#include "sprites/view_camera.h"
#include "terrain/api.h"
#include "ui/api.h"
#include "ui/chart_views.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "ui/ui_map_view.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/window.h"
#include "window/wn_resource_provider.h"

// Navigation atlas and button images retain their installed main-UI resource instances.
enum UiNavigationImageId : u32 {
  kUiNavigationZoomInImageId = 0x22f96e2d,
  kUiNavigationZoomOutImageId = 0x22f96e2e,
  kUiNavigationRotateClockwiseImageId = 0x22f96e2f,
  kUiNavigationRotateCounterclockwiseImageId = 0x22f96e30,
  kUiNavigationFirstAtlasImageId = 0x22f96e32,
  kUiNavigationSecondAtlasImageId = 0x22f96e33,
  kUiNavigationClockAtlasImageId = 0x22f96e34,
  kUiNavigationLayersImageId = 0x22f96e35,
};

// Navigation controls route zoom, rotation and layer selection to the current city view.
enum UiNavigationControlId : u32 {
  kUiNavigationZoomInButtonId = 0x22f96e1f,
  kUiNavigationZoomOutButtonId = 0x22f96e20,
  kUiNavigationRotateCounterclockwiseButtonId = 0x22f96e21,
  kUiNavigationRotateClockwiseButtonId = 0x22f96e22,
  kUiNavigationLayersButtonId = 0x22f96e24,
  kUiNavigationMarkerId = 0x22f96e26,
  kUiNavigationSidebarId = 0x2323e7b9,
};

UiMapsView::UiMapsView()
    : references(0), owner(nullptr), window(nullptr), enabled(1), standalone(0), icon(nullptr), icon_location{0, 0},
      view_id(0) {
}

UiMapsView::~UiMapsView() {
}

bool UiMapsView::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiMapsView);
  HANDLE_QUERY_INTERFACE(IID_UiMapsViewInterface, UiMapsView);
  HANDLE_QUERY_INTERFACE(IID_CtCityEnumerator, CtCityEnumerator);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increase the view count before retaining the SIMUI director, then return the live count.
u32 UiMapsView::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Release the director before decrementing the count and delete the view when the count reaches zero.
u32 UiMapsView::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

// Create the generic view window after retaining its owner, then apply the original styles and view properties.
// Failure after retaining the owner leaves it for the normal shutdown callback to release.
u8 UiMapsView::CreateView(UiMapsWindow *new_owner, ScRect *area, ScPoint *location) {
  if (IsReady())
    return 0;
  owner = ScAddRefNotNull(new_owner);
  if (!icon)
    return 0;
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result) {
    window->SetStyle(kUiGenericDraggable, 0);
    window->SetStyle(kUiGenericResizable, 0);
    window->SetStyle(kUiGenericBackButton, 0);
    window->SetStyle(kUiGenericSecondButton, 0);
    window->SetStyle(kUiGenericSecondButton, 1);
    window->SetStyle(kUiGenericSecondButton, 1);
    window->SetStyle(kUiGenericSecondButtonState, 1);
    window->SetStyle(kUiGenericNoFrame, 1);
    window->SetStyle(kUiGenericNoBevel, 1);
    window->SetStyle(kUiGenericNoBackground, 1);
    window->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
    result = window->SetCallback(static_cast<UiWindowCallback *>(this));
  }
  if (!IsIconButtonReady())
    result = 0;
  return result && SetViewArea(area) && SetViewId(view_id) && SetName(name) && SetIconButtonLocation(location);
}

// Return the borrowed maps-and-graphs window associated with this view.
UiMapsWindow *UiMapsView::GetMapsAndGraphsWindow() {
  return owner;
}

// Return the generic window's borrowed window interface when it has been created.
WnWindow *UiMapsView::GetAssociatedIGZWin() const {
  return window ? window->GetWindow() : nullptr;
}

u8 UiMapsView::EnumCityItems(CtCity *, bool) {
  return 1;
}

u8 UiMapsView::RefreshData(CtCity *) {
  return 1;
}

// Return the address of the view's stored identifier.
u32 *UiMapsView::GetViewId() {
  return &view_id;
}

// Store the identifier and propagate it to the associated window if one exists.
u8 UiMapsView::SetViewId(const u32 &identifier) {
  view_id = identifier;
  if (window)
    window->GetWindow()->SetID(view_id);
  return 1;
}

u8 UiMapsView::GetViewArea(ScRect *out) {
  *out = view_area;
  return 1;
}

// Update the rectangle and associated window only after a generic window has been installed.
u8 UiMapsView::SetViewArea(ScRect *area) {
  if (window) {
    view_area = *area;
    GetAssociatedIGZWin()->SetArea(view_area);
  }
  return 1;
}

// Return the borrowed embedded name interface.
const ScStringView *UiMapsView::GetName() const {
  return &name;
}

u8 UiMapsView::SetName(const ScStringView &text) {
  {
    ScString copy(text);
    name = copy;
  }
  if (IsIconButtonReady())
    GetIconButton()->SetTipText(name);
  return 1;
}

// Return the borrowed icon button only when the readiness operation succeeds.
WnButton *UiMapsView::GetIconButton() {
  return IsIconButtonReady() ? icon : nullptr;
}

// Replace the icon with a fixed toggle button using the image's full buffer area and the saved name.
// Configure the new button's flags and tooltip after its window has been obtained.
u8 UiMapsView::SetIconButton(GxBuffer *buffer) {
  ScRelease(&icon);
  WnResources *resources = UiGetWindowResources();
  const ScRect *area = buffer->GetBufferArea();
  icon = resources->CreateFixedToggleButton(view_id, name, buffer, area, 0);
  if (!icon)
    return 0;
  icon->GetWindow();
  icon->SetButtonFlag(kWnButtonShowText, false);
  icon->SetButtonFlag(kWnButtonShowHint, true);
  icon->SetTipText(name);
  return 1;
}

// Resolve the requested RGB16 image and pass its buffer to the virtual icon setter when available.
u8 UiMapsView::SetIconButton(u32 group, u32 instance) {
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(instance, group, GxVramRequest::SystemMemory, format);
  GxBuffer *buffer = image.GetBuffer();
  return buffer ? SetIconButton(buffer) : 0;
}

i32 UiMapsView::IsIconButtonReady() {
  return icon != nullptr;
}

u8 UiMapsView::GetIconButtonLocation(ScPoint *out) {
  *out = icon_location;
  return 1;
}

u8 UiMapsView::SetIconButtonLocation(ScPoint *location) {
  icon_location = *location;
  if (icon)
    icon->GetWindow()->GZWinMoveTo(icon_location.x, icon_location.y);
  return 1;
}

u8 UiMapsView::IconButtonToggleOn() {
  return icon ? icon->ToggleOn() : 0;
}

u8 UiMapsView::IconButtonToggleOff() {
  return icon ? icon->ToggleOff() : 0;
}

u8 UiMapsView::IsTearable() {
  return 0;
}

u8 UiMapsView::IsStandAlone() {
  return standalone;
}

u8 UiMapsView::PerformTearOff(UiMapsViewInterface **) {
  return 0;
}

i32 UiMapsView::IsReady() {
  return window != nullptr;
}

u8 UiMapsView::SetNameResource(u32 group, u32 instance) {
  UiTextRef resource(instance, group);
  ScString resolved(resource.GetText());
  ScString copy(resolved);
  SetName(copy);
  return 1;
}

u8 UiMapsView::OnInit(WnWindow *) {
  return 1;
}

u8 UiMapsView::OnShutdown(WnWindow *) {
  ScRelease(&icon);
  ScRelease(&owner);
  ScRelease(&window);
  return 1;
}

u8 UiMapsView::EndDialog(WnWindow *, u32) {
  return 0;
}

// Forward the generic-window initialization and shutdown messages through the primary view's virtual hooks.
// Other window messages are not handled by the base view.
bool UiMapsView::DoWinMsg(WnWindow *receiver, u32 message, uptr, uptr, uptr) {
  if (message == kWnMsg_Init)
    return OnInit(receiver) != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown(receiver) != 0;
  return false;
}

bool UiMapsView::Enumerate(ScMessage *, CtLayer *) {
  return true;
}

bool UiMapsView::DoMessage(ScMessage *) {
  return true;
}

// Append one trivial owned entry, retaining the original three-pointer vector layout in the receiver.
// Resource references are acquired by the caller only after this operation has published the new entry.
template <class T> static void UiAppendEntry(T **first, T **last, T **limit, T value) {
  if (*last == *limit) {
    usize count = *first ? (usize)(*last - *first) : 0;
    usize capacity = count ? count * 2 : 1;
    T *replacement = new T[capacity];
    for (usize i = 0; i < count; ++i)
      replacement[i] = (*first)[i];
    delete[] *first;
    *first = replacement;
    *last = replacement + count;
    *limit = replacement + capacity;
  }
  **last = value;
  ++*last;
}

UiMapsWindowObject::UiMapsWindowObject()
    : references(0), window(nullptr), sidebar(nullptr), frame{40, 34, 464, 474}, unknown116(1), views(nullptr),
      views_end(nullptr), views_capacity(nullptr), current(nullptr), palettes(nullptr), palettes_end(nullptr),
      palettes_capacity(nullptr), rasters(nullptr), rasters_end(nullptr), rasters_capacity(nullptr) {
  u32 language = UiGetLanguageManager()->GetCurrentLanguage();
  if (language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
      language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai)
    frame.bottom += 20;
}

UiMapsWindowObject::~UiMapsWindowObject() {
  ReleaseAllPalettes();
  ReleaseAllRasters();
  delete[] rasters;
  delete[] palettes;
  delete[] views;
}

bool UiMapsWindowObject::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiRasterQuery);
  HANDLE_QUERY_INTERFACE(IID_UiRasterQuery, UiRasterQuery);
  HANDLE_QUERY_INTERFACE(IID_CtCityEnumerator, CtCityEnumerator);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the object count before retaining the SIMUI module director.
u32 UiMapsWindowObject::AddRef() {
  ++references;
  UiGetDirector()->AddRef();
  return references;
}

// Remove the module reference before decrementing, then destroy the object when the live count reaches zero.
u32 UiMapsWindowObject::Release() {
  if (references) {
    UiGetDirector()->RemoveRef();
    --references;
  }
  u32 result = references;
  if (!result)
    delete this;
  return result;
}

bool UiMapsWindowObject::DoesMapViewExist() {
  return UiGetWindowManager()->GetMainWindow()->GetChildWindowFromIDRecursive(kUiMapsWindowId) != nullptr;
}

u32 UiMapsWindowObject::GetCurrentViewId() {
  if (IsReady() && current)
    return *current->GetViewId();
  return default_view;
}

u32 UiMapsWindowObject::GetDefaultViewId() {
  return default_view;
}

// Borrow the generic window's associated window interface when the generic object exists.
WnWindow *UiMapsWindowObject::GetWindow() const {
  return window ? window->GetWindow() : nullptr;
}

// Copy the caption, then pass the original input to an existing associated window.
u8 UiMapsWindowObject::SetCaption(const ScStringView &text) {
  {
    ScString copy(text);
    caption = copy;
  }
  if (window)
    window->GetWindow()->SetCaption(text);
  return 1;
}

u8 UiMapsWindowObject::GetCaption(ScStringView *out) const {
  out->Copy(caption);
  return 1;
}

// Save the frame and derive the client rectangle before updating an existing generic window.
u8 UiMapsWindowObject::SetFrameArea(const ScRect &area) {
  frame = area;
  content.left = 38;
  content.top = 40;
  content.right = (i32)((u32)frame.right - (u32)frame.left - 4);
  content.bottom = (i32)((u32)frame.bottom - (u32)frame.top - 4);
  if (window)
    window->GetWindow()->SetArea(frame);
  return 1;
}

u8 UiMapsWindowObject::GetFrameArea(ScRect *out) const {
  *out = frame;
  return 1;
}

// Find the requested view only when ready, then dispatch through the virtual view-switching operation.
u8 UiMapsWindowObject::ShowView(u32 identifier) {
  if (IsReady()) {
    UiMapsViewInterface *view = GetView(identifier);
    if (view)
      return ShowView(view);
  }
  return 0;
}

// Hide and release the previous view before retaining and showing the replacement.
// The window caption follows the new view's borrowed name after the visibility and toggle callbacks.
u8 UiMapsWindowObject::ShowView(UiMapsViewInterface *view) {
  if (!IsReady())
    return 0;
  if (current) {
    current->PrepareHideView();
    current->GetAssociatedIGZWin()->HideWindow();
    current->IconButtonToggleOff();
    current->Release();
    current = nullptr;
  }
  current = view;
  view->AddRef();
  current->PrepareShowView();
  current->GetAssociatedIGZWin()->ShowWindow();
  current->GetAssociatedIGZWin()->PullToFront();
  current->IconButtonToggleOn();
  WnWindow *receiver = window->GetWindow();
  receiver->SetCaption(*current->GetName());
  return 1;
}

// Find a view by identifier and request its specified interface without retaining a failed lookup.
u8 UiMapsWindowObject::GetView(const u32 &identifier, u32 iid, void **out) {
  if (IsReady()) {
    UiMapsViewInterface *view = GetView(identifier);
    if (view)
      return view->QueryInterface(iid, out);
  }
  return 0;
}

// Return the first matching borrowed view while the generic window is ready.
UiMapsViewInterface *UiMapsWindowObject::GetView(const u32 &identifier) {
  if (IsReady()) {
    for (UiMapsViewInterface **entry = views; entry != views_end; ++entry)
      if (*entry && *(*entry)->GetViewId() == identifier)
        return *entry;
  }
  return nullptr;
}

// Publish a new identifier and palette, then retain the palette; null and duplicate entries are rejected.
u8 UiMapsWindowObject::AddPalette(u32 identifier, GxPaletteView *palette) {
  if (!palette || GetPalette(identifier))
    return 0;
  UiAppendEntry(&palettes, &palettes_end, &palettes_capacity, UiPaletteEntry{identifier, palette});
  palette->AddRef();
  return 1;
}

// Return the first palette with the requested identifier without acquiring a reference.
GxPaletteView *UiMapsWindowObject::GetPalette(u32 identifier) {
  for (UiPaletteEntry *entry = palettes; entry != palettes_end; ++entry)
    if (entry->identifier == identifier)
      return entry->palette;
  return nullptr;
}

u32 UiMapsWindowObject::GetPaletteCount() const {
  return palettes ? (u32)(palettes_end - palettes) : 0;
}

// Return a borrowed palette by index, or null when the index is outside the active vector.
GxPaletteView *UiMapsWindowObject::GetPaletteByIndex(u32 index) {
  if (index >= GetPaletteCount())
    return nullptr;
  return palettes[index].palette;
}

// Append and retain a raster source only when its identifier is not already represented.
u8 UiMapsWindowObject::AddRaster(GxRasterSource *source) {
  if (!source || FindRaster(source->GetId()))
    return 0;
  UiAppendEntry(&rasters, &rasters_end, &rasters_capacity, source);
  source->AddRef();
  return 1;
}

// Return the first nonnull raster whose source identifier matches, without retaining it.
GxRasterSource *UiMapsWindowObject::FindRaster(u32 kind) {
  for (GxRasterSource **entry = rasters; entry != rasters_end; ++entry)
    if (*entry && (*entry)->GetId() == kind)
      return *entry;
  return nullptr;
}

u32 UiMapsWindowObject::GetRasterCount() const {
  return rasters ? (u32)(rasters_end - rasters) : 0;
}

// Borrow the raster at the requested index, or return null when it is outside the vector.
GxRasterSource *UiMapsWindowObject::GetRasterByIndex(u32 index) {
  if (index >= GetRasterCount())
    return nullptr;
  return rasters[index];
}

i32 UiMapsWindowObject::IsReady() {
  return window != nullptr;
}

// Release each palette in insertion order, clear its pointer and retain the vector's allocated capacity.
u8 UiMapsWindowObject::ReleaseAllPalettes() {
  UiPaletteEntry *end = palettes_end;
  for (UiPaletteEntry *entry = palettes; entry != end; ++entry) {
    if (entry->palette) {
      entry->palette->Release();
      entry->palette = nullptr;
    }
  }
  palettes_end = palettes;
  return 1;
}

// Release each raster in insertion order, clear its pointer and retain the vector's allocated capacity.
u8 UiMapsWindowObject::ReleaseAllRasters() {
  GxRasterSource **end = rasters_end;
  for (GxRasterSource **entry = rasters; entry != end; ++entry) {
    if (*entry) {
      (*entry)->Release();
      *entry = nullptr;
    }
  }
  rasters_end = rasters;
  return 1;
}

// Ask a present city layer to handle the enumeration message with the maps window as its query source.
// The original ignores the layer's result and accepts null layers.
bool UiMapsWindowObject::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<UiRasterQuery *>(this));
  return true;
}

bool UiMapsWindowObject::DoMessage(ScMessage *) {
  return true;
}

// Preserve the original no-argument factory and publish the complete owner through its leading raster-query identity.
// The default-view setter exposes its recovered state write without inventing a success value.
ScUnknown *UiCreateClass000005A3() {
  return static_cast<UiRasterQuery *>(new UiMapsWindowObject);
}

void UiMapsWindowObject::SetDefaultViewId(const u32 &identifier) {
  default_view = identifier;
}

// Query the city and its layers before refreshing each retained maps-and-graphs view in insertion order.
u8 UiMapsWindowObject::RefreshData(CtCity *city) {
  ScMessage message(kWnMsg_QueryRefresh, kUiMapsWindowId, 0, 0);
  if (!city)
    return 1;
  city->DoQueryInfo(&message, static_cast<UiRasterQuery *>(this));
  city->DoEnumLayers(&message, static_cast<CtCityEnumerator *>(this));
  UiMapsViewInterface **end = views_end;
  for (UiMapsViewInterface **entry = views; entry != end; ++entry)
    (*entry)->RefreshData(city);
  return 1;
}

u8 UiMapsWindowObject::EnumCityItems(CtCity *city, bool enabled) {
  ScMessage message(enabled ? kWnMsg_QueryOpened : kWnMsg_QueryClosed, kUiQueryTagMaps, 0, 0);
  if (city) {
    city->DoQueryInfo(&message, static_cast<UiRasterQuery *>(this));
    city->DoEnumLayers(&message, static_cast<CtCityEnumerator *>(this));
  }
  return 1;
}

// Create a view and attach its primary window before retaining it in the owned view sequence.
u8 UiMapsWindowObject::SetupView(UiMapsViewInterface *view, i32 index) {
  if (!view)
    return 0;
  ScPoint location{4, (i32)(28u * (u32)index + 4u)};
  if (!view->CreateView(static_cast<UiRasterQuery *>(this), &content, &location))
    return 0;
  WnWindow *parent = GetWindow();
  u8 result = parent->ChildAdd(view->GetAssociatedIGZWin());
  if (result) {
    view->GetAssociatedIGZWin()->HideWindow();
    if (views_end == views_capacity) {
      usize count = views ? (usize)(views_end - views) : 0;
      usize capacity = count ? count * 2 : 1;
      UiMapsViewInterface **replacement = new UiMapsViewInterface *[capacity];
      for (usize i = 0; i < count; ++i)
        replacement[i] = views[i];
      delete[] views;
      views = replacement;
      views_end = replacement + count;
      views_capacity = replacement + capacity;
    }
    *views_end++ = view;
    view->AddRef();
  }
  return result;
}

void UiMapsWindowObject::ReleaseViewsEffects() {
  UiMapsViewInterface **end = views_end;
  for (UiMapsViewInterface **entry = views; entry != end; ++entry) {
    if (*entry) {
      (*entry)->Release();
      *entry = nullptr;
    }
  }
  views_end = views;
}

void UiMapsWindowObject::ReleaseSidebarEffects() {
  ScRelease(&sidebar);
}

// Initialize the generic maps window, enumerate city items and prepare its view and sidebar dependencies.
u8 UiMapsWindowObject::Init() {
  if (IsReady())
    return 0;
  UiCursorScope cursor(14);
  u8 result = UiGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&window));
  if (result && window) {
    window->GetWindow()->SetID(kUiMapsWindowId);
    window->SetStyle(kUiGenericDraggable, 1);
    window->SetStyle(kUiGenericSecondButton, 0);
    window->GetWindow()->PrivateBuffer(true);
    result = window->SetCallback(static_cast<UiWindowCallback *>(this));
    if (result)
      result = SetFrameArea(frame);
    {
      ScString title("**<Maps, Graphs, and Charts>**");
      if (result)
        result = SetCaption(title);
    }
  }
  EnumCityItems(UiGetCity(), true);
  if (!result || !SetupViews() || !SetupSidebar())
    return 0;
  if (!UiGetPreferences()->MapsWindowPrefs()->ready ||
      (default_view = UiGetPreferences()->MapsWindowPrefs()->selected_view, !ShowView(default_view))) {
    default_view = 1530;
    if (!ShowView(default_view))
      return 0;
  }
  if (current)
    result = SetCaption(*current->GetName());
  return result != 0;
}

// Construct each map in the original order, advancing sidebar positions only for views that attach successfully.
// Preserve the unused view-ID calls and the two-row gap before charts; individual failures do not stop the ladder.
u8 UiMapsWindowObject::SetupViews() {
  UiMapsViewInterface *view = UiMapView::CreateMapAura();
  i32 index = SetupView(view, 0) != 0;
  view->GetViewId();
  view = UiMapView::CreateMapCrime();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapPopulation();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapPower();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapFlammability();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapLandValue();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapPollution();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapTraffic();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapWaterSupply();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiMapView::CreateMapZone();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  index += 2;
  view = UiChartsView::CreateChartsView();
  if (SetupView(view, index))
    ++index;
  view->GetViewId();
  view = UiGraphsView::CreateGraphsView();
  SetupView(view, index);
  view->GetViewId();
  return 1;
}

// Dispatch view buttons, window lifecycle and the original close notification.
bool UiMapsWindowObject::DoWinMsg(WnWindow *recipient, u32 message, uptr first, uptr second, uptr) {
  window->GetWindow();
  if (message == kWnMsg_Init)
    return OnInit(recipient) != 0;
  if (message == kWnMsg_Shutdown)
    return OnShutdown(recipient) != 0;
  if (message == kApMsgPreCityChange)
    return UiGetWindowManager()->DestroyWindow(recipient);
  if (message != kWnMsg_Command || first != 1)
    return false;
  u32 identifier = (u32)second;
  UiMapsViewInterface *view = GetView(identifier);
  if (view) {
    UiGetApplication()->GetBoxX()->Event(49, 0, 0, 0, 0);
    ShowView(view);
    return true;
  }
  if (identifier != kUiFrameCloseButtonId)
    return false;
  UiGetApplication()->GetBoxX()->Event(48, 0, 0, 0, 0);
  return EndDialog(recipient, 0) != 0;
}

// Register the window's message-target identity, restore geometry and reveal the sidebar.
u8 UiMapsWindowObject::OnInit(WnWindow *recipient) {
  window->GetWindow();
  ScMessageTarget *target = nullptr;
  u8 result = recipient->QueryInterface(IID_ScMessageTarget, reinterpret_cast<void **>(&target));
  if (!result)
    return result;
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->AddNotification(target, kApMsgPreCityChange);
  target->Release();
  if (recipient) {
    SetCaption(caption);
    SetFrameArea(frame);
    recipient->PullToFront();
    if (UiGetPreferences()->MapsWindowPrefs()->ready) {
      u32 language = UiGetLanguageManager()->GetCurrentLanguage();
      bool extended = language == kScLanguageJapanese || language == kScLanguageSimplifiedChinese ||
          language == kScLanguageTraditionalChinese || language == kScLanguageKorean || language == kScLanguageThai;
      ApPreferences *y_owner = UiGetPreferences(), *x_owner = UiGetPreferences(), *validator = UiGetPreferences();
      u32 y = y_owner->MapsWindowPrefs()->y, x = x_owner->MapsWindowPrefs()->x;
      if (validator->IsWindowVisible((i32)x, (i32)y, 424, extended ? 20 : 0)) {
        ApPreferences *new_y_owner = UiGetPreferences(), *new_x_owner = UiGetPreferences();
        u32 new_y = new_y_owner->MapsWindowPrefs()->y, new_x = new_x_owner->MapsWindowPrefs()->x;
        recipient->GZWinMoveTo((i32)new_x, (i32)new_y);
      }
    }
    sidebar->PullToFront();
    sidebar->ShowWindow();
  }
  return 1;
}

// Remove notifications, save view preferences and release views, sidebar, resources and generic window.
u8 UiMapsWindowObject::OnShutdown(WnWindow *recipient) {
  if (window)
    window->GetWindow();
  ScMessageTarget *target = nullptr;
  u8 result = recipient->QueryInterface(IID_ScMessageTarget, reinterpret_cast<void **>(&target));
  if (!result)
    return result;
  ScMessageServer *server = UiGetMessageServer();
  if (server)
    server->RemoveNotification(target, kApMsgPreCityChange);
  target->Release();
  ApPreferences *preferences = UiGetPreferences();
  u32 identifier = GetCurrentViewId();
  preferences->MapsWindowPrefs()->selected_view = identifier;
  if (GetWindow()) {
    WnWindow *primary = GetWindow();
    ApPreferences *x_owner = UiGetPreferences();
    x_owner->MapsWindowPrefs()->x = (u32)primary->GetL();
    primary = GetWindow();
    ApPreferences *y_owner = UiGetPreferences();
    y_owner->MapsWindowPrefs()->y = (u32)primary->GetT();
    UiGetPreferences()->MapsWindowPrefs()->ready = 1;
  }
  EnumCityItems(UiGetCity(), false);
  ScRelease(&current);
  ReleaseViewsEffects();
  ReleaseSidebarEffects();
  ReleaseAllPalettes();
  ReleaseAllRasters();
  if (window) {
    WnWindowManager *manager = recipient->GetWindowManager();
    WnWindow *parent = recipient->GetParentWin();
    manager->PostMessageToTarget(parent, kWnMsg_ChildDelete, reinterpret_cast<uptr>(recipient), 0, 0);
    window->Release();
    window = nullptr;
  }
  return 1;
}

u8 UiMapsWindowObject::EndDialog(WnWindow *, u32) {
  return 0;
}

u8 UiMapsWindowObject::DoViewChange() {
  return 1;
}

// Construct and attach the maps sidebar after adding each retained view's icon window.
u8 UiMapsWindowObject::SetupSidebar() {
  if (!IsReady())
    return 0;
  WnFlatControl *panel = nullptr;
  u8 result = UiGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&panel));
  if (!result)
    return result;
  sidebar = panel->GetWindow();
  sidebar->SetID(kUiNavigationSidebarId);
  ScRect area{4, 34, 38, 436};
  sidebar->SetArea(area);
  panel->SetOutlineFlags(13);
  panel->SetOutlineColor(UiGetWindowResources()->GetColor(kWnThemeBorderColorId));
  panel->SetFillColor(UiGetWindowResources()->GetColor(kWnThemePanelFillColorId));
  UiMapsViewInterface **end = views_end;
  for (UiMapsViewInterface **entry = views; entry != end; ++entry) {
    if (*entry) {
      WnButton *icon = (*entry)->GetIconButton();
      sidebar->ChildAdd(icon->GetWindow());
    }
  }
  WnWindow *parent = window->GetWindow();
  if (!parent->ChildAdd(sidebar)) {
    window->GetWindow()->GetWindowManager()->DestroyWindow(sidebar);
    return 0;
  }
  return result;
}

bool UiNavigation::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiNavigationInterface)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiNavigationInterface *>(this);
  AddRef();
  return true;
}

u32 UiNavigation::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiNavigation::Release() {
  return UiBaseWindow::Release();
}

// Borrow the actual primary window from the secondary navigation interface.
WnWindow *UiNavigation::AsIGZWin() {
  return this;
}

// Lock the current destination and draw each present atlas buffer into its matching rectangle.
bool UiNavigation::GZPaint() {
  if (!draw_buffer->Lock(GxLockFlags::Blt))
    return false;
  for (u32 i = 0; i < 3; ++i) {
    if (images[i]) {
      const ScRect *source = images[i]->GetBufferArea();
      draw_buffer->Blt(images[i], *source, image_areas[i], nullptr);
    }
  }
  draw_buffer->Unlock(GxLockFlags::Blt);
  return true;
}

// Force the original 160-by-164 window size and move its three atlas rectangles to the new drawing origin.
void UiNavigation::SetArea(i32 left, i32 top, i32, i32) {
  WnRootWidget::SetArea(left, top, (i32)((u32)left + 160), (i32)((u32)top + 164));
  i32 x = draw_area.left, y = draw_area.top;
  i32 dx = (i32)((u32)x - (u32)image_areas[0].left), dy = (i32)((u32)y - (u32)image_areas[0].top);
  image_areas[0].right = (i32)((u32)image_areas[0].right + (u32)dx);
  image_areas[0].bottom = (i32)((u32)image_areas[0].bottom + (u32)dy);
  image_areas[0].left = x;
  image_areas[0].top = y;
  x = (i32)((u32)draw_area.left + (u32)image_areas[0].right - (u32)image_areas[0].left);
  y = draw_area.top;
  dx = (i32)((u32)x - (u32)image_areas[1].left);
  dy = (i32)((u32)y - (u32)image_areas[1].top);
  image_areas[1].right = (i32)((u32)image_areas[1].right + (u32)dx);
  image_areas[1].bottom = (i32)((u32)image_areas[1].bottom + (u32)dy);
  image_areas[1].left = x;
  image_areas[1].top = y;
  x = draw_area.left;
  y = (i32)((u32)draw_area.top + (u32)image_areas[0].bottom - (u32)image_areas[0].top);
  dx = (i32)((u32)x - (u32)image_areas[2].left);
  dy = (i32)((u32)y - (u32)image_areas[2].top);
  image_areas[2].right = (i32)((u32)image_areas[2].right + (u32)dx);
  image_areas[2].bottom = (i32)((u32)image_areas[2].bottom + (u32)dy);
  image_areas[2].left = x;
  image_areas[2].top = y;
}

bool UiNavigation::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  ScreenToWindowCoordinates(&x, &y);
  return IsPointInWindow(x, y);
}

bool UiNavigation::IsPointInWindow(i32 x, i32 y) const {
  return WnRootWidget::IsPointInWindow(x, y);
}

bool UiNavigation::OnKeyDown(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyDown, key, modifiers, 0);
}

bool UiNavigation::OnKeyUp(u32 key, u32 modifiers) {
  return SendMsg(notification_target, kWnMsg_KeyUp, key, modifiers, 0);
}

bool UiNavigation::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  EnableClock(!clock_enabled);
  return WnRootWidget::OnMouseDownL(x, y, modifiers);
}

// Start or stop periodic clock updates only when the requested state differs.
u8 UiNavigation::EnableClock(bool enabled) {
  if (clock_enabled != enabled) {
    clock_enabled = enabled;
    if (enabled)
      timer.StartPeriodic(15000, nullptr);
    else
      timer.Stop();
    UpdateClock();
  }
  return 1;
}

// Map the three atlas indices to their original resource instances; the rotation input is unused.
u32 UiNavigation::RotationToInstance(i32, i32 index) {
  switch (index) {
  case 0:
    return kUiNavigationFirstAtlasImageId;
  case 1:
    return kUiNavigationSecondAtlasImageId;
  case 2:
    return kUiNavigationClockAtlasImageId;
  default:
    return 0;
  }
}

// Resolve absent atlas buffers and place the second and third images after the first one's edges.
u8 UiNavigation::UpdateBackgroundBuffers(i32 rotation) {
  if (!images[0]) {
    if (!GetBuffer(rotation, 0))
      return 0;
    images[0]->GetBufferArea(&image_areas[0]);
  }
  if (!images[1]) {
    if (!GetBuffer(rotation, 1))
      return 0;
    images[1]->GetBufferArea(&image_areas[1]);
    u32 width = (u32)image_areas[0].right - (u32)image_areas[0].left;
    image_areas[1].left = (i32)((u32)image_areas[1].left + width);
    image_areas[1].right = (i32)((u32)image_areas[1].right + width);
  }
  if (!images[2]) {
    if (!GetBuffer(rotation, 2))
      return 0;
    images[2]->GetBufferArea(&image_areas[2]);
    u32 height = (u32)image_areas[0].bottom - (u32)image_areas[0].top;
    image_areas[2].top = (i32)((u32)image_areas[2].top + height);
    image_areas[2].bottom = (i32)((u32)image_areas[2].bottom + height);
  }
  return 1;
}

// The original consumes the created clock buffer and, after a successful lock, its source atlas unconditionally.
bool UiNavigation::GetBuffer(i32 rotation, i32 index) {
  if (images[index])
    images[index]->Release();
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(RotationToInstance(rotation, index), kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *buffer = image.GetBuffer();
  if (buffer)
    buffer->AddRef();
  images[index] = buffer;
  if (index == 2 && !clock_buffer) {
    UiGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&clock_buffer));
    assert(clock_buffer);
    clock_buffer->AddRef();
    GxDepthType depth(GxDepthType::Depth::None);
    if (clock_buffer->Init(76, 18, format, nullptr, depth, GxVramRequest::SystemMemory, false) &&
        clock_buffer->Lock(GxLockFlags::Blt)) {
      assert(images[2]);
      i32 height = (i32)images[2]->Height();
      ScRect source{0, (i32)((u32)height - 18), 76, height}, destination{0, 0, 76, 18};
      clock_buffer->Blt(images[2], source, destination, nullptr);
      clock_buffer->Unlock(GxLockFlags::Blt);
    }
  }
  return buffer != nullptr;
}

// Restore the clock strip, optionally draw the local time and invalidate the navigation window.
// Once the language and atlas exist, the original requires the saved strip before its dimension queries.
u8 UiNavigation::UpdateClock() {
  ScLanguageUtility *language = UiGetLanguageManager()->GetLanguageUtility(kScLanguageCurrent);
  if (!language || !images[2])
    return 1;
  assert(clock_buffer);
  i32 atlas_height = (i32)images[2]->Height(), width = (i32)clock_buffer->Width();
  i32 strip_height = (i32)clock_buffer->Height();
  ScRect destination{0, (i32)((u32)atlas_height - (u32)strip_height), width, atlas_height};
  if (images[2]->Lock(GxLockFlags::Blt)) {
    const ScRect *source = clock_buffer->GetBufferArea();
    images[2]->Blt(clock_buffer, *source, destination, nullptr);
    images[2]->Unlock(GxLockFlags::Blt);
  }
  if (clock_enabled && clock_font) {
    CtTime current;
    u32 hour = current.Hour(), minute = current.Minute();
    ScString text;
    language->MakeTimeString((i32)hour, (i32)minute, 0, &text);
    clock_font->DrawTextA(images[2], destination.left + 6, destination.top, text.Data(), text.Strlen());
  }
  InvalidateSelfAndParents();
  return 1;
}

// Route navigation buttons to the city camera or acquire and execute the layers dialog command.
bool UiNavigation::OnCommand(u32 kind, u32 command_id) {
  if (kind != kWnCommandButtonActivated)
    return false;
  ApGameInterface *application = UiGetApplication();
  SpCityWindowView *city_window = application ? application->GetISC3WinCityView() : nullptr;
  switch (command_id) {
  case kUiNavigationZoomInButtonId:
    if (city_window)
      city_window->ZoomIn(true);
    return true;
  case kUiNavigationZoomOutButtonId:
    if (city_window)
      city_window->ZoomOut(true);
    return true;
  case kUiNavigationRotateCounterclockwiseButtonId:
    if (city_window)
      city_window->RotateCameraCCW(true);
    return true;
  case kUiNavigationRotateClockwiseButtonId:
    if (city_window)
      city_window->RotateCameraCW(true);
    return true;
  case kUiNavigationLayersButtonId: {
    application = UiGetApplication();
    if (application) {
      ApBoxX *box = application->GetBoxX();
      if (box)
        box->Event(kAuEventPlayEffect, kAuEffectUiButtonClick, 0, 0, 0);
    }
    UiCommandServiceRef service;
    if (service.value && service.value->GetClassFactory()) {
      UiCommandInterface *command = nullptr;
      ScCommandFactory *factory = service.value->GetClassFactory();
      if (factory->GetCommand(
              kUiModalCommand12576ClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&command))) {
        command->Execute();
        command->Release();
      }
    }
    return true;
  }
  default:
    return false;
  }
}

// Apply the repeated original directional-button sequence to an already constructed atlas reference.
static void UiNavigationButtonEffects(UiNavigation *owner, WnResources *resources, UiImageRef *image,
    const ScStringView &caption, u32 button_id, u32 tip, i32 x, i32 y) {
  GxBuffer *buffer = image->GetBuffer();
  if (!buffer)
    return;
  ScRect area;
  buffer->GetBufferArea(&area);
  u16 key = buffer->ConvertRGBValueToNative16Bit(255, 0, 255);
  buffer->SetTransparency(key);
  WnButton *button = resources->CreateFixedButton(button_id, caption, buffer, &area, 0);
  if (!button)
    return;
  UiTextRef text(tip, kRsGuiTextGroup);
  if (text.IsResolved())
    button->SetTipText(*text.GetResource());
  button->SetButtonFlag(kWnButtonShowText, false);
  button->SetButtonFlag(kWnButtonShowHint, true);
  button->SetTipPlacementType(kWnTipAboveCenter);
  WnWindow *child = button->GetWindow();
  if (owner->ChildAdd(child))
    child->GZWinMoveTo(x, y);
  button->Release();
}

// Subscribe to navigation events, build its actual buttons and compass marker, then initialize the inherited window.
// A newly acquired clock font is required by the original immediate retain before creating the minimap.
bool UiNavigation::Init() {
  if (GetInitFlag())
    return true;
  ScMessageServer *server = UiGetMessageServer();
  if (!server)
    return false;
  server->AddNotification(this, kWnMsg_ViewInfoUpdated);
  server->AddNotification(this, kCtMsg_MonthChanged);
  server->AddNotification(this, kApMsgPreCityChange);
  server->AddNotification(this, kApMsgCityReady);
  server->AddNotification(this, kTrMsg_AltitudePaletteChanged);
  WnResources *resources = UiGetWindowResources();
  if (!resources)
    return false;
  ScString empty;
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef first(kUiNavigationZoomInImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  UiNavigationButtonEffects(this, resources, &first, empty, kUiNavigationZoomInButtonId, 10, 14, 100);
  UiImageRef second(kUiNavigationZoomOutImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  UiNavigationButtonEffects(this, resources, &second, empty, kUiNavigationZoomOutButtonId, 11, 120, 100);
  UiImageRef third(kUiNavigationRotateClockwiseImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  UiNavigationButtonEffects(this, resources, &third, empty, kUiNavigationRotateClockwiseButtonId, 13, 34, 120);
  UiImageRef fourth(
      kUiNavigationRotateCounterclockwiseImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
  UiNavigationButtonEffects(this, resources, &fourth, empty, kUiNavigationRotateCounterclockwiseButtonId, 12, 100, 120);
  {
    UiImageRef layers(kUiNavigationLayersImageId, kRsMainUiImageGroup, GxVramRequest::SystemMemory, format);
    GxBuffer *buffer = layers.GetBuffer();
    if (buffer) {
      ScRect image_area;
      buffer->GetBufferArea(&image_area);
      WnButton *button = nullptr;
      if (UiGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button))) {
        UiTextRef text(kRsGuiLayerViewsText, kRsGuiTextGroup);
        if (text.GetResource())
          button->SetTipText(*text.GetResource());
        button->SetButtonFlag(kWnButtonShowText, false);
        button->SetButtonFlag(kWnButtonShowHint, true);
        button->SetTipPlacementType(kWnTipLeftMiddle);
        button->SetStyle(kWnButtonPushFixed, buffer, &image_area);
        WnWindow *child = button->GetWindow();
        child->SetID(kUiNavigationLayersButtonId);
        if (ChildAdd(child))
          child->GZWinMoveTo(114, 6);
        button->Release();
      }
    }
  }
  FtFont *marker_font = resources->GetFont(kWnFontUiSmall);
  if (marker_font) {
    UiTextRef text(kRsGuiNText, kRsGuiTextGroup);
    if (text.IsResolved()) {
      UiNavigationMarker *marker = new UiNavigationMarker;
      marker->AddRef();
      marker->SetID(kUiNavigationMarkerId);
      marker->SetCaption(*text.GetResource());
      marker->SetFont(marker_font);
      u32 shadow = MakeFillColor(96, 96, 96);
      marker->foreground = MakeFillColor(255, 200, 32);
      marker->shadow = shadow;
      ChildAdd(marker);
      marker->SetFlag(kWnFlagReorderable, false);
      marker->Release();
    }
  }
  if (!clock_font) {
    clock_font = UiGetWindowResources()->GetFont(kWnFontDefault);
    assert(clock_font);
    clock_font->AddRef();
  }
  CreateMiniMap();
  SetSize(160, 164);
  SetFlag(kWnFlagTransparentBuffer, true);
  SetFlag(kWnFlagHitTestTransparency, true);
  SetFlag(kWnFlagPrivateBuffer, true);
  UpdateBackgroundBuffers(0);
  return UiBaseWindow::Init();
}

// Hide the navigation window, retire retained images and timer, then remove its original notifications.
bool UiNavigation::Shutdown() {
  HideWindow();
  for (u32 i = 0; i < 3; ++i) {
    if (images[i]) {
      images[i]->Release();
      images[i] = nullptr;
    }
  }
  ScRelease(&clock_buffer);
  ScRelease(&clock_font);
  if (timer.IsRunning())
    timer.Stop();
  ScMessageServer *server = UiGetMessageServer();
  if (server) {
    server->RemoveNotification(this, kTrMsg_AltitudePaletteChanged);
    server->RemoveNotification(this, kWnMsg_ViewInfoUpdated);
    server->RemoveNotification(this, kCtMsg_MonthChanged);
    server->RemoveNotification(this, kApMsgPreCityChange);
    server->RemoveNotification(this, kApMsgCityReady);
  }
  UiBaseWindow::Shutdown();
  return true;
}

u8 UiNavigation::DestroyMiniMap() {
  WnWindow *child = GetChildWindowFromID(kUiNavigationMapControlId);
  return child ? ChildRemove(child) : 1;
}

// Reposition the compass marker, rotate the mini-map and refresh the current window area.
u8 UiNavigation::OnRotationChanged(i32 rotation) {
  i32 angle = -1, x = 78, y = 79;
  switch (rotation) {
  case 0:
    angle = 225;
    y = 13;
    break;
  case 1:
    angle = 315;
    x = 15;
    break;
  case 2:
    angle = 45;
    y = 143;
    break;
  case 3:
    angle = 135;
    x = 142;
    break;
  }
  if (angle != -1) {
    UiMapControl *map = nullptr;
    if (GetChildAs(kUiNavigationMapControlId, IID_UiMapControl, reinterpret_cast<void **>(&map))) {
      map->SetRotation(angle);
      map->DirtyCellRegion(nullptr);
      map->Release();
    }
  }
  WnWindow *marker = nullptr;
  if (GetChildAs(kUiNavigationMarkerId, IID_WnWindow, reinterpret_cast<void **>(&marker))) {
    i32 height = marker->GetH(), width = marker->GetW();
    marker->GZWinMoveTo(x - (width - 1) / 2, y - (height - 1) / 2);
    marker->Release();
  }
  i32 top = GetT(), left = GetL();
  SetArea(left, top, 0, 0);
  InvalidateSelfAndParents();
  return 1;
}

// Handle mini-map, camera and clock notifications; forward unrelated messages to the original window base.
bool UiNavigation::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kApMsgPreCityChange:
    DestroyMiniMap();
    return true;
  case kWnMsg_NavigationClockTick:
    UpdateClock();
    return true;
  case kTrMsg_AltitudePaletteChanged:
    CreateMiniMap();
    OnRotationChanged(word228);
    return true;
  case kApMsgCityReady:
    CreateMiniMap();
    return true;
  case kWnMsg_ViewInfoUpdated:
    if (message->data1 == kSpViewUpdate_Snapshot) {
      SpIsoViewInfo *info = reinterpret_cast<SpIsoViewInfo *>(message->data2);
      i32 compass;
      info->GetCompass(&compass);
      if (word228 != compass) {
        word228 = compass;
        UpdateBackgroundBuffers(compass);
        OnRotationChanged(word228);
      } else {
        i32 zoom;
        info->GetZoom(&zoom);
        if (word232 != zoom) {
          word232 = zoom;
          InvalidateSelfAndParents();
        } else {
          CtCoord focus{kCtUnknownCoordinate, kCtUnknownCoordinate, kCtUnknownCoordinate};
          info->GetInfo(&focus);
          if (word236 != focus.x || word240 != focus.z) {
            word236 = focus.x;
            word240 = focus.z;
            word244 = focus.altitude;
            InvalidateSelfAndParents();
          }
        }
      }
    }
    return true;
  case kWnMsg_MapMouseDownL: {
    SpViewInterface *view = UiGetCurrentView();
    if (view) {
      u32 packed = (u32)message->data1;
      view->FocusCameraOn((u32)(i32)(i16)packed, (u32)(i32)(i16)(packed >> 16), true);
    }
    return true;
  }
  case kWnMsg_MapMouseDownR:
    return true;
  case kCtMsg_MonthChanged:
    OnRotationChanged(word228);
    return true;
  default:
    return UiBaseWindow::DoMessage(message);
  }
}

// Set the current-view sprite's original filter ID, map dimensions, projection state and red outline color.
UiNavigationMapSprite::UiNavigationMapSprite(u32 x, u32 z) : color(0), grid_width(64), grid_height(64) {
  SetId(10049);
  SetBaseSize(x, z);
  SetPostFilter(1);
  color = ConvertRGBValueToNative16Bit(255, 0, 0);
}

UiNavigationMapSprite::~UiNavigationMapSprite() {
}

// Draw the current isometric view's four cell-map corners when both the view and city are present.
bool UiNavigationMapSprite::Draw(GxBuffer *destination, const ScRect &rectangle, ScRect *) {
  SpViewInterface *view = UiGetCurrentView();
  if (!view)
    return false;
  SpCellMap *map = static_cast<SpIsoView *>(view)->GetCitySpriteCellMap();
  if (!map || !UiGetCity())
    return false;
  grid_width = map->GetActualGridSizeX();
  grid_height = map->GetActualGridSizeZ();
  ScPoint points[4];
  map->GetCellsForViewCornersUnclipped(points, false, nullptr);
  DrawBox(destination, rectangle, points, color, 1, 0);
  return true;
}

// Adjust the supplied corners by the source margin and draw each edge in clockwise order.
u8 UiNavigationMapSprite::DrawBox(
    GxBuffer *destination, const ScRect &rectangle, ScPoint *points, u32 pixel, u32 mode, i32 expand) {
  if (expand) {
    points[0].x = (i32)((u32)points[0].x - (u32)expand);
    points[0].y = (i32)((u32)points[0].y - (u32)expand);
    points[1].x = (i32)((u32)points[1].x + (u32)expand);
    points[1].y = (i32)((u32)points[1].y - (u32)expand);
    points[2].x = (i32)((u32)points[2].x + (u32)expand);
    points[2].y = (i32)((u32)points[2].y + (u32)expand);
    points[3].x = (i32)((u32)points[3].x - (u32)expand);
    points[3].y = (i32)((u32)points[3].y + (u32)expand);
  }
  DrawLine(points[0].x, points[0].y, points[1].x, points[1].y, mode, destination, rectangle, pixel, nullptr);
  DrawLine(points[1].x, points[1].y, points[2].x, points[2].y, mode, destination, rectangle, pixel, nullptr);
  DrawLine(points[2].x, points[2].y, points[3].x, points[3].y, mode, destination, rectangle, pixel, nullptr);
  return DrawLine(points[3].x, points[3].y, points[0].x, points[0].y, mode, destination, rectangle, pixel, nullptr);
}

UiNavigationMarker::UiNavigationMarker()
    : font(nullptr), text_width(0), text_height(0), foreground(kWnDefaultColor), shadow(0) {
}

UiNavigationMarker::~UiNavigationMarker() {
}

// Require a font and nonempty caption, configure the original background flags and initialize the base window.
bool UiNavigationMarker::Init() {
  if (!font || !GetCaption()->Strlen()) {
    Shutdown();
    return false;
  }
  WnResources *resources = UiGetWindowResources();
  if (resources) {
    SetFlag(kWnFlagTransparentBuffer, true);
    SetFlag(kWnFlagPrivateBuffer, true);
    SetFillColor(resources->GetColor(kWnColorUiTransparency));
  }
  SetSize(0, 0);
  return UiBaseWindow::Init();
}

bool UiNavigationMarker::Shutdown() {
  ScRelease(&font);
  return UiBaseWindow::Shutdown();
}

// Fill the optional background and draw the offset shadow followed by the original foreground text.
bool UiNavigationMarker::GZPaint() {
  if (!font)
    return true;
  if (GetFlag(kWnFlagPrivateBuffer) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  font->DrawTextA(draw_buffer, draw_area.left + 1, draw_area.top + 1, caption.Data(), caption.Strlen(), shadow);
  if (foreground == kWnDefaultColor)
    font->DrawTextA(draw_buffer, draw_area.left, draw_area.top, caption.Data(), caption.Strlen());
  else
    font->DrawTextA(draw_buffer, draw_area.left, draw_area.top, caption.Data(), caption.Strlen(), foreground);
  return true;
}

void UiNavigationMarker::SetArea(i32 left, i32 top, i32, i32) {
  WnRootWidget::SetArea(left, top, (i32)((u32)left + (u32)text_width), (i32)((u32)top + (u32)text_height));
  InvalidateSelfAndParents();
}

void UiNavigationMarker::SetCaption(const ScStringView &text) {
  WnRootWidget::SetCaption(text);
  UpdateBoundsEffects();
}

// Retain the replacement font before releasing the previous font and refreshing the text extents.
u8 UiNavigationMarker::SetFont(FtFont *value) {
  if (value)
    value->AddRef();
  if (font)
    font->Release();
  font = value;
  UpdateBoundsEffects();
  return 1;
}

// Recompute dimensions and request the source's zero-size layout only when both font and caption are present.
void UiNavigationMarker::UpdateBoundsEffects() {
  if (font && caption.Strlen()) {
    u32 length = caption.Strlen();
    const char *text = caption.Data();
    text_width = font->Measure(text, length) + 1;
    text_height = font->Height(0) + 1;
    SetSize(0, 0);
  }
}

// Recreate the mini-map from the city's terrain and zoning sprites, then attach its live current-view overlay.
u8 UiNavigation::CreateMiniMap() {
  DestroyMiniMap();
  u8 result = 0;
  GxColorType format(GxColorType::Color::RGB16);
  UiImageRef image(33, kRsMapImageGroup, GxVramRequest::SystemMemory, format);
  GxBuffer *palette_image = image.GetBuffer();
  if (!palette_image)
    return result;
  CtCity *city = UiGetCity();
  if (!city)
    return result;
  ScDirtBag *terrain = city->DirtBag();
  CtZoneLayer *zones = city->ZoneLayer();
  if (!terrain || !zones)
    return result;
  UiMapControl *map = nullptr;
  if (!UiGetCom()->GetClassObject(kUiMapControlClassId, IID_UiMapControl, reinterpret_cast<void **>(&map)))
    return result;
  SpMapSpriteFactory *factory = nullptr;
  GxRasterSource *sprite = nullptr;
  if (terrain->QueryInterface(IID_SpMapSpriteFactory, reinterpret_cast<void **>(&factory))) {
    if (factory->CreateMapSprite(kUiTerrainMapRasterId, IID_GxRasterSource, reinterpret_cast<void **>(&sprite))) {
      map->AddFilter(sprite);
      sprite->Release();
      sprite = nullptr;
    }
    factory->Release();
    factory = nullptr;
  }
  if (zones->QueryInterface(IID_SpMapSpriteFactory, reinterpret_cast<void **>(&factory))) {
    if (factory->CreateMapSprite(kUiZoningMapRasterId, IID_GxRasterSource, reinterpret_cast<void **>(&sprite))) {
      GxPaletteView *palette = nullptr;
      if (CreatePalette(palette_image, 7, 16, &palette)) {
        map->AddFilter(sprite);
        sprite->SetPalette(palette);
        palette->Release();
      }
      sprite->Release();
      sprite = nullptr;
    }
    factory->Release();
  }
  u32 z = city->CellCountZ(), x = city->CellCountX();
  UiNavigationMapSprite *outline = new UiNavigationMapSprite(x, z);
  map->AddFilter(static_cast<GxRasterSource *>(outline));
  map->AsIGZWin()->SetID(kUiNavigationMapControlId);
  map->AsIGZWin()->SetArea(14, 15, 142, 143);
  WnWindow *child = map->AsIGZWin();
  result = ChildAdd(child);
  ChildToBack(map->AsIGZWin());
  map->AsIGZWin()->SetFlag(kWnFlagReorderable, false);
  map->SetCoordinateForwarding(5, true, nullptr);
  map->Release();
  return result;
}

// Construct the navigation window and return its separate source identity without acquiring another reference.
// Candidate: initialization, timer and mini-map behavior require installed-code comparison.
ScUnknown *UiCreateClassE2F95AF5() {
  return static_cast<UiNavigationInterface *>(new UiNavigation);
}

void UiNavigation::ClockTimerCallback(uptr receiver) {
  WnWindow *window = reinterpret_cast<WnWindow *>(receiver);
  UiGetWindowManager()->PostMessageToTarget(window, kWnMsg_NavigationClockTick, 0, 0, 0);
}

UiNavigation::UiNavigation()
    : images{nullptr, nullptr, nullptr}, image_areas{}, word228(-1), word232(-1), word236(INT32_MAX),
      word240(INT32_MAX), word244(INT32_MAX), clock_enabled(false), timer(), clock_buffer(nullptr),
      clock_font(nullptr) {
  uptr receiver = reinterpret_cast<uptr>(static_cast<WnWindow *>(this));
  timer.SetCallbackFunction(ClockTimerCallback, receiver);
}

UiNavigation::~UiNavigation() {
}

// Build a logical palette from one image row and publish its original palette interface.
u8 UiNavigation::CreatePalette(GxBuffer *source, u32 row, u32 count, GxPaletteView **out) {
  if (!out || !source || source->Width() < count || row > source->Height())
    return 0;
  GxPalette *palette = new GxPalette;
  palette->AddRef();
  u8 result = 0;
  if (palette->MakeGenericPalette((i32)count, 32) && source->Lock(GxLockFlags::GetSetPixel)) {
    for (u32 i = 0; i < count; ++i)
      palette->SetColor((i32)i, source->GetPixel(i, row));
    source->Unlock(GxLockFlags::GetSetPixel);
    result = 1;
  }
  if (result)
    result = palette->QueryInterface(IID_GxPaletteView, reinterpret_cast<void **>(out));
  palette->Release();
  return result;
}
