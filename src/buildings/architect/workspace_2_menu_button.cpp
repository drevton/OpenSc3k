#define WIN32_LEAN_AND_MEAN
#include <SDL3/SDL_timer.h>
#include <math.h>
#include <string.h>
#include "buildings/architect/api.h"
#include "buildings/architect/asset.h"
#include "buildings/architect/bd_catalog.h"
#include "buildings/architect/bd_editor.h"
#include "buildings/architect/bd_grid.h"
#include "buildings/architect/document.h"
#include "buildings/architect/files.h"
#include "buildings/architect/manager.h"
#include "buildings/architect/renderer.h"
#include "common/api.h"
#include "common/framework.h"
#include "common/ids.h"
#include "common/sc3.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "ui/commands.h"
#include "ui/controls.h"
#include "ui/generic_dialogs.h"
#include "window/api.h"
#include "window/controls.h"
#include "window/lists.h"
#include "window/text_edit.h"
#include "window/window.h"

// Create the requested tool group and its original item windows, then retain it in the selection's group list.
// Register both catalog-selection notifications before constructing any of the windows.
u8 BdToolSelection::BuildIcons(WnWindow *window) {
  window->SetFlag(kWnFlagDraggable, false);
  window->SetFlag(512, false);
  BdGetCatalog();
  u32 background = BdPaletteBackground;
  ScMessageServer *server = BdGetMessageServer();
  server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectToolItemSelected);
  server->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectForwardInput);
  if (group_id < kBdShapeAssetGroupId || group_id > kBdPropAssetGroupId)
    return 0;
  BdToolGroupWindow *group = new BdToolGroupWindow;
  group->AddRef();
  i32 height = window->GetH(), width = window->GetW();
  group->SetArea(0, 0, width, height);
  group->ShowWindow();
  group->SetFlag(kWnFlagDraggable, false);
  group->SetFlag(512, false);
  group->SetID(group_id);
  ScString label;
  if (group_id == kBdShapeAssetGroupId) {
    BdToolItem *item = new BdToolItem;
    item->AddRef();
    item->ShowWindow();
    item->SetID(kBdShapeItemId);
    item->SetSize(window->GetW(), 16);
    item->SetBackground(kBdShapeToolBackgroundColor);
    BuildShapeIcons(item);
    group->AddItem(item, label);
    item->SetRows(35);
    item->Release();
  } else {
    static const BdToolItemSpec materials[] = {
        {kBdWallMaterialItemId, kRsArchitectWallsText, kRsArchitectTextGroup, 35},
        {kBdRooftopMaterialItemId, kRsArchitectRooftopsText, kRsArchitectTextGroup, 35},
        {kBdGroundMaterialItemId, kRsArchitectGroundText, kRsArchitectTextGroup, 35},
        {kBdColorMaterialItemId, kRsArchitectSupplementalColorsText, kRsArchitectSupplementalTextGroup, 35},
        {kBdCustomMaterialItemId, 384, kRsArchitectTextGroup, 35}};
    static const BdToolItemSpec compositions[] = {
        {kBdTraditionalCompositionItemId, kRsArchitectTraditionalText, kRsArchitectTextGroup, 16},
        {kBdContemporaryCompositionItemId, kRsArchitectContemporaryText, kRsArchitectTextGroup, 16},
        {kBdCustomCompositionItemId, 393, kRsArchitectTextGroup, 16}};
    static const BdToolItemSpec objects[] = {
        {kBdArchitecturePropItemId, kRsArchitectSupplementalArchitectureText, kRsArchitectSupplementalTextGroup, 35},
        {kBdDecorationPropItemId, kRsArchitectSupplementalBuildingDecorationText, kRsArchitectSupplementalTextGroup,
            35},
        {kBdPlazaStreetPropItemId, kRsArchitectSupplementalPlazaAndStreetText, kRsArchitectSupplementalTextGroup, 35},
        {kBdYardPropItemId, kRsArchitectSupplementalYardObjectsText, kRsArchitectSupplementalTextGroup, 35},
        {kBdFloraPropItemId, kRsArchitectSupplementalFloraText, kRsArchitectSupplementalTextGroup, 35},
        {kBdIndustrialPropItemId, kRsArchitectSupplementalIndustrialText, kRsArchitectSupplementalTextGroup, 35},
        {kBdRooftopPropItemId, kRsArchitectSupplementalRooftopsText, kRsArchitectSupplementalTextGroup, 35},
        {kBdVehiclePropItemId, kRsArchitectSupplementalVehiclesText, kRsArchitectSupplementalTextGroup, 35},
        {kBdCustomPropItemId, kRsArchitectSupplementalCustomPropsText, kRsArchitectSupplementalTextGroup, 35}};
    const BdToolItemSpec *spec;
    u32 count;
    if (group_id == kBdMaterialAssetGroupId) {
      spec = materials;
      count = sizeof(materials) / sizeof(materials[0]);
    } else if (group_id == kBdCompositionAssetGroupId) {
      spec = compositions;
      count = sizeof(compositions) / sizeof(compositions[0]);
    } else {
      spec = objects;
      count = sizeof(objects) / sizeof(objects[0]);
    }
    for (u32 i = 0; i < count; ++i) {
      BdToolItem *item = new BdToolItem;
      item->AddRef();
      item->ShowWindow();
      item->SetID(spec[i].identifier);
      item->SetBackground(background);
      item->SetSize(170, 16);
      {
        BdTextResource text(spec[i].text, spec[i].text_group);
        label.Erase(0, label.Strlen());
        if (text.text)
          label.Copy(*static_cast<ScStringView *>(text.text));
        group->AddItem(item, label);
      }
      item->SetRows(spec[i].rows);
      item->Release();
    }
  }
  u8 added = window->ChildAdd(group->GetWindow());
  if (last == capacity) {
    usize count = first ? static_cast<usize>(last - first) : 0;
    usize next = count ? 2 * count : 1;
    BdToolGroupWindow **grown = new BdToolGroupWindow *[next];
    for (usize i = 0; i < count; ++i)
      grown[i] = first[i];
    delete[] first;
    first = grown;
    last = first + count;
    capacity = first + next;
  }
  *last++ = group;
  return added;
}

// Translate architect group messages and lazily populate the selected tool item's catalog icons.
// Three dynamic kinds clear their existing icons before being populated again.
bool BdToolSelection::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ArchitectForwardInput) {
    if (message->data1 == 3 && message->data2 == kBdCommandSelectionMode) {
      ScMessageServer *server = BdGetMessageServer();
      u32 selected = static_cast<u32>(message->data3), id = 0;
      if (selected == 1)
        id = kBdWallMaterialItemId;
      else if (selected == 2)
        id = kBdTraditionalCompositionItemId;
      else if (selected == 3)
        id = kBdArchitecturePropItemId;
      if (id) {
        ScMessage change(kWnMsg_ArchitectToolItemSelected, id, 0, 0);
        server->MessagePost(change, false);
      }
    }
    return true;
  }
  if (message->type != kWnMsg_ArchitectToolItemSelected)
    return false;
  u32 id = static_cast<u32>(message->data1);
  BdToolGroupWindow *group = nullptr;
  bool reload = false, found = false;
  for (BdToolGroupWindow **current = first; current != last; ++current) {
    group = *current;
    if (group && group->HasItem(id)) {
      reload = group->GetItemFlag(id) == 0;
      found = true;
      break;
    }
  }
  if (id == kBdCustomMaterialItemId || id == kBdCustomCompositionItemId || id == kBdCustomPropItemId) {
    if (!found)
      return true;
    BdToolItem *old = nullptr;
    group->GetItem(id, &old);
    if (!old)
      return true;
    old->ClearIcons();
    // The installed path keeps this acquired reference when it obtains the item a second time below.
    group->SetItemFlag(id, 0);
    reload = true;
  }
  if (!reload)
    return true;
  BdCatalog *catalog = BdGetCatalog();
  BdToolItem *item = nullptr;
  group->GetItem(id, &item);
  WnWindowManager *manager = BdGetWindowManager();
  bool pushed = false;
  if (manager) {
    WnCursorManager *cursors = manager->GetCursorManager();
    if (cursors)
      pushed = cursors->PushAutoCursor(14);
  }
  if (id >= kBdWallMaterialItemId && id <= kBdVehiclePropItemId && item) {
    static const u8 kinds[] = {2, 0, 1, 3, 6, 5, 4, 7, 16, 8, 8, 9, 10, 11, 12, 13, 14, 15};
    u32 kind = kinds[id - kBdWallMaterialItemId];
    catalog->IndexKind(kind);
    if (kind == 4 || kind == 5 || kind == 7) {
      catalog->BuildPlacementFiles(kind);
      BuildCompositionIcons(item, catalog, kind);
    }
    if (kind != 4 && kind != 5)
      BuildAssetIcons(item, catalog, kind);
  }
  if (item)
    item->Release();
  group->SetItemFlag(id, 1);
  if (pushed) {
    manager = BdGetWindowManager();
    if (manager) {
      WnCursorManager *cursors = manager->GetCursorManager();
      if (cursors)
        cursors->PopAutoCursor();
    }
  }
  return true;
}

#undef RGB

BdFrameTimer BdWorkspaceTimer = {0, 0, 0, 0, 0, 2000};

BdToolButtonSpec::BdToolButtonSpec()
    : identifier(0), text_group(kRsArchitectTextGroup), text_instance(0), image_group(kBdWorkspaceImageGroup),
      image_instance(0), flags(0) {
}

// A toolbar strip owns its child image buttons through the ordinary root-window hierarchy.
class BdToolStrip : public WnArchitectRootWidget {
public:
  BdToolStrip();
  ~BdToolStrip() override;
  virtual u8 AddButton(u32 id, GxBuffer *image, const ScStringView &tip);
  virtual u8 FitButtons();
  virtual u8 ClearButtonLayout();
};

// Initialize the root window for a toolbar strip; the strip adds no instance fields.
BdToolStrip::BdToolStrip() = default;

// Destroy the toolbar strip through its root-window base.
BdToolStrip::~BdToolStrip() = default;

// Create and attach an image button with the requested identifier and localized tip.
// Fit the button and move its window behind siblings after attachment, returning the attachment result.
u8 BdToolStrip::AddButton(u32 id, GxBuffer *image, const ScStringView &tip) {
  if (!image)
    return 0;
  WnButton *button = nullptr;
  if (!BdGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button)))
    return 0;
  button->GetWindow()->SetID(id);
  button->SetTipText(tip);
  button->SetButtonFlag(kWnButtonShowHint, true);
  button->SetButtonFlag(kWnButtonShowText, false);
  button->SetTipPlacementType(kWnTipAboveCenter);
  button->SetImage(3, image, *image->GetBufferArea());
  u8 result = ChildAdd(button->GetWindow());
  button->GetWindow()->SetFlag(kWnFlagReorderable, true);
  button->GetWindow()->SendToBack();
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  button->Fit();
  button->Release();
  return result;
}

// Place a strip child at the running horizontal offset and update the original width-based extent words.
static bool BdPlaceStripButton(WnWindow *, u32, void *object, void *context) {
  WnButton *button = static_cast<WnButton *>(object);
  i32 *extent = static_cast<i32 *>(context);
  button->GetWindow()->GZWinMoveTo(extent[0], extent[1]);
  extent[0] = extent[0] + button->GetWindow()->GetW();
  extent[2] = extent[2] + button->GetWindow()->GetW();
  i32 width = button->GetWindow()->GetW();
  if (width >= extent[3])
    extent[3] = width;
  return true;
}

static bool BdStopStripEnumeration(WnWindow *, u32, void *, void *) {
  return false;
}

// Arrange image-button children horizontally and resize the strip to the accumulated source extents.
u8 BdToolStrip::FitButtons() {
  i32 extent[4] = {};
  EnumChildren(IID_WnButton, BdPlaceStripButton, extent);
  SetSize(extent[2], extent[3]);
  return 1;
}

// Enumerate buttons without changing the context and set the strip size to the unchanged empty extent.
u8 BdToolStrip::ClearButtonLayout() {
  i32 extent[4] = {};
  EnumChildren(IID_WnButton, BdStopStripEnumeration, extent);
  SetSize(extent[2], extent[3]);
  return 1;
}

// Build a toolbar strip from the requested image and text resources and attach it to the parent window.
// Missing images are skipped; missing localized tips use the empty String. The original always returns zero.
u8 BD_STDCALL BdCreateToolStrip(WnWindow *parent, u32 identifier, const BdToolButtonSpec *buttons, i32 count) {
  ScString empty;
  BdToolStrip *strip = new BdToolStrip;
  strip->AddRef();
  strip->SetID(identifier);
  while (count) {
    GxColorType format(GxColorType::Color::RGB16);
    BdImageResource image(buttons->image_instance, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
    if (image.buffer) {
      BdTextResource text(buttons->text_instance, kRsArchitectTextGroup);
      ScStringView *tip = text.text ? static_cast<ScStringView *>(text.text) : &empty;
      strip->AddButton(buttons->identifier, image.buffer, *tip);
    }
    ++buttons;
    count = static_cast<i32>(static_cast<u32>(count) - 1);
  }
  parent->ChildAdd(strip);
  strip->FitButtons();
  strip->Release();
  return 0;
}

// Resolve a toolbar image and create the requested image button, applying its optional color key and localized tip.
// Publish one retained button only after the original mode-specific image and tip settings have been applied.
u8 BD_STDCALL BdCreateToolButton(u32 mode, const BdToolButtonSpec *spec, WnButton **out) {
  if (!out || mode > 4)
    return 0;
  GxColorType format(GxColorType::Color::RGB16);
  BdImageResource resource(spec->image_instance, spec->image_group, GxVramRequest::SystemMemory, format);
  GxBuffer *image = resource.buffer;
  if (!image)
    return 0;
  WnButton *button = nullptr;
  if (!BdGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button)))
    return 0;
  button->GetWindow()->SetID(spec->identifier);
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  button->SetButtonFlag(kWnButtonShowText, false);
  const ScRect *bounds = image->GetBufferArea();
  if (spec->flags & 1)
    image->SetTransparency(image->ConvertRGBValueToNative16Bit(255, 0, 255));
  if (mode == 1) {
    button->SetImage(5, image, *bounds);
    button->SetButtonFlag(kWnButtonToggleOnPress, true);
  } else {
    if (mode > 2)
      button->GetWindow()->SetFlag(kWnFlagEnabled, false);
    button->SetImage(3, image, *bounds);
  }
  if (spec->text_instance) {
    ScString empty;
    BdTextResource text(spec->text_instance, spec->text_group);
    ScStringView *tip = text.text ? static_cast<ScStringView *>(text.text) : &empty;
    button->SetTipText(*tip);
    button->SetButtonFlag(kWnButtonShowHint, true);
    button->SetTipPlacementType(kWnTipLeftMiddle);
  }
  button->AddRef();
  *out = button;
  button->Release();
  return 1;
}

// Create a resource-backed menu button and give the group its heading or an action according to the image mode.
// Release the builder's temporary reference after the group has retained the button.
u8 BD_STDCALL BdAddMenuButton(BdToolMenuGroupView *group, u32 mode, const BdToolButtonSpec *spec) {
  WnButton *button = nullptr;
  if (!BdCreateToolButton(mode, spec, &button))
    return 0;
  u8 result = mode == 1 ? group->SetHeading(button) : group->AddButton(button);
  button->Release();
  return result;
}

// Fit a label child to its caption, releasing the queried control after the operation.
// The lookup result, rather than the label's fit result, determines success.
bool BdFitChildLabel(WnWindow *window, u32 identifier) {
  if (!window)
    return false;
  WnLabelControl *label = nullptr;
  if (!window->GetChildAs(identifier, IID_WnLabelControl, reinterpret_cast<void **>(&label)))
    return false;
  label->Fit();
  label->Release();
  return true;
}

// Update a label child's flag through its queried control and release that reference.
// A found control reports success even when its flag operation returns false.
bool BdSetChildLabelFlag(WnWindow *window, u32 identifier, u32 mask, bool enabled) {
  if (!window)
    return false;
  WnLabelControl *label = nullptr;
  if (!window->GetChildAs(identifier, IID_WnLabelControl, reinterpret_cast<void **>(&label)))
    return false;
  label->SetLabelFlag(mask, enabled);
  label->Release();
  return true;
}

// Retain an image and initialize its source rectangle from the supplied area or the image itself.
// Both current and initial coordinates start at the supplied offsets.
BdButtonImage::BdButtonImage(GxBuffer *image, const ScRect *rectangle, i32 x, i32 y)
    : buffer(image), offset_x(x), offset_y(y), origin_x(x), origin_y(y) {
  if (rectangle)
    source = *rectangle;
  else if (buffer)
    buffer->GetBufferArea(&source);
  else
    source = {};
  if (buffer)
    buffer->AddRef();
}

// Release the retained image and clear its geometry when the enclosing button is destroyed.
BdButtonImage::~BdButtonImage() {
  if (buffer)
    buffer->Release();
  buffer = nullptr;
  source = {};
  offset_x = offset_y = origin_x = origin_y = 0;
}

// Retain the replacement before releasing the previous image, including when both are the same object.
// Reset the source rectangle to the replacement's full area without changing its offsets.
bool BdButtonImage::SetBuffer(GxBuffer *image) {
  if (image)
    image->AddRef();
  if (buffer)
    buffer->Release();
  buffer = image;
  if (image)
    image->GetBufferArea(&source);
  else
    source = {};
  return true;
}

// Treat points outside the image as transparent and query in-range pixels only while the image is locked.
// A failed lock leaves the original false result and does not issue an unlock.
bool BdButtonImage::Transparent(i32 x, i32 y) const {
  if (!buffer || x < offset_x || y < offset_y)
    return true;
  i32 width = buffer->Width();
  if (x > (i32)((u32)offset_x + (u32)width))
    return true;
  i32 captured_y = offset_y;
  if (y > (i32)((u32)captured_y + buffer->Height()))
    return true;
  x = (i32)((u32)x - (u32)offset_x);
  y = (i32)((u32)y - (u32)captured_y);
  if (x < 0 || y < 0)
    return true;
  bool result = false;
  if (buffer->Lock(GxLockFlags::GetSetPixel)) {
    result = buffer->IsPixelTransparent((u32)x, (u32)y);
    buffer->Unlock(GxLockFlags::GetSetPixel);
  }
  return result;
}

BdMenuButton::BdMenuButton() : image(nullptr, nullptr, 0, 0), button_flags(4) {
}

BdMenuButton::~BdMenuButton() {
}

// Let the root process all messages first, forwarding unhandled key up and down messages to the parent.
bool BdMenuButton::DoMessage(ScMessage *message) {
  if (message->type < kWnMsg_KeyDown || message->type > kWnMsg_KeyUp)
    return WnArchitectRootWidget::DoMessage(message);
  if (WnArchitectRootWidget::DoMessage(message))
    return true;
  if (!GetParentWin())
    return false;
  return GetParentWin()->DoMessage(message);
}

// Test the inclusive absolute window bounds and, when enabled, the image's transparency at that point.
bool BdMenuButton::IsPointInWindowScreenCoordinates(i32 x, i32 y) const {
  if (x < absolute_area.left || y < absolute_area.top || x > absolute_area.right || y > absolute_area.bottom)
    return false;
  if (button_flags & 4)
    return !image.Transparent((i32)((u32)x - (u32)absolute_area.left), (i32)((u32)y - (u32)absolute_area.top));
  return true;
}

// Draw the retained image at its offset inside the clipped drawing area while the destination is locked.
bool BdMenuButton::GZPaint() {
  if (image.buffer && (button_flags & 1) && draw_buffer->Lock(GxLockFlags::Blt)) {
    i32 left = (i32)((u32)draw_area.left + (u32)image.offset_x);
    i32 top = (i32)((u32)draw_area.top + (u32)image.offset_y);
    ScRect destination = {left, top, (i32)((u32)left + (u32)image.source.right - (u32)image.source.left),
        (i32)((u32)top + (u32)image.source.bottom - (u32)image.source.top)};
    draw_buffer->Blt(image.buffer, image.source, destination, &draw_area);
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  return true;
}

bool BdMenuButton::IsPointInWindow(i32 x, i32 y) const {
  return IsPointInWindowScreenCoordinates(
      (i32)((u32)x + (u32)absolute_area.left), (i32)((u32)y + (u32)absolute_area.top));
}

bool BdMenuButton::OnMouseDownL(i32, i32, u32) {
  SetButtonFlag(8, true);
  return true;
}

// Clear the pressed state and send the parent's command message only for a release inside the button.
bool BdMenuButton::OnMouseUpL(i32 x, i32 y, u32) {
  bool activate = (button_flags & 8) && IsPointInWindow(x, y);
  SetButtonFlag(8, false);
  if (activate) {
    u32 command = GetID();
    WnWindow *parent_window = GetParentWin();
    SendMsg(parent_window, kWnMsg_Command, kWnCommandButtonActivated, command, 0);
  }
  return true;
}

// Capture the pointer while it is over the button unless a left-button press is in progress.
// Release capture when the pointer moves outside the captured button.
bool BdMenuButton::OnMouseMove(i32 x, i32 y, u32) {
  if (!(button_flags & 8)) {
    if (owner->GetCapture() == this) {
      if (!IsPointInWindow(x, y))
        owner->ReleaseCapture(this);
    } else {
      owner->SetCapture(this);
      SetButtonFlag(kWnButtonChecked, true);
    }
  }
  return true;
}

bool BdMenuButton::GZOnCaptureChanged(WnWindow *window) {
  SetButtonFlag(kWnButtonChecked, window == this);
  if (window != this)
    SetButtonFlag(8, false);
  InvalidateSelfAndParents();
  return true;
}

bool BdMenuButton::SetImage(GxBuffer *buffer) {
  return image.SetBuffer(buffer);
}

bool BdMenuButton::Fit() {
  if (!image.buffer)
    return false;
  i32 height = image.buffer->Height(), width = image.buffer->Width();
  SetSize(width, height);
  return true;
}

// Set or clear the requested button-state bits and return the original success value.
bool BdMenuButton::SetButtonFlag(u32 mask, bool enabled) {
  if (enabled)
    button_flags |= mask;
  else
    button_flags &= ~mask;
  return true;
}

// Return the window interface owned by this button without acquiring another reference.
WnWindow *BdMenuButton::GetWindow() {
  return this;
}

BdWorkspace::BdWorkspace()
    : editor(nullptr), menu_callback(nullptr), field_0ac(nullptr), preview_callback(nullptr), field_0b4(nullptr),
      tool_callback(nullptr), menu_window(nullptr), field_0c0(nullptr), tool_window(nullptr), field_0c8(1),
      field_0cc(0) {
  static const u32 kIds[] = {kWnMsg_ArchitectQueryClose, kWnMsg_ArchitectEnterEditor, kWnMsg_Unresolved24,
      kWnMsg_ArchitectShowMenu, kWnMsg_ArchitectShowMenuAlternate, kWnMsg_ArchitectExit, kWnMsg_ArchitectNewBuilding,
      kWnMsg_ArchitectOpenBuilding, kWnMsg_ArchitectSaveBuilding, kWnMsg_Unresolved26, kWnMsg_Unresolved27,
      kWnMsg_ArchitectBuildingInfo, kWnMsg_Unresolved28, kWnMsg_Unresolved29, kWnMsg_Unresolved25,
      kWnMsg_ArchitectReturnToEditor, kWnMsg_ArchitectReturnToEditorAlternate, kWnMsg_ProcHook};
  for (u32 id : kIds)
    *notifications.Append() = id;
}

// Other workspace interfaces are closed by the separate original shutdown operation.
BdWorkspace::~BdWorkspace() = default;

// Unregister the workspace and release its owned windows, editor, and callbacks in source order.
// An uninitialized workspace returns without touching its retained fields.
bool BdWorkspace::Shutdown() {
  if (!GetFlag(kWnFlagInitialized))
    return true;
  UnregisterNotifications();
  if (field_0c0) {
    field_0c0->Shutdown();
    field_0c0->Release();
  }
  field_0c0 = nullptr;
  if (editor) {
    editor->Shutdown();
    editor->Release();
    editor = nullptr;
  }
  if (tool_window)
    tool_window->Release();
  tool_window = nullptr;
  if (menu_window)
    menu_window->Release();
  menu_window = nullptr;
  if (menu_callback)
    menu_callback->Release();
  menu_callback = nullptr;
  if (preview_callback)
    preview_callback->Release();
  preview_callback = nullptr;
  if (field_0b4)
    field_0b4->Release();
  field_0b4 = nullptr;
  if (tool_callback)
    tool_callback->Release();
  tool_callback = nullptr;
  return WnArchitectRootWidget::Shutdown();
}

// Sample the multimedia clock and advance the shared frame clock by at most its configured interval.
// Preserve the accumulated correction and increment the original frame counter even for a capped sample.
u32 BdFrameTimer::Update() {
  u32 now = static_cast<u32>(SDL_GetTicks()), previous = current, adjusted = now + correction;
  sample = now;
  elapsed = adjusted - previous;
  if (elapsed > limit) {
    elapsed = limit;
    current = limit + previous;
    correction = current - now;
  } else
    current = adjusted;
  ++count;
  return now;
}

// Paint black margins around the centered 800 by 600 architect area when the workspace is wider than 800.
// The real frame timer is updated first; each border reads the live drawing rectangle after the preceding fill.
bool BdWorkspace::GZPaint() {
  BdWorkspaceTimer.Update();
  if (!field_0cc && GetW() > 800) {
    i32 horizontal = (draw_area.right - draw_area.left - 800) / 2;
    i32 vertical = (draw_area.bottom - draw_area.top - 600) / 2;
    if (draw_buffer->Lock(GxLockFlags::Fill)) {
      ScRect rectangle = {draw_area.left, draw_area.top, draw_area.left + horizontal, draw_area.bottom};
      if (rectangle.left < rectangle.right && rectangle.top < rectangle.bottom)
        draw_buffer->Fill(0, rectangle, nullptr);
      rectangle = {draw_area.left + horizontal, draw_area.top, draw_area.right, draw_area.top + vertical};
      if (rectangle.left < rectangle.right && rectangle.top < rectangle.bottom)
        draw_buffer->Fill(0, rectangle, nullptr);
      rectangle = {draw_area.right - horizontal, draw_area.top + vertical, draw_area.right, draw_area.bottom};
      if (rectangle.left < rectangle.right && rectangle.top < rectangle.bottom)
        draw_buffer->Fill(0, rectangle, nullptr);
      rectangle = {
          draw_area.left + horizontal, draw_area.bottom - vertical, draw_area.right - horizontal, draw_area.bottom};
      if (rectangle.left < rectangle.right && rectangle.top < rectangle.bottom)
        draw_buffer->Fill(0, rectangle, nullptr);
      draw_buffer->Unlock(GxLockFlags::Fill);
    }
  }
  return true;
}

// Register this workspace for each original notification in the stored order.
// A missing message server leaves the list untouched and reports failure.
bool BdWorkspace::RegisterNotifications() {
  ScMessageServer *server = BdGetMessageServer();
  if (!server)
    return false;
  for (u32 i = 0; i < notifications.size; ++i)
    server->AddNotification(this, notifications.data[i]);
  return true;
}

// Remove this workspace from the same ordered notification set during shutdown.
// Ignore individual removal results, as the original method does.
bool BdWorkspace::UnregisterNotifications() {
  ScMessageServer *server = BdGetMessageServer();
  if (!server)
    return false;
  for (u32 i = 0; i < notifications.size; ++i)
    server->RemoveNotification(this, notifications.data[i]);
  return true;
}

BdMenuCallback::BdMenuCallback() : references(0) {
}

BdMenuCallback::~BdMenuCallback() {
}

// Publish this callback for the base unknown and the original window-callback interface IDs.
// The base unknown path retains the original requirement for a valid output pointer.
bool BdMenuCallback::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && (iid != IID_UiWindowCallback || !out))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 BdMenuCallback::AddRef() {
  BdGetDirector()->AddRef();
  return ++references;
}

// Release the director before decrementing the callback and deleting it on its final reference.
// Preserve the original nonfinal callback count as the return value.
u32 BdMenuCallback::Release() {
  BdGetDirector()->RemoveRef();
  if (--references)
    return references;
  delete this;
  return 0;
}

// Create the original generic window, configure its menu styles, and install this callback.
// Publish the window view even when installing the callback returns false.
bool BdMenuCallback::BuildWindow(WnWindow **window) {
  UiGenericInterface *generic = nullptr;
  bool result = BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  if (result && generic) {
    generic->SetStyle(kUiGenericNoFrame, 1);
    generic->SetStyle(kUiGenericDraggable, 0);
    generic->SetStyle(kUiGenericCloseButton, 0);
    generic->SetStyle(kUiGenericScroller, 0);
    generic->SetStyle(kUiGenericNoBevel, 1);
    generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
    result = generic->SetCallback(this);
    *window = generic->GetWindow();
  }
  return result;
}

BdToolCallback::BdToolCallback() : references(0), panel(nullptr) {
}

BdToolCallback::~BdToolCallback() {
}

// Expose the actual unknown and window-callback identities, retaining the callback on success.
// The original requires an output pointer for either supported identity.
bool BdToolCallback::QueryInterface(u32 iid, void **out) {
  if ((iid != IID_ScUnknown && iid != IID_UiWindowCallback) || !out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

// Increment the callback count before acquiring the architect director reference.
u32 BdToolCallback::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Drop a director reference only when a callback reference exists, then delete at zero.
u32 BdToolCallback::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Create the toolbox's generic window and apply its distinct style sequence before installing this callback.
// The returned window is published even if callback installation fails.
bool BdToolCallback::BuildWindow(WnWindow **window) {
  UiGenericInterface *generic = nullptr;
  bool result = BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  if (result && generic) {
    generic->SetStyle(kUiGenericNoFrame, 1);
    generic->SetStyle(kUiGenericDraggable, 0);
    generic->SetStyle(kUiGenericCloseButton, 0);
    generic->SetStyle(kUiGenericScroller, 0);
    generic->SetStyle(kUiGenericNoPainting, 1);
    result = generic->SetCallback(this);
    *window = generic->GetWindow();
  }
  return result;
}

// Create the panel for the generic window's build event and accept its close event.
bool BdToolCallback::DoWinMsg(WnWindow *window, u32 message, uptr, uptr, uptr) {
  if (message == kWnMsg_Init)
    return BuildPanel(window);
  if (message == kWnMsg_Shutdown)
    return BdCanCloseWindow(window);
  return 0;
}

BdPreviewCallback::BdPreviewCallback() : references(0) {
}

BdPreviewCallback::~BdPreviewCallback() {
}

// Publish the primary callback interface for the unknown and callback identifiers, then retain it.
// The original requires an output pointer when either supported identifier is requested.
bool BdPreviewCallback::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the callback count before retaining the architect director and rereading the count.
u32 BdPreviewCallback::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Release an existing director hold before decrementing the callback count, and delete at zero.
u32 BdPreviewCallback::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Create the preview's generic window, install its three styles and retain its window view on callback success.
// The original reports success without modifying the output when the class factory itself fails.
u8 BdPreviewCallback::BuildWindow(WnWindow **out) {
  if (!out)
    return 0;
  UiGenericInterface *generic = nullptr;
  u8 result = 1;
  if (BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    generic->SetStyle(kUiGenericDraggable, 1);
    generic->SetStyle(kUiGenericCloseButton, 1);
    generic->SetStyle(kUiGenericScroller, 1);
    result = generic->SetCallback(this);
    if (result) {
      generic->AddRef();
      *out = generic->GetWindow();
    }
    generic->Release();
  }
  return result;
}

u32 BdPreviewGetSelection(WnWindow *window, u32 identifier) {
  WnOptionControl *options = nullptr;
  u32 value = 0;
  if (window->GetChildAs(identifier, IID_WnOptionControl, reinterpret_cast<void **>(&options))) {
    value = options->GetValue();
    options->Release();
  }
  return value;
}

u8 BD_STDCALL BdPreviewEndModal(WnWindow *window, u32 result) {
  window->GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), result);
  return 1;
}

// Cancel the preview for either close command or publish the selected option before accepting it.
u8 BD_STDCALL BdPreviewCommand(WnWindow *window, u32 command) {
  if (command == kUiFrameCloseButtonId || command == kBdPreviewCancelButtonId)
    return BdPreviewEndModal(window, kBdPreviewModalCancelled);
  if (command != kBdPreviewAcceptId)
    return 0;
  u32 value = BdPreviewGetSelection(window, kBdPreviewTileOptionsId);
  UiGenericInterface *generic = nullptr;
  if (value >= kBdPreviewTileOneId && value <= kBdPreviewTileFiveId &&
      window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    generic->SetData(reinterpret_cast<UiWindowData *>(static_cast<uptr>(value)));
    generic->Release();
  }
  return BdPreviewEndModal(window, kBdPreviewModalAccepted);
}

// Compute the preview's minimum dimensions from its present child controls and retain the original 640 by 480 maximum.
u8 BD_STDCALL BdPreviewGetLimits(WnWindow *window, ScRect *limits) {
  limits->left = limits->top = 0;
  limits->right = 640;
  limits->bottom = 480;
  i32 width = 0, height = 0;
  WnWindow *child = window->GetChildWindowFromID(kBdPreviewHeadingId);
  if (child) {
    i32 candidate = (i32)((u32)child->GetW() + 8);
    if (candidate > width)
      width = candidate;
    height = (i32)((u32)child->GetH() + 4);
  }
  child = window->GetChildWindowFromID(kBdPreviewTileOptionsId);
  if (child) {
    i32 candidate = (i32)((u32)child->GetW() + 8);
    if (candidate > width)
      width = candidate;
    height = (i32)((u32)height + (u32)child->GetH() + 4);
  }
  child = window->GetChildWindowFromID(kBdPreviewAcceptId);
  if (child) {
    i32 candidate = (i32)((u32)child->GetW() + 8);
    if (candidate > width)
      width = candidate;
    height = (i32)((u32)height + (u32)child->GetH() + 12);
  }
  if (height)
    height = (i32)((u32)height + 4);
  if (width > limits->left)
    limits->left = width;
  if (height > limits->top)
    limits->top = height;
  return 1;
}

// Position the preview's heading, option selector, and bottom controls within the supplied client rectangle.
// A backdrop can be positioned only when the bottom control supplied its original initialized rectangle.
u8 BD_STDCALL BdPreviewLayout(WnWindow *window, const ScRect *area) {
  i32 heading_height = 0;
  WnWindow *heading = window->GetChildWindowFromID(kBdPreviewHeadingId);
  if (heading) {
    heading->GZWinMoveTo(area->left, (i32)((u32)area->top + 4));
    heading_height = (i32)((u32)heading->GetH() + 4);
  }
  WnWindow *options = window->GetChildWindowFromID(kBdPreviewTileOptionsId);
  if (options) {
    i32 left = area->left, width = options->GetW();
    i32 centered = (i32)((u32)area->right - (u32)left - (u32)width) / 2;
    options->GZWinMoveTo((i32)((u32)centered + (u32)left), (i32)((u32)area->top + (u32)heading_height + 4));
    options->GetH();
  }
  ScRect backdrop;
  WnWindow *bottom = window->GetChildWindowFromID(kBdPreviewAcceptId);
  if (bottom) {
    i32 height = bottom->GetH();
    backdrop = {(i32)((u32)area->left - 5), (i32)((u32)area->bottom - (u32)height - 8), area->right, area->bottom};
    i32 width = bottom->GetW();
    bottom->GZWinMoveTo((i32)((u32)backdrop.right - (u32)width - 4), (i32)((u32)backdrop.top + 4));
  }
  WnWindow *background = window->GetChildWindowFromID(kBdPreviewBackdropId);
  if (background && bottom)
    background->SetArea(backdrop);
  return 1;
}

// Create a preview label through a scoped window-resource provider and attach its window to the supplied parent.
// On successful attachment, optionally return an additional retained label interface to the caller.
u8 BD_STDCALL BdPreviewAddLabel(
    WnWindow *window, i32 x, i32 y, i32 width, i32 height, const ScStringView *text, WnLabelControl **out) {
  WnResources *resources = nullptr;
  BdFrameWork()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&resources));
  u8 added = 0;
  if (resources) {
    u32 identifier = (u32)x + 1000u * (u32)y;
    WnLabelControl *label = resources->CreateLabel(identifier, *text);
    if (label) {
      ScRect rectangle = {x, y, (i32)((u32)x + (u32)width), (i32)((u32)y + (u32)height)};
      FtFont *font = resources->GetFont(kWnFontUiSmall);
      label->GetWindow()->SetFlag(kWnFlagReorderable, false);
      label->SetLabelFlag(kWnLabelWrapText, false);
      label->SetAlignment(kWnLabelLeftTop);
      if (font)
        label->SetFont(font);
      label->GetWindow()->SetArea(rectangle);
      label->GetWindow()->SetSize(width, height);
      added = window->ChildAdd(label->GetWindow());
      if (added && out) {
        label->AddRef();
        *out = label;
      }
      label->Release();
    }
    resources->Release();
  }
  return added;
}

// Create the preview's button through the module's borrowed resource provider and attach its window.
// The original accepts the button after creation even if the parent's child-add operation fails.
u8 BD_STDCALL BdPreviewAddButton(WnWindow *window, u32 identifier, i32 x, i32 y, const ScStringView *text) {
  WnButton *button;
  if (identifier == kBdPreviewAcceptId) {
    GxBuffer *image;
    {
      const GxColorType format(GxColorType::Color::RGB16);
      BdImageResource resource(kRsStandardButtonImageId, kRsUiImageGroup, GxVramRequest::SystemMemory, format);
      image = resource.buffer;
    }
    const ScRect *rectangle = image ? image->GetBufferArea() : nullptr;
    button = BdGetWindowResources()->CreateButton(identifier, *text, image, rectangle, 0);
    if (!button)
      return 0;
    button->SetButtonFlag(kWnButtonShowText, false);
  } else {
    button = BdGetWindowResources()->CreateButton(identifier, *text, nullptr, nullptr, 0);
    if (!button)
      return 0;
    button->SetGutters(11, 6, 18, 8);
    button->Fit();
  }
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  button->GetWindow()->GZWinMoveTo(x, y);
  window->ChildAdd(button->GetWindow());
  button->Release();
  return 1;
}

// Route preview commands and generic-window lifecycle and layout notifications to their original operations.
bool BdPreviewCallback::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return first == 1 ? BdPreviewCommand(window, (u32)second) : 0;
  if (message == kWnMsg_Init)
    return BuildScene(window);
  if (message == kWnMsg_Shutdown)
    return BdCanCloseWindow(window);
  if (message == kWnMsg_ClientConstraints && first)
    return BdPreviewGetLimits(window, reinterpret_cast<ScRect *>(first));
  if (message == kWnMsg_ClientLayout && first)
    return BdPreviewLayout(window, reinterpret_cast<const ScRect *>(first));
  return 0;
}

// Open the preview modally, returning the accepted option or the original cancellation result.
// The created window is shut down and released after the modal manager returns.
i32 BdPreviewCallback::ShowModal(u32 selection, u32 *result) {
  u32 initial = selection >= kBdPreviewTileOneId && selection <= kBdPreviewTileFiveId ? selection : kBdPreviewTileOneId;
  i32 modal = kBdPreviewModalCancelled;
  WnWindow *window = nullptr;
  if (BuildWindow(&window)) {
    UiGenericInterface *generic = nullptr;
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      generic->SetData(reinterpret_cast<UiWindowData *>(static_cast<uptr>(initial)));
      generic->Release();
    }
    modal = window->GetWindowManager()->DoModalWin(window);
    if (modal == kBdPreviewModalAccepted)
      *result = BdPreviewGetSelection(window, kBdPreviewTileOptionsId);
    else {
      modal = kBdPreviewModalCancelled;
      *result = 0;
    }
    window->Shutdown();
    window->Release();
  }
  return modal;
}

// Lazily retain the tool panel and attach its created window at the right edge of the toolbox.
// The original accepts the build event even when the panel window cannot be created.
u8 BdToolCallback::BuildPanel(WnWindow *window) {
  if (!panel)
    panel = ScAddRefNotNull(new BdToolPanel);
  WnWindow *child = nullptr;
  if (panel && panel->BuildWindow(&child)) {
    i32 left = (i32)((u32)window->GetW() - 202), height = window->GetH();
    child->SetID(kBdToolPanelChildId);
    child->SetArea(left, 0, (i32)((u32)left + 202), height);
    child->ShowWindow();
    window->ChildAdd(child);
    child->Release();
  }
  return 1;
}

BdToolGroup::BdToolGroup()
    : word0(0), word1(0), word2(0), word3(0), word4(0), color(0), kind(kBdWorkspaceImageGroup), word7(0) {
}

i32 BdToolGroup::SetColor(u8 red, u8 green, u8 blue) {
  color = ((u32)red << 16) | ((u32)green << 8) | blue;
  return (i32)color;
}

BdToolPanel::BdToolPanel()
    : references(0), groups{}, active_window_id(0), window(nullptr), field_0d4(nullptr), selection(nullptr) {
  groups[1].word0 = kBdConstructGroupButtonId;
  groups[2].word0 = kBdPaintGroupButtonId;
  groups[3].word0 = kBdCompositionGroupButtonId;
  groups[4].word0 = kBdPropGroupButtonId;
  groups[5].word0 = kBdFileGroupButtonId;
  for (u32 i = 1; i <= 4; ++i) {
    groups[i].word1 = kBdToolStripIdBase + i;
    groups[i].word4 = kBdAssetGroupIdBase + i;
    groups[i].word7 = 0x10f + i;
  }
  groups[1].word2 = kBdBlockToolButtonId;
  groups[2].word2 = kBdFillToolButtonId;
  groups[3].word2 = kBdCompositionToolButtonId;
  groups[4].word2 = kBdPropToolButtonId;
  groups[1].word3 = kBdConstructPaletteWindowId;
  groups[2].word3 = kBdMaterialPaletteWindowId;
  groups[3].word3 = kBdCompositionPaletteWindowId;
  groups[4].word3 = kBdPropPaletteWindowId;
  groups[1].SetColor(191, 159, 127);
  groups[2].SetColor(185, 144, 104);
  groups[3].SetColor(194, 133, 151);
  groups[4].SetColor(190, 147, 179);
}

BdToolPanel::~BdToolPanel() {
}

// Return the callback identity for unknown and callback queries, and the separate message-target view for IID 1421.
// Retain the common panel object after publishing either supported interface.
bool BdToolPanel::QueryInterface(u32 iid, void **out) {
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_ScMessageTarget, ScMessageTarget);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the shared object count before retaining the architect director.
u32 BdToolPanel::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Release an existing director hold before decrementing the object count, then delete at zero.
u32 BdToolPanel::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Create the panel's generic window when no live window is recorded, then configure its original flags and callback.
bool BdToolPanel::BuildWindow(WnWindow **out) {
  if (!out || window)
    return false;
  UiGenericInterface *generic = nullptr;
  bool result = BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  if (result && generic) {
    generic->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
    generic->GetWindow()->SetFlag(kWnFlagReorderable, false);
    generic->SetStyle(kUiGenericNoFrame, 1);
    generic->SetStyle(kUiGenericDraggable, 1);
    generic->SetStyle(kUiGenericCloseButton, 0);
    generic->SetStyle(kUiGenericScroller, 0);
    generic->SetStyle(kUiGenericNoPainting, 1);
    generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
    generic->SetCallback(this);
    *out = generic->GetWindow();
  }
  return result;
}

// Dispatch toolbar commands and generic-window build and close events to their original panel methods.
bool BdToolPanel::DoWinMsg(WnWindow *source, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return first == 1 ? Dispatch(source, (u32)second) : 0;
  if (message == kWnMsg_Init)
    return BuildContent(source);
  if (message == kWnMsg_Shutdown)
    return Close(source);
  return 0;
}

// Build the architect toolbar's minimap, background, menu, selection windows and four tool strips.
// Retain the panel window and callbacks, then register both original message subscriptions before creating children.
u8 BdToolPanel::BuildContent(WnWindow *source) {
  ScCom *com = BdGetCom();
  if (!com || !source)
    return 0;
  window = source;
  source->AddRef();
  BdGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectBrushPicked);
  BdGetMessageServer()->AddNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectRebuildToolPanel);
  if (field_0d4)
    return 0;
  field_0d4 = ScAddRefNotNull(new BdMiniMapCallback);
  if (selection)
    return 0;
  selection = ScAddRefNotNull(new BdToolSelection);

  i32 initial_height = source->GetH(), width = source->GetW(), height = source->GetH();
  ScRect minimap_area = {0, height, width, initial_height};
  WnWindow *minimap = nullptr;
  if (field_0d4->BuildWindow(&minimap)) {
    minimap->SetID(kBdMiniMapWindowId);
    minimap->SetFlag(kWnFlagReorderable, false);
    minimap->ShowWindow();
    source->ChildAdd(minimap);
    i32 minimap_width = minimap->GetW();
    i32 x = source->GetW() - minimap_width;
    i32 minimap_height = minimap->GetH();
    i32 y = source->GetH() - minimap_height;
    minimap->GZWinMoveTo(x, y);
    minimap->GetArea(&minimap_area);
    minimap->Release();
  }

  width = source->GetW();
  WnFlatControl *background = nullptr;
  if (com->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&background))) {
    background->GetWindow()->SetFlag(kWnFlagReorderable, false);
    background->GetWindow()->SetArea(0, 0, width, minimap_area.top);
    background->GetWindow()->SetFillColor(192, 192, 250);
    background->SetOutlineFlags(0);
    source->ChildAdd(background->GetWindow());
    background->Release();
  }

  i32 button_y = 8;
  {
    GxColorType format(GxColorType::Color::RGB16);
    BdImageResource image(163, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
    if (image.buffer) {
      WnBitmapControl *bitmap = nullptr;
      if (com->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
        i32 image_width = image.buffer->Width(), image_height = image.buffer->Height();
        bitmap->SetImage(image.buffer);
        bitmap->GetWindow()->SetFlag(kWnFlagReorderable, false);
        i32 image_right = image_width + 8;
        i32 image_bottom = image_height + 8;
        bitmap->GetWindow()->SetArea(8, 8, image_right, image_bottom);
        source->ChildAdd(bitmap->GetWindow());
        button_y = static_cast<i32>(static_cast<u32>(image_bottom) + 4);
        bitmap->Release();
      }
    }
    WnButton *button = nullptr;
    BdToolButtonSpec spec;
    spec.identifier = kBdBuildingsExchangeButtonId;
    spec.text_instance = 5;
    spec.text_group = kRsArchitectSupplementalTextGroup;
    spec.image_instance = 257;
    if (BdCreateToolButton(0, &spec, &button)) {
      button->GetWindow()->GZWinMoveTo(8, button_y);
      source->ChildAdd(button->GetWindow());
      button->Release();
    }
  }

  BdToolMenuView *menu = nullptr;
  BuildMenu(source, &menu);
  if (menu) {
    WnWindow *menu_window = menu->GetWindow();
    menu_window->SetID(kBdToolMenuWindowId);
    i32 menu_width = menu_window->GetW();
    i32 menu_x = source->GetW() - menu_width - 1;
    menu_window->GZWinMoveTo(menu_x, 1);
    i32 menu_top = menu_window->GetT();
    i32 group_top = static_cast<i32>(static_cast<u32>(menu->GetHeadingBottom()) + static_cast<u32>(menu_top));
    ScRect group_area = {0, group_top, source->GetW(), minimap_area.top};
    BuildGroupWindows(source, &group_area);
    menu->Release();
  }

  BdToolButtonSpec buttons[4];
  buttons[0].identifier = kBdEraseToolButtonId;
  buttons[1].identifier = kBdSelectToolButtonId;
  buttons[2].identifier = kBdUndoToolButtonId;
  buttons[3].identifier = kBdRedoToolButtonId;
  for (u32 i = 0; i < 4; ++i)
    buttons[i].text_instance = 160 + i;
  static const u32 order[] = {1, 3, 2, 4};
  static const u32 images[][4] = {{25, 28, 31, 34}, {27, 30, 33, 36}, {26, 29, 32, 35}, {58, 59, 60, 61}};
  i32 span = minimap_area.right - minimap_area.left;
  i32 row_y = minimap_area.top - 25;
  for (u32 i = 0; i < 4; ++i) {
    u32 id = groups[order[i]].word1;
    for (u32 j = 0; j < 4; ++j)
      buttons[j].image_instance = images[i][j];
    BdCreateToolStrip(source, id, buttons, 4);
    WnWindow *strip = source->GetChildWindowFromID(id);
    if (strip) {
      i32 strip_width = strip->GetW();
      i32 difference = span - strip_width;
      i32 x = minimap_area.left + (difference / 2);
      strip->GZWinMoveTo(x, row_y);
    }
  }
  WnWindow *child = source->GetChildWindowFromID(kBdMiniMapWindowId);
  if (child) {
    child->SetFlag(kWnFlagReorderable, true);
    child->PullToFront();
    child->SetFlag(kWnFlagReorderable, false);
  }
  for (i32 i = 0; i < 6; ++i) {
    if (groups[i].word1) {
      child = source->GetChildWindowFromID(groups[i].word1);
      if (child) {
        child->SetFlag(kWnFlagReorderable, true);
        child->PullToFront();
        child->SetFlag(kWnFlagReorderable, false);
      }
    }
  }
  ShowGroupButtons(source, 0);
  return 1;
}

// Acquire an architect command from the framework's command factory and release the temporary factory reference.
static UiCommandInterface *BdAcquireToolCommand(u32 identifier) {
  ScCommandFactory *factory = nullptr;
  UiCommandInterface *command = nullptr;
  BdFrameWork()->GetSystemService(kScCommandServiceId, IID_ScCommandFactory, reinterpret_cast<void **>(&factory));
  if (factory) {
    factory->GetCommand(identifier, IID_UiCommandInterface, reinterpret_cast<void **>(&command));
    factory->Release();
  }
  return command;
}

// Route toolbar selections, group visibility, document operations and command execution through their game services.
// Switching a group invokes the panel's virtual callback with its saved selection after posting the group change.
u8 BdToolPanel::Dispatch(WnWindow *source, u32 command) {
  ScMessageServer *server = BdGetMessageServer();
  i32 group = 0, tool = -1;
  switch (command) {
  case kBdConstructGroupButtonId:
    group = 1;
    break;
  case kBdPaintGroupButtonId:
    group = 2;
    break;
  case kBdCompositionGroupButtonId:
    group = 3;
    break;
  case kBdPropGroupButtonId:
    group = 4;
    break;
  case kBdFileGroupButtonId:
    group = 5;
    break;
  case kBdBlockToolButtonId:
    group = 1;
    tool = 5;
    break;
  case kBdLineToolButtonId:
    group = 1;
    tool = 6;
    break;
  case kBdPlaneToolButtonId:
    group = 1;
    tool = 7;
    break;
  case kBdFillToolButtonId:
    group = 2;
    tool = 9;
    break;
  case kBdPaintbrushToolButtonId:
    group = 2;
    tool = 8;
    break;
  case kBdCompositionToolButtonId:
    group = 3;
    tool = 10;
    break;
  case kBdPropToolButtonId:
    group = 4;
    tool = 11;
    break;
  case kBdUndoToolButtonId:
    tool = 1;
    break;
  case kBdRedoToolButtonId:
    tool = 2;
    break;
  case kBdEraseToolButtonId:
    tool = 3;
    break;
  case kBdSelectToolButtonId:
    tool = 4;
    break;
  default:
    break;
  }
  if (tool >= 0) {
    if (group)
      SetGroupCommand(group, command);
    server->MessagePost(ScMessage(kWnMsg_ArchitectForwardInput, 3, kBdCommandToolMode, tool), false);
    return 1;
  }
  if (group) {
    HideActiveWindow(source);
    if (active_window_id != groups[group].word3) {
      if (group != 5)
        SetActiveWindow(source, groups[group].word3);
      server->MessagePost(ScMessage(kWnMsg_ArchitectForwardInput, 3, kBdCommandSelectionMode, group - 1), false);
      if (group != 5) {
        DoWinMsg(source, kWnMsg_Command, kWnCommandButtonActivated, GetGroupCommand(group), 0);
        ShowGroupButtons(source, group);
        return 1;
      }
      server->MessagePost(ScMessage(kWnMsg_ArchitectForwardInput, 3, kBdCommandToolMode, 0), false);
    }
    SetActiveWindow(source, 0);
    ShowGroupButtons(source, 0);
    return 1;
  }
  if (command == kBdRenderBuildingListButtonId) {
    BdManager *manager = BdGetManager();
    if (!manager)
      return 1;
    ScString path;
    BdGrid *grid;
    if (!manager->GetPath(&path) || !(grid = manager->GetGrid()) || grid->changed) {
      ScString message, title;
      {
        BdTextResource message_resource(
            kRsArchitectSupplementalSaveBeforeRenderText, kRsArchitectSupplementalTextGroup),
            title_resource(kRsArchitectSupplementalUpdateBuildingListText, kRsArchitectSupplementalTextGroup);
        if (message_resource.text && title_resource.text) {
          message = message_resource.GetRawText();
          title = title_resource.GetRawText();
        }
      }
      if (BdGetWindowManager()->GZMsgBox(message, title, 65537, true, 0) != kWnOkButtonId) {
        path.Erase(0, kScStringToEnd);
      } else {
        path.Erase(0, kScStringToEnd);
        if (server->MessageSend(ScMessage(kWnMsg_ArchitectSaveBuilding)) && !manager->GetPath(&path))
          path.Erase(0, kScStringToEnd);
      }
    }
    if (path.Strlen()) {
      UiCommandInterface *action = BdAcquireToolCommand(kBdRenderBuildingListCommandClassId);
      if (action) {
        action->PushBackInput(new ScValue(path));
        action->PushBackInput(new ScValue(1u, kScValueUint32));
        action->Execute();
        action->Release();
      }
    }
    return 1;
  }
  if (command == kBdSaveToolButtonId) {
    BdManager *manager = BdGetManager();
    if (manager) {
      ScString path;
      bool saved = false;
      if (manager->GetPath(&path)) {
        const char *filename = path.Data();
        BdGrid *grid = BdGetCurrentGrid();
        saved = manager->SaveGrid(grid, filename);
      }
      if (!saved)
        server->MessagePost(ScMessage(kWnMsg_ArchitectSaveBuilding), false);
    }
    return 1;
  }
  if (command >= kBdSaveAsToolButtonId && command <= kBdExitToolButtonId) {
    const u32 messages[] = {kWnMsg_ArchitectSaveBuilding, kWnMsg_ArchitectOpenBuilding, kWnMsg_ArchitectNewBuilding,
        kWnMsg_ArchitectQueryClose};
    server->MessagePost(ScMessage(messages[command - kBdSaveAsToolButtonId]), false);
    return 1;
  }
  if (command == kBdBuildingsExchangeButtonId) {
    UiCommandInterface *action = BdAcquireToolCommand(kBdBuildingsExchangeCommandClassId);
    if (action) {
      action->Execute();
      action->Release();
    }
    return 1;
  }
  return 0;
}

// Build the five original architect menu groups, attach the menu and optionally retain its primary interface.
void BdToolPanel::BuildMenu(WnWindow *source, BdToolMenuView **out) {
  if (!source)
    return;
  BdToolButtonSpec spec;
  BdToolMenu *menu = new BdToolMenu;
  menu->AddRef();
  menu->SetRowHeight(42);

  BdToolMenuGroup *group = new BdToolMenuGroup;
  group->AddRef();
  spec.identifier = groups[1].word0;
  spec.image_instance = 1;
  spec.text_instance = 120;
  spec.flags = 1;
  BdAddMenuButton(group, 1, &spec);
  spec.text_instance = 0;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 17;
  BdAddMenuButton(group, 3, &spec);
  spec.identifier = kBdBlockToolButtonId;
  spec.image_instance = 6;
  spec.text_instance = 140;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdLineToolButtonId;
  spec.image_instance = 7;
  spec.text_instance = 141;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdPlaneToolButtonId;
  spec.image_instance = 8;
  spec.text_instance = 142;
  BdAddMenuButton(group, 2, &spec);
  spec.text_instance = 0;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 21;
  BdAddMenuButton(group, 4, &spec);
  menu->AddGroup(group);
  group->Release();

  group = ScAddRefNotNull(new BdToolMenuGroup);
  spec.identifier = groups[2].word0;
  spec.image_instance = 2;
  spec.text_instance = 121;
  spec.flags = 1;
  BdAddMenuButton(group, 1, &spec);
  spec.text_instance = 0;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 18;
  BdAddMenuButton(group, 3, &spec);
  spec.identifier = kBdFillToolButtonId;
  spec.image_instance = 9;
  spec.text_instance = 143;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdPaintbrushToolButtonId;
  spec.image_instance = 10;
  spec.text_instance = 144;
  BdAddMenuButton(group, 2, &spec);
  spec.text_instance = 0;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 22;
  BdAddMenuButton(group, 4, &spec);
  menu->AddGroup(group);
  group->Release();

  group = ScAddRefNotNull(new BdToolMenuGroup);
  spec.identifier = groups[3].word0;
  spec.image_instance = 3;
  spec.text_instance = 52;
  spec.text_group = kRsArchitectSupplementalTextGroup;
  spec.flags = 1;
  BdAddMenuButton(group, 1, &spec);
  spec.text_instance = 0;
  spec.text_group = kRsArchitectTextGroup;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 19;
  BdAddMenuButton(group, 3, &spec);
  spec.identifier = kBdCompositionToolButtonId;
  spec.image_instance = 11;
  spec.text_group = kRsArchitectSupplementalTextGroup;
  spec.text_instance = 53;
  BdAddMenuButton(group, 2, &spec);
  spec.text_instance = 0;
  spec.text_group = kRsArchitectTextGroup;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 23;
  BdAddMenuButton(group, 4, &spec);
  menu->AddGroup(group);
  group->Release();

  group = ScAddRefNotNull(new BdToolMenuGroup);
  spec.identifier = groups[4].word0;
  spec.image_instance = 54;
  spec.text_instance = 4;
  spec.text_group = kRsArchitectSupplementalTextGroup;
  spec.flags = 1;
  BdAddMenuButton(group, 1, &spec);
  spec.text_instance = 0;
  spec.text_group = kRsArchitectTextGroup;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 56;
  BdAddMenuButton(group, 3, &spec);
  spec.identifier = kBdPropToolButtonId;
  spec.image_instance = 55;
  spec.text_instance = 6;
  spec.text_group = kRsArchitectSupplementalTextGroup;
  BdAddMenuButton(group, 2, &spec);
  spec.text_instance = 0;
  spec.text_group = kRsArchitectTextGroup;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 57;
  BdAddMenuButton(group, 4, &spec);
  menu->AddGroup(group);
  group->Release();

  group = ScAddRefNotNull(new BdToolMenuGroup);
  spec.identifier = groups[5].word0;
  spec.image_instance = 4;
  spec.text_instance = 123;
  spec.flags = 1;
  BdAddMenuButton(group, 1, &spec);
  spec.text_instance = 0;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 20;
  BdAddMenuButton(group, 3, &spec);
  spec.identifier = kBdRenderBuildingListButtonId;
  spec.image_instance = 513;
  spec.text_group = kRsArchitectSupplementalTextGroup;
  spec.text_instance = 7;
  BdAddMenuButton(group, 2, &spec);
  spec.text_group = kRsArchitectTextGroup;
  spec.identifier = kBdSaveToolButtonId;
  spec.image_instance = 12;
  spec.text_instance = 146;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdSaveAsToolButtonId;
  spec.image_instance = 13;
  spec.text_instance = 147;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdLoadToolButtonId;
  spec.image_instance = 14;
  spec.text_instance = 148;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdNewToolButtonId;
  spec.image_instance = 15;
  spec.text_instance = 149;
  BdAddMenuButton(group, 2, &spec);
  spec.identifier = kBdExitToolButtonId;
  spec.image_instance = 16;
  spec.text_instance = 150;
  BdAddMenuButton(group, 2, &spec);
  spec.text_instance = 0;
  spec.identifier = kBdToolMenuDecorationId;
  spec.image_instance = 24;
  BdAddMenuButton(group, 4, &spec);
  menu->AddGroup(group);
  group->Release();

  menu->GetWindow()->SetFlag(kWnFlagReorderable, false);
  if (source->ChildAdd(menu->GetWindow()) && out) {
    menu->AddRef();
    *out = menu;
  }
  menu->Release();
}

// Release the panel window and auxiliary interfaces, removing both message subscriptions between those releases.
u8 BdToolPanel::Close(WnWindow *) {
  if (window)
    window->Release();
  window = nullptr;
  BdGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectBrushPicked);
  BdGetMessageServer()->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectRebuildToolPanel);
  if (selection)
    selection->Release();
  selection = nullptr;
  if (field_0d4)
    field_0d4->Release();
  field_0d4 = nullptr;
  return 1;
}

void BdToolPanel::SelectGroup(i32 index) {
  if (!window || index >= 6)
    return;
  WnWindow *container = window->GetChildWindowFromID(kBdToolMenuWindowId);
  if (container) {
    WnButton *button = nullptr;
    if (container->GetChildAs(groups[index].word0, IID_WnButton, reinterpret_cast<void **>(&button))) {
      button->ToggleOn();
      button->Release();
    }
    ScMessage message(kWnMsg_Command, kWnCommandButtonActivated, groups[index].word0, 0);
    container->DoMessage(&message);
  }
}

// Forward tool changes to the real selection object and translate group notifications into button selections.
bool BdToolPanel::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_ArchitectBrushPicked) {
    u32 kind = (u32)message->data1;
    if (kind == 0)
      return selection->Apply(0, (u32)message->data3);
    if (kind <= 3)
      return selection->Apply((u32)message->data2, (u32)message->data3);
    return true;
  }
  if (message->type != kWnMsg_ArchitectRebuildToolPanel || !window)
    return false;
  u32 index = (u32)message->data1;
  if (index <= 4)
    SelectGroup((i32)index + 1);
  return true;
}

BdToolSelection::BdToolSelection() : references(0), group_id(0), first(nullptr), last(nullptr), capacity(nullptr) {
}

BdToolSelection::~BdToolSelection() {
  delete[] first;
}

// Publish the primary callback view for the unknown and callback identities and retain the selection object.
// The original output pointer is required whenever either supported identity is requested.
bool BdToolSelection::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiWindowCallback);
  HANDLE_QUERY_INTERFACE(IID_UiWindowCallback, UiWindowCallback);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the selection count before retaining the architect director, then return the live count.
u32 BdToolSelection::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Drop an existing director reference before decrementing the count, and destroy the selection object at zero.
u32 BdToolSelection::Release() {
  if (references) {
    BdGetDirector()->RemoveRef();
    --references;
  }
  if (!references) {
    delete this;
    return 0;
  }
  return references;
}

// Forward tool icon changes as their two original architect messages and handle the generic-window lifecycle.
bool BdToolSelection::DoWinMsg(WnWindow *source, u32 message, uptr argument, uptr second, uptr) {
  if (message == kWnMsg_Command)
    return argument == 1 || argument == 6;
  if (message == kWnMsg_Init)
    return BuildIcons(source);
  if (message == kWnMsg_Shutdown)
    return Close(source);
  if (message != kWnMsg_ArchitectToolGroupSelectionChanged)
    return 0;
  ScMessageServer *server = BdGetMessageServer();
  u32 tool = (u32)argument, value = (u32)second;
  if (tool == kBdShapeAssetGroupId) {
    ScMessage changed(kWnMsg_ArchitectSetBrush, (u32)(i32)(i8)value, (u32)(i32)(i8)(value >> 8), 0);
    server->MessagePost(changed, false);
  } else if (tool == kBdMaterialAssetGroupId || tool == kBdCompositionAssetGroupId || tool == kBdPropAssetGroupId) {
    ScMessage changed(kWnMsg_ArchitectSetAsset, value, 0, 0);
    server->MessagePost(changed, false);
  } else
    return 0;
  ScMessage select(kWnMsg_ArchitectSetTool, 21, 0, 0);
  server->MessagePost(select, false);
  return 1;
}

// Map the architect selection kind to the original icon category and query each stored tool group in order.
// Stop at the first nonzero raw result, preserving that byte for the caller.
u8 BdToolSelection::Apply(u32 kind, u32 value) {
  u8 category = 0;
  switch (kind) {
  case 0:
  case 7:
  case 10:
    category = 2;
    break;
  case 1:
  case 4:
  case 9:
    category = 1;
    break;
  case 3:
  case 11:
    category = 3;
    break;
  case 6:
  case 12:
    category = 4;
    break;
  case 13:
  case 14:
  case 15:
  case 16:
    category = (u8)(kind - 8);
    break;
  }
  u8 result = 0;
  for (BdToolGroupWindow **item = first; item != last && !result; ++item)
    result = (*item)->SelectIcon(category, value);
  return result;
}

// Remove both tool-selection notifications, release the stored group windows, and empty their pointer sequence.
u8 BdToolSelection::Close(WnWindow *) {
  ScMessageServer *server = BdGetMessageServer();
  server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectToolItemSelected);
  server->RemoveNotification(static_cast<ScMessageTarget *>(this), kWnMsg_ArchitectForwardInput);
  BdToolGroupWindow **end = last;
  for (BdToolGroupWindow **item = first; item != end; ++item)
    if (*item)
      (*item)->Release();
  last = first;
  return 1;
}

u8 BD_STDCALL BdCanCloseWindow(WnWindow *) {
  return 1;
}

// Center a window in its main window, reading the original dimensions in their original order.
// A missing window or main window cannot be positioned and returns zero.
u8 BD_STDCALL BdCenterWindow(WnWindow *window) {
  if (!window)
    return 0;
  WnWindow *main = window->GetMainWindow();
  if (!main)
    return 0;
  i32 main_height = main->GetH(), height = window->GetH();
  i32 main_width = main->GetW(), width = window->GetW();
  window->GZWinMoveTo((main_width - width) / 2, (main_height - height) / 2);
  return 1;
}

// Convert a packed RGB color through the text control's associated window pixel format.
// Release the queried window after conversion and return zero if the interface is unavailable.
u32 BdTextColor(WnLabelControl *control, u32 color) {
  void *value = nullptr;
  u32 pixel = 0;
  if (control->QueryInterface(IID_WnWindow, &value)) {
    WnWindow *window = static_cast<WnWindow *>(value);
    pixel = window->MakeFillColor(static_cast<u8>(color >> 16), static_cast<u8>(color >> 8), static_cast<u8>(color));
    window->Release();
  }
  return pixel;
}

// Create a localized label through the current window-resource provider and attach it to its parent.
// Apply the specified font, alignment and converted colors before positioning the child window.
u8 BD_STDCALL BdMakeLabel(WnWindow *parent, const BdLabelSpec *spec, i32 x, i32 y) {
  WnResources *resources = nullptr;
  BdFrameWork()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&resources));
  if (!resources)
    return 0;
  WnLabelControl *label;
  {
    BdTextResource text(spec->instance, spec->group);
    if (text.text) {
      label = resources->CreateLabel(spec->identifier, *static_cast<ScStringView *>(text.text));
    } else {
      ScString empty;
      label = resources->CreateLabel(spec->identifier, empty);
    }
  }
  u8 added = 0;
  if (label) {
    added = parent->ChildAdd(label->GetWindow());
    if (added) {
      label->SetFont(resources->GetFont(spec->font));
      label->SetLabelFlag(kWnLabelForwardMessages, true);
      label->SetAlignment(spec->alignment);
      label->SetTextColor(0, BdTextColor(label, spec->foreground));
      label->SetTextColor(1, BdTextColor(label, spec->background));
      label->Fit();
      label->SetBackground(false);
      label->GetWindow()->GZWinMoveTo(x, y);
      label->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
    }
    label->Release();
  }
  resources->Release();
  return added;
}

// Create an architect image button, attach it to the parent, and configure its optional image and label.
// Release the temporary button and resource references after the parent has acquired its own child reference.
u8 BD_STDCALL BdMakeMenuButton(WnWindow *parent, const BdButtonSpec *spec, i32 x, i32 y) {
  BdMenuButton *button = new BdMenuButton;
  button->AddRef();
  button->GetWindow()->SetID(spec->identifier);
  u8 added = parent->ChildAdd(button->GetWindow());
  if (added) {
    if (spec->image_instance) {
      const GxColorType format(GxColorType::Color::RGB16);
      BdImageResource resource(spec->image_instance, spec->image_group, GxVramRequest::SystemMemory, format);
      GxBuffer *image = resource.buffer;
      if (image) {
        if (spec->flags & 1)
          image->SetTransparency(image->ConvertRGBValueToNative16Bit(255, 0, 255));
        button->SetImage(image);
      }
    }
    button->Fit();
    button->GetWindow()->GZWinMoveTo(x, y);
    button->GetWindow()->SetFlag(kWnFlagReorderable, false);
    if (spec->label_instance) {
      BdLabelSpec label = {spec->label_id, spec->label_group, spec->label_instance, 4, 16, kBdMenuLabelForegroundColor,
          kBdMenuButtonLabelBackgroundColor};
      if (BdMakeLabel(parent, &label, 0, 0)) {
        WnWindow *child = parent->GetChildWindowFromID(label.identifier);
        if (child) {
          WnWindow *window = button->GetWindow();
          i32 width = button->GetWindow()->GetW();
          i32 centered = (width - child->GetW()) / 2;
          i32 left = (i32)((u32)window->GetL() + (u32)centered);
          i32 top = button->GetWindow()->GetT();
          child->GZWinMoveTo(left, (i32)((u32)spec->label_y + (u32)top));
        }
      }
    }
  }
  button->Release();
  return added;
}

// Translate the menu's actual command IDs into architect notifications and forward lifecycle messages.
// Unrecognized messages and command identifiers leave the server untouched and return zero.
bool BdMenuCallback::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if (static_cast<u32>(first) != 1)
      return 0;
    u32 command = static_cast<u32>(second), result;
    if (command == kUiFrameCloseButtonId || command == kBdMainMenuCloseButtonId)
      result = kWnMsg_ArchitectQueryClose;
    else if (command == kBdMainMenuBuildingInfoButtonId)
      result = kWnMsg_ArchitectBuildingInfo;
    else if (command == kBdMainMenuNewBuildingButtonId)
      result = kWnMsg_ArchitectNewBuilding;
    else if (command == kBdMainMenuOpenBuildingButtonId)
      result = kWnMsg_ArchitectOpenBuilding;
    else if (command == kBdMainMenuExitButtonId)
      result = kWnMsg_ArchitectExit;
    else
      return 0;
    ScMessage posted(result);
    BdGetMessageServer()->MessagePost(posted, false);
    return 1;
  }
  if (message == kWnMsg_Init)
    return BuildMenu(window);
  if (message == kWnMsg_Shutdown)
    return BdCanCloseWindow(window);
  return 0;
}

// Populate the architect's main menu with its original background, image buttons and localized labels.
// The label windows are positioned only when their requested interfaces are present.
u8 BdMenuCallback::BuildMenu(WnWindow *window) {
  window->SetID(kBdMainMenuWindowId);
  window->SetSize(800, 600);
  {
    BdTextResource title(kRsArchitectSupplementalBuildingArchitectPlusText, kRsArchitectSupplementalTextGroup);
    if (title.text)
      window->SetCaption(*static_cast<ScStringView *>(title.text));
    UiGenericInterface *generic = nullptr;
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      const GxColorType format(GxColorType::Color::RGB16);
      BdImageResource background(1, kBdDialogImageGroup, GxVramRequest::SystemMemory, format);
      if (background.buffer) {
        generic->SetImage(1, background.buffer);
        i32 height = background.buffer->Height(), width = background.buffer->Width();
        window->SetSize(width, height);
      }
      generic->Release();
    }
    BdCenterWindow(window);
  }
  BdButtonSpec button = {kBdMainMenuOpenBuildingButtonId, 1, kBdDialogImageGroup, 5, kBdMainMenuOpenBuildingLabelId,
      kRsArchitectTextGroup, 41, 0, 172};
  BdMakeMenuButton(window, &button, 56, 163);
  button.identifier = kBdMainMenuNewBuildingButtonId;
  button.image_instance = 4;
  button.label_id = kBdMainMenuNewBuildingLabelId;
  button.label_instance = 40;
  button.label_y = 137;
  BdMakeMenuButton(window, &button, 291, 295);
  button.identifier = kBdMainMenuCloseButtonId;
  button.image_instance = 6;
  button.label_id = kBdMainMenuCloseLabelId;
  button.label_instance = 44;
  button.label_y = 139;
  BdMakeMenuButton(window, &button, 576, 190);
  button.identifier = kBdMainMenuBuildingInfoButtonId;
  button.image_instance = 2;
  button.label_instance = 0;
  button.label_y = 0;
  BdMakeMenuButton(window, &button, 0, 483);

  BdLabelSpec label = {kBdMainMenuTitleLabelId, kRsArchitectSupplementalTextGroup, 48, 3, 16,
      kBdMenuLabelForegroundColor, kBdMenuTextBackgroundColor};
  BdMakeLabel(window, &label, 0, 0);
  WnLabelControl *control = nullptr;
  if (window->GetChildAs(label.identifier, IID_WnLabelControl, reinterpret_cast<void **>(&control))) {
    control->SetGutters(4, 4);
    control->Fit();
    WnWindow *measure = control->GetWindow(), *target = control->GetWindow();
    i32 width = window->GetW(), label_width = measure->GetW();
    target->GZWinMoveTo((width - label_width) / 2, 104);
    control->Release();
    control = nullptr;
  }
  label = {kBdMainMenuCopyrightLabelId, kRsSupplementalGameTextGroup, 407, 8, 14, kBdMenuLabelForegroundColor,
      kBdMenuTextBackgroundColor};
  BdMakeLabel(window, &label, 0, 0);
  if (window->GetChildAs(label.identifier, IID_WnLabelControl, reinterpret_cast<void **>(&control))) {
    control->SetGutters(4, 4);
    control->Fit();
    WnWindow *height_window = control->GetWindow(), *width_window = control->GetWindow();
    WnWindow *target = control->GetWindow();
    i32 label_height = height_window->GetH(), height = window->GetH();
    i32 width = window->GetW(), label_width = width_window->GetW();
    target->GZWinMoveTo(width - label_width, height - label_height);
    control->Release();
    control = nullptr;
  }
  label = {kBdMainMenuVersionLabelId, kRsArchitectTextGroup, 24, 2, 14, kBdMenuLabelForegroundColor,
      kBdMenuTextBackgroundColor};
  BdMakeLabel(window, &label, 0, 0);
  if (window->GetChildAs(label.identifier, IID_WnLabelControl, reinterpret_cast<void **>(&control))) {
    control->SetGutters(4, 4);
    control->Fit();
    WnWindow *measure = control->GetWindow(), *target = control->GetWindow();
    i32 label_height = measure->GetH(), height = window->GetH();
    target->GZWinMoveTo(0, height - label_height);
    control->Release();
  }
  return 1;
}

BdInfoCallback::BdInfoCallback() : references(0) {
}

BdInfoCallback::~BdInfoCallback() {
}

// Publish the callback and unknown interfaces when the caller supplies an output location.
bool BdInfoCallback::QueryInterface(u32 iid, void **out) {
  if (!out || (iid != IID_ScUnknown && iid != IID_UiWindowCallback))
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiWindowCallback *>(this);
  AddRef();
  return true;
}

u32 BdInfoCallback::AddRef() {
  BdGetDirector()->AddRef();
  return ++references;
}

// Drop a callback reference, deleting the object at zero.
u32 BdInfoCallback::Release() {
  BdGetDirector()->RemoveRef();
  u32 remaining = --references;
  if (!remaining)
    delete this;
  return remaining;
}

// Create the information window and install this callback before returning its window interface.
// The generic object is released after the returned window has acquired the source's retained reference.
u8 BdInfoCallback::BuildWindow(WnWindow **out) {
  UiGenericInterface *generic = nullptr;
  u8 result = BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  if (result && generic) {
    generic->GetWindow()->PrivateBuffer(true);
    generic->SetStyle(kUiGenericNoFrame, 1);
    generic->SetStyle(kUiGenericDraggable, 0);
    generic->SetStyle(kUiGenericCloseButton, 0);
    generic->SetStyle(kUiGenericScroller, 0);
    generic->SetStyle(kUiGenericNoBevel, 1);
    result = generic->SetCallback(this);
    if (result) {
      generic->AddRef();
      *out = generic->GetWindow();
    }
    generic->Release();
  }
  return result;
}

// Dispatch information-window lifecycle and dismissal messages to the actual window manager.
// Modal windows post a modal result; other windows post their parent the original close notification.
bool BdInfoCallback::DoWinMsg(WnWindow *window, u32 message, uptr, uptr, uptr) {
  if (message == kWnMsg_KeyDown)
    return 1;
  if (message == kWnMsg_KeyUp || message == kWnMsg_MouseDownL) {
    if (window->GetFlag(kWnFlagModal)) {
      window->GetWindowManager()->GZWinMsgPost(kWnMsg_EndModal, reinterpret_cast<uptr>(window), 0);
    } else {
      WnWindowManager *manager = window->GetWindowManager();
      manager->PostMessageToTarget(window->GetParentWin(), kWnMsg_ChildDelete, reinterpret_cast<uptr>(window), 0, 0);
    }
    return 1;
  }
  if (message == kWnMsg_Init)
    return BuildContent(window);
  if (message == kWnMsg_Shutdown)
    return BdCanCloseWindow(window);
  return 0;
}

// Build the information window's background, logo, and two columns of localized help entries.
// Missing image or text resources leave their corresponding controls absent.
u8 BdInfoCallback::BuildContent(WnWindow *window) {
  ScRect area = {0, 0, 800, 600};
  UiGenericInterface *generic = nullptr;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    const GxColorType format(GxColorType::Color::RGB16);
    BdImageResource background(512, kBdDialogImageGroup, GxVramRequest::SystemMemory, format);
    if (background.buffer) {
      generic->SetImage(1, background.buffer);
      area.right = background.buffer->Width();
      area.bottom = background.buffer->Height();
    }
    generic->Release();
  }
  window->SetArea(area);
  BdCenterWindow(window);
  {
    const GxColorType format(GxColorType::Color::RGB16);
    BdImageResource image(7, kBdDialogImageGroup, GxVramRequest::SystemMemory, format);
    if (image.buffer) {
      WnBitmapControl *bitmap = nullptr;
      if (BdGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
        WnWindow *child = bitmap->GetWindow();
        BdTextResource text(kRsArchitectSupplementalBuildingArchitectPlusText, kRsArchitectSupplementalTextGroup);
        image.buffer->SetTransparency(image.buffer->ConvertRGBValueToNative16Bit(255, 0, 255));
        child->GZWinMoveTo(20, 28);
        i32 height = image.buffer->Height(), width = image.buffer->Width();
        child->SetSize(width, height);
        bitmap->SetImage(image.buffer);
        window->ChildAdd(child);
        child->Release();
        if (text.text) {
          ScString caption = text.GetRawText();
          WnLabelControl *label = nullptr;
          i32 y = static_cast<i32>(image.buffer->Height() + 28);
          CreateLabel(&label, window, 20, y, 285, 0, &caption, 0, 16);
          if (label)
            label->Release();
        }
      }
    }
  }
  i32 x = 305, y = 126;
  CreateItem(window, &x, &y, 233, 256, 800, 820);
  y = y + 35;
  CreateItem(window, &x, &y, 233, 257, 803, 823);
  y = y + 35;
  CreateItem(window, &x, &y, 233, 258, 805, 825);
  x = 538;
  y = 126;
  CreateItem(window, &x, &y, 233, 259, 801, 821);
  y = y + 35;
  CreateItem(window, &x, &y, 233, 260, 802, 822);
  y = y + 35;
  CreateItem(window, &x, &y, 233, 261, 804, 824);
  y = y + 35;
  CreateItem(window, &x, &y, 233, 262, 806, 826);
  return 1;
}

// Create the shadow and foreground labels for one information-window text item.
// The second label is returned with its factory reference for the caller to release.
void BdInfoCallback::CreateLabel(WnLabelControl **out, WnWindow *parent, i32 x, i32 y, i32 width, i32 height,
    const ScStringView *text, u32 alignment, u32 font) {
  u32 id = static_cast<u32>(x) + 1000u * static_cast<u32>(y);
  *out = BdGetWindowResources()->CreateLabel(id, *text);
  if (*out) {
    WnLabelControl *label = *out;
    label->GetWindow()->SetFlag(kWnFlagReorderable, false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->SetLabelFlag(kWnLabelForwardMessages, true);
    label->SetAlignment(alignment);
    label->SetFont(BdGetWindowResources()->GetFont(font));
    label->GetWindow()->SetW(width);
    label->Fit();
    label->SetTextColor(0, BdGetGraphics()->ConvertRGBValueToNative16Bit(32, 32, 32));
    label->SetTextColor(1, BdGetGraphics()->ConvertRGBValueToNative16Bit(31, 31, 31));
    if (parent) {
      i32 top = y;
      if (alignment == 4)
        top = y - (label->GetWindow()->GetH() / 2) - height + parent->GetH();
      label->GetWindow()->GZWinMoveTo(x + 2, top + 1);
      parent->ChildAdd(label->GetWindow());
    }
    label->Release();
    *out = nullptr;
  }
  *out = BdGetWindowResources()->CreateLabel(id + 1, *text);
  if (*out) {
    WnLabelControl *label = *out;
    label->GetWindow()->SetFlag(kWnFlagReorderable, false);
    label->SetLabelFlag(kWnLabelWrapText, true);
    label->SetLabelFlag(kWnLabelForwardMessages, true);
    label->SetAlignment(alignment);
    label->SetFont(BdGetWindowResources()->GetFont(font));
    label->GetWindow()->SetW(width);
    label->Fit();
    label->SetTextColor(0, BdGetGraphics()->ConvertRGBValueToNative16Bit(212, 122, 72));
    label->SetTextColor(1, BdGetGraphics()->ConvertRGBValueToNative16Bit(61, 61, 61));
    if (parent) {
      i32 top = y;
      if (alignment == 4)
        top = y - (label->GetWindow()->GetH() / 2) - height + parent->GetH();
      label->GetWindow()->GZWinMoveTo(x, top);
      parent->ChildAdd(label->GetWindow());
    }
  }
}

// Attach an item's optional image and its localized heading and detail labels.
// The two text heights advance the caller's vertical position in their original order.
void BdInfoCallback::CreateItem(WnWindow *parent, i32 *x, i32 *y, i32 width, u32 image, u32 heading, u32 detail) {
  const GxColorType format(GxColorType::Color::RGB16);
  BdImageResource picture(image, kBdDialogImageGroup, GxVramRequest::SystemMemory, format);
  BdTextResource title(heading, kRsArchitectTextGroup), description(detail, kRsArchitectTextGroup);
  if (picture.buffer) {
    WnBitmapControl *bitmap = nullptr;
    if (BdGetCom()->GetClassObject(kWnBitmapClassId, IID_WnBitmapControl, reinterpret_cast<void **>(&bitmap))) {
      WnWindow *window = bitmap->GetWindow();
      i32 image_width = picture.buffer->Width();
      window->GZWinMoveTo(*x - image_width - 5, *y);
      i32 image_height = picture.buffer->Height();
      image_width = picture.buffer->Width();
      window->SetSize(image_width, image_height);
      bitmap->SetImage(picture.buffer);
      parent->ChildAdd(window);
      window->Release();
    }
  }
  if (title.text) {
    ScString caption = title.GetRawText();
    WnLabelControl *label = nullptr;
    CreateLabel(&label, parent, *x, *y, width, 0, &caption, 0, 14);
    if (label) {
      *y = *y + label->GetWindow()->GetH();
      label->Release();
    }
  }
  if (description.text) {
    ScString caption = description.GetRawText();
    WnLabelControl *label = nullptr;
    CreateLabel(&label, parent, *x + 15, *y, width - 15, 0, &caption, 0, 15);
    if (label) {
      *y = *y + label->GetWindow()->GetH();
      label->Release();
    }
  }
}

// Find the original toolbox child and retain it for the caller's immediate use.
// A missing parent or child leaves the output untouched.
static u8 BD_STDCALL BdGetToolChild(WnWindow *parent, u32 identifier, WnWindow **out) {
  if (!parent)
    return false;
  WnWindow *child = parent->GetChildWindowFromID(identifier);
  if (!child)
    return false;
  child->AddRef();
  *out = child;
  return true;
}

// Unsupported size identifiers leave the manager and message server untouched.
u8 BdCreateDefaultGrid(i32 size) {
  static const u8 sides[] = {8, 16, 24, 32, 40};
  static const u8 heights[] = {28, 64, 108, 144, 80};
  if (size < 1 || size > 5)
    return 0;
  BdGrid *grid = new BdGrid;
  grid->AddRef();
  grid->SetupStorage(sides[size - 1], heights[size - 1]);
  grid->InitializeGround();
  grid->ResetChanges();
  u8 result = static_cast<u8>(BdGetManager()->SetGrid(grid, 1));
  grid->Release();
  BdGetMessageServer()->MessagePost(ScMessage(kWnMsg_ArchitectForwardInput, 3, kBdCommandSelectionMode, 0), false);
  return result;
}

// Ask about unsaved grid changes and save the current path or open the save dialog when requested.
// A failed dialog creation preserves the source's affirmative result; a zero modal result cancels the operation.
u8 BdWorkspace::ConfirmDiscard() {
  BdGrid *grid = BdGetCurrentGrid();
  if (!grid || !grid->changed)
    return 1;
  ScString title, message;
  {
    BdTextResource resource(kRsArchitectSaveBuildingText, kRsArchitectTextGroup);
    title = resource.GetRawText();
  }
  {
    BdTextResource resource(kRsArchitectConfirmSaveText, kRsArchitectTextGroup);
    message = resource.GetRawText();
  }
  u32 response = GetWindowManager()->GZMsgBox(message, title, 65540, true, 0);
  if (response == kWnNoButtonId)
    return 1;
  if (response != kWnYesButtonId)
    return 0;
  ScString path;
  if (!BdGetManager()->GetPath(&path) || !BdGetManager()->SaveGrid(grid, path.Data())) {
    BdFileDialog *dialog = static_cast<BdFileDialog *>(field_0b4);
    WnWindow *window = nullptr;
    if (dialog->BuildWindow(&window)) {
      dialog->DoWinMsg(window, kWnMsg_Init, kBdFileSaveBuilding, 0, 0);
      u8 result = owner->DoModalWin(window) != 0;
      window->Shutdown();
      window->Release();
      return result;
    }
  }
  return 1;
}

// Initialize the architect workspace, create its callbacks and windows, and attach the editor to the toolbox.
// Existing callbacks and windows are retained; a repeated initialization returns immediately.
bool BdWorkspace::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnArchitectRootWidget::Init();
  GxMode mode = BdGetGraphics()->GetGameMetrics();
  SetFlag(kWnFlagReorderable, false);
  SetArea(0, 0, static_cast<i32>(mode.width), static_cast<i32>(mode.height));
  WnCursor *initial_cursor = GetWindowManager()->GetCursorManager()->GetIGZCursor(14);
  SetCursor(initial_cursor, true);
  if (!menu_callback)
    menu_callback = ScAddRefNotNull(new BdMenuCallback);
  if (!preview_callback)
    preview_callback = ScAddRefNotNull(new BdPreviewCallback);
  if (!field_0b4)
    field_0b4 = ScAddRefNotNull(new BdFileDialog);
  if (!tool_callback)
    tool_callback = ScAddRefNotNull(new BdToolCallback);
  if (!menu_window && menu_callback->BuildWindow(&menu_window)) {
    ChildAdd(menu_window);
    menu_window->ShowWindow();
  }
  i32 editor_right = 0;
  if (!tool_window && tool_callback->BuildWindow(&tool_window)) {
    i32 height = GetH(), width = GetW();
    tool_window->SetArea(0, 0, width, height);
    tool_window->HideWindow();
    ChildAdd(tool_window);
    WnWindow *panel = nullptr;
    if (BdGetToolChild(tool_window, kBdToolPanelChildId, &panel)) {
      editor_right = panel->GetL();
      panel->GetW();
      panel->Release();
    }
  }
  if (!editor) {
    editor = ScAddRefNotNull(new BdEditor);
    editor->SetArea(0, 0, editor_right, GetH());
    if (tool_window)
      tool_window->ChildAdd(editor);
    editor->GZWinMoveTo(0, 0);
  }
  if (tool_callback) {
    WnWindow *panel = nullptr;
    if (BdGetToolChild(tool_window, kBdToolPanelChildId, &panel)) {
      editor->SetToolPanel(panel);
      panel->SetNotificationTarget(editor);
      panel->Release();
    }
  }
  editor->GetWindowManager()->SetFocus(editor);
  RegisterNotifications();
  return true;
}

static u32 BdLastGridSize = kBdPreviewTileTwoId;

// Switch from the architect menu to its toolbox and record the active editing state.
static void BdEnterEditor(BdWorkspace *workspace) {
  workspace->menu_window->HideWindow();
  workspace->tool_window->ShowWindow();
  workspace->field_0cc = 2;
}

// Dispatch architect commands, modal dialogs and native input notifications through their real owners.
// Unrecognized messages retain the root widget's handling and result.
bool BdWorkspace::DoMessage(ScMessage *message) {
  switch (message->type) {
  case kWnMsg_ArchitectQueryClose: {
    u8 can_close = field_0cc == 3 ? BdCanCloseWindow(nullptr) : field_0cc == 2 ? ConfirmDiscard() : 1;
    // The installed exit provider returns null and is immediately dereferenced on the affirmative branch.
    // That original execution is undefined; only the defined close-query effects can be preserved here.
    if (can_close)
      return true;
    return true;
  }
  case kWnMsg_ArchitectEnterEditor: {
    ScString unused;
    if (BdCanCloseWindow(nullptr)) {
      if (BdGetManager()->GetGrid()) {
        BdEnterEditor(this);
      } else {
        ScMessage next(kWnMsg_ArchitectNewBuilding);
        BdGetMessageServer()->MessagePost(next, false);
      }
    }
    return true;
  }
  case kWnMsg_ArchitectShowMenu:
  case kWnMsg_ArchitectShowMenuAlternate:
    field_0cc = 0;
    if (menu_window) {
      menu_window->PullToFront();
      menu_window->ShowWindow();
    }
    return true;
  case kWnMsg_ArchitectNewBuilding: {
    if (!ConfirmDiscard())
      return true;
    u32 selection = 0;
    if (preview_callback->ShowModal(BdLastGridSize, &selection) == kBdPreviewModalCancelled)
      return true;
    i32 size = 1;
    if (selection >= kBdPreviewTileTwoId && selection <= kBdPreviewTileFiveId)
      size = static_cast<i32>(selection - kBdPreviewTileOneId + 1);
    if (selection >= kBdPreviewTileOneId && selection <= kBdPreviewTileFiveId)
      BdLastGridSize = selection;
    BdCreateDefaultGrid(size);
    BdEnterEditor(this);
    BdGetWindowManager()->SetFocus(editor);
    ScMessage next(kWnMsg_ArchitectRebuildToolPanel);
    BdGetMessageServer()->MessagePost(next, false);
    return true;
  }
  case kWnMsg_ArchitectOpenBuilding: {
    if (ConfirmDiscard()) {
      BdFileDialog *dialog = static_cast<BdFileDialog *>(field_0b4);
      WnWindow *window = nullptr;
      bool accepted = true;
      if (dialog->BuildWindow(&window)) {
        dialog->DoWinMsg(window, kWnMsg_Init, kBdFileOpenBuilding, 0, 0);
        accepted = owner->DoModalWin(window) == 1;
        window->Shutdown();
        window->Release();
      }
      if (accepted)
        BdEnterEditor(this);
      BdGetWindowManager()->SetFocus(editor);
    }
    return true;
  }
  case kWnMsg_ArchitectSaveBuilding: {
    BdFileDialog *dialog = static_cast<BdFileDialog *>(field_0b4);
    WnWindow *window = nullptr;
    bool accepted = false;
    if (dialog->BuildWindow(&window)) {
      dialog->DoWinMsg(window, kWnMsg_Init, kBdFileSaveBuilding, 0, 0);
      accepted = owner->DoModalWin(window) == 1;
      window->Shutdown();
      window->Release();
    }
    BdGetWindowManager()->SetFocus(editor);
    return accepted;
  }
  case kWnMsg_ArchitectBuildingInfo: {
    WnWindow *window = nullptr;
    BdInfoCallback *callback = new BdInfoCallback;
    callback->AddRef();
    if (callback->BuildWindow(&window)) {
      HideWindow();
      window->GetWindowManager()->DoModalWin(window);
      ShowWindow();
      window->Shutdown();
      window->Release();
    }
    callback->Release();
    RsManager *resources = nullptr;
    BdFrameWork()->GetSystemService(kRsManagerServiceId, IID_RsManager, reinterpret_cast<void **>(&resources));
    if (resources) {
      resources->ForceGarbageCollection();
      resources->Release();
    }
    BdGetWindowManager()->SetFocus(editor);
    return true;
  }
  case kWnMsg_ArchitectReturnToEditor:
  case kWnMsg_ArchitectReturnToEditorAlternate:
    BdEnterEditor(this);
    return true;
  default:
    return WnArchitectRootWidget::DoMessage(message);
  }
}

// Populate the preview-size dialog with its original backdrop, five choices and localized controls.
// The selected size is restored from the generic window's data when it names an available option.
u8 BdPreviewCallback::BuildScene(WnWindow *window) {
  WnResources *resources = nullptr;
  BdFrameWork()->GetSystemService(kWnResourcesServiceId, IID_WnResources, reinterpret_cast<void **>(&resources));
  if (!resources)
    return 0;
  ScString empty, title;
  u32 selected = kBdPreviewTileOneId;
  {
    BdTextResource resource(kRsArchitectNewBuildingTileSizeText, kRsArchitectTextGroup);
    if (resource.text)
      title = resource.GetRawText();
    window->SetCaption(title);
    UiGenericInterface *generic = nullptr;
    if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
      selected = static_cast<u32>(reinterpret_cast<uptr>(generic->GetData()));
      generic->Release();
      if (selected < kBdPreviewTileOneId || selected > kBdPreviewTileFiveId)
        selected = kBdPreviewTileOneId;
    }
  }
  WnFlatControl *backdrop = nullptr;
  if (BdGetCom()->GetClassObject(kWnFlatRectClassId, IID_WnFlatControl, reinterpret_cast<void **>(&backdrop))) {
    u32 fill = resources->GetColor(kWnThemePanelFillColorId), border = resources->GetColor(kWnThemeBorderColorId);
    backdrop->GetWindow()->SetID(kBdPreviewBackdropId);
    backdrop->GetWindow()->SetFillColor(fill);
    backdrop->SetOutlineColor(static_cast<u16>(border));
    backdrop->SetOutlineFlags(14);
    window->ChildAdd(backdrop->GetWindow());
    backdrop->GetWindow()->PullToFront();
    backdrop->GetWindow()->SetFlag(kWnFlagReorderable, false);
    backdrop->Release();
  }
  BdPreviewAddButton(window, kBdPreviewAcceptId, 0, 0, &empty);
  ScRect area = {0, 0, 16, 16};
  WnOptionControl *options = resources->CreateOptGrp(kBdPreviewTileOptionsId, empty, &area);
  if (options) {
    options->SetButtonFont(resources->GetFont(kWnFontUiSmall));
    for (i32 index = 2; index <= 4; ++index)
      options->SetFontColor(index, BdGetWindowResources()->GetColor(kWnThemeTextColorId));
    options->SetOptionFlag(1, 0);
    options->SetOptionFlag(8, 0);
    for (u32 index = 0; index < 5; ++index) {
      u32 instance = index == 4 ? 9 : 400 + index;
      u32 group = index == 4 ? kRsArchitectSupplementalTextGroup : kRsArchitectTextGroup;
      BdTextResource resource(instance, group);
      ScString caption = resource.GetRawText();
      options->AddButton(kBdPreviewTileOneId + index, caption, 0, nullptr, nullptr);
    }
    window->ChildAdd(options->GetWindow());
    options->SetValue(selected);
    options->GetWindow()->SetSize(1000, 1000);
    options->AutoSize();
    options->Release();
  }
  {
    BdTextResource text(kRsArchitectChooseTileSizeText, kRsArchitectTextGroup);
    if (text.text) {
      WnLabelControl *label = nullptr;
      if (BdPreviewAddLabel(window, 0, 0, 0, 0, static_cast<ScStringView *>(text.text), &label)) {
        label->GetWindow()->SetID(kBdPreviewHeadingId);
        label->Fit();
        label->Release();
      }
    }
  }
  window->SetArea(0, 0, 256, 128);
  BdCenterWindow(window);
  resources->Release();
  return 1;
}
