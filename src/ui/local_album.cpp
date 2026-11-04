#define WIN32_LEAN_AND_MEAN
#include "ui/local_album.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include <new>

#include "application/api.h"
#include "application/application_core.h"
#include "city/api.h"
#include "city/ct_layer.h"
#include "common/framework.h"
#include "common/path.h"
#include "common/sc3_class_factory.h"
#include "common/sc3_director.h"
#include "common/sc3_error.h"
#include "common/sc3_unknown.h"
#include "common/value.h"
#include "geometry/api.h"
#include "graphics/image_codecs.h"
#include "graphics/sc3_graphics.h"
#include "misc/api.h"
#include "resources/api.h"
#include "resources/resource.h"
#include "sprites/cell_map.h"
#include "sprites/module.h"
#include "ui/controls.h"
#include "ui/query_dialogs.h"
#include "ui/ui_module.h"
#include "ui/ui_resources.h"
#include "ui/ui_ticks.h"
#include "window/controls.h"

// Slide scroll buttons use the corresponding direction and page-step artwork.
enum UiSlideScrollImageId : u32 {
  kUiSlidePreviousImageId = 0xba096c28,
  kUiSlidePreviousPageImageId = 0xfa096c3d,
  kUiSlideNextImageId = 0xba096cb6,
  kUiSlideNextPageImageId = 0xba096cd6,
};

// Slide controls move selection by one item or four-item page; item IDs start at their separate base.
enum UiSlideScrollControlId : u32 {
  kUiSlidePreviousButtonId = 0x590818e,
  kUiSlidePreviousPageButtonId = 0x590818f,
  kUiSlideNextPageButtonId = 0x5908190,
  kUiSlideNextButtonId = 0x5908191,
  kUiSlideFirstItemId = 0x159054f0,
};

UiSlideList::UiSlideList()
    : picture_size(100), gap(4), scroll_x(UINT32_MAX), word176(UINT32_MAX), total_width(0), word184(0), selected(0),
      scroll_indent(0) {
}

UiSlideList::~UiSlideList() {
  RemoveAllSlides();
}

// Enable the original private-buffer flags and clamp the existing selection before initializing the common window.
bool UiSlideList::Init() {
  SetFlag(kWnFlagPrivateBuffer, true);
  SetFlag(kWnFlagTransparentBuffer, true);
  SetFlag(kWnFlagClearTransparentBuffer, true);
  SetSelection(selected);
  return UiBaseWindow::Init();
}

// Publish the uncreated row before retaining either input, then derive the live total width and refresh visibility.
// The original always returns zero after adding the row; that byte is independent of successful insertion.
u8 UiSlideList::AddOriginal(GeAttrib *original, OcOverride *replacement) {
  u32 slide_id = slides.size + kUiSlideFirstItemId;
  UiSlideInfo *slide = slides.Append();
  slide->identifier = slide_id;
  slide->original = original;
  slide->replacement = replacement;
  slide->created = false;
  if (original)
    original->AddRef();
  if (replacement)
    replacement->AddRef();
  u32 count = slides.size;
  total_width = count * picture_size + (count - 1) * gap;
  SetScrollX(scroll_x);
  return 0;
}

// Acquire the provider even for an existing preview, and create only an uncreated row through the real preview kind.
// Attach before configuring identity, geometry and attributes; mark creation only after both attribute setters return.
u8 UiSlideList::CreateSlide(UiSlideInfo *slide, i32 x, i32 y) {
  UiWindowResourcesRef resources;
  if (!resources.provider || slide->created)
    return 0;
  UiPreviewControl *preview = nullptr;
  u8 result = 0;
  if (resources.provider->CreateObject(
          kUiResourceBuildingPreview, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
    WnWindow *window = preview->GetWindow();
    result = ChildAdd(window);
    if (result) {
      window->SetID(slide->identifier);
      window->SetSize((i32)picture_size, (i32)picture_size);
      window->GZWinMoveTo(x, y);
      preview->SetAttributes(slide->original);
      preview->SetOverride(slide->replacement);
      slide->created = true;
    }
    preview->Release();
  }
  return result;
}

// Detach an existing preview child and clear the row's created flag only when the actual removal succeeds.
u8 UiSlideList::RemoveSlide(UiSlideInfo *slide) {
  if (!slide->created)
    return 0;
  WnWindow *child = GetChildWindowFromID(slide->identifier);
  if (!child)
    return 0;
  u8 result = ChildRemove(child);
  if (result)
    slide->created = false;
  return result;
}

// Delete created children and release each captured row's original before reading and releasing its live replacement.
// Clear the live inventory and scroll state only after callbacks, then perform the original empty visibility refresh.
u8 UiSlideList::RemoveAllSlides() {
  UiSlideInfo *slide = slides.data;
  u32 count = slides.size;
  for (u32 i = 0; i < count; ++i, ++slide) {
    if (slide->created) {
      WnWindow *child = GetChildWindowFromID(slide->identifier);
      if (child)
        ChildDelete(child);
    }
    if (slide->original) {
      slide->original->Release();
      slide->original = nullptr;
    }
    if (slide->replacement) {
      slide->replacement->Release();
      slide->replacement = nullptr;
    }
  }
  slides.Clear();
  total_width = scroll_x = 0;
  selected = 0;
  SetScrollX(0);
  return 1;
}

// Clear replacement previews and holds only for created rows that currently have a replacement.
// Uncreated rows retain their replacements, matching the original conditional traversal.
u8 UiSlideList::ClearAllOverRides() {
  UiSlideInfo *slide = slides.data;
  u32 count = slides.size;
  UiPreviewControl *preview = nullptr;
  for (u32 i = 0; i < count; ++i, ++slide) {
    if (!slide->created || !slide->replacement)
      continue;
    if (GetChildAs(slide->identifier, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
      preview->SetOverride(nullptr);
      preview->Release();
      preview = nullptr;
    }
    slide->replacement->Release();
    slide->replacement = nullptr;
  }
  return 1;
}

// Retain the incoming replacement before retiring the old one, then update an existing preview through its live row.
// Created rows invalidate even after failed preview queries; uncreated rows update ownership without redraw work.
u8 UiSlideList::SetOverRide(u32 index, OcOverride *replacement) {
  if (index >= slides.size)
    return 0;
  UiSlideInfo *slide = slides.data + index;
  if (replacement)
    replacement->AddRef();
  if (slide->replacement)
    slide->replacement->Release();
  slide->replacement = replacement;
  if (slide->created) {
    UiPreviewControl *preview = nullptr;
    if (GetChildAs(slide->identifier, IID_UiPreviewControl, reinterpret_cast<void **>(&preview))) {
      preview->SetOverride(slide->replacement);
      preview->Release();
    }
    InvalidateSelf();
  }
  return 1;
}

u8 UiSlideList::GetMinMaxInfo(ScRect *limits) const {
  *limits = {(i32)(picture_size + gap), (i32)picture_size, 2000, 2000};
  return 1;
}

u32 UiSlideList::GetPicSize() const {
  return picture_size;
}

u32 UiSlideList::GetScrollIndent() const {
  return scroll_indent;
}

i32 UiSlideList::GetSelection() const {
  return selected;
}

// Change the picture size only while the actual slide inventory is empty; existing previews prevent the change.
u8 UiSlideList::SetPicSize(u32 size) {
  if (slides.size)
    return 0;
  picture_size = size;
  return 1;
}

// Publish the new indent before refreshing visibility at the previously captured scroll position.
u8 UiSlideList::SetScrollIndent(u32 indent) {
  u32 position = scroll_x;
  scroll_indent = indent;
  SetScrollX(position);
  return 1;
}

// Clamp the ordinal by its unsigned bits, so a negative input selects the last item when the inventory is nonempty.
u8 UiSlideList::SetSelection(i32 selection) {
  if (!slides.size)
    return 0;
  if ((u32)selection >= slides.size)
    selection = (i32)(slides.size - 1);
  selected = selection;
  return 1;
}

// Enforce the original picture minima before forwarding the four scalar bounds to the common window implementation.
void UiSlideList::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  ScRect limits;
  GetMinMaxInfo(&limits);
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width <= limits.left)
    width = limits.left;
  if (height <= limits.top)
    height = limits.top;
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
}

// Clamp the unsigned scroll position after reading live geometry, then create or detach previews in the captured range.
// Re-read picture size and spacing between rows, and invalidate after the complete visibility update.
u8 UiSlideList::SetScrollX(u32 position) {
  u32 size = picture_size;
  i32 y = (i32)((u32)GetH() - size) / 2, minimum = (i32)(0u - size);
  size = picture_size;
  i32 maximum = (i32)(size + (u32)GetW());
  u32 limit = total_width - size;
  scroll_x = position;
  if (position > limit)
    scroll_x = limit;
  i32 x = (i32)(scroll_indent - scroll_x);
  UiSlideInfo *slide = slides.data;
  u32 count = slides.size;
  for (u32 i = 0; i < count; ++i, ++slide) {
    if (x < minimum || x > maximum) {
      if (slide->created)
        RemoveSlide(slide);
    } else {
      if (!slide->created)
        CreateSlide(slide, x, y);
      WnWindow *child = GetChildWindowFromID(slide->identifier);
      if (child)
        child->GZWinMoveTo(x, y);
    }
    x = (i32)((u32)x + gap + picture_size);
  }
  InvalidateSelf();
  return 1;
}

// Ease halfway toward the selected item's scroll target, using a final exact step for a one-pixel difference.
void UiSlideList::Tick() {
  u32 position = scroll_x, target = (u32)selected * (picture_size + gap);
  if (target == position)
    return;
  i32 difference = (i32)(target - position);
  if (difference >= 2 || difference <= -2)
    SetScrollX(position + (u32)(difference / 2));
  else
    SetScrollX(target);
}

#undef SendMessage
#undef PostMessage

// Construct the separate window view before initializing frame insets, held child and repeat state.
// Resolve the ten original bevel colors in their original order after the complete scalar state exists.
UiSlides::UiSlides()
    : list(nullptr), inset_left(14), inset_top(13), inset_right(14), inset_bottom(13), button_width(0),
      button_height(0), pending_command(0), last_scroll(0) {
  colors[0] = MakeFillColor(0x32, 0x37, 0x7d);
  colors[1] = MakeFillColor(0x84, 0x84, 0xb4);
  colors[2] = MakeFillColor(0x26, 0x29, 0x5e);
  colors[3] = MakeFillColor(0x5b, 0x5f, 0x97);
  colors[4] = MakeFillColor(0x40, 0x40, 0x80);
  colors[5] = MakeFillColor(0x4e, 0x5b, 0xa1);
  colors[6] = MakeFillColor(0xa0, 0xae, 0xff);
  colors[7] = MakeFillColor(0x6d, 0x7e, 0xe0);
  colors[8] = MakeFillColor(0x8a, 0x9c, 0xff);
  colors[9] = MakeFillColor(0x7c, 0x90, 0xff);
}

// Retire the held inner-list reference before clearing it; the base window separately retires its child-list reference.
UiSlides::~UiSlides() {
  if (list)
    list->Release();
  list = nullptr;
}

bool UiSlides::QueryInterface(u32 iid, void **out) {
  if (iid != IID_UiSlidesControl)
    return UiBaseWindow::QueryInterface(iid, out);
  if (!out)
    RETURN_QUERY_INTERFACE_FAILURE();
  *out = static_cast<UiSlidesControl *>(this);
  AddRef();
  return true;
}

u32 UiSlides::AddRef() {
  return UiBaseWindow::AddRef();
}

u32 UiSlides::Release() {
  return UiBaseWindow::Release();
}

// Allocate and hold the inner list before sizing and attaching it; attachment supplies a separate child reference.
u8 UiSlides::CreateSlidesOnly() {
  if (!list) {
    list = ScAddRefNotNull(new UiSlideList);
    list->SetPicSize(100);
    ChildAdd(list);
  }
  return 1;
}

u8 UiSlides::AddOriginal(GeAttrib *original, OcOverride *replacement) {
  u8 result = CreateSlidesOnly();
  return result ? list->AddOriginal(original, replacement) : result;
}

u8 UiSlides::RemoveAllSlides() {
  return list ? list->RemoveAllSlides() : 0;
}

u8 UiSlides::ClearAllOverRides() {
  return list ? list->ClearAllOverRides() : 0;
}

// Capture the existing inner selection before reloading the live list and updating that row's replacement.
u8 UiSlides::SetOverRide(OcOverride *replacement) {
  if (!list)
    return 0;
  i32 selected = list->GetSelection();
  return list->SetOverRide((u32)selected, replacement);
}

i32 UiSlides::GetSelection() const {
  return list ? list->GetSelection() : -1;
}

u8 UiSlides::SetSelection(i32 selection) {
  return list ? list->SetSelection(selection) : 0;
}

u8 UiSlides::SetPicSize(u32 size) {
  return CreateSlidesOnly() ? list->SetPicSize(size) : 0;
}

u8 UiSlides::SetScrollIndent(u32 indent) {
  return list ? list->SetScrollIndent(indent) : 0;
}

// Add frame and button extents to the inner minimum, retaining its fixed maxima and the button-height minimum.
u8 UiSlides::GetMinMaxInfo(ScRect *limits) const {
  if (!list)
    return 0;
  ScRect inner;
  list->GetMinMaxInfo(&inner);
  limits->left = (i32)((u32)inner.left + (u32)inset_left + (u32)inset_right + (u32)button_width);
  i32 height = (i32)((u32)inner.top + (u32)inset_top + (u32)inset_bottom);
  limits->top = height < button_height ? button_height : height;
  limits->right = limits->bottom = 2000;
  return 1;
}

// Create a mode-three button, attach its freshly borrowed window and fit it before retiring the button view.
// Recalculate button extents after every attempt, including a failed class query or child attachment.
u8 UiSlides::AddButton(u32 button_id, GxBuffer *image, const ScRect &rectangle) {
  WnButton *button = nullptr;
  u8 result = 0;
  if (UiGetCom()->GetClassObject(kWnImageButtonClassId, IID_WnButton, reinterpret_cast<void **>(&button))) {
    button->GetWindow()->SetID(button_id);
    button->SetStyle(kWnButtonPushFixed, image, &rectangle);
    result = ChildAdd(button->GetWindow());
    button->Fit();
    button->Release();
  }
  CalcButtonArea();
  return result;
}

// Measure the four original scroll buttons in ID order, adding four pixels only to nonzero final extents.
u8 UiSlides::CalcButtonArea() {
  button_width = button_height = 0;
  for (u32 button_id = kUiSlidePreviousButtonId; button_id <= kUiSlideNextButtonId; ++button_id) {
    WnWindow *button = GetChildWindowFromID(button_id);
    if (button) {
      i32 width = button->GetW();
      if (width > button_width)
        button_width = width;
      button_height = (i32)((u32)button_height + (u32)button->GetH());
    }
  }
  if (button_width)
    button_width = (i32)((u32)button_width + 4);
  if (button_height)
    button_height = (i32)((u32)button_height + 4);
  return 1;
}

// Create the inner list and resource-backed scroll buttons once, then initialize and resize through the live area view.
// The source requires its resource manager; absence is an unavailable execution boundary rather than a game reply.
bool UiSlides::Init() {
  if (GetInitFlag())
    return true;
  CreateSlidesOnly();
  UiResourceManagerRef resources;
  assert(resources.manager);
  GxBuffer *image = nullptr;
  RsKey key(kRsImageResourceType, kRsUiImageGroup, 0);
  static const u32 images[][2] = {{kUiSlidePreviousImageId, kUiSlidePreviousButtonId},
      {kUiSlidePreviousPageImageId, kUiSlidePreviousPageButtonId}, {kUiSlideNextImageId, kUiSlideNextButtonId},
      {kUiSlideNextPageImageId, kUiSlideNextPageButtonId}};
  for (u32 i = 0; i < 4; ++i) {
    key.instance = images[i][0];
    if (resources.manager->GetResource(key, IID_GxBuffer, reinterpret_cast<void **>(&image))) {
      const ScRect *rectangle = image->GetBufferArea();
      AddButton(images[i][1], image, *rectangle);
      image->Release();
      image = nullptr;
    }
  }
  UiBaseWindow::Init();
  SetArea(*GetArea());
  return true;
}

// Enforce the live virtual minimum, size the inner viewport, then vertically center and place the four scroll buttons.
void UiSlides::SetArea(i32 left, i32 top, i32 right, i32 bottom) {
  ScRect limits;
  [[maybe_unused]] bool have_limits = static_cast<UiSlidesControl *>(this)->GetMinMaxInfo(&limits);
  assert(have_limits);
  i32 width = (i32)((u32)right - (u32)left), height = (i32)((u32)bottom - (u32)top);
  if (width <= limits.left)
    width = limits.left;
  if (height <= limits.top)
    height = limits.top;
  UiBaseWindow::SetArea(left, top, (i32)((u32)left + (u32)width), (i32)((u32)top + (u32)height));
  if (list) {
    i32 inner_bottom = (i32)((u32)GetH() - (u32)inset_bottom);
    i32 inner_right = (i32)((u32)GetW() - (u32)button_width - (u32)inset_right);
    list->SetArea(inset_left, inset_top, inner_right, inner_bottom);
  }
  u32 y = ((u32)height - (u32)button_height) / 2 + 2;
  for (u32 button_id = kUiSlidePreviousButtonId; button_id <= kUiSlideNextButtonId; ++button_id) {
    WnWindow *button = GetChildWindowFromID(button_id);
    if (button) {
      i32 x = (i32)(2u - (u32)button_width + (u32)GetW());
      button->GZWinMoveTo(x, (i32)y);
      y += (u32)button->GetH();
    }
  }
}

// Consume scroll-button hold messages by storing the command and original future tick, or clearing both on release.
// Other messages retain the common window's dispatch, including its virtual command hooks.
bool UiSlides::DoMessage(ScMessage *message) {
  if (message->type != kWnMsg_Command || (u32)message->data1 != 17)
    return UiBaseWindow::DoMessage(message);
  if ((u32)message->data3) {
    pending_command = (u32)message->data2;
    last_scroll = UiTicks() + 1000u;
  } else
    pending_command = last_scroll = 0;
  return true;
}

// Throttle scroll requests with the original unsigned tick subtraction and take one- or four-item selection steps.
// Notify the freshly fetched parent only after the inner selection accepts the request.
u8 UiSlides::OnScroll(u32 command) {
  if (!list)
    return 0;
  if (UiTicks() - last_scroll < 300)
    return 1;
  last_scroll = UiTicks();
  i32 selected = list->GetSelection(), target;
  switch (command) {
  case kUiSlidePreviousButtonId:
  case kUiSlidePreviousPageButtonId:
    target = (i32)((u32)selected - (command == kUiSlidePreviousButtonId ? 1u : 4u));
    if (target < 0)
      target = 0;
    break;
  case kUiSlideNextPageButtonId:
    target = (i32)((u32)selected + 4u);
    break;
  case kUiSlideNextButtonId:
    target = (i32)((u32)selected + 1u);
    break;
  default:
    return 1;
  }
  if (list->SetSelection(target)) {
    u32 id = GetID();
    WnWindow *target_window = GetParentWin();
    SendMsg(target_window, kWnMsg_Command, kUiSlideSelectionChangedCommand, id, 0);
  }
  return 1;
}

// Fill one-pixel axis-aligned spans against the live drawing clip; diagonal requests preserve the original zero byte.
u8 UiSlides::DrawLine(GxBuffer *target, u32 pixel, i32 x1, i32 y1, i32 x2, i32 y2) const {
  if (x1 == x2) {
    ScRect rectangle = {x1, y1, (i32)((u32)x1 + 1), y2};
    return target->Fill(pixel, rectangle, const_cast<ScRect *>(&draw_area));
  }
  if (y1 == y2) {
    ScRect rectangle = {x1, y1, x2, (i32)((u32)y1 + 1)};
    return target->Fill(pixel, rectangle, const_cast<ScRect *>(&draw_area));
  }
  return 0;
}

// Draw the two bevel rings with palette pairs selected by raised state, optionally filling the remaining interior.
// Preserve the helper's final unlock even though its caller acquired the surrounding paint lock.
u8 UiSlides::DrawOutline(GxBuffer *target, const ScRect &rectangle, bool raised, bool filled, u32 *palette) const {
  ScRect edges = rectangle;
  u32 first = palette[raised ? 0 : 1], second = palette[raised ? 1 : 0];
  u32 third = palette[raised ? 2 : 3], fourth = palette[raised ? 3 : 2];
  edges.right = (i32)((u32)edges.right - 1);
  DrawLine(target, first, edges.right, edges.top, edges.right, edges.bottom);
  edges.bottom = (i32)((u32)edges.bottom - 1);
  DrawLine(target, first, edges.left, edges.bottom, edges.right, edges.bottom);
  DrawLine(target, second, edges.left, edges.top, edges.right, edges.top);
  DrawLine(target, second, edges.left, edges.top, edges.left, edges.bottom);
  edges.top = (i32)((u32)edges.top + 1);
  edges.right = (i32)((u32)edges.right - 1);
  DrawLine(target, third, edges.right, edges.top, edges.right, edges.bottom);
  edges.left = (i32)((u32)edges.left + 1);
  edges.bottom = (i32)((u32)edges.bottom - 1);
  DrawLine(target, third, edges.left, edges.bottom, edges.right, edges.bottom);
  DrawLine(target, fourth, edges.left, edges.top, edges.right, edges.top);
  DrawLine(target, fourth, edges.left, edges.top, edges.left, edges.bottom);
  if (filled) {
    edges.left = (i32)((u32)edges.left + 1);
    edges.top = (i32)((u32)edges.top + 1);
    target->Fill(palette[4], edges, nullptr);
  }
  target->Unlock(GxLockFlags::Fill);
  return 1;
}

// Advance inner easing and held-button selection before acquiring the paint buffer and drawing three frame regions.
// Each outline retains its own source unlock, followed by the enclosing painter's final unlock.
bool UiSlides::GZPaint() {
  if (list)
    list->Tick();
  OnScroll(pending_command);
  if (!draw_buffer->Lock(GxLockFlags::Fill))
    return false;
  ScRect rectangle = draw_area;
  rectangle.right = (i32)((u32)rectangle.right - (u32)button_width);
  u32 pixel = GetFillColor();
  draw_buffer->Fill(pixel, rectangle, nullptr);
  DrawOutline(draw_buffer, rectangle, true, true, colors);
  rectangle.left = (i32)((u32)rectangle.left + (u32)inset_left - 2);
  rectangle.top = (i32)((u32)rectangle.top + (u32)inset_top - 2);
  rectangle.right = (i32)((u32)rectangle.right + 2u - (u32)inset_right);
  rectangle.bottom = (i32)((u32)rectangle.bottom + 2u - (u32)inset_bottom);
  DrawOutline(draw_buffer, rectangle, false, false, colors);
  rectangle.right = draw_area.right;
  rectangle.left = (i32)((u32)rectangle.right - (u32)button_width);
  rectangle.bottom = draw_area.bottom;
  rectangle.top = draw_area.top;
  DrawOutline(draw_buffer, rectangle, true, true, colors + 5);
  draw_buffer->Unlock(GxLockFlags::Fill);
  return true;
}

UiBulkSnapshotCommand::UiBulkSnapshotCommand() : snapshot_references(0) {
}

UiBulkSnapshotCommand::~UiBulkSnapshotCommand() {
}

// Publish the primary Unknown or bulk identity, or the separate command view, before the shared virtual retain.
bool UiBulkSnapshotCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, UiBulkSnapshotIdentity);
  HANDLE_QUERY_INTERFACE(IID_UiBulkSnapshotIdentity, UiBulkSnapshotIdentity);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the separate live count before retaining the SIMUI module, then return its possibly changed value.
u32 UiBulkSnapshotCommand::AddRef() {
  ++snapshot_references;
  UiGetDirector()->AddRef();
  return snapshot_references;
}

// Release the module before decrementing a nonzero live count, and delete the complete owner at zero.
u32 UiBulkSnapshotCommand::Release() {
  if (snapshot_references) {
    UiGetDirector()->RemoveRef();
    --snapshot_references;
  }
  u32 result = snapshot_references;
  if (!result)
    delete this;
  return result;
}

// Enumerate both source filename filters, sort the combined names and load each city through the real command service.
// Request its snapshot only for accepted loads; missing load commands refuse the batch, while an empty batch succeeds.
bool UiBulkSnapshotCommand::Execute() {
  ScString path;
  ApIniFile settings;
  ScString configuration, scratch;
  RsFileSystem *files = UiGetFileSystem();
  if (files) {
    const char *directory = files->AppDirectory();
    if (directory) {
      ScString copy(directory);
      configuration = copy;
    }
  }
  configuration.Append("SnapShot.ini");
  settings.SetPath(configuration);
  settings.ReadEntry("SnapShotSpecs", "BulkDirectory", &path);
  if (!path.Strlen())
    return true;
  ScDirectory directory(path, true, false, false);
  ScDirectoryNames names;
  {
    UiTextRef resource(0x7f, kRsCommonTextGroup);
    ScString text = resource.GetText(), copy(text), filter(copy);
    directory.SetNewDirectoryEntryFilter(filter, true);
  }
  if (directory.GetDirectoryEntryCount())
    directory.ReadNames(&names, 8);
  {
    ScString filter("*.SCT");
    directory.SetNewDirectoryEntryFilter(filter, true);
  }
  if (directory.GetDirectoryEntryCount())
    directory.ReadNames(&names, 8);
  names.Sort();
  for (u32 i = 0; i < names.entries.size; ++i) {
    ScString full_path(path);
    ScString *name = names.entries.data[i];
    full_path.Append(name->begin, static_cast<u32>(name->end - name->begin));
    UiCursorScope cursor(14);
    UiCommandInterface *load = nullptr;
    if (!UiGetCommandFactory()->GetCommand(
            kCtLoadCityCommandId, IID_UiCommandInterface, reinterpret_cast<void **>(&load)))
      return false;
    load->PushBackInput(new ScValue(kCtFileOpenCity, kScValueUint32));
    ScValue *quiet = new ScValue(false);
    quiet->AddRef();
    load->PushBackInput(quiet);
    quiet->Release();
    ScValue *filename = new ScValue(full_path);
    filename->AddRef();
    load->PushBackInput(filename);
    filename->Release();
    bool result = load->Execute();
    u8 accepted = 0;
    if (result && load->OutputQueueSize()) {
      ScVariant *output;
      load->PopFrontOutput(&output);
      memcpy(&accepted, output->AsBool(), sizeof(accepted));
      output->Release();
    }
    load->Release();
    if (accepted == 1 && UiGetCity()) {
      ScString destination;
      ScPath loaded(full_path);
      ScString *component = loaded.Drive();
      destination.Append(component->begin, static_cast<u32>(component->end - component->begin));
      component = &loaded.Directory();
      destination.Append(component->begin, static_cast<u32>(component->end - component->begin));
      component = &loaded.FileName();
      destination.Append(component->begin, static_cast<u32>(component->end - component->begin));
      UiCommandInterface *snapshot = nullptr;
      if (UiGetCommandFactory()->GetCommand(
              kUiSnapshotCommandClassId, IID_UiCommandInterface, reinterpret_cast<void **>(&snapshot))) {
        snapshot->Execute();
        snapshot->Release();
      }
    }
  }
  return true;
}

UiSnapshotCommand::UiSnapshotCommand() : snapshot_references(0) {
}

UiSnapshotCommand::~UiSnapshotCommand() {
}

// Publish the primary Unknown/graph-writer or separate command view, clearing the output.
bool UiSnapshotCommand::QueryInterface(u32 iid, void **out) {
  HANDLE_QUERY_INTERFACE(IID_ScUnknown, ScModelReceiver);
  HANDLE_QUERY_INTERFACE(IID_ScModelReceiver, ScModelReceiver);
  HANDLE_QUERY_INTERFACE(IID_UiCommandInterface, UiCommandInterface);
  RETURN_QUERY_INTERFACE_FAILURE();
}

// Increment the snapshot count independently of the inherited command count, then retain its module.
u32 UiSnapshotCommand::AddRef() {
  ++snapshot_references;
  UiGetDirector()->AddRef();
  return snapshot_references;
}

// Retire one module hold before decrementing the live snapshot count and deleting its file-owning object at zero.
u32 UiSnapshotCommand::Release() {
  if (snapshot_references) {
    UiGetDirector()->RemoveRef();
    --snapshot_references;
  }
  u32 result = snapshot_references;
  if (!result)
    delete this;
  return result;
}

// Forward the supplied query and primary snapshot identity to a present layer; absent layers still report success.
bool UiSnapshotCommand::Enumerate(ScMessage *message, CtLayer *layer) {
  if (layer)
    layer->DoQueryInfo(message, static_cast<ScModelReceiver *>(this));
  return true;
}

// Read the original dimensions and byte colors before querying the live city; no city is a genuine false result.
// Build the original destination root and pass the dimensions in the helper's height/width order.
bool UiSnapshotCommand::Execute() {
  ApIniFile settings;
  ScString configuration, value;
  RsFileSystem *files = UiGetFileSystem();
  if (files) {
    const char *directory = files->AppDirectory();
    if (directory) {
      ScString copy(directory);
      configuration = copy;
    }
  }
  configuration.Append("SnapShot.ini");
  settings.SetPath(configuration);
  const char *keys[] = {"Width", "Height", "BackgroundColor_R", "BackgroundColor_G", "BackgroundColor_B"};
  u32 values[] = {160, 160, 0, 0, 0};
  for (u32 i = 0; i < 5; ++i) {
    bool found = settings.ReadEntry("SnapShotSpecs", keys[i], &value);
    if (found)
      values[i] = ScConvertToUint32(value.begin);
  }
  u16 color = UiGetGraphics()->ConvertRGBValueToNative16Bit(
      static_cast<u8>(values[2]), static_cast<u8>(values[3]), static_cast<u8>(values[4]));
  CtCity *city = UiGetCity();
  if (!city)
    return false;
  ScString root;
  GetSnapshotDestPath(city, &root);
  return DoSnapshot(city, &root, values[1], values[0], color) != 0;
}

// Choose the saved city's directory and filename, falling back to the application data directory and localized name.
// Recheck the city's path after the directory existence query, as the original does.
u8 SC3_STDCALL UiSnapshotCommand::GetSnapshotDestPath(CtCity *city, ScStringView *out) {
  ScString root, saved(*city->GetFilePath());
  if (!saved.Strlen())
    UiGetApplication()->GetAppDataDirectory(&root);
  else {
    ScPath path(saved);
    root = *path.Drive();
    ScString &directory = path.Directory();
    root.Append(directory.begin, static_cast<u32>(directory.end - directory.begin));
  }
  ScPath directory(root);
  if (!directory.Exists()) {
    ScString fallback(UiGetFileSystem()->AppDirectory());
    root = fallback;
  }
  ScString current(*city->GetFilePath());
  if (!current.Strlen()) {
    UiTextRef resource(kRsGameNewCityText, kRsGameTextGroup);
    ScString text = resource.GetText(), copy(text), name(copy);
    root.Append(name.begin, static_cast<u32>(name.end - name.begin));
  } else {
    ScPath path(current);
    ScString &name = path.FileName();
    root.Append(name.begin, static_cast<u32>(name.end - name.begin));
  }
  out->FromChar(root.begin);
  return 1;
}

// Test a row against native black in a locked16-bit buffer. A refused lock retains the original true result.
// The final coordinate is intentionally inclusive; GetPixel owns the original out-of-bounds handling.
u8 SC3_STDCALL UiSnapshotCommand::IsBufferRowBlack(GxBuffer *buffer, i32 row) {
  if (row < 0 || static_cast<u32>(row) >= buffer->Height())
    return 0;
  if (buffer->GetBitsPerPixel() != 16)
    return 0;
  u32 black = buffer->ConvertRGBValueToNative16Bit(0, 0, 0);
  u8 result = 1;
  if (buffer->Lock(GxLockFlags::Bits)) {
    buffer->Width();
    u32 x = 0;
    for (;;) {
      if (buffer->GetPixel(x, static_cast<u32>(row)) != black) {
        result = 0;
        break;
      }
      if (++x > buffer->Width())
        break;
    }
    buffer->Unlock(GxLockFlags::Bits);
  }
  return result;
}

// Test a column against native black with the original lock-failure result and inclusive terminal coordinate.
// This uses the buffer's pixel operation rather than reading outside its backing allocation.
u8 SC3_STDCALL UiSnapshotCommand::IsBufferColumnBlack(GxBuffer *buffer, i32 column) {
  if (column < 0 || static_cast<u32>(column) >= buffer->Width())
    return 0;
  if (buffer->GetBitsPerPixel() != 16)
    return 0;
  u32 black = buffer->ConvertRGBValueToNative16Bit(0, 0, 0);
  u8 result = 1;
  if (buffer->Lock(GxLockFlags::Bits)) {
    buffer->Height();
    u32 y = 0;
    for (;;) {
      if (buffer->GetPixel(static_cast<u32>(column), y) != black) {
        result = 0;
        break;
      }
      if (++y > buffer->Height())
        break;
    }
    buffer->Unlock(GxLockFlags::Bits);
  }
  return result;
}

struct UiSnapshotTap {
  i32 index;
  f64 weight;
};

struct UiSnapshotContributions {
  i32 count;
  UiSnapshotTap *taps;
};

static GxUnpackFunction UiSnapshotUnpack;
static GxPackFunction UiSnapshotPack;

// Build an axis in the original shared tap allocation, retaining binary32 centers and the horizontal group stride.
static void UiSnapshotBuildContributions(UiSnapshotContributions *contributions, UiSnapshotTap *storage, i32 source,
    i32 target, f32 scale, i32 capacity, usize allocated) {
  f32 support = scale >= 1 ? 1 : static_cast<f32>(1.0 / scale);
  for (i32 destination = 0; destination < target; ++destination) {
    f64 center = static_cast<f64>(destination) / scale;
    f32 rounded_center = static_cast<f32>(center);
    i32 first = static_cast<i32>(ceil(center - support));
    i32 last = static_cast<i32>(floor(static_cast<f64>(rounded_center) + support));
    UiSnapshotContributions *entry = contributions + destination;
    entry->count = 0;
    usize offset = static_cast<usize>(destination) * capacity;
    entry->taps = offset <= allocated ? storage + offset : nullptr;
    for (i32 sample = first; sample <= last; ++sample) {
      f64 distance = (static_cast<f64>(rounded_center) - sample) / support;
      if (distance < 0)
        distance = -distance;
      f64 weight = distance >= 1 ? 0 : distance * ((distance + distance - 3) * distance) + 1;
      i32 reflected = sample < 0 ? -sample : sample >= source ? 2 * source - sample - 1 : sample;
      assert(offset < allocated && static_cast<usize>(entry->count) < allocated - offset);
      entry->taps[entry->count++] = {reflected, weight / support};
    }
  }
}

// Clamp the normalized binary32 channel before the original add-half and integer truncation.
static u8 UiSnapshotRoundChannel(f32 value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return static_cast<u8>(static_cast<f64>(value) + 0.5);
}

// Accumulate a16-bit pixel with binary32 sums, excluding color-key samples before the original0.4 coverage test.
// Zero-weight endpoints cannot affect the result and must not force unused pixel reads across a scanline boundary.
static u16 UiSnapshotFilterPixel(const u16 *pixels, u32 step, [[maybe_unused]] i64 first_index,
    [[maybe_unused]] i64 limit_index,
    const UiSnapshotContributions &contributions, bool transparent, u32 key, u16 transparent_pixel) {
  f32 red = 0, green = 0, blue = 0, total = 0;
  for (i32 sample = 0; sample < contributions.count; ++sample) {
    const UiSnapshotTap *tap = contributions.taps + sample;
    if (tap->weight == 0)
      continue;
    u8 r, g, b;
    assert(tap->index >= first_index && tap->index < limit_index);
    UiSnapshotUnpack(pixels[static_cast<isize>(tap->index) * static_cast<isize>(step)], &r, &g, &b);
    u32 rgb = (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) | b;
    if (!transparent || rgb != key) {
      red = static_cast<f32>(static_cast<f64>(r) * tap->weight + red);
      green = static_cast<f32>(static_cast<f64>(g) * tap->weight + green);
      blue = static_cast<f32>(static_cast<f64>(b) * tap->weight + blue);
      total = static_cast<f32>(tap->weight + total);
    }
  }
  if (total < 0.4f)
    return transparent_pixel;
  f64 reciprocal = 1.0 / total;
  red = static_cast<f32>(reciprocal * red);
  green = static_cast<f32>(reciprocal * green);
  blue = static_cast<f32>(reciprocal * blue);
  u16 result;
  UiSnapshotPack(&result, UiSnapshotRoundChannel(red), UiSnapshotRoundChannel(green), UiSnapshotRoundChannel(blue));
  return result;
}

// Resample a cropped16-bit buffer horizontally and then vertically through the source's actual converters.
// Preserve original lock ordering and raw storage deletion; the original never calls buffer Release or destruction here.
static GxBuffer *UiResampleImage(GxBuffer *source, f32 scale_x, f32 scale_y, const ScRect *crop) {
  if (crop && (crop->left >= crop->right || crop->top >= crop->bottom))
    return nullptr;
  UiSnapshotUnpack = source->Get16ToRGBConverterPtr();
  UiSnapshotPack = source->GetRGBTo16ConverterPtr();
  if (!UiSnapshotUnpack || !UiSnapshotPack)
    return nullptr;
  bool transparent = source->GetTransparency() != 0;
  u32 transparent_color = 0, key = 0;
  if (transparent) {
    source->GetTransparentColor(&transparent_color);
    u8 red, green, blue;
    UiSnapshotUnpack(static_cast<u16>(transparent_color), &red, &green, &blue);
    key = (static_cast<u32>(red) << 16) | (static_cast<u32>(green) << 8) | blue;
  }
  i32 left = crop ? crop->left : 0, top = crop ? crop->top : 0;
  i32 width = crop ? crop->right - crop->left : static_cast<i32>(source->Width());
  i32 height = crop ? crop->bottom - crop->top : static_cast<i32>(source->Height());
  f64 scaled_width = ceil(static_cast<f64>(width) * scale_x);
  f64 scaled_height = ceil(static_cast<f64>(height) * scale_y);
  i32 target_width = static_cast<i32>(static_cast<i64>(scaled_width + (scaled_width >= 0 ? 0.5 : -0.5)));
  i32 target_height = static_cast<i32>(static_cast<i64>(scaled_height + (scaled_height >= 0 ? 0.5 : -0.5)));
  f32 effective_x = static_cast<f32>(static_cast<f64>(target_width) / width);
  f32 effective_y = static_cast<f32>(static_cast<f64>(target_height) / height);
  if (effective_x == 0 || effective_y == 0)
    return nullptr;
  GxBuffer *output = nullptr, *intermediate = nullptr;
  UiSnapshotContributions *contributions = nullptr;
  UiSnapshotTap *storage = nullptr;
  bool failed = true;
  do {
    UiGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&output));
    if (!output ||
        !output->Init(target_width, target_height, GxColorType(GxColorType::Color::RGB16), nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false))
      break;
    UiGetCom()->GetClassObject(kGxBufferClassId, IID_GxBuffer, reinterpret_cast<void **>(&intermediate));
    if (!intermediate ||
        !intermediate->Init(target_width, height, GxColorType(GxColorType::Color::RGB16), nullptr,
            GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false))
      break;
    i32 count = target_width > target_height ? target_width : target_height;
    f32 support = effective_x >= 1 ? 1 : static_cast<f32>(1.0 / effective_x);
    i32 capacity = static_cast<i32>(2.0 * (static_cast<f64>(support) + 1.0));
    usize allocated = static_cast<usize>(count) * capacity;
    contributions = new UiSnapshotContributions[count];
    storage = new UiSnapshotTap[allocated];
    UiSnapshotBuildContributions(contributions, storage, width, target_width, effective_x, capacity, allocated);
    if (!source->Lock(GxLockFlags::Bits))
      break;
    if (!intermediate->Lock(GxLockFlags::Bits)) {
      source->Unlock(GxLockFlags::Bits);
      break;
    }
    auto *temporary = static_cast<u16 *>(intermediate->GetColorSurfaceBits());
    auto *input = static_cast<const u16 *>(source->GetColorSurfaceBits());
    u32 source_stride = source->GetColorSurfaceStride() >> 1;
    u32 temporary_stride = intermediate->GetColorSurfaceStride() >> 1;
    i32 temporary_width = static_cast<i32>(intermediate->Width());
    i32 temporary_height = static_cast<i32>(intermediate->Height());
    i64 source_offset = i64(source_stride) * top + left, source_cells = i64(source_stride) * source->Height();
    input += left + static_cast<isize>(source_stride) * top;
    for (i32 row = 0; row < temporary_height; ++row) {
      for (i32 column = 0; column < temporary_width; ++column)
        temporary[column] = UiSnapshotFilterPixel(
            input, 1, -source_offset, source_cells - source_offset, contributions[column], transparent, key,
            static_cast<u16>(transparent_color));
      input += source_stride;
      source_offset += source_stride;
      temporary += temporary_stride;
    }
    source->Unlock(GxLockFlags::Bits);
    UiSnapshotBuildContributions(
        contributions, storage, temporary_height, target_height, effective_y, capacity, allocated);
    if (!output->Lock(GxLockFlags::Bits))
      break;
    temporary_stride = intermediate->GetColorSurfaceStride() >> 1;
    u32 output_stride = output->GetColorSurfaceStride() >> 1;
    auto *pixels = static_cast<u16 *>(output->GetColorSurfaceBits());
    input = static_cast<const u16 *>(intermediate->GetColorSurfaceBits());
    for (i32 column = 0; column < target_width; ++column)
      for (i32 row = 0; row < target_height; ++row)
        pixels[static_cast<usize>(row) * output_stride + column] = UiSnapshotFilterPixel(input + column,
            temporary_stride, 0, temporary_height, contributions[row], transparent, key,
            static_cast<u16>(transparent_color));
    intermediate->Unlock(GxLockFlags::Bits);
    output->Unlock(GxLockFlags::Bits);
    failed = false;
  } while (false);
  delete[] contributions;
  delete[] storage;
  ::operator delete(intermediate);
  if (failed) {
    ::operator delete(output);
    return nullptr;
  }
  return output;
}

// Capture the original256-pixel city tiles and restore the displayed background, zoom and view origin before scaling.
u8 UiSnapshotCommand::GetCityThumbnail(GxBuffer **out, u32 width, u32 height, u16 color) {
  capture_buffer = nullptr;
  GxGimexImage *writer = nullptr;
  UiGetCom()->GetClassObject(kGxGimexImageClassId, IID_GxGimexImage, reinterpret_cast<void **>(&writer));
  if (!writer)
    return 0;
  SpCityWindowView *window = UiGetApplication()->GetISC3WinCityView();
  SpIsoView *view = window->GetIsoView();
  assert(view);
  UiGetCity()->SimHiddenPause();
  SpCellMap *map = view->GetCitySpriteCellMap();
  ScRect saved_view;
  map->GetViewRect(&saved_view);
  i32 zoom = map->GetZoom();
  GxBuffer *small = nullptr, *saved = nullptr;
  UiGetGraphics()->CreateBuffer(&small);
  small->Init(16, 16, GxColorType(GxColorType::Color::RGB16), nullptr, GxDepthType(GxDepthType::Depth::None),
      GxVramRequest::SystemMemory, false);
  UiGetGraphics()->CreateBuffer(&saved);
  if (!small->Lock(GxLockFlags::Fill)) {
    if (small)
      small->Release();
    if (saved)
      saved->Release();
    UiGetCity()->SimHiddenUnpause();
    return 0;
  }
  small->FillNoClip(color);
  small->Unlock(GxLockFlags::Fill);
  GxBuffer *background = nullptr;
  map->GetBackgroundBuffer(&background);
  u32 saved_height = background->Height(), saved_width = background->Width();
  saved->Init(saved_width, saved_height, GxColorType(GxColorType::Color::RGB16), nullptr,
      GxDepthType(GxDepthType::Depth::None), GxVramRequest::SystemMemory, false);
  if (!saved->Lock(GxLockFlags::Blt)) {
    if (small)
      small->Release();
    if (saved)
      saved->Release();
    window->AnimationPause(false);
    UiGetCity()->SimHiddenUnpause();
    return 0;
  }
  const ScRect *destination = saved->GetBufferArea(), *source = background->GetBufferArea();
  saved->Blt(background, *source, *destination, static_cast<const ScRect *>(nullptr));
  saved->Unlock(GxLockFlags::Blt);
  map->SetBackgroundBuffer(small, false);
  if (!map->GetCityBufferPtr())
    return 0;
  map->SetZoom(0, true, INT32_MAX, INT32_MAX);
  map->EnableBackgroundTask(false, false);
  map->LoadZoomAndRotation(0, map->GetRotate());
  map->UpdateCitySurfaceAll();
  ScRect tile = {0, 0, 256, 256};
  i32 extent = 0;
  switch (UiGetCity()->CellCountX()) {
  case 64:
  case 128:
    extent = 512;
    break;
  case 192:
    extent = 768;
    break;
  case 256:
    extent = 1024;
    break;
  default:
    break;
  }
  UiGetGraphics()->CreateBuffer(&capture_buffer);
  assert(extent && "city size must have defined tile bounds");
  capture_buffer->Init(static_cast<u32>(extent * 2), static_cast<u32>(extent + 256),
      GxColorType(GxColorType::Color::RGB16), nullptr, GxDepthType(GxDepthType::Depth::None),
      GxVramRequest::SystemMemory, true);
  for (i32 x = -extent, column = 0; x <= extent; x += 256, ++column) {
    for (i32 y = -256, top = 0; y <= extent; y += 256, top += 256) {
      map->MoveAbsolute(x, y, true);
      map->UpdateCitySurfaceAll();
      GxBuffer *city = map->GetCityBufferPtr();
      if (capture_buffer->Lock(GxLockFlags::Blt)) {
        ScRect target = {column * 256, top, (column + 1) * 256, top + 256};
        capture_buffer->Blt(city, tile, target, static_cast<const ScRect *>(nullptr));
        capture_buffer->Unlock(GxLockFlags::Blt);
      }
    }
  }
  map->SetBackgroundBuffer(saved, false);
  map->SetZoom(zoom, false, INT32_MAX, INT32_MAX);
  map->MoveAbsolute(saved_view.left, saved_view.top, true);
  map->UpdateCitySurfaceAll();
  ScRect crop = {0, 0, static_cast<i32>(capture_buffer->Width()) - 1, static_cast<i32>(capture_buffer->Height()) - 1};
  for (i32 x = 0; x <= crop.right; ++x) {
    if (!IsBufferColumnBlack(capture_buffer, x)) {
      crop.left = x;
      break;
    }
  }
  for (i32 x = crop.right; x >= crop.left; --x) {
    if (!IsBufferColumnBlack(capture_buffer, x)) {
      crop.right = x;
      break;
    }
  }
  for (i32 y = 0; y <= crop.bottom; ++y) {
    if (!IsBufferRowBlack(capture_buffer, y)) {
      crop.top = y;
      break;
    }
  }
  for (i32 y = crop.bottom; y >= 0; --y) {
    if (!IsBufferRowBlack(capture_buffer, y)) {
      crop.bottom = y;
      break;
    }
  }
  f32 scale_x = static_cast<f32>(static_cast<f64>(width) / capture_buffer->Width());
  f32 scale_y = static_cast<f32>(static_cast<f64>(height) / capture_buffer->Height());
  GxBuffer *scaled = UiResampleImage(capture_buffer, scale_x, scale_y, &crop);
  *out = scaled;
  assert(scaled);
  if ((*out)->Lock(GxLockFlags::Bits)) {
    if ((*out)->GetColorType().depth == 16) {
      auto *bits = static_cast<u8 *>((*out)->GetColorSurfaceBits());
      u32 stride = ((*out)->GetColorSurfaceStride() >> 1) * 2;
      u32 columns = (*out)->Width(), rows = (*out)->Height();
      GxPackFunction pack = (*out)->GetRGBTo16ConverterPtr();
      GxUnpackFunction unpack = (*out)->Get16ToRGBConverterPtr();
      for (u32 y = 0; y < rows; ++y) {
        auto *pixels = reinterpret_cast<u16 *>(bits + y * stride);
        for (u32 x = 0; x < columns; ++x) {
          u8 red, green, blue;
          unpack(pixels[x], &red, &green, &blue);
          red = static_cast<u8>((red > 25 ? red - 25 : 0) * 1.1086956);
          green = static_cast<u8>((green > 25 ? green - 25 : 0) * 1.1086956);
          blue = static_cast<u8>((blue > 25 ? blue - 25 : 0) * 1.1086956);
          u8 minimum = red < green ? red : green;
          if (blue < minimum)
            minimum = blue;
          f64 r = (red - minimum) * 1.25 + minimum;
          f64 g = (green - minimum) * 1.25 + minimum;
          f64 b = (blue - minimum) * 1.25 + minimum;
          red = r <= 255.0 ? static_cast<u8>(r) : 255;
          green = g <= 255.0 ? static_cast<u8>(g) : 255;
          blue = b <= 255.0 ? static_cast<u8>(b) : 255;
          pack(&pixels[x], red, green, blue);
        }
      }
    }
    (*out)->Unlock(GxLockFlags::Bits);
  }
  ScRelease(&capture_buffer);
  UiGetCity()->SimHiddenUnpause();
  if (writer)
    writer->Release();
  return 1;
}

// Write the captured bitmap, then export the city graph header and enumerate its layers through the original message.
u8 UiSnapshotCommand::DoSnapshot(CtCity *city, ScStringView *root, u32 height, u32 width, u16 color) {
  if (!city)
    return 0;
  u8 result = 0;
  ScString output;
  UiCursorScope cursor(14);
  GxGimexImage *writer = nullptr;
  UiGetCom()->GetClassObject(kGxGimexImageClassId, IID_GxGimexImage, reinterpret_cast<void **>(&writer));
  if (writer) {
    GxBuffer *buffer = nullptr;
    if (!GetCityThumbnail(&buffer, width, height, color))
      return 0;
    if (buffer) {
      {
        ScString copy(*root);
        output = copy;
      }
      output.Append(".BMP");
      result = writer->SaveToFile(output, buffer, 100);
      buffer->Release();
    }
    writer->Release();
  }
  {
    ScString copy(*root);
    output = copy;
  }
  output.Append(".TXT");
  file.SetName(output);
  if (file.Open(kRsFileAccessWrite, kRsFileCreateAlways, kRsFileShareRead)) {
    {
      ScString header("Graph Data (12 months, 10 years, 10 decades):\r\n");
      output = header;
    }
    file.Write(output.begin, static_cast<u32>(output.end - output.begin));
    ScMessage message = {kUiMsg_CollectSnapshotGraphData, kUiSnapshotCommandClassId, 0, 0};
    city->DoEnumLayers(&message, static_cast<CtCityEnumerator *>(this));
    file.Close();
  }
  return result;
}

u8 UiSnapshotCommand::DumpGraphData(ScGraphInfoView *model) {
  ScString output;
  {
    ScString label(*model->GetSelectionLabel());
    output = label;
  }
  output.Append(":\r\n");
  file.Write(output.begin, static_cast<u32>(output.end - output.begin));
  u32 months[12], years[10], decades[10];
  model->GetMonthDataSet(months);
  model->GetYearDataSet(years);
  model->GetDecadeDataSet(decades);
  output.Sprintf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n", static_cast<i32>(months[0]), static_cast<i32>(months[1]),
      static_cast<i32>(months[2]), static_cast<i32>(months[3]), static_cast<i32>(months[4]),
      static_cast<i32>(months[5]), static_cast<i32>(months[6]), static_cast<i32>(months[7]),
      static_cast<i32>(months[8]), static_cast<i32>(months[9]), static_cast<i32>(months[10]),
      static_cast<i32>(months[11]));
  file.Write(output.begin, static_cast<u32>(output.end - output.begin));
  output.Sprintf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n", static_cast<i32>(years[0]), static_cast<i32>(years[1]),
      static_cast<i32>(years[2]), static_cast<i32>(years[3]), static_cast<i32>(years[4]), static_cast<i32>(years[5]),
      static_cast<i32>(years[6]), static_cast<i32>(years[7]), static_cast<i32>(years[8]), static_cast<i32>(years[9]));
  file.Write(output.begin, static_cast<u32>(output.end - output.begin));
  output.Sprintf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n", static_cast<i32>(decades[0]), static_cast<i32>(decades[1]),
      static_cast<i32>(decades[2]), static_cast<i32>(decades[3]), static_cast<i32>(decades[4]),
      static_cast<i32>(decades[5]), static_cast<i32>(decades[6]), static_cast<i32>(decades[7]),
      static_cast<i32>(decades[8]), static_cast<i32>(decades[9]));
  file.Write(output.begin, static_cast<u32>(output.end - output.begin));
  return 1;
}
