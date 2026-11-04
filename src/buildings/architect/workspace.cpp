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

// Tool groups retain their default colors and shape-icon identifier prefix; shape edits preserve the lower cell fields.
enum BdToolPresentation : u32 {
  kBdToolGroupDefaultColor = 0xa0a0a0,
  kBdShapeToolBackgroundColor = 0x5f5f7f,
  kBdShapeToolIdentifierPrefix = 0xffeaea00,
  kBdCellWithoutKindOrRotation = 0x3ffffff,
};

// Icon-grid selection uses a command payload; menu labels retain their independent foreground and background colors.
enum BdWorkspaceUiValue : u32 {
  kBdIconSelectionChangedCommand = 0x743ad461,
  kBdMenuLabelForegroundColor = 0xd47a48,
  kBdMenuButtonLabelBackgroundColor = 0x404040,
  kBdMenuTextBackgroundColor = 0x202020,
};

// Installed architect text 5 labels the Buildings Exchange button; dispatch requests its separate command class.
enum BdBuildingsExchangeIdentity : u32 {
  kBdBuildingsExchangeButtonId = 0x65b3d4c8,
  kBdBuildingsExchangeCommandClassId = 0x45c50aff,
};

BdToolMenuGroup::BdToolMenuGroup() : heading(nullptr), first(nullptr), last(nullptr), capacity(nullptr) {
}

// Release the heading and every retained action button before retiring their pointer storage and root window.
BdToolMenuGroup::~BdToolMenuGroup() {
  if (heading)
    heading->Release();
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    if (first[i])
      first[i]->Release();
    first[i] = nullptr;
  }
  delete[] first;
}

bool BdToolMenuGroup::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdToolMenuGroupView, BdToolMenuGroupView);
  return WnArchitectRootWidget::QueryInterface(iid, out);
}

u32 BdToolMenuGroup::AddRef() {
  return WnArchitectRootWidget::AddRef();
}

u32 BdToolMenuGroup::Release() {
  return WnArchitectRootWidget::Release();
}

// Borrow the group's secondary root-window interface without changing its lifetime.
WnWindow *BdToolMenuGroup::GetWindow() {
  return static_cast<WnArchitectRootWidget *>(this);
}

// Release the old heading, publish the replacement and retain it in that order.
u8 BdToolMenuGroup::SetHeading(WnButton *button) {
  if (heading)
    heading->Release();
  heading = button;
  button->AddRef();
  return 1;
}

// Borrow the retained heading button without acquiring another reference.
WnButton *BdToolMenuGroup::GetHeading() {
  return heading;
}

// Retain a non-null action button and append it to the original doubling pointer array.
u8 BdToolMenuGroup::AddButton(WnButton *button) {
  if (!button)
    return 0;
  button->AddRef();
  if (last == capacity) {
    usize count = first ? static_cast<usize>(last - first) : 0;
    usize next = count ? 2 * count : 1;
    WnButton **grown = new WnButton *[next];
    for (usize i = 0; i < count; ++i)
      grown[i] = first[i];
    delete[] first;
    first = grown;
    last = first + count;
    capacity = first + next;
  }
  *last++ = button;
  return 1;
}

i32 BdToolMenuGroup::GetButtonCount() {
  return first ? static_cast<i32>(last - first) : 0;
}

i32 BdToolMenuGroup::GetVisible() {
  return WnArchitectRootWidget::GetFlag(kWnFlagVisible);
}

u32 BdToolMenuGroup::SetVisible(u8 visible) {
  return WnArchitectRootWidget::SetFlag(kWnFlagVisible, visible != 0);
}

// Initialize the root and arrange each retained action window vertically in list order.
// The group width is the maximum child width and its height is the sum of child heights.
bool BdToolMenuGroup::Init() {
  if (!WnArchitectRootWidget::Init())
    return false;
  i32 width = 0, height = 0;
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    WnWindow *child = first[i]->GetWindow();
    ChildAdd(child);
    child->GZWinMoveTo(0, height);
    height = height + child->GetH();
    i32 child_width = child->GetW();
    if (child_width >= width)
      width = child_width;
  }
  SetSize(width, height);
  return true;
}

bool BdToolMenuGroup::Shutdown() {
  if (heading)
    heading->Release();
  heading = nullptr;
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    if (first[i])
      first[i]->Release();
    first[i] = nullptr;
  }
  return WnArchitectRootWidget::Shutdown();
}

// Forward selected keyboard messages to the parent and relay action commands as a fresh parent message.
// Other messages retain the root window's normal dispatch behavior.
bool BdToolMenuGroup::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_Command && message->data1 == 1) {
    WnWindow *target = GetParentWin();
    if (IsWindowValid(target)) {
      ScMessage forwarded(kWnMsg_Command, kWnCommandButtonActivated, message->data2, message->data3);
      target->DoMessage(&forwarded);
    }
    return true;
  }
  if (message->type > kWnMsg_Character && message->type <= kWnMsg_KeyUp && GetParentWin())
    return GetParentWin()->DoMessage(message);
  return WnArchitectRootWidget::DoMessage(message);
}

BdToolMenu::BdToolMenu()
    : active(nullptr), first(nullptr), last(nullptr), capacity(nullptr), group_x(0), heading_bottom(0), indent(0),
      row_height(0) {
}

// Release each retained group before deleting the pointer storage and retiring the root window.
BdToolMenu::~BdToolMenu() {
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    if (first[i])
      first[i]->Release();
    first[i] = nullptr;
  }
  delete[] first;
}

bool BdToolMenu::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_BdToolMenuView, BdToolMenuView);
  return WnArchitectRootWidget::QueryInterface(iid, out);
}

u32 BdToolMenu::AddRef() {
  return WnArchitectRootWidget::AddRef();
}

u32 BdToolMenu::Release() {
  return WnArchitectRootWidget::Release();
}

// Borrow the menu's secondary root-window view without retaining it.
WnWindow *BdToolMenu::GetWindow() {
  return static_cast<WnArchitectRootWidget *>(this);
}

i32 BdToolMenu::GetGroupX() {
  return group_x;
}

i32 BdToolMenu::GetHeadingBottom() {
  return heading_bottom;
}

void BdToolMenu::HideActive() {
  if (active) {
    WnButton *button = active->GetHeading();
    if (button)
      button->ToggleOff();
    active->SetVisible(0);
    active = nullptr;
  }
}

// Borrow the currently active group without changing its reference count.
BdToolMenuGroupView *BdToolMenu::GetActive() {
  return active;
}

// Retain and append a group only when it has a heading, preserving the original doubling storage growth.
u8 BdToolMenu::AddGroup(BdToolMenuGroupView *group) {
  if (!group || !group->GetHeading())
    return 0;
  group->AddRef();
  if (last == capacity) {
    usize count = first ? static_cast<usize>(last - first) : 0;
    usize next = count ? 2 * count : 1;
    BdToolMenuGroupView **grown = new BdToolMenuGroupView *[next];
    for (usize i = 0; i < count; ++i)
      grown[i] = first[i];
    delete[] first;
    first = grown;
    last = first + count;
    capacity = first + next;
  }
  *last++ = group;
  return 1;
}

i32 BdToolMenu::SetIndent(i32 value) {
  indent = value;
  return value;
}

i32 BdToolMenu::SetRowHeight(i32 value) {
  row_height = value;
  return value;
}

// Attach each group's heading and popup, then position and size them using the original two-pass layout.
// The first pass initializes child windows; the second hides popups and records their and the headings' extents.
bool BdToolMenu::Init() {
  if (!WnArchitectRootWidget::Init())
    return false;
  i32 widest_group = 0, width = 0, bottom = 0;
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    WnWindow *heading_window = first[i]->GetHeading()->GetWindow();
    ChildAdd(heading_window);
    i32 heading_width = heading_window->GetW();
    first[i]->GetHeading()->ToggleOff();
    WnWindow *group_window = first[i]->GetWindow();
    ChildAdd(group_window);
    group_window->SetFlag(kWnFlagReorderable, true);
    group_window->PullToFront();
    group_window->SetFlag(kWnFlagReorderable, false);
    if (group_window->GetW() > widest_group)
      widest_group = group_window->GetW();
    u32 combined_bits = static_cast<u32>(group_window->GetW()) - static_cast<u32>(indent);
    i32 combined = static_cast<i32>(combined_bits + static_cast<u32>(heading_width));
    if (combined > width)
      width = combined;
    WnWindow *heading = first[i]->GetHeading()->GetWindow();
    heading->SetFlag(kWnFlagReorderable, true);
    heading->PullToFront();
    heading->SetFlag(kWnFlagReorderable, false);
  }
  group_x = widest_group - indent;
  heading_bottom = 0;
  i32 y = 0;
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    WnWindow *heading_window = first[i]->GetHeading()->GetWindow();
    heading_window->GZWinMoveTo(group_x, y);
    WnWindow *group_window = first[i]->GetWindow();
    group_window->GZWinMoveTo(0, y);
    group_window->SetFlag(kWnFlagVisible, false);
    i32 edge = group_window->GetB();
    if (edge >= bottom)
      bottom = edge;
    edge = heading_window->GetB();
    if (edge >= heading_bottom)
      heading_bottom = edge;
    i32 step = row_height ? row_height : heading_window->GetH();
    y = y + step;
  }
  SetSize(width, bottom);
  return true;
}

// Release and clear the retained groups before shutting down the root window and its children.
bool BdToolMenu::Shutdown() {
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    if (first[i])
      first[i]->Release();
    first[i] = nullptr;
  }
  return WnArchitectRootWidget::Shutdown();
}

// Toggle the selected heading's group and forward the command to the menu's parent when it is a valid window.
u8 BdToolMenu::Select(u32 id) {
  for (usize i = 0; first && i < static_cast<usize>(last - first); ++i) {
    WnButton *heading = first[i]->GetHeading();
    if (heading->GetWindow()->GetID() != id)
      continue;
    if (!heading->IsOn())
      HideActive();
    else {
      HideActive();
      heading->ToggleOn();
      active = first[i];
      active->SetVisible(1);
    }
    break;
  }
  WnWindow *target = GetParentWin();
  if (!IsWindowValid(target))
    return 0;
  ScMessage forwarded(kWnMsg_Command, kWnCommandButtonActivated, id, 0);
  return target->DoMessage(&forwarded);
}

// Process heading commands locally, forward keyboard messages to the parent and delegate other messages to the root.
bool BdToolMenu::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_Command && message->data1 == 1)
    return Select(static_cast<u32>(message->data2));
  if (message->type > kWnMsg_Character && message->type <= kWnMsg_KeyUp && GetParentWin())
    return GetParentWin()->DoMessage(message);
  return WnArchitectRootWidget::DoMessage(message);
}

// Copy the label and retain the item's primary window while keeping the item pointer borrowed.
BdToolGroupEntry::BdToolGroupEntry(BdToolItem *value, const ScStringView &text)
    : item(nullptr), window(nullptr), label(text), flag(0) {
  if (value) {
    window = ScAddRefNotNull(value->GetWindow());
    item = value;
  }
}

// Copy a group entry and acquire its separately retained window reference.
BdToolGroupEntry::BdToolGroupEntry(const BdToolGroupEntry &source)
    : item(nullptr), window(nullptr), label(source.label), flag(0) {
  if (source.window) {
    source.window->AddRef();
    window = source.window;
    item = source.item;
    flag = source.flag;
  }
}

BdToolGroupEntry::~BdToolGroupEntry() {
  if (window)
    window->Release();
}

BdToolGroupWindow::BdToolGroupWindow()
    : group_flags(1), group_color(kBdToolGroupDefaultColor), dark_color(0), light_color(0xffff), image_mode(0),
      group_image(nullptr), margin_left(4), margin_top(4), margin_right(4), margin_bottom(4), tab_gap(4), tab(nullptr) {
}

BdToolGroupWindow::~BdToolGroupWindow() {
  for (u32 i = 0; i < entries.size; ++i)
    delete entries.data[i];
}

bool BdToolGroupWindow::QueryInterface(u32 iid, void **out) {
  if (iid != IID_BdToolGroupControl)
    return WnArchitectRootWidget::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<BdToolGroupControl *>(this);
  AddRef();
  return true;
}

u32 BdToolGroupWindow::AddRef() {
  return WnArchitectRootWidget::AddRef();
}

u32 BdToolGroupWindow::Release() {
  return WnArchitectRootWidget::Release();
}

// Return the borrowed primary window for the secondary group interface.
WnWindow *BdToolGroupWindow::GetWindow() {
  return this;
}

// Create and configure the tab control, repopulate it from existing entries and then lay out the group.
bool BdToolGroupWindow::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnArchitectRootWidget::Init();
  SetFlag(kWnFlagPrivateBuffer, true);
  SetFlag(kWnFlagTransparentBuffer, false);
  SetFlag(kWnFlagClearTransparentBuffer, false);
  SetFlag(kWnFlagHitTestTransparency, false);
  WnComboControl *control = nullptr;
  if (BdGetCom()->GetClassObject(kWnComboClassId, IID_WnComboControl, reinterpret_cast<void **>(&control))) {
    FtFont *font = nullptr;
    if (BdGetWindowResources())
      font = BdGetWindowResources()->GetFont(kWnFontUiFileDialog);
    control->GetWindow()->SetID(kBdToolGroupTabId);
    WnWindow *color_source = control->GetWindow(), *color_target = control->GetWindow();
    u32 white = color_source->MakeFillColor(255, 255, 255);
    color_target->SetFillColor(white);
    control->GetWindow()->HideWindow();
    control->SetEditable(0);
    control->SetFont(font);
    u32 blue = control->GetWindow()->MakeFillColor(0, 0, 255);
    control->SetHighlightColor(blue);
    control->SetSelection(0, true);
    u32 count = entries.size;
    for (u32 i = 0; i < count; ++i)
      control->InsertString(entries.data[i]->label, static_cast<i32>(i));
    if (count)
      control->SetSelection(0, true);
    if (count > 1)
      control->GetWindow()->ShowWindow();
    if (ChildAdd(control->GetWindow()))
      tab = ScAddRefNotNull(control);
    control->Release();
  }
  i32 height = GetH(), width = GetW();
  LayoutItems(width, height);
  SetGroupColor(group_color);
  return true;
}

bool BdToolGroupWindow::Shutdown() {
  if (tab)
    tab->Release();
  tab = nullptr;
  if (group_image)
    group_image->Release();
  group_image = nullptr;
  return WnArchitectRootWidget::Shutdown();
}

// Clamp the window to its margin-derived minimum dimensions and relayout after a size change.
void BdToolGroupWindow::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  i32 minimum_width = margin_left + margin_right;
  i32 minimum_height = margin_top + margin_bottom + tab_gap;
  i32 width = right - left, height = bottom - top;
  if (width < minimum_width)
    width = minimum_width;
  if (height < minimum_height)
    height = minimum_height;
  bool changed = width != GetW() || height != GetH();
  WnArchitectRootWidget::SetArea(left, top, left + width, top + height);
  if (changed)
    LayoutItems(width, height);
}

// Show the selected tab's item and post its window identifier after hiding every group item.
void BdToolGroupWindow::SelectTab(u32 id) {
  if (id != kBdToolGroupTabId)
    return;
  for (u32 i = 0; i < entries.size; ++i)
    if (entries.data[i]->window)
      entries.data[i]->window->HideWindow();
  if (tab) {
    i32 selected = tab->GetSelection();
    if (selected != -1) {
      WnWindow *window = entries.data[selected]->window;
      if (window) {
        window->ShowWindow();
        ScMessage message(kWnMsg_ArchitectToolItemSelected, window->GetID(), 0, 0);
        BdGetMessageServer()->MessagePost(message, false);
      }
    }
  }
}

// Synchronize item selection and notify the target, or handle the tab's selection change.
bool BdToolGroupWindow::DoMessage(ScMessage *message) {
  if (message->type == kWnMsg_Command) {
    if (message->data1 == 6) {
      for (u32 i = 0; i < entries.size; ++i) {
        BdToolItem *item = entries.data[i]->item;
        item->SelectValue(-1);
        item->SelectValue(static_cast<i32>(message->data2));
      }
      SendMsg(notification_target, kWnMsg_Command, kWnCommandListSelectionChanged, message->data2, 0);
      SendMsg(notification_target, kWnMsg_ArchitectToolGroupSelectionChanged, GetID(), message->data2, 0);
    } else if (message->data1 == 11)
      SelectTab(static_cast<u32>(message->data2));
    else
      return false;
    return true;
  }
  if (message->type == kWnMsg_KeyDown || message->type == kWnMsg_KeyUp) {
    if (GetParentWin())
      return GetParentWin()->DoMessage(message);
    return false;
  }
  return WnArchitectRootWidget::DoMessage(message);
}

// Paint the optional fill and background image, then draw the two-tone border around the item area.
bool BdToolGroupWindow::GZPaint() {
  if ((group_flags & 1) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  if ((group_flags & 2) && group_image && draw_buffer->Lock(GxLockFlags::Blt)) {
    if (!image_mode)
      draw_buffer->BltTiled(group_image, *(group_image->GetBufferArea()), draw_area, 0, 0, nullptr);
    else if (image_mode == 1 || image_mode == 2) {
      u32 height = group_image->Height() / 3;
      u32 width = group_image->Width() / 3;
      ScRect slice = {0, 0, static_cast<i32>(width), static_cast<i32>(height)};
      draw_buffer->BltEdge(group_image, slice, draw_area, static_cast<u8>(image_mode == 1 ? 1 : 0));
    }
    draw_buffer->Unlock(GxLockFlags::Blt);
  }
  if (draw_buffer->Lock(GxLockFlags::Line)) {
    i32 left = content_area.left + draw_area.left - 1, top = content_area.top + draw_area.top - 1;
    i32 right = content_area.right + draw_area.left, bottom = content_area.bottom + draw_area.top;
    draw_buffer->DrawLine(left, top, left, bottom, static_cast<i32>(static_cast<u16>(dark_color)), 1);
    draw_buffer->DrawLine(left, top, right, top, static_cast<i32>(static_cast<u16>(dark_color)), 1);
    draw_buffer->DrawLine(right, top, right, bottom, static_cast<i32>(static_cast<u16>(light_color)), 1);
    draw_buffer->DrawLine(left, bottom, right, bottom, static_cast<i32>(static_cast<u16>(light_color)), 1);
    draw_buffer->Unlock(GxLockFlags::Line);
  }
  return true;
}

u8 BdToolGroupWindow::SetGroupFlags(u32 value) {
  group_flags = value;
  return 1;
}

// Scale the three color channels and clamp each result to the byte range.
// Read all three inputs before writing any output, preserving the original behavior when pointers alias.
void BdScaleGroupColor(u8 *red, u8 *green, u8 *blue, f32 scale) {
  f64 first = static_cast<f64>(*red) * scale;
  f32 second = static_cast<f32>(static_cast<f64>(*green) * scale);
  f32 third = static_cast<f32>(static_cast<f64>(*blue) * scale);
  f64 values[] = {first, second, third};
  u8 *channels[] = {red, green, blue};
  for (u32 i = 0; i < 3; ++i)
    if (!(values[i] >= 0.0))
      *channels[i] = 0;
    else if (values[i] > 255.0)
      *channels[i] = 255;
    else
      *channels[i] = static_cast<u8>(values[i]);
}

// Update the group fill and its darker and lighter border colors, including the current tab's edit border.
u8 BdToolGroupWindow::SetGroupColor(u32 value) {
  group_color = value;
  u8 red = static_cast<u8>(value >> 16), green = static_cast<u8>(value >> 8), blue = static_cast<u8>(value);
  SetFillColor(red, green, blue);
  value = group_color;
  red = static_cast<u8>(value >> 16);
  green = static_cast<u8>(value >> 8);
  blue = static_cast<u8>(value);
  BdScaleGroupColor(&red, &green, &blue, 0.75f);
  dark_color = MakeFillColor(red, green, blue);
  value = group_color;
  red = static_cast<u8>(value >> 16);
  green = static_cast<u8>(value >> 8);
  blue = static_cast<u8>(value);
  BdScaleGroupColor(&red, &green, &blue, 1.25f);
  light_color = MakeFillColor(red, green, blue);
  if (tab) {
    WnTextControl *line = tab->GetLineInput();
    line->SetOutlineColor(WnInputEdge::LeftTop, dark_color);
    line->SetOutlineColor(WnInputEdge::RightBottom, light_color);
  }
  return 1;
}

u8 BdToolGroupWindow::SetImageMode(u32 mode) {
  image_mode = mode;
  return 1;
}

// Retain the replacement background image before releasing the previous one.
u8 BdToolGroupWindow::SetGroupImage(GxBuffer *image) {
  if (image)
    image->AddRef();
  if (group_image)
    group_image->Release();
  group_image = image;
  return 1;
}

u8 BdToolGroupWindow::SetMargins(i32 left, i32 top, i32 right, i32 bottom) {
  margin_left = left;
  margin_top = top;
  margin_right = right;
  margin_bottom = bottom;
  i32 height = GetH(), width = GetW();
  LayoutItems(width, height);
  return 1;
}

u8 BdToolGroupWindow::SetTabGap(i32 gap) {
  tab_gap = gap;
  i32 height = GetH(), width = GetW();
  LayoutItems(width, height);
  return 1;
}

// Place the visible tab and give every item window the same remaining content rectangle.
void BdToolGroupWindow::LayoutItems(i32 width, i32 height) {
  i32 top = margin_top;
  if (tab && tab->GetWindow()->IsVisible()) {
    ScRect limits;
    tab->GetMinMaxInfo(&limits);
    tab->GetWindow()->SetArea(margin_left, margin_top, width - margin_right - 1, margin_top + limits.top);
    top = tab_gap + tab->GetWindow()->GetB();
  }
  content_area = {margin_left + 1, top + 1, width - margin_right - 1, height - margin_bottom - 1};
  for (u32 i = 0; i < entries.size; ++i)
    entries.data[i]->window->SetArea(content_area);
}

// Add an item window and its retained entry, then update tab visibility and the group layout.
bool BdToolGroupWindow::AddItem(BdToolItem *item, const ScStringView &label) {
  if (!item || !ChildAdd(item->GetWindow()))
    return false;
  {
    BdToolGroupEntry entry(item, label);
    if (entries.size == entries.capacity) {
      u32 count = entries.size, capacity = count ? count * 2 : 1;
      BdToolGroupEntry **previous = count ? new BdToolGroupEntry *[count] : nullptr;
      for (u32 i = 0; i < count; ++i)
        previous[i] = entries.data[i];
      entries.Reserve(capacity);
      entries.capacity = capacity;
      for (u32 i = 0; i < count; ++i)
        entries.data[i] = new BdToolGroupEntry(*previous[i]);
      *entries.Append() = new BdToolGroupEntry(entry);
      for (u32 i = 0; i < count; ++i)
        delete previous[i];
      delete[] previous;
    } else
      *entries.Append() = new BdToolGroupEntry(entry);
  }
  if (entries.size == 1)
    item->GetWindow()->ShowWindow();
  else
    item->GetWindow()->HideWindow();
  if (tab) {
    tab->InsertString(label, static_cast<i32>(entries.size));
    if (entries.size > 1)
      tab->GetWindow()->ShowWindow();
  }
  i32 height = GetH(), width = GetW();
  LayoutItems(width, height);
  return true;
}

// Select the first item containing the requested icon and hide all other item windows.
u8 BdToolGroupWindow::SelectIcon(u8 category, u32 value) {
  BdToolItem *selected = nullptr;
  u8 result = 0;
  for (u32 i = 0; i < entries.size && !result; ++i) {
    BdToolItem *item = entries.data[i]->item;
    if (item->GetSelection() != static_cast<i32>(value)) {
      result = item->SelectValueVisible(static_cast<i32>(value));
      if (result)
        selected = item;
    }
  }
  if (selected) {
    for (u32 i = 0; i < entries.size; ++i) {
      BdToolItem *item = entries.data[i]->item;
      item->SelectValue(-1);
      item->GetWindow()->HideWindow();
    }
    tab->SetSelection(category, true);
    selected->ShowWindow();
    selected->SelectValueVisible(static_cast<i32>(value));
  }
  return result;
}

// Test whether an entry's retained window has the given identifier.
u8 BdToolGroupWindow::HasItem(u32 id) {
  for (u32 i = 0; i < entries.size; ++i)
    if (entries.data[i]->window && entries.data[i]->window->GetID() == id)
      return 1;
  return 0;
}

// Return the flag of the first entry with the given window identifier.
u8 BdToolGroupWindow::GetItemFlag(u32 id) {
  for (u32 i = 0; i < entries.size; ++i)
    if (entries.data[i]->window && entries.data[i]->window->GetID() == id)
      return entries.data[i]->flag;
  return 0;
}

// Update the first entry with the given window identifier.
void BdToolGroupWindow::SetItemFlag(u32 id, u8 value) {
  for (u32 i = 0; i < entries.size; ++i)
    if (entries.data[i]->window && entries.data[i]->window->GetID() == id) {
      entries.data[i]->flag = value;
      break;
    }
}

// Return and retain the item whose window has the requested identifier, or clear the output on failure.
u8 BdToolGroupWindow::GetItem(u32 id, BdToolItem **out) {
  if (!out)
    return 0;
  *out = nullptr;
  for (u32 i = 0; i < entries.size; ++i) {
    BdToolGroupEntry *entry = entries.data[i];
    if (entry->window && entry->window->GetID() == id) {
      if (entry->item) {
        entry->item->AddRef();
        *out = entry->item;
        return 1;
      }
      break;
    }
  }
  return 0;
}

BdInterpolator::BdInterpolator(f32 initial, f32) : target(initial), speed(1.0f), velocity(1.0f), deadline(0) {
}

// Choose the direction from the starting value and compute the absolute completion time.
i64 BdInterpolator::Rebase(f32 start, u32 now) {
  f64 difference = static_cast<f64>(target) - start;
  f64 signed_speed = difference > 0.0 ? speed : -static_cast<f64>(speed);
  velocity = static_cast<f32>(signed_speed);
  i64 result = static_cast<i64>(difference / signed_speed + static_cast<f64>(now));
  deadline = static_cast<u32>(result);
  return result;
}

f64 BdInterpolator::Get(u32 now) {
  if (now < deadline)
    return target - static_cast<f64>(deadline - now) * velocity;
  return target;
}

// Set a new target and speed magnitude, then rebase its deadline from the supplied starting value.
i64 BdInterpolator::Configure(f32 start, f32 value, f32 rate, u32 now) {
  velocity = rate;
  target = value;
  speed = rate < 0.0f ? -rate : rate;
  return Rebase(start, now);
}

// Initialize the tool's icon list, scroll settings and original dark background.
BdToolItem::BdToolItem()
    : selection(0), max_right(0), max_bottom(0), content_height(0), item_flags(1), background(0), rows(1), scrolling(0),
      scroll_rate(32.0f), scroll_position(0), scroll(0.0f, 0.0f) {
  WnArchitectRootWidget::SetFillColor(WnArchitectRootWidget::MakeFillColor(65, 65, 65));
}

// Release each retained icon interface before the root window destroys its children.
BdToolItem::~BdToolItem() {
  for (u32 i = 0; i < icons.size; ++i)
    if (icons.data[i].button)
      icons.data[i].button->Release();
}

// Initialize the root and create the tool's original vertical scrollbar when the window factory supplies it.
bool BdToolItem::Init() {
  if (GetFlag(kWnFlagInitialized))
    return true;
  WnArchitectRootWidget::Init();
  SetFlag(kWnFlagPrivateBuffer, true);
  SetFlag(kWnFlagTransparentBuffer, false);
  SetFlag(kWnFlagClearTransparentBuffer, false);
  SetFlag(kWnFlagHitTestTransparency, false);
  WnScrollbarControl *scrollbar = nullptr;
  if (BdGetCom()->GetClassObject(kWnScrollbarClassId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar))) {
    const GxColorType format(GxColorType::Color::RGB16);
    BdImageResource image(kBdToolItemScrollbarImageId, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
    scrollbar->GetWindow()->SetID(kBdToolItemScrollbarId);
    scrollbar->GetSlider()->SetDirection(WnSliderDirection::Vertical);
    scrollbar->GetSlider()->SetImage(image.buffer, nullptr);
    scrollbar->GetSlider()->SetValue(0);
    ChildAdd(scrollbar->GetWindow());
    scrollbar->Release();
  }
  i32 height = GetH(), width = GetW();
  LayoutIcons(width, height);
  return true;
}

// Release the retained icon entries before the root window shuts down its children.
bool BdToolItem::Shutdown() {
  for (u32 i = 0; i < icons.size; ++i)
    if (icons.data[i].button)
      icons.data[i].button->Release();
  icons.Clear();
  return WnArchitectRootWidget::Shutdown();
}

// Respect the scrollbar's minimum dimensions before applying the new root area and updating its layout.
void BdToolItem::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  ScRect minimum{};
  WnScrollbarControl *scrollbar = nullptr;
  GetChildAs(kBdToolItemScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar));
  if (scrollbar)
    scrollbar->GetExtent(&minimum);
  i32 width = right - left;
  i32 height = bottom - top;
  if (width < minimum.left)
    width = minimum.left;
  if (height < minimum.top)
    height = minimum.top;
  WnArchitectRootWidget::SetArea(left, top, left + width, top + height);
  LayoutIcons(width, height);
  if (scrollbar)
    scrollbar->Release();
}

void BdToolItem::StartScroll(i32, u32 target) {
  f64 precise_slope = (550.0 - static_cast<f64>(0.0025f)) / (static_cast<f64>(scroll_rate) - 1.0);
  f32 slope = static_cast<f32>(precise_slope), offset = static_cast<f32>(0.0025f - precise_slope);
  u32 now = BdWorkspaceTimer.current;
  f32 start;
  if (scrolling)
    start = static_cast<f32>(scroll.Get(now));
  else {
    start = static_cast<f32>(static_cast<u32>(scroll_position));
    scrolling = 1;
  }
  f32 destination = static_cast<f32>(target);
  f64 speed = fabs(static_cast<f64>(start) - destination) * slope + offset;
  if (speed < 0.0025f)
    speed = 0.0025f;
  else if (speed > 550.0f)
    speed = 550.0f;
  scroll.Configure(start, destination, static_cast<f32>(speed * 0.001f), now);
  InvalidateSelfAndParents();
}

// Advance animated icon positions and paint the tool background through the current drawing buffer.
bool BdToolItem::GZPaint() {
  if (scrolling) {
    u32 now = BdWorkspaceTimer.current;
    i32 offset = static_cast<i32>(static_cast<i64>(scroll.Get(now) + 0.5));
    for (u32 i = 0; i < icons.size; ++i) {
      BdToolIconEntry *entry = &icons.data[i];
      if (entry->button)
        entry->button->GetWindow()->GZWinMoveTo(entry->x, entry->y - offset);
    }
    if (now >= scroll.deadline)
      scrolling = 0;
    InvalidateSelfAndParents();
  }
  if ((item_flags & 1) && draw_buffer->Lock(GxLockFlags::Fill)) {
    u32 color = GetFillColor();
    draw_buffer->Fill(color, draw_area, nullptr);
    draw_buffer->Unlock(GxLockFlags::Fill);
  }
  return true;
}

// Forward icon activation to the parent and animate changes from this item's scrollbar.
bool BdToolItem::OnCommand(u32 kind, u32 command) {
  if (kind == kWnCommandButtonActivated)
    return SendMsg(GetParentWin(), kWnMsg_Command, kWnCommandListSelectionChanged, command, 0);
  if (kind != kWnCommandSliderValueChanged)
    return WnArchitectRootWidget::OnCommand(kind, command);
  if (command != kBdToolItemScrollbarId)
    return false;
  WnScrollbarControl *scrollbar = nullptr;
  GetChildAs(kBdToolItemScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar));
  if (scrollbar) {
    StartScroll(0, static_cast<u32>(scrollbar->GetValue()));
    scroll_position = scrollbar->GetValue();
    scrollbar->Release();
  }
  return true;
}

// Return the item's primary window interface without acquiring a reference.
WnWindow *BdToolItem::GetWindow() {
  return this;
}

// Show and size the existing scrollbar only when icon content exceeds the visible height.
// Keep the original range, page count and line count update order.
void BdToolItem::LayoutIcons(i32 width, i32 height) {
  WnScrollbarControl *scrollbar = nullptr;
  GetChildAs(kBdToolItemScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar));
  if (scrollbar) {
    i32 bottom = max_bottom;
    scrollbar->GetWindow()->SetFlag(kWnFlagVisible, bottom > height);
    if (bottom > height) {
      i32 range = bottom - height + 4;
      scrollbar->GetWindow()->SetSize(1, height);
      WnWindow *window = scrollbar->GetWindow();
      i32 scrollbar_width = scrollbar->GetWindow()->GetW();
      window->GZWinMoveTo(width - scrollbar_width, 0);
      scrollbar->SetRange(0, range);
      scrollbar->SetPageCount(height / 2);
      scrollbar->SetLineCount(rows);
    }
    scrollbar->Release();
  }
}

// Add and retain a positioned button, update the content extents, and refresh the scrollbar and window.
void BdToolItem::AddIcon(WnButton *button, i32 x, i32 y) {
  button->GetWindow()->GZWinMoveTo(x, y);
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  ChildAdd(button->GetWindow());
  button->AddRef();
  WnWindow *window = button->GetWindow();
  BdToolIconEntry entry{button, window->GetL(), button->GetWindow()->GetT()};
  button->AddRef();
  *icons.Append() = entry;
  i32 right = button->GetWindow()->GetR();
  if (right > max_right)
    max_right = button->GetWindow()->GetR();
  i32 bottom = button->GetWindow()->GetB();
  if (bottom > max_bottom)
    max_bottom = button->GetWindow()->GetB();
  button->Release();
  i32 height = GetH(), width = GetW();
  LayoutIcons(width, height);
  WnScrollbarControl *scrollbar = nullptr;
  GetChildAs(kBdToolItemScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar));
  if (scrollbar) {
    scrollbar->GetWindow()->SetFlag(kWnFlagReorderable, true);
    scrollbar->GetWindow()->PullToFront();
    scrollbar->GetWindow()->SetFlag(kWnFlagReorderable, false);
  }
  InvalidateSelfAndParents();
  if (scrollbar)
    scrollbar->Release();
}

// Clear the selected state of every retained icon without changing the selection identifier.
void BdToolItem::ClearSelection() {
  for (u32 i = 0; i < icons.size; ++i)
    if (icons.data[i].button)
      icons.data[i].button->SetButtonFlag(kWnButtonChecked, false);
}

// Remove every icon child and its retained button, then refresh the scrollbar and item window.
void BdToolItem::ClearIcons() {
  ClearSelection();
  for (u32 i = 0; i < icons.size; ++i)
    ChildDeleteAbsolute(icons.data[i].button->GetWindow());
  for (u32 i = 0; i < icons.size; ++i)
    if (icons.data[i].button)
      icons.data[i].button->Release();
  icons.Clear();
  PullToFront();
  i32 height = GetH(), width = GetW();
  LayoutIcons(width, height);
  InvalidateSelfAndParents();
}

// Apply the shared selection effects, optionally scrolling the selected icon into view.
// Notify the parent only when the stored identifier changes, including when clearing to minus one.
u8 BdToolItem::Select(i32 value, bool reveal) {
  i32 previous = selection;
  u8 result = 0;
  if (value == -1) {
    selection = -1;
    ClearSelection();
    InvalidateSelfAndParents();
  } else {
    BdToolIconEntry entry{};
    for (u32 i = 0; i < icons.size; ++i) {
      WnButton *button = icons.data[i].button;
      u32 icon_id = button ? button->GetWindow()->GetID() : 0;
      if (icon_id == static_cast<u32>(value)) {
        entry = icons.data[i];
        if (entry.button)
          entry.button->AddRef();
        selection = value;
        ClearSelection();
        if (entry.button)
          entry.button->SetButtonFlag(kWnButtonChecked, true);
        if (reveal) {
          WnScrollbarControl *scrollbar = nullptr;
          GetChildAs(kBdToolItemScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar));
          if (scrollbar) {
            scrollbar->SetValue(entry.y > 4 ? entry.y - 4 : 0);
            scrollbar->Release();
          }
        }
        result = 1;
        if (entry.button)
          entry.button->Release();
        break;
      }
    }
  }
  if (previous != selection) {
    SendMsg(GetParentWin(), kWnMsg_Command, kBdIconSelectionChangedCommand, static_cast<u16>(selection), 0);
    InvalidateSelfAndParents();
  }
  return result;
}

u8 BdToolItem::SelectValue(i32 value) {
  return Select(value, false);
}

u8 BdToolItem::SelectValueVisible(i32 value) {
  return Select(value, true);
}

i32 BdToolItem::GetSelection() {
  return selection;
}

void BdToolItem::SetRows(i32 count) {
  if (count < 0)
    count = 1;
  WnScrollbarControl *scrollbar = nullptr;
  GetChildAs(kBdToolItemScrollbarId, IID_WnScrollbarControl, reinterpret_cast<void **>(&scrollbar));
  if (scrollbar)
    scrollbar->SetLineCount(count);
  rows = count;
  if (scrollbar)
    scrollbar->Release();
}

i32 BdToolItem::SetContentHeight(i32 height) {
  content_height = height;
  return height;
}

i32 BdToolItem::GetContentHeight() {
  return content_height;
}

i32 BdToolItem::GetIconCount() {
  return static_cast<i32>(icons.size);
}

// Store the RGB color and convert it to the window's current fill format.
u8 BdToolItem::SetBackground(u32 color) {
  background = color;
  u32 pixel = MakeFillColor(static_cast<u8>(color >> 16), static_cast<u8>(color >> 8), static_cast<u8>(color));
  SetFillColor(pixel);
  return 1;
}

u32 BdToolItem::GetBackground() {
  return background;
}

u8 BD_STDCALL BdSetChildVisible(WnWindow *window, u32 identifier, u8 visible) {
  if (!window)
    return 0;
  WnWindow *child = window->GetChildWindowFromID(identifier);
  if (!child)
    return 0;
  child->SetFlag(kWnFlagVisible, visible != 0);
  return 1;
}

// Store the most recent command for a valid tool-group descriptor.
u8 BdToolPanel::SetGroupCommand(i32 index, u32 command) {
  if (static_cast<u32>(index) >= 6)
    return 0;
  groups[index].word2 = command;
  return 1;
}

u32 BdToolPanel::GetGroupCommand(i32 index) {
  return static_cast<u32>(index) < 6 ? groups[index].word2 : 0;
}

u8 BdToolPanel::HideActiveWindow(WnWindow *source) {
  if (active_window_id)
    return BdSetChildVisible(source, active_window_id, 0);
  return 1;
}

// Store the active content identifier and show the corresponding child when it is nonzero.
u8 BdToolPanel::SetActiveWindow(WnWindow *source, u32 id) {
  active_window_id = id;
  if (id)
    return BdSetChildVisible(source, id, 1);
  return 1;
}

// Show only the selected group's auxiliary button window among all six descriptors.
void BdToolPanel::ShowGroupButtons(WnWindow *source, i32 index) {
  for (i32 i = 0; i < 6; ++i)
    if (groups[i].word1)
      BdSetChildVisible(source, groups[i].word1, static_cast<u8>(index == i));
}

BdMiniMapCallback::BdMiniMapCallback() : references(0), map_window(nullptr), initialized(0) {
}

BdMiniMapCallback::~BdMiniMapCallback() {
  if (map_window)
    map_window->Release();
  map_window = nullptr;
}

// Publish the callback or message-target view for the original identities and retain the shared object.
bool BdMiniMapCallback::QueryInterface(u32 iid, void **out) {
  if (iid != IID_ScUnknown && iid != IID_ScMessageTarget && iid != IID_UiWindowCallback)
    RETURN_QUERY_INTERFACE_FAILURE();
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = iid == IID_ScMessageTarget ? static_cast<void *>(static_cast<ScMessageTarget *>(this))
                                    : static_cast<void *>(static_cast<UiWindowCallback *>(this));
  AddRef();
  return true;
}

// Retain the shared callback and architect director in the original order.
u32 BdMiniMapCallback::AddRef() {
  ++references;
  BdGetDirector()->AddRef();
  return references;
}

// Drop the director hold before deleting the callback at its final reference.
u32 BdMiniMapCallback::Release() {
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

BdMiniMapWindow::BdMiniMapWindow()
    : grid(nullptr), renderer(nullptr), catalog(nullptr), image(nullptr), overlay(nullptr), surface(nullptr),
      scene(nullptr), scale(1.0f), bounds{}, selection_bounds{}, selected{}, view_mode(2), invalid_flags(2),
      interaction_flags(0), zoom_factor(1.0f), drag_offset{} {
}

BdMiniMapWindow::~BdMiniMapWindow() {
}

// Initialize the minimap's scene buffers and renderer, then retain its background image when available.
// The neutral initial scale avoids the original uninitialized read before a grid has configured the renderer.
bool BdMiniMapWindow::Init() {
  if (GetInitFlag())
    return true;
  WnArchitectRootWidget::Init();
  BdGetGraphics()->CreateBuffer(&scene);
  BdGetGraphics()->CreateBuffer(&surface);
  BdGetGraphics()->CreateBuffer(&overlay);
  GxColorType current = BdGetGraphics()->GetScreenColorType();
  if (scene->Init(
          171, 684, current, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
    colors[0] = scene->ConvertRGBValueToNative16Bit(255, 0, 255);
    scene->SetTransparency(colors[0]);
    if (scene->Lock(GxLockFlags::Fill)) {
      scene->Fill(colors[0], nullptr);
      scene->Unlock(GxLockFlags::Fill);
    }
    current = BdGetGraphics()->GetScreenColorType();
    if (surface->Init(
            171, 171, current, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false)) {
      canvas_rectangle = {0, 0, 171, 171};
      current = BdGetGraphics()->GetScreenColorType();
      overlay->Init(
          171, 171, current, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
      colors[2] = overlay->ConvertRGBValueToNative16Bit(191, 191, 0);
      colors[1] = overlay->ConvertRGBValueToNative16Bit(127, 127, 0);
    }
  }
  if (!renderer)
    renderer = new BdRenderer;
  u32 half_height = scene->Height() >> 1;
  offset.x = static_cast<i32>((scene->Width() >> 1) - 1);
  offset.y = static_cast<i32>(half_height);
  renderer->SetOffset(&offset);
  renderer->SetBuffer(scene);
  renderer->SetCatalog(catalog);
  renderer->SetScale(scale);
  {
    const GxColorType image_format(GxColorType::Color::RGB16);
    BdImageResource background(176, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, image_format);
    if (background.buffer)
      image = ScAddRefNotNull(background.buffer);
  }
  if (renderer && grid) {
    half_height = scene->Height() >> 1;
    offset.x = static_cast<i32>((scene->Width() >> 1) - 1);
    offset.y = static_cast<i32>(half_height);
    renderer->SetOffset(&offset);
  }
  Invalidate(1);
  return true;
}

bool BdMiniMapWindow::Shutdown() {
  if (renderer) {
    renderer->SetBuffer(nullptr);
    delete renderer;
    renderer = nullptr;
  }
  ScRelease(&image);
  ScRelease(&grid);
  catalog = nullptr;
  ScRelease(&overlay);
  ScRelease(&surface);
  ScRelease(&scene);
  return WnArchitectRootWidget::Shutdown();
}

// Render the minimap scene, redraw its interaction overlay, and copy the result into the window buffer.
bool BdMiniMapWindow::GZPaint() {
  if (invalid_flags & 1) {
    ScRect image_area = *image->GetBufferArea();
    if (overlay->Lock(GxLockFlags::Blt)) {
      overlay->Blt(image, image_area, image_area, nullptr);
      overlay->Unlock(GxLockFlags::Blt);
    }
    if (scene->Lock(GxLockFlags::Fill)) {
      u16 key = scene->ConvertRGBValueToNative16Bit(255, 0, 255);
      scene->Fill(key, nullptr);
      scene->Unlock(GxLockFlags::Fill);
    }
    RenderScene();
    f64 vertical = 171.0 / (scene_rectangle.bottom - scene_rectangle.top);
    f32 horizontal = static_cast<f32>(171.0 / (scene_rectangle.right - scene_rectangle.left));
    if (vertical > horizontal)
      vertical = horizontal;
    zoom_factor = static_cast<f32>(vertical);
    GxBuffer *scaled = BdScalePalettedBicubic(zoom_factor, scene, &scene_rectangle, nullptr, nullptr);
    if (scaled) {
      scaled->SetTransparency(colors[0]);
      ScRect source = *scaled->GetBufferArea();
      ScRect destination = {
          canvas_rectangle.left, canvas_rectangle.top, canvas_rectangle.right, canvas_rectangle.bottom + view_offset};
      i32 source_width = source.right - source.left;
      if (source_width < destination.right - destination.left) {
        i32 half_width = (canvas_rectangle.right - canvas_rectangle.left) / 2;
        destination.left = static_cast<i32>(static_cast<i64>(half_width - source_width * 0.5 + 0.5));
        destination.right = static_cast<i32>(static_cast<i64>(half_width + (source_width + 1.0) * 0.5));
      }
      if (source.bottom - source.top < destination.bottom - destination.top)
        destination.top = destination.bottom + source.top - source.bottom;
      i32 destination_width = destination.right - destination.left;
      if (source_width < destination_width) {
        destination.left = source.left + destination.right - source.right;
        destination_width = source.right - source.left;
      }
      i32 half_width = destination_width / 2;
      scene_center.x = half_width + destination.left;
      scene_center.y = destination.bottom - half_width;
      if (overlay->Lock(GxLockFlags::Blt)) {
        overlay->Blt(scaled, source, destination, nullptr);
        overlay->Unlock(GxLockFlags::Blt);
      }
      scaled->Release();
    }
    RefreshSurface();
    invalid_flags = (invalid_flags & ~7u) | 6;
  }
  if (invalid_flags & 2) {
    if (overlay->Lock(GxLockFlags::Blt)) {
      ScRect surface_area = *surface->GetBufferArea();
      overlay->Blt(surface, surface_area, surface_area, nullptr);
      overlay->Unlock(GxLockFlags::Blt);
    }
    DrawGizmo();
    invalid_flags = (invalid_flags & ~6u) | 4;
  }
  if (invalid_flags & 4) {
    ScRect overlay_area = *overlay->GetBufferArea();
    if (draw_buffer->Lock(GxLockFlags::Blt)) {
      draw_buffer->Blt(overlay, overlay_area, draw_area, nullptr);
      draw_buffer->Unlock(GxLockFlags::Blt);
    }
    invalid_flags |= 4;
  }
  return true;
}

// Capture the mouse and begin a minimap drag, invalidating the viewport when pressed inside it.
bool BdMiniMapWindow::OnMouseDownL(i32 x, i32 y, u32 modifiers) {
  BdRect hit{};
  if (selection_mode >= 1 && selection_mode <= 4)
    hit = viewport;
  drag_origin = {x, y};
  GZSetCapture();
  interaction_flags |= 1;
  if (x >= hit.left && y >= hit.top && x <= hit.right && y <= hit.bottom)
    Invalidate(2);
  return WnArchitectRootWidget::OnMouseDownL(x, y, modifiers);
}

// Track a captured drag and update the offset used to redraw the minimap viewport.
bool BdMiniMapWindow::OnMouseMove(i32 x, i32 y, u32 modifiers) {
  BdRect hit{};
  if (selection_mode >= 1 && selection_mode <= 4)
    hit = viewport;
  if ((interaction_flags & 1) && x >= hit.left && y >= hit.top && x <= hit.right && y <= hit.bottom)
    interaction_flags |= 2;
  if (interaction_flags & 2) {
    drag_offset.x = x - static_cast<i32>(static_cast<i64>(drag_origin.x * static_cast<f64>(zoom_factor)));
    drag_offset.y = y - static_cast<i32>(static_cast<i64>(drag_origin.y * static_cast<f64>(zoom_factor)));
    Invalidate(2);
  }
  return WnArchitectRootWidget::OnMouseMove(x, y, modifiers);
}

// Finish a minimap drag or click by posting the corresponding camera movement.
bool BdMiniMapWindow::OnMouseUpL(i32 x, i32 y, u32 modifiers) {
  if (interaction_flags & 1) {
    if (selection_mode) {
      ScRect scene_area = *scene->GetBufferArea();
      i32 projected_x = static_cast<i32>(
          static_cast<i64>(static_cast<f64>(GetW()) * offset.x / (scene_area.right - scene_area.left)));
      i32 projected_y = static_cast<i32>(
          static_cast<i64>(static_cast<f64>(GetH()) * offset.y / (scene_area.bottom - scene_area.top)));
      f32 width = static_cast<f32>(GetW());
      i32 height = GetH();
      f64 inverse_zoom = 1.0 / zoom_factor;
      i32 target_x = kBdPanMessageCoordinateBias -
          static_cast<i32>(
              static_cast<i64>((drag_origin.x - projected_x) / static_cast<f64>(width) * inverse_zoom * -100000.0));
      i32 target_y = kBdPanMessageCoordinateBias -
          static_cast<i32>(
              static_cast<i64>((drag_origin.y - projected_y) / static_cast<f64>(height) * inverse_zoom * -100000.0));
      ScMessage message(kWnMsg_ArchitectPanView, static_cast<u32>(target_x), static_cast<u32>(target_y), 0);
      BdGetMessageServer()->MessagePost(message, false);
    }
    interaction_flags &= ~3u;
    drag_offset = {0, 0};
    GZReleaseCapture();
  } else {
    if (selection_mode) {
      i32 target_x = (viewport.right - viewport.left) / 2 - x + viewport.left + 1000;
      i32 target_y = (viewport.bottom - viewport.top) / 2 - y + viewport.top + 1000;
      ScMessage message(kWnMsg_ArchitectPanView, static_cast<u32>(target_x), static_cast<u32>(target_y), 0);
      BdGetMessageServer()->MessagePost(message, false);
    }
    drag_offset = {0, 0};
  }
  return WnArchitectRootWidget::OnMouseUpL(x, y, modifiers);
}

bool BdMiniMapWindow::GZOnMouseWheel(i32 x, i32 y, u32 modifiers, i32 wheel) {
  return WnArchitectRootWidget::GZOnMouseWheel(x, y, modifiers, wheel);
}

// End an active minimap interaction and invalidate the scene before forwarding the event.
bool BdMiniMapWindow::GZOnMouseExit(WnWindow *window) {
  if (interaction_flags & 1) {
    interaction_flags &= ~3u;
    Invalidate(1);
  }
  return WnArchitectRootWidget::GZOnMouseExit(window);
}

void BdMiniMapWindow::Invalidate(u32 mask) {
  invalid_flags |= mask;
  InvalidateSelfAndParents();
}

// Project a grid point into the minimap scene using its current origin and integer zoom.
BdMiniMapWindow *BD_FASTCALL BdMiniMapWindow::Project(const BdPoint3 *point, BdPoint *out) {
  u32 x = static_cast<u32>(point->y) - static_cast<u32>(point->x);
  u32 y = static_cast<u32>(point->x) + static_cast<u32>(point->y) - 2u * static_cast<u32>(point->z);
  out->x = static_cast<i32>(static_cast<u32>(offset.x) + (x << (static_cast<u32>(zoom) & 31)));
  out->y = static_cast<i32>(static_cast<u32>(offset.y) + (y << ((static_cast<u32>(zoom) - 1) & 31)));
  return this;
}

// Draw the grid's columns into the minimap scene, then constrain and rebuild its selected render slices.
const BdVolume *BdMiniMapWindow::RenderScene() {
  BdPoint3 origin = {0, 0, 0};
  i32 top = INT32_MAX;
  renderer->SetLighting(4, grid->rotation << 7);
  grid->BuildRenderSlices(&grid->bounds, &origin);
  renderer->BeginScene();
  for (i32 x = grid->bounds.origin.x; x < grid->bounds.origin.x + grid->bounds.size.x; ++x) {
    for (i32 y = grid->bounds.origin.y; y < grid->bounds.origin.y + grid->bounds.size.y; ++y) {
      u32 index = grid->render_columns[x][y];
      if (!index)
        continue;
      BdPoint3 point = {x, y, 0};
      BdPoint screen;
      Project(&point, &screen);
      i32 baseline = screen.y;
      u32 height = static_cast<u16>(grid->bounds.size.z);
      do {
        BdCell *cell = &grid->cells[index];
        u32 cell_height = (cell->link_height_kind >> 17) & kBdCellHeightValueMask;
        if (cell_height >= height)
          break;
        screen.y = static_cast<i32>(static_cast<u32>(baseline) - (cell_height << (static_cast<u32>(zoom) & 31)));
        if (cell->link_height_kind & kBdCellHeightMask)
          renderer->DrawCell(&screen, cell, 0, BdSelectedHighlight);
        else
          renderer->DrawTopCell(&screen, cell, 0, BdSelectedHighlight);
        index = cell->link_height_kind & kBdCellNextMask;
        if (screen.y < top)
          top = screen.y;
      } while (index);
    }
  }
  renderer->EndScene();
  if (top != INT32_MAX)
    scene_rectangle.top = static_cast<i32>(static_cast<u32>(top) - (2u << (static_cast<u32>(zoom) & 31)));
  if (selection_bounds.origin.x < 0)
    selection_bounds.origin.x = 0;
  if (selection_bounds.size.x > grid->bounds.size.x)
    selection_bounds.size.x = grid->bounds.size.x;
  if (selection_bounds.origin.x + selection_bounds.size.x > grid->bounds.origin.x + grid->bounds.size.x)
    selection_bounds.origin.x = grid->bounds.origin.x + grid->bounds.size.x - selection_bounds.size.x;
  if (selection_bounds.origin.y < 0)
    selection_bounds.origin.y = 0;
  if (selection_bounds.size.y > grid->bounds.size.y)
    selection_bounds.size.y = grid->bounds.size.y;
  if (selection_bounds.origin.y + selection_bounds.size.y > grid->bounds.origin.y + grid->bounds.size.y)
    selection_bounds.origin.y = grid->bounds.origin.y + grid->bounds.size.y - selection_bounds.size.y;
  if (selection_bounds.origin.z < 0)
    selection_bounds.origin.z = 0;
  if (selection_bounds.size.z > grid->bounds.size.z)
    selection_bounds.size.z = grid->bounds.size.z;
  if (selection_bounds.origin.z + selection_bounds.size.z > grid->bounds.origin.z + grid->bounds.size.z)
    selection_bounds.origin.z = grid->bounds.origin.z + grid->bounds.size.z - selection_bounds.size.z;
  BdClampPoint(&selected, &selection_bounds);
  return grid->BuildRenderSlices(&selection_bounds, &selected);
}

// Copy the current minimap overlay to its surface when the surface can be locked for blitting.
u8 BdMiniMapWindow::RefreshSurface() {
  u8 result = surface->Lock(GxLockFlags::Blt);
  if (result) {
    ScRect rect = *overlay->GetBufferArea();
    surface->Blt(overlay, rect, rect, nullptr);
    result = surface->Unlock(GxLockFlags::Blt);
  }
  return result;
}

// Draw either the selected volume outline or the camera viewport, retaining its hit bounds and anchor.
void BdMiniMapWindow::DrawGizmo() {
  if (!selection_mode) {
    BdPoint projected;
    Project(&selection_bounds.origin, &projected);
    i32 side = static_cast<i32>(static_cast<u32>(selection_bounds.size.y) << (static_cast<u32>(zoom) & 31));
    i32 height = static_cast<i32>(static_cast<u32>(selection_bounds.size.z) << (static_cast<u32>(zoom) & 31));
    const ScRect *scene_area = scene->GetBufferArea();
    i32 scene_height = scene_area->bottom - scene_area->top;
    scene_area = scene->GetBufferArea();
    i32 x = projected.x + drag_offset.x - (scene_area->right - scene_area->left) / 2;
    i32 bottom = scene_height - drag_offset.y - projected.y;
    i32 middle = scene_height - side / 2 - drag_offset.y - projected.y;
    BdPoint points[6] = {{x + side, middle}, {x, bottom - side}, {x - side, middle}, {x, height + bottom},
        {x + side, height + middle}, {x - side, height + middle}};
    if (zoom_factor < 1.0f) {
      for (BdPoint &point : points) {
        point.x = static_cast<i32>(static_cast<i64>(point.x * static_cast<f64>(zoom_factor)));
        point.y = static_cast<i32>(static_cast<i64>(point.y * static_cast<f64>(zoom_factor)));
      }
    }
    const ScRect *overlay_area = overlay->GetBufferArea();
    i32 overlay_height = overlay_area->bottom - overlay_area->top;
    overlay_area = overlay->GetBufferArea();
    i32 overlay_width = overlay_area->right - overlay_area->left;
    for (BdPoint &point : points) {
      point.x += overlay_width / 2;
      point.y = overlay_height + view_offset - point.y;
    }
    if (overlay->Lock(GxLockFlags::Line)) {
      static const u8 edges[6][2] = {{1, 0}, {1, 2}, {4, 3}, {5, 3}, {4, 0}, {5, 2}};
      for (u32 pass = 0; pass < 2; ++pass) {
        i32 shift = pass == 0 ? 1 : 0;
        u16 color = static_cast<u16>(colors[pass == 0 ? 1 : 2]);
        for (const auto &edge : edges) {
          const BdPoint &first = points[edge[0]], &second = points[edge[1]];
          overlay->DrawLine(first.x + shift, first.y, second.x + shift, second.y, static_cast<i32>(color), 1);
        }
      }
      overlay->Unlock(GxLockFlags::Line);
    }
    gizmo_bounds = {points[2].x, points[3].y, points[0].x, points[1].y};
    return;
  }
  if (selection_mode > 4)
    return;
  BdRect source = selection_rectangle;
  i32 delta;
  if (grid->view_mode == 1)
    delta = static_cast<i32>(view_mode) - 5;
  else if (grid->view_mode == 2)
    delta = static_cast<i32>(view_mode) - 4;
  else if (grid->view_mode <= 5 && grid->view_mode > 2)
    delta = static_cast<i32>(view_mode) - 3;
  else
    return; // The original consumes an uninitialized value for other grid modes.
  f32 view_scale = 1.0f;
  if (delta > 0)
    view_scale = static_cast<f32>(1.0 / static_cast<i32>(1u << (static_cast<u32>(delta) & 31)));
  else if (delta < 0)
    view_scale = static_cast<f32>(static_cast<i32>(1u << (static_cast<u32>(-delta) & 31)));
  f32 horizontal = static_cast<f32>(171.0 / (source.right - source.left));
  f32 vertical = static_cast<f32>(171.0 / (source.bottom - source.top));
  scene->GetBufferArea();
  scene->GetBufferArea();
  BdRect projected;
  projected.left =
      static_cast<i32>(static_cast<i64>((source.left - selection_point.x) * static_cast<f64>(view_scale) * horizontal));
  projected.right = static_cast<i32>(
      static_cast<i64>((source.right - selection_point.x) * static_cast<f64>(view_scale) * horizontal));
  projected.top =
      static_cast<i32>(static_cast<i64>((source.top - selection_point.y) * static_cast<f64>(view_scale) * vertical));
  projected.bottom =
      static_cast<i32>(static_cast<i64>((source.bottom - selection_point.y) * static_cast<f64>(view_scale) * vertical));
  i32 *coordinates[] = {&projected.left, &projected.top, &projected.right, &projected.bottom};
  for (u32 i = 0; i < 4; ++i) {
    *coordinates[i] = static_cast<i32>(static_cast<i64>(*coordinates[i] * static_cast<f64>(zoom_factor)));
    *coordinates[i] += (i & 1) ? scene_center.y : scene_center.x;
    if (*coordinates[i] < 3)
      *coordinates[i] = 3;
    if (*coordinates[i] > 168)
      *coordinates[i] = 168;
  }
  viewport = projected;
  i32 left = viewport.left + drag_offset.x, top = viewport.top + drag_offset.y;
  i32 right = viewport.right + drag_offset.x, bottom = viewport.bottom + drag_offset.y;
  if (overlay->Lock(GxLockFlags::Line)) {
    for (u32 pass = 0; pass < 2; ++pass) {
      i32 shift = pass == 0 ? 1 : 0;
      u16 color = static_cast<u16>(colors[pass == 0 ? 1 : 2]);
      overlay->DrawLine(left + shift, top + shift, right + shift, top + shift, static_cast<i32>(color), 1);
      overlay->DrawLine(right + shift, top + shift, right + shift, bottom + shift, static_cast<i32>(color), 1);
      overlay->DrawLine(left + shift, bottom + shift, right + shift, bottom + shift, static_cast<i32>(color), 1);
      overlay->DrawLine(left + shift, bottom + shift, left + shift, top + shift, static_cast<i32>(color), 1);
    }
    overlay->Unlock(GxLockFlags::Line);
  }
  BdPoint anchor = {offset.x + drag_offset.x, offset.y + drag_offset.y};
  if (anchor.x >= viewport.left && anchor.y >= viewport.top && anchor.x <= viewport.right &&
      anchor.y <= viewport.bottom)
    viewport_anchor = anchor;
}

// Retain the new grid and configure the renderer and minimap bounds for its view size.
void BD_FASTCALL BdMiniMapWindow::SetGrid(BdGrid *value) {
  if (!value)
    return;
  value->AddRef();
  ScRelease(&grid);
  grid = value;
  if (renderer) {
    BdMaterials *materials = nullptr;
    grid->GetMaterials(&materials);
    renderer->SetMaterials(materials);
    materials->Release();
    static const i32 offsets[] = {0, 0, 0, 16, 0, -16};
    u8 mode = grid->view_mode;
    view_offset = offsets[mode];
    u32 half_width = scene->Width() >> 1;
    u32 y = scene->Height() - half_width;
    u32 width = scene->Width();
    scene_rectangle.left = 0;
    offset.x = (width >> 1) - 1;
    offset.y = y;
    scene_rectangle.top = scene->Height() - offset.y;
    scene_rectangle.right = scene->Width();
    scene_rectangle.bottom = scene->Height();
    if (mode == 1)
      zoom = 3;
    else if (mode == 2)
      zoom = 2;
    else if (mode > 2 && mode <= 5)
      zoom = 1;
    scale = static_cast<f32>(1 << zoom);
    renderer->SetScale(scale);
    renderer->SetOffset(&offset);
    selection_bounds = grid->bounds;
    bounds = grid->bounds;
  }
  Invalidate(1);
}

// Apply editor state messages to the minimap and request redraws for affected geometry.
bool BdMiniMapCallback::DoMessage(ScMessage *message) {
  if (!map_window)
    return false;
  switch (message->type) {
  case kWnMsg_ArchitectGridChanged: {
    BdGrid *grid = BdGetCurrentGrid();
    if (grid)
      map_window->SetGrid(grid);
    break;
  }
  case kWnMsg_ArchitectInvalidateGrid:
    map_window->Invalidate(1);
    break;
  case kWnMsg_ArchitectViewModeChanged:
    map_window->view_mode = static_cast<u8>(message->data1);
    break;
  case kWnMsg_ArchitectSelectedCellChanged:
    map_window->selected = *reinterpret_cast<const BdPoint3 *>(message->data1);
    break;
  case kWnMsg_ArchitectSelectionBoundsChanged:
    map_window->selection_bounds = *reinterpret_cast<const BdVolume *>(message->data1);
    map_window->Invalidate(2);
    break;
  case kWnMsg_ArchitectProjectionBoundsChanged:
    map_window->selection_point = *reinterpret_cast<const BdPoint *>(message->data1);
    map_window->selection_rectangle = *reinterpret_cast<const BdRect *>(message->data2);
    map_window->Invalidate(2);
    break;
  case kWnMsg_ArchitectForwardInput:
    if (static_cast<u32>(message->data2) == kBdCommandSelectionMode)
      map_window->selection_mode = static_cast<u8>(message->data3);
    break;
  }
  return false;
}

// Create the minimap's generic window and install its background image and callback.
// The original uses a 200-pixel fallback size when the background resource is unavailable.
u8 BdMiniMapCallback::BuildWindow(WnWindow **out) {
  if (!out)
    return 0;
  UiGenericInterface *generic = nullptr;
  u8 result = BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  if (result && generic) {
    generic->GetWindow()->PrivateBuffer(true);
    generic->SetStyle(kUiGenericNoFrame, 1);
    generic->SetStyle(kUiGenericDraggable, 0);
    generic->SetStyle(kUiGenericCloseButton, 0);
    generic->SetStyle(kUiGenericScroller, 0);
    generic->SetStyle(kUiGenericNoBevel, 1);
    generic->SetStyle(kUiGenericNoBackground, 0);
    generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
    generic->SetBorder(0, 0);
    const GxColorType format(GxColorType::Color::RGB16);
    i32 width = 200, height = 200;
    BdImageResource image(256, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
    if (image.buffer) {
      generic->SetImage(1, image.buffer);
      width = image.buffer->Width();
      height = image.buffer->Height();
    }
    generic->GetWindow()->SetSize(width, height);
    result = generic->SetCallback(this);
    *out = generic->GetWindow();
  }
  return result;
}

// Create a minimap button in its requested style, apply its optional localized tip, and attach it to the parent.
// The source enables radio buttons immediately and fits every successfully created control before positioning it.
u8 BdMiniMapCallback::AddButton(
    WnWindow *parent, u32 identifier, const ScStringView *caption, i32 x, i32 y, GxBuffer *image, i32 style, u32 tip) {
  if (!parent)
    return 0;
  WnResources *resources = BdGetWindowResources();
  WnButton *button;
  if (style == 0) {
    button = resources->CreateRadioButton(identifier, *caption, image, nullptr, 0);
    if (!button)
      return 0;
    button->ToggleOn();
  } else if (style == 1) {
    if (image)
      button = resources->CreateFixedToggleButton(identifier, *caption, image, image->GetBufferArea(), 0);
    else
      button = resources->CreateToggleButton(identifier, *caption, nullptr, nullptr, 0);
  } else {
    if (image)
      button = resources->CreateFixedButton(identifier, *caption, image, image->GetBufferArea(), 0);
    else
      button = resources->CreateButton(identifier, *caption, nullptr, nullptr, 0);
  }
  if (!button)
    return 0;
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  if (tip) {
    BdTextResource resource(tip, kRsArchitectTextGroup);
    if (resource.text) {
      button->SetTipText(*static_cast<ScStringView *>(resource.text));
      button->SetButtonFlag(kWnButtonShowText, false);
      button->SetButtonFlag(kWnButtonShowHint, true);
      button->SetTipPlacementType(kWnTipAboveCenter);
    }
  }
  parent->ChildAdd(button->GetWindow());
  button->Fit();
  button->GetWindow()->GZWinMoveTo(x, y);
  button->Release();
  return 1;
}

// Create the minimap child and its four controls, then subscribe to editor state messages.
u8 BdMiniMapCallback::BuildContent(WnWindow *window) {
  if (initialized)
    return 1;
  ScRect child_area;
  window->GetArea(&child_area);
  map_window = ScAddRefNotNull(new BdMiniMapWindow());
  map_window->SetID(kBdMiniMapCanvasId);
  map_window->SetArea(17, 16, 188, 187);
  map_window->SetFlag(kWnFlagReorderable, false);
  window->ChildAdd(map_window);
  map_window->GZWinMoveTo(17, 16);
  map_window->GetArea(&child_area);
  {
    ScString caption;
    const GxColorType format(GxColorType::Color::RGB16);
    {
      BdImageResource image(179, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
      AddButton(window, kBdMiniMapZoomInId, &caption, child_area.left + 8, child_area.top + 127, image.buffer, 2, 180);
    }
    {
      BdImageResource image(177, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
      AddButton(window, kBdMiniMapRotateNegativeId, &caption, child_area.left + 36, child_area.top + 142, image.buffer,
          2, 182);
    }
    {
      BdImageResource image(178, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
      AddButton(window, kBdMiniMapRotatePositiveId, &caption, child_area.left + 107, child_area.top + 142, image.buffer,
          2, 183);
    }
    {
      BdImageResource image(180, kBdWorkspaceImageGroup, GxVramRequest::SystemMemory, format);
      AddButton(
          window, kBdMiniMapZoomOutId, &caption, child_area.left + 135, child_area.top + 128, image.buffer, 2, 181);
    }
  }
  ScMessageServer *server = BdGetMessageServer();
  if (server) {
    static const u32 messages[] = {kWnMsg_ArchitectGridChanged, kWnMsg_ArchitectInvalidateGrid,
        kWnMsg_ArchitectSelectionBoundsChanged, kWnMsg_ArchitectProjectionBoundsChanged, kWnMsg_ArchitectForwardInput,
        kWnMsg_ArchitectSelectedCellChanged, kWnMsg_ArchitectViewModeChanged};
    for (u32 message : messages)
      server->AddNotification(static_cast<ScMessageTarget *>(this), message);
  }
  initialized = 1;
  return 1;
}

// Read the dimensions of the generic window's borrowed background image.
// Leave the outputs untouched when the window has no generic interface or image.
u8 BD_STDCALL BdMiniMapGetDimensions(WnWindow *window, i32 *width, i32 *height) {
  UiGenericInterface *generic = nullptr;
  if (!window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic)))
    return 0;
  u32 index;
  GxBuffer *image = nullptr;
  generic->GetImage(&index, &image);
  u8 result = 0;
  if (image) {
    *width = image->Width();
    *height = image->Height();
    result = 1;
  }
  generic->Release();
  return result;
}

// Set equal minimum and maximum dimensions from the generic window's borrowed background image.
u8 BD_STDCALL BdMiniMapGetLimits(WnWindow *window, ScRect *limits) {
  if (!window || !limits)
    return 0;
  i32 width, height;
  if (!BdMiniMapGetDimensions(window, &width, &height))
    return 0;
  *limits = {width, height, width, height};
  return 1;
}

// Remove the minimap's notifications and release its window before clearing the initialized flag.
u8 BdMiniMapCallback::Close(WnWindow *) {
  ScMessageServer *server = BdGetMessageServer();
  if (server) {
    static const u32 messages[] = {kWnMsg_ArchitectGridChanged, kWnMsg_ArchitectInvalidateGrid,
        kWnMsg_ArchitectSelectionBoundsChanged, kWnMsg_ArchitectProjectionBoundsChanged, kWnMsg_ArchitectForwardInput,
        kWnMsg_ArchitectSelectedCellChanged, kWnMsg_ArchitectViewModeChanged};
    for (u32 message : messages)
      server->RemoveNotification(static_cast<ScMessageTarget *>(this), message);
  }
  if (map_window)
    map_window->Release();
  map_window = nullptr;
  initialized = 0;
  return 1;
}

// Dispatch generic-window events and translate the four minimap button commands into editor messages.
bool BdMiniMapCallback::DoWinMsg(WnWindow *window, u32 message, uptr first, uptr second, uptr) {
  if (message == kWnMsg_Command) {
    if (first != 1)
      return 0;
    ScMessageServer *server = BdGetMessageServer();
    u32 type;
    switch (second) {
    case kBdMiniMapRotateNegativeId:
      type = kWnMsg_ArchitectRotateViewNegative;
      break;
    case kBdMiniMapRotatePositiveId:
      type = kWnMsg_ArchitectRotateViewPositive;
      break;
    case kBdMiniMapZoomInId:
      type = kWnMsg_ArchitectZoomIn;
      break;
    case kBdMiniMapZoomOutId:
      type = kWnMsg_ArchitectZoomOut;
      break;
    default:
      return 0;
    }
    ScMessage command(type);
    return server->MessagePost(command, false);
  }
  if (message == kWnMsg_Init)
    return BuildContent(window);
  if (message == kWnMsg_Shutdown)
    return Close(window);
  if (message == kWnMsg_ClientConstraints)
    return BdMiniMapGetLimits(window, reinterpret_cast<ScRect *>(first));
  return 0;
}

// Accept points inside the button only when the parent also accepts the corresponding screen coordinates.
// An unattached button uses the ordinary image-button hit test alone.
bool BdToolIcon::IsPointInWindow(i32 x, i32 y) const {
  if (!GetParentWin())
    return WnImageButton::IsPointInWindow(x, y);
  if (!WnImageButton::IsPointInWindow(x, y))
    return false;
  WindowToScreenCoordinates(&x, &y);
  return GetParentWin()->IsPointInWindowScreenCoordinates(x, y);
}

// Attach a four-state image button to the tool item at the requested position.
// The item receives the button before this routine releases its temporary interface reference.
u8 BD_STDCALL BdAddToolIcon(BdToolItem *item, u32 identifier, i32 x, i32 y, GxBuffer *image) {
  if (!item || !image)
    return 0;
  ScRect bounds;
  image->GetBufferArea(&bounds);
  BdToolIcon *created = new BdToolIcon;
  WnButton *button = static_cast<WnButton *>(created);
  button->AddRef();
  button->GetWindow()->SetID(identifier);
  button->SetStyle(kWnButtonToggleFixed, image, &bounds);
  button->Fit();
  button->GetWindow()->SetFlag(kWnFlagReorderable, false);
  item->AddIcon(button, x, y);
  button->Release();
  return 1;
}

// A failed drawing lock retains the successful initialization result without publishing an output buffer.
u8 BD_STDCALL BdCreateToolIconImage(GxBuffer *source, const BdRect *bounds, u8 border, GxBuffer **out) {
  if (!source)
    return 0;
  GxGraphics *graphics = nullptr;
  BdFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&graphics));
  if (!graphics)
    return 0;
  GxBuffer *image = nullptr;
  graphics->CreateBuffer(&image);
  if (!image) {
    graphics->Release();
    return 0;
  }
  i32 width = bounds->right - bounds->left;
  i32 height = bounds->bottom - bounds->top;
  GxColorType current = graphics->GetScreenColorType();
  u8 result = image->Init(4 * static_cast<u32>(width), static_cast<u32>(height), current, nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (result) {
    image->RemoveTransparency();
    if (image->Lock(GxLockFlags::Blt)) {
      ScRect original = {bounds->left, bounds->top, bounds->right, bounds->bottom};
      ScRect target = {0, 0, width, height};
      for (u32 index = 0; index != 4; ++index) {
        image->Blt(source, original, target, nullptr);
        target.left = target.left + width;
        target.right = target.right + width;
      }
      image->Unlock(GxLockFlags::Blt);
    }
    u8 locked = image->Lock(GxLockFlags::Bits);
    u16 gray_dark = image->ConvertRGBValueToNative16Bit(63, 63, 63);
    u16 gray_light = image->ConvertRGBValueToNative16Bit(95, 95, 95);
    u16 yellow_dark = image->ConvertRGBValueToNative16Bit(127, 127, 0);
    u16 yellow = image->ConvertRGBValueToNative16Bit(191, 191, 0);
    u16 yellow_light = image->ConvertRGBValueToNative16Bit(223, 223, 0);
    if (locked) {
      ScRect frame = {width, 0, 2 * width, height};
      if (border) {
        ScRect line = frame;
        image->DrawRectangleOutline(line, gray_dark, 1);
        ++line.left;
        ++line.top;
        image->DrawRectangleOutline(line, gray_dark, 1);
        --line.left;
        --line.top;
        --line.right;
        --line.bottom;
        image->DrawRectangleOutline(line, gray_light, 1);
      }
      frame.left = frame.left + width;
      frame.right = frame.right + width;
      ScRect line = frame;
      image->DrawRectangleOutline(line, yellow, 1);
      ++line.left;
      ++line.top;
      image->DrawRectangleOutline(line, yellow, 1);
      --line.left;
      --line.top;
      --line.right;
      --line.bottom;
      image->DrawRectangleOutline(line, yellow_light, 1);
      frame.left = frame.left + width;
      frame.right = frame.right + width;
      line = frame;
      image->DrawRectangleOutline(line, yellow_dark, 1);
      ++line.left;
      ++line.top;
      image->DrawRectangleOutline(line, yellow_dark, 1);
      --line.left;
      --line.top;
      --line.right;
      --line.bottom;
      image->DrawRectangleOutline(line, yellow, 1);
      image->Unlock(GxLockFlags::Bits);
      result = 1;
      image->AddRef();
      *out = image;
    }
  }
  image->Release();
  graphics->Release();
  return result;
}

// Clear a palette buffer to the requested color when its drawing lock succeeds.
static u8 BdFillShapeBuffer(GxBuffer *buffer, u32 color) {
  u8 locked = buffer->Lock(GxLockFlags::Fill);
  if (locked) {
    buffer->Fill(color, nullptr);
    buffer->Unlock(GxLockFlags::Fill);
    return 1;
  }
  return locked;
}

// Render the original four-by-fourteen shape palette and attach the generated four-state icon images.
// Each shape preview uses the original 16-bit bicubic scaler before its icon states are assembled.
u8 BdToolSelection::BuildShapeIcons(BdToolItem *item) {
  GxGraphics *graphics = nullptr;
  BdFrameWork()->GetSystemService(kGxGraphicsServiceId, IID_GxGraphics, reinterpret_cast<void **>(&graphics));
  if (!graphics)
    return 0;
  GxColorType current = graphics->GetScreenColorType();
  GxBuffer *target = nullptr, *preview = nullptr;
  bool ready = graphics->CreateBuffer(&target);
  if (ready)
    ready = target->Init(
        104, 104, current, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (ready)
    ready = graphics->CreateBuffer(&preview);
  if (ready)
    ready = preview->Init(
        32, 32, current, nullptr, GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (!ready) {
    if (target)
      target->Release();
    if (preview)
      preview->Release();
    graphics->Release();
    return 0;
  }
  graphics->Release();
  BdRect icon_bounds = {0, 0, 32, 32};
  BdPoint screen = {51, 51};
  u16 background = target->ConvertRGBValueToNative16Bit(95, 95, 127);
  BdFillShapeBuffer(preview, background);
  BdRenderer *renderer = new BdRenderer;
  renderer->SetOffset(&screen);
  renderer->SetBuffer(target);
  renderer->SetScale(52.0f);
  renderer->SetLighting(0, 0);
  BdCell cell;
  static const u8 rotations[] = {3, 2, 1, 0};
  static const u8 shapes[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14};
  i32 x = 5;
  for (u32 i = 0; i < sizeof(rotations); ++i) {
    i32 y = 5;
    for (u32 j = 0; j < sizeof(shapes); ++j) {
      BdFillShapeBuffer(target, background);
      u32 packed = (shapes[j] & 15u) | ((rotations[i] & 3u) << 4);
      cell.link_height_kind = (cell.link_height_kind & kBdCellWithoutKindOrRotation) | (packed << 26);
      renderer->BeginScene();
      renderer->DrawMaskedCell(&screen, &cell, 7);
      renderer->EndScene();
      GxBuffer *scaled = BdScalePalettedBicubic(0.25f, target, nullptr, nullptr, nullptr);
      if (scaled) {
        ScRect destination = *scaled->GetBufferArea();
        destination.left += 3;
        destination.top += 3;
        destination.right += 3;
        destination.bottom += 3;
        if (preview->Lock(GxLockFlags::Blt)) {
          preview->Blt(scaled, *(scaled->GetBufferArea()), destination, nullptr);
          preview->Unlock(GxLockFlags::Blt);
        }
        scaled->Release();
      }
      GxBuffer *image = nullptr;
      if (BdCreateToolIconImage(preview, &icon_bounds, 0, &image)) {
        u32 identifier =
            static_cast<u32>(shapes[j]) | ((static_cast<u32>(rotations[i]) | kBdShapeToolIdentifierPrefix) << 8);
        BdAddToolIcon(item, identifier, x, y, image);
        if (image)
          image->Release();
      }
      y += 37;
    }
    x += 37;
  }
  target->Release();
  preview->Release();
  renderer->SetBuffer(nullptr);
  delete renderer;
  return 1;
}

// The original enumeration callback collects independently copied asset names for the subsequent layout.
struct BdAssetVisitContext {
  ScArray<ScString *> names;
};

static void BdCollectIconName(const ScString *name, BdCatalog *, BdAssetVisitContext *context) {
  *context->names.Append() = new ScString(*name);
}

static bool BdIconNameLess(const ScString *first, const ScString *second) {
  u32 first_size = first->Strlen(), second_size = second->Strlen();
  u32 length = first_size < second_size ? first_size : second_size;
  i32 result = length ? memcmp(first->Data(), second->Data(), length) : 0;
  return result < 0 || (!result && first_size < second_size);
}

// Place unused catalog assets in wrapped rows, preserving kind 3's insertion order and tighter spacing.
// Return whether the original enumeration visited at least one asset, even if none could be rendered.
bool BdToolSelection::BuildAssetIcons(BdToolItem *item, BdCatalog *catalog, u32 kind) {
  BdAssetVisitContext context;
  u32 visited = catalog->EnumerateAssets(kind, BdCollectIconName, &context);
  i32 initial_height = item->GetContentHeight();
  if (kind != 3) {
    for (u32 i = 1; i < context.names.size; ++i) {
      ScString *name = context.names.data[i];
      u32 position = i;
      while (position && BdIconNameLess(name, context.names.data[position - 1])) {
        context.names.data[position] = context.names.data[position - 1];
        --position;
      }
      context.names.data[position] = name;
    }
  }
  i32 spacing = kind == 3 ? 1 : 5;
  u32 available = static_cast<u32>(item->GetWindow()->GetW()) - 16u;
  i32 x = 5, y = initial_height + 5, row_height = 0;
  for (u32 i = 0; i < context.names.size; ++i) {
    BdAsset *asset = nullptr;
    if (!catalog->FindAsset(context.names.data[i], &asset) || (asset->flags & 8))
      continue;
    BdRect *bounds = &asset->preview_bounds;
    GxBuffer *rendered = asset->RenderImage(nullptr, item->GetBackground());
    u32 right = static_cast<u32>(bounds->right) + static_cast<u32>(x) - static_cast<u32>(bounds->left);
    if (right > available) {
      x = 5;
      y = y + spacing + row_height;
      row_height = 0;
    }
    GxBuffer *image = nullptr;
    if (BdCreateToolIconImage(rendered, bounds, 1, &image)) {
      BdAddToolIcon(item, asset->definition_id, x, y, image);
      if (image)
        image->Release();
    }
    i32 height = bounds->bottom - bounds->top;
    if (height > row_height)
      row_height = height;
    x = x + bounds->right + spacing - bounds->left;
    if (rendered)
      rendered->Release();
  }
  for (u32 i = 0; i < context.names.size; ++i)
    delete context.names.data[i];
  return visited != 0;
}

// Lay out each composition's preview assets at their original 16-pixel offsets and mark them as already placed.
// The item content height is updated after each composition, even when no asset in that composition loads.
u8 BdToolSelection::BuildCompositionIcons(BdToolItem *item, BdCatalog *catalog, u32 group) {
  BdGroup *selected = catalog->groups.data[group];
  i32 top = 5;
  u32 count = selected->compositions.size;
  for (u32 i = 0; i < count; ++i) {
    BdComposition *composition = selected->compositions.data[i];
    u32 part_count = composition->parts.size;
    for (u32 j = 0; j < part_count; ++j) {
      BdPart *part = composition->parts.data[j];
      BdAsset *asset = nullptr;
      if (!catalog->FindAsset(&part->name, &asset))
        continue;
      i32 x = 5 + 16 * part->x;
      i32 y = static_cast<i32>(static_cast<u32>(top) + 16u * part->y);
      BdRect bounds = asset->preview_bounds;
      GxBuffer *rendered = asset->RenderImage(&bounds, item->GetBackground());
      asset->flags |= 8;
      GxBuffer *image = nullptr;
      if (BdCreateToolIconImage(rendered, &bounds, 0, &image)) {
        BdAddToolIcon(item, asset->definition_id, x, y, image);
        if (image)
          image->Release();
      }
      if (rendered)
        rendered->Release();
    }
    top = top + composition->height + 8;
    item->SetContentHeight(top);
  }
  return 1;
}

// Localized item identifiers and line counts for the three catalog-backed tool groups.
struct BdToolItemSpec {
  u32 identifier, text, text_group;
  i32 rows;
};

// The queried interface is retained through its window view for the caller to release.
u8 BdToolSelection::BuildWindow(WnWindow **out, u32 group) {
  if (group < kBdShapeAssetGroupId || group > kBdPropAssetGroupId)
    return 0;
  group_id = group;
  UiGenericInterface *generic = nullptr;
  u8 result = BdGetCom()->GetClassObject(kUiGenericClassId, IID_UiGenericBase, reinterpret_cast<void **>(&generic));
  if (result && generic) {
    generic->GetWindow()->SetFlag(kWnFlagPrivateBuffer, true);
    generic->SetStyle(kUiGenericNoFrame, 1);
    generic->SetStyle(kUiGenericDraggable, 0);
    generic->SetStyle(kUiGenericCloseButton, 0);
    generic->SetStyle(kUiGenericScroller, 0);
    generic->SetStyle(kUiGenericNoPainting, 1);
    generic->SetStyle(kUiGenericCloseOnEscapeAltF4, 0);
    generic->SetData(reinterpret_cast<UiWindowData *>(static_cast<uptr>(group_id)));
    result = generic->SetCallback(this);
    *out = generic->GetWindow();
  }
  return result;
}

// Read the generic window's group identifier only after initialization and a successful interface query.
u32 BD_STDCALL BdGetToolGroupID(WnWindow *window) {
  if (!window->GetFlag(kWnFlagInitialized))
    return 0;
  UiGenericInterface *generic = nullptr;
  u32 id = 0;
  if (window->QueryInterface(IID_UiGenericBase, reinterpret_cast<void **>(&generic))) {
    id = static_cast<u32>(reinterpret_cast<uptr>(generic->GetData()));
    generic->Release();
  }
  return id;
}

u8 BD_STDCALL BdGetToolGroupControl(WnWindow *window, BdToolGroupControl **out) {
  u32 id = BdGetToolGroupID(window);
  return id ? window->GetChildAs(id, IID_BdToolGroupControl, reinterpret_cast<void **>(out)) : 0;
}

// Translate the original two flag bits and apply them to the identified tool-group control.
u8 BD_STDCALL BdSetToolGroupFlags(WnWindow *window, u16 flags) {
  BdToolGroupControl *group = nullptr;
  if (!BdGetToolGroupControl(window, &group))
    return 0;
  u32 value = (flags & 0x1000) != 0;
  if (flags & 0x2000)
    value |= 2;
  u8 result = group->SetGroupFlags(value);
  group->Release();
  return result;
}

u8 BD_STDCALL BdSetToolGroupColor(WnWindow *window, u32 color) {
  BdToolGroupControl *group = nullptr;
  if (!BdGetToolGroupControl(window, &group))
    return 0;
  u8 result = group->SetGroupColor(color);
  group->Release();
  return result;
}

// Map the three supported image modes and apply the selected mode to the identified group.
u8 BD_STDCALL BdSetToolGroupImageMode(WnWindow *window, i32 mode) {
  BdToolGroupControl *group = nullptr;
  if (!BdGetToolGroupControl(window, &group))
    return 0;
  u32 value = mode == 1 ? 1 : mode == 2 ? 2 : 0;
  u8 result = group->SetImageMode(value);
  group->Release();
  return result;
}

u8 BD_STDCALL BdSetToolGroupImage(WnWindow *window, GxBuffer *image) {
  BdToolGroupControl *group = nullptr;
  if (!BdGetToolGroupControl(window, &group))
    return 0;
  u8 result = group->SetGroupImage(image);
  group->Release();
  return result;
}

u8 BD_STDCALL BdSetToolGroupMargins(WnWindow *window, i32 left, i32 top, i32 right, i32 bottom) {
  BdToolGroupControl *group = nullptr;
  if (!BdGetToolGroupControl(window, &group))
    return 0;
  u8 result = group->SetMargins(left, top, right, bottom);
  group->Release();
  return result;
}

// Create, hide and attach one selection window, then configure its child group with the source descriptor.
// Release the window reference after the attachment and any resulting control updates.
u8 BdToolPanel::BuildGroupWindow(WnWindow *source, const BdToolGroup *group, const ScRect *area) {
  WnWindow *child_window = nullptr;
  u8 result = 0;
  if (selection->BuildWindow(&child_window, group->word4) && child_window) {
    child_window->SetID(group->word3);
    child_window->SetFlag(kWnFlagReorderable, false);
    child_window->SetArea(*area);
    child_window->HideWindow();
    result = source->ChildAdd(child_window);
    if (result) {
      GxColorType format(GxColorType::Color::RGB16);
      BdImageResource image(group->word7, group->kind, GxVramRequest::SystemMemory, format);
      BdSetToolGroupFlags(child_window, 0);
      BdSetToolGroupColor(child_window, group->color);
      BdSetToolGroupImageMode(child_window, 2);
      BdSetToolGroupImage(child_window, image.buffer);
      BdSetToolGroupMargins(child_window, 14, 16, 14, 25);
    }
    child_window->Release();
  }
  return result;
}

void BdToolPanel::BuildGroupWindows(WnWindow *source, const ScRect *area) {
  if (!source || !selection)
    return;
  i32 height = area->bottom - area->top;
  if (height < 96 || area->left >= area->right)
    return;
  for (i32 i = 0; i < 6; ++i)
    if (groups[i].word3)
      BuildGroupWindow(source, &groups[i], area);
}
